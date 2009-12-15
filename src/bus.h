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
#ifndef _BUS_H
#  define _BUS_H

/**
 * @file
 *
 * Bus specifics
 *
 */

winvblock__def_struct ( bus__type )
{
  PDEVICE_OBJECT LowerDeviceObject;
  PDEVICE_OBJECT PhysicalDeviceObject;
  ULONG Children;
  winvblock__uint8_ptr first_child_ptr;
  KSPIN_LOCK SpinLock;
};

extern NTSTATUS Bus_Start (
  void
 );

extern VOID Bus_Stop (
  void
 );

extern NTSTATUS STDCALL Bus_AddDevice (
  IN PDRIVER_OBJECT DriverObject,
  IN PDEVICE_OBJECT PhysicalDeviceObject
 );

extern irp__handler_decl (
  Bus_DispatchPnP
 );

extern irp__handler_decl (
  Bus_DispatchDeviceControl
 );

extern irp__handler_decl (
  Bus_DispatchSystemControl
 );

extern VOID STDCALL Bus_AddTarget (
  IN winvblock__uint8_ptr ClientMac,
  IN winvblock__uint8_ptr ServerMac,
  winvblock__uint16 Major,
  winvblock__uint8 Minor,
  LONGLONG LBASize
 );

extern VOID Bus_CleanupTargetList (
  void
 );

extern NTSTATUS STDCALL Bus_GetDeviceCapabilities (
  IN PDEVICE_OBJECT DeviceObject,
  IN PDEVICE_CAPABILITIES DeviceCapabilities
 );

/* An unfortunate forward declaration.  Definition resolved in disk.h */
struct _disk__type;

extern winvblock__bool STDCALL Bus_AddChild (
  IN PDEVICE_OBJECT BusDeviceObject,
  IN struct _disk__type Disk,
  IN winvblock__bool Boot
 );

extern bus__type_ptr STDCALL get_bus_ptr (
  driver__dev_ext_ptr dev_ext_ptr
 );
extern disk__type_ptr STDCALL get_disk_ptr (
  driver__dev_ext_ptr dev_ext_ptr
 );

extern PDEVICE_OBJECT bus__fdo;

#endif				/* _BUS_H */
