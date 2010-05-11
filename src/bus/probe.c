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

/**
 * @file
 *
 * Boot-time disk probing specifics
 *
 */

#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "disk.h"
#include "mount.h"
#include "bus.h"
#include "debug.h"
#include "bus.h"
#include "aoe.h"
#include "ramdisk.h"
#include "memdisk.h"
#include "grub4dos.h"
#include "filedisk.h"
#include "probe.h"

safe_mbr_hook_ptr STDCALL
get_safe_hook (
  IN winvblock__uint8_ptr PhysicalMemory,
  IN int_vector_ptr InterruptVector
 )
{
  winvblock__uint32 Int13Hook;
  safe_mbr_hook_ptr SafeMbrHookPtr;
  winvblock__uint8 Signature[9] = { 0 };
  winvblock__uint8 VendorID[9] = { 0 };

  Int13Hook =
    ( ( ( winvblock__uint32 ) InterruptVector->Segment ) << 4 ) +
    ( ( winvblock__uint32 ) InterruptVector->Offset );
  SafeMbrHookPtr = ( safe_mbr_hook_ptr ) ( PhysicalMemory + Int13Hook );
  RtlCopyMemory ( Signature, SafeMbrHookPtr->Signature, 8 );
  RtlCopyMemory ( VendorID, SafeMbrHookPtr->VendorID, 8 );
  DBG ( "INT 0x13 Segment: 0x%04x\n", InterruptVector->Segment );
  DBG ( "INT 0x13 Offset: 0x%04x\n", InterruptVector->Offset );
  DBG ( "INT 0x13 Hook: 0x%08x\n", Int13Hook );
  DBG ( "INT 0x13 Safe Hook Signature: %s\n", Signature );
  if ( !( RtlCompareMemory ( Signature, "$INT13SF", 8 ) == 8 ) )
    {
      DBG ( "Invalid INT 0x13 Safe Hook Signature; End of chain\n" );
      return NULL;
    }
  return SafeMbrHookPtr;
}

extern void
probe__disks (
  void
 )
{
#ifndef SPLIT_AOE
  aoe__process_abft (  );
#endif
  memdisk__find (  );
  grub4dos__find (  );
}
