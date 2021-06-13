/*
 * Copyright © 2019 Raspberry Pi
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

#include "v3dv_private.h"
#include "vk_util.h"
#include "vk_format_info.h"

#include "broadcom/cle/v3dx_pack.h"
#include "drm-uapi/drm_fourcc.h"
#include "util/format/u_format.h"
#include "vulkan/wsi/wsi_common.h"

#define SWIZ(x,y,z,w) {   \
   PIPE_SWIZZLE_##x,      \
   PIPE_SWIZZLE_##y,      \
   PIPE_SWIZZLE_##z,      \
   PIPE_SWIZZLE_##w       \
}

#define FORMAT(vk, rt, tex, swiz, return_size, supports_filtering)  \
   [VK_FORMAT_##vk] = {                                             \
      true,                                                         \
      V3D_OUTPUT_IMAGE_FORMAT_##rt,                                 \
      TEXTURE_DATA_FORMAT_##tex,                                    \
      swiz,                                                         \
      return_size,                                                  \
      supports_filtering,                                           \
   }

#define SWIZ_X001 SWIZ(X, 0, 0, 1)
#define SWIZ_XY01 SWIZ(X, Y, 0, 1)
#define SWIZ_XYZ1 SWIZ(X, Y, Z, 1)
#define SWIZ_XYZW SWIZ(X, Y, Z, W)
#define SWIZ_YZWX SWIZ(Y, Z, W, X)
#define SWIZ_YZW1 SWIZ(Y, Z, W, 1)
#define SWIZ_ZYXW SWIZ(Z, Y, X, W)
#define SWIZ_ZYX1 SWIZ(Z, Y, X, 1)
#define SWIZ_XXXY SWIZ(X, X, X, Y)
#define SWIZ_XXX1 SWIZ(X, X, X, 1)
#define SWIZ_XXXX SWIZ(X, X, X, X)
#define SWIZ_000X SWIZ(0, 0, 0, X)
#define SWIZ_WXYZ SWIZ(W, X, Y, Z)

/* FIXME: expand format table to describe whether the format is supported
 * for buffer surfaces (texel buffers, vertex buffers, etc).
 */
