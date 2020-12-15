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
#include "util/half_float.h"

static uint32_t
tu_pack_float32_for_unorm(float val, int bits)
{
   return _mesa_lroundevenf(CLAMP(val, 0.0f, 1.0f) * (float) ((1 << bits) - 1));
}

/* r2d_ = BLIT_OP_SCALE operations */

static enum a6xx_2d_ifmt
format_to_ifmt(VkFormat format)
{
   if (format == VK_FORMAT_D24_UNORM_S8_UINT ||
       format == VK_FORMAT_X8_D24_UNORM_PACK32)
      return R2D_UNORM8;

   /* get_component_bits doesn't work with depth/stencil formats: */
   if (format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT)
      return R2D_FLOAT32;
   if (format == VK_FORMAT_S8_UINT)
      return R2D_INT8;

   /* use the size of the red channel to find the corresponding "ifmt" */
   bool is_int = vk_format_is_int(format);
   switch (vk_format_get_component_bits(format, UTIL_FORMAT_COLORSPACE_RGB, PIPE_SWIZZLE_X)) {
   case 4: case 5: case 8:
      return is_int ? R2D_INT8 : R2D_UNORM8;
   case 10: case 11:
      return is_int ? R2D_INT16 : R2D_FLOAT16;
   case 16:
      if (vk_format_is_float(format))
         return R2D_FLOAT16;
      return is_int ? R2D_INT16 : R2D_FLOAT32;
   case 32:
      return is_int ? R2D_INT32 : R2D_FLOAT32;
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
                   A6XX_GRAS_2D_SRC_TL_X(src->x),
                   A6XX_GRAS_2D_SRC_BR_X(src->x + extent->width - 1),
                   A6XX_GRAS_2D_SRC_TL_Y(src->y),
                   A6XX_GRAS_2D_SRC_BR_Y(src->y + extent->height - 1));
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
      enum a6xx_2d_ifmt ifmt = format_to_ifmt(format);

      assert(desc && (desc->layout == UTIL_FORMAT_LAYOUT_PLAIN ||
                      format == VK_FORMAT_B10G11R11_UFLOAT_PACK32));

      for (unsigned i = 0; i < desc->nr_channels; i++) {
         const struct util_format_channel_description *ch = &desc->channel[i];
         if (ifmt == R2D_UNORM8) {
            float linear = val->color.float32[i];
            if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB && i < 3)
               linear = util_format_linear_to_srgb_float(val->color.float32[i]);

            if (ch->type == UTIL_FORMAT_TYPE_SIGNED)
               clear_value[i] = _mesa_lroundevenf(CLAMP(linear, -1.0f, 1.0f) * 127.0f);
            else
               clear_value[i] = tu_pack_float32_for_unorm(linear, 8);
         } else if (ifmt == R2D_FLOAT16) {
            clear_value[i] = _mesa_float_to_half(val->color.float32[i]);
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
        VkFilter filter)
{
   uint32_t src_info = iview->SP_PS_2D_SRC_INFO;
   if (filter != VK_FILTER_NEAREST)
      src_info |= A6XX_SP_PS_2D_SRC_INFO_FILTER;

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_PS_2D_SRC_INFO, 5);
   tu_cs_emit(cs, src_info);
   tu_cs_emit(cs, iview->SP_PS_2D_SRC_SIZE);
   tu_cs_image_ref_2d(cs, iview, layer, true);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_PS_2D_SRC_FLAGS_LO, 3);
   tu_cs_image_flag_ref(cs, iview, layer);
}

static void
r2d_src_stencil(struct tu_cmd_buffer *cmd,
                struct tu_cs *cs,
                const struct tu_image_view *iview,
                uint32_t layer,
                VkFilter filter)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_SP_PS_2D_SRC_INFO, 5);
   tu_cs_emit(cs, tu_image_view_stencil(iview, SP_PS_2D_SRC_INFO) & ~A6XX_SP_PS_2D_SRC_INFO_FLAGS);
   tu_cs_emit(cs, iview->SP_PS_2D_SRC_SIZE);
   tu_cs_emit_qw(cs, iview->stencil_base_addr + iview->stencil_layer_size * layer);
   /* SP_PS_2D_SRC_PITCH has shifted pitch field */
   tu_cs_emit(cs, iview->stencil_PITCH << 9);
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
   tu_cs_emit_pkt4(cs, REG_A6XX_RB_2D_DST_INFO, 4);
   tu_cs_emit(cs, iview->RB_2D_DST_INFO);
   tu_cs_image_ref_2d(cs, iview, layer, false);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_2D_DST_FLAGS_LO, 3);
   tu_cs_image_flag_ref(cs, iview, layer);
}

static void
r2d_dst_stencil(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_RB_2D_DST_INFO, 4);
   tu_cs_emit(cs, tu_image_view_stencil(iview, RB_2D_DST_INFO) & ~A6XX_RB_2D_DST_INFO_FLAGS);
   tu_cs_emit_qw(cs, iview->stencil_base_addr + iview->stencil_layer_size * layer);
   tu_cs_emit(cs, iview->stencil_PITCH);
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
                   A6XX_RB_2D_DST_PITCH(pitch));
}

