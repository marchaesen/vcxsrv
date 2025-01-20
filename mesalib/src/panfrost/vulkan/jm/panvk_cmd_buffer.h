/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_BUFFER_H
#define PANVK_CMD_BUFFER_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "vulkan/runtime/vk_command_buffer.h"

#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_dispatch.h"
#include "panvk_cmd_draw.h"
#include "panvk_cmd_push_constant.h"
#include "panvk_descriptor_set.h"
#include "panvk_descriptor_set_layout.h"
#include "panvk_device.h"
#include "panvk_macros.h"
#include "panvk_mempool.h"
#include "panvk_shader.h"

#include "pan_jc.h"

#include "util/list.h"

#include "genxml/gen_macros.h"

struct panvk_batch {
   struct list_head node;
   struct util_dynarray jobs;
   struct util_dynarray event_ops;
   struct pan_jc vtc_jc;
   struct pan_jc frag_jc;
   struct {
      struct panfrost_ptr desc;
      uint32_t desc_stride;
      uint32_t bo_count;

      /* One slot per color, two more slots for the depth/stencil buffers. */
      struct pan_kmod_bo *bos[MAX_RTS + 2];
      uint32_t layer_count;
   } fb;
   struct {
      struct pan_kmod_bo *src, *dst;
   } blit;
   struct panfrost_ptr tls;
   struct {
      struct pan_tiler_context ctx;
      struct panfrost_ptr heap_desc;
      struct panfrost_ptr ctx_descs;
      struct mali_tiler_heap_packed heap_templ;
      struct mali_tiler_context_packed ctx_templ;
   } tiler;
   struct pan_tls_info tlsinfo;
   unsigned wls_total_size;
   bool issued;
};

enum panvk_cmd_event_op_type {
   PANVK_EVENT_OP_SET,
   PANVK_EVENT_OP_RESET,
   PANVK_EVENT_OP_WAIT,
};

struct panvk_cmd_event_op {
   enum panvk_cmd_event_op_type type;
   struct panvk_event *event;
};

struct panvk_cmd_buffer {
   struct vk_command_buffer vk;

   struct panvk_pool desc_pool;
   struct panvk_pool varying_pool;
   struct panvk_pool tls_pool;
   struct list_head batches;
   struct list_head push_sets;
   struct panvk_batch *cur_batch;

   struct {
      struct panvk_cmd_graphics_state gfx;
      struct panvk_cmd_compute_state compute;
      struct panvk_push_constant_state push_constants;
   } state;
};

VK_DEFINE_HANDLE_CASTS(panvk_cmd_buffer, vk.base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)

#define panvk_cmd_buffer_obj_list_init(cmdbuf, list_name)                      \
   list_inithead(&(cmdbuf)->list_name)

#define panvk_cmd_buffer_obj_list_cleanup(cmdbuf, list_name)                   \
   do {                                                                        \
      struct panvk_cmd_pool *__pool =                                          \
         container_of(cmdbuf->vk.pool, struct panvk_cmd_pool, vk);             \
      list_splicetail(&(cmdbuf)->list_name, &__pool->list_name);               \
   } while (0)

#define panvk_cmd_buffer_obj_list_reset(cmdbuf, list_name)                     \
   do {                                                                        \
      struct panvk_cmd_pool *__pool =                                          \
         container_of(cmdbuf->vk.pool, struct panvk_cmd_pool, vk);             \
      list_splicetail(&(cmdbuf)->list_name, &__pool->list_name);               \
      list_inithead(&(cmdbuf)->list_name);                                     \
   } while (0)

static inline struct panvk_descriptor_state *
panvk_cmd_get_desc_state(struct panvk_cmd_buffer *cmdbuf,
                         VkPipelineBindPoint bindpoint)
{
   switch (bindpoint) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      return &cmdbuf->state.gfx.desc_state;

   case VK_PIPELINE_BIND_POINT_COMPUTE:
      return &cmdbuf->state.compute.desc_state;

   default:
      assert(!"Unsupported bind point");
      return NULL;
   }
}

extern const struct vk_command_buffer_ops panvk_per_arch(cmd_buffer_ops);

struct panvk_batch *
   panvk_per_arch(cmd_open_batch)(struct panvk_cmd_buffer *cmdbuf);

void panvk_per_arch(cmd_close_batch)(struct panvk_cmd_buffer *cmdbuf);

VkResult panvk_per_arch(cmd_alloc_fb_desc)(struct panvk_cmd_buffer *cmdbuf);

VkResult panvk_per_arch(cmd_alloc_tls_desc)(struct panvk_cmd_buffer *cmdbuf,
                                            bool gfx);

VkResult
   panvk_per_arch(cmd_prepare_tiler_context)(struct panvk_cmd_buffer *cmdbuf,
                                             uint32_t layer_idx);

void panvk_per_arch(cmd_preload_fb_after_batch_split)(
   struct panvk_cmd_buffer *cmdbuf);

void panvk_per_arch(cmd_bind_shaders)(struct vk_command_buffer *vk_cmd,
                                      uint32_t stage_count,
                                      const gl_shader_stage *stages,
                                      struct vk_shader **const shaders);

#endif
