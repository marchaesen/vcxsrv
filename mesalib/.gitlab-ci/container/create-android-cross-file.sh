#!/bin/bash

ndk=$1
arch=$2
cpu_family=$3
cpu=$4
cross_file="/cross_file-$arch.txt"

# armv7 has the toolchain split between two names.
arch2=${5:-$2}

# Note that we disable C++ exceptions, because Mesa doesn't use exceptions,
# and allowing it in code generation means we get unwind symbols that break
# the libEGL and driver symbol tests.

cat >$cross_file <<EOF
[binaries]
ar = '$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/$arch-ar'
c = ['ccache', '$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/${arch2}29-clang', '-fno-exceptions', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables']
cpp = ['ccache', '$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/${arch2}29-clang++', '-fno-exceptions', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-static-libstdc++']
c_ld = 'lld'
cpp_ld = 'lld'
strip = '$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/$arch-strip'
pkgconfig = ['/usr/bin/pkg-config']

[host_machine]
system = 'linux'
cpu_family = '$cpu_family'
cpu = '$cpu'
endian = 'little'

[properties]
needs_exe_wrapper = true

EOF
