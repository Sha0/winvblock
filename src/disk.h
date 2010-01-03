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
#ifndef _DISK_H
#  define _DISK_H

/**
 * @file
 *
 * Disk specifics
 *
 */

enum _disk__media
{
  disk__media_floppy,
  disk__media_hard,
  disk__media_optical
};
winvblock__def_enum ( disk__media );

enum _disk__io_mode
{
  disk__io_mode_read,
  disk__io_mode_write
};
winvblock__def_enum ( disk__io_mode );

/* Forward declaration */
winvblock__def_struct ( disk__type );

/**
 * I/O Request
 *
 * @v dev_ext_ptr     The device extension space
 * @v mode            Read / write mode
 * @v start_sector    First sector for request
 * @v sector_count    Number of sectors to work with
 * @v buffer          Buffer to read / write sectors to / from
 * @v irp             Interrupt request packet for this request
 */
#  define disk__io_decl( x ) \
\
NTSTATUS \
x ( \
  IN driver__dev_ext_ptr dev_ext_ptr, \
  IN disk__io_mode mode, \
  IN LONGLONG start_sector, \
  IN winvblock__uint32 sector_count, \
  IN winvblock__uint8_ptr buffer, \
  IN PIRP irp \
 )
/*
 * Function pointer for a disk I/O routine.
 * 'indent' mangles this, so it looks weird
 */
typedef disk__io_decl (
   ( *disk__io_routine )
 );

/**
 * Maximum transfer length response routine
 *
 * @v disk_ptr        The disk being queried
 */
#  define disk__max_xfer_len_decl( x ) \
\
winvblock__uint32 \
x ( \
  IN disk__type_ptr disk_ptr \
 )
/*
 * Function pointer for a maximum transfer length response routine.
 * 'indent' mangles this, so it looks weird
 */
typedef disk__max_xfer_len_decl (
   ( *disk__max_xfer_len_routine )
 );

extern disk__max_xfer_len_decl (
  disk__default_max_xfer_len
 );

/**
 * Disk initialization routine
 *
 * @v dev_ext_ptr     The disk device being initialized
 */
#  define disk__init_decl( x ) \
\
winvblock__bool STDCALL \
x ( \
  IN driver__dev_ext_ptr dev_ext_ptr \
 )
/*
 * Function pointer for a disk initialization routine.
 * 'indent' mangles this, so it looks weird
 */
typedef disk__init_decl (
   ( *disk__init_routine )
 );

extern disk__init_decl (
  disk__default_init
 );

/**
 * Disk PnP ID reponse routine
 *
 * @v disk_ptr        The disk device being queried
 * @v query_type      The query type
 * @v buf_512         Wide character 512-element buffer for ID response
 *
 * Returns the number of wide characters in the response.
 */
#  define disk__pnp_id_decl( x ) \
\
winvblock__uint32 STDCALL \
x ( \
  IN disk__type_ptr disk_ptr, \
  IN BUS_QUERY_ID_TYPE query_type, \
  OUT PWCHAR buf_512 \
 )
/*
 * Function pointer for a disk PnP response routine.
 * 'indent' mangles this, so it looks weird
 */
typedef disk__pnp_id_decl (
   ( *disk__pnp_id_routine )
 );

winvblock__def_struct ( disk__ops )
{
  disk__io_routine io;
  disk__max_xfer_len_routine max_xfer_len;
  disk__init_routine init;
  disk__pnp_id_routine pnp_id;
};

struct _disk__type
{
  driver__dev_ext dev_ext;
  PDEVICE_OBJECT Parent;
  disk__type_ptr next_sibling_ptr;
  KEVENT SearchEvent;
  KSPIN_LOCK SpinLock;
  winvblock__bool BootDrive;
  winvblock__bool Unmount;
  winvblock__uint32 DiskNumber;
  disk__media media;
  disk__ops_ptr ops;
  LONGLONG LBADiskSize;
  LONGLONG Cylinders;
  winvblock__uint32 Heads;
  winvblock__uint32 Sectors;
  winvblock__uint32 SectorSize;
  winvblock__uint32 SpecialFileCount;
};

extern irp__handling disk__handling_table[];
extern size_t disk__handling_table_size;

/*
 * Establish a pointer into the child disk device's extension space
 */
__inline disk__type_ptr STDCALL
get_disk_ptr (
  driver__dev_ext_ptr dev_ext_ptr
 )
{
  /*
   * Since the device extension is the first member of a disk
   * structure, a simple cast will suffice
   */
  return ( disk__type_ptr ) dev_ext_ptr;
}

static __inline
disk__io_decl (
  disk__io
 )
{
  disk__type_ptr disk_ptr;

  /*
   * Establish a pointer into the disk device's extension space
   */
  disk_ptr = get_disk_ptr ( dev_ext_ptr );

  return disk_ptr->ops->io ( dev_ext_ptr, mode, start_sector, sector_count,
			     buffer, irp );
}

static __inline
disk__max_xfer_len_decl (
  disk__max_xfer_len
 )
{
  return disk_ptr->ops->max_xfer_len ( disk_ptr );
}

__inline
disk__init_decl (
  disk__init
 )
{
  disk__type_ptr disk_ptr;

  /*
   * Establish a pointer into the disk device's extension space
   */
  disk_ptr = get_disk_ptr ( dev_ext_ptr );

  return disk_ptr->ops->init ( dev_ext_ptr );
}

__inline
disk__pnp_id_decl (
  disk__query_id
 )
{
  return disk_ptr->ops->pnp_id ( disk_ptr, query_type, buf_512 );
}

#endif				/* _DISK_H */
