/*
 * Copyright © 2019 Raspberry Pi
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

#include "v3dv_private.h"
#include "broadcom/cle/v3dx_pack.h"
#include "util/half_float.h"
#include "util/u_pack_color.h"
#include "vk_format_info.h"
#include "vk_util.h"

const struct v3dv_dynamic_state default_dynamic_state = {
   .viewport = {
      .count = 0,
   },
   .scissor = {
      .count = 0,
   },
   .stencil_compare_mask =
   {
     .front = ~0u,
     .back = ~0u,
   },
   .stencil_write_mask =
   {
     .front = ~0u,
     .back = ~0u,
   },
   .stencil_reference =
   {
     .front = 0u,
     .back = 0u,
   },
   .blend_constants = { 0.0f, 0.0f, 0.0f, 0.0f },
   .depth_bias = {
      .constant_factor = 0.0f,
      .depth_bias_clamp = 0.0f,
      .slope_factor = 0.0f,
   },
   .line_width = 1.0f,
};

void
v3dv_job_add_bo(struct v3dv_job *job, struct v3dv_bo *bo)
{
   if (!bo)
      return;

   if (_mesa_set_search(job->bos, bo))
      return;

   _mesa_set_add(job->bos, bo);
   job->bo_count++;
}

static void
cmd_buffer_emit_render_pass_rcl(struct v3dv_cmd_buffer *cmd_buffer);

VkResult
v3dv_CreateCommandPool(VkDevice _device,
                       const VkCommandPoolCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkCommandPool *pCmdPool)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_cmd_pool *pool;

   /* We only support one queue */
   assert(pCreateInfo->queueFamilyIndex == 0);

   pool = vk_object_zalloc(&device->vk, pAllocator, sizeof(*pool),
                           VK_OBJECT_TYPE_COMMAND_POOL);
   if (pool == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (pAllocator)
      pool->alloc = *pAllocator;
   else
      pool->alloc = device->vk.alloc;

   list_inithead(&pool->cmd_buffers);

   *pCmdPool = v3dv_cmd_pool_to_handle(pool);

   return VK_SUCCESS;
}

static void
cmd_buffer_init(struct v3dv_cmd_buffer *cmd_buffer,
                struct v3dv_device *device,
                struct v3dv_cmd_pool *pool,
                VkCommandBufferLevel level)
{
   /* Do not reset the base object! If we are calling this from a command
    * buffer reset that would reset the loader's dispatch table for the
    * command buffer, and any other relevant info from vk_object_base
    */
   const uint32_t base_size = sizeof(struct vk_object_base);
   uint8_t *cmd_buffer_driver_start = ((uint8_t *) cmd_buffer) + base_size;
   memset(cmd_buffer_driver_start, 0, sizeof(*cmd_buffer) - base_size);

   cmd_buffer->device = device;
   cmd_buffer->pool = pool;
   cmd_buffer->level = level;

   list_inithead(&cmd_buffer->private_objs);
   list_inithead(&cmd_buffer->jobs);
   list_inithead(&cmd_buffer->list_link);

   assert(pool);
   list_addtail(&cmd_buffer->pool_link, &pool->cmd_buffers);

   cmd_buffer->state.subpass_idx = -1;
   cmd_buffer->state.meta.subpass_idx = -1;

   cmd_buffer->status = V3DV_CMD_BUFFER_STATUS_INITIALIZED;
}

static VkResult
cmd_buffer_create(struct v3dv_device *device,
                  struct v3dv_cmd_pool *pool,
                  VkCommandBufferLevel level,
                  VkCommandBuffer *pCommandBuffer)
{
   struct v3dv_cmd_buffer *cmd_buffer;
   cmd_buffer = vk_object_zalloc(&device->vk,
                                 &pool->alloc,
                                 sizeof(*cmd_buffer),
                                 VK_OBJECT_TYPE_COMMAND_BUFFER);
   if (cmd_buffer == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   cmd_buffer_init(cmd_buffer, device, pool, level);

   *pCommandBuffer = v3dv_cmd_buffer_to_handle(cmd_buffer);

   return VK_SUCCESS;
}

static void
job_destroy_gpu_cl_resources(struct v3dv_job *job)
{
   assert(job->type == V3DV_JOB_TYPE_GPU_CL ||
          job->type == V3DV_JOB_TYPE_GPU_CL_SECONDARY);

   v3dv_cl_destroy(&job->bcl);
   v3dv_cl_destroy(&job->rcl);
   v3dv_cl_destroy(&job->indirect);

   /* Since we don't ref BOs when we add them to the command buffer, don't
    * unref them here either. Bo's will be freed when their corresponding API
    * objects are destroyed.
    */
   _mesa_set_destroy(job->bos, NULL);

   v3dv_bo_free(job->device, job->tile_alloc);
   v3dv_bo_free(job->device, job->tile_state);
}

static void
job_destroy_cloned_gpu_cl_resources(struct v3dv_job *job)
{
   assert(job->type == V3DV_JOB_TYPE_GPU_CL);

   list_for_each_entry_safe(struct v3dv_bo, bo, &job->bcl.bo_list, list_link) {
      list_del(&bo->list_link);
      vk_free(&job->device->vk.alloc, bo);
   }

   list_for_each_entry_safe(struct v3dv_bo, bo, &job->rcl.bo_list, list_link) {
      list_del(&bo->list_link);
      vk_free(&job->device->vk.alloc, bo);
   }

   list_for_each_entry_safe(struct v3dv_bo, bo, &job->indirect.bo_list, list_link) {
      list_del(&bo->list_link);
      vk_free(&job->device->vk.alloc, bo);
   }
}

static void
job_destroy_gpu_csd_resources(struct v3dv_job *job)
{
   assert(job->type == V3DV_JOB_TYPE_GPU_CSD);
   assert(job->cmd_buffer);

   v3dv_cl_destroy(&job->indirect);

   _mesa_set_destroy(job->bos, NULL);

   if (job->csd.shared_memory)
      v3dv_bo_free(job->device, job->csd.shared_memory);
}

static void
job_destroy_cpu_wait_events_resources(struct v3dv_job *job)
{
   assert(job->type == V3DV_JOB_TYPE_CPU_WAIT_EVENTS);
   assert(job->cmd_buffer);
   vk_free(&job->cmd_buffer->device->vk.alloc, job->cpu.event_wait.events);
}

static void
job_destroy_cpu_csd_indirect_resources(struct v3dv_job *job)
{
   assert(job->type == V3DV_JOB_TYPE_CPU_CSD_INDIRECT);
   assert(job->cmd_buffer);
   v3dv_job_destroy(job->cpu.csd_indirect.csd_job);
}

void
v3dv_job_destroy(struct v3dv_job *job)
{
   assert(job);

   list_del(&job->list_link);

   /* Cloned jobs don't make deep copies of the original jobs, so they don't
    * own any of their resources. However, they do allocate clones of BO
    * structs, so make sure we free those.
    */
   if (!job->is_clone) {
      switch (job->type) {
      case V3DV_JOB_TYPE_GPU_CL:
      case V3DV_JOB_TYPE_GPU_CL_SECONDARY:
         job_destroy_gpu_cl_resources(job);
         break;
      case V3DV_JOB_TYPE_GPU_CSD:
         job_destroy_gpu_csd_resources(job);
         break;
      case V3DV_JOB_TYPE_CPU_WAIT_EVENTS:
         job_destroy_cpu_wait_events_resources(job);
         break;
      case V3DV_JOB_TYPE_CPU_CSD_INDIRECT:
         job_destroy_cpu_csd_indirect_resources(job);
         break;
      default:
         break;
      }
   } else {
      /* Cloned jobs */
      if (job->type == V3DV_JOB_TYPE_GPU_CL)
         job_destroy_cloned_gpu_cl_resources(job);
   }

   vk_free(&job->device->vk.alloc, job);
}

void
v3dv_cmd_buffer_add_private_obj(struct v3dv_cmd_buffer *cmd_buffer,
                                uint64_t obj,
                                v3dv_cmd_buffer_private_obj_destroy_cb destroy_cb)
{
   struct v3dv_cmd_buffer_private_obj *pobj =
      vk_alloc(&cmd_buffer->device->vk.alloc, sizeof(*pobj), 8,
               VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!pobj) {
      v3dv_flag_oom(cmd_buffer, NULL);
      return;
   }

   pobj->obj = obj;
   pobj->destroy_cb = destroy_cb;

   list_addtail(&pobj->list_link, &cmd_buffer->private_objs);
}

static void
cmd_buffer_destroy_private_obj(struct v3dv_cmd_buffer *cmd_buffer,
                               struct v3dv_cmd_buffer_private_obj *pobj)
{
   assert(pobj && pobj->obj && pobj->destroy_cb);
   pobj->destroy_cb(v3dv_device_to_handle(cmd_buffer->device),
                    pobj->obj,
                    &cmd_buffer->device->vk.alloc);
   list_del(&pobj->list_link);
   vk_free(&cmd_buffer->device->vk.alloc, pobj);
}

static void
cmd_buffer_free_resources(struct v3dv_cmd_buffer *cmd_buffer)
{
   list_for_each_entry_safe(struct v3dv_job, job,
                            &cmd_buffer->jobs, list_link) {
      v3dv_job_destroy(job);
   }

   if (cmd_buffer->state.job)
      v3dv_job_destroy(cmd_buffer->state.job);

   if (cmd_buffer->state.attachments)
      vk_free(&cmd_buffer->pool->alloc, cmd_buffer->state.attachments);

   if (cmd_buffer->state.query.end.alloc_count > 0)
      vk_free(&cmd_buffer->device->vk.alloc, cmd_buffer->state.query.end.states);

   if (cmd_buffer->push_constants_resource.bo)
      v3dv_bo_free(cmd_buffer->device, cmd_buffer->push_constants_resource.bo);

   list_for_each_entry_safe(struct v3dv_cmd_buffer_private_obj, pobj,
                            &cmd_buffer->private_objs, list_link) {
      cmd_buffer_destroy_private_obj(cmd_buffer, pobj);
   }

   if (cmd_buffer->state.meta.attachments) {
         assert(cmd_buffer->state.meta.attachment_alloc_count > 0);
         vk_free(&cmd_buffer->device->vk.alloc, cmd_buffer->state.meta.attachments);
   }
}

static void
cmd_buffer_destroy(struct v3dv_cmd_buffer *cmd_buffer)
{
   list_del(&cmd_buffer->pool_link);
   cmd_buffer_free_resources(cmd_buffer);
   vk_object_free(&cmd_buffer->device->vk, &cmd_buffer->pool->alloc, cmd_buffer);
}

void
v3dv_job_emit_binning_flush(struct v3dv_job *job)
{
   assert(job);

   v3dv_cl_ensure_space_with_branch(&job->bcl, cl_packet_length(FLUSH));
   v3dv_return_if_oom(NULL, job);

   cl_emit(&job->bcl, FLUSH, flush);
}

static bool
attachment_list_is_subset(struct v3dv_subpass_attachment *l1, uint32_t l1_count,
                          struct v3dv_subpass_attachment *l2, uint32_t l2_count)
{
   for (uint32_t i = 0; i < l1_count; i++) {
      uint32_t attachment_idx = l1[i].attachment;
      if (attachment_idx == VK_ATTACHMENT_UNUSED)
         continue;

      uint32_t j;
      for (j = 0; j < l2_count; j++) {
         if (l2[j].attachment == attachment_idx)
            break;
      }
      if (j == l2_count)
         return false;
   }

   return true;
 }

static bool
cmd_buffer_can_merge_subpass(struct v3dv_cmd_buffer *cmd_buffer,
                             uint32_t subpass_idx)
{
   const struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   assert(state->pass);

   const struct v3dv_physical_device *physical_device =
      &cmd_buffer->device->instance->physicalDevice;

   if (cmd_buffer->level != VK_COMMAND_BUFFER_LEVEL_PRIMARY)
      return false;

   if (!cmd_buffer->state.job)
      return false;

   if (cmd_buffer->state.job->always_flush)
      return false;

   if (!physical_device->options.merge_jobs)
      return false;

   /* Each render pass starts a new job */
   if (subpass_idx == 0)
      return false;

   /* Two subpasses can be merged in the same job if we can emit a single RCL
    * for them (since the RCL includes the END_OF_RENDERING command that
    * triggers the "render job finished" interrupt). We can do this so long
    * as both subpasses render against the same attachments.
    */
   assert(state->subpass_idx == subpass_idx - 1);
   struct v3dv_subpass *prev_subpass = &state->pass->subpasses[state->subpass_idx];
   struct v3dv_subpass *subpass = &state->pass->subpasses[subpass_idx];

   /* Because the list of subpass attachments can include VK_ATTACHMENT_UNUSED,
    * we need to check that for each subpass all its used attachments are
    * used by the other subpass.
    */
   bool compatible =
      attachment_list_is_subset(prev_subpass->color_attachments,
                                prev_subpass->color_count,
                                subpass->color_attachments,
                                subpass->color_count);
   if (!compatible)
      return false;

   compatible =
      attachment_list_is_subset(subpass->color_attachments,
                                subpass->color_count,
                                prev_subpass->color_attachments,
                                prev_subpass->color_count);
   if (!compatible)
      return false;

   if (subpass->ds_attachment.attachment !=
       prev_subpass->ds_attachment.attachment)
      return false;

   /* FIXME: Since some attachment formats can't be resolved using the TLB we
    * need to emit separate resolve jobs for them and that would not be
    * compatible with subpass merges. We could fix that by testing if any of
    * the attachments to resolve doesn't suppotr TLB resolves.
    */
   if (prev_subpass->resolve_attachments || subpass->resolve_attachments)
      return false;

   return true;
}

/**
 * Computes and sets the job frame tiling information required to setup frame
 * binning and rendering.
 */
static struct v3dv_frame_tiling *
job_compute_frame_tiling(struct v3dv_job *job,
                         uint32_t width,
                         uint32_t height,
                         uint32_t layers,
                         uint32_t render_target_count,
                         uint8_t max_internal_bpp,
                         bool msaa)
{
   static const uint8_t tile_sizes[] = {
      64, 64,
      64, 32,
      32, 32,
      32, 16,
      16, 16,
      16,  8,
       8,  8
   };

   assert(job);
   struct v3dv_frame_tiling *tiling = &job->frame_tiling;

   tiling->width = width;
   tiling->height = height;
   tiling->layers = layers;
   tiling->render_target_count = render_target_count;
   tiling->msaa = msaa;

   uint32_t tile_size_index = 0;

   if (render_target_count > 2)
      tile_size_index += 2;
   else if (render_target_count > 1)
      tile_size_index += 1;

   if (msaa)
      tile_size_index += 2;

   tiling->internal_bpp = max_internal_bpp;
   tile_size_index += tiling->internal_bpp;
   assert(tile_size_index < ARRAY_SIZE(tile_sizes) / 2);

   tiling->tile_width = tile_sizes[tile_size_index * 2];
   tiling->tile_height = tile_sizes[tile_size_index * 2 + 1];

   tiling->draw_tiles_x = DIV_ROUND_UP(width, tiling->tile_width);
   tiling->draw_tiles_y = DIV_ROUND_UP(height, tiling->tile_height);

   /* Size up our supertiles until we get under the limit */
   const uint32_t max_supertiles = 256;
   tiling->supertile_width = 1;
   tiling->supertile_height = 1;
   for (;;) {
      tiling->frame_width_in_supertiles =
         DIV_ROUND_UP(tiling->draw_tiles_x, tiling->supertile_width);
      tiling->frame_height_in_supertiles =
         DIV_ROUND_UP(tiling->draw_tiles_y, tiling->supertile_height);
      const uint32_t num_supertiles = tiling->frame_width_in_supertiles *
                                      tiling->frame_height_in_supertiles;
      if (num_supertiles < max_supertiles)
         break;

      if (tiling->supertile_width < tiling->supertile_height)
         tiling->supertile_width++;
      else
         tiling->supertile_height++;
   }

   return tiling;
}

void
v3dv_job_start_frame(struct v3dv_job *job,
                     uint32_t width,
                     uint32_t height,
                     uint32_t layers,
                     uint32_t render_target_count,
                     uint8_t max_internal_bpp,
                     bool msaa)
{
   assert(job);

   /* Start by computing frame tiling spec for this job */
   const struct v3dv_frame_tiling *tiling =
      job_compute_frame_tiling(job,
                               width, height, layers,
                               render_target_count, max_internal_bpp, msaa);

   v3dv_cl_ensure_space_with_branch(&job->bcl, 256);
   v3dv_return_if_oom(NULL, job);

   /* The PTB will request the tile alloc initial size per tile at start
    * of tile binning.
    */
   uint32_t tile_alloc_size = 64 * tiling->layers *
                              tiling->draw_tiles_x *
                              tiling->draw_tiles_y;

   /* The PTB allocates in aligned 4k chunks after the initial setup. */
   tile_alloc_size = align(tile_alloc_size, 4096);

   /* Include the first two chunk allocations that the PTB does so that
    * we definitely clear the OOM condition before triggering one (the HW
    * won't trigger OOM during the first allocations).
    */
   tile_alloc_size += 8192;

   /* For performance, allocate some extra initial memory after the PTB's
    * minimal allocations, so that we hopefully don't have to block the
    * GPU on the kernel handling an OOM signal.
    */
   tile_alloc_size += 512 * 1024;

   job->tile_alloc = v3dv_bo_alloc(job->device, tile_alloc_size,
                                   "tile_alloc", true);
   if (!job->tile_alloc) {
      v3dv_flag_oom(NULL, job);
      return;
   }

   v3dv_job_add_bo(job, job->tile_alloc);

   const uint32_t tsda_per_tile_size = 256;
   const uint32_t tile_state_size = tiling->layers *
                                    tiling->draw_tiles_x *
                                    tiling->draw_tiles_y *
                                    tsda_per_tile_size;
   job->tile_state = v3dv_bo_alloc(job->device, tile_state_size, "TSDA", true);
   if (!job->tile_state) {
      v3dv_flag_oom(NULL, job);
      return;
   }

   v3dv_job_add_bo(job, job->tile_state);

   /* This must go before the binning mode configuration. It is
    * required for layered framebuffers to work.
    */
   cl_emit(&job->bcl, NUMBER_OF_LAYERS, config) {
      config.number_of_layers = layers;
   }

   cl_emit(&job->bcl, TILE_BINNING_MODE_CFG, config) {
      config.width_in_pixels = tiling->width;
      config.height_in_pixels = tiling->height;
      config.number_of_render_targets = MAX2(tiling->render_target_count, 1);
      config.multisample_mode_4x = tiling->msaa;
      config.maximum_bpp_of_all_render_targets = tiling->internal_bpp;
   }

   /* There's definitely nothing in the VCD cache we want. */
   cl_emit(&job->bcl, FLUSH_VCD_CACHE, bin);

   /* "Binning mode lists must have a Start Tile Binning item (6) after
    *  any prefix state data before the binning list proper starts."
    */
   cl_emit(&job->bcl, START_TILE_BINNING, bin);

   job->ez_state = VC5_EZ_UNDECIDED;
   job->first_ez_state = VC5_EZ_UNDECIDED;
}

static void
cmd_buffer_end_render_pass_frame(struct v3dv_cmd_buffer *cmd_buffer)
{
   assert(cmd_buffer->state.job);

   /* Typically, we have a single job for each subpass and we emit the job's RCL
    * here when we are ending the frame for the subpass. However, some commands
    * such as vkCmdClearAttachments need to run in their own separate job and
    * they emit their own RCL even if they execute inside a subpass. In this
    * scenario, we don't want to emit subpass RCL when we end the frame for
    * those jobs, so we only emit the subpass RCL if the job has not recorded
    * any RCL commands of its own.
    */
   if (v3dv_cl_offset(&cmd_buffer->state.job->rcl) == 0)
      cmd_buffer_emit_render_pass_rcl(cmd_buffer);

   v3dv_job_emit_binning_flush(cmd_buffer->state.job);
}

static void
cmd_buffer_end_render_pass_secondary(struct v3dv_cmd_buffer *cmd_buffer)
{
   assert(cmd_buffer->state.job);
   v3dv_cl_ensure_space_with_branch(&cmd_buffer->state.job->bcl,
                                    cl_packet_length(RETURN_FROM_SUB_LIST));
   v3dv_return_if_oom(cmd_buffer, NULL);
   cl_emit(&cmd_buffer->state.job->bcl, RETURN_FROM_SUB_LIST, ret);
}

struct v3dv_job *
v3dv_cmd_buffer_create_cpu_job(struct v3dv_device *device,
                               enum v3dv_job_type type,
                               struct v3dv_cmd_buffer *cmd_buffer,
                               uint32_t subpass_idx)
{
   struct v3dv_job *job = vk_zalloc(&device->vk.alloc,
                                    sizeof(struct v3dv_job), 8,
                                    VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!job) {
      v3dv_flag_oom(cmd_buffer, NULL);
      return NULL;
   }

   v3dv_job_init(job, type, device, cmd_buffer, subpass_idx);
   return job;
}

static void
cmd_buffer_add_cpu_jobs_for_pending_state(struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;

   if (state->query.end.used_count > 0) {
      const uint32_t query_count = state->query.end.used_count;
      for (uint32_t i = 0; i < query_count; i++) {
         assert(i < state->query.end.used_count);
         struct v3dv_job *job =
            v3dv_cmd_buffer_create_cpu_job(cmd_buffer->device,
                                           V3DV_JOB_TYPE_CPU_END_QUERY,
                                           cmd_buffer, -1);
         v3dv_return_if_oom(cmd_buffer, NULL);

         job->cpu.query_end = state->query.end.states[i];
         list_addtail(&job->list_link, &cmd_buffer->jobs);
      }
   }
}

void
v3dv_cmd_buffer_finish_job(struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_job *job = cmd_buffer->state.job;
   if (!job)
      return;

   if (cmd_buffer->state.oom) {
      v3dv_job_destroy(job);
      cmd_buffer->state.job = NULL;
      return;
   }

   /* If we have created a job for a command buffer then we should have
    * recorded something into it: if the job was started in a render pass, it
    * should at least have the start frame commands, otherwise, it should have
    * a transfer command. The only exception are secondary command buffers
    * inside a render pass.
    */
   assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY ||
          v3dv_cl_offset(&job->bcl) > 0);

   /* When we merge multiple subpasses into the same job we must only emit one
    * RCL, so we do that here, when we decided that we need to finish the job.
    * Any rendering that happens outside a render pass is never merged, so
    * the RCL should have been emitted by the time we got here.
    */
   assert(v3dv_cl_offset(&job->rcl) != 0 || cmd_buffer->state.pass);

   /* If we are finishing a job inside a render pass we have two scenarios:
    *
    * 1. It is a regular CL, in which case we will submit the job to the GPU,
    *    so we may need to generate an RCL and add a binning flush.
    *
    * 2. It is a partial CL recorded in a secondary command buffer, in which
    *    case we are not submitting it directly to the GPU but rather branch to
    *    it from a primary command buffer. In this case we just want to end
    *    the BCL with a RETURN_FROM_SUB_LIST and the RCL and binning flush
    *    will be the primary job that branches to this CL.
    */
   if (cmd_buffer->state.pass) {
      if (job->type == V3DV_JOB_TYPE_GPU_CL) {
         cmd_buffer_end_render_pass_frame(cmd_buffer);
      } else {
         assert(job->type == V3DV_JOB_TYPE_GPU_CL_SECONDARY);
         cmd_buffer_end_render_pass_secondary(cmd_buffer);
      }
   }

   list_addtail(&job->list_link, &cmd_buffer->jobs);
   cmd_buffer->state.job = NULL;

   /* If we have recorded any state with this last GPU job that requires to
    * emit CPU jobs after the job is completed, add them now. The only
    * exception is secondary command buffers inside a render pass, because in
    * that case we want to defer this until we finish recording the primary
    * job into which we execute the secondary.
    */
   if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY ||
       !cmd_buffer->state.pass) {
      cmd_buffer_add_cpu_jobs_for_pending_state(cmd_buffer);
   }
}

static bool
job_type_is_gpu(struct v3dv_job *job)
{
   switch (job->type) {
   case V3DV_JOB_TYPE_GPU_CL:
   case V3DV_JOB_TYPE_GPU_CL_SECONDARY:
   case V3DV_JOB_TYPE_GPU_TFU:
   case V3DV_JOB_TYPE_GPU_CSD:
      return true;
   default:
      return false;
   }
}

static void
cmd_buffer_serialize_job_if_needed(struct v3dv_cmd_buffer *cmd_buffer,
                                   struct v3dv_job *job)
{
   assert(cmd_buffer && job);

   if (!cmd_buffer->state.has_barrier)
      return;

   /* Serialization only affects GPU jobs, CPU jobs are always automatically
    * serialized.
    */
   if (!job_type_is_gpu(job))
      return;

