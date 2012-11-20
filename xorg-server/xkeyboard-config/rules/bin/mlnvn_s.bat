@echo off
set variant=%1

set OUTFILE=base.ml%variant%v%variant%_s.part

if exist %OUTFILE% del %OUTFILE%

gawk "{  printf """  *		%%s		%%s	=	+%%s(%%s):%variant%\n""", $1, $2, $3, $4;}" < variantsMapping.lst >> %OUTFILE%
