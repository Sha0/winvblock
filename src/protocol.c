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
 * Protocol specifics
 *
 */

#include <ntddk.h>
#include <ndis.h>
#include <ntddndis.h>

#include "winvblock.h"
#include "portable.h"
#include "protocol.h"
#include "driver.h"
#include "aoe.h"
#include "debug.h"

/* in this file */
static VOID STDCALL Protocol_OpenAdapterComplete (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN NDIS_STATUS Status,
	IN NDIS_STATUS OpenErrorStatus
 );
static VOID STDCALL Protocol_CloseAdapterComplete (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN NDIS_STATUS Status
 );
static VOID STDCALL Protocol_SendComplete (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN PNDIS_PACKET Packet,
	IN NDIS_STATUS Status
 );
static VOID STDCALL Protocol_TransferDataComplete (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN PNDIS_PACKET Packet,
	IN NDIS_STATUS Status,
	IN UINT BytesTransferred
 );
static VOID STDCALL Protocol_ResetComplete (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN NDIS_STATUS Status
 );
static VOID STDCALL Protocol_RequestComplete (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN PNDIS_REQUEST NdisRequest,
	IN NDIS_STATUS Status
 );
static NDIS_STATUS STDCALL Protocol_Receive (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN NDIS_HANDLE MacReceiveContext,
	IN PVOID HeaderBuffer,
	IN UINT HeaderBufferSize,
	IN PVOID LookAheadBuffer,
	IN UINT LookaheadBufferSize,
	IN UINT PacketSize
 );
static VOID STDCALL Protocol_ReceiveComplete (
	IN NDIS_HANDLE ProtocolBindingContext
 );
static VOID STDCALL Protocol_Status (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN NDIS_STATUS GeneralStatus,
	IN PVOID StatusBuffer,
	IN UINT StatusBufferSize
 );
static VOID STDCALL Protocol_StatusComplete (
	IN NDIS_HANDLE ProtocolBindingContext
 );
static VOID STDCALL Protocol_BindAdapter (
	OUT PNDIS_STATUS Status,
	IN NDIS_HANDLE BindContext,
	IN PNDIS_STRING DeviceName,
	IN PVOID SystemSpecific1,
	IN PVOID SystemSpecific2
 );
static VOID STDCALL Protocol_UnbindAdapter (
	OUT PNDIS_STATUS Status,
	IN NDIS_HANDLE ProtocolBindingContext,
	IN NDIS_HANDLE UnbindContext
 );
static NDIS_STATUS STDCALL Protocol_PnPEvent (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN PNET_PNP_EVENT NetPnPEvent
 );
static PCHAR STDCALL Protocol_NetEventString (
	IN NET_PNP_EVENT_CODE NetEvent
 );

#ifdef _MSC_VER
#  pragma pack(1)
#endif
typedef struct _PROTOCOL_HEADER
{
	winvblock__uint8 DestinationMac[6];
	winvblock__uint8 SourceMac[6];
	winvblock__uint16 Protocol;
	winvblock__uint8 Data[];
} __attribute__ ( ( __packed__ ) ) PROTOCOL_HEADER, *PPROTOCOL_HEADER;
#ifdef _MSC_VER
#  pragma pack()
#endif

typedef struct _PROTOCOL_BINDINGCONTEXT
{
	BOOLEAN Active;
	winvblock__uint8 Mac[6];
	UINT MTU;
	NDIS_STATUS Status;
	NDIS_HANDLE PacketPoolHandle;
	NDIS_HANDLE BufferPoolHandle;
	NDIS_HANDLE BindingHandle;
	KEVENT Event;
	BOOLEAN OutstandingRequest;
	PWCHAR AdapterName;
	PWCHAR DeviceName;
	struct _PROTOCOL_BINDINGCONTEXT *Next;
} PROTOCOL_BINDINGCONTEXT,
*PPROTOCOL_BINDINGCONTEXT;

static KEVENT Protocol_Globals_StopEvent;
static KSPIN_LOCK Protocol_Globals_SpinLock;
static PPROTOCOL_BINDINGCONTEXT Protocol_Globals_BindingContextList = NULL;
static NDIS_HANDLE Protocol_Globals_Handle = NULL;
static BOOLEAN Protocol_Globals_Started = FALSE;

