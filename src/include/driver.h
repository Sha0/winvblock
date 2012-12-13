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
#ifndef WV_M_DRIVER_H_
#  define WV_M_DRIVER_H_

/**
 * @file
 *
 * Driver specifics.
 */

/* For testing and debugging */
#if 0
#  define RIS
#  define WVL_M_DEBUG_IRPS 1
#  define DEBUGMOSTPROTOCOLCALLS
#  define DEBUGALLPROTOCOLCALLS
#endif

#define POOLSIZE 2048

/** Constants */

/** Device flags for the Flags member of a WV_DEV_EXT */
enum E_WVL_DEVICE_FLAG {
    CvWvlDeviceFlagSerialIrps_,
    CvWvlDeviceFlagSerialIrpsNeedSignal_,
    CvWvlDeviceFlagAvailable_,
    CvWvlDeviceFlagLinked_,
    CvWvlDeviceFlagThread_,
    CvWvlDeviceFlagSerialIrps = 1 << CvWvlDeviceFlagSerialIrps_,
    CvWvlDeviceFlagSerialIrpsNeedSignal =
      1 << CvWvlDeviceFlagSerialIrpsNeedSignal_,
    CvWvlDeviceFlagAvailable = 1 << CvWvlDeviceFlagAvailable_,
    CvWvlDeviceFlagLinked = 1 << CvWvlDeviceFlagLinked_,
    CvWvlDeviceFlagThread = 1 << CvWvlDeviceFlagThread_,
    CvWvlDeviceFlagZero = 0
  };

/** Object types */

/** PETHREAD is documented as a pointer type... */
typedef struct _ETHREAD ETHREAD;

typedef enum E_WVL_DEVICE_FLAG E_WVL_DEVICE_FLAG;

typedef struct S_WVL_LOCKED_LIST S_WVL_LOCKED_LIST;

typedef struct S_WVL_RESOURCE_TRACKER S_WVL_RESOURCE_TRACKER;

typedef struct S_WVL_MINI_DRIVER S_WVL_MINI_DRIVER;

typedef struct WV_DEV_EXT WV_S_DEV_EXT, * WV_SP_DEV_EXT;

typedef struct S_WVL_STATUS_WAITER S_WVL_STATUS_WAITER;

typedef struct S_WVL_DEVICE_THREAD_WORK_ITEM S_WVL_DEVICE_THREAD_WORK_ITEM;

typedef DRIVER_DISPATCH * A_WVL_MJ_DISPATCH_TABLE[IRP_MJ_MAXIMUM_FUNCTION + 1];

/** Function types */

/**
 * The type of a function to be called within a device's thread context
 *
 * @param Device
 *   The device whose thread context the function will be called within
 *
 * @param Context
 *   Optional.  Additional context to pass to the function
 *
 * @return
 *   The status of the operation
 */
typedef NTSTATUS F_WVL_DEVICE_THREAD_FUNCTION(IN DEVICE_OBJECT *, IN VOID *);

/* Haven't found a better place for this, yet. */
extern NTSTATUS STDCALL WvDriverGetDevCapabilities(
    IN PDEVICE_OBJECT,
    IN PDEVICE_CAPABILITIES
  );

/** Function declarations */

/* From driver.c */
extern DRIVER_INITIALIZE DriverEntry;

/* From mainbus/mainbus.c */

/**
 * Add a child node to the main bus
 *
 * @param Device
 *   Points to the child device to add
 *
 * @retval TRUE
 *   Success
 * @retval FALSE
 *   Failure
 */
/* TODO: Fix WV_SP_DEV_T being unknown, here */
extern BOOLEAN STDCALL WvBusAddDev(IN WV_SP_DEV_T);

/** From disk.c */
extern NTSTATUS STDCALL WvDiskPnpQueryDevText(
    IN PDEVICE_OBJECT,
    IN PIRP,
    IN struct WVL_DISK_T *
  );

