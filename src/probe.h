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

extern MDI_PATCHAREA Probe_Globals_BootMemdisk;

extern VOID STDCALL Probe_MemDisk (
	PDEVICE_OBJECT BusDeviceObject
 );

extern VOID STDCALL Probe_AoE (
	PDEVICE_OBJECT BusDeviceObject
 );

#endif													/* _PROBE_H */
