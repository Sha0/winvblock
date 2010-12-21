@echo off

set c=driver.c bus.c protocol.c registry.c aoe.rc wv_stdlib.c wv_string.c

set name=AoE%bits%

echo INCLUDES=..\include			> sources
echo TARGETNAME=%name%				>> sources
echo TARGETTYPE=DRIVER				>> sources
echo TARGETPATH=obj				>> sources
echo TARGETLIBS=..\\..\\bin\\WVBlk%bits%.lib   \>> sources
echo            $(DDK_LIB_PATH)\\ndis.lib	>> sources
echo SOURCES=%c%				>> sources
echo C_DEFINES=-DPROJECT_AOE=1			>> sources

build
copy obj%obj%\%arch%\%name%.sys ..\..\bin >nul
copy obj%obj%\%arch%\%name%.pdb ..\..\bin >nul
copy obj%obj%\%arch%\%name%.lib ..\..\bin >nul