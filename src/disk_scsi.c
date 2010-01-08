/**
 * Copyright (C) 2009, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
 * Disk SCSI IRP handling
 *
 */

#include <ntddk.h>
#include <scsi.h>
#include <ntdddisk.h>
#include <ntddcdrm.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "disk.h"
#include "debug.h"
#include "mount.h"
#include "aoe.h"

#if _WIN32_WINNT <= 0x0600
#  if 0				/* FIXME: To build with WINDDK 6001.18001 */
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
#  endif			/* To build with WINDDK 6001.18001 */

#  if _WIN32_WINNT < 0x0500
#    if 0			/* FIXME: To build with WINDDK 6001.18001 */
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
#    endif			/* To build with WINDDK 6001.18001 */
#  endif			/* _WIN32_WINNT < 0x0500 */

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
#endif				/* if _WIN32_WINNT <= 0x0600 */

#define scsi_op( x ) \
\
NTSTATUS STDCALL \
x ( \
  IN struct _driver__dev_ext *DeviceExtension, \
  IN PIRP Irp, \
  IN disk__type_ptr disk_ptr, \
  IN PSCSI_REQUEST_BLOCK Srb, \
  IN PCDB Cdb, \
  OUT winvblock__bool_ptr completion_ptr \
 )

static
scsi_op (
  read_write
 )
{
  LONGLONG start_sector;
  winvblock__uint32 sector_count;
  NTSTATUS status = STATUS_SUCCESS;

  if ( Cdb->AsByte[0] == SCSIOP_READ16 || Cdb->AsByte[0] == SCSIOP_WRITE16 )
    {
      REVERSE_BYTES_QUAD ( &start_sector,
			   &( ( ( PDISK_CDB16 ) Cdb )->LogicalBlock[0] ) );
      REVERSE_BYTES ( &sector_count,
		      &( ( ( PDISK_CDB16 ) Cdb )->TransferLength[0] ) );
    }
  else
    {
      start_sector =
	( Cdb->CDB10.LogicalBlockByte0 << 24 ) +
	( Cdb->CDB10.LogicalBlockByte1 << 16 ) +
	( Cdb->CDB10.LogicalBlockByte2 << 8 ) + Cdb->CDB10.LogicalBlockByte3;
      sector_count =
	( Cdb->CDB10.TransferBlocksMsb << 8 ) + Cdb->CDB10.TransferBlocksLsb;
    }
  if ( start_sector >= disk_ptr->LBADiskSize )
    {
      DBG ( "Fixed sector_count (start_sector off disk)!!\n" );
      sector_count = 0;
    }
  if ( ( start_sector + sector_count > disk_ptr->LBADiskSize )
       && sector_count != 0 )
    {
      DBG ( "Fixed sector_count (start_sector + "
	    "sector_count off disk)!!\n" );
      sector_count =
	( winvblock__uint32 ) ( disk_ptr->LBADiskSize - start_sector );
    }
  if ( sector_count * disk_ptr->SectorSize > Srb->DataTransferLength )
    {
      DBG ( "Fixed sector_count (DataTransferLength " "too small)!!\n" );
      sector_count = Srb->DataTransferLength / disk_ptr->SectorSize;
    }
  if ( Srb->DataTransferLength % disk_ptr->SectorSize != 0 )
    DBG ( "DataTransferLength not aligned!!\n" );
  if ( Srb->DataTransferLength > sector_count * disk_ptr->SectorSize )
    DBG ( "DataTransferLength too big!!\n" );

  Srb->DataTransferLength = sector_count * disk_ptr->SectorSize;
  Srb->SrbStatus = SRB_STATUS_SUCCESS;
  if ( sector_count == 0 )
    {
      Irp->IoStatus.Information = 0;
      return status;
    }

  if ( ( ( ( winvblock__uint8_ptr ) Srb->DataBuffer -
	   ( winvblock__uint8_ptr )
	   MmGetMdlVirtualAddress ( Irp->MdlAddress ) ) +
	 ( winvblock__uint8_ptr )
	 MmGetSystemAddressForMdlSafe ( Irp->MdlAddress,
					HighPagePriority ) ) == NULL )
    {
      status = STATUS_INSUFFICIENT_RESOURCES;
      Irp->IoStatus.Information = 0;
      return status;
    }

  if ( Cdb->AsByte[0] == SCSIOP_READ || Cdb->AsByte[0] == SCSIOP_READ16 )
    {
      *completion_ptr = TRUE;
      return disk__io ( DeviceExtension, disk__io_mode_read, start_sector,
			sector_count,
			( ( winvblock__uint8_ptr ) Srb->DataBuffer -
			  ( winvblock__uint8_ptr )
			  MmGetMdlVirtualAddress ( Irp->MdlAddress ) ) +
			( winvblock__uint8_ptr )
			MmGetSystemAddressForMdlSafe ( Irp->MdlAddress,
						       HighPagePriority ),
			Irp );
    }
  else
    {
      *completion_ptr = TRUE;
      return disk__io ( DeviceExtension, disk__io_mode_write, start_sector,
			sector_count,
			( ( winvblock__uint8_ptr ) Srb->DataBuffer -
			  ( winvblock__uint8_ptr )
			  MmGetMdlVirtualAddress ( Irp->MdlAddress ) ) +
			( winvblock__uint8_ptr )
			MmGetSystemAddressForMdlSafe ( Irp->MdlAddress,
						       HighPagePriority ),
			Irp );
    }
  return status;
}

