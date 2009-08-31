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
#ifndef _AOEDISK_H
#  define _AOEDISK_H

/**
 * @file
 *
 * AoE disk specifics
 *
 */

typedef struct _AOEDISK_AOEDISK
{
	ULONG MTU;
	UCHAR ClientMac[6];
	UCHAR ServerMac[6];
	ULONG Major;
	ULONG Minor;
	ULONG MaxSectorsPerPacket;
	ULONG Timeout;
} AOEDISK_AOEDISK,
*PAOEDISK_AOEDISK;

extern IRPHandler_Declaration (
	Disk_Dispatch
 );

extern IRPHandler_Declaration (
	Disk_DispatchPnP
 );

extern IRPHandler_Declaration (
	Disk_DispatchSCSI
 );

extern IRPHandler_Declaration (
	Disk_DispatchDeviceControl
 );

extern IRPHandler_Declaration (
	Disk_DispatchSystemControl
 );

#endif													/* _AOEDISK_H */
