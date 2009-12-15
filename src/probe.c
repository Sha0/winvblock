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
 * Boot-time disk probing specifics
 *
 */

#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "disk.h"
#include "bus.h"
#include "debug.h"
#include "mdi.h"
#include "bus.h"
#include "aoe.h"

winvblock__def_struct ( int_vector )
{
  winvblock__uint16 Offset;
  winvblock__uint16 Segment;
};

#ifdef _MSC_VER
#  pragma pack(1)
#endif
winvblock__def_struct ( abft )
{
  winvblock__uint32 Signature;	/* 0x54464261 (aBFT) */
  winvblock__uint32 Length;
  winvblock__uint8 Revision;
  winvblock__uint8 Checksum;
  winvblock__uint8 OEMID[6];
  winvblock__uint8 OEMTableID[8];
  winvblock__uint8 Reserved1[12];
  winvblock__uint16 Major;
  winvblock__uint8 Minor;
  winvblock__uint8 Reserved2;
  winvblock__uint8 ClientMac[6];
}

__attribute__ ( ( __packed__ ) );
#ifdef _MSC_VER
#  pragma pack()
#endif

#ifdef _MSC_VER
#  pragma pack(1)
#endif
winvblock__def_struct ( safe_mbr_hook )
{
  winvblock__uint8 Jump[3];
  winvblock__uint8 Signature[8];
  winvblock__uint8 VendorID[8];
  int_vector PrevHook;
  winvblock__uint32 Flags;
  winvblock__uint32 mBFT;	/* MEMDISK only */
}

__attribute__ ( ( __packed__ ) );
#ifdef _MSC_VER
#  pragma pack()
#endif

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

static winvblock__bool STDCALL
no_init (
  IN driver__dev_ext_ptr dev_ext
 )
{
  return TRUE;
}

void
find_aoe_disks (
  void
 )
{
  PHYSICAL_ADDRESS PhysicalAddress;
  winvblock__uint8_ptr PhysicalMemory;
  winvblock__uint32 Offset,
   Checksum,
   i;
  winvblock__bool FoundAbft = FALSE;
  abft AoEBootRecord;
  disk__type Disk;
  bus__type_ptr bus_ptr;

  /*
   * Establish a pointer into the bus device's extension space
   */
  bus_ptr = get_bus_ptr ( ( driver__dev_ext_ptr ) bus__fdo->DeviceExtension );
  /*
   * Find aBFT
   */
  PhysicalAddress.QuadPart = 0LL;
  PhysicalMemory = MmMapIoSpace ( PhysicalAddress, 0xa0000, MmNonCached );
  if ( !PhysicalMemory )
    {
      DBG ( "Could not map low memory\n" );
    }
  else
    {
      for ( Offset = 0; Offset < 0xa0000; Offset += 0x10 )
	{
	  if ( ( ( abft_ptr ) & PhysicalMemory[Offset] )->Signature ==
	       0x54464261 )
	    {
	      Checksum = 0;
	      for ( i = 0;
		    i < ( ( abft_ptr ) & PhysicalMemory[Offset] )->Length;
		    i++ )
		Checksum += PhysicalMemory[Offset + i];
	      if ( Checksum & 0xff )
		continue;
	      if ( ( ( abft_ptr ) & PhysicalMemory[Offset] )->Revision != 1 )
		{
		  DBG ( "Found aBFT with mismatched revision v%d at "
			"segment 0x%4x. want v1.\n",
			( ( abft_ptr ) & PhysicalMemory[Offset] )->Revision,
			( Offset / 0x10 ) );
		  continue;
		}
	      DBG ( "Found aBFT at segment: 0x%04x\n", ( Offset / 0x10 ) );
	      RtlCopyMemory ( &AoEBootRecord, &PhysicalMemory[Offset],
			      sizeof ( abft ) );
	      FoundAbft = TRUE;
	      break;
	    }
	}
      MmUnmapIoSpace ( PhysicalMemory, 0xa0000 );
    }

#ifdef RIS
  FoundAbft = TRUE;
  RtlCopyMemory ( AoEBootRecord.ClientMac, "\x00\x0c\x29\x34\x69\x34", 6 );
  AoEBootRecord.Major = 0;
  AoEBootRecord.Minor = 10;
#endif

  if ( FoundAbft )
    {
      DBG ( "Attaching AoE disk from client NIC "
	    "%02x:%02x:%02x:%02x:%02x:%02x to major: %d minor: %d\n",
	    AoEBootRecord.ClientMac[0], AoEBootRecord.ClientMac[1],
	    AoEBootRecord.ClientMac[2], AoEBootRecord.ClientMac[3],
	    AoEBootRecord.ClientMac[4], AoEBootRecord.ClientMac[5],
	    AoEBootRecord.Major, AoEBootRecord.Minor );
      Disk.Initialize = AoE_SearchDrive;
      RtlCopyMemory ( Disk.AoE.ClientMac, AoEBootRecord.ClientMac, 6 );
      RtlFillMemory ( Disk.AoE.ServerMac, 6, 0xff );
      Disk.AoE.Major = AoEBootRecord.Major;
      Disk.AoE.Minor = AoEBootRecord.Minor;
      Disk.AoE.MaxSectorsPerPacket = 1;
      Disk.AoE.Timeout = 200000;	/* 20 ms. */
      Disk.IsRamdisk = FALSE;

      if ( !Bus_AddChild ( bus__fdo, Disk, TRUE ) )
	DBG ( "Bus_AddChild() failed for aBFT AoE disk\n" );
      else
	{
	  if ( bus_ptr->PhysicalDeviceObject != NULL )
	    {
	      IoInvalidateDeviceRelations ( bus_ptr->PhysicalDeviceObject,
					    BusRelations );
	    }
	}
    }
  else
    {
      DBG ( "No aBFT found\n" );
    }
}

