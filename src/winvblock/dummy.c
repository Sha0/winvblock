/**
 * Copyright (C) 2010-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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

/* From driver.c */
extern WVL_S_BUS_T WvBus;

/* Forward declarations. */
static NTSTATUS STDCALL WvDummyIds(IN PIRP, IN WV_SP_DUMMY_IDS);
static WV_F_DEV_PNP WvDummyPnp;
static WVL_F_BUS_WORK_ITEM WvDummyAdd_;

/* Dummy PnP IRP handler. */
static NTSTATUS STDCALL WvDummyPnp(
    IN WV_SP_DEV_T dev,
    IN PIRP irp,
    IN UCHAR code
  ) {
    if (code != IRP_MN_QUERY_ID)
      return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);

    /* The WV_S_DEV_T extension points to the dummy IDs. */
    return WvDummyIds(irp, dev->ext);
  }

typedef struct WV_ADD_DUMMY {
    const WV_S_DUMMY_IDS * DummyIds;
    DEVICE_TYPE DevType;
    ULONG DevCharacteristics;
    PKEVENT Event;
    NTSTATUS Status;
  } WV_S_ADD_DUMMY, * WV_SP_ADD_DUMMY;

/**
 * Add a dummy PDO child node in the context of the bus' thread.  Internal.
 *
 * @v context           Points to the WV_S_ADD_DUMMY to process.
 */
static VOID STDCALL WvDummyAdd_(PVOID context) {
    WV_SP_ADD_DUMMY dummy_context = context;
    NTSTATUS status;
    PDEVICE_OBJECT pdo = NULL;
    WV_SP_DEV_T dev;
    SIZE_T dummy_ids_size;
    WV_SP_DUMMY_IDS dummy_ids;
    static WV_S_DEV_IRP_MJ irp_mj = {
        (WV_FP_DEV_DISPATCH) 0,
        (WV_FP_DEV_DISPATCH) 0,
        (WV_FP_DEV_CTL) 0,
        (WV_FP_DEV_SCSI) 0,
        WvDummyPnp,
      };

    status = IoCreateDevice(
        WvDriverObj,
        sizeof (WV_S_DEV_EXT),
        NULL,
        dummy_context->DevType,
        dummy_context->DevCharacteristics,
        FALSE,
        &pdo
      );
    if (!NT_SUCCESS(status) || !pdo) {
        DBG("Couldn't create dummy device.\n");
        dummy_context->Status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_create_pdo;
      }

    dev = wv_malloc(sizeof *dev);
    if (!dev) {
        DBG("Couldn't allocate dummy device.\n");
        dummy_context->Status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_dev;
      }

    dummy_ids_size =
      sizeof *dummy_ids +
      dummy_context->DummyIds->Len * sizeof dummy_ids->Text[0] -
      sizeof dummy_ids->Text[0];        /* The struct hack uses a WCHAR[1]. */
    dummy_ids = wv_malloc(dummy_ids_size);
    if (!dummy_ids) {
        DBG("Couldn't allocate dummy IDs.\n");
        dummy_context->Status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_dummy_ids;
      }
    /* Copy the IDs offsets and lengths. */
    RtlCopyMemory(dummy_ids, dummy_context->DummyIds, sizeof *dummy_ids);
    /* Copy the text of the IDs. */
    RtlCopyMemory(
        &dummy_ids->Text,
        dummy_context->DummyIds->Ids,
        dummy_context->DummyIds->Len * sizeof dummy_ids->Text[0]
      );
    /* Point to the copy of the text. */
    dummy_ids->Ids = dummy_ids->Text;

    /* Ok! */
    WvDevInit(dev);
    dev->IrpMj = &irp_mj;
    dev->ext = dummy_ids;       /* TODO: Implement a dummy free.  Leaking. */
    dev->Self = pdo;
    WvDevForDevObj(pdo, dev);
    WvlBusInitNode(&dev->BusNode, pdo);
    /* Associate the parent bus. */
    dev->Parent = WvBus.Fdo;
    /* Add the new PDO device to the bus' list of children. */
    WvlBusAddNode(&WvBus, &dev->BusNode);
    dev->DevNum = WvlBusGetNodeNum(&dev->BusNode);
    pdo->Flags &= ~DO_DEVICE_INITIALIZING;

    dummy_context->Status = STATUS_SUCCESS;
    KeSetEvent(dummy_context->Event, 0, FALSE);
    return;

    wv_free(dummy_ids);
    err_dummy_ids:

    wv_free(dev);
    err_dev:

    IoDeleteDevice(pdo);
    err_create_pdo:

    KeSetEvent(dummy_context->Event, 0, FALSE);
    return;
  }

