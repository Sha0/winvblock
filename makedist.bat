@echo off
call config.bat
rd /s /q bin 2>nul
rd /s /q src\obj 2>nul
rd /s /q src\nbp\pxe.asm\obj 2>nul
rd /s /q src\nbp\pxe.c\obj 2>nul
mkdir bin 2>nul
cmd /c makeutils
cmd /c makeinf
cmd /c makedriver f 32
cmd /c makedriver f 64
del bin\loader32.exe
del bin\loader64.exe
del bin\wvblk32.pdb
del bin\wvblk64.pdb
copy src\nbp\pxe.%pxestyle%\aoe.0 bin
