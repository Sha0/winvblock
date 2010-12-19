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
 * Bus specifics.
 */

#include <ntddk.h>

#include "winvblock.h"
#include "wv_stdlib.h"
#include "portable.h"
#include "driver.h"
#include "device.h"
#include "bus.h"
#include "debug.h"

/* IRP_MJ_DEVICE_CONTROL dispatcher from bus/dev_ctl.c */
extern device__dev_ctl_func WvBusDevCtlDispatch;
/* IRP_MJ_PNP dispatcher from bus/pnp.c */
extern device__pnp_func WvBusPnpDispatch;

/* Types. */
typedef enum WV_BUS_WORK_ITEM_CMD_ {
    WvBusWorkItemCmdAddPdo_,
    WvBusWorkItemCmdRemovePdo_,
    WvBusWorkItemCmds_
  } WV_E_BUS_WORK_ITEM_CMD_, * WV_EP_BUS_WORK_ITEM_CMD_;

typedef struct WV_BUS_WORK_ITEM_ {
    LIST_ENTRY Link;
    WV_E_BUS_WORK_ITEM_CMD_ Cmd;
    union {
        WV_SP_BUS_NODE Node;
      } Context;
  } WV_S_BUS_WORK_ITEM_, * WV_SP_BUS_WORK_ITEM_;

/* Forward declarations. */
static device__free_func WvBusFree_;
static device__create_pdo_func WvBusCreatePdo_;
static device__dispatch_func WvBusPower_;
static device__dispatch_func WvBusSysCtl_;
static WV_F_BUS_THREAD WvBusDefaultThread_;
static winvblock__bool WvBusAddWorkItem_(
    WV_SP_BUS_T,
    WV_SP_BUS_WORK_ITEM_
  );
static WV_SP_BUS_WORK_ITEM_ WvBusGetWorkItem_(WV_SP_BUS_T);

/* Globals. */
struct device__irp_mj WvBusIrpMj_ = {
    WvBusPower_,
    WvBusSysCtl_,
    WvBusDevCtlDispatch,
    (device__scsi_func *) 0,
    WvBusPnpDispatch,
  };

/**
 * Add a child node to the bus.
 *
 * @v bus_ptr           Points to the bus receiving the child.
 * @v dev_ptr           Points to the child device to add.
 * @ret                 TRUE for success, FALSE for failure.
 */
winvblock__lib_func winvblock__bool STDCALL WvBusAddChild(
    IN OUT WV_SP_BUS_T bus_ptr,
    IN OUT struct device__type * dev_ptr
  ) {
    /* The new node's device object. */
    PDEVICE_OBJECT dev_obj_ptr;
    /* Walks the child nodes. */
    struct device__type * walker;
    winvblock__uint32 dev_num;

    DBG("Entry\n");
    if ((bus_ptr == NULL) || (dev_ptr == NULL)) {
        DBG("No bus or no device!\n");
        return FALSE;
      }
    /* Create the child device. */
    dev_obj_ptr = device__create_pdo(dev_ptr);
    if (dev_obj_ptr == NULL) {
        DBG("PDO creation failed!\n");
        device__free(dev_ptr);
        return FALSE;
      }

    dev_ptr->Parent = bus_ptr->Dev->Self;
    /*
     * Initialize the device.  For disks, this routine is responsible for
     * determining the disk's geometry appropriately for AoE/RAM/file disks.
     */
    dev_ptr->ops.init(dev_ptr);
    dev_obj_ptr->Flags &= ~DO_DEVICE_INITIALIZING;
    /* Add the new device's extension to the bus' list of children. */
    dev_num = 0;
    if (bus_ptr->first_child == NULL) {
        bus_ptr->first_child = dev_ptr;
      } else {
        walker = bus_ptr->first_child;
        /* If the first child device number isn't 0... */
        if (walker->dev_num) {
            /* We insert before. */
            dev_ptr->next_sibling_ptr = walker;
            bus_ptr->first_child = dev_ptr;
          } else {
            while (walker->next_sibling_ptr != NULL) {
                /* If there's a gap in the device numbers for the bus... */
                if (walker->dev_num < walker->next_sibling_ptr->dev_num - 1) {
                    /* Insert here, instead of at the end. */
                    dev_num = walker->dev_num + 1;
                    dev_ptr->next_sibling_ptr = walker->next_sibling_ptr;
                    walker->next_sibling_ptr = dev_ptr;
                    break;
                  }
                walker = walker->next_sibling_ptr;
                dev_num = walker->dev_num + 1;
              }
            /* If we haven't already inserted the device... */
            if (!dev_ptr->next_sibling_ptr) {
                walker->next_sibling_ptr = dev_ptr;
                dev_num = walker->dev_num + 1;
              }
          }
      }
    dev_ptr->dev_num = dev_num;
    bus_ptr->Children++;
    if (bus_ptr->PhysicalDeviceObject != NULL) {
        IoInvalidateDeviceRelations(
            bus_ptr->PhysicalDeviceObject,
            BusRelations
          );
      }
    DBG("Exit\n");
    return TRUE;
  }

