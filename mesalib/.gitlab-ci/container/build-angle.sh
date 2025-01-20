#!/usr/bin/env bash

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# KERNEL_ROOTFS_TAG

set -uex

uncollapsed_section_start angle "Building angle"

ANGLE_REV="76025caa1a059f464a2b0e8f879dbd4746f092b9"
SCRIPTS_DIR="$(pwd)/.gitlab-ci"
ANGLE_PATCH_DIR="${SCRIPTS_DIR}/container/patches"

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

angle_patch_files=(
  build-angle_deps_Make-more-sources-conditional.patch
)
for patch in "${angle_patch_files[@]}"; do
  echo "Apply patch to ANGLE from ${patch}"
  GIT_COMMITTER_DATE="$(LC_TIME=C date -d@0)" git am < "${ANGLE_PATCH_DIR}/${patch}"
done

{
  echo "ANGLE base version $ANGLE_REV"
  echo "The following local patches are applied on top:"
  git log --reverse --oneline $ANGLE_REV.. --format='- %s'
} > /angle/version

# source preparation
gclient config --name REPLACE-WITH-A-DOT --unmanaged \
  --custom-var='angle_enable_cl=False' \
  --custom-var='angle_enable_cl_testing=False' \
  --custom-var='angle_enable_vulkan_validation_layers=False' \
  --custom-var='angle_enable_wgpu=False' \
  --custom-var='build_allow_regenerate=False' \
  --custom-var='build_angle_deqp_tests=False' \
  --custom-var='build_angle_perftests=False' \
  --custom-var='build_with_catapult=False' \
  --custom-var='build_with_swiftshader=False' \
  https://chromium.googlesource.com/angle/angle.git
sed -e 's/REPLACE-WITH-A-DOT/./;' -i .gclient
gclient sync -j"${FDO_CI_CONCURRENT:-4}"

mkdir -p out/Release
echo '
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
angle_use_custom_libvulkan=false
angle_egl_extension="so.1"
angle_glesv2_extension="so.2"
build_angle_deqp_tests=false
dcheck_always_on=true
enable_expensive_dchecks=false
is_debug=false
' > out/Release/args.gn

if [[ "$DEBIAN_ARCH" = "arm64" ]]; then
  build/linux/sysroot_scripts/install-sysroot.py --arch=arm64
fi

gn gen out/Release
# depot_tools overrides ninja with a version that doesn't work.  We want
# ninja with FDO_CI_CONCURRENT anyway.
/usr/local/bin/ninja -C out/Release/ libEGL libGLESv2

rm -f out/Release/libvulkan.so* out/Release/*.so.TOC
cp out/Release/lib*.so* /angle/
ln -s libEGL.so.1 /angle/libEGL.so
ln -s libGLESv2.so.2 /angle/libGLESv2.so

rm -rf out

popd
rm -rf /depot-tools
rm -rf /angle-build

section_end angle
