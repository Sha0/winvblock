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
#include "bus.h"
#include "bus_pnp.h"
#include "bus_dev_ctl.h"
#include "debug.h"
#include "probe.h"

/* Temporarily here so it can be in the mini IRP handling table */
extern irp__handler_decl (
  aoe__bus_dev_ctl_dispatch
 );

PDEVICE_OBJECT bus__fdo = NULL;

void
Bus_Stop (
  void
 )
{
  UNICODE_STRING DosDeviceName;

  DBG ( "Entry\n" );
  RtlInitUnicodeString ( &DosDeviceName,
			 L"\\DosDevices\\" winvblock__literal_w );
  IoDeleteSymbolicLink ( &DosDeviceName );
  bus__fdo = NULL;
  DBG ( "Exit\n" );
}

/**
 * Add a child node to the bus
 *
 * @v bus_dev_obj_ptr The bus to add the node to
 * @v dev_ext_ptr     The details for the child device to add
 *
 * Returns TRUE for success, FALSE for failure
 */
winvblock__bool STDCALL
bus__add_child (
  IN PDEVICE_OBJECT bus_dev_obj_ptr,
  IN driver__dev_ext_ptr dev_ext_ptr
 )
{
  /**
   * @v dev_obj_ptr         The new node's device object
   * @v bus_ptr             A pointer to the bus device's details
   * @v walker              Walks the child nodes
   */
  PDEVICE_OBJECT dev_obj_ptr;
  bus__type_ptr bus_ptr;
  driver__dev_ext_ptr walker;

  DBG ( "Entry\n" );
  /*
   * Establish a pointer into the bus device's extension space
   */
  bus_ptr = get_bus_ptr ( bus_dev_obj_ptr->DeviceExtension );
  /*
   * Create the child device
   */
  dev_obj_ptr = dev_ext_ptr->ops->create_pdo ( dev_ext_ptr );
  if ( !dev_obj_ptr )
    return FALSE;

  /*
   * Re-purpose dev_ext_ptr to point into the PDO's device
   * extension space.  We don't need the original details anymore
   */
  dev_ext_ptr = dev_obj_ptr->DeviceExtension;
  dev_ext_ptr->Parent = bus_dev_obj_ptr;
  dev_ext_ptr->next_sibling_ptr = NULL;
  /*
   * Initialize the device.  For disks, this routine is responsible for
   * determining the disk's geometry appropriately for AoE/RAM/file disks
   */
  dev_ext_ptr->ops->init ( dev_ext_ptr );
  dev_obj_ptr->Flags &= ~DO_DEVICE_INITIALIZING;
  /*
   * Add the new device's extension to the bus' list of children
   */
  if ( bus_ptr->first_child_ptr == NULL )
    {
      bus_ptr->first_child_ptr = ( winvblock__uint8_ptr ) dev_ext_ptr;
    }
  else
    {
      walker = ( driver__dev_ext_ptr ) bus_ptr->first_child_ptr;
      while ( walker->next_sibling_ptr != NULL )
	walker = walker->next_sibling_ptr;
      walker->next_sibling_ptr = dev_ext_ptr;
    }
  bus_ptr->Children++;
  DBG ( "Exit\n" );
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
  {IRP_MJ_DEVICE_CONTROL, 0, FALSE, TRUE, aoe__bus_dev_ctl_dispatch}
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
  driver__dev_ext_ptr bus_dev_ext_ptr;
  bus__type_ptr bus_ptr;

  DBG ( "Entry\n" );
  if ( bus__fdo )
    return STATUS_SUCCESS;
  RtlInitUnicodeString ( &DeviceName, L"\\Device\\" winvblock__literal_w );
  RtlInitUnicodeString ( &DosDeviceName,
			 L"\\DosDevices\\" winvblock__literal_w );
  Status =
    IoCreateDevice ( DriverObject, sizeof ( bus__type ), &DeviceName,
		     FILE_DEVICE_CONTROLLER, FILE_DEVICE_SECURE_OPEN, FALSE,
		     &bus__fdo );
  if ( !NT_SUCCESS ( Status ) )
    {
      return Error ( "Bus_AddDevice IoCreateDevice", Status );
    }
  Status = IoCreateSymbolicLink ( &DosDeviceName, &DeviceName );
  if ( !NT_SUCCESS ( Status ) )
    {
      IoDeleteDevice ( bus__fdo );
      return Error ( "Bus_AddDevice IoCreateSymbolicLink", Status );
    }

  /*
   * Set some default parameters for a bus
   */
  bus_dev_ext_ptr = ( driver__dev_ext_ptr ) bus__fdo->DeviceExtension;
  RtlZeroMemory ( bus_dev_ext_ptr, sizeof ( bus__type ) );
  bus_dev_ext_ptr->IsBus = TRUE;
  bus_dev_ext_ptr->size = sizeof ( bus__type );
  bus_dev_ext_ptr->DriverObject = DriverObject;
  bus_dev_ext_ptr->Self = bus__fdo;
  bus_dev_ext_ptr->State = NotStarted;
  bus_dev_ext_ptr->OldState = NotStarted;
  /*
   * Register the default driver IRP handling table
   */
  irp__reg_table_s ( &bus_dev_ext_ptr->irp_handler_chain,
		     driver__handling_table, driver__handling_table_size );
  /*
   * Register the default bus IRP handling table
   */
  irp__reg_table ( &bus_dev_ext_ptr->irp_handler_chain, handling_table );
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
      bus_ptr->LowerDeviceObject =
	IoAttachDeviceToDeviceStack ( bus__fdo, PhysicalDeviceObject );
      if ( bus_ptr->LowerDeviceObject == NULL )
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
  DBG ( "Exit\n" );
  return STATUS_SUCCESS;
}
