#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_PYUTILS_TAG

set -e

. .gitlab-ci/setup-test-env.sh

set -o xtrace

export DEBIAN_FRONTEND=noninteractive

apt-get install -y ca-certificates
sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list.d/*
echo "deb [trusted=yes] https://gitlab.freedesktop.org/gfx-ci/ci-deb-repo/-/raw/${PKG_REPO_REV}/ ${FDO_DISTRIBUTION_VERSION%-*} main" | tee /etc/apt/sources.list.d/gfx-ci_.list

# Ephemeral packages (installed for this script and removed again at
# the end)
EPHEMERAL=(
    binutils
    build-essential
    cpp
    dpkg-dev
    g++
    gcc
    libc6-dev
    perl
    python3-dev
)

DEPS=(
    apt-utils
    curl
    file
    findutils
    git
    python3-pil
    python3-pip
    python3-ply
    python3-setuptools
    python3-venv
    python3-yaml
    shellcheck
    xz-utils
    yamllint
    zstd
)

apt-get update

apt-get install -y --no-remove --no-install-recommends "${DEPS[@]}" "${EPHEMERAL[@]}" \
        "${EXTRA_LOCAL_PACKAGES:-}"

# Needed for ci-fairy, this revision is able to upload files to S3
pip3 install --break-system-packages git+http://gitlab.freedesktop.org/freedesktop/ci-templates@ffe4d1b10aab7534489f0c4bbc4c5899df17d3f2

pip3 install --break-system-packages -r bin/ci/test/requirements.txt

############### Uninstall ephemeral packages

apt-get purge -y "${EPHEMERAL[@]}"
apt-get autoremove --purge -y

. .gitlab-ci/container/container_post_build.sh
