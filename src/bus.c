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
#include "driver.h"
#include "aoe.h"
#include "mount.h"

// in this file
BOOLEAN STDCALL BusAddChild(IN PDEVICE_OBJECT BusDeviceObject, IN PUCHAR ClientMac, IN ULONG Major, IN ULONG Minor, IN BOOLEAN Boot);
NTSTATUS STDCALL IoCompletionRoutine(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PKEVENT Event);

#ifdef _MSC_VER
#pragma pack(1)
#endif
typedef struct _ABFT {
  UINT Signature;               // 0x54464261 (aBFT)
  UINT Length;
  UCHAR Revision;
  UCHAR Checksum;
  UCHAR OEMID[6];
  UCHAR OEMTableID[8];
  UCHAR Reserved1[12];
  USHORT Major;
  UCHAR Minor;
  UCHAR Reserved2;
  UCHAR ClientMac[6];
} __attribute__((__packed__)) ABFT, *PABFT;
#ifdef _MSC_VER
#pragma pack()
#endif

typedef struct _TARGETLIST {
  TARGET Target;
  struct _TARGETLIST *Next;
} TARGETLIST, *PTARGETLIST;

PTARGETLIST TargetList = NULL;
KSPIN_LOCK TargetListSpinLock;
ULONG NextDisk = 0;

NTSTATUS STDCALL BusStart() {
  DbgPrint("BusStart\n");
  KeInitializeSpinLock(&TargetListSpinLock);
  return STATUS_SUCCESS;
}

VOID STDCALL BusStop() {
  UNICODE_STRING DosDeviceName;
  PTARGETLIST Walker, Next;
  KIRQL Irql;

  DbgPrint("BusStop\n");
  KeAcquireSpinLock(&TargetListSpinLock, &Irql);
  Walker = TargetList;
  while (Walker != NULL) {
    Next = Walker->Next;
    ExFreePool(Walker);
    Walker = Next;
  }
  KeReleaseSpinLock(&TargetListSpinLock, Irql);
  RtlInitUnicodeString(&DosDeviceName, L"\\DosDevices\\AoE");
  IoDeleteSymbolicLink(&DosDeviceName);
}

NTSTATUS STDCALL BusAddDevice(IN PDRIVER_OBJECT DriverObject, IN PDEVICE_OBJECT PhysicalDeviceObject) {
  NTSTATUS Status;
  PHYSICAL_ADDRESS PhysicalAddress;
  PUCHAR PhysicalMemory;
  UINT Offset, Checksum, i;
  ABFT AOEBootRecord;
  BOOLEAN FoundAbft = FALSE;
  UNICODE_STRING DeviceName, DosDeviceName;
  PDEVICEEXTENSION DeviceExtension;
  PDEVICE_OBJECT DeviceObject;

  DbgPrint("BusAddDevice\n");
  RtlInitUnicodeString(&DeviceName, L"\\Device\\AoE");
  RtlInitUnicodeString(&DosDeviceName, L"\\DosDevices\\AoE");
  if (!NT_SUCCESS(Status = IoCreateDevice(DriverObject, sizeof(DEVICEEXTENSION), &DeviceName, FILE_DEVICE_CONTROLLER, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject))) {
    return Error("BusAddDevice IoCreateDevice", Status);
  }
  if (!NT_SUCCESS(Status = IoCreateSymbolicLink(&DosDeviceName, &DeviceName))) {
    IoDeleteDevice(DeviceObject);
    return Error("BusAddDevice IoCreateSymbolicLink", Status);
  }

  DeviceExtension = (PDEVICEEXTENSION)DeviceObject->DeviceExtension;
  RtlZeroMemory(DeviceExtension, sizeof(DEVICEEXTENSION));
  DeviceExtension->IsBus = TRUE;
  DeviceExtension->DriverObject = DriverObject;
  DeviceExtension->Self = DeviceObject;
  DeviceExtension->State = NotStarted;
  DeviceExtension->OldState = NotStarted;
  DeviceExtension->Bus.PhysicalDeviceObject = PhysicalDeviceObject;
  DeviceExtension->Bus.Children = 0;
  DeviceExtension->Bus.ChildList = NULL;
  KeInitializeSpinLock(&DeviceExtension->Bus.SpinLock);
  DeviceObject->Flags |= DO_DIRECT_IO;                  // FIXME?
  DeviceObject->Flags |= DO_POWER_INRUSH;               // FIXME?
  if (PhysicalDeviceObject != NULL) {
    if ((DeviceExtension->Bus.LowerDeviceObject = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject)) == NULL) {
      IoDeleteDevice(DeviceObject);
      return Error("AddDevice IoAttachDeviceToDeviceStack", STATUS_NO_SUCH_DEVICE);
    }
  }
  DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

  PhysicalAddress.QuadPart = 0LL;
  PhysicalMemory = MmMapIoSpace(PhysicalAddress, 0xa0000, MmNonCached);
  if (!PhysicalMemory) {
    DbgPrint("Could not map low memory\n");
  } else {
    for (Offset = 0; Offset < 0xa0000; Offset += 0x10) {
      if (((PABFT)&PhysicalMemory[Offset])->Signature == 0x54464261) {
        Checksum = 0;
        for (i = 0; i < ((PABFT)&PhysicalMemory[Offset])->Length; i++) Checksum += PhysicalMemory[Offset + i];
        if (Checksum & 0xff) continue;
        if (((PABFT)&PhysicalMemory[Offset])->Revision != 1) {
          DbgPrint("Found aBFT with mismatched revision v%d at segment 0x%4x. want v1.\n", ((PABFT)&PhysicalMemory[Offset])->Revision, (Offset / 0x10));
          continue;
        }
        DbgPrint("Found aBFT at segment: 0x%04x\n", (Offset / 0x10));
        RtlCopyMemory(&AOEBootRecord, &PhysicalMemory[Offset], sizeof(ABFT));
        FoundAbft = TRUE;
        break;
      }
    }
    MmUnmapIoSpace(PhysicalMemory, 0xa0000);
  }

