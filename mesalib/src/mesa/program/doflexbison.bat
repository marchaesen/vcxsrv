@echo off
setlocal

cd "%~dp0"

set M4=..\..\..\..\tools\mhmake\m4.exe
set BISON_PKGDATADIR=../../../../tools/mhmake/src/bisondata

set path=..\..\..\..\tools\mhmake;%path%

..\..\..\..\tools\mhmake\bison.exe -v -d -p "_mesa_program_" --output=program_parse.tab.c program_parse.y

copy "..\..\..\..\tools\mhmake\flex++.exe" flex.exe
flex.exe --never-interactive --outfile=lex.yy.c program_lexer.l
del flex.exe

endlocal

