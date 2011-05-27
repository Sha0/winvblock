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

/*** Struct definitions. */
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

/*** Function declarations. */
extern BOOLEAN WvProbeSafeHookChain(PUCHAR, SP_X86_SEG16OFF16);
extern VOID WvProbeDisks(void);

#endif  /* WV_M_PROBE_H_ */
