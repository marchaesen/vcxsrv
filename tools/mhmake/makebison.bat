@echo off
setlocal

set M4=.\m4.exe
set BISON_PKGDATADIR=src/bisondata

bison -d -ra -Ssrc/bisondata/lalr1.cc -o%1/mhmakeparser.cpp src\mhmakeParser.y
python addstdafxh.py %1\mhmakeparser.cpp

endlocal
