/*
 * Copyright 2019-2020 Valve Corporation
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Jonathan Marek <jonathan@marek.ca>
 */

#include "tu_private.h"

#include "tu_cs.h"
#include "vk_format.h"

#include "util/format_r11g11b10f.h"
#include "util/format_rgb9e5.h"
#include "util/format_srgb.h"
#include "util/u_half.h"

/* helper functions previously in tu_formats.c */

static uint32_t
tu_pack_mask(int bits)
{
   assert(bits <= 32);
   return (1ull << bits) - 1;
}

static uint32_t
tu_pack_float32_for_unorm(float val, int bits)
{
   const uint32_t max = tu_pack_mask(bits);
   if (val < 0.0f)
      return 0;
   else if (val > 1.0f)
      return max;
   else
      return _mesa_lroundevenf(val * (float) max);
}

static uint32_t
tu_pack_float32_for_snorm(float val, int bits)
{
   const int32_t max = tu_pack_mask(bits - 1);
   int32_t tmp;
   if (val < -1.0f)
      tmp = -max;
   else if (val > 1.0f)
      tmp = max;
   else
      tmp = _mesa_lroundevenf(val * (float) max);

   return tmp & tu_pack_mask(bits);
}

static uint32_t
tu_pack_float32_for_uscaled(float val, int bits)
{
   const uint32_t max = tu_pack_mask(bits);
   if (val < 0.0f)
      return 0;
   else if (val > (float) max)
      return max;
   else
      return (uint32_t) val;
}

static uint32_t
tu_pack_float32_for_sscaled(float val, int bits)
{
   const int32_t max = tu_pack_mask(bits - 1);
   const int32_t min = -max - 1;
   int32_t tmp;
   if (val < (float) min)
      tmp = min;
   else if (val > (float) max)
      tmp = max;
   else
      tmp = (int32_t) val;

   return tmp & tu_pack_mask(bits);
}

static uint32_t
tu_pack_uint32_for_uint(uint32_t val, int bits)
{
   return val & tu_pack_mask(bits);
}

static uint32_t
tu_pack_int32_for_sint(int32_t val, int bits)
{
   return val & tu_pack_mask(bits);
}

static uint32_t
tu_pack_float32_for_sfloat(float val, int bits)
{
   assert(bits == 16 || bits == 32);
   return bits == 16 ? util_float_to_half(val) : fui(val);
}

union tu_clear_component_value {
   float float32;
   int32_t int32;
   uint32_t uint32;
};

static uint32_t
tu_pack_clear_component_value(union tu_clear_component_value val,
                              const struct util_format_channel_description *ch)
{
   uint32_t packed;

   switch (ch->type) {
   case UTIL_FORMAT_TYPE_UNSIGNED:
      /* normalized, scaled, or pure integer */
      if (ch->normalized)
         packed = tu_pack_float32_for_unorm(val.float32, ch->size);
      else if (ch->pure_integer)
         packed = tu_pack_uint32_for_uint(val.uint32, ch->size);
      else
         packed = tu_pack_float32_for_uscaled(val.float32, ch->size);
      break;
   case UTIL_FORMAT_TYPE_SIGNED:
      /* normalized, scaled, or pure integer */
      if (ch->normalized)
         packed = tu_pack_float32_for_snorm(val.float32, ch->size);
      else if (ch->pure_integer)
         packed = tu_pack_int32_for_sint(val.int32, ch->size);
      else
         packed = tu_pack_float32_for_sscaled(val.float32, ch->size);
      break;
   case UTIL_FORMAT_TYPE_FLOAT:
      packed = tu_pack_float32_for_sfloat(val.float32, ch->size);
      break;
   default:
      unreachable("unexpected channel type");
      packed = 0;
      break;
   }

   assert((packed & tu_pack_mask(ch->size)) == packed);
   return packed;
}

static const struct util_format_channel_description *
tu_get_format_channel_description(const struct util_format_description *desc,
                                  int comp)
{
   switch (desc->swizzle[comp]) {
   case PIPE_SWIZZLE_X:
      return &desc->channel[0];
   case PIPE_SWIZZLE_Y:
      return &desc->channel[1];
   case PIPE_SWIZZLE_Z:
      return &desc->channel[2];
   case PIPE_SWIZZLE_W:
      return &desc->channel[3];
   default:
      return NULL;
   }
}

static union tu_clear_component_value
tu_get_clear_component_value(const VkClearValue *val, int comp,
                             enum util_format_colorspace colorspace)
{
   assert(comp < 4);

   union tu_clear_component_value tmp;
   switch (colorspace) {
   case UTIL_FORMAT_COLORSPACE_ZS:
      assert(comp < 2);
      if (comp == 0)
         tmp.float32 = val->depthStencil.depth;
      else
         tmp.uint32 = val->depthStencil.stencil;
      break;
   case UTIL_FORMAT_COLORSPACE_SRGB:
      if (comp < 3) {
         tmp.float32 = util_format_linear_to_srgb_float(val->color.float32[comp]);
         break;
      }
   default:
      assert(comp < 4);
      tmp.uint32 = val->color.uint32[comp];
      break;
   }

   return tmp;
}

/* r2d_ = BLIT_OP_SCALE operations */

static enum a6xx_2d_ifmt
format_to_ifmt(enum a6xx_format fmt)
{
   switch (fmt) {
   case FMT6_A8_UNORM:
   case FMT6_8_UNORM:
   case FMT6_8_SNORM:
   case FMT6_8_8_UNORM:
   case FMT6_8_8_SNORM:
   case FMT6_8_8_8_8_UNORM:
   case FMT6_8_8_8_X8_UNORM:
   case FMT6_8_8_8_8_SNORM:
   case FMT6_4_4_4_4_UNORM:
   case FMT6_5_5_5_1_UNORM:
   case FMT6_5_6_5_UNORM:
   case FMT6_Z24_UNORM_S8_UINT:
   case FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8:
      return R2D_UNORM8;

   case FMT6_32_UINT:
   case FMT6_32_SINT:
   case FMT6_32_32_UINT:
   case FMT6_32_32_SINT:
   case FMT6_32_32_32_32_UINT:
   case FMT6_32_32_32_32_SINT:
      return R2D_INT32;

   case FMT6_16_UINT:
   case FMT6_16_SINT:
   case FMT6_16_16_UINT:
   case FMT6_16_16_SINT:
   case FMT6_16_16_16_16_UINT:
   case FMT6_16_16_16_16_SINT:
   case FMT6_10_10_10_2_UINT:
      return R2D_INT16;

   case FMT6_8_UINT:
   case FMT6_8_SINT:
   case FMT6_8_8_UINT:
   case FMT6_8_8_SINT:
   case FMT6_8_8_8_8_UINT:
   case FMT6_8_8_8_8_SINT:
      return R2D_INT8;

   case FMT6_16_UNORM:
   case FMT6_16_SNORM:
   case FMT6_16_16_UNORM:
   case FMT6_16_16_SNORM:
   case FMT6_16_16_16_16_UNORM:
   case FMT6_16_16_16_16_SNORM:
   case FMT6_32_FLOAT:
   case FMT6_32_32_FLOAT:
   case FMT6_32_32_32_32_FLOAT:
      return R2D_FLOAT32;

   case FMT6_16_FLOAT:
   case FMT6_16_16_FLOAT:
   case FMT6_16_16_16_16_FLOAT:
   case FMT6_11_11_10_FLOAT:
   case FMT6_10_10_10_2_UNORM:
   case FMT6_10_10_10_2_UNORM_DEST:
      return R2D_FLOAT16;

   default:
      unreachable("bad format");
      return 0;
   }
}

static void
r2d_coords(struct tu_cs *cs,
           const VkOffset2D *dst,
           const VkOffset2D *src,
           const VkExtent2D *extent)
{
   tu_cs_emit_regs(cs,
      A6XX_GRAS_2D_DST_TL(.x = dst->x,                     .y = dst->y),
      A6XX_GRAS_2D_DST_BR(.x = dst->x + extent->width - 1, .y = dst->y + extent->height - 1));

   if (!src)
      return;

   tu_cs_emit_regs(cs,
                   A6XX_GRAS_2D_SRC_TL_X(.x = src->x),
                   A6XX_GRAS_2D_SRC_BR_X(.x = src->x + extent->width - 1),
                   A6XX_GRAS_2D_SRC_TL_Y(.y = src->y),
                   A6XX_GRAS_2D_SRC_BR_Y(.y = src->y + extent->height - 1));
}

static void
r2d_clear_value(struct tu_cs *cs, VkFormat format, const VkClearValue *val)
{
   uint32_t clear_value[4] = {};

   switch (format) {
   case VK_FORMAT_X8_D24_UNORM_PACK32:
   case VK_FORMAT_D24_UNORM_S8_UINT:
      /* cleared as r8g8b8a8_unorm using special format */
      clear_value[0] = tu_pack_float32_for_unorm(val->depthStencil.depth, 24);
      clear_value[1] = clear_value[0] >> 8;
      clear_value[2] = clear_value[0] >> 16;
      clear_value[3] = val->depthStencil.stencil;
      break;
   case VK_FORMAT_D16_UNORM:
   case VK_FORMAT_D32_SFLOAT:
      /* R2D_FLOAT32 */
      clear_value[0] = fui(val->depthStencil.depth);
      break;
   case VK_FORMAT_S8_UINT:
      clear_value[0] = val->depthStencil.stencil;
      break;
   case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
      /* cleared as UINT32 */
      clear_value[0] = float3_to_rgb9e5(val->color.float32);
      break;
   default:
      assert(!vk_format_is_depth_or_stencil(format));
      const struct util_format_description *desc = vk_format_description(format);
      enum a6xx_2d_ifmt ifmt = format_to_ifmt(tu6_base_format(format));

      assert(desc && (desc->layout == UTIL_FORMAT_LAYOUT_PLAIN ||
                      format == VK_FORMAT_B10G11R11_UFLOAT_PACK32));

      for (unsigned i = 0; i < desc->nr_channels; i++) {
         const struct util_format_channel_description *ch = &desc->channel[i];
         if (ifmt == R2D_UNORM8) {
            float linear = val->color.float32[i];
            if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB && i < 3)
               linear = util_format_linear_to_srgb_float(val->color.float32[i]);

            if (ch->type == UTIL_FORMAT_TYPE_SIGNED)
               clear_value[i] = tu_pack_float32_for_snorm(linear, 8);
            else
               clear_value[i] = tu_pack_float32_for_unorm(linear, 8);
         } else if (ifmt == R2D_FLOAT16) {
            clear_value[i] = util_float_to_half(val->color.float32[i]);
         } else {
            assert(ifmt == R2D_FLOAT32 || ifmt == R2D_INT32 ||
                   ifmt == R2D_INT16 || ifmt == R2D_INT8);
            clear_value[i] = val->color.uint32[i];
         }
      }
      break;
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_2D_SRC_SOLID_C0, 4);
   tu_cs_emit_array(cs, clear_value, 4);
}

static void
r2d_src(struct tu_cmd_buffer *cmd,
        struct tu_cs *cs,
        const struct tu_image_view *iview,
        uint32_t layer,
        bool linear_filter)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_SP_PS_2D_SRC_INFO, 5);
   tu_cs_emit(cs, iview->SP_PS_2D_SRC_INFO |
                  COND(linear_filter, A6XX_SP_PS_2D_SRC_INFO_FILTER));
   tu_cs_emit(cs, iview->SP_PS_2D_SRC_SIZE);
   tu_cs_image_ref_2d(cs, iview, layer, true);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_PS_2D_SRC_FLAGS_LO, 3);
   tu_cs_image_flag_ref(cs, iview, layer);
}

