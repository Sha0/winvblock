/**
 * Copyright 2006-2008, V.
 * Portions copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
 * Driver installation
 *
 */

#include <stdio.h>
#include <windows.h>
#include <Setupapi.h>
#include <newdev.h>

#include "portable.h"
#include "winvblock.h"

#define MAX_CLASS_NAME_LEN 64

/* typedef BOOL WINAPI (*PROC)(HWND, LPCTSTR, LPCTSTR, DWORD, PBOOL OPTIONAL);
 */

static VOID
DisplayError (
  char *String
 )
{
  CHAR ErrorString[1024];
  UINT32 ErrorCode = GetLastError();

  printf ( "Error: %s\n", String );
  printf ( "GetLastError: 0x%08x (%d)\n", ErrorCode, ( int )ErrorCode );
  if ( FormatMessage
       ( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
	 ErrorCode, MAKELANGID ( LANG_NEUTRAL, SUBLANG_DEFAULT ),
	 ( LPTSTR ) ErrorString, sizeof ( ErrorString ), NULL ) )
    printf ( "%s", ErrorString );
}

int
main (
  int argc,
  char **argv,
  char **envp
 )
{
  HDEVINFO DeviceInfoSet = 0;
  SP_DEVINFO_DATA DeviceInfoData;
  GUID ClassGUID;
  TCHAR ClassName[MAX_CLASS_NAME_LEN];
  HINSTANCE Library;
  PROC UpdateDriverForPlugAndPlayDevicesA;
  BOOL RebootRequired = FALSE;
  TCHAR FullFilePath[1024];

  if ( !GetFullPathName
       ( "winvblk.inf", sizeof ( FullFilePath ), FullFilePath, NULL ) )
    goto GetFullPathNameError;
  if ( ( Library = LoadLibrary ( "newdev.dll" ) ) == NULL )
    goto LoadLibraryError;
  if ( ( UpdateDriverForPlugAndPlayDevicesA =
	 GetProcAddress ( Library,
			  "UpdateDriverForPlugAndPlayDevicesA" ) ) == NULL )
    goto GetProcAddressError;

  if ( !SetupDiGetINFClass
       ( FullFilePath, &ClassGUID, ClassName, sizeof ( ClassName ), 0 ) )
    goto GetINFClassError;
  if ( ( DeviceInfoSet =
	 SetupDiCreateDeviceInfoList ( &ClassGUID,
				       0 ) ) == INVALID_HANDLE_VALUE )
    goto CreateDeviceInfoListError;
  DeviceInfoData.cbSize = sizeof ( SP_DEVINFO_DATA );
  if ( !SetupDiCreateDeviceInfo
       ( DeviceInfoSet, ClassName, &ClassGUID, NULL, 0, DICD_GENERATE_ID,
	 &DeviceInfoData ) )
    goto CreateDeviceInfoError;
  if ( !SetupDiSetDeviceRegistryProperty
       ( DeviceInfoSet, &DeviceInfoData, SPDRP_HARDWAREID,
	 ( LPBYTE ) WVL_M_LIT "\0\0\0",
	 ( lstrlen ( WVL_M_LIT "\0\0\0" ) + 1 +
	   1 ) * sizeof ( TCHAR ) ) )
    goto SetDeviceRegistryPropertyError;
  if ( !SetupDiCallClassInstaller
       ( DIF_REGISTERDEVICE, DeviceInfoSet, &DeviceInfoData ) )
    goto CallClassInstallerREGISTERDEVICEError;

  if ( !UpdateDriverForPlugAndPlayDevices
       ( 0, WVL_M_LIT "\0\0\0", FullFilePath, INSTALLFLAG_FORCE,
	 &RebootRequired ) )
    {
      DWORD err = GetLastError (  );
      DisplayError ( "UpdateDriverForPlugAndPlayDevices" );
      if ( !SetupDiCallClassInstaller
	   ( DIF_REMOVE, DeviceInfoSet, &DeviceInfoData ) )
	DisplayError ( "CallClassInstaller(REMOVE)" );
      SetLastError ( err );
    }
  if ( RebootRequired )
    printf ( "Need reboot\n" );

  printf ( "Press enter to remove\n" );
  getchar (  );
  if ( !SetupDiCallClassInstaller
       ( DIF_REMOVE, DeviceInfoSet, &DeviceInfoData ) )
    DisplayError ( "CallClassInstaller(REMOVE)" );
  return 0;

GetFullPathNameError:
  DisplayError ( "GetFullPathName" );
  goto end;
LoadLibraryError:
  DisplayError ( "LoadLibrary" );
  goto end;
GetProcAddressError:
  DisplayError ( "GetProcAddress" );
  goto end;
GetINFClassError:
  DisplayError ( "GetINFClass" );
  goto end;
CreateDeviceInfoListError:
  DisplayError ( "CreateDeviceInfoList" );
  goto end;

CreateDeviceInfoError:
  DisplayError ( "CreateDeviceInfo" );
  goto cleanup_DeviceInfo;
SetDeviceRegistryPropertyError:
  DisplayError ( "SetDeviceRegistryProperty" );
  goto cleanup_DeviceInfo;
CallClassInstallerREGISTERDEVICEError:
  DisplayError ( "CallClassInstaller(REGISTERDEVICE)" );
  goto cleanup_DeviceInfo;

cleanup_DeviceInfo:
  SetupDiDestroyDeviceInfoList ( DeviceInfoSet );
end:
  printf ( "Press enter to exit\n" );
  getchar (  );
  return 0;
}
