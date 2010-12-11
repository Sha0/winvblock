/**
 * Copyright (C) 2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
 *
 * This file is part of WinVBlock, derived from WinAoE.
 * For WinAoE contact information, see http://winaoe.org/
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
 * Handling IRPs.
 */

#include <ntddk.h>
#include <stdlib.h>

#include "winvblock.h"
#include "wv_stdlib.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "device.h"
#include "debug.h"

/* Forward declarations. */
static device__thread_func (irp_thread);

/*
 * An internal type.  A device extension will have a
 * pointer to this structure, but its type will be a void pointer
 */
winvblock__def_struct ( handler_chain )
{
  /*
   * Points to an array of irp__handlings 
   */
  irp__handling *table;
  /*
   * Total table size, in bytes 
   */
  size_t size;
  /*
   * Points to the next table in the chain or NULL
   */
  handler_chain_ptr next;
};

/**
 * Register an IRP handling table with a chain (with table size)
 *
 * @v chain_ptr Pointer to IRP handler chain to attach a table to
 * @v table     Table to add
 * @v size      Size of the table to add, in bytes
 * @ret         FALSE for failure, TRUE for success
 */
winvblock__lib_func winvblock__bool
irp__reg_table_s (
  IN OUT irp__handler_chain_ptr chain_ptr,
  IN irp__handling_ptr table,
  IN size_t size
 )
{
  /*
   * The type used by a device extension is opaque
   */
  handler_chain_ptr *link = ( handler_chain_ptr * ) chain_ptr,
    new_link;

  if ( link == NULL )
    /*
     * Nothing to attach to
     */
    return FALSE;
  /*
   * Allocate and attach a new link in the chain.
   * Maybe we should use a spin-lock for this
   */
  new_link = wv_malloc(sizeof *new_link);
  if ( new_link == NULL )
    {
      /*
       * Really too bad
       */
      DBG ( "Could not allocate IRP handler chain!\n" );

      return FALSE;
    }
  new_link->table = table;
  /*
   * Could sanity-check the size to be a multiple of sizeof(irp__handling)
   */
  new_link->size = size;
  new_link->next = *link;
  *link = new_link;
  return TRUE;
}

/**
 * Un-register an IRP handling table from a chain
 *
 * @v chain_ptr Pointer to IRP handler chain to remove table from
 * @v table     Table to remove
 * @ret         FALSE for failure, TRUE for success
 */
winvblock__lib_func winvblock__bool
irp__unreg_table (
  IN OUT irp__handler_chain_ptr chain_ptr,
  IN irp__handling_ptr table
 )
{
  winvblock__bool done = FALSE;
  /*
   * The type used by a device extension is opaque
   */
  handler_chain_ptr *link = ( handler_chain_ptr * ) chain_ptr;

  if ( link == NULL )
    /*
     * No chain given
     */
    return FALSE;
  /*
   * Walk the chain, looking for the given table
   */
  while ( *link != NULL )
    {
      if ( link[0]->table == table )
	{
	  /*
	   * Remove this link in the chain
	   */
	  handler_chain_ptr next = link[0]->next;
    wv_free(*link);
	  *link = next;
	  return TRUE;
	}
      link = &link[0]->next;
    }

  DBG ( "Table not found\n" );
  return FALSE;
}

/**
 * Walk a mini IRP handling table and process matches.
 *
 * @v dev               The device to process.
 * @v irp               The IRP to process.
 * @v table             The mini IRP handling table to use to process the IRP.
 * @v table_size        The size of the mini IRP handling table, in bytes.
 * @v completion        Points to a boolean which is TRUE at IRP completion.
 * @ret                 STATUS_NOT_SUPPORTED for an unhandled IRP.
 */
winvblock__lib_func NTSTATUS STDCALL (irp__process_with_table)(
    IN PDEVICE_OBJECT (dev),
    IN PIRP (irp),
    const irp__handling * (table),
    size_t (table_size),
    winvblock__bool * (completion)
  ) {
    int (index);
    PIO_STACK_LOCATION (io_stack_loc);
    winvblock__bool (handles_major), (handles_minor);
    NTSTATUS (status) = STATUS_NOT_SUPPORTED;

    /* We could assert this division. */
    index = table_size / sizeof *table;

    io_stack_loc = IoGetCurrentIrpStackLocation(irp);
    /* For each entry in the stack, in last-is-first order. */
    while (index--) {
        handles_major = (
            (table[index].irp_major_func == io_stack_loc->MajorFunction) ||
            table[index].any_major
          );
        handles_minor = (
            (table[index].irp_minor_func == io_stack_loc->MinorFunction) ||
            table[index].any_minor
          );
        if (handles_major && handles_minor) {
            status = table[index].handler(
                dev,
                irp,
                io_stack_loc,
                device__get(dev),
    				    completion
              );
            /* Do not process the IRP any further down the stack. */
        	  if (*completion) break;
          } /* if handles */
      } /* while index */

    return status;
  }

/**
 * Mini IRP handling strategy.
 *
 */
NTSTATUS STDCALL irp__process(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PIO_STACK_LOCATION Stack,
    IN struct device__type * dev_ptr,
    OUT winvblock__bool_ptr completion_ptr
  )
  {
    NTSTATUS status = STATUS_NOT_SUPPORTED;
    handler_chain_ptr link;
  
    link = ( handler_chain_ptr ) dev_ptr->irp_handler_chain;
  
    while ( link != NULL )
      {
        status = irp__process_with_table(
            DeviceObject,
            Irp,
            link->table,
            link->size,
            completion_ptr
          );
        if ( *completion_ptr )
          break;
        link = link->next;
      }
    return status;
  }

static void STDCALL (irp_thread)(IN void * (context)) {
    struct device__type * dev = context;
    LARGE_INTEGER (timeout);
    PLIST_ENTRY (walker);

    /* Wake up at least every second. */
    timeout.QuadPart = -10000000LL;

    /* While the device is active... */
    while (dev->thread) {
        /* Wait for the signal or the timeout. */
        KeWaitForSingleObject(
            &dev->thread_wakeup,
            Executive,
            KernelMode,
            FALSE,
            &timeout
          );
        KeResetEvent(&dev->thread_wakeup);
        /* Process each IRP in the queue. */
        while (walker = ExInterlockedRemoveHeadList(
            &dev->irp_list,
					  &dev->irp_list_lock
          )) {
            NTSTATUS (status);
            PIRP (irp) = CONTAINING_RECORD(walker, IRP, Tail.Overlay.ListEntry);
            winvblock__bool (completion) = FALSE;

            /* Process the IRP. */
            status = irp__process(
                dev->Self,
                irp,
                IoGetCurrentIrpStackLocation(irp),
                dev,
                &completion
              );
            #ifdef DEBUGIRPS
            if (status != STATUS_PENDING) Debug_IrpEnd(Irp, status);
            #endif
          } /* while walker */
      } /* while active */
    /* The device has finished. */
    dev->ops.free(dev);

    return;
  }
