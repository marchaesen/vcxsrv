/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "tu_private.h"

#include "main/menums.h"
#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "spirv/nir_spirv.h"
#include "util/debug.h"
#include "util/mesa-sha1.h"
#include "util/u_atomic.h"
#include "vk_format.h"
#include "vk_util.h"

#include "tu_cs.h"

struct tu_pipeline_builder
{
   struct tu_device *device;
   struct tu_pipeline_cache *cache;
   const VkAllocationCallbacks *alloc;
   const VkGraphicsPipelineCreateInfo *create_info;

   struct tu_shader *shaders[MESA_SHADER_STAGES];
   uint32_t shader_offsets[MESA_SHADER_STAGES];
   uint32_t binning_vs_offset;
   uint32_t shader_total_size;

   bool rasterizer_discard;
   /* these states are affectd by rasterizer_discard */
   VkSampleCountFlagBits samples;
   bool use_depth_stencil_attachment;
   bool use_color_attachments;
   uint32_t color_attachment_count;
   VkFormat color_attachment_formats[MAX_RTS];
};

static enum tu_dynamic_state_bits
tu_dynamic_state_bit(VkDynamicState state)
{
   switch (state) {
   case VK_DYNAMIC_STATE_VIEWPORT:
      return TU_DYNAMIC_VIEWPORT;
   case VK_DYNAMIC_STATE_SCISSOR:
      return TU_DYNAMIC_SCISSOR;
   case VK_DYNAMIC_STATE_LINE_WIDTH:
      return TU_DYNAMIC_LINE_WIDTH;
   case VK_DYNAMIC_STATE_DEPTH_BIAS:
      return TU_DYNAMIC_DEPTH_BIAS;
   case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
      return TU_DYNAMIC_BLEND_CONSTANTS;
   case VK_DYNAMIC_STATE_DEPTH_BOUNDS:
      return TU_DYNAMIC_DEPTH_BOUNDS;
   case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
      return TU_DYNAMIC_STENCIL_COMPARE_MASK;
   case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:
      return TU_DYNAMIC_STENCIL_WRITE_MASK;
   case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
      return TU_DYNAMIC_STENCIL_REFERENCE;
   default:
      unreachable("invalid dynamic state");
      return 0;
   }
}

static gl_shader_stage
tu_shader_stage(VkShaderStageFlagBits stage)
{
   switch (stage) {
   case VK_SHADER_STAGE_VERTEX_BIT:
      return MESA_SHADER_VERTEX;
   case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
      return MESA_SHADER_TESS_CTRL;
   case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
      return MESA_SHADER_TESS_EVAL;
   case VK_SHADER_STAGE_GEOMETRY_BIT:
      return MESA_SHADER_GEOMETRY;
   case VK_SHADER_STAGE_FRAGMENT_BIT:
      return MESA_SHADER_FRAGMENT;
   case VK_SHADER_STAGE_COMPUTE_BIT:
      return MESA_SHADER_COMPUTE;
   default:
      unreachable("invalid VkShaderStageFlagBits");
      return MESA_SHADER_NONE;
   }
}

static const VkVertexInputAttributeDescription *
tu_find_vertex_input_attribute(
   const VkPipelineVertexInputStateCreateInfo *vi_info, uint32_t slot)
{
   assert(slot >= VERT_ATTRIB_GENERIC0);
   slot -= VERT_ATTRIB_GENERIC0;
   for (uint32_t i = 0; i < vi_info->vertexAttributeDescriptionCount; i++) {
      if (vi_info->pVertexAttributeDescriptions[i].location == slot)
         return &vi_info->pVertexAttributeDescriptions[i];
   }
   return NULL;
}

static const VkVertexInputBindingDescription *
tu_find_vertex_input_binding(
   const VkPipelineVertexInputStateCreateInfo *vi_info,
   const VkVertexInputAttributeDescription *vi_attr)
{
   assert(vi_attr);
   for (uint32_t i = 0; i < vi_info->vertexBindingDescriptionCount; i++) {
      if (vi_info->pVertexBindingDescriptions[i].binding == vi_attr->binding)
         return &vi_info->pVertexBindingDescriptions[i];
   }
   return NULL;
}

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

static enum pc_di_primtype
tu6_primtype(VkPrimitiveTopology topology)
{
   switch (topology) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return DI_PT_POINTLIST;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
      return DI_PT_LINELIST;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      return DI_PT_LINESTRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
      return DI_PT_TRILIST;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      return DI_PT_TRILIST;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      return DI_PT_TRIFAN;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
      return DI_PT_LINE_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
      return DI_PT_LINESTRIP_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
      return DI_PT_TRI_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
      return DI_PT_TRISTRIP_ADJ;
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
   default:
      unreachable("invalid primitive topology");
      return DI_PT_NONE;
   }
}

static enum adreno_compare_func
tu6_compare_func(VkCompareOp op)
{
   switch (op) {
   case VK_COMPARE_OP_NEVER:
      return FUNC_NEVER;
   case VK_COMPARE_OP_LESS:
      return FUNC_LESS;
   case VK_COMPARE_OP_EQUAL:
      return FUNC_EQUAL;
   case VK_COMPARE_OP_LESS_OR_EQUAL:
      return FUNC_LEQUAL;
   case VK_COMPARE_OP_GREATER:
      return FUNC_GREATER;
   case VK_COMPARE_OP_NOT_EQUAL:
      return FUNC_NOTEQUAL;
   case VK_COMPARE_OP_GREATER_OR_EQUAL:
      return FUNC_GEQUAL;
   case VK_COMPARE_OP_ALWAYS:
      return FUNC_ALWAYS;
   default:
      unreachable("invalid VkCompareOp");
      return FUNC_NEVER;
   }
}

static enum adreno_stencil_op
tu6_stencil_op(VkStencilOp op)
{
   switch (op) {
   case VK_STENCIL_OP_KEEP:
      return STENCIL_KEEP;
   case VK_STENCIL_OP_ZERO:
      return STENCIL_ZERO;
   case VK_STENCIL_OP_REPLACE:
      return STENCIL_REPLACE;
   case VK_STENCIL_OP_INCREMENT_AND_CLAMP:
      return STENCIL_INCR_CLAMP;
   case VK_STENCIL_OP_DECREMENT_AND_CLAMP:
      return STENCIL_DECR_CLAMP;
   case VK_STENCIL_OP_INVERT:
      return STENCIL_INVERT;
   case VK_STENCIL_OP_INCREMENT_AND_WRAP:
      return STENCIL_INCR_WRAP;
   case VK_STENCIL_OP_DECREMENT_AND_WRAP:
      return STENCIL_DECR_WRAP;
   default:
      unreachable("invalid VkStencilOp");
      return STENCIL_KEEP;
   }
}

static enum a3xx_rop_code
tu6_rop(VkLogicOp op)
{
   switch (op) {
   case VK_LOGIC_OP_CLEAR:
      return ROP_CLEAR;
   case VK_LOGIC_OP_AND:
      return ROP_AND;
   case VK_LOGIC_OP_AND_REVERSE:
      return ROP_AND_REVERSE;
   case VK_LOGIC_OP_COPY:
      return ROP_COPY;
   case VK_LOGIC_OP_AND_INVERTED:
      return ROP_AND_INVERTED;
   case VK_LOGIC_OP_NO_OP:
      return ROP_NOOP;
   case VK_LOGIC_OP_XOR:
      return ROP_XOR;
   case VK_LOGIC_OP_OR:
      return ROP_OR;
   case VK_LOGIC_OP_NOR:
      return ROP_NOR;
   case VK_LOGIC_OP_EQUIVALENT:
      return ROP_EQUIV;
   case VK_LOGIC_OP_INVERT:
      return ROP_INVERT;
   case VK_LOGIC_OP_OR_REVERSE:
      return ROP_OR_REVERSE;
   case VK_LOGIC_OP_COPY_INVERTED:
      return ROP_COPY_INVERTED;
   case VK_LOGIC_OP_OR_INVERTED:
      return ROP_OR_INVERTED;
   case VK_LOGIC_OP_NAND:
      return ROP_NAND;
   case VK_LOGIC_OP_SET:
      return ROP_SET;
   default:
      unreachable("invalid VkLogicOp");
      return ROP_NOOP;
   }
}

