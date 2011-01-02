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
 */

#define WVL_M_LIT "WinVBlock"
#define WVL_M_WLIT L"WinVBlock"

/* To export functions while serving as a library. */
#ifdef PROJECT_WV
#  define WVL_M_LIB __declspec(dllexport)
#else
#  define WVL_M_LIB __declspec(dllimport)
#endif

#endif  /* WVL_M_WINVBLOCK_H_ */
