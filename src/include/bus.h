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
#ifndef WVL_M_BUS_H_
#  define WVL_M_BUS_H_

/**
 * @file
 *
 * Bus specifics
 */

/** Object types */
typedef struct S_WVL_MAIN_BUS_PROBE_REGISTRATION
  S_WVL_MAIN_BUS_PROBE_REGISTRATION;
struct WVL_BUS_T;

/** Function types */

/**
 * Callback for the main bus' initial bus relations probe
 *
 * @param DeviceObject
 *   The main bus device
 */
typedef VOID F_WVL_MAIN_BUS_PROBE_REGISTRATION(IN DEVICE_OBJECT *);

/**
 * A bus PnP routine.
 *
 * @v bus               The bus to receive the PnP IRP.
 * @v irp               The IRP to process.
 * @ret NTSTATUS        The status of the operation.
 */
typedef NTSTATUS STDCALL WVL_F_BUS_PNP(IN struct WVL_BUS_T *, IN PIRP);
typedef WVL_F_BUS_PNP * WVL_FP_BUS_PNP;

/** Constants */

/* Device state */
typedef enum WVL_BUS_STATE {
    WvlBusStateNotStarted,
    WvlBusStateStarted,
    WvlBusStateStopPending,
    WvlBusStateStopped,
    WvlBusStateRemovePending,
    WvlBusStateSurpriseRemovePending,
    WvlBusStateDeleted,
    WvlBusStates
  } WVL_E_BUS_STATE, * WVL_EP_BUS_STATE;

/** Function declarations */

/* From ../winvblock/mainbus/mainbus.c */

/**
 * Add a device to the main bus
 *
 * @param DeviceObject
 *   The device to be added
 *
 * @retval STATUS_SUCCESS
 * @retval STATUS_NO_SUCH_DEVICE
 * @retval STATUS_INSUFFICIENT_RESOURCES
 */
extern WVL_M_LIB NTSTATUS STDCALL WvlAddDeviceToMainBus(
    IN DEVICE_OBJECT * DeviceObject
  );

/**
 * Remove a device from the main bus
 *
 * @param DeviceObject
 *   The device to be removed
 */
extern WVL_M_LIB VOID STDCALL WvlRemoveDeviceFromMainBus(
    IN DEVICE_OBJECT * DeviceObject
  );

/**
 * Register a callback for the main bus' initial bus relations probe
 *
 * @param RegistrationRecord
 *   A record in non-paged memory noting the callback to be performed
 *   during the initial probe
 *
 * The record will be deregistered after the callback has been called
 */
extern WVL_M_LIB VOID STDCALL WvlRegisterMainBusInitialProbeCallback(
    IN S_WVL_MAIN_BUS_PROBE_REGISTRATION * reg
  );

/** Struct/union type definitions */

/** Registration for the main bus' initial bus relations probe */
struct S_WVL_MAIN_BUS_PROBE_REGISTRATION {
    /** The link in the linked list of all registrations */
    LIST_ENTRY Link[1];

    /** The callback to be performed during the initial probe */
    F_WVL_MAIN_BUS_PROBE_REGISTRATION * Callback;
  };

/* The bus type. */
typedef struct WVL_BUS_T {
    PDEVICE_OBJECT LowerDeviceObject;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Fdo;
    WVL_E_BUS_STATE OldState;
    WVL_E_BUS_STATE State;
    WVL_FP_BUS_PNP QueryDevText;
    struct {
        LIST_ENTRY Nodes;
        KSPIN_LOCK NodeLock;
        KIRQL NodeLockIrql;
        USHORT NodeCount;
      } BusPrivate_;
  } WVL_S_BUS_T, * WVL_SP_BUS_T;

/* A child PDO node on a bus.  Treat this as an opaque type. */
typedef struct WVL_BUS_NODE {
    struct {
        LIST_ENTRY Link;
        PDEVICE_OBJECT Pdo;
        WVL_SP_BUS_T Bus;
        /* The child's unit number relative to the parent bus. */
        UINT32 Num;
      } BusPrivate_;
    BOOLEAN Linked;
  } WVL_S_BUS_NODE, * WVL_SP_BUS_NODE;

/** Objects */

/* From mainbus.c */
extern WVL_M_LIB WVL_S_BUS_T WvBus;

/** More function declarations (TODO: Reorganize) */

/* Exports. */
extern WVL_M_LIB VOID WvlBusInit(WVL_SP_BUS_T);
extern WVL_M_LIB VOID STDCALL WvlBusClear(IN WVL_SP_BUS_T);
extern WVL_M_LIB VOID WvlBusLock(IN WVL_SP_BUS_T);
extern WVL_M_LIB VOID WvlBusUnlock(IN WVL_SP_BUS_T);
extern WVL_M_LIB BOOLEAN STDCALL WvlBusInitNode(
    OUT WVL_SP_BUS_NODE,
    IN PDEVICE_OBJECT
  );
extern WVL_M_LIB NTSTATUS STDCALL WvlBusAddNode(
    WVL_SP_BUS_T,
    WVL_SP_BUS_NODE
  );
extern WVL_M_LIB NTSTATUS STDCALL WvlBusRemoveNode(WVL_SP_BUS_NODE);
extern WVL_M_LIB UINT32 STDCALL WvlBusGetNodeNum(
    IN WVL_SP_BUS_NODE
  );
extern WVL_M_LIB WVL_SP_BUS_NODE STDCALL WvlBusGetNextNode(
    IN WVL_SP_BUS_T,
    IN WVL_SP_BUS_NODE
  );
extern WVL_M_LIB PDEVICE_OBJECT STDCALL WvlBusGetNodePdo(
    IN WVL_SP_BUS_NODE
  );
extern WVL_M_LIB UINT32 STDCALL WvlBusGetNodeCount(
    WVL_SP_BUS_T
  );
/* IRP_MJ_PNP dispatcher in libbus/pnp.c */
extern WVL_M_LIB NTSTATUS STDCALL WvlBusPnp(
    IN WVL_SP_BUS_T,
    IN PIRP
  );

#endif  /* WVL_M_BUS_H_ */
