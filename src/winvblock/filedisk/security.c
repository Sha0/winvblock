/**
 * Copyright (C) 2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
 * File-backed disk security impersonation.
 */

#include <ntifs.h>

#include "portable.h"
#include "wv_stdlib.h"

NTSTATUS STDCALL WvFilediskImpersonate(IN PVOID Impersonation) {
    return SeImpersonateClientEx(
        Impersonation,
        NULL
      );
  }

VOID WvFilediskStopImpersonating(void) {
    PsRevertToSelf();
    return;
  }

NTSTATUS STDCALL WvFilediskCreateClientSecurity(OUT PVOID * Impersonation) {
    PSECURITY_CLIENT_CONTEXT sec_context;
    NTSTATUS status;
    SECURITY_QUALITY_OF_SERVICE sec_qos = {0};

    /* Allocate the client security context. */
    sec_context = wv_malloc(sizeof *sec_context);
    if (!sec_context) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_context;
      }

    /* Build the impersonation context.  Taken from Bo B.'s filedisk. */
    sec_qos.Length = sizeof sec_qos;
    sec_qos.ImpersonationLevel = SecurityImpersonation;
    sec_qos.ContextTrackingMode = SECURITY_STATIC_TRACKING;
    sec_qos.EffectiveOnly = FALSE;
    status = SeCreateClientSecurity(
        PsGetCurrentThread(),
        &sec_qos,
        FALSE,
        sec_context
      );
    if (!NT_SUCCESS(status)) {
        goto err_impersonation;
      }

    *Impersonation = sec_context;
    return status;

    err_impersonation:

    err_context:

    return status;
  }

VOID STDCALL WvFilediskDeleteClientSecurity(IN OUT PVOID * Impersonation) {
    PSECURITY_CLIENT_CONTEXT sec_context;

    if (!*Impersonation)
      return;

    sec_context = *Impersonation;
    #if (NTDDI_VERSION >= NTDDI_WINXP)
    SeDeleteClientSecurity(sec_context);
    #endif
    wv_free(*Impersonation);
    *Impersonation = NULL;
    return;
  }
