#!/bin/bash

set -e
set -o xtrace

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
. .gitlab-ci/build-rust.sh

if [[ "$DEBIAN_ARCH" = "arm64" ]]; then
    GCC_ARCH="aarch64-linux-gnu"
    KERNEL_ARCH="arm64"
    DEFCONFIG="arch/arm64/configs/defconfig"
    DEVICE_TREES="arch/arm64/boot/dts/rockchip/rk3399-gru-kevin.dtb arch/arm64/boot/dts/amlogic/meson-gxl-s905x-libretech-cc.dtb arch/arm64/boot/dts/allwinner/sun50i-h6-pine-h64.dtb arch/arm64/boot/dts/amlogic/meson-gxm-khadas-vim2.dtb arch/arm64/boot/dts/qcom/apq8016-sbc.dtb arch/arm64/boot/dts/amlogic/meson-g12b-a311d-khadas-vim3.dtb"
    KERNEL_IMAGE_NAME="Image"
elif [[ "$DEBIAN_ARCH" = "armhf" ]]; then
    GCC_ARCH="arm-linux-gnueabihf"
    KERNEL_ARCH="arm"
    DEFCONFIG="arch/arm/configs/multi_v7_defconfig"
    DEVICE_TREES="arch/arm/boot/dts/rk3288-veyron-jaq.dtb arch/arm/boot/dts/sun8i-h3-libretech-all-h3-cc.dtb"
    KERNEL_IMAGE_NAME="zImage"
    . .gitlab-ci/create-cross-file.sh armhf
else
    GCC_ARCH="x86_64-linux-gnu"
    KERNEL_ARCH="x86_64"
    DEFCONFIG="arch/x86/configs/x86_64_defconfig"
    DEVICE_TREES=""
    KERNEL_IMAGE_NAME="bzImage"
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
apt-get install -y automake \
                   bc \
                   cmake \
                   debootstrap \
                   git \
                   libboost-dev \
                   libegl1-mesa-dev \
                   libgbm-dev \
                   libgles2-mesa-dev \
                   libpcre3-dev \
                   libpng-dev \
                   libpython3-dev \
                   libssl-dev \
                   libvulkan-dev \
                   libwaffle-dev \
                   libxcb-keysyms1-dev \
                   libxkbcommon-dev \
                   patch \
                   python3-dev \
                   python3-distutils \
                   python3-mako \
                   python3-numpy \
                   python3-serial \
                   qt5-default \
                   qt5-qmake \
                   qtbase5-dev \
                   wget


if [[ "$DEBIAN_ARCH" = "armhf" ]]; then
    apt-get install -y libboost-dev:armhf \
                       libegl1-mesa-dev:armhf \
                       libelf-dev:armhf \
                       libgbm-dev:armhf \
                       libgles2-mesa-dev:armhf \
                       libpcre3-dev:armhf \
                       libpng-dev:armhf \
                       libpython3-dev:armhf \
                       libvulkan-dev:armhf \
                       libwaffle-dev:armhf \
                       libxcb-keysyms1-dev:armhf \
                       libxkbcommon-dev:armhf \
                       qtbase5-dev:armhf
fi


############### Building
STRIP_CMD="${GCC_ARCH}-strip"
mkdir -p /lava-files/rootfs-${DEBIAN_ARCH}


############### Build dEQP runner
. .gitlab-ci/build-deqp-runner.sh
mkdir -p /lava-files/rootfs-${DEBIAN_ARCH}/usr/bin
mv /usr/local/bin/deqp-runner /lava-files/rootfs-${DEBIAN_ARCH}/usr/bin/.


############### Build dEQP
DEQP_TARGET=surfaceless . .gitlab-ci/build-deqp.sh

mv /deqp /lava-files/rootfs-${DEBIAN_ARCH}/.


############### Build piglit
if [ -n "$INCLUDE_PIGLIT" ]; then
    . .gitlab-ci/build-piglit.sh
    mv /piglit /lava-files/rootfs-${DEBIAN_ARCH}/.
fi


############### Build apitrace
. .gitlab-ci/build-apitrace.sh
mkdir -p /lava-files/rootfs-${DEBIAN_ARCH}/apitrace
mv /apitrace/build /lava-files/rootfs-${DEBIAN_ARCH}/apitrace
rm -rf /apitrace

mkdir -p /lava-files/rootfs-${DEBIAN_ARCH}/waffle
mv /waffle/build /lava-files/rootfs-${DEBIAN_ARCH}/waffle
rm -rf /waffle


############### Build renderdoc
EXTRA_CMAKE_ARGS+=" -DENABLE_XCB=false"
. .gitlab-ci/build-renderdoc.sh
mkdir -p /lava-files/rootfs-${DEBIAN_ARCH}/renderdoc
mv /renderdoc/build /lava-files/rootfs-${DEBIAN_ARCH}/renderdoc
rm -rf /renderdoc


