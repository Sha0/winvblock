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

extern NTSTATUS STDCALL ZwWaitForSingleObject ( IN HANDLE Handle,
					 IN BOOLEAN Alertable,
					 IN PLARGE_INTEGER Timeout OPTIONAL );

/* In this file */
static VOID STDCALL Thread ( IN PVOID StartContext );

#ifdef _MSC_VER
#pragma pack(1)
#endif

/** Tag types */
typedef enum { RequestType, SearchDriveType } TAGTYPE, *PTAGTYPE;

/** AoE packet */
typedef struct _AOE {
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
    union {
	UCHAR Err;
	UCHAR Feature;
    };
    UCHAR Count;
    union {
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
} __attribute__ ( ( __packed__ ) ) AOE, *PAOE;

#ifdef _MSC_VER
#pragma pack()
#endif

/** A request */
typedef struct _REQUEST {
    REQUESTMODE Mode;
    ULONG SectorCount;
    PUCHAR Buffer;
    PIRP Irp;
    ULONG TagCount;
    ULONG TotalTags;
} REQUEST, *PREQUEST;

/** A tag */
typedef struct _TAG {
    TAGTYPE Type;
    PDEVICEEXTENSION DeviceExtension;
    PREQUEST Request;
    ULONG Id;
    PAOE PacketData;
    ULONG PacketSize;
    LARGE_INTEGER FirstSendTime;
    LARGE_INTEGER SendTime;
    ULONG BufferOffset;
    ULONG SectorCount;
    struct _TAG *Next;
    struct _TAG *Previous;
} TAG, *PTAG;

/** A disk search */
typedef struct _DISKSEARCH {
    PDEVICEEXTENSION DeviceExtension;
    PTAG Tag;
    struct _DISKSEARCH *Next;
} DISKSEARCH, *PDISKSEARCH;

/** Globals */
static BOOLEAN Stop = FALSE;
static KSPIN_LOCK SpinLock;
static KEVENT ThreadSignalEvent;
static PTAG TagList = NULL;
static PTAG TagListLast = NULL;
static PTAG ProbeTag = NULL;
static PDISKSEARCH DiskSearchList = NULL;
static LONG OutstandingTags = 0;
static HANDLE ThreadHandle;

/**
 * Start AoE operations
 *
 * @ret Status		Return status code
 */
NTSTATUS STDCALL AoEStart (  )
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PVOID ThreadObject;

    DBG ( "Entry\n" );

    /* Allocate and zero-fill the global probe tag */
    if ( ( ProbeTag =
	   ( PTAG ) ExAllocatePool ( NonPagedPool, sizeof ( TAG ) ) )
	 == NULL ) {
	DBG ( "Couldn't allocate ProbeTag; bye!\n" );
	return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory ( ProbeTag, sizeof ( TAG ) );

    /* Set up the probe tag's AoE packet reference */
    ProbeTag->PacketSize = sizeof ( AOE );
    /* Allocate and zero-fill the probe tag's packet reference */
    if ( ( ProbeTag->PacketData = ( PAOE ) ExAllocatePool ( NonPagedPool,
							    ProbeTag->PacketSize ) )
	 == NULL ) {
	DBG ( "Couldn't allocate ProbeTag->PacketData\n" );
	ExFreePool ( ProbeTag );
	return STATUS_INSUFFICIENT_RESOURCES;
    }
    ProbeTag->SendTime.QuadPart = 0LL;
    RtlZeroMemory ( ProbeTag->PacketData, ProbeTag->PacketSize );

    /* Initialize the probe tag's AoE packet */
    ProbeTag->PacketData->Ver = AOEPROTOCOLVER;
    ProbeTag->PacketData->Major = htons ( ( USHORT ) - 1 );
    ProbeTag->PacketData->Minor = ( UCHAR ) - 1;
    ProbeTag->PacketData->Cmd = 0xec;	/* IDENTIFY DEVICE */
    ProbeTag->PacketData->Count = 1;

    /* Initialize global spin-lock and global thread signal event */
    KeInitializeSpinLock ( &SpinLock );
    KeInitializeEvent ( &ThreadSignalEvent, SynchronizationEvent, FALSE );

    /* Initialize object attributes */
    InitializeObjectAttributes ( &ObjectAttributes, NULL, OBJ_KERNEL_HANDLE,
				 NULL, NULL );

    /* Create global thread */
    if ( !NT_SUCCESS ( Status = PsCreateSystemThread ( &ThreadHandle,
						       THREAD_ALL_ACCESS,
						       &ObjectAttributes,
						       NULL, NULL, Thread,
						       NULL ) ) )
	return Error ( "PsCreateSystemThread", Status );

    if ( !NT_SUCCESS ( Status = ObReferenceObjectByHandle ( ThreadHandle,
							    THREAD_ALL_ACCESS,
							    NULL, KernelMode,
							    &ThreadObject,
							    NULL ) ) ) {
	ZwClose ( ThreadHandle );
	Error ( "ObReferenceObjectByHandle", Status );
	Stop = TRUE;
	KeSetEvent ( &ThreadSignalEvent, 0, FALSE );
    }

    return Status;
}

/**
 * Stop AoE operations
 */
VOID STDCALL AoEStop (  )
{
    NTSTATUS Status;
    PDISKSEARCH DiskSearch, PreviousDiskSearch;
    PTAG Tag;
    KIRQL Irql;

    DBG ( "Entry\n" );

    /* If we're not already shutting down, signal the event */
    if ( !Stop ) {
	Stop = TRUE;
	KeSetEvent ( &ThreadSignalEvent, 0, FALSE );
	/* Wait until the event has been signalled */
	if ( !NT_SUCCESS
	     ( Status = ZwWaitForSingleObject ( ThreadHandle, FALSE, NULL ) ) )
	    Error ( "AoEStop ZwWaitForSingleObject", Status );
	ZwClose ( ThreadHandle );
    }

    /* Wait until we have the global spin-lock */
    KeAcquireSpinLock ( &SpinLock, &Irql );

    /* Free disk searches in the global disk search list */
    DiskSearch = DiskSearchList;
    while ( DiskSearch != NULL ) {
	KeSetEvent ( &DiskSearch->DeviceExtension->Disk.SearchEvent, 0,
		     FALSE );
	PreviousDiskSearch = DiskSearch;
	DiskSearch = DiskSearch->Next;
	ExFreePool ( PreviousDiskSearch );
    }

    /* Cancel and free all tags in the global tag list */
    Tag = TagList;
    while ( Tag != NULL ) {
	if ( Tag->Request != NULL && --Tag->Request->TagCount == 0 ) {
	    Tag->Request->Irp->IoStatus.Information = 0;
	    Tag->Request->Irp->IoStatus.Status = STATUS_CANCELLED;
	    IoCompleteRequest ( Tag->Request->Irp, IO_NO_INCREMENT );
	    ExFreePool ( Tag->Request );
	}
	if ( Tag->Next == NULL ) {
	    ExFreePool ( Tag->PacketData );
	    ExFreePool ( Tag );
	    Tag = NULL;
	} else {
	    Tag = Tag->Next;
	    ExFreePool ( Tag->Previous->PacketData );
	    ExFreePool ( Tag->Previous );
	}
    }
    TagList = NULL;
    TagListLast = NULL;

    /* Free the global probe tag and its AoE packet */
    ExFreePool ( ProbeTag->PacketData );
    ExFreePool ( ProbeTag );

    /* Release the global spin-lock */
    KeReleaseSpinLock ( &SpinLock, Irql );
}

/**
 * Search for disk parameters
 *
 * @v DeviceExtension		The device extension for the disk
 *
 * Returns TRUE if the disk could be matched, FALSE otherwise.
 */
BOOLEAN STDCALL AoESearchDrive ( IN PDEVICEEXTENSION DeviceExtension )
{
    PDISKSEARCH DiskSearch, DiskSearchWalker, PreviousDiskSearch;
    LARGE_INTEGER Timeout, CurrentTime;
    PTAG Tag, TagWalker;
    KIRQL Irql, InnerIrql;
    LARGE_INTEGER MaxSectorsPerPacketSendTime;
    ULONG MTU;

    /* Allocate our disk search */
    if ( ( DiskSearch = ( PDISKSEARCH ) ExAllocatePool ( NonPagedPool,
							 sizeof
							 ( DISKSEARCH ) ) )
	 == NULL ) {
	DBG ( "Couldn't allocate DiskSearch; bye!\n" );
	return FALSE;
    }

    /* Initialize the disk search */
    DiskSearch->DeviceExtension = DeviceExtension;
    DiskSearch->Next = NULL;
    DeviceExtension->Disk.SearchState = SearchNIC;

    KeResetEvent ( &DeviceExtension->Disk.SearchEvent );

    /* Wait until we have the global spin-lock */
    KeAcquireSpinLock ( &SpinLock, &Irql );

    /* Add our disk search to the global list of disk searches */
    if ( DiskSearchList == NULL ) {
	DiskSearchList = DiskSearch;
    } else {
	DiskSearchWalker = DiskSearchList;
	while ( DiskSearchWalker->Next )
	    DiskSearchWalker = DiskSearchWalker->Next;
	DiskSearchWalker->Next = DiskSearch;
    }

    /* Release the global spin-lock */
    KeReleaseSpinLock ( &SpinLock, Irql );

    /* We go through all the states until the disk is ready for use */
    while ( TRUE ) {
	/* Wait for our device's extension's search to be signalled */
	/* TODO: Make the below value a #defined constant */
	/* 500.000 * 100ns = 50.000.000 ns = 50ms */
	Timeout.QuadPart = -500000LL;
	KeWaitForSingleObject ( &DeviceExtension->Disk.SearchEvent, Executive,
				KernelMode, FALSE, &Timeout );
	if ( Stop ) {
	    DBG ( "AoE is shutting down; bye!\n" );
	    return FALSE;
	}

	/* Wait until we have the device extension's spin-lock */
	KeAcquireSpinLock ( &DeviceExtension->Disk.SpinLock, &Irql );

	if ( DeviceExtension->Disk.SearchState == SearchNIC ) {
	    if ( !ProtocolSearchNIC ( DeviceExtension->Disk.ClientMac ) ) {
		KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
		continue;
	    } else {
		/* We found the adapter to use, get MTU next */
		DeviceExtension->Disk.MTU =
		    ProtocolGetMTU ( DeviceExtension->Disk.ClientMac );
		DeviceExtension->Disk.SearchState = GetSize;
	    }
	}

	if ( DeviceExtension->Disk.SearchState == GettingSize ) {
	    /* Still getting the disk's size */
	    KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
	    continue;
	}
	if ( DeviceExtension->Disk.SearchState == GettingGeometry ) {
	    /* Still getting the disk's geometry */
	    KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
	    continue;
	}
	if ( DeviceExtension->Disk.SearchState == GettingMaxSectorsPerPacket ) {
	    KeQuerySystemTime ( &CurrentTime );
	    /* TODO: Make the below value a #defined constant */
	    /* 2.500.000 * 100ns = 250.000.000 ns = 250ms */
	    if ( CurrentTime.QuadPart >
		 MaxSectorsPerPacketSendTime.QuadPart + 2500000LL ) {
		DBG ( "No reply after 250ms for MaxSectorsPerPacket %d, "
		      "giving up\n",
		      DeviceExtension->Disk.MaxSectorsPerPacket );
		DeviceExtension->Disk.MaxSectorsPerPacket--;
		DeviceExtension->Disk.SearchState = Done;
	    } else {
		/* Still getting the maximum sectors per packet count */
		KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
		continue;
	    }
	}

	if ( DeviceExtension->Disk.SearchState == Done ) {
	    /* We've finished the disk search; perform clean-up */
	    KeAcquireSpinLock ( &SpinLock, &InnerIrql );

	    /* Tag clean-up: Find out if our tag is in the global tag list */
	    TagWalker = TagList;
	    while ( TagWalker != NULL && TagWalker != Tag )
		TagWalker = TagWalker->Next;
	    if ( TagWalker != NULL ) {
		/* We found it.  If it's at the beginning of the list, adjust
		 * the list to point the the next tag
		 */
		if ( Tag->Previous == NULL )
		    TagList = Tag->Next;
		else
		    /* Remove our tag from the list */
		    Tag->Previous->Next = Tag->Next;
		/* If we 're at the end of the list, adjust the list's end to
		 * point to the penultimate tag
		 */
		if ( Tag->Next == NULL )
		    TagListLast = Tag->Previous;
		else
		    /* Remove our tag from the list */
		    Tag->Next->Previous = Tag->Previous;
		OutstandingTags--;
		if ( OutstandingTags < 0 )
		    DBG ( "OutstandingTags < 0!!\n" );
		/* Free our tag and its AoE packet */
		ExFreePool ( Tag->PacketData );
		ExFreePool ( Tag );
	    }

	    /* Disk search clean-up */
	    if ( DiskSearchList == NULL ) {
		DBG ( "DiskSearchList == NULL!!\n" );
	    } else {
		/* Find our disk search in the global list of disk searches */
		DiskSearchWalker = DiskSearchList;
		while ( DiskSearchWalker &&
			DiskSearchWalker->DeviceExtension !=
			DeviceExtension ) {
		    PreviousDiskSearch = DiskSearchWalker;
		    DiskSearchWalker = DiskSearchWalker->Next;
		}
		if ( DiskSearchWalker ) {
		    /* We found our disk search.  If it's the first one in
		     * the list, adjust the list and remove it
		     */
		    if ( DiskSearchWalker == DiskSearchList )
			DiskSearchList = DiskSearchWalker->Next;
		    else
			/* Just remove it */
			PreviousDiskSearch->Next = DiskSearchWalker->Next;
		    /* Free our disk search */
		    ExFreePool ( DiskSearchWalker );
		} else {
		    DBG ( "Disk not found in DiskSearchList!!\n" );
		}
	    }

	    /* Release global and device extension spin-locks */
	    KeReleaseSpinLock ( &SpinLock, InnerIrql );
	    KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );

	    DBG ( "Disk size: %I64uM cylinders: %I64u heads: %u "
		  "sectors: %u sectors per packet: %u\n",
		  DeviceExtension->Disk.LBADiskSize / 2048,
		  DeviceExtension->Disk.Cylinders,
		  DeviceExtension->Disk.Heads,
		  DeviceExtension->Disk.Sectors,
		  DeviceExtension->Disk.MaxSectorsPerPacket );
	    return TRUE;
	}

	/* if ( DeviceExtension->Disk.SearchState == Done ) */
	/* Establish our tag */
	if ( ( Tag = ( PTAG ) ExAllocatePool ( NonPagedPool, sizeof ( TAG ) ) )
	     == NULL ) {
	    DBG ( "Couldn't allocate Tag\n" );
	    KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
	    /* Maybe next time around */
	    continue;
	}
	RtlZeroMemory ( Tag, sizeof ( TAG ) );
	Tag->Type = SearchDriveType;
	Tag->DeviceExtension = DeviceExtension;

	/* Establish our tag's AoE packet */
	Tag->PacketSize = sizeof ( AOE );
	if ( ( Tag->PacketData = ( PAOE ) ExAllocatePool ( NonPagedPool,
							   Tag->PacketSize ) )
	     == NULL ) {
	    DBG ( "Couldn't allocate Tag->PacketData\n" );
	    ExFreePool ( Tag );
	    Tag = NULL;
	    KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
	    /* Maybe next time around */
	    continue;
	}
	RtlZeroMemory ( Tag->PacketData, Tag->PacketSize );
	Tag->PacketData->Ver = AOEPROTOCOLVER;
	Tag->PacketData->Major =
	    htons ( ( USHORT ) DeviceExtension->Disk.Major );
	Tag->PacketData->Minor = ( UCHAR ) DeviceExtension->Disk.Minor;
	Tag->PacketData->ExtendedAFlag = TRUE;

	/* Initialize the packet appropriately based on our current phase */
	switch ( DeviceExtension->Disk.SearchState ) {
	case GetSize:
	    /* TODO: Make the below value into a #defined constant */
	    Tag->PacketData->Cmd = 0xec;	/* IDENTIFY DEVICE */
	    Tag->PacketData->Count = 1;
	    DeviceExtension->Disk.SearchState = GettingSize;
	    break;
	case GetGeometry:
	    /* TODO: Make the below value into a #defined constant */
	    Tag->PacketData->Cmd = 0x24;	/* READ SECTOR */
	    Tag->PacketData->Count = 1;
	    DeviceExtension->Disk.SearchState = GettingGeometry;
	    break;
	case GetMaxSectorsPerPacket:
	    /* TODO: Make the below value into a #defined constant */
	    Tag->PacketData->Cmd = 0x24;	/* READ SECTOR */
	    Tag->PacketData->Count =
		( UCHAR ) ( ++DeviceExtension->Disk.MaxSectorsPerPacket );
	    KeQuerySystemTime ( &MaxSectorsPerPacketSendTime );
	    DeviceExtension->Disk.SearchState = GettingMaxSectorsPerPacket;
	    /* TODO: Make the below value into a #defined constant */
	    DeviceExtension->Disk.Timeout = 200000;
	    break;
	default:
	    DBG ( "Undefined SearchState!!\n" );
	    ExFreePool ( Tag->PacketData );
	    ExFreePool ( Tag );
	    /* TODO: Do we need to nullify Tag here? */
	    KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
	    continue;
	    break;
	}

	/* Enqueue our tag */
	Tag->Next = NULL;
	KeAcquireSpinLock ( &SpinLock, &InnerIrql );
	if ( TagList == NULL ) {
	    TagList = Tag;
	    Tag->Previous = NULL;
	} else {
	    TagListLast->Next = Tag;
	    Tag->Previous = TagListLast;
	}
	TagListLast = Tag;
	KeReleaseSpinLock ( &SpinLock, InnerIrql );
	KeReleaseSpinLock ( &DeviceExtension->Disk.SpinLock, Irql );
    }
}

