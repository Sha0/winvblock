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
static NTSTATUS STDCALL WvMainBusEstablish(
    IN DRIVER_OBJECT * DriverObject,
    IN UNICODE_STRING * RegistryPath
  );
static F_WVL_DEVICE_THREAD_FUNCTION WvMainBusAddDevice;
static F_WVL_DEVICE_THREAD_FUNCTION WvMainBusRemoveDevice;

/** Public objects */

UNICODE_STRING WvBusDosName = {
    sizeof WV_M_BUS_DOSNAME - sizeof (WCHAR),
    sizeof WV_M_BUS_DOSNAME - sizeof (WCHAR),
    WV_M_BUS_DOSNAME
  };

/* The main bus */
WVL_M_LIB WVL_S_BUS_T WvBus = {0};
WV_S_DEV_T WvBusDev = {0};

volatile LONG WvMainBusCreators;

/** This mini-driver */
S_WVL_MINI_DRIVER * WvMainBusMiniDriver;

/** Objects */

static UNICODE_STRING WvBusName = {
    sizeof WV_M_BUS_NAME - sizeof (WCHAR),
    sizeof WV_M_BUS_NAME - sizeof (WCHAR),
    WV_M_BUS_NAME
  };

static WVL_S_BUS_NODE WvBusSafeHookChild;

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
    status = WvMainBusEstablish(drv_obj, reg_path);
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
    bus->Flags = CvWvMainBusFlagIrpsHeld;
    bus->PhysicalDeviceObject = pdo;
    bus->PnpBusInfo->BusTypeGuid = GUID_MAIN_BUS_TYPE;
    bus->PnpBusInfo->LegacyBusType = PNPBus;
    bus->PnpBusInfo->BusNumber = 0;
    bus->BusRelations = NULL;
    WvlInitializeLockedList(bus->InitialProbeRegistrations);

    WvBusDev.IsBus = TRUE;
    WvDevForDevObj(WvBus.Fdo, &WvBusDev);
    WvDevSetIrpHandler(WvBus.Fdo, WvMainBusIrpDispatch);
    /* TODO: Review the following two settings */
    WvBus.Fdo->Flags |= DO_DIRECT_IO;
    WvBus.Fdo->Flags |= DO_POWER_INRUSH;
    #ifdef RIS
    WvBus.Dev.State = Started;
    #endif

    /* Attach the FDO to the PDO */
    if (!WvlAttachDeviceToDeviceStack(fdo, pdo)) {
        DBG("Error driving PDO %p!\n", (VOID *) pdo);
        goto err_lower;
      }
    WvBus.LowerDeviceObject = WvlGetLowerDeviceObject(fdo);
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

    return STATUS_SUCCESS;

    IoDeleteSymbolicLink(&WvBusDosName);
    err_dos_symlink:

    WvBus.State = WvlBusStateNotStarted;
    WvBus.Pdo = WvBus.LowerDeviceObject = NULL;
    WvlDetachDevice(fdo);
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
static NTSTATUS STDCALL WvMainBusEstablish(
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

    IoInvalidateDeviceRelations(pdo, BusRelations);

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
    DEVICE_RELATIONS * dev_relations;
    LIST_ENTRY * link;
    S_WVL_MAIN_BUS_PROBE_REGISTRATION * reg;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(!bus->BusRelations);

    /* Make an empty list */
    dev_relations = wv_malloc(sizeof *dev_relations);
    if (!dev_relations) {
        DBG("Couldn't allocate initial main bus relations\n");
        return STATUS_INSUFFICIENT_RESOURCES;
      }

    dev_relations->Count = 0;
    dev_relations->Objects[0] = NULL;
    bus->BusRelations = dev_relations;

    /* Invoke initial probe callbacks */
    WvlAcquireLockedList(bus->InitialProbeRegistrations);
    for (
        link = bus->InitialProbeRegistrations->List->Flink;
        link != bus->InitialProbeRegistrations->List;
        link = link->Flink
      ) {
        ASSERT(link);
        reg = CONTAINING_RECORD(
            link,
            S_WVL_MAIN_BUS_PROBE_REGISTRATION,
            Link
          );
        ASSERT(reg);
        ASSERT(reg->Callback);
        reg->Callback(dev_obj);
      }
    WvlReleaseLockedList(bus->InitialProbeRegistrations);
    WvMemdiskFind();

    return STATUS_SUCCESS;
  }

WVL_M_LIB NTSTATUS STDCALL WvlAddDeviceToMainBus(
    IN DEVICE_OBJECT * dev_obj
  ) {
    S_WV_MAIN_BUS * bus;
    NTSTATUS status;

    ASSERT(dev_obj);
    ASSERT(WvMainBusDevice);
    bus = WvMainBusDevice->DeviceExtension;
    ASSERT(bus);

    status = WvlCallFunctionInDeviceThread(
        WvMainBusDevice,
        WvMainBusAddDevice,
        dev_obj,
        TRUE
      );
    if (NT_SUCCESS(status) && !WvlInDeviceThread(WvMainBusDevice)) {
        ASSERT(bus->PhysicalDeviceObject);
        IoInvalidateDeviceRelations(bus->PhysicalDeviceObject, BusRelations);
      }
    return status;
  }

/**
 * Add a device to the main bus
 *
 * @param DeviceObject
 *   The main bus device
 *
 * @param Context
 *   The device to be added
 *
 * @retval STATUS_SUCCESS
 * @retval STATUS_INVALID_PARAMETER - The child device could not be assigned
 * @retval STATUS_INSUFFICIENT_RESOURCES
 */
