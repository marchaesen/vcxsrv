/*
 * Copyright © 2021 Bas Nieuwenhuizen
 * Copyright © 2024 Valve Corporation
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

#include "tu_buffer.h"
#include "tu_device.h"
#include "tu_cmd_buffer.h"

#include "vk_acceleration_structure.h"
#include "tu_acceleration_structure.h"
#include "radix_sort/radix_sort_u64.h"


#include "common/freedreno_gpu_event.h"

#include "util/u_hexdump.h"

#include "bvh/tu_build_interface.h"

static const uint32_t encode_spv[] = {
#include "bvh/encode.spv.h"
};

static const uint32_t header_spv[] = {
#include "bvh/header.spv.h"
};

static const uint32_t copy_spv[] = {
#include "bvh/copy.spv.h"
};

static_assert(sizeof(struct tu_instance_descriptor) == AS_RECORD_SIZE);
static_assert(sizeof(struct tu_accel_struct_header) == AS_RECORD_SIZE);
static_assert(sizeof(struct tu_internal_node) == AS_NODE_SIZE);
static_assert(sizeof(struct tu_leaf_node) == AS_NODE_SIZE);

static VkResult
get_pipeline_spv(struct tu_device *device,
                 const char *name, const uint32_t *spv, uint32_t spv_size,
                 unsigned push_constant_size,
                 VkPipeline *pipeline, VkPipelineLayout *layout)
{
   size_t key_size = strlen(name);

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .offset = 0,
      .size = push_constant_size,
   };

   VkResult result = vk_meta_get_pipeline_layout(&device->vk,
                                                 &device->meta, NULL,
                                                 &pc_range, name, key_size,
                                                 layout);

   if (result != VK_SUCCESS)
      return result;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta, name, key_size);
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline = pipeline_from_cache;
      return VK_SUCCESS;
   }

   VkShaderModuleCreateInfo module_info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .codeSize = spv_size,
      .pCode = spv,
   };

   VkPipelineShaderStageCreateInfo shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext = &module_info,
      .flags = 0,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .flags = 0,
      .stage = shader_stage,
      .layout = *layout,
   };

   return vk_meta_create_compute_pipeline(&device->vk, &device->meta, &pipeline_info,
                                          name, key_size, pipeline);
}

struct bvh_layout {
   uint64_t bvh_offset;
   uint64_t size;
};

static void
get_bvh_layout(VkGeometryTypeKHR geometry_type,
               uint32_t leaf_count,
               struct bvh_layout *layout)
{
   uint32_t internal_count = MAX2(leaf_count, 2) - 1;

   uint64_t offset = sizeof(struct tu_accel_struct_header);
   
   /* Instance descriptors, one per instance. */
   if (geometry_type == VK_GEOMETRY_TYPE_INSTANCES_KHR) {
      offset += leaf_count * sizeof(struct tu_instance_descriptor);
   }

   /* Parent links, which have to go directly before bvh_offset as we index
    * them using negative offsets from there.
    */
   offset += (internal_count + leaf_count) * sizeof(uint32_t);

   /* The BVH and hence bvh_offset needs 64 byte alignment for RT nodes. */
   offset = ALIGN(offset, 64);
   layout->bvh_offset = offset;

   offset += internal_count * sizeof(struct tu_internal_node) +
      leaf_count * sizeof(struct tu_leaf_node);

   layout->size = offset;
}

VkDeviceSize get_bvh_size(VkDevice device,
                          const VkAccelerationStructureBuildGeometryInfoKHR *pBuildInfo,
                          uint32_t leaf_count)
{
   struct bvh_layout layout;
   get_bvh_layout(vk_get_as_geometry_type(pBuildInfo), leaf_count, &layout);
   return layout.size;
}

static uint32_t
encode_key(VkAccelerationStructureTypeKHR type,
           VkBuildAccelerationStructureFlagBitsKHR flags)
{
   return 0;
}


