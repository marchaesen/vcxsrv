#!/bin/bash

if [[ "$1" != "nox86" ]]; then

  rm -f vcxsrv.*.installer*.exe

  cp "$VCToolsRedistDir/x86/Microsoft.VC143.CRT/msvcp140.dll" .
  cp "$VCToolsRedistDir/x86/Microsoft.VC143.CRT/vcruntime140.dll" .
  cp "$VCToolsRedistDir/debug_nonredist/x86/Microsoft.VC143.DebugCRT/msvcp140d.dll" .
  cp "$VCToolsRedistDir/debug_nonredist/x86/Microsoft.VC143.DebugCRT/vcruntime140d.dll" .

  if [[ -f "/mnt/c/Program Files (x86)/NSIS/makensis.exe" ]]; then
  	if [[ -f ../obj/servrelease/vcxsrv.exe ]]; then
  	  "/mnt/c/Program Files (x86)/NSIS/makensis.exe" vcxsrv.nsi
    fi
  	if [[ -f ../obj/servdebug/vcxsrv.exe ]]; then
  	  "/mnt/c/Program Files (x86)/NSIS/makensis.exe" vcxsrv-debug.nsi
    fi
  else
  	if [[ -f ../obj/servrelease/vcxsrv.exe ]]; then
  	  "/mnt/c/Program Files/NSIS/makensis.exe" vcxsrv.nsi
    fi
  	if [[ -f ../obj/servdebug/vcxsrv.exe ]]; then
  	  "/mnt/c/Program Files/NSIS/makensis.exe" vcxsrv-debug.nsi
    fi
  fi
  patch -p3 < noadmin.patch
  if [[ -f "/mnt/c/Program Files (x86)/NSIS/makensis.exe" ]]; then
  	if [[ -f ../obj/servrelease/vcxsrv.exe ]]; then
  	  "/mnt/c/Program Files (x86)/NSIS/makensis.exe" vcxsrv.nsi
    fi
  	if [[ -f ../obj/servdebug/vcxsrv.exe ]]; then
  	  "/mnt/c/Program Files (x86)/NSIS/makensis.exe" vcxsrv-debug.nsi
    fi
  else
  	if [[ -f ../obj/servrelease/vcxsrv.exe ]]; then
  	  "/mnt/c/Program Files/NSIS/makensis.exe" vcxsrv.nsi
    fi
  	if [[ -f ../obj/servdebug/vcxsrv.exe ]]; then
  	  "/mnt/c/Program Files/NSIS/makensis.exe" vcxsrv-debug.nsi
    fi
  fi
  patch -p3 -R < noadmin.patch
fi

if [[ "$1" != "nox64" ]]; then

  rm -f vcxsrv-64.*.installer*.exe

  cp "$VCToolsRedistDir/x64/Microsoft.VC143.CRT/msvcp140.dll" .
  cp "$VCToolsRedistDir/x64/Microsoft.VC143.CRT/vcruntime140.dll" .
  cp "$VCToolsRedistDir/x64/Microsoft.VC143.CRT/vcruntime140_1.dll" .
  cp "$VCToolsRedistDir/debug_nonredist/x64/Microsoft.VC143.DebugCRT/msvcp140d.dll" .
  cp "$VCToolsRedistDir/debug_nonredist/x64/Microsoft.VC143.DebugCRT/vcruntime140d.dll" .
  cp "$VCToolsRedistDir/debug_nonredist/x64/Microsoft.VC143.DebugCRT/vcruntime140_1d.dll" .

  if [[ -f "/mnt/c/Program Files (x86)/NSIS/makensis.exe" ]]; then
  	if [[ -f ../obj64/servrelease/vcxsrv.exe ]]; then
      "/mnt/c/Program Files (x86)/NSIS/makensis.exe" vcxsrv-64.nsi
    fi
  	if [[ -f ../obj64/servdebug/vcxsrv.exe ]]; then
  	  "/mnt/c/Program Files (x86)/NSIS/makensis.exe" vcxsrv-64-debug.nsi
    fi
  else
  	if [[ -f ../obj64/servrelease/vcxsrv.exe ]]; then
  	  "/mnt/c/Program Files/NSIS/makensis.exe" vcxsrv-64.nsi
    fi
  	if [[ -f ../obj64/servdebug/vcxsrv.exe ]]; then
  	  "/mnt/c/Program Files/NSIS/makensis.exe" vcxsrv-64-debug.nsi
    fi
  fi
  patch -p3 < noadmin.patch
  if [[ -f "/mnt/c/Program Files (x86)/NSIS/makensis.exe" ]]; then
  	if [[ -f ../obj64/servrelease/vcxsrv.exe ]]; then
      "/mnt/c/Program Files (x86)/NSIS/makensis.exe" vcxsrv-64.nsi
    fi
  	if [[ -f ../obj64/servdebug/vcxsrv.exe ]]; then
  	  "/mnt/c/Program Files (x86)/NSIS/makensis.exe" vcxsrv-64-debug.nsi
    fi
  else
  	if [[ -f ../obj64/servrelease/vcxsrv.exe ]]; then
  	  "/mnt/c/Program Files/NSIS/makensis.exe" vcxsrv-64.nsi
    fi
  	if [[ -f ../obj64/servdebug/vcxsrv.exe ]]; then
  	  "/mnt/c/Program Files/NSIS/makensis.exe" vcxsrv-64-debug.nsi
    fi
  fi
  patch -p3 -R < noadmin.patch
fi

rm -f vcruntime140.dll
rm -f vcruntime140d.dll
rm -f msvcp140.dll
rm -f msvcp140d.dll
rm -f vcruntime140_1.dll
rm -f vcruntime140_1d.dll
