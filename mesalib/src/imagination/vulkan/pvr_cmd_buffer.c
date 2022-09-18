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
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "hwdef/rogue_hw_defs.h"
#include "hwdef/rogue_hw_utils.h"
#include "pvr_bo.h"
#include "pvr_csb.h"
#include "pvr_csb_enum_helpers.h"
#include "pvr_device_info.h"
#include "pvr_end_of_tile.h"
#include "pvr_formats.h"
#include "pvr_hw_pass.h"
#include "pvr_job_common.h"
#include "pvr_job_render.h"
#include "pvr_limits.h"
#include "pvr_pds.h"
#include "pvr_private.h"
#include "pvr_types.h"
#include "pvr_winsys.h"
#include "util/bitscan.h"
#include "util/compiler.h"
#include "util/list.h"
#include "util/macros.h"
#include "util/u_dynarray.h"
#include "util/u_pack_color.h"
#include "vk_alloc.h"
#include "vk_command_buffer.h"
#include "vk_command_pool.h"
#include "vk_format.h"
#include "vk_log.h"
#include "vk_object.h"
#include "vk_util.h"

/* Structure used to pass data into pvr_compute_generate_control_stream()
 * function.
 */
struct pvr_compute_kernel_info {
   pvr_dev_addr_t indirect_buffer_addr;
   bool global_offsets_present;
   uint32_t usc_common_size;
   uint32_t usc_unified_size;
   uint32_t pds_temp_size;
   uint32_t pds_data_size;
   enum PVRX(CDMCTRL_USC_TARGET) usc_target;
   bool is_fence;
   uint32_t pds_data_offset;
   uint32_t pds_code_offset;
   enum PVRX(CDMCTRL_SD_TYPE) sd_type;
   bool usc_common_shared;
   uint32_t local_size[PVR_WORKGROUP_DIMENSIONS];
   uint32_t global_size[PVR_WORKGROUP_DIMENSIONS];
   uint32_t max_instances;
};

static void pvr_cmd_buffer_free_sub_cmd(struct pvr_cmd_buffer *cmd_buffer,
                                        struct pvr_sub_cmd *sub_cmd)
{
   switch (sub_cmd->type) {
   case PVR_SUB_CMD_TYPE_GRAPHICS:
      pvr_csb_finish(&sub_cmd->gfx.control_stream);
      pvr_bo_free(cmd_buffer->device, sub_cmd->gfx.depth_bias_bo);
      pvr_bo_free(cmd_buffer->device, sub_cmd->gfx.scissor_bo);
      break;

   case PVR_SUB_CMD_TYPE_COMPUTE:
      pvr_csb_finish(&sub_cmd->compute.control_stream);
      break;

   case PVR_SUB_CMD_TYPE_TRANSFER:
      list_for_each_entry_safe (struct pvr_transfer_cmd,
                                transfer_cmd,
                                &sub_cmd->transfer.transfer_cmds,
                                link) {
         list_del(&transfer_cmd->link);
         vk_free(&cmd_buffer->vk.pool->alloc, transfer_cmd);
      }
      break;

   case PVR_SUB_CMD_TYPE_EVENT:
      if (sub_cmd->event.type == PVR_EVENT_TYPE_WAIT)
         vk_free(&cmd_buffer->vk.pool->alloc, sub_cmd->event.wait.events);

      break;

   default:
      pvr_finishme("Unsupported sub-command type %d", sub_cmd->type);
      break;
   }

   list_del(&sub_cmd->link);
   vk_free(&cmd_buffer->vk.pool->alloc, sub_cmd);
}

static void pvr_cmd_buffer_free_sub_cmds(struct pvr_cmd_buffer *cmd_buffer)
{
   list_for_each_entry_safe (struct pvr_sub_cmd,
                             sub_cmd,
                             &cmd_buffer->sub_cmds,
                             link) {
      pvr_cmd_buffer_free_sub_cmd(cmd_buffer, sub_cmd);
   }
}

static void pvr_cmd_buffer_free_resources(struct pvr_cmd_buffer *cmd_buffer)
{
   vk_free(&cmd_buffer->vk.pool->alloc,
           cmd_buffer->state.render_pass_info.attachments);
   vk_free(&cmd_buffer->vk.pool->alloc,
           cmd_buffer->state.render_pass_info.clear_values);

   pvr_cmd_buffer_free_sub_cmds(cmd_buffer);

   list_for_each_entry_safe (struct pvr_bo, bo, &cmd_buffer->bo_list, link) {
      list_del(&bo->link);
      pvr_bo_free(cmd_buffer->device, bo);
   }

   util_dynarray_fini(&cmd_buffer->scissor_array);
   util_dynarray_fini(&cmd_buffer->depth_bias_array);
}

static void pvr_cmd_buffer_reset(struct pvr_cmd_buffer *cmd_buffer)
{
   if (cmd_buffer->status != PVR_CMD_BUFFER_STATUS_INITIAL) {
      /* FIXME: For now we always free all resources as if
       * VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT was set.
       */
      pvr_cmd_buffer_free_resources(cmd_buffer);

      vk_command_buffer_reset(&cmd_buffer->vk);

      memset(&cmd_buffer->state, 0, sizeof(cmd_buffer->state));
      memset(cmd_buffer->scissor_words, 0, sizeof(cmd_buffer->scissor_words));

      cmd_buffer->usage_flags = 0;
      cmd_buffer->state.status = VK_SUCCESS;
      cmd_buffer->status = PVR_CMD_BUFFER_STATUS_INITIAL;
   }
}

static void pvr_cmd_buffer_destroy(struct vk_command_buffer *vk_cmd_buffer)
{
   struct pvr_cmd_buffer *cmd_buffer =
      container_of(vk_cmd_buffer, struct pvr_cmd_buffer, vk);

   pvr_cmd_buffer_free_resources(cmd_buffer);
   vk_command_buffer_finish(&cmd_buffer->vk);
   vk_free(&cmd_buffer->vk.pool->alloc, cmd_buffer);
}

static const struct vk_command_buffer_ops cmd_buffer_ops = {
   .destroy = pvr_cmd_buffer_destroy,
};

static VkResult pvr_cmd_buffer_create(struct pvr_device *device,
                                      struct vk_command_pool *pool,
                                      VkCommandBufferLevel level,
                                      VkCommandBuffer *pCommandBuffer)
{
   struct pvr_cmd_buffer *cmd_buffer;
   VkResult result;

   cmd_buffer = vk_zalloc(&pool->alloc,
                          sizeof(*cmd_buffer),
                          8U,
                          VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmd_buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   result =
      vk_command_buffer_init(pool, &cmd_buffer->vk, &cmd_buffer_ops, level);
   if (result != VK_SUCCESS) {
      vk_free(&pool->alloc, cmd_buffer);
      return result;
   }

   cmd_buffer->device = device;

   util_dynarray_init(&cmd_buffer->depth_bias_array, NULL);
   util_dynarray_init(&cmd_buffer->scissor_array, NULL);

   cmd_buffer->state.status = VK_SUCCESS;
   cmd_buffer->status = PVR_CMD_BUFFER_STATUS_INITIAL;

   list_inithead(&cmd_buffer->sub_cmds);
   list_inithead(&cmd_buffer->bo_list);

   *pCommandBuffer = pvr_cmd_buffer_to_handle(cmd_buffer);

   return VK_SUCCESS;
}

VkResult
pvr_AllocateCommandBuffers(VkDevice _device,
                           const VkCommandBufferAllocateInfo *pAllocateInfo,
                           VkCommandBuffer *pCommandBuffers)
{
   VK_FROM_HANDLE(vk_command_pool, pool, pAllocateInfo->commandPool);
   PVR_FROM_HANDLE(pvr_device, device, _device);
   VkResult result = VK_SUCCESS;
   uint32_t i;

   for (i = 0; i < pAllocateInfo->commandBufferCount; i++) {
      result = pvr_cmd_buffer_create(device,
                                     pool,
                                     pAllocateInfo->level,
                                     &pCommandBuffers[i]);
      if (result != VK_SUCCESS)
         break;
   }

   if (result != VK_SUCCESS) {
      while (i--) {
         VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, pCommandBuffers[i]);
         pvr_cmd_buffer_destroy(cmd_buffer);
      }

      for (i = 0; i < pAllocateInfo->commandBufferCount; i++)
         pCommandBuffers[i] = VK_NULL_HANDLE;
   }

   return result;
}

static void pvr_cmd_buffer_update_barriers(struct pvr_cmd_buffer *cmd_buffer,
                                           enum pvr_sub_cmd_type type)
{
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   uint32_t barriers;

   switch (type) {
   case PVR_SUB_CMD_TYPE_GRAPHICS:
      barriers = PVR_PIPELINE_STAGE_GEOM_BIT | PVR_PIPELINE_STAGE_FRAG_BIT;
      break;

   case PVR_SUB_CMD_TYPE_COMPUTE:
      barriers = PVR_PIPELINE_STAGE_COMPUTE_BIT;
      break;

   case PVR_SUB_CMD_TYPE_TRANSFER:
      barriers = PVR_PIPELINE_STAGE_TRANSFER_BIT;
      break;

   case PVR_SUB_CMD_TYPE_EVENT:
      barriers = 0;
      break;

   default:
      barriers = 0;
      pvr_finishme("Unsupported sub-command type %d", type);
      break;
   }

   for (uint32_t i = 0; i < ARRAY_SIZE(state->barriers_needed); i++)
      state->barriers_needed[i] |= barriers;
}

static VkResult
pvr_cmd_buffer_upload_tables(struct pvr_device *device,
                             struct pvr_cmd_buffer *cmd_buffer,
                             struct pvr_sub_cmd_gfx *const sub_cmd)
{
   const uint32_t cache_line_size =
      rogue_get_slc_cache_line_size(&device->pdevice->dev_info);
   VkResult result;

   assert(!sub_cmd->depth_bias_bo && !sub_cmd->scissor_bo);

   if (cmd_buffer->depth_bias_array.size > 0) {
      result =
         pvr_gpu_upload(device,
                        device->heaps.general_heap,
                        util_dynarray_begin(&cmd_buffer->depth_bias_array),
                        cmd_buffer->depth_bias_array.size,
                        cache_line_size,
                        &sub_cmd->depth_bias_bo);
      if (result != VK_SUCCESS)
         return result;
   }

   if (cmd_buffer->scissor_array.size > 0) {
      result = pvr_gpu_upload(device,
                              device->heaps.general_heap,
                              util_dynarray_begin(&cmd_buffer->scissor_array),
                              cmd_buffer->scissor_array.size,
                              cache_line_size,
                              &sub_cmd->scissor_bo);
      if (result != VK_SUCCESS)
         goto err_free_depth_bias_bo;
   }

   util_dynarray_clear(&cmd_buffer->depth_bias_array);
   util_dynarray_clear(&cmd_buffer->scissor_array);

   return VK_SUCCESS;

err_free_depth_bias_bo:
   pvr_bo_free(device, sub_cmd->depth_bias_bo);
   sub_cmd->depth_bias_bo = NULL;

   return result;
}

static VkResult
pvr_cmd_buffer_emit_ppp_state(struct pvr_cmd_buffer *cmd_buffer,
                              struct pvr_sub_cmd_gfx *const sub_cmd)
{
   struct pvr_framebuffer *framebuffer =
      cmd_buffer->state.render_pass_info.framebuffer;

   pvr_csb_emit (&sub_cmd->control_stream, VDMCTRL_PPP_STATE0, state0) {
      state0.addrmsb = framebuffer->ppp_state_bo->vma->dev_addr;
      state0.word_count = framebuffer->ppp_state_size;
   }

   pvr_csb_emit (&sub_cmd->control_stream, VDMCTRL_PPP_STATE1, state1) {
      state1.addrlsb = framebuffer->ppp_state_bo->vma->dev_addr;
   }

   return VK_SUCCESS;
}

static VkResult
pvr_cmd_buffer_upload_general(struct pvr_cmd_buffer *const cmd_buffer,
                              const void *const data,
                              const size_t size,
                              struct pvr_bo **const pvr_bo_out)
{
   struct pvr_device *const device = cmd_buffer->device;
   const uint32_t cache_line_size =
      rogue_get_slc_cache_line_size(&device->pdevice->dev_info);
   struct pvr_bo *pvr_bo;
   VkResult result;

   result = pvr_gpu_upload(device,
                           device->heaps.general_heap,
                           data,
                           size,
                           cache_line_size,
                           &pvr_bo);
   if (result != VK_SUCCESS)
      return result;

   list_add(&pvr_bo->link, &cmd_buffer->bo_list);

   *pvr_bo_out = pvr_bo;

   return VK_SUCCESS;
}

static VkResult
pvr_cmd_buffer_upload_usc(struct pvr_cmd_buffer *const cmd_buffer,
                          const void *const code,
                          const size_t code_size,
                          uint64_t code_alignment,
                          struct pvr_bo **const pvr_bo_out)
{
   struct pvr_device *const device = cmd_buffer->device;
   const uint32_t cache_line_size =
      rogue_get_slc_cache_line_size(&device->pdevice->dev_info);
   struct pvr_bo *pvr_bo;
   VkResult result;

   code_alignment = MAX2(code_alignment, cache_line_size);

   result =
      pvr_gpu_upload_usc(device, code, code_size, code_alignment, &pvr_bo);
   if (result != VK_SUCCESS)
      return result;

   list_add(&pvr_bo->link, &cmd_buffer->bo_list);

   *pvr_bo_out = pvr_bo;

   return VK_SUCCESS;
}

static VkResult
pvr_cmd_buffer_upload_pds(struct pvr_cmd_buffer *const cmd_buffer,
                          const uint32_t *data,
                          uint32_t data_size_dwords,
                          uint32_t data_alignment,
                          const uint32_t *code,
                          uint32_t code_size_dwords,
                          uint32_t code_alignment,
                          uint64_t min_alignment,
                          struct pvr_pds_upload *const pds_upload_out)
{
   struct pvr_device *const device = cmd_buffer->device;
   VkResult result;

   result = pvr_gpu_upload_pds(device,
                               data,
                               data_size_dwords,
                               data_alignment,
                               code,
                               code_size_dwords,
                               code_alignment,
                               min_alignment,
                               pds_upload_out);
   if (result != VK_SUCCESS)
      return result;

   list_add(&pds_upload_out->pvr_bo->link, &cmd_buffer->bo_list);

   return VK_SUCCESS;
}

static inline VkResult
pvr_cmd_buffer_upload_pds_data(struct pvr_cmd_buffer *const cmd_buffer,
                               const uint32_t *data,
                               uint32_t data_size_dwords,
                               uint32_t data_alignment,
                               struct pvr_pds_upload *const pds_upload_out)
{
   return pvr_cmd_buffer_upload_pds(cmd_buffer,
                                    data,
                                    data_size_dwords,
                                    data_alignment,
                                    NULL,
                                    0,
                                    0,
                                    data_alignment,
                                    pds_upload_out);
}

static VkResult pvr_sub_cmd_gfx_per_job_fragment_programs_create_and_upload(
   struct pvr_cmd_buffer *const cmd_buffer,
   const uint32_t pbe_cs_words[static const ROGUE_NUM_PBESTATE_STATE_WORDS],
   struct pvr_pds_upload *const pds_upload_out)
{
   struct pvr_pds_event_program pixel_event_program = {
      /* No data to DMA, just a DOUTU needed. */
      .num_emit_word_pairs = 0,
   };
   const uint32_t staging_buffer_size =
      cmd_buffer->device->pixel_event_data_size_in_dwords * sizeof(uint32_t);
   const VkAllocationCallbacks *const allocator = &cmd_buffer->vk.pool->alloc;
   struct pvr_device *const device = cmd_buffer->device;
   /* FIXME: This should come from the compiler for the USC pixel program. */
   const uint32_t usc_temp_count = 0;
   struct pvr_bo *usc_eot_program;
   uint8_t *usc_eot_program_ptr;
   uint32_t *staging_buffer;
   VkResult result;

   result = pvr_cmd_buffer_upload_usc(cmd_buffer,
                                      pvr_end_of_tile_program,
                                      sizeof(pvr_end_of_tile_program),
                                      4,
                                      &usc_eot_program);
   if (result != VK_SUCCESS)
      return result;

   assert((pbe_cs_words[1] & 0x3F) == 0x20);

   /* FIXME: Stop patching the framebuffer address (this will require the
    * end-of-tile program to be generated at run-time).
    */
   pvr_bo_cpu_map(device, usc_eot_program);
   usc_eot_program_ptr = usc_eot_program->bo->map;
   usc_eot_program_ptr[6] = (pbe_cs_words[0] >> 0) & 0xFF;
   usc_eot_program_ptr[7] = (pbe_cs_words[0] >> 8) & 0xFF;
   usc_eot_program_ptr[8] = (pbe_cs_words[0] >> 16) & 0xFF;
   usc_eot_program_ptr[9] = (pbe_cs_words[0] >> 24) & 0xFF;
   pvr_bo_cpu_unmap(device, usc_eot_program);

   pvr_pds_setup_doutu(&pixel_event_program.task_control,
                       usc_eot_program->vma->dev_addr.addr,
                       usc_temp_count,
                       PVRX(PDSINST_DOUTU_SAMPLE_RATE_INSTANCE),
                       false);

   /* TODO: We could skip allocating this and generate directly into the device
    * buffer thus removing one allocation and memcpy() per job. Would this
    * speed up things in a noticeable way?
    */
   staging_buffer = vk_alloc(allocator,
                             staging_buffer_size,
                             8,
                             VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_usc_pixel_program;
   }

   /* Generate the data segment. The code segment was uploaded earlier when
    * setting up the PDS static heap data.
    */
   pvr_pds_generate_pixel_event_data_segment(&pixel_event_program,
                                             staging_buffer,
                                             &device->pdevice->dev_info);

   result = pvr_cmd_buffer_upload_pds_data(
      cmd_buffer,
      staging_buffer,
      cmd_buffer->device->pixel_event_data_size_in_dwords,
      4,
      pds_upload_out);
   if (result != VK_SUCCESS)
      goto err_free_pixel_event_staging_buffer;

   vk_free(allocator, staging_buffer);

   return VK_SUCCESS;

err_free_pixel_event_staging_buffer:
   vk_free(allocator, staging_buffer);

err_free_usc_pixel_program:
   list_del(&usc_eot_program->link);
   pvr_bo_free(device, usc_eot_program);

   return result;
}

static uint32_t pvr_get_hw_clear_color(VkFormat vk_format,
                                       const VkClearValue *clear_value)
{
   union util_color uc = { .ui = 0 };

   switch (vk_format) {
   case VK_FORMAT_B8G8R8A8_UNORM:
      util_pack_color(clear_value->color.float32,
                      PIPE_FORMAT_R8G8B8A8_UNORM,
                      &uc);
      break;

   default:
      assert(!"Unsupported format");
      uc.ui[0] = 0;
      break;
   }

   return uc.ui[0];
}

static VkResult
pvr_load_op_constants_create_and_upload(struct pvr_cmd_buffer *cmd_buffer,
                                        uint32_t idx,
                                        pvr_dev_addr_t *const addr_out)
{
   const struct pvr_render_pass_info *render_pass_info =
      &cmd_buffer->state.render_pass_info;
   const struct pvr_render_pass *pass = render_pass_info->pass;
   const struct pvr_renderpass_hwsetup_render *hw_render =
      &pass->hw_setup->renders[idx];
   ASSERTED const struct pvr_load_op *load_op = hw_render->client_data;
   const struct pvr_renderpass_colorinit *color_init =
      &hw_render->color_init[0];
   const struct pvr_render_pass_attachment *attachment =
      &pass->attachments[color_init->driver_id];
   const VkClearValue *clear_value =
      &render_pass_info->clear_values[color_init->driver_id];
   uint32_t hw_clear_value;
   struct pvr_bo *clear_bo;
   VkResult result;

   pvr_finishme("Add missing load op data support");

   assert(load_op->is_hw_object);
   assert(hw_render->color_init_count == 1);

   /* FIXME: add support for RENDERPASS_SURFACE_INITOP_LOAD. */
   assert(color_init->op == RENDERPASS_SURFACE_INITOP_CLEAR);

   /* FIXME: do this at the point we store the clear values? */
   hw_clear_value = pvr_get_hw_clear_color(attachment->vk_format, clear_value);

   result = pvr_cmd_buffer_upload_general(cmd_buffer,
                                          &hw_clear_value,
                                          sizeof(hw_clear_value),
                                          &clear_bo);
   if (result != VK_SUCCESS)
      return result;

   *addr_out = clear_bo->vma->dev_addr;

   return VK_SUCCESS;
}

static VkResult pvr_load_op_pds_data_create_and_upload(
   struct pvr_cmd_buffer *cmd_buffer,
   uint32_t idx,
   pvr_dev_addr_t constants_addr,
   struct pvr_pds_upload *const pds_upload_out)
{
   const struct pvr_render_pass_info *render_pass_info =
      &cmd_buffer->state.render_pass_info;
   const struct pvr_load_op *load_op =
      render_pass_info->pass->hw_setup->renders[idx].client_data;
   struct pvr_device *device = cmd_buffer->device;
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   struct pvr_pds_pixel_shader_sa_program program = { 0 };
   uint32_t staging_buffer_size;
   uint32_t *staging_buffer;
   VkResult result;

   program.num_texture_dma_kicks = 1;

   pvr_csb_pack (&program.texture_dma_address[0],
                 PDSINST_DOUT_FIELDS_DOUTD_SRC0,
                 value) {
      value.sbase = constants_addr;
   }

   pvr_csb_pack (&program.texture_dma_control[0],
                 PDSINST_DOUT_FIELDS_DOUTD_SRC1,
                 value) {
      value.dest = PVRX(PDSINST_DOUTD_DEST_COMMON_STORE);
      value.a0 = load_op->shareds_dest_offset;
      value.bsize = load_op->shareds_count;
   }

   pvr_pds_set_sizes_pixel_shader_sa_texture_data(&program, dev_info);

   staging_buffer_size = program.data_size * sizeof(*staging_buffer);

   staging_buffer = vk_alloc(&cmd_buffer->vk.pool->alloc,
                             staging_buffer_size,
                             8,
                             VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   pvr_pds_generate_pixel_shader_sa_texture_state_data(&program,
                                                       staging_buffer,
                                                       dev_info);

   result = pvr_cmd_buffer_upload_pds_data(cmd_buffer,
                                           staging_buffer,
                                           program.data_size,
                                           1,
                                           pds_upload_out);
   if (result != VK_SUCCESS) {
      vk_free(&cmd_buffer->vk.pool->alloc, staging_buffer);
      return result;
   }

   vk_free(&cmd_buffer->vk.pool->alloc, staging_buffer);

   return VK_SUCCESS;
}

/* FIXME: Should this function be specific to the HW background object, in
 * which case its name should be changed, or should it have the load op
 * structure passed in?
 */
static VkResult
pvr_load_op_data_create_and_upload(struct pvr_cmd_buffer *cmd_buffer,
                                   uint32_t idx,
                                   struct pvr_pds_upload *const pds_upload_out)
{
   pvr_dev_addr_t constants_addr;
   VkResult result;

   result =
      pvr_load_op_constants_create_and_upload(cmd_buffer, idx, &constants_addr);
   if (result != VK_SUCCESS)
      return result;

