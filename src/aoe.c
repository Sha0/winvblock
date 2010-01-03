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
static void STDCALL thread (
  IN void *StartContext
 );

/** Tag types */
enum _tag_type
{
  tag_type_io,
  tag_type_search_drive
};
winvblock__def_enum ( tag_type );

#ifdef _MSC_VER
#  pragma pack(1)
#endif

/** AoE packet */
winvblock__def_struct ( packet )
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
}

__attribute__ ( ( __packed__ ) );

#ifdef _MSC_VER
#  pragma pack()
#endif

/** An I/O request */
winvblock__def_struct ( io_req )
{
  disk__io_mode Mode;
  winvblock__uint32 SectorCount;
  winvblock__uint8_ptr Buffer;
  PIRP Irp;
  winvblock__uint32 TagCount;
  winvblock__uint32 TotalTags;
};

/** A work item "tag" */
winvblock__def_struct ( work_tag )
{
  tag_type type;
  driver__dev_ext_ptr DeviceExtension;
  io_req_ptr request_ptr;
  winvblock__uint32 Id;
  packet_ptr packet_data;
  winvblock__uint32 PacketSize;
  LARGE_INTEGER FirstSendTime;
  LARGE_INTEGER SendTime;
  winvblock__uint32 BufferOffset;
  winvblock__uint32 SectorCount;
  work_tag_ptr next;
  work_tag_ptr previous;
};

/** A disk search */
winvblock__def_struct ( disk_search )
{
  driver__dev_ext_ptr DeviceExtension;
  work_tag_ptr tag;
  disk_search_ptr next;
};

