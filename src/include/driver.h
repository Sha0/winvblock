/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef WV_M_DRIVER_H_
#  define WV_M_DRIVER_H_

/**
 * @file
 *
 * Driver specifics.
 */

/* For testing and debugging */
#if 0
#  define RIS
#  define DEBUGIRPS
#  define DEBUGMOSTPROTOCOLCALLS
#  define DEBUGALLPROTOCOLCALLS
#endif

#define POOLSIZE 2048

/* An unfortunate forward declaration.  Definition resolved in device.h */
struct WV_DEV_T;

extern PDRIVER_OBJECT WvDriverObj;
extern WVL_M_LIB VOID STDCALL WvDriverCompletePendingIrp(IN PIRP);
/* Note the exception to the function naming convention. */
extern WVL_M_LIB NTSTATUS STDCALL Error(IN PCHAR, IN NTSTATUS);
/* Note the exception to the function naming convention. */
extern NTSTATUS STDCALL DriverEntry(
    IN PDRIVER_OBJECT,
    IN PUNICODE_STRING
  );

/* The physical/function device object's (PDO's/FDO's) DeviceExtension */
typedef struct WV_DEV_EXT {
    struct WV_DEV_T * device;
  } WV_S_DEV_EXT, * WV_SP_DEV_EXT;

/* The prototype for a device IRP dispatch. */
typedef NTSTATUS STDCALL driver__dispatch_func(
    IN PDEVICE_OBJECT,
    IN PIRP
  );
/* PnP IDs for a dummy device. */
typedef struct WV_DRIVER_DUMMY_IDS {
    UINT32 DevOffset;
    UINT32 DevLen;
    UINT32 InstanceOffset;
    UINT32 InstanceLen;
    UINT32 HardwareOffset;
    UINT32 HardwareLen;
    UINT32 CompatOffset;
    UINT32 CompatLen;
    UINT32 Len;
    const WCHAR * Ids;
    WCHAR Text[1];
  } WV_S_DRIVER_DUMMY_IDS, * WV_SP_DRIVER_DUMMY_IDS;

/* Macro support for dummy ID generation. */
#define WV_M_DRIVER_DUMMY_IDS_X_ENUM(prefix_, name_, literal_)    \
  prefix_ ## name_ ## Offset_,                                    \
  prefix_ ## name_ ## Len_ = sizeof (literal_) / sizeof (WCHAR),  \
  prefix_ ## name_ ## End_ =                                      \
    prefix_ ## name_ ## Offset_ + prefix_ ## name_ ## Len_ - 1,

#define WV_M_DRIVER_DUMMY_IDS_X_LITERALS(prefix_, name_, literal_) \
  literal_ L"\0"

#define WV_M_DRIVER_DUMMY_IDS_X_FILL(prefix_, name_, literal_)  \
  prefix_ ## name_ ## Offset_,                                  \
  prefix_ ## name_ ## Len_,

/**
 * Generate a static const WV_S_DRIVER_DUMMY_IDS object.
 *
 * @v DummyIds          The name of the desired object.  Also used as prefix.
 * @v XMacro            The x-macro with the ID text.
 *
 * This macro will produce the following:
 *   enum values:
 *     [DummyIds]DevOffset_
 *     [DummyIds]DevLen_
 *     [DummyIds]DevEnd_
 *     [DummyIds]InstanceOffset_
 *     [DummyIds]InstanceLen_
 *     [DummyIds]InstanceEnd_
 *     [DummyIds]HardwareOffset_
 *     [DummyIds]HardwareLen_
 *     [DummyIds]HardwareEnd_
 *     [DummyIds]CompatOffset_
 *     [DummyIds]CompatLen_
 *     [DummyIds]CompatEnd_
 *     [DummyIds]Len_
 *   WCHAR[]:
 *     [DummyIds]String_
 *   static const WV_S_DRIVER_DUMMY_IDS:
 *     [DummyIds]
 */
#define WV_M_DRIVER_DUMMY_ID_GEN(DummyIds, XMacro)    \
                                                      \
enum {                                                \
    XMacro(WV_M_DRIVER_DUMMY_IDS_X_ENUM, DummyIds)    \
    DummyIds ## Len_                                  \
  };                                                  \
                                                      \
static const WCHAR DummyIds ## String_[] =            \
  XMacro(WV_M_DRIVER_DUMMY_IDS_X_LITERALS, DummyIds); \
                                                      \
static const WV_S_DRIVER_DUMMY_IDS DummyIds = {       \
    XMacro(WV_M_DRIVER_DUMMY_IDS_X_FILL, DummyIds)    \
    DummyIds ## Len_,                                 \
    DummyIds ## String_                               \
  }

extern WVL_M_LIB NTSTATUS STDCALL driver__complete_irp(
    IN PIRP,
    IN ULONG_PTR,
    IN NTSTATUS
  );
extern WVL_M_LIB BOOLEAN STDCALL WvDriverBusAddDev(
    IN WV_SP_DEV_T
  );
extern NTSTATUS STDCALL WvDriverGetDevCapabilities(
    IN PDEVICE_OBJECT,
    IN PDEVICE_CAPABILITIES
  );
extern WVL_M_LIB NTSTATUS STDCALL WvDriverAddDummy(
    IN const WV_S_DRIVER_DUMMY_IDS *,
    IN DEVICE_TYPE,
    IN ULONG
  );
extern WVL_M_LIB NTSTATUS STDCALL WvDriverDummyIds(
    IN PIRP,
    IN WV_SP_DRIVER_DUMMY_IDS
  );

#endif	/* WV_M_DRIVER_H_ */
