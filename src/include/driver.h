/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef WV_M_DRIVER_H_
#  define WV_M_DRIVER_H_

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

extern PDRIVER_OBJECT WvDriverObj;
/* Note the exception to the function naming convention. */
extern DRIVER_INITIALIZE DriverEntry;

/* The physical/function device object's (PDO's/FDO's) DeviceExtension */
typedef struct WV_DEV_EXT {
    struct WV_DEV_T * device;
  } WV_S_DEV_EXT, * WV_SP_DEV_EXT;

extern BOOLEAN STDCALL WvBusAddDev(IN WV_SP_DEV_T);
extern NTSTATUS STDCALL WvDriverGetDevCapabilities(
    IN PDEVICE_OBJECT,
    IN PDEVICE_CAPABILITIES
  );

/**
 * Miscellaneous: Grouped memory allocation functions.
 */

/* A group of memory allocations. */
typedef struct WVL_MEM_GROUP {
    PCHAR First;
    PCHAR Last;
    PCHAR Current;
  } WVL_S_MEM_GROUP, * WVL_SP_MEM_GROUP;
extern WVL_M_LIB VOID STDCALL WvlMemGroupInit(OUT WVL_SP_MEM_GROUP);
extern WVL_M_LIB PVOID STDCALL WvlMemGroupAlloc(
    IN OUT WVL_SP_MEM_GROUP,
    IN SIZE_T
  );
extern WVL_M_LIB VOID STDCALL WvlMemGroupFree(IN OUT WVL_SP_MEM_GROUP);
extern WVL_M_LIB PVOID STDCALL WvlMemGroupNextObj(IN OUT WVL_SP_MEM_GROUP);
extern WVL_M_LIB PVOID STDCALL WvlMemGroupBatchAlloc(
    IN OUT WVL_SP_MEM_GROUP,
    IN SIZE_T,
    IN UINT32
  );

#endif	/* WV_M_DRIVER_H_ */
