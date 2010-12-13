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
 * Disk PnP IRP handling.
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

/* Forward declarations. */
static device__dispatch_func disk_pnp__query_dev_text_;
static device__dispatch_func disk_pnp__query_dev_relations_;
static device__dispatch_func disk_pnp__query_bus_info_;
static device__dispatch_func disk_pnp__query_capabilities_;
static device__pnp_func disk_pnp__simple_;
device__pnp_func disk_pnp__dispatch;

static NTSTATUS STDCALL disk_pnp__query_dev_text_(
    IN struct device__type * dev,
    IN PIRP irp
  ) {
    disk__type_ptr disk;
    WCHAR (*str)[512];
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;
    winvblock__uint32 str_len;

    disk = disk__get_ptr(dev);
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
          str_len = swprintf(*str, winvblock__literal_w L" Disk") + 1;
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

static NTSTATUS STDCALL disk_pnp__query_dev_relations_(
    IN struct device__type * dev,
    IN PIRP irp
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
    dev_relations = wv_palloc(sizeof *dev_relations + sizeof dev->Self);
    if (dev_relations == NULL) {
        DBG("wv_palloc IRP_MN_QUERY_DEVICE_RELATIONS\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto alloc_dev_relations;
      }
    dev_relations->Objects[0] = dev->Self;
    dev_relations->Count = 1;
    ObReferenceObject(dev->Self);
    irp->IoStatus.Information = (ULONG_PTR) dev_relations;
    status = STATUS_SUCCESS;

    /* irp->IoStatus.Information (dev_relations) not freed. */
    alloc_dev_relations:

    out:

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
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

static NTSTATUS STDCALL disk_pnp__query_bus_info_(
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

static NTSTATUS STDCALL disk_pnp__query_capabilities_(
    IN struct device__type * dev,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    PDEVICE_CAPABILITIES DeviceCapabilities =
      io_stack_loc->Parameters.DeviceCapabilities.Capabilities;
    NTSTATUS status;
    disk__type_ptr disk;
    struct bus__type * bus;
    DEVICE_CAPABILITIES ParentDeviceCapabilities;
    PDEVICE_OBJECT bus_lower;

    if (DeviceCapabilities->Version != 1 ||
        DeviceCapabilities->Size < sizeof (DEVICE_CAPABILITIES)
      ) {
        status = STATUS_UNSUCCESSFUL;
        goto out;
      }
    disk = disk__get_ptr(dev);
    bus = bus__get(device__get(dev->Parent));
    bus_lower = bus->LowerDeviceObject;
    if (bus_lower) {
        status = bus__get_dev_capabilities(
            bus_lower,
            &ParentDeviceCapabilities
          );
      } else {
        status = bus__get_dev_capabilities(
            bus->device->Self,
            &ParentDeviceCapabilities
          );
      }      
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
    DeviceCapabilities->Removable = disk__removable[disk->media];
    DeviceCapabilities->SurpriseRemovalOK = FALSE;
    DeviceCapabilities->UniqueID = FALSE;
    DeviceCapabilities->SilentInstall = FALSE;
    #if 0
    DeviceCapabilities->Address = dev->Self->SerialNo;
    DeviceCapabilities->UINumber = dev->Self->SerialNo;
    #endif
    status = STATUS_SUCCESS;

    out:
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

static NTSTATUS STDCALL disk_pnp__simple_(
    IN struct device__type * dev,
    IN PIRP irp,
    IN UCHAR code
  ) {
    disk__type_ptr disk = disk__get_ptr(dev);
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;

    switch (code) {
        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
          if (io_stack_loc->Parameters.UsageNotification.InPath) {
              disk->SpecialFileCount++;
            } else {
              disk->SpecialFileCount--;
            }
          irp->IoStatus.Information = 0;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
          irp->IoStatus.Information = 0;
          status = STATUS_SUCCESS;
          break;

        case IRP_MN_START_DEVICE:
          dev->old_state = dev->state;
          dev->state = device__state_started;
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

        case IRP_MN_REMOVE_DEVICE:
          dev->old_state = dev->state;
          dev->state = device__state_not_started;
          if (disk->Unmount) {
              device__close(dev);
              IoDeleteDevice(dev->Self);
              device__free(dev);
              status = STATUS_NO_SUCH_DEVICE;
            } else {
              status = STATUS_SUCCESS;
            }
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
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

/* Disk PnP dispatch routine. */
NTSTATUS STDCALL disk_pnp__dispatch(
    IN struct device__type * dev,
    IN PIRP irp,
    IN UCHAR code
  ) {
    switch (code) {
        case IRP_MN_QUERY_ID:
          return device__pnp_query_id(dev, irp);

        case IRP_MN_QUERY_DEVICE_TEXT:
          return disk_pnp__query_dev_text_(dev, irp);

        case IRP_MN_QUERY_DEVICE_RELATIONS:
          return disk_pnp__query_dev_relations_(dev, irp);

        case IRP_MN_QUERY_BUS_INFORMATION:
          return disk_pnp__query_bus_info_(dev, irp);

        case IRP_MN_QUERY_CAPABILITIES:
          return disk_pnp__query_capabilities_(dev, irp);

        default:
          return disk_pnp__simple_(dev, irp, code);
      }
  }
