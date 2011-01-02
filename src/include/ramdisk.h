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
#ifndef WV_M_RAMDISK_H_
#  define WV_M_RAMDISK_H_

/**
 * @file
 *
 * RAM disk specifics.
 */

typedef struct WV_RAMDISK_T {
    WV_SP_DISK_T disk;
    UINT32 DiskBuf;
    UINT32 DiskSize;
    WV_FP_DEV_FREE prev_free;
    LIST_ENTRY tracking;
  } WV_S_RAMDISK_T, * WV_SP_RAMDISK_T;

extern NTSTATUS ramdisk__module_init(void);

/**
 * Create a new RAM disk
 *
 * @ret ramdisk_ptr     The address of a new RAM disk, or NULL for failure
 *
 * This function should not be confused with a PDO creation routine, which is
 * actually implemented for each device type.  This routine will allocate a
 * WV_S_RAMDISK_T, track it in a global list, as well as populate the disk
 * with default values.
 */
extern WV_SP_RAMDISK_T ramdisk__create (
  void
 );

#endif  /* WV_M_RAMDISK_H_ */