static NTSTATUS WvMainBusAddDevice(
    IN DEVICE_OBJECT * dev_obj,
    IN VOID * context
  ) {
    DEVICE_OBJECT * const new_dev_obj = context;
    S_WV_MAIN_BUS * bus;
    WV_S_DEV_EXT * new_dev_ext;
    LONG flags;
    NTSTATUS status;
    DEVICE_RELATIONS * dev_relations;
    ULONG i;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(new_dev_obj);
    new_dev_ext = new_dev_obj->DeviceExtension;
    ASSERT(new_dev_ext);

    /* TODO: Remove this check when everything is mini-driver */
    if (new_dev_ext->MiniDriver)
    if (!WvlAssignDeviceToBus(new_dev_obj, dev_obj)) {
        status = STATUS_INVALID_PARAMETER;
        goto err_assign;
      }

    status = WvlWaitForActiveIrps(dev_obj);
    if (!NT_SUCCESS(status))
      goto err_wait;

    if (!bus->BusRelations) {
        status = WvMainBusInitialBusRelations(dev_obj);
        if (!NT_SUCCESS(status))
          goto err_bus_relations;
      }
    ASSERT(bus->BusRelations);

    dev_relations = wv_malloc(
        sizeof *dev_relations +
        sizeof dev_relations->Objects[0] * bus->BusRelations->Count
      );
    if (!dev_relations) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_dev_relations;
      }

    dev_relations->Count = bus->BusRelations->Count + 1;
    for (i = 0; i < bus->BusRelations->Count; ++i)
      dev_relations->Objects[i] = bus->BusRelations->Objects[i];
    dev_relations->Objects[i] = new_dev_obj;

    wv_free(bus->BusRelations);
    bus->BusRelations = dev_relations;
    return STATUS_SUCCESS;

    err_dev_relations:

    err_bus_relations:

    err_wait:

    /* Best effort */
    WvlAssignDeviceToBus(new_dev_obj, NULL);
    err_assign:

    return status;
  }

WVL_M_LIB VOID STDCALL WvlRemoveDeviceFromMainBus(IN DEVICE_OBJECT * dev_obj) {
    S_WV_MAIN_BUS * bus;
    NTSTATUS status;

    ASSERT(dev_obj);
    ASSERT(WvMainBusDevice);
    bus = WvMainBusDevice->DeviceExtension;
    ASSERT(bus);

    status = WvlCallFunctionInDeviceThread(
        WvMainBusDevice,
        WvMainBusRemoveDevice,
        dev_obj,
        TRUE
      );
    if (NT_SUCCESS(status) && !WvlInDeviceThread(WvMainBusDevice)) {
        ASSERT(bus->PhysicalDeviceObject);
        IoInvalidateDeviceRelations(bus->PhysicalDeviceObject, BusRelations);
      }
  }

/**
 * Remove a device from the main bus
 *
 * @param DeviceObject
 *   The main bus device
 *
 * @param Context
 *   The device to be removed
 *
 * @retval STATUS_SUCCESS
 * @retval STATUS_NO_SUCH_DEVICE
 * @retval STATUS_UNSUCCESSFUL
 */
static NTSTATUS WvMainBusRemoveDevice(
    IN DEVICE_OBJECT * dev_obj,
    IN VOID * context
  ) {
    DEVICE_OBJECT * const rem_dev_obj = context;
    S_WV_MAIN_BUS * bus;
    WV_S_DEV_EXT * rem_dev_ext;
    NTSTATUS status;
    ULONG i;
    UCHAR found;
    LONG flags;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(rem_dev_obj);
    rem_dev_ext = rem_dev_obj->DeviceExtension;
    ASSERT(rem_dev_ext);
    ASSERT(rem_dev_ext->ParentBusDeviceObject == dev_obj);

    status = WvlWaitForActiveIrps(dev_obj);
    if (!NT_SUCCESS(status))
      goto err_wait;

    ASSERT(bus->BusRelations);

    for (i = found = 0; i < bus->BusRelations->Count; ++i) {
        if (bus->BusRelations->Objects[i] == rem_dev_obj) {
            found = 1;
            if (i + 1 == bus->BusRelations->Count) {
                bus->BusRelations->Objects[i] = NULL;
                break;
              }
          }
        bus->BusRelations->Objects[i] = bus->BusRelations->Objects[i + found];
      }
    ASSERT(found);
    bus->BusRelations->Count -= found;

    if (!found || !WvlAssignDeviceToBus(rem_dev_obj, NULL)) {
        status = STATUS_UNSUCCESSFUL;
        goto err_found;
      }

    /* TODO: Remove this check when everything is mini-driver */
    if (rem_dev_ext->MiniDriver)
    /* If linked, we take responsibility for deletion, too */
    WvlDeleteDevice(rem_dev_obj);

    return STATUS_SUCCESS;

    err_found:

    err_wait:

    return status;
  }

WVL_M_LIB VOID STDCALL WvlRegisterMainBusInitialProbeCallback(
    IN S_WVL_MAIN_BUS_PROBE_REGISTRATION * reg
  ) {
    S_WV_MAIN_BUS * bus;

    ASSERT(reg);
    ASSERT(reg->Callback);
    ASSERT(WvMainBusDevice);
    bus = WvMainBusDevice->DeviceExtension;
    ASSERT(bus);
    WvlAppendLockedListLink(bus->InitialProbeRegistrations, reg->Link);
  }