static enum adreno_rb_blend_factor
tu6_blend_factor(VkBlendFactor factor)
{
   switch (factor) {
   case VK_BLEND_FACTOR_ZERO:
      return FACTOR_ZERO;
   case VK_BLEND_FACTOR_ONE:
      return FACTOR_ONE;
   case VK_BLEND_FACTOR_SRC_COLOR:
      return FACTOR_SRC_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
      return FACTOR_ONE_MINUS_SRC_COLOR;
   case VK_BLEND_FACTOR_DST_COLOR:
      return FACTOR_DST_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
      return FACTOR_ONE_MINUS_DST_COLOR;
   case VK_BLEND_FACTOR_SRC_ALPHA:
      return FACTOR_SRC_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
      return FACTOR_ONE_MINUS_SRC_ALPHA;
   case VK_BLEND_FACTOR_DST_ALPHA:
      return FACTOR_DST_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
      return FACTOR_ONE_MINUS_DST_ALPHA;
   case VK_BLEND_FACTOR_CONSTANT_COLOR:
      return FACTOR_CONSTANT_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
      return FACTOR_ONE_MINUS_CONSTANT_COLOR;
   case VK_BLEND_FACTOR_CONSTANT_ALPHA:
      return FACTOR_CONSTANT_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
      return FACTOR_ONE_MINUS_CONSTANT_ALPHA;
   case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
      return FACTOR_SRC_ALPHA_SATURATE;
   case VK_BLEND_FACTOR_SRC1_COLOR:
      return FACTOR_SRC1_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
      return FACTOR_ONE_MINUS_SRC1_COLOR;
   case VK_BLEND_FACTOR_SRC1_ALPHA:
      return FACTOR_SRC1_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
      return FACTOR_ONE_MINUS_SRC1_ALPHA;
   default:
      unreachable("invalid VkBlendFactor");
      return FACTOR_ZERO;
   }
}

static enum a3xx_rb_blend_opcode
tu6_blend_op(VkBlendOp op)
{
   switch (op) {
   case VK_BLEND_OP_ADD:
      return BLEND_DST_PLUS_SRC;
   case VK_BLEND_OP_SUBTRACT:
      return BLEND_SRC_MINUS_DST;
   case VK_BLEND_OP_REVERSE_SUBTRACT:
      return BLEND_DST_MINUS_SRC;
   case VK_BLEND_OP_MIN:
      return BLEND_MIN_DST_SRC;
   case VK_BLEND_OP_MAX:
      return BLEND_MAX_DST_SRC;
   default:
      unreachable("invalid VkBlendOp");
      return BLEND_DST_PLUS_SRC;
   }
}

static void
tu6_emit_vs_config(struct tu_cs *cs, const struct ir3_shader_variant *vs)
{
   uint32_t sp_vs_ctrl =
      A6XX_SP_VS_CTRL_REG0_THREADSIZE(FOUR_QUADS) |
      A6XX_SP_VS_CTRL_REG0_FULLREGFOOTPRINT(vs->info.max_reg + 1) |
      A6XX_SP_VS_CTRL_REG0_MERGEDREGS |
      A6XX_SP_VS_CTRL_REG0_BRANCHSTACK(vs->branchstack);
   if (vs->num_samp)
      sp_vs_ctrl |= A6XX_SP_VS_CTRL_REG0_PIXLODENABLE;

   uint32_t sp_vs_config = A6XX_SP_VS_CONFIG_NTEX(vs->num_samp) |
                           A6XX_SP_VS_CONFIG_NSAMP(vs->num_samp);
   if (vs->instrlen)
      sp_vs_config |= A6XX_SP_VS_CONFIG_ENABLED;

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_VS_CTRL_REG0, 1);
   tu_cs_emit(cs, sp_vs_ctrl);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_VS_CONFIG, 2);
   tu_cs_emit(cs, sp_vs_config);
   tu_cs_emit(cs, vs->instrlen);

   tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_VS_CNTL, 1);
   tu_cs_emit(cs, A6XX_HLSQ_VS_CNTL_CONSTLEN(align(vs->constlen, 4)) | 0x100);
}

static void
tu6_emit_hs_config(struct tu_cs *cs, const struct ir3_shader_variant *hs)
{
   uint32_t sp_hs_config = 0;
   if (hs->instrlen)
      sp_hs_config |= A6XX_SP_HS_CONFIG_ENABLED;

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_HS_UNKNOWN_A831, 1);
   tu_cs_emit(cs, 0);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_HS_CONFIG, 2);
   tu_cs_emit(cs, sp_hs_config);
   tu_cs_emit(cs, hs->instrlen);

   tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_HS_CNTL, 1);
   tu_cs_emit(cs, A6XX_HLSQ_HS_CNTL_CONSTLEN(align(hs->constlen, 4)));
}

static void
tu6_emit_ds_config(struct tu_cs *cs, const struct ir3_shader_variant *ds)
{
   uint32_t sp_ds_config = 0;
   if (ds->instrlen)
      sp_ds_config |= A6XX_SP_DS_CONFIG_ENABLED;

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_DS_CONFIG, 2);
   tu_cs_emit(cs, sp_ds_config);
   tu_cs_emit(cs, ds->instrlen);

   tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_DS_CNTL, 1);
   tu_cs_emit(cs, A6XX_HLSQ_DS_CNTL_CONSTLEN(align(ds->constlen, 4)));
}

static void
tu6_emit_gs_config(struct tu_cs *cs, const struct ir3_shader_variant *gs)
{
   uint32_t sp_gs_config = 0;
   if (gs->instrlen)
      sp_gs_config |= A6XX_SP_GS_CONFIG_ENABLED;

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_GS_UNKNOWN_A871, 1);
   tu_cs_emit(cs, 0);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_GS_CONFIG, 2);
   tu_cs_emit(cs, sp_gs_config);
   tu_cs_emit(cs, gs->instrlen);

   tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_GS_CNTL, 1);
   tu_cs_emit(cs, A6XX_HLSQ_GS_CNTL_CONSTLEN(align(gs->constlen, 4)));
}

static void
tu6_emit_fs_config(struct tu_cs *cs, const struct ir3_shader_variant *fs)
{
   uint32_t sp_fs_ctrl =
      A6XX_SP_FS_CTRL_REG0_THREADSIZE(FOUR_QUADS) | 0x1000000 |
      A6XX_SP_FS_CTRL_REG0_FULLREGFOOTPRINT(fs->info.max_reg + 1) |
      A6XX_SP_FS_CTRL_REG0_MERGEDREGS |
      A6XX_SP_FS_CTRL_REG0_BRANCHSTACK(fs->branchstack);
   if (fs->total_in > 0 || fs->frag_coord)
      sp_fs_ctrl |= A6XX_SP_FS_CTRL_REG0_VARYING;
   if (fs->num_samp > 0)
      sp_fs_ctrl |= A6XX_SP_FS_CTRL_REG0_PIXLODENABLE;

   uint32_t sp_fs_config = A6XX_SP_FS_CONFIG_NTEX(fs->num_samp) |
                           A6XX_SP_FS_CONFIG_NSAMP(fs->num_samp);
   if (fs->instrlen)
      sp_fs_config |= A6XX_SP_FS_CONFIG_ENABLED;

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_UNKNOWN_A99E, 1);
   tu_cs_emit(cs, 0x7fc0);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_UNKNOWN_A9A8, 1);
   tu_cs_emit(cs, 0);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_UNKNOWN_AB00, 1);
   tu_cs_emit(cs, 0x5);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_CTRL_REG0, 1);
   tu_cs_emit(cs, sp_fs_ctrl);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_CONFIG, 2);
   tu_cs_emit(cs, sp_fs_config);
   tu_cs_emit(cs, fs->instrlen);

   tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_FS_CNTL, 1);
   tu_cs_emit(cs, A6XX_HLSQ_FS_CNTL_CONSTLEN(align(fs->constlen, 4)) | 0x100);
}

static void
tu6_emit_vs_system_values(struct tu_cs *cs,
                          const struct ir3_shader_variant *vs)
{
   const uint32_t vertexid_regid =
      ir3_find_sysval_regid(vs, SYSTEM_VALUE_VERTEX_ID_ZERO_BASE);
   const uint32_t instanceid_regid =
      ir3_find_sysval_regid(vs, SYSTEM_VALUE_INSTANCE_ID);

   tu_cs_emit_pkt4(cs, REG_A6XX_VFD_CONTROL_1, 6);
   tu_cs_emit(cs, A6XX_VFD_CONTROL_1_REGID4VTX(vertexid_regid) |
                     A6XX_VFD_CONTROL_1_REGID4INST(instanceid_regid) |
                     0xfcfc0000);
   tu_cs_emit(cs, 0x0000fcfc); /* VFD_CONTROL_2 */
   tu_cs_emit(cs, 0xfcfcfcfc); /* VFD_CONTROL_3 */
   tu_cs_emit(cs, 0x000000fc); /* VFD_CONTROL_4 */
   tu_cs_emit(cs, 0x0000fcfc); /* VFD_CONTROL_5 */
   tu_cs_emit(cs, 0x00000000); /* VFD_CONTROL_6 */
}

