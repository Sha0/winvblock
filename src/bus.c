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
 * Bus specifics
 *
 */

#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "disk.h"
#include "bus.h"
#include "aoe.h"
#include "mount.h"
#include "debug.h"
#include "mdi.h"
#include "protocol.h"
#include "probe.h"

/* in this file */
static irp__handler_decl (
  Bus_DispatchNotSupported
 );
static irp__handler_decl (
  Bus_DispatchPower
 );
winvblock__bool STDCALL Bus_AddChild (
  IN PDEVICE_OBJECT BusDeviceObject,
  IN disk__type Disk,
  IN winvblock__bool Boot
 );
static NTSTATUS STDCALL Bus_IoCompletionRoutine (
  IN PDEVICE_OBJECT DeviceObject,
  IN PIRP Irp,
  IN PKEVENT Event
 );

typedef struct _BUS_TARGETLIST
{
  MOUNT_TARGET Target;
  struct _BUS_TARGETLIST *Next;
} BUS_TARGETLIST,
*PBUS_TARGETLIST;

static PBUS_TARGETLIST Bus_Globals_TargetList = NULL;
static KSPIN_LOCK Bus_Globals_TargetListSpinLock;
static ULONG Bus_Globals_NextDisk = 0;
PDEVICE_OBJECT bus__fdo = NULL;

NTSTATUS STDCALL
Bus_Start (
  void
 )
{
  DBG ( "Entry\n" );
  KeInitializeSpinLock ( &Bus_Globals_TargetListSpinLock );
  return STATUS_SUCCESS;
}

VOID STDCALL
Bus_Stop (
  void
 )
{
  UNICODE_STRING DosDeviceName;
  PBUS_TARGETLIST Walker,
   Next;
  KIRQL Irql;

  DBG ( "Entry\n" );
  KeAcquireSpinLock ( &Bus_Globals_TargetListSpinLock, &Irql );
  Walker = Bus_Globals_TargetList;
  while ( Walker != NULL )
    {
      Next = Walker->Next;
      ExFreePool ( Walker );
      Walker = Next;
    }
  KeReleaseSpinLock ( &Bus_Globals_TargetListSpinLock, Irql );
  RtlInitUnicodeString ( &DosDeviceName, L"\\DosDevices\\AoE" );
  IoDeleteSymbolicLink ( &DosDeviceName );
  bus__fdo = NULL;
}

VOID STDCALL
Bus_AddTarget (
  IN winvblock__uint8_ptr ClientMac,
  IN winvblock__uint8_ptr ServerMac,
  winvblock__uint16 Major,
  winvblock__uint8 Minor,
  LONGLONG LBASize
 )
{
  PBUS_TARGETLIST Walker,
   Last;
  KIRQL Irql;

  KeAcquireSpinLock ( &Bus_Globals_TargetListSpinLock, &Irql );
  Last = Bus_Globals_TargetList;
  Walker = Bus_Globals_TargetList;
  while ( Walker != NULL )
    {
      if ( ( RtlCompareMemory ( &Walker->Target.ClientMac, ClientMac, 6 ) ==
	     6 )
	   && ( RtlCompareMemory ( &Walker->Target.ServerMac, ServerMac, 6 ) ==
		6 ) && Walker->Target.Major == Major
	   && Walker->Target.Minor == Minor )
	{
	  if ( Walker->Target.LBASize != LBASize )
	    {
	      DBG ( "LBASize changed for e%d.%d " "(%I64u->%I64u)\n", Major,
		    Minor, Walker->Target.LBASize, LBASize );
	      Walker->Target.LBASize = LBASize;
	    }
	  KeQuerySystemTime ( &Walker->Target.ProbeTime );
	  KeReleaseSpinLock ( &Bus_Globals_TargetListSpinLock, Irql );
	  return;
	}
      Last = Walker;
      Walker = Walker->Next;
    }

  if ( ( Walker =
	 ( PBUS_TARGETLIST ) ExAllocatePool ( NonPagedPool,
					      sizeof ( BUS_TARGETLIST ) ) ) ==
       NULL )
    {
      DBG ( "ExAllocatePool Target\n" );
      KeReleaseSpinLock ( &Bus_Globals_TargetListSpinLock, Irql );
      return;
    }
  Walker->Next = NULL;
  RtlCopyMemory ( Walker->Target.ClientMac, ClientMac, 6 );
  RtlCopyMemory ( Walker->Target.ServerMac, ServerMac, 6 );
  Walker->Target.Major = Major;
  Walker->Target.Minor = Minor;
  Walker->Target.LBASize = LBASize;
  KeQuerySystemTime ( &Walker->Target.ProbeTime );

  if ( Last == NULL )
    {
      Bus_Globals_TargetList = Walker;
    }
  else
    {
      Last->Next = Walker;
    }
  KeReleaseSpinLock ( &Bus_Globals_TargetListSpinLock, Irql );
}

