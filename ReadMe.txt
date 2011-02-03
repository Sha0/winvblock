Licensed under the GPL version 3 or later.  Some code licensed under the
GPL version 2 or later.  See GPL.TXT.

After starting each driver, you should use Device Manager or the Found
New Hardware wizard to update the drivers using the .INF files.  Please
do this for the WinVBlock driver before installing and starting the
AoE and HTTPDisk drivers.


To install using WinVBlk.Exe:
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Copy whichever .Sys files (drivers) you require into the C:\Windows\System32\Drivers\ directory, then run:

  winvblk.exe -cmd install -service xxxxbb

where xxxxbb is the filename of the driver to install.  For example:

  winvblk.exe -cmd install -service wvblk32


To install using SC.Exe:
~~~~~~~~~~~~~~~~~~~~~~~~
Copy whichever .Sys files (drivers) you require into the whatever directory you like, then run:

  sc create WinVBlock binPath= c:\windows\system32\drivers\wvblk32.sys type= kernel start= boot group= "SCSI miniport"

where you should substitute the path you've chosen.  As another example, for the AoE driver, you could do:

  sc create AoE binPath= c:\windows\system32\drivers\aoe32.sys type= kernel start= boot group= "SCSI Class"

As another example, for the HTTPDisk driver, you could do:

  sc create HTTPDisk binPath= c:\windows\system32\drivers\wvhttp32.sys type= kernel start= boot group= "SCSI Class"


To start the services:
~~~~~~~~~~~~~~~~~~~~~~
You may use the Net.Exe command, as in:

  net start winvblock
  net start aoe
  net start httpdisk


- Shao Miller