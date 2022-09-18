/*
 * Copyright © 2022 Friedrich Vock
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

#include "amd_family.h"
#include "radv_acceleration_structure.h"
#include "vk_common_entrypoints.h"

#define RRA_MAGIC 0x204644525F444D41

struct rra_file_header {
   uint64_t magic;
   uint32_t version;
   uint32_t unused;
   uint64_t chunk_descriptions_offset;
   uint64_t chunk_descriptions_size;
};

static_assert(sizeof(struct rra_file_header) == 32, "rra_file_header does not match RRA spec");

enum rra_chunk_type {
   RADV_RRA_CHUNK_ID_ASIC_API_INFO = 0x1,
   RADV_RRA_CHUNK_ID_ACCEL_STRUCT = 0xF0005,
};

enum rra_file_api {
   RADV_RRA_API_DX9,
   RADV_RRA_API_DX11,
   RADV_RRA_API_DX12,
   RADV_RRA_API_VULKAN,
   RADV_RRA_API_OPENGL,
   RADV_RRA_API_OPENCL,
   RADV_RRA_API_MANTLE,
   RADV_RRA_API_GENERIC,
};

struct rra_file_chunk_description {
   char name[16];
   bool is_zstd_compressed;
   enum rra_chunk_type type;
   uint64_t header_offset;
   uint64_t header_size;
   uint64_t data_offset;
   uint64_t data_size;
   uint64_t unused;
};

static_assert(sizeof(struct rra_file_chunk_description) == 64,
              "rra_file_chunk_description does not match RRA spec");

static void
rra_dump_header(FILE *output, uint64_t chunk_descriptions_offset, uint64_t chunk_descriptions_size)
{
   struct rra_file_header header = {
      .magic = RRA_MAGIC,
      .version = 3,
      .chunk_descriptions_offset = chunk_descriptions_offset,
      .chunk_descriptions_size = chunk_descriptions_size,
   };
   fwrite(&header, sizeof(header), 1, output);
}

static void
rra_dump_chunk_description(uint64_t offset, uint64_t header_size, uint64_t data_size,
                           const char *name, enum rra_chunk_type type, FILE *output)
{
   struct rra_file_chunk_description chunk = {
      .type = type,
      .header_offset = offset,
      .header_size = header_size,
      .data_offset = offset + header_size,
      .data_size = data_size,
   };
   strncpy(chunk.name, name, sizeof(chunk.name) - 1);
   fwrite(&chunk, sizeof(struct rra_file_chunk_description), 1, output);
}

#define RRA_FILE_DEVICE_NAME_MAX_SIZE 256

struct rra_asic_info {
   uint64_t min_shader_clk_freq;
   uint64_t min_mem_clk_freq;
   char unused[8];
   uint64_t max_shader_clk_freq;
   uint64_t max_mem_clk_freq;
   uint32_t device_id;
   uint32_t rev_id;
   char unused2[80];
   uint64_t vram_size;
   uint32_t bus_width;
   char unused3[12];
   char device_name[RRA_FILE_DEVICE_NAME_MAX_SIZE];
   char unused4[16];
   uint32_t mem_ops_per_clk;
   uint32_t mem_type;
   char unused5[135];
   bool valid;
};

static_assert(sizeof(struct rra_asic_info) == 568, "rra_asic_info does not match RRA spec");

static uint32_t
amdgpu_vram_type_to_rra(uint32_t type)
{
   switch (type) {
   case AMD_VRAM_TYPE_UNKNOWN:
      return 0;
   case AMD_VRAM_TYPE_DDR2:
      return 2;
   case AMD_VRAM_TYPE_DDR3:
      return 3;
   case AMD_VRAM_TYPE_DDR4:
      return 4;
   case AMD_VRAM_TYPE_DDR5:
      return 5;
   case AMD_VRAM_TYPE_HBM:
      return 10;
   case AMD_VRAM_TYPE_GDDR3:
      return 6;
   case AMD_VRAM_TYPE_GDDR4:
      return 7;
   case AMD_VRAM_TYPE_GDDR5:
      return 8;
   case AMD_VRAM_TYPE_GDDR6:
      return 9;
   default:
      unreachable("invalid vram type");
   }
}

static void
rra_dump_asic_info(struct radeon_info *rad_info, FILE *output)
{
   struct rra_asic_info asic_info = {
      /* All frequencies are in Hz */
      .min_shader_clk_freq = 0,
      .max_shader_clk_freq = rad_info->max_gpu_freq_mhz * 1000000,
      .min_mem_clk_freq = 0,
      .max_mem_clk_freq = rad_info->memory_freq_mhz * 1000000,

      .vram_size = (uint64_t)rad_info->vram_size_kb * 1024,

      .mem_type = amdgpu_vram_type_to_rra(rad_info->vram_type),
      .mem_ops_per_clk = ac_memory_ops_per_clock(rad_info->vram_type),
      .bus_width = rad_info->memory_bus_width,

      .device_id = rad_info->pci_dev,
      .rev_id = rad_info->pci_rev_id,
   };

   strncpy(asic_info.device_name,
           rad_info->marketing_name ? rad_info->marketing_name : rad_info->name,
           RRA_FILE_DEVICE_NAME_MAX_SIZE - 1);

   fwrite(&asic_info, sizeof(struct rra_asic_info), 1, output);
}

