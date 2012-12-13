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
 * "Safe INT 0x13 hook" mini-driver
 */

#include <ntddk.h>
#include <stdio.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_string.h"
#include "wv_stdlib.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "mount.h"
#include "dummy.h"
#include "debug.h"
#include "bus.h"
#include "ramdisk.h"
#include "grub4dos.h"
#include "thread.h"
#include "filedisk.h"
#include "x86.h"
#include "safehook.h"
#include "memdisk.h"

/** Macros */

#define M_WV_SAFE_HOOK_SIGNATURE "$INT13SF"

/** Safe hook dummy IDs */
#define M_WV_SAFE_HOOK_VENDOR_ID WVL_M_WLIT L"\\%sHook\0\0\0\0\0\0\0"
#define M_WV_SAFE_HOOK_HARDWARE_ID WVL_M_WLIT L"\\Int13hHook\0"
#define M_WV_SAFE_HOOK_INSTANCE_ID L"HOOK_at_%08X\0\0\0\0"
#define M_WV_SAFE_HOOK_COMPAT_ID \
  M_WV_SAFE_HOOK_VENDOR_ID M_WV_SAFE_HOOK_HARDWARE_ID
#define M_WV_SAFE_HOOK_DEVICE_TEXT_DESC L"Safe INT 0x13 Hook Bus"

/** Default dummy IDs for a safe hook PDO */
WV_M_DUMMY_ID_GEN(
    static,
    WvSafeHookDefaultDummyIds,
    M_WV_SAFE_HOOK_HARDWARE_ID,
    M_WV_SAFE_HOOK_INSTANCE_ID,
    M_WV_SAFE_HOOK_VENDOR_ID,
    M_WV_SAFE_HOOK_COMPAT_ID,
    M_WV_SAFE_HOOK_DEVICE_TEXT_DESC,
    FILE_DEVICE_CONTROLLER,
    FILE_DEVICE_SECURE_OPEN
  );

/** Object types */

typedef UCHAR
  A_WV_SAFE_HOOK_VENDOR_STRING[sizeof M_WV_SAFE_HOOK_SIGNATURE];
typedef WCHAR
  A_WV_SAFE_HOOK_WIDE_VENDOR_STRING[sizeof M_WV_SAFE_HOOK_SIGNATURE];

/** This type is used for an alignment calculation */
typedef struct S_WV_SAFE_HOOK_DUMMY_WRAPPER {
    /** A dummy PDO extension */
    S_WVL_DUMMY_PDO DummyPdo[1];

    /** The type of this member was produced by WV_M_DUMMY_ID_GEN */
    struct WvSafeHookDefaultDummyIdsStruct_ DummyIdsWrapper[1];

    /** The associated safe hook */
    S_X86_SEG16OFF16 Whence[1];
  } S_WV_SAFE_HOOK_DUMMY_WRAPPER;

/** Constants */

static const ULONG CvWvSafeHookDummyIdsSize =
  sizeof (struct WvSafeHookDefaultDummyIdsStruct_);

/** Alignment calculations */
static const ULONG CvWvSafeHookDummyIdsOffset =
  FIELD_OFFSET(S_WV_SAFE_HOOK_DUMMY_WRAPPER, DummyIdsWrapper);
static const ULONG CvWvSafeHookDummyIdsExtraDataOffset =
  FIELD_OFFSET(S_WV_SAFE_HOOK_DUMMY_WRAPPER, Whence);

/** Public function declarations */
DRIVER_INITIALIZE WvSafeHookDriverEntry;

/** Function declarations */
static DRIVER_ADD_DEVICE WvSafeHookDriveDevice;
static DRIVER_UNLOAD WvSafeHookUnload;
static VOID WvSafeHookInitialProbe(IN DEVICE_OBJECT * DeviceObject);
static BOOLEAN WvGrub4dosProcessSlot(SP_WV_G4D_DRIVE_MAPPING);
static BOOLEAN WvGrub4dosProcessSafeHook(
    PUCHAR,
    SP_X86_SEG16OFF16,
    WV_SP_PROBE_SAFE_MBR_HOOK
  );
