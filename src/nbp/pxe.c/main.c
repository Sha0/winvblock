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

asm ( ".code16gcc\n" );
#include "main.h"
#include "asm.h"
#include "pxe.h"
#include "lib.h"

#define D(x) printf(#x "\n");
#define BP 562

int imagesize = ( int )&end;

unsigned char clientmac[6];
unsigned char servermac[6];
unsigned int major;
unsigned int minor;
int tag = 0;

int heads,
 sectors,
 cylinders;
unsigned int disksize;
unsigned int realdisksize;

unsigned char bootdrive;
unsigned char aoedrive;
int drives = 0;
t_abft _abft;
t_abft *abft;
int checksum = 0;

int getbootinfo (
	void
 );
void getbootdriveinfo (
	void
 );
void readwrite (
	unsigned char read,
	unsigned int lba,
	int count,
	unsigned short seg,
	unsigned short off
 );
int pollisr (
	void
 );
void sendreceive (
	t_aoe * sendbuffer,
	int sendsize,
	t_aoe * receivebuffer,
	int *receivesize
 );
void sendpacket (
	void *packet,
	int size
 );
int processpacket (
	void *buffer,
	int *size
 );
void printbuffer (
	unsigned short seg,
	unsigned short off,
	size_t n
 );

int
chk (
	void
 )
{
	int i1,
	 i2,
	 r = 0;
	unsigned char buffer[16];
	for ( i1 = 0x9ecc; i1 < 0xa000; i1++ )
		{
//  for (i1 = (segment + 0x1000); i1 < 0x9c7f; i1++) {
			segmemcpy ( ( segment << 16 ) + ( int )buffer, i1 << 16, 16 );
			for ( i2 = 0; i2 < 16; i2++ )
				{
					r += buffer[i2];
				}
		}
	return r;
}

int
_main (
	t_cpu * cpu
 )
{
	t_PXENV_UNDI_OPEN undiopen;
	t_PXENV_UNDI_GET_INFORMATION undigetinformation;
	int i,
	 c,
	 v;
	unsigned char k;

	printf ( "\nWelcome to AoE Boot...\n\n" );

	checksum = chk (  );
	printf ( "Checksum: %08x\n", checksum );

	memset ( servermac, 0xff, 6 );
	if ( !( v = pxeinit (  ) ) )
		{
			printf ( "PXE API Vector not found\n" );
			halt (  );
		}

	memset ( ( void * )&undiopen, 0, sizeof ( undiopen ) );
	undiopen.PktFilter = FLTR_DIRECTED | 0xf;
	if ( api ( PXENV_UNDI_OPEN, &undiopen ) )
		apierror ( "PXENV_UNDI_OPEN", undiopen.Status );
	memset ( ( void * )&undigetinformation, 0, sizeof ( undigetinformation ) );
	if ( api ( PXENV_UNDI_GET_INFORMATION, &undigetinformation ) )
		apierror ( "PXENV_UNDI_GET_INFORMATION", undigetinformation.Status );
	irq = undigetinformation.IntNumber;
	if ( !getbootinfo (  ) )
		halt (  );
	printf
		( "Segment: %04x  API: %04x:%04x  IRQ: %d%s  NIC: %02x:%02x:%02x:%02x:%02x:%02x\n",
			segment, ( ( v >> 16 ) & 0xffff ), ( v & 0xffff ), irq,
			( irq == 0 ? " (polling)" : "" ), clientmac[0], clientmac[1],
			clientmac[2], clientmac[3], clientmac[4], clientmac[5] );

	getbootdriveinfo (  );
	segmemcpy ( ( ( segment << 16 ) + ( int )&drives ), 0x00400075, 1 );
	printf
		( "Target: e%d.%d  Size: %dM  Cylinders: %d  Heads: %d  Sectors: %d\n\n",
			major, minor, ( ( disksize / 2 ) / 1024 ), cylinders, heads, sectors );
	do
		{
			printf ( "Boot from (N)etwork" );
			if ( drives > 0 )
				printf ( ", (H)arddisk" );
			printf ( " or (F)loppy? " );
			if ( ( k = getkey ( PROMPTTIMEOUT ) ) >= 'a' )
				k -= 'a' - 'A';
			if ( k == '\0' || k == '\n' )
				{
					k = 'N';
				}
			else
				{
					putchar ( k );
				}
			putchar ( '\n' );
	} while ( !( k == 'N' || k == 'F' || ( k == 'H' && drives > 0 ) ) );

	switch ( k )
		{
			case 'N':
				abft = ( t_abft * ) ( ( ( ( int )&_abft ) + 15 ) & 0xfffffff0 );
				memset ( abft, 0, sizeof ( t_abft ) - 15 );
				abft->signature = 0x54464261;
				abft->length = sizeof ( t_abft ) - 15;
				abft->revision = 1;
				memcpy ( abft->clientmac, clientmac, 6 );
				abft->major = major;
				abft->minor = minor;
				c = 0;
				for ( i = 0; i < ( sizeof ( t_abft ) - 15 ); i++ )
					c -= ( ( unsigned char * )abft )[i];
				abft->checksum = c & 0xff;
				bootdrive = 0x80;
				aoedrive = 0x80;
				drives++;
				break;
			case 'H':
				bootdrive = 0x80;
				aoedrive = 0x80 + drives++;
				break;
			case 'F':
				bootdrive = 0x00;
				aoedrive = 0x80 + drives++;
				break;
		}

	segmemcpy ( 0x00400075, ( ( ( segment << 16 ) + ( int )&drives ) ), 1 );
	cpu->eax &= 0xffff0000;
	cpu->eax |= 0x0201;
	cpu->ecx &= 0xffff0000;
	cpu->ecx |= 0x0001;
	cpu->ebx &= 0xffff0000;
	cpu->ebx |= 0x7c00;
	cpu->edx &= 0xffff0000;
	cpu->edx |= bootdrive;
	cpu->es &= 0;
	oldint13 = GETVECTOR ( 0x13 );
	SETVECTOR ( 0x13, ( segment << 16 ) + ( int )int13 );
	return 0;
}

