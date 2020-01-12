#!/bin/bash

set -e
set -o xtrace

echo 'deb-src https://deb.debian.org/debian testing main' > /etc/apt/sources.list.d/deb-src.list
apt-get update
apt-get install -y \
	autoconf \
	automake \
	ca-certificates \
	ccache \
	git \
	libgl1 \
	libglx-mesa0 \
	libnvidia-egl-wayland-dev \
	libtool \
	libxkbcommon-dev \
	meson \
	python3-mako \
	python3-numpy \
	python3-six \
	x11-utils \
	x11-xserver-utils \
	xauth \
	xvfb \

apt-get build-dep -y xorg-server

cd /root

git clone https://gitlab.freedesktop.org/mesa/piglit.git --depth 1

git clone https://gitlab.freedesktop.org/xorg/test/xts --depth 1
cd xts
./autogen.sh
xvfb-run make -j4
cd ..

git clone https://gitlab.freedesktop.org/xorg/test/rendercheck --depth 1
cd rendercheck
meson build
ninja -j4 -C build install
cd ..

rm -rf piglit/.git xts/.git piglit/tests/spec/

echo '[xts]' > piglit/piglit.conf
echo 'path=/root/xts' >> piglit/piglit.conf

find -name \*.a -o -name \*.o -o -name \*.c -o -name \*.h -o -name \*.la\* | xargs rm
strip xts/xts5/*/.libs/*

apt-get purge -y \
	git \
	libxkbcommon-dev \
	x11-utils \
	x11-xserver-utils \
	xauth \
	xvfb \

apt-get autoremove -y --purge
