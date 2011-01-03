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

#ifndef WV_M_DEBUG_H_
#  define WV_M_DEBUG_H_

/**
 * @file
 *
 * Debugging message system.
 *
 * We wrap the DbgPrint() function in order to automatically display file,
 * function and line number in any debugging output messages.
 */

/* DBG() macro to output debugging messages. */
#ifdef DBG
#  undef DBG
#endif
/* Uncomment to enable. */
#if 0
#  define DBG(...) (                \
    WvlDebugPrint(                  \
        __FILE__,                   \
        (const PCHAR) __FUNCTION__, \
        __LINE__                    \
      ),                            \
    DbgPrint(__VA_ARGS__)           \
  )
#else
#  define DBG( ... ) ((VOID) 0)
#endif

/* Define to enable verbose IRP debugging output. */
#undef DEBUGIRPS

extern WVL_M_LIB NTSTATUS STDCALL WvlDebugPrint(IN PCHAR, IN PCHAR, IN UINT32);
extern VOID Debug_Initialize(void);
extern WVL_M_LIB VOID STDCALL WvlDebugIrpStart (
  IN PDEVICE_OBJECT,
  IN PIRP
 );
extern VOID STDCALL Debug_IrpEnd (
  IN PIRP Irp,
  IN NTSTATUS Status
 );
extern WVL_M_LIB NTSTATUS STDCALL WvlError(IN PCHAR, IN NTSTATUS);

#endif  /* WV_M_DEBUG_H_ */
