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
 * WinVBlock thread library.
 */

#include <ntddk.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "thread.h"
#include "debug.h"

static LIST_ENTRY WvlThreads;
static KSPIN_LOCK WvlThreadLock;

static VOID WvlThreadModuleInit(void) {
    static BOOLEAN need_init = TRUE;
    KIRQL irql;

    /* Ensure we are initialized. */
    if (need_init) {
        KeRaiseIrql(HIGH_LEVEL, &irql);
        /* Now that we've locked the whole system, test again. */
        if (need_init) {
            KeInitializeSpinLock(&WvlThreadLock);
            InitializeListHead(&WvlThreads);
            need_init = FALSE;
          }
        KeLowerIrql(irql);
        DBG("Done.\n");
      }
    return;
  }

typedef struct WVL_THREAD_NODE {
    LIST_ENTRY Link;
    WVL_SP_THREAD Thread;
  } WVL_S_THREAD_NODE, * WVL_SP_THREAD_NODE;

/* Thread wrapper. */
static VOID WvlThreadWrapper(IN PVOID context) {
    KIRQL irql;
    WVL_SP_THREAD thread = context;
    WVL_S_THREAD_NODE node = { {0}, thread };

    DBG("Starting.\n");

    /* Track the thread in the global list. */
    KeAcquireSpinLock(&WvlThreadLock, &irql);
    InsertTailList(&WvlThreads, &node.Link);
    KeReleaseSpinLock(&WvlThreadLock, irql);

    /* Initialize the thread for work. */
    thread->Thread = PsGetCurrentThread();
    InitializeListHead(&thread->Main.Link);
    KeInitializeSpinLock(&thread->Lock);

    /* Signal that we're ready for work. */
    thread->State = WvlThreadStateStarted;
    KeSetEvent(&thread->Signal, 0, FALSE);

    /* Launch the thread. */
    thread->Main.Func(&thread->Main);

    /* Make sure the state is reflected. */
    thread->State = WvlThreadStateStopped;

    /* Remove the thread from the global list. */
    KeAcquireSpinLock(&WvlThreadLock, &irql);
    RemoveEntryList(&node.Link);
    KeReleaseSpinLock(&WvlThreadLock, irql);

    DBG("Finished.\n");
    PsTerminateSystemThread(STATUS_SUCCESS);
    DBG("Yikes!\n");
    return;
  }

/**
 * Start a WinVBlock library thread.
 *
 * @v Thread            Points to the WVL_S_THREAD to be started.
 * @ret NTSTATUS        The status of the operation.
 *
 * The thread must have been initialized/assigned with a
 * Func member and a State member of WvlThreadStateNotStarted.
 */
WVL_M_LIB NTSTATUS WvlThreadStart(IN OUT WVL_SP_THREAD Thread) {
    KIRQL irql;
    BOOLEAN abort = FALSE;
    OBJECT_ATTRIBUTES obj_attrs;
    NTSTATUS status;

    if (!Thread || !Thread->Main.Func) {
        DBG("No thread to start.\n");
        return STATUS_INVALID_PARAMETER;
      }
    /* Ensure the thread library is initialized. */
    WvlThreadModuleInit();
    /* Use the global spin-lock to start the thread. */
    KeAcquireSpinLock(&WvlThreadLock, &irql);
    if (Thread->State != WvlThreadStateNotStarted)
      abort = TRUE;
      else
      /* Mark thread as starting to prevent multi-starting. */
      Thread->State = WvlThreadStateStarting;
    KeReleaseSpinLock(&WvlThreadLock, irql);
    if (abort)
      return STATUS_UNSUCCESSFUL;
    /* Start the thread. */
    KeInitializeEvent(&Thread->Signal, SynchronizationEvent, FALSE);
    InitializeObjectAttributes(
        &obj_attrs,
        NULL,
        OBJ_KERNEL_HANDLE,
        NULL,
        NULL
      );
    status = PsCreateSystemThread(
        &Thread->Handle,
        THREAD_ALL_ACCESS,
        &obj_attrs,
        NULL,
        NULL,
        WvlThreadWrapper,
        Thread
      );
    if (!NT_SUCCESS(status)) {
        /* Allow for a retry. */
        Thread->State = WvlThreadStateNotStarted;
        return status;
      }
    /* Wait for the thread to signal that it's been initialized for work. */
    KeWaitForSingleObject(
        &Thread->Signal,
        Executive,
        KernelMode,
        FALSE,
        NULL
      );
    KeResetEvent(&Thread->Signal);
    return status;
  }

typedef struct WVL_THREAD_STOPPER {
    WVL_S_THREAD_ITEM Item;
    WVL_SP_THREAD Thread;
    KEVENT Signal;
  } WVL_S_THREAD_STOPPER, * WVL_SP_THREAD_STOPPER;