   return pvr_load_op_pds_data_create_and_upload(cmd_buffer,
                                                 idx,
                                                 constants_addr,
                                                 pds_upload_out);
}

static void pvr_pds_bgnd_pack_state(
   const struct pvr_load_op *load_op,
   const struct pvr_pds_upload *load_op_program,
   uint64_t pds_reg_values[static const ROGUE_NUM_CR_PDS_BGRND_WORDS])
{
   pvr_csb_pack (&pds_reg_values[0], CR_PDS_BGRND0_BASE, value) {
      value.shader_addr = PVR_DEV_ADDR(load_op->pds_frag_prog.data_offset);
      value.texunicode_addr =
         PVR_DEV_ADDR(load_op->pds_tex_state_prog.code_offset);
   }

   pvr_csb_pack (&pds_reg_values[1], CR_PDS_BGRND1_BASE, value) {
      value.texturedata_addr = PVR_DEV_ADDR(load_op_program->data_offset);
   }

   pvr_csb_pack (&pds_reg_values[2], CR_PDS_BGRND3_SIZEINFO, value) {
      value.usc_sharedsize =
         DIV_ROUND_UP(load_op->const_shareds_count,
                      PVRX(CR_PDS_BGRND3_SIZEINFO_USC_SHAREDSIZE_UNIT_SIZE));
      value.pds_texturestatesize = DIV_ROUND_UP(
         load_op_program->data_size,
         PVRX(CR_PDS_BGRND3_SIZEINFO_PDS_TEXTURESTATESIZE_UNIT_SIZE));
      value.pds_tempsize =
         DIV_ROUND_UP(load_op->temps_count,
                      PVRX(CR_PDS_BGRND3_SIZEINFO_PDS_TEMPSIZE_UNIT_SIZE));
   }
}

/**
 * \brief Calculates the stride in pixels based on the pitch in bytes and pixel
 * format.
 *
 * \param[in] pitch     Width pitch in bytes.
 * \param[in] vk_format Vulkan image format.
 * \return Stride in pixels.
 */
static inline uint32_t pvr_stride_from_pitch(uint32_t pitch, VkFormat vk_format)
{
   const unsigned int cpp = vk_format_get_blocksize(vk_format);

   assert(pitch % cpp == 0);

   return pitch / cpp;
}

static void pvr_setup_pbe_state(
   const struct pvr_device_info *dev_info,
   struct pvr_framebuffer *framebuffer,
   uint32_t mrt_index,
   const struct usc_mrt_resource *mrt_resource,
   const struct pvr_image_view *const iview,
   const VkRect2D *render_area,
   const bool down_scale,
   const uint32_t samples,
   uint32_t pbe_cs_words[static const ROGUE_NUM_PBESTATE_STATE_WORDS],
   uint64_t pbe_reg_words[static const ROGUE_NUM_PBESTATE_REG_WORDS])
{
   const struct pvr_image *image = vk_to_pvr_image(iview->vk.image);
   uint32_t level_pitch = image->mip_levels[iview->vk.base_mip_level].pitch;

   struct pvr_pbe_surf_params surface_params;
   struct pvr_pbe_render_params render_params;
   bool with_packed_usc_channel;
   const uint8_t *swizzle;
   uint32_t position;

   /* down_scale should be true when performing a resolve, in which case there
    * should be more than one sample.
    */
   assert((down_scale && samples > 1U) || (!down_scale && samples == 1U));

   /* Setup surface parameters. */

   if (PVR_HAS_FEATURE(dev_info, usc_f16sop_u8)) {
      switch (iview->vk.format) {
      case VK_FORMAT_B8G8R8A8_UNORM:
         with_packed_usc_channel = true;
         break;
      case VK_FORMAT_D32_SFLOAT:
         with_packed_usc_channel = false;
         break;
      default:
         unreachable("Unsupported Vulkan image format");
      }
   } else {
      with_packed_usc_channel = false;
   }

   swizzle = pvr_get_format_swizzle(iview->vk.format);
   memcpy(surface_params.swizzle, swizzle, sizeof(surface_params.swizzle));

   pvr_pbe_get_src_format_and_gamma(iview->vk.format,
                                    PVR_PBE_GAMMA_NONE,
                                    with_packed_usc_channel,
                                    &surface_params.source_format,
                                    &surface_params.gamma);

   surface_params.is_normalized = vk_format_is_normalized(iview->vk.format);
   surface_params.pbe_packmode = pvr_get_pbe_packmode(iview->vk.format);
   surface_params.nr_components = vk_format_get_nr_components(iview->vk.format);

   /* FIXME: Should we have an inline function to return the address of a mip
    * level?
    */
   surface_params.addr =
      PVR_DEV_ADDR_OFFSET(image->vma->dev_addr,
                          image->mip_levels[iview->vk.base_mip_level].offset);

   surface_params.mem_layout = image->memlayout;
   surface_params.stride = pvr_stride_from_pitch(level_pitch, iview->vk.format);
   surface_params.depth = iview->vk.extent.depth;
   surface_params.width = iview->vk.extent.width;
   surface_params.height = iview->vk.extent.height;
   surface_params.z_only_render = false;
   surface_params.down_scale = down_scale;
   surface_params.msaa_mode = samples;

   /* Setup render parameters. */

   if (mrt_resource->type == USC_MRT_RESOURCE_TYPE_MEMORY) {
      position = mrt_resource->u.mem.offset_in_dwords;
   } else {
      assert(mrt_resource->type == USC_MRT_RESOURCE_TYPE_OUTPUT_REGISTER);
      assert(mrt_resource->u.reg.offset == 0);

      position = mrt_resource->u.reg.out_reg;
   }

   assert(position <= 3 || PVR_HAS_FEATURE(dev_info, eight_output_registers));

   switch (position) {
   case 0:
   case 4:
      render_params.source_start = PVR_PBE_STARTPOS_BIT0;
      break;
   case 1:
   case 5:
      render_params.source_start = PVR_PBE_STARTPOS_BIT32;
      break;
   case 2:
   case 6:
      render_params.source_start = PVR_PBE_STARTPOS_BIT64;
      break;
   case 3:
   case 7:
      render_params.source_start = PVR_PBE_STARTPOS_BIT96;
      break;
   default:
      assert(!"Invalid output register");
      break;
   }

   render_params.min_x_clip = MAX2(0, render_area->offset.x);
   render_params.min_y_clip = MAX2(0, render_area->offset.y);
   render_params.max_x_clip =
      MIN2(framebuffer->width,
           render_area->offset.x + render_area->extent.width) -
      1;
   render_params.max_y_clip =
      MIN2(framebuffer->height,
           render_area->offset.y + render_area->extent.height) -
      1;

   render_params.slice = 0;
   render_params.mrt_index = mrt_index;

   pvr_pbe_pack_state(dev_info,
                      &surface_params,
                      &render_params,
                      pbe_cs_words,
                      pbe_reg_words);
}

static struct pvr_render_target *
pvr_get_render_target(const struct pvr_render_pass *pass,
                      const struct pvr_framebuffer *framebuffer,
                      uint32_t idx)
{
   const struct pvr_renderpass_hwsetup_render *hw_render =
      &pass->hw_setup->renders[idx];
   uint32_t rt_idx = 0;

   switch (hw_render->sample_count) {
   case 1:
   case 2:
   case 4:
   case 8:
      rt_idx = util_logbase2(hw_render->sample_count);
      break;

   default:
      unreachable("Unsupported sample count");
      break;
   }

   return &framebuffer->render_targets[rt_idx];
}

static uint32_t
pvr_pass_get_pixel_output_width(const struct pvr_render_pass *pass,
                                uint32_t idx,
                                const struct pvr_device_info *dev_info)
{
   const struct pvr_renderpass_hwsetup_render *hw_render =
      &pass->hw_setup->renders[idx];
   /* Default value based on the maximum value found in all existing cores. The
    * maximum is used as this is being treated as a lower bound, making it a
    * "safer" choice than the minimum value found in all existing cores.
    */
   const uint32_t min_output_regs =
      PVR_GET_FEATURE_VALUE(dev_info, usc_min_output_registers_per_pix, 2U);
   const uint32_t width = MAX2(hw_render->output_regs_count, min_output_regs);

   return util_next_power_of_two(width);
}

static VkResult pvr_sub_cmd_gfx_job_init(const struct pvr_device_info *dev_info,
                                         struct pvr_cmd_buffer *cmd_buffer,
                                         struct pvr_sub_cmd_gfx *sub_cmd)
{
   struct pvr_render_pass_info *render_pass_info =
      &cmd_buffer->state.render_pass_info;
   const struct pvr_renderpass_hwsetup_render *hw_render =
      &render_pass_info->pass->hw_setup->renders[sub_cmd->hw_render_idx];
   struct pvr_render_job *job = &sub_cmd->job;
   struct pvr_pds_upload pds_pixel_event_program;

   uint32_t pbe_cs_words[PVR_MAX_COLOR_ATTACHMENTS]
                        [ROGUE_NUM_PBESTATE_STATE_WORDS];
   struct pvr_render_target *render_target;
   VkResult result;

   assert(hw_render->eot_surface_count < ARRAY_SIZE(pbe_cs_words));

   for (uint32_t i = 0; i < hw_render->eot_surface_count; i++) {
      const struct pvr_renderpass_hwsetup_eot_surface *surface =
         &hw_render->eot_surfaces[i];
      const struct pvr_image_view *iview =
         render_pass_info->attachments[surface->attachment_index];
      const struct usc_mrt_resource *mrt_resource =
         &hw_render->eot_setup.mrt_resources[surface->mrt_index];
      uint32_t samples = 1;

      if (surface->need_resolve) {
         const struct pvr_image_view *resolve_src =
            render_pass_info->attachments[surface->src_attachment_index];

         /* Attachments that are the destination of resolve operations must be
          * loaded before their next use.
          */
         render_pass_info->enable_bg_tag = true;
         render_pass_info->process_empty_tiles = true;

         if (surface->resolve_type != PVR_RESOLVE_TYPE_PBE)
            continue;

         samples = (uint32_t)resolve_src->vk.image->samples;
      }

      pvr_setup_pbe_state(dev_info,
                          render_pass_info->framebuffer,
                          surface->mrt_index,
                          mrt_resource,
                          iview,
                          &render_pass_info->render_area,
                          surface->need_resolve,
                          samples,
                          pbe_cs_words[i],
                          job->pbe_reg_words[i]);
   }

   /* FIXME: The fragment program only supports a single surface at present. */
   assert(hw_render->eot_surface_count == 1);
   result = pvr_sub_cmd_gfx_per_job_fragment_programs_create_and_upload(
      cmd_buffer,
      pbe_cs_words[0],
      &pds_pixel_event_program);
   if (result != VK_SUCCESS)
      return result;

   job->pds_pixel_event_data_offset = pds_pixel_event_program.data_offset;

   /* FIXME: Don't do this if there is a barrier load. */
   if (render_pass_info->enable_bg_tag) {
      const struct pvr_load_op *load_op = hw_render->client_data;
      struct pvr_pds_upload load_op_program;

      /* FIXME: Should we free the PDS pixel event data or let it be freed
       * when the pool gets emptied?
       */
      result = pvr_load_op_data_create_and_upload(cmd_buffer,
                                                  sub_cmd->hw_render_idx,
                                                  &load_op_program);
      if (result != VK_SUCCESS)
         return result;

      pvr_pds_bgnd_pack_state(load_op,
                              &load_op_program,
                              job->pds_bgnd_reg_values);
   }

   job->enable_bg_tag = render_pass_info->enable_bg_tag;
   job->process_empty_tiles = render_pass_info->process_empty_tiles;

   render_target = pvr_get_render_target(render_pass_info->pass,
                                         render_pass_info->framebuffer,
                                         sub_cmd->hw_render_idx);
   job->rt_dataset = render_target->rt_dataset;

   job->ctrl_stream_addr = pvr_csb_get_start_address(&sub_cmd->control_stream);

   /* FIXME: Need to set up the border color table at device creation
    * time. Set to invalid for the time being.
    */
   job->border_colour_table_addr = PVR_DEV_ADDR_INVALID;

   if (sub_cmd->depth_bias_bo)
      job->depth_bias_table_addr = sub_cmd->depth_bias_bo->vma->dev_addr;
   else
      job->depth_bias_table_addr = PVR_DEV_ADDR_INVALID;

   if (sub_cmd->scissor_bo)
      job->scissor_table_addr = sub_cmd->scissor_bo->vma->dev_addr;
   else
      job->scissor_table_addr = PVR_DEV_ADDR_INVALID;

   job->pixel_output_width =
      pvr_pass_get_pixel_output_width(render_pass_info->pass,
                                      sub_cmd->hw_render_idx,
                                      dev_info);

   /* Setup depth/stencil job information. */
   if (hw_render->ds_surface_id != -1) {
      struct pvr_image_view *iview =
         render_pass_info->attachments[hw_render->ds_surface_id];
      const struct pvr_image *image = vk_to_pvr_image(iview->vk.image);

      if (vk_format_has_depth(image->vk.format)) {
         uint32_t level_pitch =
            image->mip_levels[iview->vk.base_mip_level].pitch;

         /* FIXME: Is this sufficient for depth buffers? */
         job->depth_addr = image->dev_addr;

         job->depth_stride =
            pvr_stride_from_pitch(level_pitch, iview->vk.format);
         job->depth_height = iview->vk.extent.height;
         job->depth_physical_width =
            u_minify(image->physical_extent.width, iview->vk.base_mip_level);
         job->depth_physical_height =
            u_minify(image->physical_extent.height, iview->vk.base_mip_level);
         job->depth_layer_size = image->layer_size;

         if (hw_render->ds_surface_id < render_pass_info->clear_value_count) {
            VkClearValue *clear_values =
               &render_pass_info->clear_values[hw_render->ds_surface_id];

            job->depth_clear_value = clear_values->depthStencil.depth;
         } else {
            job->depth_clear_value = 1.0f;
         }

         job->depth_vk_format = iview->vk.format;

         job->depth_memlayout = image->memlayout;
      } else {
         job->depth_addr = PVR_DEV_ADDR_INVALID;
         job->depth_stride = 0;
         job->depth_height = 0;
         job->depth_physical_width = 0;
         job->depth_physical_height = 0;
         job->depth_layer_size = 0;
         job->depth_clear_value = 1.0f;
         job->depth_vk_format = VK_FORMAT_UNDEFINED;
         job->depth_memlayout = PVR_MEMLAYOUT_LINEAR;
      }

      if (vk_format_has_stencil(image->vk.format)) {
         /* FIXME: Is this sufficient for stencil buffers? */
         job->stencil_addr = image->dev_addr;
      } else {
         job->stencil_addr = PVR_DEV_ADDR_INVALID;
      }
   } else {
      job->depth_addr = PVR_DEV_ADDR_INVALID;
      job->depth_stride = 0;
      job->depth_height = 0;
      job->depth_physical_width = 0;
      job->depth_physical_height = 0;
      job->depth_layer_size = 0;
      job->depth_clear_value = 1.0f;
      job->depth_vk_format = VK_FORMAT_UNDEFINED;
      job->depth_memlayout = PVR_MEMLAYOUT_LINEAR;

      job->stencil_addr = PVR_DEV_ADDR_INVALID;
   }

   if (hw_render->ds_surface_id != -1) {
      struct pvr_image_view *iview =
         render_pass_info->attachments[hw_render->ds_surface_id];
      const struct pvr_image *image = vk_to_pvr_image(iview->vk.image);

      /* If the HW render pass has a valid depth/stencil surface, determine the
       * sample count from the attachment's image.
       */
      job->samples = image->vk.samples;
   } else if (hw_render->output_regs_count) {
      /* If the HW render pass has output registers, we have color attachments
       * to write to, so determine the sample count from the count specified for
       * every color attachment in this render.
       */
      job->samples = hw_render->sample_count;
   } else if (cmd_buffer->state.gfx_pipeline) {
      /* If the HW render pass has no color or depth/stencil attachments, we
       * determine the sample count from the count given during pipeline
       * creation.
       */
      job->samples = cmd_buffer->state.gfx_pipeline->rasterization_samples;
   } else if (render_pass_info->pass->attachment_count > 0) {
      /* If we get here, we have a render pass with subpasses containing no
       * attachments. The next best thing is largest of the sample counts
       * specified by the render pass attachment descriptions.
       */
      job->samples = render_pass_info->pass->max_sample_count;
   } else {
      /* No appropriate framebuffer attachment is available. */
      mesa_logw("Defaulting render job sample count to 1.");
      job->samples = VK_SAMPLE_COUNT_1_BIT;
   }

   if (sub_cmd->max_tiles_in_flight ==
       PVR_GET_FEATURE_VALUE(dev_info, isp_max_tiles_in_flight, 1U)) {
      /* Use the default limit based on the partition store. */
      job->max_tiles_in_flight = 0U;
   } else {
      job->max_tiles_in_flight = sub_cmd->max_tiles_in_flight;
   }

   job->frag_uses_atomic_ops = sub_cmd->frag_uses_atomic_ops;
   job->disable_compute_overlap = false;
   job->max_shared_registers = cmd_buffer->state.max_shared_regs;
   job->run_frag = true;
   job->geometry_terminate = true;

   return VK_SUCCESS;
}

/* Number of shareds used in the Issue Data Fence(IDF)/Wait Data Fence(WDF)
 * kernel.
 */
#define PVR_IDF_WDF_IN_REGISTER_CONST_COUNT 12U

static void
pvr_sub_cmd_compute_job_init(const struct pvr_physical_device *pdevice,
                             struct pvr_cmd_buffer *cmd_buffer,
                             struct pvr_sub_cmd_compute *sub_cmd)
{
   const struct pvr_device_runtime_info *dev_runtime_info =
      &pdevice->dev_runtime_info;
   const struct pvr_device_info *dev_info = &pdevice->dev_info;

   if (sub_cmd->uses_barrier)
      sub_cmd->submit_info.flags |= PVR_WINSYS_COMPUTE_FLAG_PREVENT_ALL_OVERLAP;

   pvr_csb_pack (&sub_cmd->submit_info.regs.cdm_ctrl_stream_base,
                 CR_CDM_CTRL_STREAM_BASE,
                 value) {
      value.addr = pvr_csb_get_start_address(&sub_cmd->control_stream);
   }

   /* FIXME: Need to set up the border color table at device creation
    * time. Set to invalid for the time being.
    */
   pvr_csb_pack (&sub_cmd->submit_info.regs.tpu_border_colour_table,
                 CR_TPU_BORDER_COLOUR_TABLE_CDM,
                 value) {
      value.border_colour_table_address = PVR_DEV_ADDR_INVALID;
   }

   sub_cmd->num_shared_regs = MAX2(cmd_buffer->device->idfwdf_state.usc_shareds,
                                   cmd_buffer->state.max_shared_regs);

   cmd_buffer->state.max_shared_regs = 0U;

   if (PVR_HAS_FEATURE(dev_info, compute_morton_capable))
      sub_cmd->submit_info.regs.cdm_item = 0;

   pvr_csb_pack (&sub_cmd->submit_info.regs.tpu, CR_TPU, value) {
      value.tag_cem_4k_face_packing = true;
   }

   if (PVR_HAS_FEATURE(dev_info, cluster_grouping) &&
       PVR_HAS_FEATURE(dev_info, slc_mcu_cache_controls) &&
       dev_runtime_info->num_phantoms > 1 && sub_cmd->uses_atomic_ops) {
      /* Each phantom has its own MCU, so atomicity can only be guaranteed
       * when all work items are processed on the same phantom. This means we
       * need to disable all USCs other than those of the first phantom, which
       * has 4 clusters.
       */
      pvr_csb_pack (&sub_cmd->submit_info.regs.compute_cluster,
                    CR_COMPUTE_CLUSTER,
                    value) {
         value.mask = 0xFU;
      }
   } else {
      pvr_csb_pack (&sub_cmd->submit_info.regs.compute_cluster,
                    CR_COMPUTE_CLUSTER,
                    value) {
         value.mask = 0U;
      }
   }

   if (PVR_HAS_FEATURE(dev_info, gpu_multicore_support) &&
       sub_cmd->uses_atomic_ops) {
      sub_cmd->submit_info.flags |= PVR_WINSYS_COMPUTE_FLAG_SINGLE_CORE;
   }
}

#define PIXEL_ALLOCATION_SIZE_MAX_IN_BLOCKS \
   (1024 / PVRX(CDMCTRL_KERNEL0_USC_COMMON_SIZE_UNIT_SIZE))

static uint32_t
pvr_compute_flat_slot_size(const struct pvr_physical_device *pdevice,
                           uint32_t coeff_regs_count,
                           bool use_barrier,
                           uint32_t total_workitems)
{
   const struct pvr_device_runtime_info *dev_runtime_info =
      &pdevice->dev_runtime_info;
   const struct pvr_device_info *dev_info = &pdevice->dev_info;
   uint32_t max_workgroups_per_task = ROGUE_CDM_MAX_PACKED_WORKGROUPS_PER_TASK;
   uint32_t max_avail_coeff_regs =
      dev_runtime_info->cdm_max_local_mem_size_regs;
   uint32_t localstore_chunks_count =
      DIV_ROUND_UP(coeff_regs_count << 2,
                   PVRX(CDMCTRL_KERNEL0_USC_COMMON_SIZE_UNIT_SIZE));

   /* Ensure that we cannot have more workgroups in a slot than the available
    * number of coefficients allow us to have.
    */
   if (coeff_regs_count > 0U) {
      /* If TA or 3D can overlap with CDM, or if the TA is running a geometry
       * shader then we need to consider this in calculating max allowed
       * work-groups.
       */
      if (PVR_HAS_QUIRK(dev_info, 52354) &&
          (PVR_HAS_FEATURE(dev_info, compute_overlap) ||
           PVR_HAS_FEATURE(dev_info, gs_rta_support))) {
         /* Solve for n (number of work-groups per task). All values are in
          * size of common store alloc blocks:
          *
          * n + (2n + 7) * (local_memory_size_max - 1) =
          * 	(coefficient_memory_pool_size) - (7 * pixel_allocation_size_max)
          * ==>
          * n + 2n * (local_memory_size_max - 1) =
          * 	(coefficient_memory_pool_size) - (7 * pixel_allocation_size_max)
          * 	- (7 * (local_memory_size_max - 1))
          * ==>
          * n * (1 + 2 * (local_memory_size_max - 1)) =
          * 	(coefficient_memory_pool_size) - (7 * pixel_allocation_size_max)
          * 	- (7 * (local_memory_size_max - 1))
          * ==>
          * n = ((coefficient_memory_pool_size) -
          * 	(7 * pixel_allocation_size_max) -
          * 	(7 * (local_memory_size_max - 1)) / (1 +
          * 2 * (local_memory_size_max - 1)))
          */
         uint32_t max_common_store_blocks =
            DIV_ROUND_UP(max_avail_coeff_regs * 4U,
                         PVRX(CDMCTRL_KERNEL0_USC_COMMON_SIZE_UNIT_SIZE));

         /* (coefficient_memory_pool_size) - (7 * pixel_allocation_size_max)
          */
         max_common_store_blocks -= ROGUE_MAX_OVERLAPPED_PIXEL_TASK_INSTANCES *
                                    PIXEL_ALLOCATION_SIZE_MAX_IN_BLOCKS;

         /* - (7 * (local_memory_size_max - 1)) */
         max_common_store_blocks -= (ROGUE_MAX_OVERLAPPED_PIXEL_TASK_INSTANCES *
                                     (localstore_chunks_count - 1U));

         /* Divide by (1 + 2 * (local_memory_size_max - 1)) */
         max_workgroups_per_task = max_common_store_blocks /
                                   (1U + 2U * (localstore_chunks_count - 1U));

         max_workgroups_per_task =
            MIN2(max_workgroups_per_task,
                 ROGUE_CDM_MAX_PACKED_WORKGROUPS_PER_TASK);

      } else {
         max_workgroups_per_task =
            MIN2((max_avail_coeff_regs / coeff_regs_count),
                 max_workgroups_per_task);
      }
   }

   /* max_workgroups_per_task should at least be one. */
   assert(max_workgroups_per_task >= 1U);

   if (total_workitems >= ROGUE_MAX_INSTANCES_PER_TASK) {
      /* In this case, the work group size will have been padded up to the
       * next ROGUE_MAX_INSTANCES_PER_TASK so we just set max instances to be
       * ROGUE_MAX_INSTANCES_PER_TASK.
       */
      return ROGUE_MAX_INSTANCES_PER_TASK;
   }

   /* In this case, the number of instances in the slot must be clamped to
    * accommodate whole work-groups only.
    */
   if (PVR_HAS_QUIRK(dev_info, 49032) || use_barrier) {
      max_workgroups_per_task =
         MIN2(max_workgroups_per_task,
              ROGUE_MAX_INSTANCES_PER_TASK / total_workitems);
      return total_workitems * max_workgroups_per_task;
   }

   return MIN2(total_workitems * max_workgroups_per_task,
               ROGUE_MAX_INSTANCES_PER_TASK);
}

static void
pvr_compute_generate_control_stream(struct pvr_csb *csb,
                                    struct pvr_sub_cmd_compute *sub_cmd,
                                    const struct pvr_compute_kernel_info *info)
{
   /* Compute kernel 0. */
   pvr_csb_emit (csb, CDMCTRL_KERNEL0, kernel0) {
      kernel0.indirect_present = !!info->indirect_buffer_addr.addr;
      kernel0.global_offsets_present = info->global_offsets_present;
      kernel0.usc_common_size = info->usc_common_size;
      kernel0.usc_unified_size = info->usc_unified_size;
      kernel0.pds_temp_size = info->pds_temp_size;
      kernel0.pds_data_size = info->pds_data_size;
      kernel0.usc_target = info->usc_target;
      kernel0.fence = info->is_fence;
   }

   /* Compute kernel 1. */
   pvr_csb_emit (csb, CDMCTRL_KERNEL1, kernel1) {
      kernel1.data_addr = PVR_DEV_ADDR(info->pds_data_offset);
      kernel1.sd_type = info->sd_type;
      kernel1.usc_common_shared = info->usc_common_shared;
   }

   /* Compute kernel 2. */
   pvr_csb_emit (csb, CDMCTRL_KERNEL2, kernel2) {
      kernel2.code_addr = PVR_DEV_ADDR(info->pds_code_offset);
   }

   if (info->indirect_buffer_addr.addr) {
      /* Compute kernel 6. */
      pvr_csb_emit (csb, CDMCTRL_KERNEL6, kernel6) {
         kernel6.indirect_addrmsb = info->indirect_buffer_addr;
      }

      /* Compute kernel 7. */
      pvr_csb_emit (csb, CDMCTRL_KERNEL7, kernel7) {
         kernel7.indirect_addrlsb = info->indirect_buffer_addr;
      }
   } else {
      /* Compute kernel 3. */
      pvr_csb_emit (csb, CDMCTRL_KERNEL3, kernel3) {
         assert(info->global_size[0U] > 0U);
         kernel3.workgroup_x = info->global_size[0U] - 1U;
      }

      /* Compute kernel 4. */
      pvr_csb_emit (csb, CDMCTRL_KERNEL4, kernel4) {
         assert(info->global_size[1U] > 0U);
         kernel4.workgroup_y = info->global_size[1U] - 1U;
      }

      /* Compute kernel 5. */
      pvr_csb_emit (csb, CDMCTRL_KERNEL5, kernel5) {
         assert(info->global_size[2U] > 0U);
         kernel5.workgroup_z = info->global_size[2U] - 1U;
      }
   }

   /* Compute kernel 8. */
   pvr_csb_emit (csb, CDMCTRL_KERNEL8, kernel8) {
      if (info->max_instances == ROGUE_MAX_INSTANCES_PER_TASK)
         kernel8.max_instances = 0U;
      else
         kernel8.max_instances = info->max_instances;

      assert(info->local_size[0U] > 0U);
      kernel8.workgroup_size_x = info->local_size[0U] - 1U;
      assert(info->local_size[1U] > 0U);
      kernel8.workgroup_size_y = info->local_size[1U] - 1U;
      assert(info->local_size[2U] > 0U);
      kernel8.workgroup_size_z = info->local_size[2U] - 1U;
   }

   /* Track the highest amount of shared registers usage in this dispatch.
    * This is used by the FW for context switching, so must be large enough
    * to contain all the shared registers that might be in use for this compute
    * job. Coefficients don't need to be included as the context switch will not
    * happen within the execution of a single workgroup, thus nothing needs to
    * be preserved.
    */
   if (info->usc_common_shared) {
      sub_cmd->num_shared_regs =
         MAX2(sub_cmd->num_shared_regs, info->usc_common_size);
   }
}

/* TODO: This can be pre-packed and uploaded directly. Would that provide any
 * speed up?
 */
static void
pvr_compute_generate_idfwdf(struct pvr_cmd_buffer *cmd_buffer,
                            struct pvr_sub_cmd_compute *const sub_cmd)
{
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   bool *const is_sw_barier_required =
      &state->current_sub_cmd->compute.pds_sw_barrier_requires_clearing;
   const struct pvr_physical_device *pdevice = cmd_buffer->device->pdevice;
   struct pvr_csb *csb = &sub_cmd->control_stream;
   const struct pvr_pds_upload *program;

   if (PVR_NEED_SW_COMPUTE_PDS_BARRIER(&pdevice->dev_info) &&
       *is_sw_barier_required) {
      *is_sw_barier_required = false;
      program = &cmd_buffer->device->idfwdf_state.sw_compute_barrier_pds;
   } else {
      program = &cmd_buffer->device->idfwdf_state.pds;
   }

   struct pvr_compute_kernel_info info = {
      .indirect_buffer_addr = PVR_DEV_ADDR_INVALID,
      .global_offsets_present = false,
      .usc_common_size =
         DIV_ROUND_UP(cmd_buffer->device->idfwdf_state.usc_shareds << 2,
                      PVRX(CDMCTRL_KERNEL0_USC_COMMON_SIZE_UNIT_SIZE)),
      .usc_unified_size = 0U,
      .pds_temp_size = 0U,
      .pds_data_size =
         DIV_ROUND_UP(program->data_size << 2,
                      PVRX(CDMCTRL_KERNEL0_PDS_DATA_SIZE_UNIT_SIZE)),
      .usc_target = PVRX(CDMCTRL_USC_TARGET_ALL),
      .is_fence = false,
      .pds_data_offset = program->data_offset,
      .sd_type = PVRX(CDMCTRL_SD_TYPE_USC),
      .usc_common_shared = true,
      .pds_code_offset = program->code_offset,
      .global_size = { 1U, 1U, 1U },
      .local_size = { 1U, 1U, 1U },
   };

   /* We don't need to pad work-group size for this case. */

   info.max_instances =
      pvr_compute_flat_slot_size(pdevice,
                                 cmd_buffer->device->idfwdf_state.usc_shareds,
                                 false,
                                 1U);

