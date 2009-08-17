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

asm ( ".code16gcc" );
#ifndef _PXE_H
#  define _PXE_H

int pxeinit (
	void
 );
unsigned short api (
	unsigned short command,
	void *commandstruct
 );
void apierror (
	char *message,
	unsigned short status
 );

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned long UINT32;
typedef signed char INT8;
typedef signed short INT16;
typedef signed long INT32;

#  define MAC_ADDR_LEN 16
typedef UINT8 MAC_ADDR[MAC_ADDR_LEN];
typedef UINT16 OFF16;
typedef UINT16 PXENV_STATUS;
typedef UINT16 SEGSEL;
typedef UINT16 UDP_PORT;
typedef UINT32 ADDR32;

typedef struct s_SEGDESC
{
	UINT16 segment_address;
	UINT32 Physical_address;
	UINT16 Seg_Size;
} __attribute__ ( ( __packed__ ) ) SEGDESC;

typedef struct s_SEGOFF16
{
	OFF16 offset;
	SEGSEL segment;
} __attribute__ ( ( __packed__ ) ) SEGOFF16;

#  define IP_ADDR_LEN 4
typedef union u_IP4
{
	UINT32 num;
	UINT8 array[IP_ADDR_LEN];
} __attribute__ ( ( __packed__ ) ) IP4;

typedef struct s_PXENV
{
	UINT8 Signature[6];
	UINT16 Version;
	UINT8 Length;
	UINT8 Checksum;
	SEGOFF16 RMEntry;
	UINT32 PMOffset;
	SEGSEL PMSelector;
	SEGSEL StackSeg;
	UINT16 StackSize;
	SEGSEL BC_CodeSeg;
	UINT16 BC_CodeSize;
	SEGSEL BC_DataSeg;
	UINT16 BC_DataSize;
	SEGSEL UNDIDataSeg;
	UINT16 UNDIDataSize;
	SEGSEL UNDICodeSeg;
	UINT16 UNDICodeSize;
	SEGOFF16 PXEPtr;
} __attribute__ ( ( __packed__ ) ) t_PXENV;

typedef struct s_PXE
{
	UINT8 Signature[4];
	UINT8 StructLength;
	UINT8 StructCksum;
	UINT8 StructRev;
	UINT8 reserved1;
	SEGOFF16 UNDIROMID;
	SEGOFF16 BaseROMID;
	SEGOFF16 EntryPointSP;
	SEGOFF16 EntryPointESP;
	SEGOFF16 StatusCallout;
	UINT8 reserved2;
	UINT8 SegDescCnt;
	SEGSEL FirstSelector;
	SEGDESC Stack;
	SEGDESC UNDIData;
	SEGDESC UNDICode;
	SEGDESC UNDICodeWrite;
	SEGDESC BC_Data;
	SEGDESC BC_Code;
	SEGDESC BC_CodeWrite;
} __attribute__ ( ( __packed__ ) ) t_PXE;

typedef struct s_UNDI_ROM_ID
{
	UINT8 Signature[4];
	UINT8 StructLength;
	UINT8 StructCksum;
	UINT8 StructRev;
	UINT8 UNDIRev[3];
	UINT16 UNDILoader;
	UINT16 StackSize;
	UINT16 DataSize;
	UINT16 CodeSize;
	UINT8 BusType[][4];
} __attribute__ ( ( __packed__ ) ) t_UNDI_ROM_ID;

typedef struct s_UNDI_LOADER
{
	PXENV_STATUS Status;
	UINT16 AX;
	UINT16 BX;
	UINT16 DX;
	UINT16 DI;
	UINT16 ES;
	UINT16 UNDI_DS;
	UINT16 UNDI_CS;
	SEGOFF16 PXEptr;
	SEGOFF16 PXENVptr;
} __attribute__ ( ( __packed__ ) ) t_UNDI_LOADER;

#  define PXENV_START_UNDI 0x0000
typedef struct s_PXENV_START_UNDI
{
	PXENV_STATUS Status;
	UINT16 AX;
	UINT16 BX;
	UINT16 DX;
	UINT16 DI;
	UINT16 ES;
} __attribute__ ( ( __packed__ ) ) t_PXENV_START_UNDI;

