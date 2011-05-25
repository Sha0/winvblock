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
 * Disk PnP IRP handling.
 */

#include <stdio.h>
#include <ntddk.h>
#include <initguid.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "irp.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "debug.h"

/* Forward declarations. */
static WVL_F_DISK_PNP WvlDiskPnpQueryDevRelations_;
static WVL_F_DISK_PNP WvlDiskPnpQueryBusInfo_;
static WVL_F_DISK_PNP WvlDiskPnpQueryCapabilities_;
static WVL_F_DISK_PNP WvlDiskPnpSimple_;

static NTSTATUS STDCALL WvlDiskPnpQueryDevRelations_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    WVL_SP_DISK_T disk
  ) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;
    PDEVICE_RELATIONS dev_relations;

    if (io_stack_loc->Parameters.QueryDeviceRelations.Type !=
        TargetDeviceRelation
      ) {
        status = irp->IoStatus.Status;
        goto out;
      }
    dev_relations = wv_palloc(sizeof *dev_relations + sizeof dev_obj);
    if (dev_relations == NULL) {
        DBG("wv_palloc IRP_MN_QUERY_DEVICE_RELATIONS\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto alloc_dev_relations;
      }
    dev_relations->Objects[0] = dev_obj;
    dev_relations->Count = 1;
    ObReferenceObject(dev_obj);
    irp->IoStatus.Information = (ULONG_PTR) dev_relations;
    status = STATUS_SUCCESS;

    /* irp->IoStatus.Information (dev_relations) not freed. */
    alloc_dev_relations:

    out:

    return WvlIrpComplete(irp, irp->IoStatus.Information, status);
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

static NTSTATUS STDCALL WvlDiskPnpQueryBusInfo_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    WVL_SP_DISK_T disk
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

    return WvlIrpComplete(irp, irp->IoStatus.Information, status);
  }

/* From driver.c */
extern NTSTATUS STDCALL WvDriverGetDevCapabilities(
    IN PDEVICE_OBJECT,
    IN PDEVICE_CAPABILITIES
  );

static NTSTATUS STDCALL WvlDiskPnpQueryCapabilities_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    WVL_SP_DISK_T disk
  ) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    PDEVICE_CAPABILITIES DeviceCapabilities =
      io_stack_loc->Parameters.DeviceCapabilities.Capabilities;
    NTSTATUS status;
    DEVICE_CAPABILITIES ParentDeviceCapabilities;

    if (DeviceCapabilities->Version != 1 ||
        DeviceCapabilities->Size < sizeof (DEVICE_CAPABILITIES)
      ) {
        status = STATUS_UNSUCCESSFUL;
        goto out;
      }
    status = WvDriverGetDevCapabilities(
        disk->ParentBus,
        &ParentDeviceCapabilities
      );
    if (!NT_SUCCESS(status))
      goto out;

    RtlCopyMemory(
        DeviceCapabilities->DeviceState,
        ParentDeviceCapabilities.DeviceState,
        (PowerSystemShutdown + 1) * sizeof (DEVICE_POWER_STATE)
      );
    DeviceCapabilities->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    if (DeviceCapabilities->DeviceState[PowerSystemSleeping1] != PowerDeviceD0)
      DeviceCapabilities->DeviceState[PowerSystemSleeping1] = PowerDeviceD1;
    if (DeviceCapabilities->DeviceState[PowerSystemSleeping2] != PowerDeviceD0)
      DeviceCapabilities->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
    #if 0
    if (DeviceCapabilities->DeviceState[PowerSystemSleeping3] != PowerDeviceD0)
      DeviceCapabilities->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
    #endif
    DeviceCapabilities->DeviceWake = PowerDeviceD1;
    DeviceCapabilities->DeviceD1 = TRUE;
    DeviceCapabilities->DeviceD2 = FALSE;
    DeviceCapabilities->WakeFromD0 = FALSE;
    DeviceCapabilities->WakeFromD1 = FALSE;
    DeviceCapabilities->WakeFromD2 = FALSE;
    DeviceCapabilities->WakeFromD3 = FALSE;
    DeviceCapabilities->D1Latency = 0;
    DeviceCapabilities->D2Latency = 0;
    DeviceCapabilities->D3Latency = 0;
    DeviceCapabilities->EjectSupported = FALSE;
    DeviceCapabilities->HardwareDisabled = FALSE;
    DeviceCapabilities->Removable = WvlDiskIsRemovable[disk->Media];
    DeviceCapabilities->SurpriseRemovalOK = FALSE;
    DeviceCapabilities->UniqueID = FALSE;
    DeviceCapabilities->SilentInstall = FALSE;
    #if 0
    DeviceCapabilities->Address = dev_obj->SerialNo;
    DeviceCapabilities->UINumber = dev_obj->SerialNo;
    #endif
    status = STATUS_SUCCESS;

    out:
    return WvlIrpComplete(irp, irp->IoStatus.Information, status);
  }

