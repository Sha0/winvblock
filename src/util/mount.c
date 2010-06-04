/**
 * Copyright 2006-2008, V.
 * Portions copyright (C) 2009, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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

/**
 * @file
 *
 * Mount command
 *
 */

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <malloc.h>

#include "winvblock.h"
#include "portable.h"
#include "mount.h"
#include "aoe.h"

/**
 * Command routine
 *
 * @v boot_bus          A handle to the boot bus device
 */
#define command_decl( x )       \
                                \
int STDCALL                     \
x (                             \
  HANDLE boot_bus,              \
  int argc,                     \
  char **argv                   \
 )
/*
 * Function pointer for a command routine.
 * 'indent' mangles this, so it looks weird
 */
typedef command_decl (
   ( *command_routine )
 );

command_decl ( cmd_help )
{
  printf ( "winvblk <scan|show|mount|umount|attach|detach>\n"	/*    */
	   "\n"			/*    */
	   "  scan\n"		/*    */
	   "        Shows the reachable AoE targets.\n"	/*    */
	   "\n"			/*    */
	   "  show\n"		/*    */
	   "        Shows the mounted AoE targets.\n"	/*    */
	   "\n"			/*    */
	   "  mount <client mac address> <major> <minor>\n"	/*    */
	   "        Mounts an AoE target.\n"	/*    */
	   "\n"			/*    */
	   "  umount <disk number>\n"	/*    */
	   "        Unmounts an AoE disk.\n"	/*    */
	   "\n"			/*    */
	   "  attach <filepath> <disk type> <cyls> <heads> <sectors>\n"	/*    */
	   "        Attaches <filepath> disk image file.\n"	/*    */
	   "        <disk type> is 'f' for floppy, 'c' for CD/DVD, 'h' for HDD\n"	/*    */
	   "\n"			/*    */
	   "  detach <disk number>\n"	/*    */
	   "        Detaches file-backed disk.\n"	/*    */
	   "\n" );
  return 1;
}

command_decl ( cmd_scan )
{
  aoe__mount_targets_ptr targets;
  DWORD bytes_returned;
  winvblock__uint32 i;
  winvblock__uint8 string[256];
  int status = 2;

  targets =
    malloc ( sizeof ( aoe__mount_targets ) +
	     ( 32 * sizeof ( aoe__mount_target ) ) );
  if ( targets == NULL )
    {
      printf ( "Out of memory\n" );
      goto err_alloc;
    }
  if ( !DeviceIoControl
       ( boot_bus, IOCTL_AOE_SCAN, NULL, 0, targets,
	 ( sizeof ( aoe__mount_targets ) +
	   ( 32 * sizeof ( aoe__mount_target ) ) ), &bytes_returned,
	 ( LPOVERLAPPED ) NULL ) )
    {
      printf ( "DeviceIoControl (%d)\n", ( int )GetLastError (  ) );
      status = 2;
      goto err_ioctl;
    }
  if ( targets->Count == 0 )
    {
      printf ( "No AoE targets found.\n" );
      goto err_no_targets;
    }
  printf ( "Client NIC          Target      Server MAC         Size\n" );
  for ( i = 0; i < targets->Count && i < 10; i++ )
    {
      sprintf ( string, "e%lu.%lu      ", targets->Target[i].Major,
		targets->Target[i].Minor );
      string[10] = 0;
      printf ( " %02x:%02x:%02x:%02x:%02x:%02x  %s "
	       " %02x:%02x:%02x:%02x:%02x:%02x  %I64uM\n",
	       targets->Target[i].ClientMac[0],
	       targets->Target[i].ClientMac[1],
	       targets->Target[i].ClientMac[2],
	       targets->Target[i].ClientMac[3],
	       targets->Target[i].ClientMac[4],
	       targets->Target[i].ClientMac[5], string,
	       targets->Target[i].ServerMac[0],
	       targets->Target[i].ServerMac[1],
	       targets->Target[i].ServerMac[2],
	       targets->Target[i].ServerMac[3],
	       targets->Target[i].ServerMac[4],
	       targets->Target[i].ServerMac[5],
	       ( targets->Target[i].LBASize / 2048 ) );
    }

err_no_targets:

  status = 0;

err_ioctl:

  free ( targets );
err_alloc:

  return status;
}

