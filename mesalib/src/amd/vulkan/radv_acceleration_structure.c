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
#include "radv_private.h"

#include "util/half_float.h"
#include "nir_builder.h"
#include "radv_cs.h"
#include "radv_meta.h"

struct radv_accel_struct_header {
   uint32_t root_node_offset;
   uint32_t reserved;
   float aabb[2][3];
   uint64_t compacted_size;
   uint64_t serialization_size;
};

struct radv_bvh_triangle_node {
   float coords[3][3];
   uint32_t reserved[3];
   uint32_t triangle_id;
   /* flags in upper 4 bits */
   uint32_t geometry_id_and_flags;
   uint32_t reserved2;
   uint32_t id;
};

struct radv_bvh_aabb_node {
   float aabb[2][3];
   uint32_t primitive_id;
   /* flags in upper 4 bits */
   uint32_t geometry_id_and_flags;
   uint32_t reserved[8];
};

struct radv_bvh_instance_node {
   uint64_t base_ptr;
   /* lower 24 bits are the custom instance index, upper 8 bits are the visibility mask */
   uint32_t custom_instance_and_mask;
   /* lower 24 bits are the sbt offset, upper 8 bits are VkGeometryInstanceFlagsKHR */
   uint32_t sbt_offset_and_flags;

   /* The translation component is actually a pre-translation instead of a post-translation. If you
    * want to get a proper matrix out of it you need to apply the directional component of the
    * matrix to it. The pre-translation of the world->object matrix is the same as the
    * post-translation of the object->world matrix so this way we can share data between both
    * matrices. */
   float wto_matrix[12];
   float aabb[2][3];
   uint32_t instance_id;
   uint32_t reserved[9];
};

struct radv_bvh_box16_node {
   uint32_t children[4];
   uint32_t coords[4][3];
};

struct radv_bvh_box32_node {
   uint32_t children[4];
   float coords[4][2][3];
   uint32_t reserved[4];
};

void
radv_GetAccelerationStructureBuildSizesKHR(
   VkDevice _device, VkAccelerationStructureBuildTypeKHR buildType,
   const VkAccelerationStructureBuildGeometryInfoKHR *pBuildInfo,
   const uint32_t *pMaxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR *pSizeInfo)
{
   uint64_t triangles = 0, boxes = 0, instances = 0;

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
   uint64_t internal_nodes = 0;
   while (children > 1) {
      children = DIV_ROUND_UP(children, 4);
      internal_nodes += children;
   }

   /* The stray 128 is to ensure we have space for a header
    * which we'd want to use for some metadata (like the
    * total AABB of the BVH) */
   uint64_t size = boxes * 128 + instances * 128 + triangles * 64 + internal_nodes * 128 + 192;

   pSizeInfo->accelerationStructureSize = size;

   /* 2x the max number of nodes in a BVH layer (one uint32_t each) */
   pSizeInfo->updateScratchSize = pSizeInfo->buildScratchSize =
      MAX2(4096, 2 * (boxes + instances + triangles) * sizeof(uint32_t));
}

VkResult
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &accel->base, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR);

   accel->mem_offset = buffer->offset + pCreateInfo->offset;
   accel->size = pCreateInfo->size;
   accel->bo = buffer->bo;

   *pAccelerationStructure = radv_acceleration_structure_to_handle(accel);
   return VK_SUCCESS;
}

void
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

VkDeviceAddress
radv_GetAccelerationStructureDeviceAddressKHR(
   VkDevice _device, const VkAccelerationStructureDeviceAddressInfoKHR *pInfo)
{
   RADV_FROM_HANDLE(radv_acceleration_structure, accel, pInfo->accelerationStructure);
   return radv_accel_struct_get_va(accel);
}

VkResult
radv_WriteAccelerationStructuresPropertiesKHR(
   VkDevice _device, uint32_t accelerationStructureCount,
   const VkAccelerationStructureKHR *pAccelerationStructures, VkQueryType queryType,
   size_t dataSize, void *pData, size_t stride)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   char *data_out = (char*)pData;

   for (uint32_t i = 0; i < accelerationStructureCount; ++i) {
      RADV_FROM_HANDLE(radv_acceleration_structure, accel, pAccelerationStructures[i]);
      const char *base_ptr = (const char *)device->ws->buffer_map(accel->bo);
      if (!base_ptr)
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

      const struct radv_accel_struct_header *header = (const void*)(base_ptr + accel->mem_offset);
      if (stride * i + sizeof(VkDeviceSize) <= dataSize) {
         uint64_t value;
         switch (queryType) {
         case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR:
            value = header->compacted_size;
            break;
         case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR:
            value = header->serialization_size;
            break;
         default:
            unreachable("Unhandled acceleration structure query");
         }
         *(VkDeviceSize *)(data_out + stride * i) = value;
      }
      device->ws->buffer_unmap(accel->bo);
   }
   return VK_SUCCESS;
}

struct radv_bvh_build_ctx {
   uint32_t *write_scratch;
   char *base;
   char *curr_ptr;
};

static void
build_triangles(struct radv_bvh_build_ctx *ctx, const VkAccelerationStructureGeometryKHR *geom,
                const VkAccelerationStructureBuildRangeInfoKHR *range, unsigned geometry_id)
{
   const VkAccelerationStructureGeometryTrianglesDataKHR *tri_data = &geom->geometry.triangles;
   VkTransformMatrixKHR matrix;
   const char *index_data = (const char *)tri_data->indexData.hostAddress + range->primitiveOffset;

   if (tri_data->transformData.hostAddress) {
      matrix = *(const VkTransformMatrixKHR *)((const char *)tri_data->transformData.hostAddress +
                                               range->transformOffset);
   } else {
      matrix = (VkTransformMatrixKHR){
         .matrix = {{1.0, 0.0, 0.0, 0.0}, {0.0, 1.0, 0.0, 0.0}, {0.0, 0.0, 1.0, 0.0}}};
   }

   for (uint32_t p = 0; p < range->primitiveCount; ++p, ctx->curr_ptr += 64) {
      struct radv_bvh_triangle_node *node = (void*)ctx->curr_ptr;
      uint32_t node_offset = ctx->curr_ptr - ctx->base;
      uint32_t node_id = node_offset >> 3;
      *ctx->write_scratch++ = node_id;

      for (unsigned v = 0; v < 3; ++v) {
         uint32_t v_index = range->firstVertex;
         switch (tri_data->indexType) {
         case VK_INDEX_TYPE_NONE_KHR:
            v_index += p * 3 + v;
            break;
         case VK_INDEX_TYPE_UINT8_EXT:
            v_index += *(const uint8_t *)index_data;
            index_data += 1;
            break;
         case VK_INDEX_TYPE_UINT16:
            v_index += *(const uint16_t *)index_data;
            index_data += 2;
            break;
         case VK_INDEX_TYPE_UINT32:
            v_index += *(const uint32_t *)index_data;
            index_data += 4;
            break;
         case VK_INDEX_TYPE_MAX_ENUM:
            unreachable("Unhandled VK_INDEX_TYPE_MAX_ENUM");
            break;
         }

         const char *v_data = (const char *)tri_data->vertexData.hostAddress + v_index * tri_data->vertexStride;
         float coords[4];
         switch (tri_data->vertexFormat) {
         case VK_FORMAT_R32G32B32_SFLOAT:
            coords[0] = *(const float *)(v_data + 0);
            coords[1] = *(const float *)(v_data + 4);
            coords[2] = *(const float *)(v_data + 8);
            coords[3] = 1.0f;
            break;
         case VK_FORMAT_R32G32B32A32_SFLOAT:
            coords[0] = *(const float *)(v_data + 0);
            coords[1] = *(const float *)(v_data + 4);
            coords[2] = *(const float *)(v_data + 8);
            coords[3] = *(const float *)(v_data + 12);
            break;
         case VK_FORMAT_R16G16B16_SFLOAT:
            coords[0] = _mesa_half_to_float(*(const uint16_t *)(v_data + 0));
            coords[1] = _mesa_half_to_float(*(const uint16_t *)(v_data + 2));
            coords[2] = _mesa_half_to_float(*(const uint16_t *)(v_data + 4));
            coords[3] = 1.0f;
            break;
         case VK_FORMAT_R16G16B16A16_SFLOAT:
            coords[0] = _mesa_half_to_float(*(const uint16_t *)(v_data + 0));
            coords[1] = _mesa_half_to_float(*(const uint16_t *)(v_data + 2));
            coords[2] = _mesa_half_to_float(*(const uint16_t *)(v_data + 4));
            coords[3] = _mesa_half_to_float(*(const uint16_t *)(v_data + 6));
            break;
         default:
            unreachable("Unhandled vertex format in BVH build");
         }

         for (unsigned j = 0; j < 3; ++j) {
            float r = 0;
            for (unsigned k = 0; k < 4; ++k)
               r += matrix.matrix[j][k] * coords[k];
            node->coords[v][j] = r;
         }

         node->triangle_id = p;
         node->geometry_id_and_flags = geometry_id | (geom->flags << 28);

         /* Seems to be needed for IJ, otherwise I = J = ? */
         node->id = 9;
      }
   }
}