   pvr_compute_generate_control_stream(csb, sub_cmd, &info);
}

static void
pvr_compute_generate_fence(struct pvr_cmd_buffer *cmd_buffer,
                           struct pvr_sub_cmd_compute *const sub_cmd,
                           bool deallocate_shareds)
{
   const struct pvr_pds_upload *program =
      &cmd_buffer->device->pds_compute_fence_program;
   const struct pvr_physical_device *pdevice = cmd_buffer->device->pdevice;
   struct pvr_csb *csb = &sub_cmd->control_stream;

   struct pvr_compute_kernel_info info = {
      .indirect_buffer_addr = PVR_DEV_ADDR_INVALID,
      .global_offsets_present = false,
      .usc_common_size = 0U,
      .usc_unified_size = 0U,
      .pds_temp_size = 0U,
      .pds_data_size =
         DIV_ROUND_UP(program->data_size << 2,
                      PVRX(CDMCTRL_KERNEL0_PDS_DATA_SIZE_UNIT_SIZE)),
      .usc_target = PVRX(CDMCTRL_USC_TARGET_ANY),
      .is_fence = true,
      .pds_data_offset = program->data_offset,
      .sd_type = PVRX(CDMCTRL_SD_TYPE_PDS),
      .usc_common_shared = deallocate_shareds,
      .pds_code_offset = program->code_offset,
      .global_size = { 1U, 1U, 1U },
      .local_size = { 1U, 1U, 1U },
   };

   /* We don't need to pad work-group size for this case. */
   /* Here we calculate the slot size. This can depend on the use of barriers,
    * local memory, BRN's or other factors.
    */
   info.max_instances = pvr_compute_flat_slot_size(pdevice, 0U, false, 1U);

   pvr_compute_generate_control_stream(csb, sub_cmd, &info);
}

static VkResult pvr_cmd_buffer_end_sub_cmd(struct pvr_cmd_buffer *cmd_buffer)
{
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   struct pvr_sub_cmd *sub_cmd = state->current_sub_cmd;
   struct pvr_device *device = cmd_buffer->device;
   VkResult result;

   /* FIXME: Is this NULL check required because this function is called from
    * pvr_resolve_unemitted_resolve_attachments()? See comment about this
    * function being called twice in a row in pvr_CmdEndRenderPass().
    */
   if (!sub_cmd)
      return VK_SUCCESS;

   switch (sub_cmd->type) {
   case PVR_SUB_CMD_TYPE_GRAPHICS: {
      struct pvr_sub_cmd_gfx *const gfx_sub_cmd = &sub_cmd->gfx;

      if (cmd_buffer->vk.level == VK_COMMAND_BUFFER_LEVEL_SECONDARY) {
         result = pvr_csb_emit_return(&gfx_sub_cmd->control_stream);
         if (result != VK_SUCCESS) {
            state->status = result;
            return result;
         }

         break;
      }

      /* TODO: Check if the sub_cmd can be skipped based on
       * sub_cmd->gfx.empty_cmd flag.
       */

      result = pvr_cmd_buffer_upload_tables(device, cmd_buffer, gfx_sub_cmd);
      if (result != VK_SUCCESS) {
         state->status = result;
         return result;
      }

      result = pvr_cmd_buffer_emit_ppp_state(cmd_buffer, gfx_sub_cmd);
      if (result != VK_SUCCESS) {
         state->status = result;
         return result;
      }

      result = pvr_csb_emit_terminate(&gfx_sub_cmd->control_stream);
      if (result != VK_SUCCESS) {
         state->status = result;
         return result;
      }

      result = pvr_sub_cmd_gfx_job_init(&device->pdevice->dev_info,
                                        cmd_buffer,
                                        gfx_sub_cmd);
      if (result != VK_SUCCESS) {
         state->status = result;
         return result;
      }

      break;
   }

   case PVR_SUB_CMD_TYPE_COMPUTE: {
      struct pvr_sub_cmd_compute *const compute_sub_cmd = &sub_cmd->compute;

      pvr_compute_generate_fence(cmd_buffer, compute_sub_cmd, true);

      result = pvr_csb_emit_terminate(&compute_sub_cmd->control_stream);
      if (result != VK_SUCCESS) {
         state->status = result;
         return result;
      }

      pvr_sub_cmd_compute_job_init(device->pdevice,
                                   cmd_buffer,
                                   compute_sub_cmd);
      break;
   }

   case PVR_SUB_CMD_TYPE_TRANSFER:
      break;

   case PVR_SUB_CMD_TYPE_EVENT:
      break;

   default:
      pvr_finishme("Unsupported sub-command type %d", sub_cmd->type);
      break;
   }

   state->current_sub_cmd = NULL;

   return VK_SUCCESS;
}

static void pvr_reset_graphics_dirty_state(struct pvr_cmd_buffer_state *state,
                                           bool start_geom)
{
   if (start_geom) {
      /*
       * Initial geometry phase State.
       * It's the driver's responsibility to ensure that the state of the
       * hardware is correctly initialized at the start of every geometry
       * phase. This is required to prevent stale state from a previous
       * geometry phase erroneously affecting the next geometry phase. The
       * following fields in PPP State Header, and their corresponding state
       * words, must be supplied in the first PPP State Update of a geometry
       * phase that contains any geometry (draw calls). Any field not listed
       * below is safe to ignore.
       *
       *	TA_PRES_STREAM_OUT_SIZE
       *	TA_PRES_PPPCTRL
       *	TA_PRES_VARYING_WORD2
       *	TA_PRES_VARYING_WORD1
       *	TA_PRES_VARYING_WORD0
       *	TA_PRES_OUTSELECTS
       *	TA_PRES_WCLAMP
       *	TA_VIEWPORT_COUNT
       *	TA_PRES_VIEWPORT
       *	TA_PRES_REGION_CLIP
       *	TA_PRES_PDSSTATEPTR0
       *	TA_PRES_ISPCTLFB
       *	TA_PRES_ISPCTLFA
       *	TA_PRES_ISPCTL
       *
       * If a geometry phase does not contain any geometry, this restriction
       * can be ignored. If the first draw call in a geometry phase will only
       * update the depth or stencil buffers i.e. ISP_TAGWRITEDISABLE is set
       * in the ISP State Control Word, the PDS State Pointers
       * (TA_PRES_PDSSTATEPTR*) in the first PPP State Update do not need to
       * be supplied, since they will never reach the PDS in the fragment
       * phase.
       */

      state->emit_state_bits = 0;

      state->emit_state.stream_out = true;
      state->emit_state.ppp_control = true;
      state->emit_state.varying_word2 = true;
      state->emit_state.varying_word1 = true;
      state->emit_state.varying_word0 = true;
      state->emit_state.output_selects = true;
      state->emit_state.wclamp = true;
      state->emit_state.viewport = true;
      state->emit_state.region_clip = true;
      state->emit_state.pds_fragment_stateptr0 = true;
      state->emit_state.isp_fb = true;
      state->emit_state.isp = true;
   } else {
      state->emit_state.ppp_control = true;
      state->emit_state.varying_word1 = true;
      state->emit_state.varying_word0 = true;
      state->emit_state.output_selects = true;
      state->emit_state.viewport = true;
      state->emit_state.region_clip = true;
      state->emit_state.pds_fragment_stateptr0 = true;
      state->emit_state.isp_fb = true;
      state->emit_state.isp = true;
   }

   memset(&state->ppp_state, 0U, sizeof(state->ppp_state));

   state->dirty.vertex_bindings = true;
   state->dirty.gfx_pipeline_binding = true;
   state->dirty.viewport = true;
}

static VkResult pvr_cmd_buffer_start_sub_cmd(struct pvr_cmd_buffer *cmd_buffer,
                                             enum pvr_sub_cmd_type type)
{
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   struct pvr_device *device = cmd_buffer->device;
   struct pvr_sub_cmd *sub_cmd;
   VkResult result;

   /* Check the current status of the buffer. */
   if (state->status != VK_SUCCESS)
      return state->status;

   pvr_cmd_buffer_update_barriers(cmd_buffer, type);

   if (state->current_sub_cmd) {
      if (state->current_sub_cmd->type == type) {
         /* Continue adding to the current sub command. */
         return VK_SUCCESS;
      }

      /* End the current sub command. */
      result = pvr_cmd_buffer_end_sub_cmd(cmd_buffer);
      if (result != VK_SUCCESS)
         return result;
   }

   sub_cmd = vk_zalloc(&cmd_buffer->vk.pool->alloc,
                       sizeof(*sub_cmd),
                       8,
                       VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!sub_cmd) {
      state->status = vk_error(cmd_buffer, VK_ERROR_OUT_OF_HOST_MEMORY);
      return state->status;
   }

   sub_cmd->type = type;

   switch (type) {
   case PVR_SUB_CMD_TYPE_GRAPHICS:

      sub_cmd->gfx.depth_usage = PVR_DEPTH_STENCIL_USAGE_UNDEFINED;
      sub_cmd->gfx.stencil_usage = PVR_DEPTH_STENCIL_USAGE_UNDEFINED;
      sub_cmd->gfx.modifies_depth = false;
      sub_cmd->gfx.modifies_stencil = false;
      sub_cmd->gfx.max_tiles_in_flight =
         PVR_GET_FEATURE_VALUE(&device->pdevice->dev_info,
                               isp_max_tiles_in_flight,
                               1);
      sub_cmd->gfx.hw_render_idx = state->render_pass_info.current_hw_subpass;
      sub_cmd->gfx.framebuffer = state->render_pass_info.framebuffer;
      sub_cmd->gfx.empty_cmd = true;

      pvr_reset_graphics_dirty_state(state, true);
      pvr_csb_init(device,
                   PVR_CMD_STREAM_TYPE_GRAPHICS,
                   &sub_cmd->gfx.control_stream);
      break;

   case PVR_SUB_CMD_TYPE_COMPUTE:
      pvr_csb_init(device,
                   PVR_CMD_STREAM_TYPE_COMPUTE,
                   &sub_cmd->compute.control_stream);
      break;

   case PVR_SUB_CMD_TYPE_TRANSFER:
      list_inithead(&sub_cmd->transfer.transfer_cmds);
      break;

   case PVR_SUB_CMD_TYPE_EVENT:
      /* TODO: Add support for joining consecutive event sub_cmd? */
      break;

   default:
      pvr_finishme("Unsupported sub-command type %d", type);
      break;
   }

   list_addtail(&sub_cmd->link, &cmd_buffer->sub_cmds);
   state->current_sub_cmd = sub_cmd;

   return VK_SUCCESS;
}

VkResult pvr_cmd_buffer_alloc_mem(struct pvr_cmd_buffer *cmd_buffer,
                                  struct pvr_winsys_heap *heap,
                                  uint64_t size,
                                  uint32_t flags,
                                  struct pvr_bo **const pvr_bo_out)
{
   const uint32_t cache_line_size =
      rogue_get_slc_cache_line_size(&cmd_buffer->device->pdevice->dev_info);
   struct pvr_bo *pvr_bo;
   VkResult result;

   result = pvr_bo_alloc(cmd_buffer->device,
                         heap,
                         size,
                         cache_line_size,
                         flags,
                         &pvr_bo);
   if (result != VK_SUCCESS) {
      cmd_buffer->state.status = result;
      return result;
   }

   list_add(&pvr_bo->link, &cmd_buffer->bo_list);

   *pvr_bo_out = pvr_bo;

   return VK_SUCCESS;
}

static void pvr_cmd_bind_compute_pipeline(
   const struct pvr_compute_pipeline *const compute_pipeline,
   struct pvr_cmd_buffer *const cmd_buffer)
{
   cmd_buffer->state.compute_pipeline = compute_pipeline;
   cmd_buffer->state.dirty.compute_pipeline_binding = true;
}

static void pvr_cmd_bind_graphics_pipeline(
   const struct pvr_graphics_pipeline *const gfx_pipeline,
   struct pvr_cmd_buffer *const cmd_buffer)
{
   struct pvr_dynamic_state *const dest_state =
      &cmd_buffer->state.dynamic.common;
   const struct pvr_dynamic_state *const src_state =
      &gfx_pipeline->dynamic_state;
   struct pvr_cmd_buffer_state *const cmd_buffer_state = &cmd_buffer->state;
   const uint32_t state_mask = src_state->mask;

   cmd_buffer_state->gfx_pipeline = gfx_pipeline;
   cmd_buffer_state->dirty.gfx_pipeline_binding = true;

   /* FIXME: Handle PVR_DYNAMIC_STATE_BIT_VIEWPORT. */
   if (!(state_mask & PVR_DYNAMIC_STATE_BIT_VIEWPORT)) {
      assert(!"Unimplemented");
   }

   /* FIXME: Handle PVR_DYNAMIC_STATE_BIT_SCISSOR. */
   if (!(state_mask & PVR_DYNAMIC_STATE_BIT_SCISSOR)) {
      assert(!"Unimplemented");
   }

   if (!(state_mask & PVR_DYNAMIC_STATE_BIT_LINE_WIDTH)) {
      dest_state->line_width = src_state->line_width;

      cmd_buffer_state->dirty.line_width = true;
   }

   if (!(state_mask & PVR_DYNAMIC_STATE_BIT_DEPTH_BIAS)) {
      memcpy(&dest_state->depth_bias,
             &src_state->depth_bias,
             sizeof(src_state->depth_bias));

      cmd_buffer_state->dirty.depth_bias = true;
   }

   if (!(state_mask & PVR_DYNAMIC_STATE_BIT_BLEND_CONSTANTS)) {
      STATIC_ASSERT(
         __same_type(dest_state->blend_constants, src_state->blend_constants));

      typed_memcpy(dest_state->blend_constants,
                   src_state->blend_constants,
                   ARRAY_SIZE(dest_state->blend_constants));

      cmd_buffer_state->dirty.blend_constants = true;
   }

   if (!(state_mask & PVR_DYNAMIC_STATE_BIT_STENCIL_COMPARE_MASK)) {
      dest_state->compare_mask.front = src_state->compare_mask.front;
      dest_state->compare_mask.back = src_state->compare_mask.back;

      cmd_buffer_state->dirty.compare_mask = true;
   }

   if (!(state_mask & PVR_DYNAMIC_STATE_BIT_STENCIL_WRITE_MASK)) {
      dest_state->write_mask.front = src_state->write_mask.front;
      dest_state->write_mask.back = src_state->write_mask.back;

      cmd_buffer_state->dirty.write_mask = true;
   }

   if (!(state_mask & PVR_DYNAMIC_STATE_BIT_STENCIL_REFERENCE)) {
      dest_state->reference.front = src_state->reference.front;
      dest_state->reference.back = src_state->reference.back;

      cmd_buffer_state->dirty.reference = true;
   }
}

void pvr_CmdBindPipeline(VkCommandBuffer commandBuffer,
                         VkPipelineBindPoint pipelineBindPoint,
                         VkPipeline _pipeline)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   PVR_FROM_HANDLE(pvr_pipeline, pipeline, _pipeline);

   switch (pipelineBindPoint) {
   case VK_PIPELINE_BIND_POINT_COMPUTE:
      pvr_cmd_bind_compute_pipeline(to_pvr_compute_pipeline(pipeline),
                                    cmd_buffer);
      break;

   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      pvr_cmd_bind_graphics_pipeline(to_pvr_graphics_pipeline(pipeline),
                                     cmd_buffer);
      break;

   default:
      unreachable("Invalid bind point.");
      break;
   }
}

#if defined(DEBUG)
static void check_viewport_quirk_70165(const struct pvr_device *device,
                                       const VkViewport *pViewport)
{
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   float min_vertex_x, max_vertex_x, min_vertex_y, max_vertex_y;
   float min_screen_space_value, max_screen_space_value;
   float sign_to_unsigned_offset, fixed_point_max;
   float guardband_width, guardband_height;

   if (PVR_HAS_FEATURE(dev_info, simple_internal_parameter_format)) {
      /* Max representable value in 13.4 fixed point format.
       * Round-down to avoid precision issues.
       * Calculated as (2 ** 13) - 2*(2 ** -4)
       */
      fixed_point_max = 8192.0f - 2.0f / 16.0f;

      if (PVR_HAS_FEATURE(dev_info, screen_size8K)) {
         if (pViewport->width <= 4096 && pViewport->height <= 4096) {
            guardband_width = pViewport->width / 4.0f;
            guardband_height = pViewport->height / 4.0f;

            /* 2k of the range is negative */
            sign_to_unsigned_offset = 2048.0f;
         } else {
            guardband_width = 0.0f;
            guardband_height = 0.0f;

            /* For > 4k renders, the entire range is positive */
            sign_to_unsigned_offset = 0.0f;
         }
      } else {
         guardband_width = pViewport->width / 4.0f;
         guardband_height = pViewport->height / 4.0f;

         /* 2k of the range is negative */
         sign_to_unsigned_offset = 2048.0f;
      }
   } else {
      /* Max representable value in 16.8 fixed point format
       * Calculated as (2 ** 16) - (2 ** -8)
       */
      fixed_point_max = 65535.99609375f;
      guardband_width = pViewport->width / 4.0f;
      guardband_height = pViewport->height / 4.0f;

      /* 4k/20k of the range is negative */
      sign_to_unsigned_offset = (float)PVR_MAX_NEG_OFFSCREEN_OFFSET;
   }

   min_screen_space_value = -sign_to_unsigned_offset;
   max_screen_space_value = fixed_point_max - sign_to_unsigned_offset;

   min_vertex_x = pViewport->x - guardband_width;
   max_vertex_x = pViewport->x + pViewport->width + guardband_width;
   min_vertex_y = pViewport->y - guardband_height;
   max_vertex_y = pViewport->y + pViewport->height + guardband_height;
   if (min_vertex_x < min_screen_space_value ||
       max_vertex_x > max_screen_space_value ||
       min_vertex_y < min_screen_space_value ||
       max_vertex_y > max_screen_space_value) {
      mesa_logw("Viewport is affected by BRN70165, geometry outside "
                "the viewport could be corrupted");
   }
}
#endif

void pvr_CmdSetViewport(VkCommandBuffer commandBuffer,
                        uint32_t firstViewport,
                        uint32_t viewportCount,
                        const VkViewport *pViewports)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   const uint32_t total_count = firstViewport + viewportCount;
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;

   assert(firstViewport < PVR_MAX_VIEWPORTS && viewportCount > 0);
   assert(total_count >= 1 && total_count <= PVR_MAX_VIEWPORTS);

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

#if defined(DEBUG)
   if (PVR_HAS_QUIRK(&cmd_buffer->device->pdevice->dev_info, 70165)) {
      for (uint32_t viewport = 0; viewport < viewportCount; viewport++) {
         check_viewport_quirk_70165(cmd_buffer->device, &pViewports[viewport]);
      }
   }
#endif

   if (state->dynamic.common.viewport.count < total_count)
      state->dynamic.common.viewport.count = total_count;

   memcpy(&state->dynamic.common.viewport.viewports[firstViewport],
          pViewports,
          viewportCount * sizeof(*pViewports));

   state->dirty.viewport = true;
}

void pvr_CmdSetScissor(VkCommandBuffer commandBuffer,
                       uint32_t firstScissor,
                       uint32_t scissorCount,
                       const VkRect2D *pScissors)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   const uint32_t total_count = firstScissor + scissorCount;
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;

   assert(firstScissor < PVR_MAX_VIEWPORTS && scissorCount > 0);
   assert(total_count >= 1 && total_count <= PVR_MAX_VIEWPORTS);

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   if (state->dynamic.common.scissor.count < total_count)
      state->dynamic.common.scissor.count = total_count;

   memcpy(&state->dynamic.common.scissor.scissors[firstScissor],
          pScissors,
          scissorCount * sizeof(*pScissors));

   state->dirty.scissor = true;
}

void pvr_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;

   state->dynamic.common.line_width = lineWidth;
   state->dirty.line_width = true;
}

void pvr_CmdSetDepthBias(VkCommandBuffer commandBuffer,
                         float depthBiasConstantFactor,
                         float depthBiasClamp,
                         float depthBiasSlopeFactor)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;

   state->dynamic.common.depth_bias.constant_factor = depthBiasConstantFactor;
   state->dynamic.common.depth_bias.slope_factor = depthBiasSlopeFactor;
   state->dynamic.common.depth_bias.clamp = depthBiasClamp;
   state->dirty.depth_bias = true;
}

void pvr_CmdSetBlendConstants(VkCommandBuffer commandBuffer,
                              const float blendConstants[4])
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;

   STATIC_ASSERT(ARRAY_SIZE(state->dynamic.common.blend_constants) == 4);
   memcpy(state->dynamic.common.blend_constants,
          blendConstants,
          sizeof(state->dynamic.common.blend_constants));

   state->dirty.blend_constants = true;
}

void pvr_CmdSetDepthBounds(VkCommandBuffer commandBuffer,
                           float minDepthBounds,
                           float maxDepthBounds)
{
   mesa_logd("No support for depth bounds testing.");
}

void pvr_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                                  VkStencilFaceFlags faceMask,
                                  uint32_t compareMask)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      state->dynamic.common.compare_mask.front = compareMask;

   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      state->dynamic.common.compare_mask.back = compareMask;

   state->dirty.compare_mask = true;
}

void pvr_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                                VkStencilFaceFlags faceMask,
                                uint32_t writeMask)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      state->dynamic.common.write_mask.front = writeMask;

   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      state->dynamic.common.write_mask.back = writeMask;

   state->dirty.write_mask = true;
}

void pvr_CmdSetStencilReference(VkCommandBuffer commandBuffer,
                                VkStencilFaceFlags faceMask,
                                uint32_t reference)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      state->dynamic.common.reference.front = reference;

   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      state->dynamic.common.reference.back = reference;

   state->dirty.reference = true;
}

void pvr_CmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                               VkPipelineBindPoint pipelineBindPoint,
                               VkPipelineLayout _layout,
                               uint32_t firstSet,
                               uint32_t descriptorSetCount,
                               const VkDescriptorSet *pDescriptorSets,
                               uint32_t dynamicOffsetCount,
                               const uint32_t *pDynamicOffsets)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_descriptor_state *descriptor_state;

   assert(firstSet + descriptorSetCount <= PVR_MAX_DESCRIPTOR_SETS);

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   switch (pipelineBindPoint) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
   case VK_PIPELINE_BIND_POINT_COMPUTE:
      break;

   default:
      unreachable("Unsupported bind point.");
      break;
   }

   if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      descriptor_state = &cmd_buffer->state.gfx_desc_state;
      cmd_buffer->state.dirty.gfx_desc_dirty = true;
   } else {
      descriptor_state = &cmd_buffer->state.compute_desc_state;
      cmd_buffer->state.dirty.compute_desc_dirty = true;
   }

   for (uint32_t i = 0; i < descriptorSetCount; i++) {
      PVR_FROM_HANDLE(pvr_descriptor_set, set, pDescriptorSets[i]);
      uint32_t index = firstSet + i;

      if (descriptor_state->descriptor_sets[index] != set) {
         descriptor_state->descriptor_sets[index] = set;
         descriptor_state->valid_mask |= (1u << index);
      }
   }
}

void pvr_CmdBindVertexBuffers(VkCommandBuffer commandBuffer,
                              uint32_t firstBinding,
                              uint32_t bindingCount,
                              const VkBuffer *pBuffers,
                              const VkDeviceSize *pOffsets)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_vertex_binding *const vb = cmd_buffer->state.vertex_bindings;

   /* We have to defer setting up vertex buffer since we need the buffer
    * stride from the pipeline.
    */

   assert(firstBinding < PVR_MAX_VERTEX_INPUT_BINDINGS &&
          bindingCount <= PVR_MAX_VERTEX_INPUT_BINDINGS);

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   for (uint32_t i = 0; i < bindingCount; i++) {
      vb[firstBinding + i].buffer = pvr_buffer_from_handle(pBuffers[i]);
      vb[firstBinding + i].offset = pOffsets[i];
   }

   cmd_buffer->state.dirty.vertex_bindings = true;
}

void pvr_CmdBindIndexBuffer(VkCommandBuffer commandBuffer,
                            VkBuffer buffer,
                            VkDeviceSize offset,
                            VkIndexType indexType)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   PVR_FROM_HANDLE(pvr_buffer, index_buffer, buffer);
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;

   assert(offset < index_buffer->vk.size);
   assert(indexType == VK_INDEX_TYPE_UINT32 ||
          indexType == VK_INDEX_TYPE_UINT16);

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   state->index_buffer_binding.buffer = index_buffer;
   state->index_buffer_binding.offset = offset;
   state->index_buffer_binding.type = indexType;
   state->dirty.index_buffer_binding = true;
}

void pvr_CmdPushConstants(VkCommandBuffer commandBuffer,
                          VkPipelineLayout layout,
                          VkShaderStageFlags stageFlags,
                          uint32_t offset,
                          uint32_t size,
                          const void *pValues)
{
#if defined(DEBUG)
   const uint64_t ending = (uint64_t)offset + (uint64_t)size;
#endif

   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   pvr_assert(ending <= PVR_MAX_PUSH_CONSTANTS_SIZE);

   memcpy(&state->push_constants.data[offset], pValues, size);

   state->push_constants.dirty_stages |= stageFlags;
}

static VkResult
pvr_cmd_buffer_setup_attachments(struct pvr_cmd_buffer *cmd_buffer,
                                 const struct pvr_render_pass *pass,
                                 const struct pvr_framebuffer *framebuffer)
{
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   struct pvr_render_pass_info *info = &state->render_pass_info;

   assert(pass->attachment_count == framebuffer->attachment_count);

   /* Free any previously allocated attachments. */
   vk_free(&cmd_buffer->vk.pool->alloc, state->render_pass_info.attachments);

   if (pass->attachment_count == 0) {
      info->attachments = NULL;
      return VK_SUCCESS;
   }

   info->attachments =
      vk_zalloc(&cmd_buffer->vk.pool->alloc,
                pass->attachment_count * sizeof(*info->attachments),
                8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!info->attachments) {
      /* Propagate VK_ERROR_OUT_OF_HOST_MEMORY to vkEndCommandBuffer */
      state->status = vk_error(cmd_buffer, VK_ERROR_OUT_OF_HOST_MEMORY);
      return state->status;
   }

   if (framebuffer) {
      for (uint32_t i = 0; i < pass->attachment_count; i++)
         info->attachments[i] = framebuffer->attachments[i];
   }

   return VK_SUCCESS;
}

static VkResult pvr_init_render_targets(struct pvr_device *device,
                                        struct pvr_render_pass *pass,
                                        struct pvr_framebuffer *framebuffer)
{
   for (uint32_t i = 0; i < pass->hw_setup->render_count; i++) {
      struct pvr_render_target *render_target =
         pvr_get_render_target(pass, framebuffer, i);

      pthread_mutex_lock(&render_target->mutex);

      if (!render_target->valid) {
         const struct pvr_renderpass_hwsetup_render *hw_render =
            &pass->hw_setup->renders[i];
         VkResult result;

         result = pvr_render_target_dataset_create(device,
                                                   framebuffer->width,
                                                   framebuffer->height,
                                                   hw_render->sample_count,
                                                   framebuffer->layers,
                                                   &render_target->rt_dataset);
         if (result != VK_SUCCESS) {
            pthread_mutex_unlock(&render_target->mutex);
            return result;
         }

         render_target->valid = true;
      }

      pthread_mutex_unlock(&render_target->mutex);
   }

   return VK_SUCCESS;
}

static const struct pvr_renderpass_hwsetup_subpass *
pvr_get_hw_subpass(const struct pvr_render_pass *pass, const uint32_t subpass)
{
   const struct pvr_renderpass_hw_map *map =
      &pass->hw_setup->subpass_map[subpass];

   return &pass->hw_setup->renders[map->render].subpasses[map->subpass];
}

