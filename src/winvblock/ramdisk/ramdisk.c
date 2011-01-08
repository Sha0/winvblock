/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "ramdisk.h"
#include "debug.h"

/*
 * Yield a pointer to the RAM disk
 */
#define ramdisk_get_ptr( dev_ptr ) \
  ( ( WV_SP_RAMDISK_T ) ( disk__get_ptr ( dev_ptr ) )->ext )

/** Private. */
static WV_F_DEV_FREE free_ramdisk;
static WV_F_DISK_IO io;

/* With thanks to karyonix, who makes FiraDisk */
static __inline VOID STDCALL
fast_copy (
  PVOID dest,
  const VOID * src,
  size_t count
 )
{
  __movsd ( dest, src, count >> 2 );
}

static NTSTATUS STDCALL io(
    IN WV_SP_DEV_T dev_ptr,
    IN WVL_E_DISK_IO_MODE mode,
    IN LONGLONG start_sector,
    IN UINT32 sector_count,
    IN PUCHAR buffer,
    IN PIRP irp
  ) {
  PHYSICAL_ADDRESS PhysicalAddress;
  PUCHAR PhysicalMemory;
  WV_SP_DISK_T disk_ptr;
  WV_SP_RAMDISK_T ramdisk_ptr;

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
  if (mode == WvlDiskIoModeWrite)
    fast_copy ( PhysicalMemory, buffer, sector_count * disk_ptr->SectorSize );
  else
    fast_copy ( buffer, PhysicalMemory, sector_count * disk_ptr->SectorSize );
  MmUnmapIoSpace ( PhysicalMemory, sector_count * disk_ptr->SectorSize );
  irp->IoStatus.Information = sector_count * disk_ptr->SectorSize;
  irp->IoStatus.Status = STATUS_SUCCESS;
  IoCompleteRequest ( irp, IO_NO_INCREMENT );
  return STATUS_SUCCESS;
}

static UINT32 STDCALL query_id(
    IN WV_SP_DEV_T dev,
    IN BUS_QUERY_ID_TYPE query_type,
    IN OUT WCHAR (*buf)[512]
  ) {
    WV_SP_DISK_T disk = disk__get_ptr(dev);
    WV_SP_RAMDISK_T ramdisk = ramdisk_get_ptr(dev);
    static PWCHAR hw_ids[WvlDiskMediaTypes] = {
        WVL_M_WLIT L"\\RAMFloppyDisk",
        WVL_M_WLIT L"\\RAMHardDisk",
        WVL_M_WLIT L"\\RAMOpticalDisc"
      };

    switch (query_type) {
        case BusQueryDeviceID:
          return swprintf(*buf, hw_ids[disk->Media]) + 1;

        case BusQueryInstanceID:
        	/* "Location" */
          return swprintf(*buf, L"RAM_at_%08X", ramdisk->DiskBuf) + 1;

        case BusQueryHardwareIDs: {
            UINT32 tmp;

            tmp = swprintf(*buf, hw_ids[disk->Media]) + 1;
            tmp += swprintf(*buf + tmp, WvDiskCompatIds[disk->Media]) + 4;
            return tmp;
          }

        case BusQueryCompatibleIDs:
          return swprintf(*buf, WvDiskCompatIds[disk->Media]) + 4;

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
WV_SP_RAMDISK_T ramdisk__create(void) {
  WV_SP_DISK_T disk_ptr;
  WV_SP_RAMDISK_T ramdisk_ptr;

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
   * Populate non-zero device defaults
   */
  ramdisk_ptr->disk = disk_ptr;
  ramdisk_ptr->prev_free = disk_ptr->Dev->Ops.Free;
  disk_ptr->Dev->Ops.Free = free_ramdisk;
  disk_ptr->Dev->Ops.PnpId = query_id;
  disk_ptr->disk_ops.Io = io;
  disk_ptr->ext = ramdisk_ptr;
  disk_ptr->DriverObj = WvDriverObj;

  return ramdisk_ptr;

err_noramdisk:

  WvDevFree(disk_ptr->Dev);
err_nodisk:

  return NULL;
}

/**
 * Default RAM disk deletion operation.
 *
 * @v dev_ptr           Points to the RAM disk device to delete.
 */
static VOID STDCALL free_ramdisk(IN WV_SP_DEV_T dev_ptr) {
    WV_SP_DISK_T disk_ptr = disk__get_ptr(dev_ptr);
    WV_SP_RAMDISK_T ramdisk_ptr = ramdisk_get_ptr(dev_ptr);
    /* Free the "inherited class". */
    ramdisk_ptr->prev_free(dev_ptr);
  
    wv_free(ramdisk_ptr);
  }
