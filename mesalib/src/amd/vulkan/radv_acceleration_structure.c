/*
 * Copyright © 2021 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#include "meta/radv_meta.h"
#include "radv_cs.h"
#include "radv_entrypoints.h"

#include "radix_sort/radix_sort_u64.h"

#include "bvh/build_interface.h"
#include "bvh/bvh.h"

#include "vk_acceleration_structure.h"
#include "vk_common_entrypoints.h"

static const uint32_t copy_spv[] = {
#include "bvh/copy.spv.h"
};

static const uint32_t encode_spv[] = {
#include "bvh/encode.spv.h"
};

static const uint32_t encode_compact_spv[] = {
#include "bvh/encode_compact.spv.h"
};

static const uint32_t header_spv[] = {
#include "bvh/header.spv.h"
};

static const uint32_t update_spv[] = {
#include "bvh/update.spv.h"
};

struct acceleration_structure_layout {
   uint32_t geometry_info_offset;
   uint32_t bvh_offset;
   uint32_t leaf_nodes_offset;
   uint32_t internal_nodes_offset;
   uint32_t size;
};

struct scratch_layout {
   uint32_t update_size;
   uint32_t header_offset;
   uint32_t internal_ready_count_offset;
};

enum radv_encode_key_bits {
   RADV_ENCODE_KEY_COMPACT = 1,
};

static void
radv_get_acceleration_structure_layout(struct radv_device *device, uint32_t leaf_count,
                                       const VkAccelerationStructureBuildGeometryInfoKHR *build_info,
                                       struct acceleration_structure_layout *accel_struct)
{
   uint32_t internal_count = MAX2(leaf_count, 2) - 1;

   VkGeometryTypeKHR geometry_type = vk_get_as_geometry_type(build_info);

   uint32_t bvh_leaf_size;
   switch (geometry_type) {
   case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
      bvh_leaf_size = sizeof(struct radv_bvh_triangle_node);
      break;
   case VK_GEOMETRY_TYPE_AABBS_KHR:
      bvh_leaf_size = sizeof(struct radv_bvh_aabb_node);
      break;
   case VK_GEOMETRY_TYPE_INSTANCES_KHR:
      bvh_leaf_size = sizeof(struct radv_bvh_instance_node);
      break;
   default:
      unreachable("Unknown VkGeometryTypeKHR");
   }

   uint64_t bvh_size = bvh_leaf_size * leaf_count + sizeof(struct radv_bvh_box32_node) * internal_count;
   uint32_t offset = 0;
   offset += sizeof(struct radv_accel_struct_header);

   if (device->rra_trace.accel_structs) {
      accel_struct->geometry_info_offset = offset;
      offset += sizeof(struct radv_accel_struct_geometry_info) * build_info->geometryCount;
   }
   /* Parent links, which have to go directly before bvh_offset as we index them using negative
    * offsets from there. */
   offset += bvh_size / 64 * 4;

   /* The BVH and hence bvh_offset needs 64 byte alignment for RT nodes. */
   offset = ALIGN(offset, 64);
   accel_struct->bvh_offset = offset;

   /* root node */
   offset += sizeof(struct radv_bvh_box32_node);

   accel_struct->leaf_nodes_offset = offset;
   offset += bvh_leaf_size * leaf_count;

   accel_struct->internal_nodes_offset = offset;
   /* Factor out the root node. */
   offset += sizeof(struct radv_bvh_box32_node) * (internal_count - 1);

   accel_struct->size = offset;
}

static void
radv_get_scratch_layout(struct radv_device *device, uint32_t leaf_count, struct scratch_layout *scratch)
{
   uint32_t internal_count = MAX2(leaf_count, 2) - 1;

   uint32_t offset = 0;

   scratch->header_offset = offset;
   offset += sizeof(struct vk_ir_header);

   uint32_t update_offset = 0;

   update_offset += sizeof(vk_aabb) * leaf_count;
   scratch->internal_ready_count_offset = update_offset;

   update_offset += sizeof(uint32_t) * internal_count;
   scratch->update_size = update_offset;
}

