/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "driver.h"
#include "bus.h"
#include "debug.h"
#include "probe.h"

static NTSTATUS STDCALL WvlBusPnpIoCompletion(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    IN PKEVENT event
  ) {
    KeSetEvent(event, 0, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
  }

static NTSTATUS STDCALL WvlBusPnpStartDev(IN WVL_SP_BUS_T bus, IN PIRP irp) {
    NTSTATUS status;
    KEVENT event;
    PDEVICE_OBJECT lower = bus->LowerDeviceObject;

    if (!lower)
      return driver__complete_irp(irp, 0, STATUS_SUCCESS);
    KeInitializeEvent(&event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(irp);
    IoSetCompletionRoutine(
        irp,
        (PIO_COMPLETION_ROUTINE) WvlBusPnpIoCompletion,
        (PVOID) &event,
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
        bus->OldState = bus->State;
        bus->State = WvlBusStateStarted;
      }
    return driver__complete_irp(
        irp,
        irp->IoStatus.Information,
        STATUS_SUCCESS
      );
  }

static NTSTATUS STDCALL WvlBusPnpRemoveDev(IN WVL_SP_BUS_T bus, IN PIRP irp) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;
    PDEVICE_OBJECT lower;
    PLIST_ENTRY node_link;

    if (!(io_stack_loc->Control & SL_PENDING_RETURNED)) {
        /* Enqueue the IRP. */
        status = WvlBusEnqueueIrp(bus, irp);
        if (status != STATUS_PENDING)
          /* Problem. */
          return driver__complete_irp(irp, 0, status);
        /* Ok. */
        return status;
      }
    /* If we get here, we should be called by WvlBusProcessWorkItems() */
    status = STATUS_SUCCESS;
    lower = bus->LowerDeviceObject;
    bus->OldState = bus->State;
    bus->State = WvlBusStateDeleted;
    /* Pass the IRP on to any lower DEVICE_OBJECT */
    if (lower) {
        irp->IoStatus.Information = 0;
        irp->IoStatus.Status = STATUS_SUCCESS;
        IoSkipCurrentIrpStackLocation(irp);
        status = IoCallDriver(lower, irp);
      }
    /* Remove all children. */
    node_link = &bus->BusPrivate_.Nodes;
    while ((node_link = node_link->Flink) != &bus->BusPrivate_.Nodes) {
        WVL_SP_BUS_NODE node = CONTAINING_RECORD(
            node_link,
            WVL_S_BUS_NODE,
            BusPrivate_.Link
          );

        DBG("Removing PDO from bus...\n");
        RemoveEntryList(&node->BusPrivate_.Link);
        ObDereferenceObject(node->BusPrivate_.Pdo);
        bus->BusPrivate_.NodeCount--;
      }
    /* Detach from any lower DEVICE_OBJECT */
    if (lower)
      IoDetachDevice(lower);
    /* Delete. */
    IoDeleteDevice(bus->Fdo);
    /* Stop the thread. */
    bus->Stop = TRUE;
    return status;
  }

static NTSTATUS STDCALL WvlBusPnpQueryDevRelations(
    IN WVL_SP_BUS_T bus,
    IN PIRP irp
  ) {
    NTSTATUS status;
    PDEVICE_OBJECT lower = bus->LowerDeviceObject;
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    UINT32 i;
    PDEVICE_RELATIONS dev_relations;
    PLIST_ENTRY node_link;

    if (!(io_stack_loc->Control & SL_PENDING_RETURNED)) {
        /* Enqueue the IRP. */
        status = WvlBusEnqueueIrp(bus, irp);
        if (status != STATUS_PENDING)
          /* Problem. */
          return driver__complete_irp(irp, 0, status);
        /* Ok. */
        return status;
      }
    /* If we get here, we should be called by WvlBusProcessWorkItems() */
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
    WvProbeDisks();
    dev_relations = wv_malloc(
        sizeof *dev_relations +
          (sizeof (PDEVICE_OBJECT) * bus->BusPrivate_.NodeCount)
      );
    if (dev_relations == NULL) {
        /* Couldn't allocate dev_relations, but silently succeed. */
        if (lower) {
            IoSkipCurrentIrpStackLocation(irp);
            return IoCallDriver(lower, irp);
          }
        return driver__complete_irp(irp, 0, STATUS_SUCCESS);
      }
    dev_relations->Count = bus->BusPrivate_.NodeCount;

    i = 0;
    node_link = &bus->BusPrivate_.Nodes;
    while ((node_link = node_link->Flink) != &bus->BusPrivate_.Nodes) {
        WVL_SP_BUS_NODE node = CONTAINING_RECORD(
            node_link,
            WVL_S_BUS_NODE,
            BusPrivate_.Link
          );

        dev_relations->Objects[i] = node->BusPrivate_.Pdo;
        i++;
      }
    irp->IoStatus.Information = (ULONG_PTR) dev_relations;
    irp->IoStatus.Status = status = STATUS_SUCCESS;
    if (lower) {
        IoSkipCurrentIrpStackLocation(irp);
        return IoCallDriver(lower, irp);
      }
    return driver__complete_irp(irp, irp->IoStatus.Information, status);
  }

static NTSTATUS STDCALL WvlBusPnpQueryCapabilities(
    IN WVL_SP_BUS_T bus,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    PDEVICE_CAPABILITIES DeviceCapabilities =
      io_stack_loc->Parameters.DeviceCapabilities.Capabilities;
    NTSTATUS status;
    DEVICE_CAPABILITIES ParentDeviceCapabilities;
    PDEVICE_OBJECT lower;

    if (DeviceCapabilities->Version != 1 ||
        DeviceCapabilities->Size < sizeof (DEVICE_CAPABILITIES)
      )
      return driver__complete_irp(irp, 0, STATUS_UNSUCCESSFUL);
    /* Let the lower DEVICE_OBJECT handle the IRP. */
    lower = bus->LowerDeviceObject;
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(lower, irp);
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

static NTSTATUS STDCALL WvlBusPnpQueryBusInfo(
    IN WVL_SP_BUS_T bus,
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

static NTSTATUS STDCALL WvlBusPnpSimple(
    IN WVL_SP_BUS_T bus,
    IN PIRP irp,
    IN UCHAR code
  ) {
    NTSTATUS status;
    PDEVICE_OBJECT lower = bus->LowerDeviceObject;

    DBG("WvlBusPnpSimple(): ");
    switch (code) {
        case IRP_MN_QUERY_PNP_DEVICE_STATE:
          DBG("IRP_MN_QUERY_PNP_DEVICE_STATE\n");
          irp->IoStatus.Information = 0;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_QUERY_STOP_DEVICE:
          DBG("IRP_MN_QUERY_STOP_DEVICE\n");
          bus->OldState = bus->State;
          bus->State = WvlBusStateStopPending;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_CANCEL_STOP_DEVICE:
          DBG("IRP_MN_CANCEL_STOP_DEVICE\n");
          bus->State = bus->OldState;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_STOP_DEVICE:
          DBG("IRP_MN_STOP_DEVICE\n");
          bus->OldState = bus->State;
          bus->State = WvlBusStateStopped;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_QUERY_REMOVE_DEVICE:
          DBG("IRP_MN_QUERY_REMOVE_DEVICE\n");
          bus->OldState = bus->State;
          bus->State = WvlBusStateRemovePending;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
          DBG("IRP_MN_CANCEL_REMOVE_DEVICE\n");
          bus->State = bus->OldState;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_SURPRISE_REMOVAL:
          DBG("IRP_MN_SURPRISE_REMOVAL\n");
          bus->OldState = bus->State;
          bus->State = WvlBusStateSurpriseRemovePending;
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
    if (lower) {
        IoSkipCurrentIrpStackLocation(irp);
        return IoCallDriver(lower, irp);
      }
    return driver__complete_irp(irp, irp->IoStatus.Information, status);
  }

/* Bus PnP dispatch routine. */
WVL_M_LIB NTSTATUS STDCALL WvlBusPnpIrp(
    IN WVL_SP_BUS_T Bus,
    IN PIRP Irp,
    IN UCHAR Code
  ) {
    DBG("WvlBusPnpIrp(): ");
    switch (Code) {
        case IRP_MN_QUERY_DEVICE_TEXT:
          DBG("IRP_MN_QUERY_DEVICE_TEXT\n");
          return Bus->QueryDevText(Bus, Irp);

        case IRP_MN_QUERY_BUS_INFORMATION:
          DBG("IRP_MN_QUERY_BUS_INFORMATION\n");
          return WvlBusPnpQueryBusInfo(Bus, Irp);

        case IRP_MN_QUERY_DEVICE_RELATIONS:
          DBG("IRP_MN_QUERY_DEVICE_RELATIONS\n");
          return WvlBusPnpQueryDevRelations(Bus, Irp);

        case IRP_MN_QUERY_CAPABILITIES:
          DBG("IRP_MN_QUERY_CAPABILITIES\n");
          return WvlBusPnpQueryCapabilities(Bus, Irp);

        case IRP_MN_REMOVE_DEVICE:
          DBG("IRP_MN_REMOVE_DEVICE\n");
          return WvlBusPnpRemoveDev(Bus, Irp);

        case IRP_MN_START_DEVICE:
          DBG("IRP_MN_START_DEVICE\n");
          return WvlBusPnpStartDev(Bus, Irp);

        default:
          DBG("Simple\n");
          return WvlBusPnpSimple(Bus, Irp, Code);
      }
  }