NTSTATUS STDCALL
Protocol_Start (
	void
 )
{
	NDIS_STATUS Status;
	NDIS_STRING ProtocolName;
	NDIS_PROTOCOL_CHARACTERISTICS ProtocolCharacteristics;

	DBG ( "Entry\n" );
	if ( Protocol_Globals_Started )
		return STATUS_SUCCESS;
	KeInitializeEvent ( &Protocol_Globals_StopEvent, SynchronizationEvent,
											FALSE );
	KeInitializeSpinLock ( &Protocol_Globals_SpinLock );

	RtlInitUnicodeString ( &ProtocolName, L"WinVBlock" );
	NdisZeroMemory ( &ProtocolCharacteristics,
									 sizeof ( NDIS_PROTOCOL_CHARACTERISTICS ) );
	ProtocolCharacteristics.MajorNdisVersion = 5;
	ProtocolCharacteristics.MinorNdisVersion = 0;
	ProtocolCharacteristics.Name = ProtocolName;
	ProtocolCharacteristics.OpenAdapterCompleteHandler =
		Protocol_OpenAdapterComplete;
	ProtocolCharacteristics.CloseAdapterCompleteHandler =
		Protocol_CloseAdapterComplete;
	ProtocolCharacteristics.SendCompleteHandler = Protocol_SendComplete;
	ProtocolCharacteristics.TransferDataCompleteHandler =
		Protocol_TransferDataComplete;
	ProtocolCharacteristics.ResetCompleteHandler = Protocol_ResetComplete;
	ProtocolCharacteristics.RequestCompleteHandler = Protocol_RequestComplete;
	ProtocolCharacteristics.ReceiveHandler = Protocol_Receive;
	ProtocolCharacteristics.ReceiveCompleteHandler = Protocol_ReceiveComplete;
	ProtocolCharacteristics.StatusHandler = Protocol_Status;
	ProtocolCharacteristics.StatusCompleteHandler = Protocol_StatusComplete;
	ProtocolCharacteristics.BindAdapterHandler = Protocol_BindAdapter;
	ProtocolCharacteristics.UnbindAdapterHandler = Protocol_UnbindAdapter;
	ProtocolCharacteristics.PnPEventHandler = Protocol_PnPEvent;
	NdisRegisterProtocol ( ( PNDIS_STATUS ) & Status, &Protocol_Globals_Handle,
												 &ProtocolCharacteristics,
												 sizeof ( NDIS_PROTOCOL_CHARACTERISTICS ) );
	Protocol_Globals_Started = TRUE;
	return Status;
}

VOID STDCALL
Protocol_Stop (
	void
 )
{
	NDIS_STATUS Status;

	DBG ( "Entry\n" );
	if ( !Protocol_Globals_Started )
		return;
	KeResetEvent ( &Protocol_Globals_StopEvent );
	NdisDeregisterProtocol ( &Status, Protocol_Globals_Handle );
	if ( !NT_SUCCESS ( Status ) )
		Error ( "NdisDeregisterProtocol", Status );
	if ( Protocol_Globals_BindingContextList != NULL )
		KeWaitForSingleObject ( &Protocol_Globals_StopEvent, Executive, KernelMode,
														FALSE, NULL );
	Protocol_Globals_Started = FALSE;
}

BOOLEAN STDCALL
Protocol_SearchNIC (
	IN winvblock__uint8_ptr Mac
 )
{
	PPROTOCOL_BINDINGCONTEXT Context = Protocol_Globals_BindingContextList;

	while ( Context != NULL )
		{
			if ( RtlCompareMemory ( Mac, Context->Mac, 6 ) == 6 )
				break;
			Context = Context->Next;
		}
	if ( Context != NULL )
		return TRUE;
	return FALSE;
}

ULONG STDCALL
Protocol_GetMTU (
	IN winvblock__uint8_ptr Mac
 )
{
	PPROTOCOL_BINDINGCONTEXT Context = Protocol_Globals_BindingContextList;

	while ( Context != NULL )
		{
			if ( RtlCompareMemory ( Mac, Context->Mac, 6 ) == 6 )
				break;
			Context = Context->Next;
		}
	if ( Context == NULL )
		return 0;
	return Context->MTU;
}

