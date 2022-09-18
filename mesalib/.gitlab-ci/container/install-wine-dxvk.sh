#!/bin/bash

dxvk_install_release() {
    local DXVK_VERSION=${1:-"1.10.1"}

    wget "https://github.com/doitsujin/dxvk/releases/download/v${DXVK_VERSION}/dxvk-${DXVK_VERSION}.tar.gz"
    tar xzpf dxvk-"${DXVK_VERSION}".tar.gz
    "dxvk-${DXVK_VERSION}"/setup_dxvk.sh install
    rm -rf "dxvk-${DXVK_VERSION}"
    rm dxvk-"${DXVK_VERSION}".tar.gz
}

# Install from a Github PR number
dxvk_install_pr() {
    local __prnum=$1

    # NOTE: Clone all the ensite history of the repo so as not to think
    # harder about cloning just enough for 'git describe' to work.  'git
    # describe' is used by the dxvk build system to generate a
    # dxvk_version Meson variable, which is nice-to-have.
    git clone https://github.com/doitsujin/dxvk
    pushd dxvk || exit 1
    git fetch origin pull/"$__prnum"/head:pr
    git checkout pr
    ./package-release.sh pr ../dxvk-build --no-package
    popd || exit 1
    pushd ./dxvk-build/dxvk-pr || exit 1
    ./setup_dxvk.sh install
    popd || exit 1
    rm -rf ./dxvk-build ./dxvk
}

dxvk_install_release "1.10.1"
#dxvk_install_pr 2359

