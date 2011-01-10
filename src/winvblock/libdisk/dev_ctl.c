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
 * Disk Device Control IRP handling.
 */

#include <ntddk.h>
#include <scsi.h>
#include <srb.h>
#include <ntdddisk.h>
#include <ntddscsi.h>
#include <ntddstor.h>
#include <ntddvol.h>
#include <mountdev.h>

#include "portable.h"
#include "winvblock.h"
#include "irp.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "debug.h"

static NTSTATUS STDCALL WvlDiskDevCtlStorageQueryProp_(
    IN WVL_SP_DISK_T disk,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    PSTORAGE_PROPERTY_QUERY storage_prop_query = irp->AssociatedIrp.SystemBuffer;
    UINT32 copy_size;
    STORAGE_ADAPTER_DESCRIPTOR storage_adapter_desc;
    STORAGE_DEVICE_DESCRIPTOR storage_dev_desc;

    if (
        storage_prop_query->PropertyId == StorageAdapterProperty &&
        storage_prop_query->QueryType == PropertyStandardQuery
      ) {
        copy_size = (
            io_stack_loc->Parameters.DeviceIoControl.OutputBufferLength <
            sizeof (STORAGE_ADAPTER_DESCRIPTOR) ?
            io_stack_loc->Parameters.DeviceIoControl.OutputBufferLength :
            sizeof (STORAGE_ADAPTER_DESCRIPTOR)
          );
        storage_adapter_desc.Version = sizeof (STORAGE_ADAPTER_DESCRIPTOR);
        storage_adapter_desc.Size = sizeof (STORAGE_ADAPTER_DESCRIPTOR);
        storage_adapter_desc.MaximumTransferLength =
          WvlDiskMaxXferLen(disk);
        #if 0
        storage_adapter_desc.MaximumTransferLength = SECTORSIZE * POOLSIZE;
        #endif
        storage_adapter_desc.MaximumPhysicalPages = (UINT32) -1;
        storage_adapter_desc.AlignmentMask = 0;
        storage_adapter_desc.AdapterUsesPio = TRUE;
        storage_adapter_desc.AdapterScansDown = FALSE;
        storage_adapter_desc.CommandQueueing = FALSE;
        storage_adapter_desc.AcceleratedTransfer = FALSE;
        storage_adapter_desc.BusType = BusTypeScsi;
        RtlCopyMemory(
            irp->AssociatedIrp.SystemBuffer,
            &storage_adapter_desc,
            copy_size
          );
        status = STATUS_SUCCESS;
      }

    if (
        storage_prop_query->PropertyId == StorageDeviceProperty &&
        storage_prop_query->QueryType == PropertyStandardQuery
      ) {
        copy_size = (
            io_stack_loc->Parameters.DeviceIoControl.OutputBufferLength <
            sizeof (STORAGE_DEVICE_DESCRIPTOR) ?
            io_stack_loc->Parameters.DeviceIoControl.OutputBufferLength :
            sizeof (STORAGE_DEVICE_DESCRIPTOR)
          );
        storage_dev_desc.Version = sizeof (STORAGE_DEVICE_DESCRIPTOR);
        storage_dev_desc.Size = sizeof (STORAGE_DEVICE_DESCRIPTOR);
        storage_dev_desc.DeviceType = DIRECT_ACCESS_DEVICE;
        storage_dev_desc.DeviceTypeModifier = 0;
        storage_dev_desc.RemovableMedia = WvlDiskIsRemovable[disk->Media];
        storage_dev_desc.CommandQueueing = FALSE;
        storage_dev_desc.VendorIdOffset = 0;
        storage_dev_desc.ProductIdOffset = 0;
        storage_dev_desc.ProductRevisionOffset = 0;
        storage_dev_desc.SerialNumberOffset = 0;
        storage_dev_desc.BusType = BusTypeScsi;
        storage_dev_desc.RawPropertiesLength = 0;
        RtlCopyMemory(
            irp->AssociatedIrp.SystemBuffer,
            &storage_dev_desc,
            copy_size
          );
        status = STATUS_SUCCESS;
      }

    if (status == STATUS_INVALID_PARAMETER) {
        DBG(
            "!!Invalid IOCTL_STORAGE_QUERY_PROPERTY "
              "(PropertyId: %08x / QueryType: %08x)!!\n",
            storage_prop_query->PropertyId,
            storage_prop_query->QueryType
          );
        return WvlIrpComplete(irp, 0, status);
      }
    return WvlIrpComplete(irp, (ULONG_PTR) copy_size, status);
  }

