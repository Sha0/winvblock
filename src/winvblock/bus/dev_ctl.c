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
 * Bus Device Control IRP handling
 *
 */

#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "device.h"
#include "disk.h"
#include "mount.h"
#include "bus.h"
#include "debug.h"
#include "filedisk.h"

static
irp__handler_decl (
  disk_detach
 )
{
  winvblock__uint8_ptr buffer = Irp->AssociatedIrp.SystemBuffer;
  device__type_ptr dev_walker;
  disk__type_ptr disk_walker = NULL,
    prev_disk_walker;
  bus__type_ptr bus_ptr;

  DBG ( "Request to detach disk: %d\n", *( winvblock__uint32_ptr ) buffer );
  bus_ptr = bus__get_ptr ( dev_ptr );
  dev_walker = bus_ptr->first_child_ptr;
  if ( dev_walker != NULL )
    disk_walker = disk__get_ptr ( dev_walker );
  prev_disk_walker = disk_walker;
  while ( ( disk_walker != NULL )
	  && ( disk_walker->DiskNumber != *( winvblock__uint32_ptr ) buffer ) )
    {
      prev_disk_walker = disk_walker;
      dev_walker = dev_walker->next_sibling_ptr;
      if ( dev_walker != NULL )
	disk_walker = disk__get_ptr ( dev_walker );
    }
  if ( disk_walker != NULL )
    {
      if ( disk_walker->BootDrive )
	{
	  DBG ( "Cannot unmount a boot drive.\n" );
	  Irp->IoStatus.Information = 0;
	  return STATUS_INVALID_DEVICE_REQUEST;
	}
      DBG ( "Deleting disk %d\n", disk_walker->DiskNumber );
      if ( disk_walker == disk__get_ptr ( bus_ptr->first_child_ptr ) )
	{
	  bus_ptr->first_child_ptr = dev_walker->next_sibling_ptr;
	}
      else
	{
	  prev_disk_walker->device->next_sibling_ptr =
	    dev_walker->next_sibling_ptr;
	}
      disk_walker->Unmount = TRUE;
      dev_walker->next_sibling_ptr = NULL;
      if ( bus_ptr->PhysicalDeviceObject != NULL )
	IoInvalidateDeviceRelations ( bus_ptr->PhysicalDeviceObject,
				      BusRelations );
    }
  bus_ptr->Children--;
  Irp->IoStatus.Information = 0;
  return STATUS_SUCCESS;
}

irp__handler_decl ( bus_dev_ctl__dispatch )
{
  NTSTATUS status;
  switch ( Stack->Parameters.DeviceIoControl.IoControlCode )
    {
      case IOCTL_FILE_ATTACH:
	status =
	  filedisk__attach ( DeviceObject, Irp, Stack, dev_ptr,
			     completion_ptr );
	break;
      case IOCTL_FILE_DETACH:
	status =
	  disk_detach ( DeviceObject, Irp, Stack, dev_ptr, completion_ptr );
	break;
      default:
	Irp->IoStatus.Information = 0;
	status = STATUS_INVALID_DEVICE_REQUEST;
    }

  Irp->IoStatus.Status = status;
  IoCompleteRequest ( Irp, IO_NO_INCREMENT );
  *completion_ptr = TRUE;
  return status;
}