   job->serialize = true;
   if (cmd_buffer->state.has_bcl_barrier &&
       (job->type == V3DV_JOB_TYPE_GPU_CL ||
        job->type == V3DV_JOB_TYPE_GPU_CL_SECONDARY)) {
      job->needs_bcl_sync = true;
   }

   cmd_buffer->state.has_barrier = false;
   cmd_buffer->state.has_bcl_barrier = false;
}

void
v3dv_job_init(struct v3dv_job *job,
              enum v3dv_job_type type,
              struct v3dv_device *device,
              struct v3dv_cmd_buffer *cmd_buffer,
              int32_t subpass_idx)
{
   assert(job);

   /* Make sure we haven't made this new job current before calling here */
   assert(!cmd_buffer || cmd_buffer->state.job != job);

   job->type = type;

   job->device = device;
   job->cmd_buffer = cmd_buffer;

   list_inithead(&job->list_link);

   if (type == V3DV_JOB_TYPE_GPU_CL ||
       type == V3DV_JOB_TYPE_GPU_CL_SECONDARY ||
       type == V3DV_JOB_TYPE_GPU_CSD) {
      job->bos =
         _mesa_set_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);
      job->bo_count = 0;

      v3dv_cl_init(job, &job->indirect);

      if (V3D_DEBUG & V3D_DEBUG_ALWAYS_FLUSH)
         job->always_flush = true;
   }

   if (type == V3DV_JOB_TYPE_GPU_CL ||
       type == V3DV_JOB_TYPE_GPU_CL_SECONDARY) {
      v3dv_cl_init(job, &job->bcl);
      v3dv_cl_init(job, &job->rcl);
   }

   if (cmd_buffer) {
      /* Flag all state as dirty. Generally, we need to re-emit state for each
       * new job.
       *
       * FIXME: there may be some exceptions, in which case we could skip some
       * bits.
       */
      cmd_buffer->state.dirty = ~0;

      /* Honor inheritance of occlussion queries in secondaries if requested */
      if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY &&
          cmd_buffer->state.inheritance.occlusion_query_enable) {
         cmd_buffer->state.dirty &= ~V3DV_CMD_DIRTY_OCCLUSION_QUERY;
      }

      /* Keep track of the first subpass that we are recording in this new job.
       * We will use this when we emit the RCL to decide how to emit our loads
       * and stores.
       */
      if (cmd_buffer->state.pass)
         job->first_subpass = subpass_idx;

      cmd_buffer_serialize_job_if_needed(cmd_buffer, job);
   }
}

struct v3dv_job *
v3dv_cmd_buffer_start_job(struct v3dv_cmd_buffer *cmd_buffer,
                          int32_t subpass_idx,
                          enum v3dv_job_type type)
{
   /* Don't create a new job if we can merge the current subpass into
    * the current job.
    */
   if (cmd_buffer->state.pass &&
       subpass_idx != -1 &&
       cmd_buffer_can_merge_subpass(cmd_buffer, subpass_idx)) {
      cmd_buffer->state.job->is_subpass_finish = false;
      return cmd_buffer->state.job;
   }

   /* Ensure we are not starting a new job without finishing a previous one */
   if (cmd_buffer->state.job != NULL)
      v3dv_cmd_buffer_finish_job(cmd_buffer);

   assert(cmd_buffer->state.job == NULL);
   struct v3dv_job *job = vk_zalloc(&cmd_buffer->device->vk.alloc,
                                    sizeof(struct v3dv_job), 8,
                                    VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);

   if (!job) {
      fprintf(stderr, "Error: failed to allocate CPU memory for job\n");
      v3dv_flag_oom(cmd_buffer, NULL);
      return NULL;
   }

   v3dv_job_init(job, type, cmd_buffer->device, cmd_buffer, subpass_idx);
   cmd_buffer->state.job = job;

   return job;
}

static VkResult
cmd_buffer_reset(struct v3dv_cmd_buffer *cmd_buffer,
                 VkCommandBufferResetFlags flags)
{
   if (cmd_buffer->status != V3DV_CMD_BUFFER_STATUS_INITIALIZED) {
      struct v3dv_device *device = cmd_buffer->device;
      struct v3dv_cmd_pool *pool = cmd_buffer->pool;
      VkCommandBufferLevel level = cmd_buffer->level;

      /* cmd_buffer_init below will re-add the command buffer to the pool
       * so remove it here so we don't end up adding it again.
       */
      list_del(&cmd_buffer->pool_link);

      /* FIXME: For now we always free all resources as if
       * VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT was set.
       */
      if (cmd_buffer->status != V3DV_CMD_BUFFER_STATUS_NEW)
         cmd_buffer_free_resources(cmd_buffer);

      cmd_buffer_init(cmd_buffer, device, pool, level);
   }

   assert(cmd_buffer->status == V3DV_CMD_BUFFER_STATUS_INITIALIZED);
   return VK_SUCCESS;
}

VkResult
v3dv_AllocateCommandBuffers(VkDevice _device,
                            const VkCommandBufferAllocateInfo *pAllocateInfo,
                            VkCommandBuffer *pCommandBuffers)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_cmd_pool, pool, pAllocateInfo->commandPool);

   VkResult result = VK_SUCCESS;
   uint32_t i;

   for (i = 0; i < pAllocateInfo->commandBufferCount; i++) {
      result = cmd_buffer_create(device, pool, pAllocateInfo->level,
                                 &pCommandBuffers[i]);
      if (result != VK_SUCCESS)
         break;
   }

   if (result != VK_SUCCESS) {
      v3dv_FreeCommandBuffers(_device, pAllocateInfo->commandPool,
                              i, pCommandBuffers);
      for (i = 0; i < pAllocateInfo->commandBufferCount; i++)
         pCommandBuffers[i] = VK_NULL_HANDLE;
   }

   return result;
}

void
v3dv_FreeCommandBuffers(VkDevice device,
                        VkCommandPool commandPool,
                        uint32_t commandBufferCount,
                        const VkCommandBuffer *pCommandBuffers)
{
   for (uint32_t i = 0; i < commandBufferCount; i++) {
      V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, pCommandBuffers[i]);

      if (!cmd_buffer)
         continue;

      cmd_buffer_destroy(cmd_buffer);
   }
}

void
v3dv_DestroyCommandPool(VkDevice _device,
                        VkCommandPool commandPool,
                        const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_cmd_pool, pool, commandPool);

   if (!pool)
      return;

   list_for_each_entry_safe(struct v3dv_cmd_buffer, cmd_buffer,
                            &pool->cmd_buffers, pool_link) {
      cmd_buffer_destroy(cmd_buffer);
   }

   vk_object_free(&device->vk, pAllocator, pool);
}

void
v3dv_TrimCommandPool(VkDevice device,
                     VkCommandPool commandPool,
                     VkCommandPoolTrimFlags flags)
{
   /* We don't need to do anything here, our command pools never hold on to
    * any resources from command buffers that are freed or reset.
    */
}


static void
cmd_buffer_subpass_handle_pending_resolves(struct v3dv_cmd_buffer *cmd_buffer)
{
   assert(cmd_buffer->state.subpass_idx < cmd_buffer->state.pass->subpass_count);
   const struct v3dv_render_pass *pass = cmd_buffer->state.pass;
   const struct v3dv_subpass *subpass =
      &pass->subpasses[cmd_buffer->state.subpass_idx];

   if (!subpass->resolve_attachments)
      return;

   struct v3dv_framebuffer *fb = cmd_buffer->state.framebuffer;

   /* At this point we have already ended the current subpass and now we are
    * about to emit vkCmdResolveImage calls to get the resolves we can't handle
    * handle in the subpass RCL.
    *
    * vkCmdResolveImage is not supposed to be called inside a render pass so
    * before we call that we need to make sure our command buffer state reflects
    * that we are no longer in a subpass by finishing the current job and
    * resetting the framebuffer and render pass state temporarily and then
    * restoring it after we are done with the resolves.
    */
   if (cmd_buffer->state.job)
      v3dv_cmd_buffer_finish_job(cmd_buffer);
   struct v3dv_framebuffer *restore_fb = cmd_buffer->state.framebuffer;
   struct v3dv_render_pass *restore_pass = cmd_buffer->state.pass;
   uint32_t restore_subpass_idx = cmd_buffer->state.subpass_idx;
   cmd_buffer->state.framebuffer = NULL;
   cmd_buffer->state.pass = NULL;
   cmd_buffer->state.subpass_idx = -1;

   VkCommandBuffer cmd_buffer_handle = v3dv_cmd_buffer_to_handle(cmd_buffer);
   for (uint32_t i = 0; i < subpass->color_count; i++) {
      const uint32_t src_attachment_idx =
         subpass->color_attachments[i].attachment;
      if (src_attachment_idx == VK_ATTACHMENT_UNUSED)
         continue;

      if (pass->attachments[src_attachment_idx].use_tlb_resolve)
         continue;

      const uint32_t dst_attachment_idx =
         subpass->resolve_attachments[i].attachment;
      if (dst_attachment_idx == VK_ATTACHMENT_UNUSED)
         continue;

      struct v3dv_image_view *src_iview = fb->attachments[src_attachment_idx];
      struct v3dv_image_view *dst_iview = fb->attachments[dst_attachment_idx];

      VkImageResolve region = {
         .srcSubresource = {
            VK_IMAGE_ASPECT_COLOR_BIT,
            src_iview->base_level,
            src_iview->first_layer,
            src_iview->last_layer - src_iview->first_layer + 1,
         },
         .srcOffset = { 0, 0, 0 },
         .dstSubresource =  {
            VK_IMAGE_ASPECT_COLOR_BIT,
            dst_iview->base_level,
            dst_iview->first_layer,
            dst_iview->last_layer - dst_iview->first_layer + 1,
         },
         .dstOffset = { 0, 0, 0 },
         .extent = src_iview->image->extent,
      };

      VkImage src_image_handle =
         v3dv_image_to_handle((struct v3dv_image *) src_iview->image);
      VkImage dst_image_handle =
         v3dv_image_to_handle((struct v3dv_image *) dst_iview->image);
      v3dv_CmdResolveImage(cmd_buffer_handle,
                           src_image_handle,
                           VK_IMAGE_LAYOUT_GENERAL,
                           dst_image_handle,
                           VK_IMAGE_LAYOUT_GENERAL,
                           1, &region);
   }

   cmd_buffer->state.framebuffer = restore_fb;
   cmd_buffer->state.pass = restore_pass;
   cmd_buffer->state.subpass_idx = restore_subpass_idx;
}

static VkResult
cmd_buffer_begin_render_pass_secondary(
   struct v3dv_cmd_buffer *cmd_buffer,
   const VkCommandBufferInheritanceInfo *inheritance_info)
{
   assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY);
   assert(cmd_buffer->usage_flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT);
   assert(inheritance_info);

   cmd_buffer->state.pass =
      v3dv_render_pass_from_handle(inheritance_info->renderPass);
   assert(cmd_buffer->state.pass);

   cmd_buffer->state.framebuffer =
      v3dv_framebuffer_from_handle(inheritance_info->framebuffer);

   assert(inheritance_info->subpass < cmd_buffer->state.pass->subpass_count);
   cmd_buffer->state.subpass_idx = inheritance_info->subpass;

   cmd_buffer->state.inheritance.occlusion_query_enable =
      inheritance_info->occlusionQueryEnable;

   /* Secondaries that execute inside a render pass won't start subpasses
    * so we want to create a job for them here.
    */
   struct v3dv_job *job =
      v3dv_cmd_buffer_start_job(cmd_buffer, inheritance_info->subpass,
                                V3DV_JOB_TYPE_GPU_CL_SECONDARY);
   if (!job) {
      v3dv_flag_oom(cmd_buffer, NULL);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   /* Secondary command buffers don't know about the render area, but our
    * scissor setup accounts for it, so let's make sure we make it large
    * enough that it doesn't actually constrain any rendering. This should
    * be fine, since the Vulkan spec states:
    *
    *    "The application must ensure (using scissor if necessary) that all
    *     rendering is contained within the render area."
    *
    * FIXME: setup constants for the max framebuffer dimensions and use them
    * here and when filling in VkPhysicalDeviceLimits.
    */
   const struct v3dv_framebuffer *framebuffer = cmd_buffer->state.framebuffer;
   cmd_buffer->state.render_area.offset.x = 0;
   cmd_buffer->state.render_area.offset.y = 0;
   cmd_buffer->state.render_area.extent.width =
      framebuffer ? framebuffer->width : 4096;
   cmd_buffer->state.render_area.extent.height =
      framebuffer ? framebuffer->height : 4096;

   return VK_SUCCESS;
}

VkResult
v3dv_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                        const VkCommandBufferBeginInfo *pBeginInfo)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);

   /* If this is the first vkBeginCommandBuffer, we must initialize the
    * command buffer's state. Otherwise, we must reset its state. In both
    * cases we reset it.
    */
   VkResult result = cmd_buffer_reset(cmd_buffer, 0);
   if (result != VK_SUCCESS)
      return result;

   assert(cmd_buffer->status == V3DV_CMD_BUFFER_STATUS_INITIALIZED);

   cmd_buffer->usage_flags = pBeginInfo->flags;

   if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY) {
      if (pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT) {
         result =
            cmd_buffer_begin_render_pass_secondary(cmd_buffer,
                                                   pBeginInfo->pInheritanceInfo);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   cmd_buffer->status = V3DV_CMD_BUFFER_STATUS_RECORDING;

   return VK_SUCCESS;
}

VkResult
v3dv_ResetCommandBuffer(VkCommandBuffer commandBuffer,
                        VkCommandBufferResetFlags flags)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   return cmd_buffer_reset(cmd_buffer, flags);
}

VkResult
v3dv_ResetCommandPool(VkDevice device,
                      VkCommandPool commandPool,
                      VkCommandPoolResetFlags flags)
{
   V3DV_FROM_HANDLE(v3dv_cmd_pool, pool, commandPool);

   VkCommandBufferResetFlags reset_flags = 0;
   if (flags & VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT)
      reset_flags = VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT;
   list_for_each_entry_safe(struct v3dv_cmd_buffer, cmd_buffer,
                            &pool->cmd_buffers, pool_link) {
      cmd_buffer_reset(cmd_buffer, reset_flags);
   }

   return VK_SUCCESS;
}

static void
emit_clip_window(struct v3dv_job *job, const VkRect2D *rect)
{
   assert(job);

   v3dv_cl_ensure_space_with_branch(&job->bcl, cl_packet_length(CLIP_WINDOW));
   v3dv_return_if_oom(NULL, job);

   cl_emit(&job->bcl, CLIP_WINDOW, clip) {
      clip.clip_window_left_pixel_coordinate = rect->offset.x;
      clip.clip_window_bottom_pixel_coordinate = rect->offset.y;
      clip.clip_window_width_in_pixels = rect->extent.width;
      clip.clip_window_height_in_pixels = rect->extent.height;
   }
}

static void
cmd_buffer_update_tile_alignment(struct v3dv_cmd_buffer *cmd_buffer)
{
   /* Render areas and scissor/viewport are only relevant inside render passes,
    * otherwise we are dealing with transfer operations where these elements
    * don't apply.
    */
   assert(cmd_buffer->state.pass);
   const VkRect2D *rect = &cmd_buffer->state.render_area;

   /* We should only call this at the beginning of a subpass so we should
    * always have framebuffer information available.
    */
   assert(cmd_buffer->state.framebuffer);
   cmd_buffer->state.tile_aligned_render_area =
      v3dv_subpass_area_is_tile_aligned(rect,
                                        cmd_buffer->state.framebuffer,
                                        cmd_buffer->state.pass,
                                        cmd_buffer->state.subpass_idx);

   if (!cmd_buffer->state.tile_aligned_render_area) {
      perf_debug("Render area for subpass %d of render pass %p doesn't "
                 "match render pass granularity.\n",
                 cmd_buffer->state.subpass_idx, cmd_buffer->state.pass);
   }
}

void
v3dv_get_hw_clear_color(const VkClearColorValue *color,
                        uint32_t internal_type,
                        uint32_t internal_size,
                        uint32_t *hw_color)
{
   union util_color uc;
   switch (internal_type) {
   case V3D_INTERNAL_TYPE_8:
      util_pack_color(color->float32, PIPE_FORMAT_R8G8B8A8_UNORM, &uc);
      memcpy(hw_color, uc.ui, internal_size);
   break;
   case V3D_INTERNAL_TYPE_8I:
   case V3D_INTERNAL_TYPE_8UI:
      hw_color[0] = ((color->uint32[0] & 0xff) |
                     (color->uint32[1] & 0xff) << 8 |
                     (color->uint32[2] & 0xff) << 16 |
                     (color->uint32[3] & 0xff) << 24);
   break;
   case V3D_INTERNAL_TYPE_16F:
      util_pack_color(color->float32, PIPE_FORMAT_R16G16B16A16_FLOAT, &uc);
      memcpy(hw_color, uc.ui, internal_size);
   break;
   case V3D_INTERNAL_TYPE_16I:
   case V3D_INTERNAL_TYPE_16UI:
      hw_color[0] = ((color->uint32[0] & 0xffff) | color->uint32[1] << 16);
      hw_color[1] = ((color->uint32[2] & 0xffff) | color->uint32[3] << 16);
   break;
   case V3D_INTERNAL_TYPE_32F:
   case V3D_INTERNAL_TYPE_32I:
   case V3D_INTERNAL_TYPE_32UI:
      memcpy(hw_color, color->uint32, internal_size);
      break;
   }
}

static void
cmd_buffer_state_set_attachment_clear_color(struct v3dv_cmd_buffer *cmd_buffer,
                                            uint32_t attachment_idx,
                                            const VkClearColorValue *color)
{
   assert(attachment_idx < cmd_buffer->state.pass->attachment_count);

   const struct v3dv_render_pass_attachment *attachment =
      &cmd_buffer->state.pass->attachments[attachment_idx];

   uint32_t internal_type, internal_bpp;
   const struct v3dv_format *format = v3dv_get_format(attachment->desc.format);
   v3dv_get_internal_type_bpp_for_output_format(format->rt_type,
                                                &internal_type,
                                                &internal_bpp);

   uint32_t internal_size = 4 << internal_bpp;

   struct v3dv_cmd_buffer_attachment_state *attachment_state =
      &cmd_buffer->state.attachments[attachment_idx];

   v3dv_get_hw_clear_color(color, internal_type, internal_size,
                           &attachment_state->clear_value.color[0]);

   attachment_state->vk_clear_value.color = *color;
}

static void
cmd_buffer_state_set_attachment_clear_depth_stencil(
   struct v3dv_cmd_buffer *cmd_buffer,
   uint32_t attachment_idx,
   bool clear_depth, bool clear_stencil,
   const VkClearDepthStencilValue *ds)
{
   struct v3dv_cmd_buffer_attachment_state *attachment_state =
      &cmd_buffer->state.attachments[attachment_idx];

   if (clear_depth)
      attachment_state->clear_value.z = ds->depth;

   if (clear_stencil)
      attachment_state->clear_value.s = ds->stencil;

   attachment_state->vk_clear_value.depthStencil = *ds;
}

static void
cmd_buffer_state_set_clear_values(struct v3dv_cmd_buffer *cmd_buffer,
                                  uint32_t count, const VkClearValue *values)
{
   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   const struct v3dv_render_pass *pass = state->pass;

   /* There could be less clear values than attachments in the render pass, in
    * which case we only want to process as many as we have, or there could be
    * more, in which case we want to ignore those for which we don't have a
    * corresponding attachment.
    */
   count = MIN2(count, pass->attachment_count);
   for (uint32_t i = 0; i < count; i++) {
      const struct v3dv_render_pass_attachment *attachment =
         &pass->attachments[i];

      if (attachment->desc.loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR)
         continue;

      VkImageAspectFlags aspects = vk_format_aspects(attachment->desc.format);
      if (aspects & VK_IMAGE_ASPECT_COLOR_BIT) {
         cmd_buffer_state_set_attachment_clear_color(cmd_buffer, i,
                                                     &values[i].color);
      } else if (aspects & (VK_IMAGE_ASPECT_DEPTH_BIT |
                            VK_IMAGE_ASPECT_STENCIL_BIT)) {
         cmd_buffer_state_set_attachment_clear_depth_stencil(
            cmd_buffer, i,
            aspects & VK_IMAGE_ASPECT_DEPTH_BIT,
            aspects & VK_IMAGE_ASPECT_STENCIL_BIT,
            &values[i].depthStencil);
      }
   }
}

static void
cmd_buffer_init_render_pass_attachment_state(struct v3dv_cmd_buffer *cmd_buffer,
                                             const VkRenderPassBeginInfo *pRenderPassBegin)
{
   cmd_buffer_state_set_clear_values(cmd_buffer,
                                     pRenderPassBegin->clearValueCount,
                                     pRenderPassBegin->pClearValues);
}

static void
cmd_buffer_ensure_render_pass_attachment_state(struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   const struct v3dv_render_pass *pass = state->pass;

   if (state->attachment_alloc_count < pass->attachment_count) {
      if (state->attachments > 0) {
         assert(state->attachment_alloc_count > 0);
         vk_free(&cmd_buffer->device->vk.alloc, state->attachments);
      }

      uint32_t size = sizeof(struct v3dv_cmd_buffer_attachment_state) *
                      pass->attachment_count;
      state->attachments = vk_zalloc(&cmd_buffer->device->vk.alloc, size, 8,
                                     VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!state->attachments) {
         v3dv_flag_oom(cmd_buffer, NULL);
         return;
      }
      state->attachment_alloc_count = pass->attachment_count;
   }

   assert(state->attachment_alloc_count >= pass->attachment_count);
}

void
v3dv_CmdBeginRenderPass(VkCommandBuffer commandBuffer,
                        const VkRenderPassBeginInfo *pRenderPassBegin,
                        VkSubpassContents contents)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_render_pass, pass, pRenderPassBegin->renderPass);
   V3DV_FROM_HANDLE(v3dv_framebuffer, framebuffer, pRenderPassBegin->framebuffer);

   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   state->pass = pass;
   state->framebuffer = framebuffer;

   cmd_buffer_ensure_render_pass_attachment_state(cmd_buffer);
   v3dv_return_if_oom(cmd_buffer, NULL);

   cmd_buffer_init_render_pass_attachment_state(cmd_buffer, pRenderPassBegin);

   state->render_area = pRenderPassBegin->renderArea;

   /* If our render area is smaller than the current clip window we will have
    * to emit a new clip window to constraint it to the render area.
    */
   uint32_t min_render_x = state->render_area.offset.x;
   uint32_t min_render_y = state->render_area.offset.x;
   uint32_t max_render_x = min_render_x + state->render_area.extent.width - 1;
   uint32_t max_render_y = min_render_y + state->render_area.extent.height - 1;
   uint32_t min_clip_x = state->clip_window.offset.x;
   uint32_t min_clip_y = state->clip_window.offset.y;
   uint32_t max_clip_x = min_clip_x + state->clip_window.extent.width - 1;
   uint32_t max_clip_y = min_clip_y + state->clip_window.extent.height - 1;
   if (min_render_x > min_clip_x || min_render_y > min_clip_y ||
       max_render_x < max_clip_x || max_render_y < max_clip_y) {
      state->dirty |= V3DV_CMD_DIRTY_SCISSOR;
   }

   /* Setup for first subpass */
   v3dv_cmd_buffer_subpass_start(cmd_buffer, 0);
}

void
v3dv_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);

   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   assert(state->subpass_idx < state->pass->subpass_count - 1);

   /* Finish the previous subpass */
   v3dv_cmd_buffer_subpass_finish(cmd_buffer);
   cmd_buffer_subpass_handle_pending_resolves(cmd_buffer);

   /* Start the next subpass */
   v3dv_cmd_buffer_subpass_start(cmd_buffer, state->subpass_idx + 1);
}

void
v3dv_render_pass_setup_render_target(struct v3dv_cmd_buffer *cmd_buffer,
                                     int rt,
                                     uint32_t *rt_bpp,
                                     uint32_t *rt_type,
                                     uint32_t *rt_clamp)
{
   const struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;

   assert(state->subpass_idx < state->pass->subpass_count);
   const struct v3dv_subpass *subpass =
      &state->pass->subpasses[state->subpass_idx];

   if (rt >= subpass->color_count)
      return;

   struct v3dv_subpass_attachment *attachment = &subpass->color_attachments[rt];
   const uint32_t attachment_idx = attachment->attachment;
   if (attachment_idx == VK_ATTACHMENT_UNUSED)
      return;

   const struct v3dv_framebuffer *framebuffer = state->framebuffer;
   assert(attachment_idx < framebuffer->attachment_count);
   struct v3dv_image_view *iview = framebuffer->attachments[attachment_idx];
   assert(iview->aspects & VK_IMAGE_ASPECT_COLOR_BIT);

   *rt_bpp = iview->internal_bpp;
   *rt_type = iview->internal_type;
   *rt_clamp =vk_format_is_int(iview->vk_format) ?
      V3D_RENDER_TARGET_CLAMP_INT : V3D_RENDER_TARGET_CLAMP_NONE;
}

