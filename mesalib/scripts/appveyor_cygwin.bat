set PKGCACHE=C:\pkgcache
set CYGWIN_MIRROR=http://cygwin.mirror.constant.com

if _%arch%_ == _x64_ set SETUP=setup-x86_64.exe && set CYGWIN_ROOT=C:\cygwin64
if _%arch%_ == _x86_ set SETUP=setup-x86.exe && set CYGWIN_ROOT=C:\cygwin

set PATH=%CYGWIN_ROOT%\bin;%SYSTEMROOT%\system32

goto %1

:install
echo Updating Cygwin and installing build prerequsites
%CYGWIN_ROOT%\%SETUP% -qnNdO -R "%CYGWIN_ROOT%" -s "%CYGWIN_MIRROR%" -l "%PKGCACHE%" -g -P ^
bison,^
ccache,^
flex,^
glproto,^
libX11-devel,^
libX11-xcb-devel,^
libXdamage-devel,^
libXext-devel,^
libXfixes-devel,^
libexpat-devel,^
libllvm-devel,^
libxcb-dri2-devel,^
libxcb-glx-devel,^
libxcb-xfixes-devel,^
meson,^
ninja,^
python3-mako,^
zlib-devel
goto :eof

:build_script
bash -lc "cd $APPVEYOR_BUILD_FOLDER; meson _build -Degl=false --wrap-mode=nofallback && ninja -C _build"
goto :eof

:after_build
bash -lc "cd $APPVEYOR_BUILD_FOLDER; ninja -C _build test"
goto :eof
