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
#ifndef _LIB_H
#  define _LIB_H

#  include <stdarg.h>

typedef unsigned long size_t;
#  define NULL 0

#  define htons(x) (unsigned short)((((x) << 8) & 0xff00) | (((x) >> 8) & 0xff))
#  define ntohs(x) (unsigned short)((((x) << 8) & 0xff00) | (((x) >> 8) & 0xff))
#  define sti() asm volatile ("sti")
#  define cli() asm volatile ("cli")

// in libasm.S
int putchar (
	int c
 );
unsigned char volatile inb (
	int p
 );
void volatile outb (
	unsigned char v,
	int p
 );
char getkey (
	int t
 );
void halt (
	void
 );
int volatile segmemcpy (
	int dest,
	int src,
	size_t n
 );
#  ifndef MINIMAL
int segmemset (
	int s,
	int c,
	size_t n
 );
#    define debug() asm volatile ("call _debug":::"memory")
#  else
#    define debug()
#  endif

// in lib.c
size_t strlen (
	const char *s
 );
int puts (
	const char *s
 );
int isspace (
	int c
 );
int isdigit (
	int c
 );
int isalpha (
	int c
 );
int isupper (
	int c
 );
int islower (
	int c
 );
int toupper (
	int c
 );
int tolower (
	int c
 );
int memcmp (
	const void *s1,
	const void *s2,
	size_t n
 );
void *memcpy (
	void *dest,
	const void *src,
	size_t n
 );
void *memset (
	void *s,
	int c,
	size_t n
 );

// From Public Domain CLib (PDPCLIB) by Paul Edwards
long int strtol (
	const char *nptr,
	char **endptr,
	int base
 );

// From Public Domain printf.c
int vsprintf (
	char *buf,
	const char *fmt,
	va_list args
 );
int sprintf (
	char *buf,
	const char *fmt,
	...
 );
int vprintf (
	const char *fmt,
	va_list args
 );
int printf (
	const char *fmt,
	...
 );

#endif
