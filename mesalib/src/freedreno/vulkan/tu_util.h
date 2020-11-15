/*
 * Copyright 2020 Valve Corporation
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Jonathan Marek <jonathan@marek.ca>
 */

#ifndef TU_UTIL_H
#define TU_UTIL_H

#include <assert.h>
#include <stdint.h>

#include "util/macros.h"
#include "util/u_math.h"
#include "util/format/u_format_pack.h"
#include "util/format/u_format_zs.h"
#include "compiler/shader_enums.h"

#include "adreno_common.xml.h"
#include "adreno_pm4.xml.h"
#include "a6xx.xml.h"

#include <vulkan/vulkan.h>

static inline gl_shader_stage
vk_to_mesa_shader_stage(VkShaderStageFlagBits vk_stage)
{
   assert(__builtin_popcount(vk_stage) == 1);
   return util_logbase2(vk_stage);
}

static inline VkShaderStageFlagBits
mesa_to_vk_shader_stage(gl_shader_stage mesa_stage)
{
   return 1 << mesa_stage;
}

#define TU_STAGE_MASK ((1 << MESA_SHADER_STAGES) - 1)

#define tu_foreach_stage(stage, stage_bits)                                  \
   for (gl_shader_stage stage,                                               \
        __tmp = (gl_shader_stage)((stage_bits) &TU_STAGE_MASK);              \
        stage = __builtin_ffs(__tmp) - 1, __tmp; __tmp &= ~(1 << (stage)))

static inline enum a3xx_msaa_samples
tu_msaa_samples(uint32_t samples)
{
   assert(__builtin_popcount(samples) == 1);
   return util_logbase2(samples);
}

static inline uint32_t
tu6_stage2opcode(gl_shader_stage stage)
{
   if (stage == MESA_SHADER_FRAGMENT || stage == MESA_SHADER_COMPUTE)
      return CP_LOAD_STATE6_FRAG;
   return CP_LOAD_STATE6_GEOM;
}

static inline enum a6xx_state_block
tu6_stage2texsb(gl_shader_stage stage)
{
   return SB6_VS_TEX + stage;
}

static inline enum a6xx_state_block
tu6_stage2shadersb(gl_shader_stage stage)
{
   return SB6_VS_SHADER + stage;
}

static inline enum a3xx_rop_code
tu6_rop(VkLogicOp op)
{
   /* note: hw enum matches the VK enum, but with the 4 bits reversed */
   static const uint8_t lookup[] = {
      [VK_LOGIC_OP_CLEAR]           = ROP_CLEAR,
      [VK_LOGIC_OP_AND]             = ROP_AND,
      [VK_LOGIC_OP_AND_REVERSE]     = ROP_AND_REVERSE,
      [VK_LOGIC_OP_COPY]            = ROP_COPY,
      [VK_LOGIC_OP_AND_INVERTED]    = ROP_AND_INVERTED,
      [VK_LOGIC_OP_NO_OP]           = ROP_NOOP,
      [VK_LOGIC_OP_XOR]             = ROP_XOR,
      [VK_LOGIC_OP_OR]              = ROP_OR,
      [VK_LOGIC_OP_NOR]             = ROP_NOR,
      [VK_LOGIC_OP_EQUIVALENT]      = ROP_EQUIV,
      [VK_LOGIC_OP_INVERT]          = ROP_INVERT,
      [VK_LOGIC_OP_OR_REVERSE]      = ROP_OR_REVERSE,
      [VK_LOGIC_OP_COPY_INVERTED]   = ROP_COPY_INVERTED,
      [VK_LOGIC_OP_OR_INVERTED]     = ROP_OR_INVERTED,
      [VK_LOGIC_OP_NAND]            = ROP_NAND,
      [VK_LOGIC_OP_SET]             = ROP_SET,
   };
   assert(op < ARRAY_SIZE(lookup));
   return lookup[op];
}

static inline enum pc_di_primtype
tu6_primtype(VkPrimitiveTopology topology)
{
   static const uint8_t lookup[] = {
      [VK_PRIMITIVE_TOPOLOGY_POINT_LIST]                    = DI_PT_POINTLIST,
      [VK_PRIMITIVE_TOPOLOGY_LINE_LIST]                     = DI_PT_LINELIST,
      [VK_PRIMITIVE_TOPOLOGY_LINE_STRIP]                    = DI_PT_LINESTRIP,
      [VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST]                 = DI_PT_TRILIST,
      [VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP]                = DI_PT_TRISTRIP,
      [VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN]                  = DI_PT_TRIFAN,
      [VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY]      = DI_PT_LINE_ADJ,
      [VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY]     = DI_PT_LINESTRIP_ADJ,
      [VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY]  = DI_PT_TRI_ADJ,
      [VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY] = DI_PT_TRISTRIP_ADJ,
      /* Return PATCH0 and update in tu_pipeline_builder_parse_tessellation */
      [VK_PRIMITIVE_TOPOLOGY_PATCH_LIST]                    = DI_PT_PATCHES0,
   };
   assert(topology < ARRAY_SIZE(lookup));
   return lookup[topology];
}

