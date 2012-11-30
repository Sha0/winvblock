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
extern DRIVER_INITIALIZE WvMainBusDriverEntry;
extern NTSTATUS STDCALL WvBusAttach(IN PDEVICE_OBJECT);
extern NTSTATUS STDCALL WvBusEstablish(IN PUNICODE_STRING);
extern NTSTATUS STDCALL WvBusDevCtl(IN PIRP, IN ULONG POINTER_ALIGNMENT);

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
/* TODO: DRIVER_ADD_DEVICE isn't available in DDK 3790.1830, it seems */
static DRIVER_ADD_DEVICE WvDriveDevice;
static DRIVER_UNLOAD WvUnloadMiniDriver;
static VOID WvDeregisterMiniDrivers(void);
static NTSTATUS WvStartDeviceThread(IN DEVICE_OBJECT * Device);
static VOID WvStopDeviceThread(IN DEVICE_OBJECT * Device);
static F_WVL_DEVICE_THREAD_FUNCTION WvStopDeviceThreadInThread;
static F_WVL_DEVICE_THREAD_FUNCTION WvTestDeviceThread;
static NTSTATUS WvAddIrpToDeviceQueue(IN DEVICE_OBJECT * Device, IN IRP * Irp);
/* KSTART_ROUTINE isn't available in DDK 3790.1830, it seems */
static VOID WvDeviceThread(VOID * Context);
static VOID WvProcessDeviceThreadWorkItem(
    IN DEVICE_OBJECT * Device,
    IN S_WVL_DEVICE_THREAD_WORK_ITEM * WorkItem,
    IN BOOLEAN DeviceNotAvailable
  );
static VOID WvProcessDeviceIrp(
    IN DEVICE_OBJECT * Device,
    IN IRP * Irp,
    IN BOOLEAN DeviceNotAvailable
  );

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
    drv_obj->DriverExtension->AddDevice = WvDriveDevice;

    /* Initialize the list of registered mini-drivers */
    WvlInitializeLockedList(WvRegisteredMiniDrivers);
    KeInitializeEvent(&WvMiniDriversDeregistered, NotificationEvent, TRUE);
    WvlIncrementResourceUsage(WvDriverUsage);

    /*
     * Invoke internal mini-drivers.  The order here determines which
     * mini-drivers have a higher priority for driving devices
     */
    status = WvMainBusDriverEntry(drv_obj, reg_path);
    if(!NT_SUCCESS(status))
      goto err_internal_minidriver;

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

    err_internal_minidriver:

    /* Release resources and unwind state */
    WvUnload(drv_obj);
    DBG("Exit due to failure\n");
    return status;
  }

/**
 * Drive a supported device
 *
 * @param DriverObject
 *   Ignored.  The driver object provided by Windows
 *
 * @param PhysicalDeviceObject
 *   The PDO to probe and attach an FDO to
 *
 * @return
 *   The status of the operation
 */
