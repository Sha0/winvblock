#ifndef M_WV_DUMMY_H_
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

/**
 * @file
 *
 * Dummy device specifics
 */

/** Macros */
#define M_WV_DUMMY_H_

#define IOCTL_WV_DUMMY      \
CTL_CODE(                   \
    FILE_DEVICE_CONTROLLER, \
    0x806,                  \
    METHOD_IN_DIRECT,       \
    FILE_WRITE_DATA         \
  )

/* Macro support for dummy ID generation */
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
 * Generate a static-duration, const-qualified WV_S_DUMMY_IDS object
 *
 * @param Qualifiers
 *   Such as 'static', perhaps
 *
 * @param Name
 *   The name of the desired object.  Also used as prefix
 *
 * @param DevId
 *   The device ID
 *
 * @param InstanceId
 *   The instance ID
 *
 * @param HardwareId
 *   The hardware ID(s)
 *
 * @param CompatId
 *   Compatible ID(s)
 *
 * @param DevType
 *   The device type for the dummy PDO
 *
 * @param DevCharacts
 *   The device characteristics for the dummy PDO
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
 *   [Qualifiers] WV_S_DUMMY_IDS *:
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
                                                                      \
static const struct Name ## Struct_ Name ## _ = {                     \
    {                                                                 \
        WV_M_DUMMY_IDS_FILL(Name, Dev)                                \
        WV_M_DUMMY_IDS_FILL(Name, Instance)                           \
        WV_M_DUMMY_IDS_FILL(Name, Hardware)                           \
        WV_M_DUMMY_IDS_FILL(Name, Compat)                             \
        sizeof Name ## _,                                             \
        FIELD_OFFSET(struct Name ## Struct_, Ids),                    \
        DevType,                                                      \
        DevCharacts,                                                  \
      },                                                              \
    WV_M_DUMMY_IDS_LITERALS(DevId, InstanceId, HardwareId, CompatId), \
  };                                                                  \
                                                                      \
Qualifiers const WV_S_DUMMY_IDS * Name = &Name ## _.DummyIds

/** Objects types */
typedef struct WV_DUMMY_IDS WV_S_DUMMY_IDS, * WV_SP_DUMMY_IDS;

/** Function declarations */

/* From ../winvblock/mainbus/dummyirp.c */
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

/** Struct/union type definitions */

/**
 * PnP IDs for a dummy device
 *
 * Each of the offsets _before_ the 'Offset' member is relative to
 * where the IDs begin in the wrapper struct
 */
struct WV_DUMMY_IDS {
    /** For BusQueryDeviceID */
    UINT32 DevOffset;
    UINT32 DevLen;

    /** For BusQueryInstanceID */
    UINT32 InstanceOffset;
    UINT32 InstanceLen;

    /** For BusQueryHardwareIDs */
    UINT32 HardwareOffset;
    UINT32 HardwareLen;

    /** For BusQueryCompatibleIDs */
    UINT32 CompatOffset;
    UINT32 CompatLen;

    /** The length of the wrapper struct, which includes all data */
    UINT32 Len;

    /**
     * The offset from the beginning of this struct to the IDs in
     * the wrapper struct
     */
    UINT32 Offset;

    /** The FILE_DEVICE_xxx type of device */
    DEVICE_TYPE DevType;

    /** The FILE_DEVICE_xxx characteristics of the device */
    ULONG DevCharacteristics;
  };

#endif	/* M_WV_DUMMY_H_ */
