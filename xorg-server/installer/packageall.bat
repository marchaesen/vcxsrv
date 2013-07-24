@echo off
if exist vcxsrv*.installer.exe del vcxsrv*.installer.exe

copy "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\redist\x86\Microsoft.VC100.CRT\msvcp100.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\redist\x86\Microsoft.VC100.CRT\msvcr100.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\redist\Debug_NonRedist\x86\Microsoft.VC100.DebugCRT\msvcp100d.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\redist\Debug_NonRedist\x86\Microsoft.VC100.DebugCRT\msvcr100d.dll"

if exist "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" (
  "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" vcxsrv.nsi
  "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" vcxsrv-debug.nsi
) else (
  "C:\Program Files\NSIS\makensis.exe" vcxsrv.nsi
  "C:\Program Files\NSIS\makensis.exe" vcxsrv-debug.nsi
)

copy "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\redist\x64\Microsoft.VC100.CRT\msvcp100.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\redist\x64\Microsoft.VC100.CRT\msvcr100.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\redist\Debug_NonRedist\x64\Microsoft.VC100.DebugCRT\msvcp100d.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\redist\Debug_NonRedist\x64\Microsoft.VC100.DebugCRT\msvcr100d.dll"

if exist "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" (
  "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" vcxsrv-64.nsi
  "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" vcxsrv-64-debug.nsi
) else (
  "C:\Program Files\NSIS\makensis.exe" vcxsrv-64.nsi
  "C:\Program Files\NSIS\makensis.exe" vcxsrv-64-debug.nsi
)

del msvcr100.dll
del msvcr100d.dll
del msvcp100.dll
del msvcp100d.dll
