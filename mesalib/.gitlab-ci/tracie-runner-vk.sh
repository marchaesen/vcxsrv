#!/bin/sh

set -ex

INSTALL="$(pwd)/install"

# Set the Vulkan driver to use.
export VK_ICD_FILENAMES="$(pwd)/install/share/vulkan/icd.d/${VK_DRIVER}_icd.x86_64.json"

# Set environment for VulkanTools' VK_LAYER_LUNARG_screenshot layer.
export VK_LAYER_PATH="$VK_LAYER_PATH:/VulkanTools/build/etc/vulkan/explicit_layer.d"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/VulkanTools/build/lib"

# Set environment for Wine
export WINEDEBUG="-all"
export WINEPREFIX="/dxvk-wine64"
export WINEESYNC=1

# Set environment for DXVK
export DXVK_LOG_LEVEL="none"
export DXVK_STATE_CACHE=0

# Perform a self-test to ensure tracie is working properly.
python3 -m pytest -v --pyargs $INSTALL/tracie/tests/test.py

# Sanity check to ensure that our environment is sufficient to make our tests
# run against the Mesa built by CI, rather than any installed distro version.
MESA_VERSION=$(cat "$INSTALL/VERSION" | sed 's/\./\\./g')
vulkaninfo | grep "Mesa $MESA_VERSION\(\s\|$\)"

# Run gfxreconstruct traces against the host's running X server (xvfb
# doesn't have DRI3 support).
# Set the DISPLAY env variable in each gitlab-runner's configuration
# file:
# https://docs.gitlab.com/runner/configuration/advanced-configuration.html#the-runners-section
PATH="/gfxreconstruct/build/bin:$PATH" \
    python3 "$INSTALL/tracie/tracie.py" --file "$INSTALL/traces-$DRIVER_NAME.yml" --device-name "$DEVICE_NAME"
