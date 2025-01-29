#!/usr/bin/env bash
# The relative paths in this file only become valid at runtime.
# shellcheck disable=SC1091
#
# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_ANDROID_TAG

set -e

. .gitlab-ci/setup-test-env.sh

set -o xtrace

export DEBIAN_FRONTEND=noninteractive

# Ephemeral packages (installed for this script and removed again at the end)
EPHEMERAL=(
   build-essential:native
   ccache
   cmake
   config-package-dev
   debhelper-compat
   dpkg-dev
   ninja-build
   sudo
   unzip
)

DEPS=(
    iproute2
)
apt-get install -y --no-remove --no-install-recommends \
      "${DEPS[@]}" "${EPHEMERAL[@]}"

############### Building ...

. .gitlab-ci/container/container_pre_build.sh

############### Downloading NDK for native builds for the guest ...

# Fetch the NDK and extract just the toolchain we want.
ndk="android-ndk-${ANDROID_NDK_VERSION}"
curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
  -o "$ndk.zip" "https://dl.google.com/android/repository/$ndk-linux.zip"
unzip -d / "$ndk.zip"
rm "$ndk.zip"

############### Build dEQP runner

export ANDROID_NDK_HOME=/$ndk
export RUST_TARGET=x86_64-linux-android
. .gitlab-ci/container/build-rust.sh
. .gitlab-ci/container/build-deqp-runner.sh

rm -rf /root/.cargo
rm -rf /root/.rustup

############### Build dEQP GL

DEQP_API=tools \
DEQP_TARGET="android" \
EXTRA_CMAKE_ARGS="-DDEQP_ANDROID_EXE=ON -DDEQP_TARGET_TOOLCHAIN=ndk-modern -DANDROID_NDK_PATH=/$ndk -DANDROID_ABI=x86_64 -DDE_ANDROID_API=$ANDROID_SDK_VERSION" \
. .gitlab-ci/container/build-deqp.sh

DEQP_API=GL \
DEQP_TARGET="android" \
EXTRA_CMAKE_ARGS="-DDEQP_TARGET_TOOLCHAIN=ndk-modern -DANDROID_NDK_PATH=/$ndk -DANDROID_ABI=x86_64 -DDE_ANDROID_API=$ANDROID_SDK_VERSION" \
. .gitlab-ci/container/build-deqp.sh

DEQP_API=GLES \
DEQP_TARGET="android" \
EXTRA_CMAKE_ARGS="-DDEQP_TARGET_TOOLCHAIN=ndk-modern -DANDROID_NDK_PATH=/$ndk -DANDROID_ABI=x86_64 -DDE_ANDROID_API=$ANDROID_SDK_VERSION" \
. .gitlab-ci/container/build-deqp.sh

DEQP_API=VK \
DEQP_TARGET="android" \
EXTRA_CMAKE_ARGS="-DDEQP_ANDROID_EXE=ON -DDEQP_TARGET_TOOLCHAIN=ndk-modern -DANDROID_NDK_PATH=/$ndk -DANDROID_ABI=x86_64 -DDE_ANDROID_API=$ANDROID_SDK_VERSION" \
. .gitlab-ci/container/build-deqp.sh

rm -rf /VK-GL-CTS

############### Downloading Cuttlefish resources ...

CUTTLEFISH_PROJECT_PATH=ao2/aosp-manifest
CUTTLEFISH_BUILD_VERSION_TAGS=mesa-venus
CUTTLEFISH_BUILD_NUMBER=20250115.001

mkdir /cuttlefish
pushd /cuttlefish

curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
  -o aosp_cf_x86_64_phone-img-$CUTTLEFISH_BUILD_NUMBER.zip "https://${S3_HOST}/${S3_ANDROID_BUCKET}/${CUTTLEFISH_PROJECT_PATH}/aosp-${CUTTLEFISH_BUILD_VERSION_TAGS}.${CUTTLEFISH_BUILD_NUMBER}/aosp_cf_x86_64_phone-img-$CUTTLEFISH_BUILD_NUMBER.zip"

unzip aosp_cf_x86_64_phone-img-$CUTTLEFISH_BUILD_NUMBER.zip
rm aosp_cf_x86_64_phone-img-$CUTTLEFISH_BUILD_NUMBER.zip
ls -lhS ./*

curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
  -o cvd-host_package.tar.gz "https://${S3_HOST}/${S3_ANDROID_BUCKET}/${CUTTLEFISH_PROJECT_PATH}/aosp-${CUTTLEFISH_BUILD_VERSION_TAGS}.${CUTTLEFISH_BUILD_NUMBER}/cvd-host_package.tar.gz"
tar -xzvf cvd-host_package.tar.gz
rm cvd-host_package.tar.gz

AOSP_KERNEL_PROJECT_PATH=ao2/aosp-kernel-manifest
AOSP_KERNEL_BUILD_VERSION_TAGS=common-android14-6.1-venus
AOSP_KERNEL_BUILD_NUMBER=20241107.001

curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
  -o bzImage "https://${S3_HOST}/${S3_ANDROID_BUCKET}/${AOSP_KERNEL_PROJECT_PATH}/aosp-kernel-common-${AOSP_KERNEL_BUILD_VERSION_TAGS}.${AOSP_KERNEL_BUILD_NUMBER}/bzImage"
curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
  -o initramfs.img "https://${S3_HOST}/${S3_ANDROID_BUCKET}/${AOSP_KERNEL_PROJECT_PATH}/aosp-kernel-common-${AOSP_KERNEL_BUILD_VERSION_TAGS}.${AOSP_KERNEL_BUILD_NUMBER}/initramfs.img"

popd

############### Building and installing Debian package ...

ANDROID_CUTTLEFISH_VERSION=v1.0.1

mkdir android-cuttlefish
pushd android-cuttlefish
git init
git remote add origin https://github.com/google/android-cuttlefish.git
git fetch --depth 1 origin "$ANDROID_CUTTLEFISH_VERSION"
git checkout FETCH_HEAD

./tools/buildutils/build_packages.sh

apt-get install -y --allow-downgrades ./cuttlefish-base_*.deb ./cuttlefish-user_*.deb

popd
rm -rf android-cuttlefish

addgroup --system kvm
usermod -a -G kvm,cvdnetwork root

############### Uninstall the build software

rm -rf "/${ndk:?}"

export SUDO_FORCE_REMOVE=yes
apt-get purge -y "${EPHEMERAL[@]}"

. .gitlab-ci/container/container_post_build.sh

. .gitlab-ci/container/strip-rootfs.sh
