/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 ***************************************************************************/

#include "swr_context.h"
#include "swr_public.h"
#include "swr_screen.h"
#include "swr_resource.h"
#include "swr_fence.h"
#include "gen_knobs.h"

#include "pipe/p_screen.h"
#include "pipe/p_defines.h"
#include "util/u_memory.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_cpu_detect.h"
#include "util/format/u_format_s3tc.h"
#include "util/u_string.h"
#include "util/u_screen.h"

#include "frontend/sw_winsys.h"

#include "jit_api.h"

#include "memory/TilingFunctions.h"

#include <stdio.h>
#include <map>

/*
 * Max texture sizes
 * XXX Check max texture size values against core and sampler.
 */
#define SWR_MAX_TEXTURE_SIZE (2 * 1024 * 1024 * 1024ULL) /* 2GB */
/* Not all texture formats can fit into 2GB limit, but we have to
   live with that. See lp_limits.h for more details */
#define SWR_MAX_TEXTURE_2D_SIZE 16384
#define SWR_MAX_TEXTURE_3D_LEVELS 12  /* 2K x 2K x 2K for now */
#define SWR_MAX_TEXTURE_CUBE_LEVELS 14  /* 8K x 8K for now */
#define SWR_MAX_TEXTURE_ARRAY_LAYERS 512 /* 8K x 512 / 8K x 8K x 512 */

/* Default max client_copy_limit */
#define SWR_CLIENT_COPY_LIMIT 8192

/* Flag indicates creation of alternate surface, to prevent recursive loop
 * in resource creation when msaa_force_enable is set. */
#define SWR_RESOURCE_FLAG_ALT_SURFACE (PIPE_RESOURCE_FLAG_DRV_PRIV << 0)


static const char *
swr_get_name(struct pipe_screen *screen)
{
   static char buf[100];
   snprintf(buf, sizeof(buf), "SWR (LLVM " MESA_LLVM_VERSION_STRING ", %u bits)",
            lp_native_vector_width);
   return buf;
}

static const char *
swr_get_vendor(struct pipe_screen *screen)
{
   return "Intel Corporation";
}

static bool
swr_is_format_supported(struct pipe_screen *_screen,
                        enum pipe_format format,
                        enum pipe_texture_target target,
                        unsigned sample_count,
                        unsigned storage_sample_count,
                        unsigned bind)
{
   struct swr_screen *screen = swr_screen(_screen);
   struct sw_winsys *winsys = screen->winsys;
   const struct util_format_description *format_desc;

   assert(target == PIPE_BUFFER || target == PIPE_TEXTURE_1D
          || target == PIPE_TEXTURE_1D_ARRAY
          || target == PIPE_TEXTURE_2D
          || target == PIPE_TEXTURE_2D_ARRAY
          || target == PIPE_TEXTURE_RECT
          || target == PIPE_TEXTURE_3D
          || target == PIPE_TEXTURE_CUBE
          || target == PIPE_TEXTURE_CUBE_ARRAY);

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   format_desc = util_format_description(format);
   if (!format_desc)
      return false;

   if ((sample_count > screen->msaa_max_count)
      || !util_is_power_of_two_or_zero(sample_count))
      return false;

   if (bind & PIPE_BIND_DISPLAY_TARGET) {
      if (!winsys->is_displaytarget_format_supported(winsys, bind, format))
         return false;
   }

   if (bind & PIPE_BIND_RENDER_TARGET) {
      if (format_desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS)
         return false;

      if (mesa_to_swr_format(format) == (SWR_FORMAT)-1)
         return false;

      /*
       * Although possible, it is unnatural to render into compressed or YUV
       * surfaces. So disable these here to avoid going into weird paths
       * inside gallium frontends.
       */
      if (format_desc->block.width != 1 || format_desc->block.height != 1)
         return false;
   }

   if (bind & PIPE_BIND_DEPTH_STENCIL) {
      if (format_desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
         return false;

      if (mesa_to_swr_format(format) == (SWR_FORMAT)-1)
         return false;
   }

   if (bind & PIPE_BIND_VERTEX_BUFFER) {
      if (mesa_to_swr_format(format) == (SWR_FORMAT)-1) {
         return false;
      }
   }

   if (format_desc->layout == UTIL_FORMAT_LAYOUT_ASTC ||
       format_desc->layout == UTIL_FORMAT_LAYOUT_FXT1)
   {
      return false;
   }

   if (format_desc->layout == UTIL_FORMAT_LAYOUT_ETC &&
       format != PIPE_FORMAT_ETC1_RGB8) {
      return false;
   }

   if ((bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW)) &&
       ((bind & PIPE_BIND_DISPLAY_TARGET) == 0)) {
      /* Disable all 3-channel formats, where channel size != 32 bits.
       * In some cases we run into crashes (in generate_unswizzled_blend()),
       * for 3-channel RGB16 variants, there was an apparent LLVM bug.
       * In any case, disabling the shallower 3-channel formats avoids a
       * number of issues with GL_ARB_copy_image support.
       */
      if (format_desc->is_array &&
          format_desc->nr_channels == 3 &&
          format_desc->block.bits != 96) {
         return false;
      }
   }

   return TRUE;
}

