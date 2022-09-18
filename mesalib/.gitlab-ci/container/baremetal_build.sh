#!/bin/bash

set -e
set -o xtrace

# Fetch the arm-built rootfs image and unpack it in our x86 container (saves
# network transfer, disk usage, and runtime on test jobs)

# shellcheck disable=SC2154 # arch is assigned in previous scripts
if wget -q --method=HEAD "${ARTIFACTS_PREFIX}/${FDO_UPSTREAM_REPO}/${ARTIFACTS_SUFFIX}/${arch}/done"; then
  ARTIFACTS_URL="${ARTIFACTS_PREFIX}/${FDO_UPSTREAM_REPO}/${ARTIFACTS_SUFFIX}/${arch}"
else
  ARTIFACTS_URL="${ARTIFACTS_PREFIX}/${CI_PROJECT_PATH}/${ARTIFACTS_SUFFIX}/${arch}"
fi

wget "${ARTIFACTS_URL}"/lava-rootfs.tar.zst -O rootfs.tar.zst
mkdir -p /rootfs-"$arch"
tar -C /rootfs-"$arch" '--exclude=./dev/*' --zstd -xf rootfs.tar.zst
rm rootfs.tar.zst

if [[ $arch == "arm64" ]]; then
    mkdir -p /baremetal-files
    pushd /baremetal-files

    wget "${ARTIFACTS_URL}"/Image
    wget "${ARTIFACTS_URL}"/Image.gz
    wget "${ARTIFACTS_URL}"/cheza-kernel

    DEVICE_TREES=""
    DEVICE_TREES="$DEVICE_TREES apq8016-sbc.dtb"
    DEVICE_TREES="$DEVICE_TREES apq8096-db820c.dtb"
    DEVICE_TREES="$DEVICE_TREES tegra210-p3450-0000.dtb"

    for DTB in $DEVICE_TREES; do
        wget "${ARTIFACTS_URL}/$DTB"
    done

    popd
elif [[ $arch == "armhf" ]]; then
    mkdir -p /baremetal-files
    pushd /baremetal-files

    wget "${ARTIFACTS_URL}"/zImage

    DEVICE_TREES=""
    DEVICE_TREES="$DEVICE_TREES imx6q-cubox-i.dtb"
    DEVICE_TREES="$DEVICE_TREES tegra124-jetson-tk1.dtb"

    for DTB in $DEVICE_TREES; do
        wget "${ARTIFACTS_URL}/$DTB"
    done

    popd
fi