/**
 * Register a mini-driver
 *
 * @param MiniDriver
 *   Returns a mini-driver object for the registration.  This can later
 *   be used with WvlDeregisterMiniDriver().  This parameter is required
 *
 * @param DriverObject
 *   The driver object provided to the mini-driver by Windows.  This
 *   parameter is required
 *
 * @param AddDevice
 *   The mini-driver's AddDevice routine.  This parameter is optional
 *
 * @param Unload
 *   The mini-driver's Unload routine.  This routine must later call
 *   WvDeregisterMiniDriver with the mini-driver object received.
 *   This parameter is required
 *
 * @retval STATUS_SUCCESS
 * @retval STATUS_UNSUCCESSFUL
 *   An invalid parameter was passed
 * @retval STATUS_INSUFFICIENT_RESOURCES
 *   A mini-driver object could not be allocated
 */
extern WVL_M_LIB NTSTATUS WvlRegisterMiniDriver(
    OUT S_WVL_MINI_DRIVER ** MiniDriver,
    IN OUT DRIVER_OBJECT * DriverObject,
    IN DRIVER_ADD_DEVICE * AddDevice,
    IN DRIVER_UNLOAD * Unload
  );

/**
 * Deregister a mini-driver.  Called by a mini-driver's Unload routine
 *
 * @param MiniDriver
 *   The mini-driver to deregister.  After this function returns, the
 *   caller must not attempt to use the same pointer value
 */
extern WVL_M_LIB VOID WvlDeregisterMiniDriver(
    IN S_WVL_MINI_DRIVER * MiniDriver
  );

/**
 * Initialize an atomic list
 *
 * @param List
 *   The atomic list to initialize
 */
extern WVL_M_LIB VOID WvlInitializeLockedList(OUT S_WVL_LOCKED_LIST * List);

/**
 * Acquire the lock for an atomic list
 *
 * @param List
 *   The atomic list to acquire the lock for
 */
extern WVL_M_LIB VOID WvlAcquireLockedList(IN S_WVL_LOCKED_LIST * List);

/**
 * Release the lock for an atomic list
 *
 * @param List
 *   The atomic list to release the lock for
 */
extern WVL_M_LIB VOID WvlReleaseLockedList(IN S_WVL_LOCKED_LIST * List);

/**
 * Append an item to an atomic list
 *
 * @param List
 *   The atomic list to append a list item to
 *
 * @param Link
 *   The list item to append
 */
extern WVL_M_LIB VOID WvlAppendLockedListLink(
    IN OUT S_WVL_LOCKED_LIST * List,
    IN OUT LIST_ENTRY * Link
  );

/**
 * Remove an atomic list item from the list
 *
 * @param List
 *   The atomic list to remove the item from
 *
 * @param Link
 *   The item to remove
 *
 * @retval TRUE - The list is now empty
 * @retval FALSE - The list still has links
 */
extern WVL_M_LIB BOOLEAN WvlRemoveLockedListLink(
    IN OUT S_WVL_LOCKED_LIST * List,
    IN OUT LIST_ENTRY * Link
  );

/**
 * Initialize a resource tracker
 *
 * @param ResourceTracker
 *   The resource tracker to initialize
 */
extern WVL_M_LIB VOID WvlInitializeResourceTracker(
    S_WVL_RESOURCE_TRACKER * ResourceTracker
  );

/**
 * Wait for a resource to have zero usage
 *
 * @param ResourceTracker
 *   The tracker for the resource
 */
extern WVL_M_LIB VOID WvlWaitForResourceZeroUsage(
    S_WVL_RESOURCE_TRACKER * ResourceTracker
  );

/**
 * Increment the usage count for a resource
 *
 * @param ResourceTracker
 *   The tracker for the resource
 */
extern WVL_M_LIB VOID WvlIncrementResourceUsage(
    S_WVL_RESOURCE_TRACKER * ResourceTracker
  );

/**
 * Decrement the usage count for a resource
 *
 * @param ResourceTracker
 *   The tracker for the resource
 */
extern WVL_M_LIB VOID WvlDecrementResourceUsage(
    S_WVL_RESOURCE_TRACKER * ResourceTracker
  );