int
getbootinfo (
	void
 )
{
	t_PXENV_GET_CACHED_INFO getinfo;
	BOOTPLAYER info;
	char rootpath[64];
	char *endptr;
	int i,
	 l,
	 e;

	memset ( ( void * )&getinfo, 0, sizeof ( getinfo ) );
	memset ( ( void * )&info, 0, sizeof ( info ) );
	getinfo.PacketType = PXENV_PACKET_TYPE_CACHED_REPLY;
	getinfo.BufferSize = 0;
	getinfo.Buffer.segment = 0;
	getinfo.Buffer.offset = 0;
	if ( api ( PXENV_GET_CACHED_INFO, ( void * )&getinfo ) )
		apierror ( "PXENV_GET_CACHED_INFO", getinfo.Status );
	segmemcpy ( ( ( segment << 16 ) + ( int )&info ),
							( ( getinfo.Buffer.segment << 16 ) + getinfo.Buffer.offset ),
							sizeof ( info ) );
	memcpy ( clientmac, info.CAddr, 6 );

	if ( ( *( unsigned int * )info.vendor.v.magic ) != VM_RFC1048 )
		{
			printf ( "Invalid DHCP options magic...\n" );
			return 0;
		}

	i = 4;
	while ( i < BOOTP_DHCPVEND && info.vendor.d[i] != 0xff )
		{
			if ( info.vendor.d[i] == 0x11 )
				break;
			i++;
			i += info.vendor.d[i];
			i++;
		}
	if ( i >= BOOTP_DHCPVEND || info.vendor.d[i] == 0xff )
		{
			printf ( "No root-path found in DHCP options...\n" );
			return 0;
		}

	i++;
	if ( info.vendor.d[i] + i >= BOOTP_DHCPVEND )
		{
			printf
				( "DHCP option block > BOOTP_DHCPVEND bytes, root-path truncated...\n" );
			return 0;
		}

	l = info.vendor.d[i];
	memcpy ( &rootpath[0], &info.vendor.d[i + 1], l );
	rootpath[l] = '\0';
	i = 0;

	while ( i < l && rootpath[i] == ' ' )
		i++;
	e = 1;
	do
		{
			if ( tolower ( rootpath[i++] ) != 'a' )
				break;
			if ( tolower ( rootpath[i++] ) != 'o' )
				break;
			if ( tolower ( rootpath[i++] ) != 'e' )
				break;
			if ( rootpath[i++] != ':' )
				break;
			if ( tolower ( rootpath[i++] ) != 'e' )
				break;
			if ( !isdigit ( rootpath[i] ) )
				break;
			major = strtol ( &rootpath[i], &endptr, 10 );
			i += endptr - &rootpath[i];
			if ( rootpath[i++] != '.' )
				break;
			if ( !isdigit ( rootpath[i] ) )
				break;
			minor = strtol ( &rootpath[i], &endptr, 10 );
			i += endptr - &rootpath[i];
			e = 0;
	} while ( 0 );

	if ( e )
		{
			printf ( "Error in root-path\n" );
			return 0;
		}

	if ( major > 65535 )
		{
			printf ( "Error in major...\n" );
			return 0;
		}

	if ( major > 255 )
		{
			printf ( "Error in minor...\n" );
			return 0;
		}

	while ( i < l && ( rootpath[i] == ' ' || rootpath[i] == '\0' ) )
		i++;
	if ( i != l )
		{
			printf ( "Garbage after minor..." );
			return 0;
		}
	return 1;
}

