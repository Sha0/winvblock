/**
 * Copyright (C) 2009-2012, Shao Miller <sha0.miller@gmail.com>.
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
 * GRUB4DOS file-backed disk specifics.
 */

#include <ntddk.h>
#include <initguid.h>
#include <ntddstor.h>

#include "portable.h"
#include "winvblock.h"
#include "irp.h"
#include "wv_stdlib.h"
#include "wv_string.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "thread.h"
#include "filedisk.h"
#include "debug.h"
#include "x86.h"
#include "safehook.h"
#include "grub4dos.h"
#include "byte.h"
#include "msvhd.h"

/**
 * Check if a disk might be the matching backing disk for
 * a GRUB4DOS sector-mapped disk by checking for an MBR signature.
 *
 * @v file              HANDLE to an open disk.
 * @v filedisk          Points to the filedisk to match against.
 */
static BOOLEAN STDCALL WvFilediskG4dCheckDiskMatchMbrSig_(
    IN HANDLE file,
    IN WV_SP_FILEDISK_T filedisk
  ) {
    BOOLEAN ok = FALSE;
    WVL_SP_DISK_MBR buf;
    NTSTATUS status;
    IO_STATUS_BLOCK io_status;

    /* Allocate a buffer for testing for an MBR signature. */
    buf = wv_malloc(sizeof *buf);
    if (buf == NULL) {
        goto err_alloc;
      }

    /* Read a potential MBR. */
    status = ZwReadFile(
        file,
        NULL,
        NULL,
        NULL,
        &io_status,
        buf,
        sizeof *buf,
        &filedisk->offset,
        NULL
      );
    if (!NT_SUCCESS(status))
      goto err_read;

    /* Check for the MBR signature. */
    if (buf->mbr_sig == 0xAA55)
      ok = TRUE;

    err_read:

    wv_free(buf);
    err_alloc:

    return ok;
  }

/**
 * Check if a disk might be the matching backing disk for
 * a GRUB4DOS sector-mapped disk by checking for a .VHD footer.
 *
 * @v file              HANDLE to an open disk.
 * @v filedisk_ptr      Points to the filedisk to match against.
 */
static BOOLEAN STDCALL WvFilediskG4dCheckDiskMatchVHD_(
    IN HANDLE file,
    IN WV_SP_FILEDISK_T filedisk_ptr
  ) {
    BOOLEAN ok = FALSE;
    WV_SP_MSVHD_FOOTER buf;
    NTSTATUS status;
    LARGE_INTEGER end_part;
    IO_STATUS_BLOCK io_status;

    /* Allocate a buffer for testing for a MS .VHD footer. */
    buf = wv_malloc(sizeof *buf);
    if (buf == NULL) {
        goto err_alloc;
      }

    /*
     * Read in the buffer.  Note that we adjust for the .VHD footer (plus
     * prefixed padding for an .ISO) that we truncated from the reported disk
     * size earlier when the disk mapping was found.
     */
    end_part.QuadPart =
      filedisk_ptr->offset.QuadPart +
      (filedisk_ptr->disk->LBADiskSize * filedisk_ptr->disk->SectorSize) +
      filedisk_ptr->disk->SectorSize -
      sizeof *buf;
    status = ZwReadFile(
        file,
        NULL,
        NULL,
        NULL,
        &io_status,
        buf,
        sizeof *buf,
        &end_part,
        NULL
      );
    if (!NT_SUCCESS(status))
      goto err_read;

    /* Adjust the footer's byte ordering. */
    msvhd__footer_swap_endian(buf);

    /* Examine .VHD fields for validity. */
    if (!wv_memcmpeq(&buf->cookie, "conectix", sizeof buf->cookie))
      goto err_cookie;
    if (buf->file_ver.val != 0x10000)
      goto err_ver;
    if (buf->data_offset.val != 0xffffffff)
      goto err_data_offset;
    if (buf->orig_size.val != buf->cur_size.val)
      goto err_size;
    if (buf->type.val != 2)
      goto err_type;

    /* Match against our expected disk size. */
    if (
        (filedisk_ptr->disk->LBADiskSize * filedisk_ptr->disk->SectorSize ) !=
        buf->cur_size.val
      )
      goto err_real_size;

    /* Passed the tests.  We expect it to be our backing disk. */
    ok = TRUE;

    err_real_size:

    err_type:

    err_size:

    err_data_offset:

    err_ver:

    err_cookie:

    err_read:

    wv_free(buf);
    err_alloc:

    return ok;
  }

/**
 * Check if a disk might be the matching backing disk for
 * a GRUB4DOS sector-mapped disk by checking for some ISO9660
 * filesystem magic (in the first volume descriptor).
 *
 * @v file              HANDLE to an open disk.
 * @v filedisk          Points to the filedisk to match against.
 */
