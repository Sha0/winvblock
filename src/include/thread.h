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
#ifndef M_THREAD_H_

/****
 * @file
 *
 * WinVBlock thread library.
 */

/*** Macros */
#define M_THREAD_H_

/*** Constants */
enum E_WVL_THREAD_STATE_ {
    WvlThreadStateNotStarted,
    WvlThreadStateStarting,
    WvlThreadStateStarted,
    WvlThreadStateStopping,
    WvlThreadStateStopped,
    WvlThreadStates
  };

/*** Object types */
typedef struct S_WVL_THREAD_ITEM_ WVL_S_THREAD_ITEM, * WVL_SP_THREAD_ITEM;
typedef enum E_WVL_THREAD_STATE_ WVL_E_THREAD_STATE, * WVL_EP_THREAD_STATE;
typedef struct S_WVL_THREAD_ WVL_S_THREAD, * WVL_SP_THREAD;

/*** Function types */
typedef VOID STDCALL WVL_F_THREAD_ITEM(IN OUT WVL_SP_THREAD_ITEM);
typedef WVL_F_THREAD_ITEM * WVL_FP_THREAD_ITEM;

/*** Function declarations */
extern WVL_M_LIB NTSTATUS WvlThreadStart(IN OUT WVL_SP_THREAD);
extern WVL_M_LIB BOOLEAN STDCALL WvlThreadSendStopAndWait(IN WVL_SP_THREAD);
extern WVL_M_LIB WVL_SP_THREAD WvlThreadGetCurrent(void);
extern WVL_M_LIB BOOLEAN STDCALL WvlThreadAddItem(
    IN WVL_SP_THREAD,
    IN WVL_SP_THREAD_ITEM
  );
extern WVL_M_LIB WVL_SP_THREAD_ITEM STDCALL WvlThreadGetItem(IN WVL_SP_THREAD);
extern WVL_M_LIB VOID STDCALL WvlThreadTest(IN OUT WVL_SP_THREAD_ITEM);
extern WVL_M_LIB VOID WvlThreadTestMsg(IN PCHAR);

/*** Struct/union definitions */
struct S_WVL_THREAD_ITEM_ {
    LIST_ENTRY Link;
    WVL_FP_THREAD_ITEM Func;
  };

struct S_WVL_THREAD_ {
    WVL_S_THREAD_ITEM Main;
    WVL_E_THREAD_STATE State;
    KSPIN_LOCK Lock;
    KEVENT Signal;
    HANDLE Handle;
    PETHREAD Thread;
  };

#endif  /* M_THREAD_H_ */
