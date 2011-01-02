/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 * Copyright 2006-2008, V.
 * For WinAoE contact information, see http://winaoe.org/
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
#ifndef AOE_M_AOE_H_
#  define AOE_M_AOE_H_

/**
 * @file
 *
 * AoE specifics.
 */

#  define htons(x) \
  (winvblock__uint16)((((x) << 8) & 0xff00) | (((x) >> 8) & 0xff))
#  define ntohs(x) \
  (winvblock__uint16)((((x) << 8) & 0xff00) | (((x) >> 8) & 0xff))

#  define IOCTL_AOE_SCAN                \
CTL_CODE(                               \
    FILE_DEVICE_CONTROLLER,             \
    0x800,                              \
    METHOD_BUFFERED,                    \
    FILE_READ_DATA | FILE_WRITE_DATA    \
  )
#  define IOCTL_AOE_SHOW                \
CTL_CODE(                               \
    FILE_DEVICE_CONTROLLER,             \
    0x801,                              \
    METHOD_BUFFERED,                    \
    FILE_READ_DATA | FILE_WRITE_DATA    \
  )
#  define IOCTL_AOE_MOUNT               \
CTL_CODE(                               \
    FILE_DEVICE_CONTROLLER,             \
    0x802,                              \
    METHOD_BUFFERED,                    \
    FILE_READ_DATA | FILE_WRITE_DATA    \
  )
#  define IOCTL_AOE_UMOUNT              \
CTL_CODE(                               \
    FILE_DEVICE_CONTROLLER,             \
    0x803,                              \
    METHOD_BUFFERED,                    \
    FILE_READ_DATA | FILE_WRITE_DATA    \
  )

typedef struct AOE_MOUNT_TARGET {
    winvblock__uint8 ClientMac[6];
    winvblock__uint8 ServerMac[6];
    winvblock__uint32 Major;
    winvblock__uint32 Minor;
    LONGLONG LBASize;
    LARGE_INTEGER ProbeTime;
  } AOE_S_MOUNT_TARGET, * AOE_SP_MOUNT_TARGET;

typedef struct AOE_MOUNT_TARGETS {
    winvblock__uint32 Count;
    AOE_S_MOUNT_TARGET Target[];
  } AOE_S_MOUNT_TARGETS, * AOE_SP_MOUNT_TARGETS;

winvblock__def_struct(aoe__mount_disk)
  {
    winvblock__uint32 Disk;
    winvblock__uint8 ClientMac[6];
    winvblock__uint8 ServerMac[6];
    winvblock__uint32 Major;
    winvblock__uint32 Minor;
    LONGLONG LBASize;
  };

winvblock__def_struct(aoe__mount_disks)
  {
    winvblock__uint32 Count;
    aoe__mount_disk Disk[];
  };

extern void aoe__reset_probe(void);

#endif  /* AOE_M_AOE_H_ */