/** Globals */
static winvblock__bool AoE_Globals_Stop = FALSE;
static KSPIN_LOCK AoE_Globals_SpinLock;
static KEVENT AoE_Globals_ThreadSignalEvent;
static work_tag_ptr AoE_Globals_TagList = NULL;
static work_tag_ptr AoE_Globals_TagListLast = NULL;
static work_tag_ptr AoE_Globals_ProbeTag = NULL;
static disk_search_ptr AoE_Globals_DiskSearchList = NULL;
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
  void *ThreadObject;

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
	 ( work_tag_ptr ) ExAllocatePool ( NonPagedPool,
					   sizeof ( work_tag ) ) ) == NULL )
    {
      DBG ( "Couldn't allocate probe tag; bye!\n" );
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  RtlZeroMemory ( AoE_Globals_ProbeTag, sizeof ( work_tag ) );

  /*
   * Set up the probe tag's AoE packet reference 
   */
  AoE_Globals_ProbeTag->PacketSize = sizeof ( packet );
  /*
   * Allocate and zero-fill the probe tag's packet reference 
   */
  if ( ( AoE_Globals_ProbeTag->packet_data =
	 ( packet_ptr ) ExAllocatePool ( NonPagedPool,
					 AoE_Globals_ProbeTag->
					 PacketSize ) ) == NULL )
    {
      DBG ( "Couldn't allocate AoE_Globals_ProbeTag->packet_data\n" );
      ExFreePool ( AoE_Globals_ProbeTag );
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  AoE_Globals_ProbeTag->SendTime.QuadPart = 0LL;
  RtlZeroMemory ( AoE_Globals_ProbeTag->packet_data,
		  AoE_Globals_ProbeTag->PacketSize );

  /*
   * Initialize the probe tag's AoE packet 
   */
  AoE_Globals_ProbeTag->packet_data->Ver = AOEPROTOCOLVER;
  AoE_Globals_ProbeTag->packet_data->Major =
    htons ( ( winvblock__uint16 ) - 1 );
  AoE_Globals_ProbeTag->packet_data->Minor = ( winvblock__uint8 ) - 1;
  AoE_Globals_ProbeTag->packet_data->Cmd = 0xec;	/* IDENTIFY DEVICE */
  AoE_Globals_ProbeTag->packet_data->Count = 1;

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
				&ObjectAttributes, NULL, NULL, thread,
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
void
AoE_Stop (
  void
 )
{
  NTSTATUS Status;
  disk_search_ptr disk_searcher,
   previous_disk_searcher;
  work_tag_ptr tag;
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
  disk_searcher = AoE_Globals_DiskSearchList;
  while ( disk_searcher != NULL )
    {
      KeSetEvent ( &
		   ( get_disk_ptr ( disk_searcher->DeviceExtension )->
		     SearchEvent ), 0, FALSE );
      previous_disk_searcher = disk_searcher;
      disk_searcher = disk_searcher->next;
      ExFreePool ( previous_disk_searcher );
    }

  /*
   * Cancel and free all tags in the global tag list 
   */
  tag = AoE_Globals_TagList;
  while ( tag != NULL )
    {
      if ( tag->request_ptr != NULL && --tag->request_ptr->TagCount == 0 )
	{
	  tag->request_ptr->Irp->IoStatus.Information = 0;
	  tag->request_ptr->Irp->IoStatus.Status = STATUS_CANCELLED;
	  IoCompleteRequest ( tag->request_ptr->Irp, IO_NO_INCREMENT );
	  ExFreePool ( tag->request_ptr );
	}
      if ( tag->next == NULL )
	{
	  ExFreePool ( tag->packet_data );
	  ExFreePool ( tag );
	  tag = NULL;
	}
      else
	{
	  tag = tag->next;
	  ExFreePool ( tag->previous->packet_data );
	  ExFreePool ( tag->previous );
	}
    }
  AoE_Globals_TagList = NULL;
  AoE_Globals_TagListLast = NULL;

  /*
   * Free the global probe tag and its AoE packet 
   */
  ExFreePool ( AoE_Globals_ProbeTag->packet_data );
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
static
disk__init_decl (
  init
 )
{
  disk_search_ptr disk_searcher,
   disk_search_walker,
   previous_disk_searcher;
  LARGE_INTEGER Timeout,
   CurrentTime;
  work_tag_ptr tag,
   tag_walker;
  KIRQL Irql,
   InnerIrql;
  LARGE_INTEGER MaxSectorsPerPacketSendTime;
  winvblock__uint32 MTU;
  disk__type_ptr disk_ptr;
  aoe__disk_type_ptr aoe_disk_ptr;

  /*
   * Establish pointers into the disk device's extension space
   */
  disk_ptr = get_disk_ptr ( dev_ext_ptr );
  aoe_disk_ptr = aoe__get_disk_ptr ( dev_ext_ptr );
  if ( !NT_SUCCESS ( AoE_Start (  ) ) )
    {
      DBG ( "AoE startup failure!\n" );
      return FALSE;
    }
  /*
   * Allocate our disk search 
   */
  if ( ( disk_searcher =
	 ( disk_search_ptr ) ExAllocatePool ( NonPagedPool,
					      sizeof ( disk_search ) ) ) ==
       NULL )
    {
      DBG ( "Couldn't allocate for disk_searcher; bye!\n" );
      return FALSE;
    }

  /*
   * Initialize the disk search 
   */
  disk_searcher->DeviceExtension = dev_ext_ptr;
  disk_searcher->next = NULL;
  aoe_disk_ptr->search_state = search_state_search_nic;
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
      AoE_Globals_DiskSearchList = disk_searcher;
    }
  else
    {
      disk_search_walker = AoE_Globals_DiskSearchList;
      while ( disk_search_walker->next )
	disk_search_walker = disk_search_walker->next;
      disk_search_walker->next = disk_searcher;
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

      if ( aoe_disk_ptr->search_state == search_state_search_nic )
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
	      aoe_disk_ptr->search_state = search_state_get_size;
	    }
	}

      if ( aoe_disk_ptr->search_state == search_state_getting_size )
	{
	  /*
	   * Still getting the disk's size 
	   */
	  KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
	  continue;
	}
      if ( aoe_disk_ptr->search_state == search_state_getting_geometry )
	{
	  /*
	   * Still getting the disk's geometry 
	   */
	  KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
	  continue;
	}
      if ( aoe_disk_ptr->search_state ==
	   search_state_getting_max_sectors_per_packet )
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
	      aoe_disk_ptr->search_state = search_state_done;
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

      if ( aoe_disk_ptr->search_state == search_state_done )
	{
	  /*
	   * We've finished the disk search; perform clean-up 
	   */
	  KeAcquireSpinLock ( &AoE_Globals_SpinLock, &InnerIrql );

	  /*
	   * Tag clean-up: Find out if our tag is in the global tag list 
	   */
	  tag_walker = AoE_Globals_TagList;
	  while ( tag_walker != NULL && tag_walker != tag )
	    tag_walker = tag_walker->next;
	  if ( tag_walker != NULL )
	    {
	      /*
	       * We found it.  If it's at the beginning of the list, adjust
	       * the list to point the the next tag
	       */
	      if ( tag->previous == NULL )
		AoE_Globals_TagList = tag->next;
	      else
		/*
		 * Remove our tag from the list 
		 */
		tag->previous->next = tag->next;
	      /*
	       * If we 're at the end of the list, adjust the list's end to
	       * point to the penultimate tag
	       */
	      if ( tag->next == NULL )
		AoE_Globals_TagListLast = tag->previous;
	      else
		/*
		 * Remove our tag from the list 
		 */
		tag->next->previous = tag->previous;
	      AoE_Globals_OutstandingTags--;
	      if ( AoE_Globals_OutstandingTags < 0 )
		DBG ( "AoE_Globals_OutstandingTags < 0!!\n" );
	      /*
	       * Free our tag and its AoE packet 
	       */
	      ExFreePool ( tag->packet_data );
	      ExFreePool ( tag );
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
	      disk_search_walker = AoE_Globals_DiskSearchList;
	      while ( disk_search_walker
		      && disk_search_walker->DeviceExtension != dev_ext_ptr )
		{
		  previous_disk_searcher = disk_search_walker;
		  disk_search_walker = disk_search_walker->next;
		}
	      if ( disk_search_walker )
		{
		  /*
		   * We found our disk search.  If it's the first one in
		   * the list, adjust the list and remove it
		   */
		  if ( disk_search_walker == AoE_Globals_DiskSearchList )
		    AoE_Globals_DiskSearchList = disk_search_walker->next;
		  else
		    /*
		     * Just remove it 
		     */
		    previous_disk_searcher->next = disk_search_walker->next;
		  /*
		   * Free our disk search 
		   */
		  ExFreePool ( disk_search_walker );
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
       * if ( aoe_disk_ptr->search_state == search_state_done ) 
       */
      /*
       * Establish our tag 
       */
      if ( ( tag =
	     ( work_tag_ptr ) ExAllocatePool ( NonPagedPool,
					       sizeof ( work_tag ) ) ) ==
	   NULL )
	{
	  DBG ( "Couldn't allocate tag\n" );
	  KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
	  /*
	   * Maybe next time around 
	   */
	  continue;
	}
      RtlZeroMemory ( tag, sizeof ( work_tag ) );
      tag->type = tag_type_search_drive;
      tag->DeviceExtension = dev_ext_ptr;

      /*
       * Establish our tag's AoE packet 
       */
      tag->PacketSize = sizeof ( packet );
      if ( ( tag->packet_data =
	     ( packet_ptr ) ExAllocatePool ( NonPagedPool,
					     tag->PacketSize ) ) == NULL )
	{
	  DBG ( "Couldn't allocate tag->packet_data\n" );
	  ExFreePool ( tag );
	  tag = NULL;
	  KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
	  /*
	   * Maybe next time around 
	   */
	  continue;
	}
      RtlZeroMemory ( tag->packet_data, tag->PacketSize );
      tag->packet_data->Ver = AOEPROTOCOLVER;
      tag->packet_data->Major =
	htons ( ( winvblock__uint16 ) aoe_disk_ptr->Major );
      tag->packet_data->Minor = ( winvblock__uint8 ) aoe_disk_ptr->Minor;
      tag->packet_data->ExtendedAFlag = TRUE;

      /*
       * Initialize the packet appropriately based on our current phase 
       */
      switch ( aoe_disk_ptr->search_state )
	{
	  case search_state_get_size:
	    /*
	     * TODO: Make the below value into a #defined constant 
	     */
	    tag->packet_data->Cmd = 0xec;	/* IDENTIFY DEVICE */
	    tag->packet_data->Count = 1;
	    aoe_disk_ptr->search_state = search_state_getting_size;
	    break;
	  case search_state_get_geometry:
	    /*
	     * TODO: Make the below value into a #defined constant 
	     */
	    tag->packet_data->Cmd = 0x24;	/* READ SECTOR */
	    tag->packet_data->Count = 1;
	    aoe_disk_ptr->search_state = search_state_getting_geometry;
	    break;
	  case search_state_get_max_sectors_per_packet:
	    /*
	     * TODO: Make the below value into a #defined constant 
	     */
	    tag->packet_data->Cmd = 0x24;	/* READ SECTOR */
	    tag->packet_data->Count =
	      ( winvblock__uint8 ) ( ++aoe_disk_ptr->MaxSectorsPerPacket );
	    KeQuerySystemTime ( &MaxSectorsPerPacketSendTime );
	    aoe_disk_ptr->search_state =
	      search_state_getting_max_sectors_per_packet;
	    /*
	     * TODO: Make the below value into a #defined constant 
	     */
	    aoe_disk_ptr->Timeout = 200000;
	    break;
	  default:
	    DBG ( "Undefined search_state!!\n" );
	    ExFreePool ( tag->packet_data );
	    ExFreePool ( tag );
	    /*
	     * TODO: Do we need to nullify tag here? 
	     */
	    KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
	    continue;
	    break;
	}

      /*
       * Enqueue our tag 
       */
      tag->next = NULL;
      KeAcquireSpinLock ( &AoE_Globals_SpinLock, &InnerIrql );
      if ( AoE_Globals_TagList == NULL )
	{
	  AoE_Globals_TagList = tag;
	  tag->previous = NULL;
	}
      else
	{
	  AoE_Globals_TagListLast->next = tag;
	  tag->previous = AoE_Globals_TagListLast;
	}
      AoE_Globals_TagListLast = tag;
      KeReleaseSpinLock ( &AoE_Globals_SpinLock, InnerIrql );
      KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
    }
}

static
disk__io_decl (
  io
 )
{
  io_req_ptr request_ptr;
  work_tag_ptr tag,
   new_tag_list = NULL,
    previous_tag = NULL;
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
  if ( ( request_ptr =
	 ( io_req_ptr ) ExAllocatePool ( NonPagedPool,
					 sizeof ( io_req ) ) ) == NULL )
    {
      DBG ( "Couldn't allocate for reques_ptr; bye!\n" );
      irp->IoStatus.Information = 0;
      irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
      IoCompleteRequest ( irp, IO_NO_INCREMENT );
      return STATUS_INSUFFICIENT_RESOURCES;
    }
  RtlZeroMemory ( request_ptr, sizeof ( io_req ) );

  /*
   * Initialize the request 
   */
  request_ptr->Mode = mode;
  request_ptr->SectorCount = sector_count;
  request_ptr->Buffer = buffer;
  request_ptr->Irp = irp;
  request_ptr->TagCount = 0;

  /*
   * Split the requested sectors into packets in tags
   */
  for ( i = 0; i < sector_count; i += aoe_disk_ptr->MaxSectorsPerPacket )
    {
      /*
       * Allocate each tag 
       */
      if ( ( tag =
	     ( work_tag_ptr ) ExAllocatePool ( NonPagedPool,
					       sizeof ( work_tag ) ) ) ==
	   NULL )
	{
	  DBG ( "Couldn't allocate tag; bye!\n" );
	  /*
	   * We failed while allocating tags; free the ones we built 
	   */
	  tag = new_tag_list;
	  while ( tag != NULL )
	    {
	      previous_tag = tag;
	      tag = tag->next;
	      ExFreePool ( previous_tag->packet_data );
	      ExFreePool ( previous_tag );
	    }
	  ExFreePool ( request_ptr );
	  irp->IoStatus.Information = 0;
	  irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
	  IoCompleteRequest ( irp, IO_NO_INCREMENT );
	  return STATUS_INSUFFICIENT_RESOURCES;
	}

      /*
       * Initialize each tag 
       */
      RtlZeroMemory ( tag, sizeof ( work_tag ) );
      tag->type = tag_type_io;
      tag->request_ptr = request_ptr;
      tag->DeviceExtension = dev_ext_ptr;
      request_ptr->TagCount++;
      tag->Id = 0;
      tag->BufferOffset = i * disk_ptr->SectorSize;
      tag->SectorCount =
	( ( sector_count - i ) <
	  aoe_disk_ptr->MaxSectorsPerPacket ? sector_count -
	  i : aoe_disk_ptr->MaxSectorsPerPacket );

      /*
       * Allocate and initialize each tag's AoE packet 
       */
      tag->PacketSize = sizeof ( packet );
      if ( mode == disk__io_mode_write )
	tag->PacketSize += tag->SectorCount * disk_ptr->SectorSize;
      if ( ( tag->packet_data =
	     ( packet_ptr ) ExAllocatePool ( NonPagedPool,
					     tag->PacketSize ) ) == NULL )
	{
	  DBG ( "Couldn't allocate tag->packet_data; bye!\n" );
	  /*
	   * We failed while allocating an AoE packet; free
	   * the tags we built
	   */
	  ExFreePool ( tag );
	  tag = new_tag_list;
	  while ( tag != NULL )
	    {
	      previous_tag = tag;
	      tag = tag->next;
	      ExFreePool ( previous_tag->packet_data );
	      ExFreePool ( previous_tag );
	    }
	  ExFreePool ( request_ptr );
	  irp->IoStatus.Information = 0;
	  irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
	  IoCompleteRequest ( irp, IO_NO_INCREMENT );
	  return STATUS_INSUFFICIENT_RESOURCES;
	}
      RtlZeroMemory ( tag->packet_data, tag->PacketSize );
      tag->packet_data->Ver = AOEPROTOCOLVER;
      tag->packet_data->Major =
	htons ( ( winvblock__uint16 ) aoe_disk_ptr->Major );
      tag->packet_data->Minor = ( winvblock__uint8 ) aoe_disk_ptr->Minor;
      tag->packet_data->Tag = 0;
      tag->packet_data->Command = 0;
      tag->packet_data->ExtendedAFlag = TRUE;
      if ( mode == disk__io_mode_read )
	{
	  tag->packet_data->Cmd = 0x24;	/* READ SECTOR */
	}
      else
	{
	  tag->packet_data->Cmd = 0x34;	/* WRITE SECTOR */
	  tag->packet_data->WriteAFlag = 1;
	}
      tag->packet_data->Count = ( winvblock__uint8 ) tag->SectorCount;
      tag->packet_data->Lba0 =
	( winvblock__uint8 ) ( ( ( start_sector + i ) >> 0 ) & 255 );
      tag->packet_data->Lba1 =
	( winvblock__uint8 ) ( ( ( start_sector + i ) >> 8 ) & 255 );
      tag->packet_data->Lba2 =
	( winvblock__uint8 ) ( ( ( start_sector + i ) >> 16 ) & 255 );
      tag->packet_data->Lba3 =
	( winvblock__uint8 ) ( ( ( start_sector + i ) >> 24 ) & 255 );
      tag->packet_data->Lba4 =
	( winvblock__uint8 ) ( ( ( start_sector + i ) >> 32 ) & 255 );
      tag->packet_data->Lba5 =
	( winvblock__uint8 ) ( ( ( start_sector + i ) >> 40 ) & 255 );

      /*
       * For a write request, copy from the buffer into the AoE packet 
       */
      if ( mode == disk__io_mode_write )
	RtlCopyMemory ( tag->packet_data->Data, &buffer[tag->BufferOffset],
			tag->SectorCount * disk_ptr->SectorSize );

      /*
       * Add this tag to the request's tag list 
       */
      tag->previous = previous_tag;
      tag->next = NULL;
      if ( new_tag_list == NULL )
	{
	  new_tag_list = tag;
	}
      else
	{
	  previous_tag->next = tag;
	}
      previous_tag = tag;
    }
  /*
   * Split the requested sectors into packets in tags
   */
  request_ptr->TotalTags = request_ptr->TagCount;

  /*
   * Wait until we have the global spin-lock 
   */
  KeAcquireSpinLock ( &AoE_Globals_SpinLock, &Irql );

  /*
   * Enqueue our request's tag list to the global tag list 
   */
  if ( AoE_Globals_TagListLast == NULL )
    {
      AoE_Globals_TagList = new_tag_list;
    }
  else
    {
      AoE_Globals_TagListLast->next = new_tag_list;
      new_tag_list->previous = AoE_Globals_TagListLast;
    }
  /*
   * Adjust the global list to reflect our last tag 
   */
  AoE_Globals_TagListLast = tag;

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
  packet_ptr reply = ( packet_ptr ) Data;
  LONGLONG LBASize;
  work_tag_ptr tag;
  KIRQL Irql;
  winvblock__bool Found = FALSE;
  LARGE_INTEGER CurrentTime;
  disk__type_ptr disk_ptr;
  aoe__disk_type_ptr aoe_disk_ptr;

  /*
   * Discard non-responses 
   */
  if ( !reply->ResponseFlag )
    return STATUS_SUCCESS;

  /*
   * If the response matches our probe, add the AoE disk device 
   */
  if ( AoE_Globals_ProbeTag->Id == reply->Tag )
    {
      RtlCopyMemory ( &LBASize, &reply->Data[200], sizeof ( LONGLONG ) );
      Bus_AddTarget ( DestinationMac, SourceMac, ntohs ( reply->Major ),
		      reply->Minor, LBASize );
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
  tag = AoE_Globals_TagList;
  while ( tag != NULL )
    {
      if ( ( tag->Id == reply->Tag )
	   && ( tag->packet_data->Major == reply->Major )
	   && ( tag->packet_data->Minor == reply->Minor ) )
	{
	  Found = TRUE;
	  break;
	}
      tag = tag->next;
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
      if ( tag->previous == NULL )
	AoE_Globals_TagList = tag->next;
      else
	tag->previous->next = tag->next;
      if ( tag->next == NULL )
	AoE_Globals_TagListLast = tag->previous;
      else
	tag->next->previous = tag->previous;
      AoE_Globals_OutstandingTags--;
      if ( AoE_Globals_OutstandingTags < 0 )
	DBG ( "AoE_Globals_OutstandingTags < 0!!\n" );
      KeSetEvent ( &AoE_Globals_ThreadSignalEvent, 0, FALSE );
    }
  KeReleaseSpinLock ( &AoE_Globals_SpinLock, Irql );

  /*
   * Establish pointers into the disk device's extension space
   */
  disk_ptr = get_disk_ptr ( tag->DeviceExtension );
  aoe_disk_ptr = aoe__get_disk_ptr ( tag->DeviceExtension );

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
				tag->FirstSendTime.QuadPart ) ) / 1024 );
  /*
   * TODO: Replace the values below with #defined constants 
   */
  if ( aoe_disk_ptr->Timeout > 100000000 )
    aoe_disk_ptr->Timeout = 100000000;

  switch ( tag->type )
    {
      case tag_type_search_drive:
	KeAcquireSpinLock ( &disk_ptr->SpinLock, &Irql );
	switch ( aoe_disk_ptr->search_state )
	  {
	    case search_state_getting_size:
	      /*
	       * The reply tells us the disk size 
	       */
	      RtlCopyMemory ( &disk_ptr->LBADiskSize, &reply->Data[200],
			      sizeof ( LONGLONG ) );
	      /*
	       * Next we are concerned with the disk geometry 
	       */
	      aoe_disk_ptr->search_state = search_state_get_geometry;
	      break;
	    case search_state_getting_geometry:
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
	      aoe_disk_ptr->search_state =
		search_state_get_max_sectors_per_packet;
	      break;
	    case search_state_getting_max_sectors_per_packet:
	      DataSize -= sizeof ( packet );
	      if ( DataSize <
		   ( aoe_disk_ptr->MaxSectorsPerPacket *
		     disk_ptr->SectorSize ) )
		{
		  DBG ( "Packet size too low while getting "
			"MaxSectorsPerPacket (tried %d, got size of %d)\n",
			aoe_disk_ptr->MaxSectorsPerPacket, DataSize );
		  aoe_disk_ptr->MaxSectorsPerPacket--;
		  aoe_disk_ptr->search_state = search_state_done;
		}
	      else if ( aoe_disk_ptr->MTU <
			( sizeof ( packet ) +
			  ( ( aoe_disk_ptr->MaxSectorsPerPacket +
			      1 ) * disk_ptr->SectorSize ) ) )
		{
		  DBG ( "Got MaxSectorsPerPacket %d at size of %d. "
			"MTU of %d reached\n",
			aoe_disk_ptr->MaxSectorsPerPacket, DataSize,
			aoe_disk_ptr->MTU );
		  aoe_disk_ptr->search_state = search_state_done;
		}
	      else
		{
		  DBG ( "Got MaxSectorsPerPacket %d at size of %d, "
			"trying next...\n", aoe_disk_ptr->MaxSectorsPerPacket,
			DataSize );
		  aoe_disk_ptr->search_state =
		    search_state_get_max_sectors_per_packet;
		}
	      break;
	    default:
	      DBG ( "Undefined search_state!\n" );
	      break;
	  }
	KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
	KeSetEvent ( &disk_ptr->SearchEvent, 0, FALSE );
	break;
      case tag_type_io:
	/*
	 * If the reply is in response to a read request, get our data! 
	 */
	if ( tag->request_ptr->Mode == disk__io_mode_read )
	  RtlCopyMemory ( &tag->request_ptr->Buffer[tag->BufferOffset],
			  reply->Data,
			  tag->SectorCount * disk_ptr->SectorSize );
	/*
	 * If this is the last reply expected for the read request,
	 * complete the IRP and free the request
	 */
	if ( InterlockedDecrement ( &tag->request_ptr->TagCount ) == 0 )
	  {
	    tag->request_ptr->Irp->IoStatus.Information =
	      tag->request_ptr->SectorCount * disk_ptr->SectorSize;
	    tag->request_ptr->Irp->IoStatus.Status = STATUS_SUCCESS;
	    Driver_CompletePendingIrp ( tag->request_ptr->Irp );
	    ExFreePool ( tag->request_ptr );
	  }
	break;
      default:
	DBG ( "Unknown tag type!!\n" );
	break;
    }

  KeSetEvent ( &AoE_Globals_ThreadSignalEvent, 0, FALSE );
  ExFreePool ( tag->packet_data );
  ExFreePool ( tag );
  return STATUS_SUCCESS;
}

void
AoE_ResetProbe (
  void
 )
{
  AoE_Globals_ProbeTag->SendTime.QuadPart = 0LL;
}

static void STDCALL
thread (
  IN void *StartContext
 )
{
  LARGE_INTEGER Timeout,
   CurrentTime,
   ProbeTime,
   ReportTime;
  winvblock__uint32 NextTagId = 1;
  work_tag_ptr tag;
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
	  AoE_Globals_ProbeTag->packet_data->Tag = AoE_Globals_ProbeTag->Id;
	  Protocol_Send ( "\xff\xff\xff\xff\xff\xff",
			  "\xff\xff\xff\xff\xff\xff",
			  ( winvblock__uint8_ptr ) AoE_Globals_ProbeTag->
			  packet_data, AoE_Globals_ProbeTag->PacketSize,
			  NULL );
	  KeQuerySystemTime ( &AoE_Globals_ProbeTag->SendTime );
	}

      KeAcquireSpinLock ( &AoE_Globals_SpinLock, &Irql );
      if ( AoE_Globals_TagList == NULL )
	{
	  KeReleaseSpinLock ( &AoE_Globals_SpinLock, Irql );
	  continue;
	}
      tag = AoE_Globals_TagList;
      while ( tag != NULL )
	{
	  /*
	   * Establish pointers into the disk device's extension space
	   */
	  disk_ptr = get_disk_ptr ( tag->DeviceExtension );
	  aoe_disk_ptr = aoe__get_disk_ptr ( tag->DeviceExtension );

	  RequestTimeout = aoe_disk_ptr->Timeout;
	  if ( tag->Id == 0 )
	    {
	      if ( AoE_Globals_OutstandingTags <= 64 )
		{
		  /*
		   * if ( AoE_Globals_OutstandingTags <= 102400 ) { 
		   */
		  if ( AoE_Globals_OutstandingTags < 0 )
		    DBG ( "AoE_Globals_OutstandingTags < 0!!\n" );
		  tag->Id = NextTagId++;
		  if ( NextTagId == 0 )
		    NextTagId++;
		  tag->packet_data->Tag = tag->Id;
		  if ( Protocol_Send
		       ( aoe_disk_ptr->ClientMac, aoe_disk_ptr->ServerMac,
			 ( winvblock__uint8_ptr ) tag->packet_data,
			 tag->PacketSize, tag ) )
		    {
		      KeQuerySystemTime ( &tag->FirstSendTime );
		      KeQuerySystemTime ( &tag->SendTime );
		      AoE_Globals_OutstandingTags++;
		      Sends++;
		    }
		  else
		    {
		      Fails++;
		      tag->Id = 0;
		      break;
		    }
		}
	    }
	  else
	    {
	      KeQuerySystemTime ( &CurrentTime );
	      if ( CurrentTime.QuadPart >
		   ( tag->SendTime.QuadPart +
		     ( LONGLONG ) ( aoe_disk_ptr->Timeout * 2 ) ) )
		{
		  if ( Protocol_Send
		       ( aoe_disk_ptr->ClientMac, aoe_disk_ptr->ServerMac,
			 ( winvblock__uint8_ptr ) tag->packet_data,
			 tag->PacketSize, tag ) )
		    {
		      KeQuerySystemTime ( &tag->SendTime );
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
	  tag = tag->next;
	  if ( tag == AoE_Globals_TagList )
	    {
	      DBG ( "Taglist Cyclic!!\n" );
	      break;
	    }
	}
      KeReleaseSpinLock ( &AoE_Globals_SpinLock, Irql );
    }
}

static
disk__max_xfer_len_decl (
  max_xfer_len
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

disk__ops aoe__default_ops = {
  io,
  max_xfer_len,
  init
};

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
      RtlCopyMemory ( aoe_disk.ClientMac, AoEBootRecord.ClientMac, 6 );
      RtlFillMemory ( aoe_disk.ServerMac, 6, 0xff );
      aoe_disk.Major = AoEBootRecord.Major;
      aoe_disk.Minor = AoEBootRecord.Minor;
      aoe_disk.MaxSectorsPerPacket = 1;
      aoe_disk.Timeout = 200000;	/* 20 ms. */
      aoe_disk.disk.BootDrive = TRUE;
      aoe_disk.disk.ops = &aoe__default_ops;
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
