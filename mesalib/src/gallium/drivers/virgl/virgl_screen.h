/*
 * Copyright 2014, 2015 Red Hat.
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
#ifndef VIRGL_H
#define VIRGL_H

#include "pipe/p_screen.h"
#include "util/slab.h"
#include "virgl_winsys.h"

enum virgl_debug_flags {
   VIRGL_DEBUG_VERBOSE              = 1 << 0,
   VIRGL_DEBUG_TGSI                 = 1 << 1,
   VIRGL_DEBUG_NO_EMULATE_BGRA      = 1 << 2,
   VIRGL_DEBUG_NO_BGRA_DEST_SWIZZLE = 1 << 3,
   VIRGL_DEBUG_SYNC                 = 1 << 4,
   VIRGL_DEBUG_XFER                 = 1 << 5,
};

extern int virgl_debug;

struct virgl_screen {
   struct pipe_screen base;

   int refcnt;

   /* place for winsys to stash it's own stuff: */
   void *winsys_priv;

   struct virgl_winsys *vws;

   struct virgl_drm_caps caps;

   struct slab_parent_pool transfer_pool;

   uint32_t sub_ctx_id;
   bool tweak_gles_emulate_bgra;
   bool tweak_gles_apply_bgra_dest_swizzle;
   int32_t tweak_gles_tf3_value;
};


static inline struct virgl_screen *
virgl_screen(struct pipe_screen *pipe)
{
   return (struct virgl_screen *)pipe;
}

bool
virgl_has_readback_format(struct pipe_screen *screen, enum virgl_formats fmt);

/* GL_ARB_map_buffer_alignment requires 64 as the minimum alignment value.  In
 * addition to complying with the extension, a high enough alignment value is
 * expected by various external GL clients. For example, wined3d doesn't like
 * maps that don't have a 16 byte alignment.
 */
#define VIRGL_MAP_BUFFER_ALIGNMENT 64

#endif
