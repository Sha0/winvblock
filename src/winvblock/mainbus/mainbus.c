/**
 * Copyright (C) 2009-2012, Shao Miller <sha0.miller@gmail.com>.
 * Copyright 2006-2008, V.
 * For WinAoE contact information, see http://winaoe.org/
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
 * WinVBlock driver bus specifics.
 */

#include <stdio.h>
#include <ntddk.h>
#include <scsi.h>

#include "portable.h"
#include "winvblock.h"
#include "thread.h"
#include "wv_stdlib.h"
#include "wv_string.h"
#include "irp.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "registry.h"
#include "mount.h"
#include "x86.h"
#include "safehook.h"
#include "filedisk.h"
#include "dummy.h"
#include "memdisk.h"
#include "debug.h"

/* Names for the main bus. */
#define WV_M_BUS_NAME (L"\\Device\\" WVL_M_WLIT)
#define WV_M_BUS_DOSNAME (L"\\DosDevices\\" WVL_M_WLIT)

/** Public functions */
NTSTATUS STDCALL WvBusEstablish(IN PUNICODE_STRING);
NTSTATUS STDCALL WvBusAttach(IN PDEVICE_OBJECT);
VOID WvBusCleanup(void);
NTSTATUS STDCALL WvBusDevCtl(IN PIRP, IN ULONG POINTER_ALIGNMENT);
WVL_F_BUS_PNP WvBusPnpQueryDevText;

/** Public objects */
UNICODE_STRING WvBusName = {
    sizeof WV_M_BUS_NAME - sizeof (WCHAR),
    sizeof WV_M_BUS_NAME - sizeof (WCHAR),
    WV_M_BUS_NAME
  };
UNICODE_STRING WvBusDosname = {
    sizeof WV_M_BUS_DOSNAME - sizeof (WCHAR),
    sizeof WV_M_BUS_DOSNAME - sizeof (WCHAR),
    WV_M_BUS_DOSNAME
  };
BOOLEAN WvSymlinkDone = FALSE;
KSPIN_LOCK WvBusPdoLock;
/* The main bus. */
WVL_S_BUS_T WvBus = {0};
WV_S_DEV_T WvBusDev = {0};

/** Private function declarations */

/* TODO: DRIVER_ADD_DEVICE isn't available in DDK 3790.1830, it seems */
static DRIVER_ADD_DEVICE WvMainBusDriveDevice;
static DRIVER_UNLOAD WvMainBusUnload;

/** Private objects */

static WVL_S_BUS_NODE WvBusSafeHookChild;

/** This mini-driver */
static S_WVL_MINI_DRIVER * WvMainBusMiniDriver;

/**
 * The mini-driver entry-point
 *
 * @param drv_obj
 *   The driver object provided by the caller
 *
 * @param reg_path
 *   The Registry path provided by the caller
 *
 * @return
 *   The status
 */
NTSTATUS STDCALL WvMainBusDriverEntry(
    IN DRIVER_OBJECT * drv_obj,
    IN UNICODE_STRING * reg_path
  ) {
    NTSTATUS status;

    /* Register this mini-driver */
    status = WvlRegisterMiniDriver(
        &WvMainBusMiniDriver,
        drv_obj,
        WvMainBusDriveDevice,
        WvMainBusUnload
      );
    if (!NT_SUCCESS(status))
      goto err_register;
    ASSERT(WvMainBusMiniDriver);

    /* Create the bus FDO and possibly the PDO */
    status = WvBusEstablish(reg_path);
    if (!NT_SUCCESS(status))
      goto err_bus;

    return status;

    WvBusCleanup();
    err_bus:

    WvlDeregisterMiniDriver(WvMainBusMiniDriver);
    err_register:

    return status;
  }

/**
 * Drive a supported device
 *
 * @param DriverObject
 *   The driver object provided by the caller
 *
 * @param PhysicalDeviceObject
 *   The PDO to probe and attach an FDO to
 *
 * @return
 *   The status of the operation
 */
