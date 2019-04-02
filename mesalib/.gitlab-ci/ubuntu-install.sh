#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

apt-get install -y \
      curl \
      wget \
      gnupg \
      software-properties-common

curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
add-apt-repository "deb http://apt.llvm.org/bionic/ llvm-toolchain-bionic-7 main"

apt-get update
apt-get install -y \
      pkg-config \
      libdrm-dev \
      libpciaccess-dev \
      libxrandr-dev \
      libxdamage-dev \
      libxfixes-dev \
      libxshmfence-dev \
      libxxf86vm-dev \
      libvdpau-dev \
      libva-dev \
      llvm-3.9-dev \
      libclang-3.9-dev \
      llvm-5.0-dev \
      llvm-6.0-dev \
      llvm-7-dev \
      clang-5.0 \
      libclang-5.0-dev \
      clang-6.0 \
      libclang-6.0-dev \
      g++ \
      gcc \
      clang-7 \
      libclang-7-dev \
      libclc-dev \
      libxvmc-dev \
      libomxil-bellagio-dev \
      xz-utils \
      libexpat1-dev \
      libx11-xcb-dev \
      x11proto-xf86vidmode-dev \
      libelf-dev \
      libunwind8-dev \
      libglvnd-dev \
      python2.7 \
      python-pip \
      python-setuptools \
      python-wheel \
      python3.5 \
      python3-pip \
      python3-setuptools \
      python3-wheel \
      ninja-build

apt-get install -y \
      libxcb-randr0

# autotools build deps
apt-get install -y \
      autoconf \
      automake \
      xutils-dev \
      libtool \
      bison \
      flex \
      gettext \
      make

# dependencies where we want a specific version
export              XORG_RELEASES=https://xorg.freedesktop.org/releases/individual
export               XCB_RELEASES=https://xcb.freedesktop.org/dist
export           WAYLAND_RELEASES=https://wayland.freedesktop.org/releases

export         XORGMACROS_VERSION=util-macros-1.19.0
export            GLPROTO_VERSION=glproto-1.4.17
export          DRI2PROTO_VERSION=dri2proto-2.8
export       LIBPCIACCESS_VERSION=libpciaccess-0.13.4
export             LIBDRM_VERSION=libdrm-2.4.97
export           XCBPROTO_VERSION=xcb-proto-1.13
export         RANDRPROTO_VERSION=randrproto-1.3.0
export          LIBXRANDR_VERSION=libXrandr-1.3.0
export             LIBXCB_VERSION=libxcb-1.13
export       LIBXSHMFENCE_VERSION=libxshmfence-1.3
export           LIBVDPAU_VERSION=libvdpau-1.1
export              LIBVA_VERSION=libva-1.7.0
export         LIBWAYLAND_VERSION=wayland-1.15.0
export  WAYLAND_PROTOCOLS_VERSION=wayland-protocols-1.8

wget $XORG_RELEASES/util/$XORGMACROS_VERSION.tar.bz2
tar -xvf $XORGMACROS_VERSION.tar.bz2 && rm $XORGMACROS_VERSION.tar.bz2
(cd $XORGMACROS_VERSION && ./configure && make install) && rm -rf $XORGMACROS_VERSION

wget $XORG_RELEASES/proto/$GLPROTO_VERSION.tar.bz2
tar -xvf $GLPROTO_VERSION.tar.bz2 && rm $GLPROTO_VERSION.tar.bz2
(cd $GLPROTO_VERSION && ./configure && make install) && rm -rf $GLPROTO_VERSION

wget $XORG_RELEASES/proto/$DRI2PROTO_VERSION.tar.bz2
tar -xvf $DRI2PROTO_VERSION.tar.bz2 && rm $DRI2PROTO_VERSION.tar.bz2
(cd $DRI2PROTO_VERSION && ./configure && make install) && rm -rf $DRI2PROTO_VERSION

wget $XCB_RELEASES/$XCBPROTO_VERSION.tar.bz2
tar -xvf $XCBPROTO_VERSION.tar.bz2 && rm $XCBPROTO_VERSION.tar.bz2
(cd $XCBPROTO_VERSION && ./configure && make install) && rm -rf $XCBPROTO_VERSION

