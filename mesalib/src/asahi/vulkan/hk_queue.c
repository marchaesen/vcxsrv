/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "hk_queue.h"

#include "agx_bo.h"
#include "agx_device.h"
#include "agx_pack.h"
#include "decode.h"
#include "hk_cmd_buffer.h"
#include "hk_device.h"
#include "hk_physical_device.h"

#include <xf86drm.h>
#include "asahi/lib/unstable_asahi_drm.h"
#include "util/list.h"
#include "vulkan/vulkan_core.h"

#include "vk_drm_syncobj.h"
#include "vk_sync.h"

/*
 * We need to specially handle submits with no control streams. The kernel
 * can't accept empty submits, but we can end up here in Vulkan for
 * synchronization purposes only. Rather than submit a no-op job (slow),
 * we simply tie the fences together.
 */
static VkResult
queue_submit_empty(struct hk_device *dev, struct hk_queue *queue,
                   struct vk_queue_submit *submit)
{
   int fd = dev->dev.fd;

   /* Transfer the waits into the queue timeline. */
   for (unsigned i = 0; i < submit->wait_count; ++i) {
      struct vk_sync_wait *wait = &submit->waits[i];

      assert(vk_sync_type_is_drm_syncobj(wait->sync->type));
      const struct vk_drm_syncobj *syncobj = vk_sync_as_drm_syncobj(wait->sync);

      drmSyncobjTransfer(fd, queue->drm.syncobj, ++queue->drm.timeline_value,
                         syncobj->syncobj, wait->wait_value, 0);
   }

   /* Transfer the queue timeline into each out fence. They will all be
    * signalled when we reach this point.
    */
   for (unsigned i = 0; i < submit->signal_count; ++i) {
      struct vk_sync_signal *signal = &submit->signals[i];

      assert(vk_sync_type_is_drm_syncobj(signal->sync->type));
      const struct vk_drm_syncobj *syncobj =
         vk_sync_as_drm_syncobj(signal->sync);

      drmSyncobjTransfer(fd, syncobj->syncobj, signal->signal_value,
                         queue->drm.syncobj, queue->drm.timeline_value, 0);
   }

   return VK_SUCCESS;
}

static void
asahi_fill_cdm_command(struct hk_device *dev, struct hk_cs *cs,
                       struct drm_asahi_cmd_compute *cmd)
{
   size_t len = cs->stream_linked ? 65536 /* XXX */ : (cs->current - cs->start);

   *cmd = (struct drm_asahi_cmd_compute){
      .encoder_ptr = cs->addr,
      .encoder_end = cs->addr + len,

      .sampler_array = dev->samplers.table.bo->va->addr,
      .sampler_count = dev->samplers.table.alloc,
      .sampler_max = dev->samplers.table.alloc + 1,

      .usc_base = dev->dev.shader_base,

      .encoder_id = agx_get_global_id(&dev->dev),
      .cmd_id = agx_get_global_id(&dev->dev),
      .unk_mask = 0xffffffff,
   };

   if (cs->scratch.cs.main || cs->scratch.cs.preamble) {
      cmd->helper_arg = dev->scratch.cs.buf->va->addr;
      cmd->helper_cfg = cs->scratch.cs.preamble ? (1 << 16) : 0;
      cmd->helper_program = dev->dev.helper->va->addr | 1;
   }
}

static void
asahi_fill_vdm_command(struct hk_device *dev, struct hk_cs *cs,
                       struct drm_asahi_cmd_render *c)
{
   unsigned cmd_ta_id = agx_get_global_id(&dev->dev);
   unsigned cmd_3d_id = agx_get_global_id(&dev->dev);
   unsigned encoder_id = agx_get_global_id(&dev->dev);

   memset(c, 0, sizeof(*c));

   c->encoder_ptr = cs->addr;
   c->encoder_id = encoder_id;
   c->cmd_3d_id = cmd_3d_id;
   c->cmd_ta_id = cmd_ta_id;
   c->ppp_ctrl = 0x202;

   c->fragment_usc_base = dev->dev.shader_base;
   c->vertex_usc_base = c->fragment_usc_base;

