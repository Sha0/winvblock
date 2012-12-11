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

#include "mainbus.h"

/** Function declarations */

/** IRP dispatchers */
DRIVER_DISPATCH WvMainBusIrpDispatch;

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
static __drv_dispatchType(IRP_MN_QUERY_DEVICE_TEXT) DRIVER_DISPATCH
  WvMainBusPnpQueryDeviceText;
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

/** Device control handlers */
static __drv_dispatchType(IRP_MJ_DEVICE_CONTROL) DRIVER_DISPATCH
  WvMainBusDeviceControlDetach;

/** Objects */
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
 * Handle an IRP for the main bus
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
NTSTATUS STDCALL WvMainBusIrpDispatch(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WV_MAIN_BUS * bus;
    PIO_STACK_LOCATION io_stack_loc;
    LONG flags;
    BOOLEAN do_dispatch;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(irp);

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    /* Check if non-PnP IRPs are being held */
    flags = InterlockedOr(&bus->Flags, 0);
    do_dispatch = (
        !(flags & CvWvMainBusFlagIrpsHeld) ||
        io_stack_loc->MajorFunction == IRP_MJ_PNP
      );
    if (!do_dispatch)
      return WvlAddIrpToDeviceQueue(dev_obj, irp, FALSE);

    /* Handle a known IRP major type */
    if (WvMainBusMajorDispatchTable[io_stack_loc->MajorFunction]) {
        return WvMainBusMajorDispatchTable[io_stack_loc->MajorFunction](
            dev_obj,
            irp
          );
      }

    /* Handle an unknown type */
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return STATUS_NOT_SUPPORTED;
  }

/** PnP IRP dispatcher */
static NTSTATUS STDCALL WvMainBusDispatchPnpIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WV_MAIN_BUS * bus;
    IO_STACK_LOCATION * io_stack_loc;
    UCHAR code;
    LONG flags;
    NTSTATUS status;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    /*
     * All PnP IRPs will have exclusive access to the device.  This
     * simplifies things
     */
    if (!WvlInDeviceThread(dev_obj))
      return WvlAddIrpToDeviceQueue(dev_obj, irp, TRUE);

    status = WvlWaitForActiveIrps(dev_obj);
    if (status == STATUS_NO_SUCH_DEVICE) {
        /* The IRP will have been completed for us */
        return status;
      }
    ASSERT(NT_SUCCESS(status));

    code = io_stack_loc->MinorFunction;

    switch (code) {
        case IRP_MN_QUERY_DEVICE_TEXT:
        DBG("IRP_MN_QUERY_DEVICE_TEXT\n");
        status = WvMainBusPnpQueryDeviceText(dev_obj, irp);
        goto completed;

        case IRP_MN_QUERY_BUS_INFORMATION:
        DBG("IRP_MN_QUERY_BUS_INFORMATION\n");
        status = WvMainBusPnpQueryBusInfo(dev_obj, irp);
        goto completed;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
        DBG("IRP_MN_QUERY_DEVICE_RELATIONS\n");
        status = WvMainBusPnpQueryDeviceRelations(dev_obj, irp);
        goto completed;

        case IRP_MN_QUERY_CAPABILITIES:
        DBG("IRP_MN_QUERY_CAPABILITIES\n");
        status = WvMainBusPnpQueryCapabilities(dev_obj, irp);
        goto completed;

        case IRP_MN_REMOVE_DEVICE:
        DBG("IRP_MN_REMOVE_DEVICE\n");
        flags = InterlockedOr(&bus->Flags, CvWvMainBusFlagIrpsHeld);
        status = WvMainBusPnpRemoveDevice(dev_obj, irp);
        goto completed;

        case IRP_MN_START_DEVICE:
        DBG("IRP_MN_START_DEVICE\n");
        status = WvMainBusPnpStartDevice(dev_obj, irp);
        flags = InterlockedAnd(&bus->Flags, ~CvWvMainBusFlagIrpsHeld);
        goto completed;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
        DBG("IRP_MN_QUERY_PNP_DEVICE_STATE\n");
        irp->IoStatus.Information = 0;
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_QUERY_STOP_DEVICE:
        DBG("IRP_MN_QUERY_STOP_DEVICE\n");
        WvBus.OldState = WvBus.State;
        WvBus.State = WvlBusStateStopPending;
        flags = InterlockedOr(&bus->Flags, CvWvMainBusFlagIrpsHeld);
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_CANCEL_STOP_DEVICE:
        DBG("IRP_MN_CANCEL_STOP_DEVICE\n");
        WvBus.State = WvBus.OldState;
        flags = InterlockedAnd(&bus->Flags, ~CvWvMainBusFlagIrpsHeld);
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_STOP_DEVICE:
        DBG("IRP_MN_STOP_DEVICE\n");
        WvBus.OldState = WvBus.State;
        WvBus.State = WvlBusStateStopped;
        flags = InterlockedOr(&bus->Flags, CvWvMainBusFlagIrpsHeld);
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_QUERY_REMOVE_DEVICE:
        DBG("IRP_MN_QUERY_REMOVE_DEVICE\n");
        WvBus.OldState = WvBus.State;
        WvBus.State = WvlBusStateRemovePending;
        flags = InterlockedOr(&bus->Flags, CvWvMainBusFlagIrpsHeld);
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
        DBG("IRP_MN_CANCEL_REMOVE_DEVICE\n");
        WvBus.State = WvBus.OldState;
        flags = InterlockedAnd(&bus->Flags, ~CvWvMainBusFlagIrpsHeld);
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_SURPRISE_REMOVAL:
        DBG("IRP_MN_SURPRISE_REMOVAL\n");
        WvBus.OldState = WvBus.State;
        WvBus.State = WvlBusStateSurpriseRemovePending;
        flags = InterlockedOr(&bus->Flags, CvWvMainBusFlagIrpsHeld);
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_QUERY_RESOURCES:
        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
        DBG("IRP_MN_QUERY_RESOURCE*\n");
        WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;

        default:
        DBG("Unhandled IRP_MN_*: %u\n", code);
        status = irp->IoStatus.Status;
      }

    irp->IoStatus.Status = status;
    return WvlPassIrpDown(dev_obj, irp);

    completed:
    return status;
  }

