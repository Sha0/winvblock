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
  driver__dev_ext dev_ext;
  PDEVICE_OBJECT LowerDeviceObject;
  PDEVICE_OBJECT PhysicalDeviceObject;
  winvblock__uint32 Children;
  winvblock__uint8_ptr first_child_ptr;
  KSPIN_LOCK SpinLock;
};

extern void Bus_Stop (
  void
 );

extern NTSTATUS STDCALL Bus_AddDevice (
  IN PDRIVER_OBJECT DriverObject,
  IN PDEVICE_OBJECT PhysicalDeviceObject
 );

extern NTSTATUS STDCALL Bus_GetDeviceCapabilities (
  IN PDEVICE_OBJECT DeviceObject,
  IN PDEVICE_CAPABILITIES DeviceCapabilities
 );

/**
 * Add a child node to the bus
 *
 * @v dev_ext_ptr     The details for the child device to add
 *
 * Returns TRUE for success, FALSE for failure
 */
extern winvblock__lib_func winvblock__bool STDCALL bus__add_child (
  IN driver__dev_ext_ptr dev_ext_ptr
 );

/*
 * Establish a pointer into the bus device's extension space
 */
__inline bus__type_ptr STDCALL
get_bus_ptr (
  driver__dev_ext_ptr dev_ext_ptr
 )
{
  /*
   * Since the device extension is the first member of a bus
   * structure, a simple cast will suffice
   */
  return ( bus__type_ptr ) dev_ext_ptr;
}

#endif				/* _BUS_H */
