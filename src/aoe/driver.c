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

/**
 * @file
 *
 * AoE specifics.
 */

#include <stdio.h>
#include <ntddk.h>

#include "winvblock.h"
#include "wv_stdlib.h"
#include "wv_string.h"
#include "portable.h"
#include "driver.h"
#include "device.h"
#include "disk.h"
#include "mount.h"
#include "bus.h"
#include "aoe.h"
#include "registry.h"
#include "protocol.h"
#include "debug.h"

#define AOEPROTOCOLVER 1

extern NTSTATUS STDCALL ZwWaitForSingleObject(
    IN HANDLE Handle,
    IN winvblock__bool Alertable,
    IN PLARGE_INTEGER Timeout OPTIONAL
  );

/* From aoe/bus.c */
extern WV_SP_BUS_T aoe_bus;
extern winvblock__bool aoe_bus__create(void);
extern void aoe_bus__free(void);

/* Forward declarations. */
struct aoe__disk_type_;
static void STDCALL aoe__thread_(IN void *);
static void aoe__process_abft_(void);
static void STDCALL aoe__unload_(IN PDRIVER_OBJECT);
static struct aoe__disk_type_ * aoe__create_disk_(void);
static WV_F_DEV_FREE aoe__free_disk_;
static WV_F_DISK_IO io;
static WV_F_DISK_MAX_XFER_LEN max_xfer_len;

/** Tag types. */
enum aoe__tag_type_
  {
    aoe__tag_type_io_,
    aoe__tag_type_search_drive_
  };

#ifdef _MSC_VER
#  pragma pack(1)
#endif

/** AoE packet. */
struct aoe__packet_
  {
    winvblock__uint8 ReservedFlag:2;
    winvblock__uint8 ErrorFlag:1;
    winvblock__uint8 ResponseFlag:1;
    winvblock__uint8 Ver:4;
    winvblock__uint8 Error;
    winvblock__uint16 Major;
    winvblock__uint8 Minor;
    winvblock__uint8 Command;
    winvblock__uint32 Tag;

    winvblock__uint8 WriteAFlag:1;
    winvblock__uint8 AsyncAFlag:1;
    winvblock__uint8 Reserved1AFlag:2;
    winvblock__uint8 DeviceHeadAFlag:1;
    winvblock__uint8 Reserved2AFlag:1;
    winvblock__uint8 ExtendedAFlag:1;
    winvblock__uint8 Reserved3AFlag:1;
    union
      {
        winvblock__uint8 Err;
        winvblock__uint8 Feature;
      };
    winvblock__uint8 Count;
    union
      {
        winvblock__uint8 Cmd;
        winvblock__uint8 Status;
      };

    winvblock__uint8 Lba0;
    winvblock__uint8 Lba1;
    winvblock__uint8 Lba2;
    winvblock__uint8 Lba3;
    winvblock__uint8 Lba4;
    winvblock__uint8 Lba5;
    winvblock__uint16 Reserved;

    winvblock__uint8 Data[];
  } __attribute__((__packed__));

#ifdef _MSC_VER
#  pragma pack()
#endif

/** An I/O request. */
struct aoe__io_req_
  {
    WV_E_DISK_IO_MODE Mode;
    winvblock__uint32 SectorCount;
    winvblock__uint8_ptr Buffer;
    PIRP Irp;
    winvblock__uint32 TagCount;
    winvblock__uint32 TotalTags;
  };

/** A work item "tag". */
struct aoe__work_tag_
  {
    enum aoe__tag_type_ type;
    WV_SP_DEV_T device;
    struct aoe__io_req_ * request_ptr;
    winvblock__uint32 Id;
    struct aoe__packet_ * packet_data;
    winvblock__uint32 PacketSize;
    LARGE_INTEGER FirstSendTime;
    LARGE_INTEGER SendTime;
    winvblock__uint32 BufferOffset;
    winvblock__uint32 SectorCount;
    struct aoe__work_tag_ * next;
    struct aoe__work_tag_ * previous;
  };

/** A disk search. */
struct aoe__disk_search_
  {
    WV_SP_DEV_T device;
    struct aoe__work_tag_ * tag;
    struct aoe__disk_search_ * next;
  };

enum aoe__search_state_
  {
    aoe__search_state_search_nic_,
    aoe__search_state_get_size_,
    aoe__search_state_getting_size_,
    aoe__search_state_get_geometry_,
    aoe__search_state_getting_geometry_,
    aoe__search_state_get_max_sectors_per_packet_,
    aoe__search_state_getting_max_sectors_per_packet_,
    aoe__search_state_done_
  };

/** The AoE disk type. */
struct aoe__disk_type_
  {
    WV_SP_DISK_T disk;
    winvblock__uint32 MTU;
    winvblock__uint8 ClientMac[6];
    winvblock__uint8 ServerMac[6];
    winvblock__uint32 Major;
    winvblock__uint32 Minor;
    winvblock__uint32 MaxSectorsPerPacket;
    winvblock__uint32 Timeout;
    enum aoe__search_state_ search_state;
    WV_FP_DEV_FREE prev_free;
    LIST_ENTRY tracking;
  };

struct aoe__target_list_
  {
    aoe__mount_target Target;
    struct aoe__target_list_ * next;
  };

/** Private globals. */
static struct aoe__target_list_ * aoe__target_list_ = NULL;
static KSPIN_LOCK aoe__target_list_spinlock_;
static winvblock__bool aoe__stop_ = FALSE;
static KSPIN_LOCK aoe__spinlock_;
static KEVENT aoe__thread_sig_evt_;
static struct aoe__work_tag_ * aoe__tag_list_ = NULL;
static struct aoe__work_tag_ * aoe__tag_list_last_ = NULL;
static struct aoe__work_tag_ * aoe__probe_tag_ = NULL;
static struct aoe__disk_search_ * aoe__disk_search_list_ = NULL;
static LONG aoe__outstanding_tags_ = 0;
static HANDLE aoe__thread_handle_;
static winvblock__bool aoe__started_ = FALSE;
static LIST_ENTRY aoe__disk_list_;
static KSPIN_LOCK aoe__disk_list_lock_;

/* Yield a pointer to the AoE disk. */
static struct aoe__disk_type_ * aoe__get_(WV_SP_DEV_T dev_ptr)
  {
    return disk__get_ptr(dev_ptr)->ext;
  }

