/*
 * Copyright © 2018 Jonathan Marek <jonathan@marek.ca>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Jonathan Marek <jonathan@marek.ca>
 */

#include "fd2_resource.h"

uint32_t
fd2_setup_slices(struct fd_resource *rsc)
{
   struct pipe_resource *prsc = &rsc->b.b;
   enum pipe_format format = prsc->format;
   uint32_t height0 = util_format_get_nblocksy(format, prsc->height0);
   uint32_t level, size = 0;

   /* 32 pixel alignment */
   fdl_set_pitchalign(&rsc->layout, fdl_cpp_shift(&rsc->layout) + 5);

   for (level = 0; level <= prsc->last_level; level++) {
      struct fdl_slice *slice = fd_resource_slice(rsc, level);
      uint32_t pitch = fdl2_pitch(&rsc->layout, level);
      uint32_t nblocksy = align(u_minify(height0, level), 32);

      /* mipmaps have power of two sizes in memory */
      if (level)
         nblocksy = util_next_power_of_two(nblocksy);

      slice->offset = size;
      slice->size0 = align(pitch * nblocksy, 4096);

      size += slice->size0 * u_minify(prsc->depth0, level) * prsc->array_size;
   }

   return size;
}

unsigned
fd2_tile_mode(const struct pipe_resource *tmpl)
{
   /* disable tiling for cube maps, freedreno uses a 2D array for the staging
    * texture, (a2xx supports 2D arrays but it is not implemented)
    */
   if (tmpl->target == PIPE_TEXTURE_CUBE)
      return 0;
   /* we can enable tiling for any resource we can render to */
   return (tmpl->bind & PIPE_BIND_RENDER_TARGET) ? 1 : 0;
}
