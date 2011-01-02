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
#ifndef WV_M_FILEDISK_H_
#  define WV_M_FILEDISK_H_

/**
 * @file
 *
 * File-backed disk specifics.
 */

typedef struct WV_FILEDISK_T {
    WV_SP_DISK_T disk;
    HANDLE file;
    winvblock__uint32 hash;
    WV_FP_DEV_FREE prev_free;
    LIST_ENTRY tracking;
    LARGE_INTEGER offset;
    /* For threaded instances. */
    LIST_ENTRY req_list;
    KSPIN_LOCK req_list_lock;
    KEVENT signal;
    WV_FP_DISK_IO sync_io;
    char *filepath;
    UNICODE_STRING filepath_unicode;
  } WV_S_FILEDISK_T, * WV_SP_FILEDISK_T;

extern WV_F_DEV_DISPATCH filedisk__attach;

extern NTSTATUS filedisk__module_init(void);

/**
 * Create a new file-backed disk
 *
 * @ret filedisk_ptr    The address of a new filedisk, or NULL for failure
 *
 * This function should not be confused with a PDO creation routine, which is
 * actually implemented for each device type.  This routine will allocate a
 * WV_S_FILEDISK_T, track it in a global list, as well as populate the disk
 * with default values.
 */
extern WV_SP_FILEDISK_T filedisk__create (
  void
 );
/**
 * Create a new threaded, file-backed disk
 *
 * @ret filedisk_ptr    The address of a new filedisk, or NULL for failure
 *
 * See filedisk__create() above.  This routine uses threaded routines
 * for disk reads/writes, and frees asynchronously, too.
 */
extern WV_SP_FILEDISK_T filedisk__create_threaded (
  void
 );

/*
 * Yield a pointer to the file-backed disk
 */
#  define filedisk__get_ptr( dev_ptr ) \
  ((WV_SP_FILEDISK_T) (disk__get_ptr(dev_ptr))->ext)

extern void STDCALL filedisk__hot_swap_thread (
  IN void *StartContext
 );

#endif  /* WV_M_FILEDISK_H_ */