static void
cmd_buffer_render_pass_emit_load(struct v3dv_cmd_buffer *cmd_buffer,
                                 struct v3dv_cl *cl,
                                 struct v3dv_image_view *iview,
                                 uint32_t layer,
                                 uint32_t buffer)
{
   const struct v3dv_image *image = iview->image;
   const struct v3d_resource_slice *slice = &image->slices[iview->base_level];
   uint32_t layer_offset = v3dv_layer_offset(image,
                                             iview->base_level,
                                             iview->first_layer + layer);

   cl_emit(cl, LOAD_TILE_BUFFER_GENERAL, load) {
      load.buffer_to_load = buffer;
      load.address = v3dv_cl_address(image->mem->bo, layer_offset);

      load.input_image_format = iview->format->rt_type;
      load.r_b_swap = iview->swap_rb;
      load.memory_format = slice->tiling;

      if (slice->tiling == VC5_TILING_UIF_NO_XOR ||
          slice->tiling == VC5_TILING_UIF_XOR) {
         load.height_in_ub_or_stride =
            slice->padded_height_of_output_image_in_uif_blocks;
      } else if (slice->tiling == VC5_TILING_RASTER) {
         load.height_in_ub_or_stride = slice->stride;
      }

      if (image->samples > VK_SAMPLE_COUNT_1_BIT)
         load.decimate_mode = V3D_DECIMATE_MODE_ALL_SAMPLES;
      else
         load.decimate_mode = V3D_DECIMATE_MODE_SAMPLE_0;
   }
}

static bool
check_needs_load(const struct v3dv_cmd_buffer_state *state,
                 VkImageAspectFlags aspect,
                 uint32_t att_first_subpass_idx,
                 VkAttachmentLoadOp load_op)
{
   /* We call this with image->aspects & aspect, so 0 means the aspect we are
    * testing does not exist in the image.
    */
   if (!aspect)
      return false;

   /* Attachment load operations apply on the first subpass that uses the
    * attachment, otherwise we always need to load.
    */
   if (state->job->first_subpass > att_first_subpass_idx)
      return true;

   /* If the job is continuing a subpass started in another job, we always
    * need to load.
    */
   if (state->job->is_subpass_continue)
      return true;

   /* If the area is not aligned to tile boundaries, we always need to load */
   if (!state->tile_aligned_render_area)
      return true;

   /* The attachment load operations must be LOAD */
   return load_op == VK_ATTACHMENT_LOAD_OP_LOAD;
}

static bool
check_needs_clear(const struct v3dv_cmd_buffer_state *state,
                  VkImageAspectFlags aspect,
                  uint32_t att_first_subpass_idx,
                  VkAttachmentLoadOp load_op,
                  bool do_clear_with_draw)
{
   /* We call this with image->aspects & aspect, so 0 means the aspect we are
    * testing does not exist in the image.
    */
   if (!aspect)
      return false;

   /* If the aspect needs to be cleared with a draw call then we won't emit
    * the clear here.
    */
   if (do_clear_with_draw)
      return false;

   /* If this is resuming a subpass started with another job, then attachment
    * load operations don't apply.
    */
   if (state->job->is_subpass_continue)
      return false;

   /* If the render area is not aligned to tile boudaries we can't use the
    * TLB for a clear.
    */
   if (!state->tile_aligned_render_area)
      return false;

   /* If this job is running in a subpass other than the first subpass in
    * which this attachment is used then attachment load operations don't apply.
    */
   if (state->job->first_subpass != att_first_subpass_idx)
      return false;

   /* The attachment load operation must be CLEAR */
   return load_op == VK_ATTACHMENT_LOAD_OP_CLEAR;
}

static bool
check_needs_store(const struct v3dv_cmd_buffer_state *state,
                  VkImageAspectFlags aspect,
                  uint32_t att_last_subpass_idx,
                  VkAttachmentStoreOp store_op)
{
   /* We call this with image->aspects & aspect, so 0 means the aspect we are
    * testing does not exist in the image.
    */
   if (!aspect)
       return false;

   /* Attachment store operations only apply on the last subpass where the
    * attachment is used, in other subpasses we always need to store.
    */
   if (state->subpass_idx < att_last_subpass_idx)
      return true;

   /* Attachment store operations only apply on the last job we emit on the the
    * last subpass where the attachment is used, otherwise we always need to
    * store.
    */
   if (!state->job->is_subpass_finish)
      return true;

   /* The attachment store operation must be STORE */
   return store_op == VK_ATTACHMENT_STORE_OP_STORE;
}

static void
cmd_buffer_render_pass_emit_loads(struct v3dv_cmd_buffer *cmd_buffer,
                                  struct v3dv_cl *cl,
                                  uint32_t layer)
{
   const struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   const struct v3dv_framebuffer *framebuffer = state->framebuffer;
   const struct v3dv_render_pass *pass = state->pass;
   const struct v3dv_subpass *subpass = &pass->subpasses[state->subpass_idx];

   for (uint32_t i = 0; i < subpass->color_count; i++) {
      uint32_t attachment_idx = subpass->color_attachments[i].attachment;

      if (attachment_idx == VK_ATTACHMENT_UNUSED)
         continue;

      const struct v3dv_render_pass_attachment *attachment =
         &state->pass->attachments[attachment_idx];

      /* According to the Vulkan spec:
       *
       *    "The load operation for each sample in an attachment happens before
       *     any recorded command which accesses the sample in the first subpass
       *     where the attachment is used."
       *
       * If the load operation is CLEAR, we must only clear once on the first
       * subpass that uses the attachment (and in that case we don't LOAD).
       * After that, we always want to load so we don't lose any rendering done
       * by a previous subpass to the same attachment. We also want to load
       * if the current job is continuing subpass work started by a previous
       * job, for the same reason.
       *
       * If the render area is not aligned to tile boundaries then we have
       * tiles which are partially covered by it. In this case, we need to
       * load the tiles so we can preserve the pixels that are outside the
       * render area for any such tiles.
       */
      bool needs_load = check_needs_load(state,
                                         VK_IMAGE_ASPECT_COLOR_BIT,
                                         attachment->first_subpass,
                                         attachment->desc.loadOp);
      if (needs_load) {
         struct v3dv_image_view *iview = framebuffer->attachments[attachment_idx];
         cmd_buffer_render_pass_emit_load(cmd_buffer, cl, iview,
                                          layer, RENDER_TARGET_0 + i);
      }
   }

   uint32_t ds_attachment_idx = subpass->ds_attachment.attachment;
   if (ds_attachment_idx != VK_ATTACHMENT_UNUSED) {
      const struct v3dv_render_pass_attachment *ds_attachment =
         &state->pass->attachments[ds_attachment_idx];

      const VkImageAspectFlags ds_aspects =
         vk_format_aspects(ds_attachment->desc.format);

      const bool needs_depth_load =
         check_needs_load(state,
                          ds_aspects & VK_IMAGE_ASPECT_DEPTH_BIT,
                          ds_attachment->first_subpass,
                          ds_attachment->desc.loadOp);

      const bool needs_stencil_load =
         check_needs_load(state,
                          ds_aspects & VK_IMAGE_ASPECT_STENCIL_BIT,
                          ds_attachment->first_subpass,
                          ds_attachment->desc.stencilLoadOp);

      if (needs_depth_load || needs_stencil_load) {
         struct v3dv_image_view *iview =
            framebuffer->attachments[ds_attachment_idx];
         /* From the Vulkan spec:
          *
          *   "When an image view of a depth/stencil image is used as a
          *   depth/stencil framebuffer attachment, the aspectMask is ignored
          *   and both depth and stencil image subresources are used."
          *
          * So we ignore the aspects from the subresource range of the image
          * view for the depth/stencil attachment, but we still need to restrict
          * the to aspects compatible with the render pass and the image.
          */
         const uint32_t zs_buffer =
            v3dv_zs_buffer(needs_depth_load, needs_stencil_load);
         cmd_buffer_render_pass_emit_load(cmd_buffer, cl,
                                          iview, layer, zs_buffer);
      }
   }

   cl_emit(cl, END_OF_LOADS, end);
}

static void
cmd_buffer_render_pass_emit_store(struct v3dv_cmd_buffer *cmd_buffer,
                                  struct v3dv_cl *cl,
                                  uint32_t attachment_idx,
                                  uint32_t layer,
                                  uint32_t buffer,
                                  bool clear,
                                  bool is_multisample_resolve)
{
   const struct v3dv_image_view *iview =
      cmd_buffer->state.framebuffer->attachments[attachment_idx];
   const struct v3dv_image *image = iview->image;
   const struct v3d_resource_slice *slice = &image->slices[iview->base_level];
   uint32_t layer_offset = v3dv_layer_offset(image,
                                             iview->base_level,
                                             iview->first_layer + layer);

   cl_emit(cl, STORE_TILE_BUFFER_GENERAL, store) {
      store.buffer_to_store = buffer;
      store.address = v3dv_cl_address(image->mem->bo, layer_offset);
      store.clear_buffer_being_stored = clear;

      store.output_image_format = iview->format->rt_type;
      store.r_b_swap = iview->swap_rb;
      store.memory_format = slice->tiling;

      if (slice->tiling == VC5_TILING_UIF_NO_XOR ||
          slice->tiling == VC5_TILING_UIF_XOR) {
         store.height_in_ub_or_stride =
            slice->padded_height_of_output_image_in_uif_blocks;
      } else if (slice->tiling == VC5_TILING_RASTER) {
         store.height_in_ub_or_stride = slice->stride;
      }

      if (image->samples > VK_SAMPLE_COUNT_1_BIT)
         store.decimate_mode = V3D_DECIMATE_MODE_ALL_SAMPLES;
      else if (is_multisample_resolve)
         store.decimate_mode = V3D_DECIMATE_MODE_4X;
      else
         store.decimate_mode = V3D_DECIMATE_MODE_SAMPLE_0;
   }
}

static void
cmd_buffer_render_pass_emit_stores(struct v3dv_cmd_buffer *cmd_buffer,
                                   struct v3dv_cl *cl,
                                   uint32_t layer)
{
   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   const struct v3dv_subpass *subpass =
      &state->pass->subpasses[state->subpass_idx];

   bool has_stores = false;
   bool use_global_zs_clear = false;
   bool use_global_rt_clear = false;

   /* FIXME: separate stencil */
   uint32_t ds_attachment_idx = subpass->ds_attachment.attachment;
   if (ds_attachment_idx != VK_ATTACHMENT_UNUSED) {
      const struct v3dv_render_pass_attachment *ds_attachment =
         &state->pass->attachments[ds_attachment_idx];

      assert(state->job->first_subpass >= ds_attachment->first_subpass);
      assert(state->subpass_idx >= ds_attachment->first_subpass);
      assert(state->subpass_idx <= ds_attachment->last_subpass);

      /* From the Vulkan spec, VkImageSubresourceRange:
       *
       *   "When an image view of a depth/stencil image is used as a
       *   depth/stencil framebuffer attachment, the aspectMask is ignored
       *   and both depth and stencil image subresources are used."
       *
       * So we ignore the aspects from the subresource range of the image
       * view for the depth/stencil attachment, but we still need to restrict
       * the to aspects compatible with the render pass and the image.
       */
      const VkImageAspectFlags aspects =
         vk_format_aspects(ds_attachment->desc.format);

      /* Only clear once on the first subpass that uses the attachment */
      bool needs_depth_clear =
         check_needs_clear(state,
                           aspects & VK_IMAGE_ASPECT_DEPTH_BIT,
                           ds_attachment->first_subpass,
                           ds_attachment->desc.loadOp,
                           subpass->do_depth_clear_with_draw);

      bool needs_stencil_clear =
         check_needs_clear(state,
                           aspects & VK_IMAGE_ASPECT_STENCIL_BIT,
                           ds_attachment->first_subpass,
                           ds_attachment->desc.stencilLoadOp,
                           subpass->do_stencil_clear_with_draw);

      /* Skip the last store if it is not required */
      bool needs_depth_store =
         check_needs_store(state,
                           aspects & VK_IMAGE_ASPECT_DEPTH_BIT,
                           ds_attachment->last_subpass,
                           ds_attachment->desc.storeOp);

      bool needs_stencil_store =
         check_needs_store(state,
                           aspects & VK_IMAGE_ASPECT_STENCIL_BIT,
                           ds_attachment->last_subpass,
                           ds_attachment->desc.stencilStoreOp);

      /* GFXH-1689: The per-buffer store command's clear buffer bit is broken
       * for depth/stencil.
       *
       * There used to be some confusion regarding the Clear Tile Buffers
       * Z/S bit also being broken, but we confirmed with Broadcom that this
       * is not the case, it was just that some other hardware bugs (that we
       * need to work around, such as GFXH-1461) could cause this bit to behave
       * incorrectly.
       *
       * There used to be another issue where the RTs bit in the Clear Tile
       * Buffers packet also cleared Z/S, but Broadcom confirmed this is
       * fixed since V3D 4.1.
       *
       * So if we have to emit a clear of depth or stencil we don't use
       * the per-buffer store clear bit, even if we need to store the buffers,
       * instead we always have to use the Clear Tile Buffers Z/S bit.
       * If we have configured the job to do early Z/S clearing, then we
       * don't want to emit any Clear Tile Buffers command at all here.
       *
       * Note that GFXH-1689 is not reproduced in the simulator, where
       * using the clear buffer bit in depth/stencil stores works fine.
       */
      use_global_zs_clear = !state->job->early_zs_clear &&
                            (needs_depth_clear || needs_stencil_clear);
      if (needs_depth_store || needs_stencil_store) {
         const uint32_t zs_buffer =
            v3dv_zs_buffer(needs_depth_store, needs_stencil_store);
         cmd_buffer_render_pass_emit_store(cmd_buffer, cl,
                                           ds_attachment_idx, layer,
                                           zs_buffer, false, false);
         has_stores = true;
      }
   }

   for (uint32_t i = 0; i < subpass->color_count; i++) {
      uint32_t attachment_idx = subpass->color_attachments[i].attachment;

      if (attachment_idx == VK_ATTACHMENT_UNUSED)
         continue;

      const struct v3dv_render_pass_attachment *attachment =
         &state->pass->attachments[attachment_idx];

      assert(state->job->first_subpass >= attachment->first_subpass);
      assert(state->subpass_idx >= attachment->first_subpass);
      assert(state->subpass_idx <= attachment->last_subpass);

      /* Only clear once on the first subpass that uses the attachment */
      bool needs_clear =
         check_needs_clear(state,
                           VK_IMAGE_ASPECT_COLOR_BIT,
                           attachment->first_subpass,
                           attachment->desc.loadOp,
                           false);

      /* Skip the last store if it is not required  */
      bool needs_store =
         check_needs_store(state,
                           VK_IMAGE_ASPECT_COLOR_BIT,
                           attachment->last_subpass,
                           attachment->desc.storeOp);

      /* If we need to resolve this attachment emit that store first. Notice
       * that we must not request a tile buffer clear here in that case, since
       * that would clear the tile buffer before we get to emit the actual
       * color attachment store below, since the clear happens after the
       * store is completed.
       *
       * If the attachment doesn't support TLB resolves then we will have to
       * fallback to doing the resolve in a shader separately after this
       * job, so we will need to store the multisampled sttachment even if that
       * wansn't requested by the client.
       */
      const bool needs_resolve =
         subpass->resolve_attachments &&
         subpass->resolve_attachments[i].attachment != VK_ATTACHMENT_UNUSED;
      if (needs_resolve && attachment->use_tlb_resolve) {
         const uint32_t resolve_attachment_idx =
            subpass->resolve_attachments[i].attachment;
         cmd_buffer_render_pass_emit_store(cmd_buffer, cl,
                                           resolve_attachment_idx, layer,
                                           RENDER_TARGET_0 + i,
                                           false, true);
         has_stores = true;
      } else if (needs_resolve) {
         needs_store = true;
      }

      /* Emit the color attachment store if needed */
      if (needs_store) {
         cmd_buffer_render_pass_emit_store(cmd_buffer, cl,
                                           attachment_idx, layer,
                                           RENDER_TARGET_0 + i,
                                           needs_clear && !use_global_rt_clear,
                                           false);
         has_stores = true;
      } else if (needs_clear) {
         use_global_rt_clear = true;
      }
   }

   /* We always need to emit at least one dummy store */
   if (!has_stores) {
      cl_emit(cl, STORE_TILE_BUFFER_GENERAL, store) {
         store.buffer_to_store = NONE;
      }
   }

   /* If we have any depth/stencil clears we can't use the per-buffer clear
    * bit and instead we have to emit a single clear of all tile buffers.
    */
   if (use_global_zs_clear || use_global_rt_clear) {
      cl_emit(cl, CLEAR_TILE_BUFFERS, clear) {
         clear.clear_z_stencil_buffer = use_global_zs_clear;
         clear.clear_all_render_targets = use_global_rt_clear;
      }
   }
}

static void
cmd_buffer_render_pass_emit_per_tile_rcl(struct v3dv_cmd_buffer *cmd_buffer,
                                         uint32_t layer)
{
   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   /* Emit the generic list in our indirect state -- the rcl will just
    * have pointers into it.
    */
   struct v3dv_cl *cl = &job->indirect;
   v3dv_cl_ensure_space(cl, 200, 1);
   v3dv_return_if_oom(cmd_buffer, NULL);

   struct v3dv_cl_reloc tile_list_start = v3dv_cl_get_address(cl);

   cl_emit(cl, TILE_COORDINATES_IMPLICIT, coords);

   cmd_buffer_render_pass_emit_loads(cmd_buffer, cl, layer);

   /* The binner starts out writing tiles assuming that the initial mode
    * is triangles, so make sure that's the case.
    */
   cl_emit(cl, PRIM_LIST_FORMAT, fmt) {
      fmt.primitive_type = LIST_TRIANGLES;
   }

   /* PTB assumes that value to be 0, but hw will not set it. */
   cl_emit(cl, SET_INSTANCEID, set) {
      set.instance_id = 0;
   }

   cl_emit(cl, BRANCH_TO_IMPLICIT_TILE_LIST, branch);

   cmd_buffer_render_pass_emit_stores(cmd_buffer, cl, layer);

   cl_emit(cl, END_OF_TILE_MARKER, end);

   cl_emit(cl, RETURN_FROM_SUB_LIST, ret);

   cl_emit(&job->rcl, START_ADDRESS_OF_GENERIC_TILE_LIST, branch) {
      branch.start = tile_list_start;
      branch.end = v3dv_cl_get_address(cl);
   }
}

static void
cmd_buffer_emit_render_pass_layer_rcl(struct v3dv_cmd_buffer *cmd_buffer,
                                      uint32_t layer)
{
   const struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;

   struct v3dv_job *job = cmd_buffer->state.job;
   struct v3dv_cl *rcl = &job->rcl;

   /* If doing multicore binning, we would need to initialize each
    * core's tile list here.
    */
   const struct v3dv_frame_tiling *tiling = &job->frame_tiling;
   const uint32_t tile_alloc_offset =
      64 * layer * tiling->draw_tiles_x * tiling->draw_tiles_y;
   cl_emit(rcl, MULTICORE_RENDERING_TILE_LIST_SET_BASE, list) {
      list.address = v3dv_cl_address(job->tile_alloc, tile_alloc_offset);
   }

   cl_emit(rcl, MULTICORE_RENDERING_SUPERTILE_CFG, config) {
      config.number_of_bin_tile_lists = 1;
      config.total_frame_width_in_tiles = tiling->draw_tiles_x;
      config.total_frame_height_in_tiles = tiling->draw_tiles_y;

      config.supertile_width_in_tiles = tiling->supertile_width;
      config.supertile_height_in_tiles = tiling->supertile_height;

      config.total_frame_width_in_supertiles =
         tiling->frame_width_in_supertiles;
      config.total_frame_height_in_supertiles =
         tiling->frame_height_in_supertiles;
   }

   /* Start by clearing the tile buffer. */
   cl_emit(rcl, TILE_COORDINATES, coords) {
      coords.tile_column_number = 0;
      coords.tile_row_number = 0;
   }

   /* Emit an initial clear of the tile buffers. This is necessary
    * for any buffers that should be cleared (since clearing
    * normally happens at the *end* of the generic tile list), but
    * it's also nice to clear everything so the first tile doesn't
    * inherit any contents from some previous frame.
    *
    * Also, implement the GFXH-1742 workaround. There's a race in
    * the HW between the RCL updating the TLB's internal type/size
    * and the spawning of the QPU instances using the TLB's current
    * internal type/size. To make sure the QPUs get the right
    * state, we need 1 dummy store in between internal type/size
    * changes on V3D 3.x, and 2 dummy stores on 4.x.
    */
   for (int i = 0; i < 2; i++) {
      if (i > 0)
         cl_emit(rcl, TILE_COORDINATES, coords);
      cl_emit(rcl, END_OF_LOADS, end);
      cl_emit(rcl, STORE_TILE_BUFFER_GENERAL, store) {
         store.buffer_to_store = NONE;
      }
      if (i == 0 && cmd_buffer->state.tile_aligned_render_area) {
         cl_emit(rcl, CLEAR_TILE_BUFFERS, clear) {
            clear.clear_z_stencil_buffer = !job->early_zs_clear;
            clear.clear_all_render_targets = true;
         }
      }
      cl_emit(rcl, END_OF_TILE_MARKER, end);
   }

   cl_emit(rcl, FLUSH_VCD_CACHE, flush);

   cmd_buffer_render_pass_emit_per_tile_rcl(cmd_buffer, layer);

   uint32_t supertile_w_in_pixels =
      tiling->tile_width * tiling->supertile_width;
   uint32_t supertile_h_in_pixels =
      tiling->tile_height * tiling->supertile_height;
   const uint32_t min_x_supertile =
      state->render_area.offset.x / supertile_w_in_pixels;
   const uint32_t min_y_supertile =
      state->render_area.offset.y / supertile_h_in_pixels;

   uint32_t max_render_x = state->render_area.offset.x;
   if (state->render_area.extent.width > 0)
      max_render_x += state->render_area.extent.width - 1;
   uint32_t max_render_y = state->render_area.offset.y;
   if (state->render_area.extent.height > 0)
      max_render_y += state->render_area.extent.height - 1;
   const uint32_t max_x_supertile = max_render_x / supertile_w_in_pixels;
   const uint32_t max_y_supertile = max_render_y / supertile_h_in_pixels;

   for (int y = min_y_supertile; y <= max_y_supertile; y++) {
      for (int x = min_x_supertile; x <= max_x_supertile; x++) {
         cl_emit(rcl, SUPERTILE_COORDINATES, coords) {
            coords.column_number_in_supertiles = x;
            coords.row_number_in_supertiles = y;
         }
      }
   }
}

static void
set_rcl_early_z_config(struct v3dv_job *job,
                       bool *early_z_disable,
                       uint32_t *early_z_test_and_update_direction)
{
   /* If this is true then we have not emitted any draw calls in this job
    * and we don't get any benefits form early Z.
    */
   if (!job->decided_global_ez_enable) {
      assert(job->draw_count == 0);
      *early_z_disable = true;
      return;
   }

   switch (job->first_ez_state) {
   case VC5_EZ_UNDECIDED:
   case VC5_EZ_LT_LE:
      *early_z_disable = false;
      *early_z_test_and_update_direction = EARLY_Z_DIRECTION_LT_LE;
      break;
   case VC5_EZ_GT_GE:
      *early_z_disable = false;
      *early_z_test_and_update_direction = EARLY_Z_DIRECTION_GT_GE;
      break;
   case VC5_EZ_DISABLED:
      *early_z_disable = true;
      break;
   }
}