static NTSTATUS STDCALL WvMainBusDriveDevice(
    IN PDRIVER_OBJECT driver_obj,
    IN PDEVICE_OBJECT pdo
  ) {
    return WvBusAttach(pdo);
  }

/**
 * Release resources and unwind state
 *
 * @param DriverObject
 *   The driver object provided by the caller
 */
static VOID STDCALL WvMainBusUnload(IN DRIVER_OBJECT * drv_obj) {
    WvBusCleanup();
  }

/**
 * Attempt to attach the bus to a PDO.
 *
 * @v pdo               The PDO to attach to.
 * @ret NTSTATUS        The status of the operation.
 *
 * This function will return a failure status if the bus is already attached.
 */
NTSTATUS STDCALL WvBusAttach(PDEVICE_OBJECT Pdo) {
    S_X86_SEG16OFF16 int_13h;
    KIRQL irql;
    NTSTATUS status;
    PDEVICE_OBJECT lower;
    PDEVICE_OBJECT safe_hook_child;

    /* Allow our own raw PDOs to be installed */
    if (Pdo->DriverObject == WvDriverObj)
      return STATUS_SUCCESS;

    /* Do we alreay have our main bus? */
    KeAcquireSpinLock(&WvBusPdoLock, &irql);
    if (WvBus.Pdo) {
        KeReleaseSpinLock(&WvBusPdoLock, irql);
        DBG("Already have the main bus.  Refusing.\n");
        status = STATUS_NOT_SUPPORTED;
        goto err_already_established;
      }
    WvBus.Pdo = Pdo;
    KeReleaseSpinLock(&WvBusPdoLock, irql);
    WvBus.State = WvlBusStateStarted;
    /* Attach the FDO to the PDO. */
    lower = IoAttachDeviceToDeviceStack(
        WvBus.Fdo,
        Pdo
      );
    if (lower == NULL) {
        status = STATUS_NO_SUCH_DEVICE;
        DBG("IoAttachDeviceToDeviceStack() failed!\n");
        goto err_attach;
      }
    DBG("Attached to PDO %p.\n", Pdo);
    WvBus.LowerDeviceObject = lower;

    /* Probe for disks. */
    int_13h.Segment = 0;
    int_13h.Offset = 0x13 * sizeof int_13h;
    safe_hook_child = WvSafeHookProbe(&int_13h, WvBus.Fdo);
    if (safe_hook_child) {
        WvlBusInitNode(&WvBusSafeHookChild, safe_hook_child);
        WvlBusAddNode(&WvBus, &WvBusSafeHookChild);
      }
    WvMemdiskFind();

    return STATUS_SUCCESS;

    IoDetachDevice(lower);
    err_attach:

    WvBus.Pdo = NULL;
    err_already_established:

    DBG("Failed to attach.\n");
    return status;
  }

/* Handle an IRP. */
static NTSTATUS WvBusIrpDispatch(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc;

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    switch (io_stack_loc->MajorFunction) {
        case IRP_MJ_PNP:
          return WvlBusPnp(&WvBus, irp);

        case IRP_MJ_DEVICE_CONTROL: {
            ULONG POINTER_ALIGNMENT code;

            code = io_stack_loc->Parameters.DeviceIoControl.IoControlCode;
            return WvBusDevCtl(irp, code);
          }

        case IRP_MJ_POWER:
          return WvlIrpPassPowerToLower(WvBus.LowerDeviceObject, irp);

        case IRP_MJ_CREATE:
        case IRP_MJ_CLOSE:
          /* Always succeed with nothing to do. */
          return WvlIrpComplete(irp, 0, STATUS_SUCCESS);

        case IRP_MJ_SYSTEM_CONTROL:
          return WvlIrpPassToLower(WvBus.LowerDeviceObject, irp);

        default:
          ;
      }
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
  }

