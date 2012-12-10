/**
 * Copyright (C) 2010-2012, Shao Miller <sha0.miller@gmail.com>.
 *
 * This file is part of WinVBlock, originally derived from WinAoE.
 *
 * WinVBlock is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinVBlock is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WinVBlock.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * HTTPDisk bus specifics.
 */

#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "driver.h"
#include "debug.h"
#include "bus.h"
#include "device.h"
#include "dummy.h"
#include "irp.h"
#include "disk.h"
#include "httpdisk.h"

/** From httpdisk.c */
extern PDRIVER_OBJECT HttpdiskDriverObj;
extern NTSTATUS STDCALL HttpdiskCreateDevice(
    IN WVL_E_DISK_MEDIA_TYPE,
    IN PDEVICE_OBJECT *
  );
extern NTSTATUS HttpDiskConnect(IN PDEVICE_OBJECT, IN PIRP);
extern PDEVICE_OBJECT HttpDiskDeleteDevice(IN PDEVICE_OBJECT);

/** Exports. */
NTSTATUS STDCALL HttpdiskBusEstablish(void);
VOID HttpdiskBusCleanup(void);
DRIVER_ADD_DEVICE HttpdiskBusAttach;
DRIVER_DISPATCH HttpdiskBusIrp;

/** Private. */
static NTSTATUS STDCALL HttpdiskBusCreateFdo_(void);
static NTSTATUS STDCALL HttpdiskBusCreatePdo_(void);
static VOID HttpdiskBusDeleteFdo_(void);
static NTSTATUS STDCALL HttpdiskBusDevCtl_(IN PIRP);
static NTSTATUS STDCALL HttpdiskBusAdd_(IN PIRP);
static NTSTATUS STDCALL HttpdiskBusRemove_(IN PIRP);

/* The HTTPDisk bus. */
static WVL_S_BUS_T HttpdiskBus_ = {0};

/* Names for the HTTPDisk bus. */
#define HTTPDISK_M_BUS_NAME_ (L"\\Device\\HTTPDisk")
#define HTTPDISK_M_BUS_DOSNAME_ (L"\\DosDevices\\HTTPDisk")
static UNICODE_STRING HttpdiskBusName_ = {
    sizeof HTTPDISK_M_BUS_NAME_ - sizeof (WCHAR),
    sizeof HTTPDISK_M_BUS_NAME_ - sizeof (WCHAR),
    HTTPDISK_M_BUS_NAME_
  };
static UNICODE_STRING HttpdiskBusDosname_ = {
    sizeof HTTPDISK_M_BUS_DOSNAME_ - sizeof (WCHAR),
    sizeof HTTPDISK_M_BUS_DOSNAME_ - sizeof (WCHAR),
    HTTPDISK_M_BUS_DOSNAME_
  };
static BOOLEAN HttpdiskBusSymlinkDone_ = FALSE;

NTSTATUS STDCALL HttpdiskBusEstablish(void) {
    NTSTATUS status;
    PDEVICE_OBJECT dev_obj = HttpdiskDriverObj->DeviceObject;

    /* Initialize the bus. */
    WvlBusInit(&HttpdiskBus_);
    HttpdiskBus_.State = WvlBusStateStarted;

    status = HttpdiskBusCreateFdo_();
    if (!NT_SUCCESS(status))
      goto err_fdo;

    status = HttpdiskBusCreatePdo_();
    if (!NT_SUCCESS(status))
      goto err_pdo;

    DBG("Bus established.\n");
    return STATUS_SUCCESS;

    HttpdiskBusDeleteFdo_();
    err_fdo:

    /* TODO: Remove the PDO. */
    err_pdo:

    DBG("Bus not established.\n");
    return status;
  }

VOID HttpdiskBusCleanup(void) {
    /*
     * The FDO should be deleted by an IRP_MJ_PNP:IRP_MN_REMOVE_DEVICE,
     * but just in case it isn't...
     */
    HttpdiskBusDeleteFdo_();
    DBG("Cleaned up.\n");
    return;
  }

NTSTATUS HttpdiskBusAttach(
    IN PDRIVER_OBJECT DriverObj,
    IN PDEVICE_OBJECT Pdo
  ) {
    NTSTATUS status;

    /* Do we already have our bus? */
    if (HttpdiskBus_.Pdo) {
        DBG(
            "Bus PDO %p already established.  Refusing...\n",
            HttpdiskBus_.Pdo
          );
        status = STATUS_NOT_SUPPORTED;
        goto err_already_established;
      }
    /* Associate the bus with the PDO. */
    HttpdiskBus_.Pdo = Pdo;
    /* Attach the FDO to the PDO. */
    HttpdiskBus_.LowerDeviceObject = IoAttachDeviceToDeviceStack(
        HttpdiskBus_.Fdo,
        Pdo
      );
    if (HttpdiskBus_.LowerDeviceObject == NULL) {
        status = STATUS_NO_SUCH_DEVICE;
        DBG("IoAttachDeviceToDeviceStack() failed!\n");
        goto err_attach;
      }

    /* Ok! */
    DBG("Attached bus to PDO %p.\n", Pdo);
    return STATUS_SUCCESS;

    err_attach:

    err_already_established:

    DBG("PDO %p not attached.\n", Pdo);
    return status;
  }

