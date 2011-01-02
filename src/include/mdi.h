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
#ifndef WV_M_MDI_H_
#  define WV_M_MDI_H_

/**
 * @file
 *
 * MEMDISK Information/Installation structure.
 */

#ifdef _MSC_VER
#  pragma pack(1)
#endif
struct WV_MDI_PATCH_AREA {
    UINT16 mdi_bytes;
    UCHAR mdi_version_minor;
    UCHAR mdi_version_major;
    UINT32 diskbuf;
    UINT32 disksize;
    UINT16 cmdline_off, cmdline_seg;

    UINT32 oldint13;
    UINT32 oldint15;

    UINT16 olddosmem;
    UCHAR bootloaderid;
    UCHAR _pad1;

    UINT16 dpt_ptr;
    /* End of the official MemDisk_Info */
    UCHAR driveshiftlimit;
    UCHAR _pad2;
    UINT16 _pad3;
    UINT16 memint1588;

    UINT16 cylinders;
    UINT16 heads;
    UINT32 sectors;

    UINT32 mem1mb;
    UINT32 mem16mb;

    UCHAR driveno;
    /* WinVBlock does not need anything more. */
  } __attribute__((__packed__));
typedef struct WV_MDI_PATCH_AREA WV_S_MDI_PATCH_AREA, * WV_SP_MDI_PATCH_AREA;
#ifdef _MSC_VER
#  pragma pack()
#endif

#ifdef _MSC_VER
#  pragma pack(1)
#endif
struct WV_MDI_MBFT {
    UCHAR Signature[4];	/* ("mBFT") */
    UINT32 Length;
    UCHAR Revision;
    UCHAR Checksum;
    UCHAR OEMID[6];
    UCHAR OEMTableID[8];
    UCHAR Reserved1[12];
    UINT32 SafeHook;
    WV_S_MDI_PATCH_AREA mdi;
  } __attribute__((__packed__));
typedef struct WV_MDI_MBFT WV_S_MDI_MBFT, * WV_SP_MDI_MBFT;
#ifdef _MSC_VER
#  pragma pack()
#endif

#endif  /* WV_M_MDI_H_ */
