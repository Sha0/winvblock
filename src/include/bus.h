/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 * Copyright 2006-2008, V.
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
#ifndef WVL_M_BUS_H_
#  define WVL_M_BUS_H_

/**
 * @file
 *
 * Bus specifics.
 */

/**
 * A bus thread routine.
 *
 * @v bus       The bus to be used in the thread routine.
 *
 * If you implement your own bus thread routine, you should call
 * WvlBusProcessWorkItems() within its loop.
 */
typedef VOID STDCALL WVL_F_BUS_THREAD(IN WVL_SP_BUS_T);
typedef WVL_F_BUS_THREAD * WVL_FP_BUS_THREAD;

/**
 * A bus PnP routine.
 *
 * @v bus               The bus to receive the PnP IRP.
 * @v irp               The IRP to process.
 * @ret NTSTATUS        The status of the operation.
 */
typedef NTSTATUS STDCALL WVL_F_BUS_PNP(IN WVL_SP_BUS_T, IN PIRP);
typedef WVL_F_BUS_PNP * WVL_FP_BUS_PNP;

/* Device state. */
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

/* The bus type. */
typedef struct WVL_BUS_T {
    PDEVICE_OBJECT LowerDeviceObject;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Fdo;
    UINT32 Children;
    WVL_FP_BUS_THREAD Thread;
    KEVENT ThreadSignal;
    BOOLEAN Stop;
    WVL_E_BUS_STATE OldState;
    WVL_E_BUS_STATE State;
    WVL_FP_BUS_PNP QueryDevText;
    struct {
        LIST_ENTRY Nodes;
        USHORT NodeCount;
        LIST_ENTRY WorkItems;
        KSPIN_LOCK WorkItemsLock;
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
  } WVL_S_BUS_NODE, * WVL_SP_BUS_NODE;

/**
 * A custom work-item function.
 *
 * @v context           Function-specific data.
 *
 * If a driver needs to enqueue a work item which should execute in the
 * context of the bus' controlling thread (this is the thread which calls
 * WvlBusProcessWorkItems()), then this is the function prototype to be
 * used.
 */
typedef VOID STDCALL WVL_F_BUS_WORK_ITEM(PVOID);
typedef WVL_F_BUS_WORK_ITEM * WVL_FP_BUS_WORK_ITEM;

typedef struct WVL_BUS_CUSTOM_WORK_ITEM {
    WVL_FP_BUS_WORK_ITEM Func;
    PVOID Context;
  } WVL_S_BUS_CUSTOM_WORK_ITEM, * WVL_SP_BUS_CUSTOM_WORK_ITEM;

/* Exports. */
extern WVL_M_LIB VOID WvlBusInit(WVL_SP_BUS_T);
extern WVL_M_LIB VOID WvlBusProcessWorkItems(WVL_SP_BUS_T);
extern WVL_M_LIB VOID WvlBusCancelWorkItems(WVL_SP_BUS_T);
extern WVL_M_LIB NTSTATUS WvlBusStartThread(
    IN WVL_SP_BUS_T,
    OUT PETHREAD *
  );
extern WVL_M_LIB BOOLEAN STDCALL WvlBusInitNode(
    OUT WVL_SP_BUS_NODE,
    IN PDEVICE_OBJECT
  );
extern WVL_M_LIB NTSTATUS STDCALL WvlBusAddNode(
    WVL_SP_BUS_T,
    WVL_SP_BUS_NODE
  );
extern WVL_M_LIB NTSTATUS STDCALL WvlBusRemoveNode(WVL_SP_BUS_NODE);
extern WVL_M_LIB NTSTATUS STDCALL WvlBusEnqueueIrp(WVL_SP_BUS_T, PIRP);
extern WVL_M_LIB NTSTATUS STDCALL WvlBusEnqueueCustomWorkItem(
    WVL_SP_BUS_T,
    WVL_SP_BUS_CUSTOM_WORK_ITEM
  );
extern WVL_M_LIB NTSTATUS STDCALL WvlBusSysCtl(
    IN WVL_SP_BUS_T,
    IN PIRP
  );
extern WVL_M_LIB NTSTATUS STDCALL WvlBusPower(
    IN WVL_SP_BUS_T,
    IN PIRP
  );
/* IRP_MJ_PNP dispatcher in bus/pnp.c */
extern WVL_M_LIB NTSTATUS STDCALL WvBusPnp(
    IN WVL_SP_BUS_T,
    IN PIRP,
    IN UCHAR
  );
extern WVL_M_LIB UINT32 STDCALL WvBusGetNodeNum(
    IN WVL_SP_BUS_NODE
  );
extern WVL_M_LIB WVL_SP_BUS_NODE STDCALL WvBusGetNextNode(
    IN WVL_SP_BUS_T,
    IN WVL_SP_BUS_NODE
  );
extern WVL_M_LIB PDEVICE_OBJECT STDCALL WvBusGetNodePdo(
    IN WVL_SP_BUS_NODE
  );
extern WVL_M_LIB UINT32 STDCALL WvBusGetNodeCount(
    WVL_SP_BUS_T
  );

#endif  /* WVL_M_BUS_H_ */
