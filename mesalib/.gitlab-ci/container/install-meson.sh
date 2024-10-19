#!/usr/bin/env bash

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_BUILD_TAG
# FEDORA_X86_64_BUILD_TAG

rm -f /usr/lib/python3.*/EXTERNALLY-MANAGED

# We need at least 1.4.0 for rusticl
pip3 install 'meson==1.4.0'
