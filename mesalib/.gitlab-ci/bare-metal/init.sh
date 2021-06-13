#!/bin/sh

set -ex

mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev || echo possibly already mounted
mkdir -p /dev/pts
mount -t devpts devpts /dev/pts
mount -t tmpfs tmpfs /tmp

. /set-job-env-vars.sh

[ -z "$BM_KERNEL_MODULES" ] || echo -n $BM_KERNEL_MODULES | xargs -d, -n1 /usr/sbin/modprobe

# Store Mesa's disk cache under /tmp, rather than sending it out over NFS.
export XDG_CACHE_HOME=/tmp

echo "nameserver 8.8.8.8" > /etc/resolv.conf

# Not all DUTs have network
sntp -sS pool.ntp.org || true

# Start a little daemon to capture the first devcoredump we encounter.  (They
# expire after 5 minutes, so we poll for them).
./capture-devcoredump.sh &

# If we want Xorg to be running for the test, then we start it up before the
# BARE_METAL_TEST_SCRIPT because we need to use xinit to start X (otherwise
# without using -displayfd you can race with Xorg's startup), but xinit will eat
# your client's return code
if [ -n "$BM_START_XORG" ]; then
  echo "touch /xorg-started; sleep 100000" > /xorg-script
  env \
    LD_LIBRARY_PATH=/install/lib/ \
    LIBGL_DRIVERS_PATH=/install/lib/dri/ \
    xinit /bin/sh /xorg-script -- /usr/bin/Xorg -noreset -dpms -logfile /Xorg.0.log &

  # Wait for xorg to be ready for connections.
  for i in 1 2 3 4 5; do
    if [ -e /xorg-started ]; then
      break
    fi
    sleep 5
  done
  export DISPLAY=:0
fi

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
