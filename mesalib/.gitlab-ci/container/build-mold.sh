#!/bin/bash

set -ex
 
export MOLD_VERSION="1.4.1"
git clone -b v"$MOLD_VERSION" --single-branch --depth 1 https://github.com/rui314/mold.git
cd mold
make
make install
cd ..
rm -rf mold
