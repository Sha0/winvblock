/**
 * Copyright (C) 2009, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 *
 * This file is part of WinVBlock, derived from WinAoE.
 * For WinAoE contact information, see http://winaoe.org/
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
#ifndef _irp_h
#  define _irp_h

/**
 * @file
 *
 * Handling IRPs
 *
 */

/* An unfortunate forward declaration.  Definition resolved in driver.h */
struct _driver__dev_ext;

/* We have lots of these, so offer a convenience macro for declarations */
#  define irp__handler_decl( x ) \
\
NTSTATUS STDCALL \
x ( \
  IN PDEVICE_OBJECT DeviceObject, \
  IN PIRP Irp, \
  IN PIO_STACK_LOCATION Stack, \
  IN struct _driver__dev_ext *DeviceExtension \
 )
/*
 * Function pointer for an IRP handler.
 * 'indent' mangles this, so it looks weird
 */
typedef irp__handler_decl (
	 ( *irp__handler )
 );

winvblock__def_struct ( irp__handling )
{
	winvblock__uint8 function;
	irp__handler handler;
};

#endif													/* _irp_h */
