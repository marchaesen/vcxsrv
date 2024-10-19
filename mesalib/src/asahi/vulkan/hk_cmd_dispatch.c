/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "shaders/query.h"
#include "vulkan/vulkan_core.h"
#include "agx_helpers.h"
#include "agx_linker.h"
#include "agx_nir_lower_gs.h"
#include "agx_pack.h"
#include "agx_scratch.h"
#include "agx_tilebuffer.h"
#include "hk_buffer.h"
#include "hk_cmd_buffer.h"
#include "hk_descriptor_set.h"
#include "hk_device.h"
#include "hk_entrypoints.h"
#include "hk_physical_device.h"
#include "hk_shader.h"
#include "pool.h"

void
hk_cmd_buffer_begin_compute(struct hk_cmd_buffer *cmd,
                            const VkCommandBufferBeginInfo *pBeginInfo)
{
}

void
hk_cmd_invalidate_compute_state(struct hk_cmd_buffer *cmd)
{
   memset(&cmd->state.cs, 0, sizeof(cmd->state.cs));
}

void
hk_cmd_bind_compute_shader(struct hk_cmd_buffer *cmd,
                           struct hk_api_shader *shader)
{
   cmd->state.cs.shader = shader;
}

void
hk_cdm_cache_flush(struct hk_device *dev, struct hk_cs *cs)
{
   assert(cs->type == HK_CS_CDM);
   assert(cs->current + AGX_CDM_BARRIER_LENGTH < cs->end &&
          "caller must ensure space");

   uint8_t *out = cs->current;

   agx_push(out, CDM_BARRIER, cfg) {
      cfg.unk_5 = true;
      cfg.unk_6 = true;
      cfg.unk_8 = true;
      // cfg.unk_11 = true;
      // cfg.unk_20 = true;
      if (dev->dev.params.num_clusters_total > 1) {
         // cfg.unk_24 = true;
         if (dev->dev.params.gpu_generation == 13) {
            cfg.unk_4 = true;
            // cfg.unk_26 = true;
         }
      }

      /* With multiple launches in the same CDM stream, we can get cache
       * coherency (? or sync?) issues. We hit this with blits, which need - in
       * between dispatches - need the PBE cache to be flushed and the texture
       * cache to be invalidated. Until we know what bits mean what exactly,
       * let's just set these after every launch to be safe. We can revisit in
       * the future when we figure out what the bits mean.
       */
      cfg.unk_0 = true;
      cfg.unk_1 = true;
      cfg.unk_2 = true;
      cfg.usc_cache_inval = true;
      cfg.unk_4 = true;
      cfg.unk_5 = true;
      cfg.unk_6 = true;
      cfg.unk_7 = true;
      cfg.unk_8 = true;
      cfg.unk_9 = true;
      cfg.unk_10 = true;
      cfg.unk_11 = true;
      cfg.unk_12 = true;
      cfg.unk_13 = true;
      cfg.unk_14 = true;
      cfg.unk_15 = true;
      cfg.unk_16 = true;
      cfg.unk_17 = true;
      cfg.unk_18 = true;
      cfg.unk_19 = true;
   }

   cs->current = out;
   cs->stats.flushes++;
}

/*
 * Enqueue workgroups to a given CDM control stream with a given prepared USC
 * words. This does not interact with any global state, so it is suitable for
 * internal dispatches that do not save/restore state. That can be simpler /
 * lower overhead than vk_meta for special operations that logically operate
 * as graphics.
 */
void
hk_dispatch_with_usc(struct hk_device *dev, struct hk_cs *cs,
                     struct hk_shader *s, uint32_t usc, struct hk_grid grid,
                     struct hk_grid local_size)
{
   assert(cs->current + 0x2000 < cs->end && "should have ensured space");
   uint8_t *out = cs->current;

   cs->stats.cmds++;

   agx_push(out, CDM_LAUNCH_WORD_0, cfg) {
      if (grid.indirect)
         cfg.mode = AGX_CDM_MODE_INDIRECT_GLOBAL;
      else
         cfg.mode = AGX_CDM_MODE_DIRECT;

      /* For now, always bind the txf sampler and nothing else */
      cfg.sampler_state_register_count = 1;

      cfg.uniform_register_count = s->b.info.push_count;
      cfg.preshader_register_count = s->b.info.nr_preamble_gprs;
   }

