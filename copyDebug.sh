#!/bin/sh
rm -rf /mnt/c/src/debug
mkdir -p /mnt/c/src/debug
find . | grep "\(vcxsrv\(-.*-debug.*installer\)\?\.exe\|\.dll\|\.pdb\)$" | xargs -i cp {} /mnt/c/src/debug/
