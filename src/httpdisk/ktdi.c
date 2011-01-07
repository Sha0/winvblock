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

/* From httpdisk.c */
extern PVOID HttpDiskMalloc(SIZE_T);
extern PVOID HttpDiskPalloc(SIZE_T);

NTSTATUS tdi_open_transport_address(PUNICODE_STRING devName, ULONG addr, USHORT port, BOOLEAN shared, PHANDLE addressHandle, PFILE_OBJECT *addressFileObject)
{
    OBJECT_ATTRIBUTES           attr;
    PFILE_FULL_EA_INFORMATION   eaBuffer;
    ULONG                       eaSize;
    PTA_IP_ADDRESS              localAddr;
    IO_STATUS_BLOCK             iosb;
    NTSTATUS                    status;

#if (VER_PRODUCTBUILD >= 2195)
    InitializeObjectAttributes(&attr, devName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
#else
    InitializeObjectAttributes(&attr, devName, OBJ_CASE_INSENSITIVE, NULL, NULL);
#endif

    eaSize = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName[0]) +
             TDI_TRANSPORT_ADDRESS_LENGTH                      +
             1                                                 +
             sizeof(TA_IP_ADDRESS);

    eaBuffer = HttpDiskPalloc(eaSize);

    if (eaBuffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    eaBuffer->NextEntryOffset = 0;
    eaBuffer->Flags = 0;
    eaBuffer->EaNameLength = TDI_TRANSPORT_ADDRESS_LENGTH;
    eaBuffer->EaValueLength = sizeof(TA_IP_ADDRESS);

    RtlCopyMemory(eaBuffer->EaName, TdiTransportAddress, eaBuffer->EaNameLength + 1);

    localAddr = (PTA_IP_ADDRESS)(eaBuffer->EaName + eaBuffer->EaNameLength + 1);

    localAddr->TAAddressCount = 1;
    localAddr->Address[0].AddressLength = TDI_ADDRESS_LENGTH_IP;
    localAddr->Address[0].AddressType = TDI_ADDRESS_TYPE_IP;
    localAddr->Address[0].Address[0].sin_port = port;
    localAddr->Address[0].Address[0].in_addr = addr;

    RtlZeroMemory(localAddr->Address[0].Address[0].sin_zero, sizeof(localAddr->Address[0].Address[0].sin_zero));

    status = ZwCreateFile(
        addressHandle,
        GENERIC_READ | GENERIC_WRITE,
        &attr,
        &iosb,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        shared ? FILE_SHARE_READ | FILE_SHARE_WRITE : 0,
        FILE_OPEN,
        0,
        eaBuffer,
        eaSize
        );

    ExFreePool(eaBuffer);

    if (!NT_SUCCESS(status))
    {
        return status;
    }

    status = ObReferenceObjectByHandle(*addressHandle, FILE_ALL_ACCESS, NULL, KernelMode, addressFileObject, NULL);

    if (!NT_SUCCESS(status))
    {
        ZwClose(*addressHandle);
        return status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS tdi_open_connection_endpoint(PUNICODE_STRING devName, PVOID connectionContext, BOOLEAN shared, PHANDLE connectionHandle, PFILE_OBJECT *connectionFileObject)
{
    OBJECT_ATTRIBUTES           attr;
    PFILE_FULL_EA_INFORMATION   eaBuffer;
    ULONG                       eaSize;
    PVOID                       *context;
    IO_STATUS_BLOCK             iosb;
    NTSTATUS                    status;

#if (VER_PRODUCTBUILD >= 2195)
    InitializeObjectAttributes(&attr, devName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
#else
    InitializeObjectAttributes(&attr, devName, OBJ_CASE_INSENSITIVE, NULL, NULL);
#endif

    eaSize = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName[0]) +
             TDI_CONNECTION_CONTEXT_LENGTH                     +
             1                                                 +
             sizeof(int);

    eaBuffer = HttpDiskPalloc(eaSize);

    if (eaBuffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    eaBuffer->NextEntryOffset = 0;
    eaBuffer->Flags = 0;
    eaBuffer->EaNameLength = TDI_CONNECTION_CONTEXT_LENGTH;
    eaBuffer->EaValueLength = sizeof(int);

    RtlCopyMemory(eaBuffer->EaName, TdiConnectionContext, eaBuffer->EaNameLength + 1);

    context = (PVOID*) &(eaBuffer->EaName[eaBuffer->EaNameLength + 1]);

    *context = connectionContext;

    status = ZwCreateFile(
        connectionHandle,
        GENERIC_READ | GENERIC_WRITE,
        &attr,
        &iosb,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        shared ? FILE_SHARE_READ | FILE_SHARE_WRITE : 0,
        FILE_OPEN,
        0,
        eaBuffer,
        eaSize
        );

    ExFreePool(eaBuffer);

    if (!NT_SUCCESS(status))
    {
        return status;
    }

    status = ObReferenceObjectByHandle(*connectionHandle, FILE_ALL_ACCESS, NULL, KernelMode, connectionFileObject, NULL);

    if (!NT_SUCCESS(status))
    {
        ZwClose(*connectionHandle);
        return status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS tdi_set_event_handler(PFILE_OBJECT addressFileObject, LONG eventType, PVOID eventHandler, PVOID eventContext)
{
    PDEVICE_OBJECT  devObj;
    KEVENT          event;
    PIRP            irp;
    IO_STATUS_BLOCK iosb;
    NTSTATUS        status;

    devObj = IoGetRelatedDeviceObject(addressFileObject);

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = TdiBuildInternalDeviceControlIrp(TDI_SET_EVENT_HANDLER, devObj, addressFileObject, &event, &iosb);

    if (irp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    TdiBuildSetEventHandler(irp, devObj, addressFileObject, NULL, NULL, eventType, eventHandler, eventContext);

    status = IoCallDriver(devObj, irp);

    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = iosb.Status;
    }

    return status;
}

NTSTATUS tdi_unset_event_handler(PFILE_OBJECT addressFileObject, LONG eventType)
{
    return tdi_set_event_handler(addressFileObject, eventType, NULL, NULL);
}

NTSTATUS tdi_associate_address(PFILE_OBJECT connectionFileObject, HANDLE addressHandle)
{
    PDEVICE_OBJECT  devObj;
    KEVENT          event;
    PIRP            irp;
    IO_STATUS_BLOCK iosb;
    NTSTATUS        status;

    devObj = IoGetRelatedDeviceObject(connectionFileObject);

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = TdiBuildInternalDeviceControlIrp(TDI_ASSOCIATE_ADDRESS, devObj, connectionFileObject, &event, &iosb);

    if (irp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    TdiBuildAssociateAddress(irp, devObj, connectionFileObject, NULL, NULL, addressHandle);

    status = IoCallDriver(devObj, irp);

    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = iosb.Status;
    }

    return status;
}

NTSTATUS tdi_disassociate_address(PFILE_OBJECT connectionFileObject)
{
    PDEVICE_OBJECT  devObj;
    KEVENT          event;
    PIRP            irp;
    IO_STATUS_BLOCK iosb;
    NTSTATUS        status;

    devObj = IoGetRelatedDeviceObject(connectionFileObject);

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = TdiBuildInternalDeviceControlIrp(TDI_DISASSOCIATE_ADDRESS, devObj, connectionFileObject, &event, &iosb);

    if (irp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    TdiBuildDisassociateAddress(irp, devObj, connectionFileObject, NULL, NULL);

    status = IoCallDriver(devObj, irp);

    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = iosb.Status;
    }

    return status;
}

NTSTATUS tdi_connect(PFILE_OBJECT connectionFileObject, ULONG addr, USHORT port)
{
    PDEVICE_OBJECT              devObj;
    KEVENT                      event;
    PTDI_CONNECTION_INFORMATION remoteInfo;
    PTA_IP_ADDRESS              remoteAddr;
    PTDI_CONNECTION_INFORMATION returnInfo;
    PTA_IP_ADDRESS              returnAddr;
    PIRP                        irp;
    IO_STATUS_BLOCK             iosb;
    NTSTATUS                    status;

    devObj = IoGetRelatedDeviceObject(connectionFileObject);

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    remoteInfo = HttpDiskMalloc(
        2 * sizeof (TDI_CONNECTION_INFORMATION) +
        2 * sizeof (TA_IP_ADDRESS)
      );

    if (remoteInfo == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(remoteInfo, 2 * sizeof(TDI_CONNECTION_INFORMATION) + 2 * sizeof(TA_IP_ADDRESS));

    remoteInfo->RemoteAddressLength = sizeof(TA_IP_ADDRESS);
    remoteInfo->RemoteAddress = (PUCHAR)remoteInfo + sizeof(TDI_CONNECTION_INFORMATION);

    remoteAddr = (PTA_IP_ADDRESS) remoteInfo->RemoteAddress;

    remoteAddr->TAAddressCount = 1;
    remoteAddr->Address[0].AddressLength = TDI_ADDRESS_LENGTH_IP;
    remoteAddr->Address[0].AddressType = TDI_ADDRESS_TYPE_IP;
    remoteAddr->Address[0].Address[0].sin_port = port;
    remoteAddr->Address[0].Address[0].in_addr = addr;

    returnInfo = (PTDI_CONNECTION_INFORMATION)((PUCHAR)remoteInfo + sizeof(TDI_CONNECTION_INFORMATION) + sizeof(TA_IP_ADDRESS));

    returnInfo->RemoteAddressLength = sizeof(TA_IP_ADDRESS);
    returnInfo->RemoteAddress = (PUCHAR)returnInfo + sizeof(TDI_CONNECTION_INFORMATION);

    returnAddr = (PTA_IP_ADDRESS) returnInfo->RemoteAddress;

    returnAddr->TAAddressCount = 1;
    returnAddr->Address[0].AddressLength = TDI_ADDRESS_LENGTH_IP;
    returnAddr->Address[0].AddressType = TDI_ADDRESS_TYPE_IP;

    irp = TdiBuildInternalDeviceControlIrp(TDI_CONNECT, devObj, connectionFileObject, &event, &iosb);

    if (irp == NULL)
    {
        ExFreePool(remoteInfo);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    TdiBuildConnect(irp, devObj, connectionFileObject, NULL, NULL, NULL, remoteInfo, returnInfo);

    status = IoCallDriver(devObj, irp);

    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = iosb.Status;
    }

    ExFreePool(remoteInfo);

    return status;
}

NTSTATUS tdi_disconnect(PFILE_OBJECT connectionFileObject, ULONG flags)
{
    PDEVICE_OBJECT  devObj;
    KEVENT          event;
    PIRP            irp;
    IO_STATUS_BLOCK iosb;
    NTSTATUS        status;

    devObj = IoGetRelatedDeviceObject(connectionFileObject);

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = TdiBuildInternalDeviceControlIrp(TDI_DISCONNECT, devObj, connectionFileObject, &event, &iosb);

    if (irp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    TdiBuildDisconnect(irp, devObj, connectionFileObject, NULL, NULL, NULL, flags, NULL, NULL);

    status = IoCallDriver(devObj, irp);

    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = iosb.Status;
    }

    return status;
}

NTSTATUS tdi_send_dgram(PFILE_OBJECT addressFileObject, ULONG addr, USHORT port, const char *buf, int len)
{
    PDEVICE_OBJECT              devObj;
    KEVENT                      event;
    PTDI_CONNECTION_INFORMATION remoteInfo;
    PTA_IP_ADDRESS              remoteAddr;
    PIRP                        irp;
    PMDL                        mdl;
    IO_STATUS_BLOCK             iosb;
    NTSTATUS                    status;

    devObj = IoGetRelatedDeviceObject(addressFileObject);

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    remoteInfo = HttpDiskMalloc(
        sizeof (TDI_CONNECTION_INFORMATION) +
        sizeof (TA_IP_ADDRESS)
      );

    if (remoteInfo == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(remoteInfo, sizeof(TDI_CONNECTION_INFORMATION) + sizeof(TA_IP_ADDRESS));

    remoteInfo->RemoteAddressLength = sizeof(TA_IP_ADDRESS);
    remoteInfo->RemoteAddress = (PUCHAR)remoteInfo + sizeof(TDI_CONNECTION_INFORMATION);

    remoteAddr = (PTA_IP_ADDRESS) remoteInfo->RemoteAddress;

    remoteAddr->TAAddressCount = 1;
    remoteAddr->Address[0].AddressLength = TDI_ADDRESS_LENGTH_IP;
    remoteAddr->Address[0].AddressType = TDI_ADDRESS_TYPE_IP;
    remoteAddr->Address[0].Address[0].sin_port = port;
    remoteAddr->Address[0].Address[0].in_addr = addr;

    irp = TdiBuildInternalDeviceControlIrp(TDI_SEND_DATAGRAM, devObj, addressFileObject, &event, &iosb);

    if (irp == NULL)
    {
        ExFreePool(remoteInfo);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (len)
    {
        mdl = IoAllocateMdl((void*) buf, len, FALSE, FALSE, NULL);

        if (mdl == NULL)
        {
            IoFreeIrp(irp);
            ExFreePool(remoteInfo);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        __try
        {
            MmProbeAndLockPages(mdl, KernelMode, IoReadAccess);
            status = STATUS_SUCCESS;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            IoFreeMdl(mdl);
            IoFreeIrp(irp);
            ExFreePool(remoteInfo);
            status = STATUS_INVALID_USER_BUFFER;
        }

        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

    TdiBuildSendDatagram(irp, devObj, addressFileObject, NULL, NULL, len ? mdl : 0, len, remoteInfo);

    status = IoCallDriver(devObj, irp);

    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = iosb.Status;
    }

    ExFreePool(remoteInfo);

    return NT_SUCCESS(status) ? iosb.Information : status;
}

NTSTATUS tdi_recv_dgram(PFILE_OBJECT addressFileObject, PULONG addr, PUSHORT port, char *buf, int len, ULONG flags)
{
    PDEVICE_OBJECT              devObj;
    KEVENT                      event;
    PTDI_CONNECTION_INFORMATION remoteInfo;
    PTDI_CONNECTION_INFORMATION returnInfo;
    PTA_IP_ADDRESS              returnAddr;
    PIRP                        irp;
    PMDL                        mdl;
    IO_STATUS_BLOCK             iosb;
    NTSTATUS                    status;

    devObj = IoGetRelatedDeviceObject(addressFileObject);

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    remoteInfo = HttpDiskMalloc(
        2 * sizeof (TDI_CONNECTION_INFORMATION) +
        sizeof (TA_IP_ADDRESS)
      );

    if (remoteInfo == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(remoteInfo, 2 * sizeof(TDI_CONNECTION_INFORMATION) + sizeof(TA_IP_ADDRESS));

    remoteInfo->RemoteAddressLength = 0;
    remoteInfo->RemoteAddress = NULL;

    returnInfo = (PTDI_CONNECTION_INFORMATION)((PUCHAR)remoteInfo + sizeof(TDI_CONNECTION_INFORMATION));

    returnInfo->RemoteAddressLength = sizeof(TA_IP_ADDRESS);
    returnInfo->RemoteAddress = (PUCHAR)returnInfo + sizeof(TDI_CONNECTION_INFORMATION);

    returnAddr = (PTA_IP_ADDRESS) returnInfo->RemoteAddress;

    returnAddr->TAAddressCount = 1;
    returnAddr->Address[0].AddressLength = TDI_ADDRESS_LENGTH_IP;
    returnAddr->Address[0].AddressType = TDI_ADDRESS_TYPE_IP;

    irp = TdiBuildInternalDeviceControlIrp(TDI_RECEIVE_DATAGRAM, devObj, addressFileObject, &event, &iosb);

    if (irp == NULL)
    {
        ExFreePool(remoteInfo);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (len)
    {
        mdl = IoAllocateMdl((void*) buf, len, FALSE, FALSE, NULL);

        if (mdl == NULL)
        {
            IoFreeIrp(irp);
            ExFreePool(remoteInfo);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        __try
        {
            MmProbeAndLockPages(mdl, KernelMode, IoWriteAccess);
            status = STATUS_SUCCESS;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            IoFreeMdl(mdl);
            IoFreeIrp(irp);
            ExFreePool(remoteInfo);
            status = STATUS_INVALID_USER_BUFFER;
        }

        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

    TdiBuildReceiveDatagram(irp, devObj, addressFileObject, NULL, NULL, len ? mdl : 0, len, remoteInfo, returnInfo, flags);

    status = IoCallDriver(devObj, irp);

    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = iosb.Status;
    }

    if (addr)
    {
        *addr = returnAddr->Address[0].Address[0].in_addr;
    }

    if (port)
    {
        *port = returnAddr->Address[0].Address[0].sin_port;
    }

    ExFreePool(remoteInfo);

    return NT_SUCCESS(status) ? iosb.Information : status;
}

NTSTATUS tdi_send_stream(PFILE_OBJECT connectionFileObject, const char *buf, int len, ULONG flags)
{
    PDEVICE_OBJECT  devObj;
    KEVENT          event;
    PIRP            irp;
    PMDL            mdl;
    IO_STATUS_BLOCK iosb;
    NTSTATUS        status;

    devObj = IoGetRelatedDeviceObject(connectionFileObject);

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = TdiBuildInternalDeviceControlIrp(TDI_SEND, devObj, connectionFileObject, &event, &iosb);

    if (irp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (len)
    {
        mdl = IoAllocateMdl((void*) buf, len, FALSE, FALSE, NULL);

        if (mdl == NULL)
        {
            IoFreeIrp(irp);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        __try
        {
            MmProbeAndLockPages(mdl, KernelMode, IoReadAccess);
            status = STATUS_SUCCESS;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            IoFreeMdl(mdl);
            IoFreeIrp(irp);
            status = STATUS_INVALID_USER_BUFFER;
        }

        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

    TdiBuildSend(irp, devObj, connectionFileObject, NULL, NULL, len ? mdl : 0, flags, len);

    status = IoCallDriver(devObj, irp);

    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = iosb.Status;
    }

    return NT_SUCCESS(status) ? iosb.Information : status;
}

NTSTATUS tdi_recv_stream(PFILE_OBJECT connectionFileObject, char *buf, int len, ULONG flags)
{
    PDEVICE_OBJECT  devObj;
    KEVENT          event;
    PIRP            irp;
    PMDL            mdl;
    IO_STATUS_BLOCK iosb;
    NTSTATUS        status;

    devObj = IoGetRelatedDeviceObject(connectionFileObject);

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = TdiBuildInternalDeviceControlIrp(TDI_RECEIVE, devObj, connectionFileObject, &event, &iosb);

    if (irp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (len)
    {
        mdl = IoAllocateMdl((void*) buf, len, FALSE, FALSE, NULL);

        if (mdl == NULL)
        {
            IoFreeIrp(irp);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        __try
        {
            MmProbeAndLockPages(mdl, KernelMode, IoWriteAccess);
            status = STATUS_SUCCESS;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            IoFreeMdl(mdl);
            IoFreeIrp(irp);
            status = STATUS_INVALID_USER_BUFFER;
        }

        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

    TdiBuildReceive(irp, devObj, connectionFileObject, NULL, NULL, len ? mdl : 0, flags, len);

    status = IoCallDriver(devObj, irp);

    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = iosb.Status;
    }

    return NT_SUCCESS(status) ? iosb.Information : status;
}

NTSTATUS tdi_query_address(PFILE_OBJECT addressFileObject, PULONG addr, PUSHORT port)
{
    PDEVICE_OBJECT              devObj;
    KEVENT                      event;
    PTRANSPORT_ADDRESS          localInfo;
    PTA_IP_ADDRESS              localAddr;
    PIRP                        irp;
    PMDL                        mdl;
    IO_STATUS_BLOCK             iosb;
    NTSTATUS                    status;

    devObj = IoGetRelatedDeviceObject(addressFileObject);

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    localInfo = HttpDiskMalloc(sizeof (TDI_ADDRESS_INFO) * 10);

    if (localInfo == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(localInfo, sizeof(TDI_ADDRESS_INFO)*10);

    irp = TdiBuildInternalDeviceControlIrp(TDI_QUERY_INFORMATION, devObj, addressFileObject, &event, &iosb);

    if (irp == NULL)
    {
        ExFreePool(localInfo);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    {
        mdl = IoAllocateMdl((void*) localInfo, sizeof(TDI_ADDRESS_INFO)*10, FALSE, FALSE, NULL);

        if (mdl == NULL)
        {
            IoFreeIrp(irp);
            ExFreePool(localInfo);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        __try
        {
            MmProbeAndLockPages(mdl, KernelMode, IoWriteAccess);
            status = STATUS_SUCCESS;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            IoFreeMdl(mdl);
            IoFreeIrp(irp);
            ExFreePool(localInfo);
            status = STATUS_INVALID_USER_BUFFER;
        }

        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

    TdiBuildQueryInformation(irp, devObj, addressFileObject, NULL, NULL, TDI_QUERY_ADDRESS_INFO, mdl);

    status = IoCallDriver(devObj, irp);

    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        status = iosb.Status;
    }

    localAddr = (PTA_IP_ADDRESS)&localInfo->Address[0];

    if (addr)
    {
        *addr = localAddr->Address[0].Address[0].in_addr;
    }

    if (port)
    {
        *port = localAddr->Address[0].Address[0].sin_port;
    }

    ExFreePool(localInfo);

    return status;
}
