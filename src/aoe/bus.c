/**
 * Copyright (C) 2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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

#include "winvblock.h"
#include "wv_stdlib.h"
#include "portable.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "aoe.h"
#include "mount.h"
#include "debug.h"

/* Names for the AoE bus. */
#define AOE_M_BUS_NAME_ (L"\\Device\\AoE")
#define AOE_M_BUS_DOSNAME_ (L"\\DosDevices\\AoE")

/* TODO: Remove this pull from aoe/driver.c */
extern NTSTATUS STDCALL AoeBusDevCtlScan(IN PIRP);
extern NTSTATUS STDCALL AoeBusDevCtlShow(IN PIRP);
extern NTSTATUS STDCALL AoeBusDevCtlMount(IN PIRP);

/* Forward declarations. */
static WV_F_DEV_PNP_ID AoeBusPnpId_;
winvblock__bool AoeBusCreate(IN PDRIVER_OBJECT);
void AoeBusFree(void);

/* Globals. */
WV_S_BUS_T AoeBusMain = {0};
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
const WV_S_DRIVER_DUMMY_IDS * AoeBusDummyIds;

static NTSTATUS STDCALL AoeBusDevCtlDetach_(IN PIRP irp) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    winvblock__uint32 unit_num;
    WV_SP_BUS_NODE walker;

    if (!(io_stack_loc->Control & SL_PENDING_RETURNED)) {
        NTSTATUS status;

        /* Enqueue the IRP. */
        status = WvBusEnqueueIrp(&AoeBusMain, irp);
        if (status != STATUS_PENDING)
          /* Problem. */
          return driver__complete_irp(irp, 0, status);
        /* Ok. */
        return status;
      }
    /* If we get here, we should be called by WvBusProcessWorkItems() */
    unit_num = *((winvblock__uint32_ptr) irp->AssociatedIrp.SystemBuffer);
    DBG("Request to detach unit: %d\n", unit_num);

    walker = NULL;
    /* For each node on the bus... */
    while (walker = WvBusGetNextNode(&AoeBusMain, walker)) {
        WV_SP_DEV_T dev = WvDevFromDevObj(WvBusGetNodePdo(walker));

        /* If the unit number matches... */
        if (WvBusGetNodeNum(walker) == unit_num) {
            /* If it's not a boot-time device... */
            if (dev->Boot) {
                DBG("Cannot detach a boot-time device.\n");
                /* Signal error. */
                walker = NULL;
                break;
              }
            /* Detach the node and free it. */
            DBG("Removing unit %d\n", unit_num);
            WvBusRemoveNode(walker);
            WvDevClose(dev);
            IoDeleteDevice(dev->Self);
            WvDevFree(dev);
            break;
          }
      }
    if (!walker)
      return driver__complete_irp(irp, 0, STATUS_INVALID_PARAMETER);
    return driver__complete_irp(irp, 0, STATUS_SUCCESS);
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
          return driver__complete_irp(irp, 0, STATUS_NOT_SUPPORTED);
      }
  }

/* Generate dummy IDs for the AoE bus PDO. */
#define AOE_M_BUS_IDS(X_, Y_)                       \
  X_(Y_, Dev,      winvblock__literal_w L"\\AoE"  ) \
  X_(Y_, Instance, L"0"                           ) \
  X_(Y_, Hardware, winvblock__literal_w L"\\AoE\0") \
  X_(Y_, Compat,   winvblock__literal_w L"\\AoE\0")
WV_M_DRIVER_DUMMY_ID_GEN(AoeBusDummyIds_, AOE_M_BUS_IDS);

/* Destroy the AoE bus. */
void AoeBusFree(void) {
    IoDeleteSymbolicLink(&AoeBusDosname_);
    if (AoeBusMain.Fdo)
      IoDeleteDevice(AoeBusMain.Fdo);
    return;
  }

static winvblock__uint32 STDCALL AoeBusPnpId_(
    IN WV_SP_DEV_T dev,
    IN BUS_QUERY_ID_TYPE query_type,
    IN OUT WCHAR (*buf)[512]
  ) {
    switch (query_type) {
        case BusQueryDeviceID:
          return swprintf(*buf, winvblock__literal_w L"\\AoE") + 1;

        case BusQueryInstanceID:
          return swprintf(*buf, L"0") + 1;

        case BusQueryHardwareIDs:
          return swprintf(*buf, winvblock__literal_w L"\\AoE") + 2;

        case BusQueryCompatibleIDs:
          return swprintf(*buf, winvblock__literal_w L"\\AoE") + 4;

        default:
          return 0;
      }
  }

