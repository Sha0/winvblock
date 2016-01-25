/**
 * Copyright (C) 2016, Synthetel Corporation.
 *   Author: Shao Miller <winvblock@synthetel.com>
 * Copyright (C) 2012, Shao Miller <sha0.miller@gmail.com>.
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
 * File-backed disk PDO PnP IRP handling
 */

#include <stdio.h>
#include <ntifs.h>
#include <ntddk.h>
#include <initguid.h>

#include "portable.h"
#include "winvblock.h"
#include "thread.h"
#include "wv_stdlib.h"
#include "irp.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "debug.h"
#include "filedisk.h"

/** Function declarations */

__drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH WvFilediskPnpIrp;
__drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH WvFilediskPnpUnsupported;

__drv_dispatchType(IRP_MN_START_DEVICE) DRIVER_DISPATCH
  WvFilediskPnpStartDevice;
__drv_dispatchType(IRP_MN_QUERY_REMOVE_DEVICE) DRIVER_DISPATCH
  WvFilediskPnpQueryRemoveDevice;
__drv_dispatchType(IRP_MN_REMOVE_DEVICE) DRIVER_DISPATCH
  WvFilediskPnpRemoveDevice;
__drv_dispatchType(IRP_MN_CANCEL_REMOVE_DEVICE) DRIVER_DISPATCH
  WvFilediskPnpCancelRemoveDevice;
__drv_dispatchType(IRP_MN_STOP_DEVICE) DRIVER_DISPATCH WvFilediskPnpStopDevice;
__drv_dispatchType(IRP_MN_QUERY_STOP_DEVICE) DRIVER_DISPATCH
  WvFilediskPnpQueryStopDevice;
__drv_dispatchType(IRP_MN_CANCEL_STOP_DEVICE) DRIVER_DISPATCH
  WvFilediskPnpCancelStopDevice;
__drv_dispatchType(IRP_MN_QUERY_DEVICE_RELATIONS) DRIVER_DISPATCH
  WvFilediskPnpQueryDeviceRelations;
__drv_dispatchType(IRP_MN_QUERY_CAPABILITIES) DRIVER_DISPATCH
  WvFilediskPnpQueryCapabilities;
__drv_dispatchType(IRP_MN_QUERY_DEVICE_TEXT) DRIVER_DISPATCH
  WvFilediskPnpQueryDeviceText;
__drv_dispatchType(IRP_MN_QUERY_ID) DRIVER_DISPATCH WvFilediskPnpQueryId;
__drv_dispatchType(IRP_MN_QUERY_BUS_INFORMATION) DRIVER_DISPATCH
  WvFilediskPnpQueryBusInfo;
__drv_dispatchType(IRP_MN_DEVICE_USAGE_NOTIFICATION) DRIVER_DISPATCH
  WvFilediskPnpDeviceUsageNotification;
__drv_dispatchType(IRP_MN_SURPRISE_REMOVAL) DRIVER_DISPATCH
  WvFilediskPnpSurpriseRemoval;

/** Objects */

/** PnP handler table */
static DRIVER_DISPATCH * WvFilediskPnpHandlerTable[] = {
    /* IRP_MN_START_DEVICE */
    WvFilediskPnpStartDevice,
    /* IRP_MN_QUERY_REMOVE_DEVICE */
    WvFilediskPnpQueryRemoveDevice,
    /* IRP_MN_REMOVE_DEVICE */
    WvFilediskPnpRemoveDevice,
    /* IRP_MN_CANCEL_REMOVE_DEVICE */
    WvFilediskPnpCancelRemoveDevice,
    /* IRP_MN_STOP_DEVICE */
    WvFilediskPnpStopDevice,
    /* IRP_MN_QUERY_STOP_DEVICE */
    WvFilediskPnpQueryStopDevice,
    /* IRP_MN_CANCEL_STOP_DEVICE */
    WvFilediskPnpCancelStopDevice,
    /* IRP_MN_QUERY_DEVICE_RELATIONS */
    WvFilediskPnpQueryDeviceRelations,
    /* IRP_MN_QUERY_INTERFACE */
    WvFilediskPnpUnsupported,
    /* IRP_MN_QUERY_CAPABILITIES */
    WvFilediskPnpQueryCapabilities,
    /* IRP_MN_QUERY_RESOURCES */
    WvFilediskPnpUnsupported,
    /* IRP_MN_QUERY_RESOURCE_REQUIREMENTS */
    WvFilediskPnpUnsupported,
    /* IRP_MN_QUERY_DEVICE_TEXT */
    WvFilediskPnpQueryDeviceText,
    /* IRP_MN_FILTER_RESOURCE_REQUIREMENTS */
    WvFilediskPnpUnsupported,
    /* Unknown */
    WvFilediskPnpUnsupported,
    /* IRP_MN_READ_CONFIG */
    WvFilediskPnpUnsupported,
    /* IRP_MN_WRITE_CONFIG */
    WvFilediskPnpUnsupported,
    /* IRP_MN_EJECT */
    WvFilediskPnpUnsupported,
    /* IRP_MN_SET_LOCK */
    WvFilediskPnpUnsupported,
    /* IRP_MN_QUERY_ID */
    WvFilediskPnpQueryId,
    /* IRP_MN_QUERY_PNP_DEVICE_STATE */
    WvFilediskPnpUnsupported,
    /* IRP_MN_QUERY_BUS_INFORMATION */
    WvFilediskPnpQueryBusInfo,
    /* IRP_MN_DEVICE_USAGE_NOTIFICATION */
    WvFilediskPnpDeviceUsageNotification,
    /* IRP_MN_SURPRISE_REMOVAL */
    WvFilediskPnpSurpriseRemoval,
    /* IRP_MN_QUERY_LEGACY_BUS_INFORMATION */
    WvFilediskPnpUnsupported,
  };

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

