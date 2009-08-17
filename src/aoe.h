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
#ifndef _AOE_H
#define _AOE_H

/**
 * @file
 *
 * AoE specifics
 *
 */

#include "portable.h"
#include "driver.h"

#define htons(x) (USHORT)((((x) << 8) & 0xff00) | (((x) >> 8) & 0xff))
#define ntohs(x) (USHORT)((((x) << 8) & 0xff00) | (((x) >> 8) & 0xff))

typedef enum { Read, Write } REQUESTMODE, *PREQUESTMODE;

extern BOOLEAN STDCALL AoESearchDrive ( IN PDEVICEEXTENSION DeviceExtension );
extern NTSTATUS STDCALL AoERequest ( IN PDEVICEEXTENSION DeviceExtension,
			      IN REQUESTMODE Mode, IN LONGLONG StartSector,
			      IN ULONG SectorCount, IN PUCHAR Buffer,
			      IN PIRP Irp );
extern NTSTATUS STDCALL AoEReply ( IN PUCHAR SourceMac, IN PUCHAR DestinationMac,
			    IN PUCHAR Data, IN UINT DataSize );
extern VOID STDCALL AoEResetProbe ( void );
extern NTSTATUS STDCALL AoEStart ( void );
extern VOID STDCALL AoEStop ( void );

#endif				/* _AOE_H */
