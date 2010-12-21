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
#ifndef WV_M_REGISTRY_H_
#  define WV_M_REGISTRY_H_

/**
 * @file
 *
 * Registry specifics
 *
 */

/**
 * Note BOOT.INI-style OsLoadOptions from registry
 *
 * @v w_str_ptr         Pointer to pointer to wide-char string to hold options
 * @ret ntstatus	NT status
 *
 * The caller must eventually free the wide-char string.
 */
extern NTSTATUS registry__note_os_load_opts (
  LPWSTR * w_str_ptr
 );

/**
 * Open registry key
 *
 * @v reg_key_name  Registry key name
 * @v reg_key       Registry key to fill in
 * @ret ntstatus    NT status
 */
extern winvblock__lib_func NTSTATUS registry__open_key (
  LPCWSTR reg_key_name,
  PHANDLE reg_key
 );

/**
 * Close registry key
 *
 * @v reg_key   Registry key
 */
extern winvblock__lib_func void registry__close_key (
  HANDLE reg_key
 );

/**
 * Fetch registry key value information
 *
 * @v reg_key     Registry key
 * @v value_name  Registry value name
 * @v kvi         Key value information block to allocate and fill in
 * @ret ntstatus  NT status
 *
 * The caller must eventually free the allocated key value information
 * block.
 */
extern winvblock__lib_func NTSTATUS registry__fetch_kvi (
  HANDLE reg_key,
  LPCWSTR value_name,
  PKEY_VALUE_PARTIAL_INFORMATION * kvi
 );

/**
 * Fetch registry string value
 *
 * @v reg_key     Registry key
 * @v value_name  Registry value name
 * @v value       String value to allocate and fill in
 * @ret ntstatus  NT status
 *
 * The caller must eventually free the allocated value.
 */
extern winvblock__lib_func NTSTATUS registry__fetch_sz (
  HANDLE reg_key,
  LPCWSTR value_name,
  LPWSTR * value
 );

/**
 * Fetch registry multiple-string value
 *
 * @v reg_key     Registry key
 * @v value_name  Registry value name
 * @v values      Array of string values to allocate and fill in
 * @ret ntstatus  NT status
 *
 * The caller must eventually free the allocated values.
 */
extern winvblock__lib_func NTSTATUS registry__fetch_multi_sz (
  HANDLE reg_key,
  LPCWSTR value_name,
  LPWSTR ** values
 );

/**
 * Store registry string value
 *
 * @v reg_key     Registry key
 * @v value_name  Registry value name
 * @v value       String value to store
 * @ret ntstatus  NT status
 */
extern winvblock__lib_func NTSTATUS registry__store_sz (
  HANDLE reg_key,
  LPCWSTR value_name,
  LPWSTR value
 );

/**
 * Store registry dword value
 *
 * @v reg_key     Registry key
 * @v value_name  Registry value name
 * @v value       String value to store, or NULL
 * @ret ntstatus  NT status
 */
extern winvblock__lib_func NTSTATUS registry__store_dword (
  HANDLE reg_key,
  LPCWSTR value_name,
  winvblock__uint32 value
 );

#endif  /* WV_M_REGISTRY_H_ */