static VkResult
build_instances(struct radv_device *device, struct radv_bvh_build_ctx *ctx,
                const VkAccelerationStructureGeometryKHR *geom,
                const VkAccelerationStructureBuildRangeInfoKHR *range)
{
   const VkAccelerationStructureGeometryInstancesDataKHR *inst_data = &geom->geometry.instances;

   for (uint32_t p = 0; p < range->primitiveCount; ++p, ctx->curr_ptr += 128) {
      const VkAccelerationStructureInstanceKHR *instance =
         inst_data->arrayOfPointers
            ? (((const VkAccelerationStructureInstanceKHR *const *)inst_data->data.hostAddress)[p])
            : &((const VkAccelerationStructureInstanceKHR *)inst_data->data.hostAddress)[p];
      if (!instance->accelerationStructureReference) {
         continue;
      }

      struct radv_bvh_instance_node *node = (void*)ctx->curr_ptr;
      uint32_t node_offset = ctx->curr_ptr - ctx->base;
      uint32_t node_id = (node_offset >> 3) | 6;
      *ctx->write_scratch++ = node_id;

      float transform[16], inv_transform[16];
      memcpy(transform, &instance->transform.matrix, sizeof(instance->transform.matrix));
      transform[12] = transform[13] = transform[14] = 0.0f;
      transform[15] = 1.0f;

      util_invert_mat4x4(inv_transform, transform);
      memcpy(node->wto_matrix, inv_transform, sizeof(node->wto_matrix));
      node->wto_matrix[3] = transform[3];
      node->wto_matrix[7] = transform[7];
      node->wto_matrix[11] = transform[11];
      node->custom_instance_and_mask = instance->instanceCustomIndex | (instance->mask << 24);
      node->sbt_offset_and_flags =
         instance->instanceShaderBindingTableRecordOffset | (instance->flags << 24);
      node->instance_id = p;

      RADV_FROM_HANDLE(radv_acceleration_structure, src_accel_struct,
                       (VkAccelerationStructureKHR)instance->accelerationStructureReference);
      const void *src_base = device->ws->buffer_map(src_accel_struct->bo);
      if (!src_base)
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

      src_base = (const char *)src_base + src_accel_struct->mem_offset;
      const struct radv_accel_struct_header *src_header = src_base;
      node->base_ptr = radv_accel_struct_get_va(src_accel_struct) | src_header->root_node_offset;

      for (unsigned j = 0; j < 3; ++j) {
         node->aabb[0][j] = instance->transform.matrix[j][3];
         node->aabb[1][j] = instance->transform.matrix[j][3];
         for (unsigned k = 0; k < 3; ++k) {
            node->aabb[0][j] += MIN2(instance->transform.matrix[j][k] * src_header->aabb[0][k],
                                     instance->transform.matrix[j][k] * src_header->aabb[1][k]);
            node->aabb[1][j] += MAX2(instance->transform.matrix[j][k] * src_header->aabb[0][k],
                                     instance->transform.matrix[j][k] * src_header->aabb[1][k]);
         }
      }
      device->ws->buffer_unmap(src_accel_struct->bo);
   }
   return VK_SUCCESS;
}

static void
build_aabbs(struct radv_bvh_build_ctx *ctx, const VkAccelerationStructureGeometryKHR *geom,
            const VkAccelerationStructureBuildRangeInfoKHR *range, unsigned geometry_id)
{
   const VkAccelerationStructureGeometryAabbsDataKHR *aabb_data = &geom->geometry.aabbs;

   for (uint32_t p = 0; p < range->primitiveCount; ++p, ctx->curr_ptr += 64) {
      struct radv_bvh_aabb_node *node = (void*)ctx->curr_ptr;
      uint32_t node_offset = ctx->curr_ptr - ctx->base;
      uint32_t node_id = (node_offset >> 3) | 6;
      *ctx->write_scratch++ = node_id;

      const VkAabbPositionsKHR *aabb =
         (const VkAabbPositionsKHR *)((const char *)aabb_data->data.hostAddress +
                                      p * aabb_data->stride);

      node->aabb[0][0] = aabb->minX;
      node->aabb[0][1] = aabb->minY;
      node->aabb[0][2] = aabb->minZ;
      node->aabb[1][0] = aabb->maxX;
      node->aabb[1][1] = aabb->maxY;
      node->aabb[1][2] = aabb->maxZ;
      node->primitive_id = p;
      node->geometry_id_and_flags = geometry_id;
   }
}

static uint32_t
leaf_node_count(const VkAccelerationStructureBuildGeometryInfoKHR *info,
                const VkAccelerationStructureBuildRangeInfoKHR *ranges)
{
   uint32_t count = 0;
   for (uint32_t i = 0; i < info->geometryCount; ++i) {
      count += ranges[i].primitiveCount;
   }
   return count;
}

static void
compute_bounds(const char *base_ptr, uint32_t node_id, float *bounds)
{
   for (unsigned i = 0; i < 3; ++i)
      bounds[i] = INFINITY;
   for (unsigned i = 0; i < 3; ++i)
      bounds[3 + i] = -INFINITY;

   switch (node_id & 7) {
   case 0: {
      const struct radv_bvh_triangle_node *node = (const void*)(base_ptr + (node_id / 8 * 64));
      for (unsigned v = 0; v < 3; ++v) {
         for (unsigned j = 0; j < 3; ++j) {
            bounds[j] = MIN2(bounds[j], node->coords[v][j]);
            bounds[3 + j] = MAX2(bounds[3 + j], node->coords[v][j]);
         }
      }
      break;
   }
   case 5: {
      const struct radv_bvh_box32_node *node = (const void*)(base_ptr + (node_id / 8 * 64));
      for (unsigned c2 = 0; c2 < 4; ++c2) {
         if (isnan(node->coords[c2][0][0]))
            continue;
         for (unsigned j = 0; j < 3; ++j) {
            bounds[j] = MIN2(bounds[j], node->coords[c2][0][j]);
            bounds[3 + j] = MAX2(bounds[3 + j], node->coords[c2][1][j]);
         }
      }
      break;
   }
   case 6: {
      const struct radv_bvh_instance_node *node = (const void*)(base_ptr + (node_id / 8 * 64));
      for (unsigned j = 0; j < 3; ++j) {
         bounds[j] = MIN2(bounds[j], node->aabb[0][j]);
         bounds[3 + j] = MAX2(bounds[3 + j], node->aabb[1][j]);
      }
      break;
   }
   case 7: {
      const struct radv_bvh_aabb_node *node = (const void*)(base_ptr + (node_id / 8 * 64));
      for (unsigned j = 0; j < 3; ++j) {
         bounds[j] = MIN2(bounds[j], node->aabb[0][j]);
         bounds[3 + j] = MAX2(bounds[3 + j], node->aabb[1][j]);
      }
      break;
   }
   }
}

