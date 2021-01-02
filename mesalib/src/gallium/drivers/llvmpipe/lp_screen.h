/**************************************************************************
 * 
 * Copyright 2009 VMware, Inc.
 * Copyright 2007 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

/**
 * @author Jose Fonseca <jfonseca@vmware.com>
 * @author Keith Whitwell <keithw@vmware.com>
 */

#ifndef LP_SCREEN_H
#define LP_SCREEN_H

#include "pipe/p_screen.h"
#include "pipe/p_defines.h"
#include "os/os_thread.h"
#include "gallivm/lp_bld.h"
#include "gallivm/lp_bld_misc.h"

struct sw_winsys;
struct lp_cs_tpool;

struct llvmpipe_screen
{
   struct pipe_screen base;

   struct sw_winsys *winsys;

   unsigned num_threads;

   /* Increments whenever textures are modified.  Contexts can track this.
    */
   unsigned timestamp;

   struct lp_rasterizer *rast;
   mtx_t rast_mutex;

   struct lp_cs_tpool *cs_tpool;
   mtx_t cs_mutex;

   bool use_tgsi;
   bool allow_cl;

   struct disk_cache *disk_shader_cache;
   unsigned num_disk_shader_cache_hits;
   unsigned num_disk_shader_cache_misses;
};

void lp_disk_cache_find_shader(struct llvmpipe_screen *screen,
                               struct lp_cached_code *cache,
                               unsigned char ir_sha1_cache_key[20]);
void lp_disk_cache_insert_shader(struct llvmpipe_screen *screen,
                                 struct lp_cached_code *cache,
                                 unsigned char ir_sha1_cache_key[20]);


static inline struct llvmpipe_screen *
llvmpipe_screen( struct pipe_screen *pipe )
{
   return (struct llvmpipe_screen *)pipe;
}

static inline unsigned lp_get_constant_buffer_stride(struct pipe_screen *_screen)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);
   return screen->use_tgsi ? (sizeof(float) * 4) : sizeof(float);
}

#endif /* LP_SCREEN_H */
