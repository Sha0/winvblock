/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef WVL_M_WINVBLOCK_H_
#  define WVL_M_WINVBLOCK_H_

/**
 * @file
 *
 * WinVBlock project common material.
 *
 */

#define WVL_M_LIT "WinVBlock"
#define WVL_M_WLIT L"WinVBlock"

/* A common way to define a structure. */
#  define winvblock__def_struct( x ) \
\
struct _##x;\
typedef struct _##x x, *x##_ptr;\
struct _##x

/* A common way to define a type. */
#  define winvblock__def_type( old, new ) \
\
typedef old new, *new##_ptr

/* Set up type definitions for an enumated type. */
#  define winvblock__def_enum( x ) \
\
typedef enum _##x x, *x##_ptr

/* Common type definitions */
winvblock__def_type ( UCHAR, winvblock__uint8 );
winvblock__def_type ( UINT64, winvblock__uint64 );
winvblock__def_type ( UINT32, winvblock__uint32 );
winvblock__def_type ( UINT16, winvblock__uint16 );
winvblock__def_type ( BOOLEAN, winvblock__bool );
typedef void *winvblock__any_ptr;

/* To export functions while serving as a library. */
#  ifdef PROJECT_BUS
#    define WVL_M_LIB __declspec(dllexport)
#  else
#    define WVL_M_LIB __declspec(dllimport)
#  endif

#endif  /* WVL_M_WINVBLOCK_H_ */
