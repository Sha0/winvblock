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

/* Yield a pointer to the RAM disk. */
#define WvRamdiskFromDev_(dev) \
  ((WV_SP_RAMDISK_T) (disk__get_ptr(dev))->ext)

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
    IN WV_SP_DISK_T disk,
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
static UINT32 STDCALL WvRamdiskQueryId_(
    IN WV_SP_DEV_T dev,
    IN BUS_QUERY_ID_TYPE query_type,
    IN OUT WCHAR (*buf)[512]
  ) {
    static const WCHAR * hw_ids[WvlDiskMediaTypes] = {
        WVL_M_WLIT L"\\RAMFloppyDisk",
        WVL_M_WLIT L"\\RAMHardDisk",
        WVL_M_WLIT L"\\RAMOpticalDisc"
      };
    WV_SP_DISK_T disk = disk__get_ptr(dev);
    WV_SP_RAMDISK_T ramdisk = WvRamdiskFromDev_(dev);

    switch (query_type) {
        case BusQueryDeviceID:
          return swprintf(*buf, hw_ids[disk->Media]) + 1;

        case BusQueryInstanceID:
        	/* "Location". */
          return swprintf(*buf, L"RAM_at_%08X", ramdisk->DiskBuf) + 1;

        case BusQueryHardwareIDs: {
            UINT32 tmp;

            tmp = swprintf(*buf, hw_ids[disk->Media]) + 1;
            tmp += swprintf(*buf + tmp, WvDiskCompatIds[disk->Media]) + 4;
            return tmp;
          }

        case BusQueryCompatibleIDs:
          return swprintf(*buf, WvDiskCompatIds[disk->Media]) + 4;

        default:
          return 0;
      }
  }

static WVL_F_DISK_UNIT_NUM WvRamdiskUnitNum_;
static UCHAR STDCALL WvRamdiskUnitNum_(IN WV_SP_DISK_T disk) {
    WV_SP_RAMDISK_T ramdisk = CONTAINING_RECORD(
        disk,
        WV_S_RAMDISK_T,
        disk[0]
      );

    /* Possible precision loss. */
    return (UCHAR) WvlBusGetNodeNum(&ramdisk->Dev->BusNode);
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
    static WV_S_DEV_IRP_MJ irp_mj = {
        WvDiskIrpPower,
        WvDiskIrpSysCtl,
        WvDiskDevCtl,
        WvDiskScsi,
        WvDiskPnp,
      };
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
    WvDiskInit(ramdisk->disk);
    WvDevInit(ramdisk->Dev);
    ramdisk->Dev->Ops.Free = WvRamdiskFree_;
    ramdisk->Dev->Ops.PnpId = WvRamdiskQueryId_;
    ramdisk->Dev->ext = ramdisk->disk;
    ramdisk->Dev->IrpMj = &irp_mj;
    ramdisk->disk->Dev = ramdisk->Dev;
    ramdisk->disk->disk_ops.Io = WvRamdiskIo_;
    ramdisk->disk->disk_ops.UnitNum = WvRamdiskUnitNum_;
    ramdisk->disk->ext = ramdisk;
    ramdisk->disk->DriverObj = WvDriverObj;

    /* Set associations for the PDO, device, disk. */
    WvDevForDevObj(pdo, ramdisk->Dev);
    KeInitializeEvent(
        &ramdisk->disk->SearchEvent,
        SynchronizationEvent,
        FALSE
      );
    KeInitializeSpinLock(&ramdisk->disk->SpinLock);
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
