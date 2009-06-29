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

#ifdef DEBUG
#  define COUNT call count
#  define CHECK call ndebug
#else
#  define COUNT
#  define CHECK
#endif

_oldint13:
	.long	0

.globl int13init
int13init:
	enter	$0, $0
	push	%es
	pushw	$0
	pop	%es
	pushl	%es:(0x13 * 4)
	popl	%cs:_oldint13
	pushw	%cs
	popw	%es:((0x13 * 4) + 2)
	pushw	$int13
	popw	%es:(0x13 * 4)
	pop	%es
	leave
	ret	$0

int13:	enter	$0, $0
	pushfl
	pushal
	push	%ds
	push	%es
	push	%fs
	push	%gs
	COUNT
	CHECK

	cmpb	%dl, %cs:_drive		# for our drive?
	je	0f			# if so, jump
	pop	%gs
	pop	%fs
	pop	%es
	pop	%ds
	popal
	popfl
	pushf
	lcall	*%cs:_oldint13		# old int13 vector
	jmp	iret

0:	cmp	$0x00, %ah		# reset disk
	jne	0f
	jmp	x00
0:	cmp	$0x02, %ah		# read sectors
	jne	0f
	jmp	x02
0:	cmp	$0x03, %ah		# write sectors
	jne	0f
	jmp	x03
0:	cmp	$0x04, %ah		# verify sectors
	jne	0f
	jmp	x04
0:	cmp	$0x08, %ah		# get parameters
	jne	0f
	jmp	x08
0:	cmp	$0x15, %ah		# get disk type
	jne	0f
	jmp	x15
0:	cmp	$0x18, %ah		# set media type for format
	jne	0f
	jmp	x18
0:	cmp	$0x41, %ah		# extentions
	jne	0f
	jmp	x41
0:	cmp	$0x42, %ah		# extended read
	jne	0f
	jmp	x42
0:	cmp	$0x43, %ah		# extended write
	jne	0f
	jmp	x43
0:	cmp	$0x48, %ah		# extended get parameters
	jne	0f
	jmp	x48
0:	print	"Unknown int13 function (0x"
	shr	$8, %ax
	push	%ax
	call	printbyte
	print	")\n"
	halt

iret:	CHECK
	leave
	lret	$2			# iret, skip saved flags

# reset disk
# returns:
#  clear cf on success
x00:	pop	%gs
	pop	%fs
	pop	%es
	pop	%ds
	popal
	popfl
	clc
	jmp	iret

# read/write sectors
#  al: number of sectors to read/write (must be nonzero)
#  ch: low eight bits of cylinder number
#  cl: sector number 1-63 (bits 0-5)
#      high two bits of cylinder (bits 6-7)
#  dh: head number
#  dl: drive number
#  es:bx: data buffer
# returns:
#  ah = 0 and clear cf on success
x02:	pushw	$0			# read
	jmp	0f
x03:	pushw	$1			# write
0:	push	%es			# buffer high
	push	%bx			# buffer low
	mov	%eax, %ebx
	call	calculatelba		# get lba from registers (cx & dh)
	push	%eax			# lba
	xor	%bh, %bh
	push	%bx			# count
	call	processsectors
	pop	%gs
	pop	%fs
	pop	%es
	pop	%ds
	popal
	popfl
	mov	$0, %ah
	clc
	jmp	iret

# verify sectors
x04:	pop	%gs
	pop	%fs
	pop	%es
	pop	%ds
	popal
	popfl
	mov	$0, %ah
	clc
	jmp	iret

# get drive parameters
#  dl: drive number
# returns:
#  ah = 0 and clear cf on success
#  ch: low eight bits of maximum cylinder number
#  cl: maximum sector number (bits 5-0)
#      high two bits of maximum cylinder number (bits 7-6)
#  dh: maximum head number
#  dl: number of drives
x08:	pop	%gs
	pop	%fs
	pop	%es
	pop	%ds
	popal
	push	%eax
	mov	%cs:_cylinders, %eax
	cmp	$1023, %eax
	jb	0f
	mov	$1023, %ax
0:	mov	%al, %ch
	mov	%ah, %cl
	shl	$6, %cl
	or	%cs:_sectors, %cl
	mov	%cs:_heads, %dh
	dec	%dh
	mov	$1, %dl			# 1 drive
	cmpb	$0x81, %cs:_drive
	jne	0f
	add	$1, %dl			# another drive
0:	pop	%eax
	popfl
	mov	$0, %ah
	clc
	jmp	iret

