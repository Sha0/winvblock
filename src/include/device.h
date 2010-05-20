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
#ifndef _device_h
#  define _device_h

/**
 * @file
 *
 * Device specifics
 *
 */

#  include "portable.h"

enum _device__state
{
  NotStarted,
  Started,
  StopPending,
  Stopped,
  RemovePending,
  SurpriseRemovePending,
  Deleted
};
winvblock__def_enum ( device__state );

/* Forward declaration */
winvblock__def_struct ( device__type );

/**
 * Device PDO creation routine
 *
 * @v dev_ptr           The device whose PDO should be created
 */
#  define device__create_pdo_decl( x ) \
\
PDEVICE_OBJECT STDCALL \
x ( \
  IN device__type_ptr dev_ptr \
 )
/*
 * Function pointer for a device initialization routine.
 * 'indent' mangles this, so it looks weird
 */
typedef device__create_pdo_decl (
   ( *device__create_pdo_routine )
 );

/**
 * Device initialization routine
 *
 * @v dev_ptr           The device being initialized
 */
#  define device__init_decl( x ) \
\
winvblock__bool STDCALL \
x ( \
  IN device__type_ptr dev_ptr \
 )
/*
 * Function pointer for a device initialization routine.
 * 'indent' mangles this, so it looks weird
 */
typedef device__init_decl (
   ( *device__init_routine )
 );

/**
 * Device close routine
 *
 * @v dev_ptr           The device being closed
 */
#  define device__close_decl( x ) \
\
void STDCALL \
x ( \
  IN device__type_ptr dev_ptr \
 )
/*
 * Function pointer for a device close routine.
 * 'indent' mangles this, so it looks weird
 */
typedef device__close_decl (
   ( *device__close_routine )
 );

/**
 * Initialize the global, device-common environment
 *
 * @ret ntstatus        STATUS_SUCCESS or the NTSTATUS for a failure
 */
extern STDCALL NTSTATUS device__init (
  void
 );

/**
 * Create a new device
 *
 * @ret dev_ptr         The address of a new device, or NULL for failure
 *
 * This function should not be confused with a PDO creation routine, which is
 * actually implemented for each device type.  This routine will allocate a
 * device__type, track it in a global list, as well as populate the device
 * with default values.
 */
extern winvblock__lib_func STDCALL device__type_ptr device__create (
  void
 );

winvblock__def_struct ( device__ops )
{
  device__create_pdo_routine create_pdo;
  device__init_routine init;
  device__close_routine close;
};

/* Details common to all devices this driver works with */
struct _device__type
{
  size_t size;
  winvblock__bool IsBus;	/* For debugging */
  PDEVICE_OBJECT Self;
  PDEVICE_OBJECT Parent;
  PDRIVER_OBJECT DriverObject;
  device__state State;
  device__state OldState;
  irp__handler_chain irp_handler_chain;
  device__type_ptr next_sibling_ptr;
  device__ops ops;
  LIST_ENTRY tracking;
};

#endif				/* _device_h */
