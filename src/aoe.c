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
#include "aoe.h"
#include "driver.h"
#include "protocol.h"

NTSTATUS STDCALL ZwWaitForSingleObject(IN HANDLE Handle, IN BOOLEAN Alertable, IN PLARGE_INTEGER Timeout OPTIONAL);

// from bus.c
VOID STDCALL BusAddTarget(IN PUCHAR ClientMac, IN PUCHAR ServerMac, USHORT Major, UCHAR Minor, LONGLONG LBASize);
VOID STDCALL BusCleanupTargetList();

// in this file
VOID STDCALL Thread(IN PVOID StartContext);

#ifdef _MSC_VER
#pragma pack(1)
#endif
typedef enum {RequestType, SearchDriveType} TAGTYPE, *PTAGTYPE;

typedef struct _AOE {
  UCHAR ReservedFlag:2;
  UCHAR ErrorFlag:1;
  UCHAR ResponseFlag:1;
  UCHAR Ver:4;
  UCHAR Error;
  USHORT Major;
  UCHAR Minor;
  UCHAR Command;
  UINT Tag;

  UCHAR WriteAFlag:1;
  UCHAR AsyncAFlag:1;
  UCHAR Reserved1AFlag:2;
  UCHAR DeviceHeadAFlag:1;
  UCHAR Reserved2AFlag:1;
  UCHAR ExtendedAFlag:1;
  UCHAR Reserved3AFlag:1;
  union {
    UCHAR Err;
    UCHAR Feature;
  };
  UCHAR Count;
  union {
    UCHAR Cmd;
    UCHAR Status;
  };

  UCHAR Lba0;
  UCHAR Lba1;
  UCHAR Lba2;
  UCHAR Lba3;
  UCHAR Lba4;
  UCHAR Lba5;
  USHORT Reserved;

  UCHAR Data[];
} __attribute__((__packed__)) AOE, *PAOE;
#ifdef _MSC_VER
#pragma pack()
#endif

typedef struct _REQUEST {
  REQUESTMODE Mode;
  ULONG SectorCount;
  PUCHAR Buffer;
  PIRP Irp;
  ULONG TagCount;
  ULONG TotalTags;
} REQUEST, *PREQUEST;

typedef struct _TAG {
  TAGTYPE Type;
  PDEVICEEXTENSION DeviceExtension;
  PREQUEST Request;
  ULONG Id;
  PAOE PacketData;
  ULONG PacketSize;
  LARGE_INTEGER FirstSendTime;
  LARGE_INTEGER SendTime;
  ULONG BufferOffset;
  ULONG SectorCount;
  struct _TAG *Next;
  struct _TAG *Previous;
} TAG, *PTAG;

typedef struct _DISKSEARCH {
  PDEVICEEXTENSION DeviceExtension;
  PTAG Tag;
  struct _DISKSEARCH *Next;
} DISKSEARCH, *PDISKSEARCH;

BOOLEAN Stop = FALSE;
KSPIN_LOCK SpinLock;
KEVENT ThreadSignalEvent;
PTAG TagList = NULL;
PTAG TagListLast = NULL;
PTAG ProbeTag = NULL;
PDISKSEARCH DiskSearchList = NULL;
LONG OutstandingTags = 0;
HANDLE ThreadHandle;

