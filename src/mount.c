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

#include "winvblock.h"
#include "portable.h"
#include "mount.h"

typedef enum
{ CommandScan, CommandShow, CommandMount, CommandUmount, CommandAttach,
  CommandDetach
} COMMAND;

static void
Help (
  void
 )
{
  printf ( "winvblk <scan|show|mount|umount|attach|detach>\n\n" );
  printf ( "  scan\n        Shows the reachable AoE targets.\n\n" );
  printf ( "  show\n        Shows the mounted AoE targets.\n\n" );
  printf ( "  mount <client mac address> <major> <minor>\n"
	   "        Mounts an AoE target.\n\n" );
  printf ( "  umount <disk number>\n        Unmounts an AoE disk.\n\n" );
  printf ( "  attach <filepath> <disk type> <cyls> <heads> <sectors>\n"
	   "        Attaches <filepath> disk image file.\n"
	   "        <disk type> is 'f' for floppy, 'c' for CD/DVD, "
	   "'h' for HDD\n\n" );
  printf ( "  detach <disk number>\n"
	   "        Detaches file-backed disk.\n\n" );
}

int
main (
  int argc,
  char **argv,
  char **envp
 )
{
  COMMAND Command;
  HANDLE DeviceHandle;
  winvblock__uint8 InBuffer[sizeof ( mount__filedisk ) + 1024];
  winvblock__uint8 String[256];
  PMOUNT_TARGETS Targets;
  PMOUNT_DISKS Disks;
  winvblock__uint8 Mac[6];
  DWORD BytesReturned;
  winvblock__uint32 Major,
   Minor,
   Disk;
  winvblock__uint32 i;

  if ( argc < 2 )
    {
      Help (  );
      goto end;
    }

  if ( strcmp ( argv[1], "scan" ) == 0 )
    {
      Command = CommandScan;
    }
  else if ( strcmp ( argv[1], "show" ) == 0 )
    {
      Command = CommandShow;
    }
  else if ( strcmp ( argv[1], "mount" ) == 0 )
    {
      Command = CommandMount;
    }
  else if ( strcmp ( argv[1], "umount" ) == 0 )
    {
      Command = CommandUmount;
    }
  else if ( strcmp ( argv[1], "attach" ) == 0 )
    {
      Command = CommandAttach;
    }
  else if ( strcmp ( argv[1], "detach" ) == 0 )
    {
      Command = CommandDetach;
    }
  else
    {
      Help (  );
      printf ( "Press enter to exit\n" );
      getchar (  );
      goto end;
    }

  DeviceHandle =
    CreateFile ( "\\\\.\\" winvblock__literal, GENERIC_READ | GENERIC_WRITE,
		 FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0,
		 NULL );
  if ( DeviceHandle == INVALID_HANDLE_VALUE )
    {
      printf ( "CreateFile (%d)\n", ( int )GetLastError (  ) );
      goto end;
    }

  switch ( Command )
    {
      case CommandScan:
	if ( ( Targets =
	       ( PMOUNT_TARGETS ) malloc ( sizeof ( MOUNT_TARGETS ) +
					   ( 32 *
					     sizeof ( MOUNT_TARGET ) ) ) ) ==
	     NULL )
	  {
	    printf ( "Out of memory\n" );
	    break;
	  }
	if ( !DeviceIoControl
	     ( DeviceHandle, IOCTL_AOE_SCAN, NULL, 0, Targets,
	       ( sizeof ( MOUNT_TARGETS ) + ( 32 * sizeof ( MOUNT_TARGET ) ) ),
	       &BytesReturned, ( LPOVERLAPPED ) NULL ) )
	  {
	    printf ( "DeviceIoControl (%d)\n", ( int )GetLastError (  ) );
	    free ( Targets );
	    break;
	  }
	if ( Targets->Count == 0 )
	  {
	    printf ( "No AoE targets found.\n" );
	  }
	else
	  {
	    printf
	      ( "Client NIC          Target      Server MAC         Size\n" );
	    for ( i = 0; i < Targets->Count && i < 10; i++ )
	      {
		sprintf ( String, "e%lu.%lu      ", Targets->Target[i].Major,
			  Targets->Target[i].Minor );
		String[10] = 0;
		printf
		  ( " %02x:%02x:%02x:%02x:%02x:%02x  %s  %02x:%02x:%02x:%02x:%02x:%02x  %I64uM\n",
		    Targets->Target[i].ClientMac[0],
		    Targets->Target[i].ClientMac[1],
		    Targets->Target[i].ClientMac[2],
		    Targets->Target[i].ClientMac[3],
		    Targets->Target[i].ClientMac[4],
		    Targets->Target[i].ClientMac[5], String,
		    Targets->Target[i].ServerMac[0],
		    Targets->Target[i].ServerMac[1],
		    Targets->Target[i].ServerMac[2],
		    Targets->Target[i].ServerMac[3],
		    Targets->Target[i].ServerMac[4],
		    Targets->Target[i].ServerMac[5],
		    ( Targets->Target[i].LBASize / 2048 ) );
	      }
	  }
	free ( Targets );
	printf ( "Press enter to exit\n" );
	getchar (  );
	break;
      case CommandShow:
	if ( ( Disks =
	       ( PMOUNT_DISKS ) malloc ( sizeof ( MOUNT_DISKS ) +
					 ( 32 * sizeof ( MOUNT_DISK ) ) ) ) ==
	     NULL )
	  {
	    printf ( "Out of memory\n" );
	    break;
	  }
	if ( !DeviceIoControl
	     ( DeviceHandle, IOCTL_AOE_SHOW, NULL, 0, Disks,
	       ( sizeof ( MOUNT_DISKS ) + ( 32 * sizeof ( MOUNT_DISK ) ) ),
	       &BytesReturned, ( LPOVERLAPPED ) NULL ) )
	  {
	    printf ( "DeviceIoControl (%d)\n", ( int )GetLastError (  ) );
	    free ( Disks );
	    break;
	  }
	if ( Disks->Count == 0 )
	  {
	    printf ( "No AoE disks mounted.\n" );
	  }
	else
	  {
	    printf
	      ( "Disk  Client NIC         Server MAC         Target      Size\n" );
	    for ( i = 0; i < Disks->Count && i < 10; i++ )
	      {
		sprintf ( String, "e%lu.%lu      ", Disks->Disk[i].Major,
			  Disks->Disk[i].Minor );
		String[10] = 0;
		printf
		  ( " %-4lu %02x:%02x:%02x:%02x:%02x:%02x  %02x:%02x:%02x:%02x:%02x:%02x  %s  %I64uM\n",
		    Disks->Disk[i].Disk, Disks->Disk[i].ClientMac[0],
		    Disks->Disk[i].ClientMac[1], Disks->Disk[i].ClientMac[2],
		    Disks->Disk[i].ClientMac[3], Disks->Disk[i].ClientMac[4],
		    Disks->Disk[i].ClientMac[5], Disks->Disk[i].ServerMac[0],
		    Disks->Disk[i].ServerMac[1], Disks->Disk[i].ServerMac[2],
		    Disks->Disk[i].ServerMac[3], Disks->Disk[i].ServerMac[4],
		    Disks->Disk[i].ServerMac[5], String,
		    ( Disks->Disk[i].LBASize / 2048 ) );
	      }
	  }
	free ( Disks );
	printf ( "Press enter to exit\n" );
	getchar (  );
	break;
      case CommandMount:
	sscanf ( argv[2], "%02x:%02x:%02x:%02x:%02x:%02x", ( int * )&Mac[0],
		 ( int * )&Mac[1], ( int * )&Mac[2], ( int * )&Mac[3],
		 ( int * )&Mac[4], ( int * )&Mac[5] );
	sscanf ( argv[3], "%d", ( int * )&Major );
	sscanf ( argv[4], "%d", ( int * )&Minor );
	printf ( "mounting e%d.%d from %02x:%02x:%02x:%02x:%02x:%02x\n",
		 ( int )Major, ( int )Minor, Mac[0], Mac[1], Mac[2], Mac[3],
		 Mac[4], Mac[5] );
	memcpy ( &InBuffer[0], Mac, 6 );
	*( winvblock__uint16_ptr ) ( &InBuffer[6] ) =
	  ( winvblock__uint16 ) Major;
	*( winvblock__uint8_ptr ) ( &InBuffer[8] ) =
	  ( winvblock__uint8 ) Minor;
	if ( !DeviceIoControl
	     ( DeviceHandle, IOCTL_AOE_MOUNT, InBuffer, sizeof ( InBuffer ),
	       NULL, 0, &BytesReturned, ( LPOVERLAPPED ) NULL ) )
	  {
	    printf ( "DeviceIoControl (%d)\n", ( int )GetLastError (  ) );
	  }
	break;
      case CommandUmount:
	sscanf ( argv[2], "%d", ( int * )&Disk );
	printf ( "unmounting disk %d\n", ( int )Disk );
	memcpy ( &InBuffer, &Disk, 4 );
	if ( !DeviceIoControl
	     ( DeviceHandle, IOCTL_AOE_UMOUNT, InBuffer, sizeof ( InBuffer ),
	       NULL, 0, &BytesReturned, ( LPOVERLAPPED ) NULL ) )
	  {
	    printf ( "DeviceIoControl (%d)\n", ( int )GetLastError (  ) );
	  }
	break;
      case CommandAttach:
	{
	  mount__filedisk filedisk;

	  if ( argc < 6 )
	    {
	      printf ( "Too few parameters.\n" );
	      Help (  );
	      CloseHandle ( DeviceHandle );
	      return 1;
	    }
	  filedisk.type = *argv[3];
	  sscanf ( argv[4], "%d", ( int * )&filedisk.cylinders );
	  sscanf ( argv[5], "%d", ( int * )&filedisk.heads );
	  sscanf ( argv[6], "%d", ( int * )&filedisk.sectors );
	  memcpy ( &InBuffer, &filedisk, sizeof ( mount__filedisk ) );
	  memcpy ( &InBuffer[sizeof ( mount__filedisk )], argv[2],
		   strlen ( argv[2] ) + 1 );
	  if ( !DeviceIoControl
	       ( DeviceHandle, IOCTL_FILE_ATTACH, InBuffer,
		 sizeof ( InBuffer ), NULL, 0, &BytesReturned,
		 ( LPOVERLAPPED ) NULL ) )
	    {
	      printf ( "DeviceIoControl (%d)\n", ( int )GetLastError (  ) );
	    }
	  break;
	}
      case CommandDetach:
	sscanf ( argv[2], "%d", ( int * )&Disk );
	printf ( "Detaching file-backed disk %d\n", ( int )Disk );
	memcpy ( &InBuffer, &Disk, 4 );
	if ( !DeviceIoControl
	     ( DeviceHandle, IOCTL_FILE_DETACH, InBuffer, sizeof ( InBuffer ),
	       NULL, 0, &BytesReturned, ( LPOVERLAPPED ) NULL ) )
	  {
	    printf ( "DeviceIoControl (%d)\n", ( int )GetLastError (  ) );
	  }
	break;
    }
  CloseHandle ( DeviceHandle );

end:
  return 0;
}
