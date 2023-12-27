@echo off
setlocal

cd "%~dp0"

set BISON_PKGDATADIR=../../../tools/mhmake/src/bisondata

c:\winflexbison\win_bison.exe -d -o%1/winprefsyacc.c winprefsyacc.y

c:\winflexbison\win_flex.exe -i -o%1/winprefslex.c winprefslex.l

endlocal

