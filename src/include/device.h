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
#ifndef _DEVICE_H
#  define _DEVICE_H

/**
 * @file
 *
 * Device specifics.
 */

enum device__state {
    device__state_not_started,
    device__state_started,
    device__state_stop_pending,
    device__state_stopped,
    device__state_remove_pending,
    device__state_surprise_remove_pending,
    device__state_deleted
  };

/* Forward declaration. */
struct device__type;

/**
 * Device PDO creation routine.
 *
 * @v dev               The device whose PDO should be created.
 * @ret pdo             Points to the new PDO, or is NULL upon failure.
 */
typedef PDEVICE_OBJECT STDCALL device__create_pdo_func(
    IN struct device__type *
  );

extern winvblock__lib_func device__create_pdo_func device__create_pdo;

/**
 * Device initialization routine.
 *
 * @v dev               The device being initialized.
 */
typedef winvblock__bool STDCALL device__init_func(IN struct device__type *);

/**
 * Device close routine
 *
 * @v dev_ptr           The device being closed
 */
#  define device__close_decl( x ) \
\
void STDCALL \
x ( \
  IN struct device__type * dev_ptr \
 )
/*
 * Function pointer for a device close routine.
 * 'indent' mangles this, so it looks weird
 */
typedef device__close_decl (
   ( *device__close_routine )
 );
/**
 * Close a device
 *
 * @v dev_ptr           Points to the device to close
 */
extern winvblock__lib_func device__close_decl (
  device__close
 );

/**
 * Device deletion routine.
 *
 * @v dev_ptr           Points to the device to delete.
 */
typedef void STDCALL device__free_func(IN struct device__type *);
/**
 * Delete a device.
 *
 * @v dev_ptr           Points to the device to delete.
 */
extern winvblock__lib_func device__free_func device__free;

/**
 * Initialize the global, device-common environment
 *
 * @ret ntstatus        STATUS_SUCCESS or the NTSTATUS for a failure
 */
extern STDCALL NTSTATUS device__init (
  void
 );

/**
 * Create a new device
 *
 * @ret dev_ptr         The address of a new device, or NULL for failure
 *
 * This function should not be confused with a PDO creation routine, which is
 * actually implemented for each device type.  This routine will allocate a
 * device__type, track it in a global list, as well as populate the device
 * with default values.
 */
extern winvblock__lib_func struct device__type * device__create (
  void
 );

winvblock__def_struct(device__ops) {
    device__create_pdo_func * create_pdo;
    device__init_func * init;
    device__close_routine close;
    device__free_func * free;
  };

typedef void STDCALL (device__thread_func)(IN void *);

/* Details common to all devices this driver works with */
struct device__type
{
  winvblock__bool IsBus;	/* For debugging */
  /* A device's IRP dispatch routine. */
  driver__dispatch_func * (dispatch);
  /* The device's thread routine. */
  device__thread_func * (thread);
  /* The device's thread wakeup signal. */
  KEVENT (thread_wakeup);
  /* The device's IRP queue. */
  LIST_ENTRY (irp_list);
  /* The device's IRP queue lock. */
  KSPIN_LOCK (irp_list_lock);
  PDEVICE_OBJECT Self;
  PDEVICE_OBJECT Parent;
  PDRIVER_OBJECT DriverObject;
  enum device__state state;
  enum device__state old_state;
  irp__handler_chain irp_handler_chain;
  struct device__type * next_sibling_ptr;
  device__ops ops;
  LIST_ENTRY tracking;
  winvblock__any_ptr ext;
};

extern winvblock__lib_func struct device__type * device__get(PDEVICE_OBJECT);
extern winvblock__lib_func void device__set(
    PDEVICE_OBJECT,
    struct device__type *
  );

#endif  /* _DEVICE_H */
