#!/usr/bin/env bash

set -exu

# If CI vars are not set, assign an empty value, this prevents -u to fail
: "${CI:=}"
: "${CI_PROJECT_PATH:=}"

# Early check for required env variables, relies on `set -u`
: "$ANDROID_NDK_VERSION"
: "$ANDROID_SDK_VERSION"
: "$ANDROID_LLVM_VERSION"
: "$ANDROID_LLVM_ARTIFACT_NAME"
: "$S3_JWT_FILE"
: "$S3_HOST"
: "$S3_ANDROID_BUCKET"

# Check for CI if the auth file used later on is non-empty
if [ -n "$CI" ] && [ ! -s "${S3_JWT_FILE}" ]; then
  echo "Error: ${S3_JWT_FILE} is empty." 1>&2
  exit 1
fi

if curl -s -o /dev/null -I -L -f --retry 4 --retry-delay 15 "https://${S3_HOST}/${S3_ANDROID_BUCKET}/${CI_PROJECT_PATH}/${ANDROID_LLVM_ARTIFACT_NAME}.tar.zst"; then
  echo "Artifact ${ANDROID_LLVM_ARTIFACT_NAME}.tar.zst already exists, skip re-building."

  # Download prebuilt LLVM libraries for Android when they have not changed,
  # to save some time
  curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
    -o "/${ANDROID_LLVM_ARTIFACT_NAME}.tar.zst" "https://${S3_HOST}/${S3_ANDROID_BUCKET}/${CI_PROJECT_PATH}/${ANDROID_LLVM_ARTIFACT_NAME}.tar.zst"
  tar -C / --zstd -xf "/${ANDROID_LLVM_ARTIFACT_NAME}.tar.zst"
  rm "/${ANDROID_LLVM_ARTIFACT_NAME}.tar.zst"

  exit
fi

# Install some dependencies needed to build LLVM
EPHEMERAL=(
  ninja-build
  unzip
)

apt-get update
apt-get install -y --no-install-recommends --no-remove "${EPHEMERAL[@]}"

ANDROID_NDK="android-ndk-${ANDROID_NDK_VERSION}"
ANDROID_NDK_ROOT="/${ANDROID_NDK}"
if [ ! -d "$ANDROID_NDK_ROOT" ];
then
  curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
    -o "${ANDROID_NDK}.zip" \
    "https://dl.google.com/android/repository/${ANDROID_NDK}-linux.zip"
  unzip -d / "${ANDROID_NDK}.zip" "$ANDROID_NDK/source.properties" "$ANDROID_NDK/build/cmake/*" "$ANDROID_NDK/toolchains/llvm/*"
  rm "${ANDROID_NDK}.zip"
fi

if [ ! -d "/llvm-project" ];
then
  mkdir "/llvm-project"
  pushd "/llvm-project"
  git init
  git remote add origin https://github.com/llvm/llvm-project.git
  git fetch --depth 1 origin "$ANDROID_LLVM_VERSION"
  git checkout FETCH_HEAD
  popd
fi

pushd "/llvm-project"

# Checkout again the intended version, just in case of a pre-existing full clone
git checkout "$ANDROID_LLVM_VERSION" || true

LLVM_INSTALL_PREFIX="/${ANDROID_LLVM_ARTIFACT_NAME}"

rm -rf build/
cmake -GNinja -S llvm -B build/ \
    -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=x86_64 \
    -DANDROID_PLATFORM="android-${ANDROID_SDK_VERSION}" \
    -DANDROID_NDK="${ANDROID_NDK_ROOT}" \
    -DCMAKE_ANDROID_ARCH_ABI=x86_64 \
    -DCMAKE_ANDROID_NDK="${ANDROID_NDK_ROOT}" \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DCMAKE_SYSTEM_NAME=Android \
    -DCMAKE_SYSTEM_VERSION="${ANDROID_SDK_VERSION}" \
    -DCMAKE_INSTALL_PREFIX="${LLVM_INSTALL_PREFIX}" \
    -DCMAKE_CXX_FLAGS="-march=x86-64 --target=x86_64-linux-android${ANDROID_SDK_VERSION} -fno-rtti" \
    -DLLVM_HOST_TRIPLE="x86_64-linux-android${ANDROID_SDK_VERSION}" \
    -DLLVM_TARGETS_TO_BUILD=X86 \
    -DLLVM_BUILD_LLVM_DYLIB=OFF \
    -DLLVM_BUILD_TESTS=OFF \
    -DLLVM_BUILD_EXAMPLES=OFF \
    -DLLVM_BUILD_DOCS=OFF \
    -DLLVM_BUILD_TOOLS=OFF \
    -DLLVM_ENABLE_RTTI=OFF \
    -DLLVM_BUILD_INSTRUMENTED_COVERAGE=OFF \
    -DLLVM_NATIVE_TOOL_DIR="${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin" \
    -DLLVM_ENABLE_PIC=False \
    -DLLVM_OPTIMIZED_TABLEGEN=ON

ninja "-j${FDO_CI_CONCURRENT:-4}" -C build/ install

popd

rm -rf /llvm-project

tar --zstd -cf "${ANDROID_LLVM_ARTIFACT_NAME}.tar.zst" "$LLVM_INSTALL_PREFIX"

# If run in CI upload the tar.zst archive to S3 to avoid rebuilding it if the
# version does not change, and delete it.
# The file is not deleted for non-CI because it can be useful in local runs.
if [ -n "$CI" ]; then
  ci-fairy s3cp --token-file "${S3_JWT_FILE}" "${ANDROID_LLVM_ARTIFACT_NAME}.tar.zst" "https://${S3_HOST}/${S3_ANDROID_BUCKET}/${CI_PROJECT_PATH}/${ANDROID_LLVM_ARTIFACT_NAME}.tar.zst"
  rm "${ANDROID_LLVM_ARTIFACT_NAME}.tar.zst"
fi

rm -rf "$LLVM_INSTALL_PREFIX"

apt-get purge -y "${EPHEMERAL[@]}"