static void
r2d_setup_common(struct tu_cmd_buffer *cmd,
                 struct tu_cs *cs,
                 VkFormat vk_format,
                 VkImageAspectFlags aspect_mask,
                 unsigned blit_param,
                 bool clear,
                 bool ubwc,
                 bool scissor)
{
   enum a6xx_format format = tu6_base_format(vk_format);
   enum a6xx_2d_ifmt ifmt = format_to_ifmt(vk_format);
   uint32_t unknown_8c01 = 0;

   if ((vk_format == VK_FORMAT_D24_UNORM_S8_UINT ||
        vk_format == VK_FORMAT_X8_D24_UNORM_PACK32) && ubwc) {
      format = FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8;
   }

   /* note: the only format with partial clearing is D24S8 */
   if (vk_format == VK_FORMAT_D24_UNORM_S8_UINT) {
      /* preserve stencil channel */
      if (aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT)
         unknown_8c01 = 0x08000041;
      /* preserve depth channels */
      if (aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT)
         unknown_8c01 = 0x00084001;
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_2D_UNKNOWN_8C01, 1);
   tu_cs_emit(cs, unknown_8c01);

   uint32_t blit_cntl = A6XX_RB_2D_BLIT_CNTL(
         .scissor = scissor,
         .rotate = blit_param,
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

   tu_cs_emit_regs(cs, A6XX_SP_2D_DST_FORMAT(
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
          VkImageAspectFlags aspect_mask,
          unsigned blit_param,
          bool clear,
          bool ubwc)
{
   tu_emit_cache_flush_ccu(cmd, cs, TU_CMD_CCU_SYSMEM);

   r2d_setup_common(cmd, cs, vk_format, aspect_mask, blit_param, clear, ubwc, false);
}

static void
r2d_teardown(struct tu_cmd_buffer *cmd,
             struct tu_cs *cs)
{
   /* nothing to do here */
}

static void
r2d_run(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   tu_cs_emit_pkt7(cs, CP_BLIT, 1);
   tu_cs_emit(cs, CP_BLIT_0_OP(BLIT_OP_SCALE));
}

/* r3d_ = shader path operations */

void
tu_init_clear_blit_shaders(struct tu6_global *global)
{
#define MOV(args...) { .cat1 = { .opc_cat = 1, .src_type = TYPE_S32, .dst_type = TYPE_S32, args } }
#define CAT2(op, args...) { .cat2 = { .opc_cat = 2, .opc = (op) & 63, .full = 1, args } }
#define CAT3(op, args...) { .cat3 = { .opc_cat = 3, .opc = (op) & 63, args } }

   static const instr_t vs_code[] = {
      /* r0.xyz = r0.w ? c1.xyz : c0.xyz
       * r1.xy = r0.w ? c1.zw : c0.zw
       * r1.z = c2.x (for z_scale path)
       * r0.w = 1.0f
       */
      CAT3(OPC_SEL_B32, .repeat = 2, .dst = 0,
         .c1 = {.src1_c = 1, .src1 = 4}, .src1_r = 1,
         .src2 = 3,
         .c2 = {.src3_c = 1, .dummy = 1, .src3 = 0}),
      CAT3(OPC_SEL_B32, .repeat = 1, .dst = 4,
         .c1 = {.src1_c = 1, .src1 = 6}, .src1_r = 1,
         .src2 = 3,
         .c2 = {.src3_c = 1, .dummy = 1, .src3 = 2}),
      MOV(.dst = 6, .src_c = 1, .src = 8 ),
      MOV(.dst = 3, .src_im = 1, .fim_val = 1.0f ),
      { .cat0 = { .opc = OPC_END } },
   };

   static const instr_t fs_blit[] = {
      /* " bary.f (ei)r63.x, 0, r0.x" note the blob doesn't have this in its
       * blit path (its not clear what allows it to not have it)
       */
      CAT2(OPC_BARY_F, .ei = 1, .full = 1, .dst = 63 * 4, .src1_im = 1),
      { .cat0 = { .opc = OPC_END } },
   };

   static const instr_t fs_blit_zscale[] = {
      /* (rpt2)bary.f (ei)r0.x, (r)0, r0.x
       * (rpt5)nop
       * sam.3d (s32)(xyzw)r0.x, r0.x, s#0, t#0
       */
      CAT2(OPC_BARY_F, .ei = 1, .full = 1, .dst = 0, .src1_im = 1, .src1 = 0, .repeat = 2, .src1_r = 1),
      { .cat0 = { .repeat = 5 } },
      { .cat5 = { .opc_cat = 5, .opc = OPC_SAM & 31, .dst = 0, .wrmask = 0xf, .type = TYPE_S32,
         .is_3d = 1, .norm = { .full = 1, .src1 = 0 } } },
      { .cat0 = { .opc = OPC_END } },
   };

   memcpy(&global->shaders[GLOBAL_SH_VS], vs_code, sizeof(vs_code));
   memcpy(&global->shaders[GLOBAL_SH_FS_BLIT], fs_blit, sizeof(fs_blit));
   memcpy(&global->shaders[GLOBAL_SH_FS_BLIT_ZSCALE], fs_blit_zscale, sizeof(fs_blit_zscale));

   for (uint32_t num_rts = 0; num_rts <= MAX_RTS; num_rts++) {
      instr_t *code = global->shaders[GLOBAL_SH_FS_CLEAR0 + num_rts];
      for (uint32_t i = 0; i < num_rts; i++) {
         /* (rpt3)mov.s32s32 r0.x, (r)c[i].x */
         *code++ = (instr_t) MOV(.repeat = 3, .dst = i * 4, .src_c = 1, .src_r = 1, .src = i * 4);
      }
      *code++ = (instr_t) { .cat0 = { .opc = OPC_END } };
   }
}

static void
r3d_common(struct tu_cmd_buffer *cmd, struct tu_cs *cs, bool blit, uint32_t num_rts,
           bool layered_clear, bool z_scale)
{
   struct ir3_const_state dummy_const_state = {};
   struct ir3_shader dummy_shader = {};

   struct ir3_shader_variant vs = {
      .type = MESA_SHADER_VERTEX,
      .instrlen = 1,
      .constlen = 4,
      .info.max_reg = 1,
      .inputs_count = 1,
      .inputs[0] = {
         .slot = SYSTEM_VALUE_VERTEX_ID,
         .regid = regid(0, 3),
         .sysval = true,
      },
      .outputs_count = blit ? 2 : 1,
      .outputs[0] = {
         .slot = VARYING_SLOT_POS,
         .regid = regid(0, 0),
      },
      .outputs[1] = {
         .slot = VARYING_SLOT_VAR0,
         .regid = regid(1, 0),
      },
      .shader = &dummy_shader,
      .const_state = &dummy_const_state,
   };
   if (layered_clear) {
      vs.outputs[1].slot = VARYING_SLOT_LAYER;
      vs.outputs[1].regid = regid(1, 1);
      vs.outputs_count = 2;
   }

   struct ir3_shader_variant fs = {
      .type = MESA_SHADER_FRAGMENT,
      .instrlen = 1, /* max of 9 instructions with num_rts = 8 */
      .constlen = align(num_rts, 4),
      .info.max_reg = MAX2(num_rts, 1) - 1,
      .total_in = blit ? 2 : 0,
      .num_samp = blit ? 1 : 0,
      .inputs_count = blit ? 2 : 0,
      .inputs[0] = {
         .slot = VARYING_SLOT_VAR0,
         .inloc = 0,
         .compmask = 3,
         .bary = true,
      },
      .inputs[1] = {
         .slot = SYSTEM_VALUE_BARYCENTRIC_PERSP_PIXEL,
         .regid = regid(0, 0),
         .sysval = 1,
      },
      .num_sampler_prefetch = blit ? 1 : 0,
      .sampler_prefetch[0] = {
         .src = 0,
         .wrmask = 0xf,
         .cmd = 4,
      },
      .shader = &dummy_shader,
      .const_state = &dummy_const_state,
   };

   enum global_shader fs_id = GLOBAL_SH_FS_BLIT;

   if (!blit)
      fs_id = GLOBAL_SH_FS_CLEAR0 + num_rts;

   /* z_scale blit path has an extra varying and doesn't use prefetch */
   if (z_scale) {
      assert(blit);
      fs.total_in = 3;
      fs.num_sampler_prefetch = 0;
      fs.inputs[0].compmask = 7;
      fs_id = GLOBAL_SH_FS_BLIT_ZSCALE;
   }

   tu_cs_emit_regs(cs, A6XX_HLSQ_INVALIDATE_CMD(
         .vs_state = true,
         .hs_state = true,
         .ds_state = true,
         .gs_state = true,
         .fs_state = true,
         .cs_state = true,
         .gfx_ibo = true,
         .cs_ibo = true,
         .gfx_shared_const = true,
         .gfx_bindless = 0x1f,
         .cs_bindless = 0x1f));

   struct tu_pvtmem_config pvtmem = {};
   tu6_emit_xs_config(cs, MESA_SHADER_VERTEX, &vs, &pvtmem, global_iova(cmd, shaders[GLOBAL_SH_VS]));
   tu6_emit_xs_config(cs, MESA_SHADER_TESS_CTRL, NULL, &pvtmem, 0);
   tu6_emit_xs_config(cs, MESA_SHADER_TESS_EVAL, NULL, &pvtmem, 0);
   tu6_emit_xs_config(cs, MESA_SHADER_GEOMETRY, NULL, &pvtmem, 0);
   tu6_emit_xs_config(cs, MESA_SHADER_FRAGMENT, &fs, &pvtmem, global_iova(cmd, shaders[fs_id]));

   tu_cs_emit_regs(cs, A6XX_PC_PRIMITIVE_CNTL_0());
   tu_cs_emit_regs(cs, A6XX_VFD_CONTROL_0());

   /* Copy what the blob does here. This will emit an extra 0x3f
    * CP_EVENT_WRITE when multiview is disabled. I'm not exactly sure what
    * this is working around yet.
    */
   tu_cs_emit_pkt7(cs, CP_REG_WRITE, 3);
   tu_cs_emit(cs, CP_REG_WRITE_0_TRACKER(UNK_EVENT_WRITE));
   tu_cs_emit(cs, REG_A6XX_PC_MULTIVIEW_CNTL);
   tu_cs_emit(cs, 0);
   tu_cs_emit_regs(cs, A6XX_VFD_MULTIVIEW_CNTL());

   tu6_emit_vpc(cs, &vs, NULL, NULL, NULL, &fs, 0, false);

   /* REPL_MODE for varying with RECTLIST (2 vertices only) */
   tu_cs_emit_regs(cs, A6XX_VPC_VARYING_INTERP_MODE(0, 0));
   tu_cs_emit_regs(cs, A6XX_VPC_VARYING_PS_REPL_MODE(0, 2 << 2 | 1 << 0));

   tu6_emit_fs_inputs(cs, &fs);

   tu_cs_emit_regs(cs,
                   A6XX_GRAS_CL_CNTL(
                      .persp_division_disable = 1,
                      .vp_xform_disable = 1,
                      .vp_clip_code_ignore = 1,
                      .clip_disable = 1));
   tu_cs_emit_regs(cs, A6XX_GRAS_SU_CNTL()); // XXX msaa enable?

   tu_cs_emit_regs(cs, A6XX_PC_RASTER_CNTL());
   tu_cs_emit_regs(cs, A6XX_VPC_UNKNOWN_9107());

   tu_cs_emit_regs(cs,
                   A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL(0, .x = 0, .y = 0),
                   A6XX_GRAS_SC_VIEWPORT_SCISSOR_BR(0, .x = 0x7fff, .y = 0x7fff));
   tu_cs_emit_regs(cs,
                   A6XX_GRAS_SC_SCREEN_SCISSOR_TL(0, .x = 0, .y = 0),
                   A6XX_GRAS_SC_SCREEN_SCISSOR_BR(0, .x = 0x7fff, .y = 0x7fff));

   tu_cs_emit_regs(cs,
                   A6XX_VFD_INDEX_OFFSET(),
                   A6XX_VFD_INSTANCE_START_OFFSET());
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

/* z coordinate for "z scale" blit path which uses a 3d texture */
static void
r3d_coord_z(struct tu_cs *cs, float z)
{
   tu_cs_emit_pkt7(cs, CP_LOAD_STATE6_GEOM, 3 + 4);
   tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(2) |
                  CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                  CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                  CP_LOAD_STATE6_0_STATE_BLOCK(SB6_VS_SHADER) |
                  CP_LOAD_STATE6_0_NUM_UNIT(1));
   tu_cs_emit(cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
   tu_cs_emit(cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));
   tu_cs_emit(cs, fui(z));
   tu_cs_emit(cs, 0);
   tu_cs_emit(cs, 0);
   tu_cs_emit(cs, 0);
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
               VkFilter filter)
{
   struct tu_cs_memory texture = { };
   VkResult result = tu_cs_alloc(&cmd->sub_cs,
                                 2, /* allocate space for a sampler too */
                                 A6XX_TEX_CONST_DWORDS, &texture);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

   memcpy(texture.map, tex_const, A6XX_TEX_CONST_DWORDS * 4);

   /* patch addresses for layer offset */
   *(uint64_t*) (texture.map + 4) += offset_base;
   uint64_t ubwc_addr = (texture.map[7] | (uint64_t) texture.map[8] << 32) + offset_ubwc;
   texture.map[7] = ubwc_addr;
   texture.map[8] = ubwc_addr >> 32;

   texture.map[A6XX_TEX_CONST_DWORDS + 0] =
      A6XX_TEX_SAMP_0_XY_MAG(tu6_tex_filter(filter, false)) |
      A6XX_TEX_SAMP_0_XY_MIN(tu6_tex_filter(filter, false)) |
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
        VkFilter filter)
{
   r3d_src_common(cmd, cs, iview->descriptor,
                  iview->layer_size * layer,
                  iview->ubwc_layer_size * layer,
                  filter);
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
      A6XX_TEX_CONST_2_PITCH(pitch) |
      A6XX_TEX_CONST_2_TYPE(A6XX_TEX_2D);
   desc[3] = 0;
   desc[4] = va;
   desc[5] = va >> 32;
   for (uint32_t i = 6; i < A6XX_TEX_CONST_DWORDS; i++)
      desc[i] = 0;

   r3d_src_common(cmd, cs, desc, 0, 0, VK_FILTER_NEAREST);
}

static void
r3d_dst(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer)
{
   tu6_emit_msaa(cs, iview->image->layout[0].nr_samples); /* TODO: move to setup */

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_BUF_INFO(0), 6);
   tu_cs_emit(cs, iview->RB_MRT_BUF_INFO);
   tu_cs_image_ref(cs, iview, layer);
   tu_cs_emit(cs, 0);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_FLAG_BUFFER(0), 3);
   tu_cs_image_flag_ref(cs, iview, layer);

   tu_cs_emit_regs(cs, A6XX_RB_RENDER_CNTL(.flag_mrts = iview->ubwc_enabled));
}

static void
r3d_dst_stencil(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer)
{
   tu6_emit_msaa(cs, iview->image->layout[0].nr_samples); /* TODO: move to setup */

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_BUF_INFO(0), 6);
   tu_cs_emit(cs, tu_image_view_stencil(iview, RB_MRT_BUF_INFO));
   tu_cs_image_stencil_ref(cs, iview, layer);
   tu_cs_emit(cs, 0);

   tu_cs_emit_regs(cs, A6XX_RB_RENDER_CNTL());
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

static uint8_t
aspect_write_mask(VkFormat vk_format, VkImageAspectFlags aspect_mask)
{
   uint8_t mask = 0xf;
   assert(aspect_mask);
   /* note: the only format with partial writing is D24S8,
    * clear/blit uses the _AS_R8G8B8A8 format to access it
    */
   if (vk_format == VK_FORMAT_D24_UNORM_S8_UINT) {
      if (aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT)
         mask = 0x7;
      if (aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT)
         mask = 0x8;
   }
   return mask;
}

static void
r3d_setup(struct tu_cmd_buffer *cmd,
          struct tu_cs *cs,
          VkFormat vk_format,
          VkImageAspectFlags aspect_mask,
          unsigned blit_param,
          bool clear,
          bool ubwc)
{
   enum a6xx_format format = tu6_base_format(vk_format);

   if ((vk_format == VK_FORMAT_D24_UNORM_S8_UINT ||
        vk_format == VK_FORMAT_X8_D24_UNORM_PACK32) && ubwc) {
      format = FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8;
   }

   if (!cmd->state.pass) {
      tu_emit_cache_flush_ccu(cmd, cs, TU_CMD_CCU_SYSMEM);
      tu6_emit_window_scissor(cs, 0, 0, 0x3fff, 0x3fff);
   }

   tu_cs_emit_regs(cs, A6XX_GRAS_BIN_CONTROL(.dword = 0xc00000));
   tu_cs_emit_regs(cs, A6XX_RB_BIN_CONTROL(.dword = 0xc00000));

   r3d_common(cmd, cs, !clear, clear ? 1 : 0, false, blit_param);

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
                        .color_format = format,
                        .color_sint = vk_format_is_sint(vk_format),
                        .color_uint = vk_format_is_uint(vk_format)));

   tu_cs_emit_regs(cs, A6XX_RB_MRT_CONTROL(0,
      .component_enable = aspect_write_mask(vk_format, aspect_mask)));
   tu_cs_emit_regs(cs, A6XX_RB_SRGB_CNTL(vk_format_is_srgb(vk_format)));
   tu_cs_emit_regs(cs, A6XX_SP_SRGB_CNTL(vk_format_is_srgb(vk_format)));

   tu_cs_emit_regs(cs, A6XX_GRAS_LRZ_CNTL(0));
   tu_cs_emit_regs(cs, A6XX_RB_LRZ_CNTL(0));

   if (cmd->state.predication_active) {
      tu_cs_emit_pkt7(cs, CP_DRAW_PRED_ENABLE_LOCAL, 1);
      tu_cs_emit(cs, 0);
   }
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
}

