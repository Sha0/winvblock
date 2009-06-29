@echo off
mkdir bin 2>nul
cmd /c makeutils
cmd /c makedriver f 32
cmd /c makedriver f 64
