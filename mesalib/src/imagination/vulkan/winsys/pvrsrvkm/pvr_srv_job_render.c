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
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include "fw-api/pvr_rogue_fwif.h"
#include "fw-api/pvr_rogue_fwif_rf.h"
#include "pvr_private.h"
#include "pvr_srv.h"
#include "pvr_srv_bo.h"
#include "pvr_srv_bridge.h"
#include "pvr_srv_job_common.h"
#include "pvr_srv_job_render.h"
#include "pvr_srv_sync.h"
#include "pvr_types.h"
#include "pvr_winsys.h"
#include "util/libsync.h"
#include "util/log.h"
#include "util/macros.h"
#include "vk_alloc.h"
#include "vk_log.h"
#include "vk_util.h"

struct pvr_srv_winsys_free_list {
   struct pvr_winsys_free_list base;

   void *handle;

   struct pvr_srv_winsys_free_list *parent;
};

#define to_pvr_srv_winsys_free_list(free_list) \
   container_of(free_list, struct pvr_srv_winsys_free_list, base)

struct pvr_srv_winsys_rt_dataset {
   struct pvr_winsys_rt_dataset base;

   struct {
      void *handle;
      struct pvr_srv_sync_prim *sync_prim;
   } rt_datas[ROGUE_FWIF_NUM_RTDATAS];
};

#define to_pvr_srv_winsys_rt_dataset(rt_dataset) \
   container_of(rt_dataset, struct pvr_srv_winsys_rt_dataset, base)

struct pvr_srv_winsys_render_ctx {
   struct pvr_winsys_render_ctx base;

   /* Handle to kernel context. */
   void *handle;

   int timeline_geom;
   int timeline_frag;
};

#define to_pvr_srv_winsys_render_ctx(ctx) \
   container_of(ctx, struct pvr_srv_winsys_render_ctx, base)

VkResult pvr_srv_winsys_free_list_create(
   struct pvr_winsys *ws,
   struct pvr_winsys_vma *free_list_vma,
   uint32_t initial_num_pages,
   uint32_t max_num_pages,
   uint32_t grow_num_pages,
   uint32_t grow_threshold,
   struct pvr_winsys_free_list *parent_free_list,
   struct pvr_winsys_free_list **const free_list_out)
{
   struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(ws);
   struct pvr_srv_winsys_bo *srv_free_list_bo =
      to_pvr_srv_winsys_bo(free_list_vma->bo);
   struct pvr_srv_winsys_free_list *srv_free_list;
   void *parent_handle;
   VkResult result;

   srv_free_list = vk_zalloc(srv_ws->alloc,
                             sizeof(*srv_free_list),
                             8,
                             VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!srv_free_list)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (parent_free_list) {
      srv_free_list->parent = to_pvr_srv_winsys_free_list(parent_free_list);
      parent_handle = srv_free_list->parent->handle;
   } else {
      srv_free_list->parent = NULL;
      parent_handle = NULL;
   }

   result = pvr_srv_rgx_create_free_list(srv_ws->render_fd,
                                         srv_ws->server_memctx_data,
                                         max_num_pages,
                                         initial_num_pages,
                                         grow_num_pages,
                                         grow_threshold,
                                         parent_handle,
#if defined(DEBUG)
                                         PVR_SRV_TRUE /* free_list_check */,
#else
                                         PVR_SRV_FALSE /* free_list_check */,
#endif
                                         free_list_vma->dev_addr,
                                         srv_free_list_bo->pmr,
                                         0 /* pmr_offset */,
                                         &srv_free_list->handle);
   if (result != VK_SUCCESS)
      goto err_vk_free_srv_free_list;

   srv_free_list->base.ws = ws;

   *free_list_out = &srv_free_list->base;

   return VK_SUCCESS;

err_vk_free_srv_free_list:
   vk_free(srv_ws->alloc, srv_free_list);

   return result;
}