static void
r3d_teardown(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   if (cmd->state.predication_active) {
      tu_cs_emit_pkt7(cs, CP_DRAW_PRED_ENABLE_LOCAL, 1);
      tu_cs_emit(cs, 1);
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
        VkFilter filter);
   void (*src_buffer)(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                      VkFormat vk_format,
                      uint64_t va, uint32_t pitch,
                      uint32_t width, uint32_t height);
   void (*dst)(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer);
   void (*dst_buffer)(struct tu_cs *cs, VkFormat vk_format, uint64_t va, uint32_t pitch);
   void (*setup)(struct tu_cmd_buffer *cmd,
                 struct tu_cs *cs,
                 VkFormat vk_format,
                 VkImageAspectFlags aspect_mask,
                 unsigned blit_param, /* CmdBlitImage: rotation in 2D path and z scaling in 3D path */
                 bool clear,
                 bool ubwc);
   void (*run)(struct tu_cmd_buffer *cmd, struct tu_cs *cs);
   void (*teardown)(struct tu_cmd_buffer *cmd,
                    struct tu_cs *cs);
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
   .teardown = r2d_teardown,
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
   .teardown = r3d_teardown,
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

static VkFormat
copy_format(VkFormat format, VkImageAspectFlags aspect_mask, bool copy_buffer)
{
   if (vk_format_is_compressed(format)) {
      switch (vk_format_get_blocksize(format)) {
      case 1: return VK_FORMAT_R8_UINT;
      case 2: return VK_FORMAT_R16_UINT;
      case 4: return VK_FORMAT_R32_UINT;
      case 8: return VK_FORMAT_R32G32_UINT;
      case 16:return VK_FORMAT_R32G32B32A32_UINT;
      default:
         unreachable("unhandled format size");
      }
   }

   switch (format) {
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
      if (aspect_mask == VK_IMAGE_ASPECT_PLANE_1_BIT)
         return VK_FORMAT_R8G8_UNORM;
      /* fallthrough */
   case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
      return VK_FORMAT_R8_UNORM;
   case VK_FORMAT_D24_UNORM_S8_UINT:
      if (aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT && copy_buffer)
         return VK_FORMAT_R8_UNORM;
      /* fallthrough */
   default:
      return format;
   case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
      return VK_FORMAT_R32_UINT;
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      if (aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT)
         return VK_FORMAT_S8_UINT;
      assert(aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT);
      return VK_FORMAT_D32_SFLOAT;
   }
}

void
tu6_clear_lrz(struct tu_cmd_buffer *cmd,
              struct tu_cs *cs,
              struct tu_image *image,
              const VkClearValue *value)
{
   const struct blit_ops *ops = &r2d_ops;

   ops->setup(cmd, cs, VK_FORMAT_D16_UNORM, VK_IMAGE_ASPECT_DEPTH_BIT, 0, true, false);
   ops->clear_value(cs, VK_FORMAT_D16_UNORM, value);
   ops->dst_buffer(cs, VK_FORMAT_D16_UNORM,
                   image->bo->iova + image->bo_offset + image->lrz_offset,
                   image->lrz_pitch * 2);
   ops->coords(cs, &(VkOffset2D) {}, NULL, &(VkExtent2D) {image->lrz_pitch, image->lrz_height});
   ops->run(cmd, cs);
   ops->teardown(cmd, cs);
}

static void
tu_image_view_copy_blit(struct tu_image_view *iview,
                        struct tu_image *image,
                        VkFormat format,
                        const VkImageSubresourceLayers *subres,
                        uint32_t layer,
                        bool stencil_read,
                        bool z_scale)
{
   VkImageAspectFlags aspect_mask = subres->aspectMask;

   /* always use the AS_R8G8B8A8 format for these */
   if (format == VK_FORMAT_D24_UNORM_S8_UINT ||
       format == VK_FORMAT_X8_D24_UNORM_PACK32) {
      aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
   }

   tu_image_view_init(iview, &(VkImageViewCreateInfo) {
      .image = tu_image_to_handle(image),
      .viewType = z_scale ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D,
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
   }, false);
}

static void
tu_image_view_copy(struct tu_image_view *iview,
                   struct tu_image *image,
                   VkFormat format,
                   const VkImageSubresourceLayers *subres,
                   uint32_t layer,
                   bool stencil_read)
{
   format = copy_format(format, subres->aspectMask, false);
   tu_image_view_copy_blit(iview, image, format, subres, layer, stencil_read, false);
}

static void
tu_image_view_blit(struct tu_image_view *iview,
                   struct tu_image *image,
                   const VkImageSubresourceLayers *subres,
                   uint32_t layer)
{
   tu_image_view_copy_blit(iview, image, image->vk_format, subres, layer, false, false);
}

static void
tu6_blit_image(struct tu_cmd_buffer *cmd,
               struct tu_image *src_image,
               struct tu_image *dst_image,
               const VkImageBlit *info,
               VkFilter filter)
{
   const struct blit_ops *ops = &r3d_ops;
   struct tu_cs *cs = &cmd->cs;
   bool z_scale = false;
   uint32_t layers = info->dstOffsets[1].z - info->dstOffsets[0].z;

   /* 2D blit can't do rotation mirroring from just coordinates */
   static const enum a6xx_rotation rotate[2][2] = {
      {ROTATE_0, ROTATE_HFLIP},
      {ROTATE_VFLIP, ROTATE_180},
   };

   bool mirror_x = (info->srcOffsets[1].x < info->srcOffsets[0].x) !=
                   (info->dstOffsets[1].x < info->dstOffsets[0].x);
   bool mirror_y = (info->srcOffsets[1].y < info->srcOffsets[0].y) !=
                   (info->dstOffsets[1].y < info->dstOffsets[0].y);

   int32_t src0_z = info->srcOffsets[0].z;
   int32_t src1_z = info->srcOffsets[1].z;

   if ((info->srcOffsets[1].z - info->srcOffsets[0].z !=
        info->dstOffsets[1].z - info->dstOffsets[0].z) ||
       info->srcOffsets[1].z < info->srcOffsets[0].z) {
      z_scale = true;
   }

   if (info->dstOffsets[1].z < info->dstOffsets[0].z) {
      layers = info->dstOffsets[0].z - info->dstOffsets[1].z;
      src0_z = info->srcOffsets[1].z;
      src1_z = info->srcOffsets[0].z;
   }

   if (info->dstSubresource.layerCount > 1) {
      assert(layers <= 1);
      layers = info->dstSubresource.layerCount;
   }

   /* BC1_RGB_* formats need to have their last components overriden with 1
    * when sampling, which is normally handled with the texture descriptor
    * swizzle. The 2d path can't handle that, so use the 3d path.
    *
    * TODO: we could use RB_2D_BLIT_CNTL::MASK to make these formats work with
    * the 2d path.
    */

   unsigned blit_param = rotate[mirror_y][mirror_x];
   if (dst_image->layout[0].nr_samples > 1 ||
       src_image->vk_format == VK_FORMAT_BC1_RGB_UNORM_BLOCK ||
       src_image->vk_format == VK_FORMAT_BC1_RGB_SRGB_BLOCK ||
       filter == VK_FILTER_CUBIC_EXT ||
       z_scale) {
      ops = &r3d_ops;
      blit_param = z_scale;
   }

   /* use the right format in setup() for D32_S8
    * TODO: this probably should use a helper
    */
   VkFormat format = dst_image->vk_format;
   if (format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
      if (info->dstSubresource.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT)
         format = VK_FORMAT_D32_SFLOAT;
      else if (info->dstSubresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT)
         format = VK_FORMAT_S8_UINT;
      else
         unreachable("unexpected D32_S8 aspect mask in blit_image");
   }

   ops->setup(cmd, cs, format, info->dstSubresource.aspectMask,
              blit_param, false, dst_image->layout[0].ubwc);

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
         A6XX_GRAS_2D_SRC_TL_X(MIN2(info->srcOffsets[0].x, info->srcOffsets[1].x)),
         A6XX_GRAS_2D_SRC_BR_X(MAX2(info->srcOffsets[0].x, info->srcOffsets[1].x) - 1),
         A6XX_GRAS_2D_SRC_TL_Y(MIN2(info->srcOffsets[0].y, info->srcOffsets[1].y)),
         A6XX_GRAS_2D_SRC_BR_Y(MAX2(info->srcOffsets[0].y, info->srcOffsets[1].y) - 1));
   }

   struct tu_image_view dst, src;
   tu_image_view_blit(&dst, dst_image, &info->dstSubresource,
                      MIN2(info->dstOffsets[0].z, info->dstOffsets[1].z));

   if (z_scale) {
      tu_image_view_copy_blit(&src, src_image, src_image->vk_format,
                              &info->srcSubresource, 0, false, true);
      ops->src(cmd, cs, &src, 0, filter);
   } else {
      tu_image_view_blit(&src, src_image, &info->srcSubresource, info->srcOffsets[0].z);
   }

   for (uint32_t i = 0; i < layers; i++) {
      if (z_scale) {
         float t = ((float) i + 0.5f) / (float) layers;
         r3d_coord_z(cs, t * (src1_z - src0_z) + src0_z);
      } else {
         ops->src(cmd, cs, &src, i, filter);
      }
      ops->dst(cs, &dst, i);
      ops->run(cmd, cs);
   }

   ops->teardown(cmd, cs);
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

   for (uint32_t i = 0; i < regionCount; ++i) {
      /* can't blit both depth and stencil at once with D32_S8
       * TODO: more advanced 3D blit path to support it instead?
       */
      if (src_image->vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
          dst_image->vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
         VkImageBlit region = pRegions[i];
         uint32_t b;
         for_each_bit(b, pRegions[i].dstSubresource.aspectMask) {
            region.srcSubresource.aspectMask = BIT(b);
            region.dstSubresource.aspectMask = BIT(b);
            tu6_blit_image(cmd, src_image, dst_image, &region, filter);
         }
         continue;
      }
      tu6_blit_image(cmd, src_image, dst_image, pRegions + i, filter);
   }
}

