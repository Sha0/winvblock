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

//#define STACKSIZE 8192
//#define STACKSIZE 16384
#define STACKSIZE 32768
//#define STACKSIZE 40960
//#define STACKSIZE 49152
#define print call _printline; .asciz
#define halt jmp _halt
#define debug call _debug

.bss
.globl	_segment, _oldnmi, _oldint13, _gotisr, _irq, _timer
_segment:
	.long	0
_oldnmi:
	.long	0
_oldint13:
	.long	0
_gotisr:
	.long	0
_irq:
	.long	0
_timer:
	.long	0

#ifndef __MINGW32__
.type _segment, @object
.type _oldnmi, @object
.type _oldint13, @object
.type _gotisr, @object
.type _irq, @object
.type _timer, @object
.type _idtr, @object
.size _segment, 4
.size _oldnmi, 4
.size _oldint13, 4
.size _gotisr, 4
.size _irq, 4
.size _timer, 4
.size _idtr, 6
#endif

_ss1:	.word	0
_esp1:	.long	0
_ebp1:	.long	0
_ss2:	.word	0
_esp2:	.long	0
_ebp2:	.long	0
_ss3:	.word	0
_esp3:	.long	0
_ebp3:	.long	0
_ss4:	.word	0
_esp4:	.long	0
_ebp4:	.long	0
_switchcount:
	.word	0
#ifndef __MINGW32__
.type _ss1, @object
.type _ebp1, @object
.type _esp1, @object
.type _ss2, @object
.type _ebp2, @object
.type _esp2, @object
.type _ss3, @object
.type _ebp3, @object
.type _esp3, @object
.type _ss4, @object
.type _ebp4, @object
.type _esp4, @object
.size _switchcount, 2
.size _ss1, 2
.size _ebp1, 4
.size _esp1, 4
.size _ss2, 2
.size _ebp2, 4
.size _esp2, 4
.size _ss3, 2
.size _ebp3, 4
.size _esp3, 4
.size _ss4, 2
.size _ebp4, 4
.size _esp4, 4
.size _switchcount, 2
#endif


.text
.globl _start
_start:
	pushfl				# save eflags
	pushl	%eax			# save eax
	call	0f			# pushl eip
0:	popl	%eax			# popl eip
	subw	$0b, %ax		# offset by 0: to get _start adress
	cmpw	$0x7c00, %ax		# did we start from 0x7c00?
	je	0f			# if not, print error
	print	"\nip is not 0x7c00, can not continue...\n"
	halt
0:	movw	%cs, %ax		# get %cs
	cmpw	$0x0000, %ax		# is %cs 0x0000?
	je	0f			# if not, print error
	print	"\ncs is not 0x0000, can not continue...\n"
	halt
0:	popl	%eax			# restore eax
	popfl				# restore eflags
	jmp	$0x07c0, $0f		# realign cs:eip to let _start be 0

0:	pushl	$0x00007c00		# set stack to below the boot code
	movw	%sp, %bp		# use bp for lss (lss can't use sp)
	lssw	(%bp), %sp		# setup new stack in one call
	movw	%sp, %bp		# set correct bp

	pushfl     			# push flags and used registers
	pushal
	pushw	%ds
	pushw	%es

	int	$0x12			# get memory size in kb
	shlw	$6, %ax			# change to paragraphs
	movw	$(_end + STACKSIZE + 15), %cx	# get total size
	shrw	$4, %cx			# change to round up paragraphs
	subw	%cx, %ax		# calculate target segment
	andw	$0xffc0, %ax		# round down to kb boundry
	movw	%ax, %cs:_segment	# store target segment
	shrw	$6, %ax			# convert to kb
	pushw	$0x0040			# select bios area segment
	popw	%es			# to write free memory
	movw	%ax, %es:0x0013		# at 0040:0013

	pushw	%cs			# set ds as cs to copy
	popw	%ds			# the current code segment
	movw	%cs:_segment, %es	# point to target segment
	xorw	%si, %si		# zero si
	xorw	%di, %di		# zero di
	movw	$_end, %cx		# get size in bytes
	cld				# copy forward
	rep	movsb			# into target segment

	popw	%es			# pop eflags and used registers
	popw	%ds
	popal
	popfl
	pushw	$0			# push 0 for lret ahead
	pushw	%cs:_segment		# push target segment
	pushl	$0f			# push offset in target segment
	lret				# long return into target segment