static int
swr_get_param(struct pipe_screen *screen, enum pipe_cap param)
{
   switch (param) {
      /* limits */
   case PIPE_CAP_MAX_RENDER_TARGETS:
      return PIPE_MAX_COLOR_BUFS;
   case PIPE_CAP_MAX_TEXTURE_2D_SIZE:
      return SWR_MAX_TEXTURE_2D_SIZE;
   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
      return SWR_MAX_TEXTURE_3D_LEVELS;
   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
      return SWR_MAX_TEXTURE_CUBE_LEVELS;
   case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
      return MAX_SO_STREAMS;
   case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
   case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
      return MAX_ATTRIBUTES * 4;
   case PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES:
   case PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS:
      return 1024;
   case PIPE_CAP_MAX_VERTEX_STREAMS:
      return 4;
   case PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE:
      return 2048;
   case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS:
      return SWR_MAX_TEXTURE_ARRAY_LAYERS;
   case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
   case PIPE_CAP_MIN_TEXEL_OFFSET:
      return -8;
   case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
   case PIPE_CAP_MAX_TEXEL_OFFSET:
      return 7;
   case PIPE_CAP_MAX_TEXTURE_GATHER_COMPONENTS:
      return 4;
   case PIPE_CAP_GLSL_FEATURE_LEVEL:
      return 330;
   case PIPE_CAP_GLSL_FEATURE_LEVEL_COMPATIBILITY:
      return 140;
   case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
      return 16;
   case PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT:
      return 64;
   case PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE:
      return 65536;
   case PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT:
      return 1;
   case PIPE_CAP_MAX_VIEWPORTS:
      return KNOB_NUM_VIEWPORTS_SCISSORS;
   case PIPE_CAP_ENDIANNESS:
      return PIPE_ENDIAN_NATIVE;

      /* supported features */
   case PIPE_CAP_NPOT_TEXTURES:
   case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
   case PIPE_CAP_MIXED_COLOR_DEPTH_BITS:
   case PIPE_CAP_FRAGMENT_SHADER_TEXTURE_LOD:
   case PIPE_CAP_FRAGMENT_SHADER_DERIVATIVES:
   case PIPE_CAP_VERTEX_SHADER_SATURATE:
   case PIPE_CAP_POINT_SPRITE:
   case PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS:
   case PIPE_CAP_OCCLUSION_QUERY:
   case PIPE_CAP_QUERY_TIME_ELAPSED:
   case PIPE_CAP_QUERY_PIPELINE_STATISTICS:
   case PIPE_CAP_TEXTURE_MIRROR_CLAMP:
   case PIPE_CAP_TEXTURE_MIRROR_CLAMP_TO_EDGE:
   case PIPE_CAP_TEXTURE_SWIZZLE:
   case PIPE_CAP_BLEND_EQUATION_SEPARATE:
   case PIPE_CAP_INDEP_BLEND_ENABLE:
   case PIPE_CAP_INDEP_BLEND_FUNC:
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER:
   case PIPE_CAP_DEPTH_CLIP_DISABLE:
   case PIPE_CAP_PRIMITIVE_RESTART:
   case PIPE_CAP_PRIMITIVE_RESTART_FIXED_INDEX:
   case PIPE_CAP_TGSI_INSTANCEID:
   case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
   case PIPE_CAP_START_INSTANCE:
   case PIPE_CAP_SEAMLESS_CUBE_MAP:
   case PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE:
   case PIPE_CAP_CONDITIONAL_RENDER:
   case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
   case PIPE_CAP_MIXED_COLORBUFFER_FORMATS:
   case PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION:
   case PIPE_CAP_USER_VERTEX_BUFFERS:
   case PIPE_CAP_STREAM_OUTPUT_INTERLEAVE_BUFFERS:
   case PIPE_CAP_QUERY_TIMESTAMP:
   case PIPE_CAP_TEXTURE_BUFFER_OBJECTS:
   case PIPE_CAP_BUFFER_MAP_PERSISTENT_COHERENT:
   case PIPE_CAP_DRAW_INDIRECT:
   case PIPE_CAP_UMA:
   case PIPE_CAP_CONDITIONAL_RENDER_INVERTED:
   case PIPE_CAP_CLIP_HALFZ:
   case PIPE_CAP_POLYGON_OFFSET_CLAMP:
   case PIPE_CAP_DEPTH_BOUNDS_TEST:
   case PIPE_CAP_CLEAR_TEXTURE:
   case PIPE_CAP_TEXTURE_FLOAT_LINEAR:
   case PIPE_CAP_TEXTURE_HALF_FLOAT_LINEAR:
   case PIPE_CAP_CULL_DISTANCE:
   case PIPE_CAP_CUBE_MAP_ARRAY:
   case PIPE_CAP_DOUBLES:
   case PIPE_CAP_TEXTURE_QUERY_LOD:
   case PIPE_CAP_COPY_BETWEEN_COMPRESSED_AND_PLAIN_FORMATS:
   case PIPE_CAP_TGSI_TG4_COMPONENT_IN_SWIZZLE:
   case PIPE_CAP_QUERY_SO_OVERFLOW:
   case PIPE_CAP_STREAM_OUTPUT_PAUSE_RESUME:
      return 1;

   case PIPE_CAP_SHAREABLE_SHADERS:
      return 0;

   /* MSAA support
    * If user has explicitly set max_sample_count = 1 (via SWR_MSAA_MAX_COUNT)
    * then disable all MSAA support and go back to old (FAKE_SW_MSAA) caps. */
   case PIPE_CAP_TEXTURE_MULTISAMPLE:
   case PIPE_CAP_MULTISAMPLE_Z_RESOLVE:
      return (swr_screen(screen)->msaa_max_count > 1) ? 1 : 0;
   case PIPE_CAP_FAKE_SW_MSAA:
      return (swr_screen(screen)->msaa_max_count > 1) ? 0 : 1;

   /* fetch jit change for 2-4GB buffers requires alignment */
   case PIPE_CAP_VERTEX_BUFFER_OFFSET_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_ELEMENT_SRC_OFFSET_4BYTE_ALIGNED_ONLY:
      return 1;

      /* unsupported features */
   case PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER:
   case PIPE_CAP_PCI_GROUP:
   case PIPE_CAP_PCI_BUS:
   case PIPE_CAP_PCI_DEVICE:
   case PIPE_CAP_PCI_FUNCTION:
   case PIPE_CAP_GLSL_OPTIMIZE_CONSERVATIVELY:
      return 0;
   case PIPE_CAP_MAX_GS_INVOCATIONS:
      return 32;
   case PIPE_CAP_MAX_SHADER_BUFFER_SIZE:
      return 1 << 27;
   case PIPE_CAP_MAX_VARYINGS:
      return 32;

   case PIPE_CAP_VENDOR_ID:
      return 0xFFFFFFFF;
   case PIPE_CAP_DEVICE_ID:
      return 0xFFFFFFFF;
   case PIPE_CAP_ACCELERATED:
      return 0;
   case PIPE_CAP_VIDEO_MEMORY: {
      /* XXX: Do we want to return the full amount of system memory ? */
      uint64_t system_memory;

      if (!os_get_total_physical_memory(&system_memory))
         return 0;

      return (int)(system_memory >> 20);
   }
   default:
      return u_pipe_screen_get_param_defaults(screen, param);
   }
}