command_decl ( cmd_show )
{
  aoe__mount_disks_ptr mounted_disks;
  DWORD bytes_returned;
  winvblock__uint32 i;
  winvblock__uint8 string[256];
  int status = 2;

  mounted_disks =
    malloc ( sizeof ( aoe__mount_disks ) +
	     ( 32 * sizeof ( aoe__mount_disk ) ) );
  if ( mounted_disks == NULL )
    {
      printf ( "Out of memory\n" );
      goto err_alloc;
    }
  if ( !DeviceIoControl
       ( boot_bus, IOCTL_AOE_SHOW, NULL, 0, mounted_disks,
	 ( sizeof ( aoe__mount_disks ) + ( 32 * sizeof ( aoe__mount_disk ) ) ),
	 &bytes_returned, ( LPOVERLAPPED ) NULL ) )
    {
      printf ( "DeviceIoControl (%d)\n", ( int )GetLastError (  ) );
      goto err_ioctl;
    }

  status = 0;

  if ( mounted_disks->Count == 0 )
    {
      printf ( "No AoE disks mounted.\n" );
      goto err_no_disks;
    }
  printf ( "Disk  Client NIC         Server MAC         Target      Size\n" );
  for ( i = 0; i < mounted_disks->Count && i < 10; i++ )
    {
      sprintf ( string, "e%lu.%lu      ", mounted_disks->Disk[i].Major,
		mounted_disks->Disk[i].Minor );
      string[10] = 0;
      printf
	( " %-4lu %02x:%02x:%02x:%02x:%02x:%02x  %02x:%02x:%02x:%02x:%02x:%02x  %s  %I64uM\n",
	  mounted_disks->Disk[i].Disk, mounted_disks->Disk[i].ClientMac[0],
	  mounted_disks->Disk[i].ClientMac[1],
	  mounted_disks->Disk[i].ClientMac[2],
	  mounted_disks->Disk[i].ClientMac[3],
	  mounted_disks->Disk[i].ClientMac[4],
	  mounted_disks->Disk[i].ClientMac[5],
	  mounted_disks->Disk[i].ServerMac[0],
	  mounted_disks->Disk[i].ServerMac[1],
	  mounted_disks->Disk[i].ServerMac[2],
	  mounted_disks->Disk[i].ServerMac[3],
	  mounted_disks->Disk[i].ServerMac[4],
	  mounted_disks->Disk[i].ServerMac[5], string,
	  ( mounted_disks->Disk[i].LBASize / 2048 ) );
    }

err_no_disks:

err_ioctl:

  free ( mounted_disks );
err_alloc:

  return status;
}

command_decl ( cmd_mount )
{
  winvblock__uint8 mac_addr[6];
  winvblock__uint32 ver_major,
   ver_minor;
  winvblock__uint8 in_buf[sizeof ( mount__filedisk ) + 1024];
  DWORD bytes_returned;

  sscanf ( argv[2], "%02x:%02x:%02x:%02x:%02x:%02x", ( int * )&mac_addr[0],
	   ( int * )&mac_addr[1], ( int * )&mac_addr[2], ( int * )&mac_addr[3],
	   ( int * )&mac_addr[4], ( int * )&mac_addr[5] );
  sscanf ( argv[3], "%d", ( int * )&ver_major );
  sscanf ( argv[4], "%d", ( int * )&ver_minor );
  printf ( "mounting e%d.%d from %02x:%02x:%02x:%02x:%02x:%02x\n",
	   ( int )ver_major, ( int )ver_minor, mac_addr[0], mac_addr[1],
	   mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5] );
  memcpy ( &in_buf[0], mac_addr, 6 );
  ( ( winvblock__uint16_ptr ) & in_buf[6] )[0] =
    ( winvblock__uint16 ) ver_major;
  ( ( winvblock__uint8_ptr ) & in_buf[8] )[0] = ( winvblock__uint8 ) ver_minor;
  if ( !DeviceIoControl
       ( boot_bus, IOCTL_AOE_MOUNT, in_buf, sizeof ( in_buf ), NULL, 0,
	 &bytes_returned, ( LPOVERLAPPED ) NULL ) )
    {
      printf ( "DeviceIoControl (%d)\n", ( int )GetLastError (  ) );
      return 2;
    }
  return 0;
}

