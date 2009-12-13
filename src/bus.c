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
#include "driver.h"
#include "aoe.h"
#include "mount.h"
#include "debug.h"
#include "mdi.h"
#include "protocol.h"
#include "probe.h"

/* in this file */
static IRPHandler_Declaration (
	Bus_DispatchNotSupported
 );
static IRPHandler_Declaration (
	Bus_DispatchPower
 );
winvblock__bool STDCALL Bus_AddChild (
	IN PDEVICE_OBJECT BusDeviceObject,
	IN DISK_DISK Disk,
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
PDEVICE_OBJECT Bus_Globals_Self = NULL;

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
	Bus_Globals_Self = NULL;
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
	IN DISK_DISK Disk,
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
	driver__dev_ext_ptr BusDeviceExtension =
		( driver__dev_ext_ptr ) BusDeviceObject->DeviceExtension;
	PDEVICE_OBJECT DeviceObject;
	driver__dev_ext_ptr DeviceExtension,
	 Walker;
	DEVICE_TYPE DiskType =
		Disk.DiskType == OpticalDisc ? FILE_DEVICE_CD_ROM : FILE_DEVICE_DISK;
	ULONG DiskType2 =
		Disk.DiskType ==
		OpticalDisc ? FILE_READ_ONLY_DEVICE | FILE_REMOVABLE_MEDIA : Disk.DiskType
		== FloppyDisk ? FILE_REMOVABLE_MEDIA | FILE_FLOPPY_DISKETTE : 0;

	DBG ( "Entry\n" );
	/*
	 * Create the child device
	 */
	if ( !NT_SUCCESS
			 ( Status =
				 IoCreateDevice ( BusDeviceExtension->DriverObject,
													sizeof ( driver__dev_ext ), NULL, DiskType,
													FILE_AUTOGENERATED_DEVICE_NAME |
													FILE_DEVICE_SECURE_OPEN | DiskType2, FALSE,
													&DeviceObject ) ) )
		{
			Error ( "Bus_AddChild IoCreateDevice", Status );
			return FALSE;
		}
	/*
	 * Establish a shotcut to the child device's extension
	 */
	DeviceExtension = ( driver__dev_ext_ptr ) DeviceObject->DeviceExtension;
	/*
	 * Clear the extension and establish parameters
	 */
	RtlZeroMemory ( DeviceExtension, sizeof ( driver__dev_ext ) );
	DeviceExtension->IsBus = FALSE;
	DeviceExtension->Dispatch = Disk_Dispatch;
	DeviceExtension->Self = DeviceObject;
	DeviceExtension->DriverObject = BusDeviceExtension->DriverObject;
	DeviceExtension->State = NotStarted;
	DeviceExtension->OldState = NotStarted;

	RtlCopyMemory ( &DeviceExtension->Disk, &Disk, sizeof ( DISK_DISK ) );
	DeviceExtension->Disk.Parent = BusDeviceObject;
	DeviceExtension->Disk.Next = NULL;
	KeInitializeEvent ( &DeviceExtension->Disk.SearchEvent, SynchronizationEvent,
											FALSE );
	KeInitializeSpinLock ( &DeviceExtension->Disk.SpinLock );
	DeviceExtension->Disk.BootDrive = Boot;
	DeviceExtension->Disk.Unmount = FALSE;
	DeviceExtension->Disk.DiskNumber =
		InterlockedIncrement ( &Bus_Globals_NextDisk ) - 1;
	/*
	 * Some device parameters
	 */
	DeviceObject->Flags |= DO_DIRECT_IO;	/* FIXME? */
	DeviceObject->Flags |= DO_POWER_INRUSH;	/* FIXME? */
	/*
	 * Determine the disk's geometry differently for AoE/MEMDISK
	 */
	DeviceExtension->Disk.Initialize ( DeviceExtension );

	DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
	/*
	 * Add the new device's extension to the bus' list of children
	 */
	if ( BusDeviceExtension->Bus.ChildList == NULL )
		{
			BusDeviceExtension->Bus.ChildList = DeviceExtension;
		}
	else
		{
			Walker = BusDeviceExtension->Bus.ChildList;
			while ( Walker->Disk.Next != NULL )
				Walker = Walker->Disk.Next;
			Walker->Disk.Next = DeviceExtension;
		}
	BusDeviceExtension->Bus.Children++;
	return TRUE;
}

