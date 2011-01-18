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
#include "irp.h"
#include "driver.h"
#include "bus.h"
#include "device.h"
#include "disk.h"
#include "mount.h"
#include "libthread.h"
#include "filedisk.h"
#include "debug.h"

/* From bus.c */
extern WVL_S_BUS_T WvBus;

/** Private function declarations. */
static WVL_F_DISK_IO WvFilediskIo_;
static WV_F_DEV_CLOSE WvFilediskClose_;
static WVL_F_DISK_UNIT_NUM WvFilediskUnitNum_;
static WVL_F_THREAD_ITEM WvFilediskThread_;
static WV_F_DEV_FREE WvFilediskFree_;

/** Private function definitions. */
static NTSTATUS STDCALL WvFilediskIo_(
    IN WVL_SP_DISK_T disk_ptr,
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

    if (sector_count < 1) {
        /* A silly request. */
        DBG("sector_count < 1; cancelling\n");
        return WvlIrpComplete(irp, 0, STATUS_CANCELLED);
      }

    /* Establish pointer to the filedisk. */
    filedisk_ptr = CONTAINING_RECORD(disk_ptr, WV_S_FILEDISK_T, disk);

    /*
     * These SCSI read/write IRPs should be completed in the thread context.
     * Check if the IRP was already marked pending.
     */
    if (!(IoGetCurrentIrpStackLocation(irp)->Control & SL_PENDING_RETURNED)) {
        /* Enqueue and signal work. */
        IoMarkIrpPending(irp);
        ExInterlockedInsertTailList(
            filedisk_ptr->Irps,
            &irp->Tail.Overlay.ListEntry,
            filedisk_ptr->IrpsLock
          );
        KeSetEvent(&filedisk_ptr->Thread->Signal, 0, FALSE);
        return STATUS_PENDING;
      }

    /* Calculate the offset. */
    offset.QuadPart = start_sector * disk_ptr->SectorSize;
    offset.QuadPart += filedisk_ptr->offset.QuadPart;

    /* Perform the read/write. */
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
        /* When the MBR is read, re-determine the disk geometry. */
        if (!start_sector)
          WvlDiskGuessGeometry((WVL_AP_DISK_BOOT_SECT) buffer, disk_ptr);
      }
    return WvlIrpComplete(irp, sector_count * disk_ptr->SectorSize, status);
  }

static NTSTATUS STDCALL WvFilediskPnpQueryId_(
    IN PDEVICE_OBJECT dev_obj,
    IN PIRP irp,
    IN WVL_SP_DISK_T disk
  ) {
    static const WCHAR * hw_ids[WvlDiskMediaTypes] = {
        WVL_M_WLIT L"\\FileFloppyDisk",
        WVL_M_WLIT L"\\FileHardDisk",
        WVL_M_WLIT L"\\FileOpticalDisc"
      };
    WCHAR (*buf)[512];
    NTSTATUS status;
    WV_SP_FILEDISK_T filedisk = CONTAINING_RECORD(disk, WV_S_FILEDISK_T, disk);
    BUS_QUERY_ID_TYPE query_type;

    /* Allocate a buffer. */
    buf = wv_mallocz(sizeof *buf);
    if (!buf) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto err_buf;
      }

    /* Populate the buffer with IDs. */
    query_type = IoGetCurrentIrpStackLocation(irp)->Parameters.QueryId.IdType;
    switch (query_type) {
        case BusQueryDeviceID:
          swprintf(*buf, hw_ids[disk->Media]);
          break;

        case BusQueryInstanceID:
          /* "Location". */
          swprintf(*buf, L"Hash_%08X", filedisk->hash);
          break;

        case BusQueryHardwareIDs:
          swprintf(
              *buf + swprintf(*buf, hw_ids[disk->Media]) + 1,
              WvlDiskCompatIds[disk->Media]
            );
          break;

        case BusQueryCompatibleIDs:
          swprintf(*buf, WvlDiskCompatIds[disk->Media]);

        default:
          DBG("Unknown query type %d for %p!\n", query_type, filedisk);
          status = STATUS_INVALID_PARAMETER;
          goto err_query_type;
      }

    DBG("IRP_MN_QUERY_ID for file-backed disk %p.\n", filedisk);
    return WvlIrpComplete(irp, (ULONG_PTR) buf, STATUS_SUCCESS);

    err_query_type:

    wv_free(buf);
    err_buf:

    return WvlIrpComplete(irp, 0, status);
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
    filedisk->disk->ParentBus = WvBus.Fdo;
    if (!WvBusAddDev(filedisk->Dev)) {
        status = STATUS_UNSUCCESSFUL;
        goto err_add_child;
      }

    return STATUS_SUCCESS;

    err_add_child:

    WvFilediskFree_(filedisk->Dev);
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
        Dev
      );

    ZwClose(filedisk->file);
    return;
  }

static UCHAR STDCALL WvFilediskUnitNum_(IN WVL_SP_DISK_T disk) {
    WV_SP_FILEDISK_T filedisk = CONTAINING_RECORD(
        disk,
        WV_S_FILEDISK_T,
        disk[0]
      );

    /* Possible precision loss. */
    return (UCHAR) WvlBusGetNodeNum(&filedisk->Dev->BusNode);
  }