   c->fb_width = cs->cr.width;
   c->fb_height = cs->cr.height;

   c->isp_bgobjdepth = cs->cr.isp_bgobjdepth;
   c->isp_bgobjvals = cs->cr.isp_bgobjvals;

   static_assert(sizeof(c->zls_ctrl) == sizeof(cs->cr.zls_control));
   memcpy(&c->zls_ctrl, &cs->cr.zls_control, sizeof(cs->cr.zls_control));

   c->depth_dimensions = (cs->cr.width - 1) | ((cs->cr.height - 1) << 15);

   c->depth_buffer_load = cs->cr.depth.buffer;
   c->depth_buffer_store = cs->cr.depth.buffer;
   c->depth_buffer_partial = cs->cr.depth.buffer;

   c->depth_buffer_load_stride = cs->cr.depth.stride;
   c->depth_buffer_store_stride = cs->cr.depth.stride;
   c->depth_buffer_partial_stride = cs->cr.depth.stride;

   c->depth_meta_buffer_load = cs->cr.depth.meta;
   c->depth_meta_buffer_store = cs->cr.depth.meta;
   c->depth_meta_buffer_partial = cs->cr.depth.meta;

   c->depth_meta_buffer_load_stride = cs->cr.depth.stride;
   c->depth_meta_buffer_store_stride = cs->cr.depth.meta_stride;
   c->depth_meta_buffer_partial_stride = cs->cr.depth.meta_stride;

   c->stencil_buffer_load = cs->cr.stencil.buffer;
   c->stencil_buffer_store = cs->cr.stencil.buffer;
   c->stencil_buffer_partial = cs->cr.stencil.buffer;

   c->stencil_buffer_load_stride = cs->cr.stencil.stride;
   c->stencil_buffer_store_stride = cs->cr.stencil.stride;
   c->stencil_buffer_partial_stride = cs->cr.stencil.stride;

   c->stencil_meta_buffer_load = cs->cr.stencil.meta;
   c->stencil_meta_buffer_store = cs->cr.stencil.meta;
   c->stencil_meta_buffer_partial = cs->cr.stencil.meta;

   c->stencil_meta_buffer_load_stride = cs->cr.stencil.stride;
   c->stencil_meta_buffer_store_stride = cs->cr.stencil.meta_stride;
   c->stencil_meta_buffer_partial_stride = cs->cr.stencil.meta_stride;

   c->iogpu_unk_214 = cs->cr.iogpu_unk_214;

   if (dev->dev.debug & AGX_DBG_NOCLUSTER) {
      c->flags |= ASAHI_RENDER_NO_VERTEX_CLUSTERING;
   } else {
      /* XXX: We don't know what this does exactly, and the name is
       * surely wrong. But it fixes dEQP-VK.memory.pipeline_barrier.* tests on
       * G14C when clustering is enabled...
       */
      c->flags |= ASAHI_RENDER_NO_CLEAR_PIPELINE_TEXTURES;
   }

#if 0
   /* XXX is this for just MSAA+Z+S or MSAA+(Z|S)? */
   if (tib->nr_samples > 1 && framebuffer->zsbuf)
      c->flags |= ASAHI_RENDER_MSAA_ZS;
#endif

   c->utile_width = cs->tib.tile_size.width;
   c->utile_height = cs->tib.tile_size.height;

   /* Can be 0 for attachmentless rendering with no draws */
   c->samples = MAX2(cs->tib.nr_samples, 1);
   c->layers = cs->cr.layers;

   c->ppp_multisamplectl = cs->ppp_multisamplectl;
   c->sample_size = cs->tib.sample_size_B;
   c->tib_blocks = ALIGN_POT(agx_tilebuffer_total_size(&cs->tib), 2048) / 2048;

   float tan_60 = 1.732051f;
   c->merge_upper_x = fui(tan_60 / cs->cr.width);
   c->merge_upper_y = fui(tan_60 / cs->cr.height);

   c->load_pipeline = cs->cr.bg.main.usc | 4;
   c->store_pipeline = cs->cr.eot.main.usc | 4;
   c->partial_reload_pipeline = cs->cr.bg.partial.usc | 4;
   c->partial_store_pipeline = cs->cr.eot.partial.usc | 4;

