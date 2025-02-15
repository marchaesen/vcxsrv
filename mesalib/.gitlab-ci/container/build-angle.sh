#!/usr/bin/env bash

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_ANDROID_TAG
# KERNEL_ROOTFS_TAG

set -uex

uncollapsed_section_start angle "Building ANGLE"

ANGLE_REV="6abdc11741c4932e8e64ec055e67f556a7cbb5b1"

# Set ANGLE_ARCH based on DEBIAN_ARCH if it hasn't been explicitly defined
if [[ -z "${ANGLE_ARCH:-}" ]]; then
  case "$DEBIAN_ARCH" in
    amd64) ANGLE_ARCH=x64;;
    arm64) ANGLE_ARCH=arm64;;
  esac
fi

# DEPOT tools
git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git /depot-tools
export PATH=/depot-tools:$PATH
export DEPOT_TOOLS_UPDATE=0

mkdir /angle-build
mkdir /angle
pushd /angle-build
git init
git remote add origin https://chromium.googlesource.com/angle/angle.git
git fetch --depth 1 origin "$ANGLE_REV"
git checkout FETCH_HEAD

echo "$ANGLE_REV" > /angle/version

GCLIENT_CUSTOM_VARS=()
GCLIENT_CUSTOM_VARS+=('--custom-var=angle_enable_cl=False')
GCLIENT_CUSTOM_VARS+=('--custom-var=angle_enable_cl_testing=False')
GCLIENT_CUSTOM_VARS+=('--custom-var=angle_enable_vulkan_validation_layers=False')
GCLIENT_CUSTOM_VARS+=('--custom-var=angle_enable_wgpu=False')
GCLIENT_CUSTOM_VARS+=('--custom-var=build_angle_deqp_tests=False')
GCLIENT_CUSTOM_VARS+=('--custom-var=build_angle_perftests=False')
if [[ "$ANGLE_TARGET" == "android" ]]; then
  GCLIENT_CUSTOM_VARS+=('--custom-var=checkout_android=True')
fi

# source preparation
gclient config --name REPLACE-WITH-A-DOT --unmanaged \
  "${GCLIENT_CUSTOM_VARS[@]}" \
  https://chromium.googlesource.com/angle/angle.git
sed -e 's/REPLACE-WITH-A-DOT/./;' -i .gclient
sed -e 's|"custom_deps" : {|"custom_deps" : {\
      "third_party/clspv/src": None,\
      "third_party/dawn": None,\
      "third_party/glmark2/src": None,\
      "third_party/libjpeg_turbo": None,\
      "third_party/llvm/src": None,\
      "third_party/OpenCL-CTS/src": None,\
      "third_party/SwiftShader": None,\
      "third_party/VK-GL-CTS/src": None,\
      "third_party/vulkan-validation-layers/src": None,|' -i .gclient
gclient sync --no-history -j"${FDO_CI_CONCURRENT:-4}"

mkdir -p out/Release
cat > out/Release/args.gn <<EOF
angle_assert_always_on=false
angle_build_all=false
angle_build_tests=false
angle_enable_cl=false
angle_enable_cl_testing=false
angle_enable_gl=false
angle_enable_gl_desktop_backend=false
angle_enable_null=false
angle_enable_swiftshader=false
angle_enable_trace=false
angle_enable_wgpu=false
angle_enable_vulkan=true
angle_enable_vulkan_api_dump_layer=false
angle_enable_vulkan_validation_layers=false
angle_has_frame_capture=false
angle_has_histograms=false
angle_has_rapidjson=false
angle_use_custom_libvulkan=false
build_angle_deqp_tests=false
dcheck_always_on=true
enable_expensive_dchecks=false
is_component_build=false
is_debug=false
target_cpu="${ANGLE_ARCH}"
target_os="${ANGLE_TARGET}"
EOF

case "$ANGLE_TARGET" in
  linux) cat >> out/Release/args.gn <<EOF
angle_egl_extension="so.1"
angle_glesv2_extension="so.2"
use_custom_libcxx=false
custom_toolchain="//build/toolchain/linux/unbundle:default"
host_toolchain="//build/toolchain/linux/unbundle:default"
EOF
    ;;
  android) cat >> out/Release/args.gn <<EOF
android_ndk_version="${ANDROID_NDK_VERSION}"
android64_ndk_api_level=${ANDROID_SDK_VERSION}
android32_ndk_api_level=${ANDROID_SDK_VERSION}
use_custom_libcxx=true
EOF
    ;;
    *) echo "Unexpected ANGLE_TARGET value: $ANGLE_TARGET"; exit 1;;
esac

if [[ "$DEBIAN_ARCH" = "arm64" ]]; then
  # We need to get an AArch64 sysroot - because ANGLE isn't great friends with
  # system dependencies - but use the default system toolchain, because the
  # 'arm64' toolchain you get from Google infrastructure is a cross-compiler
  # from x86-64
  build/linux/sysroot_scripts/install-sysroot.py --arch=arm64
fi

(
  # The 'unbundled' toolchain configuration requires clang, and it also needs to
  # be configured via environment variables.
  export CC="clang-${LLVM_VERSION}"
  export HOST_CC="$CC"
  export CFLAGS="-Wno-unknown-warning-option"
  export HOST_CFLAGS="$CFLAGS"
  export CXX="clang++-${LLVM_VERSION}"
  export HOST_CXX="$CXX"
  export CXXFLAGS="-Wno-unknown-warning-option"
  export HOST_CXXFLAGS="$CXXFLAGS"
  export AR="ar"
  export HOST_AR="$AR"
  export NM="nm"
  export HOST_NM="$NM"
  export LDFLAGS="-fuse-ld=lld-${LLVM_VERSION} -lpthread -ldl"
  export HOST_LDFLAGS="$LDFLAGS"

  gn gen out/Release
  # depot_tools overrides ninja with a version that doesn't work.  We want
  # ninja with FDO_CI_CONCURRENT anyway.
  /usr/local/bin/ninja -C out/Release/ libEGL libGLESv1_CM libGLESv2
)

rm -f out/Release/libvulkan.so* out/Release/*.so*.TOC
cp out/Release/lib*.so* /angle/

if [[ "$ANGLE_TARGET" == "linux" ]]; then
  ln -s libEGL.so.1 /angle/libEGL.so
  ln -s libGLESv2.so.2 /angle/libGLESv2.so
fi

rm -rf out

popd
rm -rf /depot-tools
rm -rf /angle-build

section_end angle
