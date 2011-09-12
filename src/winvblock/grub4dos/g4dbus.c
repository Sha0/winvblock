/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 * Copyright 2006-2008, V.
 * For WinAoE contact information, see http://winaoe.org/
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

/****
 * @file
 *
 * GRUB4DOS bus specifics; both PDO and FDO together.
 */

#include <ntddk.h>
#include <initguid.h>
#include <stdio.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "grub4dos.h"
#include "debug.h"

/*** Macros. */
#define M_FUNC_ENTRY()

/*** Object types. */
typedef struct S_WVG4DBUS_ S_WVG4DBUS, * SP_WVG4DBUS;

/*** Struct/union definitions. */
struct S_WVG4DBUS_ {
    WV_S_DEV_EXT DevExt;
    struct {
        DEVICE_RELATIONS Data;
        /*
         * 'DevRelations' has an array of one PDEVICE_OBJECT.
         * We need a total of M_G4D_SLOTS + 1 (for an INT 0x13 hook), so...
         */
        PDEVICE_OBJECT Padding[M_G4D_SLOTS];
      } BusRelations;
    WV_S_DEV_T Dev;
    PDEVICE_OBJECT ParentBus;
    PVOID Int13hHook;
  };

/*** Function declarations. */
static NTSTATUS STDCALL WvG4dBusIrpComplete(IN PIRP);
static NTSTATUS WvG4dBusIrpDispatch(IN PDEVICE_OBJECT, IN PIRP);
static NTSTATUS STDCALL WvG4dBusIrpPnp(IN PDEVICE_OBJECT, IN PIRP);
static NTSTATUS STDCALL WvG4dBusIrpPnpQueryId(IN PDEVICE_OBJECT, IN PIRP);
static NTSTATUS STDCALL WvG4dBusIrpPnpQueryDevText(IN PDEVICE_OBJECT, IN PIRP);
static NTSTATUS STDCALL WvG4dBusIrpPnpQueryBusInfo(IN PDEVICE_OBJECT, IN PIRP);
static NTSTATUS STDCALL WvG4dBusIrpPnpQueryDevRelations(
    IN PDEVICE_OBJECT,
    IN PIRP
  );
static NTSTATUS STDCALL WvG4dBusIrpPnpQueryCapabilities(
    IN PDEVICE_OBJECT,
    IN PIRP
  );

/*** Objects. */
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

/*** Function definitions. */

/* Complete an IRP. */
static NTSTATUS STDCALL WvG4dBusIrpComplete(IN PIRP irp) {
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    WVL_M_DEBUG_IRP_END(irp, irp->IoStatus.Status);
    return irp->IoStatus.Status;
  }

/* Dispatch an IRP. */
static NTSTATUS WvG4dBusIrpDispatch(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc;
    M_FUNC_ENTRY();

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    switch (io_stack_loc->MajorFunction) {
        case IRP_MJ_PNP:
          return WvG4dBusIrpPnp(dev_obj, irp);

        default:
          ;
      }
    irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;
    return WvG4dBusIrpComplete(irp);
  }

/* Handle a PnP IRP. */
static NTSTATUS STDCALL WvG4dBusIrpPnp(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    UCHAR code;
    M_FUNC_ENTRY();

    code = IoGetCurrentIrpStackLocation(irp)->MinorFunction;
    switch (code) {
        case IRP_MN_QUERY_ID:
          return WvG4dBusIrpPnpQueryId(dev_obj, irp);

        case IRP_MN_QUERY_DEVICE_TEXT:
          return WvG4dBusIrpPnpQueryDevText(dev_obj, irp);

        case IRP_MN_QUERY_BUS_INFORMATION:
          return WvG4dBusIrpPnpQueryBusInfo(dev_obj, irp);

        case IRP_MN_QUERY_DEVICE_RELATIONS:
          return WvG4dBusIrpPnpQueryDevRelations(dev_obj, irp);

        case IRP_MN_QUERY_CAPABILITIES:
          return WvG4dBusIrpPnpQueryCapabilities(dev_obj, irp);

        case IRP_MN_REMOVE_DEVICE:
        case IRP_MN_START_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_QUERY_RESOURCES:
        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
          irp->IoStatus.Status = STATUS_SUCCESS;
          break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
          irp->IoStatus.Information = 0;
          irp->IoStatus.Status = STATUS_SUCCESS;
          break;

        default:
          irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
      }
    return WvG4dBusIrpComplete(irp);
  }

