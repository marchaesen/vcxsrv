/*
 * Copyright © 2024 Collabora Ltd.
 *
 * Derived from tu_cmd_buffer.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"

#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_desc_state.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_macros.h"
#include "panvk_meta.h"
#include "panvk_physical_device.h"

#include "pan_desc.h"
#include "pan_encoder.h"
#include "pan_jc.h"
#include "pan_props.h"

#include <vulkan/vulkan_core.h>

uint64_t
panvk_per_arch(cmd_dispatch_prepare_tls)(struct panvk_cmd_buffer *cmdbuf,
                                         const struct panvk_shader *shader,
                                         const struct pan_compute_dim *dim,
                                         bool indirect)
{
   struct panvk_batch *batch = cmdbuf->cur_batch;

   assert(batch);
   assert(!indirect && "Indirect not supported yet!");

   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(cmdbuf->vk.base.device->physical);

   panvk_per_arch(cmd_alloc_tls_desc)(cmdbuf, false);

   batch->tlsinfo.tls.size = shader->info.tls_size;
   batch->tlsinfo.wls.size = shader->info.wls_size;
   if (batch->tlsinfo.wls.size) {
      unsigned core_id_range;

      panfrost_query_core_count(&phys_dev->kmod.props, &core_id_range);
      batch->tlsinfo.wls.instances = pan_wls_instances(dim);
      batch->wls_total_size = pan_wls_adjust_size(batch->tlsinfo.wls.size) *
                              batch->tlsinfo.wls.instances * core_id_range;
   }

   return batch->tls.gpu;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDispatchBase)(VkCommandBuffer commandBuffer,
                                uint32_t baseGroupX, uint32_t baseGroupY,
                                uint32_t baseGroupZ, uint32_t groupCountX,
                                uint32_t groupCountY, uint32_t groupCountZ)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   const struct panvk_shader *shader = cmdbuf->state.compute.shader;
   VkResult result;

   if (groupCountX == 0 || groupCountY == 0 || groupCountZ == 0)
      return;

   /* If there's no compute shader, we can skip the dispatch. */
   if (!panvk_priv_mem_dev_addr(shader->rsd))
      return;

   struct panvk_dispatch_info info = {
      .wg_base = {baseGroupX, baseGroupY, baseGroupZ},
      .direct.wg_count = {groupCountX, groupCountY, groupCountZ},
   };
   struct pan_compute_dim wg_count = {groupCountX, groupCountY, groupCountZ};

   panvk_per_arch(cmd_close_batch)(cmdbuf);
   struct panvk_batch *batch = panvk_per_arch(cmd_open_batch)(cmdbuf);

   struct panvk_descriptor_state *desc_state =
      &cmdbuf->state.compute.desc_state;
   struct panvk_shader_desc_state *cs_desc_state =
      &cmdbuf->state.compute.cs.desc;

   uint64_t tsd = panvk_per_arch(cmd_dispatch_prepare_tls)(
      cmdbuf, shader, &wg_count, false);

   result = panvk_per_arch(cmd_prepare_push_descs)(
      cmdbuf, desc_state, shader->desc_info.used_set_mask);
   if (result != VK_SUCCESS)
      return;

   if (compute_state_dirty(cmdbuf, CS) ||
       compute_state_dirty(cmdbuf, DESC_STATE)) {
      result = panvk_per_arch(cmd_prepare_dyn_ssbos)(cmdbuf, desc_state, shader,
                                                     cs_desc_state);
      if (result != VK_SUCCESS)
         return;
   }

   panvk_per_arch(cmd_prepare_dispatch_sysvals)(cmdbuf, &info);

   result = panvk_per_arch(cmd_prepare_push_uniforms)(
      cmdbuf, cmdbuf->state.compute.shader);
   if (result != VK_SUCCESS)
      return;

   struct panfrost_ptr copy_desc_job = {0};

   if (compute_state_dirty(cmdbuf, CS) ||
       compute_state_dirty(cmdbuf, DESC_STATE)) {
      result = panvk_per_arch(cmd_prepare_shader_desc_tables)(
         cmdbuf, desc_state, shader, cs_desc_state);

      result = panvk_per_arch(meta_get_copy_desc_job)(
         cmdbuf, shader, &cmdbuf->state.compute.desc_state, cs_desc_state, 0,
         &copy_desc_job);
      if (result != VK_SUCCESS)
         return;

      if (copy_desc_job.cpu)
         util_dynarray_append(&batch->jobs, void *, copy_desc_job.cpu);
   }

   struct panfrost_ptr job = panvk_cmd_alloc_desc(cmdbuf, COMPUTE_JOB);
   if (!job.gpu)
      return;

   util_dynarray_append(&batch->jobs, void *, job.cpu);

   panfrost_pack_work_groups_compute(
      pan_section_ptr(job.cpu, COMPUTE_JOB, INVOCATION), wg_count.x, wg_count.y,
      wg_count.z, shader->local_size.x, shader->local_size.y,
      shader->local_size.z, false, false);

   pan_section_pack(job.cpu, COMPUTE_JOB, PARAMETERS, cfg) {
      cfg.job_task_split = util_logbase2_ceil(shader->local_size.x + 1) +
                           util_logbase2_ceil(shader->local_size.y + 1) +
                           util_logbase2_ceil(shader->local_size.z + 1);
   }

   pan_section_pack(job.cpu, COMPUTE_JOB, DRAW, cfg) {
      cfg.state = panvk_priv_mem_dev_addr(shader->rsd);
      cfg.attributes = cs_desc_state->img_attrib_table;
      cfg.attribute_buffers =
         cs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_IMG];
      cfg.thread_storage = tsd;
      cfg.uniform_buffers = cs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_UBO];
      cfg.push_uniforms = cmdbuf->state.compute.push_uniforms;
      cfg.textures = cs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_TEXTURE];
      cfg.samplers = cs_desc_state->tables[PANVK_BIFROST_DESC_TABLE_SAMPLER];
   }

   unsigned copy_desc_dep =
      copy_desc_job.gpu
         ? pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, false, false,
                          0, 0, &copy_desc_job, false)
         : 0;

   pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_COMPUTE, false, false, 0,
                  copy_desc_dep, &job, false);

   panvk_per_arch(cmd_close_batch)(cmdbuf);
   clear_dirty_after_dispatch(cmdbuf);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDispatchIndirect)(VkCommandBuffer commandBuffer,
                                    VkBuffer _buffer, VkDeviceSize offset)
{
   panvk_stub();
}
