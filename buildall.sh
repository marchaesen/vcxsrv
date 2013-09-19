#!/usr/bin/bash

function check-error {
    if [ $? -ne 0 ]; then
        echo $1
        exit
    fi
}

which nasm > /dev/null 2>&1
check-error 'Please install nasm'

which MSBuild.exe > /dev/null 2>&1
check-error 'Please install/set environment for visual studio 2010'

# echo script lines from now one
#set -v

if [[ "$IS64" == "" ]]; then
MSBuild.exe freetype/freetypevc10.sln /t:Build /p:Configuration="Release Multithreaded" /p:Platform=Win32
check-error 'Error compiling freetype'
MSBuild.exe freetype/freetypevc10.sln /t:Build /p:Configuration="Debug Multithreaded" /p:Platform=Win32
check-error 'Error compiling freetype'
else
MSBuild.exe freetype/freetypevc10.sln /t:Build /p:Configuration="Release Multithreaded" /p:Platform=x64
check-error 'Error compiling freetype'
MSBuild.exe freetype/freetypevc10.sln /t:Build /p:Configuration="Debug Multithreaded" /p:Platform=x64
check-error 'Error compiling freetype'
fi



cd openssl

if [[ "$IS64" == "" ]]; then
perl Configure VC-WIN32
check-error 'Error executing perl'
ms/do_nasm.bat
check-error 'Error configuring openssl for nasm'
else
perl Configure VC-WIN64A
check-error 'Error executing perl'
ms/do_win64a.bat
check-error 'Error configuring openssl for nasm'
fi

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

MSBuild.exe tools/mhmake/mhmakevc10.sln /t:Build /p:Configuration=Release /p:Platform=Win32
check-error 'Error compiling mhmake for release'

MSBuild.exe tools/mhmake/mhmakevc10.sln /t:Build /p:Configuration=Debug /p:Platform=Win32
check-error 'Error compiling mhmake for debug'

export MHMAKECONF=`cygpath -da .`

tools/mhmake/release/mhmake $PARBUILD -C xorg-server MAKESERVER=1 DEBUG=1
check-error 'Error compiling vcxsrv for debug'

tools/mhmake/release/mhmake.exe $PARBUILD -C xorg-server MAKESERVER=1
check-error 'Error compiling vcxsrv for release'

cd xorg-server/installer
./packageall.bat