static NTSTATUS STDCALL WvBusSysCtl_(
    IN struct device__type * dev,
    IN PIRP irp
  ) {
    WV_SP_BUS_T bus = WvBusFromDev(dev);
    PDEVICE_OBJECT lower = bus->LowerDeviceObject;

    if (lower) {
        DBG("Passing IRP_MJ_SYSTEM_CONTROL down\n");
        IoSkipCurrentIrpStackLocation(irp);
        return IoCallDriver(lower, irp);
      }
    return driver__complete_irp(irp, 0, STATUS_SUCCESS);
  }

static NTSTATUS STDCALL WvBusPower_(
    IN struct device__type * dev,
    IN PIRP irp
  ) {
    WV_SP_BUS_T bus = WvBusFromDev(dev);
    PDEVICE_OBJECT lower = bus->LowerDeviceObject;

    PoStartNextPowerIrp(irp);
    if (lower) {
        IoSkipCurrentIrpStackLocation(irp);
        return PoCallDriver(lower, irp);
      }
    return driver__complete_irp(irp, 0, STATUS_SUCCESS);
  }

NTSTATUS STDCALL WvBusGetDevCapabilities(
    IN PDEVICE_OBJECT DeviceObject,
    IN PDEVICE_CAPABILITIES DeviceCapabilities
  ) {
    IO_STATUS_BLOCK ioStatus;
    KEVENT pnpEvent;
    NTSTATUS status;
    PDEVICE_OBJECT targetObject;
    PIO_STACK_LOCATION irpStack;
    PIRP pnpIrp;

    RtlZeroMemory(DeviceCapabilities, sizeof (DEVICE_CAPABILITIES));
    DeviceCapabilities->Size = sizeof (DEVICE_CAPABILITIES);
    DeviceCapabilities->Version = 1;
    DeviceCapabilities->Address = -1;
    DeviceCapabilities->UINumber = -1;

    KeInitializeEvent(&pnpEvent, NotificationEvent, FALSE);
    targetObject = IoGetAttachedDeviceReference(DeviceObject);
    pnpIrp = IoBuildSynchronousFsdRequest(
        IRP_MJ_PNP,
        targetObject,
        NULL,
        0,
        NULL,
        &pnpEvent,
        &ioStatus
      );
    if (pnpIrp == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
      } else {
        pnpIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        irpStack = IoGetNextIrpStackLocation(pnpIrp);
        RtlZeroMemory(irpStack, sizeof (IO_STACK_LOCATION));
        irpStack->MajorFunction = IRP_MJ_PNP;
        irpStack->MinorFunction = IRP_MN_QUERY_CAPABILITIES;
        irpStack->Parameters.DeviceCapabilities.Capabilities =
          DeviceCapabilities;
        status = IoCallDriver(targetObject, pnpIrp);
        if (status == STATUS_PENDING) {
            KeWaitForSingleObject(
                &pnpEvent,
                Executive,
                KernelMode,
                FALSE,
                NULL
              );
            status = ioStatus.Status;
          }
      }
    ObDereferenceObject(targetObject);
    return status;
  }

/* Initialize a bus. */
static winvblock__bool STDCALL bus__init_(IN struct device__type * dev) {
    return TRUE;
  }

/**
 * Initialize bus defaults.
 *
 * @v bus               Points to the bus to initialize with defaults.
 */
winvblock__lib_func void WvBusInit(WV_SP_BUS_T bus) {
    struct device__type * dev = bus->Dev;

    RtlZeroMemory(bus, sizeof *bus);
    /* Populate non-zero bus device defaults. */
    bus->Dev = dev;
    bus->BusPrivate_.PrevFree = dev->ops.free;
    bus->Thread = WvBusDefaultThread_;
    KeInitializeSpinLock(&bus->BusPrivate_.WorkItemsLock);
    InitializeListHead(&bus->BusPrivate_.WorkItems);
    KeInitializeEvent(&bus->ThreadSignal, SynchronizationEvent, FALSE);
    dev->ops.create_pdo = WvBusCreatePdo_;
    dev->ops.init = bus__init_;
    dev->ops.free = WvBusFree_;
    dev->ext = bus;
    dev->irp_mj = &WvBusIrpMj_;
    dev->IsBus = TRUE;
  }

