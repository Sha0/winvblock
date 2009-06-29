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

#include "aoe.h"

_int:		.byte	0
_tag:		.long	0
_oldisr:	.long	0
_api:		.long	0
send:		.org	.+AoEsize, 0
receive:	.org	.+AoEsize, 0

_undi_get_information:			# 0x000c
	.word	0			# PXENV_STATUS Status
	.word	0			# UINT16 BaseIo
	.word	0			# UINT16 IntNumber
	.word	0			# UINT16 MaxTranUnit
	.word	0			# UINT16 HwType
	.word	0			# UINT16 HwAddrLen
	.org	.+16, 0			# MAC_ADDR CurrentNodeAddress
	.org	.+16, 0			# MAC_ADDR PermNodeAddress
	.word	0			# SEGSEL ROMAddress
	.word	0			# UINT16 RxBufCt
	.word	0			# UINT16 TxBufCt

_get_cached_info:			# 0x0071
	.word	0			# PXENV_STATUS Status
	.word	3			# UINT16 PacketType
	.word	0			# UINT16 BufferSize
	.long	0			# SEGOFF16 Buffer
	.word	0			# UINT16 BufferLimit

_undi_open:				# 0x0006
	.word	0			# PXENV_STATUS status
	.word	0			# UINT16 OpenFlag
	.word	3			# UINT16 PktFilter
	.org	.+(2 + (8 * 16)), 0	# t_PXENV_UNDI_MCAST_ADDRESS R_Mcast_Buf

_undi_isr_start:			# 0x0014
	.word	0			# PXENV_STATUS Status
	.word	0			# UINT16 FuncFlag
	.word	0			# UINT16 BufferLength
	.word	0			# UINT16 FrameLength
	.word	0			# UINT16 FrameHeaderLength
	.long	0			# SEGOFF16 Frame
	.byte	0			# UINT8 ProtType
	.byte	0			# UINT8 PktType

_undi_isr:				# 0x0014
	.word	0			# PXENV_STATUS Status
	.word	0			# UINT16 FuncFlag
	.word	0			# UINT16 BufferLength
	.word	0			# UINT16 FrameLength
	.word	0			# UINT16 FrameHeaderLength
	.long	0			# SEGOFF16 Frame
	.byte	0			# UINT8 ProtType
	.byte	0			# UINT8 PktType

_undi_transmit_packet:			# 0x0008
	.word	0			# PXENV_STATUS Status
	.byte	0			# UINT8 Protocol
	.byte	0			# UINT8 XmitFlag
	.long	(send + AoEdstaddr)	# SEGOFF16 DestAddr
	.long	_undi_TBD		# SEGOFF16 TBD
	.org	.+(2 * 4), 0		# UINT32 Reserved[2]

_undi_TBD:
	.word	0			# UINT16 ImmedLength
	.long	send			# SEGOFF16 Xmit
	.word	0			# UINT16 DataBlkCount
	.org	.+(8 * 8), 0		# DataBlk DataBlock[8]

.globl pxeinit
pxeinit:
	enter	$0, $0
	movw	$0x5650, %ax		# Check for PXE extension
	int	$0x1a
	cmpw	$0x564e, %ax
	je	0f			# jump if PXE found
	print	"PXE check failed...\n"
	halt

0:	lesw	%es:0x28(%bx), %bx	# get !PXE in es:bx
	pushl	%es:0x10(%bx)		# get api entry point
	popl	%cs:_api

	push	$0x0006			# open undi
	push	$_undi_open
	call	api
	cmpw	$0, %ax
	je	0f
	print	"open undi failed\n"
	halt

0:	movw	%cs, %cs:_undi_TBD + 4
	movw	%cs, %cs:_undi_transmit_packet + 6
	movw	%cs, %cs:_undi_transmit_packet + 10
	movw	%cs, %cs:_debug_undi_TBD + 4
	movw	%cs, %cs:_debug_undi_transmit_packet + 6
	movw	%cs, %cs:_debug_undi_transmit_packet + 10

	pushw	$0x000c			# get nic information
	pushw	$_undi_get_information
	call	api
	cmpw	$0, %ax
	je	0f
	print	"get information failed\n"
	halt