static VOID STDCALL WvFilediskThread_(IN OUT WVL_SP_THREAD_ITEM item) {
    WV_SP_FILEDISK_T filedisk = CONTAINING_RECORD(
        item,
        WV_S_FILEDISK_T,
        Thread[0].Main
      );
    LARGE_INTEGER timeout;
    WVL_SP_THREAD_ITEM work_item;
    PLIST_ENTRY irp_item;

    /* Wake up at least every 30 seconds. */
    timeout.QuadPart = -300000000LL;

    while (
        (filedisk->Thread->State == WvlThreadStateStarted) ||
        (filedisk->Thread->State == WvlThreadStateStopping)
      ) {
        /* Wait for the work signal or the timeout. */
        KeWaitForSingleObject(
            &filedisk->Thread->Signal,
            Executive,
            KernelMode,
            FALSE,
            &timeout
          );
        /* Reset the work signal. */
        KeResetEvent(&filedisk->Thread->Signal);

        /* Process work items.  One of these might be a stopper. */
        while (work_item = WvlThreadGetItem(filedisk->Thread))
          work_item->Func(work_item);

        /* Process SCSI IRPs. */
        while (irp_item = ExInterlockedRemoveHeadList(
            filedisk->Irps,
            filedisk->IrpsLock
          )) {
            PIRP irp;
            PIO_STACK_LOCATION io_stack_loc;

            irp = CONTAINING_RECORD(irp_item, IRP, Tail.Overlay.ListEntry);
            io_stack_loc = IoGetCurrentIrpStackLocation(irp);
            if (io_stack_loc->MajorFunction != IRP_MJ_SCSI) {
                DBG("Non-SCSI IRP!\n");
                continue;
              }
            WvlDiskScsi(filedisk->Dev->Self, irp, filedisk->disk);
          }

        /* Are we finished? */
        if (filedisk->Thread->State == WvlThreadStateStopping)
          filedisk->Thread->State = WvlThreadStateStopped;
      } /* while thread started or stopping. */
    return;
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
        WvDiskPower,
        WvDiskSysCtl,
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
        goto err_pdo;
      }

    /* Initialize. */
    filedisk = pdo->DeviceExtension;
    RtlZeroMemory(filedisk, sizeof *filedisk);
    WvlDiskInit(filedisk->disk);
    WvDevInit(filedisk->Dev);
    filedisk->Dev->Ops.Free = WvFilediskFree_;
    filedisk->Dev->Ops.Close = WvFilediskClose_;
    filedisk->Dev->ext = filedisk->disk;
    filedisk->Dev->IrpMj = &irp_mj;
    filedisk->disk->disk_ops.Io = WvFilediskIo_;
    filedisk->disk->disk_ops.UnitNum = WvFilediskUnitNum_;
    filedisk->disk->disk_ops.PnpQueryId = WvFilediskPnpQueryId_;
    filedisk->disk->disk_ops.PnpQueryDevText = WvDiskPnpQueryDevText;
    filedisk->disk->ext = filedisk;
    filedisk->disk->DriverObj = WvDriverObj;
    InitializeListHead(filedisk->Irps);
    KeInitializeSpinLock(filedisk->IrpsLock);

    /* Start the thread. */
    filedisk->Thread->Main.Func = WvFilediskThread_;
    status = WvlThreadStart(filedisk->Thread);
    if (!NT_SUCCESS(status)) {
        DBG("Couldn't create thread!\n");
        goto err_thread;
      }

    /* Set associations for the PDO, device, disk. */
    WvDevForDevObj(pdo, filedisk->Dev);
    filedisk->Dev->Self = pdo;

    /* Some device parameters. */
    pdo->Flags |= DO_DIRECT_IO;         /* FIXME? */
    pdo->Flags |= DO_POWER_INRUSH;      /* FIXME? */

    DBG("New PDO: %p\n", pdo);
    return filedisk;

    WvlThreadSendStopAndWait(filedisk->Thread);
    err_thread:

    IoDeleteDevice(pdo);
    err_pdo:

    return NULL;
  }

/**
 * Default file-backed disk deletion operation.
 *
 * @v dev               Points to the file-backed disk device to delete.
 */
static VOID STDCALL WvFilediskFree_(IN WV_SP_DEV_T dev) {
    WV_SP_FILEDISK_T filedisk = CONTAINING_RECORD(
        dev,
        WV_S_FILEDISK_T,
        Dev[0]
      );

    WvlThreadSendStopAndWait(filedisk->Thread);
    IoDeleteDevice(dev->Self);
  }

/**
 * Find and hot-swap to a backing file.  This is a thread work item.
 *
 * We search all filesystems for a particular filename, then swap
 * to using it as the backing store.  This is currently useful for
 * sector-mapped disks which should really have a file-in-use lock
 * for the file they represent.  Once the backing file is established,
 * we free the work item.  Otherwise, we re-enqueue the thread item
 * and the thread will keep trying.
 */
