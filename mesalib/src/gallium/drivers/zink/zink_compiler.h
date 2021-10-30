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

#ifndef ZINK_COMPILER_H
#define ZINK_COMPILER_H

#include "pipe/p_defines.h"
#include "pipe/p_state.h"

#include "compiler/nir/nir.h"
#include "compiler/shader_info.h"
#include "util/u_live_shader_cache.h"

#include <vulkan/vulkan.h>
#include "zink_descriptors.h"

#define ZINK_WORKGROUP_SIZE_X 1
#define ZINK_WORKGROUP_SIZE_Y 2
#define ZINK_WORKGROUP_SIZE_Z 3

struct pipe_screen;
struct zink_context;
struct zink_screen;
struct zink_shader_key;
struct zink_shader_module;
struct zink_gfx_program;

struct nir_shader_compiler_options;
struct nir_shader;

struct set;

struct tgsi_token;
struct zink_so_info {
   struct pipe_stream_output_info so_info;
   unsigned so_info_slots[PIPE_MAX_SO_OUTPUTS];
   bool have_xfb;
};


const void *
zink_get_compiler_options(struct pipe_screen *screen,
                          enum pipe_shader_ir ir,
                          enum pipe_shader_type shader);

struct nir_shader *
zink_tgsi_to_nir(struct pipe_screen *screen, const struct tgsi_token *tokens);

struct zink_shader {
   struct util_live_shader base;
   uint32_t hash;
   struct nir_shader *nir;
   enum pipe_prim_type reduced_prim; // PIPE_PRIM_MAX for vs

   struct zink_so_info streamout;

   struct {
      int index;
      int binding;
      VkDescriptorType type;
      unsigned char size;
   } bindings[ZINK_DESCRIPTOR_TYPES][ZINK_MAX_DESCRIPTORS_PER_TYPE];
   size_t num_bindings[ZINK_DESCRIPTOR_TYPES];
   unsigned num_texel_buffers;
   uint32_t ubos_used; // bitfield of which ubo indices are used
   uint32_t ssbos_used; // bitfield of which ssbo indices are used
   bool bindless;

   simple_mtx_t lock;
   struct set *programs;

   union {
      struct zink_shader *generated; // a generated shader that this shader "owns"
      bool is_generated; // if this is a driver-created shader (e.g., tcs)
      nir_variable *fbfetch; //for fs output
   };
};

void
zink_screen_init_compiler(struct zink_screen *screen);
void
zink_compiler_assign_io(nir_shader *producer, nir_shader *consumer);
VkShaderModule
zink_shader_compile(struct zink_screen *screen, struct zink_shader *zs, nir_shader *nir, const struct zink_shader_key *key);

struct zink_shader *
zink_shader_create(struct zink_screen *screen, struct nir_shader *nir,
                 const struct pipe_stream_output_info *so_info);

char *
zink_shader_finalize(struct pipe_screen *pscreen, void *nirptr);

void
zink_shader_free(struct zink_context *ctx, struct zink_shader *shader);

struct zink_shader *
zink_shader_tcs_create(struct zink_screen *screen, struct zink_shader *vs, unsigned vertices_per_patch);

static inline bool
zink_shader_descriptor_is_buffer(struct zink_shader *zs, enum zink_descriptor_type type, unsigned i)
{
   return zs->bindings[type][i].type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER ||
          zs->bindings[type][i].type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
}

#endif
