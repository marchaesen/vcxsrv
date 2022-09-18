/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_pipeline.h"

#include "common/freedreno_guardband.h"

#include "ir3/ir3_nir.h"
#include "main/menums.h"
#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "spirv/nir_spirv.h"
#include "util/debug.h"
#include "util/mesa-sha1.h"
#include "vk_pipeline.h"
#include "vk_render_pass.h"
#include "vk_util.h"

#include "tu_cmd_buffer.h"
#include "tu_cs.h"
#include "tu_device.h"
#include "tu_drm.h"
#include "tu_formats.h"
#include "tu_lrz.h"
#include "tu_pass.h"

/* Emit IB that preloads the descriptors that the shader uses */

static void
emit_load_state(struct tu_cs *cs, unsigned opcode, enum a6xx_state_type st,
                enum a6xx_state_block sb, unsigned base, unsigned offset,
                unsigned count)
{
   /* Note: just emit one packet, even if count overflows NUM_UNIT. It's not
    * clear if emitting more packets will even help anything. Presumably the
    * descriptor cache is relatively small, and these packets stop doing
    * anything when there are too many descriptors.
    */
   tu_cs_emit_pkt7(cs, opcode, 3);
   tu_cs_emit(cs,
              CP_LOAD_STATE6_0_STATE_TYPE(st) |
              CP_LOAD_STATE6_0_STATE_SRC(SS6_BINDLESS) |
              CP_LOAD_STATE6_0_STATE_BLOCK(sb) |
              CP_LOAD_STATE6_0_NUM_UNIT(MIN2(count, 1024-1)));
   tu_cs_emit_qw(cs, offset | (base << 28));
}

static unsigned
tu6_load_state_size(struct tu_pipeline *pipeline,
                    struct tu_pipeline_layout *layout)
{
   const unsigned load_state_size = 4;
   unsigned size = 0;
   for (unsigned i = 0; i < layout->num_sets; i++) {
      if (!(pipeline->active_desc_sets & (1u << i)))
         continue;

      struct tu_descriptor_set_layout *set_layout = layout->set[i].layout;
      for (unsigned j = 0; j < set_layout->binding_count; j++) {
         struct tu_descriptor_set_binding_layout *binding = &set_layout->binding[j];
         unsigned count = 0;
         /* See comment in tu6_emit_load_state(). */
         VkShaderStageFlags stages = pipeline->active_stages & binding->shader_stages;
         unsigned stage_count = util_bitcount(stages);

         if (!binding->array_size)
            continue;

         switch (binding->type) {
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            /* IBO-backed resources only need one packet for all graphics stages */
            if (stage_count)
               count += 1;
            break;
         case VK_DESCRIPTOR_TYPE_SAMPLER:
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT:
            /* Textures and UBO's needs a packet for each stage */
            count = stage_count;
            break;
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            /* Because of how we pack combined images and samplers, we
             * currently can't use one packet for the whole array.
             */
            count = stage_count * binding->array_size * 2;
            break;
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         case VK_DESCRIPTOR_TYPE_MUTABLE_VALVE:
            break;
         default:
            unreachable("bad descriptor type");
         }
         size += count * load_state_size;
      }
   }
   return size;
}

static void
tu6_emit_load_state(struct tu_pipeline *pipeline,
                    struct tu_pipeline_layout *layout)
{
   unsigned size = tu6_load_state_size(pipeline, layout);
   if (size == 0)
      return;

   struct tu_cs cs;
   tu_cs_begin_sub_stream(&pipeline->cs, size, &cs);

   for (unsigned i = 0; i < layout->num_sets; i++) {
      /* From 13.2.7. Descriptor Set Binding:
       *
       *    A compatible descriptor set must be bound for all set numbers that
       *    any shaders in a pipeline access, at the time that a draw or
       *    dispatch command is recorded to execute using that pipeline.
       *    However, if none of the shaders in a pipeline statically use any
       *    bindings with a particular set number, then no descriptor set need
       *    be bound for that set number, even if the pipeline layout includes
       *    a non-trivial descriptor set layout for that set number.
       *
       * This means that descriptor sets unused by the pipeline may have a
       * garbage or 0 BINDLESS_BASE register, which will cause context faults
       * when prefetching descriptors from these sets. Skip prefetching for
       * descriptors from them to avoid this. This is also an optimization,
       * since these prefetches would be useless.
       */
      if (!(pipeline->active_desc_sets & (1u << i)))
         continue;

      struct tu_descriptor_set_layout *set_layout = layout->set[i].layout;
      for (unsigned j = 0; j < set_layout->binding_count; j++) {
         struct tu_descriptor_set_binding_layout *binding = &set_layout->binding[j];
         unsigned base = i;
         unsigned offset = binding->offset / 4;
         /* Note: amber sets VK_SHADER_STAGE_ALL for its descriptor layout, and
          * zink has descriptors for each stage in the push layout even if some
          * stages aren't present in a used pipeline.  We don't want to emit
          * loads for unused descriptors.
          */
         VkShaderStageFlags stages = pipeline->active_stages & binding->shader_stages;
         unsigned count = binding->array_size;

         /* If this is a variable-count descriptor, then the array_size is an
          * upper bound on the size, but we don't know how many descriptors
          * will actually be used. Therefore we can't pre-load them here.
          */
         if (j == set_layout->binding_count - 1 &&
             set_layout->has_variable_descriptors)
            continue;

         if (count == 0 || stages == 0)
            continue;
         switch (binding->type) {
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            base = MAX_SETS;
            offset = (layout->set[i].dynamic_offset_start +
                      binding->dynamic_offset_offset) / 4;
            FALLTHROUGH;
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
            unsigned mul = binding->size / (A6XX_TEX_CONST_DWORDS * 4);
            /* IBO-backed resources only need one packet for all graphics stages */
            if (stages & ~VK_SHADER_STAGE_COMPUTE_BIT) {
               emit_load_state(&cs, CP_LOAD_STATE6, ST6_SHADER, SB6_IBO,
                               base, offset, count * mul);
            }
            if (stages & VK_SHADER_STAGE_COMPUTE_BIT) {
               emit_load_state(&cs, CP_LOAD_STATE6_FRAG, ST6_IBO, SB6_CS_SHADER,
                               base, offset, count * mul);
            }
            break;
         }
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         case VK_DESCRIPTOR_TYPE_MUTABLE_VALVE:
            /* nothing - input attachment doesn't use bindless */
            break;
         case VK_DESCRIPTOR_TYPE_SAMPLER:
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: {
            tu_foreach_stage(stage, stages) {
               emit_load_state(&cs, tu6_stage2opcode(stage),
                               binding->type == VK_DESCRIPTOR_TYPE_SAMPLER ?
                               ST6_SHADER : ST6_CONSTANTS,
                               tu6_stage2texsb(stage), base, offset, count);
            }
            break;
         }
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            base = MAX_SETS;
            offset = (layout->set[i].dynamic_offset_start +
                      binding->dynamic_offset_offset) / 4;
            FALLTHROUGH;
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT: {
            tu_foreach_stage(stage, stages) {
               emit_load_state(&cs, tu6_stage2opcode(stage), ST6_UBO,
                               tu6_stage2shadersb(stage), base, offset, count);
            }
            break;
         }
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
            tu_foreach_stage(stage, stages) {
               /* TODO: We could emit less CP_LOAD_STATE6 if we used
                * struct-of-arrays instead of array-of-structs.
                */
               for (unsigned i = 0; i < count; i++) {
                  unsigned tex_offset = offset + 2 * i * A6XX_TEX_CONST_DWORDS;
                  unsigned sam_offset = offset + (2 * i + 1) * A6XX_TEX_CONST_DWORDS;
                  emit_load_state(&cs, tu6_stage2opcode(stage),
                                  ST6_CONSTANTS, tu6_stage2texsb(stage),
                                  base, tex_offset, 1);
                  emit_load_state(&cs, tu6_stage2opcode(stage),
                                  ST6_SHADER, tu6_stage2texsb(stage),
                                  base, sam_offset, 1);
               }
            }
            break;
         }
         default:
            unreachable("bad descriptor type");
         }
      }
   }

   pipeline->load_state = tu_cs_end_draw_state(&pipeline->cs, &cs);
}

struct tu_pipeline_builder
{
   struct tu_device *device;
   void *mem_ctx;
   struct vk_pipeline_cache *cache;
   struct tu_pipeline_layout *layout;
   const VkAllocationCallbacks *alloc;
   const VkGraphicsPipelineCreateInfo *create_info;

   struct tu_compiled_shaders *shaders;
   struct ir3_shader_variant *binning_variant;
   uint64_t shader_iova[MESA_SHADER_FRAGMENT + 1];
   uint64_t binning_vs_iova;

   uint32_t additional_cs_reserve_size;

   struct tu_pvtmem_config pvtmem;

   bool rasterizer_discard;
   /* these states are affectd by rasterizer_discard */
   bool emit_msaa_state;
   bool depth_clip_disable;
   VkSampleCountFlagBits samples;
   bool use_color_attachments;
   bool use_dual_src_blend;
   bool alpha_to_coverage;
   uint32_t color_attachment_count;
   VkFormat color_attachment_formats[MAX_RTS];
   VkFormat depth_attachment_format;
   uint32_t render_components;
   uint32_t multiview_mask;

   bool subpass_raster_order_attachment_access;
   bool subpass_feedback_loop_color;
   bool subpass_feedback_loop_ds;
   bool feedback_loop_may_involve_textures;
};

static bool
tu_logic_op_reads_dst(VkLogicOp op)
{
   switch (op) {
   case VK_LOGIC_OP_CLEAR:
   case VK_LOGIC_OP_COPY:
   case VK_LOGIC_OP_COPY_INVERTED:
   case VK_LOGIC_OP_SET:
      return false;
   default:
      return true;
   }
}

static VkBlendFactor
tu_blend_factor_no_dst_alpha(VkBlendFactor factor)
{
   /* treat dst alpha as 1.0 and avoid reading it */
   switch (factor) {
   case VK_BLEND_FACTOR_DST_ALPHA:
      return VK_BLEND_FACTOR_ONE;
   case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
      return VK_BLEND_FACTOR_ZERO;
   default:
      return factor;
   }
}

static bool tu_blend_factor_is_dual_src(VkBlendFactor factor)
{
   switch (factor) {
   case VK_BLEND_FACTOR_SRC1_COLOR:
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
   case VK_BLEND_FACTOR_SRC1_ALPHA:
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
      return true;
   default:
      return false;
   }
}

static bool
tu_blend_state_is_dual_src(const VkPipelineColorBlendStateCreateInfo *info)
{
   if (!info)
      return false;

   for (unsigned i = 0; i < info->attachmentCount; i++) {
      const VkPipelineColorBlendAttachmentState *blend = &info->pAttachments[i];
      if (tu_blend_factor_is_dual_src(blend->srcColorBlendFactor) ||
          tu_blend_factor_is_dual_src(blend->dstColorBlendFactor) ||
          tu_blend_factor_is_dual_src(blend->srcAlphaBlendFactor) ||
          tu_blend_factor_is_dual_src(blend->dstAlphaBlendFactor))
         return true;
   }

   return false;
}

static const struct xs_config {
   uint16_t reg_sp_xs_ctrl;
   uint16_t reg_sp_xs_config;
   uint16_t reg_sp_xs_instrlen;
   uint16_t reg_hlsq_xs_ctrl;
   uint16_t reg_sp_xs_first_exec_offset;
   uint16_t reg_sp_xs_pvt_mem_hw_stack_offset;
} xs_config[] = {
   [MESA_SHADER_VERTEX] = {
      REG_A6XX_SP_VS_CTRL_REG0,
      REG_A6XX_SP_VS_CONFIG,
      REG_A6XX_SP_VS_INSTRLEN,
      REG_A6XX_HLSQ_VS_CNTL,
      REG_A6XX_SP_VS_OBJ_FIRST_EXEC_OFFSET,
      REG_A6XX_SP_VS_PVT_MEM_HW_STACK_OFFSET,
   },
   [MESA_SHADER_TESS_CTRL] = {
      REG_A6XX_SP_HS_CTRL_REG0,
      REG_A6XX_SP_HS_CONFIG,
      REG_A6XX_SP_HS_INSTRLEN,
      REG_A6XX_HLSQ_HS_CNTL,
      REG_A6XX_SP_HS_OBJ_FIRST_EXEC_OFFSET,
      REG_A6XX_SP_HS_PVT_MEM_HW_STACK_OFFSET,
   },
   [MESA_SHADER_TESS_EVAL] = {
      REG_A6XX_SP_DS_CTRL_REG0,
      REG_A6XX_SP_DS_CONFIG,
      REG_A6XX_SP_DS_INSTRLEN,
      REG_A6XX_HLSQ_DS_CNTL,
      REG_A6XX_SP_DS_OBJ_FIRST_EXEC_OFFSET,
      REG_A6XX_SP_DS_PVT_MEM_HW_STACK_OFFSET,
   },
   [MESA_SHADER_GEOMETRY] = {
      REG_A6XX_SP_GS_CTRL_REG0,
      REG_A6XX_SP_GS_CONFIG,
      REG_A6XX_SP_GS_INSTRLEN,
      REG_A6XX_HLSQ_GS_CNTL,
      REG_A6XX_SP_GS_OBJ_FIRST_EXEC_OFFSET,
      REG_A6XX_SP_GS_PVT_MEM_HW_STACK_OFFSET,
   },
   [MESA_SHADER_FRAGMENT] = {
      REG_A6XX_SP_FS_CTRL_REG0,
      REG_A6XX_SP_FS_CONFIG,
      REG_A6XX_SP_FS_INSTRLEN,
      REG_A6XX_HLSQ_FS_CNTL,
      REG_A6XX_SP_FS_OBJ_FIRST_EXEC_OFFSET,
      REG_A6XX_SP_FS_PVT_MEM_HW_STACK_OFFSET,
   },
   [MESA_SHADER_COMPUTE] = {
      REG_A6XX_SP_CS_CTRL_REG0,
      REG_A6XX_SP_CS_CONFIG,
      REG_A6XX_SP_CS_INSTRLEN,
      REG_A6XX_HLSQ_CS_CNTL,
      REG_A6XX_SP_CS_OBJ_FIRST_EXEC_OFFSET,
      REG_A6XX_SP_CS_PVT_MEM_HW_STACK_OFFSET,
   },
};

static uint32_t
tu_xs_get_immediates_packet_size_dwords(const struct ir3_shader_variant *xs)
{
   const struct ir3_const_state *const_state = ir3_const_state(xs);
   uint32_t base = const_state->offsets.immediate;
   int32_t size = DIV_ROUND_UP(const_state->immediates_count, 4);

   /* truncate size to avoid writing constants that shader
    * does not use:
    */
   size = MIN2(size + base, xs->constlen) - base;

   return MAX2(size, 0) * 4;
}

/* We allocate fixed-length substreams for shader state, however some
 * parts of the state may have unbound length. Their additional space
 * requirements should be calculated here.
 */
static uint32_t
tu_xs_get_additional_cs_size_dwords(const struct ir3_shader_variant *xs)
{
   const struct ir3_const_state *const_state = ir3_const_state(xs);

   uint32_t size = tu_xs_get_immediates_packet_size_dwords(xs);

   /* Variable number of UBO upload ranges. */
   size += 4 * const_state->ubo_state.num_enabled;

   /* Variable number of dwords for the primitive map */
   size += xs->input_size;

   size += xs->constant_data_size / 4;

   return size;
}

void
tu6_emit_xs_config(struct tu_cs *cs,
                   gl_shader_stage stage, /* xs->type, but xs may be NULL */
                   const struct ir3_shader_variant *xs)
{
   const struct xs_config *cfg = &xs_config[stage];

   if (!xs) {
      /* shader stage disabled */
      tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_config, 1);
      tu_cs_emit(cs, 0);

      tu_cs_emit_pkt4(cs, cfg->reg_hlsq_xs_ctrl, 1);
      tu_cs_emit(cs, 0);
      return;
   }

   tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_config, 1);
   tu_cs_emit(cs, A6XX_SP_VS_CONFIG_ENABLED |
                  COND(xs->bindless_tex, A6XX_SP_VS_CONFIG_BINDLESS_TEX) |
                  COND(xs->bindless_samp, A6XX_SP_VS_CONFIG_BINDLESS_SAMP) |
                  COND(xs->bindless_ibo, A6XX_SP_VS_CONFIG_BINDLESS_IBO) |
                  COND(xs->bindless_ubo, A6XX_SP_VS_CONFIG_BINDLESS_UBO) |
                  A6XX_SP_VS_CONFIG_NTEX(xs->num_samp) |
                  A6XX_SP_VS_CONFIG_NSAMP(xs->num_samp));

   tu_cs_emit_pkt4(cs, cfg->reg_hlsq_xs_ctrl, 1);
   tu_cs_emit(cs, A6XX_HLSQ_VS_CNTL_CONSTLEN(xs->constlen) |
                  A6XX_HLSQ_VS_CNTL_ENABLED);
}

void
tu6_emit_xs(struct tu_cs *cs,
            gl_shader_stage stage, /* xs->type, but xs may be NULL */
            const struct ir3_shader_variant *xs,
            const struct tu_pvtmem_config *pvtmem,
            uint64_t binary_iova)
{
   const struct xs_config *cfg = &xs_config[stage];

   if (!xs) {
      /* shader stage disabled */
      return;
   }

   enum a6xx_threadsize thrsz =
      xs->info.double_threadsize ? THREAD128 : THREAD64;
   switch (stage) {
   case MESA_SHADER_VERTEX:
      tu_cs_emit_regs(cs, A6XX_SP_VS_CTRL_REG0(
               .fullregfootprint = xs->info.max_reg + 1,
               .halfregfootprint = xs->info.max_half_reg + 1,
               .branchstack = ir3_shader_branchstack_hw(xs),
               .mergedregs = xs->mergedregs,
      ));
      break;
   case MESA_SHADER_TESS_CTRL:
      tu_cs_emit_regs(cs, A6XX_SP_HS_CTRL_REG0(
               .fullregfootprint = xs->info.max_reg + 1,
               .halfregfootprint = xs->info.max_half_reg + 1,
               .branchstack = ir3_shader_branchstack_hw(xs),
      ));
      break;
   case MESA_SHADER_TESS_EVAL:
      tu_cs_emit_regs(cs, A6XX_SP_DS_CTRL_REG0(
               .fullregfootprint = xs->info.max_reg + 1,
               .halfregfootprint = xs->info.max_half_reg + 1,
               .branchstack = ir3_shader_branchstack_hw(xs),
      ));
      break;
   case MESA_SHADER_GEOMETRY:
      tu_cs_emit_regs(cs, A6XX_SP_GS_CTRL_REG0(
               .fullregfootprint = xs->info.max_reg + 1,
               .halfregfootprint = xs->info.max_half_reg + 1,
               .branchstack = ir3_shader_branchstack_hw(xs),
      ));
      break;
   case MESA_SHADER_FRAGMENT:
      tu_cs_emit_regs(cs, A6XX_SP_FS_CTRL_REG0(
               .fullregfootprint = xs->info.max_reg + 1,
               .halfregfootprint = xs->info.max_half_reg + 1,
               .branchstack = ir3_shader_branchstack_hw(xs),
               .mergedregs = xs->mergedregs,
               .threadsize = thrsz,
               .pixlodenable = xs->need_pixlod,
               .diff_fine = xs->need_fine_derivatives,
               .varying = xs->total_in != 0,
               /* unknown bit, seems unnecessary */
               .unk24 = true,
      ));
      break;
   case MESA_SHADER_COMPUTE:
      tu_cs_emit_regs(cs, A6XX_SP_CS_CTRL_REG0(
               .fullregfootprint = xs->info.max_reg + 1,
               .halfregfootprint = xs->info.max_half_reg + 1,
               .branchstack = ir3_shader_branchstack_hw(xs),
               .mergedregs = xs->mergedregs,
               .threadsize = thrsz,
      ));
      break;
   default:
      unreachable("bad shader stage");
   }

   tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_instrlen, 1);
   tu_cs_emit(cs, xs->instrlen);

   /* emit program binary & private memory layout
    * binary_iova should be aligned to 1 instrlen unit (128 bytes)
    */

   assert((binary_iova & 0x7f) == 0);
   assert((pvtmem->iova & 0x1f) == 0);

   tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_first_exec_offset, 7);
   tu_cs_emit(cs, 0);
   tu_cs_emit_qw(cs, binary_iova);
   tu_cs_emit(cs,
              A6XX_SP_VS_PVT_MEM_PARAM_MEMSIZEPERITEM(pvtmem->per_fiber_size));
   tu_cs_emit_qw(cs, pvtmem->iova);
   tu_cs_emit(cs, A6XX_SP_VS_PVT_MEM_SIZE_TOTALPVTMEMSIZE(pvtmem->per_sp_size) |
                  COND(pvtmem->per_wave, A6XX_SP_VS_PVT_MEM_SIZE_PERWAVEMEMLAYOUT));

   tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_pvt_mem_hw_stack_offset, 1);
   tu_cs_emit(cs, A6XX_SP_VS_PVT_MEM_HW_STACK_OFFSET_OFFSET(pvtmem->per_sp_size));

   uint32_t shader_preload_size =
      MIN2(xs->instrlen, cs->device->physical_device->info->a6xx.instr_cache_size);

   tu_cs_emit_pkt7(cs, tu6_stage2opcode(stage), 3);
   tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(0) |
                  CP_LOAD_STATE6_0_STATE_TYPE(ST6_SHADER) |
                  CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                  CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(stage)) |
                  CP_LOAD_STATE6_0_NUM_UNIT(shader_preload_size));
   tu_cs_emit_qw(cs, binary_iova);

   /* emit immediates */

   const struct ir3_const_state *const_state = ir3_const_state(xs);
   uint32_t base = const_state->offsets.immediate;
   unsigned immediate_size = tu_xs_get_immediates_packet_size_dwords(xs);

   if (immediate_size > 0) {
      tu_cs_emit_pkt7(cs, tu6_stage2opcode(stage), 3 + immediate_size);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(base) |
                 CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                 CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                 CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(stage)) |
                 CP_LOAD_STATE6_0_NUM_UNIT(immediate_size / 4));
      tu_cs_emit(cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
      tu_cs_emit(cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));

      tu_cs_emit_array(cs, const_state->immediates, immediate_size);
   }

   if (const_state->constant_data_ubo != -1) {
      uint64_t iova = binary_iova + xs->info.constant_data_offset;

      /* Upload UBO state for the constant data. */
      tu_cs_emit_pkt7(cs, tu6_stage2opcode(stage), 5);
      tu_cs_emit(cs,
                 CP_LOAD_STATE6_0_DST_OFF(const_state->constant_data_ubo) |
                 CP_LOAD_STATE6_0_STATE_TYPE(ST6_UBO)|
                 CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                 CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(stage)) |
                 CP_LOAD_STATE6_0_NUM_UNIT(1));
      tu_cs_emit(cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
      tu_cs_emit(cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));
      int size_vec4s = DIV_ROUND_UP(xs->constant_data_size, 16);
      tu_cs_emit_qw(cs,
                    iova |
                    (uint64_t)A6XX_UBO_1_SIZE(size_vec4s) << 32);

      /* Upload the constant data to the const file if needed. */
      const struct ir3_ubo_analysis_state *ubo_state = &const_state->ubo_state;

      for (int i = 0; i < ubo_state->num_enabled; i++) {
         if (ubo_state->range[i].ubo.block != const_state->constant_data_ubo ||
             ubo_state->range[i].ubo.bindless) {
            continue;
         }

         uint32_t start = ubo_state->range[i].start;
         uint32_t end = ubo_state->range[i].end;
         uint32_t size = MIN2(end - start,
                              (16 * xs->constlen) - ubo_state->range[i].offset);

         tu_cs_emit_pkt7(cs, tu6_stage2opcode(stage), 3);
         tu_cs_emit(cs,
                    CP_LOAD_STATE6_0_DST_OFF(ubo_state->range[i].offset / 16) |
                    CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                    CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                    CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(stage)) |
                    CP_LOAD_STATE6_0_NUM_UNIT(size / 16));
         tu_cs_emit_qw(cs, iova + start);
      }
   }

   /* emit FS driver param */
   if (stage == MESA_SHADER_FRAGMENT && const_state->num_driver_params > 0) {
      uint32_t base = const_state->offsets.driver_param;
      int32_t size = DIV_ROUND_UP(const_state->num_driver_params, 4);
      size = MAX2(MIN2(size + base, xs->constlen) - base, 0);

      if (size > 0) {
         tu_cs_emit_pkt7(cs, tu6_stage2opcode(stage), 3 + size * 4);
         tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(base) |
                    CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                    CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                    CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(stage)) |
                    CP_LOAD_STATE6_0_NUM_UNIT(size));
         tu_cs_emit(cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
         tu_cs_emit(cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));

         assert(size == 1);
         tu_cs_emit(cs, xs->info.double_threadsize ? 128 : 64);
         tu_cs_emit(cs, 0);
         tu_cs_emit(cs, 0);
         tu_cs_emit(cs, 0);
      }
   }
}

