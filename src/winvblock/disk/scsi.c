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
 * Disk SCSI IRP handling.
 */

#include <ntddk.h>
#include <scsi.h>
#include <ntdddisk.h>
#include <ntddcdrm.h>

#include "winvblock.h"
#include "portable.h"
#include "driver.h"
#include "device.h"
#include "disk.h"
#include "debug.h"

/**
 * The prototype for a disk SCSI function.
 *
 * @v dev               Points to the disk's device for the SCSI request.
 * @v irp               Points to the IRP.
 * @v disk              Points to the disk for the SCSI request.
 * @v srb               Points to the SCSI request block.
 * @v cdb               Points to the command descriptor block.
 * @v completion        Points to a boolean for whether the IRP has
 *                      been completed or not.
 * @ret NTSTATUS        The status of the SCSI operation.
 */
typedef NTSTATUS STDCALL disk_scsi__func(
    IN WV_SP_DEV_T,
    IN PIRP,
    IN disk__type_ptr,
    IN PSCSI_REQUEST_BLOCK,
    IN PCDB,
    OUT winvblock__bool_ptr
  );

/* Forward declarations. */
disk_scsi__func disk_scsi__read_write_;
disk_scsi__func disk_scsi__verify_;
disk_scsi__func disk_scsi__read_capacity_;
disk_scsi__func disk_scsi__read_capacity_16_;
disk_scsi__func disk_scsi__mode_sense_;
disk_scsi__func disk_scsi__read_toc_;
device__scsi_func disk_scsi__dispatch;

#if _WIN32_WINNT <= 0x0600
#  if 0        /* FIXME: To build with WINDDK 6001.18001 */
#    ifdef _MSC_VER
#      pragma pack(1)
#    endif
typedef union _EIGHT_BYTE
{
  struct
  {
    winvblock__uint8 Byte0;
    winvblock__uint8 Byte1;
    winvblock__uint8 Byte2;
    winvblock__uint8 Byte3;
    winvblock__uint8 Byte4;
    winvblock__uint8 Byte5;
    winvblock__uint8 Byte6;
    winvblock__uint8 Byte7;
  };
  ULONGLONG AsULongLong;
} __attribute__ ( ( __packed__ ) ) EIGHT_BYTE, *PEIGHT_BYTE;
#    ifdef _MSC_VER
#      pragma pack()
#    endif
#  endif      /* To build with WINDDK 6001.18001 */

#  if _WIN32_WINNT < 0x0500
#    if 0      /* FIXME: To build with WINDDK 6001.18001 */
#      ifdef _MSC_VER
#        pragma pack(1)
#      endif
typedef struct _READ_CAPACITY_DATA_EX
{
  LARGE_INTEGER LogicalBlockAddress;
  ULONG BytesPerBlock;
} __attribute__ ( ( __packed__ ) ) READ_CAPACITY_DATA_EX,
  *PREAD_CAPACITY_DATA_EX;
#      ifdef _MSC_VER
#        pragma pack()
#      endif
#    endif      /* To build with WINDDK 6001.18001 */
#  endif      /* _WIN32_WINNT < 0x0500 */

#  ifdef _MSC_VER
#    pragma pack(1)
#  endif
typedef struct _DISK_CDB16
{
  winvblock__uint8 OperationCode;
  winvblock__uint8 Reserved1:3;
  winvblock__uint8 ForceUnitAccess:1;
  winvblock__uint8 DisablePageOut:1;
  winvblock__uint8 Protection:3;
  winvblock__uint8 LogicalBlock[8];
  winvblock__uint8 TransferLength[4];
  winvblock__uint8 Reserved2;
  winvblock__uint8 Control;
} __attribute__ ( ( __packed__ ) ) DISK_CDB16, *PDISK_CDB16;
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
#endif        /* if _WIN32_WINNT <= 0x0600 */

