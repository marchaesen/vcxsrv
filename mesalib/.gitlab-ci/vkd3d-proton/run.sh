#!/bin/sh

set -ex

if [ "x$VK_DRIVER" = "x" ]; then
    exit 1
fi

INSTALL=$(realpath -s "$PWD"/install)

RESULTS=$(realpath -s "$PWD"/results)

# Set up the driver environment.
# Modifiying here directly LD_LIBRARY_PATH may cause problems when
# using a command wrapper. Hence, we will just set it when running the
# command.
export __LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$INSTALL/lib/"


# Sanity check to ensure that our environment is sufficient to make our tests
# run against the Mesa built by CI, rather than any installed distro version.
MESA_VERSION=$(cat "$INSTALL/VERSION" | sed 's/\./\\./g')

# Set the Vulkan driver to use.
export VK_ICD_FILENAMES="$INSTALL/share/vulkan/icd.d/${VK_DRIVER}_icd.x86_64.json"

# Set environment for Wine.
export WINEDEBUG="-all"
export WINEPREFIX="/vkd3d-proton-wine64"
export WINEESYNC=1

print_red() {
    RED='\033[0;31m'
    NC='\033[0m' # No Color
    printf "${RED}"
    "$@"
    printf "${NC}"
}

# wrapper to supress +x to avoid spamming the log
quiet() {
    set +x
    "$@"
    set -x
}

SANITY_MESA_VERSION_CMD="vulkaninfo | tee /tmp/version.txt | grep \"Mesa $MESA_VERSION\(\s\|$\)\""

HANG_DETECTION_CMD="/parallel-deqp-runner/build/bin/hang-detection"

RUN_CMD="export LD_LIBRARY_PATH=$__LD_LIBRARY_PATH; $SANITY_MESA_VERSION_CMD"

set +e
eval $RUN_CMD

if [ $? -ne 0 ]; then
    printf "%s\n" "Found $(cat /tmp/version.txt), expected $MESA_VERSION"
fi
set -e

if [ -d "$RESULTS" ]; then
    cd "$RESULTS" && rm -rf ..?* .[!.]* * && cd -
else
    mkdir "$RESULTS"
fi

VKD3D_PROTON_TESTSUITE_CMD="wine /vkd3d-proton-tests/x64/bin/d3d12.exe >$RESULTS/vkd3d-proton.log 2>&1"

quiet printf "%s\n" "Running vkd3d-proton testsuite..."
RUN_CMD="export LD_LIBRARY_PATH=$__LD_LIBRARY_PATH; $HANG_DETECTION_CMD $VKD3D_PROTON_TESTSUITE_CMD"

set +e
eval $RUN_CMD

VKD3D_PROTON_RESULTS="vkd3d-proton-${VKD3D_PROTON_RESULTS:-results}"
RESULTSFILE="$RESULTS/$VKD3D_PROTON_RESULTS.txt"
mkdir -p .gitlab-ci/vkd3d-proton
grep "Test failed" "$RESULTS"/vkd3d-proton.log > "$RESULTSFILE"

if [ -f "$INSTALL/$VKD3D_PROTON_RESULTS.txt" ]; then
    cp "$INSTALL/$VKD3D_PROTON_RESULTS.txt" \
       ".gitlab-ci/vkd3d-proton/$VKD3D_PROTON_RESULTS.txt.baseline"
else
    touch ".gitlab-ci/vkd3d-proton/$VKD3D_PROTON_RESULTS.txt.baseline"
fi

if diff -q ".gitlab-ci/vkd3d-proton/$VKD3D_PROTON_RESULTS.txt.baseline" "$RESULTSFILE"; then
    exit 0
fi

quiet print_red printf "%s\n" "Changes found, see vkd3d-proton.log!"
quiet diff --color=always -u ".gitlab-ci/vkd3d-proton/$VKD3D_PROTON_RESULTS.txt.baseline" "$RESULTSFILE"
exit 1
