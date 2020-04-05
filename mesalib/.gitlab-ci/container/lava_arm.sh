#!/bin/bash

set -e
set -o xtrace

if [[ "$DEBIAN_ARCH" = "arm64" ]]; then
    GCC_ARCH="aarch64-linux-gnu"
    KERNEL_ARCH="arm64"
    DEFCONFIG="arch/arm64/configs/defconfig"
    DEVICE_TREES="arch/arm64/boot/dts/rockchip/rk3399-gru-kevin.dtb arch/arm64/boot/dts/amlogic/meson-gxl-s905x-libretech-cc.dtb arch/arm64/boot/dts/allwinner/sun50i-h6-pine-h64.dtb arch/arm64/boot/dts/amlogic/meson-gxm-khadas-vim2.dtb arch/arm64/boot/dts/qcom/apq8016-sbc.dtb"
    KERNEL_IMAGE_NAME="Image"
else
    GCC_ARCH="arm-linux-gnueabihf"
    KERNEL_ARCH="arm"
    DEFCONFIG="arch/arm/configs/multi_v7_defconfig"
    DEVICE_TREES="arch/arm/boot/dts/rk3288-veyron-jaq.dtb arch/arm/boot/dts/sun8i-h3-libretech-all-h3-cc.dtb"
    KERNEL_IMAGE_NAME="zImage"
fi

############### Build dEQP runner
if [[ "$DEBIAN_ARCH" = "armhf" ]]; then
    EXTRA_MESON_ARGS="--cross-file /cross_file-armhf.txt"
fi
. .gitlab-ci/build-cts-runner.sh
mkdir -p /lava-files/rootfs-${DEBIAN_ARCH}/usr/bin
mv /usr/local/bin/deqp-runner /lava-files/rootfs-${DEBIAN_ARCH}/usr/bin/.


############### Build dEQP
EXTRA_CMAKE_ARGS="-DCMAKE_C_COMPILER=${GCC_ARCH}-gcc -DCMAKE_CXX_COMPILER=${GCC_ARCH}-g++"
STRIP_CMD="${GCC_ARCH}-strip"
. .gitlab-ci/build-deqp-gl.sh
mv /deqp /lava-files/rootfs-${DEBIAN_ARCH}/.


############### Cross-build kernel
KERNEL_URL="https://gitlab.freedesktop.org/tomeu/linux/-/archive/v5.5-panfrost-fixes/linux-v5.5-panfrost-fixes.tar.gz"

if [[ "$DEBIAN_ARCH" = "armhf" ]]; then
    export ARCH=${KERNEL_ARCH}
    export CROSS_COMPILE="${GCC_ARCH}-"
fi

mkdir -p kernel
wget -qO- ${KERNEL_URL} | tar -xz --strip-components=1 -C kernel
pushd kernel
./scripts/kconfig/merge_config.sh ${DEFCONFIG} ../.gitlab-ci/${KERNEL_ARCH}.config
make ${KERNEL_IMAGE_NAME} dtbs
cp arch/${KERNEL_ARCH}/boot/${KERNEL_IMAGE_NAME} /lava-files/.
cp ${DEVICE_TREES} /lava-files/.
popd
rm -rf kernel


############### Create rootfs
set +e
debootstrap \
    --variant=minbase \
    --arch=${DEBIAN_ARCH} \
     --components main,contrib,non-free \
    testing \
    /lava-files/rootfs-${DEBIAN_ARCH}/ \
    http://deb.debian.org/debian

cat /lava-files/rootfs-${DEBIAN_ARCH}/debootstrap/debootstrap.log
set -e

cp .gitlab-ci/create-rootfs.sh /lava-files/rootfs-${DEBIAN_ARCH}/.
chroot /lava-files/rootfs-${DEBIAN_ARCH} sh /create-rootfs.sh
rm /lava-files/rootfs-${DEBIAN_ARCH}/create-rootfs.sh

if [ ${DEBIAN_ARCH} = arm64 ]; then
    # Pull down a specific build of qcomlt/release/qcomlt-5.4 8c79b3d12355
    # ("Merge tag 'v5.4.23' into release/qcomlt-5.4"), where I used the
    # .config from
    # http://snapshots.linaro.org/96boards/dragonboard820c/linaro/debian/457/config-5.4.0-qcomlt-arm64
    # with the following merged in:
    #
    # CONFIG_DRM=y
    # CONFIG_DRM_MSM=y
    # CONFIG_ATL1C=y
    #
    # Reason: 5.5 has a big stack of oopses and warns on db820c.  4.14-5.4
    # linaro kernel binaries (see above .config link) have these as modules
    # and distributed the modules only in the debian system, not the initrd,
    # so they're very hard to extract (involving simg2img and loopback
    # mounting).  4.11 is missing d72fea538fe6 ("drm/msm: Fix the check for
    # the command size") so it can't actually run fredreno.  qcomlt-4.14 is
    # unstable at boot (~10% instaboot rate).  The 5.4 qcomlt kernel with msm
    # built in seems like the easiest way to go.
    wget https://people.freedesktop.org/~anholt/qcomlt-5.4-msm-build/Image.gz -O Image.gz \
         -O /lava-files/db820c-kernel
    wget https://people.freedesktop.org/~anholt/qcomlt-5.4-msm-build/apq8096-db820c.dtb \
         -O /lava-files/db820c.dtb

    # Make a gzipped copy of the Image for db410c.
    gzip -k /lava-files/Image
fi