static const struct v3dv_format format_table[] = {
   /* Color, 4 channels */
   FORMAT(B8G8R8A8_SRGB,           SRGB8_ALPHA8, RGBA8,         SWIZ_ZYXW, 16, true),
   FORMAT(B8G8R8A8_UNORM,          RGBA8,        RGBA8,         SWIZ_ZYXW, 16, true),

   FORMAT(R8G8B8A8_SRGB,           SRGB8_ALPHA8, RGBA8,         SWIZ_XYZW, 16, true),
   FORMAT(R8G8B8A8_UNORM,          RGBA8,        RGBA8,         SWIZ_XYZW, 16, true),
   FORMAT(R8G8B8A8_SNORM,          NO,           RGBA8_SNORM,   SWIZ_XYZW, 16, true),
   FORMAT(R8G8B8A8_SINT,           RGBA8I,       RGBA8I,        SWIZ_XYZW, 16, false),
   FORMAT(R8G8B8A8_UINT,           RGBA8UI,      RGBA8UI,       SWIZ_XYZW, 16, false),

   FORMAT(R16G16B16A16_SFLOAT,     RGBA16F,      RGBA16F,       SWIZ_XYZW, 16, true),
   FORMAT(R16G16B16A16_UNORM,      NO,           RGBA16,        SWIZ_XYZW, 32, true),
   FORMAT(R16G16B16A16_SNORM,      NO,           RGBA16_SNORM,  SWIZ_XYZW, 32, true),
   FORMAT(R16G16B16A16_SINT,       RGBA16I,      RGBA16I,       SWIZ_XYZW, 16, false),
   FORMAT(R16G16B16A16_UINT,       RGBA16UI,     RGBA16UI,      SWIZ_XYZW, 16, false),

   FORMAT(R32G32B32A32_SFLOAT,     RGBA32F,      RGBA32F,       SWIZ_XYZW, 32, false),
   FORMAT(R32G32B32A32_SINT,       RGBA32I,      RGBA32I,       SWIZ_XYZW, 32, false),
   FORMAT(R32G32B32A32_UINT,       RGBA32UI,     RGBA32UI,      SWIZ_XYZW, 32, false),

   /* Color, 3 channels */
   FORMAT(R32G32B32_SFLOAT,        NO,           NO,            SWIZ_XYZ1,  0, false),
   FORMAT(R32G32B32_UINT,          NO,           NO,            SWIZ_XYZ1,  0, false),
   FORMAT(R32G32B32_SINT,          NO,           NO,            SWIZ_XYZ1,  0, false),

   /* Color, 2 channels */
   FORMAT(R8G8_UNORM,              RG8,          RG8,           SWIZ_XY01, 16, true),
   FORMAT(R8G8_SNORM,              NO,           RG8_SNORM,     SWIZ_XY01, 16, true),
   FORMAT(R8G8_SINT,               RG8I,         RG8I,          SWIZ_XY01, 16, false),
   FORMAT(R8G8_UINT,               RG8UI,        RG8UI,         SWIZ_XY01, 16, false),

   FORMAT(R16G16_UNORM,            NO,           RG16,          SWIZ_XY01, 32, true),
   FORMAT(R16G16_SNORM,            NO,           RG16_SNORM,    SWIZ_XY01, 32, true),
   FORMAT(R16G16_SFLOAT,           RG16F,        RG16F,         SWIZ_XY01, 16, true),
   FORMAT(R16G16_SINT,             RG16I,        RG16I,         SWIZ_XY01, 16, false),
   FORMAT(R16G16_UINT,             RG16UI,       RG16UI,        SWIZ_XY01, 16, false),

   FORMAT(R32G32_SFLOAT,           RG32F,        RG32F,         SWIZ_XY01, 32, false),
   FORMAT(R32G32_SINT,             RG32I,        RG32I,         SWIZ_XY01, 32, false),
   FORMAT(R32G32_UINT,             RG32UI,       RG32UI,        SWIZ_XY01, 32, false),

   /* Color, 1 channel */
   FORMAT(R8_UNORM,                R8,           R8,            SWIZ_X001, 16, true),
   FORMAT(R8_SNORM,                NO,           R8_SNORM,      SWIZ_X001, 16, true),
   FORMAT(R8_SINT,                 R8I,          R8I,           SWIZ_X001, 16, false),
   FORMAT(R8_UINT,                 R8UI,         R8UI,          SWIZ_X001, 16, false),

   FORMAT(R16_UNORM,               NO,           R16,           SWIZ_X001, 32, true),
   FORMAT(R16_SNORM,               NO,           R16_SNORM,     SWIZ_X001, 32, true),
   FORMAT(R16_SFLOAT,              R16F,         R16F,          SWIZ_X001, 16, true),
   FORMAT(R16_SINT,                R16I,         R16I,          SWIZ_X001, 16, false),
   FORMAT(R16_UINT,                R16UI,        R16UI,         SWIZ_X001, 16, false),

   FORMAT(R32_SFLOAT,              R32F,         R32F,          SWIZ_X001, 32, false),
   FORMAT(R32_SINT,                R32I,         R32I,          SWIZ_X001, 32, false),
   FORMAT(R32_UINT,                R32UI,        R32UI,         SWIZ_X001, 32, false),

   /* Color, packed */
   FORMAT(B4G4R4A4_UNORM_PACK16,   ABGR4444,     RGBA4,         SWIZ_ZYXW, 16, true), /* Swap RB */
   FORMAT(R5G6B5_UNORM_PACK16,     BGR565,       RGB565,        SWIZ_XYZ1, 16, true),
   FORMAT(R5G5B5A1_UNORM_PACK16,   ABGR1555,     RGB5_A1,       SWIZ_XYZW, 16, true),
   FORMAT(A1R5G5B5_UNORM_PACK16,   RGBA5551,     A1_RGB5,       SWIZ_ZYXW, 16, true), /* Swap RB */
   FORMAT(A8B8G8R8_UNORM_PACK32,   RGBA8,        RGBA8,         SWIZ_XYZW, 16, true), /* RGBA8 UNORM */
   FORMAT(A8B8G8R8_SNORM_PACK32,   NO,           RGBA8_SNORM,   SWIZ_XYZW, 16, true), /* RGBA8 SNORM */
   FORMAT(A8B8G8R8_UINT_PACK32,    RGBA8UI,      RGBA8UI,       SWIZ_XYZW, 16, true), /* RGBA8 UINT */
   FORMAT(A8B8G8R8_SINT_PACK32,    RGBA8I,       RGBA8I,        SWIZ_XYZW, 16, true), /* RGBA8 SINT */
   FORMAT(A8B8G8R8_SRGB_PACK32,    SRGB8_ALPHA8, RGBA8,         SWIZ_XYZW, 16, true), /* RGBA8 sRGB */
   FORMAT(A2B10G10R10_UNORM_PACK32,RGB10_A2,     RGB10_A2,      SWIZ_XYZW, 16, true),
   FORMAT(A2B10G10R10_UINT_PACK32, RGB10_A2UI,   RGB10_A2UI,    SWIZ_XYZW, 16, true),
   FORMAT(E5B9G9R9_UFLOAT_PACK32,  NO,           RGB9_E5,       SWIZ_XYZ1, 16, true),
   FORMAT(B10G11R11_UFLOAT_PACK32, R11F_G11F_B10F,R11F_G11F_B10F, SWIZ_XYZ1, 16, true),

   /* Depth */
   FORMAT(D16_UNORM,               D16,          DEPTH_COMP16,  SWIZ_X001, 32, false),
   FORMAT(D32_SFLOAT,              D32F,         DEPTH_COMP32F, SWIZ_X001, 32, false),
   FORMAT(X8_D24_UNORM_PACK32,     D24S8,        DEPTH24_X8,    SWIZ_X001, 32, false),

   /* Depth + Stencil */
   FORMAT(D24_UNORM_S8_UINT,       D24S8,        DEPTH24_X8,    SWIZ_X001, 32, false),

   /* Compressed: ETC2 / EAC */
   FORMAT(ETC2_R8G8B8_UNORM_BLOCK,    NO,  RGB8_ETC2,                SWIZ_XYZ1, 16, true),
   FORMAT(ETC2_R8G8B8_SRGB_BLOCK,     NO,  RGB8_ETC2,                SWIZ_XYZ1, 16, true),
   FORMAT(ETC2_R8G8B8A1_UNORM_BLOCK,  NO,  RGB8_PUNCHTHROUGH_ALPHA1, SWIZ_XYZW, 16, true),
   FORMAT(ETC2_R8G8B8A1_SRGB_BLOCK,   NO,  RGB8_PUNCHTHROUGH_ALPHA1, SWIZ_XYZW, 16, true),
   FORMAT(ETC2_R8G8B8A8_UNORM_BLOCK,  NO,  RGBA8_ETC2_EAC,           SWIZ_XYZW, 16, true),
   FORMAT(ETC2_R8G8B8A8_SRGB_BLOCK,   NO,  RGBA8_ETC2_EAC,           SWIZ_XYZW, 16, true),
   FORMAT(EAC_R11_UNORM_BLOCK,        NO,  R11_EAC,                  SWIZ_X001, 16, true),
   FORMAT(EAC_R11_SNORM_BLOCK,        NO,  SIGNED_R11_EAC,           SWIZ_X001, 16, true),
   FORMAT(EAC_R11G11_UNORM_BLOCK,     NO,  RG11_EAC,                 SWIZ_XY01, 16, true),
   FORMAT(EAC_R11G11_SNORM_BLOCK,     NO,  SIGNED_RG11_EAC,          SWIZ_XY01, 16, true),

   /* Compressed: BC1-3 */
   FORMAT(BC1_RGB_UNORM_BLOCK,        NO,  BC1,                      SWIZ_XYZ1, 16, true),
   FORMAT(BC1_RGB_SRGB_BLOCK,         NO,  BC1,                      SWIZ_XYZ1, 16, true),
   FORMAT(BC1_RGBA_UNORM_BLOCK,       NO,  BC1,                      SWIZ_XYZW, 16, true),
   FORMAT(BC1_RGBA_SRGB_BLOCK,        NO,  BC1,                      SWIZ_XYZW, 16, true),
   FORMAT(BC2_UNORM_BLOCK,            NO,  BC2,                      SWIZ_XYZW, 16, true),
   FORMAT(BC2_SRGB_BLOCK,             NO,  BC2,                      SWIZ_XYZW, 16, true),
   FORMAT(BC3_UNORM_BLOCK,            NO,  BC3,                      SWIZ_XYZW, 16, true),
   FORMAT(BC3_SRGB_BLOCK,             NO,  BC3,                      SWIZ_XYZW, 16, true),
};

