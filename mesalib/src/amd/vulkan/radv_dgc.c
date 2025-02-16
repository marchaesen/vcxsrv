/*
 * Copyright © 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_dgc.h"
#include "meta/radv_meta.h"
#include "nir/radv_meta_nir.h"
#include "radv_entrypoints.h"
#include "radv_pipeline_rt.h"

#include "ac_rgp.h"

#include "nir_builder.h"

#include "vk_common_entrypoints.h"
#include "vk_device_generated_commands.h"
#include "vk_shader_module.h"

#define PKT3_INDIRECT_BUFFER_BYTES 16
#define DGC_VBO_INFO_SIZE (sizeof(struct radv_vbo_info) + 4 /* vbo_offsets */)

/* The DGC command buffer layout is quite complex, here's some explanations:
 *
 * Without the DGC preamble, the default layout looks like:
 *
 * +---------+----------+---------+-----------------+
 * | trailer | commands | padding | jump to trailer |
 * +---------+----------+---------+-----------------+
 *
 * The trailer is used to implement IB chaining for compute queue because IB2 isn't supported. The
 * trailer is patched at execute time to chain back the DGC command buffer. The trailer is added at
 * the beginning to make sure the offset is fixed (ie. not possible to know the offset with a
 * preamble). In practice the execution looks like:
 *
 * +----------+---------+-----------------+    +---------+    +-----------------------+
 * | commands | padding | jump to trailer | -> | trailer | -> | postamble (normal CS) |
 * +----------+---------+-----------------+    +---------+    +-----------------------+
 *
 * When DGC uses a preamble (to optimize large empty indirect sequence count by removing a ton of
 * padding), the trailer is still used but the layout looks like:
 *
 * +---------+---------+-----------------+     +----------+---------+-----------------+
 * | trailer | padding | INDIRECT_BUFFER | ->  | commands | padding | jump to trailer |
 * +---------+---------+-----------------+     +----------+---------+-----------------+
 *
 * When DGC uses task shaders, the command buffer is split in two parts (GFX/COMPUTE), the
 * default layout looks like:
 *
 * +--------------+---------+--------------+---------+
 * | GFX commands | padding | ACE commands | padding |
 * +--------------+---------+--------------+---------+
 *
 * The execution of this DGC command buffer is different if it's GFX or COMPUTE queue:
 * - on GFX, the driver uses the IB2 packet which the easiest solution
 * - on COMPUTE, IB2 isn't supported and the driver chains the DGC command buffer by patching the
 *   trailer
 */

uint32_t
radv_dgc_get_buffer_alignment(const struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   return MAX2(pdev->info.ip[AMD_IP_GFX].ib_alignment, pdev->info.ip[AMD_IP_COMPUTE].ib_alignment);
}

static uint32_t
radv_pad_cmdbuf(const struct radv_device *device, uint32_t size, enum amd_ip_type ip_type)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const uint32_t ib_alignment = (pdev->info.ip[ip_type].ib_pad_dw_mask + 1) * 4;

   return align(size, ib_alignment);
}

static uint32_t
radv_align_cmdbuf(const struct radv_device *device, uint32_t size, enum amd_ip_type ip_type)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const uint32_t ib_alignment = pdev->info.ip[ip_type].ib_alignment;

   return align(size, ib_alignment);
}

static unsigned
radv_dgc_preamble_cmdbuf_size(const struct radv_device *device, enum amd_ip_type ip_type)
{
   return radv_pad_cmdbuf(device, PKT3_INDIRECT_BUFFER_BYTES, ip_type);
}

static unsigned
radv_dgc_trailer_cmdbuf_size(const struct radv_device *device, enum amd_ip_type ip_type)
{
   return radv_pad_cmdbuf(device, PKT3_INDIRECT_BUFFER_BYTES, ip_type);
}

static bool
radv_dgc_use_preamble(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo)
{
   /* Heuristic on when the overhead for the preamble (i.e. double jump) is worth it. Obviously
    * a bit of a guess as it depends on the actual count which we don't know. */
   return pGeneratedCommandsInfo->sequenceCountAddress != 0 && pGeneratedCommandsInfo->maxSequenceCount >= 64;
}

struct radv_shader *
radv_dgc_get_shader(const VkGeneratedCommandsPipelineInfoEXT *pipeline_info,
                    const VkGeneratedCommandsShaderInfoEXT *eso_info, gl_shader_stage stage)
{
   if (pipeline_info) {
      VK_FROM_HANDLE(radv_pipeline, pipeline, pipeline_info->pipeline);
      return radv_get_shader(pipeline->shaders, stage);
   } else if (eso_info) {
      VkShaderStageFlags stages = 0;

      for (uint32_t i = 0; i < eso_info->shaderCount; i++) {
         VK_FROM_HANDLE(radv_shader_object, shader_object, eso_info->pShaders[i]);
         stages |= mesa_to_vk_shader_stage(shader_object->stage);
      }

      for (uint32_t i = 0; i < eso_info->shaderCount; i++) {
         VK_FROM_HANDLE(radv_shader_object, shader_object, eso_info->pShaders[i]);

         if (shader_object->stage != stage)
            continue;

         if (stage == MESA_SHADER_VERTEX && (stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)) {
            return shader_object->as_ls.shader;
         } else if ((stage == MESA_SHADER_VERTEX || stage == MESA_SHADER_TESS_EVAL) &&
                    (stages & VK_SHADER_STAGE_GEOMETRY_BIT)) {
            return shader_object->as_es.shader;
         } else {
            return shader_object->shader;
         }
      }
   }

   return NULL;
}

static void
radv_get_sequence_size_compute(const struct radv_indirect_command_layout *layout, const void *pNext, uint32_t *cmd_size,
                               uint32_t *upload_size)
{
   const struct radv_device *device = container_of(layout->vk.base.device, struct radv_device, vk);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   const VkGeneratedCommandsPipelineInfoEXT *pipeline_info =
      vk_find_struct_const(pNext, GENERATED_COMMANDS_PIPELINE_INFO_EXT);
   const VkGeneratedCommandsShaderInfoEXT *eso_info = vk_find_struct_const(pNext, GENERATED_COMMANDS_SHADER_INFO_EXT);

   struct radv_shader *cs = radv_dgc_get_shader(pipeline_info, eso_info, MESA_SHADER_COMPUTE);

   /* dispatch */
   *cmd_size += 5 * 4;

   if (cs) {
      const struct radv_userdata_info *loc = radv_get_user_sgpr_info(cs, AC_UD_CS_GRID_SIZE);
      if (loc->sgpr_idx != -1) {
         if (device->load_grid_size_from_user_sgpr) {
            /* PKT3_SET_SH_REG for immediate values */
            *cmd_size += 5 * 4;
         } else {
            /* PKT3_SET_SH_REG for pointer */
            *cmd_size += 4 * 4;
         }
      }
   } else {
      /* COMPUTE_PGM_{LO,RSRC1,RSRC2} */
      *cmd_size += 7 * 4;

      if (pdev->info.gfx_level >= GFX10) {
         /* COMPUTE_PGM_RSRC3 */
         *cmd_size += 3 * 4;
      }

      /* COMPUTE_{RESOURCE_LIMITS,NUM_THREADS_X} */
      *cmd_size += 8 * 4;

      /* Assume the compute shader needs grid size because we can't know the information for
       * indirect pipelines.
       */
      if (device->load_grid_size_from_user_sgpr) {
         /* PKT3_SET_SH_REG for immediate values */
         *cmd_size += 5 * 4;
      } else {
         /* PKT3_SET_SH_REG for pointer */
         *cmd_size += 4 * 4;
      }

      /* PKT3_SET_SH_REG for indirect descriptor sets pointer */
      *cmd_size += 3 * 4;
   }

   if (device->sqtt.bo) {
      /* sqtt markers */
      *cmd_size += 8 * 3 * 4;
   }
}

static void
radv_get_sequence_size_graphics(const struct radv_indirect_command_layout *layout, const void *pNext,
                                uint32_t *cmd_size, uint32_t *ace_cmd_size, uint32_t *upload_size)
{
   const struct radv_device *device = container_of(layout->vk.base.device, struct radv_device, vk);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   const VkGeneratedCommandsPipelineInfoEXT *pipeline_info =
      vk_find_struct_const(pNext, GENERATED_COMMANDS_PIPELINE_INFO_EXT);
   const VkGeneratedCommandsShaderInfoEXT *eso_info = vk_find_struct_const(pNext, GENERATED_COMMANDS_SHADER_INFO_EXT);

   struct radv_shader *vs = radv_dgc_get_shader(pipeline_info, eso_info, MESA_SHADER_VERTEX);

   if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_VB)) {
      *upload_size += 16 * util_bitcount(vs->info.vs.vb_desc_usage_mask);

      /* One PKT3_SET_SH_REG for emitting VBO pointer (32-bit) */
      *cmd_size += 3 * 4;
   }

   if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IB)) {
      /* Index type write (normal reg write) + index buffer base write (64-bits, but special packet
       * so only 1 word overhead) + index buffer size (again, special packet so only 1 word
       * overhead)
       */
      *cmd_size += (3 + 3 + 2) * 4;
   }

   if (layout->vk.draw_count) {
      if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DRAW_MESH)) {
         const struct radv_shader *task_shader = radv_dgc_get_shader(pipeline_info, eso_info, MESA_SHADER_TASK);

         if (task_shader) {
            /* PKT3_DISPATCH_TASKMESH_GFX */
            *cmd_size += 4 * 4;

            /* PKT3_DISPATCH_TASKMESH_INDIRECT_MULTI_ACE */
            *ace_cmd_size += 11 * 4;
         } else {
            struct radv_shader *ms = radv_dgc_get_shader(pipeline_info, eso_info, MESA_SHADER_MESH);

            /* PKT3_SET_BASE + PKT3_SET_SH_REG + PKT3_DISPATCH_MESH_INDIRECT_MULTI */
            *cmd_size += (4 + (ms->info.vs.needs_draw_id ? 3 : 0) + 9) * 4;
         }
      } else {
         /* PKT3_SET_BASE + PKT3_DRAW_{INDEX}_INDIRECT_MULTI */
         *cmd_size += (4 + 10) * 4;
      }
   } else {
      if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DRAW_INDEXED)) {
         if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IB)) {
            /* userdata writes + instance count + indexed draw */
            *cmd_size += (5 + 2 + 5) * 4;
         } else {
            /* PKT3_SET_BASE + PKT3_SET_SH_REG + PKT3_DRAW_{INDEX}_INDIRECT_MULTI */
            *cmd_size += (4 + (vs->info.vs.needs_draw_id ? 10 : 5)) * 4;
         }
      } else {
         if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DRAW_MESH)) {
            const struct radv_shader *task_shader = radv_dgc_get_shader(pipeline_info, eso_info, MESA_SHADER_TASK);

            if (task_shader) {
               const struct radv_userdata_info *xyz_loc = radv_get_user_sgpr_info(task_shader, AC_UD_CS_GRID_SIZE);
               const struct radv_userdata_info *draw_id_loc =
                  radv_get_user_sgpr_info(task_shader, AC_UD_CS_TASK_DRAW_ID);

               /* PKT3_DISPATCH_TASKMESH_GFX */
               *cmd_size += 4 * 4;

               if (xyz_loc->sgpr_idx != -1)
                  *ace_cmd_size += 5 * 4;
               if (draw_id_loc->sgpr_idx != -1)
                  *ace_cmd_size += 3 * 4;

               /* PKT3_DISPATCH_TASKMESH_DIRECT_ACE */
               *ace_cmd_size += 6 * 4;
            } else {
               /* userdata writes + instance count + non-indexed draw */
               *cmd_size += (6 + 2 + (pdev->mesh_fast_launch_2 ? 5 : 3)) * 4;
            }
         } else {
            /* userdata writes + instance count + non-indexed draw */
            *cmd_size += (5 + 2 + 3) * 4;
         }
      }
   }

   if (device->sqtt.bo) {
      /* sqtt markers */
      *cmd_size += 5 * 3 * 4;
   }
}

static void
radv_get_sequence_size_rt(const struct radv_indirect_command_layout *layout, const void *pNext, uint32_t *cmd_size,
                          uint32_t *upload_size)
{
   const struct radv_device *device = container_of(layout->vk.base.device, struct radv_device, vk);

   const VkGeneratedCommandsPipelineInfoEXT *pipeline_info =
      vk_find_struct_const(pNext, GENERATED_COMMANDS_PIPELINE_INFO_EXT);
   VK_FROM_HANDLE(radv_pipeline, pipeline, pipeline_info->pipeline);
   const struct radv_ray_tracing_pipeline *rt_pipeline = radv_pipeline_to_ray_tracing(pipeline);
   const struct radv_shader *rt_prolog = rt_pipeline->prolog;

   /* dispatch */
   *cmd_size += 5 * 4;

   const struct radv_userdata_info *cs_grid_size_loc = radv_get_user_sgpr_info(rt_prolog, AC_UD_CS_GRID_SIZE);
   if (cs_grid_size_loc->sgpr_idx != -1) {
      if (device->load_grid_size_from_user_sgpr) {
         /* PKT3_LOAD_SH_REG_INDEX */
         *cmd_size += 5 * 4;
      } else {
         /* PKT3_SET_SH_REG for pointer */
         *cmd_size += 4 * 4;
      }
   }

   const struct radv_userdata_info *cs_sbt_descriptors_loc =
      radv_get_user_sgpr_info(rt_prolog, AC_UD_CS_SBT_DESCRIPTORS);
   if (cs_sbt_descriptors_loc->sgpr_idx != -1) {
      /* PKT3_SET_SH_REG for pointer */
      *cmd_size += 4 * 4;
   }

   const struct radv_userdata_info *cs_ray_launch_size_addr_loc =
      radv_get_user_sgpr_info(rt_prolog, AC_UD_CS_RAY_LAUNCH_SIZE_ADDR);
   if (cs_ray_launch_size_addr_loc->sgpr_idx != -1) {
      /* PKT3_SET_SH_REG for pointer */
      *cmd_size += 4 * 4;
   }

   if (device->sqtt.bo) {
      /* sqtt markers */
      *cmd_size += 5 * 3 * 4;
   }
}

static void
radv_get_sequence_size(const struct radv_indirect_command_layout *layout, const void *pNext, uint32_t *cmd_size,
                       uint32_t *ace_cmd_size, uint32_t *upload_size)
{
   const struct radv_device *device = container_of(layout->vk.base.device, struct radv_device, vk);
   const VkGeneratedCommandsPipelineInfoEXT *pipeline_info =
      vk_find_struct_const(pNext, GENERATED_COMMANDS_PIPELINE_INFO_EXT);
   const VkGeneratedCommandsShaderInfoEXT *eso_info = vk_find_struct_const(pNext, GENERATED_COMMANDS_SHADER_INFO_EXT);

   *cmd_size = 0;
   *ace_cmd_size = 0;
   *upload_size = 0;

   if (layout->vk.dgc_info & (BITFIELD_BIT(MESA_VK_DGC_PC) | BITFIELD_BIT(MESA_VK_DGC_SI))) {
      VK_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, layout->vk.layout);
      bool need_copy = false;

      if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES)) {
         /* Assume the compute shader needs both user SGPRs because we can't know the information
          * for indirect pipelines.
          */
         *cmd_size += 3 * 4;
         need_copy = true;

         *cmd_size += (3 * util_bitcount64(layout->push_constant_mask)) * 4;
      } else {
         struct radv_shader *shaders[MESA_VULKAN_SHADER_STAGES] = {0};
         if (pipeline_info) {
            VK_FROM_HANDLE(radv_pipeline, pipeline, pipeline_info->pipeline);

            if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_RT)) {
               const struct radv_ray_tracing_pipeline *rt_pipeline = radv_pipeline_to_ray_tracing(pipeline);
               struct radv_shader *rt_prolog = rt_pipeline->prolog;

               shaders[MESA_SHADER_COMPUTE] = rt_prolog;
            } else {
               memcpy(shaders, pipeline->shaders, sizeof(shaders));
            }
         } else if (eso_info) {
            for (unsigned i = 0; i < eso_info->shaderCount; ++i) {
               VK_FROM_HANDLE(radv_shader_object, shader_object, eso_info->pShaders[i]);
               struct radv_shader *shader = shader_object->shader;
               gl_shader_stage stage = shader->info.stage;

               shaders[stage] = shader;
            }
         }

         for (unsigned i = 0; i < ARRAY_SIZE(shaders); ++i) {
            const struct radv_shader *shader = shaders[i];

            if (!shader)
               continue;

            const struct radv_userdata_locations *locs = &shader->info.user_sgprs_locs;
            if (locs->shader_data[AC_UD_PUSH_CONSTANTS].sgpr_idx >= 0) {
               /* One PKT3_SET_SH_REG for emitting push constants pointer (32-bit) */
               if (i == MESA_SHADER_TASK) {
                  *ace_cmd_size += 3 * 4;
               } else {
                  *cmd_size += 3 * 4;
               }
               need_copy = true;
            }
            if (locs->shader_data[AC_UD_INLINE_PUSH_CONSTANTS].sgpr_idx >= 0) {
               /* One PKT3_SET_SH_REG writing all inline push constants. */
               const uint32_t inline_pc_size = (3 * util_bitcount64(layout->push_constant_mask)) * 4;

               if (i == MESA_SHADER_TASK) {
                  *ace_cmd_size += inline_pc_size;
               } else {
                  *cmd_size += inline_pc_size;
               }
            }
         }
      }

      if (need_copy) {
         *upload_size += align(pipeline_layout->push_constant_size, 16);
      }
   }

   if (device->sqtt.bo) {
      /* THREAD_TRACE_MARKER */
      *cmd_size += 2 * 4;
   }

   if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DISPATCH)) {
      radv_get_sequence_size_compute(layout, pNext, cmd_size, upload_size);
   } else if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_RT)) {
      radv_get_sequence_size_rt(layout, pNext, cmd_size, upload_size);
   } else {
      radv_get_sequence_size_graphics(layout, pNext, cmd_size, ace_cmd_size, upload_size);
   }
}

struct dgc_cmdbuf_layout {
   bool use_preamble;
   uint32_t alloc_size;

   uint32_t main_trailer_offset;
   uint32_t main_preamble_offset;
   uint32_t main_offset;
   uint32_t main_cmd_stride;
   uint32_t main_preamble_size;
   uint32_t main_size;

   uint32_t ace_trailer_offset;
   uint32_t ace_preamble_offset;
   uint32_t ace_main_offset;
   uint32_t ace_cmd_stride;
   uint32_t ace_preamble_size;
   uint32_t ace_size;

   uint32_t upload_offset;
   uint32_t upload_stride;
   uint32_t upload_size;
};

