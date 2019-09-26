#!/bin/bash

set -e
set -o xtrace

############### Install packages for building
apt-get update
apt-get -y install ca-certificates
apt-get -y install --no-install-recommends \
	g++ \
	git \
	pkg-config \
	python \
	python3-pip \
	python3-setuptools \
	bison \
	flex \
	gettext \
	cmake \
	ninja-build \
	bc \
	bzip2 \
	libssl-dev \
	curl \
	unzip \
	wget \
	procps \
	libexpat1 \
	libelf1 \
	zlib1g-dev \
	libpng-dev \
	libgbm-dev \
	libgles2-mesa-dev

export             LIBDRM_VERSION=libdrm-2.4.99

pip3 install meson

############### Build libdrm

wget https://dri.freedesktop.org/libdrm/$LIBDRM_VERSION.tar.bz2
tar -xvf $LIBDRM_VERSION.tar.bz2 && rm $LIBDRM_VERSION.tar.bz2
cd $LIBDRM_VERSION; meson build/ -Detnaviv=true; ninja -C build/ install; cd ..
rm -rf $LIBDRM_VERSION

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

# --insecure is due to SSL cert failures hitting sourceforge for zlib and
# libpng (sigh).  The archives get their checksums checked anyway, and git
# always goes through ssh or https.
python3 external/fetch_sources.py --insecure

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
        cmake \
        git \
        gcc \
        g++ \
        bison \
        flex \
        ninja-build
