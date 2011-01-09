/**
 * Copyright (C) 2009-2011, Shao Miller <shao.miller@yrdsb.edu.on.ca>.
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

/**
 * @file
 *
 * File-backed disk specifics.
 */

#include <stdio.h>
#include <ntddk.h>
#include <initguid.h>
#include <ntddstor.h>

#include "portable.h"
#include "winvblock.h"
#include "wv_stdlib.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "mount.h"
#include "filedisk.h"
#include "debug.h"

/** Private. */
static WV_F_DEV_FREE WvFilediskFree_;
static WVL_F_DISK_IO WvFilediskIo_;
static WVL_F_DISK_IO WvFilediskThreadedIo_;
static WV_F_DEV_CLOSE WvFilediskClose_;

static NTSTATUS STDCALL WvFilediskIo_(
    IN WV_SP_DISK_T disk_ptr,
    IN WVL_E_DISK_IO_MODE mode,
    IN LONGLONG start_sector,
    IN UINT32 sector_count,
    IN PUCHAR buffer,
    IN PIRP irp
  ) {
    WV_SP_FILEDISK_T filedisk_ptr;
    LARGE_INTEGER offset;
    NTSTATUS status;
    IO_STATUS_BLOCK io_status;

    /* Establish pointer to the filedisk. */
    filedisk_ptr = CONTAINING_RECORD(disk_ptr, WV_S_FILEDISK_T, disk);

    if (sector_count < 1) {
        /* A silly request. */
        DBG("sector_count < 1; cancelling\n");
        irp->IoStatus.Information = 0;
        irp->IoStatus.Status = STATUS_CANCELLED;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return STATUS_CANCELLED;
      }
    /* Calculate the offset. */
    offset.QuadPart = start_sector * disk_ptr->SectorSize;
    offset.QuadPart += filedisk_ptr->offset.QuadPart;

    if (mode == WvlDiskIoModeWrite) {
        status = ZwWriteFile(
            filedisk_ptr->file,
            NULL,
            NULL,
            NULL,
            &io_status,
            buffer,
            sector_count * disk_ptr->SectorSize,
            &offset,
            NULL
          );
      } else {
        status = ZwReadFile(
            filedisk_ptr->file,
            NULL,
            NULL,
            NULL,
            &io_status,
            buffer,
            sector_count * disk_ptr->SectorSize,
            &offset,
            NULL
          );
      }
    if (!start_sector)
      disk__guess_geometry((WVL_AP_DISK_BOOT_SECT) buffer, disk_ptr);
    irp->IoStatus.Information = sector_count * disk_ptr->SectorSize;
    irp->IoStatus.Status = status;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
  }

static UINT32 STDCALL query_id(
    IN WV_SP_DEV_T dev,
    IN BUS_QUERY_ID_TYPE query_type,
    IN OUT WCHAR (*buf)[512]
  ) {
    WV_SP_DISK_T disk = disk__get_ptr(dev);
    WV_SP_FILEDISK_T filedisk = filedisk__get_ptr(dev);
    static PWCHAR hw_ids[WvlDiskMediaTypes] = {
        WVL_M_WLIT L"\\FileFloppyDisk",
        WVL_M_WLIT L"\\FileHardDisk",
        WVL_M_WLIT L"\\FileOpticalDisc"
      };

    switch (query_type) {
        case BusQueryDeviceID:
          return swprintf(*buf, hw_ids[disk->Media]) + 1;

        case BusQueryInstanceID:
          return swprintf(*buf, L"Hash_%08X", filedisk->hash) + 1;

        case BusQueryHardwareIDs: {
            UINT32 tmp;

            tmp = swprintf(*buf, hw_ids[disk->Media]) + 1;
            tmp += swprintf(*buf + tmp, WvDiskCompatIds[disk->Media]) + 4;
            return tmp;
          }

        case BusQueryCompatibleIDs:
          return swprintf(*buf, WvDiskCompatIds[disk->Media]) + 4;

        default:
          return 0;
      }
  }

