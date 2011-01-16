/*
    HTTP Virtual Disk.
    Copyright (C) 2006 Bo Brantén.
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <windows.h>
#include <winioctl.h>
#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>

/* Spoof these types for the #include to succeed. */
typedef char KEVENT, WVL_S_BUS_NODE, WVL_S_DISK_T;
#include "httpdisk.h"

int HttpDiskSyntax(void)
{
    fprintf(stderr, "syntax:\n");
    fprintf(stderr, "httpdisk /mount  <url> [/cd]\n");
    fprintf(stderr, "httpdisk /umount <unit_num>\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "example:\n");
    fprintf(stderr, "httpdisk /mount  http://server.domain.com/path/diskimage.img\n");
    fprintf(stderr, "httpdisk /mount  http://server.domain.com/path/cdimage.iso /cd\n");
    fprintf(stderr, "...\n");
    fprintf(stderr, "httpdisk /umount 0\n");
    fprintf(stderr, "httpdisk /umount 1\n");

    return -1;
}

void PrintLastError(char* Prefix)
{
    LPVOID lpMsgBuf;

    FormatMessage( 
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        GetLastError(),
        0,
        (LPTSTR) &lpMsgBuf,
        0,
        NULL
        );

    fprintf(stderr, "%s: %s", Prefix, (LPTSTR) lpMsgBuf);

    LocalFree(lpMsgBuf);
}

static char HTTPDiskBus[] = "\\\\.\\HTTPDisk";

int
HttpDiskMount(
    PHTTP_DISK_INFORMATION  HttpDiskInformation
)
{
    HANDLE  Device;
    DWORD   BytesReturned;

    printf("Trying to connect...\n");
    Device = CreateFile(
        HTTPDiskBus,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING,
        NULL
        );

    if (Device == INVALID_HANDLE_VALUE)
    {
        PrintLastError("CreateFile()");
        return -1;
    }

    if (!DeviceIoControl(
        Device,
        IOCTL_HTTP_DISK_CONNECT,
        HttpDiskInformation,
        sizeof *HttpDiskInformation + HttpDiskInformation->FileNameLength - 1,
        NULL,
        0,
        &BytesReturned,
        NULL
        ))
    {
        PrintLastError("HttpDisk");
        return -1;
    }

    return 0;
}

int HttpDiskUmount(int DeviceNumber)
{
    HANDLE  Device;
    DWORD   BytesReturned;

    Device = CreateFile(
        HTTPDiskBus,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING,
        NULL
        );

    if (Device == INVALID_HANDLE_VALUE)
    {
        PrintLastError("CreateFile()");
        return -1;
    }

    if (!DeviceIoControl(
        Device,
        IOCTL_HTTP_DISK_DISCONNECT,
        &DeviceNumber,
        sizeof DeviceNumber,
        NULL,
        0,
        &BytesReturned,
        NULL
        ))
    {
        PrintLastError("HttpDisk");
        return -1;
    }

    CloseHandle(Device);

    return 0;
}

int __cdecl main(int argc, char* argv[])
{
    char*                   Command;
    int                     DeviceNumber;
    char*                   Url;
    char*                   Option;
    PHTTP_DISK_INFORMATION  HttpDiskInformation;
    char*                   FileName;
    char*                   PortStr;
    struct hostent*         HostEnt;
    WSADATA                 wsaData;
    int                     rc;

    Command = argv[1];

    if ((argc == 3 || argc == 4) && !strcmp(Command, "/mount"))
    {
        Url = argv[2];

        HttpDiskInformation = malloc(sizeof *HttpDiskInformation + strlen(Url));

        memset(
            HttpDiskInformation,
            0,
            sizeof *HttpDiskInformation + strlen(Url)
          );

        if (argc > 3)
        {
            Option = argv[3];

            if (!strcmp(Option, "/cd"))
            {
                HttpDiskInformation->Optical = TRUE;
            }
            else
            {
                free(HttpDiskInformation);
                return HttpDiskSyntax();
            }
        }
        else
        {
            Option = NULL;
            HttpDiskInformation->Optical = FALSE;
        }

        if (strstr(Url, "//"))
        {
            if (strlen(Url) > 7 && !strncmp(Url, "http://", 7))
            {
                Url += 7;
            }
            else
            {
                fprintf(stderr, "Invalid protocol.\n");
                free(HttpDiskInformation);
                return -1;
            }
        }

        FileName = strstr(Url, "/");

        if (!FileName)
        {
            fprintf(stderr, "%s: Invalid url.\n", Url);
            free(HttpDiskInformation);
            return -1;
        }

        strcpy(HttpDiskInformation->FileName, FileName);

        HttpDiskInformation->FileNameLength = (USHORT) strlen(HttpDiskInformation->FileName);

        *FileName = '\0';

        PortStr = strstr(Url, ":");

        if (PortStr)
        {
            HttpDiskInformation->Port = htons((USHORT) atoi(PortStr + 1));

            if (HttpDiskInformation->Port == 0)
            {
                fprintf(stderr, "%s: Invalid port.\n", PortStr + 1);
                free(HttpDiskInformation);
                return -1;
            }

            *PortStr = '\0';
        }
        else
        {
            HttpDiskInformation->Port = htons(80);
        }

        HttpDiskInformation->HostNameLength = (USHORT) strlen(Url);

        if (HttpDiskInformation->HostNameLength > 255)
        {
            fprintf(stderr, "%s: Host name to long.\n", Url);
            free(HttpDiskInformation);
            return -1;
        }

        strcpy(HttpDiskInformation->HostName, Url);

        HttpDiskInformation->Address = inet_addr(Url);

        if (HttpDiskInformation->Address == INADDR_NONE)
        {
            if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0)
            {
                PrintLastError("HttpDisk");
                free(HttpDiskInformation);
                return -1;
            }

            HostEnt = gethostbyname(Url);

            if (!HostEnt)
            {
                PrintLastError(Url);
                free(HttpDiskInformation);
                return -1;
            }

            HttpDiskInformation->Address = ((struct in_addr*) HostEnt->h_addr)->s_addr;
        }

        rc = HttpDiskMount(HttpDiskInformation);
        free(HttpDiskInformation);
        return rc;
    }
    else if (argc == 3 && !strcmp(Command, "/umount"))
    {
        DeviceNumber = atoi(argv[2]);
        return HttpDiskUmount(DeviceNumber);
    }
    else
    {
        return HttpDiskSyntax();
    }
}
