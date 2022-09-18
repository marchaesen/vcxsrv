/*
 * Copyright © 2021 Bas Nieuwenhuizen
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
#include "radv_acceleration_structure.h"
#include "radv_private.h"

#include "nir_builder.h"
#include "radv_cs.h"
#include "radv_meta.h"

#include "radix_sort/radv_radix_sort.h"

#include "bvh/build_interface.h"

static const uint32_t leaf_spv[] = {
#include "bvh/leaf.comp.spv.h"
};

static const uint32_t morton_spv[] = {
#include "bvh/morton.comp.spv.h"
};

static const uint32_t internal_spv[] = {
#include "bvh/internal.comp.spv.h"
};

/* Min and max bounds of the bvh used to compute morton codes */
#define SCRATCH_TOTAL_BOUNDS_SIZE (6 * sizeof(float))

#define KEY_ID_PAIR_SIZE 8

VKAPI_ATTR void VKAPI_CALL
radv_GetAccelerationStructureBuildSizesKHR(
   VkDevice _device, VkAccelerationStructureBuildTypeKHR buildType,
   const VkAccelerationStructureBuildGeometryInfoKHR *pBuildInfo,
   const uint32_t *pMaxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR *pSizeInfo)
{
   RADV_FROM_HANDLE(radv_device, device, _device);

   uint64_t triangles = 0, boxes = 0, instances = 0;

   STATIC_ASSERT(sizeof(struct radv_bvh_triangle_node) == 64);
   STATIC_ASSERT(sizeof(struct radv_bvh_aabb_node) == 64);
   STATIC_ASSERT(sizeof(struct radv_bvh_instance_node) == 128);
   STATIC_ASSERT(sizeof(struct radv_bvh_box16_node) == 64);
   STATIC_ASSERT(sizeof(struct radv_bvh_box32_node) == 128);

   for (uint32_t i = 0; i < pBuildInfo->geometryCount; ++i) {
      const VkAccelerationStructureGeometryKHR *geometry;
      if (pBuildInfo->pGeometries)
         geometry = &pBuildInfo->pGeometries[i];
      else
         geometry = pBuildInfo->ppGeometries[i];

      switch (geometry->geometryType) {
      case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
         triangles += pMaxPrimitiveCounts[i];
         break;
      case VK_GEOMETRY_TYPE_AABBS_KHR:
         boxes += pMaxPrimitiveCounts[i];
         break;
      case VK_GEOMETRY_TYPE_INSTANCES_KHR:
         instances += pMaxPrimitiveCounts[i];
         break;
      case VK_GEOMETRY_TYPE_MAX_ENUM_KHR:
         unreachable("VK_GEOMETRY_TYPE_MAX_ENUM_KHR unhandled");
      }
   }

   uint64_t children = boxes + instances + triangles;
   /* Initialize to 1 to have enought space for the root node. */
   uint64_t internal_nodes = 1;
   while (children > 1) {
      children = DIV_ROUND_UP(children, 4);
      internal_nodes += children;
   }

   uint64_t size = boxes * 128 + instances * 128 + triangles * 64 + internal_nodes * 128 +
                   ALIGN(sizeof(struct radv_accel_struct_header), 64);
   size +=
      pBuildInfo->geometryCount * sizeof(struct radv_accel_struct_geometry_info);

   pSizeInfo->accelerationStructureSize = size;

   /* 2x the max number of nodes in a BVH layer and order information for sorting. */
   uint32_t leaf_count = boxes + instances + triangles;
   VkDeviceSize scratchSize = 2 * leaf_count * KEY_ID_PAIR_SIZE;

   radix_sort_vk_memory_requirements_t requirements;
   radix_sort_vk_get_memory_requirements(device->meta_state.accel_struct_build.radix_sort,
                                         leaf_count, &requirements);

   /* Make sure we have the space required by the radix sort. */
   scratchSize = MAX2(scratchSize, requirements.keyvals_size * 2);

   scratchSize += requirements.internal_size + SCRATCH_TOTAL_BOUNDS_SIZE;

   scratchSize = MAX2(4096, scratchSize);
   pSizeInfo->updateScratchSize = scratchSize;
   pSizeInfo->buildScratchSize = scratchSize;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateAccelerationStructureKHR(VkDevice _device,
                                    const VkAccelerationStructureCreateInfoKHR *pCreateInfo,
                                    const VkAllocationCallbacks *pAllocator,
                                    VkAccelerationStructureKHR *pAccelerationStructure)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_buffer, buffer, pCreateInfo->buffer);
   struct radv_acceleration_structure *accel;

   accel = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*accel), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (accel == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &accel->base, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR);

   accel->mem_offset = buffer->offset + pCreateInfo->offset;
   accel->size = pCreateInfo->size;
   accel->bo = buffer->bo;
   accel->va = radv_buffer_get_va(accel->bo) + accel->mem_offset;

   *pAccelerationStructure = radv_acceleration_structure_to_handle(accel);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyAccelerationStructureKHR(VkDevice _device,
                                     VkAccelerationStructureKHR accelerationStructure,
                                     const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_acceleration_structure, accel, accelerationStructure);

   if (!accel)
      return;

   vk_object_base_finish(&accel->base);
   vk_free2(&device->vk.alloc, pAllocator, accel);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL
