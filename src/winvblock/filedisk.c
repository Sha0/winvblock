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
 * File-backed disk specifics
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
#include "mount.h"
#include "bus.h"
#include "filedisk.h"
#include "debug.h"

winvblock__def_struct ( filedisk_type )
{
  disk__type_ptr disk;
  HANDLE file;
  winvblock__uint32 hash;
  device__free_routine prev_free;
  LIST_ENTRY tracking;
};

/*
 * Yield a pointer to the file-backed disk
 */
#define filedisk_get_ptr( dev_ptr ) \
  ( ( filedisk_type_ptr ) ( disk__get_ptr ( dev_ptr ) )->ext )

static LIST_ENTRY filedisk_list;
static KSPIN_LOCK filedisk_list_lock;
/* Forward declarations */
static filedisk_type_ptr create_filedisk (
  void
 );
static device__free_decl (
  free_filedisk
 );

static
disk__io_decl (
  io
 )
{
  disk__type_ptr disk_ptr;
  filedisk_type_ptr filedisk_ptr;
  LARGE_INTEGER offset;
  NTSTATUS status;
  IO_STATUS_BLOCK io_status;

  /*
   * Establish pointers to the disk and filedisk
   */
  disk_ptr = disk__get_ptr ( dev_ptr );
  filedisk_ptr = filedisk_get_ptr ( dev_ptr );

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

  offset.QuadPart = start_sector * disk_ptr->SectorSize;
  if ( mode == disk__io_mode_write )
    status =
      ZwWriteFile ( filedisk_ptr->file, NULL, NULL, NULL, &io_status, buffer,
		    sector_count * disk_ptr->SectorSize, &offset, NULL );
  else
    status =
      ZwReadFile ( filedisk_ptr->file, NULL, NULL, NULL, &io_status, buffer,
		   sector_count * disk_ptr->SectorSize, &offset, NULL );
  if ( !start_sector )
    disk__guess_geometry ( buffer, disk_ptr );
  irp->IoStatus.Information = sector_count * disk_ptr->SectorSize;
  irp->IoStatus.Status = status;
  IoCompleteRequest ( irp, IO_NO_INCREMENT );
  return status;
}

