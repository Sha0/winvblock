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
#ifndef _PROBE_H
#  define _PROBE_H

/**
 * @file
 *
 * Boot-time disk probing specifics
 *
 */

#  include <ntddk.h>
#  include "mdi.h"

extern VOID STDCALL Probe_AoE (
	PDEVICE_OBJECT BusDeviceObject
 );

extern VOID STDCALL Probe_MemDisk (
	PDEVICE_OBJECT BusDeviceObject
 );

extern VOID STDCALL Probe_Grub4Dos (
	PDEVICE_OBJECT BusDeviceObject
 );

#endif													/* _PROBE_H */
