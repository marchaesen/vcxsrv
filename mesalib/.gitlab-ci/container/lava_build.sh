#!/usr/bin/env bash
# shellcheck disable=SC1091 # The relative paths in this file only become valid at runtime.
# shellcheck disable=SC2034 # Variables are used in scripts called from here
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC2016 # non-expanded variables are intentional
# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# KERNEL_ROOTFS_TAG
# If you need to update the fluster vectors cache without updating the fluster revision,
# you can update the FLUSTER_VECTORS_VERSION tag in .gitlab-ci/image-tags.yml.
# When changing FLUSTER_REVISION, KERNEL_ROOTFS_TAG needs to be updated as well to rebuild
# the rootfs.

set -e

. .gitlab-ci/setup-test-env.sh

set -o xtrace

export DEBIAN_FRONTEND=noninteractive
: "${LLVM_VERSION:?llvm version not set!}"
export FIRMWARE_FILES="${FIRMWARE_FILES}"
export SKIP_UPDATE_FLUSTER_VECTORS=0

check_minio()
{
    S3_PATH="${S3_HOST}/${S3_KERNEL_BUCKET}/$1/${DISTRIBUTION_TAG}/${DEBIAN_ARCH}"
    if curl -L --retry 4 -f --retry-delay 60 -s -X HEAD \
      "https://${S3_PATH}/done"; then
        echo "Remote files are up-to-date, skip rebuilding them."
        exit
    fi
}

check_fluster()
{
    S3_PATH_FLUSTER="${S3_HOST}/${S3_KERNEL_BUCKET}/$1/${DATA_STORAGE_PATH}/fluster/${FLUSTER_VECTORS_VERSION}"
    if curl -L --retry 4 -f --retry-delay 60 -s -X HEAD \
      "https://${S3_PATH_FLUSTER}/done"; then
        echo "Fluster vectors are up-to-date, skip downloading them."
        export SKIP_UPDATE_FLUSTER_VECTORS=1
    fi
}

check_minio "${FDO_UPSTREAM_REPO}"
check_minio "${CI_PROJECT_PATH}"

check_fluster "${FDO_UPSTREAM_REPO}"
check_fluster "${CI_PROJECT_PATH}"

. .gitlab-ci/container/container_pre_build.sh

# Install rust, which we'll be using for deqp-runner.  It will be cleaned up at the end.
. .gitlab-ci/container/build-rust.sh

if [[ "$DEBIAN_ARCH" = "arm64" ]]; then
    BUILD_CL="ON"
    BUILD_VK="ON"
    GCC_ARCH="aarch64-linux-gnu"
    KERNEL_ARCH="arm64"
    DEFCONFIG="arch/arm64/configs/defconfig"
    DEVICE_TREES="rk3399-gru-kevin.dtb"
    DEVICE_TREES+=" meson-g12b-a311d-khadas-vim3.dtb"
    DEVICE_TREES+=" meson-gxl-s805x-libretech-ac.dtb"
    DEVICE_TREES+=" meson-gxm-khadas-vim2.dtb"
    DEVICE_TREES+=" sun50i-h6-pine-h64.dtb"
    DEVICE_TREES+=" imx8mq-nitrogen.dtb"
    DEVICE_TREES+=" mt8192-asurada-spherion-r0.dtb"
    DEVICE_TREES+=" mt8183-kukui-jacuzzi-juniper-sku16.dtb"
    DEVICE_TREES+=" tegra210-p3450-0000.dtb"
    DEVICE_TREES+=" apq8016-sbc-usb-host.dtb"
    DEVICE_TREES+=" apq8096-db820c.dtb"
    DEVICE_TREES+=" sc7180-trogdor-lazor-limozeen-nots-r5.dtb"
    DEVICE_TREES+=" sc7180-trogdor-kingoftown.dtb"
    DEVICE_TREES+=" sm8350-hdk.dtb"
    KERNEL_IMAGE_NAME="Image"