NTSTATUS STDCALL AoEStart() {
  NTSTATUS Status;
  OBJECT_ATTRIBUTES ObjectAttributes;
  PVOID ThreadObject;

  DbgPrint("AoEStart\n");
  if ((ProbeTag = (PTAG)ExAllocatePool(NonPagedPool, sizeof(TAG))) == NULL) {
    DbgPrint("AddStart ExAllocatePool ProbeTag\n");
    return STATUS_INSUFFICIENT_RESOURCES;
  }
  RtlZeroMemory(ProbeTag, sizeof(TAG));
  ProbeTag->PacketSize = sizeof(AOE);
  if ((ProbeTag->PacketData = (PAOE)ExAllocatePool(NonPagedPool, ProbeTag->PacketSize)) == NULL) {
    DbgPrint("AddStart ExAllocatePool ProbeTag->PacketData\n");
    ExFreePool(ProbeTag);
  }
  ProbeTag->SendTime.QuadPart = 0LL;
  RtlZeroMemory(ProbeTag->PacketData, ProbeTag->PacketSize);
  ProbeTag->PacketData->Ver = AOEPROTOCOLVER;
  ProbeTag->PacketData->Major = htons((USHORT)-1);
  ProbeTag->PacketData->Minor = (UCHAR)-1;
  ProbeTag->PacketData->Cmd = 0xec;             // IDENTIFY DEVICE
  ProbeTag->PacketData->Count = 1;

  KeInitializeSpinLock(&SpinLock);
  KeInitializeEvent(&ThreadSignalEvent, SynchronizationEvent, FALSE);

  InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
  if (!NT_SUCCESS(Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, &ObjectAttributes, NULL, NULL, Thread, NULL))) return Error("PsCreateSystemThread", Status);
  if (!NT_SUCCESS(Status = ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, NULL, KernelMode, &ThreadObject, NULL))) {
    ZwClose(ThreadHandle);
    Error("ObReferenceObjectByHandle", Status);
    Stop = TRUE;
    KeSetEvent(&ThreadSignalEvent, 0, FALSE);
  }
  return Status;
}

VOID STDCALL AoEStop() {
  NTSTATUS Status;
  PDISKSEARCH DiskSearch, PreviousDiskSearch;
  PTAG Tag;
  KIRQL Irql;

  DbgPrint("AoEStop\n");
  if (!Stop) {
    Stop = TRUE;
    KeSetEvent(&ThreadSignalEvent, 0, FALSE);
    if (!NT_SUCCESS(Status = ZwWaitForSingleObject(ThreadHandle, FALSE, NULL))) Error("AoEStop ZwWaitForSingleObject", Status);
    ZwClose(ThreadHandle);
  }

  KeAcquireSpinLock(&SpinLock, &Irql);

  DiskSearch = DiskSearchList;
  while (DiskSearch != NULL) {
    KeSetEvent(&DiskSearch->DeviceExtension->Disk.SearchEvent, 0, FALSE);
    PreviousDiskSearch = DiskSearch;
    DiskSearch = DiskSearch->Next;
    ExFreePool(PreviousDiskSearch);
  }

  Tag = TagList;
  while (Tag != NULL) {
    if (Tag->Request != NULL && --Tag->Request->TagCount == 0) {
      Tag->Request->Irp->IoStatus.Information = 0;
      Tag->Request->Irp->IoStatus.Status = STATUS_CANCELLED;
      IoCompleteRequest(Tag->Request->Irp, IO_NO_INCREMENT);
      ExFreePool(Tag->Request);
    }
    if (Tag->Next == NULL) {
      ExFreePool(Tag->PacketData);
      ExFreePool(Tag);
      Tag = NULL;
    } else {
      Tag = Tag->Next;
      ExFreePool(Tag->Previous->PacketData);
      ExFreePool(Tag->Previous);
    }
  }
  TagList = NULL;
  TagListLast = NULL;
  ExFreePool(ProbeTag->PacketData);
  ExFreePool(ProbeTag);

  KeReleaseSpinLock(&SpinLock, Irql);
}