void
getbootdriveinfo (
	void
 )
{
	unsigned char sendbuffer[MAXPACKETSIZE];
	unsigned char receivebuffer[MAXPACKETSIZE];
	t_aoe *send = ( t_aoe * ) sendbuffer;
	t_aoe *receive = ( t_aoe * ) receivebuffer;
	t_mbr *mbr;
	int i;

	memset ( send, 0, sizeof ( t_aoe ) );
	memcpy ( send->dst, servermac, 6 );
	memcpy ( send->src, clientmac, 6 );
	send->protocol = htons ( AOE_PROTOCOL );
	send->ver = AOE_VERSION;
	send->major = htons ( major );
	send->minor = minor;
	send->command = 0;
	send->aflags = 0;
	send->count = 1;

	send->cmd = 0xec;							// IDENTIFY DEVICE
	sendreceive ( send, sizeof ( t_aoe ) - ( 2 * SECTORSIZE ), receive, &i );
	realdisksize = disksize = *( unsigned int * )&( receive->data[200] );

	send->cmd = 0x24;							// READ SECTOR
	send->lba0 = 0;
	send->lba1 = 0;
	send->lba2 = 0;
	send->lba3 = 0;
	send->lba4 = 0;
	send->lba5 = 0;
	sendreceive ( send, sizeof ( t_aoe ) - ( 2 * SECTORSIZE ), receive, &i );
	mbr = ( t_mbr * ) & ( receive->data[0] );

	heads = 255;
	sectors = 63;
	if ( mbr->magic == 0xaa55 )
		{
			for ( i = 0; i < 4; i++ )
				{
					if ( mbr->partition[i].size != 0 )
						{
							heads = mbr->partition[i].endhead + 1;
							sectors = mbr->partition[i].endsector;
							break;
						}
				}
		}
	cylinders = disksize / ( heads * sectors );
	disksize = cylinders * heads * sectors;
}

