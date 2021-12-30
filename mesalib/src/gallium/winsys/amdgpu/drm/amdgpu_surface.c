/*
 * Copyright © 2011 Red Hat All Rights Reserved.
 * Copyright © 2014 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

#include "amdgpu_winsys.h"
#include "util/format/u_format.h"

static int amdgpu_surface_sanity(const struct pipe_resource *tex)
{
   switch (tex->target) {
   case PIPE_TEXTURE_1D:
      if (tex->height0 > 1)
         return -EINVAL;
      FALLTHROUGH;
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_RECT:
      if (tex->depth0 > 1 || tex->array_size > 1)
         return -EINVAL;
      break;
   case PIPE_TEXTURE_3D:
      if (tex->array_size > 1)
         return -EINVAL;
      break;
   case PIPE_TEXTURE_1D_ARRAY:
      if (tex->height0 > 1)
         return -EINVAL;
      FALLTHROUGH;
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_CUBE_ARRAY:
      if (tex->depth0 > 1)
         return -EINVAL;
      break;
   default:
      return -EINVAL;
   }
   return 0;
}

static int amdgpu_surface_init(struct radeon_winsys *rws,
                               const struct pipe_resource *tex,
                               uint64_t flags, unsigned bpe,
                               enum radeon_surf_mode mode,
                               struct radeon_surf *surf)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);
   int r;

   r = amdgpu_surface_sanity(tex);
   if (r)
      return r;

   surf->blk_w = util_format_get_blockwidth(tex->format);
   surf->blk_h = util_format_get_blockheight(tex->format);
   surf->bpe = bpe;
   surf->flags = flags;

   struct ac_surf_config config;

   config.info.width = tex->width0;
   config.info.height = tex->height0;
   config.info.depth = tex->depth0;
   config.info.array_size = tex->array_size;
   config.info.samples = tex->nr_samples;
   config.info.storage_samples = tex->nr_storage_samples;
   config.info.levels = tex->last_level + 1;
   config.info.num_channels = util_format_get_nr_components(tex->format);
   config.is_1d = tex->target == PIPE_TEXTURE_1D ||
                  tex->target == PIPE_TEXTURE_1D_ARRAY;
   config.is_3d = tex->target == PIPE_TEXTURE_3D;
   config.is_cube = tex->target == PIPE_TEXTURE_CUBE;

   /* Use different surface counters for color and FMASK, so that MSAA MRTs
    * always use consecutive surface indices when FMASK is allocated between
    * them.
    */
   config.info.surf_index = &ws->surf_index_color;
   config.info.fmask_surf_index = &ws->surf_index_fmask;

   if (flags & RADEON_SURF_Z_OR_SBUFFER)
      config.info.surf_index = NULL;

   return ac_compute_surface(ws->addrlib, &ws->info, &config, mode, surf);
}

void amdgpu_surface_init_functions(struct amdgpu_screen_winsys *ws)
{
   ws->base.surface_init = amdgpu_surface_init;
}
