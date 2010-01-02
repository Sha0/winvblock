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
 * AoE specifics
 *
 */

#include <stdio.h>
#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "disk.h"
#include "mount.h"
#include "bus.h"
#include "aoe.h"
#include "protocol.h"
#include "debug.h"
#include "bus.h"

extern NTSTATUS STDCALL ZwWaitForSingleObject (
  IN HANDLE Handle,
  IN winvblock__bool Alertable,
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
  winvblock__uint8 ReservedFlag:2;
  winvblock__uint8 ErrorFlag:1;
  winvblock__uint8 ResponseFlag:1;
  winvblock__uint8 Ver:4;
  winvblock__uint8 Error;
  winvblock__uint16 Major;
  winvblock__uint8 Minor;
  winvblock__uint8 Command;
  winvblock__uint32 Tag;

  winvblock__uint8 WriteAFlag:1;
  winvblock__uint8 AsyncAFlag:1;
  winvblock__uint8 Reserved1AFlag:2;
  winvblock__uint8 DeviceHeadAFlag:1;
  winvblock__uint8 Reserved2AFlag:1;
  winvblock__uint8 ExtendedAFlag:1;
  winvblock__uint8 Reserved3AFlag:1;
  union
  {
    winvblock__uint8 Err;
    winvblock__uint8 Feature;
  };
  winvblock__uint8 Count;
  union
  {
    winvblock__uint8 Cmd;
    winvblock__uint8 Status;
  };

  winvblock__uint8 Lba0;
  winvblock__uint8 Lba1;
  winvblock__uint8 Lba2;
  winvblock__uint8 Lba3;
  winvblock__uint8 Lba4;
  winvblock__uint8 Lba5;
  winvblock__uint16 Reserved;

  winvblock__uint8 Data[];
} __attribute__ ( ( __packed__ ) ) AOE_PACKET, *PAOE_PACKET;

#ifdef _MSC_VER
#  pragma pack()
#endif

/** A request */
typedef struct _AOE_REQUEST
{
  disk__io_mode Mode;
  winvblock__uint32 SectorCount;
  winvblock__uint8_ptr Buffer;
  PIRP Irp;
  winvblock__uint32 TagCount;
  winvblock__uint32 TotalTags;
} AOE_REQUEST,
*PAOE_REQUEST;

/** A tag */
typedef struct _AOE_TAG
{
  AOE_TAGTYPE Type;
  driver__dev_ext_ptr DeviceExtension;
  PAOE_REQUEST Request;
  winvblock__uint32 Id;
  PAOE_PACKET PacketData;
  winvblock__uint32 PacketSize;
  LARGE_INTEGER FirstSendTime;
  LARGE_INTEGER SendTime;
  winvblock__uint32 BufferOffset;
  winvblock__uint32 SectorCount;
  struct _AOE_TAG *Next;
  struct _AOE_TAG *Previous;
} AOE_TAG,
*PAOE_TAG;

/** A disk search */
typedef struct _AOE_DISKSEARCH
{
  driver__dev_ext_ptr DeviceExtension;
  PAOE_TAG Tag;
  struct _AOE_DISKSEARCH *Next;
} AOE_DISKSEARCH,
*PAOE_DISKSEARCH;

/** Globals */
static winvblock__bool AoE_Globals_Stop = FALSE;
static KSPIN_LOCK AoE_Globals_SpinLock;
static KEVENT AoE_Globals_ThreadSignalEvent;
static PAOE_TAG AoE_Globals_TagList = NULL;
static PAOE_TAG AoE_Globals_TagListLast = NULL;
static PAOE_TAG AoE_Globals_ProbeTag = NULL;
static PAOE_DISKSEARCH AoE_Globals_DiskSearchList = NULL;
static LONG AoE_Globals_OutstandingTags = 0;
static HANDLE AoE_Globals_ThreadHandle;
static winvblock__bool AoE_Globals_Started = FALSE;

/**
 * Start AoE operations
 *
 * @ret Status		Return status code
 */
