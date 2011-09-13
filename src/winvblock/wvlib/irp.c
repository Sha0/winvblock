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

/**
 * @file
 *
 * IRP specifics.
 */

#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "debug.h"

/**
 * Common IRP completion routine.
 *
 * @v Irp               Points to the IRP to complete.
 * @v Info              Number of bytes returned for the IRP, or 0.
 * @v Status            Status for the IRP to complete.
 * @ret NTSTATUS        Returns the status value, as passed.
 */
WVL_M_LIB NTSTATUS STDCALL WvlIrpComplete(
    IN PIRP Irp,
    IN ULONG_PTR Info,
    IN NTSTATUS Status
  ) {
    Irp->IoStatus.Information = Info;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    WVL_M_DEBUG_IRP_END(Irp, Status);
    return Status;
  }
