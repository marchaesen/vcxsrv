/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on v3dv driver which is:
 * Copyright © 2019 Raspberry Pi
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
#include <string.h>
#include <vulkan/vulkan.h>

#include "compiler/shader_enums.h"
#include "hwdef/rogue_hw_utils.h"
#include "nir/nir.h"
#include "pco/pco.h"
#include "pco/pco_data.h"
#include "pvr_bo.h"
#include "pvr_csb.h"
#include "pvr_csb_enum_helpers.h"
#include "pvr_hardcode.h"
#include "pvr_nir.h"
#include "pvr_pds.h"
#include "pvr_private.h"
#include "pvr_robustness.h"
#include "pvr_shader.h"
#include "pvr_types.h"
#include "rogue/rogue.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/ralloc.h"
#include "util/u_dynarray.h"
#include "util/u_math.h"
#include "vk_alloc.h"
#include "vk_format.h"
#include "vk_graphics_state.h"
#include "vk_log.h"
#include "vk_object.h"
#include "vk_pipeline_cache.h"
#include "vk_render_pass.h"
#include "vk_util.h"
#include "vulkan/runtime/vk_pipeline.h"

/*****************************************************************************
   PDS functions
*****************************************************************************/

/* If allocator == NULL, the internal one will be used. */
static VkResult pvr_pds_coeff_program_create_and_upload(
   struct pvr_device *device,
   const VkAllocationCallbacks *allocator,
   struct pvr_pds_coeff_loading_program *program,
   struct pvr_fragment_shader_state *fragment_state)
{
   uint32_t staging_buffer_size;
   uint32_t *staging_buffer;
   VkResult result;

   assert(program->num_fpu_iterators < PVR_MAXIMUM_ITERATIONS);

   /* Get the size of the program and then allocate that much memory. */
   pvr_pds_coefficient_loading(program, NULL, PDS_GENERATE_SIZES);

   if (!program->code_size) {
      fragment_state->pds_coeff_program.pvr_bo = NULL;
      fragment_state->pds_coeff_program.code_size = 0;
      fragment_state->pds_coeff_program.data_size = 0;
      fragment_state->stage_state.pds_temps_count = 0;

      return VK_SUCCESS;
   }

   staging_buffer_size =
      PVR_DW_TO_BYTES(program->code_size + program->data_size);

   staging_buffer = vk_alloc2(&device->vk.alloc,
                              allocator,
                              staging_buffer_size,
                              8,
                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Generate the program into is the staging_buffer. */
   pvr_pds_coefficient_loading(program,
                               staging_buffer,
                               PDS_GENERATE_CODEDATA_SEGMENTS);

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               &staging_buffer[0],
                               program->data_size,
                               16,
                               &staging_buffer[program->data_size],
                               program->code_size,
                               16,
                               16,
                               &fragment_state->pds_coeff_program);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, allocator, staging_buffer);
      return result;
   }

   vk_free2(&device->vk.alloc, allocator, staging_buffer);

   fragment_state->stage_state.pds_temps_count = program->temps_used;

   return VK_SUCCESS;
}

/* FIXME: move this elsewhere since it's also called in pvr_pass.c? */
/* If allocator == NULL, the internal one will be used. */
VkResult pvr_pds_fragment_program_create_and_upload(
   struct pvr_device *device,
   const VkAllocationCallbacks *allocator,
   pco_shader *fs,
   struct pvr_fragment_shader_state *fragment_state)
{
   /* TODO: remove the below + revert the pvr_pds_setup_doutu
    * args and make sure fs isn't NULL instead;
    * temporarily in place for hardcoded load ops in
    * pvr_pass.c:pvr_generate_load_op_shader()
    */
   unsigned temps = 0;
   bool has_phase_rate_change = false;
   unsigned entry_offset = 0;

   if (fs) {
      pco_data *fs_data = pco_shader_data(fs);
      temps = fs_data->common.temps;
      has_phase_rate_change = fs_data->fs.uses.phase_change;
      entry_offset = fs_data->common.entry_offset;
   }

   struct pvr_pds_kickusc_program program = { 0 };
   uint32_t staging_buffer_size;
   uint32_t *staging_buffer;
   VkResult result;

   const pvr_dev_addr_t exec_addr =
      PVR_DEV_ADDR_OFFSET(fragment_state->bo->dev_addr,
                          /* fs_data->common.entry_offset */ entry_offset);

   /* Note this is not strictly required to be done before calculating the
    * staging_buffer_size in this particular case. It can also be done after
    * allocating the buffer. The size from pvr_pds_kick_usc() is constant.
    */
   pvr_pds_setup_doutu(
      &program.usc_task_control,
      exec_addr.addr,
      /* fs_data->common.temps */ temps,
      fragment_state->sample_rate,
      /* fs_data->fs.uses.phase_change */ has_phase_rate_change);

   pvr_pds_kick_usc(&program, NULL, 0, false, PDS_GENERATE_SIZES);

   staging_buffer_size = PVR_DW_TO_BYTES(program.code_size + program.data_size);

   staging_buffer = vk_alloc2(&device->vk.alloc,
                              allocator,
                              staging_buffer_size,
                              8,
                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   pvr_pds_kick_usc(&program,
                    staging_buffer,
                    0,
                    false,
                    PDS_GENERATE_CODEDATA_SEGMENTS);

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               &staging_buffer[0],
                               program.data_size,
                               16,
                               &staging_buffer[program.data_size],
                               program.code_size,
                               16,
                               16,
                               &fragment_state->pds_fragment_program);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, allocator, staging_buffer);
      return result;
   }

   vk_free2(&device->vk.alloc, allocator, staging_buffer);

   return VK_SUCCESS;
}

static inline size_t pvr_pds_get_max_vertex_program_const_map_size_in_bytes(
   const struct pvr_device_info *dev_info,
   bool robust_buffer_access)
{
   /* FIXME: Use more local variable to improve formatting. */

   /* Maximum memory allocation needed for const map entries in
    * pvr_pds_generate_vertex_primary_program().
    * When robustBufferAccess is disabled, it must be >= 410.
    * When robustBufferAccess is enabled, it must be >= 570.
    *
    * 1. Size of entry for base instance
    *        (pvr_const_map_entry_base_instance)
    *
    * 2. Max. number of vertex inputs (PVR_MAX_VERTEX_INPUT_BINDINGS) * (
    *     if (!robustBufferAccess)
    *         size of vertex attribute entry
    *             (pvr_const_map_entry_vertex_attribute_address) +
    *     else
    *         size of robust vertex attribute entry
    *             (pvr_const_map_entry_robust_vertex_attribute_address) +
    *         size of entry for max attribute index
    *             (pvr_const_map_entry_vertex_attribute_max_index) +
    *     fi
    *     size of Unified Store burst entry
    *         (pvr_const_map_entry_literal32) +
    *     size of entry for vertex stride
    *         (pvr_const_map_entry_literal32) +
    *     size of entries for DDMAD control word
    *         (num_ddmad_literals * pvr_const_map_entry_literal32))
    *
    * 3. Size of entry for DOUTW vertex/instance control word
    *     (pvr_const_map_entry_literal32)
    *
    * 4. Size of DOUTU entry (pvr_const_map_entry_doutu_address)
    */

   const size_t attribute_size =
      (!robust_buffer_access)
         ? sizeof(struct pvr_const_map_entry_vertex_attribute_address)
         : sizeof(struct pvr_const_map_entry_robust_vertex_attribute_address) +
              sizeof(struct pvr_const_map_entry_vertex_attribute_max_index);

   /* If has_pds_ddmadt the DDMAD control word is now a DDMADT control word
    * and is increased by one DWORD to contain the data for the DDMADT's
    * out-of-bounds check.
    */
   const size_t pvr_pds_const_map_vertex_entry_num_ddmad_literals =
      1U + (size_t)PVR_HAS_FEATURE(dev_info, pds_ddmadt);

   return (sizeof(struct pvr_const_map_entry_base_instance) +
           PVR_MAX_VERTEX_INPUT_BINDINGS *
              (attribute_size +
               (2 + pvr_pds_const_map_vertex_entry_num_ddmad_literals) *
                  sizeof(struct pvr_const_map_entry_literal32)) +
           sizeof(struct pvr_const_map_entry_literal32) +
           sizeof(struct pvr_const_map_entry_doutu_address));
}

static VkResult pvr_pds_vertex_attrib_program_create_and_upload(
   struct pvr_device *const device,
   const VkAllocationCallbacks *const allocator,
   struct pvr_pds_vertex_primary_program_input *const input,
   struct pvr_pds_attrib_program *const program_out)
{
   const size_t const_entries_size_in_bytes =
      pvr_pds_get_max_vertex_program_const_map_size_in_bytes(
         &device->pdevice->dev_info,
         device->vk.enabled_features.robustBufferAccess);
   struct pvr_pds_upload *const program = &program_out->program;
   struct pvr_pds_info *const info = &program_out->info;
   struct pvr_const_map_entry *new_entries;
   ASSERTED uint32_t code_size_in_dwords;
   size_t staging_buffer_size;
   uint32_t *staging_buffer;
   VkResult result;

   memset(info, 0, sizeof(*info));

   info->entries = vk_alloc2(&device->vk.alloc,
                             allocator,
                             const_entries_size_in_bytes,
                             8,
                             VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!info->entries) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_out;
   }

   info->entries_size_in_bytes = const_entries_size_in_bytes;

   pvr_pds_generate_vertex_primary_program(
      input,
      NULL,
      info,
      device->vk.enabled_features.robustBufferAccess,
      &device->pdevice->dev_info);

   code_size_in_dwords = info->code_size_in_dwords;
   staging_buffer_size = PVR_DW_TO_BYTES(info->code_size_in_dwords);

   staging_buffer = vk_alloc2(&device->vk.alloc,
                              allocator,
                              staging_buffer_size,
                              8,
                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_entries;
   }

   /* This also fills in info->entries. */
   pvr_pds_generate_vertex_primary_program(
      input,
      staging_buffer,
      info,
      device->vk.enabled_features.robustBufferAccess,
      &device->pdevice->dev_info);

   assert(info->code_size_in_dwords <= code_size_in_dwords);

   /* FIXME: Add a vk_realloc2() ? */
   new_entries = vk_realloc((!allocator) ? &device->vk.alloc : allocator,
                            info->entries,
                            info->entries_written_size_in_bytes,
                            8,
                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!new_entries) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_staging_buffer;
   }

   info->entries = new_entries;
   info->entries_size_in_bytes = info->entries_written_size_in_bytes;

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               NULL,
                               0,
                               0,
                               staging_buffer,
                               info->code_size_in_dwords,
                               16,
                               16,
                               program);
   if (result != VK_SUCCESS)
      goto err_free_staging_buffer;

   vk_free2(&device->vk.alloc, allocator, staging_buffer);

   return VK_SUCCESS;

err_free_staging_buffer:
   vk_free2(&device->vk.alloc, allocator, staging_buffer);

err_free_entries:
   vk_free2(&device->vk.alloc, allocator, info->entries);

err_out:
   return result;
}

