/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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

/****
 * @file
 *
 * IRP specifics.
 */

#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "debug.h"
#include "irp.h"

/*** Function definitions */

WVL_M_LIB NTSTATUS STDCALL WvlIrpComplete(
    IN PIRP Irp,
    IN ULONG_PTR Info,
    IN NTSTATUS Status
  ) {
    Irp->IoStatus.Information = Info;
    Irp->IoStatus.Status = Status;
    WVL_M_DEBUG_IRP_END(Irp, Status);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
  }

WVL_M_LIB NTSTATUS STDCALL WvlIrpPassToLower(
    IN PDEVICE_OBJECT Lower,
    IN PIRP Irp
  ) {
    /* Check that a lower device object was passed. */
    if (Lower) {
        DBG("Passing IRP down\n");
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(Lower, Irp);
      }

    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    WVL_M_DEBUG_IRP_END(Irp, STATUS_SUCCESS);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
  }

WVL_M_LIB NTSTATUS STDCALL WvlIrpPassPowerToLower(
    IN PDEVICE_OBJECT Lower,
    IN PIRP Irp
  ) {
    PoStartNextPowerIrp(Irp);

    /* Check that a lower device object was passed. */
    if (Lower) {
        DBG("Passing power IRP down\n");
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(Lower, Irp);
      }

    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    WVL_M_DEBUG_IRP_END(Irp, STATUS_SUCCESS);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
  }

WVL_M_LIB NTSTATUS STDCALL WvlIrpHandleWithTable(
    IN PDEVICE_OBJECT DevObj,
    IN PIRP Irp,
    IN SP_WVL_IRP_HANDLER_TABLE Table
  ) {
    PIO_STACK_LOCATION io_stack_loc;
    UCHAR code;
    SP_WVL_IRP_HANDLER end;
    SP_WVL_IRP_HANDLER handler;

    ASSERT(DevObj);
    ASSERT(Irp);
    ASSERT(Table);
    ASSERT(Table->Count);
    ASSERT(Table->Elements);

    io_stack_loc = IoGetCurrentIrpStackLocation(Irp);

    if (Table->IsMajor)
      code = io_stack_loc->MajorFunction;
      else
      code = io_stack_loc->MinorFunction;

    end = Table->Elements + Table->Count;

    for (handler = Table->Elements; handler < end; ++handler) {
        ASSERT(handler->Function);

        if (code == handler->Code)
          return handler->Function(DevObj, Irp);

        continue;
      }

    DBG("IRP not handled\n");
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    WVL_M_DEBUG_IRP_END(Irp, STATUS_NOT_SUPPORTED);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_NOT_SUPPORTED;
  }
