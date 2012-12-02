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

/* Names for the main bus. */
#define WV_M_BUS_NAME (L"\\Device\\" WVL_M_WLIT)
#define WV_M_BUS_DOSNAME (L"\\DosDevices\\" WVL_M_WLIT)

/** Object types */
typedef struct S_WV_MAIN_BUS S_WV_MAIN_BUS;

/** External functions */

/* From ../libbus/pnp.c */
extern NTSTATUS STDCALL WvlBusPnpRemoveDev(IN WVL_SP_BUS_T Bus, IN PIRP Irp);
extern NTSTATUS STDCALL WvlBusPnpStartDev(IN WVL_SP_BUS_T Bus, IN PIRP Irp);

/** Public functions */
NTSTATUS STDCALL WvBusDevCtl(IN PIRP, IN ULONG POINTER_ALIGNMENT);
WVL_F_BUS_PNP WvBusPnpQueryDevText;

/** Public objects */
UNICODE_STRING WvBusName = {
    sizeof WV_M_BUS_NAME - sizeof (WCHAR),
    sizeof WV_M_BUS_NAME - sizeof (WCHAR),
    WV_M_BUS_NAME
  };
UNICODE_STRING WvBusDosName = {
    sizeof WV_M_BUS_DOSNAME - sizeof (WCHAR),
    sizeof WV_M_BUS_DOSNAME - sizeof (WCHAR),
    WV_M_BUS_DOSNAME
  };
/* The main bus. */
WVL_S_BUS_T WvBus = {0};
WV_S_DEV_T WvBusDev = {0};

/** Private function declarations */

/* TODO: DRIVER_ADD_DEVICE isn't available in DDK 3790.1830, it seems */
static DRIVER_ADD_DEVICE WvMainBusDriveDevice;
static DRIVER_UNLOAD WvMainBusUnload;
static VOID WvMainBusOldProbe(DEVICE_OBJECT * DeviceObject);
static NTSTATUS STDCALL WvBusEstablish(
    IN DRIVER_OBJECT * DriverObject,
    IN UNICODE_STRING * RegistryPath
  );
static NTSTATUS WvMainBusInitialBusRelations(DEVICE_OBJECT * DeviceObject);

/** Main bus IRP dispatchers */
static DRIVER_DISPATCH WvBusIrpDispatch;
static __drv_dispatchType(IRP_MJ_PNP) DRIVER_DISPATCH WvMainBusDispatchPnpIrp;
static __drv_dispatchType(IRP_MJ_DEVICE_CONTROL) DRIVER_DISPATCH
  WvMainBusDispatchDeviceControlIrp;
static __drv_dispatchType(IRP_MJ_POWER) DRIVER_DISPATCH
  WvMainBusDispatchPowerIrp;
static __drv_dispatchType(IRP_MJ_CREATE) __drv_dispatchType(IRP_MJ_CLOSE)
  DRIVER_DISPATCH WvMainBusDispatchCreateCloseIrp;
static __drv_dispatchType(IRP_MJ_SYSTEM_CONTROL) DRIVER_DISPATCH
  WvMainBusDispatchSystemControlIrp;

/** PnP handlers */
static __drv_dispatchType(IRP_MN_QUERY_BUS_INFORMATION) DRIVER_DISPATCH
  WvMainBusPnpQueryBusInfo;
static __drv_dispatchType(IRP_MN_QUERY_CAPABILITIES) DRIVER_DISPATCH
  WvMainBusPnpQueryCapabilities;
static __drv_dispatchType(IRP_MN_QUERY_DEVICE_RELATIONS) DRIVER_DISPATCH
  WvMainBusPnpQueryDeviceRelations;

/** Struct/union type definitions */

/** The bus extension type */
struct S_WV_MAIN_BUS {
    /** This must be the first member of all extension types */
    WV_S_DEV_EXT DeviceExtension[1];

    /** The lower device this FDO is attached to, if any */
    DEVICE_OBJECT * LowerDeviceObject;

