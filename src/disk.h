/**
 * Copyright 2006-2008, V.
 * Portions copyright (C) 2009 Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef _DISK_H
#  define _DISK_H

/**
 * @file
 *
 * Disk specifics
 *
 */

#  include "aoedisk.h"
#  include "ramdisk.h"

typedef struct _DISK_DISK
{
	PDEVICE_OBJECT Parent;
	PDRIVER_DEVICEEXTENSION Next;
	KEVENT SearchEvent;
	SEARCHSTATE SearchState;
	KSPIN_LOCK SpinLock;
	BOOLEAN BootDrive;
	BOOLEAN Unmount;
	ULONG DiskNumber;
	union
	{
		AOEDISK_AOEDISK AoE;
		RAMDISK_RAMDISK RAMDisk;
	};
	LONGLONG LBADiskSize;
	LONGLONG Cylinders;
	ULONG Heads;
	ULONG Sectors;
	ULONG SpecialFileCount;
} DISK_DISK,
*PDISK_DISK;

#endif													/* _DISK_H */
