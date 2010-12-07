/**
 * Copyright (C) 2009-2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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

/**
 * @file
 *
 * Device specifics.
 */

#include <ntddk.h>

#include "winvblock.h"
#include "wv_stdlib.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "device.h"
#include "debug.h"

static LIST_ENTRY dev_list;
static KSPIN_LOCK dev_list_lock;
/* Forward declarations */
static device__free_func free_dev;
static device__create_pdo_decl (
  make_dev_pdo
 );

/**
 * Initialize the global, device-common environment
 *
 * @ret ntstatus        STATUS_SUCCESS or the NTSTATUS for a failure
 */
STDCALL NTSTATUS
device__init (
  void
 )
{
  /*
   * Initialize the global list of devices
   */
  InitializeListHead ( &dev_list );
  KeInitializeSpinLock ( &dev_list_lock );

  return STATUS_SUCCESS;
}

/**
 * Create a new device
 *
 * @ret dev_ptr         The address of a new device, or NULL for failure
 *
 * See the header file for additional details
 */
winvblock__lib_func device__type_ptr
device__create (
  void
 )
{
  device__type_ptr dev_ptr;

  /*
   * Devices might be used for booting and should
   * not be allocated from a paged memory pool
   */
  dev_ptr = wv_mallocz(sizeof *dev_ptr);
  if ( dev_ptr == NULL )
    return NULL;
  /*
   * Track the new device in our global list
   */
  ExInterlockedInsertTailList ( &dev_list, &dev_ptr->tracking,
				&dev_list_lock );
  /*
   * Populate non-zero device defaults
   */
  dev_ptr->dispatch = driver__default_dispatch;
  dev_ptr->DriverObject = driver__obj_ptr;
  dev_ptr->ops.create_pdo = make_dev_pdo;
  dev_ptr->ops.free = free_dev;

  return dev_ptr;
}

/**
 * Create a device PDO
 *
 * @v dev_ptr           Points to the device that needs a PDO
 */
winvblock__lib_func
device__create_pdo_decl (
  device__create_pdo
 )
{
  return dev_ptr->ops.create_pdo ( dev_ptr );
}

/**
 * Default PDO creation operation
 *
 * @v dev_ptr           Points to the device that needs a PDO
 *
 * This function does nothing, since it doesn't make sense to create a PDO
 * for an unknown type of device.
 */
static
device__create_pdo_decl (
  make_dev_pdo
 )
{
  DBG ( "No specific PDO creation operation for this device!\n" );
  return NULL;
}

/**
 * Close a device
 *
 * @v dev_ptr           Points to the device to close
 */
winvblock__lib_func
device__close_decl (
  device__close
 )
{
  /*
   * Call the device's close routine
   */
  dev_ptr->ops.close ( dev_ptr );
}

/**
 * Delete a device.
 *
 * @v dev_ptr           Points to the device to delete.
 */
winvblock__lib_func void STDCALL device__free(IN device__type_ptr dev_ptr)
  {
    /* Call the device's free routine. */
    dev_ptr->ops.free(dev_ptr);
  }

/**
 * Default device deletion operation.
 *
 * @v dev_ptr           Points to the device to delete.
 */
static void STDCALL free_dev(IN device__type_ptr dev_ptr)
  {
    /*
     * Track the device deletion in our global list.  Unfortunately,
     * for now we have faith that a device won't be deleted twice and
     * result in a race condition.  Something to keep in mind...
     */
    ExInterlockedRemoveHeadList(dev_ptr->tracking.Blink, &dev_list_lock);
  
    wv_free(dev_ptr);
  }