BOOLEAN STDCALL AoESearchDrive(IN PDEVICEEXTENSION DeviceExtension) {
  PDISKSEARCH DiskSearch, DiskSearchWalker, PreviousDiskSearch;
  LARGE_INTEGER Timeout, CurrentTime;
  PTAG Tag, TagWalker;
  KIRQL Irql, InnerIrql;
  LARGE_INTEGER MaxSectorsPerPacketSendTime;
  ULONG MTU;

  if ((DiskSearch = (PDISKSEARCH)ExAllocatePool(NonPagedPool, sizeof(DISKSEARCH))) == NULL) {
    DbgPrint("AoESearchBootDrive ExAllocatePool DiskSearch\n");
    return FALSE;
  }

  DiskSearch->DeviceExtension = DeviceExtension;
  DiskSearch->Next = NULL;
  DeviceExtension->Disk.SearchState = SearchNIC;
  KeResetEvent(&DeviceExtension->Disk.SearchEvent);

  KeAcquireSpinLock(&SpinLock, &Irql);
  if (DiskSearchList == NULL) {
    DiskSearchList = DiskSearch;
  } else {
    DiskSearchWalker = DiskSearchList;
    while (DiskSearchWalker->Next) DiskSearchWalker = DiskSearchWalker->Next;
    DiskSearchWalker->Next = DiskSearch;
  }
  KeReleaseSpinLock(&SpinLock, Irql);

  while (TRUE) {
    Timeout.QuadPart = -500000LL;               // 500.000 * 100ns = 50.000.000 ns = 50ms
    KeWaitForSingleObject(&DeviceExtension->Disk.SearchEvent, Executive, KernelMode, FALSE, &Timeout);
    if (Stop) {
      DbgPrint("AoESearchBootDrive cancled\n");
      return FALSE;
    }

    KeAcquireSpinLock(&DeviceExtension->Disk.SpinLock, &Irql);
    if (DeviceExtension->Disk.SearchState == SearchNIC) {
      if (!ProtocolSearchNIC(DeviceExtension->Disk.ClientMac)) {
        KeReleaseSpinLock(&DeviceExtension->Disk.SpinLock, Irql);
        continue;
      } else {
        DeviceExtension->Disk.MTU = ProtocolGetMTU(DeviceExtension->Disk.ClientMac);
        DeviceExtension->Disk.SearchState = GetSize;
      }
    }

    if (DeviceExtension->Disk.SearchState == GettingSize) {
      KeReleaseSpinLock(&DeviceExtension->Disk.SpinLock, Irql);
      continue;
    }
    if (DeviceExtension->Disk.SearchState == GettingGeometry) {
      KeReleaseSpinLock(&DeviceExtension->Disk.SpinLock, Irql);
      continue;
    }
    if (DeviceExtension->Disk.SearchState == GettingMaxSectorsPerPacket) {
      KeQuerySystemTime(&CurrentTime);
      if (CurrentTime.QuadPart > MaxSectorsPerPacketSendTime.QuadPart + 2500000LL) {    // 2.500.000 * 100ns = 250.000.000 ns = 250ms
        DbgPrint("No reply after 250ms for MaxSectorsPerPacket %d, giving up\n", DeviceExtension->Disk.MaxSectorsPerPacket);
        DeviceExtension->Disk.MaxSectorsPerPacket--;
        DeviceExtension->Disk.SearchState = Done;
      } else {
        KeReleaseSpinLock(&DeviceExtension->Disk.SpinLock, Irql);
        continue;
      }
    }

    if (DeviceExtension->Disk.SearchState == Done) {
      KeAcquireSpinLock(&SpinLock, &InnerIrql);
      TagWalker = TagList;
      while (TagWalker != NULL && TagWalker != Tag) TagWalker = TagWalker->Next;
      if (TagWalker != NULL) {
        if (Tag->Previous == NULL) TagList = Tag->Next;
        else Tag->Previous->Next = Tag->Next;
        if (Tag->Next == NULL) TagListLast = Tag->Previous;
        else Tag->Next->Previous = Tag->Previous;
        OutstandingTags--;
        if (OutstandingTags < 0) DbgPrint("!!OutstandingTags < 0 (AoESearchDrive)!!\n");
        ExFreePool(Tag->PacketData);
        ExFreePool(Tag);
      }

      if (DiskSearchList == NULL) {
        DbgPrint("!!DiskSearchList == NULL!!\n");
      } else {
        DiskSearchWalker = DiskSearchList;
        while (DiskSearchWalker && DiskSearchWalker->DeviceExtension != DeviceExtension) {
          PreviousDiskSearch = DiskSearchWalker;
          DiskSearchWalker = DiskSearchWalker->Next;
        }
        if (DiskSearchWalker) {
          if (DiskSearchWalker == DiskSearchList) {
            DiskSearchList = DiskSearchWalker->Next;
          } else {
            PreviousDiskSearch->Next = DiskSearchWalker->Next;
          }
          ExFreePool(DiskSearchWalker);
        } else {
          DbgPrint("!!Disk not found in DiskSearchList!!\n");
        }
      }

      KeReleaseSpinLock(&SpinLock, InnerIrql);
      KeReleaseSpinLock(&DeviceExtension->Disk.SpinLock, Irql);
        DbgPrint("Disk size: %I64uM cylinders: %I64u heads: %u sectors: %u sectors per packet: %u\n",
        DeviceExtension->Disk.LBADiskSize / 2048,
        DeviceExtension->Disk.Cylinders,
        DeviceExtension->Disk.Heads,
        DeviceExtension->Disk.Sectors,
        DeviceExtension->Disk.MaxSectorsPerPacket
      );
      return TRUE;
    }

    if ((Tag = (PTAG)ExAllocatePool(NonPagedPool, sizeof(TAG))) == NULL) {
      DbgPrint("AoESearchBootDrive ExAllocatePool Tag\n");
      KeReleaseSpinLock(&DeviceExtension->Disk.SpinLock, Irql);
      continue;
    }
    RtlZeroMemory(Tag, sizeof(TAG));
    Tag->Type = SearchDriveType;
    Tag->DeviceExtension = DeviceExtension;
    Tag->PacketSize = sizeof(AOE);
    if ((Tag->PacketData = (PAOE)ExAllocatePool(NonPagedPool, Tag->PacketSize)) == NULL) {
      DbgPrint("AoESearchBootDrive ExAllocatePool Tag->PacketData\n");
      ExFreePool(Tag);
      Tag = NULL;
      KeReleaseSpinLock(&DeviceExtension->Disk.SpinLock, Irql);
      continue;
    }
    RtlZeroMemory(Tag->PacketData, Tag->PacketSize);
    Tag->PacketData->Ver = AOEPROTOCOLVER;
    Tag->PacketData->Major = htons((USHORT)DeviceExtension->Disk.Major);
    Tag->PacketData->Minor = (UCHAR)DeviceExtension->Disk.Minor;
    Tag->PacketData->ExtendedAFlag = TRUE;

    switch (DeviceExtension->Disk.SearchState) {
      case GetSize:
        Tag->PacketData->Cmd = 0xec;               // IDENTIFY DEVICE
        Tag->PacketData->Count = 1;
        DeviceExtension->Disk.SearchState = GettingSize;
        break;
      case GetGeometry:
        Tag->PacketData->Cmd = 0x24;               // READ SECTOR
        Tag->PacketData->Count = 1;
        DeviceExtension->Disk.SearchState = GettingGeometry;
        break;
      case GetMaxSectorsPerPacket:
        Tag->PacketData->Cmd = 0x24;               // READ SECTOR
        Tag->PacketData->Count = (UCHAR)(++DeviceExtension->Disk.MaxSectorsPerPacket);
        KeQuerySystemTime(&MaxSectorsPerPacketSendTime);
        DeviceExtension->Disk.SearchState = GettingMaxSectorsPerPacket;
        DeviceExtension->Disk.Timeout = 200000;
        break;
      default:
        DbgPrint("AoESearchBootDrive Undefined SearchState!\n");
        ExFreePool(Tag->PacketData);
        ExFreePool(Tag);
        KeReleaseSpinLock(&DeviceExtension->Disk.SpinLock, Irql);
        continue;
        break;
    }

    Tag->Next = NULL;
    KeAcquireSpinLock(&SpinLock, &InnerIrql);
    if (TagList == NULL) {
      TagList = Tag;
      Tag->Previous = NULL;
    } else {
      TagListLast->Next = Tag;
      Tag->Previous = TagListLast;
    }
    TagListLast = Tag;
    KeReleaseSpinLock(&SpinLock, InnerIrql);
    KeReleaseSpinLock(&DeviceExtension->Disk.SpinLock, Irql);
  }
}

