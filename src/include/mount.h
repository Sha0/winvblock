/**
 * Copyright 2006-2008, V.
 * Portions copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef WV_M_MOUNT_H_
#  define WV_M_MOUNT_H_

/**
 * @file
 *
 * Mount command and device control code header.
 */

#  define IOCTL_FILE_ATTACH             \
  CTL_CODE(                             \
    FILE_DEVICE_CONTROLLER,             \
    0x804,                              \
    METHOD_BUFFERED,                    \
    FILE_READ_DATA | FILE_WRITE_DATA    \
    )
#  define IOCTL_FILE_DETACH             \
  CTL_CODE(                             \
    FILE_DEVICE_CONTROLLER,             \
    0x805,                              \
    METHOD_BUFFERED,                    \
    FILE_READ_DATA | FILE_WRITE_DATA    \
    )

typedef struct WV_MOUNT_DISK {
    char type;
    int cylinders;
    int heads;
    int sectors;
  } WV_S_MOUNT_DISK, * WV_SP_MOUNT_DISK;

#endif  /* WV_M_MOUNT_H_ */
