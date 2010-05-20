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
 * Device specifics
 *
 */

#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "device.h"
#include "driver.h"

static LIST_ENTRY dev_list;
static KSPIN_LOCK dev_list_lock;

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
winvblock__lib_func STDCALL device__type_ptr
device__create (
  void
 )
{
  device__type_ptr dev_ptr;

  /*
   * Devices might be used for booting and should
   * not be allocated from a paged memory pool
   */
  dev_ptr = ExAllocatePool ( NonPagedPool, sizeof ( device__type ) );
  if ( dev_ptr == NULL )
    goto err_nomem;
  RtlZeroMemory ( dev_ptr, sizeof ( device__type ) );
  /*
   * Track the new device in our global list
   */
  ExInterlockedInsertTailList ( &dev_list, &dev_ptr->tracking,
				&dev_list_lock );
  /*
   * Populate non-zero device defaults
   */
  dev_ptr->size = sizeof ( device__type );
  dev_ptr->DriverObject = driver__obj_ptr;
  /*
   * Register the default driver IRP handling table
   */
  irp__reg_table_s ( &dev_ptr->irp_handler_chain, driver__handling_table,
		     driver__handling_table_size );

err_nomem:

  return dev_ptr;
}
