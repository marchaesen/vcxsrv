#!/bin/bash

if [ "${1}" == "1" ]; then
    export IS64=1
elif [ "${1}" == "0" ]; then
    export IS64=0
else
    echo "Please pass 1 (64-bit compilation) or 0 (32-bit compilation) as first argument"
    exit 1
fi

export MHMAKEPATH
if [ "${BUILDDEBUG}" == "1" ] ; then
    MHMAKEPATH="tools/mhmake/Debug"
fi
if [ "${BUILDRELEASE}" == "1" ] ; then
    MHMAKEPATH="tools/mhmake/Release"
fi
if [ "${IS64}" == "1" ]; then
    MHMAKEPATH="${MHMAKEPATH}64"
fi

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

rm -f commands.sh
python setenv.py $1 > commands.sh
chmod +x commands.sh
source commands.sh

if [ -z "${CYGWIN}" ]; then
    # WSL build.
    export PATH=/usr/local/bin:/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games:/usr/local/games:/usr/lib/wsl/lib:${DIR}/${MHMAKEPATH}:/mnt/c/nasm:$PATH:/mnt/c/gnuwin32/bin:/mnt/c/perl/perl/bin
    export MHMAKECONF=${DIR}
    export PYTHON3=/mnt/c/Python39/python.exe
    export WSLENV="${WSLENV}:MHMAKECONF/l:PYTHON3/l:IS64/l:CFLAGS/l:TERM"
else
    # Docker cygwin build - $PATH is setup in Dockerfile so no need to set it here.
    export PATH=${DIR}/${MHMAKEPATH}:$PATH
    export MHMAKECONF=$(cygpath -w ${DIR})
    export PYTHON3=python
fi
rm -f commands.sh
export CFLAGS="-FS"

