=============================@( GOto  :@:  CRAZY )=============================
123456789A123456789A123456789A123456789A123456789A123456789A123456789A123456789
ÉÍÍÍÍÍÍÍÍÏÍÍÍÍÍÍÍÍÍÏÍÍÍÍÍÍÍÍÍÏÍÍÍÍÍÍÍÍÍÏÍÍÍÍÍÍÍÍÍÏÍÍÍÍÍÍÍÍÍÏÍÍÍÍÍÍÍÍÍÏÍÍÍÍÍÍÍÍ»
º _ _ _ Each line of this file is optimally formatted for 79 columns. _ _ _ _ º
º _ _ _ Best viewed from a command-line interface (a DOS box), with the _ _ _ º
º _ _ _ _ 'type' command or in Windows Notepad with the Terminal font _ _ _ _ º
ÇÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ¶
º Programmed by Shao Miller @ 2010-05-14_19:52. _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ º
ÈÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍ¼



-----MAIN-----
===

 Extract the .INF file for WinVBlock's bus driver

===
:_main

mkdir bin %_%

%@% Extracting WinVBlk.INF...
%@%
call :_extract WinVBlk > bin\WinVBlk.INF

%@% Extracting TxtSetup.OEM...
%@%
call :_extract TxtSetup > bin\TxtSetup.OEM

%@% Extracting AoE.INF...
%@%
call :_extract AoE > bin\AoE.INF

%@% Extracting HTTPDisk.INF...
%@%
call :_extract HTTPDisk > bin\HTTPDisk.INF

%Q%

-----EOF-----



-----WinVBlk-----
; Common
; ~~~~~~
 
[Version]
Signature="$Windows NT$"
Class=SCSIAdapter
ClassGUID={4D36E97B-E325-11CE-BFC1-08002BE10318}
Provider=WinVBlock
CatalogFile=winvblk.cat
DriverVer=05/16/2010,0.0.0.8
 
[Manufacturer]
WinVBlock=WinVBlockDriver,,NTamd64
 
[SourceDisksNames]
0="Install Disk"
 
[SourceDisksFiles]
winvblk.exe=0
wvblk32.sys=0
wvblk64.sys=0
 
[DestinationDirs]
Files.Driver=12
Files.Driver.NTamd64=12
Files.Tools=11
 
[Files.Tools]
winvblk.exe
 
[PdoDone]
HKR,,PdoDone,0x00010001,1
 
[BootStart]
HKR,,Start,0x00010001,0
 
; x86
; ~~~
 
[WinVBlockDriver]
"WinVBlock Bus"=WinVBlock,Root\WinVBlock, Detected\WinVBlock
 
[Files.Driver]
wvblk32.sys
 
[WinVBlock]
CopyFiles=Files.Driver,Files.Tools
 
[WinVBlock.Services]
AddService=WinVBlock,0x00000002,Service
 
[DefaultInstall]
CopyINF=WinVBlk.INF
CopyFiles=File.Driver,Files.Tools
 
[DefaultInstall.Services]
AddService=WinVBlock,0x00000002,Service
 
[Service]
ServiceType=0x00000001
StartType=0x00000002
ErrorControl=0x00000001
ServiceBinary=%12%\wvblk32.sys
LoadOrderGroup=SCSI miniport
;AddReg=PdoDone
AddReg=BootStart
 
; amd64
; ~~~~~
 
[WinVBlockDriver.NTamd64]
"WinVBlock Bus"=WinVBlock.NTamd64,Root\WinVBlock, Detected\WinVBlock
 
[Files.Driver.NTamd64]
wvblk64.sys
 
[WinVBlock.NTamd64]
CopyFiles=Files.Driver.NTamd64,Files.Tools
 
[WinVBlock.NTamd64.Services]
AddService=WinVBlock,0x00000002,Service.NTamd64
 
[DefaultInstall.NTamd64]
CopyINF=WinVBlk.INF
CopyFiles=File.Driver.NTamd64,Files.Tools
 
[DefaultInstall.NTamd64.Services]
AddService=WinVBlock,0x00000002,Service.NTamd64
 