static winvblock__bool STDCALL setup_reg(OUT PNTSTATUS status_out)
  {
    NTSTATUS status;
    winvblock__bool Updated = FALSE;
    WCHAR InterfacesPath[] = L"\\Ndi\\Interfaces\\";
    WCHAR LinkagePath[] = L"\\Linkage\\";
    WCHAR NdiPath[] = L"\\Ndi\\";
    WCHAR DriverServiceNamePath[] =
      L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
    OBJECT_ATTRIBUTES SubKeyObject;
    HANDLE
      ControlKeyHandle, NetworkClassKeyHandle, SubKeyHandle;
    winvblock__uint32
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
    winvblock__bool Update, Found;

    DBG("Entry\n");

    RtlInitUnicodeString(&LowerRange, L"LowerRange");
    RtlInitUnicodeString(&UpperBind, L"UpperBind");
    RtlInitUnicodeString(&Service, L"Service");

    /* Open the network adapter class key. */
    status = registry__open_key(
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
            registry__close_key(NetworkClassKeyHandle);
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
                      sizeof winvblock__literal_w / sizeof (WCHAR));
                    i++)
                  {
                    if (wv_memcmpeq(
                        winvblock__literal_w,
                        ((PWCHAR)KeyValueInformation->Data) + i,
                        sizeof winvblock__literal_w
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
                      sizeof winvblock__literal_w;
                    if ((NewValue = wv_malloc(NewValueLength)) == NULL)
                      {
                        DBG("wv_malloc NewValue 2 failed\n");
                        goto e2_2;
                      }
                    RtlCopyMemory(
                        NewValue,
                        winvblock__literal_w,
                        sizeof winvblock__literal_w
                      );
                    RtlCopyMemory(
                        NewValue +
                          (sizeof winvblock__literal_w / sizeof (WCHAR)),
                        KeyValueInformation->Data,
                        KeyValueInformation->DataLength
                      );
                     } /* else !Found */
                wv_free(KeyValueInformation);
              }
              else
              {
                Updated = TRUE;
                NewValueLength = sizeof winvblock__literal_w + sizeof (WCHAR);
                if ((NewValue = wv_mallocz(NewValueLength)) == NULL)
                  {
                    DBG("wv_mallocz NewValue 3 failed\n");
                    goto e2_1;
                  }
                RtlCopyMemory(
                    NewValue,
                    winvblock__literal_w,
                    sizeof winvblock__literal_w
                  );
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
    registry__close_key ( NetworkClassKeyHandle );
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

    registry__close_key(NetworkClassKeyHandle);
    err_keyopennetworkclass:

    *status_out = STATUS_UNSUCCESSFUL;
    return FALSE;
  }

/**
 * Start AoE operations.
 *
 * @ret Status          Return status code.
 */
NTSTATUS STDCALL DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
  ) {
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    void * ThreadObject;
    WV_SP_BUS_T bus_ptr;

    DBG("Entry\n");

    if (aoe__started_)
      return STATUS_SUCCESS;
    /* Initialize the global list of AoE disks. */
    InitializeListHead(&aoe__disk_list_);
    KeInitializeSpinLock(&aoe__disk_list_lock_);
    /* Setup the Registry. */
    if (!NT_SUCCESS(setup_reg(&Status))) {
        DBG("Could not update Registry!\n");
        return Status;
      } else {
        DBG("Registry updated\n");
      }
    /* Start up the protocol. */
    if (!NT_SUCCESS(Status = Protocol_Start())) {
        DBG("Protocol startup failure!\n");
        return Status;
      }
    /* Allocate and zero-fill the global probe tag. */
    aoe__probe_tag_ = wv_mallocz(sizeof *aoe__probe_tag_);
    if (aoe__probe_tag_ == NULL) {
        DBG("Couldn't allocate probe tag; bye!\n");
        return STATUS_INSUFFICIENT_RESOURCES;
      }

    /* Set up the probe tag's AoE packet reference. */
    aoe__probe_tag_->PacketSize = sizeof (struct aoe__packet_);
    /* Allocate and zero-fill the probe tag's packet reference. */
    aoe__probe_tag_->packet_data = wv_mallocz(aoe__probe_tag_->PacketSize);
    if (aoe__probe_tag_->packet_data == NULL) {
        DBG("Couldn't allocate aoe__probe_tag_->packet_data\n");
        wv_free(aoe__probe_tag_);
        return STATUS_INSUFFICIENT_RESOURCES;
      }
    aoe__probe_tag_->SendTime.QuadPart = 0LL;

    /* Initialize the probe tag's AoE packet. */
    aoe__probe_tag_->packet_data->Ver = AOEPROTOCOLVER;
    aoe__probe_tag_->packet_data->Major =
      htons((winvblock__uint16) -1);
    aoe__probe_tag_->packet_data->Minor = (winvblock__uint8) -1;
    aoe__probe_tag_->packet_data->Cmd = 0xec;           /* IDENTIFY DEVICE */
    aoe__probe_tag_->packet_data->Count = 1;

    /* Initialize global target-list spinlock. */
    KeInitializeSpinLock(&aoe__target_list_spinlock_);

    /* Initialize global spin-lock and global thread signal event. */
    KeInitializeSpinLock(&aoe__spinlock_);
    KeInitializeEvent(&aoe__thread_sig_evt_, SynchronizationEvent, FALSE);

    /* Initialize object attributes for thread. */
    InitializeObjectAttributes(
        &ObjectAttributes,
        NULL,
        OBJ_KERNEL_HANDLE,
        NULL,
        NULL
      );

    /* Create global thread. */
    if (!NT_SUCCESS(Status = PsCreateSystemThread(
        &aoe__thread_handle_,
        THREAD_ALL_ACCESS,
        &ObjectAttributes,
        NULL,
        NULL,
        aoe__thread_,
        NULL
      )))
      return Error("PsCreateSystemThread", Status);

    if (!NT_SUCCESS(Status = ObReferenceObjectByHandle(
        aoe__thread_handle_,
        THREAD_ALL_ACCESS,
        NULL,
        KernelMode,
        &ThreadObject,
        NULL
      ))) {
        ZwClose(aoe__thread_handle_);
        Error("ObReferenceObjectByHandle", Status);
        aoe__stop_ = TRUE;
        KeSetEvent(&aoe__thread_sig_evt_, 0, FALSE);
      }

    DriverObject->DriverUnload = aoe__unload_;
    aoe__started_ = TRUE;
    if (!aoe_bus__create()) {
        DBG("Unable to create AoE bus!\n");
        aoe__unload_(DriverObject);
        return STATUS_INSUFFICIENT_RESOURCES;
      }
    aoe__process_abft_();
    DBG("Exit\n");
    return Status;
  }

/**
 * Stop AoE operations.
 */
static void STDCALL aoe__unload_(IN PDRIVER_OBJECT DriverObject) {
    NTSTATUS Status;
    struct aoe__disk_search_ * disk_searcher, * previous_disk_searcher;
    struct aoe__work_tag_ * tag;
    KIRQL Irql, Irql2;
    struct aoe__target_list_ * Walker, * Next;

    DBG("Entry\n");
    /* If we're not already started, there's nothing to do. */
    if (!aoe__started_)
      return;
    /* Destroy the AoE bus. */
    aoe_bus__free();
    /* Stop the AoE protocol. */
    Protocol_Stop();
    /* If we're not already shutting down, signal the event. */
    if (!aoe__stop_) {
        aoe__stop_ = TRUE;
        KeSetEvent(&aoe__thread_sig_evt_, 0, FALSE);
        /* Wait until the event has been signalled. */
        if (!NT_SUCCESS(Status = ZwWaitForSingleObject(
            aoe__thread_handle_,
            FALSE,
            NULL
          )))
          Error("AoE_Stop ZwWaitForSingleObject", Status);
        ZwClose(aoe__thread_handle_);
      }

    /* Free the target list. */
    KeAcquireSpinLock(&aoe__target_list_spinlock_, &Irql2);
    Walker = aoe__target_list_;
    while (Walker != NULL) {
        Next = Walker->next;
        wv_free(Walker);
        Walker = Next;
      }
    KeReleaseSpinLock(&aoe__target_list_spinlock_, Irql2);

    /* Wait until we have the global spin-lock. */
    KeAcquireSpinLock(&aoe__spinlock_, &Irql);

    /* Free disk searches in the global disk search list. */
    disk_searcher = aoe__disk_search_list_;
    while (disk_searcher != NULL) {
        KeSetEvent(
            &(disk__get_ptr(disk_searcher->device)->SearchEvent),
            0,
            FALSE
          );
        previous_disk_searcher = disk_searcher;
        disk_searcher = disk_searcher->next;
        wv_free(previous_disk_searcher);
      }

    /* Cancel and free all tags in the global tag list. */
    tag = aoe__tag_list_;
    while (tag != NULL) {
        if (tag->request_ptr != NULL && --tag->request_ptr->TagCount == 0) {
            tag->request_ptr->Irp->IoStatus.Information = 0;
            tag->request_ptr->Irp->IoStatus.Status = STATUS_CANCELLED;
            IoCompleteRequest(tag->request_ptr->Irp, IO_NO_INCREMENT);
            wv_free(tag->request_ptr);
          }
        if (tag->next == NULL) {
            wv_free(tag->packet_data);
            wv_free(tag);
            tag = NULL;
          } else {
            tag = tag->next;
            wv_free(tag->previous->packet_data);
            wv_free(tag->previous);
          }
      }
    aoe__tag_list_ = NULL;
    aoe__tag_list_last_ = NULL;

    /* Free the global probe tag and its AoE packet. */
    wv_free(aoe__probe_tag_->packet_data);
    wv_free(aoe__probe_tag_);

    /* Release the global spin-lock. */
    KeReleaseSpinLock(&aoe__spinlock_, Irql);
    aoe_bus__free();
    aoe__started_ = FALSE;
    DBG("Exit\n");
  }

/**
 * Search for disk parameters.
 *
 * @v dev_ptr           The device extension for the disk.
 *
 * Returns TRUE if the disk could be matched, FALSE otherwise.
 */