/**
 * Create a mini-driver device
 *
 * @param MiniDriver
 *   The mini-driver associated with the device
 *
 * @param DeviceExtensionSize
 *   The size of the device extension, in bytes.  This must be at least
 *   large enough to hold a WV_S_DEV_EXT, as the device extension for
 *   all mini-driver devices must point to a WV_S_DEV_EXT
 *
 * @param DeviceName
 *   Optional.  The name of the device
 *
 * @param DeviceType
 *   The device type
 *
 * @param DeviceCharacteristics
 *   Flags for the device characteristics
 *
 * @param Exclusive
 *   Determines whether or not to allow multiple handles to the device
 *
 * @param DeviceObject
 *   Points to the DEVICE_OBJECT pointer to populate with a pointer to
 *   the created mini-driver device
 *
 * @return
 *   The status of the operation
 *
 * Each mini-driver device has an associated thread
 */
extern WVL_M_LIB NTSTATUS STDCALL WvlCreateDevice(
    IN S_WVL_MINI_DRIVER * MiniDriver,
    IN ULONG DeviceExtensionSize,
    IN UNICODE_STRING * DeviceName,
    IN DEVICE_TYPE DeviceType,
    IN ULONG DeviceCharacteristics,
    IN BOOLEAN Exclusive,
    OUT DEVICE_OBJECT ** DeviceObject
  );

/**
 * Delete a mini-driver device
 *
 * @param Device
 *   The mini-driver device to be deleted
 */
extern WVL_M_LIB VOID STDCALL WvlDeleteDevice(IN DEVICE_OBJECT * Device);

/**
 * Acquire the lock for a mini-driver device
 *
 * @param Device
 *   The device to acquire the lock for
 *
 * Certain members of the device extension require the device lock for
 * inspection or modification.  Call this function to acquire that lock.
 * Call WvlUnlockDevice to unlock the device.  While the device lock is
 * acquired, the IRQL == DISPATCH_LEVEL, so the restrictions of that IRQL
 * apply
 */
extern WVL_M_LIB VOID STDCALL WvlLockDevice(IN DEVICE_OBJECT * Device);

/**
 * Release the lock for a mini-driver device
 *
 * @param Device
 *   The device to release the lock for
 *
 * Certain members of the device extension require the device lock for
 * inspection or modification.  Call this function to release that lock.
 * Call WvlLockDevice to lock the device.  Because acquiring the device
 * locked yields IRQL == DISPATCH_LEVEL, this function must be called in
 * order to restore the previous IRQL
 */
extern WVL_M_LIB VOID STDCALL WvlUnlockDevice(IN DEVICE_OBJECT * Device);

/**
 * Call a function in a device's thread context
 *
 * @param Device
 *   The device whose thread context will be used
 *
 * @param Function
 *   The function to be called
 *
 * @param Context
 *   Optional.  Additional context to pass to the function
 *
 * @param Wait
 *   Specifies whether or not to wait for the called function to complete.
 *   If FALSE, a work item will be allocated from non-paged pool and will
 *   be freed once the called function has completed
 *
 * @retval STATUS_INSUFFICIENT_RESOURCES
 *   If 'Wait' was FALSE, this means that a work item could not be
 *   allocated from non-paged pool.  Otherwise, this is the status returned
 *   by the called function
 * @retval STATUS_SUCCESS
 *   If 'Wait' was FALSE, this means the the work item was enqueued
 * @retval STATUS_INVALID_PARAMETER
 *    An invalid parameter was passed to this function or, if 'Wait'
 *    is 'TRUE', this status could also have been returned by the called
 *    function
 * @retval STATUS_NO_SUCH_DEVICE
 *    Returned if the device is no longer available
 * @return
 *   If 'Wait' was TRUE, the status is that returned by the called function
 */
extern WVL_M_LIB NTSTATUS STDCALL WvlCallFunctionInDeviceThread(
    IN DEVICE_OBJECT * Device,
    IN F_WVL_DEVICE_THREAD_FUNCTION * Function,
    IN VOID * Context,
    IN BOOLEAN Wait
  );

