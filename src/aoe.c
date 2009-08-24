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
 * AoE specifics
 *
 */

#include "portable.h"
#include <ntddk.h>
#include "aoe.h"
#include "driver.h"
#include "protocol.h"
#include "debug.h"
#include "bus.h"

extern NTSTATUS STDCALL ZwWaitForSingleObject (
	IN HANDLE Handle,
	IN BOOLEAN Alertable,
	IN PLARGE_INTEGER Timeout OPTIONAL
 );

/* In this file */
static VOID STDCALL AoE_Thread (
	IN PVOID StartContext
 );

#ifdef _MSC_VER
#  pragma pack(1)
#endif

/** Tag types */
typedef enum
{ AoE_RequestType, AoE_SearchDriveType } AOE_TAGTYPE,
*PAOE_TAGTYPE;

/** AoE packet */
typedef struct _AOE_PACKET
{
	UCHAR ReservedFlag:2;
	UCHAR ErrorFlag:1;
	UCHAR ResponseFlag:1;
	UCHAR Ver:4;
	UCHAR Error;
	USHORT Major;
	UCHAR Minor;
	UCHAR Command;
	UINT Tag;

	UCHAR WriteAFlag:1;
	UCHAR AsyncAFlag:1;
	UCHAR Reserved1AFlag:2;
	UCHAR DeviceHeadAFlag:1;
	UCHAR Reserved2AFlag:1;
	UCHAR ExtendedAFlag:1;
	UCHAR Reserved3AFlag:1;
	union
	{
		UCHAR Err;
		UCHAR Feature;
	};
	UCHAR Count;
	union
	{
		UCHAR Cmd;
		UCHAR Status;
	};

	UCHAR Lba0;
	UCHAR Lba1;
	UCHAR Lba2;
	UCHAR Lba3;
	UCHAR Lba4;
	UCHAR Lba5;
	USHORT Reserved;

	UCHAR Data[];
} __attribute__ ( ( __packed__ ) ) AOE_PACKET, *PAOE_PACKET;

#ifdef _MSC_VER
#  pragma pack()
#endif

/** A request */
typedef struct _AOE_REQUEST
{
	AOE_REQUESTMODE Mode;
	ULONG SectorCount;
	PUCHAR Buffer;
	PIRP Irp;
	ULONG TagCount;
	ULONG TotalTags;
} AOE_REQUEST,
*PAOE_REQUEST;

/** A tag */
typedef struct _AOE_TAG
{
	AOE_TAGTYPE Type;
	PDRIVER_DEVICEEXTENSION DeviceExtension;
	PAOE_REQUEST Request;
	ULONG Id;
	PAOE_PACKET PacketData;
	ULONG PacketSize;
	LARGE_INTEGER FirstSendTime;
	LARGE_INTEGER SendTime;
	ULONG BufferOffset;
	ULONG SectorCount;
	struct _AOE_TAG *Next;
	struct _AOE_TAG *Previous;
} AOE_TAG,
*PAOE_TAG;

/** A disk search */
typedef struct _AOE_DISKSEARCH
{
	PDRIVER_DEVICEEXTENSION DeviceExtension;
	PAOE_TAG Tag;
	struct _AOE_DISKSEARCH *Next;
} AOE_DISKSEARCH,
*PAOE_DISKSEARCH;

/** Globals */
static BOOLEAN AoE_Globals_Stop = FALSE;
static KSPIN_LOCK AoE_Globals_SpinLock;
static KEVENT AoE_Globals_ThreadSignalEvent;
static PAOE_TAG AoE_Globals_TagList = NULL;
static PAOE_TAG AoE_Globals_TagListLast = NULL;
static PAOE_TAG AoE_Globals_ProbeTag = NULL;
static PAOE_DISKSEARCH AoE_Globals_DiskSearchList = NULL;
static LONG AoE_Globals_OutstandingTags = 0;
static HANDLE AoE_Globals_ThreadHandle;

/**
 * Start AoE operations
 *
 * @ret Status		Return status code
 */
NTSTATUS STDCALL
AoE_Start (
	void
 )
{
	NTSTATUS Status;
	OBJECT_ATTRIBUTES ObjectAttributes;
	PVOID ThreadObject;

	DBG ( "Entry\n" );

	/*
	 * Allocate and zero-fill the global probe tag 
	 */
	if ( ( AoE_Globals_ProbeTag =
				 ( PAOE_TAG ) ExAllocatePool ( NonPagedPool,
																			 sizeof ( AOE_TAG ) ) ) == NULL )
		{
			DBG ( "Couldn't allocate probe tag; bye!\n" );
			return STATUS_INSUFFICIENT_RESOURCES;
		}
	RtlZeroMemory ( AoE_Globals_ProbeTag, sizeof ( AOE_TAG ) );

	/*
	 * Set up the probe tag's AoE packet reference 
	 */
	AoE_Globals_ProbeTag->PacketSize = sizeof ( AOE_PACKET );
	/*
	 * Allocate and zero-fill the probe tag's packet reference 
	 */
	if ( ( AoE_Globals_ProbeTag->PacketData =
				 ( PAOE_PACKET ) ExAllocatePool ( NonPagedPool,
																					AoE_Globals_ProbeTag->
																					PacketSize ) ) == NULL )
		{
			DBG ( "Couldn't allocate AoE_Globals_ProbeTag->PacketData\n" );
			ExFreePool ( AoE_Globals_ProbeTag );
			return STATUS_INSUFFICIENT_RESOURCES;
		}
	AoE_Globals_ProbeTag->SendTime.QuadPart = 0LL;
	RtlZeroMemory ( AoE_Globals_ProbeTag->PacketData,
									AoE_Globals_ProbeTag->PacketSize );

	/*
	 * Initialize the probe tag's AoE packet 
	 */
	AoE_Globals_ProbeTag->PacketData->Ver = AOEPROTOCOLVER;
	AoE_Globals_ProbeTag->PacketData->Major = htons ( ( USHORT ) - 1 );
	AoE_Globals_ProbeTag->PacketData->Minor = ( UCHAR ) - 1;
	AoE_Globals_ProbeTag->PacketData->Cmd = 0xec;	/* IDENTIFY DEVICE */
	AoE_Globals_ProbeTag->PacketData->Count = 1;

	/*
	 * Initialize global spin-lock and global thread signal event 
	 */
	KeInitializeSpinLock ( &AoE_Globals_SpinLock );
	KeInitializeEvent ( &AoE_Globals_ThreadSignalEvent, SynchronizationEvent,
											FALSE );

	/*
	 * Initialize object attributes 
	 */
	InitializeObjectAttributes ( &ObjectAttributes, NULL, OBJ_KERNEL_HANDLE,
															 NULL, NULL );

	/*
	 * Create global thread 
	 */
	if ( !NT_SUCCESS
			 ( Status =
				 PsCreateSystemThread ( &AoE_Globals_ThreadHandle, THREAD_ALL_ACCESS,
																&ObjectAttributes, NULL, NULL, AoE_Thread,
																NULL ) ) )
		return Error ( "PsCreateSystemThread", Status );

	if ( !NT_SUCCESS
			 ( Status =
				 ObReferenceObjectByHandle ( AoE_Globals_ThreadHandle,
																		 THREAD_ALL_ACCESS, NULL, KernelMode,
																		 &ThreadObject, NULL ) ) )
		{
			ZwClose ( AoE_Globals_ThreadHandle );
			Error ( "ObReferenceObjectByHandle", Status );
			AoE_Globals_Stop = TRUE;
			KeSetEvent ( &AoE_Globals_ThreadSignalEvent, 0, FALSE );
		}

	return Status;
}