elif [[ "$DEBIAN_ARCH" = "armhf" ]]; then
    BUILD_CL="OFF"
    BUILD_VK="OFF"
    GCC_ARCH="arm-linux-gnueabihf"
    KERNEL_ARCH="arm"
    DEFCONFIG="arch/arm/configs/multi_v7_defconfig"
    DEVICE_TREES="rk3288-veyron-jaq.dtb"
    DEVICE_TREES+=" sun8i-h3-libretech-all-h3-cc.dtb"
    DEVICE_TREES+=" imx6q-cubox-i.dtb"
    DEVICE_TREES+=" tegra124-jetson-tk1.dtb"
    KERNEL_IMAGE_NAME="zImage"
    . .gitlab-ci/container/create-cross-file.sh armhf
    CONTAINER_ARCH_PACKAGES=(
      libegl1-mesa-dev:armhf
      libelf-dev:armhf
      libgbm-dev:armhf
      libgles2-mesa-dev:armhf
      libpng-dev:armhf
      libudev-dev:armhf
      libvulkan-dev:armhf
      libwaffle-dev:armhf
      libwayland-dev:armhf
      libx11-xcb-dev:armhf
      libxkbcommon-dev:armhf
    )
else
    BUILD_CL="ON"
    BUILD_VK="ON"
    GCC_ARCH="x86_64-linux-gnu"
    KERNEL_ARCH="x86_64"
    DEFCONFIG="arch/x86/configs/x86_64_defconfig"
    DEVICE_TREES=""
    KERNEL_IMAGE_NAME="bzImage"
    CONTAINER_ARCH_PACKAGES=(
      libasound2-dev libcap-dev libfdt-dev libva-dev p7zip wine
    )
fi

# Determine if we're in a cross build.
if [[ -e /cross_file-$DEBIAN_ARCH.txt ]]; then
    EXTRA_MESON_ARGS="--cross-file /cross_file-$DEBIAN_ARCH.txt"
    EXTRA_CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=/toolchain-$DEBIAN_ARCH.cmake"

    if [ $DEBIAN_ARCH = arm64 ]; then
        RUST_TARGET="aarch64-unknown-linux-gnu"
    elif [ $DEBIAN_ARCH = armhf ]; then
        RUST_TARGET="armv7-unknown-linux-gnueabihf"
    fi
    rustup target add $RUST_TARGET
    export EXTRA_CARGO_ARGS="--target $RUST_TARGET"

    export ARCH=${KERNEL_ARCH}
    export CROSS_COMPILE="${GCC_ARCH}-"
fi

# no need to remove these at end, image isn't saved at the end
CONTAINER_EPHEMERAL=(
    arch-test
    automake
    bc
    "clang-${LLVM_VERSION}"
    cmake
    curl
    mmdebstrap
    git
    glslang-tools
    jq
    libdrm-dev
    libegl1-mesa-dev
    libxext-dev
    libfontconfig-dev
    libgbm-dev
    libgl-dev
    libgles2-mesa-dev
    libglu1-mesa-dev
    libglx-dev
    libpng-dev
    libssl-dev
    libudev-dev
    libvulkan-dev
    libwaffle-dev
    libwayland-dev
    libx11-xcb-dev
    libxcb-dri2-0-dev
    libxkbcommon-dev
    libwayland-dev
    "lld-${LLVM_VERSION}"
    ninja-build
    openssh-server
    patch
    protobuf-compiler
    python-is-python3
    python3-distutils
    python3-mako
    python3-numpy
    python3-serial
    python3-venv
    unzip
    wayland-protocols
    zstd
)

[ "$BUILD_CL" == "ON" ] && CONTAINER_EPHEMERAL+=(
	ocl-icd-opencl-dev
)


echo "deb [trusted=yes] https://gitlab.freedesktop.org/gfx-ci/ci-deb-repo/-/raw/${PKG_REPO_REV}/ ${FDO_DISTRIBUTION_VERSION%-*} main" | tee /etc/apt/sources.list.d/gfx-ci_.list

