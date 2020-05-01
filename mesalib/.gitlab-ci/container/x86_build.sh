#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

CROSS_ARCHITECTURES="i386 ppc64el s390x"
for arch in $CROSS_ARCHITECTURES; do
    dpkg --add-architecture $arch
done

apt-get install -y \
      ca-certificates \
      gnupg \
      unzip \
      wget

# Upstream LLVM package repository
apt-key add .gitlab-ci/container/llvm-snapshot.gpg.key
echo "deb https://apt.llvm.org/buster/ llvm-toolchain-buster-9 main" >/etc/apt/sources.list.d/llvm9.list

# Upstream Wine (WineHQ) package repository. We use the OBS service
# instead of the repository at the winehq.org domain because:
#
#   " The WineHQ packages for Debian 10 and later require libfaudio0
#     as a dependency. Since the distro does not provide it for Debian
#     10, users of that version can download libfaudio0 packages from
#     the OBS. See https://forum.winehq.org/viewtopic.php?f=8&t=32192
#     for details."
#
# As explained at https://wiki.winehq.org/Debian
apt-key add .gitlab-ci/container/obs-emulators-wine-debian.gpg.key
echo 'deb https://download.opensuse.org/repositories/Emulators:/Wine:/Debian/Debian_10/ ./' >/etc/apt/sources.list.d/obs-emulators-wine-debian.list

sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
echo 'deb https://deb.debian.org/debian buster-backports main' >/etc/apt/sources.list.d/backports.list

apt-get update
apt-get dist-upgrade -y

apt-get install -y --no-remove \
      autoconf \
      automake \
      autotools-dev \
      bison \
      ccache \
      clang-9 \
      cmake \
      flex \
      g++ \
      gcc \
      gettext \
      git \
      libclang-6.0-dev \
      libclang-7-dev \
      libclang-8-dev \
      libclang-9-dev \
      libclc-dev \
      libelf-dev \
      libepoxy-dev \
      libexpat1-dev \
      libgbm-dev \
      libgtk-3-dev \
      libomxil-bellagio-dev \
      libpciaccess-dev \
      libtool \
      libunwind-dev \
      libva-dev \
      libvdpau-dev \
      libvulkan-dev \
      libvulkan-dev:ppc64el \
      libx11-dev \
      libx11-xcb-dev \
      libxdamage-dev \
      libxext-dev \
      libxml2-utils \
      libxrandr-dev \
      libxrender-dev \
      libxshmfence-dev \
      libxvmc-dev \
      libxxf86vm-dev \
      llvm-6.0-dev \
      llvm-7-dev \
      llvm-9-dev \
      meson \
      pkg-config \
      python-mako \
      python3-mako \
      python3-pil \
      python3-requests \
      qemu-user \
      scons \
      x11proto-dri2-dev \
      x11proto-gl-dev \
      x11proto-randr-dev \
      xz-utils \
      zlib1g-dev

. .gitlab-ci/container/container_pre_build.sh

# Cross-build Mesa deps
for arch in $CROSS_ARCHITECTURES; do
    apt-get install -y --no-remove \
            crossbuild-essential-${arch} \
            libdrm-dev:${arch} \
            libelf-dev:${arch} \
            libexpat1-dev:${arch} \
            libffi-dev:${arch} \
            libllvm8:${arch} \
            libstdc++6:${arch} \
            libtinfo-dev:${arch}

    if [ "$arch" == "i386" ]; then
        # libpciaccess-dev is only needed for Intel.
        apt-get install -y --no-remove \
            libpciaccess-dev:${arch}
    fi

    mkdir /var/cache/apt/archives/${arch}
    # Download llvm-* packages, but don't install them yet, since they can
    # only be installed for one architecture at a time
    apt-get install -o Dir::Cache::archives=/var/cache/apt/archives/$arch --download-only -y --no-remove \
       llvm-8-dev:${arch}
done

apt-get install -y --no-remove \
      llvm-8-dev \

# for 64bit windows cross-builds
apt-get install -y --no-remove \
      libz-mingw-w64-dev \
      mingw-w64 \
      winehq-stable

# Debian's pkg-config wrapers for mingw are broken, and there's no sign that
# they're going to be fixed, so we'll just have to fix it ourselves
# https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=930492
cat >/usr/local/bin/x86_64-w64-mingw32-pkg-config <<EOF
#!/bin/sh

PKG_CONFIG_LIBDIR=/usr/x86_64-w64-mingw32/lib/pkgconfig pkg-config \$@
EOF
chmod +x /usr/local/bin/x86_64-w64-mingw32-pkg-config


