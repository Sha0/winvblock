/**
 * Copyright (C) 2010-2012, Shao Miller <sha0.miller@gmail.com>.
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

#  define IOCTL_WV_DUMMY    \
CTL_CODE(                   \
    FILE_DEVICE_CONTROLLER, \
    0x806,                  \
    METHOD_IN_DIRECT,       \
    FILE_WRITE_DATA         \
  )

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
    UINT32 Offset;
    DEVICE_TYPE DevType;
    ULONG DevCharacteristics;
  } WV_S_DUMMY_IDS, * WV_SP_DUMMY_IDS;

/* Macro support for dummy ID generation. */
#define WV_M_DUMMY_IDS_ENUM(prefix_, name_, literal_)             \
  prefix_ ## name_ ## Offset_,                                    \
  prefix_ ## name_ ## Len_ = sizeof (literal_) / sizeof (WCHAR),  \
  prefix_ ## name_ ## End_ =                                      \
    prefix_ ## name_ ## Offset_ + prefix_ ## name_ ## Len_ - 1,

#define WV_M_DUMMY_IDS_LITERALS(dev_id_, inst_id_, hard_id_, compat_id_) \
  dev_id_ L"\0" inst_id_ L"\0" hard_id_ L"\0" compat_id_

#define WV_M_DUMMY_IDS_FILL(prefix_, name_) \
  prefix_ ## name_ ## Offset_,              \
  prefix_ ## name_ ## Len_,

/**
 * Generate a static const WV_S_DUMMY_IDS object.
 *
 * @v Qualifiers        Such as 'static const', perhaps.
 * @v Name              The name of the desired object.  Also used as prefix.
 * @v DevId             The device ID.
 * @v InstanceId        The instance ID.
 * @v HardwareId        The hardware ID(s).
 * @v CompatId          Compatible ID(s).
 * @v DevType           The device type for the dummy PDO.
 * @v DevCharacts       The device characteristics for the dummy PDO.
 *
 * This macro will produce the following:
 *   enum values:
 *     [Name]DevOffset_
 *     [Name]DevLen_
 *     [Name]DevEnd_
 *     [Name]InstanceOffset_
 *     [Name]InstanceLen_
 *     [Name]InstanceEnd_
 *     [Name]HardwareOffset_
 *     [Name]HardwareLen_
 *     [Name]HardwareEnd_
 *     [Name]CompatOffset_
 *     [Name]CompatLen_
 *     [Name]CompatEnd_
 *     [Name]Len_
 *   [Qualifiers] WV_S_DUMMY_IDS:
 *     [Name]
 */
#define WV_M_DUMMY_ID_GEN(                                            \
    Qualifiers,                                                       \
    Name,                                                             \
    DevId,                                                            \
    InstanceId,                                                       \
    HardwareId,                                                       \
    CompatId,                                                         \
    DevType,                                                          \
    DevCharacts                                                       \
  )                                                                   \
                                                                      \
enum {                                                                \
    WV_M_DUMMY_IDS_ENUM(Name, Dev, DevId)                             \
    WV_M_DUMMY_IDS_ENUM(Name, Instance, InstanceId)                   \
    WV_M_DUMMY_IDS_ENUM(Name, Hardware, HardwareId)                   \
    WV_M_DUMMY_IDS_ENUM(Name, Compat, CompatId)                       \
    Name ## Len_                                                      \
  };                                                                  \
                                                                      \
struct Name ## Struct_ {                                              \
    WV_S_DUMMY_IDS DummyIds;                                          \
    WCHAR Ids[Name ## Len_];                                          \
  };                                                                  \
Qualifiers struct Name ## Struct_ Name = {                            \
    {                                                                 \
        WV_M_DUMMY_IDS_FILL(Name, Dev)                                \
        WV_M_DUMMY_IDS_FILL(Name, Instance)                           \
        WV_M_DUMMY_IDS_FILL(Name, Hardware)                           \
        WV_M_DUMMY_IDS_FILL(Name, Compat)                             \
        sizeof (struct Name ## Struct_),                              \
        FIELD_OFFSET(struct Name ## Struct_, Ids),                    \
        DevType,                                                      \
        DevCharacts,                                                  \
      },                                                              \
    WV_M_DUMMY_IDS_LITERALS(DevId, InstanceId, HardwareId, CompatId), \
  }

extern WVL_M_LIB NTSTATUS STDCALL WvDummyAdd(IN const WV_S_DUMMY_IDS *);
extern WVL_M_LIB NTSTATUS STDCALL WvDummyRemove(IN PDEVICE_OBJECT);

/**
 * Handle an IOCTL for creating a dummy PDO
 *
 * @param DeviceObject
 *   The device handling the IOCTL and creating the dummy
 *
 * @param Irp
 *   An IRP with an associated buffer containing WV_S_DUMMY_IDS data
 *
 * @param Irp->AssociatedIrp.SystemBuffer
 *   The input buffer with the dummy PDO details
 *
 * @param Parameters.DeviceIoControl.InputBufferLength (I/O stack location)
 *   The length of the input buffer
 *
 * @return
 *   The status of the operation
 */
extern NTSTATUS STDCALL WvDummyIoctl(
    IN DEVICE_OBJECT * DeviceObject,
    IN IRP * Irp
  );

#endif	/* WV_M_DUMMY_H_ */