radv_GetAccelerationStructureDeviceAddressKHR(
   VkDevice _device, const VkAccelerationStructureDeviceAddressInfoKHR *pInfo)
{
   RADV_FROM_HANDLE(radv_acceleration_structure, accel, pInfo->accelerationStructure);
   return accel->va;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_WriteAccelerationStructuresPropertiesKHR(
   VkDevice _device, uint32_t accelerationStructureCount,
   const VkAccelerationStructureKHR *pAccelerationStructures, VkQueryType queryType,
   size_t dataSize, void *pData, size_t stride)
{
   unreachable("Unimplemented");
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_BuildAccelerationStructuresKHR(
   VkDevice _device, VkDeferredOperationKHR deferredOperation, uint32_t infoCount,
   const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
   const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   unreachable("Unimplemented");
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CopyAccelerationStructureKHR(VkDevice _device, VkDeferredOperationKHR deferredOperation,
                                  const VkCopyAccelerationStructureInfoKHR *pInfo)
{
   unreachable("Unimplemented");
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

static nir_builder
create_accel_build_shader(struct radv_device *device, const char *name)
{
   nir_builder b = radv_meta_init_shader(device, MESA_SHADER_COMPUTE, "%s", name);
   b.shader->info.workgroup_size[0] = 64;

   assert(b.shader->info.workgroup_size[1] == 1);
   assert(b.shader->info.workgroup_size[2] == 1);
   assert(!b.shader->info.workgroup_size_variable);

   return b;
}

enum copy_mode {
   COPY_MODE_COPY,
   COPY_MODE_SERIALIZE,
   COPY_MODE_DESERIALIZE,
};

struct copy_constants {
   uint64_t src_addr;
   uint64_t dst_addr;
   uint32_t mode;
};

static nir_shader *
build_copy_shader(struct radv_device *dev)
{
   nir_builder b = create_accel_build_shader(dev, "accel_copy");

   nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
   nir_ssa_def *wg_id = nir_load_workgroup_id(&b, 32);
   nir_ssa_def *block_size =
      nir_imm_ivec4(&b, b.shader->info.workgroup_size[0], b.shader->info.workgroup_size[1],
                    b.shader->info.workgroup_size[2], 0);

   nir_ssa_def *global_id =
      nir_channel(&b, nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id), 0);

   nir_variable *offset_var =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "offset");
   nir_ssa_def *offset = nir_imul_imm(&b, global_id, 16);
   nir_store_var(&b, offset_var, offset, 1);

   nir_ssa_def *increment = nir_imul_imm(&b, nir_channel(&b, nir_load_num_workgroups(&b, 32), 0),
                                         b.shader->info.workgroup_size[0] * 16);

   nir_ssa_def *pconst0 =
      nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .base = 0, .range = 16);
   nir_ssa_def *pconst1 =
      nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 0), .base = 16, .range = 4);
   nir_ssa_def *src_base_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst0, 0b0011));
   nir_ssa_def *dst_base_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst0, 0b1100));
   nir_ssa_def *mode = nir_channel(&b, pconst1, 0);

   nir_variable *compacted_size_var =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint64_t_type(), "compacted_size");
   nir_variable *src_offset_var =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "src_offset");
   nir_variable *dst_offset_var =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "dst_offset");
   nir_variable *instance_offset_var =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "instance_offset");
   nir_variable *instance_count_var =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "instance_count");
   nir_variable *value_var =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_vec4_type(), "value");

   nir_push_if(&b, nir_ieq_imm(&b, mode, COPY_MODE_SERIALIZE));
   {
      nir_ssa_def *instance_count = nir_build_load_global(
         &b, 1, 32,
         nir_iadd_imm(&b, src_base_addr,
                      offsetof(struct radv_accel_struct_header, instance_count)));
      nir_ssa_def *compacted_size = nir_build_load_global(
         &b, 1, 64,
         nir_iadd_imm(&b, src_base_addr,
                      offsetof(struct radv_accel_struct_header, compacted_size)));
      nir_ssa_def *serialization_size = nir_build_load_global(
         &b, 1, 64,
         nir_iadd_imm(&b, src_base_addr,
                      offsetof(struct radv_accel_struct_header, serialization_size)));

      nir_store_var(&b, compacted_size_var, compacted_size, 1);
      nir_store_var(&b, instance_offset_var,
                    nir_build_load_global(
                       &b, 1, 32,
                       nir_iadd_imm(&b, src_base_addr,
                                    offsetof(struct radv_accel_struct_header, instance_offset))),
                    1);
      nir_store_var(&b, instance_count_var, instance_count, 1);

      nir_ssa_def *dst_offset = nir_iadd_imm(&b, nir_imul_imm(&b, instance_count, sizeof(uint64_t)),
                                             sizeof(struct radv_accel_struct_serialization_header));
      nir_store_var(&b, src_offset_var, nir_imm_int(&b, 0), 1);
      nir_store_var(&b, dst_offset_var, dst_offset, 1);

      nir_push_if(&b, nir_ieq_imm(&b, global_id, 0));
      {
         nir_build_store_global(&b, serialization_size,
                                nir_iadd_imm(&b, dst_base_addr,
                                             offsetof(struct radv_accel_struct_serialization_header,
                                                      serialization_size)));
         nir_build_store_global(
            &b, compacted_size,
            nir_iadd_imm(&b, dst_base_addr,
                         offsetof(struct radv_accel_struct_serialization_header, compacted_size)));
         nir_build_store_global(
            &b, nir_u2u64(&b, instance_count),
            nir_iadd_imm(&b, dst_base_addr,
                         offsetof(struct radv_accel_struct_serialization_header, instance_count)));
      }
      nir_pop_if(&b, NULL);
   }
   nir_push_else(&b, NULL);
   nir_push_if(&b, nir_ieq_imm(&b, mode, COPY_MODE_DESERIALIZE));
   {
      nir_ssa_def *instance_count = nir_build_load_global(
         &b, 1, 32,
         nir_iadd_imm(&b, src_base_addr,
                      offsetof(struct radv_accel_struct_serialization_header, instance_count)));
      nir_ssa_def *src_offset = nir_iadd_imm(&b, nir_imul_imm(&b, instance_count, sizeof(uint64_t)),
                                             sizeof(struct radv_accel_struct_serialization_header));

      nir_ssa_def *header_addr = nir_iadd(&b, src_base_addr, nir_u2u64(&b, src_offset));
      nir_store_var(&b, compacted_size_var,
                    nir_build_load_global(
                       &b, 1, 64,
                       nir_iadd_imm(&b, header_addr,
                                    offsetof(struct radv_accel_struct_header, compacted_size))),
                    1);
      nir_store_var(&b, instance_offset_var,
                    nir_build_load_global(
                       &b, 1, 32,
                       nir_iadd_imm(&b, header_addr,
                                    offsetof(struct radv_accel_struct_header, instance_offset))),
                    1);
      nir_store_var(&b, instance_count_var, instance_count, 1);
      nir_store_var(&b, src_offset_var, src_offset, 1);
      nir_store_var(&b, dst_offset_var, nir_imm_int(&b, 0), 1);
   }
   nir_push_else(&b, NULL); /* COPY_MODE_COPY */
   {
      nir_store_var(&b, compacted_size_var,
                    nir_build_load_global(
                       &b, 1, 64,
                       nir_iadd_imm(&b, src_base_addr,
                                    offsetof(struct radv_accel_struct_header, compacted_size))),
                    1);

      nir_store_var(&b, src_offset_var, nir_imm_int(&b, 0), 1);
      nir_store_var(&b, dst_offset_var, nir_imm_int(&b, 0), 1);
      nir_store_var(&b, instance_offset_var, nir_imm_int(&b, 0), 1);
      nir_store_var(&b, instance_count_var, nir_imm_int(&b, 0), 1);
   }
   nir_pop_if(&b, NULL);
   nir_pop_if(&b, NULL);

   nir_ssa_def *instance_bound =
      nir_imul_imm(&b, nir_load_var(&b, instance_count_var), sizeof(struct radv_bvh_instance_node));
   nir_ssa_def *compacted_size = nir_build_load_global(
      &b, 1, 32,
      nir_iadd_imm(&b, src_base_addr, offsetof(struct radv_accel_struct_header, compacted_size)));

   nir_push_loop(&b);
   {
      offset = nir_load_var(&b, offset_var);
      nir_push_if(&b, nir_ilt(&b, offset, compacted_size));
      {
         nir_ssa_def *src_offset = nir_iadd(&b, offset, nir_load_var(&b, src_offset_var));
         nir_ssa_def *dst_offset = nir_iadd(&b, offset, nir_load_var(&b, dst_offset_var));
         nir_ssa_def *src_addr = nir_iadd(&b, src_base_addr, nir_u2u64(&b, src_offset));
         nir_ssa_def *dst_addr = nir_iadd(&b, dst_base_addr, nir_u2u64(&b, dst_offset));

         nir_ssa_def *value = nir_build_load_global(&b, 4, 32, src_addr, .align_mul = 16);
         nir_store_var(&b, value_var, value, 0xf);

         nir_ssa_def *instance_offset = nir_isub(&b, offset, nir_load_var(&b, instance_offset_var));
         nir_ssa_def *in_instance_bound =
            nir_iand(&b, nir_uge(&b, offset, nir_load_var(&b, instance_offset_var)),
                     nir_ult(&b, instance_offset, instance_bound));
         nir_ssa_def *instance_start = nir_ieq_imm(
            &b, nir_iand_imm(&b, instance_offset, sizeof(struct radv_bvh_instance_node) - 1), 0);

         nir_push_if(&b, nir_iand(&b, in_instance_bound, instance_start));
         {
            nir_ssa_def *instance_id = nir_ushr_imm(&b, instance_offset, 7);

            nir_push_if(&b, nir_ieq_imm(&b, mode, COPY_MODE_SERIALIZE));
            {
               nir_ssa_def *instance_addr = nir_imul_imm(&b, instance_id, sizeof(uint64_t));
               instance_addr = nir_iadd_imm(&b, instance_addr,
                                            sizeof(struct radv_accel_struct_serialization_header));
               instance_addr = nir_iadd(&b, dst_base_addr, nir_u2u64(&b, instance_addr));

               nir_build_store_global(&b, nir_channels(&b, value, 3), instance_addr,
                                      .align_mul = 8);
            }
            nir_push_else(&b, NULL);
            {
               nir_ssa_def *instance_addr = nir_imul_imm(&b, instance_id, sizeof(uint64_t));
               instance_addr = nir_iadd_imm(&b, instance_addr,
                                            sizeof(struct radv_accel_struct_serialization_header));
               instance_addr = nir_iadd(&b, src_base_addr, nir_u2u64(&b, instance_addr));

               nir_ssa_def *instance_value =
                  nir_build_load_global(&b, 2, 32, instance_addr, .align_mul = 8);

               nir_ssa_def *values[] = {
                  nir_channel(&b, instance_value, 0),
                  nir_channel(&b, instance_value, 1),
                  nir_channel(&b, value, 2),
                  nir_channel(&b, value, 3),
               };

               nir_store_var(&b, value_var, nir_vec(&b, values, 4), 0xf);
            }
            nir_pop_if(&b, NULL);
         }
         nir_pop_if(&b, NULL);

         nir_store_var(&b, offset_var, nir_iadd(&b, offset, increment), 1);

         nir_build_store_global(&b, nir_load_var(&b, value_var), dst_addr, .align_mul = 16);
      }
      nir_push_else(&b, NULL);
      {
         nir_jump(&b, nir_jump_break);
      }
      nir_pop_if(&b, NULL);
   }
   nir_pop_loop(&b, NULL);
   return b.shader;
}

