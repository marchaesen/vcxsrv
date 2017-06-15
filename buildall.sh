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

# c;\perl should have a copy of strawberry perl portable edition
ORIPATH=$PATH
export PATH=/cygdrive/c/perl/site/bin:/cygdrive/c/perl/perl/bin:/cygdrive/c/perl/bin:$PATH

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

NOASM=no-asm

if [[ ! -d "release32" ]]; then
  mkdir release32
fi
cd release32

perl ../Configure VC-WIN32 $NOASM --release
check-error 'Error executing perl'
else

if [[ ! -d "release64" ]]; then
  mkdir release64
fi
cd release64

perl ../Configure VC-WIN64A --release
check-error 'Error executing perl'
fi

nmake
check-error 'Error compiling openssl for release'

cd ..

if [[ "$IS64" == "" ]]; then

if [[ ! -d "debug32" ]]; then
  mkdir debug32
fi
cd debug32

perl ../Configure VC-WIN32 $NOASM --debug
check-error 'Error executing perl'
else

if [[ ! -d "debug64" ]]; then
  mkdir debug64
fi
cd debug64

perl ../Configure VC-WIN64A --debug
check-error 'Error executing perl'
fi

nmake
check-error 'Error compiling openssl for debug'

cd ../../pthreads
nmake VC-static
check-error 'Error compiling pthreads for release'

nmake VC-static-debug
check-error 'Error compiling pthreads for debug'

cd ..

#reuse the cygwin perl again
export PATH=$ORIPATH

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