static inline void pvr_pds_vertex_attrib_program_destroy(
   struct pvr_device *const device,
   const struct VkAllocationCallbacks *const allocator,
   struct pvr_pds_attrib_program *const program)
{
   pvr_bo_suballoc_free(program->program.pvr_bo);
   vk_free2(&device->vk.alloc, allocator, program->info.entries);
}

/* This is a const pointer to an array of pvr_pds_attrib_program structs.
 * The array being pointed to is of PVR_PDS_VERTEX_ATTRIB_PROGRAM_COUNT size.
 */
typedef struct pvr_pds_attrib_program (*const pvr_pds_attrib_programs_array_ptr)
   [PVR_PDS_VERTEX_ATTRIB_PROGRAM_COUNT];

/* Generate and uploads a PDS program for DMAing vertex attribs into USC vertex
 * inputs. This will bake the code segment and create a template of the data
 * segment for the command buffer to fill in.
 */
/* If allocator == NULL, the internal one will be used.
 *
 * programs_out_ptr is a pointer to the array where the outputs will be placed.
 */
static VkResult pvr_pds_vertex_attrib_programs_create_and_upload(
   struct pvr_device *device,
   const VkAllocationCallbacks *const allocator,
   pco_data *shader_data,
   const struct pvr_pds_vertex_dma
      dma_descriptions[static const PVR_MAX_VERTEX_ATTRIB_DMAS],
   uint32_t dma_count,
   pvr_pds_attrib_programs_array_ptr programs_out_ptr)
{
   struct pvr_pds_vertex_primary_program_input input = {
      .dma_list = dma_descriptions,
      .dma_count = dma_count,
   };
   uint32_t usc_temp_count = shader_data->common.temps;
   struct pvr_pds_attrib_program *const programs_out = *programs_out_ptr;
   VkResult result;

   pco_range *sys_vals = shader_data->common.sys_vals;
   if (sys_vals[SYSTEM_VALUE_VERTEX_ID].count > 0) {
      input.flags |= PVR_PDS_VERTEX_FLAGS_VERTEX_ID_REQUIRED;
      input.vertex_id_register = sys_vals[SYSTEM_VALUE_VERTEX_ID].start;
   }

   if (sys_vals[SYSTEM_VALUE_INSTANCE_ID].count > 0) {
      input.flags |= PVR_PDS_VERTEX_FLAGS_INSTANCE_ID_REQUIRED;
      input.instance_id_register = sys_vals[SYSTEM_VALUE_INSTANCE_ID].start;
   }

   if (sys_vals[SYSTEM_VALUE_BASE_INSTANCE].count > 0) {
      input.flags |= PVR_PDS_VERTEX_FLAGS_BASE_INSTANCE_REQUIRED;
      input.base_instance_register = sys_vals[SYSTEM_VALUE_BASE_INSTANCE].start;
   }

   if (sys_vals[SYSTEM_VALUE_BASE_VERTEX].count > 0) {
      input.flags |= PVR_PDS_VERTEX_FLAGS_BASE_VERTEX_REQUIRED;
      input.base_vertex_register = sys_vals[SYSTEM_VALUE_BASE_VERTEX].start;
   }

   if (sys_vals[SYSTEM_VALUE_DRAW_ID].count > 0) {
      input.flags |= PVR_PDS_VERTEX_FLAGS_DRAW_INDEX_REQUIRED;
      input.draw_index_register = sys_vals[SYSTEM_VALUE_DRAW_ID].start;
   }

   pvr_pds_setup_doutu(&input.usc_task_control,
                       0,
                       usc_temp_count,
                       ROGUE_PDSINST_DOUTU_SAMPLE_RATE_INSTANCE,
                       false);

   /* Note: programs_out_ptr is a pointer to an array so this is fine. See the
    * typedef.
    */
   for (uint32_t i = 0; i < ARRAY_SIZE(*programs_out_ptr); i++) {
      uint32_t extra_flags;

      switch (i) {
      case PVR_PDS_VERTEX_ATTRIB_PROGRAM_BASIC:
         extra_flags = 0;
         break;

      case PVR_PDS_VERTEX_ATTRIB_PROGRAM_BASE_INSTANCE:
         extra_flags = PVR_PDS_VERTEX_FLAGS_BASE_INSTANCE_VARIANT;
         break;

      case PVR_PDS_VERTEX_ATTRIB_PROGRAM_DRAW_INDIRECT:
         extra_flags = PVR_PDS_VERTEX_FLAGS_DRAW_INDIRECT_VARIANT;
         break;

      default:
         unreachable("Invalid vertex attrib program type.");
      }

      input.flags |= extra_flags;

      result =
         pvr_pds_vertex_attrib_program_create_and_upload(device,
                                                         allocator,
                                                         &input,
                                                         &programs_out[i]);
      if (result != VK_SUCCESS) {
         for (uint32_t j = 0; j < i; j++) {
            pvr_pds_vertex_attrib_program_destroy(device,
                                                  allocator,
                                                  &programs_out[j]);
         }

         return result;
      }

      input.flags &= ~extra_flags;
   }

   return VK_SUCCESS;
}

size_t pvr_pds_get_max_descriptor_upload_const_map_size_in_bytes(void)
{
   /* Maximum memory allocation needed for const map entries in
    * pvr_pds_generate_descriptor_upload_program().
    * It must be >= 688 bytes. This size is calculated as the sum of:
    *
    *  1. Max. number of descriptor sets (8) * (
    *         size of descriptor entry
    *             (pvr_const_map_entry_descriptor_set) +
    *         size of Common Store burst entry
    *             (pvr_const_map_entry_literal32))
    *
    *  2. Max. number of PDS program buffers (24) * (
    *         size of the largest buffer structure
    *             (pvr_const_map_entry_constant_buffer) +
    *         size of Common Store burst entry
    *             (pvr_const_map_entry_literal32)
    *
    *  3. Size of DOUTU entry (pvr_const_map_entry_doutu_address)
    *
    *  4. Max. number of PDS address literals (8) * (
    *         size of entry
    *             (pvr_const_map_entry_descriptor_set_addrs_table)
    *
    *  5. Max. number of address literals with single buffer entry to DOUTD
              size of entry
                  (pvr_pds_const_map_entry_addr_literal_buffer) +
              8 * size of entry (pvr_pds_const_map_entry_addr_literal)
    */

   /* FIXME: PVR_MAX_DESCRIPTOR_SETS is 4 and not 8. The comment above seems to
    * say that it should be 8.
    * Figure our a define for this or is the comment wrong?
    */
   return (8 * (sizeof(struct pvr_const_map_entry_descriptor_set) +
                sizeof(struct pvr_const_map_entry_literal32)) +
           PVR_PDS_MAX_BUFFERS *
              (sizeof(struct pvr_const_map_entry_constant_buffer) +
               sizeof(struct pvr_const_map_entry_literal32)) +
           sizeof(struct pvr_const_map_entry_doutu_address) +
           sizeof(struct pvr_pds_const_map_entry_addr_literal_buffer) +
           8 * sizeof(struct pvr_pds_const_map_entry_addr_literal));
}

static VkResult pvr_pds_descriptor_program_create_and_upload(
   struct pvr_device *const device,
   const VkAllocationCallbacks *const allocator,
   const struct pvr_pipeline_layout *const layout,
   enum pvr_stage_allocation stage,
   const struct pvr_sh_reg_layout *sh_reg_layout,
   struct pvr_stage_allocation_descriptor_state *const descriptor_state)
{
   const size_t const_entries_size_in_bytes =
      pvr_pds_get_max_descriptor_upload_const_map_size_in_bytes();
   struct pvr_pds_info *const pds_info = &descriptor_state->pds_info;
   struct pvr_pds_descriptor_program_input program = { 0 };
   struct pvr_const_map_entry *new_entries;
   ASSERTED uint32_t code_size_in_dwords;
   uint32_t staging_buffer_size;
   uint32_t addr_literals = 0;
   uint32_t *staging_buffer;
   VkResult result;

   assert(stage != PVR_STAGE_ALLOCATION_COUNT);

   *pds_info = (struct pvr_pds_info){ 0 };

   if (sh_reg_layout->descriptor_set_addrs_table.present) {
      program.addr_literals[addr_literals] = (struct pvr_pds_addr_literal){
         .type = PVR_PDS_ADDR_LITERAL_DESC_SET_ADDRS_TABLE,
         .destination = sh_reg_layout->descriptor_set_addrs_table.offset,
      };
      addr_literals++;
   }

   if (sh_reg_layout->push_consts.present) {
      program.addr_literals[addr_literals] = (struct pvr_pds_addr_literal){
         .type = PVR_PDS_ADDR_LITERAL_PUSH_CONSTS,
         .destination = sh_reg_layout->push_consts.offset,
      };
      addr_literals++;
   }

   if (sh_reg_layout->blend_consts.present) {
      program.addr_literals[addr_literals] = (struct pvr_pds_addr_literal){
         .type = PVR_PDS_ADDR_LITERAL_BLEND_CONSTANTS,
         .destination = sh_reg_layout->blend_consts.offset,
      };
      addr_literals++;
   }

   program.addr_literal_count = addr_literals;

   pds_info->entries = vk_alloc2(&device->vk.alloc,
                                 allocator,
                                 const_entries_size_in_bytes,
                                 8,
                                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pds_info->entries) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_static_consts;
   }

   pds_info->entries_size_in_bytes = const_entries_size_in_bytes;

   pvr_pds_generate_descriptor_upload_program(&program, NULL, pds_info);

   code_size_in_dwords = pds_info->code_size_in_dwords;
   staging_buffer_size = PVR_DW_TO_BYTES(pds_info->code_size_in_dwords);

   if (!staging_buffer_size) {
      vk_free2(&device->vk.alloc, allocator, pds_info->entries);

      *descriptor_state = (struct pvr_stage_allocation_descriptor_state){ 0 };

      return VK_SUCCESS;
   }

   staging_buffer = vk_alloc2(&device->vk.alloc,
                              allocator,
                              staging_buffer_size,
                              8,
                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_entries;
   }

   pvr_pds_generate_descriptor_upload_program(&program,
                                              staging_buffer,
                                              pds_info);

   assert(pds_info->code_size_in_dwords <= code_size_in_dwords);

   /* FIXME: use vk_realloc2() ? */
   new_entries = vk_realloc((!allocator) ? &device->vk.alloc : allocator,
                            pds_info->entries,
                            pds_info->entries_written_size_in_bytes,
                            8,
                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!new_entries) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_staging_buffer;
   }

   pds_info->entries = new_entries;
   pds_info->entries_size_in_bytes = pds_info->entries_written_size_in_bytes;

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               NULL,
                               0,
                               0,
                               staging_buffer,
                               pds_info->code_size_in_dwords,
                               16,
                               16,
                               &descriptor_state->pds_code);
   if (result != VK_SUCCESS)
      goto err_free_staging_buffer;

   vk_free2(&device->vk.alloc, allocator, staging_buffer);

   return VK_SUCCESS;

err_free_staging_buffer:
   vk_free2(&device->vk.alloc, allocator, staging_buffer);