   agx_push(out, CDM_LAUNCH_WORD_1, cfg) {
      cfg.pipeline = usc;
   }

   /* Added in G14X */
   if (dev->dev.params.gpu_generation >= 14 &&
       dev->dev.params.num_clusters_total > 1) {

      agx_push(out, CDM_UNK_G14X, cfg)
         ;
   }

   assert(!local_size.indirect);

   if (grid.indirect) {
      agx_push(out, CDM_INDIRECT, cfg) {
         cfg.address_hi = grid.ptr >> 32;
         cfg.address_lo = grid.ptr & BITFIELD64_MASK(32);
      }
   } else {
      agx_push(out, CDM_GLOBAL_SIZE, cfg) {
         cfg.x = grid.count[0];
         cfg.y = grid.count[1];
         cfg.z = grid.count[2];
      }
   }

   agx_push(out, CDM_LOCAL_SIZE, cfg) {
      cfg.x = local_size.count[0];
      cfg.y = local_size.count[1];
      cfg.z = local_size.count[2];
   }

   cs->current = out;
   hk_cdm_cache_flush(dev, cs);
}

static void
dispatch(struct hk_cmd_buffer *cmd, struct hk_grid grid)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   struct hk_shader *s = hk_only_variant(cmd->state.cs.shader);
   struct hk_cs *cs = hk_cmd_buffer_get_cs(cmd, true /* compute */);
   if (!cs)
      return;

   uint64_t stat = hk_pipeline_stat_addr(
      cmd, VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT);

   if (stat) {
      uint32_t local_size_threads = s->info.cs.local_size[0] *
                                    s->info.cs.local_size[1] *
                                    s->info.cs.local_size[2];

      struct libagx_cs_invocation_params p = {
         .grid = cmd->state.cs.descriptors.root.cs.group_count_addr,
         .local_size_threads = local_size_threads,
         .statistic = stat,
      };

      struct hk_shader *s =
         hk_meta_kernel(dev, agx_nir_increment_cs_invocations, NULL, 0);

      uint64_t params = hk_pool_upload(cmd, &p, sizeof(p), 8);
      uint32_t usc =
         hk_upload_usc_words_kernel(cmd, s, &params, sizeof(params));

      perf_debug(dev, "CS invocation statistic");
      hk_dispatch_with_usc(dev, cs, s, usc, hk_grid(1, 1, 1), hk_grid(1, 1, 1));
   }

   hk_ensure_cs_has_space(cmd, cs, 0x2000 /* TODO */);
   hk_dispatch(cmd, cs, s, grid);
   cs->stats.calls++;
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX,
                   uint32_t baseGroupY, uint32_t baseGroupZ,
                   uint32_t groupCountX, uint32_t groupCountY,
                   uint32_t groupCountZ)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   struct hk_descriptor_state *desc = &cmd->state.cs.descriptors;
   if (desc->push_dirty)
      hk_cmd_buffer_flush_push_descriptors(cmd, desc);

   desc->root.cs.base_group[0] = baseGroupX;
   desc->root.cs.base_group[1] = baseGroupY;
   desc->root.cs.base_group[2] = baseGroupZ;

   /* We don't want to key the shader to whether we're indirectly dispatching,
    * so treat everything as indirect.
    */
   VkDispatchIndirectCommand group_count = {
      .x = groupCountX,
      .y = groupCountY,
      .z = groupCountZ,
   };

   desc->root.cs.group_count_addr =
      hk_pool_upload(cmd, &group_count, sizeof(group_count), 8);

   dispatch(cmd, hk_grid(groupCountX, groupCountY, groupCountZ));
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                       VkDeviceSize offset)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(hk_buffer, buffer, _buffer);
   struct hk_descriptor_state *desc = &cmd->state.cs.descriptors;
   if (desc->push_dirty)
      hk_cmd_buffer_flush_push_descriptors(cmd, desc);

   desc->root.cs.base_group[0] = 0;
   desc->root.cs.base_group[1] = 0;
   desc->root.cs.base_group[2] = 0;

   uint64_t dispatch_addr = hk_buffer_address(buffer, offset);
   assert(dispatch_addr != 0);

   desc->root.cs.group_count_addr = dispatch_addr;
   dispatch(cmd, hk_grid_indirect(dispatch_addr));
}