/**
 * Stop AoE operations
 */
VOID STDCALL
AoE_Stop (
	void
 )
{
	NTSTATUS Status;
	PAOE_DISKSEARCH DiskSearch,
	 PreviousDiskSearch;
	PAOE_TAG Tag;
	KIRQL Irql;

	DBG ( "Entry\n" );

	/*
	 * If we're not already shutting down, signal the event 
	 */
	if ( !AoE_Globals_Stop )
		{
			AoE_Globals_Stop = TRUE;
			KeSetEvent ( &AoE_Globals_ThreadSignalEvent, 0, FALSE );
			/*
			 * Wait until the event has been signalled 
			 */
			if ( !NT_SUCCESS
					 ( Status =
						 ZwWaitForSingleObject ( AoE_Globals_ThreadHandle, FALSE,
																		 NULL ) ) )
				Error ( "AoE_Stop ZwWaitForSingleObject", Status );
			ZwClose ( AoE_Globals_ThreadHandle );
		}

	/*
	 * Wait until we have the global spin-lock 
	 */
	KeAcquireSpinLock ( &AoE_Globals_SpinLock, &Irql );

	/*
	 * Free disk searches in the global disk search list 
	 */
	DiskSearch = AoE_Globals_DiskSearchList;
	while ( DiskSearch != NULL )
		{
			KeSetEvent ( &DiskSearch->DeviceExtension->Disk.SearchEvent, 0, FALSE );
			PreviousDiskSearch = DiskSearch;
			DiskSearch = DiskSearch->Next;
			ExFreePool ( PreviousDiskSearch );
		}

	/*
	 * Cancel and free all tags in the global tag list 
	 */
	Tag = AoE_Globals_TagList;
	while ( Tag != NULL )
		{
			if ( Tag->Request != NULL && --Tag->Request->TagCount == 0 )
				{
					Tag->Request->Irp->IoStatus.Information = 0;
					Tag->Request->Irp->IoStatus.Status = STATUS_CANCELLED;
					IoCompleteRequest ( Tag->Request->Irp, IO_NO_INCREMENT );
					ExFreePool ( Tag->Request );
				}
			if ( Tag->Next == NULL )
				{
					ExFreePool ( Tag->PacketData );
					ExFreePool ( Tag );
					Tag = NULL;
				}
			else
				{
					Tag = Tag->Next;
					ExFreePool ( Tag->Previous->PacketData );
					ExFreePool ( Tag->Previous );
				}
		}
	AoE_Globals_TagList = NULL;
	AoE_Globals_TagListLast = NULL;

	/*
	 * Free the global probe tag and its AoE packet 
	 */
	ExFreePool ( AoE_Globals_ProbeTag->PacketData );
	ExFreePool ( AoE_Globals_ProbeTag );

	/*
	 * Release the global spin-lock 
	 */
	KeReleaseSpinLock ( &AoE_Globals_SpinLock, Irql );
}

/**
 * Search for disk parameters
 *
 * @v DeviceExtension		The device extension for the disk
 *
 * Returns TRUE if the disk could be matched, FALSE otherwise.
 */