/** Function definitions */

/** PnP IRP dispatcher */
NTSTATUS STDCALL WvFilediskDispatchPnpIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    NTSTATUS status;
    IO_STACK_LOCATION * io_stack_loc;
    UCHAR code;

    ASSERT(dev_obj);
    ASSERT(irp);

    /* Process all PnP IRPs in the device's thread */
    if (!WvlInDeviceThread(dev_obj))
      return WvlAddIrpToDeviceQueue(dev_obj, irp, TRUE);

    status = WvlWaitForActiveIrps(dev_obj);
    if (status == STATUS_NO_SUCH_DEVICE) {
        /* The IRP will have been completed for us */
        return status;
      }
    ASSERT(NT_SUCCESS(status));

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);
    ASSERT(io_stack_loc->MajorFunction == IRP_MJ_PNP);

    code = io_stack_loc->MinorFunction;
    if (code < WvlCountof(WvFilediskPnpHandlerTable))
      return WvFilediskPnpHandlerTable[code](dev_obj, irp);

    return WvFilediskPnpUnsupported(dev_obj, irp);
  }

/**
 * IRP_MJ_PNP:IRP_MN_START_DEVICE handler
 *
 * IRQL == PASSIVE_LEVEL, system thread
 * Completed by PDO first (here), then caught by higher drivers
 * Do not send this IRP
 *
 * @param Parameters.StartDevice.AllocatedResources (I/O stack location)
 *
 * @param Parameters.StartDevice.AllocatedResourcesTranslated
 *   (I/O stack location)
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *     Irp->IoStatus.Status == STATUS_PENDING
 */