/* Change a thread's state to stopping.  Internal use. */
static VOID WvlThreadStop_(IN WVL_SP_THREAD_ITEM item) {
    WVL_SP_THREAD_STOPPER stopper;

    stopper = CONTAINING_RECORD(item, WVL_S_THREAD_STOPPER, Item);
    /* We are executing in the context of the thread, so it's very simple. */
    stopper->Thread->State = WvlThreadStateStopping;
    return;
  }

/**
 * Send a stop signal and wait for the thread to be closed.
 *
 * @v Thread            The thread to stop and close.
 * @ret BOOLEAN         TRUE for success, FALSE for failure.
 */
WVL_M_LIB BOOLEAN STDCALL WvlThreadSendStopAndWait(IN WVL_SP_THREAD Thread) {
    WVL_S_THREAD_STOPPER stopper = {
        /* Item */
        {
            /* Link */
            {0},
            /* Func */
            WvlThreadStop_,
          },
        /* Thread */
        Thread,
        /* Signal */
        {0},
      };
    PETHREAD thread_;
    NTSTATUS status;

    if (!Thread) {
        DBG("No thread.\n");
        return FALSE;
      }
    if (Thread->Thread == PsGetCurrentThread()) {
        DBG("Are you trying to hang forever?!\n");
        return FALSE;
      }
    /* Take out a reference on the thread. */
    status = ObReferenceObjectByHandle(
        Thread->Handle,
        THREAD_ALL_ACCESS,
        *PsThreadType,
        KernelMode,
        &thread_,
        NULL
      );
    if (!NT_SUCCESS(status)) {
        DBG("Couldn't increment reference count.\n");
        return FALSE;
      }
    /* Enqueue the stop request. */
    status = WvlThreadAddItem(Thread, &stopper.Item);
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(thread_);
        return FALSE;
      }
    /* Wait for the thread to complete. */
    KeWaitForSingleObject(
        thread_,
        Executive,
        KernelMode,
        FALSE,
        NULL
      );
    ObDereferenceObject(thread_);
    ZwClose(Thread->Handle);

    return TRUE;
  }

/**
 * Fetch the current thread.
 *
 * @ret WVL_SP_THREAD   The currently running thread, or NULL.
 */
WVL_M_LIB WVL_SP_THREAD WvlThreadGetCurrent(void) {
    PLIST_ENTRY link = &WvlThreads;
    PETHREAD cur_thread;
    KIRQL irql;
    WVL_SP_THREAD current = NULL;

    WvlThreadModuleInit();
    cur_thread = PsGetCurrentThread();
    KeAcquireSpinLock(&WvlThreadLock, &irql);
    /* Walk all globally tracked threads. */
    while ((link = link->Flink) != &WvlThreads) {
        WVL_SP_THREAD_NODE node;

        node = CONTAINING_RECORD(link, WVL_S_THREAD_NODE, Link);
        if (node->Thread->Thread == cur_thread)
          /* Match. */
          current = node->Thread;
      }
    KeReleaseSpinLock(&WvlThreadLock, irql);
    return current;
  }

/**
 * Enqueue an item for a thread.
 *
 * @v Thread            The thread to enqueue and item for.
 * @v Item              The item to enqueue for the thread.
 * @ret BOOLEAN         FALSE for failure, TRUE for success.
 *
 * The thread must be started for this function to succeed.
 */
WVL_M_LIB BOOLEAN STDCALL WvlThreadAddItem(
    IN WVL_SP_THREAD Thread,
    IN WVL_SP_THREAD_ITEM Item
  ) {
    KIRQL irql;

    if (!Thread || !Item) {
        DBG("No thread or no item.\n");
        return FALSE;
      }
    if (Thread->State != WvlThreadStateStarted)
      /* The spin-lock mightn't've been initialized yet. */
      goto err_state;
    KeAcquireSpinLock(&Thread->Lock, &irql);
    if (Thread->State != WvlThreadStateStarted) {
        KeReleaseSpinLock(&Thread->Lock, irql);
        goto err_state;
      }
    InsertTailList(&Thread->Main.Link, &Item->Link);
    KeReleaseSpinLock(&Thread->Lock, irql);
    DBG("Added: %p\n", (PVOID) Item);
    KeSetEvent(&Thread->Signal, 0, FALSE);
    return TRUE;

    err_state:
    DBG("Not adding: %p\n", (PVOID) Item);
    return FALSE;
  }

/**
 * Get the next thread item in a thread's queue.
 *
 * @v Thread                    The thread to fetch an item from.
 * @ret WVL_SP_THREAD_ITEM      The next item in the queue, or NULL.
 *
 * The thread must have been started for this function to succeed.
 * This function must be called within the context of the thread for this
 * function to succeed.
 */
