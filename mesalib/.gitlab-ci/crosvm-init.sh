#!/bin/sh

set -e

export DEQP_TEMP_DIR="$1"

mount -t proc none /proc
mount -t sysfs none /sys
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /tmp

. $DEQP_TEMP_DIR/crosvm-env.sh

# .gitlab-ci.yml script variable is using relative paths to install directory,
# so change to that dir before running `crosvm-script`
cd "${CI_PROJECT_DIR}"

# The exception is the dEQP binary, since it needs to run from the directory
# it's in
if [ -d "${DEQP_BIN_DIR}" ]
then
    cd "${DEQP_BIN_DIR}"
fi

dmesg --level crit,err,warn -w >> $DEQP_TEMP_DIR/stderr &

set +e
stdbuf -oL sh $DEQP_TEMP_DIR/crosvm-script.sh 2>> $DEQP_TEMP_DIR/stderr >> $DEQP_TEMP_DIR/stdout
echo $? > $DEQP_TEMP_DIR/exit_code
set -e

sync
sleep 1

poweroff -d -n -f || true

sleep 1   # Just in case init would exit before the kernel shuts down the VM