static disk__init_decl(init)
  {
    struct aoe__disk_search_
      * disk_searcher, * disk_search_walker, * previous_disk_searcher;
    LARGE_INTEGER Timeout, CurrentTime;
    struct aoe__work_tag_ * tag, * tag_walker;
    KIRQL Irql, InnerIrql;
    LARGE_INTEGER MaxSectorsPerPacketSendTime;
    winvblock__uint32 MTU;
    struct aoe__disk_type_ * aoe_disk_ptr;

    aoe_disk_ptr = aoe__get_ ( disk_ptr->device );
    /*
     * Allocate our disk search 
     */
    if ((disk_searcher = wv_malloc(sizeof *disk_searcher)) == NULL) {
        DBG ( "Couldn't allocate for disk_searcher; bye!\n" );
        return FALSE;
      }

    /*
     * Initialize the disk search 
     */
    disk_searcher->device = disk_ptr->device;
    disk_searcher->next = NULL;
    aoe_disk_ptr->search_state = aoe__search_state_search_nic_;
    KeResetEvent ( &disk_ptr->SearchEvent );

    /*
     * Wait until we have the global spin-lock 
     */
    KeAcquireSpinLock ( &aoe__spinlock_, &Irql );

    /*
     * Add our disk search to the global list of disk searches 
     */
    if ( aoe__disk_search_list_ == NULL )
      {
        aoe__disk_search_list_ = disk_searcher;
      }
    else
      {
        disk_search_walker = aoe__disk_search_list_;
        while ( disk_search_walker->next )
    disk_search_walker = disk_search_walker->next;
        disk_search_walker->next = disk_searcher;
      }

    /*
     * Release the global spin-lock 
     */
    KeReleaseSpinLock ( &aoe__spinlock_, Irql );

    /*
     * We go through all the states until the disk is ready for use 
     */
    while ( TRUE )
      {
        /*
         * Wait for our device's extension's search to be signalled 
         */
        /*
         * TODO: Make the below value a #defined constant 
         */
        /*
         * 500.000 * 100ns = 50.000.000 ns = 50ms 
         */
        Timeout.QuadPart = -500000LL;
        KeWaitForSingleObject ( &disk_ptr->SearchEvent, Executive, KernelMode,
              FALSE, &Timeout );
        if ( aoe__stop_ )
    {
      DBG ( "AoE is shutting down; bye!\n" );
      return FALSE;
    }

        /*
         * Wait until we have the device extension's spin-lock 
         */
        KeAcquireSpinLock ( &disk_ptr->SpinLock, &Irql );

        if (aoe_disk_ptr->search_state == aoe__search_state_search_nic_)
    {
      if ( !Protocol_SearchNIC ( aoe_disk_ptr->ClientMac ) )
        {
          KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
          continue;
        }
      else
        {
          /*
           * We found the adapter to use, get MTU next 
           */
          aoe_disk_ptr->MTU = Protocol_GetMTU ( aoe_disk_ptr->ClientMac );
          aoe_disk_ptr->search_state = aoe__search_state_get_size_;
        }
    }

        if (aoe_disk_ptr->search_state == aoe__search_state_getting_size_)
    {
      /*
       * Still getting the disk's size 
       */
      KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
      continue;
    }
        if (aoe_disk_ptr->search_state == aoe__search_state_getting_geometry_)
    {
      /*
       * Still getting the disk's geometry 
       */
      KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
      continue;
    }
        if (aoe_disk_ptr->search_state ==
          aoe__search_state_getting_max_sectors_per_packet_)
    {
      KeQuerySystemTime ( &CurrentTime );
      /*
       * TODO: Make the below value a #defined constant 
       */
      /*
       * 2.500.000 * 100ns = 250.000.000 ns = 250ms 
       */
      if ( CurrentTime.QuadPart >
           MaxSectorsPerPacketSendTime.QuadPart + 2500000LL )
        {
          DBG ( "No reply after 250ms for MaxSectorsPerPacket %d, "
          "giving up\n", aoe_disk_ptr->MaxSectorsPerPacket );
          aoe_disk_ptr->MaxSectorsPerPacket--;
          aoe_disk_ptr->search_state = aoe__search_state_done_;
        }
      else
        {
          /*
           * Still getting the maximum sectors per packet count 
           */
          KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
          continue;
        }
    }

        if (aoe_disk_ptr->search_state == aoe__search_state_done_)
    {
      /*
       * We've finished the disk search; perform clean-up 
       */
      KeAcquireSpinLock ( &aoe__spinlock_, &InnerIrql );

      /*
       * Tag clean-up: Find out if our tag is in the global tag list 
       */
      tag_walker = aoe__tag_list_;
      while ( tag_walker != NULL && tag_walker != tag )
        tag_walker = tag_walker->next;
      if ( tag_walker != NULL )
        {
          /*
           * We found it.  If it's at the beginning of the list, adjust
           * the list to point the the next tag
           */
          if ( tag->previous == NULL )
      aoe__tag_list_ = tag->next;
          else
      /*
       * Remove our tag from the list 
       */
      tag->previous->next = tag->next;
          /*
           * If we 're at the end of the list, adjust the list's end to
           * point to the penultimate tag
           */
          if ( tag->next == NULL )
      aoe__tag_list_last_ = tag->previous;
          else
      /*
       * Remove our tag from the list 
       */
      tag->next->previous = tag->previous;
          aoe__outstanding_tags_--;
          if ( aoe__outstanding_tags_ < 0 )
      DBG ( "aoe__outstanding_tags_ < 0!!\n" );
          /*
           * Free our tag and its AoE packet 
           */
          wv_free(tag->packet_data);
          wv_free(tag);
        }

      /*
       * Disk search clean-up 
       */
      if ( aoe__disk_search_list_ == NULL )
        {
          DBG ( "aoe__disk_search_list_ == NULL!!\n" );
        }
      else
        {
          /*
           * Find our disk search in the global list of disk searches 
           */
          disk_search_walker = aoe__disk_search_list_;
          while ( disk_search_walker
            && disk_search_walker->device != disk_ptr->device )
      {
        previous_disk_searcher = disk_search_walker;
        disk_search_walker = disk_search_walker->next;
      }
          if ( disk_search_walker )
      {
        /*
         * We found our disk search.  If it's the first one in
         * the list, adjust the list and remove it
         */
        if ( disk_search_walker == aoe__disk_search_list_ )
          aoe__disk_search_list_ = disk_search_walker->next;
        else
          /*
           * Just remove it 
           */
          previous_disk_searcher->next = disk_search_walker->next;
        /*
         * Free our disk search 
         */
        wv_free(disk_search_walker);
      }
          else
      {
        DBG ( "Disk not found in aoe__disk_search_list_!!\n" );
      }
        }

      /*
       * Release global and device extension spin-locks 
       */
      KeReleaseSpinLock ( &aoe__spinlock_, InnerIrql );
      KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );

      DBG ( "Disk size: %I64uM cylinders: %I64u heads: %u "
      "sectors: %u sectors per packet: %u\n",
      disk_ptr->LBADiskSize / 2048, disk_ptr->Cylinders,
      disk_ptr->Heads, disk_ptr->Sectors,
      aoe_disk_ptr->MaxSectorsPerPacket );
      return TRUE;
    }

        #if 0
        if ( aoe_disk_ptr->search_state == aoe__search_state_done_)
        #endif
        /*
         * Establish our tag 
         */
        if ((tag = wv_mallocz(sizeof *tag)) == NULL) {
      DBG ( "Couldn't allocate tag\n" );
      KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
      /*
       * Maybe next time around 
       */
      continue;
    }
        tag->type = aoe__tag_type_search_drive_;
        tag->device = disk_ptr->device;

        /*
         * Establish our tag's AoE packet 
         */
        tag->PacketSize = sizeof (struct aoe__packet_);
        if ((tag->packet_data = wv_mallocz(tag->PacketSize)) == NULL) {
      DBG ( "Couldn't allocate tag->packet_data\n" );
      wv_free(tag);
      tag = NULL;
      KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
      /*
       * Maybe next time around 
       */
      continue;
    }
        tag->packet_data->Ver = AOEPROTOCOLVER;
        tag->packet_data->Major =
    htons ( ( winvblock__uint16 ) aoe_disk_ptr->Major );
        tag->packet_data->Minor = ( winvblock__uint8 ) aoe_disk_ptr->Minor;
        tag->packet_data->ExtendedAFlag = TRUE;

        /*
         * Initialize the packet appropriately based on our current phase 
         */
        switch ( aoe_disk_ptr->search_state )
    {
      case aoe__search_state_get_size_:
        /*
         * TODO: Make the below value into a #defined constant 
         */
        tag->packet_data->Cmd = 0xec;  /* IDENTIFY DEVICE */
        tag->packet_data->Count = 1;
        aoe_disk_ptr->search_state = aoe__search_state_getting_size_;
        break;
      case aoe__search_state_get_geometry_:
        /*
         * TODO: Make the below value into a #defined constant 
         */
        tag->packet_data->Cmd = 0x24;  /* READ SECTOR */
        tag->packet_data->Count = 1;
        aoe_disk_ptr->search_state = aoe__search_state_getting_geometry_;
        break;
      case aoe__search_state_get_max_sectors_per_packet_:
        /*
         * TODO: Make the below value into a #defined constant 
         */
        tag->packet_data->Cmd = 0x24;  /* READ SECTOR */
        tag->packet_data->Count =
          ( winvblock__uint8 ) ( ++aoe_disk_ptr->MaxSectorsPerPacket );
        KeQuerySystemTime ( &MaxSectorsPerPacketSendTime );
        aoe_disk_ptr->search_state =
          aoe__search_state_getting_max_sectors_per_packet_;
        /*
         * TODO: Make the below value into a #defined constant 
         */
        aoe_disk_ptr->Timeout = 200000;
        break;
      default:
        DBG ( "Undefined search_state!!\n" );
        wv_free(tag->packet_data);
        wv_free(tag);
        /*
         * TODO: Do we need to nullify tag here? 
         */
        KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
        continue;
        break;
    }

        /*
         * Enqueue our tag 
         */
        tag->next = NULL;
        KeAcquireSpinLock ( &aoe__spinlock_, &InnerIrql );
        if ( aoe__tag_list_ == NULL )
    {
      aoe__tag_list_ = tag;
      tag->previous = NULL;
    }
        else
    {
      aoe__tag_list_last_->next = tag;
      tag->previous = aoe__tag_list_last_;
    }
        aoe__tag_list_last_ = tag;
        KeReleaseSpinLock ( &aoe__spinlock_, InnerIrql );
        KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
      }
  }