NTSTATUS STDCALL AoERequest(IN PDEVICEEXTENSION DeviceExtension, IN REQUESTMODE Mode, IN LONGLONG StartSector, IN ULONG SectorCount, IN PUCHAR Buffer, IN PIRP Irp) {
  PREQUEST Request;
  PTAG Tag, NewTagList = NULL, PreviousTag = NULL;
  KIRQL Irql;
  ULONG i;

  if (Stop) {
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_CANCELLED;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_CANCELLED;
  }

  if (SectorCount < 1) {
    DbgPrint("AoERequest SectorCount < 1!!\n");
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_CANCELLED;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_CANCELLED;
  }

  if ((Request = (PREQUEST)ExAllocatePool(NonPagedPool, sizeof(REQUEST))) == NULL) {
    DbgPrint("AoERequest ExAllocatePool Request\n");
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INSUFFICIENT_RESOURCES;
  }
  RtlZeroMemory(Request, sizeof(REQUEST));
  Request->Mode = Mode;
  Request->SectorCount = SectorCount;
  Request->Buffer = Buffer;
  Request->Irp = Irp;
  Request->TagCount = 0;

  for (i = 0; i < SectorCount; i += DeviceExtension->Disk.MaxSectorsPerPacket) {
    if ((Tag = (PTAG)ExAllocatePool(NonPagedPool, sizeof(TAG))) == NULL) {
      DbgPrint("AoERequest ExAllocatePool Tag\n");
      Tag = NewTagList;
      while (Tag != NULL) {
        PreviousTag = Tag;
        Tag = Tag->Next;
        ExFreePool(PreviousTag->PacketData);
        ExFreePool(PreviousTag);
      }
      ExFreePool(Request);
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Tag, sizeof(TAG));
    Tag->Type = RequestType;
    Tag->Request = Request;
    Tag->DeviceExtension = DeviceExtension;
    Request->TagCount++;
    Tag->Id = 0;
    Tag->BufferOffset = i * SECTORSIZE;
    Tag->SectorCount = ((SectorCount - i) < DeviceExtension->Disk.MaxSectorsPerPacket?SectorCount - i:DeviceExtension->Disk.MaxSectorsPerPacket);
    Tag->PacketSize = sizeof(AOE);
    if (Mode == Write) Tag->PacketSize += Tag->SectorCount * SECTORSIZE;
    if ((Tag->PacketData = (PAOE)ExAllocatePool(NonPagedPool, Tag->PacketSize)) == NULL) {
      DbgPrint("AoERequest ExAllocatePool Tag->PacketData\n");
      ExFreePool(Tag);
      Tag = NewTagList;
      while (Tag != NULL) {
        PreviousTag = Tag;
        Tag = Tag->Next;
        ExFreePool(PreviousTag->PacketData);
        ExFreePool(PreviousTag);
      }
      ExFreePool(Request);
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Tag->PacketData, Tag->PacketSize);
    Tag->PacketData->Ver = AOEPROTOCOLVER;
    Tag->PacketData->Major = htons((USHORT)DeviceExtension->Disk.Major);
    Tag->PacketData->Minor = (UCHAR)DeviceExtension->Disk.Minor;
    Tag->PacketData->Tag = 0;
    Tag->PacketData->Command = 0;
    Tag->PacketData->ExtendedAFlag = TRUE;
    if (Mode == Read) {
      Tag->PacketData->Cmd = 0x24;                 // READ SECTOR
    } else {
      Tag->PacketData->Cmd = 0x34;                 // WRITE SECTOR
      Tag->PacketData->WriteAFlag = 1;
    }
    Tag->PacketData->Count = (UCHAR)Tag->SectorCount;
    Tag->PacketData->Lba0 = (UCHAR)(((StartSector + i) >> 0) & 255);
    Tag->PacketData->Lba1 = (UCHAR)(((StartSector + i) >> 8) & 255);
    Tag->PacketData->Lba2 = (UCHAR)(((StartSector + i) >> 16) & 255);
    Tag->PacketData->Lba3 = (UCHAR)(((StartSector + i) >> 24) & 255);
    Tag->PacketData->Lba4 = (UCHAR)(((StartSector + i) >> 32) & 255);
    Tag->PacketData->Lba5 = (UCHAR)(((StartSector + i) >> 40) & 255);

    if (Mode == Write) RtlCopyMemory(Tag->PacketData->Data, &Buffer[Tag->BufferOffset], Tag->SectorCount * SECTORSIZE);
    Tag->Previous = PreviousTag;
    Tag->Next = NULL;
    if (NewTagList == NULL) {
      NewTagList = Tag;
    } else {
      PreviousTag->Next = Tag;
    }
    PreviousTag = Tag;
  }
  Request->TotalTags = Request->TagCount;

  KeAcquireSpinLock(&SpinLock, &Irql);
  if (TagListLast == NULL) {
    TagList = NewTagList;
  } else {
    TagListLast->Next = NewTagList;
    NewTagList->Previous = TagListLast;
  }
  TagListLast = Tag;

  Irp->IoStatus.Information = 0;
  Irp->IoStatus.Status = STATUS_PENDING;
  IoMarkIrpPending(Irp);

  KeReleaseSpinLock(&SpinLock, Irql);
  KeSetEvent(&ThreadSignalEvent, 0, FALSE);
  return STATUS_PENDING;
}

