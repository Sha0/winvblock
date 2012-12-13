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
#ifndef WV_M_PROBE_H_
#  define WV_M_PROBE_H_

/**
 * @file
 *
 * "Safe INT 0x13 hook" mini-driver
 */

/** Object types */
typedef struct WV_PROBE_SAFE_MBR_HOOK
  WV_S_PROBE_SAFE_MBR_HOOK, * WV_SP_PROBE_SAFE_MBR_HOOK;
typedef struct S_WV_SAFEHOOK_PDO_ S_WV_SAFEHOOK_PDO, * SP_WV_SAFEHOOK_PDO;
typedef struct S_WV_SAFE_HOOK_BUS S_WV_SAFE_HOOK_BUS;

/** Function declarations */

/**
 * Create a "safe INT 0x13 hook" PDO
 *
 * @v HookAddr          The SEG16:OFF16 address for the hook
 * @v SafeHook          Points to the safe hook
 * @v ParentBus         The parent bus for the newly-created safe hook bus
 * @ret PDEVICE_OBJECT  The newly-create safe hook bus, or NULL upon failure
 */
extern PDEVICE_OBJECT STDCALL WvSafeHookPdoCreate(
    IN SP_X86_SEG16OFF16 HookAddr,
    IN WV_SP_PROBE_SAFE_MBR_HOOK SafeHook,
    IN PDEVICE_OBJECT ParentBus
  );

/**
 * Process a potential INT 0x13 "safe hook"
 *
 * @param HookAddress
 *   The SEG16:OFF16 address to probe for a safe hook
 *
 * @param DeviceObject
 *   Points to the DEVICE_OBJECT pointer to populate with a pointer to
 *   the created safe hook device
 *
 * @retval STATUS_SUCCESS
 * @retval STATUS_NOT_SUPPORTED
 *   The address does not appear to contain a safe hook
 * @retval STATUS_INSUFFICIENT_RESOURCES
 *   A device for the safe hook could not be created
 */
extern WVL_M_LIB NTSTATUS STDCALL WvlCreateSafeHookDevice(
    IN S_X86_SEG16OFF16 * HookAddress,
    IN OUT DEVICE_OBJECT ** DeviceObject
  );

/**
 * Check if a device is an INT 0x13 "safe hook"
 *
 * @param DeviceObject
 *   The device to check.  This device must have been created via
 *   WvlCreateDevice
 *
 * @retval NULL
 *   The device is not a safe hook
 * @return
 *   Otherwise, returns a pointer to the safe hook SEG16:OFF16
 */
extern WVL_M_LIB S_X86_SEG16OFF16 * STDCALL WvlGetSafeHook(
    IN DEVICE_OBJECT * DeviceObject
  );

/* From ../src/winvblock/safehook/fdo.c */

/** FDO IRP dispatcher */
DRIVER_DISPATCH WvSafeHookIrpDispatch;

/** Struct/union type definitions */

#ifdef _MSC_VER
#  pragma pack(1)
#endif
struct WV_PROBE_SAFE_MBR_HOOK {
    UCHAR Jump[3];
    UCHAR Signature[8];
    UCHAR VendorId[8];
    S_X86_SEG16OFF16 PrevHook;
    UINT32 Flags;
    UINT32 Mbft;     /* MEMDISK only. */
  } __attribute__((__packed__));
#ifdef _MSC_VER
#  pragma pack()
#endif

struct S_WV_SAFEHOOK_PDO_ {
    WV_S_DEV_EXT DevExt;
    WV_S_PROBE_SAFE_MBR_HOOK SafeHookCopy;
    S_X86_SEG16OFF16 Whence;
    DEVICE_OBJECT * ParentBus;
    DEVICE_OBJECT * Self;
    S_WV_SAFEHOOK_PDO * Next;
  };

struct S_WV_SAFE_HOOK_BUS {
    /** This must be the first member of all extension types */
    WV_S_DEV_EXT DeviceExtension[1];

    /** Flags for state that must be accessed atomically */
    volatile LONG Flags;

    /** The safe hook's PDO (bottom of the device stack) */
    DEVICE_OBJECT * PhysicalDeviceObject;

    /** PnP bus relations.  A safe hook can only have one child, at most */
    DEVICE_RELATIONS BusRelations[1];

    /** A record of the previous INT 0x13 handler in the chain */
    S_X86_SEG16OFF16 PreviousInt13hHandler[1];
  };

#endif  /* WV_M_PROBE_H_ */
