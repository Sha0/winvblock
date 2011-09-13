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
#ifndef M_IRP_H_

/****
 * @file
 *
 * IRP specifics.
 */

/*** Macros */
#define M_IRP_H_

/*** Function declarations */

/**
 * Common IRP completion routine.
 *
 * @v Irp               Points to the IRP to complete.
 * @v Info              Number of bytes returned for the IRP, or 0.
 * @v Status            Status for the IRP to complete.
 * @ret NTSTATUS        Returns the status value, as passed.
 */
extern WVL_M_LIB NTSTATUS STDCALL WvlIrpComplete(
    IN PIRP,
    IN ULONG_PTR,
    IN NTSTATUS
  );

/**
 * Pass an IRP on to a lower device object.
 *
 * @v Lower             Points to the lower device object in the stack.
 * @v Irp               Points to the IRP to pass.
 * @ret NTSTATUS        Returns the status value, as returned by the
 *                      lower device object, or STATUS_SUCCESS, if
 *                      no lower device object was passed.
 */
extern WVL_M_LIB NTSTATUS STDCALL WvlIrpPassToLower(
    IN PDEVICE_OBJECT Lower,
    IN PIRP Irp
  );

/**
 * Pass a power IRP on to a lower device object.
 *
 * @v Lower             Points to the lower device object in the stack.
 * @v Irp               Points to the power IRP to pass.
 * @ret NTSTATUS        Returns the status value, as returned by the
 *                      lower device object, or STATUS_SUCCESS, if
 *                      no lower device object was passed.
 */
extern WVL_M_LIB NTSTATUS STDCALL WvlIrpPassPowerToLower(
    IN PDEVICE_OBJECT Lower,
    IN PIRP Irp
  );

#endif	/* M_IRP_H_ */