NTSTATUS HttpdiskBusIrp(IN PDEVICE_OBJECT DevObj, IN PIRP Irp) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(Irp);
    UCHAR major = io_stack_loc->MajorFunction;
    NTSTATUS status;

    switch (major) {
        case IRP_MJ_DEVICE_CONTROL:
            return HttpdiskBusDevCtl_(Irp);

        case IRP_MJ_PNP:
          status = WvlBusPnp(&HttpdiskBus_, Irp);
          /* Is the bus still attached?  If not, it's time to stop. */
          if (HttpdiskBus_.State == WvlBusStateDeleted)
            HttpdiskBusDeleteFdo_();
          return status;

        case IRP_MJ_POWER:
          return WvlIrpPassPowerToLower(HttpdiskBus_.LowerDeviceObject, Irp);

        case IRP_MJ_SYSTEM_CONTROL:
          return WvlIrpPassToLower(HttpdiskBus_.LowerDeviceObject, Irp);

        default:
          DBG("Unhandled major: %d\n", major);
          break;
      }
    return WvlIrpComplete(Irp, 0, STATUS_NOT_SUPPORTED);
  }

static NTSTATUS STDCALL HttpdiskBusCreateFdo_(void) {
    NTSTATUS status;
    HTTPDISK_SP_DEV dev;

    status = IoCreateDevice(
        HttpdiskDriverObj,
        sizeof *dev,
        &HttpdiskBusName_,
        FILE_DEVICE_CONTROLLER,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &HttpdiskBus_.Fdo
      );
    if (!NT_SUCCESS(status)) {
        DBG("FDO not created.\n");
        goto err_fdo;
      }

    /* DosDevice symlink. */
    status = IoCreateSymbolicLink(
        &HttpdiskBusDosname_,
        &HttpdiskBusName_
      );
    if (!NT_SUCCESS(status)) {
        DBG("IoCreateSymbolicLink() failed!\n");
        goto err_symlink;
      }
    HttpdiskBusSymlinkDone_ = TRUE;

    /* Initialize device extension. */
    dev = HttpdiskBus_.Fdo->DeviceExtension;
    dev->bus = TRUE;

    DBG("FDO created: %p.\n", (PVOID) HttpdiskBus_.Fdo);
    return STATUS_SUCCESS;

    IoDeleteSymbolicLink(&HttpdiskBusDosname_);
    err_symlink:

    IoDeleteDevice(HttpdiskBus_.Fdo);
    err_fdo:

    return status;
  }

static NTSTATUS STDCALL HttpdiskBusCreatePdo_(void) {
    NTSTATUS status;
    DEVICE_OBJECT * bus_pdo;

    /* Generate dummy IDs for the HTTPDisk bus PDO */
    WV_M_DUMMY_ID_GEN(
        static,
        HttpdiskBusDummyIds,
        WVL_M_WLIT L"\\HTTPDisk",
        L"0",
        WVL_M_WLIT L"\\HTTPDisk\0",
        WVL_M_WLIT L"\\HTTPDisk\0",
        L"HTTPDisk Bus",
        FILE_DEVICE_CONTROLLER,
        FILE_DEVICE_SECURE_OPEN
      );

    status = WvDummyAdd(
        /* Mini-driver: Main bus */
        NULL,
        /* Dummy IDs */
        HttpdiskBusDummyIds,
        /* Dummy IDs offset: Auto */
        0,
        /* Extra data offset: None */
        0,
        /* Extra data size: None */
        0,
        /* Device name: Not specified */
        NULL,
        /* Exclusive access? */
        FALSE,
        /* PDO pointer to populate */
        &bus_pdo
      );
    if (!NT_SUCCESS(status)) {
        DBG("PDO not created.\n");
        return status;
      }
    ASSERT(bus_pdo);

    /* Add the new PDO device to the main bus' list of children */
    WvlAddDeviceToMainBus(bus_pdo);

    DBG("PDO created.\n");
    return STATUS_SUCCESS;
  }