static void
get_dgc_cmdbuf_layout(const struct radv_device *device, const struct radv_indirect_command_layout *dgc_layout,
                      const void *pNext, uint32_t sequences_count, bool use_preamble, struct dgc_cmdbuf_layout *layout)
{
   uint32_t offset = 0;

   memset(layout, 0, sizeof(*layout));

   radv_get_sequence_size(dgc_layout, pNext, &layout->main_cmd_stride, &layout->ace_cmd_stride, &layout->upload_stride);

   layout->use_preamble = use_preamble;
   if (layout->use_preamble) {
      layout->main_preamble_size = radv_dgc_preamble_cmdbuf_size(device, AMD_IP_GFX);
      layout->ace_preamble_size = radv_dgc_preamble_cmdbuf_size(device, AMD_IP_COMPUTE);
   }

   layout->main_size =
      radv_pad_cmdbuf(device, (layout->main_cmd_stride * sequences_count) + PKT3_INDIRECT_BUFFER_BYTES, AMD_IP_GFX);
   layout->ace_size =
      radv_pad_cmdbuf(device, (layout->ace_cmd_stride * sequences_count) + PKT3_INDIRECT_BUFFER_BYTES, AMD_IP_COMPUTE);
   layout->upload_size = layout->upload_stride * sequences_count;

   /* Main */
   layout->main_trailer_offset = 0;

   offset += radv_dgc_trailer_cmdbuf_size(device, AMD_IP_GFX);
   offset = radv_align_cmdbuf(device, offset, AMD_IP_GFX);
   layout->main_preamble_offset = offset;

   if (layout->use_preamble)
      offset += layout->main_preamble_size;
   offset = radv_align_cmdbuf(device, offset, AMD_IP_GFX);

   layout->main_offset = offset;
   offset += layout->main_size;

   /* ACE */
   if (layout->ace_cmd_stride) {
      offset = radv_align_cmdbuf(device, offset, AMD_IP_COMPUTE);

      layout->ace_trailer_offset = offset;

      offset += radv_dgc_trailer_cmdbuf_size(device, AMD_IP_COMPUTE);
      offset = radv_align_cmdbuf(device, offset, AMD_IP_COMPUTE);

      layout->ace_preamble_offset = offset;

      if (layout->use_preamble)
         offset += layout->ace_preamble_size;
      offset = radv_align_cmdbuf(device, offset, AMD_IP_COMPUTE);

      layout->ace_main_offset = offset;
      offset += layout->ace_size;
   }

   /* Upload */
   layout->upload_offset = offset;
   offset += layout->upload_size;

   layout->alloc_size = offset;
}

static uint32_t
radv_get_indirect_cmdbuf_size(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo, enum amd_ip_type ip_type)
{
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, pGeneratedCommandsInfo->indirectCommandsLayout);
   const struct radv_device *device = container_of(layout->vk.base.device, struct radv_device, vk);
   const bool use_preamble = radv_dgc_use_preamble(pGeneratedCommandsInfo);
   const uint32_t sequences_count = pGeneratedCommandsInfo->maxSequenceCount;
   struct dgc_cmdbuf_layout cmdbuf_layout;

   get_dgc_cmdbuf_layout(device, layout, pGeneratedCommandsInfo->pNext, sequences_count, use_preamble, &cmdbuf_layout);

   if (use_preamble)
      return ip_type == AMD_IP_GFX ? cmdbuf_layout.main_preamble_size : cmdbuf_layout.ace_preamble_size;

   return ip_type == AMD_IP_GFX ? cmdbuf_layout.main_size : cmdbuf_layout.ace_size;
}

static uint32_t
radv_get_indirect_cmdbuf_offset(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo, enum amd_ip_type ip_type)
{
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, pGeneratedCommandsInfo->indirectCommandsLayout);
   const struct radv_device *device = container_of(layout->vk.base.device, struct radv_device, vk);
   const bool use_preamble = radv_dgc_use_preamble(pGeneratedCommandsInfo);
   const uint32_t sequences_count = pGeneratedCommandsInfo->maxSequenceCount;
   struct dgc_cmdbuf_layout cmdbuf_layout;

   get_dgc_cmdbuf_layout(device, layout, pGeneratedCommandsInfo->pNext, sequences_count, use_preamble, &cmdbuf_layout);

   return ip_type == AMD_IP_GFX ? cmdbuf_layout.main_preamble_offset : cmdbuf_layout.ace_preamble_offset;
}

static uint32_t
radv_get_indirect_trailer_offset(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo, enum amd_ip_type ip_type)
{
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, pGeneratedCommandsInfo->indirectCommandsLayout);
   const struct radv_device *device = container_of(layout->vk.base.device, struct radv_device, vk);
   const bool use_preamble = radv_dgc_use_preamble(pGeneratedCommandsInfo);
   const uint32_t sequences_count = pGeneratedCommandsInfo->maxSequenceCount;
   struct dgc_cmdbuf_layout cmdbuf_layout;

   get_dgc_cmdbuf_layout(device, layout, pGeneratedCommandsInfo->pNext, sequences_count, use_preamble, &cmdbuf_layout);

   const uint32_t offset = ip_type == AMD_IP_GFX ? cmdbuf_layout.main_trailer_offset : cmdbuf_layout.ace_trailer_offset;

   return offset + radv_dgc_trailer_cmdbuf_size(device, ip_type) - PKT3_INDIRECT_BUFFER_BYTES;
}

uint32_t
radv_get_indirect_main_cmdbuf_size(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo)
{
   return radv_get_indirect_cmdbuf_size(pGeneratedCommandsInfo, AMD_IP_GFX);
}

uint32_t
radv_get_indirect_main_cmdbuf_offset(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo)
{
   return radv_get_indirect_cmdbuf_offset(pGeneratedCommandsInfo, AMD_IP_GFX);
}

uint32_t
radv_get_indirect_main_trailer_offset(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo)
{
   return radv_get_indirect_trailer_offset(pGeneratedCommandsInfo, AMD_IP_GFX);
}

uint32_t
radv_get_indirect_ace_cmdbuf_size(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo)
{
   return radv_get_indirect_cmdbuf_size(pGeneratedCommandsInfo, AMD_IP_COMPUTE);
}

uint32_t
radv_get_indirect_ace_cmdbuf_offset(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo)
{
   return radv_get_indirect_cmdbuf_offset(pGeneratedCommandsInfo, AMD_IP_COMPUTE);
}

uint32_t
radv_get_indirect_ace_trailer_offset(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo)
{
   return radv_get_indirect_trailer_offset(pGeneratedCommandsInfo, AMD_IP_COMPUTE);
}

struct radv_dgc_params {
   uint32_t cmd_buf_preamble_offset;
   uint32_t cmd_buf_main_offset;
   uint32_t cmd_buf_stride;
   uint32_t cmd_buf_size;
   uint32_t ace_cmd_buf_trailer_offset;
   uint32_t ace_cmd_buf_preamble_offset;
   uint32_t ace_cmd_buf_main_offset;
   uint32_t ace_cmd_buf_stride;
   uint32_t ace_cmd_buf_size;
   uint32_t upload_main_offset;
   uint32_t upload_stride;
   uint32_t upload_addr;
   uint32_t sequence_count;
   uint64_t sequence_count_addr;
   uint64_t stream_addr;

   uint8_t queue_family;
   uint8_t use_preamble;

   uint64_t params_addr;

   /* draw info */
   uint16_t vtx_base_sgpr;
   uint32_t max_index_count;
   uint32_t max_draw_count;

   /* task/mesh info */
   uint8_t has_task_shader;
   uint16_t mesh_ring_entry_sgpr;
   uint8_t linear_dispatch_en;
   uint16_t task_ring_entry_sgpr;
   uint16_t task_xyz_sgpr;
   uint16_t task_draw_id_sgpr;

   /* dispatch info */
   uint16_t grid_base_sgpr;
   uint32_t wave32;

   /* RT info */
   uint16_t cs_sbt_descriptors;
   uint16_t cs_ray_launch_size_addr;

   /* VBO info */
   uint32_t vb_desc_usage_mask;
   uint16_t vbo_reg;
   uint8_t dynamic_vs_input;
   uint8_t use_per_attribute_vb_descs;

   /* push constants info */
   uint8_t const_copy;
   uint16_t push_constant_stages;

   /* IES info */
   uint64_t ies_addr;
   uint32_t ies_stride;
   uint32_t indirect_desc_sets_va;

   /* For conditional rendering on ACE. */
   uint8_t predicating;
   uint8_t predication_type;
   uint64_t predication_va;
};

enum {
   DGC_USES_DRAWID = 1u << 14,
   DGC_USES_BASEINSTANCE = 1u << 15,
   DGC_USES_GRID_SIZE = DGC_USES_BASEINSTANCE, /* Mesh shader only */
};

struct dgc_cmdbuf {
   const struct radv_device *dev;
   const struct radv_indirect_command_layout *layout;

   nir_builder *b;
   nir_def *va;
   nir_variable *offset;
   nir_variable *upload_offset;

   nir_def *ies_va;
};

static void
dgc_emit(struct dgc_cmdbuf *cs, unsigned count, nir_def **values)
{
   nir_builder *b = cs->b;

   for (unsigned i = 0; i < count; i += 4) {
      nir_def *offset = nir_load_var(b, cs->offset);
      nir_def *store_val = nir_vec(b, values + i, MIN2(count - i, 4));
      assert(store_val->bit_size >= 32);
      nir_build_store_global(b, store_val, nir_iadd(b, cs->va, nir_u2u64(b, offset)), .access = ACCESS_NON_READABLE);
      nir_store_var(b, cs->offset, nir_iadd_imm(b, offset, store_val->num_components * store_val->bit_size / 8), 0x1);
   }
}

static void
dgc_upload(struct dgc_cmdbuf *cs, nir_def *data)
{
   nir_builder *b = cs->b;

   nir_def *upload_offset = nir_load_var(b, cs->upload_offset);
   nir_build_store_global(b, data, nir_iadd(b, cs->va, nir_u2u64(b, upload_offset)), .access = ACCESS_NON_READABLE);
   nir_store_var(b, cs->upload_offset, nir_iadd_imm(b, upload_offset, data->num_components * data->bit_size / 8), 0x1);
}

#define load_param32(b, field)                                                                                         \
   nir_load_push_constant((b), 1, 32, nir_imm_int((b), 0), .base = offsetof(struct radv_dgc_params, field), .range = 4)

#define load_param16(b, field)                                                                                         \
   nir_ubfe_imm((b),                                                                                                   \
                nir_load_push_constant((b), 1, 32, nir_imm_int((b), 0),                                                \
                                       .base = (offsetof(struct radv_dgc_params, field) & ~3), .range = 4),            \
                (offsetof(struct radv_dgc_params, field) & 2) * 8, 16)

#define load_param8(b, field)                                                                                          \
   nir_ubfe_imm((b),                                                                                                   \
                nir_load_push_constant((b), 1, 32, nir_imm_int((b), 0),                                                \
                                       .base = (offsetof(struct radv_dgc_params, field) & ~3), .range = 4),            \
                (offsetof(struct radv_dgc_params, field) & 3) * 8, 8)

#define load_param64(b, field)                                                                                         \
   nir_pack_64_2x32((b), nir_load_push_constant((b), 2, 32, nir_imm_int((b), 0),                                       \
                                                .base = offsetof(struct radv_dgc_params, field), .range = 8))

static nir_def *
dgc_load_ies_va(struct dgc_cmdbuf *cs, nir_def *stream_addr)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   nir_builder *b = cs->b;

   nir_def *offset = nir_imm_int(b, layout->vk.ies_src_offset_B);
   nir_def *ies_index =
      nir_build_load_global(b, 1, 32, nir_iadd(b, stream_addr, nir_u2u64(b, offset)), .access = ACCESS_NON_WRITEABLE);
   nir_def *ies_stride = load_param32(b, ies_stride);
   nir_def *ies_offset = nir_imul(b, ies_index, ies_stride);

   return nir_iadd(b, load_param64(b, ies_addr), nir_u2u64(b, ies_offset));
}

static nir_def *
dgc_load_shader_metadata(struct dgc_cmdbuf *cs, uint32_t bitsize, uint32_t field_offset)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   nir_builder *b = cs->b;
   nir_def *va;

   if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES)) {
      va = cs->ies_va;
   } else {
      va = load_param64(b, params_addr);
   }

   return nir_load_global(b, nir_iadd_imm(b, va, field_offset), 4, 1, bitsize);
}

#define load_shader_metadata32(cs, field)                                                                              \
   dgc_load_shader_metadata(cs, 32, offsetof(struct radv_compute_pipeline_metadata, field))
#define load_shader_metadata64(cs, field)                                                                              \
   dgc_load_shader_metadata(cs, 64, offsetof(struct radv_compute_pipeline_metadata, field))

static nir_def *
dgc_load_vbo_metadata(struct dgc_cmdbuf *cs, uint32_t bitsize, nir_def *idx, uint32_t field_offset)
{
   nir_builder *b = cs->b;

   nir_def *va = load_param64(b, params_addr);
   nir_def *offset = nir_iadd_imm(b, nir_imul_imm(b, idx, DGC_VBO_INFO_SIZE), field_offset);

   return nir_load_global(b, nir_iadd(b, va, nir_u2u64(b, offset)), 4, 1, bitsize);
}

#define load_vbo_metadata32(cs, idx, field) dgc_load_vbo_metadata(cs, 32, idx, offsetof(struct radv_vbo_info, field))
#define load_vbo_metadata64(cs, idx, field) dgc_load_vbo_metadata(cs, 64, idx, offsetof(struct radv_vbo_info, field))
#define load_vbo_offset(cs, idx)            dgc_load_vbo_metadata(cs, 32, idx, sizeof(struct radv_vbo_info))

/* DGC cs emit macros */
#define dgc_cs_begin(cs)                                                                                               \
   struct dgc_cmdbuf *__cs = (cs);                                                                                     \
   nir_def *__dwords[32];                                                                                              \
   unsigned __num_dw = 0;

#define dgc_cs_emit(value)                                                                                             \
   assert(__num_dw < ARRAY_SIZE(__dwords));                                                                            \
   __dwords[__num_dw++] = value;

#define dgc_cs_emit_imm(value) dgc_cs_emit(nir_imm_int(__cs->b, value));

#define dgc_cs_set_sh_reg_seq(reg, num)                                                                                \
   dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, num, 0));                                                                     \
   dgc_cs_emit_imm((reg - SI_SH_REG_OFFSET) >> 2);

#define dgc_cs_end() dgc_emit(__cs, __num_dw, __dwords);

static nir_def *
nir_pkt3_base(nir_builder *b, unsigned op, nir_def *len, bool predicate)
{
   len = nir_iand_imm(b, len, 0x3fff);
   return nir_ior_imm(b, nir_ishl_imm(b, len, 16), PKT_TYPE_S(3) | PKT3_IT_OPCODE_S(op) | PKT3_PREDICATE(predicate));
}

static nir_def *
nir_pkt3(nir_builder *b, unsigned op, nir_def *len)
{
   return nir_pkt3_base(b, op, len, false);
}

/**
 * SQTT
 */
static void
dgc_emit_sqtt_userdata(struct dgc_cmdbuf *cs, nir_def *data)
{
   const struct radv_device *device = cs->dev;
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_builder *b = cs->b;

   if (!cs->dev->sqtt.bo)
      return;

   dgc_cs_begin(cs);
   dgc_cs_emit(nir_pkt3_base(b, PKT3_SET_UCONFIG_REG, nir_imm_int(b, 1), pdev->info.gfx_level >= GFX10));
   dgc_cs_emit_imm((R_030D08_SQ_THREAD_TRACE_USERDATA_2 - CIK_UCONFIG_REG_OFFSET) >> 2);
   dgc_cs_emit(data);
   dgc_cs_end();
}

static void
dgc_emit_sqtt_thread_trace_marker(struct dgc_cmdbuf *cs)
{
   if (!cs->dev->sqtt.bo)
      return;

   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_EVENT_WRITE, 0, 0));
   dgc_cs_emit_imm(EVENT_TYPE(V_028A90_THREAD_TRACE_MARKER | EVENT_INDEX(0)));
   dgc_cs_end();
}

static void
dgc_emit_sqtt_marker_event(struct dgc_cmdbuf *cs, nir_def *sequence_id, enum rgp_sqtt_marker_event_type event)
{
   struct rgp_sqtt_marker_event marker = {0};
   nir_builder *b = cs->b;

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_EVENT;
   marker.api_type = event;

   dgc_emit_sqtt_userdata(cs, nir_imm_int(b, marker.dword01));
   dgc_emit_sqtt_userdata(cs, nir_imm_int(b, marker.dword02));
   dgc_emit_sqtt_userdata(cs, sequence_id);
}

static void
dgc_emit_sqtt_marker_event_with_dims(struct dgc_cmdbuf *cs, nir_def *sequence_id, nir_def *x, nir_def *y, nir_def *z,
                                     enum rgp_sqtt_marker_event_type event)
{
   struct rgp_sqtt_marker_event_with_dims marker = {0};
   nir_builder *b = cs->b;

   marker.event.identifier = RGP_SQTT_MARKER_IDENTIFIER_EVENT;
   marker.event.api_type = event;
   marker.event.has_thread_dims = 1;

   dgc_emit_sqtt_userdata(cs, nir_imm_int(b, marker.event.dword01));
   dgc_emit_sqtt_userdata(cs, nir_imm_int(b, marker.event.dword02));
   dgc_emit_sqtt_userdata(cs, sequence_id);
   dgc_emit_sqtt_userdata(cs, x);
   dgc_emit_sqtt_userdata(cs, y);
   dgc_emit_sqtt_userdata(cs, z);
}

static void
dgc_emit_sqtt_begin_api_marker(struct dgc_cmdbuf *cs, enum rgp_sqtt_marker_general_api_type api_type)
{
   struct rgp_sqtt_marker_general_api marker = {0};
   nir_builder *b = cs->b;

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_GENERAL_API;
   marker.api_type = api_type;

   dgc_emit_sqtt_userdata(cs, nir_imm_int(b, marker.dword01));
}

static void
dgc_emit_sqtt_end_api_marker(struct dgc_cmdbuf *cs, enum rgp_sqtt_marker_general_api_type api_type)
{
   struct rgp_sqtt_marker_general_api marker = {0};
   nir_builder *b = cs->b;

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_GENERAL_API;
   marker.api_type = api_type;
   marker.is_end = 1;

   dgc_emit_sqtt_userdata(cs, nir_imm_int(b, marker.dword01));
}

/**
 * Command buffer
 */
static nir_def *
dgc_cmd_buf_size(nir_builder *b, nir_def *sequence_count, bool is_ace, const struct radv_device *device)
{
   nir_def *cmd_buf_size = is_ace ? load_param32(b, ace_cmd_buf_size) : load_param32(b, cmd_buf_size);
   nir_def *cmd_buf_stride = is_ace ? load_param32(b, ace_cmd_buf_stride) : load_param32(b, cmd_buf_stride);
   const enum amd_ip_type ip_type = is_ace ? AMD_IP_COMPUTE : AMD_IP_GFX;

   nir_def *use_preamble = nir_ine_imm(b, load_param8(b, use_preamble), 0);
   nir_def *size = nir_iadd_imm(b, nir_imul(b, cmd_buf_stride, sequence_count), PKT3_INDIRECT_BUFFER_BYTES);
   unsigned align_mask = radv_pad_cmdbuf(device, 1, ip_type) - 1;

   size = nir_iand_imm(b, nir_iadd_imm(b, size, align_mask), ~align_mask);

   /* Ensure we don't have to deal with a jump to an empty IB in the preamble. */
   size = nir_imax(b, size, nir_imm_int(b, align_mask + 1));

   return nir_bcsel(b, use_preamble, size, cmd_buf_size);
}

