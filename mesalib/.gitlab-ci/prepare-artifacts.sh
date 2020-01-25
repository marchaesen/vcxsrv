#!/bin/bash

set -e
set -o xtrace

CROSS_FILE=/cross_file-"$CROSS".txt

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

# If the container has LAVA stuff, prepare the artifacts for LAVA jobs
if [ -d /lava-files ]; then
        # Copy kernel and device trees for LAVA
        cp /lava-files/*Image artifacts/.
        cp /lava-files/*.dtb artifacts/.

        # Pack ramdisk for LAVA
        mkdir -p /lava-files/rootfs-${CROSS:-arm64}/mesa
        cp -a install/* /lava-files/rootfs-${CROSS:-arm64}/mesa/.

        cp .gitlab-ci/deqp-runner.sh /lava-files/rootfs-${CROSS:-arm64}/deqp/.
        cp .gitlab-ci/deqp-*-fails.txt /lava-files/rootfs-${CROSS:-arm64}/deqp/.
        cp .gitlab-ci/deqp-*-skips.txt /lava-files/rootfs-${CROSS:-arm64}/deqp/.
        find /lava-files/rootfs-${CROSS:-arm64}/ -type f -printf "%s\t%i\t%p\n" | sort -n | tail -100

        pushd /lava-files/rootfs-${CROSS:-arm64}/
        find -H  |  cpio -H newc -o | gzip -c - > $CI_PROJECT_DIR/artifacts/lava-rootfs-${CROSS:-arm64}.cpio.gz
        popd

        # Store job ID so the test stage can build URLs to the artifacts
        echo $CI_JOB_ID > artifacts/build_job_id.txt

        # Pass needed files to the test stage
        cp $CI_PROJECT_DIR/.gitlab-ci/generate_lava.py artifacts/.
        cp $CI_PROJECT_DIR/.gitlab-ci/lava-deqp.yml.jinja2 artifacts/.
fi
