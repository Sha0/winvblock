/*
 * Copyright (C) 2009, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
 *
 * Jul-03-2009@22:39: Initial revision.
 */

#ifndef _DEBUG_H
#  define _DEBUG_H

/**
 * @file
 *
 * Debugging message system
 *
 * We wrap the DbgPrint() function in order to automatically display file,
 * function and line number in any debugging output messages.
 */

#  ifdef DBG
#    undef DBG
#  endif
#  define DBG( ... ) xDbgPrint ( __FILE__, ( const PCHAR )__FUNCTION__, \
                                  __LINE__ ) || 1 ? \
                                  DbgPrint ( __VA_ARGS__ ) : \
                                  0

extern NTSTATUS STDCALL xDbgPrint (
	IN PCHAR File,
	IN PCHAR Function,
	IN winvblock__uint32 Line
 );
extern VOID STDCALL Debug_Initialize (
	void
 );
extern VOID STDCALL Debug_IrpStart (
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp
 );
extern VOID STDCALL Debug_IrpEnd (
	IN PIRP Irp,
	IN NTSTATUS Status
 );

#endif													/* _DEBUG_H */
