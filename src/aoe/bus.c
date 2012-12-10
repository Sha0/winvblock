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
 * AoE bus specifics.
 */

#include <stdio.h>
#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "irp.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "dummy.h"
#include "disk.h"
#include "aoe.h"
#include "mount.h"
#include "debug.h"

/* Names for the AoE bus. */
#define AOE_M_BUS_NAME_ (L"\\Device\\AoE")
#define AOE_M_BUS_DOSNAME_ (L"\\DosDevices\\AoE")

/** Object types */
typedef struct S_AOE_BUS S_AOE_BUS;

/* TODO: Remove this pull from aoe/driver.c */
extern NTSTATUS STDCALL AoeBusDevCtlScan(IN PIRP);
extern NTSTATUS STDCALL AoeBusDevCtlShow(IN PIRP);
extern NTSTATUS STDCALL AoeBusDevCtlMount(IN PIRP);
extern VOID AoeStop(void);

/* Forward declarations. */
static WV_F_DEV_PNP_ID AoeBusPnpId_;
NTSTATUS AoeBusCreate(IN PDRIVER_OBJECT);
VOID AoeBusFree(void);
DRIVER_DISPATCH AoeBusIrpDispatch;

/* Globals. */
WVL_S_BUS_T AoeBusMain = {0};
static UNICODE_STRING AoeBusName_ = {
    sizeof AOE_M_BUS_NAME_ - sizeof (WCHAR),
    sizeof AOE_M_BUS_NAME_ - sizeof (WCHAR),
    AOE_M_BUS_NAME_
  };
static UNICODE_STRING AoeBusDosname_ = {
    sizeof AOE_M_BUS_DOSNAME_ - sizeof (WCHAR),
    sizeof AOE_M_BUS_DOSNAME_ - sizeof (WCHAR),
    AOE_M_BUS_DOSNAME_
  };

static NTSTATUS STDCALL AoeBusDevCtlDetach_(IN PIRP irp) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    UINT32 unit_num;
    WVL_SP_BUS_NODE walker;
    AOE_SP_DISK aoe_disk = NULL;

    unit_num = *((PUINT32) irp->AssociatedIrp.SystemBuffer);
    DBG("Request to detach unit: %d\n", unit_num);

    walker = NULL;
    /* For each node on the bus... */
    WvlBusLock(&AoeBusMain);
    while (walker = WvlBusGetNextNode(&AoeBusMain, walker)) {
        aoe_disk = CONTAINING_RECORD(walker, AOE_S_DISK, BusNode[0]);
        /* If the unit number matches... */
        if (WvlBusGetNodeNum(walker) == unit_num) {
            /* If it's not a boot-time device... */
            if (aoe_disk->Boot) {
                DBG("Cannot detach a boot-time device.\n");
                /* Signal error. */
                aoe_disk = NULL;
                break;
              }
          }
      }
    WvlBusUnlock(&AoeBusMain);
    if (!aoe_disk) {
        DBG("Unit %d not found.\n", unit_num);
        return WvlIrpComplete(irp, 0, STATUS_INVALID_PARAMETER);
      }
    /* Detach the node. */
    WvlBusRemoveNode(aoe_disk->BusNode);
    DBG("Removed unit %d.\n", unit_num);
    return WvlIrpComplete(irp, 0, STATUS_SUCCESS);
  }

NTSTATUS STDCALL AoeBusDevCtl(
    IN PIRP irp,
    IN ULONG POINTER_ALIGNMENT code
  ) {
    switch(code) {
        case IOCTL_AOE_SCAN:
          return AoeBusDevCtlScan(irp);

        case IOCTL_AOE_SHOW:
          return AoeBusDevCtlShow(irp);

        case IOCTL_AOE_MOUNT:
          return AoeBusDevCtlMount(irp);

        case IOCTL_AOE_UMOUNT:
          return AoeBusDevCtlDetach_(irp);

        default:
          DBG("Unsupported IOCTL\n");
          return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
      }
  }

/* Generate dummy IDs for the AoE bus PDO */
WV_M_DUMMY_ID_GEN(
    static,
    AoeBusDummyIds,
    WVL_M_WLIT L"\\AoE",
    L"0",
    WVL_M_WLIT L"\\AoE\0",
    WVL_M_WLIT L"\\AoE\0",
    L"AoE Bus",
    FILE_DEVICE_CONTROLLER,
    FILE_DEVICE_SECURE_OPEN
  );

/* Destroy the AoE bus. */
VOID AoeBusFree(void) {
    IoDeleteSymbolicLink(&AoeBusDosname_);
    if (AoeBusMain.Fdo)
      IoDeleteDevice(AoeBusMain.Fdo);
    WvlBusClear(&AoeBusMain);
    return;
  }

