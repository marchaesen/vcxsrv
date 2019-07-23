#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

CROSS_ARCHITECTURES="armhf arm64 i386"
for arch in $CROSS_ARCHITECTURES; do
    dpkg --add-architecture $arch
done

apt-get install -y \
      apt-transport-https \
      ca-certificates \
      curl \
      wget \
      unzip \
      gnupg \
      software-properties-common

curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
add-apt-repository "deb https://apt.llvm.org/stretch/ llvm-toolchain-stretch-7 main"
add-apt-repository "deb https://apt.llvm.org/stretch/ llvm-toolchain-stretch-8 main"

sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
echo 'deb https://deb.debian.org/debian stretch-backports main' >/etc/apt/sources.list.d/backports.list
echo 'deb https://deb.debian.org/debian jessie main' >/etc/apt/sources.list.d/jessie.list

apt-get update
apt-get install -y -t stretch-backports \
      llvm-3.4-dev \
      llvm-3.9-dev \
      libclang-3.9-dev \
      llvm-4.0-dev \
      libclang-4.0-dev \
      llvm-5.0-dev \
      libclang-5.0-dev \
      llvm-6.0-dev \
      libclang-6.0-dev \
      llvm-7-dev \
      libclang-7-dev \
      llvm-8-dev \
      libclang-8-dev \
      g++ \
      clang-8

# Install remaining packages from Debian buster to get newer versions
add-apt-repository "deb https://deb.debian.org/debian/ buster main"
add-apt-repository "deb https://deb.debian.org/debian/ buster-updates main"
apt-get update
apt-get install -y \
      bzip2 \
      zlib1g-dev \
      pkg-config \
      libxrender-dev \
      libxdamage-dev \
      libxxf86vm-dev \
      gcc \
      libclc-dev \
      libxvmc-dev \
      libomxil-bellagio-dev \
      xz-utils \
      libexpat1-dev \
      libx11-xcb-dev \
      libelf-dev \
      libunwind-dev \
      libglvnd-dev \
      python-mako \
      python3-mako \
      meson \
      scons

# autotools build deps
apt-get install -y \
      automake \
      libtool \
      bison \
      flex \
      gettext \
      make

# Cross-build Mesa deps
for arch in $CROSS_ARCHITECTURES; do
    apt-get install -y \
            libdrm-dev:${arch} \
            libexpat1-dev:${arch} \
            libelf-dev:${arch}
done
apt-get install -y \
        dpkg-dev \
        gcc-aarch64-linux-gnu \
        g++-aarch64-linux-gnu \
        gcc-arm-linux-gnueabihf \
        g++-arm-linux-gnueabihf \
        gcc-i686-linux-gnu \
        g++-i686-linux-gnu

# for 64bit windows cross-builds
apt-get install -y mingw-w64

# for the vulkan overlay layer
wget https://github.com/KhronosGroup/glslang/releases/download/master-tot/glslang-master-linux-Release.zip
unzip glslang-master-linux-Release.zip bin/glslangValidator
install -m755 bin/glslangValidator /usr/local/bin/
rm bin/glslangValidator glslang-master-linux-Release.zip


# dependencies where we want a specific version
export              XORG_RELEASES=https://xorg.freedesktop.org/releases/individual
export               XCB_RELEASES=https://xcb.freedesktop.org/dist
export           WAYLAND_RELEASES=https://wayland.freedesktop.org/releases

export         XORGMACROS_VERSION=util-macros-1.19.0
export            GLPROTO_VERSION=glproto-1.4.17
export          DRI2PROTO_VERSION=dri2proto-2.8
export       LIBPCIACCESS_VERSION=libpciaccess-0.13.4
export             LIBDRM_VERSION=libdrm-2.4.99
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
cd $XORGMACROS_VERSION; ./configure; make install; cd ..
rm -rf $XORGMACROS_VERSION

wget $XORG_RELEASES/proto/$GLPROTO_VERSION.tar.bz2
tar -xvf $GLPROTO_VERSION.tar.bz2 && rm $GLPROTO_VERSION.tar.bz2
cd $GLPROTO_VERSION; ./configure; make install; cd ..
rm -rf $GLPROTO_VERSION

wget $XORG_RELEASES/proto/$DRI2PROTO_VERSION.tar.bz2
tar -xvf $DRI2PROTO_VERSION.tar.bz2 && rm $DRI2PROTO_VERSION.tar.bz2
cd $DRI2PROTO_VERSION; ./configure; make install; cd ..
rm -rf $DRI2PROTO_VERSION

