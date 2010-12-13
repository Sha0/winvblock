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
#include "irp.h"
#include "driver.h"
#include "device.h"
#include "disk.h"
#include "registry.h"
#include "mount.h"
#include "bus.h"
#include "filedisk.h"
#include "ramdisk.h"
#include "debug.h"

/* Exported. */
PDRIVER_OBJECT driver__obj_ptr = NULL;

/* Globals. */
static void * driver__state_handle_;
static winvblock__bool driver__started_ = FALSE;
static PDEVICE_OBJECT driver__bus_fdo_ = NULL;
static KSPIN_LOCK driver__bus_fdo_lock_;
/* Contains TXTSETUP.SIF/BOOT.INI-style OsLoadOptions parameters. */
static LPWSTR driver__os_load_opts_ = NULL;

/* Forward declarations. */
static driver__dispatch_func driver__dispatch_not_supported_;
static driver__dispatch_func driver__dispatch_power_;
static driver__dispatch_func driver__dispatch_create_close_;
static driver__dispatch_func driver__dispatch_sys_ctl_;
static driver__dispatch_func driver__dispatch_dev_ctl_;
static driver__dispatch_func driver__dispatch_scsi_;
static driver__dispatch_func driver__dispatch_;
static void STDCALL driver__unload_(IN PDRIVER_OBJECT);