    /** PnP bus information */
    PNP_BUS_INFORMATION PnpBusInfo[1];

    /** PnP bus relations */
    DEVICE_RELATIONS * BusRelations;

    /** Hack until proper PDO-add support is implemented */
    DEVICE_RELATIONS * BusRelationsHack;
  };

/** Private objects */

static WVL_S_BUS_NODE WvBusSafeHookChild;

/** This mini-driver */
static S_WVL_MINI_DRIVER * WvMainBusMiniDriver;

/** The main bus */
static DEVICE_OBJECT * WvMainBusDevice;

static A_WVL_MJ_DISPATCH_TABLE WvMainBusMajorDispatchTable;

static volatile LONG WvMainBusCreators;

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
    WvMainBusMajorDispatchTable[IRP_MJ_PNP] = WvMainBusDispatchPnpIrp;
    WvMainBusMajorDispatchTable[IRP_MJ_DEVICE_CONTROL] =
      WvMainBusDispatchDeviceControlIrp;
    WvMainBusMajorDispatchTable[IRP_MJ_POWER] = WvMainBusDispatchPowerIrp;
    WvMainBusMajorDispatchTable[IRP_MJ_CREATE] =
      WvMainBusDispatchCreateCloseIrp;
    WvMainBusMajorDispatchTable[IRP_MJ_CLOSE] =
      WvMainBusDispatchCreateCloseIrp;
    WvMainBusMajorDispatchTable[IRP_MJ_SYSTEM_CONTROL] =
      WvMainBusDispatchSystemControlIrp;

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
 * Handle an IRP from the main bus' thread context
 *
 * @param DeviceObject
 *   The main bus device
 *
 * @param Irp
 *   The IRP to process
 *
 * @return
 *   The status of the operation
 */