/**
 * I/O Request
 *
 * @v DeviceExtension	The device extension for the disk
 * @v Mode		        Read / write mode
 * @v StartSector		First sector for request
 * @v SectorCount		Number of sectors to work with
 * @v Buffer			Buffer to read / write sectors to / from
 * @v Irp			    Interrupt request packet for this request
 */
NTSTATUS STDCALL AoERequest ( IN PDEVICEEXTENSION DeviceExtension,
			      IN REQUESTMODE Mode, IN LONGLONG StartSector,
			      IN ULONG SectorCount, IN PUCHAR Buffer,
			      IN PIRP Irp )
{
    PREQUEST Request;
    PTAG Tag, NewTagList = NULL, PreviousTag = NULL;
    KIRQL Irql;
    ULONG i;

    if ( Stop ) {
	/* Shutting down AoE; we can't service this request */
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_CANCELLED;
	IoCompleteRequest ( Irp, IO_NO_INCREMENT );
	return STATUS_CANCELLED;
    }

    if ( SectorCount < 1 ) {
	/* A silly request */
	DBG ( "SectorCount < 1; cancelling\n" );
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_CANCELLED;
	IoCompleteRequest ( Irp, IO_NO_INCREMENT );
	return STATUS_CANCELLED;
    }

    /* Allocate and zero-fill our request */
    if ( ( Request = ( PREQUEST ) ExAllocatePool ( NonPagedPool,
						   sizeof ( REQUEST ) ) )
	 == NULL ) {
	DBG ( "Couldn't allocate Request; bye!\n" );
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
	IoCompleteRequest ( Irp, IO_NO_INCREMENT );
	return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory ( Request, sizeof ( REQUEST ) );

    /* Initialize the request */
    Request->Mode = Mode;
    Request->SectorCount = SectorCount;
    Request->Buffer = Buffer;
    Request->Irp = Irp;
    Request->TagCount = 0;

    /* Split the requested sectors into packets in tags */
    for ( i = 0; i < SectorCount;
	  i += DeviceExtension->Disk.MaxSectorsPerPacket ) {
	/* Allocate each tag */
	if ( ( Tag = ( PTAG ) ExAllocatePool ( NonPagedPool, sizeof ( TAG ) ) )
	     == NULL ) {
	    DBG ( "Couldn't allocate Tag; bye!\n" );
	    /* We failed while allocating tags; free the ones we built */
	    Tag = NewTagList;
	    while ( Tag != NULL ) {
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

	/* Initialize each tag */
	RtlZeroMemory ( Tag, sizeof ( TAG ) );
	Tag->Type = RequestType;
	Tag->Request = Request;
	Tag->DeviceExtension = DeviceExtension;
	Request->TagCount++;
	Tag->Id = 0;
	Tag->BufferOffset = i * SECTORSIZE;
	Tag->SectorCount = ( ( SectorCount - i ) <
			     DeviceExtension->Disk.MaxSectorsPerPacket ?
			     SectorCount - i :
			     DeviceExtension->Disk.MaxSectorsPerPacket );

	/* Allocate and initialize each tag's AoE packet */
	Tag->PacketSize = sizeof ( AOE );
	if ( Mode == Write )
	    Tag->PacketSize += Tag->SectorCount * SECTORSIZE;
	if ( ( Tag->PacketData =
	       ( PAOE ) ExAllocatePool ( NonPagedPool,
					 Tag->PacketSize ) ) == NULL ) {
	    DBG ( "Couldn't allocate Tag->PacketData; bye!\n" );
	    /* We failed while allocating an AoE packet; free
	     * the tags we built
	     */
	    ExFreePool ( Tag );
	    Tag = NewTagList;
	    while ( Tag != NULL ) {
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
	    htons ( ( USHORT ) DeviceExtension->Disk.Major );
	Tag->PacketData->Minor = ( UCHAR ) DeviceExtension->Disk.Minor;
	Tag->PacketData->Tag = 0;
	Tag->PacketData->Command = 0;
	Tag->PacketData->ExtendedAFlag = TRUE;
	if ( Mode == Read ) {
	    Tag->PacketData->Cmd = 0x24;	/* READ SECTOR */
	} else {
	    Tag->PacketData->Cmd = 0x34;	/* WRITE SECTOR */
	    Tag->PacketData->WriteAFlag = 1;
	}
	Tag->PacketData->Count = ( UCHAR ) Tag->SectorCount;
	Tag->PacketData->Lba0 =
	    ( UCHAR ) ( ( ( StartSector + i ) >> 0 ) & 255 );
	Tag->PacketData->Lba1 =
	    ( UCHAR ) ( ( ( StartSector + i ) >> 8 ) & 255 );
	Tag->PacketData->Lba2 =
	    ( UCHAR ) ( ( ( StartSector + i ) >> 16 ) & 255 );
	Tag->PacketData->Lba3 =
	    ( UCHAR ) ( ( ( StartSector + i ) >> 24 ) & 255 );
	Tag->PacketData->Lba4 =
	    ( UCHAR ) ( ( ( StartSector + i ) >> 32 ) & 255 );
	Tag->PacketData->Lba5 =
	    ( UCHAR ) ( ( ( StartSector + i ) >> 40 ) & 255 );

	/* For a write request, copy from the buffer into the AoE packet */
	if ( Mode == Write )
	    RtlCopyMemory ( Tag->PacketData->Data, &Buffer[Tag->BufferOffset],
			    Tag->SectorCount * SECTORSIZE );

	/* Add this tag to the request's tag list */
	Tag->Previous = PreviousTag;
	Tag->Next = NULL;
	if ( NewTagList == NULL ) {
	    NewTagList = Tag;
	} else {
	    PreviousTag->Next = Tag;
	}
	PreviousTag = Tag;
    }				/* Split the requested sectors into packets in tags */
    Request->TotalTags = Request->TagCount;

    /* Wait until we have the global spin-lock */
    KeAcquireSpinLock ( &SpinLock, &Irql );

    /* Enqueue our request's tag list to the global tag list */
    if ( TagListLast == NULL ) {
	TagList = NewTagList;
    } else {
	TagListLast->Next = NewTagList;
	NewTagList->Previous = TagListLast;
    }
    /* Adjust the global list to reflect our last tag */
    TagListLast = Tag;

    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_PENDING;
    IoMarkIrpPending ( Irp );

    KeReleaseSpinLock ( &SpinLock, Irql );
    KeSetEvent ( &ThreadSignalEvent, 0, FALSE );
    return STATUS_PENDING;
}

/**
 * Process an AoE reply
 *
 * @v SourceMac			The AoE server's MAC address
 * @v DestinationMac		The AoE client's MAC address
 * @v Data			The AoE packet
 * @v DataSize			The AoE packet's size
 */
NTSTATUS STDCALL AoEReply ( IN PUCHAR SourceMac, IN PUCHAR DestinationMac,
			    IN PUCHAR Data, IN UINT DataSize )
{
    PAOE Reply = ( PAOE ) Data;
    LONGLONG LBASize;
    PTAG Tag;
    KIRQL Irql;
    BOOLEAN Found = FALSE;
    LARGE_INTEGER CurrentTime;

    /* Discard non-responses */
    if ( !Reply->ResponseFlag )
	return STATUS_SUCCESS;

    /* If the response matches our probe, add the AoE disk device */
    if ( ProbeTag->Id == Reply->Tag ) {
	RtlCopyMemory ( &LBASize, &Reply->Data[200], sizeof ( LONGLONG ) );
	BusAddTarget ( DestinationMac, SourceMac, ntohs ( Reply->Major ),
		       Reply->Minor, LBASize );
	return STATUS_SUCCESS;
    }

    /* Wait until we have the global spin-lock */
    KeAcquireSpinLock ( &SpinLock, &Irql );

    /* Search for request tag */
    if ( TagList == NULL ) {
	KeReleaseSpinLock ( &SpinLock, Irql );
	return STATUS_SUCCESS;
    }
    Tag = TagList;
    while ( Tag != NULL ) {
	if ( ( Tag->Id == Reply->Tag ) &&
	     ( Tag->PacketData->Major == Reply->Major ) &&
	     ( Tag->PacketData->Minor == Reply->Minor ) ) {
	    Found = TRUE;
	    break;
	}
	Tag = Tag->Next;
    }
    if ( !Found ) {
	KeReleaseSpinLock ( &SpinLock, Irql );
	return STATUS_SUCCESS;
    } else {
	/* Remove the tag from the global tag list */
	if ( Tag->Previous == NULL )
	    TagList = Tag->Next;
	else
	    Tag->Previous->Next = Tag->Next;
	if ( Tag->Next == NULL )
	    TagListLast = Tag->Previous;
	else
	    Tag->Next->Previous = Tag->Previous;
	OutstandingTags--;
	if ( OutstandingTags < 0 )
	    DBG ( "OutstandingTags < 0!!\n" );
	KeSetEvent ( &ThreadSignalEvent, 0, FALSE );
    }
    KeReleaseSpinLock ( &SpinLock, Irql );

    /* If our tag was a discovery request, note the server */
    if ( RtlCompareMemory ( Tag->DeviceExtension->Disk.ServerMac,
			    "\xff\xff\xff\xff\xff\xff", 6 ) == 6 ) {
	RtlCopyMemory ( Tag->DeviceExtension->Disk.ServerMac, SourceMac, 6 );
	DBG ( "Major: %d minor: %d found on server "
	      "%02x:%02x:%02x:%02x:%02x:%02x\n",
	      Tag->DeviceExtension->Disk.Major,
	      Tag->DeviceExtension->Disk.Minor, SourceMac[0],
	      SourceMac[1], SourceMac[2], SourceMac[3], SourceMac[4],
	      SourceMac[5] );
    }

    KeQuerySystemTime ( &CurrentTime );
    Tag->DeviceExtension->Disk.Timeout -=
	( ULONG ) ( ( Tag->DeviceExtension->Disk.Timeout -
		      ( CurrentTime.QuadPart - Tag->FirstSendTime.QuadPart )
		     ) / 1024 );
    /* TODO: Replace the values below with #defined constants */
    if ( Tag->DeviceExtension->Disk.Timeout > 100000000 )
	Tag->DeviceExtension->Disk.Timeout = 100000000;

    switch ( Tag->Type ) {
    case SearchDriveType:
	KeAcquireSpinLock ( &Tag->DeviceExtension->Disk.SpinLock, &Irql );
	switch ( Tag->DeviceExtension->Disk.SearchState ) {
	case GettingSize:
	    /* The reply tells us the disk size */
	    RtlCopyMemory ( &Tag->DeviceExtension->Disk.LBADiskSize,
			    &Reply->Data[200], sizeof ( LONGLONG ) );
	    /* Next we are concerned with the disk geometry */
	    Tag->DeviceExtension->Disk.SearchState = GetGeometry;
	    break;
	case GettingGeometry:
	    /* FIXME: use real values from partition table */
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
	    /* Next we are concerned with the maximum sectors per packet */
	    Tag->DeviceExtension->Disk.SearchState = GetMaxSectorsPerPacket;
	    break;
	case GettingMaxSectorsPerPacket:
	    DataSize -= sizeof ( AOE );
	    if ( DataSize < ( Tag->DeviceExtension->Disk.MaxSectorsPerPacket *
			      SECTORSIZE ) ) {
		DBG ( "Packet size too low while getting "
		      "MaxSectorsPerPacket (tried %d, got size of %d)\n",
		      Tag->DeviceExtension->Disk.MaxSectorsPerPacket,
		      DataSize );
		Tag->DeviceExtension->Disk.MaxSectorsPerPacket--;
		Tag->DeviceExtension->Disk.SearchState = Done;
	    } else if ( Tag->DeviceExtension->Disk.MTU <
			( sizeof ( AOE ) +
			  ( ( Tag->DeviceExtension->Disk.MaxSectorsPerPacket +
			      1 ) * SECTORSIZE ) ) ) {
		DBG ( "Got MaxSectorsPerPacket %d at size of %d. "
		      "MTU of %d reached\n",
		      Tag->DeviceExtension->Disk.MaxSectorsPerPacket, DataSize,
		      Tag->DeviceExtension->Disk.MTU );
		Tag->DeviceExtension->Disk.SearchState = Done;
	    } else {
		DBG ( "Got MaxSectorsPerPacket %d at size of %d, "
		      "trying next...\n",
		      Tag->DeviceExtension->Disk.MaxSectorsPerPacket,
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
    case RequestType:
	/* If the reply is in response to a read request, get our data! */
	if ( Tag->Request->Mode == Read )
	    RtlCopyMemory ( &Tag->Request->Buffer[Tag->BufferOffset],
			    Reply->Data, Tag->SectorCount * SECTORSIZE );
	/* If this is the last reply expected for the read request,
	 * complete the IRP and free the request
	 */
	if ( InterlockedDecrement ( &Tag->Request->TagCount ) == 0 ) {
	    Tag->Request->Irp->IoStatus.Information =
		Tag->Request->SectorCount * SECTORSIZE;
	    Tag->Request->Irp->IoStatus.Status = STATUS_SUCCESS;
	    CompletePendingIrp ( Tag->Request->Irp );
	    ExFreePool ( Tag->Request );
	}
	break;
    default:
	DBG ( "Unknown tag type!!\n" );
	break;
    }

    KeSetEvent ( &ThreadSignalEvent, 0, FALSE );
    ExFreePool ( Tag->PacketData );
    ExFreePool ( Tag );
    return STATUS_SUCCESS;
}

VOID STDCALL AoEResetProbe (  )
{
    ProbeTag->SendTime.QuadPart = 0LL;
}

static VOID STDCALL Thread ( IN PVOID StartContext )
{
    LARGE_INTEGER Timeout, CurrentTime, ProbeTime, ReportTime;
    ULONG NextTagId = 1;
    PTAG Tag;
    KIRQL Irql;
    ULONG Sends = 0;
    ULONG Resends = 0;
    ULONG ResendFails = 0;
    ULONG Fails = 0;
    ULONG RequestTimeout = 0;

    DBG ( "Entry\n" );
    ReportTime.QuadPart = 0LL;
    ProbeTime.QuadPart = 0LL;

    while ( TRUE ) {
	/* TODO: Make the below value a #defined constant */
	Timeout.QuadPart = -100000LL;	/* 100.000 * 100ns = 10.000.000 ns = 10ms */
	KeWaitForSingleObject ( &ThreadSignalEvent, Executive, KernelMode,
				FALSE, &Timeout );
	KeResetEvent ( &ThreadSignalEvent );
	if ( Stop ) {
	    DBG ( "Stopping...\n" );
	    PsTerminateSystemThread ( STATUS_SUCCESS );
	}
	BusCleanupTargetList (  );

	KeQuerySystemTime ( &CurrentTime );
	/* TODO: Make the below value a #defined constant */
	if ( CurrentTime.QuadPart > ( ReportTime.QuadPart + 10000000LL ) ) {
	    DBG ( "Sends: %d  Resends: %d  ResendFails: %d  Fails: %d  "
		  "OutstandingTags: %d  RequestTimeout: %d\n", Sends,
		  Resends, ResendFails, Fails, OutstandingTags,
		  RequestTimeout );
	    Sends = 0;
	    Resends = 0;
	    ResendFails = 0;
	    Fails = 0;
	    KeQuerySystemTime ( &ReportTime );
	}

	/* TODO: Make the below value a #defined constant */
	if ( CurrentTime.QuadPart >
	     ( ProbeTag->SendTime.QuadPart + 100000000LL ) ) {
	    ProbeTag->Id = NextTagId++;
	    if ( NextTagId == 0 )
		NextTagId++;
	    ProbeTag->PacketData->Tag = ProbeTag->Id;
	    ProtocolSend ( "\xff\xff\xff\xff\xff\xff",
			   "\xff\xff\xff\xff\xff\xff",
			   ( PUCHAR ) ProbeTag->PacketData,
			   ProbeTag->PacketSize, NULL );
	    KeQuerySystemTime ( &ProbeTag->SendTime );
	}

	KeAcquireSpinLock ( &SpinLock, &Irql );
	if ( TagList == NULL ) {
	    KeReleaseSpinLock ( &SpinLock, Irql );
	    continue;
	}
	Tag = TagList;
	while ( Tag != NULL ) {
	    RequestTimeout = Tag->DeviceExtension->Disk.Timeout;
	    if ( Tag->Id == 0 ) {
		if ( OutstandingTags <= 64 ) {
		    /* if ( OutstandingTags <= 102400 ) { */
		    if ( OutstandingTags < 0 )
			DBG ( "OutstandingTags < 0!!\n" );
		    Tag->Id = NextTagId++;
		    if ( NextTagId == 0 )
			NextTagId++;
		    Tag->PacketData->Tag = Tag->Id;
		    if ( ProtocolSend ( Tag->DeviceExtension->Disk.ClientMac,
					Tag->DeviceExtension->Disk.ServerMac,
					( PUCHAR ) Tag->PacketData,
					Tag->PacketSize, Tag ) ) {
			KeQuerySystemTime ( &Tag->FirstSendTime );
			KeQuerySystemTime ( &Tag->SendTime );
			OutstandingTags++;
			Sends++;
		    } else {
			Fails++;
			Tag->Id = 0;
			break;
		    }
		}
	    } else {
		KeQuerySystemTime ( &CurrentTime );
		if ( CurrentTime.QuadPart >
		     ( Tag->SendTime.QuadPart +
		       ( LONGLONG ) ( Tag->DeviceExtension->Disk.Timeout *
				      2 ) ) ) {
		    if ( ProtocolSend
			 ( Tag->DeviceExtension->Disk.ClientMac,
			   Tag->DeviceExtension->Disk.ServerMac,
			   ( PUCHAR ) Tag->PacketData, Tag->PacketSize,
			   Tag ) ) {
			KeQuerySystemTime ( &Tag->SendTime );
			Tag->DeviceExtension->Disk.Timeout +=
			    Tag->DeviceExtension->Disk.Timeout / 1000;
			if ( Tag->DeviceExtension->Disk.Timeout > 100000000 )
			    Tag->DeviceExtension->Disk.Timeout = 100000000;
			Resends++;
		    } else {
			ResendFails++;
			break;
		    }
		}
	    }
	    Tag = Tag->Next;
	    if ( Tag == TagList ) {
		DBG ( "Taglist Cyclic!!\n" );
		break;
	    }
	}
	KeReleaseSpinLock ( &SpinLock, Irql );
    }
}