VKAPI_ATTR void VKAPI_CALL
radv_GetAccelerationStructureBuildSizesKHR(VkDevice _device, VkAccelerationStructureBuildTypeKHR buildType,
                                           const VkAccelerationStructureBuildGeometryInfoKHR *pBuildInfo,
                                           const uint32_t *pMaxPrimitiveCounts,
                                           VkAccelerationStructureBuildSizesInfoKHR *pSizeInfo)
{
   VK_FROM_HANDLE(radv_device, device, _device);

   STATIC_ASSERT(sizeof(struct radv_bvh_triangle_node) == 64);
   STATIC_ASSERT(sizeof(struct radv_bvh_aabb_node) == 64);
   STATIC_ASSERT(sizeof(struct radv_bvh_instance_node) == 128);
   STATIC_ASSERT(sizeof(struct radv_bvh_box16_node) == 64);
   STATIC_ASSERT(sizeof(struct radv_bvh_box32_node) == 128);

   if (radv_device_init_accel_struct_build_state(device) != VK_SUCCESS)
      return;

   vk_get_as_build_sizes(_device, buildType, pBuildInfo, pMaxPrimitiveCounts, pSizeInfo,
                         &device->meta_state.accel_struct_build.build_args);
}

void
radv_device_finish_accel_struct_build_state(struct radv_device *device)
{
   VkDevice _device = radv_device_to_handle(device);
   struct radv_meta_state *state = &device->meta_state;
   struct vk_device_dispatch_table *dispatch = &device->vk.dispatch_table;

   dispatch->DestroyPipeline(_device, state->accel_struct_build.copy_pipeline, &state->alloc);
   dispatch->DestroyPipeline(_device, state->accel_struct_build.encode_pipeline, &state->alloc);
   dispatch->DestroyPipeline(_device, state->accel_struct_build.encode_compact_pipeline, &state->alloc);
   dispatch->DestroyPipeline(_device, state->accel_struct_build.header_pipeline, &state->alloc);
   dispatch->DestroyPipeline(_device, state->accel_struct_build.update_pipeline, &state->alloc);
   radv_DestroyPipelineLayout(_device, state->accel_struct_build.copy_p_layout, &state->alloc);
   radv_DestroyPipelineLayout(_device, state->accel_struct_build.encode_p_layout, &state->alloc);
   radv_DestroyPipelineLayout(_device, state->accel_struct_build.header_p_layout, &state->alloc);
   radv_DestroyPipelineLayout(_device, state->accel_struct_build.update_p_layout, &state->alloc);

   if (state->accel_struct_build.radix_sort)
      radix_sort_vk_destroy(state->accel_struct_build.radix_sort, _device, &state->alloc);

   radv_DestroyBuffer(_device, state->accel_struct_build.null.buffer, &state->alloc);
   radv_FreeMemory(_device, state->accel_struct_build.null.memory, &state->alloc);
   vk_common_DestroyAccelerationStructureKHR(_device, state->accel_struct_build.null.accel_struct, &state->alloc);
}

static VkResult
create_build_pipeline_spv(struct radv_device *device, const uint32_t *spv, uint32_t spv_size,
                          unsigned push_constant_size, VkPipeline *pipeline, VkPipelineLayout *layout)
{
   if (*pipeline)
      return VK_SUCCESS;

   VkDevice _device = radv_device_to_handle(device);

   const VkPipelineLayoutCreateInfo pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, push_constant_size},
   };

   VkShaderModuleCreateInfo module_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .codeSize = spv_size,
      .pCode = spv,
   };

   VkShaderModule module;
   VkResult result =
      device->vk.dispatch_table.CreateShaderModule(_device, &module_info, &device->meta_state.alloc, &module);
   if (result != VK_SUCCESS)
      return result;

   if (!*layout) {
      result = radv_CreatePipelineLayout(_device, &pl_create_info, &device->meta_state.alloc, layout);
      if (result != VK_SUCCESS)
         goto cleanup;
   }

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

   result = device->vk.dispatch_table.CreateComputePipelines(_device, device->meta_state.cache, 1, &pipeline_info,
                                                             &device->meta_state.alloc, pipeline);

