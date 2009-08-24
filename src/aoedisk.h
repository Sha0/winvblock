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
#ifndef _AOEDISK_H
#  define _AOEDISK_H

/**
 * @file
 *
 * AoE disk specifics
 *
 */

typedef struct _AOEDISK_AOEDISK
{
	ULONG MTU;
	UCHAR ClientMac[6];
	UCHAR ServerMac[6];
	ULONG Major;
	ULONG Minor;
	ULONG MaxSectorsPerPacket;
	ULONG Timeout;
} AOEDISK_AOEDISK,
*PAOEDISK_AOEDISK;

extern NTSTATUS STDCALL Disk_DispatchPnP (
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp,
	IN PIO_STACK_LOCATION Stack,
	IN PDRIVER_DEVICEEXTENSION DeviceExtension
 );

extern NTSTATUS STDCALL Disk_DispatchSCSI (
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp,
	IN PIO_STACK_LOCATION Stack,
	IN PDRIVER_DEVICEEXTENSION DeviceExtension
 );

extern NTSTATUS STDCALL Disk_DispatchDeviceControl (
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp,
	IN PIO_STACK_LOCATION Stack,
	IN PDRIVER_DEVICEEXTENSION DeviceExtension
 );

extern NTSTATUS STDCALL Disk_DispatchSystemControl (
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp,
	IN PIO_STACK_LOCATION Stack,
	IN PDRIVER_DEVICEEXTENSION DeviceExtension
 );

#endif													/* _AOEDISK_H */
