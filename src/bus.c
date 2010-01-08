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
#include "mount.h"
#include "bus.h"
#include "bus_pnp.h"
#include "bus_dev_ctl.h"
#include "debug.h"
#include "protocol.h"
#include "probe.h"

bus__target_list_ptr Bus_Globals_TargetList = NULL;
KSPIN_LOCK Bus_Globals_TargetListSpinLock;
static winvblock__uint32 Bus_Globals_NextDisk = 0;
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

void STDCALL
Bus_Stop (
  void
 )
{
  UNICODE_STRING DosDeviceName;
  bus__target_list_ptr Walker,
   Next;
  KIRQL Irql;

  DBG ( "Entry\n" );
  KeAcquireSpinLock ( &Bus_Globals_TargetListSpinLock, &Irql );
  Walker = Bus_Globals_TargetList;
  while ( Walker != NULL )
    {
      Next = Walker->next;
      ExFreePool ( Walker );
      Walker = Next;
    }
  KeReleaseSpinLock ( &Bus_Globals_TargetListSpinLock, Irql );
  RtlInitUnicodeString ( &DosDeviceName,
			 L"\\DosDevices\\" winvblock__literal_w );
  IoDeleteSymbolicLink ( &DosDeviceName );
  bus__fdo = NULL;
}

void STDCALL
Bus_AddTarget (
  IN winvblock__uint8_ptr ClientMac,
  IN winvblock__uint8_ptr ServerMac,
  winvblock__uint16 Major,
  winvblock__uint8 Minor,
  LONGLONG LBASize
 )
{
  bus__target_list_ptr Walker,
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
      Walker = Walker->next;
    }

  if ( ( Walker =
	 ( bus__target_list_ptr ) ExAllocatePool ( NonPagedPool,
						   sizeof
						   ( bus__target_list ) ) ) ==
       NULL )
    {
      DBG ( "ExAllocatePool Target\n" );
      KeReleaseSpinLock ( &Bus_Globals_TargetListSpinLock, Irql );
      return;
    }
  Walker->next = NULL;
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
      Last->next = Walker;
    }
  KeReleaseSpinLock ( &Bus_Globals_TargetListSpinLock, Irql );
}

void STDCALL
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
 *
 * Returns TRUE for success, FALSE for failure
 */