static
disk__pnp_id_decl (
  query_id
 )
{
  filedisk_type_ptr filedisk_ptr = filedisk_get_ptr ( disk_ptr->device );
  static PWCHAR hw_ids[disk__media_count] =
    { winvblock__literal_w L"\\FileFloppyDisk",
    winvblock__literal_w L"\\FileHardDisk",
    winvblock__literal_w L"\\FileOpticalDisc"
  };

  switch ( query_type )
    {
      case BusQueryDeviceID:
	return swprintf ( buf_512, hw_ids[disk_ptr->media] ) + 1;
      case BusQueryInstanceID:
	return swprintf ( buf_512, L"Hash_%08X", filedisk_ptr->hash ) + 1;
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

irp__handler_decl ( filedisk__attach )
{
  ANSI_STRING file_path1;
  winvblock__uint8_ptr buf = Irp->AssociatedIrp.SystemBuffer;
  mount__filedisk_ptr params = ( mount__filedisk_ptr ) buf;
  UNICODE_STRING file_path2;
  OBJECT_ATTRIBUTES obj_attrs;
  NTSTATUS status;
  HANDLE file = NULL;
  IO_STATUS_BLOCK io_status;
  FILE_STANDARD_INFORMATION info;
  filedisk_type_ptr filedisk_ptr;

  filedisk_ptr = create_filedisk (  );
  if ( filedisk_ptr == NULL )
    {
      DBG ( "Could not create file-backed disk!\n" );
      return STATUS_INSUFFICIENT_RESOURCES;
    }

  RtlInitAnsiString ( &file_path1,
		      ( char * )&buf[sizeof ( mount__filedisk )] );
  status = RtlAnsiStringToUnicodeString ( &file_path2, &file_path1, TRUE );
  if ( !NT_SUCCESS ( status ) )
    return status;
  InitializeObjectAttributes ( &obj_attrs, &file_path2,
			       OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL,
			       NULL );
  /*
   * Open the file.  The handle is closed by close()
   */
  status =
    ZwCreateFile ( &file, GENERIC_READ | GENERIC_WRITE, &obj_attrs, &io_status,
		   NULL, FILE_ATTRIBUTE_NORMAL,
		   FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE,
		   FILE_OPEN,
		   FILE_NON_DIRECTORY_FILE | FILE_RANDOM_ACCESS |
		   FILE_NO_INTERMEDIATE_BUFFERING |
		   FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0 );
  RtlFreeUnicodeString ( &file_path2 );
  if ( !NT_SUCCESS ( status ) )
    return status;

  filedisk_ptr->file = file;

  switch ( params->type )
    {
      case 'f':
	filedisk_ptr->disk->media = disk__media_floppy;
	filedisk_ptr->disk->SectorSize = 512;
	break;
      case 'c':
	filedisk_ptr->disk->media = disk__media_optical;
	filedisk_ptr->disk->SectorSize = 2048;
	break;
      default:
	filedisk_ptr->disk->media = disk__media_hard;
	filedisk_ptr->disk->SectorSize = 512;
	break;
    }
  DBG ( "File-backed disk is type: %d\n", filedisk_ptr->disk->media );
  /*
   * Determine the disk's size
   */
  status =
    ZwQueryInformationFile ( file, &io_status, &info, sizeof ( info ),
			     FileStandardInformation );
  if ( !NT_SUCCESS ( status ) )
    {
      ZwClose ( file );
      free_filedisk ( filedisk_ptr->disk->device );
      return status;
    }
  filedisk_ptr->disk->LBADiskSize =
    info.EndOfFile.QuadPart / filedisk_ptr->disk->SectorSize;
  filedisk_ptr->disk->Cylinders = params->cylinders;
  filedisk_ptr->disk->Heads = params->heads;
  filedisk_ptr->disk->Sectors = params->sectors;
  /*
   * A really stupid "hash".  RtlHashUnicodeString() would have been
   * good, but is only available >= Windows XP
   */
  filedisk_ptr->hash = ( winvblock__uint32 ) filedisk_ptr->disk->LBADiskSize;
  {
    char *path_iterator = file_path1.Buffer;

    while ( *path_iterator )
      filedisk_ptr->hash += *path_iterator++;
  }
  /*
   * FIXME: Check for error below!
   */
  bus__add_child ( bus__boot (  ), filedisk_ptr->disk->device );
  return STATUS_SUCCESS;
}

static
disk__close_decl (
  close
 )
{
  filedisk_type_ptr filedisk_ptr = filedisk_get_ptr ( disk_ptr->device );
  ZwClose ( filedisk_ptr->file );
  return;
}

/**
 * Create a new file-backed disk
 *
 * @ret filedisk_ptr    The address of a new filedisk, or NULL for failure
 *
 * This function should not be confused with a PDO creation routine, which is
 * actually implemented for each device type.  This routine will allocate a
 * filedisk_type, track it in a global list, as well as populate the disk
 * with default values.
 */
static filedisk_type_ptr
create_filedisk (
  void
 )
{
  disk__type_ptr disk_ptr;
  filedisk_type_ptr filedisk_ptr;

  /*
   * Try to create a disk
   */
  disk_ptr = disk__create (  );
  if ( disk_ptr == NULL )
    goto err_nodisk;
  /*
   * File-backed disk devices might be used for booting and should
   * not be allocated from a paged memory pool
   */
  filedisk_ptr = ExAllocatePool ( NonPagedPool, sizeof ( filedisk_type ) );
  if ( filedisk_ptr == NULL )
    goto err_nofiledisk;
  RtlZeroMemory ( filedisk_ptr, sizeof ( filedisk_type ) );
  /*
   * Track the new file-backed disk in our global list
   */
  ExInterlockedInsertTailList ( &filedisk_list, &filedisk_ptr->tracking,
				&filedisk_list_lock );
  /*
   * Populate non-zero device defaults
   */
  filedisk_ptr->disk = disk_ptr;
  filedisk_ptr->prev_free = disk_ptr->device->ops.free;
  disk_ptr->device->ops.free = free_filedisk;
  disk_ptr->disk_ops.io = io;
  disk_ptr->disk_ops.pnp_id = query_id;
  disk_ptr->disk_ops.close = close;
  disk_ptr->ext = filedisk_ptr;

  return filedisk_ptr;

err_nofiledisk:

  device__free ( disk_ptr->device );
err_nodisk:

  return NULL;
}

/**
 * Initialize the global, file-backed disk-common environment
 *
 * @ret ntstatus        STATUS_SUCCESS or the NTSTATUS for a failure
 */
NTSTATUS
filedisk__init (
  void
 )
{
  /*
   * Initialize the global list of file-backed disks
   */
  InitializeListHead ( &filedisk_list );
  KeInitializeSpinLock ( &filedisk_list_lock );

  return STATUS_SUCCESS;
}

/**
 * Default file-backed disk deletion operation
 *
 * @v dev_ptr           Points to the file-backed disk device to delete
 */
static
device__free_decl (
  free_filedisk
 )
{
  disk__type_ptr disk_ptr = disk__get_ptr ( dev_ptr );
  filedisk_type_ptr filedisk_ptr = filedisk_get_ptr ( dev_ptr );
  /*
   * Free the "inherited class"
   */
  filedisk_ptr->prev_free ( dev_ptr );
  /*
   * Track the file-backed disk deletion in our global list.  Unfortunately,
   * for now we have faith that a file-backed disk won't be deleted twice and
   * result in a race condition.  Something to keep in mind...
   */
  ExInterlockedRemoveHeadList ( filedisk_ptr->tracking.Blink,
				&filedisk_list_lock );

  ExFreePool ( filedisk_ptr );
}