/* Return device IDs .*/
static NTSTATUS STDCALL WvG4dBusIrpPnpQueryId(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    static const WCHAR hw_id[] = WVL_M_WLIT L"\\Int13hHook\0";
    static const WCHAR instance_id[] = L"HOOK_at_%08X\0\0\0\0";
    BUS_QUERY_ID_TYPE query_type;
    SIZE_T buf_size;
    WCHAR * buf;
    NTSTATUS status;
    INT copied;
    SP_WVG4DBUS bus;
    M_FUNC_ENTRY();

    query_type = IoGetCurrentIrpStackLocation(irp)->Parameters.QueryId.IdType;

    /* Determine the buffer size to allocate */
    switch (query_type) {
        case BusQueryDeviceID:
        case BusQueryHardwareIDs:
        case BusQueryCompatibleIDs:
          buf_size = sizeof hw_id;
          break;

        case BusQueryInstanceID:
          buf_size = sizeof instance_id;
          break;

        default:
          DBG("Unknown query type for G4D bus dev %p!", (PVOID) dev_obj);
          status = STATUS_INVALID_PARAMETER;
          goto err_query_type;
      }

    /* Allocate the buffer */
    buf = wv_mallocz(buf_size);
    if (!buf) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_buf;
      }

    /* Populate the buffer with the ID */
    switch (query_type) {
        case BusQueryDeviceID:
        case BusQueryHardwareIDs:
        case BusQueryCompatibleIDs:
          copied = swprintf(buf, hw_id);
          break;

        case BusQueryInstanceID:
        	/* "Location" */
        	bus = CONTAINING_RECORD(
              dev_obj->DeviceExtension,
              S_WVG4DBUS,
              DevExt
            );
          copied = swprintf(buf, instance_id, bus->Int13hHook);
          break;

        default:
          /* Impossible thanks to earlier 'switch' */
          ;
      }
    if (!copied) {
        DBG("swprintf() error!\n");
        status = STATUS_UNSUCCESSFUL;
        goto err_copied;
      }

    DBG("IRP_MN_QUERY_ID for G4D bus dev %p\n", (PVOID) dev_obj);
    irp->IoStatus.Information = (ULONG_PTR) buf;
    irp->IoStatus.Status = STATUS_SUCCESS;
    return WvG4dBusIrpComplete(irp);

    err_copied:

    wv_free(buf);
    err_buf:

    err_query_type:

    irp->IoStatus.Information = 0;
    irp->IoStatus.Status = status;
    return WvG4dBusIrpComplete(irp);
  }

/* Return device text. */
static NTSTATUS STDCALL WvG4dBusIrpPnpQueryDevText(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    static const WCHAR name[] = L"GRUB4DOS Bus";
    PIO_STACK_LOCATION io_stack_loc;
    PWCHAR result;
    M_FUNC_ENTRY();

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    switch (io_stack_loc->Parameters.QueryDeviceText.DeviceTextType) {
        case DeviceTextDescription:
          result = wv_palloc(sizeof name);
          if (!result) {
              DBG("wv_palloc failed!\n");
              irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
              irp->IoStatus.Information = 0;
              goto err_alloc_result;
            }

          RtlCopyMemory(result, name, sizeof name);
          irp->IoStatus.Status = STATUS_SUCCESS;
          irp->IoStatus.Information = (ULONG_PTR) result;
          break;

          wv_free(result);
          err_alloc_result:

          break;

        default:
          irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
          irp->IoStatus.Information = 0;
          break;
      }
    return WvG4dBusIrpComplete(irp);
  }