const struct v3dv_format *
v3dv_get_format(VkFormat format)
{
   if (format < ARRAY_SIZE(format_table) && format_table[format].supported)
      return &format_table[format];
   else
      return NULL;
}

void
v3dv_get_internal_type_bpp_for_output_format(uint32_t format,
                                             uint32_t *type,
                                             uint32_t *bpp)
{
   switch (format) {
   case V3D_OUTPUT_IMAGE_FORMAT_RGBA8:
   case V3D_OUTPUT_IMAGE_FORMAT_RGB8:
   case V3D_OUTPUT_IMAGE_FORMAT_RG8:
   case V3D_OUTPUT_IMAGE_FORMAT_R8:
   case V3D_OUTPUT_IMAGE_FORMAT_ABGR4444:
   case V3D_OUTPUT_IMAGE_FORMAT_BGR565:
   case V3D_OUTPUT_IMAGE_FORMAT_ABGR1555:
      *type = V3D_INTERNAL_TYPE_8;
      *bpp = V3D_INTERNAL_BPP_32;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_RGBA8I:
   case V3D_OUTPUT_IMAGE_FORMAT_RG8I:
   case V3D_OUTPUT_IMAGE_FORMAT_R8I:
      *type = V3D_INTERNAL_TYPE_8I;
      *bpp = V3D_INTERNAL_BPP_32;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_RGBA8UI:
   case V3D_OUTPUT_IMAGE_FORMAT_RG8UI:
   case V3D_OUTPUT_IMAGE_FORMAT_R8UI:
      *type = V3D_INTERNAL_TYPE_8UI;
      *bpp = V3D_INTERNAL_BPP_32;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_SRGB8_ALPHA8:
   case V3D_OUTPUT_IMAGE_FORMAT_SRGB:
   case V3D_OUTPUT_IMAGE_FORMAT_RGB10_A2:
   case V3D_OUTPUT_IMAGE_FORMAT_R11F_G11F_B10F:
   case V3D_OUTPUT_IMAGE_FORMAT_RGBA16F:
      /* Note that sRGB RTs are stored in the tile buffer at 16F,
       * and the conversion to sRGB happens at tilebuffer load/store.
       */
      *type = V3D_INTERNAL_TYPE_16F;
      *bpp = V3D_INTERNAL_BPP_64;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_RG16F:
   case V3D_OUTPUT_IMAGE_FORMAT_R16F:
      *type = V3D_INTERNAL_TYPE_16F;
      /* Use 64bpp to make sure the TLB doesn't throw away the alpha
       * channel before alpha test happens.
       */
      *bpp = V3D_INTERNAL_BPP_64;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_RGBA16I:
      *type = V3D_INTERNAL_TYPE_16I;
      *bpp = V3D_INTERNAL_BPP_64;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_RG16I:
   case V3D_OUTPUT_IMAGE_FORMAT_R16I:
      *type = V3D_INTERNAL_TYPE_16I;
      *bpp = V3D_INTERNAL_BPP_32;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_RGB10_A2UI:
   case V3D_OUTPUT_IMAGE_FORMAT_RGBA16UI:
      *type = V3D_INTERNAL_TYPE_16UI;
      *bpp = V3D_INTERNAL_BPP_64;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_RG16UI:
   case V3D_OUTPUT_IMAGE_FORMAT_R16UI:
      *type = V3D_INTERNAL_TYPE_16UI;
      *bpp = V3D_INTERNAL_BPP_32;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_RGBA32I:
      *type = V3D_INTERNAL_TYPE_32I;
      *bpp = V3D_INTERNAL_BPP_128;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_RG32I:
      *type = V3D_INTERNAL_TYPE_32I;
      *bpp = V3D_INTERNAL_BPP_64;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_R32I:
      *type = V3D_INTERNAL_TYPE_32I;
      *bpp = V3D_INTERNAL_BPP_32;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_RGBA32UI:
      *type = V3D_INTERNAL_TYPE_32UI;
      *bpp = V3D_INTERNAL_BPP_128;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_RG32UI:
      *type = V3D_INTERNAL_TYPE_32UI;
      *bpp = V3D_INTERNAL_BPP_64;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_R32UI:
      *type = V3D_INTERNAL_TYPE_32UI;
      *bpp = V3D_INTERNAL_BPP_32;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_RGBA32F:
      *type = V3D_INTERNAL_TYPE_32F;
      *bpp = V3D_INTERNAL_BPP_128;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_RG32F:
      *type = V3D_INTERNAL_TYPE_32F;
      *bpp = V3D_INTERNAL_BPP_64;
      break;

   case V3D_OUTPUT_IMAGE_FORMAT_R32F:
      *type = V3D_INTERNAL_TYPE_32F;
      *bpp = V3D_INTERNAL_BPP_32;
      break;

   default:
      /* Provide some default values, as we'll be called at RB
       * creation time, even if an RB with this format isn't supported.
       */
      *type = V3D_INTERNAL_TYPE_8;
      *bpp = V3D_INTERNAL_BPP_32;
      break;
   }
}