static void
cmd_buffer_emit_render_pass_rcl(struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   const struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   const struct v3dv_framebuffer *framebuffer = state->framebuffer;

   /* We can't emit the RCL until we have a framebuffer, which we may not have
    * if we are recording a secondary command buffer. In that case, we will
    * have to wait until vkCmdExecuteCommands is called from a primary command
    * buffer.
    */
   if (!framebuffer) {
      assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY);
      return;
   }

   const struct v3dv_frame_tiling *tiling = &job->frame_tiling;

   const uint32_t fb_layers = framebuffer->layers;
   v3dv_cl_ensure_space_with_branch(&job->rcl, 200 +
                                    MAX2(fb_layers, 1) * 256 *
                                    cl_packet_length(SUPERTILE_COORDINATES));
   v3dv_return_if_oom(cmd_buffer, NULL);

   assert(state->subpass_idx < state->pass->subpass_count);
   const struct v3dv_render_pass *pass = state->pass;
   const struct v3dv_subpass *subpass = &pass->subpasses[state->subpass_idx];
   struct v3dv_cl *rcl = &job->rcl;

   /* Comon config must be the first TILE_RENDERING_MODE_CFG and
    * Z_STENCIL_CLEAR_VALUES must be last. The ones in between are optional
    * updates to the previous HW state.
    */
   bool do_early_zs_clear = false;
   const uint32_t ds_attachment_idx = subpass->ds_attachment.attachment;
   cl_emit(rcl, TILE_RENDERING_MODE_CFG_COMMON, config) {
      config.image_width_pixels = framebuffer->width;
      config.image_height_pixels = framebuffer->height;
      config.number_of_render_targets = MAX2(subpass->color_count, 1);
      config.multisample_mode_4x = tiling->msaa;
      config.maximum_bpp_of_all_render_targets = tiling->internal_bpp;

      if (ds_attachment_idx != VK_ATTACHMENT_UNUSED) {
         const struct v3dv_image_view *iview =
            framebuffer->attachments[ds_attachment_idx];
         config.internal_depth_type = iview->internal_type;

         set_rcl_early_z_config(job,
                                &config.early_z_disable,
                                &config.early_z_test_and_update_direction);

         /* Early-Z/S clear can be enabled if the job is clearing and not
          * storing (or loading) depth. If a stencil aspect is also present
          * we have the same requirements for it, however, in this case we
          * can accept stencil loadOp DONT_CARE as well, so instead of
          * checking that stencil is cleared we check that is not loaded.
          *
          * Early-Z/S clearing is independent of Early Z/S testing, so it is
          * possible to enable one but not the other so long as their
          * respective requirements are met.
          */
         struct v3dv_render_pass_attachment *ds_attachment =
            &pass->attachments[ds_attachment_idx];

         const VkImageAspectFlags ds_aspects =
            vk_format_aspects(ds_attachment->desc.format);

         bool needs_depth_clear =
            check_needs_clear(state,
                              ds_aspects & VK_IMAGE_ASPECT_DEPTH_BIT,
                              ds_attachment->first_subpass,
                              ds_attachment->desc.loadOp,
                              subpass->do_depth_clear_with_draw);

         bool needs_depth_store =
            check_needs_store(state,
                              ds_aspects & VK_IMAGE_ASPECT_DEPTH_BIT,
                              ds_attachment->last_subpass,
                              ds_attachment->desc.storeOp);

         do_early_zs_clear = needs_depth_clear && !needs_depth_store;
         if (do_early_zs_clear &&
             vk_format_has_stencil(ds_attachment->desc.format)) {
            bool needs_stencil_load =
               check_needs_load(state,
                                ds_aspects & VK_IMAGE_ASPECT_STENCIL_BIT,
                                ds_attachment->first_subpass,
                                ds_attachment->desc.stencilLoadOp);

            bool needs_stencil_store =
               check_needs_store(state,
                                 ds_aspects & VK_IMAGE_ASPECT_STENCIL_BIT,
                                 ds_attachment->last_subpass,
                                 ds_attachment->desc.stencilStoreOp);

            do_early_zs_clear = !needs_stencil_load && !needs_stencil_store;
         }

         config.early_depth_stencil_clear = do_early_zs_clear;
      } else {
         config.early_z_disable = true;
      }
   }

   /* If we enabled early Z/S clear, then we can't emit any "Clear Tile Buffers"
    * commands with the Z/S bit set, so keep track of whether we enabled this
    * in the job so we can skip these later.
    */
   job->early_zs_clear = do_early_zs_clear;

   for (uint32_t i = 0; i < subpass->color_count; i++) {
      uint32_t attachment_idx = subpass->color_attachments[i].attachment;
      if (attachment_idx == VK_ATTACHMENT_UNUSED)
         continue;

      struct v3dv_image_view *iview =
         state->framebuffer->attachments[attachment_idx];

      const struct v3dv_image *image = iview->image;
      const struct v3d_resource_slice *slice = &image->slices[iview->base_level];

      const uint32_t *clear_color =
         &state->attachments[attachment_idx].clear_value.color[0];

      uint32_t clear_pad = 0;
      if (slice->tiling == VC5_TILING_UIF_NO_XOR ||
          slice->tiling == VC5_TILING_UIF_XOR) {
         int uif_block_height = v3d_utile_height(image->cpp) * 2;

         uint32_t implicit_padded_height =
            align(framebuffer->height, uif_block_height) / uif_block_height;

         if (slice->padded_height_of_output_image_in_uif_blocks -
             implicit_padded_height >= 15) {
            clear_pad = slice->padded_height_of_output_image_in_uif_blocks;
         }
      }

      cl_emit(rcl, TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART1, clear) {
         clear.clear_color_low_32_bits = clear_color[0];
         clear.clear_color_next_24_bits = clear_color[1] & 0xffffff;
         clear.render_target_number = i;
      };

      if (iview->internal_bpp >= V3D_INTERNAL_BPP_64) {
         cl_emit(rcl, TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART2, clear) {
            clear.clear_color_mid_low_32_bits =
              ((clear_color[1] >> 24) | (clear_color[2] << 8));
            clear.clear_color_mid_high_24_bits =
              ((clear_color[2] >> 24) | ((clear_color[3] & 0xffff) << 8));
            clear.render_target_number = i;
         };
      }

      if (iview->internal_bpp >= V3D_INTERNAL_BPP_128 || clear_pad) {
         cl_emit(rcl, TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART3, clear) {
            clear.uif_padded_height_in_uif_blocks = clear_pad;
            clear.clear_color_high_16_bits = clear_color[3] >> 16;
            clear.render_target_number = i;
         };
      }
   }

   cl_emit(rcl, TILE_RENDERING_MODE_CFG_COLOR, rt) {
      v3dv_render_pass_setup_render_target(cmd_buffer, 0,
                                           &rt.render_target_0_internal_bpp,
                                           &rt.render_target_0_internal_type,
                                           &rt.render_target_0_clamp);
      v3dv_render_pass_setup_render_target(cmd_buffer, 1,
                                           &rt.render_target_1_internal_bpp,
                                           &rt.render_target_1_internal_type,
                                           &rt.render_target_1_clamp);
      v3dv_render_pass_setup_render_target(cmd_buffer, 2,
                                           &rt.render_target_2_internal_bpp,
                                           &rt.render_target_2_internal_type,
                                           &rt.render_target_2_clamp);
      v3dv_render_pass_setup_render_target(cmd_buffer, 3,
                                           &rt.render_target_3_internal_bpp,
                                           &rt.render_target_3_internal_type,
                                           &rt.render_target_3_clamp);
   }

   /* Ends rendering mode config. */
   if (ds_attachment_idx != VK_ATTACHMENT_UNUSED) {
      cl_emit(rcl, TILE_RENDERING_MODE_CFG_ZS_CLEAR_VALUES, clear) {
         clear.z_clear_value =
            state->attachments[ds_attachment_idx].clear_value.z;
         clear.stencil_clear_value =
            state->attachments[ds_attachment_idx].clear_value.s;
      };
   } else {
      cl_emit(rcl, TILE_RENDERING_MODE_CFG_ZS_CLEAR_VALUES, clear) {
         clear.z_clear_value = 1.0f;
         clear.stencil_clear_value = 0;
      };
   }

   /* Always set initial block size before the first branch, which needs
    * to match the value from binning mode config.
    */
   cl_emit(rcl, TILE_LIST_INITIAL_BLOCK_SIZE, init) {
      init.use_auto_chained_tile_lists = true;
      init.size_of_first_block_in_chained_tile_lists =
         TILE_ALLOCATION_BLOCK_SIZE_64B;
   }

   for (int layer = 0; layer < MAX2(1, fb_layers); layer++)
      cmd_buffer_emit_render_pass_layer_rcl(cmd_buffer, layer);

   cl_emit(rcl, END_OF_RENDERING, end);
}

static void
cmd_buffer_emit_subpass_clears(struct v3dv_cmd_buffer *cmd_buffer)
{
   assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);

   assert(cmd_buffer->state.pass);
   assert(cmd_buffer->state.subpass_idx < cmd_buffer->state.pass->subpass_count);
   const struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   const struct v3dv_render_pass *pass = state->pass;
   const struct v3dv_subpass *subpass = &pass->subpasses[state->subpass_idx];

   /* We only need to emit subpass clears as draw calls when the render
    * area is not aligned to tile boundaries or for GFXH-1461.
    */
   if (cmd_buffer->state.tile_aligned_render_area &&
       !subpass->do_depth_clear_with_draw &&
       !subpass->do_depth_clear_with_draw) {
      return;
   }

   uint32_t att_count = 0;
   VkClearAttachment atts[V3D_MAX_DRAW_BUFFERS + 1]; /* 4 color + D/S */

   /* We only need to emit subpass clears as draw calls for color attachments
    * if the render area is not aligned to tile boundaries.
    */
   if (!cmd_buffer->state.tile_aligned_render_area) {
      for (uint32_t i = 0; i < subpass->color_count; i++) {
         const uint32_t att_idx = subpass->color_attachments[i].attachment;
         if (att_idx == VK_ATTACHMENT_UNUSED)
            continue;

         struct v3dv_render_pass_attachment *att = &pass->attachments[att_idx];
         if (att->desc.loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR)
            continue;

         if (state->subpass_idx != att->first_subpass)
            continue;

         atts[att_count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
         atts[att_count].colorAttachment = i;
         atts[att_count].clearValue = state->attachments[att_idx].vk_clear_value;
         att_count++;
      }
   }

   /* For D/S we may also need to emit a subpass clear for GFXH-1461 */
   const uint32_t ds_att_idx = subpass->ds_attachment.attachment;
   if (ds_att_idx != VK_ATTACHMENT_UNUSED) {
      struct v3dv_render_pass_attachment *att = &pass->attachments[ds_att_idx];
      if (state->subpass_idx == att->first_subpass) {
         VkImageAspectFlags aspects = vk_format_aspects(att->desc.format);
         if (att->desc.loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR ||
             (cmd_buffer->state.tile_aligned_render_area &&
              !subpass->do_depth_clear_with_draw)) {
            aspects &= ~VK_IMAGE_ASPECT_DEPTH_BIT;
         }
         if (att->desc.stencilLoadOp != VK_ATTACHMENT_LOAD_OP_CLEAR ||
             (cmd_buffer->state.tile_aligned_render_area &&
              !subpass->do_stencil_clear_with_draw)) {
            aspects &= ~VK_IMAGE_ASPECT_STENCIL_BIT;
         }
         if (aspects) {
            atts[att_count].aspectMask = aspects;
            atts[att_count].colorAttachment = 0; /* Ignored */
            atts[att_count].clearValue =
               state->attachments[ds_att_idx].vk_clear_value;
            att_count++;
         }
      }
   }

   if (att_count == 0)
      return;

   if (!cmd_buffer->state.tile_aligned_render_area) {
      perf_debug("Render area doesn't match render pass granularity, falling "
                 "back to vkCmdClearAttachments for "
                 "VK_ATTACHMENT_LOAD_OP_CLEAR.\n");
   } else if (subpass->do_depth_clear_with_draw ||
              subpass->do_stencil_clear_with_draw) {
      perf_debug("Subpass clears DEPTH but loads STENCIL (or viceversa), "
                 "falling back to vkCmdClearAttachments for "
                 "VK_ATTACHMENT_LOAD_OP_CLEAR.\n");
   }

   /* From the Vulkan 1.0 spec:
    *
    *    "VK_ATTACHMENT_LOAD_OP_CLEAR specifies that the contents within the
    *     render area will be cleared to a uniform value, which is specified
    *     when a render pass instance is begun."
    *
    * So the clear is only constrained by the render area and not by pipeline
    * state such as scissor or viewport, these are the semantics of
    * vkCmdClearAttachments as well.
    */
   VkCommandBuffer _cmd_buffer = v3dv_cmd_buffer_to_handle(cmd_buffer);
   VkClearRect rect = {
      .rect = state->render_area,
      .baseArrayLayer = 0,
      .layerCount = 1,
   };
   v3dv_CmdClearAttachments(_cmd_buffer, att_count, atts, 1, &rect);
}

static struct v3dv_job *
cmd_buffer_subpass_create_job(struct v3dv_cmd_buffer *cmd_buffer,
                              uint32_t subpass_idx,
                              enum v3dv_job_type type)
{
   assert(type == V3DV_JOB_TYPE_GPU_CL ||
          type == V3DV_JOB_TYPE_GPU_CL_SECONDARY);

   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   assert(subpass_idx < state->pass->subpass_count);

   /* Starting a new job can trigger a finish of the current one, so don't
    * change the command buffer state for the new job until we are done creating
    * the new job.
    */
   struct v3dv_job *job =
      v3dv_cmd_buffer_start_job(cmd_buffer, subpass_idx, type);
   if (!job)
      return NULL;

   state->subpass_idx = subpass_idx;

   /* If we are starting a new job we need to setup binning. We only do this
    * for V3DV_JOB_TYPE_GPU_CL jobs because V3DV_JOB_TYPE_GPU_CL_SECONDARY
    * jobs are not submitted to the GPU directly, and are instead meant to be
    * branched to from other V3DV_JOB_TYPE_GPU_CL jobs.
    */
   if (type == V3DV_JOB_TYPE_GPU_CL &&
       job->first_subpass == state->subpass_idx) {
      const struct v3dv_subpass *subpass =
         &state->pass->subpasses[state->subpass_idx];

      const struct v3dv_framebuffer *framebuffer = state->framebuffer;

      uint8_t internal_bpp;
      bool msaa;
      v3dv_framebuffer_compute_internal_bpp_msaa(framebuffer, subpass,
                                                 &internal_bpp, &msaa);

      v3dv_job_start_frame(job,
                           framebuffer->width,
                           framebuffer->height,
                           framebuffer->layers,
                           subpass->color_count,
                           internal_bpp,
                           msaa);
   }

   return job;
}

struct v3dv_job *
v3dv_cmd_buffer_subpass_start(struct v3dv_cmd_buffer *cmd_buffer,
                              uint32_t subpass_idx)
{
   assert(cmd_buffer->state.pass);
   assert(subpass_idx < cmd_buffer->state.pass->subpass_count);

   struct v3dv_job *job =
      cmd_buffer_subpass_create_job(cmd_buffer, subpass_idx,
                                    V3DV_JOB_TYPE_GPU_CL);
   if (!job)
      return NULL;

   /* Check if our render area is aligned to tile boundaries. We have to do
    * this in each subpass because the subset of attachments used can change
    * and with that the tile size selected by the hardware can change too.
    */
   cmd_buffer_update_tile_alignment(cmd_buffer);

   /* If we can't use TLB clears then we need to emit draw clears for any
    * LOAD_OP_CLEAR attachments in this subpass now. We might also need to emit
    * Depth/Stencil clears if we hit GFXH-1461.
    *
    * Secondary command buffers don't start subpasses (and may not even have
    * framebuffer state), so we only care about this in primaries. The only
    * exception could be a secondary runnning inside a subpass that needs to
    * record a meta operation (with its own render pass) that relies on
    * attachment load clears, but we don't have any instances of that right
    * now.
    */
   if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY)
      cmd_buffer_emit_subpass_clears(cmd_buffer);

   return job;
}

struct v3dv_job *
v3dv_cmd_buffer_subpass_resume(struct v3dv_cmd_buffer *cmd_buffer,
                               uint32_t subpass_idx)
{
   assert(cmd_buffer->state.pass);
   assert(subpass_idx < cmd_buffer->state.pass->subpass_count);

   struct v3dv_job *job;
   if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
      job = cmd_buffer_subpass_create_job(cmd_buffer, subpass_idx,
                                          V3DV_JOB_TYPE_GPU_CL);
   } else {
      assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY);
      job = cmd_buffer_subpass_create_job(cmd_buffer, subpass_idx,
                                          V3DV_JOB_TYPE_GPU_CL_SECONDARY);
   }

   if (!job)
      return NULL;

   job->is_subpass_continue = true;

   return job;
}

void
v3dv_cmd_buffer_subpass_finish(struct v3dv_cmd_buffer *cmd_buffer)
{
   /* We can end up here without a job if the last command recorded into the
    * subpass already finished the job (for example a pipeline barrier). In
    * that case we miss to set the is_subpass_finish flag, but that is not
    * required for proper behavior.
    */
   struct v3dv_job *job = cmd_buffer->state.job;
   if (job)
      job->is_subpass_finish = true;
}

void
v3dv_CmdEndRenderPass(VkCommandBuffer commandBuffer)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);

   /* Finalize last subpass */
   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   assert(state->subpass_idx == state->pass->subpass_count - 1);
   v3dv_cmd_buffer_subpass_finish(cmd_buffer);
   v3dv_cmd_buffer_finish_job(cmd_buffer);

   cmd_buffer_subpass_handle_pending_resolves(cmd_buffer);

   /* We are no longer inside a render pass */
   state->framebuffer = NULL;
   state->pass = NULL;
   state->subpass_idx = -1;
}

VkResult
v3dv_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);

   if (cmd_buffer->state.oom)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   /* Primaries should have ended any recording jobs by the time they hit
    * vkEndRenderPass (if we are inside a render pass). Commands outside
    * a render pass instance (for both primaries and secondaries) spawn
    * complete jobs too. So the only case where we can get here without
    * finishing a recording job is when we are recording a secondary
    * inside a render pass.
    */
   if (cmd_buffer->state.job) {
      assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY &&
             cmd_buffer->state.pass);
      v3dv_cmd_buffer_finish_job(cmd_buffer);
   }

   cmd_buffer->status = V3DV_CMD_BUFFER_STATUS_EXECUTABLE;

   return VK_SUCCESS;
}

static void
emit_occlusion_query(struct v3dv_cmd_buffer *cmd_buffer);

static void
ensure_array_state(struct v3dv_cmd_buffer *cmd_buffer,
                   uint32_t slot_size,
                   uint32_t used_count,
                   uint32_t *alloc_count,
                   void **ptr);

static void
cmd_buffer_copy_secondary_end_query_state(struct v3dv_cmd_buffer *primary,
                                          struct v3dv_cmd_buffer *secondary)
{
   struct v3dv_cmd_buffer_state *p_state = &primary->state;
   struct v3dv_cmd_buffer_state *s_state = &secondary->state;

   const uint32_t total_state_count =
      p_state->query.end.used_count + s_state->query.end.used_count;
   ensure_array_state(primary,
                      sizeof(struct v3dv_end_query_cpu_job_info),
                      total_state_count,
                      &p_state->query.end.alloc_count,
                      (void **) &p_state->query.end.states);
   v3dv_return_if_oom(primary, NULL);

   for (uint32_t i = 0; i < s_state->query.end.used_count; i++) {
      const struct v3dv_end_query_cpu_job_info *s_qstate =
         &secondary->state.query.end.states[i];

      struct v3dv_end_query_cpu_job_info *p_qstate =
         &p_state->query.end.states[p_state->query.end.used_count++];

      p_qstate->pool = s_qstate->pool;
      p_qstate->query = s_qstate->query;
   }
}

static void
clone_bo_list(struct v3dv_cmd_buffer *cmd_buffer,
              struct list_head *dst,
              struct list_head *src)
{
   assert(cmd_buffer);

   list_inithead(dst);
   list_for_each_entry(struct v3dv_bo, bo, src, list_link) {
      struct v3dv_bo *clone_bo =
         vk_alloc(&cmd_buffer->device->vk.alloc, sizeof(struct v3dv_bo), 8,
                  VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!clone_bo) {
         v3dv_flag_oom(cmd_buffer, NULL);
         return;
      }

      *clone_bo = *bo;
      list_addtail(&clone_bo->list_link, dst);
   }
}

/* Clones a job for inclusion in the given command buffer. Note that this
 * doesn't make a deep copy so the cloned job it doesn't own any resources.
 * Useful when we need to have a job in more than one list, which happens
 * for jobs recorded in secondary command buffers when we want to execute
 * them in primaries.
 */
static struct v3dv_job *
job_clone_in_cmd_buffer(struct v3dv_job *job,
                        struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_job *clone_job = vk_alloc(&job->device->vk.alloc,
                                         sizeof(struct v3dv_job), 8,
                                         VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!clone_job) {
      v3dv_flag_oom(cmd_buffer, NULL);
      return NULL;
   }

   /* Cloned jobs don't duplicate resources! */
   *clone_job = *job;
   clone_job->is_clone = true;
   clone_job->cmd_buffer = cmd_buffer;
   list_addtail(&clone_job->list_link, &cmd_buffer->jobs);

   /* We need to regen the BO lists so that they point to the BO list in the
    * cloned job. Otherwise functions like list_length() will loop forever.
    */
   if (job->type == V3DV_JOB_TYPE_GPU_CL) {
      clone_bo_list(cmd_buffer, &clone_job->bcl.bo_list, &job->bcl.bo_list);
      clone_bo_list(cmd_buffer, &clone_job->rcl.bo_list, &job->rcl.bo_list);
      clone_bo_list(cmd_buffer, &clone_job->indirect.bo_list,
                    &job->indirect.bo_list);
   }

   return clone_job;
}

static struct v3dv_job *
cmd_buffer_subpass_split_for_barrier(struct v3dv_cmd_buffer *cmd_buffer,
                                     bool is_bcl_barrier)
{
   assert(cmd_buffer->state.subpass_idx >= 0);
   v3dv_cmd_buffer_finish_job(cmd_buffer);
   struct v3dv_job *job =
      v3dv_cmd_buffer_subpass_resume(cmd_buffer,
                                     cmd_buffer->state.subpass_idx);
   if (!job)
      return NULL;

   job->serialize = true;
   job->needs_bcl_sync = is_bcl_barrier;
   return job;
}

static void
cmd_buffer_execute_inside_pass(struct v3dv_cmd_buffer *primary,
                               uint32_t cmd_buffer_count,
                               const VkCommandBuffer *cmd_buffers)
{
   assert(primary->state.job);

   /* Emit occlusion query state if needed so the draw calls inside our
    * secondaries update the counters.
    */
   bool has_occlusion_query =
      primary->state.dirty & V3DV_CMD_DIRTY_OCCLUSION_QUERY;
   if (has_occlusion_query)
      emit_occlusion_query(primary);

   /* FIXME: if our primary job tiling doesn't enable MSSA but any of the
    * pipelines used by the secondaries do, we need to re-start the primary
    * job to enable MSAA. See cmd_buffer_restart_job_for_msaa_if_needed.
    */
   bool pending_barrier = false;
   bool pending_bcl_barrier = false;
   for (uint32_t i = 0; i < cmd_buffer_count; i++) {
      V3DV_FROM_HANDLE(v3dv_cmd_buffer, secondary, cmd_buffers[i]);

      assert(secondary->usage_flags &
             VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT);

      list_for_each_entry(struct v3dv_job, secondary_job,
                          &secondary->jobs, list_link) {
         if (secondary_job->type == V3DV_JOB_TYPE_GPU_CL_SECONDARY) {
            /* If the job is a CL, then we branch to it from the primary BCL.
             * In this case the secondary's BCL is finished with a
             * RETURN_FROM_SUB_LIST command to return back to the primary BCL
             * once we are done executing it.
             */
            assert(v3dv_cl_offset(&secondary_job->rcl) == 0);
            assert(secondary_job->bcl.bo);

            /* Sanity check that secondary BCL ends with RETURN_FROM_SUB_LIST */
            STATIC_ASSERT(cl_packet_length(RETURN_FROM_SUB_LIST) == 1);
            assert(v3dv_cl_offset(&secondary_job->bcl) >= 1);
            assert(*(((uint8_t *)secondary_job->bcl.next) - 1) ==
                   V3D42_RETURN_FROM_SUB_LIST_opcode);

            /* If this secondary has any barriers (or we had any pending barrier
             * to apply), then we can't just branch to it from the primary, we
             * need to split the primary to create a new job that can consume
             * the barriers first.
             *
             * FIXME: in this case, maybe just copy the secondary BCL without
             * the RETURN_FROM_SUB_LIST into the primary job to skip the
             * branch?
             */
            struct v3dv_job *primary_job = primary->state.job;
            if (!primary_job || secondary_job->serialize || pending_barrier) {
               const bool needs_bcl_barrier =
                  secondary_job->needs_bcl_sync || pending_bcl_barrier;
               primary_job =
                  cmd_buffer_subpass_split_for_barrier(primary,
                                                       needs_bcl_barrier);
               v3dv_return_if_oom(primary, NULL);

               /* Since we have created a new primary we need to re-emit
                * occlusion query state.
                */
               if (has_occlusion_query)
                  emit_occlusion_query(primary);
            }

            /* Make sure our primary job has all required BO references */
            set_foreach(secondary_job->bos, entry) {
               struct v3dv_bo *bo = (struct v3dv_bo *)entry->key;
               v3dv_job_add_bo(primary_job, bo);
            }

            /* Emit required branch instructions. We expect each of these
             * to end with a corresponding 'return from sub list' item.
             */
            list_for_each_entry(struct v3dv_bo, bcl_bo,
                                &secondary_job->bcl.bo_list, list_link) {
               v3dv_cl_ensure_space_with_branch(&primary_job->bcl,
                                                cl_packet_length(BRANCH_TO_SUB_LIST));
               v3dv_return_if_oom(primary, NULL);
               cl_emit(&primary_job->bcl, BRANCH_TO_SUB_LIST, branch) {
                  branch.address = v3dv_cl_address(bcl_bo, 0);
               }
            }

            primary_job->tmu_dirty_rcl |= secondary_job->tmu_dirty_rcl;
         } else if (secondary_job->type == V3DV_JOB_TYPE_CPU_CLEAR_ATTACHMENTS) {
            if (pending_barrier) {
               cmd_buffer_subpass_split_for_barrier(primary, pending_bcl_barrier);
               v3dv_return_if_oom(primary, NULL);
            }

            const struct v3dv_clear_attachments_cpu_job_info *info =
               &secondary_job->cpu.clear_attachments;
            v3dv_CmdClearAttachments(v3dv_cmd_buffer_to_handle(primary),
                                     info->attachment_count,
                                     info->attachments,
                                     info->rect_count,
                                     info->rects);
         } else {
            /* This is a regular job (CPU or GPU), so just finish the current
             * primary job (if any) and then add the secondary job to the
             * primary's job list right after it.
             */
            v3dv_cmd_buffer_finish_job(primary);
            job_clone_in_cmd_buffer(secondary_job, primary);
            if (pending_barrier) {
               secondary_job->serialize = true;
               if (pending_bcl_barrier)
                  secondary_job->needs_bcl_sync = true;
            }
         }

         pending_barrier = false;
         pending_bcl_barrier = false;
      }

      /* If the secondary has recorded any vkCmdEndQuery commands, we need to
       * copy this state to the primary so it is processed properly when the
       * current primary job is finished.
       */
      cmd_buffer_copy_secondary_end_query_state(primary, secondary);

      /* If this secondary had any pending barrier state we will need that
       * barrier state consumed with whatever comes next in the primary.
       */
      assert(secondary->state.has_barrier || !secondary->state.has_bcl_barrier);
      pending_barrier = secondary->state.has_barrier;
      pending_bcl_barrier = secondary->state.has_bcl_barrier;
   }

   if (pending_barrier) {
      primary->state.has_barrier = true;
      primary->state.has_bcl_barrier |= pending_bcl_barrier;
   }
}

