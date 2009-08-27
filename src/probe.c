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
#include "aoe.h"

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

typedef struct _PROBE_SAFEMBRHOOK
{
	UCHAR Jump[3];
	UCHAR Signature[8];
	UCHAR VendorID[8];
	UINT16 PrevHookOff;
	UINT16 PrevHookSeg;
	UINT64 Flags;
} PROBE_SAFEMBRHOOK,
*PPROBE_SAFEMBRHOOK;

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

VOID STDCALL
Probe_MemDisk (
	PDEVICE_OBJECT BusDeviceObject
 )
{
	PHYSICAL_ADDRESS PhysicalAddress;
	PUCHAR PhysicalMemory;
	UINT16 Int13Seg,
	 Int13Off;
	UINT32 Int13Hook;
	PMDI_PATCHAREA MemdiskPtr;
	UINT Offset;
	BOOLEAN FoundMemdisk = FALSE;
	PDRIVER_DEVICEEXTENSION BusDeviceExtension =
		( PDRIVER_DEVICEEXTENSION ) BusDeviceObject->DeviceExtension;
	DISK_DISK Disk;

	/*
	 * Find a MEMDISK.  This discovery strategy is extremely poor at the
	 * moment.  The eventual goal is to discover MEMDISK RAM disks using
	 * an ACPI strcture.  Slight modifications to MEMDISK will help with this
	 */
	PhysicalAddress.QuadPart = 0LL;
	PhysicalMemory = MmMapIoSpace ( PhysicalAddress, 0x100000, MmNonCached );
	if ( !PhysicalMemory )
		{
			DBG ( "Could not map low memory\n" );
			return;
		}
	Int13Seg = *( ( PUINT16 ) & PhysicalMemory[0x13 * 4 + 2] );
	Int13Off = *( ( PUINT16 ) & PhysicalMemory[0x13 * 4] );
	Int13Hook = ( ( ( UINT32 ) Int13Seg ) << 4 ) + ( ( UINT32 ) Int13Off );
	DBG ( "INT 0x13 Segment: 0x%04x\n", Int13Seg );
	DBG ( "INT 0x13 Offset: 0x%04x\n", Int13Off );
	DBG ( "INT 0x13 Hook: 0x%08x\n", Int13Hook );
	for ( Offset = 0; Offset < 0x100000 - sizeof ( MDI_PATCHAREA ); Offset += 2 )
		{
			MemdiskPtr = ( PMDI_PATCHAREA ) & PhysicalMemory[Offset];
			if ( MemdiskPtr->mdi_bytes != 0x1e )
				continue;
			if ( ( MemdiskPtr->mdi_version_major != 3 )
					 || ( MemdiskPtr->mdi_version_minor < 0x50 )
					 || ( MemdiskPtr->mdi_version_minor > 0x53 ) )
				continue;
			DBG ( "Found MEMDISK sig: 0x%08x\n", Offset );
			DBG ( "MEMDISK DiskBuf: 0x%08x\n", MemdiskPtr->diskbuf );
			DBG ( "MEMDISK DiskSize: %d sectors\n", MemdiskPtr->disksize );
			Disk.Initialize = Probe_NoInitialize;
			Disk.RAMDisk.DiskBuf = MemdiskPtr->diskbuf;
			Disk.LBADiskSize = Disk.RAMDisk.DiskSize = MemdiskPtr->disksize;
			Disk.IsRamdisk = TRUE;
			FoundMemdisk = TRUE;
			break;
		}
	MmUnmapIoSpace ( PhysicalMemory, 0x100000 );
	if ( FoundMemdisk )
		{
			PhysicalAddress.QuadPart = Disk.RAMDisk.DiskBuf;
			Disk.RAMDisk.PhysicalMemory =
				MmMapIoSpace ( PhysicalAddress, Disk.LBADiskSize * SECTORSIZE,
											 MmNonCached );
			if ( !Disk.RAMDisk.PhysicalMemory )
				{
					DBG ( "Could not map memory for MEMDISK!\n" );
					return;
				}
			if ( !Bus_AddChild ( BusDeviceObject, Disk, TRUE ) )
				{
					DBG ( "Bus_AddChild() failed for MEMDISK\n" );
					MmUnmapIoSpace ( Disk.RAMDisk.PhysicalMemory,
													 Disk.LBADiskSize * SECTORSIZE );
				}
			else if ( BusDeviceExtension->Bus.PhysicalDeviceObject != NULL )
				{
					IoInvalidateDeviceRelations ( BusDeviceExtension->Bus.
																				PhysicalDeviceObject, BusRelations );
				}
		}
	else
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
	UINT16 Int13Seg,
	 Int13Off;
	UINT32 Int13Hook;
	PPROBE_SAFEMBRHOOK SafeMbrHookPtr;
	UCHAR Signature[9] = { 0, 0, 0, 0, 0, 0, 0, 0 }
	, VendorID[9] =
	{
	0, 0, 0, 0, 0, 0, 0, 0};
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
	Int13Seg = *( ( PUINT16 ) & PhysicalMemory[0x13 * 4 + 2] );
	Int13Off = *( ( PUINT16 ) & PhysicalMemory[0x13 * 4] );
	Int13Hook = ( ( ( UINT32 ) Int13Seg ) << 4 ) + ( ( UINT32 ) Int13Off );
	SafeMbrHookPtr = ( PPROBE_SAFEMBRHOOK ) & PhysicalMemory[Int13Hook];
	RtlCopyMemory ( Signature, SafeMbrHookPtr->Signature, 8 );
	DBG ( "INT 0x13 Segment: 0x%04x\n", Int13Seg );
	DBG ( "INT 0x13 Offset: 0x%04x\n", Int13Off );
	DBG ( "INT 0x13 Hook: 0x%08x\n", Int13Hook );
	DBG ( "INT 0x13 Safety Hook Signature: %s\n", Signature );
	if ( !RtlCompareMemory ( Signature, "$INT13SF", 8 ) )
		{
			DBG ( "Invalid INT 0x13 Safety Hook Signature\n" );
			goto no_grub4dos;
		}
	RtlCopyMemory ( VendorID, SafeMbrHookPtr->VendorID, 8 );
	DBG ( "INT 0x13 Safety Hook Vendor ID: %s\n", VendorID );
	if ( !RtlCompareMemory ( VendorID, "GRUB4DOS", 8 ) )
		{
			DBG ( "Non-GRUB4DOS INT 0x13 Safety Hook\n" );
			goto no_grub4dos;
		}
	Grub4DosDriveMapSlotPtr =
		( PPROBE_GRUB4DOSDRIVEMAPSLOT ) &
		PhysicalMemory[( ( ( UINT32 ) Int13Seg ) << 4 ) + 0x20];
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
	Disk.RAMDisk.DiskBuf = Grub4DosDriveMapSlotPtr->SectorStart * SECTORSIZE;
	Disk.LBADiskSize = Disk.RAMDisk.DiskSize =
		Grub4DosDriveMapSlotPtr->SectorCount;
	Disk.Heads = Grub4DosDriveMapSlotPtr->MaxHead + 1;
	Disk.Sectors = Grub4DosDriveMapSlotPtr->DestMaxSector;
	Disk.Cylinders = Disk.LBADiskSize / ( Disk.Heads * Disk.Sectors );
	Disk.IsRamdisk = TRUE;
	FoundGrub4DosMapping = TRUE;

no_grub4dos:
	MmUnmapIoSpace ( PhysicalMemory, 0x100000 );
	if ( FoundGrub4DosMapping )
		{
			PhysicalAddress.QuadPart = Disk.RAMDisk.DiskBuf;
			/*
			 * Possible precision loss
			 */
			Disk.RAMDisk.PhysicalMemory =
				MmMapIoSpace ( PhysicalAddress, Disk.LBADiskSize * SECTORSIZE,
											 MmNonCached );
			if ( !Disk.RAMDisk.PhysicalMemory )
				{
					DBG ( "Could not map memory for GRUB4DOS disk!\n" );
					return;
				}
			if ( !Bus_AddChild ( BusDeviceObject, Disk, TRUE ) )
				{
					DBG ( "Bus_AddChild() failed for GRUB4DOS\n" );
					MmUnmapIoSpace ( Disk.RAMDisk.PhysicalMemory,
													 Disk.LBADiskSize * SECTORSIZE );
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