static void pvr_perform_start_of_render_attachment_clear(
   struct pvr_cmd_buffer *cmd_buffer,
   const struct pvr_framebuffer *framebuffer,
   uint32_t index,
   bool is_depth_stencil,
   uint32_t *index_list_clear_mask)
{
   struct pvr_render_pass_info *info = &cmd_buffer->state.render_pass_info;
   const struct pvr_render_pass *pass = info->pass;
   const struct pvr_renderpass_hwsetup_render *hw_render;
   const struct pvr_renderpass_hwsetup *hw_setup;
   struct pvr_image_view *iview;
   uint32_t view_idx;
   uint32_t height;
   uint32_t width;

   hw_setup = pass->hw_setup;
   hw_render =
      &hw_setup->renders[hw_setup->subpass_map[info->subpass_idx].render];

   if (is_depth_stencil) {
      bool stencil_clear;
      bool depth_clear;
      bool is_stencil;
      bool is_depth;

      assert(hw_render->ds_surface_id != -1);
      assert(index == 0);

      view_idx = hw_render->ds_surface_id;

      is_depth = vk_format_has_depth(pass->attachments[view_idx].vk_format);
      is_stencil = vk_format_has_stencil(pass->attachments[view_idx].vk_format);
      depth_clear = hw_render->depth_init == RENDERPASS_SURFACE_INITOP_CLEAR;
      stencil_clear = hw_render->stencil_init ==
                      RENDERPASS_SURFACE_INITOP_CLEAR;

      /* Attempt to clear the ds attachment. Do not erroneously discard an
       * attachment that has no depth clear but has a stencil attachment.
       */
      /* if not (a ∧ c) ∨ (b ∧ d) */
      if (!((is_depth && depth_clear) || (is_stencil && stencil_clear)))
         return;
   } else if (hw_render->color_init[index].op !=
              RENDERPASS_SURFACE_INITOP_CLEAR) {
      return;
   } else {
      view_idx = hw_render->color_init[index].driver_id;
   }

   iview = info->attachments[view_idx];
   width = iview->vk.extent.width;
   height = iview->vk.extent.height;

   /* FIXME: It would be nice if this function and pvr_sub_cmd_gfx_job_init()
    * were doing the same check (even if it's just an assert) to determine if a
    * clear is needed.
    */
   /* If this is single-layer fullscreen, we already do the clears in
    * pvr_sub_cmd_gfx_job_init().
    */
   if (info->render_area.offset.x == 0 && info->render_area.offset.y == 0 &&
       info->render_area.extent.width == width &&
       info->render_area.extent.height == height && framebuffer->layers == 1) {
      return;
   }

   pvr_finishme("Unimplemented path!");
}

static void
pvr_perform_start_of_render_clears(struct pvr_cmd_buffer *cmd_buffer)
{
   struct pvr_render_pass_info *info = &cmd_buffer->state.render_pass_info;
   const struct pvr_framebuffer *framebuffer = info->framebuffer;
   const struct pvr_render_pass *pass = info->pass;
   const struct pvr_renderpass_hwsetup *hw_setup = pass->hw_setup;
   const struct pvr_renderpass_hwsetup_render *hw_render;

   /* Mask of attachment clears using index lists instead of background object
    * to clear.
    */
   uint32_t index_list_clear_mask = 0;

   hw_render =
      &hw_setup->renders[hw_setup->subpass_map[info->subpass_idx].render];
   if (!hw_render) {
      info->process_empty_tiles = false;
      info->enable_bg_tag = false;
      return;
   }

   for (uint32_t i = 0; i < hw_render->color_init_count; i++) {
      pvr_perform_start_of_render_attachment_clear(cmd_buffer,
                                                   framebuffer,
                                                   i,
                                                   false,
                                                   &index_list_clear_mask);
   }

   info->enable_bg_tag = !!hw_render->color_init_count;

   /* If we're not using index list for all clears/loads then we need to run
    * the background object on empty tiles.
    */
   if (hw_render->color_init_count &&
       index_list_clear_mask != ((1u << hw_render->color_init_count) - 1u)) {
      info->process_empty_tiles = true;
   } else {
      info->process_empty_tiles = false;
   }

   if (hw_render->ds_surface_id != -1) {
      uint32_t ds_index_list = 0;

      pvr_perform_start_of_render_attachment_clear(cmd_buffer,
                                                   framebuffer,
                                                   0,
                                                   true,
                                                   &ds_index_list);
   }

   if (index_list_clear_mask)
      pvr_finishme("Add support for generating loadops shaders!");
}

static void pvr_stash_depth_format(struct pvr_cmd_buffer_state *state,
                                   struct pvr_sub_cmd_gfx *const sub_cmd)
{
   const struct pvr_render_pass *pass = state->render_pass_info.pass;
   const struct pvr_renderpass_hwsetup_render *hw_render =
      &pass->hw_setup->renders[sub_cmd->hw_render_idx];

   if (hw_render->ds_surface_id != -1) {
      struct pvr_image_view **iviews = state->render_pass_info.attachments;

      state->depth_format = iviews[hw_render->ds_surface_id]->vk.format;
   }
}

static bool pvr_loadops_contain_clear(struct pvr_renderpass_hwsetup *hw_setup)
{
   for (uint32_t i = 0; i < hw_setup->render_count; i++) {
      struct pvr_renderpass_hwsetup_render *hw_render = &hw_setup->renders[i];
      uint32_t render_targets_count =
         hw_render->init_setup.render_targets_count;

      for (uint32_t j = 0;
           j < (hw_render->color_init_count * render_targets_count);
           j += render_targets_count) {
         for (uint32_t k = 0; k < hw_render->init_setup.render_targets_count;
              k++) {
            if (hw_render->color_init[j + k].op ==
                RENDERPASS_SURFACE_INITOP_CLEAR) {
               return true;
            }
         }
      }
      if (hw_render->depth_init == RENDERPASS_SURFACE_INITOP_CLEAR ||
          hw_render->stencil_init == RENDERPASS_SURFACE_INITOP_CLEAR) {
         return true;
      }
   }

   return false;
}

static VkResult
pvr_cmd_buffer_set_clear_values(struct pvr_cmd_buffer *cmd_buffer,
                                const VkRenderPassBeginInfo *pRenderPassBegin)
{
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;

   /* Free any previously allocated clear values. */
   vk_free(&cmd_buffer->vk.pool->alloc, state->render_pass_info.clear_values);

   if (pRenderPassBegin->clearValueCount) {
      const size_t size = pRenderPassBegin->clearValueCount *
                          sizeof(*state->render_pass_info.clear_values);

      state->render_pass_info.clear_values =
         vk_zalloc(&cmd_buffer->vk.pool->alloc,
                   size,
                   8,
                   VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!state->render_pass_info.clear_values) {
         state->status = vk_error(cmd_buffer, VK_ERROR_OUT_OF_HOST_MEMORY);
         return state->status;
      }

      memcpy(state->render_pass_info.clear_values,
             pRenderPassBegin->pClearValues,
             size);
   } else {
      state->render_pass_info.clear_values = NULL;
   }

   state->render_pass_info.clear_value_count =
      pRenderPassBegin->clearValueCount;

   return VK_SUCCESS;
}

static bool
pvr_is_large_clear_required(const struct pvr_cmd_buffer *const cmd_buffer)
{
   const struct pvr_device_info *const dev_info =
      &cmd_buffer->device->pdevice->dev_info;
   const VkRect2D render_area = cmd_buffer->state.render_pass_info.render_area;
   const uint32_t vf_max_x = rogue_get_param_vf_max_x(dev_info);
   const uint32_t vf_max_y = rogue_get_param_vf_max_x(dev_info);

   return (render_area.extent.width > (vf_max_x / 2) - 1) ||
          (render_area.extent.height > (vf_max_y / 2) - 1);
}

static void pvr_emit_clear_words(struct pvr_cmd_buffer *const cmd_buffer,
                                 struct pvr_sub_cmd_gfx *const sub_cmd)
{
   struct pvr_csb *csb = &sub_cmd->control_stream;
   struct pvr_device *device = cmd_buffer->device;
   uint32_t *stream;

   stream = pvr_csb_alloc_dwords(csb, PVR_CLEAR_VDM_STATE_DWORD_COUNT);
   if (!stream) {
      cmd_buffer->state.status = VK_ERROR_OUT_OF_HOST_MEMORY;
      return;
   }

   if (pvr_is_large_clear_required(cmd_buffer)) {
      memcpy(stream,
             device->static_clear_state.large_clear_vdm_words,
             sizeof(device->static_clear_state.large_clear_vdm_words));
   } else {
      memcpy(stream,
             device->static_clear_state.vdm_words,
             sizeof(device->static_clear_state.vdm_words));
   }
}

static VkResult pvr_cs_write_load_op(struct pvr_cmd_buffer *cmd_buffer,
                                     struct pvr_sub_cmd_gfx *sub_cmd,
                                     struct pvr_load_op *load_op,
                                     uint32_t userpass_spawn)
{
   const struct pvr_device *device = cmd_buffer->device;
   struct pvr_static_clear_ppp_template template =
      device->static_clear_state.ppp_templates[PVR_STATIC_CLEAR_COLOR_BIT];
   uint32_t pds_state[PVR_STATIC_CLEAR_PDS_STATE_COUNT];
   struct pvr_pds_upload shareds_update_program;
   struct pvr_bo *pvr_bo;
   VkResult result;

   result = pvr_load_op_data_create_and_upload(cmd_buffer,
                                               0,
                                               &shareds_update_program);
   if (result != VK_SUCCESS)
      return result;

   template.config.ispctl.upass = userpass_spawn;

   /* It might look odd that we aren't specifying the code segment's
    * address anywhere. This is because the hardware always assumes that the
    * data size is 2 128bit words and the code segments starts after that.
    */
   pvr_csb_pack (&pds_state[PVR_STATIC_CLEAR_PPP_PDS_TYPE_SHADERBASE],
                 TA_STATE_PDS_SHADERBASE,
                 shaderbase) {
      shaderbase.addr = PVR_DEV_ADDR(load_op->pds_frag_prog.data_offset);
   }

   pvr_csb_pack (&pds_state[PVR_STATIC_CLEAR_PPP_PDS_TYPE_TEXUNICODEBASE],
                 TA_STATE_PDS_TEXUNICODEBASE,
                 texunicodebase) {
      texunicodebase.addr =
         PVR_DEV_ADDR(load_op->pds_tex_state_prog.code_offset);
   }

   pvr_csb_pack (&pds_state[PVR_STATIC_CLEAR_PPP_PDS_TYPE_SIZEINFO1],
                 TA_STATE_PDS_SIZEINFO1,
                 sizeinfo1) {
      /* Dummy coefficient loading program. */
      sizeinfo1.pds_varyingsize = 0;

      sizeinfo1.pds_texturestatesize = DIV_ROUND_UP(
         shareds_update_program.data_size,
         PVRX(TA_STATE_PDS_SIZEINFO1_PDS_TEXTURESTATESIZE_UNIT_SIZE));

      sizeinfo1.pds_tempsize =
         DIV_ROUND_UP(load_op->temps_count,
                      PVRX(TA_STATE_PDS_SIZEINFO1_PDS_TEMPSIZE_UNIT_SIZE));
   }

   pvr_csb_pack (&pds_state[PVR_STATIC_CLEAR_PPP_PDS_TYPE_SIZEINFO2],
                 TA_STATE_PDS_SIZEINFO2,
                 sizeinfo2) {
      sizeinfo2.usc_sharedsize =
         DIV_ROUND_UP(load_op->const_shareds_count,
                      PVRX(TA_STATE_PDS_SIZEINFO2_USC_SHAREDSIZE_UNIT_SIZE));
   }

   /* Dummy coefficient loading program. */
   pds_state[PVR_STATIC_CLEAR_PPP_PDS_TYPE_VARYINGBASE] = 0;

   pvr_csb_pack (&pds_state[PVR_STATIC_CLEAR_PPP_PDS_TYPE_TEXTUREDATABASE],
                 TA_STATE_PDS_TEXTUREDATABASE,
                 texturedatabase) {
      texturedatabase.addr = PVR_DEV_ADDR(shareds_update_program.data_offset);
   }

   template.config.pds_state = &pds_state;

   pvr_emit_ppp_from_template(&sub_cmd->control_stream, &template, &pvr_bo);
   list_add(&pvr_bo->link, &cmd_buffer->bo_list);

   pvr_emit_clear_words(cmd_buffer, sub_cmd);

   pvr_reset_graphics_dirty_state(&cmd_buffer->state, false);

   return VK_SUCCESS;
}

void pvr_CmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                             const VkRenderPassBeginInfo *pRenderPassBeginInfo,
                             const VkSubpassBeginInfo *pSubpassBeginInfo)
{
   PVR_FROM_HANDLE(pvr_framebuffer,
                   framebuffer,
                   pRenderPassBeginInfo->framebuffer);
   PVR_FROM_HANDLE(pvr_render_pass, pass, pRenderPassBeginInfo->renderPass);
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   const struct pvr_renderpass_hwsetup_subpass *hw_subpass;
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   VkResult result;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   assert(!state->render_pass_info.pass);
   assert(cmd_buffer->vk.level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);

   /* FIXME: Create a separate function for everything using pass->subpasses,
    * look at cmd_buffer_begin_subpass() for example. */
   state->render_pass_info.pass = pass;
   state->render_pass_info.framebuffer = framebuffer;
   state->render_pass_info.subpass_idx = 0;
   state->render_pass_info.render_area = pRenderPassBeginInfo->renderArea;
   state->render_pass_info.current_hw_subpass = 0;
   state->render_pass_info.pipeline_bind_point =
      pass->subpasses[0].pipeline_bind_point;
   state->render_pass_info.userpass_spawn = pass->subpasses[0].userpass_spawn;
   state->dirty.userpass_spawn = true;

   result = pvr_cmd_buffer_setup_attachments(cmd_buffer, pass, framebuffer);
   if (result != VK_SUCCESS)
      return;

   state->status =
      pvr_init_render_targets(cmd_buffer->device, pass, framebuffer);
   if (state->status != VK_SUCCESS)
      return;

   result = pvr_cmd_buffer_set_clear_values(cmd_buffer, pRenderPassBeginInfo);
   if (result != VK_SUCCESS)
      return;

   assert(pass->subpasses[0].pipeline_bind_point ==
          VK_PIPELINE_BIND_POINT_GRAPHICS);

   result = pvr_cmd_buffer_start_sub_cmd(cmd_buffer, PVR_SUB_CMD_TYPE_GRAPHICS);
   if (result != VK_SUCCESS)
      return;

   /* Run subpass 0 "soft" background object after the actual background
    * object.
    */
   hw_subpass = pvr_get_hw_subpass(pass, 0);
   if (hw_subpass->load_op) {
      result = pvr_cs_write_load_op(cmd_buffer,
                                    &cmd_buffer->state.current_sub_cmd->gfx,
                                    hw_subpass->load_op,
                                    0);
      if (result != VK_SUCCESS)
         return;
   }

   pvr_perform_start_of_render_clears(cmd_buffer);
   pvr_stash_depth_format(&cmd_buffer->state,
                          &cmd_buffer->state.current_sub_cmd->gfx);

   if (!pvr_loadops_contain_clear(pass->hw_setup)) {
      state->dynamic.scissor_accum_state = PVR_SCISSOR_ACCUM_CHECK_FOR_CLEAR;
      state->dynamic.scissor_accum_bounds.offset.x = 0;
      state->dynamic.scissor_accum_bounds.offset.y = 0;
      state->dynamic.scissor_accum_bounds.extent.width = 0;
      state->dynamic.scissor_accum_bounds.extent.height = 0;
   } else {
      state->dynamic.scissor_accum_state = PVR_SCISSOR_ACCUM_DISABLED;
   }
}

VkResult pvr_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                                const VkCommandBufferBeginInfo *pBeginInfo)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *state;
   VkResult result;

   pvr_cmd_buffer_reset(cmd_buffer);

   cmd_buffer->usage_flags = pBeginInfo->flags;
   state = &cmd_buffer->state;

   /* VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT must be ignored for
    * primary level command buffers.
    *
    * From the Vulkan 1.0 spec:
    *
    *    VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT specifies that a
    *    secondary command buffer is considered to be entirely inside a render
    *    pass. If this is a primary command buffer, then this bit is ignored.
    */
   if (cmd_buffer->vk.level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
      cmd_buffer->usage_flags &=
         ~VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
   }

   if (cmd_buffer->usage_flags &
       VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT) {
      const VkCommandBufferInheritanceInfo *inheritance_info =
         pBeginInfo->pInheritanceInfo;
      struct pvr_render_pass *pass;

      pass = pvr_render_pass_from_handle(inheritance_info->renderPass);
      state->render_pass_info.pass = pass;
      state->render_pass_info.framebuffer =
         pvr_framebuffer_from_handle(inheritance_info->framebuffer);
      state->render_pass_info.subpass_idx = inheritance_info->subpass;
      state->render_pass_info.userpass_spawn =
         pass->subpasses[inheritance_info->subpass].userpass_spawn;

      result =
         pvr_cmd_buffer_start_sub_cmd(cmd_buffer, PVR_SUB_CMD_TYPE_GRAPHICS);
      if (result != VK_SUCCESS)
         return result;
   }

   memset(state->barriers_needed,
          0xFF,
          sizeof(*state->barriers_needed) * ARRAY_SIZE(state->barriers_needed));

   cmd_buffer->status = PVR_CMD_BUFFER_STATUS_RECORDING;

   return VK_SUCCESS;
}

VkResult pvr_ResetCommandBuffer(VkCommandBuffer commandBuffer,
                                VkCommandBufferResetFlags flags)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);

   pvr_cmd_buffer_reset(cmd_buffer);

   return VK_SUCCESS;
}

VkResult pvr_cmd_buffer_add_transfer_cmd(struct pvr_cmd_buffer *cmd_buffer,
                                         struct pvr_transfer_cmd *transfer_cmd)
{
   struct pvr_sub_cmd_transfer *sub_cmd;
   VkResult result;

   result = pvr_cmd_buffer_start_sub_cmd(cmd_buffer, PVR_SUB_CMD_TYPE_TRANSFER);
   if (result != VK_SUCCESS)
      return result;

   sub_cmd = &cmd_buffer->state.current_sub_cmd->transfer;

   list_addtail(&transfer_cmd->link, &sub_cmd->transfer_cmds);

   return VK_SUCCESS;
}

#define PVR_WRITE(_buffer, _value, _offset, _max)                \
   do {                                                          \
      __typeof__(_value) __value = _value;                       \
      uint64_t __offset = _offset;                               \
      uint32_t __nr_dwords = sizeof(__value) / sizeof(uint32_t); \
      static_assert(__same_type(*_buffer, __value),              \
                    "Buffer and value type mismatch");           \
      assert((__offset + __nr_dwords) <= (_max));                \
      assert((__offset % __nr_dwords) == 0U);                    \
      _buffer[__offset / __nr_dwords] = __value;                 \
   } while (0)

static VkResult
pvr_setup_vertex_buffers(struct pvr_cmd_buffer *cmd_buffer,
                         const struct pvr_graphics_pipeline *const gfx_pipeline)
{
   const struct pvr_vertex_shader_state *const vertex_state =
      &gfx_pipeline->vertex_shader_state;
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;
   const struct pvr_pds_info *const pds_info = state->pds_shader.info;
   const uint8_t *entries;
   uint32_t *dword_buffer;
   uint64_t *qword_buffer;
   struct pvr_bo *pvr_bo;
   VkResult result;

   result = pvr_cmd_buffer_alloc_mem(cmd_buffer,
                                     cmd_buffer->device->heaps.pds_heap,
                                     pds_info->data_size_in_dwords,
                                     PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                                     &pvr_bo);
   if (result != VK_SUCCESS)
      return result;

   dword_buffer = (uint32_t *)pvr_bo->bo->map;
   qword_buffer = (uint64_t *)pvr_bo->bo->map;

   entries = (uint8_t *)pds_info->entries;

   for (uint32_t i = 0; i < pds_info->entry_count; i++) {
      const struct pvr_const_map_entry *const entry_header =
         (struct pvr_const_map_entry *)entries;

      switch (entry_header->type) {
      case PVR_PDS_CONST_MAP_ENTRY_TYPE_LITERAL32: {
         const struct pvr_const_map_entry_literal32 *const literal =
            (struct pvr_const_map_entry_literal32 *)entries;

         PVR_WRITE(dword_buffer,
                   literal->literal_value,
                   literal->const_offset,
                   pds_info->data_size_in_dwords);

         entries += sizeof(*literal);
         break;
      }

      case PVR_PDS_CONST_MAP_ENTRY_TYPE_DOUTU_ADDRESS: {
         const struct pvr_const_map_entry_doutu_address *const doutu_addr =
            (struct pvr_const_map_entry_doutu_address *)entries;
         const pvr_dev_addr_t exec_addr =
            PVR_DEV_ADDR_OFFSET(vertex_state->bo->vma->dev_addr,
                                vertex_state->entry_offset);
         uint64_t addr = 0ULL;

         pvr_set_usc_execution_address64(&addr, exec_addr.addr);

         PVR_WRITE(qword_buffer,
                   addr | doutu_addr->doutu_control,
                   doutu_addr->const_offset,
                   pds_info->data_size_in_dwords);

         entries += sizeof(*doutu_addr);
         break;
      }

      case PVR_PDS_CONST_MAP_ENTRY_TYPE_BASE_INSTANCE: {
         const struct pvr_const_map_entry_base_instance *const base_instance =
            (struct pvr_const_map_entry_base_instance *)entries;

         PVR_WRITE(dword_buffer,
                   state->draw_state.base_instance,
                   base_instance->const_offset,
                   pds_info->data_size_in_dwords);

         entries += sizeof(*base_instance);
         break;
      }

      case PVR_PDS_CONST_MAP_ENTRY_TYPE_VERTEX_ATTRIBUTE_ADDRESS: {
         const struct pvr_const_map_entry_vertex_attribute_address
            *const attribute =
               (struct pvr_const_map_entry_vertex_attribute_address *)entries;
         const struct pvr_vertex_binding *const binding =
            &state->vertex_bindings[attribute->binding_index];
         const pvr_dev_addr_t addr =
            PVR_DEV_ADDR_OFFSET(binding->buffer->dev_addr,
                                binding->offset + attribute->offset);

         PVR_WRITE(qword_buffer,
                   addr.addr,
                   attribute->const_offset,
                   pds_info->data_size_in_dwords);

         entries += sizeof(*attribute);
         break;
      }

      default:
         unreachable("Unsupported data section map");
         break;
      }
   }

   state->pds_vertex_attrib_offset =
      pvr_bo->vma->dev_addr.addr -
      cmd_buffer->device->heaps.pds_heap->base_addr.addr;

   pvr_bo_cpu_unmap(cmd_buffer->device, pvr_bo);

   return VK_SUCCESS;
}

