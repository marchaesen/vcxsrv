#!/bin/bash

set -e
set -o xtrace

############### Install packages for building
apt-get -y install ca-certificates
sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
echo 'deb https://deb.debian.org/debian buster-backports main' >/etc/apt/sources.list.d/backports.list
dpkg --add-architecture armhf
apt-get update
apt-get -y install \
	abootimg \
	android-sdk-ext4-utils \
	autoconf \
	automake \
	bc \
	bison \
	ccache \
	cmake \
	cpio \
	crossbuild-essential-armhf \
	debootstrap \
	fastboot \
	flex \
	g++ \
	git \
	lavacli \
	libboost-dev:armhf \
	libboost-dev \
	libdrm-dev:armhf \
	libdrm-dev \
	libegl1-mesa-dev \
	libegl1-mesa-dev:armhf \
	libelf-dev \
	libelf-dev:armhf \
	libexpat1-dev \
	libexpat1-dev:armhf \
	libgbm-dev \
	libgbm-dev:armhf \
	libgles2-mesa-dev \
	libgles2-mesa-dev:armhf \
	libpcre3-dev \
	libpcre3-dev:armhf \
	libpng-dev \
	libpng-dev:armhf \
	libpython3-dev \
	libpython3-dev:armhf \
	libssl-dev \
	libvulkan-dev \
	libvulkan-dev \
	libvulkan-dev:armhf \
	libxcb-keysyms1-dev \
	libxcb-keysyms1-dev:armhf \
	llvm-7-dev:armhf \
	llvm-8-dev \
	pkg-config \
	python \
	python3-dev \
	python3-distutils \
	python3-setuptools \
	python3-mako \
	python3-serial \
	qt5-default \
	qt5-qmake \
	qtbase5-dev \
	qtbase5-dev:armhf \
	unzip \
	wget \
	xz-utils \
	zlib1g-dev

apt install -y --no-remove -t buster-backports \
    meson

. .gitlab-ci/container/container_pre_build.sh

# dependencies where we want a specific version
export LIBDRM_VERSION=libdrm-2.4.100

wget https://dri.freedesktop.org/libdrm/$LIBDRM_VERSION.tar.bz2
tar -xvf $LIBDRM_VERSION.tar.bz2 && rm $LIBDRM_VERSION.tar.bz2
cd $LIBDRM_VERSION; meson build -D vc4=true -D freedreno=true -D etnaviv=true; ninja -C build install; cd ..
rm -rf $LIBDRM_VERSION

############### Generate cross build file for Meson

. .gitlab-ci/create-cross-file.sh armhf

############### Generate kernel, ramdisk, test suites, etc for LAVA jobs
KERNEL_URL="https://gitlab.freedesktop.org/tomeu/linux/-/archive/v5.5-panfrost-fixes/linux-v5.5-panfrost-fixes.tar.gz"

DEBIAN_ARCH=arm64 . .gitlab-ci/container/lava_arm.sh
DEBIAN_ARCH=armhf . .gitlab-ci/container/lava_arm.sh

apt-get purge -y \
        wget

. .gitlab-ci/container/container_post_build.sh
