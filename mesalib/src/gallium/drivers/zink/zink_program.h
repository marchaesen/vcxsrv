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

#ifndef ZINK_PROGRAM_H
#define ZINK_PROGRAM_H

#include <vulkan/vulkan.h>

#include "compiler/shader_enums.h"
#include "pipe/p_state.h"
#include "util/u_inlines.h"

#include "zink_context.h"
#include "zink_shader_keys.h"

struct zink_screen;
struct zink_shader;
struct zink_gfx_pipeline_state;

struct hash_table;
struct set;

/* a shader module is used for directly reusing a shader module between programs,
 * e.g., in the case where we're swapping out only one shader,
 * allowing us to skip going through shader keys
 */
struct zink_shader_module {
   struct pipe_reference reference;
   VkShaderModule shader;
};

/* the shader cache stores a mapping of zink_shader_key::VkShaderModule */
struct zink_shader_cache {
   struct pipe_reference reference;
   struct hash_table *shader_cache;
};

struct zink_gfx_program {
   struct pipe_reference reference;

   struct zink_shader_module *modules[ZINK_SHADER_COUNT]; // compute stage doesn't belong here
   struct zink_shader *shaders[ZINK_SHADER_COUNT];
   struct zink_shader_cache *shader_cache;
   unsigned char shader_slot_map[VARYING_SLOT_MAX];
   unsigned char shader_slots_reserved;
   VkDescriptorSetLayout dsl;
   VkPipelineLayout layout;
   unsigned num_descriptors;
   struct hash_table *pipelines[11]; // number of draw modes we support
   struct set *render_passes;
};


void
zink_update_gfx_program(struct zink_context *ctx, struct zink_gfx_program *prog);

struct zink_gfx_program *
zink_create_gfx_program(struct zink_context *ctx,
                        struct zink_shader *stages[ZINK_SHADER_COUNT]);

void
zink_destroy_gfx_program(struct zink_screen *screen,
                         struct zink_gfx_program *prog);

VkPipeline
zink_get_gfx_pipeline(struct zink_screen *screen,
                      struct zink_gfx_program *prog,
                      struct zink_gfx_pipeline_state *state,
                      enum pipe_prim_type mode);

void
zink_program_init(struct zink_context *ctx);

void
debug_describe_zink_gfx_program(char* buf, const struct zink_gfx_program *ptr);

static inline void
zink_gfx_program_reference(struct zink_screen *screen,
                           struct zink_gfx_program **dst,
                           struct zink_gfx_program *src)
{
   struct zink_gfx_program *old_dst = dst ? *dst : NULL;

   if (pipe_reference_described(old_dst ? &old_dst->reference : NULL, &src->reference,
                                (debug_reference_descriptor)debug_describe_zink_gfx_program))
      zink_destroy_gfx_program(screen, old_dst);
   if (dst) *dst = src;
}
#endif
