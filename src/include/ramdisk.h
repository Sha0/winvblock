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
#ifndef _ramdisk_h
#  define _ramdisk_h

/**
 * @file
 *
 * RAM disk specifics.
 */

winvblock__def_struct ( ramdisk__type )
{
  disk__type_ptr disk;
  winvblock__uint32 DiskBuf;
  winvblock__uint32 DiskSize;
  device__free_func * prev_free;
  LIST_ENTRY tracking;
};

/**
 * Initialize the global, RAM disk-common environment
 *
 * @ret ntstatus        STATUS_SUCCESS or the NTSTATUS for a failure
 */
extern NTSTATUS ramdisk__init (
  void
 );

/**
 * Create a new RAM disk
 *
 * @ret ramdisk_ptr     The address of a new RAM disk, or NULL for failure
 *
 * This function should not be confused with a PDO creation routine, which is
 * actually implemented for each device type.  This routine will allocate a
 * ramdisk__type, track it in a global list, as well as populate the disk
 * with default values.
 */
extern ramdisk__type_ptr ramdisk__create (
  void
 );

#endif				/* _ramdisk_h */