#ifdef RIS
  FoundAbft = TRUE;
  RtlCopyMemory(AOEBootRecord.ClientMac, "\x00\x0c\x29\x34\x69\x34", 6);
  AOEBootRecord.Major = 0;
  AOEBootRecord.Minor = 10;
#endif

  if (FoundAbft) {
    DbgPrint("Boot from client NIC %02x:%02x:%02x:%02x:%02x:%02x to major: %d minor: %d\n", AOEBootRecord.ClientMac[0], AOEBootRecord.ClientMac[1], AOEBootRecord.ClientMac[2], AOEBootRecord.ClientMac[3], AOEBootRecord.ClientMac[4], AOEBootRecord.ClientMac[5], AOEBootRecord.Major, AOEBootRecord.Minor);
    if (!BusAddChild(DeviceObject, AOEBootRecord.ClientMac, AOEBootRecord.Major, AOEBootRecord.Minor, TRUE)) {
      DbgPrint("BusAddChild failed\n");
    } else {
      if (DeviceExtension->Bus.PhysicalDeviceObject != NULL) IoInvalidateDeviceRelations(DeviceExtension->Bus.PhysicalDeviceObject, BusRelations);
    }
  } else {
    DbgPrint("Not booting...\n");
  }
#ifdef RIS
  DeviceExtension->State = Started;
#endif
  return STATUS_SUCCESS;
}

