/**
 * Copyright (C) 2009-2012, Shao Miller <sha0.miller@gmail.com>.
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
 * "Safe INT 0x13 hook" bus FDO
 */

#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "driver.h"
#include "debug.h"
#include "x86.h"
#include "safehook.h"

/** Function declarations */

/** IRP dispatchers */
DRIVER_DISPATCH WvSafeHookIrpDispatch;

static __drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH WvSafeHookDispatchPnpIrp;

/** PnP handlers */
static __drv_dispatchType(IRP_MN_QUERY_DEVICE_RELATIONS) DRIVER_DISPATCH
  WvSafeHookPnpQueryDeviceRelations;
static __drv_dispatchType(IRP_MN_REMOVE_DEVICE) DRIVER_DISPATCH
  WvSafeHookPnpRemoveDevice;

/** Function definitions */

/**
 * Handle an IRP for a safe hook bus
 *
 * @param DeviceObject
 *   The safe hook device
 *
 * @param Irp
 *   The IRP to process
 *
 * @return
 *   The status of the operation
 */
NTSTATUS STDCALL WvSafeHookIrpDispatch(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    IO_STACK_LOCATION * io_stack_loc;
    NTSTATUS status;

    ASSERT(dev_obj);

    /* Process all IRPs in the device's thread */
    if (!WvlInDeviceThread(dev_obj))
      return WvlAddIrpToDeviceQueue(dev_obj, irp, TRUE);

    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    /* Handle PnP IRPs, only */
    if (io_stack_loc->MajorFunction != IRP_MJ_PNP) {
        DBG("Unhandled major function for FDO %p\n", (VOID *) dev_obj);
        status = STATUS_NOT_SUPPORTED;
        irp->IoStatus.Information = 0;
        goto err_major;
      }

    return WvSafeHookDispatchPnpIrp(dev_obj, irp);

    err_major:

    irp->IoStatus.Status = status;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

/** PnP IRP dispatcher */
static NTSTATUS STDCALL WvSafeHookDispatchPnpIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WV_SAFE_HOOK_BUS * bus;
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
        case IRP_MN_QUERY_DEVICE_RELATIONS:
        status = WvSafeHookPnpQueryDeviceRelations(dev_obj, irp);
        goto completed;

        case IRP_MN_REMOVE_DEVICE:
        status = WvSafeHookPnpRemoveDevice(dev_obj, irp);
        goto completed;

        default:
        DBG("Unhandled IRP_MN_*: %u\n", code);
      }

    return WvlPassIrpDown(dev_obj, irp);

    completed:
    return status;
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
static NTSTATUS STDCALL WvSafeHookPnpQueryDeviceRelations(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    S_WV_SAFE_HOOK_BUS* bus;
    IO_STACK_LOCATION * io_stack_loc;
    DEVICE_RELATIONS * higher_dev_relations;
    ULONG pdo_count;
    DEVICE_RELATIONS * dev_relations;
    NTSTATUS status;
    ULONG i;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);

    higher_dev_relations = (VOID *) irp->IoStatus.Information;

    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);    

    switch (io_stack_loc->Parameters.QueryDeviceRelations.Type) {
        case BusRelations:
        pdo_count = bus->BusRelations->Count;

        /* If we've nothing to report, pass it down */
        if (!pdo_count)
          return WvlPassIrpDown(dev_obj, irp);

        /* Should we add to a higher device's list? */
        if (higher_dev_relations && higher_dev_relations->Count)
          pdo_count += higher_dev_relations->Count;

        /* Allocate the response */
        if (higher_dev_relations && !higher_dev_relations->Count) {
            /* A higher device has conveniently left us a slot */
            dev_relations = higher_dev_relations;
          } else {
            dev_relations = wv_palloc(
                sizeof *dev_relations +
                sizeof dev_relations->Objects[0] * (pdo_count - 1)
              );
          }
        if (!dev_relations) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            irp->IoStatus.Information = 0;

            /*
             * It's not clear whether or not we should do the following,
             * but it can't hurt...  Right?
             */
            if (higher_dev_relations)
              wv_free(higher_dev_relations);
            goto err_dev_relations;
          }

        /* Populate the response */
        dev_relations->Count = pdo_count;
        i = 0;
        if (higher_dev_relations) {
            for (/* i == 0 */; i < higher_dev_relations->Count; ++i)
              dev_relations->Objects[i] = higher_dev_relations->Objects[i];

            /* Don't free if we're using a slot that an upper device left */
            if (higher_dev_relations->Count)
              wv_free(higher_dev_relations);
          }
        dev_relations->Objects[i] = bus->BusRelations->Objects[0];

        status = STATUS_SUCCESS;
        irp->IoStatus.Information = (ULONG_PTR) dev_relations;
        break;

        default:
        DBG("Unsupported device relations query\n");
        return WvlPassIrpDown(dev_obj, irp);
      }

    irp->IoStatus.Status = status;
    return WvlPassIrpDown(dev_obj, irp);

    err_dev_relations:

    irp->IoStatus.Status = status;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

/**
 * IRP_MJ_PNP:IRP_MN_REMOVE_DEVICE handler
 *
 * IRQL == PASSIVE_LEVEL, system thread
 * Completed by PDO
 * Do not send this IRP
 * Any child PDOs receive one of these IRPs first, except possibly
 * if they've been surprise-removed
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 */
static NTSTATUS STDCALL WvSafeHookPnpRemoveDevice(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WV_SAFE_HOOK_BUS * bus;
    NTSTATUS status;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(irp);

    /* TODO: Unlink the child PDO, if any */
    if (bus->BusRelations->Count) {
        WvlDeleteDevice(bus->BusRelations->Objects[0]);
        bus->BusRelations->Count = 0;
        bus->BusRelations->Objects[0] = NULL;
      }

    /* Send the IRP down */
    status = WvlPassIrpDown(dev_obj, irp);

    /* Detach FDO from PDO */
    WvlDetachDevice(dev_obj);

    /* Schedule deletion of this device when the thread finishes */
    WvlDeleteDevice(dev_obj);

    return status;
  }