static VkResult
build_bvh(struct radv_device *device, const VkAccelerationStructureBuildGeometryInfoKHR *info,
          const VkAccelerationStructureBuildRangeInfoKHR *ranges)
{
   RADV_FROM_HANDLE(radv_acceleration_structure, accel, info->dstAccelerationStructure);
   VkResult result = VK_SUCCESS;

   uint32_t *scratch[2];
   scratch[0] = info->scratchData.hostAddress;
   scratch[1] = scratch[0] + leaf_node_count(info, ranges);

   char *base_ptr = (char*)device->ws->buffer_map(accel->bo);
   if (!base_ptr)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   base_ptr = base_ptr + accel->mem_offset;
   struct radv_accel_struct_header *header = (void*)base_ptr;
   void *first_node_ptr = (char *)base_ptr + ALIGN(sizeof(*header), 64);

   struct radv_bvh_build_ctx ctx = {.write_scratch = scratch[0],
                                    .base = base_ptr,
                                    .curr_ptr = (char *)first_node_ptr + 128};

   /* This initializes the leaf nodes of the BVH all at the same level. */
   for (uint32_t i = 0; i < info->geometryCount; ++i) {
      const VkAccelerationStructureGeometryKHR *geom =
         info->pGeometries ? &info->pGeometries[i] : info->ppGeometries[i];

      switch (geom->geometryType) {
      case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
         build_triangles(&ctx, geom, ranges + i, i);
         break;
      case VK_GEOMETRY_TYPE_AABBS_KHR:
         build_aabbs(&ctx, geom, ranges + i, i);
         break;
      case VK_GEOMETRY_TYPE_INSTANCES_KHR: {
         result = build_instances(device, &ctx, geom, ranges + i);
         if (result != VK_SUCCESS)
            goto fail;
         break;
      }
      case VK_GEOMETRY_TYPE_MAX_ENUM_KHR:
         unreachable("VK_GEOMETRY_TYPE_MAX_ENUM_KHR unhandled");
      }
   }

   uint32_t node_counts[2] = {ctx.write_scratch - scratch[0], 0};
   unsigned d;

   /*
    * This is the most naive BVH building algorithm I could think of:
    * just iteratively builds each level from bottom to top with
    * the children of each node being in-order and tightly packed.
    *
    * Is probably terrible for traversal but should be easy to build an
    * equivalent GPU version.
    */
   for (d = 0; node_counts[d & 1] > 1 || d == 0; ++d) {
      uint32_t child_count = node_counts[d & 1];
      const uint32_t *children = scratch[d & 1];
      uint32_t *dst_ids = scratch[(d & 1) ^ 1];
      unsigned dst_count;
      unsigned child_idx = 0;
      for (dst_count = 0; child_idx < MAX2(1, child_count); ++dst_count, child_idx += 4) {
         unsigned local_child_count = MIN2(4, child_count - child_idx);
         uint32_t child_ids[4];
         float bounds[4][6];

         for (unsigned c = 0; c < local_child_count; ++c) {
            uint32_t id = children[child_idx + c];
            child_ids[c] = id;

            compute_bounds(base_ptr, id, bounds[c]);
         }

         struct radv_bvh_box32_node *node;

         /* Put the root node at base_ptr so the id = 0, which allows some
          * traversal optimizations. */
         if (child_idx == 0 && local_child_count == child_count) {
            node = first_node_ptr;
            header->root_node_offset = ((char *)first_node_ptr - (char *)base_ptr) / 64 * 8 + 5;
         } else {
            uint32_t dst_id = (ctx.curr_ptr - base_ptr) / 64;
            dst_ids[dst_count] = dst_id * 8 + 5;

            node = (void*)ctx.curr_ptr;
            ctx.curr_ptr += 128;
         }

         for (unsigned c = 0; c < local_child_count; ++c) {
            node->children[c] = child_ids[c];
            for (unsigned i = 0; i < 2; ++i)
               for (unsigned j = 0; j < 3; ++j)
                  node->coords[c][i][j] = bounds[c][i * 3 + j];
         }
         for (unsigned c = local_child_count; c < 4; ++c) {
            for (unsigned i = 0; i < 2; ++i)
               for (unsigned j = 0; j < 3; ++j)
                  node->coords[c][i][j] = NAN;
         }
      }

      node_counts[(d & 1) ^ 1] = dst_count;
   }

   compute_bounds(base_ptr, header->root_node_offset, &header->aabb[0][0]);

   /* TODO init sizes and figure out what is needed for serialization. */

fail:
   device->ws->buffer_unmap(accel->bo);
   return result;
}

VkResult
radv_BuildAccelerationStructuresKHR(
   VkDevice _device, VkDeferredOperationKHR deferredOperation, uint32_t infoCount,
   const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
   const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VkResult result = VK_SUCCESS;

   for (uint32_t i = 0; i < infoCount; ++i) {
      result = build_bvh(device, pInfos + i, ppBuildRangeInfos[i]);
      if (result != VK_SUCCESS)
         break;
   }
   return result;
}