0:	cli
	call	_switchstack		# switch stack
	call	_pushcpu		# push cpu struct
	movzwl	%sp, %eax		# sp points to the pushed struct
	pushl	%eax			# push pointer for call to main
	movw	%cs:_segment, %ds	# set correct ds
	movw	%cs:_segment, %es	# set correct es
	call	__main			# jump to main
	addw	$4, %sp			# clean pointer from stack
	call	_popcpu			# pop cpu
	call	_restorestack		# restore external stack
	int	$0x13			# read disk sector setup by main
	jmp	$0x0, $0x7c00		# jump to bootsector
#ifndef __MINGW32__
.type _start, @function
.size _start, .-_start
#endif

# printline: prints a '\0' terminated string, located right behind the call
# return to the instruction behind the '\0'
_printline:
	pushl	%ebp			# setup ebp and frame pointer
	movl	%esp, %ebp
	pushfl				# save flags and registers
	pushal
	pushw	%ds
	pushw	%es
	pushw	%fs
	pushw	%gs

	pushw	%cs			# set ds as cs
	popw	%ds
	xorl	%eax, %eax		# clean up eax
	cld				# read forward
0:	movw	4(%bp), %si		# get character address
	lodsb				# load char string
	movw	%si, 4(%bp)		# store back new return value
	testb	%al, %al		# jump if we reach a '\0'
	jz	0f
	pushl	%eax			# push char
	call	_putchar		# print char
	addw	$4, %sp			# clean up stack
	jmp	0b

0:	popw	%gs			# restore registers and flags
	popw	%fs
	popw	%es
	popw	%ds
	popal
	popfl
	popl	%ebp			# restore ebp
	ret
#ifndef __MINGW32__
.type _printline, @function
.size _printline, .-_printline
#endif

.globl _int8
_int8:
	pushw	%ax
	incl	%cs:_timer
	mov	$0x20, %al
	outb	%al, $0x20
	popw	%ax
.code16
	iret
.code16gcc
#ifndef __MINGW32__
.type _int8, @function
.size _int8, .-_int8
#endif

.globl _isr
_isr:
	pushfl
	pushal
	pushw	%ds
	pushw	%es
	pushw	%fs
	pushw	%gs
	pushw	%bp
	sub	$16, %sp
	movw	%sp, %bp
	movl	$0x10000, 0(%bp)
	movl	$0, 4(%bp)
	movl	$0, 8(%bp)
	movl	$0, 12(%bp)
	pushw	%ss
	pushw	%bp
	pushw	$0x0014			# PXENV_UNDI_ISR
.code16
	lcall	*%cs:_apivector
.code16gcc
	addw	$6, %sp
	testw	%ax, %ax
	jnz	1f
	cmpw	$1, 2(%bp)
	jne	0f
	print	"\n\nISR Not ours...\n"
	halt
0:	mov	$0x20, %al
	cmp	$7, %cs:_irq
	jbe	0f
	outb	%al, $0xa0
0:	outb	%al, $0x20
	add	$16, %sp
	popw	%bp
	popw	%gs
	popw	%fs
	popw	%es
	popw	%ds
	popal
	popfl
	movl	$1, %cs:_gotisr
.code16
	iret
.code16gcc
1:	print	"\n\nPXENV_UNDI_ISR: "
	movb	1(%bp), %bl
	movb	%bl, %bh
	shrb	$4, %bh
	rorl	$16, %ebx
	movb	0(%bp), %bl
	movb	%bl, %bh
	shrb	$4, %bh
	andl	$0x0f0f0f0f, %ebx
	mov	$4, %cx
0:	roll	$8, %ebx
	addb	$0x30, %bl
	cmpb	$0x3a, %bl
	jb	1f
	addb	$0x27, %bl
1:	pushl	%ebx
	call	_putchar
	add	$4, %sp
	decw	%cx
	jnz	0b
	print	"\n"
	halt
#ifndef __MINGW32__
.type _isr, @function
.size _isr, .-_isr
#endif

