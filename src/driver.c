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
 * Driver specifics
 *
 */

#include "portable.h"
#include <stdio.h>
#include <ntddk.h>
#include "driver.h"
#include "debug.h"
#include "registry.h"
#include "bus.h"
#include "disk.h"
#include "aoe.h"
#include "protocol.h"
#include "debug.h"

/* in this file */
static NTSTATUS STDCALL Dispatch (
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp
 );

static VOID STDCALL Unload (
	IN PDRIVER_OBJECT DriverObject
 );

static PVOID StateHandle;

NTSTATUS STDCALL
DriverEntry (
	IN PDRIVER_OBJECT DriverObject,
	IN PUNICODE_STRING RegistryPath
 )
{
	NTSTATUS Status;
	int i;

		/**
     * TODO: Remove this fun test
     *
     * LARGE_INTEGER NapTime;
     * PWCHAR DisplayStringInternal;
     * UNICODE_STRING DisplayString;
     */

	DBG ( "Entry\n" );
	InitializeDebug (  );
	if ( !NT_SUCCESS ( Status = CheckRegistry (  ) ) )
		return Error ( "CheckRegistry", Status );
	if ( !NT_SUCCESS ( Status = BusStart (  ) ) )
		return Error ( "BusStart", Status );
	if ( !NT_SUCCESS ( Status = AoEStart (  ) ) )
		{
			BusStop (  );
			return Error ( "AoEStart", Status );
		}
	if ( !NT_SUCCESS ( Status = ProtocolStart (  ) ) )
		{
			AoEStop (  );
			BusStop (  );
			return Error ( "ProtocolStart", Status );
		}

		/**
     * TODO: Remove this fun test
     *
     * NapTime.QuadPart = -10000000;
     * DisplayStringInternal = L"\nHello!\n";
     * DisplayString.Buffer = DisplayStringInternal;
     * DisplayString.Length = wcslen ( DisplayStringInternal ) * sizeof ( WCHAR );
     * DisplayString.MaximumLength = DisplayString.Length + sizeof ( WCHAR );
     * while ( 1 ) {
     *     i = 0;
     *     while ( i < 65535 ) i++;
     *     ZwDisplayString ( &DisplayString );
     * }
     */

	StateHandle = NULL;

	if ( ( StateHandle =
				 PoRegisterSystemState ( NULL, ES_CONTINUOUS ) ) == NULL )
		{
			DBG ( "Could not set system state to ES_CONTINUOUS!!\n" );
		}

	for ( i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++ )
		DriverObject->MajorFunction[i] = Dispatch;
	DriverObject->DriverExtension->AddDevice = BusAddDevice;
	DriverObject->MajorFunction[IRP_MJ_PNP] = Dispatch;
	DriverObject->MajorFunction[IRP_MJ_POWER] = Dispatch;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = Dispatch;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = Dispatch;
	DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = Dispatch;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Dispatch;
	DriverObject->MajorFunction[IRP_MJ_SCSI] = Dispatch;
	DriverObject->DriverUnload = Unload;
#ifdef RIS
	IoReportDetectedDevice ( DriverObject, InterfaceTypeUndefined, -1, -1, NULL,
													 NULL, FALSE, &PDODeviceObject );
	if ( !NT_SUCCESS
			 ( Status = BusAddDevice ( DriverObject, PDODeviceObject ) ) )
		{
			ProtocolStop (  );
			AoEStop (  );
			Error ( "AddDevice", Status );
		}
	return Status;
#else
	return STATUS_SUCCESS;
#endif
}