. .gitlab-ci/container/debian/maybe-add-llvm-repo.sh

apt-get update
apt-get install -y --no-remove \
		   -o Dpkg::Options::='--force-confdef' -o Dpkg::Options::='--force-confold' \
		   "${CONTAINER_EPHEMERAL[@]}" \
                   "${CONTAINER_ARCH_PACKAGES[@]}" \
                   ${EXTRA_LOCAL_PACKAGES}

export ROOTFS=/lava-files/rootfs-${DEBIAN_ARCH}
mkdir -p "$ROOTFS"

# rootfs packages
PKG_BASE=(
  tzdata mount
)
PKG_CI=(
  firmware-realtek
  bash ca-certificates curl
  initramfs-tools jq netcat-openbsd dropbear openssh-server
  libasan8
  libubsan1
  git
  python3-dev python3-pip python3-setuptools python3-wheel
  weston # Wayland
  xinit xserver-xorg-core xwayland # X11
)
PKG_MESA_DEP=(
  libdrm2 libsensors5 libexpat1 # common
  libvulkan1 # vulkan
  libx11-6 libx11-xcb1 libxcb-dri2-0 libxcb-dri3-0 libxcb-glx0 libxcb-present0 libxcb-randr0 libxcb-shm0 libxcb-sync1 libxcb-xfixes0 libxdamage1 libxext6 libxfixes3 libxkbcommon0 libxrender1 libxshmfence1 libxxf86vm1 # X11
)
PKG_DEP=(
  libpng16-16
  libva-wayland2
  libwaffle-1-0
  libpython3.11 python3 python3-lxml python3-mako python3-numpy python3-packaging python3-pil python3-renderdoc python3-requests python3-simplejson python3-yaml # Python
  sntp
  strace
  waffle-utils
  zstd
)
# arch dependent rootfs packages
[ "$DEBIAN_ARCH" = "arm64" ] && PKG_ARCH=(
  libgl1 libglu1-mesa
  firmware-linux-nonfree firmware-qcom-media
  libfontconfig1
)
[ "$DEBIAN_ARCH" = "amd64" ] && PKG_ARCH=(
  firmware-amd-graphics
  firmware-misc-nonfree
  gstreamer1.0-plugins-bad gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-ugly gstreamer1.0-tools gstreamer1.0-vaapi libgstreamer1.0-0 # Fluster
  libgl1 libglu1-mesa
  inetutils-syslogd iptables libcap2
  libfontconfig1
  spirv-tools
  libelf1 libfdt1 "libllvm${LLVM_VERSION}"
  libva2 libva-drm2
  socat
  sysvinit-core
  wine
)
[ "$DEBIAN_ARCH" = "armhf" ] && PKG_ARCH=(
  firmware-misc-nonfree
)

[ "$BUILD_CL" == "ON" ] && PKG_ARCH+=(
	clinfo
	"libclang-cpp${LLVM_VERSION}"
	"libclang-common-${LLVM_VERSION}-dev"
	ocl-icd-libopencl1
)
[ "$BUILD_VK" == "ON" ] && PKG_ARCH+=(
	libvulkan-dev
)

mmdebstrap \
    --variant=apt \
    --arch="${DEBIAN_ARCH}" \
    --components main,contrib,non-free-firmware \
    --customize-hook='.gitlab-ci/container/get-firmware-from-source.sh "$ROOTFS" "$FIRMWARE_FILES"' \
    --include "${PKG_BASE[*]} ${PKG_CI[*]} ${PKG_DEP[*]} ${PKG_MESA_DEP[*]} ${PKG_ARCH[*]}" \
    bookworm \
    "$ROOTFS/" \
    "http://deb.debian.org/debian" \
    "deb [trusted=yes] https://gitlab.freedesktop.org/gfx-ci/ci-deb-repo/-/raw/${PKG_REPO_REV}/ ${FDO_DISTRIBUTION_VERSION%-*} main" \
    "${LLVM_APT_REPO:-}"

