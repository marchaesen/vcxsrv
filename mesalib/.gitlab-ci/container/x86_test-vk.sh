#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

# Ephemeral packages (installed for this script and removed again at the end)
STABLE_EPHEMERAL=" \
      ccache \
      cmake \
      g++ \
      libgbm-dev \
      libgles2-mesa-dev \
      liblz4-dev \
      libpciaccess-dev \
      libpng-dev \
      libvulkan-dev \
      libwaffle-dev \
      libxcb-ewmh-dev \
      libxcb-keysyms1-dev \
      libxkbcommon-dev \
      libxrandr-dev \
      libxrender-dev \
      libzstd-dev \
      meson \
      p7zip \
      patch \
      pkg-config \
      python3-distutils \
      wget \
      xz-utils \
      "

apt-get install -y --no-remove \
      $STABLE_EPHEMERAL \
      libxcb-shm0 \
      python3-lxml \
      python3-simplejson

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

############### Build piglit

PIGLIT_BUILD_TARGETS="piglit_replayer" . .gitlab-ci/container/build-piglit.sh

############### Build dEQP runner (and install rust temporarily for it)
. .gitlab-ci/container/build-rust.sh
. .gitlab-ci/container/build-deqp-runner.sh
rm -rf /root/.rustup /root/.cargo

############### Build Fossilize

. .gitlab-ci/container/build-fossilize.sh

############### Build dEQP VK
. .gitlab-ci/container/build-deqp.sh

############### Build gfxreconstruct

. .gitlab-ci/container/build-gfxreconstruct.sh

############### Build libdrm

. .gitlab-ci/container/build-libdrm.sh

############### Uninstall the build software

ccache --show-stats

apt-get purge -y \
      $STABLE_EPHEMERAL

apt-get autoremove -y --purge