static NTSTATUS STDCALL WvlDiskDevCtlGetGeom_(
    IN WVL_SP_DISK_T disk,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    UINT32 copy_size;
    DISK_GEOMETRY disk_geom;

    copy_size = (
        io_stack_loc->Parameters.DeviceIoControl.OutputBufferLength <
        sizeof (DISK_GEOMETRY) ?
        io_stack_loc->Parameters.DeviceIoControl.OutputBufferLength :
        sizeof (DISK_GEOMETRY)
      );
    disk_geom.MediaType = FixedMedia;
    disk_geom.Cylinders.QuadPart = disk->Cylinders;
    disk_geom.TracksPerCylinder = disk->Heads;
    disk_geom.SectorsPerTrack = disk->Sectors;
    disk_geom.BytesPerSector = disk->SectorSize;
    RtlCopyMemory(
        irp->AssociatedIrp.SystemBuffer,
        &disk_geom,
        copy_size
      );
    return WvlIrpComplete(irp, (ULONG_PTR) copy_size, STATUS_SUCCESS);
  }

static NTSTATUS STDCALL WvlDiskDevCtlScsiGetAddr_(
    IN WVL_SP_DISK_T disk,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc= IoGetCurrentIrpStackLocation(irp);
    UINT32 copy_size;
    SCSI_ADDRESS scsi_address;

    copy_size = (
        io_stack_loc->Parameters.DeviceIoControl.OutputBufferLength <
        sizeof (SCSI_ADDRESS) ?
        io_stack_loc->Parameters.DeviceIoControl.OutputBufferLength :
        sizeof (SCSI_ADDRESS)
      );
    scsi_address.Length = sizeof (SCSI_ADDRESS);
    scsi_address.PortNumber = 0;
    scsi_address.PathId = 0;
    scsi_address.TargetId = WvlDiskUnitNum(disk);
    scsi_address.Lun = 0;
    RtlCopyMemory(
        irp->AssociatedIrp.SystemBuffer,
        &scsi_address,
        copy_size
      );
    return WvlIrpComplete(irp, (ULONG_PTR) copy_size, STATUS_SUCCESS);
  }

/**
 * Handle a disk IRP_MJ_DEVICE_CONTROL IRP.
 *
 * @v Disk              The disk to process with the IRP.
 * @v Irp               The IRP to process with the disk.
 * @v Code              The device control code.
 * @ret NTSTATUS        The status of the operation.
 */
WVL_M_LIB NTSTATUS STDCALL WvlDiskDevCtl(
    IN WVL_SP_DISK_T Disk,
    IN PIRP Irp,
    IN ULONG POINTER_ALIGNMENT Code
  ) {
    switch (Code) {
        case IOCTL_STORAGE_QUERY_PROPERTY:
          return WvlDiskDevCtlStorageQueryProp_(Disk, Irp);

        case IOCTL_DISK_GET_DRIVE_GEOMETRY:
          return WvlDiskDevCtlGetGeom_(Disk, Irp);

        case IOCTL_SCSI_GET_ADDRESS:
          return WvlDiskDevCtlScsiGetAddr_(Disk, Irp);

        /* Some cases that pop up on Windows Server 2003. */
        #if 0
        case IOCTL_MOUNTDEV_UNIQUE_ID_CHANGE_NOTIFY:
        case IOCTL_MOUNTDEV_LINK_CREATED:
        case IOCTL_MOUNTDEV_QUERY_STABLE_GUID:
        case IOCTL_VOLUME_ONLINE:
          return WvlIrpComplete(Irp, 0, STATUS_SUCCESS);
        #endif
      }

    return WvlIrpComplete(Irp, 0, STATUS_INVALID_PARAMETER);
  }