/**
 * Add an IRP (or pseudo-IRP) to a device's IRP queue
 *
 * @param DeviceObject
 *   The device to process the IRP
 *
 * @param Irp
 *   The IRP to enqueue for the device
 *
 * @param Wait
 *   Specifies whether or not to wait for the IRP to be processed by the
 *   mini-driver device's thread.
 *
 *   If FALSE and the IRP is added to the device's queue, this function
 *   should return STATUS_PENDING.
 *
 *   If FALSE and the device is no longer available, the IRP will be
 *   completed and this function should return STATUS_NO_SUCH_DEVICE.
 *
 *   If TRUE and the IRP is added to the device's queue, this function
 *   returns the status of processing the IRP within the device's
 *   thread context.
 *
 *   If TRUE and the device is no longer available, the IRP will be
 *   completed and this function should return STATUS_NO_SUCH_DEVICE
 *
 * @retval STATUS_NO_SUCH_DEVICE
 *   The device is no longer available.  'Wait' is TRUE or FALSE
 * @retval STATUS_PENDING
 *   The IRP was successfully queued and 'Wait' was FALSE
 * @return
 *   Other values are returned when 'Wait' is TRUE and indicate the
 *   status of processing the IRP within the device's thread context
 *
 * It is a programming error to call WvlPassIrpUp or to access the IRP
 * after calling this function until the next time the device's thread
 * schedules the IRP for processing.
 *
 * It is a programming error to attempt to access the device or its
 * extension after receiving STATUS_NO_SUCH_DEVICE
 */
extern WVL_M_LIB NTSTATUS STDCALL WvlAddIrpToDeviceQueue(
    IN DEVICE_OBJECT * Device,
    IN IRP * Irp,
    IN BOOLEAN Wait
  );

/**
 * Pass an IRP up the device stack, or complete it, if we're at the top
 *
 * @param DeviceObject
 *   The device releasing the IRP
 *
 * @param Irp
 *   The IRP that has been completed by this driver
 *
 * @param PriorityBoost
 *   A system-defined value specifying how to boost the priority of the
 *   original thread that produced the IRP
 *
 * This function should be called instead of IoCompleteRequest, by
 * mini-drivers
 */
extern WVL_M_LIB VOID STDCALL WvlPassIrpUp(
    IN DEVICE_OBJECT * DeviceObject,
    IN IRP * Irp,
    IN const CHAR PriorityBoost
  );

/**
 * Pass an IRP down the device stack
 *
 * @param DeviceObject
 *   The device at the current level of the IRP's processing
 *
 * @param Irp
 *   The IRP that will be processed by the next-lower device
 *
 * This function should be called instead of IoCallDriver, by
 * mini-drivers
 */
extern WVL_M_LIB NTSTATUS STDCALL WvlPassIrpDown(
    IN DEVICE_OBJECT * DeviceObject,
    IN OUT IRP * Irp
  );

/**
 * Wait for a lower device object in the device stack to complete an IRP
 *
 * @param DeviceObject
 *   The device at the current level of the IRP's processing
 *
 * @param Irp
 *   The IRP to send to the lower device and wait for
 *
 * @return
 *   The completed IRP's status returned by the lower device driver(s)
 */
extern WVL_M_LIB NTSTATUS STDCALL WvlWaitForIrpCompletion(
    IN DEVICE_OBJECT * DeviceObject,
    IN IRP * Irp
  );

/**
 * Test if the current thread is the mini-driver device's thread
 *
 * @param DeviceObject
 *   The device object whose thread will be compared with the current thread
 *
 * @reval TRUE
 * @retval FALSE
 */
extern WVL_M_LIB BOOLEAN STDCALL WvlInDeviceThread(
    IN DEVICE_OBJECT * DeviceObject
  );

/**
 * Wait for the active IRP count to reach 1
 *
 * @param DeviceObject
 *   The device object to process the IRP with
 *
 * @retval STATUS_SUCCESS
 * @retval STATUS_NO_SUCH_DEVICE
 *
 * Upon success, this function will ensure that all IRPs that came before
 * the call have completed.  In this way, it serves as a sequence point.
 * It is beyond the scope of this function to prevent new IRPs from being
 * processed _after_ this function returns; a different synchronization
 * method is needed for that.
 *
 * This is useful for PnP IRPs that must change device state after
 * outstanding IRPs have been completed, but before any new IRPs are
 * processed, for example.
 *
 * It is a programming error to attempt to access a device or its extension
 * after receiving STATUS_NO_SUCH_DEVICE
 */
extern WVL_M_LIB NTSTATUS STDCALL WvlWaitForActiveIrps(
    IN DEVICE_OBJECT * DeviceObject
  );