void pvr_srv_winsys_free_list_destroy(struct pvr_winsys_free_list *free_list)
{
   struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(free_list->ws);
   struct pvr_srv_winsys_free_list *srv_free_list =
      to_pvr_srv_winsys_free_list(free_list);

   pvr_srv_rgx_destroy_free_list(srv_ws->render_fd, srv_free_list->handle);
   vk_free(srv_ws->alloc, srv_free_list);
}

VkResult pvr_srv_render_target_dataset_create(
   struct pvr_winsys *ws,
   const struct pvr_winsys_rt_dataset_create_info *create_info,
   struct pvr_winsys_rt_dataset **const rt_dataset_out)
{
   const pvr_dev_addr_t macrotile_addrs[ROGUE_FWIF_NUM_RTDATAS] = {
      [0] = create_info->rt_datas[0].macrotile_array_dev_addr,
      [1] = create_info->rt_datas[1].macrotile_array_dev_addr,
   };
   const pvr_dev_addr_t pm_mlist_addrs[ROGUE_FWIF_NUM_RTDATAS] = {
      [0] = create_info->rt_datas[0].pm_mlist_dev_addr,
      [1] = create_info->rt_datas[1].pm_mlist_dev_addr,
   };
   const pvr_dev_addr_t rgn_header_addrs[ROGUE_FWIF_NUM_RTDATAS] = {
      [0] = create_info->rt_datas[0].rgn_header_dev_addr,
      [1] = create_info->rt_datas[1].rgn_header_dev_addr,
   };

   struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(ws);
   struct pvr_srv_winsys_free_list *srv_local_free_list =
      to_pvr_srv_winsys_free_list(create_info->local_free_list);
   void *free_lists[ROGUE_FW_MAX_FREELISTS] = { NULL };
   struct pvr_srv_winsys_rt_dataset *srv_rt_dataset;
   void *handles[ROGUE_FWIF_NUM_RTDATAS];
   VkResult result;

   free_lists[ROGUE_FW_LOCAL_FREELIST] = srv_local_free_list->handle;

   if (srv_local_free_list->parent) {
      free_lists[ROGUE_FW_GLOBAL_FREELIST] =
         srv_local_free_list->parent->handle;
   }

   srv_rt_dataset = vk_zalloc(srv_ws->alloc,
                              sizeof(*srv_rt_dataset),
                              8,
                              VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!srv_rt_dataset)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* If greater than 1 we'll have to pass in an array. For now just passing in
    * the reference.
    */
   STATIC_ASSERT(ROGUE_FWIF_NUM_GEOMDATAS == 1);
   /* If not 2 the arrays used in the bridge call will require updating. */
   STATIC_ASSERT(ROGUE_FWIF_NUM_RTDATAS == 2);

   result = pvr_srv_rgx_create_hwrt_dataset(
      srv_ws->render_fd,
      create_info->ppp_multi_sample_ctl_y_flipped,
      create_info->ppp_multi_sample_ctl,
      macrotile_addrs,
      pm_mlist_addrs,
      &create_info->rtc_dev_addr,
      rgn_header_addrs,
      &create_info->tpc_dev_addr,
      &create_info->vheap_table_dev_addr,
      free_lists,
      create_info->isp_merge_lower_x,
      create_info->isp_merge_lower_y,
      create_info->isp_merge_scale_x,
      create_info->isp_merge_scale_y,
      create_info->isp_merge_upper_x,
      create_info->isp_merge_upper_y,
      create_info->isp_mtile_size,
      create_info->mtile_stride,
      create_info->ppp_screen,
      create_info->rgn_header_size,
      create_info->te_aa,
      create_info->te_mtile1,
      create_info->te_mtile2,
      create_info->te_screen,
      create_info->tpc_size,
      create_info->tpc_stride,
      create_info->max_rts,
      handles);
   if (result != VK_SUCCESS)
      goto err_vk_free_srv_rt_dataset;

   srv_rt_dataset->rt_datas[0].handle = handles[0];
   srv_rt_dataset->rt_datas[1].handle = handles[1];

   for (uint32_t i = 0; i < ARRAY_SIZE(srv_rt_dataset->rt_datas); i++) {
      srv_rt_dataset->rt_datas[i].sync_prim = pvr_srv_sync_prim_alloc(srv_ws);
      if (!srv_rt_dataset->rt_datas[i].sync_prim)
         goto err_srv_sync_prim_free;
   }

