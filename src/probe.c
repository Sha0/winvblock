/**
 * Copyright (C) 2009, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#include "aoe.h"

typedef struct _PROBE_INT_VECTOR
{
	USHORT Offset;
	USHORT Segment;
} PROBE_INT_VECTOR,
*PPROBE_INT_VECTOR;

#ifdef _MSC_VER
#  pragma pack(1)
#endif
typedef struct _PROBE_ABFT
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
} __attribute__ ( ( __packed__ ) ) PROBE_ABFT, *PPROBE_ABFT;
#ifdef _MSC_VER
#  pragma pack()
#endif

#ifdef _MSC_VER
#  pragma pack(1)
#endif
typedef struct _PROBE_SAFEMBRHOOK
{
	UCHAR Jump[3];
	UCHAR Signature[8];
	UCHAR VendorID[8];
	PROBE_INT_VECTOR PrevHook;
	UINT Flags;
	UINT mBFT;										/* MEMDISK only */
} __attribute__ ( ( __packed__ ) ) PROBE_SAFEMBRHOOK, *PPROBE_SAFEMBRHOOK;
#ifdef _MSC_VER
#  pragma pack()
#endif

/* From GRUB4DOS 0.4.4's stage2/shared.h */
typedef struct _PROBE_GRUB4DOSDRIVEMAPSLOT
{
	UCHAR SourceDrive;
	UCHAR DestDrive;
	UCHAR MaxHead;
	UCHAR MaxSector:6;
	UCHAR RestrictionX:1;
	UINT8 DestMaxCylinder;
	UCHAR DestMaxHead;
	UCHAR DestMaxSector:6;
	UCHAR ResitrctionY:1;
	UCHAR InSituOption:1;
	UINT64 SectorStart;
	UINT64 SectorCount;
} PROBE_GRUB4DOSDRIVEMAPSLOT,
*PPROBE_GRUB4DOSDRIVEMAPSLOT;

static BOOLEAN STDCALL
Probe_NoInitialize (
	IN PDRIVER_DEVICEEXTENSION DeviceExtension
 )
{
	return TRUE;
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
	PROBE_ABFT AoEBootRecord;
	DISK_DISK Disk;

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
					if ( ( ( PPROBE_ABFT ) & PhysicalMemory[Offset] )->Signature ==
							 0x54464261 )
						{
							Checksum = 0;
							for ( i = 0;
										i < ( ( PPROBE_ABFT ) & PhysicalMemory[Offset] )->Length;
										i++ )
								Checksum += PhysicalMemory[Offset + i];
							if ( Checksum & 0xff )
								continue;
							if ( ( ( PPROBE_ABFT ) & PhysicalMemory[Offset] )->Revision !=
									 1 )
								{
									DBG ( "Found aBFT with mismatched revision v%d at "
												"segment 0x%4x. want v1.\n",
												( ( PPROBE_ABFT ) & PhysicalMemory[Offset] )->Revision,
												( Offset / 0x10 ) );
									continue;
								}
							DBG ( "Found aBFT at segment: 0x%04x\n", ( Offset / 0x10 ) );
							RtlCopyMemory ( &AoEBootRecord, &PhysicalMemory[Offset],
															sizeof ( PROBE_ABFT ) );
							FoundAbft = TRUE;
							break;
						}
				}
			MmUnmapIoSpace ( PhysicalMemory, 0xa0000 );
		}

#ifdef RIS
	FoundAbft = TRUE;
	RtlCopyMemory ( AoEBootRecord.ClientMac, "\x00\x0c\x29\x34\x69\x34", 6 );
	AoEBootRecord.Major = 0;
	AoEBootRecord.Minor = 10;
#endif

	if ( FoundAbft )
		{
			DBG ( "Attaching AoE disk from client NIC "
						"%02x:%02x:%02x:%02x:%02x:%02x to major: %d minor: %d\n",
						AoEBootRecord.ClientMac[0], AoEBootRecord.ClientMac[1],
						AoEBootRecord.ClientMac[2], AoEBootRecord.ClientMac[3],
						AoEBootRecord.ClientMac[4], AoEBootRecord.ClientMac[5],
						AoEBootRecord.Major, AoEBootRecord.Minor );
			Disk.Initialize = AoE_SearchDrive;
			RtlCopyMemory ( Disk.AoE.ClientMac, AoEBootRecord.ClientMac, 6 );
			RtlFillMemory ( Disk.AoE.ServerMac, 6, 0xff );
			Disk.AoE.Major = AoEBootRecord.Major;
			Disk.AoE.Minor = AoEBootRecord.Minor;
			Disk.AoE.MaxSectorsPerPacket = 1;
			Disk.AoE.Timeout = 200000;	/* 20 ms. */
			Disk.IsRamdisk = FALSE;

			if ( !Bus_AddChild ( BusDeviceObject, Disk, TRUE ) )
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
			DBG ( "No aBFT found\n" );
		}
}