static LPWSTR STDCALL get_opt(IN LPWSTR opt_name) {
    LPWSTR our_opts, the_opt;
    WCHAR our_sig[] = L"WINVBLOCK=";
    /* To produce constant integer expressions. */
    enum {
        our_sig_len_bytes = sizeof ( our_sig ) - sizeof ( WCHAR ),
        our_sig_len = our_sig_len_bytes / sizeof ( WCHAR )
      };
    size_t opt_name_len, opt_name_len_bytes;

    if (!driver__os_load_opts_ || !opt_name)
      return NULL;

    /* Find /WINVBLOCK= options. */
    our_opts = driver__os_load_opts_;
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
    IN PDEVICE_OBJECT PhysicalDeviceObject
  ) {
    KIRQL irql;
    NTSTATUS status;
    PLIST_ENTRY walker;
    struct bus__type * bus;
    PUNICODE_STRING dev_name = NULL;
    PDEVICE_OBJECT fdo = NULL;
    struct device__type * dev_ptr;

    DBG("Entry\n");
    /* Do we alreay have our main bus? */
    if (driver__bus_fdo_) {
        DBG("Already have the main bus.  Refusing.\n");
        status = STATUS_NOT_SUPPORTED;
        goto err_already_established;
      }
    /* Create the bus. */
    bus = bus__create();
    if (!bus) {
        DBG("bus__create() failed for the main bus.\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_bus;
      }
    /* In booting, he has a name.  His name is WinVBlock. */
    RtlInitUnicodeString(
        &bus->dev_name,
        L"\\Device\\" winvblock__literal_w
      );
    RtlInitUnicodeString(
        &bus->dos_dev_name,
        L"\\DosDevices\\" winvblock__literal_w
      );
    bus->named = TRUE;
    /* Create the bus FDO. */
    status = IoCreateDevice(
        DriverObject,
        sizeof (driver__dev_ext),
        &bus->dev_name,
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
        &bus->dos_dev_name,
        &bus->dev_name
      );
    if (!NT_SUCCESS(status)) {
        DBG("IoCreateSymbolicLink() failed!\n");
        goto err_dos_symlink;
      }
    /* Set associations for the bus, device, FDO, PDO. */
    device__set(fdo, bus->device);
    bus->device->Self = fdo;
    bus->PhysicalDeviceObject = PhysicalDeviceObject;
    fdo->Flags |= DO_DIRECT_IO;         /* FIXME? */
    fdo->Flags |= DO_POWER_INRUSH;      /* FIXME? */
    /* Attach the FDO to the PDO. */
    bus->LowerDeviceObject = IoAttachDeviceToDeviceStack(
        fdo,
        PhysicalDeviceObject
      );
    if (bus->LowerDeviceObject == NULL) {
        status = STATUS_NO_SUCH_DEVICE;
        DBG("IoAttachDeviceToDeviceStack() failed!\n");
        goto err_attach;
      }
    /* Ok! */
    KeAcquireSpinLock(&driver__bus_fdo_lock_, &irql);
    if (driver__bus_fdo_) {
        KeReleaseSpinLock(&driver__bus_fdo_lock_, irql);
        DBG("Beaten to it!\n");
        status = STATUS_NOT_SUPPORTED;
        goto err_race_failed;
      }
    fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    #ifdef RIS
    bus->device->State = Started;
    #endif
    driver__bus_fdo_ = fdo;
    KeReleaseSpinLock(&driver__bus_fdo_lock_, irql);
    DBG("Exit\n");
    return STATUS_SUCCESS;

    err_race_failed:

    err_attach:

    IoDeleteSymbolicLink(&bus->dos_dev_name);
    err_dos_symlink:

    IoDeleteDevice(fdo);
    err_fdo:

    device__free(bus->device);
    err_bus:

    err_already_established:

    DBG("Exit with failure\n");
    return status;
  }

/* Create the root-enumerated, main bus device. */
NTSTATUS STDCALL driver__create_bus_(void) {
    struct bus__type * bus;
    NTSTATUS status;
    PDEVICE_OBJECT bus_pdo = NULL;

    /* Create the PDO. */
    IoReportDetectedDevice(
        driver__obj_ptr,
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
    /* Attach FDO to PDO. */
    status = driver__attach_fdo_(driver__obj_ptr, bus_pdo);
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
    if (driver__obj_ptr) {
        DBG("Re-entry not allowed!\n");
        return STATUS_NOT_SUPPORTED;
      }
    driver__obj_ptr = DriverObject;
    if (driver__started_)
      return STATUS_SUCCESS;
    Debug_Initialize();
    status = registry__note_os_load_opts(&driver__os_load_opts_);
    if (!NT_SUCCESS(status))
      return Error("registry__note_driver__os_load_opts", status);

    driver__state_handle_ = NULL;
    KeInitializeSpinLock(&driver__bus_fdo_lock_);

    if ((driver__state_handle_ = PoRegisterSystemState(
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
    DriverObject->MajorFunction[IRP_MJ_PNP] = driver__dispatch_;
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
    disk__init();               /* TODO: Check for error. */
    filedisk__init();           /* TODO: Check for error. */
    ramdisk__init();            /* TODO: Check for error. */

    /*
     * Always create the root-enumerated, main bus device.
     * This is required in order to boot from a WinVBlock disk.
     */
    status = driver__create_bus_();
    if(!NT_SUCCESS(status))
      goto err_bus;

    driver__started_ = TRUE;
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

/* IRP is not understood. */
extern winvblock__lib_func NTSTATUS STDCALL driver__not_supported(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    IN PIO_STACK_LOCATION stack,
    IN struct device__type * dev_ptr,
    OUT winvblock__bool_ptr completion_ptr
  ) {
    NTSTATUS status = STATUS_NOT_SUPPORTED;

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    *completion_ptr = TRUE;
    return status;
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
    /* device__get() checks for a NULL dev_obj */
    struct device__type * dev = device__get(dev_obj);

    #ifdef DEBUGIRPS
    Debug_IrpStart(dev_obj, irp);
    #endif
    /* Check that the device exists. */
    if (!dev || dev->state == device__state_deleted) {
        /* Even if it doesn't, a power IRP is important! */
        PoStartNextPowerIrp(irp);
        return driver__complete_irp(irp, 0, STATUS_NO_SUCH_DEVICE);
      }
    /* Call the particular device's power handler. */
    if (dev->irp_mj && dev->irp_mj->power)
      return dev->irp_mj->power(dev, irp);
    /* Otherwise, we don't support the IRP. */
    return driver__complete_irp(irp, 0, STATUS_NOT_SUPPORTED);
  }

/* Handle an IRP_MJ_CREATE or IRP_MJ_CLOSE IRP. */
static NTSTATUS driver__dispatch_create_close_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    /* device__get() checks for a NULL dev_obj */
    struct device__type * dev = device__get(dev_obj);

    #ifdef DEBUGIRPS
    Debug_IrpStart(dev_obj, irp);
    #endif
    /* Check that the device exists. */
    if (!dev || dev->state == device__state_deleted)
      return driver__complete_irp(irp, 0, STATUS_NO_SUCH_DEVICE);
    /* Always succeed with nothing to do. */
    return driver__complete_irp(irp, 0, STATUS_SUCCESS);
  }

/* Handle an IRP_MJ_SYSTEM_CONTROL IRP. */
static NTSTATUS driver__dispatch_sys_ctl_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    /* device__get() checks for a NULL dev_obj */
    struct device__type * dev = device__get(dev_obj);

    #ifdef DEBUGIRPS
    Debug_IrpStart(dev_obj, irp);
    #endif
    /* Check that the device exists. */
    if (!dev || dev->state == device__state_deleted)
      return driver__complete_irp(irp, 0, STATUS_NO_SUCH_DEVICE);
    /* Call the particular device's power handler. */
    if (dev->irp_mj && dev->irp_mj->sys_ctl)
      return dev->irp_mj->sys_ctl(dev, irp);
    /* Otherwise, we don't support the IRP. */
    return driver__complete_irp(irp, 0, STATUS_NOT_SUPPORTED);
  }

/* Handle an IRP_MJ_DEVICE_CONTROL IRP. */
static NTSTATUS driver__dispatch_dev_ctl_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    /* device__get() checks for a NULL dev_obj */
    struct device__type * dev = device__get(dev_obj);
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);

    #ifdef DEBUGIRPS
    Debug_IrpStart(dev_obj, irp);
    #endif
    /* Check that the device exists. */
    if (!dev || dev->state == device__state_deleted)
      return driver__complete_irp(irp, 0, STATUS_NO_SUCH_DEVICE);
    /* Call the particular device's power handler. */
    if (dev->irp_mj && dev->irp_mj->dev_ctl) {
        return dev->irp_mj->dev_ctl(
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
    /* device__get() checks for a NULL dev_obj */
    struct device__type * dev = device__get(dev_obj);
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);

    #ifdef DEBUGIRPS
    Debug_IrpStart(dev_obj, irp);
    #endif
    /* Check that the device exists. */
    if (!dev || dev->state == device__state_deleted)
      return driver__complete_irp(irp, 0, STATUS_NO_SUCH_DEVICE);
    /* Call the particular device's power handler. */
    if (dev->irp_mj && dev->irp_mj->scsi) {
        return dev->irp_mj->scsi(
            dev,
            irp,
            io_stack_loc->Parameters.Scsi.Srb->Function
          );
      }
    /* Otherwise, we don't support the IRP. */
    return driver__complete_irp(irp, 0, STATUS_NOT_SUPPORTED);
  }

static NTSTATUS STDCALL driver__dispatch_(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
  ) {
    NTSTATUS status;
    struct device__type * dev_ptr;

    #ifdef DEBUGIRPS
    Debug_IrpStart(DeviceObject, Irp);
    #endif
    dev_ptr = device__get(DeviceObject);

    /* Check for a deleted device. */
    if (dev_ptr->state == device__state_deleted) {
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = STATUS_NO_SUCH_DEVICE;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        #ifdef DEBUGIRPS
        Debug_IrpEnd ( Irp, STATUS_NO_SUCH_DEVICE );
        #endif
        return STATUS_NO_SUCH_DEVICE;
      }

    /* Enqueue the IRP for threaded devices, or process immediately. */
    if (dev_ptr->thread) {
        IoMarkIrpPending(Irp);
        ExInterlockedInsertTailList(
            &dev_ptr->irp_list,
            /* Where IRPs can be linked. */
            &Irp->Tail.Overlay.ListEntry,
            &dev_ptr->irp_list_lock
          );
        KeSetEvent(&dev_ptr->thread_wakeup, 0, FALSE);
        status = STATUS_PENDING;
      } else {
        if (dev_ptr->dispatch)
          status = dev_ptr->dispatch(DeviceObject, Irp);
          else
          return driver__complete_irp(Irp, 0, STATUS_NOT_SUPPORTED);
      }

    return status;
  }

static void STDCALL driver__unload_(IN PDRIVER_OBJECT DriverObject) {
    UNICODE_STRING DosDeviceName;

    DBG("Unloading...\n");
    if (driver__state_handle_ != NULL)
      PoUnregisterSystemState(driver__state_handle_);
    RtlInitUnicodeString(
        &DosDeviceName,
        L"\\DosDevices\\" winvblock__literal_w
      );
    IoDeleteSymbolicLink(&DosDeviceName);
    driver__bus_fdo_ = NULL;
    wv_free(driver__os_load_opts_);
    driver__started_ = FALSE;
    DBG("Done\n");
  }

winvblock__lib_func void STDCALL Driver_CompletePendingIrp(IN PIRP Irp) {
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

/**
 * Get a pointer to the driver bus device.
 *
 * @ret         A pointer to the driver bus, or NULL.
 */
winvblock__lib_func struct bus__type * driver__bus(void) {
    if (!driver__bus_fdo_) {
        DBG("No driver bus device!\n");
        return NULL;
      }
    return bus__get(device__get(driver__bus_fdo_));
  }