VOID STDCALL BusAddTarget(IN PUCHAR ClientMac, IN PUCHAR ServerMac, USHORT Major, UCHAR Minor, LONGLONG LBASize) {
  PTARGETLIST Walker, Last;
  KIRQL Irql;

  KeAcquireSpinLock(&TargetListSpinLock, &Irql);
  Last = TargetList;
  Walker = TargetList;
  while (Walker != NULL) {
    if ((RtlCompareMemory(&Walker->Target.ClientMac, ClientMac, 6) == 6) && (RtlCompareMemory(&Walker->Target.ServerMac, ServerMac, 6) == 6) && Walker->Target.Major == Major && Walker->Target.Minor == Minor) {
      if (Walker->Target.LBASize != LBASize) {
        DbgPrint("BusAddTarget LBASize changed for e%d.%d (%I64u->%I64u)\n", Major, Minor, Walker->Target.LBASize, LBASize);
        Walker->Target.LBASize = LBASize;
      }
      KeQuerySystemTime(&Walker->Target.ProbeTime);
      KeReleaseSpinLock(&TargetListSpinLock, Irql);
      return;
    }
    Last = Walker;
    Walker = Walker->Next;
  }

  if ((Walker = (PTARGETLIST)ExAllocatePool(NonPagedPool, sizeof(TARGETLIST))) == NULL) {
    DbgPrint("BusAddTarget ExAllocatePool Target\n");
    KeReleaseSpinLock(&TargetListSpinLock, Irql);
    return;
  }
  Walker->Next = NULL;
  RtlCopyMemory(Walker->Target.ClientMac, ClientMac, 6);
  RtlCopyMemory(Walker->Target.ServerMac, ServerMac, 6);
  Walker->Target.Major = Major;
  Walker->Target.Minor = Minor;
  Walker->Target.LBASize = LBASize;
  KeQuerySystemTime(&Walker->Target.ProbeTime);

  if (Last == NULL) {
    TargetList = Walker;
  } else {
    Last->Next = Walker;
  }
  KeReleaseSpinLock(&TargetListSpinLock, Irql);
}

VOID STDCALL BusCleanupTargetList() {
}

BOOLEAN STDCALL BusAddChild(IN PDEVICE_OBJECT BusDeviceObject, IN PUCHAR ClientMac, IN ULONG Major, IN ULONG Minor, IN BOOLEAN Boot) {
  NTSTATUS Status;
  PDEVICEEXTENSION BusDeviceExtension = (PDEVICEEXTENSION)BusDeviceObject->DeviceExtension;
  PDEVICE_OBJECT DeviceObject;
  PDEVICEEXTENSION DeviceExtension, Walker;

  DbgPrint("BusAddChild\n");
  if (!NT_SUCCESS(Status = IoCreateDevice(BusDeviceExtension->DriverObject, sizeof(DEVICEEXTENSION), NULL, FILE_DEVICE_DISK, FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject))) {
    Error("BusAddChild IoCreateDevice", Status);
    return FALSE;
  }
  DeviceExtension = (PDEVICEEXTENSION)DeviceObject->DeviceExtension;
  RtlZeroMemory(DeviceExtension, sizeof(DEVICEEXTENSION));

  DeviceExtension->IsBus = FALSE;
  DeviceExtension->Self = DeviceObject;
  DeviceExtension->DriverObject = BusDeviceExtension->DriverObject;
  DeviceExtension->State = NotStarted;
  DeviceExtension->OldState = NotStarted;

  DeviceExtension->Disk.Parent = BusDeviceObject;
  DeviceExtension->Disk.Next = NULL;
  KeInitializeEvent(&DeviceExtension->Disk.SearchEvent, SynchronizationEvent, FALSE);
  KeInitializeSpinLock(&DeviceExtension->Disk.SpinLock);
  DeviceExtension->Disk.BootDrive = Boot;
  DeviceExtension->Disk.Unmount = FALSE;
  DeviceExtension->Disk.DiskNumber = InterlockedIncrement(&NextDisk) - 1;
  RtlCopyMemory(DeviceExtension->Disk.ClientMac, ClientMac, 6);
  RtlFillMemory(DeviceExtension->Disk.ServerMac, 6, 0xff);
  DeviceExtension->Disk.Major = Major;
  DeviceExtension->Disk.Minor = Minor;
  DeviceExtension->Disk.MaxSectorsPerPacket = 1;
  DeviceExtension->Disk.Timeout = 200000;               // 20 ms.

  DeviceObject->Flags |= DO_DIRECT_IO;                  // FIXME?
  DeviceObject->Flags |= DO_POWER_INRUSH;               // FIXME?
  AoESearchDrive(DeviceExtension);
  DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
  if (BusDeviceExtension->Bus.ChildList == NULL) {
    BusDeviceExtension->Bus.ChildList = DeviceExtension;
  } else {
    Walker = BusDeviceExtension->Bus.ChildList;
    while (Walker->Disk.Next != NULL) Walker = Walker->Disk.Next;
    Walker->Disk.Next = DeviceExtension;
  }
  BusDeviceExtension->Bus.Children++;
  return TRUE;
}

