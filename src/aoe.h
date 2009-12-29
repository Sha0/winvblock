/**
 * Copyright (C) 2009, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 * Copyright 2006-2008, V.
 * For WinAoE contact information, see http://winaoe.org/
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
#ifndef _AOE_H
#  define _AOE_H

/**
 * @file
 *
 * AoE specifics
 *
 */

#  define htons(x) (winvblock__uint16)((((x) << 8) & 0xff00) | \
                                       (((x) >> 8) & 0xff))
#  define ntohs(x) (winvblock__uint16)((((x) << 8) & 0xff00) | \
                                       (((x) >> 8) & 0xff))

winvblock__def_struct ( aoe__disk_type )
{
  disk__type disk;
  winvblock__uint32 MTU;
  winvblock__uint8 ClientMac[6];
  winvblock__uint8 ServerMac[6];
  winvblock__uint32 Major;
  winvblock__uint32 Minor;
  winvblock__uint32 MaxSectorsPerPacket;
  winvblock__uint32 Timeout;
};

extern winvblock__bool STDCALL AoE_SearchDrive (
  IN driver__dev_ext_ptr DeviceExtension
 );
extern disk__io_decl (
  aoe__disk_io
 );
extern winvblock__uint32 aoe__max_xfer_len (
  disk__type_ptr disk_ptr
 );
extern winvblock__uint32 aoe__query_id (
  disk__type_ptr disk_ptr,
  BUS_QUERY_ID_TYPE query_type,
  PWCHAR buf_512
 );
extern NTSTATUS STDCALL AoE_Reply (
  IN winvblock__uint8_ptr SourceMac,
  IN winvblock__uint8_ptr DestinationMac,
  IN winvblock__uint8_ptr Data,
  IN winvblock__uint32 DataSize
 );
extern VOID AoE_ResetProbe (
  void
 );
extern NTSTATUS AoE_Start (
  void
 );
extern VOID AoE_Stop (
  void
 );
extern void aoe__process_abft (
  void
 );

/*
 * Establish a pointer into the AoE disk device's extension space
 */
__inline aoe__disk_type_ptr STDCALL
aoe__get_disk_ptr (
  driver__dev_ext_ptr dev_ext_ptr
 )
{
  /*
   * Since the device extension is the first member of the disk
   * member of an AoE disk, and the disk structure is itself the
   * first member of an AoE disk structure, a simple cast will suffice
   */
  return ( aoe__disk_type_ptr ) dev_ext_ptr;
}

#endif				/* _AOE_H */
