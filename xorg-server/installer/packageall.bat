@echo off

if "%1"=="nox86" goto skipx86

if exist vcxsrv.*.installer.exe del vcxsrv.*.installer.exe

copy "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.32.31326\x86\Microsoft.VC143.CRT\msvcp140.dll"
copy "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.32.31326\x86\Microsoft.VC143.CRT\vcruntime140.dll"
copy "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.32.31326\debug_nonredist\x86\Microsoft.VC143.DebugCRT\msvcp140d.dll"
copy "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.32.31326\debug_nonredist\x86\Microsoft.VC143.DebugCRT\vcruntime140d.dll"

if exist "C:\Program Files (x86)\NSIS\makensis.exe" (
	if exist ..\obj\servrelease\vcxsrv.exe "C:\Program Files (x86)\NSIS\makensis.exe" vcxsrv.nsi
	if exist ..\obj\servdebug\vcxsrv.exe "C:\Program Files (x86)\NSIS\makensis.exe" vcxsrv-debug.nsi
) else (
	if exist ..\obj\servrelease\vcxsrv.exe "C:\Program Files\NSIS\makensis.exe" vcxsrv.nsi
	if exist ..\obj\servdebug\vcxsrv.exe "C:\Program Files\NSIS\makensis.exe" vcxsrv-debug.nsi
)

:skipx86
if "%1"=="nox64" goto skipx64

if exist vcxsrv-64.*.installer.exe del vcxsrv-64.*.installer.exe

copy "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.32.31326\x64\Microsoft.VC143.CRT\msvcp140.dll"
copy "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.32.31326\x64\Microsoft.VC143.CRT\vcruntime140.dll"
copy "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.32.31326\x64\Microsoft.VC143.CRT\vcruntime140_1.dll"
copy "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.32.31326\debug_nonredist\x64\Microsoft.VC143.DebugCRT\msvcp140d.dll"
copy "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.32.31326\debug_nonredist\x64\Microsoft.VC143.DebugCRT\vcruntime140d.dll"
copy "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.32.31326\debug_nonredist\x64\Microsoft.VC143.DebugCRT\vcruntime140_1d.dll"

if exist "C:\Program Files (x86)\NSIS\makensis.exe" (
	if exist ..\obj64\servrelease\vcxsrv.exe "C:\Program Files (x86)\NSIS\makensis.exe" vcxsrv-64.nsi
	if exist ..\obj64\servdebug\vcxsrv.exe "C:\Program Files (x86)\NSIS\makensis.exe" vcxsrv-64-debug.nsi
) else (
	if exist ..\obj32\servrelease\vcxsrv.exe "C:\Program Files\NSIS\makensis.exe" vcxsrv-64.nsi
	if exist ..\obj32\servdebug\vcxsrv.exe "C:\Program Files\NSIS\makensis.exe" vcxsrv-64-debug.nsi
)

del vcruntime140_1.dll
del vcruntime140_1d.dll

:skipx64

del vcruntime140.dll
del vcruntime140d.dll
del msvcp140.dll
del msvcp140d.dll