static void
r2d_src_buffer(struct tu_cmd_buffer *cmd,
               struct tu_cs *cs,
               VkFormat vk_format,
               uint64_t va, uint32_t pitch,
               uint32_t width, uint32_t height)
{
   struct tu_native_format format = tu6_format_texture(vk_format, TILE6_LINEAR);

   tu_cs_emit_regs(cs,
                   A6XX_SP_PS_2D_SRC_INFO(
                      .color_format = format.fmt,
                      .color_swap = format.swap,
                      .srgb = vk_format_is_srgb(vk_format),
                      .unk20 = 1,
                      .unk22 = 1),
                   A6XX_SP_PS_2D_SRC_SIZE(.width = width, .height = height),
                   A6XX_SP_PS_2D_SRC_LO((uint32_t) va),
                   A6XX_SP_PS_2D_SRC_HI(va >> 32),
                   A6XX_SP_PS_2D_SRC_PITCH(.pitch = pitch));
}

static void
r2d_dst(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer)
{
   assert(iview->image->samples == 1);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_2D_DST_INFO, 4);
   tu_cs_emit(cs, iview->RB_2D_DST_INFO);
   tu_cs_image_ref_2d(cs, iview, layer, false);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_2D_DST_FLAGS_LO, 3);
   tu_cs_image_flag_ref(cs, iview, layer);
}

static void
r2d_dst_buffer(struct tu_cs *cs, VkFormat vk_format, uint64_t va, uint32_t pitch)
{
   struct tu_native_format format = tu6_format_color(vk_format, TILE6_LINEAR);

   tu_cs_emit_regs(cs,
                   A6XX_RB_2D_DST_INFO(
                      .color_format = format.fmt,
                      .color_swap = format.swap,
                      .srgb = vk_format_is_srgb(vk_format)),
                   A6XX_RB_2D_DST_LO((uint32_t) va),
                   A6XX_RB_2D_DST_HI(va >> 32),
                   A6XX_RB_2D_DST_SIZE(.pitch = pitch));
}

static void
r2d_setup_common(struct tu_cmd_buffer *cmd,
                 struct tu_cs *cs,
                 VkFormat vk_format,
                 enum a6xx_rotation rotation,
                 bool clear,
                 uint8_t mask,
                 bool scissor)
{
   enum a6xx_format format = tu6_base_format(vk_format);
   enum a6xx_2d_ifmt ifmt = format_to_ifmt(format);
   uint32_t unknown_8c01 = 0;

   if (format == FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8) {
      /* preserve depth channels */
      if (mask == 0x8)
         unknown_8c01 = 0x00084001;
      /* preserve stencil channel */
      if (mask == 0x7)
         unknown_8c01 = 0x08000041;
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_UNKNOWN_8C01, 1);
   tu_cs_emit(cs, unknown_8c01);

   uint32_t blit_cntl = A6XX_RB_2D_BLIT_CNTL(
         .scissor = scissor,
         .rotate = rotation,
         .solid_color = clear,
         .d24s8 = format == FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8 && !clear,
         .color_format = format,
         .mask = 0xf,
         .ifmt = vk_format_is_srgb(vk_format) ? R2D_UNORM8_SRGB : ifmt,
      ).value;

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_2D_BLIT_CNTL, 1);
   tu_cs_emit(cs, blit_cntl);

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_2D_BLIT_CNTL, 1);
   tu_cs_emit(cs, blit_cntl);

   if (format == FMT6_10_10_10_2_UNORM_DEST)
      format = FMT6_16_16_16_16_FLOAT;

   tu_cs_emit_regs(cs, A6XX_SP_2D_SRC_FORMAT(
         .sint = vk_format_is_sint(vk_format),
         .uint = vk_format_is_uint(vk_format),
         .color_format = format,
         .srgb = vk_format_is_srgb(vk_format),
         .mask = 0xf));
}

static void
r2d_setup(struct tu_cmd_buffer *cmd,
          struct tu_cs *cs,
          VkFormat vk_format,
          enum a6xx_rotation rotation,
          bool clear,
          uint8_t mask)
{
   const struct tu_physical_device *phys_dev = cmd->device->physical_device;

   /* TODO: flushing with barriers instead of blindly always flushing */
   tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS, true);
   tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_DEPTH_TS, true);
   tu6_emit_event_write(cmd, cs, PC_CCU_INVALIDATE_COLOR, false);
   tu6_emit_event_write(cmd, cs, PC_CCU_INVALIDATE_DEPTH, false);
   tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE, false);

   tu_cs_emit_wfi(cs);
   tu_cs_emit_regs(cs,
                  A6XX_RB_CCU_CNTL(.offset = phys_dev->ccu_offset_bypass));

   r2d_setup_common(cmd, cs, vk_format, rotation, clear, mask, false);
}

static void
r2d_run(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   tu_cs_emit_pkt7(cs, CP_BLIT, 1);
   tu_cs_emit(cs, CP_BLIT_0_OP(BLIT_OP_SCALE));

   /* TODO: flushing with barriers instead of blindly always flushing */
   tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS, true);
   tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_DEPTH_TS, true);
   tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE, false);
}

/* r3d_ = shader path operations */

static void
r3d_pipeline(struct tu_cmd_buffer *cmd, struct tu_cs *cs, bool blit, uint32_t num_rts)
{
   static const instr_t vs_code[] = {
      /* r0.xyz = r0.w ? c1.xyz : c0.xyz
       * r1.xy = r0.w ? c1.zw : c0.zw
       * r0.w = 1.0f
       */
      { .cat3 = {
         .opc_cat = 3, .opc = OPC_SEL_B32 & 63, .repeat = 2, .dst = 0,
         .c1 = {.src1_c = 1, .src1 = 4}, .src1_r = 1,
         .src2 = 3,
         .c2 = {.src3_c = 1, .dummy = 1, .src3 = 0},
      } },
      { .cat3 = {
         .opc_cat = 3, .opc = OPC_SEL_B32 & 63, .repeat = 1, .dst = 4,
         .c1 = {.src1_c = 1, .src1 = 6}, .src1_r = 1,
         .src2 = 3,
         .c2 = {.src3_c = 1, .dummy = 1, .src3 = 2},
      } },
      { .cat1 = { .opc_cat = 1, .src_type = TYPE_F32, .dst_type = TYPE_F32, .dst = 3,
                  .src_im = 1, .fim_val = 1.0f } },
      { .cat0 = { .opc = OPC_END } },
   };
#define FS_OFFSET (16 * sizeof(instr_t))
   STATIC_ASSERT(sizeof(vs_code) <= FS_OFFSET);

   /* vs inputs: only vtx id in r0.w */
   tu_cs_emit_pkt4(cs, REG_A6XX_VFD_CONTROL_0, 7);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0xfcfcfc00 | A6XX_VFD_CONTROL_1_REGID4VTX(3));
   tu_cs_emit(cs, 0x0000fcfc);
   tu_cs_emit(cs, 0xfcfcfcfc);
   tu_cs_emit(cs, 0x000000fc);
   tu_cs_emit(cs, 0x0000fcfc);
   tu_cs_emit(cs, 0x00000000);

   /* vs outputs: position in r0.xyzw, blit coords in r1.xy */
   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_VAR_DISABLE(0), 4);
   tu_cs_emit(cs, blit ? 0xffffffcf : 0xffffffff);
   tu_cs_emit(cs, 0xffffffff);
   tu_cs_emit(cs, 0xffffffff);
   tu_cs_emit(cs, 0xffffffff);

   tu_cs_emit_regs(cs, A6XX_SP_VS_OUT_REG(0,
         .a_regid = 0, .a_compmask = 0xf,
         .b_regid = 4, .b_compmask = 0x3));
   tu_cs_emit_regs(cs, A6XX_SP_VS_VPC_DST_REG(0, .outloc0 = 0, .outloc1 = 4));

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_CNTL_0, 1);
   tu_cs_emit(cs, 0xff00ff00 |
                  COND(blit, A6XX_VPC_CNTL_0_VARYING) |
                  A6XX_VPC_CNTL_0_NUMNONPOSVAR(blit ? 8 : 0));

   tu_cs_emit_regs(cs, A6XX_VPC_PACK(
         .positionloc = 0,
         .psizeloc = 0xff,
         .stride_in_vpc = blit ? 6 : 4));
   tu_cs_emit_regs(cs, A6XX_SP_PRIMITIVE_CNTL(.vsout = blit ? 2 : 1));
   tu_cs_emit_regs(cs,
                   A6XX_PC_PRIMITIVE_CNTL_0(),
                   A6XX_PC_PRIMITIVE_CNTL_1(.stride_in_vpc = blit ? 6 : 4));


   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_VARYING_INTERP_MODE(0), 8);
   tu_cs_emit(cs, blit ? 0xe000 : 0); // I think this can just be 0
   for (uint32_t i = 1; i < 8; i++)
      tu_cs_emit(cs, 0);

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_VARYING_PS_REPL_MODE(0), 8);
   for (uint32_t i = 0; i < 8; i++)
      tu_cs_emit(cs, 0x99999999);

   /* fs inputs: none, prefetch in blit case */
   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_PREFETCH_CNTL, 1 + blit);
   tu_cs_emit(cs, A6XX_SP_FS_PREFETCH_CNTL_COUNT(blit) |
                  A6XX_SP_FS_PREFETCH_CNTL_UNK4(0xfc) |
                  0x7000);
   if (blit) {
         tu_cs_emit(cs, A6XX_SP_FS_PREFETCH_CMD_SRC(4) |
                        A6XX_SP_FS_PREFETCH_CMD_SAMP_ID(0) |
                        A6XX_SP_FS_PREFETCH_CMD_TEX_ID(0) |
                        A6XX_SP_FS_PREFETCH_CMD_DST(0) |
                        A6XX_SP_FS_PREFETCH_CMD_WRMASK(0xf) |
                        A6XX_SP_FS_PREFETCH_CMD_CMD(0x4));
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_HLSQ_CONTROL_1_REG, 5);
   tu_cs_emit(cs, 0x3); // XXX blob uses 3 in blit path
   tu_cs_emit(cs, 0xfcfcfcfc);
   tu_cs_emit(cs, A6XX_HLSQ_CONTROL_3_REG_BARY_IJ_PIXEL(blit ? 0 : 0xfc) |
                  A6XX_HLSQ_CONTROL_3_REG_BARY_IJ_CENTROID(0xfc) |
                  0xfc00fc00);
   tu_cs_emit(cs, 0xfcfcfcfc);
   tu_cs_emit(cs, 0xfcfc);

   tu_cs_emit_regs(cs, A6XX_HLSQ_UNKNOWN_B980(blit ? 3 : 1));
   tu_cs_emit_regs(cs, A6XX_GRAS_CNTL(.varying = blit));
   tu_cs_emit_regs(cs,
                   A6XX_RB_RENDER_CONTROL0(.varying = blit, .unk10 = blit),
                   A6XX_RB_RENDER_CONTROL1());

   tu_cs_emit_regs(cs, A6XX_RB_SAMPLE_CNTL());
   tu_cs_emit_regs(cs, A6XX_GRAS_UNKNOWN_8101());
   tu_cs_emit_regs(cs, A6XX_GRAS_SAMPLE_CNTL());

   /* shaders */
   struct ts_cs_memory shaders = { };
   VkResult result = tu_cs_alloc(&cmd->sub_cs, 2, 16 * sizeof(instr_t), &shaders);
   assert(result == VK_SUCCESS);

   memcpy(shaders.map, vs_code, sizeof(vs_code));

   instr_t *fs = (instr_t*) ((uint8_t*) shaders.map + FS_OFFSET);
   for (uint32_t i = 0; i < num_rts; i++) {
      /* (rpt3)mov.s32s32 r0.x, (r)c[i].x */
      fs[i] =  (instr_t) { .cat1 = { .opc_cat = 1, .src_type = TYPE_S32, .dst_type = TYPE_S32,
                              .repeat = 3, .dst = i * 4, .src_c = 1, .src_r = 1, .src = i * 4 } };
   }
   fs[num_rts] = (instr_t) { .cat0 = { .opc = OPC_END } };
   /* note: assumed <= 16 instructions (MAX_RTS is 8) */

   tu_cs_emit_regs(cs, A6XX_HLSQ_UPDATE_CNTL(0x7ffff));
   tu_cs_emit_regs(cs,
                   A6XX_HLSQ_VS_CNTL(.constlen = 8, .enabled = true),
                   A6XX_HLSQ_HS_CNTL(),
                   A6XX_HLSQ_DS_CNTL(),
                   A6XX_HLSQ_GS_CNTL());
   tu_cs_emit_regs(cs, A6XX_HLSQ_FS_CNTL(.constlen = 4 * num_rts, .enabled = true));

   tu_cs_emit_regs(cs,
                   A6XX_SP_VS_CONFIG(.enabled = true),
                   A6XX_SP_VS_INSTRLEN(1));
   tu_cs_emit_regs(cs, A6XX_SP_HS_CONFIG());
   tu_cs_emit_regs(cs, A6XX_SP_DS_CONFIG());
   tu_cs_emit_regs(cs, A6XX_SP_GS_CONFIG());
   tu_cs_emit_regs(cs,
                   A6XX_SP_FS_CONFIG(.enabled = true, .ntex = blit, .nsamp = blit),
                   A6XX_SP_FS_INSTRLEN(1));

   tu_cs_emit_regs(cs, A6XX_SP_VS_CTRL_REG0(
                        .threadsize = FOUR_QUADS,
                        .fullregfootprint = 2,
                        .mergedregs = true));
   tu_cs_emit_regs(cs, A6XX_SP_FS_CTRL_REG0(
                        .varying = blit,
                        .threadsize = FOUR_QUADS,
                        /* could this be 0 in !blit && !num_rts case ? */
                        .fullregfootprint = MAX2(1, num_rts),
                        .mergedregs = true)); /* note: tu_pipeline also sets 0x1000000 bit */

   tu_cs_emit_regs(cs, A6XX_SP_IBO_COUNT(0));

   tu_cs_emit_pkt7(cs, CP_LOAD_STATE6_GEOM, 3);
   tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(0) |
                     CP_LOAD_STATE6_0_STATE_TYPE(ST6_SHADER) |
                     CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                     CP_LOAD_STATE6_0_STATE_BLOCK(SB6_VS_SHADER) |
                     CP_LOAD_STATE6_0_NUM_UNIT(1));
   tu_cs_emit_qw(cs, shaders.iova);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_VS_OBJ_START_LO, 2);
   tu_cs_emit_qw(cs, shaders.iova);

   tu_cs_emit_pkt7(cs, CP_LOAD_STATE6_FRAG, 3);
   tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(0) |
                     CP_LOAD_STATE6_0_STATE_TYPE(ST6_SHADER) |
                     CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                     CP_LOAD_STATE6_0_STATE_BLOCK(SB6_FS_SHADER) |
                     CP_LOAD_STATE6_0_NUM_UNIT(1));
   tu_cs_emit_qw(cs, shaders.iova + FS_OFFSET);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_OBJ_START_LO, 2);
   tu_cs_emit_qw(cs, shaders.iova + FS_OFFSET);

   tu_cs_emit_regs(cs,
                   A6XX_GRAS_CL_CNTL(
                      .persp_division_disable = 1,
                      .vp_xform_disable = 1,
                      .vp_clip_code_ignore = 1,
                      .clip_disable = 1),
                   A6XX_GRAS_UNKNOWN_8001(0));
   tu_cs_emit_regs(cs, A6XX_GRAS_SU_CNTL()); // XXX msaa enable?

   tu_cs_emit_regs(cs,
                   A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL_0(.x = 0, .y = 0),
                   A6XX_GRAS_SC_VIEWPORT_SCISSOR_BR_0(.x = 0x7fff, .y = 0x7fff));
   tu_cs_emit_regs(cs,
                   A6XX_GRAS_SC_SCREEN_SCISSOR_TL_0(.x = 0, .y = 0),
                   A6XX_GRAS_SC_SCREEN_SCISSOR_BR_0(.x = 0x7fff, .y = 0x7fff));
}

