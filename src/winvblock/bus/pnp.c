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

#include <stdio.h>
#include <ntddk.h>
#include <initguid.h>

#include "winvblock.h"
#include "wv_stdlib.h"
#include "portable.h"
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
static device__dispatch_func bus_pnp__query_capabilities_;
static device__dispatch_func bus_pnp__query_dev_text_;
static device__dispatch_func bus_pnp__query_bus_info_;
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
    WV_SP_BUS_T bus = bus__get(dev);
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
    WV_SP_BUS_T bus = bus__get(dev);
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
    WV_SP_BUS_T bus = bus__get(dev);
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

static NTSTATUS STDCALL bus_pnp__query_capabilities_(
    IN struct device__type * dev,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    PDEVICE_CAPABILITIES DeviceCapabilities =
      io_stack_loc->Parameters.DeviceCapabilities.Capabilities;
    NTSTATUS status;
    WV_SP_BUS_T bus = bus__get(dev);
    DEVICE_CAPABILITIES ParentDeviceCapabilities;
    PDEVICE_OBJECT lower;

    if (DeviceCapabilities->Version != 1 ||
        DeviceCapabilities->Size < sizeof (DEVICE_CAPABILITIES)
      )
      return driver__complete_irp(irp, 0, STATUS_UNSUCCESSFUL);
    /* If there's a lower DEVICE_OBJECT, let it handle the IRP. */
    lower = bus->LowerDeviceObject;
    if (lower) {
        IoSkipCurrentIrpStackLocation(irp);
        return IoCallDriver(lower, irp);
      }
    /* Otherwise, return our parent's capabilities. */
    status = bus__get_dev_capabilities(
        dev->Parent,
        DeviceCapabilities
      );
    if (!NT_SUCCESS(status))
      return driver__complete_irp(irp, 0, status);
    return driver__complete_irp(irp, irp->IoStatus.Information, STATUS_SUCCESS);
  }