static NTSTATUS STDCALL AoeBusPnpQueryDevText_(
    IN WV_SP_BUS_T bus,
    IN PIRP irp
  ) {
    WCHAR (*str)[512];
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;
    winvblock__uint32 str_len;

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
            (ULONG_PTR) wv_palloc(str_len * sizeof *str);
          if (irp->IoStatus.Information == 0) {
              DBG("wv_palloc DeviceTextDescription\n");
              status = STATUS_INSUFFICIENT_RESOURCES;
              goto alloc_info;
            }
          RtlCopyMemory(
              (PWCHAR) irp->IoStatus.Information,
              str,
              str_len * sizeof (WCHAR)
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
            (ULONG_PTR) wv_palloc(str_len * sizeof *str);
          if (irp->IoStatus.Information == 0) {
              DBG("wv_palloc DeviceTextLocationInformation\n");
              status = STATUS_INSUFFICIENT_RESOURCES;
              goto alloc_info;
            }
          RtlCopyMemory(
              (PWCHAR) irp->IoStatus.Information,
              str,
              str_len * sizeof (WCHAR)
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

    return driver__complete_irp(irp, irp->IoStatus.Information, status);
  }

NTSTATUS STDCALL AoeBusAttachFdo(
    IN PDRIVER_OBJECT driver_obj,
    IN PDEVICE_OBJECT pdo
  ) {
    KIRQL irql;
    NTSTATUS status;
    PLIST_ENTRY walker;

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
 * Create the AoE bus.
 *
 * @ret         TRUE for success, else FALSE.
 */
winvblock__bool AoeBusCreate(IN PDRIVER_OBJECT driver_obj) {
    NTSTATUS status;

    /* Do we already have our main bus? */
    if (AoeBusMain.Fdo) {
        DBG("AoeBusCreate called twice.\n");
        return FALSE;
      }
    /* Initialize the bus. */
    WvBusInit(&AoeBusMain);
    /* Create the bus FDO. */
    status = IoCreateDevice(
        driver_obj,
        0,
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
    AoeBusDummyIds = &AoeBusDummyIds_;
    AoeBusMain.QueryDevText = AoeBusPnpQueryDevText_;
    AoeBusMain.Fdo->Flags |= DO_DIRECT_IO;         /* FIXME? */
    AoeBusMain.Fdo->Flags |= DO_POWER_INRUSH;      /* FIXME? */
    AoeBusMain.Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    #ifdef RIS
    AoeBusMain.State = WvlBusStateStarted;
    #endif
    /* All done. */
    DBG("Exit\n");
    return TRUE;

    IoDeleteSymbolicLink(&AoeBusDosname_);
    err_dos_symlink:

    IoDeleteDevice(AoeBusMain.Fdo);
    err_fdo:

    DBG("Exit with failure\n");
    return FALSE;
  }

/**
 * Add a child node to the AoE bus.
 *
 * @v Dev               Points to the child device to add.
 * @ret                 TRUE for success, FALSE for failure.
 */
winvblock__bool STDCALL AoeBusAddDev(
    IN OUT WV_SP_DEV_T Dev
  ) {
    /* The new node's device object. */
    PDEVICE_OBJECT dev_obj;

    DBG("Entry\n");
    if (!AoeBusMain.Fdo || !Dev) {
        DBG("No bus or no device!\n");
        return FALSE;
      }
    /* Create the child device. */
    dev_obj = WvDevCreatePdo(Dev);
    if (!dev_obj) {
        DBG("PDO creation failed!\n");
        return FALSE;
      }
    WvBusInitNode(&Dev->BusNode, dev_obj);
    /* Associate the parent bus. */
    Dev->Parent = AoeBusMain.Fdo;
    /*
     * Initialize the device.  For disks, this routine is responsible for
     * determining the disk's geometry appropriately for AoE disks.
     */
    Dev->Ops.Init(Dev);
    dev_obj->Flags &= ~DO_DEVICE_INITIALIZING;
    /* Add the new PDO device to the bus' list of children. */
    WvBusAddNode(&AoeBusMain, &Dev->BusNode);
    Dev->DevNum = WvBusGetNodeNum(&Dev->BusNode);

    DBG("Exit\n");
    return TRUE;
  }