static void
r3d_coords_raw(struct tu_cs *cs, const float *coords)
{
   tu_cs_emit_pkt7(cs, CP_LOAD_STATE6_GEOM, 3 + 8);
   tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(0) |
                  CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                  CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                  CP_LOAD_STATE6_0_STATE_BLOCK(SB6_VS_SHADER) |
                  CP_LOAD_STATE6_0_NUM_UNIT(2));
   tu_cs_emit(cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
   tu_cs_emit(cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));
   tu_cs_emit_array(cs, (const uint32_t *) coords, 8);
}

static void
r3d_coords(struct tu_cs *cs,
           const VkOffset2D *dst,
           const VkOffset2D *src,
           const VkExtent2D *extent)
{
   int32_t src_x1 = src ? src->x : 0;
   int32_t src_y1 = src ? src->y : 0;
   r3d_coords_raw(cs, (float[]) {
      dst->x,                 dst->y,
      src_x1,                 src_y1,
      dst->x + extent->width, dst->y + extent->height,
      src_x1 + extent->width, src_y1 + extent->height,
   });
}

static void
r3d_clear_value(struct tu_cs *cs, VkFormat format, const VkClearValue *val)
{
   tu_cs_emit_pkt7(cs, CP_LOAD_STATE6_FRAG, 3 + 4);
   tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(0) |
                  CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                  CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                  CP_LOAD_STATE6_0_STATE_BLOCK(SB6_FS_SHADER) |
                  CP_LOAD_STATE6_0_NUM_UNIT(1));
   tu_cs_emit(cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
   tu_cs_emit(cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));
   switch (format) {
   case VK_FORMAT_X8_D24_UNORM_PACK32:
   case VK_FORMAT_D24_UNORM_S8_UINT: {
      /* cleared as r8g8b8a8_unorm using special format */
      uint32_t tmp = tu_pack_float32_for_unorm(val->depthStencil.depth, 24);
      tu_cs_emit(cs, fui((tmp & 0xff) / 255.0f));
      tu_cs_emit(cs, fui((tmp >> 8 & 0xff) / 255.0f));
      tu_cs_emit(cs, fui((tmp >> 16 & 0xff) / 255.0f));
      tu_cs_emit(cs, fui((val->depthStencil.stencil & 0xff) / 255.0f));
   } break;
   case VK_FORMAT_D16_UNORM:
   case VK_FORMAT_D32_SFLOAT:
      tu_cs_emit(cs, fui(val->depthStencil.depth));
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, 0);
      break;
   case VK_FORMAT_S8_UINT:
      tu_cs_emit(cs, val->depthStencil.stencil & 0xff);
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, 0);
      break;
   default:
      /* as color formats use clear value as-is */
      assert(!vk_format_is_depth_or_stencil(format));
      tu_cs_emit_array(cs, val->color.uint32, 4);
      break;
   }
}

static void
r3d_src_common(struct tu_cmd_buffer *cmd,
               struct tu_cs *cs,
               const uint32_t *tex_const,
               uint32_t offset_base,
               uint32_t offset_ubwc,
               bool linear_filter)
{
   struct ts_cs_memory texture = { };
   VkResult result = tu_cs_alloc(&cmd->sub_cs,
                                 2, /* allocate space for a sampler too */
                                 A6XX_TEX_CONST_DWORDS, &texture);
   assert(result == VK_SUCCESS);

   memcpy(texture.map, tex_const, A6XX_TEX_CONST_DWORDS * 4);

   /* patch addresses for layer offset */
   *(uint64_t*) (texture.map + 4) += offset_base;
   uint64_t ubwc_addr = (texture.map[7] | (uint64_t) texture.map[8] << 32) + offset_ubwc;
   texture.map[7] = ubwc_addr;
   texture.map[8] = ubwc_addr >> 32;

   texture.map[A6XX_TEX_CONST_DWORDS + 0] =
      A6XX_TEX_SAMP_0_XY_MAG(linear_filter ? A6XX_TEX_LINEAR : A6XX_TEX_NEAREST) |
      A6XX_TEX_SAMP_0_XY_MIN(linear_filter ? A6XX_TEX_LINEAR : A6XX_TEX_NEAREST) |
      A6XX_TEX_SAMP_0_WRAP_S(A6XX_TEX_CLAMP_TO_EDGE) |
      A6XX_TEX_SAMP_0_WRAP_T(A6XX_TEX_CLAMP_TO_EDGE) |
      A6XX_TEX_SAMP_0_WRAP_R(A6XX_TEX_CLAMP_TO_EDGE) |
      0x60000; /* XXX used by blob, doesn't seem necessary */
   texture.map[A6XX_TEX_CONST_DWORDS + 1] =
      0x1 | /* XXX used by blob, doesn't seem necessary */
      A6XX_TEX_SAMP_1_UNNORM_COORDS |
      A6XX_TEX_SAMP_1_MIPFILTER_LINEAR_FAR;
   texture.map[A6XX_TEX_CONST_DWORDS + 2] = 0;
   texture.map[A6XX_TEX_CONST_DWORDS + 3] = 0;

   tu_cs_emit_pkt7(cs, CP_LOAD_STATE6_FRAG, 3);
   tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(0) |
               CP_LOAD_STATE6_0_STATE_TYPE(ST6_SHADER) |
               CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
               CP_LOAD_STATE6_0_STATE_BLOCK(SB6_FS_TEX) |
               CP_LOAD_STATE6_0_NUM_UNIT(1));
   tu_cs_emit_qw(cs, texture.iova + A6XX_TEX_CONST_DWORDS * 4);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_TEX_SAMP_LO, 2);
   tu_cs_emit_qw(cs, texture.iova + A6XX_TEX_CONST_DWORDS * 4);

   tu_cs_emit_pkt7(cs, CP_LOAD_STATE6_FRAG, 3);
   tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(0) |
      CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
      CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
      CP_LOAD_STATE6_0_STATE_BLOCK(SB6_FS_TEX) |
      CP_LOAD_STATE6_0_NUM_UNIT(1));
   tu_cs_emit_qw(cs, texture.iova);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_TEX_CONST_LO, 2);
   tu_cs_emit_qw(cs, texture.iova);

   tu_cs_emit_regs(cs, A6XX_SP_FS_TEX_COUNT(1));
}

static void
r3d_src(struct tu_cmd_buffer *cmd,
        struct tu_cs *cs,
        const struct tu_image_view *iview,
        uint32_t layer,
        bool linear_filter)
{
   r3d_src_common(cmd, cs, iview->descriptor,
                  iview->layer_size * layer,
                  iview->ubwc_layer_size * layer,
                  linear_filter);
}

