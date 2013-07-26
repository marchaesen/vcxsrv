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
OutFile "vcxsrv-debug.1.14.2.1.installer.exe"

; The default installation directory
InstallDir $PROGRAMFILES32\VcXsrv

; Registry key to check for directory (so if you install again, it will
; overwrite the old one automatically)
InstallDirRegKey HKLM SOFTWARE\VcXsrv "Install_Dir"

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
  File "..\..\mesalib\windows\VC8\mesa\Win32\Debug\swrast_dri.dll"
  File "..\hw\xwin\swrastwgl_dri\obj\debug\swrastwgl_dri.dll"
  File "..\..\dxtn\obj\debug\dxtn.dll"
  File "..\..\zlib\obj\debug\zlib1.dll"
  File "..\..\libxcb\src\obj\debug\libxcb.dll"
  File "..\..\libXau\obj\debug\libXau.dll"
  File "..\..\libX11\obj\debug\libX11.dll"
  File "..\..\libXext\src\obj\debug\libXext.dll"
  File "..\..\libXmu\src\obj\debug\libXmu.dll"
  File "msvcr100d.dll"
  File "msvcp100d.dll"

  WriteRegStr HKLM SOFTWARE\VcXsrv "Install_Dir" "$INSTDIR"
SectionEnd