cleanup:
   device->vk.dispatch_table.DestroyShaderModule(_device, module, &device->meta_state.alloc);
   return result;
}

VkResult
radv_device_init_null_accel_struct(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (pdev->memory_properties.memoryTypeCount == 0)
      return VK_SUCCESS; /* Exit in the case of null winsys. */

   VkDevice _device = radv_device_to_handle(device);

   uint32_t bvh_offset = ALIGN(sizeof(struct radv_accel_struct_header), 64);
   uint32_t size = bvh_offset + sizeof(struct radv_bvh_box32_node);

   VkResult result;

   VkBuffer buffer = VK_NULL_HANDLE;
   VkDeviceMemory memory = VK_NULL_HANDLE;
   VkAccelerationStructureKHR accel_struct = VK_NULL_HANDLE;

   VkBufferCreateInfo buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext =
         &(VkBufferUsageFlags2CreateInfo){
            .sType = VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO,
            .usage = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
         },
      .size = size,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };

   result = radv_CreateBuffer(_device, &buffer_create_info, &device->meta_state.alloc, &buffer);
   if (result != VK_SUCCESS)
      return result;

   VkBufferMemoryRequirementsInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
      .buffer = buffer,
   };
   VkMemoryRequirements2 mem_req = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
   };
   vk_common_GetBufferMemoryRequirements2(_device, &info, &mem_req);

   VkMemoryAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mem_req.memoryRequirements.size,
      .memoryTypeIndex =
         radv_find_memory_index(pdev, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
   };

   result = radv_AllocateMemory(_device, &alloc_info, &device->meta_state.alloc, &memory);
   if (result != VK_SUCCESS)
      return result;

   VkBindBufferMemoryInfo bind_info = {
      .sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
      .buffer = buffer,
      .memory = memory,
   };

   result = radv_BindBufferMemory2(_device, 1, &bind_info);
   if (result != VK_SUCCESS)
      return result;

   void *data;
   result = vk_common_MapMemory(_device, memory, 0, size, 0, &data);
   if (result != VK_SUCCESS)
      return result;

   struct radv_accel_struct_header header = {
      .bvh_offset = bvh_offset,
   };
   memcpy(data, &header, sizeof(struct radv_accel_struct_header));

   struct radv_bvh_box32_node root = {
      .children =
         {
            RADV_BVH_INVALID_NODE,
            RADV_BVH_INVALID_NODE,
            RADV_BVH_INVALID_NODE,
            RADV_BVH_INVALID_NODE,
         },
   };

   for (uint32_t child = 0; child < 4; child++) {
      root.coords[child] = (vk_aabb){
         .min.x = NAN,
         .min.y = NAN,
         .min.z = NAN,
         .max.x = NAN,
         .max.y = NAN,
         .max.z = NAN,
      };
   }

   memcpy((uint8_t *)data + bvh_offset, &root, sizeof(struct radv_bvh_box32_node));

   vk_common_UnmapMemory(_device, memory);

   VkAccelerationStructureCreateInfoKHR create_info = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
      .buffer = buffer,
      .size = size,
      .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
   };

   result = vk_common_CreateAccelerationStructureKHR(_device, &create_info, &device->meta_state.alloc, &accel_struct);
   if (result != VK_SUCCESS)
      return result;

   device->meta_state.accel_struct_build.null.buffer = buffer;
   device->meta_state.accel_struct_build.null.memory = memory;
   device->meta_state.accel_struct_build.null.accel_struct = accel_struct;

   return VK_SUCCESS;
}

static VkDeviceSize
radv_get_as_size(VkDevice _device, const VkAccelerationStructureBuildGeometryInfoKHR *pBuildInfo, uint32_t leaf_count)
{
   VK_FROM_HANDLE(radv_device, device, _device);

   struct acceleration_structure_layout accel_struct;
   radv_get_acceleration_structure_layout(device, leaf_count, pBuildInfo, &accel_struct);
   return accel_struct.size;
}