static void
tu6_emit_vpc(struct tu_cs *cs,
             const struct ir3_shader_variant *vs,
             const struct ir3_shader_variant *fs,
             bool binning_pass)
{
   struct ir3_shader_linkage linkage = { 0 };
   ir3_link_shaders(&linkage, vs, fs);

   if (vs->shader->stream_output.num_outputs && !binning_pass)
      tu_finishme("stream output");

   BITSET_DECLARE(vpc_var_enables, 128) = { 0 };
   for (uint32_t i = 0; i < linkage.cnt; i++) {
      const uint32_t comp_count = util_last_bit(linkage.var[i].compmask);
      for (uint32_t j = 0; j < comp_count; j++)
         BITSET_SET(vpc_var_enables, linkage.var[i].loc + j);
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_VAR_DISABLE(0), 4);
   tu_cs_emit(cs, ~vpc_var_enables[0]);
   tu_cs_emit(cs, ~vpc_var_enables[1]);
   tu_cs_emit(cs, ~vpc_var_enables[2]);
   tu_cs_emit(cs, ~vpc_var_enables[3]);

   /* a6xx finds position/pointsize at the end */
   const uint32_t position_regid =
      ir3_find_output_regid(vs, VARYING_SLOT_POS);
   const uint32_t pointsize_regid =
      ir3_find_output_regid(vs, VARYING_SLOT_PSIZ);
   uint32_t pointsize_loc = 0xff;
   if (position_regid != regid(63, 0))
      ir3_link_add(&linkage, position_regid, 0xf, linkage.max_loc);
   if (pointsize_regid != regid(63, 0)) {
      pointsize_loc = linkage.max_loc;
      ir3_link_add(&linkage, pointsize_regid, 0x1, linkage.max_loc);
   }

   /* map vs outputs to VPC */
   assert(linkage.cnt <= 32);
   const uint32_t sp_vs_out_count = (linkage.cnt + 1) / 2;
   const uint32_t sp_vs_vpc_dst_count = (linkage.cnt + 3) / 4;
   uint32_t sp_vs_out[16];
   uint32_t sp_vs_vpc_dst[8];
   sp_vs_out[sp_vs_out_count - 1] = 0;
   sp_vs_vpc_dst[sp_vs_vpc_dst_count - 1] = 0;
   for (uint32_t i = 0; i < linkage.cnt; i++) {
      ((uint16_t *) sp_vs_out)[i] =
         A6XX_SP_VS_OUT_REG_A_REGID(linkage.var[i].regid) |
         A6XX_SP_VS_OUT_REG_A_COMPMASK(linkage.var[i].compmask);
      ((uint8_t *) sp_vs_vpc_dst)[i] =
         A6XX_SP_VS_VPC_DST_REG_OUTLOC0(linkage.var[i].loc);
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_VS_OUT_REG(0), sp_vs_out_count);
   tu_cs_emit_array(cs, sp_vs_out, sp_vs_out_count);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_VS_VPC_DST_REG(0), sp_vs_vpc_dst_count);
   tu_cs_emit_array(cs, sp_vs_vpc_dst, sp_vs_vpc_dst_count);

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_CNTL_0, 1);
   tu_cs_emit(cs, A6XX_VPC_CNTL_0_NUMNONPOSVAR(fs->total_in) |
                     (fs->total_in > 0 ? A6XX_VPC_CNTL_0_VARYING : 0) |
                     0xff00ff00);

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_PACK, 1);
   tu_cs_emit(cs, A6XX_VPC_PACK_NUMNONPOSVAR(fs->total_in) |
                     A6XX_VPC_PACK_PSIZELOC(pointsize_loc) |
                     A6XX_VPC_PACK_STRIDE_IN_VPC(linkage.max_loc));

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_GS_SIV_CNTL, 1);
   tu_cs_emit(cs, 0x0000ffff); /* XXX */

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_PRIMITIVE_CNTL, 1);
   tu_cs_emit(cs, A6XX_SP_PRIMITIVE_CNTL_VSOUT(linkage.cnt));

   tu_cs_emit_pkt4(cs, REG_A6XX_PC_PRIMITIVE_CNTL_1, 1);
   tu_cs_emit(cs, A6XX_PC_PRIMITIVE_CNTL_1_STRIDE_IN_VPC(linkage.max_loc) |
                     (vs->writes_psize ? A6XX_PC_PRIMITIVE_CNTL_1_PSIZE : 0));
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
   } else if ((fs->inputs[index].interpolate == INTERP_MODE_FLAT) ||
              fs->inputs[index].rasterflat) {
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
                           const struct ir3_shader_variant *fs,
                           bool binning_pass)
{
   uint32_t interp_modes[8] = { 0 };
   uint32_t ps_repl_modes[8] = { 0 };

   if (!binning_pass) {
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
      }
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_VARYING_INTERP_MODE(0), 8);
   tu_cs_emit_array(cs, interp_modes, 8);

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_VARYING_PS_REPL_MODE(0), 8);
   tu_cs_emit_array(cs, ps_repl_modes, 8);
}

static void
tu6_emit_fs_system_values(struct tu_cs *cs,
                          const struct ir3_shader_variant *fs)
{
   const uint32_t frontfacing_regid =
      ir3_find_sysval_regid(fs, SYSTEM_VALUE_FRONT_FACE);
   const uint32_t sampleid_regid =
      ir3_find_sysval_regid(fs, SYSTEM_VALUE_SAMPLE_ID);
   const uint32_t samplemaskin_regid =
      ir3_find_sysval_regid(fs, SYSTEM_VALUE_SAMPLE_MASK_IN);
   const uint32_t fragcoord_xy_regid =
      ir3_find_sysval_regid(fs, SYSTEM_VALUE_FRAG_COORD);
   const uint32_t fragcoord_zw_regid = (fragcoord_xy_regid != regid(63, 0))
                                          ? (fragcoord_xy_regid + 2)
                                          : fragcoord_xy_regid;
   const uint32_t varyingcoord_regid =
      ir3_find_sysval_regid(fs, SYSTEM_VALUE_VARYING_COORD);

   tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_CONTROL_1_REG, 5);
   tu_cs_emit(cs, 0x7);
   tu_cs_emit(cs, A6XX_HLSQ_CONTROL_2_REG_FACEREGID(frontfacing_regid) |
                     A6XX_HLSQ_CONTROL_2_REG_SAMPLEID(sampleid_regid) |
                     A6XX_HLSQ_CONTROL_2_REG_SAMPLEMASK(samplemaskin_regid) |
                     0xfc000000);
   tu_cs_emit(cs,
              A6XX_HLSQ_CONTROL_3_REG_FRAGCOORDXYREGID(varyingcoord_regid) |
                 0xfcfcfc00);
   tu_cs_emit(cs,
              A6XX_HLSQ_CONTROL_4_REG_XYCOORDREGID(fragcoord_xy_regid) |
                 A6XX_HLSQ_CONTROL_4_REG_ZWCOORDREGID(fragcoord_zw_regid) |
                 0x0000fcfc);
   tu_cs_emit(cs, 0xfc);
}

static void
tu6_emit_fs_inputs(struct tu_cs *cs, const struct ir3_shader_variant *fs)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_UNKNOWN_B980, 1);
   tu_cs_emit(cs, fs->total_in > 0 ? 3 : 1);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_UNKNOWN_A982, 1);
   tu_cs_emit(cs, 0); /* XXX */

   tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_UPDATE_CNTL, 1);
   tu_cs_emit(cs, 0xff); /* XXX */

   uint32_t gras_cntl = 0;
   if (fs->total_in > 0)
      gras_cntl |= A6XX_GRAS_CNTL_VARYING;
   if (fs->frag_coord) {
      gras_cntl |= A6XX_GRAS_CNTL_UNK3 | A6XX_GRAS_CNTL_XCOORD |
                   A6XX_GRAS_CNTL_YCOORD | A6XX_GRAS_CNTL_ZCOORD |
                   A6XX_GRAS_CNTL_WCOORD;
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_CNTL, 1);
   tu_cs_emit(cs, gras_cntl);

   uint32_t rb_render_control = 0;
   if (fs->total_in > 0) {
      rb_render_control =
         A6XX_RB_RENDER_CONTROL0_VARYING | A6XX_RB_RENDER_CONTROL0_UNK10;
   }
   if (fs->frag_coord) {
      rb_render_control |=
         A6XX_RB_RENDER_CONTROL0_UNK3 | A6XX_RB_RENDER_CONTROL0_XCOORD |
         A6XX_RB_RENDER_CONTROL0_YCOORD | A6XX_RB_RENDER_CONTROL0_ZCOORD |
         A6XX_RB_RENDER_CONTROL0_WCOORD;
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_RENDER_CONTROL0, 2);
   tu_cs_emit(cs, rb_render_control);
   tu_cs_emit(cs, (fs->frag_face ? A6XX_RB_RENDER_CONTROL1_FACENESS : 0));
}

