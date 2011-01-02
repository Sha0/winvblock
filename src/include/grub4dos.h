/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef WV_M_GRUB4DOS_H_
#  define WV_M_GRUB4DOS_H_

/**
 * @file
 *
 * GRUB4DOS disk specifics.
 */

/* From GRUB4DOS 0.4.4's stage2/shared.h */
typedef struct WV_GRUB4DOS_DRIVE_MAPPING {
    UCHAR SourceDrive;
    UCHAR DestDrive;
    UCHAR MaxHead;
    UCHAR MaxSector:6;
    UCHAR RestrictionX:1;
    winvblock__uint16 DestMaxCylinder:13;
    winvblock__uint16 SourceODD:1;
    winvblock__uint16 DestODD:1;
    winvblock__uint16 DestLBASupport:1;
    UCHAR DestMaxHead;
    UCHAR DestMaxSector:6;
    UCHAR RestrictionY:1;
    UCHAR InSituOption:1;
    winvblock__uint64 SectorStart;
    winvblock__uint64 SectorCount;
  } WV_S_GRUB4DOS_DRIVE_MAPPING, * WV_SP_GRUB4DOS_DRIVE_MAPPING;

extern void ramdisk_grub4dos__find (
  void
 );
extern void filedisk_grub4dos__find (
  void
 );

#endif  /* WV_M_GRUB4DOS_H_ */
