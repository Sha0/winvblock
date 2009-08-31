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
#  define SECTORSIZE 512
#  define POOLSIZE 2048

typedef enum
{ NotStarted, Started, StopPending, Stopped, RemovePending,
	SurpriseRemovePending, Deleted
} STATE,
*PSTATE;
typedef enum
{ SearchNIC, GetSize, GettingSize, GetGeometry, GettingGeometry,
	GetMaxSectorsPerPacket, GettingMaxSectorsPerPacket,
	Done
} SEARCHSTATE,
*PSEARCHSTATE;

/*
 * Forward declarations for BUS_BUS and DISK_DISK types
 */
typedef struct _DRIVER_DEVICEEXTENSION DRIVER_DEVICEEXTENSION,
*PDRIVER_DEVICEEXTENSION;

#  define IRPHandler_Declaration(x) NTSTATUS STDCALL\
                                  x (\
                                  	IN PDEVICE_OBJECT DeviceObject,\
                                  	IN PIRP Irp,\
                                  	IN PIO_STACK_LOCATION Stack,\
                                  	IN PDRIVER_DEVICEEXTENSION DeviceExtension\
                                   )
/*
 * Function-type for an IRP handler. 'indent' mangles this, so it looks weird
 */
typedef IRPHandler_Declaration (
	 ( *DRIVER_IRPHANDLER )
 );

#  include "disk.h"
#  include "bus.h"

struct _DRIVER_DEVICEEXTENSION
{
	BOOLEAN IsBus;
	PDEVICE_OBJECT Self;
	PDRIVER_OBJECT DriverObject;
	STATE State;
	STATE OldState;
	DRIVER_IRPHANDLER Dispatch;
	union
	{
		struct _BUS_BUS Bus;
		struct _DISK_DISK Disk;
	};
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

#endif													/* _DRIVER_H */
