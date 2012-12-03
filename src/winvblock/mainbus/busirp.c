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
 * Main bus IRP handling
 */

#include <stdio.h>
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
#include "x86.h"
#include "safehook.h"
#include "filedisk.h"
#include "dummy.h"
#include "memdisk.h"
#include "debug.h"

/** Object types - Must match mainbus.c */
typedef struct S_WV_MAIN_BUS S_WV_MAIN_BUS;

/** External functions */

/* From mainbus.c */
extern NTSTATUS WvMainBusInitialBusRelations(DEVICE_OBJECT * DeviceObject);
extern NTSTATUS STDCALL WvBusRemoveDev(IN WV_S_DEV_T * Device);

/** Public functions */
VOID WvMainBusBuildMajorDispatchTable(void);
DRIVER_DISPATCH WvBusIrpDispatch;
WVL_F_BUS_PNP WvBusPnpQueryDevText;

/** Private function declarations */

static NTSTATUS STDCALL WvBusDevCtlDetach(IN IRP * Irp);

/** IRP dispatchers */
static __drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH WvMainBusDispatchPnpIrp;
static __drv_dispatchType(IRP_MJ_DEVICE_CONTROL) DRIVER_DISPATCH
  WvMainBusDispatchDeviceControlIrp;
static __drv_dispatchType(IRP_MJ_POWER) DRIVER_DISPATCH
  WvMainBusDispatchPowerIrp;
static __drv_dispatchType(IRP_MJ_CREATE) __drv_dispatchType(IRP_MJ_CLOSE)
  DRIVER_DISPATCH WvMainBusDispatchCreateCloseIrp;
static __drv_dispatchType(IRP_MJ_SYSTEM_CONTROL) DRIVER_DISPATCH
  WvMainBusDispatchSystemControlIrp;

/** PnP handlers */
static __drv_dispatchType(IRP_MN_QUERY_BUS_INFORMATION) DRIVER_DISPATCH
  WvMainBusPnpQueryBusInfo;
static __drv_dispatchType(IRP_MN_QUERY_CAPABILITIES) DRIVER_DISPATCH
  WvMainBusPnpQueryCapabilities;
static __drv_dispatchType(IRP_MN_QUERY_DEVICE_RELATIONS) DRIVER_DISPATCH
  WvMainBusPnpQueryDeviceRelations;
static __drv_dispatchType(IRP_MN_REMOVE_DEVICE) DRIVER_DISPATCH
  WvMainBusPnpRemoveDevice;
static __drv_dispatchType(IRP_MN_START_DEVICE) DRIVER_DISPATCH
  WvMainBusPnpStartDevice;

static NTSTATUS STDCALL WvBusDevCtl(
    IN IRP * Irp,
    IN ULONG POINTER_ALIGNMENT Code
  );

/** Struct/union type definitions */

/** The bus extension type */
struct S_WV_MAIN_BUS {
    /** This must be the first member of all extension types */
    WV_S_DEV_EXT DeviceExtension[1];

    /** The lower device this FDO is attached to, if any */
    DEVICE_OBJECT * LowerDeviceObject;

    /** PnP bus information */
    PNP_BUS_INFORMATION PnpBusInfo[1];

    /** PnP bus relations */
    DEVICE_RELATIONS * BusRelations;

    /** Hack until proper PDO-add support is implemented */
    DEVICE_RELATIONS * BusRelationsHack;
  };

/** External objects */

/* From mainbus.c */
extern UNICODE_STRING WvBusDosName;
extern WVL_S_BUS_T WvBus;
extern WV_S_DEV_T WvBusDev;
extern volatile LONG WvMainBusCreators;

/** Private objects */
static A_WVL_MJ_DISPATCH_TABLE WvMainBusMajorDispatchTable;

/** Function definitions */

/** Build the main bus' major dispatch table */
VOID WvMainBusBuildMajorDispatchTable(void) {
    WvMainBusMajorDispatchTable[IRP_MJ_PNP] = WvMainBusDispatchPnpIrp;
    WvMainBusMajorDispatchTable[IRP_MJ_DEVICE_CONTROL] =
      WvMainBusDispatchDeviceControlIrp;
    WvMainBusMajorDispatchTable[IRP_MJ_POWER] = WvMainBusDispatchPowerIrp;
    WvMainBusMajorDispatchTable[IRP_MJ_CREATE] =
      WvMainBusDispatchCreateCloseIrp;
    WvMainBusMajorDispatchTable[IRP_MJ_CLOSE] =
      WvMainBusDispatchCreateCloseIrp;
    WvMainBusMajorDispatchTable[IRP_MJ_SYSTEM_CONTROL] =
      WvMainBusDispatchSystemControlIrp;
  }

