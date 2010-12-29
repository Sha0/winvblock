/*
    HTTP Virtual Disk.
    Copyright (C) 2006 Bo Brantén.
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef _HTTP_DISK_
#define _HTTP_DISK_

#ifndef __T
#ifdef _NTDDK_
#define __T(x)  L ## x
#else
#define __T(x)  x
#endif
#endif

#ifndef _T
#define _T(x)   __T(x)
#endif

#define DEVICE_BASE_NAME    _T("\\HttpDisk")
#define DEVICE_DIR_NAME     _T("\\Device")      DEVICE_BASE_NAME
#define DEVICE_NAME_PREFIX  DEVICE_DIR_NAME     _T("\\Http")

#define FILE_DEVICE_HTTP_DISK       0x8000

#define IOCTL_HTTP_DISK_CONNECT     CTL_CODE(FILE_DEVICE_HTTP_DISK, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_HTTP_DISK_DISCONNECT  CTL_CODE(FILE_DEVICE_HTTP_DISK, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

typedef struct _HTTP_DISK_INFORMATION {
    ULONG   Address;
    USHORT  Port;
    USHORT  HostNameLength;
    UCHAR   HostName[256];
    USHORT  FileNameLength;
    UCHAR   FileName[1];
} HTTP_DISK_INFORMATION, *PHTTP_DISK_INFORMATION;

#endif
