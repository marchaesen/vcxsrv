#!/bin/bash

set -ex

git config --global user.email "mesa@example.com"
git config --global user.name "Mesa CI"
git clone \
    https://github.com/KhronosGroup/VK-GL-CTS.git \
    -b vulkan-cts-1.2.7.0 \
    --depth 1 \
    /VK-GL-CTS
pushd /VK-GL-CTS

# --insecure is due to SSL cert failures hitting sourceforge for zlib and
# libpng (sigh).  The archives get their checksums checked anyway, and git
# always goes through ssh or https.
python3 external/fetch_sources.py --insecure

mkdir -p /deqp

# Save the testlog stylesheets:
cp doc/testlog-stylesheet/testlog.{css,xsl} /deqp
popd

pushd /deqp
# When including EGL/X11 testing, do that build first and save off its
# deqp-egl binary.
cmake -S /VK-GL-CTS -B . -G Ninja \
      -DDEQP_TARGET=x11_egl_glx \
      -DCMAKE_BUILD_TYPE=Release \
      $EXTRA_CMAKE_ARGS
ninja modules/egl/deqp-egl
cp /deqp/modules/egl/deqp-egl /deqp/modules/egl/deqp-egl-x11


cmake -S /VK-GL-CTS -B . -G Ninja \
      -DDEQP_TARGET=${DEQP_TARGET:-x11_glx} \
      -DCMAKE_BUILD_TYPE=Release \
      $EXTRA_CMAKE_ARGS
ninja

mv /deqp/modules/egl/deqp-egl-x11 /deqp/modules/egl/deqp-egl

# Copy out the mustpass lists we want.
mkdir /deqp/mustpass
for mustpass in $(< /VK-GL-CTS/external/vulkancts/mustpass/master/vk-default.txt) ; do
    cat /VK-GL-CTS/external/vulkancts/mustpass/master/$mustpass \
        >> /deqp/mustpass/vk-master.txt
done

cp \
    /deqp/external/openglcts/modules/gl_cts/data/mustpass/gles/aosp_mustpass/3.2.6.x/*.txt \
    /deqp/mustpass/.
cp \
    /deqp/external/openglcts/modules/gl_cts/data/mustpass/egl/aosp_mustpass/3.2.6.x/egl-master.txt \
    /deqp/mustpass/.
cp \
    /deqp/external/openglcts/modules/gl_cts/data/mustpass/gles/khronos_mustpass/3.2.6.x/*-master.txt \
    /deqp/mustpass/.
cp \
    /deqp/external/openglcts/modules/gl_cts/data/mustpass/gl/khronos_mustpass/4.6.1.x/*-master.txt \
    /deqp/mustpass/.

# Save *some* executor utils, but otherwise strip things down
# to reduct deqp build size:
mkdir /deqp/executor.save
cp /deqp/executor/testlog-to-* /deqp/executor.save
rm -rf /deqp/executor
mv /deqp/executor.save /deqp/executor

rm -rf /deqp/external/openglcts/modules/gl_cts/data/mustpass
rm -rf /deqp/external/openglcts/modules/cts-runner
rm -rf /deqp/modules/internal
rm -rf /deqp/execserver
rm -rf /deqp/framework
find -iname '*cmake*' -o -name '*ninja*' -o -name '*.o' -o -name '*.a' | xargs rm -rf
${STRIP_CMD:-strip} external/vulkancts/modules/vulkan/deqp-vk
${STRIP_CMD:-strip} external/openglcts/modules/glcts
${STRIP_CMD:-strip} modules/*/deqp-*
du -sh *
rm -rf /VK-GL-CTS
popd
