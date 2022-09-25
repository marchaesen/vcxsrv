/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#ifndef TU_SHADER_H
#define TU_SHADER_H

#include "tu_common.h"

struct tu_push_constant_range
{
   uint32_t lo;
   uint32_t dwords;
};

struct tu_const_state
{
   struct tu_push_constant_range push_consts;
   uint32_t dynamic_offset_loc;
};

struct tu_shader
{
   struct ir3_shader *ir3_shader;

   struct tu_const_state const_state;
   unsigned reserved_user_consts_vec4;
   uint8_t active_desc_sets;
};

struct tu_shader_key {
   unsigned multiview_mask;
   bool force_sample_interp;
   enum ir3_wavesize_option api_wavesize, real_wavesize;
};

bool
tu_nir_lower_multiview(nir_shader *nir, uint32_t mask, struct tu_device *dev);

nir_shader *
tu_spirv_to_nir(struct tu_device *dev,
                void *mem_ctx,
                const VkPipelineShaderStageCreateInfo *stage_info,
                gl_shader_stage stage);

struct tu_shader *
tu_shader_create(struct tu_device *dev,
                 nir_shader *nir,
                 const struct tu_shader_key *key,
                 struct tu_pipeline_layout *layout,
                 const VkAllocationCallbacks *alloc);

void
tu_shader_destroy(struct tu_device *dev,
                  struct tu_shader *shader,
                  const VkAllocationCallbacks *alloc);

#endif /* TU_SHADER_H */
