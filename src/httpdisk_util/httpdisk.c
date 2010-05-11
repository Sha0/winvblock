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
#include "httpdisk.h"

int HttpDiskSyntax(void)
{
    fprintf(stderr, "syntax:\n");
    fprintf(stderr, "httpdisk /mount  <devicenumber> <url> [/cd] <drive:>\n");
    fprintf(stderr, "httpdisk /umount <drive:>\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "example:\n");
    fprintf(stderr, "httpdisk /mount  0 http://server.domain.com/path/diskimage.img d:\n");
    fprintf(stderr, "httpdisk /mount  1 http://server.domain.com/path/cdimage.iso /cd e:\n");
    fprintf(stderr, "...\n");
    fprintf(stderr, "httpdisk /umount d:\n");
    fprintf(stderr, "httpdisk /umount e:\n");

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

int
HttpDiskMount(
    int                     DeviceNumber,
    PHTTP_DISK_INFORMATION  HttpDiskInformation,
    char                    DriveLetter,
    BOOLEAN                 CdImage
)
{
    char    VolumeName[] = "\\\\.\\ :";
    char    DeviceName[255];
    HANDLE  Device;
    DWORD   BytesReturned;

    VolumeName[4] = DriveLetter;

    Device = CreateFile(
        VolumeName,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING,
        NULL
        );

    if (Device != INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_BUSY);
        PrintLastError(&VolumeName[4]);
        return -1;
    }

    if (CdImage)
    {
        sprintf(DeviceName, DEVICE_NAME_PREFIX "Cd" "%u", DeviceNumber);
    }
    else
    {
        sprintf(DeviceName, DEVICE_NAME_PREFIX "Disk" "%u", DeviceNumber);
    }

    if (!DefineDosDevice(
        DDD_RAW_TARGET_PATH,
        &VolumeName[4],
        DeviceName
        ))
    {
        PrintLastError(&VolumeName[4]);
        return -1;
    }

    Device = CreateFile(
        VolumeName,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING,
        NULL
        );

    if (Device == INVALID_HANDLE_VALUE)
    {
        PrintLastError(&VolumeName[4]);
        DefineDosDevice(DDD_REMOVE_DEFINITION, &VolumeName[4], NULL);
        return -1;
    }

    if (!DeviceIoControl(
        Device,
        IOCTL_HTTP_DISK_CONNECT,
        HttpDiskInformation,
        sizeof(HTTP_DISK_INFORMATION) + HttpDiskInformation->FileNameLength - 1,
        NULL,
        0,
        &BytesReturned,
        NULL
        ))
    {
        PrintLastError("HttpDisk");
        DefineDosDevice(DDD_REMOVE_DEFINITION, &VolumeName[4], NULL);
        return -1;
    }

    return 0;
}

int HttpDiskUmount(char DriveLetter)
{
    char    VolumeName[] = "\\\\.\\ :";
    HANDLE  Device;
    DWORD   BytesReturned;

    VolumeName[4] = DriveLetter;

    Device = CreateFile(
        VolumeName,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING,
        NULL
        );

    if (Device == INVALID_HANDLE_VALUE)
    {
        PrintLastError(&VolumeName[4]);
        return -1;
    }

    if (!DeviceIoControl(
        Device,
        FSCTL_LOCK_VOLUME,
        NULL,
        0,
        NULL,
        0,
        &BytesReturned,
        NULL
        ))
    {
        PrintLastError(&VolumeName[4]);
        return -1;
    }

    if (!DeviceIoControl(
        Device,
        IOCTL_HTTP_DISK_DISCONNECT,
        NULL,
        0,
        NULL,
        0,
        &BytesReturned,
        NULL
        ))
    {
        PrintLastError("HttpDisk");
        return -1;
    }

    if (!DeviceIoControl(
        Device,
        FSCTL_DISMOUNT_VOLUME,
        NULL,
        0,
        NULL,
        0,
        &BytesReturned,
        NULL
        ))
    {
        PrintLastError(&VolumeName[4]);
        return -1;
    }

    if (!DeviceIoControl(
        Device,
        FSCTL_UNLOCK_VOLUME,
        NULL,
        0,
        NULL,
        0,
        &BytesReturned,
        NULL
        ))
    {
        PrintLastError(&VolumeName[4]);
        return -1;
    }

    CloseHandle(Device);

    if (!DefineDosDevice(
        DDD_REMOVE_DEFINITION,
        &VolumeName[4],
        NULL
        ))
    {
        PrintLastError(&VolumeName[4]);
        return -1;
    }

    return 0;
}

int __cdecl main(int argc, char* argv[])
{
    char*                   Command;
    int                     DeviceNumber;
    char*                   Url;
    char*                   Option;
    char                    DriveLetter;
    BOOLEAN                 CdImage;
    PHTTP_DISK_INFORMATION  HttpDiskInformation;
    char*                   FileName;
    char*                   PortStr;
    struct hostent*         HostEnt;
    WSADATA                 wsaData;

    Command = argv[1];

    if ((argc == 5 || argc == 6) && !strcmp(Command, "/mount"))
    {
        DeviceNumber = atoi(argv[2]);
        Url = argv[3];

        if (argc > 5)
        {
            Option = argv[4];
            DriveLetter = argv[5][0];

            if (!strcmp(Option, "/cd"))
            {
                CdImage = TRUE;
            }
            else
            {
                return HttpDiskSyntax();
            }
        }
        else
        {
            Option = NULL;
            DriveLetter = argv[4][0];
            CdImage = FALSE;
        }

        HttpDiskInformation = malloc(sizeof(HTTP_DISK_INFORMATION) + strlen(Url));

        memset(
            HttpDiskInformation,
            0,
            sizeof(HTTP_DISK_INFORMATION) + strlen(Url)
            );

        if (strstr(Url, "//"))
        {
            if (strlen(Url) > 7 && !strncmp(Url, "http://", 7))
            {
                Url += 7;
            }
            else
            {
                fprintf(stderr, "Invalid protocol.\n");
                return -1;
            }
        }

        FileName = strstr(Url, "/");

        if (!FileName)
        {
            fprintf(stderr, "%s: Invalid url.\n", Url);
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
            return -1;
        }

        strcpy(HttpDiskInformation->HostName, Url);

        HttpDiskInformation->Address = inet_addr(Url);

        if (HttpDiskInformation->Address == INADDR_NONE)
        {
            if (WSAStartup(MAKEWORD(1, 1), &wsaData) != 0)
            {
                PrintLastError("HttpDisk");
                return -1;
            }

            HostEnt = gethostbyname(Url);

            if (!HostEnt)
            {
                PrintLastError(Url);
                return -1;
            }

            HttpDiskInformation->Address = ((struct in_addr*) HostEnt->h_addr)->s_addr;
        }

        return HttpDiskMount(DeviceNumber, HttpDiskInformation, DriveLetter, CdImage);
    }
    else if (argc == 3 && !strcmp(Command, "/umount"))
    {
        DriveLetter = argv[2][0];
        return HttpDiskUmount(DriveLetter);
    }
    else
    {
        return HttpDiskSyntax();
    }
}