static int
swr_get_shader_param(struct pipe_screen *screen,
                     enum pipe_shader_type shader,
                     enum pipe_shader_cap param)
{
   if (shader != PIPE_SHADER_VERTEX &&
       shader != PIPE_SHADER_FRAGMENT &&
       shader != PIPE_SHADER_GEOMETRY &&
       shader != PIPE_SHADER_TESS_CTRL &&
       shader != PIPE_SHADER_TESS_EVAL)
      return 0;

   if (param == PIPE_SHADER_CAP_MAX_SHADER_BUFFERS ||
       param == PIPE_SHADER_CAP_MAX_SHADER_IMAGES) {
      return 0;
   }

   return gallivm_get_shader_param(param);
}


static float
swr_get_paramf(struct pipe_screen *screen, enum pipe_capf param)
{
   switch (param) {
   case PIPE_CAPF_MAX_LINE_WIDTH:
   case PIPE_CAPF_MAX_LINE_WIDTH_AA:
   case PIPE_CAPF_MAX_POINT_WIDTH:
      return 255.0; /* arbitrary */
   case PIPE_CAPF_MAX_POINT_WIDTH_AA:
      return 0.0;
   case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
      return 0.0;
   case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
      return 16.0; /* arbitrary */
   case PIPE_CAPF_MIN_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_MAX_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_CONSERVATIVE_RASTER_DILATE_GRANULARITY:
      return 0.0f;
   }
   /* should only get here on unhandled cases */
   debug_printf("Unexpected PIPE_CAPF %d query\n", param);
   return 0.0;
}

