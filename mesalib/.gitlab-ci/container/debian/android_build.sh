#!/bin/bash

set -ex

EPHEMERAL="\
         rdfind \
         unzip \
         "

apt-get install -y --no-remove $EPHEMERAL

# Fetch the NDK and extract just the toolchain we want.
ndk=android-ndk-r21d
wget -O $ndk.zip https://dl.google.com/android/repository/$ndk-linux-x86_64.zip
unzip -d / $ndk.zip "$ndk/toolchains/llvm/*"
rm $ndk.zip
# Since it was packed as a zip file, symlinks/hardlinks got turned into
# duplicate files.  Turn them into hardlinks to save on container space.
rdfind -makehardlinks true -makeresultsfile false /android-ndk-r21d/
# Drop some large tools we won't use in this build.
find /android-ndk-r21d/ -type f | egrep -i "clang-check|clang-tidy|lldb" | xargs rm -f

sh .gitlab-ci/container/create-android-ndk-pc.sh /$ndk zlib.pc "" "-lz" "1.2.3"

sh .gitlab-ci/container/create-android-cross-file.sh /$ndk x86_64-linux-android x86_64 x86_64
sh .gitlab-ci/container/create-android-cross-file.sh /$ndk i686-linux-android x86 x86
sh .gitlab-ci/container/create-android-cross-file.sh /$ndk aarch64-linux-android arm armv8
sh .gitlab-ci/container/create-android-cross-file.sh /$ndk arm-linux-androideabi arm armv7hl armv7a-linux-androideabi

# Not using build-libdrm.sh because we don't want its cleanup after building
# each arch.  Fetch and extract now.
export LIBDRM_VERSION=libdrm-2.4.102
wget https://dri.freedesktop.org/libdrm/$LIBDRM_VERSION.tar.xz
tar -xf $LIBDRM_VERSION.tar.xz && rm $LIBDRM_VERSION.tar.xz

for arch in \
        x86_64-linux-android \
        i686-linux-android \
        aarch64-linux-android \
        arm-linux-androideabi ; do

    cd $LIBDRM_VERSION
    rm -rf build-$arch
    meson build-$arch \
          --cross-file=/cross_file-$arch.txt \
          --libdir=lib/$arch \
          -Dlibkms=false \
          -Dnouveau=false \
          -Dvc4=false \
          -Detnaviv=false \
          -Dfreedreno=false \
          -Dintel=false \
          -Dcairo-tests=false
    ninja -C build-$arch install
    cd ..
done

rm -rf $LIBDRM_VERSION

apt-get purge -y $EPHEMERAL
