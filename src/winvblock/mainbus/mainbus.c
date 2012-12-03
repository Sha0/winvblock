/**
 * Copyright (C) 2009-2012, Shao Miller <sha0.miller@gmail.com>.
 * Copyright 2006-2008, V.
 * For WinAoE contact information, see http://winaoe.org/
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
 * Main bus specifics
 */

#include <stdio.h>
#include <ntddk.h>
#include <scsi.h>
#include <initguid.h>

#include "portable.h"
#include "winvblock.h"
#include "thread.h"
#include "wv_stdlib.h"
#include "wv_string.h"
#include "irp.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "registry.h"
#include "mount.h"
#include "x86.h"
#include "safehook.h"
#include "filedisk.h"
#include "dummy.h"
#include "memdisk.h"
#include "debug.h"

#include "mainbus.h"

/** Macros */

/* Names for the main bus */
#define WV_M_BUS_NAME (L"\\Device\\" WVL_M_WLIT)
#define WV_M_BUS_DOSNAME (L"\\DosDevices\\" WVL_M_WLIT)

/** Public function declarations */
DRIVER_INITIALIZE WvMainBusDriverEntry;

/** Function declarations */

static BOOLEAN WvMainBusPdoDone(IN UNICODE_STRING * RegistryPath);
/* TODO: DRIVER_ADD_DEVICE isn't available in DDK 3790.1830, it seems */
static DRIVER_ADD_DEVICE WvMainBusDriveDevice;
static DRIVER_UNLOAD WvMainBusUnload;
static VOID WvMainBusOldProbe(DEVICE_OBJECT * DeviceObject);
static NTSTATUS STDCALL WvBusEstablish(
    IN DRIVER_OBJECT * DriverObject,
    IN UNICODE_STRING * RegistryPath
  );

/** Public objects */

UNICODE_STRING WvBusDosName = {
    sizeof WV_M_BUS_DOSNAME - sizeof (WCHAR),
    sizeof WV_M_BUS_DOSNAME - sizeof (WCHAR),
    WV_M_BUS_DOSNAME
  };

/* The main bus */
WVL_S_BUS_T WvBus = {0};
WV_S_DEV_T WvBusDev = {0};

volatile LONG WvMainBusCreators;

/** Objects */

static UNICODE_STRING WvBusName = {
    sizeof WV_M_BUS_NAME - sizeof (WCHAR),
    sizeof WV_M_BUS_NAME - sizeof (WCHAR),
    WV_M_BUS_NAME
  };

static WVL_S_BUS_NODE WvBusSafeHookChild;

/** This mini-driver */
static S_WVL_MINI_DRIVER * WvMainBusMiniDriver;

/** The main bus */
static DEVICE_OBJECT * WvMainBusDevice;

/** The main bus type GUID */
DEFINE_GUID(
    GUID_MAIN_BUS_TYPE,
    0x2530ea73L,
    0x086b,
    0x11d1,
    0xa0,
    0x9f,
    0x00,
    0xc0,
    0x4f,
    0xc3,
    0x40,
    0xb1
  );

/** Function definitions */

/**
 * The mini-driver entry-point
 *
 * @param drv_obj
 *   The driver object provided by the caller
 *
 * @param reg_path
 *   The Registry path provided by the caller
 *
 * @return
 *   The status
 */
NTSTATUS STDCALL WvMainBusDriverEntry(
    IN DRIVER_OBJECT * drv_obj,
    IN UNICODE_STRING * reg_path
  ) {
    NTSTATUS status;

    /* Build the main bus' major dispatch table */
    WvMainBusBuildMajorDispatchTable();

    /* Register this mini-driver */
    status = WvlRegisterMiniDriver(
        &WvMainBusMiniDriver,
        drv_obj,
        WvMainBusDriveDevice,
        WvMainBusUnload
      );
    if (!NT_SUCCESS(status))
      goto err_register;
    ASSERT(WvMainBusMiniDriver);

    /* Create the bus PDO and FDO, if required */
    status = WvBusEstablish(drv_obj, reg_path);
    if (!NT_SUCCESS(status))
      goto err_bus;

    /* Do not introduce more resource acquisition, here */

    return status;

    /* Nothing to do.  See comment, above */
    err_bus:

    WvlDeregisterMiniDriver(WvMainBusMiniDriver);
    err_register:

    return status;
  }

