#include "util/format/u_format.h"
#include "zink_format.h"

static const VkFormat formats[PIPE_FORMAT_COUNT] = {
#define MAP_FORMAT_NORM(FMT) \
   [PIPE_FORMAT_ ## FMT ## _UNORM] = VK_FORMAT_ ## FMT ## _UNORM, \
   [PIPE_FORMAT_ ## FMT ## _SNORM] = VK_FORMAT_ ## FMT ## _SNORM,

#define MAP_FORMAT_SCALED(FMT) \
   [PIPE_FORMAT_ ## FMT ## _USCALED] = VK_FORMAT_ ## FMT ## _USCALED, \
   [PIPE_FORMAT_ ## FMT ## _SSCALED] = VK_FORMAT_ ## FMT ## _SSCALED,

#define MAP_FORMAT_INT(FMT) \
   [PIPE_FORMAT_ ## FMT ## _UINT] = VK_FORMAT_ ## FMT ## _UINT, \
   [PIPE_FORMAT_ ## FMT ## _SINT] = VK_FORMAT_ ## FMT ## _SINT,

#define MAP_FORMAT_SRGB(FMT) \
   [PIPE_FORMAT_ ## FMT ## _SRGB] = VK_FORMAT_ ## FMT ## _SRGB,

#define MAP_FORMAT_FLOAT(FMT) \
   [PIPE_FORMAT_ ## FMT ## _FLOAT] = VK_FORMAT_ ## FMT ## _SFLOAT,

   // one component

   // 8-bits
   MAP_FORMAT_NORM(R8)
   MAP_FORMAT_SCALED(R8)
   MAP_FORMAT_INT(R8)
   MAP_FORMAT_SRGB(R8)
   // 16-bits
   MAP_FORMAT_NORM(R16)
   MAP_FORMAT_SCALED(R16)
   MAP_FORMAT_INT(R16)
   MAP_FORMAT_FLOAT(R16)
   // 32-bits
   MAP_FORMAT_INT(R32)
   MAP_FORMAT_FLOAT(R32)

   // two components

   // 8-bits
   MAP_FORMAT_NORM(R8G8)
   MAP_FORMAT_SCALED(R8G8)
   MAP_FORMAT_INT(R8G8)
   MAP_FORMAT_SRGB(R8G8)
   // 16-bits
   MAP_FORMAT_NORM(R16G16)
   MAP_FORMAT_SCALED(R16G16)
   MAP_FORMAT_INT(R16G16)
   MAP_FORMAT_FLOAT(R16G16)
   // 32-bits
   MAP_FORMAT_INT(R32G32)
   MAP_FORMAT_FLOAT(R32G32)

   // three components

   // 8-bits
   MAP_FORMAT_NORM(R8G8B8)
   MAP_FORMAT_SCALED(R8G8B8)
   MAP_FORMAT_INT(R8G8B8)
   MAP_FORMAT_SRGB(R8G8B8)
   MAP_FORMAT_NORM(B8G8R8)
   MAP_FORMAT_SCALED(B8G8R8)
   MAP_FORMAT_INT(B8G8R8)
   MAP_FORMAT_SRGB(B8G8R8)
   // 16-bits
   MAP_FORMAT_NORM(R16G16B16)
   MAP_FORMAT_SCALED(R16G16B16)
   MAP_FORMAT_INT(R16G16B16)
   MAP_FORMAT_FLOAT(R16G16B16)
   // 32-bits
   MAP_FORMAT_INT(R32G32B32)
   MAP_FORMAT_FLOAT(R32G32B32)

   // four components

   // 8-bits
   MAP_FORMAT_NORM(R8G8B8A8)
   MAP_FORMAT_SCALED(R8G8B8A8)
   MAP_FORMAT_INT(R8G8B8A8)
   MAP_FORMAT_NORM(B8G8R8A8)
   MAP_FORMAT_SCALED(B8G8R8A8)
   MAP_FORMAT_INT(B8G8R8A8)
   MAP_FORMAT_SRGB(B8G8R8A8)
   [PIPE_FORMAT_RGBA8888_SRGB] = VK_FORMAT_A8B8G8R8_SRGB_PACK32,
   // 16-bits
   MAP_FORMAT_NORM(R16G16B16A16)
   MAP_FORMAT_SCALED(R16G16B16A16)
   MAP_FORMAT_INT(R16G16B16A16)
   MAP_FORMAT_FLOAT(R16G16B16A16)
   // 32-bits
   MAP_FORMAT_INT(R32G32B32A32)
   MAP_FORMAT_FLOAT(R32G32B32A32)

   // other color formats
   [PIPE_FORMAT_A4B4G4R4_UNORM] = VK_FORMAT_R4G4B4A4_UNORM_PACK16,
   [PIPE_FORMAT_A4R4G4B4_UNORM] = VK_FORMAT_B4G4R4A4_UNORM_PACK16,
   [PIPE_FORMAT_B4G4R4A4_UNORM] = VK_FORMAT_A4R4G4B4_UNORM_PACK16_EXT,
   [PIPE_FORMAT_R4G4B4A4_UNORM] = VK_FORMAT_A4B4G4R4_UNORM_PACK16_EXT,
   [PIPE_FORMAT_B5G6R5_UNORM] = VK_FORMAT_R5G6B5_UNORM_PACK16,
   [PIPE_FORMAT_R5G6B5_UNORM] = VK_FORMAT_B5G6R5_UNORM_PACK16,

   [PIPE_FORMAT_A1B5G5R5_UNORM] = VK_FORMAT_R5G5B5A1_UNORM_PACK16,
   [PIPE_FORMAT_A1R5G5B5_UNORM] = VK_FORMAT_B5G5R5A1_UNORM_PACK16,
   [PIPE_FORMAT_B5G5R5A1_UNORM] = VK_FORMAT_A1R5G5B5_UNORM_PACK16,

   [PIPE_FORMAT_R11G11B10_FLOAT] = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
   [PIPE_FORMAT_R9G9B9E5_FLOAT] = VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
   /* ARB_vertex_type_2_10_10_10 */
   [PIPE_FORMAT_R10G10B10A2_UNORM] = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
   [PIPE_FORMAT_R10G10B10A2_SNORM] = VK_FORMAT_A2B10G10R10_SNORM_PACK32,
   [PIPE_FORMAT_B10G10R10A2_UNORM] = VK_FORMAT_A2R10G10B10_UNORM_PACK32,
   [PIPE_FORMAT_B10G10R10A2_SNORM] = VK_FORMAT_A2R10G10B10_SNORM_PACK32,
   [PIPE_FORMAT_R10G10B10A2_USCALED] = VK_FORMAT_A2B10G10R10_USCALED_PACK32,
   [PIPE_FORMAT_R10G10B10A2_SSCALED] = VK_FORMAT_A2B10G10R10_SSCALED_PACK32,
   [PIPE_FORMAT_B10G10R10A2_USCALED] = VK_FORMAT_A2R10G10B10_USCALED_PACK32,
   [PIPE_FORMAT_B10G10R10A2_SSCALED] = VK_FORMAT_A2R10G10B10_SSCALED_PACK32,
   [PIPE_FORMAT_R10G10B10A2_UINT] = VK_FORMAT_A2B10G10R10_UINT_PACK32,
   [PIPE_FORMAT_B10G10R10A2_UINT] = VK_FORMAT_A2R10G10B10_UINT_PACK32,
   [PIPE_FORMAT_B10G10R10A2_SINT] = VK_FORMAT_A2R10G10B10_SINT_PACK32,

   // depth/stencil formats
   [PIPE_FORMAT_Z32_FLOAT] = VK_FORMAT_D32_SFLOAT,
   [PIPE_FORMAT_Z32_FLOAT_S8X24_UINT] = VK_FORMAT_D32_SFLOAT_S8_UINT,
   [PIPE_FORMAT_Z16_UNORM] = VK_FORMAT_D16_UNORM,
   [PIPE_FORMAT_Z16_UNORM_S8_UINT] = VK_FORMAT_D16_UNORM_S8_UINT,
   [PIPE_FORMAT_Z24X8_UNORM] = VK_FORMAT_X8_D24_UNORM_PACK32,
   [PIPE_FORMAT_Z24_UNORM_S8_UINT] = VK_FORMAT_D24_UNORM_S8_UINT,
   [PIPE_FORMAT_S8_UINT] = VK_FORMAT_S8_UINT,

   // compressed formats
   [PIPE_FORMAT_DXT1_RGB] = VK_FORMAT_BC1_RGB_UNORM_BLOCK,
   [PIPE_FORMAT_DXT1_RGBA] = VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
   [PIPE_FORMAT_DXT3_RGBA] = VK_FORMAT_BC2_UNORM_BLOCK,
   [PIPE_FORMAT_DXT5_RGBA] = VK_FORMAT_BC3_UNORM_BLOCK,
   [PIPE_FORMAT_DXT1_SRGB] = VK_FORMAT_BC1_RGB_SRGB_BLOCK,
   [PIPE_FORMAT_DXT1_SRGBA] = VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
   [PIPE_FORMAT_DXT3_SRGBA] = VK_FORMAT_BC2_SRGB_BLOCK,
   [PIPE_FORMAT_DXT5_SRGBA] = VK_FORMAT_BC3_SRGB_BLOCK,

   [PIPE_FORMAT_RGTC1_UNORM] = VK_FORMAT_BC4_UNORM_BLOCK,
   [PIPE_FORMAT_RGTC1_SNORM] = VK_FORMAT_BC4_SNORM_BLOCK,
   [PIPE_FORMAT_RGTC2_UNORM] = VK_FORMAT_BC5_UNORM_BLOCK,
   [PIPE_FORMAT_RGTC2_SNORM] = VK_FORMAT_BC5_SNORM_BLOCK,
   [PIPE_FORMAT_BPTC_RGBA_UNORM] = VK_FORMAT_BC7_UNORM_BLOCK,
   [PIPE_FORMAT_BPTC_SRGBA] = VK_FORMAT_BC7_SRGB_BLOCK,
   [PIPE_FORMAT_BPTC_RGB_FLOAT] = VK_FORMAT_BC6H_SFLOAT_BLOCK,
   [PIPE_FORMAT_BPTC_RGB_UFLOAT] = VK_FORMAT_BC6H_UFLOAT_BLOCK,
};

enum pipe_format
zink_decompose_vertex_format(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   unsigned first_non_void = util_format_get_first_non_void_channel(format);
   enum pipe_format new_format;
   assert(first_non_void == 0);
   if (!desc->is_array)
      return PIPE_FORMAT_NONE;
   if (desc->is_unorm) {
      enum pipe_format unorm_formats[] = {
         PIPE_FORMAT_R8_UNORM,
         PIPE_FORMAT_R16_UNORM,
         PIPE_FORMAT_R32_UNORM
      };
      return unorm_formats[desc->channel[first_non_void].size >> 4];
   } else if (desc->is_snorm) {
      enum pipe_format snorm_formats[] = {
         PIPE_FORMAT_R8_SNORM,
         PIPE_FORMAT_R16_SNORM,
         PIPE_FORMAT_R32_SNORM
      };
      return snorm_formats[desc->channel[first_non_void].size >> 4];
   } else {
      enum pipe_format uint_formats[][3] = {
         {PIPE_FORMAT_R8_USCALED, PIPE_FORMAT_R16_USCALED, PIPE_FORMAT_R32_USCALED},
         {PIPE_FORMAT_R8_UINT, PIPE_FORMAT_R16_UINT, PIPE_FORMAT_R32_UINT},
      };
      enum pipe_format sint_formats[][3] = {
         {PIPE_FORMAT_R8_SSCALED, PIPE_FORMAT_R16_SSCALED, PIPE_FORMAT_R32_SSCALED},
         {PIPE_FORMAT_R8_SINT, PIPE_FORMAT_R16_SINT, PIPE_FORMAT_R32_SINT},
      };
      switch (desc->channel[first_non_void].type) {
      case UTIL_FORMAT_TYPE_UNSIGNED:
         return uint_formats[desc->channel[first_non_void].pure_integer][desc->channel[first_non_void].size >> 4];
      case UTIL_FORMAT_TYPE_SIGNED:
         return sint_formats[desc->channel[first_non_void].pure_integer][desc->channel[first_non_void].size >> 4];
      case UTIL_FORMAT_TYPE_FLOAT:
         return desc->channel[first_non_void].size == 16 ? PIPE_FORMAT_R16_FLOAT : PIPE_FORMAT_R32_FLOAT;
         break;
      default:
         return PIPE_FORMAT_NONE;
      }
   }
   return new_format;
}

VkFormat
zink_pipe_format_to_vk_format(enum pipe_format format)
{
   return formats[format];
}


bool
zink_format_is_voidable_rgba_variant(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   unsigned chan;

   if(desc->block.width != 1 ||
      desc->block.height != 1 ||
      (desc->block.bits != 32 && desc->block.bits != 64))
      return false;

   if (desc->nr_channels != 4)
      return false;

   unsigned size = desc->channel[0].size;
   for(chan = 0; chan < 4; ++chan) {
      if(desc->channel[chan].size != size)
         return false;
   }

   return true;
}