void
radv_device_finish_accel_struct_build_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;
   radv_DestroyPipeline(radv_device_to_handle(device), state->accel_struct_build.copy_pipeline,
                        &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device), state->accel_struct_build.internal_pipeline,
                        &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device), state->accel_struct_build.leaf_pipeline,
                        &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device), state->accel_struct_build.morton_pipeline,
                        &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device),
                              state->accel_struct_build.copy_p_layout, &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device),
                              state->accel_struct_build.internal_p_layout, &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device),
                              state->accel_struct_build.leaf_p_layout, &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device),
                              state->accel_struct_build.morton_p_layout, &state->alloc);

   if (state->accel_struct_build.radix_sort)
      radix_sort_vk_destroy(state->accel_struct_build.radix_sort, radv_device_to_handle(device),
                            &state->alloc);
}

static VkResult
create_build_pipeline(struct radv_device *device, nir_shader *shader, unsigned push_constant_size,
                      VkPipeline *pipeline, VkPipelineLayout *layout)
{
   const VkPipelineLayoutCreateInfo pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges =
         &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, push_constant_size},
   };

   VkResult result = radv_CreatePipelineLayout(radv_device_to_handle(device), &pl_create_info,
                                               &device->meta_state.alloc, layout);
   if (result != VK_SUCCESS) {
      ralloc_free(shader);
      return result;
   }

   VkPipelineShaderStageCreateInfo shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(shader),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = shader_stage,
      .flags = 0,
      .layout = *layout,
   };

   result = radv_CreateComputePipelines(radv_device_to_handle(device),
                                        radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
                                        &pipeline_info, &device->meta_state.alloc, pipeline);

   if (result != VK_SUCCESS) {
      ralloc_free(shader);
      return result;
   }

   return VK_SUCCESS;
}