SWR_FORMAT
mesa_to_swr_format(enum pipe_format format)
{
   static const std::map<pipe_format,SWR_FORMAT> mesa2swr = {
      /* depth / stencil */
      {PIPE_FORMAT_Z16_UNORM,              R16_UNORM}, // z
      {PIPE_FORMAT_Z32_FLOAT,              R32_FLOAT}, // z
      {PIPE_FORMAT_Z24_UNORM_S8_UINT,      R24_UNORM_X8_TYPELESS}, // z
      {PIPE_FORMAT_Z24X8_UNORM,            R24_UNORM_X8_TYPELESS}, // z
      {PIPE_FORMAT_Z32_FLOAT_S8X24_UINT,   R32_FLOAT_X8X24_TYPELESS}, // z

      /* alpha */
      {PIPE_FORMAT_A8_UNORM,               A8_UNORM},
      {PIPE_FORMAT_A16_UNORM,              A16_UNORM},
      {PIPE_FORMAT_A16_FLOAT,              A16_FLOAT},
      {PIPE_FORMAT_A32_FLOAT,              A32_FLOAT},

      /* odd sizes, bgr */
      {PIPE_FORMAT_B5G6R5_UNORM,           B5G6R5_UNORM},
      {PIPE_FORMAT_B5G6R5_SRGB,            B5G6R5_UNORM_SRGB},
      {PIPE_FORMAT_B5G5R5A1_UNORM,         B5G5R5A1_UNORM},
      {PIPE_FORMAT_B5G5R5X1_UNORM,         B5G5R5X1_UNORM},
      {PIPE_FORMAT_B4G4R4A4_UNORM,         B4G4R4A4_UNORM},
      {PIPE_FORMAT_B8G8R8A8_UNORM,         B8G8R8A8_UNORM},
      {PIPE_FORMAT_B8G8R8A8_SRGB,          B8G8R8A8_UNORM_SRGB},
      {PIPE_FORMAT_B8G8R8X8_UNORM,         B8G8R8X8_UNORM},
      {PIPE_FORMAT_B8G8R8X8_SRGB,          B8G8R8X8_UNORM_SRGB},

      /* rgb10a2 */
      {PIPE_FORMAT_R10G10B10A2_UNORM,      R10G10B10A2_UNORM},
      {PIPE_FORMAT_R10G10B10A2_SNORM,      R10G10B10A2_SNORM},
      {PIPE_FORMAT_R10G10B10A2_USCALED,    R10G10B10A2_USCALED},
      {PIPE_FORMAT_R10G10B10A2_SSCALED,    R10G10B10A2_SSCALED},
      {PIPE_FORMAT_R10G10B10A2_UINT,       R10G10B10A2_UINT},

      /* rgb10x2 */
      {PIPE_FORMAT_R10G10B10X2_USCALED,    R10G10B10X2_USCALED},

      /* bgr10a2 */
      {PIPE_FORMAT_B10G10R10A2_UNORM,      B10G10R10A2_UNORM},
      {PIPE_FORMAT_B10G10R10A2_SNORM,      B10G10R10A2_SNORM},
      {PIPE_FORMAT_B10G10R10A2_USCALED,    B10G10R10A2_USCALED},
      {PIPE_FORMAT_B10G10R10A2_SSCALED,    B10G10R10A2_SSCALED},
      {PIPE_FORMAT_B10G10R10A2_UINT,       B10G10R10A2_UINT},

      /* bgr10x2 */
      {PIPE_FORMAT_B10G10R10X2_UNORM,      B10G10R10X2_UNORM},

      /* r11g11b10 */
      {PIPE_FORMAT_R11G11B10_FLOAT,        R11G11B10_FLOAT},

      /* 32 bits per component */
      {PIPE_FORMAT_R32_FLOAT,              R32_FLOAT},
      {PIPE_FORMAT_R32G32_FLOAT,           R32G32_FLOAT},
      {PIPE_FORMAT_R32G32B32_FLOAT,        R32G32B32_FLOAT},
      {PIPE_FORMAT_R32G32B32A32_FLOAT,     R32G32B32A32_FLOAT},
      {PIPE_FORMAT_R32G32B32X32_FLOAT,     R32G32B32X32_FLOAT},

      {PIPE_FORMAT_R32_USCALED,            R32_USCALED},
      {PIPE_FORMAT_R32G32_USCALED,         R32G32_USCALED},
      {PIPE_FORMAT_R32G32B32_USCALED,      R32G32B32_USCALED},
      {PIPE_FORMAT_R32G32B32A32_USCALED,   R32G32B32A32_USCALED},

      {PIPE_FORMAT_R32_SSCALED,            R32_SSCALED},
      {PIPE_FORMAT_R32G32_SSCALED,         R32G32_SSCALED},
      {PIPE_FORMAT_R32G32B32_SSCALED,      R32G32B32_SSCALED},
      {PIPE_FORMAT_R32G32B32A32_SSCALED,   R32G32B32A32_SSCALED},

      {PIPE_FORMAT_R32_UINT,               R32_UINT},
      {PIPE_FORMAT_R32G32_UINT,            R32G32_UINT},
      {PIPE_FORMAT_R32G32B32_UINT,         R32G32B32_UINT},
      {PIPE_FORMAT_R32G32B32A32_UINT,      R32G32B32A32_UINT},

      {PIPE_FORMAT_R32_SINT,               R32_SINT},
      {PIPE_FORMAT_R32G32_SINT,            R32G32_SINT},
      {PIPE_FORMAT_R32G32B32_SINT,         R32G32B32_SINT},
      {PIPE_FORMAT_R32G32B32A32_SINT,      R32G32B32A32_SINT},

      /* 16 bits per component */
      {PIPE_FORMAT_R16_UNORM,              R16_UNORM},
      {PIPE_FORMAT_R16G16_UNORM,           R16G16_UNORM},
      {PIPE_FORMAT_R16G16B16_UNORM,        R16G16B16_UNORM},
      {PIPE_FORMAT_R16G16B16A16_UNORM,     R16G16B16A16_UNORM},
      {PIPE_FORMAT_R16G16B16X16_UNORM,     R16G16B16X16_UNORM},

      {PIPE_FORMAT_R16_USCALED,            R16_USCALED},
      {PIPE_FORMAT_R16G16_USCALED,         R16G16_USCALED},
      {PIPE_FORMAT_R16G16B16_USCALED,      R16G16B16_USCALED},
      {PIPE_FORMAT_R16G16B16A16_USCALED,   R16G16B16A16_USCALED},

      {PIPE_FORMAT_R16_SNORM,              R16_SNORM},
      {PIPE_FORMAT_R16G16_SNORM,           R16G16_SNORM},
      {PIPE_FORMAT_R16G16B16_SNORM,        R16G16B16_SNORM},
      {PIPE_FORMAT_R16G16B16A16_SNORM,     R16G16B16A16_SNORM},

      {PIPE_FORMAT_R16_SSCALED,            R16_SSCALED},
      {PIPE_FORMAT_R16G16_SSCALED,         R16G16_SSCALED},
      {PIPE_FORMAT_R16G16B16_SSCALED,      R16G16B16_SSCALED},
      {PIPE_FORMAT_R16G16B16A16_SSCALED,   R16G16B16A16_SSCALED},

      {PIPE_FORMAT_R16_UINT,               R16_UINT},
      {PIPE_FORMAT_R16G16_UINT,            R16G16_UINT},
      {PIPE_FORMAT_R16G16B16_UINT,         R16G16B16_UINT},
      {PIPE_FORMAT_R16G16B16A16_UINT,      R16G16B16A16_UINT},

      {PIPE_FORMAT_R16_SINT,               R16_SINT},
      {PIPE_FORMAT_R16G16_SINT,            R16G16_SINT},
      {PIPE_FORMAT_R16G16B16_SINT,         R16G16B16_SINT},
      {PIPE_FORMAT_R16G16B16A16_SINT,      R16G16B16A16_SINT},

      {PIPE_FORMAT_R16_FLOAT,              R16_FLOAT},
      {PIPE_FORMAT_R16G16_FLOAT,           R16G16_FLOAT},
      {PIPE_FORMAT_R16G16B16_FLOAT,        R16G16B16_FLOAT},
      {PIPE_FORMAT_R16G16B16A16_FLOAT,     R16G16B16A16_FLOAT},
      {PIPE_FORMAT_R16G16B16X16_FLOAT,     R16G16B16X16_FLOAT},

      /* 8 bits per component */
      {PIPE_FORMAT_R8_UNORM,               R8_UNORM},
      {PIPE_FORMAT_R8G8_UNORM,             R8G8_UNORM},
      {PIPE_FORMAT_R8G8B8_UNORM,           R8G8B8_UNORM},
      {PIPE_FORMAT_R8G8B8_SRGB,            R8G8B8_UNORM_SRGB},
      {PIPE_FORMAT_R8G8B8A8_UNORM,         R8G8B8A8_UNORM},
      {PIPE_FORMAT_R8G8B8A8_SRGB,          R8G8B8A8_UNORM_SRGB},
      {PIPE_FORMAT_R8G8B8X8_UNORM,         R8G8B8X8_UNORM},
      {PIPE_FORMAT_R8G8B8X8_SRGB,          R8G8B8X8_UNORM_SRGB},

      {PIPE_FORMAT_R8_USCALED,             R8_USCALED},
      {PIPE_FORMAT_R8G8_USCALED,           R8G8_USCALED},
      {PIPE_FORMAT_R8G8B8_USCALED,         R8G8B8_USCALED},
      {PIPE_FORMAT_R8G8B8A8_USCALED,       R8G8B8A8_USCALED},

      {PIPE_FORMAT_R8_SNORM,               R8_SNORM},
      {PIPE_FORMAT_R8G8_SNORM,             R8G8_SNORM},
      {PIPE_FORMAT_R8G8B8_SNORM,           R8G8B8_SNORM},
      {PIPE_FORMAT_R8G8B8A8_SNORM,         R8G8B8A8_SNORM},

      {PIPE_FORMAT_R8_SSCALED,             R8_SSCALED},
      {PIPE_FORMAT_R8G8_SSCALED,           R8G8_SSCALED},
      {PIPE_FORMAT_R8G8B8_SSCALED,         R8G8B8_SSCALED},
      {PIPE_FORMAT_R8G8B8A8_SSCALED,       R8G8B8A8_SSCALED},

      {PIPE_FORMAT_R8_UINT,                R8_UINT},
      {PIPE_FORMAT_R8G8_UINT,              R8G8_UINT},
      {PIPE_FORMAT_R8G8B8_UINT,            R8G8B8_UINT},
      {PIPE_FORMAT_R8G8B8A8_UINT,          R8G8B8A8_UINT},

      {PIPE_FORMAT_R8_SINT,                R8_SINT},
      {PIPE_FORMAT_R8G8_SINT,              R8G8_SINT},
      {PIPE_FORMAT_R8G8B8_SINT,            R8G8B8_SINT},
      {PIPE_FORMAT_R8G8B8A8_SINT,          R8G8B8A8_SINT},

      /* These formats are valid for vertex data, but should not be used
       * for render targets.
       */

      {PIPE_FORMAT_R32_FIXED,              R32_SFIXED},
      {PIPE_FORMAT_R32G32_FIXED,           R32G32_SFIXED},
      {PIPE_FORMAT_R32G32B32_FIXED,        R32G32B32_SFIXED},
      {PIPE_FORMAT_R32G32B32A32_FIXED,     R32G32B32A32_SFIXED},

      {PIPE_FORMAT_R64_FLOAT,              R64_FLOAT},
      {PIPE_FORMAT_R64G64_FLOAT,           R64G64_FLOAT},
      {PIPE_FORMAT_R64G64B64_FLOAT,        R64G64B64_FLOAT},
      {PIPE_FORMAT_R64G64B64A64_FLOAT,     R64G64B64A64_FLOAT},

      /* These formats have entries in SWR but don't have Load/StoreTile
       * implementations. That means these aren't renderable, and thus having
       * a mapping entry here is detrimental.
       */
      /*

      {PIPE_FORMAT_L8_UNORM,               L8_UNORM},
      {PIPE_FORMAT_I8_UNORM,               I8_UNORM},
      {PIPE_FORMAT_L8A8_UNORM,             L8A8_UNORM},
      {PIPE_FORMAT_L16_UNORM,              L16_UNORM},
      {PIPE_FORMAT_UYVY,                   YCRCB_SWAPUVY},

      {PIPE_FORMAT_L8_SRGB,                L8_UNORM_SRGB},
      {PIPE_FORMAT_L8A8_SRGB,              L8A8_UNORM_SRGB},

      {PIPE_FORMAT_DXT1_RGBA,              BC1_UNORM},
      {PIPE_FORMAT_DXT3_RGBA,              BC2_UNORM},
      {PIPE_FORMAT_DXT5_RGBA,              BC3_UNORM},

      {PIPE_FORMAT_DXT1_SRGBA,             BC1_UNORM_SRGB},
      {PIPE_FORMAT_DXT3_SRGBA,             BC2_UNORM_SRGB},
      {PIPE_FORMAT_DXT5_SRGBA,             BC3_UNORM_SRGB},

      {PIPE_FORMAT_RGTC1_UNORM,            BC4_UNORM},
      {PIPE_FORMAT_RGTC1_SNORM,            BC4_SNORM},
      {PIPE_FORMAT_RGTC2_UNORM,            BC5_UNORM},
      {PIPE_FORMAT_RGTC2_SNORM,            BC5_SNORM},

      {PIPE_FORMAT_L16A16_UNORM,           L16A16_UNORM},
      {PIPE_FORMAT_I16_UNORM,              I16_UNORM},
      {PIPE_FORMAT_L16_FLOAT,              L16_FLOAT},
      {PIPE_FORMAT_L16A16_FLOAT,           L16A16_FLOAT},
      {PIPE_FORMAT_I16_FLOAT,              I16_FLOAT},
      {PIPE_FORMAT_L32_FLOAT,              L32_FLOAT},
      {PIPE_FORMAT_L32A32_FLOAT,           L32A32_FLOAT},
      {PIPE_FORMAT_I32_FLOAT,              I32_FLOAT},

      {PIPE_FORMAT_I8_UINT,                I8_UINT},
      {PIPE_FORMAT_L8_UINT,                L8_UINT},
      {PIPE_FORMAT_L8A8_UINT,              L8A8_UINT},

      {PIPE_FORMAT_I8_SINT,                I8_SINT},
      {PIPE_FORMAT_L8_SINT,                L8_SINT},
      {PIPE_FORMAT_L8A8_SINT,              L8A8_SINT},

      */
   };

   auto it = mesa2swr.find(format);
   if (it == mesa2swr.end())
      return (SWR_FORMAT)-1;
   else
      return it->second;
}

