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
 * Dummy device specifics.
 */

#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "irp.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "dummy.h"
#include "debug.h"

/* From mainbus.c */
extern NTSTATUS STDCALL WvBusRemoveDev(IN WV_SP_DEV_T);

/** Function declarations */
static DRIVER_DISPATCH WvDummyIrpDispatch;
static __drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH WvDummyDispatchPnpIrp;

/** PnP handlers */
static __drv_dispatchType(IRP_MN_QUERY_ID) DRIVER_DISPATCH WvDummyPnpQueryId;

/** Function definitions */

/**
 * Handle an IRP for a dummy device
 *
 * @param DeviceObject
 *   The dummy device
 *
 * @param Irp
 *   The IRP to process
 *
 * @return
 *   The status of the operation
 */
static NTSTATUS STDCALL WvDummyIrpDispatch(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    IO_STACK_LOCATION * io_stack_loc;

    ASSERT(dev_obj);
    ASSERT(irp);

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    switch (io_stack_loc->MajorFunction) {
        case IRP_MJ_PNP:
        return WvDummyDispatchPnpIrp(dev_obj, irp);

        default:
        DBG("Unhandled IRP_MJ_*: %d\n", io_stack_loc->MajorFunction);
      }
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
  }

/** PnP IRP dispatcher */
static NTSTATUS STDCALL WvDummyDispatchPnpIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    IO_STACK_LOCATION * io_stack_loc;
    WV_S_DEV_T * dev;

    ASSERT(dev_obj);
    ASSERT(irp);

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    dev = WvDevFromDevObj(dev_obj);
    ASSERT(dev);

    switch (io_stack_loc->MinorFunction) {
        case IRP_MN_QUERY_ID:
        return WvDummyPnpQueryId(dev_obj, irp);

        case IRP_MN_QUERY_REMOVE_DEVICE:
        return WvlIrpComplete(irp, 0, STATUS_SUCCESS);

        case IRP_MN_REMOVE_DEVICE:
        /* Any error status for the removal slips away, here */
        WvBusRemoveDev(dev);
        return WvlIrpComplete(irp, 0, STATUS_SUCCESS);

        default:
        /* Return whatever upper drivers in the stack yielded */
        return WvlIrpComplete(
            irp,
            irp->IoStatus.Information,
            irp->IoStatus.Status
          );
      }
  }

/**
 * IRP_MJ_PNP:IRP_MN_QUERY_ID handler
 *
 * IRQL == PASSIVE_LEVEL, any thread
 * Ok to send this IRP
 * Completed by PDO (here)
 *
 * @param Parameters.QueryId.IdType (I/O stack location)
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *     Irp->IoStatus.Information populated from paged memory
 *   Error:
 *     Irp->IoStatus.Information == 0
 *     Irp->IoStatus.Status == STATUS_INSUFFICIENT_RESOURCES
 *       or
 *     Irp->IoStatus.Status == STATUS_NOT_SUPPORTED
 */
