#!/bin/sh

# Very early init, used to make sure devices and network are set up and
# reachable.

set -ex

cd /

mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev || echo possibly already mounted
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /tmp

echo "nameserver 8.8.8.8" > /etc/resolv.conf
[ -z "$NFS_SERVER_IP" ] || echo "$NFS_SERVER_IP caching-proxy" >> /etc/hosts

# Set the time so we can validate certificates before we fetch anything;
# however as not all DUTs have network, make this non-fatal.
for i in 1 2 3; do sntp -sS pool.ntp.org && break || sleep 2; done || true
