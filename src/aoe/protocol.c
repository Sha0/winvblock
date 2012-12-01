/**
 * Copyright (C) 2009-2012, Shao Miller <sha0.miller@gmail.com>.
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
 * Protocol specifics.
 */

#include <ntddk.h>
#include <ndis.h>
#include <ntddndis.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "wv_string.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "mount.h"
#include "aoe.h"
#include "protocol.h"
#include "debug.h"

#define AOEPROTOCOLID 0x88a2

/** From AoE module */
extern NTSTATUS STDCALL aoe__reply (
  IN PUCHAR SourceMac,
  IN PUCHAR DestinationMac,
  IN PUCHAR Data,
  IN UINT32 DataSize
 );

/** In this file */
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
  IN UINT32 BytesTransferred
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
  IN UINT32 HeaderBufferSize,
  IN PVOID LookAheadBuffer,
  IN UINT32 LookaheadBufferSize,
  IN UINT32 PacketSize
 );
static VOID STDCALL Protocol_ReceiveComplete (
  IN NDIS_HANDLE ProtocolBindingContext
 );
static VOID STDCALL Protocol_Status (
  IN NDIS_HANDLE ProtocolBindingContext,
  IN NDIS_STATUS GeneralStatus,
  IN PVOID StatusBuffer,
  IN UINT32 StatusBufferSize
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
  UCHAR DestinationMac[6];
  UCHAR SourceMac[6];
  UINT16 Protocol;
  UCHAR Data[];
} __attribute__ ( ( __packed__ ) ) PROTOCOL_HEADER, *PPROTOCOL_HEADER;
#ifdef _MSC_VER
#  pragma pack()
#endif

