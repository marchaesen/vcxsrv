#!/bin/bash

set -e
set -o xtrace

############### Install packages for building
dpkg --add-architecture ${DEBIAN_ARCH}
echo 'deb-src https://deb.debian.org/debian testing main' > /etc/apt/sources.list.d/deb-src.list
apt-get update
apt-get -y install ca-certificates
apt-get -y install --no-install-recommends \
	crossbuild-essential-${DEBIAN_ARCH} \
	meson \
	g++ \
	git \
	ccache \
	pkg-config \
	python3-mako \
	python-numpy \
	python-six \
	python-mako \
	python3-pip \
	python3-setuptools \
	python3-six \
	python3-wheel \
	python3-jinja2 \
	bison \
	flex \
	gettext \
	cmake \
	bc \
	libssl-dev \
	lqa \
	csvkit \
	curl \
	unzip \
	wget \
	debootstrap \
	procps \
	qemu-user-static \
	cpio \
	clang-8 \
	llvm-8 \
	libclang-8-dev \
	llvm-8-dev \
	gdc-9 \
	lld-8 \
	nasm \
	libegl1-mesa-dev \
	\
	libdrm-dev:${DEBIAN_ARCH} \
	libx11-dev:${DEBIAN_ARCH} \
	libxxf86vm-dev:${DEBIAN_ARCH} \
	libexpat1-dev:${DEBIAN_ARCH} \
	libsensors-dev:${DEBIAN_ARCH} \
	libxfixes-dev:${DEBIAN_ARCH} \
	libxdamage-dev:${DEBIAN_ARCH} \
	libxext-dev:${DEBIAN_ARCH} \
	x11proto-dev:${DEBIAN_ARCH} \
	libx11-xcb-dev:${DEBIAN_ARCH} \
	libxcb-dri2-0-dev:${DEBIAN_ARCH} \
	libxcb-glx0-dev:${DEBIAN_ARCH} \
	libxcb-xfixes0-dev:${DEBIAN_ARCH} \
	libxcb-dri3-dev:${DEBIAN_ARCH} \
	libxcb-present-dev:${DEBIAN_ARCH} \
	libxcb-randr0-dev:${DEBIAN_ARCH} \
	libxcb-sync-dev:${DEBIAN_ARCH} \
	libxrandr-dev:${DEBIAN_ARCH} \
	libxshmfence-dev:${DEBIAN_ARCH} \
	libelf-dev:${DEBIAN_ARCH} \
	zlib1g-dev:${DEBIAN_ARCH} \
	libglvnd-core-dev:${DEBIAN_ARCH} \
	libgles2-mesa-dev:${DEBIAN_ARCH} \
	libegl1-mesa-dev:${DEBIAN_ARCH} \
	libpng-dev:${DEBIAN_ARCH}


############### Install lavacli (remove after it's back into Debian testing)
mkdir -p lavacli
wget -qO- https://git.lavasoftware.org/lava/lavacli/-/archive/v0.9.8/lavacli-v0.9.8.tar.gz | tar -xz --strip-components=1 -C lavacli
pushd lavacli
python3 ./setup.py install
popd


############### Cross-build dEQP
mkdir -p /artifacts/rootfs/deqp

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

cd /artifacts/rootfs/deqp
cmake -G Ninja                                \
      -DDEQP_TARGET=surfaceless               \
      -DCMAKE_BUILD_TYPE=Release              \
      -DCMAKE_C_COMPILER=${GCC_ARCH}-gcc      \
      -DCMAKE_CXX_COMPILER=${GCC_ARCH}-g++    \
      /VK-GL-CTS
ninja
rm -rf /artifacts/rootfs/deqp/external
rm -rf /artifacts/rootfs/deqp/modules/gles31
rm -rf /artifacts/rootfs/deqp/modules/internal
rm -rf /artifacts/rootfs/deqp/executor
rm -rf /artifacts/rootfs/deqp/execserver
rm -rf /artifacts/rootfs/deqp/modules/egl
rm -rf /artifacts/rootfs/deqp/framework
find . -name CMakeFiles | xargs rm -rf
find . -name lib\*.a | xargs rm -rf
du -sh *
rm -rf /VK-GL-CTS-opengl-es-cts-3.2.5.0


############### Cross-build Volt dEQP runner
mkdir -p /battery
cd /battery
wget https://github.com/VoltLang/Battery/releases/download/v0.1.23/battery-0.1.23-x86_64-linux.tar.gz
tar xzvf battery-0.1.23-x86_64-linux.tar.gz
rm battery-0.1.23-x86_64-linux.tar.gz
mv battery /usr/local/bin
rm -rf /battery

mkdir -p /volt
cd /volt
mkdir -p Watt Volta dEQP
wget -qO- https://github.com/VoltLang/Watt/archive/v0.1.3.tar.gz | tar -xz --strip-components=1 -C ./Watt
wget -qO- https://github.com/VoltLang/Volta/archive/v0.1.3.tar.gz | tar -xz --strip-components=1 -C ./Volta
wget -qO- https://github.com/Wallbraker/dEQP/archive/v0.1.4.tar.gz | tar -xz --strip-components=1 -C ./dEQP
battery config --release --lto Volta Watt
battery build
battery config --arch ${VOLT_ARCH} --cmd-volta Volta/volta Volta/rt Watt dEQP
battery build
rm /usr/local/bin/battery
cp dEQP/deqp /artifacts/rootfs/deqp/deqp-volt
rm -rf /volt


############### Remove LLVM now, so the container image is smaller
apt-get -y remove \*llvm\*


############### Cross-build kernel
KERNEL_URL="https://gitlab.freedesktop.org/tomeu/linux/-/archive/panfrost-veyron-fix/linux-panfrost-veyron-fix.tar.gz"
export ARCH=${KERNEL_ARCH}
export CROSS_COMPILE="${GCC_ARCH}-"

mkdir -p /kernel
wget -qO- ${KERNEL_URL} | tar -xz --strip-components=1 -C /kernel
cd /kernel
./scripts/kconfig/merge_config.sh ${DEFCONFIG} /tmp/clone/.gitlab-ci/${KERNEL_ARCH}.config
make -j12 ${KERNEL_IMAGE_NAME} dtbs
cp arch/${KERNEL_ARCH}/boot/${KERNEL_IMAGE_NAME} /artifacts/.
cp ${DEVICE_TREES} /artifacts/.
rm -rf /kernel


############### Create rootfs
cp /tmp/clone/.gitlab-ci/create-rootfs.sh /artifacts/rootfs/.
mkdir -p /artifacts/rootfs/bin
cp /usr/bin/qemu-aarch64-static /artifacts/rootfs/bin
cp /usr/bin/qemu-arm-static /artifacts/rootfs/bin

set +e
debootstrap --variant=minbase --arch=${DEBIAN_ARCH} testing /artifacts/rootfs/ http://deb.debian.org/debian
cat /artifacts/rootfs/debootstrap/debootstrap.log
set -e
chroot /artifacts/rootfs sh /create-rootfs.sh

rm /artifacts/rootfs/bin/qemu-arm-static
rm /artifacts/rootfs/bin/qemu-aarch64-static
rm /artifacts/rootfs/create-rootfs.sh