err_free_entries:
   vk_free2(&device->vk.alloc, allocator, pds_info->entries);

err_free_static_consts:
   pvr_bo_suballoc_free(descriptor_state->static_consts);

   return result;
}

static void pvr_pds_descriptor_program_destroy(
   struct pvr_device *const device,
   const struct VkAllocationCallbacks *const allocator,
   struct pvr_stage_allocation_descriptor_state *const descriptor_state)
{
   if (!descriptor_state)
      return;

   pvr_bo_suballoc_free(descriptor_state->pds_code.pvr_bo);
   vk_free2(&device->vk.alloc, allocator, descriptor_state->pds_info.entries);
   pvr_bo_suballoc_free(descriptor_state->static_consts);
}

static void pvr_pds_compute_program_setup(
   const struct pvr_device_info *dev_info,
   const uint32_t local_input_regs[static const PVR_WORKGROUP_DIMENSIONS],
   const uint32_t work_group_input_regs[static const PVR_WORKGROUP_DIMENSIONS],
   uint32_t barrier_coefficient,
   bool add_base_workgroup,
   uint32_t usc_temps,
   pvr_dev_addr_t usc_shader_dev_addr,
   struct pvr_pds_compute_shader_program *const program)
{
   pvr_pds_compute_shader_program_init(program);
   program->local_input_regs[0] = local_input_regs[0];
   program->local_input_regs[1] = local_input_regs[1];
   program->local_input_regs[2] = local_input_regs[2];
   program->work_group_input_regs[0] = work_group_input_regs[0];
   program->work_group_input_regs[1] = work_group_input_regs[1];
   program->work_group_input_regs[2] = work_group_input_regs[2];
   program->barrier_coefficient = barrier_coefficient;
   program->add_base_workgroup = add_base_workgroup;
   program->flattened_work_groups = true;
   program->kick_usc = true;

   STATIC_ASSERT(ARRAY_SIZE(program->local_input_regs) ==
                 PVR_WORKGROUP_DIMENSIONS);
   STATIC_ASSERT(ARRAY_SIZE(program->work_group_input_regs) ==
                 PVR_WORKGROUP_DIMENSIONS);
   STATIC_ASSERT(ARRAY_SIZE(program->global_input_regs) ==
                 PVR_WORKGROUP_DIMENSIONS);

   pvr_pds_setup_doutu(&program->usc_task_control,
                       usc_shader_dev_addr.addr,
                       usc_temps,
                       ROGUE_PDSINST_DOUTU_SAMPLE_RATE_INSTANCE,
                       false);

   pvr_pds_compute_shader(program, NULL, PDS_GENERATE_SIZES, dev_info);
}

/* FIXME: See if pvr_device_init_compute_pds_program() and this could be merged.
 */
static VkResult pvr_pds_compute_program_create_and_upload(
   struct pvr_device *const device,
   const VkAllocationCallbacks *const allocator,
   const uint32_t local_input_regs[static const PVR_WORKGROUP_DIMENSIONS],
   const uint32_t work_group_input_regs[static const PVR_WORKGROUP_DIMENSIONS],
   uint32_t barrier_coefficient,
   uint32_t usc_temps,
   pvr_dev_addr_t usc_shader_dev_addr,
   struct pvr_pds_upload *const pds_upload_out,
   struct pvr_pds_info *const pds_info_out)
{
   struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   struct pvr_pds_compute_shader_program program;
   uint32_t staging_buffer_size;
   uint32_t *staging_buffer;
   VkResult result;

   pvr_pds_compute_program_setup(dev_info,
                                 local_input_regs,
                                 work_group_input_regs,
                                 barrier_coefficient,
                                 false,
                                 usc_temps,
                                 usc_shader_dev_addr,
                                 &program);

   /* FIXME: According to pvr_device_init_compute_pds_program() the code size
    * is in bytes. Investigate this.
    */
   staging_buffer_size = PVR_DW_TO_BYTES(program.code_size + program.data_size);

   staging_buffer = vk_alloc2(&device->vk.alloc,
                              allocator,
                              staging_buffer_size,
                              8,
                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* FIXME: pvr_pds_compute_shader doesn't implement
    * PDS_GENERATE_CODEDATA_SEGMENTS.
    */
   pvr_pds_compute_shader(&program,
                          &staging_buffer[0],
                          PDS_GENERATE_CODE_SEGMENT,
                          dev_info);

   pvr_pds_compute_shader(&program,
                          &staging_buffer[program.code_size],
                          PDS_GENERATE_DATA_SEGMENT,
                          dev_info);

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               &staging_buffer[program.code_size],
                               program.data_size,
                               16,
                               &staging_buffer[0],
                               program.code_size,
                               16,
                               16,
                               pds_upload_out);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, allocator, staging_buffer);
      return result;
   }

   *pds_info_out = (struct pvr_pds_info){
      .temps_required = program.highest_temp,
      .code_size_in_dwords = program.code_size,
      .data_size_in_dwords = program.data_size,
   };

   vk_free2(&device->vk.alloc, allocator, staging_buffer);

   return VK_SUCCESS;
};

static void pvr_pds_compute_program_destroy(
   struct pvr_device *const device,
   const struct VkAllocationCallbacks *const allocator,
   struct pvr_pds_upload *const pds_program,
   struct pvr_pds_info *const pds_info)
{
   /* We don't allocate an entries buffer so we don't need to free it */
   pvr_bo_suballoc_free(pds_program->pvr_bo);
}

/* This only uploads the code segment. The data segment will need to be patched
 * with the base workgroup before uploading.
 */
static VkResult pvr_pds_compute_base_workgroup_variant_program_init(
   struct pvr_device *const device,
   const VkAllocationCallbacks *const allocator,
   const uint32_t local_input_regs[static const PVR_WORKGROUP_DIMENSIONS],
   const uint32_t work_group_input_regs[static const PVR_WORKGROUP_DIMENSIONS],
   uint32_t barrier_coefficient,
   uint32_t usc_temps,
   pvr_dev_addr_t usc_shader_dev_addr,
   struct pvr_pds_base_workgroup_program *program_out)
{
   struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   struct pvr_pds_compute_shader_program program;
   uint32_t buffer_size;
   uint32_t *buffer;
   VkResult result;

   pvr_pds_compute_program_setup(dev_info,
                                 local_input_regs,
                                 work_group_input_regs,
                                 barrier_coefficient,
                                 true,
                                 usc_temps,
                                 usc_shader_dev_addr,
                                 &program);

   /* FIXME: According to pvr_device_init_compute_pds_program() the code size
    * is in bytes. Investigate this.
    */
   buffer_size = PVR_DW_TO_BYTES(MAX2(program.code_size, program.data_size));

   buffer = vk_alloc2(&device->vk.alloc,
                      allocator,
                      buffer_size,
                      8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   pvr_pds_compute_shader(&program,
                          &buffer[0],
                          PDS_GENERATE_CODE_SEGMENT,
                          dev_info);

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               NULL,
                               0,
                               0,
                               buffer,
                               program.code_size,
                               16,
                               16,
                               &program_out->code_upload);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, allocator, buffer);
      return result;
   }

   pvr_pds_compute_shader(&program, buffer, PDS_GENERATE_DATA_SEGMENT, dev_info);

   program_out->data_section = buffer;

   /* We'll need to patch the base workgroup in the PDS data section before
    * dispatch so we save the offsets at which to patch. We only need to save
    * the offset for the first workgroup id since the workgroup ids are stored
    * contiguously in the data segment.
    */
   program_out->base_workgroup_data_patching_offset =
      program.base_workgroup_constant_offset_in_dwords[0];

   program_out->info = (struct pvr_pds_info){
      .temps_required = program.highest_temp,
      .code_size_in_dwords = program.code_size,
      .data_size_in_dwords = program.data_size,
   };

   return VK_SUCCESS;
}

static void pvr_pds_compute_base_workgroup_variant_program_finish(
   struct pvr_device *device,
   const VkAllocationCallbacks *const allocator,
   struct pvr_pds_base_workgroup_program *const state)
{
   pvr_bo_suballoc_free(state->code_upload.pvr_bo);
   vk_free2(&device->vk.alloc, allocator, state->data_section);
}

/******************************************************************************
   Generic pipeline functions
 ******************************************************************************/

static void pvr_pipeline_init(struct pvr_device *device,
                              enum pvr_pipeline_type type,
                              struct pvr_pipeline *const pipeline)
{
   assert(!pipeline->layout);

   vk_object_base_init(&device->vk, &pipeline->base, VK_OBJECT_TYPE_PIPELINE);

   pipeline->type = type;
}

static void pvr_pipeline_finish(struct pvr_pipeline *pipeline)
{
   vk_object_base_finish(&pipeline->base);
}

/* How many shared regs it takes to store a pvr_dev_addr_t.
 * Each shared reg is 32 bits.
 */
#define PVR_DEV_ADDR_SIZE_IN_SH_REGS \
   DIV_ROUND_UP(sizeof(pvr_dev_addr_t), sizeof(uint32_t))

/**
 * \brief Allocates shared registers.
 *
 * \return How many sh regs are required.
 */
static uint32_t
pvr_pipeline_alloc_shareds(const struct pvr_device *device,
                           const struct pvr_pipeline_layout *layout,
                           enum pvr_stage_allocation stage,
                           struct pvr_sh_reg_layout *const sh_reg_layout_out)
{
   ASSERTED const uint64_t reserved_shared_size =
      device->pdevice->dev_runtime_info.reserved_shared_size;
   ASSERTED const uint64_t max_coeff =
      device->pdevice->dev_runtime_info.max_coeffs;

   struct pvr_sh_reg_layout reg_layout = { 0 };
   uint32_t next_free_sh_reg = 0;

   reg_layout.descriptor_set_addrs_table.present =
      !!(layout->shader_stage_mask & BITFIELD_BIT(stage));

   if (reg_layout.descriptor_set_addrs_table.present) {
      reg_layout.descriptor_set_addrs_table.offset = next_free_sh_reg;
      next_free_sh_reg += PVR_DEV_ADDR_SIZE_IN_SH_REGS;
   }

   reg_layout.push_consts.present =
      !!(layout->push_constants_shader_stages & BITFIELD_BIT(stage));

   if (reg_layout.push_consts.present) {
      reg_layout.push_consts.offset = next_free_sh_reg;
      next_free_sh_reg += PVR_DEV_ADDR_SIZE_IN_SH_REGS;
   }

   *sh_reg_layout_out = reg_layout;

   /* FIXME: We might need to take more things into consideration.
    * See pvr_calc_fscommon_size_and_tiles_in_flight().
    */
   assert(next_free_sh_reg <= reserved_shared_size - max_coeff);

   return next_free_sh_reg;
}

/******************************************************************************
   Compute pipeline functions
 ******************************************************************************/

