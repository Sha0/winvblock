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
 * Driver specifics
 *
 */

#include <stdio.h>
#include <ntddk.h>

#include "winvblock.h"
#include "wv_stdlib.h"
#include "wv_string.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "device.h"
#include "disk.h"
#include "registry.h"
#include "mount.h"
#include "bus.h"
#include "filedisk.h"
#include "ramdisk.h"
#include "debug.h"

/* Forward declarations. */
static driver__dispatch_func (driver_dispatch_not_supported);
static driver__dispatch_func (driver_dispatch);

static void STDCALL Driver_Unload (
  IN PDRIVER_OBJECT DriverObject
 );

static void *Driver_Globals_StateHandle;
static winvblock__bool Driver_Globals_Started = FALSE;

PDRIVER_OBJECT driver__obj_ptr = NULL;

/* Contains TXTSETUP.SIF/BOOT.INI-style OsLoadOptions parameters */
LPWSTR os_load_opts = NULL;

static LPWSTR STDCALL
get_opt (
  IN LPWSTR opt_name
 )
{
  LPWSTR our_opts,
   the_opt;
  WCHAR our_sig[] = L"WINVBLOCK=";
/* To produce constant integer expressions */
  enum
  {
    our_sig_len_bytes = sizeof ( our_sig ) - sizeof ( WCHAR ),
    our_sig_len = our_sig_len_bytes / sizeof ( WCHAR )
  };
  size_t opt_name_len,
   opt_name_len_bytes;

  if ( !os_load_opts || !opt_name )
    return NULL;

  /*
   * Find /WINVBLOCK= options
   */
  our_opts = os_load_opts;
  while ( *our_opts != L'\0' )
    {
      if (!wv_memcmpeq(our_opts, our_sig, our_sig_len_bytes)) {
	  our_opts++;
	  continue;
	}
      our_opts += our_sig_len;
      break;
    }

  /*
   * Search for the specific option
   */
  the_opt = our_opts;
  opt_name_len = wcslen ( opt_name );
  opt_name_len_bytes = opt_name_len * sizeof ( WCHAR );
  while ( *the_opt != L'\0' && *the_opt != L' ' )
    {
      if (!wv_memcmpeq(the_opt, opt_name, opt_name_len_bytes)) {
	  while ( *the_opt != L'\0' && *the_opt != L' ' && *the_opt != L',' )
	    the_opt++;
	  continue;
	}
      the_opt += opt_name_len;
      break;
    }

  if ( *the_opt == L'\0' || *the_opt == L' ' )
    return NULL;

  /*
   * Next should come "=" 
   */
  if ( *the_opt != L'=' )
    return NULL;

  /*
   * And finally our option's value.  The caller needs
   * to worry about looking past the end of the option 
   */
  the_opt++;
  if ( *the_opt == L'\0' || *the_opt == L' ' )
    return NULL;
  return the_opt;
}

/*
 * Note the exception to the function naming convention.
 * TODO: See if a Makefile change is good enough
 */
NTSTATUS STDCALL
DriverEntry (
  IN PDRIVER_OBJECT DriverObject,
  IN PUNICODE_STRING RegistryPath
 )
{
  NTSTATUS status;
  int i;

  DBG ( "Entry\n" );
  if ( driver__obj_ptr )
    {
      DBG ( "Re-entry not allowed!\n" );
      return STATUS_NOT_SUPPORTED;
    }
  driver__obj_ptr = DriverObject;
  if ( Driver_Globals_Started )
    return STATUS_SUCCESS;
  Debug_Initialize (  );
  status = registry__note_os_load_opts ( &os_load_opts );
  if ( !NT_SUCCESS ( status ) )
    return Error ( "registry__note_os_load_opts", status );

  Driver_Globals_StateHandle = NULL;

  if ( ( Driver_Globals_StateHandle =
	 PoRegisterSystemState ( NULL, ES_CONTINUOUS ) ) == NULL )
    {
      DBG ( "Could not set system state to ES_CONTINUOUS!!\n" );
    }
  /*
   * Set up IRP MajorFunction function table for devices
   * this driver handles
   */
  for ( i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++ )
    DriverObject->MajorFunction[i] = driver_dispatch_not_supported;
  DriverObject->MajorFunction[IRP_MJ_PNP] = driver_dispatch;
  DriverObject->MajorFunction[IRP_MJ_POWER] = driver_dispatch;
  DriverObject->MajorFunction[IRP_MJ_CREATE] = driver_dispatch;
  DriverObject->MajorFunction[IRP_MJ_CLOSE] = driver_dispatch;
  DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = driver_dispatch;
  DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = driver_dispatch;
  DriverObject->MajorFunction[IRP_MJ_SCSI] = driver_dispatch;
  /*
   * Set the driver Unload callback
   */
  DriverObject->DriverUnload = Driver_Unload;
  /*
   * Initialize various modules
   */
  device__init (  );		/* TODO: Check for error */
  bus__init (  );		/* TODO: Check for error */
  disk__init (  );		/* TODO: Check for error */
  filedisk__init (  );		/* TODO: Check for error */
  ramdisk__init (  );		/* TODO: Check for error */

  Driver_Globals_Started = TRUE;
  DBG ( "Exit\n" );
  return STATUS_SUCCESS;
}