NTSTATUS STDCALL BusDispatchPnP(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PIO_STACK_LOCATION Stack, IN PDEVICEEXTENSION DeviceExtension) {
  NTSTATUS Status;
  KEVENT Event;
  PDEVICE_RELATIONS DeviceRelations;
  PDEVICEEXTENSION Walker, Next;
  ULONG Count;

  switch (Stack->MinorFunction) {
    case IRP_MN_START_DEVICE:
      KeInitializeEvent(&Event, NotificationEvent, FALSE);
      IoCopyCurrentIrpStackLocationToNext(Irp);
      IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE)IoCompletionRoutine, (PVOID)&Event, TRUE, TRUE, TRUE);
      Status = IoCallDriver(DeviceExtension->Bus.LowerDeviceObject, Irp);
      if (Status == STATUS_PENDING) {
        DbgPrint("Locked\n");
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
      }
      if (NT_SUCCESS(Status = Irp->IoStatus.Status)) {
        DeviceExtension->OldState = DeviceExtension->State;
        DeviceExtension->State = Started;
      }
      Status = STATUS_SUCCESS;
      Irp->IoStatus.Status = Status;
      IoCompleteRequest(Irp, IO_NO_INCREMENT);
      return Status;
    case IRP_MN_REMOVE_DEVICE:
      DeviceExtension->OldState = DeviceExtension->State;
      DeviceExtension->State = Deleted;
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_SUCCESS;
      IoSkipCurrentIrpStackLocation(Irp);
      Status = IoCallDriver(DeviceExtension->Bus.LowerDeviceObject, Irp);
      Walker = DeviceExtension->Bus.ChildList;
      while (Walker != NULL) {
        Next = Walker->Disk.Next;
        IoDeleteDevice(Walker->Self);
        Walker = Next;
      }
      DeviceExtension->Bus.Children = 0;
      DeviceExtension->Bus.ChildList = NULL;
      IoDetachDevice(DeviceExtension->Bus.LowerDeviceObject);
      IoDeleteDevice(DeviceExtension->Self);
      return Status;
    case IRP_MN_QUERY_DEVICE_RELATIONS:
      if (Stack->Parameters.QueryDeviceRelations.Type != BusRelations || Irp->IoStatus.Information) {
        Status = Irp->IoStatus.Status;
        break;
      }
      Count = 0;
      Walker = DeviceExtension->Bus.ChildList;
      while (Walker != NULL) {
        Count++;
        Walker = Walker->Disk.Next;
      }

      if ((DeviceRelations = (PDEVICE_RELATIONS)ExAllocatePool(NonPagedPool, sizeof(DEVICE_RELATIONS) + (sizeof(PDEVICE_OBJECT) * Count))) == NULL) {
        Irp->IoStatus.Information = 0;
        Status = STATUS_INSUFFICIENT_RESOURCES;
        break;
      }
      DeviceRelations->Count = Count;

      Count = 0;
      Walker = DeviceExtension->Bus.ChildList;
      while (Walker != NULL) {
        DeviceRelations->Objects[Count] = Walker->Self;
        Count++;
        Walker = Walker->Disk.Next;
      }
      Irp->IoStatus.Information = (ULONG_PTR)DeviceRelations;
      Status = STATUS_SUCCESS;
      break;
    case IRP_MN_QUERY_PNP_DEVICE_STATE:
      Irp->IoStatus.Information = 0;
      Status = STATUS_SUCCESS;
      break;
    case IRP_MN_QUERY_STOP_DEVICE:
      DeviceExtension->OldState = DeviceExtension->State;
      DeviceExtension->State = StopPending;
      Status = STATUS_SUCCESS;
      break;
    case IRP_MN_CANCEL_STOP_DEVICE:
      DeviceExtension->State = DeviceExtension->OldState;
      Status = STATUS_SUCCESS;
      break;
    case IRP_MN_STOP_DEVICE:
      DeviceExtension->OldState = DeviceExtension->State;
      DeviceExtension->State = Stopped;
      Status = STATUS_SUCCESS;
      break;
    case IRP_MN_QUERY_REMOVE_DEVICE:
      DeviceExtension->OldState = DeviceExtension->State;
      DeviceExtension->State = RemovePending;
      Status = STATUS_SUCCESS;
      break;
    case IRP_MN_CANCEL_REMOVE_DEVICE:
      DeviceExtension->State = DeviceExtension->OldState;
      Status = STATUS_SUCCESS;
      break;
    case IRP_MN_SURPRISE_REMOVAL:
      DeviceExtension->OldState = DeviceExtension->State;
      DeviceExtension->State = SurpriseRemovePending;
      Status = STATUS_SUCCESS;
      break;
    default:
      Status = Irp->IoStatus.Status;
  }

  Irp->IoStatus.Status = Status;
  IoSkipCurrentIrpStackLocation(Irp);
  Status = IoCallDriver(DeviceExtension->Bus.LowerDeviceObject, Irp);
  return Status;
}

