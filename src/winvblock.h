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
#ifndef _winvblock_h
#  define _winvblock_h

/**
 * @file
 *
 * WinVBlock project common material
 *
 */

/* A common way to define a structure */
#  define winvblock__def_struct( x ) \
\
struct _##x;\
typedef struct _##x x, *x##_ptr;\
struct _##x

/* Common type definitions */
typedef UCHAR winvblock__uint8,
*winvblock__uint8_ptr;
typedef UINT64 winvblock__uint64,
*winvblock__uint64_ptr;

#endif													/* _winvblock_h */
