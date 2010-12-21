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
 * MEMDISK Information/Installation structure
 *
 */

#  ifdef _MSC_VER
#    pragma pack(1)
#  endif
winvblock__def_struct ( mdi__patch_area )
{
  winvblock__uint16 mdi_bytes;
  winvblock__uint8 mdi_version_minor;
  winvblock__uint8 mdi_version_major;
  winvblock__uint32 diskbuf;
  winvblock__uint32 disksize;
  winvblock__uint16 cmdline_off,
   cmdline_seg;

  winvblock__uint32 oldint13;
  winvblock__uint32 oldint15;

  winvblock__uint16 olddosmem;
  winvblock__uint8 bootloaderid;
  winvblock__uint8 _pad1;

  winvblock__uint16 dpt_ptr;
  /*
   * End of the official MemDisk_Info 
   */
  winvblock__uint8 driveshiftlimit;
  winvblock__uint8 _pad2;
  winvblock__uint16 _pad3;
  winvblock__uint16 memint1588;

  winvblock__uint16 cylinders;
  winvblock__uint16 heads;
  winvblock__uint32 sectors;

  winvblock__uint32 mem1mb;
  winvblock__uint32 mem16mb;

  winvblock__uint8 driveno;
  /*
   * WinVBlock does not need anything more 
   */
}

__attribute__ ( ( __packed__ ) );
#  ifdef _MSC_VER
#    pragma pack()
#  endif

#  ifdef _MSC_VER
#    pragma pack(1)
#  endif
winvblock__def_struct ( mdi__mbft )
{
  winvblock__uint8 Signature[4];	/* ("mBFT") */
  winvblock__uint32 Length;
  winvblock__uint8 Revision;
  winvblock__uint8 Checksum;
  winvblock__uint8 OEMID[6];
  winvblock__uint8 OEMTableID[8];
  winvblock__uint8 Reserved1[12];
  winvblock__uint32 SafeHook;
  mdi__patch_area mdi;
}

__attribute__ ( ( __packed__ ) );
#  ifdef _MSC_VER
#    pragma pack()
#  endif

#endif  /* WV_M_MDI_H_ */