static VOID HttpdiskBusDeleteFdo_(void) {
    if (!HttpdiskBus_.Fdo)
      return;
    if (HttpdiskBusSymlinkDone_)
      IoDeleteSymbolicLink(&HttpdiskBusDosname_);
    IoDeleteDevice(HttpdiskBus_.Fdo);
    DBG("FDO %p deleted.\n", (PVOID) HttpdiskBus_.Fdo);
    HttpdiskBus_.Fdo = NULL;
    return;
  }

static NTSTATUS STDCALL HttpdiskBusDevCtl_(IN PIRP irp) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);

    switch (io_stack_loc->Parameters.DeviceIoControl.IoControlCode) {
        case IOCTL_HTTP_DISK_CONNECT:
          return HttpdiskBusAdd_(irp);

        case IOCTL_HTTP_DISK_DISCONNECT:
          return HttpdiskBusRemove_(irp);
      }
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
  }

static NTSTATUS STDCALL HttpdiskBusAdd_(IN PIRP irp) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    PHTTP_DISK_INFORMATION info = irp->AssociatedIrp.SystemBuffer;
    NTSTATUS status;
    PDEVICE_OBJECT pdo = NULL;
    HTTPDISK_SP_DEV dev;

    /* Validate buffer size. */
    if (
        (io_stack_loc->Parameters.DeviceIoControl.InputBufferLength <
          sizeof *info) ||
        (io_stack_loc->Parameters.DeviceIoControl.InputBufferLength <
          sizeof *info + info->FileNameLength - sizeof info->FileName[0])
      ) {
        DBG("Buffer too small.\n");
        status = STATUS_INVALID_PARAMETER;
        goto err_buf;
      }

    /* Create a new disk.  TODO: Disk media type. */
    status = HttpdiskCreateDevice(
        info->Optical ? WvlDiskMediaTypeOptical : WvlDiskMediaTypeHard,
        &pdo
      );
    if (!pdo) {
        DBG("Could not create PDO!\n");
        goto err_pdo;
      }
    dev = pdo->DeviceExtension;

    /* Connect the HTTPDisk. */
    status = HttpDiskConnect(pdo, irp);
    if (!NT_SUCCESS(status)) {
        DBG("Connection failed!\n");
        goto err_connect;
      }
    dev->Disk->LBADiskSize = dev->file_size.QuadPart / dev->Disk->SectorSize;

    /* Add it to the bus. */
    dev->Disk->ParentBus = HttpdiskBus_.Fdo;
    WvlBusInitNode(&dev->BusNode, pdo);
    status = WvlBusAddNode(&HttpdiskBus_, &dev->BusNode);
    if (!NT_SUCCESS(status)) {
        DBG("Couldn't add node to bus!\n");
        goto err_add_node;
      }

    /* All done. */
    DBG("Created and added HTTPDisk PDO: %p\n", (PVOID) pdo);
    return WvlIrpComplete(irp, 0, status);

    WvlBusRemoveNode(&dev->BusNode);
    err_add_node:

    err_connect:

    HttpDiskDeleteDevice(pdo);
    err_pdo:

    err_buf:

    return WvlIrpComplete(irp, 0, status);
  }

static NTSTATUS STDCALL HttpdiskBusRemove_(IN PIRP irp) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    PUINT32 unit_num;
    NTSTATUS status;
    WVL_SP_BUS_NODE walker;
    PDEVICE_OBJECT pdo = NULL;

    /* Validate buffer size. */
    if (
        io_stack_loc->Parameters.DeviceIoControl.InputBufferLength <
        sizeof *unit_num
      ) {
        DBG("Buffer too small.\n");
        status = STATUS_INVALID_PARAMETER;
        goto err_buf;
      }
    unit_num = irp->AssociatedIrp.SystemBuffer;

    DBG("Request to detach unit %d...\n", *unit_num);

    walker = NULL;
    /* For each node on the bus... */
    WvlBusLock(&HttpdiskBus_);
    while (walker = WvlBusGetNextNode(&HttpdiskBus_, walker)) {
        /* If the unit number matches... */
        if (WvlBusGetNodeNum(walker) == *unit_num) {
            pdo = WvlBusGetNodePdo(walker);
            break;
          }
      }
    WvlBusUnlock(&HttpdiskBus_);
    if (!pdo) {
        DBG("Unit %d not found.\n", *unit_num);
        status = STATUS_INVALID_PARAMETER;
        goto err_unit;
      }
    /* Detach and destroy the node. */
    WvlBusRemoveNode(walker);
    HttpDiskDeleteDevice(pdo);
    DBG("Removed unit %d.\n", *unit_num);

    return WvlIrpComplete(irp, 0, irp->IoStatus.Status);

    err_unit:

    err_buf:

    return WvlIrpComplete(irp, 0, status);
  }
