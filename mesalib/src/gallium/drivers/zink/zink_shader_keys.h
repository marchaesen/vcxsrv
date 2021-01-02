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

struct zink_fs_key {
   unsigned shader_id;
   //bool flat_shade;
   bool samples;
};

struct zink_tcs_key {
   unsigned shader_id;
   unsigned vertices_per_patch;
   uint64_t vs_outputs_written;
};

/* a shader key is used for swapping out shader modules based on pipeline states,
 * e.g., if sampleCount changes, we must verify that the fs doesn't need a recompile
 *       to account for GL ignoring gl_SampleMask in some cases when VK will not
 * which allows us to avoid recompiling shaders when the pipeline state changes repeatedly
 */
struct zink_shader_key {
   union {
      struct zink_fs_key fs;
      struct zink_tcs_key tcs;
   } key;
   uint32_t size;
};

static inline const struct zink_fs_key *
zink_fs_key(const struct zink_shader_key *key)
{
   return &key->key.fs;
}



#endif