static PPROBE_SAFEMBRHOOK STDCALL
Probe_GetSafeHook (
	IN PUCHAR PhysicalMemory,
	IN PPROBE_INT_VECTOR InterruptVector
 )
{
	UINT32 Int13Hook;
	PPROBE_SAFEMBRHOOK SafeMbrHookPtr;
	UCHAR Signature[9] = { 0 };
	UCHAR VendorID[9] = { 0 };

	Int13Hook =
		( ( ( UINT32 ) InterruptVector->Segment ) << 4 ) +
		( ( UINT32 ) InterruptVector->Offset );
	SafeMbrHookPtr = ( PPROBE_SAFEMBRHOOK ) ( PhysicalMemory + Int13Hook );
	RtlCopyMemory ( Signature, SafeMbrHookPtr->Signature, 8 );
	RtlCopyMemory ( VendorID, SafeMbrHookPtr->VendorID, 8 );
	DBG ( "INT 0x13 Segment: 0x%04x\n", InterruptVector->Segment );
	DBG ( "INT 0x13 Offset: 0x%04x\n", InterruptVector->Offset );
	DBG ( "INT 0x13 Hook: 0x%08x\n", Int13Hook );
	DBG ( "INT 0x13 Safe Hook Signature: %s\n", Signature );
	if ( !( RtlCompareMemory ( Signature, "$INT13SF", 8 ) == 8 ) )
		{
			DBG ( "Invalid INT 0x13 Safe Hook Signature; End of chain\n" );
			return NULL;
		}
	return SafeMbrHookPtr;
}

BOOLEAN STDCALL
Probe_MemDisk_mBFT (
	PDEVICE_OBJECT BusDeviceObject,
	PUCHAR PhysicalMemory,
	UINT Offset
 )
{
	PMDI_MBFT mBFT = ( PMDI_MBFT ) ( PhysicalMemory + Offset );
	UINT i;
	UCHAR Checksum = 0;
	PPROBE_SAFEMBRHOOK AssociatedHook;
	DISK_DISK Disk;
	PDRIVER_DEVICEEXTENSION BusDeviceExtension =
		( PDRIVER_DEVICEEXTENSION ) BusDeviceObject->DeviceExtension;

	if ( Offset >= 0x100000 )
		{
			DBG ( "mBFT physical pointer too high!\n" );
			return FALSE;
		}
	if ( !( RtlCompareMemory ( mBFT->Signature, "mBFT", 4 ) == 4 ) )
		return FALSE;
	if ( Offset + mBFT->Length >= 0x100000 )
		{
			DBG ( "mBFT length out-of-bounds\n" );
			return FALSE;
		}
	for ( i = 0; i < mBFT->Length; i++ )
		Checksum += ( ( UCHAR * ) mBFT )[i];
	if ( Checksum )
		{
			DBG ( "Invalid mBFT checksum\n" );
			return FALSE;
		}
	DBG ( "Found mBFT: 0x%08x\n", mBFT );
	if ( mBFT->SafeHook >= 0x100000 )
		{
			DBG ( "mBFT safe hook physical pointer too high!\n" );
			return FALSE;
		}
	AssociatedHook = ( PPROBE_SAFEMBRHOOK ) ( PhysicalMemory + mBFT->SafeHook );
	if ( AssociatedHook->Flags )
		{
			DBG ( "This MEMDISK already processed\n" );
			return TRUE;
		}
	DBG ( "MEMDISK DiskBuf: 0x%08x\n", mBFT->MDI.diskbuf );
	DBG ( "MEMDISK DiskSize: %d sectors\n", mBFT->MDI.disksize );
	Disk.Initialize = Probe_NoInitialize;
	Disk.RAMDisk.DiskBuf = mBFT->MDI.diskbuf;
	Disk.LBADiskSize = Disk.RAMDisk.DiskSize = mBFT->MDI.disksize;
	Disk.IsRamdisk = TRUE;
	if ( !Bus_AddChild ( BusDeviceObject, Disk, TRUE ) )
		{
			DBG ( "Bus_AddChild() failed for MEMDISK\n" );
		}
	else if ( BusDeviceExtension->Bus.PhysicalDeviceObject != NULL )
		{
			IoInvalidateDeviceRelations ( BusDeviceExtension->Bus.
																		PhysicalDeviceObject, BusRelations );
		}
	AssociatedHook->Flags = 1;
	return TRUE;
}