static BOOLEAN STDCALL WvFilediskG4dCheckDiskMatchIsoSig_(
    IN HANDLE file,
    IN WV_SP_FILEDISK_T filedisk
  ) {
    #ifdef _MSC_VER
    #  pragma pack(1)
    #endif
    struct vol_desc {
        UCHAR type;
        UCHAR id[5];
    } __attribute__((__packed__));
    #ifdef _MSC_VER
    #  pragma pack()
    #endif
    enum { first_vol_desc_byte_offset = 32768 };
    static CHAR iso_magic[] = "CD001";
    BOOLEAN ok = FALSE;
    struct vol_desc * buf;
    LARGE_INTEGER start;
    NTSTATUS status;
    IO_STATUS_BLOCK io_status;

    /* Allocate a buffer for testing for some ISO9660 magic. */
    buf = wv_malloc(filedisk->disk->SectorSize);
    if (buf == NULL) {
        goto err_alloc;
      }

    /* Calculate where a volume descriptor might be. */
    start.QuadPart = filedisk->offset.QuadPart + first_vol_desc_byte_offset;

    /* Read a potential ISO9660 volume descriptor. */
    status = ZwReadFile(
        file,
        NULL,
        NULL,
        NULL,
        &io_status,
        buf,
        filedisk->disk->SectorSize,
        &start,
        NULL
      );
    if (!NT_SUCCESS(status))
      goto err_read;

    /* Check for the ISO9660 magic. */
    if (wv_memcmpeq(buf->id, iso_magic, sizeof iso_magic - 1))
      ok = TRUE;

    err_read:

    wv_free(buf);
    err_alloc:

    return ok;
  }

/**
 * Check if a disk might be the matching backing disk for
 * a GRUB4DOS sector-mapped disk.
 *
 * @v file              HANDLE to an open disk.
 * @v filedisk          Points to the filedisk to match against.
 */
static BOOLEAN STDCALL WvFilediskG4dCheckDiskMatch_(
    IN HANDLE file,
    IN WV_SP_FILEDISK_T filedisk
  ) {
    BOOLEAN ok = FALSE;

    switch (filedisk->disk->Media) {
        case WvlDiskMediaTypeFloppy:
          break;

        case WvlDiskMediaTypeHard:
          ok = WvFilediskG4dCheckDiskMatchMbrSig_(file, filedisk);
          if (ok)
            break;
          ok = WvFilediskG4dCheckDiskMatchVHD_(file, filedisk);
          break;
        case WvlDiskMediaTypeOptical:
          ok = WvFilediskG4dCheckDiskMatchIsoSig_(file, filedisk);
          break;
      }
    return ok;
  }

/* A work item for a filedisk thread to find the backing disk. */
typedef struct S_WV_FILEDISK_G4D_FIND_BACKING_DISK_ {
    WVL_S_THREAD_ITEM item[1];
    WV_SP_FILEDISK_T filedisk;
  }
  S_WV_FILEDISK_G4D_FIND_BACKING_DISK,
  * SP_WV_FILEDISK_G4D_FIND_BACKING_DISK;

/**
 * Stalls the arrival of a GRUB4DOS sector-mapped disk until
 * the backing disk is found, and stalls driver re-initialization.
 */
