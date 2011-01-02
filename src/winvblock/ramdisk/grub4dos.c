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
 * GRUB4DOS RAM disk specifics.
 */

#include <stdio.h>
#include <ntddk.h>

#include "winvblock.h"
#include "wv_string.h"
#include "portable.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "ramdisk.h"
#include "debug.h"
#include "probe.h"
#include "grub4dos.h"

void
ramdisk_grub4dos__find (
  void
 )
{
  PHYSICAL_ADDRESS PhysicalAddress;
  PUCHAR PhysicalMemory;
  WV_SP_PROBE_INT_VECTOR InterruptVector;
  winvblock__uint32 Int13Hook;
  WV_SP_PROBE_SAFE_MBR_HOOK SafeMbrHookPtr;
  WV_SP_GRUB4DOS_DRIVE_MAPPING Grub4DosDriveMapSlotPtr;
  winvblock__uint32 i = 8;
  winvblock__bool FoundGrub4DosMapping = FALSE;
  WV_SP_RAMDISK_T ramdisk_ptr;

  /*
   * Find a GRUB4DOS memory-mapped disk.  Start by looking at the
   * real-mode IDT and following the "SafeMBRHook" INT 0x13 hook
   */
  PhysicalAddress.QuadPart = 0LL;
  PhysicalMemory = MmMapIoSpace ( PhysicalAddress, 0x100000, MmNonCached );
  if ( !PhysicalMemory )
    {
      DBG ( "Could not map low memory\n" );
      return;
    }
  InterruptVector =
    (WV_SP_PROBE_INT_VECTOR) (PhysicalMemory + 0x13 * sizeof *InterruptVector);
  /* Walk the "safe hook" chain of INT 13h hooks as far as possible. */
  while (SafeMbrHookPtr = WvProbeGetSafeHook(PhysicalMemory, InterruptVector)) {
      if (!wv_memcmpeq(
          SafeMbrHookPtr->VendorId,
          "GRUB4DOS",
          sizeof "GRUB4DOS" - 1
        )) {
	  DBG ( "Non-GRUB4DOS INT 0x13 Safe Hook\n" );
	  InterruptVector = &SafeMbrHookPtr->PrevHook;
	  continue;
	}
      Grub4DosDriveMapSlotPtr = (WV_SP_GRUB4DOS_DRIVE_MAPPING) (
          PhysicalMemory +
          (((winvblock__uint32) InterruptVector->Segment) << 4) +
          0x20
        );
      while ( i-- )
	{
	  DBG ( "GRUB4DOS SourceDrive: 0x%02x\n",
		Grub4DosDriveMapSlotPtr[i].SourceDrive );
	  DBG ( "GRUB4DOS DestDrive: 0x%02x\n",
		Grub4DosDriveMapSlotPtr[i].DestDrive );
	  DBG ( "GRUB4DOS MaxHead: %d\n", Grub4DosDriveMapSlotPtr[i].MaxHead );
	  DBG ( "GRUB4DOS MaxSector: %d\n",
		Grub4DosDriveMapSlotPtr[i].MaxSector );
	  DBG ( "GRUB4DOS DestMaxCylinder: %d\n",
		Grub4DosDriveMapSlotPtr[i].DestMaxCylinder );
	  DBG ( "GRUB4DOS DestMaxHead: %d\n",
		Grub4DosDriveMapSlotPtr[i].DestMaxHead );
	  DBG ( "GRUB4DOS DestMaxSector: %d\n",
		Grub4DosDriveMapSlotPtr[i].DestMaxSector );
	  DBG ( "GRUB4DOS SectorStart: 0x%08x\n",
		Grub4DosDriveMapSlotPtr[i].SectorStart );
	  DBG ( "GRUB4DOS SectorCount: %d\n",
		Grub4DosDriveMapSlotPtr[i].SectorCount );
	  if ( !( Grub4DosDriveMapSlotPtr[i].DestDrive == 0xff ) )
	    {
	      DBG ( "Skipping non-RAM disk GRUB4DOS mapping\n" );
	      continue;
	    }
	  ramdisk_ptr = ramdisk__create (  );
	  if ( ramdisk_ptr == NULL )
	    {
	      DBG ( "Could not create GRUB4DOS disk!\n" );
	      return;
	    }
	  /*
	   * Possible precision loss
	   */
	  if ( Grub4DosDriveMapSlotPtr[i].SourceODD )
	    {
	      ramdisk_ptr->disk->Media = WvDiskMediaTypeOptical;
	      ramdisk_ptr->disk->SectorSize = 2048;
	    }
	  else
	    {
	      ramdisk_ptr->disk->Media =
		Grub4DosDriveMapSlotPtr[i].SourceDrive & 0x80 ?
		WvDiskMediaTypeHard : WvDiskMediaTypeFloppy;
	      ramdisk_ptr->disk->SectorSize = 512;
	    }
	  DBG ( "RAM Drive is type: %d\n", ramdisk_ptr->disk->Media );
	  ramdisk_ptr->DiskBuf =
	    ( winvblock__uint32 ) ( Grub4DosDriveMapSlotPtr[i].SectorStart *
				    512 );
	  ramdisk_ptr->disk->LBADiskSize = ramdisk_ptr->DiskSize =
	    ( winvblock__uint32 ) Grub4DosDriveMapSlotPtr[i].SectorCount;
	  ramdisk_ptr->disk->Heads = Grub4DosDriveMapSlotPtr[i].MaxHead + 1;
	  ramdisk_ptr->disk->Sectors =
	    Grub4DosDriveMapSlotPtr[i].DestMaxSector;
	  ramdisk_ptr->disk->Cylinders =
	    ramdisk_ptr->disk->LBADiskSize / ( ramdisk_ptr->disk->Heads *
					       ramdisk_ptr->disk->Sectors );
	  ramdisk_ptr->disk->Dev->Boot = TRUE;
	  FoundGrub4DosMapping = TRUE;
 	  /* Add the ramdisk to the bus. */
	  if (!WvDriverBusAddDev(ramdisk_ptr->disk->Dev))
      WvDevFree(ramdisk_ptr->disk->Dev);
	}
      InterruptVector = &SafeMbrHookPtr->PrevHook;
    }

  MmUnmapIoSpace ( PhysicalMemory, 0x100000 );
  if ( !FoundGrub4DosMapping )
    {
      DBG ( "No GRUB4DOS drive mappings found\n" );
    }
}
