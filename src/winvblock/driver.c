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
 * Driver specifics.
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
#include "probe.h"
#include "debug.h"

/* From bus.c */
extern WVL_S_BUS_T WvBus;
extern WV_S_DEV_T WvBusDev;
extern UNICODE_STRING WvBusName;
extern UNICODE_STRING WvBusDosname;
extern PETHREAD WvBusThread;
extern NTSTATUS STDCALL WvBusDevCtl(
    IN PIRP,
    IN ULONG POINTER_ALIGNMENT
  );
extern WVL_F_BUS_PNP WvBusPnpQueryDevText;
extern NTSTATUS STDCALL WvBusEstablish(IN PUNICODE_STRING);

/* Exported. */
PDRIVER_OBJECT WvDriverObj = NULL;

/* Globals. */
static PVOID WvDriverStateHandle;
static BOOLEAN WvDriverStarted = FALSE;
/* Contains TXTSETUP.SIF/BOOT.INI-style OsLoadOptions parameters. */
static LPWSTR WvOsLoadOpts = NULL;

/* Forward declarations. */
static DRIVER_DISPATCH WvIrpNotSupported;
static __drv_dispatchType(IRP_MJ_POWER) DRIVER_DISPATCH WvIrpPower;
static
  __drv_dispatchType(IRP_MJ_CREATE)
  __drv_dispatchType(IRP_MJ_CLOSE)
  DRIVER_DISPATCH WvIrpCreateClose;
static __drv_dispatchType(IRP_MJ_SYSTEM_CONTROL)
  DRIVER_DISPATCH WvIrpSysCtl;
static __drv_dispatchType(IRP_MJ_DEVICE_CONTROL)
  DRIVER_DISPATCH WvIrpDevCtl;
static __drv_dispatchType(IRP_MJ_SCSI) DRIVER_DISPATCH WvIrpScsi;
static __drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH WvIrpPnp;
static DRIVER_UNLOAD WvUnload;

static LPWSTR STDCALL WvGetOpt(IN LPWSTR opt_name) {
    LPWSTR our_opts, the_opt;
    WCHAR our_sig[] = L"WINVBLOCK=";
    /* To produce constant integer expressions. */
    enum {
        our_sig_len_bytes = sizeof ( our_sig ) - sizeof ( WCHAR ),
        our_sig_len = our_sig_len_bytes / sizeof ( WCHAR )
      };
    size_t opt_name_len, opt_name_len_bytes;

    if (!WvOsLoadOpts || !opt_name)
      return NULL;

    /* Find /WINVBLOCK= options. */
    our_opts = WvOsLoadOpts;
    while (*our_opts != L'\0') {
        if (!wv_memcmpeq(our_opts, our_sig, our_sig_len_bytes)) {
            our_opts++;
            continue;
          }
        our_opts += our_sig_len;
        break;
      }

    /* Search for the specific option. */
    the_opt = our_opts;
    opt_name_len = wcslen(opt_name);
    opt_name_len_bytes = opt_name_len * sizeof (WCHAR);
    while (*the_opt != L'\0' && *the_opt != L' ') {
        if (!wv_memcmpeq(the_opt, opt_name, opt_name_len_bytes)) {
            while (*the_opt != L'\0' && *the_opt != L' ' && *the_opt != L',')
              the_opt++;
            continue;
          }
        the_opt += opt_name_len;
        break;
      }

    if (*the_opt == L'\0' || *the_opt == L' ')
      return NULL;

    /* Next should come "=". */
    if (*the_opt != L'=')
      return NULL;

    /*
     * And finally our option's value.  The caller needs
     * to worry about looking past the end of the option.
     */
    the_opt++;
    if (*the_opt == L'\0' || *the_opt == L' ')
      return NULL;
    return the_opt;
  }

