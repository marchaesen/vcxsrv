#!/bin/sh
rm -rf /mnt/c/src/release
mkdir -p /mnt/c/src/release
find . | grep "\(vcxsrv\(-.*installer\)\?\.exe\|\.dll\)$" | grep -v debug | xargs -i cp {} /mnt/c/src/release