VOID STDCALL
Bus_CleanupTargetList (
  void
 )
{
}

/*
 * Establish a pointer into the bus device's extension space
 */
bus__type_ptr STDCALL
get_bus_ptr (
  driver__dev_ext_ptr dev_ext_ptr
 )
{
  winvblock__uint8_ptr tmp = ( winvblock__uint8_ptr ) dev_ext_ptr;
  bus__type_ptr bus_ptr =
    ( bus__type_ptr ) ( tmp + sizeof ( driver__dev_ext ) );
  return bus_ptr;
}

/*
 * Establish a pointer into the child disk device's extension space
 */
disk__type_ptr STDCALL
get_disk_ptr (
  driver__dev_ext_ptr dev_ext_ptr
 )
{
  winvblock__uint8_ptr tmp = ( winvblock__uint8_ptr ) dev_ext_ptr;
  disk__type_ptr disk_ptr =
    ( disk__type_ptr ) ( tmp + sizeof ( driver__dev_ext ) );
  return disk_ptr;
}

/**
 * Add a child node to the bus
 *
 * @v BusDeviceObject The bus to add the node to
 * @v Disk            The disk to add
 * @v Boot            Is this a boot device?
 *
 * Returns TRUE for success, FALSE for failure
 */
winvblock__bool STDCALL
Bus_AddChild (
  IN PDEVICE_OBJECT BusDeviceObject,
  IN disk__type Disk,
  IN winvblock__bool Boot
 )
{
	/**
   * @v Status              Status of last operation
   * @v BusDeviceExtension  Shortcut to the bus' device extension
   * @v DeviceObject        The new node's device object
   * @v DeviceExtension     The new node's device extension
   * @v Walker              Walks the child nodes
   */
  NTSTATUS Status;
  PDEVICE_OBJECT DeviceObject;
  driver__dev_ext_ptr bus_dev_ext_ptr,
   disk_dev_ext_ptr;
  bus__type_ptr bus_ptr;
  disk__type_ptr disk_ptr,
   Walker;
  DEVICE_TYPE DiskType =
    Disk.DiskType == OpticalDisc ? FILE_DEVICE_CD_ROM : FILE_DEVICE_DISK;
  ULONG DiskType2 =
    Disk.DiskType ==
    OpticalDisc ? FILE_READ_ONLY_DEVICE | FILE_REMOVABLE_MEDIA : Disk.
    DiskType == FloppyDisk ? FILE_REMOVABLE_MEDIA | FILE_FLOPPY_DISKETTE : 0;

  DBG ( "Entry\n" );
  /*
   * Establish pointers into the bus device's extension space
   */
  bus_dev_ext_ptr = ( driver__dev_ext_ptr ) BusDeviceObject->DeviceExtension;
  bus_ptr = get_bus_ptr ( bus_dev_ext_ptr );
  /*
   * Create the child device
   */
  if ( !NT_SUCCESS
       ( Status =
	 IoCreateDevice ( bus_dev_ext_ptr->DriverObject,
			  sizeof ( driver__dev_ext ) + sizeof ( disk__type ),
			  NULL, DiskType,
			  FILE_AUTOGENERATED_DEVICE_NAME |
			  FILE_DEVICE_SECURE_OPEN | DiskType2, FALSE,
			  &DeviceObject ) ) )
    {
      Error ( "Bus_AddChild IoCreateDevice", Status );
      return FALSE;
    }
  /*
   * Establish pointers into the child disk device's extension space
   */
  disk_dev_ext_ptr = ( driver__dev_ext_ptr ) DeviceObject->DeviceExtension;
  disk_ptr = get_disk_ptr ( disk_dev_ext_ptr );
  /*
   * Clear the extension space and establish parameters
   */
  RtlZeroMemory ( disk_dev_ext_ptr,
		  sizeof ( driver__dev_ext ) + sizeof ( disk__type ) );
  disk_dev_ext_ptr->IsBus = FALSE;
  disk_dev_ext_ptr->dispatch = Disk_Dispatch;
  disk_dev_ext_ptr->Self = DeviceObject;
  disk_dev_ext_ptr->DriverObject = bus_dev_ext_ptr->DriverObject;
  disk_dev_ext_ptr->State = NotStarted;
  disk_dev_ext_ptr->OldState = NotStarted;

  /*
   * Copy the provided disk parameters into the disk extension space
   */
  RtlCopyMemory ( disk_ptr, &Disk, sizeof ( disk__type ) );
  disk_ptr->dev_ext_ptr = disk_dev_ext_ptr;
  disk_ptr->Parent = BusDeviceObject;
  disk_ptr->next_sibling_ptr = NULL;
  KeInitializeEvent ( &disk_ptr->SearchEvent, SynchronizationEvent, FALSE );
  KeInitializeSpinLock ( &disk_ptr->SpinLock );
  disk_ptr->BootDrive = Boot;
  disk_ptr->Unmount = FALSE;
  disk_ptr->DiskNumber = InterlockedIncrement ( &Bus_Globals_NextDisk ) - 1;
  /*
   * Some device parameters
   */
  DeviceObject->Flags |= DO_DIRECT_IO;	/* FIXME? */
  DeviceObject->Flags |= DO_POWER_INRUSH;	/* FIXME? */
  /*
   * Determine the disk's geometry differently for AoE/MEMDISK
   */
  disk_ptr->Initialize ( disk_dev_ext_ptr );

  DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
  /*
   * Add the new device's extension to the bus' list of children
   */
  if ( bus_ptr->first_child_ptr == NULL )
    {
      bus_ptr->first_child_ptr = disk_ptr;
    }
  else
    {
      Walker = ( disk__type_ptr ) bus_ptr->first_child_ptr;
      while ( Walker->next_sibling_ptr != NULL )
	Walker = Walker->next_sibling_ptr;
      Walker->next_sibling_ptr = disk_ptr;
    }
  bus_ptr->Children++;
  return TRUE;
}