static NTSTATUS STDCALL io(
    IN WV_SP_DEV_T dev_ptr,
    IN WV_E_DISK_IO_MODE mode,
    IN LONGLONG start_sector,
    IN winvblock__uint32 sector_count,
    IN winvblock__uint8_ptr buffer,
    IN PIRP irp
  ) {
    struct aoe__io_req_ * request_ptr;
    struct aoe__work_tag_ * tag, * new_tag_list = NULL, * previous_tag = NULL;
    KIRQL Irql;
    winvblock__uint32 i;
    PHYSICAL_ADDRESS PhysicalAddress;
    winvblock__uint8_ptr PhysicalMemory;
    WV_SP_DISK_T disk_ptr;
    struct aoe__disk_type_ * aoe_disk_ptr;

    /*
     * Establish pointers to the disk and AoE disk
     */
    disk_ptr = disk__get_ptr ( dev_ptr );
    aoe_disk_ptr = aoe__get_ ( dev_ptr );

    if ( aoe__stop_ )
      {
        /*
         * Shutting down AoE; we can't service this request 
         */
        irp->IoStatus.Information = 0;
        irp->IoStatus.Status = STATUS_CANCELLED;
        IoCompleteRequest ( irp, IO_NO_INCREMENT );
        return STATUS_CANCELLED;
      }

    if ( sector_count < 1 )
      {
        /*
         * A silly request 
         */
        DBG ( "sector_count < 1; cancelling\n" );
        irp->IoStatus.Information = 0;
        irp->IoStatus.Status = STATUS_CANCELLED;
        IoCompleteRequest ( irp, IO_NO_INCREMENT );
        return STATUS_CANCELLED;
      }

    /*
     * Allocate and zero-fill our request 
     */
    if ((request_ptr = wv_mallocz(sizeof *request_ptr)) == NULL) {
        DBG ( "Couldn't allocate for reques_ptr; bye!\n" );
        irp->IoStatus.Information = 0;
        irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest ( irp, IO_NO_INCREMENT );
        return STATUS_INSUFFICIENT_RESOURCES;
      }

    /*
     * Initialize the request 
     */
    request_ptr->Mode = mode;
    request_ptr->SectorCount = sector_count;
    request_ptr->Buffer = buffer;
    request_ptr->Irp = irp;
    request_ptr->TagCount = 0;

    /*
     * Split the requested sectors into packets in tags
     */
    for ( i = 0; i < sector_count; i += aoe_disk_ptr->MaxSectorsPerPacket )
      {
        /*
         * Allocate each tag 
         */
        if ((tag = wv_mallocz(sizeof *tag)) == NULL) {
      DBG ( "Couldn't allocate tag; bye!\n" );
      /*
       * We failed while allocating tags; free the ones we built 
       */
      tag = new_tag_list;
      while ( tag != NULL )
        {
          previous_tag = tag;
          tag = tag->next;
          wv_free(previous_tag->packet_data);
          wv_free(previous_tag);
        }
      wv_free(request_ptr);
      irp->IoStatus.Information = 0;
      irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
      IoCompleteRequest ( irp, IO_NO_INCREMENT );
      return STATUS_INSUFFICIENT_RESOURCES;
    }

        /*
         * Initialize each tag 
         */
        tag->type = aoe__tag_type_io_;
        tag->request_ptr = request_ptr;
        tag->device = dev_ptr;
        request_ptr->TagCount++;
        tag->Id = 0;
        tag->BufferOffset = i * disk_ptr->SectorSize;
        tag->SectorCount =
    ( ( sector_count - i ) <
      aoe_disk_ptr->MaxSectorsPerPacket ? sector_count -
      i : aoe_disk_ptr->MaxSectorsPerPacket );

        /*
         * Allocate and initialize each tag's AoE packet 
         */
        tag->PacketSize = sizeof (struct aoe__packet_);
        if (mode == WvDiskIoModeWrite)
    tag->PacketSize += tag->SectorCount * disk_ptr->SectorSize;
        if ((tag->packet_data = wv_mallocz(tag->PacketSize)) == NULL) {
      DBG ( "Couldn't allocate tag->packet_data; bye!\n" );
      /*
       * We failed while allocating an AoE packet; free
       * the tags we built
       */
      wv_free(tag);
      tag = new_tag_list;
      while ( tag != NULL )
        {
          previous_tag = tag;
          tag = tag->next;
          wv_free(previous_tag->packet_data);
          wv_free(previous_tag);
        }
      wv_free(request_ptr);
      irp->IoStatus.Information = 0;
      irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
      IoCompleteRequest ( irp, IO_NO_INCREMENT );
      return STATUS_INSUFFICIENT_RESOURCES;
    }
        tag->packet_data->Ver = AOEPROTOCOLVER;
        tag->packet_data->Major =
    htons ( ( winvblock__uint16 ) aoe_disk_ptr->Major );
        tag->packet_data->Minor = ( winvblock__uint8 ) aoe_disk_ptr->Minor;
        tag->packet_data->Tag = 0;
        tag->packet_data->Command = 0;
        tag->packet_data->ExtendedAFlag = TRUE;
        if (mode == WvDiskIoModeRead)
    {
      tag->packet_data->Cmd = 0x24;  /* READ SECTOR */
    }
        else
    {
      tag->packet_data->Cmd = 0x34;  /* WRITE SECTOR */
      tag->packet_data->WriteAFlag = 1;
    }
        tag->packet_data->Count = ( winvblock__uint8 ) tag->SectorCount;
        tag->packet_data->Lba0 =
    ( winvblock__uint8 ) ( ( ( start_sector + i ) >> 0 ) & 255 );
        tag->packet_data->Lba1 =
    ( winvblock__uint8 ) ( ( ( start_sector + i ) >> 8 ) & 255 );
        tag->packet_data->Lba2 =
    ( winvblock__uint8 ) ( ( ( start_sector + i ) >> 16 ) & 255 );
        tag->packet_data->Lba3 =
    ( winvblock__uint8 ) ( ( ( start_sector + i ) >> 24 ) & 255 );
        tag->packet_data->Lba4 =
    ( winvblock__uint8 ) ( ( ( start_sector + i ) >> 32 ) & 255 );
        tag->packet_data->Lba5 =
    ( winvblock__uint8 ) ( ( ( start_sector + i ) >> 40 ) & 255 );

        /* For a write request, copy from the buffer into the AoE packet. */
        if (mode == WvDiskIoModeWrite)
    RtlCopyMemory ( tag->packet_data->Data, &buffer[tag->BufferOffset],
        tag->SectorCount * disk_ptr->SectorSize );

        /*
         * Add this tag to the request's tag list 
         */
        tag->previous = previous_tag;
        tag->next = NULL;
        if ( new_tag_list == NULL )
    {
      new_tag_list = tag;
    }
        else
    {
      previous_tag->next = tag;
    }
        previous_tag = tag;
      }
    /*
     * Split the requested sectors into packets in tags
     */
    request_ptr->TotalTags = request_ptr->TagCount;

    /*
     * Wait until we have the global spin-lock 
     */
    KeAcquireSpinLock ( &aoe__spinlock_, &Irql );

    /*
     * Enqueue our request's tag list to the global tag list 
     */
    if ( aoe__tag_list_last_ == NULL )
      {
        aoe__tag_list_ = new_tag_list;
      }
    else
      {
        aoe__tag_list_last_->next = new_tag_list;
        new_tag_list->previous = aoe__tag_list_last_;
      }
    /*
     * Adjust the global list to reflect our last tag 
     */
    aoe__tag_list_last_ = tag;

    irp->IoStatus.Information = 0;
    irp->IoStatus.Status = STATUS_PENDING;
    IoMarkIrpPending ( irp );

    KeReleaseSpinLock ( &aoe__spinlock_, Irql );
    KeSetEvent ( &aoe__thread_sig_evt_, 0, FALSE );
    return STATUS_PENDING;
  }

