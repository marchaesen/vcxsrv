/*
 * Copyright 2020 Mike Blumenkrantz
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


/** this file exists for organization and to be included in nir_to_spirv/ without pulling in extra deps */
#ifndef ZINK_SHADER_KEYS_H
# define ZINK_SHADER_KEYS_H

#include "compiler/shader_info.h"

struct zink_vs_key_base {
   bool clip_halfz;
   bool push_drawid;
   bool last_vertex_stage;
};

struct zink_vs_key {
   struct zink_vs_key_base base;
   uint8_t pad;
   union {
      struct {
         uint32_t decomposed_attrs;
         uint32_t decomposed_attrs_without_w;
      } u32;
      struct {
         uint16_t decomposed_attrs;
         uint16_t decomposed_attrs_without_w;
      } u16;
      struct {
         uint8_t decomposed_attrs;
         uint8_t decomposed_attrs_without_w;
      } u8;
   };
   // not hashed
   unsigned size;
};

struct zink_fs_key {
   uint8_t coord_replace_bits;
   bool coord_replace_yinvert;
   bool samples;
   bool force_dual_color_blend;
};

struct zink_shader_key_base {
   uint32_t inlined_uniform_values[MAX_INLINABLE_UNIFORMS];
};

/* a shader key is used for swapping out shader modules based on pipeline states,
 * e.g., if sampleCount changes, we must verify that the fs doesn't need a recompile
 *       to account for GL ignoring gl_SampleMask in some cases when VK will not
 * which allows us to avoid recompiling shaders when the pipeline state changes repeatedly
 */
struct zink_shader_key {
   union {
      /* reuse vs key for now with tes/gs since we only use clip_halfz */
      struct zink_vs_key vs;
      struct zink_vs_key_base vs_base;
      struct zink_fs_key fs;
   } key;
   struct zink_shader_key_base base;
   unsigned inline_uniforms:1;
   uint32_t size;
};

static inline const struct zink_fs_key *
zink_fs_key(const struct zink_shader_key *key)
{
   assert(key);
   return &key->key.fs;
}

static inline const struct zink_vs_key_base *
zink_vs_key_base(const struct zink_shader_key *key)
{
   return &key->key.vs_base;
}

static inline const struct zink_vs_key *
zink_vs_key(const struct zink_shader_key *key)
{
   assert(key);
   return &key->key.vs;
}



#endif
