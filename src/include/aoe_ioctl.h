/**
 * Copyright (C) 2009-2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef _AOE_IOCTL_H
#  define _AOE_IOCTL_H

/**
 * @file
 *
 * AoE IOCTL specifics
 *
 */

#  define IOCTL_AOE_SCAN                \
  CTL_CODE(                             \
    FILE_DEVICE_CONTROLLER,             \
    0x800,                              \
    METHOD_BUFFERED,                    \
    FILE_READ_DATA | FILE_WRITE_DATA    \
    )
#  define IOCTL_AOE_SHOW                \
  CTL_CODE(                             \
    FILE_DEVICE_CONTROLLER,             \
    0x801,                              \
    METHOD_BUFFERED,                    \
    FILE_READ_DATA | FILE_WRITE_DATA    \
    )
#  define IOCTL_AOE_MOUNT               \
  CTL_CODE(                             \
    FILE_DEVICE_CONTROLLER,             \
    0x802,                              \
    METHOD_BUFFERED,                    \
    FILE_READ_DATA | FILE_WRITE_DATA    \
    )
#  define IOCTL_AOE_UMOUNT              \
  CTL_CODE(                             \
    FILE_DEVICE_CONTROLLER,             \
    0x803,                              \
    METHOD_BUFFERED,                    \
    FILE_READ_DATA | FILE_WRITE_DATA    \
    )

#endif				/* _AOE_IOCTL_H */
