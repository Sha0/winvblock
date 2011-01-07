@echo off

set c=ksocket.c ktdi.c httpdisk.c bus.c httpdisk.rc

set name=WvHTTP%bits%

echo INCLUDES=^
..\include;^
$(DDK_INC_PATH);^
$(BASEDIR)\inc\mfc42;^
$(BASEDIR)\src\network\inc			> sources

echo TARGETNAME=%name%				>> sources
echo TARGETTYPE=DRIVER				>> sources
echo TARGETPATH=obj				>> sources
echo TARGETLIBS=..\\..\\bin\\WVBlk%bits%.lib   \>> sources
echo            $(DDK_LIB_PATH)\\ndis.lib      \>> sources
echo            $(SDK_LIB_PATH)\libcntpr.lib    >> sources
echo SOURCES=%c%				>> sources
echo C_DEFINES=-DPROJECT_AOE=1			>> sources

build
copy obj%obj%\%arch%\%name%.sys ..\..\bin >nul
copy obj%obj%\%arch%\%name%.pdb ..\..\bin >nul
copy obj%obj%\%arch%\%name%.lib ..\..\bin >nul