enum rra_bvh_type {
   RRA_BVH_TYPE_TLAS,
   RRA_BVH_TYPE_BLAS,
};

struct rra_accel_struct_chunk_header {
   /*
    * Declaring this as uint64_t would make the compiler insert padding to
    * satisfy alignment restrictions.
    */
   uint32_t virtual_address[2];
   uint32_t metadata_offset;
   uint32_t metadata_size;
   uint32_t header_offset;
   uint32_t header_size;
   enum rra_bvh_type bvh_type;
};

static_assert(sizeof(struct rra_accel_struct_chunk_header) == 28,
              "rra_accel_struct_chunk_header does not match RRA spec");

struct rra_accel_struct_post_build_info {
   uint32_t bvh_type : 1;
   uint32_t : 5;
   uint32_t tri_compression_mode : 2;
   uint32_t fp16_interior_mode : 2;
   uint32_t : 6;
   uint32_t build_flags : 16;
};

static_assert(sizeof(struct rra_accel_struct_post_build_info) == 4,
              "rra_accel_struct_post_build_info does not match RRA spec");

struct rra_accel_struct_header {
   struct rra_accel_struct_post_build_info post_build_info;
   /*
    * Size of the internal acceleration structure metadata in the
    * proprietary drivers. Seems to always be 128.
    */
   uint32_t metadata_size;
   uint32_t file_size;
   uint32_t primitive_count;
   uint32_t active_primitive_count;
   uint32_t unused1;
   uint32_t geometry_description_count;
   VkGeometryTypeKHR geometry_type;
   uint32_t internal_node_data_start;
   uint32_t internal_node_data_end;
   uint32_t leaf_node_data_start;
   uint32_t leaf_node_data_end;
   uint32_t interior_fp32_node_count;
   uint32_t interior_fp16_node_count;
   uint32_t leaf_node_count;
   uint32_t rt_driver_interface_version;
   uint64_t unused2;
   uint32_t half_fp32_node_count;
   char unused3[44];
};

#define RRA_ROOT_NODE_OFFSET align(sizeof(struct rra_accel_struct_header), 64)

static_assert(sizeof(struct rra_accel_struct_header) == 120,
              "rra_accel_struct_header does not match RRA spec");

struct rra_accel_struct_metadata {
   uint64_t virtual_address;
   uint32_t byte_size;
   char unused[116];
};

static_assert(sizeof(struct rra_accel_struct_metadata) == 128,
              "rra_accel_struct_metadata does not match RRA spec");

struct rra_geometry_info {
   uint32_t primitive_count : 29;
   uint32_t flags : 3;
   uint32_t unknown;
   uint32_t leaf_node_list_offset;
};

static_assert(sizeof(struct rra_geometry_info) == 12, "rra_geometry_info does not match RRA spec");

static struct rra_accel_struct_header
rra_fill_accel_struct_header_common(struct radv_accel_struct_header *header,
                                    size_t leaf_node_data_size, size_t internal_node_data_size,
                                    uint64_t primitive_count)
{
   return (struct rra_accel_struct_header){
      .post_build_info =
         {
            .build_flags = header->build_flags,
            /* Seems to be no compression */
            .tri_compression_mode = 0,
         },
      .metadata_size = sizeof(struct rra_accel_struct_metadata),
      .file_size = sizeof(struct rra_accel_struct_metadata) +
                   sizeof(struct rra_accel_struct_header) + internal_node_data_size +
                   leaf_node_data_size,
      .primitive_count = primitive_count,
      /* TODO: calculate active primitives */
      .active_primitive_count = primitive_count,
      .geometry_description_count = header->geometry_count,
      .internal_node_data_start = sizeof(struct rra_accel_struct_metadata),
      .internal_node_data_end = sizeof(struct rra_accel_struct_metadata) + internal_node_data_size,
      .leaf_node_data_start = sizeof(struct rra_accel_struct_metadata) + internal_node_data_size,
      .leaf_node_data_end =
         sizeof(struct rra_accel_struct_metadata) + internal_node_data_size + leaf_node_data_size,
      .interior_fp32_node_count = internal_node_data_size / sizeof(struct radv_bvh_box32_node),
      .leaf_node_count = primitive_count,
   };
}

struct rra_box32_node {
   uint32_t children[4];
   float coords[4][2][3];
   uint32_t reserved[4];
};

/*
 * RRA files contain this struct in place of hardware
 * instance nodes. They're named "instance desc" internally.
 */
struct rra_instance_node {
   float wto_matrix[12];
   uint32_t custom_instance_id : 24;
   uint32_t mask : 8;
   uint32_t sbt_offset : 24;
   uint32_t instance_flags : 8;
   uint64_t blas_va : 54;
   uint64_t hw_instance_flags : 10;
   uint32_t instance_id;
   uint32_t unused1;
   uint32_t blas_metadata_size;
   uint32_t unused2;
   float otw_matrix[12];
};

static_assert(sizeof(struct rra_instance_node) == 128,
              "rra_instance_node does not match RRA spec!");

/*
 * Format RRA uses for aabb nodes
 */
struct rra_aabb_node {
   float aabb[2][3];
   uint32_t unused1[6];
   uint32_t geometry_id : 28;
   uint32_t flags : 4;
   uint32_t primitive_id;
   uint32_t unused[2];
};