static VkResult
create_build_pipeline_spv(struct radv_device *device, const uint32_t *spv, uint32_t spv_size,
                          unsigned push_constant_size, VkPipeline *pipeline,
                          VkPipelineLayout *layout)
{
   const VkPipelineLayoutCreateInfo pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges =
         &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, push_constant_size},
   };

   VkShaderModuleCreateInfo module_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .codeSize = spv_size,
      .pCode = spv,
   };

   VkShaderModule module;
   VkResult result = device->vk.dispatch_table.CreateShaderModule(
      radv_device_to_handle(device), &module_info, &device->meta_state.alloc, &module);
   if (result != VK_SUCCESS)
      return result;

   result = radv_CreatePipelineLayout(radv_device_to_handle(device), &pl_create_info,
                                      &device->meta_state.alloc, layout);
   if (result != VK_SUCCESS)
      goto cleanup;

   VkPipelineShaderStageCreateInfo shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = module,
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = shader_stage,
      .flags = 0,
      .layout = *layout,
   };

   result = radv_CreateComputePipelines(radv_device_to_handle(device),
                                        radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
                                        &pipeline_info, &device->meta_state.alloc, pipeline);

cleanup:
   device->vk.dispatch_table.DestroyShaderModule(radv_device_to_handle(device), module,
                                                 &device->meta_state.alloc);
   return result;
}

