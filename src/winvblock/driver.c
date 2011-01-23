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

/* From bus.c */
extern WVL_S_BUS_T WvBus;
extern NTSTATUS STDCALL WvBusAttach(IN PDEVICE_OBJECT);
extern NTSTATUS STDCALL WvBusEstablish(IN PUNICODE_STRING);
extern NTSTATUS STDCALL WvBusDevCtl(IN PIRP, IN ULONG POINTER_ALIGNMENT);
extern VOID WvBusCleanup(void);

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
    IN PDRIVER_OBJECT driver_obj,
    IN PDEVICE_OBJECT pdo
  ) {
    return WvBusAttach(pdo);
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
    WvlDebugModuleInit();
    status = WvlRegNoteOsLoadOpts(&WvOsLoadOpts);
    if (!NT_SUCCESS(status)) {
        WvlDebugModuleUnload();
        return WvlError("WvlRegNoteOsLoadOpts", status);
      }

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
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
  }

/* Handle a power IRP. */
static NTSTATUS WvIrpPower(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    WV_SP_DEV_T dev;

    WVL_M_DEBUG_IRP_START(dev_obj, irp);
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

    WVL_M_DEBUG_IRP_START(dev_obj, irp);
    /* Check for a bus IRP. */
    if (dev_obj == WvBus.Fdo)
      /* Always succeed with nothing to do. */
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

    WVL_M_DEBUG_IRP_START(dev_obj, irp);
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
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ULONG POINTER_ALIGNMENT code =
      io_stack_loc->Parameters.DeviceIoControl.IoControlCode;
    WV_SP_DEV_T dev;

    WVL_M_DEBUG_IRP_START(dev_obj, irp);
    /* Check for a bus IRP. */
    if (dev_obj == WvBus.Fdo)
      return WvBusDevCtl(irp, code);
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
            code
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

    WVL_M_DEBUG_IRP_START(dev_obj, irp);
    /* Check for a bus IRP. */
    if (dev_obj == WvBus.Fdo)
      /* Not supported. */
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
    NTSTATUS status;

    WVL_M_DEBUG_IRP_START(dev_obj, irp);
    /* Check for a bus IRP. */
    if (dev_obj == WvBus.Fdo)
      return WvlBusPnp(&WvBus, irp);
    /* WvDevFromDevObj() checks for a NULL dev_obj */
    dev = WvDevFromDevObj(dev_obj);
    /* Check that the device exists. */
    if (!dev || dev->State == WvDevStateDeleted)
      return WvlIrpComplete(irp, 0, STATUS_NO_SUCH_DEVICE);
    /* Call the particular device's power handler. */
    if (dev->IrpMj && dev->IrpMj->Pnp) {
        PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);

        status = dev->IrpMj->Pnp(
            dev,
            irp,
            io_stack_loc->MinorFunction
          );
        return status;
      }
    /* Otherwise, we don't support the IRP. */
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
  }

