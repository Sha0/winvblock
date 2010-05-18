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
 * Bus PnP IRP handling
 *
 */

#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "disk.h"
#include "bus.h"
#include "debug.h"
#include "probe.h"

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

irp__handler_decl ( bus_pnp__start_dev )
{
  NTSTATUS status;
  KEVENT event;
  bus__type_ptr bus_ptr;

  bus_ptr = get_bus_ptr ( DeviceExtension );
  KeInitializeEvent ( &event, NotificationEvent, FALSE );
  IoCopyCurrentIrpStackLocationToNext ( Irp );
  IoSetCompletionRoutine ( Irp,
			   ( PIO_COMPLETION_ROUTINE ) Bus_IoCompletionRoutine,
			   ( void * )&event, TRUE, TRUE, TRUE );
  status = IoCallDriver ( bus_ptr->LowerDeviceObject, Irp );
  if ( status == STATUS_PENDING )
    {
      DBG ( "Locked\n" );
      KeWaitForSingleObject ( &event, Executive, KernelMode, FALSE, NULL );
    }
  if ( NT_SUCCESS ( status = Irp->IoStatus.Status ) )
    {
      DeviceExtension->OldState = DeviceExtension->State;
      DeviceExtension->State = Started;
    }
  status = STATUS_SUCCESS;
  Irp->IoStatus.Status = status;
  IoCompleteRequest ( Irp, IO_NO_INCREMENT );
  *completion_ptr = TRUE;
  return status;
}

irp__handler_decl ( bus_pnp__remove_dev )
{
  NTSTATUS status;
  bus__type_ptr bus_ptr;
  device__type_ptr walker,
   next;

  DeviceExtension->OldState = DeviceExtension->State;
  DeviceExtension->State = Deleted;
  Irp->IoStatus.Information = 0;
  Irp->IoStatus.Status = STATUS_SUCCESS;
  IoSkipCurrentIrpStackLocation ( Irp );
  bus_ptr = get_bus_ptr ( DeviceExtension );
  status = IoCallDriver ( bus_ptr->LowerDeviceObject, Irp );
  walker = ( device__type_ptr ) bus_ptr->first_child_ptr;
  while ( walker != NULL )
    {
      next = ( device__type_ptr ) walker->next_sibling_ptr;
      walker->ops->close ( walker );
      IoDeleteDevice ( walker->Self );
      walker = next;
    }
  bus_ptr->Children = 0;
  bus_ptr->first_child_ptr = NULL;
  IoDetachDevice ( bus_ptr->LowerDeviceObject );
  IoDeleteDevice ( DeviceExtension->Self );
  *completion_ptr = TRUE;
  return status;
}

irp__handler_decl ( bus_pnp__query_dev_relations )
{
  NTSTATUS status;
  bus__type_ptr bus_ptr;
  winvblock__uint32 count;
  disk__type_ptr walker;
  PDEVICE_RELATIONS dev_relations;

  bus_ptr = get_bus_ptr ( DeviceExtension );
  if ( Stack->Parameters.QueryDeviceRelations.Type != BusRelations
       || Irp->IoStatus.Information )
    {
      status = Irp->IoStatus.Status;
      IoSkipCurrentIrpStackLocation ( Irp );
      status = IoCallDriver ( bus_ptr->LowerDeviceObject, Irp );
      *completion_ptr = TRUE;
      return status;
    }
  probe__disks (  );
  count = 0;
  walker = ( disk__type_ptr ) bus_ptr->first_child_ptr;
  while ( walker != NULL )
    {
      count++;
      walker = ( disk__type_ptr ) walker->device.next_sibling_ptr;
    }
  dev_relations =
    ( PDEVICE_RELATIONS ) ExAllocatePool ( NonPagedPool,
					   sizeof ( DEVICE_RELATIONS ) +
					   ( sizeof ( PDEVICE_OBJECT ) *
					     count ) );
  if ( dev_relations == NULL )
    {
      Irp->IoStatus.Information = 0;
      status = STATUS_INSUFFICIENT_RESOURCES;
      Irp->IoStatus.Status = status;
      IoSkipCurrentIrpStackLocation ( Irp );
      status = IoCallDriver ( bus_ptr->LowerDeviceObject, Irp );
      *completion_ptr = TRUE;
      return status;
    }
  dev_relations->Count = count;

  count = 0;
  walker = ( disk__type_ptr ) bus_ptr->first_child_ptr;
  while ( walker != NULL )
    {
      ObReferenceObject ( walker->device.Self );
      dev_relations->Objects[count] = walker->device.Self;
      count++;
      walker = ( disk__type_ptr ) walker->device.next_sibling_ptr;
    }
  Irp->IoStatus.Information = ( ULONG_PTR ) dev_relations;
  status = STATUS_SUCCESS;
  Irp->IoStatus.Status = status;
  IoSkipCurrentIrpStackLocation ( Irp );
  status = IoCallDriver ( bus_ptr->LowerDeviceObject, Irp );
  *completion_ptr = TRUE;
  return status;
}

irp__handler_decl ( bus_pnp__simple )
{
  NTSTATUS status;
  bus__type_ptr bus_ptr;

  bus_ptr = get_bus_ptr ( DeviceExtension );
  switch ( Stack->MinorFunction )
    {
      case IRP_MN_QUERY_PNP_DEVICE_STATE:
	Irp->IoStatus.Information = 0;
	status = STATUS_SUCCESS;
	break;
      case IRP_MN_QUERY_STOP_DEVICE:
	DeviceExtension->OldState = DeviceExtension->State;
	DeviceExtension->State = StopPending;
	status = STATUS_SUCCESS;
	break;
      case IRP_MN_CANCEL_STOP_DEVICE:
	DeviceExtension->State = DeviceExtension->OldState;
	status = STATUS_SUCCESS;
	break;
      case IRP_MN_STOP_DEVICE:
	DeviceExtension->OldState = DeviceExtension->State;
	DeviceExtension->State = Stopped;
	status = STATUS_SUCCESS;
	break;
      case IRP_MN_QUERY_REMOVE_DEVICE:
	DeviceExtension->OldState = DeviceExtension->State;
	DeviceExtension->State = RemovePending;
	status = STATUS_SUCCESS;
	break;
      case IRP_MN_CANCEL_REMOVE_DEVICE:
	DeviceExtension->State = DeviceExtension->OldState;
	status = STATUS_SUCCESS;
	break;
      case IRP_MN_SURPRISE_REMOVAL:
	DeviceExtension->OldState = DeviceExtension->State;
	DeviceExtension->State = SurpriseRemovePending;
	status = STATUS_SUCCESS;
	break;
      default:
	status = Irp->IoStatus.Status;
    }

  Irp->IoStatus.Status = status;
  IoSkipCurrentIrpStackLocation ( Irp );
  status = IoCallDriver ( bus_ptr->LowerDeviceObject, Irp );
  *completion_ptr = TRUE;
  return status;
}
