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
 * RAM disk specifics
 *
 */

#include <stdio.h>
#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "disk.h"
#include "ramdisk.h"
#include "debug.h"

/* With thanks to karyonix, who makes FiraDisk */
static __inline void STDCALL
fast_copy (
  void *dest,
  const void *src,
  size_t count
 )
{
  __movsd ( dest, src, count >> 2 );
}

static
disk__io_decl (
  io
 )
{
  PHYSICAL_ADDRESS PhysicalAddress;
  winvblock__uint8_ptr PhysicalMemory;
  disk__type_ptr disk_ptr;
  ramdisk__type_ptr ramdisk_ptr;

  /*
   * Establish pointers into the disk device's extension space
   */
  disk_ptr = get_disk_ptr ( dev_ext_ptr );
  ramdisk_ptr = ramdisk__get_ptr ( dev_ext_ptr );

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

  PhysicalAddress.QuadPart =
    ramdisk_ptr->DiskBuf + ( start_sector * disk_ptr->SectorSize );
  /*
   * Possible precision loss
   */
  PhysicalMemory =
    MmMapIoSpace ( PhysicalAddress, sector_count * disk_ptr->SectorSize,
		   MmNonCached );
  if ( !PhysicalMemory )
    {
      DBG ( "Could not map memory for RAM disk!\n" );
      irp->IoStatus.Information = 0;
      irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
      IoCompleteRequest ( irp, IO_NO_INCREMENT );
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  if ( mode == disk__io_mode_write )
    fast_copy ( PhysicalMemory, buffer, sector_count * disk_ptr->SectorSize );
  else
    fast_copy ( buffer, PhysicalMemory, sector_count * disk_ptr->SectorSize );
  MmUnmapIoSpace ( PhysicalMemory, sector_count * disk_ptr->SectorSize );
  irp->IoStatus.Information = sector_count * disk_ptr->SectorSize;
  irp->IoStatus.Status = STATUS_SUCCESS;
  IoCompleteRequest ( irp, IO_NO_INCREMENT );
  return STATUS_SUCCESS;
}

static
disk__pnp_id_decl (
  query_id
 )
{
  ramdisk__type_ptr ramdisk_ptr = ramdisk__get_ptr ( &disk_ptr->dev_ext );
  static PWCHAR hw_ids[disk__media_count] =
    { winvblock__literal_w L"\\RAMFloppyDisk",
    winvblock__literal_w L"\\RAMHardDisk",
    winvblock__literal_w L"\\RAMOpticalDisc"
  };

  switch ( query_type )
    {
      case BusQueryDeviceID:
	return swprintf ( buf_512, hw_ids[disk_ptr->media] ) + 1;
      case BusQueryInstanceID:
	/*
	 * "Location" 
	 */
	return swprintf ( buf_512, L"RAM_at_%08X", ramdisk_ptr->DiskBuf ) + 1;
      case BusQueryHardwareIDs:
	{
	  winvblock__uint32 tmp;
	  tmp = swprintf ( buf_512, hw_ids[disk_ptr->media] ) + 1;
	  tmp +=
	    swprintf ( &buf_512[tmp], disk__compat_ids[disk_ptr->media] ) + 4;
	  return tmp;
	}
      case BusQueryCompatibleIDs:
	return swprintf ( buf_512, disk__compat_ids[disk_ptr->media] ) + 4;
      default:
	return 0;
    }
}

disk__ops ramdisk__default_ops = {
  io,
  disk__default_max_xfer_len,
  disk__default_init,
  query_id
};