BOOLEAN STDCALL
Protocol_Send (
	IN winvblock__uint8_ptr SourceMac,
	IN winvblock__uint8_ptr DestinationMac,
	IN winvblock__uint8_ptr Data,
	IN ULONG DataSize,
	IN PVOID PacketContext
 )
{
	PPROTOCOL_BINDINGCONTEXT Context = Protocol_Globals_BindingContextList;
	NDIS_STATUS Status;
	PNDIS_PACKET Packet;
	PNDIS_BUFFER Buffer;
	PPROTOCOL_HEADER DataBuffer;
#if defined(DEBUGALLPROTOCOLCALLS)
	DBG ( "Entry\n" );
#endif

	if ( RtlCompareMemory ( SourceMac, "\xff\xff\xff\xff\xff\xff", 6 ) == 6 )
		{
			while ( Context != NULL )
				{
					Protocol_Send ( Context->Mac, DestinationMac, Data, DataSize, NULL );
					Context = Context->Next;
				}
			return TRUE;
		}

	while ( Context != NULL )
		{
			if ( RtlCompareMemory ( SourceMac, Context->Mac, 6 ) == 6 )
				break;
			Context = Context->Next;
		}
	if ( Context == NULL )
		{
			DBG ( "Can't find NIC %02x:%02x:%02x:%02x:%02x:%02x\n", SourceMac[0],
						SourceMac[1], SourceMac[2], SourceMac[3], SourceMac[4],
						SourceMac[5] );
			return FALSE;
		}

	if ( DataSize > Context->MTU )
		{
			DBG ( "Tried to send oversized packet (size: %d, MTU:)\n", DataSize,
						Context->MTU );
			return FALSE;
		}

	if ( ( DataBuffer =
				 ( PPROTOCOL_HEADER ) ExAllocatePool ( NonPagedPool,
																							 ( sizeof ( PROTOCOL_HEADER ) +
																								 DataSize ) ) ) == NULL )
		{
			DBG ( "ExAllocatePool DataBuffer\n" );
			return FALSE;
		}

	RtlCopyMemory ( DataBuffer->SourceMac, SourceMac, 6 );
	RtlCopyMemory ( DataBuffer->DestinationMac, DestinationMac, 6 );
	DataBuffer->Protocol = htons ( AOEPROTOCOLID );
	RtlCopyMemory ( DataBuffer->Data, Data, DataSize );

	NdisAllocatePacket ( &Status, &Packet, Context->PacketPoolHandle );
	if ( !NT_SUCCESS ( Status ) )
		{
			Error ( "Protocol_Send NdisAllocatePacket", Status );
			ExFreePool ( DataBuffer );
			return FALSE;
		}

	NdisAllocateBuffer ( &Status, &Buffer, Context->BufferPoolHandle, DataBuffer,
											 ( sizeof ( PROTOCOL_HEADER ) + DataSize ) );
	if ( !NT_SUCCESS ( Status ) )
		{
			Error ( "Protocol_Send NdisAllocateBuffer", Status );
			NdisFreePacket ( Packet );
			ExFreePool ( DataBuffer );
			return FALSE;
		}

	NdisChainBufferAtFront ( Packet, Buffer );
	*( PVOID * ) Packet->ProtocolReserved = PacketContext;
	NdisSend ( &Status, Context->BindingHandle, Packet );
	if ( Status != NDIS_STATUS_PENDING )
		Protocol_SendComplete ( Context, Packet, Status );
	return TRUE;
}

static VOID STDCALL
Protocol_OpenAdapterComplete (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN NDIS_STATUS Status,
	IN NDIS_STATUS OpenErrorStatus
 )
{
#if !defined(DEBUGMOSTPROTOCOLCALLS) && !defined(DEBUGALLPROTOCOLCALLS)
	if ( !NT_SUCCESS ( Status ) )
#endif
		DBG ( "0x%08x 0x%08x\n", Status, OpenErrorStatus );
}

static VOID STDCALL
Protocol_CloseAdapterComplete (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN NDIS_STATUS Status
 )
{
#if !defined(DEBUGMOSTPROTOCOLCALLS) && !defined(DEBUGALLPROTOCOLCALLS)
	if ( !NT_SUCCESS ( Status ) )
#endif
		Error ( "Protocol_CloseAdapterComplete", Status );
}

