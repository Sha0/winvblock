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

/****
 * @file
 *
 * "Safe INT 0x13 hook" probing/walking
 */

#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_string.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "mount.h"
#include "debug.h"
#include "bus.h"
#include "ramdisk.h"
#include "grub4dos.h"
#include "thread.h"
#include "filedisk.h"
#include "x86.h"
#include "safehook.h"
#include "memdisk.h"

/*** Function declarations */
static BOOLEAN WvGrub4dosProcessSlot(SP_WV_G4D_DRIVE_MAPPING);
static BOOLEAN WvGrub4dosProcessSafeHook(
    PUCHAR,
    SP_X86_SEG16OFF16,
    WV_SP_PROBE_SAFE_MBR_HOOK
  );

/*** Function definitions */

/* Process a potential INT 0x13 "safe hook" */
PDEVICE_OBJECT STDCALL WvSafeHookProbe(
    IN SP_X86_SEG16OFF16 HookAddr,
    IN PDEVICE_OBJECT ParentBus
  ) {
    static const CHAR sig[] = "$INT13SF";
    PHYSICAL_ADDRESS phys_addr;
    PUCHAR phys_mem;
    PDEVICE_OBJECT dev;
    UINT32 hook_addr;
    WV_SP_PROBE_SAFE_MBR_HOOK safe_mbr_hook;

    dev = NULL;

    /* Map the first MiB of memory */
    phys_addr.QuadPart = 0LL;
    phys_mem = MmMapIoSpace(phys_addr, 0x100000, MmNonCached);
    if (!phys_mem) {
        DBG("Could not map low memory!\n");
        goto err_map;
      }

    /* Special-case 0:004C to use the IVT entry for INT 0x13 */
    if (HookAddr->Segment == 0 && HookAddr->Offset == (4 * 0x13))
      HookAddr = (PVOID) (phys_mem + 4 * 0x13);

    hook_addr = M_X86_SEG16OFF16_ADDR(HookAddr);
    DBG(
        "Probing for safe hook at %04X:%04X (0x%08X)...\n",
        HookAddr->Segment,
        HookAddr->Offset,
        hook_addr
      );

    /* Check for the signature */
    safe_mbr_hook = (WV_SP_PROBE_SAFE_MBR_HOOK) (phys_mem + hook_addr);
    if (!wv_memcmpeq(safe_mbr_hook->Signature, sig, sizeof sig - 1)) {
        DBG("Invalid safe INT 0x13 hook signature.  End of chain\n");
        goto err_sig;
      }

    /* Found one */
    DBG("Found safe hook with vendor ID: %.8s\n", safe_mbr_hook->VendorId);
    dev = WvSafeHookPdoCreate(HookAddr, safe_mbr_hook, ParentBus);

    err_sig:

    MmUnmapIoSpace(phys_mem, 0x100000);
    err_map:

    return dev;
  }

/** Process a GRUB4DOS drive mapping slot.  Probably belongs elsewhere */
static BOOLEAN WvGrub4dosProcessSlot(SP_WV_G4D_DRIVE_MAPPING slot) {
    WVL_E_DISK_MEDIA_TYPE media_type;
    UINT32 sector_size;

    /* Check for an empty mapping */
    if (slot->SectorCount == 0)
      return FALSE;
    DBG("GRUB4DOS SourceDrive: 0x%02x\n", slot->SourceDrive);
    DBG("GRUB4DOS DestDrive: 0x%02x\n", slot->DestDrive);
    DBG("GRUB4DOS MaxHead: %d\n", slot->MaxHead);
    DBG("GRUB4DOS MaxSector: %d\n", slot->MaxSector);
    DBG("GRUB4DOS DestMaxCylinder: %d\n", slot->DestMaxCylinder);
    DBG("GRUB4DOS DestMaxHead: %d\n", slot->DestMaxHead);
    DBG("GRUB4DOS DestMaxSector: %d\n", slot->DestMaxSector);
    DBG("GRUB4DOS SectorStart: 0x%08x\n", slot->SectorStart);
    DBG("GRUB4DOS SectorCount: %d\n", slot->SectorCount);

    if (slot->SourceODD) {
        media_type = WvlDiskMediaTypeOptical;
        sector_size = 2048;
      } else {
        media_type =
          (slot->SourceDrive & 0x80) ?
          WvlDiskMediaTypeHard :
          WvlDiskMediaTypeFloppy;
        sector_size = 512;
      }

    /* Check for a RAM disk mapping */
    if (slot->DestDrive == 0xFF) {
        WvRamdiskCreateG4dDisk(slot, media_type, sector_size);
      } else {
        WvFilediskCreateG4dDisk(slot, media_type, sector_size);
      }
    return TRUE;
  }

/** Process a GRUB4DOS "safe hook".  Probably belongs elsewhere */
static BOOLEAN WvGrub4dosProcessSafeHook(
    PUCHAR phys_mem,
    SP_X86_SEG16OFF16 segoff,
    WV_SP_PROBE_SAFE_MBR_HOOK safe_mbr_hook
  ) {
    enum {CvG4dSlots = 8};
    static const UCHAR sig[sizeof safe_mbr_hook->VendorId] = "GRUB4DOS";
    SP_WV_G4D_DRIVE_MAPPING g4d_map;
    #ifdef TODO_RESTORE_FILE_MAPPED_G4D_DISKS
    WV_SP_FILEDISK_GRUB4DOS_DRIVE_FILE_SET sets;
    #endif
    int i;
    BOOLEAN found = FALSE;

    if (!wv_memcmpeq(safe_mbr_hook->VendorId, sig, sizeof sig)) {
        DBG("Not a GRUB4DOS safe hook\n");
        return FALSE;
      }
    DBG("Processing GRUB4DOS safe hook...\n");

    #ifdef TODO_RESTORE_FILE_MAPPED_G4D_DISKS
    sets = wv_mallocz(sizeof *sets * CvG4dSlots);
    if (!sets) {
        DBG("Couldn't allocate GRUB4DOS file mapping!\n");
    #endif

    g4d_map = (SP_WV_G4D_DRIVE_MAPPING) (
        phys_mem + (((UINT32) segoff->Segment) << 4) + 0x20
      );
    /* Process each drive mapping slot */
    i = CvG4dSlots;
    while (i--)
      found |= WvGrub4dosProcessSlot(g4d_map + i);
    DBG("%sGRUB4DOS drives found\n", found ? "" : "No ");

    return TRUE;
  }