static safe_mbr_hook_ptr STDCALL
get_safe_hook (
  IN winvblock__uint8_ptr PhysicalMemory,
  IN int_vector_ptr InterruptVector
 )
{
  winvblock__uint32 Int13Hook;
  safe_mbr_hook_ptr SafeMbrHookPtr;
  winvblock__uint8 Signature[9] = { 0 };
  winvblock__uint8 VendorID[9] = { 0 };

  Int13Hook =
    ( ( ( winvblock__uint32 ) InterruptVector->Segment ) << 4 ) +
    ( ( winvblock__uint32 ) InterruptVector->Offset );
  SafeMbrHookPtr = ( safe_mbr_hook_ptr ) ( PhysicalMemory + Int13Hook );
  RtlCopyMemory ( Signature, SafeMbrHookPtr->Signature, 8 );
  RtlCopyMemory ( VendorID, SafeMbrHookPtr->VendorID, 8 );
  DBG ( "INT 0x13 Segment: 0x%04x\n", InterruptVector->Segment );
  DBG ( "INT 0x13 Offset: 0x%04x\n", InterruptVector->Offset );
  DBG ( "INT 0x13 Hook: 0x%08x\n", Int13Hook );
  DBG ( "INT 0x13 Safe Hook Signature: %s\n", Signature );
  if ( !( RtlCompareMemory ( Signature, "$INT13SF", 8 ) == 8 ) )
    {
      DBG ( "Invalid INT 0x13 Safe Hook Signature; End of chain\n" );
      return NULL;
    }
  return SafeMbrHookPtr;
}

winvblock__bool STDCALL
check_mbft (
  winvblock__uint8_ptr PhysicalMemory,
  winvblock__uint32 Offset
 )
{
  PMDI_MBFT mBFT = ( PMDI_MBFT ) ( PhysicalMemory + Offset );
  winvblock__uint32 i;
  winvblock__uint8 Checksum = 0;
  safe_mbr_hook_ptr AssociatedHook;
  disk__type Disk;
  bus__type_ptr bus_ptr;

  /*
   * Establish a pointer into the bus device's extension space
   */
  bus_ptr = get_bus_ptr ( ( driver__dev_ext_ptr ) bus__fdo->DeviceExtension );

  if ( Offset >= 0x100000 )
    {
      DBG ( "mBFT physical pointer too high!\n" );
      return FALSE;
    }
  if ( !( RtlCompareMemory ( mBFT->Signature, "mBFT", 4 ) == 4 ) )
    return FALSE;
  if ( Offset + mBFT->Length >= 0x100000 )
    {
      DBG ( "mBFT length out-of-bounds\n" );
      return FALSE;
    }
  for ( i = 0; i < mBFT->Length; i++ )
    Checksum += ( ( winvblock__uint8 * ) mBFT )[i];
  if ( Checksum )
    {
      DBG ( "Invalid mBFT checksum\n" );
      return FALSE;
    }
  DBG ( "Found mBFT: 0x%08x\n", mBFT );
  if ( mBFT->SafeHook >= 0x100000 )
    {
      DBG ( "mBFT safe hook physical pointer too high!\n" );
      return FALSE;
    }
  AssociatedHook = ( safe_mbr_hook_ptr ) ( PhysicalMemory + mBFT->SafeHook );
  if ( AssociatedHook->Flags )
    {
      DBG ( "This MEMDISK already processed\n" );
      return TRUE;
    }
  DBG ( "MEMDISK DiskBuf: 0x%08x\n", mBFT->MDI.diskbuf );
  DBG ( "MEMDISK DiskSize: %d sectors\n", mBFT->MDI.disksize );
  Disk.Initialize = no_init;
  Disk.RAMDisk.DiskBuf = mBFT->MDI.diskbuf;
  Disk.LBADiskSize = Disk.RAMDisk.DiskSize = mBFT->MDI.disksize;
  if ( mBFT->MDI.driveno == 0xE0 )
    {
      Disk.DiskType = OpticalDisc;
      Disk.SectorSize = 2048;
    }
  else
    {
      if ( mBFT->MDI.driveno & 0x80 )
	Disk.DiskType = HardDisk;
      else
	Disk.DiskType = FloppyDisk;
      Disk.SectorSize = 512;
    }
  DBG ( "RAM Drive is type: %d\n", Disk.DiskType );
  Disk.IsRamdisk = TRUE;
  if ( !Bus_AddChild ( bus__fdo, Disk, TRUE ) )
    {
      DBG ( "Bus_AddChild() failed for MEMDISK\n" );
    }
  else if ( bus_ptr->PhysicalDeviceObject != NULL )
    {
      IoInvalidateDeviceRelations ( bus_ptr->PhysicalDeviceObject,
				    BusRelations );
    }
  AssociatedHook->Flags = 1;
  return TRUE;
}

