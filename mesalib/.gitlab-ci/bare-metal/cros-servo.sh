#!/bin/bash

# Boot script for Chrome OS devices attached to a servo debug connector, using
# NFS and TFTP to boot.

# We're run from the root of the repo, make a helper var for our paths
BM=$CI_PROJECT_DIR/install/bare-metal

# Runner config checks
if [ -z "$BM_SERIAL" ]; then
  echo "Must set BM_SERIAL in your gitlab-runner config.toml [[runners]] environment"
  echo "This is the CPU serial device."
  exit 1
fi

if [ -z "$BM_SERIAL_EC" ]; then
  echo "Must set BM_SERIAL in your gitlab-runner config.toml [[runners]] environment"
  echo "This is the EC serial device for controlling board power"
  exit 1
fi

if [ ! -d /nfs ]; then
  echo "NFS rootfs directory needs to be mounted at /nfs by the gitlab runner"
  exit 1
fi

if [ ! -d /tftp ]; then
  echo "TFTP directory for this board needs to be mounted at /tftp by the gitlab runner"
  exit 1
fi

# job config checks
if [ -z "$BM_KERNEL" ]; then
  echo "Must set BM_KERNEL to your board's kernel FIT image"
  exit 1
fi

if [ -z "$BM_ROOTFS" ]; then
  echo "Must set BM_ROOTFS to your board's rootfs directory in the job's variables"
  exit 1
fi

if [ -z "$BM_CMDLINE" ]; then
  echo "Must set BM_CMDLINE to your board's kernel command line arguments"
  exit 1
fi

set -ex

# Clear out any previous run's artifacts.
rm -rf results/
mkdir -p results

# Create the rootfs in the NFS directory.  rm to make sure it's in a pristine
# state, since it's volume-mounted on the host.
rsync -a --delete $BM_ROOTFS/ /nfs/
mkdir -p /nfs/results
. $BM/rootfs-setup.sh /nfs

# Put the kernel/dtb image and the boot command line in the tftp directory for
# the board to find.  For normal Mesa development, we build the kernel and
# store it in the docker container that this script is running in.
#
# However, container builds are expensive, so when you're hacking on the
# kernel, it's nice to be able to skip the half hour container build and plus
# moving that container to the runner.  So, if BM_KERNEL is a URL, fetch it
# instead of looking in the container.  Note that the kernel build should be
# the output of:
#
# make Image.lzma
#
# mkimage \
#  -A arm64 \
#  -f auto \
#  -C lzma \
#  -d arch/arm64/boot/Image.lzma \
#  -b arch/arm64/boot/dts/qcom/sdm845-cheza-r3.dtb \
#  cheza-image.img

rm -rf /tftp/*
if echo "$BM_KERNEL" | grep -q http; then
  apt install -y wget
  wget $BM_KERNEL -O /tftp/vmlinuz
else
  cp $BM_KERNEL /tftp/vmlinuz
fi
echo "$BM_CMDLINE" > /tftp/cmdline

set +e
python3 $BM/cros_servo_run.py \
        --cpu $BM_SERIAL \
        --ec $BM_SERIAL_EC
ret=$?
set -e

# Bring artifacts back from the NFS dir to the build dir where gitlab-runner
# will look for them.
cp -Rp /nfs/results/. results/

exit $ret