   srv_rt_dataset->base.ws = ws;

   *rt_dataset_out = &srv_rt_dataset->base;

   return VK_SUCCESS;

err_srv_sync_prim_free:
   for (uint32_t i = 0; i < ARRAY_SIZE(srv_rt_dataset->rt_datas); i++) {
      pvr_srv_sync_prim_free(srv_rt_dataset->rt_datas[i].sync_prim);

      if (srv_rt_dataset->rt_datas[i].handle) {
         pvr_srv_rgx_destroy_hwrt_dataset(srv_ws->render_fd,
                                          srv_rt_dataset->rt_datas[i].handle);
      }
   }

err_vk_free_srv_rt_dataset:
   vk_free(srv_ws->alloc, srv_rt_dataset);

   return result;
}

void pvr_srv_render_target_dataset_destroy(
   struct pvr_winsys_rt_dataset *rt_dataset)
{
   struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(rt_dataset->ws);
   struct pvr_srv_winsys_rt_dataset *srv_rt_dataset =
      to_pvr_srv_winsys_rt_dataset(rt_dataset);

   for (uint32_t i = 0; i < ARRAY_SIZE(srv_rt_dataset->rt_datas); i++) {
      pvr_srv_sync_prim_free(srv_rt_dataset->rt_datas[i].sync_prim);

      if (srv_rt_dataset->rt_datas[i].handle) {
         pvr_srv_rgx_destroy_hwrt_dataset(srv_ws->render_fd,
                                          srv_rt_dataset->rt_datas[i].handle);
      }
   }

   vk_free(srv_ws->alloc, srv_rt_dataset);
}

static void pvr_srv_render_ctx_fw_static_state_init(
   struct pvr_winsys_render_ctx_create_info *create_info,
   struct rogue_fwif_static_rendercontext_state *static_state)
{
   struct pvr_winsys_render_ctx_static_state *ws_static_state =
      &create_info->static_state;
   struct rogue_fwif_ta_regs_cswitch *regs =
      &static_state->ctx_switch_geom_regs[0];

   memset(static_state, 0, sizeof(*static_state));

   regs->vdm_context_state_base_addr = ws_static_state->vdm_ctx_state_base_addr;
   regs->ta_context_state_base_addr = ws_static_state->geom_ctx_state_base_addr;

   STATIC_ASSERT(ARRAY_SIZE(regs->ta_state) ==
                 ARRAY_SIZE(ws_static_state->geom_state));
   for (uint32_t i = 0; i < ARRAY_SIZE(ws_static_state->geom_state); i++) {
      regs->ta_state[i].vdm_context_store_task0 =
         ws_static_state->geom_state[i].vdm_ctx_store_task0;
      regs->ta_state[i].vdm_context_store_task1 =
         ws_static_state->geom_state[i].vdm_ctx_store_task1;
      regs->ta_state[i].vdm_context_store_task2 =
         ws_static_state->geom_state[i].vdm_ctx_store_task2;

      regs->ta_state[i].vdm_context_resume_task0 =
         ws_static_state->geom_state[i].vdm_ctx_resume_task0;
      regs->ta_state[i].vdm_context_resume_task1 =
         ws_static_state->geom_state[i].vdm_ctx_resume_task1;
      regs->ta_state[i].vdm_context_resume_task2 =
         ws_static_state->geom_state[i].vdm_ctx_resume_task2;
   }
}

VkResult pvr_srv_winsys_render_ctx_create(
   struct pvr_winsys *ws,
   struct pvr_winsys_render_ctx_create_info *create_info,
   struct pvr_winsys_render_ctx **const ctx_out)
{
   struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(ws);
   struct rogue_fwif_rf_cmd reset_cmd = { 0 };

   struct rogue_fwif_static_rendercontext_state static_state;
   struct pvr_srv_winsys_render_ctx *srv_ctx;
   const uint32_t call_stack_depth = 1U;
   VkResult result;