/**
 * IRP_MJ_PNP:IRP_MN_QUERY_DEVICE_TEXT handler
 *
 * IRQL == PASSIVE_LEVEL, any thread
 * Do not send this IRP
 * Completed by PDO under normal circumstances, but not here
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *     Irp->IoStatus.Information populated from paged memory
 *   Error:
 *     Irp->IoStatus.Status == STATUS_INSUFFICIENT_RESOURCES
 *     Irp->IoStatus.Information == 0
 *
 * Not supposed to be handled by function drivers, but we need to filter
 * it to override the PnP Manager's root-enumerated device
 */
static NTSTATUS STDCALL WvMainBusPnpQueryDeviceText(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    static const WCHAR dev_text_desc[] = WVL_M_WLIT L" Bus";
    IO_STACK_LOCATION * io_stack_loc;
    WCHAR * response;
    NTSTATUS status;

    ASSERT(dev_obj);
    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    /* Determine the query type */
    switch (io_stack_loc->Parameters.QueryDeviceText.DeviceTextType) {
        case DeviceTextDescription:

        /* Allocate the response */
        response = wv_pallocz(sizeof dev_text_desc);
        if (!response) {
            DBG("Unable to allocate DeviceTextDescription\n");
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto err_response;
          }
            
        /* Populate the response */
        RtlCopyMemory(response, dev_text_desc, sizeof dev_text_desc);
        irp->IoStatus.Information = (ULONG_PTR) response;
        status = STATUS_SUCCESS;

        /* response not freed */
        err_response:

        break;

        default:
        irp->IoStatus.Information = 0;
        status = STATUS_NOT_SUPPORTED;
      }

    irp->IoStatus.Status = status;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
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
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
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
    ASSERT(dev_obj);
    ASSERT(irp);

    /* Let the lower device handle the IRP */
    return WvlPassIrpDown(dev_obj, irp);
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
    DEVICE_RELATIONS * response;
    ULONG i;

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
                WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
              }
          }
        dev_relations = bus->BusRelations;
        break;

        case TargetDeviceRelation:
        case RemovalRelations:
        case EjectionRelations:
        default:
        DBG("Unsupported device relations query\n");
        return WvlPassIrpDown(dev_obj, irp);
      }

    /* We need to include any higher driver's PDOs */
    higher_dev_relations = (VOID *) irp->IoStatus.Information;
    response = WvlMergeDeviceRelations(dev_relations, higher_dev_relations);
    if (!response) {
        /*
         * It's not clear whether or not we should do the following,
         * but it can't hurt...  Right?
         */
        if (higher_dev_relations) {
            for (i = 0; i < higher_dev_relations->Count; ++i)
              ObDereferenceObject(higher_dev_relations->Objects[i]);
            wv_free(higher_dev_relations);
          }

        irp->IoStatus.Status = status = STATUS_INSUFFICIENT_RESOURCES;
        irp->IoStatus.Information = 0;
        WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
        return status;
      }

    /* Reference our PDOs */
    for (i = 0; i < dev_relations->Count; ++i)
      ObReferenceObject(dev_relations->Objects[i]);

    /* Send the IRP down */
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = (ULONG_PTR) response;
    return WvlPassIrpDown(dev_obj, irp);
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
    NTSTATUS status;
    LONG c;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(irp);

    IoDeleteSymbolicLink(&WvBusDosName);

    /* Delete all child PDOs */
    if (bus->BusRelations) {
        while (bus->BusRelations->Count)
          WvlRemoveDeviceFromMainBus(bus->BusRelations->Objects[0]);
        wv_free(bus->BusRelations);
        bus->BusRelations = NULL;
      }

    /* Send the IRP down */
    status = WvlPassIrpDown(dev_obj, irp);

    /* Detach FDO from PDO */
    WvlDetachDevice(dev_obj);
    WvBus.LowerDeviceObject = NULL;
    WvBus.Pdo = NULL;

    /* Schedule deletion of this device when the thread finishes */
    WvlDeleteDevice(dev_obj);

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
    status = WvlWaitForIrpCompletion(dev_obj, irp);
    if (!NT_SUCCESS(status)) {
        DBG(
            "Lower device %p failed to start\n",
            (VOID *) WvlGetLowerDeviceObject(dev_obj)
          );
        goto err_lower;
      }

    WvBus.OldState = WvBus.State;
    WvBus.State = WvlBusStateStarted;
    InterlockedAnd(&bus->Flags, ~CvWvMainBusFlagIrpsHeld);

    irp->IoStatus.Status = STATUS_SUCCESS;

    err_lower:

    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