static_assert(sizeof(struct rra_aabb_node) == 64, "rra_aabb_node does not match RRA spec!");

struct rra_triangle_node {
   float coords[3][3];
   uint32_t reserved[3];
   uint32_t geometry_id : 28;
   uint32_t flags : 4;
   uint32_t triangle_id;
   uint32_t reserved2;
   uint32_t id;
};

static_assert(sizeof(struct rra_triangle_node) == 64, "rra_triangle_node does not match RRA spec!");

static void
rra_dump_tlas_header(struct radv_accel_struct_header *header, size_t leaf_node_data_size,
                     size_t internal_node_data_size, uint64_t primitive_count, FILE *output)
{
   struct rra_accel_struct_header file_header = rra_fill_accel_struct_header_common(
      header, leaf_node_data_size, internal_node_data_size, primitive_count);
   file_header.post_build_info.bvh_type = RRA_BVH_TYPE_TLAS;
   file_header.geometry_type = VK_GEOMETRY_TYPE_INSTANCES_KHR;

   fwrite(&file_header, sizeof(struct rra_accel_struct_header), 1, output);
}

static void
rra_dump_blas_header(struct radv_accel_struct_header *header,
                     struct radv_accel_struct_geometry_info *geometry_infos,
                     size_t leaf_node_data_size, size_t internal_node_data_size,
                     uint64_t primitive_count, FILE *output)
{
   struct rra_accel_struct_header file_header = rra_fill_accel_struct_header_common(
      header, leaf_node_data_size, internal_node_data_size, primitive_count);
   file_header.post_build_info.bvh_type = RRA_BVH_TYPE_BLAS;
   file_header.geometry_type =
      header->geometry_count ? geometry_infos->type : VK_GEOMETRY_TYPE_TRIANGLES_KHR;
   /*
    * In BLASes in RRA, this seems to correspond to the start offset of the
    * geometry info instead of leaf node data.
    */
   file_header.leaf_node_data_start += leaf_node_data_size;

   fwrite(&file_header, sizeof(struct rra_accel_struct_header), 1, output);
}

static void
rra_dump_blas_geometry_infos(struct radv_accel_struct_geometry_info *geometry_infos, 
                             uint32_t geometry_count, FILE *output)
{
   uint32_t accumulated_primitive_count = 0;
   for (uint32_t i = 0; i < geometry_count; ++i) {
      accumulated_primitive_count += geometry_infos[i].primitive_count;
      struct rra_geometry_info geometry_info = {
         .primitive_count = geometry_infos[i].primitive_count,
         .flags = geometry_infos[i].flags,
         .leaf_node_list_offset = accumulated_primitive_count * sizeof(uint32_t),
      };
      fwrite(&geometry_info, sizeof(struct rra_geometry_info), 1, output);
   }
}

static uint32_t
rra_parent_table_index_from_offset(uint32_t offset, uint32_t parent_table_size)
{
   uint32_t max_parent_table_index = parent_table_size / sizeof(uint32_t) - 1;
   return max_parent_table_index - (offset - RRA_ROOT_NODE_OFFSET) / 64;
}

static void PRINTFLIKE(2, 3)
rra_accel_struct_validation_fail(uint32_t offset, const char *reason, ...)
{
   fprintf(stderr, "radv: AS validation failed at node with offset 0x%x with reason: ", offset);

   va_list list;
   va_start(list, reason);
   vfprintf(stderr, reason, list);
   va_end(list);

   fprintf(stderr, "\n");
}

static bool
rra_validate_node(struct hash_table_u64 *accel_struct_vas, uint8_t *data,
                  struct radv_bvh_box32_node *node, uint32_t root_node_offset,
                  uint32_t leaf_nodes_size, uint32_t internal_nodes_size,
                  uint32_t parent_table_size, bool is_bottom_level)
{
   bool result = true;
   uint32_t cur_offset = (uint8_t *)node - data;
   for (uint32_t i = 0; i < 4; ++i) {
      if (isnan(node->coords[i][0][0]))
         continue;

      uint32_t type = node->children[i] & 7;
      uint32_t offset = (node->children[i] & (~7u)) << 3;

      bool is_node_type_valid = true;
      bool node_type_matches_as_type = true;

      switch (type) {
      case radv_bvh_node_internal:
         break;
      case radv_bvh_node_instance:
         node_type_matches_as_type = !is_bottom_level;
         break;
      case radv_bvh_node_triangle:
      case radv_bvh_node_aabb:
         node_type_matches_as_type = is_bottom_level;
         break;
      default:
         is_node_type_valid = false;
      }
      if (!is_node_type_valid) {
         rra_accel_struct_validation_fail(cur_offset, "Invalid node type %u (child index %u)", type,
                                          i);
         result = false;
      }
      if (!node_type_matches_as_type) {
         rra_accel_struct_validation_fail(offset,
                                          is_bottom_level ? "BLAS node in TLAS (child index %u)"
                                                          : "TLAS node in BLAS (child index %u)",
                                          i);

         result = false;
      }

      if (offset > root_node_offset + leaf_nodes_size + internal_nodes_size) {
         rra_accel_struct_validation_fail(cur_offset, "Invalid child offset (child index %u)", i);
         result = false;
         continue;
      }

      if (type == radv_bvh_node_internal) {
         result &= rra_validate_node(
            accel_struct_vas, data, (struct radv_bvh_box32_node *)(data + offset), root_node_offset,
            leaf_nodes_size, internal_nodes_size, parent_table_size, is_bottom_level);
      } else if (type == radv_bvh_node_instance) {
         struct radv_bvh_instance_node *src = (struct radv_bvh_instance_node *)(data + offset);
         uint64_t blas_va = src->base_ptr & (~63UL);
         if (!_mesa_hash_table_u64_search(accel_struct_vas, blas_va)) {
            rra_accel_struct_validation_fail(offset, "Invalid instance node pointer 0x%llx",
                                             (unsigned long long)blas_va);
            result = false;
         }
      }
   }
   return result;
}

