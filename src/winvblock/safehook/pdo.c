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
 * "Safe INT 0x13 hook" bus PDO
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
#include "debug.h"
#include "x86.h"
#include "safehook.h"
#include "irp.h"

/*** Macros */
#define M_FUNC_ENTRY()

/*** Function declarations */
static NTSTATUS STDCALL WvSafeHookPdoIrpComplete(IN PIRP);
static DRIVER_DISPATCH WvSafeHookPdoIrpDispatch;
static __drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH WvSafeHookPdoIrpPnp;
static __drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH
  WvSafeHookPdoIrpPnpQueryId;
static __drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH
  WvSafeHookPdoIrpPnpQueryDevText;
static __drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH
  WvSafeHookPdoIrpPnpQueryBusInfo;
static __drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH
  WvSafeHookPdoIrpPnpQueryDevRelations;
static __drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH
  WvSafeHookPdoIrpPnpQueryCapabilities;
static __drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH
  WvSafeHookPdoIrpPnpSuccess;
static __drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH
  WvSafeHookPdoIrpPnpSuccessNoInfo;

/*** Objects */
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

static S_WVL_IRP_HANDLER WvSafeHookPdoIrpHandlers[] = {
    { IRP_MJ_PNP, WvSafeHookPdoIrpPnp },
  };

static S_WVL_IRP_HANDLER_TABLE WvSafeHookPdoIrpHandlerTable = {
    TRUE,
    sizeof WvSafeHookPdoIrpHandlers / sizeof *WvSafeHookPdoIrpHandlers,
    WvSafeHookPdoIrpHandlers,
  };

static S_WVL_IRP_HANDLER WvSafeHookPdoIrpPnpHandlers[] = {
    { IRP_MN_QUERY_ID, WvSafeHookPdoIrpPnpQueryId },
    { IRP_MN_QUERY_DEVICE_TEXT, WvSafeHookPdoIrpPnpQueryDevText },
    { IRP_MN_QUERY_BUS_INFORMATION, WvSafeHookPdoIrpPnpQueryBusInfo },
    { IRP_MN_QUERY_DEVICE_RELATIONS, WvSafeHookPdoIrpPnpQueryDevRelations },
    { IRP_MN_QUERY_CAPABILITIES, WvSafeHookPdoIrpPnpQueryCapabilities },
    { IRP_MN_REMOVE_DEVICE, WvSafeHookPdoIrpPnpSuccess },
    { IRP_MN_START_DEVICE, WvSafeHookPdoIrpPnpSuccess },
    { IRP_MN_QUERY_STOP_DEVICE, WvSafeHookPdoIrpPnpSuccess },
    { IRP_MN_CANCEL_STOP_DEVICE, WvSafeHookPdoIrpPnpSuccess },
    { IRP_MN_STOP_DEVICE, WvSafeHookPdoIrpPnpSuccess },
    { IRP_MN_QUERY_REMOVE_DEVICE, WvSafeHookPdoIrpPnpSuccess },
    { IRP_MN_CANCEL_REMOVE_DEVICE, WvSafeHookPdoIrpPnpSuccess },
    { IRP_MN_SURPRISE_REMOVAL, WvSafeHookPdoIrpPnpSuccess },
    { IRP_MN_QUERY_RESOURCES, WvSafeHookPdoIrpPnpSuccess },
    { IRP_MN_QUERY_RESOURCE_REQUIREMENTS, WvSafeHookPdoIrpPnpSuccess },
    { IRP_MN_QUERY_PNP_DEVICE_STATE, WvSafeHookPdoIrpPnpSuccessNoInfo },
  };

static S_WVL_IRP_HANDLER_TABLE WvSafeHookPdoIrpPnpHandlerTable = {
    FALSE,
    sizeof WvSafeHookPdoIrpPnpHandlers / sizeof *WvSafeHookPdoIrpPnpHandlers,
    WvSafeHookPdoIrpPnpHandlers,
  };

/*** Function definitions */

/* Complete an IRP */
static NTSTATUS STDCALL WvSafeHookPdoIrpComplete(IN PIRP irp) {
    M_FUNC_ENTRY();

    WVL_M_DEBUG_IRP_END(irp, irp->IoStatus.Status);
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return irp->IoStatus.Status;
  }