VOID STDCALL
Probe_MemDisk (
	PDEVICE_OBJECT BusDeviceObject
 )
{
	PHYSICAL_ADDRESS PhysicalAddress;
	PUCHAR PhysicalMemory;
	PPROBE_INT_VECTOR InterruptVector;
	PPROBE_SAFEMBRHOOK SafeMbrHookPtr;
	UINT Offset;
	BOOLEAN FoundMemdisk = FALSE;

	/*
	 * Find a MEMDISK.  We check the "safe hook" chain, then scan for mBFTs
	 */
	PhysicalAddress.QuadPart = 0LL;
	PhysicalMemory = MmMapIoSpace ( PhysicalAddress, 0x100000, MmNonCached );
	if ( !PhysicalMemory )
		{
			DBG ( "Could not map low memory\n" );
			return;
		}
	InterruptVector =
		( PPROBE_INT_VECTOR ) ( PhysicalMemory +
														0x13 * sizeof ( PROBE_INT_VECTOR ) );
	/*
	 * Walk the "safe hook" chain of INT 13h hooks as far as possible
	 */
	while ( SafeMbrHookPtr =
					Probe_GetSafeHook ( PhysicalMemory, InterruptVector ) )
		{
			if ( !
					 ( RtlCompareMemory ( SafeMbrHookPtr->VendorID, "MEMDISK ", 8 ) ==
						 8 ) )
				{
					DBG ( "Non-MEMDISK INT 0x13 Safe Hook\n" );
				}
			else
				{
					FoundMemdisk |=
						Probe_MemDisk_mBFT ( BusDeviceObject, PhysicalMemory,
																 SafeMbrHookPtr->mBFT );
				}
			InterruptVector = &SafeMbrHookPtr->PrevHook;
		}
	/*
	 * Now search for "floating" mBFTs
	 */
	for ( Offset = 0; Offset < 0xFFFF0; Offset += 0x10 )
		{
			FoundMemdisk |=
				Probe_MemDisk_mBFT ( BusDeviceObject, PhysicalMemory, Offset );
		}
	MmUnmapIoSpace ( PhysicalMemory, 0x100000 );
	if ( !FoundMemdisk )
		{
			DBG ( "No MEMDISKs found\n" );
		}
}

