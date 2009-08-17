/*
  Copyright 2006-2008, V.
  For contact information, see http://winaoe.org/

  strtol function copyright by Paul Edwards.
  printf.c copyright by Chris Giese.
  Don't bother them with questions on WinAoE, but see http://www.winaoe.org/

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
#include "lib.h"
#include "asm.h"

size_t
strlen (
	const char *s
 )
{
	int l = 0;
	while ( s[l] )
		l++;
	return l;
}

int
puts (
	const char *s
 )
{
	while ( *s )
		putchar ( *s++ );
	putchar ( '\n' );
	return -1;
}

int
isspace (
	int c
 )
{
	switch ( c )
		{
			case ' ':
			case '\f':
			case '\n':
			case '\r':
			case '\t':
			case '\v':
				return 1;
		}
	return 0;
}

int
isdigit (
	int c
 )
{
	if ( ( c >= '0' ) && ( c <= '9' ) )
		return 1;
	return 0;
}

int
isalpha (
	int c
 )
{
	return ( isupper ( c ) || islower ( c ) );
}

int
isupper (
	int c
 )
{
	if ( ( c >= 'A' ) && ( c <= 'Z' ) )
		return 1;
	return 0;
}

int
islower (
	int c
 )
{
	if ( ( c >= 'a' ) && ( c <= 'z' ) )
		return 1;
	return 0;
}

int
toupper (
	int c
 )
{
	if ( islower ( c ) )
		return ( ( c & ~0x20 ) & 0xff );
	return c;
}

int
tolower (
	int c
 )
{
	if ( isupper ( c ) )
		return ( c | 0x20 );
	return c;
}

int
memcmp (
	const void *s1,
	const void *s2,
	size_t n
 )
{
	int i;
	if ( s1 == s2 )
		return 0;
	for ( i = 0; i < n; i++ )
		{
			if ( ( ( unsigned char * )s1 )[i] < ( ( unsigned char * )s2 )[i] )
				return -1;
			if ( ( ( unsigned char * )s1 )[i] > ( ( unsigned char * )s2 )[i] )
				return 1;
		}
	return 0;
}

void *
memcpy (
	void *dest,
	const void *src,
	size_t n
 )
{
	int i;
	if ( src == dest )
		return dest;
	if ( src < dest )
		{
			for ( i = n - 1; i >= 0; i-- )
				( ( unsigned char * )dest )[i] = ( ( unsigned char * )src )[i];
		}
	else
		{
			for ( i = 0; i < n; i++ )
				( ( unsigned char * )dest )[i] = ( ( unsigned char * )src )[i];
		}
	return dest;
}

void *
memset (
	void *s,
	int c,
	size_t n
 )
{
	int i;
	for ( i = 0; i < n; i++ )
		( ( unsigned char * )s )[i] = c;
	return s;
}

// From Public Domain CLib (PDPCLIB) by Paul Edwards
long int
strtol (
	const char *nptr,
	char **endptr,
	int base
 )
{
	long x = 0;
	int undecided = 0;

	if ( base == 0 )
		undecided = 1;
	while ( 1 )
		{
			if ( isdigit ( *nptr ) )
				{
					if ( base == 0 )
						{
							if ( *nptr == '0' )
								base = 8;
							else
								{
									base = 10;
									undecided = 0;
								}
						}
					x = x * base + ( *nptr - '0' );
					nptr++;
				}
			else if ( isalpha ( *nptr ) )
				{
					if ( ( *nptr == 'X' ) || ( *nptr == 'x' ) )
						{
							if ( ( base == 0 ) || ( ( base == 8 ) && undecided ) )
								{
									base = 16;
									undecided = 0;
								}
							else
								break;
						}
					else
						{
							x = x * base + ( toupper ( ( unsigned char )*nptr ) - 'A' ) + 10;
							nptr++;
						}
				}
			else
				break;
		}
	if ( endptr != NULL )
		*endptr = ( char * )nptr;
	return ( x );
}

// Include Public Domain printf.c
#include "printf.c"
