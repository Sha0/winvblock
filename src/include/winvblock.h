/**
 * Copyright (C) 2009-2012, Shao Miller <sha0.miller@gmail.com>.
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
#ifndef WVL_M_WINVBLOCK_H_
#  define WVL_M_WINVBLOCK_H_

/**
 * @file
 *
 * WinVBlock project common material
 */

#define WvlCountof(array) (sizeof (array) / sizeof *(array))

#define WVL_M_LIT "WinVBlock"
#define WVL_M_WLIT L"WinVBlock"

/* To export functions while serving as a library. */
#ifdef PROJECT_WV
#  define WVL_M_LIB __declspec(dllexport)
#else
#  define WVL_M_LIB __declspec(dllimport)
#endif

/*** Object types. */
typedef union U_WV_LARGE_INT_ U_WV_LARGE_INT, UP_WV_LARGE_INT;

/*** Struct/union definitions. */
union U_WV_LARGE_INT_ {
    LONGLONG longlong;
    LARGE_INTEGER large_int;
  };

#endif  /* WVL_M_WINVBLOCK_H_ */
