/**
 * Copyright (C) 2016, Synthetel Corporation.
 *   Author: Shao Miller <winvblock@synthetel.com>
 * Copyright (C) 2012, Shao Miller  <sha0.miller@gmail.com>.
 * Portions copyright (C) 2006 - 2008, V.
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
 * File-backed disk PDO SCSI IRP handling
 */

#include <ntifs.h>
#include <ntddk.h>
#include <ntifs.h>
#include <scsi.h>

#include "portable.h"
#include "winvblock.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "thread.h"
#include "filedisk.h"
#include "debug.h"

/* For portability */
#if _WIN32_WINNT <= 0x0600
#  if 0        /* FIXME: To build with WINDDK 6001.18001 */
#    ifdef _MSC_VER
#      pragma pack(1)
#    endif
union _EIGHT_BYTE {
    struct {
        UCHAR Byte0;
        UCHAR Byte1;
        UCHAR Byte2;
        UCHAR Byte3;
        UCHAR Byte4;
        UCHAR Byte5;
        UCHAR Byte6;
        UCHAR Byte7;
      };
    ULONGLONG AsULongLong;
  } __attribute__((__packed__));
typedef union _EIGHT_BYTE EIGHT_BYTE, * PEIGHT_BYTE;
#    ifdef _MSC_VER
#      pragma pack()
#    endif
#  endif      /* To build with WINDDK 6001.18001 */

#  if _WIN32_WINNT < 0x0500
#    if 0      /* FIXME: To build with WINDDK 6001.18001 */
#      ifdef _MSC_VER
#        pragma pack(1)
#      endif
struct _READ_CAPACITY_DATA_EX {
    LARGE_INTEGER LogicalBlockAddress;
    ULONG BytesPerBlock;
  } __attribute__((__packed__));
typedef struct _READ_CAPACITY_DATA_EX
  READ_CAPACITY_DATA_EX, * PREAD_CAPACITY_DATA_EX;
#      ifdef _MSC_VER
#        pragma pack()
#      endif
#    endif      /* To build with WINDDK 6001.18001 */
#  endif      /* _WIN32_WINNT < 0x0500 */

#  ifdef _MSC_VER
#    pragma pack(1)
#  endif
struct _DISK_CDB16 {
    UCHAR OperationCode;
    UCHAR Reserved1:3;
    UCHAR ForceUnitAccess:1;
    UCHAR DisablePageOut:1;
    UCHAR Protection:3;
    UCHAR LogicalBlock[8];
    UCHAR TransferLength[4];
    UCHAR Reserved2;
    UCHAR Control;
  } __attribute__((__packed__));
typedef struct _DISK_CDB16 DISK_CDB16, * PDISK_CDB16;
#  ifdef _MSC_VER
#    pragma pack()
#  endif

#  define REVERSE_BYTES_QUAD(Destination, Source) { \
  PEIGHT_BYTE d = (PEIGHT_BYTE)(Destination);     \
  PEIGHT_BYTE s = (PEIGHT_BYTE)(Source);          \
  d->Byte7 = s->Byte0;                            \
  d->Byte6 = s->Byte1;                            \
  d->Byte5 = s->Byte2;                            \
  d->Byte4 = s->Byte3;                            \
  d->Byte3 = s->Byte4;                            \
  d->Byte2 = s->Byte5;                            \
  d->Byte1 = s->Byte6;                            \
  d->Byte0 = s->Byte7;                            \
}
#endif          /* if _WIN32_WINNT <= 0x0600 */

/** Function declarations */

__drv_dispatchType(IRP_MJ_SCSI) DRIVER_DISPATCH WvFilediskDispatchScsiIrp;
static __drv_dispatchType(IRP_MJ_SCSI) DRIVER_DISPATCH WvFilediskScsiExecute;
static __drv_dispatchType(IRP_MJ_SCSI) DRIVER_DISPATCH WvFilediskScsiReadWrite;

/** Function definitions */

