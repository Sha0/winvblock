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

%@% Extracting WinVBlk.INF...
%@%
mkdir bin %_%
call :_extract WinVBlk > bin\WinVBlk.INF
%Q%

-----EOF-----



-----WinVBlk-----
[Version]
Signature="$Windows NT$"
Class=SCSIAdapter
ClassGUID={4D36E97B-E325-11CE-BFC1-08002BE10318}
Provider=WinVBlock
CatalogFile=winvblk.cat
DriverVer=05/16/2010,0.0.0.8
 
[Manufacturer]
WinVBlock=WinVBlockDriver,,NTamd64
  
[WinVBlockDriver]
"WinVBlock Driver"=WinVBlock,Root\WinVBlock, Detected\WinVBlock
 
[WinVBlockDriver.NTamd64]
"WinVBlock Driver"=WinVBlock.NTamd64,Root\WinVBlock, Detected\WinVBlock
 
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
 
[Files.Driver]
wvblk32.sys
 
[Files.Driver.NTamd64]
wvblk64.sys
 
[Files.Tools]
winvblk.exe
 
[WinVBlock]
CopyFiles=Files.Driver,Files.Tools
 
[WinVBlock.NTamd64]
CopyFiles=Files.Driver.NTamd64,Files.Tools
 
[WinVBlock.Services]
AddService=WinVBlock,0x00000002,Service
 
[WinVBlock.NTamd64.Services]
AddService=WinVBlock,0x00000002,Service.NTamd64
 
[Service]
ServiceType=0x00000001
StartType=0x00000000
ErrorControl=0x00000001
ServiceBinary=%12%\wvblk32.sys
 
[Service.NTamd64]
ServiceType=0x00000001
StartType=0x00000000
ErrorControl=0x00000001
ServiceBinary=%12%\wvblk64.sys
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