/* Compiles and uploads shaders and PDS programs. */
static VkResult pvr_compute_pipeline_compile(
   struct pvr_device *const device,
   struct vk_pipeline_cache *cache,
   const VkComputePipelineCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *const allocator,
   struct pvr_compute_pipeline *const compute_pipeline)
{
   struct pvr_pipeline_layout *layout = compute_pipeline->base.layout;
   struct pvr_sh_reg_layout *sh_reg_layout =
      &layout->sh_reg_layout_per_stage[PVR_STAGE_ALLOCATION_COMPUTE];
   uint32_t work_group_input_regs[PVR_WORKGROUP_DIMENSIONS];
   uint32_t local_input_regs[PVR_WORKGROUP_DIMENSIONS];
   uint32_t barrier_coefficient;
   uint32_t usc_temps;
   uint32_t sh_count;
   VkResult result;

   sh_count = pvr_pipeline_alloc_shareds(device,
                                         layout,
                                         PVR_STAGE_ALLOCATION_COMPUTE,
                                         sh_reg_layout);

   compute_pipeline->shader_state.const_shared_reg_count = sh_count;

   /* FIXME: Compile and upload the shader. */
   /* FIXME: Initialize the shader state and setup build info. */
   unreachable("finishme: compute support");

   result = pvr_pds_descriptor_program_create_and_upload(
      device,
      allocator,
      layout,
      PVR_STAGE_ALLOCATION_COMPUTE,
      sh_reg_layout,
      &compute_pipeline->descriptor_state);
   if (result != VK_SUCCESS)
      goto err_free_shader;

   result = pvr_pds_compute_program_create_and_upload(
      device,
      allocator,
      local_input_regs,
      work_group_input_regs,
      barrier_coefficient,
      usc_temps,
      compute_pipeline->shader_state.bo->dev_addr,
      &compute_pipeline->primary_program,
      &compute_pipeline->primary_program_info);
   if (result != VK_SUCCESS)
      goto err_free_descriptor_program;

   /* If the workgroup ID is required, then we require the base workgroup
    * variant of the PDS compute program as well.
    */
   compute_pipeline->flags.base_workgroup =
      work_group_input_regs[0] != PVR_PDS_REG_UNUSED ||
      work_group_input_regs[1] != PVR_PDS_REG_UNUSED ||
      work_group_input_regs[2] != PVR_PDS_REG_UNUSED;

   if (compute_pipeline->flags.base_workgroup) {
      result = pvr_pds_compute_base_workgroup_variant_program_init(
         device,
         allocator,
         local_input_regs,
         work_group_input_regs,
         barrier_coefficient,
         usc_temps,
         compute_pipeline->shader_state.bo->dev_addr,
         &compute_pipeline->primary_base_workgroup_variant_program);
      if (result != VK_SUCCESS)
         goto err_destroy_compute_program;
   }

   return VK_SUCCESS;

err_destroy_compute_program:
   pvr_pds_compute_program_destroy(device,
                                   allocator,
                                   &compute_pipeline->primary_program,
                                   &compute_pipeline->primary_program_info);

err_free_descriptor_program:
   pvr_pds_descriptor_program_destroy(device,
                                      allocator,
                                      &compute_pipeline->descriptor_state);

err_free_shader:
   pvr_bo_suballoc_free(compute_pipeline->shader_state.bo);

   return result;
}

static VkResult
pvr_compute_pipeline_init(struct pvr_device *device,
                          struct vk_pipeline_cache *cache,
                          const VkComputePipelineCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *allocator,
                          struct pvr_compute_pipeline *compute_pipeline)
{
   VkResult result;

   pvr_pipeline_init(device,
                     PVR_PIPELINE_TYPE_COMPUTE,
                     &compute_pipeline->base);

   compute_pipeline->base.layout =
      pvr_pipeline_layout_from_handle(pCreateInfo->layout);

   result = pvr_compute_pipeline_compile(device,
                                         cache,
                                         pCreateInfo,
                                         allocator,
                                         compute_pipeline);
   if (result != VK_SUCCESS) {
      pvr_pipeline_finish(&compute_pipeline->base);
      return result;
   }

   return VK_SUCCESS;
}

static VkResult
pvr_compute_pipeline_create(struct pvr_device *device,
                            struct vk_pipeline_cache *cache,
                            const VkComputePipelineCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *allocator,
                            VkPipeline *const pipeline_out)
{
   struct pvr_compute_pipeline *compute_pipeline;
   VkResult result;

   compute_pipeline = vk_zalloc2(&device->vk.alloc,
                                 allocator,
                                 sizeof(*compute_pipeline),
                                 8,
                                 VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!compute_pipeline)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Compiles and uploads shaders and PDS programs. */
   result = pvr_compute_pipeline_init(device,
                                      cache,
                                      pCreateInfo,
                                      allocator,
                                      compute_pipeline);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, allocator, compute_pipeline);
      return result;
   }

   *pipeline_out = pvr_pipeline_to_handle(&compute_pipeline->base);

   return VK_SUCCESS;
}

static void pvr_compute_pipeline_destroy(
   struct pvr_device *const device,
   const VkAllocationCallbacks *const allocator,
   struct pvr_compute_pipeline *const compute_pipeline)
{
   if (compute_pipeline->flags.base_workgroup) {
      pvr_pds_compute_base_workgroup_variant_program_finish(
         device,
         allocator,
         &compute_pipeline->primary_base_workgroup_variant_program);
   }

   pvr_pds_compute_program_destroy(device,
                                   allocator,
                                   &compute_pipeline->primary_program,
                                   &compute_pipeline->primary_program_info);
   pvr_pds_descriptor_program_destroy(device,
                                      allocator,
                                      &compute_pipeline->descriptor_state);
   pvr_bo_suballoc_free(compute_pipeline->shader_state.bo);

   pvr_pipeline_finish(&compute_pipeline->base);

   vk_free2(&device->vk.alloc, allocator, compute_pipeline);
}

VkResult
pvr_CreateComputePipelines(VkDevice _device,
                           VkPipelineCache pipelineCache,
                           uint32_t createInfoCount,
                           const VkComputePipelineCreateInfo *pCreateInfos,
                           const VkAllocationCallbacks *pAllocator,
                           VkPipeline *pPipelines)
{
   VK_FROM_HANDLE(vk_pipeline_cache, cache, pipelineCache);
   PVR_FROM_HANDLE(pvr_device, device, _device);
   VkResult result = VK_SUCCESS;

   for (uint32_t i = 0; i < createInfoCount; i++) {
      const VkResult local_result =
         pvr_compute_pipeline_create(device,
                                     cache,
                                     &pCreateInfos[i],
                                     pAllocator,
                                     &pPipelines[i]);
      if (local_result != VK_SUCCESS) {
         result = local_result;
         pPipelines[i] = VK_NULL_HANDLE;
      }
   }

   return result;
}

/******************************************************************************
   Graphics pipeline functions
 ******************************************************************************/

static void
pvr_graphics_pipeline_destroy(struct pvr_device *const device,
                              const VkAllocationCallbacks *const allocator,
                              struct pvr_graphics_pipeline *const gfx_pipeline)
{
   const uint32_t num_vertex_attrib_programs =
      ARRAY_SIZE(gfx_pipeline->shader_state.vertex.pds_attrib_programs);

   pvr_pds_descriptor_program_destroy(
      device,
      allocator,
      &gfx_pipeline->shader_state.fragment.descriptor_state);

   pvr_pds_descriptor_program_destroy(
      device,
      allocator,
      &gfx_pipeline->shader_state.vertex.descriptor_state);

   for (uint32_t i = 0; i < num_vertex_attrib_programs; i++) {
      struct pvr_pds_attrib_program *const attrib_program =
         &gfx_pipeline->shader_state.vertex.pds_attrib_programs[i];

      pvr_pds_vertex_attrib_program_destroy(device, allocator, attrib_program);
   }

   pvr_bo_suballoc_free(
      gfx_pipeline->shader_state.fragment.pds_fragment_program.pvr_bo);
   pvr_bo_suballoc_free(
      gfx_pipeline->shader_state.fragment.pds_coeff_program.pvr_bo);

   pvr_bo_suballoc_free(gfx_pipeline->shader_state.fragment.bo);
   pvr_bo_suballoc_free(gfx_pipeline->shader_state.vertex.bo);

   pvr_pipeline_finish(&gfx_pipeline->base);

   vk_free2(&device->vk.alloc, allocator, gfx_pipeline);
}

static void pvr_vertex_state_save(struct pvr_graphics_pipeline *gfx_pipeline,
                                  pco_shader *vs)
{
   struct pvr_vertex_shader_state *vertex_state =
      &gfx_pipeline->shader_state.vertex;

   const pco_data *shader_data = pco_shader_data(vs);
   memcpy(&gfx_pipeline->vs_data, shader_data, sizeof(*shader_data));

   /* This ends up unused since we'll use the temp_usage for the PDS program we
    * end up selecting, and the descriptor PDS program doesn't use any temps.
    * Let's set it to ~0 in case it ever gets used.
    */
   vertex_state->stage_state.pds_temps_count = ~0;
}

static void pvr_fragment_state_save(struct pvr_graphics_pipeline *gfx_pipeline,
                                    pco_shader *fs)
{
   struct pvr_fragment_shader_state *fragment_state =
      &gfx_pipeline->shader_state.fragment;

   const pco_data *shader_data = pco_shader_data(fs);
   memcpy(&gfx_pipeline->fs_data, shader_data, sizeof(*shader_data));

   /* TODO: add selection for other values of pass type and sample rate. */
   fragment_state->pass_type = ROGUE_TA_PASSTYPE_OPAQUE;
   fragment_state->sample_rate = ROGUE_PDSINST_DOUTU_SAMPLE_RATE_INSTANCE;

   /* We can't initialize it yet since we still need to generate the PDS
    * programs so set it to `~0` to make sure that we set this up later on.
    */
   fragment_state->stage_state.pds_temps_count = ~0;
}

static bool pvr_blend_factor_requires_consts(VkBlendFactor factor)
{
   switch (factor) {
   case VK_BLEND_FACTOR_CONSTANT_COLOR:
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
   case VK_BLEND_FACTOR_CONSTANT_ALPHA:
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
      return true;

   default:
      return false;
   }
}

/**
 * \brief Indicates whether dynamic blend constants are needed.
 *
 * If the user has specified the blend constants to be dynamic, they might not
 * necessarily be using them. This function makes sure that they are being used
 * in order to determine whether we need to upload them later on for the shader
 * to access them.
 */
static bool pvr_graphics_pipeline_requires_dynamic_blend_consts(
   const struct pvr_graphics_pipeline *gfx_pipeline)
{
   const struct vk_dynamic_graphics_state *const state =
      &gfx_pipeline->dynamic_state;

   if (BITSET_TEST(state->set, MESA_VK_DYNAMIC_CB_BLEND_CONSTANTS))
      return false;

   for (uint32_t i = 0; i < state->cb.attachment_count; i++) {
      const struct vk_color_blend_attachment_state *attachment =
         &state->cb.attachments[i];

      const bool has_color_write =
         attachment->write_mask &
         (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
          VK_COLOR_COMPONENT_B_BIT);
      const bool has_alpha_write = attachment->write_mask &
                                   VK_COLOR_COMPONENT_A_BIT;

      if (!attachment->blend_enable || attachment->write_mask == 0)
         continue;

      if (has_color_write) {
         const uint8_t src_color_blend_factor =
            attachment->src_color_blend_factor;
         const uint8_t dst_color_blend_factor =
            attachment->dst_color_blend_factor;

         if (pvr_blend_factor_requires_consts(src_color_blend_factor) ||
             pvr_blend_factor_requires_consts(dst_color_blend_factor)) {
            return true;
         }
      }

      if (has_alpha_write) {
         const uint8_t src_alpha_blend_factor =
            attachment->src_alpha_blend_factor;
         const uint8_t dst_alpha_blend_factor =
            attachment->dst_alpha_blend_factor;

         if (pvr_blend_factor_requires_consts(src_alpha_blend_factor) ||
             pvr_blend_factor_requires_consts(dst_alpha_blend_factor)) {
            return true;
         }
      }
   }

   return false;
}