static NTSTATUS STDCALL WvDummyPnpQueryId(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WVL_DUMMY_PDO * dummy;
    IO_STACK_LOCATION * io_stack_loc;
    const UCHAR * ptr;
    const WCHAR * ids;
    BUS_QUERY_ID_TYPE query_type;
    UINT32 len;
    VOID * response;
    NTSTATUS status;

    ASSERT(dev_obj);
    dummy = dev_obj->DeviceExtension;
    ASSERT(dummy);
    ASSERT(irp);

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    ptr = (const VOID *) dummy->DummyIds;
    ptr += dummy->DummyIds->Offset;
    ids = (const VOID *) ptr;

    query_type = io_stack_loc->Parameters.QueryId.IdType;
    switch (query_type) {
        case BusQueryDeviceID:
        ids += dummy->DummyIds->DevOffset;
        len = dummy->DummyIds->DevLen;
        break;

        case BusQueryHardwareIDs:
        ids += dummy->DummyIds->HardwareOffset;
        len = dummy->DummyIds->HardwareLen;
        break;

        case BusQueryCompatibleIDs:
        ids += dummy->DummyIds->CompatOffset;
        len = dummy->DummyIds->CompatLen;
        break;

        case BusQueryInstanceID:
        ids += dummy->DummyIds->InstanceOffset;
        len = dummy->DummyIds->InstanceLen;
        break;

        default:
        status = STATUS_NOT_SUPPORTED;
        irp->IoStatus.Information = 0;
        goto err_query_type;
      }

    /* Allocate the respone */
    response = wv_palloc(len * sizeof *ids);
    if (!response) {
        DBG("Couldn't allocate response\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        irp->IoStatus.Information = 0;
        goto err_response;
      }

    /* Copy the IDs into the response */
    RtlCopyMemory(response, ids, len * sizeof *ids);

    irp->IoStatus.Information = (ULONG_PTR) response;
    status = STATUS_SUCCESS;

    /* irp->IoStatus.Information not freed */
    err_response:

    err_query_type:

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

WVL_M_LIB NTSTATUS STDCALL WvDummyAdd(
    IN const WV_S_DUMMY_IDS * dummy_ids,
    IN ULONG dummy_ids_offset,
    IN ULONG extra_data_offset,
    IN ULONG extra_data_sz,
    IN UNICODE_STRING * dev_name,
    IN BOOLEAN exclusive,
    OUT DEVICE_OBJECT ** pdo
  ) {
    /* Next two lines calculate the minimum "safe" dummy ID offset */
    struct wv_dummy_add_ { S_WVL_DUMMY_PDO p; WV_S_DUMMY_IDS i; };
    const SIZE_T dummy_min_offset = FIELD_OFFSET(struct wv_dummy_add_, i);
    BOOLEAN invalid_params;
    ULONG dummy_end;
    NTSTATUS status;
    ULONG dev_ext_sz;
    DEVICE_OBJECT * new_pdo;
    UCHAR * ptr;
    S_WVL_DUMMY_PDO * dummy;

    /* Check parameters.  "Whoa" */
    invalid_params = (
        !dummy_ids ||
        dummy_ids->Len < sizeof *dummy_ids ||
        (!dummy_ids_offset && (extra_data_offset || extra_data_sz)) ||
        (dummy_ids_offset && dummy_ids_offset < dummy_min_offset) ||
        (!extra_data_offset && extra_data_sz) ||
        /* Next line just to sneak in a calculation */
        !(dummy_end = dummy_ids_offset + dummy_ids->Len) ||
        (extra_data_offset && extra_data_offset < dummy_end) ||
        (!extra_data_sz && extra_data_offset)
      );
    if (invalid_params) {
        status = STATUS_INVALID_PARAMETER;
        goto err_invalid_params;
      }

    /* If the caller didn't specify a dummy ID offset, we choose one */
    if (!dummy_ids_offset) {
        dummy_ids_offset = dummy_min_offset;
        dummy_end = dummy_ids_offset + dummy_ids->Len;
      }

    dev_ext_sz = dummy_end;
    if (extra_data_offset)
      dev_ext_sz = extra_data_offset + extra_data_sz;

    new_pdo = NULL;

    status = IoCreateDevice(
        WvDriverObj,
        dev_ext_sz,
        dev_name,
        dummy_ids->DevType,
        dummy_ids->DevCharacteristics,
        exclusive,
        &new_pdo
      );
    if (!NT_SUCCESS(status)) {
        DBG("Couldn't create dummy device\n");
        goto err_new_pdo;
      }
    ASSERT(new_pdo);

    /* The caller might've requested more storage */
    ptr = new_pdo->DeviceExtension;
    ASSERT(ptr);

    /* Work with the dummy device extension */
    dummy = new_pdo->DeviceExtension;

    /* Copy the dummy IDs, which means the whole wrapper */
    dummy->DummyIds = (VOID *) (ptr + dummy_ids_offset);
    RtlCopyMemory(dummy->DummyIds, dummy_ids, dummy_ids->Len);

    /* The caller might've requested more storage */
    if (!extra_data_offset)
      dummy->ExtraData = NULL;
      else
      dummy->ExtraData = (ptr + extra_data_offset);

    /* Initialize the old device stuff */
    WvDevInit(dummy->OldDevice);
    dummy->OldDevice->ext = dummy->DummyIds;
    dummy->OldDevice->Self = new_pdo;
    dummy->OldDevice->Ops.Free = NULL;

    WvDevForDevObj(new_pdo, dummy->OldDevice);
    WvDevSetIrpHandler(new_pdo, WvDummyIrpDispatch);

    new_pdo->Flags &= ~DO_DEVICE_INITIALIZING;

    /* Optionally fill the caller's PDO pointer */
    if (pdo)
      *pdo = new_pdo;

    return STATUS_SUCCESS;

    IoDeleteDevice(new_pdo);
    err_new_pdo:

    err_invalid_params:

    return status;
  }

/**
 * Remove a dummy PDO node on the WinVBlock bus.
 *
 * @v Pdo               The PDO to remove.
 * @ret NTSTATUS        The status of the operation.
 *
 * It might actually be better to handle a PnP remove IOCTL.
 */
WVL_M_LIB NTSTATUS STDCALL WvDummyRemove(IN PDEVICE_OBJECT Pdo) {
    /* Sanity check. */
    if (Pdo->DriverObject == WvDriverObj)
      return WvBusRemoveDev(WvDevFromDevObj(Pdo));
    return STATUS_INVALID_PARAMETER;
  }

NTSTATUS STDCALL WvDummyIoctl(IN DEVICE_OBJECT * dev_obj, IN IRP * irp) {
    IO_STACK_LOCATION * io_stack_loc;
    WV_S_DUMMY_IDS * dummy_ids;
    NTSTATUS status;
    DEVICE_OBJECT * new_pdo;
    WV_S_DEV_T * new_dev;

    ASSERT(dev_obj);
    (VOID) dev_obj;
    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    if (
        io_stack_loc->Parameters.DeviceIoControl.InputBufferLength <
        sizeof *dummy_ids
      ) {
        DBG(
            "Input buffer too small for dummy details in IRP %p\n",
            (VOID *) irp
          );
        status = STATUS_INVALID_PARAMETER;
        irp->IoStatus.Information = 0;
        goto err_sz;
      }

    dummy_ids = irp->AssociatedIrp.SystemBuffer;
    ASSERT(dummy_ids);

    status = WvDummyAdd(
        /* Dummy IDs */
        dummy_ids,
        /* Dummy IDs offset: Auto */
        0,
        /* Extra data offset: None */
        0,
        /* Extra data size: None */
        0,
        /* Device name: Not specified */
        NULL,
        /* Exclusive access? */
        FALSE,
        /* PDO pointer to populate */
        &new_pdo
      );
    if (!NT_SUCCESS(status))
      goto err_new_pdo;
    ASSERT(new_pdo);

    new_dev = WvDevFromDevObj(new_pdo);
    ASSERT(new_dev);

    WvlBusInitNode(&new_dev->BusNode, new_pdo);

    /* Make the main bus the parent bus */
    new_dev->Parent = WvBus.Fdo;

    /* Add the new PDO device to the main bus' list of children */
    WvlBusAddNode(&WvBus, &new_dev->BusNode);

    status = STATUS_SUCCESS;
    goto out;

    IoDeleteDevice(new_pdo);
    err_new_pdo:

    err_sz:

    out:
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }
