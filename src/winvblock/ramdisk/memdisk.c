/**
 * Copyright (C) 2009-2012, Shao Miller <sha0.miller@gmail.com>.
 *
 * This file is part of WinVBlock, originally derived from WinAoE.
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
#include "x86.h"
#include "safehook.h"

static BOOLEAN STDCALL WvMemdiskCheckMbft_(
    PUCHAR phys_mem,
    UINT32 offset,
    BOOLEAN walk
  ) {
    WV_SP_MDI_MBFT mbft = (WV_SP_MDI_MBFT) (phys_mem + offset);
    UINT32 i;
    UCHAR chksum = 0;
    WV_SP_PROBE_SAFE_MBR_HOOK assoc_hook;
    WVL_E_DISK_MEDIA_TYPE media_type;
    UINT32 sector_size;
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
    if (mbft->mdi.driveno == 0xE0) {
        media_type = WvlDiskMediaTypeOptical;
        sector_size = 2048;
      } else {
        if (mbft->mdi.driveno & 0x80)
        	media_type = WvlDiskMediaTypeHard;
          else
        	media_type = WvlDiskMediaTypeFloppy;
        sector_size = 512;
      }
    ramdisk = WvRamdiskCreatePdo(media_type);
    if (ramdisk == NULL) {
        DBG("Could not create MEMDISK disk!\n");
        return FALSE;
      }
    ramdisk->DiskBuf = mbft->mdi.diskbuf;
    ramdisk->disk->LBADiskSize = ramdisk->DiskSize = mbft->mdi.disksize;
    DBG("RAM Drive is type: %d\n", media_type);
    ramdisk->disk->Cylinders = mbft->mdi.cylinders;
    ramdisk->disk->Heads = mbft->mdi.heads;
    ramdisk->disk->Sectors = mbft->mdi.sectors;
    ramdisk->disk->Media = media_type;
    ramdisk->disk->SectorSize = sector_size;
    ramdisk->Dev->Boot = TRUE;

    /* Add the ramdisk to the bus. */
    ramdisk->disk->ParentBus = WvBus.Fdo;
    if (!WvBusAddDev(ramdisk->Dev)) {
        WvDevFree(ramdisk->Dev);
        return FALSE;
      }
    assoc_hook->Flags = 1;
    /* Should we try to walk the "safe hook" chain? */
    if (walk)
      { DEVICE_OBJECT * TODO = NULL;
        WvlCreateSafeHookDevice(&assoc_hook->PrevHook, &TODO);
      }
    return TRUE;
  }

VOID WvMemdiskFind(void) {
    PHYSICAL_ADDRESS phys_addr;
    PUCHAR phys_mem;
    UINT32 offset;
    BOOLEAN found = FALSE;

    /* Find a MEMDISK by scanning for mBFTs.  Map the first MiB of memory. */
    phys_addr.QuadPart = 0LL;
    phys_mem = MmMapIoSpace(phys_addr, 0x100000, MmNonCached);
    if (!phys_mem) {
        DBG("Could not map low memory\n");
        goto err_map;
      }

    for (offset = 0; offset < 0xFFFF0; offset += 0x10) {
        found |= WvMemdiskCheckMbft_(phys_mem, offset, TRUE);
      }

    MmUnmapIoSpace(phys_mem, 0x100000);
    err_map:

    DBG("%smBFTs found\n", found ? "" : "No ");
    return;
  }

BOOLEAN WvMemdiskProcessSafeHook(
    PUCHAR phys_mem,
    WV_SP_PROBE_SAFE_MBR_HOOK mbr_safe_hook
  ) {
    return WvMemdiskCheckMbft_(phys_mem, mbr_safe_hook->Mbft, FALSE);
  }