static NTSTATUS STDCALL (driver_dispatch_not_supported)(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
  ) {
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoCompleteRequest ( Irp, IO_NO_INCREMENT );
    return Irp->IoStatus.Status;
  }

static
irp__handler_decl (
  create_close
 )
{
  NTSTATUS status = STATUS_SUCCESS;

  Irp->IoStatus.Status = status;
  IoCompleteRequest ( Irp, IO_NO_INCREMENT );
  *completion_ptr = TRUE;
  return status;
}

static
irp__handler_decl (
  not_supported
 )
{
  NTSTATUS status = STATUS_NOT_SUPPORTED;
  Irp->IoStatus.Status = status;
  IoCompleteRequest ( Irp, IO_NO_INCREMENT );
  *completion_ptr = TRUE;
  return status;
}

irp__handling driver__handling_table[] = {
  /*
   * Major, minor, any major?, any minor?, handler
   * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
   * Note that the fall-through case must come FIRST!
   * Why? It sets completion to true, so others won't be called
   */
  {0, 0, TRUE, TRUE, not_supported}
  ,
  {IRP_MJ_CLOSE, 0, FALSE, TRUE, create_close}
  ,
  {IRP_MJ_CREATE, 0, FALSE, TRUE, create_close}
};

size_t driver__handling_table_size = sizeof ( driver__handling_table );

static NTSTATUS STDCALL (driver_dispatch)(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp
  ) {
  NTSTATUS status;
  device__type_ptr dev_ptr;

#ifdef DEBUGIRPS
  Debug_IrpStart ( DeviceObject, Irp );
#endif
  dev_ptr = ( ( driver__dev_ext_ptr ) DeviceObject->DeviceExtension )->device;

  /*
   * We handle IRP_MJ_POWER as an exception 
   */
  if ( dev_ptr->State == Deleted )
    {
      if (IoGetCurrentIrpStackLocation(Irp)->MajorFunction == IRP_MJ_POWER)
	PoStartNextPowerIrp ( Irp );
      Irp->IoStatus.Information = 0;
      Irp->IoStatus.Status = STATUS_NO_SUCH_DEVICE;
      IoCompleteRequest ( Irp, IO_NO_INCREMENT );
#ifdef DEBUGIRPS
      Debug_IrpEnd ( Irp, STATUS_NO_SUCH_DEVICE );
#endif
      return STATUS_NO_SUCH_DEVICE;
    }

  /* Enqueue the IRP for threaded devices, or process immediately. */
  if (dev_ptr->thread) {
      IoMarkIrpPending(Irp);
      ExInterlockedInsertTailList(
          &dev_ptr->irp_list,
          /* Where IRPs can be linked. */
          &Irp->Tail.Overlay.ListEntry,
          &dev_ptr->irp_list_lock
        );
      KeSetEvent(&dev_ptr->thread_wakeup, 0, FALSE);
      status = STATUS_PENDING;
    } else status = dev_ptr->dispatch(DeviceObject, Irp);

  return status;
}

/* Place-holder while implementing a dispatch routine per device class. */
winvblock__lib_func NTSTATUS STDCALL (driver__default_dispatch)(
    IN PDEVICE_OBJECT (dev),
    IN PIRP (irp)
  ) {
    NTSTATUS (status);
    winvblock__bool (completion) = FALSE;

    status = irp__process(
        dev,
        irp,
        IoGetCurrentIrpStackLocation(irp),
        ((driver__dev_ext_ptr) dev->DeviceExtension)->device,
        &completion
      );
    #ifdef DEBUGIRPS
    if (status != STATUS_PENDING)
      Debug_IrpEnd(irp, status);
    #endif

    return status;
  }

static void STDCALL
Driver_Unload (
  IN PDRIVER_OBJECT DriverObject
 )
{
  if ( Driver_Globals_StateHandle != NULL )
    PoUnregisterSystemState ( Driver_Globals_StateHandle );
  bus__finalize (  );
  wv_free(os_load_opts);
  Driver_Globals_Started = FALSE;
  DBG ( "Done\n" );
}

winvblock__lib_func void STDCALL
Driver_CompletePendingIrp (
  IN PIRP Irp
 )
{
#ifdef DEBUGIRPS
  Debug_IrpEnd ( Irp, Irp->IoStatus.Status );
#endif
  IoCompleteRequest ( Irp, IO_NO_INCREMENT );
}

/*
 * Note the exception to the function naming convention
 */
winvblock__lib_func NTSTATUS STDCALL
Error (
  IN PCHAR Message,
  IN NTSTATUS Status
 )
{
  DBG ( "%s: 0x%08x\n", Message, Status );
  return Status;
}