static VOID STDCALL
Protocol_SendComplete (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN PNDIS_PACKET Packet,
	IN NDIS_STATUS Status
 )
{
	PNDIS_BUFFER Buffer;
	winvblock__uint8_ptr DataBuffer;
#ifndef DEBUGALLPROTOCOLCALLS
	if ( !NT_SUCCESS ( Status ) && Status != NDIS_STATUS_NO_CABLE )
#endif
		Error ( "Protocol_SendComplete", Status );

	NdisUnchainBufferAtFront ( Packet, &Buffer );
	if ( Buffer != NULL )
		{
			DataBuffer = NdisBufferVirtualAddress ( Buffer );
			ExFreePool ( DataBuffer );
			NdisFreeBuffer ( Buffer );
		}
	else
		{
			DBG ( "Buffer == NULL\n" );
		}
	NdisFreePacket ( Packet );
}

static VOID STDCALL
Protocol_TransferDataComplete (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN PNDIS_PACKET Packet,
	IN NDIS_STATUS Status,
	IN UINT BytesTransferred
 )
{
	PNDIS_BUFFER Buffer;
	PPROTOCOL_HEADER Header = NULL;
	winvblock__uint8_ptr Data = NULL;
	UINT HeaderSize,
	 DataSize;
#ifndef DEBUGALLPROTOCOLCALLS
	if ( !NT_SUCCESS ( Status ) )
#endif
		Error ( "Protocol_TransferDataComplete", Status );

	NdisUnchainBufferAtFront ( Packet, &Buffer );
	if ( Buffer != NULL )
		{
			NdisQueryBuffer ( Buffer, &Data, &DataSize );
			NdisFreeBuffer ( Buffer );
		}
	else
		{
			DBG ( "Data (front) Buffer == NULL\n" );
		}
	NdisUnchainBufferAtBack ( Packet, &Buffer );
	if ( Buffer != NULL )
		{
			NdisQueryBuffer ( Buffer, &Header, &HeaderSize );
			NdisFreeBuffer ( Buffer );
		}
	else
		{
			DBG ( "Header (back) Buffer == NULL\n" );
		}
	if ( Header != NULL && Data != NULL )
		AoE_Reply ( Header->SourceMac, Header->DestinationMac, Data, DataSize );
	if ( Header != NULL )
		ExFreePool ( Header );
	if ( Data != NULL )
		ExFreePool ( Data );
	NdisFreePacket ( Packet );
}

static VOID STDCALL
Protocol_ResetComplete (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN NDIS_STATUS Status
 )
{
#if !defined(DEBUGMOSTPROTOCOLCALLS) && !defined(DEBUGALLPROTOCOLCALLS)
	if ( !NT_SUCCESS ( Status ) )
#endif
		Error ( "Protocol_ResetComplete", Status );
}

static VOID STDCALL
Protocol_RequestComplete (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN PNDIS_REQUEST NdisRequest,
	IN NDIS_STATUS Status
 )
{
	PPROTOCOL_BINDINGCONTEXT Context =
		( PPROTOCOL_BINDINGCONTEXT ) ProtocolBindingContext;
#if !defined(DEBUGMOSTPROTOCOLCALLS) && !defined(DEBUGALLPROTOCOLCALLS)
	if ( !NT_SUCCESS ( Status ) )
#endif
		Error ( "Protocol_RequestComplete", Status );

	Context->Status = Status;
	KeSetEvent ( &Context->Event, 0, FALSE );
}

