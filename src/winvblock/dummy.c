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

/* From bus.c */
extern WVL_S_BUS_T WvBus;
extern NTSTATUS STDCALL WvBusRemoveDev(IN WV_SP_DEV_T);

/* Forward declarations. */
static NTSTATUS STDCALL WvDummyIds(IN PIRP, IN WV_SP_DUMMY_IDS);
static WV_F_DEV_PNP WvDummyPnp;

/* Dummy PnP IRP handler. */
static NTSTATUS STDCALL WvDummyPnp(
    IN WV_SP_DEV_T dev,
    IN PIRP irp,
    IN UCHAR code
  ) {
    switch (code) {
        case IRP_MN_QUERY_ID:
          /* The WV_S_DEV_T extension points to the dummy IDs. */
          return WvDummyIds(irp, dev->ext);

        case IRP_MN_QUERY_REMOVE_DEVICE:
          return WvlIrpComplete(irp, 0, STATUS_SUCCESS);

        case IRP_MN_REMOVE_DEVICE:
          /* Any error status for the removal slips away, here. */
          WvBusRemoveDev(dev);
          return WvlIrpComplete(irp, 0, STATUS_SUCCESS);

        default:
          /* Return whatever upper drivers in the stack yielded. */
          return WvlIrpComplete(
              irp,
              irp->IoStatus.Information,
              irp->IoStatus.Status
            );
      }
  }

/* Handle an IRP. */
static NTSTATUS WvDummyIrpDispatch(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc;
    WV_SP_DEV_T dev;

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    dev = WvDevFromDevObj(dev_obj);
    switch (io_stack_loc->MajorFunction) {
        case IRP_MJ_PNP:
          return WvDummyPnp(
              dev,
              irp,
              io_stack_loc->MinorFunction
            );

        default:
          DBG("Unhandled IRP_MJ_*: %d\n", io_stack_loc->MajorFunction);
      }
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
  }

typedef struct WV_ADD_DUMMY {
    const WV_S_DUMMY_IDS * DummyIds;
    DEVICE_TYPE DevType;
    ULONG DevCharacteristics;
    PKEVENT Event;
    NTSTATUS Status;
    PDEVICE_OBJECT Pdo;
  } WV_S_ADD_DUMMY, * WV_SP_ADD_DUMMY;

/* Produce a dummy PDO node on the main bus.  Internal */
static NTSTATUS STDCALL WvDummyAdd_(
    IN WV_SP_DUMMY_IDS DummyIds,
    IN PDEVICE_OBJECT * Pdo
  ) {
    NTSTATUS status;
    PDEVICE_OBJECT pdo = NULL;
    WV_SP_DEV_T dev;
    WV_SP_DUMMY_IDS new_dummy_ids;

    status = IoCreateDevice(
        WvDriverObj,
        sizeof (WV_S_DEV_EXT),
        NULL,
        DummyIds->DevType,
        DummyIds->DevCharacteristics,
        FALSE,
        &pdo
      );
    if (!NT_SUCCESS(status) || !pdo) {
        DBG("Couldn't create dummy device.\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_create_pdo;
      }

    dev = wv_malloc(sizeof *dev);
    if (!dev) {
        DBG("Couldn't allocate dummy device.\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_dev;
      }

    new_dummy_ids = wv_malloc(DummyIds->Len);
    if (!new_dummy_ids) {
        DBG("Couldn't allocate dummy IDs.\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_dummy_ids;
      }

    /* Copy the IDs' offsets and lengths. */
    RtlCopyMemory(new_dummy_ids, DummyIds, DummyIds->Len);

    /* Ok! */
    WvDevInit(dev);
    dev->ext = new_dummy_ids;   /* TODO: Implement a dummy free.  Leaking. */
    dev->Self = pdo;
    /* Optionally fill the caller's PDO pointer. */
    if (Pdo)
      *Pdo = pdo;
    WvDevForDevObj(pdo, dev);
    WvDevSetIrpHandler(pdo, WvDummyIrpDispatch);
    WvlBusInitNode(&dev->BusNode, pdo);
    /* Associate the parent bus. */
    dev->Parent = WvBus.Fdo;
    /* Add the new PDO device to the bus' list of children. */
    WvlBusAddNode(&WvBus, &dev->BusNode);
    pdo->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;

    wv_free(new_dummy_ids);
    err_dummy_ids:

    wv_free(dev);
    err_dev:

    IoDeleteDevice(pdo);
    err_create_pdo:

    return status;
  }

/**
 * Produce a dummy PDO node on the main bus.
 *
 * @v DummyIds                  The PnP IDs for the dummy.  Also includes
 *                              the device type and characteristics.
 * @ret NTSTATUS                The status of the operation.
 */
WVL_M_LIB NTSTATUS STDCALL WvDummyAdd(
    IN const WV_S_DUMMY_IDS * DummyIds
  ) {
    KEVENT signal;
    PIRP irp;
    IO_STATUS_BLOCK io_status = {0};
    NTSTATUS status;

    /*
     * In this function, we actually send the request through to the
     * driver via an IRP.  This is a good exercise, since we expect a
     * user-land utility to be capable of the same.
     */
    if (!WvBus.Fdo)
      return STATUS_NO_SUCH_DEVICE;

    /* Prepare the request. */
    KeInitializeEvent(&signal, SynchronizationEvent, FALSE);
    irp = IoBuildDeviceIoControlRequest(
        IOCTL_WV_DUMMY,
        WvBus.Fdo,
        (PVOID) DummyIds,
        DummyIds->Len,
        NULL,
        0,
        FALSE,
        &signal,
        &io_status
      );
    if (!irp)
      return STATUS_INSUFFICIENT_RESOURCES;

    status = IoCallDriver(WvBus.Fdo, irp);
    if (status == STATUS_PENDING) {
        KeWaitForSingleObject(
            &signal,
            Executive,
            KernelMode,
            FALSE,
            NULL
          );
        status = io_status.Status;
      }
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

/**
 * Handle a dummy IOCTL for creating a dummy PDO.
 *
 * @v Irp               An IRP with an associated buffer containing
 *                      WV_S_DUMMY_IDS data.
 * @ret NTSTATUS        The status of the operation.
 */
NTSTATUS STDCALL WvDummyIoctl(IN PIRP Irp) {
    WV_SP_DUMMY_IDS dummy_ids = Irp->AssociatedIrp.SystemBuffer;
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(Irp);

    if (
        io_stack_loc->Parameters.DeviceIoControl.InputBufferLength <
        sizeof *dummy_ids
      ) {
        DBG("Dummy IDs too small in IRP %p.\n", Irp);
        return WvlIrpComplete(Irp, 0, STATUS_INVALID_PARAMETER);
      }

    return WvlIrpComplete(Irp, 0, WvDummyAdd_(dummy_ids, NULL));
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
    const CHAR * start;
    const WCHAR * ids;
    UINT32 len;
    NTSTATUS status;

    start = (const CHAR *) DummyIds + DummyIds->Offset;
    ids = (const WCHAR *) start;
    switch (query_type) {
        case BusQueryDeviceID:
          ids += DummyIds->DevOffset;
          len = DummyIds->DevLen;
          break;

        case BusQueryInstanceID:
          ids += DummyIds->InstanceOffset;
          len = DummyIds->InstanceLen;
          break;

        case BusQueryHardwareIDs:
          ids += DummyIds->HardwareOffset;
          len = DummyIds->HardwareLen;
          break;

        case BusQueryCompatibleIDs:
          ids += DummyIds->CompatOffset;
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
