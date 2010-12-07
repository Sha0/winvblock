/**
 * Copyright (C) 2009-2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
 * RAM disk specifics.
 */

#include <stdio.h>
#include <ntddk.h>

#include "winvblock.h"
#include "wv_stdlib.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "device.h"
#include "disk.h"
#include "ramdisk.h"
#include "debug.h"

/*
 * Yield a pointer to the RAM disk
 */
#define ramdisk_get_ptr( dev_ptr ) \
  ( ( ramdisk__type_ptr ) ( disk__get_ptr ( dev_ptr ) )->ext )

/* Globals. */
static LIST_ENTRY ramdisk_list;
static KSPIN_LOCK ramdisk_list_lock;

/* Forward declarations. */
static device__free_func free_ramdisk;

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
   * Establish pointers to the disk and RAM disk
   */
  disk_ptr = disk__get_ptr ( dev_ptr );
  ramdisk_ptr = ramdisk_get_ptr ( dev_ptr );

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
  ramdisk__type_ptr ramdisk_ptr = ramdisk_get_ptr ( disk_ptr->device );
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

/**
 * Create a new RAM disk
 *
 * @ret ramdisk_ptr     The address of a new RAM disk, or NULL for failure
 *
 * See the header file for additional details
 */
ramdisk__type_ptr
ramdisk__create (
  void
 )
{
  disk__type_ptr disk_ptr;
  ramdisk__type_ptr ramdisk_ptr;

  /*
   * Try to create a disk
   */
  disk_ptr = disk__create (  );
  if ( disk_ptr == NULL )
    goto err_nodisk;
  /*
   * RAM disk devices might be used for booting and should
   * not be allocated from a paged memory pool
   */
  ramdisk_ptr = wv_mallocz(sizeof *ramdisk_ptr);
  if ( ramdisk_ptr == NULL )
    goto err_noramdisk;
  /*
   * Track the new RAM disk in our global list
   */
  ExInterlockedInsertTailList ( &ramdisk_list, &ramdisk_ptr->tracking,
				&ramdisk_list_lock );
  /*
   * Populate non-zero device defaults
   */
  ramdisk_ptr->disk = disk_ptr;
  ramdisk_ptr->prev_free = disk_ptr->device->ops.free;
  disk_ptr->device->ops.free = free_ramdisk;
  disk_ptr->disk_ops.io = io;
  disk_ptr->disk_ops.pnp_id = query_id;
  disk_ptr->ext = ramdisk_ptr;

  return ramdisk_ptr;

err_noramdisk:

  device__free ( disk_ptr->device );
err_nodisk:

  return NULL;
}

/**
 * Initialize the global, RAM disk-common environment
 *
 * @ret ntstatus        STATUS_SUCCESS or the NTSTATUS for a failure
 */
NTSTATUS
ramdisk__init (
  void
 )
{
  /*
   * Initialize the global list of RAM disks
   */
  InitializeListHead ( &ramdisk_list );
  KeInitializeSpinLock ( &ramdisk_list_lock );

  return STATUS_SUCCESS;
}

/**
 * Default RAM disk deletion operation.
 *
 * @v dev_ptr           Points to the RAM disk device to delete.
 */
static void STDCALL free_ramdisk(IN device__type_ptr dev_ptr)
  {
    disk__type_ptr disk_ptr = disk__get_ptr(dev_ptr);
    ramdisk__type_ptr ramdisk_ptr = ramdisk_get_ptr(dev_ptr);
    /* Free the "inherited class". */
    ramdisk_ptr->prev_free(dev_ptr);
    /*
     * Track the RAM disk deletion in our global list.  Unfortunately,
     * for now we have faith that a RAM disk won't be deleted twice and
     * result in a race condition.  Something to keep in mind...
     */
    ExInterlockedRemoveHeadList(
        ramdisk_ptr->tracking.Blink,
  			&ramdisk_list_lock
      );
  
    wv_free(ramdisk_ptr);
  }
