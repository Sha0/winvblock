/**
 * Copyright (C) 2009-2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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

#include "winvblock.h"
#include "wv_stdlib.h"
#include "wv_string.h"
#include "portable.h"
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
#define WV_M_BUS_NAME_ (L"\\Device\\" winvblock__literal_w)
#define WV_M_BUS_DOSNAME_ (L"\\DosDevices\\" winvblock__literal_w)

/* Exported. */
PDRIVER_OBJECT WvDriverObj = NULL;

/* Globals. */
static void * WvDriverStateHandle_;
static winvblock__bool WvDriverStarted_ = FALSE;
static PDEVICE_OBJECT WvDriverBusFdo_ = NULL;
static KSPIN_LOCK WvDriverBusFdoLock_;
static UNICODE_STRING WvDriverBusName_ = {
    sizeof WV_M_BUS_NAME_ - sizeof (WCHAR),
    sizeof WV_M_BUS_NAME_ - sizeof (WCHAR),
    WV_M_BUS_NAME_
  };
static UNICODE_STRING WvDriverBusDosname_ = {
    sizeof WV_M_BUS_DOSNAME_ - sizeof (WCHAR),
    sizeof WV_M_BUS_DOSNAME_ - sizeof (WCHAR),
    WV_M_BUS_DOSNAME_
  };
/* The main bus. */
static WV_S_BUS_T WvDriverBus_ = {0};
static WV_S_DEV_T WvDriverBusDev_ = {0};
static PETHREAD WvDriverBusThread_ = NULL;
/* Contains TXTSETUP.SIF/BOOT.INI-style OsLoadOptions parameters. */
static LPWSTR WvDriverOsLoadOpts_ = NULL;

/* Forward declarations. */
static driver__dispatch_func driver__dispatch_not_supported_;
static driver__dispatch_func driver__dispatch_power_;
static driver__dispatch_func driver__dispatch_create_close_;
static driver__dispatch_func driver__dispatch_sys_ctl_;
static driver__dispatch_func driver__dispatch_dev_ctl_;
static driver__dispatch_func driver__dispatch_scsi_;
static driver__dispatch_func driver__dispatch_pnp_;
static void STDCALL driver__unload_(IN PDRIVER_OBJECT);
static WV_F_DEV_DISPATCH WvDriverBusSysCtl_;
static WV_F_DEV_CTL WvDriverBusDevCtl_;
static WV_F_DEV_DISPATCH WvDriverBusPower_;
static WV_F_DEV_PNP WvDriverBusPnp_;
static WVL_F_BUS_PNP WvDriverBusPnpQueryDevText_;