static uint32_t pvr_graphics_pipeline_alloc_shareds(
   const struct pvr_device *device,
   const struct pvr_graphics_pipeline *gfx_pipeline,
   enum pvr_stage_allocation stage,
   struct pvr_sh_reg_layout *const sh_reg_layout_out)
{
   ASSERTED const uint64_t reserved_shared_size =
      device->pdevice->dev_runtime_info.reserved_shared_size;
   ASSERTED const uint64_t max_coeff =
      device->pdevice->dev_runtime_info.max_coeffs;

   const struct pvr_pipeline_layout *layout = gfx_pipeline->base.layout;
   struct pvr_sh_reg_layout reg_layout = { 0 };
   uint32_t next_free_sh_reg = 0;

   next_free_sh_reg =
      pvr_pipeline_alloc_shareds(device, layout, stage, &reg_layout);

   reg_layout.blend_consts.present =
      (stage == PVR_STAGE_ALLOCATION_FRAGMENT &&
       pvr_graphics_pipeline_requires_dynamic_blend_consts(gfx_pipeline));
   if (reg_layout.blend_consts.present) {
      reg_layout.blend_consts.offset = next_free_sh_reg;
      next_free_sh_reg += PVR_DEV_ADDR_SIZE_IN_SH_REGS;
   }

   *sh_reg_layout_out = reg_layout;

   /* FIXME: We might need to take more things into consideration.
    * See pvr_calc_fscommon_size_and_tiles_in_flight().
    */
   assert(next_free_sh_reg <= reserved_shared_size - max_coeff);

   return next_free_sh_reg;
}

#undef PVR_DEV_ADDR_SIZE_IN_SH_REGS

static void pvr_graphics_pipeline_setup_vertex_dma(
   pco_shader *vs,
   const VkPipelineVertexInputStateCreateInfo *const vertex_input_state,
   struct pvr_pds_vertex_dma *const dma_descriptions,
   uint32_t *const dma_count)
{
   pco_vs_data *vs_data = &pco_shader_data(vs)->vs;

   const VkVertexInputBindingDescription
      *sorted_bindings[PVR_MAX_VERTEX_INPUT_BINDINGS] = { 0 };
   const VkVertexInputAttributeDescription
      *sorted_attributes[PVR_MAX_VERTEX_INPUT_BINDINGS] = { 0 };

   /* Vertex attributes map to the `layout(location = x)` annotation in the
    * shader where `x` is the attribute's location.
    * Vertex bindings have NO relation to the shader. They have nothing to do
    * with the `layout(set = x, binding = y)` notation. They instead indicate
    * where the data for a collection of vertex attributes comes from. The
    * application binds a VkBuffer with vkCmdBindVertexBuffers() to a specific
    * binding number and based on that we'll know which buffer to DMA the data
    * from, to fill in the collection of vertex attributes.
    */

   for (uint32_t i = 0; i < vertex_input_state->vertexBindingDescriptionCount;
        i++) {
      const VkVertexInputBindingDescription *binding_desc =
         &vertex_input_state->pVertexBindingDescriptions[i];

      sorted_bindings[binding_desc->binding] = binding_desc;
   }

   for (uint32_t i = 0; i < vertex_input_state->vertexAttributeDescriptionCount;
        i++) {
      const VkVertexInputAttributeDescription *attribute_desc =
         &vertex_input_state->pVertexAttributeDescriptions[i];

      sorted_attributes[attribute_desc->location] = attribute_desc;
   }

   for (uint32_t i = 0; i < vertex_input_state->vertexAttributeDescriptionCount;
        i++) {
      const VkVertexInputAttributeDescription *attribute = sorted_attributes[i];
      if (!attribute)
         continue;

      gl_vert_attrib location = attribute->location + VERT_ATTRIB_GENERIC0;
      const VkVertexInputBindingDescription *binding =
         sorted_bindings[attribute->binding];
      struct pvr_pds_vertex_dma *dma_desc = &dma_descriptions[*dma_count];
      const struct util_format_description *fmt_description =
         vk_format_description(attribute->format);

      const pco_range *attrib_range = &vs_data->attribs[location];

      /* Skip unused attributes. */
      if (!attrib_range->count)
         continue;

      /* DMA setup. */

      /* The PDS program sets up DDMADs to DMA attributes into vtxin regs.
       *
       * DDMAD -> Multiply, add, and DOUTD (i.e. DMA from that address).
       *          DMA source addr = src0 * src1 + src2
       *          DMA params = src3
       *
       * In the PDS program we setup src0 with the binding's stride and src1
       * with either the instance id or vertex id (both of which get filled by
       * the hardware). We setup src2 later on once we know which VkBuffer to
       * DMA the data from so it's saved for later when we patch the data
       * section.
       */

      /* TODO: Right now we're setting up a DMA per attribute. In a case where
       * there are multiple attributes packed into a single binding with
       * adjacent locations we'd still be DMAing them separately. This is not
       * great so the DMA setup should be smarter and could do with some
       * optimization.
       */

      *dma_desc = (struct pvr_pds_vertex_dma){ 0 };

      /* In relation to the Vulkan spec. 22.4. Vertex Input Address Calculation
       * this corresponds to `attribDesc.offset`.
       * The PDS program doesn't do anything with it but just save it in the
       * PDS program entry.
       */
      dma_desc->offset = attribute->offset;

      /* In relation to the Vulkan spec. 22.4. Vertex Input Address Calculation
       * this corresponds to `bindingDesc.stride`.
       * The PDS program will calculate the `effectiveVertexOffset` with this
       * and add it to the address provided in the patched data segment.
       */
      dma_desc->stride = binding->stride;

      if (binding->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE)
         dma_desc->flags = PVR_PDS_VERTEX_DMA_FLAGS_INSTANCE_RATE;
      else
         dma_desc->flags = 0;

      /* Size to DMA per vertex attribute. Used to setup src3 in the DDMAD. */
      /* TODO: what if not all components are used */
      assert(attrib_range->count == fmt_description->block.bits / 32);
      dma_desc->size_in_dwords = attrib_range->count;

      /* Vtxin reg offset to start DMAing into. */
      dma_desc->destination = attrib_range->start;

      /* Will be used by the driver to figure out buffer address to patch in the
       * data section. I.e. which binding we should DMA from.
       */
      dma_desc->binding_index = attribute->binding;

      /* We don't currently support VK_EXT_vertex_attribute_divisor so no
       * repeating of instance-rate vertex attributes needed. We should always
       * move on to the next vertex attribute.
       */
      assert(binding->inputRate != VK_VERTEX_INPUT_RATE_INSTANCE);
      dma_desc->divisor = 1;

      /* Will be used to generate PDS code that takes care of robust buffer
       * access, and later on by the driver to write the correct robustness
       * buffer address to DMA the fallback values from.
       */
      dma_desc->robustness_buffer_offset =
         pvr_get_robustness_buffer_format_offset(attribute->format);

      /* Used by later on by the driver to figure out if the buffer is being
       * accessed out of bounds, for robust buffer access.
       */
      dma_desc->component_size_in_bytes =
         fmt_description->block.bits / fmt_description->nr_channels / 8;

      ++*dma_count;
   }
}

static void pvr_graphics_pipeline_setup_fragment_coeff_program(
   pco_fs_data *fs_data,
   pco_vs_data *vs_data,
   nir_shader *fs,
   struct pvr_pds_coeff_loading_program *frag_coeff_program)
{
   uint64_t varyings_used = fs->info.inputs_read &
                            BITFIELD64_RANGE(VARYING_SLOT_VAR0, MAX_VARYING);

   unsigned fpu = 0;
   unsigned dest = 0;

   if (fs_data->uses.z) {
      pvr_csb_pack (&frag_coeff_program->FPU_iterators[fpu],
                    PDSINST_DOUT_FIELDS_DOUTI_SRC,
                    douti_src) {
         /* TODO: define instead of sizeof(uint16_t). */
         douti_src.f32_offset = fs_data->uses.w ? 1 * sizeof(uint16_t) : 0;
         douti_src.f16_offset = douti_src.f32_offset;
         douti_src.shademodel = ROGUE_PDSINST_DOUTI_SHADEMODEL_GOURUAD;
         douti_src.size = ROGUE_PDSINST_DOUTI_SIZE_1D;
      }

      frag_coeff_program->destination[fpu++] = dest++;
   }

   if (fs_data->uses.w) {
      pvr_csb_pack (&frag_coeff_program->FPU_iterators[fpu],
                    PDSINST_DOUT_FIELDS_DOUTI_SRC,
                    douti_src) {
         douti_src.f32_offset = 0;
         douti_src.f16_offset = douti_src.f32_offset;
         douti_src.shademodel = ROGUE_PDSINST_DOUTI_SHADEMODEL_GOURUAD;
         douti_src.size = ROGUE_PDSINST_DOUTI_SIZE_1D;
      }

      frag_coeff_program->destination[fpu++] = dest++;
   }

   if (fs_data->uses.pntc) {
      pvr_csb_pack (&frag_coeff_program->FPU_iterators[fpu],
                    PDSINST_DOUT_FIELDS_DOUTI_SRC,
                    douti_src) {
         douti_src.shademodel = ROGUE_PDSINST_DOUTI_SHADEMODEL_GOURUAD;
         douti_src.size = ROGUE_PDSINST_DOUTI_SIZE_2D;
         douti_src.pointsprite = true;
      }

      frag_coeff_program->destination[fpu++] = dest;
      dest += 2;
   }

