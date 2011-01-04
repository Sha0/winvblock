/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 * Copyright 2006-2008, V.
 * For WinAoE contact information, see http://winaoe.org/
 *
 * This file is part of WinVBlock, derived from WinAoE.
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
#include "libthread.h"
#include "wv_stdlib.h"
#include "wv_string.h"
#include "irp.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "registry.h"
#include "mount.h"
#include "filedisk.h"
#include "ramdisk.h"
#include "debug.h"

/* Names for the main bus. */
#define WV_M_BUS_NAME (L"\\Device\\" WVL_M_WLIT)
#define WV_M_BUS_DOSNAME (L"\\DosDevices\\" WVL_M_WLIT)

/* Globals. */
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
/* The main bus. */
WVL_S_BUS_T WvBus = {0};
WV_S_DEV_T WvBusDev = {0};

/* Forward declarations. */
NTSTATUS STDCALL WvBusDevCtl(
    IN PIRP,
    IN ULONG POINTER_ALIGNMENT
  );
WVL_F_BUS_PNP WvBusPnpQueryDevText;

static WVL_S_THREAD WvBusThread_ = {0};
static WVL_F_THREAD_ITEM WvBusThread;
static VOID STDCALL WvBusThread(IN OUT WVL_SP_THREAD_ITEM item) {
    LARGE_INTEGER timeout;
    PVOID signals[] = {&WvBusThread_.Signal, &WvBus.ThreadSignal};
    WVL_SP_THREAD_ITEM work_item;

    if (!item) {
        DBG("Bad call!\n");
        return;
      }

    /* Wake up at most every 30 seconds. */
    timeout.QuadPart = -300000000LL;
    WvBusThread_.State = WvlThreadStateStarted;

    do {
        WvlBusProcessWorkItems(&WvBus);

        /* Wait for the work signal or the timeout. */
        KeWaitForMultipleObjects(
            sizeof signals / sizeof *signals,
            signals,
            WaitAny,
            Executive,
            KernelMode,
            FALSE,
            &timeout,
            NULL
          );
        /* Reset the work signals. */
        KeResetEvent(&WvBusThread_.Signal);
        KeResetEvent(&WvBus.ThreadSignal);

        while (work_item = WvlThreadGetItem(&WvBusThread_))
          /* Launch the item. */
          work_item->Func(work_item);
        if (WvBusThread_.State == WvlThreadStateStopping)
          WvBusThread_.State = WvlThreadStateStopped;
      } while (
        (WvBusThread_.State == WvlThreadStateStarted) ||
        (WvBusThread_.State == WvlThreadStateStopping)
      );
    WvlBusCancelWorkItems(&WvBus);
    return;
  }

NTSTATUS STDCALL WvBusAttach(PDEVICE_OBJECT pdo) {
    NTSTATUS status;
    PDEVICE_OBJECT lower;
    static WV_S_DEV_IRP_MJ irp_mj = {
        (WV_FP_DEV_DISPATCH) 0,
        (WV_FP_DEV_DISPATCH) 0,
        (WV_FP_DEV_CTL) 0,
        (WV_FP_DEV_SCSI) 0,
        (WV_FP_DEV_PNP) 0,
      };

    DBG("Attaching to PDO %p...\n", pdo);
    /* Do we alreay have our main bus? */
    if (WvBus.Pdo) {
        DBG("Already have the main bus.  Refusing.\n");
        status = STATUS_NOT_SUPPORTED;
        goto err_already_established;
      }
    /* Attach the FDO to the PDO. */
    lower = IoAttachDeviceToDeviceStack(
        WvBus.Fdo,
        pdo
      );
    if (lower == NULL) {
        status = STATUS_NO_SUCH_DEVICE;
        DBG("IoAttachDeviceToDeviceStack() failed!\n");
        goto err_attach;
      }
    /* Set associations for the bus, device, FDO, PDO. */
    WvDevForDevObj(WvBus.Fdo, &WvBusDev);
    WvBusDev.Self = WvBus.Fdo;
    WvBusDev.IsBus = TRUE;
    WvBusDev.IrpMj = &irp_mj;
    WvBus.QueryDevText = WvBusPnpQueryDevText;
    WvBus.Pdo = pdo;
    WvBus.LowerDeviceObject = lower;
    WvBus.Fdo->Flags |= DO_DIRECT_IO;         /* FIXME? */
    WvBus.Fdo->Flags |= DO_POWER_INRUSH;      /* FIXME? */
    WvBus.Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    #ifdef RIS
    WvBus.Dev.State = Started;
    #endif
    DBG("Exit\n");
    return STATUS_SUCCESS;

    err_attach:

    err_already_established:

    DBG("Failed to attach.\n");
    return status;
  }