static LPWSTR STDCALL get_opt(IN LPWSTR opt_name) {
    LPWSTR our_opts, the_opt;
    WCHAR our_sig[] = L"WINVBLOCK=";
    /* To produce constant integer expressions. */
    enum {
        our_sig_len_bytes = sizeof ( our_sig ) - sizeof ( WCHAR ),
        our_sig_len = our_sig_len_bytes / sizeof ( WCHAR )
      };
    size_t opt_name_len, opt_name_len_bytes;

    if (!WvDriverOsLoadOpts_ || !opt_name)
      return NULL;

    /* Find /WINVBLOCK= options. */
    our_opts = WvDriverOsLoadOpts_;
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

static NTSTATUS STDCALL driver__attach_fdo_(
    IN PDRIVER_OBJECT DriverObject,
    IN PDEVICE_OBJECT Pdo
  ) {
    KIRQL irql;
    NTSTATUS status;
    PLIST_ENTRY walker;
    PDEVICE_OBJECT fdo = NULL;
    static WV_S_DEV_IRP_MJ irp_mj = {
        WvDriverBusPower_,
        WvDriverBusSysCtl_,
        WvDriverBusDevCtl_,
        (WV_FP_DEV_SCSI) 0,
        WvDriverBusPnp_,
      };

    DBG("Entry\n");
    /* Do we alreay have our main bus? */
    if (WvDriverBusFdo_) {
        DBG("Already have the main bus.  Refusing.\n");
        status = STATUS_NOT_SUPPORTED;
        goto err_already_established;
      }
    /* Initialize the bus. */
    WvBusInit(&WvDriverBus_);
    WvDevInit(&WvDriverBusDev_);
    /* Create the bus FDO. */
    status = IoCreateDevice(
        DriverObject,
        sizeof (driver__dev_ext),
        &WvDriverBusName_,
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
        &WvDriverBusDosname_,
        &WvDriverBusName_
      );
    if (!NT_SUCCESS(status)) {
        DBG("IoCreateSymbolicLink() failed!\n");
        goto err_dos_symlink;
      }
    /* Set associations for the bus, device, FDO, PDO. */
    WvDevForDevObj(fdo, &WvDriverBusDev_);
    WvDriverBusDev_.Self = WvDriverBus_.Fdo = fdo;
    WvDriverBusDev_.IsBus = TRUE;
    WvDriverBusDev_.IrpMj = &irp_mj;
    WvDriverBus_.QueryDevText = WvDriverBusPnpQueryDevText_;
    WvDriverBus_.Pdo = Pdo;
    fdo->Flags |= DO_DIRECT_IO;         /* FIXME? */
    fdo->Flags |= DO_POWER_INRUSH;      /* FIXME? */
    /* Attach the FDO to the PDO. */
    WvDriverBus_.LowerDeviceObject = IoAttachDeviceToDeviceStack(
        fdo,
        Pdo
      );
    if (WvDriverBus_.LowerDeviceObject == NULL) {
        status = STATUS_NO_SUCH_DEVICE;
        DBG("IoAttachDeviceToDeviceStack() failed!\n");
        goto err_attach;
      }
    status = WvBusStartThread(&WvDriverBus_, &WvDriverBusThread_);
    if (!NT_SUCCESS(status)) {
        DBG("Couldn't start bus thread!\n");
        goto err_thread;
      }
    /* Ok! */
    KeAcquireSpinLock(&WvDriverBusFdoLock_, &irql);
    if (WvDriverBusFdo_) {
        KeReleaseSpinLock(&WvDriverBusFdoLock_, irql);
        DBG("Beaten to it!\n");
        status = STATUS_NOT_SUPPORTED;
        goto err_race_failed;
      }
    fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    #ifdef RIS
    WvDriverBus_.Dev.State = Started;
    #endif
    WvDriverBusFdo_ = fdo;
    KeReleaseSpinLock(&WvDriverBusFdoLock_, irql);
    DBG("Exit\n");
    return STATUS_SUCCESS;

    err_race_failed:

    err_thread:

    err_attach:

    IoDeleteSymbolicLink(&WvDriverBusDosname_);
    err_dos_symlink:

    IoDeleteDevice(fdo);
    err_fdo:

    err_already_established:

    DBG("Exit with failure\n");
    return status;
  }

/* Establish the bus PDO. */
static NTSTATUS STDCALL WvDriverBusEstablish_(IN PUNICODE_STRING RegistryPath) {
    NTSTATUS status;
    HANDLE reg_key;
    winvblock__uint32 pdo_done = 0;
    PDEVICE_OBJECT bus_pdo = NULL;

    /* Open our Registry path. */
    status = WvlRegOpenKey(RegistryPath->Buffer, &reg_key);
    if (!NT_SUCCESS(status))
      return status;

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
        &bus_pdo
      );
    if (bus_pdo == NULL) {
        DBG("IoReportDetectedDevice() went wrong!  Exiting.\n");
        status = STATUS_UNSUCCESSFUL;
        goto err_driver_bus;
      }

    /* Remember that we have a PDO for next time. */
    status = WvlRegStoreDword(reg_key, L"PdoDone", 1);
    if (!NT_SUCCESS(status)) {
        DBG("Couldn't save PdoDone to Registry.  Oh well.\n");
      }
    /* Attach FDO to PDO. */
    status = driver__attach_fdo_(WvDriverObj, bus_pdo);
    if (!NT_SUCCESS(status)) {
        DBG("driver__attach_fdo_() went wrong!\n");
        goto err_add_dev;
      }
    /* PDO created, FDO attached.  All done. */
    return STATUS_SUCCESS;

    err_add_dev:

    IoDeleteDevice(bus_pdo);
    err_driver_bus:

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
    if (WvDriverStarted_)
      return STATUS_SUCCESS;
    Debug_Initialize();
    status = WvlRegNoteOsLoadOpts(&WvDriverOsLoadOpts_);
    if (!NT_SUCCESS(status))
      return Error("WvlRegNoteOsLoadOpts", status);

    WvDriverStateHandle_ = NULL;
    KeInitializeSpinLock(&WvDriverBusFdoLock_);

    if ((WvDriverStateHandle_ = PoRegisterSystemState(
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
      DriverObject->MajorFunction[i] = driver__dispatch_not_supported_;
    DriverObject->MajorFunction[IRP_MJ_PNP] = driver__dispatch_pnp_;
    DriverObject->MajorFunction[IRP_MJ_POWER] = driver__dispatch_power_;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = driver__dispatch_create_close_;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = driver__dispatch_create_close_;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] =
      driver__dispatch_sys_ctl_;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] =
      driver__dispatch_dev_ctl_;
    DriverObject->MajorFunction[IRP_MJ_SCSI] = driver__dispatch_scsi_;
    /* Set the driver Unload callback. */
    DriverObject->DriverUnload = driver__unload_;
    /* Set the driver AddDevice callback. */
    DriverObject->DriverExtension->AddDevice = driver__attach_fdo_;
    /* Initialize various modules. */
    disk__module_init();        /* TODO: Check for error. */
    filedisk__module_init();    /* TODO: Check for error. */
    ramdisk__module_init();     /* TODO: Check for error. */

    /* Establish the bus PDO. */
    status = WvDriverBusEstablish_(RegistryPath);
    if(!NT_SUCCESS(status))
      goto err_bus;

    WvDriverStarted_ = TRUE;
    DBG("Exit\n");
    return STATUS_SUCCESS;

    err_bus:

    driver__unload_(DriverObject);
    DBG("Exit due to failure\n");
    return status;
  }

static NTSTATUS STDCALL driver__dispatch_not_supported_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return irp->IoStatus.Status;
  }