static void
radix_sort_fill_buffer(VkCommandBuffer commandBuffer,
                       radix_sort_vk_buffer_info_t const *buffer_info, VkDeviceSize offset,
                       VkDeviceSize size, uint32_t data)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

   assert(size != VK_WHOLE_SIZE);

   radv_fill_buffer(cmd_buffer, NULL, NULL, buffer_info->devaddr + buffer_info->offset + offset,
                    size, data);
}

VkResult
radv_device_init_accel_struct_build_state(struct radv_device *device)
{
   VkResult result;
   nir_shader *copy_cs = build_copy_shader(device);

   result = create_build_pipeline_spv(device, leaf_spv, sizeof(leaf_spv), sizeof(struct leaf_args),
                                      &device->meta_state.accel_struct_build.leaf_pipeline,
                                      &device->meta_state.accel_struct_build.leaf_p_layout);
   if (result != VK_SUCCESS)
      return result;

   result = create_build_pipeline_spv(device, internal_spv, sizeof(internal_spv),
                                      sizeof(struct internal_args),
                                      &device->meta_state.accel_struct_build.internal_pipeline,
                                      &device->meta_state.accel_struct_build.internal_p_layout);
   if (result != VK_SUCCESS)
      return result;

   result = create_build_pipeline(device, copy_cs, sizeof(struct copy_constants),
                                  &device->meta_state.accel_struct_build.copy_pipeline,
                                  &device->meta_state.accel_struct_build.copy_p_layout);

   if (result != VK_SUCCESS)
      return result;

   result =
      create_build_pipeline_spv(device, morton_spv, sizeof(morton_spv), sizeof(struct morton_args),
                                &device->meta_state.accel_struct_build.morton_pipeline,
                                &device->meta_state.accel_struct_build.morton_p_layout);
   if (result != VK_SUCCESS)
      return result;

   device->meta_state.accel_struct_build.radix_sort =
      radv_create_radix_sort_u64(radv_device_to_handle(device), &device->meta_state.alloc,
                                 radv_pipeline_cache_to_handle(&device->meta_state.cache));

   struct radix_sort_vk_sort_devaddr_info *radix_sort_info =
      &device->meta_state.accel_struct_build.radix_sort_info;
   radix_sort_info->ext = NULL;
   radix_sort_info->key_bits = 24;
   radix_sort_info->fill_buffer = radix_sort_fill_buffer;

   return result;
}

struct bvh_state {
   uint32_t node_offset;
   uint32_t node_count;
   uint32_t scratch_offset;
   uint32_t buffer_1_offset;
   uint32_t buffer_2_offset;

   uint32_t leaf_node_offset;
   uint32_t leaf_node_count;
   uint32_t internal_node_count;
};