static void
r3d_src_buffer(struct tu_cmd_buffer *cmd,
               struct tu_cs *cs,
               VkFormat vk_format,
               uint64_t va, uint32_t pitch,
               uint32_t width, uint32_t height)
{
   uint32_t desc[A6XX_TEX_CONST_DWORDS];

   struct tu_native_format format = tu6_format_texture(vk_format, TILE6_LINEAR);

   desc[0] =
      COND(vk_format_is_srgb(vk_format), A6XX_TEX_CONST_0_SRGB) |
      A6XX_TEX_CONST_0_FMT(format.fmt) |
      A6XX_TEX_CONST_0_SWAP(format.swap) |
      A6XX_TEX_CONST_0_SWIZ_X(A6XX_TEX_X) |
      // XXX to swizzle into .w for stencil buffer_to_image
      A6XX_TEX_CONST_0_SWIZ_Y(vk_format == VK_FORMAT_R8_UNORM ? A6XX_TEX_X : A6XX_TEX_Y) |
      A6XX_TEX_CONST_0_SWIZ_Z(vk_format == VK_FORMAT_R8_UNORM ? A6XX_TEX_X : A6XX_TEX_Z) |
      A6XX_TEX_CONST_0_SWIZ_W(vk_format == VK_FORMAT_R8_UNORM ? A6XX_TEX_X : A6XX_TEX_W);
   desc[1] = A6XX_TEX_CONST_1_WIDTH(width) | A6XX_TEX_CONST_1_HEIGHT(height);
   desc[2] =
      A6XX_TEX_CONST_2_FETCHSIZE(tu6_fetchsize(vk_format)) |
      A6XX_TEX_CONST_2_PITCH(pitch) |
      A6XX_TEX_CONST_2_TYPE(A6XX_TEX_2D);
   desc[3] = 0;
   desc[4] = va;
   desc[5] = va >> 32;
   for (uint32_t i = 6; i < A6XX_TEX_CONST_DWORDS; i++)
      desc[i] = 0;

   r3d_src_common(cmd, cs, desc, 0, 0, false);
}

static void
r3d_dst(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer)
{
   tu6_emit_msaa(cs, iview->image->samples); /* TODO: move to setup */

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_BUF_INFO(0), 6);
   tu_cs_emit(cs, iview->RB_MRT_BUF_INFO);
   tu_cs_image_ref(cs, iview, layer);
   tu_cs_emit(cs, 0);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_FLAG_BUFFER(0), 3);
   tu_cs_image_flag_ref(cs, iview, layer);

   tu_cs_emit_regs(cs, A6XX_RB_RENDER_CNTL(.flag_mrts = iview->ubwc_enabled));
}

static void
r3d_dst_buffer(struct tu_cs *cs, VkFormat vk_format, uint64_t va, uint32_t pitch)
{
   struct tu_native_format format = tu6_format_color(vk_format, TILE6_LINEAR);

   tu6_emit_msaa(cs, 1); /* TODO: move to setup */

   tu_cs_emit_regs(cs,
                   A6XX_RB_MRT_BUF_INFO(0, .color_format = format.fmt, .color_swap = format.swap),
                   A6XX_RB_MRT_PITCH(0, pitch),
                   A6XX_RB_MRT_ARRAY_PITCH(0, 0),
                   A6XX_RB_MRT_BASE_LO(0, (uint32_t) va),
                   A6XX_RB_MRT_BASE_HI(0, va >> 32),
                   A6XX_RB_MRT_BASE_GMEM(0, 0));

   tu_cs_emit_regs(cs, A6XX_RB_RENDER_CNTL());
}

static void
r3d_setup(struct tu_cmd_buffer *cmd,
          struct tu_cs *cs,
          VkFormat vk_format,
          enum a6xx_rotation rotation,
          bool clear,
          uint8_t mask)
{
   const struct tu_physical_device *phys_dev = cmd->device->physical_device;

   if (!cmd->state.pass) {
      /* TODO: flushing with barriers instead of blindly always flushing */
      tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS, true);
      tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_DEPTH_TS, true);
      tu6_emit_event_write(cmd, cs, PC_CCU_INVALIDATE_COLOR, false);
      tu6_emit_event_write(cmd, cs, PC_CCU_INVALIDATE_DEPTH, false);
      tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE, false);

      tu_cs_emit_regs(cs,
                     A6XX_RB_CCU_CNTL(.offset = phys_dev->ccu_offset_bypass));

      tu6_emit_window_scissor(cs, 0, 0, 0x7fff, 0x7fff);
   }
   tu_cs_emit_regs(cs, A6XX_GRAS_BIN_CONTROL(.dword = 0xc00000));
   tu_cs_emit_regs(cs, A6XX_RB_BIN_CONTROL(.dword = 0xc00000));

   r3d_pipeline(cmd, cs, !clear, clear ? 1 : 0);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_OUTPUT_CNTL0, 2);
   tu_cs_emit(cs, A6XX_SP_FS_OUTPUT_CNTL0_DEPTH_REGID(0xfc) |
                  A6XX_SP_FS_OUTPUT_CNTL0_SAMPMASK_REGID(0xfc) |
                  0xfc000000);
   tu_cs_emit(cs, A6XX_SP_FS_OUTPUT_CNTL1_MRT(1));

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_OUTPUT_REG(0), 1);
   tu_cs_emit(cs, A6XX_SP_FS_OUTPUT_REG_REGID(0));

   tu_cs_emit_regs(cs,
                   A6XX_RB_FS_OUTPUT_CNTL0(),
                   A6XX_RB_FS_OUTPUT_CNTL1(.mrt = 1));

   tu_cs_emit_regs(cs, A6XX_SP_BLEND_CNTL());
   tu_cs_emit_regs(cs, A6XX_RB_BLEND_CNTL(.sample_mask = 0xffff));
   tu_cs_emit_regs(cs, A6XX_RB_ALPHA_CONTROL());

   tu_cs_emit_regs(cs, A6XX_RB_DEPTH_PLANE_CNTL());
   tu_cs_emit_regs(cs, A6XX_RB_DEPTH_CNTL());
   tu_cs_emit_regs(cs, A6XX_GRAS_SU_DEPTH_PLANE_CNTL());
   tu_cs_emit_regs(cs, A6XX_RB_STENCIL_CONTROL());
   tu_cs_emit_regs(cs, A6XX_RB_STENCILMASK());
   tu_cs_emit_regs(cs, A6XX_RB_STENCILWRMASK());
   tu_cs_emit_regs(cs, A6XX_RB_STENCILREF());

   tu_cs_emit_regs(cs, A6XX_RB_RENDER_COMPONENTS(.rt0 = 0xf));
   tu_cs_emit_regs(cs, A6XX_SP_FS_RENDER_COMPONENTS(.rt0 = 0xf));

   tu_cs_emit_regs(cs, A6XX_SP_FS_MRT_REG(0,
                        .color_format = tu6_base_format(vk_format),
                        .color_sint = vk_format_is_sint(vk_format),
                        .color_uint = vk_format_is_uint(vk_format)));

   tu_cs_emit_regs(cs, A6XX_RB_MRT_CONTROL(0, .component_enable = mask));
   tu_cs_emit_regs(cs, A6XX_RB_SRGB_CNTL(vk_format_is_srgb(vk_format)));
   tu_cs_emit_regs(cs, A6XX_SP_SRGB_CNTL(vk_format_is_srgb(vk_format)));
}

static void
r3d_run(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   tu_cs_emit_pkt7(cs, CP_DRAW_INDX_OFFSET, 3);
   tu_cs_emit(cs, CP_DRAW_INDX_OFFSET_0_PRIM_TYPE(DI_PT_RECTLIST) |
                  CP_DRAW_INDX_OFFSET_0_SOURCE_SELECT(DI_SRC_SEL_AUTO_INDEX) |
                  CP_DRAW_INDX_OFFSET_0_VIS_CULL(IGNORE_VISIBILITY));
   tu_cs_emit(cs, 1); /* instance count */
   tu_cs_emit(cs, 2); /* vertex count */

   if (!cmd->state.pass) {
      /* TODO: flushing with barriers instead of blindly always flushing */
      tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS, true);
      tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_DEPTH_TS, true);
      tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE, false);
   }
}

/* blit ops - common interface for 2d/shader paths */

struct blit_ops {
   void (*coords)(struct tu_cs *cs,
                  const VkOffset2D *dst,
                  const VkOffset2D *src,
                  const VkExtent2D *extent);
   void (*clear_value)(struct tu_cs *cs, VkFormat format, const VkClearValue *val);
   void (*src)(
        struct tu_cmd_buffer *cmd,
        struct tu_cs *cs,
        const struct tu_image_view *iview,
        uint32_t layer,
        bool linear_filter);
   void (*src_buffer)(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                      VkFormat vk_format,
                      uint64_t va, uint32_t pitch,
                      uint32_t width, uint32_t height);
   void (*dst)(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer);
   void (*dst_buffer)(struct tu_cs *cs, VkFormat vk_format, uint64_t va, uint32_t pitch);
   void (*setup)(struct tu_cmd_buffer *cmd,
                 struct tu_cs *cs,
                 VkFormat vk_format,
                 enum a6xx_rotation rotation,
                 bool clear,
                 uint8_t mask);
   void (*run)(struct tu_cmd_buffer *cmd, struct tu_cs *cs);
};

static const struct blit_ops r2d_ops = {
   .coords = r2d_coords,
   .clear_value = r2d_clear_value,
   .src = r2d_src,
   .src_buffer = r2d_src_buffer,
   .dst = r2d_dst,
   .dst_buffer = r2d_dst_buffer,
   .setup = r2d_setup,
   .run = r2d_run,
};

static const struct blit_ops r3d_ops = {
   .coords = r3d_coords,
   .clear_value = r3d_clear_value,
   .src = r3d_src,
   .src_buffer = r3d_src_buffer,
   .dst = r3d_dst,
   .dst_buffer = r3d_dst_buffer,
   .setup = r3d_setup,
   .run = r3d_run,
};

/* passthrough set coords from 3D extents */
static void
coords(const struct blit_ops *ops,
       struct tu_cs *cs,
       const VkOffset3D *dst,
       const VkOffset3D *src,
       const VkExtent3D *extent)
{
   ops->coords(cs, (const VkOffset2D*) dst, (const VkOffset2D*) src, (const VkExtent2D*) extent);
}

static void
tu_image_view_blit2(struct tu_image_view *iview,
                    struct tu_image *image,
                    VkFormat format,
                    const VkImageSubresourceLayers *subres,
                    uint32_t layer,
                    bool stencil_read)
{
   VkImageAspectFlags aspect_mask = subres->aspectMask;

   /* always use the AS_R8G8B8A8 format for these */
   if (format == VK_FORMAT_D24_UNORM_S8_UINT ||
       format == VK_FORMAT_X8_D24_UNORM_PACK32) {
      aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
   }

   tu_image_view_init(iview, &(VkImageViewCreateInfo) {
      .image = tu_image_to_handle(image),
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      /* image_to_buffer from d24s8 with stencil aspect mask writes out to r8 */
      .components.r = stencil_read ? VK_COMPONENT_SWIZZLE_A : VK_COMPONENT_SWIZZLE_R,
      .subresourceRange = {
         .aspectMask = aspect_mask,
         .baseMipLevel = subres->mipLevel,
         .levelCount = 1,
         .baseArrayLayer = subres->baseArrayLayer + layer,
         .layerCount = 1,
      },
   });
}

static void
tu_image_view_blit(struct tu_image_view *iview,
                   struct tu_image *image,
                   const VkImageSubresourceLayers *subres,
                   uint32_t layer)
{
   tu_image_view_blit2(iview, image, image->vk_format, subres, layer, false);
}