static void
build_dgc_buffer_tail(nir_builder *b, nir_def *cmd_buf_offset, nir_def *cmd_buf_size, nir_def *cmd_buf_stride,
                      nir_def *cmd_buf_trailer_offset, nir_def *sequence_count, unsigned trailer_size, bool is_ace,
                      const struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_def *is_compute_queue = nir_ior_imm(b, nir_ieq_imm(b, load_param8(b, queue_family), RADV_QUEUE_COMPUTE), is_ace);

   nir_def *global_id = radv_meta_nir_get_global_ids(b, 1);

   nir_push_if(b, nir_ieq_imm(b, global_id, 0));
   {
      nir_def *cmd_buf_tail_start = nir_imul(b, cmd_buf_stride, sequence_count);

      nir_variable *offset = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "offset");
      nir_store_var(b, offset, cmd_buf_tail_start, 0x1);

      /* On compute queue, the DGC command buffer is chained by patching the
       * trailer but this isn't needed on graphics because it's using IB2.
       */
      cmd_buf_size =
         nir_bcsel(b, is_compute_queue, nir_iadd_imm(b, cmd_buf_size, -PKT3_INDIRECT_BUFFER_BYTES), cmd_buf_size);

      nir_def *va = nir_pack_64_2x32_split(b, load_param32(b, upload_addr), nir_imm_int(b, pdev->info.address32_hi));
      nir_push_loop(b);
      {
         nir_def *curr_offset = nir_load_var(b, offset);
         const unsigned MAX_PACKET_WORDS = 0x3FFC;

         nir_break_if(b, nir_ieq(b, curr_offset, cmd_buf_size));

         nir_def *packet, *packet_size;

         packet_size = nir_isub(b, cmd_buf_size, curr_offset);
         packet_size = nir_umin(b, packet_size, nir_imm_int(b, MAX_PACKET_WORDS * 4));

         nir_def *len = nir_ushr_imm(b, packet_size, 2);
         len = nir_iadd_imm(b, len, -2);
         packet = nir_pkt3(b, PKT3_NOP, len);

         nir_build_store_global(b, packet, nir_iadd(b, va, nir_u2u64(b, nir_iadd(b, curr_offset, cmd_buf_offset))),
                                .access = ACCESS_NON_READABLE);

         nir_store_var(b, offset, nir_iadd(b, curr_offset, packet_size), 0x1);
      }
      nir_pop_loop(b, NULL);

      nir_push_if(b, is_compute_queue);
      {
         nir_def *chain_packets[] = {
            nir_imm_int(b, PKT3(PKT3_INDIRECT_BUFFER, 2, 0)),
            nir_iadd(b, load_param32(b, upload_addr), cmd_buf_trailer_offset),
            nir_imm_int(b, pdev->info.address32_hi),
            nir_imm_int(b, trailer_size | S_3F2_CHAIN(1) | S_3F2_VALID(1) | S_3F2_PRE_ENA(false)),
         };

         nir_build_store_global(b, nir_vec(b, chain_packets, 4),
                                nir_iadd(b, va, nir_u2u64(b, nir_iadd(b, nir_load_var(b, offset), cmd_buf_offset))),
                                .access = ACCESS_NON_READABLE);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static void
build_dgc_buffer_tail_main(nir_builder *b, nir_def *sequence_count, const struct radv_device *device)
{
   nir_def *cmd_buf_offset = load_param32(b, cmd_buf_main_offset);
   nir_def *cmd_buf_size = dgc_cmd_buf_size(b, sequence_count, false, device);
   nir_def *cmd_buf_stride = load_param32(b, cmd_buf_stride);
   nir_def *cmd_buf_trailer_offset = nir_imm_int(b, 0);
   unsigned trailer_size = radv_dgc_trailer_cmdbuf_size(device, AMD_IP_GFX) / 4;

   build_dgc_buffer_tail(b, cmd_buf_offset, cmd_buf_size, cmd_buf_stride, cmd_buf_trailer_offset, sequence_count,
                         trailer_size, false, device);
}

static void
build_dgc_buffer_tail_ace(nir_builder *b, nir_def *sequence_count, const struct radv_device *device)
{
   nir_def *cmd_buf_offset = load_param32(b, ace_cmd_buf_main_offset);
   nir_def *cmd_buf_size = dgc_cmd_buf_size(b, sequence_count, true, device);
   nir_def *cmd_buf_stride = load_param32(b, ace_cmd_buf_stride);
   nir_def *cmd_buf_trailer_offset = load_param32(b, ace_cmd_buf_trailer_offset);
   unsigned trailer_size = radv_dgc_trailer_cmdbuf_size(device, AMD_IP_COMPUTE) / 4;

   build_dgc_buffer_tail(b, cmd_buf_offset, cmd_buf_size, cmd_buf_stride, cmd_buf_trailer_offset, sequence_count,
                         trailer_size, true, device);
}

static void
build_dgc_buffer_trailer(nir_builder *b, nir_def *cmd_buf_offset, unsigned trailer_size,
                         const struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   nir_def *global_id = radv_meta_nir_get_global_ids(b, 1);

   nir_push_if(b, nir_ieq_imm(b, global_id, 0));
   {
      nir_def *va = nir_pack_64_2x32_split(b, load_param32(b, upload_addr), nir_imm_int(b, pdev->info.address32_hi));
      va = nir_iadd(b, va, nir_u2u64(b, cmd_buf_offset));

      const uint32_t pad_size = trailer_size - PKT3_INDIRECT_BUFFER_BYTES;
      const uint32_t pad_size_dw = pad_size >> 2;

      nir_def *len = nir_imm_int(b, pad_size_dw - 2);
      nir_def *packet = nir_pkt3(b, PKT3_NOP, len);

      nir_build_store_global(b, packet, va, .access = ACCESS_NON_READABLE);

      nir_def *nop_packets[] = {
         nir_imm_int(b, PKT3_NOP_PAD),
         nir_imm_int(b, PKT3_NOP_PAD),
         nir_imm_int(b, PKT3_NOP_PAD),
         nir_imm_int(b, PKT3_NOP_PAD),
      };

      nir_build_store_global(b, nir_vec(b, nop_packets, 4), nir_iadd_imm(b, va, pad_size),
                             .access = ACCESS_NON_READABLE);
   }
   nir_pop_if(b, NULL);
}

static void
build_dgc_buffer_trailer_main(nir_builder *b, const struct radv_device *device)
{
   nir_def *cmd_buf_offset = nir_imm_int(b, 0);
   const unsigned trailer_size = radv_dgc_trailer_cmdbuf_size(device, AMD_IP_GFX);

   build_dgc_buffer_trailer(b, cmd_buf_offset, trailer_size, device);
}

static void
build_dgc_buffer_trailer_ace(nir_builder *b, const struct radv_device *device)
{
   nir_def *cmd_buf_offset = load_param32(b, ace_cmd_buf_trailer_offset);
   const unsigned trailer_size = radv_dgc_trailer_cmdbuf_size(device, AMD_IP_COMPUTE);

   build_dgc_buffer_trailer(b, cmd_buf_offset, trailer_size, device);
}

static void
build_dgc_buffer_preamble(nir_builder *b, nir_def *cmd_buf_preamble_offset, nir_def *cmd_buf_size,
                          nir_def *cmd_buf_main_offset, unsigned preamble_size, nir_def *sequence_count,
                          const struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   nir_def *global_id = radv_meta_nir_get_global_ids(b, 1);
   nir_def *use_preamble = nir_ine_imm(b, load_param8(b, use_preamble), 0);

   nir_push_if(b, nir_iand(b, nir_ieq_imm(b, global_id, 0), use_preamble));
   {
      nir_def *va = nir_pack_64_2x32_split(b, load_param32(b, upload_addr), nir_imm_int(b, pdev->info.address32_hi));
      va = nir_iadd(b, va, nir_u2u64(b, cmd_buf_preamble_offset));

      nir_def *words = nir_ushr_imm(b, cmd_buf_size, 2);

      const uint32_t pad_size = preamble_size - PKT3_INDIRECT_BUFFER_BYTES;
      const uint32_t pad_size_dw = pad_size >> 2;

      nir_def *len = nir_imm_int(b, pad_size_dw - 2);
      nir_def *packet = nir_pkt3(b, PKT3_NOP, len);

      nir_build_store_global(b, packet, va, .access = ACCESS_NON_READABLE);

      nir_def *chain_packets[] = {
         nir_imm_int(b, PKT3(PKT3_INDIRECT_BUFFER, 2, 0)),
         nir_iadd(b, cmd_buf_main_offset, load_param32(b, upload_addr)),
         nir_imm_int(b, pdev->info.address32_hi),
         nir_ior_imm(b, words, S_3F2_CHAIN(1) | S_3F2_VALID(1) | S_3F2_PRE_ENA(false)),
      };

      nir_build_store_global(b, nir_vec(b, chain_packets, 4), nir_iadd_imm(b, va, pad_size),
                             .access = ACCESS_NON_READABLE);
   }
   nir_pop_if(b, NULL);
}

static void
build_dgc_buffer_preamble_main(nir_builder *b, nir_def *sequence_count, const struct radv_device *device)
{
   nir_def *cmd_buf_preamble_offset = load_param32(b, cmd_buf_preamble_offset);
   nir_def *cmd_buf_main_offset = load_param32(b, cmd_buf_main_offset);
   nir_def *cmd_buf_size = dgc_cmd_buf_size(b, sequence_count, false, device);
   unsigned preamble_size = radv_dgc_preamble_cmdbuf_size(device, AMD_IP_GFX);

   build_dgc_buffer_preamble(b, cmd_buf_preamble_offset, cmd_buf_size, cmd_buf_main_offset, preamble_size,
                             sequence_count, device);
}

static void
build_dgc_buffer_preamble_ace(nir_builder *b, nir_def *sequence_count, const struct radv_device *device)
{
   nir_def *cmd_buf_preamble_offset = load_param32(b, ace_cmd_buf_preamble_offset);
   nir_def *cmd_buf_main_offset = load_param32(b, ace_cmd_buf_main_offset);
   nir_def *cmd_buf_size = dgc_cmd_buf_size(b, sequence_count, true, device);
   unsigned preamble_size = radv_dgc_preamble_cmdbuf_size(device, AMD_IP_COMPUTE);

   build_dgc_buffer_preamble(b, cmd_buf_preamble_offset, cmd_buf_size, cmd_buf_main_offset, preamble_size,
                             sequence_count, device);
}

/**
 * Draw
 */
static void
dgc_emit_userdata_vertex(struct dgc_cmdbuf *cs, nir_def *first_vertex, nir_def *first_instance, nir_def *drawid)
{
   nir_builder *b = cs->b;

   nir_def *vtx_base_sgpr = load_param16(b, vtx_base_sgpr);
   vtx_base_sgpr = nir_u2u32(b, vtx_base_sgpr);

   nir_def *has_drawid = nir_test_mask(b, vtx_base_sgpr, DGC_USES_DRAWID);
   nir_def *has_baseinstance = nir_test_mask(b, vtx_base_sgpr, DGC_USES_BASEINSTANCE);

   nir_def *pkt_cnt = nir_imm_int(b, 1);
   pkt_cnt = nir_bcsel(b, has_drawid, nir_iadd_imm(b, pkt_cnt, 1), pkt_cnt);
   pkt_cnt = nir_bcsel(b, has_baseinstance, nir_iadd_imm(b, pkt_cnt, 1), pkt_cnt);

   dgc_cs_begin(cs);
   dgc_cs_emit(nir_pkt3(b, PKT3_SET_SH_REG, pkt_cnt));
   dgc_cs_emit(nir_iand_imm(b, vtx_base_sgpr, 0x3FFF));
   dgc_cs_emit(first_vertex);
   dgc_cs_emit(nir_bcsel(b, nir_ior(b, has_drawid, has_baseinstance), nir_bcsel(b, has_drawid, drawid, first_instance),
                         nir_imm_int(b, PKT3_NOP_PAD)));
   dgc_cs_emit(nir_bcsel(b, nir_iand(b, has_drawid, has_baseinstance), first_instance, nir_imm_int(b, PKT3_NOP_PAD)));
   dgc_cs_end();
}

static void
dgc_emit_instance_count(struct dgc_cmdbuf *cs, nir_def *instance_count)
{
   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_NUM_INSTANCES, 0, 0));
   dgc_cs_emit(instance_count);
   dgc_cs_end();
}

static void
dgc_emit_draw_index_offset_2(struct dgc_cmdbuf *cs, nir_def *index_offset, nir_def *index_count,
                             nir_def *max_index_count)
{
   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_DRAW_INDEX_OFFSET_2, 3, 0));
   dgc_cs_emit(max_index_count);
   dgc_cs_emit(index_offset);
   dgc_cs_emit(index_count);
   dgc_cs_emit_imm(V_0287F0_DI_SRC_SEL_DMA);
   dgc_cs_end();
}

static void
dgc_emit_draw_index_auto(struct dgc_cmdbuf *cs, nir_def *vertex_count)
{
   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_DRAW_INDEX_AUTO, 1, 0));
   dgc_cs_emit(vertex_count);
   dgc_cs_emit_imm(V_0287F0_DI_SRC_SEL_AUTO_INDEX);
   dgc_cs_end();
}

static void
dgc_emit_pkt3_set_base(struct dgc_cmdbuf *cs, nir_def *va)
{
   nir_builder *b = cs->b;

   nir_def *va_lo = nir_unpack_64_2x32_split_x(b, va);
   nir_def *va_hi = nir_unpack_64_2x32_split_y(b, va);

   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_SET_BASE, 2, 0));
   dgc_cs_emit_imm(1);
   dgc_cs_emit(va_lo);
   dgc_cs_emit(va_hi);
   dgc_cs_end();
}

static void
dgc_emit_pkt3_draw_indirect(struct dgc_cmdbuf *cs, bool indexed)
{
   const unsigned di_src_sel = indexed ? V_0287F0_DI_SRC_SEL_DMA : V_0287F0_DI_SRC_SEL_AUTO_INDEX;
   nir_builder *b = cs->b;

   nir_def *vtx_base_sgpr = load_param16(b, vtx_base_sgpr);

   nir_def *has_drawid = nir_test_mask(b, vtx_base_sgpr, DGC_USES_DRAWID);
   nir_def *has_baseinstance = nir_test_mask(b, vtx_base_sgpr, DGC_USES_BASEINSTANCE);

   vtx_base_sgpr = nir_iand_imm(b, nir_u2u32(b, vtx_base_sgpr), 0x3FFF);

   /* vertex_offset_reg = (base_reg - SI_SH_REG_OFFSET) >> 2 */
   nir_def *vertex_offset_reg = vtx_base_sgpr;

   /* start_instance_reg = (base_reg + (draw_id_enable ? 8 : 4) - SI_SH_REG_OFFSET) >> 2 */
   nir_def *start_instance_offset = nir_bcsel(b, has_drawid, nir_imm_int(b, 2), nir_imm_int(b, 1));
   nir_def *start_instance_reg = nir_iadd(b, vtx_base_sgpr, start_instance_offset);

   /* draw_id_reg = (base_reg + 4 - SI_SH_REG_OFFSET) >> 2 */
   nir_def *draw_id_reg = nir_iadd(b, vtx_base_sgpr, nir_imm_int(b, 1));

   nir_if *if_drawid = nir_push_if(b, has_drawid);
   {
      const unsigned pkt3_op = indexed ? PKT3_DRAW_INDEX_INDIRECT_MULTI : PKT3_DRAW_INDIRECT_MULTI;

      dgc_cs_begin(cs);
      dgc_cs_emit_imm(PKT3(pkt3_op, 8, 0));
      dgc_cs_emit_imm(0);
      dgc_cs_emit(vertex_offset_reg);
      dgc_cs_emit(nir_bcsel(b, has_baseinstance, start_instance_reg, nir_imm_int(b, 0)));
      dgc_cs_emit(nir_ior(b, draw_id_reg, nir_imm_int(b, S_2C3_DRAW_INDEX_ENABLE(1))));
      dgc_cs_emit_imm(1); /* draw count */
      dgc_cs_emit_imm(0); /* count va low */
      dgc_cs_emit_imm(0); /* count va high */
      dgc_cs_emit_imm(0); /* stride */
      dgc_cs_emit_imm(di_src_sel);
      dgc_cs_end();
   }
   nir_push_else(b, if_drawid);
   {
      const unsigned pkt3_op = indexed ? PKT3_DRAW_INDEX_INDIRECT : PKT3_DRAW_INDIRECT;

      dgc_cs_begin(cs);
      dgc_cs_emit_imm(PKT3(pkt3_op, 3, 0));
      dgc_cs_emit_imm(0);
      dgc_cs_emit(vertex_offset_reg);
      dgc_cs_emit(nir_bcsel(b, has_baseinstance, start_instance_reg, nir_imm_int(b, 0)));
      dgc_cs_emit_imm(di_src_sel);
      dgc_cs_end();
   }
   nir_pop_if(b, if_drawid);
}

static void
dgc_emit_draw_indirect(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *sequence_id, bool indexed)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   nir_builder *b = cs->b;

   nir_def *va = nir_iadd_imm(b, stream_addr, layout->vk.draw_src_offset_B);

   dgc_emit_sqtt_begin_api_marker(cs, indexed ? ApiCmdDrawIndexedIndirect : ApiCmdDrawIndirect);
   dgc_emit_sqtt_marker_event(cs, sequence_id, indexed ? EventCmdDrawIndexedIndirect : EventCmdDrawIndirect);

   dgc_emit_pkt3_set_base(cs, va);
   dgc_emit_pkt3_draw_indirect(cs, indexed);

   dgc_emit_sqtt_thread_trace_marker(cs);
   dgc_emit_sqtt_end_api_marker(cs, indexed ? ApiCmdDrawIndexedIndirect : ApiCmdDrawIndirect);
}

static void
dgc_emit_draw(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *sequence_id)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   nir_builder *b = cs->b;

   nir_def *draw_data0 = nir_build_load_global(b, 4, 32, nir_iadd_imm(b, stream_addr, layout->vk.draw_src_offset_B),
                                               .access = ACCESS_NON_WRITEABLE);
   nir_def *vertex_count = nir_channel(b, draw_data0, 0);
   nir_def *instance_count = nir_channel(b, draw_data0, 1);
   nir_def *vertex_offset = nir_channel(b, draw_data0, 2);
   nir_def *first_instance = nir_channel(b, draw_data0, 3);

   nir_push_if(b, nir_iand(b, nir_ine_imm(b, vertex_count, 0), nir_ine_imm(b, instance_count, 0)));
   {
      dgc_emit_sqtt_begin_api_marker(cs, ApiCmdDraw);
      dgc_emit_sqtt_marker_event(cs, sequence_id, EventCmdDraw);

      dgc_emit_userdata_vertex(cs, vertex_offset, first_instance, nir_imm_int(b, 0));
      dgc_emit_instance_count(cs, instance_count);
      dgc_emit_draw_index_auto(cs, vertex_count);

      dgc_emit_sqtt_thread_trace_marker(cs);
      dgc_emit_sqtt_end_api_marker(cs, ApiCmdDraw);
   }
   nir_pop_if(b, 0);
}

