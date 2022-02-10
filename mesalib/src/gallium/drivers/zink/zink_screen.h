/*
 * Copyright 2018 Collabora Ltd.
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

#ifndef ZINK_SCREEN_H
#define ZINK_SCREEN_H

#include "zink_device_info.h"
#include "zink_instance.h"
#include "vk_dispatch_table.h"

#include "util/u_idalloc.h"
#include "pipe/p_screen.h"
#include "util/slab.h"
#include "compiler/nir/nir.h"
#include "util/disk_cache.h"
#include "util/log.h"
#include "util/simple_mtx.h"
#include "util/u_queue.h"
#include "util/u_live_shader_cache.h"
#include "util/u_vertex_state_cache.h"
#include "pipebuffer/pb_cache.h"
#include "pipebuffer/pb_slab.h"
#include <vulkan/vulkan.h>

extern uint32_t zink_debug;
struct hash_table;

struct zink_batch_state;
struct zink_context;
struct zink_descriptor_layout_key;
struct zink_program;
struct zink_shader;
enum zink_descriptor_type;

/* this is the spec minimum */
#define ZINK_SPARSE_BUFFER_PAGE_SIZE (64 * 1024)

#define ZINK_DEBUG_NIR 0x1
#define ZINK_DEBUG_SPIRV 0x2
#define ZINK_DEBUG_TGSI 0x4
#define ZINK_DEBUG_VALIDATION 0x8

#define NUM_SLAB_ALLOCATORS 3
#define MIN_SLAB_ORDER 8

enum zink_descriptor_mode {
   ZINK_DESCRIPTOR_MODE_AUTO,
   ZINK_DESCRIPTOR_MODE_LAZY,
   ZINK_DESCRIPTOR_MODE_NOFALLBACK,
   ZINK_DESCRIPTOR_MODE_NOTEMPLATES,
};

struct zink_modifier_prop {
    uint32_t                             drmFormatModifierCount;
    VkDrmFormatModifierPropertiesEXT*    pDrmFormatModifierProperties;
};

struct zink_screen {
   struct pipe_screen base;
   bool threaded;
   bool is_cpu;
   uint32_t curr_batch; //the current batch id
   uint32_t last_finished; //this is racy but ultimately doesn't matter
   VkSemaphore sem;
   VkSemaphore prev_sem;
   struct util_queue flush_queue;

   unsigned buffer_rebind_counter;

   bool device_lost;
   struct sw_winsys *winsys;
   int drm_fd;

   struct hash_table framebuffer_cache;
   simple_mtx_t framebuffer_mtx;

   struct slab_parent_pool transfer_pool;
   struct disk_cache *disk_cache;
   struct util_queue cache_put_thread;
   struct util_queue cache_get_thread;

   struct util_live_shader_cache shaders;

   struct {
      struct pb_cache bo_cache;
      struct pb_slabs bo_slabs[NUM_SLAB_ALLOCATORS];
      unsigned min_alloc_size;
      struct hash_table *bo_export_table;
      simple_mtx_t bo_export_table_lock;
      uint32_t next_bo_unique_id;
   } pb;
   uint8_t heap_map[VK_MAX_MEMORY_TYPES];
   bool resizable_bar;

   uint64_t total_video_mem;
   uint64_t clamp_video_mem;
   uint64_t total_mem;

   VkInstance instance;
   struct zink_instance_info instance_info;

   VkPhysicalDevice pdev;
   uint32_t vk_version, spirv_version;
   struct util_idalloc_mt buffer_ids;
   struct util_vertex_state_cache vertex_state_cache;

   struct zink_device_info info;
   struct nir_shader_compiler_options nir_options;

   bool have_X8_D24_UNORM_PACK32;
   bool have_D24_UNORM_S8_UINT;
   bool have_triangle_fans;
   bool need_2D_zs;
   bool faked_e5sparse; //drivers may not expose R9G9B9E5 but cts requires it

   uint32_t gfx_queue;
   uint32_t max_queues;
   uint32_t timestamp_valid_bits;
   unsigned max_fences;
   VkDevice dev;
   VkQueue queue; //gfx+compute
   VkQueue thread_queue; //gfx+compute
   simple_mtx_t queue_lock;
   VkDebugUtilsMessengerEXT debugUtilsCallbackHandle;

   uint32_t cur_custom_border_color_samplers;

   bool needs_mesa_wsi;
   bool needs_mesa_flush_wsi;

   struct vk_dispatch_table vk;