static VkDeviceSize
radv_get_update_scratch_size(struct vk_device *vk_device, uint32_t leaf_count)
{
   struct radv_device *device = container_of(vk_device, struct radv_device, vk);

   struct scratch_layout scratch;
   radv_get_scratch_layout(device, leaf_count, &scratch);
   return scratch.update_size;
}

static uint32_t
radv_get_encode_key(VkAccelerationStructureTypeKHR type, VkBuildAccelerationStructureFlagBitsKHR flags)
{
   if (flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)
      return RADV_ENCODE_KEY_COMPACT;

   return 0;
}

static VkResult
radv_encode_bind_pipeline(VkCommandBuffer commandBuffer, uint32_t key)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   bool compact = key & RADV_ENCODE_KEY_COMPACT;
   device->vk.dispatch_table.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                             compact ? device->meta_state.accel_struct_build.encode_compact_pipeline
                                                     : device->meta_state.accel_struct_build.encode_pipeline);

   return VK_SUCCESS;
}

static void
radv_encode_as(VkCommandBuffer commandBuffer, const VkAccelerationStructureBuildGeometryInfoKHR *build_info,
               const VkAccelerationStructureBuildRangeInfoKHR *build_range_infos, VkDeviceAddress intermediate_as_addr,
               VkDeviceAddress intermediate_header_addr, uint32_t leaf_count, uint32_t key,
               struct vk_acceleration_structure *dst)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   struct acceleration_structure_layout layout;
   radv_get_acceleration_structure_layout(device, leaf_count, build_info, &layout);

   if (key & RADV_ENCODE_KEY_COMPACT) {
      uint32_t dst_offset = layout.internal_nodes_offset - layout.bvh_offset;
      radv_update_buffer_cp(cmd_buffer, intermediate_header_addr + offsetof(struct vk_ir_header, dst_node_offset),
                            &dst_offset, sizeof(uint32_t));
   }

   const struct encode_args args = {
      .intermediate_bvh = intermediate_as_addr,
      .output_bvh = vk_acceleration_structure_get_va(dst) + layout.bvh_offset,
      .header = intermediate_header_addr,
      .output_bvh_offset = layout.bvh_offset,
      .leaf_node_count = leaf_count,
      .geometry_type = vk_get_as_geometry_type(build_info),
   };
   vk_common_CmdPushConstants(commandBuffer, device->meta_state.accel_struct_build.encode_p_layout,
                              VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(args), &args);

   struct radv_dispatch_info dispatch = {
      .unaligned = true,
      .ordered = true,
      .blocks = {MAX2(leaf_count, 1), 1, 1},
   };

   radv_compute_dispatch(cmd_buffer, &dispatch);
}

static VkResult
radv_init_header_bind_pipeline(VkCommandBuffer commandBuffer, uint32_t key)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   if (!(key & RADV_ENCODE_KEY_COMPACT))
      return VK_SUCCESS;

   /* Wait for encoding to finish. */
   cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
                                   radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                         VK_ACCESS_2_SHADER_WRITE_BIT, 0, NULL, NULL) |
                                   radv_dst_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                         VK_ACCESS_2_SHADER_READ_BIT, 0, NULL, NULL);

   device->vk.dispatch_table.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                             device->meta_state.accel_struct_build.header_pipeline);

   return VK_SUCCESS;
}