static
scsi_op (
  verify
 )
{
  LONGLONG start_sector;
  winvblock__uint32 sector_count;

  if ( Cdb->AsByte[0] == SCSIOP_VERIFY16 )
    {
      REVERSE_BYTES_QUAD ( &start_sector,
			   &( ( ( PDISK_CDB16 ) Cdb )->LogicalBlock[0] ) );
      REVERSE_BYTES ( &sector_count,
		      &( ( ( PDISK_CDB16 ) Cdb )->TransferLength[0] ) );
    }
  else
    {
      start_sector =
	( Cdb->CDB10.LogicalBlockByte0 << 24 ) +
	( Cdb->CDB10.LogicalBlockByte1 << 16 ) +
	( Cdb->CDB10.LogicalBlockByte2 << 8 ) + Cdb->CDB10.LogicalBlockByte3;
      sector_count =
	( Cdb->CDB10.TransferBlocksMsb << 8 ) + Cdb->CDB10.TransferBlocksLsb;
    }
#if 0
  Srb->DataTransferLength = sector_count * SECTORSIZE;
#endif
  Srb->SrbStatus = SRB_STATUS_SUCCESS;
  return STATUS_SUCCESS;
}

static
scsi_op (
  read_capacity
 )
{
  winvblock__uint32 temp = disk_ptr->SectorSize;
  PREAD_CAPACITY_DATA data = ( PREAD_CAPACITY_DATA ) Srb->DataBuffer;

  REVERSE_BYTES ( &data->BytesPerBlock, &temp );
  if ( ( disk_ptr->LBADiskSize - 1 ) > 0xffffffff )
    {
      data->LogicalBlockAddress = -1;
    }
  else
    {
      temp = ( winvblock__uint32 ) ( disk_ptr->LBADiskSize - 1 );
      REVERSE_BYTES ( &data->LogicalBlockAddress, &temp );
    }
  Irp->IoStatus.Information = sizeof ( READ_CAPACITY_DATA );
  Srb->SrbStatus = SRB_STATUS_SUCCESS;
  return STATUS_SUCCESS;
}

static
scsi_op (
  read_capacity_16
 )
{
  winvblock__uint32 temp;
  LONGLONG big_temp;

  temp = disk_ptr->SectorSize;
  REVERSE_BYTES ( &
		  ( ( ( PREAD_CAPACITY_DATA_EX ) Srb->DataBuffer )->
		    BytesPerBlock ), &temp );
  big_temp = disk_ptr->LBADiskSize - 1;
  REVERSE_BYTES_QUAD ( &
		       ( ( ( PREAD_CAPACITY_DATA_EX ) Srb->DataBuffer )->
			 LogicalBlockAddress.QuadPart ), &big_temp );
  Irp->IoStatus.Information = sizeof ( READ_CAPACITY_DATA_EX );
  Srb->SrbStatus = SRB_STATUS_SUCCESS;
  return STATUS_SUCCESS;
}

static
scsi_op (
  mode_sense
 )
{
  PMODE_PARAMETER_HEADER mode_param_header;
  static MEDIA_TYPE media_types[disk__media_count] =
    { RemovableMedia, FixedMedia, RemovableMedia };

  if ( Srb->DataTransferLength < sizeof ( MODE_PARAMETER_HEADER ) )
    {
      Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
      return STATUS_SUCCESS;
    }
  mode_param_header = ( PMODE_PARAMETER_HEADER ) Srb->DataBuffer;
  RtlZeroMemory ( mode_param_header, Srb->DataTransferLength );
  mode_param_header->ModeDataLength = sizeof ( MODE_PARAMETER_HEADER );
  mode_param_header->MediumType = media_types[disk_ptr->media];
  mode_param_header->BlockDescriptorLength = 0;
  Srb->DataTransferLength = sizeof ( MODE_PARAMETER_HEADER );
  Irp->IoStatus.Information = sizeof ( MODE_PARAMETER_HEADER );
  Srb->SrbStatus = SRB_STATUS_SUCCESS;
  return STATUS_SUCCESS;
}

