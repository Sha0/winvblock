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

#include <ntddk.h>
#include <tdikrnl.h>
#include "ktdi.h"
#include "ksocket.h"

/* From httpdisk.c */
extern PVOID HttpDiskMalloc(SIZE_T);
extern PVOID HttpDiskPalloc(SIZE_T);

typedef struct _STREAM_SOCKET {
    HANDLE              connectionHandle;
    PFILE_OBJECT        connectionFileObject;
    KEVENT              disconnectEvent;
} STREAM_SOCKET, *PSTREAM_SOCKET;

typedef struct _SOCKET {
    int                 type;
    BOOLEAN             isBound;
    BOOLEAN             isConnected;
    BOOLEAN             isListening;
    BOOLEAN             isShuttingdown;
    BOOLEAN             isShared;
    HANDLE              addressHandle;
    PFILE_OBJECT        addressFileObject;
    PSTREAM_SOCKET      streamSocket;
    struct sockaddr     peer;
} SOCKET, *PSOCKET;

NTSTATUS event_disconnect(PVOID TdiEventContext, CONNECTION_CONTEXT ConnectionContext, LONG DisconnectDataLength,
                          PVOID DisconnectData, LONG DisconnectInformationLength, PVOID DisconnectInformation,
                          ULONG DisconnectFlags)
{
    PSOCKET s = (PSOCKET) TdiEventContext;
    PSTREAM_SOCKET streamSocket = (PSTREAM_SOCKET) ConnectionContext;
    KeSetEvent(&streamSocket->disconnectEvent, 0, FALSE);
    return STATUS_SUCCESS;
}

int __cdecl accept(int socket, struct sockaddr *addr, int *addrlen)
{
    return -1;
}

int __cdecl bind(int socket, const struct sockaddr *addr, int addrlen)
{
    PSOCKET s = (PSOCKET) -socket;
    const struct sockaddr_in* localAddr = (const struct sockaddr_in*) addr;
    UNICODE_STRING devName;
    NTSTATUS status;

    if (s->isBound || addr == NULL || addrlen < sizeof(struct sockaddr_in))
    {
        return -1;
    }

    if (s->type == SOCK_DGRAM)
    {
        RtlInitUnicodeString(&devName, L"\\Device\\Udp");
    }
    else if (s->type == SOCK_STREAM)
    {
        RtlInitUnicodeString(&devName, L"\\Device\\Tcp");
    }
    else
    {
        return -1;
    }

    status = tdi_open_transport_address(
        &devName,
        localAddr->sin_addr.s_addr,
        localAddr->sin_port,
        s->isShared,
        &s->addressHandle,
        &s->addressFileObject
        );

    if (!NT_SUCCESS(status))
    {
        s->addressFileObject = NULL;
        s->addressHandle = (HANDLE) -1;
        return status;
    }

    if (s->type == SOCK_STREAM)
    {
        tdi_set_event_handler(s->addressFileObject, TDI_EVENT_DISCONNECT, event_disconnect, s);
    }

    s->isBound = TRUE;

    return 0;
}

int __cdecl close(int socket)
{
    PSOCKET s = (PSOCKET) -socket;

    if (s->isBound)
    {
        if (s->type == SOCK_STREAM && s->streamSocket)
        {
            if (s->isConnected)
            {
                if (!s->isShuttingdown)
                {
                    tdi_disconnect(s->streamSocket->connectionFileObject, TDI_DISCONNECT_RELEASE);
                }
                //KeWaitForSingleObject(&s->streamSocket->disconnectEvent, Executive, KernelMode, FALSE, NULL);
            }
            if (s->streamSocket->connectionFileObject)
            {
                tdi_disassociate_address(s->streamSocket->connectionFileObject);
                ObDereferenceObject(s->streamSocket->connectionFileObject);
            }
            if (s->streamSocket->connectionHandle != (HANDLE) -1)
            {
                ZwClose(s->streamSocket->connectionHandle);
            }
            ExFreePool(s->streamSocket);
        }

        if (s->type == SOCK_DGRAM || s->type == SOCK_STREAM)
        {
            ObDereferenceObject(s->addressFileObject);
            if (s->addressHandle != (HANDLE) -1)
            {
                ZwClose(s->addressHandle);
            }
        }
    }

    ExFreePool(s);

    return 0;
}

