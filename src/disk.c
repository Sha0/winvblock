/**
 * Copyright 2006-2008, V.
 * Portions copyright (C) 2009 Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 * For contact information, see http://winaoe.org/
 *
 * This file is part of WinAoE.
 *
 * WinAoE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinAoE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WinAoE.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Disk specifics
 *
 */

#include "portable.h"
#include <stdio.h>
#include <ntddk.h>
#include <srb.h>
#include <scsi.h>
#include <ntddscsi.h>
#include <ntddstor.h>
#include <ntdddisk.h>
#include <initguid.h>
#include "driver.h"
#include "aoe.h"
#include "debug.h"

#ifndef _MSC_VER
static long long
__divdi3 (
	long long u,
	long long v
 )
{
	return u / v;
}
#endif

#if _WIN32_WINNT <= 0x0500
#  if 0													/* FIXME: To build with WINDDK 6001.18001 */
#    ifdef _MSC_VER
#      pragma pack(1)
#    endif
typedef union _EIGHT_BYTE
{
	struct
	{
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
} __attribute__ ( ( __packed__ ) ) EIGHT_BYTE, *PEIGHT_BYTE;
#    ifdef _MSC_VER
#      pragma pack()
#    endif
#  endif												/* To build with WINDDK 6001.18001 */

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
#endif													/* if _WIN32_WINNT <= 0x0500 */

#if _WIN32_WINNT < 0x0502
#  if 0													/* FIXME: To build with WINDDK 6001.18001 */
#    ifdef _MSC_VER
#      pragma pack(1)
#    endif
typedef struct _READ_CAPACITY_DATA_EX
{
	LARGE_INTEGER LogicalBlockAddress;
	ULONG BytesPerBlock;
} __attribute__ ( ( __packed__ ) ) READ_CAPACITY_DATA_EX,
	*PREAD_CAPACITY_DATA_EX;
#    ifdef _MSC_VER
#      pragma pack()
#    endif
#  endif												/* To build with WINDDK 6001.18001 */
#endif													/* _WIN32_WINNT < 0x0502 */

#ifdef _MSC_VER
#  pragma pack(1)
#endif
typedef struct _PORTABLE_CDB16
{
	UCHAR OperationCode;
	UCHAR Reserved1:3;
	UCHAR ForceUnitAccess:1;
	UCHAR DisablePageOut:1;
	UCHAR Protection:3;
	UCHAR LogicalBlock[8];
	UCHAR TransferLength[4];
	UCHAR Reserved2;
	UCHAR Control;
} __attribute__ ( ( __packed__ ) ) CDB16, *PCDB16;
#ifdef _MSC_VER
#  pragma pack()
#endif

DEFINE_GUID ( GUID_BUS_TYPE_INTERNAL, 0x2530ea73L, 0x086b, 0x11d1, 0xa0, 0x9f,
							0x00, 0xc0, 0x4f, 0xc3, 0x40, 0xb1 );

/* in this file */
static NTSTATUS STDCALL BusGetDeviceCapabilities (
	IN PDEVICE_OBJECT DeviceObject,
	IN PDEVICE_CAPABILITIES DeviceCapabilities
 );

NTSTATUS STDCALL
DiskDispatchPnP (
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp,
	IN PIO_STACK_LOCATION Stack,
	IN PDEVICEEXTENSION DeviceExtension
 )
{
	NTSTATUS Status;
	PDEVICE_RELATIONS DeviceRelations;
	PPNP_BUS_INFORMATION PnPBusInformation;
	PDEVICE_CAPABILITIES DeviceCapabilities;
	DEVICE_CAPABILITIES ParentDeviceCapabilities;
	PWCHAR String;
	ULONG StringLength;

	switch ( Stack->MinorFunction )
		{
			case IRP_MN_QUERY_ID:
				if ( ( String =
							 ( PWCHAR ) ExAllocatePool ( NonPagedPool,
																					 ( 512 * sizeof ( WCHAR ) ) ) ) ==
						 NULL )
					{
						DBG ( "ExAllocatePool IRP_MN_QUERY_ID\n" );
						Status = STATUS_INSUFFICIENT_RESOURCES;
						break;
					}
				RtlZeroMemory ( String, ( 512 * sizeof ( WCHAR ) ) );
				switch ( Stack->Parameters.QueryId.IdType )
					{
						case BusQueryDeviceID:
							StringLength =
								swprintf ( String, L"AoE\\e%d.%d",
													 DeviceExtension->Disk.AoE.Major,
													 DeviceExtension->Disk.AoE.Minor ) + 1;
							if ( ( Irp->IoStatus.Information =
										 ( ULONG_PTR ) ExAllocatePool ( PagedPool,
																										StringLength *
																										sizeof ( WCHAR ) ) ) == 0 )
								{
									DBG ( "ExAllocatePool BusQueryDeviceID\n" );
									Status = STATUS_INSUFFICIENT_RESOURCES;
									break;
								}
							RtlCopyMemory ( ( PWCHAR ) Irp->IoStatus.Information, String,
															StringLength * sizeof ( WCHAR ) );
							Status = STATUS_SUCCESS;
							break;
						case BusQueryInstanceID:
							StringLength =
								swprintf ( String, L"AOEDISK%d.%d",
													 DeviceExtension->Disk.AoE.Major,
													 DeviceExtension->Disk.AoE.Minor ) + 1;
							if ( ( Irp->IoStatus.Information =
										 ( ULONG_PTR ) ExAllocatePool ( PagedPool,
																										StringLength *
																										sizeof ( WCHAR ) ) ) == 0 )
								{
									DBG ( "ExAllocatePool BusQueryInstanceID\n" );
									Status = STATUS_INSUFFICIENT_RESOURCES;
									break;
								}
							RtlCopyMemory ( ( PWCHAR ) Irp->IoStatus.Information, String,
															StringLength * sizeof ( WCHAR ) );
							Status = STATUS_SUCCESS;
							break;
						case BusQueryHardwareIDs:
							StringLength =
								swprintf ( String, L"AoE\\e%d.%d",
													 DeviceExtension->Disk.AoE.Major,
													 DeviceExtension->Disk.AoE.Minor ) + 1;
							StringLength +=
								swprintf ( &String[StringLength], L"GenDisk" ) + 4;
							if ( ( Irp->IoStatus.Information =
										 ( ULONG_PTR ) ExAllocatePool ( PagedPool,
																										StringLength *
																										sizeof ( WCHAR ) ) ) == 0 )
								{
									DBG ( "ExAllocatePool BusQueryHardwareIDs\n" );
									Status = STATUS_INSUFFICIENT_RESOURCES;
									break;
								}
							RtlCopyMemory ( ( PWCHAR ) Irp->IoStatus.Information, String,
															StringLength * sizeof ( WCHAR ) );
							Status = STATUS_SUCCESS;
							break;
						case BusQueryCompatibleIDs:
							StringLength = swprintf ( String, L"GenDisk" ) + 4;
							if ( ( Irp->IoStatus.Information =
										 ( ULONG_PTR ) ExAllocatePool ( PagedPool,
																										StringLength *
																										sizeof ( WCHAR ) ) ) == 0 )
								{
									DBG ( "ExAllocatePool BusQueryCompatibleIDs\n" );
									Status = STATUS_INSUFFICIENT_RESOURCES;
									break;
								}
							RtlCopyMemory ( ( PWCHAR ) Irp->IoStatus.Information, String,
															StringLength * sizeof ( WCHAR ) );
							Status = STATUS_SUCCESS;
							break;
						default:
							Irp->IoStatus.Information = 0;
							Status = STATUS_NOT_SUPPORTED;
					}
				ExFreePool ( String );
				break;
			case IRP_MN_QUERY_DEVICE_TEXT:
				if ( ( String =
							 ( PWCHAR ) ExAllocatePool ( NonPagedPool,
																					 ( 512 * sizeof ( WCHAR ) ) ) ) ==
						 NULL )
					{
						DBG ( "ExAllocatePool IRP_MN_QUERY_DEVICE_TEXT\n" );
						Status = STATUS_INSUFFICIENT_RESOURCES;
						break;
					}
				RtlZeroMemory ( String, ( 512 * sizeof ( WCHAR ) ) );
				switch ( Stack->Parameters.QueryDeviceText.DeviceTextType )
					{
						case DeviceTextDescription:
							StringLength = swprintf ( String, L"AoE Disk" ) + 1;
							if ( ( Irp->IoStatus.Information =
										 ( ULONG_PTR ) ExAllocatePool ( PagedPool,
																										StringLength *
																										sizeof ( WCHAR ) ) ) == 0 )
								{
									DBG ( "ExAllocatePool DeviceTextDescription\n" );
									Status = STATUS_INSUFFICIENT_RESOURCES;
									break;
								}
							RtlCopyMemory ( ( PWCHAR ) Irp->IoStatus.Information, String,
															StringLength * sizeof ( WCHAR ) );
							Status = STATUS_SUCCESS;
							break;
						case DeviceTextLocationInformation:
							StringLength =
								swprintf ( String, L"AoE e%d.%d",
													 DeviceExtension->Disk.AoE.Major,
													 DeviceExtension->Disk.AoE.Minor ) + 1;
							if ( ( Irp->IoStatus.Information =
										 ( ULONG_PTR ) ExAllocatePool ( PagedPool,
																										StringLength *
																										sizeof ( WCHAR ) ) ) == 0 )
								{
									DBG ( "ExAllocatePool DeviceTextLocationInformation\n" );
									Status = STATUS_INSUFFICIENT_RESOURCES;
									break;
								}
							RtlCopyMemory ( ( PWCHAR ) Irp->IoStatus.Information, String,
															StringLength * sizeof ( WCHAR ) );
							Status = STATUS_SUCCESS;
							break;
						default:
							Irp->IoStatus.Information = 0;
							Status = STATUS_NOT_SUPPORTED;
					}
				ExFreePool ( String );
				break;
			case IRP_MN_QUERY_DEVICE_RELATIONS:
				if ( Stack->Parameters.QueryDeviceRelations.Type !=
						 TargetDeviceRelation )
					{
						Status = Irp->IoStatus.Status;
						break;
					}
				if ( ( DeviceRelations =
							 ( PDEVICE_RELATIONS ) ExAllocatePool ( PagedPool,
																											sizeof
																											( DEVICE_RELATIONS ) +
																											sizeof
																											( PDEVICE_OBJECT ) ) ) ==
						 NULL )
					{
						DBG ( "ExAllocatePool IRP_MN_QUERY_DEVICE_RELATIONS\n" );
						Status = STATUS_INSUFFICIENT_RESOURCES;
						break;
					}
				DeviceRelations->Objects[0] = DeviceExtension->Self;
				DeviceRelations->Count = 1;
				ObReferenceObject ( DeviceExtension->Self );
				Irp->IoStatus.Information = ( ULONG_PTR ) DeviceRelations;
				Status = STATUS_SUCCESS;
				break;
			case IRP_MN_QUERY_BUS_INFORMATION:
				if ( ( PnPBusInformation =
							 ( PPNP_BUS_INFORMATION ) ExAllocatePool ( PagedPool,
																												 sizeof
																												 ( PNP_BUS_INFORMATION ) ) )
						 == NULL )
					{
						DBG ( "ExAllocatePool IRP_MN_QUERY_BUS_INFORMATION\n" );
						Status = STATUS_INSUFFICIENT_RESOURCES;
						break;
					}
				PnPBusInformation->BusTypeGuid = GUID_BUS_TYPE_INTERNAL;
				PnPBusInformation->LegacyBusType = PNPBus;
				PnPBusInformation->BusNumber = 0;
				Irp->IoStatus.Information = ( ULONG_PTR ) PnPBusInformation;
				Status = STATUS_SUCCESS;
				break;
			case IRP_MN_QUERY_CAPABILITIES:
				DeviceCapabilities = Stack->Parameters.DeviceCapabilities.Capabilities;
				if ( DeviceCapabilities->Version != 1
						 || DeviceCapabilities->Size < sizeof ( DEVICE_CAPABILITIES ) )
					{
						Status = STATUS_UNSUCCESSFUL;
						break;
					}
				if ( !NT_SUCCESS
						 ( Status =
							 BusGetDeviceCapabilities ( ( ( PDEVICEEXTENSION )
																						DeviceExtension->Disk.
																						Parent->DeviceExtension )->
																					Bus.LowerDeviceObject,
																					&ParentDeviceCapabilities ) ) )
					break;
				RtlCopyMemory ( DeviceCapabilities->DeviceState,
												ParentDeviceCapabilities.DeviceState,
												( PowerSystemShutdown +
													1 ) * sizeof ( DEVICE_POWER_STATE ) );
				DeviceCapabilities->DeviceState[PowerSystemWorking] = PowerDeviceD0;
				if ( DeviceCapabilities->DeviceState[PowerSystemSleeping1] !=
						 PowerDeviceD0 )
					DeviceCapabilities->DeviceState[PowerSystemSleeping1] =
						PowerDeviceD1;
				if ( DeviceCapabilities->DeviceState[PowerSystemSleeping2] !=
						 PowerDeviceD0 )
					DeviceCapabilities->DeviceState[PowerSystemSleeping2] =
						PowerDeviceD3;
/*      if (DeviceCapabilities->DeviceState[PowerSystemSleeping3] !=
 *          PowerDeviceD0)
 *         DeviceCapabilities->DeviceState[PowerSystemSleeping3] =
 *             PowerDeviceD3;
 */
				DeviceCapabilities->DeviceWake = PowerDeviceD1;
				DeviceCapabilities->DeviceD1 = TRUE;
				DeviceCapabilities->DeviceD2 = FALSE;
				DeviceCapabilities->WakeFromD0 = FALSE;
				DeviceCapabilities->WakeFromD1 = FALSE;
				DeviceCapabilities->WakeFromD2 = FALSE;
				DeviceCapabilities->WakeFromD3 = FALSE;
				DeviceCapabilities->D1Latency = 0;
				DeviceCapabilities->D2Latency = 0;
				DeviceCapabilities->D3Latency = 0;
				DeviceCapabilities->EjectSupported = FALSE;
				DeviceCapabilities->HardwareDisabled = FALSE;
				DeviceCapabilities->Removable = FALSE;
				DeviceCapabilities->SurpriseRemovalOK = FALSE;
				DeviceCapabilities->UniqueID = FALSE;
				DeviceCapabilities->SilentInstall = FALSE;
/*      DeviceCapabilities->Address = DeviceObject->SerialNo;
 *      DeviceCapabilities->UINumber = DeviceObject->SerialNo;
 */
				Status = STATUS_SUCCESS;
				break;
			case IRP_MN_DEVICE_USAGE_NOTIFICATION:
				if ( Stack->Parameters.UsageNotification.InPath )
					{
						DeviceExtension->Disk.SpecialFileCount++;
					}
				else
					{
						DeviceExtension->Disk.SpecialFileCount--;
					}
				Irp->IoStatus.Information = 0;
				Status = STATUS_SUCCESS;
				break;
			case IRP_MN_QUERY_PNP_DEVICE_STATE:
				Irp->IoStatus.Information = 0;
				Status = STATUS_SUCCESS;
				break;
			case IRP_MN_START_DEVICE:
				DeviceExtension->OldState = DeviceExtension->State;
				DeviceExtension->State = Started;
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
			case IRP_MN_REMOVE_DEVICE:
				DeviceExtension->OldState = DeviceExtension->State;
				DeviceExtension->State = NotStarted;
				if ( DeviceExtension->Disk.Unmount )
					{
						IoDeleteDevice ( DeviceExtension->Self );
						Status = STATUS_NO_SUCH_DEVICE;
					}
				else
					{
						Status = STATUS_SUCCESS;
					}
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
	IoCompleteRequest ( Irp, IO_NO_INCREMENT );
	return Status;
}

NTSTATUS STDCALL
DiskDispatchSCSI (
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp,
	IN PIO_STACK_LOCATION Stack,
	IN PDEVICEEXTENSION DeviceExtension
 )
{
	NTSTATUS Status;
	PSCSI_REQUEST_BLOCK Srb;
	PCDB Cdb;
	LONGLONG StartSector;
	ULONG SectorCount,
	 Temp;
	LONGLONG LargeTemp;
	PMODE_PARAMETER_HEADER ModeParameterHeader;

	Srb = Stack->Parameters.Scsi.Srb;
	Cdb = ( PCDB ) Srb->Cdb;

	Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
	Srb->ScsiStatus = SCSISTAT_GOOD;
	Irp->IoStatus.Information = 0;
	Status = STATUS_SUCCESS;
	if ( Srb->Lun == 0 )
		{
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
									if ( Cdb->AsByte[0] == SCSIOP_READ16
											 || Cdb->AsByte[0] == SCSIOP_WRITE16 )
										{
											REVERSE_BYTES_QUAD ( &StartSector,
																					 &( ( ( PCDB16 )
																								Cdb )->LogicalBlock[0] ) );
											REVERSE_BYTES ( &SectorCount,
																			&( ( ( PCDB16 )
																					 Cdb )->TransferLength[0] ) );
										}
									else
										{
											StartSector =
												( Cdb->CDB10.LogicalBlockByte0 << 24 ) +
												( Cdb->CDB10.LogicalBlockByte1 << 16 ) +
												( Cdb->CDB10.LogicalBlockByte2 << 8 ) +
												Cdb->CDB10.LogicalBlockByte3;
											SectorCount =
												( Cdb->CDB10.TransferBlocksMsb << 8 ) +
												Cdb->CDB10.TransferBlocksLsb;
										}
									if ( StartSector >= DeviceExtension->Disk.LBADiskSize )
										{
											DBG ( "Fixed SectorCount (StartSector off disk)!!\n" );
											SectorCount = 0;
										}
									if ( ( StartSector + SectorCount >
												 DeviceExtension->Disk.LBADiskSize )
											 && SectorCount != 0 )
										{
											DBG ( "Fixed SectorCount (StartSector + "
														"SectorCount off disk)!!\n" );
											SectorCount =
												( ULONG ) ( DeviceExtension->Disk.LBADiskSize -
																		StartSector );
										}
									if ( SectorCount * SECTORSIZE > Srb->DataTransferLength )
										{
											DBG ( "Fixed SectorCount (DataTransferLength "
														"too small)!!\n" );
											SectorCount = Srb->DataTransferLength / SECTORSIZE;
										}
									if ( Srb->DataTransferLength % SECTORSIZE != 0 )
										DBG ( "DataTransferLength not aligned!!\n" );
									if ( Srb->DataTransferLength > SectorCount * SECTORSIZE )
										DBG ( "DataTransferLength too big!!\n" );

									Srb->DataTransferLength = SectorCount * SECTORSIZE;
									Srb->SrbStatus = SRB_STATUS_SUCCESS;
									if ( SectorCount == 0 )
										{
											Irp->IoStatus.Information = 0;
											break;
										}

									if ( ( ( ( PUCHAR ) Srb->DataBuffer -
													 ( PUCHAR )
													 MmGetMdlVirtualAddress ( Irp->MdlAddress ) ) +
												 ( PUCHAR )
												 MmGetSystemAddressForMdlSafe ( Irp->MdlAddress,
																												HighPagePriority ) ) ==
											 NULL )
										{
											Status = STATUS_INSUFFICIENT_RESOURCES;
											Irp->IoStatus.Information = 0;
											break;
										}

									if ( Cdb->AsByte[0] == SCSIOP_READ
											 || Cdb->AsByte[0] == SCSIOP_READ16 )
										{
											return AoERequest ( DeviceExtension, Read, StartSector,
																					SectorCount,
																					( ( PUCHAR ) Srb->DataBuffer -
																						( PUCHAR )
																						MmGetMdlVirtualAddress
																						( Irp->MdlAddress ) ) +
																					( PUCHAR )
																					MmGetSystemAddressForMdlSafe
																					( Irp->MdlAddress,
																						HighPagePriority ), Irp );
										}
									else
										{
											return AoERequest ( DeviceExtension, Write, StartSector,
																					SectorCount,
																					( ( PUCHAR ) Srb->DataBuffer -
																						( PUCHAR )
																						MmGetMdlVirtualAddress
																						( Irp->MdlAddress ) ) +
																					( PUCHAR )
																					MmGetSystemAddressForMdlSafe
																					( Irp->MdlAddress,
																						HighPagePriority ), Irp );
										}
									break;
								case SCSIOP_VERIFY:
								case SCSIOP_VERIFY16:
									if ( Cdb->AsByte[0] == SCSIOP_VERIFY16 )
										{
											REVERSE_BYTES_QUAD ( &StartSector,
																					 &( ( ( PCDB16 )
																								Cdb )->LogicalBlock[0] ) );
											REVERSE_BYTES ( &SectorCount,
																			&( ( ( PCDB16 )
																					 Cdb )->TransferLength[0] ) );
										}
									else
										{
											StartSector =
												( Cdb->CDB10.LogicalBlockByte0 << 24 ) +
												( Cdb->CDB10.LogicalBlockByte1 << 16 ) +
												( Cdb->CDB10.LogicalBlockByte2 << 8 ) +
												Cdb->CDB10.LogicalBlockByte3;
											SectorCount =
												( Cdb->CDB10.TransferBlocksMsb << 8 ) +
												Cdb->CDB10.TransferBlocksLsb;
										}
/*            Srb->DataTransferLength = SectorCount * SECTORSIZE;
 */
									Srb->SrbStatus = SRB_STATUS_SUCCESS;
									break;
								case SCSIOP_READ_CAPACITY:
									Temp = SECTORSIZE;
									REVERSE_BYTES ( &
																	( ( ( PREAD_CAPACITY_DATA )
																			Srb->DataBuffer )->BytesPerBlock ),
																	&Temp );
									if ( ( DeviceExtension->Disk.LBADiskSize - 1 ) > 0xffffffff )
										{
											( ( PREAD_CAPACITY_DATA ) Srb->
												DataBuffer )->LogicalBlockAddress = -1;
										}
									else
										{
											Temp =
												( ULONG ) ( DeviceExtension->Disk.LBADiskSize - 1 );
											REVERSE_BYTES ( &
																			( ( ( PREAD_CAPACITY_DATA )
																					Srb->DataBuffer )->
																				LogicalBlockAddress ), &Temp );
										}
									Irp->IoStatus.Information = sizeof ( READ_CAPACITY_DATA );
									Srb->SrbStatus = SRB_STATUS_SUCCESS;
									Status = STATUS_SUCCESS;
									break;
								case SCSIOP_READ_CAPACITY16:
									Temp = SECTORSIZE;
									REVERSE_BYTES ( &
																	( ( ( PREAD_CAPACITY_DATA_EX )
																			Srb->DataBuffer )->BytesPerBlock ),
																	&Temp );
									LargeTemp = DeviceExtension->Disk.LBADiskSize - 1;
									REVERSE_BYTES_QUAD ( &
																			 ( ( ( PREAD_CAPACITY_DATA_EX )
																					 Srb->DataBuffer )->
																				 LogicalBlockAddress.QuadPart ),
																			 &LargeTemp );
									Irp->IoStatus.Information = sizeof ( READ_CAPACITY_DATA_EX );
									Srb->SrbStatus = SRB_STATUS_SUCCESS;
									Status = STATUS_SUCCESS;
									break;
								case SCSIOP_MODE_SENSE:
									if ( Srb->DataTransferLength <
											 sizeof ( MODE_PARAMETER_HEADER ) )
										{
											Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
											break;
										}
									ModeParameterHeader =
										( PMODE_PARAMETER_HEADER ) Srb->DataBuffer;
									RtlZeroMemory ( ModeParameterHeader,
																	Srb->DataTransferLength );
									ModeParameterHeader->ModeDataLength =
										sizeof ( MODE_PARAMETER_HEADER );
									ModeParameterHeader->MediumType = FixedMedia;
									ModeParameterHeader->BlockDescriptorLength = 0;
									Srb->DataTransferLength = sizeof ( MODE_PARAMETER_HEADER );
									Irp->IoStatus.Information = sizeof ( MODE_PARAMETER_HEADER );
									Srb->SrbStatus = SRB_STATUS_SUCCESS;
									break;
								case SCSIOP_MEDIUM_REMOVAL:
									Irp->IoStatus.Information = 0;
									Srb->SrbStatus = SRB_STATUS_SUCCESS;
									Status = STATUS_SUCCESS;
									break;
								default:
									DBG ( "Invalid SCSIOP (%02x)!!\n", Cdb->AsByte[0] );
									Srb->SrbStatus = SRB_STATUS_ERROR;
									Status = STATUS_NOT_IMPLEMENTED;
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
						Status = STATUS_NOT_IMPLEMENTED;
				}
		}
	else
		{
			DBG ( "Invalid Lun!!\n" );
		}

	Irp->IoStatus.Status = Status;
	IoCompleteRequest ( Irp, IO_NO_INCREMENT );
	return Status;
}

NTSTATUS STDCALL
DiskDispatchDeviceControl (
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp,
	IN PIO_STACK_LOCATION Stack,
	IN PDEVICEEXTENSION DeviceExtension
 )
{
	NTSTATUS Status;
	ULONG CopySize;
	PSTORAGE_PROPERTY_QUERY StoragePropertyQuery;
	STORAGE_ADAPTER_DESCRIPTOR StorageAdapterDescriptor;
	STORAGE_DEVICE_DESCRIPTOR StorageDeviceDescriptor;
	DISK_GEOMETRY DiskGeometry;
	SCSI_ADDRESS ScsiAdress;

	switch ( Stack->Parameters.DeviceIoControl.IoControlCode )
		{
			case IOCTL_STORAGE_QUERY_PROPERTY:
				StoragePropertyQuery = Irp->AssociatedIrp.SystemBuffer;
				Status = STATUS_INVALID_PARAMETER;

				if ( StoragePropertyQuery->PropertyId == StorageAdapterProperty
						 && StoragePropertyQuery->QueryType == PropertyStandardQuery )
					{
						CopySize =
							( Stack->Parameters.DeviceIoControl.OutputBufferLength <
								sizeof ( STORAGE_ADAPTER_DESCRIPTOR ) ? Stack->
								Parameters.DeviceIoControl.
								OutputBufferLength : sizeof ( STORAGE_ADAPTER_DESCRIPTOR ) );
						StorageAdapterDescriptor.Version =
							sizeof ( STORAGE_ADAPTER_DESCRIPTOR );
						StorageAdapterDescriptor.Size =
							sizeof ( STORAGE_ADAPTER_DESCRIPTOR );
						StorageAdapterDescriptor.MaximumTransferLength =
							SECTORSIZE * DeviceExtension->Disk.AoE.MaxSectorsPerPacket;
/*        StorageAdapterDescriptor.MaximumTransferLength = SECTORSIZE * POOLSIZE;
 */
						StorageAdapterDescriptor.MaximumPhysicalPages = ( ULONG ) - 1;
						StorageAdapterDescriptor.AlignmentMask = 0;
						StorageAdapterDescriptor.AdapterUsesPio = TRUE;
						StorageAdapterDescriptor.AdapterScansDown = FALSE;
						StorageAdapterDescriptor.CommandQueueing = FALSE;
						StorageAdapterDescriptor.AcceleratedTransfer = FALSE;
						StorageAdapterDescriptor.BusType = BusTypeScsi;
						RtlCopyMemory ( Irp->AssociatedIrp.SystemBuffer,
														&StorageAdapterDescriptor, CopySize );
						Irp->IoStatus.Information = ( ULONG_PTR ) CopySize;
						Status = STATUS_SUCCESS;
					}

				if ( StoragePropertyQuery->PropertyId == StorageDeviceProperty
						 && StoragePropertyQuery->QueryType == PropertyStandardQuery )
					{
						CopySize =
							( Stack->Parameters.DeviceIoControl.OutputBufferLength <
								sizeof ( STORAGE_DEVICE_DESCRIPTOR ) ? Stack->
								Parameters.DeviceIoControl.
								OutputBufferLength : sizeof ( STORAGE_DEVICE_DESCRIPTOR ) );
						StorageDeviceDescriptor.Version =
							sizeof ( STORAGE_DEVICE_DESCRIPTOR );
						StorageDeviceDescriptor.Size =
							sizeof ( STORAGE_DEVICE_DESCRIPTOR );
						StorageDeviceDescriptor.DeviceType = DIRECT_ACCESS_DEVICE;
						StorageDeviceDescriptor.DeviceTypeModifier = 0;
						StorageDeviceDescriptor.RemovableMedia = FALSE;
						StorageDeviceDescriptor.CommandQueueing = FALSE;
						StorageDeviceDescriptor.VendorIdOffset = 0;
						StorageDeviceDescriptor.ProductIdOffset = 0;
						StorageDeviceDescriptor.ProductRevisionOffset = 0;
						StorageDeviceDescriptor.SerialNumberOffset = 0;
						StorageDeviceDescriptor.BusType = BusTypeScsi;
						StorageDeviceDescriptor.RawPropertiesLength = 0;
						RtlCopyMemory ( Irp->AssociatedIrp.SystemBuffer,
														&StorageDeviceDescriptor, CopySize );
						Irp->IoStatus.Information = ( ULONG_PTR ) CopySize;
						Status = STATUS_SUCCESS;
					}

				if ( Status == STATUS_INVALID_PARAMETER )
					{
						DBG ( "!!Invalid IOCTL_STORAGE_QUERY_PROPERTY "
									"(PropertyId: %08x / QueryType: %08x)!!\n",
									StoragePropertyQuery->PropertyId,
									StoragePropertyQuery->QueryType );
					}
				break;
			case IOCTL_DISK_GET_DRIVE_GEOMETRY:
				CopySize =
					( Stack->Parameters.DeviceIoControl.OutputBufferLength <
						sizeof ( DISK_GEOMETRY ) ? Stack->Parameters.DeviceIoControl.
						OutputBufferLength : sizeof ( DISK_GEOMETRY ) );
				DiskGeometry.MediaType = FixedMedia;
				DiskGeometry.Cylinders.QuadPart = DeviceExtension->Disk.Cylinders;
				DiskGeometry.TracksPerCylinder = DeviceExtension->Disk.Heads;
				DiskGeometry.SectorsPerTrack = DeviceExtension->Disk.Sectors;
				DiskGeometry.BytesPerSector = SECTORSIZE;
				RtlCopyMemory ( Irp->AssociatedIrp.SystemBuffer, &DiskGeometry,
												CopySize );
				Irp->IoStatus.Information = ( ULONG_PTR ) CopySize;
				Status = STATUS_SUCCESS;
				break;
			case IOCTL_SCSI_GET_ADDRESS:
				CopySize =
					( Stack->Parameters.DeviceIoControl.OutputBufferLength <
						sizeof ( SCSI_ADDRESS ) ? Stack->Parameters.DeviceIoControl.
						OutputBufferLength : sizeof ( SCSI_ADDRESS ) );
				ScsiAdress.Length = sizeof ( SCSI_ADDRESS );
				ScsiAdress.PortNumber = 0;
				ScsiAdress.PathId = 0;
				ScsiAdress.TargetId = ( UCHAR ) DeviceExtension->Disk.DiskNumber;
				ScsiAdress.Lun = 0;
				RtlCopyMemory ( Irp->AssociatedIrp.SystemBuffer, &ScsiAdress,
												CopySize );
				Irp->IoStatus.Information = ( ULONG_PTR ) CopySize;
				Status = STATUS_SUCCESS;
				break;
			default:
				Irp->IoStatus.Information = 0;
				Status = STATUS_INVALID_PARAMETER;
		}

	Irp->IoStatus.Status = Status;
	IoCompleteRequest ( Irp, IO_NO_INCREMENT );
	return Status;
}

NTSTATUS STDCALL
DiskDispatchSystemControl (
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp,
	IN PIO_STACK_LOCATION Stack,
	IN PDEVICEEXTENSION DeviceExtension
 )
{
	NTSTATUS Status;

	Status = Irp->IoStatus.Status;
	IoCompleteRequest ( Irp, IO_NO_INCREMENT );
	return Status;
}

/* FIXME put in SCSI */
static NTSTATUS STDCALL
BusGetDeviceCapabilities (
	IN PDEVICE_OBJECT DeviceObject,
	IN PDEVICE_CAPABILITIES DeviceCapabilities
 )
{
	IO_STATUS_BLOCK ioStatus;
	KEVENT pnpEvent;
	NTSTATUS status;
	PDEVICE_OBJECT targetObject;
	PIO_STACK_LOCATION irpStack;
	PIRP pnpIrp;

	RtlZeroMemory ( DeviceCapabilities, sizeof ( DEVICE_CAPABILITIES ) );
	DeviceCapabilities->Size = sizeof ( DEVICE_CAPABILITIES );
	DeviceCapabilities->Version = 1;
	DeviceCapabilities->Address = -1;
	DeviceCapabilities->UINumber = -1;

	KeInitializeEvent ( &pnpEvent, NotificationEvent, FALSE );
	targetObject = IoGetAttachedDeviceReference ( DeviceObject );
	pnpIrp =
		IoBuildSynchronousFsdRequest ( IRP_MJ_PNP, targetObject, NULL, 0, NULL,
																	 &pnpEvent, &ioStatus );
	if ( pnpIrp == NULL )
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
		}
	else
		{
			pnpIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;
			irpStack = IoGetNextIrpStackLocation ( pnpIrp );
			RtlZeroMemory ( irpStack, sizeof ( IO_STACK_LOCATION ) );
			irpStack->MajorFunction = IRP_MJ_PNP;
			irpStack->MinorFunction = IRP_MN_QUERY_CAPABILITIES;
			irpStack->Parameters.DeviceCapabilities.Capabilities =
				DeviceCapabilities;
			status = IoCallDriver ( targetObject, pnpIrp );
			if ( status == STATUS_PENDING )
				{
					KeWaitForSingleObject ( &pnpEvent, Executive, KernelMode, FALSE,
																	NULL );
					status = ioStatus.Status;
				}
		}
	ObDereferenceObject ( targetObject );
	return status;
}