static void STDCALL add_target(
    IN winvblock__uint8_ptr ClientMac,
    IN winvblock__uint8_ptr ServerMac,
    winvblock__uint16 Major,
    winvblock__uint8 Minor,
    LONGLONG LBASize
  )
  {
    struct aoe__target_list_ * Walker, * Last;
    KIRQL Irql;

    KeAcquireSpinLock ( &aoe__target_list_spinlock_, &Irql );
    Walker = Last = aoe__target_list_;
    while ( Walker != NULL )
      {
        if (wv_memcmpeq(&Walker->Target.ClientMac, ClientMac, 6) &&
          wv_memcmpeq(&Walker->Target.ServerMac, ServerMac, 6) &&
          Walker->Target.Major == Major
          && Walker->Target.Minor == Minor) {
      if ( Walker->Target.LBASize != LBASize )
        {
          DBG ( "LBASize changed for e%d.%d " "(%I64u->%I64u)\n", Major,
          Minor, Walker->Target.LBASize, LBASize );
          Walker->Target.LBASize = LBASize;
        }
      KeQuerySystemTime ( &Walker->Target.ProbeTime );
      KeReleaseSpinLock ( &aoe__target_list_spinlock_, Irql );
      return;
    }
        Last = Walker;
        Walker = Walker->next;
      }

    if ((Walker = wv_malloc(sizeof *Walker)) == NULL) {
        DBG("wv_malloc Walker\n");
        KeReleaseSpinLock ( &aoe__target_list_spinlock_, Irql );
        return;
      }
    Walker->next = NULL;
    RtlCopyMemory ( Walker->Target.ClientMac, ClientMac, 6 );
    RtlCopyMemory ( Walker->Target.ServerMac, ServerMac, 6 );
    Walker->Target.Major = Major;
    Walker->Target.Minor = Minor;
    Walker->Target.LBASize = LBASize;
    KeQuerySystemTime ( &Walker->Target.ProbeTime );

    if ( Last == NULL )
      {
        aoe__target_list_ = Walker;
      }
    else
      {
        Last->next = Walker;
      }
    KeReleaseSpinLock ( &aoe__target_list_spinlock_, Irql );
  }

/**
 * Process an AoE reply.
 *
 * @v SourceMac         The AoE server's MAC address.
 * @v DestinationMac    The AoE client's MAC address.
 * @v Data              The AoE packet.
 * @v DataSize          The AoE packet's size.
 */