   memcpy(&c->load_pipeline_bind, &cs->cr.bg.main.counts,
          sizeof(struct agx_counts_packed));

   memcpy(&c->store_pipeline_bind, &cs->cr.eot.main.counts,
          sizeof(struct agx_counts_packed));

   memcpy(&c->partial_reload_pipeline_bind, &cs->cr.bg.partial.counts,
          sizeof(struct agx_counts_packed));

   memcpy(&c->partial_store_pipeline_bind, &cs->cr.eot.partial.counts,
          sizeof(struct agx_counts_packed));

   c->scissor_array = cs->uploaded_scissor;
   c->depth_bias_array = cs->uploaded_zbias;

   c->vertex_sampler_array = dev->samplers.table.bo->va->addr;
   c->vertex_sampler_count = dev->samplers.table.alloc;
   c->vertex_sampler_max = dev->samplers.table.alloc + 1;

   c->fragment_sampler_array = c->vertex_sampler_array;
   c->fragment_sampler_count = c->vertex_sampler_count;
   c->fragment_sampler_max = c->vertex_sampler_max;

   c->visibility_result_buffer = dev->occlusion_queries.bo->va->addr;

   if (cs->cr.process_empty_tiles)
      c->flags |= ASAHI_RENDER_PROCESS_EMPTY_TILES;

   if (cs->scratch.vs.main || cs->scratch.vs.preamble) {
      c->flags |= ASAHI_RENDER_VERTEX_SPILLS;
      c->vertex_helper_arg = dev->scratch.vs.buf->va->addr;
      c->vertex_helper_cfg = cs->scratch.vs.preamble ? (1 << 16) : 0;
      c->vertex_helper_program = dev->dev.helper->va->addr | 1;
   }

   if (cs->scratch.fs.main || cs->scratch.fs.preamble) {
      c->fragment_helper_arg = dev->scratch.fs.buf->va->addr;
      c->fragment_helper_cfg = cs->scratch.fs.preamble ? (1 << 16) : 0;
      c->fragment_helper_program = dev->dev.helper->va->addr | 1;
   }
}

static void
asahi_fill_sync(struct drm_asahi_sync *sync, struct vk_sync *vk_sync,
                uint64_t value)
{
   if (unlikely(!vk_sync_type_is_drm_syncobj(vk_sync->type))) {
      unreachable("Unsupported sync type");
      return;
   }

   const struct vk_drm_syncobj *syncobj = vk_sync_as_drm_syncobj(vk_sync);
   *sync = (struct drm_asahi_sync){.handle = syncobj->syncobj};

   if (vk_sync->flags & VK_SYNC_IS_TIMELINE) {
      sync->sync_type = DRM_ASAHI_SYNC_TIMELINE_SYNCOBJ;
      sync->timeline_value = value;
   } else {
      sync->sync_type = DRM_ASAHI_SYNC_SYNCOBJ;
   }
}

union drm_asahi_cmd {
   struct drm_asahi_cmd_compute compute;
   struct drm_asahi_cmd_render render;
};

/* XXX: Batching multiple commands per submission is causing rare (7ppm) flakes
 * on the CTS once lossless compression is enabled. This needs to be
 * investigated before we can reenable this mechanism. We are likely missing a
 * cache flush or barrier somewhere.
 */
static inline unsigned
max_commands_per_submit(struct hk_device *dev)
{
   return HK_PERF(dev, BATCH) ? 64 : 1;
}

static VkResult
queue_submit_single(struct agx_device *dev, struct drm_asahi_submit *submit)
{
   /* Currently we don't use the result buffer or implicit sync */
   struct agx_submit_virt virt = {
      .vbo_res_id = 0,
      .extres_count = 0,
   };

   int ret = dev->ops.submit(dev, submit, &virt);

   /* XXX: don't trap */
   if (ret) {
      fprintf(stderr, "DRM_IOCTL_ASAHI_SUBMIT failed: %m\n");
      assert(0);
   }

   return VK_SUCCESS;
}

/*
 * The kernel/firmware jointly impose a limit on commands per submit ioctl, but
 * we can build up arbitrarily large command buffers. We handle this here by
 * looping the ioctl, submitting slices of the command buffers that are within
 * bounds.
 */