static nir_ssa_def *
get_indices(nir_builder *b, nir_ssa_def *addr, nir_ssa_def *type, nir_ssa_def *id)
{
   const struct glsl_type *uvec3_type = glsl_vector_type(GLSL_TYPE_UINT, 3);
   nir_variable *result =
      nir_variable_create(b->shader, nir_var_shader_temp, uvec3_type, "indices");

   nir_push_if(b, nir_ult(b, type, nir_imm_int(b, 2)));
   nir_push_if(b, nir_ieq(b, type, nir_imm_int(b, VK_INDEX_TYPE_UINT16)));
   {
      nir_ssa_def *index_id = nir_umul24(b, id, nir_imm_int(b, 6));
      nir_ssa_def *indices[3];
      for (unsigned i = 0; i < 3; ++i) {
         indices[i] = nir_build_load_global(
            b, 1, 16, nir_iadd(b, addr, nir_u2u64(b, nir_iadd(b, index_id, nir_imm_int(b, 2 * i)))),
            .align_mul = 2, .align_offset = 0);
      }
      nir_store_var(b, result, nir_u2u32(b, nir_vec(b, indices, 3)), 7);
   }
   nir_push_else(b, NULL);
   {
      nir_ssa_def *index_id = nir_umul24(b, id, nir_imm_int(b, 12));
      nir_ssa_def *indices = nir_build_load_global(
         b, 3, 32, nir_iadd(b, addr, nir_u2u64(b, index_id)), .align_mul = 4, .align_offset = 0);
      nir_store_var(b, result, indices, 7);
   }
   nir_pop_if(b, NULL);
   nir_push_else(b, NULL);
   {
      nir_ssa_def *index_id = nir_umul24(b, id, nir_imm_int(b, 3));
      nir_ssa_def *indices[] = {
         index_id,
         nir_iadd(b, index_id, nir_imm_int(b, 1)),
         nir_iadd(b, index_id, nir_imm_int(b, 2)),
      };

      nir_push_if(b, nir_ieq(b, type, nir_imm_int(b, VK_INDEX_TYPE_NONE_KHR)));
      {
         nir_store_var(b, result, nir_vec(b, indices, 3), 7);
      }
      nir_push_else(b, NULL);
      {
         for (unsigned i = 0; i < 3; ++i) {
            indices[i] = nir_build_load_global(b, 1, 8, nir_iadd(b, addr, nir_u2u64(b, indices[i])),
                                               .align_mul = 1, .align_offset = 0);
         }
         nir_store_var(b, result, nir_u2u32(b, nir_vec(b, indices, 3)), 7);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
   return nir_load_var(b, result);
}

static void
get_vertices(nir_builder *b, nir_ssa_def *addresses, nir_ssa_def *format, nir_ssa_def *positions[3])
{
   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);
   nir_variable *results[3] = {
      nir_variable_create(b->shader, nir_var_shader_temp, vec3_type, "vertex0"),
      nir_variable_create(b->shader, nir_var_shader_temp, vec3_type, "vertex1"),
      nir_variable_create(b->shader, nir_var_shader_temp, vec3_type, "vertex2")};

   VkFormat formats[] = {
      VK_FORMAT_R32G32B32_SFLOAT,
      VK_FORMAT_R32G32B32A32_SFLOAT,
      VK_FORMAT_R16G16B16_SFLOAT,
      VK_FORMAT_R16G16B16A16_SFLOAT,
   };

   for (unsigned f = 0; f < ARRAY_SIZE(formats); ++f) {
      if (f + 1 < ARRAY_SIZE(formats))
         nir_push_if(b, nir_ieq(b, format, nir_imm_int(b, formats[f])));

      for (unsigned i = 0; i < 3; ++i) {
         switch (formats[f]) {
         case VK_FORMAT_R32G32B32_SFLOAT:
         case VK_FORMAT_R32G32B32A32_SFLOAT:
            nir_store_var(b, results[i],
                          nir_build_load_global(b, 3, 32, nir_channel(b, addresses, i),
                                                .align_mul = 4, .align_offset = 0),
                          7);
            break;
         case VK_FORMAT_R16G16B16_SFLOAT:
         case VK_FORMAT_R16G16B16A16_SFLOAT: {
            nir_ssa_def *values[3];
            nir_ssa_def *addr = nir_channel(b, addresses, i);
            for (unsigned j = 0; j < 3; ++j)
               values[j] =
                  nir_build_load_global(b, 1, 16, nir_iadd(b, addr, nir_imm_int64(b, j * 2)),
                                        .align_mul = 2, .align_offset = 0);
            nir_store_var(b, results[i], nir_f2f32(b, nir_vec(b, values, 3)), 7);
            break;
         }
         default:
            unreachable("Unhandled format");
         }
      }
      if (f + 1 < ARRAY_SIZE(formats))
         nir_push_else(b, NULL);
   }
   for (unsigned f = 1; f < ARRAY_SIZE(formats); ++f) {
      nir_pop_if(b, NULL);
   }

   for (unsigned i = 0; i < 3; ++i)
      positions[i] = nir_load_var(b, results[i]);
}

struct build_primitive_constants {
   uint64_t node_dst_addr;
   uint64_t scratch_addr;
   uint32_t dst_offset;
   uint32_t dst_scratch_offset;
   uint32_t geometry_type;
   uint32_t geometry_id;

   union {
      struct {
         uint64_t vertex_addr;
         uint64_t index_addr;
         uint64_t transform_addr;
         uint32_t vertex_stride;
         uint32_t vertex_format;
         uint32_t index_format;
      };
      struct {
         uint64_t instance_data;
      };
      struct {
         uint64_t aabb_addr;
         uint32_t aabb_stride;
      };
   };
};

struct build_internal_constants {
   uint64_t node_dst_addr;
   uint64_t scratch_addr;
   uint32_t dst_offset;
   uint32_t dst_scratch_offset;
   uint32_t src_scratch_offset;
   uint32_t fill_header;
};

/* This inverts a 3x3 matrix using cofactors, as in e.g.
 * https://www.mathsisfun.com/algebra/matrix-inverse-minors-cofactors-adjugate.html */
static void
nir_invert_3x3(nir_builder *b, nir_ssa_def *in[3][3], nir_ssa_def *out[3][3])
{
   nir_ssa_def *cofactors[3][3];
   for (unsigned i = 0; i < 3; ++i) {
      for (unsigned j = 0; j < 3; ++j) {
         cofactors[i][j] =
            nir_fsub(b, nir_fmul(b, in[(i + 1) % 3][(j + 1) % 3], in[(i + 2) % 3][(j + 2) % 3]),
                     nir_fmul(b, in[(i + 1) % 3][(j + 2) % 3], in[(i + 2) % 3][(j + 1) % 3]));
      }
   }

   nir_ssa_def *det = NULL;
   for (unsigned i = 0; i < 3; ++i) {
      nir_ssa_def *det_part = nir_fmul(b, in[0][i], cofactors[0][i]);
      det = det ? nir_fadd(b, det, det_part) : det_part;
   }

   nir_ssa_def *det_inv = nir_frcp(b, det);
   for (unsigned i = 0; i < 3; ++i) {
      for (unsigned j = 0; j < 3; ++j) {
         out[i][j] = nir_fmul(b, cofactors[j][i], det_inv);
      }
   }
}

static nir_shader *
build_leaf_shader(struct radv_device *dev)
{
   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);
   nir_builder b =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL, "accel_build_leaf_shader");

   b.shader->info.workgroup_size[0] = 64;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;

   nir_ssa_def *pconst0 =
      nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .base = 0, .range = 16);
   nir_ssa_def *pconst1 =
      nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .base = 16, .range = 16);
   nir_ssa_def *pconst2 =
      nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .base = 32, .range = 16);
   nir_ssa_def *pconst3 =
      nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .base = 48, .range = 16);
   nir_ssa_def *pconst4 =
      nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 0), .base = 64, .range = 4);

   nir_ssa_def *geom_type = nir_channel(&b, pconst1, 2);
   nir_ssa_def *node_dst_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst0, 3));
   nir_ssa_def *scratch_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst0, 12));
   nir_ssa_def *node_dst_offset = nir_channel(&b, pconst1, 0);
   nir_ssa_def *scratch_offset = nir_channel(&b, pconst1, 1);
   nir_ssa_def *geometry_id = nir_channel(&b, pconst1, 3);

   nir_ssa_def *global_id =
      nir_iadd(&b,
               nir_umul24(&b, nir_channels(&b, nir_load_workgroup_id(&b, 32), 1),
                          nir_imm_int(&b, b.shader->info.workgroup_size[0])),
               nir_channels(&b, nir_load_local_invocation_id(&b), 1));
   scratch_addr = nir_iadd(
      &b, scratch_addr,
      nir_u2u64(&b, nir_iadd(&b, scratch_offset, nir_umul24(&b, global_id, nir_imm_int(&b, 4)))));

   nir_push_if(&b, nir_ieq(&b, geom_type, nir_imm_int(&b, VK_GEOMETRY_TYPE_TRIANGLES_KHR)));
   { /* Triangles */
      nir_ssa_def *vertex_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst2, 3));
      nir_ssa_def *index_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst2, 12));
      nir_ssa_def *transform_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst3, 3));
      nir_ssa_def *vertex_stride = nir_channel(&b, pconst3, 2);
      nir_ssa_def *vertex_format = nir_channel(&b, pconst3, 3);
      nir_ssa_def *index_format = nir_channel(&b, pconst4, 0);
      unsigned repl_swizzle[4] = {0, 0, 0, 0};

      nir_ssa_def *node_offset =
         nir_iadd(&b, node_dst_offset, nir_umul24(&b, global_id, nir_imm_int(&b, 64)));
      nir_ssa_def *triangle_node_dst_addr = nir_iadd(&b, node_dst_addr, nir_u2u64(&b, node_offset));

      nir_ssa_def *indices = get_indices(&b, index_addr, index_format, global_id);
      nir_ssa_def *vertex_addresses = nir_iadd(
         &b, nir_u2u64(&b, nir_imul(&b, indices, nir_swizzle(&b, vertex_stride, repl_swizzle, 3))),
         nir_swizzle(&b, vertex_addr, repl_swizzle, 3));
      nir_ssa_def *positions[3];
      get_vertices(&b, vertex_addresses, vertex_format, positions);

      nir_ssa_def *node_data[16];
      memset(node_data, 0, sizeof(node_data));

      nir_variable *transform[] = {
         nir_variable_create(b.shader, nir_var_shader_temp, glsl_vec4_type(), "transform0"),
         nir_variable_create(b.shader, nir_var_shader_temp, glsl_vec4_type(), "transform1"),
         nir_variable_create(b.shader, nir_var_shader_temp, glsl_vec4_type(), "transform2"),
      };
      nir_store_var(&b, transform[0], nir_imm_vec4(&b, 1.0, 0.0, 0.0, 0.0), 0xf);
      nir_store_var(&b, transform[1], nir_imm_vec4(&b, 0.0, 1.0, 0.0, 0.0), 0xf);
      nir_store_var(&b, transform[2], nir_imm_vec4(&b, 0.0, 0.0, 1.0, 0.0), 0xf);

      nir_push_if(&b, nir_ine(&b, transform_addr, nir_imm_int64(&b, 0)));
      nir_store_var(
         &b, transform[0],
         nir_build_load_global(&b, 4, 32, nir_iadd(&b, transform_addr, nir_imm_int64(&b, 0)),
                               .align_mul = 4, .align_offset = 0),
         0xf);
      nir_store_var(
         &b, transform[1],
         nir_build_load_global(&b, 4, 32, nir_iadd(&b, transform_addr, nir_imm_int64(&b, 16)),
                               .align_mul = 4, .align_offset = 0),
         0xf);
      nir_store_var(
         &b, transform[2],
         nir_build_load_global(&b, 4, 32, nir_iadd(&b, transform_addr, nir_imm_int64(&b, 32)),
                               .align_mul = 4, .align_offset = 0),
         0xf);
      nir_pop_if(&b, NULL);

      for (unsigned i = 0; i < 3; ++i)
         for (unsigned j = 0; j < 3; ++j)
            node_data[i * 3 + j] = nir_fdph(&b, positions[i], nir_load_var(&b, transform[j]));

      node_data[12] = global_id;
      node_data[13] = geometry_id;
      node_data[15] = nir_imm_int(&b, 9);
      for (unsigned i = 0; i < ARRAY_SIZE(node_data); ++i)
         if (!node_data[i])
            node_data[i] = nir_imm_int(&b, 0);

      for (unsigned i = 0; i < 4; ++i) {
         nir_build_store_global(&b, nir_vec(&b, node_data + i * 4, 4),
                                nir_iadd(&b, triangle_node_dst_addr, nir_imm_int64(&b, i * 16)),
                                .write_mask = 15, .align_mul = 16, .align_offset = 0);
      }

      nir_ssa_def *node_id = nir_ushr(&b, node_offset, nir_imm_int(&b, 3));
      nir_build_store_global(&b, node_id, scratch_addr, .write_mask = 1, .align_mul = 4,
                             .align_offset = 0);
   }
   nir_push_else(&b, NULL);
   nir_push_if(&b, nir_ieq(&b, geom_type, nir_imm_int(&b, VK_GEOMETRY_TYPE_AABBS_KHR)));
   { /* AABBs */
      nir_ssa_def *aabb_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst2, 3));
      nir_ssa_def *aabb_stride = nir_channel(&b, pconst2, 2);

      nir_ssa_def *node_offset =
         nir_iadd(&b, node_dst_offset, nir_umul24(&b, global_id, nir_imm_int(&b, 64)));
      nir_ssa_def *aabb_node_dst_addr = nir_iadd(&b, node_dst_addr, nir_u2u64(&b, node_offset));
      nir_ssa_def *node_id =
         nir_iadd(&b, nir_ushr(&b, node_offset, nir_imm_int(&b, 3)), nir_imm_int(&b, 7));
      nir_build_store_global(&b, node_id, scratch_addr, .write_mask = 1, .align_mul = 4,
                             .align_offset = 0);

      aabb_addr = nir_iadd(&b, aabb_addr, nir_u2u64(&b, nir_imul(&b, aabb_stride, global_id)));

      nir_ssa_def *min_bound =
         nir_build_load_global(&b, 3, 32, nir_iadd(&b, aabb_addr, nir_imm_int64(&b, 0)),
                               .align_mul = 4, .align_offset = 0);
      nir_ssa_def *max_bound =
         nir_build_load_global(&b, 3, 32, nir_iadd(&b, aabb_addr, nir_imm_int64(&b, 12)),
                               .align_mul = 4, .align_offset = 0);

      nir_ssa_def *values[] = {nir_channel(&b, min_bound, 0),
                               nir_channel(&b, min_bound, 1),
                               nir_channel(&b, min_bound, 2),
                               nir_channel(&b, max_bound, 0),
                               nir_channel(&b, max_bound, 1),
                               nir_channel(&b, max_bound, 2),
                               global_id,
                               geometry_id};

      nir_build_store_global(&b, nir_vec(&b, values + 0, 4),
                             nir_iadd(&b, aabb_node_dst_addr, nir_imm_int64(&b, 0)),
                             .write_mask = 15, .align_mul = 16, .align_offset = 0);
      nir_build_store_global(&b, nir_vec(&b, values + 4, 4),
                             nir_iadd(&b, aabb_node_dst_addr, nir_imm_int64(&b, 16)),
                             .write_mask = 15, .align_mul = 16, .align_offset = 0);
   }
   nir_push_else(&b, NULL);
   { /* Instances */

      nir_ssa_def *instance_addr =
         nir_iadd(&b, nir_pack_64_2x32(&b, nir_channels(&b, pconst2, 3)),
                  nir_u2u64(&b, nir_imul(&b, global_id, nir_imm_int(&b, 64))));
      nir_ssa_def *inst_transform[] = {
         nir_build_load_global(&b, 4, 32, nir_iadd(&b, instance_addr, nir_imm_int64(&b, 0)),
                               .align_mul = 4, .align_offset = 0),
         nir_build_load_global(&b, 4, 32, nir_iadd(&b, instance_addr, nir_imm_int64(&b, 16)),
                               .align_mul = 4, .align_offset = 0),
         nir_build_load_global(&b, 4, 32, nir_iadd(&b, instance_addr, nir_imm_int64(&b, 32)),
                               .align_mul = 4, .align_offset = 0)};
      nir_ssa_def *inst3 =
         nir_build_load_global(&b, 4, 32, nir_iadd(&b, instance_addr, nir_imm_int64(&b, 48)),
                               .align_mul = 4, .align_offset = 0);

      nir_ssa_def *node_offset =
         nir_iadd(&b, node_dst_offset, nir_umul24(&b, global_id, nir_imm_int(&b, 128)));
      node_dst_addr = nir_iadd(&b, node_dst_addr, nir_u2u64(&b, node_offset));
      nir_ssa_def *node_id =
         nir_iadd(&b, nir_ushr(&b, node_offset, nir_imm_int(&b, 3)), nir_imm_int(&b, 6));
      nir_build_store_global(&b, node_id, scratch_addr, .write_mask = 1, .align_mul = 4,
                             .align_offset = 0);

      nir_variable *bounds[2] = {
         nir_variable_create(b.shader, nir_var_shader_temp, vec3_type, "min_bound"),
         nir_variable_create(b.shader, nir_var_shader_temp, vec3_type, "max_bound"),
      };

      nir_store_var(&b, bounds[0], nir_channels(&b, nir_imm_vec4(&b, NAN, NAN, NAN, NAN), 7), 7);
      nir_store_var(&b, bounds[1], nir_channels(&b, nir_imm_vec4(&b, NAN, NAN, NAN, NAN), 7), 7);

      nir_ssa_def *header_addr = nir_pack_64_2x32(&b, nir_channels(&b, inst3, 12));
      nir_push_if(&b, nir_ine(&b, header_addr, nir_imm_int64(&b, 0)));
      nir_ssa_def *header_root_offset =
         nir_build_load_global(&b, 1, 32, nir_iadd(&b, header_addr, nir_imm_int64(&b, 0)),
                               .align_mul = 4, .align_offset = 0);
      nir_ssa_def *header_min =
         nir_build_load_global(&b, 3, 32, nir_iadd(&b, header_addr, nir_imm_int64(&b, 8)),
                               .align_mul = 4, .align_offset = 0);
      nir_ssa_def *header_max =
         nir_build_load_global(&b, 3, 32, nir_iadd(&b, header_addr, nir_imm_int64(&b, 20)),
                               .align_mul = 4, .align_offset = 0);

      nir_ssa_def *bound_defs[2][3];
      for (unsigned i = 0; i < 3; ++i) {
         bound_defs[0][i] = bound_defs[1][i] = nir_channel(&b, inst_transform[i], 3);

         nir_ssa_def *mul_a = nir_fmul(&b, nir_channels(&b, inst_transform[i], 7), header_min);
         nir_ssa_def *mul_b = nir_fmul(&b, nir_channels(&b, inst_transform[i], 7), header_max);
         nir_ssa_def *mi = nir_fmin(&b, mul_a, mul_b);
         nir_ssa_def *ma = nir_fmax(&b, mul_a, mul_b);
         for (unsigned j = 0; j < 3; ++j) {
            bound_defs[0][i] = nir_fadd(&b, bound_defs[0][i], nir_channel(&b, mi, j));
            bound_defs[1][i] = nir_fadd(&b, bound_defs[1][i], nir_channel(&b, ma, j));
         }
      }

      nir_store_var(&b, bounds[0], nir_vec(&b, bound_defs[0], 3), 7);
      nir_store_var(&b, bounds[1], nir_vec(&b, bound_defs[1], 3), 7);

      nir_ssa_def *m_in[3][3], *m_out[3][3], *m_vec[3][4];
      for (unsigned i = 0; i < 3; ++i)
         for (unsigned j = 0; j < 3; ++j)
            m_in[i][j] = nir_channel(&b, inst_transform[i], j);
      nir_invert_3x3(&b, m_in, m_out);
      for (unsigned i = 0; i < 3; ++i) {
         for (unsigned j = 0; j < 3; ++j)
            m_vec[i][j] = m_out[i][j];
         m_vec[i][3] = nir_channel(&b, inst_transform[i], 3);
      }

      for (unsigned i = 0; i < 3; ++i) {
         nir_build_store_global(&b, nir_vec(&b, m_vec[i], 4),
                                nir_iadd(&b, node_dst_addr, nir_imm_int64(&b, 16 + 16 * i)),
                                .write_mask = 0xf, .align_mul = 4, .align_offset = 0);
      }

      nir_ssa_def *out0[4] = {
         nir_ior(&b, nir_channel(&b, nir_unpack_64_2x32(&b, header_addr), 0), header_root_offset),
         nir_channel(&b, nir_unpack_64_2x32(&b, header_addr), 1), nir_channel(&b, inst3, 0),
         nir_channel(&b, inst3, 1)};
      nir_build_store_global(&b, nir_vec(&b, out0, 4),
                             nir_iadd(&b, node_dst_addr, nir_imm_int64(&b, 0)), .write_mask = 0xf,
                             .align_mul = 4, .align_offset = 0);
      nir_build_store_global(&b, global_id, nir_iadd(&b, node_dst_addr, nir_imm_int64(&b, 88)),
                             .write_mask = 0x1, .align_mul = 4, .align_offset = 0);
      nir_pop_if(&b, NULL);
      nir_build_store_global(&b, nir_load_var(&b, bounds[0]),
                             nir_iadd(&b, node_dst_addr, nir_imm_int64(&b, 64)), .write_mask = 0x7,
                             .align_mul = 4, .align_offset = 0);
      nir_build_store_global(&b, nir_load_var(&b, bounds[1]),
                             nir_iadd(&b, node_dst_addr, nir_imm_int64(&b, 76)), .write_mask = 0x7,
                             .align_mul = 4, .align_offset = 0);
   }
   nir_pop_if(&b, NULL);
   nir_pop_if(&b, NULL);

   return b.shader;
}

