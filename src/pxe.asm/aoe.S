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

start:
	pushfl				# save eflags
	pushw	%ax			# save ax
	call	0f			# pushl ip
0:	popw	%ax			# popl ip
	subw	$0b, %ax		# offset by 0: to get _start adress
	cmpw	$0x7c00, %ax		# did we start from 0x7c00?
	je	0f			# if not, print error
	popw	%ax			# restore ax (cosmetic)
	popfl				# restore flags (cosmetic)
	print	"\nip is not 0x7c00, can not continue...\n"
	halt
0:	movw	%cs, %ax		# get %cs
	cmpw	$0x0000, %ax		# is %cs 0x0000?
	je	0f			# if not, print error
	popw	%ax			# restore ax (cosmetic)
	popfl				# restore flags (cosmetic)
	print	"\ncs is not 0x0000, can not continue...\n"
	halt
0:	popw	%ax			# restore ax
	popfl				# restore eflags
	ljmp	$0x07c0, $0f		# realign cs:eip to let start be 0

0:	movw	$0, %bp			# use bp to set ss
	movw	%bp, %ss		# setup new stack
	movw	$0x7c00, %sp		# setup sp to below boot sector
	movw	$0x7c00, %bp		# setup bp to the same

	pushfl				# everything should have its initial
	pushal				# values, so push it all on the stack
	pushw	%ds
	pushw	%es
	pushw	%fs
	pushw	%gs

	int	$0x12			# get memory size in kb in ax
	shlw	$6, %ax			# change from kb to paragraphs
	movw	$_pxesize, %bx		# get pxe size
	addw	$0x0f, %bx		# add 15 to round up on paragraph
	shrw	$4, %bx			# change to paragraphs
	subw	%bx, %ax		# calculate target segment
	andw	$0xffc0, %ax		# round down to kb boundry
	movw	%ax, %es		# set es as target segment
	shrw	$6, %ax			# convert to kb for new free mem
	pushw	$0x0040			# bios area
	popw	%ds
	movw	%ax, %ds:0x0013		# store in bios area at 0040:0013
	pushw	%cs			# read from segment 07c0
	popw	%ds
	xorw	%si, %si		# zero si
	xorw	%di, %di		# zero di
	movw	$_pxesize, %cx		# get size
	cld				# positive direction
	rep	movsb			# copy ourself to target segment
	pushw	%es			# setup segment for lret
	pushw	$0f			# setup offset for lret
	lret				# long return into target segment

0:	print	"\nWelcome to AoE Boot...\n\n"

#ifdef DEBUG
	call	debuginit		# init debug vectors (debug.S)
	pushw	$0x0000			# set es ready for access to the bios
	popw	%es			# area and loading the boot sector
	movw	$0x7c00, %di
	movw	$(0xffff - 0x7c00), %cx
	movb	$0xcc, %al		# int3 (break)
	cld
	rep	stosb			# fill 0000:7c00-0000:ffff with breaks
#endif

	call	pxeinit			# init pxe api and get rootpath (pxe.S)
//movb	$0, %cs:_irq		# always use polling
	call	printaoeparameters	# print aoe parameters
	call	getdiskparameters	# get disk config (pxe.S)
	call	printdiskparameters	# print disk parameters
	call	int13init		# setup int13 handler (int13.S)

	pushw	$0x0000			# set es ready for access to the bios
	popw	%es			# area and loading the boot sector
1:	print	"\nBoot from (N)etwork, (H)arddisk or (F)loppy?\n"
	pushw	$TIMEOUT
	call	getkey
	cmpb	$0, %al
	je	1f			# default to network boot
	cmpb	$'Z', %al		# make input upcase
	jna	0f
	subb	$('a' - 'A'), %al
0:	cmpb	$'N', %al
	je	1f
	cmpb	$'H', %al
	je	2f
	cmpb	$'F', %al
	je	3f
	jmp	1b			# input error

