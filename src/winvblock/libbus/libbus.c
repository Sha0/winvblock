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
 * Bus specifics.
 */

#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "irp.h"
#include "bus.h"
#include "debug.h"

/**
 * Initialize bus defaults.
 *
 * @v Bus               Points to the bus to initialize with defaults.
 */
WVL_M_LIB VOID WvlBusInit(WVL_SP_BUS_T Bus) {
    RtlZeroMemory(Bus, sizeof *Bus);
    /* Populate non-zero bus device defaults. */
    InitializeListHead(&Bus->BusPrivate_.Nodes);
    KeInitializeSpinLock(&Bus->BusPrivate_.NodeLock);
  }

/**
 * Add a PDO node to a bus' list of children.  Internal.
 *
 * @v bus               The bus to add the node to.
 * @v new_node          The PDO node to add to the bus.
 *
 * Don't call this function yourself.  It doesn't perform any error-checking.
 */
static VOID STDCALL WvlBusAddNode_(WVL_SP_BUS_T bus, WVL_SP_BUS_NODE new_node) {
    PLIST_ENTRY walker;
    KIRQL irql;

    DBG(
        "Adding PDO %p to bus %p.\n",
        (PVOID) new_node->BusPrivate_.Pdo,
        (PVOID) bus
      );
    ObReferenceObject(new_node->BusPrivate_.Pdo);
    /* It's too bad about having both linked list and bus ref. */
    new_node->BusPrivate_.Bus = bus;

    /* Find a slot for the new child. */
    walker = &bus->BusPrivate_.Nodes;
    new_node->BusPrivate_.Num = 0;
    KeAcquireSpinLock(&bus->BusPrivate_.NodeLock, &irql);
    while ((walker = walker->Flink) != &bus->BusPrivate_.Nodes) {
        WVL_SP_BUS_NODE node = CONTAINING_RECORD(
            walker,
            WVL_S_BUS_NODE,
            BusPrivate_.Link
          );

        if (
            node->BusPrivate_.Num &&
            (node->BusPrivate_.Link.Blink == &bus->BusPrivate_.Nodes)
          ) {
            /* The first node's unit number is != 0.  Insert here. */
            break;
          }
        if (node->BusPrivate_.Num > new_node->BusPrivate_.Num) {
            /* There is a gap so insert here. */
            break;
          }
        /* Continue trying to find a slot. */
        new_node->BusPrivate_.Num++;
      } /* while */
    /* Insert before walker. */
    InsertTailList(walker, &new_node->BusPrivate_.Link);
    bus->BusPrivate_.NodeCount++;
    KeReleaseSpinLock(&bus->BusPrivate_.NodeLock, irql);
    new_node->Linked = TRUE;
    /* We might be floating. */
    if (bus->Pdo)
      IoInvalidateDeviceRelations(bus->Pdo, BusRelations);
    /* Hack: Use the new method for the main bus */
    if (bus == &WvBus)
      WvlAddDeviceToMainBus(new_node->BusPrivate_.Pdo);
    return;
  }

/**
 * Remove a PDO node from a bus.  Internal.
 *
 * @v bus             The bus to remove the node from.
 * @v node            The PDO node to remove from its parent bus.
 *
 * Don't call this function yourself.  It doesn't perform any error-checking.
 */
static VOID STDCALL WvlBusRemoveNode_(
    WVL_SP_BUS_T bus,
    WVL_SP_BUS_NODE node
  ) {
    KIRQL irql;

    DBG(
        "Removing PDO 0x%08X from bus 0x%08X.\n",
        (PVOID) node->BusPrivate_.Pdo,
        (PVOID) bus
      );
    KeAcquireSpinLock(&bus->BusPrivate_.NodeLock, &irql);
    RemoveEntryList(&node->BusPrivate_.Link);
    bus->BusPrivate_.NodeCount--;
    KeReleaseSpinLock(&bus->BusPrivate_.NodeLock, irql);
    node->Linked = FALSE;
    ObDereferenceObject(node->BusPrivate_.Pdo);
    /* We might be floating. */
    if (bus->Pdo)
      IoInvalidateDeviceRelations(bus->Pdo, BusRelations);
    /* Hack: Use the new method for the main bus */
    if (bus == &WvBus)
      WvlRemoveDeviceFromMainBus(node->BusPrivate_.Pdo);
    return;
  }

