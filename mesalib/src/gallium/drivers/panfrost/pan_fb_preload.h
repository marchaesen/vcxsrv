/*
 * Copyright (C) 2021 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef __PAN_FB_PRELOAD_H
#define __PAN_FB_PRELOAD_H

#include "util/format/u_format.h"
#include "pan_desc.h"
#include "pan_pool.h"
#include "pan_texture.h"
#include "pan_util.h"

struct pan_blend_shader_cache;
struct pan_fb_info;
struct pan_jc;
struct pan_pool;

struct pan_fb_preload_cache {
   unsigned gpu_id;
   struct {
      struct pan_pool *pool;
      struct hash_table *preload;
      struct hash_table *blend;
      pthread_mutex_t lock;
   } shaders;
   struct {
      struct pan_pool *pool;
      struct hash_table *rsds;
      pthread_mutex_t lock;
   } rsds;
   struct pan_blend_shader_cache *blend_shader_cache;
};

#ifdef PAN_ARCH
void GENX(pan_fb_preload_cache_init)(
   struct pan_fb_preload_cache *cache, unsigned gpu_id,
   struct pan_blend_shader_cache *blend_shader_cache, struct pan_pool *bin_pool,
   struct pan_pool *desc_pool);

void GENX(pan_fb_preload_cache_cleanup)(struct pan_fb_preload_cache *cache);

unsigned GENX(pan_preload_fb)(struct pan_fb_preload_cache *cache,
                              struct pan_pool *desc_pool,
                              struct pan_fb_info *fb, mali_ptr tsd,
                              struct panfrost_ptr *jobs);
#endif

#endif
