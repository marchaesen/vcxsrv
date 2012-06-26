/*  This file is part of vcxsrv.
 *
 *  Copyright (C) 2009 Marc Haesen
 *
 *  vcxsrv is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  vcxsrv is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with vcxsrv.  If not, see <http://www.gnu.org/licenses/>.
*/
;--------------------------------

; The name of the installer
Name "VcXsrv"

; The file to write
OutFile "vcxsrv.1.12.2.0.installer.exe"

; The default installation directory
InstallDir $PROGRAMFILES\VcXsrv

; Registry key to check for directory (so if you install again, it will 
; overwrite the old one automatically)
InstallDirRegKey HKLM "Software\VcXsrv" "Install_Dir"

; Request application privileges for Windows Vista
RequestExecutionLevel admin

;--------------------------------
InstType "Full"
InstType "Minimal"

; Pages

Page components
Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

SetPluginUnload alwaysoff
; ShowInstDetails show
XPStyle on

!define FUSION_REFCOUNT_UNINSTALL_SUBKEY_GUID {8cedc215-ac4b-488b-93c0-a50a49cb2fb8}

!ifdef VS2008
!include runtime
!endif

;--------------------------------
; The stuff to install
Section "VcXsrv (required)"

  SectionIn RO
  SectionIn 1 2

  ; Set output path to the installation directory.
  SetOutPath $INSTDIR

  ; Remove old opengl32.dll file if it extits
  IfFileExists "$INSTDIR\opengl32.dll" 0 +2
    Delete "$INSTDIR\opengl32.dll"

  ; Put files there
  File "..\obj\servrelease\vcxsrv.exe"
  File "..\obj\servdebug\vcxsrv_dbg.exe"
  File "..\obj\servdebug\vcxsrv_dbg.pdb"
  File "..\protocol.txt"
  File "..\system.XWinrc"
  File "..\..\xkbcomp\obj\release\xkbcomp.exe"
  File "..\..\apps\xhost\obj\release\xhost.exe"
  File "..\..\apps\xauth\obj\release\xauth.exe"
  File "..\..\apps\xcalc\obj\release\xcalc.exe"
  File "..\xcalc"
  File "..\xcalc-color"
  File "..\..\apps\xclock\obj\release\xclock.exe"
  File "..\xclock"
  File "..\xclock-color"
  File "..\..\apps\xwininfo\obj\release\xwininfo.exe"
  File "..\XKeysymDB"
  File "..\XErrorDB"
  File "..\XtErrorDB"
  File "..\.Xdefaults"
  File "..\hw\xwin\xlaunch\obj\release\xlaunch.exe"
  File "..\..\tools\plink\obj\release\plink.exe"
  File "..\swrast_dri.dll"
  File "..\swrast_dri_dbg.dll"
  File "..\swrastwgl_dri.dll"
  File "..\swrastwgl_dri_dbg.dll"
  File "..\dxtn.dll"
  File "..\..\libxml2\bin\libxml2.dll"
  File "..\..\zlib\obj\release\zlib1.dll"
  File "..\..\libXau\obj\release\libXau.dll"
  File "..\..\libX11\obj\release\libX11.dll"
  File "..\..\libxml2\bin\iconv.dll"
!ifndef VS2008
  File "msvcr100.dll"
  File "msvcp100.dll"
  File "msvcr100d.dll"