wget $XCB_RELEASES/$LIBXCB_VERSION.tar.bz2
tar -xvf $LIBXCB_VERSION.tar.bz2 && rm $LIBXCB_VERSION.tar.bz2
(cd $LIBXCB_VERSION && ./configure && make install) && rm -rf $LIBXCB_VERSION

wget $XORG_RELEASES/lib/$LIBPCIACCESS_VERSION.tar.bz2
tar -xvf $LIBPCIACCESS_VERSION.tar.bz2 && rm $LIBPCIACCESS_VERSION.tar.bz2
(cd $LIBPCIACCESS_VERSION && ./configure && make install) && rm -rf $LIBPCIACCESS_VERSION

wget https://dri.freedesktop.org/libdrm/$LIBDRM_VERSION.tar.bz2
tar -xvf $LIBDRM_VERSION.tar.bz2 && rm $LIBDRM_VERSION.tar.bz2
(cd $LIBDRM_VERSION && ./configure --enable-vc4 --enable-freedreno --enable-etnaviv-experimental-api && make install) && rm -rf $LIBDRM_VERSION

wget $XORG_RELEASES/proto/$RANDRPROTO_VERSION.tar.bz2
tar -xvf $RANDRPROTO_VERSION.tar.bz2 && rm $RANDRPROTO_VERSION.tar.bz2
(cd $RANDRPROTO_VERSION && ./configure && make install) && rm -rf $RANDRPROTO_VERSION

wget $XORG_RELEASES/lib/$LIBXRANDR_VERSION.tar.bz2
tar -xvf $LIBXRANDR_VERSION.tar.bz2 && rm $LIBXRANDR_VERSION.tar.bz2
(cd $LIBXRANDR_VERSION && ./configure && make install) && rm -rf $LIBXRANDR_VERSION

wget $XORG_RELEASES/lib/$LIBXSHMFENCE_VERSION.tar.bz2
tar -xvf $LIBXSHMFENCE_VERSION.tar.bz2 && rm $LIBXSHMFENCE_VERSION.tar.bz2
(cd $LIBXSHMFENCE_VERSION && ./configure && make install) && rm -rf $LIBXSHMFENCE_VERSION

wget https://people.freedesktop.org/~aplattner/vdpau/$LIBVDPAU_VERSION.tar.bz2
tar -xvf $LIBVDPAU_VERSION.tar.bz2 && rm $LIBVDPAU_VERSION.tar.bz2
(cd $LIBVDPAU_VERSION && ./configure && make install) && rm -rf $LIBVDPAU_VERSION

wget https://www.freedesktop.org/software/vaapi/releases/libva/$LIBVA_VERSION.tar.bz2
tar -xvf $LIBVA_VERSION.tar.bz2 && rm $LIBVA_VERSION.tar.bz2
(cd $LIBVA_VERSION && ./configure --disable-wayland --disable-dummy-driver && make install) && rm -rf $LIBVA_VERSION

wget $WAYLAND_RELEASES/$LIBWAYLAND_VERSION.tar.xz
tar -xvf $LIBWAYLAND_VERSION.tar.xz && rm $LIBWAYLAND_VERSION.tar.xz
(cd $LIBWAYLAND_VERSION && ./configure --enable-libraries --without-host-scanner --disable-documentation --disable-dtd-validation && make install) && rm -rf $LIBWAYLAND_VERSION

wget $WAYLAND_RELEASES/$WAYLAND_PROTOCOLS_VERSION.tar.xz
tar -xvf $WAYLAND_PROTOCOLS_VERSION.tar.xz && rm $WAYLAND_PROTOCOLS_VERSION.tar.xz
(cd $WAYLAND_PROTOCOLS_VERSION && ./configure && make install) && rm -rf $WAYLAND_PROTOCOLS_VERSION

pip3 install 'meson>=0.49'
pip2 install 'scons>=2.4'

pip2 install mako
pip3 install mako

# Use ccache to speed up builds
apt-get install -y ccache

# We need xmllint to validate the XML files in Mesa
apt-get install -y libxml2-utils
