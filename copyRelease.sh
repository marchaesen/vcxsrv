#!/bin/sh
rm -rf /cygdrive/c/src/release
mkdir -p /cygdrive/c/src/release
find . | grep "\(vcxsrv\(-.*installer\)\?\.exe\|\.dll\)$" | grep -v debug | xargs -i cp {} /cygdrive/c/src/release