static void
cmd_buffer_execute_outside_pass(struct v3dv_cmd_buffer *primary,
                                uint32_t cmd_buffer_count,
                                const VkCommandBuffer *cmd_buffers)
{
   bool pending_barrier = false;
   bool pending_bcl_barrier = false;
   for (uint32_t i = 0; i < cmd_buffer_count; i++) {
      V3DV_FROM_HANDLE(v3dv_cmd_buffer, secondary, cmd_buffers[i]);

      assert(!(secondary->usage_flags &
               VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT));

      /* Secondary command buffers that execute outside a render pass create
       * complete jobs with an RCL and tile setup, so we simply want to merge
       * their job list into the primary's. However, because they may be
       * executed into multiple primaries at the same time and we only have a
       * single list_link in each job, we can't just add then to the primary's
       * job list and we instead have to clone them first.
       *
       * Alternatively, we could create a "execute secondary" CPU job that
       * when executed in a queue, would submit all the jobs in the referenced
       * secondary command buffer. However, this would raise some challenges
       * to make it work with the implementation of wait threads in the queue
       * which we use for event waits, for example.
       */
      list_for_each_entry(struct v3dv_job, secondary_job,
                          &secondary->jobs, list_link) {
         /* These can only happen inside a render pass */
         assert(secondary_job->type != V3DV_JOB_TYPE_CPU_CLEAR_ATTACHMENTS);
         assert(secondary_job->type != V3DV_JOB_TYPE_GPU_CL_SECONDARY);
         struct v3dv_job *job = job_clone_in_cmd_buffer(secondary_job, primary);
         if (!job)
            return;

         if (pending_barrier) {
            job->serialize = true;
            if (pending_bcl_barrier)
               job->needs_bcl_sync = true;
            pending_barrier = false;
            pending_bcl_barrier = false;
         }
      }

      /* If this secondary had any pending barrier state we will need that
       * barrier state consumed with whatever comes after it (first job in
       * the next secondary or the primary, if this was the last secondary).
       */
      assert(secondary->state.has_barrier || !secondary->state.has_bcl_barrier);
      pending_barrier = secondary->state.has_barrier;
      pending_bcl_barrier = secondary->state.has_bcl_barrier;
   }

   if (pending_barrier) {
      primary->state.has_barrier = true;
      primary->state.has_bcl_barrier |= pending_bcl_barrier;
   }
}

void
v3dv_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                        uint32_t commandBufferCount,
                        const VkCommandBuffer *pCommandBuffers)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, primary, commandBuffer);

   if (primary->state.pass != NULL) {
      cmd_buffer_execute_inside_pass(primary,
                                     commandBufferCount, pCommandBuffers);
   } else {
      cmd_buffer_execute_outside_pass(primary,
                                      commandBufferCount, pCommandBuffers);
   }
}

/* This goes though the list of possible dynamic states in the pipeline and,
 * for those that are not configured as dynamic, copies relevant state into
 * the command buffer.
 */
static void
cmd_buffer_bind_pipeline_static_state(struct v3dv_cmd_buffer *cmd_buffer,
                                      const struct v3dv_dynamic_state *src)
{
   struct v3dv_dynamic_state *dest = &cmd_buffer->state.dynamic;
   uint32_t dynamic_mask = src->mask;
   uint32_t dirty = 0;

   if (!(dynamic_mask & V3DV_DYNAMIC_VIEWPORT)) {
      dest->viewport.count = src->viewport.count;
      if (memcmp(&dest->viewport.viewports, &src->viewport.viewports,
                 src->viewport.count * sizeof(VkViewport))) {
         typed_memcpy(dest->viewport.viewports,
                      src->viewport.viewports,
                      src->viewport.count);
         typed_memcpy(dest->viewport.scale, src->viewport.scale,
                      src->viewport.count);
         typed_memcpy(dest->viewport.translate, src->viewport.translate,
                      src->viewport.count);
         dirty |= V3DV_CMD_DIRTY_VIEWPORT;
      }
   }

   if (!(dynamic_mask & V3DV_DYNAMIC_SCISSOR)) {
      dest->scissor.count = src->scissor.count;
      if (memcmp(&dest->scissor.scissors, &src->scissor.scissors,
                 src->scissor.count * sizeof(VkRect2D))) {
         typed_memcpy(dest->scissor.scissors,
                      src->scissor.scissors, src->scissor.count);
         dirty |= V3DV_CMD_DIRTY_SCISSOR;
      }
   }

   if (!(dynamic_mask & V3DV_DYNAMIC_STENCIL_COMPARE_MASK)) {
      if (memcmp(&dest->stencil_compare_mask, &src->stencil_compare_mask,
                 sizeof(src->stencil_compare_mask))) {
         dest->stencil_compare_mask = src->stencil_compare_mask;
         dirty |= V3DV_CMD_DIRTY_STENCIL_COMPARE_MASK;
      }
   }

   if (!(dynamic_mask & V3DV_DYNAMIC_STENCIL_WRITE_MASK)) {
      if (memcmp(&dest->stencil_write_mask, &src->stencil_write_mask,
                 sizeof(src->stencil_write_mask))) {
         dest->stencil_write_mask = src->stencil_write_mask;
         dirty |= V3DV_CMD_DIRTY_STENCIL_WRITE_MASK;
      }
   }

   if (!(dynamic_mask & V3DV_DYNAMIC_STENCIL_REFERENCE)) {
      if (memcmp(&dest->stencil_reference, &src->stencil_reference,
                 sizeof(src->stencil_reference))) {
         dest->stencil_reference = src->stencil_reference;
         dirty |= V3DV_CMD_DIRTY_STENCIL_REFERENCE;
      }
   }

   if (!(dynamic_mask & V3DV_DYNAMIC_BLEND_CONSTANTS)) {
      if (memcmp(dest->blend_constants, src->blend_constants,
                 sizeof(src->blend_constants))) {
         memcpy(dest->blend_constants, src->blend_constants,
                sizeof(src->blend_constants));
         dirty |= V3DV_CMD_DIRTY_BLEND_CONSTANTS;
      }
   }

   if (!(dynamic_mask & V3DV_DYNAMIC_DEPTH_BIAS)) {
      if (memcmp(&dest->depth_bias, &src->depth_bias,
                 sizeof(src->depth_bias))) {
         memcpy(&dest->depth_bias, &src->depth_bias, sizeof(src->depth_bias));
         dirty |= V3DV_CMD_DIRTY_DEPTH_BIAS;
      }
   }

   if (!(dynamic_mask & V3DV_DYNAMIC_LINE_WIDTH)) {
      if (dest->line_width != src->line_width) {
         dest->line_width = src->line_width;
         dirty |= V3DV_CMD_DIRTY_LINE_WIDTH;
      }
   }

   cmd_buffer->state.dynamic.mask = dynamic_mask;
   cmd_buffer->state.dirty |= dirty;
}

static void
job_update_ez_state(struct v3dv_job *job,
                    struct v3dv_pipeline *pipeline,
                    struct v3dv_cmd_buffer *cmd_buffer)
{
   /* If first_ez_state is VC5_EZ_DISABLED it means that we have already
    * determined that we should disable EZ completely for all draw calls in
    * this job. This will cause us to disable EZ for the entire job in the
    * Tile Rendering Mode RCL packet and when we do that we need to make sure
    * we never emit a draw call in the job with EZ enabled in the CFG_BITS
    * packet, so ez_state must also be VC5_EZ_DISABLED;
    */
   if (job->first_ez_state == VC5_EZ_DISABLED) {
      assert(job->ez_state == VC5_EZ_DISABLED);
      return;
   }

   /* This is part of the pre draw call handling, so we should be inside a
    * render pass.
    */
   assert(cmd_buffer->state.pass);

   /* If this is the first time we update EZ state for this job we first check
    * if there is anything that requires disabling it completely for the entire
    * job (based on state that is not related to the current draw call and
    * pipeline state).
    */
   if (!job->decided_global_ez_enable) {
      job->decided_global_ez_enable = true;

      struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
      assert(state->subpass_idx < state->pass->subpass_count);
      struct v3dv_subpass *subpass = &state->pass->subpasses[state->subpass_idx];
      if (subpass->ds_attachment.attachment == VK_ATTACHMENT_UNUSED) {
         job->first_ez_state = VC5_EZ_DISABLED;
         job->ez_state = VC5_EZ_DISABLED;
         return;
      }

      /* GFXH-1918: the early-z buffer may load incorrect depth values
       * if the frame has odd width or height.
       *
       * So we need to disable EZ in this case.
       */
      const struct v3dv_render_pass_attachment *ds_attachment =
         &state->pass->attachments[subpass->ds_attachment.attachment];

      const VkImageAspectFlags ds_aspects =
         vk_format_aspects(ds_attachment->desc.format);

      bool needs_depth_load =
         check_needs_load(state,
                          ds_aspects & VK_IMAGE_ASPECT_DEPTH_BIT,
                          ds_attachment->first_subpass,
                          ds_attachment->desc.loadOp);

      if (needs_depth_load) {
         struct v3dv_framebuffer *fb = state->framebuffer;

         if (!fb) {
            assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY);
            perf_debug("Loading depth aspect in a secondary command buffer "
                       "without framebuffer info disables early-z tests.\n");
            job->first_ez_state = VC5_EZ_DISABLED;
            job->ez_state = VC5_EZ_DISABLED;
            return;
         }

         if (((fb->width % 2) != 0 || (fb->height % 2) != 0)) {
            perf_debug("Loading depth aspect for framebuffer with odd width "
                       "or height disables early-Z tests.\n");
            job->first_ez_state = VC5_EZ_DISABLED;
            job->ez_state = VC5_EZ_DISABLED;
            return;
         }
      }
   }

   /* Otherwise, we can decide to selectively enable or disable EZ for draw
    * calls using the CFG_BITS packet based on the bound pipeline state.
    */

   /* If the FS writes Z, then it may update against the chosen EZ direction */
   struct v3dv_shader_variant *fs_variant =
      pipeline->shared_data->variants[BROADCOM_SHADER_FRAGMENT];
   if (fs_variant->prog_data.fs->writes_z) {
      job->ez_state = VC5_EZ_DISABLED;
      return;
   }

   switch (pipeline->ez_state) {
   case VC5_EZ_UNDECIDED:
      /* If the pipeline didn't pick a direction but didn't disable, then go
       * along with the current EZ state. This allows EZ optimization for Z
       * func == EQUAL or NEVER.
       */
      break;

   case VC5_EZ_LT_LE:
   case VC5_EZ_GT_GE:
      /* If the pipeline picked a direction, then it needs to match the current
       * direction if we've decided on one.
       */
      if (job->ez_state == VC5_EZ_UNDECIDED)
         job->ez_state = pipeline->ez_state;
      else if (job->ez_state != pipeline->ez_state)
         job->ez_state = VC5_EZ_DISABLED;
      break;

   case VC5_EZ_DISABLED:
      /* If the pipeline disables EZ because of a bad Z func or stencil
       * operation, then we can't do any more EZ in this frame.
       */
      job->ez_state = VC5_EZ_DISABLED;
      break;
   }

   if (job->first_ez_state == VC5_EZ_UNDECIDED &&
       job->ez_state != VC5_EZ_DISABLED) {
      job->first_ez_state = job->ez_state;
   }
}

static void
bind_graphics_pipeline(struct v3dv_cmd_buffer *cmd_buffer,
                       struct v3dv_pipeline *pipeline)
{
   assert(pipeline && !(pipeline->active_stages & VK_SHADER_STAGE_COMPUTE_BIT));
   if (cmd_buffer->state.gfx.pipeline == pipeline)
      return;

   /* Enable always flush if we are blending to sRGB render targets. This
    * fixes test failures in:
    * dEQP-VK.pipeline.blend.format.r8g8b8a8_srgb.*
    *
    * FIXME: not sure why we need this. The tile buffer is always linear, with
    * conversion from/to sRGB happening on tile load/store operations. This
    * means that when we enable flushing the only difference is that we convert
    * to sRGB on the store after each draw call and we convert from sRGB on the
    * load before each draw call, but the blend happens in linear format in the
    * tile buffer anyway, which is the same scenario as if we didn't flush.
    */
   assert(pipeline->subpass);
   if (pipeline->subpass->has_srgb_rt && pipeline->blend.enables) {
      assert(cmd_buffer->state.job);
      cmd_buffer->state.job->always_flush = true;
      perf_debug("flushing draw calls for subpass %d because bound pipeline "
                 "uses sRGB blending\n", cmd_buffer->state.subpass_idx);
   }

   cmd_buffer->state.gfx.pipeline = pipeline;

   cmd_buffer_bind_pipeline_static_state(cmd_buffer, &pipeline->dynamic_state);

   cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_PIPELINE;
}

static void
bind_compute_pipeline(struct v3dv_cmd_buffer *cmd_buffer,
                      struct v3dv_pipeline *pipeline)
{
   assert(pipeline && pipeline->active_stages == VK_SHADER_STAGE_COMPUTE_BIT);

   if (cmd_buffer->state.compute.pipeline == pipeline)
      return;

   cmd_buffer->state.compute.pipeline = pipeline;
   cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_COMPUTE_PIPELINE;
}

void
v3dv_CmdBindPipeline(VkCommandBuffer commandBuffer,
                     VkPipelineBindPoint pipelineBindPoint,
                     VkPipeline _pipeline)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_pipeline, pipeline, _pipeline);

   switch (pipelineBindPoint) {
   case VK_PIPELINE_BIND_POINT_COMPUTE:
      bind_compute_pipeline(cmd_buffer, pipeline);
      break;

   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      bind_graphics_pipeline(cmd_buffer, pipeline);
      break;

   default:
      assert(!"invalid bind point");
      break;
   }
}

/* FIXME: C&P from radv. tu has similar code. Perhaps common place? */
void
v3dv_viewport_compute_xform(const VkViewport *viewport,
                            float scale[3],
                            float translate[3])
{
   float x = viewport->x;
   float y = viewport->y;
   float half_width = 0.5f * viewport->width;
   float half_height = 0.5f * viewport->height;
   double n = viewport->minDepth;
   double f = viewport->maxDepth;

   scale[0] = half_width;
   translate[0] = half_width + x;
   scale[1] = half_height;
   translate[1] = half_height + y;

   scale[2] = (f - n);
   translate[2] = n;

   /* It seems that if the scale is small enough the hardware won't clip
    * correctly so we work around this my choosing the smallest scale that
    * seems to work.
    *
    * This case is exercised by CTS:
    * dEQP-VK.draw.inverted_depth_ranges.nodepthclamp_deltazero
    */
   const float min_abs_scale = 0.000009f;
   if (fabs(scale[2]) < min_abs_scale)
      scale[2] = min_abs_scale * (scale[2] < 0 ? -1.0f : 1.0f);
}

void
v3dv_CmdSetViewport(VkCommandBuffer commandBuffer,
                    uint32_t firstViewport,
                    uint32_t viewportCount,
                    const VkViewport *pViewports)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   const uint32_t total_count = firstViewport + viewportCount;

   assert(firstViewport < MAX_VIEWPORTS);
   assert(total_count >= 1 && total_count <= MAX_VIEWPORTS);

   if (state->dynamic.viewport.count < total_count)
      state->dynamic.viewport.count = total_count;

   if (!memcmp(state->dynamic.viewport.viewports + firstViewport,
               pViewports, viewportCount * sizeof(*pViewports))) {
      return;
   }

   memcpy(state->dynamic.viewport.viewports + firstViewport, pViewports,
          viewportCount * sizeof(*pViewports));

   for (uint32_t i = firstViewport; i < total_count; i++) {
      v3dv_viewport_compute_xform(&state->dynamic.viewport.viewports[i],
                                  state->dynamic.viewport.scale[i],
                                  state->dynamic.viewport.translate[i]);
   }

   cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_VIEWPORT;
}

void
v3dv_CmdSetScissor(VkCommandBuffer commandBuffer,
                   uint32_t firstScissor,
                   uint32_t scissorCount,
                   const VkRect2D *pScissors)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;

   assert(firstScissor < MAX_SCISSORS);
   assert(firstScissor + scissorCount >= 1 &&
          firstScissor + scissorCount <= MAX_SCISSORS);

   if (state->dynamic.scissor.count < firstScissor + scissorCount)
      state->dynamic.scissor.count = firstScissor + scissorCount;

   if (!memcmp(state->dynamic.scissor.scissors + firstScissor,
               pScissors, scissorCount * sizeof(*pScissors))) {
      return;
   }

   memcpy(state->dynamic.scissor.scissors + firstScissor, pScissors,
          scissorCount * sizeof(*pScissors));

   cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_SCISSOR;
}

static void
emit_scissor(struct v3dv_cmd_buffer *cmd_buffer)
{
   if (cmd_buffer->state.dynamic.viewport.count == 0)
      return;

   struct v3dv_dynamic_state *dynamic = &cmd_buffer->state.dynamic;

   /* FIXME: right now we only support one viewport. viewporst[0] would work
    * now, but would need to change if we allow multiple viewports.
    */
   float *vptranslate = dynamic->viewport.translate[0];
   float *vpscale = dynamic->viewport.scale[0];

   float vp_minx = -fabsf(vpscale[0]) + vptranslate[0];
   float vp_maxx = fabsf(vpscale[0]) + vptranslate[0];
   float vp_miny = -fabsf(vpscale[1]) + vptranslate[1];
   float vp_maxy = fabsf(vpscale[1]) + vptranslate[1];

   /* Quoting from v3dx_emit:
    * "Clip to the scissor if it's enabled, but still clip to the
    * drawable regardless since that controls where the binner
    * tries to put things.
    *
    * Additionally, always clip the rendering to the viewport,
    * since the hardware does guardband clipping, meaning
    * primitives would rasterize outside of the view volume."
    */
   uint32_t minx, miny, maxx, maxy;

   /* From the Vulkan spec:
    *
    * "The application must ensure (using scissor if necessary) that all
    *  rendering is contained within the render area. The render area must be
    *  contained within the framebuffer dimensions."
    *
    * So it is the application's responsibility to ensure this. Still, we can
    * help by automatically restricting the scissor rect to the render area.
    */
   minx = MAX2(vp_minx, cmd_buffer->state.render_area.offset.x);
   miny = MAX2(vp_miny, cmd_buffer->state.render_area.offset.y);
   maxx = MIN2(vp_maxx, cmd_buffer->state.render_area.offset.x +
                        cmd_buffer->state.render_area.extent.width);
   maxy = MIN2(vp_maxy, cmd_buffer->state.render_area.offset.y +
                        cmd_buffer->state.render_area.extent.height);

   minx = vp_minx;
   miny = vp_miny;
   maxx = vp_maxx;
   maxy = vp_maxy;

   /* Clip against user provided scissor if needed.
    *
    * FIXME: right now we only allow one scissor. Below would need to be
    * updated if we support more
    */
   if (dynamic->scissor.count > 0) {
      VkRect2D *scissor = &dynamic->scissor.scissors[0];
      minx = MAX2(minx, scissor->offset.x);
      miny = MAX2(miny, scissor->offset.y);
      maxx = MIN2(maxx, scissor->offset.x + scissor->extent.width);
      maxy = MIN2(maxy, scissor->offset.y + scissor->extent.height);
   }

   /* If the scissor is outside the viewport area we end up with
    * min{x,y} > max{x,y}.
    */
   if (minx > maxx)
      maxx = minx;
   if (miny > maxy)
      maxy = miny;

   cmd_buffer->state.clip_window.offset.x = minx;
   cmd_buffer->state.clip_window.offset.y = miny;
   cmd_buffer->state.clip_window.extent.width = maxx - minx;
   cmd_buffer->state.clip_window.extent.height = maxy - miny;

   emit_clip_window(cmd_buffer->state.job, &cmd_buffer->state.clip_window);

   cmd_buffer->state.dirty &= ~V3DV_CMD_DIRTY_SCISSOR;
}

static void
emit_viewport(struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_dynamic_state *dynamic = &cmd_buffer->state.dynamic;
   /* FIXME: right now we only support one viewport. viewporst[0] would work
    * now, would need to change if we allow multiple viewports
    */
   float *vptranslate = dynamic->viewport.translate[0];
   float *vpscale = dynamic->viewport.scale[0];

   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   const uint32_t required_cl_size =
      cl_packet_length(CLIPPER_XY_SCALING) +
      cl_packet_length(CLIPPER_Z_SCALE_AND_OFFSET) +
      cl_packet_length(CLIPPER_Z_MIN_MAX_CLIPPING_PLANES) +
      cl_packet_length(VIEWPORT_OFFSET);
   v3dv_cl_ensure_space_with_branch(&job->bcl, required_cl_size);
   v3dv_return_if_oom(cmd_buffer, NULL);

   cl_emit(&job->bcl, CLIPPER_XY_SCALING, clip) {
      clip.viewport_half_width_in_1_256th_of_pixel = vpscale[0] * 256.0f;
      clip.viewport_half_height_in_1_256th_of_pixel = vpscale[1] * 256.0f;
   }

   cl_emit(&job->bcl, CLIPPER_Z_SCALE_AND_OFFSET, clip) {
      clip.viewport_z_offset_zc_to_zs = vptranslate[2];
      clip.viewport_z_scale_zc_to_zs = vpscale[2];
   }
   cl_emit(&job->bcl, CLIPPER_Z_MIN_MAX_CLIPPING_PLANES, clip) {
      /* Vulkan's Z NDC is [0..1], unlile OpenGL which is [-1, 1] */
      float z1 = vptranslate[2];
      float z2 = vptranslate[2] + vpscale[2];
      clip.minimum_zw = MIN2(z1, z2);
      clip.maximum_zw = MAX2(z1, z2);
   }

   cl_emit(&job->bcl, VIEWPORT_OFFSET, vp) {
      vp.viewport_centre_x_coordinate = vptranslate[0];
      vp.viewport_centre_y_coordinate = vptranslate[1];
   }

   cmd_buffer->state.dirty &= ~V3DV_CMD_DIRTY_VIEWPORT;
}

