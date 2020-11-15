#!/bin/bash

_mhmake=`which mhmake`
if [ ! -x "$_mhmake" ] ; then
  DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
  export PATH=$DIR/tools/mhmake/Release64:$PATH
fi

rm -f commands.sh
python setenv.py $1 > commands.sh
chmod +x commands.sh
source commands.sh
rm -f commands.sh
if [[ "$MHMAKECONF" == "" ]] ; then
  export MHMAKECONF=`cygpath -w $DIR`
  export PYTHON3=c:\\Python39\\python.exe
fi

export IS64=$1