############### Build libdrm
EXTRA_MESON_ARGS+=" -D prefix=/libdrm"
. .gitlab-ci/build-libdrm.sh


############### Cross-build kernel
mkdir -p kernel
wget -qO- ${KERNEL_URL} | tar -xz --strip-components=1 -C kernel
pushd kernel

# The kernel doesn't like the gold linker (or the old lld in our debians).
# Sneak in some override symlinks during kernel build until we can update
# debian (they'll get blown away by the rm of the kernel dir at the end).
mkdir -p ld-links
for i in /usr/bin/*-ld /usr/bin/ld; do
    i=`basename $i`
    ln -sf /usr/bin/$i.bfd ld-links/$i
done
export PATH=`pwd`/ld-links:$PATH

if [ -n "$INSTALL_KERNEL_MODULES" ]; then
    # Disable all modules in defconfig, so we only build the ones we want
    sed -i 's/=m/=n/g' ${DEFCONFIG}
fi

# Force db410c to host mode instead of OTG (which is otherwise selected by
# default due to our micro cable for fastboot)
sed -i 's/dr_mode = "otg"/dr_mode = "host"/' arch/arm64/boot/dts/qcom/apq8016-sbc.dtsi

./scripts/kconfig/merge_config.sh ${DEFCONFIG} ../.gitlab-ci/${KERNEL_ARCH}.config
make ${KERNEL_IMAGE_NAME}
for image in ${KERNEL_IMAGE_NAME}; do
    cp arch/${KERNEL_ARCH}/boot/${image} /lava-files/.
done

if [[ -n ${DEVICE_TREES} ]]; then
    make dtbs
    cp ${DEVICE_TREES} /lava-files/.
fi

if [ -n "$INSTALL_KERNEL_MODULES" ]; then
    make modules
    INSTALL_MOD_PATH=/lava-files/rootfs-${DEBIAN_ARCH}/ make modules_install
fi

if [[ ${DEBIAN_ARCH} = "arm64" ]] && which mkimage > /dev/null; then
    make Image.lzma
    mkimage \
        -f auto \
        -A arm \
        -O linux \
        -d arch/arm64/boot/Image.lzma \
        -C lzma\
        -b arch/arm64/boot/dts/qcom/sdm845-cheza-r3.dtb \
        /lava-files/cheza-kernel
fi

popd
rm -rf kernel

############### Delete rust, since the tests won't be compiling anything.
rm -rf /root/.rustup /root/.cargo

############### Create rootfs
set +e
debootstrap \
    --variant=minbase \
    --arch=${DEBIAN_ARCH} \
     --components main,contrib,non-free \
    buster \
    /lava-files/rootfs-${DEBIAN_ARCH}/ \
    http://deb.debian.org/debian

cat /lava-files/rootfs-${DEBIAN_ARCH}/debootstrap/debootstrap.log
set -e

cp .gitlab-ci/create-rootfs.sh /lava-files/rootfs-${DEBIAN_ARCH}/.
cp .gitlab-ci/container/llvm-snapshot.gpg.key /lava-files/rootfs-${DEBIAN_ARCH}/.
chroot /lava-files/rootfs-${DEBIAN_ARCH} \
    sh -c "INCLUDE_PIGLIT=$INCLUDE_PIGLIT sh /create-rootfs.sh"
rm /lava-files/rootfs-${DEBIAN_ARCH}/create-rootfs.sh
rm /lava-files/rootfs-${DEBIAN_ARCH}/llvm-snapshot.gpg.key


############### Install the built libdrm
# Dependencies pulled during the creation of the rootfs may overwrite
# the built libdrm. Hence, we add it after the rootfs has been already
# created.
mkdir -p /lava-files/rootfs-${DEBIAN_ARCH}/usr/lib/$GCC_ARCH
find /libdrm/ -name lib\*\.so\* | xargs cp -t /lava-files/rootfs-${DEBIAN_ARCH}/usr/lib/$GCC_ARCH/.
rm -rf /libdrm


du -ah /lava-files/rootfs-${DEBIAN_ARCH} | sort -h | tail -100
pushd /lava-files/rootfs-${DEBIAN_ARCH}
  tar czf /lava-files/lava-rootfs.tgz .
popd

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

    # Add missing a630 firmware, added to debian packge in apr 2020
    wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/qcom/a630_gmu.bin \
         -O /lava-files/rootfs-arm64/lib/firmware/qcom/a630_gmu.bin
    wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/qcom/a630_sqe.fw \
         -O /lava-files/rootfs-arm64/lib/firmware/qcom/a630_sqe.fw
fi

. .gitlab-ci/container/container_post_build.sh

############### Upload the files!
if [ -n "$UPLOAD_FOR_LAVA" ]; then
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
fi

