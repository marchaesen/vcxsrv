@echo off
setlocal

cd "%~dp0"

set M4=..\tools\mhmake\m4.exe
set BISON_PKGDATADIR=../tools/mhmake/src/bisondata

set path=..\tools\mhmake;%path%

..\tools\mhmake\bison.exe %1 %2 %3

endlocal

