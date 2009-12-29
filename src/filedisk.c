/**
 * Copyright (C) 2009, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
 * File-backed disk specifics
 *
 */

#include <stdio.h>
#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "disk.h"
#include "mount.h"
#include "bus.h"
#include "filedisk.h"
#include "debug.h"

disk__io_decl ( filedisk__io )
{
  disk__type_ptr disk_ptr;
  filedisk__type_ptr filedisk_ptr;

  /*
   * Establish pointers into the disk device's extension space
   */
  disk_ptr = get_disk_ptr ( dev_ext_ptr );
  filedisk_ptr = filedisk__get_ptr ( dev_ext_ptr );

  if ( sector_count < 1 )
    {
      /*
       * A silly request 
       */
      DBG ( "sector_count < 1; cancelling\n" );
      irp->IoStatus.Information = 0;
      irp->IoStatus.Status = STATUS_CANCELLED;
      IoCompleteRequest ( irp, IO_NO_INCREMENT );
      return STATUS_CANCELLED;
    }

  if ( mode == disk__io_mode_write )
    ( void )0;
  else
    RtlZeroMemory ( buffer, sector_count * disk_ptr->SectorSize );
  irp->IoStatus.Information = sector_count * disk_ptr->SectorSize;
  irp->IoStatus.Status = STATUS_SUCCESS;
  IoCompleteRequest ( irp, IO_NO_INCREMENT );
  return STATUS_SUCCESS;
}

winvblock__uint32
filedisk__max_xfer_len (
  disk__type_ptr disk_ptr
 )
{
  return 1024 * 1024;
}

winvblock__uint32
filedisk__query_id (
  disk__type_ptr disk_ptr,
  BUS_QUERY_ID_TYPE query_type,
  PWCHAR buf_512
 )
{
  filedisk__type_ptr filedisk_ptr = filedisk__get_ptr ( &disk_ptr->dev_ext );

  switch ( query_type )
    {
      case BusQueryDeviceID:
	return swprintf ( buf_512, L"WinVBlock\\FileDisk%08x", 19 ) + 1;
      case BusQueryInstanceID:
	return swprintf ( buf_512, L"FileDisk%08x", 19 ) + 1;
      case BusQueryHardwareIDs:
	{
	  winvblock__uint32 tmp =
	    swprintf ( buf_512, L"WinVBlock\\FileDisk%08x",
		       19 ) + 1;
	  tmp +=
	    swprintf ( &buf_512[tmp],
		       disk_ptr->DiskType ==
		       OpticalDisc ? L"GenCdRom" : disk_ptr->DiskType ==
		       FloppyDisk ? L"GenSFloppy" : L"GenDisk" ) + 4;
	  return tmp;
	}
      case BusQueryCompatibleIDs:
	return swprintf ( buf_512,
			  disk_ptr->DiskType ==
			  OpticalDisc ? L"GenCdRom" : disk_ptr->DiskType ==
			  FloppyDisk ? L"GenSFloppy" : L"GenDisk" ) + 4;
      default:
	return 0;
    }
}

winvblock__bool STDCALL
filedisk__no_init (
  IN driver__dev_ext_ptr dev_ext
 )
{
  return TRUE;
}

void
filedisk__find (
  void
 )
{
  filedisk__type filedisk;
  bus__type_ptr bus_ptr;

  /*
   * Establish a pointer into the bus device's extension space
   */
  bus_ptr = get_bus_ptr ( ( driver__dev_ext_ptr ) bus__fdo->DeviceExtension );
  filedisk.disk.Initialize = filedisk__no_init;
  filedisk.disk.DiskType = HardDisk;
  filedisk.disk.SectorSize = 512;
  DBG ( "File-backed disk is type: %d\n", filedisk.disk.DiskType );
  filedisk.disk.LBADiskSize = 20480;
  filedisk.disk.Heads = 64;
  filedisk.disk.Sectors = 32;
  filedisk.disk.Cylinders = 10;
  filedisk.disk.io = filedisk__io;
  filedisk.disk.max_xfer_len = filedisk__max_xfer_len;
  filedisk.disk.query_id = filedisk__query_id;
  filedisk.disk.dev_ext.size = sizeof ( filedisk__type );
  if ( !Bus_AddChild ( bus__fdo, &filedisk.disk, TRUE ) )
    {
      DBG ( "Bus_AddChild() failed for file-backed disk\n" );
    }
  else if ( bus_ptr->PhysicalDeviceObject != NULL )
    {
      IoInvalidateDeviceRelations ( bus_ptr->PhysicalDeviceObject,
				    BusRelations );
    }
}