static VkResult
encode_bind_pipeline(VkCommandBuffer commandBuffer, uint32_t key)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   struct tu_device *device = cmdbuf->device;

   VkPipeline pipeline;
   VkPipelineLayout layout;
   VkResult result =
      get_pipeline_spv(device, "encode", encode_spv, sizeof(encode_spv),
                       sizeof(encode_args), &pipeline, &layout);

   if (result != VK_SUCCESS)
      return result;

   tu_CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
   return VK_SUCCESS;
}

static void
encode(VkCommandBuffer commandBuffer,
       const VkAccelerationStructureBuildGeometryInfoKHR *build_info,
       const VkAccelerationStructureBuildRangeInfoKHR *build_range_infos,
       VkDeviceAddress intermediate_as_addr,
       VkDeviceAddress intermediate_header_addr,
       uint32_t leaf_count,
       uint32_t key,
       struct vk_acceleration_structure *dst)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   struct tu_device *device = cmdbuf->device;
   VkGeometryTypeKHR geometry_type = vk_get_as_geometry_type(build_info);

   VkPipeline pipeline;
   VkPipelineLayout layout;
   get_pipeline_spv(device, "encode", encode_spv, sizeof(encode_spv),
                    sizeof(encode_args), &pipeline, &layout);

   struct bvh_layout bvh_layout;
   get_bvh_layout(geometry_type, leaf_count, &bvh_layout);

   const struct encode_args args = {
      .intermediate_bvh = intermediate_as_addr,
      .output_bvh = vk_acceleration_structure_get_va(dst) + bvh_layout.bvh_offset,
      .header = intermediate_header_addr,
      .output_bvh_offset = bvh_layout.bvh_offset,
      .leaf_node_count = leaf_count,
      .geometry_type = geometry_type,
   };
   vk_common_CmdPushConstants(commandBuffer, layout,
                              VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(args),
                              &args);

   tu_dispatch_unaligned_indirect(commandBuffer,
                                  intermediate_header_addr +
                                  offsetof(struct vk_ir_header, ir_internal_node_count));

   *(VkDeviceSize *)
      util_sparse_array_get(&device->accel_struct_ranges,
                            vk_acceleration_structure_get_va(dst)) = dst->size;

}

/* Don't bother copying over the compacted size using a compute shader if
 * compaction is never going to happen.
 */
enum tu_header_key {
   HEADER_NO_DISPATCH,
   HEADER_USE_DISPATCH
};

static uint32_t
header_key(VkAccelerationStructureTypeKHR type,
           VkBuildAccelerationStructureFlagBitsKHR flags)
{
   return (flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR) ?
      HEADER_USE_DISPATCH : HEADER_NO_DISPATCH;
}

static VkResult
header_bind_pipeline(VkCommandBuffer commandBuffer, uint32_t key)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   struct tu_device *device = cmdbuf->device;

   if (key == HEADER_USE_DISPATCH) {
      VkPipeline pipeline;
      VkPipelineLayout layout;
      VkResult result =
         get_pipeline_spv(device, "header", header_spv, sizeof(header_spv),
                          sizeof(header_args), &pipeline, &layout);

      if (result != VK_SUCCESS)
         return result;

      static const VkMemoryBarrier mb = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      };

      vk_common_CmdPipelineBarrier(commandBuffer,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   0, 1, &mb, 0, NULL, 0, NULL);

      tu_CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
   }

   return VK_SUCCESS;
}