!endif
  SetOutPath $INSTDIR\xkbdata
  File /r "..\xkbdata\*.*"
  SetOutPath $INSTDIR\locale
  File /r "..\locale\*.*"
  SetOutPath $INSTDIR\bitmaps
  File /r "..\bitmaps\*.*"

  ; Write the installation path into the registry
  WriteRegStr HKLM SOFTWARE\VcXsrv "Install_Dir" "$INSTDIR"

  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VcXsrv" "DisplayName" "VcXsrv"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VcXsrv" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VcXsrv" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VcXsrv" "NoRepair" 1
  WriteUninstaller "uninstall.exe"
  
  ; Register the xlaunch file extension
  WriteRegStr HKCR ".xlaunch" "" "XLaunchFile"
  WriteRegStr HKCR "XLaunchFile" "" "XLaunch Configuration"
  WriteRegStr HKCR "XLaunchFile\DefaultIcon" "" "$INSTDIR\xlaunch.exe,0"
  WriteRegStr HKCR "XLaunchFile\shell" "" 'open'
  WriteRegStr HKCR "XLaunchFile\shell\open\command" "" '"$INSTDIR\XLaunch.exe" -run "%1"'
  WriteRegStr HKCR "XLaunchFile\shell\open\ddeexec\Application" "" "XLaunch"
  WriteRegStr HKCR "XLaunchFile\shell\open\ddeexec\Topic" "" "System"
  WriteRegStr HKCR "XLaunchFile\shell\edit\command" "" '"$INSTDIR\XLaunch.exe" -load "%1"'
  WriteRegStr HKCR "XLaunchFile\shell\edit\ddeexec\Application" "" "XLaunch"
  WriteRegStr HKCR "XLaunchFile\shell\edit\ddeexec\Topic" "" "System"
  WriteRegStr HKCR "XLaunchFile\shell\Validate\command" "" '"$INSTDIR\XLaunch.exe" -validate "%1"'
  WriteRegStr HKCR "XLaunchFile\shell\Validate\ddeexec\Application" "" "XLaunch"
  WriteRegStr HKCR "XLaunchFile\shell\Validate\ddeexec\Topic" "" "System"

  WriteRegStr HKCR "Applications\xlaunch.exe\shell" "" 'open'
  WriteRegStr HKCR "Applications\xlaunch.exe\shell\open\command" "" '"$INSTDIR\XLaunch.exe" -run "%1"'
  WriteRegStr HKCR "Applications\xlaunch.exe\shell\open\ddeexec\Application" "" "XLaunch"
  WriteRegStr HKCR "Applications\xlaunch.exe\shell\open\ddeexec\Topic" "" "System"
  WriteRegStr HKCR "Applications\xlaunch.exe\shell\edit\command" "" '"$INSTDIR\XLaunch.exe" -load "%1"'
  WriteRegStr HKCR "Applications\xlaunch.exe\shell\edit\ddeexec\Application" "" "XLaunch"
  WriteRegStr HKCR "Applications\xlaunch.exe\shell\edit\ddeexec\Topic" "" "System"
  WriteRegStr HKCR "Applications\xlaunch.exe\shell\Validate\command" "" '"$INSTDIR\XLaunch.exe" -validate "%1"'
  WriteRegStr HKCR "Applications\xlaunch.exe\shell\Validate\ddeexec\Application" "" "XLaunch"
  WriteRegStr HKCR "Applications\xlaunch.exe\shell\Validate\ddeexec\Topic" "" "System"

!ifdef VS2008
  InitPluginsDir
  SetOutPath $PLUGINSDIR
  File "${MSVCR90_DLL}"
  File "${MSVCM90_DLL}"
  File "${MSVCP90_DLL}"
  File "${MSVC_CAT}"
  File "${MSVC_MANIFEST}"
  File "${MSVCR90_DLL_D}"
  File "${MSVCM90_DLL_D}"
  File "${MSVCP90_DLL_D}"
  File "${MSVC_CAT_D}"
  File "${MSVC_MANIFEST_D}"
  DetailPrint "Installing CRT assembly..."
  System::Call "sxs::CreateAssemblyCache(*i .r0, i 0) i.r1"
  StrCmp $1 0 0 fail
  # Fill a FUSION_INSTALL_REFERENCE.
  # fir.cbSize = sizeof(FUSION_INSTALL_REFERENCE) == 32
  # fir.dwFlags = 0
  # fir.guidScheme = FUSION_REFCOUNT_UNINSTALL_SUBKEY_GUID
  # fir.szIdentifier = "nsissxs"
  # fir.szNonCanonicalData = 0
  System::Call "*(i 32, i 0, i 2364391957, i 1217113163, i 178634899, i 3090139977, w 'nsissxs', w '') i.s"
  Pop $2
  # IAssemblyCache::InstallAssembly(0, manifestPath, fir)
  System::Call "$0->7(i 0, w '$PLUGINSDIR\${MSVC_MANIFEST_PART}', i r2) i.r1"
  StrCmp $1 0 0 failcrt
  System::Call "$0->7(i 0, w '$PLUGINSDIR\${MSVC_MANIFEST_PART_D}', i r2) i.r1"
  StrCmp $1 0 0 faildebugcrt
  System::Free $2
  System::Call "$0->2()"
  Goto end
  
fail:
  DetailPrint "CreateAssemblyCache failed."
  DetailPrint $1
  Goto end

failcrt:
  DetailPrint "InstallAssembly CRT failed."
  DetailPrint $1
  Goto end
faildebugcrt:
  DetailPrint "InstallAssembly Debug CRT failed."
  DetailPrint $1
  Goto end
end:
!endif
SectionEnd

; Optional section (can be disabled by the user)
Section "Fonts"
  SectionIn 1

  SetOutPath $INSTDIR\fonts
  CreateDirectory "$SMPROGRAMS\VcXsrv"
  File /r "..\fonts\*.*"

SectionEnd

; Optional section (can be disabled by the user)
Section "Start Menu Shortcuts"
  SectionIn 1

  SetOutPath $INSTDIR
  CreateDirectory "$SMPROGRAMS\VcXsrv"
  CreateShortCut "$SMPROGRAMS\VcXsrv\Uninstall VcXsrv.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
  CreateShortCut "$SMPROGRAMS\VcXsrv\VcXsrv.lnk" "$INSTDIR\vcxsrv.exe" " :0 -ac -terminate -lesspointer -multiwindow -clipboard -wgl" "$INSTDIR\vcxsrv.exe" 0
  CreateShortCut "$SMPROGRAMS\VcXsrv\XLaunch.lnk" "$INSTDIR\xlaunch.exe" "" "$INSTDIR\xlaunch.exe" 0

