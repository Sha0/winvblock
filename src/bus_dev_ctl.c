/**
 * Copyright (C) 2009, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#include "disk.h"
#include "mount.h"
#include "bus.h"
#include "debug.h"
#include "aoe.h"

static
irp__handler_decl (
  aoe_scan
 )
{
  KIRQL irql;
  winvblock__uint32 count;
  bus__target_list_ptr target_walker;
  PMOUNT_TARGETS targets;

  DBG ( "Got IOCTL_AOE_SCAN...\n" );
  AoE_Start (  );
  KeAcquireSpinLock ( &Bus_Globals_TargetListSpinLock, &irql );

  count = 0;
  target_walker = Bus_Globals_TargetList;
  while ( target_walker != NULL )
    {
      count++;
      target_walker = target_walker->next;
    }

  if ( ( targets =
	 ( PMOUNT_TARGETS ) ExAllocatePool ( NonPagedPool,
					     sizeof ( MOUNT_TARGETS ) +
					     ( count *
					       sizeof ( MOUNT_TARGET ) ) ) ) ==
       NULL )
    {
      DBG ( "ExAllocatePool targets\n" );
      Irp->IoStatus.Information = 0;
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  Irp->IoStatus.Information =
    sizeof ( MOUNT_TARGETS ) + ( count * sizeof ( MOUNT_TARGET ) );
  targets->Count = count;

  count = 0;
  target_walker = Bus_Globals_TargetList;
  while ( target_walker != NULL )
    {
      RtlCopyMemory ( &targets->Target[count], &target_walker->Target,
		      sizeof ( MOUNT_TARGET ) );
      count++;
      target_walker = target_walker->next;
    }
  RtlCopyMemory ( Irp->AssociatedIrp.SystemBuffer, targets,
		  ( Stack->Parameters.DeviceIoControl.OutputBufferLength <
		    ( sizeof ( MOUNT_TARGETS ) +
		      ( count *
			sizeof ( MOUNT_TARGET ) ) ) ? Stack->Parameters.
		    DeviceIoControl.
		    OutputBufferLength : ( sizeof ( MOUNT_TARGETS ) +
					   ( count *
					     sizeof ( MOUNT_TARGET ) ) ) ) );
  ExFreePool ( targets );

  KeReleaseSpinLock ( &Bus_Globals_TargetListSpinLock, irql );
  return STATUS_SUCCESS;

}

static
irp__handler_decl (
  aoe_show
 )
{
  winvblock__uint32 count;
  disk__type_ptr disk_walker;
  bus__type_ptr bus_ptr;
  PMOUNT_DISKS disks;

  DBG ( "Got IOCTL_AOE_SHOW...\n" );

  bus_ptr = get_bus_ptr ( DeviceExtension );
  disk_walker = ( disk__type_ptr ) bus_ptr->first_child_ptr;
  count = 0;
  while ( disk_walker != NULL )
    {
      count++;
      disk_walker = disk_walker->next_sibling_ptr;
    }

  if ( ( disks =
	 ( PMOUNT_DISKS ) ExAllocatePool ( NonPagedPool,
					   sizeof ( MOUNT_DISKS ) +
					   ( count *
					     sizeof ( MOUNT_DISK ) ) ) ) ==
       NULL )
    {
      DBG ( "ExAllocatePool disks\n" );
      Irp->IoStatus.Information = 0;
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  Irp->IoStatus.Information =
    sizeof ( MOUNT_DISKS ) + ( count * sizeof ( MOUNT_DISK ) );
  disks->Count = count;

  count = 0;
  disk_walker = ( disk__type_ptr ) bus_ptr->first_child_ptr;
  while ( disk_walker != NULL )
    {
      disks->Disk[count].Disk = disk_walker->DiskNumber;
      RtlCopyMemory ( &disks->Disk[count].ClientMac,
		      &disk_walker->AoE.ClientMac, 6 );
      RtlCopyMemory ( &disks->Disk[count].ServerMac,
		      &disk_walker->AoE.ServerMac, 6 );
      disks->Disk[count].Major = disk_walker->AoE.Major;
      disks->Disk[count].Minor = disk_walker->AoE.Minor;
      disks->Disk[count].LBASize = disk_walker->LBADiskSize;
      count++;
      disk_walker = disk_walker->next_sibling_ptr;
    }
  RtlCopyMemory ( Irp->AssociatedIrp.SystemBuffer, disks,
		  ( Stack->Parameters.DeviceIoControl.OutputBufferLength <
		    ( sizeof ( MOUNT_DISKS ) +
		      ( count *
			sizeof ( MOUNT_DISK ) ) ) ? Stack->Parameters.
		    DeviceIoControl.
		    OutputBufferLength : ( sizeof ( MOUNT_DISKS ) +
					   ( count *
					     sizeof ( MOUNT_DISK ) ) ) ) );
  ExFreePool ( disks );

  return STATUS_SUCCESS;
}

static
irp__handler_decl (
  aoe_mount
 )
{
  winvblock__uint8_ptr buffer = Irp->AssociatedIrp.SystemBuffer;
  disk__type disk;
  bus__type_ptr bus_ptr;

  DBG ( "Got IOCTL_AOE_MOUNT for client: %02x:%02x:%02x:%02x:%02x:%02x "
	"Major:%d Minor:%d\n", buffer[0], buffer[1], buffer[2], buffer[3],
	buffer[4], buffer[5], *( winvblock__uint16_ptr ) ( &buffer[6] ),
	( winvblock__uint8 ) buffer[8] );
  disk.Initialize = AoE_SearchDrive;
  RtlCopyMemory ( disk.AoE.ClientMac, buffer, 6 );
  RtlFillMemory ( disk.AoE.ServerMac, 6, 0xff );
  disk.AoE.Major = *( winvblock__uint16_ptr ) ( &buffer[6] );
  disk.AoE.Minor = ( winvblock__uint8 ) buffer[8];
  disk.AoE.MaxSectorsPerPacket = 1;
  disk.AoE.Timeout = 200000;	/* 20 ms. */
  disk.IsRamdisk = FALSE;
  disk.io = aoe__disk_io;
  disk.max_xfer_len = aoe__max_xfer_len;
  if ( !Bus_AddChild ( DeviceObject, disk, FALSE ) )
    {
      DBG ( "Bus_AddChild() failed\n" );
    }
  else
    {
      bus_ptr = get_bus_ptr ( DeviceExtension );
      if ( bus_ptr->PhysicalDeviceObject != NULL )
	{

	  IoInvalidateDeviceRelations ( bus_ptr->PhysicalDeviceObject,
					BusRelations );
	}
    }
  Irp->IoStatus.Information = 0;
  return STATUS_SUCCESS;
}

