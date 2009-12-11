/*
 *   Copyright 2001-2009 H. Peter Anvin - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author: H. Peter Anvin
 *
 *   This file is part of MEMDISK, part of the Syslinux bootloader suite.
 *   Minor adaptation for WinVBlock by Shao Miller
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

#  ifdef _MSC_VER
#    pragma pack(1)
#  endif
typedef struct _MDI_PATCHAREA
{
	UINT16 mdi_bytes;
	winvblock__uint8 mdi_version_minor;
	winvblock__uint8 mdi_version_major;
	UINT32 diskbuf;
	UINT32 disksize;
	UINT16 cmdline_off,
	 cmdline_seg;

	UINT32 oldint13;
	UINT32 oldint15;

	UINT16 olddosmem;
	winvblock__uint8 bootloaderid;
	winvblock__uint8 _pad1;

	UINT16 dpt_ptr;
	/*
	 * End of the official MemDisk_Info 
	 */
	winvblock__uint8 driveshiftlimit;
	winvblock__uint8 _pad2;
	UINT16 _pad3;
	UINT16 memint1588;

	UINT16 cylinders;
	UINT16 heads;
	UINT32 sectors;

	UINT32 mem1mb;
	UINT32 mem16mb;

	winvblock__uint8 driveno;
	/*
	 * WinVBlock does not need anything more 
	 */
} __attribute__ ( ( __packed__ ) ) MDI_PATCHAREA, *PMDI_PATCHAREA;
#  ifdef _MSC_VER
#    pragma pack()
#  endif

#  ifdef _MSC_VER
#    pragma pack(1)
#  endif
typedef struct _MDI_MBFT
{
	winvblock__uint8 Signature[4];	/* ("mBFT") */
	UINT Length;
	winvblock__uint8 Revision;
	winvblock__uint8 Checksum;
	winvblock__uint8 OEMID[6];
	winvblock__uint8 OEMTableID[8];
	winvblock__uint8 Reserved1[12];
	UINT SafeHook;
	MDI_PATCHAREA MDI;
} __attribute__ ( ( __packed__ ) ) MDI_MBFT, *PMDI_MBFT;
#  ifdef _MSC_VER
#    pragma pack()
#  endif

#endif													/* _MDI_H */