/**
 * Common IRP completion routine.
 *
 * @v irp               Points to the IRP to complete.
 * @v info              Number of bytes returned for the IRP, or 0.
 * @v status            Status for the IRP to complete.
 * @ret NTSTATUS        Returns the status value, as passed.
 */
winvblock__lib_func NTSTATUS STDCALL driver__complete_irp(
    IN PIRP irp,
    IN ULONG_PTR info,
    IN NTSTATUS status
  ) {
    irp->IoStatus.Information = info;
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    #ifdef DEBUGIRPS
    Debug_IrpEnd(irp, status);
    #endif
    return status;
  }

/* Handle a power IRP. */
static NTSTATUS driver__dispatch_power_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    /* WvDevFromDevObj() checks for a NULL dev_obj */
    WV_SP_DEV_T dev = WvDevFromDevObj(dev_obj);

    #ifdef DEBUGIRPS
    Debug_IrpStart(dev_obj, irp);
    #endif
    /* Check that the device exists. */
    if (!dev || dev->State == WvDevStateDeleted) {
        /* Even if it doesn't, a power IRP is important! */
        PoStartNextPowerIrp(irp);
        return driver__complete_irp(irp, 0, STATUS_NO_SUCH_DEVICE);
      }
    /* Call the particular device's power handler. */
    if (dev->IrpMj && dev->IrpMj->Power)
      return dev->IrpMj->Power(dev, irp);
    /* Otherwise, we don't support the IRP. */
    return driver__complete_irp(irp, 0, STATUS_NOT_SUPPORTED);
  }

/* Handle an IRP_MJ_CREATE or IRP_MJ_CLOSE IRP. */
static NTSTATUS driver__dispatch_create_close_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    /* WvDevFromDevObj() checks for a NULL dev_obj */
    WV_SP_DEV_T dev = WvDevFromDevObj(dev_obj);

    #ifdef DEBUGIRPS
    Debug_IrpStart(dev_obj, irp);
    #endif
    /* Check that the device exists. */
    if (!dev || dev->State == WvDevStateDeleted)
      return driver__complete_irp(irp, 0, STATUS_NO_SUCH_DEVICE);
    /* Always succeed with nothing to do. */
    return driver__complete_irp(irp, 0, STATUS_SUCCESS);
  }

