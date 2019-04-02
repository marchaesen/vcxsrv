#!/bin/bash

set -e
set -o xtrace

echo 'deb-src https://deb.debian.org/debian testing main' > /etc/apt/sources.list.d/deb-src.list
apt-get update
apt-get install -y \
	meson git ca-certificates ccache automake autoconf libtool \
	libxkbcommon-dev python3-mako python3-numpy python3-six \
	x11-utils x11-xserver-utils xauth xvfb \
        libgl1 libglx-mesa0

apt-get build-dep -y xorg-server

cd /root

git clone https://gitlab.freedesktop.org/mesa/piglit.git --depth 1

git clone https://gitlab.freedesktop.org/xorg/test/xts --depth 1
cd xts
./autogen.sh
xvfb-run make -j$(nproc)
cd ..

git clone https://gitlab.freedesktop.org/xorg/test/rendercheck --depth 1
cd rendercheck
meson build
ninja -C build install
cd ..

rm -rf piglit/.git xts/.git

echo '[xts]' > piglit/piglit.conf
echo 'path=/root/xts' >> piglit/piglit.conf

find -name \*.a -o -name \*.o | xargs rm

apt-get purge -y git libxkbcommon-dev \
    x11-utils x11-xserver-utils xauth xvfb
apt-get autoremove -y --purge
