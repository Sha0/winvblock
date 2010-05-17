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
if "%2" == "32" set bits=32
if "%2" == "64" set bits=64
if "%2" == "32" set obj=%obj%_w2k_x86
if "%2" == "64" set obj=%obj%_wnet_amd64
goto run

:next_2
if "%1" == "32" set arg2=w2k
if "%1" == "64" set arg2=wnet amd64
if "%1" == "32" set arch=i386
if "%1" == "64" set arch=amd64
if "%1" == "32" set bits=32
if "%1" == "64" set bits=64
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

rem Build order is important here
set sys=winvblock aoe

for /d %%a in (%sys%) do (
  pushd .
  cd src\%%a
  call makedriver.bat
  popd
  )

:end