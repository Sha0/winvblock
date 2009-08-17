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
#ifndef _ASM_H
#  define _ASM_H

#  include "lib.h"

typedef struct s_cpu
{
	unsigned short gs;
	unsigned short fs;
	unsigned short es;
	unsigned short ds;
	unsigned int edi;
	unsigned int esi;
	unsigned int edx;
	unsigned int ecx;
	unsigned int ebx;
	unsigned int eax;
	union
	{
		unsigned int eflags;
		unsigned short flags;
	};
} __attribute__ ( ( __packed__ ) ) t_cpu;

extern void *end;								// end of image from linker
//extern t_cpu cpu;             // cpu state struct in asm.S
extern unsigned short segment;	// target segment in asm.S
extern int volatile oldint13;
extern int volatile gotisr;
extern int volatile timer;
extern int irq;

int GETVECTOR (
	int i
 );
void SETVECTOR (
	int i,
	int v
 );
int _CHAININTERRUPT (
	int v,
	t_cpu * cpu
 );

#  ifdef __MINGW32__
#    define INTERRUPTPROTO(x) \
  extern void _##x(t_cpu *cpu); extern void x()
#  else
#    define INTERRUPTPROTO(x) \
  ".type __" #x ", @function\n" \
  ".size __" #x ", .-__" #x "\n" \
  extern void _##x(t_cpu *cpu); extern void x()
#  endif
#  define INTERRUPT(x) asm("\n" \
  ".text\n" \
  ".globl __" #x "\n" \
  "__" #x ":\n" \
  "	call	_switchstack\n" \
  "	call	_pushcpu\n" \
  "	cli\n" \
  "	movzwl	%sp, %eax\n" \
  "	pushl	%eax\n" \
  "	movw	%cs:_segment, %ds\n" \
  "	movw	%cs:_segment, %es\n" \
  "	call	_" #x "\n" \
  "	addw	$4, %sp\n" \
  "	call	_popcpu\n" \
  "	call	_restorestack\n" \
  ".code16\n" \
  "	lret	$2\n" \
  ".code16gcc\n" \
  "0:	lret	$2\n" \
); \
void x (t_cpu *cpu)

void int8 (
	void
 );															// in asm.S
void int13 (
	void
 );															// in asm.S
void isr (
	void
 );															// in asm.S
void nmi (
	void
 );															// in asm.S
void i0 (
	void
 );															// in asm.S
void i1 (
	void
 );															// in asm.S
void i2 (
	void
 );															// in asm.S
void i3 (
	void
 );															// in asm.S
void i4 (
	void
 );															// in asm.S
void i5 (
	void
 );															// in asm.S
void i6 (
	void
 );															// in asm.S
void i7 (
	void
 );															// in asm.S

#endif