void
_int13 (
	t_cpu * cpu
 )
{
	t_INT13_EXTENDED_READWRITE rwpacket;
	t_INT13_EXTENDED_GET_PARAMETERS parameterspacket;
	unsigned int cylinder,
	 head,
	 sector,
	 lba;
	int c;

	if ( checksum != ( c = chk (  ) ) )
		{
			printf ( "\n\nin checksum failed on tag: %d (%08x != %08x)\n", tag,
							 checksum, c );
			debug (  );
			halt (  );
		}
	switch ( ( cpu->eax >> 8 ) & 0xff )
		{
				// reset disk
				//  dl: drive number
				// returns:
				//  ah = 0 and clear cf on success
			case 0x00:
				if ( ( cpu->edx & 0xff ) != aoedrive )
					goto unhandled;
				cpu->eax &= ~( 0xff << 8 );
				cpu->eflags &= ~1;
				break;

				// read sectors & write sectors
				//  al: number of sectors to read/write (must be nonzero)
				//  ch: low eight bits of cylinder number
				//  cl: sector number 1-63 (bits 0-5)
				//      high two bits of cylinder (bits 6-7)
				//  dh: head number
				//  dl: drive number
				//  es:bx: data buffer
				// returns:
				//  ah = 0 and clear cf on success
			case 0x02:
			case 0x03:
				if ( ( cpu->edx & 0xff ) != aoedrive )
					goto unhandled;
				cylinder = ( ( cpu->ecx >> 8 ) & 0xff ) + ( ( cpu->ecx & 0xc0 ) << 2 );
				head = ( cpu->edx >> 8 ) & 0xff;
				sector = cpu->ecx & 0x3f;
				lba = ( ( ( cylinder * heads ) + head ) * sectors ) + sector - 1;
				readwrite ( ( ( ( cpu->eax >> 8 ) & 0xff ) == 0x02 ? 1 : 0 ), lba,
										cpu->eax & 0xff, cpu->es, cpu->ebx & 0xffff );
				cpu->eax &= ~( 0xff << 8 );
				cpu->eflags &= ~1;
				break;

				// verify sectors
				//  dl: drive number
				// returns:
				//  ah = 0 and clear cf on success
			case 0x04:
				if ( ( cpu->edx & 0xff ) != aoedrive )
					goto unhandled;
				cpu->eax &= ~( 0xff << 8 );
				cpu->eflags &= ~1;
				break;

				// get parameters
				//  dl: drive number
				// returns:
				//  ah = 0 and clear cf on success
				//  ch: low eight bits of maximum cylinder number
				//  cl: maximum sector number (bits 5-0)
				//      high two bits of maximum cylinder number (bits 7-6)
				//  dh: maximum head number
				//  dl: number of drives
			case 0x08:
				if ( ( cpu->edx & 0xff ) != aoedrive )
					goto unhandled;
				cpu->ecx &= ~0xffff;
				if ( cylinders > 1024 )
					{
						cpu->ecx |= 0x3ff << 6;
					}
				else
					{
						cpu->ecx |= ( ( cylinders - 1 ) & 0xff ) << 8;
						cpu->ecx |= ( ( cylinders - 1 ) >> 8 ) << 6;
					}
				cpu->ecx |= sectors;

				cpu->edx &= ~0xffff;
				cpu->edx |= ( heads - 1 ) << 8;
				cpu->edx |= drives;

				cpu->eax &= ~( 0xff << 8 );
				cpu->eflags &= ~1;
				break;

				// get disk type
				//  dl: drive number
				// returns:
				//  clear cf on success
				//  ah: type (3 = harddisk)
				//  cx:dx: number of sectors
			case 0x15:
				if ( ( cpu->edx & 0xff ) != aoedrive )
					goto unhandled;
				if ( ( cpu->edx & 0xff ) != aoedrive )
					{
						cpu->eflags |= 1;
						break;
					}
				cpu->eax &= ~( 0xff << 8 );
				cpu->eax |= 3 << 8;
				cpu->ecx &= ~0xffff;
				cpu->ecx |= realdisksize >> 16;
				cpu->edx &= ~0xffff;
				cpu->edx |= realdisksize & 0xffff;
				cpu->eflags &= ~1;
				break;

				// set media type for format
				// ch: lower 8 bits of highest cylinder number
				// cl: sectors per track (bits 0-5)
				//     high 2 bits of cylinder number (bit 6+7)
				//  dl: drive number
				// returns:
				//  clear cf on success
				//  ah: status (1 = function not available)
				//  es:di: pointer to parameter table
			case 0x18:
				if ( ( cpu->edx & 0xff ) != aoedrive )
					goto unhandled;
				cpu->eax &= ~( 0xff << 8 );
				cpu->eax |= 1 << 8;
				cpu->eflags &= ~1;
				break;

				// extentions
				//  bx: 0x55aa
				//  dl: drive number
				// returns:
				//  clear cf on success
				//  ah: version (1)
				//  bx: 0xaa55
				//  cx: support map (0b001)
			case 0x41:
				if ( ( cpu->edx & 0xff ) != aoedrive )
					goto unhandled;
				cpu->eax &= ~( 0xff << 8 );
				cpu->eax |= 1 << 8;
				cpu->ebx &= ~0xffff;
				cpu->ebx |= 0xaa55;
				cpu->ecx &= ~0xffff;
				cpu->ecx |= 1;
				cpu->eflags &= ~1;
				break;

				// extended read & extended write
				//  for write: al: write flags (bit 0: verify)
				//  dl: drive number
				//  ds:si: disk address packet
				//   (byte)0: size of packet (10h or 18h)
				//   (byte)1: reserved
				//   (word)2: count
				//   (long)4: buffer
				//   (longlong)8: lba
				// returns:
				//  ah = 0 and clear cf on success
			case 0x42:
			case 0x43:
				if ( ( cpu->edx & 0xff ) != aoedrive )
					goto unhandled;
				segmemcpy ( ( ( segment << 16 ) + ( int )&rwpacket ),
										( ( cpu->ds << 16 ) + ( cpu->esi & 0xffff ) ),
										sizeof ( t_INT13_EXTENDED_READWRITE ) );
				readwrite ( ( ( ( cpu->eax >> 8 ) & 0xff ) == 0x42 ? 1 : 0 ),
										rwpacket.lba, rwpacket.count, rwpacket.buffersegment,
										rwpacket.bufferoffset );
				cpu->eax &= ~( 0xff << 8 );
				cpu->eflags &= ~1;
				break;

				// extended get parameters
				//  dl: drive number
				//  ds:si: drive parameters packet to fill in
				//   (word)0: packet size (1ah)
				//   (word)2: information flags (3)
				//   (long)4: cylinders
				//   (long)8: heads
				//   (long)12: sectors
				//   (longlong)16: total size in sectors
				//   (word)24: bytes per sector
				// returns:
				//  ah = 0 and clear cf on success
			case 0x48:
				if ( ( cpu->edx & 0xff ) != aoedrive )
					goto unhandled;
				memset ( &parameterspacket, 0,
								 sizeof ( t_INT13_EXTENDED_GET_PARAMETERS ) );
				parameterspacket.structsize =
					sizeof ( t_INT13_EXTENDED_GET_PARAMETERS );
				parameterspacket.flags = 3;
				if ( cylinders > 1024 )
					{
						parameterspacket.cylinders = 1024;
					}
				else
					{
						parameterspacket.cylinders = cylinders;
					}
				parameterspacket.heads = heads;
				parameterspacket.sectors = sectors;
				parameterspacket.disksize = realdisksize;
				parameterspacket.sectorsize = SECTORSIZE;
				segmemcpy ( ( ( cpu->ds << 16 ) | ( cpu->esi & 0xffff ) ),
										( ( segment << 16 ) + ( int )&parameterspacket ),
										sizeof ( t_INT13_EXTENDED_GET_PARAMETERS ) );
				cpu->eax &= ~( 0xff << 8 );
				cpu->eflags &= ~1;
				break;

			default:
				goto unhandled;
		}
	if ( checksum != ( c = chk (  ) ) )
		{
			printf ( "\n\nout checksum failed on tag: %d (%08x != %08x)\n", tag,
							 checksum, c );
			debug (  );
			halt (  );
		}
	return;

unhandled:
#ifndef MINIMAL
	printf ( "\n\nUnhandled INT13...\n" );
	printf ( "eax:%08x ebx:%08x ecx:%08x edx:%08x esi:%08x edi:%08x\n", cpu->eax,
					 cpu->ebx, cpu->ecx, cpu->edx, cpu->esi, cpu->edi );
	printf ( "ds:%04x es:%04x fs:%04x gs:%04x", cpu->ds, cpu->es, cpu->fs,
					 cpu->gs );
	printf ( " ID:%d VIP:%d VIF:%d AC:%d VM:%d RF:%d NT:%d IOPL:%d%d\n",
					 ( ( cpu->eflags >> 21 ) & 1 ), ( ( cpu->eflags >> 20 ) & 1 ),
					 ( ( cpu->eflags >> 19 ) & 1 ), ( ( cpu->eflags >> 18 ) & 1 ),
					 ( ( cpu->eflags >> 17 ) & 1 ), ( ( cpu->eflags >> 16 ) & 1 ),
					 ( ( cpu->eflags >> 14 ) & 1 ), ( ( cpu->eflags >> 13 ) & 1 ),
					 ( ( cpu->eflags >> 12 ) & 1 ) );
	printf
		( "OF:%d DF:%d IF:%d TF:%d SF:%d ZF:%d AF:%d PF:%d CF:%d  EFLAGS:%08x  aoedrive:%02x\n",
			( ( cpu->eflags >> 11 ) & 1 ), ( ( cpu->eflags >> 10 ) & 1 ),
			( ( cpu->eflags >> 9 ) & 1 ), ( ( cpu->eflags >> 8 ) & 1 ),
			( ( cpu->eflags >> 7 ) & 1 ), ( ( cpu->eflags >> 6 ) & 1 ),
			( ( cpu->eflags >> 4 ) & 1 ), ( ( cpu->eflags >> 2 ) & 1 ),
			( ( cpu->eflags >> 0 ) & 1 ), cpu->eflags, aoedrive );
#endif
//  _CHAININTERRUPT(_oldint13, cpu);
	printf ( "\n\nUnhandled INT13...\n" );
	halt (  );
}