.globl _int13
_int13:
	cli
	pushfl
	cmpb	$0x80, %dl
	jne	1f
	cmpb	$0x00, %ah
	je	0f
	cmpb	$0x02, %ah
	je	0f
	cmpb	$0x03, %ah
	je	0f
	cmpb	$0x04, %ah
	je	0f
	cmpb	$0x08, %ah
	je	0f
	cmpb	$0x15, %ah
	je	0f
	cmpb	$0x18, %ah
	je	0f
	cmpb	$0x41, %ah
	je	0f
	cmpb	$0x42, %ah
	je	0f
	cmpb	$0x43, %ah
	je	0f
	cmpb	$0x48, %ah
	je	0f
1:	popfl
.code16
	ljmp	*%cs:_oldint13
.code16gcc
0:	popfl
	call	_switchstack
	call	_pushcpu
	movzwl	%sp, %eax
	pushl	%eax
	movw	%cs:_segment, %ds
	movw	%cs:_segment, %es
	call	__int13
	addw	$4, %sp
	call	_popcpu
	call	_restorestack
.code16
	lret	$2
.code16gcc
#ifndef __MINGW32__
.type _int13, @function
.size _int13, .-_int13
#endif

.globl _pushcpu
_pushcpu:
	pushfl				# push flags
	subw	$36, %sp		# reserve stack for cpu structure
	pushl	%ebp			# setup ebp and frame pointer
	movl	%esp, %ebp
	pushl	44(%bp)			# push return address
	popl	8(%bp)			# pop on the correct spot
	pushl	40(%bp)			# push stored eflags
	popl	4(%bp)			# pop on the correct spot
	pushl	40(%bp)			# push stored eflags again
	popl	44(%bp)			# pop in structure
	movl	%eax, 40(%bp)		# save cpu state in structure
	movl	%ebx, 36(%bp)
	movl	%ecx, 32(%bp)
	movl	%edx, 28(%bp)
	movl	%esi, 24(%bp)
	movl	%edi, 20(%bp)
	movw	%ds, 18(%bp)
	movw	%es, 16(%bp)
	movw	%fs, 14(%bp)
	movw	%gs, 12(%bp)
	popl	%ebp			# restore ebp
	popfl				# restore flags
	ret
#ifndef __MINGW32__
.type _pushcpu, @function
.size _pushcpu, .-_pushcpu
#endif

.globl _popcpu
_popcpu:
	pushl	%ebp			# save ebp
	movl	%esp, %ebp              # use bp to access stack
	movl	36(%bp), %eax 		# restore cpu state from structure
	movl	32(%bp), %ebx
	movl	28(%bp), %ecx
	movl	24(%bp), %edx
	movl	20(%bp), %esi
	movl	16(%bp), %edi
	movw	14(%bp), %ds
	movw	12(%bp), %es
	movw	10(%bp), %fs
	movw	8(%bp), %gs
	pushl	40(%bp)			# push eflags
	popl	36(%bp)			# pop eflags
	pushl	4(%bp)			# push return address
	popl	40(%bp)			# pop return address
	popl	%ebp			# restore ebp
	addw	$32, %sp		# delete cpu structure
	popfl				# restore flags
	ret
#ifndef __MINGW32__
.type _popcpu, @function
.size _popcpu, .-_popcpu
#endif

# increment switch count and switch stack if it was zero
.globl _switchstack
_switchstack:
 	pushfl				#
	cli				#
	popl	%cs:8f			#
	popl	%cs:9f			#

	cmpw	$0, %cs:_switchcount	#
	jne	0f			#
	movw	%ss, %cs:_ss1		# save stack settings
	movl	%ebp, %cs:_ebp1
	movl	%esp, %cs:_esp1
	movl	$_end, %ebp		# initialize ebp and esp to
	leal	((STACKSIZE / 4) * 1)(%ebp), %ebp	# STACKSIZE above code
	movw	%cs:_segment, %ss	# switch stack to target segment
	movl	%ebp, %esp
	jmp	1f

0:	cmpw	$1, %cs:_switchcount	#
	jne	0f			#
	movw	%ss, %cs:_ss2		# save stack settings
	movl	%ebp, %cs:_ebp2
	movl	%esp, %cs:_esp2
	movl	$_end, %ebp		# initialize ebp and esp to
	leal	((STACKSIZE / 4) * 2)(%ebp), %ebp	# STACKSIZE above code
	movw	%cs:_segment, %ss	# switch stack to target segment
	movl	%ebp, %esp
	jmp	1f