BOOLEAN STDCALL
AoE_SearchDrive (
	IN PDRIVER_DEVICEEXTENSION DeviceExtension
 )
{
	PAOE_DISKSEARCH DiskSearch,
	 DiskSearchWalker,
	 PreviousDiskSearch;
	LARGE_INTEGER Timeout,
	 CurrentTime;
	PAOE_TAG Tag,
	 TagWalker;
	KIRQL Irql,
	 InnerIrql;
	LARGE_INTEGER MaxSectorsPerPacketSendTime;
	ULONG MTU;

	/*
	 * Allocate our disk search 
	 */
	if ( ( DiskSearch =
				 ( PAOE_DISKSEARCH ) ExAllocatePool ( NonPagedPool,
																							sizeof ( AOE_DISKSEARCH ) ) ) ==
			 NULL )
		{
			DBG ( "Couldn't allocate DiskSearch; bye!\n" );
			return FALSE;
		}

	/*
	 * Initialize the disk search 
	 */
	DiskSearch->DeviceExtension = DeviceExtension;
	DiskSearch->Next = NULL;
	DeviceExtension->Disk.SearchState = SearchNIC;

	KeResetEvent ( &DeviceExtension->Disk.SearchEvent );

	/*
	 * Wait until we have the global spin-lock 
	 */
	KeAcquireSpinLock ( &AoE_Globals_SpinLock, &Irql );

	/*
	 * Add our disk search to the global list of disk searches 
	 */
	if ( AoE_Globals_DiskSearchList == NULL )
		{
			AoE_Globals_DiskSearchList = DiskSearch;
		}
	else
		{
			DiskSearchWalker = AoE_Globals_DiskSearchList;
			while ( DiskSearchWalker->Next )
				DiskSearchWalker = DiskSearchWalker->Next;
			DiskSearchWalker->Next = DiskSearch;
		}

	/*
	 * Release the global spin-lock 
	 */
	KeReleaseSpinLock ( &AoE_Globals_SpinLock, Irql );

	/*
	 * We go through all the states until the disk is ready for use 
	 */
	while ( TRUE )
		{
			/*
			 * Wait for our device's extension's search to be signalled 
			 */
			/*
			 * TODO: Make the below value a #defined constant 
			 */
			/*
			 * 500.000 * 100ns = 50.000.000 ns = 50ms 
			 */
			Timeout.QuadPart = -500000LL;
			KeWaitForSingleObject ( &DeviceExtension->Disk.SearchEvent, Executive,
															KernelMode, FALSE, &Timeout );
			if ( AoE_Globals_Stop )
				{
					DBG ( "AoE is shutting down; bye!\n" );
					return FALSE;
				}

			/*
			 * Wait until we have the device extension's spin-lock 
			 */
			KeAcquireSpinLock ( &DeviceExtension->Disk.SpinLock, &Irql );

			if ( DeviceExtension->Disk.SearchState == SearchNIC )
				{
					if ( !Protocol_SearchNIC ( DeviceExtension->Disk.AoE.ClientMac ) )
						{
							KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
							continue;
						}
					else
						{
							/*
							 * We found the adapter to use, get MTU next 
							 */
							DeviceExtension->Disk.AoE.MTU =
								Protocol_GetMTU ( DeviceExtension->Disk.AoE.ClientMac );
							DeviceExtension->Disk.SearchState = GetSize;
						}
				}

			if ( DeviceExtension->Disk.SearchState == GettingSize )
				{
					/*
					 * Still getting the disk's size 
					 */
					KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
					continue;
				}
			if ( DeviceExtension->Disk.SearchState == GettingGeometry )
				{
					/*
					 * Still getting the disk's geometry 
					 */
					KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
					continue;
				}
			if ( DeviceExtension->Disk.SearchState == GettingMaxSectorsPerPacket )
				{
					KeQuerySystemTime ( &CurrentTime );
					/*
					 * TODO: Make the below value a #defined constant 
					 */
					/*
					 * 2.500.000 * 100ns = 250.000.000 ns = 250ms 
					 */
					if ( CurrentTime.QuadPart >
							 MaxSectorsPerPacketSendTime.QuadPart + 2500000LL )
						{
							DBG ( "No reply after 250ms for MaxSectorsPerPacket %d, "
										"giving up\n",
										DeviceExtension->Disk.AoE.MaxSectorsPerPacket );
							DeviceExtension->Disk.AoE.MaxSectorsPerPacket--;
							DeviceExtension->Disk.SearchState = Done;
						}
					else
						{
							/*
							 * Still getting the maximum sectors per packet count 
							 */
							KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
							continue;
						}
				}

			if ( DeviceExtension->Disk.SearchState == Done )
				{
					/*
					 * We've finished the disk search; perform clean-up 
					 */
					KeAcquireSpinLock ( &AoE_Globals_SpinLock, &InnerIrql );

					/*
					 * Tag clean-up: Find out if our tag is in the global tag list 
					 */
					TagWalker = AoE_Globals_TagList;
					while ( TagWalker != NULL && TagWalker != Tag )
						TagWalker = TagWalker->Next;
					if ( TagWalker != NULL )
						{
							/*
							 * We found it.  If it's at the beginning of the list, adjust
							 * the list to point the the next tag
							 */
							if ( Tag->Previous == NULL )
								AoE_Globals_TagList = Tag->Next;
							else
								/*
								 * Remove our tag from the list 
								 */
								Tag->Previous->Next = Tag->Next;
							/*
							 * If we 're at the end of the list, adjust the list's end to
							 * point to the penultimate tag
							 */
							if ( Tag->Next == NULL )
								AoE_Globals_TagListLast = Tag->Previous;
							else
								/*
								 * Remove our tag from the list 
								 */
								Tag->Next->Previous = Tag->Previous;
							AoE_Globals_OutstandingTags--;
							if ( AoE_Globals_OutstandingTags < 0 )
								DBG ( "AoE_Globals_OutstandingTags < 0!!\n" );
							/*
							 * Free our tag and its AoE packet 
							 */
							ExFreePool ( Tag->PacketData );
							ExFreePool ( Tag );
						}

					/*
					 * Disk search clean-up 
					 */
					if ( AoE_Globals_DiskSearchList == NULL )
						{
							DBG ( "AoE_Globals_DiskSearchList == NULL!!\n" );
						}
					else
						{
							/*
							 * Find our disk search in the global list of disk searches 
							 */
							DiskSearchWalker = AoE_Globals_DiskSearchList;
							while ( DiskSearchWalker
											&& DiskSearchWalker->DeviceExtension != DeviceExtension )
								{
									PreviousDiskSearch = DiskSearchWalker;
									DiskSearchWalker = DiskSearchWalker->Next;
								}
							if ( DiskSearchWalker )
								{
									/*
									 * We found our disk search.  If it's the first one in
									 * the list, adjust the list and remove it
									 */
									if ( DiskSearchWalker == AoE_Globals_DiskSearchList )
										AoE_Globals_DiskSearchList = DiskSearchWalker->Next;
									else
										/*
										 * Just remove it 
										 */
										PreviousDiskSearch->Next = DiskSearchWalker->Next;
									/*
									 * Free our disk search 
									 */
									ExFreePool ( DiskSearchWalker );
								}
							else
								{
									DBG ( "Disk not found in AoE_Globals_DiskSearchList!!\n" );
								}
						}

					/*
					 * Release global and device extension spin-locks 
					 */
					KeReleaseSpinLock ( &AoE_Globals_SpinLock, InnerIrql );
					KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );

					DBG ( "Disk size: %I64uM cylinders: %I64u heads: %u "
								"sectors: %u sectors per packet: %u\n",
								DeviceExtension->Disk.LBADiskSize / 2048,
								DeviceExtension->Disk.Cylinders, DeviceExtension->Disk.Heads,
								DeviceExtension->Disk.Sectors,
								DeviceExtension->Disk.AoE.MaxSectorsPerPacket );
					return TRUE;
				}

			/*
			 * if ( DeviceExtension->Disk.SearchState == Done ) 
			 */
			/*
			 * Establish our tag 
			 */
			if ( ( Tag =
						 ( PAOE_TAG ) ExAllocatePool ( NonPagedPool,
																					 sizeof ( AOE_TAG ) ) ) == NULL )
				{
					DBG ( "Couldn't allocate Tag\n" );
					KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
					/*
					 * Maybe next time around 
					 */
					continue;
				}
			RtlZeroMemory ( Tag, sizeof ( AOE_TAG ) );
			Tag->Type = AoE_SearchDriveType;
			Tag->DeviceExtension = DeviceExtension;

			/*
			 * Establish our tag's AoE packet 
			 */
			Tag->PacketSize = sizeof ( AOE_PACKET );
			if ( ( Tag->PacketData =
						 ( PAOE_PACKET ) ExAllocatePool ( NonPagedPool,
																							Tag->PacketSize ) ) == NULL )
				{
					DBG ( "Couldn't allocate Tag->PacketData\n" );
					ExFreePool ( Tag );
					Tag = NULL;
					KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
					/*
					 * Maybe next time around 
					 */
					continue;
				}
			RtlZeroMemory ( Tag->PacketData, Tag->PacketSize );
			Tag->PacketData->Ver = AOEPROTOCOLVER;
			Tag->PacketData->Major =
				htons ( ( USHORT ) DeviceExtension->Disk.AoE.Major );
			Tag->PacketData->Minor = ( UCHAR ) DeviceExtension->Disk.AoE.Minor;
			Tag->PacketData->ExtendedAFlag = TRUE;

			/*
			 * Initialize the packet appropriately based on our current phase 
			 */
			switch ( DeviceExtension->Disk.SearchState )
				{
					case GetSize:
						/*
						 * TODO: Make the below value into a #defined constant 
						 */
						Tag->PacketData->Cmd = 0xec;	/* IDENTIFY DEVICE */
						Tag->PacketData->Count = 1;
						DeviceExtension->Disk.SearchState = GettingSize;
						break;
					case GetGeometry:
						/*
						 * TODO: Make the below value into a #defined constant 
						 */
						Tag->PacketData->Cmd = 0x24;	/* READ SECTOR */
						Tag->PacketData->Count = 1;
						DeviceExtension->Disk.SearchState = GettingGeometry;
						break;
					case GetMaxSectorsPerPacket:
						/*
						 * TODO: Make the below value into a #defined constant 
						 */
						Tag->PacketData->Cmd = 0x24;	/* READ SECTOR */
						Tag->PacketData->Count =
							( UCHAR ) ( ++DeviceExtension->Disk.AoE.MaxSectorsPerPacket );
						KeQuerySystemTime ( &MaxSectorsPerPacketSendTime );
						DeviceExtension->Disk.SearchState = GettingMaxSectorsPerPacket;
						/*
						 * TODO: Make the below value into a #defined constant 
						 */
						DeviceExtension->Disk.AoE.Timeout = 200000;
						break;
					default:
						DBG ( "Undefined SearchState!!\n" );
						ExFreePool ( Tag->PacketData );
						ExFreePool ( Tag );
						/*
						 * TODO: Do we need to nullify Tag here? 
						 */
						KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
						continue;
						break;
				}

			/*
			 * Enqueue our tag 
			 */
			Tag->Next = NULL;
			KeAcquireSpinLock ( &AoE_Globals_SpinLock, &InnerIrql );
			if ( AoE_Globals_TagList == NULL )
				{
					AoE_Globals_TagList = Tag;
					Tag->Previous = NULL;
				}
			else
				{
					AoE_Globals_TagListLast->Next = Tag;
					Tag->Previous = AoE_Globals_TagListLast;
				}
			AoE_Globals_TagListLast = Tag;
			KeReleaseSpinLock ( &AoE_Globals_SpinLock, InnerIrql );
			KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
		}
}