   srv_ctx = vk_zalloc(srv_ws->alloc,
                       sizeof(*srv_ctx),
                       8,
                       VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!srv_ctx)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = pvr_srv_create_timeline(srv_ws->render_fd, &srv_ctx->timeline_geom);
   if (result != VK_SUCCESS)
      goto err_free_srv_ctx;

   result = pvr_srv_create_timeline(srv_ws->render_fd, &srv_ctx->timeline_frag);
   if (result != VK_SUCCESS)
      goto err_close_timeline_geom;

   pvr_srv_render_ctx_fw_static_state_init(create_info, &static_state);

   /* TODO: Add support for reset framework. Currently we subtract
    * reset_cmd.regs size from reset_cmd size to only pass empty flags field.
    */
   result = pvr_srv_rgx_create_render_context(
      srv_ws->render_fd,
      pvr_srv_from_winsys_priority(create_info->priority),
      create_info->vdm_callstack_addr,
      call_stack_depth,
      sizeof(reset_cmd) - sizeof(reset_cmd.regs),
      (uint8_t *)&reset_cmd,
      srv_ws->server_memctx_data,
      sizeof(static_state),
      (uint8_t *)&static_state,
      0,
      RGX_CONTEXT_FLAG_DISABLESLR,
      0,
      UINT_MAX,
      UINT_MAX,
      &srv_ctx->handle);
   if (result != VK_SUCCESS)
      goto err_close_timeline_frag;

   srv_ctx->base.ws = ws;

   *ctx_out = &srv_ctx->base;

   return VK_SUCCESS;

err_close_timeline_frag:
   close(srv_ctx->timeline_frag);

err_close_timeline_geom:
   close(srv_ctx->timeline_geom);

err_free_srv_ctx:
   vk_free(srv_ws->alloc, srv_ctx);

   return vk_error(NULL, VK_ERROR_INITIALIZATION_FAILED);
}

void pvr_srv_winsys_render_ctx_destroy(struct pvr_winsys_render_ctx *ctx)
{
   struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(ctx->ws);
   struct pvr_srv_winsys_render_ctx *srv_ctx =
      to_pvr_srv_winsys_render_ctx(ctx);

   pvr_srv_rgx_destroy_render_context(srv_ws->render_fd, srv_ctx->handle);
   close(srv_ctx->timeline_frag);
   close(srv_ctx->timeline_geom);
   vk_free(srv_ws->alloc, srv_ctx);
}

static void pvr_srv_geometry_cmd_init(
   const struct pvr_winsys_render_submit_info *submit_info,
   const struct pvr_srv_sync_prim *sync_prim,
   struct rogue_fwif_cmd_ta *cmd)
{
   const struct pvr_winsys_geometry_state *state = &submit_info->geometry;
   struct rogue_fwif_ta_regs *fw_regs = &cmd->geom_regs;

   memset(cmd, 0, sizeof(*cmd));

   cmd->cmd_shared.cmn.frame_num = submit_info->frame_num;

   fw_regs->vdm_ctrl_stream_base = state->regs.vdm_ctrl_stream_base;
   fw_regs->tpu_border_colour_table = state->regs.tpu_border_colour_table;
   fw_regs->ppp_ctrl = state->regs.ppp_ctrl;
   fw_regs->te_psg = state->regs.te_psg;
   fw_regs->tpu = state->regs.tpu;
   fw_regs->vdm_context_resume_task0_size =
      state->regs.vdm_ctx_resume_task0_size;

   assert(state->regs.pds_ctrl >> 32U == 0U);
   fw_regs->pds_ctrl = (uint32_t)state->regs.pds_ctrl;

   if (state->flags & PVR_WINSYS_GEOM_FLAG_FIRST_GEOMETRY)
      cmd->flags |= ROGUE_FWIF_TAFLAGS_FIRSTKICK;

   if (state->flags & PVR_WINSYS_GEOM_FLAG_LAST_GEOMETRY)
      cmd->flags |= ROGUE_FWIF_TAFLAGS_LASTKICK;