VOID STDCALL
Probe_Grub4Dos (
	PDEVICE_OBJECT BusDeviceObject
 )
{
	PHYSICAL_ADDRESS PhysicalAddress;
	PUCHAR PhysicalMemory;
	PPROBE_INT_VECTOR InterruptVector;
	UINT32 Int13Hook;
	PPROBE_SAFEMBRHOOK SafeMbrHookPtr;
	UCHAR Signature[9] = { 0 };
	UCHAR VendorID[9] = { 0 };
	PPROBE_GRUB4DOSDRIVEMAPSLOT Grub4DosDriveMapSlotPtr;
	UINT Offset;
	BOOLEAN FoundGrub4DosMapping = FALSE;
	PDRIVER_DEVICEEXTENSION BusDeviceExtension =
		( PDRIVER_DEVICEEXTENSION ) BusDeviceObject->DeviceExtension;
	DISK_DISK Disk;

	/*
	 * Find a GRUB4DOS memory-mapped disk.  Start by looking at the
	 * real-mode IDT and following the "SafeMBRHook" INT 0x13 hook
	 */
	PhysicalAddress.QuadPart = 0LL;
	PhysicalMemory = MmMapIoSpace ( PhysicalAddress, 0x100000, MmNonCached );
	if ( !PhysicalMemory )
		{
			DBG ( "Could not map low memory\n" );
			return;
		}
	InterruptVector =
		( PPROBE_INT_VECTOR ) ( PhysicalMemory +
														0x13 * sizeof ( PROBE_INT_VECTOR ) );
	Int13Hook =
		( ( ( UINT32 ) InterruptVector->Segment ) << 4 ) +
		( ( UINT32 ) InterruptVector->Offset );
	SafeMbrHookPtr = ( PPROBE_SAFEMBRHOOK ) ( PhysicalMemory + Int13Hook );
	RtlCopyMemory ( Signature, SafeMbrHookPtr->Signature, 8 );
	DBG ( "INT 0x13 Segment: 0x%04x\n", InterruptVector->Segment );
	DBG ( "INT 0x13 Offset: 0x%04x\n", InterruptVector->Offset );
	DBG ( "INT 0x13 Hook: 0x%08x\n", Int13Hook );
	DBG ( "INT 0x13 Safety Hook Signature: %s\n", Signature );
	if ( !( RtlCompareMemory ( Signature, "$INT13SF", 8 ) == 8 ) )
		{
			DBG ( "Invalid INT 0x13 Safety Hook Signature\n" );
			goto no_grub4dos;
		}
	RtlCopyMemory ( VendorID, SafeMbrHookPtr->VendorID, 8 );
	DBG ( "INT 0x13 Safety Hook Vendor ID: %s\n", VendorID );
	if ( !( RtlCompareMemory ( VendorID, "GRUB4DOS", 8 ) == 8 ) )
		{
			DBG ( "Non-GRUB4DOS INT 0x13 Safety Hook\n" );
			goto no_grub4dos;
		}
	Grub4DosDriveMapSlotPtr =
		( PPROBE_GRUB4DOSDRIVEMAPSLOT ) ( PhysicalMemory +
																			( ( ( UINT32 ) InterruptVector->Segment )
																				<< 4 ) + 0x20 );
	DBG ( "GRUB4DOS SourceDrive: 0x%02x\n",
				Grub4DosDriveMapSlotPtr->SourceDrive );
	DBG ( "GRUB4DOS DestDrive: 0x%02x\n", Grub4DosDriveMapSlotPtr->DestDrive );
	DBG ( "GRUB4DOS MaxHead: %d\n", Grub4DosDriveMapSlotPtr->MaxHead );
	DBG ( "GRUB4DOS MaxSector: %d\n", Grub4DosDriveMapSlotPtr->MaxSector );
	DBG ( "GRUB4DOS DestMaxCylinder: %d\n",
				Grub4DosDriveMapSlotPtr->DestMaxCylinder );
	DBG ( "GRUB4DOS DestMaxHead: %d\n", Grub4DosDriveMapSlotPtr->DestMaxHead );
	DBG ( "GRUB4DOS DestMaxSector: %d\n",
				Grub4DosDriveMapSlotPtr->DestMaxSector );
	DBG ( "GRUB4DOS SectorStart: 0x%08x\n",
				Grub4DosDriveMapSlotPtr->SectorStart );
	DBG ( "GRUB4DOS SectorCount: %d\n", Grub4DosDriveMapSlotPtr->SectorCount );
	Disk.Initialize = Probe_NoInitialize;
	/*
	 * Possible precision loss
	 */
	Disk.RAMDisk.DiskBuf =
		( UINT32 ) ( Grub4DosDriveMapSlotPtr->SectorStart * SECTORSIZE );
	Disk.LBADiskSize = Disk.RAMDisk.DiskSize =
		( UINT32 ) Grub4DosDriveMapSlotPtr->SectorCount;
	Disk.Heads = Grub4DosDriveMapSlotPtr->MaxHead + 1;
	Disk.Sectors = Grub4DosDriveMapSlotPtr->DestMaxSector;
	Disk.Cylinders = Disk.LBADiskSize / ( Disk.Heads * Disk.Sectors );
	Disk.IsRamdisk = TRUE;
	FoundGrub4DosMapping = TRUE;

no_grub4dos:
	MmUnmapIoSpace ( PhysicalMemory, 0x100000 );
	if ( FoundGrub4DosMapping )
		{
			if ( !Bus_AddChild ( BusDeviceObject, Disk, TRUE ) )
				{
					DBG ( "Bus_AddChild() failed for GRUB4DOS\n" );
				}
			else if ( BusDeviceExtension->Bus.PhysicalDeviceObject != NULL )
				{
					IoInvalidateDeviceRelations ( BusDeviceExtension->Bus.
																				PhysicalDeviceObject, BusRelations );
				}
		}
	else
		{
			DBG ( "No GRUB4DOS drive mappings found\n" );
		}
}