static void
tu6_emit_fs_outputs(struct tu_cs *cs,
                    const struct ir3_shader_variant *fs,
                    uint32_t mrt_count)
{
   const uint32_t fragdepth_regid =
      ir3_find_output_regid(fs, FRAG_RESULT_DEPTH);
   uint32_t fragdata_regid[8];
   if (fs->color0_mrt) {
      fragdata_regid[0] = ir3_find_output_regid(fs, FRAG_RESULT_COLOR);
      for (uint32_t i = 1; i < ARRAY_SIZE(fragdata_regid); i++)
         fragdata_regid[i] = fragdata_regid[0];
   } else {
      for (uint32_t i = 0; i < ARRAY_SIZE(fragdata_regid); i++)
         fragdata_regid[i] = ir3_find_output_regid(fs, FRAG_RESULT_DATA0 + i);
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_OUTPUT_CNTL0, 2);
   tu_cs_emit(
      cs, A6XX_SP_FS_OUTPUT_CNTL0_DEPTH_REGID(fragdepth_regid) | 0xfcfc0000);
   tu_cs_emit(cs, A6XX_SP_FS_OUTPUT_CNTL1_MRT(mrt_count));

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_OUTPUT_REG(0), 8);
   for (uint32_t i = 0; i < ARRAY_SIZE(fragdata_regid); i++) {
      // TODO we could have a mix of half and full precision outputs,
      // we really need to figure out half-precision from IR3_REG_HALF
      tu_cs_emit(cs, A6XX_SP_FS_OUTPUT_REG_REGID(fragdata_regid[i]) |
                        (false ? A6XX_SP_FS_OUTPUT_REG_HALF_PRECISION : 0));
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_FS_OUTPUT_CNTL0, 2);
   tu_cs_emit(cs, fs->writes_pos ? A6XX_RB_FS_OUTPUT_CNTL0_FRAG_WRITES_Z : 0);
   tu_cs_emit(cs, A6XX_RB_FS_OUTPUT_CNTL1_MRT(mrt_count));

   uint32_t gras_su_depth_plane_cntl = 0;
   uint32_t rb_depth_plane_cntl = 0;
   if (fs->no_earlyz | fs->writes_pos) {
      gras_su_depth_plane_cntl |= A6XX_GRAS_SU_DEPTH_PLANE_CNTL_FRAG_WRITES_Z;
      rb_depth_plane_cntl |= A6XX_RB_DEPTH_PLANE_CNTL_FRAG_WRITES_Z;
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SU_DEPTH_PLANE_CNTL, 1);
   tu_cs_emit(cs, gras_su_depth_plane_cntl);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_DEPTH_PLANE_CNTL, 1);
   tu_cs_emit(cs, rb_depth_plane_cntl);
}

static void
tu6_emit_shader_object(struct tu_cs *cs,
                       gl_shader_stage stage,
                       const struct ir3_shader_variant *variant,
                       const struct tu_bo *binary_bo,
                       uint32_t binary_offset)
{
   uint16_t reg;
   uint8_t opcode;
   enum a6xx_state_block sb;
   switch (stage) {
   case MESA_SHADER_VERTEX:
      reg = REG_A6XX_SP_VS_OBJ_START_LO;
      opcode = CP_LOAD_STATE6_GEOM;
      sb = SB6_VS_SHADER;
      break;
   case MESA_SHADER_TESS_CTRL:
      reg = REG_A6XX_SP_HS_OBJ_START_LO;
      opcode = CP_LOAD_STATE6_GEOM;
      sb = SB6_HS_SHADER;
      break;
   case MESA_SHADER_TESS_EVAL:
      reg = REG_A6XX_SP_DS_OBJ_START_LO;
      opcode = CP_LOAD_STATE6_GEOM;
      sb = SB6_DS_SHADER;
      break;
   case MESA_SHADER_GEOMETRY:
      reg = REG_A6XX_SP_GS_OBJ_START_LO;
      opcode = CP_LOAD_STATE6_GEOM;
      sb = SB6_GS_SHADER;
      break;
   case MESA_SHADER_FRAGMENT:
      reg = REG_A6XX_SP_FS_OBJ_START_LO;
      opcode = CP_LOAD_STATE6_FRAG;
      sb = SB6_FS_SHADER;
      break;
   case MESA_SHADER_COMPUTE:
      reg = REG_A6XX_SP_CS_OBJ_START_LO;
      opcode = CP_LOAD_STATE6_FRAG;
      sb = SB6_CS_SHADER;
      break;
   default:
      unreachable("invalid gl_shader_stage");
      opcode = CP_LOAD_STATE6_GEOM;
      sb = SB6_VS_SHADER;
      break;
   }

   if (!variant->instrlen) {
      tu_cs_emit_pkt4(cs, reg, 2);
      tu_cs_emit_qw(cs, 0);
      return;
   }

   assert(variant->type == stage);

   const uint64_t binary_iova = binary_bo->iova + binary_offset;
   assert((binary_iova & 0x3) == 0);

   tu_cs_emit_pkt4(cs, reg, 2);
   tu_cs_emit_qw(cs, binary_iova);

   /* always indirect */
   const bool indirect = true;
   if (indirect) {
      tu_cs_emit_pkt7(cs, opcode, 3);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(0) |
                        CP_LOAD_STATE6_0_STATE_TYPE(ST6_SHADER) |
                        CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                        CP_LOAD_STATE6_0_STATE_BLOCK(sb) |
                        CP_LOAD_STATE6_0_NUM_UNIT(variant->instrlen));
      tu_cs_emit_qw(cs, binary_iova);
   } else {
      const void *binary = binary_bo->map + binary_offset;

      tu_cs_emit_pkt7(cs, opcode, 3 + variant->info.sizedwords);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(0) |
                        CP_LOAD_STATE6_0_STATE_TYPE(ST6_SHADER) |
                        CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                        CP_LOAD_STATE6_0_STATE_BLOCK(sb) |
                        CP_LOAD_STATE6_0_NUM_UNIT(variant->instrlen));
      tu_cs_emit_qw(cs, 0);
      tu_cs_emit_array(cs, binary, variant->info.sizedwords);
   }
}

static void
tu6_emit_program(struct tu_cs *cs,
                 const struct tu_pipeline_builder *builder,
                 const struct tu_bo *binary_bo,
                 bool binning_pass)
{
   static const struct ir3_shader_variant dummy_variant = {
      .type = MESA_SHADER_NONE
   };
   assert(builder->shaders[MESA_SHADER_VERTEX]);
   const struct ir3_shader_variant *vs =
      &builder->shaders[MESA_SHADER_VERTEX]->variants[0];
   const struct ir3_shader_variant *hs =
      builder->shaders[MESA_SHADER_TESS_CTRL]
         ? &builder->shaders[MESA_SHADER_TESS_CTRL]->variants[0]
         : &dummy_variant;
   const struct ir3_shader_variant *ds =
      builder->shaders[MESA_SHADER_TESS_EVAL]
         ? &builder->shaders[MESA_SHADER_TESS_EVAL]->variants[0]
         : &dummy_variant;
   const struct ir3_shader_variant *gs =
      builder->shaders[MESA_SHADER_GEOMETRY]
         ? &builder->shaders[MESA_SHADER_GEOMETRY]->variants[0]
         : &dummy_variant;
   const struct ir3_shader_variant *fs =
      builder->shaders[MESA_SHADER_FRAGMENT]
         ? &builder->shaders[MESA_SHADER_FRAGMENT]->variants[0]
         : &dummy_variant;

   if (binning_pass) {
      vs = &builder->shaders[MESA_SHADER_VERTEX]->variants[1];
      fs = &dummy_variant;
   }

   tu6_emit_vs_config(cs, vs);
   tu6_emit_hs_config(cs, hs);
   tu6_emit_ds_config(cs, ds);
   tu6_emit_gs_config(cs, gs);
   tu6_emit_fs_config(cs, fs);

   tu6_emit_vs_system_values(cs, vs);
   tu6_emit_vpc(cs, vs, fs, binning_pass);
   tu6_emit_vpc_varying_modes(cs, fs, binning_pass);
   tu6_emit_fs_system_values(cs, fs);
   tu6_emit_fs_inputs(cs, fs);
   tu6_emit_fs_outputs(cs, fs, builder->color_attachment_count);

   tu6_emit_shader_object(cs, MESA_SHADER_VERTEX, vs, binary_bo,
                          builder->shader_offsets[MESA_SHADER_VERTEX]);

   tu6_emit_shader_object(cs, MESA_SHADER_FRAGMENT, fs, binary_bo,
                          builder->shader_offsets[MESA_SHADER_FRAGMENT]);
}

