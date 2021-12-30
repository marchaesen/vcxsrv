@echo off
setlocal

flex.exe --nounistd -Ssrc/flex.skl -o%1/mhmakelexer.cpp src/mhmakelexer.l

c:\Python39\python.exe addstdafxh.py %1\mhmakelexer.cpp

endlocal

