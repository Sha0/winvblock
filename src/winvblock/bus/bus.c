/**
 * Copyright (C) 2009-2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#include "device.h"
#include "bus.h"
#include "bus_pnp.h"
#include "bus_dev_ctl.h"
#include "debug.h"

static PDEVICE_OBJECT bus_fdo = NULL;
static LIST_ENTRY bus_list;
static KSPIN_LOCK bus_list_lock;

/**
 * Tear down the global, bus-common environment
 */
void
bus__finalize (
  void
 )
{
  UNICODE_STRING DosDeviceName;

  DBG ( "Entry\n" );
  RtlInitUnicodeString ( &DosDeviceName,
			 L"\\DosDevices\\" winvblock__literal_w );
  IoDeleteSymbolicLink ( &DosDeviceName );
  bus_fdo = NULL;
  DBG ( "Exit\n" );
}

/**
 * Add a child node to the bus
 *
 * @v dev_ptr           The details for the child device to add
 *
 * Returns TRUE for success, FALSE for failure
 */
winvblock__lib_func winvblock__bool STDCALL
bus__add_child (
  IN device__type_ptr dev_ptr
 )
{
  /**
   * @v dev_obj_ptr         The new node's device object
   * @v bus_ptr             A pointer to the bus device's details
   * @v walker              Walks the child nodes
   */
  PDEVICE_OBJECT dev_obj_ptr;
  bus__type_ptr bus_ptr;
  device__type_ptr walker;

  DBG ( "Entry\n" );
  if ( !bus_fdo )
    {
      DBG ( "No bus device!\n" );
      return FALSE;
    }
  /*
   * Establish a pointer to the bus
   */
  bus_ptr = get_bus_ptr ( bus_fdo->DeviceExtension );
  /*
   * Create the child device
   */
  dev_obj_ptr = dev_ptr->ops.create_pdo ( dev_ptr );
  if ( !dev_obj_ptr )
    {
      DBG ( "bus__add_child() failed!\n" );
      return FALSE;
    }

  /*
   * Re-purpose dev_ptr to point into the PDO's device
   * extension space.  We don't need the original details anymore
   */
  dev_ptr = dev_obj_ptr->DeviceExtension;
  dev_ptr->Parent = bus_fdo;
  dev_ptr->next_sibling_ptr = NULL;
  /*
   * Initialize the device.  For disks, this routine is responsible for
   * determining the disk's geometry appropriately for AoE/RAM/file disks
   */
  dev_ptr->ops.init ( dev_ptr );
  dev_obj_ptr->Flags &= ~DO_DEVICE_INITIALIZING;
  /*
   * Add the new device's extension to the bus' list of children
   */
  if ( bus_ptr->first_child_ptr == NULL )
    {
      bus_ptr->first_child_ptr = ( winvblock__uint8_ptr ) dev_ptr;
    }
  else
    {
      walker = ( device__type_ptr ) bus_ptr->first_child_ptr;
      while ( walker->next_sibling_ptr != NULL )
	walker = walker->next_sibling_ptr;
      walker->next_sibling_ptr = dev_ptr;
    }
  bus_ptr->Children++;
  if ( bus_ptr->PhysicalDeviceObject != NULL )
    {
      IoInvalidateDeviceRelations ( bus_ptr->PhysicalDeviceObject,
				    BusRelations );
    }
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

static NTSTATUS STDCALL
attach_fdo (
  IN PDRIVER_OBJECT DriverObject,
  IN PDEVICE_OBJECT PhysicalDeviceObject
 )
{
  NTSTATUS Status;
  UNICODE_STRING DeviceName,
   DosDeviceName;
  device__type_ptr bus_dev_ptr;
  bus__type_ptr bus_ptr;

  DBG ( "Entry\n" );
  if ( bus_fdo )
    return STATUS_SUCCESS;
  RtlInitUnicodeString ( &DeviceName, L"\\Device\\" winvblock__literal_w );
  RtlInitUnicodeString ( &DosDeviceName,
			 L"\\DosDevices\\" winvblock__literal_w );
  Status =
    IoCreateDevice ( DriverObject, sizeof ( bus__type ), &DeviceName,
		     FILE_DEVICE_CONTROLLER, FILE_DEVICE_SECURE_OPEN, FALSE,
		     &bus_fdo );
  if ( !NT_SUCCESS ( Status ) )
    {
      return Error ( "Bus_AddDevice IoCreateDevice", Status );
    }
  Status = IoCreateSymbolicLink ( &DosDeviceName, &DeviceName );
  if ( !NT_SUCCESS ( Status ) )
    {
      IoDeleteDevice ( bus_fdo );
      return Error ( "Bus_AddDevice IoCreateSymbolicLink", Status );
    }

  /*
   * Set some default parameters for a bus
   */
  bus_dev_ptr = ( device__type_ptr ) bus_fdo->DeviceExtension;
  RtlZeroMemory ( bus_dev_ptr, sizeof ( bus__type ) );
  bus_dev_ptr->IsBus = TRUE;
  bus_dev_ptr->size = sizeof ( bus__type );
  bus_dev_ptr->DriverObject = DriverObject;
  bus_dev_ptr->Self = bus_fdo;
  bus_dev_ptr->State = NotStarted;
  bus_dev_ptr->OldState = NotStarted;
  /*
   * Register the default driver IRP handling table
   */
  irp__reg_table_s ( &bus_dev_ptr->irp_handler_chain, driver__handling_table,
		     driver__handling_table_size );
  /*
   * Register the default bus IRP handling table
   */
  irp__reg_table ( &bus_dev_ptr->irp_handler_chain, handling_table );
  /*
   * Establish a pointer to the bus
   */
  bus_ptr = get_bus_ptr ( bus_dev_ptr );
  bus_ptr->PhysicalDeviceObject = PhysicalDeviceObject;
  bus_ptr->Children = 0;
  bus_ptr->first_child_ptr = NULL;
  KeInitializeSpinLock ( &bus_ptr->SpinLock );
  bus_fdo->Flags |= DO_DIRECT_IO;	/* FIXME? */
  bus_fdo->Flags |= DO_POWER_INRUSH;	/* FIXME? */
  /*
   * Add the bus to the device tree
   */
  if ( PhysicalDeviceObject != NULL )
    {
      bus_ptr->LowerDeviceObject =
	IoAttachDeviceToDeviceStack ( bus_fdo, PhysicalDeviceObject );
      if ( bus_ptr->LowerDeviceObject == NULL )
	{
	  IoDeleteDevice ( bus_fdo );
	  bus_fdo = NULL;
	  return Error ( "AddDevice IoAttachDeviceToDeviceStack",
			 STATUS_NO_SUCH_DEVICE );
	}
    }
  bus_fdo->Flags &= ~DO_DEVICE_INITIALIZING;
#ifdef RIS
  bus_dev_ptr->State = Started;
#endif
  DBG ( "Exit\n" );
  return STATUS_SUCCESS;
}

/**
 * Create a new bus
 *
 * @ret bus_ptr         The address of a new bus, or NULL for failure
 *
 * See the header file for additional details
 */
winvblock__lib_func STDCALL bus__type_ptr
bus__create (
  void
 )
{
  bus__type_ptr bus_ptr;

  /*
   * Bus devices might be used for booting and should
   * not be allocated from a paged memory pool
   */
  bus_ptr = ExAllocatePool ( NonPagedPool, sizeof ( device__type ) );
  if ( bus_ptr == NULL )
    goto err_nomem;
  RtlZeroMemory ( bus_ptr, sizeof ( bus__type ) );
  /*
   * Track the new device in our global list
   */
  ExInterlockedInsertTailList ( &bus_list, &bus_ptr->tracking,
				&bus_list_lock );
  /*
   * TODO: Populate non-zero device defaults
   */
  /*
   * TODO: Register the default driver IRP handling table
   */

err_nomem:

  return bus_ptr;
}

/**
 * Create a bus PDO
 *
 * @v dev_ptr           Populate PDO dev. ext. space from these details
 * @ret pdo_ptr         Points to the new PDO, or is NULL upon failure
 *
 * Returns a Physical Device Object pointer on success, NULL for failure.
 */
static
device__create_pdo_decl (
  create_pdo
 )
{
  PDEVICE_OBJECT pdo_ptr = NULL;
  NTSTATUS status;

  /*
   * Always create the root-enumerated bus device 
   */
  IoReportDetectedDevice ( driver__obj_ptr, InterfaceTypeUndefined, -1, -1,
			   NULL, NULL, FALSE, &pdo_ptr );
  if ( pdo_ptr == NULL )
    {
      DBG ( "IoReportDetectedDevice() went wrong!\n" );
      return NULL;
    }
  /*
   * Attach FDO to PDO *sigh*
   */
  status = attach_fdo ( driver__obj_ptr, pdo_ptr );
  if ( !NT_SUCCESS ( status ) )
    {
      DBG ( "Bus_AddDevice() went wrong!\n" );
      goto err_add_dev;
    }
  return pdo_ptr;

err_add_dev:

  IoDeleteDevice ( pdo_ptr );
  return NULL;
}

/**
 * Initialize the global, bus-common environment
 *
 * @ret ntstatus        STATUS_SUCCESS or the NTSTATUS for a failure
 */
extern STDCALL NTSTATUS
bus__init (
  void
 )
{
  /*
   * Initialize the global list of devices
   */
  InitializeListHead ( &bus_list );
  KeInitializeSpinLock ( &bus_list_lock );
  /*
   * We handle AddDevice call-backs for the driver
   */
  driver__obj_ptr->DriverExtension->AddDevice = attach_fdo;
  /*
   * Establish the boot bus (required for booting from a WinVBlock disk)
   */
  if ( create_pdo ( NULL ) == NULL )	/* TODO: Pass a bus */
    return STATUS_UNSUCCESSFUL;

  return STATUS_SUCCESS;
}

/**
 * Get a pointer to the bus device's extension space
 *
 * @ret         A pointer to the bus device's extension space, or NULL
 */
winvblock__lib_func bus__type_ptr STDCALL
bus__dev (
  void
 )
{
  if ( !bus_fdo )
    {
      DBG ( "No bus device!\n" );
      return NULL;
    }
  return get_bus_ptr ( bus_fdo->DeviceExtension );
}
