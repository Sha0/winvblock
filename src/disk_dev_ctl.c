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
 * Disk Device Control IRP handling
 *
 */

#include <ntddk.h>
#include <scsi.h>
#include <srb.h>
#include <ntdddisk.h>
#include <ntddscsi.h>
#include <ntddstor.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "disk.h"
#include "debug.h"

static
irp__handler_decl (
  storage_query_prop
 )
{
  NTSTATUS status = STATUS_INVALID_PARAMETER;
  PSTORAGE_PROPERTY_QUERY storage_prop_query = Irp->AssociatedIrp.SystemBuffer;
  winvblock__uint32 copy_size;
  disk__type_ptr disk_ptr = get_disk_ptr ( DeviceExtension );
  STORAGE_ADAPTER_DESCRIPTOR storage_adapter_desc;
  STORAGE_DEVICE_DESCRIPTOR storage_dev_desc;

  if ( storage_prop_query->PropertyId == StorageAdapterProperty
       && storage_prop_query->QueryType == PropertyStandardQuery )
    {
      copy_size =
	( Stack->Parameters.DeviceIoControl.OutputBufferLength <
	  sizeof ( STORAGE_ADAPTER_DESCRIPTOR ) ? Stack->Parameters.
	  DeviceIoControl.OutputBufferLength :
	  sizeof ( STORAGE_ADAPTER_DESCRIPTOR ) );
      storage_adapter_desc.Version = sizeof ( STORAGE_ADAPTER_DESCRIPTOR );
      storage_adapter_desc.Size = sizeof ( STORAGE_ADAPTER_DESCRIPTOR );
      storage_adapter_desc.MaximumTransferLength =
	disk_ptr->SectorSize * disk_ptr->AoE.MaxSectorsPerPacket;
#if 0
      storage_adapter_desc.MaximumTransferLength = SECTORSIZE * POOLSIZE;
#endif
      storage_adapter_desc.MaximumPhysicalPages = ( winvblock__uint32 ) - 1;
      storage_adapter_desc.AlignmentMask = 0;
      storage_adapter_desc.AdapterUsesPio = TRUE;
      storage_adapter_desc.AdapterScansDown = FALSE;
      storage_adapter_desc.CommandQueueing = FALSE;
      storage_adapter_desc.AcceleratedTransfer = FALSE;
      storage_adapter_desc.BusType = BusTypeScsi;
      RtlCopyMemory ( Irp->AssociatedIrp.SystemBuffer, &storage_adapter_desc,
		      copy_size );
      Irp->IoStatus.Information = ( winvblock__uint32_ptr ) copy_size;
      status = STATUS_SUCCESS;
    }

  if ( storage_prop_query->PropertyId == StorageDeviceProperty
       && storage_prop_query->QueryType == PropertyStandardQuery )
    {
      copy_size =
	( Stack->Parameters.DeviceIoControl.OutputBufferLength <
	  sizeof ( STORAGE_DEVICE_DESCRIPTOR ) ? Stack->Parameters.
	  DeviceIoControl.OutputBufferLength :
	  sizeof ( STORAGE_DEVICE_DESCRIPTOR ) );
      storage_dev_desc.Version = sizeof ( STORAGE_DEVICE_DESCRIPTOR );
      storage_dev_desc.Size = sizeof ( STORAGE_DEVICE_DESCRIPTOR );
      storage_dev_desc.DeviceType = DIRECT_ACCESS_DEVICE;
      storage_dev_desc.DeviceTypeModifier = 0;
      storage_dev_desc.RemovableMedia = FALSE;
      storage_dev_desc.CommandQueueing = FALSE;
      storage_dev_desc.VendorIdOffset = 0;
      storage_dev_desc.ProductIdOffset = 0;
      storage_dev_desc.ProductRevisionOffset = 0;
      storage_dev_desc.SerialNumberOffset = 0;
      storage_dev_desc.BusType = BusTypeScsi;
      storage_dev_desc.RawPropertiesLength = 0;
      RtlCopyMemory ( Irp->AssociatedIrp.SystemBuffer, &storage_dev_desc,
		      copy_size );
      Irp->IoStatus.Information = ( winvblock__uint32_ptr ) copy_size;
      status = STATUS_SUCCESS;
    }

  if ( status == STATUS_INVALID_PARAMETER )
    {
      DBG ( "!!Invalid IOCTL_STORAGE_QUERY_PROPERTY "
	    "(PropertyId: %08x / QueryType: %08x)!!\n",
	    storage_prop_query->PropertyId, storage_prop_query->QueryType );
    }
  return status;
}

