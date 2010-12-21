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
#ifndef WV_M_PORTABLE_H_
#  define WV_M_PORTABLE_H_

/**
 * @file
 *
 * Portability definitions
 *
 */

#  define NDIS50 1
#  define OBJ_KERNEL_HANDLE 0x00000200L
#  ifdef _MSC_VER
#    define STDCALL
#    define __attribute__(x)
typedef unsigned int UINT,
*PUINT;
#  endif

#  if _WIN32_WINNT < 0x0502
#    ifdef SCSIOP_READ16
#      undef SCSIOP_READ16
#      define SCSIOP_READ16 0x88
#    endif
#    ifdef SCSIOP_WRITE16
#      undef SCSIOP_WRITE16
#      define SCSIOP_WRITE16 0x8a
#    endif
#    ifdef SCSIOP_VERIFY16
#      undef SCSIOP_VERIFY16
#      define SCSIOP_VERIFY16 0x8f
#    endif
#    ifdef SCSIOP_READ_CAPACITY16
#      undef SCSIOP_READ_CAPACITY16
#      define SCSIOP_READ_CAPACITY16 0x9e
#    endif
#  endif

#endif  /* WV_M_PORTABLE_H_ */