static void
dgc_emit_draw_indexed(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *sequence_id, nir_def *max_index_count)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   nir_builder *b = cs->b;

   nir_def *draw_data0 = nir_build_load_global(b, 4, 32, nir_iadd_imm(b, stream_addr, layout->vk.draw_src_offset_B),
                                               .access = ACCESS_NON_WRITEABLE);
   nir_def *draw_data1 =
      nir_build_load_global(b, 1, 32, nir_iadd_imm(b, nir_iadd_imm(b, stream_addr, layout->vk.draw_src_offset_B), 16),
                            .access = ACCESS_NON_WRITEABLE);
   nir_def *index_count = nir_channel(b, draw_data0, 0);
   nir_def *instance_count = nir_channel(b, draw_data0, 1);
   nir_def *first_index = nir_channel(b, draw_data0, 2);
   nir_def *vertex_offset = nir_channel(b, draw_data0, 3);
   nir_def *first_instance = nir_channel(b, draw_data1, 0);

   nir_push_if(b, nir_iand(b, nir_ine_imm(b, index_count, 0), nir_ine_imm(b, instance_count, 0)));
   {
      dgc_emit_sqtt_begin_api_marker(cs, ApiCmdDrawIndexed);
      dgc_emit_sqtt_marker_event(cs, sequence_id, EventCmdDrawIndexed);

      dgc_emit_userdata_vertex(cs, vertex_offset, first_instance, nir_imm_int(b, 0));
      dgc_emit_instance_count(cs, instance_count);
      dgc_emit_draw_index_offset_2(cs, first_index, index_count, max_index_count);

      dgc_emit_sqtt_thread_trace_marker(cs);
      dgc_emit_sqtt_end_api_marker(cs, ApiCmdDrawIndexed);
   }
   nir_pop_if(b, 0);
}

static void
dgc_emit_draw_with_count(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *sequence_id, bool indexed)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   nir_builder *b = cs->b;

   nir_def *vtx_base_sgpr = load_param16(b, vtx_base_sgpr);
   nir_def *has_drawid = nir_test_mask(b, vtx_base_sgpr, DGC_USES_DRAWID);
   nir_def *has_baseinstance = nir_test_mask(b, vtx_base_sgpr, DGC_USES_BASEINSTANCE);

   nir_def *draw_data = nir_build_load_global(b, 4, 32, nir_iadd_imm(b, stream_addr, layout->vk.draw_src_offset_B),
                                              .access = ACCESS_NON_WRITEABLE);
   nir_def *va = nir_pack_64_2x32(b, nir_channels(b, draw_data, 0x3));
   nir_def *stride = nir_channel(b, draw_data, 2);
   nir_def *draw_count = nir_umin(b, load_param32(b, max_draw_count), nir_channel(b, draw_data, 3));

   dgc_emit_pkt3_set_base(cs, va);

   nir_def *vertex_offset_reg = nir_iand_imm(b, vtx_base_sgpr, 0x3FFF);
   nir_def *start_instance_offset = nir_bcsel(b, has_drawid, nir_imm_int(b, 2), nir_imm_int(b, 1));
   nir_def *start_instance_reg =
      nir_bcsel(b, has_baseinstance, nir_iadd(b, vertex_offset_reg, start_instance_offset), nir_imm_int(b, 0));
   nir_def *draw_id_reg = nir_bcsel(
      b, has_drawid, nir_ior_imm(b, nir_iadd(b, vertex_offset_reg, nir_imm_int(b, 1)), S_2C3_DRAW_INDEX_ENABLE(1)),
      nir_imm_int(b, 0));

   nir_def *di_src_sel = nir_imm_int(b, indexed ? V_0287F0_DI_SRC_SEL_DMA : V_0287F0_DI_SRC_SEL_AUTO_INDEX);

   dgc_emit_sqtt_begin_api_marker(cs, indexed ? ApiCmdDrawIndexedIndirectCount : ApiCmdDrawIndirectCount);
   dgc_emit_sqtt_marker_event(cs, sequence_id, indexed ? EventCmdDrawIndexedIndirectCount : EventCmdDrawIndirectCount);

   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(indexed ? PKT3_DRAW_INDEX_INDIRECT_MULTI : PKT3_DRAW_INDIRECT_MULTI, 8, false));
   dgc_cs_emit_imm(0);
   dgc_cs_emit(vertex_offset_reg);
   dgc_cs_emit(start_instance_reg);
   dgc_cs_emit(draw_id_reg);
   dgc_cs_emit(draw_count);
   dgc_cs_emit_imm(0);
   dgc_cs_emit_imm(0);
   dgc_cs_emit(stride);
   dgc_cs_emit(di_src_sel);
   dgc_cs_end();

   dgc_emit_sqtt_thread_trace_marker(cs);
   dgc_emit_sqtt_end_api_marker(cs, indexed ? ApiCmdDrawIndexedIndirectCount : ApiCmdDrawIndirectCount);
}

/**
 * Index buffer
 */
static nir_def *
dgc_get_index_type(struct dgc_cmdbuf *cs, nir_def *user_index_type)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   nir_builder *b = cs->b;

   if (layout->vk.index_mode_is_dx) {
      nir_def *index_type = nir_bcsel(b, nir_ieq_imm(b, user_index_type, 0x2a /* DXGI_FORMAT_R32_UINT */),
                                      nir_imm_int(b, V_028A7C_VGT_INDEX_32), nir_imm_int(b, V_028A7C_VGT_INDEX_16));
      return nir_bcsel(b, nir_ieq_imm(b, user_index_type, 0x3e /* DXGI_FORMAT_R8_UINT */),
                       nir_imm_int(b, V_028A7C_VGT_INDEX_8), index_type);
   } else {
      nir_def *index_type = nir_bcsel(b, nir_ieq_imm(b, user_index_type, VK_INDEX_TYPE_UINT32),
                                      nir_imm_int(b, V_028A7C_VGT_INDEX_32), nir_imm_int(b, V_028A7C_VGT_INDEX_16));
      return nir_bcsel(b, nir_ieq_imm(b, user_index_type, VK_INDEX_TYPE_UINT8), nir_imm_int(b, V_028A7C_VGT_INDEX_8),
                       index_type);
   }
}

static void
dgc_emit_index_buffer(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_variable *max_index_count_var)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   const struct radv_device *device = cs->dev;
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_builder *b = cs->b;

   nir_def *data = nir_build_load_global(b, 4, 32, nir_iadd_imm(b, stream_addr, layout->vk.index_src_offset_B),
                                         .access = ACCESS_NON_WRITEABLE);

   nir_def *index_type = dgc_get_index_type(cs, nir_channel(b, data, 3));
   nir_def *index_size = nir_iand_imm(b, nir_ushr(b, nir_imm_int(b, 0x142), nir_imul_imm(b, index_type, 4)), 0xf);

   nir_def *max_index_count = nir_udiv(b, nir_channel(b, data, 2), index_size);
   nir_store_var(b, max_index_count_var, max_index_count, 0x1);

   nir_def *addr_upper = nir_channel(b, data, 1);
   addr_upper = nir_ishr_imm(b, nir_ishl_imm(b, addr_upper, 16), 16);

   dgc_cs_begin(cs);

   if (pdev->info.gfx_level >= GFX9) {
      unsigned opcode = PKT3_SET_UCONFIG_REG_INDEX;
      if (pdev->info.gfx_level < GFX9 || (pdev->info.gfx_level == GFX9 && pdev->info.me_fw_version < 26))
         opcode = PKT3_SET_UCONFIG_REG;
      dgc_cs_emit_imm(PKT3(opcode, 1, 0));
      dgc_cs_emit_imm((R_03090C_VGT_INDEX_TYPE - CIK_UCONFIG_REG_OFFSET) >> 2 | (2u << 28));
      dgc_cs_emit(index_type);
   } else {
      dgc_cs_emit_imm(PKT3(PKT3_INDEX_TYPE, 0, 0));
      dgc_cs_emit(index_type);
      dgc_cs_emit(nir_imm_int(b, PKT3_NOP_PAD));
   }

   dgc_cs_emit_imm(PKT3(PKT3_INDEX_BASE, 1, 0));
   dgc_cs_emit(nir_channel(b, data, 0));
   dgc_cs_emit(addr_upper);

   dgc_cs_emit_imm(PKT3(PKT3_INDEX_BUFFER_SIZE, 0, 0));
   dgc_cs_emit(max_index_count);

   dgc_cs_end();
}

/**
 * Push constants
 */
static nir_def *
dgc_get_push_constant_stages(struct dgc_cmdbuf *cs)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   nir_builder *b = cs->b;

   if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DISPATCH)) {
      nir_def *has_push_constant = nir_ine_imm(b, load_shader_metadata32(cs, push_const_sgpr), 0);
      return nir_bcsel(b, has_push_constant, nir_imm_int(b, VK_SHADER_STAGE_COMPUTE_BIT), nir_imm_int(b, 0));
   } else {
      return load_param16(b, push_constant_stages);
   }
}

static nir_def *
dgc_get_upload_sgpr(struct dgc_cmdbuf *cs, nir_def *param_offset, gl_shader_stage stage)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   nir_builder *b = cs->b;
   nir_def *res;

   if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DISPATCH)) {
      res = load_shader_metadata32(cs, push_const_sgpr);
   } else {
      nir_def *va = load_param64(b, params_addr);
      res = nir_build_load_global(b, 1, 32, nir_iadd(b, va, nir_u2u64(b, nir_iadd_imm(b, param_offset, stage * 12))));
   }

   return nir_ubfe_imm(b, res, 0, 16);
}

static nir_def *
dgc_get_inline_sgpr(struct dgc_cmdbuf *cs, nir_def *param_offset, gl_shader_stage stage)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   nir_builder *b = cs->b;
   nir_def *res;

   if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DISPATCH)) {
      res = load_shader_metadata32(cs, push_const_sgpr);
   } else {
      nir_def *va = load_param64(b, params_addr);
      res = nir_build_load_global(b, 1, 32, nir_iadd(b, va, nir_u2u64(b, nir_iadd_imm(b, param_offset, stage * 12))));
   }

   return nir_ubfe_imm(b, res, 16, 16);
}

static nir_def *
dgc_get_inline_mask(struct dgc_cmdbuf *cs, nir_def *param_offset, gl_shader_stage stage)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   nir_builder *b = cs->b;

   if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DISPATCH)) {
      return load_shader_metadata64(cs, inline_push_const_mask);
   } else {
      nir_def *va = load_param64(b, params_addr);
      nir_def *reg_info =
         nir_build_load_global(b, 2, 32, nir_iadd(b, va, nir_u2u64(b, nir_iadd_imm(b, param_offset, stage * 12 + 4))));
      return nir_pack_64_2x32(b, nir_channels(b, reg_info, 0x3));
   }
}

static nir_def *
dgc_push_constant_needs_copy(struct dgc_cmdbuf *cs)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   nir_builder *b = cs->b;

   if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DISPATCH)) {
      return nir_ine_imm(b, nir_ubfe_imm(b, load_shader_metadata32(cs, push_const_sgpr), 0, 16), 0);
   } else {
      return nir_ine_imm(b, load_param8(b, const_copy), 0);
   }
}

struct dgc_pc_params {
   nir_def *offset;
   nir_def *const_offset;
};

static struct dgc_pc_params
dgc_get_pc_params(struct dgc_cmdbuf *cs)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   struct dgc_pc_params params = {0};
   nir_builder *b = cs->b;

   uint32_t offset = 0;
   if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DISPATCH)) {
      offset =
         (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES)) ? 0 : sizeof(struct radv_compute_pipeline_metadata);
   } else {
      if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_VB))
         offset = MAX_VBS * DGC_VBO_INFO_SIZE;
   }

   params.offset = nir_imm_int(b, offset);
   params.const_offset = nir_iadd_imm(b, params.offset, MESA_VULKAN_SHADER_STAGES * 12);

   return params;
}

static void
dgc_alloc_push_constant(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *sequence_id,
                        const struct dgc_pc_params *params)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   VK_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, layout->vk.layout);
   nir_builder *b = cs->b;

   for (uint32_t i = 0; i < pipeline_layout->push_constant_size / 4; i++) {
      nir_def *data;

      if (layout->sequence_index_mask & (1ull << i)) {
         data = sequence_id;
      } else if ((layout->push_constant_mask & (1ull << i))) {
         data = nir_build_load_global(b, 1, 32, nir_iadd_imm(b, stream_addr, layout->push_constant_offsets[i]),
                                      .access = ACCESS_NON_WRITEABLE);
      } else {
         nir_def *va = load_param64(b, params_addr);
         data = nir_build_load_global(b, 1, 32,
                                      nir_iadd(b, va, nir_u2u64(b, nir_iadd_imm(b, params->const_offset, i * 4))));
      }

      dgc_upload(cs, data);
   }
}

static void
dgc_emit_push_constant_for_stage(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *sequence_id,
                                 const struct dgc_pc_params *params, gl_shader_stage stage)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   VK_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, layout->vk.layout);
   nir_builder *b = cs->b;

   nir_def *upload_sgpr = dgc_get_upload_sgpr(cs, params->offset, stage);
   nir_def *inline_sgpr = dgc_get_inline_sgpr(cs, params->offset, stage);
   nir_def *inline_mask = dgc_get_inline_mask(cs, params->offset, stage);

   nir_push_if(b, nir_ine_imm(b, upload_sgpr, 0));
   {
      dgc_cs_begin(cs);
      dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 1, 0));
      dgc_cs_emit(upload_sgpr);
      dgc_cs_emit(nir_iadd(b, load_param32(b, upload_addr), nir_load_var(b, cs->upload_offset)));
      dgc_cs_end();
   }
   nir_pop_if(b, NULL);

   nir_push_if(b, nir_ine_imm(b, inline_sgpr, 0));
   {
      nir_variable *pc_idx = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "pc_idx");
      nir_store_var(b, pc_idx, nir_imm_int(b, 0), 0x1);

      for (uint32_t i = 0; i < pipeline_layout->push_constant_size / 4; i++) {
         nir_push_if(b, nir_ine_imm(b, nir_iand_imm(b, inline_mask, 1ull << i), 0));
         {
            nir_def *data = NULL;

            if (layout->sequence_index_mask & (1ull << i)) {
               data = sequence_id;
            } else if (layout->push_constant_mask & (1ull << i)) {
               data = nir_build_load_global(b, 1, 32, nir_iadd_imm(b, stream_addr, layout->push_constant_offsets[i]),
                                            .access = ACCESS_NON_WRITEABLE);
            } else if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES)) {
               /* For indirect pipeline binds, partial push constant updates can't be emitted when
                * the DGC execute is called because there is no bound pipeline and they have to be
                * emitted from the DGC prepare shader.
                */
               nir_def *va = load_param64(b, params_addr);
               data = nir_build_load_global(
                  b, 1, 32, nir_iadd(b, va, nir_u2u64(b, nir_iadd_imm(b, params->const_offset, i * 4))));
            }

            if (data) {
               dgc_cs_begin(cs);
               dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 1, 0));
               dgc_cs_emit(nir_iadd(b, inline_sgpr, nir_load_var(b, pc_idx)));
               dgc_cs_emit(data);
               dgc_cs_end();
            }

            nir_store_var(b, pc_idx, nir_iadd_imm(b, nir_load_var(b, pc_idx), 1), 0x1);
         }
         nir_pop_if(b, NULL);
      }
   }
   nir_pop_if(b, NULL);
}

static void
dgc_emit_push_constant(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *sequence_id, VkShaderStageFlags stages)
{
   const struct dgc_pc_params params = dgc_get_pc_params(cs);
   nir_builder *b = cs->b;

   nir_def *push_constant_stages = dgc_get_push_constant_stages(cs);
   radv_foreach_stage(s, stages)
   {
      nir_push_if(b, nir_test_mask(b, push_constant_stages, mesa_to_vk_shader_stage(s)));
      {
         dgc_emit_push_constant_for_stage(cs, stream_addr, sequence_id, &params, s);
      }
      nir_pop_if(b, NULL);
   }

   nir_def *const_copy = dgc_push_constant_needs_copy(cs);
   nir_push_if(b, const_copy);
   {
      dgc_alloc_push_constant(cs, stream_addr, sequence_id, &params);
   }
   nir_pop_if(b, NULL);
}

/**
 * Vertex buffers
 */
struct dgc_vbo_info {
   nir_def *va;
   nir_def *size;
   nir_def *stride;

   nir_def *attrib_end;
   nir_def *attrib_index_offset;

   nir_def *non_trivial_format;
};

static nir_def *
dgc_get_rsrc3_vbo_desc(struct dgc_cmdbuf *cs, const struct dgc_vbo_info *vbo_info)
{
   const struct radv_device *device = cs->dev;
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_builder *b = cs->b;

   uint32_t rsrc_word3 = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                         S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

   if (pdev->info.gfx_level >= GFX10) {
      rsrc_word3 |= S_008F0C_FORMAT_GFX10(V_008F0C_GFX10_FORMAT_32_UINT);
   } else {
      rsrc_word3 |=
         S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_UINT) | S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
   }

   nir_def *uses_dynamic_inputs = nir_ieq_imm(b, load_param8(b, dynamic_vs_input), 1);
   nir_def *uses_non_trivial_format = nir_iand(b, uses_dynamic_inputs, nir_ine_imm(b, vbo_info->non_trivial_format, 0));

   return nir_bcsel(b, uses_non_trivial_format, vbo_info->non_trivial_format, nir_imm_int(b, rsrc_word3));
}

