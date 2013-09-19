@echo off
if exist vcxsrv*.installer.exe del vcxsrv*.installer.exe

if "%1"=="nox86" goto skipx86

copy "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\redist\x86\Microsoft.VC110.CRT\msvcp110.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\redist\x86\Microsoft.VC110.CRT\msvcr110.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\redist\Debug_NonRedist\x86\Microsoft.VC110.DebugCRT\msvcp110d.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\redist\Debug_NonRedist\x86\Microsoft.VC110.DebugCRT\msvcr110d.dll"

if exist "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" (
  "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" vcxsrv.nsi
  "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" vcxsrv-debug.nsi
) else (
  "C:\Program Files\NSIS\makensis.exe" vcxsrv.nsi
  "C:\Program Files\NSIS\makensis.exe" vcxsrv-debug.nsi
)

:skipx86
if "%1"=="nox64" goto skipx64

copy "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\redist\x64\Microsoft.VC110.CRT\msvcp110.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\redist\x64\Microsoft.VC110.CRT\msvcr110.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\redist\Debug_NonRedist\x64\Microsoft.VC110.DebugCRT\msvcp110d.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\redist\Debug_NonRedist\x64\Microsoft.VC110.DebugCRT\msvcr110d.dll"

if exist "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" (
  "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" vcxsrv-64.nsi
  "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" vcxsrv-64-debug.nsi
) else (
  "C:\Program Files\NSIS\makensis.exe" vcxsrv-64.nsi
  "C:\Program Files\NSIS\makensis.exe" vcxsrv-64-debug.nsi
)

:skipx64

del msvcr110.dll
del msvcr110d.dll
del msvcp110.dll
del msvcp110d.dll