static void
tu6_emit_shared_consts_enable(struct tu_cs *cs, bool enable)
{
   /* Enable/disable shared constants */
   tu_cs_emit_regs(cs, A6XX_HLSQ_SHARED_CONSTS(.enable = enable));
   tu_cs_emit_regs(cs, A6XX_SP_MODE_CONTROL(.constant_demotion_enable = true,
                                            .isammode = ISAMMODE_GL,
                                            .shared_consts_enable = enable));
}

static void
tu6_emit_cs_config(struct tu_cs *cs,
                   const struct ir3_shader_variant *v,
                   const struct tu_pvtmem_config *pvtmem,
                   uint64_t binary_iova)
{
   bool shared_consts_enable = ir3_const_state(v)->shared_consts_enable;
   tu6_emit_shared_consts_enable(cs, shared_consts_enable);

   tu_cs_emit_regs(cs, A6XX_HLSQ_INVALIDATE_CMD(
         .cs_state = true,
         .cs_ibo = true,
         .cs_shared_const = shared_consts_enable));

   tu6_emit_xs_config(cs, MESA_SHADER_COMPUTE, v);
   tu6_emit_xs(cs, MESA_SHADER_COMPUTE, v, pvtmem, binary_iova);

   uint32_t shared_size = MAX2(((int)v->shared_size - 1) / 1024, 1);
   tu_cs_emit_pkt4(cs, REG_A6XX_SP_CS_UNKNOWN_A9B1, 1);
   tu_cs_emit(cs, A6XX_SP_CS_UNKNOWN_A9B1_SHARED_SIZE(shared_size) |
                  A6XX_SP_CS_UNKNOWN_A9B1_UNK6);

   if (cs->device->physical_device->info->a6xx.has_lpac) {
      tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_CS_UNKNOWN_B9D0, 1);
      tu_cs_emit(cs, A6XX_HLSQ_CS_UNKNOWN_B9D0_SHARED_SIZE(shared_size) |
                     A6XX_HLSQ_CS_UNKNOWN_B9D0_UNK6);
   }

   uint32_t local_invocation_id =
      ir3_find_sysval_regid(v, SYSTEM_VALUE_LOCAL_INVOCATION_ID);
   uint32_t work_group_id =
      ir3_find_sysval_regid(v, SYSTEM_VALUE_WORKGROUP_ID);

   enum a6xx_threadsize thrsz = v->info.double_threadsize ? THREAD128 : THREAD64;
   tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_CS_CNTL_0, 2);
   tu_cs_emit(cs,
              A6XX_HLSQ_CS_CNTL_0_WGIDCONSTID(work_group_id) |
              A6XX_HLSQ_CS_CNTL_0_WGSIZECONSTID(regid(63, 0)) |
              A6XX_HLSQ_CS_CNTL_0_WGOFFSETCONSTID(regid(63, 0)) |
              A6XX_HLSQ_CS_CNTL_0_LOCALIDREGID(local_invocation_id));
   tu_cs_emit(cs, A6XX_HLSQ_CS_CNTL_1_LINEARLOCALIDREGID(regid(63, 0)) |
                  A6XX_HLSQ_CS_CNTL_1_THREADSIZE(thrsz));

   if (cs->device->physical_device->info->a6xx.has_lpac) {
      tu_cs_emit_pkt4(cs, REG_A6XX_SP_CS_CNTL_0, 2);
      tu_cs_emit(cs,
                 A6XX_SP_CS_CNTL_0_WGIDCONSTID(work_group_id) |
                 A6XX_SP_CS_CNTL_0_WGSIZECONSTID(regid(63, 0)) |
                 A6XX_SP_CS_CNTL_0_WGOFFSETCONSTID(regid(63, 0)) |
                 A6XX_SP_CS_CNTL_0_LOCALIDREGID(local_invocation_id));
      tu_cs_emit(cs, A6XX_SP_CS_CNTL_1_LINEARLOCALIDREGID(regid(63, 0)) |
                     A6XX_SP_CS_CNTL_1_THREADSIZE(thrsz));
   }
}

#define TU6_EMIT_VFD_DEST_MAX_DWORDS (MAX_VERTEX_ATTRIBS + 2)

static void
tu6_emit_vfd_dest(struct tu_cs *cs,
                  const struct ir3_shader_variant *vs)
{
   int32_t input_for_attr[MAX_VERTEX_ATTRIBS];
   uint32_t attr_count = 0;

   for (unsigned i = 0; i < MAX_VERTEX_ATTRIBS; i++)
      input_for_attr[i] = -1;

   for (unsigned i = 0; i < vs->inputs_count; i++) {
      if (vs->inputs[i].sysval || vs->inputs[i].regid == regid(63, 0))
         continue;

      assert(vs->inputs[i].slot >= VERT_ATTRIB_GENERIC0);
      unsigned loc = vs->inputs[i].slot - VERT_ATTRIB_GENERIC0;
      input_for_attr[loc] = i;
      attr_count = MAX2(attr_count, loc + 1);
   }

   tu_cs_emit_regs(cs,
                   A6XX_VFD_CONTROL_0(
                     .fetch_cnt = attr_count, /* decode_cnt for binning pass ? */
                     .decode_cnt = attr_count));

   if (attr_count)
      tu_cs_emit_pkt4(cs, REG_A6XX_VFD_DEST_CNTL_INSTR(0), attr_count);

   for (unsigned i = 0; i < attr_count; i++) {
      if (input_for_attr[i] >= 0) {
            unsigned input_idx = input_for_attr[i];
            tu_cs_emit(cs, A6XX_VFD_DEST_CNTL_INSTR(0,
                             .writemask = vs->inputs[input_idx].compmask,
                             .regid = vs->inputs[input_idx].regid).value);
      } else {
            tu_cs_emit(cs, A6XX_VFD_DEST_CNTL_INSTR(0,
                             .writemask = 0,
                             .regid = regid(63, 0)).value);
      }
   }
}

static void
tu6_emit_vs_system_values(struct tu_cs *cs,
                          const struct ir3_shader_variant *vs,
                          const struct ir3_shader_variant *hs,
                          const struct ir3_shader_variant *ds,
                          const struct ir3_shader_variant *gs,
                          bool primid_passthru)
{
   const uint32_t vertexid_regid =
         ir3_find_sysval_regid(vs, SYSTEM_VALUE_VERTEX_ID);
   const uint32_t instanceid_regid =
         ir3_find_sysval_regid(vs, SYSTEM_VALUE_INSTANCE_ID);
   const uint32_t tess_coord_x_regid = hs ?
         ir3_find_sysval_regid(ds, SYSTEM_VALUE_TESS_COORD) :
         regid(63, 0);
   const uint32_t tess_coord_y_regid = VALIDREG(tess_coord_x_regid) ?
         tess_coord_x_regid + 1 :
         regid(63, 0);
   const uint32_t hs_rel_patch_regid = hs ?
         ir3_find_sysval_regid(hs, SYSTEM_VALUE_REL_PATCH_ID_IR3) :
         regid(63, 0);
   const uint32_t ds_rel_patch_regid = hs ?
         ir3_find_sysval_regid(ds, SYSTEM_VALUE_REL_PATCH_ID_IR3) :
         regid(63, 0);
   const uint32_t hs_invocation_regid = hs ?
         ir3_find_sysval_regid(hs, SYSTEM_VALUE_TCS_HEADER_IR3) :
         regid(63, 0);
   const uint32_t gs_primitiveid_regid = gs ?
         ir3_find_sysval_regid(gs, SYSTEM_VALUE_PRIMITIVE_ID) :
         regid(63, 0);
   const uint32_t vs_primitiveid_regid = hs ?
         ir3_find_sysval_regid(hs, SYSTEM_VALUE_PRIMITIVE_ID) :
         gs_primitiveid_regid;
   const uint32_t ds_primitiveid_regid = ds ?
         ir3_find_sysval_regid(ds, SYSTEM_VALUE_PRIMITIVE_ID) :
         regid(63, 0);
   const uint32_t gsheader_regid = gs ?
         ir3_find_sysval_regid(gs, SYSTEM_VALUE_GS_HEADER_IR3) :
         regid(63, 0);

   /* Note: we currently don't support multiview with tess or GS. If we did,
    * and the HW actually works, then we'd have to somehow share this across
    * stages. Note that the blob doesn't support this either.
    */
   const uint32_t viewid_regid =
      ir3_find_sysval_regid(vs, SYSTEM_VALUE_VIEW_INDEX);

   tu_cs_emit_pkt4(cs, REG_A6XX_VFD_CONTROL_1, 6);
   tu_cs_emit(cs, A6XX_VFD_CONTROL_1_REGID4VTX(vertexid_regid) |
                  A6XX_VFD_CONTROL_1_REGID4INST(instanceid_regid) |
                  A6XX_VFD_CONTROL_1_REGID4PRIMID(vs_primitiveid_regid) |
                  A6XX_VFD_CONTROL_1_REGID4VIEWID(viewid_regid));
   tu_cs_emit(cs, A6XX_VFD_CONTROL_2_REGID_HSRELPATCHID(hs_rel_patch_regid) |
                  A6XX_VFD_CONTROL_2_REGID_INVOCATIONID(hs_invocation_regid));
   tu_cs_emit(cs, A6XX_VFD_CONTROL_3_REGID_DSRELPATCHID(ds_rel_patch_regid) |
                  A6XX_VFD_CONTROL_3_REGID_TESSX(tess_coord_x_regid) |
                  A6XX_VFD_CONTROL_3_REGID_TESSY(tess_coord_y_regid) |
                  A6XX_VFD_CONTROL_3_REGID_DSPRIMID(ds_primitiveid_regid));
   tu_cs_emit(cs, 0x000000fc); /* VFD_CONTROL_4 */
   tu_cs_emit(cs, A6XX_VFD_CONTROL_5_REGID_GSHEADER(gsheader_regid) |
                  0xfc00); /* VFD_CONTROL_5 */
   tu_cs_emit(cs, COND(primid_passthru, A6XX_VFD_CONTROL_6_PRIMID_PASSTHRU)); /* VFD_CONTROL_6 */
}

static void
tu6_setup_streamout(struct tu_cs *cs,
                    const struct ir3_shader_variant *v,
                    struct ir3_shader_linkage *l)
{
   const struct ir3_stream_output_info *info = &v->stream_output;
   /* Note: 64 here comes from the HW layout of the program RAM. The program
    * for stream N is at DWORD 64 * N.
    */
#define A6XX_SO_PROG_DWORDS 64
   uint32_t prog[A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS] = {};
   BITSET_DECLARE(valid_dwords, A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS) = {0};

   /* TODO: streamout state should be in a non-GMEM draw state */

   /* no streamout: */
   if (info->num_outputs == 0) {
      unsigned sizedw = 4;
      if (cs->device->physical_device->info->a6xx.tess_use_shared)
         sizedw += 2;

      tu_cs_emit_pkt7(cs, CP_CONTEXT_REG_BUNCH, sizedw);
      tu_cs_emit(cs, REG_A6XX_VPC_SO_CNTL);
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, REG_A6XX_VPC_SO_STREAM_CNTL);
      tu_cs_emit(cs, 0);

      if (cs->device->physical_device->info->a6xx.tess_use_shared) {
         tu_cs_emit(cs, REG_A6XX_PC_SO_STREAM_CNTL);
         tu_cs_emit(cs, 0);
      }

      return;
   }

   for (unsigned i = 0; i < info->num_outputs; i++) {
      const struct ir3_stream_output *out = &info->output[i];
      unsigned k = out->register_index;
      unsigned idx;

      /* Skip it, if it's an output that was never assigned a register. */
      if (k >= v->outputs_count || v->outputs[k].regid == INVALID_REG)
         continue;

      /* linkage map sorted by order frag shader wants things, so
       * a bit less ideal here..
       */
      for (idx = 0; idx < l->cnt; idx++)
         if (l->var[idx].slot == v->outputs[k].slot)
            break;

      assert(idx < l->cnt);

      for (unsigned j = 0; j < out->num_components; j++) {
         unsigned c   = j + out->start_component;
         unsigned loc = l->var[idx].loc + c;
         unsigned off = j + out->dst_offset;  /* in dwords */

         assert(loc < A6XX_SO_PROG_DWORDS * 2);
         unsigned dword = out->stream * A6XX_SO_PROG_DWORDS + loc/2;
         if (loc & 1) {
            prog[dword] |= A6XX_VPC_SO_PROG_B_EN |
                           A6XX_VPC_SO_PROG_B_BUF(out->output_buffer) |
                           A6XX_VPC_SO_PROG_B_OFF(off * 4);
         } else {
            prog[dword] |= A6XX_VPC_SO_PROG_A_EN |
                           A6XX_VPC_SO_PROG_A_BUF(out->output_buffer) |
                           A6XX_VPC_SO_PROG_A_OFF(off * 4);
         }
         BITSET_SET(valid_dwords, dword);
      }
   }

   unsigned prog_count = 0;
   unsigned start, end;
   BITSET_FOREACH_RANGE(start, end, valid_dwords,
                        A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS) {
      prog_count += end - start + 1;
   }

   const bool emit_pc_so_stream_cntl =
      cs->device->physical_device->info->a6xx.tess_use_shared &&
      v->type == MESA_SHADER_TESS_EVAL;

   if (emit_pc_so_stream_cntl)
      prog_count += 1;

   tu_cs_emit_pkt7(cs, CP_CONTEXT_REG_BUNCH, 10 + 2 * prog_count);
   tu_cs_emit(cs, REG_A6XX_VPC_SO_STREAM_CNTL);
   tu_cs_emit(cs, A6XX_VPC_SO_STREAM_CNTL_STREAM_ENABLE(info->streams_written) |
                  COND(info->stride[0] > 0,
                       A6XX_VPC_SO_STREAM_CNTL_BUF0_STREAM(1 + info->buffer_to_stream[0])) |
                  COND(info->stride[1] > 0,
                       A6XX_VPC_SO_STREAM_CNTL_BUF1_STREAM(1 + info->buffer_to_stream[1])) |
                  COND(info->stride[2] > 0,
                       A6XX_VPC_SO_STREAM_CNTL_BUF2_STREAM(1 + info->buffer_to_stream[2])) |
                  COND(info->stride[3] > 0,
                       A6XX_VPC_SO_STREAM_CNTL_BUF3_STREAM(1 + info->buffer_to_stream[3])));
   for (uint32_t i = 0; i < 4; i++) {
      tu_cs_emit(cs, REG_A6XX_VPC_SO_BUFFER_STRIDE(i));
      tu_cs_emit(cs, info->stride[i]);
   }
   bool first = true;
   BITSET_FOREACH_RANGE(start, end, valid_dwords,
                        A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS) {
      tu_cs_emit(cs, REG_A6XX_VPC_SO_CNTL);
      tu_cs_emit(cs, COND(first, A6XX_VPC_SO_CNTL_RESET) |
                     A6XX_VPC_SO_CNTL_ADDR(start));
      for (unsigned i = start; i < end; i++) {
         tu_cs_emit(cs, REG_A6XX_VPC_SO_PROG);
         tu_cs_emit(cs, prog[i]);
      }
      first = false;
   }

   if (emit_pc_so_stream_cntl) {
      /* Possibly not tess_use_shared related, but the combination of
       * tess + xfb fails some tests if we don't emit this.
       */
      tu_cs_emit(cs, REG_A6XX_PC_SO_STREAM_CNTL);
      tu_cs_emit(cs, A6XX_PC_SO_STREAM_CNTL_STREAM_ENABLE(info->streams_written));
   }
}

static void
tu6_emit_const(struct tu_cs *cs, uint32_t opcode, uint32_t base,
               enum a6xx_state_block block, uint32_t offset,
               uint32_t size, const uint32_t *dwords) {
   assert(size % 4 == 0);

   tu_cs_emit_pkt7(cs, opcode, 3 + size);
   tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(base) |
         CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
         CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
         CP_LOAD_STATE6_0_STATE_BLOCK(block) |
         CP_LOAD_STATE6_0_NUM_UNIT(size / 4));

   tu_cs_emit(cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
   tu_cs_emit(cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));
   dwords = (uint32_t *)&((uint8_t *)dwords)[offset];

   tu_cs_emit_array(cs, dwords, size);
}

static void
tu6_emit_link_map(struct tu_cs *cs,
                  const struct ir3_shader_variant *producer,
                  const struct ir3_shader_variant *consumer,
                  enum a6xx_state_block sb)
{
   const struct ir3_const_state *const_state = ir3_const_state(consumer);
   uint32_t base = const_state->offsets.primitive_map;
   int size = DIV_ROUND_UP(consumer->input_size, 4);

   size = (MIN2(size + base, consumer->constlen) - base) * 4;
   if (size <= 0)
      return;

   tu6_emit_const(cs, CP_LOAD_STATE6_GEOM, base, sb, 0, size,
                         producer->output_loc);
}

static uint16_t
primitive_to_tess(enum shader_prim primitive) {
   switch (primitive) {
   case SHADER_PRIM_POINTS:
      return TESS_POINTS;
   case SHADER_PRIM_LINE_STRIP:
      return TESS_LINES;
   case SHADER_PRIM_TRIANGLE_STRIP:
      return TESS_CW_TRIS;
   default:
      unreachable("");
   }
}

