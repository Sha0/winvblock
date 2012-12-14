/**
 * Copyright (C) 2012, Shao Miller <sha0.miller@gmail.com>.
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
 * MEMDISK mini-driver
 */

#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "wv_string.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "ramdisk.h"
#include "debug.h"
#include "x86.h"
#include "safehook.h"
#include "mdi.h"
#include "memdisk.h"

/** Macros */
#define M_WV_MEMDISK_SAFE_HOOK_SIGNATURE "MEMDISK "

/** Public function declarations */
DRIVER_INITIALIZE WvMemdiskDriverEntry;

/** Function declarations */
static DRIVER_ADD_DEVICE WvMemdiskDriveDevice;
static DRIVER_UNLOAD WvMemdiskUnload;
static VOID WvMemdiskInitialProbe(IN DEVICE_OBJECT * DeviceObject);
static BOOLEAN STDCALL WvMemdiskCheckMbft(
    UCHAR * PhysicalMemory,
    UINT32 Offset
  );

/** Objects */
static S_WVL_MINI_DRIVER * WvMemdiskMiniDriver;

/** Function definitions */

/**
 * The mini-driver entry-point
 *
 * @param DriverObject
 *   The driver object provided by the caller
 *
 * @param RegistryPath
 *   The Registry path provided by the caller
 *
 * @return
 *   The status of the operation
 */
NTSTATUS WvMemdiskDriverEntry(
    IN DRIVER_OBJECT * drv_obj,
    IN UNICODE_STRING * reg_path
  ) {
    static S_WVL_MAIN_BUS_PROBE_REGISTRATION probe_reg;
    NTSTATUS status;

    /* Register this mini-driver */
    status = WvlRegisterMiniDriver(
        &WvMemdiskMiniDriver,
        drv_obj,
        WvMemdiskDriveDevice,
        WvMemdiskUnload
      );
    if (!NT_SUCCESS(status))
      goto err_register;
    ASSERT(WvMemdiskMiniDriver);

    probe_reg.Callback = WvMemdiskInitialProbe;
    WvlRegisterMainBusInitialProbeCallback(&probe_reg);

    return STATUS_SUCCESS;

    err_register:

    return status;
  }

/**
 * Drive a safe hook PDO with a MEMDISK FDO
 *
 * @param DriverObject
 *   The driver object provided by the caller
 *
 * @param PhysicalDeviceObject
 *   The PDO to probe and attach the MEMDISK FDO to
 *
 * @return
 *   The status of the operation
 */