static void
tu6_emit_vertex_input(struct tu_cs *cs,
                      const struct ir3_shader_variant *vs,
                      const VkPipelineVertexInputStateCreateInfo *vi_info,
                      uint8_t bindings[MAX_VERTEX_ATTRIBS],
                      uint16_t strides[MAX_VERTEX_ATTRIBS],
                      uint16_t offsets[MAX_VERTEX_ATTRIBS],
                      uint32_t *count)
{
   uint32_t vfd_decode_idx = 0;

   /* why do we go beyond inputs_count? */
   assert(vs->inputs_count + 1 <= MAX_VERTEX_ATTRIBS);
   for (uint32_t i = 0; i <= vs->inputs_count; i++) {
      if (vs->inputs[i].sysval || !vs->inputs[i].compmask)
         continue;

      const VkVertexInputAttributeDescription *vi_attr =
         tu_find_vertex_input_attribute(vi_info, vs->inputs[i].slot);
      const VkVertexInputBindingDescription *vi_binding =
         tu_find_vertex_input_binding(vi_info, vi_attr);
      assert(vi_attr && vi_binding);

      const struct tu_native_format *format =
         tu6_get_native_format(vi_attr->format);
      assert(format && format->vtx >= 0);

      uint32_t vfd_decode = A6XX_VFD_DECODE_INSTR_IDX(vfd_decode_idx) |
                            A6XX_VFD_DECODE_INSTR_FORMAT(format->vtx) |
                            A6XX_VFD_DECODE_INSTR_SWAP(format->swap) |
                            A6XX_VFD_DECODE_INSTR_UNK30;
      if (vi_binding->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE)
         vfd_decode |= A6XX_VFD_DECODE_INSTR_INSTANCED;
      if (!vk_format_is_int(vi_attr->format))
         vfd_decode |= A6XX_VFD_DECODE_INSTR_FLOAT;

      const uint32_t vfd_decode_step_rate = 1;

      const uint32_t vfd_dest_cntl =
         A6XX_VFD_DEST_CNTL_INSTR_WRITEMASK(vs->inputs[i].compmask) |
         A6XX_VFD_DEST_CNTL_INSTR_REGID(vs->inputs[i].regid);

      tu_cs_emit_pkt4(cs, REG_A6XX_VFD_DECODE(vfd_decode_idx), 2);
      tu_cs_emit(cs, vfd_decode);
      tu_cs_emit(cs, vfd_decode_step_rate);

      tu_cs_emit_pkt4(cs, REG_A6XX_VFD_DEST_CNTL(vfd_decode_idx), 1);
      tu_cs_emit(cs, vfd_dest_cntl);

      bindings[vfd_decode_idx] = vi_binding->binding;
      strides[vfd_decode_idx] = vi_binding->stride;
      offsets[vfd_decode_idx] = vi_attr->offset;

      vfd_decode_idx++;
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_VFD_CONTROL_0, 1);
   tu_cs_emit(
      cs, A6XX_VFD_CONTROL_0_VTXCNT(vfd_decode_idx) | (vfd_decode_idx << 8));

   *count = vfd_decode_idx;
}

static uint32_t
tu6_guardband_adj(uint32_t v)
{
   if (v > 256)
      return (uint32_t)(511.0 - 65.0 * (log2(v) - 8.0));
   else
      return 511;
}

void
tu6_emit_viewport(struct tu_cs *cs, const VkViewport *viewport)
{
   float offsets[3];
   float scales[3];
   scales[0] = viewport->width / 2.0f;
   scales[1] = viewport->height / 2.0f;
   scales[2] = viewport->maxDepth - viewport->minDepth;
   offsets[0] = viewport->x + scales[0];
   offsets[1] = viewport->y + scales[1];
   offsets[2] = viewport->minDepth;

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
   assert(min.x >= 0 && min.x < max.x);
   assert(min.y >= 0 && min.y < max.y);

   VkExtent2D guardband_adj;
   guardband_adj.width = tu6_guardband_adj(max.x - min.x);
   guardband_adj.height = tu6_guardband_adj(max.y - min.y);

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_CL_VPORT_XOFFSET_0, 6);
   tu_cs_emit(cs, A6XX_GRAS_CL_VPORT_XOFFSET_0(offsets[0]));
   tu_cs_emit(cs, A6XX_GRAS_CL_VPORT_XSCALE_0(scales[0]));
   tu_cs_emit(cs, A6XX_GRAS_CL_VPORT_YOFFSET_0(offsets[1]));
   tu_cs_emit(cs, A6XX_GRAS_CL_VPORT_YSCALE_0(scales[1]));
   tu_cs_emit(cs, A6XX_GRAS_CL_VPORT_ZOFFSET_0(offsets[2]));
   tu_cs_emit(cs, A6XX_GRAS_CL_VPORT_ZSCALE_0(scales[2]));

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL_0, 2);
   tu_cs_emit(cs, A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL_0_X(min.x) |
                     A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL_0_Y(min.y));
   tu_cs_emit(cs, A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL_0_X(max.x - 1) |
                     A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL_0_Y(max.y - 1));

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_CL_GUARDBAND_CLIP_ADJ, 1);
   tu_cs_emit(cs,
              A6XX_GRAS_CL_GUARDBAND_CLIP_ADJ_HORZ(guardband_adj.width) |
                 A6XX_GRAS_CL_GUARDBAND_CLIP_ADJ_VERT(guardband_adj.height));
}

void
tu6_emit_scissor(struct tu_cs *cs, const VkRect2D *scissor)
{
   const VkOffset2D min = scissor->offset;
   const VkOffset2D max = {
      scissor->offset.x + scissor->extent.width,
      scissor->offset.y + scissor->extent.height,
   };

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SC_SCREEN_SCISSOR_TL_0, 2);
   tu_cs_emit(cs, A6XX_GRAS_SC_SCREEN_SCISSOR_TL_0_X(min.x) |
                     A6XX_GRAS_SC_SCREEN_SCISSOR_TL_0_Y(min.y));
   tu_cs_emit(cs, A6XX_GRAS_SC_SCREEN_SCISSOR_TL_0_X(max.x - 1) |
                     A6XX_GRAS_SC_SCREEN_SCISSOR_TL_0_Y(max.y - 1));
}

static void
tu6_emit_gras_unknowns(struct tu_cs *cs)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_UNKNOWN_8000, 1);
   tu_cs_emit(cs, 0x80);
   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_UNKNOWN_8001, 1);
   tu_cs_emit(cs, 0x0);
   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_UNKNOWN_8004, 1);
   tu_cs_emit(cs, 0x0);
}

static void
tu6_emit_point_size(struct tu_cs *cs)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SU_POINT_MINMAX, 2);
   tu_cs_emit(cs, A6XX_GRAS_SU_POINT_MINMAX_MIN(1.0f / 16.0f) |
                     A6XX_GRAS_SU_POINT_MINMAX_MAX(4092.0f));
   tu_cs_emit(cs, A6XX_GRAS_SU_POINT_SIZE(1.0f));
}

static uint32_t
tu6_gras_su_cntl(const VkPipelineRasterizationStateCreateInfo *rast_info,
                 VkSampleCountFlagBits samples)
{
   uint32_t gras_su_cntl = 0;

   if (rast_info->cullMode & VK_CULL_MODE_FRONT_BIT)
      gras_su_cntl |= A6XX_GRAS_SU_CNTL_CULL_FRONT;
   if (rast_info->cullMode & VK_CULL_MODE_BACK_BIT)
      gras_su_cntl |= A6XX_GRAS_SU_CNTL_CULL_BACK;

   if (rast_info->frontFace == VK_FRONT_FACE_CLOCKWISE)
      gras_su_cntl |= A6XX_GRAS_SU_CNTL_FRONT_CW;

   /* don't set A6XX_GRAS_SU_CNTL_LINEHALFWIDTH */

   if (rast_info->depthBiasEnable)
      gras_su_cntl |= A6XX_GRAS_SU_CNTL_POLY_OFFSET;

   if (samples > VK_SAMPLE_COUNT_1_BIT)
      gras_su_cntl |= A6XX_GRAS_SU_CNTL_MSAA_ENABLE;

   return gras_su_cntl;
}

void
tu6_emit_gras_su_cntl(struct tu_cs *cs,
                      uint32_t gras_su_cntl,
                      float line_width)
{
   assert((gras_su_cntl & A6XX_GRAS_SU_CNTL_LINEHALFWIDTH__MASK) == 0);
   gras_su_cntl |= A6XX_GRAS_SU_CNTL_LINEHALFWIDTH(line_width / 2.0f);

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SU_CNTL, 1);
   tu_cs_emit(cs, gras_su_cntl);
}

