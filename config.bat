rem This is the root directory of the ddk.
set ddkdir=c:\winddk\6001.18001

rem Next two lines are duplicated in Makefile, edit both when adding files or changing pxe style.
set c=driver.c registry.c bus.c bus_pnp.c bus_dev_ctl.c disk.c ramdisk.c filedisk.c memdisk.c grub4dos.c disk_pnp.c disk_dev_ctl.c disk_scsi.c debug.c probe.c winvblock.rc irp.c
set pxestyle=asm