/**
 * Create a new bus.
 *
 * @ret bus_ptr         The address of a new bus, or NULL for failure.
 *
 * This function should not be confused with a PDO creation routine, which is
 * actually implemented for each device type.  This routine will allocate a
 * WV_S_BUS_T, track it in a global list, as well as populate the bus
 * with default values.
 */
winvblock__lib_func WV_SP_BUS_T WvBusCreate(void) {
    struct device__type * dev;
    WV_SP_BUS_T bus;

    /* Try to create a device. */
    dev = device__create();
    if (dev == NULL)
      goto err_no_dev;
    /*
     * Bus devices might be used for booting and should
     * not be allocated from a paged memory pool.
     */
    bus = wv_malloc(sizeof *bus);
    if (bus == NULL)
      goto err_no_bus;

    bus->Dev = dev;
    WvBusInit(bus);
    return bus;

    err_no_bus:

    device__free(dev);
    err_no_dev:

    return NULL;
  }

/**
 * Create a bus PDO.
 *
 * @v dev               Populate PDO dev. ext. space from these details.
 * @ret pdo             Points to the new PDO, or is NULL upon failure.
 *
 * Returns a Physical Device Object pointer on success, NULL for failure.
 */
static PDEVICE_OBJECT STDCALL WvBusCreatePdo_(IN struct device__type * dev) {
    PDEVICE_OBJECT pdo = NULL;
    WV_SP_BUS_T bus;
    NTSTATUS status;

    /* Note the bus device needing a PDO. */
    if (dev == NULL) {
        DBG("No device passed\n");
        return NULL;
      }
    bus = WvBusFromDev(dev);
    /* Create the PDO. */
    status = IoCreateDevice(
        dev->DriverObject,
        sizeof (driver__dev_ext),
        NULL,
        FILE_DEVICE_CONTROLLER,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &pdo
      );
    if (pdo == NULL) {
        DBG("IoCreateDevice() failed!\n");
        goto err_pdo;
      }

    /* Set associations for the bus, device, PDO. */
    device__set(pdo, dev);
    dev->Self = bus->PhysicalDeviceObject = pdo;

    /* Set some DEVICE_OBJECT status. */
    pdo->Flags |= DO_DIRECT_IO;         /* FIXME? */
    pdo->Flags |= DO_POWER_INRUSH;      /* FIXME? */
    pdo->Flags &= ~DO_DEVICE_INITIALIZING;
    #ifdef RIS
    dev->State = Started;
    #endif

    return pdo;

    IoDeleteDevice(pdo);
    err_pdo:

    return NULL;
  }

/**
 * Default bus deletion operation.
 *
 * @v dev_ptr           Points to the bus device to delete.
 */
static void STDCALL WvBusFree_(IN struct device__type * dev_ptr) {
    WV_SP_BUS_T bus_ptr = WvBusFromDev(dev_ptr);
    /* Free the "inherited class". */
    bus_ptr->BusPrivate_.PrevFree(dev_ptr);

    wv_free(bus_ptr);
  }

/**
 * Get a bus from a device.
 *
 * @v dev       A pointer to a device.
 * @ret         A pointer to the device's associated bus.
 */
extern winvblock__lib_func WV_SP_BUS_T WvBusFromDev(
    struct device__type * dev
  ) {
    return dev->ext;
  }

/**
 * Add a work item for a bus to process.
 *
 * @v bus                       The bus to process the work item.
 * @v work_item                 The work item to add.
 * @ret winvblock__bool         TRUE if added, else FALSE
 *
 * Note that this function will initialize the work item's completion signal.
 */
static winvblock__bool WvBusAddWorkItem_(
    WV_SP_BUS_T bus,
    WV_SP_BUS_WORK_ITEM_ work_item
  ) {
    ExInterlockedInsertTailList(
        &bus->BusPrivate_.WorkItems,
        &work_item->Link,
        &bus->BusPrivate_.WorkItemsLock
      );

    return TRUE;
  }

