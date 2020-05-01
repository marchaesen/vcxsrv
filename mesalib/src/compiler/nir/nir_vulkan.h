/* SPDX-License-Identifier: MIT */

#include "nir.h"
#include "nir_builder.h"
#include "vulkan/vulkan_core.h"

nir_ssa_def *
nir_convert_ycbcr_to_rgb(nir_builder *b,
                         VkSamplerYcbcrModelConversion model,
                         VkSamplerYcbcrRange range,
                         nir_ssa_def *raw_channels,
                         uint32_t *bpcs);