void
tu6_emit_vpc(struct tu_cs *cs,
             const struct ir3_shader_variant *vs,
             const struct ir3_shader_variant *hs,
             const struct ir3_shader_variant *ds,
             const struct ir3_shader_variant *gs,
             const struct ir3_shader_variant *fs,
             uint32_t patch_control_points)
{
   /* note: doesn't compile as static because of the array regs.. */
   const struct reg_config {
      uint16_t reg_sp_xs_out_reg;
      uint16_t reg_sp_xs_vpc_dst_reg;
      uint16_t reg_vpc_xs_pack;
      uint16_t reg_vpc_xs_clip_cntl;
      uint16_t reg_gras_xs_cl_cntl;
      uint16_t reg_pc_xs_out_cntl;
      uint16_t reg_sp_xs_primitive_cntl;
      uint16_t reg_vpc_xs_layer_cntl;
      uint16_t reg_gras_xs_layer_cntl;
   } reg_config[] = {
      [MESA_SHADER_VERTEX] = {
         REG_A6XX_SP_VS_OUT_REG(0),
         REG_A6XX_SP_VS_VPC_DST_REG(0),
         REG_A6XX_VPC_VS_PACK,
         REG_A6XX_VPC_VS_CLIP_CNTL,
         REG_A6XX_GRAS_VS_CL_CNTL,
         REG_A6XX_PC_VS_OUT_CNTL,
         REG_A6XX_SP_VS_PRIMITIVE_CNTL,
         REG_A6XX_VPC_VS_LAYER_CNTL,
         REG_A6XX_GRAS_VS_LAYER_CNTL
      },
      [MESA_SHADER_TESS_CTRL] = {
         0,
         0,
         0,
         0,
         0,
         REG_A6XX_PC_HS_OUT_CNTL,
         0,
         0,
         0
      },
      [MESA_SHADER_TESS_EVAL] = {
         REG_A6XX_SP_DS_OUT_REG(0),
         REG_A6XX_SP_DS_VPC_DST_REG(0),
         REG_A6XX_VPC_DS_PACK,
         REG_A6XX_VPC_DS_CLIP_CNTL,
         REG_A6XX_GRAS_DS_CL_CNTL,
         REG_A6XX_PC_DS_OUT_CNTL,
         REG_A6XX_SP_DS_PRIMITIVE_CNTL,
         REG_A6XX_VPC_DS_LAYER_CNTL,
         REG_A6XX_GRAS_DS_LAYER_CNTL
      },
      [MESA_SHADER_GEOMETRY] = {
         REG_A6XX_SP_GS_OUT_REG(0),
         REG_A6XX_SP_GS_VPC_DST_REG(0),
         REG_A6XX_VPC_GS_PACK,
         REG_A6XX_VPC_GS_CLIP_CNTL,
         REG_A6XX_GRAS_GS_CL_CNTL,
         REG_A6XX_PC_GS_OUT_CNTL,
         REG_A6XX_SP_GS_PRIMITIVE_CNTL,
         REG_A6XX_VPC_GS_LAYER_CNTL,
         REG_A6XX_GRAS_GS_LAYER_CNTL
      },
   };

   const struct ir3_shader_variant *last_shader;
   if (gs) {
      last_shader = gs;
   } else if (hs) {
      last_shader = ds;
   } else {
      last_shader = vs;
   }

   const struct reg_config *cfg = &reg_config[last_shader->type];

   struct ir3_shader_linkage linkage = {
      .primid_loc = 0xff,
      .clip0_loc = 0xff,
      .clip1_loc = 0xff,
   };
   if (fs)
      ir3_link_shaders(&linkage, last_shader, fs, true);

   if (last_shader->stream_output.num_outputs)
      ir3_link_stream_out(&linkage, last_shader);

   /* We do this after linking shaders in order to know whether PrimID
    * passthrough needs to be enabled.
    */
   bool primid_passthru = linkage.primid_loc != 0xff;
   tu6_emit_vs_system_values(cs, vs, hs, ds, gs, primid_passthru);

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_VAR_DISABLE(0), 4);
   tu_cs_emit(cs, ~linkage.varmask[0]);
   tu_cs_emit(cs, ~linkage.varmask[1]);
   tu_cs_emit(cs, ~linkage.varmask[2]);
   tu_cs_emit(cs, ~linkage.varmask[3]);

   /* a6xx finds position/pointsize at the end */
   const uint32_t pointsize_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_PSIZ);
   const uint32_t layer_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_LAYER);
   const uint32_t view_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_VIEWPORT);
   const uint32_t clip0_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_CLIP_DIST0);
   const uint32_t clip1_regid =
      ir3_find_output_regid(last_shader, VARYING_SLOT_CLIP_DIST1);
   uint32_t flags_regid = gs ?
      ir3_find_output_regid(gs, VARYING_SLOT_GS_VERTEX_FLAGS_IR3) : 0;

   uint32_t pointsize_loc = 0xff, position_loc = 0xff, layer_loc = 0xff, view_loc = 0xff;

   if (layer_regid != regid(63, 0)) {
      layer_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_LAYER, layer_regid, 0x1, linkage.max_loc);
   }

   if (view_regid != regid(63, 0)) {
      view_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_VIEWPORT, view_regid, 0x1, linkage.max_loc);
   }

   unsigned extra_pos = 0;

   for (unsigned i = 0; i < last_shader->outputs_count; i++) {
      if (last_shader->outputs[i].slot != VARYING_SLOT_POS)
         continue;

      if (position_loc == 0xff)
         position_loc = linkage.max_loc;

      ir3_link_add(&linkage, last_shader->outputs[i].slot,
                   last_shader->outputs[i].regid,
                   0xf, position_loc + 4 * last_shader->outputs[i].view);
      extra_pos = MAX2(extra_pos, last_shader->outputs[i].view);
   }

   if (pointsize_regid != regid(63, 0)) {
      pointsize_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_PSIZ, pointsize_regid, 0x1, linkage.max_loc);
   }

   uint8_t clip_cull_mask = last_shader->clip_mask | last_shader->cull_mask;

   /* Handle the case where clip/cull distances aren't read by the FS */
   uint32_t clip0_loc = linkage.clip0_loc, clip1_loc = linkage.clip1_loc;
   if (clip0_loc == 0xff && clip0_regid != regid(63, 0)) {
      clip0_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_CLIP_DIST0, clip0_regid,
                   clip_cull_mask & 0xf, linkage.max_loc);
   }
   if (clip1_loc == 0xff && clip1_regid != regid(63, 0)) {
      clip1_loc = linkage.max_loc;
      ir3_link_add(&linkage, VARYING_SLOT_CLIP_DIST1, clip1_regid,
                   clip_cull_mask >> 4, linkage.max_loc);
   }

   tu6_setup_streamout(cs, last_shader, &linkage);

   /* The GPU hangs on some models when there are no outputs (xs_pack::CNT),
    * at least when a DS is the last stage, so add a dummy output to keep it
    * happy if there aren't any. We do this late in order to avoid emitting
    * any unused code and make sure that optimizations don't remove it.
    */
   if (linkage.cnt == 0)
      ir3_link_add(&linkage, 0, 0, 0x1, linkage.max_loc);

   /* map outputs of the last shader to VPC */
   assert(linkage.cnt <= 32);
   const uint32_t sp_out_count = DIV_ROUND_UP(linkage.cnt, 2);
   const uint32_t sp_vpc_dst_count = DIV_ROUND_UP(linkage.cnt, 4);
   uint32_t sp_out[16] = {0};
   uint32_t sp_vpc_dst[8] = {0};
   for (uint32_t i = 0; i < linkage.cnt; i++) {
      ((uint16_t *) sp_out)[i] =
         A6XX_SP_VS_OUT_REG_A_REGID(linkage.var[i].regid) |
         A6XX_SP_VS_OUT_REG_A_COMPMASK(linkage.var[i].compmask);
      ((uint8_t *) sp_vpc_dst)[i] =
         A6XX_SP_VS_VPC_DST_REG_OUTLOC0(linkage.var[i].loc);
   }

   tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_out_reg, sp_out_count);
   tu_cs_emit_array(cs, sp_out, sp_out_count);

   tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_vpc_dst_reg, sp_vpc_dst_count);
   tu_cs_emit_array(cs, sp_vpc_dst, sp_vpc_dst_count);

   tu_cs_emit_pkt4(cs, cfg->reg_vpc_xs_pack, 1);
   tu_cs_emit(cs, A6XX_VPC_VS_PACK_POSITIONLOC(position_loc) |
                  A6XX_VPC_VS_PACK_PSIZELOC(pointsize_loc) |
                  A6XX_VPC_VS_PACK_STRIDE_IN_VPC(linkage.max_loc) |
                  A6XX_VPC_VS_PACK_EXTRAPOS(extra_pos));

   tu_cs_emit_pkt4(cs, cfg->reg_vpc_xs_clip_cntl, 1);
   tu_cs_emit(cs, A6XX_VPC_VS_CLIP_CNTL_CLIP_MASK(clip_cull_mask) |
                  A6XX_VPC_VS_CLIP_CNTL_CLIP_DIST_03_LOC(clip0_loc) |
                  A6XX_VPC_VS_CLIP_CNTL_CLIP_DIST_47_LOC(clip1_loc));

   tu_cs_emit_pkt4(cs, cfg->reg_gras_xs_cl_cntl, 1);
   tu_cs_emit(cs, A6XX_GRAS_VS_CL_CNTL_CLIP_MASK(last_shader->clip_mask) |
                  A6XX_GRAS_VS_CL_CNTL_CULL_MASK(last_shader->cull_mask));

   const struct ir3_shader_variant *geom_shaders[] = { vs, hs, ds, gs };

   for (unsigned i = 0; i < ARRAY_SIZE(geom_shaders); i++) {
      const struct ir3_shader_variant *shader = geom_shaders[i];
      if (!shader)
         continue;

      bool primid = shader->type != MESA_SHADER_VERTEX &&
         VALIDREG(ir3_find_sysval_regid(shader, SYSTEM_VALUE_PRIMITIVE_ID));

      tu_cs_emit_pkt4(cs, reg_config[shader->type].reg_pc_xs_out_cntl, 1);
      if (shader == last_shader) {
         tu_cs_emit(cs, A6XX_PC_VS_OUT_CNTL_STRIDE_IN_VPC(linkage.max_loc) |
                        CONDREG(pointsize_regid, A6XX_PC_VS_OUT_CNTL_PSIZE) |
                        CONDREG(layer_regid, A6XX_PC_VS_OUT_CNTL_LAYER) |
                        CONDREG(view_regid, A6XX_PC_VS_OUT_CNTL_VIEW) |
                        COND(primid, A6XX_PC_VS_OUT_CNTL_PRIMITIVE_ID) |
                        A6XX_PC_VS_OUT_CNTL_CLIP_MASK(clip_cull_mask));
      } else {
         tu_cs_emit(cs, COND(primid, A6XX_PC_VS_OUT_CNTL_PRIMITIVE_ID));
      }
   }

   /* if vertex_flags somehow gets optimized out, your gonna have a bad time: */
   if (gs)
      assert(flags_regid != INVALID_REG);

   tu_cs_emit_pkt4(cs, cfg->reg_sp_xs_primitive_cntl, 1);
   tu_cs_emit(cs, A6XX_SP_VS_PRIMITIVE_CNTL_OUT(linkage.cnt) |
                  A6XX_SP_GS_PRIMITIVE_CNTL_FLAGS_REGID(flags_regid));

   tu_cs_emit_pkt4(cs, cfg->reg_vpc_xs_layer_cntl, 1);
   tu_cs_emit(cs, A6XX_VPC_VS_LAYER_CNTL_LAYERLOC(layer_loc) |
                  A6XX_VPC_VS_LAYER_CNTL_VIEWLOC(view_loc));

   tu_cs_emit_pkt4(cs, cfg->reg_gras_xs_layer_cntl, 1);
   tu_cs_emit(cs, CONDREG(layer_regid, A6XX_GRAS_GS_LAYER_CNTL_WRITES_LAYER) |
                  CONDREG(view_regid, A6XX_GRAS_GS_LAYER_CNTL_WRITES_VIEW));

   tu_cs_emit_regs(cs, A6XX_PC_PRIMID_PASSTHRU(primid_passthru));

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_CNTL_0, 1);
   tu_cs_emit(cs, A6XX_VPC_CNTL_0_NUMNONPOSVAR(fs ? fs->total_in : 0) |
                  COND(fs && fs->total_in, A6XX_VPC_CNTL_0_VARYING) |
                  A6XX_VPC_CNTL_0_PRIMIDLOC(linkage.primid_loc) |
                  A6XX_VPC_CNTL_0_VIEWIDLOC(linkage.viewid_loc));

   if (hs) {
      tu_cs_emit_pkt4(cs, REG_A6XX_PC_TESS_NUM_VERTEX, 1);
      tu_cs_emit(cs, hs->tess.tcs_vertices_out);

      uint32_t patch_local_mem_size_16b =
         patch_control_points * vs->output_size / 4;

      /* Total attribute slots in HS incoming patch. */
      tu_cs_emit_pkt4(cs, REG_A6XX_PC_HS_INPUT_SIZE, 1);
      tu_cs_emit(cs, patch_local_mem_size_16b);

      const uint32_t wavesize = 64;
      const uint32_t vs_hs_local_mem_size = 16384;

      uint32_t max_patches_per_wave;
      if (cs->device->physical_device->info->a6xx.tess_use_shared) {
         /* HS invocations for a patch are always within the same wave,
          * making barriers less expensive. VS can't have barriers so we
          * don't care about VS invocations being in the same wave.
          */
         max_patches_per_wave = wavesize / hs->tess.tcs_vertices_out;
      } else {
         /* VS is also in the same wave */
         max_patches_per_wave =
            wavesize / MAX2(patch_control_points, hs->tess.tcs_vertices_out);
      }

      uint32_t patches_per_wave =
         MIN2(vs_hs_local_mem_size / (patch_local_mem_size_16b * 16),
              max_patches_per_wave);

      uint32_t wave_input_size = DIV_ROUND_UP(
         patches_per_wave * patch_local_mem_size_16b * 16, 256);

      tu_cs_emit_pkt4(cs, REG_A6XX_SP_HS_WAVE_INPUT_SIZE, 1);
      tu_cs_emit(cs, wave_input_size);

      /* In SPIR-V generated from GLSL, the tessellation primitive params are
       * are specified in the tess eval shader, but in SPIR-V generated from
       * HLSL, they are specified in the tess control shader. */
      const struct ir3_shader_variant *tess =
         ds->tess.spacing == TESS_SPACING_UNSPECIFIED ? hs : ds;
      tu_cs_emit_pkt4(cs, REG_A6XX_PC_TESS_CNTL, 1);
      uint32_t output;
      if (tess->tess.point_mode)
         output = TESS_POINTS;
      else if (tess->tess.primitive_mode == TESS_PRIMITIVE_ISOLINES)
         output = TESS_LINES;
      else if (tess->tess.ccw)
         output = TESS_CCW_TRIS;
      else
         output = TESS_CW_TRIS;

      enum a6xx_tess_spacing spacing;
      switch (tess->tess.spacing) {
      case TESS_SPACING_EQUAL:
         spacing = TESS_EQUAL;
         break;
      case TESS_SPACING_FRACTIONAL_ODD:
         spacing = TESS_FRACTIONAL_ODD;
         break;
      case TESS_SPACING_FRACTIONAL_EVEN:
         spacing = TESS_FRACTIONAL_EVEN;
         break;
      case TESS_SPACING_UNSPECIFIED:
      default:
         unreachable("invalid tess spacing");
      }
      tu_cs_emit(cs, A6XX_PC_TESS_CNTL_SPACING(spacing) |
            A6XX_PC_TESS_CNTL_OUTPUT(output));

      tu6_emit_link_map(cs, vs, hs, SB6_HS_SHADER);
      tu6_emit_link_map(cs, hs, ds, SB6_DS_SHADER);
   }


   if (gs) {
      uint32_t vertices_out, invocations, output, vec4_size;
      uint32_t prev_stage_output_size = ds ? ds->output_size : vs->output_size;

      if (hs) {
         tu6_emit_link_map(cs, ds, gs, SB6_GS_SHADER);
      } else {
         tu6_emit_link_map(cs, vs, gs, SB6_GS_SHADER);
      }
      vertices_out = gs->gs.vertices_out - 1;
      output = primitive_to_tess(gs->gs.output_primitive);
      invocations = gs->gs.invocations - 1;
      /* Size of per-primitive alloction in ldlw memory in vec4s. */
      vec4_size = gs->gs.vertices_in *
                  DIV_ROUND_UP(prev_stage_output_size, 4);

      tu_cs_emit_pkt4(cs, REG_A6XX_PC_PRIMITIVE_CNTL_5, 1);
      tu_cs_emit(cs,
            A6XX_PC_PRIMITIVE_CNTL_5_GS_VERTICES_OUT(vertices_out) |
            A6XX_PC_PRIMITIVE_CNTL_5_GS_OUTPUT(output) |
            A6XX_PC_PRIMITIVE_CNTL_5_GS_INVOCATIONS(invocations));

      tu_cs_emit_pkt4(cs, REG_A6XX_VPC_GS_PARAM, 1);
      tu_cs_emit(cs, 0xff);

      tu_cs_emit_pkt4(cs, REG_A6XX_PC_PRIMITIVE_CNTL_6, 1);
      tu_cs_emit(cs, A6XX_PC_PRIMITIVE_CNTL_6_STRIDE_IN_VPC(vec4_size));

      uint32_t prim_size = prev_stage_output_size;
      if (prim_size > 64)
         prim_size = 64;
      else if (prim_size == 64)
         prim_size = 63;
      tu_cs_emit_pkt4(cs, REG_A6XX_SP_GS_PRIM_SIZE, 1);
      tu_cs_emit(cs, prim_size);
   }
}

static int
tu6_vpc_varying_mode(const struct ir3_shader_variant *fs,
                     uint32_t index,
                     uint8_t *interp_mode,
                     uint8_t *ps_repl_mode)
{
   enum
   {
      INTERP_SMOOTH = 0,
      INTERP_FLAT = 1,
      INTERP_ZERO = 2,
      INTERP_ONE = 3,
   };
   enum
   {
      PS_REPL_NONE = 0,
      PS_REPL_S = 1,
      PS_REPL_T = 2,
      PS_REPL_ONE_MINUS_T = 3,
   };

   const uint32_t compmask = fs->inputs[index].compmask;

   /* NOTE: varyings are packed, so if compmask is 0xb then first, second, and
    * fourth component occupy three consecutive varying slots
    */
   int shift = 0;
   *interp_mode = 0;
   *ps_repl_mode = 0;
   if (fs->inputs[index].slot == VARYING_SLOT_PNTC) {
      if (compmask & 0x1) {
         *ps_repl_mode |= PS_REPL_S << shift;
         shift += 2;
      }
      if (compmask & 0x2) {
         *ps_repl_mode |= PS_REPL_T << shift;
         shift += 2;
      }
      if (compmask & 0x4) {
         *interp_mode |= INTERP_ZERO << shift;
         shift += 2;
      }
      if (compmask & 0x8) {
         *interp_mode |= INTERP_ONE << 6;
         shift += 2;
      }
   } else if (fs->inputs[index].flat) {
      for (int i = 0; i < 4; i++) {
         if (compmask & (1 << i)) {
            *interp_mode |= INTERP_FLAT << shift;
            shift += 2;
         }
      }
   }

   return shift;
}

static void
tu6_emit_vpc_varying_modes(struct tu_cs *cs,
                           const struct ir3_shader_variant *fs)
{
   uint32_t interp_modes[8] = { 0 };
   uint32_t ps_repl_modes[8] = { 0 };
   uint32_t interp_regs = 0;

   if (fs) {
      for (int i = -1;
           (i = ir3_next_varying(fs, i)) < (int) fs->inputs_count;) {

         /* get the mode for input i */
         uint8_t interp_mode;
         uint8_t ps_repl_mode;
         const int bits =
            tu6_vpc_varying_mode(fs, i, &interp_mode, &ps_repl_mode);

         /* OR the mode into the array */
         const uint32_t inloc = fs->inputs[i].inloc * 2;
         uint32_t n = inloc / 32;
         uint32_t shift = inloc % 32;
         interp_modes[n] |= interp_mode << shift;
         ps_repl_modes[n] |= ps_repl_mode << shift;
         if (shift + bits > 32) {
            n++;
            shift = 32 - shift;

            interp_modes[n] |= interp_mode >> shift;
            ps_repl_modes[n] |= ps_repl_mode >> shift;
         }
         interp_regs = MAX2(interp_regs, n + 1);
      }
   }

   if (interp_regs) {
      tu_cs_emit_pkt4(cs, REG_A6XX_VPC_VARYING_INTERP_MODE(0), interp_regs);
      tu_cs_emit_array(cs, interp_modes, interp_regs);

      tu_cs_emit_pkt4(cs, REG_A6XX_VPC_VARYING_PS_REPL_MODE(0), interp_regs);
      tu_cs_emit_array(cs, ps_repl_modes, interp_regs);
   }
}

void
tu6_emit_fs_inputs(struct tu_cs *cs, const struct ir3_shader_variant *fs)
{
   uint32_t face_regid, coord_regid, zwcoord_regid, samp_id_regid;
   uint32_t ij_regid[IJ_COUNT];
   uint32_t smask_in_regid;

   bool sample_shading = fs->per_samp | fs->key.sample_shading;
   bool enable_varyings = fs->total_in > 0;

   samp_id_regid   = ir3_find_sysval_regid(fs, SYSTEM_VALUE_SAMPLE_ID);
   smask_in_regid  = ir3_find_sysval_regid(fs, SYSTEM_VALUE_SAMPLE_MASK_IN);
   face_regid      = ir3_find_sysval_regid(fs, SYSTEM_VALUE_FRONT_FACE);
   coord_regid     = ir3_find_sysval_regid(fs, SYSTEM_VALUE_FRAG_COORD);
   zwcoord_regid   = VALIDREG(coord_regid) ? coord_regid + 2 : regid(63, 0);
   for (unsigned i = 0; i < ARRAY_SIZE(ij_regid); i++)
      ij_regid[i] = ir3_find_sysval_regid(fs, SYSTEM_VALUE_BARYCENTRIC_PERSP_PIXEL + i);

   if (fs->num_sampler_prefetch > 0) {
      assert(VALIDREG(ij_regid[IJ_PERSP_PIXEL]));
      /* also, it seems like ij_pix is *required* to be r0.x */
      assert(ij_regid[IJ_PERSP_PIXEL] == regid(0, 0));
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_PREFETCH_CNTL, 1 + fs->num_sampler_prefetch);
   tu_cs_emit(cs, A6XX_SP_FS_PREFETCH_CNTL_COUNT(fs->num_sampler_prefetch) |
         A6XX_SP_FS_PREFETCH_CNTL_UNK4(regid(63, 0)) |
         0x7000);    // XXX);
   for (int i = 0; i < fs->num_sampler_prefetch; i++) {
      const struct ir3_sampler_prefetch *prefetch = &fs->sampler_prefetch[i];
      tu_cs_emit(cs, A6XX_SP_FS_PREFETCH_CMD_SRC(prefetch->src) |
                     A6XX_SP_FS_PREFETCH_CMD_SAMP_ID(prefetch->samp_id) |
                     A6XX_SP_FS_PREFETCH_CMD_TEX_ID(prefetch->tex_id) |
                     A6XX_SP_FS_PREFETCH_CMD_DST(prefetch->dst) |
                     A6XX_SP_FS_PREFETCH_CMD_WRMASK(prefetch->wrmask) |
                     COND(prefetch->half_precision, A6XX_SP_FS_PREFETCH_CMD_HALF) |
                     A6XX_SP_FS_PREFETCH_CMD_CMD(prefetch->cmd));
   }

   if (fs->num_sampler_prefetch > 0) {
      tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_BINDLESS_PREFETCH_CMD(0), fs->num_sampler_prefetch);
      for (int i = 0; i < fs->num_sampler_prefetch; i++) {
         const struct ir3_sampler_prefetch *prefetch = &fs->sampler_prefetch[i];
         tu_cs_emit(cs,
                    A6XX_SP_FS_BINDLESS_PREFETCH_CMD_SAMP_ID(prefetch->samp_bindless_id) |
                    A6XX_SP_FS_BINDLESS_PREFETCH_CMD_TEX_ID(prefetch->tex_bindless_id));
      }
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_CONTROL_1_REG, 5);
   tu_cs_emit(cs, 0x7);
   tu_cs_emit(cs, A6XX_HLSQ_CONTROL_2_REG_FACEREGID(face_regid) |
                  A6XX_HLSQ_CONTROL_2_REG_SAMPLEID(samp_id_regid) |
                  A6XX_HLSQ_CONTROL_2_REG_SAMPLEMASK(smask_in_regid) |
                  A6XX_HLSQ_CONTROL_2_REG_CENTERRHW(ij_regid[IJ_PERSP_CENTER_RHW]));
   tu_cs_emit(cs, A6XX_HLSQ_CONTROL_3_REG_IJ_PERSP_PIXEL(ij_regid[IJ_PERSP_PIXEL]) |
                  A6XX_HLSQ_CONTROL_3_REG_IJ_LINEAR_PIXEL(ij_regid[IJ_LINEAR_PIXEL]) |
                  A6XX_HLSQ_CONTROL_3_REG_IJ_PERSP_CENTROID(ij_regid[IJ_PERSP_CENTROID]) |
                  A6XX_HLSQ_CONTROL_3_REG_IJ_LINEAR_CENTROID(ij_regid[IJ_LINEAR_CENTROID]));
   tu_cs_emit(cs, A6XX_HLSQ_CONTROL_4_REG_XYCOORDREGID(coord_regid) |
                  A6XX_HLSQ_CONTROL_4_REG_ZWCOORDREGID(zwcoord_regid) |
                  A6XX_HLSQ_CONTROL_4_REG_IJ_PERSP_SAMPLE(ij_regid[IJ_PERSP_SAMPLE]) |
                  A6XX_HLSQ_CONTROL_4_REG_IJ_LINEAR_SAMPLE(ij_regid[IJ_LINEAR_SAMPLE]));
   tu_cs_emit(cs, 0xfcfc);

   enum a6xx_threadsize thrsz = fs->info.double_threadsize ? THREAD128 : THREAD64;
   tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_FS_CNTL_0, 1);
   tu_cs_emit(cs, A6XX_HLSQ_FS_CNTL_0_THREADSIZE(thrsz) |
                  COND(enable_varyings, A6XX_HLSQ_FS_CNTL_0_VARYINGS));

   bool need_size = fs->frag_face || fs->fragcoord_compmask != 0;
   bool need_size_persamp = false;
   if (VALIDREG(ij_regid[IJ_PERSP_CENTER_RHW])) {
      if (sample_shading)
         need_size_persamp = true;
      else
         need_size = true;
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_CNTL, 1);
   tu_cs_emit(cs,
         CONDREG(ij_regid[IJ_PERSP_PIXEL], A6XX_GRAS_CNTL_IJ_PERSP_PIXEL) |
         CONDREG(ij_regid[IJ_PERSP_CENTROID], A6XX_GRAS_CNTL_IJ_PERSP_CENTROID) |
         CONDREG(ij_regid[IJ_PERSP_SAMPLE], A6XX_GRAS_CNTL_IJ_PERSP_SAMPLE) |
         CONDREG(ij_regid[IJ_LINEAR_PIXEL], A6XX_GRAS_CNTL_IJ_LINEAR_PIXEL) |
         CONDREG(ij_regid[IJ_LINEAR_CENTROID], A6XX_GRAS_CNTL_IJ_LINEAR_CENTROID) |
         CONDREG(ij_regid[IJ_LINEAR_SAMPLE], A6XX_GRAS_CNTL_IJ_LINEAR_SAMPLE) |
         COND(need_size, A6XX_GRAS_CNTL_IJ_LINEAR_PIXEL) |
         COND(need_size_persamp, A6XX_GRAS_CNTL_IJ_LINEAR_SAMPLE) |
         COND(fs->fragcoord_compmask != 0, A6XX_GRAS_CNTL_COORD_MASK(fs->fragcoord_compmask)));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_RENDER_CONTROL0, 2);
   tu_cs_emit(cs,
         CONDREG(ij_regid[IJ_PERSP_PIXEL], A6XX_RB_RENDER_CONTROL0_IJ_PERSP_PIXEL) |
         CONDREG(ij_regid[IJ_PERSP_CENTROID], A6XX_RB_RENDER_CONTROL0_IJ_PERSP_CENTROID) |
         CONDREG(ij_regid[IJ_PERSP_SAMPLE], A6XX_RB_RENDER_CONTROL0_IJ_PERSP_SAMPLE) |
         CONDREG(ij_regid[IJ_LINEAR_PIXEL], A6XX_RB_RENDER_CONTROL0_IJ_LINEAR_PIXEL) |
         CONDREG(ij_regid[IJ_LINEAR_CENTROID], A6XX_RB_RENDER_CONTROL0_IJ_LINEAR_CENTROID) |
         CONDREG(ij_regid[IJ_LINEAR_SAMPLE], A6XX_RB_RENDER_CONTROL0_IJ_LINEAR_SAMPLE) |
         COND(need_size, A6XX_RB_RENDER_CONTROL0_IJ_LINEAR_PIXEL) |
         COND(enable_varyings, A6XX_RB_RENDER_CONTROL0_UNK10) |
         COND(need_size_persamp, A6XX_RB_RENDER_CONTROL0_IJ_LINEAR_SAMPLE) |
         COND(fs->fragcoord_compmask != 0,
                           A6XX_RB_RENDER_CONTROL0_COORD_MASK(fs->fragcoord_compmask)));
   tu_cs_emit(cs,
         A6XX_RB_RENDER_CONTROL1_FRAGCOORDSAMPLEMODE(
            sample_shading ? FRAGCOORD_SAMPLE : FRAGCOORD_CENTER) |
         CONDREG(smask_in_regid, A6XX_RB_RENDER_CONTROL1_SAMPLEMASK) |
         CONDREG(samp_id_regid, A6XX_RB_RENDER_CONTROL1_SAMPLEID) |
         CONDREG(ij_regid[IJ_PERSP_CENTER_RHW], A6XX_RB_RENDER_CONTROL1_CENTERRHW) |
         COND(fs->frag_face, A6XX_RB_RENDER_CONTROL1_FACENESS));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_SAMPLE_CNTL, 1);
   tu_cs_emit(cs, COND(sample_shading, A6XX_RB_SAMPLE_CNTL_PER_SAMP_MODE));

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_LRZ_PS_INPUT_CNTL, 1);
   tu_cs_emit(cs, CONDREG(samp_id_regid, A6XX_GRAS_LRZ_PS_INPUT_CNTL_SAMPLEID) |
              A6XX_GRAS_LRZ_PS_INPUT_CNTL_FRAGCOORDSAMPLEMODE(
                 sample_shading ? FRAGCOORD_SAMPLE : FRAGCOORD_CENTER));

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SAMPLE_CNTL, 1);
   tu_cs_emit(cs, COND(sample_shading, A6XX_GRAS_SAMPLE_CNTL_PER_SAMP_MODE));
}

static void
tu6_emit_fs_outputs(struct tu_cs *cs,
                    const struct ir3_shader_variant *fs,
                    uint32_t mrt_count, bool dual_src_blend,
                    uint32_t render_components,
                    bool no_earlyz,
                    struct tu_pipeline *pipeline)
{
   uint32_t smask_regid, posz_regid, stencilref_regid;