static NTSTATUS STDCALL disk_scsi__read_write_(
    IN WV_SP_DEV_T dev,
    IN PIRP irp,
    IN disk__type_ptr disk,
    IN PSCSI_REQUEST_BLOCK srb,
    IN PCDB cdb,
    OUT winvblock__bool_ptr completion
  ) {
    ULONGLONG start_sector;
    winvblock__uint32 sector_count;
    NTSTATUS status = STATUS_SUCCESS;

    if (cdb->AsByte[0] == SCSIOP_READ16 || cdb->AsByte[0] == SCSIOP_WRITE16) {
        REVERSE_BYTES_QUAD(
            &start_sector,
            &(((PDISK_CDB16) cdb)->LogicalBlock[0])
          );
        REVERSE_BYTES(
            &sector_count,
            &(((PDISK_CDB16) cdb )->TransferLength[0])
          );
      } else {
        start_sector = (cdb->CDB10.LogicalBlockByte0 << 24) +
          (cdb->CDB10.LogicalBlockByte1 << 16) +
          (cdb->CDB10.LogicalBlockByte2 << 8 ) +
          cdb->CDB10.LogicalBlockByte3;
        sector_count = (cdb->CDB10.TransferBlocksMsb << 8) +
          cdb->CDB10.TransferBlocksLsb;
      }
    if (start_sector >= disk->LBADiskSize) {
        DBG("Fixed sector_count (start_sector off disk)!!\n");
        sector_count = 0;
      }
    if (
        (start_sector + sector_count > disk->LBADiskSize) &&
        sector_count != 0
      ) {
        DBG("Fixed sector_count (start_sector + sector_count off disk)!!\n");
        sector_count = (winvblock__uint32) (disk->LBADiskSize - start_sector);
      }
    if (sector_count * disk->SectorSize > srb->DataTransferLength) {
        DBG("Fixed sector_count (DataTransferLength " "too small)!!\n");
        sector_count = srb->DataTransferLength / disk->SectorSize;
      }
    if (srb->DataTransferLength % disk->SectorSize != 0)
      DBG("DataTransferLength not aligned!!\n");
    if (srb->DataTransferLength > sector_count * disk->SectorSize)
      DBG("DataTransferLength too big!!\n");

    srb->DataTransferLength = sector_count * disk->SectorSize;
    srb->SrbStatus = SRB_STATUS_SUCCESS;
    if (sector_count == 0) {
        irp->IoStatus.Information = 0;
        return status;
      }

    if ((
        (
            (winvblock__uint8_ptr) srb->DataBuffer -
            (winvblock__uint8_ptr) MmGetMdlVirtualAddress(irp->MdlAddress)
          ) + (winvblock__uint8_ptr) MmGetSystemAddressForMdlSafe(
              irp->MdlAddress,
              HighPagePriority
      )) == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        irp->IoStatus.Information = 0;
        return status;
      }

    if (cdb->AsByte[0] == SCSIOP_READ || cdb->AsByte[0] == SCSIOP_READ16) {
        status = disk__io(
            dev,
            disk__io_mode_read,
            start_sector,
            sector_count,
            ((winvblock__uint8_ptr) srb->DataBuffer -
              (winvblock__uint8_ptr) MmGetMdlVirtualAddress(irp->MdlAddress)) +
              (winvblock__uint8_ptr) MmGetSystemAddressForMdlSafe(
                  irp->MdlAddress,
                  HighPagePriority
              ),
            irp
          );
      } else {
        status = disk__io(
            dev,
            disk__io_mode_write,
            start_sector,
            sector_count,
            ((winvblock__uint8_ptr) srb->DataBuffer -
              (winvblock__uint8_ptr) MmGetMdlVirtualAddress(irp->MdlAddress)) +
              (winvblock__uint8_ptr) MmGetSystemAddressForMdlSafe(
                  irp->MdlAddress,
                  HighPagePriority
              ),
            irp
          );
      }
    if (status != STATUS_PENDING)
      *completion = TRUE;
    return status;
  }

static NTSTATUS STDCALL disk_scsi__verify_(
    IN WV_SP_DEV_T dev,
    IN PIRP irp,
    IN disk__type_ptr disk,
    IN PSCSI_REQUEST_BLOCK srb,
    IN PCDB cdb,
    OUT winvblock__bool_ptr completion
  ) {
    LONGLONG start_sector;
    winvblock__uint32 sector_count;

    if (cdb->AsByte[0] == SCSIOP_VERIFY16) {
        REVERSE_BYTES_QUAD(
            &start_sector,
            &(((PDISK_CDB16) cdb)->LogicalBlock[0])
          );
        REVERSE_BYTES(
            &sector_count,
            &(((PDISK_CDB16) cdb)->TransferLength[0])
          );
      } else {
        start_sector = (cdb->CDB10.LogicalBlockByte0 << 24) +
          (cdb->CDB10.LogicalBlockByte1 << 16) +
          (cdb->CDB10.LogicalBlockByte2 << 8) +
          cdb->CDB10.LogicalBlockByte3;
        sector_count = (cdb->CDB10.TransferBlocksMsb << 8) +
          cdb->CDB10.TransferBlocksLsb;
      }
    #if 0
    srb->DataTransferLength = sector_count * SECTORSIZE;
    #endif
    srb->SrbStatus = SRB_STATUS_SUCCESS;
    return STATUS_SUCCESS;
  }