static bool
swr_displaytarget_layout(struct swr_screen *screen, struct swr_resource *res)
{
   struct sw_winsys *winsys = screen->winsys;
   struct sw_displaytarget *dt;

   const unsigned width = align(res->swr.width, res->swr.halign);
   const unsigned height = align(res->swr.height, res->swr.valign);

   UINT stride;
   dt = winsys->displaytarget_create(winsys,
                                     res->base.bind,
                                     res->base.format,
                                     width, height,
                                     64, NULL,
                                     &stride);

   if (dt == NULL)
      return false;

   void *map = winsys->displaytarget_map(winsys, dt, 0);

   res->display_target = dt;
   res->swr.xpBaseAddress = (gfxptr_t)map;

   /* Clear the display target surface */
   if (map)
      memset(map, 0, height * stride);

   winsys->displaytarget_unmap(winsys, dt);

   return true;
}

static bool
swr_texture_layout(struct swr_screen *screen,
                   struct swr_resource *res,
                   bool allocate)
{
   struct pipe_resource *pt = &res->base;

   pipe_format fmt = pt->format;
   const struct util_format_description *desc = util_format_description(fmt);

   res->has_depth = util_format_has_depth(desc);
   res->has_stencil = util_format_has_stencil(desc);

   if (res->has_stencil && !res->has_depth)
      fmt = PIPE_FORMAT_R8_UINT;

   /* We always use the SWR layout. For 2D and 3D textures this looks like:
    *
    * |<------- pitch ------->|
    * +=======================+-------
    * |Array 0                |   ^
    * |                       |   |
    * |        Level 0        |   |
    * |                       |   |
    * |                       | qpitch
    * +-----------+-----------+   |
    * |           | L2L2L2L2  |   |
    * |  Level 1  | L3L3      |   |
    * |           | L4        |   v
    * +===========+===========+-------
    * |Array 1                |
    * |                       |
    * |        Level 0        |
    * |                       |
    * |                       |
    * +-----------+-----------+
    * |           | L2L2L2L2  |
    * |  Level 1  | L3L3      |
    * |           | L4        |
    * +===========+===========+
    *
    * The overall width in bytes is known as the pitch, while the overall
    * height in rows is the qpitch. Array slices are laid out logically below
    * one another, qpitch rows apart. For 3D surfaces, the "level" values are
    * just invalid for the higher array numbers (since depth is also
    * minified). 1D and 1D array surfaces are stored effectively the same way,
    * except that pitch never plays into it. All the levels are logically
    * adjacent to each other on the X axis. The qpitch becomes the number of
    * elements between array slices, while the pitch is unused.
    *
    * Each level's sizes are subject to the valign and halign settings of the
    * surface. For compressed formats that swr is unaware of, we will use an
    * appropriately-sized uncompressed format, and scale the widths/heights.
    *
    * This surface is stored inside res->swr. For depth/stencil textures,
    * res->secondary will have an identically-laid-out but R8_UINT-formatted
    * stencil tree. In the Z32F_S8 case, the primary surface still has 64-bpp
    * texels, to simplify map/unmap logic which copies the stencil values
    * in/out.
    */

   res->swr.width = pt->width0;
   res->swr.height = pt->height0;
   res->swr.type = swr_convert_target_type(pt->target);
   res->swr.tileMode = SWR_TILE_NONE;
   res->swr.format = mesa_to_swr_format(fmt);
   res->swr.numSamples = std::max(1u, pt->nr_samples);

   if (pt->bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DEPTH_STENCIL)) {
      res->swr.halign = KNOB_MACROTILE_X_DIM;
      res->swr.valign = KNOB_MACROTILE_Y_DIM;

      /* If SWR_MSAA_FORCE_ENABLE is set, turn on MSAA and override requested
       * surface sample count. */
      if (screen->msaa_force_enable) {
         res->swr.numSamples = screen->msaa_max_count;
         swr_print_info("swr_texture_layout: forcing sample count: %d\n",
                 res->swr.numSamples);
      }
   } else {
      res->swr.halign = 1;
      res->swr.valign = 1;
   }

   unsigned halign = res->swr.halign * util_format_get_blockwidth(fmt);
   unsigned width = align(pt->width0, halign);
   if (pt->target == PIPE_TEXTURE_1D || pt->target == PIPE_TEXTURE_1D_ARRAY) {
      for (int level = 1; level <= pt->last_level; level++)
         width += align(u_minify(pt->width0, level), halign);
      res->swr.pitch = util_format_get_blocksize(fmt);
      res->swr.qpitch = util_format_get_nblocksx(fmt, width);
   } else {
      // The pitch is the overall width of the texture in bytes. Most of the
      // time this is the pitch of level 0 since all the other levels fit
      // underneath it. However in some degenerate situations, the width of
      // level1 + level2 may be larger. In that case, we use those
      // widths. This can happen if, e.g. halign is 32, and the width of level
      // 0 is 32 or less. In that case, the aligned levels 1 and 2 will also
      // be 32 each, adding up to 64.
      unsigned valign = res->swr.valign * util_format_get_blockheight(fmt);
      if (pt->last_level > 1) {
         width = std::max<uint32_t>(
               width,
               align(u_minify(pt->width0, 1), halign) +
               align(u_minify(pt->width0, 2), halign));
      }
      res->swr.pitch = util_format_get_stride(fmt, width);

      // The qpitch is controlled by either the height of the second LOD, or
      // the combination of all the later LODs.
      unsigned height = align(pt->height0, valign);
      if (pt->last_level == 1) {
         height += align(u_minify(pt->height0, 1), valign);
      } else if (pt->last_level > 1) {
         unsigned level1 = align(u_minify(pt->height0, 1), valign);
         unsigned level2 = 0;
         for (int level = 2; level <= pt->last_level; level++) {
            level2 += align(u_minify(pt->height0, level), valign);
         }
         height += std::max(level1, level2);
      }
      res->swr.qpitch = util_format_get_nblocksy(fmt, height);
   }

   if (pt->target == PIPE_TEXTURE_3D)
      res->swr.depth = pt->depth0;
   else
      res->swr.depth = pt->array_size;

   // Fix up swr format if necessary so that LOD offset computation works
   if (res->swr.format == (SWR_FORMAT)-1) {
      switch (util_format_get_blocksize(fmt)) {
      default:
         unreachable("Unexpected format block size");
      case 1: res->swr.format = R8_UINT; break;
      case 2: res->swr.format = R16_UINT; break;
      case 4: res->swr.format = R32_UINT; break;
      case 8:
         if (util_format_is_compressed(fmt))
            res->swr.format = BC4_UNORM;
         else
            res->swr.format = R32G32_UINT;
         break;
      case 16:
         if (util_format_is_compressed(fmt))
            res->swr.format = BC5_UNORM;
         else
            res->swr.format = R32G32B32A32_UINT;
         break;
      }
   }

   for (int level = 0; level <= pt->last_level; level++) {
      res->mip_offsets[level] =
         ComputeSurfaceOffset<false>(0, 0, 0, 0, 0, level, &res->swr);
   }

   size_t total_size = (uint64_t)res->swr.depth * res->swr.qpitch *
                                 res->swr.pitch * res->swr.numSamples;

   // Let non-sampled textures (e.g. buffer objects) bypass the size limit
   if (swr_resource_is_texture(&res->base) && total_size > SWR_MAX_TEXTURE_SIZE)
      return false;

   if (allocate) {
      res->swr.xpBaseAddress = (gfxptr_t)AlignedMalloc(total_size, 64);
      if (!res->swr.xpBaseAddress)
         return false;

      if (res->has_depth && res->has_stencil) {
         res->secondary = res->swr;
         res->secondary.format = R8_UINT;
         res->secondary.pitch = res->swr.pitch / util_format_get_blocksize(fmt);

         for (int level = 0; level <= pt->last_level; level++) {
            res->secondary_mip_offsets[level] =
               ComputeSurfaceOffset<false>(0, 0, 0, 0, 0, level, &res->secondary);
         }

         total_size = res->secondary.depth * res->secondary.qpitch *
                      res->secondary.pitch * res->secondary.numSamples;

         res->secondary.xpBaseAddress = (gfxptr_t) AlignedMalloc(total_size, 64);
         if (!res->secondary.xpBaseAddress) {
            AlignedFree((void *)res->swr.xpBaseAddress);
            return false;
         }
      }
   }

   return true;
}