/**
 * Attach a device to a base device's device-stack
 *
 * @param DeviceObject
 *   The device to attach
 *
 * @param BaseDeviceObject
 *   The base device of the device-stack to attach to
 *
 * @retval TRUE
 * @retval FALSE
 *
 * A device cannot be attached to more than one device-stack
 */
extern WVL_M_LIB BOOLEAN STDCALL WvlAttachDeviceToDeviceStack(
    IN DEVICE_OBJECT * DeviceObject,
    IN DEVICE_OBJECT * BaseDeviceObject
  );

/**
 * Remove a device from a device-stack
 *
 * @param DeviceObject
 *   The device to remove from the stack
 *
 * The device must already be a member of a device-stack
 */
extern WVL_M_LIB VOID STDCALL WvlDetachDevice(
    IN OUT DEVICE_OBJECT * DeviceObject
  );

/**
 * Find the next-lower device in a device-stack
 *
 * @param DeviceObject
 *   The device above the device to find
 *
 * @return
 *   The next-lower device in the device-stack
 *
 * The device parameter needn't already be a member of a device-stack
 */
extern WVL_M_LIB DEVICE_OBJECT * STDCALL WvlGetLowerDeviceObject(
    IN DEVICE_OBJECT * dev_obj
  );

/**
 * Produce a DEVICE_RELATIONS object, possibly merging two others
 *
 * @param FixedRelations
 *   Optional.  If non-null, these relations will be listed in the output
 *
 * @param FreeableRelations
 *   Optional.  If non-null, these relations will be listed in the output
 *   and the FreeableRelations list will be freed
 *
 * @retval NULL - Failed to allocate the output list
 * @return
 *   A pointer to the output DEVICE_RELATIONS object, with zero or more
 *   DEVICE_OBJECT pointers in its list.  This object is allocated from
 *   paged memory and can be freed
 */
extern WVL_M_LIB DEVICE_RELATIONS * WvlMergeDeviceRelations(
    IN DEVICE_RELATIONS * FixedRelations,
    IN DEVICE_RELATIONS * FreeableRelations
  );

/**
 * Assign a device to a parent bus device
 *
 * @param DeviceObject
 *   The device to be assigned
 *
 * @param ParentBusDeviceObject
 *   Optional.  The parent bus device to be assigned-to.  If null, the
 *   device is unassigned from its previous parent bus, if any
 *
 * @retval FALSE - The device could not be assigned
 * @retval TRUE - The device has been assigned to the parent bus device
 *
 * This function has nothing to do with a bus device's response to a
 * BusRelations query, but it does increment a reference count on the bus.
 * That reference count must be decremented by unassigning the child
 * device before the bus device can be deleted
 */
extern WVL_M_LIB BOOLEAN STDCALL WvlAssignDeviceToBus(
    IN DEVICE_OBJECT * DeviceObject,
    IN DEVICE_OBJECT * ParentBusDeviceObject
  );

/**
 * Map or unmap low memory (the first 1 MiB)
 *
 * @param Pointer
 *   Optional.  If null, map low memory.  If non-null, unmap the low
 *   memory pointed to by this parameter
 *
 * @retval NULL - The memory could not be mapped or has been unmapped
 * @return
 *   If low memory was mapped, a pointer pointing to the mapped low memory
 */
extern WVL_M_LIB VOID * WvlMapUnmapLowMemory(VOID * Pointer);

/**
 * Miscellaneous: Grouped memory allocation functions.
 */

/* A group of memory allocations. */
typedef struct WVL_MEM_GROUP {
    PCHAR First;
    PCHAR Last;
    PCHAR Current;
  } WVL_S_MEM_GROUP, * WVL_SP_MEM_GROUP;
extern WVL_M_LIB VOID STDCALL WvlMemGroupInit(OUT WVL_SP_MEM_GROUP);
extern WVL_M_LIB PVOID STDCALL WvlMemGroupAlloc(
    IN OUT WVL_SP_MEM_GROUP,
    IN SIZE_T
  );