   u_foreach_bit64 (varying, varyings_used) {
      nir_variable *var =
         nir_find_variable_with_location(fs, nir_var_shader_in, varying);
      assert(var);

      pco_range *cf_range = &fs_data->varyings[varying];
      assert(cf_range->count > 0);
      assert(!(cf_range->start % ROGUE_USC_COEFFICIENT_SET_SIZE));
      assert(!(cf_range->count % ROGUE_USC_COEFFICIENT_SET_SIZE));

      pco_range *vtxout_range = &vs_data->varyings[varying];
      assert(vtxout_range->count > 0);
      assert(vtxout_range->start >= 4);

      assert(vtxout_range->count ==
             cf_range->count / ROGUE_USC_COEFFICIENT_SET_SIZE);

      unsigned count = vtxout_range->count;

      unsigned vtxout = vtxout_range->start;

      /* pos.x, pos.y unused. */
      vtxout -= 2;

      /* pos.z unused. */
      if (!fs_data->uses.z)
         vtxout -= 1;

      /* pos.w unused. */
      if (!fs_data->uses.w)
         vtxout -= 1;

      pvr_csb_pack (&frag_coeff_program->FPU_iterators[fpu],
                    PDSINST_DOUT_FIELDS_DOUTI_SRC,
                    douti_src) {
         /* TODO: define instead of sizeof(uint16_t). */
         douti_src.f32_offset = vtxout * sizeof(uint16_t);
         /* TODO: f16 support. */
         douti_src.f16 = false;
         douti_src.f16_offset = douti_src.f32_offset;

         switch (var->data.interpolation) {
         case INTERP_MODE_SMOOTH:
            douti_src.shademodel = ROGUE_PDSINST_DOUTI_SHADEMODEL_GOURUAD;
            douti_src.perspective = true;
            break;

         case INTERP_MODE_NOPERSPECTIVE:
            douti_src.shademodel = ROGUE_PDSINST_DOUTI_SHADEMODEL_GOURUAD;
            break;

         case INTERP_MODE_FLAT:
            /* TODO: triangle fan, provoking vertex last. */
            douti_src.shademodel = ROGUE_PDSINST_DOUTI_SHADEMODEL_FLAT_VERTEX0;
            break;

         default:
            unreachable("Unimplemented interpolation type.");
         }

         douti_src.size = ROGUE_PDSINST_DOUTI_SIZE_1D + count - 1;
      }

      frag_coeff_program->destination[fpu++] =
         cf_range->start / ROGUE_USC_COEFFICIENT_SET_SIZE;
   }

   frag_coeff_program->num_fpu_iterators = fpu;
}

static void set_var(pco_range *allocation_list,
                    unsigned to,
                    nir_variable *var,
                    unsigned dwords_each)
{
   unsigned slots = glsl_count_dword_slots(var->type, false);

   allocation_list[var->data.location] = (pco_range){
      .start = to,
      .count = slots * dwords_each,
   };
}

static void allocate_var(pco_range *allocation_list,
                         unsigned *counter,
                         nir_variable *var,
                         unsigned dwords_each)
{
   unsigned slots = glsl_count_dword_slots(var->type, false);

   allocation_list[var->data.location] = (pco_range){
      .start = *counter,
      .count = slots * dwords_each,
   };

   *counter += slots * dwords_each;
}

static void try_allocate_var(pco_range *allocation_list,
                             unsigned *counter,
                             nir_shader *nir,
                             uint64_t bitset,
                             nir_variable_mode mode,
                             int location,
                             unsigned dwords_each)
{
   nir_variable *var = nir_find_variable_with_location(nir, mode, location);

   if (!(bitset & BITFIELD64_BIT(location)))
      return;

   assert(var);

   allocate_var(allocation_list, counter, var, dwords_each);
}

static void try_allocate_vars(pco_range *allocation_list,
                              unsigned *counter,
                              nir_shader *nir,
                              uint64_t *bitset,
                              nir_variable_mode mode,
                              bool f16,
                              enum glsl_interp_mode interp_mode,
                              unsigned dwords_each)
{
   uint64_t skipped = 0;

   while (*bitset) {
      int location = u_bit_scan64(bitset);

      nir_variable *var = nir_find_variable_with_location(nir, mode, location);
      assert(var);

      if (glsl_type_is_16bit(glsl_without_array_or_matrix(var->type)) != f16 ||
          var->data.interpolation != interp_mode) {
         skipped |= BITFIELD64_BIT(location);
         continue;
      }

      allocate_var(allocation_list, counter, var, dwords_each);
   }

   *bitset |= skipped;
}

static void allocate_val(pco_range *allocation_list,
                         unsigned *counter,
                         unsigned location,
                         unsigned dwords_each)
{
   allocation_list[location] = (pco_range){
      .start = *counter,
      .count = dwords_each,
   };

   *counter += dwords_each;
}

static void pvr_alloc_vs_sysvals(pco_data *data, nir_shader *nir)
{
   BITSET_DECLARE(system_values_read, SYSTEM_VALUE_MAX);
   BITSET_COPY(system_values_read, nir->info.system_values_read);

   gl_system_value sys_vals[] = {
      SYSTEM_VALUE_VERTEX_ID,     SYSTEM_VALUE_INSTANCE_ID,
      SYSTEM_VALUE_BASE_INSTANCE, SYSTEM_VALUE_BASE_VERTEX,
      SYSTEM_VALUE_DRAW_ID,
   };

   for (unsigned u = 0; u < ARRAY_SIZE(sys_vals); ++u) {
      if (BITSET_TEST(system_values_read, sys_vals[u])) {
         allocate_val(data->common.sys_vals,
                      &data->common.vtxins,
                      sys_vals[u],
                      1);

         BITSET_CLEAR(system_values_read, sys_vals[u]);
      }
   }

   assert(BITSET_IS_EMPTY(system_values_read));
}

static void pvr_init_vs_attribs(
   pco_data *data,
   const VkPipelineVertexInputStateCreateInfo *const vertex_input_state)
{
   for (unsigned u = 0; u < vertex_input_state->vertexAttributeDescriptionCount;
        ++u) {
      const VkVertexInputAttributeDescription *attrib =
         &vertex_input_state->pVertexAttributeDescriptions[u];

      gl_vert_attrib location = attrib->location + VERT_ATTRIB_GENERIC0;

      data->vs.attrib_formats[location] =
         vk_format_to_pipe_format(attrib->format);
   }
}

static void pvr_alloc_vs_attribs(pco_data *data, nir_shader *nir)
{
   /* TODO NEXT: this should be based on the format size. */
   nir_foreach_shader_in_variable (var, nir) {
      allocate_var(data->vs.attribs, &data->common.vtxins, var, 1);
   }
}

static void pvr_alloc_vs_varyings(pco_data *data, nir_shader *nir)
{
   uint64_t vars_mask = nir->info.outputs_written &
                        BITFIELD64_RANGE(VARYING_SLOT_VAR0, MAX_VARYING);

   /* Output position must be present. */
   assert(nir_find_variable_with_location(nir,
                                          nir_var_shader_out,
                                          VARYING_SLOT_POS));

   /* Varying ordering is specific. */
   try_allocate_var(data->vs.varyings,
                    &data->vs.vtxouts,
                    nir,
                    nir->info.outputs_written,
                    nir_var_shader_out,
                    VARYING_SLOT_POS,
                    1);

   /* Save varying counts. */
   u_foreach_bit64 (location, vars_mask) {
      nir_variable *var =
         nir_find_variable_with_location(nir, nir_var_shader_out, location);
      assert(var);

      /* TODO: f16 support. */
      bool f16 = glsl_type_is_16bit(glsl_without_array_or_matrix(var->type));
      assert(!f16);
      unsigned components = glsl_get_components(var->type);

      switch (var->data.interpolation) {
      case INTERP_MODE_SMOOTH:
         if (f16)
            data->vs.f16_smooth += components;
         else
            data->vs.f32_smooth += components;

         break;

      case INTERP_MODE_FLAT:
         if (f16)
            data->vs.f16_flat += components;
         else
            data->vs.f32_flat += components;

         break;

      case INTERP_MODE_NOPERSPECTIVE:
         if (f16)
            data->vs.f16_npc += components;
         else
            data->vs.f32_npc += components;

         break;

      default:
         unreachable();
      }
   }

   for (unsigned f16 = 0; f16 <= 1; ++f16) {
      for (enum glsl_interp_mode interp_mode = INTERP_MODE_SMOOTH;
           interp_mode <= INTERP_MODE_NOPERSPECTIVE;
           ++interp_mode) {
         try_allocate_vars(data->vs.varyings,
                           &data->vs.vtxouts,
                           nir,
                           &vars_mask,
                           nir_var_shader_out,
                           f16,
                           interp_mode,
                           1);
      }
   }

   assert(!vars_mask);

   const gl_varying_slot last_slots[] = {
      VARYING_SLOT_PSIZ,
      VARYING_SLOT_VIEWPORT,
      VARYING_SLOT_LAYER,
   };

   for (unsigned u = 0; u < ARRAY_SIZE(last_slots); ++u) {
      try_allocate_var(data->vs.varyings,
                       &data->vs.vtxouts,
                       nir,
                       nir->info.outputs_written,
                       nir_var_shader_out,
                       last_slots[u],
                       1);
   }
}

static void pvr_alloc_fs_sysvals(pco_data *data, nir_shader *nir)
{
   /* TODO */
}

static void pvr_alloc_fs_varyings(pco_data *data, nir_shader *nir)
{
   assert(!data->common.coeffs);

   /* Save the z/w locations. */
   unsigned zw_count = !!data->fs.uses.z + !!data->fs.uses.w;
   allocate_val(data->fs.varyings,
                &data->common.coeffs,
                VARYING_SLOT_POS,
                zw_count * ROGUE_USC_COEFFICIENT_SET_SIZE);

   /* If point coords are used, they come after z/w (if present). */
   nir_variable *var = nir_find_variable_with_location(nir,
                                                       nir_var_shader_in,
                                                       VARYING_SLOT_PNTC);
   if (var) {
      assert(!var->data.location_frac);
      unsigned count = glsl_get_components(var->type);
      assert(count == 2);

      allocate_var(data->fs.varyings,
                   &data->common.coeffs,
                   var,
                   ROGUE_USC_COEFFICIENT_SET_SIZE);

      data->fs.uses.pntc = true;
   }

   /* Allocate the rest of the input varyings. */
   nir_foreach_shader_in_variable (var, nir) {
      /* Already handled. */
      if (var->data.location == VARYING_SLOT_POS ||
          var->data.location == VARYING_SLOT_PNTC)
         continue;

      allocate_var(data->fs.varyings,
                   &data->common.coeffs,
                   var,
                   ROGUE_USC_COEFFICIENT_SET_SIZE);
   }
}

static void
pvr_init_fs_outputs(pco_data *data,
                    const struct pvr_render_pass *pass,
                    const struct pvr_render_subpass *const subpass,
                    const struct pvr_renderpass_hwsetup_subpass *hw_subpass)
{
   for (unsigned u = 0; u < subpass->color_count; ++u) {
      unsigned idx = subpass->color_attachments[u];
      if (idx == VK_ATTACHMENT_UNUSED)
         continue;

      gl_frag_result location = FRAG_RESULT_DATA0 + u;
      VkFormat vk_format = pass->attachments[idx].vk_format;
      data->fs.output_formats[location] = vk_format_to_pipe_format(vk_format);
   }

   /* TODO: z-replicate. */
}