NTSTATUS STDCALL AoEReply(IN PUCHAR SourceMac, IN PUCHAR DestinationMac, IN PUCHAR Data, IN UINT DataSize) {
  PAOE Reply = (PAOE)Data;
  LONGLONG LBASize;
  PTAG Tag;
  KIRQL Irql;
  BOOLEAN Found = FALSE;
  LARGE_INTEGER CurrentTime;

  if (!Reply->ResponseFlag) return STATUS_SUCCESS;

  if (ProbeTag->Id == Reply->Tag) {
    RtlCopyMemory(&LBASize, &Reply->Data[200], sizeof(LONGLONG));
    BusAddTarget(DestinationMac, SourceMac, ntohs(Reply->Major), Reply->Minor, LBASize);
    return STATUS_SUCCESS;
  }

  KeAcquireSpinLock(&SpinLock, &Irql);
  if (TagList == NULL) {
    KeReleaseSpinLock(&SpinLock, Irql);
    return STATUS_SUCCESS;
  }
  Tag = TagList;
  while (Tag != NULL) {
    if ((Tag->Id == Reply->Tag) && (Tag->PacketData->Major == Reply->Major) && (Tag->PacketData->Minor == Reply->Minor)) {
      Found = TRUE;
      break;
    }
    Tag = Tag->Next;
  }
  if (!Found) {
    KeReleaseSpinLock(&SpinLock, Irql);
    return STATUS_SUCCESS;
  } else {
    if (Tag->Previous == NULL) TagList = Tag->Next;
    else Tag->Previous->Next = Tag->Next;
    if (Tag->Next == NULL) TagListLast = Tag->Previous;
    else Tag->Next->Previous = Tag->Previous;
    OutstandingTags--;
    if (OutstandingTags < 0) DbgPrint("!!OutstandingTags < 0 (AoEReply)!!\n");
    KeSetEvent(&ThreadSignalEvent, 0, FALSE);
  }
  KeReleaseSpinLock(&SpinLock, Irql);

  if (RtlCompareMemory(Tag->DeviceExtension->Disk.ServerMac, "\xff\xff\xff\xff\xff\xff", 6) == 6) {
    RtlCopyMemory(Tag->DeviceExtension->Disk.ServerMac, SourceMac, 6);
    DbgPrint("Major: %d minor: %d found on server %02x:%02x:%02x:%02x:%02x:%02x\n", Tag->DeviceExtension->Disk.Major, Tag->DeviceExtension->Disk.Minor, SourceMac[0], SourceMac[1], SourceMac[2], SourceMac[3], SourceMac[4], SourceMac[5]);
  }

  KeQuerySystemTime(&CurrentTime);
  Tag->DeviceExtension->Disk.Timeout -= (ULONG)((Tag->DeviceExtension->Disk.Timeout - (CurrentTime.QuadPart - Tag->FirstSendTime.QuadPart)) / 1024);
  if (Tag->DeviceExtension->Disk.Timeout > 100000000) Tag->DeviceExtension->Disk.Timeout = 100000000;

  switch (Tag->Type) {
    case SearchDriveType:
      KeAcquireSpinLock(&Tag->DeviceExtension->Disk.SpinLock, &Irql);
      switch (Tag->DeviceExtension->Disk.SearchState) {
        case GettingSize:
          RtlCopyMemory(&Tag->DeviceExtension->Disk.LBADiskSize, &Reply->Data[200], sizeof(LONGLONG));
          Tag->DeviceExtension->Disk.SearchState = GetGeometry;
          break;
        case GettingGeometry:
          Tag->DeviceExtension->Disk.Heads = 255;       // FIXME use real values from partition table
          Tag->DeviceExtension->Disk.Sectors = 63;
          Tag->DeviceExtension->Disk.Cylinders = Tag->DeviceExtension->Disk.LBADiskSize / (Tag->DeviceExtension->Disk.Heads * Tag->DeviceExtension->Disk.Sectors);
          Tag->DeviceExtension->Disk.LBADiskSize = Tag->DeviceExtension->Disk.Cylinders * Tag->DeviceExtension->Disk.Heads * Tag->DeviceExtension->Disk.Sectors;
          Tag->DeviceExtension->Disk.SearchState = GetMaxSectorsPerPacket;
          break;
        case GettingMaxSectorsPerPacket:
          DataSize -= sizeof(AOE);
          if (DataSize < (Tag->DeviceExtension->Disk.MaxSectorsPerPacket * SECTORSIZE)) {
            DbgPrint("AoEReply Packet size too low while getting MaxSectorsPerPacket (tried %d, got size of %d)\n", Tag->DeviceExtension->Disk.MaxSectorsPerPacket, DataSize);
            Tag->DeviceExtension->Disk.MaxSectorsPerPacket--;
            Tag->DeviceExtension->Disk.SearchState = Done;
          } else if (Tag->DeviceExtension->Disk.MTU < (sizeof(AOE) + ((Tag->DeviceExtension->Disk.MaxSectorsPerPacket + 1) * SECTORSIZE))) {
            DbgPrint("AoEReply Got MaxSectorsPerPacket %d at size of %d. MTU of %d reached\n", Tag->DeviceExtension->Disk.MaxSectorsPerPacket, DataSize, Tag->DeviceExtension->Disk.MTU);
            Tag->DeviceExtension->Disk.SearchState = Done;
          } else {
            DbgPrint("AoEReply Got MaxSectorsPerPacket %d at size of %d, trying next...\n", Tag->DeviceExtension->Disk.MaxSectorsPerPacket, DataSize);
            Tag->DeviceExtension->Disk.SearchState = GetMaxSectorsPerPacket;
          }
          break;
        default:
          DbgPrint("AoEReply Undefined SearchState!\n");
          break;
      }
      KeReleaseSpinLock(&Tag->DeviceExtension->Disk.SpinLock, Irql);
      KeSetEvent(&Tag->DeviceExtension->Disk.SearchEvent, 0, FALSE);
      break;
    case RequestType:
      if (Tag->Request->Mode == Read) RtlCopyMemory(&Tag->Request->Buffer[Tag->BufferOffset], Reply->Data, Tag->SectorCount * SECTORSIZE);
      if (InterlockedDecrement(&Tag->Request->TagCount) == 0) {
        Tag->Request->Irp->IoStatus.Information = Tag->Request->SectorCount * SECTORSIZE;
        Tag->Request->Irp->IoStatus.Status = STATUS_SUCCESS;
        CompletePendingIrp(Tag->Request->Irp);
        ExFreePool(Tag->Request);
      }
      break;
    default:
      DbgPrint("Unknown tag type??\n");
      break;
  }

  KeSetEvent(&ThreadSignalEvent, 0, FALSE);
  ExFreePool(Tag->PacketData);
  ExFreePool(Tag);
  return STATUS_SUCCESS;
}

