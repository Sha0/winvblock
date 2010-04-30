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
 * MEMDISK specifics
 *
 */

#include <stdio.h>
#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "disk.h"
#include "bus.h"
#include "ramdisk.h"
#include "debug.h"
#include "mdi.h"
#include "probe.h"

winvblock__bool STDCALL
check_mbft (
  winvblock__uint8_ptr PhysicalMemory,
  winvblock__uint32 Offset
 )
{
  mdi__mbft_ptr mBFT = ( mdi__mbft_ptr ) ( PhysicalMemory + Offset );
  winvblock__uint32 i;
  winvblock__uint8 Checksum = 0;
  safe_mbr_hook_ptr AssociatedHook;
  ramdisk__type ramdisk = { 0 };
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
  DBG ( "MEMDISK DiskBuf: 0x%08x\n", mBFT->mdi.diskbuf );
  DBG ( "MEMDISK DiskSize: %d sectors\n", mBFT->mdi.disksize );
  ramdisk.DiskBuf = mBFT->mdi.diskbuf;
  ramdisk.disk.LBADiskSize = ramdisk.DiskSize = mBFT->mdi.disksize;
  if ( mBFT->mdi.driveno == 0xE0 )
    {
      ramdisk.disk.media = disk__media_optical;
      ramdisk.disk.SectorSize = 2048;
    }
  else
    {
      if ( mBFT->mdi.driveno & 0x80 )
	ramdisk.disk.media = disk__media_hard;
      else
	ramdisk.disk.media = disk__media_floppy;
      ramdisk.disk.SectorSize = 512;
    }
  DBG ( "RAM Drive is type: %d\n", ramdisk.disk.media );
  ramdisk.disk.Cylinders = mBFT->mdi.cylinders;
  ramdisk.disk.Heads = mBFT->mdi.heads;
  ramdisk.disk.Sectors = mBFT->mdi.sectors;
  ramdisk.disk.BootDrive = TRUE;
  ramdisk.disk.ops = &ramdisk__default_ops;
  ramdisk.disk.dev_ext.ops = &disk__dev_ops;
  ramdisk.disk.dev_ext.size = sizeof ( ramdisk__type );
  if ( !bus__add_child ( bus__fdo, &ramdisk.disk.dev_ext ) )
    {
      DBG ( "bus__add_child() failed for MEMDISK\n" );
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
memdisk__find (
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
