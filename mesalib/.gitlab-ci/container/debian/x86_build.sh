#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

# Ephemeral packages (installed for this script and removed again at the end)
STABLE_EPHEMERAL=" \
      autoconf \
      automake \
      autotools-dev \
      bzip2 \
      libtool \
      python3-pip \
      "

# We need multiarch for Wine
dpkg --add-architecture i386
apt-get update

apt-get install -y --no-remove \
      $STABLE_EPHEMERAL \
      check \
      clang \
      cmake \
      libasan6 \
      libarchive-dev \
      libclang-cpp11-dev \
      libgbm-dev \
      libglvnd-dev \
      libllvmspirvlib-dev \
      liblua5.3-dev \
      libxcb-dri2-0-dev \
      libxcb-dri3-dev \
      libxcb-glx0-dev \
      libxcb-present-dev \
      libxcb-randr0-dev \
      libxcb-shm0-dev \
      libxcb-sync-dev \
      libxcb-xfixes0-dev \
      libxcb1-dev \
      libxml2-dev \
      llvm-11-dev \
      llvm-9-dev \
      ocl-icd-opencl-dev \
      procps \
      spirv-tools \
      strace \
      time \
      wine \
      wine32


. .gitlab-ci/container/container_pre_build.sh


# Debian's pkg-config wrapers for mingw are broken, and there's no sign that
# they're going to be fixed, so we'll just have to fix it ourselves
# https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=930492
cat >/usr/local/bin/x86_64-w64-mingw32-pkg-config <<EOF
#!/bin/sh

PKG_CONFIG_LIBDIR=/usr/x86_64-w64-mingw32/lib/pkgconfig pkg-config \$@
EOF
chmod +x /usr/local/bin/x86_64-w64-mingw32-pkg-config


# dependencies where we want a specific version
export              XORG_RELEASES=https://xorg.freedesktop.org/releases/individual

export         XORGMACROS_VERSION=util-macros-1.19.0

wget $XORG_RELEASES/util/$XORGMACROS_VERSION.tar.bz2
tar -xvf $XORGMACROS_VERSION.tar.bz2 && rm $XORGMACROS_VERSION.tar.bz2
cd $XORGMACROS_VERSION; ./configure; make install; cd ..
rm -rf $XORGMACROS_VERSION

. .gitlab-ci/container/build-libdrm.sh

. .gitlab-ci/container/build-wayland.sh

pushd /usr/local
git clone https://gitlab.freedesktop.org/mesa/shader-db.git --depth 1
rm -rf shader-db/.git
cd shader-db
make
popd

git clone https://github.com/microsoft/DirectX-Headers -b v1.0.1 --depth 1
pushd DirectX-Headers
mkdir build
cd build
meson .. --backend=ninja --buildtype=release -Dbuild-test=false
ninja
ninja install
popd
rm -rf DirectX-Headers

pip3 install git+https://git.lavasoftware.org/lava/lavacli@3db3ddc45e5358908bc6a17448059ea2340492b7

############### Uninstall the build software

apt-get purge -y \
      $STABLE_EPHEMERAL

. .gitlab-ci/container/container_post_build.sh