VOID STDCALL AoEResetProbe() {
  ProbeTag->SendTime.QuadPart = 0LL;
}

VOID STDCALL Thread(IN PVOID StartContext) {
  LARGE_INTEGER Timeout, CurrentTime, ProbeTime, ReportTime;
  ULONG NextTagId = 1;
  PTAG Tag;
  KIRQL Irql;
  ULONG Sends = 0;
  ULONG Resends = 0;
  ULONG ResendFails = 0;
  ULONG Fails = 0;
  ULONG RequestTimeout = 0;

  DbgPrint("Thread\n");
  ReportTime.QuadPart = 0LL;
  ProbeTime.QuadPart = 0LL;

  while (TRUE) {
    Timeout.QuadPart = -100000LL;               // 100.000 * 100ns = 10.000.000 ns = 10ms
    KeWaitForSingleObject(&ThreadSignalEvent, Executive, KernelMode, FALSE, &Timeout);
    KeResetEvent(&ThreadSignalEvent);
    if (Stop) {
      DbgPrint("Stopping thread...\n");
      PsTerminateSystemThread(STATUS_SUCCESS);
    }
    BusCleanupTargetList();

    KeQuerySystemTime(&CurrentTime);
    if (CurrentTime.QuadPart > (ReportTime.QuadPart + 10000000LL)) {
//      DbgPrint("Sends: %d  Resends: %d  ResendFails: %d  Fails: %d  OutstandingTags: %d  RequestTimeout: %d\n", Sends, Resends, ResendFails, Fails, OutstandingTags, RequestTimeout);
      Sends = 0;
      Resends = 0;
      ResendFails = 0;
      Fails = 0;
      KeQuerySystemTime(&ReportTime);
    }

    if (CurrentTime.QuadPart > (ProbeTag->SendTime.QuadPart + 100000000LL)) {
      ProbeTag->Id = NextTagId++;
      if (NextTagId == 0) NextTagId++;
      ProbeTag->PacketData->Tag = ProbeTag->Id;
      ProtocolSend("\xff\xff\xff\xff\xff\xff", "\xff\xff\xff\xff\xff\xff", (PUCHAR)ProbeTag->PacketData, ProbeTag->PacketSize, NULL);
      KeQuerySystemTime(&ProbeTag->SendTime);
    }

    KeAcquireSpinLock(&SpinLock, &Irql);
    if (TagList == NULL) {
      KeReleaseSpinLock(&SpinLock, Irql);
      continue;
    }
    Tag = TagList;
    while (Tag != NULL) {
      RequestTimeout = Tag->DeviceExtension->Disk.Timeout;
      if (Tag->Id == 0) {
        if (OutstandingTags <= 64) {
        //if (OutstandingTags <= 102400) {
          if (OutstandingTags < 0) DbgPrint("!!OutstandingTags < 0 (Thread)!!\n");
          Tag->Id = NextTagId++;
          if (NextTagId == 0) NextTagId++;
          Tag->PacketData->Tag = Tag->Id;
          if (ProtocolSend(Tag->DeviceExtension->Disk.ClientMac, Tag->DeviceExtension->Disk.ServerMac, (PUCHAR)Tag->PacketData, Tag->PacketSize, Tag)) {
            KeQuerySystemTime(&Tag->FirstSendTime);
            KeQuerySystemTime(&Tag->SendTime);
            OutstandingTags++;
	    Sends++;
          } else {
            Fails++;
            Tag->Id = 0;
            break;
          }
        }
      } else {
        KeQuerySystemTime(&CurrentTime);
        if (CurrentTime.QuadPart > (Tag->SendTime.QuadPart + (LONGLONG)(Tag->DeviceExtension->Disk.Timeout * 2))) {
          if (ProtocolSend(Tag->DeviceExtension->Disk.ClientMac, Tag->DeviceExtension->Disk.ServerMac, (PUCHAR)Tag->PacketData, Tag->PacketSize, Tag)) {
            KeQuerySystemTime(&Tag->SendTime);
            Tag->DeviceExtension->Disk.Timeout += Tag->DeviceExtension->Disk.Timeout / 1000;
            if (Tag->DeviceExtension->Disk.Timeout > 100000000) Tag->DeviceExtension->Disk.Timeout = 100000000;
            Resends++;
          } else {
            ResendFails++;
            break;
          }
        }
      }
      Tag = Tag->Next;
      if (Tag == TagList) {
        DbgPrint("!!Taglist Cyclic!!\n");
        break;
      }
    }
    KeReleaseSpinLock(&SpinLock, Irql);
  }
}
