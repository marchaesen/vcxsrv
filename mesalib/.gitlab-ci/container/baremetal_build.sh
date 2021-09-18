#!/bin/bash

set -e
set -o xtrace

# Fetch the arm-built rootfs image and unpack it in our x86 container (saves
# network transfer, disk usage, and runtime on test jobs)

if wget -q --method=HEAD "${ARTIFACTS_PREFIX}/${FDO_UPSTREAM_REPO}/${ARTIFACTS_SUFFIX}/${arch}/done"; then
  ARTIFACTS_URL="${ARTIFACTS_PREFIX}/${FDO_UPSTREAM_REPO}/${ARTIFACTS_SUFFIX}/${arch}"
else
  ARTIFACTS_URL="${ARTIFACTS_PREFIX}/${CI_PROJECT_PATH}/${ARTIFACTS_SUFFIX}/${arch}"
fi

wget ${ARTIFACTS_URL}/lava-rootfs.tgz -O rootfs.tgz
mkdir -p /rootfs-$arch
tar -C /rootfs-$arch '--exclude=./dev/*' -zxf rootfs.tgz
rm rootfs.tgz

if [[ $arch == "arm64" ]]; then
    mkdir -p /baremetal-files
    pushd /baremetal-files

    wget ${ARTIFACTS_URL}/Image
    wget ${ARTIFACTS_URL}/Image.gz
    wget ${ARTIFACTS_URL}/cheza-kernel

    DEVICE_TREES="apq8016-sbc.dtb apq8096-db820c.dtb"

    for DTB in $DEVICE_TREES; do
        wget ${ARTIFACTS_URL}/$DTB
    done

    popd
elif [[ $arch == "armhf" ]]; then
    mkdir -p /baremetal-files
    pushd /baremetal-files

    wget ${ARTIFACTS_URL}/zImage

    DEVICE_TREES="imx6q-cubox-i.dtb"

    for DTB in $DEVICE_TREES; do
        wget ${ARTIFACTS_URL}/$DTB
    done

    popd
fi
