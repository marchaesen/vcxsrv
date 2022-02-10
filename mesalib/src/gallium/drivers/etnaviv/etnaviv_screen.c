/*
 * Copyright (c) 2012-2015 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Wladimir J. van der Laan <laanwj@gmail.com>
 *    Christian Gmeiner <christian.gmeiner@gmail.com>
 */

#include "etnaviv_screen.h"

#include "hw/common.xml.h"

#include "etnaviv_compiler.h"
#include "etnaviv_context.h"
#include "etnaviv_debug.h"
#include "etnaviv_fence.h"
#include "etnaviv_format.h"
#include "etnaviv_query.h"
#include "etnaviv_resource.h"
#include "etnaviv_translate.h"

#include "util/hash_table.h"
#include "util/os_time.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/u_screen.h"
#include "util/u_string.h"

#include "frontend/drm_driver.h"

#include "drm-uapi/drm_fourcc.h"

#define ETNA_DRM_VERSION_FENCE_FD      ETNA_DRM_VERSION(1, 1)
#define ETNA_DRM_VERSION_PERFMON       ETNA_DRM_VERSION(1, 2)

static const struct debug_named_value etna_debug_options[] = {
   {"dbg_msgs",       ETNA_DBG_MSGS, "Print debug messages"},
   {"drm_msgs",       ETNA_DRM_MSGS, "Print drm messages"},
   {"frame_msgs",     ETNA_DBG_FRAME_MSGS, "Print frame messages"},
   {"resource_msgs",  ETNA_DBG_RESOURCE_MSGS, "Print resource messages"},
   {"compiler_msgs",  ETNA_DBG_COMPILER_MSGS, "Print compiler messages"},
   {"linker_msgs",    ETNA_DBG_LINKER_MSGS, "Print linker messages"},
   {"dump_shaders",   ETNA_DBG_DUMP_SHADERS, "Dump shaders"},
   {"no_ts",          ETNA_DBG_NO_TS, "Disable TS"},
   {"no_autodisable", ETNA_DBG_NO_AUTODISABLE, "Disable autodisable"},
   {"no_supertile",   ETNA_DBG_NO_SUPERTILE, "Disable supertiles"},
   {"no_early_z",     ETNA_DBG_NO_EARLY_Z, "Disable early z"},
   {"cflush_all",     ETNA_DBG_CFLUSH_ALL, "Flush every cache before state update"},
   {"msaa2x",         ETNA_DBG_MSAA_2X, "Force 2x msaa"},
   {"msaa4x",         ETNA_DBG_MSAA_4X, "Force 4x msaa"},
   {"flush_all",      ETNA_DBG_FLUSH_ALL, "Flush after every rendered primitive"},
   {"zero",           ETNA_DBG_ZERO, "Zero all resources after allocation"},
   {"draw_stall",     ETNA_DBG_DRAW_STALL, "Stall FE/PE after each rendered primitive"},
   {"shaderdb",       ETNA_DBG_SHADERDB, "Enable shaderdb output"},
   {"no_singlebuffer",ETNA_DBG_NO_SINGLEBUF, "Disable single buffer feature"},
   {"tgsi",           ETNA_DBG_TGSI, "use old TGSI compiler"},
   {"deqp",           ETNA_DBG_DEQP, "Hacks to run dEQP GLES3 tests"}, /* needs MESA_GLES_VERSION_OVERRIDE=3.0 */
   {"nocache",        ETNA_DBG_NOCACHE,    "Disable shader cache"},
   DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(etna_mesa_debug, "ETNA_MESA_DEBUG", etna_debug_options, 0)
int etna_mesa_debug = 0;

static void
etna_screen_destroy(struct pipe_screen *pscreen)
{
   struct etna_screen *screen = etna_screen(pscreen);

   if (screen->perfmon)
      etna_perfmon_del(screen->perfmon);

   if (screen->compiler)
      etna_compiler_destroy(screen->compiler);

   if (screen->pipe)
      etna_pipe_del(screen->pipe);

   if (screen->gpu)
      etna_gpu_del(screen->gpu);

   if (screen->ro)
      screen->ro->destroy(screen->ro);

   if (screen->dev)
      etna_device_del(screen->dev);

   FREE(screen);
}

static const char *
etna_screen_get_name(struct pipe_screen *pscreen)
{
   struct etna_screen *priv = etna_screen(pscreen);
   static char buffer[128];

   snprintf(buffer, sizeof(buffer), "Vivante GC%x rev %04x", priv->model,
            priv->revision);

   return buffer;
}

static const char *
etna_screen_get_vendor(struct pipe_screen *pscreen)
{
   return "etnaviv";
}

static const char *
etna_screen_get_device_vendor(struct pipe_screen *pscreen)
{
   return "Vivante";
}

static int
etna_screen_get_param(struct pipe_screen *pscreen, enum pipe_cap param)
{
   struct etna_screen *screen = etna_screen(pscreen);

   switch (param) {
   /* Supported features (boolean caps). */
   case PIPE_CAP_POINT_SPRITE:
   case PIPE_CAP_BLEND_EQUATION_SEPARATE:
   case PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT:
   case PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER:
   case PIPE_CAP_FRAGMENT_SHADER_TEXTURE_LOD:
   case PIPE_CAP_FRAGMENT_SHADER_DERIVATIVES:
   case PIPE_CAP_VERTEX_SHADER_SATURATE:
   case PIPE_CAP_TEXTURE_BARRIER:
   case PIPE_CAP_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION:
   case PIPE_CAP_VERTEX_BUFFER_OFFSET_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_BUFFER_STRIDE_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_VERTEX_ELEMENT_SRC_OFFSET_4BYTE_ALIGNED_ONLY:
   case PIPE_CAP_TGSI_TEXCOORD:
   case PIPE_CAP_VERTEX_COLOR_UNCLAMPED:
   case PIPE_CAP_MIXED_COLOR_DEPTH_BITS:
   case PIPE_CAP_MIXED_FRAMEBUFFER_SIZES:
   case PIPE_CAP_STRING_MARKER:
   case PIPE_CAP_FRONTEND_NOOP:
      return 1;
   case PIPE_CAP_NATIVE_FENCE_FD:
      return screen->drm_version >= ETNA_DRM_VERSION_FENCE_FD;
   case PIPE_CAP_TGSI_FS_POSITION_IS_SYSVAL:
   case PIPE_CAP_TGSI_FS_FACE_IS_INTEGER_SYSVAL: /* note: not integer */
      return !DBG_ENABLED(ETNA_DBG_TGSI);
   case PIPE_CAP_TGSI_FS_POINT_IS_SYSVAL:
      return 0;

   /* Memory */
   case PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT:
      return 256;
   case PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT:
      return 4; /* XXX could easily be supported */

   case PIPE_CAP_NPOT_TEXTURES:
      return true; /* VIV_FEATURE(priv->dev, chipMinorFeatures1,
                      NON_POWER_OF_TWO); */

   case PIPE_CAP_ANISOTROPIC_FILTER:
   case PIPE_CAP_TEXTURE_SWIZZLE:
   case PIPE_CAP_PRIMITIVE_RESTART:
   case PIPE_CAP_PRIMITIVE_RESTART_FIXED_INDEX:
      return VIV_FEATURE(screen, chipMinorFeatures1, HALTI0);

   case PIPE_CAP_ALPHA_TEST:
      if (!DBG_ENABLED(ETNA_DBG_TGSI))
         return !VIV_FEATURE(screen, chipMinorFeatures7, PE_NO_ALPHA_TEST);
      else
         return 1;

   /* Unsupported features. */
   case PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT:
   case PIPE_CAP_GLSL_OPTIMIZE_CONSERVATIVELY:
   case PIPE_CAP_ALLOW_MAPPED_BUFFERS_DURING_EXECUTION:
   case PIPE_CAP_TEXRECT:
      return 0;

   /* Stream output. */
   case PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS:
      return DBG_ENABLED(ETNA_DBG_DEQP) ? 4 : 0;
   case PIPE_CAP_MAX_STREAM_OUTPUT_SEPARATE_COMPONENTS:
   case PIPE_CAP_MAX_STREAM_OUTPUT_INTERLEAVED_COMPONENTS:
      return 0;

   case PIPE_CAP_MAX_VERTEX_ATTRIB_STRIDE:
      return 128;
   case PIPE_CAP_MAX_VERTEX_ELEMENT_SRC_OFFSET:
      return 255;
   case PIPE_CAP_MAX_VERTEX_BUFFERS:
      return screen->specs.stream_count;
   case PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR:
      return VIV_FEATURE(screen, chipMinorFeatures4, HALTI2);


   /* Texturing. */
   case PIPE_CAP_TEXTURE_SHADOW_MAP:
      return !DBG_ENABLED(ETNA_DBG_TGSI);
   case PIPE_CAP_MAX_TEXTURE_2D_SIZE:
   case PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS: /* TODO: verify */
      return screen->specs.max_texture_size;
   case PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS:
   case PIPE_CAP_MAX_TEXTURE_3D_LEVELS:
   {
      int log2_max_tex_size = util_last_bit(screen->specs.max_texture_size);
      assert(log2_max_tex_size > 0);
      return log2_max_tex_size;
   }

   case PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET:
   case PIPE_CAP_MIN_TEXEL_OFFSET:
      return -8;
   case PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET:
   case PIPE_CAP_MAX_TEXEL_OFFSET:
      return 7;
   case PIPE_CAP_SEAMLESS_CUBE_MAP_PER_TEXTURE:
      return screen->specs.seamless_cube_map;

   /* Queries. */
   case PIPE_CAP_OCCLUSION_QUERY:
      return VIV_FEATURE(screen, chipMinorFeatures1, HALTI0);

   /* Preferences */
   case PIPE_CAP_TEXTURE_TRANSFER_MODES:
      return 0;
   case PIPE_CAP_MAX_TEXTURE_UPLOAD_MEMORY_BUDGET: {
      /* etnaviv is being run on systems as small as 256MB total RAM so
       * we need to provide a sane value for such a device. Limit the
       * memory budget to min(~3% of pyhiscal memory, 64MB).
       *
       * a simple divison by 32 provides the numbers we want.
       *    256MB / 32 =  8MB
       *   2048MB / 32 = 64MB
       */
      uint64_t system_memory;

      if (!os_get_total_physical_memory(&system_memory))
         system_memory = (uint64_t)4096 << 20;

      return MIN2(system_memory / 32, 64 * 1024 * 1024);
   }

   case PIPE_CAP_MAX_VARYINGS:
      return screen->specs.max_varyings;

   case PIPE_CAP_SUPPORTED_PRIM_MODES:
   case PIPE_CAP_SUPPORTED_PRIM_MODES_WITH_RESTART: {
      /* Generate the bitmask of supported draw primitives. */
      uint32_t modes = 1 << PIPE_PRIM_POINTS |
                       1 << PIPE_PRIM_LINES |
                       1 << PIPE_PRIM_LINE_STRIP |
                       1 << PIPE_PRIM_TRIANGLES |
                       1 << PIPE_PRIM_TRIANGLE_FAN;

      /* TODO: The bug relates only to indexed draws, but here we signal
       * that there is no support for triangle strips at all. This should
       * be refined.
       */
      if (VIV_FEATURE(screen, chipMinorFeatures2, BUG_FIXES8))
         modes |= 1 << PIPE_PRIM_TRIANGLE_STRIP;

      if (VIV_FEATURE(screen, chipMinorFeatures2, LINE_LOOP))
         modes |= 1 << PIPE_PRIM_LINE_LOOP;

      return modes;
   }

   case PIPE_CAP_PCI_GROUP:
   case PIPE_CAP_PCI_BUS:
   case PIPE_CAP_PCI_DEVICE:
   case PIPE_CAP_PCI_FUNCTION:
      return 0;
   case PIPE_CAP_ACCELERATED:
      return 1;
   case PIPE_CAP_VIDEO_MEMORY:
      return 0;
   case PIPE_CAP_UMA:
      return 1;
   default:
      return u_pipe_screen_get_param_defaults(pscreen, param);
   }
}

static float
etna_screen_get_paramf(struct pipe_screen *pscreen, enum pipe_capf param)
{
   struct etna_screen *screen = etna_screen(pscreen);

   switch (param) {
   case PIPE_CAPF_MIN_LINE_WIDTH:
   case PIPE_CAPF_MIN_LINE_WIDTH_AA:
   case PIPE_CAPF_MIN_POINT_SIZE:
   case PIPE_CAPF_MIN_POINT_SIZE_AA:
      return 1;
   case PIPE_CAPF_POINT_SIZE_GRANULARITY:
   case PIPE_CAPF_LINE_WIDTH_GRANULARITY:
      return 0.1;
   case PIPE_CAPF_MAX_LINE_WIDTH:
   case PIPE_CAPF_MAX_LINE_WIDTH_AA:
   case PIPE_CAPF_MAX_POINT_SIZE:
   case PIPE_CAPF_MAX_POINT_SIZE_AA:
      return 8192.0f;
   case PIPE_CAPF_MAX_TEXTURE_ANISOTROPY:
      return 16.0f;
   case PIPE_CAPF_MAX_TEXTURE_LOD_BIAS:
      return util_last_bit(screen->specs.max_texture_size);
   case PIPE_CAPF_MIN_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_MAX_CONSERVATIVE_RASTER_DILATE:
   case PIPE_CAPF_CONSERVATIVE_RASTER_DILATE_GRANULARITY:
      return 0.0f;
   }

   debug_printf("unknown paramf %d", param);
   return 0;
}

static int
etna_screen_get_shader_param(struct pipe_screen *pscreen,
                             enum pipe_shader_type shader,
                             enum pipe_shader_cap param)
{
   struct etna_screen *screen = etna_screen(pscreen);
   bool ubo_enable = screen->specs.halti >= 2 && !DBG_ENABLED(ETNA_DBG_TGSI);

   if (DBG_ENABLED(ETNA_DBG_DEQP))
      ubo_enable = true;

   switch (shader) {
   case PIPE_SHADER_FRAGMENT:
   case PIPE_SHADER_VERTEX:
      break;
   case PIPE_SHADER_COMPUTE:
   case PIPE_SHADER_GEOMETRY:
   case PIPE_SHADER_TESS_CTRL:
   case PIPE_SHADER_TESS_EVAL:
      return 0;
   default:
      DBG("unknown shader type %d", shader);
      return 0;
   }

   switch (param) {
   case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
      return ETNA_MAX_TOKENS;
   case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
      return ETNA_MAX_DEPTH; /* XXX */
   case PIPE_SHADER_CAP_MAX_INPUTS:
      /* Maximum number of inputs for the vertex shader is the number
       * of vertex elements - each element defines one vertex shader
       * input register.  For the fragment shader, this is the number
       * of varyings. */
      return shader == PIPE_SHADER_FRAGMENT ? screen->specs.max_varyings
                                            : screen->specs.vertex_max_elements;
   case PIPE_SHADER_CAP_MAX_OUTPUTS:
      return 16; /* see VIVS_VS_OUTPUT */
   case PIPE_SHADER_CAP_MAX_TEMPS:
      return 64; /* Max native temporaries. */
   case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
      return ubo_enable ? ETNA_MAX_CONST_BUF : 1;
   case PIPE_SHADER_CAP_TGSI_CONT_SUPPORTED:
      return 1;
   case PIPE_SHADER_CAP_INDIRECT_INPUT_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_OUTPUT_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
   case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
      return 1;
   case PIPE_SHADER_CAP_SUBROUTINES:
      return 0;
   case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
      return VIV_FEATURE(screen, chipMinorFeatures0, HAS_SQRT_TRIG);
   case PIPE_SHADER_CAP_INT64_ATOMICS:
   case PIPE_SHADER_CAP_FP16:
   case PIPE_SHADER_CAP_FP16_DERIVATIVES:
   case PIPE_SHADER_CAP_FP16_CONST_BUFFERS:
   case PIPE_SHADER_CAP_INT16:
   case PIPE_SHADER_CAP_GLSL_16BIT_CONSTS:
      return 0;
   case PIPE_SHADER_CAP_INTEGERS:
      return !DBG_ENABLED(ETNA_DBG_TGSI) && screen->specs.halti >= 2;
   case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
   case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
      return shader == PIPE_SHADER_FRAGMENT
                ? screen->specs.fragment_sampler_count
                : screen->specs.vertex_sampler_count;
   case PIPE_SHADER_CAP_PREFERRED_IR:
      return !DBG_ENABLED(ETNA_DBG_TGSI) ? PIPE_SHADER_IR_NIR : PIPE_SHADER_IR_TGSI;
   case PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE:
      if (ubo_enable)
         return 16384; /* 16384 so state tracker enables UBOs */
      return shader == PIPE_SHADER_FRAGMENT
                ? screen->specs.max_ps_uniforms * sizeof(float[4])
                : screen->specs.max_vs_uniforms * sizeof(float[4]);
   case PIPE_SHADER_CAP_TGSI_DROUND_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_DFRACEXP_DLDEXP_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_LDEXP_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_FMA_SUPPORTED:
   case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
      return false;
   case PIPE_SHADER_CAP_SUPPORTED_IRS:
      return (1 << PIPE_SHADER_IR_TGSI) |
             (!DBG_ENABLED(ETNA_DBG_TGSI) ? 1 << PIPE_SHADER_IR_NIR : 0);
   case PIPE_SHADER_CAP_MAX_UNROLL_ITERATIONS_HINT:
      return 32;
   case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
   case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
   case PIPE_SHADER_CAP_LOWER_IF_THRESHOLD:
   case PIPE_SHADER_CAP_TGSI_SKIP_MERGE_REGISTERS:
   case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTERS:
   case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTER_BUFFERS:
      return 0;
   }

   debug_printf("unknown shader param %d", param);
   return 0;
}

static uint64_t
etna_screen_get_timestamp(struct pipe_screen *pscreen)
{
   return os_time_get_nano();
}

static bool
gpu_supports_texture_target(struct etna_screen *screen,
                            enum pipe_texture_target target)
{
   if (target == PIPE_TEXTURE_CUBE_ARRAY)
      return false;

   /* pre-halti has no array/3D */
   if (screen->specs.halti < 0 &&
       (target == PIPE_TEXTURE_1D_ARRAY ||
        target == PIPE_TEXTURE_2D_ARRAY ||
        target == PIPE_TEXTURE_3D))
      return false;

   return true;
}

static bool
gpu_supports_texture_format(struct etna_screen *screen, uint32_t fmt,
                            enum pipe_format format)
{
   bool supported = true;

   if (fmt == TEXTURE_FORMAT_ETC1)
      supported = VIV_FEATURE(screen, chipFeatures, ETC1_TEXTURE_COMPRESSION);

   if (fmt >= TEXTURE_FORMAT_DXT1 && fmt <= TEXTURE_FORMAT_DXT4_DXT5)
      supported = VIV_FEATURE(screen, chipFeatures, DXT_TEXTURE_COMPRESSION);

   if (util_format_is_srgb(format))
      supported = VIV_FEATURE(screen, chipMinorFeatures1, HALTI0);

   if (fmt & EXT_FORMAT)
      supported = VIV_FEATURE(screen, chipMinorFeatures1, HALTI0);

   if (fmt & ASTC_FORMAT) {
      supported = screen->specs.tex_astc;
   }

   if (util_format_is_snorm(format))
      supported = VIV_FEATURE(screen, chipMinorFeatures2, HALTI1);

   if (format != PIPE_FORMAT_S8_UINT_Z24_UNORM &&
       (util_format_is_pure_integer(format) || util_format_is_float(format)))
      supported = VIV_FEATURE(screen, chipMinorFeatures4, HALTI2);


   if (!supported)
      return false;

   if (texture_format_needs_swiz(format))
      return VIV_FEATURE(screen, chipMinorFeatures1, HALTI0);

   return true;
}

static bool
gpu_supports_render_format(struct etna_screen *screen, enum pipe_format format,
                           unsigned sample_count)
{
   const uint32_t fmt = translate_pe_format(format);

   if (fmt == ETNA_NO_MATCH)
      return false;

   /* MSAA is broken */
   if (sample_count > 1)
         return false;

   if (format == PIPE_FORMAT_R8_UNORM)
      return VIV_FEATURE(screen, chipMinorFeatures5, HALTI5);

   /* figure out 8bpp RS clear to enable these formats */
   if (format == PIPE_FORMAT_R8_SINT || format == PIPE_FORMAT_R8_UINT)
      return VIV_FEATURE(screen, chipMinorFeatures5, HALTI5);

   if (util_format_is_srgb(format))
      return VIV_FEATURE(screen, chipMinorFeatures5, HALTI3);

   if (util_format_is_pure_integer(format) || util_format_is_float(format))
      return VIV_FEATURE(screen, chipMinorFeatures4, HALTI2);

   if (format == PIPE_FORMAT_R8G8_UNORM)
      return VIV_FEATURE(screen, chipMinorFeatures4, HALTI2);

   /* any other extended format is HALTI0 (only R10G10B10A2?) */
   if (fmt >= PE_FORMAT_R16F)
      return VIV_FEATURE(screen, chipMinorFeatures1, HALTI0);

   return true;
}

static bool
gpu_supports_vertex_format(struct etna_screen *screen, enum pipe_format format)
{
   if (translate_vertex_format_type(format) == ETNA_NO_MATCH)
      return false;

   if (util_format_is_pure_integer(format))
      return VIV_FEATURE(screen, chipMinorFeatures4, HALTI2);

   return true;
}

static bool
etna_screen_is_format_supported(struct pipe_screen *pscreen,
                                enum pipe_format format,
                                enum pipe_texture_target target,
                                unsigned sample_count,
                                unsigned storage_sample_count,
                                unsigned usage)
{
   struct etna_screen *screen = etna_screen(pscreen);
   unsigned allowed = 0;

   if (!gpu_supports_texture_target(screen, target))
      return false;

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   if (usage & PIPE_BIND_RENDER_TARGET) {
      if (gpu_supports_render_format(screen, format, sample_count))
         allowed |= PIPE_BIND_RENDER_TARGET;
   }

   if (usage & PIPE_BIND_DEPTH_STENCIL) {
      if (translate_depth_format(format) != ETNA_NO_MATCH)
         allowed |= PIPE_BIND_DEPTH_STENCIL;
   }

   if (usage & PIPE_BIND_SAMPLER_VIEW) {
      uint32_t fmt = translate_texture_format(format);

      if (!gpu_supports_texture_format(screen, fmt, format))
         fmt = ETNA_NO_MATCH;

      if (sample_count < 2 && fmt != ETNA_NO_MATCH)
         allowed |= PIPE_BIND_SAMPLER_VIEW;
   }

   if (usage & PIPE_BIND_VERTEX_BUFFER) {
      if (gpu_supports_vertex_format(screen, format))
         allowed |= PIPE_BIND_VERTEX_BUFFER;
   }

   if (usage & PIPE_BIND_INDEX_BUFFER) {
      /* must be supported index format */
      if (format == PIPE_FORMAT_R8_UINT || format == PIPE_FORMAT_R16_UINT ||
          (format == PIPE_FORMAT_R32_UINT &&
           VIV_FEATURE(screen, chipFeatures, 32_BIT_INDICES))) {
         allowed |= PIPE_BIND_INDEX_BUFFER;
      }
   }

   /* Always allowed */
   allowed |=
      usage & (PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT | PIPE_BIND_SHARED);

   if (usage != allowed) {
      DBG("not supported: format=%s, target=%d, sample_count=%d, "
          "usage=%x, allowed=%x",
          util_format_name(format), target, sample_count, usage, allowed);
   }

   return usage == allowed;
}

const uint64_t supported_modifiers[] = {
   DRM_FORMAT_MOD_LINEAR,
   DRM_FORMAT_MOD_VIVANTE_TILED,
   DRM_FORMAT_MOD_VIVANTE_SUPER_TILED,
   DRM_FORMAT_MOD_VIVANTE_SPLIT_TILED,
   DRM_FORMAT_MOD_VIVANTE_SPLIT_SUPER_TILED,
};

static bool modifier_num_supported(struct pipe_screen *pscreen, int num)
{
   struct etna_screen *screen = etna_screen(pscreen);

   /* don't advertise split tiled formats on single pipe/buffer GPUs */
   if ((screen->specs.pixel_pipes == 1 || screen->specs.single_buffer) &&
       num >= 3)
      return false;

   return true;
}

static void
etna_screen_query_dmabuf_modifiers(struct pipe_screen *pscreen,
                                   enum pipe_format format, int max,
                                   uint64_t *modifiers,
                                   unsigned int *external_only, int *count)
{
   int i, num_modifiers = 0;

   if (max > ARRAY_SIZE(supported_modifiers))
      max = ARRAY_SIZE(supported_modifiers);

   if (!max) {
      modifiers = NULL;
      max = ARRAY_SIZE(supported_modifiers);
   }

   for (i = 0; num_modifiers < max; i++) {
      if (!modifier_num_supported(pscreen, i))
         break;

      if (modifiers)
         modifiers[num_modifiers] = supported_modifiers[i];
      if (external_only)
         external_only[num_modifiers] = util_format_is_yuv(format) ? 1 : 0;
      num_modifiers++;
   }

   *count = num_modifiers;
}

static bool
etna_screen_is_dmabuf_modifier_supported(struct pipe_screen *pscreen,
                                         uint64_t modifier,
                                         enum pipe_format format,
                                         bool *external_only)
{
   int i;

   for (i = 0; i < ARRAY_SIZE(supported_modifiers); i++) {
      if (!modifier_num_supported(pscreen, i))
         break;

      if (modifier == supported_modifiers[i]) {
         if (external_only)
            *external_only = util_format_is_yuv(format) ? 1 : 0;

         return true;
      }
   }

   return false;
}

static void
etna_determine_uniform_limits(struct etna_screen *screen)
{
   /* values for the non unified case are taken from
    * gcmCONFIGUREUNIFORMS in the Vivante kernel driver file
    * drivers/mxc/gpu-viv/hal/kernel/inc/gc_hal_base.h.
    */
   if (screen->model == chipModel_GC2000 &&
       (screen->revision == 0x5118 || screen->revision == 0x5140)) {
      screen->specs.max_vs_uniforms = 256;
      screen->specs.max_ps_uniforms = 64;
   } else if (screen->specs.num_constants == 320) {
      screen->specs.max_vs_uniforms = 256;
      screen->specs.max_ps_uniforms = 64;
   } else if (screen->specs.num_constants > 256 &&
              screen->model == chipModel_GC1000) {
      /* All GC1000 series chips can only support 64 uniforms for ps on non-unified const mode. */
      screen->specs.max_vs_uniforms = 256;
      screen->specs.max_ps_uniforms = 64;
   } else if (screen->specs.num_constants > 256) {
      screen->specs.max_vs_uniforms = 256;
      screen->specs.max_ps_uniforms = 256;
   } else if (screen->specs.num_constants == 256) {
      screen->specs.max_vs_uniforms = 256;
      screen->specs.max_ps_uniforms = 256;
   } else {
      screen->specs.max_vs_uniforms = 168;
      screen->specs.max_ps_uniforms = 64;
   }
}

static void
etna_determine_sampler_limits(struct etna_screen *screen)
{
   /* vertex and fragment samplers live in one address space */
   if (screen->specs.halti >= 1) {
      screen->specs.vertex_sampler_offset = 16;
      screen->specs.fragment_sampler_count = 16;
      screen->specs.vertex_sampler_count = 16;
   } else {
      screen->specs.vertex_sampler_offset = 8;
      screen->specs.fragment_sampler_count = 8;
      screen->specs.vertex_sampler_count = 4;
   }

   if (screen->model == 0x400)
      screen->specs.vertex_sampler_count = 0;
}

static bool
etna_get_specs(struct etna_screen *screen)
{
   uint64_t val;
   uint32_t instruction_count;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_INSTRUCTION_COUNT, &val)) {
      DBG("could not get ETNA_GPU_INSTRUCTION_COUNT");
      goto fail;
   }
   instruction_count = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_VERTEX_OUTPUT_BUFFER_SIZE,
                          &val)) {
      DBG("could not get ETNA_GPU_VERTEX_OUTPUT_BUFFER_SIZE");
      goto fail;
   }
   screen->specs.vertex_output_buffer_size = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_VERTEX_CACHE_SIZE, &val)) {
      DBG("could not get ETNA_GPU_VERTEX_CACHE_SIZE");
      goto fail;
   }
   screen->specs.vertex_cache_size = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_SHADER_CORE_COUNT, &val)) {
      DBG("could not get ETNA_GPU_SHADER_CORE_COUNT");
      goto fail;
   }
   screen->specs.shader_core_count = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_STREAM_COUNT, &val)) {
      DBG("could not get ETNA_GPU_STREAM_COUNT");
      goto fail;
   }
   screen->specs.stream_count = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_REGISTER_MAX, &val)) {
      DBG("could not get ETNA_GPU_REGISTER_MAX");
      goto fail;
   }
   screen->specs.max_registers = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_PIXEL_PIPES, &val)) {
      DBG("could not get ETNA_GPU_PIXEL_PIPES");
      goto fail;
   }
   screen->specs.pixel_pipes = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_NUM_CONSTANTS, &val)) {
      DBG("could not get %s", "ETNA_GPU_NUM_CONSTANTS");
      goto fail;
   }
   if (val == 0) {
      fprintf(stderr, "Warning: zero num constants (update kernel?)\n");
      val = 168;
   }
   screen->specs.num_constants = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_NUM_VARYINGS, &val)) {
      DBG("could not get ETNA_GPU_NUM_VARYINGS");
      goto fail;
   }
   screen->specs.max_varyings = MAX2(val, ETNA_NUM_VARYINGS);

   /* Figure out gross GPU architecture. See rnndb/common.xml for a specific
    * description of the differences. */
   if (VIV_FEATURE(screen, chipMinorFeatures5, HALTI5))
      screen->specs.halti = 5; /* New GC7000/GC8x00  */
   else if (VIV_FEATURE(screen, chipMinorFeatures5, HALTI4))
      screen->specs.halti = 4; /* Old GC7000/GC7400 */
   else if (VIV_FEATURE(screen, chipMinorFeatures5, HALTI3))
      screen->specs.halti = 3; /* None? */
   else if (VIV_FEATURE(screen, chipMinorFeatures4, HALTI2))
      screen->specs.halti = 2; /* GC2500/GC3000/GC5000/GC6400 */
   else if (VIV_FEATURE(screen, chipMinorFeatures2, HALTI1))
      screen->specs.halti = 1; /* GC900/GC4000/GC7000UL */
   else if (VIV_FEATURE(screen, chipMinorFeatures1, HALTI0))
      screen->specs.halti = 0; /* GC880/GC2000/GC7000TM */
   else
      screen->specs.halti = -1; /* GC7000nanolite / pre-GC2000 except GC880 */
   if (screen->specs.halti >= 0)
      DBG("etnaviv: GPU arch: HALTI%d", screen->specs.halti);
   else
      DBG("etnaviv: GPU arch: pre-HALTI");

   screen->specs.can_supertile =
      VIV_FEATURE(screen, chipMinorFeatures0, SUPER_TILED);
   screen->specs.bits_per_tile =
      VIV_FEATURE(screen, chipMinorFeatures0, 2BITPERTILE) ? 2 : 4;
   screen->specs.ts_clear_value =
      VIV_FEATURE(screen, chipMinorFeatures5, BLT_ENGINE)  ? 0xffffffff :
      VIV_FEATURE(screen, chipMinorFeatures0, 2BITPERTILE) ? 0x55555555 :
                                                             0x11111111;

   screen->specs.vs_need_z_div =
      screen->model < 0x1000 && screen->model != 0x880;
   screen->specs.has_sin_cos_sqrt =
      VIV_FEATURE(screen, chipMinorFeatures0, HAS_SQRT_TRIG);
   screen->specs.has_sign_floor_ceil =
      VIV_FEATURE(screen, chipMinorFeatures0, HAS_SIGN_FLOOR_CEIL);
   screen->specs.has_shader_range_registers =
      screen->model >= 0x1000 || screen->model == 0x880;
   screen->specs.npot_tex_any_wrap =
      VIV_FEATURE(screen, chipMinorFeatures1, NON_POWER_OF_TWO);
   screen->specs.has_new_transcendentals =
      VIV_FEATURE(screen, chipMinorFeatures3, HAS_FAST_TRANSCENDENTALS);
   screen->specs.has_halti2_instructions =
      VIV_FEATURE(screen, chipMinorFeatures4, HALTI2);
   screen->specs.v4_compression =
      VIV_FEATURE(screen, chipMinorFeatures6, V4_COMPRESSION);
   screen->specs.seamless_cube_map =
      (screen->model != 0x880) && /* Seamless cubemap is broken on GC880? */
      VIV_FEATURE(screen, chipMinorFeatures2, SEAMLESS_CUBE_MAP);

   if (screen->specs.halti >= 5) {
      /* GC7000 - this core must load shaders from memory. */
      screen->specs.vs_offset = 0;
      screen->specs.ps_offset = 0;
      screen->specs.max_instructions = 0; /* Do not program shaders manually */
      screen->specs.has_icache = true;
   } else if (VIV_FEATURE(screen, chipMinorFeatures3, INSTRUCTION_CACHE)) {
      /* GC3000 - this core is capable of loading shaders from
       * memory. It can also run shaders from registers, as a fallback, but
       * "max_instructions" does not have the correct value. It has place for
       * 2*256 instructions just like GC2000, but the offsets are slightly
       * different.
       */
      screen->specs.vs_offset = 0xC000;
      /* State 08000-0C000 mirrors 0C000-0E000, and the Vivante driver uses
       * this mirror for writing PS instructions, probably safest to do the
       * same.
       */
      screen->specs.ps_offset = 0x8000 + 0x1000;
      screen->specs.max_instructions = 256; /* maximum number instructions for non-icache use */
      screen->specs.has_icache = true;
   } else {
      if (instruction_count > 256) { /* unified instruction memory? */
         screen->specs.vs_offset = 0xC000;
         screen->specs.ps_offset = 0xD000; /* like vivante driver */
         screen->specs.max_instructions = 256;
      } else {
         screen->specs.vs_offset = 0x4000;
         screen->specs.ps_offset = 0x6000;
         screen->specs.max_instructions = instruction_count / 2;
      }
      screen->specs.has_icache = false;
   }

   if (VIV_FEATURE(screen, chipMinorFeatures1, HALTI0)) {
      screen->specs.vertex_max_elements = 16;
   } else {
      /* Etna_viv documentation seems confused over the correct value
       * here so choose the lower to be safe: HALTI0 says 16 i.s.o.
       * 10, but VERTEX_ELEMENT_CONFIG register says 16 i.s.o. 12. */
      screen->specs.vertex_max_elements = 10;
   }

   etna_determine_uniform_limits(screen);
   etna_determine_sampler_limits(screen);

   if (screen->specs.halti >= 5) {
      screen->specs.has_unified_uniforms = true;
      screen->specs.vs_uniforms_offset = VIVS_SH_HALTI5_UNIFORMS_MIRROR(0);
      screen->specs.ps_uniforms_offset = VIVS_SH_HALTI5_UNIFORMS(screen->specs.max_vs_uniforms*4);
   } else if (screen->specs.halti >= 1) {
      /* unified uniform memory on GC3000 - HALTI1 feature bit is just a guess
      */
      screen->specs.has_unified_uniforms = true;
      screen->specs.vs_uniforms_offset = VIVS_SH_UNIFORMS(0);
      /* hardcode PS uniforms to start after end of VS uniforms -
       * for more flexibility this offset could be variable based on the
       * shader.
       */
      screen->specs.ps_uniforms_offset = VIVS_SH_UNIFORMS(screen->specs.max_vs_uniforms*4);
   } else {
      screen->specs.has_unified_uniforms = false;
      screen->specs.vs_uniforms_offset = VIVS_VS_UNIFORMS(0);
      screen->specs.ps_uniforms_offset = VIVS_PS_UNIFORMS(0);
   }

   screen->specs.max_texture_size =
      VIV_FEATURE(screen, chipMinorFeatures0, TEXTURE_8K) ? 8192 : 2048;
   screen->specs.max_rendertarget_size =
      VIV_FEATURE(screen, chipMinorFeatures0, RENDERTARGET_8K) ? 8192 : 2048;

   screen->specs.single_buffer = VIV_FEATURE(screen, chipMinorFeatures4, SINGLE_BUFFER);
   if (screen->specs.single_buffer)
      DBG("etnaviv: Single buffer mode enabled with %d pixel pipes", screen->specs.pixel_pipes);

   screen->specs.tex_astc = VIV_FEATURE(screen, chipMinorFeatures4, TEXTURE_ASTC) &&
                            !VIV_FEATURE(screen, chipMinorFeatures6, NO_ASTC);

   screen->specs.use_blt = VIV_FEATURE(screen, chipMinorFeatures5, BLT_ENGINE);

   return true;