irp__handler_decl ( Bus_DispatchPnP )
{
  NTSTATUS Status;
  KEVENT Event;
  PDEVICE_RELATIONS DeviceRelations;
  bus__type_ptr bus_ptr;
  disk__type_ptr Walker,
   Next;
  ULONG Count;

  /*
   * Establish a pointer into the bus device's extension space
   */
  bus_ptr = get_bus_ptr ( DeviceExtension );

  switch ( Stack->MinorFunction )
    {
      case IRP_MN_START_DEVICE:
	KeInitializeEvent ( &Event, NotificationEvent, FALSE );
	IoCopyCurrentIrpStackLocationToNext ( Irp );
	IoSetCompletionRoutine ( Irp,
				 ( PIO_COMPLETION_ROUTINE )
				 Bus_IoCompletionRoutine, ( PVOID ) & Event,
				 TRUE, TRUE, TRUE );
	Status = IoCallDriver ( bus_ptr->LowerDeviceObject, Irp );
	if ( Status == STATUS_PENDING )
	  {
	    DBG ( "Locked\n" );
	    KeWaitForSingleObject ( &Event, Executive, KernelMode, FALSE,
				    NULL );
	  }
	if ( NT_SUCCESS ( Status = Irp->IoStatus.Status ) )
	  {
	    DeviceExtension->OldState = DeviceExtension->State;
	    DeviceExtension->State = Started;
	  }
	Status = STATUS_SUCCESS;
	Irp->IoStatus.Status = Status;
	IoCompleteRequest ( Irp, IO_NO_INCREMENT );
	return Status;
      case IRP_MN_REMOVE_DEVICE:
	DeviceExtension->OldState = DeviceExtension->State;
	DeviceExtension->State = Deleted;
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoSkipCurrentIrpStackLocation ( Irp );
	Status = IoCallDriver ( bus_ptr->LowerDeviceObject, Irp );
	Walker = ( disk__type_ptr ) bus_ptr->first_child_ptr;
	while ( Walker != NULL )
	  {
	    Next = Walker->next_sibling_ptr;
	    IoDeleteDevice ( Walker->dev_ext_ptr->Self );
	    Walker = Next;
	  }
	bus_ptr->Children = 0;
	bus_ptr->first_child_ptr = NULL;
	IoDetachDevice ( bus_ptr->LowerDeviceObject );
	IoDeleteDevice ( DeviceExtension->Self );
	return Status;
      case IRP_MN_QUERY_DEVICE_RELATIONS:
	if ( Stack->Parameters.QueryDeviceRelations.Type != BusRelations
	     || Irp->IoStatus.Information )
	  {
	    Status = Irp->IoStatus.Status;
	    break;
	  }
	Count = 0;
	Walker = ( disk__type_ptr ) bus_ptr->first_child_ptr;
	while ( Walker != NULL )
	  {
	    Count++;
	    Walker = Walker->next_sibling_ptr;
	  }
	DeviceRelations =
	  ( PDEVICE_RELATIONS ) ExAllocatePool ( NonPagedPool,
						 sizeof ( DEVICE_RELATIONS ) +
						 ( sizeof ( PDEVICE_OBJECT ) *
						   Count ) );
	if ( DeviceRelations == NULL )
	  {
	    Irp->IoStatus.Information = 0;
	    Status = STATUS_INSUFFICIENT_RESOURCES;
	    break;
	  }
	DeviceRelations->Count = Count;

	Count = 0;
	Walker = ( disk__type_ptr ) bus_ptr->first_child_ptr;
	while ( Walker != NULL )
	  {
	    ObReferenceObject ( Walker->dev_ext_ptr->Self );
	    DeviceRelations->Objects[Count] = Walker->dev_ext_ptr->Self;
	    Count++;
	    Walker = Walker->next_sibling_ptr;
	  }
	Irp->IoStatus.Information = ( ULONG_PTR ) DeviceRelations;
	Status = STATUS_SUCCESS;
	break;
      case IRP_MN_QUERY_PNP_DEVICE_STATE:
	Irp->IoStatus.Information = 0;
	Status = STATUS_SUCCESS;
	break;
      case IRP_MN_QUERY_STOP_DEVICE:
	DeviceExtension->OldState = DeviceExtension->State;
	DeviceExtension->State = StopPending;
	Status = STATUS_SUCCESS;
	break;
      case IRP_MN_CANCEL_STOP_DEVICE:
	DeviceExtension->State = DeviceExtension->OldState;
	Status = STATUS_SUCCESS;
	break;
      case IRP_MN_STOP_DEVICE:
	DeviceExtension->OldState = DeviceExtension->State;
	DeviceExtension->State = Stopped;
	Status = STATUS_SUCCESS;
	break;
      case IRP_MN_QUERY_REMOVE_DEVICE:
	DeviceExtension->OldState = DeviceExtension->State;
	DeviceExtension->State = RemovePending;
	Status = STATUS_SUCCESS;
	break;
      case IRP_MN_CANCEL_REMOVE_DEVICE:
	DeviceExtension->State = DeviceExtension->OldState;
	Status = STATUS_SUCCESS;
	break;
      case IRP_MN_SURPRISE_REMOVAL:
	DeviceExtension->OldState = DeviceExtension->State;
	DeviceExtension->State = SurpriseRemovePending;
	Status = STATUS_SUCCESS;
	break;
      default:
	Status = Irp->IoStatus.Status;
    }

  Irp->IoStatus.Status = Status;
  IoSkipCurrentIrpStackLocation ( Irp );
  Status = IoCallDriver ( bus_ptr->LowerDeviceObject, Irp );
  return Status;
}

