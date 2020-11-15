/*
 * Copyright © 2015 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "tu_private.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/u_math.h"
#include "vk_enum_to_str.h"

void PRINTFLIKE(3, 4)
   __tu_finishme(const char *file, int line, const char *format, ...)
{
   va_list ap;
   char buffer[256];

   va_start(ap, format);
   vsnprintf(buffer, sizeof(buffer), format, ap);
   va_end(ap);

   mesa_loge("%s:%d: FINISHME: %s\n", file, line, buffer);
}

VkResult
__vk_errorf(struct tu_instance *instance,
            VkResult error,
            bool always_print,
            const char *file,
            int line,
            const char *format,
            ...)
{
   va_list ap;
   char buffer[256];

   const char *error_str = vk_Result_to_str(error);

#ifndef DEBUG
   if (!always_print)
      return error;
#endif

   if (format) {
      va_start(ap, format);
      vsnprintf(buffer, sizeof(buffer), format, ap);
      va_end(ap);

      mesa_loge("%s:%d: %s (%s)\n", file, line, buffer, error_str);
   } else {
      mesa_loge("%s:%d: %s\n", file, line, error_str);
   }

   return error;
}

static void
tu_tiling_config_update_tile_layout(struct tu_framebuffer *fb,
                                    const struct tu_device *dev,
                                    const struct tu_render_pass *pass)
{
   const uint32_t tile_align_w = pass->tile_align_w;
   const uint32_t tile_align_h = dev->physical_device->info.tile_align_h;
   const uint32_t max_tile_width = 1024;

   /* start from 1 tile */
   fb->tile_count = (VkExtent2D) {
      .width = 1,
      .height = 1,
   };
   fb->tile0 = (VkExtent2D) {
      .width = util_align_npot(fb->width, tile_align_w),
      .height = align(fb->height, tile_align_h),
   };

   if (unlikely(dev->physical_device->instance->debug_flags & TU_DEBUG_FORCEBIN)) {
      /* start with 2x2 tiles */
      fb->tile_count.width = 2;
      fb->tile_count.height = 2;
      fb->tile0.width = util_align_npot(DIV_ROUND_UP(fb->width, 2), tile_align_w);
      fb->tile0.height = align(DIV_ROUND_UP(fb->height, 2), tile_align_h);
   }

   /* do not exceed max tile width */
   while (fb->tile0.width > max_tile_width) {
      fb->tile_count.width++;
      fb->tile0.width =
         util_align_npot(DIV_ROUND_UP(fb->width, fb->tile_count.width), tile_align_w);
   }

   /* will force to sysmem, don't bother trying to have a valid tile config
    * TODO: just skip all GMEM stuff when sysmem is forced?
    */
   if (!pass->gmem_pixels)
      return;

   /* do not exceed gmem size */
   while (fb->tile0.width * fb->tile0.height > pass->gmem_pixels) {
      if (fb->tile0.width > MAX2(tile_align_w, fb->tile0.height)) {
         fb->tile_count.width++;
         fb->tile0.width =
            util_align_npot(DIV_ROUND_UP(fb->width, fb->tile_count.width), tile_align_w);
      } else {
         /* if this assert fails then layout is impossible.. */
         assert(fb->tile0.height > tile_align_h);
         fb->tile_count.height++;
         fb->tile0.height =
            align(DIV_ROUND_UP(fb->height, fb->tile_count.height), tile_align_h);
      }
   }
}

static void
tu_tiling_config_update_pipe_layout(struct tu_framebuffer *fb,
                                    const struct tu_device *dev)
{
   const uint32_t max_pipe_count = 32; /* A6xx */

   /* start from 1 tile per pipe */
   fb->pipe0 = (VkExtent2D) {
      .width = 1,
      .height = 1,
   };
   fb->pipe_count = fb->tile_count;

   while (fb->pipe_count.width * fb->pipe_count.height > max_pipe_count) {
      if (fb->pipe0.width < fb->pipe0.height) {
         fb->pipe0.width += 1;
         fb->pipe_count.width =
            DIV_ROUND_UP(fb->tile_count.width, fb->pipe0.width);
      } else {
         fb->pipe0.height += 1;
         fb->pipe_count.height =
            DIV_ROUND_UP(fb->tile_count.height, fb->pipe0.height);
      }
   }
}

static void
tu_tiling_config_update_pipes(struct tu_framebuffer *fb,
                              const struct tu_device *dev)
{
   const uint32_t max_pipe_count = 32; /* A6xx */
   const uint32_t used_pipe_count =
      fb->pipe_count.width * fb->pipe_count.height;
   const VkExtent2D last_pipe = {
      .width = (fb->tile_count.width - 1) % fb->pipe0.width + 1,
      .height = (fb->tile_count.height - 1) % fb->pipe0.height + 1,
   };

   assert(used_pipe_count <= max_pipe_count);
   assert(max_pipe_count <= ARRAY_SIZE(fb->pipe_config));

   for (uint32_t y = 0; y < fb->pipe_count.height; y++) {
      for (uint32_t x = 0; x < fb->pipe_count.width; x++) {
         const uint32_t pipe_x = fb->pipe0.width * x;
         const uint32_t pipe_y = fb->pipe0.height * y;
         const uint32_t pipe_w = (x == fb->pipe_count.width - 1)
                                    ? last_pipe.width
                                    : fb->pipe0.width;
         const uint32_t pipe_h = (y == fb->pipe_count.height - 1)
                                    ? last_pipe.height
                                    : fb->pipe0.height;
         const uint32_t n = fb->pipe_count.width * y + x;

         fb->pipe_config[n] = A6XX_VSC_PIPE_CONFIG_REG_X(pipe_x) |
                                  A6XX_VSC_PIPE_CONFIG_REG_Y(pipe_y) |
                                  A6XX_VSC_PIPE_CONFIG_REG_W(pipe_w) |
                                  A6XX_VSC_PIPE_CONFIG_REG_H(pipe_h);
         fb->pipe_sizes[n] = CP_SET_BIN_DATA5_0_VSC_SIZE(pipe_w * pipe_h);
      }
   }

   memset(fb->pipe_config + used_pipe_count, 0,
          sizeof(uint32_t) * (max_pipe_count - used_pipe_count));
}

void
tu_framebuffer_tiling_config(struct tu_framebuffer *fb,
                             const struct tu_device *device,
                             const struct tu_render_pass *pass)
{
   tu_tiling_config_update_tile_layout(fb, device, pass);
   tu_tiling_config_update_pipe_layout(fb, device);
   tu_tiling_config_update_pipes(fb, device);
}
