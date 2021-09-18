#!/bin/bash

set -ex

mkdir -p kernel
wget -qO- ${KERNEL_URL} | tar -xj --strip-components=1 -C kernel
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

export LOCALVERSION="`basename $KERNEL_URL`"
./scripts/kconfig/merge_config.sh ${DEFCONFIG} ../.gitlab-ci/container/${KERNEL_ARCH}.config
make ${KERNEL_IMAGE_NAME}
for image in ${KERNEL_IMAGE_NAME}; do
    cp arch/${KERNEL_ARCH}/boot/${image} /lava-files/.
done

if [[ -n ${DEVICE_TREES} ]]; then
    make dtbs
    cp ${DEVICE_TREES} /lava-files/.
fi

if [[ ${DEBIAN_ARCH} = "amd64" ]]; then
    make modules
    INSTALL_MOD_PATH=/lava-files/rootfs-${DEBIAN_ARCH}/ make modules_install
fi

if [[ ${DEBIAN_ARCH} = "arm64" ]]; then
    make Image.lzma
    mkimage \
        -f auto \
        -A arm \
        -O linux \
        -d arch/arm64/boot/Image.lzma \
        -C lzma\
        -b arch/arm64/boot/dts/qcom/sdm845-cheza-r3.dtb \
        /lava-files/cheza-kernel
    KERNEL_IMAGE_NAME+=" cheza-kernel"
fi

popd
rm -rf kernel