static BOOLEAN STDCALL WvFilediskHotSwap_(
    IN WV_SP_FILEDISK_T filedisk,
    IN PUNICODE_STRING filename
  ) {
    NTSTATUS status;
    GUID vol_guid = GUID_DEVINTERFACE_VOLUME;
    PWSTR sym_links;
    PWCHAR pos;
    KIRQL irql;

    /* Do we currently have a backing disk?  If not, re-enqueue for later. */
    if (filedisk->file == NULL)
      return FALSE;
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
          filename->Length;
        filepath.Buffer = wv_malloc(filepath.Length);
        if (filepath.Buffer == NULL) {
            status = STATUS_UNSUCCESSFUL;
            goto err_alloc_buf;
          }
        {   PCHAR buf = (PCHAR) filepath.Buffer;

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
            RtlCopyMemory(buf, filename->Buffer, filename->Length);
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
        {   HANDLE old = filedisk->file;
  
            filedisk->file = 0;
            filedisk->offset.QuadPart = 0;
            filedisk->file = file;
            ZwClose(old);
          } /* old scope */
  
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
    /* Success? */
    if (NT_SUCCESS(status)) {
        DBG("Finished hot-swapping.\n");
        return TRUE;
      }
    return FALSE;
  }

typedef struct WV_FILEDISK_HOT_SWAPPER_ {
    WVL_S_THREAD_ITEM item[1];
    WV_SP_FILEDISK_T filedisk;
    UNICODE_STRING filename[1];
  } WV_S_FILEDISK_HOT_SWAPPER_, * WV_SP_FILEDISK_HOT_SWAPPER_;

static VOID STDCALL WvFilediskHotSwapThread_(IN OUT WVL_SP_THREAD_ITEM item) {
    LARGE_INTEGER timeout;
    WVL_SP_THREAD thread = CONTAINING_RECORD(item, WVL_S_THREAD, Main);
    WVL_SP_THREAD_ITEM work_item;
    WV_SP_FILEDISK_HOT_SWAPPER_ info;

    /* Wake up at least every 10 seconds. */
    timeout.QuadPart = -100000000LL;

    while (
        (thread->State == WvlThreadStateStarted) ||
        (thread->State == WvlThreadStateStopping)
      ) {
        /* Wait for the work signal or the timeout. */
        KeWaitForSingleObject(
            &thread->Signal,
            Executive,
            KernelMode,
            FALSE,
            &timeout
          );

        work_item = WvlThreadGetItem(thread);
        if (!work_item)
          continue;

        if (work_item->Func != WvFilediskHotSwapThread_) {
            DBG("Unknown work item.\n");
            continue;
          }
        info = CONTAINING_RECORD(
            work_item,
            WV_S_FILEDISK_HOT_SWAPPER_,
            item[0]
          );
        /* Attempt a hot swap. */
        if (WvFilediskHotSwap_(info->filedisk, info->filename)) {
            /* Success. */
            RtlFreeUnicodeString(info->filename);
            wv_free(info);
          } else {
            /* Re-enqueue. */
            WvlThreadAddItem(thread, info->item);
          }

        /* Reset the work signal. */
        KeResetEvent(&thread->Signal);
      } /* while thread is running. */
    return;
  }

VOID STDCALL WvFilediskHotSwap(IN WV_SP_FILEDISK_T filedisk, IN PCHAR file) {
    static WVL_S_THREAD thread = {
        /* Main */
        {
            /* Link */
            {0},
            /* Func */
            WvFilediskHotSwapThread_,
          },
        /* State */
        WvlThreadStateNotStarted,
      };
    WV_SP_FILEDISK_HOT_SWAPPER_ hot_swapper;
    ANSI_STRING file_ansi;
    NTSTATUS status;

    hot_swapper = wv_malloc(sizeof *hot_swapper);
    if (!hot_swapper) {
        DBG("Non-critical: Couldn't allocate work item.\n");
        return;
      }
    /* Build the Unicode string. */
    RtlInitAnsiString(&file_ansi, file);
    hot_swapper->filename->Buffer = NULL;
    status = RtlAnsiStringToUnicodeString(
        hot_swapper->filename,
        &file_ansi,
        TRUE
      );
    if (!NT_SUCCESS(status)) {
        DBG("Non-critical: Couldn't allocate unicode string.\n");
        wv_free(hot_swapper);
        return;
      }
    /* Build the rest of the work item. */
    hot_swapper->item->Func = WvFilediskHotSwapThread_;
    hot_swapper->filedisk = filedisk;
    /* Start the thread.  If it's already been started, no matter. */
    WvlThreadStart(&thread);
    /* Add the hot-swapper work item. */
    if (!WvlThreadAddItem(&thread, hot_swapper->item)) {
        DBG("Non-critical: Couldn't add work item.\n");
        RtlFreeUnicodeString(hot_swapper->filename);
        wv_free(hot_swapper);
      }
    /* The thread is responsible for freeing the work item and file path. */
    return;
  }