   if (state->flags & PVR_WINSYS_GEOM_FLAG_SINGLE_CORE)
      cmd->flags |= ROGUE_FWIF_TAFLAGS_SINGLE_CORE;

   cmd->partial_render_ta_3d_fence.ufo_addr.addr =
      pvr_srv_sync_prim_get_fw_addr(sync_prim);
   cmd->partial_render_ta_3d_fence.value = sync_prim->value;
}

static void pvr_srv_fragment_cmd_init(
   const struct pvr_winsys_render_submit_info *submit_info,
   struct rogue_fwif_cmd_3d *cmd)
{
   const struct pvr_winsys_fragment_state *state = &submit_info->fragment;
   struct rogue_fwif_3d_regs *fw_regs = &cmd->regs;

   memset(cmd, 0, sizeof(*cmd));

   cmd->cmd_shared.cmn.frame_num = submit_info->frame_num;

   fw_regs->usc_pixel_output_ctrl = state->regs.usc_pixel_output_ctrl;
   fw_regs->isp_bgobjdepth = state->regs.isp_bgobjdepth;
   fw_regs->isp_bgobjvals = state->regs.isp_bgobjvals;
   fw_regs->isp_aa = state->regs.isp_aa;
   fw_regs->isp_ctl = state->regs.isp_ctl;
   fw_regs->tpu = state->regs.tpu;
   fw_regs->event_pixel_pds_info = state->regs.event_pixel_pds_info;
   fw_regs->pixel_phantom = state->regs.pixel_phantom;
   fw_regs->event_pixel_pds_data = state->regs.event_pixel_pds_data;
   fw_regs->isp_scissor_base = state->regs.isp_scissor_base;
   fw_regs->isp_dbias_base = state->regs.isp_dbias_base;
   fw_regs->isp_oclqry_base = state->regs.isp_oclqry_base;
   fw_regs->isp_zlsctl = state->regs.isp_zlsctl;
   fw_regs->isp_zload_store_base = state->regs.isp_zload_store_base;
   fw_regs->isp_stencil_load_store_base =
      state->regs.isp_stencil_load_store_base;
   fw_regs->isp_zls_pixels = state->regs.isp_zls_pixels;

   STATIC_ASSERT(ARRAY_SIZE(fw_regs->pbe_word) ==
                 ARRAY_SIZE(state->regs.pbe_word));

   STATIC_ASSERT(ARRAY_SIZE(fw_regs->pbe_word[0]) <=
                 ARRAY_SIZE(state->regs.pbe_word[0]));

#if !defined(NDEBUG)
   /* Depending on the hardware we might have more PBE words than the firmware
    * accepts so check that the extra words are 0.
    */
   if (ARRAY_SIZE(fw_regs->pbe_word[0]) < ARRAY_SIZE(state->regs.pbe_word[0])) {
      /* For each color attachment. */
      for (uint32_t i = 0; i < ARRAY_SIZE(state->regs.pbe_word); i++) {
         /* For each extra PBE word not used by the firmware. */
         for (uint32_t j = ARRAY_SIZE(fw_regs->pbe_word[0]);
              j < ARRAY_SIZE(state->regs.pbe_word[0]);
              j++) {
            assert(state->regs.pbe_word[i][j] == 0);
         }
      }
   }
#endif

   memcpy(fw_regs->pbe_word, state->regs.pbe_word, sizeof(fw_regs->pbe_word));

   fw_regs->tpu_border_colour_table = state->regs.tpu_border_colour_table;

   STATIC_ASSERT(ARRAY_SIZE(fw_regs->pds_bgnd) ==
                 ARRAY_SIZE(state->regs.pds_bgnd));
   typed_memcpy(fw_regs->pds_bgnd,
                state->regs.pds_bgnd,
                ARRAY_SIZE(fw_regs->pds_bgnd));

   STATIC_ASSERT(ARRAY_SIZE(fw_regs->pds_pr_bgnd) ==
                 ARRAY_SIZE(state->regs.pds_pr_bgnd));
   typed_memcpy(fw_regs->pds_pr_bgnd,
                state->regs.pds_pr_bgnd,
                ARRAY_SIZE(fw_regs->pds_pr_bgnd));

