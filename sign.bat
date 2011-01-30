@echo off
echo Signing...

inf2cat /driver:bin /os:2000,XP_x86,Server2003_x86,Vista_x86,Server2008_x86

if not exist testcer.cer makecert -r -ss my -n CN="Test Certificate for WinVBlock" testcer.cer

signtool sign -ac testcer.cer -n "Test Certificate for WinVBlock" -t http://timestamp.verisign.com/scripts/timestamp.dll -d "WinVBlock Driver" bin\*.sys bin\*.cat bin\*.exe