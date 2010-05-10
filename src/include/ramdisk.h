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
#ifndef _ramdisk_h
#  define _ramdisk_h

/**
 * @file
 *
 * RAM disk specifics
 *
 */

winvblock__def_struct ( ramdisk__type )
{
  disk__type disk;
  winvblock__uint32 DiskBuf;
  winvblock__uint32 DiskSize;
};

extern disk__ops ramdisk__default_ops;

/*
 * Establish a pointer into the RAM disk device's extension space.
 * Since the device extension is the first member of the disk
 * member of an AoE disk, and the disk structure is itself the
 * first member of an AoE disk structure, a simple cast will suffice
 */
#  define ramdisk__get_ptr( dev_ext_ptr ) \
  ( ( ramdisk__type_ptr ) dev_ext_ptr )

#endif				/* _ramdisk_h */
