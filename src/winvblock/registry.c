/**
 * Copyright (C) 2009-2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 * Copyright (C) 2008, Michael Brown <mbrown@fensystems.co.uk>.
 * Copyright 2006-2008, V.
 * For WinAoE contact information, see http://winaoe.org/
 *
 * This file is part of WinVBlock, derived from WinAoE.
 * Contains functions from "sanbootconf" by Michael Brown.
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

/**
 * @file
 *
 * Registry specifics
 */

#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "debug.h"
#include "irp.h"
#include "driver.h"
#include "registry.h"

/**
 * Open registry key
 *
 * @v reg_key_name  Registry key name
 * @v reg_key       Registry key to fill in
 * @ret ntstatus    NT status
 */
winvblock__lib_func NTSTATUS
registry__open_key (
  LPCWSTR reg_key_name,
  PHANDLE reg_key
 )
{
  UNICODE_STRING unicode_string;
  OBJECT_ATTRIBUTES object_attrs;
  NTSTATUS status;

  RtlInitUnicodeString ( &unicode_string, reg_key_name );
  InitializeObjectAttributes ( &object_attrs, &unicode_string,
			       OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL,
			       NULL );
  status = ZwOpenKey ( reg_key, KEY_ALL_ACCESS, &object_attrs );
  if ( !NT_SUCCESS ( status ) )
    {
      DBG ( "Could not open %S: %x\n", reg_key_name, status );
      return status;
    }

  return STATUS_SUCCESS;
}

/**
 * Close registry key
 *
 * @v reg_key   Registry key
 */
winvblock__lib_func void
registry__close_key (
  HANDLE reg_key
 )
{
  ZwClose ( reg_key );
}

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
winvblock__lib_func NTSTATUS
registry__fetch_kvi (
  HANDLE reg_key,
  LPCWSTR value_name,
  PKEY_VALUE_PARTIAL_INFORMATION * kvi
 )
{
  UNICODE_STRING u_value_name;
  winvblock__uint32 kvi_len;
  NTSTATUS status;

  /*
   * Get value length 
   */
  RtlInitUnicodeString ( &u_value_name, value_name );
  status =
    ZwQueryValueKey ( reg_key, &u_value_name, KeyValuePartialInformation, NULL,
		      0, &kvi_len );
  if ( !
       ( ( status == STATUS_SUCCESS ) || ( status == STATUS_BUFFER_OVERFLOW )
	 || ( status == STATUS_BUFFER_TOO_SMALL ) ) )
    {
      DBG ( "Could not get KVI length for \"%S\": %x\n", value_name, status );
      goto err_zwqueryvaluekey_len;
    }

  /*
   * Allocate value buffer 
   */
  *kvi = ExAllocatePool ( NonPagedPool, kvi_len );
  if ( !*kvi )
    {
      DBG ( "Could not allocate KVI for \"%S\": %x\n", value_name, status );
      goto err_exallocatepoolwithtag_kvi;
    }

  /*
   * Fetch value 
   */
  status =
    ZwQueryValueKey ( reg_key, &u_value_name, KeyValuePartialInformation, *kvi,
		      kvi_len, &kvi_len );
  if ( !NT_SUCCESS ( status ) )
    {
      DBG ( "Could not get KVI for \"%S\": %x\n", value_name, status );
      goto err_zwqueryvaluekey;
    }

  return STATUS_SUCCESS;

err_zwqueryvaluekey:
  ExFreePool ( kvi );
err_exallocatepoolwithtag_kvi:
err_zwqueryvaluekey_len:
  return status;
}

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
winvblock__lib_func NTSTATUS
registry__fetch_sz (
  HANDLE reg_key,
  LPCWSTR value_name,
  LPWSTR * value
 )
{
  PKEY_VALUE_PARTIAL_INFORMATION kvi;
  winvblock__uint32 value_len;
  NTSTATUS status;

  /*
   * Fetch key value information 
   */
  status = registry__fetch_kvi ( reg_key, value_name, &kvi );
  if ( !NT_SUCCESS ( status ) )
    goto err_fetchkvi;

  /*
   * Allocate and populate string 
   */
  value_len = ( kvi->DataLength + sizeof ( value[0] ) );
  *value = ExAllocatePool ( NonPagedPool, value_len );
  if ( !*value )
    {
      DBG ( "Could not allocate value for \"%S\"\n", value_name );
      status = STATUS_UNSUCCESSFUL;
      goto err_exallocatepoolwithtag_value;
    }
  RtlZeroMemory ( *value, value_len );
  RtlCopyMemory ( *value, kvi->Data, kvi->DataLength );

err_exallocatepoolwithtag_value:
  ExFreePool ( kvi );
err_fetchkvi:
  return status;
}

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
winvblock__lib_func NTSTATUS
registry__fetch_multi_sz (
  HANDLE reg_key,
  LPCWSTR value_name,
  LPWSTR ** values
 )
{
  PKEY_VALUE_PARTIAL_INFORMATION kvi;
  LPWSTR string;
  winvblock__uint32 num_strings;
  winvblock__uint32 values_len;
  winvblock__uint32 i;
  NTSTATUS status;

  /*
   * Fetch key value information 
   */
  status = registry__fetch_kvi ( reg_key, value_name, &kvi );
  if ( !NT_SUCCESS ( status ) )
    goto err_fetchkvi;

  /*
   * Count number of strings in the array.  This is a
   * potential(ly harmless) overestimate.
   */
  num_strings = 0;
  for ( string = ( ( LPWSTR ) kvi->Data );
	string < ( ( LPWSTR ) ( kvi->Data + kvi->DataLength ) ); string++ )
    {
      if ( !*string )
	num_strings++;
    }

  /*
   * Allocate and populate string array 
   */
  values_len =
    ( ( ( num_strings + 1 ) * sizeof ( values[0] ) ) + kvi->DataLength +
      sizeof ( values[0][0] ) );
  *values = ExAllocatePool ( NonPagedPool, values_len );
  if ( !*values )
    {
      DBG ( "Could not allocate value array for \"%S\"\n", value_name );
      status = STATUS_UNSUCCESSFUL;
      goto err_exallocatepoolwithtag_value;
    }
  RtlZeroMemory ( *values, values_len );
  string = ( ( LPWSTR ) ( *values + num_strings + 1 ) );
  RtlCopyMemory ( string, kvi->Data, kvi->DataLength );
  for ( i = 0; i < num_strings; i++ )
    {
      ( *values )[i] = string;
      while ( *string )
	string++;
      while ( !*string )
	string++;
    }

err_exallocatepoolwithtag_value:
  ExFreePool ( kvi );
err_fetchkvi:
  return status;
}

