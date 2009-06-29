/*
  Copyright 2006-2008, V.
  For contact information, see http://winaoe.org/

  This file is part of WinAoE.

  WinAoE is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  WinAoE is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with WinAoE.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "portable.h"
#include <ntddk.h>

// in this file
BOOLEAN STDCALL SetupRegistry(OUT PNTSTATUS StatusOut);

NTSTATUS STDCALL CheckRegistry() {
  NTSTATUS Status;

  if (SetupRegistry(&Status)) {
    DbgPrint("Registry updated\n");
  }
  return Status;
}

BOOLEAN STDCALL SetupRegistry(OUT PNTSTATUS StatusOut) {
  NTSTATUS Status;
  BOOLEAN Updated = FALSE;
  WCHAR InterfacesPath[] = L"\\Ndi\\Interfaces\\";
  WCHAR LinkagePath[] = L"\\Linkage\\";
  WCHAR NdiPath[] = L"\\Ndi\\";
  WCHAR DriverServiceNamePath[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
  OBJECT_ATTRIBUTES NetworkClassKeyObject, SubKeyObject;
  HANDLE NetworkClassKeyHandle, SubKeyHandle;
  ULONG i, SubkeyIndex, ResultLength, InterfacesKeyStringLength, LinkageKeyStringLength, NdiKeyStringLength, NewValueLength;
  PWCHAR InterfacesKeyString, LinkageKeyString, NdiKeyString, DriverServiceNameString, NewValue;
  UNICODE_STRING NetworkClassKey, InterfacesKey, LinkageKey, NdiKey, LowerRange, UpperBind, Service, DriverServiceName;
  PKEY_BASIC_INFORMATION KeyInformation;
  PKEY_VALUE_PARTIAL_INFORMATION KeyValueInformation;
  BOOLEAN Update, Found;

  DbgPrint("SetupRegistry\n");
  RtlInitUnicodeString(&NetworkClassKey, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\");
  RtlInitUnicodeString(&LowerRange, L"LowerRange");
  RtlInitUnicodeString(&UpperBind, L"UpperBind");
  RtlInitUnicodeString(&Service, L"Service");

  InitializeObjectAttributes(&NetworkClassKeyObject, &NetworkClassKey, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
  if (!NT_SUCCESS(ZwOpenKey(&NetworkClassKeyHandle, KEY_ALL_ACCESS, &NetworkClassKeyObject))) {
    DbgPrint("Failed to open Network Class key\n");
    goto e0_0;
  }

  SubkeyIndex = 0;
  while ((Status = ZwEnumerateKey(NetworkClassKeyHandle, SubkeyIndex, KeyBasicInformation, NULL, 0, &ResultLength)) != STATUS_NO_MORE_ENTRIES) {
    if ((Status != STATUS_SUCCESS) && (Status != STATUS_BUFFER_OVERFLOW) && (Status != STATUS_BUFFER_TOO_SMALL)) {
      DbgPrint("ZwEnumerateKey 1 failed in SetupRegistry (%lx)\n", Status);
      goto e0_1;
    }
    if ((KeyInformation = (PKEY_BASIC_INFORMATION)ExAllocatePool(NonPagedPool, ResultLength)) == NULL) {
      DbgPrint("ExAllocatePool KeyData failed in SetupRegistry\n");
      goto e0_1;
      if (!NT_SUCCESS(ZwClose(NetworkClassKeyHandle))) DbgPrint("ZwClose NetworkClassKeyHandle failed in SetupRegistry\n");
    }
    if (!(NT_SUCCESS(ZwEnumerateKey(NetworkClassKeyHandle, SubkeyIndex, KeyBasicInformation, KeyInformation, ResultLength, &ResultLength)))) {
      DbgPrint("ZwEnumerateKey 2 failed in SetupRegistry\n");
      goto e0_2;
    }

    InterfacesKeyStringLength = KeyInformation->NameLength + sizeof(InterfacesPath);
    if ((InterfacesKeyString = (PWCHAR)ExAllocatePool(NonPagedPool, InterfacesKeyStringLength)) == NULL) {
      DbgPrint("ExAllocatePool InterfacesKeyString failed in SetupRegistry\n");
      goto e0_2;
    }

    RtlCopyMemory(InterfacesKeyString, KeyInformation->Name, KeyInformation->NameLength);
    RtlCopyMemory(&InterfacesKeyString[(KeyInformation->NameLength / sizeof(WCHAR))], InterfacesPath, sizeof(InterfacesPath));
    RtlInitUnicodeString(&InterfacesKey, InterfacesKeyString);

    Update = FALSE;
    InitializeObjectAttributes(&SubKeyObject, &InterfacesKey, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NetworkClassKeyHandle, NULL);
    if (NT_SUCCESS(ZwOpenKey(&SubKeyHandle, KEY_ALL_ACCESS, &SubKeyObject))) {
      if ((Status = ZwQueryValueKey(SubKeyHandle, &LowerRange, KeyValuePartialInformation, NULL, 0, &ResultLength)) != STATUS_OBJECT_NAME_NOT_FOUND) {
        if ((Status != STATUS_SUCCESS) && (Status != STATUS_BUFFER_OVERFLOW) && (Status != STATUS_BUFFER_TOO_SMALL)) {
          DbgPrint("ZwQueryValueKey InterfacesKey 1 failed in SetupRegistry (%lx)\n", Status);
          goto e1_1;
        }
        if ((KeyValueInformation = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePool(NonPagedPool, ResultLength)) == NULL) {
          DbgPrint("ExAllocatePool InterfacesKey KeyValueData failed in SetupRegistry\n");
          goto e1_1;
        }
        if (!(NT_SUCCESS(ZwQueryValueKey(SubKeyHandle, &LowerRange, KeyValuePartialInformation, KeyValueInformation, ResultLength, &ResultLength)))) {
          DbgPrint("ZwQueryValueKey InterfacesKey 2 failed in SetupRegistry\n");
          goto e1_2;
        }
        if (RtlCompareMemory(L"ethernet", KeyValueInformation->Data, sizeof(L"ethernet")) == sizeof(L"ethernet")) Update = TRUE;
        ExFreePool(KeyValueInformation);
        if (!NT_SUCCESS(ZwClose(SubKeyHandle))) {
          DbgPrint("ZwClose InterfacesKey SubKeyHandle failed in SetupRegistry\n");
          goto e1_0;
        }
      }
    }
    ExFreePool(InterfacesKeyString);

    if (Update) {
      LinkageKeyStringLength = KeyInformation->NameLength + sizeof(LinkagePath);
      if ((LinkageKeyString = (PWCHAR)ExAllocatePool(NonPagedPool, LinkageKeyStringLength)) == NULL) {
        DbgPrint("ExAllocatePool LinkageKeyString failed in SetupRegistry\n");
        goto e0_2;
      }
      RtlCopyMemory(LinkageKeyString, KeyInformation->Name, KeyInformation->NameLength);
      RtlCopyMemory(&LinkageKeyString[(KeyInformation->NameLength / sizeof(WCHAR))], LinkagePath, sizeof(LinkagePath));
      RtlInitUnicodeString(&LinkageKey, LinkageKeyString);

      InitializeObjectAttributes(&SubKeyObject, &LinkageKey, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NetworkClassKeyHandle, NULL);
      if (!NT_SUCCESS(ZwCreateKey(&SubKeyHandle, KEY_ALL_ACCESS, &SubKeyObject, 0, NULL, REG_OPTION_NON_VOLATILE, NULL))) {
        DbgPrint("ZwCreateKey failed in SetupRegistry (%lx)\n");
        goto e2_0;
      }
      if ((Status = ZwQueryValueKey(SubKeyHandle, &UpperBind, KeyValuePartialInformation, NULL, 0, &ResultLength)) != STATUS_OBJECT_NAME_NOT_FOUND) {;
        if ((Status != STATUS_SUCCESS) && (Status != STATUS_BUFFER_OVERFLOW) && (Status != STATUS_BUFFER_TOO_SMALL)) {
          DbgPrint("ZwQueryValueKey LinkageKey 1 failed in SetupRegistry (%lx)\n", Status);
          goto e2_1;
        }
        if ((KeyValueInformation = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePool(NonPagedPool, ResultLength)) == NULL) {
          DbgPrint("ExAllocatePool LinkageKey KeyValueData failed in SetupRegistry\n");
          goto e2_1;
        }
        if (!(NT_SUCCESS(ZwQueryValueKey(SubKeyHandle, &UpperBind, KeyValuePartialInformation, KeyValueInformation, ResultLength, &ResultLength)))) {
          DbgPrint("ZwQueryValueKey LinkageKey 2 failed in SetupRegistry\n");
          goto e2_2;
        }

        Found = FALSE;
        for (i = 0; i < (KeyValueInformation->DataLength - sizeof(L"AoE") / sizeof(WCHAR)); i++) {
          if (RtlCompareMemory(L"AoE", &(((PWCHAR)KeyValueInformation->Data)[i]), sizeof(L"AoE")) == sizeof(L"AoE")) {
            Found = TRUE;
            break;
          }
        }

        if (Found) {
          NewValueLength = KeyValueInformation->DataLength;
          if ((NewValue = (PWCHAR)ExAllocatePool(NonPagedPool, NewValueLength)) == NULL) {
            DbgPrint("ExAllocatePool NewValue 1 failed in SetupRegistry\n");
            goto e2_2;
          }
          RtlCopyMemory(NewValue, KeyValueInformation->Data, KeyValueInformation->DataLength);
        } else {
          Updated = TRUE;
          NewValueLength = KeyValueInformation->DataLength + sizeof(L"AoE");
          if ((NewValue = (PWCHAR)ExAllocatePool(NonPagedPool, NewValueLength)) == NULL) {
            DbgPrint("ExAllocatePool NewValue 2 failed in SetupRegistry\n");
            goto e2_2;
          }
          RtlCopyMemory(NewValue, L"AoE", sizeof(L"AoE"));
          RtlCopyMemory(&NewValue[(sizeof(L"AoE") / sizeof(WCHAR))], KeyValueInformation->Data, KeyValueInformation->DataLength);
        }
        ExFreePool(KeyValueInformation);
      } else {
        Updated = TRUE;
        NewValueLength = sizeof(L"AoE") + sizeof(WCHAR);
        if ((NewValue = (PWCHAR)ExAllocatePool(NonPagedPool, NewValueLength)) == NULL) {
          DbgPrint("ExAllocatePool NewValue 3 failed in SetupRegistry\n");
          goto e2_1;
        }
        RtlZeroMemory(NewValue, NewValueLength);
        RtlCopyMemory(NewValue, L"AoE", sizeof(L"AoE"));
      }
      if (!NT_SUCCESS(ZwSetValueKey(SubKeyHandle, &UpperBind, 0, REG_MULTI_SZ, NewValue, NewValueLength))) {
        DbgPrint("ZwSetValueKey failed in SetupRegistry\n");
        ExFreePool(NewValue);
        goto e2_1;
      }
      ExFreePool(NewValue);
      if (!NT_SUCCESS(ZwClose(SubKeyHandle))) {
        DbgPrint("ZwClose LinkageKey SubKeyHandle failed in SetupRegistry\n");
        goto e2_0;
      }
      ExFreePool(LinkageKeyString);

// start nic (
      NdiKeyStringLength = KeyInformation->NameLength + sizeof(NdiPath);
      if ((NdiKeyString = (PWCHAR)ExAllocatePool(NonPagedPool, NdiKeyStringLength)) == NULL) {
        DbgPrint("ExAllocatePool NdiKeyString failed in SetupRegistry\n");
        goto e0_2;
      }
      RtlCopyMemory(NdiKeyString, KeyInformation->Name, KeyInformation->NameLength);
      RtlCopyMemory(&NdiKeyString[(KeyInformation->NameLength / sizeof(WCHAR))], NdiPath, sizeof(NdiPath));
      RtlInitUnicodeString(&NdiKey, NdiKeyString);

      InitializeObjectAttributes(&SubKeyObject, &NdiKey, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NetworkClassKeyHandle, NULL);
      if (NT_SUCCESS(ZwOpenKey(&SubKeyHandle, KEY_ALL_ACCESS, &SubKeyObject))) {
        if ((Status = ZwQueryValueKey(SubKeyHandle, &Service, KeyValuePartialInformation, NULL, 0, &ResultLength)) != STATUS_OBJECT_NAME_NOT_FOUND) {
          if ((Status != STATUS_SUCCESS) && (Status != STATUS_BUFFER_OVERFLOW) && (Status != STATUS_BUFFER_TOO_SMALL)) {
            DbgPrint("ZwQueryValueKey NdiKey 1 failed in SetupRegistry (%lx)\n", Status);
            goto e3_1;
          }
          if ((KeyValueInformation = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePool(NonPagedPool, ResultLength)) == NULL) {
            DbgPrint("ExAllocatePool NdiKey KeyValueData failed in SetupRegistry\n");
            goto e3_1;
          }
          if (!(NT_SUCCESS(ZwQueryValueKey(SubKeyHandle, &Service, KeyValuePartialInformation, KeyValueInformation, ResultLength, &ResultLength)))) {
            DbgPrint("ZwQueryValueKey NdiKey 2 failed in SetupRegistry\n");
            ExFreePool(KeyValueInformation);
            goto e3_1;
          }
          if (!NT_SUCCESS(ZwClose(SubKeyHandle))) {
            DbgPrint("ZwClose NdiKey SubKeyHandle failed in SetupRegistry\n");
            goto e3_0;
          }
          if ((DriverServiceNameString = (PWCHAR)ExAllocatePool(NonPagedPool, sizeof(DriverServiceNamePath) + KeyValueInformation->DataLength - sizeof(WCHAR))) == NULL) {
            DbgPrint("ExAllocatePool DriverServiceNameString failed in SetupRegistry\n");
            goto e3_0;
          }

          RtlCopyMemory(DriverServiceNameString, DriverServiceNamePath, sizeof(DriverServiceNamePath));
          RtlCopyMemory(&DriverServiceNameString[(sizeof(DriverServiceNamePath) / sizeof(WCHAR)) - 1], KeyValueInformation->Data, KeyValueInformation->DataLength);
          RtlInitUnicodeString(&DriverServiceName, DriverServiceNameString);
//          DbgPrint("Starting driver %S -> %08x\n", KeyValueInformation->Data, ZwLoadDriver(&DriverServiceName));
          ExFreePool(DriverServiceNameString);
          ExFreePool(KeyValueInformation);
        }
      }
      ExFreePool(NdiKeyString);
    }
    ExFreePool(KeyInformation);
    SubkeyIndex++;
  }
  if (!NT_SUCCESS(ZwClose(NetworkClassKeyHandle))) DbgPrint("ZwClose NetworkClassKeyHandle failed in SetupRegistry\n");
  *StatusOut = STATUS_SUCCESS;
  return Updated;

  e3_1: if (!NT_SUCCESS(ZwClose(SubKeyHandle))) DbgPrint("ZwClose SubKeyHandle failed in SetupRegistry\n");
  e3_0: ExFreePool(NdiKeyString);
        goto e0_2;

  e2_2: ExFreePool(KeyValueInformation);
  e2_1: if (!NT_SUCCESS(ZwClose(SubKeyHandle))) DbgPrint("ZwClose SubKeyHandle failed in SetupRegistry\n");
  e2_0: ExFreePool(LinkageKeyString);
        goto e0_2;

  e1_2: ExFreePool(KeyValueInformation);
  e1_1: if (!NT_SUCCESS(ZwClose(SubKeyHandle))) DbgPrint("ZwClose SubKeyHandle failed in SetupRegistry\n");
  e1_0: ExFreePool(InterfacesKeyString);
        goto e0_2;

  e0_2: ExFreePool(KeyInformation);
  e0_1: if (!NT_SUCCESS(ZwClose(NetworkClassKeyHandle))) DbgPrint("ZwClose NetworkClassKeyHandle failed in SetupRegistry\n");
  e0_0: *StatusOut = STATUS_UNSUCCESSFUL;
        return FALSE;
}
