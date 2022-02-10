/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <amdgpu.h>
#include <assert.h>
#include <libsync.h>
#include <pthread.h>
#include <stdlib.h>
#include "drm-uapi/amdgpu_drm.h"

#include "util/u_memory.h"
#include "ac_debug.h"
#include "radv_amdgpu_bo.h"
#include "radv_amdgpu_cs.h"
#include "radv_amdgpu_winsys.h"
#include "radv_debug.h"
#include "radv_radeon_winsys.h"
#include "sid.h"
#include "vk_alloc.h"
#include "vk_drm_syncobj.h"
#include "vk_sync.h"
#include "vk_sync_dummy.h"

#define GFX6_MAX_CS_SIZE 0xffff8 /* in dwords */

enum { VIRTUAL_BUFFER_HASH_TABLE_SIZE = 1024 };

struct radv_amdgpu_ib {
   struct radeon_winsys_bo *bo;
   unsigned cdw;
};

struct radv_amdgpu_cs {
   struct radeon_cmdbuf base;
   struct radv_amdgpu_winsys *ws;

   struct amdgpu_cs_ib_info ib;

   struct radeon_winsys_bo *ib_buffer;
   uint8_t *ib_mapped;
   unsigned max_num_buffers;
   unsigned num_buffers;
   struct drm_amdgpu_bo_list_entry *handles;

   struct radv_amdgpu_ib *old_ib_buffers;
   unsigned num_old_ib_buffers;
   unsigned max_num_old_ib_buffers;
   unsigned *ib_size_ptr;
   VkResult status;
   bool is_chained;

   int buffer_hash_table[1024];
   unsigned hw_ip;

   unsigned num_virtual_buffers;
   unsigned max_num_virtual_buffers;
   struct radeon_winsys_bo **virtual_buffers;
   int *virtual_buffer_hash_table;

   /* For chips that don't support chaining. */
   struct radeon_cmdbuf *old_cs_buffers;
   unsigned num_old_cs_buffers;
};

struct radv_winsys_sem_counts {
   uint32_t syncobj_count;
   uint32_t timeline_syncobj_count;
   uint32_t *syncobj;
   uint64_t *points;
};

struct radv_winsys_sem_info {
   bool cs_emit_signal;
   bool cs_emit_wait;
   struct radv_winsys_sem_counts wait;
   struct radv_winsys_sem_counts signal;
};

static uint32_t radv_amdgpu_ctx_queue_syncobj(struct radv_amdgpu_ctx *ctx, unsigned ip,
                                              unsigned ring);

static inline struct radv_amdgpu_cs *
radv_amdgpu_cs(struct radeon_cmdbuf *base)
{
   return (struct radv_amdgpu_cs *)base;
}

static int
ring_to_hw_ip(enum ring_type ring)
{
   switch (ring) {
   case RING_GFX:
      return AMDGPU_HW_IP_GFX;
   case RING_DMA:
      return AMDGPU_HW_IP_DMA;
   case RING_COMPUTE:
      return AMDGPU_HW_IP_COMPUTE;
   default:
      unreachable("unsupported ring");
   }
}

static enum ring_type
hw_ip_to_ring(int hw_ip)
{
   switch (hw_ip) {
   case AMDGPU_HW_IP_GFX:
      return RING_GFX;
   case AMDGPU_HW_IP_DMA:
      return RING_DMA;
   case AMDGPU_HW_IP_COMPUTE:
      return RING_COMPUTE;
   default:
      unreachable("unsupported hw ip");
   }
}

struct radv_amdgpu_cs_request {
   /** Specify HW IP block type to which to send the IB. */
   unsigned ip_type;

   /** IP instance index if there are several IPs of the same type. */
   unsigned ip_instance;

   /**
    * Specify ring index of the IP. We could have several rings
    * in the same IP. E.g. 0 for SDMA0 and 1 for SDMA1.
    */
   uint32_t ring;

   /**
    * BO list handles used by this request.
    */
   struct drm_amdgpu_bo_list_entry *handles;
   uint32_t num_handles;

   /** Number of IBs to submit in the field ibs. */
   uint32_t number_of_ibs;

   /**
    * IBs to submit. Those IBs will be submit together as single entity
    */
   struct amdgpu_cs_ib_info *ibs;

   /**
    * The returned sequence number for the command submission
    */
   uint64_t seq_no;
};

static int radv_amdgpu_cs_submit(struct radv_amdgpu_ctx *ctx,
                                 struct radv_amdgpu_cs_request *request,
                                 struct radv_winsys_sem_info *sem_info);

static void
radv_amdgpu_request_to_fence(struct radv_amdgpu_ctx *ctx, struct radv_amdgpu_fence *fence,
                             struct radv_amdgpu_cs_request *req)
{
   fence->fence.context = ctx->ctx;
   fence->fence.ip_type = req->ip_type;
   fence->fence.ip_instance = req->ip_instance;
   fence->fence.ring = req->ring;
   fence->fence.fence = req->seq_no;
}

static void
radv_amdgpu_cs_destroy(struct radeon_cmdbuf *rcs)
{
   struct radv_amdgpu_cs *cs = radv_amdgpu_cs(rcs);

   if (cs->ib_buffer)
      cs->ws->base.buffer_destroy(&cs->ws->base, cs->ib_buffer);
   else
      free(cs->base.buf);

   for (unsigned i = 0; i < cs->num_old_ib_buffers; ++i)
      cs->ws->base.buffer_destroy(&cs->ws->base, cs->old_ib_buffers[i].bo);

   for (unsigned i = 0; i < cs->num_old_cs_buffers; ++i) {
      free(cs->old_cs_buffers[i].buf);
   }

   free(cs->old_cs_buffers);
   free(cs->old_ib_buffers);
   free(cs->virtual_buffers);
   free(cs->virtual_buffer_hash_table);
   free(cs->handles);
   free(cs);
}

static void
radv_amdgpu_init_cs(struct radv_amdgpu_cs *cs, enum ring_type ring_type)
{
   for (int i = 0; i < ARRAY_SIZE(cs->buffer_hash_table); ++i)
      cs->buffer_hash_table[i] = -1;

   cs->hw_ip = ring_to_hw_ip(ring_type);
}

static enum radeon_bo_domain
radv_amdgpu_cs_domain(const struct radeon_winsys *_ws)
{
   const struct radv_amdgpu_winsys *ws = (const struct radv_amdgpu_winsys *)_ws;

   bool enough_vram = ws->info.all_vram_visible ||
                      p_atomic_read_relaxed(&ws->allocated_vram_vis) * 2 <= ws->info.vram_vis_size;
   bool use_sam =
      (enough_vram && ws->info.has_dedicated_vram && !(ws->perftest & RADV_PERFTEST_NO_SAM)) ||
      (ws->perftest & RADV_PERFTEST_SAM);
   return use_sam ? RADEON_DOMAIN_VRAM : RADEON_DOMAIN_GTT;
}

static struct radeon_cmdbuf *
radv_amdgpu_cs_create(struct radeon_winsys *ws, enum ring_type ring_type)
{
   struct radv_amdgpu_cs *cs;
   uint32_t ib_pad_dw_mask = MAX2(3, radv_amdgpu_winsys(ws)->info.ib_pad_dw_mask[ring_type]);
   uint32_t ib_size = align(20 * 1024 * 4, ib_pad_dw_mask + 1);
   cs = calloc(1, sizeof(struct radv_amdgpu_cs));
   if (!cs)
      return NULL;

   cs->ws = radv_amdgpu_winsys(ws);
   radv_amdgpu_init_cs(cs, ring_type);

   if (cs->ws->use_ib_bos) {
      VkResult result =
         ws->buffer_create(ws, ib_size, 0, radv_amdgpu_cs_domain(ws),
                           RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING |
                              RADEON_FLAG_READ_ONLY | RADEON_FLAG_GTT_WC,
                           RADV_BO_PRIORITY_CS, 0, &cs->ib_buffer);
      if (result != VK_SUCCESS) {
         free(cs);
         return NULL;
      }

      cs->ib_mapped = ws->buffer_map(cs->ib_buffer);
      if (!cs->ib_mapped) {
         ws->buffer_destroy(ws, cs->ib_buffer);
         free(cs);
         return NULL;
      }

      cs->ib.ib_mc_address = radv_amdgpu_winsys_bo(cs->ib_buffer)->base.va;
      cs->base.buf = (uint32_t *)cs->ib_mapped;
      cs->base.max_dw = ib_size / 4 - 4;
      cs->ib_size_ptr = &cs->ib.size;
      cs->ib.size = 0;

      ws->cs_add_buffer(&cs->base, cs->ib_buffer);
   } else {
      uint32_t *buf = malloc(16384);
      if (!buf) {
         free(cs);
         return NULL;
      }
      cs->base.buf = buf;
      cs->base.max_dw = 4096;
   }

   return &cs->base;
}

static bool hw_can_chain(unsigned hw_ip)
{
   return hw_ip == AMDGPU_HW_IP_GFX || hw_ip == AMDGPU_HW_IP_COMPUTE;
}