VKAPI_ATTR void VKAPI_CALL
radv_CmdBuildAccelerationStructuresKHR(
   VkCommandBuffer commandBuffer, uint32_t infoCount,
   const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
   const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_meta_saved_state saved_state;

   enum radv_cmd_flush_bits flush_bits =
      RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
      radv_src_access_flush(cmd_buffer, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                            NULL) |
      radv_dst_access_flush(cmd_buffer, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                            NULL);

   radv_meta_save(
      &saved_state, cmd_buffer,
      RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);
   struct bvh_state *bvh_states = calloc(infoCount, sizeof(struct bvh_state));

   for (uint32_t i = 0; i < infoCount; ++i) {
      /* Clear the bvh bounds with int max/min. */
      si_cp_dma_clear_buffer(cmd_buffer, pInfos[i].scratchData.deviceAddress, 3 * sizeof(float),
                             0x7fffffff);
      si_cp_dma_clear_buffer(cmd_buffer, pInfos[i].scratchData.deviceAddress + 3 * sizeof(float),
                             3 * sizeof(float), 0x80000000);
   }

   cmd_buffer->state.flush_bits |= flush_bits;

   radv_CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                        cmd_buffer->device->meta_state.accel_struct_build.leaf_pipeline);

   for (uint32_t i = 0; i < infoCount; ++i) {
      RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct,
                       pInfos[i].dstAccelerationStructure);

      struct leaf_args leaf_consts = {
         .bvh = accel_struct->va,
         .bounds = pInfos[i].scratchData.deviceAddress,
         .ids = pInfos[i].scratchData.deviceAddress + SCRATCH_TOTAL_BOUNDS_SIZE,
         .dst_offset =
            ALIGN(sizeof(struct radv_accel_struct_header), 64) + sizeof(struct radv_bvh_box32_node),
      };
      bvh_states[i].node_offset = leaf_consts.dst_offset;
      bvh_states[i].leaf_node_offset = leaf_consts.dst_offset;

      for (unsigned j = 0; j < pInfos[i].geometryCount; ++j) {
         const VkAccelerationStructureGeometryKHR *geom =
            pInfos[i].pGeometries ? &pInfos[i].pGeometries[j] : pInfos[i].ppGeometries[j];

         const VkAccelerationStructureBuildRangeInfoKHR *buildRangeInfo = &ppBuildRangeInfos[i][j];

         leaf_consts.first_id = bvh_states[i].node_count;

         leaf_consts.geometry_type = geom->geometryType;
         leaf_consts.geometry_id = j | (geom->flags << 28);
         unsigned prim_size;
         switch (geom->geometryType) {
         case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
            assert(pInfos[i].type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);

            leaf_consts.data = geom->geometry.triangles.vertexData.deviceAddress +
                               buildRangeInfo->firstVertex * geom->geometry.triangles.vertexStride;
            leaf_consts.indices = geom->geometry.triangles.indexData.deviceAddress;

            if (geom->geometry.triangles.indexType == VK_INDEX_TYPE_NONE_KHR)
               leaf_consts.data += buildRangeInfo->primitiveOffset;
            else
               leaf_consts.indices += buildRangeInfo->primitiveOffset;

            leaf_consts.transform = geom->geometry.triangles.transformData.deviceAddress;
            if (leaf_consts.transform)
               leaf_consts.transform += buildRangeInfo->transformOffset;

            leaf_consts.stride = geom->geometry.triangles.vertexStride;
            leaf_consts.vertex_format = geom->geometry.triangles.vertexFormat;
            leaf_consts.index_format = geom->geometry.triangles.indexType;

            prim_size = sizeof(struct radv_bvh_triangle_node);
            break;
         case VK_GEOMETRY_TYPE_AABBS_KHR:
            assert(pInfos[i].type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);

            leaf_consts.data =
               geom->geometry.aabbs.data.deviceAddress + buildRangeInfo->primitiveOffset;
            leaf_consts.stride = geom->geometry.aabbs.stride;

            prim_size = sizeof(struct radv_bvh_aabb_node);
            break;
         case VK_GEOMETRY_TYPE_INSTANCES_KHR:
            assert(pInfos[i].type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR);

            leaf_consts.data =
               geom->geometry.instances.data.deviceAddress + buildRangeInfo->primitiveOffset;

            if (geom->geometry.instances.arrayOfPointers)
               leaf_consts.stride = 8;
            else
               leaf_consts.stride = sizeof(VkAccelerationStructureInstanceKHR);

            prim_size = sizeof(struct radv_bvh_instance_node);
            break;
         default:
            unreachable("Unknown geometryType");
         }

         radv_CmdPushConstants(commandBuffer,
                               cmd_buffer->device->meta_state.accel_struct_build.leaf_p_layout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(leaf_consts), &leaf_consts);
         radv_unaligned_dispatch(cmd_buffer, buildRangeInfo->primitiveCount, 1, 1);

         leaf_consts.dst_offset += prim_size * buildRangeInfo->primitiveCount;

         bvh_states[i].leaf_node_count += buildRangeInfo->primitiveCount;
         bvh_states[i].node_count += buildRangeInfo->primitiveCount;
      }
      bvh_states[i].node_offset = leaf_consts.dst_offset;
   }

   cmd_buffer->state.flush_bits |= flush_bits;

   radv_CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                        cmd_buffer->device->meta_state.accel_struct_build.morton_pipeline);

   for (uint32_t i = 0; i < infoCount; ++i) {
      RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct,
                       pInfos[i].dstAccelerationStructure);

      const struct morton_args consts = {
         .bvh = accel_struct->va,
         .bounds = pInfos[i].scratchData.deviceAddress,
         .ids = pInfos[i].scratchData.deviceAddress + SCRATCH_TOTAL_BOUNDS_SIZE,
      };

      radv_CmdPushConstants(commandBuffer,
                            cmd_buffer->device->meta_state.accel_struct_build.morton_p_layout,
                            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);
      radv_unaligned_dispatch(cmd_buffer, bvh_states[i].node_count, 1, 1);
   }

   cmd_buffer->state.flush_bits |= flush_bits;

   for (uint32_t i = 0; i < infoCount; ++i) {
      struct radix_sort_vk_memory_requirements requirements;
      radix_sort_vk_get_memory_requirements(
         cmd_buffer->device->meta_state.accel_struct_build.radix_sort, bvh_states[i].node_count,
         &requirements);

      struct radix_sort_vk_sort_devaddr_info info =
         cmd_buffer->device->meta_state.accel_struct_build.radix_sort_info;
      info.count = bvh_states[i].node_count;

      VkDeviceAddress base_addr = pInfos[i].scratchData.deviceAddress + SCRATCH_TOTAL_BOUNDS_SIZE;

      info.keyvals_even.buffer = VK_NULL_HANDLE;
      info.keyvals_even.offset = 0;
      info.keyvals_even.devaddr = base_addr;

      info.keyvals_odd = base_addr + requirements.keyvals_size;

      info.internal.buffer = VK_NULL_HANDLE;
      info.internal.offset = 0;
      info.internal.devaddr = base_addr + requirements.keyvals_size * 2;

      VkDeviceAddress result_addr;
      radix_sort_vk_sort_devaddr(cmd_buffer->device->meta_state.accel_struct_build.radix_sort,
                                 &info, radv_device_to_handle(cmd_buffer->device), commandBuffer,
                                 &result_addr);

      assert(result_addr == info.keyvals_even.devaddr || result_addr == info.keyvals_odd);

      if (result_addr == info.keyvals_even.devaddr) {
         bvh_states[i].buffer_1_offset = SCRATCH_TOTAL_BOUNDS_SIZE;
         bvh_states[i].buffer_2_offset = SCRATCH_TOTAL_BOUNDS_SIZE + requirements.keyvals_size;
      } else {
         bvh_states[i].buffer_1_offset = SCRATCH_TOTAL_BOUNDS_SIZE + requirements.keyvals_size;
         bvh_states[i].buffer_2_offset = SCRATCH_TOTAL_BOUNDS_SIZE;
      }
      bvh_states[i].scratch_offset = bvh_states[i].buffer_1_offset;
   }

   cmd_buffer->state.flush_bits |= flush_bits;

   radv_CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                        cmd_buffer->device->meta_state.accel_struct_build.internal_pipeline);
   bool progress = true;
   for (unsigned iter = 0; progress; ++iter) {
      progress = false;
      for (uint32_t i = 0; i < infoCount; ++i) {
         RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct,
                          pInfos[i].dstAccelerationStructure);

         if (iter && bvh_states[i].node_count == 1)
            continue;

         if (!progress)
            cmd_buffer->state.flush_bits |= flush_bits;

         progress = true;

         uint32_t dst_node_count = MAX2(1, DIV_ROUND_UP(bvh_states[i].node_count, 4));
         bool final_iter = dst_node_count == 1;

         uint32_t src_scratch_offset = bvh_states[i].scratch_offset;
         uint32_t buffer_1_offset = bvh_states[i].buffer_1_offset;
         uint32_t buffer_2_offset = bvh_states[i].buffer_2_offset;
         uint32_t dst_scratch_offset =
            (src_scratch_offset == buffer_1_offset) ? buffer_2_offset : buffer_1_offset;

         uint32_t dst_node_offset = bvh_states[i].node_offset;
         if (final_iter)
            dst_node_offset = ALIGN(sizeof(struct radv_accel_struct_header), 64);

         const struct internal_args consts = {
            .bvh = accel_struct->va,
            .src_ids = pInfos[i].scratchData.deviceAddress + src_scratch_offset,
            .dst_ids = pInfos[i].scratchData.deviceAddress + dst_scratch_offset,
            .dst_offset = dst_node_offset,
            .fill_count = bvh_states[i].node_count | (final_iter ? 0x80000000U : 0),
         };

         radv_CmdPushConstants(commandBuffer,
                               cmd_buffer->device->meta_state.accel_struct_build.internal_p_layout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);
         radv_unaligned_dispatch(cmd_buffer, dst_node_count, 1, 1);
         if (!final_iter)
            bvh_states[i].node_offset += dst_node_count * 128;
         bvh_states[i].node_count = dst_node_count;
         bvh_states[i].internal_node_count += dst_node_count;
         bvh_states[i].scratch_offset = dst_scratch_offset;
      }
   }
   for (uint32_t i = 0; i < infoCount; ++i) {
      RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct,
                       pInfos[i].dstAccelerationStructure);
      const size_t base = offsetof(struct radv_accel_struct_header, compacted_size);
      struct radv_accel_struct_header header;

      bool is_tlas = pInfos[i].type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

      uint64_t geometry_infos_size =
         pInfos[i].geometryCount * sizeof(struct radv_accel_struct_geometry_info);

      header.instance_offset = bvh_states[i].leaf_node_offset;
      header.instance_count = is_tlas ? bvh_states[i].leaf_node_count : 0;
      header.compacted_size = bvh_states[i].node_offset + geometry_infos_size;

      header.copy_dispatch_size[0] = DIV_ROUND_UP(header.compacted_size, 16 * 64);
      header.copy_dispatch_size[1] = 1;
      header.copy_dispatch_size[2] = 1;

      header.serialization_size =
         header.compacted_size + align(sizeof(struct radv_accel_struct_serialization_header) +
                                          sizeof(uint64_t) * header.instance_count,
                                       128);

      header.size = header.serialization_size -
                    sizeof(struct radv_accel_struct_serialization_header) -
                    sizeof(uint64_t) * header.instance_count;

      header.build_flags = pInfos[i].flags;
      header.geometry_count = pInfos[i].geometryCount;
      header.internal_node_count = bvh_states[i].internal_node_count;

      struct radv_accel_struct_geometry_info *geometry_infos = malloc(geometry_infos_size);
      if (!geometry_infos)
         goto fail;

      for (uint32_t j = 0; j < pInfos[i].geometryCount; ++j) {
         const VkAccelerationStructureGeometryKHR *geometry =
            pInfos[i].pGeometries ? pInfos[i].pGeometries + j : pInfos[i].ppGeometries[j];
         geometry_infos[j].type = geometry->geometryType;
         geometry_infos[j].flags = geometry->flags;
         geometry_infos[j].primitive_count = ppBuildRangeInfos[i][j].primitiveCount;
      }

      radv_update_buffer_cp(cmd_buffer,
                            radv_buffer_get_va(accel_struct->bo) + accel_struct->mem_offset + base,
                            (const char *)&header + base, sizeof(header) - base);

      struct radv_buffer accel_struct_buffer;
      radv_buffer_init(&accel_struct_buffer, cmd_buffer->device, accel_struct->bo,
                       accel_struct->size, accel_struct->mem_offset);
      radv_CmdUpdateBuffer(commandBuffer, radv_buffer_to_handle(&accel_struct_buffer),
                           bvh_states[i].node_offset, geometry_infos_size, geometry_infos);
      radv_buffer_finish(&accel_struct_buffer);

      free(geometry_infos);
   }