int __cdecl connect(int socket, const struct sockaddr *addr, int addrlen)
{
    PSOCKET s = (PSOCKET) -socket;
    const struct sockaddr_in* remoteAddr = (const struct sockaddr_in*) addr;
    UNICODE_STRING devName;
    NTSTATUS status;

    if (addr == NULL || addrlen < sizeof(struct sockaddr_in))
    {
        return -1;
    }

    if (!s->isBound)
    {
        struct sockaddr_in localAddr;

        localAddr.sin_family = AF_INET;
        localAddr.sin_port = 0;
        localAddr.sin_addr.s_addr = INADDR_ANY;

        status = bind(socket, (struct sockaddr*) &localAddr, sizeof(localAddr));

        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

    if (s->type == SOCK_STREAM)
    {
        if (s->isConnected || s->isListening)
        {
            return -1;
        }

        if (!s->streamSocket)
        {
            s->streamSocket = HttpDiskMalloc(sizeof *s->streamSocket);

            if (!s->streamSocket)
            {
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            RtlZeroMemory(s->streamSocket, sizeof(STREAM_SOCKET));
            s->streamSocket->connectionHandle = (HANDLE) -1;
            KeInitializeEvent(&s->streamSocket->disconnectEvent, NotificationEvent, FALSE);
        }

        RtlInitUnicodeString(&devName, L"\\Device\\Tcp");

        status = tdi_open_connection_endpoint(
            &devName,
            s->streamSocket,
            s->isShared,
            &s->streamSocket->connectionHandle,
            &s->streamSocket->connectionFileObject
            );

        if (!NT_SUCCESS(status))
        {
            s->streamSocket->connectionFileObject = NULL;
            s->streamSocket->connectionHandle = (HANDLE) -1;
            return status;
        }

        status = tdi_associate_address(s->streamSocket->connectionFileObject, s->addressHandle);

        if (!NT_SUCCESS(status))
        {
            ObDereferenceObject(s->streamSocket->connectionFileObject);
            s->streamSocket->connectionFileObject = NULL;
            ZwClose(s->streamSocket->connectionHandle);
            s->streamSocket->connectionHandle = (HANDLE) -1;
            return status;
        }

        status = tdi_connect(
            s->streamSocket->connectionFileObject,
            remoteAddr->sin_addr.s_addr,
            remoteAddr->sin_port
            );

        if (!NT_SUCCESS(status))
        {
            tdi_disassociate_address(s->streamSocket->connectionFileObject);
            ObDereferenceObject(s->streamSocket->connectionFileObject);
            s->streamSocket->connectionFileObject = NULL;
            ZwClose(s->streamSocket->connectionHandle);
            s->streamSocket->connectionHandle = (HANDLE) -1;
            return status;
        }
        else
        {
            s->peer = *addr;
            s->isConnected = TRUE;
            return 0;
        }
    }
    else if (s->type == SOCK_DGRAM)
    {
        s->peer = *addr;
        if (remoteAddr->sin_addr.s_addr == 0 && remoteAddr->sin_port == 0)
        {
            s->isConnected = FALSE;
        }
        else
        {
            s->isConnected = TRUE;
        }
        return 0;
    }
    else
    {
        return -1;
    }
}

int __cdecl getpeername(int socket, struct sockaddr *addr, int *addrlen)
{
    PSOCKET s = (PSOCKET) -socket;

    if (!s->isConnected || addr == NULL || addrlen == NULL || *addrlen < sizeof(struct sockaddr_in))
    {
        return -1;
    }

    *addr = s->peer;
    *addrlen = sizeof(s->peer);

    return 0;
}

int __cdecl getsockname(int socket, struct sockaddr *addr, int *addrlen)
{
    PSOCKET s = (PSOCKET) -socket;
    struct sockaddr_in* localAddr = (struct sockaddr_in*) addr;

    if (!s->isBound || addr == NULL || addrlen == NULL || *addrlen < sizeof(struct sockaddr_in))
    {
        return -1;
    }

    if (s->type == SOCK_DGRAM)
    {
        *addrlen = sizeof(struct sockaddr_in);

        return tdi_query_address(
            s->addressFileObject,
            &localAddr->sin_addr.s_addr,
            &localAddr->sin_port
            );
    }
    else if (s->type == SOCK_STREAM)
    {
        *addrlen = sizeof(struct sockaddr_in);

        return tdi_query_address(
            s->streamSocket && s->streamSocket->connectionFileObject ? s->streamSocket->connectionFileObject : s->addressFileObject,
            &localAddr->sin_addr.s_addr,
            &localAddr->sin_port
            );
    }
    else
    {
        return -1;
    }
}

int __cdecl getsockopt(int socket, int level, int optname, char *optval, int *optlen)
{
    return -1;
}

int __cdecl listen(int socket, int backlog)
{
    return -1;
}

int __cdecl recv(int socket, char *buf, int len, int flags)
{
    PSOCKET s = (PSOCKET) -socket;

    if (s->type == SOCK_DGRAM)
    {
        return recvfrom(socket, buf, len, flags, 0, 0);
    }
    else if (s->type == SOCK_STREAM)
    {
        if (!s->isConnected)
        {
            return -1;
        }

        return tdi_recv_stream(
            s->streamSocket->connectionFileObject,
            buf,
            len,
            flags == MSG_OOB ? TDI_RECEIVE_EXPEDITED : TDI_RECEIVE_NORMAL
            );
    }
    else
    {
        return -1;
    }
}

int __cdecl recvfrom(int socket, char *buf, int len, int flags, struct sockaddr *addr, int *addrlen)
{
    PSOCKET s = (PSOCKET) -socket;
    struct sockaddr_in* returnAddr = (struct sockaddr_in*) addr;

    if (s->type == SOCK_STREAM)
    {
        return recv(socket, buf, len, flags);
    }
    else if (s->type == SOCK_DGRAM)
    {
        u_long* sin_addr = 0;
        u_short* sin_port = 0;

        if (!s->isBound)
        {
            return -1;
        }

        if (addr != NULL && addrlen != NULL && *addrlen >= sizeof(struct sockaddr_in))
        {
            sin_addr = &returnAddr->sin_addr.s_addr;
            sin_port = &returnAddr->sin_port;
            *addrlen = sizeof(struct sockaddr_in);
        }

        return tdi_recv_dgram(
            s->addressFileObject,
            sin_addr,
            sin_port,
            buf,
            len,
            TDI_RECEIVE_NORMAL
            );
    }
    else
    {
        return -1;
    }
}

int __cdecl select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timeval *timeout)
{
    return -1;
}

int __cdecl send(int socket, const char *buf, int len, int flags)
{
    PSOCKET s = (PSOCKET) -socket;

    if (!s->isConnected)
    {
        return -1;
    }

    if (s->type == SOCK_DGRAM)
    {
        return sendto(socket, buf, len, flags, &s->peer, sizeof(s->peer));
    }
    else if (s->type == SOCK_STREAM)
    {
        return tdi_send_stream(
            s->streamSocket->connectionFileObject,
            buf,
            len,
            flags == MSG_OOB ? TDI_SEND_EXPEDITED : 0
            );
    }
    else
    {
        return -1;
    }
}

int __cdecl sendto(int socket, const char *buf, int len, int flags, const struct sockaddr *addr, int addrlen)
{
    PSOCKET s = (PSOCKET) -socket;
    const struct sockaddr_in* remoteAddr = (const struct sockaddr_in*) addr;

    if (s->type == SOCK_STREAM)
    {
        return send(socket, buf, len, flags);
    }
    else if (s->type == SOCK_DGRAM)
    {
        if (addr == NULL || addrlen < sizeof(struct sockaddr_in))
        {
            return -1;
        }

        if (!s->isBound)
        {
            struct sockaddr_in localAddr;
            NTSTATUS status;

            localAddr.sin_family = AF_INET;
            localAddr.sin_port = 0;
            localAddr.sin_addr.s_addr = INADDR_ANY;

            status = bind(socket, (struct sockaddr*) &localAddr, sizeof(localAddr));

            if (!NT_SUCCESS(status))
            {
                return status;
            }
        }

        return tdi_send_dgram(
            s->addressFileObject,
            remoteAddr->sin_addr.s_addr,
            remoteAddr->sin_port,
            buf,
            len
            );
    }
    else
    {
        return -1;
    }
}

int __cdecl setsockopt(int socket, int level, int optname, const char *optval, int optlen)
{
    return -1;
}

int __cdecl shutdown(int socket, int how)
{
    PSOCKET s = (PSOCKET) -socket;

    if (!s->isConnected)
    {
        return -1;
    }

    if (s->type == SOCK_STREAM)
    {
        s->isShuttingdown = TRUE;
        return tdi_disconnect(s->streamSocket->connectionFileObject, TDI_DISCONNECT_RELEASE);
    }
    else
    {
        return -1;
    }
}

int __cdecl socket(int af, int type, int protocol)
{
    PSOCKET s;

    if (af != AF_INET ||
       (type != SOCK_DGRAM && type != SOCK_STREAM) ||
       (type == SOCK_DGRAM && protocol != IPPROTO_UDP && protocol != 0) ||
       (type == SOCK_STREAM && protocol != IPPROTO_TCP && protocol != 0)
       )
    {
        return STATUS_INVALID_PARAMETER;
    }

    s = HttpDiskMalloc(sizeof *s);

    if (!s)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(s, sizeof(SOCKET));

    s->type = type;
    s->addressHandle = (HANDLE) -1;

    return -(int)s;
}
