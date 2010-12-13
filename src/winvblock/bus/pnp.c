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
 * Bus PnP IRP handling.
 */

#include <ntddk.h>

#include "winvblock.h"
#include "wv_stdlib.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "device.h"
#include "disk.h"
#include "bus.h"
#include "debug.h"
#include "probe.h"

static NTSTATUS STDCALL bus_pnp__io_completion_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    IN PKEVENT event
  ) {
    KeSetEvent(event, 0, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
  }

NTSTATUS STDCALL bus_pnp__start_dev(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    IN PIO_STACK_LOCATION io_stack_loc,
    IN struct device__type * dev,
    OUT winvblock__bool_ptr completion
  ) {
    NTSTATUS status;
    KEVENT event;
    struct bus__type * bus = bus__get(dev);
    PDEVICE_OBJECT lower = bus->LowerDeviceObject;

    if (!lower) {
        *completion = TRUE;
        return STATUS_SUCCESS;
      }
    KeInitializeEvent(&event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(irp);
    IoSetCompletionRoutine(
        irp,
        (PIO_COMPLETION_ROUTINE) bus_pnp__io_completion_,
        (void *) &event,
        TRUE,
        TRUE,
        TRUE
      );
    status = IoCallDriver(lower, irp);
    if (status == STATUS_PENDING) {
        DBG("Locked\n");
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
      }
    if (NT_SUCCESS(status = irp->IoStatus.Status)) {
        dev->old_state = dev->state;
        dev->state = device__state_started;
      }
    status = STATUS_SUCCESS;
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    *completion = TRUE;
    return status;
  }

NTSTATUS STDCALL bus_pnp__remove_dev(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    IN PIO_STACK_LOCATION io_stack_loc,
    IN struct device__type * dev,
    OUT winvblock__bool_ptr completion
  ) {
    NTSTATUS status;
    struct bus__type * bus;
    struct device__type * walker, * next;
    PDEVICE_OBJECT lower;

    dev->old_state = dev->state;
    dev->state = device__state_deleted;
    irp->IoStatus.Information = 0;
    irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(irp);
    bus = bus__get(dev);
    status = IoCallDriver(bus->LowerDeviceObject, irp);
    walker = bus->first_child;
    while (walker != NULL) {
        next = walker->next_sibling_ptr;
        device__close(walker);
        IoDeleteDevice(walker->Self);
        device__free(walker);
        walker = next;
      }
    bus->Children = 0;
    bus->first_child = NULL;
    lower = bus->LowerDeviceObject;
    if (lower)
      IoDetachDevice(lower);
    IoDeleteDevice(dev->Self);
    device__free(dev);
    *completion = TRUE;
    return status;
  }

NTSTATUS STDCALL bus_pnp__query_dev_relations(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    IN PIO_STACK_LOCATION io_stack_loc,
    IN struct device__type * dev,
    OUT winvblock__bool_ptr completion
  ) {
    NTSTATUS status;
    struct bus__type * bus;
    winvblock__uint32 count;
    struct device__type * walker;
    PDEVICE_RELATIONS dev_relations;
    PDEVICE_OBJECT lower;

    bus = bus__get(dev);
    lower = bus->LowerDeviceObject;
    if (
        io_stack_loc->Parameters.QueryDeviceRelations.Type != BusRelations ||
        irp->IoStatus.Information
      ) {
        status = irp->IoStatus.Status;
        if (lower) {
            IoSkipCurrentIrpStackLocation(irp);
            status = IoCallDriver(lower, irp);
          }
        *completion = TRUE;
        return status;
      }
    probe__disks();
    count = 0;
    walker = bus->first_child;
    while (walker != NULL) {
        count++;
        walker = walker->next_sibling_ptr;
      }
    dev_relations = wv_malloc(
        sizeof *dev_relations + (sizeof (PDEVICE_OBJECT) * count)
      );
    if (dev_relations == NULL) {
        irp->IoStatus.Information = 0;
        status = STATUS_SUCCESS;
        irp->IoStatus.Status = status;
        if (lower) {
            IoSkipCurrentIrpStackLocation(irp);
            status = IoCallDriver(lower, irp);
          }
        *completion = TRUE;
        return status;
      }
    dev_relations->Count = count;

    count = 0;
    walker = bus->first_child;
    while (walker != NULL) {
        ObReferenceObject(walker->Self);
        dev_relations->Objects[count] = walker->Self;
        count++;
        walker = walker->next_sibling_ptr;
      }
    irp->IoStatus.Information = (ULONG_PTR) dev_relations;
    status = STATUS_SUCCESS;
    irp->IoStatus.Status = status;
    if (lower) {
        IoSkipCurrentIrpStackLocation(irp);
        status = IoCallDriver(lower, irp);
      }
    *completion = TRUE;
    return status;
  }

NTSTATUS STDCALL bus_pnp__simple(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    IN PIO_STACK_LOCATION io_stack_loc,
    IN struct device__type * dev,
    OUT winvblock__bool_ptr completion
  ) {
    NTSTATUS status;
    struct bus__type * bus;
    PDEVICE_OBJECT lower;

    bus = bus__get(dev);
    lower = bus->LowerDeviceObject;
    switch (io_stack_loc->MinorFunction) {
        case IRP_MN_QUERY_PNP_DEVICE_STATE:
          irp->IoStatus.Information = 0;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_QUERY_STOP_DEVICE:
          dev->old_state = dev->state;
          dev->state = device__state_stop_pending;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_CANCEL_STOP_DEVICE:
          dev->state = dev->old_state;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_STOP_DEVICE:
          dev->old_state = dev->state;
          dev->state = device__state_stopped;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_QUERY_REMOVE_DEVICE:
          dev->old_state = dev->state;
          dev->state = device__state_remove_pending;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
          dev->state = dev->old_state;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_SURPRISE_REMOVAL:
          dev->old_state = dev->state;
          dev->state = device__state_surprise_remove_pending;
          status = STATUS_SUCCESS;
          break;

        default:
          status = irp->IoStatus.Status;
      }

    irp->IoStatus.Status = status;
    if (lower) {
        IoSkipCurrentIrpStackLocation(irp);
        status = IoCallDriver(lower, irp);
      }
    *completion = TRUE;
    return status;
  }
