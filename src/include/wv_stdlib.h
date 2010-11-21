/**
 * Copyright (C) 2009-2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 *
 * This file is part of WinVBlock, originally derived from WinAoE.
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
#ifndef _wv_stdlib_h
#  define _wv_stdlib_h
#  include "wv_stddef.h"

/* Allocate memory from non-paged memory pool. */
void *wv_malloc(wv_size_t size);

/* Allocate memory from paged memory pool. */
void *wv_palloc(wv_size_t size);

#endif /* _wv_stdlib_h */