fail:
   return false;
}

struct etna_bo *
etna_screen_bo_from_handle(struct pipe_screen *pscreen,
                           struct winsys_handle *whandle)
{
   struct etna_screen *screen = etna_screen(pscreen);
   struct etna_bo *bo;

   if (whandle->type == WINSYS_HANDLE_TYPE_SHARED) {
      bo = etna_bo_from_name(screen->dev, whandle->handle);
   } else if (whandle->type == WINSYS_HANDLE_TYPE_FD) {
      bo = etna_bo_from_dmabuf(screen->dev, whandle->handle);
   } else {
      DBG("Attempt to import unsupported handle type %d", whandle->type);
      return NULL;
   }

   if (!bo) {
      DBG("ref name 0x%08x failed", whandle->handle);
      return NULL;
   }

   return bo;
}

static const void *
etna_get_compiler_options(struct pipe_screen *pscreen,
                          enum pipe_shader_ir ir, unsigned shader)
{
   return &etna_screen(pscreen)->options;
}

static struct disk_cache *
etna_get_disk_shader_cache(struct pipe_screen *pscreen)
{
   struct etna_screen *screen = etna_screen(pscreen);
   struct etna_compiler *compiler = screen->compiler;

   return compiler->disk_cache;
}

struct pipe_screen *
etna_screen_create(struct etna_device *dev, struct etna_gpu *gpu,
                   struct renderonly *ro)
{
   struct etna_screen *screen = CALLOC_STRUCT(etna_screen);
   struct pipe_screen *pscreen;
   uint64_t val;

