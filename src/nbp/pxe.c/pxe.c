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
#include "pxe.h"
#include "asm.h"
#include "lib.h"

int apivector = 0;

int
pxeinit (
	void
 )
{
	int v;
asm ( "pushw	%%es\n\t" "movw	$0x5650, %%ax\n\t" "int	$0x1a\n\t" "cmpw	$0x564e, %%ax\n\t" "je	0f\n\t" "xorl	%%eax, %%eax\n\t" "jmp	1f\n" "0:	les	%%es:0x28(%%bx), %%bx\n\t" "movl	%%es:0x10(%%bx), %%eax\n" "1:	popw	%%es": "=a" ( v ):
:"ebx", "cc" );
	apivector = v;
	return v;
}

unsigned short
api (
	unsigned short command,
	void *commandstruct
 )
{
	unsigned short r;
	asm volatile (
//    "pushfl\n\t"
//    "pushw    %%ds\n\t"
//    "pushw    %%es\n\t"
//    "pushw    %%fs\n\t"
//    "pushw    %%gs\n\t"
//    "pushal\n\t"
	"xorw	%%ax, %%ax\n\t" "pushw	%%cs\n\t" "pushw	%%bx\n\t" "pushw	%%cx\n"
	".code16\n\t" "lcall	*%%cs:_apivector\n" ".code16gcc\n\t"
//    "cli\n\t"
	"addw	$6, %%sp\n\t"
//    "movw     %%ax, %%gs\n\t"
//    "popal\n\t"
//    "movw     %%gs, %%ax\n\t"
//    "popw     %%gs\n\t"
//    "popw     %%fs\n\t"
//    "popw     %%es\n\t"
//    "popw     %%ds\n\t"
//    "popfl"
	:"=a" ( r ):"b" ( commandstruct ),
	"c" ( command ):"cc",
	"memory"
	 );
	return r;
}

void
apierror (
	char *message,
	unsigned short status
 )
{
	printf ( "%s: 0x%04x\n", message, status );
	halt (  );
}