bool
v3dv_format_supports_tlb_resolve(const struct v3dv_format *format)
{
   uint32_t type, bpp;
   v3dv_get_internal_type_bpp_for_output_format(format->rt_type, &type, &bpp);
   return type == V3D_INTERNAL_TYPE_8 || type == V3D_INTERNAL_TYPE_16F;
}

const uint8_t *
v3dv_get_format_swizzle(VkFormat f)
{
   const struct v3dv_format *vf = v3dv_get_format(f);
   static const uint8_t fallback[] = {0, 1, 2, 3};

   if (!vf)
      return fallback;

   return vf->swizzle;
}

uint8_t
v3dv_get_tex_return_size(const struct v3dv_format *vf,
                         bool compare_enable)
{
   if (compare_enable)
      return 16;

   return vf->return_size;
}

bool
v3dv_tfu_supports_tex_format(const struct v3d_device_info *devinfo,
                             uint32_t tex_format)
{
   assert(devinfo->ver >= 42);

   switch (tex_format) {
   case TEXTURE_DATA_FORMAT_R8:
   case TEXTURE_DATA_FORMAT_R8_SNORM:
   case TEXTURE_DATA_FORMAT_RG8:
   case TEXTURE_DATA_FORMAT_RG8_SNORM:
   case TEXTURE_DATA_FORMAT_RGBA8:
   case TEXTURE_DATA_FORMAT_RGBA8_SNORM:
   case TEXTURE_DATA_FORMAT_RGB565:
   case TEXTURE_DATA_FORMAT_RGBA4:
   case TEXTURE_DATA_FORMAT_RGB5_A1:
   case TEXTURE_DATA_FORMAT_RGB10_A2:
   case TEXTURE_DATA_FORMAT_R16:
   case TEXTURE_DATA_FORMAT_R16_SNORM:
   case TEXTURE_DATA_FORMAT_RG16:
   case TEXTURE_DATA_FORMAT_RG16_SNORM:
   case TEXTURE_DATA_FORMAT_RGBA16:
   case TEXTURE_DATA_FORMAT_RGBA16_SNORM:
   case TEXTURE_DATA_FORMAT_R16F:
   case TEXTURE_DATA_FORMAT_RG16F:
   case TEXTURE_DATA_FORMAT_RGBA16F:
   case TEXTURE_DATA_FORMAT_R11F_G11F_B10F:
   case TEXTURE_DATA_FORMAT_R4:
   case TEXTURE_DATA_FORMAT_RGB9_E5:
   case TEXTURE_DATA_FORMAT_R32F:
   case TEXTURE_DATA_FORMAT_RG32F:
   case TEXTURE_DATA_FORMAT_RGBA32F:
   case TEXTURE_DATA_FORMAT_RGB8_ETC2:
   case TEXTURE_DATA_FORMAT_RGB8_PUNCHTHROUGH_ALPHA1:
   case TEXTURE_DATA_FORMAT_RGBA8_ETC2_EAC:
   case TEXTURE_DATA_FORMAT_R11_EAC:
   case TEXTURE_DATA_FORMAT_SIGNED_R11_EAC:
   case TEXTURE_DATA_FORMAT_RG11_EAC:
   case TEXTURE_DATA_FORMAT_SIGNED_RG11_EAC:
      return true;
   default:
      return false;
   }
}

