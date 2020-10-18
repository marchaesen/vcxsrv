#!/bin/sh

set -ex

mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev || echo possibly already mounted
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /tmp

. /set-job-env-vars.sh

# Store Mesa's disk cache under /tmp, rather than sending it out over NFS.
export XDG_CACHE_HOME=/tmp

echo "nameserver 8.8.8.8" > /etc/resolv.conf

# Not all DUTs have network
sntp -sS pool.ntp.org || true

# Start a little daemon to capture the first devcoredump we encounter.  (They
# expire after 5 minutes, so we poll for them).
./capture-devcoredump.sh &

if sh $BARE_METAL_TEST_SCRIPT; then
  OK=1
else
  OK=0
fi

# upload artifacts via webdav
WEBDAV=$(cat /proc/cmdline | tr " " "\n" | grep webdav | cut -d '=' -f 2 || true)
if [ -n "$WEBDAV" ]; then
  find /results -type f -exec curl -T {} $WEBDAV/{} \;
fi

if [ $OK -eq 1 ]; then
    echo "bare-metal result: pass"
else
    echo "bare-metal result: fail"
fi

# Wait until the job would have timed out anyway, so we don't spew a "init
# exited" panic.
sleep 6000