static void
copy_compressed(VkFormat format,
                VkOffset3D *offset,
                VkExtent3D *extent,
                uint32_t *width,
                uint32_t *height)
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
   if (width)
      *width = DIV_ROUND_UP(*width, block_width);
   if (height)
      *height = DIV_ROUND_UP(*height, block_height);
}

static void
tu_copy_buffer_to_image(struct tu_cmd_buffer *cmd,
                        struct tu_buffer *src_buffer,
                        struct tu_image *dst_image,
                        const VkBufferImageCopy *info)
{
   struct tu_cs *cs = &cmd->cs;
   uint32_t layers = MAX2(info->imageExtent.depth, info->imageSubresource.layerCount);
   VkFormat src_format =
      copy_format(dst_image->vk_format, info->imageSubresource.aspectMask, true);
   const struct blit_ops *ops = &r2d_ops;

   /* special case for buffer to stencil */
   if (dst_image->vk_format == VK_FORMAT_D24_UNORM_S8_UINT &&
       info->imageSubresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT) {
      ops = &r3d_ops;
   }

   /* TODO: G8_B8R8_2PLANE_420_UNORM Y plane has different hardware format,
    * which matters for UBWC. buffer_to_image/etc can fail because of this
    */

   VkOffset3D offset = info->imageOffset;
   VkExtent3D extent = info->imageExtent;
   uint32_t src_width = info->bufferRowLength ?: extent.width;
   uint32_t src_height = info->bufferImageHeight ?: extent.height;

   copy_compressed(dst_image->vk_format, &offset, &extent, &src_width, &src_height);

   uint32_t pitch = src_width * vk_format_get_blocksize(src_format);
   uint32_t layer_size = src_height * pitch;

   ops->setup(cmd, cs,
              copy_format(dst_image->vk_format, info->imageSubresource.aspectMask, false),
              info->imageSubresource.aspectMask, 0, false, dst_image->layout[0].ubwc);

   struct tu_image_view dst;
   tu_image_view_copy(&dst, dst_image, dst_image->vk_format, &info->imageSubresource, offset.z, false);

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

   ops->teardown(cmd, cs);
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
   VkFormat dst_format =
      copy_format(src_image->vk_format, info->imageSubresource.aspectMask, true);
   bool stencil_read = false;

   if (src_image->vk_format == VK_FORMAT_D24_UNORM_S8_UINT &&
       info->imageSubresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT) {
      stencil_read = true;
   }

   const struct blit_ops *ops = stencil_read ? &r3d_ops : &r2d_ops;
   VkOffset3D offset = info->imageOffset;
   VkExtent3D extent = info->imageExtent;
   uint32_t dst_width = info->bufferRowLength ?: extent.width;
   uint32_t dst_height = info->bufferImageHeight ?: extent.height;

   copy_compressed(src_image->vk_format, &offset, &extent, &dst_width, &dst_height);

   uint32_t pitch = dst_width * vk_format_get_blocksize(dst_format);
   uint32_t layer_size = pitch * dst_height;

   ops->setup(cmd, cs, dst_format, VK_IMAGE_ASPECT_COLOR_BIT, 0, false, false);

   struct tu_image_view src;
   tu_image_view_copy(&src, src_image, src_image->vk_format, &info->imageSubresource, offset.z, stencil_read);

   for (uint32_t i = 0; i < layers; i++) {
      ops->src(cmd, cs, &src, i, VK_FILTER_NEAREST);

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

   ops->teardown(cmd, cs);
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

   for (unsigned i = 0; i < regionCount; ++i)
      tu_copy_image_to_buffer(cmd, src_image, dst_buffer, pRegions + i);
}

/* Tiled formats don't support swapping, which means that we can't support
 * formats that require a non-WZYX swap like B8G8R8A8 natively. Also, some
 * formats like B5G5R5A1 have a separate linear-only format when sampling.
 * Currently we fake support for tiled swapped formats and use the unswapped
 * format instead, but this means that reinterpreting copies to and from
 * swapped formats can't be performed correctly unless we can swizzle the
 * components by reinterpreting the other image as the "correct" swapped
 * format, i.e. only when the other image is linear.
 */

static bool
is_swapped_format(VkFormat format)
{
   struct tu_native_format linear = tu6_format_texture(format, TILE6_LINEAR);
   struct tu_native_format tiled = tu6_format_texture(format, TILE6_3);
   return linear.fmt != tiled.fmt || linear.swap != tiled.swap;
}

/* R8G8_* formats have a different tiling layout than other cpp=2 formats, and
 * therefore R8G8 images can't be reinterpreted as non-R8G8 images (and vice
 * versa). This should mirror the logic in fdl6_layout.
 */
static bool
image_is_r8g8(struct tu_image *image)
{
   return image->layout[0].cpp == 2 &&
      vk_format_get_nr_components(image->vk_format) == 2;
}

static void
tu_copy_image_to_image(struct tu_cmd_buffer *cmd,
                       struct tu_image *src_image,
                       struct tu_image *dst_image,
                       const VkImageCopy *info)
{
   const struct blit_ops *ops = &r2d_ops;
   struct tu_cs *cs = &cmd->cs;

   if (dst_image->layout[0].nr_samples > 1)
      ops = &r3d_ops;

   VkFormat format = VK_FORMAT_UNDEFINED;
   VkOffset3D src_offset = info->srcOffset;
   VkOffset3D dst_offset = info->dstOffset;
   VkExtent3D extent = info->extent;

   /* From the Vulkan 1.2.140 spec, section 19.3 "Copying Data Between
    * Images":
    *
    *    When copying between compressed and uncompressed formats the extent
    *    members represent the texel dimensions of the source image and not
    *    the destination. When copying from a compressed image to an
    *    uncompressed image the image texel dimensions written to the
    *    uncompressed image will be source extent divided by the compressed
    *    texel block dimensions. When copying from an uncompressed image to a
    *    compressed image the image texel dimensions written to the compressed
    *    image will be the source extent multiplied by the compressed texel
    *    block dimensions.
    *
    * This means we only have to adjust the extent if the source image is
    * compressed.
    */
   copy_compressed(src_image->vk_format, &src_offset, &extent, NULL, NULL);
   copy_compressed(dst_image->vk_format, &dst_offset, NULL, NULL, NULL);

   VkFormat dst_format = copy_format(dst_image->vk_format, info->dstSubresource.aspectMask, false);
   VkFormat src_format = copy_format(src_image->vk_format, info->srcSubresource.aspectMask, false);

   bool use_staging_blit = false;

   if (src_format == dst_format) {
      /* Images that share a format can always be copied directly because it's
       * the same as a blit.
       */
      format = src_format;
   } else if (!src_image->layout[0].tile_mode) {
      /* If an image is linear, we can always safely reinterpret it with the
       * other image's format and then do a regular blit.
       */
      format = dst_format;
   } else if (!dst_image->layout[0].tile_mode) {
      format = src_format;
   } else if (image_is_r8g8(src_image) != image_is_r8g8(dst_image)) {
      /* We can't currently copy r8g8 images to/from other cpp=2 images,
       * due to the different tile layout.
       */
      use_staging_blit = true;
   } else if (is_swapped_format(src_format) ||
              is_swapped_format(dst_format)) {
      /* If either format has a non-identity swap, then we can't copy
       * to/from it.
       */
      use_staging_blit = true;
   } else if (!src_image->layout[0].ubwc) {
      format = dst_format;
   } else if (!dst_image->layout[0].ubwc) {
      format = src_format;
   } else {
      /* Both formats use UBWC and so neither can be reinterpreted.
       * TODO: We could do an in-place decompression of the dst instead.
       */
      use_staging_blit = true;
   }

   struct tu_image_view dst, src;

   if (use_staging_blit) {
      tu_image_view_copy(&dst, dst_image, dst_format, &info->dstSubresource, dst_offset.z, false);
      tu_image_view_copy(&src, src_image, src_format, &info->srcSubresource, src_offset.z, false);

      struct tu_image staging_image = {
         .vk_format = src_format,
         .level_count = 1,
         .layer_count = info->srcSubresource.layerCount,
         .bo_offset = 0,
      }; 

      VkImageSubresourceLayers staging_subresource = {
         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
         .mipLevel = 0,
         .baseArrayLayer = 0,
         .layerCount = info->srcSubresource.layerCount,
      };

      VkOffset3D staging_offset = { 0 };

      staging_image.layout[0].tile_mode = TILE6_LINEAR;
      staging_image.layout[0].ubwc = false;

      fdl6_layout(&staging_image.layout[0],
                  vk_format_to_pipe_format(staging_image.vk_format),
                  src_image->layout[0].nr_samples,
                  extent.width,
                  extent.height,
                  extent.depth,
                  staging_image.level_count,
                  staging_image.layer_count,
                  extent.depth > 1,
                  NULL);

      VkResult result = tu_get_scratch_bo(cmd->device,
                                          staging_image.layout[0].size,
                                          &staging_image.bo);
      if (result != VK_SUCCESS) {
         cmd->record_result = result;
         return;
      }

      struct tu_image_view staging;
      tu_image_view_copy(&staging, &staging_image, src_format,
                         &staging_subresource, 0, false);

      ops->setup(cmd, cs, src_format, VK_IMAGE_ASPECT_COLOR_BIT, 0, false, false);
      coords(ops, cs, &staging_offset, &src_offset, &extent);

      for (uint32_t i = 0; i < info->extent.depth; i++) {
         ops->src(cmd, cs, &src, i, VK_FILTER_NEAREST);
         ops->dst(cs, &staging, i);
         ops->run(cmd, cs);
      }

      /* When executed by the user there has to be a pipeline barrier here,
       * but since we're doing it manually we'll have to flush ourselves.
       */
      tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS);
      tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE);

      tu_image_view_copy(&staging, &staging_image, dst_format,
                         &staging_subresource, 0, false);

      ops->setup(cmd, cs, dst_format, info->dstSubresource.aspectMask,
                 0, false, dst_image->layout[0].ubwc);
      coords(ops, cs, &dst_offset, &staging_offset, &extent);

      for (uint32_t i = 0; i < info->extent.depth; i++) {
         ops->src(cmd, cs, &staging, i, VK_FILTER_NEAREST);
         ops->dst(cs, &dst, i);
         ops->run(cmd, cs);
      }
   } else {
      tu_image_view_copy(&dst, dst_image, format, &info->dstSubresource, dst_offset.z, false);
      tu_image_view_copy(&src, src_image, format, &info->srcSubresource, src_offset.z, false);

      ops->setup(cmd, cs, format, info->dstSubresource.aspectMask,
                 0, false, dst_image->layout[0].ubwc);
      coords(ops, cs, &dst_offset, &src_offset, &extent);

      for (uint32_t i = 0; i < info->extent.depth; i++) {
         ops->src(cmd, cs, &src, i, VK_FILTER_NEAREST);
         ops->dst(cs, &dst, i);
         ops->run(cmd, cs);
      }
   }

   ops->teardown(cmd, cs);
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

   ops->setup(cmd, cs, format, VK_IMAGE_ASPECT_COLOR_BIT, 0, false, false);

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

   ops->teardown(cmd, cs);
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

   struct tu_cs_memory tmp;
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

   if (fillSize == VK_WHOLE_SIZE)
      fillSize = buffer->size - dstOffset;

   uint64_t dst_va = tu_buffer_iova(buffer) + dstOffset;
   uint32_t blocks = fillSize / 4;

   ops->setup(cmd, cs, VK_FORMAT_R32_UINT, VK_IMAGE_ASPECT_COLOR_BIT, 0, true, false);
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

   ops->teardown(cmd, cs);
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

   ops->setup(cmd, cs, dst_image->vk_format, VK_IMAGE_ASPECT_COLOR_BIT,
              0, false, dst_image->layout[0].ubwc);

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
         ops->src(cmd, cs, &src, i, VK_FILTER_NEAREST);
         ops->dst(cs, &dst, i);
         ops->run(cmd, cs);
      }
   }

   ops->teardown(cmd, cs);
}

