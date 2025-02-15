/*
 * Copyright © 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "lvp_acceleration_structure.h"
#include "lvp_entrypoints.h"

#include "radix_sort/radix_sort_u64.h"
#include "bvh/vk_bvh.h"

struct radix_sort_vk_target_config lvp_radix_sort_config = {
   .keyval_dwords = 2,
   .init = {
      .workgroup_size_log2 = 4,
   },
   .fill = {
      .workgroup_size_log2 = 4,
      .block_rows = 4,
   },
   .histogram = {
      .workgroup_size_log2 = 7,
      .subgroup_size_log2 = 3,
      .block_rows = 16,
   },
   .prefix = {
      .workgroup_size_log2 = 8,
      .subgroup_size_log2 = 3,
   },
   .scatter = {
      .workgroup_size_log2 = 7,
      .subgroup_size_log2 = 3,
      .block_rows = 8,
   },
   .nonsequential_dispatch = true,
};

static void
lvp_init_radix_sort(struct lvp_device *device)
{
   simple_mtx_lock(&device->radix_sort_lock);
   if (device->radix_sort) {
      simple_mtx_unlock(&device->radix_sort_lock);
      return;
   }

   device->radix_sort =
      vk_create_radix_sort_u64(lvp_device_to_handle(device),
                               &device->vk.alloc, VK_NULL_HANDLE,
                               lvp_radix_sort_config);

   device->accel_struct_args.radix_sort = device->radix_sort;

   simple_mtx_unlock(&device->radix_sort_lock);
}

static void
lvp_write_buffer_cp(VkCommandBuffer cmdbuf, VkDeviceAddress addr,
                    void *data, uint32_t size)
{
   VK_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, cmdbuf);

   struct vk_cmd_queue_entry *entry =
      vk_zalloc(cmd_buffer->vk.cmd_queue.alloc, sizeof(struct vk_cmd_queue_entry),
                8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!entry)
      return;

   entry->type = LVP_CMD_WRITE_BUFFER_CP;

   struct lvp_cmd_write_buffer_cp *cmd =
      vk_zalloc(cmd_buffer->vk.cmd_queue.alloc, sizeof(struct lvp_cmd_write_buffer_cp) + size,
                8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!entry) {
      vk_free(cmd_buffer->vk.cmd_queue.alloc, entry);
      return;
   }

   cmd->addr = addr;
   cmd->data = cmd + 1;
   cmd->size = size;

   memcpy(cmd->data, data, size);

   entry->driver_data = cmd;

   list_addtail(&entry->cmd_link, &cmd_buffer->vk.cmd_queue.cmds);
}

static void
lvp_flush_buffer_write_cp(VkCommandBuffer cmdbuf)
{
}

static void
lvp_cmd_dispatch_unaligned(VkCommandBuffer cmdbuf, uint32_t invocations_x,
                           uint32_t invocations_y, uint32_t invocations_z)
{
   VK_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, cmdbuf);

   struct vk_cmd_queue_entry *entry =
      vk_zalloc(cmd_buffer->vk.cmd_queue.alloc, sizeof(struct vk_cmd_queue_entry),
                8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!entry)
      return;

   entry->type = LVP_CMD_DISPATCH_UNALIGNED;

   entry->u.dispatch.group_count_x = invocations_x;
   entry->u.dispatch.group_count_y = invocations_y;
   entry->u.dispatch.group_count_z = invocations_z;

   list_addtail(&entry->cmd_link, &cmd_buffer->vk.cmd_queue.cmds);
}

static void
lvp_cmd_fill_buffer_addr(VkCommandBuffer cmdbuf, VkDeviceAddress addr,
                         VkDeviceSize size, uint32_t data)
{
   VK_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, cmdbuf);

   struct vk_cmd_queue_entry *entry =
      vk_zalloc(cmd_buffer->vk.cmd_queue.alloc, sizeof(struct vk_cmd_queue_entry),
                8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!entry)
      return;

   entry->type = LVP_CMD_FILL_BUFFER_ADDR;

   struct lvp_cmd_fill_buffer_addr *cmd =
      vk_zalloc(cmd_buffer->vk.cmd_queue.alloc, sizeof(struct lvp_cmd_write_buffer_cp),
                8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!entry) {
      vk_free(cmd_buffer->vk.cmd_queue.alloc, entry);
      return;
   }

   cmd->addr = addr;
   cmd->size = size;
   cmd->data = data;

   entry->driver_data = cmd;

   list_addtail(&entry->cmd_link, &cmd_buffer->vk.cmd_queue.cmds);
}

static void
lvp_enqueue_encode_as(VkCommandBuffer commandBuffer,
                      const VkAccelerationStructureBuildGeometryInfoKHR *build_info,
                      const VkAccelerationStructureBuildRangeInfoKHR *build_range_infos,
                      VkDeviceAddress intermediate_as_addr,
                      VkDeviceAddress intermediate_header_addr,
                      uint32_t leaf_count,
                      uint32_t key,
                      struct vk_acceleration_structure *dst)
{
   VK_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);

   struct vk_cmd_queue_entry *entry =
      vk_zalloc(cmd_buffer->vk.cmd_queue.alloc, sizeof(struct vk_cmd_queue_entry),
                8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!entry)
      return;

   entry->type = LVP_CMD_ENCODE_AS;

   struct lvp_cmd_encode_as *cmd =
      vk_zalloc(cmd_buffer->vk.cmd_queue.alloc, sizeof(struct lvp_cmd_encode_as),
                8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!entry) {
      vk_free(cmd_buffer->vk.cmd_queue.alloc, entry);
      return;
   }

   cmd->dst = dst;
   cmd->intermediate_as_addr = intermediate_as_addr;
   cmd->intermediate_header_addr = intermediate_header_addr;
   cmd->leaf_count = leaf_count;
   cmd->geometry_type = vk_get_as_geometry_type(build_info);

   entry->driver_data = cmd;

   list_addtail(&entry->cmd_link, &cmd_buffer->vk.cmd_queue.cmds);
}

static uint32_t
ir_id_to_offset(uint32_t id)
{
   return id & (~3u);
}

static uint32_t
lvp_pack_sbt_offset_and_flags(uint32_t sbt_offset, VkGeometryInstanceFlagsKHR flags)
{
   uint32_t ret = sbt_offset;
   if (flags & VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR)
      ret |= LVP_INSTANCE_FORCE_OPAQUE;
   if (!(flags & VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR))
      ret |= LVP_INSTANCE_NO_FORCE_NOT_OPAQUE;
   if (flags & VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR)
      ret |= LVP_INSTANCE_TRIANGLE_FACING_CULL_DISABLE;
   if (flags & VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR)
      ret |= LVP_INSTANCE_TRIANGLE_FLIP_FACING;
   return ret;
}

static void
lvp_select_subtrees_to_flatten(const struct vk_ir_header *header, const struct vk_ir_box_node *ir_box_nodes,
                               const uint32_t *node_depth, const uint32_t *child_counts, uint32_t root_offset,
                               uint32_t index, struct util_dynarray *subtrees, uint32_t *max_subtree_size)
{
   uint32_t depth = node_depth[header->ir_internal_node_count - index - 1];
   uint32_t available_depth = 23 - depth;
   uint32_t allowed_child_count = 1 << available_depth;
   uint32_t child_count = child_counts[index];
   bool flatten = child_count > allowed_child_count;

   const struct vk_ir_box_node *node = ir_box_nodes + index;

   bool has_internal_child = false;
   for (uint32_t child_index = 0; child_index < 2; child_index++) {
      if (node->children[child_index] == VK_BVH_INVALID_NODE)
         continue;

      uint32_t ir_child_offset = ir_id_to_offset(node->children[child_index]);
      if (ir_child_offset < root_offset)
         continue;

      if (!flatten) {
         uint32_t src_index = (ir_child_offset - root_offset) / sizeof(struct vk_ir_box_node);
         lvp_select_subtrees_to_flatten(header, ir_box_nodes, node_depth, child_counts, root_offset,
                                        src_index, subtrees, max_subtree_size);
      }

      has_internal_child = true;
   }

   if (flatten && has_internal_child) {
      util_dynarray_append(subtrees, uint32_t, index);
      *max_subtree_size = MAX2(*max_subtree_size, child_count);
      return;
   }
}

static void
lvp_gather_subtree(const uint8_t *output, uint32_t offset, uint32_t *leaf_nodes,
                   vk_aabb *leaf_bounds, uint32_t *leaf_node_count, uint32_t *internal_nodes,
                   uint32_t *internal_node_count)
{
   const struct lvp_bvh_box_node *node = (void *)(output + offset);

   for (uint32_t child_index = 0; child_index < 2; child_index++) {
      if (node->children[child_index] == VK_BVH_INVALID_NODE)
         continue;

      uint32_t type = node->children[child_index] & 3;
      if (type == lvp_bvh_node_internal) {
         internal_nodes[*internal_node_count] = node->children[child_index];
         (*internal_node_count)++;

         uint32_t ir_child_offset = ir_id_to_offset(node->children[child_index]);
         lvp_gather_subtree(output, ir_child_offset, leaf_nodes, leaf_bounds,
                            leaf_node_count, internal_nodes, internal_node_count);
      } else {
         leaf_nodes[*leaf_node_count] = node->children[child_index];
         leaf_bounds[*leaf_node_count] = node->bounds[child_index];
         (*leaf_node_count)++;
      }
   }
}

static uint32_t
lvp_rebuild_subtree(const uint8_t *output, uint32_t *leaf_nodes, vk_aabb *leaf_bounds,
                    uint32_t *internal_nodes, uint32_t leaf_node_count, 
                    uint32_t *internal_node_index)
{
   uint32_t child_nodes[2];
   vk_aabb child_leaf_bounds[2];

   if (leaf_node_count < 2)
      return leaf_nodes[0];

   uint32_t node_id = internal_nodes[*internal_node_index];
   (*internal_node_index)++;

   uint32_t split_index = leaf_node_count / 2;

   child_leaf_bounds[0] = leaf_bounds[0];
   child_nodes[0] = lvp_rebuild_subtree(output, leaf_nodes, leaf_bounds, internal_nodes, split_index, internal_node_index);

   child_leaf_bounds[1] = leaf_bounds[split_index];
   child_nodes[1] = lvp_rebuild_subtree(output, leaf_nodes + split_index, leaf_bounds + split_index, internal_nodes,
                                        leaf_node_count - split_index, internal_node_index);

   struct lvp_bvh_box_node *node = (void *)(output + ir_id_to_offset(node_id));

   for (uint32_t i = 0; i < 2; i++) {
      node->children[i] = child_nodes[i];

      uint32_t type = child_nodes[i] & 3;
      if (type == lvp_bvh_node_internal) {
         const struct lvp_bvh_box_node *child_node =
            (void *)(output + ir_id_to_offset(child_nodes[i]));
         node->bounds[i].min.x = MIN2(child_node->bounds[0].min.x, child_node->bounds[1].min.x);
         node->bounds[i].min.y = MIN2(child_node->bounds[0].min.y, child_node->bounds[1].min.y);
         node->bounds[i].min.z = MIN2(child_node->bounds[0].min.z, child_node->bounds[1].min.z);
         node->bounds[i].max.x = MAX2(child_node->bounds[0].max.x, child_node->bounds[1].max.x);
         node->bounds[i].max.y = MAX2(child_node->bounds[0].max.y, child_node->bounds[1].max.y);
         node->bounds[i].max.z = MAX2(child_node->bounds[0].max.z, child_node->bounds[1].max.z);
      } else {
         node->bounds[i] = child_leaf_bounds[i];
      }
   }

   return node_id;
}

static void
lvp_flatten_as(const struct vk_ir_header *header, const struct vk_ir_box_node *ir_box_nodes,
               uint32_t root_offset, const uint32_t *node_depth, uint8_t *output)
{
   struct util_dynarray subtrees = { 0 };
   uint32_t *child_counts = NULL;
   uint32_t *leaf_nodes = NULL;
   vk_aabb *leaf_bounds = NULL;
   uint32_t *internal_nodes = NULL;

   child_counts = calloc(header->ir_internal_node_count, sizeof(uint32_t));
   if (!child_counts)
      goto ret;

   /* Iterate over the internal nodes in bottom-up order and
    * compute the the number child leafs for each internal node.
    */
   for (uint32_t i = 0; i < header->ir_internal_node_count; i++) {
      const struct vk_ir_box_node *ir_box = ir_box_nodes + i;
      for (uint32_t child_index = 0; child_index < 2; child_index++) {
         if (ir_box->children[child_index] == VK_BVH_INVALID_NODE)
            continue;

         uint32_t ir_child_offset = ir_id_to_offset(ir_box->children[child_index]);
         if (ir_child_offset < root_offset) {
            child_counts[i]++;
         } else {
            uint32_t src_index = (ir_child_offset - root_offset) / sizeof(struct vk_ir_box_node);
            child_counts[i] += child_counts[src_index];
         }
      }
   }

   /* Select the subtrees that have to be rebuilt in order to
    * limit the BVH to a supported depth.
    */
   util_dynarray_init(&subtrees, NULL);
   uint32_t max_subtree_size = 0;
   lvp_select_subtrees_to_flatten(header, ir_box_nodes, node_depth, child_counts,
                                  root_offset, header->ir_internal_node_count - 1,
                                  &subtrees, &max_subtree_size);

   leaf_nodes = calloc(max_subtree_size, sizeof(uint32_t));
   leaf_bounds = calloc(max_subtree_size, sizeof(vk_aabb));
   internal_nodes = calloc(max_subtree_size, sizeof(uint32_t));
   if (!leaf_nodes || !leaf_bounds || !internal_nodes)
      goto ret;

   util_dynarray_foreach(&subtrees, uint32_t, root_index) {
      uint32_t offset = sizeof(struct lvp_bvh_header) +
         (header->ir_internal_node_count - 1 - *root_index) * sizeof(struct lvp_bvh_box_node);

      internal_nodes[0] = offset | lvp_bvh_node_internal;

      uint32_t leaf_node_count = 0;
      uint32_t internal_node_count = 1;
      lvp_gather_subtree(output, offset, leaf_nodes, leaf_bounds, &leaf_node_count, internal_nodes, &internal_node_count);

      uint32_t internal_node_index = 0;
      lvp_rebuild_subtree(output, leaf_nodes, leaf_bounds, internal_nodes, leaf_node_count, &internal_node_index);
   }

