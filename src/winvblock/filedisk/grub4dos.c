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
 * GRUB4DOS file-backed disk specifics
 *
 */

#include <ntddk.h>
#include <initguid.h>
#include <ntddstor.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "device.h"
#include "disk.h"
#include "bus.h"
#include "filedisk.h"
#include "debug.h"
#include "probe.h"
#include "grub4dos.h"
#include "byte.h"
#include "msvhd.h"

static disk__io_routine sync_io;

/**
 * Check if a disk might be the matching backing disk for
 * a GRUB4DOS sector-mapped disk
 *
 * @v file              HANDLE to an open disk
 * @v filedisk_ptr      Points to the filedisk to match against
 */
static NTSTATUS STDCALL
check_disk_match (
  IN HANDLE file,
  IN filedisk__type_ptr filedisk_ptr
 )
{
  msvhd__footer_ptr buf;
  NTSTATUS status;
  IO_STATUS_BLOCK io_status;
  LARGE_INTEGER end_part;

  /*
   * Allocate a buffer for testing for a MS .VHD footer
   */
  buf = ExAllocatePool ( NonPagedPool, sizeof ( *buf ) );
  if ( buf == NULL )
    {
      status = STATUS_INSUFFICIENT_RESOURCES;
      goto err_alloc;
    }
  /*
   * Read in the buffer.  Note that we adjust for the .VHD footer (plus
   * prefixed padding for an .ISO) that we truncated from the reported disk
   * size earlier when the disk mapping was found
   */
  end_part.QuadPart =
    filedisk_ptr->offset.QuadPart +
    ( filedisk_ptr->disk->LBADiskSize * filedisk_ptr->disk->SectorSize ) +
    filedisk_ptr->disk->SectorSize - sizeof ( *buf );
  status =
    ZwReadFile ( file, NULL, NULL, NULL, &io_status, buf, sizeof ( *buf ),
		 &end_part, NULL );
  if ( !NT_SUCCESS ( status ) )
    goto err_read;
  /*
   * Adjust the footer's byte ordering
   */
  msvhd__footer_swap_endian ( buf );
  /*
   * Examine .VHD fields for validity
   */
  if ( RtlCompareMemory ( &buf->cookie, "conectix", sizeof ( buf->cookie ) ) !=
       sizeof ( buf->cookie ) )
    status = STATUS_UNSUCCESSFUL;
  if ( buf->file_ver.val != 0x10000 )
    status = STATUS_UNSUCCESSFUL;
  if ( buf->data_offset.val != 0xffffffff )
    status = STATUS_UNSUCCESSFUL;
  if ( buf->orig_size.val != buf->cur_size.val )
    status = STATUS_UNSUCCESSFUL;
  if ( buf->type.val != 2 )
    status = STATUS_UNSUCCESSFUL;
  /*
   * Match against our expected disk size
   */
  if ( ( filedisk_ptr->disk->LBADiskSize * filedisk_ptr->disk->SectorSize ) !=
       buf->cur_size.val )
    status = STATUS_UNSUCCESSFUL;

err_read:

  ExFreePool ( buf );
err_alloc:

  return status;
}

/**
 * Temporarily used by established disks in order to access the
 * backing disk late(r) during the boot process
 */
static
disk__io_decl (
  io
 )
{
  filedisk__type_ptr filedisk_ptr;
  NTSTATUS status;
  GUID disk_guid = GUID_DEVINTERFACE_DISK;
  PWSTR sym_links;
  PWCHAR pos;
  HANDLE file;

  filedisk_ptr = filedisk__get_ptr ( dev_ptr );
  /*
   * Find the backing disk and use it.  We walk a list
   * of unicode disk device names and check each one
   */
  status = IoGetDeviceInterfaces ( &disk_guid, NULL, 0, &sym_links );
  if ( !NT_SUCCESS ( status ) )
    goto dud;
  pos = sym_links;
  while ( *pos != UNICODE_NULL )
    {
      UNICODE_STRING path;
      OBJECT_ATTRIBUTES obj_attrs;
      IO_STATUS_BLOCK io_status;
      RtlInitUnicodeString ( &path, pos );
      InitializeObjectAttributes ( &obj_attrs, &path,
				   OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
				   NULL, NULL );
      /*
       * Try this disk
       */
      file = 0;
      status =
	ZwCreateFile ( &file, GENERIC_READ | GENERIC_WRITE, &obj_attrs,
		       &io_status, NULL, FILE_ATTRIBUTE_NORMAL,
		       FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE,
		       FILE_OPEN,
		       FILE_NON_DIRECTORY_FILE | FILE_RANDOM_ACCESS |
		       FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0 );
      /*
       * If we could open it, check it out
       */
      if ( NT_SUCCESS ( status ) )
	status = check_disk_match ( file, filedisk_ptr );
      /*
       * If we liked this disk as our backing disk, stop looking
       */
      if ( NT_SUCCESS ( status ) )
	break;
      /*
       * We could not open this disk or didn't like it.  Try the next one
       */
      if ( file )
	ZwClose ( file );
      while ( *pos != UNICODE_NULL )
	pos++;
      pos++;
    }
  ExFreePool ( sym_links );
  /*
   * If we did not find the backing disk, we are a dud
   */
  if ( !NT_SUCCESS ( status ) )
    goto dud;
  /*
   *  Use the backing disk and restore the original read/write routine
   */
  filedisk_ptr->file = file;
  filedisk_ptr->sync_io = sync_io;
  /*
   * Call the original read/write routine
   */
  return sync_io ( dev_ptr, mode, start_sector, sector_count, buffer, irp );

dud:
  irp->IoStatus.Information = 0;
  irp->IoStatus.Status = STATUS_NO_MEDIA_IN_DEVICE;
  IoCompleteRequest ( irp, IO_NO_INCREMENT );
  return STATUS_NO_MEDIA_IN_DEVICE;
}

