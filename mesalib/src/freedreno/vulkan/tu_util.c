/*
 * Copyright © 2015 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "tu_util.h"

#include <errno.h>
#include <stdarg.h>

#include "util/u_math.h"
#include "util/timespec.h"
#include "vk_enum_to_str.h"

#include "tu_device.h"
#include "tu_pass.h"

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
__vk_startup_errorf(struct tu_instance *instance,
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
                                    const struct tu_render_pass *pass,
                                    enum tu_gmem_layout gmem_layout)
{
   const uint32_t tile_align_w = pass->tile_align_w;
   const uint32_t tile_align_h = dev->physical_device->info->tile_align_h;
   const uint32_t max_tile_width = dev->physical_device->info->tile_max_w;
   const uint32_t max_tile_height = dev->physical_device->info->tile_max_h;
   struct tu_tiling_config *tiling = &fb->tiling[gmem_layout];

   /* start from 1 tile */
   tiling->tile_count = (VkExtent2D) {
      .width = 1,
      .height = 1,
   };
   tiling->tile0 = (VkExtent2D) {
      .width = util_align_npot(fb->width, tile_align_w),
      .height = align(fb->height, tile_align_h),
   };

   /* will force to sysmem, don't bother trying to have a valid tile config
    * TODO: just skip all GMEM stuff when sysmem is forced?
    */
   if (!pass->gmem_pixels[gmem_layout])
      return;

   if (unlikely(dev->physical_device->instance->debug_flags & TU_DEBUG_FORCEBIN)) {
      /* start with 2x2 tiles */
      tiling->tile_count.width = 2;
      tiling->tile_count.height = 2;
      tiling->tile0.width = util_align_npot(DIV_ROUND_UP(fb->width, 2), tile_align_w);
      tiling->tile0.height = align(DIV_ROUND_UP(fb->height, 2), tile_align_h);
   }

   /* do not exceed max tile width */
   while (tiling->tile0.width > max_tile_width) {
      tiling->tile_count.width++;
      tiling->tile0.width =
         util_align_npot(DIV_ROUND_UP(fb->width, tiling->tile_count.width), tile_align_w);
   }

   /* do not exceed max tile height */
   while (tiling->tile0.height > max_tile_height) {
      tiling->tile_count.height++;
      tiling->tile0.height =
         util_align_npot(DIV_ROUND_UP(fb->height, tiling->tile_count.height), tile_align_h);
   }

   /* do not exceed gmem size */
   while (tiling->tile0.width * tiling->tile0.height > pass->gmem_pixels[gmem_layout]) {
      if (tiling->tile0.width > MAX2(tile_align_w, tiling->tile0.height)) {
         tiling->tile_count.width++;
         tiling->tile0.width =
            util_align_npot(DIV_ROUND_UP(fb->width, tiling->tile_count.width), tile_align_w);
      } else {
         /* if this assert fails then layout is impossible.. */
         assert(tiling->tile0.height > tile_align_h);
         tiling->tile_count.height++;
         tiling->tile0.height =
            align(DIV_ROUND_UP(fb->height, tiling->tile_count.height), tile_align_h);
      }
   }
}

static void
tu_tiling_config_update_pipe_layout(struct tu_tiling_config *tiling,
                                    const struct tu_device *dev)
{
   const uint32_t max_pipe_count = 32; /* A6xx */

   /* start from 1 tile per pipe */
   tiling->pipe0 = (VkExtent2D) {
      .width = 1,
      .height = 1,
   };
   tiling->pipe_count = tiling->tile_count;

   while (tiling->pipe_count.width * tiling->pipe_count.height > max_pipe_count) {
      if (tiling->pipe0.width < tiling->pipe0.height) {
         tiling->pipe0.width += 1;
         tiling->pipe_count.width =
            DIV_ROUND_UP(tiling->tile_count.width, tiling->pipe0.width);
      } else {
         tiling->pipe0.height += 1;
         tiling->pipe_count.height =
            DIV_ROUND_UP(tiling->tile_count.height, tiling->pipe0.height);
      }
   }
}

