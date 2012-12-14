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
#ifndef M_MEMDISK_H_

/**
 * @file
 *
 * MEMDISK specifics
 */

/** Macros */
#define M_MEMDISK_H_

/** Object types */
typedef struct S_WV_MEMDISK_BUS S_WV_MEMDISK_BUS;

/** Function declarations */

extern VOID WvMemdiskFind(void);
extern BOOLEAN WvMemdiskProcessSafeHook(
    PUCHAR,
    WV_SP_PROBE_SAFE_MBR_HOOK
  );

/* From ../src/winvblock/memdisk/memdisk.c */

/**
 * Create a MEMDISK disk
 *
 * @param Parent
 *   The safe hook parent of the MEMDISK
 *
 * @param PhysicalDeviceObject
 *   Points to the DEVICE_OBJECT pointer to populate with a pointer to
 *   the created MEMDISK disk
 *
 * @return
 *   The status of the operation
 */
extern NTSTATUS WvMemdiskCreateDisk(
    IN DEVICE_OBJECT * Parent,
    IN DEVICE_OBJECT ** PhysicalDeviceObject
  );

/* From ../src/winvblock/memdisk/fdo.c */

/** FDO IRP dispatcher */
extern DRIVER_DISPATCH WvMemdiskIrpDispatch;

/** Struct/union type definitions */

/** A MEMDISK FDO device extension */
struct S_WV_MEMDISK_BUS {
    /** This must be the first member of all extension types */
    WV_S_DEV_EXT DeviceExtension[1];

    /** Flags for state that must be accessed atomically */
    volatile LONG Flags;

    /** The safe hook's PDO (bottom of the device stack) */
    DEVICE_OBJECT * PhysicalDeviceObject;

    /**
     * PnP bus relations.
     * A MEMDISK FDO can have only two children, at most
     */
    DEVICE_RELATIONS BusRelations[1];
    DEVICE_OBJECT * Padding_[1];

    /** A record of the previous INT 0x13 handler in the chain */
    S_X86_SEG16OFF16 PreviousInt13hHandler[1];
  };

#endif  /* M_MEMDISK_H_ */