void
tu6_emit_depth_bias(struct tu_cs *cs,
                    float constant_factor,
                    float clamp,
                    float slope_factor)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SU_POLY_OFFSET_SCALE, 3);
   tu_cs_emit(cs, A6XX_GRAS_SU_POLY_OFFSET_SCALE(slope_factor));
   tu_cs_emit(cs, A6XX_GRAS_SU_POLY_OFFSET_OFFSET(constant_factor));
   tu_cs_emit(cs, A6XX_GRAS_SU_POLY_OFFSET_OFFSET_CLAMP(clamp));
}

static void
tu6_emit_alpha_control_disable(struct tu_cs *cs)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_RB_ALPHA_CONTROL, 1);
   tu_cs_emit(cs, 0);
}

static void
tu6_emit_depth_control(struct tu_cs *cs,
                       const VkPipelineDepthStencilStateCreateInfo *ds_info)
{
   assert(!ds_info->depthBoundsTestEnable);

   uint32_t rb_depth_cntl = 0;
   if (ds_info->depthTestEnable) {
      rb_depth_cntl |=
         A6XX_RB_DEPTH_CNTL_Z_ENABLE |
         A6XX_RB_DEPTH_CNTL_ZFUNC(tu6_compare_func(ds_info->depthCompareOp)) |
         A6XX_RB_DEPTH_CNTL_Z_TEST_ENABLE;

      if (ds_info->depthWriteEnable)
         rb_depth_cntl |= A6XX_RB_DEPTH_CNTL_Z_WRITE_ENABLE;
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_DEPTH_CNTL, 1);
   tu_cs_emit(cs, rb_depth_cntl);
}

static void
tu6_emit_stencil_control(struct tu_cs *cs,
                         const VkPipelineDepthStencilStateCreateInfo *ds_info)
{
   uint32_t rb_stencil_control = 0;
   if (ds_info->stencilTestEnable) {
      const VkStencilOpState *front = &ds_info->front;
      const VkStencilOpState *back = &ds_info->back;
      rb_stencil_control |=
         A6XX_RB_STENCIL_CONTROL_STENCIL_ENABLE |
         A6XX_RB_STENCIL_CONTROL_STENCIL_ENABLE_BF |
         A6XX_RB_STENCIL_CONTROL_STENCIL_READ |
         A6XX_RB_STENCIL_CONTROL_FUNC(tu6_compare_func(front->compareOp)) |
         A6XX_RB_STENCIL_CONTROL_FAIL(tu6_stencil_op(front->failOp)) |
         A6XX_RB_STENCIL_CONTROL_ZPASS(tu6_stencil_op(front->passOp)) |
         A6XX_RB_STENCIL_CONTROL_ZFAIL(tu6_stencil_op(front->depthFailOp)) |
         A6XX_RB_STENCIL_CONTROL_FUNC_BF(tu6_compare_func(back->compareOp)) |
         A6XX_RB_STENCIL_CONTROL_FAIL_BF(tu6_stencil_op(back->failOp)) |
         A6XX_RB_STENCIL_CONTROL_ZPASS_BF(tu6_stencil_op(back->passOp)) |
         A6XX_RB_STENCIL_CONTROL_ZFAIL_BF(tu6_stencil_op(back->depthFailOp));
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_STENCIL_CONTROL, 1);
   tu_cs_emit(cs, rb_stencil_control);
}

void
tu6_emit_stencil_compare_mask(struct tu_cs *cs, uint32_t front, uint32_t back)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_RB_STENCILMASK, 1);
   tu_cs_emit(
      cs, A6XX_RB_STENCILMASK_MASK(front) | A6XX_RB_STENCILMASK_BFMASK(back));
}

void
tu6_emit_stencil_write_mask(struct tu_cs *cs, uint32_t front, uint32_t back)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_RB_STENCILWRMASK, 1);
   tu_cs_emit(cs, A6XX_RB_STENCILWRMASK_WRMASK(front) |
                     A6XX_RB_STENCILWRMASK_BFWRMASK(back));
}

void
tu6_emit_stencil_reference(struct tu_cs *cs, uint32_t front, uint32_t back)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_RB_STENCILREF, 1);
   tu_cs_emit(cs,
              A6XX_RB_STENCILREF_REF(front) | A6XX_RB_STENCILREF_BFREF(back));
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
                   bool is_int,
                   bool has_alpha)
{
   uint32_t rb_mrt_control =
      A6XX_RB_MRT_CONTROL_COMPONENT_ENABLE(att->colorWriteMask);

   /* ignore blending and logic op for integer attachments */
   if (is_int) {
      rb_mrt_control |= A6XX_RB_MRT_CONTROL_ROP_CODE(ROP_COPY);
      return rb_mrt_control;
   }

   rb_mrt_control |= rb_mrt_control_rop;

   if (att->blendEnable) {
      rb_mrt_control |= A6XX_RB_MRT_CONTROL_BLEND;

      if (has_alpha)
         rb_mrt_control |= A6XX_RB_MRT_CONTROL_BLEND2;
   }

   return rb_mrt_control;
}

static void
tu6_emit_rb_mrt_controls(struct tu_cs *cs,
                         const VkPipelineColorBlendStateCreateInfo *blend_info,
                         const VkFormat attachment_formats[MAX_RTS],
                         uint32_t *blend_enable_mask)
{
   *blend_enable_mask = 0;

   bool rop_reads_dst = false;
   uint32_t rb_mrt_control_rop = 0;
   if (blend_info->logicOpEnable) {
      rop_reads_dst = tu_logic_op_reads_dst(blend_info->logicOp);
      rb_mrt_control_rop =
         A6XX_RB_MRT_CONTROL_ROP_ENABLE |
         A6XX_RB_MRT_CONTROL_ROP_CODE(tu6_rop(blend_info->logicOp));
   }

   for (uint32_t i = 0; i < blend_info->attachmentCount; i++) {
      const VkPipelineColorBlendAttachmentState *att =
         &blend_info->pAttachments[i];
      const VkFormat format = attachment_formats[i];

      uint32_t rb_mrt_control = 0;
      uint32_t rb_mrt_blend_control = 0;
      if (format != VK_FORMAT_UNDEFINED) {
         const bool is_int = vk_format_is_int(format);
         const bool has_alpha = vk_format_has_alpha(format);

         rb_mrt_control =
            tu6_rb_mrt_control(att, rb_mrt_control_rop, is_int, has_alpha);
         rb_mrt_blend_control = tu6_rb_mrt_blend_control(att, has_alpha);

         if (att->blendEnable || rop_reads_dst)
            *blend_enable_mask |= 1 << i;
      }

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_CONTROL(i), 2);
      tu_cs_emit(cs, rb_mrt_control);
      tu_cs_emit(cs, rb_mrt_blend_control);
   }

   for (uint32_t i = blend_info->attachmentCount; i < MAX_RTS; i++) {
      tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_CONTROL(i), 2);
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, 0);
   }
}

static void
tu6_emit_blend_control(struct tu_cs *cs,
                       uint32_t blend_enable_mask,
                       const VkPipelineMultisampleStateCreateInfo *msaa_info)
{
   assert(!msaa_info->sampleShadingEnable);
   assert(!msaa_info->alphaToOneEnable);

   uint32_t sp_blend_cntl = A6XX_SP_BLEND_CNTL_UNK8;
   if (blend_enable_mask)
      sp_blend_cntl |= A6XX_SP_BLEND_CNTL_ENABLED;
   if (msaa_info->alphaToCoverageEnable)
      sp_blend_cntl |= A6XX_SP_BLEND_CNTL_ALPHA_TO_COVERAGE;

   const uint32_t sample_mask =
      msaa_info->pSampleMask ? *msaa_info->pSampleMask
                             : ((1 << msaa_info->rasterizationSamples) - 1);

   /* set A6XX_RB_BLEND_CNTL_INDEPENDENT_BLEND only when enabled? */
   uint32_t rb_blend_cntl =
      A6XX_RB_BLEND_CNTL_ENABLE_BLEND(blend_enable_mask) |
      A6XX_RB_BLEND_CNTL_INDEPENDENT_BLEND |
      A6XX_RB_BLEND_CNTL_SAMPLE_MASK(sample_mask);
   if (msaa_info->alphaToCoverageEnable)
      rb_blend_cntl |= A6XX_RB_BLEND_CNTL_ALPHA_TO_COVERAGE;

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_BLEND_CNTL, 1);
   tu_cs_emit(cs, sp_blend_cntl);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLEND_CNTL, 1);
   tu_cs_emit(cs, rb_blend_cntl);
}