static NTSTATUS STDCALL WvMemdiskDriveDevice(
    IN DRIVER_OBJECT * drv_obj,
    IN DEVICE_OBJECT * pdo
  ) {
    static const UCHAR memdisk_sig[] = M_WV_MEMDISK_SAFE_HOOK_SIGNATURE;
    S_X86_SEG16OFF16 * safe_hook;
    NTSTATUS status;
    UCHAR * phys_mem;
    UINT32 hook_phys_addr;
    WV_S_PROBE_SAFE_MBR_HOOK * hook;
    LONG flags;
    DEVICE_OBJECT * fdo;
    S_WV_MEMDISK_BUS * bus;

    if (pdo->DriverObject != drv_obj || !(safe_hook = WvlGetSafeHook(pdo))) {
        status = STATUS_NOT_SUPPORTED;
        goto err_safe_hook;
      }

    /* Examine the hook */
    phys_mem = WvlMapUnmapLowMemory(NULL);
    if (!phys_mem) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_phys_mem;
      }

    hook_phys_addr = M_X86_SEG16OFF16_ADDR(safe_hook);
    hook = (VOID *) (phys_mem + hook_phys_addr);

    /* Check if this is a MEMDISK hook */
    if (!wv_memcmpeq(hook->VendorId, memdisk_sig, sizeof memdisk_sig - 1)) {
        status = STATUS_NOT_SUPPORTED;
        goto err_sig;
      }

    /*
     * Quickly claim the safe hook.  Let's hope other drivers offer
     * the same courtesy
     */
    flags = InterlockedOr(&hook->Flags, 1);
    if (flags & 1) {
        DBG("Safe hook already claimed\n");
        status = STATUS_DEVICE_BUSY;
        goto err_claimed;
      }

    fdo = NULL;
    status = WvlCreateDevice(
        WvMemdiskMiniDriver,
        sizeof *bus,
        NULL,
        FILE_DEVICE_CONTROLLER,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &fdo
      );
    if (!NT_SUCCESS(status))
      goto err_fdo;
    ASSERT(fdo);

    bus = fdo->DeviceExtension;
    ASSERT(bus);

    bus->DeviceExtension->IrpDispatch = WvMemdiskIrpDispatch;
    bus->Flags = 0;
    bus->PhysicalDeviceObject = pdo;
    bus->BusRelations->Count = 0;
    bus->BusRelations->Objects[0] = NULL;
    bus->PreviousInt13hHandler[0] = hook->PrevHook;

    /* Attach the FDO to the PDO */
    if (!WvlAttachDeviceToDeviceStack(fdo, pdo)) {
        DBG("Error driving PDO %p!\n", (VOID *) pdo);
        status = STATUS_DRIVER_INTERNAL_ERROR;
        goto err_lower;
      }

    fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    DBG("Driving PDO %p with FDO %p\n", (VOID *) pdo, (VOID *) fdo);
    status = STATUS_SUCCESS;
    goto out;

    err_lower:

    WvlDeleteDevice(fdo);
    err_fdo:

    flags = InterlockedAnd(&hook->Flags, ~1);
    err_claimed:

    err_sig:

    out:

    WvlMapUnmapLowMemory(phys_mem);
    err_phys_mem:

    err_safe_hook:

    if (!NT_SUCCESS(status))
      DBG("Refusing to drive PDO %p\n", (VOID *) pdo);
    return status;
  }

/**
 * Release resources and unwind state
 *
 * @param DriverObject
 *   The driver object provided by the caller
 */
static VOID STDCALL WvMemdiskUnload(IN DRIVER_OBJECT * drv_obj) {
    ASSERT(drv_obj);
    (VOID) drv_obj;
    WvlDeregisterMiniDriver(WvMemdiskMiniDriver);
  }

/**
 * Main bus initial bus relations probe callback
 *
 * @param DeviceObject
 *   The main bus device
 */
static VOID WvMemdiskInitialProbe(IN DEVICE_OBJECT * dev_obj) {
    UCHAR * phys_mem;
    BOOLEAN found;
    UINT32 offset;

    phys_mem = WvlMapUnmapLowMemory(NULL);
    if (!phys_mem) {
        DBG("Couldn't map low memory\n");
        goto err_phys_mem;
      }

    for (found = FALSE, offset = 0; offset < 0xFFFF0; offset += 0x10) {
        if (WvMemdiskCheckMbft(phys_mem, offset))
          found = TRUE;
      }

    if (found)
      DBG("Processed one or more mBFTs\n");

    WvlMapUnmapLowMemory(phys_mem);
    err_phys_mem:

    return;
  }

/**
 * Check an address for an mBFT and create a device for it
 *
 * @param PhysicalMemory
 *   Points to mapped low memory
 *
 * @param Offset
 *   The offset in the low memory to check for an mBFT
 *
 * @retval FALSE - An mBFT was not found or an invalid mBFT was found
 * @retval TRUE - An mBFT was found and processed
 *
 * This function will attempt to produce a safe hook PDO for the mBFT,
 * but could potentially fail to do so without informing the caller
 */