0:	pushw	$0x0071			# get dhcp information
	pushw	$_get_cached_info
	call	api
	cmpw	$0, %ax
	je	0f
	print	"get dhcp information failed\n"
	halt

0:	movl	%cs:_undi_get_information + 12, %eax	# client mac (high 4)
	movl	%eax, %cs:(send + AoEsrcaddr)
	movl	%eax, %cs:_debug_srcaddr
	movl	%eax, %cs:_clientmac
	movw	%cs:_undi_get_information + 16, %ax	# client mac (low 2)
	movw	%ax, %cs:(send + AoEsrcaddr + 4)
	movw	%ax, %cs:_debug_srcaddr + 4
	movw	%ax, %cs:_clientmac + 4
	movw	%cs:_undi_get_information + 4, %ax	# irq
	movb	%al, %cs:_irq
	movw	$0xa288, %cs:(send + AoEprotocol)	# protocol
	movb	$0x10, %cs:(send + AoEver)		# version 1

	call	searchrootpath
	pushl	%eax
	call	readrootpath

	leave
	ret	$0

# api: calls undi api
#  (word)bp+6: function
#  (word)bp+4: struct adress in cs
# returns:
#  ax passed on from api call
.globl api
api:	enter	$0, $0
	pushfl
	pushw	%cs
	pushw	4(%bp)
	pushw	6(%bp)
	lcall	*%cs:_api
	addw	$6, %sp
	popfl
	leave
	ret	$4

# searchrootpath: search root-path in dhcp packet
# returns:
#  eax: position of root-path in memory
searchrootpath:
	enter	$0, $0
	pushw	%cx
	pushw	%ds
	pushw	%si

	ldsw	%cs:(_get_cached_info + 6), %si	# load ds:si with dhcp location
	xorb	%ah, %ah
	xorw	%cx, %cx
	addw	$0xf0, %si		# offset si to point to dhcp options
1:	incw	%cx
	lodsb
	cmpb	$0xff, %al
	jne	0f
2:	print	"No root-path found in DHCP options...\n"
	halt
0:	cmpb	$0x11, %al
	je	0f
	incw	%cx
	lodsb
	addw	%ax, %cx
	addw	%ax, %si
	cmpw	$0x1024, %cx
	jb	1b
	jmp	2b
0:	movw	%ds, %ax
	shll	$16, %eax
	movw	%si, %ax

	pop	%si
	pop	%ds
	pop	%cx
	leave
	ret	$0

# readrootpath: read root-path from dhcp and sets packet data
#  (long)bp+4: address of root-path
readrootpath:
	enter	$0, $0
	push	%ax
	push	%bx
	push	%ds
	push	%si

	mov	4(%bp), %si
	mov	6(%bp), %ds
	lodsb
	xor	%ah, %ah
	add	%ax, %si
	movb	$0, %ds:(%si)
	sub	%ax, %si

	call	5f			# read spaces
	lodsw				# read "aoe:e"
	or	$0x2020, %ax
	cmp	$0x6f61, %ax		# 'oa'
	jne	9f			# general error
	lodsw
	or	$0x0020, %ax
	cmp	$0x3a65, %ax		# ':e'
	jne	9f			# general error
	lodsb
	or	$0x20, %al
	cmp	$'e', %al
	jne	9f			# general error

	call	6f			# read number
	jc	7f			# major error
	mov	%ax, %cs:_major
	xchg	%ah, %al
	mov	%ax, %cs:(send + AoEmajor)

	lodsb
	cmp	$'.', %al
	jne	9f			# general error
	call	6f			# read number
	jc	8f			# minor error
	cmp	$0, %ah
	jne	8f			# minor error
	mov	%al, %cs:_minor
	mov	%al, %cs:(send + AoEminor)

	call	5f			# read spaces
	lodsb
	cmp	$0, %al
	jne	9f			# general error
	pop	%si
	pop	%ds
	pop	%bx
	pop	%ax
	leave
	ret	$4