static void
tu6_blit_image(struct tu_cmd_buffer *cmd,
               struct tu_image *src_image,
               struct tu_image *dst_image,
               const VkImageBlit *info,
               VkFilter filter)
{
   const struct blit_ops *ops = &r2d_ops;
   struct tu_cs *cs = &cmd->cs;
   uint32_t layers;

   /* 2D blit can't do rotation mirroring from just coordinates */
   static const enum a6xx_rotation rotate[2][2] = {
      {ROTATE_0, ROTATE_HFLIP},
      {ROTATE_VFLIP, ROTATE_180},
   };

   bool mirror_x = (info->srcOffsets[1].x < info->srcOffsets[0].x) !=
                   (info->dstOffsets[1].x < info->dstOffsets[0].x);
   bool mirror_y = (info->srcOffsets[1].y < info->srcOffsets[0].y) !=
                   (info->dstOffsets[1].y < info->dstOffsets[0].y);
   bool mirror_z = (info->srcOffsets[1].z < info->srcOffsets[0].z) !=
                   (info->dstOffsets[1].z < info->dstOffsets[0].z);

   if (mirror_z) {
      tu_finishme("blit z mirror\n");
      return;
   }

   if (info->srcOffsets[1].z - info->srcOffsets[0].z !=
       info->dstOffsets[1].z - info->dstOffsets[0].z) {
      tu_finishme("blit z filter\n");
      return;
   }

   layers = info->srcOffsets[1].z - info->srcOffsets[0].z;
   if (info->dstSubresource.layerCount > 1) {
      assert(layers <= 1);
      layers = info->dstSubresource.layerCount;
   }

   uint8_t mask = 0xf;
   if (dst_image->vk_format == VK_FORMAT_D24_UNORM_S8_UINT) {
      assert(info->srcSubresource.aspectMask == info->dstSubresource.aspectMask);
      if (info->dstSubresource.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT)
         mask = 0x7;
      if (info->dstSubresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT)
         mask = 0x8;
   }

   if (dst_image->samples > 1)
      ops = &r3d_ops;

   /* TODO: shader path fails some of blit_image.all_formats.generate_mipmaps.* tests,
    * figure out why (should be able to pass all tests with only shader path)
    */

   ops->setup(cmd, cs, dst_image->vk_format, rotate[mirror_y][mirror_x], false, mask);

   if (ops == &r3d_ops) {
      r3d_coords_raw(cs, (float[]) {
         info->dstOffsets[0].x, info->dstOffsets[0].y,
         info->srcOffsets[0].x, info->srcOffsets[0].y,
         info->dstOffsets[1].x, info->dstOffsets[1].y,
         info->srcOffsets[1].x, info->srcOffsets[1].y
      });
   } else {
      tu_cs_emit_regs(cs,
         A6XX_GRAS_2D_DST_TL(.x = MIN2(info->dstOffsets[0].x, info->dstOffsets[1].x),
                             .y = MIN2(info->dstOffsets[0].y, info->dstOffsets[1].y)),
         A6XX_GRAS_2D_DST_BR(.x = MAX2(info->dstOffsets[0].x, info->dstOffsets[1].x) - 1,
                             .y = MAX2(info->dstOffsets[0].y, info->dstOffsets[1].y) - 1));
      tu_cs_emit_regs(cs,
         A6XX_GRAS_2D_SRC_TL_X(.x = MIN2(info->srcOffsets[0].x, info->srcOffsets[1].x)),
         A6XX_GRAS_2D_SRC_BR_X(.x = MAX2(info->srcOffsets[0].x, info->srcOffsets[1].x) - 1),
         A6XX_GRAS_2D_SRC_TL_Y(.y = MIN2(info->srcOffsets[0].y, info->srcOffsets[1].y)),
         A6XX_GRAS_2D_SRC_BR_Y(.y = MAX2(info->srcOffsets[0].y, info->srcOffsets[1].y) - 1));
   }

   struct tu_image_view dst, src;
   tu_image_view_blit(&dst, dst_image, &info->dstSubresource, info->dstOffsets[0].z);
   tu_image_view_blit(&src, src_image, &info->srcSubresource, info->srcOffsets[0].z);

   for (uint32_t i = 0; i < layers; i++) {
      ops->dst(cs, &dst, i);
      ops->src(cmd, cs, &src, i, filter == VK_FILTER_LINEAR);
      ops->run(cmd, cs);
   }
}

void
tu_CmdBlitImage(VkCommandBuffer commandBuffer,
                VkImage srcImage,
                VkImageLayout srcImageLayout,
                VkImage dstImage,
                VkImageLayout dstImageLayout,
                uint32_t regionCount,
                const VkImageBlit *pRegions,
                VkFilter filter)

{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_image, src_image, srcImage);
   TU_FROM_HANDLE(tu_image, dst_image, dstImage);

   tu_bo_list_add(&cmd->bo_list, src_image->bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmd->bo_list, dst_image->bo, MSM_SUBMIT_BO_WRITE);

   for (uint32_t i = 0; i < regionCount; ++i)
      tu6_blit_image(cmd, src_image, dst_image, pRegions + i, filter);
}

static VkFormat
copy_format(VkFormat format)
{
   switch (vk_format_get_blocksizebits(format)) {
   case 8:  return VK_FORMAT_R8_UINT;
   case 16: return VK_FORMAT_R16_UINT;
   case 32: return VK_FORMAT_R32_UINT;
   case 64: return VK_FORMAT_R32G32_UINT;
   case 96: return VK_FORMAT_R32G32B32_UINT;
   case 128:return VK_FORMAT_R32G32B32A32_UINT;
   default:
      unreachable("unhandled format size");
   }
}

static void
copy_compressed(VkFormat format,
                VkOffset3D *offset,
                VkExtent3D *extent,
                uint32_t *pitch,
                uint32_t *layer_size)
{
   if (!vk_format_is_compressed(format))
      return;

   uint32_t block_width = vk_format_get_blockwidth(format);
   uint32_t block_height = vk_format_get_blockheight(format);

   offset->x /= block_width;
   offset->y /= block_height;

   if (extent) {
      extent->width = DIV_ROUND_UP(extent->width, block_width);
      extent->height = DIV_ROUND_UP(extent->height, block_height);
   }
   if (pitch)
      *pitch /= block_width;
   if (layer_size)
      *layer_size /= (block_width * block_height);
}

static void
tu_copy_buffer_to_image(struct tu_cmd_buffer *cmd,
                        struct tu_buffer *src_buffer,
                        struct tu_image *dst_image,
                        const VkBufferImageCopy *info)
{
   struct tu_cs *cs = &cmd->cs;
   uint32_t layers = MAX2(info->imageExtent.depth, info->imageSubresource.layerCount);
   VkFormat dst_format = dst_image->vk_format;
   VkFormat src_format = dst_image->vk_format;
   const struct blit_ops *ops = &r2d_ops;

   uint8_t mask = 0xf;

   if (dst_image->vk_format == VK_FORMAT_D24_UNORM_S8_UINT) {
      switch (info->imageSubresource.aspectMask) {
      case VK_IMAGE_ASPECT_STENCIL_BIT:
         src_format = VK_FORMAT_R8_UNORM; /* changes how src buffer is interpreted */
         mask = 0x8;
         ops = &r3d_ops;
         break;
      case VK_IMAGE_ASPECT_DEPTH_BIT:
         mask = 0x7;
         break;
      }
   }

   VkOffset3D offset = info->imageOffset;
   VkExtent3D extent = info->imageExtent;
   uint32_t pitch =
      (info->bufferRowLength ?: extent.width) * vk_format_get_blocksize(src_format);
   uint32_t layer_size = (info->bufferImageHeight ?: extent.height) * pitch;

   if (dst_format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 || vk_format_is_compressed(src_format)) {
      assert(src_format == dst_format);
      copy_compressed(dst_format, &offset, &extent, &pitch, &layer_size);
      src_format = dst_format = copy_format(dst_format);
   }

   /* note: the src_va/pitch alignment of 64 is for 2D engine,
    * it is also valid for 1cpp format with shader path (stencil aspect path)
    */

   ops->setup(cmd, cs, dst_format, ROTATE_0, false, mask);

   struct tu_image_view dst;
   tu_image_view_blit2(&dst, dst_image, dst_format, &info->imageSubresource, offset.z, false);

   for (uint32_t i = 0; i < layers; i++) {
      ops->dst(cs, &dst, i);

      uint64_t src_va = tu_buffer_iova(src_buffer) + info->bufferOffset + layer_size * i;
      if ((src_va & 63) || (pitch & 63)) {
         for (uint32_t y = 0; y < extent.height; y++) {
            uint32_t x = (src_va & 63) / vk_format_get_blocksize(src_format);
            ops->src_buffer(cmd, cs, src_format, src_va & ~63, pitch,
                            x + extent.width, 1);
            ops->coords(cs, &(VkOffset2D){offset.x, offset.y + y},  &(VkOffset2D){x},
                        &(VkExtent2D) {extent.width, 1});
            ops->run(cmd, cs);
            src_va += pitch;
         }
      } else {
         ops->src_buffer(cmd, cs, src_format, src_va, pitch, extent.width, extent.height);
         coords(ops, cs, &offset, &(VkOffset3D){}, &extent);
         ops->run(cmd, cs);
      }
   }
}

void
tu_CmdCopyBufferToImage(VkCommandBuffer commandBuffer,
                        VkBuffer srcBuffer,
                        VkImage dstImage,
                        VkImageLayout dstImageLayout,
                        uint32_t regionCount,
                        const VkBufferImageCopy *pRegions)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_image, dst_image, dstImage);
   TU_FROM_HANDLE(tu_buffer, src_buffer, srcBuffer);

   tu_bo_list_add(&cmd->bo_list, src_buffer->bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmd->bo_list, dst_image->bo, MSM_SUBMIT_BO_WRITE);

   for (unsigned i = 0; i < regionCount; ++i)
      tu_copy_buffer_to_image(cmd, src_buffer, dst_image, pRegions + i);
}

static void
tu_copy_image_to_buffer(struct tu_cmd_buffer *cmd,
                        struct tu_image *src_image,
                        struct tu_buffer *dst_buffer,
                        const VkBufferImageCopy *info)
{
   struct tu_cs *cs = &cmd->cs;
   uint32_t layers = MAX2(info->imageExtent.depth, info->imageSubresource.layerCount);
   VkFormat src_format = src_image->vk_format;
   VkFormat dst_format = src_image->vk_format;
   bool stencil_read = false;

   if (src_image->vk_format == VK_FORMAT_D24_UNORM_S8_UINT &&
       info->imageSubresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT) {
      dst_format = VK_FORMAT_R8_UNORM;
      stencil_read = true;
   }

   const struct blit_ops *ops = stencil_read ? &r3d_ops : &r2d_ops;
   VkOffset3D offset = info->imageOffset;
   VkExtent3D extent = info->imageExtent;
   uint32_t pitch = (info->bufferRowLength ?: extent.width) * vk_format_get_blocksize(dst_format);
   uint32_t layer_size = (info->bufferImageHeight ?: extent.height) * pitch;

   if (dst_format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 || vk_format_is_compressed(dst_format)) {
      assert(src_format == dst_format);
      copy_compressed(dst_format, &offset, &extent, &pitch, &layer_size);
      src_format = dst_format = copy_format(dst_format);
   }

   /* note: the dst_va/pitch alignment of 64 is for 2D engine,
    * it is also valid for 1cpp format with shader path (stencil aspect)
    */

   ops->setup(cmd, cs, dst_format, ROTATE_0, false, 0xf);

   struct tu_image_view src;
   tu_image_view_blit2(&src, src_image, src_format, &info->imageSubresource, offset.z, stencil_read);

   for (uint32_t i = 0; i < layers; i++) {
      ops->src(cmd, cs, &src, i, false);

      uint64_t dst_va = tu_buffer_iova(dst_buffer) + info->bufferOffset + layer_size * i;
      if ((dst_va & 63) || (pitch & 63)) {
         for (uint32_t y = 0; y < extent.height; y++) {
            uint32_t x = (dst_va & 63) / vk_format_get_blocksize(dst_format);
            ops->dst_buffer(cs, dst_format, dst_va & ~63, 0);
            ops->coords(cs, &(VkOffset2D) {x}, &(VkOffset2D){offset.x, offset.y + y},
                        &(VkExtent2D) {extent.width, 1});
            ops->run(cmd, cs);
            dst_va += pitch;
         }
      } else {
         ops->dst_buffer(cs, dst_format, dst_va, pitch);
         coords(ops, cs, &(VkOffset3D) {0, 0}, &offset, &extent);
         ops->run(cmd, cs);
      }
   }
}

