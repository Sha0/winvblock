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

/**
 * @file
 *
 * Boot-time disk probing specifics.
 */

#include <ntddk.h>

#include "winvblock.h"
#include "wv_string.h"
#include "portable.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "mount.h"
#include "debug.h"
#include "bus.h"
#include "ramdisk.h"
#include "memdisk.h"
#include "grub4dos.h"
#include "filedisk.h"
#include "probe.h"

WV_SP_PROBE_SAFE_MBR_HOOK STDCALL WvProbeGetSafeHook(
    IN PUCHAR PhysicalMemory,
    IN WV_SP_PROBE_INT_VECTOR InterruptVector
  ) {
    UINT32 int13_hook;
    WV_SP_PROBE_SAFE_MBR_HOOK safe_mbr_hook;
    UCHAR sig[9] = {0};
    UCHAR ven_id[9] = {0};

    int13_hook = (((UINT32) InterruptVector->Segment) << 4) +
      ((UINT32) InterruptVector->Offset);
    safe_mbr_hook = (WV_SP_PROBE_SAFE_MBR_HOOK) (PhysicalMemory + int13_hook);
    RtlCopyMemory(sig, safe_mbr_hook->Signature, 8);
    RtlCopyMemory(ven_id, safe_mbr_hook->VendorId, 8);
    DBG("INT 0x13 Segment: 0x%04x\n", InterruptVector->Segment);
    DBG("INT 0x13 Offset: 0x%04x\n", InterruptVector->Offset);
    DBG("INT 0x13 Hook: 0x%08x\n", int13_hook);
    DBG("INT 0x13 Safe Hook Signature: %s\n", sig);
    if (!wv_memcmpeq(sig, "$INT13SF", sizeof "$INT13SF" - 1)) {
        DBG("Invalid INT 0x13 Safe Hook Signature; End of chain\n");
        return NULL;
      }
    return safe_mbr_hook;
  }

void WvProbeDisks(void) {
    static winvblock__bool probed = FALSE;

    if (probed)
      return;
    WvMemdiskFind();
    ramdisk_grub4dos__find();
    filedisk_grub4dos__find();
    probed = TRUE;
  }
