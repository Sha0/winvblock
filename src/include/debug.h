/*
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
 *
 * Jul-03-2009@22:39: Initial revision.
 */

#ifndef WVL_M_DEBUG_H_
#  define WVL_M_DEBUG_H_

/**
 * @file
 *
 * Debugging message system.
 *
 * We wrap the DbgPrint() function in order to automatically display file,
 * function and line number in any debugging output messages.
 */

/** Debugging choices. */

/* Debug messages. */
#define WVL_M_DEBUG 0

/* Include file. */
#define WVL_M_DEBUG_FILE 1

/* Include line number.  This depends on WVL_M_DEBUG_FILE */
#define WVL_M_DEBUG_LINE 1

/* Include thread. */
#define WVL_M_DEBUG_THREAD 1

/* Verbose IRP debugging.  This depends on WVL_M_DEBUG */
#define WVL_M_DEBUG_IRPS 0

/** End of debugging choices. */

/* DBG() macro to output debugging messages. */
#ifdef DBG
#  undef DBG
#endif
#if WVL_M_DEBUG
#  define DBG(...) (                \
    WvlDebugPrint(                  \
        __FILE__,                   \
        (const PCHAR) __FUNCTION__, \
        __LINE__,                   \
        __VA_ARGS__                 \
      ),                            \
    DbgPrint(__VA_ARGS__)           \
  )
extern WVL_M_LIB NTSTATUS STDCALL WvlDebugPrint(
    IN PCHAR,
    IN PCHAR,
    IN UINT32,
    IN PCHAR,
    ...
  );
#else
#  define DBG(...) ((VOID) 0)
#endif

/* Establish macros for verbose IRP debugging messages, if applicable. */
#if WVL_M_DEBUG && WVL_M_DEBUG_IRPS
#  define WVL_M_DEBUG_IRP_START(Do_, Irp_) (WvlDebugIrpStart((Do_), (Irp_)))
#  define WVL_M_DEBUG_IRP_END(Irp_, Status_) (WvlDebugIrpEnd((Irp_), (Status_)))
extern WVL_M_LIB VOID STDCALL WvlDebugIrpStart(IN PDEVICE_OBJECT, IN PIRP);
extern VOID STDCALL WvlDebugIrpEnd(IN PIRP, IN NTSTATUS);
#else
#  define WVL_M_DEBUG_IRP_START(Do_, Irp_) ((VOID) 0)
#  define WVL_M_DEBUG_IRP_END(Irp_, Status_) ((VOID) 0)
#endif

extern VOID WvlDebugModuleInit(void);
extern VOID WvlDebugModuleUnload(void);
extern WVL_M_LIB NTSTATUS STDCALL WvlError(IN PCHAR, IN NTSTATUS);

#endif  /* WVL_M_DEBUG_H_ */
