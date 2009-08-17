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
#ifndef _BUS_H
#  define _BUS_H

/**
 * @file
 *
 * Bus specifics
 *
 */

extern NTSTATUS STDCALL BusStart (
	void
 );

extern VOID STDCALL BusStop (
	void
 );

extern NTSTATUS STDCALL BusAddDevice (
	IN PDRIVER_OBJECT DriverObject,
	IN PDEVICE_OBJECT PhysicalDeviceObject
 );

extern NTSTATUS STDCALL BusDispatchPnP (
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp,
	IN PIO_STACK_LOCATION Stack,
	IN PDEVICEEXTENSION DeviceExtension
 );

extern NTSTATUS STDCALL BusDispatchDeviceControl (
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp,
	IN PIO_STACK_LOCATION Stack,
	IN PDEVICEEXTENSION DeviceExtension
 );

extern NTSTATUS STDCALL BusDispatchSystemControl (
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp,
	IN PIO_STACK_LOCATION Stack,
	IN PDEVICEEXTENSION DeviceExtension
 );

extern VOID STDCALL BusAddTarget (
	IN PUCHAR ClientMac,
	IN PUCHAR ServerMac,
	USHORT Major,
	UCHAR Minor,
	LONGLONG LBASize
 );

extern VOID STDCALL BusCleanupTargetList (
	void
 );

#endif													/* _BUS_H */
