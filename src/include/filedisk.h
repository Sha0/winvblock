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
    WV_S_DEV_EXT DevExt;
    WV_S_DISK_T disk[1];
    HANDLE file;
    UINT32 hash;
    WV_FP_DEV_FREE prev_free;
    LIST_ENTRY tracking;
    LARGE_INTEGER offset;
    /* For threaded instances. */
    LIST_ENTRY req_list;
    KSPIN_LOCK req_list_lock;
    KEVENT signal;
    WVL_FP_DISK_IO sync_io;
    PCHAR filepath;
    UNICODE_STRING filepath_unicode;
  } WV_S_FILEDISK_T, * WV_SP_FILEDISK_T;

extern NTSTATUS STDCALL WvFilediskAttach(IN PIRP);
extern WV_SP_FILEDISK_T WvFilediskCreatePdo(IN WVL_E_DISK_MEDIA_TYPE);
extern WV_SP_FILEDISK_T WvFilediskCreatePdoThreaded(IN WVL_E_DISK_MEDIA_TYPE);

/* Yield a pointer to the file-backed disk. */
#define filedisk__get_ptr(dev_ptr) \
  ((WV_SP_FILEDISK_T) (disk__get_ptr(dev_ptr))->ext)

extern VOID STDCALL WvFilediskHotSwapThread(IN PVOID);

#endif  /* WV_M_FILEDISK_H_ */