static NTSTATUS STDCALL WvAttachFdo(
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT Pdo
  ) {
    NTSTATUS status;
    PLIST_ENTRY walker;
    PDEVICE_OBJECT fdo = NULL;
    static WV_S_DEV_IRP_MJ irp_mj = {
        (WV_FP_DEV_DISPATCH) 0,
        (WV_FP_DEV_DISPATCH) 0,
        (WV_FP_DEV_CTL) 0,
        (WV_FP_DEV_SCSI) 0,
        (WV_FP_DEV_PNP) 0,
      };

    DBG("Entry\n");
    /* Do we alreay have our main bus? */
    if (WvBus.Fdo) {
        DBG("Already have the main bus.  Refusing.\n");
        status = STATUS_NOT_SUPPORTED;
        goto err_already_established;
      }
    /* Initialize the bus. */
    WvlBusInit(&WvBus);
    WvDevInit(&WvBusDev);
    /* Create the bus FDO. */
    status = IoCreateDevice(
        DriverObject,
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
    /* DosDevice symlink. */
    status = IoCreateSymbolicLink(
        &WvBusDosname,
        &WvBusName
      );
    if (!NT_SUCCESS(status)) {
        DBG("IoCreateSymbolicLink() failed!\n");
        goto err_dos_symlink;
      }
    /* Set associations for the bus, device, FDO, PDO. */
    WvDevForDevObj(fdo, &WvBusDev);
    WvBusDev.Self = WvBus.Fdo = fdo;
    WvBusDev.IsBus = TRUE;
    WvBusDev.IrpMj = &irp_mj;
    WvBus.QueryDevText = WvBusPnpQueryDevText;
    WvBus.Pdo = Pdo;
    fdo->Flags |= DO_DIRECT_IO;         /* FIXME? */
    fdo->Flags |= DO_POWER_INRUSH;      /* FIXME? */
    /* Attach the FDO to the PDO. */
    WvBus.LowerDeviceObject = IoAttachDeviceToDeviceStack(
        fdo,
        Pdo
      );
    if (WvBus.LowerDeviceObject == NULL) {
        status = STATUS_NO_SUCH_DEVICE;
        DBG("IoAttachDeviceToDeviceStack() failed!\n");
        goto err_attach;
      }
    status = WvlBusStartThread(&WvBus, &WvBusThread);
    if (!NT_SUCCESS(status)) {
        DBG("Couldn't start bus thread!\n");
        goto err_thread;
      }
    /* Ok! */
    fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    #ifdef RIS
    WvBus.Dev.State = Started;
    #endif
    DBG("Exit\n");
    return STATUS_SUCCESS;

    err_thread:

    err_attach:

    IoDeleteSymbolicLink(&WvBusDosname);
    err_dos_symlink:

    IoDeleteDevice(fdo);
    err_fdo:

    err_already_established:

    DBG("Exit with failure\n");
    return status;
  }

/*
 * Note the exception to the function naming convention.
 * TODO: See if a Makefile change is good enough.
 */
NTSTATUS STDCALL DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
  ) {
    NTSTATUS status;
    int i;

    DBG("Entry\n");
    if (WvDriverObj) {
        DBG("Re-entry not allowed!\n");
        return STATUS_NOT_SUPPORTED;
      }
    WvDriverObj = DriverObject;
    if (WvDriverStarted)
      return STATUS_SUCCESS;
    Debug_Initialize();
    status = WvlRegNoteOsLoadOpts(&WvOsLoadOpts);
    if (!NT_SUCCESS(status))
      return WvlError("WvlRegNoteOsLoadOpts", status);

    WvDriverStateHandle = NULL;

    if ((WvDriverStateHandle = PoRegisterSystemState(
        NULL,
        ES_CONTINUOUS
      )) == NULL) {
        DBG("Could not set system state to ES_CONTINUOUS!!\n");
      }
    /*
     * Set up IRP MajorFunction function table for devices
     * this driver handles.
     */
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
      DriverObject->MajorFunction[i] = WvIrpNotSupported;
    DriverObject->MajorFunction[IRP_MJ_PNP] = WvIrpPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = WvIrpPower;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = WvIrpCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = WvIrpCreateClose;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] =
      WvIrpSysCtl;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] =
      WvIrpDevCtl;
    DriverObject->MajorFunction[IRP_MJ_SCSI] = WvIrpScsi;
    /* Set the driver Unload callback. */
    DriverObject->DriverUnload = WvUnload;
    /* Set the driver AddDevice callback. */
    DriverObject->DriverExtension->AddDevice = WvAttachFdo;
    /* Initialize various modules. */
    disk__module_init();        /* TODO: Check for error. */
    WvFilediskModuleInit();     /* TODO: Check for error. */
    ramdisk__module_init();     /* TODO: Check for error. */

    /* Establish the bus PDO. */
    status = WvBusEstablish(RegistryPath);
    if(!NT_SUCCESS(status))
      goto err_bus;

    WvDriverStarted = TRUE;
    DBG("Exit\n");
    return STATUS_SUCCESS;

    err_bus:

    WvUnload(DriverObject);
    DBG("Exit due to failure\n");
    return status;
  }

static NTSTATUS STDCALL WvIrpNotSupported(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return irp->IoStatus.Status;
  }

/* Handle a power IRP. */
static NTSTATUS WvIrpPower(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    WV_SP_DEV_T dev;

    #ifdef DEBUGIRPS
    WvlDebugIrpStart(dev_obj, irp);
    #endif
    /* Check for a bus IRP. */
    if (dev_obj == WvBus.Fdo)
      return WvlBusPower(&WvBus, irp);
    /* WvDevFromDevObj() checks for a NULL dev_obj */
    dev = WvDevFromDevObj(dev_obj);
    /* Check that the device exists. */
    if (!dev || dev->State == WvDevStateDeleted) {
        /* Even if it doesn't, a power IRP is important! */
        PoStartNextPowerIrp(irp);
        return WvlIrpComplete(irp, 0, STATUS_NO_SUCH_DEVICE);
      }
    /* Call the particular device's power handler. */
    if (dev->IrpMj && dev->IrpMj->Power)
      return dev->IrpMj->Power(dev, irp);
    /* Otherwise, we don't support the IRP. */
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
  }

