#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

CROSS_ARCHITECTURES="armhf arm64 i386"
for arch in $CROSS_ARCHITECTURES; do
    dpkg --add-architecture $arch
done

apt-get install -y \
      ca-certificates \
      wget \
      unzip

sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
echo 'deb https://deb.debian.org/debian buster-backports main' >/etc/apt/sources.list.d/backports.list

apt-get update

# Use newer packages from backports by default
cat >/etc/apt/preferences <<EOF
Package: *
Pin: release a=buster-backports
Pin-Priority: 500
EOF

apt-get dist-upgrade -y

apt-get install -y --no-remove \
      llvm-6.0-dev \
      libclang-6.0-dev \
      llvm-7-dev \
      libclang-7-dev \
      llvm-8-dev \
      libclang-8-dev \
      g++ \
      clang-8 \
      git \
      bzip2 \
      zlib1g-dev \
      pkg-config \
      libxrender-dev \
      libxdamage-dev \
      libxxf86vm-dev \
      gcc \
      git \
      libepoxy-dev \
      libegl1-mesa-dev \
      libgbm-dev \
      libclc-dev \
      libxvmc-dev \
      libomxil-bellagio-dev \
      xz-utils \
      libexpat1-dev \
      libx11-xcb-dev \
      libelf-dev \
      libunwind-dev \
      libglvnd-dev \
      libgtk-3-dev \
      libpng-dev \
      libgbm-dev \
      libgles2-mesa-dev \
      python-mako \
      python3-mako \
      bison \
      flex \
      gettext \
      cmake \
      meson \
      scons

# Cross-build Mesa deps
for arch in $CROSS_ARCHITECTURES; do
    apt-get install -y --no-remove \
            libdrm-dev:${arch} \
            libexpat1-dev:${arch} \
            libelf-dev:${arch} \
            crossbuild-essential-${arch}
done

# for 64bit windows cross-builds
apt-get install -y --no-remove mingw-w64

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
export         RANDRPROTO_VERSION=randrproto-1.5.0
export          LIBXRANDR_VERSION=libXrandr-1.5.0
export             LIBXCB_VERSION=libxcb-1.13
export       LIBXSHMFENCE_VERSION=libxshmfence-1.3
export           LIBVDPAU_VERSION=libvdpau-1.1
export              LIBVA_VERSION=libva-1.7.0
export         LIBWAYLAND_VERSION=wayland-1.15.0
export  WAYLAND_PROTOCOLS_VERSION=wayland-protocols-1.12

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

pushd /usr/local
git clone https://gitlab.freedesktop.org/mesa/shader-db.git --depth 1
rm -rf shader-db/.git
cd shader-db
make
popd

# Use ccache to speed up builds
apt-get install -y --no-remove ccache

# We need xmllint to validate the XML files in Mesa
apt-get install -y --no-remove libxml2-utils


# Generate cross build files for Meson
for arch in $CROSS_ARCHITECTURES; do
  cross_file="/cross_file-$arch.txt"
  /usr/share/meson/debcrossgen --arch "$arch" -o "$cross_file"
  # Work around a bug in debcrossgen that should be fixed in the next release
  if [ "$arch" = "i386" ]; then
    sed -i "s|cpu_family = 'i686'|cpu_family = 'x86'|g" "$cross_file"
  fi
done


############### Build dEQP
git config --global user.email "mesa@example.com"
git config --global user.name "Mesa CI"
# XXX: Use --depth 1 once we can drop the cherry-picks.
git clone \
    https://github.com/KhronosGroup/VK-GL-CTS.git \
    -b opengl-es-cts-3.2.5.1 \
    /VK-GL-CTS
cd /VK-GL-CTS
# Fix surfaceless build
git cherry-pick -x 22f41e5e321c6dcd8569c4dad91bce89f06b3670
git cherry-pick -x 1daa8dff73161ea60ead965bd6c9f2a0a2165648

# surfaceless links against libkms and such despite not using it.
sed -i '/gbm/d' targets/surfaceless/surfaceless.cmake
sed -i '/libkms/d' targets/surfaceless/surfaceless.cmake
sed -i '/libgbm/d' targets/surfaceless/surfaceless.cmake

python3 external/fetch_sources.py

mkdir -p /deqp
cd /deqp
cmake -G Ninja \
      -DDEQP_TARGET=surfaceless               \
      -DCMAKE_BUILD_TYPE=Release              \
      /VK-GL-CTS
ninja

# Copy out the mustpass lists we want from a bunch of other junk.
mkdir /deqp/mustpass
for gles in gles2 gles3 gles31; do
    cp \
        /deqp/external/openglcts/modules/gl_cts/data/mustpass/gles/aosp_mustpass/3.2.5.x/$gles-master.txt \
        /deqp/mustpass/$gles-master.txt
done

# Remove the rest of the build products that we don't need.
rm -rf /deqp/external
rm -rf /deqp/modules/internal
rm -rf /deqp/executor
rm -rf /deqp/execserver
rm -rf /deqp/modules/egl
rm -rf /deqp/framework
du -sh *
rm -rf /VK-GL-CTS

############### Uninstall the build software

apt-get purge -y \
      wget \
      unzip \
      cmake \
      git \
      libgles2-mesa-dev \
      libgbm-dev

apt-get autoremove -y --purge