/**
 * I/O Request
 *
 * @v DeviceExtension The device extension for the disk
 * @v Mode            Read / write mode
 * @v StartSector     First sector for request
 * @v SectorCount     Number of sectors to work with
 * @v Buffer          Buffer to read / write sectors to / from
 * @v Irp             Interrupt request packet for this request
 */
NTSTATUS STDCALL
AoE_Request (
	IN PDRIVER_DEVICEEXTENSION DeviceExtension,
	IN AOE_REQUESTMODE Mode,
	IN LONGLONG StartSector,
	IN ULONG SectorCount,
	IN PUCHAR Buffer,
	IN PIRP Irp
 )
{
	PAOE_REQUEST Request;
	PAOE_TAG Tag,
	 NewTagList = NULL,
		PreviousTag = NULL;
	KIRQL Irql;
	ULONG i;
	PHYSICAL_ADDRESS PhysicalAddress;
	static PUCHAR PhysicalMemory = NULL;

	if ( AoE_Globals_Stop )
		{
			/*
			 * Shutting down AoE; we can't service this request 
			 */
			Irp->IoStatus.Information = 0;
			Irp->IoStatus.Status = STATUS_CANCELLED;
			IoCompleteRequest ( Irp, IO_NO_INCREMENT );
			return STATUS_CANCELLED;
		}

	if ( SectorCount < 1 )
		{
			/*
			 * A silly request 
			 */
			DBG ( "SectorCount < 1; cancelling\n" );
			Irp->IoStatus.Information = 0;
			Irp->IoStatus.Status = STATUS_CANCELLED;
			IoCompleteRequest ( Irp, IO_NO_INCREMENT );
			return STATUS_CANCELLED;
		}

	/*
	 * Handle the MEMDISK case
	 */
	if ( DeviceExtension->IsMemdisk )
		{
			if ( !PhysicalMemory )
				{
					PhysicalAddress.QuadPart = DeviceExtension->Disk.RAMDisk.DiskBuf;
					/*
					 * Possible precision loss from LONGLONG to SIZE_T here
					 */
					PhysicalMemory =
						MmMapIoSpace ( PhysicalAddress,
													 DeviceExtension->Disk.LBADiskSize * SECTORSIZE,
													 MmNonCached );
					if ( !PhysicalMemory )
						{
							DBG ( "Could not map low memory\n" );
							Irp->IoStatus.Information = 0;
							Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
							IoCompleteRequest ( Irp, IO_NO_INCREMENT );
							return STATUS_INSUFFICIENT_RESOURCES;
						}
				}
			if ( Mode == AoE_RequestMode_Write )
				RtlCopyMemory ( &PhysicalMemory[StartSector * SECTORSIZE], Buffer,
												SectorCount * SECTORSIZE );
			else
				RtlCopyMemory ( Buffer, &PhysicalMemory[StartSector * SECTORSIZE],
												SectorCount * SECTORSIZE );
			Irp->IoStatus.Information = SectorCount * SECTORSIZE;
			Irp->IoStatus.Status = STATUS_SUCCESS;
			IoCompleteRequest ( Irp, IO_NO_INCREMENT );
			return STATUS_SUCCESS;
		}

	/*
	 * Allocate and zero-fill our request 
	 */
	if ( ( Request =
				 ( PAOE_REQUEST ) ExAllocatePool ( NonPagedPool,
																					 sizeof ( AOE_REQUEST ) ) ) == NULL )
		{
			DBG ( "Couldn't allocate Request; bye!\n" );
			Irp->IoStatus.Information = 0;
			Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
			IoCompleteRequest ( Irp, IO_NO_INCREMENT );
			return STATUS_INSUFFICIENT_RESOURCES;
		}
	RtlZeroMemory ( Request, sizeof ( AOE_REQUEST ) );

	/*
	 * Initialize the request 
	 */
	Request->Mode = Mode;
	Request->SectorCount = SectorCount;
	Request->Buffer = Buffer;
	Request->Irp = Irp;
	Request->TagCount = 0;

	/*
	 * Split the requested sectors into packets in tags
	 */
	for ( i = 0; i < SectorCount;
				i += DeviceExtension->Disk.AoE.MaxSectorsPerPacket )
		{
			/*
			 * Allocate each tag 
			 */
			if ( ( Tag =
						 ( PAOE_TAG ) ExAllocatePool ( NonPagedPool,
																					 sizeof ( AOE_TAG ) ) ) == NULL )
				{
					DBG ( "Couldn't allocate Tag; bye!\n" );
					/*
					 * We failed while allocating tags; free the ones we built 
					 */
					Tag = NewTagList;
					while ( Tag != NULL )
						{
							PreviousTag = Tag;
							Tag = Tag->Next;
							ExFreePool ( PreviousTag->PacketData );
							ExFreePool ( PreviousTag );
						}
					ExFreePool ( Request );
					Irp->IoStatus.Information = 0;
					Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
					IoCompleteRequest ( Irp, IO_NO_INCREMENT );
					return STATUS_INSUFFICIENT_RESOURCES;
				}

			/*
			 * Initialize each tag 
			 */
			RtlZeroMemory ( Tag, sizeof ( AOE_TAG ) );
			Tag->Type = AoE_RequestType;
			Tag->Request = Request;
			Tag->DeviceExtension = DeviceExtension;
			Request->TagCount++;
			Tag->Id = 0;
			Tag->BufferOffset = i * SECTORSIZE;
			Tag->SectorCount =
				( ( SectorCount - i ) <
					DeviceExtension->Disk.AoE.MaxSectorsPerPacket ? SectorCount -
					i : DeviceExtension->Disk.AoE.MaxSectorsPerPacket );

			/*
			 * Allocate and initialize each tag's AoE packet 
			 */
			Tag->PacketSize = sizeof ( AOE_PACKET );
			if ( Mode == AoE_RequestMode_Write )
				Tag->PacketSize += Tag->SectorCount * SECTORSIZE;
			if ( ( Tag->PacketData =
						 ( PAOE_PACKET ) ExAllocatePool ( NonPagedPool,
																							Tag->PacketSize ) ) == NULL )
				{
					DBG ( "Couldn't allocate Tag->PacketData; bye!\n" );
					/*
					 * We failed while allocating an AoE packet; free
					 * the tags we built
					 */
					ExFreePool ( Tag );
					Tag = NewTagList;
					while ( Tag != NULL )
						{
							PreviousTag = Tag;
							Tag = Tag->Next;
							ExFreePool ( PreviousTag->PacketData );
							ExFreePool ( PreviousTag );
						}
					ExFreePool ( Request );
					Irp->IoStatus.Information = 0;
					Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
					IoCompleteRequest ( Irp, IO_NO_INCREMENT );
					return STATUS_INSUFFICIENT_RESOURCES;
				}
			RtlZeroMemory ( Tag->PacketData, Tag->PacketSize );
			Tag->PacketData->Ver = AOEPROTOCOLVER;
			Tag->PacketData->Major =
				htons ( ( USHORT ) DeviceExtension->Disk.AoE.Major );
			Tag->PacketData->Minor = ( UCHAR ) DeviceExtension->Disk.AoE.Minor;
			Tag->PacketData->Tag = 0;
			Tag->PacketData->Command = 0;
			Tag->PacketData->ExtendedAFlag = TRUE;
			if ( Mode == AoE_RequestMode_Read )
				{
					Tag->PacketData->Cmd = 0x24;	/* READ SECTOR */
				}
			else
				{
					Tag->PacketData->Cmd = 0x34;	/* WRITE SECTOR */
					Tag->PacketData->WriteAFlag = 1;
				}
			Tag->PacketData->Count = ( UCHAR ) Tag->SectorCount;
			Tag->PacketData->Lba0 = ( UCHAR ) ( ( ( StartSector + i ) >> 0 ) & 255 );
			Tag->PacketData->Lba1 = ( UCHAR ) ( ( ( StartSector + i ) >> 8 ) & 255 );
			Tag->PacketData->Lba2 =
				( UCHAR ) ( ( ( StartSector + i ) >> 16 ) & 255 );
			Tag->PacketData->Lba3 =
				( UCHAR ) ( ( ( StartSector + i ) >> 24 ) & 255 );
			Tag->PacketData->Lba4 =
				( UCHAR ) ( ( ( StartSector + i ) >> 32 ) & 255 );
			Tag->PacketData->Lba5 =
				( UCHAR ) ( ( ( StartSector + i ) >> 40 ) & 255 );

			/*
			 * For a write request, copy from the buffer into the AoE packet 
			 */
			if ( Mode == AoE_RequestMode_Write )
				RtlCopyMemory ( Tag->PacketData->Data, &Buffer[Tag->BufferOffset],
												Tag->SectorCount * SECTORSIZE );

			/*
			 * Add this tag to the request's tag list 
			 */
			Tag->Previous = PreviousTag;
			Tag->Next = NULL;
			if ( NewTagList == NULL )
				{
					NewTagList = Tag;
				}
			else
				{
					PreviousTag->Next = Tag;
				}
			PreviousTag = Tag;
		}
	/*
	 * Split the requested sectors into packets in tags
	 */
	Request->TotalTags = Request->TagCount;

	/*
	 * Wait until we have the global spin-lock 
	 */
	KeAcquireSpinLock ( &AoE_Globals_SpinLock, &Irql );

	/*
	 * Enqueue our request's tag list to the global tag list 
	 */
	if ( AoE_Globals_TagListLast == NULL )
		{
			AoE_Globals_TagList = NewTagList;
		}
	else
		{
			AoE_Globals_TagListLast->Next = NewTagList;
			NewTagList->Previous = AoE_Globals_TagListLast;
		}
	/*
	 * Adjust the global list to reflect our last tag 
	 */
	AoE_Globals_TagListLast = Tag;

	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_PENDING;
	IoMarkIrpPending ( Irp );

	KeReleaseSpinLock ( &AoE_Globals_SpinLock, Irql );
	KeSetEvent ( &AoE_Globals_ThreadSignalEvent, 0, FALSE );
	return STATUS_PENDING;
}

