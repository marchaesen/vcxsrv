@echo off
setlocal

cd "%~dp0"

set M4=..\..\..\tools\mhmake\m4.exe
set BISON_PKGDATADIR=../../../tools/mhmake/src/bisondata

set path=..\..\..\tools\mhmake;%path%

..\..\..\tools\mhmake\bison.exe -d -o%1/winprefsyacc.c winprefsyacc.y

copy "..\..\..\tools\mhmake\flex++.exe" flex.exe
flex.exe -i -o%1/winprefslex.c winprefslex.l
del flex.exe

endlocal