static void
radv_init_header(VkCommandBuffer commandBuffer, const VkAccelerationStructureBuildGeometryInfoKHR *build_info,
                 const VkAccelerationStructureBuildRangeInfoKHR *build_range_infos,
                 VkDeviceAddress intermediate_as_addr, VkDeviceAddress intermediate_header_addr, uint32_t leaf_count,
                 uint32_t key, struct vk_acceleration_structure *dst)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   size_t base = offsetof(struct radv_accel_struct_header, compacted_size);

   uint64_t instance_count = build_info->type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR ? leaf_count : 0;

   struct acceleration_structure_layout layout;
   radv_get_acceleration_structure_layout(device, leaf_count, build_info, &layout);

   if (key & RADV_ENCODE_KEY_COMPACT) {
      base = offsetof(struct radv_accel_struct_header, geometry_count);

      struct header_args args = {
         .src = intermediate_header_addr,
         .dst = vk_acceleration_structure_get_va(dst),
         .bvh_offset = layout.bvh_offset,
         .instance_count = instance_count,
      };

      vk_common_CmdPushConstants(commandBuffer, device->meta_state.accel_struct_build.header_p_layout,
                                 VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(args), &args);

      radv_unaligned_dispatch(cmd_buffer, 1, 1, 1);
   }

   struct radv_accel_struct_header header;

   header.instance_offset = layout.bvh_offset + sizeof(struct radv_bvh_box32_node);
   header.instance_count = instance_count;
   header.compacted_size = layout.size;

   header.copy_dispatch_size[0] = DIV_ROUND_UP(header.compacted_size, 16 * 64);
   header.copy_dispatch_size[1] = 1;
   header.copy_dispatch_size[2] = 1;

   header.serialization_size =
      header.compacted_size +
      align(sizeof(struct radv_accel_struct_serialization_header) + sizeof(uint64_t) * header.instance_count, 128);

   header.size = header.serialization_size - sizeof(struct radv_accel_struct_serialization_header) -
                 sizeof(uint64_t) * header.instance_count;

   header.build_flags = build_info->flags;
   header.geometry_count = build_info->geometryCount;

   radv_update_buffer_cp(cmd_buffer, vk_acceleration_structure_get_va(dst) + base, (const char *)&header + base,
                         sizeof(header) - base);

   if (device->rra_trace.accel_structs) {
      uint64_t geometry_infos_size = build_info->geometryCount * sizeof(struct radv_accel_struct_geometry_info);

      struct radv_accel_struct_geometry_info *geometry_infos = malloc(geometry_infos_size);
      if (!geometry_infos)
         return;

      for (uint32_t i = 0; i < build_info->geometryCount; i++) {
         const VkAccelerationStructureGeometryKHR *geometry =
            build_info->pGeometries ? &build_info->pGeometries[i] : build_info->ppGeometries[i];
         geometry_infos[i].type = geometry->geometryType;
         geometry_infos[i].flags = geometry->flags;
         geometry_infos[i].primitive_count = build_range_infos[i].primitiveCount;
      }

      radv_CmdUpdateBuffer(commandBuffer, dst->buffer, dst->offset + layout.geometry_info_offset, geometry_infos_size,
                           geometry_infos);

      free(geometry_infos);
   }
}

static void
radv_init_update_scratch(VkCommandBuffer commandBuffer, VkDeviceAddress scratch, uint32_t leaf_count,
                         struct vk_acceleration_structure *src_as, struct vk_acceleration_structure *dst_as)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   struct scratch_layout layout;
   radv_get_scratch_layout(device, leaf_count, &layout);

   /* Prepare ready counts for internal nodes */
   radv_fill_buffer(cmd_buffer, NULL, NULL, scratch + layout.internal_ready_count_offset,
                    layout.update_size - layout.internal_ready_count_offset, 0x0);
}

static void
radv_update_bind_pipeline(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   /* Wait for update scratch initialization to finish.. */
   cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
                                   radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                         VK_ACCESS_2_SHADER_WRITE_BIT, 0, NULL, NULL) |
                                   radv_dst_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                         VK_ACCESS_2_SHADER_READ_BIT, 0, NULL, NULL);

   device->vk.dispatch_table.CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                             device->meta_state.accel_struct_build.update_pipeline);
}

static uint32_t
pack_geometry_id_and_flags(uint32_t geometry_id, uint32_t flags)
{
   uint32_t geometry_id_and_flags = geometry_id;
   if (flags & VK_GEOMETRY_OPAQUE_BIT_KHR)
      geometry_id_and_flags |= RADV_GEOMETRY_OPAQUE;

   return geometry_id_and_flags;
}