static void
determine_bounds(nir_builder *b, nir_ssa_def *node_addr, nir_ssa_def *node_id,
                 nir_variable *bounds_vars[2])
{
   nir_ssa_def *node_type = nir_iand(b, node_id, nir_imm_int(b, 7));
   node_addr = nir_iadd(
      b, node_addr,
      nir_u2u64(b, nir_ishl(b, nir_iand(b, node_id, nir_imm_int(b, ~7u)), nir_imm_int(b, 3))));

   nir_push_if(b, nir_ieq(b, node_type, nir_imm_int(b, 0)));
   {
      nir_ssa_def *positions[3];
      for (unsigned i = 0; i < 3; ++i)
         positions[i] =
            nir_build_load_global(b, 3, 32, nir_iadd(b, node_addr, nir_imm_int64(b, i * 12)),
                                  .align_mul = 4, .align_offset = 0);
      nir_ssa_def *bounds[] = {positions[0], positions[0]};
      for (unsigned i = 1; i < 3; ++i) {
         bounds[0] = nir_fmin(b, bounds[0], positions[i]);
         bounds[1] = nir_fmax(b, bounds[1], positions[i]);
      }
      nir_store_var(b, bounds_vars[0], bounds[0], 7);
      nir_store_var(b, bounds_vars[1], bounds[1], 7);
   }
   nir_push_else(b, NULL);
   nir_push_if(b, nir_ieq(b, node_type, nir_imm_int(b, 5)));
   {
      nir_ssa_def *input_bounds[4][2];
      for (unsigned i = 0; i < 4; ++i)
         for (unsigned j = 0; j < 2; ++j)
            input_bounds[i][j] = nir_build_load_global(
               b, 3, 32, nir_iadd(b, node_addr, nir_imm_int64(b, 16 + i * 24 + j * 12)),
               .align_mul = 4, .align_offset = 0);
      nir_ssa_def *bounds[] = {input_bounds[0][0], input_bounds[0][1]};
      for (unsigned i = 1; i < 4; ++i) {
         bounds[0] = nir_fmin(b, bounds[0], input_bounds[i][0]);
         bounds[1] = nir_fmax(b, bounds[1], input_bounds[i][1]);
      }

      nir_store_var(b, bounds_vars[0], bounds[0], 7);
      nir_store_var(b, bounds_vars[1], bounds[1], 7);
   }
   nir_push_else(b, NULL);
   nir_push_if(b, nir_ieq(b, node_type, nir_imm_int(b, 6)));
   { /* Instances */
      nir_ssa_def *bounds[2];
      for (unsigned i = 0; i < 2; ++i)
         bounds[i] =
            nir_build_load_global(b, 3, 32, nir_iadd(b, node_addr, nir_imm_int64(b, 64 + i * 12)),
                                  .align_mul = 4, .align_offset = 0);
      nir_store_var(b, bounds_vars[0], bounds[0], 7);
      nir_store_var(b, bounds_vars[1], bounds[1], 7);
   }
   nir_push_else(b, NULL);
   { /* AABBs */
      nir_ssa_def *bounds[2];
      for (unsigned i = 0; i < 2; ++i)
         bounds[i] =
            nir_build_load_global(b, 3, 32, nir_iadd(b, node_addr, nir_imm_int64(b, i * 12)),
                                  .align_mul = 4, .align_offset = 0);
      nir_store_var(b, bounds_vars[0], bounds[0], 7);
      nir_store_var(b, bounds_vars[1], bounds[1], 7);
   }
   nir_pop_if(b, NULL);
   nir_pop_if(b, NULL);
   nir_pop_if(b, NULL);
}

