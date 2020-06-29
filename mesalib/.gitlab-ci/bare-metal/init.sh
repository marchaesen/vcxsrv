#!/bin/sh

set -ex

mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev || echo possibly already mounted
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts

. /set-job-env-vars.sh

echo "nameserver 8.8.8.8" > /etc/resolv.conf

if sh $BARE_METAL_TEST_SCRIPT; then
    echo "bare-metal result: pass"
else
    echo "bare-metal result: fail"
fi

# Wait until the job would have timed out anyway, so we don't spew a "init
# exited" panic.
sleep 6000
