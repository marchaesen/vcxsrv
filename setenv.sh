#!/bin/bash

if [[ "$1" == "1" ]] ; then
    export IS64=1
    relese="Release64"
else
    export IS64=0
    relese="Release"
fi

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

rm -f commands.sh
python setenv.py $1 > commands.sh
chmod +x commands.sh
source commands.sh
if [[ "X$(uname -o)" -ne "XCygwin" ]]; then
    export PATH=/usr/local/bin:/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games:/usr/local/games:/usr/lib/wsl/lib:${DIR}/tools/mhmake/${release}:/mnt/c/nasm:$PATH:/mnt/c/gnuwin32/bin:/mnt/c/perl/perl/bin
else
    export PATH=${DIR}/tools/mhmake/${release}:$PATH
    if [[ "X${MHMAKECONF}" == "X" ]] ; then
        export MHMAKECONF=$(cygpath -w ${DIR})
        export PYTHON3=c:\\Python39\\python.exe
    fi
fi
rm -f commands.sh
export MHMAKECONF=${DIR}
export PYTHON3=/mnt/c/Python39/python.exe
export IS64=$1

export CFLAGS="-FS"
export WSLENV="${WSLENV}:MHMAKECONF/l:PYTHON3/l:IS64/l:CFLAGS/l"