/* Handle an IRP_MJ_SYSTEM_CONTROL IRP. */
static NTSTATUS driver__dispatch_sys_ctl_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    /* WvDevFromDevObj() checks for a NULL dev_obj */
    WV_SP_DEV_T dev = WvDevFromDevObj(dev_obj);

    #ifdef DEBUGIRPS
    Debug_IrpStart(dev_obj, irp);
    #endif
    /* Check that the device exists. */
    if (!dev || dev->State == WvDevStateDeleted)
      return driver__complete_irp(irp, 0, STATUS_NO_SUCH_DEVICE);
    /* Call the particular device's power handler. */
    if (dev->IrpMj && dev->IrpMj->SysCtl)
      return dev->IrpMj->SysCtl(dev, irp);
    /* Otherwise, we don't support the IRP. */
    return driver__complete_irp(irp, 0, STATUS_NOT_SUPPORTED);
  }

/* Handle an IRP_MJ_DEVICE_CONTROL IRP. */
static NTSTATUS driver__dispatch_dev_ctl_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    /* WvDevFromDevObj() checks for a NULL dev_obj */
    WV_SP_DEV_T dev = WvDevFromDevObj(dev_obj);
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);

    #ifdef DEBUGIRPS
    Debug_IrpStart(dev_obj, irp);
    #endif
    /* Check that the device exists. */
    if (!dev || dev->State == WvDevStateDeleted)
      return driver__complete_irp(irp, 0, STATUS_NO_SUCH_DEVICE);
    /* Call the particular device's power handler. */
    if (dev->IrpMj && dev->IrpMj->DevCtl) {
        return dev->IrpMj->DevCtl(
            dev,
            irp,
            io_stack_loc->Parameters.DeviceIoControl.IoControlCode
          );
      }
    /* Otherwise, we don't support the IRP. */
    return driver__complete_irp(irp, 0, STATUS_NOT_SUPPORTED);
  }

/* Handle an IRP_MJ_SCSI IRP. */
static NTSTATUS driver__dispatch_scsi_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    /* WvDevFromDevObj() checks for a NULL dev_obj */
    WV_SP_DEV_T dev = WvDevFromDevObj(dev_obj);
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);

    #ifdef DEBUGIRPS
    Debug_IrpStart(dev_obj, irp);
    #endif
    /* Check that the device exists. */
    if (!dev || dev->State == WvDevStateDeleted)
      return driver__complete_irp(irp, 0, STATUS_NO_SUCH_DEVICE);
    /* Call the particular device's power handler. */
    if (dev->IrpMj && dev->IrpMj->Scsi) {
        return dev->IrpMj->Scsi(
            dev,
            irp,
            io_stack_loc->Parameters.Scsi.Srb->Function
          );
      }
    /* Otherwise, we don't support the IRP. */
    return driver__complete_irp(irp, 0, STATUS_NOT_SUPPORTED);
  }

/* Handle an IRP_MJ_PNP IRP. */
static NTSTATUS driver__dispatch_pnp_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    /* WvDevFromDevObj() checks for a NULL dev_obj */
    WV_SP_DEV_T dev = WvDevFromDevObj(dev_obj);
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;

    #ifdef DEBUGIRPS
    Debug_IrpStart(dev_obj, irp);
    #endif
    /* Check that the device exists. */
    if (!dev || dev->State == WvDevStateDeleted)
      return driver__complete_irp(irp, 0, STATUS_NO_SUCH_DEVICE);
    /* Call the particular device's power handler. */
    if (dev->IrpMj && dev->IrpMj->Pnp) {
        status = dev->IrpMj->Pnp(
            dev,
            irp,
            io_stack_loc->MinorFunction
          );
        if (dev->IsBus) {
            dev->OldState = WvDriverBus_.OldState;
            dev->State = WvDriverBus_.State;
          }
        return status;
      }
    /* Otherwise, we don't support the IRP. */
    return driver__complete_irp(irp, 0, STATUS_NOT_SUPPORTED);
  }