static void
dgc_write_vertex_descriptor(struct dgc_cmdbuf *cs, const struct dgc_vbo_info *vbo_info, nir_variable *desc)
{
   const struct radv_device *device = cs->dev;
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_builder *b = cs->b;

   nir_variable *num_records = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "num_records");
   nir_store_var(b, num_records, vbo_info->size, 0x1);

   nir_def *use_per_attribute_vb_descs = nir_ieq_imm(b, load_param8(b, use_per_attribute_vb_descs), 1);
   nir_push_if(b, use_per_attribute_vb_descs);
   {
      nir_push_if(b, nir_ult(b, nir_load_var(b, num_records), vbo_info->attrib_end));
      {
         nir_store_var(b, num_records, nir_imm_int(b, 0), 0x1);
      }
      nir_push_else(b, NULL);
      nir_push_if(b, nir_ieq_imm(b, vbo_info->stride, 0));
      {
         nir_store_var(b, num_records, nir_imm_int(b, 1), 0x1);
      }
      nir_push_else(b, NULL);
      {
         nir_def *r = nir_iadd(
            b,
            nir_iadd_imm(
               b, nir_udiv(b, nir_isub(b, nir_load_var(b, num_records), vbo_info->attrib_end), vbo_info->stride), 1),
            vbo_info->attrib_index_offset);
         nir_store_var(b, num_records, r, 0x1);
      }
      nir_pop_if(b, NULL);
      nir_pop_if(b, NULL);

      nir_def *convert_cond = nir_ine_imm(b, nir_load_var(b, num_records), 0);
      if (pdev->info.gfx_level == GFX9)
         convert_cond = nir_imm_false(b);
      else if (pdev->info.gfx_level != GFX8)
         convert_cond = nir_iand(b, convert_cond, nir_ieq_imm(b, vbo_info->stride, 0));

      nir_def *new_records = nir_iadd(
         b, nir_imul(b, nir_iadd_imm(b, nir_load_var(b, num_records), -1), vbo_info->stride), vbo_info->attrib_end);
      new_records = nir_bcsel(b, convert_cond, new_records, nir_load_var(b, num_records));
      nir_store_var(b, num_records, new_records, 0x1);
   }
   nir_push_else(b, NULL);
   {
      if (pdev->info.gfx_level != GFX8) {
         nir_push_if(b, nir_ine_imm(b, vbo_info->stride, 0));
         {
            nir_def *r = nir_iadd(b, nir_load_var(b, num_records), nir_iadd_imm(b, vbo_info->stride, -1));
            nir_store_var(b, num_records, nir_udiv(b, r, vbo_info->stride), 0x1);
         }
         nir_pop_if(b, NULL);
      }
   }
   nir_pop_if(b, NULL);

   nir_def *rsrc_word3 = dgc_get_rsrc3_vbo_desc(cs, vbo_info);
   if (pdev->info.gfx_level >= GFX10) {
      nir_def *oob_select = nir_bcsel(b, nir_ieq_imm(b, vbo_info->stride, 0), nir_imm_int(b, V_008F0C_OOB_SELECT_RAW),
                                      nir_imm_int(b, V_008F0C_OOB_SELECT_STRUCTURED));
      rsrc_word3 = nir_iand_imm(b, rsrc_word3, C_008F0C_OOB_SELECT);
      rsrc_word3 = nir_ior(b, rsrc_word3, nir_ishl_imm(b, oob_select, 28));
   }

   nir_def *va_hi = nir_iand_imm(b, nir_unpack_64_2x32_split_y(b, vbo_info->va), 0xFFFF);
   nir_def *stride = nir_iand_imm(b, vbo_info->stride, 0x3FFF);
   nir_def *new_vbo_data[4] = {nir_unpack_64_2x32_split_x(b, vbo_info->va),
                               nir_ior(b, nir_ishl_imm(b, stride, 16), va_hi), nir_load_var(b, num_records),
                               rsrc_word3};
   nir_store_var(b, desc, nir_vec(b, new_vbo_data, 4), 0xf);

   /* On GFX9, it seems bounds checking is disabled if both
    * num_records and stride are zero. This doesn't seem necessary on GFX8, GFX10 and
    * GFX10.3 but it doesn't hurt.
    */
   nir_def *buf_va =
      nir_iand_imm(b, nir_pack_64_2x32(b, nir_trim_vector(b, nir_load_var(b, desc), 2)), (1ull << 48) - 1ull);
   nir_push_if(b, nir_ior(b, nir_ieq_imm(b, nir_load_var(b, num_records), 0), nir_ieq_imm(b, buf_va, 0)));
   {
      nir_def *has_dynamic_vs_input = nir_ieq_imm(b, load_param8(b, dynamic_vs_input), 1);

      new_vbo_data[0] = nir_imm_int(b, 0);
      new_vbo_data[1] = nir_bcsel(b, has_dynamic_vs_input, nir_imm_int(b, S_008F04_STRIDE(16)), nir_imm_int(b, 0));
      new_vbo_data[2] = nir_imm_int(b, 0);
      new_vbo_data[3] = nir_bcsel(b, has_dynamic_vs_input, nir_channel(b, nir_load_var(b, desc), 3), nir_imm_int(b, 0));

      nir_store_var(b, desc, nir_vec(b, new_vbo_data, 4), 0xf);
   }
   nir_pop_if(b, NULL);
}

static void
dgc_emit_vertex_buffer(struct dgc_cmdbuf *cs, nir_def *stream_addr)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   nir_builder *b = cs->b;

   nir_def *vb_desc_usage_mask = load_param32(b, vb_desc_usage_mask);
   nir_def *vbo_cnt = nir_bit_count(b, vb_desc_usage_mask);

   nir_push_if(b, nir_ine_imm(b, vbo_cnt, 0));
   {
      dgc_cs_begin(cs);
      dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 1, 0));
      dgc_cs_emit(load_param16(b, vbo_reg));
      dgc_cs_emit(nir_iadd(b, load_param32(b, upload_addr), nir_load_var(b, cs->upload_offset)));
      dgc_cs_end();
   }
   nir_pop_if(b, NULL);

   nir_variable *vbo_idx = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "vbo_idx");
   nir_store_var(b, vbo_idx, nir_imm_int(b, 0), 0x1);

   nir_push_loop(b);
   {
      nir_def *cur_idx = nir_load_var(b, vbo_idx);

      nir_break_if(b, nir_uge_imm(b, cur_idx, 32 /* bits in vb_desc_usage_mask */));

      nir_def *l = nir_ishl(b, nir_imm_int(b, 1), cur_idx);
      nir_push_if(b, nir_ieq_imm(b, nir_iand(b, l, vb_desc_usage_mask), 0));
      {
         nir_store_var(b, vbo_idx, nir_iadd_imm(b, cur_idx, 1), 0x1);
         nir_jump(b, nir_jump_continue);
      }
      nir_pop_if(b, NULL);

      nir_variable *va_var = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint64_t_type(), "va_var");
      nir_variable *size_var = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "size_var");
      nir_variable *stride_var = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "stride_var");

      nir_def *binding = load_vbo_metadata32(cs, cur_idx, binding);

      nir_def *vbo_override = nir_ine_imm(
         b, nir_iand(b, nir_imm_int(b, layout->vk.vertex_bindings), nir_ishl(b, nir_imm_int(b, 1), binding)), 0);
      nir_push_if(b, vbo_override);
      {
         nir_def *stream_offset = load_vbo_offset(cs, cur_idx);
         nir_def *stream_data = nir_build_load_global(b, 4, 32, nir_iadd(b, stream_addr, nir_u2u64(b, stream_offset)),
                                                      .access = ACCESS_NON_WRITEABLE);

         nir_def *va = nir_pack_64_2x32(b, nir_trim_vector(b, stream_data, 2));
         nir_def *size = nir_channel(b, stream_data, 2);

         nir_def *stride = nir_channel(b, stream_data, 3);

         nir_store_var(b, va_var, va, 0x1);
         nir_store_var(b, size_var, size, 0x1);
         nir_store_var(b, stride_var, stride, 0x1);
      }
      nir_push_else(b, NULL);
      {
         nir_store_var(b, va_var, load_vbo_metadata64(cs, cur_idx, va), 0x1);
         nir_store_var(b, size_var, load_vbo_metadata32(cs, cur_idx, size), 0x1);
         nir_store_var(b, stride_var, load_vbo_metadata32(cs, cur_idx, stride), 0x1);
      }
      nir_pop_if(b, NULL);

      nir_def *attrib_index_offset = load_vbo_metadata32(cs, cur_idx, attrib_index_offset);
      nir_def *non_trivial_format = load_vbo_metadata32(cs, cur_idx, non_trivial_format);
      nir_def *attrib_offset = load_vbo_metadata32(cs, cur_idx, attrib_offset);
      nir_def *attrib_format_size = load_vbo_metadata32(cs, cur_idx, attrib_format_size);
      nir_def *attrib_end = nir_iadd(b, attrib_offset, attrib_format_size);

      nir_def *has_dynamic_vs_input = nir_ieq_imm(b, load_param8(b, dynamic_vs_input), 1);
      nir_def *va = nir_iadd(b, nir_load_var(b, va_var),
                             nir_bcsel(b, has_dynamic_vs_input, nir_u2u64(b, attrib_offset), nir_imm_int64(b, 0)));

      struct dgc_vbo_info vbo_info = {
         .va = va,
         .size = nir_load_var(b, size_var),
         .stride = nir_load_var(b, stride_var),
         .attrib_end = attrib_end,
         .attrib_index_offset = attrib_index_offset,
         .non_trivial_format = non_trivial_format,
      };

      nir_variable *vbo_data = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uvec4_type(), "vbo_data");

      dgc_write_vertex_descriptor(cs, &vbo_info, vbo_data);

      dgc_upload(cs, nir_load_var(b, vbo_data));

      nir_store_var(b, vbo_idx, nir_iadd_imm(b, cur_idx, 1), 0x1);
   }
   nir_pop_loop(b, NULL);
}

/**
 * Compute dispatch
 */
static nir_def *
dgc_get_dispatch_initiator(struct dgc_cmdbuf *cs)
{
   const struct radv_device *device = cs->dev;
   nir_builder *b = cs->b;

   const uint32_t dispatch_initiator = device->dispatch_initiator | S_00B800_FORCE_START_AT_000(1);
   nir_def *is_wave32 = nir_ieq_imm(b, load_shader_metadata32(cs, wave32), 1);
   return nir_bcsel(b, is_wave32, nir_imm_int(b, dispatch_initiator | S_00B800_CS_W32_EN(1)),
                    nir_imm_int(b, dispatch_initiator));
}

static void
dgc_emit_grid_size_user_sgpr(struct dgc_cmdbuf *cs, nir_def *grid_base_sgpr, nir_def *wg_x, nir_def *wg_y,
                             nir_def *wg_z)
{
   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 3, 0));
   dgc_cs_emit(grid_base_sgpr);
   dgc_cs_emit(wg_x);
   dgc_cs_emit(wg_y);
   dgc_cs_emit(wg_z);
   dgc_cs_end();
}

static void
dgc_emit_grid_size_pointer(struct dgc_cmdbuf *cs, nir_def *grid_base_sgpr, nir_def *size_va)
{
   nir_builder *b = cs->b;

   nir_def *va_lo = nir_unpack_64_2x32_split_x(b, size_va);
   nir_def *va_hi = nir_unpack_64_2x32_split_y(b, size_va);

   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 2, 0));
   dgc_cs_emit(grid_base_sgpr);
   dgc_cs_emit(va_lo);
   dgc_cs_emit(va_hi);
   dgc_cs_end();
}

static void
dgc_emit_dispatch_direct(struct dgc_cmdbuf *cs, nir_def *wg_x, nir_def *wg_y, nir_def *wg_z,
                         nir_def *dispatch_initiator, nir_def *grid_sgpr, nir_def *size_va, nir_def *sequence_id,
                         bool is_rt)
{
   const struct radv_device *device = cs->dev;
   nir_builder *b = cs->b;

   nir_push_if(b, nir_iand(b, nir_ine_imm(b, wg_x, 0), nir_iand(b, nir_ine_imm(b, wg_y, 0), nir_ine_imm(b, wg_z, 0))));
   {
      nir_push_if(b, nir_ine_imm(b, grid_sgpr, 0));
      {
         if (device->load_grid_size_from_user_sgpr) {
            dgc_emit_grid_size_user_sgpr(cs, grid_sgpr, wg_x, wg_y, wg_z);
         } else {
            dgc_emit_grid_size_pointer(cs, grid_sgpr, size_va);
         }
      }
      nir_pop_if(b, 0);

      dgc_emit_sqtt_begin_api_marker(cs, ApiCmdDispatch);
      dgc_emit_sqtt_marker_event_with_dims(
         cs, sequence_id, wg_x, wg_y, wg_z,
         is_rt ? EventCmdTraceRaysKHR | ApiRayTracingSeparateCompiled : EventCmdDispatch);

      dgc_cs_begin(cs);
      dgc_cs_emit_imm(PKT3(PKT3_DISPATCH_DIRECT, 3, 0) | PKT3_SHADER_TYPE_S(1));
      dgc_cs_emit(wg_x);
      dgc_cs_emit(wg_y);
      dgc_cs_emit(wg_z);
      dgc_cs_emit(dispatch_initiator);
      dgc_cs_end();

      dgc_emit_sqtt_thread_trace_marker(cs);
      dgc_emit_sqtt_end_api_marker(cs, ApiCmdDispatch);
   }
   nir_pop_if(b, 0);
}

static void
dgc_emit_dispatch(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *sequence_id)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   nir_builder *b = cs->b;

   nir_def *dispatch_data = nir_build_load_global(
      b, 3, 32, nir_iadd_imm(b, stream_addr, layout->vk.dispatch_src_offset_B), .access = ACCESS_NON_WRITEABLE);
   nir_def *wg_x = nir_channel(b, dispatch_data, 0);
   nir_def *wg_y = nir_channel(b, dispatch_data, 1);
   nir_def *wg_z = nir_channel(b, dispatch_data, 2);

   nir_def *grid_sgpr = load_shader_metadata32(cs, grid_base_sgpr);
   nir_def *dispatch_initiator = dgc_get_dispatch_initiator(cs);
   nir_def *size_va = nir_iadd_imm(b, stream_addr, layout->vk.dispatch_src_offset_B);

   dgc_emit_dispatch_direct(cs, wg_x, wg_y, wg_z, dispatch_initiator, grid_sgpr, size_va, sequence_id, false);
}

/**
 * Draw mesh/task
 */
static void
dgc_emit_userdata_mesh(struct dgc_cmdbuf *cs, nir_def *x, nir_def *y, nir_def *z, nir_def *drawid)
{
   nir_builder *b = cs->b;

   nir_def *vtx_base_sgpr = load_param16(b, vtx_base_sgpr);
   vtx_base_sgpr = nir_u2u32(b, vtx_base_sgpr);

   nir_def *has_grid_size = nir_test_mask(b, vtx_base_sgpr, DGC_USES_GRID_SIZE);
   nir_def *has_drawid = nir_test_mask(b, vtx_base_sgpr, DGC_USES_DRAWID);

   nir_push_if(b, nir_ior(b, has_grid_size, has_drawid));
   {
      nir_def *pkt_cnt = nir_imm_int(b, 0);
      pkt_cnt = nir_bcsel(b, has_grid_size, nir_iadd_imm(b, pkt_cnt, 3), pkt_cnt);
      pkt_cnt = nir_bcsel(b, has_drawid, nir_iadd_imm(b, pkt_cnt, 1), pkt_cnt);

      dgc_cs_begin(cs);
      dgc_cs_emit(nir_pkt3(b, PKT3_SET_SH_REG, pkt_cnt));
      dgc_cs_emit(nir_iand_imm(b, vtx_base_sgpr, 0x3FFF));
      /* DrawID needs to be first if no GridSize. */
      dgc_cs_emit(nir_bcsel(b, has_grid_size, x, drawid));
      dgc_cs_emit(nir_bcsel(b, has_grid_size, y, nir_imm_int(b, PKT3_NOP_PAD)));
      dgc_cs_emit(nir_bcsel(b, has_grid_size, z, nir_imm_int(b, PKT3_NOP_PAD)));
      dgc_cs_emit(nir_bcsel(b, has_drawid, drawid, nir_imm_int(b, PKT3_NOP_PAD)));
      dgc_cs_end();
   }
   nir_pop_if(b, NULL);
}

static void
dgc_emit_dispatch_mesh_direct(struct dgc_cmdbuf *cs, nir_def *x, nir_def *y, nir_def *z)
{
   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_DISPATCH_MESH_DIRECT, 3, 0));
   dgc_cs_emit(x);
   dgc_cs_emit(y);
   dgc_cs_emit(z);
   dgc_cs_emit_imm(S_0287F0_SOURCE_SELECT(V_0287F0_DI_SRC_SEL_AUTO_INDEX));
   dgc_cs_end();
}

static void
dgc_emit_dispatch_taskmesh_gfx(struct dgc_cmdbuf *cs, nir_def *sequence_id)
{
   const struct radv_device *device = cs->dev;
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_builder *b = cs->b;

   nir_def *vtx_base_sgpr = load_param16(b, vtx_base_sgpr);
   nir_def *has_grid_size = nir_test_mask(b, vtx_base_sgpr, DGC_USES_GRID_SIZE);
   nir_def *has_linear_dispatch_en = nir_ieq_imm(b, load_param8(b, linear_dispatch_en), 1);

   nir_def *base_reg = nir_iand_imm(b, vtx_base_sgpr, 0x3FFF);
   nir_def *xyz_dim_reg = nir_bcsel(b, has_grid_size, base_reg, nir_imm_int(b, 0));
   nir_def *ring_entry_reg = load_param16(b, mesh_ring_entry_sgpr);

   nir_def *xyz_dim_enable = nir_bcsel(b, has_grid_size, nir_imm_int(b, S_4D1_XYZ_DIM_ENABLE(1)), nir_imm_int(b, 0));
   nir_def *mode1_enable = nir_imm_int(b, S_4D1_MODE1_ENABLE(!pdev->mesh_fast_launch_2));
   nir_def *linear_dispatch_en =
      nir_bcsel(b, has_linear_dispatch_en, nir_imm_int(b, S_4D1_LINEAR_DISPATCH_ENABLE(1)), nir_imm_int(b, 0));
   nir_def *sqtt_enable = nir_imm_int(b, device->sqtt.bo ? S_4D1_THREAD_TRACE_MARKER_ENABLE(1) : 0);

   dgc_emit_sqtt_begin_api_marker(cs, ApiCmdDrawMeshTasksEXT);
   dgc_emit_sqtt_marker_event(cs, sequence_id, EventCmdDrawMeshTasksEXT);

   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_DISPATCH_TASKMESH_GFX, 2, 0) | PKT3_RESET_FILTER_CAM_S(1));
   /* S_4D0_RING_ENTRY_REG(ring_entry_reg) | S_4D0_XYZ_DIM_REG(xyz_dim_reg) */
   dgc_cs_emit(nir_ior(b, xyz_dim_reg, nir_ishl_imm(b, ring_entry_reg, 16)));
   if (pdev->info.gfx_level >= GFX11) {
      dgc_cs_emit(nir_ior(b, xyz_dim_enable, nir_ior(b, mode1_enable, nir_ior(b, linear_dispatch_en, sqtt_enable))));
   } else {
      dgc_cs_emit(sqtt_enable);
   }
   dgc_cs_emit_imm(V_0287F0_DI_SRC_SEL_AUTO_INDEX);
   dgc_cs_end();

   dgc_emit_sqtt_thread_trace_marker(cs);
   dgc_emit_sqtt_end_api_marker(cs, ApiCmdDrawMeshTasksEXT);
}