irp__handler_decl ( Bus_DispatchDeviceControl )
{
  NTSTATUS Status;
  winvblock__uint8_ptr Buffer;
  ULONG Count;
  PBUS_TARGETLIST TargetWalker;
  bus__type_ptr bus_ptr;
  disk__type_ptr DiskWalker,
   DiskWalkerPrevious;
  PMOUNT_TARGETS Targets;
  PMOUNT_DISKS Disks;
  KIRQL Irql;
  disk__type Disk;

  /*
   * Establish a pointer into the bus device's extension space
   */
  bus_ptr = get_bus_ptr ( DeviceExtension );

  switch ( Stack->Parameters.DeviceIoControl.IoControlCode )
    {
      case IOCTL_AOE_SCAN:
	DBG ( "Got IOCTL_AOE_SCAN...\n" );
	AoE_Start (  );
	KeAcquireSpinLock ( &Bus_Globals_TargetListSpinLock, &Irql );

	Count = 0;
	TargetWalker = Bus_Globals_TargetList;
	while ( TargetWalker != NULL )
	  {
	    Count++;
	    TargetWalker = TargetWalker->Next;
	  }

	if ( ( Targets =
	       ( PMOUNT_TARGETS ) ExAllocatePool ( NonPagedPool,
						   sizeof ( MOUNT_TARGETS ) +
						   ( Count *
						     sizeof
						     ( MOUNT_TARGET ) ) ) ) ==
	     NULL )
	  {
	    DBG ( "ExAllocatePool Targets\n" );
	    Irp->IoStatus.Information = 0;
	    Status = STATUS_INSUFFICIENT_RESOURCES;
	    break;
	  }
	Irp->IoStatus.Information =
	  sizeof ( MOUNT_TARGETS ) + ( Count * sizeof ( MOUNT_TARGET ) );
	Targets->Count = Count;

	Count = 0;
	TargetWalker = Bus_Globals_TargetList;
	while ( TargetWalker != NULL )
	  {
	    RtlCopyMemory ( &Targets->Target[Count], &TargetWalker->Target,
			    sizeof ( MOUNT_TARGET ) );
	    Count++;
	    TargetWalker = TargetWalker->Next;
	  }
	RtlCopyMemory ( Irp->AssociatedIrp.SystemBuffer, Targets,
			( Stack->Parameters.DeviceIoControl.
			  OutputBufferLength <
			  ( sizeof ( MOUNT_TARGETS ) +
			    ( Count *
			      sizeof ( MOUNT_TARGET ) ) ) ? Stack->Parameters.
			  DeviceIoControl.
			  OutputBufferLength : ( sizeof ( MOUNT_TARGETS ) +
						 ( Count *
						   sizeof
						   ( MOUNT_TARGET ) ) ) ) );
	ExFreePool ( Targets );

	KeReleaseSpinLock ( &Bus_Globals_TargetListSpinLock, Irql );
	Status = STATUS_SUCCESS;
	break;
      case IOCTL_AOE_SHOW:
	DBG ( "Got IOCTL_AOE_SHOW...\n" );

	Count = 0;
	DiskWalker = ( disk__type_ptr ) bus_ptr->first_child_ptr;
	while ( DiskWalker != NULL )
	  {
	    Count++;
	    DiskWalker = DiskWalker->next_sibling_ptr;
	  }

	if ( ( Disks =
	       ( PMOUNT_DISKS ) ExAllocatePool ( NonPagedPool,
						 sizeof ( MOUNT_DISKS ) +
						 ( Count *
						   sizeof ( MOUNT_DISK ) ) ) )
	     == NULL )
	  {
	    DBG ( "ExAllocatePool Disks\n" );
	    Irp->IoStatus.Information = 0;
	    Status = STATUS_INSUFFICIENT_RESOURCES;
	    break;
	  }
	Irp->IoStatus.Information =
	  sizeof ( MOUNT_DISKS ) + ( Count * sizeof ( MOUNT_DISK ) );
	Disks->Count = Count;

	Count = 0;
	DiskWalker = ( disk__type_ptr ) bus_ptr->first_child_ptr;
	while ( DiskWalker != NULL )
	  {
	    Disks->Disk[Count].Disk = DiskWalker->DiskNumber;
	    RtlCopyMemory ( &Disks->Disk[Count].ClientMac,
			    &DiskWalker->AoE.ClientMac, 6 );
	    RtlCopyMemory ( &Disks->Disk[Count].ServerMac,
			    &DiskWalker->AoE.ServerMac, 6 );
	    Disks->Disk[Count].Major = DiskWalker->AoE.Major;
	    Disks->Disk[Count].Minor = DiskWalker->AoE.Minor;
	    Disks->Disk[Count].LBASize = DiskWalker->LBADiskSize;
	    Count++;
	    DiskWalker = DiskWalker->next_sibling_ptr;
	  }
	RtlCopyMemory ( Irp->AssociatedIrp.SystemBuffer, Disks,
			( Stack->Parameters.DeviceIoControl.
			  OutputBufferLength <
			  ( sizeof ( MOUNT_DISKS ) +
			    ( Count *
			      sizeof ( MOUNT_DISK ) ) ) ? Stack->Parameters.
			  DeviceIoControl.
			  OutputBufferLength : ( sizeof ( MOUNT_DISKS ) +
						 ( Count *
						   sizeof
						   ( MOUNT_DISK ) ) ) ) );
	ExFreePool ( Disks );

	Status = STATUS_SUCCESS;
	break;
      case IOCTL_AOE_MOUNT:
	Buffer = Irp->AssociatedIrp.SystemBuffer;
	DBG ( "Got IOCTL_AOE_MOUNT for client: %02x:%02x:%02x:%02x:%02x:%02x "
	      "Major:%d Minor:%d\n", Buffer[0], Buffer[1], Buffer[2],
	      Buffer[3], Buffer[4], Buffer[5],
	      *( winvblock__uint16_ptr ) ( &Buffer[6] ),
	      ( winvblock__uint8 ) Buffer[8] );
	Disk.Initialize = AoE_SearchDrive;
	RtlCopyMemory ( Disk.AoE.ClientMac, Buffer, 6 );
	RtlFillMemory ( Disk.AoE.ServerMac, 6, 0xff );
	Disk.AoE.Major = *( winvblock__uint16_ptr ) ( &Buffer[6] );
	Disk.AoE.Minor = ( winvblock__uint8 ) Buffer[8];
	Disk.AoE.MaxSectorsPerPacket = 1;
	Disk.AoE.Timeout = 200000;	/* 20 ms. */
	Disk.IsRamdisk = FALSE;
	if ( !Bus_AddChild ( DeviceObject, Disk, FALSE ) )
	  {
	    DBG ( "Bus_AddChild() failed\n" );
	  }
	else
	  {
	    if ( bus_ptr->PhysicalDeviceObject != NULL )
	      IoInvalidateDeviceRelations ( bus_ptr->PhysicalDeviceObject,
					    BusRelations );
	  }
	Irp->IoStatus.Information = 0;
	Status = STATUS_SUCCESS;
	break;
      case IOCTL_AOE_UMOUNT:
	Buffer = Irp->AssociatedIrp.SystemBuffer;
	DBG ( "Got IOCTL_AOE_UMOUNT for disk: %d\n", *( PULONG ) Buffer );
	DiskWalker = ( disk__type_ptr ) bus_ptr->first_child_ptr;
	DiskWalkerPrevious = DiskWalker;
	while ( ( DiskWalker != NULL )
		&& ( DiskWalker->DiskNumber != *( PULONG ) Buffer ) )
	  {
	    DiskWalkerPrevious = DiskWalker;
	    DiskWalker = DiskWalker->next_sibling_ptr;
	  }
	if ( DiskWalker != NULL )
	  {
	    if ( DiskWalker->BootDrive )
	      {
		DBG ( "Cannot unmount a boot drive.\n" );
		Irp->IoStatus.Information = 0;
		Status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	      }
	    DBG ( "Deleting disk %d\n", DiskWalker->DiskNumber );
	    if ( DiskWalker == ( disk__type_ptr ) bus_ptr->first_child_ptr )
	      {
		bus_ptr->first_child_ptr =
		  ( winvblock__uint8_ptr ) DiskWalker->next_sibling_ptr;
	      }
	    else
	      {
		DiskWalkerPrevious->next_sibling_ptr =
		  DiskWalker->next_sibling_ptr;
	      }
	    DiskWalker->Unmount = TRUE;
	    DiskWalker->next_sibling_ptr = NULL;
	    if ( bus_ptr->PhysicalDeviceObject != NULL )
	      IoInvalidateDeviceRelations ( bus_ptr->PhysicalDeviceObject,
					    BusRelations );
	  }
	bus_ptr->Children--;
	Irp->IoStatus.Information = 0;
	Status = STATUS_SUCCESS;
	break;
      default:
	Irp->IoStatus.Information = 0;
	Status = STATUS_INVALID_DEVICE_REQUEST;
    }

  Irp->IoStatus.Status = Status;
  IoCompleteRequest ( Irp, IO_NO_INCREMENT );
  return Status;
}