/**
 * Initialize a bus node with an associated PDO.
 *
 * @v Node              The node to initialize.
 * @v Pdo               The PDO to associate the node with.
 * @ret BOOLEAN FALSE for a NULL argument, otherwise TRUE
 */
WVL_M_LIB BOOLEAN STDCALL WvlBusInitNode(
    OUT WVL_SP_BUS_NODE Node,
    IN PDEVICE_OBJECT Pdo
  ) {
    if (!Node || !Pdo)
      return FALSE;

    RtlZeroMemory(Node, sizeof *Node);
    Node->BusPrivate_.Pdo = Pdo;
    return TRUE;
  }

/**
 * Clear a bus of its associations.
 *
 * @v Bus               The bus to clear.
 *
 * Note that you should set the bus start != WvlBusStateStarted before
 * calling this function.  Otherwise, if an IRP comes through, it might
 * race with the associations being cleared.
 */
WVL_M_LIB VOID STDCALL WvlBusClear(IN WVL_SP_BUS_T Bus) {
    KIRQL irql;
    PLIST_ENTRY node_link;

    if (Bus->State == WvlBusStateStarted)
      DBG("Caller did not stop the bus!\n");
    /*
     * Remove all children.  The state should have been set by the caller,
     * so wait for anyone working with the node list to finish.
     */
    KeAcquireSpinLock(&Bus->BusPrivate_.NodeLock, &irql);
    KeReleaseSpinLock(&Bus->BusPrivate_.NodeLock, irql);
    /* Safe now, assuming no-one after us goes for the lock. */
    node_link = &Bus->BusPrivate_.Nodes;
    while ((node_link = node_link->Flink) != &Bus->BusPrivate_.Nodes) {
        WVL_SP_BUS_NODE node = CONTAINING_RECORD(
            node_link,
            WVL_S_BUS_NODE,
            BusPrivate_.Link
          );

        RemoveEntryList(&node->BusPrivate_.Link);
        node->Linked = FALSE;
        ObDereferenceObject(node->BusPrivate_.Pdo);
        Bus->BusPrivate_.NodeCount--;
        DBG("Removed PDO from bus %p.\n", Bus);
      }
    /* Detach and disassociate. */
    if (Bus->LowerDeviceObject)
      IoDetachDevice(Bus->LowerDeviceObject);
    /* Disassociate. */
    Bus->LowerDeviceObject = NULL;
    Bus->Pdo = NULL;
    return;
  }

/**
 * Lock a bus' list of child nodes for iteration.
 *
 * @v Bus               The bus to be locked.
 */
WVL_M_LIB VOID STDCALL WvlBusLock(IN WVL_SP_BUS_T Bus) {
    if (!Bus) {
        DBG("No bus specified!\n");
        return;
      }
    KeAcquireSpinLock(
        &Bus->BusPrivate_.NodeLock,
        &Bus->BusPrivate_.NodeLockIrql
      );
    return;
  }

/**
 * Unlock a bus' list of child nodes from iteration.
 *
 * @v Bus               The bus to be unlocked.
 */
WVL_M_LIB VOID STDCALL WvlBusUnlock(IN WVL_SP_BUS_T Bus) {
    if (!Bus) {
        DBG("No bus specified!\n");
        return;
      }
    KeReleaseSpinLock(
        &Bus->BusPrivate_.NodeLock,
        Bus->BusPrivate_.NodeLockIrql
      );
    return;
  }

