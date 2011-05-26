/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 *
 * This file is part of WinVBlock, originally derived from WinAoE.
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
 * RAM disk specifics.
 */

#include <stdio.h>
#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "irp.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "ramdisk.h"
#include "debug.h"

/** Private. */
static WV_F_DEV_FREE WvRamdiskFree_;
static WVL_F_DISK_IO WvRamdiskIo_;

/* With thanks to karyonix, who makes FiraDisk. */
static __inline VOID STDCALL WvRamdiskFastCopy_(
    PVOID dest,
    const VOID * src,
    size_t count
  ) {
    __movsd(dest, src, count >> 2);
  }

/* RAM disk I/O routine. */
static NTSTATUS STDCALL WvRamdiskIo_(
    IN WVL_SP_DISK_T disk,
    IN WVL_E_DISK_IO_MODE mode,
    IN LONGLONG start_sector,
    IN UINT32 sector_count,
    IN PUCHAR buffer,
    IN PIRP irp
  ) {
    PHYSICAL_ADDRESS phys_addr;
    PUCHAR phys_mem;
    WV_SP_RAMDISK_T ramdisk;

    /* Establish pointer to the RAM disk. */
    ramdisk = CONTAINING_RECORD(disk, WV_S_RAMDISK_T, disk);

    if (sector_count < 1) {
        /* A silly request. */
        DBG("sector_count < 1; cancelling\n");
        irp->IoStatus.Information = 0;
        irp->IoStatus.Status = STATUS_CANCELLED;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return STATUS_CANCELLED;
      }

    phys_addr.QuadPart =
      ramdisk->DiskBuf + (start_sector * disk->SectorSize);
    /* Possible precision loss. */
    phys_mem = MmMapIoSpace(
        phys_addr,
        sector_count * disk->SectorSize,
  		  MmNonCached
      );
    if (!phys_mem) {
        DBG("Could not map memory for RAM disk!\n");
        return WvlIrpComplete(irp, 0, STATUS_INSUFFICIENT_RESOURCES);
      }
    if (mode == WvlDiskIoModeWrite)
      WvRamdiskFastCopy_(phys_mem, buffer, sector_count * disk->SectorSize);
      else
      WvRamdiskFastCopy_(buffer, phys_mem, sector_count * disk->SectorSize);
    MmUnmapIoSpace(phys_mem, sector_count * disk->SectorSize);
    return WvlIrpComplete(
        irp,
        sector_count * disk->SectorSize,
        STATUS_SUCCESS
      );
  }

/* Copy RAM disk IDs to the provided buffer. */
static NTSTATUS STDCALL WvRamdiskPnpQueryId_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    IN WVL_SP_DISK_T disk
  ) {
    static const WCHAR * hw_ids[WvlDiskMediaTypes] = {
        WVL_M_WLIT L"\\RAMFloppyDisk",
        WVL_M_WLIT L"\\RAMHardDisk",
        WVL_M_WLIT L"\\RAMOpticalDisc"
      };
    WCHAR (*buf)[512];
    NTSTATUS status;
    WV_SP_RAMDISK_T ramdisk = CONTAINING_RECORD(disk, WV_S_RAMDISK_T, disk);
    PWCHAR hw_id;
    BUS_QUERY_ID_TYPE query_type;

    /* Allocate a buffer. */
    buf = wv_mallocz(sizeof *buf);
    if (!buf) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_buf;
      }

    /* Populate the buffer with IDs. */
    query_type = IoGetCurrentIrpStackLocation(irp)->Parameters.QueryId.IdType;
    switch (query_type) {
        case BusQueryDeviceID:
          swprintf(*buf, hw_ids[disk->Media]);
          break;

        case BusQueryInstanceID:
        	/* "Location". */
          swprintf(*buf, L"RAM_at_%08X", ramdisk->DiskBuf);
          break;

        case BusQueryHardwareIDs:
          swprintf(
              *buf + swprintf(*buf, hw_ids[disk->Media]) + 1,
              WvlDiskCompatIds[disk->Media]
            );
          break;

        case BusQueryCompatibleIDs:
          swprintf(*buf, WvlDiskCompatIds[disk->Media]);
          break;

        default:
          DBG("Unknown query type %d for %p!\n", query_type, ramdisk);
          status = STATUS_INVALID_PARAMETER;
          goto err_query_type;
      }

    DBG("IRP_MN_QUERY_ID for RAM disk %p.\n", ramdisk);
    return WvlIrpComplete(irp, (ULONG_PTR) buf, STATUS_SUCCESS);

    err_query_type:

    wv_free(buf);
    err_buf:

    return WvlIrpComplete(irp, 0, status);
  }

