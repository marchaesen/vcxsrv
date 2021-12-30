#!/bin/sh

set -ex

# This script can be called concurrently, pass arguments and env in a per-instance tmp dir
export DEQP_TEMP_DIR=`mktemp -d /tmp.XXXXXXXXXX`

# The dEQP binary needs to run from the directory it's in
if [ -z "${1##*"deqp"*}" ]; then
  PWD=`dirname $1`
fi

export -p > $DEQP_TEMP_DIR/crosvm-env.sh

CROSVM_KERNEL_ARGS="console=null root=my_root rw rootfstype=virtiofs init=$CI_PROJECT_DIR/install/crosvm-init.sh ip=192.168.30.2::192.168.30.1:255.255.255.0:crosvm:eth0 -- $DEQP_TEMP_DIR"

echo $@ > $DEQP_TEMP_DIR/crosvm-script.sh

unset DISPLAY
unset XDG_RUNTIME_DIR

/usr/sbin/iptables-legacy -w -t nat -A POSTROUTING -o eth0 -j MASQUERADE
echo 1 > /proc/sys/net/ipv4/ip_forward

# Send output from guest to host
touch $DEQP_TEMP_DIR/stderr $DEQP_TEMP_DIR/stdout
tail -f $DEQP_TEMP_DIR/stderr > /dev/stderr &
ERR_TAIL_PID=$!
tail -f $DEQP_TEMP_DIR/stdout > /dev/stdout &
OUT_TAIL_PID=$!

trap "exit \$exit_code" INT TERM
trap "exit_code=\$?; kill $ERR_TAIL_PID $OUT_TAIL_PID" EXIT

# We aren't testing LLVMPipe here, so we don't need to validate NIR on the host
NIR_DEBUG="novalidate" LIBGL_ALWAYS_SOFTWARE="true" GALLIUM_DRIVER="$CROSVM_GALLIUM_DRIVER" stdbuf -oL crosvm run \
  --gpu "$CROSVM_GPU_ARGS" \
  -m 4096 \
  -c 2 \
  --disable-sandbox \
  --shared-dir /:my_root:type=fs:writeback=true:timeout=60:cache=always \
  --host_ip=192.168.30.1 --netmask=255.255.255.0 --mac "AA:BB:CC:00:00:12" \
  -p "$CROSVM_KERNEL_ARGS" \
  /lava-files/bzImage >> $DEQP_TEMP_DIR/stderr > /dev/null

exit `cat $DEQP_TEMP_DIR/exit_code`