/**
 * Produce a dummy PDO node on the main bus.
 *
 * @v DummyIds                  The PnP IDs for the dummy.
 * @v DevType                   The type for the dummy device.
 * @v DevCharacteristics        The dummy device characteristics.
 * @ret NTSTATUS                The status of the operation.
 */
WVL_M_LIB NTSTATUS STDCALL WvDummyAdd(
    IN const WV_S_DUMMY_IDS * DummyIds,
    IN DEVICE_TYPE DevType,
    IN ULONG DevCharacteristics
  ) {
    KEVENT event;
    WV_S_ADD_DUMMY context = {
        DummyIds,
        DevType,
        DevCharacteristics,
        &event,
        STATUS_UNSUCCESSFUL
      };
    WVL_S_BUS_CUSTOM_WORK_ITEM work_item = {
        WvDummyAdd_,
        &context
      };
    NTSTATUS status;

    if (!DummyIds)
      return STATUS_INVALID_PARAMETER;

    if (!WvBus.Fdo)
      return STATUS_NO_SUCH_DEVICE;

    KeInitializeEvent(&event, SynchronizationEvent, FALSE);

    status = WvlBusEnqueueCustomWorkItem(&WvBus, &work_item);
    if (!NT_SUCCESS(status))
      return status;

    /* Wait for WvDummyAdd_() to complete. */
    KeWaitForSingleObject(
        &event,
        Executive,
        KernelMode,
        FALSE,
        NULL
      );

    return context.Status;
  }

/**
 * Handle a PnP ID query with a WV_S_DUMMY_IDS object.
 *
 * @v Irp               The PnP ID query IRP to handle.
 * @v DummyIds          The object containing the IDs to respond with.
 * @ret NTSTATUS        The status of the operation.
 */
static NTSTATUS STDCALL WvDummyIds(
    IN PIRP Irp,
    IN WV_SP_DUMMY_IDS DummyIds
  ) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(Irp);
    BUS_QUERY_ID_TYPE query_type = io_stack_loc->Parameters.QueryId.IdType;
    const WCHAR * ids;
    UINT32 len;
    NTSTATUS status;

    switch (query_type) {
        case BusQueryDeviceID:
          ids = DummyIds->Ids + DummyIds->DevOffset;
          len = DummyIds->DevLen;
          break;

        case BusQueryInstanceID:
          ids = DummyIds->Ids + DummyIds->InstanceOffset;
          len = DummyIds->InstanceLen;
          break;

        case BusQueryHardwareIDs:
          ids = DummyIds->Ids + DummyIds->HardwareOffset;
          len = DummyIds->HardwareLen;
          break;

        case BusQueryCompatibleIDs:
          ids = DummyIds->Ids + DummyIds->CompatOffset;
          len = DummyIds->CompatLen;
          break;

        default:
          return WvlIrpComplete(Irp, 0, STATUS_NOT_SUPPORTED);
      }

    /* Allocate the return buffer. */
    Irp->IoStatus.Information = (ULONG_PTR) wv_palloc(len * sizeof *ids);
    if (Irp->IoStatus.Information == 0) {
        DBG("wv_palloc failed.\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto alloc_info;
      }
    /* Copy the working buffer to the return buffer. */
    RtlCopyMemory(
        (PVOID) Irp->IoStatus.Information,
        ids,
        len * sizeof *ids
      );
    status = STATUS_SUCCESS;

    /* irp->IoStatus.Information not freed. */
    alloc_info:

    return WvlIrpComplete(Irp, Irp->IoStatus.Information, status);
  }
