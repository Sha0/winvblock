/*
  Copyright 2006-2008, V.
  For contact information, see http://winaoe.org/

  This file is part of WinAoE.

  WinAoE is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  WinAoE is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with WinAoE.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _AOE_H
#define _AOE_H
#include "portable.h"
#include "driver.h"

#define htons(x) (USHORT)((((x) << 8) & 0xff00) | (((x) >> 8) & 0xff))
#define ntohs(x) (USHORT)((((x) << 8) & 0xff00) | (((x) >> 8) & 0xff))

typedef enum {Read, Write} REQUESTMODE, *PREQUESTMODE;

BOOLEAN STDCALL AoESearchDrive(IN PDEVICEEXTENSION DeviceExtension);
NTSTATUS STDCALL AoERequest(IN PDEVICEEXTENSION DeviceExtension, IN REQUESTMODE Mode, IN LONGLONG StartSector, IN ULONG SectorCount, IN PUCHAR Buffer, IN PIRP Irp);
NTSTATUS STDCALL AoEReply(IN PUCHAR SourceMac, IN PUCHAR DestinationMac, IN PUCHAR Data, IN UINT DataSize);
VOID STDCALL AoEResetProbe();

#endif
