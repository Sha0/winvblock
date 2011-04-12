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
 * Device specifics.
 */

#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "irp.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "debug.h"

/* Forward declarations. */
static WV_F_DEV_FREE WvDevFreeDev_;
static WV_F_DEV_CREATE_PDO WvDevMakePdo_;

/**
 * Initialize device defaults.
 *
 * @v Dev               Points to the device to initialize with defaults.
 */
WVL_M_LIB VOID WvDevInit(WV_SP_DEV_T Dev) {
    RtlZeroMemory(Dev, sizeof *Dev);
    /* Populate non-zero device defaults. */
    Dev->DriverObject = WvDriverObj;
    Dev->Ops.CreatePdo = WvDevMakePdo_;
    Dev->Ops.Free = WvDevFreeDev_;
  }

/**
 * Create a new device.
 *
 * @ret dev             The address of a new device, or NULL for failure.
 *
 * This function should not be confused with a PDO creation routine, which is
 * actually implemented for each device type.  This routine will allocate a
 * WV_DEV_T and populate the device with default values.
 */
WVL_M_LIB WV_SP_DEV_T WvDevCreate(void) {
    WV_SP_DEV_T dev;

    /*
     * Devices might be used for booting and should
     * not be allocated from a paged memory pool.
     */
    dev = wv_malloc(sizeof *dev);
    if (dev == NULL)
      return NULL;

    WvDevInit(dev);
    return dev;
  }

/**
 * Create a device PDO.
 *
 * @v Dev               Points to the device that needs a PDO.
 */
WVL_M_LIB PDEVICE_OBJECT STDCALL WvDevCreatePdo(IN WV_SP_DEV_T Dev) {
    return Dev->Ops.CreatePdo ? Dev->Ops.CreatePdo(Dev) : NULL;
  }

/**
 * Default PDO creation operation.
 *
 * @v dev               Points to the device that needs a PDO.
 * @ret NULL            Reports failure, no matter what.
 *
 * This function does nothing, since it doesn't make sense to create a PDO
 * for an unknown type of device.
 */
static PDEVICE_OBJECT STDCALL WvDevMakePdo_(IN WV_SP_DEV_T dev) {
    DBG("No specific PDO creation operation for this device!\n");
    return NULL;
  }

/**
 * Respond to a device PnP ID query.
 *
 * @v dev                       The device being queried for PnP IDs.
 * @v query_type                The query type.
 * @v buf                       Wide character, 512-element buffer for the
 *                              ID response.
 * @ret UINT32       The number of wide characters in the response,
 *                              or 0 upon a failure.
 */
UINT32 STDCALL WvDevPnpId(
    IN WV_SP_DEV_T dev,
    IN BUS_QUERY_ID_TYPE query_type,
    IN OUT WCHAR (*buf)[512]
  ) {
    return dev->Ops.PnpId ? dev->Ops.PnpId(dev, query_type, buf) : 0;
  }

/* An IRP handler for a PnP ID query. */
NTSTATUS STDCALL WvDevPnpQueryId(IN WV_SP_DEV_T dev, IN PIRP irp) {
    NTSTATUS status;
    WCHAR (*str)[512];
    UINT32 str_len;
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);

    /* Allocate the working buffer. */
    str = wv_mallocz(sizeof *str);
    if (str == NULL) {
        DBG("wv_malloc IRP_MN_QUERY_ID\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto alloc_str;
      }
    /* Invoke the specific device's ID query. */
    str_len = WvDevPnpId(
        dev,
        io_stack_loc->Parameters.QueryId.IdType,
        str
      );
    if (str_len == 0) {
        irp->IoStatus.Information = 0;
        status = STATUS_NOT_SUPPORTED;
        goto alloc_info;
      }
    /* Allocate the return buffer. */
    irp->IoStatus.Information = (ULONG_PTR) wv_palloc(str_len * sizeof **str);
    if (irp->IoStatus.Information == 0) {
        DBG("wv_palloc failed.\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto alloc_info;
      }
    /* Copy the working buffer to the return buffer. */
    RtlCopyMemory(
        (PVOID) irp->IoStatus.Information,
        str,
        str_len * sizeof **str
      );
    status = STATUS_SUCCESS;

    /* irp->IoStatus.Information not freed. */
    alloc_info:

    wv_free(str);
    alloc_str:

    return WvlIrpComplete(irp, irp->IoStatus.Information, status);
  }

/**
 * Close a device.
 *
 * @v Dev               Points to the device to close.
 */
WVL_M_LIB VOID STDCALL WvDevClose(IN WV_SP_DEV_T Dev) {
    /* Call the device's close routine. */
    if (Dev->Ops.Close)
      Dev->Ops.Close(Dev);
    return;
  }

/**
 * Delete a device.
 *
 * @v Dev               Points to the device to delete.
 */
WVL_M_LIB VOID STDCALL WvDevFree(IN WV_SP_DEV_T Dev) {
    /* Call the device's free routine. */
    if (Dev->Ops.Free)
      Dev->Ops.Free(Dev);
    return;
  }

/**
 * Default device deletion operation.
 *
 * @v dev               Points to the device to delete.
 */
static VOID STDCALL WvDevFreeDev_(IN WV_SP_DEV_T dev) {
    wv_free(dev);
  }

/**
 * Get a device from a DEVICE_OBJECT.
 *
 * @v dev_obj           Points to the DEVICE_OBJECT to get the device from.
 * @ret                 Returns a pointer to the device on success, else NULL.
 */
WVL_M_LIB WV_SP_DEV_T WvDevFromDevObj(PDEVICE_OBJECT dev_obj) {
    WV_SP_DEV_EXT dev_ext;

    if (!dev_obj)
      return NULL;
    dev_ext = dev_obj->DeviceExtension;
    return dev_ext->device;
  }

/**
 * Set the device for a DEVICE_OBJECT.
 *
 * @v dev_obj           Points to the DEVICE_OBJECT to set the device for.
 * @v dev               Points to the device to associate with.
 */
WVL_M_LIB VOID WvDevForDevObj(PDEVICE_OBJECT dev_obj, WV_SP_DEV_T dev) {
    WV_SP_DEV_EXT dev_ext = dev_obj->DeviceExtension;
    dev_ext->device = dev;
    return;
  }

/**
 * Get a device's IRP handler routine.
 *
 * @v dev_obj                   Points to the DEVICE_OBJECT to get
 *                              the handler for.
 * @ret PDRIVER_DISPATCH        Points to the IRP handler routine.
 */
WVL_M_LIB PDRIVER_DISPATCH STDCALL WvDevGetIrpHandler(
    IN PDEVICE_OBJECT dev_obj
  ) {
    WV_SP_DEV_EXT dev_ext = dev_obj->DeviceExtension;
    return dev_ext->IrpDispatch;
  }

/**
 * Set a device's IRP handler routine.
 *
 * @v dev_obj           Points to the DEVICE_OBJECT to set the handler for.
 * @v irp_dispatch      Points to the IRP handler routine.
 */
WVL_M_LIB VOID STDCALL WvDevSetIrpHandler(
    IN PDEVICE_OBJECT dev_obj,
    IN PDRIVER_DISPATCH irp_dispatch
  ) {
    WV_SP_DEV_EXT dev_ext = dev_obj->DeviceExtension;
    dev_ext->IrpDispatch = irp_dispatch;
    return;
  }
