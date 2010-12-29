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
#include "device.h"
#include "disk.h"
#include "registry.h"
#include "mount.h"
#include "bus.h"
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
    sizeof WV_M_BUS_NAME_,
    sizeof WV_M_BUS_NAME_,
    WV_M_BUS_NAME_
  };
static UNICODE_STRING WvDriverBusDosname_ = {
    sizeof WV_M_BUS_DOSNAME_,
    sizeof WV_M_BUS_DOSNAME_,
    WV_M_BUS_DOSNAME_
  };
/* The main bus. */
static WV_S_BUS_T WvDriverBus_ = {0};
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
static WV_F_BUS_PNP WvDriverBusPnpQueryDevText_;

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
    IN PDEVICE_OBJECT PhysicalDeviceObject
  ) {
    KIRQL irql;
    NTSTATUS status;
    PLIST_ENTRY walker;
    PUNICODE_STRING dev_name = NULL;
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
    WvDevForDevObj(fdo, &WvDriverBus_.Dev);
    WvDriverBus_.Dev.Self = WvDriverBus_.Fdo = fdo;
    WvDriverBus_.Dev.IsBus = TRUE;
    WvDriverBus_.Dev.IrpMj = &irp_mj;
    WvDriverBus_.QueryDevText = WvDriverBusPnpQueryDevText_;
    WvDriverBus_.PhysicalDeviceObject = PhysicalDeviceObject;
    fdo->Flags |= DO_DIRECT_IO;         /* FIXME? */
    fdo->Flags |= DO_POWER_INRUSH;      /* FIXME? */
    /* Attach the FDO to the PDO. */
    WvDriverBus_.LowerDeviceObject = IoAttachDeviceToDeviceStack(
        fdo,
        PhysicalDeviceObject
      );
    if (WvDriverBus_.LowerDeviceObject == NULL) {
        status = STATUS_NO_SUCH_DEVICE;
        DBG("IoAttachDeviceToDeviceStack() failed!\n");
        goto err_attach;
      }
    status = WvBusStartThread(&WvDriverBus_);
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

/* Create the root-enumerated, main bus device. */
NTSTATUS STDCALL driver__create_bus_(void) {
    WV_SP_BUS_T bus;
    NTSTATUS status;
    PDEVICE_OBJECT bus_pdo = NULL;

    /* Create the PDO. */
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
    status = registry__note_os_load_opts(&WvDriverOsLoadOpts_);
    if (!NT_SUCCESS(status))
      return Error("registry__note_driver__os_load_opts", status);

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

    /*
     * Always create the root-enumerated, main bus device.
     * This is required in order to boot from a WinVBlock disk.
     */
    status = driver__create_bus_();
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
    /* Create a node.  TODO: Put the node somewhere better. */
    Dev->BusNode = wv_malloc(sizeof *(Dev->BusNode));
    if (!Dev->BusNode) {
        DBG("Couldn't allocate node storage!\n");
        IoDeleteDevice(dev_obj);
        return FALSE;
      }
    WvBusInitNode(Dev->BusNode, dev_obj);
    /* Associate the parent bus. */
    Dev->Parent = WvDriverBus_.Dev.Self;
    /*
     * Initialize the device.  For disks, this routine is responsible for
     * determining the disk's geometry appropriately for AoE/RAM/file disks.
     */
    Dev->Ops.Init(Dev);
    dev_obj->Flags &= ~DO_DEVICE_INITIALIZING;
    /* Add the new PDO device to the bus' list of children. */
    WvBusAddNode(&WvDriverBus_, Dev->BusNode);

    DBG("Exit\n");
    return TRUE;
  }

static NTSTATUS STDCALL WvDriverBusDevCtlDetach_(
    IN PIRP irp
  ) {
    winvblock__uint32 disk_num =
      *((winvblock__uint32_ptr) irp->AssociatedIrp.SystemBuffer);
    WV_SP_DEV_T dev_walker;
    WV_SP_DISK_T disk_walker = NULL, prev_disk_walker;
    WV_SP_BUS_T bus = &WvDriverBus_;

    DBG("Request to detach disk: %d\n", disk_num);
    dev_walker = bus->first_child;
    if (dev_walker != NULL)
      disk_walker = disk__get_ptr(dev_walker);
    prev_disk_walker = disk_walker;
    while ((disk_walker != NULL) && (dev_walker->DevNum != disk_num)) {
        prev_disk_walker = disk_walker;
        dev_walker = dev_walker->next_sibling_ptr;
        if (dev_walker != NULL)
          disk_walker = disk__get_ptr(dev_walker);
      }
    if (disk_walker != NULL) {
        if (disk_walker->Dev->Boot) {
            DBG("Cannot unmount a boot drive.\n");
            irp->IoStatus.Information = 0;
            return STATUS_INVALID_DEVICE_REQUEST;
          }
        DBG("Deleting disk %d\n", dev_walker->DevNum);
        if (disk_walker == disk__get_ptr(bus->first_child))
          bus->first_child = dev_walker->next_sibling_ptr;
          else {
            prev_disk_walker->Dev->next_sibling_ptr =
              dev_walker->next_sibling_ptr;
          }
        disk_walker->Unmount = TRUE;
        dev_walker->next_sibling_ptr = NULL;
        if (bus->PhysicalDeviceObject != NULL)
          IoInvalidateDeviceRelations(bus->PhysicalDeviceObject, BusRelations);
      }
    bus->Children--;
    irp->IoStatus.Information = 0;
    return STATUS_SUCCESS;
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
          status = WvDriverBusDevCtlDetach_(irp);
          break;

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
              &WvDriverBus_.Dev,
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
