@echo off

set libname=safehook

set c=pdo.c probe.c

echo !INCLUDE $(NTMAKEENV)\makefile.def	> makefile

echo INCLUDES=..\..\include		> sources
echo TARGETNAME=%libname%		>> sources
echo TARGETTYPE=DRIVER_LIBRARY		>> sources
echo TARGETPATH=obj			>> sources
echo SOURCES=%c%			>> sources
echo C_DEFINES=-DPROJECT_WV=1		>> sources

build
set links=%links% %libname%\\obj%obj%\\%arch%\\%libname%.lib