SectionEnd

; Optional section (can be disabled by the user)
Section "Desktop Shortcuts"
  SectionIn 1

  SetOutPath $INSTDIR
  CreateShortCut "$DESKTOP\VcXsrv.lnk" "$INSTDIR\vcxsrv.exe" " :0 -ac -terminate -lesspointer -multiwindow -clipboard -wgl" "$INSTDIR\vcxsrv.exe" 0
  CreateShortCut "$DESKTOP\XLaunch.lnk" "$INSTDIR\xlaunch.exe" "" "$INSTDIR\xlaunch.exe" 0

SectionEnd

;--------------------------------

; Uninstaller

Section "Uninstall"
  
  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VcXsrv"
  DeleteRegKey HKLM SOFTWARE\VcXsrv

  ; Register the xlaunch file extension
  DeleteRegKey HKCR ".xlaunch"
  DeleteRegKey HKCR "XLaunchFile"
  DeleteRegKey HKCR "Applications\xlaunch.exe"

  ; Remove files and uninstaller
  Delete "$INSTDIR\vcxsrv.exe"
  Delete "$INSTDIR\vcxsrv_dbg.exe"
  Delete "$INSTDIR\vcxsrv_dbg.pdb"
  Delete "$INSTDIR\uninstall.exe"
  Delete "$INSTDIR\protocol.txt"
  Delete "$INSTDIR\system.XWinrc"
  Delete "$INSTDIR\xkbcomp.exe"
  Delete "$INSTDIR\xcalc.exe"
  Delete "$INSTDIR\xcalc"
  Delete "$INSTDIR\xcalc-color"
  Delete "$INSTDIR\xclock.exe"
  Delete "$INSTDIR\xclock"
  Delete "$INSTDIR\xclock-color"
  Delete "$INSTDIR\xwininfo.exe"
  Delete "$INSTDIR\XKeysymDB"
  Delete "$INSTDIR\XErrorDB"
  Delete "$INSTDIR\XtErrorDB"
  Delete "$INSTDIR\.Xdefaults"
  Delete "$INSTDIR\xlaunch.exe"
  Delete "$INSTDIR\plink.exe"
  Delete "$INSTDIR\swrast_dri.dll"
  Delete "$INSTDIR\swrast_dri_dbg.dll"
  Delete "$INSTDIR\dxtn.dll"
  Delete "$INSTDIR\libxml2.dll"
  Delete "$INSTDIR\zlib1.dll"
  Delete "$INSTDIR\iconv.dll"

!ifndef VS2008
  Delete "$INSTDIR\msvcr100.dll"
  Delete "$INSTDIR\msvcp100.dll"
  Delete "$INSTDIR\msvcr100d.dll"
!endif

  RMDir /r "$INSTDIR\fonts"
  RMDir /r "$INSTDIR\xkbdata"
  RMDir /r "$INSTDIR\locale"

  ; Remove shortcuts, if any
  Delete "$SMPROGRAMS\VcXsrv\*.*"
  Delete "$DESKTOP\VcXsrv.lnk"
  Delete "$DESKTOP\XLaunch.lnk"

  ; Remove directories used
  RMDir "$SMPROGRAMS\VcXsrv"
  RMDir "$INSTDIR"

;  Currently disable uninstalling the run-time; because sometimes it is removing
;  the run-time although other applications are using them
;  DetailPrint "Removing CRT assembly..."
;  System::Call "sxs::CreateAssemblyCache(*i .r0, i 0) i.r1"
;  StrCmp $1 0 0 fail
;  System::Call "*(i 32, i 0, i 2364391957, i 1217113163, i 178634899, i 3090139977, w 'nsissxs', w '') i.s"
;  Pop $2
;  System::Call "$0->3(i 0, w 'Microsoft.VC90.CRT,version=$\"9.0.${MSVC_VERSION}$\",type=$\"win32$\",processorArchitecture=$\"x86$\",publicKeyToken=$\"${MSVC_PUBLICTOKEN}$\"', i r2, *i . r3) i.r1"
;  StrCmp $1 0 0 failcrt
;  System::Call "$0->3(i 0, w 'Microsoft.VC90.DebugCRT,version=$\"9.0.${MSVC_VERSION_D}$\",type=$\"win32$\",processorArchitecture=$\"x86$\",publicKeyToken=$\"${MSVC_PUBLICTOKEN}$\"', i r2, *i . r3) i.r1"
;  StrCmp $1 0 0 faildebugcrt
;  DetailPrint "Disposition returned is $3"
;  System::Free $2
;  System::Call "$0->2()"
;  Goto end
;  fail:
;  DetailPrint "CreateAssemblyCache failed."
;  DetailPrint $1
;  Goto end
;  failcrt:
;  DetailPrint "UninstallAssembly CRT failed."
;  DetailPrint $1
;  Goto end
;  faildebugcrt:
;  DetailPrint "UninstallAssembly Debug CRT failed."
;  DetailPrint $1
;  Goto end
;end:

SectionEnd