/** PnP IRP dispatcher */
NTSTATUS STDCALL WvFilediskDispatchScsiIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    NTSTATUS status;
    IO_STACK_LOCATION * io_stack_loc;
    SCSI_REQUEST_BLOCK * srb;
    UCHAR code;
    
    ASSERT(dev_obj);
    ASSERT(irp);
    
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);
    
    srb = io_stack_loc->Parameters.Scsi.Srb;
    ASSERT(srb);
    
    code = srb->Function;
    switch (code) {
        case SRB_FUNCTION_EXECUTE_SCSI:
        return WvFilediskScsiExecute(dev_obj, irp);
        
        case SRB_FUNCTION_CLAIM_DEVICE:
        srb->DataBuffer = dev_obj;
        srb->SrbStatus = SRB_STATUS_SUCCESS;
        status = STATUS_SUCCESS;
        break;

        case SRB_FUNCTION_IO_CONTROL:
        srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        status = STATUS_SUCCESS;
        break;

        case SRB_FUNCTION_RELEASE_DEVICE:
        ObDereferenceObject(dev_obj);
        /* Fall through */
        
        case SRB_FUNCTION_SHUTDOWN:
        case SRB_FUNCTION_FLUSH:
        srb->SrbStatus = SRB_STATUS_SUCCESS;
        status = STATUS_SUCCESS;
        break;

        case SRB_FUNCTION_RECEIVE_EVENT:
        case SRB_FUNCTION_RELEASE_QUEUE:
        case SRB_FUNCTION_ATTACH_DEVICE:
        case SRB_FUNCTION_ABORT_COMMAND:
        case SRB_FUNCTION_RELEASE_RECOVERY:
        case SRB_FUNCTION_RESET_BUS:
        case SRB_FUNCTION_RESET_DEVICE:
        case SRB_FUNCTION_TERMINATE_IO:
        case SRB_FUNCTION_FLUSH_QUEUE:
        case SRB_FUNCTION_REMOVE_DEVICE:
        case SRB_FUNCTION_WMI:
        case SRB_FUNCTION_LOCK_QUEUE:
        case SRB_FUNCTION_UNLOCK_QUEUE:
        default:
        DBG("SRB function 0x%02X not yet implemented\n", code);
        srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        /* TODO: Use SCSISTAT_COMMAND_TERMINATED? */
        srb->ScsiStatus = SCSISTAT_GOOD;
        status = STATUS_NOT_IMPLEMENTED;
      }
    irp->IoStatus.Information = 0;
    irp->IoStatus.Status = status;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

NTSTATUS STDCALL WvFilediskScsiExecute(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    NTSTATUS status;
    IO_STACK_LOCATION * io_stack_loc;
    SCSI_REQUEST_BLOCK * srb;
    CDB * cdb;
    UCHAR code;

    ASSERT(dev_obj);
    ASSERT(irp);

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    srb = io_stack_loc->Parameters.Scsi.Srb;
    ASSERT(srb);

    cdb = (CDB *) srb->Cdb;
    code = cdb->AsByte[0];
    switch (code) {
        case SCSIOP_TEST_UNIT_READY:
        case SCSIOP_INQUIRY:
        case SCSIOP_MEDIUM_REMOVAL:
        srb->SrbStatus = SRB_STATUS_SUCCESS;
        status = STATUS_SUCCESS;
        break;
        
        case SCSIOP_READ:
        case SCSIOP_READ16:
        case SCSIOP_WRITE:
        case SCSIOP_WRITE16:
        return WvFilediskScsiReadWrite(dev_obj, irp);
        
        case SCSIOP_VERIFY:
        case SCSIOP_VERIFY16:
        case SCSIOP_READ_CAPACITY:
        case SCSIOP_READ_CAPACITY16:
        case SCSIOP_MODE_SENSE:
        case SCSIOP_READ_TOC:
        default:
        DBG("SCSI operation 0x%02X not yet implemented\n", code);
        srb->SrbStatus = SRB_STATUS_ERROR;
        /* TODO: Use SCSISTAT_COMMAND_TERMINATED? */
        srb->ScsiStatus = SCSISTAT_GOOD;
        status = STATUS_NOT_IMPLEMENTED;
      }
    irp->IoStatus.Information = 0;
    irp->IoStatus.Status = status;
    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
    return status;
  }