static void
radv_update_as(VkCommandBuffer commandBuffer, const VkAccelerationStructureBuildGeometryInfoKHR *build_info,
               const VkAccelerationStructureBuildRangeInfoKHR *build_range_infos, uint32_t leaf_count,
               struct vk_acceleration_structure *src, struct vk_acceleration_structure *dst)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   if (src != dst) {
      VK_FROM_HANDLE(radv_buffer, src_as_buffer, src->buffer);
      VK_FROM_HANDLE(radv_buffer, dst_as_buffer, dst->buffer);

      struct acceleration_structure_layout layout;
      radv_get_acceleration_structure_layout(device, leaf_count, build_info, &layout);

      /* Copy header/metadata */
      const uint64_t src_va = src_as_buffer->addr + src->offset;
      const uint64_t dst_va = dst_as_buffer->addr + dst->offset;

      radv_copy_buffer(cmd_buffer, src_as_buffer->bo, dst_as_buffer->bo, src_va, dst_va, layout.bvh_offset);
   }

   struct scratch_layout layout;
   radv_get_scratch_layout(device, leaf_count, &layout);

   struct update_args update_consts = {
      .src = vk_acceleration_structure_get_va(src),
      .dst = vk_acceleration_structure_get_va(dst),
      .leaf_bounds = build_info->scratchData.deviceAddress,
      .internal_ready_count = build_info->scratchData.deviceAddress + layout.internal_ready_count_offset,
      .leaf_node_count = leaf_count,
   };

   uint32_t first_id = 0;
   for (uint32_t i = 0; i < build_info->geometryCount; i++) {
      const VkAccelerationStructureGeometryKHR *geom =
         build_info->pGeometries ? &build_info->pGeometries[i] : build_info->ppGeometries[i];

      const VkAccelerationStructureBuildRangeInfoKHR *build_range_info = &build_range_infos[i];

      update_consts.geom_data = vk_fill_geometry_data(build_info->type, first_id, i, geom, build_range_info);

      vk_common_CmdPushConstants(commandBuffer, device->meta_state.accel_struct_build.update_p_layout,
                                 VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(update_consts), &update_consts);
      radv_unaligned_dispatch(cmd_buffer, build_range_info->primitiveCount, 1, 1);

      first_id += build_range_info->primitiveCount;
   }
}

static const struct radix_sort_vk_target_config radix_sort_config = {
   .keyval_dwords = 2,
   .fill.workgroup_size_log2 = 7,
   .fill.block_rows = 8,
   .histogram.workgroup_size_log2 = 8,
   .histogram.subgroup_size_log2 = 6,
   .histogram.block_rows = 14,
   .prefix.workgroup_size_log2 = 8,
   .prefix.subgroup_size_log2 = 6,
   .scatter.workgroup_size_log2 = 8,
   .scatter.subgroup_size_log2 = 6,
   .scatter.block_rows = 14,
};

static const struct vk_acceleration_structure_build_ops build_ops = {
   .begin_debug_marker = vk_accel_struct_cmd_begin_debug_marker,
   .end_debug_marker = vk_accel_struct_cmd_end_debug_marker,
   .get_as_size = radv_get_as_size,
   .get_update_scratch_size = radv_get_update_scratch_size,
   .get_encode_key[0] = radv_get_encode_key,
   .get_encode_key[1] = radv_get_encode_key,
   .encode_bind_pipeline[0] = radv_encode_bind_pipeline,
   .encode_bind_pipeline[1] = radv_init_header_bind_pipeline,
   .encode_as[0] = radv_encode_as,
   .encode_as[1] = radv_init_header,
   .init_update_scratch = radv_init_update_scratch,
   .update_bind_pipeline[0] = radv_update_bind_pipeline,
   .update_as[0] = radv_update_as,
};

static void
radv_write_buffer_cp(VkCommandBuffer commandBuffer, VkDeviceAddress addr, void *data, uint32_t size)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   radv_update_buffer_cp(cmd_buffer, addr, data, size);
}

