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
#ifndef M_GRUB4DOS_H_
#  define M_GRUB4DOS_H_

/**
 * @file
 *
 * GRUB4DOS mini-driver
 */

/** Macros */
#define M_G4D_SLOTS 8

/** Object types */
typedef struct S_WV_G4D_DRIVE_MAPPING_
  S_WV_G4D_DRIVE_MAPPING,
  * SP_WV_G4D_DRIVE_MAPPING;
typedef struct S_WV_G4D_BUS S_WV_G4D_BUS;

/** Function declarations */
extern DEVICE_OBJECT * WvFilediskCreateG4dDisk(
    SP_WV_G4D_DRIVE_MAPPING,
    DEVICE_OBJECT * BusDeviceObject,
    WVL_E_DISK_MEDIA_TYPE,
    UINT32
  );
extern DEVICE_OBJECT * WvRamdiskCreateG4dDisk(
    SP_WV_G4D_DRIVE_MAPPING,
    DEVICE_OBJECT * BusDeviceObject,
    WVL_E_DISK_MEDIA_TYPE,
    UINT32
  );

/* From ../src/winvblock/grub4dos/g4dbus.c */

/**
 * Process a GRUB4DOS drive mapping slot and produce a PDO
 *
 * @param BusDeviceObject
 *   The intended parent bus device for the resulting PDO
 *
 * @param Slot
 *   Points to a G4D drive mapping slot near the G4D INT 0x13 hook
 *
 * @return
 *   A pointer to the resulting DEVICE_OBJECT, or a null pointer,
 *   upon failure
 */
extern DEVICE_OBJECT * WvG4dProcessSlot(
    IN DEVICE_OBJECT * BusDeviceObject,
    IN S_WV_G4D_DRIVE_MAPPING * Slot
  );

/* From ../src/winvblock/grub4dos/fdo.c */

/** FDO IRP dispatcher */
extern DRIVER_DISPATCH WvG4dIrpDispatch;

/** Struct/union type definitions */

/* From GRUB4DOS 0.4.4's stage2/shared.h */
struct S_WV_G4D_DRIVE_MAPPING_ {
    UCHAR SourceDrive;
    UCHAR DestDrive;
    UCHAR MaxHead;
    UCHAR MaxSector:6;
    UCHAR RestrictionX:1;
    UINT16 DestMaxCylinder:13;
    UINT16 SourceODD:1;
    UINT16 DestODD:1;
    UINT16 DestLBASupport:1;
    UCHAR DestMaxHead;
    UCHAR DestMaxSector:6;
    UCHAR RestrictionY:1;
    UCHAR InSituOption:1;
    UINT64 SectorStart;
    UINT64 SectorCount;
  };

/** A GRUB4DOS FDO device extension */
struct S_WV_G4D_BUS {
    /** This must be the first member of all extension types */
    WV_S_DEV_EXT DeviceExtension[1];

    /** Flags for state that must be accessed atomically */
    volatile LONG Flags;

    /** The GRUB4DOS bus's safe hook PDO (bottom of the device stack) */
    DEVICE_OBJECT * PhysicalDeviceObject;

    /**
     * PnP bus relations.
     * A GRUB4DOS FDO can have only 9 children, at most
     */
    DEVICE_RELATIONS BusRelations[1];
    DEVICE_OBJECT * Padding_[M_G4D_SLOTS];

    /** A copy of the drive mappings */
    S_WV_G4D_DRIVE_MAPPING DriveMappings[M_G4D_SLOTS];

    /** Tracks which of the 9 possible nodes need a PDO */
    USHORT NodesNeeded;

    /** A record of the previous INT 0x13 handler in the chain */
    S_X86_SEG16OFF16 PreviousInt13hHandler[1];
  };

#endif  /* M_GRUB4DOS_H_ */
