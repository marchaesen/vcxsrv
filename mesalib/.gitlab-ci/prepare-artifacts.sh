#!/bin/bash

set -e
set -o xtrace

# Delete unused bin and includes from artifacts to save space.
rm -rf install/bin install/include

# Strip the drivers in the artifacts to cut 80% of the artifacts size.
if [ -n "$CROSS" ]; then
    STRIP=`sed -n -E "s/strip\s*=\s*'(.*)'/\1/p" "$CROSS_FILE"`
    if [ -z "$STRIP" ]; then
        echo "Failed to find strip command in cross file"
        exit 1
    fi
else
    STRIP="strip"
fi
find install -name \*.so -exec $STRIP {} \;

# Test runs don't pull down the git tree, so put the dEQP helper
# script and associated bits there.
mkdir -p artifacts/
cp VERSION artifacts/
cp -Rp .gitlab-ci/deqp* artifacts/
cp -Rp .gitlab-ci/piglit artifacts/

# Tar up the install dir so that symlinks and hardlinks aren't each
# packed separately in the zip file.
tar -cf artifacts/install.tar install