/* Some cases of transfer operations are raw data copies that don't depend
 * on the semantics of the pixel format (no pixel format conversions are
 * involved). In these cases, it is safe to choose any format supported by
 * the TFU so long as it has the same texel size, which allows us to use the
 * TFU paths with formats that are not TFU supported otherwise.
 */
const struct v3dv_format *
v3dv_get_compatible_tfu_format(const struct v3d_device_info *devinfo,
                               uint32_t bpp,
                               VkFormat *out_vk_format)
{
   VkFormat vk_format;
   switch (bpp) {
   case 16: vk_format = VK_FORMAT_R32G32B32A32_SFLOAT;  break;
   case 8:  vk_format = VK_FORMAT_R16G16B16A16_SFLOAT;  break;
   case 4:  vk_format = VK_FORMAT_R32_SFLOAT;           break;
   case 2:  vk_format = VK_FORMAT_R16_SFLOAT;           break;
   case 1:  vk_format = VK_FORMAT_R8_UNORM;             break;
   default: unreachable("unsupported format bit-size"); break;
   };

   if (out_vk_format)
      *out_vk_format = vk_format;

   const struct v3dv_format *format = v3dv_get_format(vk_format);
   assert(v3dv_tfu_supports_tex_format(devinfo, format->tex_type));

   return format;
}

static bool
format_supports_blending(const struct v3dv_format *format)
{
   /* Hardware blending is only supported on render targets that are configured
    * 4x8-bit unorm, 2x16-bit float or 4x16-bit float.
    */
   uint32_t type, bpp;
   v3dv_get_internal_type_bpp_for_output_format(format->rt_type, &type, &bpp);
   switch (type) {
   case V3D_INTERNAL_TYPE_8:
      return bpp == V3D_INTERNAL_BPP_32;
   case V3D_INTERNAL_TYPE_16F:
      return bpp == V3D_INTERNAL_BPP_32 || V3D_INTERNAL_BPP_64;
   default:
      return false;
   }
}