static void
radv_flush_buffer_write_cp(VkCommandBuffer commandBuffer)
{
}

static void
radv_cmd_dispatch_unaligned(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   radv_unaligned_dispatch(cmd_buffer, x, y, z);
}

static void
radv_cmd_fill_buffer_addr(VkCommandBuffer commandBuffer, VkDeviceAddress addr, VkDeviceSize size, uint32_t data)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   radv_fill_buffer(cmd_buffer, NULL, NULL, addr, size, data);
}

VkResult
radv_device_init_accel_struct_build_state(struct radv_device *device)
{
   VkResult result = VK_SUCCESS;
   mtx_lock(&device->meta_state.mtx);

   if (device->meta_state.accel_struct_build.radix_sort)
      goto exit;

   result = create_build_pipeline_spv(device, encode_spv, sizeof(encode_spv), sizeof(struct encode_args),
                                      &device->meta_state.accel_struct_build.encode_pipeline,
                                      &device->meta_state.accel_struct_build.encode_p_layout);
   if (result != VK_SUCCESS)
      goto exit;

   result =
      create_build_pipeline_spv(device, encode_compact_spv, sizeof(encode_compact_spv), sizeof(struct encode_args),
                                &device->meta_state.accel_struct_build.encode_compact_pipeline,
                                &device->meta_state.accel_struct_build.encode_p_layout);
   if (result != VK_SUCCESS)
      goto exit;

   result = create_build_pipeline_spv(device, header_spv, sizeof(header_spv), sizeof(struct header_args),
                                      &device->meta_state.accel_struct_build.header_pipeline,
                                      &device->meta_state.accel_struct_build.header_p_layout);
   if (result != VK_SUCCESS)
      goto exit;

   result = create_build_pipeline_spv(device, update_spv, sizeof(update_spv), sizeof(struct update_args),
                                      &device->meta_state.accel_struct_build.update_pipeline,
                                      &device->meta_state.accel_struct_build.update_p_layout);
   if (result != VK_SUCCESS)
      goto exit;

   device->meta_state.accel_struct_build.radix_sort = vk_create_radix_sort_u64(
      radv_device_to_handle(device), &device->meta_state.alloc, device->meta_state.cache, radix_sort_config);

   device->vk.as_build_ops = &build_ops;
   device->vk.write_buffer_cp = radv_write_buffer_cp;
   device->vk.flush_buffer_write_cp = radv_flush_buffer_write_cp;
   device->vk.cmd_dispatch_unaligned = radv_cmd_dispatch_unaligned;
   device->vk.cmd_fill_buffer_addr = radv_cmd_fill_buffer_addr;

   struct vk_acceleration_structure_build_args *build_args = &device->meta_state.accel_struct_build.build_args;
   build_args->subgroup_size = 64;
   build_args->bvh_bounds_offset = offsetof(struct radv_accel_struct_header, aabb);
   build_args->emit_markers = device->sqtt.bo;
   build_args->radix_sort = device->meta_state.accel_struct_build.radix_sort;

exit:
   mtx_unlock(&device->meta_state.mtx);
   return result;
}

