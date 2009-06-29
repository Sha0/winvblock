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

.globl _debug_srcaddr, _debug_undi_transmit_packet, _debug_undi_TBD

_debug:
_debug_dstaddr:		.org	.+6, 0xff
_debug_srcaddr:		.org	.+6, 0
_debug_protocol:	.word	0x0180
_debug_count:		.long	0
_debug_eflags:		.long	0
_debug_ip:		.word	0
_debug_cs:		.word	0
_debug_ds:		.word	0
_debug_es:		.word	0
_debug_fs:		.word	0
_debug_gs:		.word	0
_debug_ss:		.word	0
_debug_ebp:		.long	0
_debug_esp:		.long	0
_debug_eax:		.long	0
_debug_ebx:		.long	0
_debug_ecx:		.long	0
_debug_edx:		.long	0
_debug_esi:		.long	0
_debug_edi:		.long	0
_debug_end:

_debug_undi_transmit_packet:		# 0x0008
	.word	0			# PXENV_STATUS Status
	.byte	0			# UINT8 Protocol
	.byte	1			# UINT8 XmitFlag
	.long	_debug_dstaddr		# SEGOFF16 DestAddr
	.long	_debug_undi_TBD		# SEGOFF16 TBD
	.org	.+(2 * 4), 0		# UINT32 Reserved[2]

_debug_undi_TBD:
	.word	(_debug_end - _debug)	# UINT16 ImmedLength
	.long	_debug			# SEGOFF16 Xmit
	.word	0			# UINT16 DataBlkCount
	.org	.+(8 * 8), 0		# DataBlk DataBlock[8]

_count:	.long	0

# sets exception vectors
.globl debuginit
debuginit:
	enter	$0, $0
	pushf
	push	%es
	push	$0
	pop	%es
	pushw	%cs
	popw	%es:((0x3 * 4) + 2)
	pushw	$int3
	popw	%es:(0x3 * 4)
	pop	%es
	popf
	leave
	ret	$0

# int3: break (prints cpu state and halts)
int3:
	call	debug
	jmp	.

# count: inceases _count, break when BREAK reached
.globl count
count:
	enter	$0, $0
	pushf
	push	%eax
	incl	%cs:_count
	mov	$BREAK, %eax
	cmpl	$0, %eax
	pop	%eax
	je	0f
	cmpl	$BREAK, %cs:_count
	jb	0f
	print	"Break on: "
	push	$BREAK
	call	printword
	call	line
	popf
	call	debug
	halt
0:	popf
	leave
	ret	$0

# debug: prints cpu state
.globl debug
debug:
	enter	$0, $0
	pushfl
	pushal
	print	"count:"
	pushl	%cs:_count
	call	printlong
	print	"  cs:"
	pushw	%cs
	call	printword
	print	" ip:"
	pushw	2(%bp)
	call	printword
	print	" ss:"
	pushw	%ss
	call	printword
	print	" ebp:"
	mov	%ebp, %eax
	mov	0(%bp), %ax
	pushl	%eax
	call	printlong
	print	" esp:"
	mov	%esp, %eax
	mov	%bp, %ax
	add	$4, %ax
	pushl	%eax
	call	printlong
	call	line

	print	"eax:"
	pushl	-8(%bp)
	call	printlong
	print	" ebx:"
	pushl	-20(%bp)
	call	printlong
	print	" ecx:"
	pushl	-12(%bp)
	call	printlong
	print	" edx:"
	pushl	-16(%bp)
	call	printlong
	call	line

	print	"ds:"
	pushw	%ds
	call	printword
	print	" esi:"
	pushl	-32(%bp)
	call	printlong
	print	" es:"
	pushw	%es
	call	printword
	print	" edi:"
	pushl	-36(%bp)
	call	printlong
	print	" fs:"
	pushw	%fs
	call	printword
	print	" gs:"
	pushw	%gs
	call	printword
	call	line

	mov	-4(%bp), %eax
	print	"ID VIP VIF AC VM RF NT IOPL OF DF IF TF SF ZF AF PF CF   EFLAGS\n"
	print	" "
	bt	$21, %eax
	call	printbit
	print	"   "
	bt	$20, %eax
	call	printbit
	print	"   "
	bt	$19, %eax
	call	printbit
	print	"  "
	bt	$18, %eax
	call	printbit
	print	"  "
	bt	$17, %eax
	call	printbit
	print	"  "
	bt	$16, %eax
	call	printbit
	print	"  "
	bt	$14, %eax
	call	printbit
	print	"   "
	bt	$13, %eax
	call	printbit
	bt	$12, %eax
	call	printbit
	print	"  "
	bt	$11, %eax
	call	printbit
	print	"  "
	bt	$10, %eax
	call	printbit
	print	"  "
	bt	$9, %eax
	call	printbit
	print	"  "
	bt	$8, %eax
	call	printbit
	print	"  "
	bt	$7, %eax
	call	printbit
	print	"  "
	bt	$6, %eax
	call	printbit
	print	"  "
	bt	$4, %eax
	call	printbit
	print	"  "
	bt	$2, %eax
	call	printbit
	print	"  "
	bt	$0, %eax
	call	printbit
	print	"  "
	push	%eax
	call	printlong
	call	line

	popal
	popfl
	leave
	ret	$0