static nir_shader *
build_internal_shader(struct radv_device *dev)
{
   const struct glsl_type *vec3_type = glsl_vector_type(GLSL_TYPE_FLOAT, 3);
   nir_builder b =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL, "accel_build_internal_shader");

   b.shader->info.workgroup_size[0] = 64;
   b.shader->info.workgroup_size[1] = 1;
   b.shader->info.workgroup_size[2] = 1;

   /*
    * push constants:
    *   i32 x 2: node dst address
    *   i32 x 2: scratch address
    *   i32: dst offset
    *   i32: dst scratch offset
    *   i32: src scratch offset
    *   i32: src_node_count | (fill_header << 31)
    */
   nir_ssa_def *pconst0 =
      nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .base = 0, .range = 16);
   nir_ssa_def *pconst1 =
      nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .base = 16, .range = 16);

   nir_ssa_def *node_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst0, 3));
   nir_ssa_def *scratch_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst0, 12));
   nir_ssa_def *node_dst_offset = nir_channel(&b, pconst1, 0);
   nir_ssa_def *dst_scratch_offset = nir_channel(&b, pconst1, 1);
   nir_ssa_def *src_scratch_offset = nir_channel(&b, pconst1, 2);
   nir_ssa_def *src_node_count =
      nir_iand(&b, nir_channel(&b, pconst1, 3), nir_imm_int(&b, 0x7FFFFFFFU));
   nir_ssa_def *fill_header =
      nir_ine(&b, nir_iand(&b, nir_channel(&b, pconst1, 3), nir_imm_int(&b, 0x80000000U)),
              nir_imm_int(&b, 0));

   nir_ssa_def *global_id =
      nir_iadd(&b,
               nir_umul24(&b, nir_channels(&b, nir_load_workgroup_id(&b, 32), 1),
                          nir_imm_int(&b, b.shader->info.workgroup_size[0])),
               nir_channels(&b, nir_load_local_invocation_id(&b), 1));
   nir_ssa_def *src_idx = nir_imul(&b, global_id, nir_imm_int(&b, 4));
   nir_ssa_def *src_count = nir_umin(&b, nir_imm_int(&b, 4), nir_isub(&b, src_node_count, src_idx));

   nir_ssa_def *node_offset =
      nir_iadd(&b, node_dst_offset, nir_ishl(&b, global_id, nir_imm_int(&b, 7)));
   nir_ssa_def *node_dst_addr = nir_iadd(&b, node_addr, nir_u2u64(&b, node_offset));
   nir_ssa_def *src_nodes = nir_build_load_global(
      &b, 4, 32,
      nir_iadd(&b, scratch_addr,
               nir_u2u64(&b, nir_iadd(&b, src_scratch_offset,
                                      nir_ishl(&b, global_id, nir_imm_int(&b, 4))))),
      .align_mul = 4, .align_offset = 0);

   nir_build_store_global(&b, src_nodes, nir_iadd(&b, node_dst_addr, nir_imm_int64(&b, 0)),
                          .write_mask = 0xf, .align_mul = 4, .align_offset = 0);

   nir_ssa_def *total_bounds[2] = {
      nir_channels(&b, nir_imm_vec4(&b, NAN, NAN, NAN, NAN), 7),
      nir_channels(&b, nir_imm_vec4(&b, NAN, NAN, NAN, NAN), 7),
   };

   for (unsigned i = 0; i < 4; ++i) {
      nir_variable *bounds[2] = {
         nir_variable_create(b.shader, nir_var_shader_temp, vec3_type, "min_bound"),
         nir_variable_create(b.shader, nir_var_shader_temp, vec3_type, "max_bound"),
      };
      nir_store_var(&b, bounds[0], nir_channels(&b, nir_imm_vec4(&b, NAN, NAN, NAN, NAN), 7), 7);
      nir_store_var(&b, bounds[1], nir_channels(&b, nir_imm_vec4(&b, NAN, NAN, NAN, NAN), 7), 7);

      nir_push_if(&b, nir_ilt(&b, nir_imm_int(&b, i), src_count));
      determine_bounds(&b, node_addr, nir_channel(&b, src_nodes, i), bounds);
      nir_pop_if(&b, NULL);
      nir_build_store_global(&b, nir_load_var(&b, bounds[0]),
                             nir_iadd(&b, node_dst_addr, nir_imm_int64(&b, 16 + 24 * i)),
                             .write_mask = 0x7, .align_mul = 4, .align_offset = 0);
      nir_build_store_global(&b, nir_load_var(&b, bounds[1]),
                             nir_iadd(&b, node_dst_addr, nir_imm_int64(&b, 28 + 24 * i)),
                             .write_mask = 0x7, .align_mul = 4, .align_offset = 0);
      total_bounds[0] = nir_fmin(&b, total_bounds[0], nir_load_var(&b, bounds[0]));
      total_bounds[1] = nir_fmax(&b, total_bounds[1], nir_load_var(&b, bounds[1]));
   }

   nir_ssa_def *node_id =
      nir_iadd(&b, nir_ushr(&b, node_offset, nir_imm_int(&b, 3)), nir_imm_int(&b, 5));
   nir_ssa_def *dst_scratch_addr = nir_iadd(
      &b, scratch_addr,
      nir_u2u64(&b, nir_iadd(&b, dst_scratch_offset, nir_ishl(&b, global_id, nir_imm_int(&b, 2)))));
   nir_build_store_global(&b, node_id, dst_scratch_addr, .write_mask = 1, .align_mul = 4,
                          .align_offset = 0);

   nir_push_if(&b, fill_header);
   nir_build_store_global(&b, node_id, node_addr, .write_mask = 1, .align_mul = 4,
                          .align_offset = 0);
   nir_build_store_global(&b, total_bounds[0], nir_iadd(&b, node_addr, nir_imm_int64(&b, 8)),
                          .write_mask = 7, .align_mul = 4, .align_offset = 0);
   nir_build_store_global(&b, total_bounds[1], nir_iadd(&b, node_addr, nir_imm_int64(&b, 20)),
                          .write_mask = 7, .align_mul = 4, .align_offset = 0);
   nir_pop_if(&b, NULL);
   return b.shader;
}

