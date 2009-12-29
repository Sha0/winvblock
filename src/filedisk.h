/**
 * Copyright (C) 2009, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef _filedisk_h
#  define _filedisk_h

/**
 * @file
 *
 * File-backed disk specifics
 *
 */

winvblock__def_struct ( filedisk__type )
{
  disk__type disk;
};

extern disk__io_decl (
  filedisk__io
 );
extern winvblock__uint32 filedisk__max_xfer_len (
  disk__type_ptr disk_ptr
 );
extern winvblock__uint32 filedisk__query_id (
  disk__type_ptr disk_ptr,
  BUS_QUERY_ID_TYPE query_type,
  PWCHAR buf_512
 );

/*
 * Establish a pointer into the file-backed disk device's extension space
 */
__inline filedisk__type_ptr STDCALL
filedisk__get_ptr (
  driver__dev_ext_ptr dev_ext_ptr
 )
{
  /*
   * Since the device extension is the first member of the disk
   * member of a file-backed disk, and the disk structure is itself the
   * first member of a file-backed disk structure, a simple cast will suffice
   */
  return ( filedisk__type_ptr ) dev_ext_ptr;
}

extern winvblock__bool STDCALL filedisk__no_init (
  IN driver__dev_ext_ptr dev_ext
 );
extern void filedisk__find (
  void
 );

#endif				/* _filedisk_h */
