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

# getticks: get ticks since midnight
# returns:
#  ticks in eax
.globl getticks
getticks:
	enter	$0, $0
	pushf
	push	%cx
	push	%dx
	xor	%ah, %ah
	int	$0x1a
	mov	%cx, %ax
	shl	$16, %eax
	mov	%dx, %ax
	pop	%dx
	pop	%cx
	popf
	leave
	ret	$0

# getkey: gets a key with timeout
#  (word)bp+4: timeout in ticks
# returns:
#  al: ascii value of key, 0 for timeout, change 13 (\r) to 10 (\n)
.globl getkey
getkey:
	enter	$0, $0
	pushf
	push	%ebx
	push	%eax
	xor	%ebx, %ebx
	mov	4(%bp), %bx
	call	getticks
	add	%eax, %ebx
0:	mov	$1, %ah
	int	$0x16
	jnz	0f
	call	getticks
	cmp	%ebx, %eax
	jb	0b
	mov	$0, %al
	jmp	1f
0:	mov	$0, %ah
	int	$0x16
	cmp	$'\r', %al
	jne	1f
	mov	$'\n', %al
1:	mov	%al, %bl
	pop	%eax
	mov	%bl, %al
	pop	%ebx
	popf
	leave
	ret	$2

# printchar: prints a char
#  (word)bp+4: char
.globl printchar
printchar:
	enter	$0, $0
	pushf
	pushw	%ax
	pushw	%bx
	pushw	%dx
	movb	4(%bp), %al
	cmpb	$'\n', %al
	jne	0f
	pushw	$'\r'
	call	printchar
0:	movb	$0x0e, %ah
	movb	4(%bp), %al
	xorw	%bx, %bx
	int	$0x10
#ifdef SERIAL
	pushfl
	pushal
//	movw	$0x00e3, %ax
//	movw	$0x0000, %dx
//	int	$0x14
//	movb	$0x01, %ah
//	movb	4(%bp), %al
//	movw	$0x0000, %dx
//	int	$0x14
	popal
	popfl
#endif
	pop	%dx
	pop	%bx
	pop	%ax
	popf
	leave
	ret	$2

.globl line, space, point, d0, d1, d2, d3, d4, d5, d6, d7, d8, d9
# quick chars
line:	push	$'\n'
	jmp	0f
space:	push	$' '
	jmp	0f
point:	push	$'.'
	jmp	0f
d0:	pushw	$'0'
	jmp	0f
d1:	pushw	$'1'
	jmp	0f
d2:	pushw	$'2'
	jmp	0f
d3:	pushw	$'3'
	jmp	0f
d4:	pushw	$'4'
	jmp	0f
d5:	pushw	$'5'
	jmp	0f
d6:	pushw	$'6'
	jmp	0f
d7:	pushw	$'7'
	jmp	0f
d8:	pushw	$'8'
	jmp	0f
d9:	pushw	$'9'
0:	call	printchar
	ret

# printnumber: prints a decimal number
#  (word)bp+4: number
.globl printnumber
printnumber:
	enter	$0, $0
	pushf
	push	%ax
	push	%bx
	push	%cx
	push	%dx
	mov	4(%bp), %ax
	xor	%cx, %cx
	mov	$10, %bx
0:	xor	%dx, %dx
	div	%bx
	add	$0x30, %dx
	push	%dx
	inc	%cx
	cmp	$0, %ax
	jne	0b
0:	call	printchar
	dec	%cx
	jnz	0b
	pop	%dx
	pop	%cx
	pop	%bx
	pop	%ax
	popf
	leave
	ret	$2

# printlongnumber: prints a long decimal number
#  (long)bp+4: number
.globl printlongnumber
printlongnumber:
	enter	$0, $0
	pushf
	push	%eax
	push	%ebx
	push	%cx
	push	%edx
	mov	4(%bp), %eax
	xor	%cx, %cx
	mov	$10, %ebx
0:	xor	%edx, %edx
	div	%ebx
	add	$0x30, %dx
	push	%dx
	inc	%cx
	cmp	$0, %eax
	jne	0b
0:	call	printchar
	dec	%cx
	jnz	0b
	pop	%edx
	pop	%cx
	pop	%ebx
	pop	%eax
	popf
	leave
	ret	$4