static VkFormatFeatureFlags
image_format_features(VkFormat vk_format,
                      const struct v3dv_format *v3dv_format,
                      VkImageTiling tiling)
{
   if (!v3dv_format || !v3dv_format->supported)
      return 0;

   const VkImageAspectFlags aspects = vk_format_aspects(vk_format);

   const VkImageAspectFlags zs_aspects = VK_IMAGE_ASPECT_DEPTH_BIT |
                                         VK_IMAGE_ASPECT_STENCIL_BIT;
   const VkImageAspectFlags supported_aspects = VK_IMAGE_ASPECT_COLOR_BIT |
                                                zs_aspects;
   if ((aspects & supported_aspects) != aspects)
      return 0;

   /* FIXME: We don't support separate stencil yet */
   if ((aspects & zs_aspects) == VK_IMAGE_ASPECT_STENCIL_BIT)
      return 0;

   if (v3dv_format->tex_type == TEXTURE_DATA_FORMAT_NO &&
       v3dv_format->rt_type == V3D_OUTPUT_IMAGE_FORMAT_NO) {
      return 0;
   }

   VkFormatFeatureFlags flags = 0;

   /* Raster format is only supported for 1D textures, so let's just
    * always require optimal tiling for anything that requires sampling.
    * Note: even if the user requests optimal for a 1D image, we will still
    * use raster format since that is what the HW requires.
    */
   if (v3dv_format->tex_type != TEXTURE_DATA_FORMAT_NO &&
       tiling == VK_IMAGE_TILING_OPTIMAL) {
      flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
               VK_FORMAT_FEATURE_BLIT_SRC_BIT;

      if (v3dv_format->supports_filtering)
         flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
   }

   if (v3dv_format->rt_type != V3D_OUTPUT_IMAGE_FORMAT_NO) {
      if (aspects & VK_IMAGE_ASPECT_COLOR_BIT) {
         flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                  VK_FORMAT_FEATURE_BLIT_DST_BIT;
         if (format_supports_blending(v3dv_format))
            flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
      } else if (aspects & zs_aspects) {
         flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                  VK_FORMAT_FEATURE_BLIT_DST_BIT;
      }
   }

   const struct util_format_description *desc =
      vk_format_description(vk_format);
   assert(desc);

   if (desc->layout == UTIL_FORMAT_LAYOUT_PLAIN && desc->is_array) {
      flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
      if (desc->nr_channels == 1 && vk_format_is_int(vk_format))
         flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;
   } else if (vk_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
              vk_format == VK_FORMAT_A2B10G10R10_UINT_PACK32 ||
              vk_format == VK_FORMAT_B10G11R11_UFLOAT_PACK32) {
      /* To comply with shaderStorageImageExtendedFormats */
      flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
   }

   if (flags) {
      flags |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
               VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
   }

   return flags;
}

static VkFormatFeatureFlags
buffer_format_features(VkFormat vk_format, const struct v3dv_format *v3dv_format)
{
   if (!v3dv_format || !v3dv_format->supported)
      return 0;

   if (!v3dv_format->supported)
      return 0;

   /* We probably only want to support buffer formats that have a
    * color format specification.
    */
   if (!vk_format_is_color(vk_format))
      return 0;

   const struct util_format_description *desc =
      vk_format_description(vk_format);
   assert(desc);

   VkFormatFeatureFlags flags = 0;
   if (desc->layout == UTIL_FORMAT_LAYOUT_PLAIN &&
       desc->colorspace == UTIL_FORMAT_COLORSPACE_RGB &&
       desc->is_array) {
      flags |=  VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
      if (v3dv_format->tex_type != TEXTURE_DATA_FORMAT_NO) {
         flags |= VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT |
                  VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
      }
   } else if (vk_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32) {
      flags |= VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT |
               VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT |
               VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
   } else if (vk_format == VK_FORMAT_A2B10G10R10_UINT_PACK32 ||
              vk_format == VK_FORMAT_B10G11R11_UFLOAT_PACK32) {
      flags |= VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT |
               VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_PLAIN &&
       desc->is_array &&
       desc->nr_channels == 1 &&
       vk_format_is_int(vk_format)) {
      flags |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;
   }

   return flags;
}

bool
v3dv_buffer_format_supports_features(VkFormat vk_format,
                                     VkFormatFeatureFlags features)
{
   const struct v3dv_format *v3dv_format = v3dv_get_format(vk_format);
   const VkFormatFeatureFlags supported =
      buffer_format_features(vk_format, v3dv_format);
   return (supported & features) == features;
}

void
v3dv_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice,
                                       VkFormat format,
                                       VkFormatProperties* pFormatProperties)
{
   const struct v3dv_format *v3dv_format = v3dv_get_format(format);

   *pFormatProperties = (VkFormatProperties) {
      .linearTilingFeatures =
         image_format_features(format, v3dv_format, VK_IMAGE_TILING_LINEAR),
      .optimalTilingFeatures =
         image_format_features(format, v3dv_format, VK_IMAGE_TILING_OPTIMAL),
      .bufferFeatures =
         buffer_format_features(format, v3dv_format),
   };
}

