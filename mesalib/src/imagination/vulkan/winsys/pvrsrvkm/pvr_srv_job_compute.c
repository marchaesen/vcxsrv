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

#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include "fw-api/pvr_rogue_fwif.h"
#include "fw-api/pvr_rogue_fwif_rf.h"
#include "pvr_private.h"
#include "pvr_srv.h"
#include "pvr_srv_bridge.h"
#include "pvr_srv_job_common.h"
#include "pvr_srv_job_compute.h"
#include "pvr_srv_sync.h"
#include "pvr_winsys.h"
#include "util/libsync.h"
#include "util/macros.h"
#include "vk_alloc.h"
#include "vk_log.h"

struct pvr_srv_winsys_compute_ctx {
   struct pvr_winsys_compute_ctx base;

   void *handle;

   int timeline;
};

#define to_pvr_srv_winsys_compute_ctx(ctx) \
   container_of(ctx, struct pvr_srv_winsys_compute_ctx, base)

VkResult pvr_srv_winsys_compute_ctx_create(
   struct pvr_winsys *ws,
   const struct pvr_winsys_compute_ctx_create_info *create_info,
   struct pvr_winsys_compute_ctx **const ctx_out)
{
   struct rogue_fwif_static_computecontext_state static_state = {
		.ctx_switch_regs = {
			.cdm_context_pds0 = create_info->static_state.cdm_ctx_store_pds0,
			.cdm_context_pds0_b =
				create_info->static_state.cdm_ctx_store_pds0_b,
			.cdm_context_pds1 = create_info->static_state.cdm_ctx_store_pds1,

			.cdm_terminate_pds = create_info->static_state.cdm_ctx_terminate_pds,
			.cdm_terminate_pds1 =
				create_info->static_state.cdm_ctx_terminate_pds1,

			.cdm_resume_pds0 = create_info->static_state.cdm_ctx_resume_pds0,
			.cdm_resume_pds0_b = create_info->static_state.cdm_ctx_resume_pds0_b,
		},
	};

   struct rogue_fwif_rf_cmd reset_cmd = { 0 };

   struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(ws);
   struct pvr_srv_winsys_compute_ctx *srv_ctx;
   VkResult result;

   srv_ctx = vk_alloc(srv_ws->alloc,
                      sizeof(*srv_ctx),
                      8U,
                      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!srv_ctx)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = pvr_srv_create_timeline(srv_ws->render_fd, &srv_ctx->timeline);
   if (result != VK_SUCCESS)
      goto err_free_srv_ctx;

   /* TODO: Add support for reset framework. Currently we subtract
    * reset_cmd.regs size from reset_cmd size to only pass empty flags field.
    */
   result = pvr_srv_rgx_create_compute_context(
      srv_ws->render_fd,
      pvr_srv_from_winsys_priority(create_info->priority),
      sizeof(reset_cmd) - sizeof(reset_cmd.regs),
      (uint8_t *)&reset_cmd,
      srv_ws->server_memctx_data,
      sizeof(static_state),
      (uint8_t *)&static_state,
      0U,
      RGX_CONTEXT_FLAG_DISABLESLR,
      0U,
      UINT_MAX,
      &srv_ctx->handle);
   if (result != VK_SUCCESS)
      goto err_close_timeline;

   srv_ctx->base.ws = ws;

   *ctx_out = &srv_ctx->base;

   return VK_SUCCESS;

err_close_timeline:
   close(srv_ctx->timeline);

err_free_srv_ctx:
   vk_free(srv_ws->alloc, srv_ctx);

   return result;
}

void pvr_srv_winsys_compute_ctx_destroy(struct pvr_winsys_compute_ctx *ctx)
{
   struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(ctx->ws);
   struct pvr_srv_winsys_compute_ctx *srv_ctx =
      to_pvr_srv_winsys_compute_ctx(ctx);

   pvr_srv_rgx_destroy_compute_context(srv_ws->render_fd, srv_ctx->handle);
   close(srv_ctx->timeline);
   vk_free(srv_ws->alloc, srv_ctx);
}