# printbyte: prints a byte
#  (word)bp+4: byte
.globl printbyte
printbyte:
	enter	$0, $0
	pushf
	push	%ax
	mov	4(%bp), %ax
	shr	$4, %al
	add	$0x30, %al
	cmp	$0x3a, %al
	jl	0f
	add	$39, %al
0:	push	%ax
	call	printchar
	mov	4(%bp), %ax
	and	$0x0f, %al
	add	$0x30, %al
	cmp	$0x3a, %al
	jl	1f
	add	$39, %al
1:	push	%ax
	call	printchar
	pop	%ax
	popf
	leave
	ret	$2

# printword: prints a word
#  (word)bp+4: word
.globl printword
printword:
	enter	$0, $0
	pushf
	push	%ax
	mov	4(%bp), %ax
	shr	$8, %ax
	push	%ax
	call	printbyte
	push	4(%bp)
	call	printbyte
	pop	%ax
	popf
	leave
	ret	$2

# printlong: prints a long
#  (long)bp+4: long
.globl printlong
printlong:
	enter	$0, $0
	pushf
	push	%eax
	mov	4(%bp), %eax
	shr	$16, %eax
	push	%ax
	call	printword
	push	4(%bp)
	call	printword
	pop	%eax
	popf
	leave
	ret	$4


# printbit: prints "1" if cf flag is set, else print "0"
.globl printbit
printbit:
	enter	$0, $0
	pushf
	jc	0f
	pushw	$'0'
	call	printchar
	jmp	1f
0:	pushw	$'1'
	call	printchar
1:	popf
	leave
	ret	$0

# printbits: prints a word in bits
#  (word)bp+4: word
.globl printbits
printbits:
	enter	$0, $0
	pushf
	push	%ax
	push	%cx
	mov	4(%bp), %ax
	mov	$16, %cx
0:	shl	$1, %ax
	call	printbit
	loop	0b
	pop	%cx
	pop	%ax
	popf
	leave
	ret	$2

# printline: prints a '\0' terminated string, located right behind the call
# return to the instruction behind the '\0'
.globl printline
printline:
	enter	$0, $0
	pushf
	push	%ax
	push	%ds
	push	%si
	push	%cs
	pop	%ds	
0:	mov	2(%bp), %si		# get character address
	lodsb				# load char string
	mov	%si, 2(%bp)		# store back new return value
	cmp	$0, %al			# until we reach a '\0'
	jz	0f
	push	%ax
	call	printchar
	jmp	0b
0:	pop	%si
	pop	%ds
	pop	%ax
	popf
	leave
	ret	$0

# printbuffer: prints a buffer
#  (long)bp+6: buffer
#  (word)bp+4: length
.globl printbuffer
printbuffer:
	enter	$0, $0
	pushf
	push	%ax
	push	%bx
	push	%cx
	push	%ds
	push	%si

	cld
	lds	6(%bp), %si
	xor	%bx, %bx
2:	cmpw	$0, 4(%bp)
	je	2f
	push	%ds
	call	printword
	push	$':'
	call	printchar
	push	%si
	call	printword
	call	space

	mov	4(%bp), %cx
	cmp	$0x10, %cx
	jb	0f
	mov	$0x10, %cx

0:	sub	%cx, 4(%bp)
	push	%cx
0:	call	space
	lodsb
	push	%ax
	call	printbyte
	loop	0b
	pop	%ax

	push	%ax
	mov	$0x11, %cx
	sub	%ax, %cx
0:	print	"   "
	loop	0b
	pop	%cx

	sub	%cx, %si
1:	lodsb
	cmp	$0x20, %al
	ja	0f
	mov	$'.', %al
0:	push	%ax
	call	printchar
	loop	1b
	call	line
	inc	%bx
	cmp	$20, %bx
	jb	0f
	print	"Press a key\n"
1:	push	$0
	call	getkey
	cmp	$0, %al
	je	1b
	xor	%bx, %bx
0:	jmp	2b

2:	pop	%si	
	pop	%ds
	pop	%cx
	pop	%bx
	pop	%ax
	popf
	leave
	ret	$6

.globl haltcpu
haltcpu:
	sti			# hlt to idle the cpu, which
	hlt			# saves power on laptops and
	jmp	haltcpu		# is nice to cpu usage in vmware
