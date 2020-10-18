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
find artifacts/ -name serial\*.txt  | xargs rm -f

# Create the rootfs in the NFS directory.  rm to make sure it's in a pristine
# state, since it's volume-mounted on the host.
rsync -a --delete $BM_ROOTFS/ /nfs/
mkdir -p /nfs/results
. $BM/rootfs-setup.sh /nfs

# Set up the TFTP kernel/cmdline.  When we support more than one board with
# this method, we'll need to do some check on the runner name or something.
rm -rf /tftp/*
cp $BM_KERNEL /tftp/vmlinuz
echo "$BM_CMDLINE" > /tftp/cmdline

# Start watching serials, and power up the device.
$BM/serial-buffer.py $BM_SERIAL_EC | tee serial-ec-output.txt | sed -u 's|^|SERIAL-EC> |g' &
$BM/serial-buffer.py $BM_SERIAL | tee serial-output.txt | sed -u 's|^|SERIAL-CPU> |g'  &
while [ ! -e serial-output.txt ]; do
  sleep 1
done
# Flush any partial commands in the EC's prompt, then ask for a reboot.
$BM/write-serial.py $BM_SERIAL_EC ""
$BM/write-serial.py $BM_SERIAL_EC reboot

# This is emitted right when the bootloader pauses to check for input.  Emit a
# ^N character to request network boot, because we don't have a
# direct-to-netboot firmware on cheza.
$BM/expect-output.sh serial-output.txt -f "load_archive: loading locale_en.bin"
$BM/write-serial.py $BM_SERIAL `printf '\016'`

# Wait for the device to complete the deqp run
$BM/expect-output.sh serial-output.txt \
    -f "bare-metal result" \
    -e "---. end Kernel panic" \
    -e "POWER_GOOD not seen in time"

# power down the CPU on the device
$BM/write-serial.py $BM_SERIAL_EC 'power off'

set -ex

# Bring artifacts back from the NFS dir to the build dir where gitlab-runner
# will look for them.  Note that results/ may already exist, so be careful
# with cp.
mkdir -p results
cp -Rp /nfs/results/. results/

set +e
if grep -q "bare-metal result: pass" serial-output.txt; then
   exit 0
else
   exit 1
fi
