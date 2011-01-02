/**
 * Copyright (C) 2010-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef WV_M_DUMMY_H_
#  define WV_M_DUMMY_H_

/**
 * @file
 *
 * Dummy device specifics.
 */

/* PnP IDs for a dummy device. */
typedef struct WV_DUMMY_IDS {
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
  } WV_S_DUMMY_IDS, * WV_SP_DUMMY_IDS;

/* Macro support for dummy ID generation. */
#define WV_M_DUMMY_IDS_X_ENUM(prefix_, name_, literal_)           \
  prefix_ ## name_ ## Offset_,                                    \
  prefix_ ## name_ ## Len_ = sizeof (literal_) / sizeof (WCHAR),  \
  prefix_ ## name_ ## End_ =                                      \
    prefix_ ## name_ ## Offset_ + prefix_ ## name_ ## Len_ - 1,

#define WV_M_DUMMY_IDS_X_LITERALS(prefix_, name_, literal_) \
  literal_ L"\0"

#define WV_M_DUMMY_IDS_X_FILL(prefix_, name_, literal_) \
  prefix_ ## name_ ## Offset_,                          \
  prefix_ ## name_ ## Len_,

/**
 * Generate a static const WV_S_DUMMY_IDS object.
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
 *   static const WV_S_DUMMY_IDS:
 *     [DummyIds]
 */
#define WV_M_DUMMY_ID_GEN(DummyIds, XMacro)     \
                                                \
enum {                                          \
    XMacro(WV_M_DUMMY_IDS_X_ENUM, DummyIds)     \
    DummyIds ## Len_                            \
  };                                            \
                                                \
static const WCHAR DummyIds ## String_[] =      \
  XMacro(WV_M_DUMMY_IDS_X_LITERALS, DummyIds);  \
                                                \
static const WV_S_DUMMY_IDS DummyIds = {        \
    XMacro(WV_M_DUMMY_IDS_X_FILL, DummyIds)     \
    DummyIds ## Len_,                           \
    DummyIds ## String_                         \
  }

extern WVL_M_LIB NTSTATUS STDCALL WvDummyAdd(
    IN const WV_S_DUMMY_IDS *,
    IN DEVICE_TYPE,
    IN ULONG
  );

#endif	/* WV_M_DUMMY_H_ */
