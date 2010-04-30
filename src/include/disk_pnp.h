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
#ifndef _DISK_PNP_H
#  define _DISK_PNP_H

/**
 * @file
 *
 * Disk PnP IRP handling
 *
 */

extern irp__handler_decl (
  disk_pnp__query_id
 );
extern irp__handler_decl (
  disk_pnp__query_dev_text
 );
extern irp__handler_decl (
  disk_pnp__query_dev_relations
 );
extern irp__handler_decl (
  disk_pnp__query_bus_info
 );
extern irp__handler_decl (
  disk_pnp__query_capabilities
 );
extern irp__handler_decl (
  disk_pnp__simple
 );

#endif				/* _DISK_PNP_H */