static void
pvr_setup_fs_outputs(pco_data *data,
                     nir_shader *nir,
                     const struct pvr_render_subpass *const subpass,
                     const struct pvr_renderpass_hwsetup_subpass *hw_subpass)
{
   ASSERTED unsigned num_outputs = hw_subpass->setup.num_render_targets;
   assert(num_outputs == subpass->color_count);

   uint64_t outputs_written = nir->info.outputs_written;
   assert(util_bitcount64(outputs_written) == num_outputs);

   for (unsigned u = 0; u < subpass->color_count; ++u) {
      gl_frag_result location = FRAG_RESULT_DATA0 + u;
      unsigned idx = subpass->color_attachments[u];
      const struct usc_mrt_resource *mrt_resource;
      ASSERTED bool output_reg;
      enum pipe_format format;
      unsigned format_bits;
      nir_variable *var;

      if (idx == VK_ATTACHMENT_UNUSED)
         continue;

      assert(u == idx); /* TODO: not sure if this is true or not... */

      mrt_resource = &hw_subpass->setup.mrt_resources[u];
      output_reg = mrt_resource->type == USC_MRT_RESOURCE_TYPE_OUTPUT_REG;

      assert(output_reg);
      /* TODO: tile buffer support. */

      var = nir_find_variable_with_location(nir, nir_var_shader_out, location);
      assert(var);

      format = data->fs.output_formats[location];
      format_bits = util_format_get_blocksizebits(format);
      /* TODO: other sized formats. */
      assert(!(format_bits % 32));

      assert(mrt_resource->intermediate_size == format_bits / 8);

      set_var(data->fs.outputs,
              mrt_resource->reg.output_reg,
              var,
              format_bits / 32);
      data->fs.output_reg[location] = output_reg;

      outputs_written &= ~BITFIELD64_BIT(location);
   }

   /* TODO: z-replicate. */

   assert(!outputs_written);
}

static void pvr_init_fs_input_attachments(
   pco_data *data,
   const struct pvr_render_subpass *const subpass,
   const struct pvr_renderpass_hwsetup_subpass *hw_subpass)
{
   pvr_finishme("pvr_init_fs_input_attachments");
}

static void pvr_setup_fs_input_attachments(
   pco_data *data,
   nir_shader *nir,
   const struct pvr_render_subpass *const subpass,
   const struct pvr_renderpass_hwsetup_subpass *hw_subpass)
{
   pvr_finishme("pvr_setup_fs_input_attachments");
}

static void
pvr_preprocess_shader_data(pco_data *data,
                           nir_shader *nir,
                           const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   switch (nir->info.stage) {
   case MESA_SHADER_VERTEX: {
      const VkPipelineVertexInputStateCreateInfo *const vertex_input_state =
         pCreateInfo->pVertexInputState;

      pvr_init_vs_attribs(data, vertex_input_state);
      break;
   }

   case MESA_SHADER_FRAGMENT: {
      PVR_FROM_HANDLE(pvr_render_pass, pass, pCreateInfo->renderPass);
      const struct pvr_render_subpass *const subpass =
         &pass->subpasses[pCreateInfo->subpass];
      const struct pvr_renderpass_hw_map *subpass_map =
         &pass->hw_setup->subpass_map[pCreateInfo->subpass];
      const struct pvr_renderpass_hwsetup_subpass *hw_subpass =
         &pass->hw_setup->renders[subpass_map->render]
             .subpasses[subpass_map->subpass];

      pvr_init_fs_outputs(data, pass, subpass, hw_subpass);
      pvr_init_fs_input_attachments(data, subpass, hw_subpass);

      /* TODO: push consts, blend consts, dynamic state, etc. */
      break;
   }

   default:
      unreachable();
   }

   /* TODO: common things, like large constants being put into shareds. */
}

static void
pvr_postprocess_shader_data(pco_data *data,
                            nir_shader *nir,
                            const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   switch (nir->info.stage) {
   case MESA_SHADER_VERTEX: {
      pvr_alloc_vs_sysvals(data, nir);
      pvr_alloc_vs_attribs(data, nir);
      pvr_alloc_vs_varyings(data, nir);
      break;
   }

   case MESA_SHADER_FRAGMENT: {
      PVR_FROM_HANDLE(pvr_render_pass, pass, pCreateInfo->renderPass);
      const struct pvr_render_subpass *const subpass =
         &pass->subpasses[pCreateInfo->subpass];
      const struct pvr_renderpass_hw_map *subpass_map =
         &pass->hw_setup->subpass_map[pCreateInfo->subpass];
      const struct pvr_renderpass_hwsetup_subpass *hw_subpass =
         &pass->hw_setup->renders[subpass_map->render]
             .subpasses[subpass_map->subpass];

      pvr_alloc_fs_sysvals(data, nir);
      pvr_alloc_fs_varyings(data, nir);
      pvr_setup_fs_outputs(data, nir, subpass, hw_subpass);
      pvr_setup_fs_input_attachments(data, nir, subpass, hw_subpass);

      /* TODO: push consts, blend consts, dynamic state, etc. */
      break;
   }

   default:
      unreachable();
   }

   /* TODO: common things, like large constants being put into shareds. */
}

/* Compiles and uploads shaders and PDS programs. */
static VkResult
pvr_graphics_pipeline_compile(struct pvr_device *const device,
                              struct vk_pipeline_cache *cache,
                              const VkGraphicsPipelineCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *const allocator,
                              struct pvr_graphics_pipeline *const gfx_pipeline)
{
   struct pvr_pipeline_layout *layout = gfx_pipeline->base.layout;
   struct pvr_sh_reg_layout *sh_reg_layout_vert =
      &layout->sh_reg_layout_per_stage[PVR_STAGE_ALLOCATION_VERTEX_GEOMETRY];
   struct pvr_sh_reg_layout *sh_reg_layout_frag =
      &layout->sh_reg_layout_per_stage[PVR_STAGE_ALLOCATION_FRAGMENT];
   const uint32_t cache_line_size =
      rogue_get_slc_cache_line_size(&device->pdevice->dev_info);
   VkResult result;

   struct pvr_vertex_shader_state *vertex_state =
      &gfx_pipeline->shader_state.vertex;
   struct pvr_fragment_shader_state *fragment_state =
      &gfx_pipeline->shader_state.fragment;

   pco_ctx *pco_ctx = device->pdevice->pco_ctx;
   const struct spirv_to_nir_options *spirv_options =
      pco_spirv_options(pco_ctx);
   const nir_shader_compiler_options *nir_options = pco_nir_options(pco_ctx);

   nir_shader *producer = NULL;
   nir_shader *consumer = NULL;
   pco_data shader_data[MESA_SHADER_STAGES] = { 0 };
   nir_shader *nir_shaders[MESA_SHADER_STAGES] = { 0 };
   pco_shader *pco_shaders[MESA_SHADER_STAGES] = { 0 };
   pco_shader **vs = &pco_shaders[MESA_SHADER_VERTEX];
   pco_shader **fs = &pco_shaders[MESA_SHADER_FRAGMENT];
   void *shader_mem_ctx = ralloc_context(NULL);

   struct pvr_pds_vertex_dma vtx_dma_descriptions[PVR_MAX_VERTEX_ATTRIB_DMAS];
   uint32_t vtx_dma_count = 0;

   struct pvr_pds_coeff_loading_program frag_coeff_program = { 0 };

   for (gl_shader_stage stage = 0; stage < MESA_SHADER_STAGES; ++stage) {
      size_t stage_index = gfx_pipeline->stage_indices[stage];

      /* Skip unused/inactive stages. */
      if (stage_index == ~0)
         continue;

      result =
         vk_pipeline_shader_stage_to_nir(&device->vk,
                                         gfx_pipeline->base.pipeline_flags,
                                         &pCreateInfo->pStages[stage_index],
                                         spirv_options,
                                         nir_options,
                                         shader_mem_ctx,
                                         &nir_shaders[stage]);
      if (result != VK_SUCCESS)
         goto err_free_build_context;

      pco_preprocess_nir(pco_ctx, nir_shaders[stage]);
   }

   for (gl_shader_stage stage = 0; stage < MESA_SHADER_STAGES; ++stage) {
      if (!nir_shaders[stage])
         continue;

      if (producer)
         pco_link_nir(pco_ctx, producer, nir_shaders[stage]);

      producer = nir_shaders[stage];
   }

   for (gl_shader_stage stage = MESA_SHADER_STAGES; stage-- > 0;) {
      if (!nir_shaders[stage])
         continue;

      if (consumer)
         pco_rev_link_nir(pco_ctx, nir_shaders[stage], consumer);

      consumer = nir_shaders[stage];
   }

   for (gl_shader_stage stage = 0; stage < MESA_SHADER_STAGES; ++stage) {
      if (!nir_shaders[stage])
         continue;

      pvr_preprocess_shader_data(&shader_data[stage],
                                 nir_shaders[stage],
                                 pCreateInfo);

      pco_lower_nir(pco_ctx, nir_shaders[stage], &shader_data[stage]);
      pvr_lower_nir(pco_ctx, layout, nir_shaders[stage]);

      pco_postprocess_nir(pco_ctx, nir_shaders[stage], &shader_data[stage]);

      pvr_postprocess_shader_data(&shader_data[stage],
                                  nir_shaders[stage],
                                  pCreateInfo);
   }

   /* TODO NEXT: setup shareds/for descriptors, here or in
    * pvr_{pre,post}process_shader_data.
    */
   memset(sh_reg_layout_vert, 0, sizeof(*sh_reg_layout_vert));
   memset(sh_reg_layout_frag, 0, sizeof(*sh_reg_layout_frag));

   for (gl_shader_stage stage = 0; stage < MESA_SHADER_STAGES; ++stage) {
      pco_shader **pco = &pco_shaders[stage];

      /* Skip unused/inactive stages. */
      if (!nir_shaders[stage])
         continue;

      *pco = pco_trans_nir(pco_ctx,
                           nir_shaders[stage],
                           &shader_data[stage],
                           shader_mem_ctx);
      if (!*pco) {
         result = VK_ERROR_INITIALIZATION_FAILED;
         goto err_free_build_context;
      }

      pco_process_ir(pco_ctx, *pco);
      pco_encode_ir(pco_ctx, *pco);
      pco_shader_finalize(pco_ctx, *pco);
   }

   pvr_graphics_pipeline_setup_vertex_dma(*vs,
                                          pCreateInfo->pVertexInputState,
                                          vtx_dma_descriptions,
                                          &vtx_dma_count);

   pvr_vertex_state_save(gfx_pipeline, *vs);

   result = pvr_gpu_upload_usc(
      device,
      pco_shader_binary_data(pco_shaders[MESA_SHADER_VERTEX]),
      pco_shader_binary_size(pco_shaders[MESA_SHADER_VERTEX]),
      cache_line_size,
      &vertex_state->bo);
   if (result != VK_SUCCESS)
      goto err_free_build_context;