   posz_regid      = ir3_find_output_regid(fs, FRAG_RESULT_DEPTH);
   smask_regid     = ir3_find_output_regid(fs, FRAG_RESULT_SAMPLE_MASK);
   stencilref_regid = ir3_find_output_regid(fs, FRAG_RESULT_STENCIL);

   int output_reg_count = MAX2(mrt_count, 1);
   uint32_t fragdata_regid[output_reg_count];
   if (fs->color0_mrt) {
      fragdata_regid[0] = ir3_find_output_regid(fs, FRAG_RESULT_COLOR);
      for (uint32_t i = 1; i < output_reg_count; i++)
         fragdata_regid[i] = fragdata_regid[0];
   } else {
      for (uint32_t i = 0; i < output_reg_count; i++)
         fragdata_regid[i] = ir3_find_output_regid(fs, FRAG_RESULT_DATA0 + i);
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_OUTPUT_CNTL0, 2);
   tu_cs_emit(cs, A6XX_SP_FS_OUTPUT_CNTL0_DEPTH_REGID(posz_regid) |
                  A6XX_SP_FS_OUTPUT_CNTL0_SAMPMASK_REGID(smask_regid) |
                  A6XX_SP_FS_OUTPUT_CNTL0_STENCILREF_REGID(stencilref_regid) |
                  COND(dual_src_blend, A6XX_SP_FS_OUTPUT_CNTL0_DUAL_COLOR_IN_ENABLE));
   tu_cs_emit(cs, A6XX_SP_FS_OUTPUT_CNTL1_MRT(mrt_count));

   uint32_t fs_render_components = 0;

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_OUTPUT_REG(0), output_reg_count);
   for (uint32_t i = 0; i < output_reg_count; i++) {
      tu_cs_emit(cs, A6XX_SP_FS_OUTPUT_REG_REGID(fragdata_regid[i]) |
                     (COND(fragdata_regid[i] & HALF_REG_ID,
                           A6XX_SP_FS_OUTPUT_REG_HALF_PRECISION)));

      if (VALIDREG(fragdata_regid[i])) {
         fs_render_components |= 0xf << (i * 4);
      }
   }

   /* dual source blending has an extra fs output in the 2nd slot */
   if (dual_src_blend) {
      fs_render_components |= 0xf << 4;
   }

   /* There is no point in having component enabled which is not written
    * by the shader. Per VK spec it is an UB, however a few apps depend on
    * attachment not being changed if FS doesn't have corresponding output.
    */
   fs_render_components &= render_components;

   tu_cs_emit_regs(cs,
                   A6XX_SP_FS_RENDER_COMPONENTS(.dword = fs_render_components));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_FS_OUTPUT_CNTL0, 2);
   tu_cs_emit(cs, COND(fs->writes_pos, A6XX_RB_FS_OUTPUT_CNTL0_FRAG_WRITES_Z) |
                  COND(fs->writes_smask, A6XX_RB_FS_OUTPUT_CNTL0_FRAG_WRITES_SAMPMASK) |
                  COND(fs->writes_stencilref, A6XX_RB_FS_OUTPUT_CNTL0_FRAG_WRITES_STENCILREF) |
                  COND(dual_src_blend, A6XX_RB_FS_OUTPUT_CNTL0_DUAL_COLOR_IN_ENABLE));
   tu_cs_emit(cs, A6XX_RB_FS_OUTPUT_CNTL1_MRT(mrt_count));

   tu_cs_emit_regs(cs,
                   A6XX_RB_RENDER_COMPONENTS(.dword = fs_render_components));

   if (pipeline) {
      pipeline->lrz.fs_has_kill = fs->has_kill;
      pipeline->lrz.early_fragment_tests = fs->fs.early_fragment_tests;

      if (!fs->fs.early_fragment_tests &&
          (fs->no_earlyz || fs->has_kill || fs->writes_pos || fs->writes_stencilref || no_earlyz || fs->writes_smask)) {
         pipeline->lrz.force_late_z = true;
      }
   }
}

static void
tu6_emit_geom_tess_consts(struct tu_cs *cs,
                          const struct ir3_shader_variant *vs,
                          const struct ir3_shader_variant *hs,
                          const struct ir3_shader_variant *ds,
                          const struct ir3_shader_variant *gs,
                          uint32_t cps_per_patch)
{
   struct tu_device *dev = cs->device;

   uint32_t num_vertices =
         hs ? cps_per_patch : gs->gs.vertices_in;

   uint32_t vs_params[4] = {
      vs->output_size * num_vertices * 4,  /* vs primitive stride */
      vs->output_size * 4,                 /* vs vertex stride */
      0,
      0,
   };
   uint32_t vs_base = ir3_const_state(vs)->offsets.primitive_param;
   tu6_emit_const(cs, CP_LOAD_STATE6_GEOM, vs_base, SB6_VS_SHADER, 0,
                  ARRAY_SIZE(vs_params), vs_params);

   if (hs) {
      assert(ds->type != MESA_SHADER_NONE);

      /* Create the shared tess factor BO the first time tess is used on the device. */
      mtx_lock(&dev->mutex);
      if (!dev->tess_bo)
         tu_bo_init_new(dev, &dev->tess_bo, TU_TESS_BO_SIZE, TU_BO_ALLOC_NO_FLAGS);
      mtx_unlock(&dev->mutex);

      uint64_t tess_factor_iova = dev->tess_bo->iova;
      uint64_t tess_param_iova = tess_factor_iova + TU_TESS_FACTOR_SIZE;

      uint32_t hs_params[8] = {
         vs->output_size * num_vertices * 4,  /* hs primitive stride */
         vs->output_size * 4,                 /* hs vertex stride */
         hs->output_size,
         cps_per_patch,
         tess_param_iova,
         tess_param_iova >> 32,
         tess_factor_iova,
         tess_factor_iova >> 32,
      };

      uint32_t hs_base = hs->const_state->offsets.primitive_param;
      uint32_t hs_param_dwords = MIN2((hs->constlen - hs_base) * 4, ARRAY_SIZE(hs_params));
      tu6_emit_const(cs, CP_LOAD_STATE6_GEOM, hs_base, SB6_HS_SHADER, 0,
                     hs_param_dwords, hs_params);
      if (gs)
         num_vertices = gs->gs.vertices_in;

      uint32_t ds_params[8] = {
         ds->output_size * num_vertices * 4,  /* ds primitive stride */
         ds->output_size * 4,                 /* ds vertex stride */
         hs->output_size,                     /* hs vertex stride (dwords) */
         hs->tess.tcs_vertices_out,
         tess_param_iova,
         tess_param_iova >> 32,
         tess_factor_iova,
         tess_factor_iova >> 32,
      };

      uint32_t ds_base = ds->const_state->offsets.primitive_param;
      uint32_t ds_param_dwords = MIN2((ds->constlen - ds_base) * 4, ARRAY_SIZE(ds_params));
      tu6_emit_const(cs, CP_LOAD_STATE6_GEOM, ds_base, SB6_DS_SHADER, 0,
                     ds_param_dwords, ds_params);
   }

   if (gs) {
      const struct ir3_shader_variant *prev = ds ? ds : vs;
      uint32_t gs_params[4] = {
         prev->output_size * num_vertices * 4,  /* gs primitive stride */
         prev->output_size * 4,                 /* gs vertex stride */
         0,
         0,
      };
      uint32_t gs_base = gs->const_state->offsets.primitive_param;
      tu6_emit_const(cs, CP_LOAD_STATE6_GEOM, gs_base, SB6_GS_SHADER, 0,
                     ARRAY_SIZE(gs_params), gs_params);
   }
}

static void
tu6_emit_program_config(struct tu_cs *cs,
                        struct tu_pipeline_builder *builder)
{
   gl_shader_stage stage = MESA_SHADER_VERTEX;

   STATIC_ASSERT(MESA_SHADER_VERTEX == 0);

   bool shared_consts_enable = tu6_shared_constants_enable(builder->layout,
         builder->device->compiler);
   tu6_emit_shared_consts_enable(cs, shared_consts_enable);

   tu_cs_emit_regs(cs, A6XX_HLSQ_INVALIDATE_CMD(
         .vs_state = true,
         .hs_state = true,
         .ds_state = true,
         .gs_state = true,
         .fs_state = true,
         .gfx_ibo = true,
         .gfx_shared_const = shared_consts_enable));
   for (; stage < ARRAY_SIZE(builder->shader_iova); stage++) {
      tu6_emit_xs_config(cs, stage, builder->shaders->variants[stage]);
   }
}

static void
tu6_emit_program(struct tu_cs *cs,
                 struct tu_pipeline_builder *builder,
                 bool binning_pass,
                 struct tu_pipeline *pipeline)
{
   const struct ir3_shader_variant *vs = builder->shaders->variants[MESA_SHADER_VERTEX];
   const struct ir3_shader_variant *bs = builder->binning_variant;
   const struct ir3_shader_variant *hs = builder->shaders->variants[MESA_SHADER_TESS_CTRL];
   const struct ir3_shader_variant *ds = builder->shaders->variants[MESA_SHADER_TESS_EVAL];
   const struct ir3_shader_variant *gs = builder->shaders->variants[MESA_SHADER_GEOMETRY];
   const struct ir3_shader_variant *fs = builder->shaders->variants[MESA_SHADER_FRAGMENT];
   gl_shader_stage stage = MESA_SHADER_VERTEX;
   uint32_t cps_per_patch = builder->create_info->pTessellationState ?
      builder->create_info->pTessellationState->patchControlPoints : 0;
   bool multi_pos_output = builder->shaders->multi_pos_output;

  /* Don't use the binning pass variant when GS is present because we don't
   * support compiling correct binning pass variants with GS.
   */
   if (binning_pass && !gs) {
      vs = bs;
      tu6_emit_xs(cs, stage, bs, &builder->pvtmem, builder->binning_vs_iova);
      stage++;
   }

   for (; stage < ARRAY_SIZE(builder->shader_iova); stage++) {
      const struct ir3_shader_variant *xs = builder->shaders->variants[stage];

      if (stage == MESA_SHADER_FRAGMENT && binning_pass)
         fs = xs = NULL;

      tu6_emit_xs(cs, stage, xs, &builder->pvtmem, builder->shader_iova[stage]);
   }

   uint32_t multiview_views = util_logbase2(builder->multiview_mask) + 1;
   uint32_t multiview_cntl = builder->multiview_mask ?
      A6XX_PC_MULTIVIEW_CNTL_ENABLE |
      A6XX_PC_MULTIVIEW_CNTL_VIEWS(multiview_views) |
      COND(!multi_pos_output, A6XX_PC_MULTIVIEW_CNTL_DISABLEMULTIPOS)
      : 0;

   /* Copy what the blob does here. This will emit an extra 0x3f
    * CP_EVENT_WRITE when multiview is disabled. I'm not exactly sure what
    * this is working around yet.
    */
   if (builder->device->physical_device->info->a6xx.has_cp_reg_write) {
      tu_cs_emit_pkt7(cs, CP_REG_WRITE, 3);
      tu_cs_emit(cs, CP_REG_WRITE_0_TRACKER(UNK_EVENT_WRITE));
      tu_cs_emit(cs, REG_A6XX_PC_MULTIVIEW_CNTL);
   } else {
      tu_cs_emit_pkt4(cs, REG_A6XX_PC_MULTIVIEW_CNTL, 1);
   }
   tu_cs_emit(cs, multiview_cntl);

   tu_cs_emit_pkt4(cs, REG_A6XX_VFD_MULTIVIEW_CNTL, 1);
   tu_cs_emit(cs, multiview_cntl);

   if (multiview_cntl &&
       builder->device->physical_device->info->a6xx.supports_multiview_mask) {
      tu_cs_emit_pkt4(cs, REG_A6XX_PC_MULTIVIEW_MASK, 1);
      tu_cs_emit(cs, builder->multiview_mask);
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_HS_WAVE_INPUT_SIZE, 1);
   tu_cs_emit(cs, 0);

   tu6_emit_vfd_dest(cs, vs);

   tu6_emit_vpc(cs, vs, hs, ds, gs, fs, cps_per_patch);
   tu6_emit_vpc_varying_modes(cs, fs);

   bool no_earlyz = builder->depth_attachment_format == VK_FORMAT_S8_UINT;
   uint32_t mrt_count = builder->color_attachment_count;
   uint32_t render_components = builder->render_components;

   if (builder->alpha_to_coverage) {
      /* alpha to coverage can behave like a discard */
      no_earlyz = true;
      /* alpha value comes from first mrt */
      render_components |= 0xf;
      if (!mrt_count) {
         mrt_count = 1;
         /* Disable memory write for dummy mrt because it doesn't get set otherwise */
         tu_cs_emit_regs(cs, A6XX_RB_MRT_CONTROL(0, .component_enable = 0));
      }
   }

   if (fs) {
      tu6_emit_fs_inputs(cs, fs);
      tu6_emit_fs_outputs(cs, fs, mrt_count,
                          builder->use_dual_src_blend,
                          render_components,
                          no_earlyz,
                          pipeline);
   } else {
      /* TODO: check if these can be skipped if fs is disabled */
      struct ir3_shader_variant dummy_variant = {};
      tu6_emit_fs_inputs(cs, &dummy_variant);
      tu6_emit_fs_outputs(cs, &dummy_variant, mrt_count,
                          builder->use_dual_src_blend,
                          render_components,
                          no_earlyz,
                          NULL);
   }

   if (gs || hs) {
      tu6_emit_geom_tess_consts(cs, vs, hs, ds, gs, cps_per_patch);
   }
}

void
tu6_emit_vertex_input(struct tu_cs *cs,
                      uint32_t binding_count,
                      const VkVertexInputBindingDescription2EXT *bindings,
                      uint32_t unsorted_attr_count,
                      const VkVertexInputAttributeDescription2EXT *unsorted_attrs)
{
   uint32_t binding_instanced = 0; /* bitmask of instanced bindings */
   uint32_t step_rate[MAX_VBS];

   for (uint32_t i = 0; i < binding_count; i++) {
      const VkVertexInputBindingDescription2EXT *binding = &bindings[i];

      if (binding->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE)
         binding_instanced |= 1u << binding->binding;

      step_rate[binding->binding] = binding->divisor;
   }

   const VkVertexInputAttributeDescription2EXT *attrs[MAX_VERTEX_ATTRIBS] = { };
   unsigned attr_count = 0;
   for (uint32_t i = 0; i < unsorted_attr_count; i++) {
      const VkVertexInputAttributeDescription2EXT *attr = &unsorted_attrs[i];
      attrs[attr->location] = attr;
      attr_count = MAX2(attr_count, attr->location + 1);
   }

   if (attr_count != 0)
      tu_cs_emit_pkt4(cs, REG_A6XX_VFD_DECODE_INSTR(0), attr_count * 2);

   for (uint32_t loc = 0; loc < attr_count; loc++) {
      const VkVertexInputAttributeDescription2EXT *attr = attrs[loc];

      if (attr) {
         const struct tu_native_format format = tu6_format_vtx(attr->format);
         tu_cs_emit(cs, A6XX_VFD_DECODE_INSTR(0,
                          .idx = attr->binding,
                          .offset = attr->offset,
                          .instanced = binding_instanced & (1 << attr->binding),
                          .format = format.fmt,
                          .swap = format.swap,
                          .unk30 = 1,
                          ._float = !vk_format_is_int(attr->format)).value);
         tu_cs_emit(cs, A6XX_VFD_DECODE_STEP_RATE(0, step_rate[attr->binding]).value);
      } else {
         tu_cs_emit(cs, 0);
         tu_cs_emit(cs, 0);
      }
   }
}

void
tu6_emit_viewport(struct tu_cs *cs, const VkViewport *viewports, uint32_t num_viewport,
                  bool z_negative_one_to_one)
{
   VkExtent2D guardband = {511, 511};

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_CL_VPORT_XOFFSET(0), num_viewport * 6);
   for (uint32_t i = 0; i < num_viewport; i++) {
      const VkViewport *viewport = &viewports[i];
      float offsets[3];
      float scales[3];
      scales[0] = viewport->width / 2.0f;
      scales[1] = viewport->height / 2.0f;
      if (z_negative_one_to_one) {
         scales[2] = 0.5 * (viewport->maxDepth - viewport->minDepth);
      } else {
         scales[2] = viewport->maxDepth - viewport->minDepth;
      }

      offsets[0] = viewport->x + scales[0];
      offsets[1] = viewport->y + scales[1];
      if (z_negative_one_to_one) {
         offsets[2] = 0.5 * (viewport->minDepth + viewport->maxDepth);
      } else {
         offsets[2] = viewport->minDepth;
      }

      for (uint32_t j = 0; j < 3; j++) {
         tu_cs_emit(cs, fui(offsets[j]));
         tu_cs_emit(cs, fui(scales[j]));
      }

      guardband.width =
         MIN2(guardband.width, fd_calc_guardband(offsets[0], scales[0], false));
      guardband.height =
         MIN2(guardband.height, fd_calc_guardband(offsets[1], scales[1], false));
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL(0), num_viewport * 2);
   for (uint32_t i = 0; i < num_viewport; i++) {
      const VkViewport *viewport = &viewports[i];
      VkOffset2D min;
      VkOffset2D max;
      min.x = (int32_t) viewport->x;
      max.x = (int32_t) ceilf(viewport->x + viewport->width);
      if (viewport->height >= 0.0f) {
         min.y = (int32_t) viewport->y;
         max.y = (int32_t) ceilf(viewport->y + viewport->height);
      } else {
         min.y = (int32_t)(viewport->y + viewport->height);
         max.y = (int32_t) ceilf(viewport->y);
      }
      /* the spec allows viewport->height to be 0.0f */
      if (min.y == max.y)
         max.y++;
      /* allow viewport->width = 0.0f for un-initialized viewports: */
      if (min.x == max.x)
         max.x++;

      min.x = MAX2(min.x, 0);
      min.y = MAX2(min.y, 0);
      max.x = MAX2(max.x, 1);
      max.y = MAX2(max.y, 1);

      assert(min.x < max.x);
      assert(min.y < max.y);

      tu_cs_emit(cs, A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL_X(min.x) |
                     A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL_Y(min.y));
      tu_cs_emit(cs, A6XX_GRAS_SC_VIEWPORT_SCISSOR_BR_X(max.x - 1) |
                     A6XX_GRAS_SC_VIEWPORT_SCISSOR_BR_Y(max.y - 1));
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_CL_Z_CLAMP(0), num_viewport * 2);
   for (uint32_t i = 0; i < num_viewport; i++) {
      const VkViewport *viewport = &viewports[i];
      tu_cs_emit(cs, fui(MIN2(viewport->minDepth, viewport->maxDepth)));
      tu_cs_emit(cs, fui(MAX2(viewport->minDepth, viewport->maxDepth)));
   }
   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_CL_GUARDBAND_CLIP_ADJ, 1);
   tu_cs_emit(cs, A6XX_GRAS_CL_GUARDBAND_CLIP_ADJ_HORZ(guardband.width) |
                  A6XX_GRAS_CL_GUARDBAND_CLIP_ADJ_VERT(guardband.height));

   /* TODO: what to do about this and multi viewport ? */
   float z_clamp_min = num_viewport ? MIN2(viewports[0].minDepth, viewports[0].maxDepth) : 0;
   float z_clamp_max = num_viewport ? MAX2(viewports[0].minDepth, viewports[0].maxDepth) : 0;

   tu_cs_emit_regs(cs,
                   A6XX_RB_Z_CLAMP_MIN(z_clamp_min),
                   A6XX_RB_Z_CLAMP_MAX(z_clamp_max));
}

void
tu6_emit_scissor(struct tu_cs *cs, const VkRect2D *scissors, uint32_t scissor_count)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SC_SCREEN_SCISSOR_TL(0), scissor_count * 2);

   for (uint32_t i = 0; i < scissor_count; i++) {
      const VkRect2D *scissor = &scissors[i];

      uint32_t min_x = scissor->offset.x;
      uint32_t min_y = scissor->offset.y;
      uint32_t max_x = min_x + scissor->extent.width - 1;
      uint32_t max_y = min_y + scissor->extent.height - 1;

      if (!scissor->extent.width || !scissor->extent.height) {
         min_x = min_y = 1;
         max_x = max_y = 0;
      } else {
         /* avoid overflow */
         uint32_t scissor_max = BITFIELD_MASK(15);
         min_x = MIN2(scissor_max, min_x);
         min_y = MIN2(scissor_max, min_y);
         max_x = MIN2(scissor_max, max_x);
         max_y = MIN2(scissor_max, max_y);
      }

      tu_cs_emit(cs, A6XX_GRAS_SC_SCREEN_SCISSOR_TL_X(min_x) |
                     A6XX_GRAS_SC_SCREEN_SCISSOR_TL_Y(min_y));
      tu_cs_emit(cs, A6XX_GRAS_SC_SCREEN_SCISSOR_BR_X(max_x) |
                     A6XX_GRAS_SC_SCREEN_SCISSOR_BR_Y(max_y));
   }
}

void
tu6_emit_sample_locations(struct tu_cs *cs, const VkSampleLocationsInfoEXT *samp_loc)
{
   if (!samp_loc) {
      tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SAMPLE_CONFIG, 1);
      tu_cs_emit(cs, 0);

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_SAMPLE_CONFIG, 1);
      tu_cs_emit(cs, 0);

      tu_cs_emit_pkt4(cs, REG_A6XX_SP_TP_SAMPLE_CONFIG, 1);
      tu_cs_emit(cs, 0);
      return;
   }

   assert(samp_loc->sampleLocationsPerPixel == samp_loc->sampleLocationsCount);
   assert(samp_loc->sampleLocationGridSize.width == 1);
   assert(samp_loc->sampleLocationGridSize.height == 1);

   uint32_t sample_config =
      A6XX_RB_SAMPLE_CONFIG_LOCATION_ENABLE;
   uint32_t sample_locations = 0;
   for (uint32_t i = 0; i < samp_loc->sampleLocationsCount; i++) {
      sample_locations |=
         (A6XX_RB_SAMPLE_LOCATION_0_SAMPLE_0_X(samp_loc->pSampleLocations[i].x) |
          A6XX_RB_SAMPLE_LOCATION_0_SAMPLE_0_Y(samp_loc->pSampleLocations[i].y)) << i*8;
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SAMPLE_CONFIG, 2);
   tu_cs_emit(cs, sample_config);
   tu_cs_emit(cs, sample_locations);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_SAMPLE_CONFIG, 2);
   tu_cs_emit(cs, sample_config);
   tu_cs_emit(cs, sample_locations);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_TP_SAMPLE_CONFIG, 2);
   tu_cs_emit(cs, sample_config);
   tu_cs_emit(cs, sample_locations);
}

static uint32_t
tu6_gras_su_cntl(const VkPipelineRasterizationStateCreateInfo *rast_info,
                 enum a5xx_line_mode line_mode,
                 bool multiview)
{
   uint32_t gras_su_cntl = 0;

   if (rast_info->cullMode & VK_CULL_MODE_FRONT_BIT)
      gras_su_cntl |= A6XX_GRAS_SU_CNTL_CULL_FRONT;
   if (rast_info->cullMode & VK_CULL_MODE_BACK_BIT)
      gras_su_cntl |= A6XX_GRAS_SU_CNTL_CULL_BACK;

   if (rast_info->frontFace == VK_FRONT_FACE_CLOCKWISE)
      gras_su_cntl |= A6XX_GRAS_SU_CNTL_FRONT_CW;

   gras_su_cntl |=
      A6XX_GRAS_SU_CNTL_LINEHALFWIDTH(rast_info->lineWidth / 2.0f);

   if (rast_info->depthBiasEnable)
      gras_su_cntl |= A6XX_GRAS_SU_CNTL_POLY_OFFSET;

   gras_su_cntl |= A6XX_GRAS_SU_CNTL_LINE_MODE(line_mode);

   if (multiview) {
      gras_su_cntl |=
         A6XX_GRAS_SU_CNTL_UNK17 |
         A6XX_GRAS_SU_CNTL_MULTIVIEW_ENABLE;
   }

   return gras_su_cntl;
}

void
tu6_emit_depth_bias(struct tu_cs *cs,
                    float constant_factor,
                    float clamp,
                    float slope_factor)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SU_POLY_OFFSET_SCALE, 3);
   tu_cs_emit(cs, A6XX_GRAS_SU_POLY_OFFSET_SCALE(slope_factor).value);
   tu_cs_emit(cs, A6XX_GRAS_SU_POLY_OFFSET_OFFSET(constant_factor).value);
   tu_cs_emit(cs, A6XX_GRAS_SU_POLY_OFFSET_OFFSET_CLAMP(clamp).value);
}

