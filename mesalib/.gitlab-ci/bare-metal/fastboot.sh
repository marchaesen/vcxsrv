#!/bin/bash

BM=$CI_PROJECT_DIR/install/bare-metal
CI_COMMON=$CI_PROJECT_DIR/install/common

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

if echo $BM_CMDLINE | grep -q "root=/dev/nfs"; then
  BM_FASTBOOT_NFSROOT=1
fi

set -ex

# Clear out any previous run's artifacts.
rm -rf results/
mkdir -p results/

if [ -n "$BM_FASTBOOT_NFSROOT" ]; then
  # Create the rootfs in the NFS directory.  rm to make sure it's in a pristine
  # state, since it's volume-mounted on the host.
  rsync -a --delete $BM_ROOTFS/ /nfs/
  mkdir -p /nfs/results
  . $BM/rootfs-setup.sh /nfs

  # Root on NFS, no need for an inintramfs.
  rm -f rootfs.cpio.gz
  touch rootfs.cpio
  gzip rootfs.cpio
else
  # Create the rootfs in a temp dir
  rsync -a --delete $BM_ROOTFS/ rootfs/
  . $BM/rootfs-setup.sh rootfs

  # Finally, pack it up into a cpio rootfs.  Skip the vulkan CTS since none of
  # these devices use it and it would take up space in the initrd.

  if [ -n "$PIGLIT_PROFILES" ]; then
    EXCLUDE_FILTER="deqp|arb_gpu_shader5|arb_gpu_shader_fp64|arb_gpu_shader_int64|glsl-4.[0123456]0|arb_tessellation_shader"
  else
    EXCLUDE_FILTER="piglit|python"
  fi

  pushd rootfs
  find -H | \
    egrep -v "external/(openglcts|vulkancts|amber|glslang|spirv-tools)" |
    egrep -v "traces-db|apitrace|renderdoc" | \
    egrep -v $EXCLUDE_FILTER | \
    cpio -H newc -o | \
    xz --check=crc32 -T4 - > $CI_PROJECT_DIR/rootfs.cpio.gz
  popd
fi

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

mkdir -p artifacts
abootimg \
  --create artifacts/fastboot.img \
  -k Image.gz-dtb \
  -r rootfs.cpio.gz \
  -c cmdline="$BM_CMDLINE"
rm Image.gz-dtb

export PATH=$BM:$PATH

# Start background command for talking to serial if we have one.
if [ -n "$BM_SERIAL_SCRIPT" ]; then
  $BM_SERIAL_SCRIPT > results/serial-output.txt &

  while [ ! -e results/serial-output.txt ]; do
    sleep 1
  done
fi

set +e
$BM/fastboot_run.py \
  --dev="$BM_SERIAL" \
  --fbserial="$BM_FASTBOOT_SERIAL" \
  --powerup="$BM_POWERUP" \
  --powerdown="$BM_POWERDOWN"
ret=$?
set -e

if [ -n "$BM_FASTBOOT_NFSROOT" ]; then
  # Bring artifacts back from the NFS dir to the build dir where gitlab-runner
  # will look for them.
  cp -Rp /nfs/results/. results/
fi

exit $ret
