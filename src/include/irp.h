/**
 * Copyright (C) 2009-2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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

/* An unfortunate forward declaration.  Definition resolved in device.h */
struct _device__type;

/* We have lots of these, so offer a convenience macro for declarations */
#  define irp__handler_decl( x ) \
\
NTSTATUS STDCALL \
x ( \
  IN PDEVICE_OBJECT DeviceObject, \
  IN PIRP Irp, \
  IN PIO_STACK_LOCATION Stack, \
  IN struct _device__type *DeviceExtension, \
  OUT winvblock__bool_ptr completion_ptr \
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
  winvblock__uint8 irp_major_func;
  winvblock__uint8 irp_minor_func;
  winvblock__bool any_major;
  winvblock__bool any_minor;
  irp__handler handler;
};

winvblock__def_type ( winvblock__any_ptr, irp__handler_chain );

/**
 * Register an IRP handling table with a chain (with table size)
 *
 * @v chain_ptr Pointer to IRP handler chain to attach a table to
 * @v table     Table to add
 * @v size      Size of the table to add, in bytes
 * @ret         FALSE for failure, TRUE for success
 */
extern winvblock__lib_func winvblock__bool irp__reg_table_s (
  IN OUT irp__handler_chain_ptr chain_ptr,
  IN irp__handling_ptr table,
  IN size_t size
 );

/**
 * Register an IRP handling table with a chain
 *
 * @v chain_ptr Pointer to IRP handler chain to attach a table to
 * @v table     Table to add
 * @ret         FALSE for failure, TRUE for success
 */
#  define irp__reg_table( chain_ptr, table ) \
  irp__reg_table_s ( chain_ptr, table, sizeof ( table ) )

/**
 * Un-register an IRP handling table from a chain
 *
 * @v chain_ptr Pointer to IRP handler chain to remove table from
 * @v table     Table to remove
 * @ret         FALSE for failure, TRUE for success
 */
winvblock__lib_func winvblock__bool irp__unreg_table (
  IN OUT irp__handler_chain_ptr chain_ptr,
  IN irp__handling_ptr table
 );

extern irp__handler_decl (
  irp__process
 );

#endif				/* _irp_h */