static void
dgc_emit_draw_mesh_tasks_gfx(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *sequence_id)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   const struct radv_device *device = cs->dev;
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_builder *b = cs->b;

   nir_def *draw_data = nir_build_load_global(b, 3, 32, nir_iadd_imm(b, stream_addr, layout->vk.draw_src_offset_B),
                                              .access = ACCESS_NON_WRITEABLE);
   nir_def *x = nir_channel(b, draw_data, 0);
   nir_def *y = nir_channel(b, draw_data, 1);
   nir_def *z = nir_channel(b, draw_data, 2);

   nir_push_if(b, nir_iand(b, nir_ine_imm(b, x, 0), nir_iand(b, nir_ine_imm(b, y, 0), nir_ine_imm(b, z, 0))));
   {
      nir_push_if(b, nir_ieq_imm(b, load_param8(b, has_task_shader), 1));
      {
         dgc_emit_dispatch_taskmesh_gfx(cs, sequence_id);
      }
      nir_push_else(b, NULL);
      {
         dgc_emit_sqtt_begin_api_marker(cs, ApiCmdDrawMeshTasksEXT);
         dgc_emit_sqtt_marker_event(cs, sequence_id, EventCmdDrawMeshTasksEXT);

         dgc_emit_userdata_mesh(cs, x, y, z, sequence_id);
         dgc_emit_instance_count(cs, nir_imm_int(b, 1));

         if (pdev->mesh_fast_launch_2) {
            dgc_emit_dispatch_mesh_direct(cs, x, y, z);
         } else {
            nir_def *vertex_count = nir_imul(b, x, nir_imul(b, y, z));
            dgc_emit_draw_index_auto(cs, vertex_count);
         }

         dgc_emit_sqtt_thread_trace_marker(cs);
         dgc_emit_sqtt_end_api_marker(cs, ApiCmdDrawMeshTasksEXT);
      }
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
}

static void
dgc_emit_draw_mesh_tasks_with_count_gfx(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *sequence_id)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   const struct radv_device *device = cs->dev;
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_builder *b = cs->b;

   nir_push_if(b, nir_ieq_imm(b, load_param8(b, has_task_shader), 1));
   {
      dgc_emit_dispatch_taskmesh_gfx(cs, sequence_id);
   }
   nir_push_else(b, NULL);
   {
      nir_def *vtx_base_sgpr = load_param16(b, vtx_base_sgpr);
      nir_def *has_grid_size = nir_test_mask(b, vtx_base_sgpr, DGC_USES_GRID_SIZE);
      nir_def *has_drawid = nir_test_mask(b, vtx_base_sgpr, DGC_USES_DRAWID);

      nir_def *draw_data = nir_build_load_global(b, 4, 32, nir_iadd_imm(b, stream_addr, layout->vk.draw_src_offset_B),
                                                 .access = ACCESS_NON_WRITEABLE);
      nir_def *va = nir_pack_64_2x32(b, nir_channels(b, draw_data, 0x3));
      nir_def *stride = nir_channel(b, draw_data, 2);
      nir_def *draw_count = nir_umin(b, load_param32(b, max_draw_count), nir_channel(b, draw_data, 3));

      dgc_emit_pkt3_set_base(cs, va);

      nir_def *base_reg = nir_iand_imm(b, vtx_base_sgpr, 0x3FFF);
      nir_def *xyz_dim_reg = nir_bcsel(b, has_grid_size, base_reg, nir_imm_int(b, 0));
      nir_def *draw_id_offset = nir_bcsel(b, has_grid_size, nir_imm_int(b, 3), nir_imm_int(b, 0));
      nir_def *draw_id_reg = nir_bcsel(b, has_drawid, nir_iadd(b, base_reg, draw_id_offset), nir_imm_int(b, 0));

      nir_push_if(b, has_drawid);
      {
         nir_def *packet[3] = {nir_imm_int(b, PKT3(PKT3_SET_SH_REG, 1, 0)), draw_id_reg, nir_imm_int(b, 0)};
         dgc_emit(cs, 3, packet);
      }
      nir_pop_if(b, NULL);

      nir_def *draw_index_enable =
         nir_bcsel(b, has_drawid, nir_imm_int(b, S_4C2_DRAW_INDEX_ENABLE(1)), nir_imm_int(b, 0));
      nir_def *xyz_dim_enable = nir_bcsel(b, has_grid_size, nir_imm_int(b, S_4C2_XYZ_DIM_ENABLE(1)), nir_imm_int(b, 0));

      dgc_emit_sqtt_begin_api_marker(cs, ApiCmdDrawMeshTasksIndirectCountEXT);
      dgc_emit_sqtt_marker_event(cs, sequence_id, EventCmdDrawMeshTasksIndirectCountEXT);

      dgc_cs_begin(cs);
      dgc_cs_emit(nir_imm_int(b, PKT3(PKT3_DISPATCH_MESH_INDIRECT_MULTI, 7, false) | PKT3_RESET_FILTER_CAM_S(1)));
      dgc_cs_emit_imm(0); /* data offset */
      /* S_4C1_XYZ_DIM_REG(xyz_dim_reg) | S_4C1_DRAW_INDEX_REG(draw_id_reg) */
      dgc_cs_emit(
         nir_ior(b, nir_iand_imm(b, xyz_dim_reg, 0xFFFF), nir_ishl_imm(b, nir_iand_imm(b, draw_id_reg, 0xFFFF), 16)));
      if (pdev->info.gfx_level >= GFX11) {
         dgc_cs_emit(nir_ior_imm(b, nir_ior(b, draw_index_enable, xyz_dim_enable),
                                 S_4C2_MODE1_ENABLE(!pdev->mesh_fast_launch_2)));
      } else {
         dgc_cs_emit(draw_index_enable);
      }
      dgc_cs_emit(draw_count);
      dgc_cs_emit_imm(0);
      dgc_cs_emit_imm(0);
      dgc_cs_emit(stride);
      dgc_cs_emit_imm(V_0287F0_DI_SRC_SEL_AUTO_INDEX);
      dgc_cs_end();

      dgc_emit_sqtt_thread_trace_marker(cs);
      dgc_emit_sqtt_end_api_marker(cs, ApiCmdDrawMeshTasksIndirectCountEXT);
   }
   nir_pop_if(b, NULL);
}

static void
dgc_emit_userdata_task(struct dgc_cmdbuf *ace_cs, nir_def *x, nir_def *y, nir_def *z)
{
   nir_builder *b = ace_cs->b;

   nir_def *xyz_sgpr = load_param16(b, task_xyz_sgpr);
   nir_push_if(b, nir_ine_imm(b, xyz_sgpr, 0));
   {
      dgc_cs_begin(ace_cs);
      dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 3, 0));
      dgc_cs_emit(xyz_sgpr);
      dgc_cs_emit(x);
      dgc_cs_emit(y);
      dgc_cs_emit(z);
      dgc_cs_end();
   }
   nir_pop_if(b, NULL);

   nir_def *draw_id_sgpr = load_param16(b, task_draw_id_sgpr);
   nir_push_if(b, nir_ine_imm(b, draw_id_sgpr, 0));
   {
      dgc_cs_begin(ace_cs);
      dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 1, 0));
      dgc_cs_emit(draw_id_sgpr);
      dgc_cs_emit_imm(0);
      dgc_cs_end();
   }
   nir_pop_if(b, NULL);
}

static nir_def *
dgc_get_dispatch_initiator_task(struct dgc_cmdbuf *ace_cs)
{
   const struct radv_device *device = ace_cs->dev;
   const uint32_t dispatch_initiator_task = device->dispatch_initiator_task;
   nir_builder *b = ace_cs->b;

   nir_def *is_wave32 = nir_ieq_imm(b, load_param8(b, wave32), 1);
   return nir_bcsel(b, is_wave32, nir_imm_int(b, dispatch_initiator_task | S_00B800_CS_W32_EN(1)),
                    nir_imm_int(b, dispatch_initiator_task));
}

static void
dgc_emit_dispatch_taskmesh_direct_ace(struct dgc_cmdbuf *ace_cs, nir_def *x, nir_def *y, nir_def *z)
{
   nir_def *dispatch_initiator = dgc_get_dispatch_initiator_task(ace_cs);
   nir_builder *b = ace_cs->b;

   dgc_cs_begin(ace_cs);
   dgc_cs_emit_imm(PKT3(PKT3_DISPATCH_TASKMESH_DIRECT_ACE, 4, 0) | PKT3_SHADER_TYPE_S(1));
   dgc_cs_emit(x);
   dgc_cs_emit(y);
   dgc_cs_emit(z);
   dgc_cs_emit(dispatch_initiator);
   dgc_cs_emit(load_param16(b, task_ring_entry_sgpr));
   dgc_cs_end();
}

static void
dgc_emit_draw_mesh_tasks_ace(struct dgc_cmdbuf *ace_cs, nir_def *stream_addr)
{
   const struct radv_indirect_command_layout *layout = ace_cs->layout;
   nir_builder *b = ace_cs->b;

   nir_def *draw_data = nir_build_load_global(b, 3, 32, nir_iadd_imm(b, stream_addr, layout->vk.draw_src_offset_B),
                                              .access = ACCESS_NON_WRITEABLE);
   nir_def *x = nir_channel(b, draw_data, 0);
   nir_def *y = nir_channel(b, draw_data, 1);
   nir_def *z = nir_channel(b, draw_data, 2);

   nir_push_if(b, nir_iand(b, nir_ine_imm(b, x, 0), nir_iand(b, nir_ine_imm(b, y, 0), nir_ine_imm(b, z, 0))));
   {
      dgc_emit_userdata_task(ace_cs, x, y, z);
      dgc_emit_dispatch_taskmesh_direct_ace(ace_cs, x, y, z);
   }
   nir_pop_if(b, NULL);
}

static void
dgc_emit_draw_mesh_tasks_with_count_ace(struct dgc_cmdbuf *ace_cs, nir_def *stream_addr, nir_def *sequence_id)
{
   const struct radv_indirect_command_layout *layout = ace_cs->layout;
   nir_builder *b = ace_cs->b;

   nir_def *draw_data = nir_build_load_global(b, 4, 32, nir_iadd_imm(b, stream_addr, layout->vk.draw_src_offset_B),
                                              .access = ACCESS_NON_WRITEABLE);
   nir_def *va_lo = nir_channel(b, draw_data, 0);
   nir_def *va_hi = nir_channel(b, draw_data, 1);
   nir_def *stride = nir_channel(b, draw_data, 2);
   nir_def *draw_count = nir_umin(b, load_param32(b, max_draw_count), nir_channel(b, draw_data, 3));

   nir_def *xyz_dim_reg = load_param16(b, task_xyz_sgpr);
   nir_def *ring_entry_reg = load_param16(b, task_ring_entry_sgpr);
   nir_def *draw_id_reg = load_param16(b, task_draw_id_sgpr);

   nir_def *draw_index_enable =
      nir_bcsel(b, nir_ine_imm(b, draw_id_reg, 0), nir_imm_int(b, S_AD3_DRAW_INDEX_ENABLE(1)), nir_imm_int(b, 0));
   nir_def *xyz_dim_enable =
      nir_bcsel(b, nir_ine_imm(b, xyz_dim_reg, 0), nir_imm_int(b, S_AD3_XYZ_DIM_ENABLE(1)), nir_imm_int(b, 0));

   nir_def *dispatch_initiator = dgc_get_dispatch_initiator_task(ace_cs);

   dgc_cs_begin(ace_cs);
   dgc_cs_emit_imm(PKT3(PKT3_DISPATCH_TASKMESH_INDIRECT_MULTI_ACE, 9, 0) | PKT3_SHADER_TYPE_S(1));
   dgc_cs_emit(va_lo);
   dgc_cs_emit(va_hi);
   dgc_cs_emit(ring_entry_reg);
   dgc_cs_emit(nir_ior(b, draw_index_enable, nir_ior(b, xyz_dim_enable, nir_ishl_imm(b, draw_id_reg, 16))));
   dgc_cs_emit(xyz_dim_reg);
   dgc_cs_emit(draw_count);
   dgc_cs_emit_imm(0);
   dgc_cs_emit_imm(0);
   dgc_cs_emit(stride);
   dgc_cs_emit(dispatch_initiator);
   dgc_cs_end();
}

/**
 * Indirect execution set
 */
static void
dgc_emit_indirect_sets(struct dgc_cmdbuf *cs)
{
   nir_builder *b = cs->b;

   nir_def *indirect_desc_sets_sgpr = load_shader_metadata32(cs, indirect_desc_sets_sgpr);
   nir_push_if(b, nir_ine_imm(b, indirect_desc_sets_sgpr, 0));
   {
      dgc_cs_begin(cs);
      dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 1, 0));
      dgc_cs_emit(indirect_desc_sets_sgpr);
      dgc_cs_emit(load_param32(b, indirect_desc_sets_va));
      dgc_cs_end();
   }
   nir_pop_if(b, NULL);
}

static void
dgc_emit_ies(struct dgc_cmdbuf *cs)
{
   nir_builder *b = cs->b;

   nir_def *va = nir_iadd_imm(b, cs->ies_va, sizeof(struct radv_compute_pipeline_metadata));
   nir_def *num_dw = nir_build_load_global(b, 1, 32, va, .access = ACCESS_NON_WRITEABLE);
   nir_def *cs_va = nir_iadd_imm(b, va, 4);

   nir_variable *offset = nir_variable_create(b->shader, nir_var_shader_temp, glsl_uint_type(), "offset");
   nir_store_var(b, offset, nir_imm_int(b, 0), 0x1);

   nir_push_loop(b);
   {
      nir_def *cur_offset = nir_load_var(b, offset);

      nir_break_if(b, nir_uge(b, cur_offset, num_dw));

      nir_def *data = nir_build_load_global(b, 1, 32, nir_iadd(b, cs_va, nir_u2u64(b, nir_imul_imm(b, cur_offset, 4))),
                                            .access = ACCESS_NON_WRITEABLE);

      dgc_cs_begin(cs);
      dgc_cs_emit(data);
      dgc_cs_end();

      nir_store_var(b, offset, nir_iadd_imm(b, cur_offset, 1), 0x1);
   }
   nir_pop_loop(b, NULL);

   dgc_emit_indirect_sets(cs);
}

/**
 * Raytracing.
 */
static void
dgc_emit_shader_pointer(struct dgc_cmdbuf *cs, nir_def *sh_offset, nir_def *va)
{
   nir_builder *b = cs->b;

   nir_def *va_lo = nir_unpack_64_2x32_split_x(b, va);
   nir_def *va_hi = nir_unpack_64_2x32_split_y(b, va);

   dgc_cs_begin(cs);
   dgc_cs_emit_imm(PKT3(PKT3_SET_SH_REG, 2, 0));
   dgc_cs_emit(sh_offset);
   dgc_cs_emit(va_lo);
   dgc_cs_emit(va_hi);
   dgc_cs_end();
}

static void
dgc_emit_rt(struct dgc_cmdbuf *cs, nir_def *stream_addr, nir_def *sequence_id)
{
   const struct radv_indirect_command_layout *layout = cs->layout;
   const struct radv_device *device = cs->dev;
   nir_builder *b = cs->b;

   nir_def *indirect_va = nir_iadd_imm(b, stream_addr, layout->vk.dispatch_src_offset_B);

   nir_def *cs_sbt_descriptors = load_param16(b, cs_sbt_descriptors);
   nir_push_if(b, nir_ine_imm(b, cs_sbt_descriptors, 0));
   {
      dgc_emit_shader_pointer(cs, cs_sbt_descriptors, indirect_va);
   }
   nir_pop_if(b, NULL);

   nir_def *launch_size_va = nir_iadd_imm(b, indirect_va, offsetof(VkTraceRaysIndirectCommand2KHR, width));

   nir_def *cs_ray_launch_size_addr = load_param16(b, cs_ray_launch_size_addr);
   nir_push_if(b, nir_ine_imm(b, cs_ray_launch_size_addr, 0));
   {
      dgc_emit_shader_pointer(cs, cs_ray_launch_size_addr, launch_size_va);
   }
   nir_pop_if(b, NULL);

   const uint32_t dispatch_initiator = device->dispatch_initiator | S_00B800_USE_THREAD_DIMENSIONS(1);
   nir_def *is_wave32 = nir_ieq_imm(b, load_param8(b, wave32), 1);
   nir_def *dispatch_initiator_rt = nir_bcsel(b, is_wave32, nir_imm_int(b, dispatch_initiator | S_00B800_CS_W32_EN(1)),
                                              nir_imm_int(b, dispatch_initiator));

   nir_def *dispatch_data = nir_build_load_global(b, 3, 32, launch_size_va, .access = ACCESS_NON_WRITEABLE);
   nir_def *width = nir_channel(b, dispatch_data, 0);
   nir_def *height = nir_channel(b, dispatch_data, 1);
   nir_def *depth = nir_channel(b, dispatch_data, 2);

   nir_def *grid_sgpr = load_param16(b, grid_base_sgpr);

   dgc_emit_dispatch_direct(cs, width, height, depth, dispatch_initiator_rt, grid_sgpr, launch_size_va, sequence_id,
                            true);
}

static nir_def *
dgc_is_cond_render_enabled(nir_builder *b)
{
   nir_def *res1, *res2;

   nir_push_if(b, nir_ieq_imm(b, load_param8(b, predicating), 1));
   {
      nir_def *val = nir_load_global(b, load_param64(b, predication_va), 4, 1, 32);
      /* By default, all rendering commands are discarded if the 32-bit value is zero. If the
       * inverted flag is set, they are discarded if the value is non-zero.
       */
      res1 = nir_ixor(b, nir_i2b(b, load_param8(b, predication_type)), nir_ine_imm(b, val, 0));
   }
   nir_push_else(b, 0);
   {
      res2 = nir_imm_bool(b, false);
   }
   nir_pop_if(b, 0);

   return nir_if_phi(b, res1, res2);
}

static void
dgc_pad_cmdbuf(struct dgc_cmdbuf *cs, nir_def *cmd_buf_end)
{
   nir_builder *b = cs->b;

   nir_push_if(b, nir_ine(b, nir_load_var(b, cs->offset), cmd_buf_end));
   {
      nir_def *cnt = nir_isub(b, cmd_buf_end, nir_load_var(b, cs->offset));
      cnt = nir_ushr_imm(b, cnt, 2);
      cnt = nir_iadd_imm(b, cnt, -2);
      nir_def *pkt = nir_pkt3(b, PKT3_NOP, cnt);

      dgc_cs_begin(cs);
      dgc_cs_emit(pkt);
      dgc_cs_end();
   }
   nir_pop_if(b, NULL);
}