   if (state->flags & PVR_WINSYS_FRAG_FLAG_DEPTH_BUFFER_PRESENT)
      cmd->flags |= ROGUE_FWIF_RENDERFLAGS_DEPTHBUFFER;

   if (state->flags & PVR_WINSYS_FRAG_FLAG_STENCIL_BUFFER_PRESENT)
      cmd->flags |= ROGUE_FWIF_RENDERFLAGS_STENCILBUFFER;

   if (state->flags & PVR_WINSYS_FRAG_FLAG_PREVENT_CDM_OVERLAP)
      cmd->flags |= ROGUE_FWIF_RENDERFLAGS_PREVENT_CDM_OVERLAP;

   if (state->flags & PVR_WINSYS_FRAG_FLAG_SINGLE_CORE)
      cmd->flags |= ROGUE_FWIF_RENDERFLAGS_SINGLE_CORE;

   cmd->zls_stride = state->zls_stride;
   cmd->sls_stride = state->sls_stride;
}

VkResult pvr_srv_winsys_render_submit(
   const struct pvr_winsys_render_ctx *ctx,
   const struct pvr_winsys_render_submit_info *submit_info,
   struct vk_sync *signal_sync_geom,
   struct vk_sync *signal_sync_frag)
{
   const struct pvr_srv_winsys_rt_dataset *srv_rt_dataset =
      to_pvr_srv_winsys_rt_dataset(submit_info->rt_dataset);
   struct pvr_srv_sync_prim *sync_prim =
      srv_rt_dataset->rt_datas[submit_info->rt_data_idx].sync_prim;
   void *rt_data_handle =
      srv_rt_dataset->rt_datas[submit_info->rt_data_idx].handle;
   const struct pvr_srv_winsys_render_ctx *srv_ctx =
      to_pvr_srv_winsys_render_ctx(ctx);
   const struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(ctx->ws);

   uint32_t sync_pmr_flags[PVR_SRV_SYNC_MAX] = { 0U };
   void *sync_pmrs[PVR_SRV_SYNC_MAX] = { NULL };
   uint32_t sync_pmr_count;

   struct pvr_srv_sync *srv_signal_sync_geom;
   struct pvr_srv_sync *srv_signal_sync_frag;

   struct rogue_fwif_cmd_ta geom_cmd;
   struct rogue_fwif_cmd_3d frag_cmd;

   int in_frag_fd = -1;
   int in_geom_fd = -1;
   int fence_frag;
   int fence_geom;

   VkResult result;

   pvr_srv_geometry_cmd_init(submit_info, sync_prim, &geom_cmd);
   pvr_srv_fragment_cmd_init(submit_info, &frag_cmd);

   for (uint32_t i = 0U; i < submit_info->wait_count; i++) {
      struct pvr_srv_sync *srv_wait_sync = to_srv_sync(submit_info->waits[i]);
      int ret;

      if (!submit_info->waits[i] || srv_wait_sync->fd < 0)
         continue;

      if (submit_info->stage_flags[i] & PVR_PIPELINE_STAGE_GEOM_BIT) {
         ret = sync_accumulate("", &in_geom_fd, srv_wait_sync->fd);
         if (ret) {
            result = vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);
            goto end_close_in_fds;
         }

         submit_info->stage_flags[i] &= ~PVR_PIPELINE_STAGE_GEOM_BIT;
      }

      if (submit_info->stage_flags[i] & PVR_PIPELINE_STAGE_FRAG_BIT) {
         ret = sync_accumulate("", &in_frag_fd, srv_wait_sync->fd);
         if (ret) {
            result = vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);
            goto end_close_in_fds;
         }

