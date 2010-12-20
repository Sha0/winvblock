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
 * Bus Device Control IRP handling.
 */

#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "driver.h"
#include "device.h"
#include "disk.h"
#include "mount.h"
#include "bus.h"
#include "debug.h"
#include "filedisk.h"

/* Forward declarations. */
static device__dispatch_func WvBusDevCtlDiskDetach_;

static NTSTATUS STDCALL WvBusDevCtlDiskDetach_(
    IN struct device__type * dev,
    IN PIRP irp
  ) {
    winvblock__uint8_ptr buffer = irp->AssociatedIrp.SystemBuffer;
    winvblock__uint32 disk_num = *(winvblock__uint32_ptr) buffer;
    struct device__type * dev_walker;
    disk__type_ptr disk_walker = NULL, prev_disk_walker;
    WV_SP_BUS_T bus;

    DBG("Request to detach disk: %d\n", disk_num);
    bus = WvBusFromDev(dev);
    dev_walker = bus->first_child;
    if (dev_walker != NULL)
      disk_walker = disk__get_ptr(dev_walker);
    prev_disk_walker = disk_walker;
    while ((disk_walker != NULL) && (dev_walker->dev_num != disk_num)) {
        prev_disk_walker = disk_walker;
        dev_walker = dev_walker->next_sibling_ptr;
        if (dev_walker != NULL)
          disk_walker = disk__get_ptr(dev_walker);
      }
    if (disk_walker != NULL) {
        if (disk_walker->BootDrive) {
            DBG("Cannot unmount a boot drive.\n");
            irp->IoStatus.Information = 0;
            return STATUS_INVALID_DEVICE_REQUEST;
          }
        DBG("Deleting disk %d\n", dev_walker->dev_num);
        if (disk_walker == disk__get_ptr(bus->first_child))
          bus->first_child = dev_walker->next_sibling_ptr;
          else {
            prev_disk_walker->device->next_sibling_ptr =
              dev_walker->next_sibling_ptr;
          }
        disk_walker->Unmount = TRUE;
        dev_walker->next_sibling_ptr = NULL;
        if (bus->PhysicalDeviceObject != NULL)
          IoInvalidateDeviceRelations(bus->PhysicalDeviceObject, BusRelations);
      }
    bus->Children--;
    irp->IoStatus.Information = 0;
    return STATUS_SUCCESS;
  }

NTSTATUS STDCALL WvBusDevCtlDispatch(
    IN struct device__type * dev,
    IN PIRP irp,
    IN ULONG POINTER_ALIGNMENT code
  ) {
    NTSTATUS status;

    switch (code) {
        case IOCTL_FILE_ATTACH:
          status = filedisk__attach(dev, irp);
          break;

        case IOCTL_FILE_DETACH:
          status = WvBusDevCtlDiskDetach_(dev, irp);
          break;

        default:
          irp->IoStatus.Information = 0;
          status = STATUS_INVALID_DEVICE_REQUEST;
      }

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }
