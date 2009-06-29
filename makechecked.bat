@echo off
mkdir bin 2>nul
cmd /c makeutils
cmd /c makedriver c 32
cmd /c makedriver c 64