1:	movb	$0x80, %cs:_drive	# aoe drive is 1st harddisk
	movw	$aBFT, %bx		# calculate aBFT segment boundry
	subw	%bx, 1			# (x-1) & 0xfff0 + 0x10
	andw	$0xfff0, %bx
	addw	$0x10, %bx

	movl	$0x54464261, %cs:(%bx)	# setup aBFT
	movl	$(slack - aBFT), %cs:(length - aBFT)(%bx)
	movb	$1, %cs:(revision - aBFT)(%bx)
	movl	%cs:_clientmac, %eax	# client mac (high 4)
	movl	%eax, %cs:(clientmac - aBFT)(%bx)
	movw	%cs:_clientmac + 4, %ax	# client mac (low 2)
	movw	%ax, %cs:(clientmac - aBFT + 4)(%bx)
	movw	%cs:_major, %ax		# major
	movw	%ax, %cs:(major - aBFT)(%bx)
	movb	%cs:_minor, %al		# minor
	movb	%al, %cs:(minor - aBFT)(%bx)

	movw	$(slack - aBFT), %cx	# length
	pushw	%bx			# keep safe
	xorb	%al, %al		# zero checksum
0:	subb	%cs:(%bx), %al
	incw	%bx			# by counting through %bx
	decw	%cx			# %cx times...
	jnz	0b
	popw	%bx			# get aBFT address and store checksum
	movb	%al, %cs:(checksum - aBFT)(%bx)

	movb	$1, %es:0x0475		# 1 harddisk
	movb	$0x80, %dl		# boot from harddisk
	jmp	0f
2:	movb	$0x81, %cs:_drive	# aoe drive is 2nd harddisk
	movb	$2, %es:0x0475		# 2 harddisks
	movb	$0x80, %dl		# boot from harddisk
	jmp	0f
3:	movb	$0x80, %cs:_drive	# aoe drive is 1st harddisk
	movb	$1, %es:0x0475		# 1 harddisk
	movb	$0x00, %dl		# boot from floppy

0:	movb	$0x02, %ah		# load sector 0 in 0:7c00
	movb	$1, %al
	movb	$0, %ch
	movb	$1, %cl
	movb	$0, %dh
	pushw	$0x0000
	popw	%es
	movw	$0x7c00, %bx
	int	$0x13
	jnc	0f
	print	"Hardware boot failure\n"
	halt

0:	popw	%gs			# pop everything to revert
	popw	%fs			# to starting state
	popw	%es
	popw	%ds
	popal
	popfl

	movb	$0x80, %dl		# for ReactOS freeloader
	ljmp	$0x0000, $0x7c00	# long jump to bootsector


# printaoeparameters: print rootpath AoE setting
printaoeparameters:
	enter	$0, $0
	pushw	%bx

	print	"Boot from: e"
	pushw	%cs:_major
	call	printnumber
	print	"."
	pushw	%cs:_minor
	call	printnumber

	xorw	%bx, %bx
	print	"  Client Address: "
0:	pushw	%cs:_clientmac(%bx)
	call	printbyte
	pushw	$':'
	call	printchar
	incw	%bx
	cmpw	$5, %bx
	jb	0b
	pushw	%cs:_clientmac(%bx)
	call	printbyte

	print	"  Irq: "
	pushw	%cs:_irq
	call	printnumber
	cmpb	$0, %cs:_irq
	jne	0f
	print	" (polling)"

0:	call	line
	popw	%bx
	leave
	ret	$0

# printdiskparameters: prints disk parameters
printdiskparameters:
	enter	$0, $0
	pushl	%eax
	pushw	%bx

	xorw	%bx, %bx
	print	"Server Address: "
0:	pushw	%cs:_servermac(%bx)
	call	printbyte
	pushw	$':'
	call	printchar
	incw	%bx
	cmpw	$5, %bx
	jb	0b
	pushw	%cs:_servermac(%bx)
	call	printbyte
	call	line

	movl	%cs:_size, %eax
	shrl	$11, %eax
	print	"Disk Size: "
	pushl	%eax
	call	printlongnumber
	print	"M Cylinders: "
	pushl	%cs:_cylinders
	call	printlongnumber
	print	" Heads: "
	xorb	%ah, %ah
	movb	%cs:_heads, %al
	pushw	%ax
	call	printnumber
	print	" Sectors: "
	movb	%cs:_sectors, %al
	pushw	%ax
	call	printnumber
	call	line

	popw	%bx
	popl	%eax
	leave
	ret	$0

aBFT:
signature:
	.org	.+4, 0
length:
	.org	.+4, 0
revision:
	.org	.+1, 0
checksum:
	.org	.+1, 0
oemid:
	.org	.+6, 0
oemtableid:
	.org	.+8, 0
reserved1:
	.org	.+12, 0
major:
	.org	.+2, 0
minor:
	.org	.+1, 0
reserved2:
	.org	.+1, 0
clientmac:
	.org	.+6, 0
slack:
	.org	.+15, 0
