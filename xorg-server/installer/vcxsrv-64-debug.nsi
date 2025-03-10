/*  This file is part of vcxsrv.
 *
 *  Copyright (C) 2024 https://github.com/marchaesen
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
!include "FileFunc.nsh"

!define NAME "VcXsrv"
!define VERSION "21.1.16.1"
!define UNINSTALL_PUBLISHER "${NAME}"
!define UNINSTALL_URL "https://github.com/marchaesen/vcxsrv"

; The name of the installer
Name "${NAME}"

; The file to write
OutFile "vcxsrv-64-debug.${VERSION}.installer.exe"

; The default installation directory
InstallDir $programfiles64\VcXsrv

SetCompressor /SOLID lzma

; Registry key to check for directory (so if you install again, it will
; overwrite the old one automatically)
InstallDirRegKey HKLM SOFTWARE\VcXsrv "Install_Dir_64"

LoadLanguageFile "${NSISDIR}\Contrib\Language files\English.nlf"

VIProductVersion "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "${NAME}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "VcXsrv windows xserver"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductVersion" "${VERSION}"

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

;--------------------------------
; The stuff to install
Section "VcXsrv debug exe and dlls (required)"

  SetShellVarContext All

  SectionIn RO
  SectionIn 1 2 3

  SetRegView 64

  ; Set output path to the installation directory.
  SetOutPath $INSTDIR

  ; Remove old opengl32.dll file if it extits
  IfFileExists "$INSTDIR\opengl32.dll" 0 +2
    Delete "$INSTDIR\opengl32.dll"
  IfFileExists "$INSTDIR\msvcr100d.dll" 0 +2
    Delete "$INSTDIR\msvcr100d.dll"
  IfFileExists "$INSTDIR\msvcp100d.dll" 0 +2
    Delete "$INSTDIR\msvcp100d.dll"
  IfFileExists "$INSTDIR\msvcr110d.dll" 0 +2
    Delete "$INSTDIR\msvcr110d.dll"
  IfFileExists "$INSTDIR\msvcp110d.dll" 0 +2
    Delete "$INSTDIR\msvcp110d.dll"
  IfFileExists "$INSTDIR\msvcr120d.dll" 0 +2
    Delete "$INSTDIR\msvcr120d.dll"
  IfFileExists "$INSTDIR\msvcp120d.dll" 0 +2
    Delete "$INSTDIR\msvcp120d.dll"

  ; Put files there
  File "..\obj64\servdebug\vcxsrv.exe"
  File "..\dix\protocol.txt"
  File "..\system.XWinrc"
  File "..\X0.hosts"
  File "..\..\xkbcomp\obj64\debug\xkbcomp.exe"
  File "..\..\apps\xhost\obj64\debug\xhost.exe"
  File "..\..\apps\xrdb\obj64\debug\xrdb.exe"
  File "..\..\apps\xauth\obj64\debug\xauth.exe"
  File "..\..\apps\xcalc\obj64\debug\xcalc.exe"
  File "..\..\apps\xcalc\app-defaults\xcalc"
  File "..\..\apps\xcalc\app-defaults\xcalc-color"
  File "..\..\apps\xclock\obj64\debug\xclock.exe"
  File "..\..\apps\xclock\app-defaults\xclock"
  File "..\..\apps\xclock\app-defaults\xclock-color"
  File "..\..\apps\xwininfo\obj64\debug\xwininfo.exe"
  File "..\XKeysymDB"
  File "..\..\libX11\src\XErrorDB"
  File "..\..\libX11\src\xcms\Xcms.txt"
  File "..\XtErrorDB"
  File "..\font-dirs"
  File "..\.Xdefaults"
  File "..\hw\xwin\xlaunch\obj64\debug\xlaunch.exe"
  File "..\..\tools\plink\obj64\debug\plink.exe"
  File "..\..\mesalib\src\obj64\debug\swrast_dri.dll"
  File "..\hw\xwin\swrastwgl_dri\obj64\debug\swrastwgl_dri.dll"
  File "..\..\dxtn\obj64\debug\dxtn.dll"
  File "..\..\libxml2\bin64\libxml2-2.dll"
  File "..\..\libxml2\bin64\libgcc_s_sjlj-1.dll"
  File "..\..\libxml2\bin64\libiconv-2.dll"
  File "..\..\libxml2\bin64\libwinpthread-1.dll"
  File "..\..\zlib\obj64\debug\zlib1.dll"
  File "..\..\libxcb\src\obj64\debug\libxcb.dll"
  File "..\..\libXau\obj64\debug\libXau.dll"
  File "..\..\libX11\obj64\debug\libX11.dll"
  File "..\..\libXext\src\obj64\debug\libXext.dll"
  File "..\..\libXmu\src\obj64\debug\libXmu.dll"
  File "..\..\openssl\debug64\libcrypto-3-x64.dll"
  File "..\..\freetype\objs\x64\Debug\freetype.dll"
  File "vcruntime140d.dll"
  File "vcruntime140_1d.dll"
  File "msvcp140d.dll"
  SetOutPath $INSTDIR\xkbdata
  File /r "..\xkbdata\*.*"
  SetOutPath $INSTDIR\locale
  File /r "..\locale\*.*"
  SetOutPath $INSTDIR\bitmaps
  File /r "..\bitmaps\*.*"

  ; Write the installation path into the registry
  WriteRegStr HKLM SOFTWARE\VcXsrv "Install_Dir_64" "$INSTDIR"

  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VcXsrv" "DisplayIcon" "$INSTDIR\vcxsrv.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VcXsrv" "DisplayName" "${NAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VcXsrv" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VcXsrv" "Publisher" "https://github.com/marchaesen"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VcXsrv" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VcXsrv" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VcXsrv" "NoRepair" 1
  WriteUninstaller "uninstall.exe"

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VcXsrv" "EstimatedSize" "$0"

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

SectionEnd

; Optional section (can be disabled by the user)
Section "Fonts"
  SectionIn 1 3

  SetShellVarContext All

  SetRegView 64

  SetOutPath $INSTDIR\fonts
  CreateDirectory "$INSTDIR\fonts"
  File /r "..\fonts\*.*"

SectionEnd

; Optional section (can be disabled by the user)
Section "Start Menu Shortcuts"
  SectionIn 1 3

  SetShellVarContext All

  SetRegView 64

  SetOutPath "$SMPROGRAMS\VcXsrv"
  CreateDirectory "$SMPROGRAMS\VcXsrv"
  CreateShortCut "$SMPROGRAMS\VcXsrv\Uninstall VcXsrv.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
  CreateShortCut "$SMPROGRAMS\VcXsrv\XLaunch.lnk" "$INSTDIR\xlaunch.exe" "" "$INSTDIR\xlaunch.exe" 0
SectionEnd

; Optional section (can be disabled by the user)
Section "Desktop Shortcuts"
  SectionIn 1 3

  SetShellVarContext All

  SetRegView 64

  SetOutPath $DESKTOP
  CreateShortCut "$DESKTOP\XLaunch.lnk" "$INSTDIR\xlaunch.exe" "" "$INSTDIR\xlaunch.exe" 0

SectionEnd

;--------------------------------

; Uninstaller

Section "Uninstall"

  SetRegView 64

  SetShellVarContext All

  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VcXsrv"
  DeleteRegKey HKLM SOFTWARE\VcXsrv

  ; Register the xlaunch file extension
  DeleteRegKey HKCR ".xlaunch"
  DeleteRegKey HKCR "XLaunchFile"
  DeleteRegKey HKCR "Applications\xlaunch.exe"

  ; Remove files and uninstaller
  Delete "$INSTDIR\vcxsrv.exe"
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
  Delete "$INSTDIR\Xcms.txt"
  Delete "$INSTDIR\XtErrorDB"
  Delete "$INSTDIR\font-dirs"
  Delete "$INSTDIR\.Xdefaults"
  Delete "$INSTDIR\xlaunch.exe"
  Delete "$INSTDIR\plink.exe"
  Delete "$INSTDIR\swrast_dri.dll"
  Delete "$INSTDIR\dxtn.dll"
  Delete "$INSTDIR\swrastwgl_dri.dll"
  Delete "$INSTDIR\libxcb.dll"
  Delete "$INSTDIR\libXau.dll"
  Delete "$INSTDIR\libX11.dll"
  Delete "$INSTDIR\libXext.dll"
  Delete "$INSTDIR\libXmu.dll"
  Delete "$INSTDIR\libxml2.dll"
  Delete "$INSTDIR\zlib1.dll"
  Delete "$INSTDIR\iconv.dll"
  Delete "$INSTDIR\vcruntime140.dll"
  Delete "$INSTDIR\vcruntime140_1.dll"
  Delete "$INSTDIR\msvcp140.dll"
  Delete "$INSTDIR\vcruntime140d.dll"
  Delete "$INSTDIR\vcruntime140_1d.dll"
  Delete "$INSTDIR\msvcp140d.dll"
  Delete "$INSTDIR\libgcc_s_sjlj-1.dll"
  Delete "$INSTDIR\libcrypto-1_1-x64.dll"
  Delete "$INSTDIR\libiconv-2.dll"
  Delete "$INSTDIR\libwinpthread-1.dll"
  Delete "$INSTDIR\libxml2-2.dll"
  Delete "$INSTDIR\X0.hosts"
  Delete "$INSTDIR\xauth.exe"
  Delete "$INSTDIR\xhost.exe"
  Delete "$INSTDIR\xrdb.exe"


  RMDir /r "$INSTDIR\fonts"
  RMDir /r "$INSTDIR\xkbdata"
  RMDir /r "$INSTDIR\locale"
  RMDir /r "$INSTDIR\bitmaps"

  ; Remove shortcuts, if any
  Delete "$SMPROGRAMS\VcXsrv\*.*"
  Delete "$DESKTOP\VcXsrv.lnk"
  Delete "$DESKTOP\XLaunch.lnk"

  ; Remove directories used
  RMDir "$SMPROGRAMS\VcXsrv"
  RMDir "$INSTDIR"

SectionEnd