/**
 * Handle an IRP from the main bus' thread context
 *
 * @param DeviceObject
 *   The main bus device
 *
 * @param Irp
 *   The IRP to process
 *
 * @return
 *   The status of the operation
 */
NTSTATUS STDCALL WvBusIrpDispatch(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    PIO_STACK_LOCATION io_stack_loc;

    ASSERT(dev_obj);
    ASSERT(irp);

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    /* Handle a known IRP major type */
    if (WvMainBusMajorDispatchTable[io_stack_loc->MajorFunction]) {
        return WvMainBusMajorDispatchTable[io_stack_loc->MajorFunction](
            dev_obj,
            irp
          );
      }

    /* Handle an unknown type */
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
  }

static NTSTATUS STDCALL WvBusDevCtlDetach(
    IN IRP * irp
  ) {
    UINT32 unit_num;
    WVL_SP_BUS_NODE walker;
    WV_SP_DEV_T dev = NULL;

    unit_num = *((PUINT32) irp->AssociatedIrp.SystemBuffer);
    DBG("Request to detach unit %d...\n", unit_num);

    walker = NULL;
    /* For each node on the bus... */
    WvlBusLock(&WvBus);
    while (walker = WvlBusGetNextNode(&WvBus, walker)) {
        /* If the unit number matches... */
        if (WvlBusGetNodeNum(walker) == unit_num) {
            dev = WvDevFromDevObj(WvlBusGetNodePdo(walker));
            /* If it's not a boot-time device... */
            if (dev->Boot) {
                DBG("Cannot detach a boot-time device.\n");
                /* Signal error. */
                dev = NULL;
              }
            break;
          }
      }
    WvlBusUnlock(&WvBus);
    if (!dev) {
        DBG("Unit %d not found.\n", unit_num);
        return WvlIrpComplete(irp, 0, STATUS_INVALID_PARAMETER);
      }
    /* Detach the node. */
    WvBusRemoveDev(dev);
    DBG("Removed unit %d.\n", unit_num);
    return WvlIrpComplete(irp, 0, STATUS_SUCCESS);
  }

static NTSTATUS STDCALL WvBusDevCtl(
    IN IRP * irp,
    IN ULONG POINTER_ALIGNMENT code
  ) {
    NTSTATUS status;

    switch (code) {
        case IOCTL_FILE_ATTACH:
          status = WvFilediskAttach(irp);
          break;

        case IOCTL_FILE_DETACH:
          return WvBusDevCtlDetach(irp);

        case IOCTL_WV_DUMMY:
          return WvDummyIoctl(irp);

        default:
          irp->IoStatus.Information = 0;
          status = STATUS_INVALID_DEVICE_REQUEST;
      }

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

NTSTATUS STDCALL WvBusPnpQueryDevText(
    IN WVL_SP_BUS_T bus,
    IN PIRP irp
  ) {
    WCHAR (*str)[512];
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;
    UINT32 str_len;

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
          str_len = swprintf(*str, WVL_M_WLIT L" Bus") + 1;
          irp->IoStatus.Information =
            (ULONG_PTR) wv_palloc(str_len * sizeof **str);
          if (irp->IoStatus.Information == 0) {
              DBG("wv_palloc DeviceTextDescription\n");
              status = STATUS_INSUFFICIENT_RESOURCES;
              goto alloc_info;
            }
          RtlCopyMemory(
              (PWCHAR) irp->IoStatus.Information,
              str,
              str_len * sizeof **str
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

    return WvlIrpComplete(irp, irp->IoStatus.Information, status);
  }

/** PnP IRP dispatcher */
static NTSTATUS WvMainBusDispatchPnpIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WV_MAIN_BUS * bus;
    IO_STACK_LOCATION * io_stack_loc;
    UCHAR code;
    NTSTATUS status;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    code = io_stack_loc->MinorFunction;

    switch (code) {
        case IRP_MN_QUERY_DEVICE_TEXT:
        DBG("IRP_MN_QUERY_DEVICE_TEXT\n");
        if (WvBus.QueryDevText)
          return WvBus.QueryDevText(&WvBus, irp);
        return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);

        case IRP_MN_QUERY_BUS_INFORMATION:
        DBG("IRP_MN_QUERY_BUS_INFORMATION\n");
        return WvMainBusPnpQueryBusInfo(dev_obj, irp);

        case IRP_MN_QUERY_DEVICE_RELATIONS:
        DBG("IRP_MN_QUERY_DEVICE_RELATIONS\n");
        return WvMainBusPnpQueryDeviceRelations(dev_obj, irp);

        case IRP_MN_QUERY_CAPABILITIES:
        DBG("IRP_MN_QUERY_CAPABILITIES\n");
        return WvMainBusPnpQueryCapabilities(dev_obj, irp);

        case IRP_MN_REMOVE_DEVICE:
        DBG("IRP_MN_REMOVE_DEVICE\n");
        return WvMainBusPnpRemoveDevice(dev_obj, irp);

        case IRP_MN_START_DEVICE:
        DBG("IRP_MN_START_DEVICE\n");
        return WvMainBusPnpStartDevice(dev_obj, irp);

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
        DBG("IRP_MN_QUERY_PNP_DEVICE_STATE\n");
        irp->IoStatus.Information = 0;
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_QUERY_STOP_DEVICE:
        DBG("IRP_MN_QUERY_STOP_DEVICE\n");
        WvBus.OldState = WvBus.State;
        WvBus.State = WvlBusStateStopPending;
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_CANCEL_STOP_DEVICE:
        DBG("IRP_MN_CANCEL_STOP_DEVICE\n");
        WvBus.State = WvBus.OldState;
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_STOP_DEVICE:
        DBG("IRP_MN_STOP_DEVICE\n");
        WvBus.OldState = WvBus.State;
        WvBus.State = WvlBusStateStopped;
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_QUERY_REMOVE_DEVICE:
        DBG("IRP_MN_QUERY_REMOVE_DEVICE\n");
        WvBus.OldState = WvBus.State;
        WvBus.State = WvlBusStateRemovePending;
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
        DBG("IRP_MN_CANCEL_REMOVE_DEVICE\n");
        WvBus.State = WvBus.OldState;
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_SURPRISE_REMOVAL:
        DBG("IRP_MN_SURPRISE_REMOVAL\n");
        WvBus.OldState = WvBus.State;
        WvBus.State = WvlBusStateSurpriseRemovePending;
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_QUERY_RESOURCES:
        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
        DBG("IRP_MN_QUERY_RESOURCE*\n");
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;

        default:
        DBG("Unhandled IRP_MN_*: %d\n", code);
        status = irp->IoStatus.Status;
      }

    irp->IoStatus.Status = status;
    IoSkipCurrentIrpStackLocation(irp);
    ASSERT(bus->LowerDeviceObject);
    return IoCallDriver(bus->LowerDeviceObject, irp);
  }

