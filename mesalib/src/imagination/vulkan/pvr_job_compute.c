/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "pvr_csb.h"
#include "pvr_job_common.h"
#include "pvr_job_context.h"
#include "pvr_job_compute.h"
#include "pvr_private.h"
#include "pvr_winsys.h"
#include "util/macros.h"

static void pvr_compute_job_ws_submit_info_init(
   struct pvr_compute_ctx *ctx,
   struct pvr_sub_cmd_compute *sub_cmd,
   struct vk_sync **waits,
   uint32_t wait_count,
   uint32_t *stage_flags,
   struct pvr_winsys_compute_submit_info *submit_info)
{
   const struct pvr_compute_ctx_switch *const ctx_switch = &ctx->ctx_switch;
   uint32_t shared_regs = sub_cmd->num_shared_regs;

   submit_info->frame_num = ctx->device->global_queue_present_count;
   submit_info->job_num = ctx->device->global_queue_job_count;

   submit_info->waits = waits;
   submit_info->wait_count = wait_count;
   submit_info->stage_flags = stage_flags;

   pvr_csb_pack (&submit_info->regs.cdm_ctx_state_base_addr,
                 CR_CDM_CONTEXT_STATE_BASE,
                 state) {
      state.addr = ctx_switch->compute_state_bo->vma->dev_addr;
   }

   /* Other registers are initialized in pvr_sub_cmd_compute_job_init(). */
   pvr_csb_pack (&submit_info->regs.cdm_resume_pds1,
                 CR_CDM_CONTEXT_PDS1,
                 state) {
      /* Convert the data size from dwords to bytes. */
      const uint32_t load_program_data_size =
         ctx_switch->sr[0].pds.load_program.data_size * 4U;

      state.pds_seq_dep = false;
      state.usc_seq_dep = false;
      state.target = false;
      state.unified_size = ctx_switch->sr[0].usc.unified_size;
      state.common_shared = true;
      state.common_size =
         DIV_ROUND_UP(shared_regs << 2,
                      PVRX(CR_CDM_CONTEXT_PDS1_COMMON_SIZE_UNIT_SIZE));
      state.temp_size = 0;

      assert(load_program_data_size %
                PVRX(CR_CDM_CONTEXT_PDS1_DATA_SIZE_UNIT_SIZE) ==
             0);
      state.data_size =
         load_program_data_size / PVRX(CR_CDM_CONTEXT_PDS1_DATA_SIZE_UNIT_SIZE);
      state.fence = false;
   }
}

VkResult pvr_compute_job_submit(struct pvr_compute_ctx *ctx,
                                struct pvr_sub_cmd_compute *sub_cmd,
                                struct vk_sync **waits,
                                uint32_t wait_count,
                                uint32_t *stage_flags,
                                struct vk_sync *signal_sync)
{
   struct pvr_device *device = ctx->device;

   pvr_compute_job_ws_submit_info_init(ctx,
                                       sub_cmd,
                                       waits,
                                       wait_count,
                                       stage_flags,
                                       &sub_cmd->submit_info);

   return device->ws->ops->compute_submit(ctx->ws_ctx,
                                          &sub_cmd->submit_info,
                                          signal_sync);
}
