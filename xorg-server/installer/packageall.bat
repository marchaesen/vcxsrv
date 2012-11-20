@echo off
if exist vcxsrv*.installer.exe del vcxsrv*.installer.exe

copy %systemroot%\system32\msvcr100.dll
copy %systemroot%\system32\msvcp100.dll
copy %systemroot%\system32\msvcr100d.dll
copy %systemroot%\system32\msvcp100d.dll

if exist "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" (
  "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" vcxsrv.nsi
  "C:\Program Files (x86)\NSIS\Unicode\makensis.exe" vcxsrv-debug.nsi
) else (
  "C:\Program Files\NSIS\makensis.exe" vcxsrv.nsi
  "C:\Program Files\NSIS\makensis.exe" vcxsrv-debug.nsi
)
del msvcr100.dll
del msvcr100d.dll
del msvcp100.dll
del msvcp100d.dll
