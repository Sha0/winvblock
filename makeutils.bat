@echo off
mkdir bin 2>nul
call config.bat
cd src
pushd .
call %ddkdir%\bin\setenv.bat %ddkdir% w2k
popd
pushd .
cd util
cl /I%CRT_INC_PATH% /I..\include /DWIN32_LEAN_AND_MEAN mount.c /Fe..\..\bin\winvblk.exe /link /LIBPATH:%DDK_LIB_DEST%\i386 /LIBPATH:%Lib%\crt\i386 bufferoverflowU.lib
del mount.obj
popd
pushd .
cd loader
cl /I%CRT_INC_PATH% /I..\include /DWIN32_LEAN_AND_MEAN loader.c /Fe..\..\bin\loader32.exe /link /LIBPATH:%DDK_LIB_DEST%\i386 /LIBPATH:%Lib%\crt\i386 setupapi.lib bufferoverflowU.lib
del loader.obj
popd
pushd .
call %ddkdir%\bin\setenv.bat %ddkdir% wnet amd64
popd
pushd .
cd loader
cl /I%CRT_INC_PATH% /I..\include /DWIN32_LEAN_AND_MEAN loader.c /Fe..\..\bin\loader64.exe /link /LIBPATH:%DDK_LIB_DEST%\%_BUILDARCH% /LIBPATH:%Lib%\crt\%_BUILDARCH% setupapi.lib bufferoverflowU.lib
del loader.obj
popd
cd ..
