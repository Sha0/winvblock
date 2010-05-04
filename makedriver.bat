@echo off
if "%1" == "" goto help
if "%2" == "" goto help
if "%1" == "/?" goto help

if "%1" == "f" goto next_1
if "%1" == "c" goto next_1
if "%1" == "32" goto next_2
if "%1" == "64" goto next_2
goto help

:next_1
if "%1" == "f" set arg1=fre
if "%1" == "c" set arg1=chk
if "%1" == "f" set obj=fre
if "%1" == "c" set obj=chk
if "%2" == "32" goto next_1_ok
if "%2" == "64" goto next_1_ok
goto help

:next_1_ok
if "%2" == "32" set arg2=w2k
if "%2" == "64" set arg2=wnet amd64
if "%2" == "32" set arch=i386
if "%2" == "64" set arch=amd64
if "%2" == "32" set name=wvblk32
if "%2" == "64" set name=wvblk64
if "%2" == "32" set obj=%obj%_w2k_x86
if "%2" == "64" set obj=%obj%_wnet_amd64
goto run

:next_2
if "%1" == "32" set arg2=w2k
if "%1" == "64" set arg2=wnet amd64
if "%1" == "32" set arch=i386
if "%1" == "64" set arch=amd64
if "%1" == "32" set name=wvblk32
if "%1" == "64" set name=wvblk64
if "%1" == "32" set obj=w2k_x86
if "%1" == "64" set obj=wnet_amd64
if "%2" == "f" goto next_2_ok
if "%2" == "c" goto next_2_ok
goto help

:next_2_ok
if "%2" == "f" set arg1=fre
if "%2" == "c" set arg1=chk
if "%2" == "f" set obj=fre_%obj%
if "%2" == "c" set obj=chk_%obj%
goto run

:help
echo.
echo Usage: "makedriver [f|c] [32|64]"
echo.
goto end

:run
mkdir bin 2>nul
call config.bat
pushd .
call %ddkdir%\bin\setenv.bat %ddkdir% %arg1% %arg2%
popd

cd src\bus
echo !INCLUDE $(NTMAKEENV)\makefile.def > makefile
echo INCLUDES=..\include > sources
echo TARGETNAME=%name% >> sources
echo TARGETTYPE=EXPORT_DRIVER >> sources
echo TARGETPATH=obj >> sources
echo TARGETLIBS=$(DDK_LIB_PATH)\\ndis.lib >> sources
echo SOURCES=%c% >> sources
echo C_DEFINES=-DPROJECT_BUS=1 >> sources
echo NAME %name%.sys> %name%.def
build
copy obj%obj%\%arch%\%name%.sys ..\..\bin >nul
copy obj%obj%\%arch%\%name%.pdb ..\..\bin >nul
copy obj%obj%\%arch%\%name%.lib ..\..\bin >nul
del makefile
del sources
del %name%.def
rem del build%obj%.log
rem del build%obj%.wrn 2>nul
rem del build%obj%.err 2>nul
rem rd /s /q obj%obj%
cd ..\..

:end