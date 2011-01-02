/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
 * Registry specifics.
 */

#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "debug.h"
#include "driver.h"
#include "registry.h"

/**
 * Open registry key.
 *
 * @v RegKeyName        Registry key name.
 * @v RegKey            Registry key handle to fill in.
 * @ret NTSTATUS        The status of the operation.
 */
WVL_M_LIB NTSTATUS STDCALL WvlRegOpenKey(
    LPCWSTR RegKeyName,
    PHANDLE RegKey
  ) {
    UNICODE_STRING unicode_string;
    OBJECT_ATTRIBUTES object_attrs;
    NTSTATUS status;

    RtlInitUnicodeString(&unicode_string, RegKeyName);
    InitializeObjectAttributes(
        &object_attrs,
        &unicode_string,
        OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
        NULL,
        NULL
      );
    status = ZwOpenKey(RegKey, KEY_ALL_ACCESS, &object_attrs);
    if (!NT_SUCCESS(status)) {
        DBG("Could not open %S: %x\n", RegKeyName, status);
        return status;
      }

    return STATUS_SUCCESS;
  }

/**
 * Close a registry key handle.
 *
 * @v RegKey            Registry key handle to close.
 */
WVL_M_LIB VOID STDCALL WvlRegCloseKey(HANDLE RegKey) {
    ZwClose(RegKey);
  }

/**
 * Fetch registry key value information.
 *
 * @v RegKey            Registry key to fetch KVI for.
 * @v ValueName         Registry value name to fetch KVI for.
 * @v Kvi               Key value information block to allocate and fill in.
 * @ret NTSTATUS        The status of the operation.
 *
 * The caller must eventually free the allocated key value information
 * block.
 */
WVL_M_LIB NTSTATUS STDCALL WvlRegFetchKvi(
    HANDLE RegKey,
    LPCWSTR ValueName,
    PKEY_VALUE_PARTIAL_INFORMATION * Kvi
  ) {
    UNICODE_STRING u_value_name;
    UINT32 kvi_len;
    NTSTATUS status;

    /* Get value length. */
    RtlInitUnicodeString(&u_value_name, ValueName);
    status = ZwQueryValueKey(
        RegKey,
        &u_value_name,
        KeyValuePartialInformation,
        NULL,
        0,
        &kvi_len
      );
    if (!(
        (status == STATUS_SUCCESS) ||
        (status == STATUS_BUFFER_OVERFLOW) ||
        (status == STATUS_BUFFER_TOO_SMALL)
      )) {
        DBG("Could not get KVI length for \"%S\": %x\n", ValueName, status);
        goto err_zwqueryvaluekey_len;
      }

    /* Allocate value buffer. */
    *Kvi = wv_malloc(kvi_len);
    if (!*Kvi) {
        DBG("Could not allocate KVI for \"%S\": %x\n", ValueName, status);
        goto err_kvi;
      }

    /* Fetch value. */
    status = ZwQueryValueKey(
        RegKey,
        &u_value_name,
        KeyValuePartialInformation,
        *Kvi,
        kvi_len,
        &kvi_len
      );
    if (!NT_SUCCESS(status)) {
        DBG("Could not get KVI for \"%S\": %x\n", ValueName, status);
        goto err_zwqueryvaluekey;
      }

    return STATUS_SUCCESS;

    err_zwqueryvaluekey:

    wv_free(*Kvi);
    err_kvi:

    err_zwqueryvaluekey_len:

    return status;
  }

/**
 * Fetch registry string value.
 *
 * @v RegKey            Handle for the registry key with the value.
 * @v ValueName         Registry value name.
 * @v Value             String value to allocate and fill in.
 * @ret NTSTATUS        The status of the operation.
 *
 * The caller must eventually free the allocated value.
 */
WVL_M_LIB NTSTATUS STDCALL WvlRegFetchSz(
    HANDLE RegKey,
    LPCWSTR ValueName,
    LPWSTR * Value
  ) {
    PKEY_VALUE_PARTIAL_INFORMATION kvi;
    UINT32 value_len;
    NTSTATUS status;

    /* Fetch key value information. */
    status = WvlRegFetchKvi(RegKey, ValueName, &kvi);
    if (!NT_SUCCESS(status))
      goto err_fetchkvi;

    /* Allocate and populate string. */
    value_len = (kvi->DataLength + sizeof **Value);
    *Value = wv_mallocz(value_len);
    if (!*Value) {
        DBG("Could not allocate value for \"%S\"\n", ValueName);
        status = STATUS_UNSUCCESSFUL;
        goto err_value;
      }
    RtlCopyMemory(*Value, kvi->Data, kvi->DataLength);

    err_value:

    wv_free(kvi);
    err_fetchkvi:

    return status;
  }

/**
 * Fetch registry multiple-string value.
 *
 * @v RegKey            Handle for the registry key with the value.
 * @v ValueName         Registry value name.
 * @v Values            Array of string values to allocate and fill in.
 * @ret NTSTATUS        The status of the operation.
 *
 * The caller must eventually free the allocated value array.
 */
