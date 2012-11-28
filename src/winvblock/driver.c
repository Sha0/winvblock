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
 * Driver specifics.
 */

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
#include "filedisk.h"
#include "ramdisk.h"
#include "debug.h"

/* From mainbus/mainbus.c */
extern WVL_S_BUS_T WvBus;
extern NTSTATUS STDCALL WvBusAttach(IN PDEVICE_OBJECT);
extern NTSTATUS STDCALL WvBusEstablish(IN PUNICODE_STRING);
extern NTSTATUS STDCALL WvBusDevCtl(IN PIRP, IN ULONG POINTER_ALIGNMENT);
extern VOID WvBusCleanup(void);

/** Public objects */
DRIVER_OBJECT * WvDriverObj;
UINT32 WvFindDisk;
KSPIN_LOCK WvFindDiskLock;
S_WVL_RESOURCE_TRACKER WvDriverUsage[1];

/** Private objects */

/** Power state handle */
static VOID * WvDriverStateHandle;

/** Is the driver started? */
static BOOLEAN WvDriverStarted;

/** Contains TXTSETUP.SIF/BOOT.INI-style OsLoadOptions parameters */
static WCHAR * WvOsLoadOpts;

/** The list of registered mini-drivers */
static S_WVL_LOCKED_LIST WvRegisteredMiniDrivers[1];

/** Notice for WvDeregisterMiniDrivers that the registration list is empty */
static KEVENT WvMiniDriversDeregistered;

/* Private function declarations */
static DRIVER_DISPATCH WvIrpNotSupported;
static
  __drv_dispatchType(IRP_MJ_POWER)
  __drv_dispatchType(IRP_MJ_CREATE)
  __drv_dispatchType(IRP_MJ_CLOSE)
  __drv_dispatchType(IRP_MJ_SYSTEM_CONTROL)
  __drv_dispatchType(IRP_MJ_DEVICE_CONTROL)
  __drv_dispatchType(IRP_MJ_SCSI)
  __drv_dispatchType(IRP_MJ_PNP)
  DRIVER_DISPATCH WvDriverDispatchIrp;
static DRIVER_UNLOAD WvUnload;
static DRIVER_UNLOAD WvUnloadMiniDriver;
static VOID WvDeregisterMiniDrivers(void);

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

static VOID STDCALL WvDriverReinitialize(
    IN PDRIVER_OBJECT driver_obj,
    IN PVOID context,
    ULONG count
  ) {
    static U_WV_LARGE_INT delay_time = {-10000000LL};
    KIRQL irql;
    UINT32 find_disk;

    DBG("Called\n");

    /* Check if any threads still need to find their disk. */
    KeAcquireSpinLock(&WvFindDiskLock, &irql);
    find_disk = WvFindDisk;
    KeReleaseSpinLock(&WvFindDiskLock, irql);

    /* Should we retry? */
    if (!find_disk || count >= 10) {
        WvlDecrementResourceUsage(WvDriverUsage);
        DBG("Exiting...\n");
        return;
      }

    /* Yes, we should. */
    IoRegisterBootDriverReinitialization(
        WvDriverObj,
        WvDriverReinitialize,
        NULL
      );
    /* Sleep. */
    DBG("Sleeping...");
    KeDelayExecutionThread(KernelMode, FALSE, &delay_time.large_int);
    return;
  }

/**
 * @name DriverEntry
 *
 * The driver entry-point
 *
 * @param drv_obj
 *   The driver object provided by Windows
 *
 * @param reg_path
 *   The Registry path provided by Windows
 *
 * @return
 *   The status
 */
