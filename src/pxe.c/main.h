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

asm ( ".code16gcc" );
#ifndef _C_H
#  define _C_H

#  define MAXPACKETSIZE 1518
#  define SECTORSIZE 512
#  define PROMPTTIMEOUT (3 * 18.206)	// in ticks (18.206 ticks / sec)
#  define RECEIVETIMEOUT 2			// in ticks

#  define PNP_SIGNATURE 0x506e5024	// PnP$
#  define ROM_SIGNATURE 0xaa55

#  define AOE_PROTOCOL 0x88a2
#  define AOE_VERSION 1

typedef struct s_abft
{
	unsigned int signature;				// 0x54464261 (aBFT)
	unsigned int length;
	unsigned char revision;
	unsigned char checksum;
	unsigned char oemid[6];
	unsigned char oemtableid[8];
	unsigned char reserved1[12];
	unsigned short major;
	unsigned char minor;
	unsigned char reserved2;
	unsigned char clientmac[6];
	unsigned char reserved3[15];	// for alignment on segment
} __attribute__ ( ( __packed__ ) ) t_abft;

typedef struct s_pnp
{
	unsigned int signature;
	unsigned char version;
	unsigned char length;
	unsigned short control;
	unsigned char checksum;
	unsigned int flags;
	struct
	{
		unsigned short offset;
		unsigned short segment;
	} __attribute__ ( ( __packed__ ) ) RMentry;
	struct
	{
		unsigned short offset;
		unsigned int segment;
	} __attribute__ ( ( __packed__ ) ) PMentry;
	unsigned int identifier;
	unsigned short RMdatasegment;
	unsigned int PMdatasegment;
} __attribute__ ( ( __packed__ ) ) t_pnp;

typedef struct s_aoe
{
	unsigned char dst[6];
	unsigned char src[6];
	unsigned short protocol;

	unsigned int flags:4;
	unsigned int ver:4;
	unsigned char error;
	unsigned short major;
	unsigned char minor;
	unsigned char command;
	unsigned int tag;

	unsigned char aflags;
	union
	{
		unsigned char err;
		unsigned char feature;
	};
	unsigned char count;
	union
	{
		unsigned char cmd;
		unsigned char status;
	};

	unsigned char lba0;
	unsigned char lba1;
	unsigned char lba2;
	unsigned char lba3;
	unsigned char lba4;
	unsigned char lba5;
	unsigned short reserved;

	unsigned char data[2 * SECTORSIZE];
} __attribute__ ( ( __packed__ ) ) t_aoe;

typedef struct s_mbr
{
	unsigned char reserved[446];
	struct
	{
		unsigned char active;
		unsigned char starthead;
		unsigned startsector:6;
		unsigned startcylinderhigh:2;
		unsigned char startcylinderlow;
		unsigned char type;
		unsigned char endhead;
		unsigned endsector:6;
		unsigned endcylinderhigh:2;
		unsigned char endcylinderlow;
		unsigned int startlba;
		unsigned int size;
	} __attribute__ ( ( __packed__ ) ) partition[4];
	unsigned short magic;
} __attribute__ ( ( __packed__ ) ) t_mbr;

typedef struct s_INT13_EXTENDED_READWRITE
{
	unsigned char structsize;
	unsigned char reserved;
	unsigned short count;
	unsigned short bufferoffset;
	unsigned short buffersegment;
	unsigned long long lba;
} __attribute__ ( ( __packed__ ) ) t_INT13_EXTENDED_READWRITE;

typedef struct s_INT13_EXTENDED_GET_PARAMETERS
{
	unsigned short structsize;
	unsigned short flags;
	unsigned int cylinders;
	unsigned int heads;
	unsigned int sectors;
	unsigned long long disksize;
	unsigned short sectorsize;
} __attribute__ ( ( __packed__ ) ) t_INT13_EXTENDED_GET_PARAMETERS;

typedef struct s_INT13_CDROM_EMULATION
{
	unsigned char packetsize;
	unsigned char bootmediatype;
	unsigned char drivenumber;
	unsigned char controllernumber;
	unsigned int lbasize;
	unsigned short drivespecification;
	unsigned short cachesegment;
	unsigned short loadsegment;
	unsigned short sectorstoload;
	unsigned char cylinderslow;
	unsigned char sectors_cylindershigh;
	unsigned char heads;
} __attribute__ ( ( __packed__ ) ) t_INT13_CDROM_EMULATION;
#endif
