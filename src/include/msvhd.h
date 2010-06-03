/**
 * Copyright (C) 2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 *
 * This file is part of WinVBlock, derived from WinAoE.
 *
 * WinVBlock is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * WinVBlock is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WinVBlock.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _msvhd_h
#  define _msvhd_h

/**
 * @file
 *
 * Microsoft .VHD disk image format specifics.
 * Structure details from the VHD Image Format Specification Document[1],
 * kindly provided by Microsoft's Open Source Promise[2].
 *
 * [1] http://technet.microsoft.com/en-us/virtualserver/bb676673.aspx
 * [2] http://www.microsoft.com/interop/osp/default.mspx
 *
 */

/* The .VHD disk image footer format */
#  ifdef _MSC_VER
#    pragma pack(1)
#  endif
winvblock__def_struct ( msvhd__footer )
{
  char cookie[8];
  byte__array_union ( winvblock__uint32, features );
  byte__array_union ( winvblock__uint32, file_ver );
  byte__array_union ( ULONGLONG, data_offset );
  byte__array_union ( winvblock__uint32, timestamp );
  char creator_app[4];
  byte__array_union ( winvblock__uint32, creator_ver );
  byte__array_union ( winvblock__uint32, creator_os );
  byte__array_union ( ULONGLONG, orig_size );
  byte__array_union ( ULONGLONG, cur_size );
  byte__array_union ( winvblock__uint16, geom_cyls );
  winvblock__uint8 geom_heads;
  winvblock__uint8 geom_sects_per_track;
  byte__array_union ( winvblock__uint32, type );
  byte__array_union ( winvblock__uint32, checksum );
  char uid[16];
  char saved_state;
  char reserved[427];
} __attribute__ ( ( __packed__ ) );

#  ifdef _MSC_VER
#    pragma pack()
#  endif

/* Function body in header so user-land utility links without WinVBlock */
static void STDCALL
msvhd__footer_swap_endian (
  msvhd__footer_ptr footer_ptr
 )
{
  byte__rev_array_union ( footer_ptr->features );
  byte__rev_array_union ( footer_ptr->file_ver );
  byte__rev_array_union ( footer_ptr->data_offset );
  byte__rev_array_union ( footer_ptr->timestamp );
  byte__rev_array_union ( footer_ptr->creator_ver );
  byte__rev_array_union ( footer_ptr->creator_os );
  byte__rev_array_union ( footer_ptr->orig_size );
  byte__rev_array_union ( footer_ptr->cur_size );
  byte__rev_array_union ( footer_ptr->geom_cyls );
  byte__rev_array_union ( footer_ptr->type );
  byte__rev_array_union ( footer_ptr->checksum );
}

#endif				/* _msvhd_h */