NTSTATUS STDCALL WvFilediskScsiReadWrite(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    NTSTATUS status;
    IO_STACK_LOCATION * io_stack_loc;
    SCSI_REQUEST_BLOCK * srb;
    CDB * cdb;
    UCHAR code;
    DISK_CDB16 * disk_cdb16;
    ULONGLONG start_sector;
    UINT32 sector_count;
    S_WVL_FILEDISK * disk;
    UCHAR * p;
    UCHAR * q;
    UCHAR * r;
    UCHAR * s;
    WVL_E_DISK_IO_MODE mode;
    LARGE_INTEGER offset;
    IO_STATUS_BLOCK io_status;

    ASSERT(dev_obj);
    ASSERT(irp);

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    srb = io_stack_loc->Parameters.Scsi.Srb;
    ASSERT(srb);

    cdb = (CDB *) srb->Cdb;
    code = cdb->AsByte[0];

    if (code == SCSIOP_READ16 || code == SCSIOP_WRITE16) {
        disk_cdb16 = (void *) cdb;
        REVERSE_BYTES_QUAD(&start_sector, disk_cdb16->LogicalBlock);
        REVERSE_BYTES(&sector_count, disk_cdb16->TransferLength);
      } else {
        start_sector = (
            (cdb->CDB10.LogicalBlockByte0 << 24) +
            (cdb->CDB10.LogicalBlockByte1 << 16) +
            (cdb->CDB10.LogicalBlockByte2 << 8) +
            (cdb->CDB10.LogicalBlockByte3 << 0)
          );
        sector_count = (
            (cdb->CDB10.TransferBlocksMsb << 8) +
            (cdb->CDB10.TransferBlocksLsb << 0)
          );
      }

    disk = dev_obj->DeviceExtension;
    ASSERT(disk);
    
    if (start_sector >= disk->LbaDiskSize) {
        DBG("start_sector too large\n");
        sector_count = 0;
      }
    if (start_sector + sector_count >= disk->LbaDiskSize) {
        DBG("start_sector + sector_count too large\n");
        sector_count = (UINT32) (disk->LbaDiskSize - start_sector);
      }
    if (sector_count * disk->SectorSize > srb->DataTransferLength) {
        DBG("srb->DataTransferLength too small\n");
        sector_count = srb->DataTransferLength / disk->SectorSize;
      }
    if (srb->DataTransferLength % disk->SectorSize != 0) {
        DBG("srb->DataTransferLength not a multiple of disk->SectorSize\n");
      }
    if (srb->DataTransferLength > sector_count * disk->SectorSize) {
        DBG("srb->DataTransferLength too big\n");
      }
    srb->DataTransferLength = sector_count * disk->SectorSize;

    /* Assume success */
    srb->SrbStatus = SRB_STATUS_SUCCESS;
    
    if (sector_count == 0) {
        status = STATUS_SUCCESS;
        irp->IoStatus.Information = 0;
        irp->IoStatus.Status = status;
        WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
        return status;
      }

    /* TODO: Review this: How can 's' ever be null? */
    p = (UCHAR *) srb->DataBuffer;
    q = (UCHAR *) MmGetMdlVirtualAddress(irp->MdlAddress);
    r = (UCHAR *) MmGetSystemAddressForMdlSafe(
        irp->MdlAddress,
        HighPagePriority
      );
    s = (p - q) + r;
    if (!s) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        irp->IoStatus.Information = 0;
        irp->IoStatus.Status = status;
        WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
        return status;
      }

    if (cdb->AsByte[0] == SCSIOP_READ || cdb->AsByte[0] == SCSIOP_READ16)
      mode = WvlDiskIoModeRead;
      else
      mode = WvlDiskIoModeWrite;
      
    /* Impersonate the user who opened the file */
    if (disk->Impersonate) {
        status = SeImpersonateClientEx(&disk->SecurityContext, NULL);
        if (!NT_SUCCESS(status)) {
            DBG("Couldn't impersonate!\n");
            irp->IoStatus.Information = 0;
            irp->IoStatus.Status = status;
            goto err_impersonate;
          }
      }

    /* Calculate the offset */
    offset.QuadPart = start_sector * disk->SectorSize;
    offset.QuadPart += disk->FileOffset.QuadPart;

    status = (mode == WvlDiskIoModeRead ? ZwReadFile : ZwWriteFile)(
        disk->FileHandle,
        NULL,
        NULL,
        NULL,
        &io_status,
        s,
        sector_count * disk->SectorSize,
        &offset,
        NULL
      );
    irp->IoStatus.Information = sector_count * disk->SectorSize;
    irp->IoStatus.Status = status;
    
    /* TODO: (Re-)Read geometry if MBR is read: pre-mini-driver logic */
    
    if (disk->Impersonate)
      PsRevertToSelf();
    err_impersonate:

    WvlPassIrpUp(dev_obj, irp, IO_NO_INCREMENT);
  }