static void
rra_transcode_triangle_node(struct rra_triangle_node *dst, const struct radv_bvh_triangle_node *src)
{
   for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j)
         dst->coords[i][j] = src->coords[i][j];
   dst->triangle_id = src->triangle_id;
   dst->geometry_id = src->geometry_id_and_flags & 0xfffffff;
   dst->flags = src->geometry_id_and_flags >> 28;
   dst->id = src->id;
}

static void
rra_transcode_aabb_node(struct rra_aabb_node *dst, const struct radv_bvh_aabb_node *src)
{
   for (int i = 0; i < 2; ++i)
      for (int j = 0; j < 3; ++j)
         dst->aabb[i][j] = src->aabb[i][j];

   dst->geometry_id = src->geometry_id_and_flags & 0xfffffff;
   dst->flags = src->geometry_id_and_flags >> 28;
   dst->primitive_id = src->primitive_id;
}

static void
rra_transcode_instance_node(struct rra_instance_node *dst, const struct radv_bvh_instance_node *src)
{
   /* Mask out root node offset from AS pointer to get the raw VA */
   uint64_t blas_va = src->base_ptr & (~63UL);

   dst->custom_instance_id = src->custom_instance_and_mask & 0xffffff;
   dst->mask = src->custom_instance_and_mask >> 24;
   dst->sbt_offset = src->sbt_offset_and_flags & 0xffffff;
   dst->instance_flags = src->sbt_offset_and_flags >> 24;
   dst->blas_va = (blas_va + sizeof(struct rra_accel_struct_metadata)) >> 3;
   dst->instance_id = src->instance_id;
   dst->blas_metadata_size = sizeof(struct rra_accel_struct_metadata);

   float full_otw_matrix[16];
   for (int row = 0; row < 3; ++row)
      for (int col = 0; col < 3; ++col)
         full_otw_matrix[row * 4 + col] = src->otw_matrix[row * 3 + col];

   full_otw_matrix[3] = src->wto_matrix[3];
   full_otw_matrix[7] = src->wto_matrix[7];
   full_otw_matrix[11] = src->wto_matrix[11];

   full_otw_matrix[12] = 0.0f;
   full_otw_matrix[13] = 0.0f;
   full_otw_matrix[14] = 0.0f;
   full_otw_matrix[15] = 1.0f;

   float full_wto_matrix[16];
   util_invert_mat4x4(full_wto_matrix, full_otw_matrix);

   memcpy(dst->wto_matrix, full_wto_matrix, sizeof(dst->wto_matrix));
   memcpy(dst->otw_matrix, full_otw_matrix, sizeof(dst->otw_matrix));
}

static void
rra_transcode_internal_node(struct rra_box32_node *dst, const struct radv_bvh_box32_node *src,
                            uint32_t dst_offset, uint32_t src_root_offset,
                            uint32_t src_leaf_node_size, uint32_t dst_leaf_node_size,
                            uint32_t src_internal_nodes_size, uint32_t internal_node_count,
                            uint32_t src_leaf_nodes_size, uint32_t *parent_id_table,
                            uint32_t parent_id_table_size)
{
   uint32_t src_leaf_nodes_offset = src_root_offset + sizeof(struct radv_bvh_box32_node);
   uint32_t src_internal_nodes_offset = src_leaf_nodes_offset + src_leaf_nodes_size;

   memcpy(dst->coords, src->coords, sizeof(dst->coords));

   for (uint32_t i = 0; i < 4; ++i) {
      if (isnan(src->coords[i][0][0])) {
         dst->children[i] = 0xffffffff;
         continue;
      }

      uint32_t child_type = src->children[i] & 7;
      uint32_t src_child_offset = (src->children[i] & (~7u)) << 3;

      uint32_t dst_child_offset = RRA_ROOT_NODE_OFFSET;
      if (child_type == radv_bvh_node_internal) {
         uint32_t dst_child_index =
            (src_child_offset - src_internal_nodes_offset) / src_leaf_node_size;
         dst_child_offset += sizeof(struct rra_box32_node) + dst_child_index * dst_leaf_node_size;
      } else {
         uint32_t dst_child_index = (src_child_offset - src_leaf_nodes_offset) / src_leaf_node_size;
         dst_child_offset += internal_node_count * sizeof(struct rra_box32_node) +
                             dst_child_index * dst_leaf_node_size;
      }

      dst->children[i] = child_type | (dst_child_offset >> 3);

      uint32_t parent_id_index = rra_parent_table_index_from_offset(
         dst_child_offset, parent_id_table_size);
      parent_id_table[parent_id_index] = radv_bvh_node_internal | (dst_offset >> 3);
   }
}

