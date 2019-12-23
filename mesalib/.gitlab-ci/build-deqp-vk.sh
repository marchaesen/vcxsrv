git clone --depth 1 \
    https://github.com/KhronosGroup/VK-GL-CTS.git \
    -b vulkan-cts-1.1.6.0 \
    /VK-GL-CTS
cd /VK-GL-CTS

# --insecure is due to SSL cert failures hitting sourceforge for zlib and
# libpng (sigh).  The archives get their checksums checked anyway, and git
# always goes through ssh or https.
python3 external/fetch_sources.py --insecure

mkdir -p /deqp
cd /deqp
cmake -G Ninja \
      -DDEQP_TARGET=x11_glx \
      -DCMAKE_BUILD_TYPE=Release \
      /VK-GL-CTS
ninja -j4

# Copy out the mustpass list we want.
mkdir /deqp/mustpass
cp /VK-GL-CTS/external/vulkancts/mustpass/master/vk-default.txt \
   /deqp/mustpass/vk-master.txt

rm -rf /deqp/modules/internal
rm -rf /deqp/executor
rm -rf /deqp/execserver
rm -rf /deqp/modules/egl
rm -rf /deqp/framework
find -iname '*cmake*' -o -name '*ninja*' -o -name '*.o' -o -name '*.a' | xargs rm -rf
strip external/vulkancts/modules/vulkan/deqp-vk
du -sh *
rm -rf /VK-GL-CTS