# local: read spaces
5:	push	%ax
0:	lodsb
	cmp	$' ', %al
	je	0b
	dec	%si
	pop	%ax
	ret

# local: read number
# returns number in %ax
# carry set on error
6:	lodsb
	cmp	$'0', %al
	jne	1f
	lodsb
	dec	%si
	cmp	$'0', %al
	jb	0f
	cmp	$'9', %al
	ja	0f
	stc
	ret
0:	xor	%ax, %ax
	clc
	ret

1:	dec	%si
	push	%bx
	push	%cx
	push	%dx
	xor	%ah, %ah
	mov	$10, %bx
	xor	%cx, %cx
	xor	%dx, %dx

0:	lodsb
	cmp	$'0', %al
	jb	1f
	cmp	$'9', %al
	ja	1f

	push	%ax
	mov	%cx, %ax
	mul	%bx
	cmp	$0, %dx
	jne	0f
	mov	%ax, %cx
	pop	%ax
	sub	$0x30, %al
	add	%ax, %cx
	jc	0f
	jmp	0b

0:	dec	%si
	pop	%dx
	pop	%cx
	pop	%bx
	stc
	ret
	
1:	dec	%si
	cmp	$0, %cx
	jne	0f
	pop	%dx
	pop	%cx
	pop	%bx
	stc
	ret

0:	mov	%cx, %ax
	pop	%dx
	pop	%cx
	pop	%bx
	clc
	ret	

# local: errors
7:	print	"root-path major out of range...\n"
	halt
8:	print	"root-path minor out of range...\n"
	halt
9:	print	"root-path misformed, should be \"aoe:e<major>.<minor>\"...\n"
	halt


.globl getdiskparameters
getdiskparameters:
	enter	$0, $0
	push	%eax
	push	%ebx
	push	%edx

	movl	$0xffffffff, %cs:(send + AoEdstaddr)	# broadcast
	movw	$0xffff, %cs:(send + AoEdstaddr + 4)
	movb	$0x1, %cs:(send + AoEcount)
	movb	$0xec, %cs:(send + AoEcmd)	# IDENTIFY DEVICE
	movw	$(AoEsize - 1024), %cs:_undi_TBD	# size
	call	requestpacket

	mov	%cs:(receive + AoEsrcaddr), %eax	# copy server mac
	mov	%eax, %cs:(send + AoEdstaddr)
	mov	%eax, %cs:(_servermac)
	mov	%cs:(receive + AoEsrcaddr + 4), %ax
	mov	%ax, %cs:(send + AoEdstaddr + 4)
	mov	%ax, %cs:(_servermac + 4)

	mov	%cs:(receive + AoEdata + 200), %eax	# copy max lba
	cmp	$0, %eax
	jne	0f
	print	"Disk size 0?\n"
	halt
0:	mov	%eax, %cs:_size

	movb	$0x1, %cs:(send + AoEcount)
	movb	$0x24, %cs:(send + AoEcmd)	# READ SECTOR
	movl	$0, %cs:(send + AoElba0)
	movw	$0, %cs:(send + AoElba4)
	movw	$(AoEsize - 1024), %cs:_undi_TBD	# size
	call	requestpacket
	mov	%cs:(receive + AoEdata + 510), %ax
	cmp	$0xaa55, %ax
	je	0f
	xor	%ax, %ax
	jmp	1f

0:	mov	%cs:(receive + AoEdata + 446 + 12), %eax
	cmp	$0, %ax
	je	0f
	mov	%cs:(receive + AoEdata + 446 + 5), %ax
	jmp	1f

