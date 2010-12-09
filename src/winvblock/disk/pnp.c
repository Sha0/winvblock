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
 * Disk PnP IRP handling.
 */

#include <stdio.h>
#include <ntddk.h>
#include <initguid.h>

#include "winvblock.h"
#include "wv_stdlib.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "device.h"
#include "disk.h"
#include "bus.h"
#include "debug.h"

NTSTATUS STDCALL disk_pnp__query_id(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PIO_STACK_LOCATION Stack,
    IN struct _device__type * dev_ptr,
    OUT winvblock__bool_ptr completion_ptr
  )
{
  NTSTATUS status;
  PWCHAR string;
  winvblock__uint32 string_length;
  disk__type_ptr disk_ptr;

  disk_ptr = disk__get_ptr ( dev_ptr );
  string = wv_mallocz(512 * sizeof *string);
  if ( string == NULL )
    {
      DBG("wv_malloc IRP_MN_QUERY_ID\n");
      status = STATUS_INSUFFICIENT_RESOURCES;
      goto alloc_string;
    }
  /*
   * Invoke the specific disk class ID query 
   */
  string_length =
    disk__query_id ( disk_ptr, Stack->Parameters.QueryId.IdType, string );

  if ( string_length == 0 )
    {
      Irp->IoStatus.Information = 0;
      status = STATUS_NOT_SUPPORTED;
      goto alloc_info;
    }

  Irp->IoStatus.Information = (ULONG_PTR) wv_palloc(
      string_length * sizeof *string
    );
  if ( Irp->IoStatus.Information == 0 )
    {
      DBG("wv_palloc failed.\n");
      status = STATUS_INSUFFICIENT_RESOURCES;
      goto alloc_info;
    }
  RtlCopyMemory ( ( PWCHAR ) Irp->IoStatus.Information, string,
		  string_length * sizeof ( WCHAR ) );
  status = STATUS_SUCCESS;
  /*
   * Irp->IoStatus.Information not freed 
   */
alloc_info:

  wv_free(string);
alloc_string:

  Irp->IoStatus.Status = status;
  IoCompleteRequest ( Irp, IO_NO_INCREMENT );
  *completion_ptr = TRUE;
  return status;
}

NTSTATUS STDCALL disk_pnp__query_dev_text(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PIO_STACK_LOCATION Stack,
    IN struct _device__type * dev_ptr,
    OUT winvblock__bool_ptr completion_ptr
  )
{
  PWCHAR string;
  NTSTATUS status;
  winvblock__uint32 string_length;
  disk__type_ptr disk_ptr;

  string = wv_mallocz(512 * sizeof *string);
  disk_ptr = disk__get_ptr ( dev_ptr );

  if ( string == NULL )
    {
      DBG("wv_malloc IRP_MN_QUERY_DEVICE_TEXT\n");
      status = STATUS_INSUFFICIENT_RESOURCES;
      goto alloc_string;
    }

  switch ( Stack->Parameters.QueryDeviceText.DeviceTextType )
    {
      case DeviceTextDescription:
	string_length = swprintf ( string, winvblock__literal_w L" Disk" ) + 1;
  Irp->IoStatus.Information = (ULONG_PTR) wv_palloc(
      string_length * sizeof *string
    );
	if ( Irp->IoStatus.Information == 0 )
	  {
      DBG("wv_palloc DeviceTextDescription\n");
	    status = STATUS_INSUFFICIENT_RESOURCES;
	    goto alloc_info;
	  }
	RtlCopyMemory ( ( PWCHAR ) Irp->IoStatus.Information, string,
			string_length * sizeof ( WCHAR ) );
	status = STATUS_SUCCESS;
	goto alloc_info;

      case DeviceTextLocationInformation:
	string_length =
	  disk__query_id ( disk_ptr, BusQueryInstanceID, string );
  Irp->IoStatus.Information = (ULONG_PTR) wv_palloc(
      string_length * sizeof *string
    );
	if ( Irp->IoStatus.Information == 0 )
	  {
      DBG("wv_palloc DeviceTextLocationInformation\n");
	    status = STATUS_INSUFFICIENT_RESOURCES;
	    goto alloc_info;
	  }
	RtlCopyMemory ( ( PWCHAR ) Irp->IoStatus.Information, string,
			string_length * sizeof ( WCHAR ) );
	status = STATUS_SUCCESS;
	goto alloc_info;

      default:
	Irp->IoStatus.Information = 0;
	status = STATUS_NOT_SUPPORTED;
    }
  /*
   * Irp->IoStatus.Information not freed 
   */
alloc_info:

  wv_free(string);
alloc_string:

  Irp->IoStatus.Status = status;
  IoCompleteRequest ( Irp, IO_NO_INCREMENT );
  *completion_ptr = TRUE;
  return status;
}