static NDIS_STATUS STDCALL
Protocol_Receive (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN NDIS_HANDLE MacReceiveContext,
	IN PVOID HeaderBuffer,
	IN UINT HeaderBufferSize,
	IN PVOID LookAheadBuffer,
	IN UINT LookaheadBufferSize,
	IN UINT PacketSize
 )
{
	PPROTOCOL_BINDINGCONTEXT Context =
		( PPROTOCOL_BINDINGCONTEXT ) ProtocolBindingContext;
	NDIS_STATUS Status;
	PNDIS_PACKET Packet;
	PNDIS_BUFFER Buffer;
	PPROTOCOL_HEADER Header;
	winvblock__uint8_ptr HeaderCopy,
	 Data;
	UINT BytesTransferred;
#ifdef DEBUGALLPROTOCOLCALLS
	DBG ( "Entry\n" );
#endif

	if ( HeaderBufferSize != sizeof ( PROTOCOL_HEADER ) )
		{
			DBG ( "HeaderBufferSize %d != sizeof(PROTOCOL_HEADER) %d\n" );
			return NDIS_STATUS_NOT_ACCEPTED;
		}
	Header = ( PPROTOCOL_HEADER ) HeaderBuffer;
	if ( ntohs ( Header->Protocol ) != AOEPROTOCOLID )
		return NDIS_STATUS_NOT_ACCEPTED;

	if ( LookaheadBufferSize == PacketSize )
		{
			AoE_Reply ( Header->SourceMac, Header->DestinationMac, LookAheadBuffer,
									PacketSize );
			return NDIS_STATUS_SUCCESS;
		}

	if ( ( HeaderCopy =
				 ( winvblock__uint8_ptr ) ExAllocatePool ( NonPagedPool,
																									 HeaderBufferSize ) ) ==
			 NULL )
		{
			DBG ( "ExAllocatePool HeaderCopy\n" );
			return NDIS_STATUS_NOT_ACCEPTED;
		}
	RtlCopyMemory ( HeaderCopy, HeaderBuffer, HeaderBufferSize );
	if ( ( Data =
				 ( winvblock__uint8_ptr ) ExAllocatePool ( NonPagedPool,
																									 PacketSize ) ) == NULL )
		{
			DBG ( "ExAllocatePool HeaderData\n" );
			ExFreePool ( HeaderCopy );
			return NDIS_STATUS_NOT_ACCEPTED;
		}
	NdisAllocatePacket ( &Status, &Packet, Context->PacketPoolHandle );
	if ( !NT_SUCCESS ( Status ) )
		{
			Error ( "Protocol_Receive NdisAllocatePacket", Status );
			ExFreePool ( Data );
			ExFreePool ( HeaderCopy );
			return NDIS_STATUS_NOT_ACCEPTED;
		}

	NdisAllocateBuffer ( &Status, &Buffer, Context->BufferPoolHandle, Data,
											 PacketSize );
	if ( !NT_SUCCESS ( Status ) )
		{
			Error ( "Protocol_Receive NdisAllocateBuffer (Data)", Status );
			NdisFreePacket ( Packet );
			ExFreePool ( Data );
			ExFreePool ( HeaderCopy );
			return NDIS_STATUS_NOT_ACCEPTED;
		}
	NdisChainBufferAtFront ( Packet, Buffer );

	NdisAllocateBuffer ( &Status, &Buffer, Context->BufferPoolHandle, HeaderCopy,
											 PacketSize );
	if ( !NT_SUCCESS ( Status ) )
		{
			Error ( "Protocol_Receive NdisAllocateBuffer (HeaderCopy)", Status );
			NdisUnchainBufferAtFront ( Packet, &Buffer );
			NdisFreeBuffer ( Buffer );
			NdisFreePacket ( Packet );
			ExFreePool ( Data );
			ExFreePool ( HeaderCopy );
			return NDIS_STATUS_NOT_ACCEPTED;
		}
	NdisChainBufferAtBack ( Packet, Buffer );

	NdisTransferData ( &Status, Context->BindingHandle, MacReceiveContext, 0,
										 PacketSize, Packet, &BytesTransferred );
	if ( Status != NDIS_STATUS_PENDING )
		Protocol_TransferDataComplete ( ProtocolBindingContext, Packet, Status,
																		BytesTransferred );
	return Status;
}

static VOID STDCALL
Protocol_ReceiveComplete (
	IN NDIS_HANDLE ProtocolBindingContext
 )
{
#ifdef DEBUGALLPROTOCOLCALLS
	DBG ( "Entry\n" );
#endif
}

static VOID STDCALL
Protocol_Status (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN NDIS_STATUS GeneralStatus,
	IN PVOID StatusBuffer,
	IN UINT StatusBufferSize
 )
{
#if !defined(DEBUGMOSTPROTOCOLCALLS) && !defined(DEBUGALLPROTOCOLCALLS)
	if ( !NT_SUCCESS ( GeneralStatus ) )
#endif
		Error ( "Protocol_Status", GeneralStatus );
}

static VOID STDCALL
Protocol_StatusComplete (
	IN NDIS_HANDLE ProtocolBindingContext
 )
{
#if defined(DEBUGMOSTPROTOCOLCALLS) || defined(DEBUGALLPROTOCOLCALLS)
	DBG ( "Entry\n" );
#endif
}