NTSTATUS
AoE_Start (
  void
 )
{
  NTSTATUS Status;
  OBJECT_ATTRIBUTES ObjectAttributes;
  PVOID ThreadObject;

  DBG ( "Entry\n" );

  if ( AoE_Globals_Started )
    return STATUS_SUCCESS;
  /*
   * Start up the protocol
   */
  if ( !NT_SUCCESS ( Status = Protocol_Start (  ) ) )
    {
      DBG ( "Protocol startup failure!\n" );
      return Status;
    }
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
  AoE_Globals_ProbeTag->PacketData->Major =
    htons ( ( winvblock__uint16 ) - 1 );
  AoE_Globals_ProbeTag->PacketData->Minor = ( winvblock__uint8 ) - 1;
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

  AoE_Globals_Started = TRUE;
  return Status;
}

/**
 * Stop AoE operations
 */
VOID
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
   * If we're not already started, there's nothing to do
   */
  if ( !AoE_Globals_Started )
    return;
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
      KeSetEvent ( &
		   ( get_disk_ptr ( DiskSearch->DeviceExtension )->
		     SearchEvent ), 0, FALSE );
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
  AoE_Globals_Started = FALSE;
}

/**
 * Search for disk parameters
 *
 * @v DeviceExtension		The device extension for the disk
 *
 * Returns TRUE if the disk could be matched, FALSE otherwise.
 */
winvblock__bool STDCALL
AoE_SearchDrive (
  IN driver__dev_ext_ptr DeviceExtension
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
  winvblock__uint32 MTU;
  disk__type_ptr disk_ptr;
  aoe__disk_type_ptr aoe_disk_ptr;

  /*
   * Establish pointers into the disk device's extension space
   */
  disk_ptr = get_disk_ptr ( DeviceExtension );
  aoe_disk_ptr = aoe__get_disk_ptr ( DeviceExtension );
  if ( !NT_SUCCESS ( AoE_Start (  ) ) )
    {
      DBG ( "AoE startup failure!\n" );
      return FALSE;
    }
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
  disk_ptr->SearchState = SearchNIC;
  KeResetEvent ( &disk_ptr->SearchEvent );

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
      KeWaitForSingleObject ( &disk_ptr->SearchEvent, Executive, KernelMode,
			      FALSE, &Timeout );
      if ( AoE_Globals_Stop )
	{
	  DBG ( "AoE is shutting down; bye!\n" );
	  return FALSE;
	}

      /*
       * Wait until we have the device extension's spin-lock 
       */
      KeAcquireSpinLock ( &disk_ptr->SpinLock, &Irql );

      if ( disk_ptr->SearchState == SearchNIC )
	{
	  if ( !Protocol_SearchNIC ( aoe_disk_ptr->ClientMac ) )
	    {
	      KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
	      continue;
	    }
	  else
	    {
	      /*
	       * We found the adapter to use, get MTU next 
	       */
	      aoe_disk_ptr->MTU = Protocol_GetMTU ( aoe_disk_ptr->ClientMac );
	      disk_ptr->SearchState = GetSize;
	    }
	}

      if ( disk_ptr->SearchState == GettingSize )
	{
	  /*
	   * Still getting the disk's size 
	   */
	  KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
	  continue;
	}
      if ( disk_ptr->SearchState == GettingGeometry )
	{
	  /*
	   * Still getting the disk's geometry 
	   */
	  KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
	  continue;
	}
      if ( disk_ptr->SearchState == GettingMaxSectorsPerPacket )
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
		    "giving up\n", aoe_disk_ptr->MaxSectorsPerPacket );
	      aoe_disk_ptr->MaxSectorsPerPacket--;
	      disk_ptr->SearchState = Done;
	    }
	  else
	    {
	      /*
	       * Still getting the maximum sectors per packet count 
	       */
	      KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
	      continue;
	    }
	}

      if ( disk_ptr->SearchState == Done )
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
	  KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );

	  DBG ( "Disk size: %I64uM cylinders: %I64u heads: %u "
		"sectors: %u sectors per packet: %u\n",
		disk_ptr->LBADiskSize / 2048, disk_ptr->Cylinders,
		disk_ptr->Heads, disk_ptr->Sectors,
		aoe_disk_ptr->MaxSectorsPerPacket );
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
	  KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
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
	  KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
	  /*
	   * Maybe next time around 
	   */
	  continue;
	}
      RtlZeroMemory ( Tag->PacketData, Tag->PacketSize );
      Tag->PacketData->Ver = AOEPROTOCOLVER;
      Tag->PacketData->Major =
	htons ( ( winvblock__uint16 ) aoe_disk_ptr->Major );
      Tag->PacketData->Minor = ( winvblock__uint8 ) aoe_disk_ptr->Minor;
      Tag->PacketData->ExtendedAFlag = TRUE;

      /*
       * Initialize the packet appropriately based on our current phase 
       */
      switch ( disk_ptr->SearchState )
	{
	  case GetSize:
	    /*
	     * TODO: Make the below value into a #defined constant 
	     */
	    Tag->PacketData->Cmd = 0xec;	/* IDENTIFY DEVICE */
	    Tag->PacketData->Count = 1;
	    disk_ptr->SearchState = GettingSize;
	    break;
	  case GetGeometry:
	    /*
	     * TODO: Make the below value into a #defined constant 
	     */
	    Tag->PacketData->Cmd = 0x24;	/* READ SECTOR */
	    Tag->PacketData->Count = 1;
	    disk_ptr->SearchState = GettingGeometry;
	    break;
	  case GetMaxSectorsPerPacket:
	    /*
	     * TODO: Make the below value into a #defined constant 
	     */
	    Tag->PacketData->Cmd = 0x24;	/* READ SECTOR */
	    Tag->PacketData->Count =
	      ( winvblock__uint8 ) ( ++aoe_disk_ptr->MaxSectorsPerPacket );
	    KeQuerySystemTime ( &MaxSectorsPerPacketSendTime );
	    disk_ptr->SearchState = GettingMaxSectorsPerPacket;
	    /*
	     * TODO: Make the below value into a #defined constant 
	     */
	    aoe_disk_ptr->Timeout = 200000;
	    break;
	  default:
	    DBG ( "Undefined SearchState!!\n" );
	    ExFreePool ( Tag->PacketData );
	    ExFreePool ( Tag );
	    /*
	     * TODO: Do we need to nullify Tag here? 
	     */
	    KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
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
      KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
    }
}

