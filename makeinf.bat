@echo off
mkdir bin 2>nul

echo [Version] > bin/winvblk.inf
echo Signature="$Windows NT$" >> bin/winvblk.inf
echo Class=SCSIAdapter >> bin/winvblk.inf
echo ClassGUID={4D36E97B-E325-11CE-BFC1-08002BE10318} >> bin/winvblk.inf
echo Provider=WinVBlock >> bin/winvblk.inf
echo CatalogFile=winvblk.cat >> bin/winvblk.inf
echo DriverVer=01/06/2010,1.0 >> bin/winvblk.inf
echo. >> bin/winvblk.inf
echo [Manufacturer] >> bin/winvblk.inf
echo WinVBlock=WinVBlockDriver,,NTamd64 >> bin/winvblk.inf
echo.  >> bin/winvblk.inf
echo [WinVBlockDriver] >> bin/winvblk.inf
echo "WinVBlock Driver"=WinVBlock,Root\WinVBlock, Detected\WinVBlock >> bin/winvblk.inf
echo. >> bin/winvblk.inf
echo [WinVBlockDriver.NTamd64] >> bin/winvblk.inf
echo "WinVBlock Driver"=WinVBlock.NTamd64,Root\WinVBlock, Detected\WinVBlock >> bin/winvblk.inf
echo. >> bin/winvblk.inf
echo [SourceDisksNames] >> bin/winvblk.inf
echo 0="Install Disk" >> bin/winvblk.inf
echo. >> bin/winvblk.inf
echo [SourceDisksFiles] >> bin/winvblk.inf
echo winvblk.exe=0 >> bin/winvblk.inf
echo wvblk32.sys=0 >> bin/winvblk.inf
echo wvblk64.sys=0 >> bin/winvblk.inf
echo. >> bin/winvblk.inf
echo [DestinationDirs] >> bin/winvblk.inf
echo Files.Driver=12 >> bin/winvblk.inf
echo Files.Driver.NTamd64=12 >> bin/winvblk.inf
echo Files.Tools=11 >> bin/winvblk.inf
echo. >> bin/winvblk.inf
echo [Files.Driver] >> bin/winvblk.inf
echo wvblk32.sys >> bin/winvblk.inf
echo. >> bin/winvblk.inf
echo [Files.Driver.NTamd64] >> bin/winvblk.inf
echo wvblk64.sys >> bin/winvblk.inf
echo. >> bin/winvblk.inf
echo [Files.Tools] >> bin/winvblk.inf
echo winvblk.exe >> bin/winvblk.inf
echo. >> bin/winvblk.inf
echo [WinVBlock] >> bin/winvblk.inf
echo CopyFiles=Files.Driver,Files.Tools >> bin/winvblk.inf
#echo DelReg=DelReg >> bin/winvblk.inf
echo. >> bin/winvblk.inf
echo [WinVBlock.NTamd64] >> bin/winvblk.inf
echo CopyFiles=Files.Driver.NTamd64,Files.Tools >> bin/winvblk.inf
#echo DelReg=DelReg >> bin/winvblk.inf
echo. >> bin/winvblk.inf
echo [WinVBlock.Services] >> bin/winvblk.inf
echo AddService=WinVBlock,0x00000002,Service >> bin/winvblk.inf
echo. >> bin/winvblk.inf
echo [WinVBlock.NTamd64.Services] >> bin/winvblk.inf
echo AddService=WinVBlock,0x00000002,Service.NTamd64 >> bin/winvblk.inf
echo. >> bin/winvblk.inf
echo [Service] >> bin/winvblk.inf
echo ServiceType=0x00000001 >> bin/winvblk.inf
echo StartType=0x00000000 >> bin/winvblk.inf
echo ErrorControl=0x00000001 >> bin/winvblk.inf
echo ServiceBinary=%%12%%\wvblk32.sys >> bin/winvblk.inf
#echo LoadOrderGroup=NDIS >> bin/winvblk.inf
echo. >> bin/winvblk.inf
echo [Service.NTamd64] >> bin/winvblk.inf
echo ServiceType=0x00000001 >> bin/winvblk.inf
echo StartType=0x00000000 >> bin/winvblk.inf
echo ErrorControl=0x00000001 >> bin/winvblk.inf
echo ServiceBinary=%%12%%\wvblk64.sys >> bin/winvblk.inf
#echo LoadOrderGroup=NDIS >> bin/winvblk.inf
#echo. >> bin/winvblk.inf
#echo [DelReg] >> bin/winvblk.inf
#echo HKLM,SYSTEM\CurrentControlSet\Services\atapi,Group >> bin/winvblk.inf

echo [Disks] > bin/txtsetup.oem
echo disk = "WINVBLOCK DISK",\winvblk.inf,\ >> bin/txtsetup.oem
echo. >> bin/txtsetup.oem
echo [Defaults] >> bin/txtsetup.oem
echo scsi = WINVBLOCK >> bin/txtsetup.oem
echo. >> bin/txtsetup.oem
echo [scsi] >> bin/txtsetup.oem
echo WINVBLOCK = "WinVBlock Driver" >> bin/txtsetup.oem
echo. >> bin/txtsetup.oem
echo [Files.scsi.WINVBLOCK] >> bin/txtsetup.oem
echo driver = disk,wvblk32.sys,WinVBlock >> bin/txtsetup.oem
echo inf = disk,winvblk.inf >> bin/txtsetup.oem