   bool (*descriptor_program_init)(struct zink_context *ctx, struct zink_program *pg);
   void (*descriptor_program_deinit)(struct zink_context *ctx, struct zink_program *pg);
   void (*descriptors_update)(struct zink_context *ctx, bool is_compute);
   void (*context_update_descriptor_states)(struct zink_context *ctx, bool is_compute);
   void (*context_invalidate_descriptor_state)(struct zink_context *ctx, enum pipe_shader_type shader,
                                               enum zink_descriptor_type type,
                                               unsigned start, unsigned count);
   bool (*batch_descriptor_init)(struct zink_screen *screen, struct zink_batch_state *bs);
   void (*batch_descriptor_reset)(struct zink_screen *screen, struct zink_batch_state *bs);
   void (*batch_descriptor_deinit)(struct zink_screen *screen, struct zink_batch_state *bs);
   bool (*descriptors_init)(struct zink_context *ctx);
   void (*descriptors_deinit)(struct zink_context *ctx);
   enum zink_descriptor_mode descriptor_mode;

   struct {
      bool dual_color_blend_by_location;
      bool inline_uniforms;
   } driconf;

   VkFormatProperties format_props[PIPE_FORMAT_COUNT];
   struct zink_modifier_prop modifier_props[PIPE_FORMAT_COUNT];
   struct {
      uint32_t image_view;
      uint32_t buffer_view;
   } null_descriptor_hashes;

   VkExtent2D maxSampleLocationGridSize[5];
};


/* update last_finished to account for batch_id wrapping */
static inline void
zink_screen_update_last_finished(struct zink_screen *screen, uint32_t batch_id)
{
   /* last_finished may have wrapped */
   if (screen->last_finished < UINT_MAX / 2) {
      /* last_finished has wrapped, batch_id has not */
      if (batch_id > UINT_MAX / 2)
         return;
   } else if (batch_id < UINT_MAX / 2) {
      /* batch_id has wrapped, last_finished has not */
      screen->last_finished = batch_id;
      return;
   }
   /* neither have wrapped */
   screen->last_finished = MAX2(batch_id, screen->last_finished);
}

/* check a batch_id against last_finished while accounting for wrapping */
static inline bool
zink_screen_check_last_finished(struct zink_screen *screen, uint32_t batch_id)
{
   /* last_finished may have wrapped */
   if (screen->last_finished < UINT_MAX / 2) {
      /* last_finished has wrapped, batch_id has not */
      if (batch_id > UINT_MAX / 2)
         return true;
   } else if (batch_id < UINT_MAX / 2) {
      /* batch_id has wrapped, last_finished has not */
      return false;
   }
   return screen->last_finished >= batch_id;
}

bool
zink_screen_init_semaphore(struct zink_screen *screen);

static inline bool
zink_screen_handle_vkresult(struct zink_screen *screen, VkResult ret)
{
   bool success = false;
   switch (ret) {
   case VK_SUCCESS:
      success = true;
      break;
   case VK_ERROR_DEVICE_LOST:
      screen->device_lost = true;
      mesa_loge("zink: DEVICE LOST!\n");
      FALLTHROUGH;
   default:
      success = false;
      break;
   }
   return success;
}

static inline struct zink_screen *
zink_screen(struct pipe_screen *pipe)
{
   return (struct zink_screen *)pipe;
}


struct mem_cache_entry {
   VkDeviceMemory mem;
   void *map;
};

#define VKCTX(fn) zink_screen(ctx->base.screen)->vk.fn
#define VKSCR(fn) screen->vk.fn

VkFormat
zink_get_format(struct zink_screen *screen, enum pipe_format format);

bool
zink_screen_batch_id_wait(struct zink_screen *screen, uint32_t batch_id, uint64_t timeout);

bool
zink_screen_timeline_wait(struct zink_screen *screen, uint32_t batch_id, uint64_t timeout);

bool
zink_is_depth_format_supported(struct zink_screen *screen, VkFormat format);

#define GET_PROC_ADDR_INSTANCE_LOCAL(instance, x) PFN_vk##x vk_##x = (PFN_vk##x)vkGetInstanceProcAddr(instance, "vk"#x)

void
zink_screen_update_pipeline_cache(struct zink_screen *screen, struct zink_program *pg);

void
zink_screen_get_pipeline_cache(struct zink_screen *screen, struct zink_program *pg);

void
zink_screen_init_descriptor_funcs(struct zink_screen *screen, bool fallback);

void
zink_stub_function_not_loaded(void);

#define warn_missing_feature(feat) \
   do { \
      static bool warned = false; \
      if (!warned) { \
         fprintf(stderr, "WARNING: Incorrect rendering will happen, " \
                         "because the Vulkan device doesn't support " \
                         "the %s feature\n", feat); \
         warned = true; \
      } \
   } while (0)

#endif
