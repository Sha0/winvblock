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

static
disk__io_decl (
  io
 )
{
  disk__type_ptr disk_ptr;
  filedisk__type_ptr filedisk_ptr;
  LARGE_INTEGER offset;
  NTSTATUS status;
  IO_STATUS_BLOCK io_status;

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
  filedisk__type_ptr filedisk_ptr = filedisk__get_ptr ( &disk_ptr->dev_ext );
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

static disk__ops default_ops;

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
  filedisk__type filedisk = { 0 };

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

  filedisk.file = file;

  switch ( params->type )
    {
      case 'f':
	filedisk.disk.media = disk__media_floppy;
	filedisk.disk.SectorSize = 512;
	break;
      case 'c':
	filedisk.disk.media = disk__media_optical;
	filedisk.disk.SectorSize = 2048;
	break;
      default:
	filedisk.disk.media = disk__media_hard;
	filedisk.disk.SectorSize = 512;
	break;
    }
  DBG ( "File-backed disk is type: %d\n", filedisk.disk.media );
  /*
   * Determine the disk's size
   */
  status =
    ZwQueryInformationFile ( file, &io_status, &info, sizeof ( info ),
			     FileStandardInformation );
  if ( !NT_SUCCESS ( status ) )
    {
      ZwClose ( file );
      return status;
    }
  filedisk.disk.LBADiskSize =
    info.EndOfFile.QuadPart / filedisk.disk.SectorSize;
  filedisk.disk.Cylinders = params->cylinders;
  filedisk.disk.Heads = params->heads;
  filedisk.disk.Sectors = params->sectors;
  /*
   * A really stupid "hash".  RtlHashUnicodeString() would have been
   * good, but is only available >= Windows XP
   */
  filedisk.hash = ( winvblock__uint32 ) filedisk.disk.LBADiskSize;
  {
    char *path_iterator = file_path1.Buffer;

    while ( *path_iterator )
      filedisk.hash += *path_iterator++;
  }
  filedisk.disk.ops = &default_ops;
  filedisk.disk.dev_ext.ops = &disk__dev_ops;
  filedisk.disk.dev_ext.size = sizeof ( filedisk__type );
  bus__add_child ( &filedisk.disk.dev_ext );
  return STATUS_SUCCESS;
}

static
disk__close_decl (
  close
 )
{
  filedisk__type_ptr filedisk_ptr = filedisk__get_ptr ( &disk_ptr->dev_ext );
  ZwClose ( filedisk_ptr->file );
  return;
}

static disk__ops default_ops = {
  io,
  disk__default_max_xfer_len,
  disk__default_init,
  query_id,
  close
};