static nir_shader *
build_dgc_prepare_shader(struct radv_device *dev, struct radv_indirect_command_layout *layout)
{
   const struct radv_physical_device *pdev = radv_device_physical(dev);
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, "meta_dgc_prepare");
   b.shader->info.workgroup_size[0] = 64;

   nir_def *global_id = radv_meta_nir_get_global_ids(&b, 1);

   nir_def *sequence_id = global_id;

   nir_def *cmd_buf_stride = load_param32(&b, cmd_buf_stride);
   nir_def *cmd_buf_base_offset = load_param32(&b, cmd_buf_main_offset);

   nir_def *sequence_count = load_param32(&b, sequence_count);
   nir_def *sequence_count_addr = load_param64(&b, sequence_count_addr);

   /* The effective number of draws is
    * min(sequencesCount, sequencesCountBuffer[sequencesCountOffset]) when
    * using sequencesCountBuffer. Otherwise it is sequencesCount. */
   nir_variable *count_var = nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "sequence_count");
   nir_store_var(&b, count_var, sequence_count, 0x1);

   nir_push_if(&b, nir_ine_imm(&b, sequence_count_addr, 0));
   {
      nir_def *cnt =
         nir_build_load_global(&b, 1, 32, load_param64(&b, sequence_count_addr), .access = ACCESS_NON_WRITEABLE);

      /* Must clamp count against the API count explicitly.
       * The workgroup potentially contains more threads than maxSequencesCount from API,
       * and we have to ensure these threads write NOP packets to pad out the IB. */
      cnt = nir_umin(&b, cnt, sequence_count);
      nir_store_var(&b, count_var, cnt, 0x1);
   }
   nir_pop_if(&b, NULL);

   nir_push_if(&b, dgc_is_cond_render_enabled(&b));
   {
      /* Reset the number of sequences when conditional rendering is enabled in order to skip the
       * entire shader and pad the cmdbuf with NOPs.
       */
      nir_store_var(&b, count_var, nir_imm_int(&b, 0), 0x1);
   }
   nir_pop_if(&b, NULL);

   sequence_count = nir_load_var(&b, count_var);

   build_dgc_buffer_trailer_main(&b, dev);

   nir_push_if(&b, nir_ult(&b, sequence_id, sequence_count));
   {
      struct dgc_cmdbuf cmd_buf = {
         .b = &b,
         .dev = dev,
         .va = nir_pack_64_2x32_split(&b, load_param32(&b, upload_addr), nir_imm_int(&b, pdev->info.address32_hi)),
         .offset = nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "cmd_buf_offset"),
         .upload_offset = nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "upload_offset"),
         .layout = layout,
      };
      nir_store_var(&b, cmd_buf.offset, nir_iadd(&b, nir_imul(&b, global_id, cmd_buf_stride), cmd_buf_base_offset), 1);
      nir_def *cmd_buf_end = nir_iadd(&b, nir_load_var(&b, cmd_buf.offset), cmd_buf_stride);

      nir_def *stream_addr = load_param64(&b, stream_addr);
      stream_addr = nir_iadd(&b, stream_addr, nir_u2u64(&b, nir_imul_imm(&b, sequence_id, layout->vk.stride)));

      nir_def *upload_offset_init =
         nir_iadd(&b, load_param32(&b, upload_main_offset), nir_imul(&b, load_param32(&b, upload_stride), sequence_id));
      nir_store_var(&b, cmd_buf.upload_offset, upload_offset_init, 0x1);

      if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES))
         cmd_buf.ies_va = dgc_load_ies_va(&cmd_buf, stream_addr);

      if (layout->push_constant_mask) {
         const VkShaderStageFlags stages =
            (layout->vk.dgc_info & (BITFIELD_BIT(MESA_VK_DGC_RT) | BITFIELD_BIT(MESA_VK_DGC_DISPATCH)))
               ? VK_SHADER_STAGE_COMPUTE_BIT
               : (VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_MESH_BIT_EXT);

         dgc_emit_push_constant(&cmd_buf, stream_addr, sequence_id, stages);
      }

      if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_RT)) {
         /* Raytracing */
         dgc_emit_rt(&cmd_buf, stream_addr, sequence_id);
      } else if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DISPATCH)) {
         /* Compute */
         if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES)) {
            dgc_emit_ies(&cmd_buf);
         }

         dgc_emit_dispatch(&cmd_buf, stream_addr, sequence_id);
      } else {
         /* Graphics */
         if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_VB)) {
            dgc_emit_vertex_buffer(&cmd_buf, stream_addr);
         }

         if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DRAW_INDEXED)) {
            if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IB)) {
               nir_variable *max_index_count_var =
                  nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "max_index_count");

               dgc_emit_index_buffer(&cmd_buf, stream_addr, max_index_count_var);

               nir_def *max_index_count = nir_load_var(&b, max_index_count_var);

               if (layout->vk.draw_count) {
                  dgc_emit_draw_with_count(&cmd_buf, stream_addr, sequence_id, true);
               } else {
                  dgc_emit_draw_indexed(&cmd_buf, stream_addr, sequence_id, max_index_count);
               }
            } else {
               if (layout->vk.draw_count) {
                  dgc_emit_draw_with_count(&cmd_buf, stream_addr, sequence_id, true);
               } else {
                  dgc_emit_draw_indirect(&cmd_buf, stream_addr, sequence_id, true);
               }
            }
         } else {
            /* Non-indexed draws */
            if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DRAW_MESH)) {
               if (layout->vk.draw_count) {
                  dgc_emit_draw_mesh_tasks_with_count_gfx(&cmd_buf, stream_addr, sequence_id);
               } else {
                  dgc_emit_draw_mesh_tasks_gfx(&cmd_buf, stream_addr, sequence_id);
               }
            } else {
               if (layout->vk.draw_count) {
                  dgc_emit_draw_with_count(&cmd_buf, stream_addr, sequence_id, false);
               } else {
                  dgc_emit_draw(&cmd_buf, stream_addr, sequence_id);
               }
            }
         }
      }

      /* Pad the cmdbuffer if we did not use the whole stride */
      dgc_pad_cmdbuf(&cmd_buf, cmd_buf_end);
   }
   nir_pop_if(&b, NULL);

   build_dgc_buffer_tail_main(&b, sequence_count, dev);
   build_dgc_buffer_preamble_main(&b, sequence_count, dev);

   /* Prepare the ACE command stream */
   nir_push_if(&b, nir_ieq_imm(&b, load_param8(&b, has_task_shader), 1));
   {
      nir_def *ace_cmd_buf_stride = load_param32(&b, ace_cmd_buf_stride);
      nir_def *ace_cmd_buf_base_offset = load_param32(&b, ace_cmd_buf_main_offset);

      build_dgc_buffer_trailer_ace(&b, dev);

      nir_push_if(&b, nir_ult(&b, sequence_id, sequence_count));
      {
         struct dgc_cmdbuf cmd_buf = {
            .b = &b,
            .dev = dev,
            .va = nir_pack_64_2x32_split(&b, load_param32(&b, upload_addr), nir_imm_int(&b, pdev->info.address32_hi)),
            .offset = nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "cmd_buf_offset"),
            .upload_offset = nir_variable_create(b.shader, nir_var_shader_temp, glsl_uint_type(), "upload_offset"),
            .layout = layout,
         };
         nir_store_var(&b, cmd_buf.offset,
                       nir_iadd(&b, nir_imul(&b, global_id, ace_cmd_buf_stride), ace_cmd_buf_base_offset), 1);
         nir_def *cmd_buf_end = nir_iadd(&b, nir_load_var(&b, cmd_buf.offset), ace_cmd_buf_stride);

         nir_def *stream_addr = load_param64(&b, stream_addr);
         stream_addr = nir_iadd(&b, stream_addr, nir_u2u64(&b, nir_imul_imm(&b, sequence_id, layout->vk.stride)));

         nir_def *upload_offset_init = nir_iadd(&b, load_param32(&b, upload_main_offset),
                                                nir_imul(&b, load_param32(&b, upload_stride), sequence_id));
         nir_store_var(&b, cmd_buf.upload_offset, upload_offset_init, 0x1);

         if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_IES))
            cmd_buf.ies_va = dgc_load_ies_va(&cmd_buf, stream_addr);

         if (layout->push_constant_mask) {
            nir_def *push_constant_stages = dgc_get_push_constant_stages(&cmd_buf);

            nir_push_if(&b, nir_test_mask(&b, push_constant_stages, VK_SHADER_STAGE_TASK_BIT_EXT));
            {
               const struct dgc_pc_params params = dgc_get_pc_params(&cmd_buf);
               dgc_emit_push_constant_for_stage(&cmd_buf, stream_addr, sequence_id, &params, MESA_SHADER_TASK);
            }
            nir_pop_if(&b, NULL);
         }

         if (layout->vk.draw_count) {
            dgc_emit_draw_mesh_tasks_with_count_ace(&cmd_buf, stream_addr, sequence_id);
         } else {
            dgc_emit_draw_mesh_tasks_ace(&cmd_buf, stream_addr);
         }

         /* Pad the cmdbuffer if we did not use the whole stride */
         dgc_pad_cmdbuf(&cmd_buf, cmd_buf_end);
      }
      nir_pop_if(&b, NULL);

      build_dgc_buffer_tail_ace(&b, sequence_count, dev);
      build_dgc_buffer_preamble_ace(&b, sequence_count, dev);
   }
   nir_pop_if(&b, NULL);

   return b.shader;
}

static VkResult
radv_create_dgc_pipeline(struct radv_device *device, struct radv_indirect_command_layout *layout)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_DGC;
   VkResult result;

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = sizeof(struct radv_dgc_params),
   };

   result = vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, NULL, &pc_range, &key, sizeof(key),
                                        &layout->pipeline_layout);
   if (result != VK_SUCCESS)
      return result;

   nir_shader *cs = build_dgc_prepare_shader(device, layout);

   const VkPipelineShaderStageCreateInfo stage_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   const VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = stage_info,
      .flags = 0,
      .layout = layout->pipeline_layout,
   };

   /* DGC pipelines don't go through the vk_meta cache because that would require to compute a
    * separate key but they are cached on-disk when possible.
    */
   result = radv_CreateComputePipelines(vk_device_to_handle(&device->vk), device->meta_state.device.pipeline_cache, 1,
                                        &pipeline_info, NULL, &layout->pipeline);

   ralloc_free(cs);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
radv_GetGeneratedCommandsMemoryRequirementsEXT(VkDevice _device,
                                               const VkGeneratedCommandsMemoryRequirementsInfoEXT *pInfo,
                                               VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, pInfo->indirectCommandsLayout);
   struct dgc_cmdbuf_layout cmdbuf_layout;

   get_dgc_cmdbuf_layout(device, layout, pInfo->pNext, pInfo->maxSequenceCount, true, &cmdbuf_layout);

   pMemoryRequirements->memoryRequirements.memoryTypeBits = pdev->memory_types_32bit;
   pMemoryRequirements->memoryRequirements.alignment = radv_dgc_get_buffer_alignment(device);
   pMemoryRequirements->memoryRequirements.size =
      align(cmdbuf_layout.alloc_size, pMemoryRequirements->memoryRequirements.alignment);
}

bool
radv_use_dgc_predication(struct radv_cmd_buffer *cmd_buffer, const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo)
{
   const VkGeneratedCommandsPipelineInfoEXT *pipeline_info =
      vk_find_struct_const(pGeneratedCommandsInfo->pNext, GENERATED_COMMANDS_PIPELINE_INFO_EXT);
   const VkGeneratedCommandsShaderInfoEXT *eso_info =
      vk_find_struct_const(pGeneratedCommandsInfo->pNext, GENERATED_COMMANDS_SHADER_INFO_EXT);

   /* Enable conditional rendering (if not enabled by user) to skip prepare/execute DGC calls when
    * the indirect sequence count might be zero. This can only be enabled on GFX because on ACE it's
    * not possible to skip the execute DGC call (ie. no INDIRECT_PACKET). It should also be disabled
    * when the graphics pipelines has a task shader for the same reason (otherwise the DGC ACE IB
    * would be uninitialized).
    */
   return cmd_buffer->qf == RADV_QUEUE_GENERAL && !radv_dgc_get_shader(pipeline_info, eso_info, MESA_SHADER_TASK) &&
          pGeneratedCommandsInfo->sequenceCountAddress != 0 && !cmd_buffer->state.predicating;
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdPreprocessGeneratedCommandsEXT(VkCommandBuffer commandBuffer,
                                       const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo,
                                       VkCommandBuffer stateCommandBuffer)
{
   VK_FROM_HANDLE(radv_cmd_buffer, state_cmd_buffer, stateCommandBuffer);
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, pGeneratedCommandsInfo->indirectCommandsLayout);

   assert(layout->vk.usage & VK_INDIRECT_COMMANDS_LAYOUT_USAGE_EXPLICIT_PREPROCESS_BIT_EXT);

   /* VK_EXT_conditional_rendering says that copy commands should not be
    * affected by conditional rendering.
    */
   const bool old_predicating = cmd_buffer->state.predicating;
   cmd_buffer->state.predicating = false;

   radv_prepare_dgc(cmd_buffer, pGeneratedCommandsInfo, state_cmd_buffer, old_predicating);

   /* Restore conditional rendering. */
   cmd_buffer->state.predicating = old_predicating;
}

static void
radv_prepare_dgc_compute(struct radv_cmd_buffer *cmd_buffer, const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo,
                         struct radv_cmd_buffer *state_cmd_buffer, unsigned *upload_size, unsigned *upload_offset,
                         void **upload_data, struct radv_dgc_params *params, bool cond_render_enabled)

{
   VK_FROM_HANDLE(radv_indirect_execution_set, ies, pGeneratedCommandsInfo->indirectExecutionSet);
   const struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const uint32_t alloc_size = ies ? 0 : sizeof(struct radv_compute_pipeline_metadata);

   *upload_size = MAX2(*upload_size + alloc_size, 16);

   if (!radv_cmd_buffer_upload_alloc(cmd_buffer, *upload_size, upload_offset, upload_data)) {
      vk_command_buffer_set_error(&cmd_buffer->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
      return;
   }

   if (cond_render_enabled) {
      params->predicating = true;
      params->predication_va = cmd_buffer->state.predication_va;
      params->predication_type = cmd_buffer->state.predication_type;
   }

   if (ies) {
      struct radv_descriptor_state *descriptors_state =
         radv_get_descriptors_state(state_cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE);

      radv_upload_indirect_descriptor_sets(cmd_buffer, descriptors_state);

      params->ies_stride = ies->stride;
      params->indirect_desc_sets_va = descriptors_state->indirect_descriptor_sets_va;
   } else {
      const VkGeneratedCommandsPipelineInfoEXT *pipeline_info =
         vk_find_struct_const(pGeneratedCommandsInfo->pNext, GENERATED_COMMANDS_PIPELINE_INFO_EXT);
      const VkGeneratedCommandsShaderInfoEXT *eso_info =
         vk_find_struct_const(pGeneratedCommandsInfo->pNext, GENERATED_COMMANDS_SHADER_INFO_EXT);
      const struct radv_shader *cs = radv_dgc_get_shader(pipeline_info, eso_info, MESA_SHADER_COMPUTE);
      struct radv_compute_pipeline_metadata *metadata = (struct radv_compute_pipeline_metadata *)(*upload_data);

      radv_get_compute_shader_metadata(device, cs, metadata);

      *upload_data = (char *)*upload_data + alloc_size;
   }
}

static void
radv_prepare_dgc_rt(struct radv_cmd_buffer *cmd_buffer, const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo,
                    unsigned *upload_size, unsigned *upload_offset, void **upload_data, struct radv_dgc_params *params)
{
   if (!radv_cmd_buffer_upload_alloc(cmd_buffer, *upload_size, upload_offset, upload_data)) {
      vk_command_buffer_set_error(&cmd_buffer->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
      return;
   }

   const VkGeneratedCommandsPipelineInfoEXT *pipeline_info =
      vk_find_struct_const(pGeneratedCommandsInfo->pNext, GENERATED_COMMANDS_PIPELINE_INFO_EXT);
   VK_FROM_HANDLE(radv_pipeline, pipeline, pipeline_info->pipeline);
   const struct radv_ray_tracing_pipeline *rt_pipeline = radv_pipeline_to_ray_tracing(pipeline);
   const struct radv_shader *rt_prolog = rt_pipeline->prolog;

   params->wave32 = rt_prolog->info.wave_size == 32;
   params->grid_base_sgpr = radv_get_user_sgpr(rt_prolog, AC_UD_CS_GRID_SIZE);
   params->cs_sbt_descriptors = radv_get_user_sgpr(rt_prolog, AC_UD_CS_SBT_DESCRIPTORS);
   params->cs_ray_launch_size_addr = radv_get_user_sgpr(rt_prolog, AC_UD_CS_RAY_LAUNCH_SIZE_ADDR);
}

static uint32_t
get_dgc_vertex_binding_offset(const struct radv_indirect_command_layout *layout, uint32_t binding)
{
   for (uint32_t i = 0; i < layout->vk.n_vb_layouts; i++) {
      if (layout->vk.vb_layouts[i].binding == binding)
         return layout->vk.vb_layouts[i].src_offset_B;
   }

   return -1;
}

static void
radv_prepare_dgc_graphics(struct radv_cmd_buffer *cmd_buffer, const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo,
                          struct radv_cmd_buffer *state_cmd_buffer, unsigned *upload_size, unsigned *upload_offset,
                          void **upload_data, struct radv_dgc_params *params)
{
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, pGeneratedCommandsInfo->indirectCommandsLayout);

   const VkGeneratedCommandsPipelineInfoEXT *pipeline_info =
      vk_find_struct_const(pGeneratedCommandsInfo->pNext, GENERATED_COMMANDS_PIPELINE_INFO_EXT);
   const VkGeneratedCommandsShaderInfoEXT *eso_info =
      vk_find_struct_const(pGeneratedCommandsInfo->pNext, GENERATED_COMMANDS_SHADER_INFO_EXT);

   const gl_shader_stage first_stage =
      (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DRAW_MESH)) ? MESA_SHADER_MESH : MESA_SHADER_VERTEX;
   struct radv_shader *first_shader = radv_dgc_get_shader(pipeline_info, eso_info, first_stage);

   unsigned vb_size = (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_VB)) ? MAX_VBS * DGC_VBO_INFO_SIZE : 0;

   *upload_size = MAX2(*upload_size + vb_size, 16);

   if (!radv_cmd_buffer_upload_alloc(cmd_buffer, *upload_size, upload_offset, upload_data)) {
      vk_command_buffer_set_error(&cmd_buffer->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
      return;
   }

   uint16_t vtx_base_sgpr = radv_get_user_sgpr(first_shader, AC_UD_VS_BASE_VERTEX_START_INSTANCE);
   const bool uses_drawid = first_shader->info.vs.needs_draw_id;

   if (uses_drawid)
      vtx_base_sgpr |= DGC_USES_DRAWID;

   if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DRAW_MESH)) {
      if (first_shader->info.cs.uses_grid_size)
         vtx_base_sgpr |= DGC_USES_GRID_SIZE;

      const struct radv_shader *task_shader = radv_dgc_get_shader(pipeline_info, eso_info, MESA_SHADER_TASK);
      if (task_shader) {
         params->has_task_shader = 1;
         params->mesh_ring_entry_sgpr = radv_get_user_sgpr(first_shader, AC_UD_TASK_RING_ENTRY);
         params->linear_dispatch_en = task_shader->info.cs.linear_taskmesh_dispatch;
         params->task_ring_entry_sgpr = radv_get_user_sgpr(task_shader, AC_UD_TASK_RING_ENTRY);
         params->wave32 = task_shader->info.wave_size == 32;
         params->task_xyz_sgpr = radv_get_user_sgpr(task_shader, AC_UD_CS_GRID_SIZE);
         params->task_draw_id_sgpr = radv_get_user_sgpr(task_shader, AC_UD_CS_TASK_DRAW_ID);
      }
   } else {
      const bool uses_baseinstance = first_shader->info.vs.needs_base_instance;

      if (uses_baseinstance)
         vtx_base_sgpr |= DGC_USES_BASEINSTANCE;
   }

   params->vtx_base_sgpr = vtx_base_sgpr;
   params->max_index_count = state_cmd_buffer->state.max_index_count;
   params->max_draw_count = pGeneratedCommandsInfo->maxDrawCount;
   params->dynamic_vs_input =
      (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_VB)) && first_shader->info.vs.dynamic_inputs;
   params->use_per_attribute_vb_descs =
      (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_VB)) && first_shader->info.vs.use_per_attribute_vb_descs;

   if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_VB)) {
      uint8_t *ptr = (uint8_t *)((char *)*upload_data);

      for (uint32_t i = 0; i < MAX_VBS; i++) {
         struct radv_vbo_info vbo_info;
         radv_get_vbo_info(state_cmd_buffer, i, &vbo_info);

         const uint32_t vbo_offset = get_dgc_vertex_binding_offset(layout, vbo_info.binding);

         memcpy(ptr, &vbo_info, sizeof(vbo_info));
         ptr += sizeof(struct radv_vbo_info);

         memcpy(ptr, &vbo_offset, sizeof(uint32_t));
         ptr += sizeof(uint32_t);
      }
      params->vb_desc_usage_mask = first_shader->info.vs.vb_desc_usage_mask;
      params->vbo_reg = radv_get_user_sgpr(first_shader, AC_UD_VS_VERTEX_BUFFERS);

      *upload_data = (char *)*upload_data + vb_size;
   }
}