/* Handle an IRP_MJ_CREATE or IRP_MJ_CLOSE IRP. */
static NTSTATUS WvIrpCreateClose(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    WV_SP_DEV_T dev;

    #ifdef DEBUGIRPS
    WvlDebugIrpStart(dev_obj, irp);
    #endif
    /* Check for a bus IRP. */
    if (dev_obj == WvBus.Fdo)
      /* Succeed with nothing to do. */
      return WvlIrpComplete(irp, 0, STATUS_SUCCESS);
    /* WvDevFromDevObj() checks for a NULL dev_obj */
    dev = WvDevFromDevObj(dev_obj);
    /* Check that the device exists. */
    if (!dev || dev->State == WvDevStateDeleted)
      return WvlIrpComplete(irp, 0, STATUS_NO_SUCH_DEVICE);
    /* Always succeed with nothing to do. */
    return WvlIrpComplete(irp, 0, STATUS_SUCCESS);
  }

/* Handle an IRP_MJ_SYSTEM_CONTROL IRP. */
static NTSTATUS WvIrpSysCtl(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    WV_SP_DEV_T dev;

    #ifdef DEBUGIRPS
    WvlDebugIrpStart(dev_obj, irp);
    #endif
    /* Check for a bus IRP. */
    if (dev_obj == WvBus.Fdo)
      return WvlBusSysCtl(&WvBus, irp);
    /* WvDevFromDevObj() checks for a NULL dev_obj */
    dev = WvDevFromDevObj(dev_obj);
    /* Check that the device exists. */
    if (!dev || dev->State == WvDevStateDeleted)
      return WvlIrpComplete(irp, 0, STATUS_NO_SUCH_DEVICE);
    /* Call the particular device's power handler. */
    if (dev->IrpMj && dev->IrpMj->SysCtl)
      return dev->IrpMj->SysCtl(dev, irp);
    /* Otherwise, we don't support the IRP. */
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
  }

/* Handle an IRP_MJ_DEVICE_CONTROL IRP. */
static NTSTATUS WvIrpDevCtl(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    WV_SP_DEV_T dev;
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);

    #ifdef DEBUGIRPS
    WvlDebugIrpStart(dev_obj, irp);
    #endif
    /* Check for a bus IRP. */
    if (dev_obj == WvBus.Fdo) {
        return WvBusDevCtl(
            irp,
            io_stack_loc->Parameters.DeviceIoControl.IoControlCode
          );
      }
    /* WvDevFromDevObj() checks for a NULL dev_obj */
    dev = WvDevFromDevObj(dev_obj);
    /* Check that the device exists. */
    if (!dev || dev->State == WvDevStateDeleted)
      return WvlIrpComplete(irp, 0, STATUS_NO_SUCH_DEVICE);
    /* Call the particular device's power handler. */
    if (dev->IrpMj && dev->IrpMj->DevCtl) {
        return dev->IrpMj->DevCtl(
            dev,
            irp,
            io_stack_loc->Parameters.DeviceIoControl.IoControlCode
          );
      }
    /* Otherwise, we don't support the IRP. */
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
  }

/* Handle an IRP_MJ_SCSI IRP. */
static NTSTATUS WvIrpScsi(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    WV_SP_DEV_T dev;
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);

    #ifdef DEBUGIRPS
    WvlDebugIrpStart(dev_obj, irp);
    #endif
    /* Check for a bus IRP. */
    if (dev_obj == WvBus.Fdo)
      return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
    /* WvDevFromDevObj() checks for a NULL dev_obj */
    dev = WvDevFromDevObj(dev_obj);
    /* Check that the device exists. */
    if (!dev || dev->State == WvDevStateDeleted)
      return WvlIrpComplete(irp, 0, STATUS_NO_SUCH_DEVICE);
    /* Call the particular device's power handler. */
    if (dev->IrpMj && dev->IrpMj->Scsi) {
        return dev->IrpMj->Scsi(
            dev,
            irp,
            io_stack_loc->Parameters.Scsi.Srb->Function
          );
      }
    /* Otherwise, we don't support the IRP. */
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
  }