static void pvr_srv_compute_cmd_init(
   const struct pvr_winsys_compute_submit_info *submit_info,
   struct rogue_fwif_cmd_compute *cmd)
{
   struct rogue_fwif_cdm_regs *fw_regs = &cmd->regs;

   memset(cmd, 0, sizeof(*cmd));

   cmd->cmn.frame_num = submit_info->frame_num;

   fw_regs->tpu_border_colour_table = submit_info->regs.tpu_border_colour_table;
   fw_regs->cdm_item = submit_info->regs.cdm_item;
   fw_regs->compute_cluster = submit_info->regs.compute_cluster;
   fw_regs->cdm_ctrl_stream_base = submit_info->regs.cdm_ctrl_stream_base;
   fw_regs->cdm_context_state_base_addr =
      submit_info->regs.cdm_ctx_state_base_addr;
   fw_regs->tpu = submit_info->regs.tpu;
   fw_regs->cdm_resume_pds1 = submit_info->regs.cdm_resume_pds1;

   if (submit_info->flags & PVR_WINSYS_COMPUTE_FLAG_PREVENT_ALL_OVERLAP)
      cmd->flags |= ROGUE_FWIF_COMPUTE_FLAG_PREVENT_ALL_OVERLAP;

   if (submit_info->flags & PVR_WINSYS_COMPUTE_FLAG_SINGLE_CORE)
      cmd->flags |= ROGUE_FWIF_COMPUTE_FLAG_SINGLE_CORE;
}

VkResult pvr_srv_winsys_compute_submit(
   const struct pvr_winsys_compute_ctx *ctx,
   const struct pvr_winsys_compute_submit_info *submit_info,
   struct vk_sync *signal_sync)
{
   const struct pvr_srv_winsys_compute_ctx *srv_ctx =
      to_pvr_srv_winsys_compute_ctx(ctx);
   const struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(ctx->ws);
   struct rogue_fwif_cmd_compute compute_cmd;
   struct pvr_srv_sync *srv_signal_sync;
   VkResult result;
   int in_fd = -1;
   int fence;

   pvr_srv_compute_cmd_init(submit_info, &compute_cmd);

   for (uint32_t i = 0U; i < submit_info->wait_count; i++) {
      struct pvr_srv_sync *srv_wait_sync = to_srv_sync(submit_info->waits[i]);
      int ret;

      if (!submit_info->waits[i] || srv_wait_sync->fd < 0)
         continue;

      if (submit_info->stage_flags[i] & PVR_PIPELINE_STAGE_COMPUTE_BIT) {
         ret = sync_accumulate("", &in_fd, srv_wait_sync->fd);
         if (ret) {
            result = vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);
            goto end_close_in_fd;
         }

         submit_info->stage_flags[i] &= ~PVR_PIPELINE_STAGE_COMPUTE_BIT;
      }
   }

   do {
      result = pvr_srv_rgx_kick_compute2(srv_ws->render_fd,
                                         srv_ctx->handle,
                                         0U,
                                         NULL,
                                         NULL,
                                         NULL,
                                         in_fd,
                                         srv_ctx->timeline,
                                         sizeof(compute_cmd),
                                         (uint8_t *)&compute_cmd,
                                         submit_info->job_num,
                                         0,
                                         NULL,
                                         NULL,
                                         0U,
                                         0U,
                                         0U,
                                         0U,
                                         "COMPUTE",
                                         &fence);
   } while (result == VK_NOT_READY);

   if (result != VK_SUCCESS)
      goto end_close_in_fd;

   if (signal_sync) {
      srv_signal_sync = to_srv_sync(signal_sync);
      pvr_srv_set_sync_payload(srv_signal_sync, fence);
   } else if (fence != -1) {
      close(fence);
   }

end_close_in_fd:
   if (in_fd >= 0)
      close(in_fd);

   return result;
}
