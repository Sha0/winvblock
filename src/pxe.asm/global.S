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

.globl _drive, _cylinders, _heads, _sectors, _size
.globl _irq, _clientmac, _servermac, _major, _minor

_drive:		.word	0
_cylinders:	.long	0
_heads:		.word	0
_sectors:	.word	0
_size:		.long	0

_irq:		.word	0
_clientmac:	.org	.+6, 0
_servermac:	.org	.+6, 0
_major:		.word	0
_minor:		.word	0