static void
header(VkCommandBuffer commandBuffer,
       const VkAccelerationStructureBuildGeometryInfoKHR *build_info,
       const VkAccelerationStructureBuildRangeInfoKHR *build_range_infos,
       VkDeviceAddress intermediate_as_addr,
       VkDeviceAddress intermediate_header_addr,
       uint32_t leaf_count,
       uint32_t key,
       struct vk_acceleration_structure *dst)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   struct tu_device *device = cmdbuf->device;
   VkGeometryTypeKHR geometry_type = vk_get_as_geometry_type(build_info);

   struct bvh_layout bvh_layout;
   get_bvh_layout(geometry_type, leaf_count, &bvh_layout);

   VkDeviceAddress header_addr = vk_acceleration_structure_get_va(dst);

   size_t base = offsetof(struct tu_accel_struct_header, copy_dispatch_size);

   uint32_t instance_count =
      geometry_type == VK_GEOMETRY_TYPE_INSTANCES_KHR ? leaf_count : 0;

   if (key == HEADER_USE_DISPATCH) {
      base = offsetof(struct tu_accel_struct_header, instance_count);
      VkPipeline pipeline;
      VkPipelineLayout layout;
      get_pipeline_spv(device, "header", header_spv, sizeof(header_spv),
                       sizeof(header_args), &pipeline, &layout);

      struct header_args args = {
         .src = intermediate_header_addr,
         .dst = vk_acceleration_structure_get_va(dst),
         .bvh_offset = bvh_layout.bvh_offset,
         .instance_count = instance_count,
      };

      vk_common_CmdPushConstants(commandBuffer, layout,
                                 VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(args),
                                 &args);

      vk_common_CmdDispatch(commandBuffer, 1, 1, 1);
   }

   struct tu_accel_struct_header header = {};

   header.instance_count = instance_count;
   header.self_ptr = header_addr;
   header.compacted_size = bvh_layout.size;

   header.copy_dispatch_size[0] = DIV_ROUND_UP(header.compacted_size, 16 * 128);
   header.copy_dispatch_size[1] = 1;
   header.copy_dispatch_size[2] = 1;

   header.serialization_size =
      header.compacted_size +
      sizeof(struct vk_accel_struct_serialization_header) + sizeof(uint64_t) * header.instance_count;

   header.size = header.serialization_size - sizeof(struct vk_accel_struct_serialization_header) -
                 sizeof(uint64_t) * header.instance_count;

   struct tu_cs *cs = &cmdbuf->cs;

   size_t header_size = sizeof(struct tu_accel_struct_header) - base;
   assert(base % sizeof(uint32_t) == 0);
   assert(header_size % sizeof(uint32_t) == 0);
   uint32_t *header_ptr = (uint32_t *)((char *)&header + base);

   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 2 + header_size / sizeof(uint32_t));
   tu_cs_emit_qw(cs, header_addr + base);
   tu_cs_emit_array(cs, header_ptr, header_size / sizeof(uint32_t));
}

const struct vk_acceleration_structure_build_ops tu_as_build_ops = {
   .get_as_size = get_bvh_size,
   .get_encode_key = { encode_key, header_key },
   .encode_bind_pipeline = { encode_bind_pipeline, header_bind_pipeline },
   .encode_as = { encode, header },
};

struct radix_sort_vk_target_config tu_radix_sort_config = {
   .keyval_dwords = 2,
   .init = { .workgroup_size_log2 = 8, },
   .fill = { .workgroup_size_log2 = 8, .block_rows = 8 },
   .histogram = {
      .workgroup_size_log2 = 8,
      .subgroup_size_log2 = 7,
      .block_rows = 14, /* TODO tune this */
   },
   .prefix = {
      .workgroup_size_log2 = 8,
      .subgroup_size_log2 = 7,
   },
   .scatter = {
      .workgroup_size_log2 = 8,
      .subgroup_size_log2 = 7,
      .block_rows = 14, /* TODO tune this */
   },
   .nonsequential_dispatch = false,
};

static VkResult
init_radix_sort(struct tu_device *device)
{
   if (!device->radix_sort) {
      mtx_lock(&device->radix_sort_mutex);
      if (!device->radix_sort) {
         device->radix_sort =
            vk_create_radix_sort_u64(tu_device_to_handle(device),
                                     &device->vk.alloc,
                                     VK_NULL_HANDLE, tu_radix_sort_config);
         if (!device->radix_sort) {
            /* TODO plumb through the error here */
            mtx_unlock(&device->radix_sort_mutex);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }

      }
      mtx_unlock(&device->radix_sort_mutex);
   }

   return VK_SUCCESS;
}

struct tu_saved_compute_state {
   uint32_t push_constants[MAX_PUSH_CONSTANTS_SIZE / 4];
   struct tu_shader *compute_shader;
};