void
tu_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer,
                        VkImage srcImage,
                        VkImageLayout srcImageLayout,
                        VkBuffer dstBuffer,
                        uint32_t regionCount,
                        const VkBufferImageCopy *pRegions)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_image, src_image, srcImage);
   TU_FROM_HANDLE(tu_buffer, dst_buffer, dstBuffer);

   tu_bo_list_add(&cmd->bo_list, src_image->bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmd->bo_list, dst_buffer->bo, MSM_SUBMIT_BO_WRITE);

   for (unsigned i = 0; i < regionCount; ++i)
      tu_copy_image_to_buffer(cmd, src_image, dst_buffer, pRegions + i);
}

static void
tu_copy_image_to_image(struct tu_cmd_buffer *cmd,
                       struct tu_image *src_image,
                       struct tu_image *dst_image,
                       const VkImageCopy *info)
{
   const struct blit_ops *ops = &r2d_ops;
   struct tu_cs *cs = &cmd->cs;

   uint8_t mask = 0xf;
   if (dst_image->vk_format == VK_FORMAT_D24_UNORM_S8_UINT) {
      if (info->dstSubresource.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT)
         mask = 0x7;
      if (info->dstSubresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT)
         mask = 0x8;
   }

   if (dst_image->samples > 1)
      ops = &r3d_ops;

   assert(info->srcSubresource.aspectMask == info->dstSubresource.aspectMask);

   VkFormat format = VK_FORMAT_UNDEFINED;
   VkOffset3D src_offset = info->srcOffset;
   VkOffset3D dst_offset = info->dstOffset;
   VkExtent3D extent = info->extent;

   /* TODO: should check (ubwc || (tile_mode && swap)) instead */
   if (src_image->layout.tile_mode && src_image->vk_format != VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)
      format = src_image->vk_format;

   if (dst_image->layout.tile_mode && dst_image->vk_format != VK_FORMAT_E5B9G9R9_UFLOAT_PACK32) {
      if (format != VK_FORMAT_UNDEFINED && format != dst_image->vk_format) {
         /* can be clever in some cases but in some cases we need and intermediate
         * linear buffer
         */
         tu_finishme("image copy between two tiled/ubwc images\n");
         return;
      }
      format = dst_image->vk_format;
   }

   if (format == VK_FORMAT_UNDEFINED)
      format = copy_format(src_image->vk_format);

   copy_compressed(src_image->vk_format, &src_offset, &extent, NULL, NULL);
   copy_compressed(dst_image->vk_format, &dst_offset, NULL, NULL, NULL);

   ops->setup(cmd, cs, format, ROTATE_0, false, mask);
   coords(ops, cs, &dst_offset, &src_offset, &extent);

   struct tu_image_view dst, src;
   tu_image_view_blit2(&dst, dst_image, format, &info->dstSubresource, dst_offset.z, false);
   tu_image_view_blit2(&src, src_image, format, &info->srcSubresource, src_offset.z, false);

   for (uint32_t i = 0; i < info->extent.depth; i++) {
      ops->src(cmd, cs, &src, i, false);
      ops->dst(cs, &dst, i);
      ops->run(cmd, cs);
   }
}

void
tu_CmdCopyImage(VkCommandBuffer commandBuffer,
                VkImage srcImage,
                VkImageLayout srcImageLayout,
                VkImage destImage,
                VkImageLayout destImageLayout,
                uint32_t regionCount,
                const VkImageCopy *pRegions)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_image, src_image, srcImage);
   TU_FROM_HANDLE(tu_image, dst_image, destImage);

   tu_bo_list_add(&cmd->bo_list, src_image->bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmd->bo_list, dst_image->bo, MSM_SUBMIT_BO_WRITE);

   for (uint32_t i = 0; i < regionCount; ++i)
      tu_copy_image_to_image(cmd, src_image, dst_image, pRegions + i);
}

static void
copy_buffer(struct tu_cmd_buffer *cmd,
            uint64_t dst_va,
            uint64_t src_va,
            uint64_t size,
            uint32_t block_size)
{
   const struct blit_ops *ops = &r2d_ops;
   struct tu_cs *cs = &cmd->cs;
   VkFormat format = block_size == 4 ? VK_FORMAT_R32_UINT : VK_FORMAT_R8_UNORM;
   uint64_t blocks = size / block_size;

   ops->setup(cmd, cs, format, ROTATE_0, false, 0xf);

   while (blocks) {
      uint32_t src_x = (src_va & 63) / block_size;
      uint32_t dst_x = (dst_va & 63) / block_size;
      uint32_t width = MIN2(MIN2(blocks, 0x4000 - src_x), 0x4000 - dst_x);

      ops->src_buffer(cmd, cs, format, src_va & ~63, 0, src_x + width, 1);
      ops->dst_buffer(     cs, format, dst_va & ~63, 0);
      ops->coords(cs, &(VkOffset2D) {dst_x}, &(VkOffset2D) {src_x}, &(VkExtent2D) {width, 1});
      ops->run(cmd, cs);

      src_va += width * block_size;
      dst_va += width * block_size;
      blocks -= width;
   }
}

void
tu_CmdCopyBuffer(VkCommandBuffer commandBuffer,
                 VkBuffer srcBuffer,
                 VkBuffer dstBuffer,
                 uint32_t regionCount,
                 const VkBufferCopy *pRegions)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, src_buffer, srcBuffer);
   TU_FROM_HANDLE(tu_buffer, dst_buffer, dstBuffer);

   tu_bo_list_add(&cmd->bo_list, src_buffer->bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmd->bo_list, dst_buffer->bo, MSM_SUBMIT_BO_WRITE);

   for (unsigned i = 0; i < regionCount; ++i) {
      copy_buffer(cmd,
                  tu_buffer_iova(dst_buffer) + pRegions[i].dstOffset,
                  tu_buffer_iova(src_buffer) + pRegions[i].srcOffset,
                  pRegions[i].size, 1);
   }
}

void
tu_CmdUpdateBuffer(VkCommandBuffer commandBuffer,
                   VkBuffer dstBuffer,
                   VkDeviceSize dstOffset,
                   VkDeviceSize dataSize,
                   const void *pData)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buffer, dstBuffer);

   tu_bo_list_add(&cmd->bo_list, buffer->bo, MSM_SUBMIT_BO_WRITE);

   struct ts_cs_memory tmp;
   VkResult result = tu_cs_alloc(&cmd->sub_cs, DIV_ROUND_UP(dataSize, 64), 64, &tmp);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

   memcpy(tmp.map, pData, dataSize);
   copy_buffer(cmd, tu_buffer_iova(buffer) + dstOffset, tmp.iova, dataSize, 4);
}

void
tu_CmdFillBuffer(VkCommandBuffer commandBuffer,
                 VkBuffer dstBuffer,
                 VkDeviceSize dstOffset,
                 VkDeviceSize fillSize,
                 uint32_t data)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buffer, dstBuffer);
   const struct blit_ops *ops = &r2d_ops;
   struct tu_cs *cs = &cmd->cs;

   tu_bo_list_add(&cmd->bo_list, buffer->bo, MSM_SUBMIT_BO_WRITE);

   if (fillSize == VK_WHOLE_SIZE)
      fillSize = buffer->size - dstOffset;

   uint64_t dst_va = tu_buffer_iova(buffer) + dstOffset;
   uint32_t blocks = fillSize / 4;

   ops->setup(cmd, cs, VK_FORMAT_R32_UINT, ROTATE_0, true, 0xf);
   ops->clear_value(cs, VK_FORMAT_R32_UINT, &(VkClearValue){.color = {.uint32[0] = data}});

   while (blocks) {
      uint32_t dst_x = (dst_va & 63) / 4;
      uint32_t width = MIN2(blocks, 0x4000 - dst_x);

      ops->dst_buffer(cs, VK_FORMAT_R32_UINT, dst_va & ~63, 0);
      ops->coords(cs, &(VkOffset2D) {dst_x}, NULL, &(VkExtent2D) {width, 1});
      ops->run(cmd, cs);

      dst_va += width * 4;
      blocks -= width;
   }
}

void
tu_CmdResolveImage(VkCommandBuffer commandBuffer,
                   VkImage srcImage,
                   VkImageLayout srcImageLayout,
                   VkImage dstImage,
                   VkImageLayout dstImageLayout,
                   uint32_t regionCount,
                   const VkImageResolve *pRegions)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_image, src_image, srcImage);
   TU_FROM_HANDLE(tu_image, dst_image, dstImage);
   const struct blit_ops *ops = &r2d_ops;
   struct tu_cs *cs = &cmd->cs;

   tu_bo_list_add(&cmd->bo_list, src_image->bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmd->bo_list, dst_image->bo, MSM_SUBMIT_BO_WRITE);

   ops->setup(cmd, cs, dst_image->vk_format, ROTATE_0, false, 0xf);

   for (uint32_t i = 0; i < regionCount; ++i) {
      const VkImageResolve *info = &pRegions[i];
      uint32_t layers = MAX2(info->extent.depth, info->dstSubresource.layerCount);

      assert(info->srcSubresource.layerCount == info->dstSubresource.layerCount);
      /* TODO: aspect masks possible ? */

      coords(ops, cs, &info->dstOffset, &info->srcOffset, &info->extent);

      struct tu_image_view dst, src;
      tu_image_view_blit(&dst, dst_image, &info->dstSubresource, info->dstOffset.z);
      tu_image_view_blit(&src, src_image, &info->srcSubresource, info->srcOffset.z);

      for (uint32_t i = 0; i < layers; i++) {
         ops->src(cmd, cs, &src, i, false);
         ops->dst(cs, &dst, i);
         ops->run(cmd, cs);
      }
   }
}

void
tu_resolve_sysmem(struct tu_cmd_buffer *cmd,
                  struct tu_cs *cs,
                  struct tu_image_view *src,
                  struct tu_image_view *dst,
                  uint32_t layers,
                  const VkRect2D *rect)
{
   const struct blit_ops *ops = &r2d_ops;

   tu_bo_list_add(&cmd->bo_list, src->image->bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmd->bo_list, dst->image->bo, MSM_SUBMIT_BO_WRITE);

   assert(src->image->vk_format == dst->image->vk_format);

   ops->setup(cmd, cs, dst->image->vk_format, ROTATE_0, false, 0xf);
   ops->coords(cs, &rect->offset, &rect->offset, &rect->extent);

   for (uint32_t i = 0; i < layers; i++) {
      ops->src(cmd, cs, src, i, false);
      ops->dst(cs, dst, i);
      ops->run(cmd, cs);
   }
}