static WV_S_DUMMY_IDS * WvSafeHookDummyIds(
    IN A_WV_SAFE_HOOK_VENDOR_STRING * Vendor,
    IN S_X86_SEG16OFF16 * Whence
  );
static VOID WvSafeHookVendorString(
    A_WV_SAFE_HOOK_WIDE_VENDOR_STRING * Destination,
    A_WV_SAFE_HOOK_VENDOR_STRING * Source
  );

/** Objects */
static S_WVL_MINI_DRIVER * WvSafeHookMiniDriver;

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
NTSTATUS WvSafeHookDriverEntry(
    IN DRIVER_OBJECT * drv_obj,
    IN UNICODE_STRING * reg_path
  ) {
    static S_WVL_MAIN_BUS_PROBE_REGISTRATION probe_reg;
    NTSTATUS status;

    /* TODO: Build the PDO and FDO IRP dispatch tables */

    /* Register this mini-driver */
    status = WvlRegisterMiniDriver(
        &WvSafeHookMiniDriver,
        drv_obj,
        WvSafeHookDriveDevice,
        WvSafeHookUnload
      );
    if (!NT_SUCCESS(status))
      goto err_register;
    ASSERT(WvSafeHookMiniDriver);

    probe_reg.Callback = WvSafeHookInitialProbe;
    WvlRegisterMainBusInitialProbeCallback(&probe_reg);

    return STATUS_SUCCESS;

    err_register:

    return status;
  }

/**
 * Drive a safe hook PDO with a safe hook FDO
 *
 * @param DriverObject
 *   The driver object provided by the caller
 *
 * @param PhysicalDeviceObject
 *   The PDO to probe and attach the safe hook FDO to
 *
 * @return
 *   The status of the operation
 */
static NTSTATUS STDCALL WvSafeHookDriveDevice(
    IN DRIVER_OBJECT * drv_obj,
    IN DEVICE_OBJECT * pdo
  ) {
    NTSTATUS status;
    S_X86_SEG16OFF16 * safe_hook;
    UCHAR * phys_mem;
    UINT32 hook_phys_addr;
    WV_S_PROBE_SAFE_MBR_HOOK * hook;
    LONG flags;
    DEVICE_OBJECT * fdo;
    S_WV_SAFE_HOOK_BUS * bus;

    if (pdo->DriverObject != drv_obj || !(safe_hook = WvlGetSafeHook(pdo))) {
        status = STATUS_NOT_SUPPORTED;
        goto err_safe_hook;
      }

    /* Ok, we'll try to drive this PDO with an FDO */
    phys_mem = WvlMapUnmapLowMemory(NULL);
    if (!phys_mem) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_phys_mem;
      }

    hook_phys_addr = M_X86_SEG16OFF16_ADDR(safe_hook);
    hook = (VOID *) (phys_mem + hook_phys_addr);

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
        WvSafeHookMiniDriver,
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

    bus->DeviceExtension->IrpDispatch = WvSafeHookIrpDispatch;
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
static VOID STDCALL WvSafeHookUnload(IN DRIVER_OBJECT * drv_obj) {
    ASSERT(drv_obj);
    (VOID) drv_obj;
    WvlDeregisterMiniDriver(WvSafeHookMiniDriver);
  }

/**
 * Main bus initial bus relations probe callback
 *
 * @param DeviceObject
 *   The main bus device
 */