void
v3dv_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice,
                                        VkFormat format,
                                        VkFormatProperties2 *pFormatProperties)
{
   v3dv_GetPhysicalDeviceFormatProperties(physicalDevice, format,
                                          &pFormatProperties->formatProperties);

   vk_foreach_struct(ext, pFormatProperties->pNext) {
      switch ((unsigned)ext->sType) {
      case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT: {
         struct VkDrmFormatModifierPropertiesListEXT *list = (void *)ext;
         VK_OUTARRAY_MAKE(out, list->pDrmFormatModifierProperties,
                          &list->drmFormatModifierCount);
         if (pFormatProperties->formatProperties.linearTilingFeatures) {
            vk_outarray_append(&out, mod_props) {
               mod_props->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
               mod_props->drmFormatModifierPlaneCount = 1;
            }
         }
         if (pFormatProperties->formatProperties.optimalTilingFeatures) {
            vk_outarray_append(&out, mod_props) {
               mod_props->drmFormatModifier = DRM_FORMAT_MOD_BROADCOM_UIF;
               mod_props->drmFormatModifierPlaneCount = 1;
            }
         }
         break;
      }
      default:
         v3dv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

static VkResult
get_image_format_properties(
   struct v3dv_physical_device *physical_device,
   const VkPhysicalDeviceImageFormatInfo2 *info,
   VkImageTiling tiling,
   VkImageFormatProperties *pImageFormatProperties,
   VkSamplerYcbcrConversionImageFormatProperties *pYcbcrImageFormatProperties)
{
   const struct v3dv_format *v3dv_format = v3dv_get_format(info->format);
   VkFormatFeatureFlags format_feature_flags =
      image_format_features(info->format, v3dv_format, tiling);
   if (!format_feature_flags)
      goto unsupported;

   if (info->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)) {
         goto unsupported;
      }

      /* Sampling of raster depth/stencil images is not supported. Since 1D
       * images are always raster, even if the user requested optimal tiling,
       * we can't have them be used as transfer sources, since that includes
       * using them for blit sources, which might require sampling.
       */
      if (info->type == VK_IMAGE_TYPE_1D &&
          vk_format_is_depth_or_stencil(info->format)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
         goto unsupported;
      }

      /* Sampling of raster depth/stencil images is not supported. Since 1D
       * images are always raster, even if the user requested optimal tiling,
       * we can't allow sampling if the format is depth/stencil.
       */
      if (info->type == VK_IMAGE_TYPE_1D &&
          vk_format_is_depth_or_stencil(info->format)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_STORAGE_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      if (!(format_feature_flags &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
         goto unsupported;
      }
   }

   /* FIXME: these are taken from VkPhysicalDeviceLimits, we should just put
    * these limits available in the physical device and read them from there
    * wherever we need them.
    */
   switch (info->type) {
   case VK_IMAGE_TYPE_1D:
      pImageFormatProperties->maxExtent.width = 4096;
      pImageFormatProperties->maxExtent.height = 1;
      pImageFormatProperties->maxExtent.depth = 1;
      pImageFormatProperties->maxArrayLayers = 2048;
      pImageFormatProperties->maxMipLevels = 13; /* log2(maxWidth) + 1 */
      break;
   case VK_IMAGE_TYPE_2D:
      pImageFormatProperties->maxExtent.width = 4096;
      pImageFormatProperties->maxExtent.height = 4096;
      pImageFormatProperties->maxExtent.depth = 1;
      pImageFormatProperties->maxArrayLayers = 2048;
      pImageFormatProperties->maxMipLevels = 13; /* log2(maxWidth) + 1 */
      break;
   case VK_IMAGE_TYPE_3D:
      pImageFormatProperties->maxExtent.width = 4096;
      pImageFormatProperties->maxExtent.height = 4096;
      pImageFormatProperties->maxExtent.depth = 4096;
      pImageFormatProperties->maxArrayLayers = 1;
      pImageFormatProperties->maxMipLevels = 13; /* log2(maxWidth) + 1 */
      break;
   default:
      unreachable("bad VkImageType");
   }

   /* Our hw doesn't support 1D compressed textures. */
   if (info->type == VK_IMAGE_TYPE_1D &&
       vk_format_is_compressed(info->format)) {
       goto unsupported;
   }

   /* From the Vulkan 1.0 spec, section 34.1.1. Supported Sample Counts:
    *
    * sampleCounts will be set to VK_SAMPLE_COUNT_1_BIT if at least one of the
    * following conditions is true:
    *
    *   - tiling is VK_IMAGE_TILING_LINEAR
    *   - type is not VK_IMAGE_TYPE_2D
    *   - flags contains VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
    *   - neither the VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT flag nor the
    *     VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT flag in
    *     VkFormatProperties::optimalTilingFeatures returned by
    *     vkGetPhysicalDeviceFormatProperties is set.
    */
   pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   if (tiling != VK_IMAGE_TILING_LINEAR &&
       info->type == VK_IMAGE_TYPE_2D &&
       !(info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
       (format_feature_flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT ||
        format_feature_flags & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
      pImageFormatProperties->sampleCounts |= VK_SAMPLE_COUNT_4_BIT;
   }

   if (tiling == VK_IMAGE_TILING_LINEAR)
      pImageFormatProperties->maxMipLevels = 1;

   pImageFormatProperties->maxResourceSize = 0xffffffff; /* 32-bit allocation */

   return VK_SUCCESS;

unsupported:
   *pImageFormatProperties = (VkImageFormatProperties) {
      .maxExtent = { 0, 0, 0 },
      .maxMipLevels = 0,
      .maxArrayLayers = 0,
      .sampleCounts = 0,
      .maxResourceSize = 0,
   };

   return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

static const VkExternalMemoryProperties prime_fd_props = {
   .externalMemoryFeatures = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
                             VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
   .exportFromImportedHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   .compatibleHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
};

VkResult
v3dv_GetPhysicalDeviceImageFormatProperties(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkImageType type,
   VkImageTiling tiling,
   VkImageUsageFlags usage,
   VkImageCreateFlags createFlags,
   VkImageFormatProperties *pImageFormatProperties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, physical_device, physicalDevice);

   const VkPhysicalDeviceImageFormatInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .pNext = NULL,
      .format = format,
      .type = type,
      .tiling = tiling,
      .usage = usage,
      .flags = createFlags,
   };

   return get_image_format_properties(physical_device, &info, tiling,
                                      pImageFormatProperties, NULL);
}

VkResult
v3dv_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice,
                                             const VkPhysicalDeviceImageFormatInfo2 *base_info,
                                             VkImageFormatProperties2 *base_props)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, physical_device, physicalDevice);
   const VkPhysicalDeviceExternalImageFormatInfo *external_info = NULL;
   const VkPhysicalDeviceImageDrmFormatModifierInfoEXT *drm_format_mod_info = NULL;
   VkExternalImageFormatProperties *external_props = NULL;
   VkImageTiling tiling = base_info->tiling;

   /* Extract input structs */
   vk_foreach_struct_const(s, base_info->pNext) {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         external_info = (const void *) s;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT:
         drm_format_mod_info = (const void *) s;
         switch (drm_format_mod_info->drmFormatModifier) {
         case DRM_FORMAT_MOD_LINEAR:
            tiling = VK_IMAGE_TILING_LINEAR;
            break;
         case DRM_FORMAT_MOD_BROADCOM_UIF:
            tiling = VK_IMAGE_TILING_OPTIMAL;
            break;
         default:
            assert("Unknown DRM format modifier");
         }
         break;
      default:
         v3dv_debug_ignored_stype(s->sType);
         break;
      }
   }

   assert(tiling == VK_IMAGE_TILING_OPTIMAL ||
          tiling == VK_IMAGE_TILING_LINEAR);

   /* Extract output structs */
   vk_foreach_struct(s, base_props->pNext) {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES:
         external_props = (void *) s;
         break;
      default:
         v3dv_debug_ignored_stype(s->sType);
         break;
      }
   }

   VkResult result =
      get_image_format_properties(physical_device, base_info, tiling,
                                  &base_props->imageFormatProperties, NULL);
   if (result != VK_SUCCESS)
      goto done;

   if (external_info && external_info->handleType != 0) {
      switch (external_info->handleType) {
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
         if (external_props)
            external_props->externalMemoryProperties = prime_fd_props;
         break;
      default:
         result = VK_ERROR_FORMAT_NOT_SUPPORTED;
         break;
      }
   }

done:
   return result;
}

void
v3dv_GetPhysicalDeviceSparseImageFormatProperties(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkImageType type,
   VkSampleCountFlagBits samples,
   VkImageUsageFlags usage,
   VkImageTiling tiling,
   uint32_t *pPropertyCount,
   VkSparseImageFormatProperties *pProperties)
{
   *pPropertyCount = 0;
}

void
v3dv_GetPhysicalDeviceSparseImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
   uint32_t *pPropertyCount,
   VkSparseImageFormatProperties2 *pProperties)
{
   *pPropertyCount = 0;
}

void
v3dv_GetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties)
{
   switch (pExternalBufferInfo->handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      pExternalBufferProperties->externalMemoryProperties = prime_fd_props;
      return;
   default: /* Unsupported */
      pExternalBufferProperties->externalMemoryProperties =
         (VkExternalMemoryProperties) {
            .compatibleHandleTypes = pExternalBufferInfo->handleType,
         };
      break;
   }
}