static
scsi_op (
  read_toc
 )
{
  /*
   * With thanks to Olof's ImDisk source
   */
  PCDROM_TOC table_of_contents = ( PCDROM_TOC ) Srb->DataBuffer;

  if ( Srb->DataTransferLength < sizeof ( CDROM_TOC ) )
    {
      Irp->IoStatus.Information = 0;
      Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
      return STATUS_BUFFER_TOO_SMALL;
    }
  RtlZeroMemory ( table_of_contents, sizeof ( CDROM_TOC ) );

  table_of_contents->FirstTrack = 1;
  table_of_contents->LastTrack = 1;
  table_of_contents->TrackData[0].Control = 4;
  Irp->IoStatus.Information = sizeof ( CDROM_TOC );
  Srb->SrbStatus = SRB_STATUS_SUCCESS;
  return STATUS_SUCCESS;
}

irp__handler_decl ( disk_scsi__dispatch )
{
  NTSTATUS status = STATUS_SUCCESS;
  disk__type_ptr disk_ptr;
  PSCSI_REQUEST_BLOCK Srb;
  PCDB Cdb;
  winvblock__bool completion = FALSE;

  /*
   * Establish a pointer into the disk device's extension space
   */
  disk_ptr = get_disk_ptr ( DeviceExtension );

  Srb = Stack->Parameters.Scsi.Srb;
  Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
  Srb->ScsiStatus = SCSISTAT_GOOD;

  Cdb = ( PCDB ) Srb->Cdb;

  Irp->IoStatus.Information = 0;
  if ( Srb->Lun != 0 )
    {
      DBG ( "Invalid Lun!!\n" );
      goto ret_path;
    }
  switch ( Srb->Function )
    {
      case SRB_FUNCTION_EXECUTE_SCSI:
	switch ( Cdb->AsByte[0] )
	  {
	    case SCSIOP_TEST_UNIT_READY:
	      Srb->SrbStatus = SRB_STATUS_SUCCESS;
	      break;
	    case SCSIOP_READ:
	    case SCSIOP_READ16:
	    case SCSIOP_WRITE:
	    case SCSIOP_WRITE16:
	      status =
		read_write ( DeviceExtension, Irp, disk_ptr, Srb, Cdb,
			     &completion );
	      break;
	    case SCSIOP_VERIFY:
	    case SCSIOP_VERIFY16:
	      status =
		verify ( DeviceExtension, Irp, disk_ptr, Srb, Cdb,
			 &completion );
	      break;
	    case SCSIOP_READ_CAPACITY:
	      status =
		read_capacity ( DeviceExtension, Irp, disk_ptr, Srb, Cdb,
				&completion );
	      break;
	    case SCSIOP_READ_CAPACITY16:
	      status =
		read_capacity_16 ( DeviceExtension, Irp, disk_ptr, Srb, Cdb,
				   &completion );
	      break;
	    case SCSIOP_MODE_SENSE:
	      status =
		mode_sense ( DeviceExtension, Irp, disk_ptr, Srb, Cdb,
			     &completion );
	      break;
	    case SCSIOP_MEDIUM_REMOVAL:
	      Irp->IoStatus.Information = 0;
	      Srb->SrbStatus = SRB_STATUS_SUCCESS;
	      status = STATUS_SUCCESS;
	      break;
	    case SCSIOP_READ_TOC:
	      status =
		read_toc ( DeviceExtension, Irp, disk_ptr, Srb, Cdb,
			   &completion );
	      break;
	    default:
	      DBG ( "Invalid SCSIOP (%02x)!!\n", Cdb->AsByte[0] );
	      Srb->SrbStatus = SRB_STATUS_ERROR;
	      status = STATUS_NOT_IMPLEMENTED;
	  }
	break;
      case SRB_FUNCTION_IO_CONTROL:
	Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
	break;
      case SRB_FUNCTION_CLAIM_DEVICE:
	Srb->DataBuffer = DeviceObject;
	break;
      case SRB_FUNCTION_RELEASE_DEVICE:
	ObDereferenceObject ( DeviceObject );
	break;
      case SRB_FUNCTION_SHUTDOWN:
      case SRB_FUNCTION_FLUSH:
	Srb->SrbStatus = SRB_STATUS_SUCCESS;
	break;
      default:
	DBG ( "Invalid SRB FUNCTION (%08x)!!\n", Srb->Function );
	status = STATUS_NOT_IMPLEMENTED;
    }

ret_path:
  if ( !completion )
    {
      Irp->IoStatus.Status = status;
      IoCompleteRequest ( Irp, IO_NO_INCREMENT );
    }
  *completion_ptr = TRUE;
  return status;
}