static VOID WvSafeHookInitialProbe(IN DEVICE_OBJECT * dev_obj) {
    S_X86_SEG16OFF16 int_13h;
    NTSTATUS status;
    DEVICE_OBJECT * first_hook;

    ASSERT(dev_obj);
    (VOID) dev_obj;

    /* Probe the first INT 0x13 vector for a safe hook */
    int_13h.Segment = 0;
    int_13h.Offset = 0x13 * sizeof int_13h;
    status = WvlCreateSafeHookDevice(&int_13h, &first_hook);
    if (!NT_SUCCESS(status))
      return;
    ASSERT(first_hook);

    status = WvlAddDeviceToMainBus(first_hook);
    if (!NT_SUCCESS(status)) {
        DBG("Couldn't add safe hook to main bus\n");
        IoDeleteDevice(first_hook);
        return;
      }
  }

WVL_M_LIB NTSTATUS STDCALL WvlCreateSafeHookDevice(
    IN S_X86_SEG16OFF16 * hook_addr,
    IN OUT DEVICE_OBJECT ** dev_obj
  ) {
    static const UCHAR sig[] = M_WV_SAFE_HOOK_SIGNATURE;
    UCHAR * phys_mem;
    NTSTATUS status;
    UINT32 hook_phys_addr;
    WV_S_PROBE_SAFE_MBR_HOOK * safe_mbr_hook;
    WV_S_DUMMY_IDS * dummy_ids;
    A_WV_SAFE_HOOK_VENDOR_STRING vendor;
    DEVICE_OBJECT * new_dev_obj;
    S_WVL_DUMMY_PDO * dummy;

    ASSERT(hook_addr);
    ASSERT(dev_obj);

    /* Map the first MiB of memory */
    phys_mem = WvlMapUnmapLowMemory(NULL);
    if (!phys_mem) {
        DBG("Could not map low memory\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_map;
      }

    /* Special-case the IVT entry for INT 0x13 */
    if (
        hook_addr->Segment == 0 &&
        hook_addr->Offset == (sizeof *hook_addr * 0x13)
      ) {
        hook_addr = (VOID *) (phys_mem + sizeof *hook_addr * 0x13);
      }

    hook_phys_addr = M_X86_SEG16OFF16_ADDR(hook_addr);
    DBG(
        "Probing for safe hook at %04X:%04X (0x%08X)...\n",
        hook_addr->Segment,
        hook_addr->Offset,
        hook_phys_addr
      );

    /* Check for the signature */
    safe_mbr_hook = (VOID *) (phys_mem + hook_phys_addr);
    if (!wv_memcmpeq(safe_mbr_hook->Signature, sig, sizeof sig - 1)) {
        DBG("Invalid safe INT 0x13 hook signature.  End of chain\n");
        status = STATUS_NOT_SUPPORTED;
        goto err_sig;
      }

    /* Found one */
    DBG("Found safe hook with vendor ID: %.8s\n", safe_mbr_hook->VendorId);

    /* Build the IDs */
    RtlCopyMemory(
        vendor,
        safe_mbr_hook->VendorId,
        sizeof safe_mbr_hook->VendorId
      );
    dummy_ids = WvSafeHookDummyIds(&vendor, hook_addr);
    if (!dummy_ids) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_dummy_ids;
      }

    new_dev_obj = NULL;
    status = WvDummyAdd(
        /* Mini-driver: Us */
        WvSafeHookMiniDriver,
        /* Dummy IDs */
        dummy_ids,
        /* Dummy IDs offset */
        CvWvSafeHookDummyIdsOffset,
        /* Extra data offset */
        CvWvSafeHookDummyIdsExtraDataOffset,
        /* Extra data size */
        sizeof *hook_addr,
        /* Device name: Not specified */
        NULL,
        /* Exclusive access? */
        FALSE,
        /* PDO pointer to populate */
        &new_dev_obj
      );
    if (!NT_SUCCESS(status))
      goto err_new_dev_obj;
    ASSERT(new_dev_obj);

    dummy = new_dev_obj->DeviceExtension;
    ASSERT(dummy);
    *((S_X86_SEG16OFF16 *) dummy->ExtraData) = *hook_addr;

    if (dev_obj)
      *dev_obj = new_dev_obj;

    status = STATUS_SUCCESS;
    goto out;

    IoDeleteDevice(new_dev_obj);
    err_new_dev_obj:

    out:

    wv_free(dummy_ids);
    err_dummy_ids:

    err_sig:

    WvlMapUnmapLowMemory(phys_mem);
    err_map:

    return status;
  }