ret:
   util_dynarray_fini(&subtrees);
   free(child_counts);
   free(leaf_nodes);
   free(leaf_bounds);
   free(internal_nodes);
}

static void
lvp_get_leaf_node_size(VkGeometryTypeKHR geometry_type, uint32_t *ir_leaf_node_size,
                       uint32_t *output_leaf_node_size)
{
   switch (geometry_type) {
   case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
      *ir_leaf_node_size = sizeof(struct vk_ir_triangle_node);
      *output_leaf_node_size = sizeof(struct lvp_bvh_triangle_node);
      break;
   case VK_GEOMETRY_TYPE_AABBS_KHR:
      *ir_leaf_node_size = sizeof(struct vk_ir_aabb_node);
      *output_leaf_node_size = sizeof(struct lvp_bvh_aabb_node);
      break;
   case VK_GEOMETRY_TYPE_INSTANCES_KHR:
      *ir_leaf_node_size = sizeof(struct vk_ir_instance_node);
      *output_leaf_node_size = sizeof(struct lvp_bvh_instance_node);
      break;
   default:
      break;
   }
}

void
lvp_encode_as(struct vk_acceleration_structure *dst, VkDeviceAddress intermediate_as_addr,
              VkDeviceAddress intermediate_header_addr, uint32_t leaf_count,
              VkGeometryTypeKHR geometry_type)
{
   const struct vk_ir_header *header = (const void *)(uintptr_t)intermediate_header_addr;
   const uint8_t *ir_bvh = (const void *)(uintptr_t)intermediate_as_addr;

   uint8_t *output = (void *)(uintptr_t)vk_acceleration_structure_get_va(dst);
   struct lvp_bvh_header *output_header = (void *)output;

   uint32_t ir_leaf_node_size = 0;
   uint32_t output_leaf_node_size = 0;
   lvp_get_leaf_node_size(geometry_type, &ir_leaf_node_size, &output_leaf_node_size);

   uint32_t root_offset = leaf_count * ir_leaf_node_size;
   const struct vk_ir_box_node *ir_box_nodes = (const void *)(ir_bvh + root_offset);

   output_header->bounds = ir_box_nodes[header->ir_internal_node_count - 1].base.aabb;

   if (geometry_type == VK_GEOMETRY_TYPE_INSTANCES_KHR)
      output_header->instance_count = leaf_count;
   else
      output_header->instance_count = 0;

   output_header->leaf_nodes_offset = sizeof(struct lvp_bvh_header) + header->ir_internal_node_count * sizeof(struct lvp_bvh_box_node);

   output_header->serialization_size = sizeof(struct lvp_accel_struct_serialization_header) +
                                       sizeof(uint64_t) * output_header->instance_count + dst->size;

   for (uint32_t i = 0; i < header->active_leaf_count; i++) {
      const void *ir_leaf = ir_bvh + i * ir_leaf_node_size;
      void *output_leaf = output + output_header->leaf_nodes_offset + i * output_leaf_node_size;
      switch (geometry_type) {
      case VK_GEOMETRY_TYPE_TRIANGLES_KHR: {
         const struct vk_ir_triangle_node *ir_triangle = ir_leaf;
         struct lvp_bvh_triangle_node *output_triangle = output_leaf;
         memcpy(output_triangle->coords, ir_triangle->coords, sizeof(output_triangle->coords));
         output_triangle->primitive_id = ir_triangle->triangle_id;
         output_triangle->geometry_id_and_flags = ir_triangle->geometry_id_and_flags;
         break;
      }
      case VK_GEOMETRY_TYPE_AABBS_KHR: {
         const struct vk_ir_aabb_node *ir_aabb = ir_leaf;
         struct lvp_bvh_aabb_node *output_aabb = output_leaf;
         output_aabb->bounds = ir_aabb->base.aabb;
         output_aabb->primitive_id = ir_aabb->primitive_id;
         output_aabb->geometry_id_and_flags = ir_aabb->geometry_id_and_flags;
         break;
      }
      case VK_GEOMETRY_TYPE_INSTANCES_KHR: {
         const struct vk_ir_instance_node *ir_instance = ir_leaf;
         struct lvp_bvh_instance_node *output_instance = output_leaf;
         output_instance->bvh_ptr = ir_instance->base_ptr;
         output_instance->custom_instance_and_mask = ir_instance->custom_instance_and_mask;
         output_instance->sbt_offset_and_flags =
            lvp_pack_sbt_offset_and_flags(ir_instance->sbt_offset_and_flags & 0xFFFFFF,
                                          ir_instance->sbt_offset_and_flags >> 24);
         output_instance->instance_id = ir_instance->instance_id;
         output_instance->otw_matrix = ir_instance->otw_matrix;

         float transform[16], inv_transform[16];
         memcpy(transform, &ir_instance->otw_matrix.values, sizeof(ir_instance->otw_matrix.values));
         transform[12] = transform[13] = transform[14] = 0.0f;
         transform[15] = 1.0f;

         util_invert_mat4x4(inv_transform, transform);
         memcpy(output_instance->wto_matrix.values, inv_transform, sizeof(output_instance->wto_matrix.values));

         break;
      }
      default:
         break;
      }
   }

   uint32_t *node_depth = calloc(header->ir_internal_node_count, sizeof(uint32_t));
   if (!node_depth)
      return;

   uint32_t max_node_depth = 0;

   for (uint32_t i = 0; i < header->ir_internal_node_count; i++) {
      const struct vk_ir_box_node *ir_box = ir_box_nodes + (header->ir_internal_node_count - i - 1);
      struct lvp_bvh_box_node *output_box =
         (void *)(output + sizeof(struct lvp_bvh_header) + i * sizeof(struct lvp_bvh_box_node));

      for (uint32_t child_index = 0; child_index < 2; child_index++) {
         if (ir_box->children[child_index] == VK_BVH_INVALID_NODE) {
            output_box->bounds[child_index] = (struct vk_aabb){
               .min.x = NAN,
               .min.y = NAN,
               .min.z = NAN,
               .max.x = NAN,
               .max.y = NAN,
               .max.z = NAN,
            };
            output_box->children[child_index] = LVP_BVH_INVALID_NODE;
            continue;
         }

         uint32_t ir_child_offset = ir_id_to_offset(ir_box->children[child_index]);
         const struct vk_ir_node *ir_child = (const void *)(ir_bvh + ir_child_offset);

         output_box->bounds[child_index] = ir_child->aabb;

         if (ir_child_offset < root_offset) {
            output_box->children[child_index] =
               output_header->leaf_nodes_offset + (ir_child_offset / ir_leaf_node_size) * output_leaf_node_size;
            switch (geometry_type) {
            case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
               output_box->children[child_index] |= lvp_bvh_node_triangle;
               break;
            case VK_GEOMETRY_TYPE_AABBS_KHR:
               output_box->children[child_index] |= lvp_bvh_node_aabb;
               break;
            case VK_GEOMETRY_TYPE_INSTANCES_KHR:
               output_box->children[child_index] |= lvp_bvh_node_instance;
               break;
            default:
               break;
            }
         } else {
            uint32_t src_index = (ir_child_offset - root_offset) / sizeof(struct vk_ir_box_node);
            uint32_t dst_index = header->ir_internal_node_count - src_index - 1;
            output_box->children[child_index] =
               sizeof(struct lvp_bvh_header) + dst_index * sizeof(struct lvp_bvh_box_node);
            output_box->children[child_index] |= lvp_bvh_node_internal;

            node_depth[dst_index] = node_depth[i] + 1;
            max_node_depth = MAX2(max_node_depth, node_depth[dst_index]);
         }
      }
   }

   /* The BVH exceeds the maximum depth supported by the traversal stack, 
    * flatten the offending parts of the tree.
    */
   if (max_node_depth >= 24)
      lvp_flatten_as(header, ir_box_nodes, root_offset, node_depth, output);

   free(node_depth);
}

