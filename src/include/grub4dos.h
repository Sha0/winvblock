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
#ifndef M_GRUB4DOS_H_
#  define M_GRUB4DOS_H_

/****
 * @file
 *
 * GRUB4DOS disk specifics.
 */

/*** Macros. */
#define M_G4D_SLOTS 8

/*** Object types. */
typedef struct S_WV_G4D_DRIVE_MAPPING_
  S_WV_G4D_DRIVE_MAPPING,
  * SP_WV_G4D_DRIVE_MAPPING;
  
/*** Function declarations. */
extern VOID WvFilediskCreateG4dDisk(
    SP_WV_G4D_DRIVE_MAPPING,
    WVL_E_DISK_MEDIA_TYPE,
    UINT32
  );
extern VOID WvRamdiskCreateG4dDisk(
    SP_WV_G4D_DRIVE_MAPPING,
    WVL_E_DISK_MEDIA_TYPE,
    UINT32
  );

/*** Struct/union definitions. */

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

#endif  /* M_GRUB4DOS_H_ */
