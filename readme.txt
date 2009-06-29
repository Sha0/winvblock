WinAoE (http://winaoe.org/)

Diskless AoE Driver for:
 Windows 2000
 Windows XP
 Windows XP 64bit
 Windows 2003
 Windows 2003 64bit
 Windows 2003R2 *
 Windows 2003R2 64bit *
 Windows Vista
 Windows Vista 64bit *
*) Not yet fully tested, should work, but need more confirmation.


This driver and code is free software, released under the GPL v3. See gpl.txt and http://www.gnu.org/ for more information.
This readme.txt is slightly outdated, still need to update it with all new options added after 0.95.
In general, the installing information still is the same though.


This driver will take diskless net booting and general AoE access to the windows world.
To use this driver you must either image a working installation with the driver installed or use a real hard disk with an installation with the driver installed.
Using the driver in Windows Setup mode is not yet supported (this might change in the future using RIS).


Installing AoE:
For normal AoE disk mounting, you can simply install the AoE driver through "add new hardware -> choose -> SCSI" and use the aoe utility to scan and mount available blades.
Run aoe from a command window to see the options.


Installing Diskless AoE:

Server side setup:
To make it all work, you need Etherboot capable NICs, either getting Etherboot over PXE or from another media as a floppy.
We'll take the pure PXE road here, to use another media, you can ignore some setup steps here.
The example here uses an Intel pro 1000 card, change to reflect your own settings where needed.
Also, the example here is mainly based on a Slackware Linux server, but it should be very similar for other distributions.
For windows servers, scroll down.

1. make a tftpboot directory in /, add the aoe.0 file to it and enable the tftp daemon

  mkdir /tftpboot
  cp /path/to/aoe.0 /tftpboot
  add the line "tftp dgram udp wait root /usr/sbin/in.tftpd in.tftpd -s /tftpboot -r blksize" to /etc/inetd.conf
  /etc/rc.d/rc.inetd restart

2.a make  an Etherboot zpxe image for your NIC on http://rom-o-matic.net/
   .. or ..
2.b download and unpack the Etherboot source package, compile the correct zpxe driver for your NIC and add the file to tftpboot

  wget http://heanet.dl.sourceforge.net/sourceforge/etherboot/etherboot-5.4.2.tar.bz2
  tar xfj etherboot-5.4.2.tar.bz2
  cd etherboot-5.4.2
  make bin/e1000.zpxe
  cp bin/e1000.zpxe /tftpboot

3. edit your dhcp configuration to look like this:

    host Client {
      hardware ethernet 00:01:02:03:04:05;			# the mac address of your boot NIC
      fixed-address 192.168.0.3;				# your client's IP
      if substring (option vendor-class-identifier, 0, 9) != "Etherboot" {
        filename "/e1000.zpxe";					# the file made in step 2
      } else {
        filename "/aoe.0";					# aoe.0 file found in the archive
      }
      # next is root-path, this is dhcp option 17 (0x11) if you have to manually specify it.
      option root-path "aoe:e0.0";	          		# major and minor id of vblade
    }

4. restart dhcpd

  killall -9 dhcpd
  dhcpd


This concludes the server side setup.
The only thing needed besides this the target AoE device, for instance vblade or AoE hardware from http://www.coraid.com/
Vblade is an Linux software version of the latter.


Client setup:
On an installed system, install the AoE driver through "add new hardware -> choose -> SCSI"
After installing, go in regedit to HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services
There, search the key for the boot NIC (in the case of an Intel E1000 it would be E1000, the one in VMware is VMXNET) and change the "Start" key to 0 (boot).
The AoE driver will automatically load itself before atapi.sys, but it will only boot from a netboot disk if started from the net through aoe.0.
This means you can, for instance, install on a real hard disk, put the disk in a server or Coraid enclosure, netboot from it, and when you want to boot from it in a normal way (attached to the PC itself) you can put it back and let it boot as normal.


Client imaging in vblade:
The image to boot from can be any file or block device on the Linux server.
The driver will take the heads/sectors/cylinders automatically from the partition table, so any real or fake disk image will do (if there is no partition table, it will default to 63 sectors/track, 255 heads).
The easiest way to do get a workable block device is taking a hard disk of an installed system or a VMware full disk image, but also own made images, partitioned in Linux fdisk, will work.

There are a few ways to make an image, with the easiest being just taking a live working hard disk and using that block device, or making an image (with dd) from a working disk to a file on the Linux server.
Another handy way is to install on a small partition, dd the partition AND the 1st 63 sectors before that partition to a Linux block device (raid devices for instance), and lastly use the Linux ntfsresize util to expand it to the full device.
This option lets you store a small (4GB) file as a fast way to reinstall, yet have a big raid disk to install to.


Windows server:
All above steps concerning dhcpd and tftp could also be run on a windows server without any problems, check your manual how.However, vblade of course does not run on windows, so you must use either Coraid's enclosures, or find a working AoE windows target.
To counter these issues just use a proper server OS (Linux/BSD/other UNIX like systems).


Compiling:
To compile this you need either MSYS or the windows 2003 DDK, preferably both.
Set the path to the DDK in config.bat and run the bat files makefree or makechecked to build everything but aoe.0.
aoe.0 will not be deleted and is prebuild in the package, so you can get away with only using the DDK.
To build aoe.0 you need MSYS or a Linux box (type make in the pxe.asm or pxe.c dir in Linux to compile it).
MSYS can also build the driver itself, but only the 32bit version, and it will not generate windbg symbol files.
Also, the MSYS generated driver has a higher chance of failing, due to compiler bugs.
When compiling the driver with makefree/makechecked or in MSYS with make, a loader32/64.exe will also be build to quickly install the driver without going through the control panel.
This is mostly useful in virtual environments (VMware) where the disk is non-persistent.
With it you can simply reboot the virtual machine to restart anew, and start loader.32 to boot it (using dbgview http://technet.microsoft.com/en-us/sysinternals/bb896647.aspx to see debug output)

Lastly, the txtsetup.oem file is not needed as of yet, it is part of the "to be" RIS function (if that will ever work)


TODO's, Notes and known issues:
0. Big TODO... fix this readme.txt (like adding compile information).
1. you CAN NOT change the position of the NIC in the PCI slots! (you should however be able to change NICs around by adding a second NIC, booting over the first, setting the second to start at boot and then removing the first)
2. There might be ways in the future to install directly to Coraid hardware/vblade using RIS, but options are limited and might not work.
3. The driver will not work with the original drive attached. to fix this, either remove the "group" parameter in the service entry in the registry of the driver on which the disk is attached (for atapi this is done on installing the AoE driver, for SATA, SCSI and others, search the correct service), or zero out the MBR of the drive.
4. adding new PCI cards might be slow (need confirmation on this).
5. uninstall StarPorts AoE driver for now, they conflict. don't know why (StarPorts fault, that's the one to crash after we have loaded).
6. qemu, virtualbox and in some cases real hardware fails with lots of PacketAllocation errors, unknown cause as of yet (happens in VMware on windows 2000 too).
7. User friendliness is lacking
8. Client trunking and Server trunking is not yet enabled.
9. aoe.0 does not work with Vista yet, use gPXE's AoE support for now.
10. unmounting when multiple disks are mounted may cause a BSOD. unmounting when only a single disk is mounted should be ok.