static VOID STDCALL WvFilediskG4dFindBackingDisk_(
    IN OUT WVL_SP_THREAD_ITEM item
  ) {
    static U_WV_LARGE_INT delay_time = {-10000000LL};
    SP_WV_FILEDISK_G4D_FIND_BACKING_DISK finder = CONTAINING_RECORD(
        item,
        S_WV_FILEDISK_G4D_FIND_BACKING_DISK,
        item[0]
      );
    WV_SP_FILEDISK_T filedisk_ptr;
    NTSTATUS status;
    GUID disk_guid = GUID_DEVINTERFACE_DISK;
    PWSTR sym_links;
    PWCHAR pos;
    HANDLE file;
    int count = 0;
    KIRQL irql;

    /* Establish pointer to the filedisk. */
    filedisk_ptr = finder->filedisk;

    /* Free the work item. */
    wv_free(item);

    do {
    /*
     * Find the backing disk and use it.  We walk a list
     * of unicode disk device names and check each one.
     */
    status = IoGetDeviceInterfaces(&disk_guid, NULL, 0, &sym_links);
    if (!NT_SUCCESS(status))
      goto retry;
    pos = sym_links;
    while (*pos != UNICODE_NULL) {
        UNICODE_STRING path;
        OBJECT_ATTRIBUTES obj_attrs;
        IO_STATUS_BLOCK io_status;

        RtlInitUnicodeString(&path, pos);
        InitializeObjectAttributes(
            &obj_attrs,
            &path,
            OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
            NULL,
            NULL
          );
        /* Try this disk. */
        file = 0;
        status = ZwCreateFile(
            &file,
            GENERIC_READ | GENERIC_WRITE,
            &obj_attrs,
            &io_status,
            NULL,
            FILE_ATTRIBUTE_NORMAL,
            FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE,
            FILE_OPEN,
            FILE_NON_DIRECTORY_FILE |
              FILE_RANDOM_ACCESS |
              FILE_SYNCHRONOUS_IO_NONALERT,
            NULL,
            0
          );
        /* If we could open it, check it out. */
        if (NT_SUCCESS(status)) {
            /* If we liked this disk as our backing disk, stop looking. */
            if (WvFilediskG4dCheckDiskMatch_(file, filedisk_ptr))
              break;
          }
        /* We could not open this disk or didn't like it.  Try the next one. */
        if (file)
          ZwClose(file);
        while (*pos != UNICODE_NULL)
          pos++;
        pos++;
      } /* while */
    wv_free(sym_links);
    /* If we did not find the backing disk, we are a dud. */
    if (!NT_SUCCESS(status))
      goto retry;
    /* Use the backing disk and report the sector-mapped disk. */
    filedisk_ptr->file = file;
    if (!WvBusAddDev(filedisk_ptr->Dev))
      WvDevFree(filedisk_ptr->Dev);

    /* Release the driver re-initialization stall. */
    KeAcquireSpinLock(&WvFindDiskLock, &irql);
    --WvFindDisk;
    KeReleaseSpinLock(&WvFindDiskLock, irql);

    DBG("Found backing disk for filedisk %p\n", (PVOID) filedisk_ptr);
    return;

    retry:
    /* Sleep. */
    DBG("Sleeping...");
    KeDelayExecutionThread(KernelMode, FALSE, &delay_time.large_int);
    } while (count++ < 10);
  }

/**
 * Initiate a search for a GRUB4DOS backing disk.
 *
 * @v filedisk          The filedisk whose backing disk we need.
 */
static BOOLEAN STDCALL WvFilediskG4dFindBackingDisk(
    IN WV_SP_FILEDISK_T filedisk
  ) {
    SP_WV_FILEDISK_G4D_FIND_BACKING_DISK finder;
    KIRQL irql;

    finder = wv_malloc(sizeof *finder);
    if (!finder) {
        DBG("Couldn't allocate work item!\n");
        goto err_finder;
      }

    KeAcquireSpinLock(&WvFindDiskLock, &irql);
    ++WvFindDisk;
    KeReleaseSpinLock(&WvFindDiskLock, irql);

    finder->item->Func = WvFilediskG4dFindBackingDisk_;
    finder->filedisk = filedisk;
    /* Add the hot-swapper work item. */
    if (!WvlThreadAddItem(filedisk->Thread, finder->item)) {
        DBG("Couldn't add work item!\n");
        goto err_work_item;
      }

    /* The thread is responsible for freeing the work item. */
    return TRUE;

    err_work_item:

    KeAcquireSpinLock(&WvFindDiskLock, &irql);
    --WvFindDisk;
    KeReleaseSpinLock(&WvFindDiskLock, irql);

    wv_free(finder);
    err_finder:

    return FALSE;
  }

typedef struct WV_FILEDISK_GRUB4DOS_DRIVE_FILE_SET {
    UCHAR int13_drive_num;
    char *filepath;
  }
  WV_S_FILEDISK_GRUB4DOS_DRIVE_FILE_SET,
  * WV_SP_FILEDISK_GRUB4DOS_DRIVE_FILE_SET;

