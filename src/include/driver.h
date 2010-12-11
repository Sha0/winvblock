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
 * Driver specifics.
 */

/* For testing and debugging */
#if 0
#  define RIS
#  define DEBUGIRPS
#  define DEBUGMOSTPROTOCOLCALLS
#  define DEBUGALLPROTOCOLCALLS
#endif

#define POOLSIZE 2048

/* An unfortunate forward declaration.  Definition resolved in device.h */
winvblock__def_struct(device__type);

extern PDRIVER_OBJECT driver__obj_ptr;
extern winvblock__lib_func void STDCALL Driver_CompletePendingIrp(IN PIRP);
/* Note the exception to the function naming convention. */
extern winvblock__lib_func NTSTATUS STDCALL Error(IN PCHAR, IN NTSTATUS);
extern winvblock__lib_func struct bus__type * driver__bus(void);
/* Note the exception to the function naming convention. */
extern NTSTATUS STDCALL DriverEntry(
    IN PDRIVER_OBJECT,
    IN PUNICODE_STRING
  );

/* The physical/function device object's (PDO's/FDO's) DeviceExtension */
winvblock__def_struct(driver__dev_ext) {
    device__type_ptr device;
  };

/* The prototype for a device IRP dispatch. */
typedef NTSTATUS STDCALL driver__dispatch_func(
    IN PDEVICE_OBJECT,
    IN PIRP
  );

extern winvblock__lib_func driver__dispatch_func driver__default_dispatch;
extern winvblock__lib_func irp__handler driver__not_supported;
extern winvblock__lib_func irp__handler driver__create_close;

#endif	/* _DRIVER_H */