WVL_M_LIB S_X86_SEG16OFF16 * STDCALL WvlGetSafeHook(
    IN DEVICE_OBJECT * dev_obj
  ) {
    WV_S_DEV_EXT * dev_ext;
    S_WVL_DUMMY_PDO * dummy;

    ASSERT(dev_obj);
    dev_ext = dev_obj->DeviceExtension;

    if (!dev_ext || dev_ext->MiniDriver != WvSafeHookMiniDriver)
      return NULL;

    /* Never call this function on an FDO.  We assume PDO */
    dummy = dev_obj->DeviceExtension;

    /* The dummy extra data should have the SEG16:OFF16 of the hook */
    ASSERT(dummy->ExtraData);
    return dummy->ExtraData;
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

static WV_S_DUMMY_IDS * WvSafeHookDummyIds(
    IN A_WV_SAFE_HOOK_VENDOR_STRING * vendor,
    IN S_X86_SEG16OFF16 * whence
  ) {
    UCHAR * ptr;
    WV_S_DUMMY_IDS * dummy_ids;
    WCHAR * dest;
    INT copied;
    A_WV_SAFE_HOOK_WIDE_VENDOR_STRING wide_vendor;

    /* Generate the safe hook vendor string */
    WvSafeHookVendorString(&wide_vendor, vendor);

    ptr = wv_malloc(CvWvSafeHookDummyIdsSize);
    if (!ptr)
      goto err_dummy_ids;

    /* Populate with defaults */
    dummy_ids = (VOID *) ptr;
    RtlCopyMemory(
        dummy_ids,
        WvSafeHookDefaultDummyIds,
        CvWvSafeHookDummyIdsSize
      );

    /* Generate the specific details */
    ptr += dummy_ids->Offset;

    /* The instance ID */
    dest = (VOID *) ptr;
    dest += dummy_ids->InstanceOffset;
    copied = swprintf(
        dest,
        M_WV_SAFE_HOOK_INSTANCE_ID,
        M_X86_SEG16OFF16_ADDR(whence)
      );
    if (copied <= 0)
      goto err_copied;

    /* The vendor ID */
    dest = (VOID *) ptr;
    dest += dummy_ids->HardwareOffset;
    copied = swprintf(dest, M_WV_SAFE_HOOK_VENDOR_ID, wide_vendor);
    if (copied <= 0)
      goto err_copied;

    /* The compatible IDs */
    dest = (VOID *) ptr;
    dest += dummy_ids->CompatOffset;
    copied = swprintf(dest, M_WV_SAFE_HOOK_VENDOR_ID, wide_vendor);
    if (copied <= 0)
      goto err_copied;

    return dummy_ids;

    err_copied:

    wv_free(dummy_ids);
    err_dummy_ids:

    return NULL;
  }

/**
 * Generate a wide-character, safe hook vendor string
 *
 * @param Destination
 *   The wide-character buffer to populate with the generated vendor string
 *
 * @param Source
 *   The ASCII vendor string source
 */
static VOID WvSafeHookVendorString(
    A_WV_SAFE_HOOK_WIDE_VENDOR_STRING * dest,
    A_WV_SAFE_HOOK_VENDOR_STRING * src
  ) {
    SIZE_T i;
    WCHAR c;
    BOOLEAN bad;

    /* Sanitize and copy */
    for (i = 0; i < WvlCountof(*dest) - 1; ++i) {
        c = 0[src][i];

        bad = (
            c <= L'\x20' ||
            c > L'\x7F' ||
            c == L'\x2C'
          );
        if (bad)
          c = L'_';

        0[dest][i] = c;
      }
    0[dest][i] = L'\0';
  }
