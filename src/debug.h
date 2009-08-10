/*
 * Copyright (C) 2009 Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Jul-03-2009@22:39: Initial revision.
 */
#ifndef _DEBUG_H
#define _DEBUG_H

/**
 * @file
 *
 * Debugging message system
 *
 * We wrap the DbgPrint() function in order to automatically display file,
 * function and line number in any debugging output messages.
 */

#define DBG( ... ) xDbgPrint ( __FILE__, ( const PCHAR )__FUNCTION__, \
			       __LINE__ ) || 1 ? \
		   DbgPrint ( __VA_ARGS__ ) : \
		   0

extern NTSTATUS STDCALL xDbgPrint ( IN PCHAR File, IN PCHAR Function,
				    IN UINT Line );
extern VOID STDCALL InitializeDebug (  );
extern VOID STDCALL DebugIrpStart ( IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp );
extern VOID STDCALL DebugIrpEnd ( IN PIRP Irp, IN NTSTATUS Status );

#endif				/* _DEBUG_H */