/* Establish the bus PDO. */
NTSTATUS STDCALL WvBusEstablish(IN PUNICODE_STRING RegistryPath) {
    NTSTATUS status;
    PDEVICE_OBJECT fdo = NULL;
    HANDLE reg_key;
    UINT32 pdo_done = 0;
    PDEVICE_OBJECT pdo = NULL;

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

    /* Start thread. */
    DBG("Starting thread...\n");
    WvBusThread_.Main.Func = WvBusThread;
    status = WvlThreadStart(&WvBusThread_);
    if (!NT_SUCCESS(status)) {
        DBG("Couldn't start bus thread!\n");
        goto err_thread;
      }

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

    /* Create a root-enumerated PDO for our bus. */
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
    err_thread:

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
    if (WvBusThread_.State != WvlThreadStateNotStarted) {
        DBG("Stopping thread...\n");
        WvlThreadSendStopAndWait(&WvBusThread_);
      }
    if (WvBus.Fdo) {
        IoDeleteDevice(WvBus.Fdo);
        WvBus.Fdo = NULL;
      }
    return;
  }

/**
 * Add a child node to the bus.
 *
 * @v Dev               Points to the child device to add.
 * @ret                 TRUE for success, FALSE for failure.
 */
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
    /* Create the child device. */
    dev_obj = WvDevCreatePdo(Dev);
    if (!dev_obj) {
        DBG("PDO creation failed!\n");
        return FALSE;
      }
    WvlBusInitNode(&Dev->BusNode, dev_obj);
    /* Associate the parent bus. */
    Dev->Parent = WvBus.Fdo;
    /*
     * Initialize the device.  For disks, this routine is responsible for
     * determining the disk's geometry appropriately for AoE/RAM/file disks.
     */
    Dev->Ops.Init(Dev);
    dev_obj->Flags &= ~DO_DEVICE_INITIALIZING;
    /* Add the new PDO device to the bus' list of children. */
    WvlBusAddNode(&WvBus, &Dev->BusNode);
    Dev->DevNum = WvlBusGetNodeNum(&Dev->BusNode);

    DBG("Exit\n");
    return TRUE;
  }

static NTSTATUS STDCALL WvBusDevCtlDetach(
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    UINT32 unit_num;
    WVL_SP_BUS_NODE walker;

    if (!(io_stack_loc->Control & SL_PENDING_RETURNED)) {
        NTSTATUS status;

        /* Enqueue the IRP. */
        status = WvlBusEnqueueIrp(&WvBus, irp);
        if (status != STATUS_PENDING)
          /* Problem. */
          return WvlIrpComplete(irp, 0, status);
        /* Ok. */
        return status;
      }
    /* If we get here, we should be called by WvlBusProcessWorkItems() */
    unit_num = *((PUINT32) irp->AssociatedIrp.SystemBuffer);
    DBG("Request to detach unit: %d\n", unit_num);

    walker = NULL;
    /* For each node on the bus... */
    while (walker = WvlBusGetNextNode(&WvBus, walker)) {
        WV_SP_DEV_T dev = WvDevFromDevObj(WvlBusGetNodePdo(walker));

        /* If the unit number matches... */
        if (WvlBusGetNodeNum(walker) == unit_num) {
            /* If it's not a boot-time device... */
            if (dev->Boot) {
                DBG("Cannot detach a boot-time device.\n");
                /* Signal error. */
                walker = NULL;
                break;
              }
            /* Detach the node and free it. */
            DBG("Removing unit %d\n", unit_num);
            WvlBusRemoveNode(walker);
            WvDevClose(dev);
            IoDeleteDevice(dev->Self);
            WvDevFree(dev);
            break;
          }
      }
    if (!walker)
      return WvlIrpComplete(irp, 0, STATUS_INVALID_PARAMETER);
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