void
readwrite (
	unsigned char read,
	unsigned int lba,
	int count,
	unsigned short seg,
	unsigned short off
 )
{
	unsigned char sendbuffer[MAXPACKETSIZE];
	unsigned char receivebuffer[MAXPACKETSIZE];
	t_aoe *send = ( t_aoe * ) sendbuffer;
	t_aoe *receive = ( t_aoe * ) receivebuffer;
	unsigned int s = lba;
	int i,
	 n,
	 c;

	printf ( "tag:%d rw:%d seg:%04x off:%04x\n", tag, read, seg, off );
	while ( count > 0 )
		{
			if ( count == 1 )
				{
					c = 1;
					count = 0;
				}
			else
				{
					c = 2;
					count -= 2;
				}

			memset ( send, 0, sizeof ( t_aoe ) );
			memcpy ( send->dst, servermac, 6 );
			memcpy ( send->src, clientmac, 6 );
			send->protocol = htons ( AOE_PROTOCOL );
			send->ver = AOE_VERSION;
			send->major = htons ( major );
			send->minor = minor;
			send->tag = tag;
			send->command = 0;
			send->aflags = 0;
			send->count = c;
			if ( read )
				{
					send->cmd = 0x24;			// READ SECTOR
				}
			else
				{
					send->cmd = 0x34;			// WRITE SECTOR
				}
			send->lba0 = ( s >> 0 ) & 255;
			send->lba1 = ( s >> 8 ) & 255;
			send->lba2 = ( s >> 16 ) & 255;
			send->lba3 = ( s >> 24 ) & 255;
//    send->lba4 = (s >> 32) & 255;
//    send->lba5 = (s >> 40) & 255;
			send->lba4 = 0;
			send->lba5 = 0;

			if ( read )
				{
					n = 0;
				}
			else
				{
					segmemcpy ( ( ( segment << 16 ) + ( int )send->data ),
											( ( seg << 16 ) + off ), c * SECTORSIZE );
					n = c;
				}

			sendreceive ( send, sizeof ( t_aoe ) - ( 2 * SECTORSIZE ), receive, &i );

			if ( read )
				{
					segmemcpy ( ( ( seg << 16 ) + off ),
											( ( segment << 16 ) + ( int )receive->data ),
											c * SECTORSIZE );
				}

			s += c;
			off += c * SECTORSIZE;
		}
}