static uint32_t
tu6_rb_mrt_blend_control(const VkPipelineColorBlendAttachmentState *att,
                         bool has_alpha)
{
   const enum a3xx_rb_blend_opcode color_op = tu6_blend_op(att->colorBlendOp);
   const enum adreno_rb_blend_factor src_color_factor = tu6_blend_factor(
      has_alpha ? att->srcColorBlendFactor
                : tu_blend_factor_no_dst_alpha(att->srcColorBlendFactor));
   const enum adreno_rb_blend_factor dst_color_factor = tu6_blend_factor(
      has_alpha ? att->dstColorBlendFactor
                : tu_blend_factor_no_dst_alpha(att->dstColorBlendFactor));
   const enum a3xx_rb_blend_opcode alpha_op = tu6_blend_op(att->alphaBlendOp);
   const enum adreno_rb_blend_factor src_alpha_factor =
      tu6_blend_factor(att->srcAlphaBlendFactor);
   const enum adreno_rb_blend_factor dst_alpha_factor =
      tu6_blend_factor(att->dstAlphaBlendFactor);

   return A6XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(src_color_factor) |
          A6XX_RB_MRT_BLEND_CONTROL_RGB_BLEND_OPCODE(color_op) |
          A6XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(dst_color_factor) |
          A6XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(src_alpha_factor) |
          A6XX_RB_MRT_BLEND_CONTROL_ALPHA_BLEND_OPCODE(alpha_op) |
          A6XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(dst_alpha_factor);
}

static uint32_t
tu6_rb_mrt_control(const VkPipelineColorBlendAttachmentState *att,
                   uint32_t rb_mrt_control_rop,
                   bool has_alpha)
{
   uint32_t rb_mrt_control =
      A6XX_RB_MRT_CONTROL_COMPONENT_ENABLE(att->colorWriteMask);

   rb_mrt_control |= rb_mrt_control_rop;

   if (att->blendEnable) {
      rb_mrt_control |= A6XX_RB_MRT_CONTROL_BLEND;

      if (has_alpha)
         rb_mrt_control |= A6XX_RB_MRT_CONTROL_BLEND2;
   }

   return rb_mrt_control;
}

uint32_t
tu6_rb_mrt_control_rop(VkLogicOp op, bool *rop_reads_dst)
{
   *rop_reads_dst = tu_logic_op_reads_dst(op);
   return A6XX_RB_MRT_CONTROL_ROP_ENABLE |
          A6XX_RB_MRT_CONTROL_ROP_CODE(tu6_rop(op));
}

static void
tu6_emit_rb_mrt_controls(struct tu_pipeline *pipeline,
                         const VkPipelineColorBlendStateCreateInfo *blend_info,
                         const VkFormat attachment_formats[MAX_RTS],
                         bool *rop_reads_dst,
                         uint32_t *color_bandwidth_per_sample)
{
   const VkPipelineColorWriteCreateInfoEXT *color_info =
      vk_find_struct_const(blend_info->pNext,
                           PIPELINE_COLOR_WRITE_CREATE_INFO_EXT);

   /* The static state is ignored if it's dynamic. In that case assume
    * everything is enabled and then the appropriate registers will be zero'd
    * dynamically.
    */
   if (pipeline->dynamic_state_mask & BIT(TU_DYNAMIC_STATE_COLOR_WRITE_ENABLE))
      color_info = NULL;

   *rop_reads_dst = false;
   *color_bandwidth_per_sample = 0;

   uint32_t rb_mrt_control_rop = 0;
   if (blend_info->logicOpEnable) {
      pipeline->logic_op_enabled = true;
      rb_mrt_control_rop = tu6_rb_mrt_control_rop(blend_info->logicOp,
                                                  rop_reads_dst);
   }

   uint32_t total_bpp = 0;
   pipeline->num_rts = blend_info->attachmentCount;
   for (uint32_t i = 0; i < blend_info->attachmentCount; i++) {
      const VkPipelineColorBlendAttachmentState *att =
         &blend_info->pAttachments[i];
      const VkFormat format = attachment_formats[i];

      uint32_t rb_mrt_control = 0;
      uint32_t rb_mrt_blend_control = 0;
      if (format != VK_FORMAT_UNDEFINED &&
          (!color_info || color_info->pColorWriteEnables[i])) {
         const bool has_alpha = vk_format_has_alpha(format);

         rb_mrt_control =
            tu6_rb_mrt_control(att, rb_mrt_control_rop, has_alpha);
         rb_mrt_blend_control = tu6_rb_mrt_blend_control(att, has_alpha);

         /* calculate bpp based on format and write mask */
         uint32_t write_bpp = 0;
         if (att->colorWriteMask == 0xf) {
            write_bpp = vk_format_get_blocksizebits(format);
         } else {
            const enum pipe_format pipe_format = vk_format_to_pipe_format(format);
            for (uint32_t i = 0; i < 4; i++) {
               if (att->colorWriteMask & (1 << i)) {
                  write_bpp += util_format_get_component_bits(pipe_format,
                        UTIL_FORMAT_COLORSPACE_RGB, i);
               }
            }
         }
         total_bpp += write_bpp;

         pipeline->color_write_enable |= BIT(i);
         if (att->blendEnable)
            pipeline->blend_enable |= BIT(i);

         if (att->blendEnable || *rop_reads_dst) {
            total_bpp += write_bpp;
         }
      }

      pipeline->rb_mrt_control[i] = rb_mrt_control & pipeline->rb_mrt_control_mask;
      pipeline->rb_mrt_blend_control[i] = rb_mrt_blend_control;
   }

   *color_bandwidth_per_sample = total_bpp / 8;
}

static void
tu6_emit_blend_control(struct tu_pipeline *pipeline,
                       uint32_t blend_enable_mask,
                       bool dual_src_blend,
                       const VkPipelineMultisampleStateCreateInfo *msaa_info)
{
   const uint32_t sample_mask =
      msaa_info->pSampleMask ? (*msaa_info->pSampleMask & 0xffff)
                             : ((1 << msaa_info->rasterizationSamples) - 1);


   pipeline->sp_blend_cntl =
       A6XX_SP_BLEND_CNTL(.enable_blend = blend_enable_mask,
                          .dual_color_in_enable = dual_src_blend,
                          .alpha_to_coverage = msaa_info->alphaToCoverageEnable,
                          .unk8 = true).value & pipeline->sp_blend_cntl_mask;

   /* set A6XX_RB_BLEND_CNTL_INDEPENDENT_BLEND only when enabled? */
   pipeline->rb_blend_cntl =
       A6XX_RB_BLEND_CNTL(.enable_blend = blend_enable_mask,
                          .independent_blend = true,
                          .sample_mask = sample_mask,
                          .dual_color_in_enable = dual_src_blend,
                          .alpha_to_coverage = msaa_info->alphaToCoverageEnable,
                          .alpha_to_one = msaa_info->alphaToOneEnable).value &
      pipeline->rb_blend_cntl_mask;
}

static void
tu6_emit_blend(struct tu_cs *cs,
               struct tu_pipeline *pipeline)
{
   tu_cs_emit_regs(cs, A6XX_SP_BLEND_CNTL(.dword = pipeline->sp_blend_cntl));
   tu_cs_emit_regs(cs, A6XX_RB_BLEND_CNTL(.dword = pipeline->rb_blend_cntl));

   for (unsigned i = 0; i < pipeline->num_rts; i++) {
      tu_cs_emit_regs(cs,
                      A6XX_RB_MRT_CONTROL(i, .dword = pipeline->rb_mrt_control[i]),
                      A6XX_RB_MRT_BLEND_CONTROL(i, .dword = pipeline->rb_mrt_blend_control[i]));
   }
}

static VkResult
tu_setup_pvtmem(struct tu_device *dev,
                struct tu_pipeline *pipeline,
                struct tu_pvtmem_config *config,
                uint32_t pvtmem_bytes,
                bool per_wave)
{
   if (!pvtmem_bytes) {
      memset(config, 0, sizeof(*config));
      return VK_SUCCESS;
   }

   /* There is a substantial memory footprint from private memory BOs being
    * allocated on a per-pipeline basis and it isn't required as the same
    * BO can be utilized by multiple pipelines as long as they have the
    * private memory layout (sizes and per-wave/per-fiber) to avoid being
    * overwritten by other active pipelines using the same BO with differing
    * private memory layouts resulting memory corruption.
    *
    * To avoid this, we create private memory BOs on a per-device level with
    * an associated private memory layout then dynamically grow them when
    * needed and reuse them across pipelines. Growth is done in terms of
    * powers of two so that we can avoid frequent reallocation of the
    * private memory BOs.
    */

   struct tu_pvtmem_bo *pvtmem_bo =
      per_wave ? &dev->wave_pvtmem_bo : &dev->fiber_pvtmem_bo;
   mtx_lock(&pvtmem_bo->mtx);

   if (pvtmem_bo->per_fiber_size < pvtmem_bytes) {
      if (pvtmem_bo->bo)
         tu_bo_finish(dev, pvtmem_bo->bo);

      pvtmem_bo->per_fiber_size =
         util_next_power_of_two(ALIGN(pvtmem_bytes, 512));
      pvtmem_bo->per_sp_size =
         ALIGN(pvtmem_bo->per_fiber_size *
                  dev->physical_device->info->a6xx.fibers_per_sp,
               1 << 12);
      uint32_t total_size =
         dev->physical_device->info->num_sp_cores * pvtmem_bo->per_sp_size;

      VkResult result = tu_bo_init_new(dev, &pvtmem_bo->bo, total_size,
                                       TU_BO_ALLOC_NO_FLAGS);
      if (result != VK_SUCCESS) {
         mtx_unlock(&pvtmem_bo->mtx);
         return result;
      }
   }

   config->per_wave = per_wave;
   config->per_fiber_size = pvtmem_bo->per_fiber_size;
   config->per_sp_size = pvtmem_bo->per_sp_size;

   pipeline->pvtmem_bo = tu_bo_get_ref(pvtmem_bo->bo);
   config->iova = pipeline->pvtmem_bo->iova;

   mtx_unlock(&pvtmem_bo->mtx);

   return VK_SUCCESS;
}

static VkResult
tu_pipeline_allocate_cs(struct tu_device *dev,
                        struct tu_pipeline *pipeline,
                        struct tu_pipeline_layout *layout,
                        struct tu_pipeline_builder *builder,
                        struct ir3_shader_variant *compute)
{
   uint32_t size = 1024 + tu6_load_state_size(pipeline, layout);

   /* graphics case: */
   if (builder) {
      size += TU6_EMIT_VERTEX_INPUT_MAX_DWORDS +
         2 * TU6_EMIT_VFD_DEST_MAX_DWORDS;

      for (uint32_t i = 0; i < ARRAY_SIZE(builder->shaders->variants); i++) {
         if (builder->shaders->variants[i]) {
            size += builder->shaders->variants[i]->info.size / 4;
         }
      }

      size += builder->binning_variant->info.size / 4;

      builder->additional_cs_reserve_size = 0;
      for (unsigned i = 0; i < ARRAY_SIZE(builder->shaders->variants); i++) {
         struct ir3_shader_variant *variant = builder->shaders->variants[i];
         if (variant) {
            builder->additional_cs_reserve_size +=
               tu_xs_get_additional_cs_size_dwords(variant);

            if (variant->binning) {
               builder->additional_cs_reserve_size +=
                  tu_xs_get_additional_cs_size_dwords(variant->binning);
            }
         }
      }

      /* The additional size is used twice, once per tu6_emit_program() call. */
      size += builder->additional_cs_reserve_size * 2;
   } else {
      size += compute->info.size / 4;

      size += tu_xs_get_additional_cs_size_dwords(compute);
   }

   /* Allocate the space for the pipeline out of the device's RO suballocator.
    *
    * Sub-allocating BOs saves memory and also kernel overhead in refcounting of
    * BOs at exec time.
    *
    * The pipeline cache would seem like a natural place to stick the
    * suballocator, except that it is not guaranteed to outlive the pipelines
    * created from it, so you can't store any long-lived state there, and you
    * can't use its EXTERNALLY_SYNCHRONIZED flag to avoid atomics because
    * pipeline destroy isn't synchronized by the cache.
    */
   pthread_mutex_lock(&dev->pipeline_mutex);
   VkResult result = tu_suballoc_bo_alloc(&pipeline->bo, &dev->pipeline_suballoc,
                                          size * 4, 128);
   pthread_mutex_unlock(&dev->pipeline_mutex);
   if (result != VK_SUCCESS)
      return result;

   tu_cs_init_suballoc(&pipeline->cs, dev, &pipeline->bo);

   return VK_SUCCESS;
}

static void
tu_pipeline_shader_key_init(struct ir3_shader_key *key,
                            const struct tu_pipeline *pipeline,
                            const VkGraphicsPipelineCreateInfo *pipeline_info)
{
   for (uint32_t i = 0; i < pipeline_info->stageCount; i++) {
      if (pipeline_info->pStages[i].stage == VK_SHADER_STAGE_GEOMETRY_BIT) {
         key->has_gs = true;
         break;
      }
   }

   if (pipeline_info->pRasterizationState->rasterizerDiscardEnable &&
       !(pipeline->dynamic_state_mask & BIT(TU_DYNAMIC_STATE_RASTERIZER_DISCARD)))
      return;

   const VkPipelineMultisampleStateCreateInfo *msaa_info = pipeline_info->pMultisampleState;
   const struct VkPipelineSampleLocationsStateCreateInfoEXT *sample_locations =
      vk_find_struct_const(msaa_info->pNext, PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT);
   if (msaa_info->rasterizationSamples > 1 ||
       /* also set msaa key when sample location is not the default
        * since this affects varying interpolation */
       (sample_locations && sample_locations->sampleLocationsEnable)) {
      key->msaa = true;
   }

   /* The 1.3.215 spec says:
    *
    *    Sample shading can be used to specify a minimum number of unique
    *    samples to process for each fragment. If sample shading is enabled,
    *    an implementation must provide a minimum of
    *
    *       max(ceil(minSampleShadingFactor * totalSamples), 1)
    *
    *    unique associated data for each fragment, where
    *    minSampleShadingFactor is the minimum fraction of sample shading.
    *
    * The definition is pretty much the same as OpenGL's GL_SAMPLE_SHADING.
    * They both require unique associated data.
    *
    * There are discussions to change the definition, such that
    * sampleShadingEnable does not imply unique associated data.  Before the
    * discussions are settled and before apps (i.e., ANGLE) are fixed to
    * follow the new and incompatible definition, we should stick to the
    * current definition.
    *
    * Note that ir3_shader_key::sample_shading is not actually used by ir3,
    * just checked in tu6_emit_fs_inputs.  We will also copy the value to
    * tu_shader_key::force_sample_interp in a bit.
    */
   if (msaa_info->sampleShadingEnable &&
       (msaa_info->minSampleShading * msaa_info->rasterizationSamples) > 1.0f)
      key->sample_shading = true;

   /* We set this after we compile to NIR because we need the prim mode */
   key->tessellation = IR3_TESS_NONE;
}

static uint32_t
tu6_get_tessmode(struct tu_shader* shader)
{
   enum tess_primitive_mode primitive_mode = shader->ir3_shader->nir->info.tess._primitive_mode;
   switch (primitive_mode) {
   case TESS_PRIMITIVE_ISOLINES:
      return IR3_TESS_ISOLINES;
   case TESS_PRIMITIVE_TRIANGLES:
      return IR3_TESS_TRIANGLES;
   case TESS_PRIMITIVE_QUADS:
      return IR3_TESS_QUADS;
   case TESS_PRIMITIVE_UNSPECIFIED:
      return IR3_TESS_NONE;
   default:
      unreachable("bad tessmode");
   }
}

static uint64_t
tu_upload_variant(struct tu_pipeline *pipeline,
                  const struct ir3_shader_variant *variant)
{
   struct tu_cs_memory memory;

   if (!variant)
      return 0;

   /* this expects to get enough alignment because shaders are allocated first
    * and total size is always aligned correctly
    * note: an assert in tu6_emit_xs_config validates the alignment
    */
   tu_cs_alloc(&pipeline->cs, variant->info.size / 4, 1, &memory);

   memcpy(memory.map, variant->bin, variant->info.size);
   return memory.iova;
}

static void
tu_append_executable(struct tu_pipeline *pipeline, struct ir3_shader_variant *variant,
                     char *nir_from_spirv)
{
   struct tu_pipeline_executable exe = {
      .stage = variant->type,
      .nir_from_spirv = nir_from_spirv,
      .nir_final = ralloc_strdup(pipeline->executables_mem_ctx, variant->disasm_info.nir),
      .disasm = ralloc_strdup(pipeline->executables_mem_ctx, variant->disasm_info.disasm),
      .stats = variant->info,
      .is_binning = variant->binning_pass,
   };

   util_dynarray_append(&pipeline->executables, struct tu_pipeline_executable, exe);
}

static void
tu_link_shaders(struct tu_pipeline_builder *builder,
                nir_shader **shaders, unsigned shaders_count)
{
   nir_shader *consumer = NULL;
   for (gl_shader_stage stage = shaders_count - 1;
        stage >= MESA_SHADER_VERTEX; stage--) {
      if (!shaders[stage])
         continue;

      nir_shader *producer = shaders[stage];
      if (!consumer) {
         consumer = producer;
         continue;
      }

      if (nir_link_opt_varyings(producer, consumer)) {
         NIR_PASS_V(consumer, nir_opt_constant_folding);
         NIR_PASS_V(consumer, nir_opt_algebraic);
         NIR_PASS_V(consumer, nir_opt_dce);
      }

      NIR_PASS_V(producer, nir_remove_dead_variables, nir_var_shader_out, NULL);
      NIR_PASS_V(consumer, nir_remove_dead_variables, nir_var_shader_in, NULL);

      bool progress = nir_remove_unused_varyings(producer, consumer);

      nir_compact_varyings(producer, consumer, true);
      if (progress) {
         if (nir_lower_global_vars_to_local(producer)) {
            /* Remove dead writes, which can remove input loads */
            NIR_PASS_V(producer, nir_remove_dead_variables, nir_var_shader_temp, NULL);
            NIR_PASS_V(producer, nir_opt_dce);
         }
         nir_lower_global_vars_to_local(consumer);
      }

      consumer = producer;
   }
}

static void
tu_shader_key_init(struct tu_shader_key *key,
                   const VkPipelineShaderStageCreateInfo *stage_info,
                   struct tu_device *dev)
{
   enum ir3_wavesize_option api_wavesize, real_wavesize;

   if (stage_info) {
      if (stage_info->flags &
          VK_PIPELINE_SHADER_STAGE_CREATE_ALLOW_VARYING_SUBGROUP_SIZE_BIT) {
         api_wavesize = real_wavesize = IR3_SINGLE_OR_DOUBLE;
      } else {
         const VkPipelineShaderStageRequiredSubgroupSizeCreateInfo *size_info =
            vk_find_struct_const(stage_info->pNext,
                                 PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO);

         if (size_info) {
            if (size_info->requiredSubgroupSize == dev->compiler->threadsize_base) {
               api_wavesize = IR3_SINGLE_ONLY;
            } else {
               assert(size_info->requiredSubgroupSize == dev->compiler->threadsize_base * 2);
               api_wavesize = IR3_DOUBLE_ONLY;
            }
         } else {
            /* Match the exposed subgroupSize. */
            api_wavesize = IR3_DOUBLE_ONLY;
         }

         if (stage_info->flags &
             VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT)
            real_wavesize = api_wavesize;
         else if (api_wavesize == IR3_SINGLE_ONLY)
            real_wavesize = IR3_SINGLE_ONLY;
         else
            real_wavesize = IR3_SINGLE_OR_DOUBLE;
      }
   } else {
      api_wavesize = real_wavesize = IR3_SINGLE_OR_DOUBLE;
   }

   key->api_wavesize = api_wavesize;
   key->real_wavesize = real_wavesize;
}

static void
tu_hash_stage(struct mesa_sha1 *ctx,
              const VkPipelineShaderStageCreateInfo *stage,
              const struct tu_shader_key *key)
{
   unsigned char stage_hash[SHA1_DIGEST_LENGTH];

   vk_pipeline_hash_shader_stage(stage, stage_hash);
   _mesa_sha1_update(ctx, stage_hash, sizeof(stage_hash));
   _mesa_sha1_update(ctx, key, sizeof(*key));
}

/* Hash flags which can affect ir3 shader compilation which aren't known until
 * logical device creation.
 */
static void
tu_hash_compiler(struct mesa_sha1 *ctx, const struct ir3_compiler *compiler)
{
   _mesa_sha1_update(ctx, &compiler->robust_buffer_access2,
                     sizeof(compiler->robust_buffer_access2));
   _mesa_sha1_update(ctx, &ir3_shader_debug, sizeof(ir3_shader_debug));
}

static void
tu_hash_shaders(unsigned char *hash,
                const VkPipelineShaderStageCreateInfo **stages,
                const struct tu_pipeline_layout *layout,
                const struct tu_shader_key *keys,
                const struct ir3_shader_key *ir3_key,
                const struct ir3_compiler *compiler)
{
   struct mesa_sha1 ctx;

   _mesa_sha1_init(&ctx);

   if (layout)
      _mesa_sha1_update(&ctx, layout->sha1, sizeof(layout->sha1));

   _mesa_sha1_update(&ctx, ir3_key, sizeof(ir3_key));

   for (int i = 0; i < MESA_SHADER_STAGES; ++i) {
      if (stages[i]) {
         tu_hash_stage(&ctx, stages[i], &keys[i]);
      }
   }
   tu_hash_compiler(&ctx, compiler);
   _mesa_sha1_final(&ctx, hash);
}

static void
tu_hash_compute(unsigned char *hash,
                const VkPipelineShaderStageCreateInfo *stage,
                const struct tu_pipeline_layout *layout,
                const struct tu_shader_key *key,
                const struct ir3_compiler *compiler)
{
   struct mesa_sha1 ctx;

   _mesa_sha1_init(&ctx);

   if (layout)
      _mesa_sha1_update(&ctx, layout->sha1, sizeof(layout->sha1));

   tu_hash_stage(&ctx, stage, key);

   tu_hash_compiler(&ctx, compiler);
   _mesa_sha1_final(&ctx, hash);
}

static bool
tu_shaders_serialize(struct vk_pipeline_cache_object *object,
                     struct blob *blob);

static struct vk_pipeline_cache_object *
tu_shaders_deserialize(struct vk_device *device,
                       const void *key_data, size_t key_size,
                       struct blob_reader *blob);

static void
tu_shaders_destroy(struct vk_pipeline_cache_object *object)
{
   struct tu_compiled_shaders *shaders =
      container_of(object, struct tu_compiled_shaders, base);

   for (unsigned i = 0; i < ARRAY_SIZE(shaders->variants); i++)
      ralloc_free(shaders->variants[i]);

   vk_pipeline_cache_object_finish(&shaders->base);
   vk_free(&object->device->alloc, shaders);
}

const struct vk_pipeline_cache_object_ops tu_shaders_ops = {
   .serialize = tu_shaders_serialize,
   .deserialize = tu_shaders_deserialize,
   .destroy = tu_shaders_destroy,
};

static struct tu_compiled_shaders *
tu_shaders_init(struct tu_device *dev, const void *key_data, size_t key_size)
{
   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct tu_compiled_shaders, shaders, 1);
   VK_MULTIALLOC_DECL_SIZE(&ma, void, obj_key_data, key_size);

   if (!vk_multialloc_zalloc(&ma, &dev->vk.alloc,
                             VK_SYSTEM_ALLOCATION_SCOPE_DEVICE))
      return NULL;

   memcpy(obj_key_data, key_data, key_size);
   vk_pipeline_cache_object_init(&dev->vk, &shaders->base,
                                 &tu_shaders_ops, obj_key_data, key_size);

   return shaders;
}

static bool
tu_shaders_serialize(struct vk_pipeline_cache_object *object,
                     struct blob *blob)
{
   struct tu_compiled_shaders *shaders =
      container_of(object, struct tu_compiled_shaders, base);

   blob_write_bytes(blob, shaders->push_consts, sizeof(shaders->push_consts));
   blob_write_uint8(blob, shaders->active_desc_sets);
   blob_write_uint8(blob, shaders->multi_pos_output);

   for (unsigned i = 0; i < ARRAY_SIZE(shaders->variants); i++) {
      if (shaders->variants[i]) {
         blob_write_uint8(blob, 1);
         ir3_store_variant(blob, shaders->variants[i]);
      } else {
         blob_write_uint8(blob, 0);
      }
   }

   return true;
}