static BOOLEAN STDCALL WvMemdiskCheckMbft(
    UCHAR * phys_mem,
    UINT32 offset
  ) {
    static const UCHAR mbft_sig[] = "mBFT";
    const UINT32 one_mib = 0x100000;
    WV_S_MDI_MBFT * mbft;
    UCHAR * i;
    UCHAR * mbft_end;
    UCHAR chksum;
    S_X86_SEG16OFF16 assoc_hook;
    DEVICE_OBJECT * dev_obj;
    NTSTATUS status;

    ASSERT(phys_mem);
    ASSERT(offset < one_mib);

    mbft = (VOID *) (phys_mem + offset);

    /* Check signature */
    if (!wv_memcmpeq(mbft->Signature, mbft_sig, sizeof mbft_sig - 1))
      return FALSE;

    /* Sanity check */
    if (offset + mbft->Length >= one_mib) {
        DBG("mBFT length out-of-bounds\n");
        return FALSE;
      }

    /* Check sum */
    chksum = 0;
    i = (VOID *) mbft;
    for (mbft_end = i + mbft->Length; i < mbft_end; ++i)
      chksum += *i;
    if (chksum) {
        DBG("Invalid mBFT checksum\n");
        return FALSE;
      }

    DBG("Found mBFT at physical address: 0x%08X\n", offset);

    /* Sanity check */
    if (mbft->SafeHook >= one_mib) {
        DBG("mBFT safe hook physical pointer too high!\n");
        return FALSE;
      }

    /* Produce the associated safe hook */
    assoc_hook.Segment = mbft->SafeHook >> 4;
    assoc_hook.Offset = mbft->SafeHook & 0xF;
    dev_obj = NULL;
    status = WvlCreateSafeHookDevice(&assoc_hook, &dev_obj);
    if (!NT_SUCCESS(status)) {
        DBG("Error creating mBFT safe hook PDO\n");
        return TRUE;
      }
    ASSERT(dev_obj);

    /* Hack: Prevent the safe hook initial probe from duplicating */
    {
        WV_S_PROBE_SAFE_MBR_HOOK * hook = (VOID *) (phys_mem + mbft->SafeHook);
        hook->Signature[0] = '!';
      }

    status = WvlAddDeviceToMainBus(dev_obj);
    if (!NT_SUCCESS(status)) {
        DBG("Couldn't add mBFT safe hook PDO to main bus\n");
        WvlDeleteDevice(dev_obj);
        return TRUE;
      }

    return TRUE;
  }

NTSTATUS WvMemdiskCreateDisk(
    IN DEVICE_OBJECT * parent,
    IN DEVICE_OBJECT ** pdo
  ) {
    S_X86_SEG16OFF16 * safe_hook;
    UCHAR * phys_mem;
    NTSTATUS status;
    UINT32 hook_phys_addr;
    WV_S_PROBE_SAFE_MBR_HOOK * hook;
    WV_S_MDI_MBFT * mbft;
    WVL_E_DISK_MEDIA_TYPE media_type;
    UINT32 sector_size;
    WV_SP_RAMDISK_T ramdisk;

    ASSERT(parent);
    safe_hook = WvlGetSafeHook(parent);
    ASSERT(safe_hook);

    phys_mem = WvlMapUnmapLowMemory(NULL);
    if (!phys_mem) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_phys_mem;
      }

    hook_phys_addr = M_X86_SEG16OFF16_ADDR(safe_hook);
    ASSERT(hook_phys_addr);
    hook = (VOID *) (phys_mem + hook_phys_addr);

    ASSERT(hook->Mbft);
    mbft = (VOID *) (phys_mem + hook->Mbft);

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

    /* Add the ramdisk to the bus */
    ramdisk->Dev->Parent = ramdisk->disk->ParentBus = parent;
    WvlBusInitNode(&ramdisk->Dev->BusNode, ramdisk->Dev->Self);
    ramdisk->Dev->BusNode.Linked = TRUE;
    ramdisk->Dev->Self->Flags &= ~DO_DEVICE_INITIALIZING;

    /* Return the new device */
    if (pdo)
      *pdo = ramdisk->Dev->Self;

    WvlMapUnmapLowMemory(phys_mem);
    err_phys_mem:

    return status;
  }
