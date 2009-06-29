/*
  Copyright 2006-2008, V.
  For contact information, see http://winaoe.org/

  This file is part of WinAoE.

  WinAoE is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  WinAoE is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with WinAoE.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _MOUNT_H
#define _MOUNT_H
#include "portable.h"

#define IOCTL_AOE_SCAN CTL_CODE(FILE_DEVICE_CONTROLLER, 0x800, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)
#define IOCTL_AOE_SHOW CTL_CODE(FILE_DEVICE_CONTROLLER, 0x801, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)
#define IOCTL_AOE_MOUNT CTL_CODE(FILE_DEVICE_CONTROLLER, 0x802, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)
#define IOCTL_AOE_UMOUNT CTL_CODE(FILE_DEVICE_CONTROLLER, 0x803, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

typedef struct _TARGET {
  UCHAR ClientMac[6];
  UCHAR ServerMac[6];
  ULONG Major;
  ULONG Minor;
  LONGLONG LBASize;
  LARGE_INTEGER ProbeTime;
} TARGET, *PTARGET;

typedef struct _TARGETS {
  ULONG Count;
  TARGET Target[];
} TARGETS, *PTARGETS;

typedef struct _DISK {
  ULONG Disk;
  UCHAR ClientMac[6];
  UCHAR ServerMac[6];
  ULONG Major;
  ULONG Minor;
  LONGLONG LBASize;
} DISK, *PDISK;

typedef struct _DISKS {
  ULONG Count;
  DISK Disk[];
} DISKS, *PDISKS;

#endif