/* Establish the bus FDO, thread, and possibly PDO. */
NTSTATUS STDCALL WvBusEstablish(IN PUNICODE_STRING RegistryPath) {
    NTSTATUS status;
    PDEVICE_OBJECT fdo = NULL;
    HANDLE reg_key;
    UINT32 pdo_done = 0;
    PDEVICE_OBJECT pdo = NULL;

    /* Initialize the spin-lock for WvBusAttach(). */
    KeInitializeSpinLock(&WvBusPdoLock);

    /* Initialize the bus. */
    WvlBusInit(&WvBus);
    WvDevInit(&WvBusDev);

    /* Create the bus FDO. */
    status = IoCreateDevice(
        WvDriverObj,
        sizeof (WV_S_DEV_EXT),
        &WvBusName,
        FILE_DEVICE_CONTROLLER,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &fdo
      );
    if (!NT_SUCCESS(status)) {
        DBG("IoCreateDevice() failed!\n");
        goto err_fdo;
      }
    WvBus.Fdo = fdo;
    WvBusDev.Self = WvBus.Fdo;
    WvBusDev.IsBus = TRUE;
    WvBus.QueryDevText = WvBusPnpQueryDevText;
    WvDevForDevObj(WvBus.Fdo, &WvBusDev);
    WvDevSetIrpHandler(WvBus.Fdo, WvBusIrpDispatch);
    WvBus.Fdo->Flags |= DO_DIRECT_IO;         /* FIXME? */
    WvBus.Fdo->Flags |= DO_POWER_INRUSH;      /* FIXME? */
    WvBus.Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    #ifdef RIS
    WvBus.Dev.State = Started;
    #endif

    /* DosDevice symlink. */
    status = IoCreateSymbolicLink(
        &WvBusDosname,
        &WvBusName
      );
    if (!NT_SUCCESS(status)) {
        DBG("IoCreateSymbolicLink() failed!\n");
        goto err_dos_symlink;
      }
    WvSymlinkDone = TRUE;

    /* Open our Registry path. */
    status = WvlRegOpenKey(RegistryPath->Buffer, &reg_key);
    if (!NT_SUCCESS(status)) {
        DBG("Couldn't open Registry path!\n");
        goto err_reg;
      }

    /* Check the Registry to see if we've already got a PDO. */
    status = WvlRegFetchDword(reg_key, L"PdoDone", &pdo_done);
    if (NT_SUCCESS(status) && pdo_done) {
        WvlRegCloseKey(reg_key);
        return status;
      }

    /* If not, create a root-enumerated PDO for our bus. */
    IoReportDetectedDevice(
        WvDriverObj,
        InterfaceTypeUndefined,
        -1,
        -1,
        NULL,
        NULL,
        FALSE,
        &pdo
      );
    if (pdo == NULL) {
        DBG("IoReportDetectedDevice() went wrong!  Exiting.\n");
        status = STATUS_UNSUCCESSFUL;
        goto err_pdo;
      }

    /* Remember that we have a PDO for next time. */
    status = WvlRegStoreDword(reg_key, L"PdoDone", 1);
    if (!NT_SUCCESS(status)) {
        DBG("Couldn't save PdoDone to Registry.  Oh well.\n");
      }
    /* Attach FDO to PDO. */
    status = WvBusAttach(pdo);
    if (!NT_SUCCESS(status)) {
        DBG("WvBusAttach() went wrong!\n");
        goto err_attach;
      }
    /* PDO created, FDO attached.  All done. */
    return STATUS_SUCCESS;

    err_attach:

    /* Should we really delete an IoReportDetectedDevice() device? */
    IoDeleteDevice(pdo);
    err_pdo:

    WvlRegCloseKey(reg_key);
    err_reg:

    /* Cleanup by WvBusCleanup() */
    err_dos_symlink:

    /* Cleanup by WvBusCleanup() */
    err_fdo:

    return status;
  }

/* Tear down WinVBlock bus resources. */
VOID WvBusCleanup(void) {
    if (WvSymlinkDone)
      IoDeleteSymbolicLink(&WvBusDosname);
    /* Similar to IRP_MJ_PNP:IRP_MN_REMOVE_DEVICE */
    WvlBusClear(&WvBus);
    IoDeleteDevice(WvBus.Fdo);
    WvBus.Fdo = NULL;
    return;
  }

