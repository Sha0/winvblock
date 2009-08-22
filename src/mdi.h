/*
 *   Copyright 2001-2009 H. Peter Anvin - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author: H. Peter Anvin
 *
 *   This file is part of MEMDISK, part of the Syslinux bootloader suite.
 *   Minor adaptation for WinAoE by Shao Miller
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 */
#ifndef _MDI_H
#  define _MDI_H

/**
 * @file
 *
 * MEMDISK Information/Installation structure
 *
 */

typedef struct _MDI_PATCHAREA
{
	UINT16 mdi_bytes;
	UINT8 mdi_version_minor;
	UINT8 mdi_version_major;
	UINT32 diskbuf;
	UINT32 disksize;
	UINT16 cmdline_off,
	 cmdline_seg;

	UINT32 oldint13;
	UINT32 oldint15;

	UINT16 olddosmem;
	UINT8 bootloaderid;
	UINT8 _pad1;

	UINT16 dpt_ptr;
	/*
	 * End of the official MemDisk_Info 
	 */
	UINT16 memint1588;

	UINT16 cylinders;
	UINT16 heads;
	UINT32 sectors;
	/*
	 * WinAoE does not need anything more 
	 */
} MDI_PATCHAREA,
*PMDI_PATCHAREA;

#endif													/* _MDI_H */