static void
tu_save_compute_state(struct tu_cmd_buffer *cmd,
                      struct tu_saved_compute_state *state)
{
   memcpy(state->push_constants, cmd->push_constants, sizeof(cmd->push_constants));
   state->compute_shader = cmd->state.shaders[MESA_SHADER_COMPUTE];
}

static void
tu_restore_compute_state(struct tu_cmd_buffer *cmd,
                         struct tu_saved_compute_state *state)
{
   cmd->state.shaders[MESA_SHADER_COMPUTE] = state->compute_shader;
   if (state->compute_shader) {
      tu_cs_emit_state_ib(&cmd->cs, state->compute_shader->state);
   }
   memcpy(cmd->push_constants, state->push_constants, sizeof(cmd->push_constants));
   cmd->state.dirty |= TU_CMD_DIRTY_SHADER_CONSTS;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount,
                                     const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
                                     const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_device *device = cmd->device;
   struct tu_saved_compute_state state;

   VkResult result = init_radix_sort(device);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   tu_save_compute_state(cmd, &state);

   struct vk_acceleration_structure_build_args args = {
      .subgroup_size = 128,
      .bvh_bounds_offset = offsetof(tu_accel_struct_header, aabb),
      .emit_markers = false,
      .radix_sort = device->radix_sort,
   };

   vk_cmd_build_acceleration_structures(commandBuffer,
                                        &device->vk,
                                        &device->meta,
                                        infoCount,
                                        pInfos,
                                        ppBuildRangeInfos,
                                        &args);

   tu_restore_compute_state(cmd, &state);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureInfoKHR *pInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, src, pInfo->src);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, pInfo->dst);
   struct tu_saved_compute_state state;

   VkPipeline pipeline;
   VkPipelineLayout layout;
   VkResult result =
      get_pipeline_spv(cmd->device, "copy", copy_spv, sizeof(copy_spv),
                    sizeof(copy_args), &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   tu_save_compute_state(cmd, &state);

   tu_CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   struct copy_args consts = {
      .src_addr = vk_acceleration_structure_get_va(src),
      .dst_addr = vk_acceleration_structure_get_va(dst),
      .mode = TU_COPY_MODE_COPY,
   };

   vk_common_CmdPushConstants(commandBuffer, layout,
                              VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts),
                              &consts);

   TU_CALLX(cmd->device, tu_CmdDispatchIndirect)(
      commandBuffer, src->buffer,
      src->offset + offsetof(struct tu_accel_struct_header, copy_dispatch_size));

   tu_restore_compute_state(cmd, &state);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                           const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, dst, pInfo->dst);
   struct tu_saved_compute_state state;

   VkPipeline pipeline;
   VkPipelineLayout layout;
   VkResult result =
      get_pipeline_spv(cmd->device, "copy", copy_spv, sizeof(copy_spv),
                    sizeof(copy_args), &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   tu_save_compute_state(cmd, &state);

   tu_CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   const struct copy_args consts = {
      .src_addr = pInfo->src.deviceAddress,
      .dst_addr = vk_acceleration_structure_get_va(dst),
      .mode = TU_COPY_MODE_DESERIALIZE,
   };

   vk_common_CmdPushConstants(commandBuffer, layout,
                              VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts),
                              &consts);

   vk_common_CmdDispatch(commandBuffer, 256, 1, 1);

   tu_restore_compute_state(cmd, &state);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdCopyAccelerationStructureToMemoryKHR(VkCommandBuffer commandBuffer,
                                           const VkCopyAccelerationStructureToMemoryInfoKHR *pInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, src, pInfo->src);
   struct tu_saved_compute_state state;

   VkPipeline pipeline;
   VkPipelineLayout layout;
   VkResult result =
      get_pipeline_spv(cmd->device, "copy", copy_spv, sizeof(copy_spv),
                    sizeof(copy_args), &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   tu_save_compute_state(cmd, &state);

   tu_CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   const struct copy_args consts = {
      .src_addr = vk_acceleration_structure_get_va(src),
      .dst_addr = pInfo->dst.deviceAddress,
      .mode = TU_COPY_MODE_SERIALIZE,
   };

   vk_common_CmdPushConstants(commandBuffer, layout,
                              VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts),
                              &consts);

   TU_CALLX(cmd->device, tu_CmdDispatchIndirect)(
      commandBuffer, src->buffer,
      src->offset + offsetof(struct tu_accel_struct_header, copy_dispatch_size));

   tu_restore_compute_state(cmd, &state);

   /* Set the header of the serialized data. */
   uint32_t header_data[2 * VK_UUID_SIZE / 4];
   memcpy(header_data, cmd->device->physical_device->driver_uuid, VK_UUID_SIZE);
   memcpy(header_data + VK_UUID_SIZE / 4, cmd->device->physical_device->cache_uuid, VK_UUID_SIZE);

   struct tu_cs *cs = &cmd->cs;

   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 2 + ARRAY_SIZE(header_data));
   tu_cs_emit_qw(cs, pInfo->dst.deviceAddress);
   tu_cs_emit_array(cs, header_data, ARRAY_SIZE(header_data));
}