fail:
   free(bvh_states);
   radv_meta_restore(&saved_state, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                     const VkCopyAccelerationStructureInfoKHR *pInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_acceleration_structure, src, pInfo->src);
   RADV_FROM_HANDLE(radv_acceleration_structure, dst, pInfo->dst);
   struct radv_meta_saved_state saved_state;

   radv_meta_save(
      &saved_state, cmd_buffer,
      RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        cmd_buffer->device->meta_state.accel_struct_build.copy_pipeline);

   const struct copy_constants consts = {
      .src_addr = src->va,
      .dst_addr = dst->va,
      .mode = COPY_MODE_COPY,
   };

   radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                         cmd_buffer->device->meta_state.accel_struct_build.copy_p_layout,
                         VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);

   cmd_buffer->state.flush_bits |=
      radv_dst_access_flush(cmd_buffer, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, NULL);

   radv_indirect_dispatch(cmd_buffer, src->bo,
                          src->va + offsetof(struct radv_accel_struct_header, copy_dispatch_size));
   radv_meta_restore(&saved_state, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDeviceAccelerationStructureCompatibilityKHR(
   VkDevice _device, const VkAccelerationStructureVersionInfoKHR *pVersionInfo,
   VkAccelerationStructureCompatibilityKHR *pCompatibility)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   uint8_t zero[VK_UUID_SIZE] = {
      0,
   };
   bool compat =
      memcmp(pVersionInfo->pVersionData, device->physical_device->driver_uuid, VK_UUID_SIZE) == 0 &&
      memcmp(pVersionInfo->pVersionData + VK_UUID_SIZE, zero, VK_UUID_SIZE) == 0;
   *pCompatibility = compat ? VK_ACCELERATION_STRUCTURE_COMPATIBILITY_COMPATIBLE_KHR
                            : VK_ACCELERATION_STRUCTURE_COMPATIBILITY_INCOMPATIBLE_KHR;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CopyMemoryToAccelerationStructureKHR(VkDevice _device,
                                          VkDeferredOperationKHR deferredOperation,
                                          const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo)
{
   unreachable("Unimplemented");
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CopyAccelerationStructureToMemoryKHR(VkDevice _device,
                                          VkDeferredOperationKHR deferredOperation,
                                          const VkCopyAccelerationStructureToMemoryInfoKHR *pInfo)
{
   unreachable("Unimplemented");
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyMemoryToAccelerationStructureKHR(
   VkCommandBuffer commandBuffer, const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_acceleration_structure, dst, pInfo->dst);
   struct radv_meta_saved_state saved_state;

   radv_meta_save(
      &saved_state, cmd_buffer,
      RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        cmd_buffer->device->meta_state.accel_struct_build.copy_pipeline);

   const struct copy_constants consts = {
      .src_addr = pInfo->src.deviceAddress,
      .dst_addr = dst->va,
      .mode = COPY_MODE_DESERIALIZE,
   };

   radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                         cmd_buffer->device->meta_state.accel_struct_build.copy_p_layout,
                         VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);

   radv_CmdDispatch(commandBuffer, 512, 1, 1);
   radv_meta_restore(&saved_state, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyAccelerationStructureToMemoryKHR(
   VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureToMemoryInfoKHR *pInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_acceleration_structure, src, pInfo->src);
   struct radv_meta_saved_state saved_state;

   radv_meta_save(
      &saved_state, cmd_buffer,
      RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        cmd_buffer->device->meta_state.accel_struct_build.copy_pipeline);

   const struct copy_constants consts = {
      .src_addr = src->va,
      .dst_addr = pInfo->dst.deviceAddress,
      .mode = COPY_MODE_SERIALIZE,
   };

   radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                         cmd_buffer->device->meta_state.accel_struct_build.copy_p_layout,
                         VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);

   cmd_buffer->state.flush_bits |=
      radv_dst_access_flush(cmd_buffer, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, NULL);

   radv_indirect_dispatch(cmd_buffer, src->bo,
                          src->va + offsetof(struct radv_accel_struct_header, copy_dispatch_size));
   radv_meta_restore(&saved_state, cmd_buffer);

   /* Set the header of the serialized data. */
   uint8_t header_data[2 * VK_UUID_SIZE] = {0};
   memcpy(header_data, cmd_buffer->device->physical_device->driver_uuid, VK_UUID_SIZE);

   radv_update_buffer_cp(cmd_buffer, pInfo->dst.deviceAddress, header_data, sizeof(header_data));
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdBuildAccelerationStructuresIndirectKHR(
   VkCommandBuffer commandBuffer, uint32_t infoCount,
   const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
   const VkDeviceAddress *pIndirectDeviceAddresses, const uint32_t *pIndirectStrides,
   const uint32_t *const *ppMaxPrimitiveCounts)
{
   unreachable("Unimplemented");
}