extern WVL_M_LIB VOID STDCALL WvlMemGroupFree(IN OUT WVL_SP_MEM_GROUP);
extern WVL_M_LIB PVOID STDCALL WvlMemGroupNextObj(IN OUT WVL_SP_MEM_GROUP);
extern WVL_M_LIB PVOID STDCALL WvlMemGroupBatchAlloc(
    IN OUT WVL_SP_MEM_GROUP,
    IN SIZE_T,
    IN UINT32
  );

/** Struct/union type definitions */

/** An atomic list */
struct S_WVL_LOCKED_LIST {
    LIST_ENTRY List[1];
    KEVENT Lock;
  };

/** Track resource usage */
struct S_WVL_RESOURCE_TRACKER {
    /** The usage count */
    LONG UsageCount;

    /** An event that is set when the usage count is zero */
    KEVENT ZeroUsage;
  };

/** A mini-driver */
struct S_WVL_MINI_DRIVER {
    /** Position in the list of registered mini-drivers */
    LIST_ENTRY Link[1];

    /** The mini-driver's driver object, provided by Windows */
    DRIVER_OBJECT * DriverObject;

    /** Probe and potentially drive a PDO with an FDO */
    DRIVER_ADD_DEVICE * AddDevice;

    /** Perform mini-driver cleanup */
    DRIVER_UNLOAD * Unload;

    /** Track mini-driver usage */
    S_WVL_RESOURCE_TRACKER Usage[1];
  };

/** The common part of the device extension for all mini-driver devices */
struct WV_DEV_EXT {
    /** Points to the old common device extension.  TODO: Rework */
    struct WV_DEV_T * device;

    /** The IRP dispatch function */
    DRIVER_DISPATCH * IrpDispatch;

    /** The device thread's signal for IRP arrival */
    KEVENT IrpArrival;

    /** A handle to the device thread */
    HANDLE ThreadHandle;

    /** The device's thread */
    ETHREAD * Thread;

    /**
     * The device's IRP queue.  Device must be acquired for inspection
     * and modification
     */
    LIST_ENTRY IrpQueue[1];

    /**
     * Prevents the device's thread from endlessly looping over IRPs that
     * keep re-adding themselves to the device's IRP queue.  Device must
     * be acquired for inspection and modification
     */
    LIST_ENTRY * SentinelIrpLink;

    /** Spin-lock for device acquisition */
    KSPIN_LOCK Lock;

    /** IRQL before the spin-lock for the device was acquired */
    KIRQL PreLockIrql;

    /**
     * Device flags of CvWvlDeviceFlagXxx enumeration values.  Must be
     * accessed atomically (such as with InterlockedXxx functions)
     */
    volatile LONG Flags;

    /** Active IRP count.  Must be accessed atomically */
    volatile LONG ActiveIrpCount;

    /** Event for when the active IRP count reaches 1 */
    KEVENT ActiveIrpCountOne;

    /** The device's mini-driver */
    S_WVL_MINI_DRIVER * MiniDriver;

    /** The device's lower object, if any (FDO) */
    DEVICE_OBJECT * LowerDeviceObject;

    /** The parent bus device object, if any (PDO) */
    DEVICE_OBJECT * ParentBusDeviceObject;

    /** The device usage */
    S_WVL_RESOURCE_TRACKER Usage[1];
  };

/** Wait for a status value to be set with this */
struct S_WVL_STATUS_WAITER {
    /** Signals when the status has been set */
    KEVENT Complete;

    /** The status returned */
    NTSTATUS Status;
  };

/** A pseudo-IRP device thread work item */
struct S_WVL_DEVICE_THREAD_WORK_ITEM {
    /**
     * The device thread function to be called when this work
     * item is processed
     */
    F_WVL_DEVICE_THREAD_FUNCTION * Function;

    /**
     * A dummy IRP that can be linked to the device's IRP queue.
     * (What a waste of space!)
     */
    IRP DummyIrp[1];
  };

/** Objects */
extern DRIVER_OBJECT * WvDriverObj;
extern UINT32 WvFindDisk;
extern KSPIN_LOCK WvFindDiskLock;
extern S_WVL_RESOURCE_TRACKER WvDriverUsage[1];
extern WVL_M_LIB BOOLEAN WvlCddbDone;

#endif	/* WV_M_DRIVER_H_ */
