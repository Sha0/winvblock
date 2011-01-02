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

/**
 * @file
 *
 * WinVBlock user-mode utility.
 */

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <malloc.h>

#include "portable.h"
#include "winvblock.h"
#include "mount.h"
#include "aoe.h"

/* Command function. */
typedef int STDCALL (WVU_F_CMD_)(void);
typedef WVU_F_CMD_ * WVU_FP_CMD_;

typedef struct WVU_OPTION {
    char *name;
    char *value;
    int has_arg;
  } WVU_S_OPTION, * WVU_SP_OPTION;

/* Handle to the device. */
static HANDLE boot_bus = NULL;

static WVU_S_OPTION opt_h1 = {
    "HELP", NULL, 0
  };

static WVU_S_OPTION opt_h2 = {
    "?", NULL, 0
  };

static WVU_S_OPTION opt_cmd = {
    "CMD", NULL, 1
  };

static WVU_S_OPTION opt_cyls = {
    "C", NULL, 1
  };

static WVU_S_OPTION opt_heads = {
    "H", NULL, 1
  };

static WVU_S_OPTION opt_spt = {
    "S", NULL, 1
  };

static WVU_S_OPTION opt_disknum = {
    "D", NULL, 1
  };

static WVU_S_OPTION opt_media = {
    "M", NULL, 1
  };

static WVU_S_OPTION opt_uri = {
    "U", NULL, 1
  };

static WVU_S_OPTION opt_mac = {
    "MAC", NULL, 1
  };

static WVU_SP_OPTION options[] = {
    &opt_h1,
    &opt_h2,
    &opt_cmd,
    &opt_cyls,
    &opt_heads,
    &opt_spt,
    &opt_disknum,
    &opt_media,
    &opt_uri,
    &opt_mac,
  };

static char present[] = "";
static char *invalid_opt = NULL;

static int WvuShowLastErr(void) {
    DWORD err_code = GetLastError();
    char * buf = NULL;

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL,
        err_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &buf,
        0,
        NULL
      );
    if (buf) {
        printf("GetLastError():\n  %s", buf);
        LocalFree(buf);
      }
    return err_code;
  }

static VOID cmdline_options(int argc, char **argv) {
    int cur = 1;

    while (cur < argc) {
        char *cur_arg = argv[cur];
        int opt;

        /* Check if the argument is an option.  Look for '-', '--', or '/'. */
        switch (*cur_arg) {
            case '-':
              cur_arg++;
              if (*cur_arg == '-')
                cur_arg++;
              break;

            case '/':
              cur_arg++;
              break;

            default:
              invalid_opt = cur_arg;
              return;
          }
        /* Convert possible option to upper-case. */
        {   char *walker = cur_arg;

            while (*walker) {
                *walker = toupper(*walker);
                walker++;
              }
          }
        /* Check if the argument is a _valid_ option. */
        opt = 0;
        while (opt < sizeof options / sizeof *options) {
            if (strcmp(cur_arg, options[opt]->name) == 0) {
                /*
                 * We have a match.
                 * Check if the option was already specified.
                 */
                if (options[opt]->value != NULL) {
                    printf(
                        "%s specified more than once, making it invalid.\n",
                        cur_arg
                      );
                    invalid_opt = cur_arg;
                    return;
                  }
                /*
                 * The option's value is the next argument.  For boolean
                 * options, we ignore the value anyway, but need to know the
                 * option is present.
                 */
                if (cur == argc - 1)
                  options[opt]->value = present;
                  else
                  options[opt]->value = argv[cur + 1];
                cur += options[opt]->has_arg;
                break;
              }
            opt++;
          }
        /* Did we find no valid option match? */
        if (opt == sizeof options / sizeof *options) {
            invalid_opt = cur_arg;
            return;
          }
        cur++;
      }
  }