[Service.NTamd64]
ServiceType=0x00000001
StartType=0x00000002
ErrorControl=0x00000001
ServiceBinary=%12%\wvblk64.sys
LoadOrderGroup=SCSI miniport
;AddReg=PdoDone
AddReg=BootStart
-----EOF-----



-----AoE-----
[Version]
Signature="$Windows NT$"
Class=SCSIAdapter
ClassGUID={4D36E97B-E325-11CE-BFC1-08002BE10318}
Provider=WinVBlock
CatalogFile=aoe.cat
DriverVer=05/16/2010,0.0.0.8
 
[Manufacturer]
WinVBlock=AoEDriver,,NTamd64
  
[AoEDriver]
"AoE Driver"=AoE,WinVBlock\AoE
 
[AoEDriver.NTamd64]
"AoE Driver"=AoE.NTamd64,WinVBlock\AoE
 
[SourceDisksNames]
0="Install Disk"
 
[SourceDisksFiles]
aoe32.sys=0
aoe64.sys=0
 
[DestinationDirs]
Files.Driver=12
Files.Driver.NTamd64=12
 
[Files.Driver]
aoe32.sys
 
[Files.Driver.NTamd64]
aoe64.sys
 
[AoE]
CopyFiles=Files.Driver
 
[AoE.NTamd64]
CopyFiles=Files.Driver.NTamd64
 
[AoE.Services]
AddService=AoE,0x00000002,Service
 
[AoE.NTamd64.Services]
AddService=AoE,0x00000002,Service.NTamd64
 
[Service]
ServiceType=0x00000001
StartType=0x00000000
ErrorControl=0x00000001
ServiceBinary=%12%\aoe32.sys
 
[Service.NTamd64]
ServiceType=0x00000001
StartType=0x00000000
ErrorControl=0x00000001
ServiceBinary=%12%\aoe64.sys
-----EOF-----



-----HTTPDisk-----
[Version]
Signature="$Windows NT$"
Class=SCSIAdapter
ClassGUID={4D36E97B-E325-11CE-BFC1-08002BE10318}
Provider=WinVBlock
CatalogFile=httpdisk.cat
DriverVer=05/16/2010,0.0.0.8
 
[Manufacturer]
WinVBlock=HTTPDiskDriver,,NTamd64
  
[HTTPDiskDriver]
"HTTPDisk Driver"=HTTPDisk,WinVBlock\HTTPDisk
 
[HTTPDiskDriver.NTamd64]
"HTTPDisk Driver"=HTTPDisk.NTamd64,WinVBlock\HTTPDisk
 
[SourceDisksNames]
0="Install Disk"
 
[SourceDisksFiles]
wvhttp32.sys=0
wvhttp64.sys=0
 
[DestinationDirs]
Files.Driver=12
Files.Driver.NTamd64=12
 
[Files.Driver]
wvhttp32.sys
 
[Files.Driver.NTamd64]
wvhttp64.sys
 
[HTTPDisk]
CopyFiles=Files.Driver
 
[HTTPDisk.NTamd64]
CopyFiles=Files.Driver.NTamd64
 
[HTTPDisk.Services]
AddService=HTTPDisk,0x00000002,Service
 
[HTTPDisk.NTamd64.Services]
AddService=HTTPDisk,0x00000002,Service.NTamd64
 
[Service]
ServiceType=0x00000001
StartType=0x00000000
ErrorControl=0x00000001
ServiceBinary=%12%\wvhttp32.sys
 
[Service.NTamd64]
ServiceType=0x00000001
StartType=0x00000000
ErrorControl=0x00000001
ServiceBinary=%12%\wvhttp64.sys
-----EOF-----



-----TxtSetup-----
[Disks]
disk = "WinVBlock Driver Disk",\WinVBlk.inf,\
 
[Defaults]
scsi = WinVBlock32
 
[scsi]
WinVBlock32 = "WinVBlock Bus (32-bit)"
WinVBlock64 = "WinVBlock Bus (64-bit)"
 
[Files.scsi.WinVBlock32]
driver = disk,WVBlk32.Sys,WinVBlock
inf = disk,WinVBlk.Inf
catalog = disk,WinVBlk.Cat
 
[Files.scsi.WinVBlock64]
driver = disk,WVBlk64.Sys,WinVBlock
inf = disk,WinVBlk.Inf
catalog = disk,WinVBlk.Cat
 
