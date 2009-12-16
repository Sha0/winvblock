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

#  define AOEPROTOCOLID 0x88a2
#  define AOEPROTOCOLVER 1
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

/* Driver-common device extension detail */
winvblock__def_struct ( driver__dev_ext )
{
  driver__dev_ext_ptr driver_dev_ext;
  winvblock__bool IsBus;	/* For debugging */
  PDEVICE_OBJECT Self;
  PDRIVER_OBJECT DriverObject;
  driver__state State;
  driver__state OldState;
  irp__handling_ptr irp_handler_stack_ptr;
  size_t irp_handler_stack_size;
  irp__handler dispatch;
};

extern VOID STDCALL Driver_CompletePendingIrp (
  IN PIRP Irp
 );
/*
 * Note the exception to the function naming convention
 */
extern NTSTATUS STDCALL Error (
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

extern NTSTATUS STDCALL Driver_Dispatch (
  IN PDEVICE_OBJECT DeviceObject,
  IN PIRP Irp
 );

#endif				/* _DRIVER_H */