NTSTATUS STDCALL disk_pnp__query_dev_relations(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PIO_STACK_LOCATION Stack,
    IN struct _device__type * dev_ptr,
    OUT winvblock__bool_ptr completion_ptr
  )
{
  NTSTATUS status;
  PDEVICE_RELATIONS dev_relations;

  if ( Stack->Parameters.QueryDeviceRelations.Type != TargetDeviceRelation )
    {
      status = Irp->IoStatus.Status;
      goto ret_path;
    }
  dev_relations = wv_palloc(sizeof *dev_relations + sizeof dev_ptr->Self);
  if ( dev_relations == NULL )
    {
      DBG("wv_palloc IRP_MN_QUERY_DEVICE_RELATIONS\n");
      status = STATUS_INSUFFICIENT_RESOURCES;
      goto alloc_dev_relations;
    }
  dev_relations->Objects[0] = dev_ptr->Self;
  dev_relations->Count = 1;
  ObReferenceObject ( dev_ptr->Self );
  Irp->IoStatus.Information = ( ULONG_PTR ) dev_relations;
  status = STATUS_SUCCESS;

  /*
   * Irp->IoStatus.Information (dev_relations) not freed 
   */
alloc_dev_relations:

ret_path:
  Irp->IoStatus.Status = status;
  IoCompleteRequest ( Irp, IO_NO_INCREMENT );
  *completion_ptr = TRUE;
  return status;
}

DEFINE_GUID ( GUID_BUS_TYPE_INTERNAL, 0x2530ea73L, 0x086b, 0x11d1, 0xa0, 0x9f,
	      0x00, 0xc0, 0x4f, 0xc3, 0x40, 0xb1 );

NTSTATUS STDCALL disk_pnp__query_bus_info(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PIO_STACK_LOCATION Stack,
    IN struct _device__type * dev_ptr,
    OUT winvblock__bool_ptr completion_ptr
  )
{
  PPNP_BUS_INFORMATION pnp_bus_info;
  NTSTATUS status;

  pnp_bus_info = wv_palloc(sizeof *pnp_bus_info);
  if ( pnp_bus_info == NULL )
    {
      DBG("wv_palloc IRP_MN_QUERY_BUS_INFORMATION\n");
      status = STATUS_INSUFFICIENT_RESOURCES;
      goto alloc_pnp_bus_info;
    }
  pnp_bus_info->BusTypeGuid = GUID_BUS_TYPE_INTERNAL;
  pnp_bus_info->LegacyBusType = PNPBus;
  pnp_bus_info->BusNumber = 0;
  Irp->IoStatus.Information = ( ULONG_PTR ) pnp_bus_info;
  status = STATUS_SUCCESS;

  /*
   * Irp-IoStatus.Information (pnp_bus_info) not freed 
   */
alloc_pnp_bus_info:

  Irp->IoStatus.Status = status;
  IoCompleteRequest ( Irp, IO_NO_INCREMENT );
  *completion_ptr = TRUE;
  return status;
}

NTSTATUS STDCALL disk_pnp__query_capabilities(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PIO_STACK_LOCATION Stack,
    IN struct _device__type * dev_ptr,
    OUT winvblock__bool_ptr completion_ptr
  )
{
  PDEVICE_CAPABILITIES DeviceCapabilities =
    Stack->Parameters.DeviceCapabilities.Capabilities;
  NTSTATUS status;
  disk__type_ptr disk_ptr;
  struct bus__type * bus_ptr;
  DEVICE_CAPABILITIES ParentDeviceCapabilities;
  PDEVICE_OBJECT bus_lower;

  if ( DeviceCapabilities->Version != 1
       || DeviceCapabilities->Size < sizeof ( DEVICE_CAPABILITIES ) )
    {
      status = STATUS_UNSUCCESSFUL;
      goto ret_path;
    }
  disk_ptr = disk__get_ptr ( dev_ptr );
  bus_ptr = bus__get(device__get(dev_ptr->Parent));
  bus_lower = bus_ptr->LowerDeviceObject;
  if (bus_lower) {
      status = bus__get_dev_capabilities(
          bus_lower,
		    	&ParentDeviceCapabilities
        );
    } else {
      status = bus__get_dev_capabilities(
          bus_ptr->device->Self,
		    	&ParentDeviceCapabilities
        );
    }      
  if ( !NT_SUCCESS ( status ) )
    goto ret_path;

  RtlCopyMemory ( DeviceCapabilities->DeviceState,
		  ParentDeviceCapabilities.DeviceState,
		  ( PowerSystemShutdown +
		    1 ) * sizeof ( DEVICE_POWER_STATE ) );
  DeviceCapabilities->DeviceState[PowerSystemWorking] = PowerDeviceD0;
  if ( DeviceCapabilities->DeviceState[PowerSystemSleeping1] != PowerDeviceD0 )
    DeviceCapabilities->DeviceState[PowerSystemSleeping1] = PowerDeviceD1;
  if ( DeviceCapabilities->DeviceState[PowerSystemSleeping2] != PowerDeviceD0 )
    DeviceCapabilities->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
#if 0
  if ( DeviceCapabilities->DeviceState[PowerSystemSleeping3] != PowerDeviceD0 )
    DeviceCapabilities->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
#endif
  DeviceCapabilities->DeviceWake = PowerDeviceD1;
  DeviceCapabilities->DeviceD1 = TRUE;
  DeviceCapabilities->DeviceD2 = FALSE;
  DeviceCapabilities->WakeFromD0 = FALSE;
  DeviceCapabilities->WakeFromD1 = FALSE;
  DeviceCapabilities->WakeFromD2 = FALSE;
  DeviceCapabilities->WakeFromD3 = FALSE;
  DeviceCapabilities->D1Latency = 0;
  DeviceCapabilities->D2Latency = 0;
  DeviceCapabilities->D3Latency = 0;
  DeviceCapabilities->EjectSupported = FALSE;
  DeviceCapabilities->HardwareDisabled = FALSE;
  DeviceCapabilities->Removable = disk__removable[disk_ptr->media];
  DeviceCapabilities->SurpriseRemovalOK = FALSE;
  DeviceCapabilities->UniqueID = FALSE;
  DeviceCapabilities->SilentInstall = FALSE;
#if 0
  DeviceCapabilities->Address = DeviceObject->SerialNo;
  DeviceCapabilities->UINumber = DeviceObject->SerialNo;
#endif
  status = STATUS_SUCCESS;

ret_path:
  Irp->IoStatus.Status = status;
  IoCompleteRequest ( Irp, IO_NO_INCREMENT );
  *completion_ptr = TRUE;
  return status;
}