VKAPI_ATTR void VKAPI_CALL
tu_GetAccelerationStructureBuildSizesKHR(VkDevice _device, VkAccelerationStructureBuildTypeKHR buildType,
                                         const VkAccelerationStructureBuildGeometryInfoKHR *pBuildInfo,
                                         const uint32_t *pMaxPrimitiveCounts,
                                         VkAccelerationStructureBuildSizesInfoKHR *pSizeInfo)
{
   VK_FROM_HANDLE(tu_device, device, _device);

   init_radix_sort(device);

   struct vk_acceleration_structure_build_args args = {
      .subgroup_size = 128,
      .radix_sort = device->radix_sort,
   };

   vk_get_as_build_sizes(_device, buildType, pBuildInfo, pMaxPrimitiveCounts,
                         pSizeInfo, &args);
}

VKAPI_ATTR void VKAPI_CALL
tu_GetDeviceAccelerationStructureCompatibilityKHR(VkDevice _device,
                                                  const VkAccelerationStructureVersionInfoKHR *pVersionInfo,
                                                  VkAccelerationStructureCompatibilityKHR *pCompatibility)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   bool compat =
      memcmp(pVersionInfo->pVersionData, device->physical_device->driver_uuid, VK_UUID_SIZE) == 0 &&
      memcmp(pVersionInfo->pVersionData + VK_UUID_SIZE, device->physical_device->cache_uuid, VK_UUID_SIZE) == 0;
   *pCompatibility = compat ? VK_ACCELERATION_STRUCTURE_COMPATIBILITY_COMPATIBLE_KHR
                            : VK_ACCELERATION_STRUCTURE_COMPATIBILITY_INCOMPATIBLE_KHR;
}

VkResult
tu_init_null_accel_struct(struct tu_device *device)
{
   VkResult result = tu_bo_init_new(device, NULL,
                                    &device->null_accel_struct_bo,
                                    sizeof(tu_accel_struct_header) +
                                    sizeof(tu_internal_node),
                                    TU_BO_ALLOC_NO_FLAGS, "null AS");
   if (result != VK_SUCCESS) {
      return result;
   }

   result = tu_bo_map(device, device->null_accel_struct_bo, NULL);
   if (result != VK_SUCCESS) {
      tu_bo_finish(device, device->null_accel_struct_bo);
      return result;
   }

   struct tu_accel_struct_header header = {
      .bvh_ptr = device->null_accel_struct_bo->iova +
         sizeof(tu_accel_struct_header),
      .self_ptr = device->null_accel_struct_bo->iova,
   };

   struct tu_internal_node node = {
      .child_count = 0,
      .type_flags = 0,
   };

   for (unsigned i = 0; i < 8; i++) {
      node.mantissas[i][0][0] = 0xff;
      node.mantissas[i][0][1] = 0xff;
      node.mantissas[i][0][2] = 0xff;
   }

   memcpy(device->null_accel_struct_bo->map, (void *)&header, sizeof(header));
   memcpy((char *)device->null_accel_struct_bo->map + sizeof(header),
          (void *)&node, sizeof(node));
   return VK_SUCCESS;
}