NTSTATUS STDCALL DriverEntry(
    IN DRIVER_OBJECT * drv_obj,
    IN UNICODE_STRING * reg_path
  ) {
    NTSTATUS status;
    ULONG i;

    DBG("Entry\n");

    /* Have we already initialized the driver? */
    if (WvDriverObj) {
        DBG("Re-entry not allowed!\n");
        return STATUS_NOT_SUPPORTED;
      }
    WvDriverObj = drv_obj;

    /* Have we already started the driver? */
    if (WvDriverStarted)
      return STATUS_SUCCESS;

    /* Track resource usage */
    WvlInitializeResourceTracker(WvDriverUsage);

    WvlDebugModuleInit();

    /* Check OS loader options */
    status = WvlRegNoteOsLoadOpts(&WvOsLoadOpts);
    if (!NT_SUCCESS(status)) {
        WvlDebugModuleUnload();
        return WvlError("WvlRegNoteOsLoadOpts", status);
      }

    /* AoE doesn't support sleeping */
    WvDriverStateHandle = PoRegisterSystemState(NULL, ES_CONTINUOUS);
    if (!WvDriverStateHandle)
      DBG("Could not set system state to ES_CONTINUOUS!!\n");

    KeInitializeSpinLock(&WvFindDiskLock);

    /*
     * Set up IRP MajorFunction function table for devices
     * this driver handles
     */
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
      drv_obj->MajorFunction[i] = WvIrpNotSupported;
    drv_obj->MajorFunction[IRP_MJ_PNP] = WvDriverDispatchIrp;
    drv_obj->MajorFunction[IRP_MJ_POWER] = WvDriverDispatchIrp;
    drv_obj->MajorFunction[IRP_MJ_CREATE] = WvDriverDispatchIrp;
    drv_obj->MajorFunction[IRP_MJ_CLOSE] = WvDriverDispatchIrp;
    drv_obj->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = WvDriverDispatchIrp;
    drv_obj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = WvDriverDispatchIrp;
    drv_obj->MajorFunction[IRP_MJ_SCSI] = WvDriverDispatchIrp;

    /* Set the driver Unload callback */
    drv_obj->DriverUnload = WvUnload;

    /* Set the driver AddDevice callback */
    drv_obj->DriverExtension->AddDevice = WvAttachFdo;

    /* Initialize the list of registered mini-drivers */
    WvlInitializeLockedList(WvRegisteredMiniDrivers);
    KeInitializeEvent(&WvMiniDriversDeregistered, NotificationEvent, FALSE);
    WvlIncrementResourceUsage(WvDriverUsage);

    /* Establish the bus PDO */
    status = WvBusEstablish(reg_path);
    if(!NT_SUCCESS(status))
      goto err_bus;

    /* Register re-initialization routine to allow for disks to arrive */
    WvlIncrementResourceUsage(WvDriverUsage);
    IoRegisterBootDriverReinitialization(
        WvDriverObj,
        WvDriverReinitialize,
        NULL
      );

    WvDriverStarted = TRUE;
    DBG("Exit\n");
    return STATUS_SUCCESS;

    err_bus:

    /* Release resources and unwind state */
    WvUnload(drv_obj);
    DBG("Exit due to failure\n");
    return status;
  }

static NTSTATUS STDCALL WvIrpNotSupported(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
  }

/**
 * Release resources and unwind state
 *
 * @param drv_obj
 *   The driver object provided by Windows
 */
