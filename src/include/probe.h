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
#ifndef _WV_M_PROBE_H_
#  define _WV_M_PROBE_H_

/**
 * @file
 *
 * Boot-time disk probing specifics.
 */

typedef struct WV_PROBE_INT_VECTOR {
    winvblock__uint16 Offset;
    winvblock__uint16 Segment;
  } WV_S_PROBE_INT_VECTOR, * WV_SP_PROBE_INT_VECTOR;

#ifdef _MSC_VER
#  pragma pack(1)
#endif
struct WV_PROBE_SAFE_MBR_HOOK {
    winvblock__uint8 Jump[3];
    winvblock__uint8 Signature[8];
    winvblock__uint8 VendorId[8];
    WV_S_PROBE_INT_VECTOR PrevHook;
    winvblock__uint32 Flags;
    winvblock__uint32 Mbft;     /* MEMDISK only. */
  } __attribute__((__packed__));
#ifdef _MSC_VER
#  pragma pack()
#endif
typedef struct WV_PROBE_SAFE_MBR_HOOK
  WV_S_PROBE_SAFE_MBR_HOOK, * WV_SP_PROBE_SAFE_MBR_HOOK;

/* Probe functions. */
extern WV_SP_PROBE_SAFE_MBR_HOOK STDCALL WvProbeGetSafeHook(
    IN winvblock__uint8_ptr,
    IN WV_SP_PROBE_INT_VECTOR
  );
extern void WvProbeDisks(void);

#endif  /* _WV_M_PROBE_H_ */