0:	mov	%cs:(receive + AoEdata + 462 + 12), %eax
	cmp	$0, %ax
	je	0f
	mov	%cs:(receive + AoEdata + 462 + 5), %ax
	jmp	1f

0:	mov	%cs:(receive + AoEdata + 478 + 12), %eax
	cmp	$0, %ax
	je	0f
	mov	%cs:(receive + AoEdata + 478 + 5), %ax
	jmp	1f

0:	mov	%cs:(receive + AoEdata + 494 + 12), %eax
	cmp	$0, %ax
	je	1f
	mov	%cs:(receive + AoEdata + 494 + 5), %ax

1:	cmp	$0, %al
	jne	0f
	mov	$254, %al
0:	inc	%al
	mov	%al, %cs:_heads

	and	$0x3f, %ah
	cmp	$0, %ah
	jne	0f
	mov	$0x3f, %ah
0:	mov	%ah, %cs:_sectors

	xor	%ebx, %ebx
	xor	%edx, %edx
	mul	%ah
	mov	%ax, %bx
	mov	%cs:_size, %eax
	div	%ebx
	mov	%eax, %cs:_cylinders

	pop	%edx
	pop	%ebx
	pop	%eax
	leave
	ret	$0


# processsectors: reads or writes sectors
#  (word)bp+14: mode (0 = read, 1 = write)
#  (long)bp+10: buffer
#  (long)bp+6: lba
#  (word)bp+4: count
.globl processsectors
processsectors:
	enter	$0, $0
	push	%eax
	push	%bx
	push	%cx
	push	%ds
	push	%es
	push	%si
	push	%di

	cmpw	$0x7f, 4(%bp)		# check for count > 0x7f (127)
	jbe	0f
	jmp	1f
0:	cmpw	$0, 4(%bp)		# check for a count of 0
	jne	2f
1:	print	"Invalid sector count ("
	push	4(%bp)
	call	printnumber
	push	$')'
	call	printchar
	call	line
	halt

2:	xor	%ax, %ax		# next, do 1 or 2 sectors?
	cmpw	$1, 4(%bp)
	je	0f
	inc	%ax
0:	inc	%ax

	mov	14(%bp), %bx
	movb	$0x24, %cs:(send + AoEcmd)	# 0x24 = READ SECTOR
	cmp	$0, %bx
	je	0f
	addb	$0x10, %cs:(send + AoEcmd)	# 0x34 = WRITE SECTOR
0:	pushl	6(%bp)				# sector
	popl	%cs:(send + AoElba0)
	movw	$0, %cs:(send + AoElba4)
	mov	%al, %cs:(send + AoEcount)
	movw	$(AoEsize - 1024), %cs:_undi_TBD	# size
	cmp	$0, 14(%bp)			# read?
	je	1f

	mov	$512, %cx
	cmpb	$2, %cs:(send + AoEcount)	# 2 sectors?
	jne	0f
	add	$512, %cx
0:	add	%cx, %cs:_undi_TBD
	mov	10(%bp), %si
	mov	12(%bp), %ds
	mov	$(send + AoEdata), %di
	push	%cs
	pop	%es
	cld				# positive direction
	rep	movsb			# copy data to packet
1:	call	requestpacket

	cmp	$1, 14(%bp)		# write?
	je	1f
	push	%cs
	pop	%ds
	mov	$(receive + AoEdata), %si
	mov	$512, %cx
	cmpb	$2, %cs:(send + AoEcount)	# 2 sectors?
	jne	0f
	add	$512, %cx
0:	mov	10(%bp), %di
	mov	12(%bp), %es
	cld
	rep	movsb

1:	addl	$2, 6(%bp)		# next sectors
	addw	$1024, 10(%bp)		# add 1024 to buffer
	subw	$1, 4(%bp)		# count one down
	jz	0f			# no more sectors
	subw	$1, 4(%bp)		# count down another
	jnz	2b			# get more sectors?

