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
#ifndef AOE_M_PROTOCOL_H_
#  define AOE_M_PROTOCOL_H_

/**
 * @file
 *
 * Protocol specifics
 *
 */

extern BOOLEAN STDCALL Protocol_SearchNIC (
  IN PUCHAR Mac
 );
extern UINT32 STDCALL Protocol_GetMTU (
  IN PUCHAR Mac
 );
extern BOOLEAN STDCALL Protocol_Send (
  IN PUCHAR SourceMac,
  IN PUCHAR DestinationMac,
  IN PUCHAR Data,
  IN UINT32 DataSize,
  IN PVOID PacketContext
 );
extern NTSTATUS Protocol_Start(void);
extern VOID Protocol_Stop(void);

#endif  /* AOE_M_PROTOCOL_H_ */