static WVL_F_DISK_UNIT_NUM WvRamdiskUnitNum_;
static UCHAR STDCALL WvRamdiskUnitNum_(IN WVL_SP_DISK_T disk) {
    WV_SP_RAMDISK_T ramdisk = CONTAINING_RECORD(
        disk,
        WV_S_RAMDISK_T,
        disk[0]
      );

    /* Possible precision loss. */
    return (UCHAR) WvlBusGetNodeNum(&ramdisk->Dev->BusNode);
  }

/* Handle an IRP. */
static NTSTATUS WvRamdiskIrpDispatch(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc;
    WV_SP_RAMDISK_T ramdisk;
    NTSTATUS status;

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ramdisk = dev_obj->DeviceExtension;
    switch (io_stack_loc->MajorFunction) {
        case IRP_MJ_SCSI:
          return WvlDiskScsi(dev_obj, irp, ramdisk->disk);

        case IRP_MJ_PNP:
          status = WvlDiskPnp(dev_obj, irp, ramdisk->disk);
          /* Note any state change. */
          ramdisk->Dev->OldState = ramdisk->disk->OldState;
          ramdisk->Dev->State = ramdisk->disk->State;
          if (ramdisk->Dev->State == WvlDiskStateNotStarted) {
              if (!ramdisk->Dev->BusNode.Linked) {
                  /* Unlinked _and_ deleted */
                  DBG("Deleting RAM disk PDO: %p", dev_obj);
                  IoDeleteDevice(dev_obj);
                }
            }
          return status;

        case IRP_MJ_DEVICE_CONTROL:
          return WvlDiskDevCtl(
              ramdisk->disk,
              irp,
              io_stack_loc->Parameters.DeviceIoControl.IoControlCode
            );

        case IRP_MJ_POWER:
          return WvlDiskPower(dev_obj, irp, ramdisk->disk);

        case IRP_MJ_CREATE:
        case IRP_MJ_CLOSE:
          /* Always succeed with nothing to do. */
          return WvlIrpComplete(irp, 0, STATUS_SUCCESS);

        case IRP_MJ_SYSTEM_CONTROL:
          return WvlDiskSysCtl(dev_obj, irp, ramdisk->disk);

        default:
          DBG("Unhandled IRP_MJ_*: %d\n", io_stack_loc->MajorFunction);
      }
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
  }

/**
 * Create a RAM disk PDO of the given media type.
 *
 * @v MediaType                 The media type for the RAM disk.
 * @ret WV_SP_RAMDISK_T         Points to the new RAM disk, or NULL.
 *
 * Returns NULL if the PDO cannot be created.
 */
WV_SP_RAMDISK_T STDCALL WvRamdiskCreatePdo(
    IN WVL_E_DISK_MEDIA_TYPE MediaType
  ) {
    NTSTATUS status;
    WV_SP_RAMDISK_T ramdisk;
    PDEVICE_OBJECT pdo;
  
    DBG("Creating RAM disk PDO...\n");

    /* Create the disk PDO. */
    status = WvlDiskCreatePdo(
        WvDriverObj,
        sizeof *ramdisk,
        MediaType,
        &pdo
      );
    if (!NT_SUCCESS(status)) {
        WvlError("WvlDiskCreatePdo", status);
        return NULL;
      }

    ramdisk = pdo->DeviceExtension;
    RtlZeroMemory(ramdisk, sizeof *ramdisk);
    WvlDiskInit(ramdisk->disk);
    WvDevInit(ramdisk->Dev);
    ramdisk->Dev->Ops.Free = WvRamdiskFree_;
    ramdisk->Dev->ext = ramdisk->disk;
    ramdisk->disk->disk_ops.Io = WvRamdiskIo_;
    ramdisk->disk->disk_ops.UnitNum = WvRamdiskUnitNum_;
    ramdisk->disk->disk_ops.PnpQueryId = WvRamdiskPnpQueryId_;
    ramdisk->disk->disk_ops.PnpQueryDevText = WvDiskPnpQueryDevText;
    ramdisk->disk->ext = ramdisk;
    ramdisk->disk->DriverObj = WvDriverObj;

    /* Set associations for the PDO, device, disk. */
    WvDevForDevObj(pdo, ramdisk->Dev);
    WvDevSetIrpHandler(pdo, WvRamdiskIrpDispatch);
    ramdisk->Dev->Self = pdo;

    /* Some device parameters. */
    pdo->Flags |= DO_DIRECT_IO;         /* FIXME? */
    pdo->Flags |= DO_POWER_INRUSH;      /* FIXME? */

    DBG("New PDO: %p\n", pdo);
    return ramdisk;
  }

/**
 * Default RAM disk deletion operation.
 *
 * @v dev               Points to the RAM disk device to delete.
 */
static VOID STDCALL WvRamdiskFree_(IN WV_SP_DEV_T dev) {
    IoDeleteDevice(dev->Self);
  }