void
find_memdisks (
  void
 )
{
  PHYSICAL_ADDRESS PhysicalAddress;
  winvblock__uint8_ptr PhysicalMemory;
  int_vector_ptr InterruptVector;
  safe_mbr_hook_ptr SafeMbrHookPtr;
  winvblock__uint32 Offset;
  winvblock__bool FoundMemdisk = FALSE;

  /*
   * Find a MEMDISK.  We check the "safe hook" chain, then scan for mBFTs
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
	   ( RtlCompareMemory ( SafeMbrHookPtr->VendorID, "MEMDISK ", 8 ) ==
	     8 ) )
	{
	  DBG ( "Non-MEMDISK INT 0x13 Safe Hook\n" );
	}
      else
	{
	  FoundMemdisk |= check_mbft ( PhysicalMemory, SafeMbrHookPtr->mBFT );
	}
      InterruptVector = &SafeMbrHookPtr->PrevHook;
    }
  /*
   * Now search for "floating" mBFTs
   */
  for ( Offset = 0; Offset < 0xFFFF0; Offset += 0x10 )
    {
      FoundMemdisk |= check_mbft ( PhysicalMemory, Offset );
    }
  MmUnmapIoSpace ( PhysicalMemory, 0x100000 );
  if ( !FoundMemdisk )
    {
      DBG ( "No MEMDISKs found\n" );
    }
}

void
find_grub4dos_disks (
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
  disk__type Disk;
  bus__type_ptr bus_ptr;

  /*
   * Establish a pointer into the bus device's extension space
   */
  bus_ptr = get_bus_ptr ( ( driver__dev_ext_ptr ) bus__fdo->DeviceExtension );

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
					     InterruptVector->Segment ) << 4 )
					 + 0x20 );
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
	  Disk.Initialize = no_init;
	  /*
	   * Possible precision loss
	   */
	  if ( Grub4DosDriveMapSlotPtr[i].SourceODD )
	    {
	      Disk.DiskType = OpticalDisc;
	      Disk.SectorSize = 2048;
	    }
	  else
	    {
	      Disk.DiskType =
		Grub4DosDriveMapSlotPtr[i].
		SourceDrive & 0x80 ? HardDisk : FloppyDisk;
	      Disk.SectorSize = 512;
	    }
	  DBG ( "RAM Drive is type: %d\n", Disk.DiskType );
	  Disk.RAMDisk.DiskBuf =
	    ( winvblock__uint32 ) ( Grub4DosDriveMapSlotPtr[i].SectorStart *
				    512 );
	  Disk.LBADiskSize = Disk.RAMDisk.DiskSize =
	    ( winvblock__uint32 ) Grub4DosDriveMapSlotPtr[i].SectorCount;
	  Disk.Heads = Grub4DosDriveMapSlotPtr[i].MaxHead + 1;
	  Disk.Sectors = Grub4DosDriveMapSlotPtr[i].DestMaxSector;
	  Disk.Cylinders = Disk.LBADiskSize / ( Disk.Heads * Disk.Sectors );
	  Disk.IsRamdisk = TRUE;
	  FoundGrub4DosMapping = TRUE;
	  if ( !Bus_AddChild ( bus__fdo, Disk, TRUE ) )
	    {
	      DBG ( "Bus_AddChild() failed for GRUB4DOS\n" );
	    }
	  else if ( bus_ptr->PhysicalDeviceObject != NULL )
	    {
	      IoInvalidateDeviceRelations ( bus_ptr->PhysicalDeviceObject,
					    BusRelations );
	    }
	}
      InterruptVector = &SafeMbrHookPtr->PrevHook;
    }

  MmUnmapIoSpace ( PhysicalMemory, 0x100000 );
  if ( !FoundGrub4DosMapping )
    {
      DBG ( "No GRUB4DOS drive mappings found\n" );
    }
}

extern void
probe__disks (
  void
 )
{
  find_aoe_disks (  );
  find_memdisks (  );
  find_grub4dos_disks (  );
}