static void STDCALL driver__unload_(IN PDRIVER_OBJECT DriverObject) {
    DBG("Unloading...\n");
    WvDriverBus_.Stop = TRUE;
    KeSetEvent(&WvDriverBus_.ThreadSignal, 0, FALSE);
    if (WvDriverBusThread_) {
        KeWaitForSingleObject(
            WvDriverBusThread_,
            Executive,
            KernelMode,
            FALSE,
            NULL
          );
        ObDereferenceObject(WvDriverBusThread_);
      }
    if (WvDriverStateHandle_ != NULL)
      PoUnregisterSystemState(WvDriverStateHandle_);
    IoDeleteSymbolicLink(&WvDriverBusDosname_);
    WvDriverBusFdo_ = NULL;
    wv_free(WvDriverOsLoadOpts_);
    WvDriverStarted_ = FALSE;
    DBG("Done\n");
  }

winvblock__lib_func void STDCALL WvDriverCompletePendingIrp(IN PIRP Irp) {
    #ifdef DEBUGIRPS
    Debug_IrpEnd(Irp, Irp->IoStatus.Status);
    #endif
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
  }

/* Note the exception to the function naming convention. */
winvblock__lib_func NTSTATUS STDCALL Error(
    IN PCHAR Message,
    IN NTSTATUS Status
  ) {
    DBG("%s: 0x%08x\n", Message, Status);
    return Status;
  }

/* Pass an IRP_MJ_SYSTEM_CONTROL IRP to the bus. */
static NTSTATUS STDCALL WvDriverBusSysCtl_(IN WV_SP_DEV_T dev, IN PIRP irp) {
    return WvBusSysCtl(&WvDriverBus_, irp);
  }

/* Pass a power IRP to the bus. */
static NTSTATUS STDCALL WvDriverBusPower_(IN WV_SP_DEV_T dev, IN PIRP irp) {
    return WvBusPower(&WvDriverBus_, irp);
  }

/* Pass an IRP_MJ_PNP to the bus. */
static NTSTATUS STDCALL WvDriverBusPnp_(
    IN WV_SP_DEV_T dev,
    IN PIRP irp,
    IN UCHAR code
  ) {
    return WvBusPnp(&WvDriverBus_, irp, code);
  }
    
/**
 * Add a child node to the bus.
 *
 * @v Dev               Points to the child device to add.
 * @ret                 TRUE for success, FALSE for failure.
 */