WVL_M_LIB NTSTATUS STDCALL WvlRegFetchMultiSz(
    HANDLE RegKey,
    LPCWSTR ValueName,
    LPWSTR ** Values
  ) {
    PKEY_VALUE_PARTIAL_INFORMATION kvi;
    LPWSTR string;
    UINT32 num_strings;
    UINT32 values_len;
    UINT32 i;
    NTSTATUS status;

    /* Fetch key value information. */
    status = WvlRegFetchKvi(RegKey, ValueName, &kvi);
    if (!NT_SUCCESS(status))
      goto err_fetchkvi;

    /*
     * Count number of strings in the array.  This is a
     * potential(ly harmless) overestimate.
     */
    num_strings = 0;
    for (
        string = ((LPWSTR) kvi->Data);
        string < ((LPWSTR) (kvi->Data + kvi->DataLength));
        string++
      ) {
        if (!*string)
          num_strings++;
      }

    /* Allocate and populate string array. */
    values_len = (
        /* The LPWSTR[] with a NULL terminator. */
        ((num_strings + 1) * sizeof **Values) +
        /* The data. */
        kvi->DataLength +
        /* A null terminator for the data. */
        sizeof ***Values
      );
    *Values = wv_mallocz(values_len);
    if (!*Values) {
        DBG("Could not allocate value array for \"%S\"\n", ValueName);
        status = STATUS_UNSUCCESSFUL;
        goto err_value;
      }
    /* We know that LPWSTR alignment is a multiple of WCHAR alignment. */
    string = ((LPWSTR) (*Values + num_strings + 1));
    /* Copy the data. */
    RtlCopyMemory(string, kvi->Data, kvi->DataLength);
    /* Walk the data, filling the LPWSTR[] with values. */
    for (i = 0; i < num_strings; i++) {
        (*Values)[i] = string;
        while (*string)
          string++;
        while (!*string)
          string++;
      }

    err_value:

    wv_free(kvi);
    err_fetchkvi:

    return status;
  }

/**
 * Fetch registry DWORD value.
 *
 * @v RegKey            Handle for the registry key with the value.
 * @v ValueName         Registry value name.
 * @v Value             DWORD value to fill in.
 * @ret NTSTATUS        The status of the operation.
 */
WVL_M_LIB NTSTATUS STDCALL WvlRegFetchDword(
    IN HANDLE RegKey,
    IN LPCWSTR ValueName,
    OUT UINT32 * Value
  ) {
    PKEY_VALUE_PARTIAL_INFORMATION kvi;
    NTSTATUS status;

    if (!Value) {
        DBG("No DWORD provided.\n");
        status = STATUS_INVALID_PARAMETER;
        goto err_value;
      }

    /* Fetch key value information. */
    status = WvlRegFetchKvi(RegKey, ValueName, &kvi);
    if (!NT_SUCCESS(status))
      goto err_fetchkvi;

    /* Copy the value. */
    if (kvi->DataLength != sizeof *Value) {
        DBG("Registry value is not a DWORD.");
        status = STATUS_INVALID_PARAMETER;
        goto err_datalen;
      }

    RtlCopyMemory(Value, kvi->Data, kvi->DataLength);

    err_datalen:

    wv_free(kvi);
    err_fetchkvi:

    err_value:

    return status;
  }

/**
 * Store registry string value.
 *
 * @v RegKey            Handle for the registry key to store the value in.
 * @v ValueName         Registry value name.
 * @v Value             String value to store.
 * @ret NTSTATUS        The status of the operation.
 */
WVL_M_LIB NTSTATUS STDCALL WvlRegStoreSz(
    HANDLE RegKey,
    LPCWSTR ValueName,
    LPWSTR Value
   ) {
    UNICODE_STRING u_value_name;
    SIZE_T value_len;
    NTSTATUS status;

    RtlInitUnicodeString(&u_value_name, ValueName);
    value_len = ((wcslen(Value) + 1) * sizeof Value[0]);
    status = ZwSetValueKey(
        RegKey,
        &u_value_name,
        0,
        REG_SZ,
        Value,
        (UINT32) value_len
      );
    if (!NT_SUCCESS(status)) {
        DBG("Could not store value \"%S\": %x\n", ValueName, status);
        return status;
      }

    return STATUS_SUCCESS;
  }

/**
 * Store registry DWORD value.
 *
 * @v RegKey            Handle for the registry key to store the value in.
 * @v ValueName         Registry value name.
 * @v Value             DWORD value to store, or NULL.
 * @ret NTSTATUS        The status of the operation.
 */
WVL_M_LIB NTSTATUS STDCALL WvlRegStoreDword(
    HANDLE RegKey,
    LPCWSTR ValueName,
    UINT32 Value
  ) {
    UNICODE_STRING u_value_name;
    NTSTATUS status;

    RtlInitUnicodeString(&u_value_name, ValueName);
    status = ZwSetValueKey(
        RegKey,
        &u_value_name,
        0,
        REG_DWORD,
        &Value,
        sizeof Value
      );
    if (!NT_SUCCESS(status)) {
        DBG("Could not store value \"%S\": %x\n", ValueName, status);
        return status;
      }

    return STATUS_SUCCESS;
  }

/**
 * Note BOOT.INI-/TXTSETUP.SIF-style OsLoadOptions from registry.
 *
 * @v WStr              Pointer to pointer to wide-char string to hold options.
 * @ret NTSTATUS        The status of the operation.
 *
 * The caller must eventually free the wide-char string.
 */
WVL_M_LIB NTSTATUS STDCALL WvlRegNoteOsLoadOpts(LPWSTR * WStr) {
    NTSTATUS status;
    HANDLE control_key;

    /* Open the Control key. */
    status = WvlRegOpenKey(
        L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\",
        &control_key
      );
    if (!NT_SUCCESS(status))
      goto err_keyopen;

    /* Put the SystemStartOptions value into w_str. */
    status = WvlRegFetchSz(control_key, L"SystemStartOptions", WStr);
    if (!NT_SUCCESS(status))
      goto err_fetchsz;

    DBG("OsLoadOptions: %S\n", *WStr);

    err_fetchsz:

    WvlRegCloseKey(control_key);
    err_keyopen:

    return status;
  }
