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

set -ex

# Clear out any previous run's artifacts.
rm -rf results/
mkdir -p results
find artifacts/ -name serial\*.txt  | xargs rm -f

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

cat $BM_KERNEL $BM_DTB > Image.gz-dtb

abootimg \
  --create artifacts/fastboot.img \
  -k Image.gz-dtb \
  -r rootfs.cpio.gz \
  -c cmdline="$BM_CMDLINE"
rm Image.gz-dtb

# Start watching serial, and power up the device.
if [ -n "$BM_SERIAL" ]; then
  $BM/serial-buffer.py $BM_SERIAL | tee artifacts/serial-output.txt &
else
  PATH=$BM:$PATH $BM_SERIAL_SCRIPT | tee artifacts/serial-output.txt &
fi

while [ ! -e artifacts/serial-output.txt ]; do
  sleep 1
done
PATH=$BM:$PATH $BM_POWERUP

# Once fastboot is ready, boot our image.
$BM/expect-output.sh artifacts/serial-output.txt \
  -f "fastboot: processing commands" \
  -f "Listening for fastboot command on" \
  -e "data abort"

fastboot boot -s $BM_FASTBOOT_SERIAL artifacts/fastboot.img

# Wait for the device to complete the deqp run
$BM/expect-output.sh artifacts/serial-output.txt \
    -f "bare-metal result" \
    -e "---. end Kernel panic"

# power down the device
PATH=$BM:$PATH $BM_POWERDOWN

set +e
if grep -q "bare-metal result: pass" artifacts/serial-output.txt; then
   exit 0
else
   exit 1
fi

