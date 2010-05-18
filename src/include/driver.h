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
#ifndef _DRIVER_H
#  define _DRIVER_H

/**
 * @file
 *
 * Driver specifics
 *
 */

#  include "portable.h"

/**
 * #define RIS
 *
 * #define DEBUGIRPS
 * #define DEBUGMOSTPROTOCOLCALLS
 * #define DEBUGALLPROTOCOLCALLS
 */

#  define POOLSIZE 2048

enum _driver__state
{
  NotStarted,
  Started,
  StopPending,
  Stopped,
  RemovePending,
  SurpriseRemovePending,
  Deleted
};
winvblock__def_enum ( driver__state );

extern irp__handling driver__handling_table[];
extern size_t driver__handling_table_size;
extern PDRIVER_OBJECT driver__obj_ptr;

/* Forward declaration */
winvblock__def_struct ( device__type );

/**
 * Device PDO creation routine
 *
 * @v dev_ext_ptr     The device whose PDO should be created
 */
#  define driver__dev_create_pdo_decl( x ) \
\
PDEVICE_OBJECT STDCALL \
x ( \
  IN device__type_ptr dev_ptr \
 )
/*
 * Function pointer for a device initialization routine.
 * 'indent' mangles this, so it looks weird
 */
typedef driver__dev_create_pdo_decl (
   ( *driver__dev_create_pdo_routine )
 );

/**
 * Device initialization routine
 *
 * @v dev_ptr           The device being initialized
 */
#  define driver__dev_init_decl( x ) \
\
winvblock__bool STDCALL \
x ( \
  IN device__type_ptr dev_ptr \
 )
/*
 * Function pointer for a device initialization routine.
 * 'indent' mangles this, so it looks weird
 */
typedef driver__dev_init_decl (
   ( *driver__dev_init_routine )
 );

/**
 * Device close routine
 *
 * @v dev_ptr           The device being closed
 */
#  define driver__dev_close_decl( x ) \
\
void STDCALL \
x ( \
  IN device__type_ptr dev_ptr \
 )
/*
 * Function pointer for a device close routine.
 * 'indent' mangles this, so it looks weird
 */
typedef driver__dev_close_decl (
   ( *driver__dev_close_routine )
 );

winvblock__def_struct ( driver__dev_ops )
{
  driver__dev_create_pdo_routine create_pdo;
  driver__dev_init_routine init;
  driver__dev_close_routine close;
};

/* Driver-common device extension detail */
struct _device__type
{
  size_t size;
  winvblock__bool IsBus;	/* For debugging */
  PDEVICE_OBJECT Self;
  PDEVICE_OBJECT Parent;
  PDRIVER_OBJECT DriverObject;
  driver__state State;
  driver__state OldState;
  irp__handler_chain irp_handler_chain;
  device__type_ptr next_sibling_ptr;
  driver__dev_ops_ptr ops;
};

extern winvblock__lib_func void STDCALL Driver_CompletePendingIrp (
  IN PIRP Irp
 );
/*
 * Note the exception to the function naming convention
 */
extern winvblock__lib_func NTSTATUS STDCALL Error (
  IN PCHAR Message,
  IN NTSTATUS Status
 );
/*
 * Note the exception to the function naming convention
 */
extern NTSTATUS STDCALL DriverEntry (
  IN PDRIVER_OBJECT DriverObject,
  IN PUNICODE_STRING RegistryPath
 );

#endif				/* _DRIVER_H */
