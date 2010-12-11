/**
 * Copyright (C) 2009-2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef _disk_h
#  define _disk_h

/**
 * @file
 *
 * Disk device specifics.
 */

enum _disk__media
{
  disk__media_floppy,
  disk__media_hard,
  disk__media_optical,
  disk__media_count
};
winvblock__def_enum ( disk__media );

typedef char disk__boot_sect_ptr[512];

extern winvblock__bool disk__removable[disk__media_count];
extern PWCHAR disk__compat_ids[disk__media_count];

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
 * @v dev_ptr           Points to the disk's device structure
 * @v mode              Read / write mode
 * @v start_sector      First sector for request
 * @v sector_count      Number of sectors to work with
 * @v buffer            Buffer to read / write sectors to / from
 * @v irp               Interrupt request packet for this request
 */
#  define disk__io_decl( x ) \
\
NTSTATUS STDCALL \
x ( \
  IN struct device__type * dev_ptr, \
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

/**
 * Disk initialization routine
 *
 * @v disk_ptr        The disk device being initialized
 */
#  define disk__init_decl( x ) \
\
winvblock__bool STDCALL \
x ( \
  IN disk__type_ptr disk_ptr \
 )
/*
 * Function pointer for a disk initialization routine.
 * 'indent' mangles this, so it looks weird
 */
typedef disk__init_decl (
   ( *disk__init_routine )
 );

/**
 * Disk close routine
 *
 * @v disk_ptr        The disk device being closed
 */
#  define disk__close_decl( x ) \
\
void STDCALL \
x ( \
  IN disk__type_ptr disk_ptr \
 )
/*
 * Function pointer for a disk close routine.
 * 'indent' mangles this, so it looks weird
 */
typedef disk__close_decl (
   ( *disk__close_routine )
 );

winvblock__def_struct ( disk__ops )
{
  disk__io_routine io;
  disk__max_xfer_len_routine max_xfer_len;
  disk__init_routine init;
  disk__close_routine close;
};

struct _disk__type
{
  struct device__type * device;
  KEVENT SearchEvent;
  KSPIN_LOCK SpinLock;
  winvblock__bool BootDrive;
  winvblock__bool Unmount;
  winvblock__uint32 DiskNumber;
  disk__media media;
  disk__ops disk_ops;
  ULONGLONG LBADiskSize;
  ULONGLONG Cylinders;
  winvblock__uint32 Heads;
  winvblock__uint32 Sectors;
  winvblock__uint32 SectorSize;
  winvblock__uint32 SpecialFileCount;
  device__free_func * prev_free;
  LIST_ENTRY tracking;
  winvblock__any_ptr ext;
};

/*
 * Yield a pointer to the disk
 */
#  define disk__get_ptr( dev_ptr ) ( ( disk__type_ptr ) dev_ptr->ext )

/* An MBR C/H/S address and ways to access its components */
typedef winvblock__uint8 chs[3];

#  define     chs_head( chs ) chs[0]
#  define   chs_sector( chs ) ( chs[1] & 0x3F )
#  define chs_cyl_high( chs ) ( ( ( winvblock__uint16 ) ( chs[1] & 0xC0 ) ) << 2 )
#  define  chs_cyl_low( chs ) ( ( winvblock__uint16 ) chs[2] )
#  define chs_cylinder( chs ) ( chs_cyl_high ( chs ) | chs_cyl_low ( chs ) )

#  ifdef _MSC_VER
#    pragma pack(1)
#  endif

/* An MBR */
winvblock__def_struct ( mbr )
{
  winvblock__uint8 code[440];
  winvblock__uint32 disk_sig;
  winvblock__uint16 pad;
  struct
  {
    winvblock__uint8 status;
    chs chs_start;
    winvblock__uint8 type;
    chs chs_end;
    winvblock__uint32 lba_start;
    winvblock__uint32 lba_count;
  } partition[4] __attribute__ ( ( packed ) );
  winvblock__uint16 mbr_sig;
} __attribute__ ( ( __packed__ ) );

#  ifdef _MSC_VER
#    pragma pack()
#  endif

extern disk__io_decl (
  disk__io
 );
extern disk__max_xfer_len_decl (
  disk__max_xfer_len
 );

/**
 * Attempt to guess a disk's geometry
 *
 * @v boot_sect_ptr     The MBR or VBR with possible geometry clues
 * @v disk_ptr          The disk to set the geometry for
 */
extern winvblock__lib_func void disk__guess_geometry (
  IN disk__boot_sect_ptr boot_sect_ptr,
  IN OUT disk__type_ptr disk_ptr
 );

/**
 * Create a new disk
 *
 * @ret disk_ptr        The address of a new disk, or NULL for failure
 *
 * This function should not be confused with a PDO creation routine, which is
 * actually implemented for each device type.  This routine will allocate a
 * disk__type, track it in a global list, as well as populate the disk
 * with default values.
 */
extern winvblock__lib_func disk__type_ptr disk__create (
  void
 );

/**
 * Initialize the global, disk-common environment
 *
 * @ret ntstatus        STATUS_SUCCESS or the NTSTATUS for a failure
 */
extern NTSTATUS disk__init (
  void
 );

#endif				/* _disk_h */