0:	pop	%di
	pop	%si
	pop	%es
	pop	%ds
	pop	%cx
	pop	%bx
	pop	%eax
	leave
	ret	$12


# requestpacket: sends request and waits for a reply
requestpacket:
	enter	$0, $0
	push	%eax
	push	%ebx
	push	%ecx
	push	%es

	xorb	%bh, %bh
	movb	%cs:_irq, %bl
	cmpb	$0, %bl
	je	1f

	movb	$0, %cs:_int

	pushw	$0x0000			# set es to vector table segment
	popw	%es
	cmpb	$7, %bl
	jbe	0f
	addb	$(0x70 - 8 - 8), %bl
0:	addb	$8, %bl
	shlw	$2, %bx
	pushl	%es:(%bx)
	popl	%cs:_oldisr
	pushw	%cs
	pushw	$isr
	popl	%es:(%bx)

	xorb	%bh, %bh
	movb	%cs:_irq, %bl
	cmpb	$7, %bl
	jbe	0f
	subb	$8, %bl
	inb	$0xa1
	btrw	%bx, %ax
	mov	$2, %bl
0:	inb	$0x21
	btrw	%bx, %ax
	outb	$0x21

1:	pushl	%cs:_tag
	popl	%cs:(send + AoEtag)

0:	call	getticks
	mov	%eax, %ecx
	push	$0x0008			# transmit packet
	push	$_undi_transmit_packet
	call	api

1:	call	getpacket
	cmp	$1, %ax
	je	0f
	call	getticks
	sub	$2, %eax
	cmp	%eax, %ecx
	ja	1b
	jmp	0b

0:	incl	%cs:_tag

	xorb	%bh, %bh
	movb	%cs:_irq, %bl
	cmpb	$0, %bl
	je	2f

	xorb	%bh, %bh
	movb	%cs:_irq, %bl
	cmpb	$7, %bl
	jbe	0f
	subb	$8, %bl
	inb	$0xa1
	btsw	%bx, %ax
	jmp	1f
0:	inb	$0x21
	btrw	%bx, %ax
	outb	$0x21

1:	pushw	$0x0000			# set es to vector table segment
	popw	%es
	cmpb	$7, %bl
	jbe	0f
	addb	$(0x70 - 8 - 8), %bl
0:	addb	$8, %bl
	shlw	$2, %bx
	pushl	%cs:_oldisr
	popl	%es:(%bx)

2:	pop	%es
	pop	%ecx
	pop	%ebx
	pop	%eax
	leave
	ret	$0


# getpacket: get a waiting packet, if any
# returns:
#  ax is 1 if packet received
getpacket:
	enter	$0, $0
        pushl	%ebx

	mov	$0, %ax
	cmpb	$0, %cs:_irq
	je	1f
	cmpb	$0, %cs:_int
	ja	2f
	jmp	3f

1:	movw	$1, %cs:_undi_isr + 2	# PXENV_UNDI_ISR_IN_START
	push	$0x0014			# undi isr
	push	$_undi_isr
	call	api
	cmp	$0, %cs:_undi_isr + 2
	jne	3f			# not ours
2:	call	processpacket
	cmp	$1, %ax
	jne     3f
	mov	$0, %ax
	cmpw	$0xa288, %cs:receive + AoEprotocol
	jne	3f
	mov	%cs:receive + AoEtag, %ebx
	cmp	%cs:_tag, %ebx
	jne	3f
	mov	%cs:receive + AoEmajor, %bx
	xchg	%bh, %bl
	cmp	%cs:_major, %bx
	jne	3f
	mov	%cs:receive + AoEminor, %bl
	cmp	%cs:_minor, %bl
	jne	3f
	mov	$1, %ax

3:	popl	%ebx
	leave
	ret	$0