void
radv_device_finish_accel_struct_build_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;
   radv_DestroyPipeline(radv_device_to_handle(device), state->accel_struct_build.internal_pipeline,
                        &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device), state->accel_struct_build.leaf_pipeline,
                        &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device),
                              state->accel_struct_build.internal_p_layout, &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device),
                              state->accel_struct_build.leaf_p_layout, &state->alloc);
}

VkResult
radv_device_init_accel_struct_build_state(struct radv_device *device)
{
   VkResult result;
   nir_shader *leaf_cs = build_leaf_shader(device);
   nir_shader *internal_cs = build_internal_shader(device);

   const VkPipelineLayoutCreateInfo leaf_pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                                    sizeof(struct build_primitive_constants)},
   };

   result = radv_CreatePipelineLayout(radv_device_to_handle(device), &leaf_pl_create_info,
                                      &device->meta_state.alloc,
                                      &device->meta_state.accel_struct_build.leaf_p_layout);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineShaderStageCreateInfo leaf_shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(leaf_cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo leaf_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = leaf_shader_stage,
      .flags = 0,
      .layout = device->meta_state.accel_struct_build.leaf_p_layout,
   };

   result = radv_CreateComputePipelines(
      radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
      &leaf_pipeline_info, NULL, &device->meta_state.accel_struct_build.leaf_pipeline);
   if (result != VK_SUCCESS)
      goto fail;

   const VkPipelineLayoutCreateInfo internal_pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                                    sizeof(struct build_internal_constants)},
   };

   result = radv_CreatePipelineLayout(radv_device_to_handle(device), &internal_pl_create_info,
                                      &device->meta_state.alloc,
                                      &device->meta_state.accel_struct_build.internal_p_layout);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineShaderStageCreateInfo internal_shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(internal_cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo internal_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = internal_shader_stage,
      .flags = 0,
      .layout = device->meta_state.accel_struct_build.internal_p_layout,
   };

   result = radv_CreateComputePipelines(
      radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
      &internal_pipeline_info, NULL, &device->meta_state.accel_struct_build.internal_pipeline);
   if (result != VK_SUCCESS)
      goto fail;

   return VK_SUCCESS;