/**
 * Drive a PDO with the main bus FDO
 *
 * @param DriverObject
 *   The driver object provided by the caller
 *
 * @param PhysicalDeviceObject
 *   The PDO to probe and attach the main bus FDO to
 *
 * @return
 *   The status of the operation
 */
static NTSTATUS STDCALL WvMainBusDriveDevice(
    IN DRIVER_OBJECT * drv_obj,
    IN DEVICE_OBJECT * pdo
  ) {
    LONG c;
    NTSTATUS status;
    DEVICE_OBJECT * fdo;
    S_WV_MAIN_BUS * bus;
    DEVICE_OBJECT * lower;

    ASSERT(drv_obj);
    ASSERT(pdo);

    /* Notify that we're in progress */
    c = InterlockedIncrement(&WvMainBusCreators);
    ASSERT(c > 0);

    /*
     * If another thread is trying to attach the main bus or if
     * the PDO belongs to our driver, reject the request
     */
    if (c > 1 || pdo->DriverObject == drv_obj) {
        DBG("Refusing to drive PDO %p\n", (VOID *) pdo);
        status = STATUS_NOT_SUPPORTED;
        goto err_params;
      }

    /* Initialize the bus */
    WvlBusInit(&WvBus);
    WvDevInit(&WvBusDev);

    /* Create the bus FDO */
    fdo = NULL;
    status = WvlCreateDevice(
        WvMainBusMiniDriver,
        sizeof *bus,
        &WvBusName,
        FILE_DEVICE_CONTROLLER,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &fdo
      );
    if (!NT_SUCCESS(status)) {
        DBG("WvlCreateDevice() failed!\n");
        goto err_fdo;
      }
    ASSERT(fdo);

    WvMainBusDevice = WvBusDev.Self = WvBus.Fdo = fdo;

    bus = fdo->DeviceExtension;
    ASSERT(bus);
    bus->PnpBusInfo->BusTypeGuid = GUID_MAIN_BUS_TYPE;
    bus->PnpBusInfo->LegacyBusType = PNPBus;
    bus->PnpBusInfo->BusNumber = 0;
    bus->BusRelations = NULL;
    /* TODO: Remove once proper PDO-add support is implemented */
    bus->BusRelationsHack = NULL;

    WvBusDev.IsBus = TRUE;
    WvBus.QueryDevText = WvBusPnpQueryDevText;
    WvDevForDevObj(WvBus.Fdo, &WvBusDev);
    WvDevSetIrpHandler(WvBus.Fdo, WvBusIrpDispatch);
    /* TODO: Review the following two settings */
    WvBus.Fdo->Flags |= DO_DIRECT_IO;
    WvBus.Fdo->Flags |= DO_POWER_INRUSH;
    #ifdef RIS
    WvBus.Dev.State = Started;
    #endif

    /* Attach the FDO to the PDO */
    lower = IoAttachDeviceToDeviceStack(fdo, pdo);
    if (!lower) {
        DBG("Error driving PDO %p!\n", (VOID *) pdo);
        goto err_lower;
      }
    bus->LowerDeviceObject = WvBus.LowerDeviceObject = lower;
    /* TODO: Remove these once this when the modules are mini-drivers */
    WvBus.Pdo = pdo;
    WvBus.State = WvlBusStateStarted;

    /* DosDevice symlink */
    status = IoCreateSymbolicLink(
        &WvBusDosName,
        &WvBusName
      );
    if (!NT_SUCCESS(status)) {
        DBG("IoCreateSymbolicLink() failed!\n");
        goto err_dos_symlink;
      }

    /* PDO created, FDO attached.  All done */
    WvBus.Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    DBG("Driving PDO %p\n", (VOID *) pdo);

    /* TODO: Mini-drivers will take care of adding themselves */
    WvMainBusOldProbe(fdo);

    return STATUS_SUCCESS;

    IoDeleteSymbolicLink(&WvBusDosName);
    err_dos_symlink:

    WvBus.State = WvlBusStateNotStarted;
    WvBus.Pdo = WvBus.LowerDeviceObject = NULL;
    IoDetachDevice(lower);
    err_lower:

    WvlDeleteDevice(fdo);
    WvMainBusDevice = WvBusDev.Self = WvBus.Fdo = NULL;
    err_fdo:

    err_params:
    c = InterlockedDecrement(&WvMainBusCreators);
    ASSERT(c >= 0);

    return status;
  }