static bool
swr_can_create_resource(struct pipe_screen *screen,
                        const struct pipe_resource *templat)
{
   struct swr_resource res;
   memset(&res, 0, sizeof(res));
   res.base = *templat;
   return swr_texture_layout(swr_screen(screen), &res, false);
}

/* Helper function that conditionally creates a single-sample resolve resource
 * and attaches it to main multisample resource. */
static bool
swr_create_resolve_resource(struct pipe_screen *_screen,
                            struct swr_resource *msaa_res)
{
   struct swr_screen *screen = swr_screen(_screen);

   /* If resource is multisample, create a single-sample resolve resource */
   if (msaa_res->base.nr_samples > 1 || (screen->msaa_force_enable &&
            !(msaa_res->base.flags & SWR_RESOURCE_FLAG_ALT_SURFACE))) {

      /* Create a single-sample copy of the resource.  Copy the original
       * resource parameters and set flag to prevent recursion when re-calling
       * resource_create */
      struct pipe_resource alt_template = msaa_res->base;
      alt_template.nr_samples = 0;
      alt_template.flags |= SWR_RESOURCE_FLAG_ALT_SURFACE;

      /* Note: Display_target is a special single-sample resource, only the
       * display_target has been created already. */
      if (msaa_res->base.bind & (PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT
               | PIPE_BIND_SHARED)) {
         /* Allocate the multisample buffers. */
         if (!swr_texture_layout(screen, msaa_res, true))
            return false;

         /* Alt resource will only be bound as PIPE_BIND_RENDER_TARGET
          * remove the DISPLAY_TARGET, SCANOUT, and SHARED bindings */
         alt_template.bind = PIPE_BIND_RENDER_TARGET;
      }

      /* Allocate single-sample resolve surface */
      struct pipe_resource *alt;
      alt = _screen->resource_create(_screen, &alt_template);
      if (!alt)
         return false;

      /* Attach it to the multisample resource */
      msaa_res->resolve_target = alt;

      /* Hang resolve surface state off the multisample surface state to so
       * StoreTiles knows where to resolve the surface. */
      msaa_res->swr.xpAuxBaseAddress = (gfxptr_t)&swr_resource(alt)->swr;
   }