struct tu_node {
   uint32_t data[16];
};

static void
dump_leaf(struct tu_leaf_node *node)
{
   fprintf(stderr, "\tID: %d\n", node->id);
   fprintf(stderr, "\tgeometry ID: %d\n", node->geometry_id);
   bool aabb = node->type_flags & TU_NODE_TYPE_AABB;
   for (unsigned i = 0; i < (aabb ? 2 : 3); i++) {
      fprintf(stderr, "\t(");
      for (unsigned j = 0; j < 3; j++) {
         if (j != 0)
            fprintf(stderr, ", ");
         fprintf(stderr, "%f", node->coords[i][j]);
      }
      fprintf(stderr, ")\n");
   }
}

static void
dump_internal(struct tu_internal_node *node, uint32_t *max_child)
{
   *max_child = MAX2(*max_child, node->id + node->child_count);
   float base[3];
   unsigned exponents[3];
   for (unsigned i = 0; i < 3; i++) {
      base[i] = uif(node->bases[i] << 16);
      exponents[i] = node->exponents[i] - 134;
   }

   for (unsigned i = 0; i < node->child_count; i++) {
      fprintf(stderr, "\tchild %d\n", node->id + i);
      for (unsigned vert = 0; vert < 2; vert++) {
         fprintf(stderr, "\t\t(");
         for (unsigned coord = 0; coord < 3; coord++) {
            unsigned mantissa = node->mantissas[i][vert][coord];
            if (coord != 0)
               fprintf(stderr, ", ");
            fprintf(stderr, "%f", base[coord] + ldexp((float)mantissa,
                                                      exponents[coord]));
         }
         fprintf(stderr, ")\n");
      }
   }
}

static void
dump_as(struct vk_acceleration_structure *as)
{
   VK_FROM_HANDLE(tu_buffer, buf, as->buffer);

   struct tu_accel_struct_header *hdr =
      (struct tu_accel_struct_header *)((char *)buf->bo->map + as->offset);
   
   fprintf(stderr, "dumping AS at %" PRIx64 "\n", buf->iova + as->offset);
   u_hexdump(stderr, (uint8_t *)hdr, sizeof(*hdr), false);

   char *base = ((char *)buf->bo->map + (hdr->bvh_ptr - buf->iova));
   struct tu_node *node = (struct tu_node *)base;

   fprintf(stderr, "dumping nodes at %" PRIx64 "\n", hdr->bvh_ptr);

   uint32_t max_child = 1;
   for (unsigned i = 0; i < max_child; i++) {
      uint32_t *parent_ptr = (uint32_t*)(base - (4 + 4 * i));
      uint32_t parent = *parent_ptr;
      fprintf(stderr, "node %d parent %d\n", i, parent);
      u_hexdump(stderr, (uint8_t *)node, sizeof(*node), false);
      if (node->data[15] & TU_NODE_TYPE_LEAF) {
         /* TODO compressed leaves */
         dump_leaf((struct tu_leaf_node *)node);
      } else {
         dump_internal((struct tu_internal_node *)node, &max_child);
      }

      node++;
   }
}

static bool
as_finished(struct tu_device *dev, struct vk_acceleration_structure *as)
{
   VK_FROM_HANDLE(tu_buffer, buf, as->buffer);
   tu_bo_map(dev, buf->bo, NULL);

   struct tu_accel_struct_header *hdr =
      (struct tu_accel_struct_header *)((char *)buf->bo->map + as->offset);
   return hdr->self_ptr == buf->iova + as->offset;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyAccelerationStructureKHR(VkDevice _device,
                                   VkAccelerationStructureKHR accelerationStructure,
                                   const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   if (TU_DEBUG(DUMPAS)) {
      VK_FROM_HANDLE(vk_acceleration_structure, as, accelerationStructure);
      if (as_finished(device, as))
         dump_as(as);
   }

   vk_common_DestroyAccelerationStructureKHR(_device, accelerationStructure,
                                             pAllocator);
}
