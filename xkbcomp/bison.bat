@echo off
setlocal

cd "%~dp0"

set BISON_PKGDATADIR=../tools/mhmake/src/bisondata

c:\winflexbison\win_bison.exe %1 %2 %3

endlocal