/**
 * Process an AoE reply
 *
 * @v SourceMac       The AoE server's MAC address
 * @v DestinationMac  The AoE client's MAC address
 * @v Data            The AoE packet
 * @v DataSize        The AoE packet's size
 */
NTSTATUS STDCALL
AoE_Reply (
	IN PUCHAR SourceMac,
	IN PUCHAR DestinationMac,
	IN PUCHAR Data,
	IN UINT DataSize
 )
{
	PAOE_PACKET Reply = ( PAOE_PACKET ) Data;
	LONGLONG LBASize;
	PAOE_TAG Tag;
	KIRQL Irql;
	BOOLEAN Found = FALSE;
	LARGE_INTEGER CurrentTime;

	/*
	 * Discard non-responses 
	 */
	if ( !Reply->ResponseFlag )
		return STATUS_SUCCESS;

	/*
	 * If the response matches our probe, add the AoE disk device 
	 */
	if ( AoE_Globals_ProbeTag->Id == Reply->Tag )
		{
			RtlCopyMemory ( &LBASize, &Reply->Data[200], sizeof ( LONGLONG ) );
			Bus_AddTarget ( DestinationMac, SourceMac, ntohs ( Reply->Major ),
											Reply->Minor, LBASize );
			return STATUS_SUCCESS;
		}

	/*
	 * Wait until we have the global spin-lock 
	 */
	KeAcquireSpinLock ( &AoE_Globals_SpinLock, &Irql );

	/*
	 * Search for request tag 
	 */
	if ( AoE_Globals_TagList == NULL )
		{
			KeReleaseSpinLock ( &AoE_Globals_SpinLock, Irql );
			return STATUS_SUCCESS;
		}
	Tag = AoE_Globals_TagList;
	while ( Tag != NULL )
		{
			if ( ( Tag->Id == Reply->Tag )
					 && ( Tag->PacketData->Major == Reply->Major )
					 && ( Tag->PacketData->Minor == Reply->Minor ) )
				{
					Found = TRUE;
					break;
				}
			Tag = Tag->Next;
		}
	if ( !Found )
		{
			KeReleaseSpinLock ( &AoE_Globals_SpinLock, Irql );
			return STATUS_SUCCESS;
		}
	else
		{
			/*
			 * Remove the tag from the global tag list 
			 */
			if ( Tag->Previous == NULL )
				AoE_Globals_TagList = Tag->Next;
			else
				Tag->Previous->Next = Tag->Next;
			if ( Tag->Next == NULL )
				AoE_Globals_TagListLast = Tag->Previous;
			else
				Tag->Next->Previous = Tag->Previous;
			AoE_Globals_OutstandingTags--;
			if ( AoE_Globals_OutstandingTags < 0 )
				DBG ( "AoE_Globals_OutstandingTags < 0!!\n" );
			KeSetEvent ( &AoE_Globals_ThreadSignalEvent, 0, FALSE );
		}
	KeReleaseSpinLock ( &AoE_Globals_SpinLock, Irql );

	/*
	 * If our tag was a discovery request, note the server 
	 */
	if ( RtlCompareMemory
			 ( Tag->DeviceExtension->Disk.AoE.ServerMac, "\xff\xff\xff\xff\xff\xff",
				 6 ) == 6 )
		{
			RtlCopyMemory ( Tag->DeviceExtension->Disk.AoE.ServerMac, SourceMac, 6 );
			DBG ( "Major: %d minor: %d found on server "
						"%02x:%02x:%02x:%02x:%02x:%02x\n",
						Tag->DeviceExtension->Disk.AoE.Major,
						Tag->DeviceExtension->Disk.AoE.Minor, SourceMac[0], SourceMac[1],
						SourceMac[2], SourceMac[3], SourceMac[4], SourceMac[5] );
		}

	KeQuerySystemTime ( &CurrentTime );
	Tag->DeviceExtension->Disk.AoE.Timeout -=
		( ULONG ) ( ( Tag->DeviceExtension->Disk.AoE.Timeout -
									( CurrentTime.QuadPart -
										Tag->FirstSendTime.QuadPart ) ) / 1024 );
	/*
	 * TODO: Replace the values below with #defined constants 
	 */
	if ( Tag->DeviceExtension->Disk.AoE.Timeout > 100000000 )
		Tag->DeviceExtension->Disk.AoE.Timeout = 100000000;

	switch ( Tag->Type )
		{
			case AoE_SearchDriveType:
				KeAcquireSpinLock ( &Tag->DeviceExtension->Disk.SpinLock, &Irql );
				switch ( Tag->DeviceExtension->Disk.SearchState )
					{
						case GettingSize:
							/*
							 * The reply tells us the disk size 
							 */
							RtlCopyMemory ( &Tag->DeviceExtension->Disk.LBADiskSize,
															&Reply->Data[200], sizeof ( LONGLONG ) );
							/*
							 * Next we are concerned with the disk geometry 
							 */
							Tag->DeviceExtension->Disk.SearchState = GetGeometry;
							break;
						case GettingGeometry:
							/*
							 * FIXME: use real values from partition table 
							 */
							Tag->DeviceExtension->Disk.Heads = 255;
							Tag->DeviceExtension->Disk.Sectors = 63;
							Tag->DeviceExtension->Disk.Cylinders =
								Tag->DeviceExtension->Disk.LBADiskSize /
								( Tag->DeviceExtension->Disk.Heads *
									Tag->DeviceExtension->Disk.Sectors );
							Tag->DeviceExtension->Disk.LBADiskSize =
								Tag->DeviceExtension->Disk.Cylinders *
								Tag->DeviceExtension->Disk.Heads *
								Tag->DeviceExtension->Disk.Sectors;
							/*
							 * Next we are concerned with the maximum sectors per packet 
							 */
							Tag->DeviceExtension->Disk.SearchState = GetMaxSectorsPerPacket;
							break;
						case GettingMaxSectorsPerPacket:
							DataSize -= sizeof ( AOE_PACKET );
							if ( DataSize <
									 ( Tag->DeviceExtension->Disk.AoE.MaxSectorsPerPacket *
										 SECTORSIZE ) )
								{
									DBG ( "Packet size too low while getting "
												"MaxSectorsPerPacket (tried %d, got size of %d)\n",
												Tag->DeviceExtension->Disk.AoE.MaxSectorsPerPacket,
												DataSize );
									Tag->DeviceExtension->Disk.AoE.MaxSectorsPerPacket--;
									Tag->DeviceExtension->Disk.SearchState = Done;
								}
							else if ( Tag->DeviceExtension->Disk.AoE.MTU <
												( sizeof ( AOE_PACKET ) +
													( ( Tag->DeviceExtension->Disk.
															AoE.MaxSectorsPerPacket + 1 ) * SECTORSIZE ) ) )
								{
									DBG ( "Got MaxSectorsPerPacket %d at size of %d. "
												"MTU of %d reached\n",
												Tag->DeviceExtension->Disk.AoE.MaxSectorsPerPacket,
												DataSize, Tag->DeviceExtension->Disk.AoE.MTU );
									Tag->DeviceExtension->Disk.SearchState = Done;
								}
							else
								{
									DBG ( "Got MaxSectorsPerPacket %d at size of %d, "
												"trying next...\n",
												Tag->DeviceExtension->Disk.AoE.MaxSectorsPerPacket,
												DataSize );
									Tag->DeviceExtension->Disk.SearchState =
										GetMaxSectorsPerPacket;
								}
							break;
						default:
							DBG ( "Undefined SearchState!\n" );
							break;
					}
				KeReleaseSpinLock ( &Tag->DeviceExtension->Disk.SpinLock, Irql );
				KeSetEvent ( &Tag->DeviceExtension->Disk.SearchEvent, 0, FALSE );
				break;
			case AoE_RequestType:
				/*
				 * If the reply is in response to a read request, get our data! 
				 */
				if ( Tag->Request->Mode == AoE_RequestMode_Read )
					RtlCopyMemory ( &Tag->Request->Buffer[Tag->BufferOffset],
													Reply->Data, Tag->SectorCount * SECTORSIZE );
				/*
				 * If this is the last reply expected for the read request,
				 * complete the IRP and free the request
				 */
				if ( InterlockedDecrement ( &Tag->Request->TagCount ) == 0 )
					{
						Tag->Request->Irp->IoStatus.Information =
							Tag->Request->SectorCount * SECTORSIZE;
						Tag->Request->Irp->IoStatus.Status = STATUS_SUCCESS;
						Driver_CompletePendingIrp ( Tag->Request->Irp );
						ExFreePool ( Tag->Request );
					}
				break;
			default:
				DBG ( "Unknown tag type!!\n" );
				break;
		}

	KeSetEvent ( &AoE_Globals_ThreadSignalEvent, 0, FALSE );
	ExFreePool ( Tag->PacketData );
	ExFreePool ( Tag );
	return STATUS_SUCCESS;
}

