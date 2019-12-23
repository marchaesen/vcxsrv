#!/bin/bash

set -e
set -o xtrace

############### Install packages for building
apt-get -y install ca-certificates
sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
echo 'deb https://deb.debian.org/debian buster-backports main' >/etc/apt/sources.list.d/backports.list
apt-get update
apt-get -y install \
	bzip2 \
	cmake \
	g++ \
	gcc \
	git \
	libc6-dev \
	libdrm-nouveau2 \
	libexpat1 \
	libgbm-dev \
	libgbm-dev \
	libgles2-mesa-dev \
	libllvm8 \
	libpng16-16 \
	libpng-dev \
	libvulkan-dev \
	libvulkan1 \
	meson \
	netcat \
	pkg-config \
	procps \
	python \
	waffle-utils \
	wget \
	zlib1g

############### Build dEQP runner

. .gitlab-ci/build-cts-runner.sh

############### Build dEQP GL

. .gitlab-ci/build-deqp-gl.sh


############### Uninstall the build software

apt-get purge -y \
        bzip2 \
        cmake \
        g++ \
        gcc \
        git \
        libc6-dev \
        libgbm-dev \
        libgles2-mesa-dev \
        libpng-dev \
        libvulkan-dev \
        meson \
        pkg-config \
        python \
        wget

apt-get autoremove -y --purge