static struct vk_pipeline_cache_object *
tu_shaders_deserialize(struct vk_device *_device,
                       const void *key_data, size_t key_size,
                       struct blob_reader *blob)
{
   struct tu_device *dev = container_of(_device, struct tu_device, vk);
   struct tu_compiled_shaders *shaders =
      tu_shaders_init(dev, key_data, key_size);

   if (!shaders)
      return NULL;

   blob_copy_bytes(blob, shaders->push_consts, sizeof(shaders->push_consts));
   shaders->active_desc_sets = blob_read_uint8(blob);
   shaders->multi_pos_output = blob_read_uint8(blob);

   for (unsigned i = 0; i < ARRAY_SIZE(shaders->variants); i++) {
      bool has_shader = blob_read_uint8(blob);
      if (has_shader) {
         shaders->variants[i] = ir3_retrieve_variant(blob, dev->compiler, NULL);
      }
   }

   return &shaders->base;
}

static struct tu_compiled_shaders *
tu_pipeline_cache_lookup(struct vk_pipeline_cache *cache,
                         const void *key_data, size_t key_size,
                         bool *application_cache_hit)
{
   struct vk_pipeline_cache_object *object =
      vk_pipeline_cache_lookup_object(cache, key_data, key_size,
                                      &tu_shaders_ops, application_cache_hit);
   if (object)
      return container_of(object, struct tu_compiled_shaders, base);
   else
      return NULL;
}

static struct tu_compiled_shaders *
tu_pipeline_cache_insert(struct vk_pipeline_cache *cache,
                         struct tu_compiled_shaders *shaders)
{
   struct vk_pipeline_cache_object *object =
      vk_pipeline_cache_add_object(cache, &shaders->base);
   return container_of(object, struct tu_compiled_shaders, base);
}

static VkResult
tu_pipeline_builder_compile_shaders(struct tu_pipeline_builder *builder,
                                    struct tu_pipeline *pipeline)
{
   VkResult result = VK_SUCCESS;
   const struct ir3_compiler *compiler = builder->device->compiler;
   const VkPipelineShaderStageCreateInfo *stage_infos[MESA_SHADER_STAGES] = {
      NULL
   };
   VkPipelineCreationFeedback pipeline_feedback = {
      .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
   };
   VkPipelineCreationFeedback stage_feedbacks[MESA_SHADER_STAGES] = { 0 };

   int64_t pipeline_start = os_time_get_nano();

   const VkPipelineCreationFeedbackCreateInfo *creation_feedback =
      vk_find_struct_const(builder->create_info->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);

   for (uint32_t i = 0; i < builder->create_info->stageCount; i++) {
      gl_shader_stage stage =
         vk_to_mesa_shader_stage(builder->create_info->pStages[i].stage);
      stage_infos[stage] = &builder->create_info->pStages[i];

      pipeline->active_stages |= builder->create_info->pStages[i].stage;
   }

   if (tu6_shared_constants_enable(builder->layout, builder->device->compiler)) {
      pipeline->shared_consts = (struct tu_push_constant_range) {
         .lo = 0,
         .dwords = builder->layout->push_constant_size / 4,
      };
   }

   struct tu_shader_key keys[ARRAY_SIZE(stage_infos)] = { };
   for (gl_shader_stage stage = MESA_SHADER_VERTEX;
        stage < ARRAY_SIZE(keys); stage++) {
      tu_shader_key_init(&keys[stage], stage_infos[stage], builder->device);
   }

   struct ir3_shader_key ir3_key = {};
   tu_pipeline_shader_key_init(&ir3_key, pipeline, builder->create_info);

   keys[MESA_SHADER_VERTEX].multiview_mask = builder->multiview_mask;
   keys[MESA_SHADER_FRAGMENT].multiview_mask = builder->multiview_mask;
   keys[MESA_SHADER_FRAGMENT].force_sample_interp = ir3_key.sample_shading;

   unsigned char pipeline_sha1[20];
   tu_hash_shaders(pipeline_sha1, stage_infos, builder->layout, keys, &ir3_key, compiler);

   const bool executable_info = builder->create_info->flags &
      VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR;

   char *nir_initial_disasm[ARRAY_SIZE(stage_infos)] = { NULL };

   struct tu_compiled_shaders *compiled_shaders;

   if (!executable_info) {
      bool application_cache_hit = false;

      compiled_shaders =
         tu_pipeline_cache_lookup(builder->cache, &pipeline_sha1,
                                  sizeof(pipeline_sha1),
                                  &application_cache_hit);

      if (application_cache_hit && builder->cache != builder->device->mem_cache) {
         pipeline_feedback.flags |=
            VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
      }

      if (compiled_shaders)
         goto done;
   }

   if (builder->create_info->flags &
       VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT) {
      return VK_PIPELINE_COMPILE_REQUIRED;
   }

   nir_shader *nir[ARRAY_SIZE(stage_infos)] = { NULL };

   struct tu_shader *shaders[ARRAY_SIZE(nir)] = { NULL };

   for (gl_shader_stage stage = MESA_SHADER_VERTEX;
        stage < ARRAY_SIZE(nir); stage++) {
      const VkPipelineShaderStageCreateInfo *stage_info = stage_infos[stage];
      if (!stage_info)
         continue;

      int64_t stage_start = os_time_get_nano();

      nir[stage] = tu_spirv_to_nir(builder->device, builder->mem_ctx, stage_info, stage);
      if (!nir[stage]) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      stage_feedbacks[stage].flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;
      stage_feedbacks[stage].duration += os_time_get_nano() - stage_start;
   }

   if (!nir[MESA_SHADER_FRAGMENT]) {
         const nir_shader_compiler_options *nir_options =
            ir3_get_compiler_options(builder->device->compiler);
         nir_builder fs_b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
                                                           nir_options,
                                                           "noop_fs");
         nir[MESA_SHADER_FRAGMENT] = fs_b.shader;
   }

   if (executable_info) {
      for (gl_shader_stage stage = MESA_SHADER_VERTEX;
            stage < ARRAY_SIZE(nir); stage++) {
         if (!nir[stage])
            continue;

         nir_initial_disasm[stage] =
            nir_shader_as_str(nir[stage], pipeline->executables_mem_ctx);
      }
   }

   tu_link_shaders(builder, nir, ARRAY_SIZE(nir));

   uint32_t desc_sets = 0;
   for (gl_shader_stage stage = MESA_SHADER_VERTEX;
        stage < ARRAY_SIZE(nir); stage++) {
      if (!nir[stage])
         continue;

      int64_t stage_start = os_time_get_nano();

      struct tu_shader *shader =
         tu_shader_create(builder->device, nir[stage], &keys[stage],
                          builder->layout, builder->alloc);
      if (!shader) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      /* In SPIR-V generated from GLSL, the primitive mode is specified in the
       * tessellation evaluation shader, but in SPIR-V generated from HLSL,
       * the mode is specified in the tessellation control shader. */
      if ((stage == MESA_SHADER_TESS_EVAL || stage == MESA_SHADER_TESS_CTRL) &&
          ir3_key.tessellation == IR3_TESS_NONE) {
         ir3_key.tessellation = tu6_get_tessmode(shader);
      }

      if (stage > MESA_SHADER_TESS_CTRL) {
         if (stage == MESA_SHADER_FRAGMENT) {
            ir3_key.tcs_store_primid = ir3_key.tcs_store_primid ||
               (nir[stage]->info.inputs_read & (1ull << VARYING_SLOT_PRIMITIVE_ID));
         } else {
            ir3_key.tcs_store_primid = ir3_key.tcs_store_primid ||
               BITSET_TEST(nir[stage]->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID);
         }
      }

      /* Keep track of the status of each shader's active descriptor sets,
       * which is set in tu_lower_io. */
      desc_sets |= shader->active_desc_sets;

      shaders[stage] = shader;

      stage_feedbacks[stage].duration += os_time_get_nano() - stage_start;
   }

   struct tu_shader *last_shader = shaders[MESA_SHADER_GEOMETRY];
   if (!last_shader)
      last_shader = shaders[MESA_SHADER_TESS_EVAL];
   if (!last_shader)
      last_shader = shaders[MESA_SHADER_VERTEX];

   uint64_t outputs_written = last_shader->ir3_shader->nir->info.outputs_written;

   ir3_key.layer_zero = !(outputs_written & VARYING_BIT_LAYER);
   ir3_key.view_zero = !(outputs_written & VARYING_BIT_VIEWPORT);

   compiled_shaders =
      tu_shaders_init(builder->device, &pipeline_sha1, sizeof(pipeline_sha1));

   if (!compiled_shaders) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   compiled_shaders->active_desc_sets = desc_sets;
   compiled_shaders->multi_pos_output =
      shaders[MESA_SHADER_VERTEX]->multi_pos_output;

   for (gl_shader_stage stage = MESA_SHADER_VERTEX;
        stage < ARRAY_SIZE(shaders); stage++) {
      if (!shaders[stage])
         continue;

      int64_t stage_start = os_time_get_nano();

      compiled_shaders->variants[stage] =
         ir3_shader_create_variant(shaders[stage]->ir3_shader, &ir3_key,
                                   executable_info);
      if (!compiled_shaders->variants[stage])
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      compiled_shaders->push_consts[stage] = shaders[stage]->push_consts;

      stage_feedbacks[stage].duration += os_time_get_nano() - stage_start;
   }

   uint32_t safe_constlens = ir3_trim_constlen(compiled_shaders->variants, compiler);

   ir3_key.safe_constlen = true;

   for (gl_shader_stage stage = MESA_SHADER_VERTEX;
        stage < ARRAY_SIZE(shaders); stage++) {
      if (!shaders[stage])
         continue;

      if (safe_constlens & (1 << stage)) {
         int64_t stage_start = os_time_get_nano();

         ralloc_free(compiled_shaders->variants[stage]);
         compiled_shaders->variants[stage] =
            ir3_shader_create_variant(shaders[stage]->ir3_shader, &ir3_key,
                                      executable_info);
         if (!compiled_shaders->variants[stage]) {
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            goto fail;
         }

         stage_feedbacks[stage].duration += os_time_get_nano() - stage_start;
      }
   }

   for (gl_shader_stage stage = MESA_SHADER_VERTEX;
         stage < ARRAY_SIZE(nir); stage++) {
      if (shaders[stage]) {
         tu_shader_destroy(builder->device, shaders[stage], builder->alloc);
      }
   }

   compiled_shaders =
      tu_pipeline_cache_insert(builder->cache, compiled_shaders);

done:
   for (gl_shader_stage stage = MESA_SHADER_VERTEX;
         stage < ARRAY_SIZE(nir); stage++) {
      if (compiled_shaders->variants[stage]) {
         tu_append_executable(pipeline, compiled_shaders->variants[stage],
            nir_initial_disasm[stage]);
      }
   }

   struct ir3_shader_variant *vs =
      compiled_shaders->variants[MESA_SHADER_VERTEX];

   struct ir3_shader_variant *variant;
   if (!vs->stream_output.num_outputs && ir3_has_binning_vs(&vs->key)) {
      tu_append_executable(pipeline, vs->binning, NULL);
      variant = vs->binning;
   } else {
      variant = vs;
   }

   builder->binning_variant = variant;

   builder->shaders = compiled_shaders;

   pipeline->active_desc_sets = compiled_shaders->active_desc_sets;
   if (compiled_shaders->variants[MESA_SHADER_TESS_CTRL]) {
      pipeline->tess.patch_type =
         compiled_shaders->variants[MESA_SHADER_TESS_CTRL]->key.tessellation;
   }

   pipeline_feedback.duration = os_time_get_nano() - pipeline_start;
   if (creation_feedback) {
      *creation_feedback->pPipelineCreationFeedback = pipeline_feedback;

      assert(builder->create_info->stageCount == 
             creation_feedback->pipelineStageCreationFeedbackCount);
      for (uint32_t i = 0; i < builder->create_info->stageCount; i++) {
         gl_shader_stage s =
            vk_to_mesa_shader_stage(builder->create_info->pStages[i].stage);
         creation_feedback->pPipelineStageCreationFeedbacks[i] = stage_feedbacks[s];
      }
   }

   return VK_SUCCESS;

fail:
   for (gl_shader_stage stage = MESA_SHADER_VERTEX;
         stage < ARRAY_SIZE(nir); stage++) {
      if (shaders[stage]) {
         tu_shader_destroy(builder->device, shaders[stage], builder->alloc);
      }
   }

   if (compiled_shaders)
      vk_pipeline_cache_object_unref(&compiled_shaders->base);

   return result;
}

static void
tu_pipeline_builder_parse_dynamic(struct tu_pipeline_builder *builder,
                                  struct tu_pipeline *pipeline)
{
   const VkPipelineDynamicStateCreateInfo *dynamic_info =
      builder->create_info->pDynamicState;

   pipeline->gras_su_cntl_mask = ~0u;
   pipeline->rb_depth_cntl_mask = ~0u;
   pipeline->rb_stencil_cntl_mask = ~0u;
   pipeline->pc_raster_cntl_mask = ~0u;
   pipeline->vpc_unknown_9107_mask = ~0u;
   pipeline->sp_blend_cntl_mask = ~0u;
   pipeline->rb_blend_cntl_mask = ~0u;
   pipeline->rb_mrt_control_mask = ~0u;

   if (!dynamic_info)
      return;

   for (uint32_t i = 0; i < dynamic_info->dynamicStateCount; i++) {
      VkDynamicState state = dynamic_info->pDynamicStates[i];
      switch (state) {
      case VK_DYNAMIC_STATE_VIEWPORT ... VK_DYNAMIC_STATE_STENCIL_REFERENCE:
         if (state == VK_DYNAMIC_STATE_LINE_WIDTH)
            pipeline->gras_su_cntl_mask &= ~A6XX_GRAS_SU_CNTL_LINEHALFWIDTH__MASK;
         pipeline->dynamic_state_mask |= BIT(state);
         break;
      case VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT:
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_SAMPLE_LOCATIONS);
         break;
      case VK_DYNAMIC_STATE_CULL_MODE:
         pipeline->gras_su_cntl_mask &=
            ~(A6XX_GRAS_SU_CNTL_CULL_BACK | A6XX_GRAS_SU_CNTL_CULL_FRONT);
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_GRAS_SU_CNTL);
         break;
      case VK_DYNAMIC_STATE_FRONT_FACE:
         pipeline->gras_su_cntl_mask &= ~A6XX_GRAS_SU_CNTL_FRONT_CW;
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_GRAS_SU_CNTL);
         break;
      case VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY:
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY);
         break;
      case VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE:
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_VB_STRIDE);
         break;
      case VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT:
         pipeline->dynamic_state_mask |= BIT(VK_DYNAMIC_STATE_VIEWPORT);
         break;
      case VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT:
         pipeline->dynamic_state_mask |= BIT(VK_DYNAMIC_STATE_SCISSOR);
         break;
      case VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE:
         pipeline->rb_depth_cntl_mask &=
            ~(A6XX_RB_DEPTH_CNTL_Z_TEST_ENABLE | A6XX_RB_DEPTH_CNTL_Z_READ_ENABLE);
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_RB_DEPTH_CNTL);
         break;
      case VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE:
         pipeline->rb_depth_cntl_mask &= ~A6XX_RB_DEPTH_CNTL_Z_WRITE_ENABLE;
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_RB_DEPTH_CNTL);
         break;
      case VK_DYNAMIC_STATE_DEPTH_COMPARE_OP:
         pipeline->rb_depth_cntl_mask &= ~A6XX_RB_DEPTH_CNTL_ZFUNC__MASK;
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_RB_DEPTH_CNTL);
         break;
      case VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE:
         pipeline->rb_depth_cntl_mask &=
            ~(A6XX_RB_DEPTH_CNTL_Z_BOUNDS_ENABLE | A6XX_RB_DEPTH_CNTL_Z_READ_ENABLE);
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_RB_DEPTH_CNTL);
         break;
      case VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE:
         pipeline->rb_stencil_cntl_mask &= ~(A6XX_RB_STENCIL_CONTROL_STENCIL_ENABLE |
                                             A6XX_RB_STENCIL_CONTROL_STENCIL_ENABLE_BF |
                                             A6XX_RB_STENCIL_CONTROL_STENCIL_READ);
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_RB_STENCIL_CNTL);
         break;
      case VK_DYNAMIC_STATE_STENCIL_OP:
         pipeline->rb_stencil_cntl_mask &= ~(A6XX_RB_STENCIL_CONTROL_FUNC__MASK |
                                             A6XX_RB_STENCIL_CONTROL_FAIL__MASK |
                                             A6XX_RB_STENCIL_CONTROL_ZPASS__MASK |
                                             A6XX_RB_STENCIL_CONTROL_ZFAIL__MASK |
                                             A6XX_RB_STENCIL_CONTROL_FUNC_BF__MASK |
                                             A6XX_RB_STENCIL_CONTROL_FAIL_BF__MASK |
                                             A6XX_RB_STENCIL_CONTROL_ZPASS_BF__MASK |
                                             A6XX_RB_STENCIL_CONTROL_ZFAIL_BF__MASK);
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_RB_STENCIL_CNTL);
         break;
      case VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE:
         pipeline->gras_su_cntl_mask &= ~A6XX_GRAS_SU_CNTL_POLY_OFFSET;
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_GRAS_SU_CNTL);
         break;
      case VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE:
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE);
         break;
      case VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE:
         pipeline->pc_raster_cntl_mask &= ~A6XX_PC_RASTER_CNTL_DISCARD;
         pipeline->vpc_unknown_9107_mask &= ~A6XX_VPC_UNKNOWN_9107_RASTER_DISCARD;
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_RASTERIZER_DISCARD);
         break;
      case VK_DYNAMIC_STATE_LOGIC_OP_EXT:
         pipeline->sp_blend_cntl_mask &= ~A6XX_SP_BLEND_CNTL_ENABLE_BLEND__MASK;
         pipeline->rb_blend_cntl_mask &= ~A6XX_RB_BLEND_CNTL_ENABLE_BLEND__MASK;
         pipeline->rb_mrt_control_mask &= ~A6XX_RB_MRT_CONTROL_ROP_CODE__MASK;
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_BLEND);
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_LOGIC_OP);
         break;
      case VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT:
         pipeline->sp_blend_cntl_mask &= ~A6XX_SP_BLEND_CNTL_ENABLE_BLEND__MASK;
         pipeline->rb_blend_cntl_mask &= ~A6XX_RB_BLEND_CNTL_ENABLE_BLEND__MASK;
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_BLEND);

         /* Dynamic color write enable doesn't directly change any of the
          * registers, but it causes us to make some of the registers 0, so we
          * set this dynamic state instead of making the register dynamic.
          */
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_COLOR_WRITE_ENABLE);
         break;
      case VK_DYNAMIC_STATE_VERTEX_INPUT_EXT:
         pipeline->dynamic_state_mask |= BIT(TU_DYNAMIC_STATE_VERTEX_INPUT) |
            BIT(TU_DYNAMIC_STATE_VB_STRIDE);
         break;
      default:
         assert(!"unsupported dynamic state");
         break;
      }
   }
}

static void
tu_pipeline_set_linkage(struct tu_program_descriptor_linkage *link,
                        struct tu_push_constant_range *push_consts,
                        struct ir3_shader_variant *v)
{
   link->const_state = *ir3_const_state(v);
   link->constlen = v->constlen;
   link->push_consts = *push_consts;
}

static void
tu_pipeline_builder_parse_shader_stages(struct tu_pipeline_builder *builder,
                                        struct tu_pipeline *pipeline)
{
   struct tu_cs prog_cs;

   /* Emit HLSQ_xS_CNTL/HLSQ_SP_xS_CONFIG *first*, before emitting anything
    * else that could depend on that state (like push constants)
    *
    * Note also that this always uses the full VS even in binning pass.  The
    * binning pass variant has the same const layout as the full VS, and
    * the constlen for the VS will be the same or greater than the constlen
    * for the binning pass variant.  It is required that the constlen state
    * matches between binning and draw passes, as some parts of the push
    * consts are emitted in state groups that are shared between the binning
    * and draw passes.
    */
   tu_cs_begin_sub_stream(&pipeline->cs, 512, &prog_cs);
   tu6_emit_program_config(&prog_cs, builder);
   pipeline->program.config_state = tu_cs_end_draw_state(&pipeline->cs, &prog_cs);

   tu_cs_begin_sub_stream(&pipeline->cs, 512 + builder->additional_cs_reserve_size, &prog_cs);
   tu6_emit_program(&prog_cs, builder, false, pipeline);
   pipeline->program.state = tu_cs_end_draw_state(&pipeline->cs, &prog_cs);

   tu_cs_begin_sub_stream(&pipeline->cs, 512 + builder->additional_cs_reserve_size, &prog_cs);
   tu6_emit_program(&prog_cs, builder, true, pipeline);
   pipeline->program.binning_state = tu_cs_end_draw_state(&pipeline->cs, &prog_cs);

   for (unsigned i = 0; i < ARRAY_SIZE(builder->shaders->variants); i++) {
      if (!builder->shaders->variants[i])
         continue;

      tu_pipeline_set_linkage(&pipeline->program.link[i],
                              &builder->shaders->push_consts[i],
                              builder->shaders->variants[i]);
   }
}

static bool
tu_pipeline_static_state(struct tu_pipeline *pipeline, struct tu_cs *cs,
                         uint32_t id, uint32_t size)
{
   assert(id < ARRAY_SIZE(pipeline->dynamic_state));

   if (pipeline->dynamic_state_mask & BIT(id))
      return false;

   pipeline->dynamic_state[id] = tu_cs_draw_state(&pipeline->cs, cs, size);
   return true;
}

static void
tu_pipeline_builder_parse_vertex_input(struct tu_pipeline_builder *builder,
                                       struct tu_pipeline *pipeline)
{
   if (pipeline->dynamic_state_mask & BIT(TU_DYNAMIC_STATE_VERTEX_INPUT))
      return;

   const VkPipelineVertexInputStateCreateInfo *vi_info =
      builder->create_info->pVertexInputState;

   struct tu_cs cs;
   if (tu_pipeline_static_state(pipeline, &cs, TU_DYNAMIC_STATE_VB_STRIDE,
                                2 * vi_info->vertexBindingDescriptionCount)) {
      for (uint32_t i = 0; i < vi_info->vertexBindingDescriptionCount; i++) {
         const VkVertexInputBindingDescription *binding =
            &vi_info->pVertexBindingDescriptions[i];

         tu_cs_emit_regs(&cs,
                         A6XX_VFD_FETCH_STRIDE(binding->binding, binding->stride));
      }
   }

   VkVertexInputBindingDescription2EXT bindings[MAX_VBS];
   VkVertexInputAttributeDescription2EXT attrs[MAX_VERTEX_ATTRIBS];

   for (unsigned i = 0; i < vi_info->vertexBindingDescriptionCount; i++) {
      const VkVertexInputBindingDescription *binding =
         &vi_info->pVertexBindingDescriptions[i];
      bindings[i] = (VkVertexInputBindingDescription2EXT) {
         .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT,
         .pNext = NULL,
         .binding = binding->binding,
         .inputRate = binding->inputRate,
         .stride = binding->stride,
         .divisor = 1,
      };

      /* Bindings may contain holes */
      pipeline->num_vbs = MAX2(pipeline->num_vbs, binding->binding + 1);
   }

   const VkPipelineVertexInputDivisorStateCreateInfoEXT *div_state =
      vk_find_struct_const(vi_info->pNext, PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT);
   if (div_state) {
      for (uint32_t i = 0; i < div_state->vertexBindingDivisorCount; i++) {
         const VkVertexInputBindingDivisorDescriptionEXT *desc =
            &div_state->pVertexBindingDivisors[i];
         bindings[desc->binding].divisor = desc->divisor;
      }
   }

   for (unsigned i = 0; i < vi_info->vertexAttributeDescriptionCount; i++) {
      const VkVertexInputAttributeDescription *attr =
         &vi_info->pVertexAttributeDescriptions[i];
      attrs[i] = (VkVertexInputAttributeDescription2EXT) {
         .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT,
         .pNext = NULL,
         .binding = attr->binding,
         .location = attr->location,
         .offset = attr->offset,
         .format = attr->format,
      };
   }

   tu_cs_begin_sub_stream(&pipeline->cs,
                          TU6_EMIT_VERTEX_INPUT_MAX_DWORDS, &cs);
   tu6_emit_vertex_input(&cs,
                         vi_info->vertexBindingDescriptionCount, bindings,
                         vi_info->vertexAttributeDescriptionCount, attrs);
   pipeline->dynamic_state[TU_DYNAMIC_STATE_VERTEX_INPUT] =
      tu_cs_end_draw_state(&pipeline->cs, &cs);
}