int
pollisr (
	void
 )
{
	t_PXENV_UNDI_ISR isr;
	memset ( ( void * )&isr, 0, sizeof ( isr ) );
	isr.FuncFlag = PXENV_UNDI_ISR_IN_START;
	if ( api ( PXENV_UNDI_ISR, ( void * )&isr ) )
		apierror ( "PXENV_UNDI_ISR", isr.Status );
	if ( isr.FuncFlag == PXENV_UNDI_ISR_OUT_OURS )
		return 1;
	return 0;
}

void
sendreceive (
	t_aoe * sendbuffer,
	int sendsize,
	t_aoe * receivebuffer,
	int *receivesize
 )
{
	int old_21,
	 old_a1;
	int volatile oldisr;
	int volatile oldint0;
	int volatile oldint1;
	int volatile oldint2;
	int volatile oldint3;
	int volatile oldint4;
	int volatile oldint5;
	int volatile oldint6;
	int volatile oldint7;
	int volatile oldint8;

	cli (  );
	timer = RECEIVETIMEOUT + 1;
	sendbuffer->tag = tag;
	gotisr = 0;
	old_21 = inb ( 0x21 );
	old_a1 = inb ( 0xa1 );
	oldint0 = GETVECTOR ( 0x0 );
	oldint1 = GETVECTOR ( 0x1 );
	oldint2 = GETVECTOR ( 0x2 );
	oldint3 = GETVECTOR ( 0x3 );
	oldint4 = GETVECTOR ( 0x4 );
	oldint5 = GETVECTOR ( 0x5 );
	oldint6 = GETVECTOR ( 0x6 );
	oldint7 = GETVECTOR ( 0x7 );
	oldint8 = GETVECTOR ( 0x8 );
	SETVECTOR ( 0x0, ( segment << 16 ) + ( int )i0 );
	SETVECTOR ( 0x1, ( segment << 16 ) + ( int )i1 );
	SETVECTOR ( 0x2, ( segment << 16 ) + ( int )i2 );
	SETVECTOR ( 0x3, ( segment << 16 ) + ( int )i3 );
	SETVECTOR ( 0x4, ( segment << 16 ) + ( int )i4 );
	SETVECTOR ( 0x5, ( segment << 16 ) + ( int )i5 );
	SETVECTOR ( 0x6, ( segment << 16 ) + ( int )i6 );
	SETVECTOR ( 0x7, ( segment << 16 ) + ( int )i7 );
	SETVECTOR ( 0x8, ( segment << 16 ) + ( int )int8 );
	if ( irq == 0 )
		{
			outb ( ~0, 0xa1 );
			outb ( ~1, 0x21 );
		}
	else
		{
			if ( irq <= 7 )
				{
					oldisr = GETVECTOR ( irq + 0x8 );
					SETVECTOR ( irq + 0x8, ( segment << 16 ) + ( int )isr );
//      outb((inb(0x21) & (~(1 << irq))), 0x21);
					outb ( ~0, 0xa1 );
					outb ( ~( 1 << irq ) & ~1, 0x21 );
				}
			else
				{
					oldisr = GETVECTOR ( irq + 0x70 - 0x8 );
					SETVECTOR ( irq + 0x70 - 0x8, ( segment << 16 ) + ( int )isr );
//      outb((inb(0xa1) & (~(1 << (irq - 8)))), 0xa1);
//      outb((inb(0x21) & (~(1 << 2))), 0x21);
					outb ( ~( 1 << ( irq - 8 ) ), 0xa1 );
					outb ( ~( 1 << 2 ) & ~1, 0x21 );
				}
		}
	while ( 1 )
		{
//if (tag >= 732) printf("lets crash?\n");
			if ( timer > RECEIVETIMEOUT )
				{
					sendpacket ( sendbuffer, sendsize );
					timer = 0;
				}
//if (tag >= 732) printf("ok...\n");
			gotisr = 0;
			if ( irq == 0 )
				{
					sti (  );
					while ( !( gotisr = pollisr (  ) ) && timer <= RECEIVETIMEOUT );
					cli (  );
				}
			else
				{
					sti (  );
					while ( !gotisr && timer <= RECEIVETIMEOUT );
					cli (  );
				}
			if ( !gotisr )
				continue;
			if ( processpacket ( receivebuffer, receivesize ) <= 0 )
				continue;
			if ( ntohs ( receivebuffer->protocol ) != AOE_PROTOCOL )
				continue;
			if ( ntohs ( receivebuffer->major ) != major )
				continue;
			if ( receivebuffer->minor != minor )
				continue;
			if ( !( receivebuffer->flags & ( 1 << 3 ) ) )
				continue;
			if ( receivebuffer->tag != tag )
				continue;
			if ( !( receivebuffer->status & ( 1 << 6 ) ) )
				continue;								// Drive not ready
			if ( receivebuffer->status & ( 1 << 5 ) )
				{
					printf ( "AoE ATA Drive error.\n" );
					halt (  );
				}
			break;
		}
	if ( memcmp ( servermac, "\xff\xff\xff\xff\xff\xff", 6 ) == 0 )
		memcpy ( servermac, receivebuffer->src, 6 );
	outb ( old_a1, 0xa1 );
	outb ( old_21, 0x21 );
	SETVECTOR ( 0x0, oldint0 );
	SETVECTOR ( 0x1, oldint1 );
	SETVECTOR ( 0x2, oldint2 );
	SETVECTOR ( 0x3, oldint3 );
	SETVECTOR ( 0x4, oldint4 );
	SETVECTOR ( 0x5, oldint5 );
	SETVECTOR ( 0x6, oldint6 );
	SETVECTOR ( 0x7, oldint7 );
	SETVECTOR ( 0x8, oldint8 );
	if ( irq != 0 )
		{
			if ( irq <= 7 )
				{
					SETVECTOR ( irq + 0x8, oldisr );
//      outb((inb(0x21) | (1 << irq)), 0x21);
				}
			else
				{
					SETVECTOR ( irq + 0x70 - 0x8, oldisr );
//      outb((inb(0xa1) | (1 << (irq - 8))), 0xa1);
				}
		}
	tag++;
}