static NTSTATUS STDCALL bus_pnp__query_dev_text_(
    IN struct device__type * dev,
    IN PIRP irp
  ) {
    WV_SP_BUS_T bus = bus__get(dev);
    WCHAR (*str)[512];
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;
    winvblock__uint32 str_len;

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
          str_len = swprintf(*str, winvblock__literal_w L" Bus") + 1;
          irp->IoStatus.Information =
            (ULONG_PTR) wv_palloc(str_len * sizeof *str);
          if (irp->IoStatus.Information == 0) {
              DBG("wv_palloc DeviceTextDescription\n");
              status = STATUS_INSUFFICIENT_RESOURCES;
              goto alloc_info;
            }
          RtlCopyMemory(
              (PWCHAR) irp->IoStatus.Information,
              str,
              str_len * sizeof (WCHAR)
            );
          status = STATUS_SUCCESS;
          goto alloc_info;

        case DeviceTextLocationInformation:
          str_len = device__pnp_id(
              dev,
              BusQueryInstanceID,
              str
            );
          irp->IoStatus.Information =
            (ULONG_PTR) wv_palloc(str_len * sizeof *str);
          if (irp->IoStatus.Information == 0) {
              DBG("wv_palloc DeviceTextLocationInformation\n");
              status = STATUS_INSUFFICIENT_RESOURCES;
              goto alloc_info;
            }
          RtlCopyMemory(
              (PWCHAR) irp->IoStatus.Information,
              str,
              str_len * sizeof (WCHAR)
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

    return driver__complete_irp(irp, irp->IoStatus.Information, status);
  }

DEFINE_GUID(
    GUID_BUS_TYPE_INTERNAL,
    0x2530ea73L,
    0x086b,
    0x11d1,
    0xa0,
    0x9f,
    0x00,
    0xc0,
    0x4f,
    0xc3,
    0x40,
    0xb1
  );

static NTSTATUS STDCALL bus_pnp__query_bus_info_(
    IN struct device__type * dev,
    IN PIRP irp
  ) {
    PPNP_BUS_INFORMATION pnp_bus_info;
    NTSTATUS status;

    pnp_bus_info = wv_palloc(sizeof *pnp_bus_info);
    if (pnp_bus_info == NULL) {
        DBG("wv_palloc IRP_MN_QUERY_BUS_INFORMATION\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto alloc_pnp_bus_info;
      }
    pnp_bus_info->BusTypeGuid = GUID_BUS_TYPE_INTERNAL;
    pnp_bus_info->LegacyBusType = PNPBus;
    pnp_bus_info->BusNumber = 0;
    irp->IoStatus.Information = (ULONG_PTR) pnp_bus_info;
    status = STATUS_SUCCESS;

    /* irp-IoStatus.Information (pnp_bus_info) not freed. */
    alloc_pnp_bus_info:

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

static NTSTATUS STDCALL bus_pnp__simple_(
    IN struct device__type * dev,
    IN PIRP irp,
    IN UCHAR code
  ) {
    NTSTATUS status;
    WV_SP_BUS_T bus = bus__get(dev);
    PDEVICE_OBJECT lower = bus->LowerDeviceObject;

    switch (code) {
        case IRP_MN_QUERY_PNP_DEVICE_STATE:
          DBG("bus_pnp: IRP_MN_QUERY_PNP_DEVICE_STATE\n");
          irp->IoStatus.Information = 0;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_QUERY_STOP_DEVICE:
          DBG("bus_pnp: IRP_MN_QUERY_STOP_DEVICE\n");
          dev->old_state = dev->state;
          dev->state = device__state_stop_pending;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_CANCEL_STOP_DEVICE:
          DBG("bus_pnp: IRP_MN_CANCEL_STOP_DEVICE\n");
          dev->state = dev->old_state;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_STOP_DEVICE:
          DBG("bus_pnp: IRP_MN_STOP_DEVICE\n");
          dev->old_state = dev->state;
          dev->state = device__state_stopped;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_QUERY_REMOVE_DEVICE:
          DBG("bus_pnp: IRP_MN_QUERY_REMOVE_DEVICE\n");
          dev->old_state = dev->state;
          dev->state = device__state_remove_pending;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
          DBG("bus_pnp: IRP_MN_CANCEL_REMOVE_DEVICE\n");
          dev->state = dev->old_state;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_SURPRISE_REMOVAL:
          DBG("bus_pnp: IRP_MN_SURPRISE_REMOVAL\n");
          dev->old_state = dev->state;
          dev->state = device__state_surprise_remove_pending;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_QUERY_RESOURCES:
        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
          DBG("bus_pnp: IRP_MN_QUERY_RESOURCE*\n");
          IoCompleteRequest(irp, IO_NO_INCREMENT);
          return STATUS_SUCCESS;
          
        default:
          DBG("bus_pnp: Unhandled IRP_MN_*: %d\n", code);
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
          DBG("bus_pnp: IRP_MN_QUERY_ID\n");
          return device__pnp_query_id(dev, irp);

        case IRP_MN_QUERY_DEVICE_TEXT:
          DBG("bus_pnp: IRP_MN_QUERY_DEVICE_TEXT\n");
          return bus_pnp__query_dev_text_(dev, irp);

        case IRP_MN_QUERY_BUS_INFORMATION:
          DBG("bus_pnp: IRP_MN_QUERY_BUS_INFORMATION\n");
          return bus_pnp__query_bus_info_(dev, irp);

        case IRP_MN_QUERY_DEVICE_RELATIONS:
          DBG("bus_pnp: IRP_MJ_QUERY_DEVICE_RELATIONS\n");
          return bus_pnp__query_dev_relations_(dev, irp);

        case IRP_MN_QUERY_CAPABILITIES:
          DBG("bus_pnp: IRP_MN_QUERY_CAPABILITIES\n");
          return bus_pnp__query_capabilities_(dev, irp);

        case IRP_MN_REMOVE_DEVICE:
          DBG("bus_pnp: IRP_MN_REMOVE_DEVICE\n");
          return bus_pnp__remove_dev_(dev, irp);

        case IRP_MN_START_DEVICE:
          DBG("bus_pnp: IRP_MN_START_DEVICE\n");
          return bus_pnp__start_dev_(dev, irp);

        default:
          return bus_pnp__simple_(dev, irp, code);
      }
  }