/* Attach a file as a disk, based on an IRP. */
NTSTATUS STDCALL WvFilediskAttach(IN PIRP irp) {
    ANSI_STRING ansi_path;
    PCHAR buf = irp->AssociatedIrp.SystemBuffer;
    UNICODE_STRING file_path;
    NTSTATUS status;
    OBJECT_ATTRIBUTES obj_attrs;
    HANDLE file = NULL;
    IO_STATUS_BLOCK io_status;
    WV_SP_MOUNT_DISK params = (WV_SP_MOUNT_DISK) buf;
    FILE_STANDARD_INFORMATION info;
    WV_SP_FILEDISK_T filedisk;
    WVL_E_DISK_MEDIA_TYPE media_type;
    UINT32 sector_size, hash;
    ULONGLONG lba_size;

    RtlInitAnsiString(&ansi_path, buf + sizeof (WV_S_MOUNT_DISK));
    status = RtlAnsiStringToUnicodeString(&file_path, &ansi_path, TRUE);
    if (!NT_SUCCESS(status))
      goto err_ansi_to_unicode;
    InitializeObjectAttributes(
        &obj_attrs,
        &file_path,
        OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
        NULL,
        NULL
      );
    /* Open the file.  The handle is closed by WvFilediskClose_() */
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
    RtlFreeUnicodeString(&file_path);
    if (!NT_SUCCESS(status))
      goto err_file_open;

    switch (params->type) {
        case 'f':
          media_type = WvlDiskMediaTypeFloppy;
          sector_size = 512;
          break;

        case 'c':
          media_type = WvlDiskMediaTypeOptical;
          sector_size = 2048;
          break;

        default:
          media_type = WvlDiskMediaTypeHard;
          sector_size = 512;
          break;
      }
    DBG("Media type: %d\n", media_type);

    /* Determine the disk's size. */
    status = ZwQueryInformationFile(
        file,
        &io_status,
        &info,
        sizeof info,
        FileStandardInformation
      );
    if (!NT_SUCCESS(status))
      goto err_query_info;
    lba_size = info.EndOfFile.QuadPart / sector_size;

    /*
     * A really stupid "hash".  RtlHashUnicodeString() would have been
     * good, but is only available >= Windows XP.
     */
    hash = (UINT32) lba_size;
    {   PCHAR path_iterator = ansi_path.Buffer;
    
        while (*path_iterator)
          hash += *path_iterator++;
      }

    /* Create the filedisk PDO. */
    filedisk = WvFilediskCreatePdo(media_type);
    if (filedisk == NULL) {
        DBG("Could not create file-backed disk!\n");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_pdo;
      }

    /* Set filedisk parameters. */
    filedisk->disk->Media = media_type;
    filedisk->disk->SectorSize = sector_size;
    filedisk->disk->LBADiskSize = lba_size;
    filedisk->disk->Cylinders = params->cylinders;
    filedisk->disk->Heads = params->heads;
    filedisk->disk->Sectors = params->sectors;
    filedisk->file = file;
    filedisk->hash = hash;

    /* Add the filedisk to the bus. */
    if (!WvBusAddDev(filedisk->disk->Dev)) {
        status = STATUS_UNSUCCESSFUL;
        goto err_add_child;
      }

    return STATUS_SUCCESS;

    err_add_child:

    WvFilediskFree_(filedisk->disk->Dev);
    err_pdo:

    err_query_info:

    ZwClose(file);
    err_file_open:

    err_ansi_to_unicode:

    return status;
  }

static VOID STDCALL WvFilediskClose_(IN WV_SP_DEV_T dev) {
    WV_SP_FILEDISK_T filedisk = CONTAINING_RECORD(
        dev,
        WV_S_FILEDISK_T,
        disk[0].Dev
      );

    ZwClose(filedisk->file);
    return;
  }