/* Return parent bus info (the WinVBlock bus). */
static NTSTATUS STDCALL WvG4dBusIrpPnpQueryBusInfo(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    PPNP_BUS_INFORMATION pnp_bus_info;
    M_FUNC_ENTRY();

    pnp_bus_info = wv_palloc(sizeof *pnp_bus_info);
    if (!pnp_bus_info) {
        DBG("wv_palloc failed!\n");
        irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        irp->IoStatus.Information = 0;
        goto err_alloc_pnp_bus_info;
      }

    pnp_bus_info->BusTypeGuid = GUID_BUS_TYPE_INTERNAL;
    pnp_bus_info->LegacyBusType = PNPBus;
    pnp_bus_info->BusNumber = 0;
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = (ULONG_PTR) pnp_bus_info;
    goto out;

    wv_free(pnp_bus_info);
    err_alloc_pnp_bus_info:

    out:
    return WvG4dBusIrpComplete(irp);
  }

/* Return device relations info. */
static NTSTATUS STDCALL WvG4dBusIrpPnpQueryDevRelations(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc;
    DEVICE_RELATION_TYPE type;
    wv_size_t size;
    SP_WVG4DBUS bus;
    PDEVICE_RELATIONS dev_relations;
    ULONG i;
    M_FUNC_ENTRY();

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);    
    type = io_stack_loc->Parameters.QueryDeviceRelations.Type;
    if (irp->IoStatus.Information) {
        DBG("IRP already handled!\n");
        goto err_handled;
      }

    switch (type) {
        case BusRelations:
          size = sizeof bus->BusRelations;
          break;

        case TargetDeviceRelation:
          size = sizeof *dev_relations;
          break;

        default:
          DBG("Unsupported!\n");
          goto err_type;
      }
    dev_relations = wv_palloc(size);
    if (!dev_relations) {
        DBG("wv_palloc failed!\n");
        irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        irp->IoStatus.Information = 0;
        goto err_alloc_dev_relations;
      }

    switch (type) {
        case BusRelations:
          bus = dev_obj->DeviceExtension;
          RtlCopyMemory(
              dev_relations,
              &bus->BusRelations,
              sizeof bus->BusRelations
            );
          for (i = 0; i < dev_relations->Count; ++i)
            ObReferenceObject(dev_relations->Objects[i]);
          break;

        case TargetDeviceRelation:
          dev_relations->Count = 1;
          dev_relations->Objects[0] = dev_obj;
          break;

        default:
          /* Impossible thanks to earlier 'switch' */
          ;
      }
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = (ULONG_PTR) dev_relations;
    goto out;

    wv_free(dev_relations);
    err_alloc_dev_relations:

    err_type:

    err_handled:

    out:
    return WvG4dBusIrpComplete(irp);
  }

/* Return device capabilities info. */
static NTSTATUS STDCALL WvG4dBusIrpPnpQueryCapabilities(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc;
    PDEVICE_CAPABILITIES dev_caps;
    SP_WVG4DBUS bus;
    DEVICE_CAPABILITIES parent_dev_caps;
    M_FUNC_ENTRY();

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    dev_caps = io_stack_loc->Parameters.DeviceCapabilities.Capabilities;
    if (dev_caps->Version != 1 || dev_caps->Size < sizeof *dev_caps) {
        irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
        DBG("Wrong version!\n");
        goto err_version;
      }

    bus = dev_obj->DeviceExtension;
    irp->IoStatus.Status =
      WvDriverGetDevCapabilities(bus->ParentBus, &parent_dev_caps);
    if (!NT_SUCCESS(irp->IoStatus.Status))
      goto err_parent_caps;

    RtlCopyMemory(dev_caps, &parent_dev_caps, sizeof *dev_caps);
    goto out;

    err_parent_caps:

    err_version:

    out:
    return WvG4dBusIrpComplete(irp);
  }