static VOID STDCALL
Protocol_BindAdapter (
	OUT PNDIS_STATUS StatusOut,
	IN NDIS_HANDLE BindContext,
	IN PNDIS_STRING DeviceName,
	IN PVOID SystemSpecific1,
	IN PVOID SystemSpecific2
 )
{
	PPROTOCOL_BINDINGCONTEXT Context,
	 Walker;
	NDIS_STATUS Status;
	NDIS_STATUS OpenErrorStatus;
	UINT SelectedMediumIndex,
	 MTU;
	NDIS_MEDIUM MediumArray[] = { NdisMedium802_3 };
	NDIS_STRING AdapterInstanceName;
	NDIS_REQUEST Request;
	ULONG InformationBuffer = NDIS_PACKET_TYPE_DIRECTED;
	winvblock__uint8 Mac[6];
	KIRQL Irql;
#if defined(DEBUGMOSTPROTOCOLCALLS) || defined(DEBUGALLPROTOCOLCALLS)
	DBG ( "Entry\n" );
#endif

	if ( ( Context =
				 ( NDIS_HANDLE ) ExAllocatePool ( NonPagedPool,
																					sizeof
																					( PROTOCOL_BINDINGCONTEXT ) ) ) ==
			 NULL )
		{
			DBG ( "ExAllocatePool Context\n" );
			*StatusOut = NDIS_STATUS_RESOURCES;
			return;
		}
	Context->Next = NULL;
	KeInitializeEvent ( &Context->Event, SynchronizationEvent, FALSE );

	NdisAllocatePacketPool ( &Status, &Context->PacketPoolHandle, POOLSIZE,
													 ( 4 * sizeof ( VOID * ) ) );
	if ( !NT_SUCCESS ( Status ) )
		{
			Error ( "Protocol_BindAdapter NdisAllocatePacketPool", Status );
			ExFreePool ( Context );
			*StatusOut = NDIS_STATUS_RESOURCES;
			return;
		}

	NdisAllocateBufferPool ( &Status, &Context->BufferPoolHandle, POOLSIZE );
	if ( !NT_SUCCESS ( Status ) )
		{
			Error ( "Protocol_BindAdapter NdisAllocateBufferPool", Status );
			NdisFreePacketPool ( Context->PacketPoolHandle );
			ExFreePool ( Context );
			*StatusOut = NDIS_STATUS_RESOURCES;
			return;
		}

	NdisOpenAdapter ( &Status, &OpenErrorStatus, &Context->BindingHandle,
										&SelectedMediumIndex, MediumArray,
										( sizeof ( MediumArray ) / sizeof ( NDIS_MEDIUM ) ),
										Protocol_Globals_Handle, Context, DeviceName, 0, NULL );
	if ( !NT_SUCCESS ( Status ) )
		{
			DBG ( "NdisOpenAdapter 0x%lx 0x%lx\n", Status, OpenErrorStatus );
			NdisFreePacketPool ( Context->PacketPoolHandle );
			NdisFreeBufferPool ( Context->BufferPoolHandle );
			ExFreePool ( Context->AdapterName );
			ExFreePool ( Context->DeviceName );
			ExFreePool ( Context );
			*StatusOut = Status;
			return;
		}
	if ( SelectedMediumIndex != 0 )
		DBG ( "NdisOpenAdapter SelectedMediumIndex: %d\n", SelectedMediumIndex );

	Context->AdapterName = NULL;
	if ( NT_SUCCESS
			 ( Status =
				 NdisQueryAdapterInstanceName ( &AdapterInstanceName,
																				Context->BindingHandle ) ) )
		{
			if ( ( Context->AdapterName =
						 ( PWCHAR ) ExAllocatePool ( NonPagedPool,
																				 AdapterInstanceName.Length +
																				 sizeof ( WCHAR ) ) ) == NULL )
				{
					DBG ( "ExAllocatePool AdapterName\n" );
				}
			else
				{
					RtlZeroMemory ( Context->AdapterName,
													AdapterInstanceName.Length + sizeof ( WCHAR ) );
					RtlCopyMemory ( Context->AdapterName, AdapterInstanceName.Buffer,
													AdapterInstanceName.Length );
				}
			NdisFreeMemory ( AdapterInstanceName.Buffer, 0, 0 );
		}
	else
		{
			Error ( "Protocol_BindAdapter NdisQueryAdapterInstanceName", Status );
		}

	Context->DeviceName = NULL;
	if ( DeviceName->Length > 0 )
		{
			if ( ( Context->DeviceName =
						 ( PWCHAR ) ExAllocatePool ( NonPagedPool,
																				 DeviceName->Length +
																				 sizeof ( WCHAR ) ) ) == NULL )
				{
					DBG ( "ExAllocatePool DeviceName\n" );
				}
			else
				{
					RtlZeroMemory ( Context->DeviceName,
													DeviceName->Length + sizeof ( WCHAR ) );
					RtlCopyMemory ( Context->DeviceName, DeviceName->Buffer,
													DeviceName->Length );
				}
		}

	if ( Context->AdapterName != NULL )
		DBG ( "Adapter: %S\n", Context->AdapterName );
	if ( Context->DeviceName != NULL )
		DBG ( "Device Name: %S\n", Context->DeviceName );
	if ( ( Context->AdapterName == NULL ) && ( Context->DeviceName == NULL ) )
		DBG ( "Unnamed Adapter...\n" );

	Request.RequestType = NdisRequestQueryInformation;
	Request.DATA.QUERY_INFORMATION.Oid = OID_802_3_CURRENT_ADDRESS;
	Request.DATA.QUERY_INFORMATION.InformationBuffer = Mac;
	Request.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof ( Mac );

	KeResetEvent ( &Context->Event );
	NdisRequest ( &Status, Context->BindingHandle, &Request );
	if ( Status == NDIS_STATUS_PENDING )
		{
			KeWaitForSingleObject ( &Context->Event, Executive, KernelMode, FALSE,
															NULL );
			Status = Context->Status;
		}
	if ( !NT_SUCCESS ( Status ) )
		{
			Error ( "Protocol_BindAdapter NdisRequest (Mac)", Status );
		}
	else
		{
			RtlCopyMemory ( Context->Mac, Mac, 6 );
			DBG ( "Mac: %02x:%02x:%02x:%02x:%02x:%02x\n", Mac[0], Mac[1], Mac[2],
						Mac[3], Mac[4], Mac[5] );
		}

	Request.RequestType = NdisRequestQueryInformation;
	Request.DATA.QUERY_INFORMATION.Oid = OID_GEN_MAXIMUM_FRAME_SIZE;
	Request.DATA.QUERY_INFORMATION.InformationBuffer = &MTU;
	Request.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof ( MTU );

	KeResetEvent ( &Context->Event );
	NdisRequest ( &Status, Context->BindingHandle, &Request );
	if ( Status == NDIS_STATUS_PENDING )
		{
			KeWaitForSingleObject ( &Context->Event, Executive, KernelMode, FALSE,
															NULL );
			Status = Context->Status;
		}
	if ( !NT_SUCCESS ( Status ) )
		{
			Error ( "Protocol_BindAdapter NdisRequest (MTU)", Status );
		}
	else
		{
			Context->MTU = MTU;
			DBG ( "MTU: %d\n", MTU );
		}

	Request.RequestType = NdisRequestSetInformation;
	Request.DATA.SET_INFORMATION.Oid = OID_GEN_CURRENT_PACKET_FILTER;
	Request.DATA.SET_INFORMATION.InformationBuffer = &InformationBuffer;
	Request.DATA.SET_INFORMATION.InformationBufferLength =
		sizeof ( InformationBuffer );

	KeResetEvent ( &Context->Event );
	NdisRequest ( &Status, Context->BindingHandle, &Request );
	if ( Status == NDIS_STATUS_PENDING )
		{
			KeWaitForSingleObject ( &Context->Event, Executive, KernelMode, FALSE,
															NULL );
			Status = Context->Status;
		}
	if ( !NT_SUCCESS ( Status ) )
		Error ( "ProtocolBindAdapter NdisRequest (filter)", Status );

	KeAcquireSpinLock ( &Protocol_Globals_SpinLock, &Irql );
	if ( Protocol_Globals_BindingContextList == NULL )
		{
			Protocol_Globals_BindingContextList = Context;
		}
	else
		{
			for ( Walker = Protocol_Globals_BindingContextList; Walker->Next != NULL;
						Walker = Walker->Next );
			Walker->Next = Context;
		}
	KeReleaseSpinLock ( &Protocol_Globals_SpinLock, Irql );

	AoE_ResetProbe (  );
	*StatusOut = NDIS_STATUS_SUCCESS;
}