static void
tu_tiling_config_update_pipes(struct tu_tiling_config *tiling,
                              const struct tu_device *dev)
{
   const uint32_t max_pipe_count = 32; /* A6xx */
   const uint32_t used_pipe_count =
      tiling->pipe_count.width * tiling->pipe_count.height;
   const VkExtent2D last_pipe = {
      .width = (tiling->tile_count.width - 1) % tiling->pipe0.width + 1,
      .height = (tiling->tile_count.height - 1) % tiling->pipe0.height + 1,
   };

   assert(used_pipe_count <= max_pipe_count);
   assert(max_pipe_count <= ARRAY_SIZE(tiling->pipe_config));

   for (uint32_t y = 0; y < tiling->pipe_count.height; y++) {
      for (uint32_t x = 0; x < tiling->pipe_count.width; x++) {
         const uint32_t pipe_x = tiling->pipe0.width * x;
         const uint32_t pipe_y = tiling->pipe0.height * y;
         const uint32_t pipe_w = (x == tiling->pipe_count.width - 1)
                                    ? last_pipe.width
                                    : tiling->pipe0.width;
         const uint32_t pipe_h = (y == tiling->pipe_count.height - 1)
                                    ? last_pipe.height
                                    : tiling->pipe0.height;
         const uint32_t n = tiling->pipe_count.width * y + x;

         tiling->pipe_config[n] = A6XX_VSC_PIPE_CONFIG_REG_X(pipe_x) |
                                  A6XX_VSC_PIPE_CONFIG_REG_Y(pipe_y) |
                                  A6XX_VSC_PIPE_CONFIG_REG_W(pipe_w) |
                                  A6XX_VSC_PIPE_CONFIG_REG_H(pipe_h);
         tiling->pipe_sizes[n] = CP_SET_BIN_DATA5_0_VSC_SIZE(pipe_w * pipe_h);
      }
   }

   memset(tiling->pipe_config + used_pipe_count, 0,
          sizeof(uint32_t) * (max_pipe_count - used_pipe_count));
}

static bool
is_hw_binning_possible(const struct tu_tiling_config *tiling)
{
   /* Similar to older gens, # of tiles per pipe cannot be more than 32.
    * But there are no hangs with 16 or more tiles per pipe in either
    * X or Y direction, so that limit does not seem to apply.
    */
   uint32_t tiles_per_pipe = tiling->pipe0.width * tiling->pipe0.height;
   return tiles_per_pipe <= 32;
}

static void
tu_tiling_config_update_binning(struct tu_tiling_config *tiling, const struct tu_device *device)
{
   tiling->binning_possible = is_hw_binning_possible(tiling);

   if (tiling->binning_possible) {
      tiling->binning = (tiling->tile_count.width * tiling->tile_count.height) > 2;

      if (unlikely(device->physical_device->instance->debug_flags & TU_DEBUG_FORCEBIN))
         tiling->binning = true;
      if (unlikely(device->physical_device->instance->debug_flags &
                   TU_DEBUG_NOBIN))
         tiling->binning = false;
   } else {
      tiling->binning = false;
   }
}

void
tu_framebuffer_tiling_config(struct tu_framebuffer *fb,
                             const struct tu_device *device,
                             const struct tu_render_pass *pass)
{
   for (int gmem_layout = 0; gmem_layout < TU_GMEM_LAYOUT_COUNT; gmem_layout++) {
      struct tu_tiling_config *tiling = &fb->tiling[gmem_layout];
      tu_tiling_config_update_tile_layout(fb, device, pass, gmem_layout);
      tu_tiling_config_update_pipe_layout(tiling, device);
      tu_tiling_config_update_pipes(tiling, device);
      tu_tiling_config_update_binning(tiling, device);
   }
}

void
tu_dbg_log_gmem_load_store_skips(struct tu_device *device)
{
   static uint32_t last_skipped_loads = 0;
   static uint32_t last_skipped_stores = 0;
   static uint32_t last_total_loads = 0;
   static uint32_t last_total_stores = 0;
   static struct timespec last_time = {};

   pthread_mutex_lock(&device->submit_mutex);

   struct timespec current_time;
   clock_gettime(CLOCK_MONOTONIC, &current_time);

   if (timespec_sub_to_nsec(&current_time, &last_time) > 1000 * 1000 * 1000) {
      last_time = current_time;
   } else {
      pthread_mutex_unlock(&device->submit_mutex);
      return;
   }

   struct tu6_global *global = device->global_bo->map;

   uint32_t current_taken_loads = global->dbg_gmem_taken_loads;
   uint32_t current_taken_stores = global->dbg_gmem_taken_stores;
   uint32_t current_total_loads = global->dbg_gmem_total_loads;
   uint32_t current_total_stores = global->dbg_gmem_total_stores;

   uint32_t skipped_loads = current_total_loads - current_taken_loads;
   uint32_t skipped_stores = current_total_stores - current_taken_stores;

   uint32_t current_time_frame_skipped_loads = skipped_loads - last_skipped_loads;
   uint32_t current_time_frame_skipped_stores = skipped_stores - last_skipped_stores;

   uint32_t current_time_frame_total_loads = current_total_loads - last_total_loads;
   uint32_t current_time_frame_total_stores = current_total_stores - last_total_stores;

   mesa_logi("[GMEM] loads total: %u skipped: %.1f%%\n",
         current_time_frame_total_loads,
         current_time_frame_skipped_loads / (float) current_time_frame_total_loads * 100.f);
   mesa_logi("[GMEM] stores total: %u skipped: %.1f%%\n",
         current_time_frame_total_stores,
         current_time_frame_skipped_stores / (float) current_time_frame_total_stores * 100.f);

   last_skipped_loads = skipped_loads;
   last_skipped_stores = skipped_stores;
   last_total_loads = current_total_loads;
   last_total_stores = current_total_stores;

   pthread_mutex_unlock(&device->submit_mutex);
}