static VkResult pvr_setup_descriptor_mappings(
   struct pvr_cmd_buffer *const cmd_buffer,
   enum pvr_stage_allocation stage,
   const struct pvr_stage_allocation_descriptor_state *descriptor_state,
   UNUSED const pvr_dev_addr_t *const num_worgroups_buff_addr,
   uint32_t *const descriptor_data_offset_out)
{
   const struct pvr_pds_info *const pds_info = &descriptor_state->pds_info;
   const struct pvr_descriptor_state *desc_state;
   const uint8_t *entries;
   uint32_t *dword_buffer;
   uint64_t *qword_buffer;
   struct pvr_bo *pvr_bo;
   VkResult result;

   pvr_finishme("Handle num_worgroups_buff_addr");

   if (!pds_info->data_size_in_dwords)
      return VK_SUCCESS;

   result = pvr_cmd_buffer_alloc_mem(cmd_buffer,
                                     cmd_buffer->device->heaps.pds_heap,
                                     pds_info->data_size_in_dwords,
                                     PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                                     &pvr_bo);
   if (result != VK_SUCCESS)
      return result;

   dword_buffer = (uint32_t *)pvr_bo->bo->map;
   qword_buffer = (uint64_t *)pvr_bo->bo->map;

   entries = (uint8_t *)pds_info->entries;

   switch (stage) {
   case PVR_STAGE_ALLOCATION_VERTEX_GEOMETRY:
   case PVR_STAGE_ALLOCATION_FRAGMENT:
      desc_state = &cmd_buffer->state.gfx_desc_state;
      break;

   case PVR_STAGE_ALLOCATION_COMPUTE:
      desc_state = &cmd_buffer->state.compute_desc_state;
      break;

   default:
      unreachable("Unsupported stage.");
      break;
   }

   for (uint32_t i = 0; i < pds_info->entry_count; i++) {
      const struct pvr_const_map_entry *const entry_header =
         (struct pvr_const_map_entry *)entries;

      /* TODO: See if instead of reusing the blend constant buffer type entry,
       * we can setup a new buffer type specifically for num_workgroups or other
       * built-in variables. The mappings are setup at pipeline creation when
       * creating the descriptor program.
       */
      pvr_finishme("Handle blend constant reuse for compute.");

      switch (entry_header->type) {
      case PVR_PDS_CONST_MAP_ENTRY_TYPE_LITERAL32: {
         const struct pvr_const_map_entry_literal32 *const literal =
            (struct pvr_const_map_entry_literal32 *)entries;

         PVR_WRITE(dword_buffer,
                   literal->literal_value,
                   literal->const_offset,
                   pds_info->data_size_in_dwords);

         entries += sizeof(*literal);
         break;
      }

      case PVR_PDS_CONST_MAP_ENTRY_TYPE_CONSTANT_BUFFER: {
         const struct pvr_const_map_entry_constant_buffer *const_buffer_entry =
            (struct pvr_const_map_entry_constant_buffer *)entries;
         const uint32_t desc_set = const_buffer_entry->desc_set;
         const uint32_t binding = const_buffer_entry->binding;
         const struct pvr_descriptor_set *descriptor_set;
         const struct pvr_descriptor *descriptor;
         pvr_dev_addr_t buffer_addr;

         assert(desc_set < PVR_MAX_DESCRIPTOR_SETS);
         descriptor_set = desc_state->descriptor_sets[desc_set];

         /* TODO: Handle dynamic buffers. */
         descriptor = &descriptor_set->descriptors[binding];
         assert(descriptor->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

         assert(descriptor->buffer_desc_range ==
                const_buffer_entry->size_in_dwords * sizeof(uint32_t));
         assert(descriptor->buffer_create_info_size ==
                const_buffer_entry->size_in_dwords * sizeof(uint32_t));

         buffer_addr =
            PVR_DEV_ADDR_OFFSET(descriptor->buffer_dev_addr,
                                const_buffer_entry->offset * sizeof(uint32_t));

         PVR_WRITE(qword_buffer,
                   buffer_addr.addr,
                   const_buffer_entry->const_offset,
                   pds_info->data_size_in_dwords);

         entries += sizeof(*const_buffer_entry);
         break;
      }

      case PVR_PDS_CONST_MAP_ENTRY_TYPE_DESCRIPTOR_SET: {
         const struct pvr_const_map_entry_descriptor_set *desc_set_entry =
            (struct pvr_const_map_entry_descriptor_set *)entries;
         const uint32_t desc_set_num = desc_set_entry->descriptor_set;
         const struct pvr_descriptor_set *descriptor_set;
         pvr_dev_addr_t desc_set_addr;

         assert(desc_set_num < PVR_MAX_DESCRIPTOR_SETS);

         /* TODO: Remove this when the compiler provides us with usage info?
          */
         /* We skip DMAing unbound descriptor sets. */
         if (!(desc_state->valid_mask & BITFIELD_BIT(desc_set_num))) {
            const struct pvr_const_map_entry_literal32 *literal;
            uint32_t zero_literal_value;

            entries += sizeof(*desc_set_entry);
            literal = (struct pvr_const_map_entry_literal32 *)entries;

            /* TODO: Is there any guarantee that a literal will follow the
             * descriptor set entry?
             */
            assert(literal->type == PVR_PDS_CONST_MAP_ENTRY_TYPE_LITERAL32);

            /* We zero out the DMA size so the DMA isn't performed. */
            zero_literal_value =
               literal->literal_value &
               PVR_ROGUE_PDSINST_DOUT_FIELDS_DOUTD_SRC1_BSIZE_CLRMSK;

            PVR_WRITE(qword_buffer,
                      UINT64_C(0),
                      desc_set_entry->const_offset,
                      pds_info->data_size_in_dwords);

            PVR_WRITE(dword_buffer,
                      zero_literal_value,
                      desc_set_entry->const_offset,
                      pds_info->data_size_in_dwords);

            entries += sizeof(*literal);
            i++;
            continue;
         }

         descriptor_set = desc_state->descriptor_sets[desc_set_num];

         desc_set_addr = descriptor_set->pvr_bo->vma->dev_addr;

         if (desc_set_entry->primary) {
            desc_set_addr = PVR_DEV_ADDR_OFFSET(
               desc_set_addr,
               descriptor_set->layout->memory_layout_in_dwords_per_stage[stage]
                     .primary_offset
                  << 2U);
         } else {
            desc_set_addr = PVR_DEV_ADDR_OFFSET(
               desc_set_addr,
               descriptor_set->layout->memory_layout_in_dwords_per_stage[stage]
                     .secondary_offset
                  << 2U);
         }

         desc_set_addr = PVR_DEV_ADDR_OFFSET(
            desc_set_addr,
            (uint64_t)desc_set_entry->offset_in_dwords << 2U);

         PVR_WRITE(qword_buffer,
                   desc_set_addr.addr,
                   desc_set_entry->const_offset,
                   pds_info->data_size_in_dwords);

         entries += sizeof(*desc_set_entry);
         break;
      }

      case PVR_PDS_CONST_MAP_ENTRY_TYPE_SPECIAL_BUFFER: {
         const struct pvr_const_map_entry_special_buffer *special_buff_entry =
            (struct pvr_const_map_entry_special_buffer *)entries;

         switch (special_buff_entry->buffer_type) {
         case PVR_BUFFER_TYPES_COMPILE_TIME: {
            uint64_t addr = descriptor_state->static_consts->vma->dev_addr.addr;

            PVR_WRITE(qword_buffer,
                      addr,
                      special_buff_entry->const_offset,
                      pds_info->data_size_in_dwords);
            break;
         }

         default:
            unreachable("Unsupported special buffer type.");
         }

         entries += sizeof(*special_buff_entry);
         break;
      }

      default:
         unreachable("Unsupported map entry type.");
      }
   }

   pvr_bo_cpu_unmap(cmd_buffer->device, pvr_bo);

   *descriptor_data_offset_out =
      pvr_bo->vma->dev_addr.addr -
      cmd_buffer->device->heaps.pds_heap->base_addr.addr;

   return VK_SUCCESS;
}

#undef PVR_WRITE

static void pvr_compute_update_shared(struct pvr_cmd_buffer *cmd_buffer,
                                      struct pvr_sub_cmd_compute *const sub_cmd)
{
   const struct pvr_physical_device *pdevice = cmd_buffer->device->pdevice;
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   struct pvr_csb *csb = &sub_cmd->control_stream;
   const struct pvr_compute_pipeline *pipeline = state->compute_pipeline;
   const uint32_t const_shared_reg_count =
      pipeline->state.shader.const_shared_reg_count;
   struct pvr_compute_kernel_info info;

   /* No shared regs, no need to use an allocation kernel. */
   if (!const_shared_reg_count)
      return;

   info = (struct pvr_compute_kernel_info){
      .indirect_buffer_addr = PVR_DEV_ADDR_INVALID,
      .sd_type = PVRX(CDMCTRL_SD_TYPE_NONE),

      .usc_target = PVRX(CDMCTRL_USC_TARGET_ALL),
      .usc_common_shared = true,
      .usc_common_size =
         DIV_ROUND_UP(const_shared_reg_count,
                      PVRX(CDMCTRL_KERNEL0_USC_COMMON_SIZE_UNIT_SIZE)),

      .local_size = { 1, 1, 1 },
      .global_size = { 1, 1, 1 },
   };

   /* Sometimes we don't have a secondary program if there were no constants to
    * write, but we still need to run a PDS program to accomplish the
    * allocation of the local/common store shared registers so we repurpose the
    * deallocation PDS program.
    */
   if (pipeline->state.descriptor.pds_info.code_size_in_dwords) {
      uint32_t pds_data_size_in_dwords =
         pipeline->state.descriptor.pds_info.data_size_in_dwords;

      info.pds_data_offset = state->pds_compute_descriptor_data_offset;
      info.pds_data_size =
         DIV_ROUND_UP(pds_data_size_in_dwords << 2U,
                      PVRX(CDMCTRL_KERNEL0_PDS_DATA_SIZE_UNIT_SIZE));

      /* Check that we have upload the code section. */
      assert(pipeline->state.descriptor.pds_code.code_size);
      info.pds_code_offset = pipeline->state.descriptor.pds_code.code_offset;
   } else {
      /* FIXME: There should be a deallocation pds program already uploaded
       * that we use at this point.
       */
      assert(!"Unimplemented");
   }

   /* We don't need to pad the workgroup size. */

   info.max_instances =
      pvr_compute_flat_slot_size(pdevice, const_shared_reg_count, false, 1U);

   pvr_compute_generate_control_stream(csb, sub_cmd, &info);
}

static uint32_t
pvr_compute_flat_pad_workgroup_size(const struct pvr_physical_device *pdevice,
                                    uint32_t workgroup_size,
                                    uint32_t coeff_regs_count)
{
   const struct pvr_device_runtime_info *dev_runtime_info =
      &pdevice->dev_runtime_info;
   const struct pvr_device_info *dev_info = &pdevice->dev_info;
   uint32_t max_avail_coeff_regs =
      dev_runtime_info->cdm_max_local_mem_size_regs;
   uint32_t coeff_regs_count_aligned =
      ALIGN_POT(coeff_regs_count,
                PVRX(CDMCTRL_KERNEL0_USC_COMMON_SIZE_UNIT_SIZE) >> 2U);

   /* If the work group size is > ROGUE_MAX_INSTANCES_PER_TASK. We now *always*
    * pad the work group size to the next multiple of
    * ROGUE_MAX_INSTANCES_PER_TASK.
    *
    * If we use more than 1/8th of the max coefficient registers then we round
    * work group size up to the next multiple of ROGUE_MAX_INSTANCES_PER_TASK
    */
   /* TODO: See if this can be optimized. */
   if (workgroup_size > ROGUE_MAX_INSTANCES_PER_TASK ||
       coeff_regs_count_aligned > (max_avail_coeff_regs / 8)) {
      assert(workgroup_size < rogue_get_compute_max_work_group_size(dev_info));

      return ALIGN_POT(workgroup_size, ROGUE_MAX_INSTANCES_PER_TASK);
   }

   return workgroup_size;
}

/* TODO: Wire up the base_workgroup variant program when implementing
 * VK_KHR_device_group. The values will also need patching into the program.
 */
static void pvr_compute_update_kernel(
   struct pvr_cmd_buffer *cmd_buffer,
   struct pvr_sub_cmd_compute *const sub_cmd,
   const uint32_t global_workgroup_size[static const PVR_WORKGROUP_DIMENSIONS])
{
   const struct pvr_physical_device *pdevice = cmd_buffer->device->pdevice;
   const struct pvr_device_runtime_info *dev_runtime_info =
      &pdevice->dev_runtime_info;
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   struct pvr_csb *csb = &sub_cmd->control_stream;
   const struct pvr_compute_pipeline *pipeline = state->compute_pipeline;
   const struct pvr_pds_info *program_info =
      &pipeline->state.primary_program_info;

   struct pvr_compute_kernel_info info = {
      .indirect_buffer_addr = PVR_DEV_ADDR_INVALID,
      .usc_target = PVRX(CDMCTRL_USC_TARGET_ANY),
      .pds_temp_size =
         DIV_ROUND_UP(program_info->temps_required << 2U,
                      PVRX(CDMCTRL_KERNEL0_PDS_TEMP_SIZE_UNIT_SIZE)),

      .pds_data_size =
         DIV_ROUND_UP(program_info->data_size_in_dwords << 2U,
                      PVRX(CDMCTRL_KERNEL0_PDS_DATA_SIZE_UNIT_SIZE)),
      .pds_data_offset = pipeline->state.primary_program.data_offset,
      .pds_code_offset = pipeline->state.primary_program.code_offset,

      .sd_type = PVRX(CDMCTRL_SD_TYPE_USC),

      .usc_unified_size =
         DIV_ROUND_UP(pipeline->state.shader.input_register_count << 2U,
                      PVRX(CDMCTRL_KERNEL0_USC_UNIFIED_SIZE_UNIT_SIZE)),

      /* clang-format off */
      .global_size = {
         global_workgroup_size[0],
         global_workgroup_size[1],
         global_workgroup_size[2]
      },
      /* clang-format on */
   };

   uint32_t work_size = pipeline->state.shader.work_size;
   uint32_t coeff_regs;

   if (work_size > ROGUE_MAX_INSTANCES_PER_TASK) {
      /* Enforce a single workgroup per cluster through allocation starvation.
       */
      coeff_regs = dev_runtime_info->cdm_max_local_mem_size_regs;
   } else {
      coeff_regs = pipeline->state.shader.coefficient_register_count;
   }

   info.usc_common_size =
      DIV_ROUND_UP(coeff_regs << 2U,
                   PVRX(CDMCTRL_KERNEL0_USC_COMMON_SIZE_UNIT_SIZE));

   /* Use a whole slot per workgroup. */
   work_size = MAX2(work_size, ROGUE_MAX_INSTANCES_PER_TASK);

   coeff_regs += pipeline->state.shader.const_shared_reg_count;

   work_size =
      pvr_compute_flat_pad_workgroup_size(pdevice, work_size, coeff_regs);

   info.local_size[0] = work_size;
   info.local_size[1] = 1U;
   info.local_size[2] = 1U;

   info.max_instances =
      pvr_compute_flat_slot_size(pdevice, coeff_regs, false, work_size);

   pvr_compute_generate_control_stream(csb, sub_cmd, &info);
}

void pvr_CmdDispatch(VkCommandBuffer commandBuffer,
                     uint32_t groupCountX,
                     uint32_t groupCountY,
                     uint32_t groupCountZ)
{
   const uint32_t workgroup_size[] = { groupCountX, groupCountY, groupCountZ };
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   const struct pvr_compute_pipeline *compute_pipeline =
      state->compute_pipeline;
   const VkShaderStageFlags push_consts_stage_mask =
      compute_pipeline->base.layout->push_constants_shader_stages;
   struct pvr_sub_cmd_compute *sub_cmd;
   VkResult result;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);
   assert(compute_pipeline);

   if (!groupCountX || !groupCountY || !groupCountZ)
      return;

   pvr_cmd_buffer_start_sub_cmd(cmd_buffer, PVR_SUB_CMD_TYPE_COMPUTE);

   sub_cmd = &state->current_sub_cmd->compute;

   sub_cmd->uses_atomic_ops |= compute_pipeline->state.shader.uses_atomic_ops;
   sub_cmd->uses_barrier |= compute_pipeline->state.shader.uses_barrier;

   if (push_consts_stage_mask & VK_SHADER_STAGE_COMPUTE_BIT) {
      /* TODO: Add a dirty push constants mask in the cmd_buffer state and
       * check for dirty compute stage.
       */
      pvr_finishme("Add support for push constants.");
   }

   if (compute_pipeline->state.shader.uses_num_workgroups) {
      struct pvr_bo *num_workgroups_bo;

      result = pvr_cmd_buffer_upload_general(cmd_buffer,
                                             workgroup_size,
                                             sizeof(workgroup_size),
                                             &num_workgroups_bo);
      if (result != VK_SUCCESS)
         return;

      result = pvr_setup_descriptor_mappings(
         cmd_buffer,
         PVR_STAGE_ALLOCATION_COMPUTE,
         &compute_pipeline->state.descriptor,
         &num_workgroups_bo->vma->dev_addr,
         &state->pds_compute_descriptor_data_offset);
      if (result != VK_SUCCESS)
         return;
   } else if ((compute_pipeline->base.layout
                  ->per_stage_descriptor_masks[PVR_STAGE_ALLOCATION_COMPUTE] &&
               state->dirty.compute_desc_dirty) ||
              state->dirty.compute_pipeline_binding) {
      result = pvr_setup_descriptor_mappings(
         cmd_buffer,
         PVR_STAGE_ALLOCATION_COMPUTE,
         &compute_pipeline->state.descriptor,
         NULL,
         &state->pds_compute_descriptor_data_offset);
      if (result != VK_SUCCESS)
         return;
   }

   pvr_compute_update_shared(cmd_buffer, sub_cmd);

   pvr_compute_update_kernel(cmd_buffer, sub_cmd, workgroup_size);
}

void pvr_CmdDispatchIndirect(VkCommandBuffer commandBuffer,
                             VkBuffer _buffer,
                             VkDeviceSize offset)
{
   assert(!"Unimplemented");
}

static void
pvr_update_draw_state(struct pvr_cmd_buffer_state *const state,
                      const struct pvr_cmd_buffer_draw_state *const draw_state)
{
   /* We don't have a state to tell us that base_instance is being used so it
    * gets used as a boolean - 0 means we'll use a pds program that skips the
    * base instance addition. If the base_instance gets used (and the last
    * draw's base_instance was 0) then we switch to the BASE_INSTANCE attrib
    * program.
    *
    * If base_instance changes then we only need to update the data section.
    *
    * The only draw call state that doesn't really matter is the start vertex
    * as that is handled properly in the VDM state in all cases.
    */
   if ((state->draw_state.draw_indexed != draw_state->draw_indexed) ||
       (state->draw_state.draw_indirect != draw_state->draw_indirect) ||
       (state->draw_state.base_instance == 0 &&
        draw_state->base_instance != 0)) {
      state->dirty.draw_variant = true;
   } else if (state->draw_state.base_instance != draw_state->base_instance) {
      state->dirty.draw_base_instance = true;
   }

   state->draw_state = *draw_state;
}

static uint32_t pvr_calc_shared_regs_count(
   const struct pvr_graphics_pipeline *const gfx_pipeline)
{
   const struct pvr_pipeline_stage_state *const vertex_state =
      &gfx_pipeline->vertex_shader_state.stage_state;
   uint32_t shared_regs = vertex_state->const_shared_reg_count +
                          vertex_state->const_shared_reg_offset;

   if (gfx_pipeline->fragment_shader_state.bo) {
      const struct pvr_pipeline_stage_state *const fragment_state =
         &gfx_pipeline->fragment_shader_state.stage_state;
      uint32_t fragment_regs = fragment_state->const_shared_reg_count +
                               fragment_state->const_shared_reg_offset;

      shared_regs = MAX2(shared_regs, fragment_regs);
   }

   return shared_regs;
}

static void
pvr_emit_dirty_pds_state(const struct pvr_cmd_buffer *const cmd_buffer,
                         struct pvr_sub_cmd_gfx *const sub_cmd,
                         const uint32_t pds_vertex_descriptor_data_offset)
{
   const struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;
   const struct pvr_stage_allocation_descriptor_state
      *const vertex_descriptor_state =
         &state->gfx_pipeline->vertex_shader_state.descriptor_state;
   const struct pvr_pipeline_stage_state *const vertex_stage_state =
      &state->gfx_pipeline->vertex_shader_state.stage_state;
   struct pvr_csb *const csb = &sub_cmd->control_stream;

   if (!vertex_descriptor_state->pds_info.code_size_in_dwords)
      return;

   pvr_csb_emit (csb, VDMCTRL_PDS_STATE0, state0) {
      state0.usc_target = PVRX(VDMCTRL_USC_TARGET_ALL);

      state0.usc_common_size =
         DIV_ROUND_UP(vertex_stage_state->const_shared_reg_count << 2,
                      PVRX(VDMCTRL_PDS_STATE0_USC_COMMON_SIZE_UNIT_SIZE));

      state0.pds_data_size = DIV_ROUND_UP(
         vertex_descriptor_state->pds_info.data_size_in_dwords << 2,
         PVRX(VDMCTRL_PDS_STATE0_PDS_DATA_SIZE_UNIT_SIZE));
   }

   pvr_csb_emit (csb, VDMCTRL_PDS_STATE1, state1) {
      state1.pds_data_addr = PVR_DEV_ADDR(pds_vertex_descriptor_data_offset);
      state1.sd_type = PVRX(VDMCTRL_SD_TYPE_NONE);
   }

   pvr_csb_emit (csb, VDMCTRL_PDS_STATE2, state2) {
      state2.pds_code_addr =
         PVR_DEV_ADDR(vertex_descriptor_state->pds_code.code_offset);
   }
}

