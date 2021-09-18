#!/bin/bash

set -e
set -o xtrace


EPHEMERAL="
        autoconf
        automake
        bzip2
        git
        libtool
        pkgconfig(epoxy)
        pkgconfig(gbm)
        unzip
        wget
        xz
        "

dnf install -y --setopt=install_weak_deps=False \
    bison \
    ccache \
    clang-devel \
    flex \
    gcc \
    gcc-c++ \
    gettext \
    kernel-headers \
    llvm-devel \
    meson \
    "pkgconfig(dri2proto)" \
    "pkgconfig(expat)" \
    "pkgconfig(glproto)" \
    "pkgconfig(libclc)" \
    "pkgconfig(libelf)" \
    "pkgconfig(libglvnd)" \
    "pkgconfig(libomxil-bellagio)" \
    "pkgconfig(libselinux)" \
    "pkgconfig(libva)" \
    "pkgconfig(pciaccess)" \
    "pkgconfig(vdpau)" \
    "pkgconfig(vulkan)" \
    "pkgconfig(wayland-egl-backend)" \
    "pkgconfig(wayland-protocols)" \
    "pkgconfig(wayland-scanner)" \
    "pkgconfig(x11)" \
    "pkgconfig(x11-xcb)" \
    "pkgconfig(xcb)" \
    "pkgconfig(xcb-dri2)" \
    "pkgconfig(xcb-dri3)" \
    "pkgconfig(xcb-glx)" \
    "pkgconfig(xcb-present)" \
    "pkgconfig(xcb-randr)" \
    "pkgconfig(xcb-sync)" \
    "pkgconfig(xcb-xfixes)" \
    "pkgconfig(xdamage)" \
    "pkgconfig(xext)" \
    "pkgconfig(xfixes)" \
    "pkgconfig(xrandr)" \
    "pkgconfig(xshmfence)" \
    "pkgconfig(xxf86vm)" \
    "pkgconfig(zlib)" \
    python-unversioned-command \
    python3-devel \
    python3-mako \
    python3-devel \
    python3-mako \
    vulkan-headers \
    $EPHEMERAL


. .gitlab-ci/container/container_pre_build.sh


# dependencies where we want a specific version
export              XORG_RELEASES=https://xorg.freedesktop.org/releases/individual
export           WAYLAND_RELEASES=https://wayland.freedesktop.org/releases

export         XORGMACROS_VERSION=util-macros-1.19.0
export         LIBWAYLAND_VERSION=wayland-1.18.0

wget $XORG_RELEASES/util/$XORGMACROS_VERSION.tar.bz2
tar -xvf $XORGMACROS_VERSION.tar.bz2 && rm $XORGMACROS_VERSION.tar.bz2
cd $XORGMACROS_VERSION; ./configure; make install; cd ..
rm -rf $XORGMACROS_VERSION

. .gitlab-ci/container/build-libdrm.sh

wget $WAYLAND_RELEASES/$LIBWAYLAND_VERSION.tar.xz
tar -xvf $LIBWAYLAND_VERSION.tar.xz && rm $LIBWAYLAND_VERSION.tar.xz
cd $LIBWAYLAND_VERSION; ./configure --enable-libraries --without-host-scanner --disable-documentation --disable-dtd-validation; make install; cd ..
rm -rf $LIBWAYLAND_VERSION


pushd /usr/local
git clone https://gitlab.freedesktop.org/mesa/shader-db.git --depth 1
rm -rf shader-db/.git
cd shader-db
make
popd


############### Uninstall the build software

dnf remove -y $EPHEMERAL

. .gitlab-ci/container/container_post_build.sh
