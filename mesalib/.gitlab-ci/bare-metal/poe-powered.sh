#!/bin/bash

# Boot script for devices attached to a PoE switch, using NFS for the root
# filesystem.

# We're run from the root of the repo, make a helper var for our paths
BM=$CI_PROJECT_DIR/install/bare-metal

# Runner config checks
if [ -z "$BM_SERIAL" ]; then
  echo "Must set BM_SERIAL in your gitlab-runner config.toml [[runners]] environment"
  echo "This is the serial port to listen the device."
  exit 1
fi

if [ -z "$BM_POE_ADDRESS" ]; then
  echo "Must set BM_POE_ADDRESS in your gitlab-runner config.toml [[runners]] environment"
  echo "This is the PoE switch address to connect for powering up/down devices."
  exit 1
fi

if [ -z "$BM_POE_USERNAME" ]; then
  echo "Must set BM_POE_USERNAME in your gitlab-runner config.toml [[runners]] environment"
  echo "This is the PoE switch username."
  exit 1
fi

if [ -z "$BM_POE_PASSWORD" ]; then
  echo "Must set BM_POE_PASSWORD in your gitlab-runner config.toml [[runners]] environment"
  echo "This is the PoE switch password."
  exit 1
fi

if [ -z "$BM_POE_INTERFACE" ]; then
  echo "Must set BM_POE_INTERFACE in your gitlab-runner config.toml [[runners]] environment"
  echo "This is the PoE switch interface where the device is connected."
  exit 1
fi

if [ -z "$BM_POWERUP" ]; then
  echo "Must set BM_POWERUP in your gitlab-runner config.toml [[runners]] environment"
  echo "This is a shell script that should power up the device and begin its boot sequence."
  exit 1
fi

if [ -z "$BM_POWERDOWN" ]; then
  echo "Must set BM_POWERDOWN in your gitlab-runner config.toml [[runners]] environment"
  echo "This is a shell script that should power off the device."
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
if [ -z "$BM_ROOTFS" ]; then
  echo "Must set BM_ROOTFS to your board's rootfs directory in the job's variables"
  exit 1
fi

if [ -z "$BM_BOOTFS" ]; then
  echo "Must set /boot files for the TFTP boot in the job's variables"
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

[ -z $BM_ROOTFS_EXTRA ] || rsync -a $BM_ROOTFS_EXTRA/ /nfs/

mkdir -p /nfs/results
. $BM/rootfs-setup.sh /nfs

rsync -a --delete $BM_BOOTFS/ /tftp/

echo "$BM_CMDLINE" > /tftp/cmdline.txt

set +e
ATTEMPTS=2
while [ $((ATTEMPTS--)) -gt 0 ]; do
  python3 $BM/poe_run.py \
          --dev="$BM_SERIAL" \
          --powerup="$BM_POWERUP" \
          --powerdown="$BM_POWERDOWN"
  ret=$?

  if [ $ret -eq 2 ]; then
    echo "Did not detect boot sequence, retrying..."
  else
    ATTEMPTS=0
  fi
done
set -e

# Bring artifacts back from the NFS dir to the build dir where gitlab-runner
# will look for them.
cp -Rp /nfs/results/. results/

exit $ret
