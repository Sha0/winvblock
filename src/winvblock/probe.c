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
#include "libthread.h"
#include "filedisk.h"
#include "x86.h"
#include "probe.h"
#include "memdisk.h"

/*** Function definitions. */
static WV_SP_PROBE_SAFE_MBR_HOOK STDCALL WvProbeGetSafeHook(
    IN PUCHAR PhysicalMemory,
    IN SP_X86_SEG16OFF16 InterruptVector
  ) {
    UINT32 int13_hook;
    WV_SP_PROBE_SAFE_MBR_HOOK safe_mbr_hook;
    UCHAR sig[9] = {0};
    UCHAR ven_id[9] = {0};

    int13_hook = M_X86_SEG16OFF16_ADDR(InterruptVector);
    safe_mbr_hook = (WV_SP_PROBE_SAFE_MBR_HOOK) (PhysicalMemory + int13_hook);
    RtlCopyMemory(sig, safe_mbr_hook->Signature, sizeof sig - 1);
    RtlCopyMemory(ven_id, safe_mbr_hook->VendorId, sizeof ven_id - 1);

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

/** Probe the IVT for a "safe INT 0x13 hook" */
static BOOLEAN WvProbeIvtForSafeHook(void) {
    PHYSICAL_ADDRESS phys_addr;
    PVOID phys_mem;
    SP_X86_SEG16OFF16 int_vector;
    BOOLEAN found = FALSE;

    /* Map the first MiB of memory. */
    phys_addr.QuadPart = 0LL;
    phys_mem = MmMapIoSpace(phys_addr, 0x100000, MmNonCached);
    if (!phys_mem) {
        DBG("Could not map low memory!\n");
        goto err_map;
      }

    int_vector = phys_mem;
    int_vector += 0x13;
    found = WvProbeSafeHookChain(phys_mem, int_vector);

    MmUnmapIoSpace(phys_mem, 0x100000);
    err_map:

    return found;
  }

/** Process a GRUB4DOS drive mapping slot.  Probably belongs elsewhere. */
static BOOLEAN WvGrub4dosProcessSlot(WV_SP_GRUB4DOS_DRIVE_MAPPING slot) {
    WVL_E_DISK_MEDIA_TYPE media_type;
    UINT32 sector_size;

    /* Check for an empty mapping. */
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

    /* Check for a RAM disk mapping. */
    if (slot->DestDrive == 0xFF) {
        WvRamdiskCreateG4dDisk(slot, media_type, sector_size);
      } else {
        WvFilediskCreateG4dDisk(slot, media_type, sector_size);
      }
    return TRUE;
  }

/** Process a GRUB4DOS "safe hook".  Probably belongs elsewhere. */
static BOOLEAN WvGrub4dosProcessSafeHook(
    PUCHAR phys_mem,
    SP_X86_SEG16OFF16 segoff,
    WV_SP_PROBE_SAFE_MBR_HOOK safe_mbr_hook
  ) {
    enum {CvG4dSlots = 8};
    static const UCHAR sig[sizeof safe_mbr_hook->VendorId] = "GRUB4DOS";
    WV_SP_GRUB4DOS_DRIVE_MAPPING g4d_map;
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

    g4d_map = (WV_SP_GRUB4DOS_DRIVE_MAPPING) (
        phys_mem + (((UINT32) segoff->Segment) << 4) + 0x20
      );
    /* Process each drive mapping slot. */
    i = CvG4dSlots;
    while (i--)
      found |= WvGrub4dosProcessSlot(g4d_map + i);
    DBG("%sGRUB4DOS drives found\n", found ? "" : "No ");

    return TRUE;
  }

/** Walk the chain of "safe INT 0x13 hooks" */
BOOLEAN WvProbeSafeHookChain(
    PUCHAR phys_mem,
    SP_X86_SEG16OFF16 segoff
  ) {
    WV_SP_PROBE_SAFE_MBR_HOOK safe_mbr_hook;
    BOOLEAN found = FALSE;

    /* Walk the "safe hook" chain of INT 0x13 hooks as far as possible. */
    while (safe_mbr_hook = WvProbeGetSafeHook(phys_mem, segoff)) {
        found |=
          WvMemdiskProcessSafeHook(phys_mem, safe_mbr_hook) ||
          WvGrub4dosProcessSafeHook(phys_mem, segoff, safe_mbr_hook);
        segoff = &safe_mbr_hook->PrevHook;
      }

    DBG(found ? "Safe hook chain walked\n" : "No safe hooks processed\n");
    return found;
  }

VOID WvProbeDisks(void) {
    WvProbeIvtForSafeHook();
    WvMemdiskFind();
  }
