/**
 * Copyright (C) 2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
 * AoE bus specifics.
 */

#include <stdio.h>
#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "driver.h"
#include "device.h"
#include "bus.h"
#include "aoe.h"
#include "mount.h"
#include "debug.h"

/* Names for the AoE bus. */
#define AOE_M_BUS_NAME_ (L"\\Device\\AoE")
#define AOE_M_BUS_DOSNAME_ (L"\\DosDevices\\AoE")

/* TODO: Remove this pull from aoe/driver.c */
extern WV_F_DEV_DISPATCH aoe__scan;
extern WV_F_DEV_DISPATCH aoe__show;
extern WV_F_DEV_DISPATCH aoe__mount;

/* Forward declarations. */
static WV_F_DEV_CTL AoeBusDevCtlDispatch_;
static WV_F_DEV_PNP_ID AoeBusPnpId_;
winvblock__bool AoeBusCreate(void);
void AoeBusFree(void);

/* Globals. */
WV_SP_BUS_T AoeBusMain = NULL;
static UNICODE_STRING AoeBusName_ = {
    sizeof AOE_M_BUS_NAME_,
    sizeof AOE_M_BUS_NAME_,
    AOE_M_BUS_NAME_
  };
static UNICODE_STRING AoeBusDosname_ = {
    sizeof AOE_M_BUS_DOSNAME_,
    sizeof AOE_M_BUS_DOSNAME_,
    AOE_M_BUS_DOSNAME_
  };

static NTSTATUS STDCALL AoeBusDevCtlDispatch_(
    IN WV_SP_DEV_T dev,
    IN PIRP irp,
    IN ULONG POINTER_ALIGNMENT code
  ) {
    switch(code) {
        case IOCTL_AOE_SCAN:
          return aoe__scan(dev, irp);

        case IOCTL_AOE_SHOW:
          return aoe__show(dev, irp);

        case IOCTL_AOE_MOUNT:
          return aoe__mount(dev, irp);

        case IOCTL_AOE_UMOUNT: {
            WV_SP_BUS_T bus = WvBusFromDev(dev);

            /* Pretend it's an IOCTL_FILE_DETACH. */
            return WvDevFromDevObj(bus->LowerDeviceObject)->IrpMj->DevCtl(
                dev,
                irp,
                IOCTL_FILE_DETACH
              );
          }

        default:
          DBG("Unsupported IOCTL\n");
          return driver__complete_irp(irp, 0, STATUS_NOT_SUPPORTED);
      }
  }

/**
 * Create the AoE bus.
 *
 * @ret         TRUE for success, else FALSE.
 */
winvblock__bool AoeBusCreate(void) {
    WV_SP_BUS_T new_bus;
    NTSTATUS status;

    /* We should only be called once. */
    if (AoeBusMain) {
        DBG("AoE bus already created\n");
        return FALSE;
      }
    /* Try to create the AoE bus. */
    new_bus = WvBusCreate();
    if (!new_bus) {
        DBG("Failed to create AoE bus!\n");
        goto err_new_bus;
      }
    /* When the PDO is created, we need to handle PnP ID queries. */
    new_bus->Dev.Ops.PnpId = AoeBusPnpId_;
    /* Add it as a sub-bus to WinVBlock. */
    if (!WvDriverBusAddDev(&new_bus->Dev)) {
        DBG("Couldn't add AoE bus to WinVBlock bus!\n");
        goto err_add_child;
      }
    /* DosDevice symlink. */
    status = IoCreateSymbolicLink(
        &AoeBusDosname_,
        &AoeBusName_
      );
    if (!NT_SUCCESS(status)) {
        DBG("IoCreateSymbolicLink() failed!\n");
        goto err_dos_symlink;
      }
    /* All done. */
    AoeBusMain = new_bus;
    return TRUE;

    IoDeleteSymbolicLink(&AoeBusDosname_);
    err_dos_symlink:

    IoDeleteDevice(AoeBusMain->Dev.Self);
    err_add_child:

    WvDevFree(&new_bus->Dev);
    err_new_bus:

    return FALSE;
  }

/* Destroy the AoE bus. */
void AoeBusFree(void) {
    if (!AoeBusMain)
      /* Nothing to do. */
      return;

    IoDeleteSymbolicLink(&AoeBusDosname_);
    IoDeleteDevice(AoeBusMain->Dev.Self);
    #if 0
    bus__remove_child(driver__bus(), &AoeBusMain->Dev);
    #endif
    WvDevFree(&AoeBusMain->Dev);
    return;
  }

static winvblock__uint32 STDCALL AoeBusPnpId_(
    IN WV_SP_DEV_T dev,
    IN BUS_QUERY_ID_TYPE query_type,
    IN OUT WCHAR (*buf)[512]
  ) {
    switch (query_type) {
        case BusQueryDeviceID:
          return swprintf(*buf, winvblock__literal_w L"\\AoE") + 1;

        case BusQueryInstanceID:
          return swprintf(*buf, L"0") + 1;

        case BusQueryHardwareIDs:
          return swprintf(*buf, winvblock__literal_w L"\\AoE") + 2;

        case BusQueryCompatibleIDs:
          return swprintf(*buf, winvblock__literal_w L"\\AoE") + 4;

        default:
          return 0;
      }
  }