/* Handle an IRP_MJ_PNP IRP. */
static NTSTATUS WvIrpPnp(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    WV_SP_DEV_T dev;
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;

    #ifdef DEBUGIRPS
    WvlDebugIrpStart(dev_obj, irp);
    #endif
    /* Check for a bus IRP. */
    if (dev_obj == WvBus.Fdo) {
        if (io_stack_loc->MinorFunction == IRP_MN_QUERY_DEVICE_RELATIONS)
          WvProbeDisks();
        return WvlBusPnpIrp(&WvBus, irp, io_stack_loc->MinorFunction);
      }
    /* WvDevFromDevObj() checks for a NULL dev_obj */
    dev = WvDevFromDevObj(dev_obj);
    /* Check that the device exists. */
    if (!dev || dev->State == WvDevStateDeleted)
      return WvlIrpComplete(irp, 0, STATUS_NO_SUCH_DEVICE);
    /* Call the particular device's power handler. */
    if (dev->IrpMj && dev->IrpMj->Pnp) {
        status = dev->IrpMj->Pnp(
            dev,
            irp,
            io_stack_loc->MinorFunction
          );
        if (dev->IsBus) {
            dev->OldState = WvBus.OldState;
            dev->State = WvBus.State;
          }
        return status;
      }
    /* Otherwise, we don't support the IRP. */
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
  }

static WVL_F_THREAD_ITEM WvTestThreadTestStop;
static VOID STDCALL WvTestThreadTestStop(IN OUT WVL_SP_THREAD_ITEM item) {
    static WVL_S_THREAD_ITEM stopper = { {0}, WvTestThreadTestStop };
    static WVL_SP_THREAD thread = NULL;
    static KEVENT event;

    /* If we have an item, we're in the thread.  Remember it. */
    if (item) {
        thread = WvlThreadGetCurrent();
        KeSetEvent(&event, 0, FALSE);
        return;
      }
    /* Otherwise, enqueue ourself. */
    KeInitializeEvent(&event, SynchronizationEvent, FALSE);
    WvlThreadTest(&stopper);
    KeWaitForSingleObject(
        &event,
        Executive,
        KernelMode,
        FALSE,
        NULL
      );
    /* Now we should know the thread.  Stop it and wait. */
    WvlThreadSendStopAndWait(thread);
    return;
  }

static VOID STDCALL WvUnload(IN PDRIVER_OBJECT DriverObject) {
    DBG("Unloading...\n");
    WvBus.Stop = TRUE;
    KeSetEvent(&WvBus.ThreadSignal, 0, FALSE);
    if (WvBusThread) {
        KeWaitForSingleObject(
            WvBusThread,
            Executive,
            KernelMode,
            FALSE,
            NULL
          );
        ObDereferenceObject(WvBusThread);
      }
    WvTestThreadTestStop(NULL);
    if (WvDriverStateHandle != NULL)
      PoUnregisterSystemState(WvDriverStateHandle);
    IoDeleteSymbolicLink(&WvBusDosname);
    WvBus.Fdo = NULL;
    wv_free(WvOsLoadOpts);
    WvDriverStarted = FALSE;
    DBG("Done\n");
  }

NTSTATUS STDCALL WvDriverGetDevCapabilities(
    IN PDEVICE_OBJECT DevObj,
    IN PDEVICE_CAPABILITIES DevCapabilities
  ) {
    IO_STATUS_BLOCK io_status;
    KEVENT pnp_event;
    NTSTATUS status;
    PDEVICE_OBJECT target_obj;
    PIO_STACK_LOCATION io_stack_loc;
    PIRP pnp_irp;

    RtlZeroMemory(DevCapabilities, sizeof *DevCapabilities);
    DevCapabilities->Size = sizeof *DevCapabilities;
    DevCapabilities->Version = 1;
    DevCapabilities->Address = -1;
    DevCapabilities->UINumber = -1;

    KeInitializeEvent(&pnp_event, NotificationEvent, FALSE);
    target_obj = IoGetAttachedDeviceReference(DevObj);
    pnp_irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_PNP,
        target_obj,
        NULL,
        0,
        NULL,
        &pnp_event,
        &io_status
      );
    if (pnp_irp == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
      } else {
        pnp_irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        io_stack_loc = IoGetNextIrpStackLocation(pnp_irp);
        RtlZeroMemory(io_stack_loc, sizeof *io_stack_loc);
        io_stack_loc->MajorFunction = IRP_MJ_PNP;
        io_stack_loc->MinorFunction = IRP_MN_QUERY_CAPABILITIES;
        io_stack_loc->Parameters.DeviceCapabilities.Capabilities =
          DevCapabilities;
        status = IoCallDriver(target_obj, pnp_irp);
        if (status == STATUS_PENDING) {
            KeWaitForSingleObject(
                &pnp_event,
                Executive,
                KernelMode,
                FALSE,
                NULL
              );
            status = io_status.Status;
          }
      }
    ObDereferenceObject(target_obj);
    return status;
  }
