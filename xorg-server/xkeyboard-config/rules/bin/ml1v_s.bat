@echo off

set OUTFILE=base.ml1v_s.part

if exist %OUTFILE% del %OUTFILE%

gawk "{   printf """  *		%%s		%%s		=	pc+%%s(%%s)\n""", $1, $2, $3, $4; }" < variantsMapping.lst >> %OUTFILE%