static void pvr_setup_output_select(struct pvr_cmd_buffer *const cmd_buffer)
{
   struct pvr_emit_state *const emit_state = &cmd_buffer->state.emit_state;
   const struct pvr_graphics_pipeline *const gfx_pipeline =
      cmd_buffer->state.gfx_pipeline;
   struct pvr_ppp_state *const ppp_state = &cmd_buffer->state.ppp_state;
   const struct pvr_vertex_shader_state *const vertex_state =
      &gfx_pipeline->vertex_shader_state;
   uint32_t output_selects;

   /* TODO: Handle vertex and fragment shader state flags. */

   pvr_csb_pack (&output_selects, TA_OUTPUT_SEL, state) {
      const VkPrimitiveTopology topology =
         gfx_pipeline->input_asm_state.topology;

      state.rhw_pres = true;
      state.vtxsize = DIV_ROUND_UP(vertex_state->vertex_output_size, 4U);
      state.psprite_size_pres = (topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
   }

   if (ppp_state->output_selects != output_selects) {
      ppp_state->output_selects = output_selects;
      emit_state->output_selects = true;
   }

   if (ppp_state->varying_word[0] != vertex_state->varying[0]) {
      ppp_state->varying_word[0] = vertex_state->varying[0];
      emit_state->varying_word0 = true;
   }

   if (ppp_state->varying_word[1] != vertex_state->varying[1]) {
      ppp_state->varying_word[1] = vertex_state->varying[1];
      emit_state->varying_word1 = true;
   }
}

static void
pvr_setup_isp_faces_and_control(struct pvr_cmd_buffer *const cmd_buffer,
                                struct PVRX(TA_STATE_ISPA) *const ispa_out)
{
   struct pvr_emit_state *const emit_state = &cmd_buffer->state.emit_state;
   const struct pvr_graphics_pipeline *const gfx_pipeline =
      cmd_buffer->state.gfx_pipeline;
   struct pvr_ppp_state *const ppp_state = &cmd_buffer->state.ppp_state;
   const struct pvr_dynamic_state *const dynamic_state =
      &cmd_buffer->state.dynamic.common;
   const struct pvr_render_pass_info *const pass_info =
      &cmd_buffer->state.render_pass_info;
   const uint32_t subpass_idx = pass_info->subpass_idx;
   const uint32_t *depth_stencil_attachment_idx =
      pass_info->pass->subpasses[subpass_idx].depth_stencil_attachment;
   const struct pvr_image_view *const attachment =
      (!depth_stencil_attachment_idx)
         ? NULL
         : pass_info->attachments[*depth_stencil_attachment_idx];

   const VkCullModeFlags cull_mode = gfx_pipeline->raster_state.cull_mode;
   const bool raster_discard_enabled =
      gfx_pipeline->raster_state.discard_enable;
   const bool disable_all = raster_discard_enabled || !attachment;

   const VkPrimitiveTopology topology = gfx_pipeline->input_asm_state.topology;
   const enum PVRX(TA_OBJTYPE) obj_type = pvr_ta_objtype(topology);

   const bool disable_stencil_write = disable_all;
   const bool disable_stencil_test =
      disable_all || !vk_format_has_stencil(attachment->vk.format);

   const bool disable_depth_write = disable_all;
   const bool disable_depth_test = disable_all ||
                                   !vk_format_has_depth(attachment->vk.format);

   uint32_t ispb_stencil_off;
   bool is_two_sided = false;
   uint32_t isp_control;

   uint32_t line_width;
   uint32_t common_a;
   uint32_t front_a;
   uint32_t front_b;
   uint32_t back_a;
   uint32_t back_b;

   /* Convert to 4.4 fixed point format. */
   line_width = util_unsigned_fixed(dynamic_state->line_width, 4);

   /* Subtract 1 to shift values from range [0=0,256=16] to [0=1/16,255=16].
    * If 0 it stays at 0, otherwise we subtract 1.
    */
   line_width = (!!line_width) * (line_width - 1);

   line_width = MIN2(line_width, PVRX(TA_STATE_ISPA_POINTLINEWIDTH_SIZE_MAX));

   /* TODO: Part of the logic in this function is duplicated in another part
    * of the code. E.g. the dcmpmode, and sop1/2/3. Could we do this earlier?
    */

   pvr_csb_pack (&common_a, TA_STATE_ISPA, ispa) {
      ispa.pointlinewidth = line_width;

      if (disable_depth_test)
         ispa.dcmpmode = PVRX(TA_CMPMODE_ALWAYS);
      else
         ispa.dcmpmode = pvr_ta_cmpmode(gfx_pipeline->depth_compare_op);

      /* FIXME: Can we just have this and remove the assignment above?
       * The user provides a depthTestEnable at vkCreateGraphicsPipelines()
       * should we be using that?
       */
      ispa.dcmpmode |= gfx_pipeline->depth_compare_op;

      ispa.dwritedisable = disable_depth_test || disable_depth_write;
      /* FIXME: Can we just have this and remove the assignment above? */
      ispa.dwritedisable = ispa.dwritedisable ||
                           gfx_pipeline->depth_write_disable;

      ispa.passtype = gfx_pipeline->fragment_shader_state.pass_type;

      ispa.objtype = obj_type;

      /* Return unpacked ispa structure. dcmpmode, dwritedisable, passtype and
       * objtype are needed by pvr_setup_triangle_merging_flag.
       */
      if (ispa_out)
         *ispa_out = ispa;
   }

   /* FIXME: This logic should be redone and improved. Can we also get rid of
    * the front and back variants?
    */

   pvr_csb_pack (&front_a, TA_STATE_ISPA, ispa) {
      ispa.sref = (!disable_stencil_test) * dynamic_state->reference.front;
   }
   front_a |= common_a;

   pvr_csb_pack (&back_a, TA_STATE_ISPA, ispa) {
      ispa.sref = (!disable_stencil_test) * dynamic_state->compare_mask.back;
   }
   back_a |= common_a;

   /* TODO: Does this actually represent the ispb control word on stencil off?
    * If not, rename the variable.
    */
   pvr_csb_pack (&ispb_stencil_off, TA_STATE_ISPB, ispb) {
      ispb.sop3 = PVRX(TA_ISPB_STENCILOP_KEEP);
      ispb.sop2 = PVRX(TA_ISPB_STENCILOP_KEEP);
      ispb.sop1 = PVRX(TA_ISPB_STENCILOP_KEEP);
      ispb.scmpmode = PVRX(TA_CMPMODE_ALWAYS);
   }

   if (disable_stencil_test) {
      back_b = front_b = ispb_stencil_off;
   } else {
      pvr_csb_pack (&front_b, TA_STATE_ISPB, ispb) {
         ispb.swmask =
            (!disable_stencil_write) * dynamic_state->write_mask.front;
         ispb.scmpmask = dynamic_state->compare_mask.front;

         ispb.sop3 = pvr_ta_stencilop(gfx_pipeline->stencil_front.pass_op);
         ispb.sop2 =
            pvr_ta_stencilop(gfx_pipeline->stencil_front.depth_fail_op);
         ispb.sop1 = pvr_ta_stencilop(gfx_pipeline->stencil_front.fail_op);

         ispb.scmpmode = pvr_ta_cmpmode(gfx_pipeline->stencil_front.compare_op);
      }

      pvr_csb_pack (&back_b, TA_STATE_ISPB, ispb) {
         ispb.swmask =
            (!disable_stencil_write) * dynamic_state->write_mask.back;
         ispb.scmpmask = dynamic_state->compare_mask.back;

         ispb.sop3 = pvr_ta_stencilop(gfx_pipeline->stencil_back.pass_op);
         ispb.sop2 = pvr_ta_stencilop(gfx_pipeline->stencil_back.depth_fail_op);
         ispb.sop1 = pvr_ta_stencilop(gfx_pipeline->stencil_back.fail_op);

         ispb.scmpmode = pvr_ta_cmpmode(gfx_pipeline->stencil_back.compare_op);
      }
   }

   if (front_a != back_a || front_b != back_b) {
      if (cull_mode & VK_CULL_MODE_BACK_BIT) {
         /* Single face, using front state. */
      } else if (cull_mode & VK_CULL_MODE_FRONT_BIT) {
         /* Single face, using back state. */

         front_a = back_a;
         front_b = back_b;
      } else {
         /* Both faces. */

         emit_state->isp_ba = is_two_sided = true;

         if (gfx_pipeline->raster_state.front_face ==
             VK_FRONT_FACE_COUNTER_CLOCKWISE) {
            uint32_t tmp = front_a;

            front_a = back_a;
            back_a = tmp;

            tmp = front_b;
            front_b = back_b;
            back_b = tmp;
         }

         /* HW defaults to stencil off. */
         if (back_b != ispb_stencil_off)
            emit_state->isp_fb = emit_state->isp_bb = true;
      }
   }

   if (!disable_stencil_test && front_b != ispb_stencil_off)
      emit_state->isp_fb = true;

   pvr_csb_pack (&isp_control, TA_STATE_ISPCTL, ispctl) {
      ispctl.upass = pass_info->userpass_spawn;

      /* TODO: is bo ever NULL? Figure out what to do. */
      ispctl.tagwritedisable = raster_discard_enabled ||
                               !gfx_pipeline->fragment_shader_state.bo;

      ispctl.two_sided = is_two_sided;
      ispctl.bpres = emit_state->isp_fb || emit_state->isp_bb;

      ispctl.dbenable = !raster_discard_enabled &&
                        gfx_pipeline->raster_state.depth_bias_enable &&
                        obj_type == PVRX(TA_OBJTYPE_TRIANGLE);
      ispctl.scenable = !raster_discard_enabled;

      ppp_state->isp.control_struct = ispctl;
   }

   emit_state->isp = true;

   ppp_state->isp.control = isp_control;
   ppp_state->isp.front_a = front_a;
   ppp_state->isp.front_b = front_b;
   ppp_state->isp.back_a = back_a;
   ppp_state->isp.back_b = back_b;
}

static float
pvr_calculate_final_depth_bias_contant_factor(struct pvr_device_info *dev_info,
                                              VkFormat format,
                                              float depth_bias)
{
   /* Information for future modifiers of these depth bias calculations.
    * ==================================================================
    * Specified depth bias equations scale the specified constant factor by a
    * value 'r' that is guaranteed to cause a resolvable difference in depth
    * across the entire range of depth values.
    * For floating point depth formats 'r' is calculated by taking the maximum
    * exponent across the triangle.
    * For UNORM formats 'r' is constant.
    * Here 'n' is the number of mantissa bits stored in the floating point
    * representation (23 for F32).
    *
    *    UNORM Format -> z += dbcf * r + slope
    *    FLOAT Format -> z += dbcf * 2^(e-n) + slope
    *
    * HW Variations.
    * ==============
    * The HW either always performs the F32 depth bias equation (exponent based
    * r), or in the case of HW that correctly supports the integer depth bias
    * equation for UNORM depth formats, we can select between both equations
    * using the ROGUE_CR_ISP_CTL.dbias_is_int flag - this is required to
    * correctly perform Vulkan UNORM depth bias (constant r).
    *
    *    if ern42307:
    *       if DBIAS_IS_INT_EN:
    *          z += dbcf + slope
    *       else:
    *          z += dbcf * 2^(e-n) + slope
    *    else:
    *       z += dbcf * 2^(e-n) + slope
    *
    */

   float nudge_factor;

   if (PVR_HAS_ERN(dev_info, 42307)) {
      switch (format) {
      case VK_FORMAT_D16_UNORM:
         return depth_bias / (1 << 15);

      case VK_FORMAT_D24_UNORM_S8_UINT:
      case VK_FORMAT_X8_D24_UNORM_PACK32:
         return depth_bias / (1 << 23);

      default:
         return depth_bias;
      }
   }

   /* The reasoning behind clamping/nudging the value here is because UNORM
    * depth formats can have higher precision over our underlying D32F
    * representation for some depth ranges.
    *
    * When the HW scales the depth bias value by 2^(e-n) [The 'r' term'] a depth
    * bias of 1 can result in a value smaller than one F32 ULP, which will get
    * quantized to 0 - resulting in no bias.
    *
    * Biasing small values away from zero will ensure that small depth biases of
    * 1 still yield a result and overcome Z-fighting.
    */
   switch (format) {
   case VK_FORMAT_D16_UNORM:
      depth_bias *= 512.0f;
      nudge_factor = 1.0f;
      break;

   case VK_FORMAT_D24_UNORM_S8_UINT:
   case VK_FORMAT_X8_D24_UNORM_PACK32:
      depth_bias *= 2.0f;
      nudge_factor = 2.0f;
      break;

   default:
      nudge_factor = 0.0f;
      break;
   }

   if (nudge_factor != 0.0f) {
      if (depth_bias < 0.0f && depth_bias > -nudge_factor)
         depth_bias -= nudge_factor;
      else if (depth_bias > 0.0f && depth_bias < nudge_factor)
         depth_bias += nudge_factor;
   }

   return depth_bias;
}

static void pvr_get_viewport_scissor_overlap(const VkViewport *const viewport,
                                             const VkRect2D *const scissor,
                                             VkRect2D *const rect_out)
{
   /* TODO: See if we can remove this struct. */
   struct pvr_rect {
      int32_t x0, y0;
      int32_t x1, y1;
   };

   /* TODO: Worry about overflow? */
   const struct pvr_rect scissor_rect = {
      .x0 = scissor->offset.x,
      .y0 = scissor->offset.y,
      .x1 = scissor->offset.x + scissor->extent.width,
      .y1 = scissor->offset.y + scissor->extent.height
   };
   struct pvr_rect viewport_rect = { 0 };

   assert(viewport->width >= 0.0f);
   assert(scissor_rect.x0 >= 0);
   assert(scissor_rect.y0 >= 0);

   if (scissor->extent.width == 0 || scissor->extent.height == 0) {
      *rect_out = (VkRect2D){ 0 };
      return;
   }

   viewport_rect.x0 = (int32_t)viewport->x;
   viewport_rect.x1 = (int32_t)viewport->x + (int32_t)viewport->width;

   /* TODO: Is there a mathematical way of doing all this and then clamp at
    * the end?
    */
   /* We flip the y0 and y1 when height is negative. */
   viewport_rect.y0 = (int32_t)viewport->y + MIN2(0, (int32_t)viewport->height);
   viewport_rect.y1 = (int32_t)viewport->y + MAX2(0, (int32_t)viewport->height);

   if (scissor_rect.x1 <= viewport_rect.x0 ||
       scissor_rect.y1 <= viewport_rect.y0 ||
       scissor_rect.x0 >= viewport_rect.x1 ||
       scissor_rect.y0 >= viewport_rect.y1) {
      *rect_out = (VkRect2D){ 0 };
      return;
   }

   /* Determine the overlapping rectangle. */
   viewport_rect.x0 = MAX2(viewport_rect.x0, scissor_rect.x0);
   viewport_rect.y0 = MAX2(viewport_rect.y0, scissor_rect.y0);
   viewport_rect.x1 = MIN2(viewport_rect.x1, scissor_rect.x1);
   viewport_rect.y1 = MIN2(viewport_rect.y1, scissor_rect.y1);

   /* TODO: Is this conversion safe? Is this logic right? */
   rect_out->offset.x = (uint32_t)viewport_rect.x0;
   rect_out->offset.y = (uint32_t)viewport_rect.y0;
   rect_out->extent.height = (uint32_t)(viewport_rect.y1 - viewport_rect.y0);
   rect_out->extent.width = (uint32_t)(viewport_rect.x1 - viewport_rect.x0);
}

static inline uint32_t
pvr_get_geom_region_clip_align_size(struct pvr_device_info *const dev_info)
{
   /* TODO: This should come from rogue_ppp.xml. */
   return 16U + 16U * (!PVR_HAS_FEATURE(dev_info, tile_size_16x16));
}

static void
pvr_setup_isp_depth_bias_scissor_state(struct pvr_cmd_buffer *const cmd_buffer)
{
   struct pvr_emit_state *const emit_state = &cmd_buffer->state.emit_state;
   struct pvr_ppp_state *const ppp_state = &cmd_buffer->state.ppp_state;
   const struct pvr_dynamic_state *const dynamic_state =
      &cmd_buffer->state.dynamic.common;
   const struct PVRX(TA_STATE_ISPCTL) *const ispctl =
      &ppp_state->isp.control_struct;
   struct pvr_device_info *const dev_info =
      &cmd_buffer->device->pdevice->dev_info;

   if (ispctl->dbenable && (cmd_buffer->state.dirty.depth_bias ||
                            cmd_buffer->depth_bias_array.size == 0)) {
      struct pvr_depth_bias_state depth_bias = dynamic_state->depth_bias;

      depth_bias.constant_factor =
         pvr_calculate_final_depth_bias_contant_factor(
            dev_info,
            cmd_buffer->state.depth_format,
            depth_bias.constant_factor);

      ppp_state->depthbias_scissor_indices.depthbias_index =
         util_dynarray_num_elements(&cmd_buffer->depth_bias_array,
                                    __typeof__(depth_bias));

      util_dynarray_append(&cmd_buffer->depth_bias_array,
                           __typeof__(depth_bias),
                           depth_bias);

      emit_state->isp_dbsc = true;
   }

   if (ispctl->scenable) {
      const uint32_t region_clip_align_size =
         pvr_get_geom_region_clip_align_size(dev_info);
      const VkViewport *const viewport = &dynamic_state->viewport.viewports[0];
      const VkRect2D *const scissor = &dynamic_state->scissor.scissors[0];
      VkRect2D overlap_rect;
      uint32_t scissor_words[2];
      uint32_t height;
      uint32_t width;
      uint32_t x;
      uint32_t y;

      /* For region clip. */
      uint32_t bottom;
      uint32_t right;
      uint32_t left;
      uint32_t top;

      /* We don't support multiple viewport calculations. */
      assert(dynamic_state->viewport.count == 1);
      /* We don't support multiple scissor calculations. */
      assert(dynamic_state->scissor.count == 1);

      pvr_get_viewport_scissor_overlap(viewport, scissor, &overlap_rect);

      x = overlap_rect.offset.x;
      y = overlap_rect.offset.y;
      width = overlap_rect.extent.width;
      height = overlap_rect.extent.height;

      pvr_csb_pack (&scissor_words[0], IPF_SCISSOR_WORD_0, word0) {
         word0.scw0_xmax = x + width;
         word0.scw0_xmin = x;
      }

      pvr_csb_pack (&scissor_words[1], IPF_SCISSOR_WORD_1, word1) {
         word1.scw1_ymax = y + height;
         word1.scw1_ymin = y;
      }

      if (cmd_buffer->scissor_array.size &&
          cmd_buffer->scissor_words[0] == scissor_words[0] &&
          cmd_buffer->scissor_words[1] == scissor_words[1]) {
         return;
      }

      cmd_buffer->scissor_words[0] = scissor_words[0];
      cmd_buffer->scissor_words[1] = scissor_words[1];

      /* Calculate region clip. */

      left = x / region_clip_align_size;
      top = y / region_clip_align_size;

      /* We prevent right=-1 with the multiplication. */
      /* TODO: Is there a better way of doing this? */
      if ((x + width) != 0U)
         right = DIV_ROUND_UP(x + width, region_clip_align_size) - 1;
      else
         right = 0;

      if ((y + height) != 0U)
         bottom = DIV_ROUND_UP(y + height, region_clip_align_size) - 1;
      else
         bottom = 0U;

      /* Setup region clip to clip everything outside what was calculated. */

      /* FIXME: Should we mask to prevent writing over other words? */
      pvr_csb_pack (&ppp_state->region_clipping.word0, TA_REGION_CLIP0, word0) {
         word0.right = right;
         word0.left = left;
         word0.mode = PVRX(TA_REGION_CLIP_MODE_OUTSIDE);
      }

      pvr_csb_pack (&ppp_state->region_clipping.word1, TA_REGION_CLIP1, word1) {
         word1.bottom = bottom;
         word1.top = top;
      }

      ppp_state->depthbias_scissor_indices.scissor_index =
         util_dynarray_num_elements(&cmd_buffer->scissor_array,
                                    __typeof__(cmd_buffer->scissor_words));

      memcpy(util_dynarray_grow_bytes(&cmd_buffer->scissor_array,
                                      1,
                                      sizeof(cmd_buffer->scissor_words)),
             cmd_buffer->scissor_words,
             sizeof(cmd_buffer->scissor_words));

      emit_state->isp_dbsc = true;
      emit_state->region_clip = true;
   }
}

static void
pvr_setup_triangle_merging_flag(struct pvr_cmd_buffer *const cmd_buffer,
                                struct PVRX(TA_STATE_ISPA) * ispa)
{
   struct pvr_emit_state *const emit_state = &cmd_buffer->state.emit_state;
   struct pvr_ppp_state *const ppp_state = &cmd_buffer->state.ppp_state;
   uint32_t merge_word;
   uint32_t mask;

   pvr_csb_pack (&merge_word, TA_STATE_PDS_SIZEINFO2, size_info) {
      /* Disable for lines or punch-through or for DWD and depth compare
       * always.
       */
      if (ispa->objtype == PVRX(TA_OBJTYPE_LINE) ||
          ispa->passtype == PVRX(TA_PASSTYPE_PUNCH_THROUGH) ||
          (ispa->dwritedisable && ispa->dcmpmode == PVRX(TA_CMPMODE_ALWAYS))) {
         size_info.pds_tri_merge_disable = true;
      }
   }

   pvr_csb_pack (&mask, TA_STATE_PDS_SIZEINFO2, size_info) {
      size_info.pds_tri_merge_disable = true;
   }

   merge_word |= ppp_state->pds.size_info2 & ~mask;

   if (merge_word != ppp_state->pds.size_info2) {
      ppp_state->pds.size_info2 = merge_word;
      emit_state->pds_fragment_stateptr0 = true;
   }
}

static void
pvr_setup_fragment_state_pointers(struct pvr_cmd_buffer *const cmd_buffer,
                                  struct pvr_sub_cmd_gfx *const sub_cmd)
{
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;
   const struct pvr_stage_allocation_descriptor_state *descriptor_shader_state =
      &state->gfx_pipeline->fragment_shader_state.descriptor_state;
   const struct pvr_pds_upload *pds_coeff_program =
      &state->gfx_pipeline->fragment_shader_state.pds_coeff_program;
   const struct pvr_pipeline_stage_state *fragment_state =
      &state->gfx_pipeline->fragment_shader_state.stage_state;
   const struct pvr_physical_device *pdevice = cmd_buffer->device->pdevice;
   struct pvr_emit_state *const emit_state = &state->emit_state;
   struct pvr_ppp_state *const ppp_state = &state->ppp_state;

   const uint32_t pds_uniform_size =
      DIV_ROUND_UP(descriptor_shader_state->pds_info.data_size_in_dwords,
                   PVRX(TA_STATE_PDS_SIZEINFO1_PDS_UNIFORMSIZE_UNIT_SIZE));

   const uint32_t pds_varying_state_size =
      DIV_ROUND_UP(pds_coeff_program->data_size,
                   PVRX(TA_STATE_PDS_SIZEINFO1_PDS_VARYINGSIZE_UNIT_SIZE));

   const uint32_t usc_varying_size =
      DIV_ROUND_UP(fragment_state->coefficient_size,
                   PVRX(TA_STATE_PDS_SIZEINFO1_USC_VARYINGSIZE_UNIT_SIZE));

   const uint32_t pds_temp_size =
      DIV_ROUND_UP(fragment_state->temps_count,
                   PVRX(TA_STATE_PDS_SIZEINFO1_PDS_TEMPSIZE_UNIT_SIZE));

   const uint32_t usc_shared_size =
      DIV_ROUND_UP(fragment_state->const_shared_reg_count,
                   PVRX(TA_STATE_PDS_SIZEINFO2_USC_SHAREDSIZE_UNIT_SIZE));

   const uint32_t max_tiles_in_flight =
      pvr_calc_fscommon_size_and_tiles_in_flight(
         pdevice,
         usc_shared_size *
            PVRX(TA_STATE_PDS_SIZEINFO2_USC_SHAREDSIZE_UNIT_SIZE),
         1);
   uint32_t size_info_mask;
   uint32_t size_info2;

   if (max_tiles_in_flight < sub_cmd->max_tiles_in_flight)
      sub_cmd->max_tiles_in_flight = max_tiles_in_flight;

   pvr_csb_pack (&ppp_state->pds.pixel_shader_base,
                 TA_STATE_PDS_SHADERBASE,
                 shader_base) {
      const struct pvr_pds_upload *const pds_upload =
         &state->gfx_pipeline->fragment_shader_state.pds_fragment_program;

      shader_base.addr = PVR_DEV_ADDR(pds_upload->data_offset);
   }

   if (descriptor_shader_state->pds_code.pvr_bo) {
      pvr_csb_pack (&ppp_state->pds.texture_uniform_code_base,
                    TA_STATE_PDS_TEXUNICODEBASE,
                    tex_base) {
         tex_base.addr =
            PVR_DEV_ADDR(descriptor_shader_state->pds_code.code_offset);
      }
   } else {
      ppp_state->pds.texture_uniform_code_base = 0U;
   }

   pvr_csb_pack (&ppp_state->pds.size_info1, TA_STATE_PDS_SIZEINFO1, info1) {
      info1.pds_uniformsize = pds_uniform_size;
      info1.pds_texturestatesize = 0U;
      info1.pds_varyingsize = pds_varying_state_size;
      info1.usc_varyingsize = usc_varying_size;
      info1.pds_tempsize = pds_temp_size;
   }

   pvr_csb_pack (&size_info_mask, TA_STATE_PDS_SIZEINFO2, mask) {
      mask.pds_tri_merge_disable = true;
   }

   ppp_state->pds.size_info2 &= size_info_mask;

   pvr_csb_pack (&size_info2, TA_STATE_PDS_SIZEINFO2, info2) {
      info2.usc_sharedsize = usc_shared_size;
   }

   ppp_state->pds.size_info2 |= size_info2;

   if (pds_coeff_program->pvr_bo) {
      state->emit_state.pds_fragment_stateptr1 = true;

      pvr_csb_pack (&ppp_state->pds.varying_base,
                    TA_STATE_PDS_VARYINGBASE,
                    base) {
         base.addr = PVR_DEV_ADDR(pds_coeff_program->data_offset);
      }
   } else {
      ppp_state->pds.varying_base = 0U;
   }

   pvr_csb_pack (&ppp_state->pds.uniform_state_data_base,
                 TA_STATE_PDS_UNIFORMDATABASE,
                 base) {
      base.addr = PVR_DEV_ADDR(state->pds_fragment_descriptor_data_offset);
   }

   emit_state->pds_fragment_stateptr0 = true;
   emit_state->pds_fragment_stateptr3 = true;
}

static void pvr_setup_viewport(struct pvr_cmd_buffer *const cmd_buffer)
{
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;
   struct pvr_emit_state *const emit_state = &state->emit_state;
   struct pvr_ppp_state *const ppp_state = &state->ppp_state;

   if (ppp_state->viewport_count != state->dynamic.common.viewport.count) {
      ppp_state->viewport_count = state->dynamic.common.viewport.count;
      emit_state->viewport = true;
   }

   if (state->gfx_pipeline->raster_state.discard_enable) {
      /* We don't want to emit any viewport data as it'll just get thrown
       * away. It's after the previous condition because we still want to
       * stash the viewport_count as it's our trigger for when
       * rasterizer discard gets disabled.
       */
      emit_state->viewport = false;
      return;
   }

   for (uint32_t i = 0; i < ppp_state->viewport_count; i++) {
      VkViewport *viewport = &state->dynamic.common.viewport.viewports[i];
      uint32_t x_scale = fui(viewport->width * 0.5f);
      uint32_t y_scale = fui(viewport->height * 0.5f);
      uint32_t z_scale = fui(viewport->maxDepth - viewport->minDepth);
      uint32_t x_center = fui(viewport->x + viewport->width * 0.5f);
      uint32_t y_center = fui(viewport->y + viewport->height * 0.5f);
      uint32_t z_center = fui(viewport->minDepth);

      if (ppp_state->viewports[i].a0 != x_center ||
          ppp_state->viewports[i].m0 != x_scale ||
          ppp_state->viewports[i].a1 != y_center ||
          ppp_state->viewports[i].m1 != y_scale ||
          ppp_state->viewports[i].a2 != z_center ||
          ppp_state->viewports[i].m2 != z_scale) {
         ppp_state->viewports[i].a0 = x_center;
         ppp_state->viewports[i].m0 = x_scale;
         ppp_state->viewports[i].a1 = y_center;
         ppp_state->viewports[i].m1 = y_scale;
         ppp_state->viewports[i].a2 = z_center;
         ppp_state->viewports[i].m2 = z_scale;

         emit_state->viewport = true;
      }
   }
}

static void pvr_setup_ppp_control(struct pvr_cmd_buffer *const cmd_buffer)
{
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;
   const struct pvr_graphics_pipeline *const gfx_pipeline = state->gfx_pipeline;
   struct pvr_emit_state *const emit_state = &state->emit_state;
   struct pvr_ppp_state *const ppp_state = &state->ppp_state;
   uint32_t ppp_control;

   pvr_csb_pack (&ppp_control, TA_STATE_PPP_CTRL, control) {
      const struct pvr_raster_state *raster_state = &gfx_pipeline->raster_state;
      VkPrimitiveTopology topology = gfx_pipeline->input_asm_state.topology;
      control.drawclippededges = true;
      control.wclampen = true;

      if (topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN)
         control.flatshade_vtx = PVRX(TA_FLATSHADE_VTX_VERTEX_1);
      else
         control.flatshade_vtx = PVRX(TA_FLATSHADE_VTX_VERTEX_0);

      if (raster_state->depth_clamp_enable)
         control.clip_mode = PVRX(TA_CLIP_MODE_NO_FRONT_OR_REAR);
      else
         control.clip_mode = PVRX(TA_CLIP_MODE_FRONT_REAR);

      /* +--- FrontIsCCW?
       * | +--- Cull Front?
       * v v
       * 0|0 CULLMODE_CULL_CCW,
       * 0|1 CULLMODE_CULL_CW,
       * 1|0 CULLMODE_CULL_CW,
       * 1|1 CULLMODE_CULL_CCW,
       */
      switch (raster_state->cull_mode) {
      case VK_CULL_MODE_BACK_BIT:
      case VK_CULL_MODE_FRONT_BIT:
         if ((raster_state->front_face == VK_FRONT_FACE_COUNTER_CLOCKWISE) ^
             (raster_state->cull_mode == VK_CULL_MODE_FRONT_BIT)) {
            control.cullmode = PVRX(TA_CULLMODE_CULL_CW);
         } else {
            control.cullmode = PVRX(TA_CULLMODE_CULL_CCW);
         }

         break;

      case VK_CULL_MODE_FRONT_AND_BACK:
      case VK_CULL_MODE_NONE:
         control.cullmode = PVRX(TA_CULLMODE_NO_CULLING);
         break;

      default:
         unreachable("Unsupported cull mode!");
      }
   }

   if (ppp_control != ppp_state->ppp_control) {
      ppp_state->ppp_control = ppp_control;
      emit_state->ppp_control = true;
   }
}

/* Largest valid PPP State update in words = 31
 * 1 - Header
 * 3 - Stream Out Config words 0, 1 and 2
 * 1 - PPP Control word
 * 3 - Varying Config words 0, 1 and 2
 * 1 - Output Select
 * 1 - WClamp
 * 6 - Viewport Transform words
 * 2 - Region Clip words
 * 3 - PDS State for fragment phase (PDSSTATEPTR 1-3)
 * 4 - PDS State for fragment phase (PDSSTATEPTR0)
 * 6 - ISP Control Words
 */
#define PVR_MAX_PPP_STATE_DWORDS 31

static VkResult pvr_emit_ppp_state(struct pvr_cmd_buffer *const cmd_buffer,
                                   struct pvr_sub_cmd_gfx *const sub_cmd)
{
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;
   struct pvr_emit_state *const emit_state = &state->emit_state;
   struct pvr_ppp_state *const ppp_state = &state->ppp_state;
   struct pvr_csb *const control_stream = &sub_cmd->control_stream;
   uint32_t ppp_state_words[PVR_MAX_PPP_STATE_DWORDS];
   uint32_t ppp_state_words_count;
   uint32_t ppp_state_header;
   bool deferred_secondary;
   struct pvr_bo *pvr_bo;
   uint32_t *buffer_ptr;
   VkResult result;

   buffer_ptr = ppp_state_words;

   pvr_csb_pack (&ppp_state_header, TA_STATE_HEADER, header) {
      header.view_port_count = (ppp_state->viewport_count == 0)
                                  ? 0U
                                  : (ppp_state->viewport_count - 1);

      /* Skip over header. */
      buffer_ptr++;

      /* Set ISP state. */
      if (emit_state->isp) {
         header.pres_ispctl = true;
         *buffer_ptr++ = ppp_state->isp.control;
         header.pres_ispctl_fa = true;
         *buffer_ptr++ = ppp_state->isp.front_a;

         if (emit_state->isp_fb) {
            header.pres_ispctl_fb = true;
            *buffer_ptr++ = ppp_state->isp.front_b;
         }

         if (emit_state->isp_ba) {
            header.pres_ispctl_ba = true;
            *buffer_ptr++ = ppp_state->isp.back_a;
         }

         if (emit_state->isp_bb) {
            header.pres_ispctl_bb = true;
            *buffer_ptr++ = ppp_state->isp.back_b;
         }
      }

      /* Depth bias / scissor
       * If deferred_secondary is true then we do a separate state update
       * which gets patched in ExecuteDeferredCommandBuffer.
       */
      /* TODO: Update above comment when we port ExecuteDeferredCommandBuffer.
       */
      deferred_secondary =
         cmd_buffer->vk.level == VK_COMMAND_BUFFER_LEVEL_SECONDARY &&
         cmd_buffer->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

      if (emit_state->isp_dbsc && !deferred_secondary) {
         header.pres_ispctl_dbsc = true;

         pvr_csb_pack (buffer_ptr++, TA_STATE_ISPDBSC, ispdbsc) {
            ispdbsc.dbindex =
               ppp_state->depthbias_scissor_indices.depthbias_index;
            ispdbsc.scindex =
               ppp_state->depthbias_scissor_indices.scissor_index;
         }
      }

      /* PDS state. */
      if (emit_state->pds_fragment_stateptr0) {
         header.pres_pds_state_ptr0 = true;

         *buffer_ptr++ = ppp_state->pds.pixel_shader_base;
         *buffer_ptr++ = ppp_state->pds.texture_uniform_code_base;
         *buffer_ptr++ = ppp_state->pds.size_info1;
         *buffer_ptr++ = ppp_state->pds.size_info2;
      }

      if (emit_state->pds_fragment_stateptr1) {
         header.pres_pds_state_ptr1 = true;
         *buffer_ptr++ = ppp_state->pds.varying_base;
      }

      /* We don't use the pds_fragment_stateptr2 (texture state programs)
       * control word, but this doesn't mean we need to set it to 0. This is
       * because the hardware runs the texture state program only when the
       * pds_texture state field of PDS_SIZEINFO1 is non-zero.
       */

      if (emit_state->pds_fragment_stateptr3) {
         header.pres_pds_state_ptr3 = true;
         *buffer_ptr++ = ppp_state->pds.uniform_state_data_base;
      }

      /* Region clip. */
      if (emit_state->region_clip) {
         header.pres_region_clip = true;
         *buffer_ptr++ = ppp_state->region_clipping.word0;
         *buffer_ptr++ = ppp_state->region_clipping.word1;
      }

      /* Viewport. */
      if (emit_state->viewport) {
         const uint32_t viewports = MAX2(1, ppp_state->viewport_count);

         header.pres_viewport = true;
         for (uint32_t i = 0; i < viewports; i++) {
            *buffer_ptr++ = ppp_state->viewports[i].a0;
            *buffer_ptr++ = ppp_state->viewports[i].m0;
            *buffer_ptr++ = ppp_state->viewports[i].a1;
            *buffer_ptr++ = ppp_state->viewports[i].m1;
            *buffer_ptr++ = ppp_state->viewports[i].a2;
            *buffer_ptr++ = ppp_state->viewports[i].m2;
         }
      }

      /* W clamp. */
      if (emit_state->wclamp) {
         const float wclamp = 0.00001f;

         header.pres_wclamp = true;
         *buffer_ptr++ = fui(wclamp);
      }

      /* Output selects. */
      if (emit_state->output_selects) {
         header.pres_outselects = true;
         *buffer_ptr++ = ppp_state->output_selects;
      }

      /* Varying words. */
      if (emit_state->varying_word0) {
         header.pres_varying_word0 = true;
         *buffer_ptr++ = ppp_state->varying_word[0];
      }

      if (emit_state->varying_word1) {
         header.pres_varying_word1 = true;
         *buffer_ptr++ = ppp_state->varying_word[1];
      }

      if (emit_state->varying_word2) {
         /* We only emit this on the first draw of a render job to prevent us
          * from inheriting a non-zero value set elsewhere.
          */
         header.pres_varying_word2 = true;
         *buffer_ptr++ = 0;
      }

      /* PPP control. */
      if (emit_state->ppp_control) {
         header.pres_ppp_ctrl = true;
         *buffer_ptr++ = ppp_state->ppp_control;
      }

      if (emit_state->stream_out) {
         /* We only emit this on the first draw of a render job to prevent us
          * from inheriting a non-zero value set elsewhere.
          */
         header.pres_stream_out_size = true;
         *buffer_ptr++ = 0;
      }
   }

   if (!ppp_state_header)
      return VK_SUCCESS;

   ppp_state_words_count = buffer_ptr - ppp_state_words;
   ppp_state_words[0] = ppp_state_header;

   result = pvr_cmd_buffer_alloc_mem(cmd_buffer,
                                     cmd_buffer->device->heaps.general_heap,
                                     ppp_state_words_count * sizeof(uint32_t),
                                     PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                                     &pvr_bo);
   if (result != VK_SUCCESS)
      return result;

   memcpy(pvr_bo->bo->map,
          ppp_state_words,
          ppp_state_words_count * sizeof(uint32_t));

   /* Write the VDM state update into the VDM control stream. */
   pvr_csb_emit (control_stream, VDMCTRL_PPP_STATE0, state0) {
      state0.word_count = ppp_state_words_count;
      state0.addrmsb = pvr_bo->vma->dev_addr;
   }

   pvr_csb_emit (control_stream, VDMCTRL_PPP_STATE1, state1) {
      state1.addrlsb = pvr_bo->vma->dev_addr;
   }

   if (emit_state->isp_dbsc &&
       cmd_buffer->vk.level == VK_COMMAND_BUFFER_LEVEL_SECONDARY) {
      pvr_finishme("Unimplemented path!!");
   }

   state->emit_state_bits = 0;

   return VK_SUCCESS;
}

static VkResult
pvr_emit_dirty_ppp_state(struct pvr_cmd_buffer *const cmd_buffer,
                         struct pvr_sub_cmd_gfx *const sub_cmd)
{
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;
   const struct pvr_graphics_pipeline *const gfx_pipeline = state->gfx_pipeline;
   const bool dirty_stencil = state->dirty.compare_mask ||
                              state->dirty.write_mask || state->dirty.reference;
   VkResult result;

   if (!(dirty_stencil || state->dirty.depth_bias ||
         state->dirty.fragment_descriptors || state->dirty.line_width ||
         state->dirty.gfx_pipeline_binding || state->dirty.scissor ||
         state->dirty.userpass_spawn || state->dirty.viewport ||
         state->emit_state_bits)) {
      return VK_SUCCESS;
   }

   if (state->dirty.gfx_pipeline_binding) {
      struct PVRX(TA_STATE_ISPA) ispa;

      pvr_setup_output_select(cmd_buffer);
      pvr_setup_isp_faces_and_control(cmd_buffer, &ispa);
      pvr_setup_triangle_merging_flag(cmd_buffer, &ispa);
   } else if (dirty_stencil || state->dirty.line_width ||
              state->dirty.userpass_spawn) {
      pvr_setup_isp_faces_and_control(cmd_buffer, NULL);
   }

   if (!gfx_pipeline->raster_state.discard_enable &&
       state->dirty.fragment_descriptors &&
       gfx_pipeline->fragment_shader_state.bo) {
      pvr_setup_fragment_state_pointers(cmd_buffer, sub_cmd);
   }

   pvr_setup_isp_depth_bias_scissor_state(cmd_buffer);

   if (state->dirty.viewport)
      pvr_setup_viewport(cmd_buffer);

   pvr_setup_ppp_control(cmd_buffer);

   /* The hardware doesn't have an explicit mode for this so we use a
    * negative viewport to make sure all objects are culled out early.
    */
   if (gfx_pipeline->raster_state.cull_mode == VK_CULL_MODE_FRONT_AND_BACK) {
      /* Shift the viewport out of the guard-band culling everything. */
      const uint32_t negative_vp_val = fui(-2.0f);

      state->ppp_state.viewports[0].a0 = negative_vp_val;
      state->ppp_state.viewports[0].m0 = 0;
      state->ppp_state.viewports[0].a1 = negative_vp_val;
      state->ppp_state.viewports[0].m1 = 0;
      state->ppp_state.viewports[0].a2 = negative_vp_val;
      state->ppp_state.viewports[0].m2 = 0;

      state->ppp_state.viewport_count = 1;

      state->emit_state.viewport = true;
   }

   result = pvr_emit_ppp_state(cmd_buffer, sub_cmd);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}

void pvr_calculate_vertex_cam_size(const struct pvr_device_info *dev_info,
                                   const uint32_t vs_output_size,
                                   const bool raster_enable,
                                   uint32_t *const cam_size_out,
                                   uint32_t *const vs_max_instances_out)
{
   /* First work out the size of a vertex in the UVS and multiply by 4 for
    * column ordering.
    */
   const uint32_t uvs_vertex_vector_size_in_dwords =
      (vs_output_size + 1U + raster_enable * 4U) * 4U;
   const uint32_t vdm_cam_size =
      PVR_GET_FEATURE_VALUE(dev_info, vdm_cam_size, 32U);

   /* This is a proxy for 8XE. */
   if (PVR_HAS_FEATURE(dev_info, simple_internal_parameter_format) &&
       vdm_cam_size < 96U) {
      /* Comparisons are based on size including scratch per vertex vector. */
      if (uvs_vertex_vector_size_in_dwords < (14U * 4U)) {
         *cam_size_out = MIN2(31U, vdm_cam_size - 1U);
         *vs_max_instances_out = 16U;
      } else if (uvs_vertex_vector_size_in_dwords < (20U * 4U)) {
         *cam_size_out = 15U;
         *vs_max_instances_out = 16U;
      } else if (uvs_vertex_vector_size_in_dwords < (28U * 4U)) {
         *cam_size_out = 11U;
         *vs_max_instances_out = 12U;
      } else if (uvs_vertex_vector_size_in_dwords < (44U * 4U)) {
         *cam_size_out = 7U;
         *vs_max_instances_out = 8U;
      } else if (PVR_HAS_FEATURE(dev_info,
                                 simple_internal_parameter_format_v2) ||
                 uvs_vertex_vector_size_in_dwords < (64U * 4U)) {
         *cam_size_out = 7U;
         *vs_max_instances_out = 4U;
      } else {
         *cam_size_out = 3U;
         *vs_max_instances_out = 2U;
      }
   } else {
      /* Comparisons are based on size including scratch per vertex vector. */
      if (uvs_vertex_vector_size_in_dwords <= (32U * 4U)) {
         /* output size <= 27 + 5 scratch. */
         *cam_size_out = MIN2(95U, vdm_cam_size - 1U);
         *vs_max_instances_out = 0U;
      } else if (uvs_vertex_vector_size_in_dwords <= 48U * 4U) {
         /* output size <= 43 + 5 scratch */
         *cam_size_out = 63U;
         if (PVR_GET_FEATURE_VALUE(dev_info, uvs_vtx_entries, 144U) < 288U)
            *vs_max_instances_out = 16U;
         else
            *vs_max_instances_out = 0U;
      } else if (uvs_vertex_vector_size_in_dwords <= 64U * 4U) {
         /* output size <= 59 + 5 scratch. */
         *cam_size_out = 31U;
         if (PVR_GET_FEATURE_VALUE(dev_info, uvs_vtx_entries, 144U) < 288U)
            *vs_max_instances_out = 16U;
         else
            *vs_max_instances_out = 0U;
      } else {
         *cam_size_out = 15U;
         *vs_max_instances_out = 16U;
      }
   }
}

static void
pvr_emit_dirty_vdm_state(const struct pvr_cmd_buffer *const cmd_buffer,
                         struct pvr_sub_cmd_gfx *const sub_cmd)
{
   /* FIXME: Assume all state is dirty for the moment. */
   struct pvr_device_info *const dev_info =
      &cmd_buffer->device->pdevice->dev_info;
   ASSERTED const uint32_t max_user_vertex_output_components =
      pvr_get_max_user_vertex_output_components(dev_info);
   struct PVRX(VDMCTRL_VDM_STATE0)
      header = { pvr_cmd_header(VDMCTRL_VDM_STATE0) };
   const struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;
   const struct pvr_graphics_pipeline *const gfx_pipeline = state->gfx_pipeline;
   struct pvr_csb *const csb = &sub_cmd->control_stream;
   uint32_t vs_output_size;
   uint32_t max_instances;
   uint32_t cam_size;

   assert(gfx_pipeline);

   /* CAM Calculations and HW state take vertex size aligned to DWORDS. */
   vs_output_size =
      DIV_ROUND_UP(gfx_pipeline->vertex_shader_state.vertex_output_size,
                   PVRX(VDMCTRL_VDM_STATE4_VS_OUTPUT_SIZE_UNIT_SIZE));

   assert(vs_output_size <= max_user_vertex_output_components);

   pvr_calculate_vertex_cam_size(dev_info,
                                 vs_output_size,
                                 true,
                                 &cam_size,
                                 &max_instances);

   pvr_csb_emit (csb, VDMCTRL_VDM_STATE0, state0) {
      state0.cam_size = cam_size;

      if (gfx_pipeline->input_asm_state.primitive_restart) {
         state0.cut_index_enable = true;
         state0.cut_index_present = true;
      }

      switch (gfx_pipeline->input_asm_state.topology) {
      case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
         state0.flatshade_control = PVRX(VDMCTRL_FLATSHADE_CONTROL_VERTEX_1);
         break;

      default:
         state0.flatshade_control = PVRX(VDMCTRL_FLATSHADE_CONTROL_VERTEX_0);
         break;
      }

      /* If we've bound a different vertex buffer, or this draw-call requires
       * a different PDS attrib data-section from the last draw call (changed
       * base_instance) then we need to specify a new data section. This is
       * also the case if we've switched pipeline or attrib program as the
       * data-section layout will be different.
       */
      state0.vs_data_addr_present =
         state->dirty.gfx_pipeline_binding || state->dirty.vertex_bindings ||
         state->dirty.draw_base_instance || state->dirty.draw_variant;

      /* Need to specify new PDS Attrib program if we've bound a different
       * pipeline or we needed a different PDS Attrib variant for this
       * draw-call.
       */
      state0.vs_other_present = state->dirty.gfx_pipeline_binding ||
                                state->dirty.draw_variant;

      /* UVB_SCRATCH_SELECT_ONE with no rasterization is only valid when
       * stream output is enabled. We use UVB_SCRATCH_SELECT_FIVE because
       * Vulkan doesn't support stream output and the vertex position is
       * always emitted to the UVB.
       */
      state0.uvs_scratch_size_select =
         PVRX(VDMCTRL_UVS_SCRATCH_SIZE_SELECT_FIVE);

      header = state0;
   }

   if (header.cut_index_present) {
      pvr_csb_emit (csb, VDMCTRL_VDM_STATE1, state1) {
         switch (state->index_buffer_binding.type) {
         case VK_INDEX_TYPE_UINT32:
            /* FIXME: Defines for these? These seem to come from the Vulkan
             * spec. for VkPipelineInputAssemblyStateCreateInfo
             * primitiveRestartEnable.
             */
            state1.cut_index = 0xFFFFFFFF;
            break;

         case VK_INDEX_TYPE_UINT16:
            state1.cut_index = 0xFFFF;
            break;

         default:
            unreachable(!"Invalid index type");
         }
      }
   }

   if (header.vs_data_addr_present) {
      pvr_csb_emit (csb, VDMCTRL_VDM_STATE2, state2) {
         state2.vs_pds_data_base_addr =
            PVR_DEV_ADDR(state->pds_vertex_attrib_offset);
      }
   }

   if (header.vs_other_present) {
      const uint32_t usc_unified_store_size_in_bytes =
         gfx_pipeline->vertex_shader_state.vertex_input_size << 2;

      pvr_csb_emit (csb, VDMCTRL_VDM_STATE3, state3) {
         state3.vs_pds_code_base_addr =
            PVR_DEV_ADDR(state->pds_shader.code_offset);
      }

      pvr_csb_emit (csb, VDMCTRL_VDM_STATE4, state4) {
         state4.vs_output_size = vs_output_size;
      }

      pvr_csb_emit (csb, VDMCTRL_VDM_STATE5, state5) {
         state5.vs_max_instances = max_instances;
         state5.vs_usc_common_size = 0U;
         state5.vs_usc_unified_size = DIV_ROUND_UP(
            usc_unified_store_size_in_bytes,
            PVRX(VDMCTRL_VDM_STATE5_VS_USC_UNIFIED_SIZE_UNIT_SIZE));
         state5.vs_pds_temp_size =
            DIV_ROUND_UP(state->pds_shader.info->temps_required << 2,
                         PVRX(VDMCTRL_VDM_STATE5_VS_PDS_TEMP_SIZE_UNIT_SIZE));
         state5.vs_pds_data_size =
            DIV_ROUND_UP(state->pds_shader.info->data_size_in_dwords << 2,
                         PVRX(VDMCTRL_VDM_STATE5_VS_PDS_DATA_SIZE_UNIT_SIZE));
      }
   }
}

static VkResult pvr_validate_draw_state(struct pvr_cmd_buffer *cmd_buffer)
{
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;
   const struct pvr_graphics_pipeline *const gfx_pipeline = state->gfx_pipeline;
   const struct pvr_pipeline_layout *const pipeline_layout =
      gfx_pipeline->base.layout;
   const struct pvr_pipeline_stage_state *const fragment_state =
      &gfx_pipeline->fragment_shader_state.stage_state;
   struct pvr_sub_cmd_gfx *sub_cmd;
   bool fstencil_writemask_zero;
   bool bstencil_writemask_zero;
   bool fstencil_keep;
   bool bstencil_keep;
   VkResult result;

   pvr_cmd_buffer_start_sub_cmd(cmd_buffer, PVR_SUB_CMD_TYPE_GRAPHICS);

   sub_cmd = &state->current_sub_cmd->gfx;
   sub_cmd->empty_cmd = false;

   /* Determine pipeline depth/stencil usage. If a pipeline uses depth or
    * stencil testing, those attachments are using their loaded values, and
    * the loadOps cannot be optimized out.
    */
   /* Pipeline uses depth testing. */
   if (sub_cmd->depth_usage == PVR_DEPTH_STENCIL_USAGE_UNDEFINED &&
       gfx_pipeline->depth_compare_op != VK_COMPARE_OP_ALWAYS) {
      sub_cmd->depth_usage = PVR_DEPTH_STENCIL_USAGE_NEEDED;
   }

   /* Pipeline uses stencil testing. */
   if (sub_cmd->stencil_usage == PVR_DEPTH_STENCIL_USAGE_UNDEFINED &&
       (gfx_pipeline->stencil_front.compare_op != VK_COMPARE_OP_ALWAYS ||
        gfx_pipeline->stencil_back.compare_op != VK_COMPARE_OP_ALWAYS)) {
      sub_cmd->stencil_usage = PVR_DEPTH_STENCIL_USAGE_NEEDED;
   }

   if (PVR_HAS_FEATURE(&cmd_buffer->device->pdevice->dev_info,
                       compute_overlap)) {
      uint32_t coefficient_size =
         DIV_ROUND_UP(fragment_state->coefficient_size,
                      PVRX(TA_STATE_PDS_SIZEINFO1_USC_VARYINGSIZE_UNIT_SIZE));

      if (coefficient_size >
          PVRX(TA_STATE_PDS_SIZEINFO1_USC_VARYINGSIZE_MAX_SIZE))
         sub_cmd->disable_compute_overlap = true;
   }

   sub_cmd->frag_uses_atomic_ops |= fragment_state->uses_atomic_ops;
   sub_cmd->frag_has_side_effects |= fragment_state->has_side_effects;
   sub_cmd->frag_uses_texture_rw |= fragment_state->uses_texture_rw;
   sub_cmd->vertex_uses_texture_rw |=
      gfx_pipeline->vertex_shader_state.stage_state.uses_texture_rw;

   fstencil_keep =
      (gfx_pipeline->stencil_front.fail_op == VK_STENCIL_OP_KEEP) &&
      (gfx_pipeline->stencil_front.pass_op == VK_STENCIL_OP_KEEP);
   bstencil_keep = (gfx_pipeline->stencil_back.fail_op == VK_STENCIL_OP_KEEP) &&
                   (gfx_pipeline->stencil_back.pass_op == VK_STENCIL_OP_KEEP);
   fstencil_writemask_zero = (state->dynamic.common.write_mask.front == 0);
   bstencil_writemask_zero = (state->dynamic.common.write_mask.back == 0);

   /* Set stencil modified flag if:
    * - Neither front nor back-facing stencil has a fail_op/pass_op of KEEP.
    * - Neither front nor back-facing stencil has a write_mask of zero.
    */
   if (!(fstencil_keep && bstencil_keep) &&
       !(fstencil_writemask_zero && bstencil_writemask_zero)) {
      sub_cmd->modifies_stencil = true;
   }

   /* Set depth modified flag if depth write is enabled. */
   if (!gfx_pipeline->depth_write_disable)
      sub_cmd->modifies_depth = true;

   /* If either the data or code changes for pds vertex attribs, regenerate the
    * data segment.
    */
   if (state->dirty.vertex_bindings || state->dirty.gfx_pipeline_binding ||
       state->dirty.draw_variant || state->dirty.draw_base_instance) {
      enum pvr_pds_vertex_attrib_program_type prog_type;
      const struct pvr_pds_attrib_program *program;

      if (state->draw_state.draw_indirect)
         prog_type = PVR_PDS_VERTEX_ATTRIB_PROGRAM_DRAW_INDIRECT;
      else if (state->draw_state.base_instance)
         prog_type = PVR_PDS_VERTEX_ATTRIB_PROGRAM_BASE_INSTANCE;
      else
         prog_type = PVR_PDS_VERTEX_ATTRIB_PROGRAM_BASIC;

      program =
         &gfx_pipeline->vertex_shader_state.pds_attrib_programs[prog_type];
      state->pds_shader.info = &program->info;
      state->pds_shader.code_offset = program->program.code_offset;

      state->max_shared_regs =
         MAX2(state->max_shared_regs, pvr_calc_shared_regs_count(gfx_pipeline));

      pvr_setup_vertex_buffers(cmd_buffer, gfx_pipeline);
   }

   /* TODO: Check for dirty push constants */

   state->dirty.vertex_descriptors = state->dirty.gfx_pipeline_binding;
   state->dirty.fragment_descriptors = state->dirty.vertex_descriptors;

   /* Account for dirty descriptor set. */
   state->dirty.vertex_descriptors |=
      state->dirty.gfx_desc_dirty &&
      pipeline_layout
         ->per_stage_descriptor_masks[PVR_STAGE_ALLOCATION_VERTEX_GEOMETRY];
   state->dirty.fragment_descriptors |=
      state->dirty.gfx_desc_dirty &&
      pipeline_layout->per_stage_descriptor_masks[PVR_STAGE_ALLOCATION_FRAGMENT];

   state->dirty.fragment_descriptors |= state->dirty.blend_constants;

   if (state->dirty.fragment_descriptors) {
      result = pvr_setup_descriptor_mappings(
         cmd_buffer,
         PVR_STAGE_ALLOCATION_FRAGMENT,
         &state->gfx_pipeline->fragment_shader_state.descriptor_state,
         NULL,
         &state->pds_fragment_descriptor_data_offset);
      if (result != VK_SUCCESS) {
         mesa_loge("Could not setup fragment descriptor mappings.");
         return result;
      }
   }

   if (state->dirty.vertex_descriptors) {
      uint32_t pds_vertex_descriptor_data_offset;

      result = pvr_setup_descriptor_mappings(
         cmd_buffer,
         PVR_STAGE_ALLOCATION_VERTEX_GEOMETRY,
         &state->gfx_pipeline->vertex_shader_state.descriptor_state,
         NULL,
         &pds_vertex_descriptor_data_offset);
      if (result != VK_SUCCESS) {
         mesa_loge("Could not setup vertex descriptor mappings.");
         return result;
      }

      pvr_emit_dirty_pds_state(cmd_buffer,
                               sub_cmd,
                               pds_vertex_descriptor_data_offset);
   }

   pvr_emit_dirty_ppp_state(cmd_buffer, sub_cmd);
   pvr_emit_dirty_vdm_state(cmd_buffer, sub_cmd);

   state->dirty.gfx_desc_dirty = false;
   state->dirty.blend_constants = false;
   state->dirty.compare_mask = false;
   state->dirty.depth_bias = false;
   state->dirty.draw_base_instance = false;
   state->dirty.draw_variant = false;
   state->dirty.fragment_descriptors = false;
   state->dirty.line_width = false;
   state->dirty.gfx_pipeline_binding = false;
   state->dirty.reference = false;
   state->dirty.scissor = false;
   state->dirty.userpass_spawn = false;
   state->dirty.vertex_bindings = false;
   state->dirty.viewport = false;
   state->dirty.write_mask = false;

   return VK_SUCCESS;
}

static uint32_t pvr_get_hw_primitive_topology(VkPrimitiveTopology topology)
{
   switch (topology) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return PVRX(VDMCTRL_PRIMITIVE_TOPOLOGY_POINT_LIST);
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
      return PVRX(VDMCTRL_PRIMITIVE_TOPOLOGY_LINE_LIST);
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      return PVRX(VDMCTRL_PRIMITIVE_TOPOLOGY_LINE_STRIP);
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
      return PVRX(VDMCTRL_PRIMITIVE_TOPOLOGY_TRI_LIST);
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      return PVRX(VDMCTRL_PRIMITIVE_TOPOLOGY_TRI_STRIP);
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      return PVRX(VDMCTRL_PRIMITIVE_TOPOLOGY_TRI_FAN);
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
      return PVRX(VDMCTRL_PRIMITIVE_TOPOLOGY_LINE_LIST_ADJ);
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
      return PVRX(VDMCTRL_PRIMITIVE_TOPOLOGY_LINE_STRIP_ADJ);
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
      return PVRX(VDMCTRL_PRIMITIVE_TOPOLOGY_TRI_LIST_ADJ);
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
      return PVRX(VDMCTRL_PRIMITIVE_TOPOLOGY_TRI_STRIP_ADJ);
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
      return PVRX(VDMCTRL_PRIMITIVE_TOPOLOGY_PATCH_LIST);
   default:
      unreachable("Undefined primitive topology");
   }
}