# ndebug: sends cpu state to netword
.globl ndebug
ndebug:
	enter	$0, $0
	pushfl
	pushal

	pushl	%cs:_count
	popl	%cs:_debug_count
	mov	%cs, %cs:_debug_cs
	pushw	2(%bp)
	popw	%cs:_debug_ip
	mov	%ss, %cs:_debug_ss
	mov	%ebp, %eax
	mov	0(%bp), %ax
	mov	%eax, %cs:_debug_ebp
	mov	%esp, %eax
	mov	%bp, %ax
	add	$4, %ax
	mov	%eax, %cs:_debug_esp
	pushl	-8(%bp)
	popl	%cs:_debug_eax
	pushl	-20(%bp)
	popl	%cs:_debug_ebx
	pushl	-12(%bp)
	popl	%cs:_debug_ecx
	pushl	-16(%bp)
	popl	%cs:_debug_edx
	mov	%ds, %cs:_debug_ds
	pushl	-32(%bp)
	popl	%cs:_debug_esi
	mov	%es, %cs:_debug_es
	pushl	-36(%bp)
	popl	%cs:_debug_edi
	mov	%fs, %cs:_debug_fs
	mov	%gs, %cs:_debug_gs
	pushl	-4(%bp)
	popl	%cs:_debug_eflags

	push	$0x0008
	push	$_debug_undi_transmit_packet
	call	api

	popal
	popfl
	leave
	ret	$0

# step: turns on step debugging
.globl step
step:
	enter	$0, $0
	push	%eax
	push	%es

	xor	%ax, %ax
	mov	%ax, %es
	mov	%cs, %ax		# get segment
	shl	$16, %eax		# offset by 16
	mov	$_step, %ax		# get offset of step function
	mov	%eax, %es:(4 * 0x1)	# save new int1 (step) vector
	pushf				# push eflags
	pop	%ax			# get flags in ax
	or	$0x0100, %ax		# set the Trap Flag bit
	push	%ax			# push the new eflags back
	popf				# pop new eflags

	pop	%es
	pop	%eax
	leave
	ret	$0

_step:	enter	$0, $0
//	push	%eax
//	push	%ebx
//	push	%ecx
//	push	%si
//	push	%ds
	push	%es

//	lds	2(%bp), %si
//	cld
//	lodsb
//	cmp	$0x66, %al
//	jne	0f
//	lodsb
//0:	cmp	$0x9d, %al		# next is popf or popfl
//	jne	0f
//	orw	$0x0100, 8(%bp)		# keep TR on (popped flags)
pushf
0:	orw	$0x0100, 6(%bp)		# keep TR on (own flags)
popf
//	mov	2(%bp), %eax

	pushw	$0xb800
	pop	%es
//	xor	%ebx, %ebx
//1:	mov	%eax, %ecx
//	shr	$28, %ecx
//	add	$'0', %cl
//	cmp	$'9', %cl
//	jbe	0f
//	add	$('a' - '9' - 1), %cl
//0:	mov	$0x07, %ch
//	mov	%cx, %es:142(,%ebx,2)
//	inc	%ebx
//	cmp	$4, %ebx
//	jne	0f
//	movw	$0x073a, %es:142(,%ebx,2)
//	inc	%ebx
//0:	shl	$4, %eax
//	cmp	$9, %ebx
//	jne	1b

	pop	%es
//	pop	%ds
//	pop	%si
//	pop	%ecx
//	pop	%ebx
//	pop	%eax
	leave
	iret