#define for_each_layer(layer, layer_mask, layers) \
   for (uint32_t layer = 0; \
        layer < ((layer_mask) ? (util_logbase2(layer_mask) + 1) : layers); \
        layer++) \
      if (!layer_mask || (layer_mask & BIT(layer)))

static void
resolve_sysmem(struct tu_cmd_buffer *cmd,
               struct tu_cs *cs,
               VkFormat format,
               struct tu_image_view *src,
               struct tu_image_view *dst,
               uint32_t layer_mask,
               uint32_t layers,
               const VkRect2D *rect,
               bool separate_stencil)
{
   const struct blit_ops *ops = &r2d_ops;

   ops->setup(cmd, cs, format, VK_IMAGE_ASPECT_COLOR_BIT,
              0, false, dst->ubwc_enabled);
   ops->coords(cs, &rect->offset, &rect->offset, &rect->extent);

   for_each_layer(i, layer_mask, layers) {
      if (separate_stencil) {
         r2d_src_stencil(cmd, cs, src, i, VK_FILTER_NEAREST);
         r2d_dst_stencil(cs, dst, i);
      } else {
         ops->src(cmd, cs, src, i, VK_FILTER_NEAREST);
         ops->dst(cs, dst, i);
      }
      ops->run(cmd, cs);
   }

   ops->teardown(cmd, cs);
}

