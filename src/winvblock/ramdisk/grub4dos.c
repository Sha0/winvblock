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
 * GRUB4DOS RAM disk specifics
 *
 */

#include <stdio.h>
#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "device.h"
#include "disk.h"
#include "bus.h"
#include "ramdisk.h"
#include "debug.h"
#include "probe.h"

/* From GRUB4DOS 0.4.4's stage2/shared.h */
winvblock__def_struct ( grub4dos_drive_mapping )
{
  winvblock__uint8 SourceDrive;
  winvblock__uint8 DestDrive;
  winvblock__uint8 MaxHead;
  winvblock__uint8 MaxSector:6;
  winvblock__uint8 RestrictionX:1;
  winvblock__uint16 DestMaxCylinder:13;
  winvblock__uint16 SourceODD:1;
  winvblock__uint16 DestODD:1;
  winvblock__uint16 DestLBASupport:1;
  winvblock__uint8 DestMaxHead;
  winvblock__uint8 DestMaxSector:6;
  winvblock__uint8 RestrictionY:1;
  winvblock__uint8 InSituOption:1;
  winvblock__uint64 SectorStart;
  winvblock__uint64 SectorCount;
};

void
grub4dos__find (
  void
 )
{
  PHYSICAL_ADDRESS PhysicalAddress;
  winvblock__uint8_ptr PhysicalMemory;
  int_vector_ptr InterruptVector;
  winvblock__uint32 Int13Hook;
  safe_mbr_hook_ptr SafeMbrHookPtr;
  grub4dos_drive_mapping_ptr Grub4DosDriveMapSlotPtr;
  winvblock__uint32 i = 8;
  winvblock__bool FoundGrub4DosMapping = FALSE;
  ramdisk__type ramdisk = { 0 };

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
    ( int_vector_ptr ) ( PhysicalMemory + 0x13 * sizeof ( int_vector ) );
  /*
   * Walk the "safe hook" chain of INT 13h hooks as far as possible
   */
  while ( SafeMbrHookPtr = get_safe_hook ( PhysicalMemory, InterruptVector ) )
    {
      if ( !
	   ( RtlCompareMemory ( SafeMbrHookPtr->VendorID, "GRUB4DOS", 8 ) ==
	     8 ) )
	{
	  DBG ( "Non-GRUB4DOS INT 0x13 Safe Hook\n" );
	  InterruptVector = &SafeMbrHookPtr->PrevHook;
	  continue;
	}
      Grub4DosDriveMapSlotPtr =
	( grub4dos_drive_mapping_ptr ) ( PhysicalMemory +
					 ( ( ( winvblock__uint32 )
					     InterruptVector->
					     Segment ) << 4 ) + 0x20 );
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
	  /*
	   * Possible precision loss
	   */
	  if ( Grub4DosDriveMapSlotPtr[i].SourceODD )
	    {
	      ramdisk.disk.media = disk__media_optical;
	      ramdisk.disk.SectorSize = 2048;
	    }
	  else
	    {
	      ramdisk.disk.media =
		Grub4DosDriveMapSlotPtr[i].SourceDrive & 0x80 ?
		disk__media_hard : disk__media_floppy;
	      ramdisk.disk.SectorSize = 512;
	    }
	  DBG ( "RAM Drive is type: %d\n", ramdisk.disk.media );
	  ramdisk.DiskBuf =
	    ( winvblock__uint32 ) ( Grub4DosDriveMapSlotPtr[i].SectorStart *
				    512 );
	  ramdisk.disk.LBADiskSize = ramdisk.DiskSize =
	    ( winvblock__uint32 ) Grub4DosDriveMapSlotPtr[i].SectorCount;
	  ramdisk.disk.Heads = Grub4DosDriveMapSlotPtr[i].MaxHead + 1;
	  ramdisk.disk.Sectors = Grub4DosDriveMapSlotPtr[i].DestMaxSector;
	  ramdisk.disk.Cylinders =
	    ramdisk.disk.LBADiskSize / ( ramdisk.disk.Heads *
					 ramdisk.disk.Sectors );
	  ramdisk.disk.BootDrive = TRUE;
	  ramdisk.disk.ops = &ramdisk__default_ops;
	  disk__put_dev_ops ( &ramdisk.disk.device );
	  ramdisk.disk.device.size = sizeof ( ramdisk__type );
	  FoundGrub4DosMapping = TRUE;
	  bus__add_child ( bus__boot (  ), &ramdisk.disk.device );
	}
      InterruptVector = &SafeMbrHookPtr->PrevHook;
    }

  MmUnmapIoSpace ( PhysicalMemory, 0x100000 );
  if ( !FoundGrub4DosMapping )
    {
      DBG ( "No GRUB4DOS drive mappings found\n" );
    }
}