#  define PXENV_UNDI_STARTUP 0x0001
typedef struct s_PXENV_UNDI_STARTUP
{
	PXENV_STATUS Status;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_STARTUP;

#  define PXENV_UNDI_CLEANUP 0x0002
typedef struct s_PXENV_UNDI_CLEANUP
{
	PXENV_STATUS Status;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_CLEANUP;

#  define PXENV_UNDI_INITIALIZE 0x0003
typedef struct s_PXENV_UNDI_INITIALIZE
{
	PXENV_STATUS Status;
	ADDR32 ProtocolIni;
	UINT8 reserved[8];
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_INITIALIZE;

#  define MAXNUM_MCADDR 8
typedef struct s_PXENV_UNDI_MCAST_ADDRESS
{
	UINT16 MCastAddrCount;
	MAC_ADDR McastAddr[MAXNUM_MCADDR];
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_MCAST_ADDRESS;

#  define PXENV_UNDI_RESET_ADAPTER 0x0004
typedef struct s_PXENV_UNDI_RESET
{
	PXENV_STATUS Status;
	t_PXENV_UNDI_MCAST_ADDRESS R_Mcast_Buf;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_RESET;

#  define PXENV_UNDI_SHUTDOWN 0x0005
typedef struct s_PXENV_UNDI_SHUTDOWN
{
	PXENV_STATUS Status;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_SHUTDOWN;

#  define PXENV_UNDI_OPEN 0x0006
typedef struct s_PXENV_UNDI_OPEN
{
	PXENV_STATUS Status;
	UINT16 OpenFlag;
	UINT16 PktFilter;
#  define FLTR_DIRECTED 0x0001
#  define FLTR_BRDCST   0x0002
#  define FLTR_PRMSCS   0x0004
#  define FLTR_SRC_RTG  0x0008
	t_PXENV_UNDI_MCAST_ADDRESS R_Mcast_Buf;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_OPEN;

#  define PXENV_UNDI_CLOSE 0x0007
typedef struct s_PXENV_UNDI_CLOSE
{
	PXENV_STATUS Status;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_CLOSE;

#  define PXENV_UNDI_TRANSMIT 0x0008
typedef struct s_PXENV_UNDI_TRANSMIT
{
	PXENV_STATUS Status;
	UINT8 Protocol;
#  define P_UNKNOWN 0
#  define P_IP      1
#  define P_ARP     2
#  define P_RARP    3
	UINT8 XmitFlag;
#  define XMT_DESTADDR  0x0000
#  define XMT_BROADCAST 0x0001
	SEGOFF16 DestAddr;
	SEGOFF16 TBD;
	UINT32 Reserved[2];
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_TRANSMIT;

#  define MAX_DATA_BLKS 8
typedef struct s_PXENV_UNDI_TBD
{
	UINT16 ImmedLength;
	SEGOFF16 Xmit;
	UINT16 DataBlkCount;
	struct DataBlk
	{
		UINT8 TDPtrType;
		UINT8 TDRsvdByte;
		UINT16 TDDataLen;
		SEGOFF16 TDDataPtr;
	} __attribute__ ( ( __packed__ ) ) DataBlock[MAX_DATA_BLKS];
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_TBD;

#  define PXENV_UNDI_SET_MCAST_ADDRESS 0x0009
typedef struct s_PXENV_UNDI_SET_MCAST_ADDRESS
{
	PXENV_STATUS Status;
	t_PXENV_UNDI_MCAST_ADDRESS R_Mcast_Buf;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_SET_MCAST_ADDR;

#  define PXENV_UNDI_SET_STATION_ADDRESS 0x000A
typedef struct s_PXENV_UNDI_SET_STATION_ADDRESS
{
	PXENV_STATUS Status;
	MAC_ADDR StationAddress;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_SET_STATION_ADDR;

#  define PXENV_UNDI_SET_PACKET_FILTER 0x000B
typedef struct s_PXENV_UNDI_SET_PACKET_FILTER
{
	PXENV_STATUS Status;
	UINT8 filter;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_SET_PACKET_FILTER;

#  define PXENV_UNDI_GET_INFORMATION 0x000C
typedef struct s_PXENV_UNDI_GET_INFORMATION
{
	PXENV_STATUS Status;
	UINT16 BaseIo;
	UINT16 IntNumber;
	UINT16 MaxTranUnit;
	UINT16 HwType;
#  define ETHER_TYPE     1
#  define EXP_ETHER_TYPE 2
#  define IEEE_TYPE      6
#  define ARCNET_TYPE    7
	UINT16 HwAddrLen;
	MAC_ADDR CurrentNodeAddress;
	MAC_ADDR PermNodeAddress;
	SEGSEL ROMAddress;
	UINT16 RxBufCt;
	UINT16 TxBufCt;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_GET_INFORMATION;

#  define PXENV_UNDI_GET_STATISTICS 0x000D
typedef struct s_PXENV_UNDI_GET_STATISTICS
{
	PXENV_STATUS Status;
	UINT32 XmtGoodFrames;
	UINT32 RcvGoodFrames;
	UINT32 RcvCRCErrors;
	UINT32 RcvResourceErrors;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_GET_STATISTICS;

#  define PXENV_UNDI_CLEAR_STATISTICS 0x000E
typedef struct s_PXENV_UNDI_CLEAR_STATISTICS
{
	PXENV_STATUS Status;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_CLEAR_STATISTICS;

#  define PXENV_UNDI_INITIATE_DIAGS 0x000F
typedef struct s_PXENV_UNDI_INITIATE_DIAGS
{
	PXENV_STATUS Status;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_INITIATE_DIAGS;

#  define PXENV_UNDI_FORCE_INTERRUPT 0x0010
typedef struct s_PXENV_UNDI_FORCE_INTERRUPT
{
	PXENV_STATUS Status;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_FORCE_INTERRUPT;

#  define PXENV_UNDI_GET_MCAST_ADDRESS 0x0011
typedef struct s_PXENV_UNDI_GET_MCAST_ADDRESS
{
	PXENV_STATUS Status;
	IP4 InetAddr;
	MAC_ADDR MediaAddr;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_GET_MCAST_ADDR;

#  define PXENV_UNDI_GET_NIC_TYPE 0x0012
typedef struct s_PXENV_UNDI_GET_NIC_TYPE
{
	PXENV_STATUS Status;
	UINT8 NicType;
#  define PCI_NIC     2
#  define PnP_NIC     3
#  define CardBus_NIC 4
	union
	{
		struct
		{
			UINT16 Vendor_ID;
			UINT16 Dev_ID;
			UINT8 Base_Class;
			UINT8 Sub_Class;
			UINT8 Prog_Intf;
			UINT8 Rev;
			UINT16 BusDevFunc;
			UINT16 SubVendor_ID;
			UINT16 SubDevice_ID;
		} __attribute__ ( ( __packed__ ) ) pci, cardbus;
		struct
		{
			UINT32 EISA_Dev_ID;
			UINT8 Base_Class;
			UINT8 Sub_Class;
			UINT8 Prog_Intf;
			UINT16 CardSelNum;
		} __attribute__ ( ( __packed__ ) ) pnp;
	} info;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_GET_NIC_TYPE;

#  define PXENV_UNDI_GET_IFACE_INFO 0x0013
typedef struct s_PXENV_UNDI_GET_IFACE_INFO
{
	PXENV_STATUS Status;
	UINT8 IfaceType[16];
	UINT32 LinkSpeed;
	UINT32 ServiceFlags;
	UINT32 Reserved[4];
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_GET_NDIS_INFO;

#  define PXENV_UNDI_ISR 0x0014
typedef struct s_PXENV_UNDI_ISR
{
	PXENV_STATUS Status;
	UINT16 FuncFlag;
#  define PXENV_UNDI_ISR_IN_START      1
#  define PXENV_UNDI_ISR_IN_PROCESS    2
#  define PXENV_UNDI_ISR_IN_GET_NEXT   3
#  define PXENV_UNDI_ISR_OUT_OURS      0
#  define PXENV_UNDI_ISR_OUT_NOT_OURS  1
#  define PXENV_UNDI_ISR_OUT_DONE      0
#  define PXENV_UNDI_ISR_OUT_TRANSMIT  2
#  define PXENV_UNDI_ISR_OUT_RECEIVE   3
#  define PXENV_UNDI_ISR_OUT_BUSY      4
	UINT16 BufferLength;
	UINT16 FrameLength;
	UINT16 FrameHeaderLength;
	SEGOFF16 Frame;
	UINT8 ProtType;
	UINT8 PktType;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_ISR;

#  define PXENV_UNDI_GET_STATE 0x0015
typedef struct s_PXENV_UNDI_GET_STATE
{
	PXENV_STATUS Status;
	UINT8 UNDIstate;
#  define PXE_UNDI_GET_STATE_STARTED     1
#  define PXE_UNDI_GET_STATE_INITIALIZED 2
#  define PXE_UNDI_GET_STATE_OPENED      3
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNDI_GET_STATE;

#  define PXENV_STOP_UNDI 0x0015
typedef struct s_PXENV_STOP_UNDI
{
	PXENV_STATUS Status;
} __attribute__ ( ( __packed__ ) ) t_PXENV_STOP_UNDI;

#  define PXENV_TFTP_OPEN 0x0020
typedef struct s_PXENV_TFTP_OPEN
{
	PXENV_STATUS Status;
	IP4 ServerIPAddress;
	IP4 GatewayIPAddress;
	UINT8 FileName[128];
	UDP_PORT TFTPPort;
	UINT16 PacketSize;
} __attribute__ ( ( __packed__ ) ) t_PXENV_TFTP_OPEN;

#  define PXENV_TFTP_CLOSE 0x0021
typedef struct s_PXENV_TFTP_CLOSE
{
	PXENV_STATUS Status;
} __attribute__ ( ( __packed__ ) ) t_PXENV_TFTP_CLOSE;

#  define PXENV_TFTP_READ 0x0022
typedef struct s_PXENV_TFTP_READ
{
	PXENV_STATUS Status;
	UINT16 PacketNumber;
	UINT16 BufferSize;
	SEGOFF16 Buffer;
} __attribute__ ( ( __packed__ ) ) t_PXENV_TFTP_READ;

#  define PXENV_TFTP_READ_FILE 0x0023
typedef struct s_PXENV_TFTP_READ_FILE
{
	PXENV_STATUS Status;
	UINT8 FileName[128];
	UINT32 BufferSize;
	ADDR32 Buffer;
	IP4 ServerIPAddress;
	IP4 GatewayIPAddress;
	IP4 McastIPAddress;
	UDP_PORT TFTPClntPort;
	UDP_PORT TFTPSrvPort;
	UINT16 TFTPOpenTimeOut;
	UINT16 TFTPReopenDelay;
} __attribute__ ( ( __packed__ ) ) t_PXENV_TFTP_READ_FILE;

#  define PXENV_TFTP_GET_FSIZE 0x0025
typedef struct s_PXENV_TFTP_GET_FSIZE
{
	PXENV_STATUS Status;
	IP4 ServerIPAddress;
	IP4 GatewayIPAddress;
	UINT8 FileName[128];
	UINT32 FileSize;
} __attribute__ ( ( __packed__ ) ) t_PXENV_TFTP_GET_FSIZE;

#  define PXENV_UDP_OPEN 0x0030
typedef struct s_PXENV_UDP_OPEN
{
	PXENV_STATUS status;
	IP4 src_ip;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UDP_OPEN;

#  define PXENV_UDP_CLOSE 0x0031
typedef struct s_PXENV_UDP_CLOSE
{
	PXENV_STATUS status;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UDP_CLOSE;

#  define PXENV_UDP_READ 0x0032
typedef struct s_PXENV_UDP_READ
{
	PXENV_STATUS status;
	IP4 src_ip;
	IP4 dest_ip;
	UDP_PORT s_port;
	UDP_PORT d_port;
	UINT16 buffer_size;
	SEGOFF16 buffer;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UDP_READ;

#  define PXENV_UDP_WRITE 0x0033
typedef struct s_PXENV_UDP_WRITE
{
	PXENV_STATUS status;
	IP4 ip;
	IP4 gw;
	UDP_PORT src_port;
	UDP_PORT dst_port;
	UINT16 buffer_size;
	SEGOFF16 buffer;
} __attribute__ ( ( __packed__ ) ) t_PXENV_UDP_WRITE;

#  define PXENV_UNLOAD_STACK 0x0070
typedef struct s_PXENV_UNLOAD_STACK
{
	PXENV_STATUS Status;
	UINT8 reserved[10];
} __attribute__ ( ( __packed__ ) ) t_PXENV_UNLOAD_STACK;

#  define PXENV_GET_CACHED_INFO 0x0071
typedef struct s_PXENV_GET_CACHED_INFO
{
	PXENV_STATUS Status;
	UINT16 PacketType;
#  define PXENV_PACKET_TYPE_DHCP_DISCOVER 1
#  define PXENV_PACKET_TYPE_DHCP_ACK      2
#  define PXENV_PACKET_TYPE_CACHED_REPLY  3
	UINT16 BufferSize;
	SEGOFF16 Buffer;
	UINT16 BufferLimit;
} __attribute__ ( ( __packed__ ) ) t_PXENV_GET_CACHED_INFO;

typedef struct s_bootph
{
	UINT8 opcode;
#  define BOOTP_REQ 1
#  define BOOTP_REP 2
	UINT8 Hardware;
	UINT8 Hardlen;
	UINT8 Gatehops;
	UINT32 ident;
	UINT16 seconds;
	UINT16 Flags;
#  define BOOTP_BCAST 0x8000
	IP4 cip;
	IP4 yip;
	IP4 sip;
	IP4 gip;
	MAC_ADDR CAddr;
	UINT8 Sname[64];
	UINT8 bootfile[128];
#  define BOOTP_DHCPVEND 1024
	union
	{
		UINT8 d[BOOTP_DHCPVEND];
		struct
		{
			UINT8 magic[4];
#  define VM_RFC1048 0x63538263
			UINT32 flags;
			UINT8 pad[56];
		} __attribute__ ( ( __packed__ ) ) v;
	} vendor;
} __attribute__ ( ( __packed__ ) ) BOOTPLAYER;

#  define PXENV_RESTART_TFTP 0x0073
typedef struct s_PXENV_RESTART_TFTP
{
	PXENV_STATUS Status;
	IP4 ServerIPAddress;
	IP4 GatewayIPAddress;
	UINT8 FileName[128];
	UDP_PORT TFTPPort;
	UINT16 PacketSize;
} __attribute__ ( ( __packed__ ) ) t_PXENV_RESTART_TFTP;

#  define PXENV_START_BASE 0x0075
typedef struct s_PXENV_START_BASE
{
	PXENV_STATUS Status;
} __attribute__ ( ( __packed__ ) ) t_PXENV_START_BASE;

#  define PXENV_STOP_BASE 0x0076
typedef struct s_PXENV_STOP_BASE
{
	PXENV_STATUS Status;
} __attribute__ ( ( __packed__ ) ) t_PXENV_STOP_BASE;

#  define PXENV_EXIT_SUCCESS 0x0000
#  define PXENV_EXIT_FAILURE 0x0001

#  define PXENV_STATUS_SUCCESS                          0x00
#  define PXENV_STATUS_FAILURE                          0x01
#  define PXENV_STATUS_BAD_FUNC                         0x02
#  define PXENV_STATUS_UNSUPPORTED                      0x03
#  define PXENV_STATUS_KEEP_UNDI                        0x04
#  define PXENV_STATUS_KEEP_ALL                         0x05
#  define PXENV_STATUS_OUT_OF_RESOURCES                 0x06
#  define PXENV_STATUS_ARP_TIMEOUT                      0x11
#  define PXENV_STATUS_UDP_CLOSED                       0x18
#  define PXENV_STATUS_UDP_OPEN                         0x19
#  define PXENV_STATUS_TFTP_CLOSED                      0x1A
#  define PXENV_STATUS_TFTP_OPEN                        0x1B
#  define PXENV_STATUS_MCOPY_PROBLEM                    0x20
#  define PXENV_STATUS_BIS_INTEGRITY_FAILURE            0x21
#  define PXENV_STATUS_BIS_VALIDATE_FAILURE             0x22
#  define PXENV_STATUS_BIS_INIT_FAILURE                 0x23
#  define PXENV_STATUS_BIS_SHUTDOWN_FAILURE             0x24
#  define PXENV_STATUS_BIS_GBOA_FAILURE                 0x25
#  define PXENV_STATUS_BIS_FREE_FAILURE                 0x26
#  define PXENV_STATUS_BIS_GSI_FAILURE                  0x27
#  define PXENV_STATUS_BIS_BAD_CKSUM                    0x28
#  define PXENV_STATUS_TFTP_CANNOT_ARP_ADDRESS          0x30
#  define PXENV_STATUS_TFTP_OPEN_TIMEOUT                0x32
#  define PXENV_STATUS_TFTP_UNKNOWN_OPCODE              0x33
#  define PXENV_STATUS_TFTP_READ_TIMEOUT                0x35
#  define PXENV_STATUS_TFTP_ERROR_OPCODE                0x36
#  define PXENV_STATUS_TFTP_CANNOT_OPEN_CONNECTION      0x38
#  define PXENV_STATUS_TFTP_CANNOT_READ_FROM_CONNECTION 0x39
#  define PXENV_STATUS_TFTP_TOO_MANY_PACKAGES           0x3A
#  define PXENV_STATUS_TFTP_FILE_NOT_FOUND              0x3B
#  define PXENV_STATUS_TFTP_ACCESS_VIOLATION            0x3C
#  define PXENV_STATUS_TFTP_NO_MCAST_ADDRESS            0x3D
#  define PXENV_STATUS_TFTP_NO_FILESIZE                 0x3E
#  define PXENV_STATUS_TFTP_INVALID_PACKET_SIZE         0x3F
#  define PXENV_STATUS_DHCP_TIMEOUT                     0x51
#  define PXENV_STATUS_DHCP_NO_IP_ADDRESS               0x52
#  define PXENV_STATUS_DHCP_NO_BOOTFILE_NAME            0x53
#  define PXENV_STATUS_DHCP_BAD_IP_ADDRESS              0x54
#  define PXENV_STATUS_UNDI_INVALID_FUNCTION            0x60
#  define PXENV_STATUS_UNDI_MEDIATEST_FAILED            0x61
#  define PXENV_STATUS_UNDI_CANNOT_INIT_NIC_FOR_MCAST   0x62
#  define PXENV_STATUS_UNDI_CANNOT_INITIALIZE_NIC       0x63
#  define PXENV_STATUS_UNDI_CANNOT_INITIALIZE_PHY       0x64
#  define PXENV_STATUS_UNDI_CANNOT_READ_CONFIG_DATA     0x65
#  define PXENV_STATUS_UNDI_CANNOT_READ_INIT_DATA       0x66
#  define PXENV_STATUS_UNDI_BAD_MAC_ADDRESS             0x67
#  define PXENV_STATUS_UNDI_BAD_EEPROM_CHECKSUM         0x68
#  define PXENV_STATUS_UNDI_ERROR_SETTING_ISR           0x69
#  define PXENV_STATUS_UNDI_INVALID_STATE               0x6A
#  define PXENV_STATUS_UNDI_TRANSMIT_ERROR              0x6B
#  define PXENV_STATUS_UNDI_INVALID_PARAMETER           0x6C
#  define PXENV_STATUS_BSTRAP_PROMPT_MENU               0x74
#  define PXENV_STATUS_BSTRAP_MCAST_ADDR                0x76
#  define PXENV_STATUS_BSTRAP_MISSING_LIST              0x77
#  define PXENV_STATUS_BSTRAP_NO_RESPONSE               0x78
#  define PXENV_STATUS_BSTRAP_FILE_TOO_BIG              0x79
#  define PXENV_STATUS_BINL_CANCELED_BY_KEYSTROKE       0xA0
#  define PXENV_STATUS_BINL_NO_PXE_SERVER               0xA1
#  define PXENV_STATUS_NOT_AVAILABLE_IN_PMODE           0xA2
#  define PXENV_STATUS_NOT_AVAILABLE_IN_RMODE           0xA3
#  define PXENV_STATUS_BUSD_DEVICE_NOT_SUPPORTED        0xB0
#  define PXENV_STATUS_LOADER_NO_FREE_BASE_MEMORY       0xC0
#  define PXENV_STATUS_LOADER_NO_BC_ROMID               0xC1
#  define PXENV_STATUS_LOADER_BAD_BC_ROMID              0xC2
#  define PXENV_STATUS_LOADER_BAD_BC_RUNTIME_IMAGE      0xC3
#  define PXENV_STATUS_LOADER_NO_UNDI_ROMID             0xC4
#  define PXENV_STATUS_LOADER_BAD_UNDI_ROMID            0xC5
#  define PXENV_STATUS_LOADER_BAD_UNDI_DRIVER_IMAGE     0xC6
#  define PXENV_STATUS_LOADER_NO_PXE_STRUCT             0xC8
#  define PXENV_STATUS_LOADER_NO_PXENV_STRUCT           0xC9
#  define PXENV_STATUS_LOADER_UNDI_START                0xCA
#  define PXENV_STATUS_LOADER_BC_START                  0xCB
#endif