# Generate cross build files for Meson
for arch in $CROSS_ARCHITECTURES; do
  cross_file="/cross_file-$arch.txt"
  /usr/share/meson/debcrossgen --arch "$arch" -o "$cross_file"
  # Explicitly set ccache path for cross compilers
  sed -i "s|/usr/bin/\([^-]*\)-linux-gnu\([^-]*\)-g|/usr/lib/ccache/\\1-linux-gnu\\2-g|g" "$cross_file"
  if [ "$arch" = "i386" ]; then
    # Work around a bug in debcrossgen that should be fixed in the next release
    sed -i "s|cpu_family = 'i686'|cpu_family = 'x86'|g" "$cross_file"
  fi

  # Rely on qemu-user being configured in binfmt_misc on the host
  sed -i -e '/\[properties\]/a\' -e "needs_exe_wrapper = False" "$cross_file"
done


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
export             LIBDRM_VERSION=libdrm-2.4.100
export           XCBPROTO_VERSION=xcb-proto-1.13
export             LIBXCB_VERSION=libxcb-1.13
export         LIBWAYLAND_VERSION=wayland-1.15.0
export  WAYLAND_PROTOCOLS_VERSION=wayland-protocols-1.12

wget $XORG_RELEASES/util/$XORGMACROS_VERSION.tar.bz2
tar -xvf $XORGMACROS_VERSION.tar.bz2 && rm $XORGMACROS_VERSION.tar.bz2
cd $XORGMACROS_VERSION; ./configure; make install; cd ..
rm -rf $XORGMACROS_VERSION

wget $XCB_RELEASES/$XCBPROTO_VERSION.tar.bz2
tar -xvf $XCBPROTO_VERSION.tar.bz2 && rm $XCBPROTO_VERSION.tar.bz2
cd $XCBPROTO_VERSION; ./configure; make install; cd ..
rm -rf $XCBPROTO_VERSION

wget $XCB_RELEASES/$LIBXCB_VERSION.tar.bz2
tar -xvf $LIBXCB_VERSION.tar.bz2 && rm $LIBXCB_VERSION.tar.bz2
cd $LIBXCB_VERSION; ./configure; make install; cd ..
rm -rf $LIBXCB_VERSION

wget https://dri.freedesktop.org/libdrm/$LIBDRM_VERSION.tar.bz2
tar -xvf $LIBDRM_VERSION.tar.bz2 && rm $LIBDRM_VERSION.tar.bz2
cd $LIBDRM_VERSION
meson build -D vc4=true -D freedreno=true -D etnaviv=true -D libdir=lib/x86_64-linux-gnu; ninja -C build install
rm -rf build; meson --cross-file=/cross_file-ppc64el.txt build -D libdir=lib/powerpc64le-linux-gnu; ninja -C build install
rm -rf build; meson --cross-file=/cross_file-i386.txt build -D libdir=lib/i386-linux-gnu; ninja -C build install
cd ..
rm -rf $LIBDRM_VERSION

wget $WAYLAND_RELEASES/$LIBWAYLAND_VERSION.tar.xz
tar -xvf $LIBWAYLAND_VERSION.tar.xz && rm $LIBWAYLAND_VERSION.tar.xz
cd $LIBWAYLAND_VERSION; ./configure --enable-libraries --without-host-scanner --disable-documentation --disable-dtd-validation; make install; cd ..
rm -rf $LIBWAYLAND_VERSION

wget $WAYLAND_RELEASES/$WAYLAND_PROTOCOLS_VERSION.tar.xz
tar -xvf $WAYLAND_PROTOCOLS_VERSION.tar.xz && rm $WAYLAND_PROTOCOLS_VERSION.tar.xz
cd $WAYLAND_PROTOCOLS_VERSION; ./configure; make install; cd ..
rm -rf $WAYLAND_PROTOCOLS_VERSION


# The version of libglvnd-dev in debian is too old
# Check this page to see when this local compilation can be dropped in favour of the package:
# https://packages.debian.org/libglvnd-dev
GLVND_VERSION=1.2.0
wget https://gitlab.freedesktop.org/glvnd/libglvnd/-/archive/v$GLVND_VERSION/libglvnd-v$GLVND_VERSION.tar.gz
tar -xvf libglvnd-v$GLVND_VERSION.tar.gz && rm libglvnd-v$GLVND_VERSION.tar.gz
pushd libglvnd-v$GLVND_VERSION; ./autogen.sh; ./configure; make install; popd
rm -rf libglvnd-v$GLVND_VERSION


pushd /usr/local
git clone https://gitlab.freedesktop.org/mesa/shader-db.git --depth 1
rm -rf shader-db/.git
cd shader-db
make
popd


############### Uninstall the build software

apt-get purge -y \
      autoconf \
      automake \
      autotools-dev \
      cmake \
      git \
      gnupg \
      libgbm-dev \
      libtool \
      unzip \
      wget

. .gitlab-ci/container/container_post_build.sh