static void
clear_image(struct tu_cmd_buffer *cmd,
            struct tu_image *image,
            const VkClearValue *clear_value,
            const VkImageSubresourceRange *range)
{
   uint32_t level_count = tu_get_levelCount(image, range);
   uint32_t layer_count = tu_get_layerCount(image, range);
   struct tu_cs *cs = &cmd->cs;
   VkFormat format = image->vk_format;
   if (format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)
      format = VK_FORMAT_R32_UINT;

   if (image->type == VK_IMAGE_TYPE_3D) {
      assert(layer_count == 1);
      assert(range->baseArrayLayer == 0);
   }

   uint8_t mask = 0xf;
   if (image->vk_format == VK_FORMAT_D24_UNORM_S8_UINT) {
      mask = 0;
      if (range->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
         mask |= 0x7;
      if (range->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
         mask |= 0x8;
   }

   const struct blit_ops *ops = image->samples > 1 ? &r3d_ops : &r2d_ops;

   ops->setup(cmd, cs, format, ROTATE_0, true, mask);
   ops->clear_value(cs, image->vk_format, clear_value);

   for (unsigned j = 0; j < level_count; j++) {
      if (image->type == VK_IMAGE_TYPE_3D)
         layer_count = u_minify(image->extent.depth, range->baseMipLevel + j);

      ops->coords(cs, &(VkOffset2D){}, NULL, &(VkExtent2D) {
                     u_minify(image->extent.width, range->baseMipLevel + j),
                     u_minify(image->extent.height, range->baseMipLevel + j)
                  });

      struct tu_image_view dst;
      tu_image_view_blit2(&dst, image, format, &(VkImageSubresourceLayers) {
         .aspectMask = range->aspectMask,
         .mipLevel = range->baseMipLevel + j,
         .baseArrayLayer = range->baseArrayLayer,
         .layerCount = 1,
      }, 0, false);

      for (uint32_t i = 0; i < layer_count; i++) {
         ops->dst(cs, &dst, i);
         ops->run(cmd, cs);
      }
   }
}

void
tu_CmdClearColorImage(VkCommandBuffer commandBuffer,
                      VkImage image_h,
                      VkImageLayout imageLayout,
                      const VkClearColorValue *pColor,
                      uint32_t rangeCount,
                      const VkImageSubresourceRange *pRanges)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_image, image, image_h);

   tu_bo_list_add(&cmd->bo_list, image->bo, MSM_SUBMIT_BO_WRITE);

   for (unsigned i = 0; i < rangeCount; i++)
      clear_image(cmd, image, (const VkClearValue*) pColor, pRanges + i);
}

void
tu_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer,
                             VkImage image_h,
                             VkImageLayout imageLayout,
                             const VkClearDepthStencilValue *pDepthStencil,
                             uint32_t rangeCount,
                             const VkImageSubresourceRange *pRanges)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_image, image, image_h);

   tu_bo_list_add(&cmd->bo_list, image->bo, MSM_SUBMIT_BO_WRITE);

   for (unsigned i = 0; i < rangeCount; i++)
      clear_image(cmd, image, (const VkClearValue*) pDepthStencil, pRanges + i);
}

static void
tu_clear_sysmem_attachments_2d(struct tu_cmd_buffer *cmd,
                               uint32_t attachment_count,
                               const VkClearAttachment *attachments,
                               uint32_t rect_count,
                               const VkClearRect *rects)
{
   const struct tu_subpass *subpass = cmd->state.subpass;
   /* note: cannot use shader path here.. there is a special shader path
    * in tu_clear_sysmem_attachments()
    */
   const struct blit_ops *ops = &r2d_ops;
   struct tu_cs *cs = &cmd->draw_cs;

   for (uint32_t j = 0; j < attachment_count; j++) {
         uint32_t a;
         if (attachments[j].aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
            a = subpass->color_attachments[attachments[j].colorAttachment].attachment;
         } else {
            a = subpass->depth_stencil_attachment.attachment;

            /* sync depth into color */
            tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_DEPTH_TS, true);
            /* also flush color to avoid losing contents from invalidate */
            tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS, true);
            tu6_emit_event_write(cmd, cs, PC_CCU_INVALIDATE_COLOR, false);
         }

         if (a == VK_ATTACHMENT_UNUSED)
               continue;

         uint8_t mask = 0xf;
         if (cmd->state.pass->attachments[a].format == VK_FORMAT_D24_UNORM_S8_UINT) {
            if (!(attachments[j].aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT))
               mask &= ~0x7;
            if (!(attachments[j].aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT))
               mask &= ~0x8;
         }

         const struct tu_image_view *iview =
            cmd->state.framebuffer->attachments[a].attachment;

         ops->setup(cmd, cs, iview->image->vk_format, ROTATE_0, true, mask);
         ops->clear_value(cs, iview->image->vk_format, &attachments[j].clearValue);

         for (uint32_t i = 0; i < rect_count; i++) {
            ops->coords(cs, &rects[i].rect.offset, NULL, &rects[i].rect.extent);
            for (uint32_t layer = 0; layer < rects[i].layerCount; layer++) {
               ops->dst(cs, iview, rects[i].baseArrayLayer + layer);
               ops->run(cmd, cs);
            }
         }

         if (attachments[j].aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
            /* does not use CCU - flush
             * note: cache invalidate might be needed to, and just not covered by test cases
             */
            if (attachments[j].colorAttachment > 0)
               tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS, true);
         } else {
            /* sync color into depth */
            tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS, true);
            tu6_emit_event_write(cmd, cs, PC_CCU_INVALIDATE_DEPTH, false);
         }
   }
}

static void
tu_clear_sysmem_attachments(struct tu_cmd_buffer *cmd,
                            uint32_t attachment_count,
                            const VkClearAttachment *attachments,
                            uint32_t rect_count,
                            const VkClearRect *rects)
{
   /* the shader path here is special, it avoids changing MRT/etc state */
   const struct tu_render_pass *pass = cmd->state.pass;
   const struct tu_subpass *subpass = cmd->state.subpass;
   const uint32_t mrt_count = subpass->color_count;
   struct tu_cs *cs = &cmd->draw_cs;
   uint32_t clear_value[MAX_RTS][4];
   float z_clear_val = 0.0f;
   uint8_t s_clear_val = 0;
   uint32_t clear_rts = 0, num_rts = 0, b;
   bool z_clear = false;
   bool s_clear = false;
   uint32_t max_samples = 1;

   for (uint32_t i = 0; i < attachment_count; i++) {
      uint32_t a;
      if (attachments[i].aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
         uint32_t c = attachments[i].colorAttachment;
         a = subpass->color_attachments[c].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;

         clear_rts |= 1 << c;
         memcpy(clear_value[c], &attachments[i].clearValue, 4 * sizeof(uint32_t));
      } else {
         a = subpass->depth_stencil_attachment.attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;

         if (attachments[i].aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
            z_clear = true;
            z_clear_val = attachments[i].clearValue.depthStencil.depth;
         }

         if (attachments[i].aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
            s_clear = true;
            s_clear_val = attachments[i].clearValue.depthStencil.stencil & 0xff;
         }
      }

      max_samples = MAX2(max_samples, pass->attachments[a].samples);
   }

   /* prefer to use 2D path for clears
    * 2D can't clear separate depth/stencil and msaa, needs known framebuffer
    */
   if (max_samples == 1 && cmd->state.framebuffer) {
      tu_clear_sysmem_attachments_2d(cmd, attachment_count, attachments, rect_count, rects);
      return;
   }

   /* TODO: this path doesn't take into account multilayer rendering */

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_OUTPUT_CNTL0, 2);
   tu_cs_emit(cs, A6XX_SP_FS_OUTPUT_CNTL0_DEPTH_REGID(0xfc) |
                  A6XX_SP_FS_OUTPUT_CNTL0_SAMPMASK_REGID(0xfc) |
                  0xfc000000);
   tu_cs_emit(cs, A6XX_SP_FS_OUTPUT_CNTL1_MRT(mrt_count));

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_OUTPUT_REG(0), mrt_count);
   for (uint32_t i = 0; i < mrt_count; i++) {
      if (clear_rts & (1 << i))
         tu_cs_emit(cs, A6XX_SP_FS_OUTPUT_REG_REGID(num_rts++ * 4));
      else
         tu_cs_emit(cs, 0);
   }

   r3d_pipeline(cmd, cs, false, num_rts);

   tu_cs_emit_regs(cs,
                   A6XX_RB_FS_OUTPUT_CNTL0(),
                   A6XX_RB_FS_OUTPUT_CNTL1(.mrt = mrt_count));

   tu_cs_emit_regs(cs, A6XX_SP_BLEND_CNTL());
   tu_cs_emit_regs(cs, A6XX_RB_BLEND_CNTL(.independent_blend = 1, .sample_mask = 0xffff));
   tu_cs_emit_regs(cs, A6XX_RB_ALPHA_CONTROL());
   for (uint32_t i = 0; i < mrt_count; i++) {
      tu_cs_emit_regs(cs, A6XX_RB_MRT_CONTROL(i,
            .component_enable = COND(clear_rts & (1 << i), 0xf)));
   }

   tu_cs_emit_regs(cs, A6XX_RB_DEPTH_PLANE_CNTL());
   tu_cs_emit_regs(cs, A6XX_RB_DEPTH_CNTL(
         .z_enable = z_clear,
         .z_write_enable = z_clear,
         .zfunc = FUNC_ALWAYS));
   tu_cs_emit_regs(cs, A6XX_GRAS_SU_DEPTH_PLANE_CNTL());
   tu_cs_emit_regs(cs, A6XX_RB_STENCIL_CONTROL(
         .stencil_enable = s_clear,
         .func = FUNC_ALWAYS,
         .zpass = VK_STENCIL_OP_REPLACE));
   tu_cs_emit_regs(cs, A6XX_RB_STENCILMASK(.mask = 0xff));
   tu_cs_emit_regs(cs, A6XX_RB_STENCILWRMASK(.wrmask = 0xff));
   tu_cs_emit_regs(cs, A6XX_RB_STENCILREF(.ref = s_clear_val));

   tu_cs_emit_pkt7(cs, CP_LOAD_STATE6_FRAG, 3 + 4 * num_rts);
   tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(0) |
                  CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                  CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                  CP_LOAD_STATE6_0_STATE_BLOCK(SB6_FS_SHADER) |
                  CP_LOAD_STATE6_0_NUM_UNIT(num_rts));
   tu_cs_emit(cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
   tu_cs_emit(cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));
   for_each_bit(b, clear_rts)
      tu_cs_emit_array(cs, clear_value[b], 4);

   for (uint32_t i = 0; i < rect_count; i++) {
      r3d_coords_raw(cs, (float[]) {
         rects[i].rect.offset.x, rects[i].rect.offset.y,
         z_clear_val, 1.0f,
         rects[i].rect.offset.x + rects[i].rect.extent.width,
         rects[i].rect.offset.y + rects[i].rect.extent.height,
         z_clear_val, 1.0f
      });
      r3d_run(cmd, cs);
   }

   cmd->state.dirty |= TU_CMD_DIRTY_PIPELINE |
      TU_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK |
      TU_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK |
      TU_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE |
      TU_CMD_DIRTY_DYNAMIC_VIEWPORT |
      TU_CMD_DIRTY_DYNAMIC_SCISSOR;
}

/**
 * Pack a VkClearValue into a 128-bit buffer. format is respected except
 * for the component order.  The components are always packed in WZYX order,
 * because gmem is tiled and tiled formats always have WZYX swap
 */
static void
pack_gmem_clear_value(const VkClearValue *val, VkFormat format, uint32_t buf[4])
{
   const struct util_format_description *desc = vk_format_description(format);

   switch (format) {
   case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
      buf[0] = float3_to_r11g11b10f(val->color.float32);
      return;
   case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
      buf[0] = float3_to_rgb9e5(val->color.float32);
      return;
   default:
      break;
   }

   assert(desc && desc->layout == UTIL_FORMAT_LAYOUT_PLAIN);

   /* S8_UINT is special and has no depth */
   const int max_components =
      format == VK_FORMAT_S8_UINT ? 2 : desc->nr_channels;

   int buf_offset = 0;
   int bit_shift = 0;
   for (int comp = 0; comp < max_components; comp++) {
      const struct util_format_channel_description *ch =
         tu_get_format_channel_description(desc, comp);
      if (!ch) {
         assert((format == VK_FORMAT_S8_UINT && comp == 0) ||
                (format == VK_FORMAT_X8_D24_UNORM_PACK32 && comp == 1));
         continue;
      }

      union tu_clear_component_value v = tu_get_clear_component_value(
         val, comp, desc->colorspace);

      /* move to the next uint32_t when there is not enough space */
      assert(ch->size <= 32);
      if (bit_shift + ch->size > 32) {
         buf_offset++;
         bit_shift = 0;
      }

      if (bit_shift == 0)
         buf[buf_offset] = 0;

      buf[buf_offset] |= tu_pack_clear_component_value(v, ch) << bit_shift;
      bit_shift += ch->size;
   }
}

