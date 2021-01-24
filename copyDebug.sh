#!/bin/sh
rm -rf /cygdrive/c/src/debug
mkdir -p /cygdrive/c/src/debug
find . | grep "\(vcxsrv\(-.*-debug.*installer\)\?\.exe\|\.dll\|\.pdb\)$" | xargs -i cp {} /cygdrive/c/src/debug/
