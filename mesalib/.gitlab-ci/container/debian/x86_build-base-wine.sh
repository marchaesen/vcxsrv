#!/bin/bash

set -e
set -o xtrace

# Installing wine, need this for testing mingw or nine

apt-get update
apt-get install -y --no-remove \
      wine \
      wine64 \
      xvfb

# Used to initialize the Wine environment to reduce build time
wine64 whoami.exe

