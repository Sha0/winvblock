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

/**
 * @file
 *
 * MEMDISK specifics.
 */

#include <stdio.h>
#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_string.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "ramdisk.h"
#include "debug.h"
#include "mdi.h"
#include "probe.h"

static BOOLEAN STDCALL WvMemdiskCheckMbft_(
    PUCHAR phys_mem,
    UINT32 offset
  ) {
    WV_SP_MDI_MBFT mbft = (WV_SP_MDI_MBFT) (phys_mem + offset);
    UINT32 i;
    UCHAR chksum = 0;
    WV_SP_PROBE_SAFE_MBR_HOOK assoc_hook;
    WV_SP_RAMDISK_T ramdisk;

    if (offset >= 0x100000) {
        DBG("mBFT physical pointer too high!\n");
        return FALSE;
      }
    if (!wv_memcmpeq(mbft->Signature, "mBFT", sizeof "mBFT" - 1)) return FALSE;
    if (offset + mbft->Length >= 0x100000) {
        DBG("mBFT length out-of-bounds\n");
        return FALSE;
      }
    for (i = 0; i < mbft->Length; i++)
      chksum += ((UCHAR *) mbft)[i];
    if (chksum) {
        DBG("Invalid mBFT checksum\n");
        return FALSE;
      }
    DBG("Found mBFT: 0x%08x\n", mbft);
    if (mbft->SafeHook >= 0x100000) {
        DBG("mBFT safe hook physical pointer too high!\n");
        return FALSE;
      }
    assoc_hook =
      (WV_SP_PROBE_SAFE_MBR_HOOK) (phys_mem + mbft->SafeHook);
    if (assoc_hook->Flags) {
        DBG("This MEMDISK already processed\n");
        return TRUE;
      }
    DBG("MEMDISK DiskBuf: 0x%08x\n", mbft->mdi.diskbuf);
    DBG("MEMDISK DiskSize: %d sectors\n", mbft->mdi.disksize);
    ramdisk = ramdisk__create();
    if (ramdisk == NULL) {
        DBG("Could not create MEMDISK disk!\n");
        return FALSE;
      }
    ramdisk->DiskBuf = mbft->mdi.diskbuf;
    ramdisk->disk->LBADiskSize = ramdisk->DiskSize = mbft->mdi.disksize;
    if (mbft->mdi.driveno == 0xE0) {
        ramdisk->disk->Media = WvlDiskMediaTypeOptical;
        ramdisk->disk->SectorSize = 2048;
      } else {
        if (mbft->mdi.driveno & 0x80)
        	ramdisk->disk->Media = WvlDiskMediaTypeHard;
          else
        	ramdisk->disk->Media = WvlDiskMediaTypeFloppy;
        ramdisk->disk->SectorSize = 512;
      }
    DBG("RAM Drive is type: %d\n", ramdisk->disk->Media);
    ramdisk->disk->Cylinders = mbft->mdi.cylinders;
    ramdisk->disk->Heads = mbft->mdi.heads;
    ramdisk->disk->Sectors = mbft->mdi.sectors;
    ramdisk->disk->Dev->Boot = TRUE;

    /* Add the ramdisk to the bus. */
    if (!WvBusAddDev(ramdisk->disk->Dev)) {
        WvDevFree(ramdisk->disk->Dev);
        return FALSE;
      }
    assoc_hook->Flags = 1;
    return TRUE;
  }

VOID WvMemdiskFind(void) {
    PHYSICAL_ADDRESS phys_addr;
    PUCHAR phys_mem;
    WV_SP_PROBE_INT_VECTOR int_vector;
    WV_SP_PROBE_SAFE_MBR_HOOK safe_mbr_hook;
    UINT32 offset;
    BOOLEAN found = FALSE;

    /* Find a MEMDISK.  We check the "safe hook" chain, then scan for mBFTs. */
    phys_addr.QuadPart = 0LL;
    phys_mem = MmMapIoSpace(phys_addr, 0x100000, MmNonCached);
    if (!phys_mem) {
        DBG("Could not map low memory\n");
        return;
      }
    int_vector =
      (WV_SP_PROBE_INT_VECTOR) (phys_mem + 0x13 * sizeof *int_vector);
    /* Walk the "safe hook" chain of INT 13h hooks as far as possible. */
    while (safe_mbr_hook = WvProbeGetSafeHook(phys_mem, int_vector)) {
        if (!wv_memcmpeq(safe_mbr_hook->VendorId, "MEMDISK ", 8)) {
        	  DBG("Non-MEMDISK INT 0x13 Safe Hook\n");
        	} else {
        	  found |= WvMemdiskCheckMbft_(phys_mem, safe_mbr_hook->Mbft);
        	}
        int_vector = &safe_mbr_hook->PrevHook;
      }
    /* Now search for "floating" mBFTs. */
    for (offset = 0; offset < 0xFFFF0; offset += 0x10) {
        found |= WvMemdiskCheckMbft_(phys_mem, offset);
      }
    MmUnmapIoSpace(phys_mem, 0x100000);
    if (!found) {
        DBG("No MEMDISKs found\n");
      }
  }