static NTSTATUS STDCALL WvDriveDevice(
    IN PDRIVER_OBJECT driver_obj,
    IN PDEVICE_OBJECT pdo
  ) {
    LIST_ENTRY * link;
    S_WVL_MINI_DRIVER * minidriver;
    NTSTATUS status;

    /* Ignore the main driver object that Windows will pass */
    (VOID) driver_obj;

    /* Assume failure */
    status = STATUS_NOT_SUPPORTED;

    /*
     * We need to own the registered mini-drivers list until we've
     * finished working with it.  Unfortunately, there is a slight race
     * here where:
     *
     * 1. An external mini-driver's Unload routine could be invoked,
     *    meaning the mini-driver should not be driving any more devices
     *
     * 2. This very function is invoked by Windows to attempt to drive a
     *    device
     *
     * 3. This function examines the list of registered drivers before
     *    the mini-driver's Unload routine has a chance to deregister
     *
     * Hopefully if the mini-driver calls IoCreateDevice while it's in an
     * "unloading" state (from Windows' perspective), the request will be
     * refused.  That would help for the mini-driver's AddDevice to fail
     * and return control back to this function.
     *
     * Just in case, the mini-driver's Unload routine's call to
     * WvlDeregisterMiniDriver will stall until all of the mini-driver's
     * devices have been deleted.  As long as the Unload routine hasn't
     * performed any clean-up before deregistering, the mini-driver
     * can continue to serve its devices, as awkward as it might be
     */
    WvlAcquireLockedList(WvRegisteredMiniDrivers);
    for (
        link = WvRegisteredMiniDrivers->List->Flink;
        link != WvRegisteredMiniDrivers->List;
        link = link->Flink
      ) {
        ASSERT(link);
        minidriver = CONTAINING_RECORD(link, S_WVL_MINI_DRIVER, Link);
        ASSERT(minidriver);

        /* Skip this mini-driver if it has no AddDevice routine */
        if (!minidriver->AddDevice)
          continue;

        ASSERT(minidriver->DriverObject);
        ASSERT(pdo);
        status = minidriver->AddDevice(minidriver->DriverObject, pdo);

        /* If the mini-driver drives the device, we're done */
        if (NT_SUCCESS(status))
          break;
      }
    WvlReleaseLockedList(WvRegisteredMiniDrivers);

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
 * @param DriverObject
 *   The driver object provided by Windows
 */
static VOID STDCALL WvUnload(IN DRIVER_OBJECT * drv_obj) {
    DBG("Unloading...\n");

    WvDeregisterMiniDrivers();

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

/**
 * Dispatch an IRP
 *
 * @param DeviceObject
 *   The target device for the IRP
 *
 * @param Irp
 *   The IRP to dispatch
 *
 * @return
 *   The status of the operation
 */
static NTSTATUS WvDriverDispatchIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    WV_S_DEV_EXT * dev_ext;
    VOID ** ptrs;
    KEVENT completion;
    NTSTATUS irp_status;
    NTSTATUS status;
    DRIVER_DISPATCH * irp_handler;

    ASSERT(dev_obj);
    ASSERT(irp);
    WVL_M_DEBUG_IRP_START(dev_obj, irp);

    /*
     * TODO: We currently handle the mini-driver and the old case.
     * Eventually there will just be the mini-driver case
     */
    dev_ext = dev_obj->DeviceExtension;
    ASSERT(dev_ext);

    if (dev_ext->MiniDriver) {
        ptrs = irp->Tail.Overlay.DriverContext;

        /* Clear context so it doesn't appear to be a work item */
        ptrs[0] = NULL;

        /* Initialize a completion event */
        KeInitializeEvent(&completion, NotificationEvent, FALSE);
        ptrs[1] = &completion;

        /* Collect status */
        ptrs[2] = &irp_status;

        /* Add the IRP to the device's queue */
        status = WvAddIrpToDeviceQueue(dev_obj, irp);
        if (!NT_SUCCESS(status))
          return status;

        /* Wait for IRP completion and return status */
        KeWaitForSingleObject(
            &completion,
            Executive,
            KernelMode,
            FALSE,
            NULL
          );
        return irp_status;
      }

    /* Handle the old, non-mini-driver case */
    irp_handler = WvDevGetIrpHandler(dev_obj);
    ASSERT(irp_handler);
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
    /* Check for invalid parameters */
    if (!minidriver || !drv_obj || !unload)
      return STATUS_UNSUCCESSFUL;

    *minidriver = wv_malloc(sizeof **minidriver);
    if (!*minidriver)
      return STATUS_INSUFFICIENT_RESOURCES;

    /* Initialize the mini-driver object */
    (*minidriver)->DriverObject = drv_obj;
    (*minidriver)->AddDevice = add_dev;
    (*minidriver)->Unload = unload;
    WvlInitializeResourceTracker((*minidriver)->Usage);

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
    KeClearEvent(&WvMiniDriversDeregistered);

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

    WvlWaitForResourceZeroUsage(minidriver->Usage);
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
    LIST_ENTRY * cur_link;
    S_WVL_MINI_DRIVER * minidriver;

    /* Find the associated mini-driver(s) */
    WvlAcquireLockedList(WvRegisteredMiniDrivers);
    for (
        cur_link = WvRegisteredMiniDrivers->List->Flink;
        cur_link != WvRegisteredMiniDrivers->List;
        cur_link = cur_link->Flink
      ) {
        ASSERT(cur_link);
        minidriver = CONTAINING_RECORD(cur_link, S_WVL_MINI_DRIVER, Link);

        /* Check for a match */
        if (minidriver->DriverObject == drv_obj) {
            WvlReleaseLockedList(WvRegisteredMiniDrivers);

            ASSERT(minidriver->Unload);
            minidriver->Unload(drv_obj);

            /* Start over at the beginning of the list */
            WvlAcquireLockedList(WvRegisteredMiniDrivers);
            cur_link = WvRegisteredMiniDrivers->List;
          }
      }
    WvlReleaseLockedList(WvRegisteredMiniDrivers);
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
    KeInitializeEvent(&list->Lock, SynchronizationEvent, TRUE);
    InitializeListHead(list->List);
  }

/* TODO: Find a better place for this and adjust headers */
WVL_M_LIB VOID WvlAcquireLockedList(IN S_WVL_LOCKED_LIST * list) {
    ASSERT(list);
    KeWaitForSingleObject(&list->Lock, Executive, KernelMode, FALSE, NULL);
  }

/* TODO: Find a better place for this and adjust headers */
WVL_M_LIB VOID WvlReleaseLockedList(IN S_WVL_LOCKED_LIST * list) {
    ASSERT(list);
    KeSetEvent(&list->Lock, 0, FALSE);
  }

/* TODO: Find a better place for this and adjust headers */
WVL_M_LIB VOID WvlAppendLockedListLink(
    IN OUT S_WVL_LOCKED_LIST * list,
    IN OUT LIST_ENTRY * link
  ) {
    ASSERT(list);
    ASSERT(link);
    WvlAcquireLockedList(list);
    InsertTailList(list->List, link);
    WvlReleaseLockedList(list);
  }

/* TODO: Find a better place for this and adjust headers */
WVL_M_LIB BOOLEAN WvlRemoveLockedListLink(
    IN OUT S_WVL_LOCKED_LIST * list,
    IN OUT LIST_ENTRY * link
  ) {
    BOOLEAN result;

    ASSERT(list);
    ASSERT(link);
    WvlAcquireLockedList(list);
    result = RemoveEntryList(link);
    WvlReleaseLockedList(list);
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

WVL_M_LIB NTSTATUS STDCALL WvlCreateDevice(
    IN S_WVL_MINI_DRIVER * minidriver,
    IN ULONG dev_ext_sz,
    IN UNICODE_STRING * dev_name,
    IN DEVICE_TYPE dev_type,
    IN ULONG dev_characteristics,
    IN BOOLEAN exclusive,
    OUT DEVICE_OBJECT ** dev_obj
  ) {
    NTSTATUS status;
    DEVICE_OBJECT * new_dev;
    WV_S_DEV_EXT * new_dev_ext;

    /* Check for invalid parameters */
    if (!minidriver || dev_ext_sz < sizeof *new_dev_ext || !dev_obj)
      return STATUS_INVALID_PARAMETER;

    /* Create the device object */
    ASSERT(minidriver->DriverObject);
    status = IoCreateDevice(
        minidriver->DriverObject,
        dev_ext_sz,
        dev_name,
        dev_type,
        dev_characteristics,
        exclusive,
        &new_dev
      );
    if (!NT_SUCCESS(status))
      goto err_create_dev;
    ASSERT(new_dev);

    /* Initialize the common part of the device extension */
    new_dev_ext = new_dev->DeviceExtension;
    ASSERT(new_dev_ext);
    /* TODO: device and IrpDispatch */
    KeInitializeEvent(&new_dev_ext->IrpArrival, NotificationEvent, FALSE);
    InitializeListHead(new_dev_ext->IrpQueue);
    new_dev_ext->NotAvailable = FALSE;
    KeInitializeSpinLock(&new_dev_ext->Lock);
    new_dev_ext->MiniDriver = minidriver;
    WvlInitializeResourceTracker(new_dev_ext->Usage);

    /* Start the device thread */
    status = WvStartDeviceThread(new_dev);
    if (!NT_SUCCESS(status))
      goto err_dev_thread;

    /* When the thread is stopping, it'll decrement this usage */
    WvlIncrementResourceUsage(minidriver->Usage);

    *dev_obj = new_dev;
    return STATUS_SUCCESS;

    WvStopDeviceThread(new_dev);
    err_dev_thread:

    IoDeleteDevice(new_dev);
    err_create_dev:

    return status;
  }

WVL_M_LIB VOID STDCALL WvlDeleteDevice(IN DEVICE_OBJECT * dev) {
    WV_S_DEV_EXT * dev_ext;
    KIRQL irql;

    ASSERT(dev);
    dev_ext = dev->DeviceExtension;
    ASSERT(dev_ext);
    KeAcquireSpinLock(&dev_ext->Lock, &irql);
    dev_ext->NotAvailable = TRUE;
    KeReleaseSpinLock(&dev_ext->Lock, irql);
  }

/**
 * Start a device thread
 *
 * @param Device
 *   The device to start a thread for
 *
 * @return
 *   The status of the operation
 */
static NTSTATUS WvStartDeviceThread(IN DEVICE_OBJECT * dev) {
    WV_S_DEV_EXT * dev_ext;
    OBJECT_ATTRIBUTES obj_attrs;
    NTSTATUS status;

    if (!dev)
      return STATUS_INVALID_PARAMETER;

    dev_ext = dev->DeviceExtension;
    ASSERT(dev_ext);

    KeInitializeEvent(&dev_ext->IrpArrival, NotificationEvent, FALSE);
    InitializeObjectAttributes(
        &obj_attrs,
        NULL,
        OBJ_KERNEL_HANDLE,
        NULL,
        NULL
      );
    status = PsCreateSystemThread(
        &dev_ext->ThreadHandle,
        THREAD_ALL_ACCESS,
        &obj_attrs,
        NULL,
        NULL,
        WvDeviceThread,
        dev
      );
    if (!NT_SUCCESS(status))
      goto err_create_thread;

    WvlIncrementResourceUsage(dev_ext->Usage);

    status = WvlCallFunctionInDeviceThread(
        dev,
        WvTestDeviceThread,
        NULL,
        TRUE
      );
    if (!NT_SUCCESS(status))
      goto err_test_thread;

    return status;

    err_test_thread:

    WvStopDeviceThread(dev);
    err_create_thread:

    return status;
  }

/**
 * Stop a device thread
 *
 * @param Device
 *   The device whose thread will be stopped
 */
static VOID WvStopDeviceThread(IN DEVICE_OBJECT * dev) {
    WV_S_DEV_EXT * dev_ext;
    HANDLE thread_handle;
    NTSTATUS status;
    PETHREAD thread;

    ASSERT(dev);
    dev_ext = dev->DeviceExtension;
    ASSERT(dev_ext);
    thread_handle = dev_ext->ThreadHandle;
    ASSERT(thread_handle);

    /* Reference the thread */
    status = ObReferenceObjectByHandle(
        thread_handle,
        THREAD_ALL_ACCESS,
        *PsThreadType,
        KernelMode,
        &thread,
        NULL
      );
    ASSERT(NT_SUCCESS(status));

    /* Signal the the stop */
    status = WvlCallFunctionInDeviceThread(
        dev,
        WvStopDeviceThreadInThread,
        NULL,
        TRUE
      );
    ASSERT(NT_SUCCESS(status));

    /* Wait for the thread to stop */
    KeWaitForSingleObject(
        thread,
        Executive,
        KernelMode,
        FALSE,
        NULL
      );

    /* Cleanup */
    ObDereferenceObject(thread);
    ZwClose(thread_handle);
  }

/**
 * Stop a device thread from within the thread
 *
 * @param Device
 *   The device whose thread will be stopped
 *
 * @param Context
 *   Ignored
 *
 * @retval STATUS_SUCCESS
 *
 * This function is a bit of a sentinel, as WvDeviceThread will notice it
 */
static NTSTATUS WvStopDeviceThreadInThread(
    IN DEVICE_OBJECT * dev,
    IN VOID * c
  ) {
    ASSERT(dev);
    (VOID) c;
    DBG("Stopping thread for device %p...\n", (VOID *) dev);
    return STATUS_SUCCESS;
  }

/**
 * Test a device thread
 *
 * @param Device
 *   The device whose thread will be tested
 *
 * @param Context
 *   Ignored.
 *
 * @retval STATUS_SUCCESS
 */
static NTSTATUS WvTestDeviceThread(IN DEVICE_OBJECT * dev, IN VOID * c) {
    WV_S_DEV_EXT * dev_ext;

    (VOID) c;

    ASSERT(dev);
    dev_ext = dev->DeviceExtension;
    ASSERT(dev_ext);

    WvlIncrementResourceUsage(dev_ext->Usage);
    WvlDecrementResourceUsage(dev_ext->Usage);
    DBG("Thread test for device %p passed\n", (VOID *) dev);
    return STATUS_SUCCESS;
  }

WVL_M_LIB NTSTATUS STDCALL WvlCallFunctionInDeviceThread(
    IN DEVICE_OBJECT * dev,
    IN F_WVL_DEVICE_THREAD_FUNCTION * func,
    IN VOID * context,
    IN BOOLEAN wait
  ) {
    WV_S_DEV_EXT * dev_ext;
    S_WVL_DEVICE_THREAD_WORK_ITEM stack_work_item;
    S_WVL_DEVICE_THREAD_WORK_ITEM * work_item;
    VOID ** ptrs;
    NTSTATUS status;

    /* Check for invalid parameters */
    if (!dev || !func)
      return STATUS_INVALID_PARAMETER;

    dev_ext = dev->DeviceExtension;
    ASSERT(dev_ext);

    /*
     * If we wait for the function to complete, we use the stack.
     * Otherwise, we try to allocate the work item
     */
    if (!wait) {
        work_item = wv_malloc(sizeof *work_item);
        if (!work_item)
          return STATUS_INSUFFICIENT_RESOURCES;
      } else {
        work_item = &stack_work_item;
      }

    work_item->Function = func;
    KeInitializeEvent(&work_item->Complete, NotificationEvent, FALSE);
    work_item->Status = STATUS_DRIVER_INTERNAL_ERROR;

    /**
     * We use some of the 4 driver-owned pointers in the dummy IRP.
     * The first lets WvDeviceThread recognize and find the work item.
     * The second is the context to be passed to the called function.
     * The third lets WvDeviceThread recognize and free the work item,
     * if required
     */
    ptrs = work_item->DummyIrp->Tail.Overlay.DriverContext;
    ptrs[0] = work_item;
    ptrs[1] = context;
    ptrs[2] = wait ? NULL : work_item;
    ptrs[3] = NULL;

    status = WvAddIrpToDeviceQueue(dev, work_item->DummyIrp);
    if (!NT_SUCCESS(status)) {
        /* The thread is probably stopped */
        if (!wait)
          wv_free(work_item);
        return status;
      }

    /* It we're not waiting, just return success */
    if (!wait)
      return STATUS_SUCCESS;

    /* Otherwise, wait for the function to complete and return its status */
    KeWaitForSingleObject(
        &work_item->Complete,
        Executive,
        KernelMode,
        FALSE,
        NULL
      );
    return work_item->Status;
  }

/**
 * Add an IRP (or pseudo-IRP) to a device's IRP queue
 *
 * @param Device
 *   The device to process the IRP
 *
 * @param Irp
 *   The IRP to enqueue for the device
 *
 * @retval STATUS_NO_SUCH_DEVICE
 *   The device is no longer available
 * @retval STATUS_SUCCESS
 *   The IRP was successfully queued
 */
static NTSTATUS WvAddIrpToDeviceQueue(IN DEVICE_OBJECT * dev, IN IRP * irp) {
    WV_S_DEV_EXT * dev_ext;
    KIRQL irql;
    NTSTATUS status;

    ASSERT(dev);
    ASSERT(irp);

    dev_ext = dev->DeviceExtension;
    ASSERT(dev_ext);

    KeAcquireSpinLock(&dev_ext->Lock, &irql);
    if (dev_ext->NotAvailable) {
        status = STATUS_NO_SUCH_DEVICE;
      } else {
        InsertTailList(dev_ext->IrpQueue, &irp->Tail.Overlay.ListEntry);
        status = STATUS_SUCCESS;
      }
    KeReleaseSpinLock(&dev_ext->Lock, irql);
    KeSetEvent(&dev_ext->IrpArrival, 0, FALSE);

    return status;
  }

/**
 * The device thread for a mini-driver device
 *
 * @param Context
 *   The device object
 *
 * This routine will process IRPs (and pseudo-IRPs) in the device's queue
 */
static VOID STDCALL WvDeviceThread(IN VOID * context) {
    DEVICE_OBJECT * dev;
    WV_S_DEV_EXT * dev_ext;
    LARGE_INTEGER timeout;
    LIST_ENTRY * link;
    BOOLEAN stop;
    KIRQL irql;
    IRP * irp;
    S_WVL_DEVICE_THREAD_WORK_ITEM * work_item;
    S_WVL_MINI_DRIVER * minidriver;

    dev = context;
    ASSERT(dev);

    DBG("Thread for device %p started\n", (VOID *) dev);

    dev_ext = dev->DeviceExtension;
    ASSERT(dev_ext);

    /* Wake up at least every 30 seconds. */
    timeout.QuadPart = -300000000LL;

    stop = FALSE;
    KeAcquireSpinLock(&dev_ext->Lock, &irql);
    while (1) {
        link = RemoveHeadList(dev_ext->IrpQueue);
        ASSERT(link);

        /* Did we finish our pass through the IRP queue? */
        if (link == dev_ext->IrpQueue) {
            /* Yes */
            KeReleaseSpinLock(&dev_ext->Lock, irql);

            /* Are we stopping? */
            if (stop) {
                /*
                 * Yes.  All IRPs have been processed and no more
                 * can be added, since the device was marked as no
                 * longer available before 'stop' was set
                 */
                break;
              }

            /* Otherwise, wait for more work or the timeout */
            KeWaitForSingleObject(
                &dev_ext->IrpArrival,
                Executive,
                KernelMode,
                FALSE,
                &timeout
              );
            KeClearEvent(&dev_ext->IrpArrival);
            KeAcquireSpinLock(&dev_ext->Lock, &irql);
            continue;
          }

        /* Otherwise, examine the IRP */
        irp = CONTAINING_RECORD(link, IRP, Tail.Overlay.ListEntry);
        ASSERT(irp);

        /* Is this a work item? */
        if (irp->Tail.Overlay.DriverContext[0]) {
            /* Yes */
            work_item = CONTAINING_RECORD(
                irp,
                S_WVL_DEVICE_THREAD_WORK_ITEM,
                DummyIrp
              );
            ASSERT(work_item);

            /* Check for a stop signal */
            if (work_item->Function == WvStopDeviceThreadInThread)
              dev_ext->NotAvailable = TRUE;

            /* Restore the IRQL and process the work item */
            KeReleaseSpinLock(&dev_ext->Lock, irql);
            WvProcessDeviceThreadWorkItem(dev, work_item, stop);
          } else {
            /* This is a normal IRP.  Restore the IRQL and process it */
            KeReleaseSpinLock(&dev_ext->Lock, irql);
            WvProcessDeviceIrp(dev, irp, stop);
          }

        /* Continue processing the IRP queue */
        KeAcquireSpinLock(&dev_ext->Lock, &irql);

        /* Check if we should be stopping, soon */
        if (dev_ext->NotAvailable)
          stop = TRUE;
      }

    minidriver = dev_ext->MiniDriver;
    ASSERT(minidriver);

    /* Delete the device */
    WvlDecrementResourceUsage(dev_ext->Usage);
    WvlWaitForResourceZeroUsage(dev_ext->Usage);
    DBG("Device %p deleted.  Thread terminated\n", (VOID *) dev);
    IoDeleteDevice(dev);

    WvlDecrementResourceUsage(minidriver->Usage);

    PsTerminateSystemThread(STATUS_SUCCESS);
    DBG("Yikes!\n");
  }

/**
 * Process a device thread work item
 *
 * @param Device
 *   The device to process the work item
 *
 * @param WorkItem
 *   The work item to process
 *
 * @param DeviceNotAvailable
 *   A boolean specifying whether or not to reject the work item
 */
static VOID WvProcessDeviceThreadWorkItem(
    IN DEVICE_OBJECT * dev,
    IN S_WVL_DEVICE_THREAD_WORK_ITEM * work_item,
    IN BOOLEAN reject
  ) {
    VOID ** ptrs;

    ASSERT(dev);
    ASSERT(work_item);

    if (reject) {
        work_item->Status = STATUS_NO_SUCH_DEVICE;
      } else {
        /* Call the work item function, passing the device and context */
        ptrs = work_item->DummyIrp->Tail.Overlay.DriverContext;
        ASSERT(work_item->Function);
        work_item->Status = work_item->Function(dev, ptrs[1]);

        /* Possible cleanup */
        /* TODO: Move this 'if' into 'wv_free'? */
        if (ptrs[2])
          wv_free(ptrs[2]);
      }

    /* Signal that the work item has been completed */
    KeSetEvent(&work_item->Complete, 0, FALSE);
  }

/**
 * Process an IRP in a device's thread context
 *
 * @param Device
 *   The device to process the IRP
 *
 * @param Irp
 *   The IRP to process
 *
 * @param DeviceNotAvailable
 *   A boolean specifying whether or not to reject the IRP
 */
static VOID WvProcessDeviceIrp(
    IN DEVICE_OBJECT * dev,
    IN IRP * irp,
    IN BOOLEAN reject
  ) {
    WV_S_DEV_EXT * dev_ext;
    KEVENT * event;
    NTSTATUS * status;

    ASSERT(dev);
    ASSERT(irp);

    /* Note the IRP completion event and status slot */
    event = irp->Tail.Overlay.DriverContext[1];
    ASSERT(event);
    status = irp->Tail.Overlay.DriverContext[2];
    ASSERT(status);

    if (reject) {
        *status = irp->IoStatus.Status = STATUS_NO_SUCH_DEVICE;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
      } else {
        /* Dispatch the IRP */
        dev_ext = dev->DeviceExtension;
        ASSERT(dev_ext);
        ASSERT(dev_ext->IrpDispatch);
        *status = dev_ext->IrpDispatch(dev, irp);
      }

    /* Signal completion to the waiting thread */
    KeSetEvent(event, 0, FALSE);
  }
