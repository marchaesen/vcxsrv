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
#include "pvr_srv_job_transfer.h"
#include "pvr_srv_sync.h"
#include "pvr_winsys.h"
#include "util/libsync.h"
#include "util/macros.h"
#include "vk_alloc.h"
#include "vk_log.h"
#include "vk_util.h"

#define PVR_SRV_TRANSFER_CONTEXT_INITIAL_CCB_SIZE_LOG2 16U
#define PVR_SRV_TRANSFER_CONTEXT_MAX_CCB_SIZE_LOG2 0U

struct pvr_srv_winsys_transfer_ctx {
   struct pvr_winsys_transfer_ctx base;

   void *handle;

   int timeline_3d;
};

#define to_pvr_srv_winsys_transfer_ctx(ctx) \
   container_of(ctx, struct pvr_srv_winsys_transfer_ctx, base)

VkResult pvr_srv_winsys_transfer_ctx_create(
   struct pvr_winsys *ws,
   const struct pvr_winsys_transfer_ctx_create_info *create_info,
   struct pvr_winsys_transfer_ctx **const ctx_out)
{
   struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(ws);
   struct pvr_srv_winsys_transfer_ctx *srv_ctx;
   struct rogue_fwif_rf_cmd reset_cmd = { 0 };
   VkResult result;

   /* First 2 U8s are 2d work load related, and the last 2 are 3d workload
    * related.
    */
   const uint32_t packed_ccb_size =
      PVR_U8888_TO_U32(PVR_SRV_TRANSFER_CONTEXT_INITIAL_CCB_SIZE_LOG2,
                       PVR_SRV_TRANSFER_CONTEXT_MAX_CCB_SIZE_LOG2,
                       PVR_SRV_TRANSFER_CONTEXT_INITIAL_CCB_SIZE_LOG2,
                       PVR_SRV_TRANSFER_CONTEXT_MAX_CCB_SIZE_LOG2);

   srv_ctx = vk_alloc(srv_ws->alloc,
                      sizeof(*srv_ctx),
                      8U,
                      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!srv_ctx)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = pvr_srv_create_timeline(srv_ws->render_fd, &srv_ctx->timeline_3d);
   if (result != VK_SUCCESS)
      goto err_free_srv_ctx;

   /* TODO: Add support for reset framework. Currently we subtract
    * reset_cmd.regs size from reset_cmd size to only pass empty flags field.
    */
   result = pvr_srv_rgx_create_transfer_context(
      srv_ws->render_fd,
      pvr_srv_from_winsys_priority(create_info->priority),
      sizeof(reset_cmd) - sizeof(reset_cmd.regs),
      (uint8_t *)&reset_cmd,
      srv_ws->server_memctx_data,
      packed_ccb_size,
      RGX_CONTEXT_FLAG_DISABLESLR,
      0U,
      NULL,
      NULL,
      &srv_ctx->handle);
   if (result != VK_SUCCESS)
      goto err_close_timeline;

   srv_ctx->base.ws = ws;
   *ctx_out = &srv_ctx->base;

   return VK_SUCCESS;

err_close_timeline:
   close(srv_ctx->timeline_3d);

err_free_srv_ctx:
   vk_free(srv_ws->alloc, srv_ctx);

   return result;
}

void pvr_srv_winsys_transfer_ctx_destroy(struct pvr_winsys_transfer_ctx *ctx)
{
   struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(ctx->ws);
   struct pvr_srv_winsys_transfer_ctx *srv_ctx =
      to_pvr_srv_winsys_transfer_ctx(ctx);

   pvr_srv_rgx_destroy_transfer_context(srv_ws->render_fd, srv_ctx->handle);
   close(srv_ctx->timeline_3d);
   vk_free(srv_ws->alloc, srv_ctx);
}

static void pvr_srv_transfer_cmds_init(
   const struct pvr_winsys_transfer_submit_info *submit_info,
   struct rogue_fwif_cmd_transfer *cmds,
   uint32_t cmd_count)
{
   memset(cmds, 0, sizeof(*cmds) * submit_info->cmd_count);

   for (uint32_t i = 0; i < cmd_count; i++) {
      struct rogue_fwif_transfer_regs *fw_regs = &cmds[i].regs;

      cmds[i].cmn.frame_num = submit_info->frame_num;

      fw_regs->isp_bgobjvals = submit_info->cmds[i].regs.isp_bgobjvals;
      fw_regs->usc_pixel_output_ctrl =
         submit_info->cmds[i].regs.usc_pixel_output_ctrl;
      fw_regs->usc_clear_register0 =
         submit_info->cmds[i].regs.usc_clear_register0;
      fw_regs->usc_clear_register1 =
         submit_info->cmds[i].regs.usc_clear_register1;
      fw_regs->usc_clear_register2 =
         submit_info->cmds[i].regs.usc_clear_register2;
      fw_regs->usc_clear_register3 =
         submit_info->cmds[i].regs.usc_clear_register3;
      fw_regs->isp_mtile_size = submit_info->cmds[i].regs.isp_mtile_size;
      fw_regs->isp_render_origin = submit_info->cmds[i].regs.isp_render_origin;
      fw_regs->isp_ctl = submit_info->cmds[i].regs.isp_ctl;
      fw_regs->isp_aa = submit_info->cmds[i].regs.isp_aa;
      fw_regs->event_pixel_pds_info =
         submit_info->cmds[i].regs.event_pixel_pds_info;
      fw_regs->event_pixel_pds_code =
         submit_info->cmds[i].regs.event_pixel_pds_code;
      fw_regs->event_pixel_pds_data =
         submit_info->cmds[i].regs.event_pixel_pds_data;
      fw_regs->isp_render = submit_info->cmds[i].regs.isp_render;
      fw_regs->isp_rgn = submit_info->cmds[i].regs.isp_rgn;
      fw_regs->pds_bgnd0_base = submit_info->cmds[i].regs.pds_bgnd0_base;
      fw_regs->pds_bgnd1_base = submit_info->cmds[i].regs.pds_bgnd1_base;
      fw_regs->pds_bgnd3_sizeinfo =
         submit_info->cmds[i].regs.pds_bgnd3_sizeinfo;
      fw_regs->isp_mtile_base = submit_info->cmds[i].regs.isp_mtile_base;

      STATIC_ASSERT(ARRAY_SIZE(fw_regs->pbe_wordx_mrty) ==
                    ARRAY_SIZE(submit_info->cmds[i].regs.pbe_wordx_mrty));
      for (uint32_t j = 0; j < ARRAY_SIZE(fw_regs->pbe_wordx_mrty); j++) {
         fw_regs->pbe_wordx_mrty[j] =
            submit_info->cmds[i].regs.pbe_wordx_mrty[j];
      }
   }
}

