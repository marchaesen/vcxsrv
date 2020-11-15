#!/bin/sh

set -eu

if [ ! -e .git ]; then
    echo must run from top-level directory;
    exit 1
fi

if [ ! -d platform-hardware-libhardware ]; then
    git clone --depth 1 https://android.googlesource.com/platform/hardware/libhardware platform-hardware-libhardware
    git clone --depth 1 https://android.googlesource.com/platform/system/core platform-system-core
    git clone --depth 1 https://android.googlesource.com/platform/frameworks/native platform-frameworks-native
fi

dest=include/android_stub

rm -rf ${dest}
mkdir ${dest}


# These directories contains mostly only the files we need, so copy wholesale

cp -av platform-frameworks-native/libs/nativewindow/include/vndk        \
    platform-system-core/libsync/include/sync                           \
    platform-system-core/libsync/include/ndk                            \
    platform-system-core/libbacktrace/include/backtrace                 \
    platform-system-core/libsystem/include/system                       \
    platform-system-core/liblog/include/log                             \
    platform-frameworks-native/libs/nativewindow/include/apex           \
    platform-frameworks-native/libs/nativewindow/include/system         \
    platform-frameworks-native/libs/nativebase/include/nativebase       \
    ${dest}


# We only need a few files from these big directories so just copy those

mkdir ${dest}/hardware
cp -av platform-hardware-libhardware/include/hardware/{hardware,gralloc,gralloc1,fb}.h ${dest}/hardware
cp -av platform-frameworks-native/vulkan/include/hardware/hwvulkan.h ${dest}/hardware

mkdir ${dest}/cutils
cp -av platform-system-core/libcutils/include/cutils/{log,native_handle,properties}.h ${dest}/cutils


# include/android has files from a few different projects

mkdir ${dest}/android
cp -av platform-frameworks-native/libs/nativewindow/include/android/*   \
    platform-frameworks-native/libs/arect/include/android/*             \
    platform-system-core/liblog/include/android/*                       \
    platform-system-core/libsync/include/android/*                      \
    ${dest}/android

