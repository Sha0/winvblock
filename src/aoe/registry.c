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

/**
 * @file
 *
 * AoE registry specifics.
 */

#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "wv_string.h"
#include "registry.h"
#include "debug.h"

BOOLEAN STDCALL AoeRegSetup(OUT PNTSTATUS status_out) {
    NTSTATUS status;
    BOOLEAN Updated = FALSE;
    WCHAR InterfacesPath[] = L"\\Ndi\\Interfaces\\";
    WCHAR LinkagePath[] = L"\\Linkage\\";
    WCHAR NdiPath[] = L"\\Ndi\\";
    WCHAR DriverServiceNamePath[] =
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
    OBJECT_ATTRIBUTES SubKeyObject;
    HANDLE
      ControlKeyHandle, NetworkClassKeyHandle, SubKeyHandle;
    UINT32
      i, SubkeyIndex, ResultLength, InterfacesKeyStringLength,
      LinkageKeyStringLength, NdiKeyStringLength, NewValueLength;
    PWCHAR
      InterfacesKeyString, LinkageKeyString, NdiKeyString,
      DriverServiceNameString, NewValue;
    UNICODE_STRING
      InterfacesKey, LinkageKey, NdiKey, LowerRange, UpperBind, Service,
      DriverServiceName;
    PKEY_BASIC_INFORMATION KeyInformation;
    PKEY_VALUE_PARTIAL_INFORMATION KeyValueInformation;
    BOOLEAN Update, Found;

    DBG("Entry\n");

    RtlInitUnicodeString(&LowerRange, L"LowerRange");
    RtlInitUnicodeString(&UpperBind, L"UpperBind");
    RtlInitUnicodeString(&Service, L"Service");

    /* Open the network adapter class key. */
    status = WvlRegOpenKey(
        (L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\"
          L"{4D36E972-E325-11CE-BFC1-08002BE10318}\\"),
        &NetworkClassKeyHandle
      );
    if (!NT_SUCCESS(status))
      goto err_keyopennetworkclass;

    /* Enumerate through subkeys. */
    SubkeyIndex = 0;
    while ((status = ZwEnumerateKey(
        NetworkClassKeyHandle,
        SubkeyIndex,
        KeyBasicInformation,
        NULL,
        0,
        &ResultLength
      )) != STATUS_NO_MORE_ENTRIES)
      {
        if ((status != STATUS_SUCCESS) &&
            (status != STATUS_BUFFER_OVERFLOW) &&
            (status != STATUS_BUFFER_TOO_SMALL)
          )
          {
            DBG("ZwEnumerateKey 1 failed (%lx)\n", status);
            goto e0_1;
          }
        if ((KeyInformation = wv_malloc(ResultLength)) == NULL)
          {
            DBG("wv_malloc KeyData failed\n");
            goto e0_1;
            WvlRegCloseKey(NetworkClassKeyHandle);
          }
        if (!(NT_SUCCESS(
            ZwEnumerateKey(
                NetworkClassKeyHandle,
                SubkeyIndex,
                KeyBasicInformation,
                KeyInformation,
                ResultLength,
                &ResultLength
          ))))
          {
            DBG ("ZwEnumerateKey 2 failed\n");
            goto e0_2;
          }

        InterfacesKeyStringLength =
          KeyInformation->NameLength + sizeof InterfacesPath;
        InterfacesKeyString = wv_malloc(InterfacesKeyStringLength);
        if (InterfacesKeyString == NULL)
          {
            DBG("wv_malloc InterfacesKeyString failed\n");
            goto e0_2;
          }

        RtlCopyMemory(
            InterfacesKeyString,
            KeyInformation->Name,
            KeyInformation->NameLength
          );
        RtlCopyMemory(
            InterfacesKeyString +
              (KeyInformation->NameLength / sizeof (WCHAR)),
            InterfacesPath,
            sizeof InterfacesPath
          );
        RtlInitUnicodeString(&InterfacesKey, InterfacesKeyString);

        Update = FALSE;
        InitializeObjectAttributes(
            &SubKeyObject,
            &InterfacesKey,
            OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
            NetworkClassKeyHandle,
            NULL
          );
        if (NT_SUCCESS(ZwOpenKey(&SubKeyHandle, KEY_ALL_ACCESS, &SubKeyObject)))
          {
            if ((status = ZwQueryValueKey(
                SubKeyHandle,
                &LowerRange,
                KeyValuePartialInformation,
                NULL,
                0,
                &ResultLength
              )) != STATUS_OBJECT_NAME_NOT_FOUND )
              {
                if ((status != STATUS_SUCCESS) &&
                    (status != STATUS_BUFFER_OVERFLOW) &&
                    (status != STATUS_BUFFER_TOO_SMALL))
                  {
                    DBG(
                        "ZwQueryValueKey InterfacesKey 1 failed (%lx)\n",
                        status
                      );
                    goto e1_1;
                  }
                if ((KeyValueInformation = wv_malloc(ResultLength)) == NULL)
                  {
                    DBG("wv_malloc InterfacesKey KeyValueData failed\n");
                    goto e1_1;
                  }
                if (!(NT_SUCCESS(ZwQueryValueKey(
                    SubKeyHandle,
                    &LowerRange,
                    KeyValuePartialInformation,
                    KeyValueInformation,
                    ResultLength,
                    &ResultLength
                  ))))
                  {
                    DBG("ZwQueryValueKey InterfacesKey 2 failed\n");
                    goto e1_2;
                  }
                if (wv_memcmpeq(
                    L"ethernet",
                    KeyValueInformation->Data,
                    sizeof L"ethernet"
                  ))
                  Update = TRUE;
                wv_free(KeyValueInformation);
                if (!NT_SUCCESS(ZwClose(SubKeyHandle)))
                  {
                    DBG("ZwClose InterfacesKey SubKeyHandle failed\n");
                    goto e1_0;
                  }
               }
          }
        wv_free(InterfacesKeyString);

        if (Update)
          {
            LinkageKeyStringLength =
              KeyInformation->NameLength + sizeof LinkagePath;
            if ((LinkageKeyString = wv_malloc(LinkageKeyStringLength)) == NULL)
              {
                DBG("wv_malloc LinkageKeyString failed\n");
                goto e0_2;
              }
            RtlCopyMemory(
                LinkageKeyString,
                KeyInformation->Name,
                KeyInformation->NameLength
              );
            RtlCopyMemory(
                LinkageKeyString +
                (KeyInformation->NameLength / sizeof (WCHAR)),
                LinkagePath,
                sizeof LinkagePath
              );
            RtlInitUnicodeString(&LinkageKey, LinkageKeyString);

            InitializeObjectAttributes(
                &SubKeyObject,
                &LinkageKey,
                OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                NetworkClassKeyHandle,
                NULL
              );
            if (!NT_SUCCESS(ZwCreateKey(
                &SubKeyHandle,
                KEY_ALL_ACCESS,
                &SubKeyObject,
                0,
                NULL,
                 REG_OPTION_NON_VOLATILE,
                NULL
              )))
              {
                DBG("ZwCreateKey failed (%lx)\n");
                goto e2_0;
              }
            if ((status = ZwQueryValueKey(
                SubKeyHandle,
                &UpperBind,
                KeyValuePartialInformation,
                NULL,
                0,
                &ResultLength
              )) != STATUS_OBJECT_NAME_NOT_FOUND)
              {
                if ((status != STATUS_SUCCESS) &&
                    (status != STATUS_BUFFER_OVERFLOW) &&
                    (status != STATUS_BUFFER_TOO_SMALL))
                  {
                    DBG("ZwQueryValueKey LinkageKey 1 failed (%lx)\n", status);
                    goto e2_1;
                  }
                if ((KeyValueInformation = wv_malloc(ResultLength)) == NULL)
                  {
                    DBG("wv_malloc LinkageKey KeyValueData failed\n");
                    goto e2_1;
                  }
                if (!(NT_SUCCESS(ZwQueryValueKey(
                    SubKeyHandle,
                    &UpperBind,
                    KeyValuePartialInformation,
                    KeyValueInformation,
                    ResultLength,
                    &ResultLength
                  ))))
                   {
                    DBG("ZwQueryValueKey LinkageKey 2 failed\n");
                    goto e2_2;
                  }

                Found = FALSE;
                for (i = 0;
                    i < (KeyValueInformation->DataLength -
                      sizeof WVL_M_WLIT / sizeof (WCHAR));
                    i++)
                  {
                    if (wv_memcmpeq(
                        WVL_M_WLIT,
                        ((PWCHAR)KeyValueInformation->Data) + i,
                        sizeof WVL_M_WLIT
                      ))
                      {
                        Found = TRUE;
                        break;
                      } /* if wv_memcmpeq */
                  } /* for */

                if (Found)
                  {
                    NewValueLength = KeyValueInformation->DataLength;
                    if ((NewValue = wv_malloc(NewValueLength)) == NULL)
                      {
                        DBG("wv_malloc NewValue 1 failed\n");
                        goto e2_2;
                      }
                    RtlCopyMemory(
                        NewValue,
                        KeyValueInformation->Data,
                        KeyValueInformation->DataLength
                      );
                  } /* if Found */
                  else
                  {
                    Updated = TRUE;
                    NewValueLength = KeyValueInformation->DataLength +
                      sizeof WVL_M_WLIT;
                    if ((NewValue = wv_malloc(NewValueLength)) == NULL)
                      {
                        DBG("wv_malloc NewValue 2 failed\n");
                        goto e2_2;
                      }
                    RtlCopyMemory(NewValue, WVL_M_WLIT, sizeof WVL_M_WLIT);
                    RtlCopyMemory(
                        NewValue +
                          (sizeof WVL_M_WLIT / sizeof (WCHAR)),
                        KeyValueInformation->Data,
                        KeyValueInformation->DataLength
                      );
                     } /* else !Found */
                wv_free(KeyValueInformation);
              }
              else
              {
                Updated = TRUE;
                NewValueLength = sizeof WVL_M_WLIT + sizeof (WCHAR);
                if ((NewValue = wv_mallocz(NewValueLength)) == NULL)
                  {
                    DBG("wv_mallocz NewValue 3 failed\n");
                    goto e2_1;
                  }
                RtlCopyMemory(NewValue, WVL_M_WLIT, sizeof WVL_M_WLIT);
              }
            if (!NT_SUCCESS(ZwSetValueKey(
                SubKeyHandle,
                &UpperBind,
                0,
                REG_MULTI_SZ,
                NewValue,
                NewValueLength
              )))
              {
                DBG("ZwSetValueKey failed\n");
                wv_free(NewValue);
                goto e2_1;
              }
            wv_free(NewValue);
            if (!NT_SUCCESS(ZwClose(SubKeyHandle)))
              {
                DBG("ZwClose LinkageKey SubKeyHandle failed\n");
                goto e2_0;
              }
            wv_free(LinkageKeyString);

            /* Not sure where this comes from. */
            #if 0
            start nic (
            #endif
            NdiKeyStringLength = KeyInformation->NameLength + sizeof NdiPath;
            if ((NdiKeyString = wv_malloc(NdiKeyStringLength)) == NULL)
              {
                DBG("wv_malloc NdiKeyString failed\n");
                goto e0_2;
              }
            RtlCopyMemory(
                NdiKeyString,
                KeyInformation->Name,
                KeyInformation->NameLength
              );
            RtlCopyMemory(
                NdiKeyString + (KeyInformation->NameLength / sizeof (WCHAR)),
                NdiPath,
                sizeof NdiPath
              );
            RtlInitUnicodeString(&NdiKey, NdiKeyString);

            InitializeObjectAttributes(
                &SubKeyObject,
                &NdiKey,
                OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                NetworkClassKeyHandle,
                NULL
              );
            if (NT_SUCCESS(ZwOpenKey(
                &SubKeyHandle,
                KEY_ALL_ACCESS,
                &SubKeyObject
              )))
              {
                if ((status = ZwQueryValueKey(
                    SubKeyHandle,
                    &Service,
                    KeyValuePartialInformation,
                    NULL,
                    0,
                    &ResultLength
                  )) != STATUS_OBJECT_NAME_NOT_FOUND)
                  {
                  if ((status != STATUS_SUCCESS) &&
                      (status != STATUS_BUFFER_OVERFLOW) &&
                      (status != STATUS_BUFFER_TOO_SMALL))
                    {
                      DBG("ZwQueryValueKey NdiKey 1 failed (%lx)\n", status);
                      goto e3_1;
                    }
                  if ((KeyValueInformation = wv_malloc(ResultLength)) == NULL)
                    {
                      DBG("wv_malloc NdiKey KeyValueData failed\n");
                      goto e3_1;
                    }
                  if (!(NT_SUCCESS(ZwQueryValueKey(
                      SubKeyHandle,
                      &Service,
                      KeyValuePartialInformation,
                      KeyValueInformation,
                      ResultLength,
                      &ResultLength
                    ))))
                    {
                      DBG("ZwQueryValueKey NdiKey 2 failed\n");
                      wv_free(KeyValueInformation);
                      goto e3_1;
                    }
                  if (!NT_SUCCESS(ZwClose(SubKeyHandle)))
                    {
                      DBG("ZwClose NdiKey SubKeyHandle failed\n");
                      goto e3_0;
                    }
                  DriverServiceNameString = wv_malloc(
                      sizeof DriverServiceNamePath +
                        KeyValueInformation->DataLength -
                        sizeof *DriverServiceNamePath
                    );
                  if (DriverServiceNameString == NULL)
                    {
                      DBG("wv_malloc DriverServiceNameString failed\n");
                      goto e3_0;
                    }

                  RtlCopyMemory(
                      DriverServiceNameString,
                      DriverServiceNamePath,
                      sizeof DriverServiceNamePath
                    );
                  RtlCopyMemory(
                      DriverServiceNameString +
                        (sizeof DriverServiceNamePath / sizeof (WCHAR)) - 1,
                      KeyValueInformation->Data,
                      KeyValueInformation->DataLength
                    );
                  RtlInitUnicodeString(
                      &DriverServiceName,
                      DriverServiceNameString
                    );
                  #if 0
                  DBG(
                      "Starting driver %S -> %08x\n",
                      KeyValueInformation->Data,
                      ZwLoadDriver(&DriverServiceName)
                    );
                  #endif
                    wv_free(DriverServiceNameString);
                    wv_free(KeyValueInformation);
                  }
              }
            wv_free(NdiKeyString);
          }
        wv_free(KeyInformation);
        SubkeyIndex++;
      } /* while */
    WvlRegCloseKey(NetworkClassKeyHandle);
    *status_out = STATUS_SUCCESS;
    return Updated;

    e3_1:

    if (!NT_SUCCESS(ZwClose(SubKeyHandle)))
      DBG("ZwClose SubKeyHandle failed\n");
    e3_0:

    wv_free(NdiKeyString);
    goto e0_2;
    e2_2:

    wv_free(KeyValueInformation);
    e2_1:

    if (!NT_SUCCESS(ZwClose(SubKeyHandle)))
      DBG("ZwClose SubKeyHandle failed\n");
    e2_0:

    wv_free(LinkageKeyString);
    goto e0_2;
    e1_2:

    wv_free(KeyValueInformation);
    e1_1:

    if (!NT_SUCCESS(ZwClose(SubKeyHandle)))
      DBG("ZwClose SubKeyHandle failed\n");
    e1_0:

    wv_free(InterfacesKeyString);
    goto e0_2;
    e0_2:

    wv_free(KeyInformation);
    e0_1:

    WvlRegCloseKey(NetworkClassKeyHandle);
    err_keyopennetworkclass:

    *status_out = STATUS_UNSUCCESSFUL;
    return FALSE;
  }