irp__handler_decl ( Bus_DispatchSystemControl )
{
  bus__type_ptr bus_ptr = get_bus_ptr ( DeviceExtension );
  DBG ( "...\n" );
  IoSkipCurrentIrpStackLocation ( Irp );
  return IoCallDriver ( bus_ptr->LowerDeviceObject, Irp );
}

irp__handler_decl ( Bus_Dispatch )
{
  NTSTATUS Status;
  bus__type_ptr bus_ptr = get_bus_ptr ( DeviceExtension );

  switch ( Stack->MajorFunction )
    {
      case IRP_MJ_POWER:
	PoStartNextPowerIrp ( Irp );
	IoSkipCurrentIrpStackLocation ( Irp );
	Status = PoCallDriver ( bus_ptr->LowerDeviceObject, Irp );
	break;
      case IRP_MJ_PNP:
	Status =
	  Bus_DispatchPnP ( DeviceObject, Irp, Stack, DeviceExtension,
			    completion_ptr );
	break;
      case IRP_MJ_SYSTEM_CONTROL:
	Status =
	  Bus_DispatchSystemControl ( DeviceObject, Irp, Stack,
				      DeviceExtension, completion_ptr );
	break;
      case IRP_MJ_DEVICE_CONTROL:
	Status =
	  Bus_DispatchDeviceControl ( DeviceObject, Irp, Stack,
				      DeviceExtension, completion_ptr );
	break;
      default:
	Status = STATUS_NOT_SUPPORTED;
	Irp->IoStatus.Status = Status;
	IoCompleteRequest ( Irp, IO_NO_INCREMENT );
    }
  return Status;
}

