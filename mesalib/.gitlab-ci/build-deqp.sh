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

# Save the testlog stylesheets:
cp doc/testlog-stylesheet/testlog.{css,xsl} /deqp

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

# Save *some* executor utils, but otherwise strip things down
# to reduct deqp build size:
mkdir /deqp/executor.save
cp /deqp/executor/testlog-to-* /deqp/executor.save
rm -rf /deqp/executor
mv /deqp/executor.save /deqp/executor

rm -rf /deqp/external
rm -rf /deqp/modules/internal
rm -rf /deqp/execserver
rm -rf /deqp/modules/egl
rm -rf /deqp/framework
find -iname '*cmake*' -o -name '*ninja*' -o -name '*.o' -o -name '*.a' | xargs rm -rf
strip modules/*/deqp-*
du -sh *
rm -rf /VK-GL-CTS