static
irp__handler_decl (
  aoe_umount
 )
{
  winvblock__uint8_ptr buffer = Irp->AssociatedIrp.SystemBuffer;
  disk__type_ptr disk_walker,
   prev_disk_walker;
  bus__type_ptr bus_ptr;

  DBG ( "Got IOCTL_AOE_UMOUNT for disk: %d\n",
	*( winvblock__uint32_ptr ) buffer );
  bus_ptr = get_bus_ptr ( DeviceExtension );
  disk_walker = ( disk__type_ptr ) bus_ptr->first_child_ptr;
  prev_disk_walker = disk_walker;
  while ( ( disk_walker != NULL )
	  && ( disk_walker->DiskNumber != *( winvblock__uint32_ptr ) buffer ) )
    {
      prev_disk_walker = disk_walker;
      disk_walker = disk_walker->next_sibling_ptr;
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
      if ( disk_walker == ( disk__type_ptr ) bus_ptr->first_child_ptr )
	{
	  bus_ptr->first_child_ptr =
	    ( winvblock__uint8_ptr ) disk_walker->next_sibling_ptr;
	}
      else
	{
	  prev_disk_walker->next_sibling_ptr = disk_walker->next_sibling_ptr;
	}
      disk_walker->Unmount = TRUE;
      disk_walker->next_sibling_ptr = NULL;
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
      case IOCTL_AOE_SCAN:
	status =
	  aoe_scan ( DeviceObject, Irp, Stack, DeviceExtension,
		     completion_ptr );
	break;
      case IOCTL_AOE_SHOW:
	status =
	  aoe_show ( DeviceObject, Irp, Stack, DeviceExtension,
		     completion_ptr );
	break;
      case IOCTL_AOE_MOUNT:
	status =
	  aoe_mount ( DeviceObject, Irp, Stack, DeviceExtension,
		      completion_ptr );
	break;
      case IOCTL_AOE_UMOUNT:
	status =
	  aoe_umount ( DeviceObject, Irp, Stack, DeviceExtension,
		       completion_ptr );
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