static void
tu_pipeline_builder_parse_input_assembly(struct tu_pipeline_builder *builder,
                                         struct tu_pipeline *pipeline)
{
   const VkPipelineInputAssemblyStateCreateInfo *ia_info =
      builder->create_info->pInputAssemblyState;

   pipeline->ia.primtype = tu6_primtype(ia_info->topology);
   pipeline->ia.primitive_restart = ia_info->primitiveRestartEnable;
}

static void
tu_pipeline_builder_parse_tessellation(struct tu_pipeline_builder *builder,
                                       struct tu_pipeline *pipeline)
{
   if (!(pipeline->active_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) ||
       !(pipeline->active_stages & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT))
      return;

   const VkPipelineTessellationStateCreateInfo *tess_info =
      builder->create_info->pTessellationState;

   assert(pipeline->ia.primtype == DI_PT_PATCHES0);
   assert(tess_info->patchControlPoints <= 32);
   pipeline->ia.primtype += tess_info->patchControlPoints;
   const VkPipelineTessellationDomainOriginStateCreateInfo *domain_info =
         vk_find_struct_const(tess_info->pNext, PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO);
   pipeline->tess.upper_left_domain_origin = !domain_info ||
         domain_info->domainOrigin == VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT;
   const struct ir3_shader_variant *hs = builder->shaders->variants[MESA_SHADER_TESS_CTRL];
   pipeline->tess.param_stride = hs->output_size * 4;
}

static void
tu_pipeline_builder_parse_viewport(struct tu_pipeline_builder *builder,
                                   struct tu_pipeline *pipeline)
{
   /* The spec says:
    *
    *    pViewportState is a pointer to an instance of the
    *    VkPipelineViewportStateCreateInfo structure, and is ignored if the
    *    pipeline has rasterization disabled."
    *
    * We leave the relevant registers stale in that case.
    */
   if (builder->rasterizer_discard)
      return;

   const VkPipelineViewportStateCreateInfo *vp_info =
      builder->create_info->pViewportState;
   const VkPipelineViewportDepthClipControlCreateInfoEXT *depth_clip_info =
         vk_find_struct_const(vp_info->pNext, PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT);
   pipeline->z_negative_one_to_one = depth_clip_info ? depth_clip_info->negativeOneToOne : false;

   struct tu_cs cs;

   if (tu_pipeline_static_state(pipeline, &cs, VK_DYNAMIC_STATE_VIEWPORT, 8 + 10 * vp_info->viewportCount))
      tu6_emit_viewport(&cs, vp_info->pViewports, vp_info->viewportCount, pipeline->z_negative_one_to_one);

   if (tu_pipeline_static_state(pipeline, &cs, VK_DYNAMIC_STATE_SCISSOR, 1 + 2 * vp_info->scissorCount))
      tu6_emit_scissor(&cs, vp_info->pScissors, vp_info->scissorCount);
}

static void
tu_pipeline_builder_parse_rasterization(struct tu_pipeline_builder *builder,
                                        struct tu_pipeline *pipeline)
{
   const VkPipelineRasterizationStateCreateInfo *rast_info =
      builder->create_info->pRasterizationState;

   pipeline->feedback_loop_may_involve_textures =
      builder->feedback_loop_may_involve_textures;

   enum a6xx_polygon_mode mode = tu6_polygon_mode(rast_info->polygonMode);

   builder->depth_clip_disable = rast_info->depthClampEnable;

   const VkPipelineRasterizationDepthClipStateCreateInfoEXT *depth_clip_state =
      vk_find_struct_const(rast_info, PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT);
   if (depth_clip_state)
      builder->depth_clip_disable = !depth_clip_state->depthClipEnable;

   pipeline->line_mode = RECTANGULAR;

   if (tu6_primtype_line(pipeline->ia.primtype) ||
       (tu6_primtype_patches(pipeline->ia.primtype) &&
        pipeline->tess.patch_type == IR3_TESS_ISOLINES)) {
      const VkPipelineRasterizationLineStateCreateInfoEXT *rast_line_state =
         vk_find_struct_const(rast_info->pNext,
                              PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT);

      if (rast_line_state && rast_line_state->lineRasterizationMode ==
               VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT) {
         pipeline->line_mode = BRESENHAM;
      }
   }

   struct tu_cs cs;
   uint32_t cs_size = 9 +
      (builder->device->physical_device->info->a6xx.has_shading_rate ? 8 : 0) +
      (builder->emit_msaa_state ? 11 : 0);
   pipeline->rast_state = tu_cs_draw_state(&pipeline->cs, &cs, cs_size);

   tu_cs_emit_regs(&cs,
                   A6XX_GRAS_CL_CNTL(
                     .znear_clip_disable = builder->depth_clip_disable,
                     .zfar_clip_disable = builder->depth_clip_disable,
                     /* TODO should this be depth_clip_disable instead? */
                     .unk5 = rast_info->depthClampEnable,
                     .zero_gb_scale_z = pipeline->z_negative_one_to_one ? 0 : 1,
                     .vp_clip_code_ignore = 1));

   tu_cs_emit_regs(&cs,
                   A6XX_VPC_POLYGON_MODE(mode));

   tu_cs_emit_regs(&cs,
                   A6XX_PC_POLYGON_MODE(mode));

   /* move to hw ctx init? */
   tu_cs_emit_regs(&cs,
                   A6XX_GRAS_SU_POINT_MINMAX(.min = 1.0f / 16.0f, .max = 4092.0f),
                   A6XX_GRAS_SU_POINT_SIZE(1.0f));

   if (builder->device->physical_device->info->a6xx.has_shading_rate) {
      tu_cs_emit_regs(&cs, A6XX_RB_UNKNOWN_8A00());
      tu_cs_emit_regs(&cs, A6XX_RB_UNKNOWN_8A10());
      tu_cs_emit_regs(&cs, A6XX_RB_UNKNOWN_8A20());
      tu_cs_emit_regs(&cs, A6XX_RB_UNKNOWN_8A30());
   }

   /* If samples count couldn't be devised from the subpass, we should emit it here.
    * It happens when subpass doesn't use any color/depth attachment.
    */
   if (builder->emit_msaa_state)
      tu6_emit_msaa(&cs, builder->samples, pipeline->line_mode);

   const VkPipelineRasterizationStateStreamCreateInfoEXT *stream_info =
      vk_find_struct_const(rast_info->pNext,
                           PIPELINE_RASTERIZATION_STATE_STREAM_CREATE_INFO_EXT);
   unsigned stream = stream_info ? stream_info->rasterizationStream : 0;

   pipeline->pc_raster_cntl = A6XX_PC_RASTER_CNTL_STREAM(stream);
   pipeline->vpc_unknown_9107 = 0;
   if (rast_info->rasterizerDiscardEnable) {
      pipeline->pc_raster_cntl |= A6XX_PC_RASTER_CNTL_DISCARD;
      pipeline->vpc_unknown_9107 |= A6XX_VPC_UNKNOWN_9107_RASTER_DISCARD;
   }

   if (tu_pipeline_static_state(pipeline, &cs, TU_DYNAMIC_STATE_RASTERIZER_DISCARD, 4)) {
      tu_cs_emit_regs(&cs, A6XX_PC_RASTER_CNTL(.dword = pipeline->pc_raster_cntl));
      tu_cs_emit_regs(&cs, A6XX_VPC_UNKNOWN_9107(.dword = pipeline->vpc_unknown_9107));
   }

   pipeline->gras_su_cntl =
      tu6_gras_su_cntl(rast_info, pipeline->line_mode, builder->multiview_mask != 0);

   if (tu_pipeline_static_state(pipeline, &cs, TU_DYNAMIC_STATE_GRAS_SU_CNTL, 2))
      tu_cs_emit_regs(&cs, A6XX_GRAS_SU_CNTL(.dword = pipeline->gras_su_cntl));

   if (tu_pipeline_static_state(pipeline, &cs, VK_DYNAMIC_STATE_DEPTH_BIAS, 4)) {
      tu6_emit_depth_bias(&cs, rast_info->depthBiasConstantFactor,
                          rast_info->depthBiasClamp,
                          rast_info->depthBiasSlopeFactor);
   }

   const struct VkPipelineRasterizationProvokingVertexStateCreateInfoEXT *provoking_vtx_state =
      vk_find_struct_const(rast_info->pNext, PIPELINE_RASTERIZATION_PROVOKING_VERTEX_STATE_CREATE_INFO_EXT);
   pipeline->provoking_vertex_last = provoking_vtx_state &&
      provoking_vtx_state->provokingVertexMode == VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT;
}

static void
tu_pipeline_builder_parse_depth_stencil(struct tu_pipeline_builder *builder,
                                        struct tu_pipeline *pipeline)
{
   /* The spec says:
    *
    *    pDepthStencilState is a pointer to an instance of the
    *    VkPipelineDepthStencilStateCreateInfo structure, and is ignored if
    *    the pipeline has rasterization disabled or if the subpass of the
    *    render pass the pipeline is created against does not use a
    *    depth/stencil attachment.
    */
   const VkPipelineDepthStencilStateCreateInfo *ds_info =
      builder->create_info->pDepthStencilState;
   const enum pipe_format pipe_format =
      vk_format_to_pipe_format(builder->depth_attachment_format);
   uint32_t rb_depth_cntl = 0, rb_stencil_cntl = 0;
   struct tu_cs cs;

   if (builder->depth_attachment_format != VK_FORMAT_UNDEFINED &&
       builder->depth_attachment_format != VK_FORMAT_S8_UINT) {
      if (ds_info->depthTestEnable) {
         rb_depth_cntl |=
            A6XX_RB_DEPTH_CNTL_Z_TEST_ENABLE |
            A6XX_RB_DEPTH_CNTL_ZFUNC(tu6_compare_func(ds_info->depthCompareOp)) |
            A6XX_RB_DEPTH_CNTL_Z_READ_ENABLE; /* TODO: don't set for ALWAYS/NEVER */

         if (builder->depth_clip_disable)
            rb_depth_cntl |= A6XX_RB_DEPTH_CNTL_Z_CLIP_DISABLE;

         if (ds_info->depthWriteEnable)
            rb_depth_cntl |= A6XX_RB_DEPTH_CNTL_Z_WRITE_ENABLE;
      }

      if (ds_info->depthBoundsTestEnable)
         rb_depth_cntl |= A6XX_RB_DEPTH_CNTL_Z_BOUNDS_ENABLE | A6XX_RB_DEPTH_CNTL_Z_READ_ENABLE;

      if (ds_info->depthBoundsTestEnable && !ds_info->depthTestEnable)
         tu6_apply_depth_bounds_workaround(builder->device, &rb_depth_cntl);

      pipeline->depth_cpp_per_sample = util_format_get_component_bits(
            pipe_format, UTIL_FORMAT_COLORSPACE_ZS, 0) / 8;
   } else {
      /* if RB_DEPTH_CNTL is set dynamically, we need to make sure it is set
       * to 0 when this pipeline is used, as enabling depth test when there
       * is no depth attachment is a problem (at least for the S8_UINT case)
       */
      if (pipeline->dynamic_state_mask & BIT(TU_DYNAMIC_STATE_RB_DEPTH_CNTL))
         pipeline->rb_depth_cntl_disable = true;
   }

   if (builder->depth_attachment_format != VK_FORMAT_UNDEFINED) {
      const VkStencilOpState *front = &ds_info->front;
      const VkStencilOpState *back = &ds_info->back;

      rb_stencil_cntl |=
         A6XX_RB_STENCIL_CONTROL_FUNC(tu6_compare_func(front->compareOp)) |
         A6XX_RB_STENCIL_CONTROL_FAIL(tu6_stencil_op(front->failOp)) |
         A6XX_RB_STENCIL_CONTROL_ZPASS(tu6_stencil_op(front->passOp)) |
         A6XX_RB_STENCIL_CONTROL_ZFAIL(tu6_stencil_op(front->depthFailOp)) |
         A6XX_RB_STENCIL_CONTROL_FUNC_BF(tu6_compare_func(back->compareOp)) |
         A6XX_RB_STENCIL_CONTROL_FAIL_BF(tu6_stencil_op(back->failOp)) |
         A6XX_RB_STENCIL_CONTROL_ZPASS_BF(tu6_stencil_op(back->passOp)) |
         A6XX_RB_STENCIL_CONTROL_ZFAIL_BF(tu6_stencil_op(back->depthFailOp));

      if (ds_info->stencilTestEnable) {
         rb_stencil_cntl |=
            A6XX_RB_STENCIL_CONTROL_STENCIL_ENABLE |
            A6XX_RB_STENCIL_CONTROL_STENCIL_ENABLE_BF |
            A6XX_RB_STENCIL_CONTROL_STENCIL_READ;
      }

      pipeline->stencil_cpp_per_sample = util_format_get_component_bits(
            pipe_format, UTIL_FORMAT_COLORSPACE_ZS, 1) / 8;
   }

   if (tu_pipeline_static_state(pipeline, &cs, TU_DYNAMIC_STATE_RB_DEPTH_CNTL, 2)) {
      tu_cs_emit_pkt4(&cs, REG_A6XX_RB_DEPTH_CNTL, 1);
      tu_cs_emit(&cs, rb_depth_cntl);
   }
   pipeline->rb_depth_cntl = rb_depth_cntl;

   if (tu_pipeline_static_state(pipeline, &cs, TU_DYNAMIC_STATE_RB_STENCIL_CNTL, 2)) {
      tu_cs_emit_pkt4(&cs, REG_A6XX_RB_STENCIL_CONTROL, 1);
      tu_cs_emit(&cs, rb_stencil_cntl);
   }
   pipeline->rb_stencil_cntl = rb_stencil_cntl;

   /* the remaining draw states arent used if there is no d/s, leave them empty */
   if (builder->depth_attachment_format == VK_FORMAT_UNDEFINED)
      return;

   if (tu_pipeline_static_state(pipeline, &cs, VK_DYNAMIC_STATE_DEPTH_BOUNDS, 3)) {
      tu_cs_emit_regs(&cs,
                      A6XX_RB_Z_BOUNDS_MIN(ds_info->minDepthBounds),
                      A6XX_RB_Z_BOUNDS_MAX(ds_info->maxDepthBounds));
   }

   if (tu_pipeline_static_state(pipeline, &cs, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, 2)) {
      tu_cs_emit_regs(&cs, A6XX_RB_STENCILMASK(.mask = ds_info->front.compareMask & 0xff,
                                               .bfmask = ds_info->back.compareMask & 0xff));
   }

   if (tu_pipeline_static_state(pipeline, &cs, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, 2)) {
      update_stencil_mask(&pipeline->stencil_wrmask,  VK_STENCIL_FACE_FRONT_BIT, ds_info->front.writeMask);
      update_stencil_mask(&pipeline->stencil_wrmask,  VK_STENCIL_FACE_BACK_BIT, ds_info->back.writeMask);
      tu_cs_emit_regs(&cs, A6XX_RB_STENCILWRMASK(.dword = pipeline->stencil_wrmask));
   }

   if (tu_pipeline_static_state(pipeline, &cs, VK_DYNAMIC_STATE_STENCIL_REFERENCE, 2)) {
      tu_cs_emit_regs(&cs, A6XX_RB_STENCILREF(.ref = ds_info->front.reference & 0xff,
                                              .bfref = ds_info->back.reference & 0xff));
   }

   if (builder->shaders->variants[MESA_SHADER_FRAGMENT]) {
      const struct ir3_shader_variant *fs = builder->shaders->variants[MESA_SHADER_FRAGMENT];
      if (fs->has_kill || builder->alpha_to_coverage) {
         pipeline->lrz.force_disable_mask |= TU_LRZ_FORCE_DISABLE_WRITE;
      }
      if (fs->no_earlyz || fs->writes_pos) {
         pipeline->lrz.force_disable_mask = TU_LRZ_FORCE_DISABLE_LRZ;
      }
   }
}

static void
tu_pipeline_builder_parse_multisample_and_color_blend(
   struct tu_pipeline_builder *builder, struct tu_pipeline *pipeline)
{
   /* The spec says:
    *
    *    pMultisampleState is a pointer to an instance of the
    *    VkPipelineMultisampleStateCreateInfo, and is ignored if the pipeline
    *    has rasterization disabled.
    *
    * Also,
    *
    *    pColorBlendState is a pointer to an instance of the
    *    VkPipelineColorBlendStateCreateInfo structure, and is ignored if the
    *    pipeline has rasterization disabled or if the subpass of the render
    *    pass the pipeline is created against does not use any color
    *    attachments.
    *
    * We leave the relevant registers stale when rasterization is disabled.
    */
   if (builder->rasterizer_discard)
      return;

   static const VkPipelineColorBlendStateCreateInfo dummy_blend_info;
   const VkPipelineMultisampleStateCreateInfo *msaa_info =
      builder->create_info->pMultisampleState;
   const VkPipelineColorBlendStateCreateInfo *blend_info =
      builder->use_color_attachments ? builder->create_info->pColorBlendState
                                     : &dummy_blend_info;

   struct tu_cs cs;
   tu6_emit_rb_mrt_controls(pipeline, blend_info,
                            builder->color_attachment_formats,
                            &pipeline->rop_reads_dst,
                            &pipeline->color_bandwidth_per_sample);

   uint32_t blend_enable_mask =
      pipeline->rop_reads_dst ? pipeline->color_write_enable : pipeline->blend_enable;
   tu6_emit_blend_control(pipeline, blend_enable_mask,
                          builder->use_dual_src_blend, msaa_info);

   if (tu_pipeline_static_state(pipeline, &cs, TU_DYNAMIC_STATE_BLEND,
                                blend_info->attachmentCount * 3 + 4)) {
      tu6_emit_blend(&cs, pipeline);
      assert(cs.cur == cs.end); /* validate draw state size */
   }

   /* Disable LRZ writes when blend or logic op that reads the destination is
    * enabled, since the resulting pixel value from the blend-draw depends on
    * an earlier draw, which LRZ in the draw pass could early-reject if the
    * previous blend-enabled draw wrote LRZ.
    *
    * TODO: We need to disable LRZ writes only for the binning pass.
    * Therefore, we need to emit it in a separate draw state. We keep
    * it disabled for sysmem path as well for the moment.
    */
   if (blend_enable_mask)
      pipeline->lrz.force_disable_mask |= TU_LRZ_FORCE_DISABLE_WRITE;

   for (int i = 0; i < blend_info->attachmentCount; i++) {
      VkPipelineColorBlendAttachmentState blendAttachment = blend_info->pAttachments[i];
      /* From the PoV of LRZ, having masked color channels is
       * the same as having blend enabled, in that the draw will
       * care about the fragments from an earlier draw.
       */
      VkFormat format = builder->color_attachment_formats[i];
      unsigned mask = MASK(vk_format_get_nr_components(format));
      if (format != VK_FORMAT_UNDEFINED &&
          ((blendAttachment.colorWriteMask & mask) != mask ||
           !(pipeline->color_write_enable & BIT(i)))) {
         pipeline->lrz.force_disable_mask |= TU_LRZ_FORCE_DISABLE_WRITE;
      }
   }

   if (tu_pipeline_static_state(pipeline, &cs, VK_DYNAMIC_STATE_BLEND_CONSTANTS, 5)) {
      tu_cs_emit_pkt4(&cs, REG_A6XX_RB_BLEND_RED_F32, 4);
      tu_cs_emit_array(&cs, (const uint32_t *) blend_info->blendConstants, 4);
   }

   const struct VkPipelineSampleLocationsStateCreateInfoEXT *sample_locations =
      vk_find_struct_const(msaa_info->pNext, PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT);
   const VkSampleLocationsInfoEXT *samp_loc = NULL;

   if (sample_locations && sample_locations->sampleLocationsEnable)
      samp_loc = &sample_locations->sampleLocationsInfo;

    if (tu_pipeline_static_state(pipeline, &cs, TU_DYNAMIC_STATE_SAMPLE_LOCATIONS,
                                 samp_loc ? 9 : 6)) {
      tu6_emit_sample_locations(&cs, samp_loc);
    }
}

static void
tu_pipeline_builder_parse_rasterization_order(
   struct tu_pipeline_builder *builder, struct tu_pipeline *pipeline)
{
   if (builder->rasterizer_discard)
      return;

   pipeline->subpass_feedback_loop_ds = builder->subpass_feedback_loop_ds;

   const VkPipelineColorBlendStateCreateInfo *blend_info =
      builder->create_info->pColorBlendState;

   const VkPipelineDepthStencilStateCreateInfo *ds_info =
      builder->create_info->pDepthStencilState;

   if (builder->use_color_attachments) {
      pipeline->raster_order_attachment_access =
         blend_info->flags &
         VK_PIPELINE_COLOR_BLEND_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_BIT_EXT;
   }

   if (builder->depth_attachment_format != VK_FORMAT_UNDEFINED) {
      pipeline->raster_order_attachment_access |=
         ds_info->flags &
         (VK_PIPELINE_DEPTH_STENCIL_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_DEPTH_ACCESS_BIT_EXT |
          VK_PIPELINE_DEPTH_STENCIL_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_STENCIL_ACCESS_BIT_EXT);
   }

   if (unlikely(builder->device->physical_device->instance->debug_flags & TU_DEBUG_RAST_ORDER))
      pipeline->raster_order_attachment_access = true;

   /* VK_EXT_blend_operation_advanced would also require ordered access
    * when implemented in the future.
    */

   uint32_t sysmem_prim_mode = NO_FLUSH;
   uint32_t gmem_prim_mode = NO_FLUSH;

   if (pipeline->raster_order_attachment_access) {
      /* VK_EXT_rasterization_order_attachment_access:
       *
       * This extension allow access to framebuffer attachments when used as
       * both input and color attachments from one fragment to the next,
       * in rasterization order, without explicit synchronization.
       */
      sysmem_prim_mode = FLUSH_PER_OVERLAP_AND_OVERWRITE;
      gmem_prim_mode = FLUSH_PER_OVERLAP;
      pipeline->sysmem_single_prim_mode = true;
   } else {
      /* If there is a feedback loop, then the shader can read the previous value
       * of a pixel being written out. It can also write some components and then
       * read different components without a barrier in between. This is a
       * problem in sysmem mode with UBWC, because the main buffer and flags
       * buffer can get out-of-sync if only one is flushed. We fix this by
       * setting the SINGLE_PRIM_MODE field to the same value that the blob does
       * for advanced_blend in sysmem mode if a feedback loop is detected.
       */
      if (builder->subpass_feedback_loop_color ||
          (builder->subpass_feedback_loop_ds &&
           (ds_info->depthWriteEnable || ds_info->stencilTestEnable))) {
         sysmem_prim_mode = FLUSH_PER_OVERLAP_AND_OVERWRITE;
         pipeline->sysmem_single_prim_mode = true;
      }
   }

   struct tu_cs cs;

   pipeline->prim_order_state_gmem = tu_cs_draw_state(&pipeline->cs, &cs, 2);
   tu_cs_emit_write_reg(&cs, REG_A6XX_GRAS_SC_CNTL,
                        A6XX_GRAS_SC_CNTL_CCUSINGLECACHELINESIZE(2) |
                        A6XX_GRAS_SC_CNTL_SINGLE_PRIM_MODE(gmem_prim_mode));

   pipeline->prim_order_state_sysmem = tu_cs_draw_state(&pipeline->cs, &cs, 2);
   tu_cs_emit_write_reg(&cs, REG_A6XX_GRAS_SC_CNTL,
                        A6XX_GRAS_SC_CNTL_CCUSINGLECACHELINESIZE(2) |
                        A6XX_GRAS_SC_CNTL_SINGLE_PRIM_MODE(sysmem_prim_mode));
}

static void
tu_pipeline_finish(struct tu_pipeline *pipeline,
                   struct tu_device *dev,
                   const VkAllocationCallbacks *alloc)
{
   tu_cs_finish(&pipeline->cs);
   pthread_mutex_lock(&dev->pipeline_mutex);
   tu_suballoc_bo_free(&dev->pipeline_suballoc, &pipeline->bo);
   pthread_mutex_unlock(&dev->pipeline_mutex);

   if (pipeline->pvtmem_bo)
      tu_bo_finish(dev, pipeline->pvtmem_bo);

   ralloc_free(pipeline->executables_mem_ctx);
}

static VkResult
tu_pipeline_builder_build(struct tu_pipeline_builder *builder,
                          struct tu_pipeline **pipeline)
{
   VkResult result;

   *pipeline = vk_object_zalloc(&builder->device->vk, builder->alloc,
                                sizeof(**pipeline), VK_OBJECT_TYPE_PIPELINE);
   if (!*pipeline)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   (*pipeline)->executables_mem_ctx = ralloc_context(NULL);
   util_dynarray_init(&(*pipeline)->executables, (*pipeline)->executables_mem_ctx);

