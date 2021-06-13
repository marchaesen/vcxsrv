#!/bin/bash

set -e
set -o xtrace

apt-get -y install ca-certificates
sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
echo 'deb https://deb.debian.org/debian buster-backports main' >/etc/apt/sources.list.d/backports.list
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
	debootstrap \
	fastboot \
	flex \
	g++ \
	git \
	kmod \
	lavacli \
	libasan5 \
	libdrm-dev \
	libelf-dev \
	libexpat1-dev \
	libx11-dev \
	libx11-xcb-dev \
	libxcb-dri2-0-dev \
	libxcb-dri3-dev \
	libxcb-glx0-dev \
	libxcb-present-dev \
	libxcb-randr0-dev \
	libxcb-shm0-dev \
	libxcb-xfixes0-dev \
	libxdamage-dev \
	libxext-dev \
	libxrandr-dev \
	libxshmfence-dev \
	libxxf86vm-dev \
	llvm-8-dev \
	pkg-config \
	python \
	python3-mako \
	python3-pil \
	python3-pip \
	python3-requests \
	python3-setuptools \
	unzip \
	wget \
	xz-utils \
	zlib1g-dev

pip3 install git+http://gitlab.freedesktop.org/freedesktop/ci-templates@6f5af7e5574509726c79109e3c147cee95e81366

apt install -y --no-remove -t buster-backports \
    meson

arch=armhf
. .gitlab-ci/container/cross_build.sh

. .gitlab-ci/container/container_pre_build.sh

# dependencies where we want a specific version
EXTRA_MESON_ARGS=
. .gitlab-ci/container/build-libdrm.sh

. .gitlab-ci/container/container_post_build.sh