static WVL_F_DISK_UNIT_NUM WvFilediskUnitNum_;
static UCHAR STDCALL WvFilediskUnitNum_(IN WV_SP_DISK_T disk) {
    WV_SP_FILEDISK_T filedisk = CONTAINING_RECORD(
        disk,
        WV_S_FILEDISK_T,
        disk[0]
      );

    /* Possible precision loss. */
    return (UCHAR) WvlBusGetNodeNum(&filedisk->disk->Dev->BusNode);
  }

/**
 * Create a filedisk PDO filled with the given disk parameters.
 *
 * @v MediaType                 The media type for the filedisk.
 * @ret WV_SP_FILEDISK_T        Points to the new filedisk, or NULL.
 *
 * Returns NULL if the PDO cannot be created.
 */
WV_SP_FILEDISK_T STDCALL WvFilediskCreatePdo(
    IN WVL_E_DISK_MEDIA_TYPE MediaType
  ) {
    static WV_S_DEV_IRP_MJ irp_mj = {
        WvDiskIrpPower,
        WvDiskIrpSysCtl,
        WvDiskDevCtl,
        WvDiskScsi,
        WvDiskPnp,
      };
    NTSTATUS status;
    WV_SP_FILEDISK_T filedisk;
    PDEVICE_OBJECT pdo;
  
    DBG("Creating filedisk PDO...\n");

    /* Create the disk PDO. */
    status = WvlDiskCreatePdo(
        WvDriverObj,
        sizeof *filedisk,
        MediaType,
        &pdo
      );
    if (!NT_SUCCESS(status)) {
        WvlError("WvlDiskCreatePdo", status);
        return NULL;
      }

    filedisk = pdo->DeviceExtension;
    RtlZeroMemory(filedisk, sizeof *filedisk);
    WvDiskInit(filedisk->disk);
    WvDevInit(filedisk->disk->Dev);
    filedisk->disk->Dev->Ops.Free = WvFilediskFree_;
    filedisk->disk->Dev->Ops.PnpId = query_id;
    filedisk->disk->Dev->Ops.Close = WvFilediskClose_;
    filedisk->disk->Dev->ext = filedisk->disk;
    filedisk->disk->Dev->IrpMj = &irp_mj;
    filedisk->disk->disk_ops.Io = WvFilediskIo_;
    filedisk->disk->disk_ops.UnitNum = WvFilediskUnitNum_;
    filedisk->disk->ext = filedisk;
    filedisk->disk->DriverObj = WvDriverObj;

    /* Set associations for the PDO, device, disk. */
    WvDevForDevObj(pdo, filedisk->disk->Dev);
    KeInitializeEvent(
        &filedisk->disk->SearchEvent,
        SynchronizationEvent,
        FALSE
      );
    KeInitializeSpinLock(&filedisk->disk->SpinLock);
    filedisk->disk->Dev->Self = pdo;

    /* Some device parameters. */
    pdo->Flags |= DO_DIRECT_IO;         /* FIXME? */
    pdo->Flags |= DO_POWER_INRUSH;      /* FIXME? */

    DBG("New PDO: %p\n", pdo);
    return filedisk;
  }

/**
 * Default file-backed disk deletion operation.
 *
 * @v dev               Points to the file-backed disk device to delete.
 */
static VOID STDCALL WvFilediskFree_(IN WV_SP_DEV_T dev) {
    IoDeleteDevice(dev->Self);
  }

/* Threaded read/write request. */
typedef struct WV_FILEDISK_THREAD_REQ {
    LIST_ENTRY list_entry;
    WV_SP_FILEDISK_T filedisk;
    WVL_E_DISK_IO_MODE mode;
    LONGLONG start_sector;
    UINT32 sector_count;
    PUCHAR buffer;
    PIRP irp;
  } WV_S_FILEDISK_THREAD_REQ, * WV_SP_FILEDISK_THREAD_REQ;

