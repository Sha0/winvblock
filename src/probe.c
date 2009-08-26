/**
 * Copyright 2006-2008, V.
 * Portions copyright (C) 2009 Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 * For contact information, see http://winaoe.org/
 *
 * This file is part of WinAoE.
 *
 * WinAoE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinAoE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WinAoE.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Boot-time disk probing specifics
 *
 */

#include "portable.h"
#include <ntddk.h>
#include "driver.h"
#include "debug.h"
#include "mdi.h"
#include "bus.h"

#ifdef _MSC_VER
#  pragma pack(1)
#endif
typedef struct _BUS_ABFT
{
	UINT Signature;								/* 0x54464261 (aBFT) */
	UINT Length;
	UCHAR Revision;
	UCHAR Checksum;
	UCHAR OEMID[6];
	UCHAR OEMTableID[8];
	UCHAR Reserved1[12];
	USHORT Major;
	UCHAR Minor;
	UCHAR Reserved2;
	UCHAR ClientMac[6];
} __attribute__ ( ( __packed__ ) ) BUS_ABFT, *PBUS_ABFT;
#ifdef _MSC_VER
#  pragma pack()
#endif

MDI_PATCHAREA Probe_Globals_BootMemdisk;

VOID STDCALL
Probe_MemDisk (
	PDEVICE_OBJECT BusDeviceObject
 )
{
	PHYSICAL_ADDRESS PhysicalAddress;
	PUCHAR PhysicalMemory;
	UINT16 RealSeg,
	 RealOff;
	UINT32 Int13Hook;
	PMDI_PATCHAREA MemdiskPtr;
	UINT Offset;
	BOOLEAN FoundMemdisk = FALSE;
	PDRIVER_DEVICEEXTENSION BusDeviceExtension =
		( PDRIVER_DEVICEEXTENSION ) BusDeviceObject->DeviceExtension;

	/*
	 * Find a MEMDISK.  Start by looking at the IDT and following
	 * the INT 0x13 hook.  This discovery strategy is extremely poor
	 * at the moment.  The eventual goal is to discover MEMDISK RAM disks
	 * as well as GRUB4DOS-mapped RAM disks.  Slight modifications to both
	 * will help with this.
	 */
	PhysicalAddress.QuadPart = 0LL;
	PhysicalMemory = MmMapIoSpace ( PhysicalAddress, 0x100000, MmNonCached );
	if ( !PhysicalMemory )
		{
			DBG ( "Could not map low memory\n" );
		}
	else
		{
			RealSeg = *( ( PUINT16 ) & PhysicalMemory[0x13 * 4 + 2] );
			RealOff = *( ( PUINT16 ) & PhysicalMemory[0x13 * 4] );
			Int13Hook = ( ( ( UINT32 ) RealSeg ) << 4 ) + ( ( UINT32 ) RealOff );
			DBG ( "INT 0x13 Segment: 0x%04x\n", RealSeg );
			DBG ( "INT 0x13 Offset: 0x%04x\n", RealOff );
			DBG ( "INT 0x13 Hook: 0x%08x\n", Int13Hook );
			for ( Offset = 0; Offset < 0x100000 - sizeof ( MDI_PATCHAREA );
						Offset += 2 )
				{
					MemdiskPtr = ( PMDI_PATCHAREA ) & PhysicalMemory[Offset];
					if ( MemdiskPtr->mdi_bytes != 0x1e )
						continue;
					DBG ( "Found MEMDISK sig #1: 0x%08x\n", Offset );
					if ( ( MemdiskPtr->mdi_version_major != 3 )
							 || ( MemdiskPtr->mdi_version_minor < 0x50 )
							 || ( MemdiskPtr->mdi_version_minor > 0x53 ) )
						continue;
					DBG ( "Found MEMDISK sig #2\n" );
					DBG ( "MEMDISK DiskBuf: 0x%08x\n", MemdiskPtr->diskbuf );
					DBG ( "MEMDISK DiskSize: 0x%08x sectors\n", MemdiskPtr->disksize );
					FoundMemdisk = TRUE;
					RtlCopyMemory ( &Probe_Globals_BootMemdisk, MemdiskPtr,
													sizeof ( MDI_PATCHAREA ) );
					break;
				}
			MmUnmapIoSpace ( PhysicalMemory, 0x100000 );
		}
	if ( FoundMemdisk )
		{
			if ( !Bus_AddChild ( BusDeviceObject, NULL, 0, 0, TRUE ) )
				DBG ( "Bus_AddChild() failed for MEMDISK\n" );
			else if ( BusDeviceExtension->Bus.PhysicalDeviceObject != NULL )
				{
					IoInvalidateDeviceRelations ( BusDeviceExtension->Bus.
																				PhysicalDeviceObject, BusRelations );
				}
		}
	else
		{
			DBG ( "Not booting from MEMDISK...\n" );
		}
}

