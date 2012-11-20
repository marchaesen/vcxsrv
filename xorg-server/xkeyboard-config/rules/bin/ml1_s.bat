@echo off

set OUTFILE=base.ml1_s.part

if exist %OUTFILE% del %OUTFILE%

gawk "{   if (index($2, """(""") == 0) {    printf """  *		%%s			=	pc+%%s%%%%(v[1])\n""", $1, $2;   } else {    printf """  *		%%s			=	pc+%%s\n""", $1, $2;   }}" < layoutsMapping.lst >> %OUTFILE%

gawk "{   printf """  *		%%s(%%s)			=	pc+%%s(%%s)\n""", $1, $2, $3, $4; }" < variantsMapping.lst >> %OUTFILE%