static uint32_t get_nop_packet(struct radv_amdgpu_cs *cs)
{
   switch(cs->hw_ip) {
   case AMDGPU_HW_IP_GFX:
   case AMDGPU_HW_IP_COMPUTE:
      return cs->ws->info.gfx_ib_pad_with_type2 ?  PKT2_NOP_PAD : PKT3_NOP_PAD;
   case AMDGPU_HW_IP_DMA:
      return cs->ws->info.chip_class <= GFX6 ? 0xF0000000 : SDMA_NOP_PAD;
   default:
      unreachable("Unknown ring type");
   }
}

static void
radv_amdgpu_cs_grow(struct radeon_cmdbuf *_cs, size_t min_size)
{
   struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);

   if (cs->status != VK_SUCCESS) {
      cs->base.cdw = 0;
      return;
   }

   if (!cs->ws->use_ib_bos) {
      const uint64_t limit_dws = GFX6_MAX_CS_SIZE;
      uint64_t ib_dws = MAX2(cs->base.cdw + min_size, MIN2(cs->base.max_dw * 2, limit_dws));

      /* The total ib size cannot exceed limit_dws dwords. */
      if (ib_dws > limit_dws) {
         /* The maximum size in dwords has been reached,
          * try to allocate a new one.
          */
         struct radeon_cmdbuf *old_cs_buffers =
            realloc(cs->old_cs_buffers, (cs->num_old_cs_buffers + 1) * sizeof(*cs->old_cs_buffers));
         if (!old_cs_buffers) {
            cs->status = VK_ERROR_OUT_OF_HOST_MEMORY;
            cs->base.cdw = 0;
            return;
         }
         cs->old_cs_buffers = old_cs_buffers;

         /* Store the current one for submitting it later. */
         cs->old_cs_buffers[cs->num_old_cs_buffers].cdw = cs->base.cdw;
         cs->old_cs_buffers[cs->num_old_cs_buffers].max_dw = cs->base.max_dw;
         cs->old_cs_buffers[cs->num_old_cs_buffers].buf = cs->base.buf;
         cs->num_old_cs_buffers++;

         /* Reset the cs, it will be re-allocated below. */
         cs->base.cdw = 0;
         cs->base.buf = NULL;

         /* Re-compute the number of dwords to allocate. */
         ib_dws = MAX2(cs->base.cdw + min_size, MIN2(cs->base.max_dw * 2, limit_dws));
         if (ib_dws > limit_dws) {
            fprintf(stderr, "amdgpu: Too high number of "
                            "dwords to allocate\n");
            cs->status = VK_ERROR_OUT_OF_HOST_MEMORY;
            return;
         }
      }

      uint32_t *new_buf = realloc(cs->base.buf, ib_dws * 4);
      if (new_buf) {
         cs->base.buf = new_buf;
         cs->base.max_dw = ib_dws;
      } else {
         cs->status = VK_ERROR_OUT_OF_HOST_MEMORY;
         cs->base.cdw = 0;
      }
      return;
   }

   enum ring_type ring_type = hw_ip_to_ring(cs->hw_ip);
   uint32_t ib_pad_dw_mask = MAX2(3, cs->ws->info.ib_pad_dw_mask[ring_type]);
   uint32_t nop_packet = get_nop_packet(cs);
   while (!cs->base.cdw || (cs->base.cdw & ib_pad_dw_mask) != ib_pad_dw_mask - 3)
      radeon_emit(&cs->base, nop_packet);

   *cs->ib_size_ptr |= cs->base.cdw + 4;

   if (cs->num_old_ib_buffers == cs->max_num_old_ib_buffers) {
      unsigned max_num_old_ib_buffers = MAX2(1, cs->max_num_old_ib_buffers * 2);
      struct radv_amdgpu_ib *old_ib_buffers =
         realloc(cs->old_ib_buffers, max_num_old_ib_buffers * sizeof(*old_ib_buffers));
      if (!old_ib_buffers) {
         cs->status = VK_ERROR_OUT_OF_HOST_MEMORY;
         return;
      }
      cs->max_num_old_ib_buffers = max_num_old_ib_buffers;
      cs->old_ib_buffers = old_ib_buffers;
   }

   cs->old_ib_buffers[cs->num_old_ib_buffers].bo = cs->ib_buffer;
   cs->old_ib_buffers[cs->num_old_ib_buffers++].cdw = cs->base.cdw;

   uint64_t ib_size = MAX2(min_size * 4 + 16, cs->base.max_dw * 4 * 2);

   /* max that fits in the chain size field. */
   ib_size = align(MIN2(ib_size, 0xfffff), ib_pad_dw_mask + 1);

   VkResult result =
      cs->ws->base.buffer_create(&cs->ws->base, ib_size, 0, radv_amdgpu_cs_domain(&cs->ws->base),
                                 RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING |
                                    RADEON_FLAG_READ_ONLY | RADEON_FLAG_GTT_WC,
                                 RADV_BO_PRIORITY_CS, 0, &cs->ib_buffer);

   if (result != VK_SUCCESS) {
      cs->base.cdw = 0;
      cs->status = VK_ERROR_OUT_OF_DEVICE_MEMORY;
      cs->ib_buffer = cs->old_ib_buffers[--cs->num_old_ib_buffers].bo;
   }

   cs->ib_mapped = cs->ws->base.buffer_map(cs->ib_buffer);
   if (!cs->ib_mapped) {
      cs->ws->base.buffer_destroy(&cs->ws->base, cs->ib_buffer);
      cs->base.cdw = 0;

      /* VK_ERROR_MEMORY_MAP_FAILED is not valid for vkEndCommandBuffer. */
      cs->status = VK_ERROR_OUT_OF_DEVICE_MEMORY;
      cs->ib_buffer = cs->old_ib_buffers[--cs->num_old_ib_buffers].bo;
   }

   cs->ws->base.cs_add_buffer(&cs->base, cs->ib_buffer);

   assert(hw_can_chain(cs->hw_ip)); /* TODO: Implement growing other queues if needed. */

   radeon_emit(&cs->base, PKT3(PKT3_INDIRECT_BUFFER_CIK, 2, 0));
   radeon_emit(&cs->base, radv_amdgpu_winsys_bo(cs->ib_buffer)->base.va);
   radeon_emit(&cs->base, radv_amdgpu_winsys_bo(cs->ib_buffer)->base.va >> 32);
   radeon_emit(&cs->base, S_3F2_CHAIN(1) | S_3F2_VALID(1));

   cs->ib_size_ptr = cs->base.buf + cs->base.cdw - 1;

   cs->base.buf = (uint32_t *)cs->ib_mapped;
   cs->base.cdw = 0;
   cs->base.max_dw = ib_size / 4 - 4;
}

static VkResult
radv_amdgpu_cs_finalize(struct radeon_cmdbuf *_cs)
{
   struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);

   if (cs->ws->use_ib_bos) {
      enum ring_type ring_type = hw_ip_to_ring(cs->hw_ip);
      uint32_t ib_pad_dw_mask = MAX2(3, cs->ws->info.ib_pad_dw_mask[ring_type]);
      uint32_t nop_packet = get_nop_packet(cs);

      if (hw_can_chain(cs->hw_ip)) {
         /* Ensure that with the 4 dword reservation we subtract from max_dw we always
          * have 4 nops at the end for chaining. */
         while (!cs->base.cdw || (cs->base.cdw & ib_pad_dw_mask) != ib_pad_dw_mask - 3)
            radeon_emit(&cs->base, nop_packet);

         radeon_emit(&cs->base, nop_packet);
         radeon_emit(&cs->base, nop_packet);
         radeon_emit(&cs->base, nop_packet);
         radeon_emit(&cs->base, nop_packet);
      } else {
         while (!cs->base.cdw || (cs->base.cdw & ib_pad_dw_mask))
            radeon_emit(&cs->base, nop_packet);
      }

      *cs->ib_size_ptr |= cs->base.cdw;

      cs->is_chained = false;

      assert(cs->base.cdw <= cs->base.max_dw + 4);
   }

   return cs->status;
}

static void
radv_amdgpu_cs_reset(struct radeon_cmdbuf *_cs)
{
   struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);
   cs->base.cdw = 0;
   cs->status = VK_SUCCESS;

   for (unsigned i = 0; i < cs->num_buffers; ++i) {
      unsigned hash = cs->handles[i].bo_handle & (ARRAY_SIZE(cs->buffer_hash_table) - 1);
      cs->buffer_hash_table[hash] = -1;
   }

   for (unsigned i = 0; i < cs->num_virtual_buffers; ++i) {
      unsigned hash =
         ((uintptr_t)cs->virtual_buffers[i] >> 6) & (VIRTUAL_BUFFER_HASH_TABLE_SIZE - 1);
      cs->virtual_buffer_hash_table[hash] = -1;
   }

   cs->num_buffers = 0;
   cs->num_virtual_buffers = 0;

   if (cs->ws->use_ib_bos) {
      cs->ws->base.cs_add_buffer(&cs->base, cs->ib_buffer);

      for (unsigned i = 0; i < cs->num_old_ib_buffers; ++i)
         cs->ws->base.buffer_destroy(&cs->ws->base, cs->old_ib_buffers[i].bo);

      cs->num_old_ib_buffers = 0;
      cs->ib.ib_mc_address = radv_amdgpu_winsys_bo(cs->ib_buffer)->base.va;
      cs->ib_size_ptr = &cs->ib.size;
      cs->ib.size = 0;
   } else {
      for (unsigned i = 0; i < cs->num_old_cs_buffers; ++i) {
         struct radeon_cmdbuf *rcs = &cs->old_cs_buffers[i];
         free(rcs->buf);
      }

      free(cs->old_cs_buffers);
      cs->old_cs_buffers = NULL;
      cs->num_old_cs_buffers = 0;
   }
}