static int STDCALL cmd_help(void) {
    char help_text[] = "\n\
WinVBlock user-land utility for disk control. (C) 2006-2008 V.,\n\
                                              (C) 2009-2010 Shao Miller\n\
Usage:\n\
  winvblk -cmd <command> [-d <disk number>] [-m <media>] [-u <uri or path>]\n\
    [-mac <client mac address>] [-c <cyls>] [-h <heads>] [-s <sects per track>]\n\
  winvblk -?\n\
\n\
Parameters:\n\
  <command> is one of:\n\
    scan   - Shows the reachable AoE targets.\n\
    show   - Shows the mounted AoE targets.\n\
    mount  - Mounts an AoE target.  Requires -mac and -u\n\
    umount - Unmounts an AoE disk.  Requires -d\n\
    attach - Attaches <filepath> disk image file.  Requires -u and -m.\n\
             -c, -h, -s are optional.\n\
    detach - Detaches file-backed disk.  Requires -d\n\
  <uri or path> is something like:\n\
    aoe:eX.Y        - Where X is the \"major\" (shelf) and Y is\n\
                      the \"minor\" (slot)\n\
    c:\\my_disk.hdd - The path to a disk image file or .ISO\n\
  <media> is one of 'c' for CD/DVD, 'f' for floppy, 'h' for hard disk drive\n\
\n";
    printf(help_text);
    return 1;
  }

static int STDCALL cmd_scan(void) {
    AOE_SP_MOUNT_TARGETS targets;
    DWORD bytes_returned;
    UINT32 i;
    UCHAR string[256];
    int status = 2;

    targets = malloc(
        sizeof (AOE_S_MOUNT_TARGETS) +
        (32 * sizeof (AOE_S_MOUNT_TARGET))
      );
    if (targets == NULL) {
        printf("Out of memory\n");
        goto err_alloc;
      }

    if (!DeviceIoControl(
        boot_bus,
        IOCTL_AOE_SCAN,
        NULL,
        0,
        targets,
        sizeof (AOE_S_MOUNT_TARGETS) + (32 * sizeof (AOE_S_MOUNT_TARGET)),
        &bytes_returned,
        (LPOVERLAPPED) NULL
      )) {
        WvuShowLastErr();
        status = 2;
        goto err_ioctl;
      }
    if (targets->Count == 0) {
        printf("No AoE targets found.\n");
        goto err_no_targets;
      }
    printf("Client NIC          Target      Server MAC         Size\n");
    for (i = 0; i < targets->Count && i < 10; i++) {
        sprintf(
            string,
            "e%lu.%lu      ",
            targets->Target[i].Major,
            targets->Target[i].Minor
          );
        string[10] = 0;
        printf(
            " %02x:%02x:%02x:%02x:%02x:%02x  %s "
              " %02x:%02x:%02x:%02x:%02x:%02x  %I64uM\n",
            targets->Target[i].ClientMac[0],
            targets->Target[i].ClientMac[1],
            targets->Target[i].ClientMac[2],
            targets->Target[i].ClientMac[3],
            targets->Target[i].ClientMac[4],
            targets->Target[i].ClientMac[5],
            string,
            targets->Target[i].ServerMac[0],
            targets->Target[i].ServerMac[1],
            targets->Target[i].ServerMac[2],
            targets->Target[i].ServerMac[3],
            targets->Target[i].ServerMac[4],
            targets->Target[i].ServerMac[5],
            targets->Target[i].LBASize / 2048
          );
      } /* for */

    err_no_targets:

    status = 0;

    err_ioctl:

    free(targets);
    err_alloc:

    return status;
  }

