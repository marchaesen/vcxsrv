#!/bin/bash

set -e
set -o xtrace

############### Install packages for building
dpkg --add-architecture ${DEBIAN_ARCH}
echo 'deb-src https://deb.debian.org/debian testing main' > /etc/apt/sources.list.d/deb-src.list
apt-get update
apt-get -y install ca-certificates
apt-get -y install --no-install-recommends \
	crossbuild-essential-${DEBIAN_ARCH} \
	meson \
	g++ \
	git \
	ccache \
	pkg-config \
	python3-setuptools \
	python-mako \
	python3-mako \
	bison \
	flex \
	gettext \
	cmake \
	bc \
	libssl-dev \
	lavacli \
	csvkit \
	curl \
	unzip \
	wget \
	debootstrap \
	procps \
	qemu-user-static \
	cpio \
	libegl1-mesa-dev \
	\
	libdrm-dev:${DEBIAN_ARCH} \
	libx11-dev:${DEBIAN_ARCH} \
	libxxf86vm-dev:${DEBIAN_ARCH} \
	libexpat1-dev:${DEBIAN_ARCH} \
	libsensors-dev:${DEBIAN_ARCH} \
	libxfixes-dev:${DEBIAN_ARCH} \
	libxdamage-dev:${DEBIAN_ARCH} \
	libxext-dev:${DEBIAN_ARCH} \
	x11proto-dev:${DEBIAN_ARCH} \
	libx11-xcb-dev:${DEBIAN_ARCH} \
	libxcb-dri2-0-dev:${DEBIAN_ARCH} \
	libxcb-glx0-dev:${DEBIAN_ARCH} \
	libxcb-xfixes0-dev:${DEBIAN_ARCH} \
	libxcb-dri3-dev:${DEBIAN_ARCH} \
	libxcb-present-dev:${DEBIAN_ARCH} \
	libxcb-randr0-dev:${DEBIAN_ARCH} \
	libxcb-sync-dev:${DEBIAN_ARCH} \
	libxrandr-dev:${DEBIAN_ARCH} \
	libxshmfence-dev:${DEBIAN_ARCH} \
	libelf-dev:${DEBIAN_ARCH} \
	zlib1g-dev:${DEBIAN_ARCH} \
	libglvnd-core-dev:${DEBIAN_ARCH} \
	libgles2-mesa-dev:${DEBIAN_ARCH} \
	libegl1-mesa-dev:${DEBIAN_ARCH} \
	libpng-dev:${DEBIAN_ARCH} \
	libvulkan-dev:${DEBIAN_ARCH} \
	libvulkan1:${DEBIAN_ARCH} \
	libclang-7-dev:${DEBIAN_ARCH}


############### Build dEQP runner
/usr/share/meson/debcrossgen --arch ${DEBIAN_ARCH} -o /tmp/cross_file.txt
EXTRA_MESON_ARGS="--cross-file /tmp/cross_file.txt"
. .gitlab-ci/build-cts-runner.sh
mkdir -p /artifacts/rootfs/usr/bin
mv /usr/local/bin/deqp-runner /artifacts/rootfs/usr/bin/.


############### Build dEQP
EXTRA_CMAKE_ARGS="-DCMAKE_C_COMPILER=${GCC_ARCH}-gcc -DCMAKE_CXX_COMPILER=${GCC_ARCH}-g++"
STRIP_CMD="${GCC_ARCH}-strip"
. .gitlab-ci/build-deqp-gl.sh
mv /deqp /artifacts/rootfs/.


############### Cross-build kernel
KERNEL_URL="https://gitlab.freedesktop.org/tomeu/linux/-/archive/v5.5-rc1-panfrost-fixes/linux-v5.5-rc1-panfrost-fixes.tar.gz"
export ARCH=${KERNEL_ARCH}
export CROSS_COMPILE="${GCC_ARCH}-"

mkdir -p /kernel
wget -qO- ${KERNEL_URL} | tar -xz --strip-components=1 -C /kernel
cd /kernel
./scripts/kconfig/merge_config.sh ${DEFCONFIG} /tmp/clone/.gitlab-ci/${KERNEL_ARCH}.config
make -j12 ${KERNEL_IMAGE_NAME} dtbs
cp arch/${KERNEL_ARCH}/boot/${KERNEL_IMAGE_NAME} /artifacts/.
cp ${DEVICE_TREES} /artifacts/.
rm -rf /kernel


############### Create rootfs
cp /tmp/clone/.gitlab-ci/create-rootfs.sh /artifacts/rootfs/.
mkdir -p /artifacts/rootfs/bin
cp /usr/bin/qemu-aarch64-static /artifacts/rootfs/bin
cp /usr/bin/qemu-arm-static /artifacts/rootfs/bin

set +e
debootstrap --variant=minbase --arch=${DEBIAN_ARCH} testing /artifacts/rootfs/ http://deb.debian.org/debian
cat /artifacts/rootfs/debootstrap/debootstrap.log
set -e
chroot /artifacts/rootfs sh /create-rootfs.sh

rm /artifacts/rootfs/bin/qemu-arm-static
rm /artifacts/rootfs/bin/qemu-aarch64-static
rm /artifacts/rootfs/create-rootfs.sh