static void
emit_stencil(struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   struct v3dv_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   struct v3dv_dynamic_state *dynamic_state = &cmd_buffer->state.dynamic;

   const uint32_t dynamic_stencil_states = V3DV_DYNAMIC_STENCIL_COMPARE_MASK |
                                           V3DV_DYNAMIC_STENCIL_WRITE_MASK |
                                           V3DV_DYNAMIC_STENCIL_REFERENCE;

   v3dv_cl_ensure_space_with_branch(&job->bcl,
                                    2 * cl_packet_length(STENCIL_CFG));
   v3dv_return_if_oom(cmd_buffer, NULL);

   bool emitted_stencil = false;
   for (uint32_t i = 0; i < 2; i++) {
      if (pipeline->emit_stencil_cfg[i]) {
         if (dynamic_state->mask & dynamic_stencil_states) {
            cl_emit_with_prepacked(&job->bcl, STENCIL_CFG,
                                   pipeline->stencil_cfg[i], config) {
               if (dynamic_state->mask & V3DV_DYNAMIC_STENCIL_COMPARE_MASK) {
                  config.stencil_test_mask =
                     i == 0 ? dynamic_state->stencil_compare_mask.front :
                              dynamic_state->stencil_compare_mask.back;
               }
               if (dynamic_state->mask & V3DV_DYNAMIC_STENCIL_WRITE_MASK) {
                  config.stencil_write_mask =
                     i == 0 ? dynamic_state->stencil_write_mask.front :
                              dynamic_state->stencil_write_mask.back;
               }
               if (dynamic_state->mask & V3DV_DYNAMIC_STENCIL_REFERENCE) {
                  config.stencil_ref_value =
                     i == 0 ? dynamic_state->stencil_reference.front :
                              dynamic_state->stencil_reference.back;
               }
            }
         } else {
            cl_emit_prepacked(&job->bcl, &pipeline->stencil_cfg[i]);
         }

         emitted_stencil = true;
      }
   }

   if (emitted_stencil) {
      const uint32_t dynamic_stencil_dirty_flags =
               V3DV_CMD_DIRTY_STENCIL_COMPARE_MASK |
               V3DV_CMD_DIRTY_STENCIL_WRITE_MASK |
               V3DV_CMD_DIRTY_STENCIL_REFERENCE;
      cmd_buffer->state.dirty &= ~dynamic_stencil_dirty_flags;
   }
}

static void
emit_depth_bias(struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   assert(pipeline);

   if (!pipeline->depth_bias.enabled)
      return;

   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   v3dv_cl_ensure_space_with_branch(&job->bcl, cl_packet_length(DEPTH_OFFSET));
   v3dv_return_if_oom(cmd_buffer, NULL);

   struct v3dv_dynamic_state *dynamic = &cmd_buffer->state.dynamic;
   cl_emit(&job->bcl, DEPTH_OFFSET, bias) {
      bias.depth_offset_factor = dynamic->depth_bias.slope_factor;
      bias.depth_offset_units = dynamic->depth_bias.constant_factor;
      if (pipeline->depth_bias.is_z16)
         bias.depth_offset_units *= 256.0f;
      bias.limit = dynamic->depth_bias.depth_bias_clamp;
   }

   cmd_buffer->state.dirty &= ~V3DV_CMD_DIRTY_DEPTH_BIAS;
}

static void
emit_line_width(struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   v3dv_cl_ensure_space_with_branch(&job->bcl, cl_packet_length(LINE_WIDTH));
   v3dv_return_if_oom(cmd_buffer, NULL);

   cl_emit(&job->bcl, LINE_WIDTH, line) {
      line.line_width = cmd_buffer->state.dynamic.line_width;
   }

   cmd_buffer->state.dirty &= ~V3DV_CMD_DIRTY_LINE_WIDTH;
}

static void
emit_sample_state(struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   assert(pipeline);

   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   v3dv_cl_ensure_space_with_branch(&job->bcl, cl_packet_length(SAMPLE_STATE));
   v3dv_return_if_oom(cmd_buffer, NULL);

   cl_emit(&job->bcl, SAMPLE_STATE, state) {
      state.coverage = 1.0f;
      state.mask = pipeline->sample_mask;
   }
}

static void
emit_blend(struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   struct v3dv_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   assert(pipeline);

   const uint32_t blend_packets_size =
      cl_packet_length(BLEND_ENABLES) +
      cl_packet_length(BLEND_CONSTANT_COLOR) +
      cl_packet_length(BLEND_CFG) * V3D_MAX_DRAW_BUFFERS +
      cl_packet_length(COLOR_WRITE_MASKS);

   v3dv_cl_ensure_space_with_branch(&job->bcl, blend_packets_size);
   v3dv_return_if_oom(cmd_buffer, NULL);

   if (cmd_buffer->state.dirty & V3DV_CMD_DIRTY_PIPELINE) {
      if (pipeline->blend.enables) {
         cl_emit(&job->bcl, BLEND_ENABLES, enables) {
            enables.mask = pipeline->blend.enables;
         }
      }

      for (uint32_t i = 0; i < V3D_MAX_DRAW_BUFFERS; i++) {
         if (pipeline->blend.enables & (1 << i))
            cl_emit_prepacked(&job->bcl, &pipeline->blend.cfg[i]);
      }

      cl_emit(&job->bcl, COLOR_WRITE_MASKS, mask) {
         mask.mask = pipeline->blend.color_write_masks;
      }
   }

   if (pipeline->blend.needs_color_constants &&
       cmd_buffer->state.dirty & V3DV_CMD_DIRTY_BLEND_CONSTANTS) {
      struct v3dv_dynamic_state *dynamic = &cmd_buffer->state.dynamic;
      cl_emit(&job->bcl, BLEND_CONSTANT_COLOR, color) {
         color.red_f16 = _mesa_float_to_half(dynamic->blend_constants[0]);
         color.green_f16 = _mesa_float_to_half(dynamic->blend_constants[1]);
         color.blue_f16 = _mesa_float_to_half(dynamic->blend_constants[2]);
         color.alpha_f16 = _mesa_float_to_half(dynamic->blend_constants[3]);
      }
      cmd_buffer->state.dirty &= ~V3DV_CMD_DIRTY_BLEND_CONSTANTS;
   }
}

static void
emit_flat_shade_flags(struct v3dv_job *job,
                      int varying_offset,
                      uint32_t varyings,
                      enum V3DX(Varying_Flags_Action) lower,
                      enum V3DX(Varying_Flags_Action) higher)
{
   v3dv_cl_ensure_space_with_branch(&job->bcl,
                                    cl_packet_length(FLAT_SHADE_FLAGS));
   v3dv_return_if_oom(NULL, job);

   cl_emit(&job->bcl, FLAT_SHADE_FLAGS, flags) {
      flags.varying_offset_v0 = varying_offset;
      flags.flat_shade_flags_for_varyings_v024 = varyings;
      flags.action_for_flat_shade_flags_of_lower_numbered_varyings = lower;
      flags.action_for_flat_shade_flags_of_higher_numbered_varyings = higher;
   }
}

static void
emit_noperspective_flags(struct v3dv_job *job,
                         int varying_offset,
                         uint32_t varyings,
                         enum V3DX(Varying_Flags_Action) lower,
                         enum V3DX(Varying_Flags_Action) higher)
{
   v3dv_cl_ensure_space_with_branch(&job->bcl,
                                    cl_packet_length(NON_PERSPECTIVE_FLAGS));
   v3dv_return_if_oom(NULL, job);

   cl_emit(&job->bcl, NON_PERSPECTIVE_FLAGS, flags) {
      flags.varying_offset_v0 = varying_offset;
      flags.non_perspective_flags_for_varyings_v024 = varyings;
      flags.action_for_non_perspective_flags_of_lower_numbered_varyings = lower;
      flags.action_for_non_perspective_flags_of_higher_numbered_varyings = higher;
   }
}

static void
emit_centroid_flags(struct v3dv_job *job,
                    int varying_offset,
                    uint32_t varyings,
                    enum V3DX(Varying_Flags_Action) lower,
                    enum V3DX(Varying_Flags_Action) higher)
{
   v3dv_cl_ensure_space_with_branch(&job->bcl,
                                    cl_packet_length(CENTROID_FLAGS));
   v3dv_return_if_oom(NULL, job);

   cl_emit(&job->bcl, CENTROID_FLAGS, flags) {
      flags.varying_offset_v0 = varying_offset;
      flags.centroid_flags_for_varyings_v024 = varyings;
      flags.action_for_centroid_flags_of_lower_numbered_varyings = lower;
      flags.action_for_centroid_flags_of_higher_numbered_varyings = higher;
   }
}

static bool
emit_varying_flags(struct v3dv_job *job,
                   uint32_t num_flags,
                   const uint32_t *flags,
                   void (*flag_emit_callback)(struct v3dv_job *job,
                                              int varying_offset,
                                              uint32_t flags,
                                              enum V3DX(Varying_Flags_Action) lower,
                                              enum V3DX(Varying_Flags_Action) higher))
{
   bool emitted_any = false;
   for (int i = 0; i < num_flags; i++) {
      if (!flags[i])
         continue;

      if (emitted_any) {
        flag_emit_callback(job, i, flags[i],
                           V3D_VARYING_FLAGS_ACTION_UNCHANGED,
                           V3D_VARYING_FLAGS_ACTION_UNCHANGED);
      } else if (i == 0) {
        flag_emit_callback(job, i, flags[i],
                           V3D_VARYING_FLAGS_ACTION_UNCHANGED,
                           V3D_VARYING_FLAGS_ACTION_ZEROED);
      } else {
        flag_emit_callback(job, i, flags[i],
                           V3D_VARYING_FLAGS_ACTION_ZEROED,
                           V3D_VARYING_FLAGS_ACTION_ZEROED);
      }

      emitted_any = true;
   }

   return emitted_any;
}

static void
emit_varyings_state(struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_job *job = cmd_buffer->state.job;
   struct v3dv_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;

   struct v3d_fs_prog_data *prog_data_fs =
      pipeline->shared_data->variants[BROADCOM_SHADER_FRAGMENT]->prog_data.fs;

   const uint32_t num_flags =
      ARRAY_SIZE(prog_data_fs->flat_shade_flags);
   const uint32_t *flat_shade_flags = prog_data_fs->flat_shade_flags;
   const uint32_t *noperspective_flags =  prog_data_fs->noperspective_flags;
   const uint32_t *centroid_flags = prog_data_fs->centroid_flags;

   if (!emit_varying_flags(job, num_flags, flat_shade_flags,
                           emit_flat_shade_flags)) {
      v3dv_cl_ensure_space_with_branch(
         &job->bcl, cl_packet_length(ZERO_ALL_FLAT_SHADE_FLAGS));
      v3dv_return_if_oom(cmd_buffer, NULL);

      cl_emit(&job->bcl, ZERO_ALL_FLAT_SHADE_FLAGS, flags);
   }

   if (!emit_varying_flags(job, num_flags, noperspective_flags,
                           emit_noperspective_flags)) {
      v3dv_cl_ensure_space_with_branch(
         &job->bcl, cl_packet_length(ZERO_ALL_NON_PERSPECTIVE_FLAGS));
      v3dv_return_if_oom(cmd_buffer, NULL);

      cl_emit(&job->bcl, ZERO_ALL_NON_PERSPECTIVE_FLAGS, flags);
   }

   if (!emit_varying_flags(job, num_flags, centroid_flags,
                           emit_centroid_flags)) {
      v3dv_cl_ensure_space_with_branch(
         &job->bcl, cl_packet_length(ZERO_ALL_CENTROID_FLAGS));
      v3dv_return_if_oom(cmd_buffer, NULL);

      cl_emit(&job->bcl, ZERO_ALL_CENTROID_FLAGS, flags);
   }
}

static void
emit_configuration_bits(struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   struct v3dv_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   assert(pipeline);

   job_update_ez_state(job, pipeline, cmd_buffer);

   v3dv_cl_ensure_space_with_branch(&job->bcl, cl_packet_length(CFG_BITS));
   v3dv_return_if_oom(cmd_buffer, NULL);

   cl_emit_with_prepacked(&job->bcl, CFG_BITS, pipeline->cfg_bits, config) {
      config.early_z_enable = job->ez_state != VC5_EZ_DISABLED;
      config.early_z_updates_enable = config.early_z_enable &&
                                      pipeline->z_updates_enable;
   }
}

static void
update_gfx_uniform_state(struct v3dv_cmd_buffer *cmd_buffer,
                         uint32_t dirty_uniform_state)
{
   /* We need to update uniform streams if any piece of state that is passed
    * to the shader as a uniform may have changed.
    *
    * If only descriptor sets are dirty then we can safely ignore updates
    * for shader stages that don't access descriptors.
    */

   struct v3dv_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   assert(pipeline);

   const bool dirty_descriptors_only =
      (cmd_buffer->state.dirty & dirty_uniform_state) ==
      V3DV_CMD_DIRTY_DESCRIPTOR_SETS;

   const bool needs_fs_update =
      !dirty_descriptors_only ||
      (pipeline->layout->shader_stages & VK_SHADER_STAGE_FRAGMENT_BIT);

   if (needs_fs_update) {
      struct v3dv_shader_variant *fs_variant =
         pipeline->shared_data->variants[BROADCOM_SHADER_FRAGMENT];

      cmd_buffer->state.uniforms.fs =
         v3dv_write_uniforms(cmd_buffer, pipeline, fs_variant);
   }

   const bool needs_vs_update =
      !dirty_descriptors_only ||
      (pipeline->layout->shader_stages & VK_SHADER_STAGE_VERTEX_BIT);

   if (needs_vs_update) {
      struct v3dv_shader_variant *vs_variant =
         pipeline->shared_data->variants[BROADCOM_SHADER_VERTEX];

       struct v3dv_shader_variant *vs_bin_variant =
         pipeline->shared_data->variants[BROADCOM_SHADER_VERTEX_BIN];

      cmd_buffer->state.uniforms.vs =
         v3dv_write_uniforms(cmd_buffer, pipeline, vs_variant);

      cmd_buffer->state.uniforms.vs_bin =
         v3dv_write_uniforms(cmd_buffer, pipeline, vs_bin_variant);
   }
}

static void
emit_gl_shader_state(struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   struct v3dv_pipeline *pipeline = state->gfx.pipeline;
   assert(pipeline);

   struct v3d_vs_prog_data *prog_data_vs =
      pipeline->shared_data->variants[BROADCOM_SHADER_VERTEX]->prog_data.vs;
   struct v3d_vs_prog_data *prog_data_vs_bin =
      pipeline->shared_data->variants[BROADCOM_SHADER_VERTEX_BIN]->prog_data.vs;
   struct v3d_fs_prog_data *prog_data_fs =
      pipeline->shared_data->variants[BROADCOM_SHADER_FRAGMENT]->prog_data.fs;

   /* Update the cache dirty flag based on the shader progs data */
   job->tmu_dirty_rcl |= prog_data_vs_bin->base.tmu_dirty_rcl;
   job->tmu_dirty_rcl |= prog_data_vs->base.tmu_dirty_rcl;
   job->tmu_dirty_rcl |= prog_data_fs->base.tmu_dirty_rcl;

   /* See GFXH-930 workaround below */
   uint32_t num_elements_to_emit = MAX2(pipeline->va_count, 1);

   uint32_t shader_rec_offset =
      v3dv_cl_ensure_space(&job->indirect,
                           cl_packet_length(GL_SHADER_STATE_RECORD) +
                           num_elements_to_emit *
                           cl_packet_length(GL_SHADER_STATE_ATTRIBUTE_RECORD),
                           32);
   v3dv_return_if_oom(cmd_buffer, NULL);

   struct v3dv_shader_variant *vs_variant =
      pipeline->shared_data->variants[BROADCOM_SHADER_VERTEX];
   struct v3dv_shader_variant *vs_bin_variant =
      pipeline->shared_data->variants[BROADCOM_SHADER_VERTEX_BIN];
   struct v3dv_shader_variant *fs_variant =
      pipeline->shared_data->variants[BROADCOM_SHADER_FRAGMENT];
   struct v3dv_bo *assembly_bo = pipeline->shared_data->assembly_bo;

   struct v3dv_bo *default_attribute_values =
      pipeline->default_attribute_values != NULL ?
      pipeline->default_attribute_values :
      pipeline->device->default_attribute_float;

   cl_emit_with_prepacked(&job->indirect, GL_SHADER_STATE_RECORD,
                          pipeline->shader_state_record, shader) {

      /* FIXME: we are setting this values here and during the
       * prepacking. This is because both cl_emit_with_prepacked and v3dv_pack
       * asserts for minimum values of these. It would be good to get
       * v3dv_pack to assert on the final value if possible
       */
      shader.min_coord_shader_input_segments_required_in_play =
         pipeline->vpm_cfg_bin.As;
      shader.min_vertex_shader_input_segments_required_in_play =
         pipeline->vpm_cfg.As;

      shader.coordinate_shader_code_address =
         v3dv_cl_address(assembly_bo, vs_bin_variant->assembly_offset);
      shader.vertex_shader_code_address =
         v3dv_cl_address(assembly_bo, vs_variant->assembly_offset);
      shader.fragment_shader_code_address =
         v3dv_cl_address(assembly_bo, fs_variant->assembly_offset);

      shader.coordinate_shader_uniforms_address = cmd_buffer->state.uniforms.vs_bin;
      shader.vertex_shader_uniforms_address = cmd_buffer->state.uniforms.vs;
      shader.fragment_shader_uniforms_address = cmd_buffer->state.uniforms.fs;

      shader.address_of_default_attribute_values =
         v3dv_cl_address(default_attribute_values, 0);
   }

   /* Upload vertex element attributes (SHADER_STATE_ATTRIBUTE_RECORD) */
   bool cs_loaded_any = false;
   const bool cs_uses_builtins = prog_data_vs_bin->uses_iid ||
                                 prog_data_vs_bin->uses_biid ||
                                 prog_data_vs_bin->uses_vid;
   const uint32_t packet_length =
      cl_packet_length(GL_SHADER_STATE_ATTRIBUTE_RECORD);

   uint32_t emitted_va_count = 0;
   for (uint32_t i = 0; emitted_va_count < pipeline->va_count; i++) {
      assert(i < MAX_VERTEX_ATTRIBS);

      if (pipeline->va[i].vk_format == VK_FORMAT_UNDEFINED)
         continue;

      const uint32_t binding = pipeline->va[i].binding;

      /* We store each vertex attribute in the array using its driver location
       * as index.
       */
      const uint32_t location = i;

      struct v3dv_vertex_binding *c_vb = &cmd_buffer->state.vertex_bindings[binding];

      cl_emit_with_prepacked(&job->indirect, GL_SHADER_STATE_ATTRIBUTE_RECORD,
                             &pipeline->vertex_attrs[i * packet_length], attr) {

         assert(c_vb->buffer->mem->bo);
         attr.address = v3dv_cl_address(c_vb->buffer->mem->bo,
                                        c_vb->buffer->mem_offset +
                                        pipeline->va[i].offset +
                                        c_vb->offset);

         attr.number_of_values_read_by_coordinate_shader =
            prog_data_vs_bin->vattr_sizes[location];
         attr.number_of_values_read_by_vertex_shader =
            prog_data_vs->vattr_sizes[location];

         /* GFXH-930: At least one attribute must be enabled and read by CS
          * and VS.  If we have attributes being consumed by the VS but not
          * the CS, then set up a dummy load of the last attribute into the
          * CS's VPM inputs.  (Since CS is just dead-code-elimination compared
          * to VS, we can't have CS loading but not VS).
          *
          * GFXH-1602: first attribute must be active if using builtins.
          */
         if (prog_data_vs_bin->vattr_sizes[location])
            cs_loaded_any = true;

         if (i == 0 && cs_uses_builtins && !cs_loaded_any) {
            attr.number_of_values_read_by_coordinate_shader = 1;
            cs_loaded_any = true;
         } else if (i == pipeline->va_count - 1 && !cs_loaded_any) {
            attr.number_of_values_read_by_coordinate_shader = 1;
            cs_loaded_any = true;
         }

         attr.maximum_index = 0xffffff;
      }

      emitted_va_count++;
   }

   if (pipeline->va_count == 0) {
      /* GFXH-930: At least one attribute must be enabled and read
       * by CS and VS.  If we have no attributes being consumed by
       * the shader, set up a dummy to be loaded into the VPM.
       */
      cl_emit(&job->indirect, GL_SHADER_STATE_ATTRIBUTE_RECORD, attr) {
         /* Valid address of data whose value will be unused. */
         attr.address = v3dv_cl_address(job->indirect.bo, 0);

         attr.type = ATTRIBUTE_FLOAT;
         attr.stride = 0;
         attr.vec_size = 1;

         attr.number_of_values_read_by_coordinate_shader = 1;
         attr.number_of_values_read_by_vertex_shader = 1;
      }
   }

   if (cmd_buffer->state.dirty & V3DV_CMD_DIRTY_PIPELINE) {
      v3dv_cl_ensure_space_with_branch(&job->bcl,
                                       sizeof(pipeline->vcm_cache_size));
      v3dv_return_if_oom(cmd_buffer, NULL);

      cl_emit_prepacked(&job->bcl, &pipeline->vcm_cache_size);
   }

   v3dv_cl_ensure_space_with_branch(&job->bcl,
                                    cl_packet_length(GL_SHADER_STATE));
   v3dv_return_if_oom(cmd_buffer, NULL);

   cl_emit(&job->bcl, GL_SHADER_STATE, state) {
      state.address = v3dv_cl_address(job->indirect.bo,
                                      shader_rec_offset);
      state.number_of_attribute_arrays = num_elements_to_emit;
   }

   cmd_buffer->state.dirty &= ~(V3DV_CMD_DIRTY_VERTEX_BUFFER |
                                V3DV_CMD_DIRTY_DESCRIPTOR_SETS |
                                V3DV_CMD_DIRTY_PUSH_CONSTANTS);
}

static void
emit_occlusion_query(struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   v3dv_cl_ensure_space_with_branch(&job->bcl,
                                    cl_packet_length(OCCLUSION_QUERY_COUNTER));
   v3dv_return_if_oom(cmd_buffer, NULL);

   cl_emit(&job->bcl, OCCLUSION_QUERY_COUNTER, counter) {
      if (cmd_buffer->state.query.active_query) {
         counter.address =
            v3dv_cl_address(cmd_buffer->state.query.active_query, 0);
      }
   }

   cmd_buffer->state.dirty &= ~V3DV_CMD_DIRTY_OCCLUSION_QUERY;
}

/* This stores command buffer state that we might be about to stomp for
 * a meta operation.
 */
