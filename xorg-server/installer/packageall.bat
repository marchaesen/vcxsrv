@echo off
if exist vcxsrv*.installer.exe del vcxsrv*.installer.exe

if "%1"=="nox86" goto skipx86

rem copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\x86\Microsoft.VC140.CRT\msvcp140.dll"
rem copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\x86\Microsoft.VC140.CRT\msvcr140.dll"
rem copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\Debug_NonRedist\x86\Microsoft.VC140.DebugCRT\msvcp140d.dll"
rem copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\Debug_NonRedist\x86\Microsoft.VC140.DebugCRT\msvcr140d.dll"

if exist "C:\Program Files (x86)\NSIS\makensis.exe" (
  "C:\Program Files (x86)\NSIS\makensis.exe" vcxsrv.nsi
  "C:\Program Files (x86)\NSIS\makensis.exe" vcxsrv-debug.nsi
) else (
  "C:\Program Files\NSIS\makensis.exe" vcxsrv.nsi
  "C:\Program Files\NSIS\makensis.exe" vcxsrv-debug.nsi
)

:skipx86
if "%1"=="nox64" goto skipx64

rem copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\x64\Microsoft.VC140.CRT\msvcp140.dll"
rem copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\x64\Microsoft.VC140.CRT\msvcr140.dll"
rem copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\Debug_NonRedist\x64\Microsoft.VC140.DebugCRT\msvcp140d.dll"
rem copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\Debug_NonRedist\x64\Microsoft.VC140.DebugCRT\msvcr140d.dll"

if exist "C:\Program Files (x86)\NSIS\makensis.exe" (
  "C:\Program Files (x86)\NSIS\makensis.exe" vcxsrv-64.nsi
  "C:\Program Files (x86)\NSIS\makensis.exe" vcxsrv-64-debug.nsi
) else (
  "C:\Program Files\NSIS\makensis.exe" vcxsrv-64.nsi
  "C:\Program Files\NSIS\makensis.exe" vcxsrv-64-debug.nsi
)

:skipx64

rem del msvcr140.dll
rem del msvcr140d.dll
rem del msvcp140.dll
rem del msvcp140d.dll