/**
 * Release resources and unwind state
 *
 * @param DriverObject
 *   The driver object provided by the caller
 */
static VOID STDCALL WvMainBusUnload(IN DRIVER_OBJECT * drv_obj) {
    ASSERT(drv_obj);
    (VOID) drv_obj;
    WvlDeregisterMiniDriver(WvMainBusMiniDriver);
    return;
  }

/**
 * Probe for default children of the main bus
 *
 * @param DeviceObject
 *   The main bus FDO
 */
static VOID WvMainBusOldProbe(DEVICE_OBJECT * dev_obj) {
    S_X86_SEG16OFF16 int_13h;
    PDEVICE_OBJECT safe_hook_child;

    ASSERT(dev_obj);

    /* Probe for children */
    int_13h.Segment = 0;
    int_13h.Offset = 0x13 * sizeof int_13h;
    safe_hook_child = WvSafeHookProbe(&int_13h, dev_obj);
    if (safe_hook_child) {
        WvlBusInitNode(&WvBusSafeHookChild, safe_hook_child);
        WvlBusAddNode(&WvBus, &WvBusSafeHookChild);
      }
    WvMemdiskFind();
  }

/**
 * Establish the bus PDO and FDO, if required
 *
 * @param DriverObject
 *   The driver object provided by the caller
 *
 * @param RegistryPath
 *   The Registry path provided by the caller
 *
 * @return
 *   The status of the operation
 */
static NTSTATUS STDCALL WvBusEstablish(
    IN DRIVER_OBJECT * drv_obj,
    IN UNICODE_STRING * reg_path
  ) {
    NTSTATUS status;
    PDEVICE_OBJECT pdo = NULL;

    ASSERT(drv_obj);
    ASSERT(reg_path);

    if (WvMainBusPdoDone(reg_path))
      return STATUS_SUCCESS;

    /* If not, create a root-enumerated PDO for our bus */
    status = IoReportDetectedDevice(
        WvDriverObj,
        InterfaceTypeUndefined,
        -1,
        -1,
        NULL,
        NULL,
        FALSE,
        &pdo
      );
    if (!NT_SUCCESS(status)) {
        DBG("IoReportDetectedDevice() failed!\n");
        goto err_pdo;
      }
    ASSERT(pdo);

    /* Attach FDO to PDO */
    status = WvMainBusDriveDevice(drv_obj, pdo);
    if (!NT_SUCCESS(status)) {
        DBG("WvMainBusDriveDevice() failed!\n");
        goto err_attach;
      }

    status = STATUS_SUCCESS;
    goto out;

    err_attach:

    /* TODO: Should we really delete an IoReportDetectedDevice() device? */
    IoDeleteDevice(pdo);
    err_pdo:

    out:

    return status;
  }

/**
 * Check to see if the main bus PDO has been created
 *
 * @param RegistryPath
 *   The Registry path for the driver, provided by Windows
 *
 * @retval FALSE - A PDO has not been noted as having been created
 * @retval TRUE - A PDO has been noted as having been created
 */
static BOOLEAN WvMainBusPdoDone(IN UNICODE_STRING * reg_path) {
    HANDLE reg_key;
    BOOLEAN rv;
    UINT32 pdo_done;
    NTSTATUS status;

    /* Assume failure */
    rv = FALSE;

    /* Open our Registry path */
    ASSERT(reg_path);
    status = WvlRegOpenKey(reg_path->Buffer, &reg_key);
    if (!NT_SUCCESS(status)) {
        DBG("Couldn't open Registry path!\n");
        goto err_reg;
      }

    /*
     * Check the Registry to see if we've already got a PDO.
     * This entry is produced when the PDO has been properly
     * installed via the .INF file
     */
    pdo_done = 0;
    status = WvlRegFetchDword(reg_key, L"PdoDone", &pdo_done);
    if (NT_SUCCESS(status) && pdo_done) {
        DBG("PdoDone was set\n");
        rv = TRUE;
      }

    WvlRegCloseKey(reg_key);
    err_reg:

    return rv;
  }