static NTSTATUS STDCALL WvFilediskPnpStartDevice(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WVL_FILEDISK * disk;
    LONG flags;
    NTSTATUS status;

    ASSERT(dev_obj);
    disk = dev_obj->DeviceExtension;
    ASSERT(disk);
    ASSERT(irp);

    flags = InterlockedOr(&disk->Flags, CvWvlFilediskFlagStarted);

    irp->IoStatus.Status = status = STATUS_SUCCESS;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

/**
 * IRP_MJ_PNP:IRP_MN_QUERY_REMOVE_DEVICE handler
 *
 * IRQL == PASSIVE_LEVEL, system thread
 * Completed anywhere in the device stack upon failure, else by PDO (here)
 * Do not send this IRP
 * Any child PDOs receive one of these IRPs, first, except possibly
 * if they've been surprise-removed
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 */
static NTSTATUS STDCALL WvFilediskPnpQueryRemoveDevice(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WVL_FILEDISK * disk;
    LONG flags;
    NTSTATUS status;

    ASSERT(dev_obj);
    disk = dev_obj->DeviceExtension;
    ASSERT(disk);
    ASSERT(irp);

    flags = InterlockedOr(&disk->Flags, CvWvlFilediskFlagRemoving);

    irp->IoStatus.Status = status = STATUS_SUCCESS;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

/**
 * IRP_MJ_PNP:IRP_MN_REMOVE_DEVICE handler
 *
 * IRQL == PASSIVE_LEVEL, system thread
 * Completed by PDO
 * Do not send this IRP
 * Any child PDOs receive one of these IRPs, first, except possibly
 * if they've been surprise-removed
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 */
static NTSTATUS STDCALL WvFilediskPnpRemoveDevice(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WVL_FILEDISK * disk;
    LONG flags;
    NTSTATUS status;

    ASSERT(dev_obj);
    disk = dev_obj->DeviceExtension;
    ASSERT(disk);
    ASSERT(irp);

    /* Flags to clear */
    flags = (
        CvWvlFilediskFlagStarted |
        CvWvlFilediskFlagRemoving |
        CvWvlFilediskFlagSurpriseRemoved
      );
    flags = InterlockedAnd(&disk->Flags, ~flags);
    if (flags & CvWvlFilediskFlagSurpriseRemoved)
      WvlDecrementResourceUsage(disk->DeviceExtension->Usage);

    irp->IoStatus.Status = status = STATUS_SUCCESS;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

/**
 * IRP_MJ_PNP:IRP_MN_CANCEL_REMOVE_DEVICE handler
 *
 * IRQL == PASSIVE_LEVEL, system thread
 * Completed by PDO (here), further processed by FDOs
 * Do not send this IRP
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 */
static NTSTATUS STDCALL WvFilediskPnpCancelRemoveDevice(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WVL_FILEDISK * disk;
    LONG flags;
    NTSTATUS status;

    ASSERT(dev_obj);
    disk = dev_obj->DeviceExtension;
    ASSERT(disk);
    ASSERT(irp);

    flags = InterlockedAnd(&disk->Flags, ~CvWvlFilediskFlagRemoving);

    irp->IoStatus.Status = status = STATUS_SUCCESS;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

/**
 * IRP_MJ_PNP:IRP_MN_STOP_DEVICE handler
 *
 * IRQL == PASSIVE_LEVEL, system thread
 * Completed by PDO (here)
 * Do not send this IRP
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 */
static NTSTATUS STDCALL WvFilediskPnpStopDevice(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WVL_FILEDISK * disk;
    LONG flags;
    NTSTATUS status;

    ASSERT(dev_obj);
    disk = dev_obj->DeviceExtension;
    ASSERT(disk);
    ASSERT(irp);

    /* Flags to clear */
    flags = (
        CvWvlFilediskFlagStarted |
        CvWvlFilediskFlagStopping
      );
    flags = InterlockedAnd(&disk->Flags, ~flags);

    irp->IoStatus.Status = status = STATUS_SUCCESS;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

/**
 * IRP_MJ_PNP:IRP_MN_QUERY_STOP_DEVICE handler
 *
 * IRQL == PASSIVE_LEVEL, system thread
 * Completed anywhere in the device stack upon failure, else by PDO (here)
 * Do not send this IRP
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *     Irp->IoStatus.Status == STATUS_UNSUCCESSFUL
 *     Irp->IoStatus.Status == STATUS_RESOURCE_REQUIREMENTS_CHANGED
 */
static NTSTATUS STDCALL WvFilediskPnpQueryStopDevice(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WVL_FILEDISK * disk;
    LONG flags;
    NTSTATUS status;

    ASSERT(dev_obj);
    disk = dev_obj->DeviceExtension;
    ASSERT(disk);
    ASSERT(irp);

    /* TODO: Check for open handles */
    flags = InterlockedOr(&disk->Flags, CvWvlFilediskFlagStopping);

    irp->IoStatus.Status = status = STATUS_SUCCESS;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

/**
 * IRP_MJ_PNP:IRP_MN_CANCEL_STOP_DEVICE handler
 *
 * IRQL == PASSIVE_LEVEL, system thread
 * Completed by PDO (here), further processed by FDOs
 * Do not send this IRP
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 */
static NTSTATUS STDCALL WvFilediskPnpCancelStopDevice(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WVL_FILEDISK * disk;
    LONG flags;
    NTSTATUS status;

    ASSERT(dev_obj);
    disk = dev_obj->DeviceExtension;
    ASSERT(disk);
    ASSERT(irp);

    flags = InterlockedAnd(&disk->Flags, ~CvWvlFilediskFlagStopping);

    irp->IoStatus.Status = status = STATUS_SUCCESS;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
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
static NTSTATUS STDCALL WvFilediskPnpQueryDeviceRelations(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    IO_STACK_LOCATION * io_stack_loc;
    DEVICE_RELATIONS * higher_dev_relations;
    ULONG i;
    DEVICE_RELATIONS target_dev_relations;
    DEVICE_RELATIONS * dev_relations;
    NTSTATUS status;

    ASSERT(dev_obj);
    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    switch (io_stack_loc->Parameters.QueryDeviceRelations.Type) {
        case TargetDeviceRelation:
        /*
         * A higher driver should not have sent this down if they
         * populated a response!
         */
        higher_dev_relations = (VOID *) irp->IoStatus.Information;
        if (higher_dev_relations) {
            for (i = 0; i < higher_dev_relations->Count; ++i)
              ObDereferenceObject(higher_dev_relations->Objects[i]);
            wv_free(higher_dev_relations);
            irp->IoStatus.Information = 0;
          }

        /* Construct the response */
        target_dev_relations.Count = 1;
        target_dev_relations.Objects[0] = dev_obj;

        /* Allocate the response */
        dev_relations = WvlMergeDeviceRelations(&target_dev_relations, NULL);
        if (!dev_relations) {
            irp->IoStatus.Status = status = STATUS_INSUFFICIENT_RESOURCES;
            break;
          }
        ObReferenceObject(dev_obj);
        irp->IoStatus.Information = (ULONG_PTR) dev_relations;
        irp->IoStatus.Status = status = STATUS_SUCCESS;
        break;

        case BusRelations:
        case RemovalRelations:
        case EjectionRelations:
        default:
        DBG("Unsupported device relations query\n");
        status = STATUS_SUCCESS;
      }

    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

/**
 * IRP_MJ_PNP:IRP_MN_QUERY_CAPABILITIES handler
 *
 * IRQL == PASSIVE_LEVEL, any thread
 * Ok to send this IRP
 * Completed by PDO (here), can be hooked
 *
 * @param Parameters.DeviceCapabilities.Capabilities (I/O stack location)
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *     I/O stack location: Parameters.DeviceCapabilities.Capabilities
 *       populated
 *   Error:
 *     Irp->IoStatus.Status == STATUS_UNSUCCESSFUL
 */
static NTSTATUS STDCALL WvFilediskPnpQueryCapabilities(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    IO_STACK_LOCATION * io_stack_loc;
    DEVICE_CAPABILITIES * dev_caps;
    S_WVL_FILEDISK * disk;
    NTSTATUS status;
    DEVICE_CAPABILITIES parent_dev_caps;

    ASSERT(dev_obj);
    ASSERT(irp);

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    dev_caps = io_stack_loc->Parameters.DeviceCapabilities.Capabilities;
    ASSERT(dev_caps);

    if (dev_caps->Version != 1 || dev_caps->Size < sizeof *dev_caps) {
        status = STATUS_UNSUCCESSFUL;
        DBG("Wrong version!\n");
        goto err_version;
      }

    disk = dev_obj->DeviceExtension;
    ASSERT(disk);

    status = WvDriverGetDevCapabilities(
        disk->DeviceExtension->ParentBusDeviceObject,
        &parent_dev_caps
      );
    if (!NT_SUCCESS(status))
      goto err_parent_caps;

    RtlCopyMemory(dev_caps, &parent_dev_caps, sizeof *dev_caps);
    dev_caps->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    if (dev_caps->DeviceState[PowerSystemSleeping1] != PowerDeviceD0)
      dev_caps->DeviceState[PowerSystemSleeping1] = PowerDeviceD1;
    if (dev_caps->DeviceState[PowerSystemSleeping2] != PowerDeviceD0)
      dev_caps->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
    #if 0
    if (dev_caps->DeviceState[PowerSystemSleeping3] != PowerDeviceD0)
      dev_caps->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
    #endif
    dev_caps->DeviceWake = PowerDeviceD1;
    dev_caps->DeviceD1 = TRUE;
    dev_caps->DeviceD2 = FALSE;
    dev_caps->WakeFromD0 = FALSE;
    dev_caps->WakeFromD1 = FALSE;
    dev_caps->WakeFromD2 = FALSE;
    dev_caps->WakeFromD3 = FALSE;
    dev_caps->D1Latency = 0;
    dev_caps->D2Latency = 0;
    dev_caps->D3Latency = 0;
    dev_caps->EjectSupported = FALSE;
    dev_caps->HardwareDisabled = FALSE;
    dev_caps->Removable = WvlDiskIsRemovable[disk->MediaType];
    dev_caps->SurpriseRemovalOK = FALSE;
    dev_caps->UniqueID = FALSE;
    dev_caps->SilentInstall = FALSE;
    #if 0
    dev_caps->Address = dev_obj->SerialNo;
    dev_caps->UINumber = dev_obj->SerialNo;
    #endif
    status = STATUS_SUCCESS;
    goto out;

    err_parent_caps:

    err_version:

    out:
    irp->IoStatus.Status = status;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

/**
 * IRP_MJ_PNP:IRP_MN_QUERY_DEVICE_TEXT handler
 *
 * IRQL == PASSIVE_LEVEL, any thread
 * Do not send this IRP
 * Completed by PDO (here)
 *
 * @param Parameters.QueryDeviceText.DeviceTextType (I/O stack location)
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *     Irp->IoStatus.Information populated from paged memory
 *   Error:
 *     Irp->IoStatus.Status == STATUS_INSUFFICIENT_RESOURCES
 *     Irp->IoStatus.Information == 0
 */
static NTSTATUS STDCALL WvFilediskPnpQueryDeviceText(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    static const WCHAR dev_text_desc[] = WVL_M_WLIT L" Filedisk";
    static const WCHAR dev_text_loc_info[] = L"PDO0x0000000000000000";
    IO_STACK_LOCATION * io_stack_loc;
    WCHAR * response;
    NTSTATUS status;

    ASSERT(dev_obj);
    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    /* Determine the query type */
    switch (io_stack_loc->Parameters.QueryDeviceText.DeviceTextType) {
        case DeviceTextDescription:

        /* Allocate the response */
        response = wv_pallocz(sizeof dev_text_desc);
        if (!response) {
            DBG("Unable to allocate DeviceTextDescription\n");
            status = STATUS_INSUFFICIENT_RESOURCES;
            irp->IoStatus.Information = 0;
            goto err_response_dev_text_desc;
          }

        /* Populate the response */
        RtlCopyMemory(response, dev_text_desc, sizeof dev_text_desc);
        irp->IoStatus.Information = (ULONG_PTR) response;
        status = STATUS_SUCCESS;

        /* response not freed */
        err_response_dev_text_desc:

        break;

        case DeviceTextLocationInformation:

        /* Allocate the response */
        response = wv_pallocz(sizeof dev_text_loc_info);
        if (!response) {
            DBG("Unable to allocate DeviceTextDescription\n");
            status = STATUS_INSUFFICIENT_RESOURCES;
            irp->IoStatus.Information = 0;
            goto err_response_dev_text_loc_info;
          }

        /* Populate the response */
        swprintf(response, L"PDO%p", (VOID *) dev_obj);
        irp->IoStatus.Information = (ULONG_PTR) response;
        status = STATUS_SUCCESS;

        /* response not freed */
        err_response_dev_text_loc_info:

        break;

        default:
        irp->IoStatus.Information = 0;
        status = STATUS_NOT_SUPPORTED;
      }
    irp->IoStatus.Status = status;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
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
static NTSTATUS STDCALL WvFilediskPnpQueryId(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    static const WCHAR * hw_ids[CvWvlFilediskMediaTypes] = {
        WVL_M_WLIT L"\\FileFloppyDisk",
        WVL_M_WLIT L"\\FileHardDisk",
        WVL_M_WLIT L"\\FileOpticalDisc"
      };
    static const WCHAR * compat_ids[CvWvlFilediskMediaTypes] = {
        L"GenSFloppy",
        L"GenDisk",
        L"GenCdRom"
      };
    static const WCHAR loc_id[] = L"PDO0x0000000000000000";
    IO_STACK_LOCATION * io_stack_loc;
    S_WVL_FILEDISK * disk;
    BUS_QUERY_ID_TYPE query_type;
    SIZE_T hw_id_sz;
    SIZE_T compat_id_sz;
    SIZE_T response_sz;
    WCHAR * response;
    NTSTATUS status;

    ASSERT(dev_obj);
    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    disk = dev_obj->DeviceExtension;
    ASSERT(disk);
    ASSERT(disk->MediaType >= 0);
    ASSERT(disk->MediaType < CvWvlFilediskMediaTypes);

    hw_id_sz = (wcslen(hw_ids[disk->MediaType]) + 1) * sizeof hw_ids[0][0];
    compat_id_sz =
      (wcslen(compat_ids[disk->MediaType]) + 1) * sizeof compat_ids[0][0];

    query_type = io_stack_loc->Parameters.QueryId.IdType;

    /* Determine the buffer size to allocate */
    switch (query_type) {
        case BusQueryDeviceID:
        response_sz = hw_id_sz;
        break;

        case BusQueryInstanceID:
        response_sz = sizeof loc_id;
        break;

        case BusQueryHardwareIDs:
        response_sz = hw_id_sz + compat_id_sz + sizeof L'\0';
        break;

        case BusQueryCompatibleIDs:
        response_sz = compat_id_sz;
        break;
        
        default:
        DBG("Unknown query type for filedisk %p!", (VOID *) dev_obj);
        status = STATUS_NOT_SUPPORTED;
        irp->IoStatus.Information = 0;
        goto err_query_type;
      }

    /* Allocate the buffer */
    response = wv_pallocz(response_sz);
    if (!response) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        irp->IoStatus.Information = 0;
        goto err_response;
      }

    /* Populate the response */
    switch (query_type) {
        case BusQueryDeviceID:
        RtlCopyMemory(response, hw_ids[disk->MediaType], hw_id_sz);
        break;

        case BusQueryInstanceID:
        swprintf(response, L"PDO%p", (VOID *) dev_obj);
        break;

        case BusQueryHardwareIDs:
        RtlCopyMemory(response, hw_ids[disk->MediaType], hw_id_sz);
        RtlCopyMemory(
            response + hw_id_sz,
            compat_ids[disk->MediaType],
            compat_id_sz
          );
        /* wv_pallocz yielded a terminator */
        break;

        case BusQueryCompatibleIDs:
        RtlCopyMemory(response, compat_ids[disk->MediaType], compat_id_sz);
        break;

        default:
        ASSERT(0);
        /* Impossible thanks to earlier 'switch' */
        ;
      }
    status = STATUS_SUCCESS;
    irp->IoStatus.Information = (ULONG_PTR) response;

    /* response not freed */
    err_response:

    err_query_type:

    irp->IoStatus.Status = status;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

/**
 * IRP_MJ_PNP:IRP_MN_QUERY_BUS_INFORMATION handler
 *
 * IRQL == PASSIVE_LEVEL, any thread
 * Do not send this IRP
 * Completed by PDO (here)
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *     Irp->IoStatus.Information populated from paged memory
 *   Error:
 *     Irp->IoStatus.Status == STATUS_INSUFFICIENT_RESOURCES
 *     Irp->IoStatus.Information == 0
 *
 * TODO: Maybe it's best for a PDO to query its parent for this info
 */
static NTSTATUS STDCALL WvFilediskPnpQueryBusInfo(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    PNP_BUS_INFORMATION * response;
    NTSTATUS status;

    ASSERT(dev_obj);
    ASSERT(irp);
    response = wv_palloc(sizeof *response);
    if (!response) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        irp->IoStatus.Information = 0;
        goto err_response;
      }
    response->BusTypeGuid = GUID_BUS_TYPE_INTERNAL;
    response->LegacyBusType = PNPBus;
    response->BusNumber = 0;
    irp->IoStatus.Information = (ULONG_PTR) response;
    status = STATUS_SUCCESS;
    
    /* response not freed */
    err_response:

    irp->IoStatus.Status = status;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

/**
 * IRP_MJ_PNP:IRP_MN_DEVICE_USAGE_NOTIFICATION handler
 *
 * IRQL == PASSIVE_LEVEL, any thread
 * Do not send this IRP, but it can be passed along
 * Completed by PDO (here)
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *   Error:
 *     Irp->IoStatus.Status == STATUS_NOT_SUPPORTED
 */
static NTSTATUS STDCALL WvFilediskPnpDeviceUsageNotification(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    ASSERT(dev_obj);
    ASSERT(irp);
    
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return STATUS_NOT_SUPPORTED;
  }

/**
 * IRP_MJ_PNP:IRP_MN_SURPRISE_REMOVAL handler
 *
 * IRQL == PASSIVE_LEVEL, system thread
 * Do not send this IRP, but it can be passed along
 * Completed by PDO (here)
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *   Error:
 *     A driver must not fail this IRP
 */

static NTSTATUS STDCALL WvFilediskPnpSurpriseRemoval(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WVL_FILEDISK * disk;
    LONG flags;
    NTSTATUS status;

    ASSERT(dev_obj);
    disk = dev_obj->DeviceExtension;
    ASSERT(disk);
    ASSERT(irp);

    WvlIncrementResourceUsage(disk->DeviceExtension->Usage);
    flags = InterlockedOr(&disk->Flags, CvWvlFilediskFlagSurpriseRemoved);
    irp->IoStatus.Status = status = STATUS_SUCCESS;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }
