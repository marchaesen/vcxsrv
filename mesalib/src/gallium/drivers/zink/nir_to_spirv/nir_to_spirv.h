/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef NIR_TO_SPIRV_H
#define NIR_TO_SPIRV_H

#include <stdlib.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "compiler/nir/nir.h"
#include "compiler/shader_enums.h"
#include "pipe/p_state.h"

#include "zink_compiler.h"

struct spirv_shader {
   uint32_t *words;
   size_t num_words;
};

struct nir_shader;
struct pipe_stream_output_info;

struct spirv_shader *
nir_to_spirv(struct nir_shader *s, const struct zink_so_info *so_info,
             unsigned char *shader_slot_map, unsigned char *shader_slots_reserved);

void
spirv_shader_delete(struct spirv_shader *s);

uint32_t
zink_binding(gl_shader_stage stage, VkDescriptorType type, int index);

static inline VkDescriptorType
zink_sampler_type(const struct glsl_type *type)
{
   assert(glsl_type_is_sampler(type));
   if (glsl_get_sampler_dim(type) < GLSL_SAMPLER_DIM_BUF || glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_MS)
      return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
   if (glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_BUF)
      return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
   unreachable("unimplemented");
   return 0;
}

struct nir_shader;

bool
zink_nir_lower_b2b(struct nir_shader *shader);

#endif