/**
 * A threaded, file-backed disk's worker thread.
 *
 * @v StartContext      Points to a file-backed disk.
 */
static VOID STDCALL thread(IN PVOID StartContext) {
    WV_SP_FILEDISK_T filedisk_ptr = StartContext;
    LARGE_INTEGER timeout;
    PLIST_ENTRY walker;

    /* Wake up at least every second. */
    timeout.QuadPart = -10000000LL;
    /* The read/write request processing loop. */
    while (TRUE) {
        /* Wait for work-to-do signal or the timeout. */
        KeWaitForSingleObject(
            &filedisk_ptr->signal,
            Executive,
            KernelMode,
            FALSE,
            &timeout
          );
        KeResetEvent(&filedisk_ptr->signal);
        /* Are we being torn down?  We abuse the device's Free() member. */
        if (filedisk_ptr->disk->Dev->Ops.Free == NULL)
          break;
        /* Process each read/write request in the list. */
        while (walker = ExInterlockedRemoveHeadList(
            &filedisk_ptr->req_list,
            &filedisk_ptr->req_list_lock
          )) {
            WV_SP_FILEDISK_THREAD_REQ req;
  
            req = CONTAINING_RECORD(
                walker,
                WV_S_FILEDISK_THREAD_REQ,
                list_entry
              );
            filedisk_ptr->sync_io(
                req->filedisk->disk,
                req->mode,
                req->start_sector,
                req->sector_count,
                req->buffer,
                req->irp
              );
            wv_free(req);
          } /* while requests */
      } /* main loop */
    /* Time to tear things down. */
    WvFilediskFree_(filedisk_ptr->disk->Dev);
  }