static VkResult
queue_submit_looped(struct hk_device *dev, struct drm_asahi_submit *submit)
{
   struct drm_asahi_command *cmds = (void *)(uintptr_t)submit->commands;
   unsigned commands_remaining = submit->command_count;
   unsigned submitted[DRM_ASAHI_SUBQUEUE_COUNT] = {0};

   while (commands_remaining) {
      bool first = commands_remaining == submit->command_count;
      bool last = commands_remaining <= max_commands_per_submit(dev);

      unsigned count = MIN2(commands_remaining, max_commands_per_submit(dev));
      commands_remaining -= count;

      assert(!last || commands_remaining == 0);
      assert(count > 0);

      /* We need to fix up the barriers since barriers are ioctl-relative */
      for (unsigned i = 0; i < count; ++i) {
         for (unsigned q = 0; q < DRM_ASAHI_SUBQUEUE_COUNT; ++q) {
            if (cmds[i].barriers[q] != DRM_ASAHI_BARRIER_NONE) {
               assert(cmds[i].barriers[q] >= submitted[q]);
               cmds[i].barriers[q] -= submitted[q];
            }
         }
      }

      /* We can't signal the out-syncobjs until all prior work finishes. Since
       * only the last ioctl will signal, make sure it waits on prior ioctls.
       *
       * TODO: there might be a more performant way to do this.
       */
      if (last && !first) {
         for (unsigned q = 0; q < DRM_ASAHI_SUBQUEUE_COUNT; ++q) {
            if (cmds[0].barriers[q] == DRM_ASAHI_BARRIER_NONE)
               cmds[0].barriers[q] = 0;
         }
      }

      struct drm_asahi_submit submit_ioctl = {
         .flags = submit->flags,
         .queue_id = submit->queue_id,
         .result_handle = submit->result_handle,
         .commands = (uint64_t)(uintptr_t)(cmds),
         .command_count = count,
         .in_syncs = first ? submit->in_syncs : 0,
         .in_sync_count = first ? submit->in_sync_count : 0,
         .out_syncs = last ? submit->out_syncs : 0,
         .out_sync_count = last ? submit->out_sync_count : 0,
      };

      VkResult result = queue_submit_single(&dev->dev, &submit_ioctl);
      if (result != VK_SUCCESS)
         return result;

      for (unsigned i = 0; i < count; ++i) {
         if (cmds[i].cmd_type == DRM_ASAHI_CMD_COMPUTE)
            submitted[DRM_ASAHI_SUBQUEUE_COMPUTE]++;
         else if (cmds[i].cmd_type == DRM_ASAHI_CMD_RENDER)
            submitted[DRM_ASAHI_SUBQUEUE_RENDER]++;
         else
            unreachable("unknown subqueue");
      }

      cmds += count;
   }

   return VK_SUCCESS;
}

static VkResult
queue_submit(struct hk_device *dev, struct hk_queue *queue,
             struct vk_queue_submit *submit)
{
   unsigned command_count = 0;

   /* Gather the number of individual commands to submit up front */
   for (unsigned i = 0; i < submit->command_buffer_count; ++i) {
      struct hk_cmd_buffer *cmdbuf =
         (struct hk_cmd_buffer *)submit->command_buffers[i];

      command_count += list_length(&cmdbuf->control_streams);
   }

   perf_debug(dev, "Submitting %u control streams (%u command buffers)",
              command_count, submit->command_buffer_count);

   if (command_count == 0)
      return queue_submit_empty(dev, queue, submit);

   unsigned wait_count = 0;
   struct drm_asahi_sync *waits =
      alloca(submit->wait_count * sizeof(struct drm_asahi_sync));

   struct drm_asahi_sync *signals =
      alloca((submit->signal_count + 1) * sizeof(struct drm_asahi_sync));