static int STDCALL cmd_show(void) {
    AOE_SP_MOUNT_DISKS mounted_disks;
    DWORD bytes_returned;
    UINT32 i;
    UCHAR string[256];
    int status = 2;

    mounted_disks = malloc(
        sizeof (AOE_S_MOUNT_DISKS) +
        (32 * sizeof (AOE_S_MOUNT_DISK)) 
      );
    if (mounted_disks == NULL) {
        printf("Out of memory\n");
        goto err_alloc;
      }
  if (!DeviceIoControl(
      boot_bus,
      IOCTL_AOE_SHOW,
      NULL,
      0,
      mounted_disks,
      sizeof (AOE_S_MOUNT_DISKS) + (32 * sizeof (AOE_S_MOUNT_DISK)),
      &bytes_returned,
      (LPOVERLAPPED) NULL
    )) {
      WvuShowLastErr();
      goto err_ioctl;
    }

    status = 0;

    if (mounted_disks->Count == 0) {
        printf("No AoE disks mounted.\n");
        goto err_no_disks;
      }
    printf("Disk  Client NIC         Server MAC         Target      Size\n");
    for (i = 0; i < mounted_disks->Count && i < 10; i++) {
        sprintf(
            string,
            "e%lu.%lu      ",
            mounted_disks->Disk[i].Major,
            mounted_disks->Disk[i].Minor
          );
        string[10] = 0;
        printf(
            " %-4lu %02x:%02x:%02x:%02x:%02x:%02x  "
              "%02x:%02x:%02x:%02x:%02x:%02x  %s  %I64uM\n",
            mounted_disks->Disk[i].Disk,
            mounted_disks->Disk[i].ClientMac[0],
            mounted_disks->Disk[i].ClientMac[1],
            mounted_disks->Disk[i].ClientMac[2],
            mounted_disks->Disk[i].ClientMac[3],
            mounted_disks->Disk[i].ClientMac[4],
            mounted_disks->Disk[i].ClientMac[5],
            mounted_disks->Disk[i].ServerMac[0],
            mounted_disks->Disk[i].ServerMac[1],
            mounted_disks->Disk[i].ServerMac[2],
            mounted_disks->Disk[i].ServerMac[3],
            mounted_disks->Disk[i].ServerMac[4],
            mounted_disks->Disk[i].ServerMac[5],
            string,
            mounted_disks->Disk[i].LBASize / 2048
          );
      }

    err_no_disks:

    err_ioctl:

    free(mounted_disks);
    err_alloc:

    return status;
  }

static int STDCALL cmd_mount(void) {
    UCHAR mac_addr[6];
    UINT32 ver_major, ver_minor;
    UCHAR in_buf[sizeof (WV_S_MOUNT_DISK) + 1024];
    DWORD bytes_returned;

    if (opt_mac.value == NULL || opt_uri.value == NULL) {
        printf("-mac and -u options required.  See -? for help.\n");
        return 1;
      }
    sscanf(
        opt_mac.value,
        "%02x:%02x:%02x:%02x:%02x:%02x",
        (int *) (mac_addr + 0),
        (int *) (mac_addr + 1),
        (int *) (mac_addr + 2),
        (int *) (mac_addr + 3),
        (int *) (mac_addr + 4),
        (int *) (mac_addr + 5)
      );
    sscanf(
        opt_uri.value,
        "aoe:e%lu.%lu",
        (int *) &ver_major,
        (int *) &ver_minor
      );
    printf(
        "mounting e%lu.%lu from %02x:%02x:%02x:%02x:%02x:%02x\n",
        (int) ver_major,
        (int) ver_minor,
        mac_addr[0],
        mac_addr[1],
        mac_addr[2],
        mac_addr[3],
        mac_addr[4],
        mac_addr[5]
      );
    memcpy(in_buf, mac_addr, 6);
    *((PUINT16) (in_buf + 6)) = (UINT16) ver_major;
    *((PUCHAR) (in_buf + 8)) = (UCHAR) ver_minor;
    if (!DeviceIoControl(
        boot_bus,
        IOCTL_AOE_MOUNT,
        in_buf,
        sizeof (in_buf),
        NULL,
        0,
        &bytes_returned,
        (LPOVERLAPPED) NULL
      )) {
        WvuShowLastErr();
        return 2;
      }
    return 0;
  }

static int STDCALL cmd_umount(void) {
    UINT32 disk_num;
    UCHAR in_buf[sizeof (WV_S_MOUNT_DISK) + 1024];
    DWORD bytes_returned;

    if (opt_disknum.value == NULL) {
        printf("-d option required.  See -? for help.\n");
        return 1;
      }
    sscanf(opt_disknum.value, "%d", (int *) &disk_num);
    printf("unmounting disk %d\n", (int) disk_num);
    memcpy(in_buf, &disk_num, 4);
    if (!DeviceIoControl(
        boot_bus,
        IOCTL_AOE_UMOUNT,
        in_buf,
        sizeof (in_buf),
        NULL,
        0,
        &bytes_returned,
        (LPOVERLAPPED) NULL
      )) {
        WvuShowLastErr();
        return 2;
      }
    return 0;
  }

