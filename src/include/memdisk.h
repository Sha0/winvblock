/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef M_MEMDISK_H_

/****
 * @file
 *
 * MEMDISK specifics.
 */

/*** Macros. */
#define M_MEMDISK_H_

/*** Function declarations. */
extern VOID WvMemdiskFind(void);
extern BOOLEAN WvMemdiskProcessSafeHook(
    PUCHAR,
    WV_SP_PROBE_SAFE_MBR_HOOK
  );

#endif  /* M_MEMDISK_H_ */