static VOID STDCALL WvUnload(IN PDRIVER_OBJECT DriverObject) {
    DBG("Unloading...\n");
    WvBusCleanup();
    if (WvDriverStateHandle != NULL)
      PoUnregisterSystemState(WvDriverStateHandle);
    wv_free(WvOsLoadOpts);
    WvDriverStarted = FALSE;
    WvlDebugModuleUnload();
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

/**
 * Miscellaneous: Grouped memory allocation functions.
 */

/* Internal type describing the layout of the tracking data. */
typedef struct WVL_MEM_GROUP_ITEM_ {
    PCHAR Next;
    PVOID Obj;
  } WVL_S_MEM_GROUP_ITEM_, * WVL_SP_MEM_GROUP_ITEM_;

/**
 * Initialize a memory group for use.
 *
 * @v Group             The memory group to initialize.
 */
WVL_M_LIB VOID STDCALL WvlMemGroupInit(OUT WVL_SP_MEM_GROUP Group) {
    if (!Group)
      return;
    Group->First = Group->Current = Group->Last = NULL;
    return;
  }

/**
 * Attempt to allocate a memory resource and add it to a group.
 *
 * @v Group             The group to add the resource to.
 * @v Size              The size, in bytes, of the object to allocate.
 * @ret PVOID           A pointer to the newly allocated object, or NULL.
 *
 * The advantage to having an allocation associated with a group is
 * that all items in a group can be freed simultaneously.  Each resource _can_
 * be individually freed once a batch free is no longer desirable.  Do _not_
 * perform a regular wv_free() on an item and then try to free a group.
 */
WVL_M_LIB PVOID STDCALL WvlMemGroupAlloc(
    IN OUT WVL_SP_MEM_GROUP Group,
    IN SIZE_T Size
  ) {
    WVL_S_MEM_GROUP_ITEM_ item;
    PCHAR item_ptr;

    if (!Group || !Size)
      return NULL;

    /* We allocate the object, with a footer item. */
    item.Obj = wv_mallocz(Size + sizeof item);
    if (!item.Obj)
      return NULL;

    /* Note the footer where the item will be copied to. */
    item_ptr = (PCHAR) item.Obj + Size;
    /* We are obviously the last item. */
    item.Next = NULL;
    /* Copy item into the new object. */
    RtlCopyMemory(item_ptr, &item, sizeof item);
    /* Point the previously last item's 'Next' member to the new item. */
    if (Group->Last)
      RtlCopyMemory(Group->Last, &item_ptr, sizeof item_ptr);
    /* Note the new item as the last in the group. */
    Group->Last = item_ptr;
    /* Note the new item as the first in the group, if applicable. */
    if (!Group->First)
      Group->First = item_ptr;

    /* All done. */
    return item.Obj;
  }

/**
 * Free all memory resources in a group.
 *
 * @v Group             The group of resources to free.
 *
 * Do _not_ perform a regular wv_free() on an item and then try to
 * free a group with this function.  The tracking data will have
 * already been freed, so resources iteration is expected to fail.
 */
WVL_M_LIB VOID STDCALL WvlMemGroupFree(IN OUT WVL_SP_MEM_GROUP Group) {
    WVL_S_MEM_GROUP_ITEM_ item;
    PCHAR next;

    if (!Group || !Group->First)
      return;

    /* Point to the first item. */
    next = Group->First;
    /* For each item, fetch it and free it. */
    while (next) {
        /* Fill with the next item. */
        RtlCopyMemory(&item, next, sizeof item);
        wv_free(item.Obj);
        next = item.Next;
      }
    Group->First = Group->Current = Group->Last = NULL;
    return;
  }

/**
 * Iterate through memory resources in a group.
 *
 * @v Group             The group of resources to iterate through.
 * @ret PVOID           The next available object in the group, or NULL.
 *
 * Before beginning iteration, set Group->Current = Group->First.
 */
WVL_M_LIB PVOID STDCALL WvlMemGroupNextObj(IN OUT WVL_SP_MEM_GROUP Group) {
    WVL_S_MEM_GROUP_ITEM_ item;

    if (!Group || !Group->First)
      return NULL;

    /* Copy the current item for inspection. */
    RtlCopyMemory(&item, Group->Current, sizeof item);
    /* Remember the next object for next time. */
    Group->Current = item.Next;
    return item.Obj;
  }

/**
 * Attempt to allocate a batch of identically-sized memory
 * resources, and associate them with a group.
 *
 * @v Group             The group to associate the allocations with.
 * @v Size              The size, in bytes, of each object to allocate.
 * @v Count             The count of objects to allocate.
 * @ret PVOID           Points to the first allocated object in the batch.
 *
 * If a single allocation in the batch fails, any of the allocations in
 * that batch will be freed.  Do not confuse that to mean that all resources
 * in the group are freed.
 *
 * You might use this function to allocate a batch of objects with size X
 * and have them associated with a group.  Then you might call it again for
 * a batch of objects with size Y which will have the same group.  Then you
 * might add some individual objects to the group with WvlMemGroupAllocate().
 * Perhaps the last allocation fails; you can still free all allocations
 * with WvlMemGroupFree().
 */
WVL_M_LIB PVOID STDCALL WvlMemGroupBatchAlloc(
    IN OUT WVL_SP_MEM_GROUP Group,
    IN SIZE_T Size,
    IN UINT32 Count
  ) {
    WVL_S_MEM_GROUP_ITEM_ item;
    PVOID first_obj;
    WVL_S_MEM_GROUP tmp_group = {0};
    PCHAR item_ptr, last = (PCHAR) &tmp_group.First;

    if (!Group || !Size || !Count)
      return NULL;

    /* Allocate the first object and remember it. */
    first_obj = item.Obj = wv_mallocz(Size + sizeof item);
    if (!first_obj)
      return NULL;
    goto jump_in;
    do {
        /* We allocate each object, with a footer item. */
        item.Obj = wv_mallocz(Size + sizeof item);
        if (!item.Obj) {
            break;
          }
        jump_in:
        /* Note the footer where the item will be copied to. */
        item_ptr = (PCHAR) item.Obj + Size;
        /* We are obviously the newest item in the batch. */
        item.Next = NULL;
        /* Copy item into the new object. */
        RtlCopyMemory(item_ptr, &item, sizeof item);
        /* Point the previous item's 'Next' member to the new item. */
        RtlCopyMemory(last, &item_ptr, sizeof item_ptr);
        /* Note the new item as the last in the batch. */
        last = item_ptr;
      } while (--Count);

    /* Abort? */
    if (Count) {
        WvlMemGroupFree(&tmp_group);
        return NULL;
      }

    /*
     * Success. Point the real group's last item's
     * 'Next' member to the batch's first item.
     */
    if (Group->Last)
      RtlCopyMemory(Group->Last, &tmp_group.First, sizeof tmp_group.First);
    /* Set the real group's 'First' member, if applicable. */
    if (!Group->First)
      Group->First = tmp_group.First;
    /* All done. */
    return first_obj;
  }
