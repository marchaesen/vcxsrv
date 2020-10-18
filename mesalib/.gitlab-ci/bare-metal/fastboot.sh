#!/bin/bash

BM=$CI_PROJECT_DIR/install/bare-metal

if [ -z "$BM_SERIAL" -a -z "$BM_SERIAL_SCRIPT" ]; then
  echo "Must set BM_SERIAL OR BM_SERIAL_SCRIPT in your gitlab-runner config.toml [[runners]] environment"
  echo "BM_SERIAL:"
  echo "  This is the serial device to talk to for waiting for fastboot to be ready and logging from the kernel."
  echo "BM_SERIAL_SCRIPT:"
  echo "  This is a shell script to talk to for waiting for fastboot to be ready and logging from the kernel."
  exit 1
fi

if [ -z "$BM_POWERUP" ]; then
  echo "Must set BM_POWERUP in your gitlab-runner config.toml [[runners]] environment"
  echo "This is a shell script that should reset the device and begin its boot sequence"
  echo "such that it pauses at fastboot."
  exit 1
fi

if [ -z "$BM_POWERDOWN" ]; then
  echo "Must set BM_POWERDOWN in your gitlab-runner config.toml [[runners]] environment"
  echo "This is a shell script that should power off the device."
  exit 1
fi

if [ -z "$BM_FASTBOOT_SERIAL" ]; then
  echo "Must set BM_FASTBOOT_SERIAL in your gitlab-runner config.toml [[runners]] environment"
  echo "This must be the a stable-across-resets fastboot serial number."
  exit 1
fi

if [ -z "$BM_KERNEL" ]; then
  echo "Must set BM_KERNEL to your board's kernel vmlinuz or Image.gz in the job's variables:"
  exit 1
fi

if [ -z "$BM_DTB" ]; then
  echo "Must set BM_DTB to your board's DTB file in the job's variables:"
  exit 1
fi

if [ -z "$BM_ROOTFS" ]; then
  echo "Must set BM_ROOTFS to your board's rootfs directory in the job's variables:"
  exit 1
fi

if [ -z "$BM_WEBDAV_IP" -o -z "$BM_WEBDAV_PORT" ]; then
  echo "BM_WEBDAV_IP and/or BM_WEBDAV_PORT is not set - no results will be uploaded from DUT!"
  WEBDAV_CMDLINE=""
else
  WEBDAV_CMDLINE="webdav=http://$BM_WEBDAV_IP:$BM_WEBDAV_PORT"
fi

set -ex

# Clear out any previous run's artifacts.
rm -rf results/
mkdir -p results

# Create the rootfs in a temp dir
rsync -a --delete $BM_ROOTFS/ rootfs/
. $BM/rootfs-setup.sh rootfs

# Finally, pack it up into a cpio rootfs.  Skip the vulkan CTS since none of
# these devices use it and it would take up space in the initrd.
pushd rootfs
find -H | \
  egrep -v "external/(openglcts|vulkancts|amber|glslang|spirv-tools)" |
  egrep -v "traces-db|apitrace|renderdoc|python" | \
  cpio -H newc -o | \
  xz --check=crc32 -T4 - > $CI_PROJECT_DIR/rootfs.cpio.gz
popd

# Make the combined kernel image and dtb for passing to fastboot.  For normal
# Mesa development, we build the kernel and store it in the docker container
# that this script is running in.
#
# However, container builds are expensive, so when you're hacking on the
# kernel, it's nice to be able to skip the half hour container build and plus
# moving that container to the runner.  So, if BM_KERNEL+BM_DTB are URLs,
# fetch them instead of looking in the container.
if echo "$BM_KERNEL $BM_DTB" | grep -q http; then
  apt install -y wget

  wget $BM_KERNEL -O kernel
  wget $BM_DTB -O dtb

  cat kernel dtb > Image.gz-dtb
  rm kernel dtb
else
  cat $BM_KERNEL $BM_DTB > Image.gz-dtb
fi

abootimg \
  --create artifacts/fastboot.img \
  -k Image.gz-dtb \
  -r rootfs.cpio.gz \
  -c cmdline="$BM_CMDLINE $WEBDAV_CMDLINE"
rm Image.gz-dtb

# Start nginx to get results from DUT
if [ -n "$WEBDAV_CMDLINE" ]; then
  ln -s `pwd`/results /results
  sed -i s/80/$BM_WEBDAV_PORT/g /etc/nginx/sites-enabled/default
  sed -i s/www-data/root/g /etc/nginx/nginx.conf
  nginx
fi

export PATH=$BM:$PATH

# Start background command for talking to serial if we have one.
if [ -n "$BM_SERIAL_SCRIPT" ]; then
  $BM_SERIAL_SCRIPT | tee results/serial-output.txt &

  while [ ! -e results/serial-output.txt ]; do
    sleep 1
  done
fi

$BM/fastboot_run.py \
  --dev="$BM_SERIAL" \
  --fbserial="$BM_FASTBOOT_SERIAL" \
  --powerup="$BM_POWERUP" \
  --powerdown="$BM_POWERDOWN"
