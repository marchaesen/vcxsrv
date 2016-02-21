@echo off
setlocal

cd "%~dp0"

set BISON_PKGDATADIR=../../../../tools/mhmake/src/bisondata

bison.exe -v -d -p "_mesa_program_" --output=program_parse.tab.c program_parse.y

flex.exe --never-interactive --outfile=lex.yy.c program_lexer.l

endlocal