   if (pco_shaders[MESA_SHADER_FRAGMENT]) {
      pvr_graphics_pipeline_setup_fragment_coeff_program(
         &pco_shader_data(pco_shaders[MESA_SHADER_FRAGMENT])->fs,
         &pco_shader_data(pco_shaders[MESA_SHADER_VERTEX])->vs,
         nir_shaders[MESA_SHADER_FRAGMENT],
         &frag_coeff_program);

      pvr_fragment_state_save(gfx_pipeline, *fs);

      result = pvr_gpu_upload_usc(
         device,
         pco_shader_binary_data(pco_shaders[MESA_SHADER_FRAGMENT]),
         pco_shader_binary_size(pco_shaders[MESA_SHADER_FRAGMENT]),
         cache_line_size,
         &fragment_state->bo);
      if (result != VK_SUCCESS)
         goto err_free_vertex_bo;

      /* TODO: powervr has an optimization where it attempts to recompile
       * shaders. See PipelineCompileNoISPFeedbackFragmentStage. Unimplemented
       * since in our case the optimization doesn't happen.
       */

      result = pvr_pds_coeff_program_create_and_upload(device,
                                                       allocator,
                                                       &frag_coeff_program,
                                                       fragment_state);
      if (result != VK_SUCCESS)
         goto err_free_fragment_bo;

      result = pvr_pds_fragment_program_create_and_upload(device,
                                                          allocator,
                                                          *fs,
                                                          fragment_state);
      if (result != VK_SUCCESS)
         goto err_free_coeff_program;

      result = pvr_pds_descriptor_program_create_and_upload(
         device,
         allocator,
         layout,
         PVR_STAGE_ALLOCATION_FRAGMENT,
         sh_reg_layout_frag,
         &fragment_state->descriptor_state);
      if (result != VK_SUCCESS)
         goto err_free_frag_program;

      /* If not, we need to MAX2() and set
       * `fragment_state->stage_state.pds_temps_count` appropriately.
       */
      assert(fragment_state->descriptor_state.pds_info.temps_required == 0);
   }

   result = pvr_pds_vertex_attrib_programs_create_and_upload(
      device,
      allocator,
      pco_shader_data(pco_shaders[MESA_SHADER_VERTEX]),
      vtx_dma_descriptions,
      vtx_dma_count,
      &vertex_state->pds_attrib_programs);
   if (result != VK_SUCCESS)
      goto err_free_frag_descriptor_program;

   result = pvr_pds_descriptor_program_create_and_upload(
      device,
      allocator,
      layout,
      PVR_STAGE_ALLOCATION_VERTEX_GEOMETRY,
      sh_reg_layout_vert,
      &vertex_state->descriptor_state);
   if (result != VK_SUCCESS)
      goto err_free_vertex_attrib_program;

   /* FIXME: When the temp_buffer_total_size is non-zero we need to allocate a
    * scratch buffer for both vertex and fragment stage.
    * Figure out the best place to do this.
    */
   /* assert(pvr_pds_descriptor_program_variables.temp_buff_total_size == 0); */
   /* TODO: Implement spilling with the above. */

   ralloc_free(shader_mem_ctx);

   return VK_SUCCESS;

err_free_vertex_attrib_program:
   for (uint32_t i = 0; i < ARRAY_SIZE(vertex_state->pds_attrib_programs);
        i++) {
      struct pvr_pds_attrib_program *const attrib_program =
         &vertex_state->pds_attrib_programs[i];

      pvr_pds_vertex_attrib_program_destroy(device, allocator, attrib_program);
   }
err_free_frag_descriptor_program:
   pvr_pds_descriptor_program_destroy(device,
                                      allocator,
                                      &fragment_state->descriptor_state);
err_free_frag_program:
   pvr_bo_suballoc_free(fragment_state->pds_fragment_program.pvr_bo);
err_free_coeff_program:
   pvr_bo_suballoc_free(fragment_state->pds_coeff_program.pvr_bo);
err_free_fragment_bo:
   pvr_bo_suballoc_free(fragment_state->bo);
err_free_vertex_bo:
   pvr_bo_suballoc_free(vertex_state->bo);
err_free_build_context:
   ralloc_free(shader_mem_ctx);
   return result;
}

static struct vk_render_pass_state
pvr_create_renderpass_state(const VkGraphicsPipelineCreateInfo *const info)
{
   PVR_FROM_HANDLE(pvr_render_pass, pass, info->renderPass);
   const struct pvr_render_subpass *const subpass =
      &pass->subpasses[info->subpass];

   enum vk_rp_attachment_flags attachments = 0;

   assert(info->subpass < pass->subpass_count);

   for (uint32_t i = 0; i < subpass->color_count; i++) {
      if (pass->attachments[subpass->color_attachments[i]].aspects)
         attachments |= MESA_VK_RP_ATTACHMENT_COLOR_0_BIT << i;
   }

   if (subpass->depth_stencil_attachment != VK_ATTACHMENT_UNUSED) {
      VkImageAspectFlags ds_aspects =
         pass->attachments[subpass->depth_stencil_attachment].aspects;
      if (ds_aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
         attachments |= MESA_VK_RP_ATTACHMENT_DEPTH_BIT;
      if (ds_aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
         attachments |= MESA_VK_RP_ATTACHMENT_STENCIL_BIT;
   }

   return (struct vk_render_pass_state){
      .attachments = attachments,

      /* TODO: This is only needed for VK_KHR_create_renderpass2 (or core 1.2),
       * which is not currently supported.
       */
      .view_mask = 0,
   };
}

static VkResult
pvr_graphics_pipeline_init(struct pvr_device *device,
                           struct vk_pipeline_cache *cache,
                           const VkGraphicsPipelineCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *allocator,
                           struct pvr_graphics_pipeline *gfx_pipeline)
{
   struct vk_dynamic_graphics_state *const dynamic_state =
      &gfx_pipeline->dynamic_state;
   const struct vk_render_pass_state rp_state =
      pvr_create_renderpass_state(pCreateInfo);

   struct vk_graphics_pipeline_all_state all_state;
   struct vk_graphics_pipeline_state state = { 0 };

   VkResult result;

   pvr_pipeline_init(device, PVR_PIPELINE_TYPE_GRAPHICS, &gfx_pipeline->base);

   result = vk_graphics_pipeline_state_fill(&device->vk,
                                            &state,
                                            pCreateInfo,
                                            &rp_state,
                                            0,
                                            &all_state,
                                            NULL,
                                            0,
                                            NULL);
   if (result != VK_SUCCESS)
      goto err_pipeline_finish;

   vk_dynamic_graphics_state_init(dynamic_state);

   /* Load static state into base dynamic state holder. */
   vk_dynamic_graphics_state_fill(dynamic_state, &state);

   /* The value of ms.rasterization_samples is undefined when
    * rasterizer_discard_enable is set, but we need a specific value.
    * Fill that in here.
    */
   if (state.rs->rasterizer_discard_enable)
      dynamic_state->ms.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;

   memset(gfx_pipeline->stage_indices, ~0, sizeof(gfx_pipeline->stage_indices));

   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      VkShaderStageFlagBits vk_stage = pCreateInfo->pStages[i].stage;
      gl_shader_stage gl_stage = vk_to_mesa_shader_stage(vk_stage);
      /* From the Vulkan 1.2.192 spec for VkPipelineShaderStageCreateInfo:
       *
       *    "stage must not be VK_SHADER_STAGE_ALL_GRAPHICS,
       *    or VK_SHADER_STAGE_ALL."
       *
       * So we don't handle that.
       *
       * We also don't handle VK_SHADER_STAGE_TESSELLATION_* and
       * VK_SHADER_STAGE_GEOMETRY_BIT stages as 'tessellationShader' and
       * 'geometryShader' are set to false in the VkPhysicalDeviceFeatures
       * structure returned by the driver.
       */
      switch (pCreateInfo->pStages[i].stage) {
      case VK_SHADER_STAGE_VERTEX_BIT:
      case VK_SHADER_STAGE_FRAGMENT_BIT:
         gfx_pipeline->stage_indices[gl_stage] = i;
         break;
      default:
         unreachable("Unsupported stage.");
      }
   }

   gfx_pipeline->base.layout =
      pvr_pipeline_layout_from_handle(pCreateInfo->layout);

   /* Compiles and uploads shaders and PDS programs. */
   result = pvr_graphics_pipeline_compile(device,
                                          cache,
                                          pCreateInfo,
                                          allocator,
                                          gfx_pipeline);
   if (result != VK_SUCCESS)
      goto err_pipeline_finish;

   return VK_SUCCESS;

err_pipeline_finish:
   pvr_pipeline_finish(&gfx_pipeline->base);

   return result;
}

/* If allocator == NULL, the internal one will be used. */
static VkResult
pvr_graphics_pipeline_create(struct pvr_device *device,
                             struct vk_pipeline_cache *cache,
                             const VkGraphicsPipelineCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *allocator,
                             VkPipeline *const pipeline_out)
{
   struct pvr_graphics_pipeline *gfx_pipeline;
   VkResult result;

   gfx_pipeline = vk_zalloc2(&device->vk.alloc,
                             allocator,
                             sizeof(*gfx_pipeline),
                             8,
                             VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!gfx_pipeline)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Compiles and uploads shaders and PDS programs too. */
   result = pvr_graphics_pipeline_init(device,
                                       cache,
                                       pCreateInfo,
                                       allocator,
                                       gfx_pipeline);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, allocator, gfx_pipeline);
      return result;
   }

   *pipeline_out = pvr_pipeline_to_handle(&gfx_pipeline->base);

   return VK_SUCCESS;
}

VkResult
pvr_CreateGraphicsPipelines(VkDevice _device,
                            VkPipelineCache pipelineCache,
                            uint32_t createInfoCount,
                            const VkGraphicsPipelineCreateInfo *pCreateInfos,
                            const VkAllocationCallbacks *pAllocator,
                            VkPipeline *pPipelines)
{
   VK_FROM_HANDLE(vk_pipeline_cache, cache, pipelineCache);
   PVR_FROM_HANDLE(pvr_device, device, _device);
   VkResult result = VK_SUCCESS;

   for (uint32_t i = 0; i < createInfoCount; i++) {
      const VkResult local_result =
         pvr_graphics_pipeline_create(device,
                                      cache,
                                      &pCreateInfos[i],
                                      pAllocator,
                                      &pPipelines[i]);
      if (local_result != VK_SUCCESS) {
         result = local_result;
         pPipelines[i] = VK_NULL_HANDLE;
      }
   }

   return result;
}

/*****************************************************************************
   Other functions
*****************************************************************************/

void pvr_DestroyPipeline(VkDevice _device,
                         VkPipeline _pipeline,
                         const VkAllocationCallbacks *pAllocator)
{
   PVR_FROM_HANDLE(pvr_pipeline, pipeline, _pipeline);
   PVR_FROM_HANDLE(pvr_device, device, _device);

   if (!pipeline)
      return;

   switch (pipeline->type) {
   case PVR_PIPELINE_TYPE_GRAPHICS: {
      struct pvr_graphics_pipeline *const gfx_pipeline =
         to_pvr_graphics_pipeline(pipeline);

      pvr_graphics_pipeline_destroy(device, pAllocator, gfx_pipeline);
      break;
   }

   case PVR_PIPELINE_TYPE_COMPUTE: {
      struct pvr_compute_pipeline *const compute_pipeline =
         to_pvr_compute_pipeline(pipeline);

      pvr_compute_pipeline_destroy(device, pAllocator, compute_pipeline);
      break;
   }

   default:
      unreachable("Unknown pipeline type.");
   }
}
