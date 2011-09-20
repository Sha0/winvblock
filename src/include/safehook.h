/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef WV_M_PROBE_H_
#  define WV_M_PROBE_H_

/****
 * @file
 *
 * Boot-time disk probing specifics.
 */

/*** Types. */
typedef struct WV_PROBE_SAFE_MBR_HOOK
  WV_S_PROBE_SAFE_MBR_HOOK, * WV_SP_PROBE_SAFE_MBR_HOOK;

/*** Function declarations. */

/**
 * Create a "safe INT 0x13 hook" bus PDO
 *
 * @v HookAddr          The SEG16:OFF16 address for the hook
 * @v SafeHook          Points to the safe hook
 * @v ParentBus         The parent bus for the newly-created safe hook bus
 * @ret PDEVICE_OBJECT  The newly-create safe hook bus, or NULL upon failure
 */
extern PDEVICE_OBJECT STDCALL WvSafeHookPdoCreate(
    IN SP_X86_SEG16OFF16 HookAddr,
    IN WV_SP_PROBE_SAFE_MBR_HOOK SafeHook,
    IN PDEVICE_OBJECT ParentBus
  );

/**
 * Probe a SEG16:OFF16 for a "safe INT 0x13 hook" and create a bus PDO
 *
 * @v HookAddr          The SEG16:OFF16 address to probe
 * @v ParentBus         If a safe hook is found, it will be a child bus to
 *                      this parent bus
 * @ret PDEVICE_OBJECT  A child bus, if found, else NULL
 */
extern PDEVICE_OBJECT STDCALL WvSafeHookProbe(
    IN SP_X86_SEG16OFF16 HookAddr,
    IN PDEVICE_OBJECT ParentBus
  );

/*** Struct/union definitions */
#ifdef _MSC_VER
#  pragma pack(1)
#endif
struct WV_PROBE_SAFE_MBR_HOOK {
    UCHAR Jump[3];
    UCHAR Signature[8];
    UCHAR VendorId[8];
    S_X86_SEG16OFF16 PrevHook;
    UINT32 Flags;
    UINT32 Mbft;     /* MEMDISK only. */
  } __attribute__((__packed__));
#ifdef _MSC_VER
#  pragma pack()
#endif

#endif  /* WV_M_PROBE_H_ */
