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

.code16gcc

# _putchar: prints a character, returns character printed
.globl _putchar
_putchar:
	pushl	%ebp			# setup ebp and frame pointer
	movl	%esp, %ebp
	pushw	%bx			# clobbered by int 0x10

	movb	8(%bp), %al		# get character
	cmpb	$'\n', %al		# compare character
	jne	0f			# jump if it was not '\n'
	pushl	$'\r'			# push '\r' to get '\r\n'
	call	_putchar		# print char
	addw	$4, %sp			# clean up stack

0:	movb	$0x0e, %ah		# int 0x10, ah 0e: display char
	movb	8(%bp), %al		# get character
	xorw	%bx, %bx		# clear bx for int 0x10
	int	$0x10

#ifdef SERIAL
	movw	$0x00e3, %ax		# int 0x14, ah 00: reset port
	movw	$0x0000, %dx		# com1: 9600,8,n,1
	int	$0x14
	movb	$0x01, %ah		# int 0x14, ah 01: send byte
	movb	8(%bp), %al		# get character
	movw	$0x0000, %dx
	int	$0x14
#endif

	movzbl	8(%bp), %eax		# return character
	popw	%bx			# restore bx
	popl	%ebp			# restore ebp
	ret
#ifndef __MINGW32__
.type _putchar, @function
.size _putchar, .-_putchar
#endif

# _inb: returns a byte from a port
.globl _inb
_inb:
	pushl	%ebp			# setup ebp and frame pointer
	movl	%esp, %ebp
	movw	8(%bp), %dx		# get port
	xorl	%eax, %eax		# clear return value
	inb	%dx, %al		# get data from port
	popl	%ebp			# restore ebp
	ret
#ifndef __MINGW32__
.type _inb, @function
.size _inb, .-_inb
#endif

# _outb: outputs a byte to a port
.globl _outb
_outb:
	pushl	%ebp			# setup ebp and frame pointer
	movl	%esp, %ebp
	movb	8(%bp), %al		# get value
	movw	12(%bp), %dx		# get port
	outb	%al, %dx		# push data to port
	popl	%ebp			# restore ebp
	ret
#ifndef __MINGW32__
.type _outb, @function
.size _outb, .-_outb
#endif

# _getkey: returns key from keyboard or 0 if timeout expired
.globl _getkey
_getkey:
	pushl	%ebp			# setup ebp and frame pointer
	movl	%esp, %ebp
	pushfl				# save flags for interrupt flag
	pushl	%ebx			# save ebx
	sti			        # enable interrupts for int 0x1a
	xorb	%ah, %ah		# int 0x1a, ah 00: get system time
	int	$0x1a			# returns time in cx:dx
	shll	$16, %ecx		# shift high part left
	movw	%dx, %cx		# add low part
	addl	8(%bp), %edx		# add timeout value
	movl	%edx, %ebx		# store timeout time in ebx
0:	movb	$1, %ah			# int 0x16, ah 01: check for key
	int	$0x16			# ZF is set if there is
	jnz	0f			# no key available
	cmpl	$0, 8(%bp)		# if timeout is 0, loop
	je	0b			# till we get a key
	xorb	%ah, %ah		# int 0x1a, ah 00: get system time
	int	$0x1a			# get system time again
	shll	$16, %ecx		# shift high part left
	movw	%dx, %cx		# add low part
	cmpl	%ebx, %ecx		# compare with stored timeout time
	jb	0b			# loop if not yet reached
	xorb	%al, %al		# return 0 when timeout is reached
	jmp	1f
0:	xorb	%ah, %ah		# int 0x16, ah 00: get key
	int	$0x16			# get key in al
1:	movzbl	%al, %eax		# return key in eax
	popl	%ebx			# restore ebx
	popfl				# restore interrupt flag
	popl	%ebp			# restore ebp
	ret
#ifndef __MINGW32__
.type _getkey, @function
.size _getkey, .-_getkey
#endif

.globl _halt
_halt:
	sti				# hlt to idle the cpu, which
	hlt				# saves power on laptops and
	jmp	_halt			# is nice to cpu usage in vmware
#ifndef __MINGW32__
.type _halt, @function
.size _halt, .-_halt
#endif

# _segmemcpy: copy memory from segmented pointers src to dest
.globl _segmemcpy
_segmemcpy:
	pushl	%ebp			# setup ebp and frame pointer
	movl	%esp, %ebp
	pushl	%esi			# save used registers
	pushl	%edi
	pushw	%ds
	pushw	%es

	lesw	8(%bp), %di		# load es:di with dest
	xorl	%eax, %eax		# clear eax
	movw	%es, %ax		# get dest segment in ax
	shrl	$4, %eax		# multiply by 16
	andl	$0xffff, %edi		# clear highpart of edi
	addl	%edi, %eax		# add to get linear address

	ldsw	12(%bp), %si		# load ds:si with src
	xorl	%edx, %edx		# clear edx
	movw	%ds, %dx		# get src segment in dx
	shrl	$4, %edx		# multiply by 16
	andl	$0xffff, %esi		# clear highpart of esi
	addl	%esi, %edx		# add to get linear address

	movl	16(%bp), %ecx		# get size in ecx
	cld				# default to forward copy
	cmpl	%eax, %edx		# if dest address is lower then
	jg	0f			# src address, copy backwards
	std				# set backward flag
	leaw	-1(%esi,%ecx), %si	# add size-1 to si
	leaw	-1(%edi,%ecx), %di	# add size-1 to di
0:	rep	movsb			# do the actual copy
	movl	8(%bp), %eax		# return dest address
	popw	%es			# restore used registers
	popw	%ds
	popl	%edi
	popl	%esi
	popl	%ebp			# restore ebp
	ret
#ifndef __MINGW32__
.type _segmemcpy, @function
.size _segmemcpy, .-_segmemcpy
#endif

#ifndef MINIMAL
# _segmemset: fill memory pointed to by segmented pointer
.globl _segmemset
_segmemset:
	pushl	%ebp			# setup ebp and frame pointer
	movl	%esp, %ebp
	pushw	%si			# save used registers
	pushw	%ds
	lesw	8(%bp), %di		# load es:di with address
	movb	12(%bp), %al		# load al with fill byte
	movw	16(%bp), %cx		# load cx with count
	cld				# fill forward
	rep	stosb			# do the actual fill
	movl	8(%bp), %eax		# return memory address
	popw	%ds			# restore used registers
	popw	%si
	popl	%ebp			# restore ebp
	ret
#ifndef __MINGW32__
.type _segmemset, @function
.size _segmemset, .-_segmemset
#endif
#endif
