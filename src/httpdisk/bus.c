/**
 * Copyright (C) 2010-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 *
 * This file is part of WinVBlock, originally derived from WinAoE.
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
 * HTTPDisk bus specifics.
 */

#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "debug.h"
#include "dummy.h"

/** From httpdisk.c */
extern PDRIVER_OBJECT HttpdiskDriverObj;

/** Exports. */
NTSTATUS STDCALL HttpdiskBusEstablish(void);
VOID HttpdiskBusCleanup(void);
DRIVER_ADD_DEVICE HttpdiskBusAttach;

/** Private. */
static NTSTATUS STDCALL HttpdiskBusCreateFdo_(void);
static NTSTATUS STDCALL HttpdiskBusCreatePdo_(void);
static VOID HttpdiskBusDeleteFdo_(void);

/* The HTTPDisk bus FDO. */
static PDEVICE_OBJECT HttpdiskBusFdo_ = NULL;

/* Names for the HTTPDisk bus. */
#define HTTPDISK_M_BUS_NAME_ (L"\\Device\\HTTPDisk")
#define HTTPDISK_M_BUS_DOSNAME_ (L"\\DosDevices\\HTTPDisk")
static UNICODE_STRING HttpdiskBusName_ = {
    sizeof HTTPDISK_M_BUS_NAME_ - sizeof (WCHAR),
    sizeof HTTPDISK_M_BUS_NAME_ - sizeof (WCHAR),
    HTTPDISK_M_BUS_NAME_
  };
static UNICODE_STRING HttpdiskBusDosname_ = {
    sizeof HTTPDISK_M_BUS_DOSNAME_ - sizeof (WCHAR),
    sizeof HTTPDISK_M_BUS_DOSNAME_ - sizeof (WCHAR),
    HTTPDISK_M_BUS_DOSNAME_
  };

NTSTATUS STDCALL HttpdiskBusEstablish(void) {
    NTSTATUS status;

    status = HttpdiskBusCreateFdo_();
    if (!NT_SUCCESS(status))
      goto err_fdo;

    status = HttpdiskBusCreatePdo_();
    if (!NT_SUCCESS(status))
      goto err_pdo;

    DBG("Bus established.\n");
    return STATUS_SUCCESS;

    HttpdiskBusDeleteFdo_();
    err_fdo:

    /* TODO: Remove the PDO. */
    err_pdo:

    DBG("Bus not established.\n");
    return status;
  }

VOID HttpdiskBusCleanup(void) {
    HttpdiskBusDeleteFdo_();
    return;
  }

NTSTATUS HttpdiskBusAttach(
    IN PDRIVER_OBJECT DriverObj,
    IN PDEVICE_OBJECT Pdo
  ) {
    return STATUS_NOT_SUPPORTED;
  }

static NTSTATUS STDCALL HttpdiskBusCreateFdo_(void) {
    NTSTATUS status;

    status = IoCreateDevice(
        HttpdiskDriverObj,
        0,
        &HttpdiskBusName_,
        FILE_DEVICE_CONTROLLER,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &HttpdiskBusFdo_
      );
    if (!NT_SUCCESS(status)) {
        DBG("FDO not created.\n");
        return status;
      }

    DBG("FDO created: %p.\n", (PVOID) HttpdiskBusFdo_);
    return STATUS_SUCCESS;
  }

static NTSTATUS STDCALL HttpdiskBusCreatePdo_(void) {
    NTSTATUS status;

    /* Generate dummy IDs for the HTTPDisk bus PDO. */
    WV_M_DUMMY_ID_GEN(
        static const,
        HttpdiskBusDummyIds_,
        WVL_M_WLIT L"\\HTTPDisk",
        L"0",
        WVL_M_WLIT L"\\HTTPDisk\0",
        WVL_M_WLIT L"\\HTTPDisk\0",
        FILE_DEVICE_CONTROLLER,
        FILE_DEVICE_SECURE_OPEN
      );

    status = WvDummyAdd(&HttpdiskBusDummyIds_.DummyIds);
    if (!NT_SUCCESS(status)) {
        DBG("PDO not created.\n");
        return status;
      }

    DBG("PDO created.\n");
    return STATUS_SUCCESS;
  }

static VOID HttpdiskBusDeleteFdo_(void) {
    if (!HttpdiskBusFdo_)
      return;
    IoDeleteDevice(HttpdiskBusFdo_);
    HttpdiskBusFdo_ = NULL;
    DBG("FDO %p deleted.\n", (PVOID) HttpdiskBusFdo_);
    return;
  }