static int STDCALL cmd_attach(void) {
    WV_S_MOUNT_DISK filedisk;
    char obj_path_prefix[] = "\\??\\";
    UCHAR in_buf[sizeof (WV_S_MOUNT_DISK) + 1024];
    DWORD bytes_returned;

    if (opt_uri.value == NULL || opt_media.value == NULL) {
        printf("-u and -m options required.  See -? for help.\n");
        return 1;
      }
    filedisk.type = opt_media.value[0];
    if (opt_cyls.value != NULL)
      sscanf(opt_cyls.value, "%d", (int *) &filedisk.cylinders);
    if (opt_heads.value != NULL)
      sscanf(opt_heads.value, "%d", (int *) &filedisk.heads);
    if (opt_spt.value != NULL)
      sscanf(opt_spt.value, "%d", (int *) &filedisk.sectors);
    memcpy(in_buf, &filedisk, sizeof (WV_S_MOUNT_DISK));
    memcpy(
        in_buf + sizeof (WV_S_MOUNT_DISK),
        obj_path_prefix,
        sizeof (obj_path_prefix)
      );
    memcpy(
        in_buf + sizeof (WV_S_MOUNT_DISK) + sizeof (obj_path_prefix) - 1,
        opt_uri.value,
        strlen(opt_uri.value) + 1
      );
    if (!DeviceIoControl(
        boot_bus,
        IOCTL_FILE_ATTACH,
        in_buf,
        sizeof (in_buf),
        NULL,
        0,
        &bytes_returned,
        (LPOVERLAPPED) NULL
      )) {
        WvuShowLastErr();
        return 2;
      }
    return 0;
  }

static int STDCALL cmd_detach(void) {
    UINT32 disk_num;
    UCHAR in_buf[sizeof (WV_S_MOUNT_DISK) + 1024];
    DWORD bytes_returned;

    if (opt_disknum.value == NULL) {
        printf("-d option required.  See -? for help.\n");
        return 1;
      }
    sscanf(opt_disknum.value, "%d", (int *) &disk_num);
    printf("Detaching file-backed disk %d\n", (int) disk_num);
    memcpy(in_buf, &disk_num, 4);
    if (!DeviceIoControl(
        boot_bus,
        IOCTL_FILE_DETACH,
        in_buf,
        sizeof (in_buf),
        NULL,
        0,
        &bytes_returned,
        (LPOVERLAPPED) NULL
      )) {
        WvuShowLastErr();
        return 2;
      }
    return 0;
  }

int main(int argc, char **argv, char **envp) {
    WVU_FP_CMD_ cmd = cmd_help;
    int status = 1;
    char winvblock[] = "\\\\.\\" WVL_M_LIT;
    char aoe[] = "\\\\.\\AoE";
    char * bus_name;

    cmdline_options(argc, argv);
    /* Check for invalid option. */
    if (invalid_opt != NULL) {
        printf("Use -? for help.  Invalid option: %s\n", invalid_opt);
        goto err_bad_cmd;
      }
    /* Check for cry for help. */
    if (opt_h1.value || opt_h2.value)
      goto do_cmd;
    /* Check for no command. */
    if (opt_cmd.value == NULL)
      goto do_cmd;
    /* Check given command. */
    if (strcmp(opt_cmd.value, "scan") == 0) {
        cmd = cmd_scan;
        bus_name = aoe;
      }
    if (strcmp(opt_cmd.value, "show") == 0) {
        cmd = cmd_show;
        bus_name = aoe;
      }
    if (strcmp(opt_cmd.value, "mount" ) == 0) {
        cmd = cmd_mount;
        bus_name = aoe;
      }
    if (strcmp(opt_cmd.value, "umount") == 0) {
        cmd = cmd_umount;
        bus_name = aoe;
      }
    if (strcmp(opt_cmd.value, "attach") == 0) {
        cmd = cmd_attach;
        bus_name = winvblock;
      }
    if (strcmp(opt_cmd.value, "detach") == 0) {
        cmd = cmd_detach;
        bus_name = winvblock;
      }
    /* Check for invalid command. */
    if (cmd == cmd_help)
      goto do_cmd;

    boot_bus = CreateFile(
        bus_name,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
      );
    if (boot_bus == INVALID_HANDLE_VALUE) {
        WvuShowLastErr();
        goto err_handle;
      }

    do_cmd:
    status = cmd();

    if (boot_bus != NULL)
      CloseHandle(boot_bus);
    err_handle:

    err_bad_cmd:

    return status;
  }