0:	cmpw	$2, %cs:_switchcount	#
	jne	0f			#
	movw	%ss, %cs:_ss3		# save stack settings
	movl	%ebp, %cs:_ebp3
	movl	%esp, %cs:_esp3
	movl	$_end, %ebp		# initialize ebp and esp to
	leal	((STACKSIZE / 4) * 3)(%ebp), %ebp	# STACKSIZE above code
	movw	%cs:_segment, %ss	# switch stack to target segment
	movl	%ebp, %esp
	jmp	1f

0:	cmpw	$3, %cs:_switchcount	#
	jne	0f			#
	movw	%ss, %cs:_ss4		# save stack settings
	movl	%ebp, %cs:_ebp4
	movl	%esp, %cs:_esp4
	movl	$_end, %ebp		# initialize ebp and esp to
	leal	((STACKSIZE / 4) * 4)(%ebp), %ebp	# STACKSIZE above code
	movw	%cs:_segment, %ss	# switch stack to target segment
	movl	%ebp, %esp
	jmp	1f

0:	print	"\n\nOut of stack pages...\n"
	jmp	.
	halt

1:	incw	%cs:_switchcount	#
	pushl	%cs:9f			# restore return address
	pushl	%cs:8f			#
	popfl				#
	ret
8:	.long	0
9:	.long	0
#ifndef __MINGW32__
.type _switchstack, @function
.size _switchstack, .-_switchstack
#endif

# decrement switch count and switch stack if it reaches 0
.globl _restorestack
_restorestack:
 	pushfl				#
	cli				#
        popl	%cs:8f			#
	popl	%cs:9f			#

	cmpw	$4, %cs:_switchcount	#
	jne	0f			#
	pushw	%cs:_ss4		# select segment 0 for new stack
	pushl	%cs:_esp4		# set stack to below the boot code
	movw	%sp, %bp		# use bp for lss (lss can't use sp)
	lssl	(%bp), %esp		# setup new stack in one call
	movl	%cs:_ebp4, %ebp		#
	jmp	1f

0:	cmpw	$3, %cs:_switchcount	#
	jne	0f			#
	pushw	%cs:_ss3		# select segment 0 for new stack
	pushl	%cs:_esp3		# set stack to below the boot code
	movw	%sp, %bp		# use bp for lss (lss can't use sp)
	lssl	(%bp), %esp		# setup new stack in one call
	movl	%cs:_ebp3, %ebp		#
	jmp	1f

0:	cmpw	$2, %cs:_switchcount	#
	jne	0f			#
	pushw	%cs:_ss2		# select segment 0 for new stack
	pushl	%cs:_esp2		# set stack to below the boot code
	movw	%sp, %bp		# use bp for lss (lss can't use sp)
	lssl	(%bp), %esp		# setup new stack in one call
	movl	%cs:_ebp2, %ebp		#
	jmp	1f

0:	cmpw	$1, %cs:_switchcount	#
	jne	0f			#
	pushw	%cs:_ss1		# select segment 0 for new stack
	pushl	%cs:_esp1		# set stack to below the boot code
	movw	%sp, %bp		# use bp for lss (lss can't use sp)
	lssl	(%bp), %esp		# setup new stack in one call
	movl	%cs:_ebp1, %ebp		#
	jmp	1f

0:	print	"\n\nStack page error...\n"
	halt

1:	decw	%cs:_switchcount	#
	pushl	%cs:9f			# restore return address
	pushl	%cs:8f			#
0:	popfl				#
	ret
8:	.long	0
9:	.long	0
#ifndef __MINGW32__
.type _restorestack, @function
.size _restorestack, .-_restorestack
#endif

# _GETVECTOR: returns an interrupt vector from segment 0
.globl _GETVECTOR
_GETVECTOR:
	pushl	%ebp			# setup ebp and frame pointer
	movl	%esp, %ebp
	pushw	$0			# select segment 0
	popw	%fs			# fs may be used in functions
	movzbl	8(%bp), %eax		# get padded interrupt number
	movl	%fs:(,%eax,4), %eax	# get vector in eax
	popl	%ebp			# restore ebp
	ret
