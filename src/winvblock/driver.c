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
/* Contains TXTSETUP.SIF/BOOT.INI-style OsLoadOptions parameters. */
static LPWSTR driver__os_load_opts_ = NULL;

/* Forward declarations. */
static driver__dispatch_func driver__dispatch_not_supported_;
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

/* Create the root-enumerated, main bus device. */
NTSTATUS STDCALL driver__create_bus_(void) {
    struct bus__type * bus;
    NTSTATUS status;
    PDEVICE_OBJECT bus_pdo = NULL;

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
    /* We have a PDO.  Note it.  We need this in order to attach the FDO. */
    bus->PhysicalDeviceObject = bus_pdo;
    /*
     * Attach FDO to PDO. *sigh*  Note that we do not own the PDO,
     * so we must associate the bus structure with the FDO, instead.
     * Consider that the AddDevice()/attach_fdo() routine takes two parameters,
     * neither of which are guaranteed to be owned by a caller in this driver.
     * Since attach_fdo() associates a bus device, it is forced to walk our
     * global list of bus devices.  Otherwise, it would be easy to pass it here
     */
    status = driver__obj_ptr->DriverExtension->AddDevice(
        driver__obj_ptr,
        bus_pdo
      );
    if (!NT_SUCCESS(status)) {
        DBG("attach_fdo() went wrong!\n");
        goto err_add_dev;
      }
    /* Bus created, PDO created, FDO attached.  All done. */
    driver__bus_fdo_ = bus->device->Self;
    return STATUS_SUCCESS;

    err_add_dev:

    IoDeleteDevice(bus_pdo);
    err_driver_bus:

    device__free(bus->device);
    err_bus:

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
    DriverObject->MajorFunction[IRP_MJ_POWER] = driver__dispatch_;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = driver__dispatch_;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = driver__dispatch_;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = driver__dispatch_;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = driver__dispatch_;
    DriverObject->MajorFunction[IRP_MJ_SCSI] = driver__dispatch_;
    /* Set the driver Unload callback. */
    DriverObject->DriverUnload = driver__unload_;
    /* Initialize various modules. */
    device__init();             /* TODO: Check for error. */
    bus__module_init();         /* TODO: Check for error. */
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

/* Handle IRP_MJ_CREATE and IRP_MJ_CLOSE */
extern winvblock__lib_func NTSTATUS STDCALL driver__create_close(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    IN PIO_STACK_LOCATION stack,
    IN struct _device__type * dev_ptr,
    OUT winvblock__bool_ptr completion_ptr
  ) {
    NTSTATUS status = STATUS_SUCCESS;

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    *completion_ptr = TRUE;
    return status;
  }

/* IRP is not understood. */
extern winvblock__lib_func NTSTATUS STDCALL driver__not_supported(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    IN PIO_STACK_LOCATION stack,
    IN struct _device__type * dev_ptr,
    OUT winvblock__bool_ptr completion_ptr
  ) {
    NTSTATUS status = STATUS_NOT_SUPPORTED;

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    *completion_ptr = TRUE;
    return status;
  }

static NTSTATUS STDCALL driver__dispatch_(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
  ) {
    NTSTATUS status;
    device__type_ptr dev_ptr;

    #ifdef DEBUGIRPS
    Debug_IrpStart(DeviceObject, Irp);
    #endif
    dev_ptr = device__get(DeviceObject);

    /* We handle IRP_MJ_POWER as an exception. */
    if (dev_ptr->State == Deleted) {
        if (IoGetCurrentIrpStackLocation(Irp)->MajorFunction == IRP_MJ_POWER)
          PoStartNextPowerIrp(Irp);
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
      } else
      status = dev_ptr->dispatch(DeviceObject, Irp);

    return status;
  }

/* Place-holder while implementing a dispatch routine per device class. */
winvblock__lib_func NTSTATUS STDCALL driver__default_dispatch(
    IN PDEVICE_OBJECT dev,
    IN PIRP irp
  ) {
    NTSTATUS status;
    winvblock__bool completion = FALSE;
    static const irp__handling handling_table[] = {
        /*
         * Major, minor, any major?, any minor?, handler
         * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
         * Note that the fall-through case must come FIRST!
         * Why? It sets completion to true, so others won't be called.
         */
        {             0, 0,  TRUE, TRUE, driver__not_supported },
        {  IRP_MJ_CLOSE, 0, FALSE, TRUE,  driver__create_close },
        { IRP_MJ_CREATE, 0, FALSE, TRUE,  driver__create_close },
      };

    status = irp__process(
        dev,
        irp,
        IoGetCurrentIrpStackLocation(irp),
        device__get(dev),
        &completion
      );
    /* Fall through to some driver defaults, if needed. */
    if (status == STATUS_NOT_SUPPORTED && !completion) {
        status = irp__process_with_table(
            dev,
            irp,
            handling_table,
            sizeof handling_table,
            &completion
          );
      }
    #ifdef DEBUGIRPS
    if (status != STATUS_PENDING)
      Debug_IrpEnd(irp, status);
    #endif

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
    bus__module_shutdown();
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
