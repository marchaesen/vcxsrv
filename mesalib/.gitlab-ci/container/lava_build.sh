#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

check_minio()
{
    MINIO_PATH="${MINIO_HOST}/mesa-lava/$1/${DISTRIBUTION_TAG}/${DEBIAN_ARCH}"
    if wget -q --method=HEAD "https://${MINIO_PATH}/done"; then
        exit
    fi
}

# If remote files are up-to-date, skip rebuilding them
check_minio "${FDO_UPSTREAM_REPO}"
check_minio "${CI_PROJECT_PATH}"

. .gitlab-ci/container/container_pre_build.sh

# Install rust, which we'll be using for deqp-runner.  It will be cleaned up at the end.
. .gitlab-ci/container/build-rust.sh

if [[ "$DEBIAN_ARCH" = "arm64" ]]; then
    GCC_ARCH="aarch64-linux-gnu"
    KERNEL_ARCH="arm64"
    DEFCONFIG="arch/arm64/configs/defconfig"
    DEVICE_TREES="arch/arm64/boot/dts/rockchip/rk3399-gru-kevin.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/amlogic/meson-gxl-s805x-libretech-ac.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/allwinner/sun50i-h6-pine-h64.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/amlogic/meson-gxm-khadas-vim2.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/qcom/apq8016-sbc.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/qcom/apq8096-db820c.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/amlogic/meson-g12b-a311d-khadas-vim3.dtb"
    DEVICE_TREES+=" arch/arm64/boot/dts/mediatek/mt8183-kukui-jacuzzi-juniper-sku16.dtb"
    KERNEL_IMAGE_NAME="Image"
elif [[ "$DEBIAN_ARCH" = "armhf" ]]; then
    GCC_ARCH="arm-linux-gnueabihf"
    KERNEL_ARCH="arm"
    DEFCONFIG="arch/arm/configs/multi_v7_defconfig"
    DEVICE_TREES="arch/arm/boot/dts/rk3288-veyron-jaq.dtb"
    DEVICE_TREES+=" arch/arm/boot/dts/sun8i-h3-libretech-all-h3-cc.dtb"
    DEVICE_TREES+=" arch/arm/boot/dts/imx6q-cubox-i.dtb"
    KERNEL_IMAGE_NAME="zImage"
    . .gitlab-ci/container/create-cross-file.sh armhf
else
    GCC_ARCH="x86_64-linux-gnu"
    KERNEL_ARCH="x86_64"
    DEFCONFIG="arch/x86/configs/x86_64_defconfig"
    DEVICE_TREES=""
    KERNEL_IMAGE_NAME="bzImage"
    ARCH_PACKAGES="libva-dev"
fi

# Determine if we're in a cross build.
if [[ -e /cross_file-$DEBIAN_ARCH.txt ]]; then
    EXTRA_MESON_ARGS="--cross-file /cross_file-$DEBIAN_ARCH.txt"
    EXTRA_CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=/toolchain-$DEBIAN_ARCH.cmake"

    if [ $DEBIAN_ARCH = arm64 ]; then
        RUST_TARGET="aarch64-unknown-linux-gnu"
    elif [ $DEBIAN_ARCH = armhf ]; then
        RUST_TARGET="armv7-unknown-linux-gnueabihf"
    fi
    rustup target add $RUST_TARGET
    export EXTRA_CARGO_ARGS="--target $RUST_TARGET"

    export ARCH=${KERNEL_ARCH}
    export CROSS_COMPILE="${GCC_ARCH}-"
fi

apt-get update
apt-get install -y --no-remove \
                   ${ARCH_PACKAGES} \
                   automake \
                   bc \
                   cmake \
                   debootstrap \
                   git \
                   glslang-tools \
                   libdrm-dev \
                   libegl1-mesa-dev \
                   libgbm-dev \
                   libgles2-mesa-dev \
                   libpng-dev \
                   libssl-dev \
                   libudev-dev \
                   libvulkan-dev \
                   libwaffle-dev \
                   libwayland-dev \
                   libx11-xcb-dev \
                   libxcb-dri2-0-dev \
                   libxkbcommon-dev \
                   patch \
                   python3-distutils \
                   python3-mako \
                   python3-numpy \
                   python3-serial \
                   wget


if [[ "$DEBIAN_ARCH" = "armhf" ]]; then
    apt-get install -y --no-remove \
                       libegl1-mesa-dev:armhf \
                       libelf-dev:armhf \
                       libgbm-dev:armhf \
                       libgles2-mesa-dev:armhf \
                       libpng-dev:armhf \
                       libudev-dev:armhf \
                       libvulkan-dev:armhf \
                       libwaffle-dev:armhf \
                       libwayland-dev:armhf \
                       libx11-xcb-dev:armhf \
                       libxkbcommon-dev:armhf