VOID STDCALL
AoE_ResetProbe (
	void
 )
{
	AoE_Globals_ProbeTag->SendTime.QuadPart = 0LL;
}

static VOID STDCALL
AoE_Thread (
	IN PVOID StartContext
 )
{
	LARGE_INTEGER Timeout,
	 CurrentTime,
	 ProbeTime,
	 ReportTime;
	ULONG NextTagId = 1;
	PAOE_TAG Tag;
	KIRQL Irql;
	ULONG Sends = 0;
	ULONG Resends = 0;
	ULONG ResendFails = 0;
	ULONG Fails = 0;
	ULONG RequestTimeout = 0;

	DBG ( "Entry\n" );
	ReportTime.QuadPart = 0LL;
	ProbeTime.QuadPart = 0LL;

	while ( TRUE )
		{
			/*
			 * TODO: Make the below value a #defined constant 
			 */
			/*
			 * 100.000 * 100ns = 10.000.000 ns = 10ms
			 */
			Timeout.QuadPart = -100000LL;
			KeWaitForSingleObject ( &AoE_Globals_ThreadSignalEvent, Executive,
															KernelMode, FALSE, &Timeout );
			KeResetEvent ( &AoE_Globals_ThreadSignalEvent );
			if ( AoE_Globals_Stop )
				{
					DBG ( "Stopping...\n" );
					PsTerminateSystemThread ( STATUS_SUCCESS );
				}
			Bus_CleanupTargetList (  );

			KeQuerySystemTime ( &CurrentTime );
			/*
			 * TODO: Make the below value a #defined constant 
			 */
			if ( CurrentTime.QuadPart > ( ReportTime.QuadPart + 10000000LL ) )
				{
					DBG ( "Sends: %d  Resends: %d  ResendFails: %d  Fails: %d  "
								"AoE_Globals_OutstandingTags: %d  RequestTimeout: %d\n", Sends,
								Resends, ResendFails, Fails, AoE_Globals_OutstandingTags,
								RequestTimeout );
					Sends = 0;
					Resends = 0;
					ResendFails = 0;
					Fails = 0;
					KeQuerySystemTime ( &ReportTime );
				}

			/*
			 * TODO: Make the below value a #defined constant 
			 */
			if ( CurrentTime.QuadPart >
					 ( AoE_Globals_ProbeTag->SendTime.QuadPart + 100000000LL ) )
				{
					AoE_Globals_ProbeTag->Id = NextTagId++;
					if ( NextTagId == 0 )
						NextTagId++;
					AoE_Globals_ProbeTag->PacketData->Tag = AoE_Globals_ProbeTag->Id;
					Protocol_Send ( "\xff\xff\xff\xff\xff\xff",
													"\xff\xff\xff\xff\xff\xff",
													( PUCHAR ) AoE_Globals_ProbeTag->PacketData,
													AoE_Globals_ProbeTag->PacketSize, NULL );
					KeQuerySystemTime ( &AoE_Globals_ProbeTag->SendTime );
				}

			KeAcquireSpinLock ( &AoE_Globals_SpinLock, &Irql );
			if ( AoE_Globals_TagList == NULL )
				{
					KeReleaseSpinLock ( &AoE_Globals_SpinLock, Irql );
					continue;
				}
			Tag = AoE_Globals_TagList;
			while ( Tag != NULL )
				{
					RequestTimeout = Tag->DeviceExtension->Disk.AoE.Timeout;
					if ( Tag->Id == 0 )
						{
							if ( AoE_Globals_OutstandingTags <= 64 )
								{
									/*
									 * if ( AoE_Globals_OutstandingTags <= 102400 ) { 
									 */
									if ( AoE_Globals_OutstandingTags < 0 )
										DBG ( "AoE_Globals_OutstandingTags < 0!!\n" );
									Tag->Id = NextTagId++;
									if ( NextTagId == 0 )
										NextTagId++;
									Tag->PacketData->Tag = Tag->Id;
									if ( Protocol_Send
											 ( Tag->DeviceExtension->Disk.AoE.ClientMac,
												 Tag->DeviceExtension->Disk.AoE.ServerMac,
												 ( PUCHAR ) Tag->PacketData, Tag->PacketSize, Tag ) )
										{
											KeQuerySystemTime ( &Tag->FirstSendTime );
											KeQuerySystemTime ( &Tag->SendTime );
											AoE_Globals_OutstandingTags++;
											Sends++;
										}
									else
										{
											Fails++;
											Tag->Id = 0;
											break;
										}
								}
						}
					else
						{
							KeQuerySystemTime ( &CurrentTime );
							if ( CurrentTime.QuadPart >
									 ( Tag->SendTime.QuadPart +
										 ( LONGLONG ) ( Tag->DeviceExtension->Disk.AoE.Timeout *
																		2 ) ) )
								{
									if ( Protocol_Send
											 ( Tag->DeviceExtension->Disk.AoE.ClientMac,
												 Tag->DeviceExtension->Disk.AoE.ServerMac,
												 ( PUCHAR ) Tag->PacketData, Tag->PacketSize, Tag ) )
										{
											KeQuerySystemTime ( &Tag->SendTime );
											Tag->DeviceExtension->Disk.AoE.Timeout +=
												Tag->DeviceExtension->Disk.AoE.Timeout / 1000;
											if ( Tag->DeviceExtension->Disk.AoE.Timeout > 100000000 )
												Tag->DeviceExtension->Disk.AoE.Timeout = 100000000;
											Resends++;
										}
									else
										{
											ResendFails++;
											break;
										}
								}
						}
					Tag = Tag->Next;
					if ( Tag == AoE_Globals_TagList )
						{
							DBG ( "Taglist Cyclic!!\n" );
							break;
						}
				}
			KeReleaseSpinLock ( &AoE_Globals_SpinLock, Irql );
		}
}
