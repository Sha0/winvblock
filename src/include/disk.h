/**
 * Copyright (C) 2009-2010, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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
#ifndef WV_M_DISK_H_
#  define WV_M_DISK_H_

/**
 * @file
 *
 * Disk device specifics.
 */

typedef enum WV_DISK_MEDIA_TYPE {
    WvDiskMediaTypeFloppy,
    WvDiskMediaTypeHard,
    WvDiskMediaTypeOptical,
    WvDiskMediaTypes
  } WV_E_DISK_MEDIA_TYPE, * WV_EP_DISK_MEDIA_TYPE;

typedef char WV_A_DISK_BOOT_SECT[512];
typedef WV_A_DISK_BOOT_SECT * WV_AP_DISK_BOOT_SECT;

extern winvblock__bool WvDiskIsRemovable[WvDiskMediaTypes];
extern PWCHAR WvDiskCompatIds[WvDiskMediaTypes];

typedef enum WV_DISK_IO_MODE {
    WvDiskIoModeRead,
    WvDiskIoModeWrite,
    WvDiskIoModes
  } WV_E_DISK_IO_MODE, * WV_EP_DISK_IO_MODE;

/* Forward declaration. */
typedef struct WV_DISK_T WV_S_DISK_T, * WV_SP_DISK_T;

/**
 * I/O Request.
 *
 * @v dev_ptr           Points to the disk's device structure.
 * @v mode              Read / write mode.
 * @v start_sector      First sector for request.
 * @v sector_count      Number of sectors to work with.
 * @v buffer            Buffer to read / write sectors to / from.
 * @v irp               Interrupt request packet for this request.
 */
typedef NTSTATUS STDCALL WV_F_DISK_IO(
    IN WV_SP_DEV_T,
    IN WV_E_DISK_IO_MODE,
    IN LONGLONG,
    IN winvblock__uint32,
    IN winvblock__uint8_ptr,
    IN PIRP
  );
typedef WV_F_DISK_IO * WV_FP_DISK_IO;

/**
 * Maximum transfer length response routine.
 *
 * @v disk_ptr        The disk being queried.
 * @ret UINT32        The maximum transfer length.
 */
typedef winvblock__uint32 WV_F_DISK_MAX_XFER_LEN(IN WV_SP_DISK_T);
typedef WV_F_DISK_MAX_XFER_LEN * WV_FP_DISK_MAX_XFER_LEN;

/**
 * Disk initialization routine.
 *
 * @v disk_ptr        The disk device being initialized.
 * @ret BOOLEAN       FALSE if initialization failed, otherwise TRUE
 */
typedef winvblock__bool STDCALL WV_F_DISK_INIT(IN WV_SP_DISK_T);
typedef WV_F_DISK_INIT * WV_FP_DISK_INIT;

/**
 * Disk close routine.
 *
 * @v disk_ptr        The disk device being closed.
 */
typedef void STDCALL WV_F_DISK_CLOSE(IN WV_SP_DISK_T);
typedef WV_F_DISK_CLOSE * WV_FP_DISK_CLOSE;

typedef struct WV_DISK_OPS {
    WV_FP_DISK_IO Io;
    WV_FP_DISK_MAX_XFER_LEN MaxXferLen;
    WV_FP_DISK_INIT Init;
    WV_FP_DISK_CLOSE Close;
  } WV_S_DISK_OPS, * WV_SP_DISK_OPS;

struct WV_DISK_T {
    WV_SP_DEV_T Dev;
    KEVENT SearchEvent;
    KSPIN_LOCK SpinLock;
    winvblock__bool BootDrive;
    winvblock__bool Unmount;
    WV_E_DISK_MEDIA_TYPE Media;
    WV_S_DISK_OPS disk_ops;
    ULONGLONG LBADiskSize;
    ULONGLONG Cylinders;
    winvblock__uint32 Heads;
    winvblock__uint32 Sectors;
    winvblock__uint32 SectorSize;
    winvblock__uint32 SpecialFileCount;
    WV_FP_DEV_FREE prev_free;
    LIST_ENTRY tracking;
    winvblock__any_ptr ext;
  };

/* Yield a pointer to the disk. */
#define disk__get_ptr(dev_ptr) ((WV_SP_DISK_T) dev_ptr->ext)

/* An MBR C/H/S address and ways to access its components. */
typedef winvblock__uint8 chs[3];

#define     chs_head(chs) chs[0]
#define   chs_sector(chs) (chs[1] & 0x3F)
#define chs_cyl_high(chs) (((winvblock__uint16) (chs[1] & 0xC0)) << 2)
#define  chs_cyl_low(chs) ((winvblock__uint16) chs[2])
#define chs_cylinder(chs) (chs_cyl_high(chs) | chs_cyl_low(chs))

#ifdef _MSC_VER
#  pragma pack(1)
#endif

/* An MBR. */
winvblock__def_struct (mbr) {
    winvblock__uint8 code[440];
    winvblock__uint32 disk_sig;
    winvblock__uint16 pad;
    struct {
        winvblock__uint8 status;
        chs chs_start;
        winvblock__uint8 type;
        chs chs_end;
        winvblock__uint32 lba_start;
        winvblock__uint32 lba_count;
      } partition[4] __attribute__((packed));
    winvblock__uint16 mbr_sig;
  } __attribute__((__packed__));

#ifdef _MSC_VER
#  pragma pack()
#endif

extern WV_F_DISK_IO disk__io;
extern WV_F_DISK_MAX_XFER_LEN disk__max_xfer_len;

/**
 * Attempt to guess a disk's geometry.
 *
 * @v boot_sect_ptr     The MBR or VBR with possible geometry clues.
 * @v disk_ptr          The disk to set the geometry for.
 */
extern winvblock__lib_func void disk__guess_geometry(
    IN WV_AP_DISK_BOOT_SECT,
    IN OUT WV_SP_DISK_T disk_ptr
  );

/**
 * Create a new disk.
 *
 * @ret disk_ptr        The address of a new disk, or NULL for failure.
 *
 * This function should not be confused with a PDO creation routine, which is
 * actually implemented for each device type.  This routine will allocate a
 * WV_S_DISK_T, track it in a global list, as well as populate the disk
 * with default values.
 */
extern winvblock__lib_func WV_SP_DISK_T disk__create(void);

extern NTSTATUS disk__module_init(void);

#endif  /* WV_M_DISK_H_ */