static VOID STDCALL WvUnload(IN DRIVER_OBJECT * drv_obj) {
    DBG("Unloading...\n");

    WvDeregisterMiniDrivers();

    WvBusCleanup();
    if (WvDriverStateHandle != NULL)
      PoUnregisterSystemState(WvDriverStateHandle);
    wv_free(WvOsLoadOpts);
    WvDriverStarted = FALSE;
    WvlDebugModuleUnload();

    WvlWaitForResourceZeroUsage(WvDriverUsage);

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

/* Handle an IRP. */
static NTSTATUS WvDriverDispatchIrp(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    PDRIVER_DISPATCH irp_handler;

    WVL_M_DEBUG_IRP_START(dev_obj, irp);

    irp_handler = WvDevGetIrpHandler(dev_obj);
    return irp_handler(dev_obj, irp);
  }

/* Allow the driver to handle IRPs of a particular major code. */
NTSTATUS STDCALL WvDriverHandleMajor(IN UCHAR Major) {
    WvDriverObj->MajorFunction[Major] = WvDriverDispatchIrp;
    return STATUS_SUCCESS;
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

WVL_M_LIB NTSTATUS WvlRegisterMiniDriver(
    OUT S_WVL_MINI_DRIVER ** minidriver,
    IN OUT DRIVER_OBJECT * drv_obj,
    IN DRIVER_ADD_DEVICE * add_dev,
    IN DRIVER_UNLOAD * unload
  ) {
    /* Check for invalid parameters or uninitialized library */
    if (!minidriver || !drv_obj || !unload || !WvDriverStarted)
      return STATUS_UNSUCCESSFUL;

    *minidriver = wv_malloc(sizeof **minidriver);
    if (!*minidriver)
      return STATUS_INSUFFICIENT_RESOURCES;

    /* Initialize the mini-driver object */
    (*minidriver)->DriverObject = drv_obj;
    (*minidriver)->AddDevice = add_dev;
    (*minidriver)->Unload = unload;

    /* If the mini-driver is external, initialize its driver object */
    if (drv_obj != WvDriverObj) {
        /* IRP major function dispatch table */
        RtlCopyMemory(
            drv_obj->MajorFunction,
            WvDriverObj->MajorFunction,
            sizeof drv_obj->MajorFunction
          );

        /* Set the driver's Unload and AddDevice routines */
        drv_obj->DriverUnload = WvUnloadMiniDriver;
        drv_obj->DriverExtension->AddDevice = add_dev;
      }

    /* Register the mini-driver */
    WvlAppendLockedListLink(WvRegisteredMiniDrivers, (*minidriver)->Link);

    return STATUS_SUCCESS;
  }

WVL_M_LIB VOID WvlDeregisterMiniDriver(
    IN S_WVL_MINI_DRIVER * minidriver
  ) {
    /* Ignore an invalid parameter */
    if (!minidriver)
      return;

    /*
     * De-register the mini-driver.  If this is the last mini-driver
     * in the registration list, notify WvDeregisterMiniDrivers
     */
    if (WvlRemoveLockedListLink(WvRegisteredMiniDrivers, minidriver->Link))
      KeSetEvent(&WvMiniDriversDeregistered, 0, FALSE);

    wv_free(minidriver);
  }

/**
 * Invoke an external mini-driver's Unload routine.  Internal
 * mini-drivers' Unload routines will be invoked by WvDeregisterMiniDrivers.
 * Multiple mini-drivers with the same driver object are understood
 *
 * @param drv_obj
 *   The driver object provided by Windows
 */
static VOID STDCALL WvUnloadMiniDriver(IN DRIVER_OBJECT * drv_obj) {
    KIRQL irql;
    LIST_ENTRY * cur_link;
    S_WVL_MINI_DRIVER * minidriver;

    /* Find the associated mini-driver(s) */
    KeAcquireSpinLock(&WvRegisteredMiniDrivers->Lock, &irql);
    for (
        cur_link = WvRegisteredMiniDrivers->List->Flink;
        cur_link != WvRegisteredMiniDrivers->List;
        cur_link = cur_link->Flink
      ) {
        ASSERT(cur_link);
        minidriver = CONTAINING_RECORD(cur_link, S_WVL_MINI_DRIVER, Link);

        /* Check for a match */
        if (minidriver->DriverObject == drv_obj) {
            KeReleaseSpinLock(&WvRegisteredMiniDrivers->Lock, irql);

            ASSERT(minidriver->Unload);
            minidriver->Unload(drv_obj);

            /* Start over at the beginning of the list */
            KeAcquireSpinLock(&WvRegisteredMiniDrivers->Lock, &irql);
            cur_link = WvRegisteredMiniDrivers->List;
          }
      }
    KeReleaseSpinLock(&WvRegisteredMiniDrivers->Lock, irql);
  }

/**
 * Unload all internal mini-drivers and wait for all
 * mini-drivers to be deregistered
 */
static VOID WvDeregisterMiniDrivers(void) {
    /* Process internal mini-drivers */
    ASSERT(WvDriverObj);
    WvUnloadMiniDriver(WvDriverObj);

    /* Wait for all mini-driver Unload routines to complete */
    KeWaitForSingleObject(
        &WvMiniDriversDeregistered,
        Executive,
        KernelMode,
        FALSE,
        NULL
      );

    WvlDecrementResourceUsage(WvDriverUsage);
  }

/* TODO: Find a better place for this and adjust headers */
WVL_M_LIB VOID WvlInitializeLockedList(OUT S_WVL_LOCKED_LIST * list) {
    ASSERT(list);
    KeInitializeSpinLock(&list->Lock);
    InitializeListHead(list->List);
  }

/* TODO: Find a better place for this and adjust headers */
WVL_M_LIB VOID WvlAppendLockedListLink(
    IN OUT S_WVL_LOCKED_LIST * list,
    IN OUT LIST_ENTRY * link
  ) {
    ASSERT(list);
    ASSERT(link);
    ExInterlockedInsertTailList(list->List, link, &list->Lock);
  }

/* TODO: Find a better place for this and adjust headers */
WVL_M_LIB BOOLEAN WvlRemoveLockedListLink(
    IN OUT S_WVL_LOCKED_LIST * list,
    IN OUT LIST_ENTRY * link
  ) {
    KIRQL irql;
    BOOLEAN result;

    ASSERT(list);
    ASSERT(link);
    KeAcquireSpinLock(&list->Lock, &irql);
    result = RemoveEntryList(link);
    KeReleaseSpinLock(&list->Lock, irql);
    return result;
  }

/* TODO: Find a better place for this and adjust headers */
WVL_M_LIB VOID WvlInitializeResourceTracker(
    S_WVL_RESOURCE_TRACKER * res_tracker
  ) {
    ASSERT(res_tracker);
    res_tracker->UsageCount = 0;
    KeInitializeEvent(&res_tracker->ZeroUsage, NotificationEvent, TRUE);
  }

/* TODO: Find a better place for this and adjust headers */
WVL_M_LIB VOID WvlWaitForResourceZeroUsage(
    S_WVL_RESOURCE_TRACKER * res_tracker
  ) {
    ASSERT(res_tracker);
    KeWaitForSingleObject(
        &res_tracker->ZeroUsage,
        Executive,
        KernelMode,
        FALSE,
        NULL
      );
  }

/* TODO: Find a better place for this and adjust headers */
WVL_M_LIB VOID WvlIncrementResourceUsage(
    S_WVL_RESOURCE_TRACKER * res_tracker
  ) {
    LONG c;

    ASSERT(res_tracker);
    c = InterlockedIncrement(&res_tracker->UsageCount);

    /* If the usage is no longer zero, clear the event */
    ASSERT(c >= 0);
    if (c == 1)
      KeClearEvent(&res_tracker->ZeroUsage);
  }

/* TODO: Find a better place for this and adjust headers */
WVL_M_LIB VOID WvlDecrementResourceUsage(
    S_WVL_RESOURCE_TRACKER * res_tracker
  ) {
    LONG c;

    ASSERT(res_tracker);
    c = InterlockedDecrement(&res_tracker->UsageCount);

    /* If the usage count reaches zero, set the event */
    ASSERT(c >= 0);
    if (!c)
      KeSetEvent(&res_tracker->ZeroUsage, 0, FALSE);
  }
