/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_cmd_buffer.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_desc_state.h"
#include "panvk_cmd_draw.h"
#include "panvk_cmd_fb_preload.h"
#include "panvk_cmd_pool.h"
#include "panvk_cmd_push_constant.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_instance.h"
#include "panvk_physical_device.h"
#include "panvk_priv_bo.h"

#include "pan_desc.h"
#include "pan_encoder.h"
#include "pan_props.h"
#include "pan_samples.h"

#include "vk_descriptor_update_template.h"
#include "vk_format.h"

static VkResult
panvk_cmd_prepare_fragment_job(struct panvk_cmd_buffer *cmdbuf, mali_ptr fbd)
{
   const struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;
   struct panvk_batch *batch = cmdbuf->cur_batch;
   struct panfrost_ptr job_ptr = panvk_cmd_alloc_desc(cmdbuf, FRAGMENT_JOB);

   if (!job_ptr.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   GENX(pan_emit_fragment_job_payload)(fbinfo, fbd, job_ptr.cpu);

   pan_section_pack(job_ptr.cpu, FRAGMENT_JOB, HEADER, header) {
      header.type = MALI_JOB_TYPE_FRAGMENT;
      header.index = 1;
   }

   pan_jc_add_job(&batch->frag_jc, MALI_JOB_TYPE_FRAGMENT, false, false, 0, 0,
                  &job_ptr, false);
   util_dynarray_append(&batch->jobs, void *, job_ptr.cpu);
   return VK_SUCCESS;
}

void
panvk_per_arch(cmd_close_batch)(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;

   if (!batch)
      return;

   struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;

   assert(batch);

   if (!batch->fb.desc.gpu && !batch->vtc_jc.first_job) {
      if (util_dynarray_num_elements(&batch->event_ops,
                                     struct panvk_cmd_event_op) == 0) {
         /* Content-less batch, let's drop it */
         vk_free(&cmdbuf->vk.pool->alloc, batch);
      } else {
         /* Batch has no jobs but is needed for synchronization, let's add a
          * NULL job so the SUBMIT ioctl doesn't choke on it.
          */
         struct panfrost_ptr ptr = panvk_cmd_alloc_desc(cmdbuf, JOB_HEADER);

         if (ptr.gpu) {
            util_dynarray_append(&batch->jobs, void *, ptr.cpu);
            pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_NULL, false, false, 0,
                           0, &ptr, false);
         }

         list_addtail(&batch->node, &cmdbuf->batches);
      }
      cmdbuf->cur_batch = NULL;
      return;
   }

   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);

   list_addtail(&batch->node, &cmdbuf->batches);

   if (batch->tlsinfo.tls.size) {
      unsigned thread_tls_alloc =
         panfrost_query_thread_tls_alloc(&phys_dev->kmod.props);
      unsigned core_id_range;

      panfrost_query_core_count(&phys_dev->kmod.props, &core_id_range);

      unsigned size = panfrost_get_total_stack_size(
         batch->tlsinfo.tls.size, thread_tls_alloc, core_id_range);
      batch->tlsinfo.tls.ptr =
         panvk_cmd_alloc_dev_mem(cmdbuf, tls, size, 4096).gpu;
   }

   if (batch->tlsinfo.wls.size) {
      assert(batch->wls_total_size);
      batch->tlsinfo.wls.ptr =
         panvk_cmd_alloc_dev_mem(cmdbuf, tls, batch->wls_total_size, 4096).gpu;
   }

   if (batch->tls.cpu)
      GENX(pan_emit_tls)(&batch->tlsinfo, batch->tls.cpu);

   if (batch->fb.desc.cpu) {
      fbinfo->sample_positions = dev->sample_positions->addr.dev +
                                 panfrost_sample_positions_offset(
                                    pan_sample_pattern(fbinfo->nr_samples));

      if (batch->vtc_jc.first_tiler) {
         VkResult result = panvk_per_arch(cmd_fb_preload)(cmdbuf);
	 if (result != VK_SUCCESS)
            return;
      }

      for (uint32_t i = 0; i < batch->fb.layer_count; i++) {
         VkResult result;

         mali_ptr fbd = batch->fb.desc.gpu + (batch->fb.desc_stride * i);

         result = panvk_per_arch(cmd_prepare_tiler_context)(cmdbuf, i);
         if (result != VK_SUCCESS)
            break;

         fbd |= GENX(pan_emit_fbd)(
            &cmdbuf->state.gfx.render.fb.info, i, &batch->tlsinfo,
            &batch->tiler.ctx,
            batch->fb.desc.cpu + (batch->fb.desc_stride * i));

         result = panvk_cmd_prepare_fragment_job(cmdbuf, fbd);
         if (result != VK_SUCCESS)
            break;
      }
   }

   cmdbuf->cur_batch = NULL;
}