void
v3dv_cmd_buffer_meta_state_push(struct v3dv_cmd_buffer *cmd_buffer,
                                bool push_descriptor_state)
{
   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;

   if (state->subpass_idx != -1) {
      state->meta.subpass_idx = state->subpass_idx;
      state->meta.framebuffer = v3dv_framebuffer_to_handle(state->framebuffer);
      state->meta.pass = v3dv_render_pass_to_handle(state->pass);

      const uint32_t attachment_state_item_size =
         sizeof(struct v3dv_cmd_buffer_attachment_state);
      const uint32_t attachment_state_total_size =
         attachment_state_item_size * state->attachment_alloc_count;
      if (state->meta.attachment_alloc_count < state->attachment_alloc_count) {
         if (state->meta.attachment_alloc_count > 0)
            vk_free(&cmd_buffer->device->vk.alloc, state->meta.attachments);

         state->meta.attachments = vk_zalloc(&cmd_buffer->device->vk.alloc,
                                             attachment_state_total_size, 8,
                                             VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
         if (!state->meta.attachments) {
            v3dv_flag_oom(cmd_buffer, NULL);
            return;
         }
         state->meta.attachment_alloc_count = state->attachment_alloc_count;
      }
      state->meta.attachment_count = state->attachment_alloc_count;
      memcpy(state->meta.attachments, state->attachments,
             attachment_state_total_size);

      state->meta.tile_aligned_render_area = state->tile_aligned_render_area;
      memcpy(&state->meta.render_area, &state->render_area, sizeof(VkRect2D));
   }

   /* We expect that meta operations are graphics-only, so we only take into
    * account the graphics pipeline, and the graphics state
    */
   state->meta.gfx.pipeline = state->gfx.pipeline;
   memcpy(&state->meta.dynamic, &state->dynamic, sizeof(state->dynamic));

   struct v3dv_descriptor_state *gfx_descriptor_state =
      &cmd_buffer->state.gfx.descriptor_state;

   if (push_descriptor_state) {
      if (gfx_descriptor_state->valid != 0) {
         memcpy(&state->meta.gfx.descriptor_state, gfx_descriptor_state,
                sizeof(state->gfx.descriptor_state));
      }
      state->meta.has_descriptor_state = true;
   } else {
      state->meta.has_descriptor_state = false;
   }

   /* FIXME: if we keep track of wether we have bound any push constant state
    *        at all we could restruct this only to cases where it is actually
    *        necessary.
    */
   memcpy(state->meta.push_constants, cmd_buffer->push_constants_data,
          sizeof(state->meta.push_constants));
}

/* This restores command buffer state after a meta operation
 */
void
v3dv_cmd_buffer_meta_state_pop(struct v3dv_cmd_buffer *cmd_buffer,
                               uint32_t dirty_dynamic_state,
                               bool needs_subpass_resume)
{
   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;

   if (state->meta.subpass_idx != -1) {
      state->pass = v3dv_render_pass_from_handle(state->meta.pass);
      state->framebuffer = v3dv_framebuffer_from_handle(state->meta.framebuffer);

      assert(state->meta.attachment_count <= state->attachment_alloc_count);
      const uint32_t attachment_state_item_size =
         sizeof(struct v3dv_cmd_buffer_attachment_state);
      const uint32_t attachment_state_total_size =
         attachment_state_item_size * state->meta.attachment_count;
      memcpy(state->attachments, state->meta.attachments,
             attachment_state_total_size);

      state->tile_aligned_render_area = state->meta.tile_aligned_render_area;
      memcpy(&state->render_area, &state->meta.render_area, sizeof(VkRect2D));

      /* Is needs_subpass_resume is true it means that the emitted the meta
       * operation in its own job (possibly with an RT config that is
       * incompatible with the current subpass), so resuming subpass execution
       * after it requires that we create a new job with the subpass RT setup.
       */
      if (needs_subpass_resume)
         v3dv_cmd_buffer_subpass_resume(cmd_buffer, state->meta.subpass_idx);
   } else {
      state->subpass_idx = -1;
   }

   if (state->meta.gfx.pipeline != NULL) {
      struct v3dv_pipeline *pipeline = state->meta.gfx.pipeline;
      VkPipelineBindPoint pipeline_binding =
         v3dv_pipeline_get_binding_point(pipeline);
      v3dv_CmdBindPipeline(v3dv_cmd_buffer_to_handle(cmd_buffer),
                           pipeline_binding,
                           v3dv_pipeline_to_handle(state->meta.gfx.pipeline));
   } else {
      state->gfx.pipeline = NULL;
   }

   if (dirty_dynamic_state) {
      memcpy(&state->dynamic, &state->meta.dynamic, sizeof(state->dynamic));
      state->dirty |= dirty_dynamic_state;
   }

   if (state->meta.has_descriptor_state) {
      if (state->meta.gfx.descriptor_state.valid != 0) {
         memcpy(&state->gfx.descriptor_state, &state->meta.gfx.descriptor_state,
                sizeof(state->gfx.descriptor_state));
      } else {
         state->gfx.descriptor_state.valid = 0;
      }
   }

   memcpy(cmd_buffer->push_constants_data, state->meta.push_constants,
          sizeof(state->meta.push_constants));

   state->meta.gfx.pipeline = NULL;
   state->meta.framebuffer = VK_NULL_HANDLE;
   state->meta.pass = VK_NULL_HANDLE;
   state->meta.subpass_idx = -1;
   state->meta.has_descriptor_state = false;
}

/* FIXME: C&P from v3dx_draw. Refactor to common place? */
static uint32_t
v3d_hw_prim_type(enum pipe_prim_type prim_type)
{
   switch (prim_type) {
   case PIPE_PRIM_POINTS:
   case PIPE_PRIM_LINES:
   case PIPE_PRIM_LINE_LOOP:
   case PIPE_PRIM_LINE_STRIP:
   case PIPE_PRIM_TRIANGLES:
   case PIPE_PRIM_TRIANGLE_STRIP:
   case PIPE_PRIM_TRIANGLE_FAN:
      return prim_type;

   case PIPE_PRIM_LINES_ADJACENCY:
   case PIPE_PRIM_LINE_STRIP_ADJACENCY:
   case PIPE_PRIM_TRIANGLES_ADJACENCY:
   case PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return 8 + (prim_type - PIPE_PRIM_LINES_ADJACENCY);

   default:
      unreachable("Unsupported primitive type");
   }
}

struct v3dv_draw_info {
   uint32_t vertex_count;
   uint32_t instance_count;
   uint32_t first_vertex;
   uint32_t first_instance;
};

static void
cmd_buffer_emit_draw(struct v3dv_cmd_buffer *cmd_buffer,
                     struct v3dv_draw_info *info)
{
   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   struct v3dv_pipeline *pipeline = state->gfx.pipeline;

   assert(pipeline);

   uint32_t hw_prim_type = v3d_hw_prim_type(pipeline->topology);

   if (info->first_instance > 0) {
      v3dv_cl_ensure_space_with_branch(
         &job->bcl, cl_packet_length(BASE_VERTEX_BASE_INSTANCE));
      v3dv_return_if_oom(cmd_buffer, NULL);

      cl_emit(&job->bcl, BASE_VERTEX_BASE_INSTANCE, base) {
         base.base_instance = info->first_instance;
         base.base_vertex = 0;
      }
   }

   if (info->instance_count > 1) {
      v3dv_cl_ensure_space_with_branch(
         &job->bcl, cl_packet_length(VERTEX_ARRAY_INSTANCED_PRIMS));
      v3dv_return_if_oom(cmd_buffer, NULL);

      cl_emit(&job->bcl, VERTEX_ARRAY_INSTANCED_PRIMS, prim) {
         prim.mode = hw_prim_type;
         prim.index_of_first_vertex = info->first_vertex;
         prim.number_of_instances = info->instance_count;
         prim.instance_length = info->vertex_count;
      }
   } else {
      v3dv_cl_ensure_space_with_branch(
         &job->bcl, cl_packet_length(VERTEX_ARRAY_PRIMS));
      v3dv_return_if_oom(cmd_buffer, NULL);
      cl_emit(&job->bcl, VERTEX_ARRAY_PRIMS, prim) {
         prim.mode = hw_prim_type;
         prim.length = info->vertex_count;
         prim.index_of_first_vertex = info->first_vertex;
      }
   }
}

static struct v3dv_job *
cmd_buffer_pre_draw_split_job(struct v3dv_cmd_buffer *cmd_buffer)
{
   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   /* If the job has been flagged with 'always_flush' and it has already
    * recorded any draw calls then we need to start a new job for it.
    */
   if (job->always_flush && job->draw_count > 0) {
      assert(cmd_buffer->state.pass);
      /* First, flag the current job as not being the last in the
       * current subpass
       */
      job->is_subpass_finish = false;

      /* Now start a new job in the same subpass and flag it as continuing
       * the current subpass.
       */
      job = v3dv_cmd_buffer_subpass_resume(cmd_buffer,
                                           cmd_buffer->state.subpass_idx);
      assert(job->draw_count == 0);

      /* Inherit the 'always flush' behavior */
      job->always_flush = true;
   }

   assert(job->draw_count == 0 || !job->always_flush);
   return job;
}

/**
 * The Vulkan spec states:
 *
 *   "It is legal for a subpass to use no color or depth/stencil
 *    attachments (...)  This kind of subpass can use shader side effects such
 *    as image stores and atomics to produce an output. In this case, the
 *    subpass continues to use the width, height, and layers of the framebuffer
 *    to define the dimensions of the rendering area, and the
 *    rasterizationSamples from each pipeline’s
 *    VkPipelineMultisampleStateCreateInfo to define the number of samples used
 *    in rasterization."
 *
 * We need to enable MSAA in the TILE_BINNING_MODE_CFG packet, which we
 * emit when we start a new frame at the begining of a subpass. At that point,
 * if the framebuffer doesn't have any attachments we won't enable MSAA and
 * the job won't be valid in the scenario described by the spec.
 *
 * This function is intended to be called before a draw call and will test if
 * we are in that scenario, in which case, it will restart the current job
 * with MSAA enabled.
 */
static void
cmd_buffer_restart_job_for_msaa_if_needed(struct v3dv_cmd_buffer *cmd_buffer)
{
   assert(cmd_buffer->state.job);

   /* We don't support variableMultisampleRate so we know that all pipelines
    * bound in the same subpass must have matching number of samples, so we
    * can do this check only on the first draw call.
    */
   if (cmd_buffer->state.job->draw_count > 0)
      return;

   /* We only need to restart the frame if the pipeline requires MSAA but
    * our frame tiling didn't enable it.
    */
   if (!cmd_buffer->state.gfx.pipeline->msaa ||
       cmd_buffer->state.job->frame_tiling.msaa) {
      return;
   }

   /* FIXME: Secondary command buffers don't start frames. Instead, they are
    * recorded into primary jobs that start them. For secondaries, we should
    * still handle this scenario, but we should do that when we record them
    * into primaries by testing if any of the secondaries has multisampled
    * draw calls in them, and then using that info to decide if we need to
    * restart the primary job into which they are being recorded.
    */
   if (cmd_buffer->level != VK_COMMAND_BUFFER_LEVEL_PRIMARY)
      return;

   /* Drop the current job and restart it with MSAA enabled */
   struct v3dv_job *old_job = cmd_buffer->state.job;
   cmd_buffer->state.job = NULL;

   struct v3dv_job *job = vk_zalloc(&cmd_buffer->device->vk.alloc,
                                    sizeof(struct v3dv_job), 8,
                                    VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!job) {
      v3dv_flag_oom(cmd_buffer, NULL);
      return;
   }

   v3dv_job_init(job, V3DV_JOB_TYPE_GPU_CL, cmd_buffer->device, cmd_buffer,
                 cmd_buffer->state.subpass_idx);
   cmd_buffer->state.job = job;

   v3dv_job_start_frame(job,
                        old_job->frame_tiling.width,
                        old_job->frame_tiling.height,
                        old_job->frame_tiling.layers,
                        old_job->frame_tiling.render_target_count,
                        old_job->frame_tiling.internal_bpp,
                        true /* msaa */);

   v3dv_job_destroy(old_job);
}

static void
cmd_buffer_emit_pre_draw(struct v3dv_cmd_buffer *cmd_buffer)
{
   assert(cmd_buffer->state.gfx.pipeline);
   assert(!(cmd_buffer->state.gfx.pipeline->active_stages & VK_SHADER_STAGE_COMPUTE_BIT));

   /* If we emitted a pipeline barrier right before this draw we won't have
    * an active job. In that case, create a new job continuing the current
    * subpass.
    */
   struct v3dv_job *job = cmd_buffer->state.job;
   if (!job) {
      job = v3dv_cmd_buffer_subpass_resume(cmd_buffer,
                                           cmd_buffer->state.subpass_idx);
   }

   /* Restart single sample job for MSAA pipeline if needed */
   cmd_buffer_restart_job_for_msaa_if_needed(cmd_buffer);

   /* If the job is configured to flush on every draw call we need to create
    * a new job now.
    */
   job = cmd_buffer_pre_draw_split_job(cmd_buffer);
   job->draw_count++;

   /* GL shader state binds shaders, uniform and vertex attribute state. The
    * compiler injects uniforms to handle some descriptor types (such as
    * textures), so we need to regen that when descriptor state changes.
    *
    * We also need to emit new shader state if we have a dirty viewport since
    * that will require that we new uniform state for QUNIFORM_VIEWPORT_*.
    */
   uint32_t *dirty = &cmd_buffer->state.dirty;

   const uint32_t dirty_uniform_state =
      *dirty & (V3DV_CMD_DIRTY_PIPELINE |
                V3DV_CMD_DIRTY_PUSH_CONSTANTS |
                V3DV_CMD_DIRTY_DESCRIPTOR_SETS |
                V3DV_CMD_DIRTY_VIEWPORT);

   if (dirty_uniform_state)
      update_gfx_uniform_state(cmd_buffer, dirty_uniform_state);

   if (dirty_uniform_state || (*dirty & V3DV_CMD_DIRTY_VERTEX_BUFFER))
      emit_gl_shader_state(cmd_buffer);

   if (*dirty & (V3DV_CMD_DIRTY_PIPELINE)) {
      emit_configuration_bits(cmd_buffer);
      emit_varyings_state(cmd_buffer);
   }

   if (*dirty & (V3DV_CMD_DIRTY_VIEWPORT | V3DV_CMD_DIRTY_SCISSOR)) {
      emit_scissor(cmd_buffer);
   }

   if (*dirty & V3DV_CMD_DIRTY_VIEWPORT) {
      emit_viewport(cmd_buffer);
   }

   const uint32_t dynamic_stencil_dirty_flags =
      V3DV_CMD_DIRTY_STENCIL_COMPARE_MASK |
      V3DV_CMD_DIRTY_STENCIL_WRITE_MASK |
      V3DV_CMD_DIRTY_STENCIL_REFERENCE;
   if (*dirty & (V3DV_CMD_DIRTY_PIPELINE | dynamic_stencil_dirty_flags))
      emit_stencil(cmd_buffer);

   if (*dirty & (V3DV_CMD_DIRTY_PIPELINE | V3DV_CMD_DIRTY_DEPTH_BIAS))
      emit_depth_bias(cmd_buffer);

   if (*dirty & (V3DV_CMD_DIRTY_PIPELINE | V3DV_CMD_DIRTY_BLEND_CONSTANTS))
      emit_blend(cmd_buffer);

   if (*dirty & V3DV_CMD_DIRTY_OCCLUSION_QUERY)
      emit_occlusion_query(cmd_buffer);

   if (*dirty & V3DV_CMD_DIRTY_LINE_WIDTH)
      emit_line_width(cmd_buffer);

   if (*dirty & V3DV_CMD_DIRTY_PIPELINE)
      emit_sample_state(cmd_buffer);

   cmd_buffer->state.dirty &= ~V3DV_CMD_DIRTY_PIPELINE;
}

static void
cmd_buffer_draw(struct v3dv_cmd_buffer *cmd_buffer,
                struct v3dv_draw_info *info)
{
   cmd_buffer_emit_pre_draw(cmd_buffer);
   cmd_buffer_emit_draw(cmd_buffer, info);
}

void
v3dv_CmdDraw(VkCommandBuffer commandBuffer,
             uint32_t vertexCount,
             uint32_t instanceCount,
             uint32_t firstVertex,
             uint32_t firstInstance)
{
   if (vertexCount == 0 || instanceCount == 0)
      return;

   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   struct v3dv_draw_info info = {};
   info.vertex_count = vertexCount;
   info.instance_count = instanceCount;
   info.first_instance = firstInstance;
   info.first_vertex = firstVertex;

   cmd_buffer_draw(cmd_buffer, &info);
}

void
v3dv_CmdDrawIndexed(VkCommandBuffer commandBuffer,
                    uint32_t indexCount,
                    uint32_t instanceCount,
                    uint32_t firstIndex,
                    int32_t vertexOffset,
                    uint32_t firstInstance)
{
   if (indexCount == 0 || instanceCount == 0)
      return;

   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer_emit_pre_draw(cmd_buffer);

   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   const struct v3dv_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   uint32_t hw_prim_type = v3d_hw_prim_type(pipeline->topology);
   uint8_t index_type = ffs(cmd_buffer->state.index_buffer.index_size) - 1;
   uint32_t index_offset = firstIndex * cmd_buffer->state.index_buffer.index_size;

   if (vertexOffset != 0 || firstInstance != 0) {
      v3dv_cl_ensure_space_with_branch(
         &job->bcl, cl_packet_length(BASE_VERTEX_BASE_INSTANCE));
      v3dv_return_if_oom(cmd_buffer, NULL);

      cl_emit(&job->bcl, BASE_VERTEX_BASE_INSTANCE, base) {
         base.base_instance = firstInstance;
         base.base_vertex = vertexOffset;
      }
   }

   if (instanceCount == 1) {
      v3dv_cl_ensure_space_with_branch(
         &job->bcl, cl_packet_length(INDEXED_PRIM_LIST));
      v3dv_return_if_oom(cmd_buffer, NULL);

      cl_emit(&job->bcl, INDEXED_PRIM_LIST, prim) {
         prim.index_type = index_type;
         prim.length = indexCount;
         prim.index_offset = index_offset;
         prim.mode = hw_prim_type;
         prim.enable_primitive_restarts = pipeline->primitive_restart;
      }
   } else if (instanceCount > 1) {
      v3dv_cl_ensure_space_with_branch(
         &job->bcl, cl_packet_length(INDEXED_INSTANCED_PRIM_LIST));
      v3dv_return_if_oom(cmd_buffer, NULL);

      cl_emit(&job->bcl, INDEXED_INSTANCED_PRIM_LIST, prim) {
         prim.index_type = index_type;
         prim.index_offset = index_offset;
         prim.mode = hw_prim_type;
         prim.enable_primitive_restarts = pipeline->primitive_restart;
         prim.number_of_instances = instanceCount;
         prim.instance_length = indexCount;
      }
   }
}

void
v3dv_CmdDrawIndirect(VkCommandBuffer commandBuffer,
                     VkBuffer _buffer,
                     VkDeviceSize offset,
                     uint32_t drawCount,
                     uint32_t stride)
{
   if (drawCount == 0)
      return;

   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_buffer, buffer, _buffer);

   /* drawCount is the number of draws to execute, and can be zero. */
   if (drawCount == 0)
      return;

   cmd_buffer_emit_pre_draw(cmd_buffer);

   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   const struct v3dv_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   uint32_t hw_prim_type = v3d_hw_prim_type(pipeline->topology);

   v3dv_cl_ensure_space_with_branch(
      &job->bcl, cl_packet_length(INDIRECT_VERTEX_ARRAY_INSTANCED_PRIMS));
   v3dv_return_if_oom(cmd_buffer, NULL);

   cl_emit(&job->bcl, INDIRECT_VERTEX_ARRAY_INSTANCED_PRIMS, prim) {
      prim.mode = hw_prim_type;
      prim.number_of_draw_indirect_array_records = drawCount;
      prim.stride_in_multiples_of_4_bytes = stride >> 2;
      prim.address = v3dv_cl_address(buffer->mem->bo,
                                     buffer->mem_offset + offset);
   }
}

void
v3dv_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer,
                            VkBuffer _buffer,
                            VkDeviceSize offset,
                            uint32_t drawCount,
                            uint32_t stride)
{
   if (drawCount == 0)
      return;

   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_buffer, buffer, _buffer);

   /* drawCount is the number of draws to execute, and can be zero. */
   if (drawCount == 0)
      return;

   cmd_buffer_emit_pre_draw(cmd_buffer);

   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   const struct v3dv_pipeline *pipeline = cmd_buffer->state.gfx.pipeline;
   uint32_t hw_prim_type = v3d_hw_prim_type(pipeline->topology);
   uint8_t index_type = ffs(cmd_buffer->state.index_buffer.index_size) - 1;

   v3dv_cl_ensure_space_with_branch(
      &job->bcl, cl_packet_length(INDIRECT_INDEXED_INSTANCED_PRIM_LIST));
   v3dv_return_if_oom(cmd_buffer, NULL);

   cl_emit(&job->bcl, INDIRECT_INDEXED_INSTANCED_PRIM_LIST, prim) {
      prim.index_type = index_type;
      prim.mode = hw_prim_type;
      prim.enable_primitive_restarts = pipeline->primitive_restart;
      prim.number_of_draw_indirect_indexed_records = drawCount;
      prim.stride_in_multiples_of_4_bytes = stride >> 2;
      prim.address = v3dv_cl_address(buffer->mem->bo,
                                     buffer->mem_offset + offset);
   }
}

void
v3dv_CmdPipelineBarrier(VkCommandBuffer commandBuffer,
                        VkPipelineStageFlags srcStageMask,
                        VkPipelineStageFlags dstStageMask,
                        VkDependencyFlags dependencyFlags,
                        uint32_t memoryBarrierCount,
                        const VkMemoryBarrier *pMemoryBarriers,
                        uint32_t bufferBarrierCount,
                        const VkBufferMemoryBarrier *pBufferBarriers,
                        uint32_t imageBarrierCount,
                        const VkImageMemoryBarrier *pImageBarriers)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);

   /* We only care about barriers between GPU jobs */
   if (srcStageMask == VK_PIPELINE_STAGE_HOST_BIT ||
       dstStageMask == VK_PIPELINE_STAGE_HOST_BIT) {
      return;
   }

   /* If we have a recording job, finish it here */
   struct v3dv_job *job = cmd_buffer->state.job;
   if (job)
      v3dv_cmd_buffer_finish_job(cmd_buffer);

   cmd_buffer->state.has_barrier = true;
   if (dstStageMask & (VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                       VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                       VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
                       VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
                       VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
                       VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT)) {
      cmd_buffer->state.has_bcl_barrier = true;
   }
}

void
v3dv_CmdBindVertexBuffers(VkCommandBuffer commandBuffer,
                          uint32_t firstBinding,
                          uint32_t bindingCount,
                          const VkBuffer *pBuffers,
                          const VkDeviceSize *pOffsets)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   struct v3dv_vertex_binding *vb = cmd_buffer->state.vertex_bindings;

   /* We have to defer setting up vertex buffer since we need the buffer
    * stride from the pipeline.
    */

   assert(firstBinding + bindingCount <= MAX_VBS);
   bool vb_state_changed = false;
   for (uint32_t i = 0; i < bindingCount; i++) {
      if (vb[firstBinding + i].buffer != v3dv_buffer_from_handle(pBuffers[i])) {
         vb[firstBinding + i].buffer = v3dv_buffer_from_handle(pBuffers[i]);
         vb_state_changed = true;
      }
      if (vb[firstBinding + i].offset != pOffsets[i]) {
         vb[firstBinding + i].offset = pOffsets[i];
         vb_state_changed = true;
      }
   }

   if (vb_state_changed)
      cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_VERTEX_BUFFER;
}

static uint32_t
get_index_size(VkIndexType index_type)
{
   switch (index_type) {
   case VK_INDEX_TYPE_UINT16:
      return 2;
      break;
   case VK_INDEX_TYPE_UINT32:
      return 4;
      break;
   default:
      unreachable("Unsupported index type");
   }
}

void
v3dv_CmdBindIndexBuffer(VkCommandBuffer commandBuffer,
                        VkBuffer buffer,
                        VkDeviceSize offset,
                        VkIndexType indexType)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_buffer, ibuffer, buffer);

   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   v3dv_cl_ensure_space_with_branch(
      &job->bcl, cl_packet_length(INDEX_BUFFER_SETUP));
   v3dv_return_if_oom(cmd_buffer, NULL);

   const uint32_t index_size = get_index_size(indexType);

   /* If we have started a new job we always need to emit index buffer state.
    * We know we are in that scenario because that is the only case where we
    * set the dirty bit.
    */
   if (!(cmd_buffer->state.dirty & V3DV_CMD_DIRTY_INDEX_BUFFER)) {
      if (buffer == cmd_buffer->state.index_buffer.buffer &&
          offset == cmd_buffer->state.index_buffer.offset &&
          index_size == cmd_buffer->state.index_buffer.index_size) {
         return;
      }
   }

   cl_emit(&job->bcl, INDEX_BUFFER_SETUP, ib) {
      ib.address = v3dv_cl_address(ibuffer->mem->bo,
                                   ibuffer->mem_offset + offset);
      ib.size = ibuffer->mem->bo->size;
   }

   cmd_buffer->state.index_buffer.buffer = buffer;
   cmd_buffer->state.index_buffer.offset = offset;
   cmd_buffer->state.index_buffer.index_size = index_size;

   cmd_buffer->state.dirty &= ~V3DV_CMD_DIRTY_INDEX_BUFFER;
}

void
v3dv_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                              VkStencilFaceFlags faceMask,
                              uint32_t compareMask)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      cmd_buffer->state.dynamic.stencil_compare_mask.front = compareMask & 0xff;
   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      cmd_buffer->state.dynamic.stencil_compare_mask.back = compareMask & 0xff;

   cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_STENCIL_COMPARE_MASK;
}

void
v3dv_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                            VkStencilFaceFlags faceMask,
                            uint32_t writeMask)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      cmd_buffer->state.dynamic.stencil_write_mask.front = writeMask & 0xff;
   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      cmd_buffer->state.dynamic.stencil_write_mask.back = writeMask & 0xff;

   cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_STENCIL_WRITE_MASK;
}

void
v3dv_CmdSetStencilReference(VkCommandBuffer commandBuffer,
                            VkStencilFaceFlags faceMask,
                            uint32_t reference)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      cmd_buffer->state.dynamic.stencil_reference.front = reference & 0xff;
   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      cmd_buffer->state.dynamic.stencil_reference.back = reference & 0xff;

   cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_STENCIL_REFERENCE;
}

void
v3dv_CmdSetDepthBias(VkCommandBuffer commandBuffer,
                     float depthBiasConstantFactor,
                     float depthBiasClamp,
                     float depthBiasSlopeFactor)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.dynamic.depth_bias.constant_factor = depthBiasConstantFactor;
   cmd_buffer->state.dynamic.depth_bias.depth_bias_clamp = depthBiasClamp;
   cmd_buffer->state.dynamic.depth_bias.slope_factor = depthBiasSlopeFactor;
   cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_DEPTH_BIAS;
}

void
v3dv_CmdSetDepthBounds(VkCommandBuffer commandBuffer,
                       float minDepthBounds,
                       float maxDepthBounds)
{
   /* We do not support depth bounds testing so we just ingore this. We are
    * already asserting that pipelines don't enable the feature anyway.
    */
}