NTSTATUS STDCALL disk_pnp__simple(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PIO_STACK_LOCATION Stack,
    IN struct _device__type * dev_ptr,
    OUT winvblock__bool_ptr completion_ptr
  )
{
  NTSTATUS status;
  disk__type_ptr disk_ptr;

  disk_ptr = disk__get_ptr ( dev_ptr );
  switch ( Stack->MinorFunction )
    {
      case IRP_MN_DEVICE_USAGE_NOTIFICATION:
	if ( Stack->Parameters.UsageNotification.InPath )
	  {
	    disk_ptr->SpecialFileCount++;
	  }
	else
	  {
	    disk_ptr->SpecialFileCount--;
	  }
	Irp->IoStatus.Information = 0;
	status = STATUS_SUCCESS;
	break;
      case IRP_MN_QUERY_PNP_DEVICE_STATE:
	Irp->IoStatus.Information = 0;
	status = STATUS_SUCCESS;
	break;
      case IRP_MN_START_DEVICE:
	dev_ptr->OldState = dev_ptr->State;
	dev_ptr->State = Started;
	status = STATUS_SUCCESS;
	break;
      case IRP_MN_QUERY_STOP_DEVICE:
	dev_ptr->OldState = dev_ptr->State;
	dev_ptr->State = StopPending;
	status = STATUS_SUCCESS;
	break;
      case IRP_MN_CANCEL_STOP_DEVICE:
	dev_ptr->State = dev_ptr->OldState;
	status = STATUS_SUCCESS;
	break;
      case IRP_MN_STOP_DEVICE:
	dev_ptr->OldState = dev_ptr->State;
	dev_ptr->State = Stopped;
	status = STATUS_SUCCESS;
	break;
      case IRP_MN_QUERY_REMOVE_DEVICE:
	dev_ptr->OldState = dev_ptr->State;
	dev_ptr->State = RemovePending;
	status = STATUS_SUCCESS;
	break;
      case IRP_MN_REMOVE_DEVICE:
	dev_ptr->OldState = dev_ptr->State;
	dev_ptr->State = NotStarted;
	if ( disk_ptr->Unmount )
	  {
	    device__close ( dev_ptr );
	    IoDeleteDevice ( dev_ptr->Self );
	    device__free ( dev_ptr );
	    status = STATUS_NO_SUCH_DEVICE;
	  }
	else
	  {
	    status = STATUS_SUCCESS;
	  }
	break;
      case IRP_MN_CANCEL_REMOVE_DEVICE:
	dev_ptr->State = dev_ptr->OldState;
	status = STATUS_SUCCESS;
	break;
      case IRP_MN_SURPRISE_REMOVAL:
	dev_ptr->OldState = dev_ptr->State;
	dev_ptr->State = SurpriseRemovePending;
	status = STATUS_SUCCESS;
	break;
      default:
	status = Irp->IoStatus.Status;
    }

  Irp->IoStatus.Status = status;
  IoCompleteRequest ( Irp, IO_NO_INCREMENT );
  *completion_ptr = TRUE;
  return status;
}