static NTSTATUS STDCALL WvlDiskPnpSimple_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    WVL_SP_DISK_T disk
  ) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;

    switch (io_stack_loc->MinorFunction) {
        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
          DBG("IRP_MN_DEVICE_USAGE_NOTIFICATION\n");
          if (disk->DenyPageFile)
            return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
          if (io_stack_loc->Parameters.UsageNotification.InPath) {
              disk->SpecialFileCount++;
            } else {
              disk->SpecialFileCount--;
            }
          irp->IoStatus.Information = 0;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
          DBG("IRP_MN_QUERY_PNP_DEVICE_STATE\n");
          status = irp->IoStatus.Status;
          IoCompleteRequest(irp, IO_NO_INCREMENT);
          return status;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
          DBG("IRP_MN_QUERY_RESOURCE_REQUIREMENTS\n");
          status = irp->IoStatus.Status;
          IoCompleteRequest(irp, IO_NO_INCREMENT);
          return status;

        case IRP_MN_QUERY_RESOURCES:
          DBG("IRP_MN_QUERY_RESOURCES\n");
          status = irp->IoStatus.Status;
          IoCompleteRequest(irp, IO_NO_INCREMENT);
          return status;

        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
          DBG("IRP_MN_FILTER_RESOURCE_REQUIREMENTS\n");
          status = irp->IoStatus.Status;
          IoCompleteRequest(irp, IO_NO_INCREMENT);
          return status;

        case IRP_MN_START_DEVICE:
          DBG("IRP_MN_START_DEVICE\n");
          disk->OldState = disk->State;
          disk->State = WvlDiskStateStarted;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_QUERY_STOP_DEVICE:
          DBG("IRP_MN_QUERY_STOP_DEVICE\n");
          disk->OldState = disk->State;
          disk->State = WvlDiskStateStopPending;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_CANCEL_STOP_DEVICE:
          DBG("IRP_MN_CANCEL_STOP_DEVICE\n");
          disk->State = disk->OldState;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_STOP_DEVICE:
          DBG("IRP_MN_STOP_DEVICE\n");
          disk->OldState = disk->State;
          disk->State = WvlDiskStateStopped;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_QUERY_REMOVE_DEVICE:
          DBG("IRP_MN_QUERY_REMOVE_DEVICE\n");
          disk->OldState = disk->State;
          disk->State = WvlDiskStateRemovePending;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_REMOVE_DEVICE:
          DBG("IRP_MN_REMOVE_DEVICE\n");
          disk->OldState = disk->State;
          disk->State = WvlDiskStateNotStarted;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
          DBG("IRP_MN_CANCEL_REMOVE_DEVICE\n");
          disk->State = disk->OldState;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_SURPRISE_REMOVAL:
          DBG("IRP_MN_SURPRISE_REMOVAL\n");
          disk->OldState = disk->State;
          disk->State = WvlDiskStateSurpriseRemovePending;
          status = STATUS_SUCCESS;
          break;

        default:
          DBG("Unhandled IRP_MN_*: %d\n", io_stack_loc->MinorFunction);
          status = irp->IoStatus.Status;
      }

    return WvlIrpComplete(irp, irp->IoStatus.Information, status);
  }

/**
 * Disk PnP dispatch routine.
 *
 * @v DevObj            The disk PDO to process the IRP with.
 * @v Irp               The IRP to process.
 * @v Disk              The disk to process the IRP with.
 * @ret NTSTATUS        The status of the operation.
 */
WVL_M_LIB NTSTATUS STDCALL WvlDiskPnp(
    IN PDEVICE_OBJECT DevObj,
    IN PIRP Irp,
    WVL_SP_DISK_T Disk
  ) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(Irp);

    switch (io_stack_loc->MinorFunction) {
        case IRP_MN_QUERY_ID:
          DBG("IRP_MN_QUERY_ID\n");
          if (Disk->disk_ops.PnpQueryId)
            return Disk->disk_ops.PnpQueryId(DevObj, Irp, Disk);
          return WvlIrpComplete(Irp, 0, STATUS_NOT_SUPPORTED);

        case IRP_MN_QUERY_DEVICE_TEXT:
          DBG("IRP_MN_QUERY_DEVICE_TEXT\n");
          if (Disk->disk_ops.PnpQueryDevText)
            return Disk->disk_ops.PnpQueryDevText(DevObj, Irp, Disk);
          return WvlIrpComplete(Irp, 0, STATUS_NOT_SUPPORTED);

        case IRP_MN_QUERY_DEVICE_RELATIONS:
          DBG("IRP_MN_QUERY_DEVICE_RELATIONS\n");
          return WvlDiskPnpQueryDevRelations_(DevObj, Irp, Disk);

        case IRP_MN_QUERY_BUS_INFORMATION:
          DBG("IRP_MN_QUERY_BUS_INFORMATION\n");
          return WvlDiskPnpQueryBusInfo_(DevObj, Irp, Disk);

        case IRP_MN_QUERY_CAPABILITIES:
          DBG("IRP_MN_QUERY_CAPABILITIES\n");
          return WvlDiskPnpQueryCapabilities_(DevObj, Irp, Disk);

        default:
          return WvlDiskPnpSimple_(DevObj, Irp, Disk);
      }
  }
