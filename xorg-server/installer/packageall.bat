@echo off
if exist vcxsrv*.installer.exe del vcxsrv*.installer.exe

if "%1"=="nox86" goto skipx86

copy "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\redist\x86\Microsoft.VC120.CRT\msvcp120.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\redist\x86\Microsoft.VC120.CRT\msvcr120.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\redist\Debug_NonRedist\x86\Microsoft.VC120.DebugCRT\msvcp120d.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\redist\Debug_NonRedist\x86\Microsoft.VC120.DebugCRT\msvcr120d.dll"

if exist "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" (
  "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" vcxsrv.nsi
  "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" vcxsrv-debug.nsi
) else (
  "C:\Program Files\NSIS\makensis.exe" vcxsrv.nsi
  "C:\Program Files\NSIS\makensis.exe" vcxsrv-debug.nsi
)

:skipx86
if "%1"=="nox64" goto skipx64

copy "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\redist\x64\Microsoft.VC120.CRT\msvcp120.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\redist\x64\Microsoft.VC120.CRT\msvcr120.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\redist\Debug_NonRedist\x64\Microsoft.VC120.DebugCRT\msvcp120d.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\redist\Debug_NonRedist\x64\Microsoft.VC120.DebugCRT\msvcr120d.dll"

if exist "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" (
  "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" vcxsrv-64.nsi
  "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" vcxsrv-64-debug.nsi
) else (
  "C:\Program Files\NSIS\makensis.exe" vcxsrv-64.nsi
  "C:\Program Files\NSIS\makensis.exe" vcxsrv-64-debug.nsi
)

:skipx64

del msvcr120.dll
del msvcr120d.dll
del msvcp120.dll
del msvcp120d.dll