fail:
   radv_device_finish_accel_struct_build_state(device);
   ralloc_free(internal_cs);
   ralloc_free(leaf_cs);
   return result;
}

struct bvh_state {
   uint32_t node_offset;
   uint32_t node_count;
   uint32_t scratch_offset;
};

void
radv_CmdBuildAccelerationStructuresKHR(
   VkCommandBuffer commandBuffer, uint32_t infoCount,
   const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
   const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_meta_saved_state saved_state;

   radv_meta_save(
      &saved_state, cmd_buffer,
      RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_CONSTANTS);
   struct bvh_state *bvh_states = calloc(infoCount, sizeof(struct bvh_state));

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        cmd_buffer->device->meta_state.accel_struct_build.leaf_pipeline);

   for (uint32_t i = 0; i < infoCount; ++i) {
      RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct,
                       pInfos[i].dstAccelerationStructure);

      struct build_primitive_constants prim_consts = {
         .node_dst_addr = radv_accel_struct_get_va(accel_struct),
         .scratch_addr = pInfos[i].scratchData.deviceAddress,
         .dst_offset = ALIGN(sizeof(struct radv_accel_struct_header), 64) + 128,
         .dst_scratch_offset = 0,
      };

      for (unsigned j = 0; j < pInfos[i].geometryCount; ++j) {
         const VkAccelerationStructureGeometryKHR *geom =
            pInfos[i].pGeometries ? &pInfos[i].pGeometries[j] : pInfos[i].ppGeometries[j];

         prim_consts.geometry_type = geom->geometryType;
         prim_consts.geometry_id = j | (geom->flags << 28);
         unsigned prim_size;
         switch (geom->geometryType) {
         case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
            prim_consts.vertex_addr =
               geom->geometry.triangles.vertexData.deviceAddress +
               ppBuildRangeInfos[i][j].firstVertex * geom->geometry.triangles.vertexStride +
               (geom->geometry.triangles.indexType != VK_INDEX_TYPE_NONE_KHR
                   ? ppBuildRangeInfos[i][j].primitiveOffset
                   : 0);
            prim_consts.index_addr = geom->geometry.triangles.indexData.deviceAddress +
                                     ppBuildRangeInfos[i][j].primitiveOffset;
            prim_consts.transform_addr = geom->geometry.triangles.transformData.deviceAddress +
                                         ppBuildRangeInfos[i][j].transformOffset;
            prim_consts.vertex_stride = geom->geometry.triangles.vertexStride;
            prim_consts.vertex_format = geom->geometry.triangles.vertexFormat;
            prim_consts.index_format = geom->geometry.triangles.indexType;
            prim_size = 64;
            break;
         case VK_GEOMETRY_TYPE_AABBS_KHR:
            prim_consts.aabb_addr =
               geom->geometry.aabbs.data.deviceAddress + ppBuildRangeInfos[i][j].primitiveOffset;
            prim_consts.aabb_stride = geom->geometry.aabbs.stride;
            prim_size = 64;
            break;
         case VK_GEOMETRY_TYPE_INSTANCES_KHR:
            prim_consts.instance_data = geom->geometry.instances.data.deviceAddress;
            prim_size = 128;
            break;
         default:
            unreachable("Unknown geometryType");
         }

         radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                               cmd_buffer->device->meta_state.accel_struct_build.leaf_p_layout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(prim_consts), &prim_consts);
         radv_unaligned_dispatch(cmd_buffer, ppBuildRangeInfos[i][j].primitiveCount, 1, 1);
         prim_consts.dst_offset += prim_size * ppBuildRangeInfos[i][j].primitiveCount;
         prim_consts.dst_scratch_offset += 4 * ppBuildRangeInfos[i][j].primitiveCount;
      }
      bvh_states[i].node_offset = prim_consts.dst_offset;
      bvh_states[i].node_count = prim_consts.dst_scratch_offset / 4;
   }

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        cmd_buffer->device->meta_state.accel_struct_build.internal_pipeline);
   bool progress = true;
   for (unsigned iter = 0; progress; ++iter) {
      progress = false;
      for (uint32_t i = 0; i < infoCount; ++i) {
         RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct,
                          pInfos[i].dstAccelerationStructure);

         if (iter && bvh_states[i].node_count == 1)
            continue;

         if (!progress) {
            cmd_buffer->state.flush_bits |=
               RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
               radv_src_access_flush(cmd_buffer, VK_ACCESS_SHADER_WRITE_BIT, NULL) |
               radv_dst_access_flush(cmd_buffer,
                                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, NULL);
         }
         progress = true;
         uint32_t dst_node_count = MAX2(1, DIV_ROUND_UP(bvh_states[i].node_count, 4));
         bool final_iter = dst_node_count == 1;
         uint32_t src_scratch_offset = bvh_states[i].scratch_offset;
         uint32_t dst_scratch_offset = src_scratch_offset ? 0 : bvh_states[i].node_count * 4;
         uint32_t dst_node_offset = bvh_states[i].node_offset;
         if (final_iter)
            dst_node_offset = ALIGN(sizeof(struct radv_accel_struct_header), 64);

         const struct build_internal_constants consts = {
            .node_dst_addr = radv_accel_struct_get_va(accel_struct),
            .scratch_addr = pInfos[i].scratchData.deviceAddress,
            .dst_offset = dst_node_offset,
            .dst_scratch_offset = dst_scratch_offset,
            .src_scratch_offset = src_scratch_offset,
            .fill_header = bvh_states[i].node_count | (final_iter ? 0x80000000U : 0),
         };

         radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                               cmd_buffer->device->meta_state.accel_struct_build.internal_p_layout,
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);
         radv_unaligned_dispatch(cmd_buffer, dst_node_count, 1, 1);
         bvh_states[i].node_offset += dst_node_count * 128;
         bvh_states[i].node_count = dst_node_count;
         bvh_states[i].scratch_offset = dst_scratch_offset;
      }
   }
   free(bvh_states);
   radv_meta_restore(&saved_state, cmd_buffer);
}