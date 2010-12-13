/**
 * Copyright (C) 2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
 * AoE bus specifics.
 */

#include <stdio.h>
#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "device.h"
#include "bus.h"
#include "aoe.h"
#include "mount.h"
#include "debug.h"

/* TODO: Remove this pull from aoe/driver.c */
extern irp__handler scan;
extern irp__handler show;
extern irp__handler mount;

/* Forward declarations. */
static NTSTATUS STDCALL aoe_bus__dev_ctl_(
    IN PDEVICE_OBJECT,
    IN PIRP,
    IN PIO_STACK_LOCATION,
    IN struct device__type *,
    OUT winvblock__bool_ptr
  );
static device__pnp_id_func aoe_bus__pnp_id_;

/* Globals. */
struct bus__type * aoe_bus = NULL;
static irp__handling aoe_bus__handling_table_[] = {
    /*
     * Major, minor, any major?, any minor?, handler
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     * Note that the fall-through case must come FIRST!
     * Why? It sets completion to true, so others won't be called.
     */
    { IRP_MJ_DEVICE_CONTROL, 0, FALSE, TRUE, aoe_bus__dev_ctl_ },
  };


static NTSTATUS STDCALL aoe_bus__dev_ctl_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    IN PIO_STACK_LOCATION io_stack_loc,
    IN struct device__type * dev,
    OUT winvblock__bool_ptr completion
  ) {
    NTSTATUS status = STATUS_NOT_SUPPORTED;

    switch(io_stack_loc->Parameters.DeviceIoControl.IoControlCode) {
        case IOCTL_AOE_SCAN:
          status = scan(dev_obj, irp, io_stack_loc, dev, completion);
          break;

        case IOCTL_AOE_SHOW:
          status = show(dev_obj, irp, io_stack_loc, dev, completion);
          break;

        case IOCTL_AOE_MOUNT:
          status = mount(dev_obj, irp, io_stack_loc, dev, completion);
          break;

        case IOCTL_AOE_UMOUNT:
          io_stack_loc->Parameters.DeviceIoControl.IoControlCode =
            IOCTL_FILE_DETACH;
          break;
      }
    if (*completion)
      IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

/**
 * Create the AoE bus.
 *
 * @ret         TRUE for success, else FALSE.
 */
winvblock__bool aoe_bus__create(void) {
    struct bus__type * new_bus;

    /* We should only be called once. */
    if (aoe_bus) {
        DBG("AoE bus already created\n");
        return FALSE;
      }
    /* Try to create the AoE bus. */
    new_bus = bus__create();
    if (!new_bus) {
        DBG("Failed to create AoE bus!\n");
        goto err_new_bus;
      }
    /* When the PDO is created, we need to handle PnP ID queries. */
    new_bus->device->ops.pnp_id = aoe_bus__pnp_id_;
    /* Name the bus when the PDO is created. */
    RtlInitUnicodeString(
        &new_bus->dev_name,
        L"\\Device\\AoE"
      );
    RtlInitUnicodeString(
        &new_bus->dos_dev_name,
        L"\\DosDevices\\AoE"
      );
    new_bus->named = TRUE;
    /* Add it as a sub-bus to WinVBlock. */
    if (!bus__add_child(driver__bus(), new_bus->device)) {
        DBG("Couldn't add AoE bus to WinVBlock bus!\n");
        goto err_add_child;
      }
    /* All done. */
    aoe_bus = new_bus;
    return TRUE;

    err_add_child:

    device__free(new_bus->device);
    err_new_bus:

    return FALSE;
  }

/* Destroy the AoE bus. */
void aoe_bus__free(void) {
    if (!aoe_bus)
      /* Nothing to do. */
      return;

    IoDeleteSymbolicLink(&aoe_bus->dos_dev_name);
    IoDeleteSymbolicLink(&aoe_bus->dev_name);
    IoDeleteDevice(aoe_bus->device->Self);
    irp__unreg_table(
        &aoe_bus->device->irp_handler_chain,
        aoe_bus__handling_table_
      );
    #if 0
    bus__remove_child(driver__bus(), aoe_bus->device);
    #endif
    device__free(aoe_bus->device);
    return;
  }

static winvblock__uint32 STDCALL aoe_bus__pnp_id_(
    IN struct device__type * dev,
    IN BUS_QUERY_ID_TYPE query_type,
    IN OUT WCHAR (*buf)[512]
  ) {
    switch (query_type) {
        case BusQueryDeviceID:
          return swprintf(*buf, L"WinVBlock\\AoEDEVID") + 1;

        case BusQueryInstanceID:
          return swprintf(*buf, L"WinVBlock\\AoEINSTID") + 1;

        case BusQueryHardwareIDs:
          return swprintf(*buf, L"WinVBlock\\AoEHWID") + 1;

        case BusQueryCompatibleIDs:
          return swprintf(*buf, L"WinVBlock\\AoECOMPATID") + 4;

        default:
          return 0;
      }
  }
