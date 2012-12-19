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
 * GRUB4DOS mini-driver
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
#include "grub4dos.h"

/** Macros */
#define M_WV_G4D_SAFE_HOOK_SIGNATURE "GRUB4DOS"

/** Public function declarations */
DRIVER_INITIALIZE WvG4dDriverEntry;

/** Function declarations */
static DRIVER_ADD_DEVICE WvG4dDriveDevice;
static DRIVER_UNLOAD WvG4dUnload;
/** Objects */
static S_WVL_MINI_DRIVER * WvG4dMiniDriver;

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
NTSTATUS WvG4dDriverEntry(
    IN DRIVER_OBJECT * drv_obj,
    IN UNICODE_STRING * reg_path
  ) {
    NTSTATUS status;

    /* Register this mini-driver */
    status = WvlRegisterMiniDriver(
        &WvG4dMiniDriver,
        drv_obj,
        WvG4dDriveDevice,
        WvG4dUnload
      );
    if (!NT_SUCCESS(status))
      goto err_register;
    ASSERT(WvG4dMiniDriver);

    return STATUS_SUCCESS;

    err_register:

    return status;
  }

/**
 * Drive a safe hook PDO with a GRUB4DOS FDO
 *
 * @param DriverObject
 *   The driver object provided by the caller
 *
 * @param PhysicalDeviceObject
 *   The PDO to probe and attach the GRUB4DOS FDO to
 *
 * @return
 *   The status of the operation
 */
static NTSTATUS STDCALL WvG4dDriveDevice(
    IN DRIVER_OBJECT * drv_obj,
    IN DEVICE_OBJECT * pdo
  ) {
    static const UCHAR g4d_sig[] = M_WV_G4D_SAFE_HOOK_SIGNATURE;
    S_X86_SEG16OFF16 * safe_hook;
    NTSTATUS status;
    UCHAR * phys_mem;
    UINT32 hook_phys_addr;
    WV_S_PROBE_SAFE_MBR_HOOK * hook;
    LONG flags;
    DEVICE_OBJECT * fdo;
    S_WV_G4D_BUS * bus;
    UINT i;

    ASSERT(drv_obj);
    ASSERT(pdo);

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

    /* Check if this is a GRUB4DOS hook */
    if (!wv_memcmpeq(hook->VendorId, g4d_sig, sizeof g4d_sig - 1)) {
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
        /*
         * This is a known bug with some versions of GRUB4DOS.  Hack
         * around it and accept it anyway.  Hopefully another driver
         * doesn't conflict with us, else there'll be two drivers both
         * providing access to the same device, which'll result in having
         * the same single-system filesystem(s) mounted with write
         * capabilities, twice.  That'd be trouble for that FS (and the OS)
         */
        hook->VendorId[6] = '0';
      }

    fdo = NULL;
    status = WvlCreateDevice(
        WvG4dMiniDriver,
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

    bus->DeviceExtension->IrpDispatch = WvG4dIrpDispatch;
    bus->Flags = 0;
    bus->PhysicalDeviceObject = pdo;
    bus->BusRelations->Count = 0;
    bus->BusRelations->Objects[0] = NULL;
    RtlCopyMemory(
        bus->DriveMappings,
        phys_mem + (((UINT32) safe_hook->Segment) << 4) + 0x20,
        sizeof bus->DriveMappings
      );
    /* Always try to create a PDO for the previous INT 0x13 handler */
    bus->NodesNeeded = 1 << M_G4D_SLOTS;
    bus->PreviousInt13hHandler[0] = hook->PrevHook;

    /* Examine the drive mappings and note which of them need a PDO */
    for (i = 0; i < M_G4D_SLOTS; ++i) {
        if (!bus->DriveMappings[i].SectorCount)
          continue;
        bus->NodesNeeded |= 1 << i;
      }

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
static VOID STDCALL WvG4dUnload(IN DRIVER_OBJECT * drv_obj) {
    ASSERT(drv_obj);
    (VOID) drv_obj;
    WvlDeregisterMiniDriver(WvG4dMiniDriver);
  }

DEVICE_OBJECT * WvG4dProcessSlot(
    IN DEVICE_OBJECT * bus_dev_obj,
    IN S_WV_G4D_DRIVE_MAPPING * slot
  ) {
    WVL_E_DISK_MEDIA_TYPE media_type;
    UINT32 sector_size;
    DEVICE_OBJECT * new_dev_obj;

    ASSERT(slot->SectorCount);

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
        new_dev_obj = WvRamdiskCreateG4dDisk(
            slot,
            bus_dev_obj,
            media_type,
            sector_size
          );
      } else {
        new_dev_obj = WvFilediskCreateG4dDisk(
            slot,
            bus_dev_obj,
            media_type,
            sector_size
          );
      }

    return new_dev_obj;
  }

NTSTATUS WvG4dCreateDisk(
    IN DEVICE_OBJECT * parent,
    IN DEVICE_OBJECT ** pdo
  ) {
    return STATUS_SUCCESS;
  }
