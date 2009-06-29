@echo off
mkdir bin 2>nul

echo [Version] > bin/aoe.inf
echo Signature="$Windows NT$" >> bin/aoe.inf
echo Class=SCSIAdapter >> bin/aoe.inf
echo ClassGUID={4D36E97B-E325-11CE-BFC1-08002BE10318} >> bin/aoe.inf
echo Provider=AoE >> bin/aoe.inf
echo CatalogFile=aoe.cat >> bin/aoe.inf
echo DriverVer=01/01/2006,1.0 >> bin/aoe.inf
echo. >> bin/aoe.inf
echo [Manufacturer] >> bin/aoe.inf
echo AoE=AoEDriver,,NTamd64 >> bin/aoe.inf
echo.  >> bin/aoe.inf
echo [AoEDriver] >> bin/aoe.inf
echo "AoE Driver"=AoE,AoE >> bin/aoe.inf
echo. >> bin/aoe.inf
echo [AoEDriver.NTamd64] >> bin/aoe.inf
echo "AoE Driver"=AoE.NTamd64,AoE >> bin/aoe.inf
echo. >> bin/aoe.inf
echo [SourceDisksNames] >> bin/aoe.inf
echo 0="Install Disk" >> bin/aoe.inf
echo. >> bin/aoe.inf
echo [SourceDisksFiles] >> bin/aoe.inf
echo aoe.exe=0 >> bin/aoe.inf
echo aoe32.sys=0 >> bin/aoe.inf
echo aoe64.sys=0 >> bin/aoe.inf
echo. >> bin/aoe.inf
echo [DestinationDirs] >> bin/aoe.inf
echo Files.Driver=12 >> bin/aoe.inf
echo Files.Driver.NTamd64=12 >> bin/aoe.inf
echo Files.Tools=11 >> bin/aoe.inf
echo. >> bin/aoe.inf
echo [Files.Driver] >> bin/aoe.inf
echo aoe32.sys >> bin/aoe.inf
echo. >> bin/aoe.inf
echo [Files.Driver.NTamd64] >> bin/aoe.inf
echo aoe64.sys >> bin/aoe.inf
echo. >> bin/aoe.inf
echo [Files.Tools] >> bin/aoe.inf
echo aoe.exe >> bin/aoe.inf
echo. >> bin/aoe.inf
echo [AoE] >> bin/aoe.inf
echo CopyFiles=Files.Driver,Files.Tools >> bin/aoe.inf
#echo DelReg=DelReg >> bin/aoe.inf
echo. >> bin/aoe.inf
echo [AoE.NTamd64] >> bin/aoe.inf
echo CopyFiles=Files.Driver.NTamd64,Files.Tools >> bin/aoe.inf
#echo DelReg=DelReg >> bin/aoe.inf
echo. >> bin/aoe.inf
echo [AoE.Services] >> bin/aoe.inf
echo AddService=AoE,0x00000002,Service >> bin/aoe.inf
echo. >> bin/aoe.inf
echo [AoE.NTamd64.Services] >> bin/aoe.inf
echo AddService=AoE,0x00000002,Service.NTamd64 >> bin/aoe.inf
echo. >> bin/aoe.inf
echo [Service] >> bin/aoe.inf
echo ServiceType=0x00000001 >> bin/aoe.inf
echo StartType=0x00000000 >> bin/aoe.inf
echo ErrorControl=0x00000001 >> bin/aoe.inf
echo ServiceBinary=%%12%%\aoe32.sys >> bin/aoe.inf
#echo LoadOrderGroup=NDIS >> bin/aoe.inf
echo. >> bin/aoe.inf
echo [Service.NTamd64] >> bin/aoe.inf
echo ServiceType=0x00000001 >> bin/aoe.inf
echo StartType=0x00000000 >> bin/aoe.inf
echo ErrorControl=0x00000001 >> bin/aoe.inf
echo ServiceBinary=%%12%%\aoe64.sys >> bin/aoe.inf
#echo LoadOrderGroup=NDIS >> bin/aoe.inf
#echo. >> bin/aoe.inf
#echo [DelReg] >> bin/aoe.inf
#echo HKLM,SYSTEM\CurrentControlSet\Services\atapi,Group >> bin/aoe.inf

echo [Disks] > bin/txtsetup.oem
echo disk = "AOE DISK",\aoe.inf,\ >> bin/txtsetup.oem
echo. >> bin/txtsetup.oem
echo [Defaults] >> bin/txtsetup.oem
echo scsi = AOE >> bin/txtsetup.oem
echo. >> bin/txtsetup.oem
echo [scsi] >> bin/txtsetup.oem
echo AOE = "AoE Driver" >> bin/txtsetup.oem
echo. >> bin/txtsetup.oem
echo [Files.scsi.AOE] >> bin/txtsetup.oem
echo driver = disk,aoe32.sys,AoE >> bin/txtsetup.oem
echo inf = disk,aoe.inf >> bin/txtsetup.oem