VkResult pvr_srv_winsys_transfer_submit(
   const struct pvr_winsys_transfer_ctx *ctx,
   const struct pvr_winsys_transfer_submit_info *submit_info,
   struct vk_sync *signal_sync)
{
   const struct pvr_srv_winsys_transfer_ctx *srv_ctx =
      to_pvr_srv_winsys_transfer_ctx(ctx);
   const struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(ctx->ws);

   struct rogue_fwif_cmd_transfer
      *cmds_ptr_arr[PVR_TRANSFER_MAX_PREPARES_PER_SUBMIT];
   uint32_t *update_sync_offsets[PVR_TRANSFER_MAX_PREPARES_PER_SUBMIT] = { 0 };
   uint32_t client_update_count[PVR_TRANSFER_MAX_PREPARES_PER_SUBMIT] = { 0 };
   void **update_ufo_syc_prims[PVR_TRANSFER_MAX_PREPARES_PER_SUBMIT] = { 0 };
   uint32_t *update_values[PVR_TRANSFER_MAX_PREPARES_PER_SUBMIT] = { 0 };
   uint32_t cmd_sizes[PVR_TRANSFER_MAX_PREPARES_PER_SUBMIT];
   uint32_t cmd_flags[PVR_TRANSFER_MAX_PREPARES_PER_SUBMIT];

   struct pvr_srv_sync *srv_signal_sync;
   uint32_t job_num;
   VkResult result;
   int in_fd = -1;
   int fence;

   STACK_ARRAY(struct rogue_fwif_cmd_transfer,
               transfer_cmds,
               submit_info->cmd_count);
   if (!transfer_cmds)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   pvr_srv_transfer_cmds_init(submit_info,
                              transfer_cmds,
                              submit_info->cmd_count);

   for (uint32_t i = 0U; i < submit_info->cmd_count; i++) {
      cmd_sizes[i] = sizeof(**cmds_ptr_arr);

      cmd_flags[i] = 0;
      if (submit_info->cmds[i].flags & PVR_WINSYS_TRANSFER_FLAG_START)
         cmd_flags[i] |= PVR_TRANSFER_PREP_FLAGS_START;

      if (submit_info->cmds[i].flags & PVR_WINSYS_TRANSFER_FLAG_END)
         cmd_flags[i] |= PVR_TRANSFER_PREP_FLAGS_END;

      cmds_ptr_arr[i] = &transfer_cmds[i];
   }

   for (uint32_t i = 0U; i < submit_info->wait_count; i++) {
      struct pvr_srv_sync *srv_wait_sync = to_srv_sync(submit_info->waits[i]);
      int ret;

      if (!submit_info->waits[i] || srv_wait_sync->fd < 0)
         continue;

      if (submit_info->stage_flags[i] & PVR_PIPELINE_STAGE_TRANSFER_BIT) {
         ret = sync_accumulate("", &in_fd, srv_wait_sync->fd);
         if (ret) {
            result = vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);
            goto end_close_in_fd;
         }

         submit_info->stage_flags[i] &= ~PVR_PIPELINE_STAGE_TRANSFER_BIT;
      }
   }

   job_num = submit_info->job_num;

   do {
      result = pvr_srv_rgx_submit_transfer2(srv_ws->render_fd,
                                            srv_ctx->handle,
                                            submit_info->cmd_count,
                                            client_update_count,
                                            update_ufo_syc_prims,
                                            update_sync_offsets,
                                            update_values,
                                            in_fd,
                                            -1,
                                            srv_ctx->timeline_3d,
                                            "TRANSFER",
                                            cmd_sizes,
                                            (uint8_t **)cmds_ptr_arr,
                                            cmd_flags,
                                            job_num,
                                            /* TODO: Add sync PMR support. */
                                            0U,
                                            NULL,
                                            NULL,
                                            NULL,
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

   STACK_ARRAY_FINISH(transfer_cmds);

   return result;
}