static NTSTATUS STDCALL
Dispatch (
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp
 )
{

	NTSTATUS Status;
	PIO_STACK_LOCATION Stack;
	PDEVICEEXTENSION DeviceExtension;

#ifdef DEBUGIRPS
	DebugIrpStart ( DeviceObject, Irp );
#endif
	Stack = IoGetCurrentIrpStackLocation ( Irp );
	DeviceExtension = ( PDEVICEEXTENSION ) DeviceObject->DeviceExtension;
	if ( DeviceExtension->State == Deleted )
		{
			if ( Stack->MajorFunction == IRP_MJ_POWER )
				PoStartNextPowerIrp ( Irp );
			Irp->IoStatus.Information = 0;
			Irp->IoStatus.Status = STATUS_NO_SUCH_DEVICE;
			IoCompleteRequest ( Irp, IO_NO_INCREMENT );
#ifdef DEBUGIRPS
			DebugIrpEnd ( Irp, STATUS_NO_SUCH_DEVICE );
#endif
			return STATUS_NO_SUCH_DEVICE;
		}
	switch ( Stack->MajorFunction )
		{
			case IRP_MJ_POWER:
				if ( DeviceExtension->IsBus )
					{
						PoStartNextPowerIrp ( Irp );
						IoSkipCurrentIrpStackLocation ( Irp );
						Status =
							PoCallDriver ( DeviceExtension->Bus.LowerDeviceObject, Irp );
					}
				else
					{
						PoStartNextPowerIrp ( Irp );
						Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
						IoCompleteRequest ( Irp, IO_NO_INCREMENT );
						Status = STATUS_NOT_SUPPORTED;
					}
				break;
			case IRP_MJ_PNP:
				if ( DeviceExtension->IsBus )
					Status =
						BusDispatchPnP ( DeviceObject, Irp, Stack, DeviceExtension );
				else
					Status =
						DiskDispatchPnP ( DeviceObject, Irp, Stack, DeviceExtension );
				break;
			case IRP_MJ_SYSTEM_CONTROL:
				if ( DeviceExtension->IsBus )
					Status =
						BusDispatchSystemControl ( DeviceObject, Irp, Stack,
																			 DeviceExtension );
				else
					Status =
						DiskDispatchSystemControl ( DeviceObject, Irp, Stack,
																				DeviceExtension );
				break;
			case IRP_MJ_DEVICE_CONTROL:
				if ( DeviceExtension->IsBus )
					Status =
						BusDispatchDeviceControl ( DeviceObject, Irp, Stack,
																			 DeviceExtension );
				else
					Status =
						DiskDispatchDeviceControl ( DeviceObject, Irp, Stack,
																				DeviceExtension );
				break;
			case IRP_MJ_CREATE:
			case IRP_MJ_CLOSE:
				Status = STATUS_SUCCESS;
				Irp->IoStatus.Status = Status;
				IoCompleteRequest ( Irp, IO_NO_INCREMENT );
				break;
			case IRP_MJ_SCSI:
				if ( !DeviceExtension->IsBus )
					{
						Status =
							DiskDispatchSCSI ( DeviceObject, Irp, Stack, DeviceExtension );
						break;
					}
			default:
				Status = STATUS_NOT_SUPPORTED;
				Irp->IoStatus.Status = Status;
				IoCompleteRequest ( Irp, IO_NO_INCREMENT );
		}
#ifdef DEBUGIRPS
	if ( Status != STATUS_PENDING )
		DebugIrpEnd ( Irp, Status );
#endif
	return Status;
}

static VOID STDCALL
Unload (
	IN PDRIVER_OBJECT DriverObject
 )
{
	if ( StateHandle != NULL )
		PoUnregisterSystemState ( StateHandle );
	ProtocolStop (  );
	AoEStop (  );
	BusStop (  );
	DBG ( "Done\n" );
}

VOID STDCALL
CompletePendingIrp (
	IN PIRP Irp
 )
{
#ifdef DEBUGIRPS
	DebugIrpEnd ( Irp, Irp->IoStatus.Status );
#endif
	IoCompleteRequest ( Irp, IO_NO_INCREMENT );
}

NTSTATUS STDCALL
Error (
	IN PCHAR Message,
	IN NTSTATUS Status
 )
{
	DBG ( "%s: 0x%08x\n", Message, Status );
	return Status;
}
