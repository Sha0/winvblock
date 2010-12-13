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

/* Forward declarations. */
static device__dispatch_func bus_pnp__start_dev_;
static device__dispatch_func bus_pnp__remove_dev_;
static device__dispatch_func bus_pnp__query_dev_relations_;
static device__pnp_func bus_pnp__simple_;
device__pnp_func bus_pnp__dispatch;

static NTSTATUS STDCALL bus_pnp__io_completion_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    IN PKEVENT event
  ) {
    KeSetEvent(event, 0, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
  }

static NTSTATUS STDCALL bus_pnp__start_dev_(
    IN struct device__type * dev,
    IN PIRP irp
  ) {
    NTSTATUS status;
    KEVENT event;
    struct bus__type * bus = bus__get(dev);
    PDEVICE_OBJECT lower = bus->LowerDeviceObject;

    if (!lower)
      return driver__complete_irp(irp, 0, STATUS_SUCCESS);
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
    return driver__complete_irp(
        irp,
        irp->IoStatus.Information,
        STATUS_SUCCESS
      );
  }

static NTSTATUS STDCALL bus_pnp__remove_dev_(
    IN struct device__type * dev,
    IN PIRP irp
  ) {
    NTSTATUS status = STATUS_SUCCESS;
    struct bus__type * bus = bus__get(dev);
    PDEVICE_OBJECT lower = bus->LowerDeviceObject;
    struct device__type * walker, * next;

    dev->old_state = dev->state;
    dev->state = device__state_deleted;
    /* Pass the IRP on to any lower DEVICE_OBJECT */
    if (lower) {
        irp->IoStatus.Information = 0;
        irp->IoStatus.Status = STATUS_SUCCESS;
        IoSkipCurrentIrpStackLocation(irp);
        status = IoCallDriver(lower, irp);
      }
    /* Remove all children. */
    walker = bus->first_child;
    while (walker != NULL) {
        next = walker->next_sibling_ptr;
        device__close(walker);
        IoDeleteDevice(walker->Self);
        device__free(walker);
        walker = next;
      }
    /* Somewhat redundant, since the bus will be freed shortly. */
    bus->Children = 0;
    bus->first_child = NULL;
    /* Detach from any lower DEVICE_OBJECT */
    if (lower)
      IoDetachDevice(lower);
    /* Delete and free. */
    IoDeleteDevice(dev->Self);
    device__free(dev);
    return status;
  }

static NTSTATUS STDCALL bus_pnp__query_dev_relations_(
    IN struct device__type * dev,
    IN PIRP irp
  ) {
    NTSTATUS status;
    struct bus__type * bus = bus__get(dev);
    PDEVICE_OBJECT lower = bus->LowerDeviceObject;
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    winvblock__uint32 count;
    struct device__type * walker;
    PDEVICE_RELATIONS dev_relations;

    if (
        io_stack_loc->Parameters.QueryDeviceRelations.Type != BusRelations ||
        irp->IoStatus.Information
      ) {
        if (lower) {
            IoSkipCurrentIrpStackLocation(irp);
            return IoCallDriver(lower, irp);
          }
        return driver__complete_irp(
            irp,
            irp->IoStatus.Information,
            irp->IoStatus.Status
          );
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
        /* Couldn't allocate dev_relations, but silently succeed. */
        if (lower) {
            IoSkipCurrentIrpStackLocation(irp);
            return IoCallDriver(lower, irp);
          }
        return driver__complete_irp(irp, 0, STATUS_SUCCESS);
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
    irp->IoStatus.Status = status = STATUS_SUCCESS;
    if (lower) {
        IoSkipCurrentIrpStackLocation(irp);
        return IoCallDriver(lower, irp);
      }
    return driver__complete_irp(irp, irp->IoStatus.Information, status);
  }

static NTSTATUS STDCALL bus_pnp__simple_(
    IN struct device__type * dev,
    IN PIRP irp,
    IN UCHAR code
  ) {
    NTSTATUS status;
    struct bus__type * bus = bus__get(dev);
    PDEVICE_OBJECT lower = bus->LowerDeviceObject;

    switch (code) {
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
        return IoCallDriver(lower, irp);
      }
    return driver__complete_irp(irp, irp->IoStatus.Information, status);
  }

/* Bus PnP dispatch routine. */
NTSTATUS STDCALL bus_pnp__dispatch(
    IN struct device__type * dev,
    IN PIRP irp,
    IN UCHAR code
  ) {
    switch (code) {
        case IRP_MN_QUERY_ID:
          return device__pnp_query_id(dev, irp);

        case IRP_MN_QUERY_DEVICE_RELATIONS:
          return bus_pnp__query_dev_relations_(dev, irp);

        case IRP_MN_REMOVE_DEVICE:
          return bus_pnp__remove_dev_(dev, irp);

        case IRP_MN_START_DEVICE:
          return bus_pnp__start_dev_(dev, irp);

        default:
          return bus_pnp__simple_(dev, irp, code);
      }
  }