struct rra_copied_accel_struct {
   VkAccelerationStructureKHR handle;
   uint8_t *data;
};

static VkResult
rra_dump_acceleration_structure(struct rra_copied_accel_struct *copied_struct,
                                struct hash_table_u64 *accel_struct_vas, bool should_validate,
                                FILE *output)
{
   VkAccelerationStructureKHR structure = copied_struct->handle;
   uint8_t *data = copied_struct->data;

   RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct, structure);
   struct radv_accel_struct_header *header = (struct radv_accel_struct_header *)data;

   bool is_tlas = header->instance_count > 0;

   uint64_t geometry_infos_offset =
      header->compacted_size -
      header->geometry_count * sizeof(struct radv_accel_struct_geometry_info);

   uint64_t src_leaf_nodes_size = 0;
   uint64_t dst_leaf_nodes_size = 0;
   uint64_t primitive_count = 0;

   uint32_t src_internal_nodes_size =
      header->internal_node_count * sizeof(struct radv_bvh_box32_node);
   uint32_t dst_internal_nodes_size = header->internal_node_count * sizeof(struct rra_box32_node);

   uint64_t src_primitive_size = 0;
   uint64_t dst_primitive_size = 0;
   VkGeometryTypeKHR geometry_type = 0;

   struct radv_accel_struct_geometry_info *geometry_infos =
      (struct radv_accel_struct_geometry_info *)(data + geometry_infos_offset);

   for (uint32_t i = 0; i < header->geometry_count; ++i) {
      primitive_count += geometry_infos[i].primitive_count;
      switch (geometry_infos[i].type) {
      case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
         src_primitive_size = sizeof(struct radv_bvh_triangle_node);
         dst_primitive_size = sizeof(struct rra_triangle_node);
         break;
      case VK_GEOMETRY_TYPE_AABBS_KHR:
         src_primitive_size = sizeof(struct radv_bvh_aabb_node);
         dst_primitive_size = sizeof(struct rra_aabb_node);
         break;
      case VK_GEOMETRY_TYPE_INSTANCES_KHR:
         src_primitive_size = sizeof(struct radv_bvh_instance_node);
         dst_primitive_size = sizeof(struct rra_instance_node);
         break;
      default:
         unreachable("invalid geometry type");
      }
      geometry_type = geometry_infos[i].type;
      src_leaf_nodes_size += geometry_infos[i].primitive_count * src_primitive_size;
      dst_leaf_nodes_size += geometry_infos[i].primitive_count * dst_primitive_size;
   }

   uint32_t node_parent_table_size =
      ((dst_leaf_nodes_size + dst_internal_nodes_size) / 64) * sizeof(uint32_t);

   /* convert root node id to offset */
   uint32_t src_root_offset = (header->root_node_offset & ~7) << 3;

   if (should_validate)
      if (!rra_validate_node(accel_struct_vas, data,
                             (struct radv_bvh_box32_node *)(data + src_root_offset),
                             src_root_offset, src_leaf_nodes_size, src_internal_nodes_size,
                             node_parent_table_size, !is_tlas)) {
         return VK_ERROR_VALIDATION_FAILED_EXT;
      }

   uint32_t *node_parent_table = malloc(node_parent_table_size);
   if (!node_parent_table) {
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   node_parent_table[rra_parent_table_index_from_offset(RRA_ROOT_NODE_OFFSET, node_parent_table_size)] = 0xffffffff;
   
   uint32_t *leaf_node_ids = malloc(primitive_count * sizeof(uint32_t));
   if (!leaf_node_ids) {
      free(node_parent_table);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   uint8_t *dst_structure_data =
      malloc(RRA_ROOT_NODE_OFFSET + dst_internal_nodes_size + dst_leaf_nodes_size);
   if (!dst_structure_data) {
      free(node_parent_table);
      free(leaf_node_ids);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   uint8_t *current_dst_data = dst_structure_data + RRA_ROOT_NODE_OFFSET + dst_internal_nodes_size;
   uint8_t *current_src_data = data + src_root_offset + sizeof(struct radv_bvh_box32_node);
   /*
    * Transcode leaf nodes
    */
   uint32_t current_leaf_node_index = 0;
   for (uint32_t i = 0; i < primitive_count; ++i) {
      uint32_t node_type;
      switch (geometry_type) {
      case VK_GEOMETRY_TYPE_TRIANGLES_KHR: {
         node_type = radv_bvh_node_triangle;
         rra_transcode_triangle_node((struct rra_triangle_node *)current_dst_data,
                                     (const struct radv_bvh_triangle_node *)current_src_data);
         break;
      }
      case VK_GEOMETRY_TYPE_AABBS_KHR: {
         node_type = radv_bvh_node_aabb;
         rra_transcode_aabb_node((struct rra_aabb_node *)current_dst_data,
                                 (const struct radv_bvh_aabb_node *)current_src_data);
         break;
      }
      case VK_GEOMETRY_TYPE_INSTANCES_KHR:
         node_type = radv_bvh_node_instance;
         rra_transcode_instance_node((struct rra_instance_node *)current_dst_data,
                                     (const struct radv_bvh_instance_node *)current_src_data);
         break;
      default:
         unreachable("invalid geometry type");
      }
      current_dst_data += dst_primitive_size;
      current_src_data += src_primitive_size;

      leaf_node_ids[current_leaf_node_index++] =
         node_type | ((current_dst_data - dst_structure_data) >> 3);
   }

   current_dst_data = dst_structure_data + RRA_ROOT_NODE_OFFSET;
   current_src_data = data + src_root_offset;

   /*
    * Transcode internal nodes
    */
   for (uint32_t i = 0; i < header->internal_node_count; ++i) {
      rra_transcode_internal_node((struct rra_box32_node *)current_dst_data,
                                  (const struct radv_bvh_box32_node *)current_src_data,
                                  current_dst_data - dst_structure_data, src_root_offset,
                                  src_primitive_size, dst_primitive_size,
                                  src_internal_nodes_size - sizeof(struct radv_bvh_box32_node),
                                  header->internal_node_count, src_leaf_nodes_size,
                                  node_parent_table, node_parent_table_size);
      current_dst_data += sizeof(struct rra_box32_node);
      current_src_data += sizeof(struct radv_bvh_box32_node);

      if (i == 0)
         current_src_data += src_leaf_nodes_size;
   }

   struct rra_accel_struct_chunk_header chunk_header = {
      .metadata_offset = 0,
      /*
       * RRA loads the part of the metadata that is used into a struct.
       * If the size is larger than just the "used" part, the loading
       * operation overwrites internal pointers with data from the file,
       * likely causing a crash.
       */
      .metadata_size = offsetof(struct rra_accel_struct_metadata, unused),
      .header_offset = sizeof(struct rra_accel_struct_metadata) + node_parent_table_size,
      .header_size = sizeof(struct rra_accel_struct_header),
      .bvh_type = is_tlas ? RRA_BVH_TYPE_TLAS : RRA_BVH_TYPE_BLAS,
   };

   /*
    * When associating TLASes with BLASes, acceleration structure VAs are
    * looked up in a hashmap. But due to the way BLAS VAs are stored for
    * each instance in the RRA file format (divided by 8, and limited to 54 bits),
    * the top bits are masked away.
    * In order to make sure BLASes can be found in the hashmap, we have
    * to replicate that mask here.
    */
   uint64_t va = accel_struct->va & 0x1FFFFFFFFFFFFFF;
   memcpy(chunk_header.virtual_address, &va, sizeof(uint64_t));

   struct rra_accel_struct_metadata rra_metadata = {
      .virtual_address = va,
      .byte_size =
         dst_leaf_nodes_size + dst_internal_nodes_size + sizeof(struct rra_accel_struct_header),
   };

   fwrite(&chunk_header, sizeof(struct rra_accel_struct_chunk_header), 1, output);
   fwrite(&rra_metadata, sizeof(struct rra_accel_struct_metadata), 1, output);

   /*
    * See the meta shader source for the memory layout of the
    * acceleration structure.
    */

   /* Write node parent id data */
   fwrite(node_parent_table, 1, node_parent_table_size, output);

   if (is_tlas)
      rra_dump_tlas_header(header, dst_leaf_nodes_size, dst_internal_nodes_size, primitive_count,
                           output);
   else
      rra_dump_blas_header(header, geometry_infos, dst_leaf_nodes_size, dst_internal_nodes_size,
                           primitive_count, output);

   /* Write acceleration structure data  */
   fwrite(dst_structure_data + RRA_ROOT_NODE_OFFSET, 1, dst_internal_nodes_size + dst_leaf_nodes_size,
          output);

   if (!is_tlas)
      rra_dump_blas_geometry_infos(geometry_infos, header->geometry_count, output);

   /* Write leaf node ids */
   uint32_t leaf_node_list_size = primitive_count * sizeof(uint32_t);
   fwrite(leaf_node_ids, 1, leaf_node_list_size, output);

   free(dst_structure_data);
   free(node_parent_table);
   free(leaf_node_ids);

   return VK_SUCCESS;
}

int
radv_rra_trace_frame()
{
   return radv_get_int_debug_option("RADV_RRA_TRACE", -1);
}

char *
radv_rra_trace_trigger_file()
{
   return getenv("RADV_RRA_TRACE_TRIGGER");
}

bool
radv_rra_trace_enabled()
{
   return radv_rra_trace_frame() != -1 || radv_rra_trace_trigger_file();
}

void
radv_rra_trace_init(struct radv_device *device)
{
   device->rra_trace.trace_frame = radv_rra_trace_frame();
   device->rra_trace.elapsed_frames = 0;
   device->rra_trace.trigger_file = radv_rra_trace_trigger_file();
   device->rra_trace.validate_as = radv_get_int_debug_option("RADV_RRA_TRACE_VALIDATE", 0) != 0;
   device->rra_trace.accel_structs = _mesa_pointer_hash_table_create(NULL);
   device->rra_trace.accel_struct_vas = _mesa_hash_table_u64_create(NULL);
   simple_mtx_init(&device->rra_trace.data_mtx, mtx_plain);
}

void
radv_rra_trace_finish(VkDevice vk_device, struct radv_rra_trace_data *data)
{
   simple_mtx_destroy(&data->data_mtx);
   _mesa_hash_table_destroy(data->accel_structs, NULL);
   _mesa_hash_table_u64_destroy(data->accel_struct_vas);
}

static uint32_t
find_memory_index(VkDevice _device, VkMemoryPropertyFlags flags)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VkPhysicalDeviceMemoryProperties *mem_properties = &device->physical_device->memory_properties;
   for (uint32_t i = 0; i < mem_properties->memoryTypeCount; ++i) {
      if (mem_properties->memoryTypes[i].propertyFlags == flags) {
         return i;
      }
   }
   unreachable("invalid memory properties");
}

#define RRA_COPY_BATCH_SIZE 8

struct rra_accel_struct_copy {
   struct rra_copied_accel_struct copied_structures[RRA_COPY_BATCH_SIZE];
   uint8_t *map_data;
   VkDeviceMemory memory;
   VkBuffer buffer;
   VkCommandPool pool;
   VkCommandBuffer cmd_buffer;
};

static VkResult
rra_init_acceleration_structure_copy(VkDevice vk_device, uint32_t family_index,
                                     struct rra_accel_struct_copy *dst)
{
   RADV_FROM_HANDLE(radv_device, device, vk_device);

   VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = family_index,
   };

   VkResult result = vk_common_CreateCommandPool(vk_device, &pool_info, NULL, &dst->pool);
   if (result != VK_SUCCESS)
      goto fail;

   VkCommandBufferAllocateInfo cmdbuf_alloc_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = dst->pool,
      .commandBufferCount = 1,
   };
   result = vk_common_AllocateCommandBuffers(vk_device, &cmdbuf_alloc_info, &dst->cmd_buffer);
   if (result != VK_SUCCESS)
      goto fail_pool;

   size_t max_size = 0;

   hash_table_foreach(device->rra_trace.accel_structs, entry)
   {
      VkAccelerationStructureKHR structure = radv_acceleration_structure_to_handle((void *)entry->key);
      RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct, structure);
      max_size = MAX2(max_size, accel_struct->size);
   }

   size_t data_size = max_size * RRA_COPY_BATCH_SIZE;

   VkBufferCreateInfo buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = data_size,
      .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
   };

   result = radv_CreateBuffer(vk_device, &buffer_create_info, NULL, &dst->buffer);
   if (result != VK_SUCCESS)
      goto fail_pool;
   VkMemoryRequirements requirements;
   vk_common_GetBufferMemoryRequirements(vk_device, dst->buffer, &requirements);

   VkMemoryAllocateFlagsInfo flags_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
      .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
   };

   VkMemoryAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &flags_info,
      .allocationSize = requirements.size,
      .memoryTypeIndex = find_memory_index(vk_device, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                         VK_MEMORY_PROPERTY_HOST_CACHED_BIT),
   };
   result = radv_AllocateMemory(vk_device, &alloc_info, NULL, &dst->memory);
   if (result != VK_SUCCESS)
      goto fail_buffer;

   result = radv_MapMemory(vk_device, dst->memory, 0, VK_WHOLE_SIZE, 0, (void **)&dst->map_data);
   if (result != VK_SUCCESS)
      goto fail_memory;

   result = vk_common_BindBufferMemory(vk_device, dst->buffer, dst->memory, 0);
   if (result != VK_SUCCESS)
      goto fail_memory;

   return result;