   /* compile and upload shaders */
   result = tu_pipeline_builder_compile_shaders(builder, *pipeline);
   if (result != VK_SUCCESS) {
      vk_object_free(&builder->device->vk, builder->alloc, *pipeline);
      return result;
   }

   result = tu_pipeline_allocate_cs(builder->device, *pipeline,
                                    builder->layout, builder, NULL);
   if (result != VK_SUCCESS) {
      vk_object_free(&builder->device->vk, builder->alloc, *pipeline);
      return result;
   }

   for (uint32_t i = 0; i < ARRAY_SIZE(builder->shader_iova); i++)
      builder->shader_iova[i] =
         tu_upload_variant(*pipeline, builder->shaders->variants[i]);

   builder->binning_vs_iova =
      tu_upload_variant(*pipeline, builder->binning_variant);

   /* Setup private memory. Note that because we're sharing the same private
    * memory for all stages, all stages must use the same config, or else
    * fibers from one stage might overwrite fibers in another.
    */

   uint32_t pvtmem_size = 0;
   bool per_wave = true;
   for (uint32_t i = 0; i < ARRAY_SIZE(builder->shaders->variants); i++) {
      if (builder->shaders->variants[i]) {
         pvtmem_size = MAX2(pvtmem_size, builder->shaders->variants[i]->pvtmem_size);
         if (!builder->shaders->variants[i]->pvtmem_per_wave)
            per_wave = false;
      }
   }

   if (builder->binning_variant) {
      pvtmem_size = MAX2(pvtmem_size, builder->binning_variant->pvtmem_size);
      if (!builder->binning_variant->pvtmem_per_wave)
         per_wave = false;
   }

   result = tu_setup_pvtmem(builder->device, *pipeline, &builder->pvtmem,
                            pvtmem_size, per_wave);
   if (result != VK_SUCCESS) {
      vk_object_free(&builder->device->vk, builder->alloc, *pipeline);
      return result;
   }

   tu_pipeline_builder_parse_dynamic(builder, *pipeline);
   tu_pipeline_builder_parse_shader_stages(builder, *pipeline);
   tu_pipeline_builder_parse_vertex_input(builder, *pipeline);
   tu_pipeline_builder_parse_input_assembly(builder, *pipeline);
   tu_pipeline_builder_parse_tessellation(builder, *pipeline);
   tu_pipeline_builder_parse_viewport(builder, *pipeline);
   tu_pipeline_builder_parse_rasterization(builder, *pipeline);
   tu_pipeline_builder_parse_depth_stencil(builder, *pipeline);
   tu_pipeline_builder_parse_multisample_and_color_blend(builder, *pipeline);
   tu_pipeline_builder_parse_rasterization_order(builder, *pipeline);
   tu6_emit_load_state(*pipeline, builder->layout);

   return VK_SUCCESS;
}

static void
tu_pipeline_builder_finish(struct tu_pipeline_builder *builder)
{
   if (builder->shaders)
      vk_pipeline_cache_object_unref(&builder->shaders->base);
   ralloc_free(builder->mem_ctx);
}

static void
tu_pipeline_builder_init_graphics(
   struct tu_pipeline_builder *builder,
   struct tu_device *dev,
   struct vk_pipeline_cache *cache,
   const VkGraphicsPipelineCreateInfo *create_info,
   const VkAllocationCallbacks *alloc)
{
   TU_FROM_HANDLE(tu_pipeline_layout, layout, create_info->layout);

   *builder = (struct tu_pipeline_builder) {
      .device = dev,
      .mem_ctx = ralloc_context(NULL),
      .cache = cache,
      .create_info = create_info,
      .alloc = alloc,
      .layout = layout,
   };

   bool rasterizer_discard_dynamic = false;
   if (create_info->pDynamicState) {
      for (uint32_t i = 0; i < create_info->pDynamicState->dynamicStateCount; i++) {
         if (create_info->pDynamicState->pDynamicStates[i] ==
               VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE) {
            rasterizer_discard_dynamic = true;
            break;
         }
      }
   }

   builder->rasterizer_discard =
      builder->create_info->pRasterizationState->rasterizerDiscardEnable &&
      !rasterizer_discard_dynamic;

   const VkPipelineRenderingCreateInfo *rendering_info =
      vk_find_struct_const(create_info->pNext, PIPELINE_RENDERING_CREATE_INFO);

   if (unlikely(dev->instance->debug_flags & TU_DEBUG_DYNAMIC) && !rendering_info)
      rendering_info = vk_get_pipeline_rendering_create_info(create_info);

   if (rendering_info) {
      builder->subpass_raster_order_attachment_access = false;
      builder->subpass_feedback_loop_ds = false;
      builder->subpass_feedback_loop_color = false;

      builder->multiview_mask = rendering_info->viewMask;

      /* We don't know with dynamic rendering whether the pipeline will be
       * used in a render pass with none of attachments enabled, so we have to
       * dynamically emit MSAA state.
       *
       * TODO: Move MSAA state to a separate draw state and emit it
       * dynamically only when the sample count is different from the
       * subpass's sample count.
       */
      builder->emit_msaa_state = !builder->rasterizer_discard;

      const VkRenderingSelfDependencyInfoMESA *self_dependency =
         vk_find_struct_const(rendering_info->pNext, RENDERING_SELF_DEPENDENCY_INFO_MESA);

      if (self_dependency) {
         builder->subpass_feedback_loop_ds =
            self_dependency->depthSelfDependency ||
            self_dependency->stencilSelfDependency;
         builder->subpass_feedback_loop_color =
            self_dependency->colorSelfDependencies;
      }

      if (!builder->rasterizer_discard) {
         builder->depth_attachment_format =
            rendering_info->depthAttachmentFormat == VK_FORMAT_UNDEFINED ?
            rendering_info->stencilAttachmentFormat :
            rendering_info->depthAttachmentFormat;

         builder->color_attachment_count =
            rendering_info->colorAttachmentCount;

         for (unsigned i = 0; i < rendering_info->colorAttachmentCount; i++) {
            builder->color_attachment_formats[i] =
               rendering_info->pColorAttachmentFormats[i];
            if (builder->color_attachment_formats[i] != VK_FORMAT_UNDEFINED) {
               builder->use_color_attachments = true;
               builder->render_components |= 0xf << (i * 4);
            }
         }
      }
   } else {
      const struct tu_render_pass *pass =
         tu_render_pass_from_handle(create_info->renderPass);
      const struct tu_subpass *subpass =
         &pass->subpasses[create_info->subpass];

      builder->subpass_raster_order_attachment_access =
         subpass->raster_order_attachment_access;
      builder->subpass_feedback_loop_color = subpass->feedback_loop_color;
      builder->subpass_feedback_loop_ds = subpass->feedback_loop_ds;

      builder->multiview_mask = subpass->multiview_mask;

      /* variableMultisampleRate support */
      builder->emit_msaa_state = (subpass->samples == 0) && !builder->rasterizer_discard;

      if (!builder->rasterizer_discard) {
         const uint32_t a = subpass->depth_stencil_attachment.attachment;
         builder->depth_attachment_format = (a != VK_ATTACHMENT_UNUSED) ?
            pass->attachments[a].format : VK_FORMAT_UNDEFINED;

         assert(subpass->color_count == 0 ||
                !create_info->pColorBlendState ||
                subpass->color_count == create_info->pColorBlendState->attachmentCount);
         builder->color_attachment_count = subpass->color_count;
         for (uint32_t i = 0; i < subpass->color_count; i++) {
            const uint32_t a = subpass->color_attachments[i].attachment;
            if (a == VK_ATTACHMENT_UNUSED)
               continue;

            builder->color_attachment_formats[i] = pass->attachments[a].format;
            builder->use_color_attachments = true;
            builder->render_components |= 0xf << (i * 4);
         }
      }
   }

   if (builder->create_info->flags & VK_PIPELINE_CREATE_COLOR_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT) {
      builder->subpass_feedback_loop_color = true;
      builder->feedback_loop_may_involve_textures = true;
   }

   if (builder->create_info->flags & VK_PIPELINE_CREATE_DEPTH_STENCIL_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT) {
      builder->subpass_feedback_loop_ds = true;
      builder->feedback_loop_may_involve_textures = true;
   }

   if (builder->rasterizer_discard) {
      builder->samples = VK_SAMPLE_COUNT_1_BIT;
   } else {
      builder->samples = create_info->pMultisampleState->rasterizationSamples;
      builder->alpha_to_coverage = create_info->pMultisampleState->alphaToCoverageEnable;

      if (tu_blend_state_is_dual_src(create_info->pColorBlendState)) {
         builder->color_attachment_count++;
         builder->use_dual_src_blend = true;
         /* dual source blending has an extra fs output in the 2nd slot */
         if (builder->color_attachment_formats[0] != VK_FORMAT_UNDEFINED)
            builder->render_components |= 0xf << 4;
      }
   }
}

static VkResult
tu_graphics_pipeline_create(VkDevice device,
                            VkPipelineCache pipelineCache,
                            const VkGraphicsPipelineCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkPipeline *pPipeline)
{
   TU_FROM_HANDLE(tu_device, dev, device);
   TU_FROM_HANDLE(vk_pipeline_cache, cache, pipelineCache);

   cache = cache ? cache : dev->mem_cache;

   struct tu_pipeline_builder builder;
   tu_pipeline_builder_init_graphics(&builder, dev, cache,
                                     pCreateInfo, pAllocator);

   struct tu_pipeline *pipeline = NULL;
   VkResult result = tu_pipeline_builder_build(&builder, &pipeline);
   tu_pipeline_builder_finish(&builder);

   if (result == VK_SUCCESS)
      *pPipeline = tu_pipeline_to_handle(pipeline);
   else
      *pPipeline = VK_NULL_HANDLE;

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateGraphicsPipelines(VkDevice device,
                           VkPipelineCache pipelineCache,
                           uint32_t count,
                           const VkGraphicsPipelineCreateInfo *pCreateInfos,
                           const VkAllocationCallbacks *pAllocator,
                           VkPipeline *pPipelines)
{
   MESA_TRACE_FUNC();
   VkResult final_result = VK_SUCCESS;
   uint32_t i = 0;

   for (; i < count; i++) {
      VkResult result = tu_graphics_pipeline_create(device, pipelineCache,
                                                    &pCreateInfos[i], pAllocator,
                                                    &pPipelines[i]);

      if (result != VK_SUCCESS) {
         final_result = result;
         pPipelines[i] = VK_NULL_HANDLE;

         if (pCreateInfos[i].flags &
             VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }

   for (; i < count; i++)
      pPipelines[i] = VK_NULL_HANDLE;

   return final_result;
}

static VkResult
tu_compute_pipeline_create(VkDevice device,
                           VkPipelineCache pipelineCache,
                           const VkComputePipelineCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkPipeline *pPipeline)
{
   TU_FROM_HANDLE(tu_device, dev, device);
   TU_FROM_HANDLE(vk_pipeline_cache, cache, pipelineCache);
   TU_FROM_HANDLE(tu_pipeline_layout, layout, pCreateInfo->layout);
   const VkPipelineShaderStageCreateInfo *stage_info = &pCreateInfo->stage;
   VkResult result;

   cache = cache ? cache : dev->mem_cache;

   struct tu_pipeline *pipeline;

   *pPipeline = VK_NULL_HANDLE;

   VkPipelineCreationFeedback pipeline_feedback = {
      .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
   };

   const VkPipelineCreationFeedbackCreateInfo *creation_feedback =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);

   int64_t pipeline_start = os_time_get_nano();

   pipeline = vk_object_zalloc(&dev->vk, pAllocator, sizeof(*pipeline),
                               VK_OBJECT_TYPE_PIPELINE);
   if (!pipeline)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   pipeline->executables_mem_ctx = ralloc_context(NULL);
   util_dynarray_init(&pipeline->executables, pipeline->executables_mem_ctx);
   pipeline->active_stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

   struct tu_shader_key key = { };
   tu_shader_key_init(&key, stage_info, dev);

   void *pipeline_mem_ctx = ralloc_context(NULL);

   unsigned char pipeline_sha1[20];
   tu_hash_compute(pipeline_sha1, stage_info, layout, &key, dev->compiler);

   struct tu_compiled_shaders *compiled = NULL;

   const bool executable_info = pCreateInfo->flags &
      VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR;

   bool application_cache_hit = false;

   if (!executable_info) {
      compiled =
         tu_pipeline_cache_lookup(cache, pipeline_sha1, sizeof(pipeline_sha1),
                                  &application_cache_hit);
   }

   if (application_cache_hit && cache != dev->mem_cache) {
      pipeline_feedback.flags |=
         VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;
   }

   if (tu6_shared_constants_enable(layout, dev->compiler)) {
      pipeline->shared_consts = (struct tu_push_constant_range) {
         .lo = 0,
         .dwords = layout->push_constant_size / 4,
      };
   }

   char *nir_initial_disasm = NULL;

   if (!compiled) {
      if (pCreateInfo->flags &
          VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT) {
         result = VK_PIPELINE_COMPILE_REQUIRED;
         goto fail;
      }

      struct ir3_shader_key ir3_key = {};

      nir_shader *nir = tu_spirv_to_nir(dev, pipeline_mem_ctx, stage_info,
                                        MESA_SHADER_COMPUTE);

      nir_initial_disasm = executable_info ?
         nir_shader_as_str(nir, pipeline->executables_mem_ctx) : NULL;

      struct tu_shader *shader =
         tu_shader_create(dev, nir, &key, layout, pAllocator);
      if (!shader) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      compiled = tu_shaders_init(dev, &pipeline_sha1, sizeof(pipeline_sha1));
      if (!compiled) {
         tu_shader_destroy(dev, shader, pAllocator);
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      compiled->active_desc_sets = shader->active_desc_sets;
      compiled->push_consts[MESA_SHADER_COMPUTE] = shader->push_consts;

      struct ir3_shader_variant *v =
         ir3_shader_create_variant(shader->ir3_shader, &ir3_key, executable_info);

      tu_shader_destroy(dev, shader, pAllocator);

      if (!v) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      compiled->variants[MESA_SHADER_COMPUTE] = v;

      compiled = tu_pipeline_cache_insert(cache, compiled);
   }

   pipeline_feedback.duration = os_time_get_nano() - pipeline_start;

   if (creation_feedback) {
      *creation_feedback->pPipelineCreationFeedback = pipeline_feedback;
      assert(creation_feedback->pipelineStageCreationFeedbackCount == 1);
      creation_feedback->pPipelineStageCreationFeedbacks[0] = pipeline_feedback;
   }

   pipeline->active_desc_sets = compiled->active_desc_sets;

   struct ir3_shader_variant *v = compiled->variants[MESA_SHADER_COMPUTE];

   tu_pipeline_set_linkage(&pipeline->program.link[MESA_SHADER_COMPUTE],
                           &compiled->push_consts[MESA_SHADER_COMPUTE], v);

   result = tu_pipeline_allocate_cs(dev, pipeline, layout, NULL, v);
   if (result != VK_SUCCESS)
      goto fail;

   uint64_t shader_iova = tu_upload_variant(pipeline, v);

   struct tu_pvtmem_config pvtmem;
   tu_setup_pvtmem(dev, pipeline, &pvtmem, v->pvtmem_size, v->pvtmem_per_wave);

   for (int i = 0; i < 3; i++)
      pipeline->compute.local_size[i] = v->local_size[i];

   pipeline->compute.subgroup_size = v->info.double_threadsize ? 128 : 64;

   struct tu_cs prog_cs;
   uint32_t additional_reserve_size = tu_xs_get_additional_cs_size_dwords(v);
   tu_cs_begin_sub_stream(&pipeline->cs, 64 + additional_reserve_size, &prog_cs);
   tu6_emit_cs_config(&prog_cs, v, &pvtmem, shader_iova);
   pipeline->program.state = tu_cs_end_draw_state(&pipeline->cs, &prog_cs);

   tu6_emit_load_state(pipeline, layout);

   tu_append_executable(pipeline, v, nir_initial_disasm);

   vk_pipeline_cache_object_unref(&compiled->base);
   ralloc_free(pipeline_mem_ctx);

   *pPipeline = tu_pipeline_to_handle(pipeline);

   return VK_SUCCESS;

fail:
   if (compiled)
      vk_pipeline_cache_object_unref(&compiled->base);

   ralloc_free(pipeline_mem_ctx);

   vk_object_free(&dev->vk, pAllocator, pipeline);

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateComputePipelines(VkDevice device,
                          VkPipelineCache pipelineCache,
                          uint32_t count,
                          const VkComputePipelineCreateInfo *pCreateInfos,
                          const VkAllocationCallbacks *pAllocator,
                          VkPipeline *pPipelines)
{
   MESA_TRACE_FUNC();
   VkResult final_result = VK_SUCCESS;
   uint32_t i = 0;

   for (; i < count; i++) {
      VkResult result = tu_compute_pipeline_create(device, pipelineCache,
                                                   &pCreateInfos[i],
                                                   pAllocator, &pPipelines[i]);
      if (result != VK_SUCCESS) {
         final_result = result;
         pPipelines[i] = VK_NULL_HANDLE;

         if (pCreateInfos[i].flags &
             VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }

   for (; i < count; i++)
      pPipelines[i] = VK_NULL_HANDLE;

   return final_result;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyPipeline(VkDevice _device,
                   VkPipeline _pipeline,
                   const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, dev, _device);
   TU_FROM_HANDLE(tu_pipeline, pipeline, _pipeline);

   if (!_pipeline)
      return;

   tu_pipeline_finish(pipeline, dev, pAllocator);
   vk_object_free(&dev->vk, pAllocator, pipeline);
}

#define WRITE_STR(field, ...) ({                                \
   memset(field, 0, sizeof(field));                             \
   UNUSED int _i = snprintf(field, sizeof(field), __VA_ARGS__); \
   assert(_i > 0 && _i < sizeof(field));                        \
})

static const struct tu_pipeline_executable *
tu_pipeline_get_executable(struct tu_pipeline *pipeline, uint32_t index)
{
   assert(index < util_dynarray_num_elements(&pipeline->executables,
                                             struct tu_pipeline_executable));
   return util_dynarray_element(
      &pipeline->executables, struct tu_pipeline_executable, index);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetPipelineExecutablePropertiesKHR(
      VkDevice _device,
      const VkPipelineInfoKHR* pPipelineInfo,
      uint32_t* pExecutableCount,
      VkPipelineExecutablePropertiesKHR* pProperties)
{
   TU_FROM_HANDLE(tu_device, dev, _device);
   TU_FROM_HANDLE(tu_pipeline, pipeline, pPipelineInfo->pipeline);
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutablePropertiesKHR, out,
                          pProperties, pExecutableCount);

   util_dynarray_foreach (&pipeline->executables, struct tu_pipeline_executable, exe) {
      vk_outarray_append_typed(VkPipelineExecutablePropertiesKHR, &out, props) {
         gl_shader_stage stage = exe->stage;
         props->stages = mesa_to_vk_shader_stage(stage);

         if (!exe->is_binning)
            WRITE_STR(props->name, "%s", _mesa_shader_stage_to_abbrev(stage));
         else
            WRITE_STR(props->name, "Binning VS");

         WRITE_STR(props->description, "%s", _mesa_shader_stage_to_string(stage));

         props->subgroupSize =
            dev->compiler->threadsize_base * (exe->stats.double_threadsize ? 2 : 1);
      }
   }

   return vk_outarray_status(&out);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetPipelineExecutableStatisticsKHR(
      VkDevice _device,
      const VkPipelineExecutableInfoKHR* pExecutableInfo,
      uint32_t* pStatisticCount,
      VkPipelineExecutableStatisticKHR* pStatistics)
{
   TU_FROM_HANDLE(tu_pipeline, pipeline, pExecutableInfo->pipeline);
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableStatisticKHR, out,
                          pStatistics, pStatisticCount);

   const struct tu_pipeline_executable *exe =
      tu_pipeline_get_executable(pipeline, pExecutableInfo->executableIndex);

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Max Waves Per Core");
      WRITE_STR(stat->description,
                "Maximum number of simultaneous waves per core.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.max_waves;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Instruction Count");
      WRITE_STR(stat->description,
                "Total number of IR3 instructions in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.instrs_count;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Code size");
      WRITE_STR(stat->description,
                "Total number of dwords in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.sizedwords;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "NOPs Count");
      WRITE_STR(stat->description,
                "Number of NOP instructions in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.nops_count;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "MOV Count");
      WRITE_STR(stat->description,
                "Number of MOV instructions in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.mov_count;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "COV Count");
      WRITE_STR(stat->description,
                "Number of COV instructions in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.cov_count;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Registers used");
      WRITE_STR(stat->description,
                "Number of registers used in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.max_reg + 1;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Half-registers used");
      WRITE_STR(stat->description,
                "Number of half-registers used in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.max_half_reg + 1;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Instructions with SS sync bit");
      WRITE_STR(stat->description,
                "SS bit is set for instructions which depend on a result "
                "of \"long\" instructions to prevent RAW hazard.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.ss;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Instructions with SY sync bit");
      WRITE_STR(stat->description,
                "SY bit is set for instructions which depend on a result "
                "of loads from global memory to prevent RAW hazard.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.sy;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Estimated cycles stalled on SS");
      WRITE_STR(stat->description,
                "A better metric to estimate the impact of SS syncs.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.sstall;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "Estimated cycles stalled on SY");
      WRITE_STR(stat->description,
                "A better metric to estimate the impact of SY syncs.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.systall;
   }

   for (int i = 0; i < ARRAY_SIZE(exe->stats.instrs_per_cat); i++) {
      vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
         WRITE_STR(stat->name, "cat%d instructions", i);
         WRITE_STR(stat->description,
                  "Number of cat%d instructions.", i);
         stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
         stat->value.u64 = exe->stats.instrs_per_cat[i];
      }
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "STP Count");
      WRITE_STR(stat->description,
                "Number of STore Private instructions in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.stp_count;
   }

   vk_outarray_append_typed(VkPipelineExecutableStatisticKHR, &out, stat) {
      WRITE_STR(stat->name, "LDP Count");
      WRITE_STR(stat->description,
                "Number of LoaD Private instructions in the final generated "
                "shader executable.");
      stat->format = VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR;
      stat->value.u64 = exe->stats.ldp_count;
   }

   return vk_outarray_status(&out);
}

static bool
write_ir_text(VkPipelineExecutableInternalRepresentationKHR* ir,
              const char *data)
{
   ir->isText = VK_TRUE;

   size_t data_len = strlen(data) + 1;

   if (ir->pData == NULL) {
      ir->dataSize = data_len;
      return true;
   }

   strncpy(ir->pData, data, ir->dataSize);
   if (ir->dataSize < data_len)
      return false;

   ir->dataSize = data_len;
   return true;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetPipelineExecutableInternalRepresentationsKHR(
    VkDevice _device,
    const VkPipelineExecutableInfoKHR* pExecutableInfo,
    uint32_t* pInternalRepresentationCount,
    VkPipelineExecutableInternalRepresentationKHR* pInternalRepresentations)
{
   TU_FROM_HANDLE(tu_pipeline, pipeline, pExecutableInfo->pipeline);
   VK_OUTARRAY_MAKE_TYPED(VkPipelineExecutableInternalRepresentationKHR, out,
                          pInternalRepresentations, pInternalRepresentationCount);
   bool incomplete_text = false;

   const struct tu_pipeline_executable *exe =
      tu_pipeline_get_executable(pipeline, pExecutableInfo->executableIndex);

   if (exe->nir_from_spirv) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR, &out, ir) {
         WRITE_STR(ir->name, "NIR from SPIRV");
         WRITE_STR(ir->description,
                   "Initial NIR before any optimizations");

         if (!write_ir_text(ir, exe->nir_from_spirv))
            incomplete_text = true;
      }
   }

   if (exe->nir_final) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR, &out, ir) {
         WRITE_STR(ir->name, "Final NIR");
         WRITE_STR(ir->description,
                   "Final NIR before going into the back-end compiler");

         if (!write_ir_text(ir, exe->nir_final))
            incomplete_text = true;
      }
   }

   if (exe->disasm) {
      vk_outarray_append_typed(VkPipelineExecutableInternalRepresentationKHR, &out, ir) {
         WRITE_STR(ir->name, "IR3 Assembly");
         WRITE_STR(ir->description,
                   "Final IR3 assembly for the generated shader binary");

         if (!write_ir_text(ir, exe->disasm))
            incomplete_text = true;
      }
   }

   return incomplete_text ? VK_INCOMPLETE : vk_outarray_status(&out);
}