static int
radv_amdgpu_cs_find_buffer(struct radv_amdgpu_cs *cs, uint32_t bo)
{
   unsigned hash = bo & (ARRAY_SIZE(cs->buffer_hash_table) - 1);
   int index = cs->buffer_hash_table[hash];

   if (index == -1)
      return -1;

   if (cs->handles[index].bo_handle == bo)
      return index;

   for (unsigned i = 0; i < cs->num_buffers; ++i) {
      if (cs->handles[i].bo_handle == bo) {
         cs->buffer_hash_table[hash] = i;
         return i;
      }
   }

   return -1;
}

static void
radv_amdgpu_cs_add_buffer_internal(struct radv_amdgpu_cs *cs, uint32_t bo, uint8_t priority)
{
   unsigned hash;
   int index = radv_amdgpu_cs_find_buffer(cs, bo);

   if (index != -1)
      return;

   if (cs->num_buffers == cs->max_num_buffers) {
      unsigned new_count = MAX2(1, cs->max_num_buffers * 2);
      struct drm_amdgpu_bo_list_entry *new_entries =
         realloc(cs->handles, new_count * sizeof(struct drm_amdgpu_bo_list_entry));
      if (new_entries) {
         cs->max_num_buffers = new_count;
         cs->handles = new_entries;
      } else {
         cs->status = VK_ERROR_OUT_OF_HOST_MEMORY;
         return;
      }
   }

   cs->handles[cs->num_buffers].bo_handle = bo;
   cs->handles[cs->num_buffers].bo_priority = priority;

   hash = bo & (ARRAY_SIZE(cs->buffer_hash_table) - 1);
   cs->buffer_hash_table[hash] = cs->num_buffers;

   ++cs->num_buffers;
}

static void
radv_amdgpu_cs_add_virtual_buffer(struct radeon_cmdbuf *_cs, struct radeon_winsys_bo *bo)
{
   struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);
   unsigned hash = ((uintptr_t)bo >> 6) & (VIRTUAL_BUFFER_HASH_TABLE_SIZE - 1);

   if (!cs->virtual_buffer_hash_table) {
      int *virtual_buffer_hash_table = malloc(VIRTUAL_BUFFER_HASH_TABLE_SIZE * sizeof(int));
      if (!virtual_buffer_hash_table) {
         cs->status = VK_ERROR_OUT_OF_HOST_MEMORY;
         return;
      }
      cs->virtual_buffer_hash_table = virtual_buffer_hash_table;

      for (int i = 0; i < VIRTUAL_BUFFER_HASH_TABLE_SIZE; ++i)
         cs->virtual_buffer_hash_table[i] = -1;
   }

   if (cs->virtual_buffer_hash_table[hash] >= 0) {
      int idx = cs->virtual_buffer_hash_table[hash];
      if (cs->virtual_buffers[idx] == bo) {
         return;
      }
      for (unsigned i = 0; i < cs->num_virtual_buffers; ++i) {
         if (cs->virtual_buffers[i] == bo) {
            cs->virtual_buffer_hash_table[hash] = i;
            return;
         }
      }
   }

   if (cs->max_num_virtual_buffers <= cs->num_virtual_buffers) {
      unsigned max_num_virtual_buffers = MAX2(2, cs->max_num_virtual_buffers * 2);
      struct radeon_winsys_bo **virtual_buffers =
         realloc(cs->virtual_buffers, sizeof(struct radeon_winsys_bo *) * max_num_virtual_buffers);
      if (!virtual_buffers) {
         cs->status = VK_ERROR_OUT_OF_HOST_MEMORY;
         return;
      }
      cs->max_num_virtual_buffers = max_num_virtual_buffers;
      cs->virtual_buffers = virtual_buffers;
   }

   cs->virtual_buffers[cs->num_virtual_buffers] = bo;

   cs->virtual_buffer_hash_table[hash] = cs->num_virtual_buffers;
   ++cs->num_virtual_buffers;
}

static void
radv_amdgpu_cs_add_buffer(struct radeon_cmdbuf *_cs, struct radeon_winsys_bo *_bo)
{
   struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);
   struct radv_amdgpu_winsys_bo *bo = radv_amdgpu_winsys_bo(_bo);

   if (cs->status != VK_SUCCESS)
      return;

   if (bo->is_virtual) {
      radv_amdgpu_cs_add_virtual_buffer(_cs, _bo);
      return;
   }

   radv_amdgpu_cs_add_buffer_internal(cs, bo->bo_handle, bo->priority);
}

static void
radv_amdgpu_cs_execute_secondary(struct radeon_cmdbuf *_parent, struct radeon_cmdbuf *_child,
                                 bool allow_ib2)
{
   struct radv_amdgpu_cs *parent = radv_amdgpu_cs(_parent);
   struct radv_amdgpu_cs *child = radv_amdgpu_cs(_child);
   struct radv_amdgpu_winsys *ws = parent->ws;
   bool use_ib2 = ws->use_ib_bos && allow_ib2;

   if (parent->status != VK_SUCCESS || child->status != VK_SUCCESS)
      return;

   for (unsigned i = 0; i < child->num_buffers; ++i) {
      radv_amdgpu_cs_add_buffer_internal(parent, child->handles[i].bo_handle,
                                         child->handles[i].bo_priority);
   }

   for (unsigned i = 0; i < child->num_virtual_buffers; ++i) {
      radv_amdgpu_cs_add_buffer(&parent->base, child->virtual_buffers[i]);
   }

   if (use_ib2) {
      if (parent->base.cdw + 4 > parent->base.max_dw)
         radv_amdgpu_cs_grow(&parent->base, 4);

      /* Not setting the CHAIN bit will launch an IB2. */
      radeon_emit(&parent->base, PKT3(PKT3_INDIRECT_BUFFER_CIK, 2, 0));
      radeon_emit(&parent->base, child->ib.ib_mc_address);
      radeon_emit(&parent->base, child->ib.ib_mc_address >> 32);
      radeon_emit(&parent->base, child->ib.size);
   } else {
      if (parent->ws->use_ib_bos) {
         /* Copy and chain old IB buffers from the child to the parent IB. */
         for (unsigned i = 0; i < child->num_old_ib_buffers; i++) {
            struct radv_amdgpu_ib *ib = &child->old_ib_buffers[i];
            uint8_t *mapped;

            if (parent->base.cdw + ib->cdw > parent->base.max_dw)
               radv_amdgpu_cs_grow(&parent->base, ib->cdw);

            mapped = ws->base.buffer_map(ib->bo);
            if (!mapped) {
               parent->status = VK_ERROR_OUT_OF_HOST_MEMORY;
               return;
            }

            /* Copy the IB data without the original chain link. */
            memcpy(parent->base.buf + parent->base.cdw, mapped, 4 * ib->cdw);
            parent->base.cdw += ib->cdw;
         }
      } else {
         /* When the secondary command buffer is huge we have to copy the list of CS buffers to the
          * parent to submit multiple IBs.
          */
         if (child->num_old_cs_buffers > 0) {
            unsigned num_cs_buffers;
            uint32_t *new_buf;

            /* Compute the total number of CS buffers needed. */
            num_cs_buffers = parent->num_old_cs_buffers + child->num_old_cs_buffers + 1;

            struct radeon_cmdbuf *old_cs_buffers =
               realloc(parent->old_cs_buffers, num_cs_buffers * sizeof(*parent->old_cs_buffers));
            if (!old_cs_buffers) {
               parent->status = VK_ERROR_OUT_OF_HOST_MEMORY;
               parent->base.cdw = 0;
               return;
            }
            parent->old_cs_buffers = old_cs_buffers;

            /* Copy the parent CS to its list of CS buffers, so submission ordering is maintained. */
            new_buf = malloc(parent->base.max_dw * 4);
            if (!new_buf) {
               parent->status = VK_ERROR_OUT_OF_HOST_MEMORY;
               parent->base.cdw = 0;
               return;
            }
            memcpy(new_buf, parent->base.buf, parent->base.max_dw * 4);

            parent->old_cs_buffers[parent->num_old_cs_buffers].cdw = parent->base.cdw;
            parent->old_cs_buffers[parent->num_old_cs_buffers].max_dw = parent->base.max_dw;
            parent->old_cs_buffers[parent->num_old_cs_buffers].buf = new_buf;
            parent->num_old_cs_buffers++;

            /* Then, copy all child CS buffers to the parent list. */
            for (unsigned i = 0; i < child->num_old_cs_buffers; i++) {
               new_buf = malloc(child->old_cs_buffers[i].max_dw * 4);
               if (!new_buf) {
                  parent->status = VK_ERROR_OUT_OF_HOST_MEMORY;
                  parent->base.cdw = 0;
                  return;
               }
               memcpy(new_buf, child->old_cs_buffers[i].buf, child->old_cs_buffers[i].max_dw * 4);

               parent->old_cs_buffers[parent->num_old_cs_buffers].cdw = child->old_cs_buffers[i].cdw;
               parent->old_cs_buffers[parent->num_old_cs_buffers].max_dw = child->old_cs_buffers[i].max_dw;
               parent->old_cs_buffers[parent->num_old_cs_buffers].buf = new_buf;
               parent->num_old_cs_buffers++;
            }

            /* Reset the parent CS before copying the child CS into it. */
            parent->base.cdw = 0;
         }
      }

      if (parent->base.cdw + child->base.cdw > parent->base.max_dw)
         radv_amdgpu_cs_grow(&parent->base, child->base.cdw);

      memcpy(parent->base.buf + parent->base.cdw, child->base.buf, 4 * child->base.cdw);
      parent->base.cdw += child->base.cdw;
   }
}