static
irp__handler_decl (
  disk_get_drive_geom
 )
{
  winvblock__uint32 copy_size;
  DISK_GEOMETRY disk_geom;
  disk__type_ptr disk_ptr;

  copy_size =
    ( Stack->Parameters.DeviceIoControl.OutputBufferLength <
      sizeof ( DISK_GEOMETRY ) ? Stack->Parameters.
      DeviceIoControl.OutputBufferLength : sizeof ( DISK_GEOMETRY ) );
  disk_geom.MediaType = FixedMedia;
  disk_ptr = get_disk_ptr ( DeviceExtension );
  disk_geom.Cylinders.QuadPart = disk_ptr->Cylinders;
  disk_geom.TracksPerCylinder = disk_ptr->Heads;
  disk_geom.SectorsPerTrack = disk_ptr->Sectors;
  disk_geom.BytesPerSector = disk_ptr->SectorSize;
  RtlCopyMemory ( Irp->AssociatedIrp.SystemBuffer, &disk_geom, copy_size );
  Irp->IoStatus.Information = ( winvblock__uint32_ptr ) copy_size;
  return STATUS_SUCCESS;
}

static
irp__handler_decl (
  scsi_get_address
 )
{
  winvblock__uint32 copy_size;
  SCSI_ADDRESS scsi_address;
  disk__type_ptr disk_ptr;

  copy_size =
    ( Stack->Parameters.DeviceIoControl.OutputBufferLength <
      sizeof ( SCSI_ADDRESS ) ? Stack->Parameters.
      DeviceIoControl.OutputBufferLength : sizeof ( SCSI_ADDRESS ) );
  scsi_address.Length = sizeof ( SCSI_ADDRESS );
  scsi_address.PortNumber = 0;
  scsi_address.PathId = 0;
  disk_ptr = get_disk_ptr ( DeviceExtension );
  scsi_address.TargetId = ( winvblock__uint8 ) disk_ptr->DiskNumber;
  scsi_address.Lun = 0;
  RtlCopyMemory ( Irp->AssociatedIrp.SystemBuffer, &scsi_address, copy_size );
  Irp->IoStatus.Information = ( winvblock__uint32_ptr ) copy_size;
  return STATUS_SUCCESS;
}

irp__handler_decl ( disk_dev_ctl__dispatch )
{
  NTSTATUS status;

  switch ( Stack->Parameters.DeviceIoControl.IoControlCode )
    {
      case IOCTL_STORAGE_QUERY_PROPERTY:
	status =
	  storage_query_prop ( DeviceObject, Irp, Stack, DeviceExtension,
			       completion_ptr );
	break;
      case IOCTL_DISK_GET_DRIVE_GEOMETRY:
	status =
	  disk_get_drive_geom ( DeviceObject, Irp, Stack, DeviceExtension,
				completion_ptr );
	break;
      case IOCTL_SCSI_GET_ADDRESS:
	status =
	  scsi_get_address ( DeviceObject, Irp, Stack, DeviceExtension,
			     completion_ptr );
	break;
      default:
	Irp->IoStatus.Information = 0;
	status = STATUS_INVALID_PARAMETER;
    }

  Irp->IoStatus.Status = status;
  IoCompleteRequest ( Irp, IO_NO_INCREMENT );
  *completion_ptr = TRUE;
  return status;
}