static VOID STDCALL
Protocol_UnbindAdapter (
	OUT PNDIS_STATUS StatusOut,
	IN NDIS_HANDLE ProtocolBindingContext,
	IN NDIS_HANDLE UnbindContext
 )
{
	PPROTOCOL_BINDINGCONTEXT Context =
		( PPROTOCOL_BINDINGCONTEXT ) ProtocolBindingContext;
	PPROTOCOL_BINDINGCONTEXT Walker,
	 PreviousContext;
	NDIS_STATUS Status;
	KIRQL Irql;
#if defined(DEBUGMOSTPROTOCOLCALLS) || defined(DEBUGALLPROTOCOLCALLS)
	DBG ( "Entry\n" );
#endif

	PreviousContext = NULL;
	KeAcquireSpinLock ( &Protocol_Globals_SpinLock, &Irql );
	for ( Walker = Protocol_Globals_BindingContextList;
				Walker != Context && Walker != NULL; Walker = Walker->Next )
		PreviousContext = Walker;
	if ( Walker == NULL )
		{
			DBG ( "Context not found in Protocol_Globals_BindingContextList!!\n" );
			KeReleaseSpinLock ( &Protocol_Globals_SpinLock, Irql );
			return;
		}
	if ( PreviousContext == NULL )
		{
			Protocol_Globals_BindingContextList = Walker->Next;
		}
	else
		{
			PreviousContext->Next = Walker->Next;
		}
	KeReleaseSpinLock ( &Protocol_Globals_SpinLock, Irql );

	NdisCloseAdapter ( &Status, Context->BindingHandle );
	if ( !NT_SUCCESS ( Status ) )
		Error ( "ProtocolUnbindAdapter NdisCloseAdapter", Status );
	NdisFreePacketPool ( Context->PacketPoolHandle );
	NdisFreeBufferPool ( Context->BufferPoolHandle );
	ExFreePool ( Context );
	if ( Protocol_Globals_BindingContextList == NULL )
		KeSetEvent ( &Protocol_Globals_StopEvent, 0, FALSE );
	*StatusOut = NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS STDCALL
Protocol_PnPEvent (
	IN NDIS_HANDLE ProtocolBindingContext,
	IN PNET_PNP_EVENT NetPnPEvent
 )
{
#if defined(DEBUGMOSTPROTOCOLCALLS) || defined(DEBUGALLPROTOCOLCALLS)
	DBG ( "%s\n", Protocol_NetEventString ( NetPnPEvent->NetEvent ) );
#endif
	if ( ProtocolBindingContext == NULL
			 && NetPnPEvent->NetEvent == NetEventReconfigure )
		{
#ifdef _MSC_VER
			NdisReEnumerateProtocolBindings ( Protocol_Globals_Handle );
#else
			DBG ( "No vector to NdisReEnumerateProtocolBindings\n" );
#endif
		}
	if ( NetPnPEvent->NetEvent == NetEventQueryRemoveDevice )
		{
			return NDIS_STATUS_FAILURE;
		}
	else
		{
			return NDIS_STATUS_SUCCESS;
		}
}

static PCHAR STDCALL
Protocol_NetEventString (
	IN NET_PNP_EVENT_CODE NetEvent
 )
{
	switch ( NetEvent )
		{
			case NetEventSetPower:
				return "NetEventSetPower";
			case NetEventQueryPower:
				return "NetEventQueryPower";
			case NetEventQueryRemoveDevice:
				return "NetEventQueryRemoveDevice";
			case NetEventCancelRemoveDevice:
				return "NetEventCancelRemoveDevice";
			case NetEventReconfigure:
				return "NetEventReconfigure";
			case NetEventBindList:
				return "NetEventBindList";
			case NetEventBindsComplete:
				return "NetEventBindsComplete";
			case NetEventPnPCapabilities:
				return "NetEventPnPCapabilities";
			default:
				return "NetEventUnknown";
		}
}