static NTSTATUS STDCALL disk_scsi__read_capacity_(
    IN WV_SP_DEV_T dev,
    IN PIRP irp,
    IN disk__type_ptr disk,
    IN PSCSI_REQUEST_BLOCK srb,
    IN PCDB cdb,
    OUT winvblock__bool_ptr completion
  ) {
    winvblock__uint32 temp = disk->SectorSize;
    PREAD_CAPACITY_DATA data = (PREAD_CAPACITY_DATA) srb->DataBuffer;

    REVERSE_BYTES(&data->BytesPerBlock, &temp);
    if ((disk->LBADiskSize - 1) > 0xffffffff) {
        data->LogicalBlockAddress = -1;
      } else {
        temp = (winvblock__uint32) (disk->LBADiskSize - 1);
        REVERSE_BYTES(&data->LogicalBlockAddress, &temp);
      }
    irp->IoStatus.Information = sizeof (READ_CAPACITY_DATA);
    srb->SrbStatus = SRB_STATUS_SUCCESS;
    return STATUS_SUCCESS;
  }

static NTSTATUS STDCALL disk_scsi__read_capacity_16_(
    IN WV_SP_DEV_T dev,
    IN PIRP irp,
    IN disk__type_ptr disk,
    IN PSCSI_REQUEST_BLOCK srb,
    IN PCDB cdb,
    OUT winvblock__bool_ptr completion
  ) {
    winvblock__uint32 temp;
    LONGLONG big_temp;

    temp = disk->SectorSize;
    REVERSE_BYTES(
        &(((PREAD_CAPACITY_DATA_EX) srb->DataBuffer)->BytesPerBlock),
        &temp
      );
    big_temp = disk->LBADiskSize - 1;
    REVERSE_BYTES_QUAD(
        &(((PREAD_CAPACITY_DATA_EX) srb->DataBuffer)->
          LogicalBlockAddress.QuadPart),
        &big_temp
      );
    irp->IoStatus.Information = sizeof (READ_CAPACITY_DATA_EX);
    srb->SrbStatus = SRB_STATUS_SUCCESS;
    return STATUS_SUCCESS;
  }

static NTSTATUS STDCALL disk_scsi__mode_sense_(
    IN WV_SP_DEV_T dev,
    IN PIRP irp,
    IN disk__type_ptr disk,
    IN PSCSI_REQUEST_BLOCK srb,
    IN PCDB cdb,
    OUT winvblock__bool_ptr completion
  ) {
    PMODE_PARAMETER_HEADER mode_param_header;
    static MEDIA_TYPE media_types[disk__media_count] =
      { RemovableMedia, FixedMedia, RemovableMedia };

    if (srb->DataTransferLength < sizeof (MODE_PARAMETER_HEADER)) {
        srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
        return STATUS_SUCCESS;
      }
    mode_param_header = (PMODE_PARAMETER_HEADER) srb->DataBuffer;
    RtlZeroMemory(mode_param_header, srb->DataTransferLength);
    mode_param_header->ModeDataLength = sizeof (MODE_PARAMETER_HEADER);
    mode_param_header->MediumType = media_types[disk->media];
    mode_param_header->BlockDescriptorLength = 0;
    srb->DataTransferLength = sizeof (MODE_PARAMETER_HEADER);
    irp->IoStatus.Information = sizeof (MODE_PARAMETER_HEADER);
    srb->SrbStatus = SRB_STATUS_SUCCESS;
    return STATUS_SUCCESS;
  }