/**
 * Get (and dequeue) a work item from a bus' queue.
 *
 * @v bus                       The bus processing the work item.
 * @ret bus__work_item_         The work item, or NULL for an empty queue.
 */
static WV_SP_BUS_WORK_ITEM_ WvBusGetWorkItem_(
    WV_SP_BUS_T bus
  ) {
    PLIST_ENTRY list_entry;

    list_entry = ExInterlockedRemoveHeadList(
        &bus->BusPrivate_.WorkItems,
        &bus->BusPrivate_.WorkItemsLock
      );
    if (!list_entry)
      return NULL;

    return CONTAINING_RECORD(list_entry, WV_S_BUS_WORK_ITEM_, Link);
  }

/**
 * Process work items for a bus.
 *
 * @v bus               The bus to process its work items.
 */
winvblock__lib_func void WvBusProcessWorkItems(WV_SP_BUS_T bus) {
    WV_SP_BUS_WORK_ITEM_ work_item;
    WV_SP_BUS_NODE node;

    while (work_item = WvBusGetWorkItem_(bus)) {
        switch (work_item->Cmd) {
            case WvBusWorkItemCmdAddPdo_:
              DBG("Adding PDO to bus...\n");

              node = work_item->Context.Node;
              /* It's too bad about having both linked list and bus ref. */
              node->BusPrivate_.Bus = bus;
              ObReferenceObject(node->BusPrivate_.Pdo);
              InsertTailList(&bus->BusPrivate_.Nodes, &node->BusPrivate_.Link);
              bus->BusPrivate_.NodeCount++;
              break;

            case WvBusWorkItemCmdRemovePdo_:
              DBG("Removing PDO from bus...\n");

              node = work_item->Context.Node;
              RemoveEntryList(&node->BusPrivate_.Link);
              ObDereferenceObject(node->BusPrivate_.Pdo);
              bus->BusPrivate_.NodeCount--;
              break;

            default:
              DBG("Unknown work item type!\n");
          }
        wv_free(work_item);
      }
    return;
  }

/**
 * Cancel pending work items for a bus.
 *
 * @v bus       The bus to cancel pending work items for.
 */
winvblock__lib_func void WvBusCancelWorkItems(WV_SP_BUS_T bus) {
    WV_SP_BUS_WORK_ITEM_ work_item;

    DBG("Canceling work items.\n");
    while (work_item = WvBusGetWorkItem_(bus))
      wv_free(work_item);
    return;
  }

/* The device__type::ops.free implementation for a threaded bus. */
static void STDCALL bus__thread_free_(IN struct device__type * dev) {
    WV_SP_BUS_T bus = WvBusFromDev(dev);

    bus->Stop = TRUE;
    KeSetEvent(&bus->ThreadSignal, 0, FALSE);
    return;
  }

/**
 * The bus thread wrapper.
 *
 * @v context           The thread context.  In our case, it points to
 *                      the bus that the thread should use in processing.
 */
static void STDCALL bus__thread_(IN void * context) {
    WV_SP_BUS_T bus = context;

    if (!bus || !bus->Thread) {
        DBG("No bus or no thread!\n");
        return;
      }

    bus->Thread(bus);
    return;
  }

/**
 * The default bus thread routine.
 *
 * @v bus       Points to the bus device for the thread to work with.
 *
 * Note that if you implement your own bus type using this library,
 * you can override the thread routine with your own.  If you do so,
 * your thread routine should call WvBusProcessWorkItems() within
 * its loop.  To start a bus thread, use WvBusStartThread()
 * If you implement your own thread routine, you are also responsible
 * for calling WvBusCancelWorkItems() and freeing the bus.
 */
static void STDCALL WvBusDefaultThread_(IN WV_SP_BUS_T bus) {
    LARGE_INTEGER timeout;

    /* Wake up at least every 30 seconds. */
    timeout.QuadPart = -300000000LL;

    /* Hook device__type::ops.free() */
    bus->Dev->ops.free = bus__thread_free_;

    /* When bus::Stop is set, we shut down. */
    while (bus->Stop) {
        DBG("Alive.\n");

        /* Wait for the work signal or the timeout. */
        KeWaitForSingleObject(
            &bus->ThreadSignal,
            Executive,
            KernelMode,
            FALSE,
            &timeout
          );
        /* Reset the work signal. */
        KeResetEvent(&bus->ThreadSignal);

        WvBusProcessWorkItems(bus);
      } /* while bus->alive */

    WvBusCancelWorkItems(bus);
    WvBusFree_(bus->Dev);
    return;
  }

