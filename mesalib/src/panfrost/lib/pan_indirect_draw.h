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
 */

#ifndef __PAN_INDIRECT_DRAW_SHADERS_H__
#define __PAN_INDIRECT_DRAW_SHADERS_H__

#include "genxml/gen_macros.h"

struct pan_device;
struct pan_scoreboard;
struct pan_pool;

struct pan_indirect_draw_info {
        mali_ptr draw_buf;
        mali_ptr index_buf;
        mali_ptr first_vertex_sysval;
        mali_ptr base_vertex_sysval;
        mali_ptr base_instance_sysval;
        mali_ptr vertex_job;
        mali_ptr tiler_job;
        mali_ptr attrib_bufs;
        mali_ptr attribs;
        mali_ptr varying_bufs;
        unsigned attrib_count;
        uint32_t restart_index;
        unsigned flags;
        unsigned index_size;
        unsigned last_indirect_draw;
};

unsigned
GENX(panfrost_emit_indirect_draw)(struct pan_pool *pool,
                                  struct pan_scoreboard *scoreboard,
                                  const struct pan_indirect_draw_info *draw_info,
                                  struct panfrost_ptr *ctx);

void
GENX(panfrost_init_indirect_draw_shaders)(struct panfrost_device *dev,
                                          struct pan_pool *bin_pool);

void
GENX(panfrost_cleanup_indirect_draw_shaders)(struct panfrost_device *dev);

#endif
