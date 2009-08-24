/**
 * Copyright (C) 2009 Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 * For contact information, see http://winaoe.org/
 *
 * This file is part of WinAoE.
 *
 * WinAoE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinAoE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WinAoE.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _RAMDISK_H
#  define _RAMDISK_H

/**
 * @file
 *
 * RAM disk specifics
 *
 */

typedef struct _RAMDISK_RAMDISK
{
	UINT32 DiskBuf;
	UINT32 DiskSize;
} RAMDISK_RAMDISK,
 *PRAMDISK_RAMDISK;

#endif													/* _RAMDISK_H */