IRPHandler_Declaration ( Bus_DispatchPnP )
{
	NTSTATUS Status;
	KEVENT Event;
	PDEVICE_RELATIONS DeviceRelations;
	driver__dev_ext_ptr Walker,
	 Next;
	ULONG Count;

	switch ( Stack->MinorFunction )
		{
			case IRP_MN_START_DEVICE:
				KeInitializeEvent ( &Event, NotificationEvent, FALSE );
				IoCopyCurrentIrpStackLocationToNext ( Irp );
				IoSetCompletionRoutine ( Irp,
																 ( PIO_COMPLETION_ROUTINE )
																 Bus_IoCompletionRoutine, ( PVOID ) & Event,
																 TRUE, TRUE, TRUE );
				Status = IoCallDriver ( DeviceExtension->Bus.LowerDeviceObject, Irp );
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
				Status = IoCallDriver ( DeviceExtension->Bus.LowerDeviceObject, Irp );
				Walker = DeviceExtension->Bus.ChildList;
				while ( Walker != NULL )
					{
						Next = Walker->Disk.Next;
						IoDeleteDevice ( Walker->Self );
						Walker = Next;
					}
				DeviceExtension->Bus.Children = 0;
				DeviceExtension->Bus.ChildList = NULL;
				IoDetachDevice ( DeviceExtension->Bus.LowerDeviceObject );
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
				Walker = DeviceExtension->Bus.ChildList;
				while ( Walker != NULL )
					{
						Count++;
						Walker = Walker->Disk.Next;
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
				Walker = DeviceExtension->Bus.ChildList;
				while ( Walker != NULL )
					{
						DeviceRelations->Objects[Count] = Walker->Self;
						Count++;
						Walker = Walker->Disk.Next;
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
	Status = IoCallDriver ( DeviceExtension->Bus.LowerDeviceObject, Irp );
	return Status;
}

IRPHandler_Declaration ( Bus_DispatchDeviceControl )
{
	NTSTATUS Status;
	winvblock__uint8_ptr Buffer;
	ULONG Count;
	PBUS_TARGETLIST TargetWalker;
	driver__dev_ext_ptr DiskWalker,
	 DiskWalkerPrevious;
	PMOUNT_TARGETS Targets;
	PMOUNT_DISKS Disks;
	KIRQL Irql;
	DISK_DISK Disk;

	switch ( Stack->Parameters.DeviceIoControl.IoControlCode )
		{
			case IOCTL_AOE_SCAN:
				DBG ( "Got IOCTL_AOE_SCAN...\n" );
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
												( Stack->Parameters.
													DeviceIoControl.OutputBufferLength <
													( sizeof ( MOUNT_TARGETS ) +
														( Count *
															sizeof ( MOUNT_TARGET ) ) ) ? Stack->
													Parameters.DeviceIoControl.OutputBufferLength
													: ( sizeof ( MOUNT_TARGETS ) +
															( Count * sizeof ( MOUNT_TARGET ) ) ) ) );
				ExFreePool ( Targets );

				KeReleaseSpinLock ( &Bus_Globals_TargetListSpinLock, Irql );
				Status = STATUS_SUCCESS;
				break;
			case IOCTL_AOE_SHOW:
				DBG ( "Got IOCTL_AOE_SHOW...\n" );

				Count = 0;
				DiskWalker = DeviceExtension->Bus.ChildList;
				while ( DiskWalker != NULL )
					{
						Count++;
						DiskWalker = DiskWalker->Disk.Next;
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
				DiskWalker = DeviceExtension->Bus.ChildList;
				while ( DiskWalker != NULL )
					{
						Disks->Disk[Count].Disk = DiskWalker->Disk.DiskNumber;
						RtlCopyMemory ( &Disks->Disk[Count].ClientMac,
														&DiskWalker->Disk.AoE.ClientMac, 6 );
						RtlCopyMemory ( &Disks->Disk[Count].ServerMac,
														&DiskWalker->Disk.AoE.ServerMac, 6 );
						Disks->Disk[Count].Major = DiskWalker->Disk.AoE.Major;
						Disks->Disk[Count].Minor = DiskWalker->Disk.AoE.Minor;
						Disks->Disk[Count].LBASize = DiskWalker->Disk.LBADiskSize;
						Count++;
						DiskWalker = DiskWalker->Disk.Next;
					}
				RtlCopyMemory ( Irp->AssociatedIrp.SystemBuffer, Disks,
												( Stack->Parameters.
													DeviceIoControl.OutputBufferLength <
													( sizeof ( MOUNT_DISKS ) +
														( Count *
															sizeof ( MOUNT_DISK ) ) ) ? Stack->
													Parameters.DeviceIoControl.OutputBufferLength
													: ( sizeof ( MOUNT_DISKS ) +
															( Count * sizeof ( MOUNT_DISK ) ) ) ) );
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
						if ( DeviceExtension->Bus.PhysicalDeviceObject != NULL )
							IoInvalidateDeviceRelations ( DeviceExtension->Bus.
																						PhysicalDeviceObject,
																						BusRelations );
					}
				Irp->IoStatus.Information = 0;
				Status = STATUS_SUCCESS;
				break;
			case IOCTL_AOE_UMOUNT:
				Buffer = Irp->AssociatedIrp.SystemBuffer;
				DBG ( "Got IOCTL_AOE_UMOUNT for disk: %d\n", *( PULONG ) Buffer );
				DiskWalker = DeviceExtension->Bus.ChildList;
				DiskWalkerPrevious = DiskWalker;
				while ( ( DiskWalker != NULL ) && ( !DiskWalker->Disk.BootDrive )
								&& ( DiskWalker->Disk.DiskNumber != *( PULONG ) Buffer ) )
					{
						DiskWalkerPrevious = DiskWalker;
						DiskWalker = DiskWalker->Disk.Next;
					}
				if ( DiskWalker != NULL )
					{
						DBG ( "Deleting disk %d\n", DiskWalker->Disk.DiskNumber );
						if ( DiskWalker == DeviceExtension->Bus.ChildList )
							{
								DeviceExtension->Bus.ChildList = DiskWalker->Disk.Next;
							}
						else
							{
								DiskWalkerPrevious->Disk.Next = DiskWalker->Disk.Next;
							}
						DiskWalker->Disk.Unmount = TRUE;
						DiskWalker->Disk.Next = NULL;
						if ( DeviceExtension->Bus.PhysicalDeviceObject != NULL )
							IoInvalidateDeviceRelations ( DeviceExtension->Bus.
																						PhysicalDeviceObject,
																						BusRelations );
					}
				DeviceExtension->Bus.Children--;
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

IRPHandler_Declaration ( Bus_DispatchSystemControl )
{
	DBG ( "...\n" );
	IoSkipCurrentIrpStackLocation ( Irp );
	return IoCallDriver ( DeviceExtension->Bus.LowerDeviceObject, Irp );
}

IRPHandler_Declaration ( Bus_Dispatch )
{
	NTSTATUS Status;

	switch ( Stack->MajorFunction )
		{
			case IRP_MJ_POWER:
				PoStartNextPowerIrp ( Irp );
				IoSkipCurrentIrpStackLocation ( Irp );
				Status = PoCallDriver ( DeviceExtension->Bus.LowerDeviceObject, Irp );
				break;
			case IRP_MJ_PNP:
				Status = Bus_DispatchPnP ( DeviceObject, Irp, Stack, DeviceExtension );
				break;
			case IRP_MJ_SYSTEM_CONTROL:
				Status =
					Bus_DispatchSystemControl ( DeviceObject, Irp, Stack,
																			DeviceExtension );
				break;
			case IRP_MJ_DEVICE_CONTROL:
				Status =
					Bus_DispatchDeviceControl ( DeviceObject, Irp, Stack,
																			DeviceExtension );
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
	driver__dev_ext_ptr DeviceExtension;
	PDEVICE_OBJECT DeviceObject;

	DBG ( "Entry\n" );
	if ( Bus_Globals_Self )
		return STATUS_SUCCESS;
	RtlInitUnicodeString ( &DeviceName, L"\\Device\\AoE" );
	RtlInitUnicodeString ( &DosDeviceName, L"\\DosDevices\\AoE" );
	if ( !NT_SUCCESS
			 ( Status =
				 IoCreateDevice ( DriverObject, sizeof ( driver__dev_ext ),
													&DeviceName, FILE_DEVICE_CONTROLLER,
													FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject ) ) )
		{
			return Error ( "Bus_AddDevice IoCreateDevice", Status );
		}
	if ( !NT_SUCCESS
			 ( Status = IoCreateSymbolicLink ( &DosDeviceName, &DeviceName ) ) )
		{
			IoDeleteDevice ( DeviceObject );
			return Error ( "Bus_AddDevice IoCreateSymbolicLink", Status );
		}

	DeviceExtension = ( driver__dev_ext_ptr ) DeviceObject->DeviceExtension;
	RtlZeroMemory ( DeviceExtension, sizeof ( driver__dev_ext ) );
	DeviceExtension->IsBus = TRUE;
	DeviceExtension->Dispatch = Bus_Dispatch;
	DeviceExtension->DriverObject = DriverObject;
	DeviceExtension->Self = DeviceObject;
	DeviceExtension->State = NotStarted;
	DeviceExtension->OldState = NotStarted;
	DeviceExtension->Bus.PhysicalDeviceObject = PhysicalDeviceObject;
	DeviceExtension->Bus.Children = 0;
	DeviceExtension->Bus.ChildList = NULL;
	KeInitializeSpinLock ( &DeviceExtension->Bus.SpinLock );
	DeviceObject->Flags |= DO_DIRECT_IO;	/* FIXME? */
	DeviceObject->Flags |= DO_POWER_INRUSH;	/* FIXME? */
	/*
	 * Add the bus to the device tree
	 */
	if ( PhysicalDeviceObject != NULL )
		{
			if ( ( DeviceExtension->Bus.LowerDeviceObject =
						 IoAttachDeviceToDeviceStack ( DeviceObject,
																					 PhysicalDeviceObject ) ) == NULL )
				{
					IoDeleteDevice ( DeviceObject );
					return Error ( "AddDevice IoAttachDeviceToDeviceStack",
												 STATUS_NO_SUCH_DEVICE );
				}
		}
	DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
#ifdef RIS
	DeviceExtension->State = Started;
#endif
	Bus_Globals_Self = DeviceObject;
	return STATUS_SUCCESS;
}