void
tu_resolve_sysmem(struct tu_cmd_buffer *cmd,
                  struct tu_cs *cs,
                  struct tu_image_view *src,
                  struct tu_image_view *dst,
                  uint32_t layer_mask,
                  uint32_t layers,
                  const VkRect2D *rect)
{
   assert(src->image->vk_format == dst->image->vk_format);

   if (dst->image->vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
      resolve_sysmem(cmd, cs, VK_FORMAT_D32_SFLOAT,
                     src, dst, layer_mask, layers, rect, false);
      resolve_sysmem(cmd, cs, VK_FORMAT_S8_UINT,
                     src, dst, layer_mask, layers, rect, true);
   } else {
      resolve_sysmem(cmd, cs, dst->image->vk_format,
                     src, dst, layer_mask, layers, rect, false);
   }
}

static void
clear_image(struct tu_cmd_buffer *cmd,
            struct tu_image *image,
            const VkClearValue *clear_value,
            const VkImageSubresourceRange *range,
            VkImageAspectFlags aspect_mask)
{
   uint32_t level_count = tu_get_levelCount(image, range);
   uint32_t layer_count = tu_get_layerCount(image, range);
   struct tu_cs *cs = &cmd->cs;
   VkFormat format = image->vk_format;
   if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)
      format = copy_format(format, aspect_mask, false);

   if (image->layout[0].depth0 > 1) {
      assert(layer_count == 1);
      assert(range->baseArrayLayer == 0);
   }

   const struct blit_ops *ops = image->layout[0].nr_samples > 1 ? &r3d_ops : &r2d_ops;

   ops->setup(cmd, cs, format, aspect_mask, 0, true, image->layout[0].ubwc);
   if (image->vk_format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)
      ops->clear_value(cs, VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, clear_value);
   else
      ops->clear_value(cs, format, clear_value);

   for (unsigned j = 0; j < level_count; j++) {
      if (image->layout[0].depth0 > 1)
         layer_count = u_minify(image->layout[0].depth0, range->baseMipLevel + j);

      ops->coords(cs, &(VkOffset2D){}, NULL, &(VkExtent2D) {
                     u_minify(image->layout[0].width0, range->baseMipLevel + j),
                     u_minify(image->layout[0].height0, range->baseMipLevel + j)
                  });

      struct tu_image_view dst;
      tu_image_view_copy_blit(&dst, image, format, &(VkImageSubresourceLayers) {
         .aspectMask = aspect_mask,
         .mipLevel = range->baseMipLevel + j,
         .baseArrayLayer = range->baseArrayLayer,
         .layerCount = 1,
      }, 0, false, false);

      for (uint32_t i = 0; i < layer_count; i++) {
         ops->dst(cs, &dst, i);
         ops->run(cmd, cs);
      }
   }

   ops->teardown(cmd, cs);
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

   for (unsigned i = 0; i < rangeCount; i++)
      clear_image(cmd, image, (const VkClearValue*) pColor, pRanges + i, VK_IMAGE_ASPECT_COLOR_BIT);
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

   for (unsigned i = 0; i < rangeCount; i++) {
      const VkImageSubresourceRange *range = &pRanges[i];

      if (image->vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
         /* can't clear both depth and stencil at once, split up the aspect mask */
         uint32_t b;
         for_each_bit(b, range->aspectMask)
            clear_image(cmd, image, (const VkClearValue*) pDepthStencil, range, BIT(b));
         continue;
      }

      clear_image(cmd, image, (const VkClearValue*) pDepthStencil, range, range->aspectMask);
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
   uint32_t clear_rts = 0, clear_components = 0, num_rts = 0, b;
   bool z_clear = false;
   bool s_clear = false;
   bool layered_clear = false;
   uint32_t max_samples = 1;

   for (uint32_t i = 0; i < attachment_count; i++) {
      uint32_t a;
      if (attachments[i].aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
         uint32_t c = attachments[i].colorAttachment;
         a = subpass->color_attachments[c].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;

         clear_rts |= 1 << c;
         clear_components |= 0xf << (c * 4);
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

   /* disable all draw states so they don't interfere
    * TODO: use and re-use draw states
    * we have to disable draw states individually to preserve
    * input attachment states, because a secondary command buffer
    * won't be able to restore them
    */
   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3 * (TU_DRAW_STATE_COUNT - 2));
   for (uint32_t i = 0; i < TU_DRAW_STATE_COUNT; i++) {
      if (i == TU_DRAW_STATE_INPUT_ATTACHMENTS_GMEM ||
          i == TU_DRAW_STATE_INPUT_ATTACHMENTS_SYSMEM)
         continue;
      tu_cs_emit(cs, CP_SET_DRAW_STATE__0_GROUP_ID(i) |
                     CP_SET_DRAW_STATE__0_DISABLE);
      tu_cs_emit_qw(cs, 0);
   }
   cmd->state.dirty |= TU_CMD_DIRTY_DRAW_STATE;

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

   for (uint32_t i = 0; i < rect_count; i++) {
      if (rects[i].baseArrayLayer || rects[i].layerCount > 1)
         layered_clear = true;
   }

   /* a630 doesn't support multiview masks, which means that we can't use the
    * normal multiview path without potentially recompiling a shader on-demand
    * or using a more complicated variant that takes the mask as a const. Just
    * use the layered path instead, since it shouldn't be much worse.
    */
   if (subpass->multiview_mask) {
      layered_clear = true;
   }

   r3d_common(cmd, cs, false, num_rts, layered_clear, false);

   tu_cs_emit_regs(cs,
                   A6XX_SP_FS_RENDER_COMPONENTS(.dword = clear_components));
   tu_cs_emit_regs(cs,
                   A6XX_RB_RENDER_COMPONENTS(.dword = clear_components));

   tu_cs_emit_regs(cs,
                   A6XX_RB_FS_OUTPUT_CNTL0(),
                   A6XX_RB_FS_OUTPUT_CNTL1(.mrt = mrt_count));

   tu_cs_emit_regs(cs, A6XX_SP_BLEND_CNTL());
   tu_cs_emit_regs(cs, A6XX_RB_BLEND_CNTL(.independent_blend = 1, .sample_mask = 0xffff));
   for (uint32_t i = 0; i < mrt_count; i++) {
      tu_cs_emit_regs(cs, A6XX_RB_MRT_CONTROL(i,
            .component_enable = COND(clear_rts & (1 << i), 0xf)));
   }

   tu_cs_emit_regs(cs, A6XX_GRAS_LRZ_CNTL(0));
   tu_cs_emit_regs(cs, A6XX_RB_LRZ_CNTL(0));

   tu_cs_emit_regs(cs, A6XX_RB_DEPTH_PLANE_CNTL());
   tu_cs_emit_regs(cs, A6XX_RB_DEPTH_CNTL(
         .z_enable = z_clear,
         .z_write_enable = z_clear,
         .zfunc = FUNC_ALWAYS));
   tu_cs_emit_regs(cs, A6XX_GRAS_SU_DEPTH_PLANE_CNTL());
   tu_cs_emit_regs(cs, A6XX_RB_STENCIL_CONTROL(
         .stencil_enable = s_clear,
         .func = FUNC_ALWAYS,
         .zpass = STENCIL_REPLACE));
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
      /* This should be true because of this valid usage for
       * vkCmdClearAttachments:
       *
       *    "If the render pass instance this is recorded in uses multiview,
       *    then baseArrayLayer must be zero and layerCount must be one"
       */
      assert(!subpass->multiview_mask || rects[i].baseArrayLayer == 0);

      for_each_layer(layer, subpass->multiview_mask, rects[i].layerCount) {
         r3d_coords_raw(cs, (float[]) {
            rects[i].rect.offset.x, rects[i].rect.offset.y,
            z_clear_val, uif(rects[i].baseArrayLayer + layer),
            rects[i].rect.offset.x + rects[i].rect.extent.width,
            rects[i].rect.offset.y + rects[i].rect.extent.height,
            z_clear_val, 1.0f,
         });
         r3d_run(cmd, cs);
      }
   }
}