/* TODO: Cosmetic changes */
BOOLEAN STDCALL WvBusAddDev(
    IN OUT WV_SP_DEV_T Dev
  ) {
    /* The new node's device object. */
    PDEVICE_OBJECT dev_obj;

    DBG("Entry\n");
    if (!WvBus.Fdo || !Dev) {
        DBG("No bus or no device!\n");
        return FALSE;
      }
    /* Create the child device, if needed. */
    dev_obj = Dev->Self;
    if (!dev_obj) {
        dev_obj = WvDevCreatePdo(Dev);
        if (!dev_obj) {
            DBG("PDO creation failed!\n");
            return FALSE;
          }
      }
    WvlBusInitNode(&Dev->BusNode, dev_obj);
    /* Associate the parent bus. */
    Dev->Parent = WvBus.Fdo;
    /*
     * Initialize the device.  For disks, this routine is responsible for
     * determining the disk's geometry appropriately for RAM/file disks.
     */
    if (Dev->Ops.Init)
      Dev->Ops.Init(Dev);
    dev_obj->Flags &= ~DO_DEVICE_INITIALIZING;
    /* Add the new PDO device to the bus' list of children. */
    WvlBusAddNode(&WvBus, &Dev->BusNode);

    DBG("Added device %p with PDO %p.\n", Dev, Dev->Self);
    return TRUE;
  }

/**
 * Remove a device node from the WinVBlock bus.
 *
 * @v Dev               The device to remove.
 * @ret NTSTATUS        The status of the operation.
 */
NTSTATUS STDCALL WvBusRemoveDev(IN WV_SP_DEV_T Dev) {
    NTSTATUS status;

    if (!Dev->BusNode.Linked) {
        WvDevClose(Dev);
        IoDeleteDevice(Dev->Self);
        WvDevFree(Dev);
      } else
        WvlBusRemoveNode(&Dev->BusNode);
    return STATUS_SUCCESS;
  }

static NTSTATUS STDCALL WvBusDevCtlDetach(
    IN PIRP irp
  ) {
    UINT32 unit_num;
    WVL_SP_BUS_NODE walker;
    WV_SP_DEV_T dev = NULL;

    unit_num = *((PUINT32) irp->AssociatedIrp.SystemBuffer);
    DBG("Request to detach unit %d...\n", unit_num);

    walker = NULL;
    /* For each node on the bus... */
    WvlBusLock(&WvBus);
    while (walker = WvlBusGetNextNode(&WvBus, walker)) {
        /* If the unit number matches... */
        if (WvlBusGetNodeNum(walker) == unit_num) {
            dev = WvDevFromDevObj(WvlBusGetNodePdo(walker));
            /* If it's not a boot-time device... */
            if (dev->Boot) {
                DBG("Cannot detach a boot-time device.\n");
                /* Signal error. */
                dev = NULL;
              }
            break;
          }
      }
    WvlBusUnlock(&WvBus);
    if (!dev) {
        DBG("Unit %d not found.\n", unit_num);
        return WvlIrpComplete(irp, 0, STATUS_INVALID_PARAMETER);
      }
    /* Detach the node. */
    WvBusRemoveDev(dev);
    DBG("Removed unit %d.\n", unit_num);
    return WvlIrpComplete(irp, 0, STATUS_SUCCESS);
  }

NTSTATUS STDCALL WvBusDevCtl(
    IN PIRP irp,
    IN ULONG POINTER_ALIGNMENT code
  ) {
    NTSTATUS status;

    switch (code) {
        case IOCTL_FILE_ATTACH:
          status = WvFilediskAttach(irp);
          break;

        case IOCTL_FILE_DETACH:
          return WvBusDevCtlDetach(irp);

        case IOCTL_WV_DUMMY:
          return WvDummyIoctl(irp);

        default:
          irp->IoStatus.Information = 0;
          status = STATUS_INVALID_DEVICE_REQUEST;
      }

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

NTSTATUS STDCALL WvBusPnpQueryDevText(
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
          str_len = swprintf(*str, WVL_M_WLIT L" Bus") + 1;
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

WVL_M_LIB DEVICE_OBJECT * WvBusFdo(void) {
    return WvBus.Fdo;
  }