void
filedisk_grub4dos__find (
  void
 )
{
  PHYSICAL_ADDRESS PhysicalAddress;
  winvblock__uint8_ptr PhysicalMemory;
  int_vector_ptr InterruptVector;
  winvblock__uint32 Int13Hook;
  safe_mbr_hook_ptr SafeMbrHookPtr;
  grub4dos__drive_mapping_ptr Grub4DosDriveMapSlotPtr;
  winvblock__uint32 i = 8;
  winvblock__bool FoundGrub4DosMapping = FALSE;
  filedisk__type_ptr filedisk_ptr;
  /*
   * Find a GRUB4DOS sector-mapped disk.  Start by looking at the
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
	( grub4dos__drive_mapping_ptr ) ( PhysicalMemory +
					  ( ( ( winvblock__uint32 )
					      InterruptVector->
					      Segment ) << 4 ) + 0x20 );
      while ( i-- )
	{
	  if ( ( Grub4DosDriveMapSlotPtr[i].SectorCount == 0 )
	       || ( Grub4DosDriveMapSlotPtr[i].DestDrive == 0xff ) )
	    {
	      DBG ( "Skipping non-sector-mapped GRUB4DOS disk\n" );
	      continue;
	    }
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
	  /*
	   * Create the threaded, file-backed disk.  Hook the
	   * read/write routine so we can accessing the backing
	   * late(r) during the boot process
	   */
	  filedisk_ptr = filedisk__create_threaded (  );
	  if ( filedisk_ptr == NULL )
	    {
	      DBG ( "Could not create GRUB4DOS disk!\n" );
	      return;
	    }
	  sync_io = filedisk_ptr->sync_io;
	  filedisk_ptr->sync_io = io;
	  /*
	   * Possible precision loss
	   */
	  if ( Grub4DosDriveMapSlotPtr[i].SourceODD )
	    {
	      filedisk_ptr->disk->media = disk__media_optical;
	      filedisk_ptr->disk->SectorSize = 2048;
	    }
	  else
	    {
	      filedisk_ptr->disk->media =
		Grub4DosDriveMapSlotPtr[i].SourceDrive & 0x80 ?
		disk__media_hard : disk__media_floppy;
	      filedisk_ptr->disk->SectorSize = 512;
	    }
	  DBG ( "Sector-mapped disk is type: %d\n",
		filedisk_ptr->disk->media );
	  /*
	   * Note the offset of the disk image from the backing disk's start
	   */
	  filedisk_ptr->offset.QuadPart =
	    Grub4DosDriveMapSlotPtr[i].SectorStart *
	    ( Grub4DosDriveMapSlotPtr[i].DestODD ? 2048 : 512 );
	  /*
	   * Size and geometry.  Please note that since we require a .VHD
	   * footer, we exclude this from the LBA disk size by truncating
	   * a 512-byte sector for HDD images, or a 2048-byte sector for .ISO
	   */
	  {
	    ULONGLONG total_size =
	      Grub4DosDriveMapSlotPtr[i].SectorCount *
	      ( Grub4DosDriveMapSlotPtr[i].DestODD ? 2048 : 512 );
	    filedisk_ptr->disk->LBADiskSize =
	      ( total_size -
		filedisk_ptr->disk->SectorSize ) /
	      filedisk_ptr->disk->SectorSize;
	  }
	  filedisk_ptr->disk->Heads = Grub4DosDriveMapSlotPtr[i].MaxHead + 1;
	  filedisk_ptr->disk->Sectors =
	    Grub4DosDriveMapSlotPtr[i].DestMaxSector;
	  filedisk_ptr->disk->Cylinders =
	    filedisk_ptr->disk->LBADiskSize / ( filedisk_ptr->disk->Heads *
						filedisk_ptr->disk->Sectors );
	  /*
	   * Set a filedisk "hash" and mark the drive as a boot-drive.
	   * The "hash" is 'G4DX', where X is the GRUB4DOS INT 13h
	   * drive number.  Note that mutiple G4D INT 13h chains will
	   * cause a "hash" collision!  Too bad for now.
	   */
	  filedisk_ptr->hash = 'G4DX';
	  ( ( winvblock__uint8 * ) & filedisk_ptr->hash )[0] =
	    Grub4DosDriveMapSlotPtr[i].SourceDrive;
	  filedisk_ptr->disk->BootDrive = TRUE;
	  FoundGrub4DosMapping = TRUE;
	  bus__add_child ( bus__boot (  ), filedisk_ptr->disk->device );
	}
      InterruptVector = &SafeMbrHookPtr->PrevHook;
    }

  MmUnmapIoSpace ( PhysicalMemory, 0x100000 );
  if ( !FoundGrub4DosMapping )
    {
      DBG ( "No GRUB4DOS sector-mapped disk mappings found\n" );
    }
}
