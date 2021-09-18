#!/bin/sh

set -ex

mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev || echo possibly already mounted
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /tmp

. /crosvm-env.sh

# / is ro
export PIGLIT_REPLAY_EXTRA_ARGS="$PIGLIT_REPLAY_EXTRA_ARGS --db-path /tmp/replayer-db"

if sh $CROSVM_TEST_SCRIPT; then
    touch /results/success
fi

sleep 5   # Leave some time to get the last output flushed out

poweroff -d -n -f || true

sleep 10   # Just in case init would exit before the kernel shuts down the VM

exit 1
