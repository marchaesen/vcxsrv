/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2024 Arm Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_buffer.h"
#include "panvk_cmd_dispatch.h"

void
panvk_per_arch(cmd_prepare_dispatch_sysvals)(
   struct panvk_cmd_buffer *cmdbuf, const struct panvk_dispatch_info *info)
{
   const struct panvk_shader *shader = cmdbuf->state.compute.shader;
   const struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);

   BITSET_DECLARE(dirty_sysvals, MAX_SYSVAL_FAUS) = {0};

   /* In indirect case, some sysvals are read from the indirect dispatch
    * buffer.
    */
   if (info->indirect.buffer_dev_addr == 0) {
      set_compute_sysval(cmdbuf, dirty_sysvals, num_work_groups.x,
                         info->direct.wg_count.x);
      set_compute_sysval(cmdbuf, dirty_sysvals, num_work_groups.y,
                         info->direct.wg_count.y);
      set_compute_sysval(cmdbuf, dirty_sysvals, num_work_groups.z,
                         info->direct.wg_count.z);
   } else {
      BITSET_SET_RANGE(dirty_sysvals,
                       sysval_fau_start(compute, num_work_groups),
                       sysval_fau_end(compute, num_work_groups));
   }

   set_compute_sysval(cmdbuf, dirty_sysvals, base.x, info->wg_base.x);
   set_compute_sysval(cmdbuf, dirty_sysvals, base.y, info->wg_base.y);
   set_compute_sysval(cmdbuf, dirty_sysvals, base.z, info->wg_base.z);
   set_compute_sysval(cmdbuf, dirty_sysvals, local_group_size.x,
                      shader->local_size.x);
   set_compute_sysval(cmdbuf, dirty_sysvals, local_group_size.y,
                      shader->local_size.y);
   set_compute_sysval(cmdbuf, dirty_sysvals, local_group_size.z,
                      shader->local_size.z);
   set_compute_sysval(cmdbuf, dirty_sysvals, printf_buffer_address,
                      dev->printf.bo->addr.dev);

#if PAN_ARCH <= 7
   struct panvk_descriptor_state *desc_state =
      &cmdbuf->state.compute.desc_state;
   struct panvk_shader_desc_state *cs_desc_state =
      &cmdbuf->state.compute.cs.desc;

   if (compute_state_dirty(cmdbuf, CS) ||
       compute_state_dirty(cmdbuf, DESC_STATE)) {
      set_compute_sysval(cmdbuf, dirty_sysvals,
                         desc.sets[PANVK_DESC_TABLE_CS_DYN_SSBOS],
                         cs_desc_state->dyn_ssbos);
   }

   for (uint32_t i = 0; i < MAX_SETS; i++) {
      if (shader->desc_info.used_set_mask & BITFIELD_BIT(i)) {
         set_compute_sysval(cmdbuf, dirty_sysvals, desc.sets[i],
                            desc_state->sets[i]->descs.dev);
      }
   }
#endif

   /* Dirty push_uniforms if the used_sysvals/dirty_sysvals overlap. */
   BITSET_AND(dirty_sysvals, dirty_sysvals, shader->fau.used_sysvals);
   if (!BITSET_IS_EMPTY(dirty_sysvals))
      compute_state_set_dirty(cmdbuf, PUSH_UNIFORMS);
}