/* TODO: Cosmetic changes */
BOOLEAN STDCALL WvBusAddDev(
    IN OUT WV_SP_DEV_T Dev
  ) {
    /* The new node's device object. */
    PDEVICE_OBJECT dev_obj;

    DBG("Entry\n");
    if (!WvBus.Fdo || !Dev) {
        DBG("No bus or no device!\n");
        return FALSE;
      }
    /* Create the child device, if needed. */
    dev_obj = Dev->Self;
    if (!dev_obj) {
        dev_obj = WvDevCreatePdo(Dev);
        if (!dev_obj) {
            DBG("PDO creation failed!\n");
            return FALSE;
          }
      }
    WvlBusInitNode(&Dev->BusNode, dev_obj);
    /* Associate the parent bus. */
    Dev->Parent = WvBus.Fdo;
    /*
     * Initialize the device.  For disks, this routine is responsible for
     * determining the disk's geometry appropriately for RAM/file disks.
     */
    if (Dev->Ops.Init)
      Dev->Ops.Init(Dev);
    dev_obj->Flags &= ~DO_DEVICE_INITIALIZING;
    /* Add the new PDO device to the bus' list of children. */
    WvlBusAddNode(&WvBus, &Dev->BusNode);

    DBG("Added device %p with PDO %p.\n", Dev, Dev->Self);
    return TRUE;
  }

/**
 * Remove a device node from the WinVBlock bus.
 *
 * @v Dev               The device to remove.
 * @ret NTSTATUS        The status of the operation.
 */
NTSTATUS STDCALL WvBusRemoveDev(IN WV_S_DEV_T * Dev) {
    NTSTATUS status;

    if (!Dev->BusNode.Linked) {
        WvDevClose(Dev);
        IoDeleteDevice(Dev->Self);
        WvDevFree(Dev);
      } else
        WvlBusRemoveNode(&Dev->BusNode);
    return STATUS_SUCCESS;
  }

/**
 * Probe the main bus for its initial set of PDOs
 *
 * @param DeviceObject
 *   The main bus device
 *
 * @retval STATUS_SUCCESS
 * @retval STATUS_INSUFFICIENT_RESOURCES
 *   Returned if the initial list could not be allocated from
 *   non-paged memory
 *
 * This function should _only_ be called if the current list is empty
 */
NTSTATUS WvMainBusInitialBusRelations(DEVICE_OBJECT * dev_obj) {
    S_WV_MAIN_BUS * bus;
    BOOLEAN need_alloc;
    DEVICE_RELATIONS * dev_relations;
    USHORT pdo_count;
    LIST_ENTRY * link;
    WVL_S_BUS_NODE * node;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(!bus->BusRelations);

    /*
     * TODO: Right now this is a hack until proper PDO-add support
     * is implemented.  With this hack, any user of the main bus'
     * BusRelations member must reset that member to NULL to ensure
     * we are called before each use of that member
     */
    dev_relations = bus->BusRelationsHack;
    pdo_count = WvBus.BusPrivate_.NodeCount;

    /* Do we need to allocate the list? */
    need_alloc = (
        !dev_relations ||
        dev_relations->Count < pdo_count
      );
    if (need_alloc) {
        bus->BusRelationsHack = NULL;
        wv_free(dev_relations);
        dev_relations = wv_malloc(
            sizeof *dev_relations +
            sizeof dev_relations->Objects[0] * (pdo_count - 1)
          );
        if (!dev_relations)
          return STATUS_INSUFFICIENT_RESOURCES;
      }

    /* Populate the list */
    dev_relations->Count = pdo_count;
    dev_relations->Objects[0] = NULL;
    for (
        pdo_count = 0, link = WvBus.BusPrivate_.Nodes.Flink;
        pdo_count < WvBus.BusPrivate_.NodeCount;
        ++pdo_count
      ) {
        ASSERT(link);
        ASSERT(link != &WvBus.BusPrivate_.Nodes);

        node = CONTAINING_RECORD(
            link,
            WVL_S_BUS_NODE,
            BusPrivate_.Link
          );
        ASSERT(node);

        dev_relations->Objects[pdo_count] = node->BusPrivate_.Pdo;
      }

    /* Save the list */
    bus->BusRelations = bus->BusRelationsHack = dev_relations;

    return STATUS_SUCCESS;
  }