void
tu6_emit_blend_constants(struct tu_cs *cs, const float constants[4])
{
   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLEND_RED_F32, 4);
   tu_cs_emit_array(cs, (const uint32_t *) constants, 4);
}

static VkResult
tu_pipeline_builder_create_pipeline(struct tu_pipeline_builder *builder,
                                    struct tu_pipeline **out_pipeline)
{
   struct tu_device *dev = builder->device;

   struct tu_pipeline *pipeline =
      vk_zalloc2(&dev->alloc, builder->alloc, sizeof(*pipeline), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pipeline)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   tu_cs_init(&pipeline->cs, TU_CS_MODE_SUB_STREAM, 2048);

   /* reserve the space now such that tu_cs_begin_sub_stream never fails */
   VkResult result = tu_cs_reserve_space(dev, &pipeline->cs, 2048);
   if (result != VK_SUCCESS) {
      vk_free2(&dev->alloc, builder->alloc, pipeline);
      return result;
   }

   *out_pipeline = pipeline;

   return VK_SUCCESS;
}

static VkResult
tu_pipeline_builder_compile_shaders(struct tu_pipeline_builder *builder)
{
   const VkPipelineShaderStageCreateInfo *stage_infos[MESA_SHADER_STAGES] = {
      NULL
   };
   for (uint32_t i = 0; i < builder->create_info->stageCount; i++) {
      gl_shader_stage stage =
         tu_shader_stage(builder->create_info->pStages[i].stage);
      stage_infos[stage] = &builder->create_info->pStages[i];
   }

   struct tu_shader_compile_options options;
   tu_shader_compile_options_init(&options, builder->create_info);

   /* compile shaders in reverse order */
   struct tu_shader *next_stage_shader = NULL;
   for (gl_shader_stage stage = MESA_SHADER_STAGES - 1;
        stage > MESA_SHADER_NONE; stage--) {
      const VkPipelineShaderStageCreateInfo *stage_info = stage_infos[stage];
      if (!stage_info)
         continue;

      struct tu_shader *shader =
         tu_shader_create(builder->device, stage, stage_info, builder->alloc);
      if (!shader)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      VkResult result =
         tu_shader_compile(builder->device, shader, next_stage_shader,
                           &options, builder->alloc);
      if (result != VK_SUCCESS)
         return result;

      builder->shaders[stage] = shader;
      builder->shader_offsets[stage] = builder->shader_total_size;
      builder->shader_total_size +=
         sizeof(uint32_t) * shader->variants[0].info.sizedwords;

      next_stage_shader = shader;
   }

   if (builder->shaders[MESA_SHADER_VERTEX]->has_binning_pass) {
      const struct tu_shader *vs = builder->shaders[MESA_SHADER_VERTEX];
      builder->binning_vs_offset = builder->shader_total_size;
      builder->shader_total_size +=
         sizeof(uint32_t) * vs->variants[1].info.sizedwords;
   }

   return VK_SUCCESS;
}

static VkResult
tu_pipeline_builder_upload_shaders(struct tu_pipeline_builder *builder,
                                   struct tu_pipeline *pipeline)
{
   struct tu_bo *bo = &pipeline->program.binary_bo;

   VkResult result =
      tu_bo_init_new(builder->device, bo, builder->shader_total_size);
   if (result != VK_SUCCESS)
      return result;

   result = tu_bo_map(builder->device, bo);
   if (result != VK_SUCCESS)
      return result;

   for (uint32_t i = 0; i < MESA_SHADER_STAGES; i++) {
      const struct tu_shader *shader = builder->shaders[i];
      if (!shader)
         continue;

      memcpy(bo->map + builder->shader_offsets[i], shader->binary,
             sizeof(uint32_t) * shader->variants[0].info.sizedwords);
   }

   if (builder->shaders[MESA_SHADER_VERTEX]->has_binning_pass) {
      const struct tu_shader *vs = builder->shaders[MESA_SHADER_VERTEX];
      memcpy(bo->map + builder->binning_vs_offset, vs->binning_binary,
             sizeof(uint32_t) * vs->variants[1].info.sizedwords);
   }

   return VK_SUCCESS;
}

static void
tu_pipeline_builder_parse_dynamic(struct tu_pipeline_builder *builder,
                                  struct tu_pipeline *pipeline)
{
   const VkPipelineDynamicStateCreateInfo *dynamic_info =
      builder->create_info->pDynamicState;

   if (!dynamic_info)
      return;

   for (uint32_t i = 0; i < dynamic_info->dynamicStateCount; i++) {
      pipeline->dynamic_state.mask |=
         tu_dynamic_state_bit(dynamic_info->pDynamicStates[i]);
   }
}

static void
tu_pipeline_builder_parse_shader_stages(struct tu_pipeline_builder *builder,
                                        struct tu_pipeline *pipeline)
{
   struct tu_cs prog_cs;
   tu_cs_begin_sub_stream(builder->device, &pipeline->cs, 512, &prog_cs);
   tu6_emit_program(&prog_cs, builder, &pipeline->program.binary_bo, false);
   pipeline->program.state_ib = tu_cs_end_sub_stream(&pipeline->cs, &prog_cs);

   tu_cs_begin_sub_stream(builder->device, &pipeline->cs, 512, &prog_cs);
   tu6_emit_program(&prog_cs, builder, &pipeline->program.binary_bo, true);
   pipeline->program.binning_state_ib =
      tu_cs_end_sub_stream(&pipeline->cs, &prog_cs);
}

static void
tu_pipeline_builder_parse_vertex_input(struct tu_pipeline_builder *builder,
                                       struct tu_pipeline *pipeline)
{
   const VkPipelineVertexInputStateCreateInfo *vi_info =
      builder->create_info->pVertexInputState;
   const struct tu_shader *vs = builder->shaders[MESA_SHADER_VERTEX];

   struct tu_cs vi_cs;
   tu_cs_begin_sub_stream(builder->device, &pipeline->cs,
                          MAX_VERTEX_ATTRIBS * 5 + 2, &vi_cs);
   tu6_emit_vertex_input(&vi_cs, &vs->variants[0], vi_info,
                         pipeline->vi.bindings, pipeline->vi.strides,
                         pipeline->vi.offsets, &pipeline->vi.count);
   pipeline->vi.state_ib = tu_cs_end_sub_stream(&pipeline->cs, &vi_cs);

   if (vs->has_binning_pass) {
      tu_cs_begin_sub_stream(builder->device, &pipeline->cs,
                             MAX_VERTEX_ATTRIBS * 5 + 2, &vi_cs);
      tu6_emit_vertex_input(
         &vi_cs, &vs->variants[1], vi_info, pipeline->vi.binning_bindings,
         pipeline->vi.binning_strides, pipeline->vi.binning_offsets,
         &pipeline->vi.binning_count);
      pipeline->vi.binning_state_ib =
         tu_cs_end_sub_stream(&pipeline->cs, &vi_cs);
   }
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

   struct tu_cs vp_cs;
   tu_cs_begin_sub_stream(builder->device, &pipeline->cs, 15, &vp_cs);

   if (!(pipeline->dynamic_state.mask & TU_DYNAMIC_VIEWPORT)) {
      assert(vp_info->viewportCount == 1);
      tu6_emit_viewport(&vp_cs, vp_info->pViewports);
   }

   if (!(pipeline->dynamic_state.mask & TU_DYNAMIC_SCISSOR)) {
      assert(vp_info->scissorCount == 1);
      tu6_emit_scissor(&vp_cs, vp_info->pScissors);
   }

   pipeline->vp.state_ib = tu_cs_end_sub_stream(&pipeline->cs, &vp_cs);
}

static void
tu_pipeline_builder_parse_rasterization(struct tu_pipeline_builder *builder,
                                        struct tu_pipeline *pipeline)
{
   const VkPipelineRasterizationStateCreateInfo *rast_info =
      builder->create_info->pRasterizationState;

   assert(!rast_info->depthClampEnable);
   assert(rast_info->polygonMode == VK_POLYGON_MODE_FILL);

   struct tu_cs rast_cs;
   tu_cs_begin_sub_stream(builder->device, &pipeline->cs, 20, &rast_cs);

   /* move to hw ctx init? */
   tu6_emit_gras_unknowns(&rast_cs);
   tu6_emit_point_size(&rast_cs);

   const uint32_t gras_su_cntl =
      tu6_gras_su_cntl(rast_info, builder->samples);

   if (!(pipeline->dynamic_state.mask & TU_DYNAMIC_LINE_WIDTH))
      tu6_emit_gras_su_cntl(&rast_cs, gras_su_cntl, rast_info->lineWidth);

   if (!(pipeline->dynamic_state.mask & TU_DYNAMIC_DEPTH_BIAS)) {
      tu6_emit_depth_bias(&rast_cs, rast_info->depthBiasConstantFactor,
                          rast_info->depthBiasClamp,
                          rast_info->depthBiasSlopeFactor);
   }