fi


############### Building
STRIP_CMD="${GCC_ARCH}-strip"
mkdir -p /lava-files/rootfs-${DEBIAN_ARCH}


############### Build apitrace
. .gitlab-ci/container/build-apitrace.sh
mkdir -p /lava-files/rootfs-${DEBIAN_ARCH}/apitrace
mv /apitrace/build /lava-files/rootfs-${DEBIAN_ARCH}/apitrace
rm -rf /apitrace


############### Build dEQP runner
. .gitlab-ci/container/build-deqp-runner.sh
mkdir -p /lava-files/rootfs-${DEBIAN_ARCH}/usr/bin
mv /usr/local/bin/*-runner /lava-files/rootfs-${DEBIAN_ARCH}/usr/bin/.


############### Build dEQP
DEQP_TARGET=surfaceless . .gitlab-ci/container/build-deqp.sh

mv /deqp /lava-files/rootfs-${DEBIAN_ARCH}/.


############### Build piglit
PIGLIT_OPTS="-DPIGLIT_BUILD_DMA_BUF_TESTS=ON" . .gitlab-ci/container/build-piglit.sh
mv /piglit /lava-files/rootfs-${DEBIAN_ARCH}/.

############### Build libva tests
if [[ "$DEBIAN_ARCH" = "amd64" ]]; then
    . .gitlab-ci/container/build-va-tools.sh
    mv /va/bin/* /lava-files/rootfs-${DEBIAN_ARCH}/usr/bin/
fi

############### Build libdrm
EXTRA_MESON_ARGS+=" -D prefix=/libdrm"
. .gitlab-ci/container/build-libdrm.sh

############### Build kernel
. .gitlab-ci/container/build-kernel.sh

############### Delete rust, since the tests won't be compiling anything.
rm -rf /root/.cargo

############### Create rootfs
set +e
if ! debootstrap \
     --variant=minbase \
     --arch=${DEBIAN_ARCH} \
     --components main,contrib,non-free \
     bullseye \
     /lava-files/rootfs-${DEBIAN_ARCH}/ \
     http://deb.debian.org/debian; then
    cat /lava-files/rootfs-${DEBIAN_ARCH}/debootstrap/debootstrap.log
    exit 1
fi
set -e

cp .gitlab-ci/container/create-rootfs.sh /lava-files/rootfs-${DEBIAN_ARCH}/.
chroot /lava-files/rootfs-${DEBIAN_ARCH} sh /create-rootfs.sh
rm /lava-files/rootfs-${DEBIAN_ARCH}/create-rootfs.sh


############### Install the built libdrm
# Dependencies pulled during the creation of the rootfs may overwrite
# the built libdrm. Hence, we add it after the rootfs has been already
# created.
mkdir -p /lava-files/rootfs-${DEBIAN_ARCH}/usr/lib/$GCC_ARCH
find /libdrm/ -name lib\*\.so\* | xargs cp -t /lava-files/rootfs-${DEBIAN_ARCH}/usr/lib/$GCC_ARCH/.
mkdir -p /lava-files/rootfs-${DEBIAN_ARCH}/libdrm/
cp -Rp /libdrm/share /lava-files/rootfs-${DEBIAN_ARCH}/libdrm/share
rm -rf /libdrm


if [ ${DEBIAN_ARCH} = arm64 ]; then
    # Make a gzipped copy of the Image for db410c.
    gzip -k /lava-files/Image
    KERNEL_IMAGE_NAME+=" Image.gz"
fi

du -ah /lava-files/rootfs-${DEBIAN_ARCH} | sort -h | tail -100
pushd /lava-files/rootfs-${DEBIAN_ARCH}
  tar czf /lava-files/lava-rootfs.tgz .
popd

. .gitlab-ci/container/container_post_build.sh

############### Upload the files!
ci-fairy minio login $CI_JOB_JWT
FILES_TO_UPLOAD="lava-rootfs.tgz \
                 $KERNEL_IMAGE_NAME"

if [[ -n $DEVICE_TREES ]]; then
    FILES_TO_UPLOAD="$FILES_TO_UPLOAD $(basename -a $DEVICE_TREES)"
fi

for f in $FILES_TO_UPLOAD; do
    ci-fairy minio cp /lava-files/$f \
             minio://${MINIO_PATH}/$f
done

touch /lava-files/done
ci-fairy minio cp /lava-files/done minio://${MINIO_PATH}/done