   for (unsigned i = 0; i < submit->wait_count; ++i) {
      /* The kernel rejects the submission if we try to wait on the same
       * timeline semaphore at multiple points.
       *
       * TODO: Can we relax the UAPI?
       *
       * XXX: This is quadratic time.
       */
      bool skip = false;
      if (submit->waits[i].sync->flags & VK_SYNC_IS_TIMELINE) {
         uint32_t v1 = submit->waits[i].wait_value;
         for (unsigned j = 0; j < submit->wait_count; ++j) {
            uint32_t v2 = submit->waits[j].wait_value;
            if (i != j && submit->waits[i].sync == submit->waits[j].sync &&
                (v1 < v2 || (v1 == v2 && i < j))) {
               skip = true;
               break;
            }
         }

         if (skip)
            continue;
      }

      asahi_fill_sync(&waits[wait_count++], submit->waits[i].sync,
                      submit->waits[i].wait_value);
   }

   for (unsigned i = 0; i < submit->signal_count; ++i) {
      asahi_fill_sync(&signals[i], submit->signals[i].sync,
                      submit->signals[i].signal_value);
   }

   /* Signal progress on the queue itself */
   signals[submit->signal_count] = (struct drm_asahi_sync){
      .sync_type = DRM_ASAHI_SYNC_TIMELINE_SYNCOBJ,
      .handle = queue->drm.syncobj,
      .timeline_value = ++queue->drm.timeline_value,
   };

   /* Now setup the command structs */
   struct drm_asahi_command *cmds = alloca(sizeof(*cmds) * command_count);
   union drm_asahi_cmd *cmds_inner =
      alloca(sizeof(*cmds_inner) * command_count);

   unsigned cmd_it = 0;
   unsigned nr_vdm = 0, nr_cdm = 0;

   for (unsigned i = 0; i < submit->command_buffer_count; ++i) {
      struct hk_cmd_buffer *cmdbuf =
         (struct hk_cmd_buffer *)submit->command_buffers[i];

      list_for_each_entry(struct hk_cs, cs, &cmdbuf->control_streams, node) {
         assert(cmd_it < command_count);

         struct drm_asahi_command cmd = {
            .cmd_buffer = (uint64_t)(uintptr_t)&cmds_inner[cmd_it],
            .result_offset = 0 /* TODO */,
            .result_size = 0 /* TODO */,
            /* Barrier on previous command */
            .barriers = {nr_vdm, nr_cdm},
         };

         if (cs->type == HK_CS_CDM) {
            perf_debug(
               dev,
               "%u: Submitting CDM with %u API calls, %u dispatches, %u flushes",
               i, cs->stats.calls, cs->stats.cmds, cs->stats.flushes);

            assert(cs->stats.cmds > 0 || cs->stats.flushes > 0);

            cmd.cmd_type = DRM_ASAHI_CMD_COMPUTE;
            cmd.cmd_buffer_size = sizeof(struct drm_asahi_cmd_compute);
            nr_cdm++;

            asahi_fill_cdm_command(dev, cs, &cmds_inner[cmd_it].compute);
         } else {
            assert(cs->type == HK_CS_VDM);
            perf_debug(dev, "%u: Submitting VDM with %u API draws, %u draws", i,
                       cs->stats.calls, cs->stats.cmds);
            assert(cs->stats.cmds > 0 || cs->cr.process_empty_tiles);

            cmd.cmd_type = DRM_ASAHI_CMD_RENDER;
            cmd.cmd_buffer_size = sizeof(struct drm_asahi_cmd_render);
            nr_vdm++;

            asahi_fill_vdm_command(dev, cs, &cmds_inner[cmd_it].render);
         }

         cmds[cmd_it++] = cmd;
      }
   }

   assert(cmd_it == command_count);

   if (dev->dev.debug & AGX_DBG_TRACE) {
      for (unsigned i = 0; i < command_count; ++i) {
         if (cmds[i].cmd_type == DRM_ASAHI_CMD_COMPUTE) {
            agxdecode_drm_cmd_compute(dev->dev.agxdecode, &dev->dev.params,
                                      &cmds_inner[i].compute, true);
         } else {
            assert(cmds[i].cmd_type == DRM_ASAHI_CMD_RENDER);
            agxdecode_drm_cmd_render(dev->dev.agxdecode, &dev->dev.params,
                                     &cmds_inner[i].render, true);
         }
      }

      agxdecode_image_heap(dev->dev.agxdecode, dev->images.bo->va->addr,
                           dev->images.alloc);

      agxdecode_next_frame();
   }