static void
pack_gmem_clear_value(const VkClearValue *val, VkFormat format, uint32_t clear_value[4])
{
   switch (format) {
   case VK_FORMAT_X8_D24_UNORM_PACK32:
   case VK_FORMAT_D24_UNORM_S8_UINT:
      clear_value[0] = tu_pack_float32_for_unorm(val->depthStencil.depth, 24) |
                       val->depthStencil.stencil << 24;
      return;
   case VK_FORMAT_D16_UNORM:
      clear_value[0] = tu_pack_float32_for_unorm(val->depthStencil.depth, 16);
      return;
   case VK_FORMAT_D32_SFLOAT:
      clear_value[0] = fui(val->depthStencil.depth);
      return;
   case VK_FORMAT_S8_UINT:
      clear_value[0] = val->depthStencil.stencil;
      return;
   default:
      break;
   }

   float tmp[4];
   memcpy(tmp, val->color.float32, 4 * sizeof(float));
   if (vk_format_is_srgb(format)) {
      for (int i = 0; i < 3; i++)
         tmp[i] = util_format_linear_to_srgb_float(tmp[i]);
   }

#define PACK_F(type) util_format_##type##_pack_rgba_float \
   ( (uint8_t*) &clear_value[0], 0, tmp, 0, 1, 1)
   switch (vk_format_get_component_bits(format, UTIL_FORMAT_COLORSPACE_RGB, PIPE_SWIZZLE_X)) {
   case 4:
      PACK_F(r4g4b4a4_unorm);
      break;
   case 5:
      if (vk_format_get_component_bits(format, UTIL_FORMAT_COLORSPACE_RGB, PIPE_SWIZZLE_Y) == 6)
         PACK_F(r5g6b5_unorm);
      else
         PACK_F(r5g5b5a1_unorm);
      break;
   case 8:
      if (vk_format_is_snorm(format))
         PACK_F(r8g8b8a8_snorm);
      else if (vk_format_is_unorm(format))
         PACK_F(r8g8b8a8_unorm);
      else
         pack_int8(clear_value, val->color.uint32);
      break;
   case 10:
      if (vk_format_is_int(format))
         pack_int10_2(clear_value, val->color.uint32);
      else
         PACK_F(r10g10b10a2_unorm);
      break;
   case 11:
      clear_value[0] = float3_to_r11g11b10f(val->color.float32);
      break;
   case 16:
      if (vk_format_is_snorm(format))
         PACK_F(r16g16b16a16_snorm);
      else if (vk_format_is_unorm(format))
         PACK_F(r16g16b16a16_unorm);
      else if (vk_format_is_float(format))
         PACK_F(r16g16b16a16_float);
      else
         pack_int16(clear_value, val->color.uint32);
      break;
   case 32:
      memcpy(clear_value, val->color.float32, 4 * sizeof(float));
      break;
   default:
      unreachable("unexpected channel size");
   }
#undef PACK_F
}

static void
clear_gmem_attachment(struct tu_cmd_buffer *cmd,
                      struct tu_cs *cs,
                      VkFormat format,
                      uint8_t clear_mask,
                      uint32_t gmem_offset,
                      const VkClearValue *value)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_DST_INFO, 1);
   tu_cs_emit(cs, A6XX_RB_BLIT_DST_INFO_COLOR_FORMAT(tu6_base_format(format)));

   tu_cs_emit_regs(cs, A6XX_RB_BLIT_INFO(.gmem = 1, .clear_mask = clear_mask));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_BASE_GMEM, 1);
   tu_cs_emit(cs, gmem_offset);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_UNKNOWN_88D0, 1);
   tu_cs_emit(cs, 0);

   uint32_t clear_vals[4] = {};
   pack_gmem_clear_value(value, format, clear_vals);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_CLEAR_COLOR_DW0, 4);
   tu_cs_emit_array(cs, clear_vals, 4);

   tu6_emit_event_write(cmd, cs, BLIT);
}

static void
tu_emit_clear_gmem_attachment(struct tu_cmd_buffer *cmd,
                              struct tu_cs *cs,
                              uint32_t attachment,
                              VkImageAspectFlags mask,
                              const VkClearValue *value)
{
   const struct tu_render_pass_attachment *att =
      &cmd->state.pass->attachments[attachment];

   if (att->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
      if (mask & VK_IMAGE_ASPECT_DEPTH_BIT)
         clear_gmem_attachment(cmd, cs, VK_FORMAT_D32_SFLOAT, 0xf, att->gmem_offset, value);
      if (mask & VK_IMAGE_ASPECT_STENCIL_BIT)
         clear_gmem_attachment(cmd, cs, VK_FORMAT_S8_UINT, 0xf, att->gmem_offset_stencil, value);
      return;
   }

   clear_gmem_attachment(cmd, cs, att->format, aspect_write_mask(att->format, mask), att->gmem_offset, value);
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

         tu_emit_clear_gmem_attachment(cmd, cs, a, attachments[j].aspectMask,
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

   /* sysmem path behaves like a draw, note we don't have a way of using different
    * flushes for sysmem/gmem, so this needs to be outside of the cond_exec
    */
   tu_emit_cache_flush_renderpass(cmd, cs);

   for (uint32_t j = 0; j < attachmentCount; j++) {
      if ((pAttachments[j].aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) == 0)
         continue;
      cmd->state.lrz.valid = false;
      cmd->state.dirty |= TU_CMD_DIRTY_LRZ;
   }

   /* vkCmdClearAttachments is supposed to respect the predicate if active.
    * The easiest way to do this is to always use the 3d path, which always
    * works even with GMEM because it's just a simple draw using the existing
    * attachment state. However it seems that IGNORE_VISIBILITY draws must be
    * skipped in the binning pass, since otherwise they produce binning data
    * which isn't consumed and leads to the wrong binning data being read, so
    * condition on GMEM | SYSMEM.
    */
   if (cmd->state.predication_active) {
      tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_GMEM |
                             CP_COND_EXEC_0_RENDER_MODE_SYSMEM);
      tu_clear_sysmem_attachments(cmd, attachmentCount, pAttachments, rectCount, pRects);
      tu_cond_exec_end(cs);
      return;
   }

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_GMEM);
   tu_clear_gmem_attachments(cmd, attachmentCount, pAttachments, rectCount, pRects);
   tu_cond_exec_end(cs);

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_SYSMEM);
   tu_clear_sysmem_attachments(cmd, attachmentCount, pAttachments, rectCount, pRects);
   tu_cond_exec_end(cs);
}

static void
clear_sysmem_attachment(struct tu_cmd_buffer *cmd,
                        struct tu_cs *cs,
                        VkFormat format,
                        VkImageAspectFlags clear_mask,
                        const VkRenderPassBeginInfo *info,
                        uint32_t a,
                        bool separate_stencil)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_image_view *iview = fb->attachments[a].attachment;
   const uint32_t clear_views = cmd->state.pass->attachments[a].clear_views;
   const struct blit_ops *ops = &r2d_ops;
   if (cmd->state.pass->attachments[a].samples > 1)
      ops = &r3d_ops;

   ops->setup(cmd, cs, format, clear_mask, 0, true, iview->ubwc_enabled);
   ops->coords(cs, &info->renderArea.offset, NULL, &info->renderArea.extent);
   ops->clear_value(cs, format, &info->pClearValues[a]);

   for_each_layer(i, clear_views, fb->layers) {
      if (separate_stencil) {
         if (ops == &r3d_ops)
            r3d_dst_stencil(cs, iview, i);
         else
            r2d_dst_stencil(cs, iview, i);
      } else {
         ops->dst(cs, iview, i);
      }
      ops->run(cmd, cs);
   }

   ops->teardown(cmd, cs);
}