/**
 * Start a bus thread.
 *
 * @v bus               The bus to start a thread for.
 * @ret NTSTATUS        The status of the thread creation operation.
 *
 * Also see WV_F_BUS_THREAD in the header for details about the prototype
 * for implementing your own bus thread routine.  You set bus::Thread to
 * specify your own thread routine, then call this function to start it.
 */
winvblock__lib_func NTSTATUS WvBusStartThread(
    WV_SP_BUS_T bus
  ) {
    OBJECT_ATTRIBUTES obj_attrs;
    HANDLE thread_handle;

    if (!bus) {
        DBG("No bus specified!\n");
        return STATUS_INVALID_PARAMETER;
      }

    InitializeObjectAttributes(
        &obj_attrs,
        NULL,
        OBJ_KERNEL_HANDLE,
        NULL,
        NULL
      );
    return PsCreateSystemThread(
        &thread_handle,
        THREAD_ALL_ACCESS,
        &obj_attrs,
        NULL,
        NULL,
        bus__thread_,
        bus
      );
  }

/**
 * Initialize a bus node with an associated PDO.
 *
 * @v Node              The node to initialize.
 * @v Pdo               The PDO to associate the node with.
 * @ret winvblock__bool FALSE for a NULL argument, otherwise TRUE
 */
winvblock__lib_func winvblock__bool STDCALL WvBusInitNode(
    OUT WV_SP_BUS_NODE Node,
    IN PDEVICE_OBJECT Pdo
  ) {
    if (!Node || !Pdo)
      return FALSE;

    RtlZeroMemory(Node, sizeof *Node);
    Node->BusPrivate_.Pdo = Pdo;
    return TRUE;
  }

/**
 * Add a PDO node to a bus' list of children.
 *
 * @v Bus               The bus to add the node to.
 * @v Node              The PDO node to add to the bus.
 * @ret NTSTATUS        The status of the operation.
 *
 * Do not attempt to add the same node to more than one bus.
 * When WvBusProcessWorkItems() is called for the bus, the
 * node will be added.  This is usually from the bus' thread.
 */
winvblock__lib_func NTSTATUS STDCALL WvBusAddNode(
    WV_SP_BUS_T Bus,
    WV_SP_BUS_NODE Node
  ) {
    WV_SP_BUS_WORK_ITEM_ work_item;

    if (
        !Bus ||
        !Node ||
        Bus->Dev->Self->DriverObject != Node->BusPrivate_.Pdo->DriverObject
      )
      return STATUS_INVALID_PARAMETER;

    if (Bus->Stop)
      return STATUS_NO_SUCH_DEVICE;

    if (!(work_item = wv_malloc(sizeof *work_item)))
      return STATUS_INSUFFICIENT_RESOURCES;

    work_item->Cmd = WvBusWorkItemCmdAddPdo_;
    work_item->Context.Node = Node;
    if (!WvBusAddWorkItem_(Bus, work_item)) {
        wv_free(work_item);
        return STATUS_UNSUCCESSFUL;
      }
    /* Fire and forget. */
    KeSetEvent(&Bus->ThreadSignal, 0, FALSE);
    return STATUS_SUCCESS;
  }

/**
 * Remove a PDO node from a bus.
 *
 * @v Node              The PDO node to remove from its parent bus.
 * @ret NTSTATUS        The status of the operation.
 *
 * When WvBusProcessWorkItems() is called for the bus, it will
 * then remove the node.  This is usually from the bus' thread.
 */
winvblock__lib_func NTSTATUS STDCALL WvBusRemoveNode(
    WV_SP_BUS_NODE Node
  ) {
    WV_SP_BUS_T bus;
    WV_SP_BUS_WORK_ITEM_ work_item;

    if (!Node || !(bus = Node->BusPrivate_.Bus))
      return STATUS_INVALID_PARAMETER;

    if (bus->Stop)
      return STATUS_NO_SUCH_DEVICE;

    if (!(work_item = wv_malloc(sizeof *work_item)))
      return STATUS_INSUFFICIENT_RESOURCES;

    work_item->Cmd = WvBusWorkItemCmdRemovePdo_;
    work_item->Context.Node = Node;
    if (!WvBusAddWorkItem_(bus, work_item)) {
        wv_free(work_item);
        return STATUS_UNSUCCESSFUL;
      }
    /* Fire and forget. */
    KeSetEvent(&bus->ThreadSignal, 0, FALSE);
    return STATUS_SUCCESS;
  }