command_decl ( cmd_umount )
{
  winvblock__uint32 disk_num;
  winvblock__uint8 in_buf[sizeof ( mount__filedisk ) + 1024];
  DWORD bytes_returned;

  sscanf ( argv[2], "%d", ( int * )&disk_num );
  printf ( "unmounting disk %d\n", ( int )disk_num );
  memcpy ( &in_buf, &disk_num, 4 );
  if ( !DeviceIoControl
       ( boot_bus, IOCTL_AOE_UMOUNT, in_buf, sizeof ( in_buf ), NULL, 0,
	 &bytes_returned, ( LPOVERLAPPED ) NULL ) )
    {
      printf ( "DeviceIoControl (%d)\n", ( int )GetLastError (  ) );
      return 2;
    }
  return 0;
}

command_decl ( cmd_attach )
{
  mount__filedisk filedisk;
  char obj_path_prefix[] = "\\??\\";
  winvblock__uint8 in_buf[sizeof ( mount__filedisk ) + 1024];
  DWORD bytes_returned;

  if ( argc < 6 )
    {
      printf ( "Too few parameters.\n\n" );
      cmd_help ( NULL, 0, NULL );
      return 1;
    }
  filedisk.type = *argv[3];
  sscanf ( argv[4], "%d", ( int * )&filedisk.cylinders );
  sscanf ( argv[5], "%d", ( int * )&filedisk.heads );
  sscanf ( argv[6], "%d", ( int * )&filedisk.sectors );
  memcpy ( &in_buf, &filedisk, sizeof ( mount__filedisk ) );
  memcpy ( &in_buf[sizeof ( mount__filedisk )], obj_path_prefix,
	   sizeof ( obj_path_prefix ) );
  memcpy ( &in_buf
	   [sizeof ( mount__filedisk ) + sizeof ( obj_path_prefix ) - 1],
	   argv[2], strlen ( argv[2] ) + 1 );
  if ( !DeviceIoControl
       ( boot_bus, IOCTL_FILE_ATTACH, in_buf, sizeof ( in_buf ), NULL, 0,
	 &bytes_returned, ( LPOVERLAPPED ) NULL ) )
    {
      printf ( "DeviceIoControl (%d)\n", ( int )GetLastError (  ) );
      return 2;
    }
  return 0;
}

command_decl ( cmd_detach )
{
  winvblock__uint32 disk_num;

  winvblock__uint8 in_buf[sizeof ( mount__filedisk ) + 1024];
  DWORD bytes_returned;

  sscanf ( argv[2], "%d", ( int * )&disk_num );
  printf ( "Detaching file-backed disk %d\n", ( int )disk_num );
  memcpy ( &in_buf, &disk_num, 4 );
  if ( !DeviceIoControl
       ( boot_bus, IOCTL_FILE_DETACH, in_buf, sizeof ( in_buf ), NULL, 0,
	 &bytes_returned, ( LPOVERLAPPED ) NULL ) )
    {
      printf ( "DeviceIoControl (%d)\n", ( int )GetLastError (  ) );
      return 2;
    }
  return 0;
}

int
main (
  int argc,
  char **argv,
  char **envp
 )
{
  command_routine cmd = cmd_help;
  HANDLE boot_bus = NULL;
  int status = 1;

  if ( argc < 2 )
    {
      cmd_help ( NULL, 0, NULL );
      goto err_too_few_args;
    }

  if ( strcmp ( argv[1], "scan" ) == 0 )
    {
      cmd = cmd_scan;
    }
  else if ( strcmp ( argv[1], "show" ) == 0 )
    {
      cmd = cmd_show;
    }
  else if ( strcmp ( argv[1], "mount" ) == 0 )
    {
      cmd = cmd_mount;
    }
  else if ( strcmp ( argv[1], "umount" ) == 0 )
    {
      cmd = cmd_umount;
    }
  else if ( strcmp ( argv[1], "attach" ) == 0 )
    {
      cmd = cmd_attach;
    }
  else if ( strcmp ( argv[1], "detach" ) == 0 )
    {
      cmd = cmd_detach;
    }
  else
    {
      cmd_help ( NULL, 0, NULL );
      goto err_bad_cmd;
    }

  boot_bus =
    CreateFile ( "\\\\.\\" winvblock__literal, GENERIC_READ | GENERIC_WRITE,
		 FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0,
		 NULL );
  if ( boot_bus == INVALID_HANDLE_VALUE )
    {
      printf ( "CreateFile (%d)\n", ( int )GetLastError (  ) );
      goto err_handle;
    }

  status = cmd ( boot_bus, argc, argv );

  CloseHandle ( boot_bus );
err_handle:

err_bad_cmd:

err_too_few_args:

  return status;
}