void
tu_clear_sysmem_attachment(struct tu_cmd_buffer *cmd,
                           struct tu_cs *cs,
                           uint32_t a,
                           const VkRenderPassBeginInfo *info)
{
   const struct tu_render_pass_attachment *attachment =
      &cmd->state.pass->attachments[a];

   if (!attachment->clear_mask)
      return;

   /* Wait for any flushes at the beginning of the renderpass to complete */
   tu_cs_emit_wfi(cs);

   if (attachment->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
      if (attachment->clear_mask & VK_IMAGE_ASPECT_DEPTH_BIT) {
         clear_sysmem_attachment(cmd, cs, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT,
                                 info, a, false);
      }
      if (attachment->clear_mask & VK_IMAGE_ASPECT_STENCIL_BIT) {
         clear_sysmem_attachment(cmd, cs, VK_FORMAT_S8_UINT, VK_IMAGE_ASPECT_COLOR_BIT,
                                 info, a, true);
      }
   } else {
      clear_sysmem_attachment(cmd, cs, attachment->format, attachment->clear_mask,
                              info, a, false);
   }

   /* The spec doesn't explicitly say, but presumably the initial renderpass
    * clear is considered part of the renderpass, and therefore barriers
    * aren't required inside the subpass/renderpass.  Therefore we need to
    * flush CCU color into CCU depth here, just like with
    * vkCmdClearAttachments(). Note that because this only happens at the
    * beginning of a renderpass, and renderpass writes are considered
    * "incoherent", we shouldn't have to worry about syncing depth into color
    * beforehand as depth should already be flushed.
    */
   if (vk_format_is_depth_or_stencil(attachment->format)) {
      tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS);
      tu6_emit_event_write(cmd, cs, PC_CCU_INVALIDATE_DEPTH);
   } else {
      tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS);
      tu6_emit_event_write(cmd, cs, PC_CCU_INVALIDATE_COLOR);
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

   if (!attachment->clear_mask)
      return;

   tu_cs_emit_regs(cs, A6XX_RB_MSAA_CNTL(tu_msaa_samples(attachment->samples)));

   tu_emit_clear_gmem_attachment(cmd, cs, a, attachment->clear_mask,
                                 &info->pClearValues[a]);
}

static void
tu_emit_blit(struct tu_cmd_buffer *cmd,
             struct tu_cs *cs,
             const struct tu_image_view *iview,
             const struct tu_render_pass_attachment *attachment,
             bool resolve,
             bool separate_stencil)
{
   tu_cs_emit_regs(cs,
                   A6XX_RB_MSAA_CNTL(tu_msaa_samples(attachment->samples)));

   tu_cs_emit_regs(cs, A6XX_RB_BLIT_INFO(
      .unk0 = !resolve,
      .gmem = !resolve,
      /* "integer" bit disables msaa resolve averaging */
      .integer = vk_format_is_int(attachment->format)));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_DST_INFO, 4);
   if (separate_stencil) {
      tu_cs_emit(cs, tu_image_view_stencil(iview, RB_BLIT_DST_INFO) & ~A6XX_RB_BLIT_DST_INFO_FLAGS);
      tu_cs_emit_qw(cs, iview->stencil_base_addr);
      tu_cs_emit(cs, iview->stencil_PITCH);

      tu_cs_emit_regs(cs,
                      A6XX_RB_BLIT_BASE_GMEM(attachment->gmem_offset_stencil));
   } else {
      tu_cs_emit(cs, iview->RB_BLIT_DST_INFO);
      tu_cs_image_ref_2d(cs, iview, 0, false);

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_FLAG_DST_LO, 3);
      tu_cs_image_flag_ref(cs, iview, 0);

      tu_cs_emit_regs(cs,
                      A6XX_RB_BLIT_BASE_GMEM(attachment->gmem_offset));
   }

   tu6_emit_event_write(cmd, cs, BLIT);
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
      tu_emit_blit(cmd, cs, iview, attachment, false, false);

   if (attachment->load_stencil || (attachment->format == VK_FORMAT_D32_SFLOAT_S8_UINT && force_load))
      tu_emit_blit(cmd, cs, iview, attachment, false, true);
}

static void
store_cp_blit(struct tu_cmd_buffer *cmd,
              struct tu_cs *cs,
              struct tu_image_view *iview,
              uint32_t samples,
              bool separate_stencil,
              VkFormat format,
              uint32_t gmem_offset,
              uint32_t cpp)
{
   r2d_setup_common(cmd, cs, format, VK_IMAGE_ASPECT_COLOR_BIT, 0, false,
                    iview->ubwc_enabled, true);
   if (separate_stencil)
      r2d_dst_stencil(cs, iview, 0);
   else
      r2d_dst(cs, iview, 0);

   tu_cs_emit_regs(cs,
                   A6XX_SP_PS_2D_SRC_INFO(
                      .color_format = tu6_format_texture(format, TILE6_2).fmt,
                      .tile_mode = TILE6_2,
                      .srgb = vk_format_is_srgb(format),
                      .samples = tu_msaa_samples(samples),
                      .samples_average = !vk_format_is_int(format) &&
                                         !vk_format_is_depth_or_stencil(format),
                      .unk20 = 1,
                      .unk22 = 1),
                   /* note: src size does not matter when not scaling */
                   A6XX_SP_PS_2D_SRC_SIZE( .width = 0x3fff, .height = 0x3fff),
                   A6XX_SP_PS_2D_SRC_LO(cmd->device->physical_device->gmem_base + gmem_offset),
                   A6XX_SP_PS_2D_SRC_HI(),
                   A6XX_SP_PS_2D_SRC_PITCH(.pitch = cmd->state.framebuffer->tile0.width * cpp));

   /* sync GMEM writes with CACHE. */
   tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE);

   /* Wait for CACHE_INVALIDATE to land */
   tu_cs_emit_wfi(cs);

   tu_cs_emit_pkt7(cs, CP_BLIT, 1);
   tu_cs_emit(cs, CP_BLIT_0_OP(BLIT_OP_SCALE));

   /* CP_BLIT writes to the CCU, unlike CP_EVENT_WRITE::BLIT which writes to
    * sysmem, and we generally assume that GMEM renderpasses leave their
    * results in sysmem, so we need to flush manually here.
    */
   tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS);
}

void
tu_store_gmem_attachment(struct tu_cmd_buffer *cmd,
                         struct tu_cs *cs,
                         uint32_t a,
                         uint32_t gmem_a)
{
   struct tu_physical_device *phys_dev = cmd->device->physical_device;
   const VkRect2D *render_area = &cmd->state.render_area;
   struct tu_render_pass_attachment *dst = &cmd->state.pass->attachments[a];
   struct tu_image_view *iview = cmd->state.framebuffer->attachments[a].attachment;
   struct tu_render_pass_attachment *src = &cmd->state.pass->attachments[gmem_a];

   if (!dst->store && !dst->store_stencil)
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
      x1 % phys_dev->info.gmem_align_w ||
      (x2 % phys_dev->info.gmem_align_w && x2 != iview->extent.width) ||
      y1 % phys_dev->info.gmem_align_h || (y2 % phys_dev->info.gmem_align_h && need_y2_align);

   /* use fast path when render area is aligned, except for unsupported resolve cases */
   if (!unaligned && (a == gmem_a || blit_can_resolve(dst->format))) {
      if (dst->store)
         tu_emit_blit(cmd, cs, iview, src, true, false);
      if (dst->store_stencil)
         tu_emit_blit(cmd, cs, iview, src, true, true);
      return;
   }

   if (dst->samples > 1) {
      /* I guess we need to use shader path in this case?
       * need a testcase which fails because of this
       */
      tu_finishme("unaligned store of msaa attachment\n");
      return;
   }

   r2d_coords(cs, &render_area->offset, &render_area->offset, &render_area->extent);

   VkFormat format = src->format;
   if (format == VK_FORMAT_D32_SFLOAT_S8_UINT)
      format = VK_FORMAT_D32_SFLOAT;

   if (dst->store) {
      store_cp_blit(cmd, cs, iview, src->samples, false, format,
                    src->gmem_offset, src->cpp);
   }
   if (dst->store_stencil) {
      store_cp_blit(cmd, cs, iview, src->samples, true, VK_FORMAT_S8_UINT,
                    src->gmem_offset_stencil, src->samples);
   }
}
