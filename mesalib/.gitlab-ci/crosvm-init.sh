#!/bin/sh

set -e

export DEQP_TEMP_DIR=$1

mount -t proc none /proc
mount -t sysfs none /sys
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /tmp

. $DEQP_TEMP_DIR/crosvm-env.sh

cd $PWD

dmesg --level crit,err,warn -w >> $DEQP_TEMP_DIR/stderr &

set +e
stdbuf -oL sh $DEQP_TEMP_DIR/crosvm-script.sh 2>> $DEQP_TEMP_DIR/stderr >> $DEQP_TEMP_DIR/stdout
echo $? > $DEQP_TEMP_DIR/exit_code
set -e

sync
sleep 1

poweroff -d -n -f || true

sleep 1   # Just in case init would exit before the kernel shuts down the VM