# get disk type
# returns:
#  clear cf on success
#  ah: type (3 = harddisk)
#  cx:dx: number of sectors
x15:	pop	%gs
	pop	%fs
	pop	%es
	pop	%ds
	popal
	popfl
	mov	$3, %ah
	mov	%cs:_size + 2, %cx
	mov	%cs:_size, %dx
	clc
	jmp	iret

# set media type for format
# ch: lower 8 bits of highest cylinder number
# cl: sectors per track (bits 0-5)
#     high 2 bits of cylinder number (bit 6+7)
# returns:
#  clear cf on success
#  ah: status (1 = function not available)
#  es:di: pointer to parameter table
x18:	pop	%gs
	pop	%fs
	pop	%es
	pop	%ds
	popal
	popfl
	mov	$1, %ah
	stc
	jmp	iret

# extentions
# returns:
#  clear cf on success
#  ah: version (1)
#  bx: 0xaa55
#  cx: support map (0b001)
x41:	pop	%gs
	pop	%fs
	pop	%es
	pop	%ds
	popal
	popfl
	mov	$1, %ah
	mov	$0xaa55, %bx
	mov	$0b001, %cx
	clc
	jmp	iret

# extended read/write
#  for write: al: write flags (bit 0: verify)
#  ds:si: disk address packet
#   (byte)0: size of packet (10h or 18h)
#   (byte)1: reserved
#   (word)2: count
#   (long)4: buffer
#   (longlong)8: lba
# returns:
#  ah = 0 and clear cf on success
x42:	pushw	$0			# read
	jmp	0f
x43:	pushw	$1			# write
0:	cmp	$0x10, (%si)		# check disk packet size
	je	0f
	cmp	$0x18, (%si)		# check disk packet size
	je	0f
	print	"Invalid disk packet size...\n"
	halt
0:	pushl	4(%si)			# buffer
	pushl	8(%si)			# lba
	pushw	2(%si)			# count
	call	processsectors
	pop	%gs
	pop	%fs
	pop	%es
	pop	%ds
	popal
	popfl
	mov	$0, %ah
	clc
	jmp	iret

# extended get parameters
#  ds:si: drive parameters packet
#   (word)0: packet size
#   (word)2: information flags (3)
#   (long)4: cylinders
#   (long)8: heads
#   (long)12: sectors
#   (longlong)16: total size in sectors
#   (word)24: bytes per sector
# returns:
#  ah = 0 and clear cf on success
x48:	movw	$0x1a, 0(%si)
	movw	$3, 2(%si)

	pushl	%cs:_cylinders
	popl	4(%si)
	xor	%eax, %eax
	mov	%cs:_heads, %al
	mov	%eax, 8(%si)
	mov	%cs:_sectors, %al
	mov	%eax, 12(%si)
	pushl	%cs:_size
	popl	16(%si)
	movw	$512, 24(%si)
	pop	%gs
	pop	%fs
	pop	%es
	pop	%ds
	popal
	popfl
	mov	$0, %ah
	clc
	jmp	iret

# calculatelba: calculates an lba from chs
#  ch: low eight bits of cylinder number (0-1023)
#  cl: sector number (1-63) (bits 0-5)
#      high two bits of cylinder (0-1023) (bits 6-7)
#  dh: head number (0-(HEADS - 1))
# returns:
#  lba in eax
calculatelba:
	enter	$0, $0
	pushf
	push	%ebx
	push	%ecx
	push	%edx
	xor	%eax, %eax		# start with 0
	mov	%ch, %al		# low cylinder part in al
	mov	%cl, %ah		# high 2 bits of cylinderpart
	shr	$6, %ah			# shifted in ah (ax now has cylinder)
	xor	%bx, %bx
	mov	%cs:_heads, %bl		# heads
	mul	%bx			# cylinder * heads => dx:ax
	shl	$16, %edx		# add highpart of mulw to eax
	add	%edx, %eax		# eax now has cylinder * heads
	pop	%edx
	push	%edx
	shr	$8, %edx		# set edx to headnumber
	and	$0xff, %edx		# add headnumber to cylinder * heads
	add	%edx, %eax		# and multiply by sectors per track
	xor	%ebx, %ebx
	mov	%cs:_sectors, %bl	# to get sector count in edx:eax
	mul	%ebx			
	and	$0x3f, %ecx
	add	%ecx, %eax		# add the low 6 bits of cl to eax
	sub	$1, %eax		# substract by 1 (cl is 1 to 63)
	cmp	$0, %edx		# check to see if dx truly is 0
	je	0f
	print	"calculate lba error, edx & eax: "
	push	%edx			# if edx was not 0, then input
	call	printlong		# was somehow false, print and halt
	call	space
	push	%eax
	call	printlong
	call	line
	halt
0:	pop	%edx
	pop	%ecx
	pop	%ebx
	popf
	leave
	ret	$0