winvblock__bool STDCALL
Bus_AddChild (
  IN PDEVICE_OBJECT BusDeviceObject,
  IN disk__type_ptr Disk
 )
{
  /**
   * @v DeviceObject        The new node's device object
   * @v bus_ptr             A pointer to the bus device's details
   * @v disk_ptr            A pointer to the disk device's details
   * @v Walker              Walks the child nodes
   */
  PDEVICE_OBJECT DeviceObject;
  bus__type_ptr bus_ptr;
  disk__type_ptr disk_ptr,
   Walker;

  DBG ( "Entry\n" );
  /*
   * Establish a pointer into the bus device's extension space
   */
  bus_ptr = get_bus_ptr ( BusDeviceObject->DeviceExtension );
  /*
   * Create the child device
   */
  DeviceObject = disk__create_pdo ( Disk );
  if ( !DeviceObject )
    return FALSE;
  /*
   * Get a pointer to the child device's disk
   */
  disk_ptr = get_disk_ptr ( DeviceObject->DeviceExtension );
  disk_ptr->Parent = BusDeviceObject;
  disk_ptr->next_sibling_ptr = NULL;
  disk_ptr->DiskNumber = InterlockedIncrement ( &Bus_Globals_NextDisk ) - 1;
  /*
   * Determine the disk's geometry differently for AoE/RAM/file disk
   */
  disk__init ( &disk_ptr->dev_ext );
  DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
  /*
   * Add the new device's extension to the bus' list of children
   */
  if ( bus_ptr->first_child_ptr == NULL )
    {
      bus_ptr->first_child_ptr = ( winvblock__uint8_ptr ) disk_ptr;
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

static
irp__handler_decl (
  sys_ctl
 )
{
  bus__type_ptr bus_ptr = get_bus_ptr ( DeviceExtension );
  DBG ( "...\n" );
  IoSkipCurrentIrpStackLocation ( Irp );
  *completion_ptr = TRUE;
  return IoCallDriver ( bus_ptr->LowerDeviceObject, Irp );
}

static
irp__handler_decl (
  power
 )
{
  bus__type_ptr bus_ptr = get_bus_ptr ( DeviceExtension );
  PoStartNextPowerIrp ( Irp );
  IoSkipCurrentIrpStackLocation ( Irp );
  *completion_ptr = TRUE;
  return PoCallDriver ( bus_ptr->LowerDeviceObject, Irp );
}

static irp__handling handling_table[] = {
  /*
   * Major, minor, any major?, any minor?, handler
   */
  {IRP_MJ_SYSTEM_CONTROL, 0, FALSE, TRUE, sys_ctl}
  ,
  {IRP_MJ_POWER, 0, FALSE, TRUE, power}
  ,
  {IRP_MJ_DEVICE_CONTROL, 0, FALSE, TRUE, bus_dev_ctl__dispatch}
  ,
  {IRP_MJ_PNP, 0, FALSE, TRUE, bus_pnp__simple}
  ,
  {IRP_MJ_PNP, IRP_MN_START_DEVICE, FALSE, FALSE, bus_pnp__start_dev}
  ,
  {IRP_MJ_PNP, IRP_MN_REMOVE_DEVICE, FALSE, FALSE, bus_pnp__remove_dev}
  ,
  {IRP_MJ_PNP, IRP_MN_QUERY_DEVICE_RELATIONS, FALSE, FALSE,
   bus_pnp__query_dev_relations}
};

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
  size_t new_dev_ext_size;
  driver__dev_ext_ptr bus_dev_ext_ptr;
  bus__type_ptr bus_ptr;

  DBG ( "Entry\n" );
  if ( bus__fdo )
    return STATUS_SUCCESS;
  RtlInitUnicodeString ( &DeviceName, L"\\Device\\" winvblock__literal_w );
  RtlInitUnicodeString ( &DosDeviceName,
			 L"\\DosDevices\\" winvblock__literal_w );
  new_dev_ext_size =
    sizeof ( bus__type ) + driver__handling_table_size +
    sizeof ( handling_table );
  if ( !NT_SUCCESS
       ( Status =
	 IoCreateDevice ( DriverObject, new_dev_ext_size, &DeviceName,
			  FILE_DEVICE_CONTROLLER, FILE_DEVICE_SECURE_OPEN,
			  FALSE, &bus__fdo ) ) )
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
  RtlZeroMemory ( bus_dev_ext_ptr, new_dev_ext_size );
  bus_dev_ext_ptr->IsBus = TRUE;
  bus_dev_ext_ptr->size = sizeof ( bus__type );
  bus_dev_ext_ptr->DriverObject = DriverObject;
  bus_dev_ext_ptr->Self = bus__fdo;
  bus_dev_ext_ptr->State = NotStarted;
  bus_dev_ext_ptr->OldState = NotStarted;
  bus_dev_ext_ptr->irp_handler_stack_ptr =
    ( irp__handling_ptr ) ( ( winvblock__uint8 * ) bus_dev_ext_ptr +
			    sizeof ( bus__type ) );
  RtlCopyMemory ( bus_dev_ext_ptr->irp_handler_stack_ptr,
		  driver__handling_table, driver__handling_table_size );
  RtlCopyMemory ( ( winvblock__uint8 * ) bus_dev_ext_ptr->
		  irp_handler_stack_ptr + driver__handling_table_size,
		  handling_table, sizeof ( handling_table ) );
  bus_dev_ext_ptr->irp_handler_stack_size =
    ( driver__handling_table_size +
      sizeof ( handling_table ) ) / sizeof ( irp__handling );
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
  probe__disks (  );
  return STATUS_SUCCESS;
}
