/**
 * Copyright (C) 2009-2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef WV_M_REGISTRY_H_
#  define WV_M_REGISTRY_H_

/**
 * @file
 *
 * Registry specifics.
 */

extern winvblock__lib_func NTSTATUS STDCALL WvlRegNoteOsLoadOpts(LPWSTR *);
extern winvblock__lib_func NTSTATUS STDCALL WvlRegOpenKey(LPCWSTR, PHANDLE);
extern winvblock__lib_func void STDCALL WvlRegCloseKey(HANDLE);
extern winvblock__lib_func NTSTATUS STDCALL WvlRegFetchKvi(
    HANDLE,
    LPCWSTR,
    PKEY_VALUE_PARTIAL_INFORMATION *
  );
extern winvblock__lib_func NTSTATUS STDCALL WvlRegFetchSz(
    HANDLE,
    LPCWSTR,
    LPWSTR *
  );
extern winvblock__lib_func NTSTATUS STDCALL WvlRegFetchMultiSz(
    HANDLE,
    LPCWSTR,
    LPWSTR **
  );
extern winvblock__lib_func NTSTATUS STDCALL WvlRegStoreSz(
    HANDLE,
    LPCWSTR,
    LPWSTR
  );
extern winvblock__lib_func NTSTATUS STDCALL WvlRegStoreDword(
    HANDLE,
    LPCWSTR,
    winvblock__uint32
  );

#endif  /* WV_M_REGISTRY_H_ */
