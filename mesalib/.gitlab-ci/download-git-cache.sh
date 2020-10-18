#!/bin/bash

set +e
set -o xtrace

# if we run this script outside of gitlab-ci for testing, ensure
# we got meaningful variables
CI_PROJECT_DIR=${CI_PROJECT_DIR:-$(mktemp -d)/mesa}

if [[ -e $CI_PROJECT_DIR/.git ]]
then
    echo "Repository already present, skip cache download"
    exit
fi

TMP_DIR=$(mktemp -d)

echo "Downloading archived master..."
/usr/bin/wget -O $TMP_DIR/mesa.tar.gz \
              https://minio-packet.freedesktop.org/git-cache/mesa/mesa/mesa.tar.gz

# check wget error code
if [[ $? -ne 0 ]]
then
    echo "Repository cache not available"
    exit
fi

set -e

rm -rf "$CI_PROJECT_DIR"
echo "Extracting tarball into '$CI_PROJECT_DIR'..."
mkdir -p "$CI_PROJECT_DIR"
tar xzf "$TMP_DIR/mesa.tar.gz" -C "$CI_PROJECT_DIR"
rm -rf "$TMP_DIR"
chmod a+w "$CI_PROJECT_DIR"