NTSTATUS STDCALL aoe__reply(
    IN winvblock__uint8_ptr SourceMac,
    IN winvblock__uint8_ptr DestinationMac,
    IN winvblock__uint8_ptr Data,
    IN winvblock__uint32 DataSize
  )
  {
    struct aoe__packet_ * reply = (struct aoe__packet_ *) Data;
    LONGLONG LBASize;
    struct aoe__work_tag_ * tag;
    KIRQL Irql;
    winvblock__bool Found = FALSE;
    LARGE_INTEGER CurrentTime;
    WV_SP_DISK_T disk_ptr;
    struct aoe__disk_type_ * aoe_disk_ptr;

    /*
     * Discard non-responses 
     */
    if ( !reply->ResponseFlag )
      return STATUS_SUCCESS;

    /*
     * If the response matches our probe, add the AoE disk device 
     */
    if ( aoe__probe_tag_->Id == reply->Tag )
      {
        RtlCopyMemory ( &LBASize, &reply->Data[200], sizeof ( LONGLONG ) );
        add_target ( DestinationMac, SourceMac, ntohs ( reply->Major ),
         reply->Minor, LBASize );
        return STATUS_SUCCESS;
      }

    /*
     * Wait until we have the global spin-lock 
     */
    KeAcquireSpinLock ( &aoe__spinlock_, &Irql );

    /*
     * Search for request tag 
     */
    if ( aoe__tag_list_ == NULL )
      {
        KeReleaseSpinLock ( &aoe__spinlock_, Irql );
        return STATUS_SUCCESS;
      }
    tag = aoe__tag_list_;
    while ( tag != NULL )
      {
        if ( ( tag->Id == reply->Tag )
       && ( tag->packet_data->Major == reply->Major )
       && ( tag->packet_data->Minor == reply->Minor ) )
    {
      Found = TRUE;
      break;
    }
        tag = tag->next;
      }
    if ( !Found )
      {
        KeReleaseSpinLock ( &aoe__spinlock_, Irql );
        return STATUS_SUCCESS;
      }
    else
      {
        /*
         * Remove the tag from the global tag list 
         */
        if ( tag->previous == NULL )
    aoe__tag_list_ = tag->next;
        else
    tag->previous->next = tag->next;
        if ( tag->next == NULL )
    aoe__tag_list_last_ = tag->previous;
        else
    tag->next->previous = tag->previous;
        aoe__outstanding_tags_--;
        if ( aoe__outstanding_tags_ < 0 )
    DBG ( "aoe__outstanding_tags_ < 0!!\n" );
        KeSetEvent ( &aoe__thread_sig_evt_, 0, FALSE );
      }
    KeReleaseSpinLock ( &aoe__spinlock_, Irql );

    /*
     * Establish pointers to the disk device and AoE disk
     */
    disk_ptr = disk__get_ptr ( tag->device );
    aoe_disk_ptr = aoe__get_ ( tag->device );

    /*
     * If our tag was a discovery request, note the server 
     */
    if (wv_memcmpeq(aoe_disk_ptr->ServerMac, "\xff\xff\xff\xff\xff\xff", 6)) {
        RtlCopyMemory ( aoe_disk_ptr->ServerMac, SourceMac, 6 );
        DBG ( "Major: %d minor: %d found on server "
        "%02x:%02x:%02x:%02x:%02x:%02x\n", aoe_disk_ptr->Major,
        aoe_disk_ptr->Minor, SourceMac[0], SourceMac[1], SourceMac[2],
        SourceMac[3], SourceMac[4], SourceMac[5] );
      }

    KeQuerySystemTime ( &CurrentTime );
    aoe_disk_ptr->Timeout -=
      ( winvblock__uint32 ) ( ( aoe_disk_ptr->Timeout -
              ( CurrentTime.QuadPart -
          tag->FirstSendTime.QuadPart ) ) / 1024 );
    /*
     * TODO: Replace the values below with #defined constants 
     */
    if ( aoe_disk_ptr->Timeout > 100000000 )
      aoe_disk_ptr->Timeout = 100000000;

    switch ( tag->type )
      {
        case aoe__tag_type_search_drive_:
    KeAcquireSpinLock ( &disk_ptr->SpinLock, &Irql );
    switch ( aoe_disk_ptr->search_state )
      {
        case aoe__search_state_getting_size_:
          /*
           * The reply tells us the disk size
           */
          RtlCopyMemory ( &disk_ptr->LBADiskSize, &reply->Data[200],
              sizeof ( LONGLONG ) );
          /*
           * Next we are concerned with the disk geometry
           */
          aoe_disk_ptr->search_state = aoe__search_state_get_geometry_;
          break;
        case aoe__search_state_getting_geometry_:
          /*
           * FIXME: use real values from partition table.
           * We used to truncate a fractional end cylinder, but
           * now leave it be in the hopes everyone uses LBA
           */
          disk_ptr->SectorSize = 512;
          disk_ptr->Heads = 255;
          disk_ptr->Sectors = 63;
          disk_ptr->Cylinders =
      disk_ptr->LBADiskSize / ( disk_ptr->Heads *
              disk_ptr->Sectors );
          /*
           * Next we are concerned with the maximum sectors per packet
           */
          aoe_disk_ptr->search_state =
            aoe__search_state_get_max_sectors_per_packet_;
          break;
        case aoe__search_state_getting_max_sectors_per_packet_:
          DataSize -= sizeof (struct aoe__packet_);
          if ( DataSize <
         ( aoe_disk_ptr->MaxSectorsPerPacket *
           disk_ptr->SectorSize ) )
      {
        DBG ( "Packet size too low while getting "
        "MaxSectorsPerPacket (tried %d, got size of %d)\n",
        aoe_disk_ptr->MaxSectorsPerPacket, DataSize );
        aoe_disk_ptr->MaxSectorsPerPacket--;
        aoe_disk_ptr->search_state = aoe__search_state_done_;
      }
          else if ( aoe_disk_ptr->MTU <
        ( sizeof (struct aoe__packet_) +
          ( ( aoe_disk_ptr->MaxSectorsPerPacket +
              1 ) * disk_ptr->SectorSize ) ) )
      {
        DBG ( "Got MaxSectorsPerPacket %d at size of %d. "
        "MTU of %d reached\n",
        aoe_disk_ptr->MaxSectorsPerPacket, DataSize,
        aoe_disk_ptr->MTU );
        aoe_disk_ptr->search_state = aoe__search_state_done_;
      }
          else
      {
        DBG ( "Got MaxSectorsPerPacket %d at size of %d, "
        "trying next...\n", aoe_disk_ptr->MaxSectorsPerPacket,
        DataSize );
        aoe_disk_ptr->search_state =
          aoe__search_state_get_max_sectors_per_packet_;
      }
          break;
        default:
          DBG ( "Undefined search_state!\n" );
          break;
      }
    KeReleaseSpinLock ( &disk_ptr->SpinLock, Irql );
    KeSetEvent ( &disk_ptr->SearchEvent, 0, FALSE );
    break;
        case aoe__tag_type_io_:
    /* If the reply is in response to a read request, get our data! */
    if (tag->request_ptr->Mode == WvDiskIoModeRead)
      RtlCopyMemory ( &tag->request_ptr->Buffer[tag->BufferOffset],
          reply->Data,
          tag->SectorCount * disk_ptr->SectorSize );
    /*
     * If this is the last reply expected for the read request,
     * complete the IRP and free the request
     */
    if ( InterlockedDecrement ( &tag->request_ptr->TagCount ) == 0 )
      {
        tag->request_ptr->Irp->IoStatus.Information =
          tag->request_ptr->SectorCount * disk_ptr->SectorSize;
        tag->request_ptr->Irp->IoStatus.Status = STATUS_SUCCESS;
        Driver_CompletePendingIrp ( tag->request_ptr->Irp );
        wv_free(tag->request_ptr);
      }
    break;
        default:
    DBG ( "Unknown tag type!!\n" );
    break;
      }

    KeSetEvent ( &aoe__thread_sig_evt_, 0, FALSE );
    wv_free(tag->packet_data);
    wv_free(tag);
    return STATUS_SUCCESS;
  }

void aoe__reset_probe(void)
  {
    aoe__probe_tag_->SendTime.QuadPart = 0LL;
  }

static void STDCALL aoe__thread_(IN void *StartContext)
  {
    LARGE_INTEGER Timeout, CurrentTime, ProbeTime, ReportTime;
    winvblock__uint32 NextTagId = 1;
    struct aoe__work_tag_ * tag;
    KIRQL Irql;
    winvblock__uint32 Sends = 0;
    winvblock__uint32 Resends = 0;
    winvblock__uint32 ResendFails = 0;
    winvblock__uint32 Fails = 0;
    winvblock__uint32 RequestTimeout = 0;
    WV_SP_DISK_T disk_ptr;
    struct aoe__disk_type_ * aoe_disk_ptr;

    DBG ( "Entry\n" );
    ReportTime.QuadPart = 0LL;
    ProbeTime.QuadPart = 0LL;

    while ( TRUE )
      {
        /*
         * TODO: Make the below value a #defined constant 
         */
        /*
         * 100.000 * 100ns = 10.000.000 ns = 10ms
         */
        Timeout.QuadPart = -100000LL;
        KeWaitForSingleObject ( &aoe__thread_sig_evt_, Executive,
              KernelMode, FALSE, &Timeout );
        KeResetEvent ( &aoe__thread_sig_evt_ );
        if ( aoe__stop_ )
    {
      DBG ( "Stopping...\n" );
      PsTerminateSystemThread ( STATUS_SUCCESS );
    }

        KeQuerySystemTime ( &CurrentTime );
        /*
         * TODO: Make the below value a #defined constant 
         */
        if ( CurrentTime.QuadPart > ( ReportTime.QuadPart + 10000000LL ) )
    {
      DBG ( "Sends: %d  Resends: %d  ResendFails: %d  Fails: %d  "
      "aoe__outstanding_tags_: %d  RequestTimeout: %d\n", Sends,
      Resends, ResendFails, Fails, aoe__outstanding_tags_,
      RequestTimeout );
      Sends = 0;
      Resends = 0;
      ResendFails = 0;
      Fails = 0;
      KeQuerySystemTime ( &ReportTime );
    }

        /*
         * TODO: Make the below value a #defined constant 
         */
        if ( CurrentTime.QuadPart >
       ( aoe__probe_tag_->SendTime.QuadPart + 100000000LL ) )
    {
      aoe__probe_tag_->Id = NextTagId++;
      if ( NextTagId == 0 )
        NextTagId++;
      aoe__probe_tag_->packet_data->Tag = aoe__probe_tag_->Id;
      Protocol_Send ( "\xff\xff\xff\xff\xff\xff",
          "\xff\xff\xff\xff\xff\xff",
          ( winvblock__uint8_ptr ) aoe__probe_tag_->
          packet_data, aoe__probe_tag_->PacketSize,
          NULL );
      KeQuerySystemTime ( &aoe__probe_tag_->SendTime );
    }

        KeAcquireSpinLock ( &aoe__spinlock_, &Irql );
        if ( aoe__tag_list_ == NULL )
    {
      KeReleaseSpinLock ( &aoe__spinlock_, Irql );
      continue;
    }
        tag = aoe__tag_list_;
        while ( tag != NULL )
    {
      /*
       * Establish pointers to the disk and AoE disk
       */
      disk_ptr = disk__get_ptr ( tag->device );
      aoe_disk_ptr = aoe__get_ ( tag->device );

      RequestTimeout = aoe_disk_ptr->Timeout;
      if ( tag->Id == 0 )
        {
          if ( aoe__outstanding_tags_ <= 64 )
      {
        /*
         * if ( aoe__outstanding_tags_ <= 102400 ) { 
         */
        if ( aoe__outstanding_tags_ < 0 )
          DBG ( "aoe__outstanding_tags_ < 0!!\n" );
        tag->Id = NextTagId++;
        if ( NextTagId == 0 )
          NextTagId++;
        tag->packet_data->Tag = tag->Id;
        if ( Protocol_Send
             ( aoe_disk_ptr->ClientMac, aoe_disk_ptr->ServerMac,
         ( winvblock__uint8_ptr ) tag->packet_data,
         tag->PacketSize, tag ) )
          {
            KeQuerySystemTime ( &tag->FirstSendTime );
            KeQuerySystemTime ( &tag->SendTime );
            aoe__outstanding_tags_++;
            Sends++;
          }
        else
          {
            Fails++;
            tag->Id = 0;
            break;
          }
      }
        }
      else
        {
          KeQuerySystemTime ( &CurrentTime );
          if ( CurrentTime.QuadPart >
         ( tag->SendTime.QuadPart +
           ( LONGLONG ) ( aoe_disk_ptr->Timeout * 2 ) ) )
      {
        if ( Protocol_Send
             ( aoe_disk_ptr->ClientMac, aoe_disk_ptr->ServerMac,
         ( winvblock__uint8_ptr ) tag->packet_data,
         tag->PacketSize, tag ) )
          {
            KeQuerySystemTime ( &tag->SendTime );
            aoe_disk_ptr->Timeout += aoe_disk_ptr->Timeout / 1000;
            if ( aoe_disk_ptr->Timeout > 100000000 )
        aoe_disk_ptr->Timeout = 100000000;
            Resends++;
          }
        else
          {
            ResendFails++;
            break;
          }
      }
        }
      tag = tag->next;
      if ( tag == aoe__tag_list_ )
        {
          DBG ( "Taglist Cyclic!!\n" );
          break;
        }
    }
        KeReleaseSpinLock ( &aoe__spinlock_, Irql );
      }
    DBG ( "Exit\n" );
  }

