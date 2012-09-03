nasm -h > nul 2>&1
if %errorlevel% NEQ 0 goto nasmerror

echo on

devenv.com freetype\freetypevc10.sln /build "Release Multithreaded|Win32"
if %errorlevel% NEQ 0 goto end
devenv.com freetype\freetypevc10.sln /build "Debug Multithreaded|Win32"
if %errorlevel% NEQ 0 goto end
cd openssl
perl Configure VC-WIN32
if %errorlevel% NEQ 0 goto end
call ms\do_nasm.bat
if %errorlevel% NEQ 0 goto end
nmake -f ms\nt.mak
if %errorlevel% NEQ 0 goto end
nmake DEBUG=1 -f ms\nt.mak
if %errorlevel% NEQ 0 goto end
cd ..\pthreads
nmake VC-static
if %errorlevel% NEQ 0 goto end
nmake VC-static-debug
if %errorlevel% NEQ 0 goto end
cd ..
devenv.com tools\mhmake\mhmakevc10.sln /build "Release|Win32"
if %errorlevel% NEQ 0 goto end
devenv.com tools\mhmake\mhmakevc10.sln /build "Debug|Win32"
if %errorlevel% NEQ 0 goto end

set MHMAKECONF=%~dp0

tools\mhmake\release\mhmake.exe -C xorg-server MAKESERVER=1 DEBUG=1 vcxsrv_dbg.exe
if %errorlevel% NEQ 0 goto end
tools\mhmake\release\mhmake.exe -C xorg-server MAKESERVER=1
if %errorlevel% NEQ 0 goto end

cd xorg-server\installer
call packageall.bat

goto end

:nasmerror
echo Please put nasm in the path
:end