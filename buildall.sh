#!/usr/bin/bash

function check-error {
    if [ $? -ne 0 ]; then
        echo $1
        exit
    fi
}

which nasm > /dev/null 2>&1
check-error 'Please install nasm'

which devenv.com > /dev/null 2>&1
check-error 'Please install/set environment for visual studio 2010'

# echo script lines from now one
#set -v

devenv.com freetype/freetypevc10.sln /build "Release Multithreaded|Win32"
check-error 'Error compiling freetype'

devenv.com freetype/freetypevc10.sln /build "Debug Multithreaded|Win32"
check-error 'Error compiling freetype'

cd openssl

perl Configure VC-WIN32
check-error 'Error executing perl'

ms/do_nasm.bat
check-error 'Error configuring openssl for nasm'

nmake -f ms/nt.mak
check-error 'Error compiling openssl for release'

nmake DEBUG=1 -f ms/nt.mak
check-error 'Error compiling openssl for debug'

cd ../pthreads
nmake VC-static
check-error 'Error compiling pthreads for release'

nmake VC-static-debug
check-error 'Error compiling pthreads for debug'

cd ..

devenv.com tools/mhmake/mhmakevc10.sln /build "Release|Win32"
check-error 'Error compiling mhmake for release'

devenv.com tools/mhmake/mhmakevc10.sln /build "Debug|Win32"
check-error 'Error compiling mhmake for debug'

export MHMAKECONF=`cygpath -da .`

tools/mhmake/release/mhmake -C xorg-server MAKESERVER=1 DEBUG=1
check-error 'Error compiling vcxsrv for debug'

tools/mhmake/release/mhmake.exe -C xorg-server MAKESERVER=1
check-error 'Error compiling vcxsrv for release'

cd xorg-server/installer
./packageall.bat