VOID STDCALL
Probe_AoE (
	PDEVICE_OBJECT BusDeviceObject
 )
{
	PHYSICAL_ADDRESS PhysicalAddress;
	PUCHAR PhysicalMemory;
	UINT Offset,
	 Checksum,
	 i;
	BOOLEAN FoundAbft = FALSE;
	PDRIVER_DEVICEEXTENSION BusDeviceExtension =
		( PDRIVER_DEVICEEXTENSION ) BusDeviceObject->DeviceExtension;
	BUS_ABFT AOEBootRecord;

	/*
	 * Find aBFT
	 */
	PhysicalAddress.QuadPart = 0LL;
	PhysicalMemory = MmMapIoSpace ( PhysicalAddress, 0xa0000, MmNonCached );
	if ( !PhysicalMemory )
		{
			DBG ( "Could not map low memory\n" );
		}
	else
		{
			for ( Offset = 0; Offset < 0xa0000; Offset += 0x10 )
				{
					if ( ( ( PBUS_ABFT ) & PhysicalMemory[Offset] )->Signature ==
							 0x54464261 )
						{
							Checksum = 0;
							for ( i = 0;
										i < ( ( PBUS_ABFT ) & PhysicalMemory[Offset] )->Length;
										i++ )
								Checksum += PhysicalMemory[Offset + i];
							if ( Checksum & 0xff )
								continue;
							if ( ( ( PBUS_ABFT ) & PhysicalMemory[Offset] )->Revision != 1 )
								{
									DBG ( "Found aBFT with mismatched revision v%d at "
												"segment 0x%4x. want v1.\n",
												( ( PBUS_ABFT ) & PhysicalMemory[Offset] )->Revision,
												( Offset / 0x10 ) );
									continue;
								}
							DBG ( "Found aBFT at segment: 0x%04x\n", ( Offset / 0x10 ) );
							RtlCopyMemory ( &AOEBootRecord, &PhysicalMemory[Offset],
															sizeof ( BUS_ABFT ) );
							FoundAbft = TRUE;
							break;
						}
				}
			MmUnmapIoSpace ( PhysicalMemory, 0xa0000 );
		}

#ifdef RIS
	FoundAbft = TRUE;
	RtlCopyMemory ( AOEBootRecord.ClientMac, "\x00\x0c\x29\x34\x69\x34", 6 );
	AOEBootRecord.Major = 0;
	AOEBootRecord.Minor = 10;
#endif

	if ( FoundAbft )
		{
			DBG ( "Boot from client NIC %02x:%02x:%02x:%02x:%02x:%02x to major: "
						"%d minor: %d\n", AOEBootRecord.ClientMac[0],
						AOEBootRecord.ClientMac[1], AOEBootRecord.ClientMac[2],
						AOEBootRecord.ClientMac[3], AOEBootRecord.ClientMac[4],
						AOEBootRecord.ClientMac[5], AOEBootRecord.Major,
						AOEBootRecord.Minor );
			if ( !Bus_AddChild
					 ( BusDeviceObject, AOEBootRecord.ClientMac, AOEBootRecord.Major,
						 AOEBootRecord.Minor, TRUE ) )
				DBG ( "Bus_AddChild() failed for aBFT AoE disk\n" );
			else
				{
					if ( BusDeviceExtension->Bus.PhysicalDeviceObject != NULL )
						{
							IoInvalidateDeviceRelations ( BusDeviceExtension->Bus.
																						PhysicalDeviceObject,
																						BusRelations );
						}
				}
		}
	else
		{
			DBG ( "Not booting from aBFT AoE disk...\n" );
		}
}