VkResult
panvk_per_arch(cmd_alloc_fb_desc)(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;

   if (batch->fb.desc.gpu)
      return VK_SUCCESS;

   const struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;
   bool has_zs_ext = fbinfo->zs.view.zs || fbinfo->zs.view.s;
   batch->fb.layer_count = cmdbuf->state.gfx.render.layer_count;
   unsigned fbd_size = pan_size(FRAMEBUFFER);

   if (has_zs_ext)
      fbd_size = ALIGN_POT(fbd_size, pan_alignment(ZS_CRC_EXTENSION)) +
                 pan_size(ZS_CRC_EXTENSION);

   fbd_size = ALIGN_POT(fbd_size, pan_alignment(RENDER_TARGET)) +
              (MAX2(fbinfo->rt_count, 1) * pan_size(RENDER_TARGET));

   batch->fb.bo_count = cmdbuf->state.gfx.render.fb.bo_count;
   memcpy(batch->fb.bos, cmdbuf->state.gfx.render.fb.bos,
          batch->fb.bo_count * sizeof(batch->fb.bos[0]));

   batch->fb.desc =
      panvk_cmd_alloc_dev_mem(cmdbuf, desc, fbd_size * batch->fb.layer_count,
                              pan_alignment(FRAMEBUFFER));
   batch->fb.desc_stride = fbd_size;

   memset(&cmdbuf->state.gfx.render.fb.info.bifrost.pre_post.dcds, 0,
          sizeof(cmdbuf->state.gfx.render.fb.info.bifrost.pre_post.dcds));

   return batch->fb.desc.gpu ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

VkResult
panvk_per_arch(cmd_alloc_tls_desc)(struct panvk_cmd_buffer *cmdbuf, bool gfx)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;

   assert(batch);
   if (!batch->tls.gpu) {
      batch->tls = panvk_cmd_alloc_desc(cmdbuf, LOCAL_STORAGE);
      if (!batch->tls.gpu)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   return VK_SUCCESS;
}

VkResult
panvk_per_arch(cmd_prepare_tiler_context)(struct panvk_cmd_buffer *cmdbuf,
                                          uint32_t layer_idx)
{
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_batch *batch = cmdbuf->cur_batch;
   mali_ptr tiler_desc;

   if (batch->tiler.ctx_descs.gpu) {
      tiler_desc =
         batch->tiler.ctx_descs.gpu + (pan_size(TILER_CONTEXT) * layer_idx);
      goto out_set_layer_ctx;
   }

   const struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;
   uint32_t layer_count = cmdbuf->state.gfx.render.layer_count;
   batch->tiler.heap_desc = panvk_cmd_alloc_desc(cmdbuf, TILER_HEAP);
   batch->tiler.ctx_descs =
      panvk_cmd_alloc_desc_array(cmdbuf, layer_count, TILER_CONTEXT);
   if (!batch->tiler.heap_desc.gpu || !batch->tiler.ctx_descs.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   tiler_desc =
      batch->tiler.ctx_descs.gpu + (pan_size(TILER_CONTEXT) * layer_idx);

   pan_pack(&batch->tiler.heap_templ, TILER_HEAP, cfg) {
      cfg.size = pan_kmod_bo_size(dev->tiler_heap->bo);
      cfg.base = dev->tiler_heap->addr.dev;
      cfg.bottom = dev->tiler_heap->addr.dev;
      cfg.top = cfg.base + cfg.size;
   }

   pan_pack(&batch->tiler.ctx_templ, TILER_CONTEXT, cfg) {
      cfg.hierarchy_mask = panvk_select_tiler_hierarchy_mask(cmdbuf);
      cfg.fb_width = fbinfo->width;
      cfg.fb_height = fbinfo->height;
      cfg.heap = batch->tiler.heap_desc.gpu;
      cfg.sample_pattern = pan_sample_pattern(fbinfo->nr_samples);
   }

   memcpy(batch->tiler.heap_desc.cpu, &batch->tiler.heap_templ,
          sizeof(batch->tiler.heap_templ));

   struct mali_tiler_context_packed *ctxs = batch->tiler.ctx_descs.cpu;

   assert(layer_count > 0);
   for (uint32_t i = 0; i < layer_count; i++) {
      STATIC_ASSERT(
         !(pan_size(TILER_CONTEXT) & (pan_alignment(TILER_CONTEXT) - 1)));

      memcpy(&ctxs[i], &batch->tiler.ctx_templ, sizeof(*ctxs));
   }

out_set_layer_ctx:
   if (PAN_ARCH >= 9)
      batch->tiler.ctx.valhall.desc = tiler_desc;
   else
      batch->tiler.ctx.bifrost.desc = tiler_desc;

   return VK_SUCCESS;
}

struct panvk_batch *
panvk_per_arch(cmd_open_batch)(struct panvk_cmd_buffer *cmdbuf)
{
   assert(!cmdbuf->cur_batch);
   cmdbuf->cur_batch =
      vk_zalloc(&cmdbuf->vk.pool->alloc, sizeof(*cmdbuf->cur_batch), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   util_dynarray_init(&cmdbuf->cur_batch->jobs, NULL);
   util_dynarray_init(&cmdbuf->cur_batch->event_ops, NULL);
   assert(cmdbuf->cur_batch);
   return cmdbuf->cur_batch;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(EndCommandBuffer)(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   panvk_per_arch(cmd_close_batch)(cmdbuf);

   return vk_command_buffer_end(&cmdbuf->vk);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdPipelineBarrier2)(VkCommandBuffer commandBuffer,
                                    const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   /* Caches are flushed/invalidated at batch boundaries for now, nothing to do
    * for memory barriers assuming we implement barriers with the creation of a
    * new batch.
    * FIXME: We can probably do better with a CacheFlush job that has the
    * barrier flag set to true.
    */
   if (cmdbuf->cur_batch) {
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_per_arch(cmd_preload_fb_after_batch_split)(cmdbuf);
      panvk_per_arch(cmd_open_batch)(cmdbuf);
   }
}

static void
panvk_reset_cmdbuf(struct vk_command_buffer *vk_cmdbuf,
                   VkCommandBufferResetFlags flags)
{
   struct panvk_cmd_buffer *cmdbuf =
      container_of(vk_cmdbuf, struct panvk_cmd_buffer, vk);

   vk_command_buffer_reset(&cmdbuf->vk);

   list_for_each_entry_safe(struct panvk_batch, batch, &cmdbuf->batches, node) {
      list_del(&batch->node);
      util_dynarray_fini(&batch->jobs);
      util_dynarray_fini(&batch->event_ops);

      vk_free(&cmdbuf->vk.pool->alloc, batch);
   }

   panvk_pool_reset(&cmdbuf->desc_pool);
   panvk_pool_reset(&cmdbuf->tls_pool);
   panvk_pool_reset(&cmdbuf->varying_pool);
   panvk_cmd_buffer_obj_list_reset(cmdbuf, push_sets);

   memset(&cmdbuf->state, 0, sizeof(cmdbuf->state));
}

static void
panvk_destroy_cmdbuf(struct vk_command_buffer *vk_cmdbuf)
{
   struct panvk_cmd_buffer *cmdbuf =
      container_of(vk_cmdbuf, struct panvk_cmd_buffer, vk);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);

   list_for_each_entry_safe(struct panvk_batch, batch, &cmdbuf->batches, node) {
      list_del(&batch->node);
      util_dynarray_fini(&batch->jobs);
      util_dynarray_fini(&batch->event_ops);

      vk_free(&cmdbuf->vk.pool->alloc, batch);
   }

   panvk_pool_cleanup(&cmdbuf->desc_pool);
   panvk_pool_cleanup(&cmdbuf->tls_pool);
   panvk_pool_cleanup(&cmdbuf->varying_pool);
   panvk_cmd_buffer_obj_list_cleanup(cmdbuf, push_sets);
   vk_command_buffer_finish(&cmdbuf->vk);
   vk_free(&dev->vk.alloc, cmdbuf);
}

static VkResult
panvk_create_cmdbuf(struct vk_command_pool *vk_pool, VkCommandBufferLevel level,
                    struct vk_command_buffer **cmdbuf_out)
{
   struct panvk_device *device =
      container_of(vk_pool->base.device, struct panvk_device, vk);
   struct panvk_cmd_pool *pool =
      container_of(vk_pool, struct panvk_cmd_pool, vk);
   struct panvk_cmd_buffer *cmdbuf;

   cmdbuf = vk_zalloc(&device->vk.alloc, sizeof(*cmdbuf), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmdbuf)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_command_buffer_init(
      &pool->vk, &cmdbuf->vk, &panvk_per_arch(cmd_buffer_ops), level);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, cmdbuf);
      return result;
   }

   panvk_cmd_buffer_obj_list_init(cmdbuf, push_sets);
   cmdbuf->vk.dynamic_graphics_state.vi = &cmdbuf->state.gfx.dynamic.vi;
   cmdbuf->vk.dynamic_graphics_state.ms.sample_locations =
      &cmdbuf->state.gfx.dynamic.sl;

   struct panvk_pool_properties desc_pool_props = {
      .create_flags = 0,
      .slab_size = 64 * 1024,
      .label = "Command buffer descriptor pool",
      .prealloc = true,
      .owns_bos = true,
      .needs_locking = false,
   };
   panvk_pool_init(&cmdbuf->desc_pool, device, &pool->desc_bo_pool,
                   &desc_pool_props);

   struct panvk_pool_properties tls_pool_props = {
      .create_flags =
         panvk_device_adjust_bo_flags(device, PAN_KMOD_BO_FLAG_NO_MMAP),
      .slab_size = 64 * 1024,
      .label = "TLS pool",
      .prealloc = false,
      .owns_bos = true,
      .needs_locking = false,
   };
   panvk_pool_init(&cmdbuf->tls_pool, device, &pool->tls_bo_pool,
                   &tls_pool_props);

   struct panvk_pool_properties var_pool_props = {
      .create_flags =
         panvk_device_adjust_bo_flags(device, PAN_KMOD_BO_FLAG_NO_MMAP),
      .slab_size = 64 * 1024,
      .label = "Varying pool",
      .prealloc = false,
      .owns_bos = true,
      .needs_locking = false,
   };
   panvk_pool_init(&cmdbuf->varying_pool, device, &pool->varying_bo_pool,
                   &var_pool_props);

   list_inithead(&cmdbuf->batches);
   *cmdbuf_out = &cmdbuf->vk;
   return VK_SUCCESS;
}

const struct vk_command_buffer_ops panvk_per_arch(cmd_buffer_ops) = {
   .create = panvk_create_cmdbuf,
   .reset = panvk_reset_cmdbuf,
   .destroy = panvk_destroy_cmdbuf,
};

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(BeginCommandBuffer)(VkCommandBuffer commandBuffer,
                                   const VkCommandBufferBeginInfo *pBeginInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   vk_command_buffer_begin(&cmdbuf->vk, pBeginInfo);

   return VK_SUCCESS;
}