static NTSTATUS STDCALL WvBusIrpDispatch(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp
  ) {
    PIO_STACK_LOCATION io_stack_loc;

    ASSERT(dev_obj);
    ASSERT(irp);

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    /* Handle a known IRP major type */
    if (WvMainBusMajorDispatchTable[io_stack_loc->MajorFunction]) {
        return WvMainBusMajorDispatchTable[io_stack_loc->MajorFunction](
            dev_obj,
            irp
          );
      }

    /* Handle an unknown type */
    return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);
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
    HANDLE reg_key;
    UINT32 pdo_done = 0;
    PDEVICE_OBJECT pdo = NULL;

    ASSERT(drv_obj);
    ASSERT(reg_path);

    /* Open our Registry path */
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
    status = WvlRegFetchDword(reg_key, L"PdoDone", &pdo_done);
    if (NT_SUCCESS(status) && pdo_done) {
        status = STATUS_SUCCESS;
        goto out;
      }

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

    WvlRegCloseKey(reg_key);
    err_reg:

    return status;
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
NTSTATUS STDCALL WvBusRemoveDev(IN WV_SP_DEV_T Dev) {
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
static NTSTATUS WvMainBusInitialBusRelations(DEVICE_OBJECT * dev_obj) {
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

static NTSTATUS STDCALL WvBusDevCtlDetach(
    IN PIRP irp
  ) {
    UINT32 unit_num;
    WVL_SP_BUS_NODE walker;
    WV_SP_DEV_T dev = NULL;

    unit_num = *((PUINT32) irp->AssociatedIrp.SystemBuffer);
    DBG("Request to detach unit %d...\n", unit_num);

    walker = NULL;
    /* For each node on the bus... */
    WvlBusLock(&WvBus);
    while (walker = WvlBusGetNextNode(&WvBus, walker)) {
        /* If the unit number matches... */
        if (WvlBusGetNodeNum(walker) == unit_num) {
            dev = WvDevFromDevObj(WvlBusGetNodePdo(walker));
            /* If it's not a boot-time device... */
            if (dev->Boot) {
                DBG("Cannot detach a boot-time device.\n");
                /* Signal error. */
                dev = NULL;
              }
            break;
          }
      }
    WvlBusUnlock(&WvBus);
    if (!dev) {
        DBG("Unit %d not found.\n", unit_num);
        return WvlIrpComplete(irp, 0, STATUS_INVALID_PARAMETER);
      }
    /* Detach the node. */
    WvBusRemoveDev(dev);
    DBG("Removed unit %d.\n", unit_num);
    return WvlIrpComplete(irp, 0, STATUS_SUCCESS);
  }

NTSTATUS STDCALL WvBusDevCtl(
    IN PIRP irp,
    IN ULONG POINTER_ALIGNMENT code
  ) {
    NTSTATUS status;

    switch (code) {
        case IOCTL_FILE_ATTACH:
          status = WvFilediskAttach(irp);
          break;

        case IOCTL_FILE_DETACH:
          return WvBusDevCtlDetach(irp);

        case IOCTL_WV_DUMMY:
          return WvDummyIoctl(irp);

        default:
          irp->IoStatus.Information = 0;
          status = STATUS_INVALID_DEVICE_REQUEST;
      }

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

NTSTATUS STDCALL WvBusPnpQueryDevText(
    IN WVL_SP_BUS_T bus,
    IN PIRP irp
  ) {
    WCHAR (*str)[512];
    PIO_STACK_LOCATION io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status;
    UINT32 str_len;

    /* Allocate a string buffer. */
    str = wv_mallocz(sizeof *str);
    if (str == NULL) {
        DBG("wv_malloc IRP_MN_QUERY_DEVICE_TEXT\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto alloc_str;
      }
    /* Determine the query type. */
    switch (io_stack_loc->Parameters.QueryDeviceText.DeviceTextType) {
        case DeviceTextDescription:
          str_len = swprintf(*str, WVL_M_WLIT L" Bus") + 1;
          irp->IoStatus.Information =
            (ULONG_PTR) wv_palloc(str_len * sizeof **str);
          if (irp->IoStatus.Information == 0) {
              DBG("wv_palloc DeviceTextDescription\n");
              status = STATUS_INSUFFICIENT_RESOURCES;
              goto alloc_info;
            }
          RtlCopyMemory(
              (PWCHAR) irp->IoStatus.Information,
              str,
              str_len * sizeof **str
            );
          status = STATUS_SUCCESS;
          goto alloc_info;

        default:
          irp->IoStatus.Information = 0;
          status = STATUS_NOT_SUPPORTED;
      }
    /* irp->IoStatus.Information not freed. */
    alloc_info:

    wv_free(str);
    alloc_str:

    return WvlIrpComplete(irp, irp->IoStatus.Information, status);
  }

WVL_M_LIB DEVICE_OBJECT * WvBusFdo(void) {
    return WvBus.Fdo;
  }

/**
 * Signal the main bus that it's time to go away
 */
VOID WvMainBusRemove(void) {
    S_WV_MAIN_BUS * bus;
    LONG c;

    /* This will be called from the main bus' thread */
    ASSERT(WvMainBusDevice);
    bus = WvMainBusDevice->DeviceExtension;
    ASSERT(bus);

    IoDeleteSymbolicLink(&WvBusDosName);

    wv_free(bus->BusRelationsHack);
    
    WvlDeleteDevice(WvMainBusDevice);

    /*
     * TODO: Put this somewhere where it doesn't race with another
     * thread calling WvMainBusDriveDevice
     */
    c = InterlockedDecrement(&WvMainBusCreators);
    ASSERT(c >= 0);
  }

/** PnP IRP dispatcher */
static NTSTATUS WvMainBusDispatchPnpIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WV_MAIN_BUS * bus;
    IO_STACK_LOCATION * io_stack_loc;
    UCHAR code;
    NTSTATUS status;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    code = io_stack_loc->MinorFunction;

    switch (code) {
        case IRP_MN_QUERY_DEVICE_TEXT:
        DBG("IRP_MN_QUERY_DEVICE_TEXT\n");
        if (WvBus.QueryDevText)
          return WvBus.QueryDevText(&WvBus, irp);
        return WvlIrpComplete(irp, 0, STATUS_NOT_SUPPORTED);

        case IRP_MN_QUERY_BUS_INFORMATION:
        DBG("IRP_MN_QUERY_BUS_INFORMATION\n");
        return WvMainBusPnpQueryBusInfo(dev_obj, irp);

        case IRP_MN_QUERY_DEVICE_RELATIONS:
        DBG("IRP_MN_QUERY_DEVICE_RELATIONS\n");
        return WvMainBusPnpQueryDeviceRelations(dev_obj, irp);

        case IRP_MN_QUERY_CAPABILITIES:
        DBG("IRP_MN_QUERY_CAPABILITIES\n");
        return WvMainBusPnpQueryCapabilities(dev_obj, irp);

        case IRP_MN_REMOVE_DEVICE:
        DBG("IRP_MN_REMOVE_DEVICE\n");
        return WvlBusPnpRemoveDev(&WvBus, irp);

        case IRP_MN_START_DEVICE:
        DBG("IRP_MN_START_DEVICE\n");
        return WvlBusPnpStartDev(&WvBus, irp);

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
        DBG("IRP_MN_QUERY_PNP_DEVICE_STATE\n");
        irp->IoStatus.Information = 0;
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_QUERY_STOP_DEVICE:
        DBG("IRP_MN_QUERY_STOP_DEVICE\n");
        WvBus.OldState = WvBus.State;
        WvBus.State = WvlBusStateStopPending;
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_CANCEL_STOP_DEVICE:
        DBG("IRP_MN_CANCEL_STOP_DEVICE\n");
        WvBus.State = WvBus.OldState;
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_STOP_DEVICE:
        DBG("IRP_MN_STOP_DEVICE\n");
        WvBus.OldState = WvBus.State;
        WvBus.State = WvlBusStateStopped;
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_QUERY_REMOVE_DEVICE:
        DBG("IRP_MN_QUERY_REMOVE_DEVICE\n");
        WvBus.OldState = WvBus.State;
        WvBus.State = WvlBusStateRemovePending;
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
        DBG("IRP_MN_CANCEL_REMOVE_DEVICE\n");
        WvBus.State = WvBus.OldState;
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_SURPRISE_REMOVAL:
        DBG("IRP_MN_SURPRISE_REMOVAL\n");
        WvBus.OldState = WvBus.State;
        WvBus.State = WvlBusStateSurpriseRemovePending;
        status = STATUS_SUCCESS;
        break;

        case IRP_MN_QUERY_RESOURCES:
        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
        DBG("IRP_MN_QUERY_RESOURCE*\n");
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;

        default:
        DBG("Unhandled IRP_MN_*: %d\n", code);
        status = irp->IoStatus.Status;
      }

    irp->IoStatus.Status = status;
    IoSkipCurrentIrpStackLocation(irp);
    ASSERT(bus->LowerDeviceObject);
    return IoCallDriver(bus->LowerDeviceObject, irp);
  }

/**
 * IRP_MJ_PNP:IRP_MN_QUERY_BUS_INFORMATION handler
 *
 * IRQL == PASSIVE_LEVEL, any thread
 * Do not send this IRP
 * Completed by PDO
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *     Irp->IoStatus.Information populated from paged memory
 *   Error:
 *     Irp->IoStatus.Status == STATUS_INSUFFICIENT_RESOURCES
 *     Irp->IoStatus.Information == 0
 *
 * TODO: Not handled by function drivers, so what is it doing here?!
 */
static NTSTATUS STDCALL WvMainBusPnpQueryBusInfo(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WV_MAIN_BUS * bus;
    VOID * pnp_bus_info;
    NTSTATUS status;

    ASSERT(dev_obj);
    ASSERT(irp);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);

    /* Allocate for a copy of the bus info */
    pnp_bus_info = wv_palloc(sizeof bus->PnpBusInfo);
    if (!pnp_bus_info) {
        DBG("wv_palloc failed\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        irp->IoStatus.Information = 0;
        goto pnp_bus_info;
      }

    /* Copy the bus info */
    RtlCopyMemory(pnp_bus_info, bus->PnpBusInfo, sizeof bus->PnpBusInfo);

    irp->IoStatus.Information = (ULONG_PTR) pnp_bus_info;
    status = STATUS_SUCCESS;

    /* irp-IoStatus.Information (pnp_bus_info) not freed. */
    pnp_bus_info:

    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

/**
 * IRP_MJ_PNP:IRP_MN_QUERY_CAPABILITIES handler
 *
 * IRQL == PASSIVE_LEVEL, any thread
 * Ok to send this IRP
 * Completed by PDO, can be hooked
 *
 * @param Parameters.DeviceCapabilities.Capabilities
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *     I/O stack location: Parameters.DeviceCapabilities.Capabilities
 *       populated
 *   Error:
 *     Irp->IoStatus.Status == STATUS_UNSUCCESSFUL
 */
static NTSTATUS STDCALL WvMainBusPnpQueryCapabilities(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WV_MAIN_BUS * bus;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(irp);

    /* Let the lower device handle the IRP */
    IoSkipCurrentIrpStackLocation(irp);
    ASSERT(bus->LowerDeviceObject);
    return IoCallDriver(bus->LowerDeviceObject, irp);
  }

/**
 * IRP_MJ_PNP:IRP_MN_QUERY_DEVICE_RELATIONS handler
 *
 * IRQL == PASSIVE_LEVEL
 * Completed by PDO, must accumulate relations downwards (upwards optional)
 * BusRelations: system thread
 *   Yes: FDO
 *   Maybe: filter
 * TargetDeviceRelation: any thread
 *   Yes: PDO
 * RemovalRelations: system thread
 *   Maybe: FDO
 *   Maybe: filter
 * PowerRelations >= Windows 7: system thread
 *   Maybe: FDO
 *   Maybe: filter
 * EjectionRelations: system thread
 *   Maybe: PDO
 * Ok to send this IRP
 * Completed by PDO
 *
 * @param Parameters.QueryDeviceRelations.Type (I/O stack location)
 *
 * @param FileObject (I/O stack location)
 *
 * @return
 *   Success:
 *     Irp->IoStatus.Status == STATUS_SUCCESS
 *     Irp->IoStatus.Information populated from paged memory
 *   Error:
 *     Irp->IoStatus.Status == STATUS_INSUFFICIENT_RESOURCES
 */
static NTSTATUS STDCALL WvMainBusPnpQueryDeviceRelations(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    S_WV_MAIN_BUS * bus;
    IO_STACK_LOCATION * io_stack_loc;
    NTSTATUS status;
    DEVICE_RELATIONS * dev_relations;
    DEVICE_RELATIONS * higher_dev_relations;
    DEVICE_RELATIONS * new_dev_relations;
    ULONG pdo_count;

    ASSERT(dev_obj);
    bus = dev_obj->DeviceExtension;
    ASSERT(bus);
    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);

    switch (io_stack_loc->Parameters.QueryDeviceRelations.Type) {
        case BusRelations:
        /* If we don't have any PDOs, trigger the probe */
        if (!bus->BusRelations) {
            status = WvMainBusInitialBusRelations(dev_obj);
            if (!NT_SUCCESS(status)) {
                irp->IoStatus.Status = status;
                IoCompleteRequest(irp, IO_NO_INCREMENT);
              }
          }
        dev_relations = bus->BusRelations;
        /*
         * TODO: The next line is a hack that needs to be removed
         * once proper PDO-add support is implemented.  This hack
         * causes WvMainBusInitialBusRelations to be called every
         * time
         */
        bus->BusRelations = NULL;
        break;

        case TargetDeviceRelation:
        case RemovalRelations:
        case EjectionRelations:
        default:
        DBG("Unsupported device relations query\n");
        IoSkipCurrentIrpStackLocation(irp);
        ASSERT(bus->LowerDeviceObject);
        return IoCallDriver(bus->LowerDeviceObject, irp);
      }

    pdo_count = dev_relations->Count;

    /* We need to include any higher driver's PDOs */
    higher_dev_relations = (VOID *) irp->IoStatus.Information;
    if (higher_dev_relations) {
        /* If we have nothing to add, pass the IRP down */
        if (!pdo_count) {
            IoSkipCurrentIrpStackLocation(irp);
            ASSERT(bus->LowerDeviceObject);
            return IoCallDriver(bus->LowerDeviceObject, irp);
            status = irp->IoStatus.Status;
            IoCompleteRequest(irp, IO_NO_INCREMENT);
            return status;
          }

        pdo_count += higher_dev_relations->Count;
      }

    /* Allocate a response */
    new_dev_relations = wv_palloc(
        sizeof *new_dev_relations +
        sizeof new_dev_relations->Objects[0] * (pdo_count - 1)
      );
    if (!new_dev_relations) {
        irp->IoStatus.Status = status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return status;
      }

    /* Populate the response... */
    new_dev_relations->Count = pdo_count;
    new_dev_relations->Objects[0] = NULL;

    /* ...with our PDOs... */
    if (dev_relations->Count) {
        RtlCopyMemory(
            new_dev_relations->Objects,
            dev_relations->Objects,
            sizeof new_dev_relations->Objects[0] * dev_relations->Count
          );
      }

    /* ...and with the higher driver's PDOs, if any */
    if (higher_dev_relations && higher_dev_relations->Count) {
        RtlCopyMemory(
            new_dev_relations->Objects + dev_relations->Count,
            higher_dev_relations->Objects,
            sizeof new_dev_relations->Objects[0] * higher_dev_relations->Count
          );
      }

    /* Free the higher driver's response, if any */
    wv_free(higher_dev_relations);

    /* Reference our PDOs.  Re-purposing pdo_count, here */
    for (pdo_count = 0; pdo_count < dev_relations->Count; ++pdo_count)
      ObReferenceObject(dev_relations->Objects[pdo_count]);

    /* Send the IRP down */
    irp->IoStatus.Status = STATUS_SUCCESS;
    irp->IoStatus.Information = (ULONG_PTR) new_dev_relations;
    IoSkipCurrentIrpStackLocation(irp);
    return IoCallDriver(bus->LowerDeviceObject, irp);
  }

static NTSTATUS WvMainBusDispatchDeviceControlIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    IO_STACK_LOCATION * io_stack_loc;
    ULONG POINTER_ALIGNMENT code;

    ASSERT(dev_obj);
    ASSERT(irp);
    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    ASSERT(io_stack_loc);
    code = io_stack_loc->Parameters.DeviceIoControl.IoControlCode;
    return WvBusDevCtl(irp, code);
  }

static NTSTATUS WvMainBusDispatchPowerIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    ASSERT(dev_obj);
    ASSERT(irp);
    return WvlIrpPassPowerToLower(WvBus.LowerDeviceObject, irp);
  }

static NTSTATUS WvMainBusDispatchCreateCloseIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    ASSERT(dev_obj);
    ASSERT(irp);
    /* Always succeed with nothing to do */
    /* TODO: Track resource usage */
    return WvlIrpComplete(irp, 0, STATUS_SUCCESS);
  }

static NTSTATUS WvMainBusDispatchSystemControlIrp(
    IN DEVICE_OBJECT * dev_obj,
    IN IRP * irp
  ) {
    ASSERT(dev_obj);
    ASSERT(irp);
    return WvlIrpPassToLower(WvBus.LowerDeviceObject, irp);
  }