void
sendpacket (
	void *packet,
	int size
 )
{
	t_PXENV_UNDI_TRANSMIT transmit;
	t_PXENV_UNDI_TBD tbd;

	memset ( ( void * )&transmit, 0, sizeof ( transmit ) );
	memset ( ( void * )&tbd, 0, sizeof ( tbd ) );
	transmit.Protocol = P_UNKNOWN;
	transmit.XmitFlag = XMT_DESTADDR;
	transmit.TBD.segment = segment;
	transmit.TBD.offset = ( unsigned int )&tbd;
	tbd.ImmedLength = size;
	tbd.Xmit.segment = segment;
	tbd.Xmit.offset = ( unsigned int )packet;
	tbd.DataBlkCount = 0;
	if ( api ( PXENV_UNDI_TRANSMIT, ( void * )&transmit ) )
		apierror ( "PXENV_UNDI_TRANSMIT", transmit.Status );
}

int
processpacket (
	void *buffer,
	int *size
 )
{
	t_PXENV_UNDI_ISR isr;
	int receiveddata = 0;
	int r = 0;

	memset ( ( void * )&isr, 0, sizeof ( isr ) );
	isr.FuncFlag = PXENV_UNDI_ISR_IN_PROCESS;
	if ( api ( PXENV_UNDI_ISR, ( void * )&isr ) )
		apierror ( "PXENV_UNDI_ISR", isr.Status );
	if ( isr.FuncFlag == PXENV_UNDI_ISR_OUT_DONE
			 || isr.FuncFlag == PXENV_UNDI_ISR_OUT_BUSY )
		return 0;

next:
	if ( isr.FuncFlag == PXENV_UNDI_ISR_OUT_TRANSMIT )
		goto release;
	if ( isr.FuncFlag == PXENV_UNDI_ISR_OUT_RECEIVE )
		{
			if ( isr.BufferLength > MAXPACKETSIZE )
				{
					printf ( "Received packet too large... (%d)\n", isr.BufferLength );
					goto release;
				}
			*size = isr.FrameLength;
			segmemcpy ( ( ( segment << 16 ) + ( int )buffer + receiveddata ),
									( ( isr.Frame.segment << 16 ) + isr.Frame.offset ),
									isr.BufferLength );
			receiveddata += isr.BufferLength;
			r = 1;
			goto release;
		}

	printf ( "Unknown function?...\n" );

release:
	memset ( ( void * )&isr, 0, sizeof ( isr ) );
	isr.FuncFlag = PXENV_UNDI_ISR_IN_GET_NEXT;
	if ( api ( PXENV_UNDI_ISR, ( void * )&isr ) )
		apierror ( "PXENV_UNDI_ISR", isr.Status );
	if ( isr.FuncFlag != PXENV_UNDI_ISR_OUT_DONE
			 && isr.FuncFlag != PXENV_UNDI_ISR_OUT_BUSY )
		goto next;
	return r;
}

#ifndef MINIMAL
void
printbuffer (
	unsigned short seg,
	unsigned short off,
	size_t n
 )
{
	int i,
	 l,
	 s;
	unsigned char buffer[16];

	l = 0;
	while ( n > 0 )
		{
			s = ( n > 16 ? 16 : n );
			segmemcpy ( ( int )( ( segment << 16 ) + ( ( int )&buffer ) ),
									( ( ( int )( seg << 16 ) ) + off ), s );
			printf ( "%04x:%04x  ", seg, off );
			for ( i = 0; i < s; i++ )
				{
					printf ( "%02x ", buffer[i] );
				}
			for ( i = s; i < 16; i++ )
				printf ( "   " );
			for ( i = 0; i < s; i++ )
				{
					if ( buffer[i] > 32 )
						{
							putchar ( buffer[i] );
						}
					else
						{
							putchar ( '.' );
						}
				}
			putchar ( '\n' );
			l++;
			if ( ( l == 16 ) && ( n > 16 ) )
				{
					l = 0;
					printf ( "Press a key\n" );
					getkey ( 0 );
				}
			n -= s;
			off += 16;
		}
}
#endif
