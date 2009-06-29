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
#ifndef MINIMAL
.text
.globl _debug
_debug:
	pushl	%ebp
	mov	%esp, %ebp
	pushfl
	pushl	%eax

	movl	$0x63733a00, %eax	# "cs:"
	call	_char4
	movw	%cs, %ax
	call	_hex4

	call	_space
	movl	$0x69703a00, %eax	# "ip:"
	call	_char4
	movw	4(%bp), %ax
	subw	$6, %ax
	call	_hex4

	call	_space
	movl	$0x64733a00, %eax	# "ds:"
	call	_char4
	movw	%ds, %ax
	call	_hex4

	call	_space
	movl	$0x6561783a, %eax	# "eax:"
	call	_char4
	movl	-8(%bp), %eax
	call	_hex8

	call	_space
	movl	$0x6562783a, %eax	# "ebx:"
	call	_char4
	movl	%ebx, %eax
	call	_hex8

	call	_space
	movl	$0x6563783a, %eax	# "ecx:"
	call	_char4
	movl	%ecx, %eax
	call	_hex8

	call	_space
	movl	$0x6564783a, %eax	# "edx:"
	call	_char4
	movl	%edx, %eax
	call	_hex8

	call	_line
	movl	$0x73733a00, %eax	# "ss:"
	call	_char4
	movw	%ss, %ax
	call	_hex4

	call	_space
	movl	$0x6562703a, %eax	# "ebp:"
	call	_char4
	movl	(%bp), %eax
	call	_hex8

	call	_space
	movl	$0x6573703a, %eax	# "esp:"
	call	_char4
	movl	%ebp, %eax
	addl	$8, %eax
	call	_hex8

	call	_space
	movl	$0x6573693a, %eax	# "esi:"
	call	_char4
	movl	%esi, %eax
	call	_hex8

	call	_space
	movl	$0x65733a00, %eax	# "es:"
	call	_char4
	movw	%es, %ax
	call	_hex4

	call	_space
	movl	$0x6564693a, %eax	# "edi:"
	call	_char4
	movl	%edi, %eax
	call	_hex8

	call	_line
	movl	$0x49442056, %eax	# "ID V"
	call	_char4
	movl	$0x49502056, %eax	# "IP V"
	call	_char4
	movl	$0x49462041, %eax	# "IF A"
	call	_char4
	movl	$0x4320564d, %eax	# "C VM"
	call	_char4
	movl	$0x20524620, %eax	# " RF "
	call	_char4
	movl	$0x4e542049, %eax	# "NT I"
	call	_char4
	movl	$0x4f504c20, %eax	# "OPL "
	call	_char4
	movl	$0x4f462044, %eax	# "OF D"
	call	_char4
	movl	$0x46204946, %eax	# "F IF"
	call	_char4
	movl	$0x20544620, %eax	# " TF "
	call	_char4
	movl	$0x5346205a, %eax	# "SF Z"
	call	_char4
	movl	$0x46204146, %eax	# "F AF"
	call	_char4
	movl	$0x20504620, %eax	# " PF "
	call	_char4
	movl	$0x43460000, %eax	# "CF"
	call	_char4

	call	_space
	call	_space
	call	_space
	movl	$0x66733a00, %eax	# "fs:"
	call	_char4
	movw	%fs, %ax
	call	_hex4

	call	_space
	movl	$0x67733a00, %eax	# "gs:"
	call	_char4
	movw	%gs, %ax
	call	_hex4

	call	_line
	movl	-4(%bp), %eax
	call	_space
	btl	$21, %eax		# ID
	call	_bit

	call	_space
	call	_space
	call	_space
	btl	$20, %eax		# VIP
	call	_bit

	call	_space
	call	_space
	call	_space
	btl	$19, %eax		# VIF
	call	_bit

	call	_space
	call	_space
	btl	$18, %eax		# AC
	call	_bit

	call	_space
	call	_space
	btl	$17, %eax		# VM
	call	_bit

	call	_space
	call	_space
	btl	$16, %eax		# RF
	call	_bit

	call	_space
	call	_space
	btl	$14, %eax		# NT
	call	_bit

	call	_space
	call	_space
	call	_space
	btl	$13, %eax		# IOPL
	call	_bit
	btl	$12, %eax		# IOPL
	call	_bit

	call	_space
	call	_space
	btl	$11, %eax		# DF
	call	_bit

	call	_space
	call	_space
	btl	$10, %eax		# OF
	call	_bit

	call	_space
	call	_space
	btl	$9, %eax		# IF
	call	_bit

	call	_space
	call	_space
	btl	$8, %eax		# TF
	call	_bit

	call	_space
	call	_space
	btl	$7, %eax		# SF
	call	_bit

	call	_space
	call	_space
	btl	$6, %eax		# ZF
	call	_bit

	call	_space
	call	_space
	btl	$4, %eax		# AF
	call	_bit

	call	_space
	call	_space
	btl	$2, %eax		# PF
	call	_bit

	call	_space
	call	_space
	btl	$0, %eax		# CF
	call	_bit

	call	_space
	call	_space
	call	_space
	movl	$0x45464c41, %eax	# "EFLA"
	call	_char4
	movl	$0x47533a00, %eax	# "GS:"
	call	_char4
	movl	-4(%bp), %eax
	call	_hex8
	call	_line

	subw	$6, %sp
	movl	$0x67647472, %eax	# "gdtr"
	call	_char4
	movb	$':', %al
	call	_char
	sgdt	-14(%bp)
	movw	-14(%bp), %ax
	call	_hex4
	movb	$':', %al
	call	_char
	movl	-12(%bp), %eax
	call	_hex8

	call	_space
	movl	$0x69647472, %eax	# "idtr"
	call	_char4
	movb	$':', %al
	call	_char
	sidt	-14(%bp)
	movw	-14(%bp), %ax
	call	_hex4
	movb	$':', %al
	call	_char
	movl	-12(%bp), %eax
	call	_hex8
	call	_line
	addw	$6, %sp

	popl	%eax
	popfl
	popl	%ebp
	ret