fail_memory:
   radv_FreeMemory(vk_device, dst->memory, NULL);
fail_buffer:
   radv_DestroyBuffer(vk_device, dst->buffer, NULL);
fail_pool:
   vk_common_DestroyCommandPool(vk_device, dst->pool, NULL);
fail:
   return result;
}

static VkResult
rra_copy_acceleration_structures(VkQueue vk_queue, struct rra_accel_struct_copy *dst,
                                 struct hash_entry **last_entry, uint32_t count,
                                 uint32_t *copied_structure_count)
{
   RADV_FROM_HANDLE(radv_queue, queue, vk_queue);
   *copied_structure_count = 0;

   struct radv_device *device = queue->device;
   VkDevice vk_device = radv_device_to_handle(device);

   RADV_FROM_HANDLE(radv_cmd_buffer, cmdbuf, dst->cmd_buffer);

   vk_common_ResetCommandPool(vk_device, dst->pool, 0);

   /*
    * Wait for possible AS build/trace calls on all queues.
    */
   VkResult result = vk_common_DeviceWaitIdle(radv_device_to_handle(device));
   if (result != VK_SUCCESS)
      goto fail;

   VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
   };

   radv_BeginCommandBuffer(dst->cmd_buffer, &begin_info);

   uint64_t dst_offset = 0;
   for (uint32_t i = 0; i < count;) {
      struct hash_entry *entry =
         _mesa_hash_table_next_entry(device->rra_trace.accel_structs, *last_entry);

      if (!entry)
         break;

      *last_entry = entry;

      VkResult event_result = radv_GetEventStatus(vk_device, radv_event_to_handle(entry->data));
      if (event_result != VK_EVENT_SET) {
         continue;
      }

      VkAccelerationStructureKHR structure = radv_acceleration_structure_to_handle((void *)entry->key);
      RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct, structure);

      struct radv_buffer tmp_buffer;
      radv_buffer_init(&tmp_buffer, cmdbuf->device, accel_struct->bo, accel_struct->size, accel_struct->mem_offset);

      radv_CmdCopyBuffer2(dst->cmd_buffer, &(const VkCopyBufferInfo2){
         .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
         .srcBuffer = radv_buffer_to_handle(&tmp_buffer),
         .dstBuffer = dst->buffer,
         .regionCount = 1,
         .pRegions = &(const VkBufferCopy2){
            .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
            .srcOffset = 0,
            .dstOffset = dst_offset,
            .size = accel_struct->size,
         },
      });

      radv_buffer_finish(&tmp_buffer);

      dst->copied_structures[i].handle = structure;
      dst->copied_structures[i].data = dst->map_data + dst_offset;
      
      dst_offset += accel_struct->size;

      ++(*copied_structure_count);
      ++i;
   }
   result = radv_EndCommandBuffer(dst->cmd_buffer);
   if (result != VK_SUCCESS)
      goto fail;

   VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers = &dst->cmd_buffer,
   };
   result = vk_common_QueueSubmit(vk_queue, 1, &submit_info, VK_NULL_HANDLE);
   if (result != VK_SUCCESS)
      goto fail;

   result = vk_common_QueueWaitIdle(vk_queue);

