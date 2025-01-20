#!/bin/bash
# Copyright 2022 Android Open Source Project
# SPDX-License-Identifier: MIT

# Please run this from the Mesa codegen directory
# >> cd ${mesa_dir}/src/gfxstream/codegen
# >> ./generate-gfxstream-vulkan.sh ${gfxstream_dir}
#
# Note in AOSP, ${gfxstream_dir} is optional, and the script autodetects the
# path to gfxstream.

export MESA_DIR="$PWD/../../.."
if [ -z "$1" ];
then
    export GFXSTREAM_DIR="$MESA_DIR/../../hardware/google/gfxstream"
else
    export GFXSTREAM_DIR="$1"
fi

export PREFIX_DIR="src/gfxstream"

# We should use just use one vk.xml eventually..
export VK_MESA_XML="$MESA_DIR/src/vulkan/registry/vk.xml"
export VK_XML="$GFXSTREAM_DIR/codegen/vulkan/vulkan-docs-next/xml/vk.xml"

export GFXSTREAM_GUEST_ENCODER_DIR="/tmp/"
export GFXSTREAM_HOST_DECODER_DIR="$GFXSTREAM_DIR/host/vulkan"
export GFXSTREAM_OUTPUT_DIR="$GFXSTREAM_HOST_DECODER_DIR/cereal"
export GFXSTREAM_SCRIPTS_DIR="$GFXSTREAM_DIR/scripts"

export GEN_VK="$MESA_DIR/$PREFIX_DIR/codegen/scripts/genvk.py"
export CUSTOM_XML="$MESA_DIR/$PREFIX_DIR/codegen/xml/vk_gfxstream.xml"

python3 "$GEN_VK" -registry "$VK_XML" -registryGfxstream "$CUSTOM_XML" cereal -o "$GFXSTREAM_OUTPUT_DIR"

export CEREAL_VARIANT=guest
export GFXSTREAM_GUEST_ENCODER_DIR="$MESA_DIR/src/gfxstream/guest/vulkan_enc"
python3 "$GEN_VK" -registry "$VK_MESA_XML" -registryGfxstream "$CUSTOM_XML" cereal -o /tmp/

# Should have a unified headers dir here:
python3 "$GEN_VK" -registry "$CUSTOM_XML" vulkan_gfxstream.h -o "$GFXSTREAM_GUEST_ENCODER_DIR"
python3 "$GEN_VK" -registry "$CUSTOM_XML" vulkan_gfxstream.h -o "$GFXSTREAM_HOST_DECODER_DIR"