static void
tu_emit_clear_gmem_attachment(struct tu_cmd_buffer *cmd,
                              struct tu_cs *cs,
                              uint32_t attachment,
                              uint8_t component_mask,
                              const VkClearValue *value)
{
   VkFormat vk_format = cmd->state.pass->attachments[attachment].format;
   /* note: component_mask is 0x7 for depth and 0x8 for stencil
    * because D24S8 is cleared with AS_R8G8B8A8 format
    */

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_DST_INFO, 1);
   tu_cs_emit(cs, A6XX_RB_BLIT_DST_INFO_COLOR_FORMAT(tu6_base_format(vk_format)));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_INFO, 1);
   tu_cs_emit(cs, A6XX_RB_BLIT_INFO_GMEM | A6XX_RB_BLIT_INFO_CLEAR_MASK(component_mask));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_BASE_GMEM, 1);
   tu_cs_emit(cs, cmd->state.pass->attachments[attachment].gmem_offset);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_UNKNOWN_88D0, 1);
   tu_cs_emit(cs, 0);

   uint32_t clear_vals[4] = {};
   pack_gmem_clear_value(value, vk_format, clear_vals);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_CLEAR_COLOR_DW0, 4);
   tu_cs_emit_array(cs, clear_vals, 4);

   tu6_emit_event_write(cmd, cs, BLIT, false);
}

static void
tu_clear_gmem_attachments(struct tu_cmd_buffer *cmd,
                          uint32_t attachment_count,
                          const VkClearAttachment *attachments,
                          uint32_t rect_count,
                          const VkClearRect *rects)
{
   const struct tu_subpass *subpass = cmd->state.subpass;
   struct tu_cs *cs = &cmd->draw_cs;

   /* TODO: swap the loops for smaller cmdstream */
   for (unsigned i = 0; i < rect_count; i++) {
      unsigned x1 = rects[i].rect.offset.x;
      unsigned y1 = rects[i].rect.offset.y;
      unsigned x2 = x1 + rects[i].rect.extent.width - 1;
      unsigned y2 = y1 + rects[i].rect.extent.height - 1;

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_SCISSOR_TL, 2);
      tu_cs_emit(cs, A6XX_RB_BLIT_SCISSOR_TL_X(x1) | A6XX_RB_BLIT_SCISSOR_TL_Y(y1));
      tu_cs_emit(cs, A6XX_RB_BLIT_SCISSOR_BR_X(x2) | A6XX_RB_BLIT_SCISSOR_BR_Y(y2));

      for (unsigned j = 0; j < attachment_count; j++) {
         uint32_t a;
         if (attachments[j].aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
            a = subpass->color_attachments[attachments[j].colorAttachment].attachment;
         else
            a = subpass->depth_stencil_attachment.attachment;

         if (a == VK_ATTACHMENT_UNUSED)
               continue;

         unsigned clear_mask = 0xf;
         if (cmd->state.pass->attachments[a].format == VK_FORMAT_D24_UNORM_S8_UINT) {
            if (!(attachments[j].aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT))
               clear_mask &= ~0x7;
            if (!(attachments[j].aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT))
               clear_mask &= ~0x8;
         }

         tu_emit_clear_gmem_attachment(cmd, cs, a, clear_mask,
                                       &attachments[j].clearValue);
      }
   }
}

void
tu_CmdClearAttachments(VkCommandBuffer commandBuffer,
                       uint32_t attachmentCount,
                       const VkClearAttachment *pAttachments,
                       uint32_t rectCount,
                       const VkClearRect *pRects)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_GMEM);
   tu_clear_gmem_attachments(cmd, attachmentCount, pAttachments, rectCount, pRects);
   tu_cond_exec_end(cs);

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_SYSMEM);
   tu_clear_sysmem_attachments(cmd, attachmentCount, pAttachments, rectCount, pRects);
   tu_cond_exec_end(cs);
}

void
tu_clear_sysmem_attachment(struct tu_cmd_buffer *cmd,
                           struct tu_cs *cs,
                           uint32_t a,
                           const VkRenderPassBeginInfo *info)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_image_view *iview = fb->attachments[a].attachment;
   const struct tu_render_pass_attachment *attachment =
      &cmd->state.pass->attachments[a];
   uint8_t mask = 0;

   if (attachment->clear_mask == VK_IMAGE_ASPECT_COLOR_BIT)
      mask = 0xf;
   if (attachment->clear_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
      mask |= 0x7;
   if (attachment->clear_mask & VK_IMAGE_ASPECT_STENCIL_BIT)
      mask |= 0x8;

   if (!mask)
      return;

   const struct blit_ops *ops = &r2d_ops;
   if (attachment->samples > 1)
      ops = &r3d_ops;

   ops->setup(cmd, cs, attachment->format, ROTATE_0, true, mask);
   ops->coords(cs, &info->renderArea.offset, NULL, &info->renderArea.extent);
   ops->clear_value(cs, attachment->format, &info->pClearValues[a]);

   for (uint32_t i = 0; i < fb->layers; i++) {
      ops->dst(cs, iview, i);
      ops->run(cmd, cs);
   }
}

void
tu_clear_gmem_attachment(struct tu_cmd_buffer *cmd,
                         struct tu_cs *cs,
                         uint32_t a,
                         const VkRenderPassBeginInfo *info)
{
   const struct tu_render_pass_attachment *attachment =
      &cmd->state.pass->attachments[a];
   unsigned clear_mask = 0;

   if (attachment->clear_mask == VK_IMAGE_ASPECT_COLOR_BIT)
      clear_mask = 0xf;
   if (attachment->clear_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
      clear_mask |= 0x7;
   if (attachment->clear_mask & VK_IMAGE_ASPECT_STENCIL_BIT)
      clear_mask |= 0x8;

   if (!clear_mask)
      return;

   tu_cs_emit_regs(cs, A6XX_RB_MSAA_CNTL(tu_msaa_samples(attachment->samples)));

   tu_emit_clear_gmem_attachment(cmd, cs, a, clear_mask,
                                 &info->pClearValues[a]);
}

static void
tu_emit_blit(struct tu_cmd_buffer *cmd,
             struct tu_cs *cs,
             const struct tu_image_view *iview,
             const struct tu_render_pass_attachment *attachment,
             bool resolve)
{
   tu_cs_emit_regs(cs,
                   A6XX_RB_MSAA_CNTL(tu_msaa_samples(attachment->samples)));

   tu_cs_emit_regs(cs, A6XX_RB_BLIT_INFO(
      .unk0 = !resolve,
      .gmem = !resolve,
      /* "integer" bit disables msaa resolve averaging */
      .integer = vk_format_is_int(attachment->format)));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_DST_INFO, 4);
   tu_cs_emit(cs, iview->RB_BLIT_DST_INFO);
   tu_cs_image_ref_2d(cs, iview, 0, false);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_FLAG_DST_LO, 3);
   tu_cs_image_flag_ref(cs, iview, 0);

   tu_cs_emit_regs(cs,
                   A6XX_RB_BLIT_BASE_GMEM(attachment->gmem_offset));

   tu6_emit_event_write(cmd, cs, BLIT, false);
}

static bool
blit_can_resolve(VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);

   /* blit event can only do resolve for simple cases:
    * averaging samples as unsigned integers or choosing only one sample
    */
   if (vk_format_is_snorm(format) || vk_format_is_srgb(format))
      return false;

   /* can't do formats with larger channel sizes
    * note: this includes all float formats
    * note2: single channel integer formats seem OK
    */
   if (desc->channel[0].size > 10)
      return false;

   switch (format) {
   /* for unknown reasons blit event can't msaa resolve these formats when tiled
    * likely related to these formats having different layout from other cpp=2 formats
    */
   case VK_FORMAT_R8G8_UNORM:
   case VK_FORMAT_R8G8_UINT:
   case VK_FORMAT_R8G8_SINT:
   /* TODO: this one should be able to work? */
   case VK_FORMAT_D24_UNORM_S8_UINT:
      return false;
   default:
      break;
   }

   return true;
}

void
tu_load_gmem_attachment(struct tu_cmd_buffer *cmd,
                        struct tu_cs *cs,
                        uint32_t a,
                        bool force_load)
{
   const struct tu_image_view *iview =
      cmd->state.framebuffer->attachments[a].attachment;
   const struct tu_render_pass_attachment *attachment =
      &cmd->state.pass->attachments[a];

   if (attachment->load || force_load)
      tu_emit_blit(cmd, cs, iview, attachment, false);
}

void
tu_store_gmem_attachment(struct tu_cmd_buffer *cmd,
                         struct tu_cs *cs,
                         uint32_t a,
                         uint32_t gmem_a)
{
   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;
   const VkRect2D *render_area = &tiling->render_area;
   struct tu_render_pass_attachment *dst = &cmd->state.pass->attachments[a];
   struct tu_image_view *iview = cmd->state.framebuffer->attachments[a].attachment;
   struct tu_render_pass_attachment *src = &cmd->state.pass->attachments[gmem_a];

   if (!dst->store)
      return;

   uint32_t x1 = render_area->offset.x;
   uint32_t y1 = render_area->offset.y;
   uint32_t x2 = x1 + render_area->extent.width;
   uint32_t y2 = y1 + render_area->extent.height;
   /* x2/y2 can be unaligned if equal to the size of the image,
    * since it will write into padding space
    * the one exception is linear levels which don't have the
    * required y padding in the layout (except for the last level)
    */
   bool need_y2_align =
      y2 != iview->extent.height || iview->need_y2_align;

   bool unaligned =
      x1 % GMEM_ALIGN_W || (x2 % GMEM_ALIGN_W && x2 != iview->extent.width) ||
      y1 % GMEM_ALIGN_H || (y2 % GMEM_ALIGN_H && need_y2_align);

   /* use fast path when render area is aligned, except for unsupported resolve cases */
   if (!unaligned && (a == gmem_a || blit_can_resolve(dst->format))) {
      tu_emit_blit(cmd, cs, iview, src, true);
      return;
   }

   if (dst->samples > 1) {
      /* I guess we need to use shader path in this case?
       * need a testcase which fails because of this
       */
      tu_finishme("unaligned store of msaa attachment\n");
      return;
   }

   r2d_setup_common(cmd, cs, dst->format, ROTATE_0, false, 0xf, true);
   r2d_dst(cs, iview, 0);
   r2d_coords(cs, &render_area->offset, &render_area->offset, &render_area->extent);

   tu_cs_emit_regs(cs,
                   A6XX_SP_PS_2D_SRC_INFO(
                      .color_format = tu6_format_texture(src->format, TILE6_2).fmt,
                      .tile_mode = TILE6_2,
                      .srgb = vk_format_is_srgb(src->format),
                      .samples = tu_msaa_samples(src->samples),
                      .samples_average = !vk_format_is_int(src->format),
                      .unk20 = 1,
                      .unk22 = 1),
                   /* note: src size does not matter when not scaling */
                   A6XX_SP_PS_2D_SRC_SIZE( .width = 0x3fff, .height = 0x3fff),
                   A6XX_SP_PS_2D_SRC_LO(cmd->device->physical_device->gmem_base + src->gmem_offset),
                   A6XX_SP_PS_2D_SRC_HI(),
                   A6XX_SP_PS_2D_SRC_PITCH(.pitch = tiling->tile0.extent.width * src->cpp));

   /* sync GMEM writes with CACHE */
   tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE, false);

   tu_cs_emit_pkt7(cs, CP_BLIT, 1);
   tu_cs_emit(cs, CP_BLIT_0_OP(BLIT_OP_SCALE));

   /* TODO: flushing with barriers instead of blindly always flushing */
   tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS, true);
   tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_DEPTH_TS, true);
   tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE, false);
}