fail:
   return result;
}

VkResult
radv_rra_dump_trace(VkQueue vk_queue, char *filename)
{
   RADV_FROM_HANDLE(radv_queue, queue, vk_queue);
   struct radv_device *device = queue->device;
   VkDevice vk_device = radv_device_to_handle(device);

   uint32_t accel_struct_count = _mesa_hash_table_num_entries(device->rra_trace.accel_structs);

   VkResult result;

   uint64_t *accel_struct_offsets = calloc(accel_struct_count, sizeof(uint64_t));
   if (!accel_struct_offsets)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   FILE *file = fopen(filename, "w");
   if (!file) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   /*
    * The header contents can only be determined after all acceleration
    * structures have been dumped. An empty struct is written instead
    * to keep offsets intact.
    */
   struct rra_file_header header = {0};
   fwrite(&header, sizeof(struct rra_file_header), 1, file);

   uint64_t api_info_offset = (uint64_t)ftell(file);
   uint64_t api = RADV_RRA_API_VULKAN;
   fwrite(&api, sizeof(uint64_t), 1, file);

   uint64_t asic_info_offset = (uint64_t)ftell(file);
   rra_dump_asic_info(&device->physical_device->rad_info, file);

   uint64_t written_accel_struct_count = 0;

   struct rra_accel_struct_copy copy = {0};
   result = rra_init_acceleration_structure_copy(vk_device, queue->vk.queue_family_index, &copy);
   if (result != VK_SUCCESS)
      goto fail;

   struct hash_entry *last_entry = NULL;
   while (_mesa_hash_table_next_entry(device->rra_trace.accel_structs, last_entry)) {
      uint32_t copied_structure_count;
      result = rra_copy_acceleration_structures(vk_queue, &copy, &last_entry, RRA_COPY_BATCH_SIZE,
                                                &copied_structure_count);
      if (result != VK_SUCCESS)
         goto copy_fail;

      for (uint32_t i = 0; i < copied_structure_count; ++i) {
         accel_struct_offsets[written_accel_struct_count] = (uint64_t)ftell(file);
         result = rra_dump_acceleration_structure(&copy.copied_structures[i],
                                                  device->rra_trace.accel_struct_vas,
                                                  device->rra_trace.validate_as, file);
         if (result != VK_SUCCESS)
            goto copy_fail;
         ++written_accel_struct_count;
      }
   }

   uint64_t chunk_info_offset = (uint64_t)ftell(file);
   rra_dump_chunk_description(api_info_offset, 0, 8, "ApiInfo", RADV_RRA_CHUNK_ID_ASIC_API_INFO,
                              file);
   rra_dump_chunk_description(asic_info_offset, 0, sizeof(struct rra_asic_info), "AsicInfo",
                              RADV_RRA_CHUNK_ID_ASIC_API_INFO, file);

   for (uint32_t i = 0; i < written_accel_struct_count; ++i) {
      uint64_t accel_struct_size;
      if (i == written_accel_struct_count - 1)
         accel_struct_size = (uint64_t)(chunk_info_offset - accel_struct_offsets[i]);
      else
         accel_struct_size = (uint64_t)(accel_struct_offsets[i + 1] - accel_struct_offsets[i]);

      rra_dump_chunk_description(accel_struct_offsets[i],
                                 sizeof(struct rra_accel_struct_chunk_header), accel_struct_size,
                                 "RawAccelStruc", RADV_RRA_CHUNK_ID_ACCEL_STRUCT, file);
   }

   uint64_t file_end = (uint64_t)ftell(file);

   /* All info is available, dump header now */
   fseek(file, 0, SEEK_SET);
   rra_dump_header(file, chunk_info_offset, file_end - chunk_info_offset);

   fclose(file);
copy_fail:
   radv_DestroyBuffer(vk_device, copy.buffer, NULL);
   radv_FreeMemory(vk_device, copy.memory, NULL);
   vk_common_DestroyCommandPool(vk_device, copy.pool, NULL);
fail:
   free(accel_struct_offsets);
   return result;
}
