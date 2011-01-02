/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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

extern WVL_M_LIB NTSTATUS STDCALL WvlRegNoteOsLoadOpts(LPWSTR *);
extern WVL_M_LIB NTSTATUS STDCALL WvlRegOpenKey(LPCWSTR, PHANDLE);
extern WVL_M_LIB VOID STDCALL WvlRegCloseKey(HANDLE);
extern WVL_M_LIB NTSTATUS STDCALL WvlRegFetchKvi(
    HANDLE,
    LPCWSTR,
    PKEY_VALUE_PARTIAL_INFORMATION *
  );
extern WVL_M_LIB NTSTATUS STDCALL WvlRegFetchSz(
    HANDLE,
    LPCWSTR,
    LPWSTR *
  );
extern WVL_M_LIB NTSTATUS STDCALL WvlRegFetchMultiSz(
    HANDLE,
    LPCWSTR,
    LPWSTR **
  );
extern WVL_M_LIB NTSTATUS STDCALL WvlRegFetchDword(
    IN HANDLE,
    IN LPCWSTR,
    OUT UINT32 *
  );
extern WVL_M_LIB NTSTATUS STDCALL WvlRegStoreSz(
    HANDLE,
    LPCWSTR,
    LPWSTR
  );
extern WVL_M_LIB NTSTATUS STDCALL WvlRegStoreDword(
    HANDLE,
    LPCWSTR,
    UINT32
  );

#endif  /* WV_M_REGISTRY_H_ */