_in:	.word	0
# isr: checks if the interrupt is for us
isr:
	enter	$0, $0
	pushfl
	pushal
	push	%ds
	push	%es
	push	%fs
	push	%gs

	bts	$0, %cs:_in
	jnc	1f
	mov	$3, %ax
	int	$0x10
	print	"reentrant\n"
	halt
	mov	$0x20, %al
	cmp	$7, %cs:_irq
	jbe	0f
	outb	%al, $0xa0
0:	outb	%al, $0x20

	pop	%gs
	pop	%fs
	pop	%es
	pop	%ds
	popal
	popfl
	leave
	iret

1:	movw	$1, %cs:_undi_isr_start + 2	# PXENV_UNDI_ISR_IN_START
	push	$0x0014				# undi isr
	push	$_undi_isr_start
	call	api
	cmp	$0, %ax
	je	0f
	print	"PXENV_UNDI_ISR_IN_START failed\n"
	pushw	%cs:_undi_isr_start
	call	printword
	call	line
	halt
0:	cmp	$0, %cs:_undi_isr_start + 2
	je	0f				# ours
	mov	$3, %ax
	int	$0x10
	print	"ISR error!\n"
	halt
	pop	%gs
	pop	%fs
	pop	%es
	pop	%ds
	popal
	popfl
	leave
	ljmp	*%cs:_oldisr
0:	mov	$0x20, %al
	cmp	$7, %cs:_irq
	jbe	0f
	outb	%al, $0xa0
0:	outb	%al, $0x20
	movb	$1, %cs:_int
	movw	$0, %cs:_in
	pop	%gs
	pop	%fs
	pop	%es
	pop	%ds
	popal
	popfl
	leave
	iret

# processpacket: reads packet and checks packet, copy if ok
# returns:
#  ax = 1 if packet copied
processpacket:
	enter	$0, $0
	push	%bx
	push	%cx
	push	%ds
	push	%es
	push	%si
	push	%di

print "i"
	xorw	%bx, %bx
	movw	$2, %cs:_undi_isr + 2	# PXENV_UNDI_ISR_IN_PROCESS
	push	$0x0014			# undi isr
	push	$_undi_isr
	call	api
	cmp	$0, %ax
	je	0f
	print	"PXENV_UNDI_ISR_IN_PROCESS failed\n"
	pushw	%cs:_undi_isr
	call	printword
	call	line
	halt

0:	cmpw	$0, %cs:_undi_isr + 2	# done
	jne	0f
	print	"ISR started, but nothing to process?\n"
	jmp	9f

0:	cmpw	$4, %cs:_undi_isr + 2	# busy
	jne	0f
	print	"b"
	jmp	9f

0:	push	%cs
	pop	%es
	mov	$receive, %di
3:	cld
	cmpw	$2, %cs:_undi_isr + 2	# transmit
	je	1f

0:	cmpw	$3, %cs:_undi_isr + 2	# receive
	je	0f
	print	"Unknown function?\n"
	jmp	1f

0:
print "."
	mov	%cs:_undi_isr + 4, %cx
	mov	%cs:_undi_isr + 12, %ds
	mov	%cs:_undi_isr + 10, %si
	rep	movsb
	mov	$1, %bx

1:	movw	$3, %cs:_undi_isr + 2	# PXENV_UNDI_ISR_IN_GET_NEXT
	push	$0x0014			# undi isr
	push	$_undi_isr
	call	api
	cmp	$0, %ax
	je	0f
	print	"PXENV_UNDI_ISR_IN_GET_NEXT failed\n"
	pushw	%cs:_undi_isr
	call	printword
	call	line
	halt

0:	cmpw	$4, %cs:_undi_isr + 2	# busy
	je	9f
	cmpw	$0, %cs:_undi_isr + 2	# done
	je	9f
	jmp	3b

9:print "o"
	mov	%bx, %ax
	pop	%di
	pop	%si
	pop	%es
	pop	%ds
	pop	%cx
	pop	%bx
	leave
	ret	$0