wget $XCB_RELEASES/$XCBPROTO_VERSION.tar.bz2
tar -xvf $XCBPROTO_VERSION.tar.bz2 && rm $XCBPROTO_VERSION.tar.bz2
cd $XCBPROTO_VERSION; ./configure; make install; cd ..
rm -rf $XCBPROTO_VERSION

wget $XCB_RELEASES/$LIBXCB_VERSION.tar.bz2
tar -xvf $LIBXCB_VERSION.tar.bz2 && rm $LIBXCB_VERSION.tar.bz2
cd $LIBXCB_VERSION; ./configure; make install; cd ..
rm -rf $LIBXCB_VERSION

wget $XORG_RELEASES/lib/$LIBPCIACCESS_VERSION.tar.bz2
tar -xvf $LIBPCIACCESS_VERSION.tar.bz2 && rm $LIBPCIACCESS_VERSION.tar.bz2
cd $LIBPCIACCESS_VERSION; ./configure; make install; cd ..
rm -rf $LIBPCIACCESS_VERSION

wget https://dri.freedesktop.org/libdrm/$LIBDRM_VERSION.tar.bz2
tar -xvf $LIBDRM_VERSION.tar.bz2 && rm $LIBDRM_VERSION.tar.bz2
cd $LIBDRM_VERSION; ./configure --enable-vc4 --enable-freedreno --enable-etnaviv-experimental-api; make install; cd ..
rm -rf $LIBDRM_VERSION

wget $XORG_RELEASES/proto/$RANDRPROTO_VERSION.tar.bz2
tar -xvf $RANDRPROTO_VERSION.tar.bz2 && rm $RANDRPROTO_VERSION.tar.bz2
cd $RANDRPROTO_VERSION; ./configure; make install; cd ..
rm -rf $RANDRPROTO_VERSION

wget $XORG_RELEASES/lib/$LIBXRANDR_VERSION.tar.bz2
tar -xvf $LIBXRANDR_VERSION.tar.bz2 && rm $LIBXRANDR_VERSION.tar.bz2
cd $LIBXRANDR_VERSION; ./configure; make install; cd ..
rm -rf $LIBXRANDR_VERSION

wget $XORG_RELEASES/lib/$LIBXSHMFENCE_VERSION.tar.bz2
tar -xvf $LIBXSHMFENCE_VERSION.tar.bz2 && rm $LIBXSHMFENCE_VERSION.tar.bz2
cd $LIBXSHMFENCE_VERSION; ./configure; make install; cd ..
rm -rf $LIBXSHMFENCE_VERSION

wget https://people.freedesktop.org/~aplattner/vdpau/$LIBVDPAU_VERSION.tar.bz2
tar -xvf $LIBVDPAU_VERSION.tar.bz2 && rm $LIBVDPAU_VERSION.tar.bz2
cd $LIBVDPAU_VERSION; ./configure; make install; cd ..
rm -rf $LIBVDPAU_VERSION

wget https://www.freedesktop.org/software/vaapi/releases/libva/$LIBVA_VERSION.tar.bz2
tar -xvf $LIBVA_VERSION.tar.bz2 && rm $LIBVA_VERSION.tar.bz2
cd $LIBVA_VERSION; ./configure --disable-wayland --disable-dummy-driver; make install; cd ..
rm -rf $LIBVA_VERSION

wget $WAYLAND_RELEASES/$LIBWAYLAND_VERSION.tar.xz
tar -xvf $LIBWAYLAND_VERSION.tar.xz && rm $LIBWAYLAND_VERSION.tar.xz
cd $LIBWAYLAND_VERSION; ./configure --enable-libraries --without-host-scanner --disable-documentation --disable-dtd-validation; make install; cd ..
rm -rf $LIBWAYLAND_VERSION

wget $WAYLAND_RELEASES/$WAYLAND_PROTOCOLS_VERSION.tar.xz
tar -xvf $WAYLAND_PROTOCOLS_VERSION.tar.xz && rm $WAYLAND_PROTOCOLS_VERSION.tar.xz
cd $WAYLAND_PROTOCOLS_VERSION; ./configure; make install; cd ..
rm -rf $WAYLAND_PROTOCOLS_VERSION

# Use ccache to speed up builds
apt-get install -y ccache

# We need xmllint to validate the XML files in Mesa
apt-get install -y libxml2-utils

# Remove unused packages
apt-get purge -y \
      automake \
      libtool \
      curl \
      unzip \
      wget \
      gnupg \
      software-properties-common
apt-get autoremove -y --purge