typedef struct _PROTOCOL_BINDINGCONTEXT
{
  BOOLEAN Active;
  UCHAR Mac[6];
  UINT32 MTU;
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

NTSTATUS Protocol_Start(void) {
  NDIS_STATUS Status;
  NDIS_STRING ProtocolName;
  NDIS_PROTOCOL_CHARACTERISTICS ProtocolCharacteristics;

  DBG ( "Entry\n" );
  if ( Protocol_Globals_Started )
    return STATUS_SUCCESS;
  KeInitializeEvent ( &Protocol_Globals_StopEvent, SynchronizationEvent,
		      FALSE );
  KeInitializeSpinLock ( &Protocol_Globals_SpinLock );

  RtlInitUnicodeString ( &ProtocolName, WVL_M_WLIT );
  NdisZeroMemory ( &ProtocolCharacteristics,
		   sizeof ( NDIS_PROTOCOL_CHARACTERISTICS ) );
  ProtocolCharacteristics.MajorNdisVersion = 4;
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
  if ( !NT_SUCCESS ( Status ) )
    DBG ( "Protocol startup failure!\n" );
  else
    Protocol_Globals_Started = TRUE;
  DBG ( "Exit\n" );
  return Status;
}

VOID Protocol_Stop(void) {
  NDIS_STATUS Status;

  DBG ( "Entry\n" );
  if ( !Protocol_Globals_Started )
    return;
  KeResetEvent ( &Protocol_Globals_StopEvent );
  NdisDeregisterProtocol ( &Status, Protocol_Globals_Handle );
  if ( !NT_SUCCESS ( Status ) )
    WvlError("NdisDeregisterProtocol", Status);
  if ( Protocol_Globals_BindingContextList != NULL )
    KeWaitForSingleObject ( &Protocol_Globals_StopEvent, Executive, KernelMode,
			    FALSE, NULL );
  Protocol_Globals_Started = FALSE;
  DBG ( "Exit\n" );
}

BOOLEAN STDCALL
Protocol_SearchNIC (
  IN PUCHAR Mac
 )
{
  PPROTOCOL_BINDINGCONTEXT Context = Protocol_Globals_BindingContextList;

  while ( Context != NULL )
    {
      if (wv_memcmpeq(Mac, Context->Mac, 6)) break;
      Context = Context->Next;
    }
  if ( Context != NULL )
    return TRUE;
  return FALSE;
}

UINT32 STDCALL
Protocol_GetMTU (
  IN PUCHAR Mac
 )
{
  PPROTOCOL_BINDINGCONTEXT Context = Protocol_Globals_BindingContextList;

  while ( Context != NULL )
    {
      if (wv_memcmpeq(Mac, Context->Mac, 6)) break;
      Context = Context->Next;
    }
  if ( Context == NULL )
    return 0;
  return Context->MTU;
}

BOOLEAN STDCALL
Protocol_Send (
  IN PUCHAR SourceMac,
  IN PUCHAR DestinationMac,
  IN PUCHAR Data,
  IN UINT32 DataSize,
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

  if (wv_memcmpeq(SourceMac, "\xff\xff\xff\xff\xff\xff", 6)) {
      while ( Context != NULL )
	{
	  Protocol_Send ( Context->Mac, DestinationMac, Data, DataSize, NULL );
	  Context = Context->Next;
	}
      return TRUE;
    }

  while ( Context != NULL )
    {
      if (wv_memcmpeq(SourceMac, Context->Mac, 6)) break;
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

  if ((DataBuffer = wv_malloc(sizeof *DataBuffer + DataSize)) == NULL) {
      DBG("wv_malloc DataBuffer\n");
      return FALSE;
    }

  RtlCopyMemory ( DataBuffer->SourceMac, SourceMac, 6 );
  RtlCopyMemory ( DataBuffer->DestinationMac, DestinationMac, 6 );
  DataBuffer->Protocol = htons ( AOEPROTOCOLID );
  RtlCopyMemory ( DataBuffer->Data, Data, DataSize );

  NdisAllocatePacket ( &Status, &Packet, Context->PacketPoolHandle );
  if ( !NT_SUCCESS ( Status ) )
    {
      WvlError("Protocol_Send NdisAllocatePacket", Status);
      wv_free(DataBuffer);
      return FALSE;
    }

  NdisAllocateBuffer ( &Status, &Buffer, Context->BufferPoolHandle, DataBuffer,
		       ( sizeof ( PROTOCOL_HEADER ) + DataSize ) );
  if ( !NT_SUCCESS ( Status ) )
    {
      WvlError("Protocol_Send NdisAllocateBuffer", Status);
      NdisFreePacket ( Packet );
      wv_free(DataBuffer);
      return FALSE;
    }

  NdisChainBufferAtFront ( Packet, Buffer );
  *(PVOID *) Packet->ProtocolReserved = PacketContext;
  NdisSend ( &Status, Context->BindingHandle, Packet );
  if ( Status != NDIS_STATUS_PENDING )
    Protocol_SendComplete ( Context, Packet, Status );
#if defined(DEBUGALLPROTOCOLCALLS)
  DBG ( "Exit\n" );
#endif
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
    WvlError("Protocol_CloseAdapterComplete", Status);
}

static VOID STDCALL
Protocol_SendComplete (
  IN NDIS_HANDLE ProtocolBindingContext,
  IN PNDIS_PACKET Packet,
  IN NDIS_STATUS Status
 )
{
  PNDIS_BUFFER Buffer;
  PUCHAR DataBuffer;
#ifndef DEBUGALLPROTOCOLCALLS
  if ( !NT_SUCCESS ( Status ) && Status != NDIS_STATUS_NO_CABLE )
#endif
    WvlError("Protocol_SendComplete", Status);

  NdisUnchainBufferAtFront ( Packet, &Buffer );
  if ( Buffer != NULL )
    {
      DataBuffer = MmGetSystemAddressForMdlSafe ( Buffer, HighPagePriority );
      wv_free(DataBuffer);
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
  IN UINT32 BytesTransferred
 )
{
  PNDIS_BUFFER Buffer;
  PPROTOCOL_HEADER Header = NULL;
  PUCHAR Data = NULL;
  UINT32 HeaderSize,
   DataSize;
#ifndef DEBUGALLPROTOCOLCALLS
  if ( !NT_SUCCESS ( Status ) )
#endif
    WvlError("Protocol_TransferDataComplete", Status);

  NdisUnchainBufferAtFront ( Packet, &Buffer );
  if ( Buffer != NULL )
    {
      NdisQueryBufferSafe ( Buffer, &Data, &DataSize, HighPagePriority );
      NdisFreeBuffer ( Buffer );
    }
  else
    {
      DBG ( "Data (front) Buffer == NULL\n" );
    }
  NdisUnchainBufferAtBack ( Packet, &Buffer );
  if ( Buffer != NULL )
    {
      NdisQueryBufferSafe ( Buffer, &Header, &HeaderSize, HighPagePriority );
      NdisFreeBuffer ( Buffer );
    }
  else
    {
      DBG ( "Header (back) Buffer == NULL\n" );
    }
  if ( Header != NULL && Data != NULL )
    aoe__reply ( Header->SourceMac, Header->DestinationMac, Data, DataSize );
  wv_free(Header);
  wv_free(Data);
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
    WvlError("Protocol_ResetComplete", Status);
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
    WvlError("Protocol_RequestComplete", Status);

  Context->Status = Status;
  KeSetEvent ( &Context->Event, 0, FALSE );
}

static NDIS_STATUS STDCALL
Protocol_Receive (
  IN NDIS_HANDLE ProtocolBindingContext,
  IN NDIS_HANDLE MacReceiveContext,
  IN PVOID HeaderBuffer,
  IN UINT32 HeaderBufferSize,
  IN PVOID LookAheadBuffer,
  IN UINT32 LookaheadBufferSize,
  IN UINT32 PacketSize
 )
{
  PPROTOCOL_BINDINGCONTEXT Context =
    ( PPROTOCOL_BINDINGCONTEXT ) ProtocolBindingContext;
  NDIS_STATUS Status;
  PNDIS_PACKET Packet;
  PNDIS_BUFFER Buffer;
  PPROTOCOL_HEADER Header;
  PUCHAR HeaderCopy,
   Data;
  UINT32 BytesTransferred;
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
      aoe__reply ( Header->SourceMac, Header->DestinationMac, LookAheadBuffer,
		   PacketSize );
      return NDIS_STATUS_SUCCESS;
    }

  if ((HeaderCopy = wv_malloc(HeaderBufferSize)) == NULL) {
      DBG("wv_malloc HeaderCopy\n");
      return NDIS_STATUS_NOT_ACCEPTED;
    }
  RtlCopyMemory ( HeaderCopy, HeaderBuffer, HeaderBufferSize );
  if ((Data = wv_malloc(PacketSize)) == NULL) {
      DBG("wv_malloc HeaderData\n");
      wv_free(HeaderCopy);
      return NDIS_STATUS_NOT_ACCEPTED;
    }
  NdisAllocatePacket ( &Status, &Packet, Context->PacketPoolHandle );
  if ( !NT_SUCCESS ( Status ) )
    {
      WvlError("Protocol_Receive NdisAllocatePacket", Status);
      wv_free(Data);
      wv_free(HeaderCopy);
      return NDIS_STATUS_NOT_ACCEPTED;
    }

  NdisAllocateBuffer ( &Status, &Buffer, Context->BufferPoolHandle, Data,
		       PacketSize );
  if ( !NT_SUCCESS ( Status ) )
    {
      WvlError("Protocol_Receive NdisAllocateBuffer (Data)", Status);
      NdisFreePacket ( Packet );
      wv_free(Data);
      wv_free(HeaderCopy);
      return NDIS_STATUS_NOT_ACCEPTED;
    }
  NdisChainBufferAtFront ( Packet, Buffer );

  NdisAllocateBuffer ( &Status, &Buffer, Context->BufferPoolHandle, HeaderCopy,
		       PacketSize );
  if ( !NT_SUCCESS ( Status ) )
    {
      WvlError("Protocol_Receive NdisAllocateBuffer (HeaderCopy)", Status);
      NdisUnchainBufferAtFront ( Packet, &Buffer );
      NdisFreeBuffer ( Buffer );
      NdisFreePacket ( Packet );
      wv_free(Data);
      wv_free(HeaderCopy);
      return NDIS_STATUS_NOT_ACCEPTED;
    }
  NdisChainBufferAtBack ( Packet, Buffer );

  NdisTransferData ( &Status, Context->BindingHandle, MacReceiveContext, 0,
		     PacketSize, Packet, &BytesTransferred );
  if ( Status != NDIS_STATUS_PENDING )
    Protocol_TransferDataComplete ( ProtocolBindingContext, Packet, Status,
				    BytesTransferred );
#if defined(DEBUGALLPROTOCOLCALLS)
  DBG ( "Exit\n" );
#endif
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
#if defined(DEBUGALLPROTOCOLCALLS)
  DBG ( "Exit\n" );
#endif
}

static VOID STDCALL
Protocol_Status (
  IN NDIS_HANDLE ProtocolBindingContext,
  IN NDIS_STATUS GeneralStatus,
  IN PVOID StatusBuffer,
  IN UINT32 StatusBufferSize
 )
{
#if !defined(DEBUGMOSTPROTOCOLCALLS) && !defined(DEBUGALLPROTOCOLCALLS)
  if ( !NT_SUCCESS ( GeneralStatus ) )
#endif
    WvlError("Protocol_Status", GeneralStatus);
}

static VOID STDCALL
Protocol_StatusComplete (
  IN NDIS_HANDLE ProtocolBindingContext
 )
{
#if defined(DEBUGMOSTPROTOCOLCALLS) || defined(DEBUGALLPROTOCOLCALLS)
  DBG ( "Entry\n" );
#endif
#if defined(DEBUGALLPROTOCOLCALLS)
  DBG ( "Exit\n" );
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
  UINT32 SelectedMediumIndex,
   MTU;
  NDIS_MEDIUM MediumArray[] = { NdisMedium802_3 };
  NDIS_STRING AdapterInstanceName;
  NDIS_REQUEST Request;
  UINT32 InformationBuffer = NDIS_PACKET_TYPE_DIRECTED;
  UCHAR Mac[6];
  KIRQL Irql;
#if defined(DEBUGMOSTPROTOCOLCALLS) || defined(DEBUGALLPROTOCOLCALLS)
  DBG ( "Entry\n" );
#endif

  if ((Context = wv_malloc(sizeof *Context)) == NULL) {
      DBG("wv_malloc Context\n");
      *StatusOut = NDIS_STATUS_RESOURCES;
      return;
    }
  Context->Next = NULL;
  KeInitializeEvent ( &Context->Event, SynchronizationEvent, FALSE );

  NdisAllocatePacketPool(
      &Status,
      &Context->PacketPoolHandle,
      POOLSIZE,
			4 * sizeof (PVOID)
    );
  if ( !NT_SUCCESS ( Status ) )
    {
      WvlError("Protocol_BindAdapter NdisAllocatePacketPool", Status);
      wv_free(Context);
      *StatusOut = NDIS_STATUS_RESOURCES;
      return;
    }

  NdisAllocateBufferPool ( &Status, &Context->BufferPoolHandle, POOLSIZE );
  if ( !NT_SUCCESS ( Status ) )
    {
      WvlError("Protocol_BindAdapter NdisAllocateBufferPool", Status);
      NdisFreePacketPool ( Context->PacketPoolHandle );
      wv_free(Context);
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
      wv_free(Context->AdapterName);
      wv_free(Context->DeviceName);
      wv_free(Context);
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
      Context->AdapterName = wv_mallocz(
          AdapterInstanceName.Length +
          sizeof (WCHAR)
        );
      if (Context->AdapterName == NULL) {
        DBG("wv_mallocz AdapterName\n");
	}
      else
	{
	  RtlCopyMemory ( Context->AdapterName, AdapterInstanceName.Buffer,
			  AdapterInstanceName.Length );
	}
      NdisFreeMemory ( AdapterInstanceName.Buffer, 0, 0 );
    }
  else
    {
      WvlError("Protocol_BindAdapter NdisQueryAdapterInstanceName", Status);
    }

  Context->DeviceName = NULL;
  if ( DeviceName->Length > 0 )
    {
      Context->DeviceName = wv_mallocz(DeviceName->Length + sizeof (WCHAR));
      if (Context->DeviceName == NULL) {
          DBG("wv_malloc DeviceName\n");
	}
      else
	{
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
      WvlError("Protocol_BindAdapter NdisRequest (Mac)", Status);
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
      WvlError("Protocol_BindAdapter NdisRequest (MTU)", Status);
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
    WvlError("ProtocolBindAdapter NdisRequest (filter)", Status);

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

  aoe__reset_probe (  );
  *StatusOut = NDIS_STATUS_SUCCESS;
#if defined(DEBUGMOSTPROTOCOLCALLS) || defined(DEBUGALLPROTOCOLCALLS)
  DBG ( "Exit\n" );
#endif
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
    WvlError("ProtocolUnbindAdapter NdisCloseAdapter", Status);
  NdisFreePacketPool ( Context->PacketPoolHandle );
  NdisFreeBufferPool ( Context->BufferPoolHandle );
  wv_free(Context);
  if ( Protocol_Globals_BindingContextList == NULL )
    KeSetEvent ( &Protocol_Globals_StopEvent, 0, FALSE );
  *StatusOut = NDIS_STATUS_SUCCESS;
#if defined(DEBUGMOSTPROTOCOLCALLS) || defined(DEBUGALLPROTOCOLCALLS)
  DBG ( "Exit\n" );
#endif
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