/* TODO: Rewrite this in terms of ALIGN_POT() and pvr_cmd_length(). */
/* Aligned to 128 bit for PDS loads / stores */
#define DUMMY_VDM_CONTROL_STREAM_BLOCK_SIZE 8

static VkResult
pvr_write_draw_indirect_vdm_stream(struct pvr_cmd_buffer *cmd_buffer,
                                   struct pvr_csb *const csb,
                                   pvr_dev_addr_t idx_buffer_addr,
                                   uint32_t idx_stride,
                                   struct PVRX(VDMCTRL_INDEX_LIST0) * list_hdr,
                                   struct pvr_buffer *buffer,
                                   VkDeviceSize offset,
                                   uint32_t count,
                                   uint32_t stride)
{
   struct pvr_pds_drawindirect_program pds_prog = { 0 };
   uint32_t word0;

   /* Draw indirect always has index offset and instance count. */
   list_hdr->index_offset_present = true;
   list_hdr->index_instance_count_present = true;

   pvr_cmd_pack(VDMCTRL_INDEX_LIST0)(&word0, list_hdr);

   pds_prog.support_base_instance = true;
   pds_prog.arg_buffer = buffer->dev_addr.addr + offset;
   pds_prog.index_buffer = idx_buffer_addr.addr;
   pds_prog.index_block_header = word0;
   pds_prog.index_stride = idx_stride;
   pds_prog.num_views = 1U;

   /* TODO: See if we can pre-upload the code section of all the pds programs
    * and reuse them here.
    */
   /* Generate and upload the PDS programs (code + data). */
   for (uint32_t i = 0U; i < count; i++) {
      const struct pvr_device_info *dev_info =
         &cmd_buffer->device->pdevice->dev_info;
      struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
      struct pvr_bo *dummy_bo;
      uint32_t *dummy_stream;
      struct pvr_bo *pds_bo;
      uint32_t *pds_base;
      uint32_t pds_size;
      VkResult result;

      pds_prog.increment_draw_id = (i != 0);

      if (state->draw_state.draw_indexed) {
         pvr_pds_generate_draw_elements_indirect(&pds_prog,
                                                 0,
                                                 PDS_GENERATE_SIZES,
                                                 dev_info);
      } else {
         pvr_pds_generate_draw_arrays_indirect(&pds_prog,
                                               0,
                                               PDS_GENERATE_SIZES,
                                               dev_info);
      }

      pds_size = (pds_prog.program.data_size_aligned +
                  pds_prog.program.code_size_aligned)
                 << 2;

      result = pvr_cmd_buffer_alloc_mem(cmd_buffer,
                                        cmd_buffer->device->heaps.pds_heap,
                                        pds_size,
                                        PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                                        &pds_bo);
      if (result != VK_SUCCESS)
         return result;

      pds_base = pds_bo->bo->map;
      memcpy(pds_base,
             pds_prog.program.code,
             pds_prog.program.code_size_aligned << 2);

      if (state->draw_state.draw_indexed) {
         pvr_pds_generate_draw_elements_indirect(
            &pds_prog,
            pds_base + pds_prog.program.code_size_aligned,
            PDS_GENERATE_DATA_SEGMENT,
            dev_info);
      } else {
         pvr_pds_generate_draw_arrays_indirect(
            &pds_prog,
            pds_base + pds_prog.program.code_size_aligned,
            PDS_GENERATE_DATA_SEGMENT,
            dev_info);
      }

      pvr_bo_cpu_unmap(cmd_buffer->device, pds_bo);

      /* Write the VDM state update. */
      pvr_csb_emit (csb, VDMCTRL_PDS_STATE0, state0) {
         state0.usc_target = PVRX(VDMCTRL_USC_TARGET_ANY);

         state0.pds_temp_size =
            DIV_ROUND_UP(pds_prog.program.temp_size_aligned << 2,
                         PVRX(VDMCTRL_PDS_STATE0_PDS_TEMP_SIZE_UNIT_SIZE));

         state0.pds_data_size =
            DIV_ROUND_UP(pds_prog.program.data_size_aligned << 2,
                         PVRX(VDMCTRL_PDS_STATE0_PDS_DATA_SIZE_UNIT_SIZE));
      }

      pvr_csb_emit (csb, VDMCTRL_PDS_STATE1, state1) {
         const uint32_t data_offset =
            pds_bo->vma->dev_addr.addr + (pds_prog.program.code_size << 2) -
            cmd_buffer->device->heaps.pds_heap->base_addr.addr;

         state1.pds_data_addr = PVR_DEV_ADDR(data_offset);
         state1.sd_type = PVRX(VDMCTRL_SD_TYPE_PDS);
         state1.sd_next_type = PVRX(VDMCTRL_SD_TYPE_NONE);
      }

      pvr_csb_emit (csb, VDMCTRL_PDS_STATE2, state2) {
         const uint32_t code_offset =
            pds_bo->vma->dev_addr.addr -
            cmd_buffer->device->heaps.pds_heap->base_addr.addr;

         state2.pds_code_addr = PVR_DEV_ADDR(code_offset);
      }

      /* Sync task to ensure the VDM doesn't start reading the dummy blocks
       * before they are ready.
       */
      pvr_csb_emit (csb, VDMCTRL_INDEX_LIST0, list0) {
         list0.primitive_topology = PVRX(VDMCTRL_PRIMITIVE_TOPOLOGY_TRI_LIST);
      }

      result = pvr_cmd_buffer_alloc_mem(cmd_buffer,
                                        cmd_buffer->device->heaps.general_heap,
                                        DUMMY_VDM_CONTROL_STREAM_BLOCK_SIZE,
                                        PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                                        &dummy_bo);
      if (result != VK_SUCCESS)
         return result;

      dummy_stream = dummy_bo->bo->map;

      /* For indexed draw cmds fill in the dummy's header (as it won't change
       * based on the indirect args) and increment by the in-use size of each
       * dummy block.
       */
      if (!state->draw_state.draw_indexed) {
         dummy_stream[0] = word0;
         dummy_stream += 4;
      } else {
         dummy_stream += 5;
      }

      /* clang-format off */
      pvr_csb_pack (dummy_stream, VDMCTRL_STREAM_RETURN, word);
      /* clang-format on */

      pvr_bo_cpu_unmap(cmd_buffer->device, dummy_bo);

      /* Stream link to the first dummy which forces the VDM to discard any
       * prefetched (dummy) control stream.
       */
      pvr_csb_emit (csb, VDMCTRL_STREAM_LINK0, link) {
         link.with_return = true;
         link.link_addrmsb = dummy_bo->vma->dev_addr;
      }

      pvr_csb_emit (csb, VDMCTRL_STREAM_LINK1, link) {
         link.link_addrlsb = dummy_bo->vma->dev_addr;
      }

      /* Point the pds program to the next argument buffer and the next VDM
       * dummy buffer.
       */
      pds_prog.arg_buffer += stride;
   }

   return VK_SUCCESS;
}