#ifndef __MINGW32__
.type _GETVECTOR, @function
.size _GETVECTOR, .-_GETVECTOR
#endif

# _SETVECTOR: sets an interrupt vector in segment 0
.globl _SETVECTOR
_SETVECTOR:
	pushl	%ebp			# setup ebp and frame pointer
	movl	%esp, %ebp
	pushw	$0			# select segment 0
	popw	%fs			# fs may be used in functions
	movzbl	8(%bp), %eax		# get padded interrupt number
	movl	12(%bp), %edx		# get vector in edx
	movl	%edx, %fs:(,%eax,4)	# set vector
	popl	%ebp			# restore ebp
	ret
#ifndef __MINGW32__
.type _SETVECTOR, @function
.size _SETVECTOR, .-_SETVECTOR
#endif

# _CHAININTERRUPT: chains an interrupt call with a cpu struct pointer
.globl _CHAININTERRUPT
_CHAININTERRUPT:
	pushl	%ebp			# setup ebp and frame pointer
	movl	%esp, %ebp
	pushal				# push state
	pushw	%ds
	pushw	%es
	movw	12(%bp), %bx		# get pointer to cpu in bx
	pushl	%ss:32(%bx)		# push eflags
	movl	%ss:28(%bx), %eax 	# restore cpu state from structure
	movl	%ss:20(%bx), %ecx
	movl	%ss:16(%bx), %edx
	movl	%ss:12(%bx), %esi
	movl	%ss:8(%bx), %edi
	movw	%ss:6(%bx), %ds
	movw	%ss:4(%bx), %es
	movw	%ss:2(%bx), %fs
	movw	%ss:(%bx), %gs
	movl	%ss:24(%bx), %ebx 	# ebx last
	popfl				# pop eflags
	pushfl
	popl %eax
	pushfw				# push flags to simulate interrupt
.code16
	lcall	*8(%bp)			# simulate interrupt
.code16gcc
	pushl	%ebx			# save ebx
	movw	12(%bp), %bx		# get pointer to cpu in bx
	pushfl				# push eflags
	popl	%ss:32(%bx)		# pop eflags in structure
	movl	%eax, %ss:28(%bx)	# save registers in structure
	popl	%ss:24(%bx)		# ebx
	movl	%ecx, %ss:20(%bx)
	movl	%edx, %ss:16(%bx)
	movl	%esi, %ss:12(%bx)
	movl	%edi, %ss:8(%bx)
	movw	%ds, %ss:6(%bx)
	movw	%es, %ss:4(%bx)
	movw	%fs, %ss:2(%bx)
	movw	%gs, %ss:(%bx)
	popw	%es			# restore state
	popw	%ds
	popal
	popl	%ebp			# restore ebp
	ret
#ifndef __MINGW32__
.type _CHAININTERRUPT, @function
.size _CHAININTERRUPT, .-_CHAININTERRUPT
#endif



.globl _i0, _i1, _i2, _i3, _i4, _i5, _i6, _i7
_i0:
pushfl
pushal
pushl $'0'
call _putchar
add $4, %sp
popal
popfl
.code16
	iret
.code16gcc
_i1:
pushfl
pushal
pushl $'1'
call _putchar
add $4, %sp
popal
popfl
.code16
	iret
.code16gcc
_i2:
pushfl
pushal
pushl $'2'
call _putchar
add $4, %sp
popal
popfl
.code16
	iret
.code16gcc
_i3:
pushfl
pushal
pushl $'3'
call _putchar
add $4, %sp
popal
popfl
.code16
	iret
.code16gcc
_i4:
pushfl
pushal
pushl $'4'
call _putchar
add $4, %sp
popal
popfl
.code16
	iret
.code16gcc
_i5:
pushfl
pushal
pushl $'5'
call _putchar
add $4, %sp
popal
popfl
.code16
	iret
.code16gcc
_i6:
popl %eax
debug
halt

pushfl
pushal
pushl $'6'
call _putchar
add $4, %sp
popal
popfl
.code16
	iret
.code16gcc
_i7:
pushfl
pushal
pushl $'7'
call _putchar
add $4, %sp
popal
popfl
.code16
	iret
.code16gcc
