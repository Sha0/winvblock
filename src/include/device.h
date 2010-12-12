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
 * Device PnP ID reponse routine.
 *
 * @v dev                       The device being queried for PnP IDs.
 * @v query_type                The query type.
 * @v buf                       Wide character, 512-element buffer for the
 *                              ID response.
 * @ret winvblock__uint32       The number of wide characters in the response.
 */
typedef winvblock__uint32 STDCALL device__pnp_id_func(
    IN struct device__type *,
    IN BUS_QUERY_ID_TYPE,
    IN OUT WCHAR (*)[512]
  );

extern winvblock__lib_func device__pnp_id_func device__pnp_id;

/**
 * Device close routine.
 *
 * @v dev               The device being closed.
 */
typedef void STDCALL device__close_func(IN struct device__type *);

extern winvblock__lib_func device__close_func device__close;

/**
 * Device deletion routine.
 *
 * @v dev_ptr           Points to the device to delete.
 */
typedef void STDCALL device__free_func(IN struct device__type *);

extern winvblock__lib_func device__free_func device__free;

extern winvblock__lib_func struct device__type * device__create(void);

winvblock__def_struct(device__ops) {
    device__create_pdo_func * create_pdo;
    device__init_func * init;
    device__pnp_id_func * pnp_id;
    device__close_func * close;
    device__free_func * free;
  };

typedef void STDCALL device__thread_func(IN void *);

/* Details common to all devices this driver works with */
struct device__type {
    /* For debugging */
    winvblock__bool IsBus;
    /* A device's IRP dispatch routine. */
    driver__dispatch_func * dispatch;
    /* The device's thread routine. */
    device__thread_func * thread;
    /* The device's thread wakeup signal. */
    KEVENT thread_wakeup;
    /* The device's IRP queue. */
    LIST_ENTRY irp_list;
    /* The device's IRP queue lock. */
    KSPIN_LOCK irp_list_lock;
    /* Self is self-explanatory. */
    PDEVICE_OBJECT Self;
    /* Points to the parent bus' DEVICE_OBJECT */
    PDEVICE_OBJECT Parent;
    /* The device's child ID relative to the parent bus. */
    winvblock__uint32 dev_num;
    /* Points to the driver. */
    PDRIVER_OBJECT DriverObject;
    /* Current state of the device. */
    enum device__state state;
    /* Previous state of the device. */
    enum device__state old_state;
    /* Deprecated: The mini IRP handler chain. */
    irp__handler_chain irp_handler_chain;
    /* The next device in the parent bus' devices.  TODO: Don't do this. */
    struct device__type * next_sibling_ptr;
    /* The device operations. */
    device__ops ops;
    /* Tracking for the device module itself. */
    LIST_ENTRY tracking;
    /* Points to further extensions. */
    winvblock__any_ptr ext;
  };

extern winvblock__lib_func struct device__type * device__get(PDEVICE_OBJECT);
extern winvblock__lib_func void device__set(
    PDEVICE_OBJECT,
    struct device__type *
  );
extern irp__handler device__pnp_query_id;

#endif  /* _DEVICE_H */
