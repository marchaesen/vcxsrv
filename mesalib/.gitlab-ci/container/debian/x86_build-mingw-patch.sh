#!/bin/bash
# shellcheck disable=SC2086 # we want word splitting

set -e

# Pull packages from msys2 repository that can be directly used.
# We can use https://packages.msys2.org/ to retrieve the newest package
mkdir ~/tmp
pushd ~/tmp
MINGW_PACKET_LIST="
mingw-w64-x86_64-headers-git-10.0.0.r14.ga08c638f8-1-any.pkg.tar.zst
mingw-w64-x86_64-vulkan-loader-1.3.211-1-any.pkg.tar.zst
mingw-w64-x86_64-libelf-0.8.13-6-any.pkg.tar.zst
mingw-w64-x86_64-zlib-1.2.12-1-any.pkg.tar.zst
mingw-w64-x86_64-zstd-1.5.2-2-any.pkg.tar.zst
"

for i in $MINGW_PACKET_LIST
do
  wget -q https://mirror.msys2.org/mingw/mingw64/$i
  tar xf $i --strip-components=1 -C /usr/x86_64-w64-mingw32/
done
popd
rm -rf ~/tmp

mkdir -p /usr/x86_64-w64-mingw32/bin

# The output of `wine64 llvm-config --system-libs --cxxflags mcdisassembler`
# containes absolute path like '-IZ:'
# The sed is used to replace `-IZ:/usr/x86_64-w64-mingw32/include`
# to `-I/usr/x86_64-w64-mingw32/include`

# Debian's pkg-config wrapers for mingw are broken, and there's no sign that
# they're going to be fixed, so we'll just have to fix it ourselves
# https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=930492
cat >/usr/x86_64-w64-mingw32/bin/pkg-config <<EOF
#!/bin/sh

PKG_CONFIG_LIBDIR=/usr/x86_64-w64-mingw32/lib/pkgconfig:/usr/x86_64-w64-mingw32/share/pkgconfig pkg-config \$@
EOF
chmod +x /usr/x86_64-w64-mingw32/bin/pkg-config

cat >/usr/x86_64-w64-mingw32/bin/llvm-config <<EOF
#!/bin/sh
wine64 llvm-config \$@ | sed -e "s,Z:/,/,gi"
EOF
chmod +x /usr/x86_64-w64-mingw32/bin/llvm-config

cat >/usr/x86_64-w64-mingw32/bin/clang <<EOF
#!/bin/sh
wine64 clang \$@
EOF
chmod +x /usr/x86_64-w64-mingw32/bin/clang

cat >/usr/x86_64-w64-mingw32/bin/llvm-as <<EOF
#!/bin/sh
wine64 llvm-as \$@
EOF
chmod +x /usr/x86_64-w64-mingw32/bin/llvm-as

cat >/usr/x86_64-w64-mingw32/bin/llvm-link <<EOF
#!/bin/sh
wine64 llvm-link \$@
EOF
chmod +x /usr/x86_64-w64-mingw32/bin/llvm-link

cat >/usr/x86_64-w64-mingw32/bin/opt <<EOF
#!/bin/sh
wine64 opt \$@
EOF
chmod +x /usr/x86_64-w64-mingw32/bin/opt

cat >/usr/x86_64-w64-mingw32/bin/llvm-spirv <<EOF
#!/bin/sh
wine64 llvm-spirv \$@
EOF
chmod +x /usr/x86_64-w64-mingw32/bin/llvm-spirv