static winvblock__uint32 max_xfer_len(IN WV_SP_DISK_T disk_ptr) {
    struct aoe__disk_type_ * aoe_disk_ptr = aoe__get_(disk_ptr->device);

    return disk_ptr->SectorSize * aoe_disk_ptr->MaxSectorsPerPacket;
  }

static winvblock__uint32 STDCALL query_id(
    IN WV_SP_DEV_T dev,
    IN BUS_QUERY_ID_TYPE query_type,
    IN OUT WCHAR (*buf)[512]
  ) {
    struct aoe__disk_type_ * aoe_disk = aoe__get_(dev);

    switch (query_type) {
        case BusQueryDeviceID:
          return swprintf(*buf, winvblock__literal_w L"\\AoEHardDisk") + 1;

        case BusQueryInstanceID:
          return swprintf(
              *buf,
              L"AoE_at_Shelf_%d.Slot_%d",
              aoe_disk->Major,
              aoe_disk->Minor
            ) + 1;

        case BusQueryHardwareIDs: {
            winvblock__uint32 tmp;

            tmp = swprintf(
                *buf,
                winvblock__literal_w L"\\AoEHardDisk"
              ) + 1;
            tmp += swprintf(*buf + tmp, L"GenDisk") + 4;
            return tmp;
          }

        case BusQueryCompatibleIDs:
          return swprintf(*buf, L"GenDisk") + 4;

        default:
          return 0;
      }
  }

#ifdef _MSC_VER
#  pragma pack(1)
#endif
winvblock__def_struct(abft) {
    winvblock__uint32 Signature;  /* 0x54464261 (aBFT) */
    winvblock__uint32 Length;
    winvblock__uint8 Revision;
    winvblock__uint8 Checksum;
    winvblock__uint8 OEMID[6];
    winvblock__uint8 OEMTableID[8];
    winvblock__uint8 Reserved1[12];
    winvblock__uint16 Major;
    winvblock__uint8 Minor;
    winvblock__uint8 Reserved2;
    winvblock__uint8 ClientMac[6];
  } __attribute__((__packed__));
#ifdef _MSC_VER
#  pragma pack()
#endif

disk__close_decl(close) {
    return;
  }

static void aoe__process_abft_(void) {
    PHYSICAL_ADDRESS PhysicalAddress;
    winvblock__uint8_ptr PhysicalMemory;
    winvblock__uint32 Offset, Checksum, i;
    winvblock__bool FoundAbft = FALSE;
    abft AoEBootRecord;
    struct aoe__disk_type_ * aoe_disk;

    /* Find aBFT. */
    PhysicalAddress.QuadPart = 0LL;
    PhysicalMemory = MmMapIoSpace(PhysicalAddress, 0xa0000, MmNonCached);
    if (!PhysicalMemory) {
        DBG("Could not map low memory\n");
        goto err_map_mem;
      }
    for (Offset = 0; Offset < 0xa0000; Offset += 0x10) {
        if (!(
            ((abft_ptr) (PhysicalMemory + Offset))->Signature ==
            0x54464261
          ))
          continue;
        Checksum = 0;
        for (
            i = 0;
            i < ((abft_ptr) (PhysicalMemory + Offset))->Length;
            i++
          )
          Checksum += PhysicalMemory[Offset + i];
        if (Checksum & 0xff)
          continue;
        if (((abft_ptr) (PhysicalMemory + Offset))->Revision != 1) {
            DBG(
                "Found aBFT with mismatched revision v%d at "
                  "segment 0x%4x. want v1.\n",
                ((abft_ptr) (PhysicalMemory + Offset))->Revision,
                (Offset / 0x10)
              );
            continue;
          }
        DBG("Found aBFT at segment: 0x%04x\n", (Offset / 0x10));
        RtlCopyMemory(
            &AoEBootRecord,
            PhysicalMemory + Offset,
            sizeof (abft)
          );
        FoundAbft = TRUE;
        break;
      }
    MmUnmapIoSpace(PhysicalMemory, 0xa0000);

    #ifdef RIS
    FoundAbft = TRUE;
    RtlCopyMemory(AoEBootRecord.ClientMac, "\x00\x0c\x29\x34\x69\x34", 6);
    AoEBootRecord.Major = 0;
    AoEBootRecord.Minor = 10;
    #endif

    if (!FoundAbft)
      goto out_no_abft;
    aoe_disk = aoe__create_disk_();
    if(aoe_disk == NULL) {
        DBG("Could not create AoE disk from aBFT!\n");
        return;
      }
    DBG(
        "Attaching AoE disk from client NIC "
          "%02x:%02x:%02x:%02x:%02x:%02x to major: %d minor: %d\n",
        AoEBootRecord.ClientMac[0],
        AoEBootRecord.ClientMac[1],
        AoEBootRecord.ClientMac[2],
        AoEBootRecord.ClientMac[3],
        AoEBootRecord.ClientMac[4],
        AoEBootRecord.ClientMac[5],
        AoEBootRecord.Major,
        AoEBootRecord.Minor
      );
    RtlCopyMemory(aoe_disk->ClientMac, AoEBootRecord.ClientMac, 6);
    RtlFillMemory(aoe_disk->ServerMac, 6, 0xff);
    aoe_disk->Major = AoEBootRecord.Major;
    aoe_disk->Minor = AoEBootRecord.Minor;
    aoe_disk->MaxSectorsPerPacket = 1;
    aoe_disk->Timeout = 200000;          /* 20 ms. */
    aoe_disk->disk->BootDrive = TRUE;
    aoe_disk->disk->Media = WvDiskMediaTypeHard;
    WvBusAddChild(driver__bus(), aoe_disk->disk->device);
    return;

    out_no_abft:
    DBG("No aBFT found\n");

    err_map_mem:

    return;
  }