   pipeline->rast.state_ib = tu_cs_end_sub_stream(&pipeline->cs, &rast_cs);

   pipeline->rast.gras_su_cntl = gras_su_cntl;
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
    *
    * We disable both depth and stenil tests in those cases.
    */
   static const VkPipelineDepthStencilStateCreateInfo dummy_ds_info;
   const VkPipelineDepthStencilStateCreateInfo *ds_info =
      builder->use_depth_stencil_attachment
         ? builder->create_info->pDepthStencilState
         : &dummy_ds_info;

   struct tu_cs ds_cs;
   tu_cs_begin_sub_stream(builder->device, &pipeline->cs, 12, &ds_cs);

   /* move to hw ctx init? */
   tu6_emit_alpha_control_disable(&ds_cs);

   tu6_emit_depth_control(&ds_cs, ds_info);
   tu6_emit_stencil_control(&ds_cs, ds_info);

   if (!(pipeline->dynamic_state.mask & TU_DYNAMIC_STENCIL_COMPARE_MASK)) {
      tu6_emit_stencil_compare_mask(&ds_cs, ds_info->front.compareMask,
                                    ds_info->back.compareMask);
   }
   if (!(pipeline->dynamic_state.mask & TU_DYNAMIC_STENCIL_WRITE_MASK)) {
      tu6_emit_stencil_write_mask(&ds_cs, ds_info->front.writeMask,
                                  ds_info->back.writeMask);
   }
   if (!(pipeline->dynamic_state.mask & TU_DYNAMIC_STENCIL_REFERENCE)) {
      tu6_emit_stencil_reference(&ds_cs, ds_info->front.reference,
                                 ds_info->back.reference);
   }

   pipeline->ds.state_ib = tu_cs_end_sub_stream(&pipeline->cs, &ds_cs);
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

   struct tu_cs blend_cs;
   tu_cs_begin_sub_stream(builder->device, &pipeline->cs, MAX_RTS * 3 + 9,
                          &blend_cs);

   uint32_t blend_enable_mask;
   tu6_emit_rb_mrt_controls(&blend_cs, blend_info,
                            builder->color_attachment_formats,
                            &blend_enable_mask);

   if (!(pipeline->dynamic_state.mask & TU_DYNAMIC_BLEND_CONSTANTS))
      tu6_emit_blend_constants(&blend_cs, blend_info->blendConstants);

   tu6_emit_blend_control(&blend_cs, blend_enable_mask, msaa_info);

   pipeline->blend.state_ib = tu_cs_end_sub_stream(&pipeline->cs, &blend_cs);
}

static void
tu_pipeline_finish(struct tu_pipeline *pipeline,
                   struct tu_device *dev,
                   const VkAllocationCallbacks *alloc)
{
   tu_cs_finish(dev, &pipeline->cs);

   if (pipeline->program.binary_bo.gem_handle)
      tu_bo_finish(dev, &pipeline->program.binary_bo);
}

static VkResult
tu_pipeline_builder_build(struct tu_pipeline_builder *builder,
                          struct tu_pipeline **pipeline)
{
   VkResult result = tu_pipeline_builder_create_pipeline(builder, pipeline);
   if (result != VK_SUCCESS)
      return result;

   /* compile and upload shaders */
   result = tu_pipeline_builder_compile_shaders(builder);
   if (result == VK_SUCCESS)
      result = tu_pipeline_builder_upload_shaders(builder, *pipeline);
   if (result != VK_SUCCESS) {
      tu_pipeline_finish(*pipeline, builder->device, builder->alloc);
      vk_free2(&builder->device->alloc, builder->alloc, *pipeline);
      *pipeline = VK_NULL_HANDLE;

      return result;
   }

   tu_pipeline_builder_parse_dynamic(builder, *pipeline);
   tu_pipeline_builder_parse_shader_stages(builder, *pipeline);
   tu_pipeline_builder_parse_vertex_input(builder, *pipeline);
   tu_pipeline_builder_parse_input_assembly(builder, *pipeline);
   tu_pipeline_builder_parse_viewport(builder, *pipeline);
   tu_pipeline_builder_parse_rasterization(builder, *pipeline);
   tu_pipeline_builder_parse_depth_stencil(builder, *pipeline);
   tu_pipeline_builder_parse_multisample_and_color_blend(builder, *pipeline);

   /* we should have reserved enough space upfront such that the CS never
    * grows
    */
   assert((*pipeline)->cs.bo_count == 1);

   return VK_SUCCESS;
}

static void
tu_pipeline_builder_finish(struct tu_pipeline_builder *builder)
{
   for (uint32_t i = 0; i < MESA_SHADER_STAGES; i++) {
      if (!builder->shaders[i])
         continue;
      tu_shader_destroy(builder->device, builder->shaders[i], builder->alloc);
   }
}

static void
tu_pipeline_builder_init_graphics(
   struct tu_pipeline_builder *builder,
   struct tu_device *dev,
   struct tu_pipeline_cache *cache,
   const VkGraphicsPipelineCreateInfo *create_info,
   const VkAllocationCallbacks *alloc)
{
   *builder = (struct tu_pipeline_builder) {
      .device = dev,
      .cache = cache,
      .create_info = create_info,
      .alloc = alloc,
   };

   builder->rasterizer_discard =
      create_info->pRasterizationState->rasterizerDiscardEnable;

   if (builder->rasterizer_discard) {
      builder->samples = VK_SAMPLE_COUNT_1_BIT;
   } else {
      builder->samples = create_info->pMultisampleState->rasterizationSamples;

      const struct tu_render_pass *pass =
         tu_render_pass_from_handle(create_info->renderPass);
      const struct tu_subpass *subpass =
         &pass->subpasses[create_info->subpass];

      builder->use_depth_stencil_attachment =
         subpass->depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED;

      assert(subpass->color_count ==
             create_info->pColorBlendState->attachmentCount);
      builder->color_attachment_count = subpass->color_count;
      for (uint32_t i = 0; i < subpass->color_count; i++) {
         const uint32_t a = subpass->color_attachments[i].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;

         builder->color_attachment_formats[i] = pass->attachments[a].format;
         builder->use_color_attachments = true;
      }
   }
}

VkResult
tu_CreateGraphicsPipelines(VkDevice device,
                           VkPipelineCache pipelineCache,
                           uint32_t count,
                           const VkGraphicsPipelineCreateInfo *pCreateInfos,
                           const VkAllocationCallbacks *pAllocator,
                           VkPipeline *pPipelines)
{
   TU_FROM_HANDLE(tu_device, dev, device);
   TU_FROM_HANDLE(tu_pipeline_cache, cache, pipelineCache);

   for (uint32_t i = 0; i < count; i++) {
      struct tu_pipeline_builder builder;
      tu_pipeline_builder_init_graphics(&builder, dev, cache,
                                        &pCreateInfos[i], pAllocator);

      struct tu_pipeline *pipeline;
      VkResult result = tu_pipeline_builder_build(&builder, &pipeline);
      tu_pipeline_builder_finish(&builder);

      if (result != VK_SUCCESS) {
         for (uint32_t j = 0; j < i; j++) {
            tu_DestroyPipeline(device, pPipelines[j], pAllocator);
            pPipelines[j] = VK_NULL_HANDLE;
         }

         return result;
      }

      pPipelines[i] = tu_pipeline_to_handle(pipeline);
   }

   return VK_SUCCESS;
}

static VkResult
tu_compute_pipeline_create(VkDevice _device,
                           VkPipelineCache _cache,
                           const VkComputePipelineCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkPipeline *pPipeline)
{
   return VK_SUCCESS;
}

VkResult
tu_CreateComputePipelines(VkDevice _device,
                          VkPipelineCache pipelineCache,
                          uint32_t count,
                          const VkComputePipelineCreateInfo *pCreateInfos,
                          const VkAllocationCallbacks *pAllocator,
                          VkPipeline *pPipelines)
{
   VkResult result = VK_SUCCESS;

   unsigned i = 0;
   for (; i < count; i++) {
      VkResult r;
      r = tu_compute_pipeline_create(_device, pipelineCache, &pCreateInfos[i],
                                     pAllocator, &pPipelines[i]);
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;
      }
   }

   return result;
}

void
tu_DestroyPipeline(VkDevice _device,
                   VkPipeline _pipeline,
                   const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, dev, _device);
   TU_FROM_HANDLE(tu_pipeline, pipeline, _pipeline);

   if (!_pipeline)
      return;

   tu_pipeline_finish(pipeline, dev, pAllocator);
   vk_free2(&dev->alloc, pAllocator, pipeline);
}