static inline enum adreno_compare_func
tu6_compare_func(VkCompareOp op)
{
   return (enum adreno_compare_func) op;
}

static inline enum adreno_stencil_op
tu6_stencil_op(VkStencilOp op)
{
   return (enum adreno_stencil_op) op;
}

static inline enum adreno_rb_blend_factor
tu6_blend_factor(VkBlendFactor factor)
{
   static const uint8_t lookup[] = {
      [VK_BLEND_FACTOR_ZERO]                    = FACTOR_ZERO,
      [VK_BLEND_FACTOR_ONE]                     = FACTOR_ONE,
      [VK_BLEND_FACTOR_SRC_COLOR]               = FACTOR_SRC_COLOR,
      [VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR]     = FACTOR_ONE_MINUS_SRC_COLOR,
      [VK_BLEND_FACTOR_DST_COLOR]               = FACTOR_DST_COLOR,
      [VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR]     = FACTOR_ONE_MINUS_DST_COLOR,
      [VK_BLEND_FACTOR_SRC_ALPHA]               = FACTOR_SRC_ALPHA,
      [VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA]     = FACTOR_ONE_MINUS_SRC_ALPHA,
      [VK_BLEND_FACTOR_DST_ALPHA]               = FACTOR_DST_ALPHA,
      [VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA]     = FACTOR_ONE_MINUS_DST_ALPHA,
      [VK_BLEND_FACTOR_CONSTANT_COLOR]          = FACTOR_CONSTANT_COLOR,
      [VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR]= FACTOR_ONE_MINUS_CONSTANT_COLOR,
      [VK_BLEND_FACTOR_CONSTANT_ALPHA]          = FACTOR_CONSTANT_ALPHA,
      [VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA]= FACTOR_ONE_MINUS_CONSTANT_ALPHA,
      [VK_BLEND_FACTOR_SRC_ALPHA_SATURATE]      = FACTOR_SRC_ALPHA_SATURATE,
      [VK_BLEND_FACTOR_SRC1_COLOR]              = FACTOR_SRC1_COLOR,
      [VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR]    = FACTOR_ONE_MINUS_SRC1_COLOR,
      [VK_BLEND_FACTOR_SRC1_ALPHA]              = FACTOR_SRC1_ALPHA,
      [VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA]    = FACTOR_ONE_MINUS_SRC1_ALPHA,
   };
   assert(factor < ARRAY_SIZE(lookup));
   return lookup[factor];
}

static inline enum a3xx_rb_blend_opcode
tu6_blend_op(VkBlendOp op)
{
   return (enum a3xx_rb_blend_opcode) op;
}

static inline enum a6xx_tex_type
tu6_tex_type(VkImageViewType type, bool storage)
{
   switch (type) {
   default:
   case VK_IMAGE_VIEW_TYPE_1D:
   case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
      return A6XX_TEX_1D;
   case VK_IMAGE_VIEW_TYPE_2D:
   case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
      return A6XX_TEX_2D;
   case VK_IMAGE_VIEW_TYPE_3D:
      return A6XX_TEX_3D;
   case VK_IMAGE_VIEW_TYPE_CUBE:
   case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
      return storage ? A6XX_TEX_2D : A6XX_TEX_CUBE;
   }
}

static inline enum a6xx_tex_clamp
tu6_tex_wrap(VkSamplerAddressMode address_mode)
{
   uint8_t lookup[] = {
      [VK_SAMPLER_ADDRESS_MODE_REPEAT]                = A6XX_TEX_REPEAT,
      [VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT]       = A6XX_TEX_MIRROR_REPEAT,
      [VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE]         = A6XX_TEX_CLAMP_TO_EDGE,
      [VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER]       = A6XX_TEX_CLAMP_TO_BORDER,
      [VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE]  = A6XX_TEX_MIRROR_CLAMP,
   };
   assert(address_mode < ARRAY_SIZE(lookup));
   return lookup[address_mode];
}

