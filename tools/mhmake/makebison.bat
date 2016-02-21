@echo off
setlocal

set BISON_PKGDATADIR=src/bisondata

bison -d -ra -Ssrc/bisondata/lalr1.cc -o%1/mhmakeparser.cpp src\mhmakeParser.y
python addstdafxh.py %1\mhmakeparser.cpp

endlocal