############### Install mold
. .gitlab-ci/container/build-mold.sh

############### Building
STRIP_CMD="${GCC_ARCH}-strip"
mkdir -p $ROOTFS/usr/lib/$GCC_ARCH

############### Build libclc

if [ "$BUILD_CL" = "ON" ]; then
  rm -rf /usr/lib/clc/*
  . .gitlab-ci/container/build-libclc.sh
  mkdir -p $ROOTFS/usr/{share,lib}/clc
  mv /usr/share/clc/spirv*-mesa3d-.spv $ROOTFS/usr/share/clc/
  ln -s /usr/share/clc/spirv64-mesa3d-.spv $ROOTFS/usr/lib/clc/
  ln -s /usr/share/clc/spirv-mesa3d-.spv $ROOTFS/usr/lib/clc/
fi

############### Build Vulkan validation layer (for zink)
if [ "$DEBIAN_ARCH" = "amd64" ]; then
  . .gitlab-ci/container/build-vulkan-validation.sh
  mv /usr/lib/x86_64-linux-gnu/libVkLayer_khronos_validation.so $ROOTFS/usr/lib/x86_64-linux-gnu/
  mkdir -p $ROOTFS/usr/share/vulkan/explicit_layer.d
  mv /usr/share/vulkan/explicit_layer.d/* $ROOTFS/usr/share/vulkan/explicit_layer.d/
fi

############### Build apitrace
. .gitlab-ci/container/build-apitrace.sh
mkdir -p $ROOTFS/apitrace
mv /apitrace/build $ROOTFS/apitrace
rm -rf /apitrace

############### Build ANGLE
if [ "$DEBIAN_ARCH" != "armhf" ]; then
  ANGLE_TARGET=linux \
  . .gitlab-ci/container/build-angle.sh
  mv /angle $ROOTFS/.
  rm -rf /angle
fi

############### Build dEQP runner
. .gitlab-ci/container/build-deqp-runner.sh
mkdir -p $ROOTFS/usr/bin
mv /usr/local/bin/*-runner $ROOTFS/usr/bin/.


############### Build dEQP

DEQP_API=tools \
DEQP_TARGET=default \
. .gitlab-ci/container/build-deqp.sh

DEQP_API=GL \
DEQP_TARGET=surfaceless \
. .gitlab-ci/container/build-deqp.sh

DEQP_API=GLES \
DEQP_TARGET=surfaceless \
. .gitlab-ci/container/build-deqp.sh

if [ "$BUILD_VK" == "ON" ]; then
  DEQP_API=VK \
  DEQP_TARGET=default \
  . .gitlab-ci/container/build-deqp.sh

  if [ "$DEBIAN_ARCH" == "amd64" ]; then
    DEQP_API=VK-main \
    DEQP_TARGET=default \
    . .gitlab-ci/container/build-deqp.sh
  fi
fi

rm -rf /VK-GL-CTS

mv /deqp-* $ROOTFS/.


############### Build SKQP
if [[ "$DEBIAN_ARCH" = "arm64" ]] \
  || [[ "$DEBIAN_ARCH" = "amd64" ]]; then
    . .gitlab-ci/container/build-skqp.sh
    mv /skqp $ROOTFS/.
fi

############### Build piglit
PIGLIT_OPTS="-DPIGLIT_USE_WAFFLE=ON
	     -DPIGLIT_USE_GBM=ON
	     -DPIGLIT_USE_WAYLAND=ON
	     -DPIGLIT_USE_X11=ON
	     -DPIGLIT_BUILD_GLX_TESTS=ON
	     -DPIGLIT_BUILD_EGL_TESTS=ON
	     -DPIGLIT_BUILD_WGL_TESTS=OFF
	     -DPIGLIT_BUILD_GL_TESTS=ON
	     -DPIGLIT_BUILD_GLES1_TESTS=ON
	     -DPIGLIT_BUILD_GLES2_TESTS=ON
	     -DPIGLIT_BUILD_GLES3_TESTS=ON
	     -DPIGLIT_BUILD_CL_TESTS=$BUILD_CL
	     -DPIGLIT_BUILD_VK_TESTS=$BUILD_VK
	     -DPIGLIT_BUILD_DMA_BUF_TESTS=ON" \
  . .gitlab-ci/container/build-piglit.sh
mv /piglit $ROOTFS/.

############### Build libva tests
if [[ "$DEBIAN_ARCH" = "amd64" ]]; then
    . .gitlab-ci/container/build-va-tools.sh
    mv /va/bin/* $ROOTFS/usr/bin/
fi

############### Build Crosvm
if [[ ${DEBIAN_ARCH} = "amd64" ]]; then
    . .gitlab-ci/container/build-crosvm.sh
    mv /usr/local/bin/crosvm $ROOTFS/usr/bin/
    mv /usr/local/lib/libvirglrenderer.* $ROOTFS/usr/lib/$GCC_ARCH/
    mkdir -p $ROOTFS/usr/local/libexec/
    mv /usr/local/libexec/virgl* $ROOTFS/usr/local/libexec/
fi

############### Build ci-kdl
. .gitlab-ci/container/build-kdl.sh
mv /ci-kdl $ROOTFS/

############### Install fluster
if [[ ${DEBIAN_ARCH} = "amd64" ]]; then
    section_start fluster "Install fluster"
    . .gitlab-ci/container/build-fluster.sh
    section_end fluster
fi

############### Build local stuff for use by igt and kernel testing, which
############### will reuse most of our container build process from a specific
############### hash of the Mesa tree.
if [[ -e ".gitlab-ci/local/build-rootfs.sh" ]]; then
    . .gitlab-ci/local/build-rootfs.sh
fi


############### Download prebuilt kernel
. .gitlab-ci/container/download-prebuilt-kernel.sh

############### Delete rust, since the tests won't be compiling anything.
rm -rf /root/.cargo
rm -rf /root/.rustup

############### Delete firmware files we don't need
if [ "$DEBIAN_ARCH" = "amd64" ]; then
   dpkg -L firmware-misc-nonfree | grep -v "i915" | xargs rm || true
fi

############### Fill rootfs
cp .gitlab-ci/setup-test-env.sh $ROOTFS/.
cp .gitlab-ci/container/setup-rootfs.sh $ROOTFS/.
cp .gitlab-ci/container/strip-rootfs.sh $ROOTFS/.
cp .gitlab-ci/container/debian/llvm-snapshot.gpg.key $ROOTFS/.
cp .gitlab-ci/container/debian/winehq.gpg.key $ROOTFS/.
chroot $ROOTFS bash /setup-rootfs.sh
rm $ROOTFS/{llvm-snapshot,winehq}.gpg.key
rm "$ROOTFS/setup-test-env.sh"
rm "$ROOTFS/setup-rootfs.sh"
rm "$ROOTFS/strip-rootfs.sh"
cp /etc/wgetrc $ROOTFS/etc/.

if [ "${DEBIAN_ARCH}" = "arm64" ]; then
    mkdir -p /lava-files/rootfs-arm64/lib/firmware/qcom/sm8350/  # for firmware imported later
    # Make a gzipped copy of the Image for db410c.
    gzip -k /lava-files/Image
    KERNEL_IMAGE_NAME+=" Image.gz"
fi

ROOTFSTAR="lava-rootfs.tar.zst"
du -ah "$ROOTFS" | sort -h | tail -100
pushd $ROOTFS
  tar --zstd -cf /lava-files/${ROOTFSTAR} .
popd

. .gitlab-ci/container/container_post_build.sh

ci-fairy s3cp --token-file "${S3_JWT_FILE}" /lava-files/"${ROOTFSTAR}" \
      https://${S3_PATH}/"${ROOTFSTAR}"

touch /lava-files/done
ci-fairy s3cp --token-file "${S3_JWT_FILE}" /lava-files/done https://${S3_PATH}/done