static NTSTATUS STDCALL WvFilediskThreadedIo_(
    IN WV_SP_DISK_T disk,
    IN WVL_E_DISK_IO_MODE mode,
    IN LONGLONG start_sector,
    IN UINT32 sector_count,
    IN PUCHAR buffer,
    IN PIRP irp
  ) {
    WV_SP_FILEDISK_T filedisk_ptr;
    WV_SP_FILEDISK_THREAD_REQ req;

    filedisk_ptr = CONTAINING_RECORD(disk, WV_S_FILEDISK_T, disk);
    /* Allocate the request. */
    req = wv_malloc(sizeof *req);
    if (req == NULL) {
        irp->IoStatus.Information = 0;
        irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest(irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
      }
    /* Remember the request. */
    req->filedisk = filedisk_ptr;
    req->mode = mode;
    req->start_sector = start_sector;
    req->sector_count = sector_count;
    req->buffer = buffer;
    req->irp = irp;
    ExInterlockedInsertTailList(
        &filedisk_ptr->req_list,
        &req->list_entry,
        &filedisk_ptr->req_list_lock
      );
    /* Signal worker thread and return. */
    KeSetEvent(&filedisk_ptr->signal, 0, FALSE);
    return STATUS_PENDING;
  }

/**
 * Threaded, file-backed disk deletion operation.
 *
 * @v dev_ptr           Points to the file-backed disk device to delete.
 */
static VOID STDCALL free_threaded_filedisk(IN WV_SP_DEV_T dev_ptr) {
    /*
     * Queue the tear-down and return.  The thread will catch this on timeout.
     */
    dev_ptr->Ops.Free = NULL;
  }

/**
 * Create a new threaded, file-backed disk.
 *
 * @v MediaType         The media type for the filedisk.
 * @ret filedisk_ptr    The address of a new filedisk, or NULL for failure.
 *
 * See WvFilediskCreatePdo() above.  This routine uses threaded routines
 * for disk reads/writes, and frees asynchronously, too.
 */
WV_SP_FILEDISK_T WvFilediskCreatePdoThreaded(
    IN WVL_E_DISK_MEDIA_TYPE MediaType
  ) {
    WV_SP_FILEDISK_T filedisk_ptr;
    OBJECT_ATTRIBUTES obj_attrs;
    HANDLE thread_handle;

    /* Try to create a filedisk. */
    filedisk_ptr = WvFilediskCreatePdo(MediaType);
    if (filedisk_ptr == NULL)
      goto err_nofiledisk;
    /* Use threaded routines. */
    filedisk_ptr->sync_io = WvFilediskIo_;
    filedisk_ptr->disk->disk_ops.Io = WvFilediskThreadedIo_;
    filedisk_ptr->disk->Dev->Ops.Free = free_threaded_filedisk;
    /* Initialize threading parameters and start the filedisk's thread. */
    InitializeListHead(&filedisk_ptr->req_list);
    KeInitializeSpinLock(&filedisk_ptr->req_list_lock);
    KeInitializeEvent(&filedisk_ptr->signal, SynchronizationEvent, FALSE);
    InitializeObjectAttributes(
        &obj_attrs,
        NULL,
        OBJ_KERNEL_HANDLE,
        NULL,
        NULL
      );
    PsCreateSystemThread(
        &thread_handle,
        THREAD_ALL_ACCESS,
        &obj_attrs,
        NULL,
        NULL,
        thread,
        filedisk_ptr
      );

    return filedisk_ptr;

    err_nofiledisk:

    return NULL;
  }

/**
 * Find and hot-swap to a backing file.
 *
 * @v filedisk_ptr      Points to the filedisk needing to hot-swap.
 * @ret                 TRUE once the new file has been established, or FALSE
 *
 * We search all filesystems for a particular filename, then swap
 * to using it as the backing store.  This is currently useful for
 * sector-mapped disks which should really have a file-in-use lock
 * for the file they represent.  Once the backing file is established,
 * we return TRUE.  Otherwise, we return FALSE and the hot-swap thread
 * will keep trying.
 */
static BOOLEAN STDCALL hot_swap(WV_SP_FILEDISK_T filedisk_ptr) {
    NTSTATUS status;
    GUID vol_guid = GUID_DEVINTERFACE_VOLUME;
    PWSTR sym_links;
    PWCHAR pos;

    /*
     * Find the backing volume and use it.  We walk a list
     * of unicode volume device names and check each one for the file.
     */
    status = IoGetDeviceInterfaces(&vol_guid, NULL, 0, &sym_links);
    if (!NT_SUCCESS(status))
      return FALSE;
    pos = sym_links;
    status = STATUS_UNSUCCESSFUL;
    while (*pos != UNICODE_NULL) {
        UNICODE_STRING path;
        PFILE_OBJECT vol_file_obj;
        PDEVICE_OBJECT vol_dev_obj;
        UNICODE_STRING vol_dos_name;
        UNICODE_STRING filepath;
        static const WCHAR obj_path_prefix[] = L"\\??\\";
        static const WCHAR path_sep = L'\\';
        OBJECT_ATTRIBUTES obj_attrs;
        HANDLE file = 0;
        IO_STATUS_BLOCK io_status;

        RtlInitUnicodeString(&path, pos);
        /* Get some object pointers for the volume. */
        status = IoGetDeviceObjectPointer(
            &path,
            FILE_READ_DATA,
            &vol_file_obj,
            &vol_dev_obj
          );
        if (!NT_SUCCESS(status))
          goto err_obj_ptrs;
        /* Get the DOS name. */
        vol_dos_name.Buffer = NULL;
        vol_dos_name.Length = vol_dos_name.MaximumLength = 0;
        status = RtlVolumeDeviceToDosName(
            vol_file_obj->DeviceObject,
            &vol_dos_name
          );
        if (!NT_SUCCESS(status))
          goto err_dos_name;
        /* Build the file path.  Ugh, what a mess. */
        filepath.Length = filepath.MaximumLength =
          sizeof obj_path_prefix -
          sizeof UNICODE_NULL +
          vol_dos_name.Length +
          sizeof path_sep +
          filedisk_ptr->filepath_unicode.Length;
        filepath.Buffer = wv_malloc(filepath.Length);
        if (filepath.Buffer == NULL) {
            status = STATUS_UNSUCCESSFUL;
            goto err_alloc_buf;
          }
        {   char *buf = (char *) filepath.Buffer;

            RtlCopyMemory(
                buf,
                obj_path_prefix,
                sizeof obj_path_prefix - sizeof UNICODE_NULL
              );
            buf += sizeof obj_path_prefix - sizeof UNICODE_NULL;
            RtlCopyMemory(buf, vol_dos_name.Buffer, vol_dos_name.Length);
            buf += vol_dos_name.Length;
            RtlCopyMemory(buf, &path_sep, sizeof path_sep);
            buf += sizeof path_sep;
            RtlCopyMemory(
                buf,
                filedisk_ptr->filepath_unicode.Buffer,
                filedisk_ptr->filepath_unicode.Length
              );
          } /* buf scope */
        InitializeObjectAttributes(
            &obj_attrs,
            &filepath,
            OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
            NULL,
            NULL
          );
        /* Look for the file on this volume. */
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
        if (!NT_SUCCESS(status))
          goto err_open;
        /* We could open it.  Do the hot-swap. */
        {   HANDLE old = filedisk_ptr->file;
  
            filedisk_ptr->file = 0;
            filedisk_ptr->offset.QuadPart = 0;
            filedisk_ptr->file = file;
            ZwClose(old);
          } /* old scope */
        RtlFreeUnicodeString(&filedisk_ptr->filepath_unicode);
        filedisk_ptr->filepath_unicode.Length = 0;
  
        err_open:
  
        wv_free(filepath.Buffer);
        err_alloc_buf:
  
        wv_free(vol_dos_name.Buffer);
        err_dos_name:
  
        ObDereferenceObject(vol_file_obj);
        err_obj_ptrs:
        /* Walk to the next terminator. */
        while (*pos != UNICODE_NULL)
          pos++;
        /* If everything succeeded, stop.  Otherwise try the next volume. */
        if (!NT_SUCCESS(status))
          pos++;
      } /* while */
    wv_free(sym_links);
    return NT_SUCCESS(status) ? TRUE : FALSE;
  }

VOID STDCALL WvFilediskHotSwapThread(IN PVOID StartContext) {
    WV_SP_FILEDISK_T filedisk_ptr = StartContext;
    KEVENT signal;
    LARGE_INTEGER timeout;

    KeInitializeEvent(&signal, SynchronizationEvent, FALSE);
    /* Wake up at least every second. */
    timeout.QuadPart = -10000000LL;
    /* The hot-swap loop. */
    while (TRUE) {
        /* Wait for work-to-do signal or the timeout. */
        KeWaitForSingleObject(
            &signal,
            Executive,
            KernelMode,
            FALSE,
            &timeout
          );
        if (filedisk_ptr->file == NULL)
          continue;
        /* Are we supposed to hot-swap to a file?  Check ANSI filepath. */
        if (filedisk_ptr->filepath != NULL) {
            ANSI_STRING tmp;
            NTSTATUS status;

            RtlInitAnsiString(&tmp, filedisk_ptr->filepath);
            filedisk_ptr->filepath_unicode.Buffer = NULL;
            status = RtlAnsiStringToUnicodeString(
                &filedisk_ptr->filepath_unicode,
                &tmp,
                TRUE
              );
            if (NT_SUCCESS(status)) {
                wv_free(filedisk_ptr->filepath);
                filedisk_ptr->filepath = NULL;
              }
          }
        /* Are we supposed to hot-swap to a file?  Check unicode filepath. */
        if (filedisk_ptr->filepath_unicode.Length && hot_swap(filedisk_ptr))
          break;
      } /* while */
  }