[Config.WinVBlock]
value="",TxtSetupInstalled,REG_DWORD,1
 
[HardwareIds.scsi.WinVBlock32]
id="ROOT\WINVBLOCK","WinVBlock"
id="Detected\WinVBlock","WinVBlock"
 
[HardwareIds.scsi.WinVBlock64]
id="ROOT\WINVBLOCK","WinVBlock"
id="Detected\WinVBlock","WinVBlock"
-----EOF-----



-----TESTING-----
This is a wacky batch file.  There is stuff all over the place.
More comments.


:_test

%@% This is the main part of the batch file
%@%

%@% Showing FILE1...
%@%
call :_extract FILE1
%@%

%@% Showing INIFILE...
%@%
call :_extract INIFILE
%@%

%@% Showing FILE2...
%@%
call :_extract FILE2
%@%

%@% Showing LIBRARY...
%@%
call :_extract LIBRARY
%@%

%@% Trying hex...
%@%
call :_hexchar 01020304

%Q%



-----EOF-----



-----LIBRARY-----



===

 Sets _line to the line number that is found for
 the -----SECTION----- "embedded file" found in this file

===
:_find_section

set _line=
for /f "delims=:" %%a in ('findstr /b /n /c:-----%1----- %~sf0') do (
  set _line=%%a
  )
%C%
if "%_line%"=="" (
  %@% Section not found!
  %E%
  )
%Q%



===

 Extract a section from this batch file.  Blank lines and
 the special illegal character are excluded from the output

===
:_extract

call :_find_section %1
%QOE%
for /f "delims=%_ill% skip=%_line%" %%a in (%~sf0) do (
  if "%%a"=="-----EOF-----" (
    %Q%
    )
  %@%%%a
  )
%Q%



===

 Set variable to a generated unique string (we sure hope)

===
:_unique_str

set _unique_str=%time::=_%
set _unique_str=%_unique_str:.=_%
set _unique_str=%_unique_str%_%random%
set %1=%_unique_str%
set _unique_str=
%Q%



===

 Display arbitrary characters, given hex input...  Except CR, LF, ':'

===
:_hexchar

:: Next line is word-wrapped.  Add the hex and some magic to the Registry
reg add hkcu\goto_crazy /v hex /t reg_binary /d 0D0A%13A4D414749434D414749430D0A /f %_%

:: Save the data out of the Registry
call :_unique_str _hive
reg save hkcu\goto_crazy %_hive%.tmp %_%

:: Clean the data out of the Registry
reg delete hkcu\goto_crazy /f %_%

:: Extract the data by the associated magic
call :_unique_str _magic
findstr MAGICMAGIC %_hive%.tmp > %_magic%.tmp 2> NUL

:: Clean-up
del %_hive%.tmp
set _hive=

:: Display the requested data; it's before the magic
for /f "delims=:" %%a in (%_magic%.tmp) do (
  %@%%%a
  )

:: Clean-up
del %_magic%.tmp
set _magic=
%Q%



===

 Sets up the environment for common features used in the batch file

===
:@:

:: Don't show commands
@echo off

:: Display a message
set @=echo.

:: Exit a function or the batch file
set Q=goto :eof

:: Exit a function or the batch file on error condition
set QOE=if errorlevel 1 %Q%

:: Suppress standard output and error messages
set _= ^> NUL 2^>^&1

:: Signal an error condition
set E=cd:%_%

:: Clear an error condition
set C=cd.%_%

:: The unique character not to be used in "file" sections.  Please
:: note that if you are displaying this very LIBRARY section, you
:: will be missing this special character, since it's illegal!
set _ill=#

:: Goto the _main function
goto :_main



-----EOF-----



-----INIFILE-----



[IniSection]
IniEntry = IniValue



-----EOF-----



-----FILE1-----



this is some random text now?

(:inp
echo input
echo input2
goto :eof

:skip
)
) )
) ) )
) ) ) )


echo Line found at: %_line%


echo Skipped
goto :ha
 -----FILE1-----
blah blah



-----FILE2-----
:ha
echo got to ha
goto :eof



-----EOF-----