/** Device control IRP dispatcher */
static NTSTATUS STDCALL WvMainBusDispatchDeviceControlIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    IO_STACK_LOCATION * io_stack_loc;
    ULONG POINTER_ALIGNMENT code;
    NTSTATUS status;

    ASSERT(dev_obj);
    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    code = io_stack_loc->Parameters.DeviceIoControl.IoControlCode;
    switch (code) {
        case IOCTL_FILE_ATTACH:
        status = WvFilediskAttach(irp);
        break;

        case IOCTL_FILE_DETACH:
        return WvMainBusDeviceControlDetach(dev_obj, irp);

        case IOCTL_WV_DUMMY:
        return WvDummyIoctl(dev_obj, irp);

        default:
        irp->IoStatus.Information = 0;
        status = STATUS_INVALID_DEVICE_REQUEST;
      }

    irp->IoStatus.Status = status;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

/**
 * Try to detach a user-specified child PDO from the main bus
 *
 * @param DeviceObject
 *   The main bus device
 *
 * @param Irp
 *   The IRP for the request
 *
 * @param Irp->AssociatedIrp.SystemBuffer
 *   Points to a buffer with the user-specified unit number to be detached
 *
 * @param Parameters.DeviceIoControl.InputBufferLength (I/O stack location)
 *   The length of the user's buffer
 *
 * @retval STATUS_SUCCESS
 * @retval STATUS_UNSUCCESSFUL
 *   The device could not be detached
 */
static NTSTATUS STDCALL WvMainBusDeviceControlDetach(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    IO_STACK_LOCATION * io_stack_loc;
    BOOLEAN buf_sz_ok;
    NTSTATUS status;
    UINT32 unit_num;
    WVL_SP_BUS_NODE walker;
    WV_SP_DEV_T dev = NULL;

    (VOID) dev_obj;
    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    /* Check the buffer */
    buf_sz_ok = (
        io_stack_loc->Parameters.DeviceIoControl.InputBufferLength <
        sizeof unit_num
      );
    if (!buf_sz_ok || !irp->AssociatedIrp.SystemBuffer) {
        DBG("Invalid request buffer\n");
        status = STATUS_INVALID_PARAMETER;
        goto err_buf;
      }

    /* Fetch the user-specified unit number to be detached */
    unit_num = *((UINT32 *) irp->AssociatedIrp.SystemBuffer);
    DBG("Request to detach unit %u...\n", unit_num);

    /* For each node on the bus... */
    walker = NULL;
    WvlBusLock(&WvBus);
    while (walker = WvlBusGetNextNode(&WvBus, walker)) {
        /* If the unit number matches... */
        if (WvlBusGetNodeNum(walker) == unit_num) {
            dev = WvDevFromDevObj(WvlBusGetNodePdo(walker));
            /* If it's a boot-time device... */
            if (dev->Boot) {
                DBG("Cannot detach a boot-time device\n");
                /* Signal error */
                dev = NULL;
              }

            /* Found */
            break;
          }
      }
    WvlBusUnlock(&WvBus);

    /* Did we find a removable match? */
    if (!dev) {
        DBG("Unit %u not found\n", unit_num);
        status = STATUS_INVALID_PARAMETER;
        goto err_dev;
      }

    /* Detach the node */
    WvBusRemoveDev(dev);
    DBG("Removed unit %u\n", unit_num);
    status =  STATUS_SUCCESS;

    err_dev:

    err_buf:

    irp->IoStatus.Status = status;
    irp->IoStatus.Information = 0;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

static NTSTATUS STDCALL WvMainBusDispatchPowerIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    ASSERT(dev_obj);
    ASSERT(irp);
    return WvlIrpPassPowerToLower(WvBus.LowerDeviceObject, irp);
  }

static NTSTATUS STDCALL WvMainBusDispatchCreateCloseIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    ASSERT(dev_obj);
    ASSERT(irp);
    /* Always succeed with nothing to do */
    /* TODO: Track resource usage */
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = 0;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
  }

static NTSTATUS STDCALL WvMainBusDispatchSystemControlIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    ASSERT(dev_obj);
    ASSERT(irp);
    return WvlIrpPassToLower(WvBus.LowerDeviceObject, irp);
  }