static NTSTATUS STDCALL
Bus_IoCompletionRoutine (
  IN PDEVICE_OBJECT DeviceObject,
  IN PIRP Irp,
  IN PKEVENT Event
 )
{
  KeSetEvent ( Event, 0, FALSE );
  return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS STDCALL
Bus_GetDeviceCapabilities (
  IN PDEVICE_OBJECT DeviceObject,
  IN PDEVICE_CAPABILITIES DeviceCapabilities
 )
{
  IO_STATUS_BLOCK ioStatus;
  KEVENT pnpEvent;
  NTSTATUS status;
  PDEVICE_OBJECT targetObject;
  PIO_STACK_LOCATION irpStack;
  PIRP pnpIrp;

  RtlZeroMemory ( DeviceCapabilities, sizeof ( DEVICE_CAPABILITIES ) );
  DeviceCapabilities->Size = sizeof ( DEVICE_CAPABILITIES );
  DeviceCapabilities->Version = 1;
  DeviceCapabilities->Address = -1;
  DeviceCapabilities->UINumber = -1;

  KeInitializeEvent ( &pnpEvent, NotificationEvent, FALSE );
  targetObject = IoGetAttachedDeviceReference ( DeviceObject );
  pnpIrp =
    IoBuildSynchronousFsdRequest ( IRP_MJ_PNP, targetObject, NULL, 0, NULL,
				   &pnpEvent, &ioStatus );
  if ( pnpIrp == NULL )
    {
      status = STATUS_INSUFFICIENT_RESOURCES;
    }
  else
    {
      pnpIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;
      irpStack = IoGetNextIrpStackLocation ( pnpIrp );
      RtlZeroMemory ( irpStack, sizeof ( IO_STACK_LOCATION ) );
      irpStack->MajorFunction = IRP_MJ_PNP;
      irpStack->MinorFunction = IRP_MN_QUERY_CAPABILITIES;
      irpStack->Parameters.DeviceCapabilities.Capabilities =
	DeviceCapabilities;
      status = IoCallDriver ( targetObject, pnpIrp );
      if ( status == STATUS_PENDING )
	{
	  KeWaitForSingleObject ( &pnpEvent, Executive, KernelMode, FALSE,
				  NULL );
	  status = ioStatus.Status;
	}
    }
  ObDereferenceObject ( targetObject );
  return status;
}

NTSTATUS STDCALL
Bus_AddDevice (
  IN PDRIVER_OBJECT DriverObject,
  IN PDEVICE_OBJECT PhysicalDeviceObject
 )
{
  NTSTATUS Status;
  UNICODE_STRING DeviceName,
   DosDeviceName;
  driver__dev_ext_ptr bus_dev_ext_ptr;
  bus__type_ptr bus_ptr;

  DBG ( "Entry\n" );
  if ( bus__fdo )
    return STATUS_SUCCESS;
  RtlInitUnicodeString ( &DeviceName, L"\\Device\\AoE" );
  RtlInitUnicodeString ( &DosDeviceName, L"\\DosDevices\\AoE" );
  if ( !NT_SUCCESS
       ( Status =
	 IoCreateDevice ( DriverObject,
			  sizeof ( driver__dev_ext ) + sizeof ( bus__type ),
			  &DeviceName, FILE_DEVICE_CONTROLLER,
			  FILE_DEVICE_SECURE_OPEN, FALSE, &bus__fdo ) ) )
    {
      return Error ( "Bus_AddDevice IoCreateDevice", Status );
    }
  if ( !NT_SUCCESS
       ( Status = IoCreateSymbolicLink ( &DosDeviceName, &DeviceName ) ) )
    {
      IoDeleteDevice ( bus__fdo );
      return Error ( "Bus_AddDevice IoCreateSymbolicLink", Status );
    }

  bus_dev_ext_ptr = ( driver__dev_ext_ptr ) bus__fdo->DeviceExtension;
  RtlZeroMemory ( bus_dev_ext_ptr,
		  sizeof ( driver__dev_ext ) + sizeof ( bus__type ) );
  bus_dev_ext_ptr->IsBus = TRUE;
  bus_dev_ext_ptr->dispatch = Bus_Dispatch;
  bus_dev_ext_ptr->DriverObject = DriverObject;
  bus_dev_ext_ptr->Self = bus__fdo;
  bus_dev_ext_ptr->State = NotStarted;
  bus_dev_ext_ptr->OldState = NotStarted;
  /*
   * Establish a pointer into the bus device's extension space
   */
  bus_ptr = get_bus_ptr ( bus_dev_ext_ptr );
  bus_ptr->PhysicalDeviceObject = PhysicalDeviceObject;
  bus_ptr->Children = 0;
  bus_ptr->first_child_ptr = NULL;
  KeInitializeSpinLock ( &bus_ptr->SpinLock );
  bus__fdo->Flags |= DO_DIRECT_IO;	/* FIXME? */
  bus__fdo->Flags |= DO_POWER_INRUSH;	/* FIXME? */
  /*
   * Add the bus to the device tree
   */
  if ( PhysicalDeviceObject != NULL )
    {
      if ( ( bus_ptr->LowerDeviceObject =
	     IoAttachDeviceToDeviceStack ( bus__fdo,
					   PhysicalDeviceObject ) ) == NULL )
	{
	  IoDeleteDevice ( bus__fdo );
	  bus__fdo = NULL;
	  return Error ( "AddDevice IoAttachDeviceToDeviceStack",
			 STATUS_NO_SUCH_DEVICE );
	}
    }
  bus__fdo->Flags &= ~DO_DEVICE_INITIALIZING;
#ifdef RIS
  bus_dev_ext_ptr->State = Started;
#endif
  return STATUS_SUCCESS;
}