static_assert(sizeof(struct lvp_bvh_triangle_node) % 8 == 0, "lvp_bvh_triangle_node is not padded");
static_assert(sizeof(struct lvp_bvh_aabb_node) % 8 == 0, "lvp_bvh_aabb_node is not padded");
static_assert(sizeof(struct lvp_bvh_instance_node) % 8 == 0, "lvp_bvh_instance_node is not padded");
static_assert(sizeof(struct lvp_bvh_box_node) % 8 == 0, "lvp_bvh_box_node is not padded");

VKAPI_ATTR void VKAPI_CALL
lvp_GetAccelerationStructureBuildSizesKHR(
   VkDevice _device, VkAccelerationStructureBuildTypeKHR buildType,
   const VkAccelerationStructureBuildGeometryInfoKHR *pBuildInfo,
   const uint32_t *pMaxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR *pSizeInfo)
{
   VK_FROM_HANDLE(lvp_device, device, _device);

   lvp_init_radix_sort(device);

   vk_get_as_build_sizes(_device, buildType, pBuildInfo, pMaxPrimitiveCounts,
                         pSizeInfo, &device->accel_struct_args);
}

VKAPI_ATTR VkResult VKAPI_CALL
lvp_WriteAccelerationStructuresPropertiesKHR(
   VkDevice _device, uint32_t accelerationStructureCount,
   const VkAccelerationStructureKHR *pAccelerationStructures, VkQueryType queryType,
   size_t dataSize, void *pData, size_t stride)
{
   unreachable("Unimplemented");
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL
lvp_BuildAccelerationStructuresKHR(
   VkDevice _device, VkDeferredOperationKHR deferredOperation, uint32_t infoCount,
   const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
   const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   unreachable("Unimplemented");
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR void VKAPI_CALL
lvp_GetDeviceAccelerationStructureCompatibilityKHR(
   VkDevice _device, const VkAccelerationStructureVersionInfoKHR *pVersionInfo,
   VkAccelerationStructureCompatibilityKHR *pCompatibility)
{
   uint8_t uuid[VK_UUID_SIZE];
   lvp_device_get_cache_uuid(uuid);
   bool compat = memcmp(pVersionInfo->pVersionData, uuid, VK_UUID_SIZE) == 0;
   *pCompatibility = compat ? VK_ACCELERATION_STRUCTURE_COMPATIBILITY_COMPATIBLE_KHR
                            : VK_ACCELERATION_STRUCTURE_COMPATIBILITY_INCOMPATIBLE_KHR;
}

VKAPI_ATTR VkResult VKAPI_CALL
lvp_CopyAccelerationStructureKHR(VkDevice _device, VkDeferredOperationKHR deferredOperation,
                                 const VkCopyAccelerationStructureInfoKHR *pInfo)
{
   unreachable("Unimplemented");
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL
lvp_CopyMemoryToAccelerationStructureKHR(VkDevice _device, VkDeferredOperationKHR deferredOperation,
                                         const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo)
{
   unreachable("Unimplemented");
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL
lvp_CopyAccelerationStructureToMemoryKHR(VkDevice _device, VkDeferredOperationKHR deferredOperation,
                                         const VkCopyAccelerationStructureToMemoryInfoKHR *pInfo)
{
   unreachable("Unimplemented");
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

static VkDeviceSize
lvp_get_as_size(VkDevice device,
                const VkAccelerationStructureBuildGeometryInfoKHR *build_info,
                uint32_t leaf_count)
{
   uint32_t internal_node_count = MAX2(leaf_count, 2) - 1;
   uint32_t nodes_size = internal_node_count * sizeof(struct lvp_bvh_box_node);

   uint32_t ir_leaf_node_size = 0;
   uint32_t output_leaf_node_size = 0;
   lvp_get_leaf_node_size(vk_get_as_geometry_type(build_info), &ir_leaf_node_size, &output_leaf_node_size);

   nodes_size += leaf_count * output_leaf_node_size;

   return sizeof(struct lvp_bvh_header) + nodes_size;
}

static uint32_t
lvp_get_encode_key(VkAccelerationStructureTypeKHR type,
                   VkBuildAccelerationStructureFlagBitsKHR flags)
{
   return 0;
}

static VkResult
lvp_encode_bind_pipeline(VkCommandBuffer cmd_buffer,
                         uint32_t key)
{
   return VK_SUCCESS;
}

const struct vk_acceleration_structure_build_ops accel_struct_ops = {
   .get_as_size = lvp_get_as_size,
   .get_encode_key[0] = lvp_get_encode_key,
   .encode_bind_pipeline[0] = lvp_encode_bind_pipeline,
   .encode_as[0] = lvp_enqueue_encode_as,
};

VkResult
lvp_device_init_accel_struct_state(struct lvp_device *device)
{
   device->accel_struct_args.subgroup_size = lp_native_vector_width / 32;

   device->vk.as_build_ops = &accel_struct_ops;
   device->vk.write_buffer_cp = lvp_write_buffer_cp;
   device->vk.flush_buffer_write_cp = lvp_flush_buffer_write_cp;
   device->vk.cmd_dispatch_unaligned = lvp_cmd_dispatch_unaligned;
   device->vk.cmd_fill_buffer_addr = lvp_cmd_fill_buffer_addr;

   simple_mtx_init(&device->radix_sort_lock, mtx_plain);

   return VK_SUCCESS;
}

void
lvp_device_finish_accel_struct_state(struct lvp_device *device)
{
   simple_mtx_destroy(&device->radix_sort_lock);

   if (device->radix_sort)
      radix_sort_vk_destroy(device->radix_sort, lvp_device_to_handle(device), &device->vk.alloc);
}

static void
lvp_enqueue_save_state(VkCommandBuffer cmdbuf)
{
   VK_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, cmdbuf);

   struct vk_cmd_queue_entry *entry =
      vk_zalloc(cmd_buffer->vk.cmd_queue.alloc, sizeof(struct vk_cmd_queue_entry),
                8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!entry)
      return;

   entry->type = LVP_CMD_SAVE_STATE;

   list_addtail(&entry->cmd_link, &cmd_buffer->vk.cmd_queue.cmds);
}

static void
lvp_enqueue_restore_state(VkCommandBuffer cmdbuf)
{
   VK_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, cmdbuf);

   struct vk_cmd_queue_entry *entry =
      vk_zalloc(cmd_buffer->vk.cmd_queue.alloc, sizeof(struct vk_cmd_queue_entry),
                8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!entry)
      return;

   entry->type = LVP_CMD_RESTORE_STATE;

   list_addtail(&entry->cmd_link, &cmd_buffer->vk.cmd_queue.cmds);
}

VKAPI_ATTR void VKAPI_CALL
lvp_CmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount,
                                      const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
                                      const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   VK_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);

   lvp_init_radix_sort(cmd_buffer->device);

   lvp_enqueue_save_state(commandBuffer);

   vk_cmd_build_acceleration_structures(commandBuffer, &cmd_buffer->device->vk, &cmd_buffer->device->meta,
                                        infoCount, pInfos, ppBuildRangeInfos, &cmd_buffer->device->accel_struct_args);

   lvp_enqueue_restore_state(commandBuffer);
}