   struct drm_asahi_submit submit_ioctl = {
      .flags = 0,
      .queue_id = queue->drm.id,
      .result_handle = 0 /* TODO */,
      .in_sync_count = wait_count,
      .out_sync_count = submit->signal_count + 1,
      .command_count = command_count,
      .in_syncs = (uint64_t)(uintptr_t)(waits),
      .out_syncs = (uint64_t)(uintptr_t)(signals),
      .commands = (uint64_t)(uintptr_t)(cmds),
   };

   if (command_count <= max_commands_per_submit(dev))
      return queue_submit_single(&dev->dev, &submit_ioctl);
   else
      return queue_submit_looped(dev, &submit_ioctl);
}

static VkResult
hk_queue_submit(struct vk_queue *vk_queue, struct vk_queue_submit *submit)
{
   struct hk_queue *queue = container_of(vk_queue, struct hk_queue, vk);
   struct hk_device *dev = hk_queue_device(queue);

   if (vk_queue_is_lost(&queue->vk))
      return VK_ERROR_DEVICE_LOST;

   VkResult result = queue_submit(dev, queue, submit);
   if (result != VK_SUCCESS)
      return vk_queue_set_lost(&queue->vk, "Submit failed");

   return VK_SUCCESS;
}

static uint32_t
translate_priority(enum VkQueueGlobalPriorityKHR prio)
{
   /* clang-format off */
   switch (prio) {
   case VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR: return 0;
   case VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR:     return 1;
   case VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR:   return 2;
   case VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR:      return 3;
   default: unreachable("Invalid VkQueueGlobalPriorityKHR");
   }
   /* clang-format on */
}

VkResult
hk_queue_init(struct hk_device *dev, struct hk_queue *queue,
              const VkDeviceQueueCreateInfo *pCreateInfo,
              uint32_t index_in_family)
{
   struct hk_physical_device *pdev = hk_device_physical(dev);
   VkResult result;

   assert(pCreateInfo->queueFamilyIndex < pdev->queue_family_count);

   const VkDeviceQueueGlobalPriorityCreateInfoKHR *priority_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR);
   const enum VkQueueGlobalPriorityKHR priority =
      priority_info ? priority_info->globalPriority
                    : VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR;

   result = vk_queue_init(&queue->vk, &dev->vk, pCreateInfo, index_in_family);
   if (result != VK_SUCCESS)
      return result;

   queue->vk.driver_submit = hk_queue_submit;

   queue->drm.id = agx_create_command_queue(&dev->dev,
                                            DRM_ASAHI_QUEUE_CAP_RENDER |
                                               DRM_ASAHI_QUEUE_CAP_BLIT |
                                               DRM_ASAHI_QUEUE_CAP_COMPUTE,
                                            translate_priority(priority));

   if (drmSyncobjCreate(dev->dev.fd, 0, &queue->drm.syncobj)) {
      mesa_loge("drmSyncobjCreate() failed %d\n", errno);
      agx_destroy_command_queue(&dev->dev, queue->drm.id);
      vk_queue_finish(&queue->vk);

      return vk_errorf(dev, VK_ERROR_OUT_OF_HOST_MEMORY,
                       "DRM_IOCTL_SYNCOBJ_CREATE failed: %m");
   }

   uint64_t initial_value = 1;
   if (drmSyncobjTimelineSignal(dev->dev.fd, &queue->drm.syncobj,
                                &initial_value, 1)) {
      hk_queue_finish(dev, queue);
      return vk_errorf(dev, VK_ERROR_OUT_OF_HOST_MEMORY,
                       "DRM_IOCTL_TIMELINE_SYNCOBJ_SIGNAL failed: %m");
   }

   return VK_SUCCESS;
}

void
hk_queue_finish(struct hk_device *dev, struct hk_queue *queue)
{
   drmSyncobjDestroy(dev->dev.fd, queue->drm.syncobj);
   agx_destroy_command_queue(&dev->dev, queue->drm.id);
   vk_queue_finish(&queue->vk);
}
