/*  This file is part of vcxsrv.
 *
 *  Copyright (C) 2014 marha@users.sourceforge.net
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
!define NAME "VcXsrv"
!define VERSION "1.20.14.0"

; The name of the installer
Name "${NAME}"

; The file to write
OutFile "vcxsrv-debug.${VERSION}.installer.exe"

; The default installation directory
InstallDir $PROGRAMFILES32\VcXsrv

SetCompressor /SOLID lzma

; Registry key to check for directory (so if you install again, it will
; overwrite the old one automatically)
InstallDirRegKey HKLM SOFTWARE\VcXsrv "Install_Dir"

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
Section "VcXsrv debug exe and dlls"

  SectionIn RO
  SectionIn 1

  ; Set output path to the installation directory.
  SetOutPath $INSTDIR

  IfFileExists "$INSTDIR\msvcr100d.dll" 0 +2
    Delete "$INSTDIR\msvcr100d.dll"
  IfFileExists "$INSTDIR\msvcp100d.dll" 0 +2
    Delete "$INSTDIR\msvcp100d.dll"
  IfFileExists "$INSTDIR\msvcr110d.dll" 0 +2
    Delete "$INSTDIR\msvcr110d.dll"
  IfFileExists "$INSTDIR\msvcp110d.dll" 0 +2
    Delete "$INSTDIR\msvcp110d.dll"
  IfFileExists "$INSTDIR\msvcr120.dll" 0 +2
    Delete "$INSTDIR\msvcr120.dll"
  IfFileExists "$INSTDIR\msvcp120.dll" 0 +2
    Delete "$INSTDIR\msvcp120.dll"

  ; Put files there
  File "..\obj\servdebug\vcxsrv.exe"
  File "..\..\xkbcomp\obj\debug\xkbcomp.exe"
  File "..\..\apps\xhost\obj\debug\xhost.exe"
  File "..\..\apps\xrdb\obj\debug\xrdb.exe"
  File "..\..\apps\xauth\obj\debug\xauth.exe"
  File "..\..\apps\xcalc\obj\debug\xcalc.exe"
  File "..\..\apps\xclock\obj\debug\xclock.exe"
  File "..\..\apps\xwininfo\obj\debug\xwininfo.exe"
  File "..\hw\xwin\xlaunch\obj\debug\xlaunch.exe"
  File "..\..\tools\plink\obj\debug\plink.exe"
  File "..\..\mesalib\src\obj\debug\swrast_dri.dll"
  File "..\hw\xwin\swrastwgl_dri\obj\debug\swrastwgl_dri.dll"
  File "..\..\dxtn\obj\debug\dxtn.dll"
  File "..\..\zlib\obj\debug\zlib1.dll"
  File "..\..\libxcb\src\obj\debug\libxcb.dll"
  File "..\..\libXau\obj\debug\libXau.dll"
  File "..\..\libX11\obj\debug\libX11.dll"
  File "..\..\libXext\src\obj\debug\libXext.dll"
  File "..\..\libXmu\src\obj\debug\libXmu.dll"
  File "..\..\openssl\debug32\libcrypto-1_1.dll"
  File "vcruntime140d.dll"
  File "msvcp140d.dll"

  WriteRegStr HKLM SOFTWARE\VcXsrv "Install_Dir" "$INSTDIR"
SectionEnd
