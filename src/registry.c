/**
 * Copyright (C) 2009, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#include "registry.h"

/** Contains BOOT.INI-style OsLoadOptions parameters */
static LPWSTR Registry_Globals_OsLoadOptions;

/**
 * Open registry key
 *
 * @v reg_key_name  Registry key name
 * @v reg_key       Registry key to fill in
 * @ret ntstatus    NT status
 */
static NTSTATUS
Registry_KeyOpen (
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
static VOID
Registry_KeyClose (
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
static NTSTATUS
Registry_FetchKVI (
	HANDLE reg_key,
	LPCWSTR value_name,
	PKEY_VALUE_PARTIAL_INFORMATION * kvi
 )
{
	UNICODE_STRING u_value_name;
	ULONG kvi_len;
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
static NTSTATUS
Registry_FetchSZ (
	HANDLE reg_key,
	LPCWSTR value_name,
	LPWSTR * value
 )
{
	PKEY_VALUE_PARTIAL_INFORMATION kvi;
	ULONG value_len;
	NTSTATUS status;

	/*
	 * Fetch key value information 
	 */
	status = Registry_FetchKVI ( reg_key, value_name, &kvi );
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
static NTSTATUS
Registry_FetchMultiSZ (
	HANDLE reg_key,
	LPCWSTR value_name,
	LPWSTR ** values
 )
{
	PKEY_VALUE_PARTIAL_INFORMATION kvi;
	LPWSTR string;
	ULONG num_strings;
	ULONG values_len;
	ULONG i;
	NTSTATUS status;

	/*
	 * Fetch key value information 
	 */
	status = Registry_FetchKVI ( reg_key, value_name, &kvi );
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
static NTSTATUS
Registry_StoreSZ (
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
										( ( ULONG ) value_len ) );
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
static NTSTATUS
Registry_StoreDWord (
	HANDLE reg_key,
	LPCWSTR value_name,
	ULONG value
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
 * @ret ntstatus	NT status
 *
 * Somewhere we must eventually free Registry_Globals_OsLoadOptions.
 */
static NTSTATUS STDCALL
Registry_NoteOsLoadOptions (
	VOID
 )
{
	NTSTATUS status;
	HANDLE control_key;

	/*
	 * Open the Control key 
	 */
	status =
		Registry_KeyOpen
		( L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\",
			&control_key );
	if ( !NT_SUCCESS ( status ) )
		goto err_keyopen;

	/*
	 * Put the SystemStartOptions value into a global 
	 */
	status =
		Registry_FetchSZ ( control_key, L"SystemStartOptions",
											 Registry_Globals_OsLoadOptions );
	if ( !NT_SUCCESS ( status ) )
		goto err_fetchsz;

	DBG ( "OsLoadOptions: %S\n", Registry_Globals_OsLoadOptions );

	/*
	 * We do not free this global 
	 */
err_fetchsz:

	Registry_KeyClose ( control_key );
err_keyopen:

	return status;
}

static winvblock__bool STDCALL
Registry_Setup (
	OUT PNTSTATUS status_out
 )
{
	NTSTATUS status;
	winvblock__bool Updated = FALSE;
	WCHAR InterfacesPath[] = L"\\Ndi\\Interfaces\\";
	WCHAR LinkagePath[] = L"\\Linkage\\";
	WCHAR NdiPath[] = L"\\Ndi\\";
	WCHAR DriverServiceNamePath[] =
		L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
	OBJECT_ATTRIBUTES SubKeyObject;
	HANDLE ControlKeyHandle,
	 NetworkClassKeyHandle,
	 SubKeyHandle;
	ULONG i,
	 SubkeyIndex,
	 ResultLength,
	 InterfacesKeyStringLength,
	 LinkageKeyStringLength,
	 NdiKeyStringLength,
	 NewValueLength;
	PWCHAR InterfacesKeyString,
	 LinkageKeyString,
	 NdiKeyString,
	 DriverServiceNameString,
	 NewValue;
	UNICODE_STRING InterfacesKey,
	 LinkageKey,
	 NdiKey,
	 LowerRange,
	 UpperBind,
	 Service,
	 DriverServiceName;
	PKEY_BASIC_INFORMATION KeyInformation;
	PKEY_VALUE_PARTIAL_INFORMATION KeyValueInformation;
	winvblock__bool Update,
	 Found;

	DBG ( "Entry\n" );

	RtlInitUnicodeString ( &LowerRange, L"LowerRange" );
	RtlInitUnicodeString ( &UpperBind, L"UpperBind" );
	RtlInitUnicodeString ( &Service, L"Service" );

	/*
	 * Open the network adapter class key 
	 */
	status =
		Registry_KeyOpen ( ( L"\\Registry\\Machine\\SYSTEM\\"
												 L"CurrentControlSet\\Control\\Class\\"
												 L"{4D36E972-E325-11CE-BFC1-08002BE10318}\\" ),
											 &NetworkClassKeyHandle );
	if ( !NT_SUCCESS ( status ) )
		goto err_keyopennetworkclass;

	/*
	 * Enumerate through subkeys 
	 */
	SubkeyIndex = 0;
	while ( ( status =
						ZwEnumerateKey ( NetworkClassKeyHandle, SubkeyIndex,
														 KeyBasicInformation, NULL, 0,
														 &ResultLength ) ) != STATUS_NO_MORE_ENTRIES )
		{
			if ( ( status != STATUS_SUCCESS ) && ( status != STATUS_BUFFER_OVERFLOW )
					 && ( status != STATUS_BUFFER_TOO_SMALL ) )
				{
					DBG ( "ZwEnumerateKey 1 failed (%lx)\n", status );
					goto e0_1;
				}
			if ( ( KeyInformation =
						 ( PKEY_BASIC_INFORMATION ) ExAllocatePool ( NonPagedPool,
																												 ResultLength ) ) ==
					 NULL )
				{
					DBG ( "ExAllocatePool KeyData failed\n" );
					goto e0_1;
					Registry_KeyClose ( NetworkClassKeyHandle );
				}
			if ( !
					 ( NT_SUCCESS
						 ( ZwEnumerateKey
							 ( NetworkClassKeyHandle, SubkeyIndex, KeyBasicInformation,
								 KeyInformation, ResultLength, &ResultLength ) ) ) )
				{
					DBG ( "ZwEnumerateKey 2 failed\n" );
					goto e0_2;
				}

			InterfacesKeyStringLength =
				KeyInformation->NameLength + sizeof ( InterfacesPath );
			if ( ( InterfacesKeyString =
						 ( PWCHAR ) ExAllocatePool ( NonPagedPool,
																				 InterfacesKeyStringLength ) ) ==
					 NULL )
				{
					DBG ( "ExAllocatePool InterfacesKeyString failed\n" );
					goto e0_2;
				}

			RtlCopyMemory ( InterfacesKeyString, KeyInformation->Name,
											KeyInformation->NameLength );
			RtlCopyMemory ( &InterfacesKeyString
											[( KeyInformation->NameLength / sizeof ( WCHAR ) )],
											InterfacesPath, sizeof ( InterfacesPath ) );
			RtlInitUnicodeString ( &InterfacesKey, InterfacesKeyString );

			Update = FALSE;
			InitializeObjectAttributes ( &SubKeyObject, &InterfacesKey,
																	 OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
																	 NetworkClassKeyHandle, NULL );
			if ( NT_SUCCESS
					 ( ZwOpenKey ( &SubKeyHandle, KEY_ALL_ACCESS, &SubKeyObject ) ) )
				{
					if ( ( status =
								 ZwQueryValueKey ( SubKeyHandle, &LowerRange,
																	 KeyValuePartialInformation, NULL, 0,
																	 &ResultLength ) ) !=
							 STATUS_OBJECT_NAME_NOT_FOUND )
						{
							if ( ( status != STATUS_SUCCESS )
									 && ( status != STATUS_BUFFER_OVERFLOW )
									 && ( status != STATUS_BUFFER_TOO_SMALL ) )
								{
									DBG ( "ZwQueryValueKey InterfacesKey 1 failed (%lx)\n",
												status );
									goto e1_1;
								}
							if ( ( KeyValueInformation =
										 ( PKEY_VALUE_PARTIAL_INFORMATION )
										 ExAllocatePool ( NonPagedPool, ResultLength ) ) == NULL )
								{
									DBG ( "ExAllocatePool InterfacesKey "
												"KeyValueData failed\n" );
									goto e1_1;
								}
							if ( !
									 ( NT_SUCCESS
										 ( ZwQueryValueKey
											 ( SubKeyHandle, &LowerRange, KeyValuePartialInformation,
												 KeyValueInformation, ResultLength,
												 &ResultLength ) ) ) )
								{
									DBG ( "ZwQueryValueKey InterfacesKey 2 failed\n" );
									goto e1_2;
								}
							if ( RtlCompareMemory
									 ( L"ethernet", KeyValueInformation->Data,
										 sizeof ( L"ethernet" ) ) == sizeof ( L"ethernet" ) )
								Update = TRUE;
							ExFreePool ( KeyValueInformation );
							if ( !NT_SUCCESS ( ZwClose ( SubKeyHandle ) ) )
								{
									DBG ( "ZwClose InterfacesKey SubKeyHandle failed\n" );
									goto e1_0;
								}
						}
				}
			ExFreePool ( InterfacesKeyString );

			if ( Update )
				{
					LinkageKeyStringLength =
						KeyInformation->NameLength + sizeof ( LinkagePath );
					if ( ( LinkageKeyString =
								 ( PWCHAR ) ExAllocatePool ( NonPagedPool,
																						 LinkageKeyStringLength ) ) ==
							 NULL )
						{
							DBG ( "ExAllocatePool LinkageKeyString failed\n" );
							goto e0_2;
						}
					RtlCopyMemory ( LinkageKeyString, KeyInformation->Name,
													KeyInformation->NameLength );
					RtlCopyMemory ( &LinkageKeyString
													[( KeyInformation->NameLength / sizeof ( WCHAR ) )],
													LinkagePath, sizeof ( LinkagePath ) );
					RtlInitUnicodeString ( &LinkageKey, LinkageKeyString );

					InitializeObjectAttributes ( &SubKeyObject, &LinkageKey,
																			 OBJ_KERNEL_HANDLE |
																			 OBJ_CASE_INSENSITIVE,
																			 NetworkClassKeyHandle, NULL );
					if ( !NT_SUCCESS
							 ( ZwCreateKey
								 ( &SubKeyHandle, KEY_ALL_ACCESS, &SubKeyObject, 0, NULL,
									 REG_OPTION_NON_VOLATILE, NULL ) ) )
						{
							DBG ( "ZwCreateKey failed (%lx)\n" );
							goto e2_0;
						}
					if ( ( status =
								 ZwQueryValueKey ( SubKeyHandle, &UpperBind,
																	 KeyValuePartialInformation, NULL, 0,
																	 &ResultLength ) ) !=
							 STATUS_OBJECT_NAME_NOT_FOUND )
						{;
							if ( ( status != STATUS_SUCCESS )
									 && ( status != STATUS_BUFFER_OVERFLOW )
									 && ( status != STATUS_BUFFER_TOO_SMALL ) )
								{
									DBG ( "ZwQueryValueKey LinkageKey 1 failed (%lx)\n",
												status );
									goto e2_1;
								}
							if ( ( KeyValueInformation =
										 ( PKEY_VALUE_PARTIAL_INFORMATION )
										 ExAllocatePool ( NonPagedPool, ResultLength ) ) == NULL )
								{
									DBG ( "ExAllocatePool LinkageKey KeyValueData failed\n" );
									goto e2_1;
								}
							if ( !
									 ( NT_SUCCESS
										 ( ZwQueryValueKey
											 ( SubKeyHandle, &UpperBind, KeyValuePartialInformation,
												 KeyValueInformation, ResultLength,
												 &ResultLength ) ) ) )
								{
									DBG ( "ZwQueryValueKey LinkageKey 2 failed\n" );
									goto e2_2;
								}

							Found = FALSE;
							for ( i = 0;
										i <
										( KeyValueInformation->DataLength -
											sizeof ( L"WinVBlock" ) / sizeof ( WCHAR ) ); i++ )
								{
									if ( RtlCompareMemory
											 ( L"WinVBlock",
												 &( ( ( PWCHAR ) KeyValueInformation->Data )[i] ),
												 sizeof ( L"WinVBlock" ) ) == sizeof ( L"WinVBlock" ) )
										{
											Found = TRUE;
											break;
										}
								}

							if ( Found )
								{
									NewValueLength = KeyValueInformation->DataLength;
									if ( ( NewValue =
												 ( PWCHAR ) ExAllocatePool ( NonPagedPool,
																										 NewValueLength ) ) ==
											 NULL )
										{
											DBG ( "ExAllocatePool NewValue 1 failed\n" );
											goto e2_2;
										}
									RtlCopyMemory ( NewValue, KeyValueInformation->Data,
																	KeyValueInformation->DataLength );
								}
							else
								{
									Updated = TRUE;
									NewValueLength =
										KeyValueInformation->DataLength + sizeof ( L"WinVBlock" );
									if ( ( NewValue =
												 ( PWCHAR ) ExAllocatePool ( NonPagedPool,
																										 NewValueLength ) ) ==
											 NULL )
										{
											DBG ( "ExAllocatePool NewValue 2 failed\n" );
											goto e2_2;
										}
									RtlCopyMemory ( NewValue, L"WinVBlock",
																	sizeof ( L"WinVBlock" ) );
									RtlCopyMemory ( &NewValue
																	[( sizeof ( L"WinVBlock" ) /
																		 sizeof ( WCHAR ) )],
																	KeyValueInformation->Data,
																	KeyValueInformation->DataLength );
								}
							ExFreePool ( KeyValueInformation );
						}
					else
						{
							Updated = TRUE;
							NewValueLength = sizeof ( L"WinVBlock" ) + sizeof ( WCHAR );
							if ( ( NewValue =
										 ( PWCHAR ) ExAllocatePool ( NonPagedPool,
																								 NewValueLength ) ) == NULL )
								{
									DBG ( "ExAllocatePool NewValue 3 failed\n" );
									goto e2_1;
								}
							RtlZeroMemory ( NewValue, NewValueLength );
							RtlCopyMemory ( NewValue, L"WinVBlock",
															sizeof ( L"WinVBlock" ) );
						}
					if ( !NT_SUCCESS
							 ( ZwSetValueKey
								 ( SubKeyHandle, &UpperBind, 0, REG_MULTI_SZ, NewValue,
									 NewValueLength ) ) )
						{
							DBG ( "ZwSetValueKey failed\n" );
							ExFreePool ( NewValue );
							goto e2_1;
						}
					ExFreePool ( NewValue );
					if ( !NT_SUCCESS ( ZwClose ( SubKeyHandle ) ) )
						{
							DBG ( "ZwClose LinkageKey SubKeyHandle failed\n" );
							goto e2_0;
						}
					ExFreePool ( LinkageKeyString );

/* start nic (
 */
					NdiKeyStringLength = KeyInformation->NameLength + sizeof ( NdiPath );
					if ( ( NdiKeyString =
								 ( PWCHAR ) ExAllocatePool ( NonPagedPool,
																						 NdiKeyStringLength ) ) == NULL )
						{
							DBG ( "ExAllocatePool NdiKeyString failed\n" );
							goto e0_2;
						}
					RtlCopyMemory ( NdiKeyString, KeyInformation->Name,
													KeyInformation->NameLength );
					RtlCopyMemory ( &NdiKeyString
													[( KeyInformation->NameLength / sizeof ( WCHAR ) )],
													NdiPath, sizeof ( NdiPath ) );
					RtlInitUnicodeString ( &NdiKey, NdiKeyString );

					InitializeObjectAttributes ( &SubKeyObject, &NdiKey,
																			 OBJ_KERNEL_HANDLE |
																			 OBJ_CASE_INSENSITIVE,
																			 NetworkClassKeyHandle, NULL );
					if ( NT_SUCCESS
							 ( ZwOpenKey ( &SubKeyHandle, KEY_ALL_ACCESS, &SubKeyObject ) ) )
						{
							if ( ( status =
										 ZwQueryValueKey ( SubKeyHandle, &Service,
																			 KeyValuePartialInformation, NULL, 0,
																			 &ResultLength ) ) !=
									 STATUS_OBJECT_NAME_NOT_FOUND )
								{
									if ( ( status != STATUS_SUCCESS )
											 && ( status != STATUS_BUFFER_OVERFLOW )
											 && ( status != STATUS_BUFFER_TOO_SMALL ) )
										{
											DBG ( "ZwQueryValueKey NdiKey 1 failed (%lx)\n",
														status );
											goto e3_1;
										}
									if ( ( KeyValueInformation =
												 ( PKEY_VALUE_PARTIAL_INFORMATION )
												 ExAllocatePool ( NonPagedPool,
																					ResultLength ) ) == NULL )
										{
											DBG ( "ExAllocatePool NdiKey KeyValueData failed\n" );
											goto e3_1;
										}
									if ( !
											 ( NT_SUCCESS
												 ( ZwQueryValueKey
													 ( SubKeyHandle, &Service,
														 KeyValuePartialInformation, KeyValueInformation,
														 ResultLength, &ResultLength ) ) ) )
										{
											DBG ( "ZwQueryValueKey NdiKey 2 failed\n" );
											ExFreePool ( KeyValueInformation );
											goto e3_1;
										}
									if ( !NT_SUCCESS ( ZwClose ( SubKeyHandle ) ) )
										{
											DBG ( "ZwClose NdiKey SubKeyHandle failed\n" );
											goto e3_0;
										}
									if ( ( DriverServiceNameString =
												 ( PWCHAR ) ExAllocatePool ( NonPagedPool,
																										 sizeof
																										 ( DriverServiceNamePath )
																										 +
																										 KeyValueInformation->
																										 DataLength -
																										 sizeof ( WCHAR ) ) ) ==
											 NULL )
										{
											DBG ( "ExAllocatePool DriverServiceNameString "
														"failed\n" );
											goto e3_0;
										}

									RtlCopyMemory ( DriverServiceNameString,
																	DriverServiceNamePath,
																	sizeof ( DriverServiceNamePath ) );
									RtlCopyMemory ( &DriverServiceNameString
																	[( sizeof ( DriverServiceNamePath ) /
																		 sizeof ( WCHAR ) ) - 1],
																	KeyValueInformation->Data,
																	KeyValueInformation->DataLength );
									RtlInitUnicodeString ( &DriverServiceName,
																				 DriverServiceNameString );
/*          DBG("Starting driver %S -> %08x\n", KeyValueInformation->Data,
 *              ZwLoadDriver(&DriverServiceName));
 */
									ExFreePool ( DriverServiceNameString );
									ExFreePool ( KeyValueInformation );
								}
						}
					ExFreePool ( NdiKeyString );
				}
			ExFreePool ( KeyInformation );
			SubkeyIndex++;
		}
	Registry_KeyClose ( NetworkClassKeyHandle );
	*status_out = STATUS_SUCCESS;
	return Updated;

e3_1:

	if ( !NT_SUCCESS ( ZwClose ( SubKeyHandle ) ) )
		DBG ( "ZwClose SubKeyHandle failed\n" );
e3_0:

	ExFreePool ( NdiKeyString );
	goto e0_2;
e2_2:

	ExFreePool ( KeyValueInformation );
e2_1:

	if ( !NT_SUCCESS ( ZwClose ( SubKeyHandle ) ) )
		DBG ( "ZwClose SubKeyHandle failed\n" );
e2_0:

	ExFreePool ( LinkageKeyString );
	goto e0_2;
e1_2:

	ExFreePool ( KeyValueInformation );
e1_1:

	if ( !NT_SUCCESS ( ZwClose ( SubKeyHandle ) ) )
		DBG ( "ZwClose SubKeyHandle failed\n" );
e1_0:

	ExFreePool ( InterfacesKeyString );
	goto e0_2;
e0_2:

	ExFreePool ( KeyInformation );
e0_1:

	Registry_KeyClose ( NetworkClassKeyHandle );
err_keyopennetworkclass:

	*status_out = STATUS_UNSUCCESSFUL;
	return FALSE;
}

/**
 * Check registry for parameters and update if needed
 *
 * @ret ntstatus  NT status
 */
NTSTATUS STDCALL
Registry_Check (
	VOID
 )
{
	NTSTATUS status;

	if ( Registry_Setup ( &status ) )
		{
			DBG ( "Registry updated\n" );
		}
	return status;
}