/**
 * IRP_MJ_PNP:IRP_MN_QUERY_BUS_INFORMATION handler
 *
 * IRQL == PASSIVE_LEVEL, any thread
 * Do not send this IRP
 * Completed by PDO
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *     Irp->IoStatus.Information populated from paged memory
 *   Error:
 *     Irp->IoStatus.Status == STATUS_INSUFFICIENT_RESOURCES
 *     Irp->IoStatus.Information == 0
 *
 * TODO: Not handled by function drivers, so what is it doing here?!
 */
static NTSTATUS STDCALL WvMainBusPnpQueryBusInfo(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WV_MAIN_BUS * bus;
    VOID * pnp_bus_info;
    NTSTATUS status;

    ASSERT(dev_obj);
    ASSERT(irp);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);

    /* Allocate for a copy of the bus info */
    pnp_bus_info = wv_palloc(sizeof bus->PnpBusInfo);
    if (!pnp_bus_info) {
        DBG("wv_palloc failed\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        irp->IoStatus.Information = 0;
        goto pnp_bus_info;
      }

    /* Copy the bus info */
    RtlCopyMemory(pnp_bus_info, bus->PnpBusInfo, sizeof bus->PnpBusInfo);

    irp->IoStatus.Information = (ULONG_PTR) pnp_bus_info;
    status = STATUS_SUCCESS;

    /* irp-IoStatus.Information (pnp_bus_info) not freed. */
    pnp_bus_info:

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

/**
 * IRP_MJ_PNP:IRP_MN_QUERY_CAPABILITIES handler
 *
 * IRQL == PASSIVE_LEVEL, any thread
 * Ok to send this IRP
 * Completed by PDO, can be hooked
 *
 * @param Parameters.DeviceCapabilities.Capabilities
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *     I/O stack location: Parameters.DeviceCapabilities.Capabilities
 *       populated
 *   Error:
 *     Irp->IoStatus.Status == STATUS_UNSUCCESSFUL
 */
static NTSTATUS STDCALL WvMainBusPnpQueryCapabilities(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WV_MAIN_BUS * bus;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(irp);

    /* Let the lower device handle the IRP */
    IoSkipCurrentIrpStackLocation(irp);
    ASSERT(bus->LowerDeviceObject);
    return IoCallDriver(bus->LowerDeviceObject, irp);
  }

/**
 * IRP_MJ_PNP:IRP_MN_QUERY_DEVICE_RELATIONS handler
 *
 * IRQL == PASSIVE_LEVEL
 * Completed by PDO, must accumulate relations downwards (upwards optional)
 * BusRelations: system thread
 *   Yes: FDO
 *   Maybe: filter
 * TargetDeviceRelation: any thread
 *   Yes: PDO
 * RemovalRelations: system thread
 *   Maybe: FDO
 *   Maybe: filter
 * PowerRelations >= Windows 7: system thread
 *   Maybe: FDO
 *   Maybe: filter
 * EjectionRelations: system thread
 *   Maybe: PDO
 *
 * @param Parameters.QueryDeviceRelations.Type (I/O stack location)
 *
 * @param FileObject (I/O stack location)
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *     Irp->IoStatus.Information populated from paged memory
 *   Error:
 *     Irp->IoStatus.Status == STATUS_INSUFFICIENT_RESOURCES
 */
static NTSTATUS STDCALL WvMainBusPnpQueryDeviceRelations(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WV_MAIN_BUS * bus;
    IO_STACK_LOCATION * io_stack_loc;
    NTSTATUS status;
    DEVICE_RELATIONS * dev_relations;
    DEVICE_RELATIONS * higher_dev_relations;
    DEVICE_RELATIONS * new_dev_relations;
    ULONG pdo_count;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    switch (io_stack_loc->Parameters.QueryDeviceRelations.Type) {
        case BusRelations:
        /* If we don't have any PDOs, trigger the probe */
        if (!bus->BusRelations) {
            status = WvMainBusInitialBusRelations(dev_obj);
            if (!NT_SUCCESS(status)) {
                irp->IoStatus.Status = status;
                IoCompleteRequest(irp, IO_NO_INCREMENT);
              }
          }
        dev_relations = bus->BusRelations;
        /*
         * TODO: The next line is a hack that needs to be removed
         * once proper PDO-add support is implemented.  This hack
         * causes WvMainBusInitialBusRelations to be called every
         * time
         */
        bus->BusRelations = NULL;
        break;

        case TargetDeviceRelation:
        case RemovalRelations:
        case EjectionRelations:
        default:
        DBG("Unsupported device relations query\n");
        IoSkipCurrentIrpStackLocation(irp);
        ASSERT(bus->LowerDeviceObject);
        return IoCallDriver(bus->LowerDeviceObject, irp);
      }

    pdo_count = dev_relations->Count;

    /* We need to include any higher driver's PDOs */
    higher_dev_relations = (VOID *) irp->IoStatus.Information;
    if (higher_dev_relations) {
        /* If we have nothing to add, pass the IRP down */
        if (!pdo_count) {
            IoSkipCurrentIrpStackLocation(irp);
            ASSERT(bus->LowerDeviceObject);
            return IoCallDriver(bus->LowerDeviceObject, irp);
            status = irp->IoStatus.Status;
            IoCompleteRequest(irp, IO_NO_INCREMENT);
            return status;
          }

        pdo_count += higher_dev_relations->Count;
      }

    /* Allocate a response */
    new_dev_relations = wv_palloc(
        sizeof *new_dev_relations +
        sizeof new_dev_relations->Objects[0] * (pdo_count - 1)
      );
    if (!new_dev_relations) {
        irp->IoStatus.Status = status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return status;
      }

    /* Populate the response... */
    new_dev_relations->Count = pdo_count;
    new_dev_relations->Objects[0] = NULL;

    /* ...with our PDOs... */
    if (dev_relations->Count) {
        RtlCopyMemory(
            new_dev_relations->Objects,
            dev_relations->Objects,
            sizeof new_dev_relations->Objects[0] * dev_relations->Count
          );
      }

    /* ...and with the higher driver's PDOs, if any */
    if (higher_dev_relations && higher_dev_relations->Count) {
        RtlCopyMemory(
            new_dev_relations->Objects + dev_relations->Count,
            higher_dev_relations->Objects,
            sizeof new_dev_relations->Objects[0] * higher_dev_relations->Count
          );
      }

    /* Free the higher driver's response, if any */
    wv_free(higher_dev_relations);

    /* Reference our PDOs.  Re-purposing pdo_count, here */
    for (pdo_count = 0; pdo_count < dev_relations->Count; ++pdo_count)
      ObReferenceObject(dev_relations->Objects[pdo_count]);

    /* Send the IRP down */
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = (ULONG_PTR) new_dev_relations;
    IoSkipCurrentIrpStackLocation(irp);
    ASSERT(bus->LowerDeviceObject);
    return IoCallDriver(bus->LowerDeviceObject, irp);
  }

/**
 * IRP_MJ_PNP:IRP_MN_REMOVE_DEVICE handler
 *
 * IRQL == PASSIVE_LEVEL, system thread
 * Completed by PDO
 * Do not send this IRP
 * Any child PDOs receive one of these IRPs, first, except possibly
 * if they've been surprise-removed
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 */
static NTSTATUS STDCALL WvMainBusPnpRemoveDevice(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WV_MAIN_BUS * bus;
    LIST_ENTRY * list;
    LIST_ENTRY * link;
    WVL_S_BUS_NODE * bus_node;
    NTSTATUS status;
    LONG c;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(irp);

    IoDeleteSymbolicLink(&WvBusDosName);

    /* Schedule deletion of this device when the thread finishes */
    WvlDeleteDevice(dev_obj);

    /* Delete all child PDOs */
    wv_free(bus->BusRelationsHack);
    list = &WvBus.BusPrivate_.Nodes;
    while ((link = RemoveHeadList(list)) != list) {
        bus_node = CONTAINING_RECORD(link, WVL_S_BUS_NODE, BusPrivate_.Link);
        ASSERT(bus_node);

        bus_node->Linked = FALSE;
        DBG(
            "Removing child PDO %p from bus FDO %p\n",
            (VOID *) bus_node->BusPrivate_.Pdo,
            (VOID *) dev_obj
          );
        ObDereferenceObject(bus_node->BusPrivate_.Pdo);
        WvBus.BusPrivate_.NodeCount--;
      }

    /* Send the IRP down */
    IoSkipCurrentIrpStackLocation(irp);
    ASSERT(bus->LowerDeviceObject);
    status = IoCallDriver(bus->LowerDeviceObject, irp);

    /* Detach FDO from PDO */
    IoDetachDevice(bus->LowerDeviceObject);
    WvBus.LowerDeviceObject = bus->LowerDeviceObject = NULL;
    WvBus.Pdo = NULL;

    /*
     * TODO: Put this somewhere where it doesn't race with another
     * thread calling WvMainBusDriveDevice
     */
    c = InterlockedDecrement(&WvMainBusCreators);
    ASSERT(c >= 0);

    return status;
  }

/**
 * IRP_MJ_PNP:IRP_MN_START_DEVICE handler
 *
 * IRQL == PASSIVE_LEVEL, system thread
 * Completed by PDO first, then caught by higher drivers
 * Do not send this IRP
 *
 * @param Parameters.StartDevice.AllocatedResources (I/O stack location)
 *
 * @param Parameters.StartDevice.AllocatedResourcesTranslated
 *   (I/O stack location)
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *     Irp->IoStatus.Status == STATUS_PENDING
 */
static NTSTATUS STDCALL WvMainBusPnpStartDevice(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WV_MAIN_BUS * bus;
    NTSTATUS status;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(irp);

    /* Send the IRP down and wait for completion by the lower driver */
    status = WvlWaitForIrpCompletion(bus->LowerDeviceObject, irp);
    if (!NT_SUCCESS(status)) {
        DBG(
            "Lower device %p failed to start\n",
            (VOID *) bus->LowerDeviceObject
          );
        goto err_lower;
      }

    WvBus.OldState = WvBus.State;
    WvBus.State = WvlBusStateStarted;

    irp->IoStatus.Status = STATUS_SUCCESS;

    err_lower:

    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

static NTSTATUS WvMainBusDispatchDeviceControlIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    IO_STACK_LOCATION * io_stack_loc;
    ULONG POINTER_ALIGNMENT code;

    ASSERT(dev_obj);
    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);
    code = io_stack_loc->Parameters.DeviceIoControl.IoControlCode;
    return WvBusDevCtl(irp, code);
  }

static NTSTATUS WvMainBusDispatchPowerIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    ASSERT(dev_obj);
    ASSERT(irp);
    return WvlIrpPassPowerToLower(WvBus.LowerDeviceObject, irp);
  }

static NTSTATUS WvMainBusDispatchCreateCloseIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    ASSERT(dev_obj);
    ASSERT(irp);
    /* Always succeed with nothing to do */
    /* TODO: Track resource usage */
    return WvlIrpComplete(irp, 0, STATUS_SUCCESS);
  }

static NTSTATUS WvMainBusDispatchSystemControlIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    ASSERT(dev_obj);
    ASSERT(irp);
    return WvlIrpPassToLower(WvBus.LowerDeviceObject, irp);
  }
