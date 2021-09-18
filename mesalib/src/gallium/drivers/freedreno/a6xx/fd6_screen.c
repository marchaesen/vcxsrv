/*
 * Copyright (C) 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
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
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "drm-uapi/drm_fourcc.h"
#include "pipe/p_screen.h"
#include "util/format/u_format.h"

#include "fd6_blitter.h"
#include "fd6_context.h"
#include "fd6_emit.h"
#include "fd6_format.h"
#include "fd6_resource.h"
#include "fd6_screen.h"

#include "ir3/ir3_compiler.h"

static bool
valid_sample_count(unsigned sample_count)
{
   switch (sample_count) {
   case 0:
   case 1:
   case 2:
   case 4:
      // TODO seems 8x works, but increases lrz width or height.. but the
      // blob I have doesn't seem to expose any egl configs w/ 8x, so
      // just hide it for now and revisit later.
      //	case 8:
      return true;
   default:
      return false;
   }
}

static bool
fd6_screen_is_format_supported(struct pipe_screen *pscreen,
                               enum pipe_format format,
                               enum pipe_texture_target target,
                               unsigned sample_count,
                               unsigned storage_sample_count, unsigned usage)
{
   unsigned retval = 0;

   if ((target >= PIPE_MAX_TEXTURE_TYPES) ||
       !valid_sample_count(sample_count)) {
      DBG("not supported: format=%s, target=%d, sample_count=%d, usage=%x",
          util_format_name(format), target, sample_count, usage);
      return false;
   }

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   if ((usage & PIPE_BIND_VERTEX_BUFFER) &&
       (fd6_pipe2vtx(format) != FMT6_NONE)) {
      retval |= PIPE_BIND_VERTEX_BUFFER;
   }

   if ((usage & (PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_SHADER_IMAGE)) &&
       (fd6_pipe2tex(format) != FMT6_NONE) &&
       (target == PIPE_BUFFER || util_format_get_blocksize(format) != 12)) {
      retval |= usage & (PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_SHADER_IMAGE);
   }

   if ((usage &
        (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET |
         PIPE_BIND_SCANOUT | PIPE_BIND_SHARED | PIPE_BIND_COMPUTE_RESOURCE)) &&
       (fd6_pipe2color(format) != FMT6_NONE) &&
       (fd6_pipe2tex(format) != FMT6_NONE)) {
      retval |= usage & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET |
                         PIPE_BIND_SCANOUT | PIPE_BIND_SHARED |
                         PIPE_BIND_COMPUTE_RESOURCE);
   }

   /* For ARB_framebuffer_no_attachments: */
   if ((usage & PIPE_BIND_RENDER_TARGET) && (format == PIPE_FORMAT_NONE)) {
      retval |= usage & PIPE_BIND_RENDER_TARGET;
   }

   if ((usage & PIPE_BIND_DEPTH_STENCIL) &&
       (fd6_pipe2depth(format) != (enum a6xx_depth_format) ~0) &&
       (fd6_pipe2tex(format) != FMT6_NONE)) {
      retval |= PIPE_BIND_DEPTH_STENCIL;
   }

   if ((usage & PIPE_BIND_INDEX_BUFFER) &&
       (fd_pipe2index(format) != (enum pc_di_index_size) ~0)) {
      retval |= PIPE_BIND_INDEX_BUFFER;
   }

   if (retval != usage) {
      DBG("not supported: format=%s, target=%d, sample_count=%d, "
          "usage=%x, retval=%x",
          util_format_name(format), target, sample_count, usage, retval);
   }

   return retval == usage;
}

/* clang-format off */
static const uint8_t primtypes[] = {
   [PIPE_PRIM_POINTS]                      = DI_PT_POINTLIST,
   [PIPE_PRIM_LINES]                       = DI_PT_LINELIST,
   [PIPE_PRIM_LINE_STRIP]                  = DI_PT_LINESTRIP,
   [PIPE_PRIM_LINE_LOOP]                   = DI_PT_LINELOOP,
   [PIPE_PRIM_TRIANGLES]                   = DI_PT_TRILIST,
   [PIPE_PRIM_TRIANGLE_STRIP]              = DI_PT_TRISTRIP,
   [PIPE_PRIM_TRIANGLE_FAN]                = DI_PT_TRIFAN,
   [PIPE_PRIM_LINES_ADJACENCY]             = DI_PT_LINE_ADJ,
   [PIPE_PRIM_LINE_STRIP_ADJACENCY]        = DI_PT_LINESTRIP_ADJ,
   [PIPE_PRIM_TRIANGLES_ADJACENCY]         = DI_PT_TRI_ADJ,
   [PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY]    = DI_PT_TRISTRIP_ADJ,
   [PIPE_PRIM_PATCHES]                     = DI_PT_PATCHES0,
   [PIPE_PRIM_MAX]                         = DI_PT_RECTLIST,  /* internal clear blits */
};
/* clang-format on */

void
fd6_screen_init(struct pipe_screen *pscreen)
{
   struct fd_screen *screen = fd_screen(pscreen);

   screen->max_rts = A6XX_MAX_RENDER_TARGETS;

   screen->ccu_offset_bypass = screen->info->num_ccu * A6XX_CCU_DEPTH_SIZE;
   screen->ccu_offset_gmem = (screen->gmemsize_bytes -
         screen->info->num_ccu * A6XX_CCU_GMEM_COLOR_SIZE);

   /* Currently only FB_READ forces GMEM path, mostly because we'd have to
    * deal with cmdstream patching otherwise..
    */
   screen->gmem_reason_mask = FD_GMEM_CLEARS_DEPTH_STENCIL |
                              FD_GMEM_DEPTH_ENABLED | FD_GMEM_STENCIL_ENABLED |
                              FD_GMEM_BLEND_ENABLED | FD_GMEM_LOGICOP_ENABLED;

   pscreen->context_create = fd6_context_create;
   pscreen->is_format_supported = fd6_screen_is_format_supported;

   screen->tile_mode = fd6_tile_mode;

   fd6_resource_screen_init(pscreen);
   fd6_emit_init_screen(pscreen);
   ir3_screen_init(pscreen);

   screen->primtypes = primtypes;
}
