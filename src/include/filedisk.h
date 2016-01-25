/**
 * Copyright (C) 2016, Synthetel Corporation.
 *   Author: Shao Miller <winvblock@synthetel.com>
 * Copyright (C) 2009-2012, Shao Miller <sha0.miller@gmail.com>.
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
 * File-backed disk specifics
 */

/** Constants */

/** Device flags for the Flags member of an S_WVL_FILEDISK */
enum E_WVL_FILEDISK_FLAG {
    CvWvlFilediskFlagStarted_,
    CvWvlFilediskFlagStopping_,
    CvWvlFilediskFlagRemoving_,
    CvWvlFilediskFlagSurpriseRemoved_,
    CvWvlFilediskFlagStarted = 1 << CvWvlFilediskFlagStarted_,
    CvWvlFilediskFlagStopping = 1 << CvWvlFilediskFlagStopping_,
    CvWvlFilediskFlagRemoving = 1 << CvWvlFilediskFlagRemoving_,
    CvWvlFilediskFlagSurpriseRemoved = 1 << CvWvlFilediskFlagSurpriseRemoved_,
    CvWvlFilediskFlagZero = 0
  };

enum E_WVL_FILEDISK_MEDIA_TYPE {
    CvWvlFilediskMediaTypeFloppy,
    CvWvlFilediskMediaTypeHard,
    CvWvlFilediskMediaTypeOptical,
    CvWvlFilediskMediaTypes
  };

/** Object types */
typedef struct S_WVL_FILEDISK S_WVL_FILEDISK;
typedef enum E_WVL_FILEDISK_MEDIA_TYPE E_WVL_FILEDISK_MEDIA_TYPE;

typedef struct WV_FILEDISK_T {
    WV_S_DEV_EXT DevExt;
    WV_S_DEV_T Dev[1];
    WVL_S_DISK_T disk[1];
    HANDLE file;
    UINT32 hash;
    LARGE_INTEGER offset;
    WVL_S_THREAD Thread[1];
    LIST_ENTRY Irps[1];
    KSPIN_LOCK IrpsLock[1];
    PVOID impersonation;
  } WV_S_FILEDISK_T, * WV_SP_FILEDISK_T;

extern NTSTATUS STDCALL WvFilediskAttach(IN PIRP);
extern WV_SP_FILEDISK_T STDCALL WvFilediskCreatePdo(IN WVL_E_DISK_MEDIA_TYPE);
extern VOID STDCALL WvFilediskHotSwap(IN WV_SP_FILEDISK_T, IN PCHAR);

/** Struct/union type definitions */
struct S_WVL_FILEDISK {
    /** This must be the first member of all extension types */
    WV_S_DEV_EXT DeviceExtension[1];
    /* Security context for file I/O, if impersonating */
    SECURITY_CLIENT_CONTEXT * SecurityContext;
    /* Offset (in bytes) into the file */
    LARGE_INTEGER FileOffset;
    HANDLE FileHandle;
    /**
     * Filedisk device flags.  Must be accessed atomically (such as with
     * InterlockedXxx functions)
     */
    volatile LONG Flags;
    /** The media type */
    E_WVL_FILEDISK_MEDIA_TYPE MediaType;
    /* The size of the disk, in logical blocks (sectors) */
    ULONGLONG LbaDiskSize;
    /* The size of a logical block (sector) */
    UINT32 SectorSize;
    /* Do we impersonate the user who opened the file? */
    BOOLEAN Impersonate;
  };

#endif  /* WV_M_FILEDISK_H_ */
