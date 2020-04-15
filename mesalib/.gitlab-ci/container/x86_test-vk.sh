#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

apt-get install -y \
      ca-certificates \
      gnupg

# Upstream LLVM package repository
apt-key add .gitlab-ci/container/llvm-snapshot.gpg.key
echo "deb https://apt.llvm.org/buster/ llvm-toolchain-buster-9 main" >/etc/apt/sources.list.d/llvm9.list

sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
echo 'deb https://deb.debian.org/debian buster-backports main' >/etc/apt/sources.list.d/backports.list

apt-get update

apt-get dist-upgrade -y

apt-get install -y --no-remove \
      ccache \
      cmake \
      g++ \
      gcc \
      git \
      git-lfs \
      libexpat1 \
      libgbm-dev \
      libgles2-mesa-dev \
      libllvm9 \
      liblz4-1 \
      liblz4-dev \
      libpng-dev \
      libpng16-16 \
      libvulkan-dev \
      libvulkan1 \
      libwayland-client0 \
      libwayland-server0 \
      libxcb-ewmh-dev \
      libxcb-ewmh2 \
      libxcb-keysyms1 \
      libxcb-keysyms1-dev \
      libxcb-randr0 \
      libxcb-xfixes0 \
      libxkbcommon-dev \
      libxkbcommon0 \
      libxrandr-dev \
      libxrandr2 \
      libxrender-dev \
      libxrender1 \
      meson \
      p7zip \
      pkg-config \
      python \
      python3-distutils \
      python3-pil \
      python3-requests \
      python3-yaml \
      vulkan-tools \
      wget \
      xauth \
      xvfb

# We need multiarch for Wine
dpkg --add-architecture i386

apt-get update

apt-get install -y --no-remove \
      wine \
      wine32 \
      wine64

############### Set up Wine env variables

export WINEDEBUG="-all"
export WINEPREFIX="/dxvk-wine64"

############### Install DXVK

DXVK_VERSION="1.6"

# We don't want crash dialogs
cat >crashdialog.reg <<EOF
Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Wine\WineDbg]
"ShowCrashDialog"=dword:00000000

EOF

# Set the wine prefix and disable the crash dialog
wine regedit crashdialog.reg
rm crashdialog.reg

# DXVK's setup often fails with:
# "${WINEPREFIX}: Not a valid wine prefix."
# and that is just spit because of checking the existance of the
# system.reg file, which fails.
# Just giving it a bit more of time for it to be created solves the
# problem ...
test -f  "${WINEPREFIX}/system.reg" || sleep 2

wget "https://github.com/doitsujin/dxvk/releases/download/v${DXVK_VERSION}/dxvk-${DXVK_VERSION}.tar.gz"
tar xzpf dxvk-"${DXVK_VERSION}".tar.gz
dxvk-"${DXVK_VERSION}"/setup_dxvk.sh install
rm -rf dxvk-"${DXVK_VERSION}"
rm dxvk-"${DXVK_VERSION}".tar.gz

############### Install Windows' apitrace binaries

APITRACE_VERSION="9.0"
APITRACE_VERSION_DATE="20191126"

wget "https://github.com/apitrace/apitrace/releases/download/${APITRACE_VERSION}/apitrace-${APITRACE_VERSION}.${APITRACE_VERSION_DATE}-win64.7z"
7zr x "apitrace-${APITRACE_VERSION}.${APITRACE_VERSION_DATE}-win64.7z" \
      "apitrace-${APITRACE_VERSION}.${APITRACE_VERSION_DATE}-win64/bin/apitrace.exe" \
      "apitrace-${APITRACE_VERSION}.${APITRACE_VERSION_DATE}-win64/bin/d3dretrace.exe"
mv "apitrace-${APITRACE_VERSION}.${APITRACE_VERSION_DATE}-win64" /apitrace-msvc-win64
rm "apitrace-${APITRACE_VERSION}.${APITRACE_VERSION_DATE}-win64.7z"

# Add the apitrace path to the registry
wine \
    reg add "HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment" \
    /v Path \
    /t REG_EXPAND_SZ \
    /d "C:\windows\system32;C:\windows;C:\windows\system32\wbem;Z:\apitrace-msvc-win64\bin" \
    /f

############### Building ...

. .gitlab-ci/container/container_pre_build.sh

############### Build dEQP runner

. .gitlab-ci/build-cts-runner.sh

############### Build Fossilize

. .gitlab-ci/build-fossilize.sh

############### Build dEQP VK

. .gitlab-ci/build-deqp-vk.sh

############### Build gfxreconstruct

. .gitlab-ci/build-gfxreconstruct.sh

############### Build VulkanTools

. .gitlab-ci/build-vulkantools.sh

############### Uninstall the build software

ccache --show-stats

apt-get purge -y \
      ccache \
      cmake \
      g++ \
      gcc \
      gnupg \
      libgbm-dev \
      libgles2-mesa-dev \
      liblz4-dev \
      libpng-dev \
      libvulkan-dev \
      libxcb-ewmh-dev \
      libxcb-keysyms1-dev \
      libxkbcommon-dev \
      libxrandr-dev \
      libxrender-dev \
      meson \
      p7zip \
      pkg-config \
      wget

apt-get autoremove -y --purge