_hex8:	pushfl
	rorl	$16, %eax
	call	_hex4
	rorl	$16, %eax
	call	_hex4
	popfl
	ret

_hex4:	pushfl
	rorw	$8, %ax
	call	_hex2
	rorw	$8, %ax
	call	_hex2
	popfl
	ret

_hex2:	pushfl
	rorb	$4, %al
	call	_hex
	rorb	$4, %al
	call	_hex
	popfl
	ret

_hex:	pushfl
	pushw	%ax
	andb	$0xf, %al
	addb	$'0', %al
	cmpb	$'9', %al
	jbe	0f
	addb	$('a' - '9' - 1), %al
0:	call	_char
	popw	%ax
	popfl
	ret

_bit:	pushfl
	pushw	%ax
	movb	$'0', %al
	jnc	0f
	incb	%al
0:	call	_char
	popw	%ax
	popfl
	ret

_char4:	pushfl
	pushl	%eax
	roll	$8, %eax
	call	_char
	roll	$8, %eax
	testb	%al, %al
	jz	0f
	call	_char
	roll	$8, %eax
	testb	%al, %al
	jz	0f
	call	_char
	roll	$8, %eax
	testb	%al, %al
	jz	0f
	call	_char
0:	popl	%eax
	popfl
	ret

_space:	pushfl
	pushw	%ax
	movb	$' ', %al
	call	_char
	popw	%ax
	popfl
	ret

_line:	pushfl
	pushw	%ax
	movb	$'\n', %al
	call	_char
	popw	%ax
	popfl
	ret

_char:	pushfl
	pushal
	pushw	%ds
	pushw	%es
	pushw	%fs
	pushw	%gs
	pushl	%eax
	call	_putchar
	addl	$4, %esp
	popw	%gs
	popw	%fs
	popw	%es
	popw	%ds
	popal
	popfl
	ret
#ifndef __MINGW32__
.type _debug, @function
.size _debug, .-_debug
#endif
#endif
