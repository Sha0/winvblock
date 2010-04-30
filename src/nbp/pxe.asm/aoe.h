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

.code16
//#define DEBUG
//#define CHKSUM
#define SERIAL

#define BREAK 0
#define TIMEOUT 55

#define print call printline; .asciz
#define halt jmp haltcpu
#define D call debug;
#define N call ndebug;

#define AoEdstaddr	0
#define AoEsrcaddr	6
#define AoEprotocol	12
#define AoEver		14
#define AoEflags	14
#define AoEerror	15
#define AoEmajor	16
#define AoEminor	18
#define AoEcommand	19
#define AoEtag		20
#define AoEaflags	24
#define AoEerr		25
#define AoEfeature	25
#define AoEcount	26
#define AoEcmd		27
#define AoEstatus	27
#define AoElba0		28
#define AoElba1		29
#define AoElba2		30
#define AoElba3		31
#define AoElba4		32
#define AoElba5		33
#define AoEreserved	34
#define AoEdata		36
#define AoEsize		1518