WVL_M_LIB WVL_SP_THREAD_ITEM STDCALL WvlThreadGetItem(
    IN WVL_SP_THREAD Thread
  ) {
    KIRQL irql;
    PLIST_ENTRY link;
    WVL_SP_THREAD_ITEM item;

    if (!Thread || Thread->State == WvlThreadStateNotStarted) {
        DBG("No thread or not started.\n");
        return NULL;
      }
    if (Thread->Thread != PsGetCurrentThread()) {
        DBG("Not called from thread.\n");
        return NULL;
      }
    KeAcquireSpinLock(&Thread->Lock, &irql);
    link = RemoveHeadList(&Thread->Main.Link);
    KeReleaseSpinLock(&Thread->Lock, irql);
    if (link == &Thread->Main.Link)
      return NULL;
    item = CONTAINING_RECORD(link, WVL_S_THREAD_ITEM, Link);
    return item;
  }

typedef struct WVL_THREAD_TEST {
    WVL_S_THREAD_ITEM ThreadItem;
    PCHAR Message;
    BOOLEAN Stop;
  } WVL_S_THREAD_TEST, * WVL_SP_THREAD_TEST;

/* "Self-contained" thread test routine! */
WVL_M_LIB VOID STDCALL WvlThreadTest(IN OUT WVL_SP_THREAD_ITEM Item) {
    static WVL_S_THREAD this_ = {0};
    NTSTATUS status;
    BOOLEAN not_thread = TRUE;
    LARGE_INTEGER timeout;
    WVL_SP_THREAD_ITEM work_item;

    /*
     * When we do not enqueue an item, the caller can test the Func member,
     * but doing so is not race-free for the case where the called function
     * unsets/frees the item before we return to the caller.
     */
    do {
        switch (this_.State) {
            case WvlThreadStateStarted:
            case WvlThreadStateStopping:
            case WvlThreadStateStopped:
              /* Typical. */
              status = WvlThreadAddItem(&this_, Item);
              if (!NT_SUCCESS(status))
                Item->Func = (WVL_FP_THREAD_ITEM) 0;
              return;

            case WvlThreadStateNotStarted:
              /* Try to start the thread. */
              this_.Main.Func = WvlThreadTest;
              WvlThreadStart(&this_);
              /*
               * Take our item back to the beginning of the process.
               * This time, hopefully we can enqueue it!
               */
              continue;

            case WvlThreadStateStarting:
              /* Were we passed this_ ? */
              if (Item == &this_.Main) {
                  /* We are to be the main thread!  Begin the thread loop. */
                  not_thread = FALSE;
                  break;
                }
              /* Otherwise, wait 1 second for the thread to start. */
              timeout.QuadPart = -10000000LL;
              KeDelayExecutionThread(KernelMode, FALSE, &timeout);
              continue;

            default:
              DBG("Unknown thread state!\n");
              return;
          } /* switch state. */
      } while (not_thread);

    /* Wake up at least every 30 seconds. */
    timeout.QuadPart = -300000000LL;

    while (
        (this_.State == WvlThreadStateStarted) ||
        (this_.State == WvlThreadStateStopping)
      ) {
        /* Wait for the work signal or the timeout. */
        KeWaitForSingleObject(
            &this_.Signal,
            Executive,
            KernelMode,
            FALSE,
            &timeout
          );
        /* Reset the work signal. */
        KeResetEvent(&this_.Signal);

        while (work_item = WvlThreadGetItem(&this_)) {
            if (work_item->Func == WvlThreadTest) {
                WVL_SP_THREAD_TEST test;

                /* This item is for us, specifically. */
                test = CONTAINING_RECORD(
                    work_item,
                    WVL_S_THREAD_TEST,
                    ThreadItem
                  );
                if (test->Message)
                  DBG("Hello from '%s'.\n", test->Message);
                if (test->Stop) {
                    /* This is _not_ fire-and-forget. */
                    this_.State = WvlThreadStateStopping;
                    continue;
                  }
                /* Otherwise, this is fire-and-forget, so we free the item. */
                wv_free(test);
                continue;
              }
            /* Launch the item. */
            work_item->Func(work_item);
          } /* while work items. */
        if (this_.State == WvlThreadStateStopping)
          /* The end. */
          this_.State = WvlThreadStateStopped;
      } /* while thread started or stopping. */
    return;
  }

WVL_M_LIB VOID WvlThreadTestMsg(IN PCHAR Message) {
    WVL_SP_THREAD_TEST test = wv_mallocz(sizeof *test);

    if (!test)
      return;
    test->ThreadItem.Func = WvlThreadTest;
    test->Message = Message;
    WvlThreadTest(&test->ThreadItem);
    return;
  }