static VkResult
radv_amdgpu_get_bo_list(struct radv_amdgpu_winsys *ws, struct radeon_cmdbuf **cs_array,
                        unsigned count, struct radv_amdgpu_winsys_bo **extra_bo_array,
                        unsigned num_extra_bo, struct radeon_cmdbuf *extra_cs,
                        unsigned *rnum_handles, struct drm_amdgpu_bo_list_entry **rhandles)
{
   struct drm_amdgpu_bo_list_entry *handles = NULL;
   unsigned num_handles = 0;

   if (ws->debug_all_bos) {
      handles = malloc(sizeof(handles[0]) * ws->global_bo_list.count);
      if (!handles) {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      for (uint32_t i = 0; i < ws->global_bo_list.count; i++) {
         handles[i].bo_handle = ws->global_bo_list.bos[i]->bo_handle;
         handles[i].bo_priority = ws->global_bo_list.bos[i]->priority;
         num_handles++;
      }
   } else if (count == 1 && !num_extra_bo && !extra_cs &&
              !radv_amdgpu_cs(cs_array[0])->num_virtual_buffers && !ws->global_bo_list.count) {
      struct radv_amdgpu_cs *cs = (struct radv_amdgpu_cs *)cs_array[0];
      if (cs->num_buffers == 0)
         return VK_SUCCESS;

      handles = malloc(sizeof(handles[0]) * cs->num_buffers);
      if (!handles)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      memcpy(handles, cs->handles, sizeof(handles[0]) * cs->num_buffers);
      num_handles = cs->num_buffers;
   } else {
      unsigned total_buffer_count = num_extra_bo;
      num_handles = num_extra_bo;
      for (unsigned i = 0; i < count; ++i) {
         struct radv_amdgpu_cs *cs = (struct radv_amdgpu_cs *)cs_array[i];
         total_buffer_count += cs->num_buffers;
         for (unsigned j = 0; j < cs->num_virtual_buffers; ++j)
            total_buffer_count += radv_amdgpu_winsys_bo(cs->virtual_buffers[j])->bo_count;
      }

      if (extra_cs) {
         total_buffer_count += ((struct radv_amdgpu_cs *)extra_cs)->num_buffers;
      }

      total_buffer_count += ws->global_bo_list.count;

      if (total_buffer_count == 0)
         return VK_SUCCESS;

      handles = malloc(sizeof(handles[0]) * total_buffer_count);
      if (!handles)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      for (unsigned i = 0; i < num_extra_bo; i++) {
         handles[i].bo_handle = extra_bo_array[i]->bo_handle;
         handles[i].bo_priority = extra_bo_array[i]->priority;
      }

      for (unsigned i = 0; i < count + !!extra_cs; ++i) {
         struct radv_amdgpu_cs *cs;

         if (i == count)
            cs = (struct radv_amdgpu_cs *)extra_cs;
         else
            cs = (struct radv_amdgpu_cs *)cs_array[i];

         if (!cs->num_buffers)
            continue;

         if (num_handles == 0 && !cs->num_virtual_buffers) {
            memcpy(handles, cs->handles, cs->num_buffers * sizeof(struct drm_amdgpu_bo_list_entry));
            num_handles = cs->num_buffers;
            continue;
         }
         int unique_bo_so_far = num_handles;
         for (unsigned j = 0; j < cs->num_buffers; ++j) {
            bool found = false;
            for (unsigned k = 0; k < unique_bo_so_far; ++k) {
               if (handles[k].bo_handle == cs->handles[j].bo_handle) {
                  found = true;
                  break;
               }
            }
            if (!found) {
               handles[num_handles] = cs->handles[j];
               ++num_handles;
            }
         }
         for (unsigned j = 0; j < cs->num_virtual_buffers; ++j) {
            struct radv_amdgpu_winsys_bo *virtual_bo =
               radv_amdgpu_winsys_bo(cs->virtual_buffers[j]);
            for (unsigned k = 0; k < virtual_bo->bo_count; ++k) {
               struct radv_amdgpu_winsys_bo *bo = virtual_bo->bos[k];
               bool found = false;
               for (unsigned m = 0; m < num_handles; ++m) {
                  if (handles[m].bo_handle == bo->bo_handle) {
                     found = true;
                     break;
                  }
               }
               if (!found) {
                  handles[num_handles].bo_handle = bo->bo_handle;
                  handles[num_handles].bo_priority = bo->priority;
                  ++num_handles;
               }
            }
         }
      }

      unsigned unique_bo_so_far = num_handles;
      for (unsigned i = 0; i < ws->global_bo_list.count; ++i) {
         struct radv_amdgpu_winsys_bo *bo = ws->global_bo_list.bos[i];
         bool found = false;
         for (unsigned j = 0; j < unique_bo_so_far; ++j) {
            if (bo->bo_handle == handles[j].bo_handle) {
               found = true;
               break;
            }
         }
         if (!found) {
            handles[num_handles].bo_handle = bo->bo_handle;
            handles[num_handles].bo_priority = bo->priority;
            ++num_handles;
         }
      }
   }

   *rhandles = handles;
   *rnum_handles = num_handles;

   return VK_SUCCESS;
}

static void
radv_assign_last_submit(struct radv_amdgpu_ctx *ctx, struct radv_amdgpu_cs_request *request)
{
   radv_amdgpu_request_to_fence(ctx, &ctx->last_submission[request->ip_type][request->ring],
                                request);
}

static VkResult
radv_amdgpu_winsys_cs_submit_chained(struct radeon_winsys_ctx *_ctx, int queue_idx,
                                     struct radv_winsys_sem_info *sem_info,
                                     struct radeon_cmdbuf **cs_array, unsigned cs_count,
                                     struct radeon_cmdbuf *initial_preamble_cs)
{
   struct radv_amdgpu_ctx *ctx = radv_amdgpu_ctx(_ctx);
   struct radv_amdgpu_cs *cs0 = radv_amdgpu_cs(cs_array[0]);
   struct radv_amdgpu_winsys *aws = cs0->ws;
   struct drm_amdgpu_bo_list_entry *handles = NULL;
   struct radv_amdgpu_cs_request request;
   struct amdgpu_cs_ib_info ibs[2];
   unsigned number_of_ibs = 1;
   unsigned num_handles = 0;
   VkResult result;

   for (unsigned i = cs_count; i--;) {
      struct radv_amdgpu_cs *cs = radv_amdgpu_cs(cs_array[i]);

      if (cs->is_chained) {
         assert(cs->base.cdw <= cs->base.max_dw + 4);
         assert(get_nop_packet(cs) == PKT3_NOP_PAD); /* Other shouldn't chain. */

         cs->is_chained = false;
         cs->base.buf[cs->base.cdw - 4] =  PKT3_NOP_PAD;
         cs->base.buf[cs->base.cdw - 3] =  PKT3_NOP_PAD;
         cs->base.buf[cs->base.cdw - 2] =  PKT3_NOP_PAD;
         cs->base.buf[cs->base.cdw - 1] =  PKT3_NOP_PAD;
      }

      if (i + 1 < cs_count) {
         struct radv_amdgpu_cs *next = radv_amdgpu_cs(cs_array[i + 1]);
         assert(cs->base.cdw <= cs->base.max_dw + 4);
         assert(get_nop_packet(cs) == PKT3_NOP_PAD); /* Other shouldn't chain. */

         cs->is_chained = true;

         cs->base.buf[cs->base.cdw - 4] = PKT3(PKT3_INDIRECT_BUFFER_CIK, 2, 0);
         cs->base.buf[cs->base.cdw - 3] = next->ib.ib_mc_address;
         cs->base.buf[cs->base.cdw - 2] = next->ib.ib_mc_address >> 32;
         cs->base.buf[cs->base.cdw - 1] = S_3F2_CHAIN(1) | S_3F2_VALID(1) | next->ib.size;
      }
   }

   u_rwlock_rdlock(&aws->global_bo_list.lock);

   /* Get the BO list. */
   result = radv_amdgpu_get_bo_list(cs0->ws, cs_array, cs_count, NULL, 0, initial_preamble_cs,
                                    &num_handles, &handles);
   if (result != VK_SUCCESS)
      goto fail;

   /* Configure the CS request. */
   if (initial_preamble_cs) {
      ibs[0] = radv_amdgpu_cs(initial_preamble_cs)->ib;
      ibs[1] = cs0->ib;
      number_of_ibs++;
   } else {
      ibs[0] = cs0->ib;
   }

   request.ip_type = cs0->hw_ip;
   request.ip_instance = 0;
   request.ring = queue_idx;
   request.number_of_ibs = number_of_ibs;
   request.ibs = ibs;
   request.handles = handles;
   request.num_handles = num_handles;

   /* Submit the CS. */
   result = radv_amdgpu_cs_submit(ctx, &request, sem_info);

   free(request.handles);

   if (result != VK_SUCCESS)
      goto fail;

   radv_assign_last_submit(ctx, &request);

fail:
   u_rwlock_rdunlock(&aws->global_bo_list.lock);
   return result;
}

static VkResult
radv_amdgpu_winsys_cs_submit_fallback(struct radeon_winsys_ctx *_ctx, int queue_idx,
                                      struct radv_winsys_sem_info *sem_info,
                                      struct radeon_cmdbuf **cs_array, unsigned cs_count,
                                      struct radeon_cmdbuf *initial_preamble_cs)
{
   struct radv_amdgpu_ctx *ctx = radv_amdgpu_ctx(_ctx);
   struct drm_amdgpu_bo_list_entry *handles = NULL;
   struct radv_amdgpu_cs_request request;
   struct amdgpu_cs_ib_info *ibs;
   struct radv_amdgpu_cs *cs0;
   struct radv_amdgpu_winsys *aws;
   unsigned num_handles = 0;
   unsigned number_of_ibs;
   VkResult result;

   assert(cs_count);
   cs0 = radv_amdgpu_cs(cs_array[0]);
   aws = cs0->ws;

   /* Compute the number of IBs for this submit. */
   number_of_ibs = cs_count + !!initial_preamble_cs;

   u_rwlock_rdlock(&aws->global_bo_list.lock);

   /* Get the BO list. */
   result = radv_amdgpu_get_bo_list(cs0->ws, &cs_array[0], cs_count, NULL, 0, initial_preamble_cs,
                                    &num_handles, &handles);
   if (result != VK_SUCCESS) {
      goto fail;
   }

   ibs = malloc(number_of_ibs * sizeof(*ibs));
   if (!ibs) {
      free(handles);
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   /* Configure the CS request. */
   if (initial_preamble_cs)
      ibs[0] = radv_amdgpu_cs(initial_preamble_cs)->ib;

   for (unsigned i = 0; i < cs_count; i++) {
      struct radv_amdgpu_cs *cs = radv_amdgpu_cs(cs_array[i]);

      ibs[i + !!initial_preamble_cs] = cs->ib;

      if (cs->is_chained) {
         assert(get_nop_packet(cs) == PKT3_NOP_PAD); /* Other shouldn't chain. */

         cs->base.buf[cs->base.cdw - 4] =  PKT3_NOP_PAD;
         cs->base.buf[cs->base.cdw - 3] =  PKT3_NOP_PAD;
         cs->base.buf[cs->base.cdw - 2] =  PKT3_NOP_PAD;
         cs->base.buf[cs->base.cdw - 1] =  PKT3_NOP_PAD;
         cs->is_chained = false;
      }
   }

   request.ip_type = cs0->hw_ip;
   request.ip_instance = 0;
   request.ring = queue_idx;
   request.handles = handles;
   request.num_handles = num_handles;
   request.number_of_ibs = number_of_ibs;
   request.ibs = ibs;

   /* Submit the CS. */
   result = radv_amdgpu_cs_submit(ctx, &request, sem_info);

   free(request.handles);
   free(ibs);

   if (result != VK_SUCCESS)
      goto fail;

   radv_assign_last_submit(ctx, &request);

fail:
   u_rwlock_rdunlock(&aws->global_bo_list.lock);
   return result;
}

static VkResult
radv_amdgpu_winsys_cs_submit_sysmem(struct radeon_winsys_ctx *_ctx, int queue_idx,
                                    struct radv_winsys_sem_info *sem_info,
                                    struct radeon_cmdbuf **cs_array, unsigned cs_count,
                                    struct radeon_cmdbuf *initial_preamble_cs,
                                    struct radeon_cmdbuf *continue_preamble_cs)
{
   struct radv_amdgpu_ctx *ctx = radv_amdgpu_ctx(_ctx);
   struct radv_amdgpu_cs *cs0 = radv_amdgpu_cs(cs_array[0]);
   struct radeon_winsys *ws = (struct radeon_winsys *)cs0->ws;
   struct radv_amdgpu_winsys *aws = cs0->ws;
   struct radv_amdgpu_cs_request request;
   uint32_t pad_word = get_nop_packet(cs0);
   enum ring_type ring_type = hw_ip_to_ring(cs0->hw_ip);
   uint32_t ib_pad_dw_mask = cs0->ws->info.ib_pad_dw_mask[ring_type];
   bool emit_signal_sem = sem_info->cs_emit_signal;
   VkResult result;

   assert(cs_count);

   for (unsigned i = 0; i < cs_count;) {
      struct amdgpu_cs_ib_info *ibs;
      struct radeon_winsys_bo **bos;
      struct radeon_cmdbuf *preamble_cs = i ? continue_preamble_cs : initial_preamble_cs;
      struct radv_amdgpu_cs *cs = radv_amdgpu_cs(cs_array[i]);
      struct drm_amdgpu_bo_list_entry *handles = NULL;
      unsigned num_handles = 0;
      unsigned number_of_ibs;
      uint32_t *ptr;
      unsigned cnt = 0;

      /* Compute the number of IBs for this submit. */
      number_of_ibs = cs->num_old_cs_buffers + 1;

      ibs = malloc(number_of_ibs * sizeof(*ibs));
      if (!ibs)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      bos = malloc(number_of_ibs * sizeof(*bos));
      if (!bos) {
         free(ibs);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      if (number_of_ibs > 1) {
         /* Special path when the maximum size in dwords has
          * been reached because we need to handle more than one
          * IB per submit.
          */
         struct radeon_cmdbuf **new_cs_array;
         unsigned idx = 0;

         new_cs_array = malloc(number_of_ibs * sizeof(*new_cs_array));
         assert(new_cs_array);

         for (unsigned j = 0; j < cs->num_old_cs_buffers; j++)
            new_cs_array[idx++] = &cs->old_cs_buffers[j];
         new_cs_array[idx++] = cs_array[i];

         for (unsigned j = 0; j < number_of_ibs; j++) {
            struct radeon_cmdbuf *rcs = new_cs_array[j];
            bool needs_preamble = preamble_cs && j == 0;
            unsigned pad_words = 0;
            unsigned size = 0;

            if (needs_preamble)
               size += preamble_cs->cdw;
            size += rcs->cdw;

            assert(size < GFX6_MAX_CS_SIZE);

            while (!size || (size & ib_pad_dw_mask)) {
               size++;
               pad_words++;
            }

            ws->buffer_create(
               ws, 4 * size, 4096, radv_amdgpu_cs_domain(ws),
               RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_READ_ONLY |
               RADEON_FLAG_GTT_WC, RADV_BO_PRIORITY_CS, 0, &bos[j]);
            ptr = ws->buffer_map(bos[j]);

            if (needs_preamble) {
               memcpy(ptr, preamble_cs->buf, preamble_cs->cdw * 4);
               ptr += preamble_cs->cdw;
            }

            memcpy(ptr, rcs->buf, 4 * rcs->cdw);
            ptr += rcs->cdw;

            for (unsigned k = 0; k < pad_words; ++k)
               *ptr++ = pad_word;

            ibs[j].size = size;
            ibs[j].ib_mc_address = radv_buffer_get_va(bos[j]);
            ibs[j].flags = 0;
         }

         cnt++;
         free(new_cs_array);
      } else {
         unsigned pad_words = 0;
         unsigned size = 0;

         if (preamble_cs)
            size += preamble_cs->cdw;

         while (i + cnt < cs_count &&
                GFX6_MAX_CS_SIZE - size >= radv_amdgpu_cs(cs_array[i + cnt])->base.cdw) {
            size += radv_amdgpu_cs(cs_array[i + cnt])->base.cdw;
            ++cnt;
         }

         while (!size || (size & ib_pad_dw_mask)) {
            size++;
            pad_words++;
         }
         assert(cnt);

         ws->buffer_create(
            ws, 4 * size, 4096, radv_amdgpu_cs_domain(ws),
            RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_READ_ONLY |
            RADEON_FLAG_GTT_WC, RADV_BO_PRIORITY_CS, 0, &bos[0]);
         ptr = ws->buffer_map(bos[0]);

         if (preamble_cs) {
            memcpy(ptr, preamble_cs->buf, preamble_cs->cdw * 4);
            ptr += preamble_cs->cdw;
         }

         for (unsigned j = 0; j < cnt; ++j) {
            struct radv_amdgpu_cs *cs2 = radv_amdgpu_cs(cs_array[i + j]);
            memcpy(ptr, cs2->base.buf, 4 * cs2->base.cdw);
            ptr += cs2->base.cdw;
         }

         for (unsigned j = 0; j < pad_words; ++j)
            *ptr++ = pad_word;

         ibs[0].size = size;
         ibs[0].ib_mc_address = radv_buffer_get_va(bos[0]);
         ibs[0].flags = 0;
      }

      u_rwlock_rdlock(&aws->global_bo_list.lock);

      result =
         radv_amdgpu_get_bo_list(cs0->ws, &cs_array[i], cnt, (struct radv_amdgpu_winsys_bo **)bos,
                                 number_of_ibs, preamble_cs, &num_handles, &handles);
      if (result != VK_SUCCESS) {
         free(ibs);
         free(bos);
         u_rwlock_rdunlock(&aws->global_bo_list.lock);
         return result;
      }

      request.ip_type = cs0->hw_ip;
      request.ip_instance = 0;
      request.ring = queue_idx;
      request.handles = handles;
      request.num_handles = num_handles;
      request.number_of_ibs = number_of_ibs;
      request.ibs = ibs;

      sem_info->cs_emit_signal = (i == cs_count - cnt) ? emit_signal_sem : false;
      result = radv_amdgpu_cs_submit(ctx, &request, sem_info);

      free(request.handles);
      u_rwlock_rdunlock(&aws->global_bo_list.lock);

      for (unsigned j = 0; j < number_of_ibs; j++) {
         ws->buffer_destroy(ws, bos[j]);
      }

      free(ibs);
      free(bos);

      if (result != VK_SUCCESS)
         return result;

      i += cnt;
   }

   radv_assign_last_submit(ctx, &request);

   return VK_SUCCESS;
}

static VkResult
radv_amdgpu_cs_submit_zero(struct radv_amdgpu_ctx *ctx, enum ring_type ring_type, int queue_idx,
                           struct radv_winsys_sem_info *sem_info)
{
   unsigned hw_ip = ring_to_hw_ip(ring_type);
   unsigned queue_syncobj = radv_amdgpu_ctx_queue_syncobj(ctx, hw_ip, queue_idx);
   int ret;

   if (!queue_syncobj)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   if (sem_info->wait.syncobj_count || sem_info->wait.timeline_syncobj_count) {
      int fd;
      ret = amdgpu_cs_syncobj_export_sync_file(ctx->ws->dev, queue_syncobj, &fd);
      if (ret < 0)
         return VK_ERROR_DEVICE_LOST;

      for (unsigned i = 0; i < sem_info->wait.syncobj_count; ++i) {
         int fd2;
         ret = amdgpu_cs_syncobj_export_sync_file(ctx->ws->dev, sem_info->wait.syncobj[i], &fd2);
         if (ret < 0) {
            close(fd);
            return VK_ERROR_DEVICE_LOST;
         }

         sync_accumulate("radv", &fd, fd2);
         close(fd2);
      }
      for (unsigned i = 0; i < sem_info->wait.timeline_syncobj_count; ++i) {
         int fd2;
         ret = amdgpu_cs_syncobj_export_sync_file2(
            ctx->ws->dev, sem_info->wait.syncobj[i + sem_info->wait.syncobj_count],
            sem_info->wait.points[i], 0, &fd2);
         if (ret < 0) {
            /* This works around a kernel bug where the fence isn't copied if it is already
             * signalled. Since it is already signalled it is totally fine to not wait on it.
             *
             * kernel patch: https://patchwork.freedesktop.org/patch/465583/ */
            uint64_t point;
            ret = amdgpu_cs_syncobj_query2(
               ctx->ws->dev, &sem_info->wait.syncobj[i + sem_info->wait.syncobj_count], &point, 1,
               0);
            if (!ret && point >= sem_info->wait.points[i])
               continue;

            close(fd);
            return VK_ERROR_DEVICE_LOST;
         }

         sync_accumulate("radv", &fd, fd2);
         close(fd2);
      }
      ret = amdgpu_cs_syncobj_import_sync_file(ctx->ws->dev, queue_syncobj, fd);
      close(fd);
      if (ret < 0)
         return VK_ERROR_DEVICE_LOST;

      ctx->queue_syncobj_wait[hw_ip][queue_idx] = true;
   }

   for (unsigned i = 0; i < sem_info->signal.syncobj_count; ++i) {
      uint32_t dst_handle = sem_info->signal.syncobj[i];
      uint32_t src_handle = queue_syncobj;

      if (ctx->ws->info.has_timeline_syncobj) {
         ret = amdgpu_cs_syncobj_transfer(ctx->ws->dev, dst_handle, 0, src_handle, 0, 0);
         if (ret < 0)
            return VK_ERROR_DEVICE_LOST;
      } else {
         int fd;
         ret = amdgpu_cs_syncobj_export_sync_file(ctx->ws->dev, src_handle, &fd);
         if (ret < 0)
            return VK_ERROR_DEVICE_LOST;

         ret = amdgpu_cs_syncobj_import_sync_file(ctx->ws->dev, dst_handle, fd);
         close(fd);
         if (ret < 0)
            return VK_ERROR_DEVICE_LOST;
      }
   }
   for (unsigned i = 0; i < sem_info->signal.timeline_syncobj_count; ++i) {
      ret = amdgpu_cs_syncobj_transfer(ctx->ws->dev,
                                       sem_info->signal.syncobj[i + sem_info->signal.syncobj_count],
                                       sem_info->signal.points[i], queue_syncobj, 0, 0);
      if (ret < 0)
         return VK_ERROR_DEVICE_LOST;
   }
   return VK_SUCCESS;
}

static VkResult
radv_amdgpu_winsys_cs_submit_internal(struct radeon_winsys_ctx *_ctx, enum ring_type ring_type,
                                      int queue_idx, struct radeon_cmdbuf **cs_array,
                                      unsigned cs_count, struct radeon_cmdbuf *initial_preamble_cs,
                                      struct radeon_cmdbuf *continue_preamble_cs,
                                      struct radv_winsys_sem_info *sem_info, bool can_patch)
{
   struct radv_amdgpu_ctx *ctx = radv_amdgpu_ctx(_ctx);
   VkResult result;

   assert(sem_info);
   if (!cs_count) {
      result = radv_amdgpu_cs_submit_zero(ctx, ring_type, queue_idx, sem_info);
   } else if (!ctx->ws->use_ib_bos) {
      result = radv_amdgpu_winsys_cs_submit_sysmem(_ctx, queue_idx, sem_info, cs_array, cs_count,
                                                   initial_preamble_cs, continue_preamble_cs);
   } else if (can_patch) {
      result = radv_amdgpu_winsys_cs_submit_chained(_ctx, queue_idx, sem_info, cs_array, cs_count,
                                                    initial_preamble_cs);
   } else {
      result = radv_amdgpu_winsys_cs_submit_fallback(_ctx, queue_idx, sem_info, cs_array, cs_count,
                                                     initial_preamble_cs);
   }

   return result;
}

static VkResult
radv_amdgpu_winsys_cs_submit(struct radeon_winsys_ctx *_ctx, enum ring_type ring_type,
                             int queue_idx, struct radeon_cmdbuf **cs_array, unsigned cs_count,
                             struct radeon_cmdbuf *initial_preamble_cs,
                             struct radeon_cmdbuf *continue_preamble_cs, uint32_t wait_count,
                             const struct vk_sync_wait *waits, uint32_t signal_count,
                             const struct vk_sync_signal *signals, bool can_patch)
{
   struct radv_amdgpu_winsys *ws = radv_amdgpu_ctx(_ctx)->ws;
   struct radv_winsys_sem_info sem_info;
   memset(&sem_info, 0, sizeof(sem_info));
   VkResult result;
   unsigned wait_idx = 0, signal_idx = 0;

   STACK_ARRAY(uint64_t, wait_points, wait_count);
   STACK_ARRAY(uint32_t, wait_syncobj, wait_count);
   STACK_ARRAY(uint64_t, signal_points, signal_count);
   STACK_ARRAY(uint32_t, signal_syncobj, signal_count);

   if (!wait_points || !wait_syncobj || !signal_points || !signal_syncobj) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto out;
   }

   sem_info.wait.points = wait_points;
   sem_info.wait.syncobj = wait_syncobj;
   sem_info.signal.points = signal_points;
   sem_info.signal.syncobj = signal_syncobj;

   for (uint32_t i = 0; i < wait_count; ++i) {
      if (waits[i].sync->type == &vk_sync_dummy_type)
         continue;

      assert(waits[i].sync->type == &ws->syncobj_sync_type);
      sem_info.wait.syncobj[wait_idx] = ((struct vk_drm_syncobj *)waits[i].sync)->syncobj;
      sem_info.wait.points[wait_idx] = waits[i].wait_value;
      ++wait_idx;
   }

   for (uint32_t i = 0; i < signal_count; ++i) {
      if (signals[i].sync->type == &vk_sync_dummy_type)
         continue;

      assert(signals[i].sync->type == &ws->syncobj_sync_type);
      sem_info.signal.syncobj[signal_idx] = ((struct vk_drm_syncobj *)signals[i].sync)->syncobj;
      sem_info.signal.points[signal_idx] = signals[i].signal_value;
      ++signal_idx;
   }

   assert(signal_idx <= signal_count);
   assert(wait_idx <= wait_count);
   sem_info.wait.timeline_syncobj_count =
      (ws->syncobj_sync_type.features & VK_SYNC_FEATURE_TIMELINE) ? wait_idx : 0;
   sem_info.wait.syncobj_count = wait_idx - sem_info.wait.timeline_syncobj_count;
   sem_info.cs_emit_wait = true;

   sem_info.signal.timeline_syncobj_count =
      (ws->syncobj_sync_type.features & VK_SYNC_FEATURE_TIMELINE) ? signal_idx : 0;
   sem_info.signal.syncobj_count = signal_idx - sem_info.signal.timeline_syncobj_count;
   sem_info.cs_emit_signal = true;

   result = radv_amdgpu_winsys_cs_submit_internal(_ctx, ring_type, queue_idx, cs_array, cs_count,
                                                  initial_preamble_cs, continue_preamble_cs,
                                                  &sem_info, can_patch);

out:
   STACK_ARRAY_FINISH(wait_points);
   STACK_ARRAY_FINISH(wait_syncobj);
   STACK_ARRAY_FINISH(signal_points);
   STACK_ARRAY_FINISH(signal_syncobj);
   return result;
}

static void *
radv_amdgpu_winsys_get_cpu_addr(void *_cs, uint64_t addr)
{
   struct radv_amdgpu_cs *cs = (struct radv_amdgpu_cs *)_cs;
   void *ret = NULL;

   if (!cs->ib_buffer)
      return NULL;
   for (unsigned i = 0; i <= cs->num_old_ib_buffers; ++i) {
      struct radv_amdgpu_winsys_bo *bo;

      bo = (struct radv_amdgpu_winsys_bo *)(i == cs->num_old_ib_buffers ? cs->ib_buffer
                                                                        : cs->old_ib_buffers[i].bo);
      if (addr >= bo->base.va && addr - bo->base.va < bo->size) {
         if (amdgpu_bo_cpu_map(bo->bo, &ret) == 0)
            return (char *)ret + (addr - bo->base.va);
      }
   }
   u_rwlock_rdlock(&cs->ws->global_bo_list.lock);
   for (uint32_t i = 0; i < cs->ws->global_bo_list.count; i++) {
      struct radv_amdgpu_winsys_bo *bo = cs->ws->global_bo_list.bos[i];
      if (addr >= bo->base.va && addr - bo->base.va < bo->size) {
         if (amdgpu_bo_cpu_map(bo->bo, &ret) == 0) {
            u_rwlock_rdunlock(&cs->ws->global_bo_list.lock);
            return (char *)ret + (addr - bo->base.va);
         }
      }
   }
   u_rwlock_rdunlock(&cs->ws->global_bo_list.lock);

   return ret;
}

static void
radv_amdgpu_winsys_cs_dump(struct radeon_cmdbuf *_cs, FILE *file, const int *trace_ids,
                           int trace_id_count)
{
   struct radv_amdgpu_cs *cs = (struct radv_amdgpu_cs *)_cs;
   void *ib = cs->base.buf;
   int num_dw = cs->base.cdw;

   if (cs->ws->use_ib_bos) {
      ib = radv_amdgpu_winsys_get_cpu_addr(cs, cs->ib.ib_mc_address);
      num_dw = cs->ib.size;
   }
   assert(ib);
   ac_parse_ib(file, ib, num_dw, trace_ids, trace_id_count, "main IB", cs->ws->info.chip_class,
               radv_amdgpu_winsys_get_cpu_addr, cs);
}

static uint32_t
radv_to_amdgpu_priority(enum radeon_ctx_priority radv_priority)
{
   switch (radv_priority) {
   case RADEON_CTX_PRIORITY_REALTIME:
      return AMDGPU_CTX_PRIORITY_VERY_HIGH;
   case RADEON_CTX_PRIORITY_HIGH:
      return AMDGPU_CTX_PRIORITY_HIGH;
   case RADEON_CTX_PRIORITY_MEDIUM:
      return AMDGPU_CTX_PRIORITY_NORMAL;
   case RADEON_CTX_PRIORITY_LOW:
      return AMDGPU_CTX_PRIORITY_LOW;
   default:
      unreachable("Invalid context priority");
   }
}

static VkResult
radv_amdgpu_ctx_create(struct radeon_winsys *_ws, enum radeon_ctx_priority priority,
                       struct radeon_winsys_ctx **rctx)
{
   struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);
   struct radv_amdgpu_ctx *ctx = CALLOC_STRUCT(radv_amdgpu_ctx);
   uint32_t amdgpu_priority = radv_to_amdgpu_priority(priority);
   VkResult result;
   int r;

   if (!ctx)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   r = amdgpu_cs_ctx_create2(ws->dev, amdgpu_priority, &ctx->ctx);
   if (r && r == -EACCES) {
      result = VK_ERROR_NOT_PERMITTED_EXT;
      goto fail_create;
   } else if (r) {
      fprintf(stderr, "amdgpu: radv_amdgpu_cs_ctx_create2 failed. (%i)\n", r);
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail_create;
   }
   ctx->ws = ws;

   assert(AMDGPU_HW_IP_NUM * MAX_RINGS_PER_TYPE * sizeof(uint64_t) <= 4096);
   result = ws->base.buffer_create(&ws->base, 4096, 8, RADEON_DOMAIN_GTT,
                                   RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING,
                                   RADV_BO_PRIORITY_CS, 0, &ctx->fence_bo);
   if (result != VK_SUCCESS) {
      goto fail_alloc;
   }

   *rctx = (struct radeon_winsys_ctx *)ctx;
   return VK_SUCCESS;

fail_alloc:
   amdgpu_cs_ctx_free(ctx->ctx);
fail_create:
   FREE(ctx);
   return result;
}

static void
radv_amdgpu_ctx_destroy(struct radeon_winsys_ctx *rwctx)
{
   struct radv_amdgpu_ctx *ctx = (struct radv_amdgpu_ctx *)rwctx;

   for (unsigned ip = 0; ip <= AMDGPU_HW_IP_DMA; ++ip) {
      for (unsigned ring = 0; ring < MAX_RINGS_PER_TYPE; ++ring) {
         if (ctx->queue_syncobj[ip][ring])
            amdgpu_cs_destroy_syncobj(ctx->ws->dev, ctx->queue_syncobj[ip][ring]);
      }
   }

   ctx->ws->base.buffer_destroy(&ctx->ws->base, ctx->fence_bo);
   amdgpu_cs_ctx_free(ctx->ctx);
   FREE(ctx);
}

static uint32_t
radv_amdgpu_ctx_queue_syncobj(struct radv_amdgpu_ctx *ctx, unsigned ip, unsigned ring)
{
   uint32_t *syncobj = &ctx->queue_syncobj[ip][ring];
   if (!*syncobj) {
      amdgpu_cs_create_syncobj2(ctx->ws->dev, DRM_SYNCOBJ_CREATE_SIGNALED, syncobj);
   }
   return *syncobj;
}

static bool
radv_amdgpu_ctx_wait_idle(struct radeon_winsys_ctx *rwctx, enum ring_type ring_type, int ring_index)
{
   struct radv_amdgpu_ctx *ctx = (struct radv_amdgpu_ctx *)rwctx;
   int ip_type = ring_to_hw_ip(ring_type);

   if (ctx->last_submission[ip_type][ring_index].fence.fence) {
      uint32_t expired;
      int ret = amdgpu_cs_query_fence_status(&ctx->last_submission[ip_type][ring_index].fence,
                                             1000000000ull, 0, &expired);

      if (ret || !expired)
         return false;
   }

   return true;
}

static void *
radv_amdgpu_cs_alloc_syncobj_chunk(struct radv_winsys_sem_counts *counts, uint32_t queue_syncobj,
                                   struct drm_amdgpu_cs_chunk *chunk, int chunk_id)
{
   unsigned count = counts->syncobj_count + (queue_syncobj ? 1 : 0);
   struct drm_amdgpu_cs_chunk_sem *syncobj =
      malloc(sizeof(struct drm_amdgpu_cs_chunk_sem) * count);
   if (!syncobj)
      return NULL;

   for (unsigned i = 0; i < counts->syncobj_count; i++) {
      struct drm_amdgpu_cs_chunk_sem *sem = &syncobj[i];
      sem->handle = counts->syncobj[i];
   }

   if (queue_syncobj)
      syncobj[counts->syncobj_count].handle = queue_syncobj;

   chunk->chunk_id = chunk_id;
   chunk->length_dw = sizeof(struct drm_amdgpu_cs_chunk_sem) / 4 * count;
   chunk->chunk_data = (uint64_t)(uintptr_t)syncobj;
   return syncobj;
}

static void *
radv_amdgpu_cs_alloc_timeline_syncobj_chunk(struct radv_winsys_sem_counts *counts,
                                            uint32_t queue_syncobj,
                                            struct drm_amdgpu_cs_chunk *chunk, int chunk_id)
{
   uint32_t count =
      counts->syncobj_count + counts->timeline_syncobj_count + (queue_syncobj ? 1 : 0);
   struct drm_amdgpu_cs_chunk_syncobj *syncobj =
      malloc(sizeof(struct drm_amdgpu_cs_chunk_syncobj) * count);
   if (!syncobj)
      return NULL;

   for (unsigned i = 0; i < counts->syncobj_count; i++) {
      struct drm_amdgpu_cs_chunk_syncobj *sem = &syncobj[i];
      sem->handle = counts->syncobj[i];
      sem->flags = 0;
      sem->point = 0;
   }

   for (unsigned i = 0; i < counts->timeline_syncobj_count; i++) {
      struct drm_amdgpu_cs_chunk_syncobj *sem = &syncobj[i + counts->syncobj_count];
      sem->handle = counts->syncobj[i + counts->syncobj_count];
      sem->flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
      sem->point = counts->points[i];
   }

   if (queue_syncobj) {
      syncobj[count - 1].handle = queue_syncobj;
      syncobj[count - 1].flags = 0;
      syncobj[count - 1].point = 0;
   }

   chunk->chunk_id = chunk_id;
   chunk->length_dw = sizeof(struct drm_amdgpu_cs_chunk_syncobj) / 4 * count;
   chunk->chunk_data = (uint64_t)(uintptr_t)syncobj;
   return syncobj;
}

static VkResult
radv_amdgpu_cs_submit(struct radv_amdgpu_ctx *ctx, struct radv_amdgpu_cs_request *request,
                      struct radv_winsys_sem_info *sem_info)
{
   int r;
   int num_chunks;
   int size;
   struct drm_amdgpu_cs_chunk *chunks;
   struct drm_amdgpu_cs_chunk_data *chunk_data;
   bool use_bo_list_create = ctx->ws->info.drm_minor < 27;
   struct drm_amdgpu_bo_list_in bo_list_in;
   void *wait_syncobj = NULL, *signal_syncobj = NULL;
   int i;
   uint32_t bo_list = 0;
   VkResult result = VK_SUCCESS;
   uint32_t queue_syncobj = radv_amdgpu_ctx_queue_syncobj(ctx, request->ip_type, request->ring);
   bool *queue_syncobj_wait = &ctx->queue_syncobj_wait[request->ip_type][request->ring];

   if (!queue_syncobj)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   size = request->number_of_ibs + 2 /* user fence */ + (!use_bo_list_create ? 1 : 0) + 3;

   chunks = malloc(sizeof(chunks[0]) * size);
   if (!chunks)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   size = request->number_of_ibs + 1 /* user fence */;

   chunk_data = malloc(sizeof(chunk_data[0]) * size);
   if (!chunk_data) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto error_out;
   }

   num_chunks = request->number_of_ibs;
   for (i = 0; i < request->number_of_ibs; i++) {
      struct amdgpu_cs_ib_info *ib;
      chunks[i].chunk_id = AMDGPU_CHUNK_ID_IB;
      chunks[i].length_dw = sizeof(struct drm_amdgpu_cs_chunk_ib) / 4;
      chunks[i].chunk_data = (uint64_t)(uintptr_t)&chunk_data[i];

      ib = &request->ibs[i];

      chunk_data[i].ib_data._pad = 0;
      chunk_data[i].ib_data.va_start = ib->ib_mc_address;
      chunk_data[i].ib_data.ib_bytes = ib->size * 4;
      chunk_data[i].ib_data.ip_type = request->ip_type;
      chunk_data[i].ib_data.ip_instance = request->ip_instance;
      chunk_data[i].ib_data.ring = request->ring;
      chunk_data[i].ib_data.flags = ib->flags;
   }

   i = num_chunks++;
   chunks[i].chunk_id = AMDGPU_CHUNK_ID_FENCE;
   chunks[i].length_dw = sizeof(struct drm_amdgpu_cs_chunk_fence) / 4;
   chunks[i].chunk_data = (uint64_t)(uintptr_t)&chunk_data[i];

   struct amdgpu_cs_fence_info fence_info;
   fence_info.handle = radv_amdgpu_winsys_bo(ctx->fence_bo)->bo;
   fence_info.offset = (request->ip_type * MAX_RINGS_PER_TYPE + request->ring) * sizeof(uint64_t);
   amdgpu_cs_chunk_fence_info_to_data(&fence_info, &chunk_data[i]);

   if (sem_info->cs_emit_wait && (sem_info->wait.timeline_syncobj_count ||
                                  sem_info->wait.syncobj_count || *queue_syncobj_wait)) {

      if (ctx->ws->info.has_timeline_syncobj) {
         wait_syncobj = radv_amdgpu_cs_alloc_timeline_syncobj_chunk(
            &sem_info->wait, queue_syncobj, &chunks[num_chunks],
            AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_WAIT);
      } else {
         wait_syncobj = radv_amdgpu_cs_alloc_syncobj_chunk(
            &sem_info->wait, queue_syncobj, &chunks[num_chunks], AMDGPU_CHUNK_ID_SYNCOBJ_IN);
      }
      if (!wait_syncobj) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto error_out;
      }
      num_chunks++;

      sem_info->cs_emit_wait = false;
      *queue_syncobj_wait = false;
   }

   if (sem_info->cs_emit_signal) {
      if (ctx->ws->info.has_timeline_syncobj) {
         signal_syncobj = radv_amdgpu_cs_alloc_timeline_syncobj_chunk(
            &sem_info->signal, queue_syncobj, &chunks[num_chunks],
            AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_SIGNAL);
      } else {
         signal_syncobj = radv_amdgpu_cs_alloc_syncobj_chunk(
            &sem_info->signal, queue_syncobj, &chunks[num_chunks], AMDGPU_CHUNK_ID_SYNCOBJ_OUT);
      }
      if (!signal_syncobj) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto error_out;
      }
      num_chunks++;
   }

   if (use_bo_list_create) {
      /* Legacy path creating the buffer list handle and passing it
       * to the CS ioctl.
       */
      r = amdgpu_bo_list_create_raw(ctx->ws->dev, request->num_handles,
                                    request->handles, &bo_list);
      if (r) {
         if (r == -ENOMEM) {
            fprintf(stderr, "amdgpu: Not enough memory for buffer list creation.\n");
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
         } else {
            fprintf(stderr, "amdgpu: buffer list creation failed (%d).\n", r);
            result = VK_ERROR_UNKNOWN;
         }
         goto error_out;
      }
   } else {
      /* Standard path passing the buffer list via the CS ioctl. */
      bo_list_in.operation = ~0;
      bo_list_in.list_handle = ~0;
      bo_list_in.bo_number = request->num_handles;
      bo_list_in.bo_info_size = sizeof(struct drm_amdgpu_bo_list_entry);
      bo_list_in.bo_info_ptr = (uint64_t)(uintptr_t)request->handles;

      chunks[num_chunks].chunk_id = AMDGPU_CHUNK_ID_BO_HANDLES;
      chunks[num_chunks].length_dw = sizeof(struct drm_amdgpu_bo_list_in) / 4;
      chunks[num_chunks].chunk_data = (uintptr_t)&bo_list_in;
      num_chunks++;
   }

   r = amdgpu_cs_submit_raw2(ctx->ws->dev, ctx->ctx, bo_list, num_chunks, chunks, &request->seq_no);

   if (r) {
      if (r == -ENOMEM) {
         fprintf(stderr, "amdgpu: Not enough memory for command submission.\n");
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
      } else if (r == -ECANCELED) {
         fprintf(stderr, "amdgpu: The CS has been cancelled because the context is lost.\n");
         result = VK_ERROR_DEVICE_LOST;
      } else {
         fprintf(stderr,
                 "amdgpu: The CS has been rejected, "
                 "see dmesg for more information (%i).\n",
                 r);
         result = VK_ERROR_UNKNOWN;
      }
   }

   if (bo_list)
      amdgpu_bo_list_destroy_raw(ctx->ws->dev, bo_list);

error_out:
   free(chunks);
   free(chunk_data);
   free(wait_syncobj);
   free(signal_syncobj);
   return result;
}

void
radv_amdgpu_cs_init_functions(struct radv_amdgpu_winsys *ws)
{
   ws->base.ctx_create = radv_amdgpu_ctx_create;
   ws->base.ctx_destroy = radv_amdgpu_ctx_destroy;
   ws->base.ctx_wait_idle = radv_amdgpu_ctx_wait_idle;
   ws->base.cs_domain = radv_amdgpu_cs_domain;
   ws->base.cs_create = radv_amdgpu_cs_create;
   ws->base.cs_destroy = radv_amdgpu_cs_destroy;
   ws->base.cs_grow = radv_amdgpu_cs_grow;
   ws->base.cs_finalize = radv_amdgpu_cs_finalize;
   ws->base.cs_reset = radv_amdgpu_cs_reset;
   ws->base.cs_add_buffer = radv_amdgpu_cs_add_buffer;
   ws->base.cs_execute_secondary = radv_amdgpu_cs_execute_secondary;
   ws->base.cs_submit = radv_amdgpu_winsys_cs_submit;
   ws->base.cs_dump = radv_amdgpu_winsys_cs_dump;
}
