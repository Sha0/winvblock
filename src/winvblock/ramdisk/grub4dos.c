/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
 * GRUB4DOS RAM disk specifics.
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
#include "probe.h"
#include "grub4dos.h"

/* Scan for GRUB4DOS RAM disks and add them to the WinVBlock bus. */
VOID WvRamdiskG4dFind(void) {
    PHYSICAL_ADDRESS phys_addr;
    PUCHAR phys_mem;
    WV_SP_PROBE_INT_VECTOR int_vector;
    WV_SP_PROBE_SAFE_MBR_HOOK safe_mbr_hook;
    WV_SP_GRUB4DOS_DRIVE_MAPPING g4d_drive_mapping;
    UINT32 i = 8;
    BOOLEAN found = FALSE;
    WV_SP_RAMDISK_T ramdisk;

    /*
     * Find a GRUB4DOS memory-mapped disk.  Start by looking at the
     * real-mode IDT and following the "SafeMBRHook" INT 0x13 hook.
     */
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
        if (!wv_memcmpeq(
            safe_mbr_hook->VendorId,
            "GRUB4DOS",
            sizeof "GRUB4DOS" - 1
          )) {
              DBG("Non-GRUB4DOS INT 0x13 Safe Hook\n");
              int_vector = &safe_mbr_hook->PrevHook;
              continue;
          }
        g4d_drive_mapping = (WV_SP_GRUB4DOS_DRIVE_MAPPING) (
            phys_mem +
            (((UINT32) int_vector->Segment) << 4) +
            0x20
          );
        while (i--) {
            WVL_E_DISK_MEDIA_TYPE media_type;
            UINT32 sector_size;

            DBG(
                "GRUB4DOS SourceDrive: 0x%02x\n",
                g4d_drive_mapping[i].SourceDrive
              );
            DBG(
                "GRUB4DOS DestDrive: 0x%02x\n",
                g4d_drive_mapping[i].DestDrive
              );
            DBG("GRUB4DOS MaxHead: %d\n", g4d_drive_mapping[i].MaxHead);
            DBG(
                "GRUB4DOS MaxSector: %d\n",
                g4d_drive_mapping[i].MaxSector
              );
            DBG(
                "GRUB4DOS DestMaxCylinder: %d\n",
                g4d_drive_mapping[i].DestMaxCylinder
              );
            DBG(
                "GRUB4DOS DestMaxHead: %d\n",
                g4d_drive_mapping[i].DestMaxHead
              );
            DBG(
                "GRUB4DOS DestMaxSector: %d\n",
                g4d_drive_mapping[i].DestMaxSector
              );
            DBG(
                "GRUB4DOS SectorStart: 0x%08x\n",
                g4d_drive_mapping[i].SectorStart
              );
            DBG(
                "GRUB4DOS SectorCount: %d\n",
                g4d_drive_mapping[i].SectorCount
              );
            if (!(g4d_drive_mapping[i].DestDrive == 0xff)) {
                DBG("Skipping non-RAM disk GRUB4DOS mapping\n");
                continue;
              }
            /* Possible precision loss. */
            if (g4d_drive_mapping[i].SourceODD) {
                media_type = WvlDiskMediaTypeOptical;
                sector_size = 2048;
              } else {
                media_type =
                  g4d_drive_mapping[i].SourceDrive & 0x80 ?
                  WvlDiskMediaTypeHard :
                  WvlDiskMediaTypeFloppy;
                sector_size = 512;
              }
            ramdisk = WvRamdiskCreatePdo(media_type);
            if (ramdisk == NULL) {
                DBG("Could not create GRUB4DOS disk!\n");
                return;
              }
            DBG("RAM Drive is type: %d\n", media_type);
            ramdisk->DiskBuf =
              (UINT32) (g4d_drive_mapping[i].SectorStart * 512);
            ramdisk->disk->LBADiskSize = ramdisk->DiskSize =
              (UINT32) g4d_drive_mapping[i].SectorCount;
            ramdisk->disk->Heads = g4d_drive_mapping[i].MaxHead + 1;
            ramdisk->disk->Sectors = g4d_drive_mapping[i].DestMaxSector;
            ramdisk->disk->Cylinders =
              ramdisk->disk->LBADiskSize /
              (ramdisk->disk->Heads * ramdisk->disk->Sectors);
            ramdisk->disk->Dev->Boot = TRUE;
            found = TRUE;
            ramdisk->disk->Media = media_type;
            ramdisk->disk->SectorSize = sector_size;
             /* Add the ramdisk to the bus. */
            if (!WvBusAddDev(ramdisk->disk->Dev))
              WvDevFree(ramdisk->disk->Dev);
            ramdisk->disk->ParentBus = ramdisk->Dev->Parent;
          } /* while i */
        int_vector = &safe_mbr_hook->PrevHook;
      } /* while safe hook chain. */

    MmUnmapIoSpace(phys_mem, 0x100000);
    if (!found) {
        DBG("No GRUB4DOS drive mappings found\n");
      }
    return;
  }
