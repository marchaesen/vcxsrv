#!/bin/bash

set -e
set -o xtrace

apt-get -y install ca-certificates
sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
echo 'deb https://deb.debian.org/debian buster main' >/etc/apt/sources.list.d/buster.list
apt-get update

EPHEMERAL="
	python3-pytest-runner
	python3-wheel
	"

apt-get -y install \
	abootimg \
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
	libasan6 \
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
	llvm-11-dev \
	meson \
	pkg-config \
	python-is-python3 \
	python3-aiohttp \
	python3-jinja2 \
	python3-mako \
	python3-pil \
	python3-pip \
	python3-requests \
	python3-setuptools \
	python3-yaml \
	python3-zmq \
	u-boot-tools \
	unzip \
	wget \
	xz-utils \
	zlib1g-dev \
	$EPHEMERAL

# Update lavacli to v1.1+
pip3 install git+https://git.lavasoftware.org/lava/lavacli@3db3ddc45e5358908bc6a17448059ea2340492b7

# Not available anymore in bullseye
apt-get install -y --no-remove -t buster \
        android-sdk-ext4-utils

pip3 install git+http://gitlab.freedesktop.org/freedesktop/ci-templates@6f5af7e5574509726c79109e3c147cee95e81366

apt-get purge -y $EPHEMERAL

arch=armhf
. .gitlab-ci/container/cross_build.sh

. .gitlab-ci/container/container_pre_build.sh

# dependencies where we want a specific version
EXTRA_MESON_ARGS=
. .gitlab-ci/container/build-libdrm.sh

. .gitlab-ci/container/container_post_build.sh
