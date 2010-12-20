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
#ifndef _WV_M_DEVICE_H_
#  define _WV_M_DEVICE_H_

/**
 * @file
 *
 * Device specifics.
 */

typedef enum WV_DEV_STATE {
    WvDevStateNotStarted,
    WvDevStateStarted,
    WvDevStateStopPending,
    WvDevStateStopped,
    WvDevStateRemovePending,
    WvDevStateSurpriseRemovePending,
    WvDevStateDeleted,
    WvDevStates
  } WV_E_DEV_STATE, * WV_EP_DEV_STATE;

/* Forward declaration. */
typedef struct WV_DEV_T WV_S_DEV_T, * WV_SP_DEV_T;

/**
 * Device PDO creation routine.
 *
 * @v dev               The device whose PDO should be created.
 * @ret pdo             Points to the new PDO, or is NULL upon failure.
 */
typedef PDEVICE_OBJECT STDCALL WV_F_DEV_CREATE_PDO(IN WV_SP_DEV_T);
typedef WV_F_DEV_CREATE_PDO * WV_FP_DEV_CREATE_PDO;
extern winvblock__lib_func WV_F_DEV_CREATE_PDO WvDevCreatePdo;

/**
 * Device initialization routine.
 *
 * @v dev               The device being initialized.
 */
typedef winvblock__bool STDCALL WV_F_DEV_INIT(IN WV_SP_DEV_T);
typedef WV_F_DEV_INIT * WV_FP_DEV_INIT;

/**
 * Device PnP ID reponse routine.
 *
 * @v dev                       The device being queried for PnP IDs.
 * @v query_type                The query type.
 * @v buf                       Wide character, 512-element buffer for the
 *                              ID response.
 * @ret winvblock__uint32       The number of wide characters in the response.
 */
typedef winvblock__uint32 STDCALL WV_F_DEV_PNP_ID(
    IN WV_SP_DEV_T,
    IN BUS_QUERY_ID_TYPE,
    IN OUT WCHAR (*)[512]
  );
typedef WV_F_DEV_PNP_ID * WV_FP_DEV_PNP_ID;
extern winvblock__lib_func WV_F_DEV_PNP_ID WvDevPnpId;

/**
 * Device close routine.
 *
 * @v dev               The device being closed.
 */
typedef void STDCALL WV_F_DEV_CLOSE(IN WV_SP_DEV_T);
typedef WV_F_DEV_CLOSE * WV_FP_DEV_CLOSE;
extern winvblock__lib_func WV_F_DEV_CLOSE WvDevClose;

/**
 * Device deletion routine.
 *
 * @v dev_ptr           Points to the device to delete.
 */
typedef void STDCALL WV_F_DEV_FREE(IN WV_SP_DEV_T);
typedef WV_F_DEV_FREE * WV_FP_DEV_FREE;
extern winvblock__lib_func WV_F_DEV_FREE WvDevFree;

extern winvblock__lib_func void WvDevInit(WV_SP_DEV_T);
extern winvblock__lib_func WV_SP_DEV_T WvDevCreate(void);

winvblock__def_struct(device__ops) {
    WV_FP_DEV_CREATE_PDO create_pdo;
    WV_FP_DEV_INIT init;
    WV_FP_DEV_PNP_ID pnp_id;
    WV_FP_DEV_CLOSE close;
    WV_FP_DEV_FREE free;
  };

typedef void STDCALL device__thread_func(IN void *);

/**
 * The prototype for a device IRP dispatch.
 *
 * @v dev               Points to the device.
 * @v irp               Points to the IRP.
 * @ret NTSTATUS        The status of processing the IRP for the device.
 */
typedef NTSTATUS STDCALL device__dispatch_func(IN WV_SP_DEV_T, IN PIRP);

/**
 * The prototype for a device IRP_MJ_DEVICE_CONTROL dispatch.
 *
 * @v dev               Points to the device.
 * @v irp               Points to the IRP.
 * @v code              The I/O control code.
 * @ret NTSTATUS        The status of processing the IRP for the device.
 */
typedef NTSTATUS STDCALL device__dev_ctl_func(
    IN WV_SP_DEV_T,
    IN PIRP,
    IN ULONG POINTER_ALIGNMENT
  );

/**
 * The prototype for a device IRP_MJ_SCSI dispatch.
 *
 * @v dev               Points to the device.
 * @v irp               Points to the IRP.
 * @v code              The SCSI function.
 * @ret NTSTATUS        The status of processing the IRP for the device.
 */
typedef NTSTATUS STDCALL device__scsi_func(IN WV_SP_DEV_T, IN PIRP, IN UCHAR);

/**
 * The prototype for a device IRP_MJ_PNP dispatch.
 *
 * @v dev               Points to the device.
 * @v irp               Points to the IRP.
 * @v code              The minor function.
 * @ret NTSTATUS        The status of processing the IRP for the device.
 */
typedef NTSTATUS STDCALL device__pnp_func(IN WV_SP_DEV_T, IN PIRP, IN UCHAR);

/* IRP major function handler table. */
struct device__irp_mj {
    device__dispatch_func * power;
    device__dispatch_func * sys_ctl;
    device__dev_ctl_func * dev_ctl;
    device__scsi_func * scsi;
    device__pnp_func * pnp;
  };

/* Details common to all devices this driver works with */
struct WV_DEV_T {
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
    /* Flag to create/delete a PDO on a threaded bus. */
    winvblock__bool thread_pdo;
    /* Points to the parent bus' DEVICE_OBJECT */
    PDEVICE_OBJECT Parent;
    /* The device's child ID relative to the parent bus. */
    winvblock__uint32 dev_num;
    /* Points to the driver. */
    PDRIVER_OBJECT DriverObject;
    /* Current state of the device. */
    WV_E_DEV_STATE State;
    /* Previous state of the device. */
    WV_E_DEV_STATE OldState;
    /* The next device in the parent bus' devices.  TODO: Don't do this. */
    WV_SP_DEV_T next_sibling_ptr;
    /* The device operations. */
    device__ops ops;
    /* Tracking for the device module itself. */
    LIST_ENTRY tracking;
    /* Points to further extensions. */
    winvblock__any_ptr ext;
    /* How to handle IRPs based on major function code. */
    struct device__irp_mj * irp_mj;
  };

extern winvblock__lib_func WV_SP_DEV_T device__get(PDEVICE_OBJECT);
extern winvblock__lib_func void device__set(PDEVICE_OBJECT, WV_SP_DEV_T);
extern device__dispatch_func device__pnp_query_id;

#endif  /* _WV_M_DEVICE_H_ */