NTSTATUS STDCALL BusDispatchDeviceControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PIO_STACK_LOCATION Stack, IN PDEVICEEXTENSION DeviceExtension) {
  NTSTATUS Status;
  PUCHAR Buffer;
  ULONG Count;
  PTARGETLIST TargetWalker;
  PDEVICEEXTENSION DiskWalker, DiskWalkerPrevious;
  PTARGETS Targets;
  PDISKS Disks;
  KIRQL Irql;

  switch (Stack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_AOE_SCAN:
      DbgPrint("Got IOCTL_AOE_SCAN...\n");
      KeAcquireSpinLock(&TargetListSpinLock, &Irql);

      Count = 0;
      TargetWalker = TargetList;
      while (TargetWalker != NULL) {
        Count++;
        TargetWalker = TargetWalker->Next;
      }

      if ((Targets = (PTARGETS)ExAllocatePool(NonPagedPool, sizeof(TARGETS) + (Count * sizeof(TARGET)))) == NULL) {
        DbgPrint("BusDispatchDeviceControl ExAllocatePool Targets\n");
        Irp->IoStatus.Information = 0;
        Status = STATUS_INSUFFICIENT_RESOURCES;
        break;
      }
      Irp->IoStatus.Information = sizeof(TARGETS) + (Count * sizeof(TARGET));
      Targets->Count = Count;

      Count = 0;
      TargetWalker = TargetList;
      while (TargetWalker != NULL) {
        RtlCopyMemory(&Targets->Target[Count], &TargetWalker->Target, sizeof(TARGET));
        Count++;
        TargetWalker = TargetWalker->Next;
      }
      RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, Targets, (Stack->Parameters.DeviceIoControl.OutputBufferLength < (sizeof(TARGETS) + (Count * sizeof(TARGET)))?Stack->Parameters.DeviceIoControl.OutputBufferLength:(sizeof(TARGETS) + (Count * sizeof(TARGET)))));
      ExFreePool(Targets);

      KeReleaseSpinLock(&TargetListSpinLock, Irql);
      Status = STATUS_SUCCESS;
      break;
    case IOCTL_AOE_SHOW:
      DbgPrint("Got IOCTL_AOE_SHOW...\n");

      Count = 0;
      DiskWalker = DeviceExtension->Bus.ChildList;
      while (DiskWalker != NULL) {
        Count++;
        DiskWalker = DiskWalker->Disk.Next;
      }

      if ((Disks = (PDISKS)ExAllocatePool(NonPagedPool, sizeof(DISKS) + (Count * sizeof(DISK)))) == NULL) {
        DbgPrint("BusDispatchDeviceControl ExAllocatePool Disks\n");
        Irp->IoStatus.Information = 0;
        Status = STATUS_INSUFFICIENT_RESOURCES;
        break;
      }
      Irp->IoStatus.Information = sizeof(DISKS) + (Count * sizeof(DISK));
      Disks->Count = Count;

      Count = 0;
      DiskWalker = DeviceExtension->Bus.ChildList;
      while (DiskWalker != NULL) {
        Disks->Disk[Count].Disk = DiskWalker->Disk.DiskNumber;
        RtlCopyMemory(&Disks->Disk[Count].ClientMac, &DiskWalker->Disk.ClientMac, 6);
        RtlCopyMemory(&Disks->Disk[Count].ServerMac, &DiskWalker->Disk.ServerMac, 6);
        Disks->Disk[Count].Major = DiskWalker->Disk.Major;
        Disks->Disk[Count].Minor = DiskWalker->Disk.Minor;
        Disks->Disk[Count].LBASize = DiskWalker->Disk.LBADiskSize;
        Count++;
        DiskWalker = DiskWalker->Disk.Next;
      }
      RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, Disks, (Stack->Parameters.DeviceIoControl.OutputBufferLength < (sizeof(DISKS) + (Count * sizeof(DISK)))?Stack->Parameters.DeviceIoControl.OutputBufferLength:(sizeof(DISKS) + (Count * sizeof(DISK)))));
      ExFreePool(Disks);

      Status = STATUS_SUCCESS;
      break;
    case IOCTL_AOE_MOUNT:
      Buffer = Irp->AssociatedIrp.SystemBuffer;
      DbgPrint("Got IOCTL_AOE_MOUNT for client: %02x:%02x:%02x:%02x:%02x:%02x Major:%d Minor:%d\n", Buffer[0], Buffer[1], Buffer[2], Buffer[3], Buffer[4], Buffer[5], *(PUSHORT)(&Buffer[6]), (UCHAR)Buffer[8]);
      if (!BusAddChild(DeviceObject, Buffer, *(PUSHORT)(&Buffer[6]), (UCHAR)Buffer[8], FALSE)) {
        DbgPrint("BusAddChild failed\n");
      } else {
        if (DeviceExtension->Bus.PhysicalDeviceObject != NULL) IoInvalidateDeviceRelations(DeviceExtension->Bus.PhysicalDeviceObject, BusRelations);
      }
      Irp->IoStatus.Information = 0;
      Status = STATUS_SUCCESS;
      break;
    case IOCTL_AOE_UMOUNT:
      Buffer = Irp->AssociatedIrp.SystemBuffer;
      DbgPrint("Got IOCTL_AOE_UMOUNT for disk: %d\n", *(PULONG)Buffer);
      DiskWalker = DeviceExtension->Bus.ChildList;
      DiskWalkerPrevious = DiskWalker;
      while ((DiskWalker != NULL) && (!DiskWalker->Disk.BootDrive) && (DiskWalker->Disk.DiskNumber != *(PULONG)Buffer)) {
        DiskWalkerPrevious = DiskWalker;
        DiskWalker = DiskWalker->Disk.Next;
      }
      if (DiskWalker != NULL) {
        DbgPrint("Deleting disk %d\n", DiskWalker->Disk.DiskNumber);
        if (DiskWalker == DeviceExtension->Bus.ChildList) {
          DeviceExtension->Bus.ChildList = DiskWalker->Disk.Next;
        } else {
          DiskWalkerPrevious->Disk.Next = DiskWalker->Disk.Next;
        }
        DiskWalker->Disk.Unmount = TRUE;
        DiskWalker->Disk.Next = NULL;
        if (DeviceExtension->Bus.PhysicalDeviceObject != NULL) IoInvalidateDeviceRelations(DeviceExtension->Bus.PhysicalDeviceObject, BusRelations);
      }
      DeviceExtension->Bus.Children--;
      Irp->IoStatus.Information = 0;
      Status = STATUS_SUCCESS;
      break;
    default:
      Irp->IoStatus.Information = 0;
      Status = STATUS_INVALID_DEVICE_REQUEST;
  }

  Irp->IoStatus.Status = Status;
  IoCompleteRequest(Irp, IO_NO_INCREMENT);
  return Status;
}

NTSTATUS STDCALL BusDispatchSystemControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PIO_STACK_LOCATION Stack, IN PDEVICEEXTENSION DeviceExtension) {
  DbgPrint("...\n");
  IoSkipCurrentIrpStackLocation(Irp);
  return IoCallDriver(DeviceExtension->Bus.LowerDeviceObject, Irp);
}

NTSTATUS STDCALL IoCompletionRoutine(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PKEVENT Event) {
  KeSetEvent(Event, 0, FALSE);
  return STATUS_MORE_PROCESSING_REQUIRED;
}