         submit_info->stage_flags[i] &= ~PVR_PIPELINE_STAGE_FRAG_BIT;
      }
   }

   if (submit_info->bo_count <= ARRAY_SIZE(sync_pmrs)) {
      sync_pmr_count = submit_info->bo_count;
   } else {
      mesa_logw("Too many bos to synchronize access to (ignoring %zu bos)\n",
                submit_info->bo_count - ARRAY_SIZE(sync_pmrs));
      sync_pmr_count = ARRAY_SIZE(sync_pmrs);
   }

   STATIC_ASSERT(ARRAY_SIZE(sync_pmrs) == ARRAY_SIZE(sync_pmr_flags));
   assert(sync_pmr_count <= ARRAY_SIZE(sync_pmrs));
   for (uint32_t i = 0; i < sync_pmr_count; i++) {
      const struct pvr_winsys_job_bo *job_bo = &submit_info->bos[i];
      const struct pvr_srv_winsys_bo *srv_bo = to_pvr_srv_winsys_bo(job_bo->bo);

      sync_pmrs[i] = srv_bo->pmr;

      if (job_bo->flags & PVR_WINSYS_JOB_BO_FLAG_WRITE)
         sync_pmr_flags[i] = PVR_BUFFER_FLAG_WRITE;
      else
         sync_pmr_flags[i] = PVR_BUFFER_FLAG_READ;
   }

   /* The 1.14 PowerVR Services KM driver doesn't add a sync dependency to the
    * fragment phase on the geometry phase for us. This makes it
    * necessary to use a sync prim for this purpose. This requires that we pass
    * in the same sync prim information for the geometry phase update and the
    * PR fence. We update the sync prim value here as this is the value the
    * sync prim will get updated to once the geometry phase has completed and
    * the value the PR or fragment phase will be fenced on.
    */
   sync_prim->value++;

   do {
      result = pvr_srv_rgx_kick_render2(srv_ws->render_fd,
                                        srv_ctx->handle,
                                        0,
                                        NULL,
                                        NULL,
                                        NULL,
                                        1,
                                        &sync_prim->srv_ws->sync_block_handle,
                                        &sync_prim->offset,
                                        &sync_prim->value,
                                        0,
                                        NULL,
                                        NULL,
                                        NULL,
                                        sync_prim->srv_ws->sync_block_handle,
                                        sync_prim->offset,
                                        sync_prim->value,
                                        in_geom_fd,
                                        srv_ctx->timeline_geom,
                                        &fence_geom,
                                        "GEOM",
                                        in_frag_fd,
                                        srv_ctx->timeline_frag,
                                        &fence_frag,
                                        "FRAG",
                                        sizeof(geom_cmd),
                                        (uint8_t *)&geom_cmd,
                                        /* Currently no support for PRs. */
                                        0,
                                        /* Currently no support for PRs. */
                                        NULL,
                                        sizeof(frag_cmd),
                                        (uint8_t *)&frag_cmd,
                                        submit_info->job_num,
                                        /* Always kick the TA. */
                                        true,
                                        /* Always kick a PR. */
                                        true,
                                        submit_info->run_frag,
                                        false,
                                        0,
                                        rt_data_handle,
                                        /* Currently no support for PRs. */
                                        NULL,
                                        /* Currently no support for PRs. */
                                        NULL,
                                        sync_pmr_count,
                                        sync_pmr_count ? sync_pmr_flags : NULL,
                                        sync_pmr_count ? sync_pmrs : NULL,
                                        0,
                                        0,
                                        0,
                                        0,
                                        0);
   } while (result == VK_NOT_READY);

   if (result != VK_SUCCESS)
      goto end_close_in_fds;

   if (signal_sync_geom) {
      srv_signal_sync_geom = to_srv_sync(signal_sync_geom);
      pvr_srv_set_sync_payload(srv_signal_sync_geom, fence_geom);
   } else if (fence_geom != -1) {
      close(fence_geom);
   }

   if (signal_sync_frag) {
      srv_signal_sync_frag = to_srv_sync(signal_sync_frag);
      pvr_srv_set_sync_payload(srv_signal_sync_frag, fence_frag);
   } else if (fence_frag != -1) {
      close(fence_frag);
   }

end_close_in_fds:
   if (in_geom_fd >= 0)
      close(in_geom_fd);

   if (in_frag_fd >= 0)
      close(in_frag_fd);

   return result;
}