/**
 * Store registry string value
 *
 * @v reg_key     Registry key
 * @v value_name  Registry value name
 * @v value       String value to store
 * @ret ntstatus  NT status
 */
winvblock__lib_func NTSTATUS
registry__store_sz (
  HANDLE reg_key,
  LPCWSTR value_name,
  LPWSTR value
 )
{
  UNICODE_STRING u_value_name;
  SIZE_T value_len;
  NTSTATUS status;

  RtlInitUnicodeString ( &u_value_name, value_name );
  value_len = ( ( wcslen ( value ) + 1 ) * sizeof ( value[0] ) );
  status =
    ZwSetValueKey ( reg_key, &u_value_name, 0, REG_SZ, value,
		    ( ( winvblock__uint32 ) value_len ) );
  if ( !NT_SUCCESS ( status ) )
    {
      DBG ( "Could not store value \"%S\": %x\n", value_name, status );
      return status;
    }

  return STATUS_SUCCESS;
}

/**
 * Store registry dword value
 *
 * @v reg_key     Registry key
 * @v value_name  Registry value name
 * @v value       String value to store, or NULL
 * @ret ntstatus  NT status
 */
winvblock__lib_func NTSTATUS
registry__store_dword (
  HANDLE reg_key,
  LPCWSTR value_name,
  winvblock__uint32 value
 )
{
  UNICODE_STRING u_value_name;
  NTSTATUS status;

  RtlInitUnicodeString ( &u_value_name, value_name );
  status =
    ZwSetValueKey ( reg_key, &u_value_name, 0, REG_DWORD, &value,
		    sizeof ( value ) );
  if ( !NT_SUCCESS ( status ) )
    {
      DBG ( "Could not store value \"%S\": %x\n", value_name, status );
      return status;
    }

  return STATUS_SUCCESS;
}

/**
 * Note BOOT.INI-style OsLoadOptions from registry
 *
 * @v w_str_ptr         Pointer to pointer to wide-char string to hold options
 * @ret ntstatus	NT status
 *
 * The caller must eventually free the wide-char string.
 */
NTSTATUS
registry__note_os_load_opts (
  LPWSTR * w_str_ptr
 )
{
  NTSTATUS status;
  HANDLE control_key;

  /*
   * Open the Control key 
   */
  status =
    registry__open_key
    ( L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\",
      &control_key );
  if ( !NT_SUCCESS ( status ) )
    goto err_keyopen;

  /*
   * Put the SystemStartOptions value into a global 
   */
  status =
    registry__fetch_sz ( control_key, L"SystemStartOptions", w_str_ptr );
  if ( !NT_SUCCESS ( status ) )
    goto err_fetchsz;

  DBG ( "OsLoadOptions: %S\n", *w_str_ptr );

err_fetchsz:

  registry__close_key ( control_key );
err_keyopen:

  return status;
}