   return true; /* success */
}

static struct pipe_resource *
swr_resource_create(struct pipe_screen *_screen,
                    const struct pipe_resource *templat)
{
   struct swr_screen *screen = swr_screen(_screen);
   struct swr_resource *res = CALLOC_STRUCT(swr_resource);
   if (!res)
      return NULL;

   res->base = *templat;
   pipe_reference_init(&res->base.reference, 1);
   res->base.screen = &screen->base;

   if (swr_resource_is_texture(&res->base)) {
      if (res->base.bind & (PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT
                            | PIPE_BIND_SHARED)) {
         /* displayable surface
          * first call swr_texture_layout without allocating to finish
          * filling out the SWR_SURFACE_STATE in res */
         swr_texture_layout(screen, res, false);
         if (!swr_displaytarget_layout(screen, res))
            goto fail;
      } else {
         /* texture map */
         if (!swr_texture_layout(screen, res, true))
            goto fail;
      }

      /* If resource was multisample, create resolve resource and attach
       * it to multisample resource. */
      if (!swr_create_resolve_resource(_screen, res))
            goto fail;

   } else {
      /* other data (vertex buffer, const buffer, etc) */
      assert(util_format_get_blocksize(templat->format) == 1);
      assert(templat->height0 == 1);
      assert(templat->depth0 == 1);
      assert(templat->last_level == 0);

      /* Easiest to just call swr_texture_layout, as it sets up
       * SWR_SURFACE_STATE in res */
      if (!swr_texture_layout(screen, res, true))
         goto fail;
   }

   return &res->base;

fail:
   FREE(res);
   return NULL;
}

static void
swr_resource_destroy(struct pipe_screen *p_screen, struct pipe_resource *pt)
{
   struct swr_screen *screen = swr_screen(p_screen);
   struct swr_resource *spr = swr_resource(pt);

   if (spr->display_target) {
      /* If resource is display target, winsys manages the buffer and will
       * free it on displaytarget_destroy. */
      swr_fence_finish(p_screen, NULL, screen->flush_fence, 0);

      struct sw_winsys *winsys = screen->winsys;
      winsys->displaytarget_destroy(winsys, spr->display_target);

      if (spr->swr.numSamples > 1) {
         /* Free an attached resolve resource */
         struct swr_resource *alt = swr_resource(spr->resolve_target);
         swr_fence_work_free(screen->flush_fence, (void*)(alt->swr.xpBaseAddress), true);

         /* Free multisample buffer */
         swr_fence_work_free(screen->flush_fence, (void*)(spr->swr.xpBaseAddress), true);
      }
   } else {
      /* For regular resources, defer deletion */
      swr_resource_unused(pt);

      if (spr->swr.numSamples > 1) {
         /* Free an attached resolve resource */
         struct swr_resource *alt = swr_resource(spr->resolve_target);
         swr_fence_work_free(screen->flush_fence, (void*)(alt->swr.xpBaseAddress), true);
      }

      swr_fence_work_free(screen->flush_fence, (void*)(spr->swr.xpBaseAddress), true);
      swr_fence_work_free(screen->flush_fence,
                          (void*)(spr->secondary.xpBaseAddress), true);

      /* If work queue grows too large, submit a fence to force queue to
       * drain.  This is mainly to decrease the amount of memory used by the
       * piglit streaming-texture-leak test */
      if (screen->pipe && swr_fence(screen->flush_fence)->work.count > 64)
         swr_fence_submit(swr_context(screen->pipe), screen->flush_fence);
   }

   FREE(spr);
}


