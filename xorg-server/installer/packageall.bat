@echo off
if exist vcxsrv*.installer.exe del vcxsrv*.installer.exe

if "%1"=="nox86" goto skipx86

copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\x86\Microsoft.VC140.CRT\msvcp140.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\x86\Microsoft.VC140.CRT\vcruntime140.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\Debug_NonRedist\x86\Microsoft.VC140.DebugCRT\msvcp140d.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\Debug_NonRedist\x86\Microsoft.VC140.DebugCRT\vcruntime140d.dll"

if exist "C:\Program Files (x86)\NSIS\makensis.exe" (
  "C:\Program Files (x86)\NSIS\makensis.exe" vcxsrv.nsi
  "C:\Program Files (x86)\NSIS\makensis.exe" vcxsrv-debug.nsi
) else (
  "C:\Program Files\NSIS\makensis.exe" vcxsrv.nsi
  "C:\Program Files\NSIS\makensis.exe" vcxsrv-debug.nsi
)

:skipx86
if "%1"=="nox64" goto skipx64

copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\x64\Microsoft.VC140.CRT\msvcp140.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\x64\Microsoft.VC140.CRT\vcruntime140.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\Debug_NonRedist\x64\Microsoft.VC140.DebugCRT\msvcp140d.dll"
copy "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\redist\Debug_NonRedist\x64\Microsoft.VC140.DebugCRT\vcruntime140d.dll"

if exist "C:\Program Files (x86)\NSIS\makensis.exe" (
  "C:\Program Files (x86)\NSIS\makensis.exe" vcxsrv-64-debug.nsi
) else (
  "C:\Program Files\NSIS\makensis.exe" vcxsrv-64-debug.nsi
)

:skipx64

del vcruntime140.dll
del vcruntime140d.dll
del msvcp140.dll
del msvcp140d.dll
