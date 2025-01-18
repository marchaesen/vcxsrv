/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 *    Jonathan Marek <jonathan@marek.ca>
 */

#include "fd4_resource.h"

uint32_t
fd4_setup_slices(struct fd_resource *rsc)
{
   struct pipe_resource *prsc = &rsc->b.b;
   enum pipe_format format = prsc->format;
   uint32_t level, size = 0;
   uint32_t width = prsc->width0;
   uint32_t height = prsc->height0;
   uint32_t depth = prsc->depth0;
   /* in layer_first layout, the level (slice) contains just one
    * layer (since in fact the layer contains the slices)
    */
   uint32_t layers_in_level, alignment;

   if (prsc->target == PIPE_TEXTURE_3D) {
      rsc->layout.layer_first = false;
      layers_in_level = prsc->array_size;
      alignment = 4096;
   } else {
      rsc->layout.layer_first = true;
      layers_in_level = 1;
      alignment = 1;
   }

   /* 32 pixel alignment */
   fdl_set_pitchalign(&rsc->layout, fdl_cpp_shift(&rsc->layout) + 5);

   for (level = 0; level <= prsc->last_level; level++) {
      struct fdl_slice *slice = fd_resource_slice(rsc, level);
      uint32_t pitch = fdl_pitch(&rsc->layout, level);
      uint32_t nblocksy = util_format_get_nblocksy(format, height);

      slice->offset = size;

      /* 3d textures can have different layer sizes for high levels, but the
       * hw auto-sizer is buggy (or at least different than what this code
       * does), so as soon as the layer size range gets into range, we stop
       * reducing it.
       */
      if (prsc->target == PIPE_TEXTURE_3D &&
          (level > 1 && fd_resource_slice(rsc, level - 1)->size0 <= 0xf000))
         slice->size0 = fd_resource_slice(rsc, level - 1)->size0;
      else
         slice->size0 = align(nblocksy * pitch, alignment);

      size += slice->size0 * depth * layers_in_level;

      width = u_minify(width, 1);
      height = u_minify(height, 1);
      depth = u_minify(depth, 1);
   }

   return size;
}