NTSTATUS STDCALL aoe__scan(
    IN WV_SP_DEV_T dev,
    IN PIRP irp
  ) {
    KIRQL irql;
    winvblock__uint32 count;
    struct aoe__target_list_ * target_walker;
    aoe__mount_targets_ptr targets;
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);

    DBG("Got IOCTL_AOE_SCAN...\n");
    KeAcquireSpinLock(&aoe__target_list_spinlock_, &irql);

    count = 0;
    target_walker = aoe__target_list_;
    while (target_walker != NULL) {
        count++;
        target_walker = target_walker->next;
      }

    targets = wv_malloc(sizeof *targets + (count * sizeof targets->Target[0]));
    if (targets == NULL) {
        DBG("wv_malloc targets\n");
        return driver__complete_irp(
            irp,
            0,
            STATUS_INSUFFICIENT_RESOURCES
          );
      }
    irp->IoStatus.Information =
      sizeof (aoe__mount_targets) + (count * sizeof (aoe__mount_target));
    targets->Count = count;

    count = 0;
    target_walker = aoe__target_list_;
    while (target_walker != NULL) {
        RtlCopyMemory(
            &targets->Target[count],
            &target_walker->Target,
            sizeof (aoe__mount_target)
          );
        count++;
        target_walker = target_walker->next;
      }
    RtlCopyMemory(
        irp->AssociatedIrp.SystemBuffer,
        targets,
        (io_stack_loc->Parameters.DeviceIoControl.OutputBufferLength <
          (sizeof (aoe__mount_targets) + (count * sizeof (aoe__mount_target))) ?
          io_stack_loc->Parameters.DeviceIoControl.OutputBufferLength :
          (sizeof (aoe__mount_targets) + (count * sizeof (aoe__mount_target)))
        )
      );
    wv_free(targets);

    KeReleaseSpinLock(&aoe__target_list_spinlock_, irql);
    return driver__complete_irp(irp, irp->IoStatus.Information, STATUS_SUCCESS);
  }

NTSTATUS STDCALL aoe__show(
    IN WV_SP_DEV_T dev,
    IN PIRP irp
  ) {
    winvblock__uint32 count;
    WV_SP_DEV_T dev_walker;
    WV_SP_BUS_T bus;
    aoe__mount_disks_ptr disks;
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);

    DBG("Got IOCTL_AOE_SHOW...\n");

    bus = WvBusFromDev(dev);
    dev_walker = bus->first_child;
    count = 0;
    while (dev_walker != NULL) {
        count++;
        dev_walker = dev_walker->next_sibling_ptr;
      }

    disks = wv_malloc(sizeof *disks + (count * sizeof disks->Disk[0]));
    if (disks == NULL) {
        DBG("wv_malloc disks\n");
        return driver__complete_irp(
            irp,
            0,
            STATUS_INSUFFICIENT_RESOURCES
          );
      }
    irp->IoStatus.Information =
      sizeof (aoe__mount_disks) + (count * sizeof (aoe__mount_disk));
    disks->Count = count;

    count = 0;
    dev_walker = bus->first_child;
    while (dev_walker != NULL) {
        WV_SP_DISK_T disk = disk__get_ptr(dev_walker);
        struct aoe__disk_type_ * aoe_disk = aoe__get_(dev_walker);

        disks->Disk[count].Disk = dev_walker->DevNum;
        RtlCopyMemory(
            &disks->Disk[count].ClientMac,
            &aoe_disk->ClientMac,
            6
          );
        RtlCopyMemory(
            &disks->Disk[count].ServerMac,
            &aoe_disk->ServerMac,
            6
          );
        disks->Disk[count].Major = aoe_disk->Major;
        disks->Disk[count].Minor = aoe_disk->Minor;
        disks->Disk[count].LBASize = disk->LBADiskSize;
        count++;
        dev_walker = dev_walker->next_sibling_ptr;
      }
    RtlCopyMemory(
        irp->AssociatedIrp.SystemBuffer,
        disks,
        (io_stack_loc->Parameters.DeviceIoControl.OutputBufferLength <
          (sizeof (aoe__mount_disks) + (count * sizeof (aoe__mount_disk))) ?
          io_stack_loc->Parameters.DeviceIoControl.OutputBufferLength :
          (sizeof (aoe__mount_disks) + (count * sizeof (aoe__mount_disk)))
        )
      );
    wv_free(disks);
    return driver__complete_irp(irp, irp->IoStatus.Information, STATUS_SUCCESS);
  }

NTSTATUS STDCALL aoe__mount(
    IN WV_SP_DEV_T dev,
    IN PIRP irp
  ) {
    winvblock__uint8_ptr buffer = irp->AssociatedIrp.SystemBuffer;
    struct aoe__disk_type_ * aoe_disk;

    DBG(
        "Got IOCTL_AOE_MOUNT for client: %02x:%02x:%02x:%02x:%02x:%02x "
          "Major:%d Minor:%d\n",
        buffer[0],
        buffer[1],
        buffer[2],
        buffer[3],
        buffer[4],
        buffer[5],
        *(winvblock__uint16_ptr) (buffer + 6),
        (winvblock__uint8) buffer[8]
      );
    aoe_disk = aoe__create_disk_();
    if (aoe_disk == NULL) {
        DBG("Could not create AoE disk!\n");
        return driver__complete_irp(
            irp,
            0,
            STATUS_INSUFFICIENT_RESOURCES
          );
      }
    RtlCopyMemory(aoe_disk->ClientMac, buffer, 6);
    RtlFillMemory(aoe_disk->ServerMac, 6, 0xff);
    aoe_disk->Major = *(winvblock__uint16_ptr) (buffer + 6);
    aoe_disk->Minor = (winvblock__uint8) buffer[8];
    aoe_disk->MaxSectorsPerPacket = 1;
    aoe_disk->Timeout = 200000;             /* 20 ms. */
    aoe_disk->disk->BootDrive = FALSE;
    aoe_disk->disk->Media = WvDiskMediaTypeHard;
    WvBusAddChild(driver__bus(), aoe_disk->disk->device);

    return driver__complete_irp(irp, 0, STATUS_SUCCESS);
  }

/**
 * Create a new AoE disk.
 *
 * @ret aoe_disk *      The address of a new AoE disk, or NULL for failure.
 *
 * This function should not be confused with a PDO creation routine, which is
 * actually implemented for each device type.  This routine will allocate a
 * aoe__disk_type_, track it in a global list, as well as populate the disk
 * with default values.
 */
static struct aoe__disk_type_ * aoe__create_disk_(void) {
    WV_SP_DISK_T disk;
    struct aoe__disk_type_ * aoe_disk;

    /* Try to create a disk. */
    disk = disk__create();
    if (disk == NULL)
      goto err_nodisk;
    /*
     * AoE disk devices might be used for booting and should
     * not be allocated from a paged memory pool.
     */
    aoe_disk = wv_mallocz(sizeof *aoe_disk);
    if (aoe_disk == NULL)
      goto err_noaoedisk;
    /* Track the new AoE disk in our global list. */
    ExInterlockedInsertTailList(
        &aoe__disk_list_,
        &aoe_disk->tracking,
        &aoe__disk_list_lock_
      );
    /* Populate non-zero device defaults. */
    aoe_disk->disk = disk;
    aoe_disk->prev_free = disk->device->Ops.Free;
    disk->device->Ops.Free = aoe__free_disk_;
    disk->device->Ops.PnpId = query_id;
    disk->disk_ops.io = io;
    disk->disk_ops.max_xfer_len = max_xfer_len;
    disk->disk_ops.init = init;
    disk->disk_ops.close = close;
    disk->ext = aoe_disk;

    return aoe_disk;

    err_noaoedisk:

    WvDevFree(disk->device);
    err_nodisk:

    return NULL;
  }

/**
 * Default AoE disk deletion operation.
 *
 * @v dev               Points to the AoE disk device to delete.
 */
static void STDCALL aoe__free_disk_(IN WV_SP_DEV_T dev) {
    struct aoe__disk_type_ * aoe_disk = aoe__get_(dev);
    /* Free the "inherited class". */
    aoe_disk->prev_free(dev);
    /*
     * Track the AoE disk deletion in our global list.  Unfortunately,
     * for now we have faith that an AoE disk won't be deleted twice and
     * result in a race condition.  Something to keep in mind...
     */
    ExInterlockedRemoveHeadList(
        aoe_disk->tracking.Blink,
        &aoe__disk_list_lock_
      );

    wv_free(aoe_disk);
  }