static UINT32 STDCALL AoeBusPnpId_(
    IN WV_SP_DEV_T dev,
    IN BUS_QUERY_ID_TYPE query_type,
    IN OUT WCHAR (*buf)[512]
  ) {
    switch (query_type) {
        case BusQueryDeviceID:
          return swprintf(*buf, WVL_M_WLIT L"\\AoE") + 1;

        case BusQueryInstanceID:
          return swprintf(*buf, L"0") + 1;

        case BusQueryHardwareIDs:
          return swprintf(*buf, WVL_M_WLIT L"\\AoE") + 2;

        case BusQueryCompatibleIDs:
          return swprintf(*buf, WVL_M_WLIT L"\\AoE") + 4;

        default:
          return 0;
      }
  }

static NTSTATUS STDCALL AoeBusPnpQueryDevText_(
    IN WVL_SP_BUS_T bus,
    IN PIRP irp
  ) {
    WCHAR (*str)[512];
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;
    UINT32 str_len;

    /* Allocate a string buffer. */
    str = wv_mallocz(sizeof *str);
    if (str == NULL) {
        DBG("wv_malloc IRP_MN_QUERY_DEVICE_TEXT\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto alloc_str;
      }
    /* Determine the query type. */
    switch (io_stack_loc->Parameters.QueryDeviceText.DeviceTextType) {
        case DeviceTextDescription:
          str_len = swprintf(*str, L"AoE Bus") + 1;
          irp->IoStatus.Information =
            (ULONG_PTR) wv_palloc(str_len * sizeof **str);
          if (irp->IoStatus.Information == 0) {
              DBG("wv_palloc DeviceTextDescription\n");
              status = STATUS_INSUFFICIENT_RESOURCES;
              goto alloc_info;
            }
          RtlCopyMemory(
              (PWCHAR) irp->IoStatus.Information,
              str,
              str_len * sizeof **str
            );
          status = STATUS_SUCCESS;
          goto alloc_info;

        case DeviceTextLocationInformation:
          str_len = AoeBusPnpId_(
              NULL,
              BusQueryInstanceID,
              str
            );
          irp->IoStatus.Information =
            (ULONG_PTR) wv_palloc(str_len * sizeof **str);
          if (irp->IoStatus.Information == 0) {
              DBG("wv_palloc DeviceTextLocationInformation\n");
              status = STATUS_INSUFFICIENT_RESOURCES;
              goto alloc_info;
            }
          RtlCopyMemory(
              (PWCHAR) irp->IoStatus.Information,
              str,
              str_len * sizeof **str
            );
          status = STATUS_SUCCESS;
          goto alloc_info;

        default:
          irp->IoStatus.Information = 0;
          status = STATUS_NOT_SUPPORTED;
      }
    /* irp->IoStatus.Information not freed. */
    alloc_info:

    wv_free(str);
    alloc_str:

    return WvlIrpComplete(irp, irp->IoStatus.Information, status);
  }

NTSTATUS STDCALL AoeBusAttachFdo(
    IN PDRIVER_OBJECT driver_obj,
    IN PDEVICE_OBJECT pdo
  ) {
    NTSTATUS status;

    DBG("Entry\n");
    /* Do we already have our main bus? */
    if (AoeBusMain.Pdo) {
        DBG("Already have the main bus.  Refusing.\n");
        status = STATUS_NOT_SUPPORTED;
        goto err_already_established;
      }
    /* Set associations for the bus, FDO, PDO. */
    AoeBusMain.Pdo = pdo;
    /* Attach the FDO to the PDO. */
    AoeBusMain.LowerDeviceObject = IoAttachDeviceToDeviceStack(
        AoeBusMain.Fdo,
        pdo
      );
    if (AoeBusMain.LowerDeviceObject == NULL) {
        status = STATUS_NO_SUCH_DEVICE;
        DBG("IoAttachDeviceToDeviceStack() failed!\n");
        goto err_attach;
      }

    /* Ok! */
    DBG("Exit\n");
    return STATUS_SUCCESS;

    err_attach:

    err_already_established:

    DBG("Exit with failure\n");
    return status;
  }

/**
 * Request a PDO for the AoE bus from the WinVBlock bus.
 *
 * @ret NTSTATUS        The status of the operation.
 */
static NTSTATUS AoeBusCreatePdo_(void) {
    NTSTATUS status;
    DEVICE_OBJECT * bus_pdo;

    status = WvDummyAdd(
        /* Mini-driver: Main bus */
        NULL,
        /* Dummy IDs */
        AoeBusDummyIds,
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
    if (!NT_SUCCESS(status))
      return status;
    ASSERT(bus_pdo);

    /* Add the new PDO device to the main bus' list of children */
    WvlAddDeviceToMainBus(bus_pdo);

    return STATUS_SUCCESS;
  }

/**
 * Create the AoE bus PDO and FDO.
 *
 * @ret NTSTATUS        The status of the operation.
 */
NTSTATUS AoeBusCreate(IN PDRIVER_OBJECT driver_obj) {
    NTSTATUS status;
    SP_AOE_DEV aoe_dev;

    /* Do we already have our main bus? */
    if (AoeBusMain.Fdo) {
        DBG("AoeBusCreate called twice.\n");
        return STATUS_UNSUCCESSFUL;
      }
    /* Create the PDO for the sub-bus on the WinVBlock bus. */
    status = AoeBusCreatePdo_();
    if (!NT_SUCCESS(status)) {
        DBG("Couldn't create AoE bus PDO!\n");
        goto err_pdo;
      }
    /* Initialize the bus. */
    WvlBusInit(&AoeBusMain);
    /* Create the bus FDO. */
    status = IoCreateDevice(
        driver_obj,
        sizeof *aoe_dev,
        &AoeBusName_,
        FILE_DEVICE_CONTROLLER,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &AoeBusMain.Fdo
      );
    if (!NT_SUCCESS(status)) {
        DBG("IoCreateDevice() failed!\n");
        goto err_fdo;
      }
    /* DosDevice symlink. */
    status = IoCreateSymbolicLink(
        &AoeBusDosname_,
        &AoeBusName_
      );
    if (!NT_SUCCESS(status)) {
        DBG("IoCreateSymbolicLink() failed!\n");
        goto err_dos_symlink;
      }
    /* Ok! */
    AoeBusMain.QueryDevText = AoeBusPnpQueryDevText_;
    aoe_dev = AoeBusMain.Fdo->DeviceExtension;
    aoe_dev->IrpDispatch = AoeBusIrpDispatch;
    AoeBusMain.Fdo->Flags |= DO_DIRECT_IO;         /* FIXME? */
    AoeBusMain.Fdo->Flags |= DO_POWER_INRUSH;      /* FIXME? */
    AoeBusMain.Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    AoeBusMain.State = WvlBusStateStarted;
    /* All done. */
    DBG("Exit\n");
    return status;

    IoDeleteSymbolicLink(&AoeBusDosname_);
    err_dos_symlink:

    IoDeleteDevice(AoeBusMain.Fdo);
    err_fdo:

    /* Difficult to remova the PDO if we don't know what it is... */
    err_pdo:

    DBG("Exit with failure\n");
    return status;
  }

/**
 * Add a child node to the AoE bus.
 *
 * @v AoeDisk           Points to the child AoE disk to add.
 * @ret                 TRUE for success, FALSE for failure.
 */
BOOLEAN STDCALL AoeBusAddDev(
    IN OUT AOE_SP_DISK AoeDisk
  ) {
    DBG("Entry\n");
    if (!AoeBusMain.Fdo || !AoeDisk) {
        DBG("No bus or no device!\n");
        return FALSE;
      }
    WvlBusInitNode(AoeDisk->BusNode, AoeDisk->Pdo);
    /* Associate the parent bus. */
    AoeDisk->disk->ParentBus = AoeBusMain.Fdo;

    AoeDisk->Pdo->Flags &= ~DO_DEVICE_INITIALIZING;
    /* Add the new PDO device to the bus' list of children. */
    WvlBusAddNode(&AoeBusMain, AoeDisk->BusNode);

    DBG("Exit\n");
    return TRUE;
  }

/* Handle an IRP. */
NTSTATUS AoeBusIrpDispatch(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc;
    NTSTATUS status;
    ULONG POINTER_ALIGNMENT code;

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    switch (io_stack_loc->MajorFunction) {
        case IRP_MJ_PNP:
          status = WvlBusPnp(&AoeBusMain, irp);
          /* Did the bus detach? */
          if (AoeBusMain.State == WvlBusStateDeleted) {
              AoeStop();
              /* Delete. */
              IoDeleteDevice(AoeBusMain.Fdo);
              /* Disassociate. */
              AoeBusMain.Fdo = NULL;
            }
          return status;

        case IRP_MJ_DEVICE_CONTROL:
          code = io_stack_loc->Parameters.DeviceIoControl.IoControlCode;
          return AoeBusDevCtl(irp, code);

        case IRP_MJ_POWER:
          return WvlIrpPassPowerToLower(AoeBusMain.LowerDeviceObject, irp);

        case IRP_MJ_CREATE:
        case IRP_MJ_CLOSE:
          /* Always succeed with nothing to do. */
          return WvlIrpComplete(irp, 0, STATUS_SUCCESS);

        case IRP_MJ_SYSTEM_CONTROL:
          return WvlIrpPassToLower(AoeBusMain.LowerDeviceObject, irp);

        default:
          ;
      }
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
  }