static NTSTATUS STDCALL disk_scsi__read_toc_(
    IN WV_SP_DEV_T dev,
    IN PIRP irp,
    IN disk__type_ptr disk,
    IN PSCSI_REQUEST_BLOCK srb,
    IN PCDB cdb,
    OUT winvblock__bool_ptr completion
  ) {
    /* With thanks to Olof Lagerkvist's ImDisk source. */
    PCDROM_TOC table_of_contents = (PCDROM_TOC) srb->DataBuffer;

    if (srb->DataTransferLength < sizeof (CDROM_TOC)) {
        irp->IoStatus.Information = 0;
        srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
        return STATUS_BUFFER_TOO_SMALL;
      }
    RtlZeroMemory(table_of_contents, sizeof (CDROM_TOC));

    table_of_contents->FirstTrack = 1;
    table_of_contents->LastTrack = 1;
    table_of_contents->TrackData[0].Control = 4;
    irp->IoStatus.Information = sizeof (CDROM_TOC);
    srb->SrbStatus = SRB_STATUS_SUCCESS;
    return STATUS_SUCCESS;
  }

NTSTATUS STDCALL disk_scsi__dispatch(
    IN WV_SP_DEV_T dev,
    IN PIRP irp,
    IN UCHAR code
  ) {
    disk__type_ptr disk = disk__get_ptr(dev);
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    PSCSI_REQUEST_BLOCK srb = io_stack_loc->Parameters.Scsi.Srb;
    NTSTATUS status = STATUS_SUCCESS;
    PCDB cdb;
    winvblock__bool completion = FALSE;

    srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
    srb->ScsiStatus = SCSISTAT_GOOD;

    cdb = (PCDB) srb->Cdb;

    irp->IoStatus.Information = 0;
    if (srb->Lun != 0) {
        DBG("Invalid Lun!!\n");
        goto out;
      }
    switch (code) {
        case SRB_FUNCTION_EXECUTE_SCSI:
          switch (cdb->AsByte[0]) {
              case SCSIOP_TEST_UNIT_READY:
                srb->SrbStatus = SRB_STATUS_SUCCESS;
                break;

              case SCSIOP_READ:
              case SCSIOP_READ16:
              case SCSIOP_WRITE:
              case SCSIOP_WRITE16:
                status = disk_scsi__read_write_(
                    dev,
                    irp,
                    disk,
                    srb,
                    cdb,
                    &completion
                  );
                break;

              case SCSIOP_VERIFY:
              case SCSIOP_VERIFY16:
                status = disk_scsi__verify_(
                    dev,
                    irp,
                    disk,
                    srb,
                    cdb,
                    &completion
                  );
                break;

              case SCSIOP_READ_CAPACITY:
                status = disk_scsi__read_capacity_(
                    dev,
                    irp,
                    disk,
                    srb,
                    cdb,
                    &completion
                  );
                break;

              case SCSIOP_READ_CAPACITY16:
                status = disk_scsi__read_capacity_16_(
                    dev,
                    irp,
                    disk,
                    srb,
                    cdb,
                    &completion
                  );
                break;

              case SCSIOP_MODE_SENSE:
                status = disk_scsi__mode_sense_(
                    dev,
                    irp,
                    disk,
                    srb,
                    cdb,
                    &completion
                  );
                break;

              case SCSIOP_MEDIUM_REMOVAL:
                irp->IoStatus.Information = 0;
                srb->SrbStatus = SRB_STATUS_SUCCESS;
                status = STATUS_SUCCESS;
                break;

              case SCSIOP_READ_TOC:
                status = disk_scsi__read_toc_(
                    dev,
                    irp,
                    disk,
                    srb,
                    cdb,
                    &completion
                  );
                break;

              default:
                DBG("Invalid SCSIOP (%02x)!!\n", cdb->AsByte[0]);
                srb->SrbStatus = SRB_STATUS_ERROR;
                status = STATUS_NOT_IMPLEMENTED;
            }
          break; /* SRB_FUNCTION_EXECUTE_SCSI */

        case SRB_FUNCTION_IO_CONTROL:
          srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
          break;

        case SRB_FUNCTION_CLAIM_DEVICE:
          srb->DataBuffer = dev->Self;
          break;

        case SRB_FUNCTION_RELEASE_DEVICE:
          ObDereferenceObject(dev->Self);
          break;

        case SRB_FUNCTION_SHUTDOWN:
        case SRB_FUNCTION_FLUSH:
          srb->SrbStatus = SRB_STATUS_SUCCESS;
          break;

        default:
          DBG("Invalid SRB FUNCTION (%08x)!!\n", code);
          status = STATUS_NOT_IMPLEMENTED;
      }

    out:
    if (!completion) {
        irp->IoStatus.Status = status;
        if (status != STATUS_PENDING)
          IoCompleteRequest(irp, IO_NO_INCREMENT);
      }
    return status;
  }