/* Dispatch an IRP */
static NTSTATUS WvSafeHookPdoIrpDispatch(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    M_FUNC_ENTRY();

    return WvlIrpHandleWithTable(dev_obj, irp, &WvSafeHookPdoIrpHandlerTable);
  }

/* Handle a PnP IRP */
static NTSTATUS STDCALL WvSafeHookPdoIrpPnp(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    M_FUNC_ENTRY();

    return WvlIrpHandleWithTable(
        dev_obj,
        irp,
        &WvSafeHookPdoIrpPnpHandlerTable
      );
  }

/* Return device IDs */
static NTSTATUS STDCALL WvSafeHookPdoIrpPnpQueryId(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    static const WCHAR hw_id[] = WVL_M_WLIT L"\\Int13hHook\0";
    static const WCHAR ven_id[] = WVL_M_WLIT L"\\%sHook\0\0\0\0\0\0\0";
    static const WCHAR instance_id[] = L"HOOK_at_%08X\0\0\0\0";
    BUS_QUERY_ID_TYPE query_type;
    SIZE_T buf_size;
    WCHAR * buf;
    NTSTATUS status;
    INT copied;
    SP_WV_SAFEHOOK_PDO bus;
    WCHAR ven_buf[9];
    M_FUNC_ENTRY();

    query_type = IoGetCurrentIrpStackLocation(irp)->Parameters.QueryId.IdType;

    /* Determine the buffer size to allocate */
    switch (query_type) {
        case BusQueryHardwareIDs:
          /* The vendor ID */
          buf_size = sizeof ven_id;
          break;

        case BusQueryDeviceID:
          buf_size = sizeof hw_id;
          break;

        case BusQueryCompatibleIDs:
          /* The vendor ID and the default */
          buf_size = sizeof ven_id + sizeof hw_id;
          break;

        case BusQueryInstanceID:
          buf_size = sizeof instance_id;
          break;

        default:
          DBG(
              "Unknown query type for safe hook bus dev %p!",
              (PVOID) dev_obj
            );
          status = STATUS_INVALID_PARAMETER;
          goto err_query_type;
      }

    /* Allocate the buffer */
    buf = wv_pallocz(buf_size);
    if (!buf) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_buf;
      }

  	bus = CONTAINING_RECORD(
        dev_obj->DeviceExtension,
        S_WV_SAFEHOOK_PDO,
        DevExt
      );

    /* Sanitize and copy */
    {
        INT i;
        INT bad;
        for (i = 0; i < sizeof ven_buf / sizeof *ven_buf - 1; ++i) {
            ven_buf[i] = bus->SafeHookCopy.VendorId[i];
            bad = (
                ven_buf[i] <= L'\x20' ||
                ven_buf[i] > L'\x7F' ||
                ven_buf[i] == L'\x2C'
              );
            if (bad)
              ven_buf[i] = L'_';
            continue;
          }
        ven_buf[i] = L'\0';
      }

    /* Populate the buffer with the ID */
    switch (query_type) {
        case BusQueryHardwareIDs:
          copied = swprintf(buf, ven_id, ven_buf);
          break;

        case BusQueryDeviceID:
          copied = swprintf(buf, hw_id);
          break;

        case BusQueryCompatibleIDs:
          copied = swprintf(buf, ven_id, ven_buf);
          if (!copied)
            break;
          /* Advance to the next ID */
          ++copied;
          copied += swprintf(buf + copied, hw_id);
          break;

        case BusQueryInstanceID:
        	/* "Location" */
          copied = swprintf(
              buf,
              instance_id,
              M_X86_SEG16OFF16_ADDR(&bus->Whence)
            );
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

    DBG("IRP_MN_QUERY_ID for safe hook bus dev %p\n", (PVOID) dev_obj);
    irp->IoStatus.Information = (ULONG_PTR) buf;
    irp->IoStatus.Status = STATUS_SUCCESS;
    return WvSafeHookPdoIrpComplete(irp);

    err_copied:

    wv_free(buf);
    err_buf:

    err_query_type:

    irp->IoStatus.Information = 0;
    irp->IoStatus.Status = status;
    return WvSafeHookPdoIrpComplete(irp);
  }

/* Return device text */
static NTSTATUS STDCALL WvSafeHookPdoIrpPnpQueryDevText(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    static const WCHAR name[] = L"Safe INT 0x13 Hook Bus";
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
    return WvSafeHookPdoIrpComplete(irp);
  }