/**
 * Add a PDO node to a bus' list of children.
 *
 * @v Bus               The bus to add the node to.
 * @v Node              The PDO node to add to the bus.
 * @ret NTSTATUS        The status of the operation.
 *
 * Do not attempt to add the same node to more than one bus.  Do not call
 * this function between WvlBusLock() and WvlBusUnlock() on the same bus.
 */
WVL_M_LIB NTSTATUS STDCALL WvlBusAddNode(
    WVL_SP_BUS_T Bus,
    WVL_SP_BUS_NODE Node
  ) {
    if (
        !Bus ||
        !Node ||
        !Bus->Fdo ||
        Bus->Fdo->DriverObject != Node->BusPrivate_.Pdo->DriverObject
      )
      return STATUS_INVALID_PARAMETER;

    if (Bus->State != WvlBusStateStarted)
      return STATUS_NO_SUCH_DEVICE;

    /* Nothing can stop me now, 'cause I don't care. any. more. */
    WvlBusAddNode_(Bus, Node);
    return STATUS_SUCCESS;
  }

/**
 * Remove a PDO node from a bus.
 *
 * @v Node              The PDO node to remove from its parent bus.
 * @ret NTSTATUS        The status of the operation.
 *
 * Do not call this function between WvlBusLock() and WvlBusUnlock() on
 * the same bus.
 */
WVL_M_LIB NTSTATUS STDCALL WvlBusRemoveNode(
    WVL_SP_BUS_NODE Node
  ) {
    WVL_SP_BUS_T bus;

    if (!Node || !(bus = Node->BusPrivate_.Bus))
      return STATUS_INVALID_PARAMETER;

    /* Remove the node. */
    WvlBusRemoveNode_(bus, Node);
    return STATUS_SUCCESS;
  }

/**
 * Get the unit number for a child node on a bus.
 *
 * @v Node              The node whose unit number we request.
 * @ret UINT32          The unit number for the node.
 */
WVL_M_LIB UINT32 STDCALL WvlBusGetNodeNum(
    IN WVL_SP_BUS_NODE Node
  ) {
    return Node->BusPrivate_.Num;
  }

/**
 * Get the next child node on a bus.
 *
 * @v Bus               The bus whose nodes are fetched.
 * @v PrevNode          The previous node.  Pass NULL to begin.
 * @ret WVL_SP_BUS_NODE  Returns NULL when there are no more nodes.
 *
 * This function does not lock the bus, so you should lock the bus first
 * with WvlBusLock(), then unlock it with WvlBusUnlock() when your
 * iteration is finished.
 */
WVL_M_LIB WVL_SP_BUS_NODE STDCALL WvlBusGetNextNode(
    IN WVL_SP_BUS_T Bus,
    IN WVL_SP_BUS_NODE PrevNode
  ) {
    PLIST_ENTRY link;

    if (!PrevNode)
      link = &Bus->BusPrivate_.Nodes;
      else
      link = &PrevNode->BusPrivate_.Link;
    link = link->Flink;
    if (link == &Bus->BusPrivate_.Nodes)
      return NULL;
    return CONTAINING_RECORD(link, WVL_S_BUS_NODE, BusPrivate_.Link);
  }

/**
 * Get a child node's PDO.
 *
 * @v Node              The node whose PDO will be returned.
 * @ret PDEVICE_OBJECT  The PDO for the node.
 */
WVL_M_LIB PDEVICE_OBJECT STDCALL WvlBusGetNodePdo(
    IN WVL_SP_BUS_NODE Node
  ) {
    return Node->BusPrivate_.Pdo;
  }

/**
 * Get the count of child nodes on a bus.
 *
 * @v Bus               The bus whose node-count will be returned.
 * @v UINT32            The count of nodes on the bus.
 *
 * In order for this function to yield a race-free, useful result, it
 * should be used inside a WvlBusLock() <-> WvlBusUnlock() section.
 */
WVL_M_LIB UINT32 STDCALL WvlBusGetNodeCount(
    WVL_SP_BUS_T Bus
  ) {
    return Bus->BusPrivate_.NodeCount;
  }
