#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

rm -f commands.sh
python setenv.py $1 > commands.sh
chmod +x commands.sh
source commands.sh
export PATH=/usr/local/bin:/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games:/usr/local/games:/usr/lib/wsl/lib:$DIR/tools/mhmake/Release64:/mnt/c/nasm:$PATH:/mnt/c/gnuwin32/bin:/mnt/c/perl/perl/bin
rm -f commands.sh
export MHMAKECONF=$DIR
export PYTHON3=/mnt/c/Python39/python.exe
export IS64=$1

export CC="cl -FS"
export WSLENV="$WSLENV:MHMAKECONF/l:PYTHON3/l:IS64/l:CC/l"