#undef DUMMY_VDM_CONTROL_STREAM_BLOCK_SIZE

static void pvr_emit_vdm_index_list(struct pvr_cmd_buffer *cmd_buffer,
                                    struct pvr_sub_cmd_gfx *const sub_cmd,
                                    VkPrimitiveTopology topology,
                                    uint32_t first_vertex,
                                    uint32_t vertex_count,
                                    uint32_t first_index,
                                    uint32_t index_count,
                                    uint32_t instance_count,
                                    struct pvr_buffer *buffer,
                                    VkDeviceSize offset,
                                    uint32_t count,
                                    uint32_t stride)
{
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   const bool vertex_shader_has_side_effects =
      state->gfx_pipeline->vertex_shader_state.stage_state.has_side_effects;
   struct PVRX(VDMCTRL_INDEX_LIST0)
      list_hdr = { pvr_cmd_header(VDMCTRL_INDEX_LIST0) };
   pvr_dev_addr_t index_buffer_addr = PVR_DEV_ADDR_INVALID;
   struct pvr_csb *const csb = &sub_cmd->control_stream;
   unsigned int index_stride = 0;

   list_hdr.primitive_topology = pvr_get_hw_primitive_topology(topology);

   /* firstInstance is not handled here in the VDM state, it's implemented as
    * an addition in the PDS vertex fetch using
    * PVR_PDS_CONST_MAP_ENTRY_TYPE_BASE_INSTANCE entry type.
    */

   list_hdr.index_count_present = true;

   if (instance_count > 1)
      list_hdr.index_instance_count_present = true;

   if (first_vertex != 0)
      list_hdr.index_offset_present = true;

   if (state->draw_state.draw_indexed) {
      struct pvr_buffer *buffer = state->index_buffer_binding.buffer;

      switch (state->index_buffer_binding.type) {
      case VK_INDEX_TYPE_UINT32:
         list_hdr.index_size = PVRX(VDMCTRL_INDEX_SIZE_B32);
         index_stride = 4;
         break;

      case VK_INDEX_TYPE_UINT16:
         list_hdr.index_size = PVRX(VDMCTRL_INDEX_SIZE_B16);
         index_stride = 2;
         break;

      default:
         unreachable("Invalid index type");
      }

      index_buffer_addr = PVR_DEV_ADDR_OFFSET(
         buffer->dev_addr,
         state->index_buffer_binding.offset + first_index * index_stride);

      list_hdr.index_addr_present = true;

      /* For indirect draw calls, index buffer address is not embedded into VDM
       * control stream.
       */
      if (!state->draw_state.draw_indirect)
         list_hdr.index_base_addrmsb = index_buffer_addr;
   }

   list_hdr.degen_cull_enable =
      PVR_HAS_FEATURE(&cmd_buffer->device->pdevice->dev_info,
                      vdm_degenerate_culling) &&
      !vertex_shader_has_side_effects;

   if (state->draw_state.draw_indirect) {
      assert(buffer);
      pvr_write_draw_indirect_vdm_stream(cmd_buffer,
                                         csb,
                                         index_buffer_addr,
                                         index_stride,
                                         &list_hdr,
                                         buffer,
                                         offset,
                                         count,
                                         stride);
      return;
   }

   pvr_csb_emit (csb, VDMCTRL_INDEX_LIST0, list0) {
      list0 = list_hdr;
   }

   if (list_hdr.index_addr_present) {
      pvr_csb_emit (csb, VDMCTRL_INDEX_LIST1, list1) {
         list1.index_base_addrlsb = index_buffer_addr;
      }
   }

   if (list_hdr.index_count_present) {
      pvr_csb_emit (csb, VDMCTRL_INDEX_LIST2, list2) {
         list2.index_count = vertex_count | index_count;
      }
   }

   if (list_hdr.index_instance_count_present) {
      pvr_csb_emit (csb, VDMCTRL_INDEX_LIST3, list3) {
         list3.instance_count = instance_count - 1;
      }
   }

   if (list_hdr.index_offset_present) {
      pvr_csb_emit (csb, VDMCTRL_INDEX_LIST4, list4) {
         list4.index_offset = first_vertex;
      }
   }

   /* TODO: See if we need list_words[5-9]. */
}

void pvr_CmdDraw(VkCommandBuffer commandBuffer,
                 uint32_t vertexCount,
                 uint32_t instanceCount,
                 uint32_t firstVertex,
                 uint32_t firstInstance)
{
   const struct pvr_cmd_buffer_draw_state draw_state = {
      .base_vertex = firstVertex,
      .base_instance = firstInstance,
   };
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   VkResult result;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   pvr_update_draw_state(state, &draw_state);

   result = pvr_validate_draw_state(cmd_buffer);
   if (result != VK_SUCCESS)
      return;

   /* Write the VDM control stream for the primitive. */
   pvr_emit_vdm_index_list(cmd_buffer,
                           &state->current_sub_cmd->gfx,
                           state->gfx_pipeline->input_asm_state.topology,
                           firstVertex,
                           vertexCount,
                           0U,
                           0U,
                           instanceCount,
                           NULL,
                           0U,
                           0U,
                           0U);
}

void pvr_CmdDrawIndexed(VkCommandBuffer commandBuffer,
                        uint32_t indexCount,
                        uint32_t instanceCount,
                        uint32_t firstIndex,
                        int32_t vertexOffset,
                        uint32_t firstInstance)
{
   const struct pvr_cmd_buffer_draw_state draw_state = {
      .base_vertex = vertexOffset,
      .base_instance = firstInstance,
      .draw_indexed = true,
   };
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   VkResult result;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   pvr_update_draw_state(state, &draw_state);

   result = pvr_validate_draw_state(cmd_buffer);
   if (result != VK_SUCCESS)
      return;

   /* Write the VDM control stream for the primitive. */
   pvr_emit_vdm_index_list(cmd_buffer,
                           &state->current_sub_cmd->gfx,
                           state->gfx_pipeline->input_asm_state.topology,
                           vertexOffset,
                           0,
                           firstIndex,
                           indexCount,
                           instanceCount,
                           NULL,
                           0U,
                           0U,
                           0U);
}

void pvr_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer,
                                VkBuffer _buffer,
                                VkDeviceSize offset,
                                uint32_t drawCount,
                                uint32_t stride)
{
   const struct pvr_cmd_buffer_draw_state draw_state = {
      .draw_indirect = true,
      .draw_indexed = true,
   };
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   PVR_FROM_HANDLE(pvr_buffer, buffer, _buffer);
   VkResult result;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   pvr_update_draw_state(state, &draw_state);

   result = pvr_validate_draw_state(cmd_buffer);
   if (result != VK_SUCCESS)
      return;

   /* Write the VDM control stream for the primitive. */
   pvr_emit_vdm_index_list(cmd_buffer,
                           &state->current_sub_cmd->gfx,
                           state->gfx_pipeline->input_asm_state.topology,
                           0,
                           0,
                           0,
                           0,
                           0,
                           buffer,
                           offset,
                           drawCount,
                           stride);
}

void pvr_CmdDrawIndirect(VkCommandBuffer commandBuffer,
                         VkBuffer _buffer,
                         VkDeviceSize offset,
                         uint32_t drawCount,
                         uint32_t stride)
{
   const struct pvr_cmd_buffer_draw_state draw_state = {
      .draw_indirect = true,
   };
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   PVR_FROM_HANDLE(pvr_buffer, buffer, _buffer);
   VkResult result;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   pvr_update_draw_state(state, &draw_state);

   result = pvr_validate_draw_state(cmd_buffer);
   if (result != VK_SUCCESS)
      return;

   /* Write the VDM control stream for the primitive. */
   pvr_emit_vdm_index_list(cmd_buffer,
                           &state->current_sub_cmd->gfx,
                           state->gfx_pipeline->input_asm_state.topology,
                           0,
                           0,
                           0,
                           0,
                           0,
                           buffer,
                           offset,
                           drawCount,
                           stride);
}

static VkResult
pvr_resolve_unemitted_resolve_attachments(struct pvr_cmd_buffer *cmd_buffer)
{
   pvr_finishme("Add attachment resolve support!");
   return pvr_cmd_buffer_end_sub_cmd(cmd_buffer);
}

void pvr_CmdEndRenderPass2(VkCommandBuffer commandBuffer,
                           const VkSubpassEndInfo *pSubpassEndInfo)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   struct pvr_image_view **attachments;
   VkClearValue *clear_values;
   VkResult result;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   assert(state->render_pass_info.pass);
   assert(state->render_pass_info.framebuffer);

   /* TODO: Investigate why pvr_cmd_buffer_end_sub_cmd/EndSubCommand is called
    * twice in this path, one here and one from
    * pvr_resolve_unemitted_resolve_attachments.
    */
   result = pvr_cmd_buffer_end_sub_cmd(cmd_buffer);
   if (result != VK_SUCCESS)
      return;

   result = pvr_resolve_unemitted_resolve_attachments(cmd_buffer);
   if (result != VK_SUCCESS)
      return;

   /* Save the required fields before clearing render_pass_info struct. */
   attachments = state->render_pass_info.attachments;
   clear_values = state->render_pass_info.clear_values;

   memset(&state->render_pass_info, 0, sizeof(state->render_pass_info));

   state->render_pass_info.attachments = attachments;
   state->render_pass_info.clear_values = clear_values;
}

void pvr_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                            uint32_t commandBufferCount,
                            const VkCommandBuffer *pCommandBuffers)
{
   assert(!"Unimplemented");
}

void pvr_CmdNextSubpass2(VkCommandBuffer commandBuffer,
                         const VkSubpassBeginInfo *pSubpassBeginInfo,
                         const VkSubpassEndInfo *pSubpassEndInfo)
{
   assert(!"Unimplemented");
}

static void pvr_insert_transparent_obj(struct pvr_cmd_buffer *const cmd_buffer,
                                       struct pvr_sub_cmd_gfx *const sub_cmd)
{
   struct pvr_device *const device = cmd_buffer->device;
   /* Yes we want a copy. The user could be recording multiple command buffers
    * in parallel so writing the template in place could cause problems.
    */
   struct pvr_static_clear_ppp_template clear =
      device->static_clear_state.ppp_templates[PVR_STATIC_CLEAR_COLOR_BIT];
   uint32_t pds_state[PVR_STATIC_CLEAR_PDS_STATE_COUNT] = { 0 };
   struct pvr_csb *csb = &sub_cmd->control_stream;
   struct pvr_bo *ppp_bo;

   assert(clear.requires_pds_state);

   /* Patch the template. */

   pvr_csb_pack (&pds_state[PVR_STATIC_CLEAR_PPP_PDS_TYPE_SHADERBASE],
                 TA_STATE_PDS_SHADERBASE,
                 shaderbase) {
      shaderbase.addr = PVR_DEV_ADDR(device->nop_program.pds.data_offset);
   }

   clear.config.pds_state = &pds_state;

   clear.config.ispctl.upass =
      cmd_buffer->state.render_pass_info.userpass_spawn;

   /* Emit PPP state from template. */

   pvr_emit_ppp_from_template(csb, &clear, &ppp_bo);
   list_add(&ppp_bo->link, &cmd_buffer->bo_list);

   /* Emit VDM state. */

   static_assert(sizeof(device->static_clear_state.large_clear_vdm_words) >=
                    PVR_CLEAR_VDM_STATE_DWORD_COUNT * sizeof(uint32_t),
                 "Large clear VDM control stream word length mismatch");
   static_assert(sizeof(device->static_clear_state.vdm_words) ==
                    PVR_CLEAR_VDM_STATE_DWORD_COUNT * sizeof(uint32_t),
                 "Clear VDM control stream word length mismatch");

   pvr_emit_clear_words(cmd_buffer, sub_cmd);

   /* Reset graphics state. */
   pvr_reset_graphics_dirty_state(&cmd_buffer->state, false);
}

static inline struct pvr_render_subpass *
pvr_get_current_subpass(const struct pvr_cmd_buffer_state *const state)
{
   const uint32_t subpass_idx = state->render_pass_info.subpass_idx;

   return &state->render_pass_info.pass->subpasses[subpass_idx];
}

static bool
pvr_stencil_has_self_dependency(const struct pvr_cmd_buffer_state *const state)
{
   const struct pvr_render_subpass *const current_subpass =
      pvr_get_current_subpass(state);
   const uint32_t *const input_attachments = current_subpass->input_attachments;

   /* We only need to check the current software subpass as we don't support
    * merging to/from a subpass with self-dep stencil.
    */

   for (uint32_t i = 0; i < current_subpass->input_count; i++) {
      if (input_attachments[i] == *current_subpass->depth_stencil_attachment)
         return true;
   }

   return false;
}

static bool pvr_is_stencil_store_load_needed(
   const struct pvr_cmd_buffer_state *const state,
   VkPipelineStageFlags2 vk_src_stage_mask,
   VkPipelineStageFlags2 vk_dst_stage_mask,
   uint32_t memory_barrier_count,
   const VkMemoryBarrier2 *const memory_barriers,
   uint32_t image_barrier_count,
   const VkImageMemoryBarrier2 *const image_barriers)
{
   const uint32_t fragment_test_stages =
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
   const struct pvr_render_pass *const pass = state->render_pass_info.pass;
   const struct pvr_renderpass_hwsetup_render *hw_render;
   struct pvr_image_view **const attachments =
      state->render_pass_info.attachments;
   const struct pvr_image_view *attachment;
   uint32_t hw_render_idx;

   if (!pass)
      return false;

   hw_render_idx = state->current_sub_cmd->gfx.hw_render_idx;
   hw_render = &pass->hw_setup->renders[hw_render_idx];
   attachment = attachments[hw_render->ds_surface_id];

   if (!(vk_src_stage_mask & fragment_test_stages) &&
       vk_dst_stage_mask & VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
      return false;

   if (hw_render->ds_surface_id == -1)
      return false;

   for (uint32_t i = 0; i < memory_barrier_count; i++) {
      const uint32_t stencil_write_bit =
         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      const uint32_t input_attachment_read_bit =
         VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

      if (!(memory_barriers[i].srcAccessMask & stencil_write_bit))
         continue;

      if (!(memory_barriers[i].dstAccessMask & input_attachment_read_bit))
         continue;

      return pvr_stencil_has_self_dependency(state);
   }

   for (uint32_t i = 0; i < image_barrier_count; i++) {
      PVR_FROM_HANDLE(pvr_image, image, image_barriers[i].image);
      const uint32_t stencil_bit = VK_IMAGE_ASPECT_STENCIL_BIT;

      if (!(image_barriers[i].subresourceRange.aspectMask & stencil_bit))
         continue;

      if (attachment && image != vk_to_pvr_image(attachment->vk.image))
         continue;

      if (!vk_format_has_stencil(image->vk.format))
         continue;

      return pvr_stencil_has_self_dependency(state);
   }

   return false;
}

static void pvr_insert_mid_frag_barrier(struct pvr_cmd_buffer *cmd_buffer)
{
   struct pvr_sub_cmd *const curr_sub_cmd = cmd_buffer->state.current_sub_cmd;

   assert(curr_sub_cmd->type == PVR_SUB_CMD_TYPE_GRAPHICS);

   pvr_finishme("Handle mid frag barrier stencil store.");

   pvr_cmd_buffer_end_sub_cmd(cmd_buffer);
   pvr_cmd_buffer_start_sub_cmd(cmd_buffer, PVR_SUB_CMD_TYPE_GRAPHICS);

   pvr_finishme("Handle mid frag barrier color attachment load.");
}

/* This is just enough to handle vkCmdPipelineBarrier().
 * TODO: Complete?
 */
void pvr_CmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                             const VkDependencyInfo *pDependencyInfo)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *const state = &cmd_buffer->state;
   const struct pvr_render_pass *const render_pass =
      state->render_pass_info.pass;
   VkPipelineStageFlags vk_src_stage_mask = 0U;
   VkPipelineStageFlags vk_dst_stage_mask = 0U;
   bool is_stencil_store_load_needed;
   uint32_t required_stage_mask = 0U;
   uint32_t src_stage_mask;
   uint32_t dst_stage_mask;
   bool is_barrier_needed;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   for (uint32_t i = 0; i < pDependencyInfo->memoryBarrierCount; i++) {
      vk_src_stage_mask |= pDependencyInfo->pMemoryBarriers[i].srcStageMask;
      vk_dst_stage_mask |= pDependencyInfo->pMemoryBarriers[i].dstStageMask;
   }

   for (uint32_t i = 0; i < pDependencyInfo->bufferMemoryBarrierCount; i++) {
      vk_src_stage_mask |=
         pDependencyInfo->pBufferMemoryBarriers[i].srcStageMask;
      vk_dst_stage_mask |=
         pDependencyInfo->pBufferMemoryBarriers[i].dstStageMask;
   }

   for (uint32_t i = 0; i < pDependencyInfo->imageMemoryBarrierCount; i++) {
      vk_src_stage_mask |=
         pDependencyInfo->pImageMemoryBarriers[i].srcStageMask;
      vk_dst_stage_mask |=
         pDependencyInfo->pImageMemoryBarriers[i].dstStageMask;
   }

   src_stage_mask = pvr_stage_mask_src(vk_src_stage_mask);
   dst_stage_mask = pvr_stage_mask_dst(vk_dst_stage_mask);

   for (uint32_t stage = 0U; stage != PVR_NUM_SYNC_PIPELINE_STAGES; stage++) {
      if (!(dst_stage_mask & BITFIELD_BIT(stage)))
         continue;

      required_stage_mask |= state->barriers_needed[stage];
   }

   src_stage_mask &= required_stage_mask;
   for (uint32_t stage = 0U; stage != PVR_NUM_SYNC_PIPELINE_STAGES; stage++) {
      if (!(dst_stage_mask & BITFIELD_BIT(stage)))
         continue;

      state->barriers_needed[stage] &= ~src_stage_mask;
   }

   if (src_stage_mask == 0 || dst_stage_mask == 0) {
      is_barrier_needed = false;
   } else if (src_stage_mask == PVR_PIPELINE_STAGE_GEOM_BIT &&
              dst_stage_mask == PVR_PIPELINE_STAGE_FRAG_BIT) {
      /* This is implicit so no need to barrier. */
      is_barrier_needed = false;
   } else if (src_stage_mask == dst_stage_mask &&
              util_bitcount(src_stage_mask) == 1) {
      struct pvr_sub_cmd *const current_sub_cmd = state->current_sub_cmd;

      switch (src_stage_mask) {
      case PVR_PIPELINE_STAGE_FRAG_BIT:
         is_barrier_needed = true;

         if (!render_pass)
            break;

         assert(current_sub_cmd->type == PVR_SUB_CMD_TYPE_GRAPHICS);

         /* Flush all fragment work up to this point. */
         pvr_insert_transparent_obj(cmd_buffer, &current_sub_cmd->gfx);
         break;

      case PVR_PIPELINE_STAGE_COMPUTE_BIT:
         is_barrier_needed = false;

         if (!current_sub_cmd ||
             current_sub_cmd->type != PVR_SUB_CMD_TYPE_COMPUTE) {
            break;
         }

         /* Multiple dispatches can be merged into a single job. When back to
          * back dispatches have a sequential dependency (CDM -> CDM pipeline
          * barrier) we need to do the following.
          *   - Dispatch a kernel which fences all previous memory writes and
          *     flushes the MADD cache.
          *   - Issue a CDM fence which ensures all previous tasks emitted by
          *     the CDM are completed before starting anything new.
          */

         /* Issue Data Fence, Wait for Data Fence (IDFWDF) makes the PDS wait
          * for data.
          */
         pvr_compute_generate_idfwdf(cmd_buffer, &current_sub_cmd->compute);

         pvr_compute_generate_fence(cmd_buffer,
                                    &current_sub_cmd->compute,
                                    false);
         break;

      default:
         is_barrier_needed = false;
         break;
      };
   } else {
      is_barrier_needed = true;
   }

   is_stencil_store_load_needed =
      pvr_is_stencil_store_load_needed(state,
                                       vk_src_stage_mask,
                                       vk_dst_stage_mask,
                                       pDependencyInfo->memoryBarrierCount,
                                       pDependencyInfo->pMemoryBarriers,
                                       pDependencyInfo->imageMemoryBarrierCount,
                                       pDependencyInfo->pImageMemoryBarriers);

   if (is_stencil_store_load_needed) {
      pvr_insert_mid_frag_barrier(cmd_buffer);
   } else {
      if (is_barrier_needed)
         pvr_finishme("Insert barrier if needed.");
   }
}

void pvr_CmdResetEvent2(VkCommandBuffer commandBuffer,
                        VkEvent _event,
                        VkPipelineStageFlags2 stageMask)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   PVR_FROM_HANDLE(pvr_event, event, _event);
   struct pvr_sub_cmd_event *sub_cmd;
   VkResult result;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   result = pvr_cmd_buffer_start_sub_cmd(cmd_buffer, PVR_SUB_CMD_TYPE_EVENT);
   if (result != VK_SUCCESS)
      return;

   sub_cmd = &cmd_buffer->state.current_sub_cmd->event;

   sub_cmd->type = PVR_EVENT_TYPE_RESET;
   sub_cmd->reset.event = event;
   sub_cmd->reset.wait_for_stage_mask = pvr_stage_mask_src(stageMask);

   pvr_cmd_buffer_end_sub_cmd(cmd_buffer);
}

void pvr_CmdSetEvent2(VkCommandBuffer commandBuffer,
                      VkEvent _event,
                      const VkDependencyInfo *pDependencyInfo)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   PVR_FROM_HANDLE(pvr_event, event, _event);
   VkPipelineStageFlags2 stage_mask = 0;
   struct pvr_sub_cmd_event *sub_cmd;
   VkResult result;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   result = pvr_cmd_buffer_start_sub_cmd(cmd_buffer, PVR_SUB_CMD_TYPE_EVENT);
   if (result != VK_SUCCESS)
      return;

   for (uint32_t i = 0; i < pDependencyInfo->memoryBarrierCount; i++)
      stage_mask |= pDependencyInfo->pMemoryBarriers[i].srcStageMask;

   for (uint32_t i = 0; i < pDependencyInfo->bufferMemoryBarrierCount; i++)
      stage_mask |= pDependencyInfo->pBufferMemoryBarriers[i].srcStageMask;

   for (uint32_t i = 0; i < pDependencyInfo->imageMemoryBarrierCount; i++)
      stage_mask |= pDependencyInfo->pImageMemoryBarriers[i].srcStageMask;

   sub_cmd = &cmd_buffer->state.current_sub_cmd->event;

   sub_cmd->type = PVR_EVENT_TYPE_SET;
   sub_cmd->set.event = event;
   sub_cmd->set.wait_for_stage_mask = pvr_stage_mask_dst(stage_mask);

   pvr_cmd_buffer_end_sub_cmd(cmd_buffer);
}

void pvr_CmdWaitEvents2(VkCommandBuffer commandBuffer,
                        uint32_t eventCount,
                        const VkEvent *pEvents,
                        const VkDependencyInfo *pDependencyInfos)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_sub_cmd_event *sub_cmd;
   struct pvr_event **events_array;
   uint32_t *stage_masks;
   VkResult result;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   VK_MULTIALLOC(ma);
   vk_multialloc_add(&ma, &events_array, __typeof__(*events_array), eventCount);
   vk_multialloc_add(&ma, &stage_masks, __typeof__(*stage_masks), eventCount);

   if (!vk_multialloc_alloc(&ma,
                            &cmd_buffer->vk.pool->alloc,
                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT)) {
      cmd_buffer->state.status =
         vk_error(cmd_buffer, VK_ERROR_OUT_OF_HOST_MEMORY);
      return;
   }

   result = pvr_cmd_buffer_start_sub_cmd(cmd_buffer, PVR_SUB_CMD_TYPE_EVENT);
   if (result != VK_SUCCESS) {
      vk_free(&cmd_buffer->vk.pool->alloc, events_array);
      return;
   }

   memcpy(events_array, pEvents, sizeof(*events_array) * eventCount);

   for (uint32_t i = 0; i < eventCount; i++) {
      const VkDependencyInfo *info = &pDependencyInfos[i];
      VkPipelineStageFlags2 mask = 0;

      for (uint32_t j = 0; j < info->memoryBarrierCount; j++)
         mask |= info->pMemoryBarriers[j].dstStageMask;

      for (uint32_t j = 0; j < info->bufferMemoryBarrierCount; j++)
         mask |= info->pBufferMemoryBarriers[j].dstStageMask;

      for (uint32_t j = 0; j < info->imageMemoryBarrierCount; j++)
         mask |= info->pImageMemoryBarriers[j].dstStageMask;

      stage_masks[i] = pvr_stage_mask_dst(mask);
   }

   sub_cmd = &cmd_buffer->state.current_sub_cmd->event;

   sub_cmd->type = PVR_EVENT_TYPE_WAIT;
   sub_cmd->wait.count = eventCount;
   sub_cmd->wait.events = events_array;
   sub_cmd->wait.wait_at_stage_masks = stage_masks;

   pvr_cmd_buffer_end_sub_cmd(cmd_buffer);
}

void pvr_CmdWriteTimestamp2KHR(VkCommandBuffer commandBuffer,
                               VkPipelineStageFlags2 stage,
                               VkQueryPool queryPool,
                               uint32_t query)
{
   unreachable("Timestamp queries are not supported.");
}

VkResult pvr_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   struct pvr_cmd_buffer_state *state = &cmd_buffer->state;
   VkResult result;

   /* From the Vulkan 1.0 spec:
    *
    * CommandBuffer must be in the recording state.
    */
   assert(cmd_buffer->status == PVR_CMD_BUFFER_STATUS_RECORDING);

   if (state->status != VK_SUCCESS)
      return state->status;

   result = pvr_cmd_buffer_end_sub_cmd(cmd_buffer);
   if (result != VK_SUCCESS)
      return result;

   cmd_buffer->status = PVR_CMD_BUFFER_STATUS_EXECUTABLE;

   return VK_SUCCESS;
}