disk__io_decl ( aoe__disk_io )
{
  PAOE_REQUEST Request;
  PAOE_TAG Tag,
   NewTagList = NULL,
    PreviousTag = NULL;
  KIRQL Irql;
  winvblock__uint32 i;
  PHYSICAL_ADDRESS PhysicalAddress;
  winvblock__uint8_ptr PhysicalMemory;
  disk__type_ptr disk_ptr;
  aoe__disk_type_ptr aoe_disk_ptr;

  /*
   * Establish pointers into the disk device's extension space
   */
  disk_ptr = get_disk_ptr ( dev_ext_ptr );
  aoe_disk_ptr = aoe__get_disk_ptr ( dev_ext_ptr );

  if ( AoE_Globals_Stop )
    {
      /*
       * Shutting down AoE; we can't service this request 
       */
      irp->IoStatus.Information = 0;
      irp->IoStatus.Status = STATUS_CANCELLED;
      IoCompleteRequest ( irp, IO_NO_INCREMENT );
      return STATUS_CANCELLED;
    }

  if ( sector_count < 1 )
    {
      /*
       * A silly request 
       */
      DBG ( "sector_count < 1; cancelling\n" );
      irp->IoStatus.Information = 0;
      irp->IoStatus.Status = STATUS_CANCELLED;
      IoCompleteRequest ( irp, IO_NO_INCREMENT );
      return STATUS_CANCELLED;
    }

  /*
   * Allocate and zero-fill our request 
   */
  if ( ( Request =
	 ( PAOE_REQUEST ) ExAllocatePool ( NonPagedPool,
					   sizeof ( AOE_REQUEST ) ) ) == NULL )
    {
      DBG ( "Couldn't allocate Request; bye!\n" );
      irp->IoStatus.Information = 0;
      irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
      IoCompleteRequest ( irp, IO_NO_INCREMENT );
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  RtlZeroMemory ( Request, sizeof ( AOE_REQUEST ) );

  /*
   * Initialize the request 
   */
  Request->Mode = mode;
  Request->SectorCount = sector_count;
  Request->Buffer = buffer;
  Request->Irp = irp;
  Request->TagCount = 0;

  /*
   * Split the requested sectors into packets in tags
   */
  for ( i = 0; i < sector_count; i += aoe_disk_ptr->MaxSectorsPerPacket )
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
	  irp->IoStatus.Information = 0;
	  irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
	  IoCompleteRequest ( irp, IO_NO_INCREMENT );
	  return STATUS_INSUFFICIENT_RESOURCES;
	}

      /*
       * Initialize each tag 
       */
      RtlZeroMemory ( Tag, sizeof ( AOE_TAG ) );
      Tag->Type = AoE_RequestType;
      Tag->Request = Request;
      Tag->DeviceExtension = dev_ext_ptr;
      Request->TagCount++;
      Tag->Id = 0;
      Tag->BufferOffset = i * disk_ptr->SectorSize;
      Tag->SectorCount =
	( ( sector_count - i ) <
	  aoe_disk_ptr->MaxSectorsPerPacket ? sector_count -
	  i : aoe_disk_ptr->MaxSectorsPerPacket );

      /*
       * Allocate and initialize each tag's AoE packet 
       */
      Tag->PacketSize = sizeof ( AOE_PACKET );
      if ( mode == disk__io_mode_write )
	Tag->PacketSize += Tag->SectorCount * disk_ptr->SectorSize;
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
	  irp->IoStatus.Information = 0;
	  irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
	  IoCompleteRequest ( irp, IO_NO_INCREMENT );
	  return STATUS_INSUFFICIENT_RESOURCES;
	}
      RtlZeroMemory ( Tag->PacketData, Tag->PacketSize );
      Tag->PacketData->Ver = AOEPROTOCOLVER;
      Tag->PacketData->Major =
	htons ( ( winvblock__uint16 ) aoe_disk_ptr->Major );
      Tag->PacketData->Minor = ( winvblock__uint8 ) aoe_disk_ptr->Minor;
      Tag->PacketData->Tag = 0;
      Tag->PacketData->Command = 0;
      Tag->PacketData->ExtendedAFlag = TRUE;
      if ( mode == disk__io_mode_read )
	{
	  Tag->PacketData->Cmd = 0x24;	/* READ SECTOR */
	}
      else
	{
	  Tag->PacketData->Cmd = 0x34;	/* WRITE SECTOR */
	  Tag->PacketData->WriteAFlag = 1;
	}
      Tag->PacketData->Count = ( winvblock__uint8 ) Tag->SectorCount;
      Tag->PacketData->Lba0 =
	( winvblock__uint8 ) ( ( ( start_sector + i ) >> 0 ) & 255 );
      Tag->PacketData->Lba1 =
	( winvblock__uint8 ) ( ( ( start_sector + i ) >> 8 ) & 255 );
      Tag->PacketData->Lba2 =
	( winvblock__uint8 ) ( ( ( start_sector + i ) >> 16 ) & 255 );
      Tag->PacketData->Lba3 =
	( winvblock__uint8 ) ( ( ( start_sector + i ) >> 24 ) & 255 );
      Tag->PacketData->Lba4 =
	( winvblock__uint8 ) ( ( ( start_sector + i ) >> 32 ) & 255 );
      Tag->PacketData->Lba5 =
	( winvblock__uint8 ) ( ( ( start_sector + i ) >> 40 ) & 255 );

      /*
       * For a write request, copy from the buffer into the AoE packet 
       */
      if ( mode == disk__io_mode_write )
	RtlCopyMemory ( Tag->PacketData->Data, &buffer[Tag->BufferOffset],
			Tag->SectorCount * disk_ptr->SectorSize );

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

  irp->IoStatus.Information = 0;
  irp->IoStatus.Status = STATUS_PENDING;
  IoMarkIrpPending ( irp );

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
  IN winvblock__uint8_ptr SourceMac,
  IN winvblock__uint8_ptr DestinationMac,
  IN winvblock__uint8_ptr Data,
  IN winvblock__uint32 DataSize
 )
{
  PAOE_PACKET Reply = ( PAOE_PACKET ) Data;
  LONGLONG LBASize;
  PAOE_TAG Tag;
  KIRQL Irql;
  winvblock__bool Found = FALSE;
  LARGE_INTEGER CurrentTime;
  disk__type_ptr disk_ptr;
  aoe__disk_type_ptr aoe_disk_ptr;

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
   * Establish pointers into the disk device's extension space
   */
  disk_ptr = get_disk_ptr ( Tag->DeviceExtension );
  aoe_disk_ptr = aoe__get_disk_ptr ( Tag->DeviceExtension );

  /*
   * If our tag was a discovery request, note the server 
   */
  if ( RtlCompareMemory
       ( aoe_disk_ptr->ServerMac, "\xff\xff\xff\xff\xff\xff", 6 ) == 6 )
    {
      RtlCopyMemory ( aoe_disk_ptr->ServerMac, SourceMac, 6 );
      DBG ( "Major: %d minor: %d found on server "
	    "%02x:%02x:%02x:%02x:%02x:%02x\n", aoe_disk_ptr->Major,
	    aoe_disk_ptr->Minor, SourceMac[0], SourceMac[1], SourceMac[2],
	    SourceMac[3], SourceMac[4], SourceMac[5] );
    }

  KeQuerySystemTime ( &CurrentTime );
  aoe_disk_ptr->Timeout -=
    ( winvblock__uint32 ) ( ( aoe_disk_ptr->Timeout -
			      ( CurrentTime.QuadPart -
				Tag->FirstSendTime.QuadPart ) ) / 1024 );
  /*
   * TODO: Replace the values below with #defined constants 
   */
  if ( aoe_disk_ptr->Timeout > 100000000 )
    aoe_disk_ptr->Timeout = 100000000;

  switch ( Tag->Type )
    {
      case AoE_SearchDriveType:
	KeAcquireSpinLock ( &disk_ptr->SpinLock, &Irql );
	switch ( disk_ptr->SearchState )
	  {
	    case GettingSize:
	      /*
	       * The reply tells us the disk size 
	       */
	      RtlCopyMemory ( &disk_ptr->LBADiskSize, &Reply->Data[200],
			      sizeof ( LONGLONG ) );
	      /*
	       * Next we are concerned with the disk geometry 
	       */
	      disk_ptr->SearchState = GetGeometry;
	      break;
	    case GettingGeometry:
	      /*
	       * FIXME: use real values from partition table 
	       */
	      disk_ptr->SectorSize = 512;
	      disk_ptr->Heads = 255;
	      disk_ptr->Sectors = 63;
	      disk_ptr->Cylinders =
		disk_ptr->LBADiskSize / ( disk_ptr->Heads *
					  disk_ptr->Sectors );
	      disk_ptr->LBADiskSize =
		disk_ptr->Cylinders * disk_ptr->Heads * disk_ptr->Sectors;
	      /*
	       * Next we are concerned with the maximum sectors per packet 
	       */
	      disk_ptr->SearchState = GetMaxSectorsPerPacket;
	      break;
	    case GettingMaxSectorsPerPacket:
	      DataSize -= sizeof ( AOE_PACKET );
	      if ( DataSize <
		   ( aoe_disk_ptr->MaxSectorsPerPacket *
		     disk_ptr->SectorSize ) )
		{
		  DBG ( "Packet size too low while getting "
			"MaxSectorsPerPacket (tried %d, got size of %d)\n",
			aoe_disk_ptr->MaxSectorsPerPacket, DataSize );
		  aoe_disk_ptr->MaxSectorsPerPacket--;
		  disk_ptr->SearchState = Done;
		}
	      else if ( aoe_disk_ptr->MTU <
			( sizeof ( AOE_PACKET ) +
			  ( ( aoe_disk_ptr->MaxSectorsPerPacket +
			      1 ) * disk_ptr->SectorSize ) ) )
		{
		  DBG ( "Got MaxSectorsPerPacket %d at size of %d. "
			"MTU of %d reached\n",
			aoe_disk_ptr->MaxSectorsPerPacket, DataSize,
			aoe_disk_ptr->MTU );
		  disk_ptr->SearchState = Done;
		}
	      else
		{
		  DBG ( "Got MaxSectorsPerPacket %d at size of %d, "
			"trying next...\n", aoe_disk_ptr->MaxSectorsPerPacket,
			DataSize );
		  disk_ptr->SearchState = GetMaxSectorsPerPacket;
		}
	      break;
	    default:
	      DBG ( "Undefined SearchState!\n" );
	      break;
	  }
	KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
	KeSetEvent ( &disk_ptr->SearchEvent, 0, FALSE );
	break;
      case AoE_RequestType:
	/*
	 * If the reply is in response to a read request, get our data! 
	 */
	if ( Tag->Request->Mode == disk__io_mode_read )
	  RtlCopyMemory ( &Tag->Request->Buffer[Tag->BufferOffset],
			  Reply->Data,
			  Tag->SectorCount * disk_ptr->SectorSize );
	/*
	 * If this is the last reply expected for the read request,
	 * complete the IRP and free the request
	 */
	if ( InterlockedDecrement ( &Tag->Request->TagCount ) == 0 )
	  {
	    Tag->Request->Irp->IoStatus.Information =
	      Tag->Request->SectorCount * disk_ptr->SectorSize;
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

VOID
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
  winvblock__uint32 NextTagId = 1;
  PAOE_TAG Tag;
  KIRQL Irql;
  winvblock__uint32 Sends = 0;
  winvblock__uint32 Resends = 0;
  winvblock__uint32 ResendFails = 0;
  winvblock__uint32 Fails = 0;
  winvblock__uint32 RequestTimeout = 0;
  disk__type_ptr disk_ptr;
  aoe__disk_type_ptr aoe_disk_ptr;

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
			  ( winvblock__uint8_ptr ) AoE_Globals_ProbeTag->
			  PacketData, AoE_Globals_ProbeTag->PacketSize, NULL );
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
	  /*
	   * Establish pointers into the disk device's extension space
	   */
	  disk_ptr = get_disk_ptr ( Tag->DeviceExtension );
	  aoe_disk_ptr = aoe__get_disk_ptr ( Tag->DeviceExtension );

	  RequestTimeout = aoe_disk_ptr->Timeout;
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
		       ( aoe_disk_ptr->ClientMac, aoe_disk_ptr->ServerMac,
			 ( winvblock__uint8_ptr ) Tag->PacketData,
			 Tag->PacketSize, Tag ) )
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
		     ( LONGLONG ) ( aoe_disk_ptr->Timeout * 2 ) ) )
		{
		  if ( Protocol_Send
		       ( aoe_disk_ptr->ClientMac, aoe_disk_ptr->ServerMac,
			 ( winvblock__uint8_ptr ) Tag->PacketData,
			 Tag->PacketSize, Tag ) )
		    {
		      KeQuerySystemTime ( &Tag->SendTime );
		      aoe_disk_ptr->Timeout += aoe_disk_ptr->Timeout / 1000;
		      if ( aoe_disk_ptr->Timeout > 100000000 )
			aoe_disk_ptr->Timeout = 100000000;
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

winvblock__uint32
aoe__max_xfer_len (
  disk__type_ptr disk_ptr
 )
{
  aoe__disk_type_ptr aoe_disk_ptr = aoe__get_disk_ptr ( &disk_ptr->dev_ext );

  return disk_ptr->SectorSize * aoe_disk_ptr->MaxSectorsPerPacket;
}

winvblock__uint32
aoe__query_id (
  disk__type_ptr disk_ptr,
  BUS_QUERY_ID_TYPE query_type,
  PWCHAR buf_512
 )
{
  aoe__disk_type_ptr aoe_disk_ptr = aoe__get_disk_ptr ( &disk_ptr->dev_ext );

  switch ( query_type )
    {
      case BusQueryDeviceID:
	return swprintf ( buf_512, L"WinVBlock\\AoEe%d.%d",
			  aoe_disk_ptr->Major, aoe_disk_ptr->Minor ) + 1;
      case BusQueryInstanceID:
	return swprintf ( buf_512, L"AOEDISK%d.%d", aoe_disk_ptr->Major,
			  aoe_disk_ptr->Minor ) + 1;
      case BusQueryHardwareIDs:
	{
	  winvblock__uint32 tmp =
	    swprintf ( buf_512, L"WinVBlock\\AoEe%d.%d", aoe_disk_ptr->Major,
		       aoe_disk_ptr->Minor ) + 1;
	  tmp += swprintf ( &buf_512[tmp], L"GenDisk" ) + 4;
	  return tmp;
	}
      case BusQueryCompatibleIDs:
	return swprintf ( buf_512, L"GenDisk" ) + 4;
      default:
	return 0;
    }
}

#ifdef _MSC_VER
#  pragma pack(1)
#endif
winvblock__def_struct ( abft )
{
  winvblock__uint32 Signature;	/* 0x54464261 (aBFT) */
  winvblock__uint32 Length;
  winvblock__uint8 Revision;
  winvblock__uint8 Checksum;
  winvblock__uint8 OEMID[6];
  winvblock__uint8 OEMTableID[8];
  winvblock__uint8 Reserved1[12];
  winvblock__uint16 Major;
  winvblock__uint8 Minor;
  winvblock__uint8 Reserved2;
  winvblock__uint8 ClientMac[6];
}

__attribute__ ( ( __packed__ ) );
#ifdef _MSC_VER
#  pragma pack()
#endif

void
aoe__process_abft (
  void
 )
{
  PHYSICAL_ADDRESS PhysicalAddress;
  winvblock__uint8_ptr PhysicalMemory;
  winvblock__uint32 Offset,
   Checksum,
   i;
  winvblock__bool FoundAbft = FALSE;
  abft AoEBootRecord;
  aoe__disk_type aoe_disk;
  bus__type_ptr bus_ptr;

  /*
   * Establish a pointer into the bus device's extension space
   */
  bus_ptr = get_bus_ptr ( ( driver__dev_ext_ptr ) bus__fdo->DeviceExtension );
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
	  if ( ( ( abft_ptr ) & PhysicalMemory[Offset] )->Signature ==
	       0x54464261 )
	    {
	      Checksum = 0;
	      for ( i = 0;
		    i < ( ( abft_ptr ) & PhysicalMemory[Offset] )->Length;
		    i++ )
		Checksum += PhysicalMemory[Offset + i];
	      if ( Checksum & 0xff )
		continue;
	      if ( ( ( abft_ptr ) & PhysicalMemory[Offset] )->Revision != 1 )
		{
		  DBG ( "Found aBFT with mismatched revision v%d at "
			"segment 0x%4x. want v1.\n",
			( ( abft_ptr ) & PhysicalMemory[Offset] )->Revision,
			( Offset / 0x10 ) );
		  continue;
		}
	      DBG ( "Found aBFT at segment: 0x%04x\n", ( Offset / 0x10 ) );
	      RtlCopyMemory ( &AoEBootRecord, &PhysicalMemory[Offset],
			      sizeof ( abft ) );
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
      aoe_disk.disk.Initialize = AoE_SearchDrive;
      RtlCopyMemory ( aoe_disk.ClientMac, AoEBootRecord.ClientMac, 6 );
      RtlFillMemory ( aoe_disk.ServerMac, 6, 0xff );
      aoe_disk.Major = AoEBootRecord.Major;
      aoe_disk.Minor = AoEBootRecord.Minor;
      aoe_disk.MaxSectorsPerPacket = 1;
      aoe_disk.Timeout = 200000;	/* 20 ms. */
      aoe_disk.disk.BootDrive = TRUE;
      aoe_disk.disk.io = aoe__disk_io;
      aoe_disk.disk.max_xfer_len = aoe__max_xfer_len;
      aoe_disk.disk.query_id = aoe__query_id;
      aoe_disk.disk.dev_ext.size = sizeof ( aoe__disk_type );

      if ( !Bus_AddChild ( bus__fdo, &aoe_disk.disk, TRUE ) )
	DBG ( "Bus_AddChild() failed for aBFT AoE disk\n" );
      else
	{
	  if ( bus_ptr->PhysicalDeviceObject != NULL )
	    {
	      IoInvalidateDeviceRelations ( bus_ptr->PhysicalDeviceObject,
					    BusRelations );
	    }
	}
    }
  else
    {
      DBG ( "No aBFT found\n" );
    }
}