static void
swr_flush_frontbuffer(struct pipe_screen *p_screen,
                      struct pipe_context *pipe,
                      struct pipe_resource *resource,
                      unsigned level,
                      unsigned layer,
                      void *context_private,
                      struct pipe_box *sub_box)
{
   struct swr_screen *screen = swr_screen(p_screen);
   struct sw_winsys *winsys = screen->winsys;
   struct swr_resource *spr = swr_resource(resource);
   struct swr_context *ctx = swr_context(pipe);

   if (pipe) {
      swr_fence_finish(p_screen, NULL, screen->flush_fence, 0);
      swr_resource_unused(resource);
      ctx->api.pfnSwrEndFrame(ctx->swrContext);
   }

   /* Multisample resolved into resolve_target at flush with store_resource */
   if (pipe && spr->swr.numSamples > 1) {
      struct pipe_resource *resolve_target = spr->resolve_target;

      /* Once resolved, copy into display target */
      SWR_SURFACE_STATE *resolve = &swr_resource(resolve_target)->swr;

      void *map = winsys->displaytarget_map(winsys, spr->display_target,
                                            PIPE_MAP_WRITE);
      memcpy(map, (void*)(resolve->xpBaseAddress), resolve->pitch * resolve->height);
      winsys->displaytarget_unmap(winsys, spr->display_target);
   }

   debug_assert(spr->display_target);
   if (spr->display_target)
      winsys->displaytarget_display(
         winsys, spr->display_target, context_private, sub_box);
}


void
swr_destroy_screen_internal(struct swr_screen **screen)
{
   struct pipe_screen *p_screen = &(*screen)->base;

   swr_fence_finish(p_screen, NULL, (*screen)->flush_fence, 0);
   swr_fence_reference(p_screen, &(*screen)->flush_fence, NULL);

   JitDestroyContext((*screen)->hJitMgr);

   if ((*screen)->pLibrary)
      util_dl_close((*screen)->pLibrary);

   FREE(*screen);
   *screen = NULL;
}


static void
swr_destroy_screen(struct pipe_screen *p_screen)
{
   struct swr_screen *screen = swr_screen(p_screen);
   struct sw_winsys *winsys = screen->winsys;

   swr_print_info("SWR destroy screen!\n");

   if (winsys->destroy)
      winsys->destroy(winsys);

   swr_destroy_screen_internal(&screen);
}


static void
swr_validate_env_options(struct swr_screen *screen)
{
   /* The client_copy_limit sets a maximum on the amount of user-buffer memory
    * copied to scratch space on a draw.  Past this, the draw will access
    * user-buffer directly and then block.  This is faster than queuing many
    * large client draws. */
   screen->client_copy_limit = SWR_CLIENT_COPY_LIMIT;
   int client_copy_limit =
      debug_get_num_option("SWR_CLIENT_COPY_LIMIT", SWR_CLIENT_COPY_LIMIT);
   if (client_copy_limit > 0)
      screen->client_copy_limit = client_copy_limit;

   /* XXX msaa under development, disable by default for now */
   screen->msaa_max_count = 1; /* was SWR_MAX_NUM_MULTISAMPLES; */

   /* validate env override values, within range and power of 2 */
   int msaa_max_count = debug_get_num_option("SWR_MSAA_MAX_COUNT", 1);
   if (msaa_max_count != 1) {
      if ((msaa_max_count < 1) || (msaa_max_count > SWR_MAX_NUM_MULTISAMPLES)
            || !util_is_power_of_two_or_zero(msaa_max_count)) {
         fprintf(stderr, "SWR_MSAA_MAX_COUNT invalid: %d\n", msaa_max_count);
         fprintf(stderr, "must be power of 2 between 1 and %d" \
                         " (or 1 to disable msaa)\n",
               SWR_MAX_NUM_MULTISAMPLES);
         fprintf(stderr, "(msaa disabled)\n");
         msaa_max_count = 1;
      }

      swr_print_info("SWR_MSAA_MAX_COUNT: %d\n", msaa_max_count);

      screen->msaa_max_count = msaa_max_count;
   }

   screen->msaa_force_enable = debug_get_bool_option(
         "SWR_MSAA_FORCE_ENABLE", false);
   if (screen->msaa_force_enable)
      swr_print_info("SWR_MSAA_FORCE_ENABLE: true\n");
}


struct pipe_screen *
swr_create_screen_internal(struct sw_winsys *winsys)
{
   struct swr_screen *screen = CALLOC_STRUCT(swr_screen);

   if (!screen)
      return NULL;

   if (!lp_build_init()) {
      FREE(screen);
      return NULL;
   }

   screen->winsys = winsys;
   screen->base.get_name = swr_get_name;
   screen->base.get_vendor = swr_get_vendor;
   screen->base.is_format_supported = swr_is_format_supported;
   screen->base.context_create = swr_create_context;
   screen->base.can_create_resource = swr_can_create_resource;

   screen->base.destroy = swr_destroy_screen;
   screen->base.get_param = swr_get_param;
   screen->base.get_shader_param = swr_get_shader_param;
   screen->base.get_paramf = swr_get_paramf;

   screen->base.resource_create = swr_resource_create;
   screen->base.resource_destroy = swr_resource_destroy;

   screen->base.flush_frontbuffer = swr_flush_frontbuffer;

   // Pass in "" for architecture for run-time determination
   screen->hJitMgr = JitCreateContext(KNOB_SIMD_WIDTH, "", "swr");

   swr_fence_init(&screen->base);

   swr_validate_env_options(screen);

   return &screen->base;
}