static VkResult
radv_device_init_accel_struct_copy_state(struct radv_device *device)
{
   mtx_lock(&device->meta_state.mtx);

   VkResult result = create_build_pipeline_spv(device, copy_spv, sizeof(copy_spv), sizeof(struct copy_args),
                                               &device->meta_state.accel_struct_build.copy_pipeline,
                                               &device->meta_state.accel_struct_build.copy_p_layout);

   mtx_unlock(&device->meta_state.mtx);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount,
                                       const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
                                       const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;

   VkResult result = radv_device_init_accel_struct_build_state(device);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   cmd_buffer->state.current_event_type = EventInternalUnknown;

   vk_cmd_build_acceleration_structures(commandBuffer, &device->vk, &device->meta_state.device, infoCount, pInfos,
                                        ppBuildRangeInfos, &device->meta_state.accel_struct_build.build_args);

   radv_meta_restore(&saved_state, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureInfoKHR *pInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, src, pInfo->src);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, pInfo->dst);
   VK_FROM_HANDLE(radv_buffer, src_buffer, src->buffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;

   VkResult result = radv_device_init_accel_struct_copy_state(device);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        device->meta_state.accel_struct_build.copy_pipeline);

   struct copy_args consts = {
      .src_addr = vk_acceleration_structure_get_va(src),
      .dst_addr = vk_acceleration_structure_get_va(dst),
      .mode = RADV_COPY_MODE_COPY,
   };

   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                              device->meta_state.accel_struct_build.copy_p_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                              sizeof(consts), &consts);

   cmd_buffer->state.flush_bits |= radv_dst_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                                                         VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, 0, NULL, NULL);

   radv_indirect_dispatch(
      cmd_buffer, src_buffer->bo,
      vk_acceleration_structure_get_va(src) + offsetof(struct radv_accel_struct_header, copy_dispatch_size));
   radv_meta_restore(&saved_state, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_GetDeviceAccelerationStructureCompatibilityKHR(VkDevice _device,
                                                    const VkAccelerationStructureVersionInfoKHR *pVersionInfo,
                                                    VkAccelerationStructureCompatibilityKHR *pCompatibility)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   bool compat = memcmp(pVersionInfo->pVersionData, pdev->driver_uuid, VK_UUID_SIZE) == 0 &&
                 memcmp(pVersionInfo->pVersionData + VK_UUID_SIZE, pdev->cache_uuid, VK_UUID_SIZE) == 0;
   *pCompatibility = compat ? VK_ACCELERATION_STRUCTURE_COMPATIBILITY_COMPATIBLE_KHR
                            : VK_ACCELERATION_STRUCTURE_COMPATIBILITY_INCOMPATIBLE_KHR;
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                             const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, pInfo->dst);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;

   VkResult result = radv_device_init_accel_struct_copy_state(device);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        device->meta_state.accel_struct_build.copy_pipeline);

   const struct copy_args consts = {
      .src_addr = pInfo->src.deviceAddress,
      .dst_addr = vk_acceleration_structure_get_va(dst),
      .mode = RADV_COPY_MODE_DESERIALIZE,
   };

   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                              device->meta_state.accel_struct_build.copy_p_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                              sizeof(consts), &consts);

   vk_common_CmdDispatch(commandBuffer, 512, 1, 1);
   radv_meta_restore(&saved_state, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyAccelerationStructureToMemoryKHR(VkCommandBuffer commandBuffer,
                                             const VkCopyAccelerationStructureToMemoryInfoKHR *pInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, src, pInfo->src);
   VK_FROM_HANDLE(radv_buffer, src_buffer, src->buffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_meta_saved_state saved_state;

   VkResult result = radv_device_init_accel_struct_copy_state(device);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        device->meta_state.accel_struct_build.copy_pipeline);

   const struct copy_args consts = {
      .src_addr = vk_acceleration_structure_get_va(src),
      .dst_addr = pInfo->dst.deviceAddress,
      .mode = RADV_COPY_MODE_SERIALIZE,
   };

   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                              device->meta_state.accel_struct_build.copy_p_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                              sizeof(consts), &consts);

   cmd_buffer->state.flush_bits |= radv_dst_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                                                         VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, 0, NULL, NULL);

   radv_indirect_dispatch(
      cmd_buffer, src_buffer->bo,
      vk_acceleration_structure_get_va(src) + offsetof(struct radv_accel_struct_header, copy_dispatch_size));
   radv_meta_restore(&saved_state, cmd_buffer);

   /* Set the header of the serialized data. */
   uint8_t header_data[2 * VK_UUID_SIZE];
   memcpy(header_data, pdev->driver_uuid, VK_UUID_SIZE);
   memcpy(header_data + VK_UUID_SIZE, pdev->cache_uuid, VK_UUID_SIZE);

   radv_update_buffer_cp(cmd_buffer, pInfo->dst.deviceAddress, header_data, sizeof(header_data));
}