static inline enum a6xx_tex_filter
tu6_tex_filter(VkFilter filter, unsigned aniso)
{
   switch (filter) {
   case VK_FILTER_NEAREST:
      return A6XX_TEX_NEAREST;
   case VK_FILTER_LINEAR:
      return aniso ? A6XX_TEX_ANISO : A6XX_TEX_LINEAR;
   case VK_FILTER_CUBIC_EXT:
      return A6XX_TEX_CUBIC;
   default:
      unreachable("illegal texture filter");
      break;
   }
}

static inline enum a6xx_reduction_mode
tu6_reduction_mode(VkSamplerReductionMode reduction_mode)
{
   return (enum a6xx_reduction_mode) reduction_mode;
}

static inline enum a6xx_depth_format
tu6_pipe2depth(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_D16_UNORM:
      return DEPTH6_16;
   case VK_FORMAT_X8_D24_UNORM_PACK32:
   case VK_FORMAT_D24_UNORM_S8_UINT:
      return DEPTH6_24_8;
   case VK_FORMAT_D32_SFLOAT:
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
   case VK_FORMAT_S8_UINT:
      return DEPTH6_32;
   default:
      return ~0;
   }
}

static inline enum a6xx_polygon_mode
tu6_polygon_mode(VkPolygonMode mode)
{
   switch (mode) {
   case VK_POLYGON_MODE_POINT:
      return POLYMODE6_POINTS;
   case VK_POLYGON_MODE_LINE:
      return POLYMODE6_LINES;
   case VK_POLYGON_MODE_FILL:
      return POLYMODE6_TRIANGLES;
   default:
      unreachable("bad polygon mode");
   }
}

struct bcolor_entry {
   uint32_t fp32[4];
   uint64_t ui16;
   uint64_t si16;
   uint64_t fp16;
   uint16_t rgb565;
   uint16_t rgb5a1;
   uint16_t rgba4;
   uint8_t __pad0[2];
   uint32_t ui8;
   uint32_t si8;
   uint32_t rgb10a2;
   uint32_t z24; /* also s8? */
   uint64_t srgb;
   uint8_t  __pad1[56];
} __attribute__((aligned(128)));

/* vulkan does not want clamping of integer clear values, differs from u_format
 * see spec for VkClearColorValue
 */
static inline void
pack_int8(uint32_t *dst, const uint32_t *val)
{
   *dst = (val[0] & 0xff) |
          (val[1] & 0xff) << 8 |
          (val[2] & 0xff) << 16 |
          (val[3] & 0xff) << 24;
}

static inline void
pack_int10_2(uint32_t *dst, const uint32_t *val)
{
   *dst = (val[0] & 0x3ff) |
          (val[1] & 0x3ff) << 10 |
          (val[2] & 0x3ff) << 20 |
          (val[3] & 0x3)   << 30;
}

static inline void
pack_int16(uint32_t *dst, const uint32_t *val)
{
   dst[0] = (val[0] & 0xffff) |
            (val[1] & 0xffff) << 16;
   dst[1] = (val[2] & 0xffff) |
            (val[3] & 0xffff) << 16;
}

static inline void
tu6_pack_border_color(struct bcolor_entry *bcolor, const VkClearColorValue *val, bool is_int)
{
   memcpy(bcolor->fp32, val, 4 * sizeof(float));
   if (is_int) {
      pack_int16((uint32_t*) &bcolor->fp16, val->uint32);
      return;
   }
#define PACK_F(x, type) util_format_##type##_pack_rgba_float \
   ( (uint8_t*) (&bcolor->x), 0, val->float32, 0, 1, 1)
   PACK_F(ui16, r16g16b16a16_unorm);
   PACK_F(si16, r16g16b16a16_snorm);
   PACK_F(fp16, r16g16b16a16_float);
   PACK_F(rgb565, r5g6b5_unorm);
   PACK_F(rgb5a1, r5g5b5a1_unorm);
   PACK_F(rgba4, r4g4b4a4_unorm);
   PACK_F(ui8, r8g8b8a8_unorm);
   PACK_F(si8, r8g8b8a8_snorm);
   PACK_F(rgb10a2, r10g10b10a2_unorm);
   util_format_x8z24_unorm_pack_z_float((uint8_t*) &bcolor->z24,
                                        0, val->float32, 0, 1, 1);
   PACK_F(srgb, r16g16b16a16_float); /* TODO: clamp? */
#undef PACK_F
}

#endif /* TU_UTIL_H */
