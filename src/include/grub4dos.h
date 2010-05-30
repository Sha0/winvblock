/**
 * Copyright (C) 2009, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef _grub4dos_h
#  define _grub4dos_h

/**
 * @file
 *
 * GRUB4DOS disk specifics
 *
 */

/* From GRUB4DOS 0.4.4's stage2/shared.h */
winvblock__def_struct ( grub4dos__drive_mapping )
{
  winvblock__uint8 SourceDrive;
  winvblock__uint8 DestDrive;
  winvblock__uint8 MaxHead;
  winvblock__uint8 MaxSector:6;
  winvblock__uint8 RestrictionX:1;
  winvblock__uint16 DestMaxCylinder:13;
  winvblock__uint16 SourceODD:1;
  winvblock__uint16 DestODD:1;
  winvblock__uint16 DestLBASupport:1;
  winvblock__uint8 DestMaxHead;
  winvblock__uint8 DestMaxSector:6;
  winvblock__uint8 RestrictionY:1;
  winvblock__uint8 InSituOption:1;
  winvblock__uint64 SectorStart;
  winvblock__uint64 SectorCount;
};

extern void ramdisk_grub4dos__find (
  void
 );
extern void filedisk_grub4dos__find (
  void
 );

#endif				/* _grub4dos_h */