winvblock__lib_func winvblock__bool STDCALL WvDriverBusAddDev(
    IN OUT WV_SP_DEV_T Dev
  ) {
    /* The new node's device object. */
    PDEVICE_OBJECT dev_obj;

    DBG("Entry\n");
    if (!WvDriverBusFdo_ || !Dev) {
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
    Dev->Parent = WvDriverBus_.Fdo;
    /*
     * Initialize the device.  For disks, this routine is responsible for
     * determining the disk's geometry appropriately for AoE/RAM/file disks.
     */
    Dev->Ops.Init(Dev);
    dev_obj->Flags &= ~DO_DEVICE_INITIALIZING;
    /* Add the new PDO device to the bus' list of children. */
    WvBusAddNode(&WvDriverBus_, &Dev->BusNode);
    Dev->DevNum = WvBusGetNodeNum(&Dev->BusNode);

    DBG("Exit\n");
    return TRUE;
  }

static NTSTATUS STDCALL WvDriverBusDevCtlDetach_(
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    winvblock__uint32 unit_num;
    WV_SP_BUS_NODE walker;

    if (!(io_stack_loc->Control & SL_PENDING_RETURNED)) {
        NTSTATUS status;

        /* Enqueue the IRP. */
        status = WvBusEnqueueIrp(&WvDriverBus_, irp);
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
    while (walker = WvBusGetNextNode(&WvDriverBus_, walker)) {
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

NTSTATUS STDCALL WvDriverBusDevCtl_(
    IN WV_SP_DEV_T dev,
    IN PIRP irp,
    IN ULONG POINTER_ALIGNMENT code
  ) {
    NTSTATUS status;

    switch (code) {
        case IOCTL_FILE_ATTACH:
          status = filedisk__attach(dev, irp);
          break;

        case IOCTL_FILE_DETACH:
          return WvDriverBusDevCtlDetach_(irp);

        default:
          irp->IoStatus.Information = 0;
          status = STATUS_INVALID_DEVICE_REQUEST;
      }

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
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

static NTSTATUS STDCALL WvDriverBusPnpQueryDevText_(
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
          str_len = swprintf(*str, winvblock__literal_w L" Bus") + 1;
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
          str_len = WvDevPnpId(
              &WvDriverBusDev_,
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

/**
 * Dummy device feature.
 */

/* Prototype safety. */
WV_F_DEV_PNP WvDriverDummyPnp_;

/* Dummy PnP IRP handler. */
static NTSTATUS STDCALL WvDriverDummyPnp_(
    IN WV_SP_DEV_T dev,
    IN PIRP irp,
    IN UCHAR code
  ) {
    if (code != IRP_MN_QUERY_ID)
      return driver__complete_irp(irp, 0, STATUS_NOT_SUPPORTED);

    /* The WV_S_DEV_T extension points to the dummy IDs. */
    return WvDriverDummyIds(irp, dev->ext);
  }

typedef struct WV_DRIVER_ADD_DUMMY_ {
    const WV_S_DRIVER_DUMMY_IDS * DummyIds;
    DEVICE_TYPE DevType;
    ULONG DevCharacteristics;
    PKEVENT Event;
    NTSTATUS Status;
  } WV_S_DRIVER_ADD_DUMMY_, * WV_SP_DRIVER_ADD_DUMMY_;

/* Prototype safety. */
static WV_F_BUS_WORK_ITEM WvDriverAddDummy_;
/**
 * Add a dummy PDO child node in the context of the bus' thread.
 *
 * @v context           Points to the WV_S_DRIVER_ADD_DUMMY_ to process.
 */
static void STDCALL WvDriverAddDummy_(void * context) {
    WV_SP_DRIVER_ADD_DUMMY_ dummy_context = context;
    NTSTATUS status;
    PDEVICE_OBJECT pdo = NULL;
    WV_SP_DEV_T dev;
    wv_size_t dummy_ids_size;
    WV_SP_DRIVER_DUMMY_IDS dummy_ids;
    static WV_S_DEV_IRP_MJ irp_mj = {
        (WV_FP_DEV_DISPATCH) 0,
        (WV_FP_DEV_DISPATCH) 0,
        (WV_FP_DEV_CTL) 0,
        (WV_FP_DEV_SCSI) 0,
        WvDriverDummyPnp_,
      };

    status = IoCreateDevice(
        WvDriverObj,
        sizeof (driver__dev_ext),
        NULL,
        dummy_context->DevType,
        dummy_context->DevCharacteristics,
        FALSE,
        &pdo
      );
    if (!NT_SUCCESS(status) || !pdo) {
        DBG("Couldn't create dummy device.\n");
        dummy_context->Status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_create_pdo;
      }

    dev = wv_malloc(sizeof *dev);
    if (!dev) {
        DBG("Couldn't allocate dummy device.\n");
        dummy_context->Status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_dev;
      }

    dummy_ids_size =
      sizeof *dummy_ids +
      dummy_context->DummyIds->Len * sizeof dummy_ids->Text[0] -
      sizeof dummy_ids->Text[0];        /* The struct hack uses a WCHAR[1]. */
    dummy_ids = wv_malloc(dummy_ids_size);
    if (!dummy_ids) {
        DBG("Couldn't allocate dummy IDs.\n");
        dummy_context->Status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_dummy_ids;
      }
    /* Copy the IDs offsets and lengths. */
    RtlCopyMemory(dummy_ids, dummy_context->DummyIds, sizeof *dummy_ids);
    /* Copy the text of the IDs. */
    RtlCopyMemory(
        &dummy_ids->Text,
        dummy_context->DummyIds->Ids,
        dummy_context->DummyIds->Len * sizeof dummy_ids->Text[0]
      );
    /* Point to the copy of the text. */
    dummy_ids->Ids = dummy_ids->Text;

    /* Ok! */
    WvDevInit(dev);
    dev->IrpMj = &irp_mj;
    dev->ext = dummy_ids;       /* TODO: Implement a dummy free.  Leaking. */
    dev->Self = pdo;
    WvDevForDevObj(pdo, dev);
    WvBusInitNode(&dev->BusNode, pdo);
    /* Associate the parent bus. */
    dev->Parent = WvDriverBus_.Fdo;
    /* Add the new PDO device to the bus' list of children. */
    WvBusAddNode(&WvDriverBus_, &dev->BusNode);
    dev->DevNum = WvBusGetNodeNum(&dev->BusNode);
    pdo->Flags &= ~DO_DEVICE_INITIALIZING;

    dummy_context->Status = STATUS_SUCCESS;
    KeSetEvent(dummy_context->Event, 0, FALSE);
    return;

    wv_free(dummy_ids);
    err_dummy_ids:

    wv_free(dev);
    err_dev:

    IoDeleteDevice(pdo);
    err_create_pdo:

    KeSetEvent(dummy_context->Event, 0, FALSE);
    return;
  }

/**
 * Produce a dummy PDO node on the main bus.
 *
 * @v DummyIds                  The PnP IDs for the dummy.
 * @v DevType                   The type for the dummy device.
 * @v DevCharacteristics        The dummy device characteristics.
 * @ret NTSTATUS                The status of the operation.
 */
winvblock__lib_func NTSTATUS STDCALL WvDriverAddDummy(
    IN const WV_S_DRIVER_DUMMY_IDS * DummyIds,
    IN DEVICE_TYPE DevType,
    IN ULONG DevCharacteristics
  ) {
    KEVENT event;
    WV_S_DRIVER_ADD_DUMMY_ context = {
        DummyIds,
        DevType,
        DevCharacteristics,
        &event,
        STATUS_UNSUCCESSFUL
      };
    WV_S_BUS_CUSTOM_WORK_ITEM work_item = {
        WvDriverAddDummy_,
        &context
      };
    NTSTATUS status;

    if (!DummyIds)
      return STATUS_INVALID_PARAMETER;

    KeInitializeEvent(&event, SynchronizationEvent, FALSE);

    status = WvBusEnqueueCustomWorkItem(&WvDriverBus_, &work_item);
    if (!NT_SUCCESS(status))
      return status;

    /* Wait for WvDriverAddDummy_() to complete. */
    KeWaitForSingleObject(
        &event,
        Executive,
        KernelMode,
        FALSE,
        NULL
      );

    return context.Status;
  }

/**
 * Handle a PnP ID query with a WV_S_DRIVER_DUMMY_IDS object.
 *
 * @v Irp               The PnP ID query IRP to handle.
 * @v DummyIds          The object containing the IDs to respond with.
 * @ret NTSTATUS        The status of the operation.
 */
winvblock__lib_func NTSTATUS STDCALL WvDriverDummyIds(
    IN PIRP Irp,
    IN WV_SP_DRIVER_DUMMY_IDS DummyIds
  ) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(Irp);
    BUS_QUERY_ID_TYPE query_type = io_stack_loc->Parameters.QueryId.IdType;
    const WCHAR * ids;
    winvblock__uint32 len;
    NTSTATUS status;

    switch (query_type) {
        case BusQueryDeviceID:
          ids = DummyIds->Ids + DummyIds->DevOffset;
          len = DummyIds->DevLen;
          break;

        case BusQueryInstanceID:
          ids = DummyIds->Ids + DummyIds->InstanceOffset;
          len = DummyIds->InstanceLen;
          break;

        case BusQueryHardwareIDs:
          ids = DummyIds->Ids + DummyIds->HardwareOffset;
          len = DummyIds->HardwareLen;
          break;

        case BusQueryCompatibleIDs:
          ids = DummyIds->Ids + DummyIds->CompatOffset;
          len = DummyIds->CompatLen;
          break;

        default:
          return driver__complete_irp(Irp, 0, STATUS_NOT_SUPPORTED);
      }

    /* Allocate the return buffer. */
    Irp->IoStatus.Information = (ULONG_PTR) wv_palloc(len * sizeof *ids);
    if (Irp->IoStatus.Information == 0) {
        DBG("wv_palloc failed.\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto alloc_info;
      }
    /* Copy the working buffer to the return buffer. */
    RtlCopyMemory(
        (void *) Irp->IoStatus.Information,
        ids,
        len * sizeof *ids
      );
    status = STATUS_SUCCESS;

    /* irp->IoStatus.Information not freed. */
    alloc_info:

    return driver__complete_irp(Irp, Irp->IoStatus.Information, status);
  }
