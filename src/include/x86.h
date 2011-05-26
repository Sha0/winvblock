/**
 * Copyright (C) 2010-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef M_X86_H_

/****
 * @file
 *
 * x86 stuff.
 */

/*** Macros. */
#define M_X86_H_

/**
 * Return a 32-bit, linear address for a SEG16:OFF16
 *
 * @v Seg16Off16        The WV_SP_PROBE_INT_VECTOR with the
 *                      segment and offset.
 * @ret UINT32          The linear address for the segment and offset.
 */
#define M_X86_SEG16OFF16_ADDR(Seg16Off16) \
  ((((UINT32)(Seg16Off16)->Segment) << 4) + (Seg16Off16)->Offset)

/*** Types. */
typedef struct S_X86_SEG16OFF16_ S_X86_SEG16OFF16, * SP_X86_SEG16OFF16;

/*** Struct definitions. */
#ifdef _MSC_VER
#  pragma pack(1)
#endif
struct S_X86_SEG16OFF16_ {
    UINT16 Offset;
    UINT16 Segment;
  } __attribute__((__packed__));
#ifdef _MSC_VER
#  pragma pack()
#endif

#endif /* M_X86_H_ */
