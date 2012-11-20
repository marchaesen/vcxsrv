@echo off
set variant=%1

set OUTFILE=base.ml%variant%_s.part

if exist %OUTFILE% del %OUTFILE%

echo "{  if (index($2, """(""") == 0) {    printf """  *		%%s		=	+%%s%%%%(v[%variant%]):%variant%\n""", $1, $2;  } else {    printf """  *		%%s		=	+%%s:%variant%\n""", $1, $2;  }}" < layoutsMapping.lst >> %OUTFILE%

gawk "{  printf """  *		%%s(%%s)	=	+%%s(%%s):%variant%\n""", $1, $2, $3, $4;}" < variantsMapping.lst >> %OUTFILE%