void
v3dv_CmdSetLineWidth(VkCommandBuffer commandBuffer,
                     float lineWidth)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->state.dynamic.line_width = lineWidth;
   cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_LINE_WIDTH;
}

void
v3dv_CmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                           VkPipelineBindPoint pipelineBindPoint,
                           VkPipelineLayout _layout,
                           uint32_t firstSet,
                           uint32_t descriptorSetCount,
                           const VkDescriptorSet *pDescriptorSets,
                           uint32_t dynamicOffsetCount,
                           const uint32_t *pDynamicOffsets)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_pipeline_layout, layout, _layout);

   uint32_t dyn_index = 0;

   assert(firstSet + descriptorSetCount <= MAX_SETS);

   struct v3dv_descriptor_state *descriptor_state =
      pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE ?
      &cmd_buffer->state.compute.descriptor_state :
      &cmd_buffer->state.gfx.descriptor_state;

   bool descriptor_state_changed = false;
   for (uint32_t i = 0; i < descriptorSetCount; i++) {
      V3DV_FROM_HANDLE(v3dv_descriptor_set, set, pDescriptorSets[i]);
      uint32_t index = firstSet + i;

      if (descriptor_state->descriptor_sets[index] != set) {
         descriptor_state->descriptor_sets[index] = set;
         descriptor_state_changed = true;
      }

      if (!(descriptor_state->valid & (1u << index))) {
         descriptor_state->valid |= (1u << index);
         descriptor_state_changed = true;
      }

      for (uint32_t j = 0; j < set->layout->dynamic_offset_count; j++, dyn_index++) {
         uint32_t idx = j + layout->set[i + firstSet].dynamic_offset_start;

         if (descriptor_state->dynamic_offsets[idx] != pDynamicOffsets[dyn_index]) {
            descriptor_state->dynamic_offsets[idx] = pDynamicOffsets[dyn_index];
            descriptor_state_changed = true;
         }
      }
   }

   if (descriptor_state_changed) {
      if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS)
         cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_DESCRIPTOR_SETS;
      else
         cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_COMPUTE_DESCRIPTOR_SETS;
   }
}

void
v3dv_CmdPushConstants(VkCommandBuffer commandBuffer,
                      VkPipelineLayout layout,
                      VkShaderStageFlags stageFlags,
                      uint32_t offset,
                      uint32_t size,
                      const void *pValues)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);

   if (!memcmp((uint8_t *) cmd_buffer->push_constants_data + offset, pValues, size))
      return;

   memcpy((uint8_t *) cmd_buffer->push_constants_data + offset, pValues, size);

   cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_PUSH_CONSTANTS;
}

void
v3dv_CmdSetBlendConstants(VkCommandBuffer commandBuffer,
                          const float blendConstants[4])
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;

   if (!memcmp(state->dynamic.blend_constants, blendConstants,
               sizeof(state->dynamic.blend_constants))) {
      return;
   }

   memcpy(state->dynamic.blend_constants, blendConstants,
          sizeof(state->dynamic.blend_constants));

   cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_BLEND_CONSTANTS;
}

void
v3dv_cmd_buffer_reset_queries(struct v3dv_cmd_buffer *cmd_buffer,
                              struct v3dv_query_pool *pool,
                              uint32_t first,
                              uint32_t count)
{
   /* Resets can only happen outside a render pass instance so we should not
    * be in the middle of job recording.
    */
   assert(cmd_buffer->state.pass == NULL);
   assert(cmd_buffer->state.job == NULL);

   assert(first < pool->query_count);
   assert(first + count <= pool->query_count);

   struct v3dv_job *job =
      v3dv_cmd_buffer_create_cpu_job(cmd_buffer->device,
                                     V3DV_JOB_TYPE_CPU_RESET_QUERIES,
                                     cmd_buffer, -1);
   v3dv_return_if_oom(cmd_buffer, NULL);

   job->cpu.query_reset.pool = pool;
   job->cpu.query_reset.first = first;
   job->cpu.query_reset.count = count;

   list_addtail(&job->list_link, &cmd_buffer->jobs);
}

static void
ensure_array_state(struct v3dv_cmd_buffer *cmd_buffer,
                   uint32_t slot_size,
                   uint32_t used_count,
                   uint32_t *alloc_count,
                   void **ptr)
{
   if (used_count >= *alloc_count) {
      const uint32_t prev_slot_count = *alloc_count;
      void *old_buffer = *ptr;

      const uint32_t new_slot_count = MAX2(*alloc_count * 2, 4);
      const uint32_t bytes = new_slot_count * slot_size;
      *ptr = vk_alloc(&cmd_buffer->device->vk.alloc, bytes, 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (*ptr == NULL) {
         fprintf(stderr, "Error: failed to allocate CPU buffer for query.\n");
         v3dv_flag_oom(cmd_buffer, NULL);
         return;
      }

      memcpy(*ptr, old_buffer, prev_slot_count * slot_size);
      *alloc_count = new_slot_count;
   }
   assert(used_count < *alloc_count);
}

void
v3dv_cmd_buffer_begin_query(struct v3dv_cmd_buffer *cmd_buffer,
                            struct v3dv_query_pool *pool,
                            uint32_t query,
                            VkQueryControlFlags flags)
{
   /* FIXME: we only support one active query for now */
   assert(cmd_buffer->state.query.active_query == NULL);
   assert(query < pool->query_count);

   cmd_buffer->state.query.active_query = pool->queries[query].bo;
   cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_OCCLUSION_QUERY;
}

void
v3dv_cmd_buffer_end_query(struct v3dv_cmd_buffer *cmd_buffer,
                          struct v3dv_query_pool *pool,
                          uint32_t query)
{
   assert(query < pool->query_count);
   assert(cmd_buffer->state.query.active_query != NULL);

   if  (cmd_buffer->state.pass) {
      /* Queue the EndQuery in the command buffer state, we will create a CPU
       * job to flag all of these queries as possibly available right after the
       * render pass job in which they have been recorded.
       */
      struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
      ensure_array_state(cmd_buffer,
                         sizeof(struct v3dv_end_query_cpu_job_info),
                         state->query.end.used_count,
                         &state->query.end.alloc_count,
                         (void **) &state->query.end.states);
      v3dv_return_if_oom(cmd_buffer, NULL);

      struct v3dv_end_query_cpu_job_info *info =
         &state->query.end.states[state->query.end.used_count++];

      info->pool = pool;
      info->query = query;
   } else {
      /* Otherwise, schedule the CPU job immediately */
      struct v3dv_job *job =
         v3dv_cmd_buffer_create_cpu_job(cmd_buffer->device,
                                        V3DV_JOB_TYPE_CPU_END_QUERY,
                                        cmd_buffer, -1);
      v3dv_return_if_oom(cmd_buffer, NULL);

      job->cpu.query_end.pool = pool;
      job->cpu.query_end.query = query;
      list_addtail(&job->list_link, &cmd_buffer->jobs);
   }

   cmd_buffer->state.query.active_query = NULL;
   cmd_buffer->state.dirty |= V3DV_CMD_DIRTY_OCCLUSION_QUERY;
}

void
v3dv_cmd_buffer_copy_query_results(struct v3dv_cmd_buffer *cmd_buffer,
                                   struct v3dv_query_pool *pool,
                                   uint32_t first,
                                   uint32_t count,
                                   struct v3dv_buffer *dst,
                                   uint32_t offset,
                                   uint32_t stride,
                                   VkQueryResultFlags flags)
{
   /* Copies can only happen outside a render pass instance so we should not
    * be in the middle of job recording.
    */
   assert(cmd_buffer->state.pass == NULL);
   assert(cmd_buffer->state.job == NULL);

   assert(first < pool->query_count);
   assert(first + count <= pool->query_count);

   struct v3dv_job *job =
      v3dv_cmd_buffer_create_cpu_job(cmd_buffer->device,
                                     V3DV_JOB_TYPE_CPU_COPY_QUERY_RESULTS,
                                     cmd_buffer, -1);
   v3dv_return_if_oom(cmd_buffer, NULL);

   job->cpu.query_copy_results.pool = pool;
   job->cpu.query_copy_results.first = first;
   job->cpu.query_copy_results.count = count;
   job->cpu.query_copy_results.dst = dst;
   job->cpu.query_copy_results.offset = offset;
   job->cpu.query_copy_results.stride = stride;
   job->cpu.query_copy_results.flags = flags;

   list_addtail(&job->list_link, &cmd_buffer->jobs);
}

void
v3dv_cmd_buffer_add_tfu_job(struct v3dv_cmd_buffer *cmd_buffer,
                            struct drm_v3d_submit_tfu *tfu)
{
   struct v3dv_device *device = cmd_buffer->device;
   struct v3dv_job *job = vk_zalloc(&device->vk.alloc,
                                    sizeof(struct v3dv_job), 8,
                                    VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!job) {
      v3dv_flag_oom(cmd_buffer, NULL);
      return;
   }

   v3dv_job_init(job, V3DV_JOB_TYPE_GPU_TFU, device, cmd_buffer, -1);
   job->tfu = *tfu;
   list_addtail(&job->list_link, &cmd_buffer->jobs);
}

void
v3dv_CmdSetEvent(VkCommandBuffer commandBuffer,
                 VkEvent _event,
                 VkPipelineStageFlags stageMask)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_event, event, _event);

   /* Event (re)sets can only happen outside a render pass instance so we
    * should not be in the middle of job recording.
    */
   assert(cmd_buffer->state.pass == NULL);
   assert(cmd_buffer->state.job == NULL);

   struct v3dv_job *job =
      v3dv_cmd_buffer_create_cpu_job(cmd_buffer->device,
                                     V3DV_JOB_TYPE_CPU_SET_EVENT,
                                     cmd_buffer, -1);
   v3dv_return_if_oom(cmd_buffer, NULL);

   job->cpu.event_set.event = event;
   job->cpu.event_set.state = 1;

   list_addtail(&job->list_link, &cmd_buffer->jobs);
}

void
v3dv_CmdResetEvent(VkCommandBuffer commandBuffer,
                   VkEvent _event,
                   VkPipelineStageFlags stageMask)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_event, event, _event);

   /* Event (re)sets can only happen outside a render pass instance so we
    * should not be in the middle of job recording.
    */
   assert(cmd_buffer->state.pass == NULL);
   assert(cmd_buffer->state.job == NULL);

   struct v3dv_job *job =
      v3dv_cmd_buffer_create_cpu_job(cmd_buffer->device,
                                     V3DV_JOB_TYPE_CPU_SET_EVENT,
                                     cmd_buffer, -1);
   v3dv_return_if_oom(cmd_buffer, NULL);

   job->cpu.event_set.event = event;
   job->cpu.event_set.state = 0;

   list_addtail(&job->list_link, &cmd_buffer->jobs);
}

void
v3dv_CmdWaitEvents(VkCommandBuffer commandBuffer,
                   uint32_t eventCount,
                   const VkEvent *pEvents,
                   VkPipelineStageFlags srcStageMask,
                   VkPipelineStageFlags dstStageMask,
                   uint32_t memoryBarrierCount,
                   const VkMemoryBarrier *pMemoryBarriers,
                   uint32_t bufferMemoryBarrierCount,
                   const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                   uint32_t imageMemoryBarrierCount,
                   const VkImageMemoryBarrier *pImageMemoryBarriers)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);

   assert(eventCount > 0);

   struct v3dv_job *job =
      v3dv_cmd_buffer_create_cpu_job(cmd_buffer->device,
                                     V3DV_JOB_TYPE_CPU_WAIT_EVENTS,
                                     cmd_buffer, -1);
   v3dv_return_if_oom(cmd_buffer, NULL);

   const uint32_t event_list_size = sizeof(struct v3dv_event *) * eventCount;

   job->cpu.event_wait.events =
      vk_alloc(&cmd_buffer->device->vk.alloc, event_list_size, 8,
               VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!job->cpu.event_wait.events) {
      v3dv_flag_oom(cmd_buffer, NULL);
      return;
   }
   job->cpu.event_wait.event_count = eventCount;

   for (uint32_t i = 0; i < eventCount; i++)
      job->cpu.event_wait.events[i] = v3dv_event_from_handle(pEvents[i]);

   /* vkCmdWaitEvents can be recorded inside a render pass, so we might have
    * an active job.
    *
    * If we are inside a render pass, because we vkCmd(Re)SetEvent can't happen
    * inside a render pass, it is safe to move the wait job so it happens right
    * before the current job we are currently recording for the subpass, if any
    * (it would actually be safe to move it all the way back to right before
    * the start of the render pass).
    *
    * If we are outside a render pass then we should not have any on-going job
    * and we are free to just add the wait job without restrictions.
    */
   assert(cmd_buffer->state.pass || !cmd_buffer->state.job);
   list_addtail(&job->list_link, &cmd_buffer->jobs);
}

void
v3dv_CmdWriteTimestamp(VkCommandBuffer commandBuffer,
                       VkPipelineStageFlagBits pipelineStage,
                       VkQueryPool queryPool,
                       uint32_t query)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_query_pool, query_pool, queryPool);

   /* If this is called inside a render pass we need to finish the current
    * job here...
    */
   if (cmd_buffer->state.pass)
      v3dv_cmd_buffer_finish_job(cmd_buffer);

   struct v3dv_job *job =
      v3dv_cmd_buffer_create_cpu_job(cmd_buffer->device,
                                     V3DV_JOB_TYPE_CPU_TIMESTAMP_QUERY,
                                     cmd_buffer, -1);
   v3dv_return_if_oom(cmd_buffer, NULL);

   job->cpu.query_timestamp.pool = query_pool;
   job->cpu.query_timestamp.query = query;

   list_addtail(&job->list_link, &cmd_buffer->jobs);
   cmd_buffer->state.job = NULL;

   /* ...and resume the subpass after the timestamp */
   if (cmd_buffer->state.pass)
      v3dv_cmd_buffer_subpass_resume(cmd_buffer, cmd_buffer->state.subpass_idx);
}

static void
cmd_buffer_emit_pre_dispatch(struct v3dv_cmd_buffer *cmd_buffer)
{
   assert(cmd_buffer->state.compute.pipeline);
   assert(cmd_buffer->state.compute.pipeline->active_stages ==
          VK_SHADER_STAGE_COMPUTE_BIT);

   uint32_t *dirty = &cmd_buffer->state.dirty;
   *dirty &= ~(V3DV_CMD_DIRTY_COMPUTE_PIPELINE |
               V3DV_CMD_DIRTY_COMPUTE_DESCRIPTOR_SETS);
}

#define V3D_CSD_CFG012_WG_COUNT_SHIFT 16
#define V3D_CSD_CFG012_WG_OFFSET_SHIFT 0
/* Allow this dispatch to start while the last one is still running. */
#define V3D_CSD_CFG3_OVERLAP_WITH_PREV (1 << 26)
/* Maximum supergroup ID.  6 bits. */
#define V3D_CSD_CFG3_MAX_SG_ID_SHIFT 20
/* Batches per supergroup minus 1.  8 bits. */
#define V3D_CSD_CFG3_BATCHES_PER_SG_M1_SHIFT 12
/* Workgroups per supergroup, 0 means 16 */
#define V3D_CSD_CFG3_WGS_PER_SG_SHIFT 8
#define V3D_CSD_CFG3_WG_SIZE_SHIFT 0

#define V3D_CSD_CFG5_PROPAGATE_NANS (1 << 2)
#define V3D_CSD_CFG5_SINGLE_SEG (1 << 1)
#define V3D_CSD_CFG5_THREADING (1 << 0)

void
v3dv_cmd_buffer_rewrite_indirect_csd_job(
   struct v3dv_csd_indirect_cpu_job_info *info,
   const uint32_t *wg_counts)
{
   assert(info->csd_job);
   struct v3dv_job *job = info->csd_job;

   assert(job->type == V3DV_JOB_TYPE_GPU_CSD);
   assert(wg_counts[0] > 0 && wg_counts[1] > 0 && wg_counts[2] > 0);

   struct drm_v3d_submit_csd *submit = &job->csd.submit;

   job->csd.wg_count[0] = wg_counts[0];
   job->csd.wg_count[1] = wg_counts[1];
   job->csd.wg_count[2] = wg_counts[2];

   submit->cfg[0] = wg_counts[0] << V3D_CSD_CFG012_WG_COUNT_SHIFT;
   submit->cfg[1] = wg_counts[1] << V3D_CSD_CFG012_WG_COUNT_SHIFT;
   submit->cfg[2] = wg_counts[2] << V3D_CSD_CFG012_WG_COUNT_SHIFT;

   submit->cfg[4] = DIV_ROUND_UP(info->wg_size, 16) *
                    (wg_counts[0] * wg_counts[1] * wg_counts[2]) - 1;
   assert(submit->cfg[4] != ~0);

   if (info->needs_wg_uniform_rewrite) {
      /* Make sure the GPU is not currently accessing the indirect CL for this
       * job, since we are about to overwrite some of the uniform data.
       */
      v3dv_bo_wait(job->device, job->indirect.bo, PIPE_TIMEOUT_INFINITE);

      for (uint32_t i = 0; i < 3; i++) {
         if (info->wg_uniform_offsets[i]) {
            /* Sanity check that our uniform pointers are within the allocated
             * BO space for our indirect CL.
             */
            assert(info->wg_uniform_offsets[i] >= (uint32_t *) job->indirect.base);
            assert(info->wg_uniform_offsets[i] < (uint32_t *) job->indirect.next);
            *(info->wg_uniform_offsets[i]) = wg_counts[i];
         }
      }
   }
}

static struct v3dv_job *
cmd_buffer_create_csd_job(struct v3dv_cmd_buffer *cmd_buffer,
                          uint32_t group_count_x,
                          uint32_t group_count_y,
                          uint32_t group_count_z,
                          uint32_t **wg_uniform_offsets_out,
                          uint32_t *wg_size_out)
{
   struct v3dv_pipeline *pipeline = cmd_buffer->state.compute.pipeline;
   assert(pipeline && pipeline->shared_data->variants[BROADCOM_SHADER_COMPUTE]);
   struct v3dv_shader_variant *cs_variant =
      pipeline->shared_data->variants[BROADCOM_SHADER_COMPUTE];

   struct v3dv_job *job = vk_zalloc(&cmd_buffer->device->vk.alloc,
                                    sizeof(struct v3dv_job), 8,
                                    VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!job) {
      v3dv_flag_oom(cmd_buffer, NULL);
      return NULL;
   }

   v3dv_job_init(job, V3DV_JOB_TYPE_GPU_CSD, cmd_buffer->device, cmd_buffer, -1);
   cmd_buffer->state.job = job;

   struct drm_v3d_submit_csd *submit = &job->csd.submit;

   job->csd.wg_count[0] = group_count_x;
   job->csd.wg_count[1] = group_count_y;
   job->csd.wg_count[2] = group_count_z;

   submit->cfg[0] |= group_count_x << V3D_CSD_CFG012_WG_COUNT_SHIFT;
   submit->cfg[1] |= group_count_y << V3D_CSD_CFG012_WG_COUNT_SHIFT;
   submit->cfg[2] |= group_count_z << V3D_CSD_CFG012_WG_COUNT_SHIFT;

   const struct v3d_compute_prog_data *cpd =
      cs_variant->prog_data.cs;

   const uint32_t wgs_per_sg = 1; /* FIXME */
   const uint32_t wg_size = cpd->local_size[0] *
                            cpd->local_size[1] *
                            cpd->local_size[2];
   submit->cfg[3] |= wgs_per_sg << V3D_CSD_CFG3_WGS_PER_SG_SHIFT;
   submit->cfg[3] |= ((DIV_ROUND_UP(wgs_per_sg * wg_size, 16) - 1) <<
                       V3D_CSD_CFG3_BATCHES_PER_SG_M1_SHIFT);
   submit->cfg[3] |= (wg_size & 0xff) << V3D_CSD_CFG3_WG_SIZE_SHIFT;
   if (wg_size_out)
      *wg_size_out = wg_size;

   uint32_t batches_per_wg = DIV_ROUND_UP(wg_size, 16);
   submit->cfg[4] = batches_per_wg *
                    (group_count_x * group_count_y * group_count_z) - 1;
   assert(submit->cfg[4] != ~0);

   assert(pipeline->shared_data->assembly_bo);
   struct v3dv_bo *cs_assembly_bo = pipeline->shared_data->assembly_bo;

   submit->cfg[5] = cs_assembly_bo->offset + cs_variant->assembly_offset;
   submit->cfg[5] |= V3D_CSD_CFG5_PROPAGATE_NANS;
   if (cs_variant->prog_data.base->single_seg)
      submit->cfg[5] |= V3D_CSD_CFG5_SINGLE_SEG;
   if (cs_variant->prog_data.base->threads == 4)
      submit->cfg[5] |= V3D_CSD_CFG5_THREADING;

   if (cs_variant->prog_data.cs->shared_size > 0) {
      job->csd.shared_memory =
         v3dv_bo_alloc(cmd_buffer->device,
                       cs_variant->prog_data.cs->shared_size * wgs_per_sg,
                       "shared_vars", true);
      if (!job->csd.shared_memory) {
         v3dv_flag_oom(cmd_buffer, NULL);
         return job;
      }
   }

   v3dv_job_add_bo(job, cs_assembly_bo);
   struct v3dv_cl_reloc uniforms =
      v3dv_write_uniforms_wg_offsets(cmd_buffer, pipeline,
                                     cs_variant,
                                     wg_uniform_offsets_out);
   submit->cfg[6] = uniforms.bo->offset + uniforms.offset;

   v3dv_job_add_bo(job, uniforms.bo);

   return job;
}

static void
cmd_buffer_dispatch(struct v3dv_cmd_buffer *cmd_buffer,
                    uint32_t group_count_x,
                    uint32_t group_count_y,
                    uint32_t group_count_z)
{
   if (group_count_x == 0 || group_count_y == 0 || group_count_z == 0)
      return;

   struct v3dv_job *job =
      cmd_buffer_create_csd_job(cmd_buffer,
                                group_count_x,
                                group_count_y,
                                group_count_z,
                                NULL, NULL);

   list_addtail(&job->list_link, &cmd_buffer->jobs);
   cmd_buffer->state.job = NULL;
}

void
v3dv_CmdDispatch(VkCommandBuffer commandBuffer,
                 uint32_t groupCountX,
                 uint32_t groupCountY,
                 uint32_t groupCountZ)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);

   cmd_buffer_emit_pre_dispatch(cmd_buffer);
   cmd_buffer_dispatch(cmd_buffer, groupCountX, groupCountY, groupCountZ);
}

static void
cmd_buffer_dispatch_indirect(struct v3dv_cmd_buffer *cmd_buffer,
                             struct v3dv_buffer *buffer,
                             uint32_t offset)
{
   /* We can't do indirect dispatches, so instead we record a CPU job that,
    * when executed in the queue, will map the indirect buffer, read the
    * dispatch parameters, and submit a regular dispatch.
    */
   struct v3dv_job *job =
      v3dv_cmd_buffer_create_cpu_job(cmd_buffer->device,
                                     V3DV_JOB_TYPE_CPU_CSD_INDIRECT,
                                     cmd_buffer, -1);
   v3dv_return_if_oom(cmd_buffer, NULL);

   /* We need to create a CSD job now, even if we still don't know the actual
    * dispatch parameters, because the job setup needs to be done using the
    * current command buffer state (i.e. pipeline, descriptor sets, push
    * constants, etc.). So we create the job with default dispatch parameters
    * and we will rewrite the parts we need at submit time if the indirect
    * parameters don't match the ones we used to setup the job.
    */
   struct v3dv_job *csd_job =
      cmd_buffer_create_csd_job(cmd_buffer,
                                1, 1, 1,
                                &job->cpu.csd_indirect.wg_uniform_offsets[0],
                                &job->cpu.csd_indirect.wg_size);
   v3dv_return_if_oom(cmd_buffer, NULL);
   assert(csd_job);

   job->cpu.csd_indirect.buffer = buffer;
   job->cpu.csd_indirect.offset = offset;
   job->cpu.csd_indirect.csd_job = csd_job;

   /* If the compute shader reads the workgroup sizes we will also need to
    * rewrite the corresponding uniforms.
    */
   job->cpu.csd_indirect.needs_wg_uniform_rewrite =
      job->cpu.csd_indirect.wg_uniform_offsets[0] ||
      job->cpu.csd_indirect.wg_uniform_offsets[1] ||
      job->cpu.csd_indirect.wg_uniform_offsets[2];

   list_addtail(&job->list_link, &cmd_buffer->jobs);
   cmd_buffer->state.job = NULL;
}

void
v3dv_CmdDispatchIndirect(VkCommandBuffer commandBuffer,
                         VkBuffer _buffer,
                         VkDeviceSize offset)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_buffer, buffer, _buffer);

   assert(offset <= UINT32_MAX);

   cmd_buffer_emit_pre_dispatch(cmd_buffer);
   cmd_buffer_dispatch_indirect(cmd_buffer, buffer, offset);
}