/* Return parent bus info */
static NTSTATUS STDCALL WvSafeHookPdoIrpPnpQueryBusInfo(
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
    return WvSafeHookPdoIrpComplete(irp);
  }

/* Return device relations info */
static NTSTATUS STDCALL WvSafeHookPdoIrpPnpQueryDevRelations(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc;
    DEVICE_RELATION_TYPE type;
    wv_size_t size;
    SP_WV_SAFEHOOK_PDO bus;
    PDEVICE_RELATIONS prev_relations;
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
          size = sizeof *dev_relations;

          /* A higher-level driver might have enumerated children */
          prev_relations = (PVOID) irp->IoStatus.Information;
          if (prev_relations)
            size += prev_relations->Count * sizeof prev_relations->Objects[0];
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
          /* Assume no children */
          dev_relations->Count = 0;

          /* Have we checked the next hook in the chain? */
          if (bus->Next == bus) {
              /* TODO: Fix this logic */
              DEVICE_OBJECT * next;

              WvlCreateSafeHookDevice(
                  &bus->SafeHookCopy.PrevHook,
                  &next
                );
              if (next)
                bus->Next = next->DeviceExtension;
                else
                bus->Next = NULL;
            }

          /* Report the next hook in the chain, if one exists */
          if (bus->Next) {
              dev_relations->Count++;
              dev_relations->Objects[0] = bus->Next->Self;
            }

          /* Copy any devices enumerated by another driver */
          if (prev_relations) {
              dev_relations += prev_relations->Count;
              if (prev_relations->Count) {
                  RtlCopyMemory(
                      dev_relations->Objects + dev_relations->Count,
                      prev_relations->Objects,
                      prev_relations->Count * sizeof prev_relations->Objects[0]
                    );
                }
              wv_free(prev_relations);
            }
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
    return WvSafeHookPdoIrpComplete(irp);
  }

/* Return device capabilities info */
static NTSTATUS STDCALL WvSafeHookPdoIrpPnpQueryCapabilities(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc;
    PDEVICE_CAPABILITIES dev_caps;
    SP_WV_SAFEHOOK_PDO bus;
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
    return WvSafeHookPdoIrpComplete(irp);
  }

static NTSTATUS STDCALL WvSafeHookPdoIrpPnpSuccess(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    M_FUNC_ENTRY();

    return irp->IoStatus.Status = STATUS_SUCCESS;
  }

static NTSTATUS STDCALL WvSafeHookPdoIrpPnpSuccessNoInfo(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    M_FUNC_ENTRY();

    irp->IoStatus.Information = 0;
    return irp->IoStatus.Status = STATUS_SUCCESS;
  }

PDEVICE_OBJECT STDCALL WvSafeHookPdoCreate(
    IN SP_X86_SEG16OFF16 HookAddr,
    IN WV_SP_PROBE_SAFE_MBR_HOOK SafeHook,
    IN PDEVICE_OBJECT ParentBus
  ) {
    NTSTATUS status;
    SP_WV_SAFEHOOK_PDO bus;
    PDEVICE_OBJECT pdo;
    M_FUNC_ENTRY();

    pdo = NULL;
    /* Create the safe hook bus PDO */
    status = IoCreateDevice(
        WvDriverObj,
        sizeof *bus,
        NULL,
        FILE_DEVICE_CONTROLLER,
        FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &pdo
      );
    if (!NT_SUCCESS(status)) {
        DBG("Could not create safe hook bus PDO!\n");
        goto err_pdo;
      }

    /* Populate */
    bus = pdo->DeviceExtension;
    bus->Whence = *HookAddr;
    bus->ParentBus = ParentBus;
    bus->Self = pdo;
    /* Abuse this member to know if we've checked the next in the chain */
    bus->Next = bus;
    WvDevSetIrpHandler(pdo, WvSafeHookPdoIrpDispatch);
    RtlCopyMemory(&bus->SafeHookCopy, SafeHook, sizeof bus->SafeHookCopy);

    pdo->Flags |= DO_DIRECT_IO;         /* FIXME? */
    pdo->Flags |= DO_POWER_INRUSH;      /* FIXME? */
    pdo->Flags &= ~DO_DEVICE_INITIALIZING;

    goto out;

    IoDeleteDevice(pdo);
    err_pdo:

    out:
    return pdo;
  }