void
radv_prepare_dgc(struct radv_cmd_buffer *cmd_buffer, const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo,
                 struct radv_cmd_buffer *state_cmd_buffer, bool cond_render_enabled)
{
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, pGeneratedCommandsInfo->indirectCommandsLayout);
   VK_FROM_HANDLE(radv_indirect_execution_set, ies, pGeneratedCommandsInfo->indirectExecutionSet);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_meta_saved_state saved_state;
   unsigned upload_offset, upload_size = 0;
   void *upload_data;

   const VkGeneratedCommandsPipelineInfoEXT *pipeline_info =
      vk_find_struct_const(pGeneratedCommandsInfo->pNext, GENERATED_COMMANDS_PIPELINE_INFO_EXT);
   const VkGeneratedCommandsShaderInfoEXT *eso_info =
      vk_find_struct_const(pGeneratedCommandsInfo->pNext, GENERATED_COMMANDS_SHADER_INFO_EXT);

   const bool use_preamble = radv_dgc_use_preamble(pGeneratedCommandsInfo);
   const uint32_t sequences_count = pGeneratedCommandsInfo->maxSequenceCount;

   struct dgc_cmdbuf_layout cmdbuf_layout;
   get_dgc_cmdbuf_layout(device, layout, pGeneratedCommandsInfo->pNext, sequences_count, use_preamble, &cmdbuf_layout);

   assert((cmdbuf_layout.main_offset + pGeneratedCommandsInfo->preprocessAddress) %
             pdev->info.ip[AMD_IP_GFX].ib_alignment ==
          0);
   assert((cmdbuf_layout.ace_main_offset + pGeneratedCommandsInfo->preprocessAddress) %
             pdev->info.ip[AMD_IP_COMPUTE].ib_alignment ==
          0);

   struct radv_dgc_params params = {
      .cmd_buf_preamble_offset = cmdbuf_layout.main_preamble_offset,
      .cmd_buf_main_offset = cmdbuf_layout.main_offset,
      .cmd_buf_stride = cmdbuf_layout.main_cmd_stride,
      .cmd_buf_size = cmdbuf_layout.main_size,
      .ace_cmd_buf_trailer_offset = cmdbuf_layout.ace_trailer_offset,
      .ace_cmd_buf_preamble_offset = cmdbuf_layout.ace_preamble_offset,
      .ace_cmd_buf_main_offset = cmdbuf_layout.ace_main_offset,
      .ace_cmd_buf_stride = cmdbuf_layout.ace_cmd_stride,
      .ace_cmd_buf_size = cmdbuf_layout.ace_size,
      .upload_main_offset = cmdbuf_layout.upload_offset,
      .upload_addr = (uint32_t)pGeneratedCommandsInfo->preprocessAddress,
      .upload_stride = cmdbuf_layout.upload_stride,
      .sequence_count = sequences_count,
      .use_preamble = use_preamble,
      .stream_addr = pGeneratedCommandsInfo->indirectAddress,
      .sequence_count_addr = pGeneratedCommandsInfo->sequenceCountAddress,
      .ies_addr = ies ? ies->va : 0,
      .queue_family = state_cmd_buffer->qf,
   };

   VK_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, layout->vk.layout);

   if (layout->vk.dgc_info & (BITFIELD_BIT(MESA_VK_DGC_PC) | BITFIELD_BIT(MESA_VK_DGC_SI))) {
      upload_size = pipeline_layout->push_constant_size + MESA_VULKAN_SHADER_STAGES * 12;
   }

   if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_DISPATCH)) {
      radv_prepare_dgc_compute(cmd_buffer, pGeneratedCommandsInfo, state_cmd_buffer, &upload_size, &upload_offset,
                               &upload_data, &params, cond_render_enabled);
   } else if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_RT)) {
      radv_prepare_dgc_rt(cmd_buffer, pGeneratedCommandsInfo, &upload_size, &upload_offset, &upload_data, &params);
   } else {
      radv_prepare_dgc_graphics(cmd_buffer, pGeneratedCommandsInfo, state_cmd_buffer, &upload_size, &upload_offset,
                                &upload_data, &params);
   }

   params.params_addr = radv_buffer_get_va(cmd_buffer->upload.upload_bo) + upload_offset;

   if (layout->push_constant_mask) {
      VkShaderStageFlags pc_stages = 0;
      uint32_t *desc = upload_data;
      upload_data = (char *)upload_data + MESA_VULKAN_SHADER_STAGES * 12;

      struct radv_shader *shaders[MESA_VULKAN_SHADER_STAGES] = {0};
      if (pipeline_info) {
         VK_FROM_HANDLE(radv_pipeline, pipeline, pipeline_info->pipeline);

         if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_RT)) {
            const struct radv_ray_tracing_pipeline *rt_pipeline = radv_pipeline_to_ray_tracing(pipeline);
            struct radv_shader *rt_prolog = rt_pipeline->prolog;

            shaders[MESA_SHADER_COMPUTE] = rt_prolog;
         } else {
            memcpy(shaders, pipeline->shaders, sizeof(shaders));
         }
      } else if (eso_info) {
         for (unsigned i = 0; i < eso_info->shaderCount; ++i) {
            VK_FROM_HANDLE(radv_shader_object, shader_object, eso_info->pShaders[i]);
            struct radv_shader *shader = shader_object->shader;
            gl_shader_stage stage = shader->info.stage;

            shaders[stage] = shader;
         }
      }

      for (unsigned i = 0; i < ARRAY_SIZE(shaders); i++) {
         const struct radv_shader *shader = shaders[i];

         if (!shader)
            continue;

         const struct radv_userdata_locations *locs = &shader->info.user_sgprs_locs;
         if (locs->shader_data[AC_UD_PUSH_CONSTANTS].sgpr_idx >= 0) {
            params.const_copy = 1;
         }

         if (locs->shader_data[AC_UD_PUSH_CONSTANTS].sgpr_idx >= 0 ||
             locs->shader_data[AC_UD_INLINE_PUSH_CONSTANTS].sgpr_idx >= 0) {
            unsigned upload_sgpr = 0;
            unsigned inline_sgpr = 0;

            if (locs->shader_data[AC_UD_PUSH_CONSTANTS].sgpr_idx >= 0) {
               upload_sgpr = (shader->info.user_data_0 + 4 * locs->shader_data[AC_UD_PUSH_CONSTANTS].sgpr_idx -
                              SI_SH_REG_OFFSET) >>
                             2;
            }

            if (locs->shader_data[AC_UD_INLINE_PUSH_CONSTANTS].sgpr_idx >= 0) {
               inline_sgpr = (shader->info.user_data_0 + 4 * locs->shader_data[AC_UD_INLINE_PUSH_CONSTANTS].sgpr_idx -
                              SI_SH_REG_OFFSET) >>
                             2;
               desc[i * 3 + 1] = shader->info.inline_push_constant_mask;
               desc[i * 3 + 2] = shader->info.inline_push_constant_mask >> 32;
            }
            desc[i * 3] = upload_sgpr | (inline_sgpr << 16);

            pc_stages |= mesa_to_vk_shader_stage(i);
         }
      }

      params.push_constant_stages = pc_stages;

      memcpy(upload_data, state_cmd_buffer->push_constants, pipeline_layout->push_constant_size);
      upload_data = (char *)upload_data + pipeline_layout->push_constant_size;
   }

   radv_meta_save(&saved_state, cmd_buffer, RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, layout->pipeline);

   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), layout->pipeline_layout,
                              VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);

   unsigned block_count = MAX2(1, DIV_ROUND_UP(pGeneratedCommandsInfo->maxSequenceCount, 64));
   vk_common_CmdDispatch(radv_cmd_buffer_to_handle(cmd_buffer), block_count, 1, 1);

   radv_meta_restore(&saved_state, cmd_buffer);
}

static void
radv_destroy_indirect_commands_layout(struct radv_device *device, const VkAllocationCallbacks *pAllocator,
                                      struct radv_indirect_command_layout *layout)
{
   radv_DestroyPipeline(radv_device_to_handle(device), layout->pipeline, &device->meta_state.alloc);

   vk_indirect_command_layout_destroy(&device->vk, pAllocator, &layout->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateIndirectCommandsLayoutEXT(VkDevice _device, const VkIndirectCommandsLayoutCreateInfoEXT *pCreateInfo,
                                     const VkAllocationCallbacks *pAllocator,
                                     VkIndirectCommandsLayoutEXT *pIndirectCommandsLayout)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   struct radv_indirect_command_layout *layout;
   VkResult result;

   layout = vk_indirect_command_layout_create(&device->vk, pCreateInfo, pAllocator, sizeof(*layout));
   if (!layout)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   for (uint32_t i = 0; i < layout->vk.n_pc_layouts; i++) {
      for (uint32_t j = layout->vk.pc_layouts[i].dst_offset_B / 4, k = 0; k < layout->vk.pc_layouts[i].size_B / 4;
           j++, k++) {
         layout->push_constant_mask |= 1ull << j;
         layout->push_constant_offsets[j] = layout->vk.pc_layouts[i].src_offset_B + k * 4;
      }
   }

   if (layout->vk.dgc_info & BITFIELD_BIT(MESA_VK_DGC_SI)) {
      layout->sequence_index_mask = 1ull << (layout->vk.si_layout.dst_offset_B / 4);
      layout->push_constant_mask |= layout->sequence_index_mask;
   }

   result = radv_create_dgc_pipeline(device, layout);
   if (result != VK_SUCCESS) {
      radv_destroy_indirect_commands_layout(device, pAllocator, layout);
      return result;
   }

   *pIndirectCommandsLayout = radv_indirect_command_layout_to_handle(layout);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyIndirectCommandsLayoutEXT(VkDevice _device, VkIndirectCommandsLayoutEXT indirectCommandsLayout,
                                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_indirect_command_layout, layout, indirectCommandsLayout);

   if (!layout)
      return;

   radv_destroy_indirect_commands_layout(device, pAllocator, layout);
}

static void
radv_update_ies_shader(struct radv_device *device, struct radv_indirect_execution_set *set, uint32_t index,
                       struct radv_shader *shader)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   uint8_t *ptr = set->mapped_ptr + set->stride * index;
   struct radv_compute_pipeline_metadata md;
   struct radeon_cmdbuf *cs;

   assert(shader->info.stage == MESA_SHADER_COMPUTE);
   radv_get_compute_shader_metadata(device, shader, &md);

   cs = calloc(1, sizeof(*cs));
   if (!cs)
      return;

   cs->reserved_dw = cs->max_dw = 32;
   cs->buf = malloc(cs->max_dw * 4);
   if (!cs->buf) {
      free(cs);
      return;
   }

   radv_emit_compute_shader(pdev, cs, shader);

   memcpy(ptr, &md, sizeof(md));
   ptr += sizeof(md);

   memcpy(ptr, &cs->cdw, sizeof(uint32_t));
   ptr += sizeof(uint32_t);

   memcpy(ptr, cs->buf, cs->cdw * sizeof(uint32_t));
   ptr += cs->cdw * sizeof(uint32_t);

   set->compute_scratch_size_per_wave = MAX2(set->compute_scratch_size_per_wave, shader->config.scratch_bytes_per_wave);
   set->compute_scratch_waves = MAX2(set->compute_scratch_waves, radv_get_max_scratch_waves(device, shader));

   free(cs->buf);
   free(cs);
}

static void
radv_update_ies_pipeline(struct radv_device *device, struct radv_indirect_execution_set *set, uint32_t index,
                         const struct radv_pipeline *pipeline)
{
   assert(pipeline->type == RADV_PIPELINE_COMPUTE);
   radv_update_ies_shader(device, set, index, pipeline->shaders[MESA_SHADER_COMPUTE]);
}

static void
radv_destroy_indirect_execution_set(struct radv_device *device, const VkAllocationCallbacks *pAllocator,
                                    struct radv_indirect_execution_set *set)
{
   if (set->bo)
      radv_bo_destroy(device, &set->base, set->bo);

   vk_object_base_finish(&set->base);
   vk_free2(&device->vk.alloc, pAllocator, set);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateIndirectExecutionSetEXT(VkDevice _device, const VkIndirectExecutionSetCreateInfoEXT *pCreateInfo,
                                   const VkAllocationCallbacks *pAllocator,
                                   VkIndirectExecutionSetEXT *pIndirectExecutionSet)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_indirect_execution_set *set;
   uint32_t num_entries;
   uint32_t stride;
   VkResult result;

   set = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*set), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!set)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &set->base, VK_OBJECT_TYPE_INDIRECT_EXECUTION_SET_EXT);

   switch (pCreateInfo->type) {
   case VK_INDIRECT_EXECUTION_SET_INFO_TYPE_PIPELINES_EXT: {
      const VkIndirectExecutionSetPipelineInfoEXT *pipeline_info = pCreateInfo->info.pPipelineInfo;
      VK_FROM_HANDLE(radv_pipeline, pipeline, pipeline_info->initialPipeline);

      assert(pipeline->type == RADV_PIPELINE_COMPUTE);
      num_entries = pipeline_info->maxPipelineCount;
      break;
   }
   case VK_INDIRECT_EXECUTION_SET_INFO_TYPE_SHADER_OBJECTS_EXT: {
      const VkIndirectExecutionSetShaderInfoEXT *shaders_info = pCreateInfo->info.pShaderInfo;
      VK_FROM_HANDLE(radv_shader_object, shader_object, shaders_info->pInitialShaders[0]);

      assert(shader_object->stage == MESA_SHADER_COMPUTE);
      num_entries = shaders_info->maxShaderCount;
      break;
   }
   default:
      unreachable("Invalid IES type");
   }

   stride = sizeof(struct radv_compute_pipeline_metadata);
   stride += 4 /* num CS DW */;
   stride += (pdev->info.gfx_level >= GFX10 ? 19 : 16) * 4;

   result = radv_bo_create(device, &set->base, num_entries * stride, 8, RADEON_DOMAIN_VRAM,
                           RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_READ_ONLY, RADV_BO_PRIORITY_DESCRIPTOR, 0,
                           false, &set->bo);
   if (result != VK_SUCCESS) {
      radv_destroy_indirect_execution_set(device, pAllocator, set);
      return vk_error(device, result);
   }

   set->mapped_ptr = (uint8_t *)radv_buffer_map(device->ws, set->bo);
   if (!set->mapped_ptr) {
      radv_destroy_indirect_execution_set(device, pAllocator, set);
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   set->va = radv_buffer_get_va(set->bo);
   set->stride = stride;

   /* The driver is supposed to always populate slot 0 with the initial pipeline/shader. */
   switch (pCreateInfo->type) {
   case VK_INDIRECT_EXECUTION_SET_INFO_TYPE_PIPELINES_EXT: {
      const VkIndirectExecutionSetPipelineInfoEXT *pipeline_info = pCreateInfo->info.pPipelineInfo;
      VK_FROM_HANDLE(radv_pipeline, pipeline, pipeline_info->initialPipeline);

      radv_update_ies_pipeline(device, set, 0, pipeline);
      break;
   }
   case VK_INDIRECT_EXECUTION_SET_INFO_TYPE_SHADER_OBJECTS_EXT: {
      const VkIndirectExecutionSetShaderInfoEXT *shaders_info = pCreateInfo->info.pShaderInfo;
      VK_FROM_HANDLE(radv_shader_object, shader_object, shaders_info->pInitialShaders[0]);

      radv_update_ies_shader(device, set, 0, shader_object->shader);
      break;
   }
   default:
      unreachable("Invalid IES type");
   }

   *pIndirectExecutionSet = radv_indirect_execution_set_to_handle(set);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyIndirectExecutionSetEXT(VkDevice _device, VkIndirectExecutionSetEXT indirectExecutionSet,
                                    const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_indirect_execution_set, set, indirectExecutionSet);

   if (!set)
      return;

   radv_destroy_indirect_execution_set(device, pAllocator, set);
}

VKAPI_ATTR void VKAPI_CALL
radv_UpdateIndirectExecutionSetPipelineEXT(VkDevice _device, VkIndirectExecutionSetEXT indirectExecutionSet,
                                           uint32_t executionSetWriteCount,
                                           const VkWriteIndirectExecutionSetPipelineEXT *pExecutionSetWrites)
{
   VK_FROM_HANDLE(radv_indirect_execution_set, set, indirectExecutionSet);
   VK_FROM_HANDLE(radv_device, device, _device);

   for (uint32_t i = 0; i < executionSetWriteCount; i++) {
      const VkWriteIndirectExecutionSetPipelineEXT *writeset = &pExecutionSetWrites[i];
      VK_FROM_HANDLE(radv_pipeline, pipeline, writeset->pipeline);

      radv_update_ies_pipeline(device, set, writeset->index, pipeline);
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_UpdateIndirectExecutionSetShaderEXT(VkDevice _device, VkIndirectExecutionSetEXT indirectExecutionSet,
                                         uint32_t executionSetWriteCount,
                                         const VkWriteIndirectExecutionSetShaderEXT *pExecutionSetWrites)
{
   VK_FROM_HANDLE(radv_indirect_execution_set, set, indirectExecutionSet);
   VK_FROM_HANDLE(radv_device, device, _device);

   for (uint32_t i = 0; i < executionSetWriteCount; i++) {
      const VkWriteIndirectExecutionSetShaderEXT *writeset = &pExecutionSetWrites[i];
      VK_FROM_HANDLE(radv_shader_object, shader_object, writeset->shader);

      radv_update_ies_shader(device, set, writeset->index, shader_object->shader);
   }
}