   if (!screen)
      return NULL;

   pscreen = &screen->base;
   screen->dev = dev;
   screen->gpu = gpu;
   screen->ro = ro;
   screen->refcnt = 1;

   screen->drm_version = etnaviv_device_version(screen->dev);
   etna_mesa_debug = debug_get_option_etna_mesa_debug();

   /* Disable autodisable for correct rendering with TS */
   etna_mesa_debug |= ETNA_DBG_NO_AUTODISABLE;

   screen->pipe = etna_pipe_new(gpu, ETNA_PIPE_3D);
   if (!screen->pipe) {
      DBG("could not create 3d pipe");
      goto fail;
   }

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_MODEL, &val)) {
      DBG("could not get ETNA_GPU_MODEL");
      goto fail;
   }
   screen->model = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_REVISION, &val)) {
      DBG("could not get ETNA_GPU_REVISION");
      goto fail;
   }
   screen->revision = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_FEATURES_0, &val)) {
      DBG("could not get ETNA_GPU_FEATURES_0");
      goto fail;
   }
   screen->features[0] = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_FEATURES_1, &val)) {
      DBG("could not get ETNA_GPU_FEATURES_1");
      goto fail;
   }
   screen->features[1] = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_FEATURES_2, &val)) {
      DBG("could not get ETNA_GPU_FEATURES_2");
      goto fail;
   }
   screen->features[2] = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_FEATURES_3, &val)) {
      DBG("could not get ETNA_GPU_FEATURES_3");
      goto fail;
   }
   screen->features[3] = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_FEATURES_4, &val)) {
      DBG("could not get ETNA_GPU_FEATURES_4");
      goto fail;
   }
   screen->features[4] = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_FEATURES_5, &val)) {
      DBG("could not get ETNA_GPU_FEATURES_5");
      goto fail;
   }
   screen->features[5] = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_FEATURES_6, &val)) {
      DBG("could not get ETNA_GPU_FEATURES_6");
      goto fail;
   }
   screen->features[6] = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_FEATURES_7, &val)) {
      DBG("could not get ETNA_GPU_FEATURES_7");
      goto fail;
   }
   screen->features[7] = val;

   if (etna_gpu_get_param(screen->gpu, ETNA_GPU_FEATURES_8, &val)) {
      DBG("could not get ETNA_GPU_FEATURES_8");
      goto fail;
   }
   screen->features[8] = val;

   if (!etna_get_specs(screen))
      goto fail;

   if (screen->specs.halti >= 5 && !etnaviv_device_softpin_capable(dev)) {
      DBG("halti5 requires softpin");
      goto fail;
   }

   screen->options = (nir_shader_compiler_options) {
      .lower_fpow = true,
      .lower_ftrunc = true,
      .fuse_ffma16 = true,
      .fuse_ffma32 = true,
      .fuse_ffma64 = true,
      .lower_bitops = true,
      .lower_all_io_to_temps = true,
      .vertex_id_zero_based = true,
      .lower_flrp32 = true,
      .lower_fmod = true,
      .lower_vector_cmp = true,
      .lower_fdph = true,
      .lower_insert_byte = true,
      .lower_insert_word = true,
      .lower_fdiv = true, /* !screen->specs.has_new_transcendentals */
      .lower_fsign = !screen->specs.has_sign_floor_ceil,
      .lower_ffloor = !screen->specs.has_sign_floor_ceil,
      .lower_fceil = !screen->specs.has_sign_floor_ceil,
      .lower_fsqrt = !screen->specs.has_sin_cos_sqrt,
      .lower_sincos = !screen->specs.has_sin_cos_sqrt,
      .lower_uniforms_to_ubo = screen->specs.halti >= 2,
      .force_indirect_unrolling = nir_var_all,
   };

   /* apply debug options that disable individual features */
   if (DBG_ENABLED(ETNA_DBG_NO_EARLY_Z))
      screen->features[viv_chipFeatures] |= chipFeatures_NO_EARLY_Z;
   if (DBG_ENABLED(ETNA_DBG_NO_TS))
         screen->features[viv_chipFeatures] &= ~chipFeatures_FAST_CLEAR;
   if (DBG_ENABLED(ETNA_DBG_NO_AUTODISABLE))
      screen->features[viv_chipMinorFeatures1] &= ~chipMinorFeatures1_AUTO_DISABLE;
   if (DBG_ENABLED(ETNA_DBG_NO_SUPERTILE))
      screen->specs.can_supertile = 0;
   if (DBG_ENABLED(ETNA_DBG_NO_SINGLEBUF))
      screen->specs.single_buffer = 0;

   pscreen->destroy = etna_screen_destroy;
   pscreen->get_param = etna_screen_get_param;
   pscreen->get_paramf = etna_screen_get_paramf;
   pscreen->get_shader_param = etna_screen_get_shader_param;
   pscreen->get_compiler_options = etna_get_compiler_options;
   pscreen->get_disk_shader_cache = etna_get_disk_shader_cache;

   pscreen->get_name = etna_screen_get_name;
   pscreen->get_vendor = etna_screen_get_vendor;
   pscreen->get_device_vendor = etna_screen_get_device_vendor;

   pscreen->get_timestamp = etna_screen_get_timestamp;
   pscreen->context_create = etna_context_create;
   pscreen->is_format_supported = etna_screen_is_format_supported;
   pscreen->query_dmabuf_modifiers = etna_screen_query_dmabuf_modifiers;
   pscreen->is_dmabuf_modifier_supported = etna_screen_is_dmabuf_modifier_supported;

   screen->compiler = etna_compiler_create(etna_screen_get_name(pscreen));
   if (!screen->compiler)
      goto fail;

   etna_fence_screen_init(pscreen);
   etna_query_screen_init(pscreen);
   etna_resource_screen_init(pscreen);

   util_dynarray_init(&screen->supported_pm_queries, NULL);
   slab_create_parent(&screen->transfer_pool, sizeof(struct etna_transfer), 16);

   if (screen->drm_version >= ETNA_DRM_VERSION_PERFMON)
      etna_pm_query_setup(screen);

   return pscreen;

fail:
   etna_screen_destroy(pscreen);
   return NULL;
}
