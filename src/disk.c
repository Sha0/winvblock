/**
 * Copyright (C) 2009, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
 * AoE disk specifics
 *
 */

#include <ntddk.h>

#include "winvblock.h"
#include "portable.h"
#include "irp.h"
#include "driver.h"
#include "disk.h"
#include "disk_pnp.h"
#include "disk_dev_ctl.h"
#include "disk_scsi.h"
#include "debug.h"

#ifndef _MSC_VER
static long long
__divdi3 (
  long long u,
  long long v
 )
{
  return u / v;
}
#endif

disk__max_xfer_len_decl ( disk__default_max_xfer_len )
{
  return 1024 * 1024;
}

disk__init_decl ( disk__default_init )
{
  return TRUE;
}

static
irp__handler_decl (
  power
 )
{
  PoStartNextPowerIrp ( Irp );
  Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
  IoCompleteRequest ( Irp, IO_NO_INCREMENT );
  *completion_ptr = TRUE;
  return STATUS_NOT_SUPPORTED;
}

static
irp__handler_decl (
  sys_ctl
 )
{
  NTSTATUS status = Irp->IoStatus.Status;
  IoCompleteRequest ( Irp, IO_NO_INCREMENT );
  *completion_ptr = TRUE;
  return status;
}

irp__handling disk__handling_table[] = {
  /*
   * Major, minor, any major?, any minor?, handler
   */
  {IRP_MJ_DEVICE_CONTROL, 0, FALSE, TRUE, disk_dev_ctl__dispatch}
  ,
  {IRP_MJ_SYSTEM_CONTROL, 0, FALSE, TRUE, sys_ctl}
  ,
  {IRP_MJ_POWER, 0, FALSE, TRUE, power}
  ,
  {IRP_MJ_SCSI, 0, FALSE, TRUE, disk_scsi__dispatch}
  ,
  {IRP_MJ_PNP, 0, FALSE, TRUE, disk_pnp__simple}
  ,
  {IRP_MJ_PNP, IRP_MN_QUERY_CAPABILITIES, FALSE, FALSE,
   disk_pnp__query_capabilities}
  ,
  {IRP_MJ_PNP, IRP_MN_QUERY_BUS_INFORMATION, FALSE, FALSE,
   disk_pnp__query_bus_info}
  ,
  {IRP_MJ_PNP, IRP_MN_QUERY_DEVICE_RELATIONS, FALSE, FALSE,
   disk_pnp__query_dev_relations}
  ,
  {IRP_MJ_PNP, IRP_MN_QUERY_DEVICE_TEXT, FALSE, FALSE, disk_pnp__query_dev_text}
  ,
  {IRP_MJ_PNP, IRP_MN_QUERY_ID, FALSE, FALSE, disk_pnp__query_id}
};

size_t disk__handling_table_size = sizeof ( disk__handling_table );