static VOID STDCALL process_param_block(
    const char * param_block,
    WV_SP_FILEDISK_GRUB4DOS_DRIVE_FILE_SET sets
  ) {
    const char * end = param_block + 2047;
    const char sig[] = "#!GRUB4DOS";
    const char ver[] = "v=1";
    int i = 0;

    /* Check signature. */
    if (!wv_memcmpeq(param_block, sig, sizeof sig)) {
        DBG("RAM disk is not a parameter block.  Skipping.\n");
        return;
      }
    param_block += sizeof sig;
    /*
     * Looks like a parameter block someone passed from GRUB4DOS.
     * Check the version.
     */
    if (!wv_memcmpeq(param_block, ver, sizeof ver)) {
        DBG("Parameter block version unsupported.  Skipping.\n");
        return;
      }
    param_block += sizeof ver;
    /*
     * We are interested in {filepath, NUL, X} sets,
     * where X is INT13h drive num.
     */
    RtlZeroMemory(sets, sizeof *sets * 8);
    while (param_block < end && i != 8) {
        const char *walker = param_block;

        /* Walk to ASCII NUL terminator. */
        while (walker != end && *walker)
          walker++;
        if (walker == param_block || walker == end)
          /* End of filenames or run-away sequence. */
          break;
        walker++;
        /* Make a note of the filename.  Skip initial '/' or '\'. */
        if (*param_block == '/' || *param_block == '\\')
          param_block++;
        sets[i].filepath = wv_malloc(walker - param_block);
        if (sets[i].filepath == NULL) {
            DBG("Could not store filename\n");
            /* Skip drive num. */
            walker++;
            param_block = walker;
            continue;
          } /* if */
        RtlCopyMemory(sets[i].filepath, param_block, walker - param_block);
        /* Replace '/' with '\'. */
        {   char * rep = sets[i].filepath;

            while (*rep) {
                if (*rep == '/')
                  *rep = '\\';
                rep++;
              }
          } /* rep scope. */
        /* The next byte is expected to be the INT 13h drive num. */
        sets[i].int13_drive_num = *walker;
        walker++;
        i++;
        param_block = walker;
      } /* while */
  }

/** Create a GRUB4DOS sector-mapped disk and add it to the WinVBlock bus. */
VOID WvFilediskCreateG4dDisk(
    SP_WV_G4D_DRIVE_MAPPING slot,
    WVL_E_DISK_MEDIA_TYPE media_type,
    UINT32 sector_size
  ) {
    WV_SP_FILEDISK_T filedisk_ptr;
    #ifdef TODO_RESTORE_FILE_MAPPED_G4D_DISKS
    /* Matches disks to files. */
    WV_S_FILEDISK_GRUB4DOS_DRIVE_FILE_SET sets[8] = {0};
    #endif

    /*
     * Create the threaded, file-backed disk.  Hook the
     * read/write routine so we can access the backing disk
     * late(r) during the boot process.
     */
    filedisk_ptr = WvFilediskCreatePdo(media_type);
    if (!filedisk_ptr) {
        DBG("Could not create GRUB4DOS sector-mapped disk!\n");
        return;
      }
    DBG("Sector-mapped disk is type: %d\n", media_type);

    /* Other parameters we know. */
    filedisk_ptr->disk->Media = media_type;
    filedisk_ptr->disk->SectorSize = sector_size;

    #ifdef TODO_RESTORE_FILE_MAPPED_G4D_DISKS
    /* Find an associated filename, if one exists. */
    {   int j = 8;

        while (j--) {
            if (
                (sets[j].int13_drive_num == slot->SourceDrive) &&
                (sets[j].filepath != NULL)
              )
              WvFilediskHotSwap(filedisk_ptr, sets[j].filepath);
          }
      } /* j scope. */
    #endif

    /* Note the offset of the disk image from the backing disk's start. */
    filedisk_ptr->offset.QuadPart =
      slot->SectorStart * (slot->DestODD ? 2048 : 512);
    /*
     * Size and geometry.  Please note that since we require a .VHD
     * footer, we exclude this from the LBA disk size by truncating
     * a 512-byte sector for HDD images, or a 2048-byte sector for .ISO.
     */
    {   ULONGLONG total_size =
          slot->SectorCount * (slot->DestODD ? 2048 : 512);

        filedisk_ptr->disk->LBADiskSize =
          (total_size - filedisk_ptr->disk->SectorSize) /
          filedisk_ptr->disk->SectorSize;
      } /* total_size scope. */
    filedisk_ptr->disk->Heads = slot->MaxHead + 1;
    filedisk_ptr->disk->Sectors = slot->DestMaxSector;
    filedisk_ptr->disk->Cylinders = (
        filedisk_ptr->disk->LBADiskSize /
        (filedisk_ptr->disk->Heads * filedisk_ptr->disk->Sectors)
      );
    /*
     * Set a filedisk "hash" and mark the drive as a boot-drive.
     * The "hash" is 'G4DX', where X is the GRUB4DOS INT 13h
     * drive number.  Note that mutiple G4D INT 13h chains will
     * cause a "hash" collision!  Too bad for now.
     */
    filedisk_ptr->hash = 'G4DX';
    ((PUCHAR) &filedisk_ptr->hash)[0] = slot->SourceDrive;
    filedisk_ptr->Dev->Boot = TRUE;
    /* Add the filedisk to the bus. */
    filedisk_ptr->disk->ParentBus = WvBus.Fdo;
    if (!WvFilediskG4dFindBackingDisk(filedisk_ptr))
      WvDevFree(filedisk_ptr->Dev);

    return;
  }
