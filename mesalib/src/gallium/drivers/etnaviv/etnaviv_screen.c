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
   {"ml_msgs",        ETNA_DBG_ML_MSGS, "Print ML messages"},
   {"dump_shaders",   ETNA_DBG_DUMP_SHADERS, "Dump shaders"},
   {"no_ts",          ETNA_DBG_NO_TS, "Disable TS"},
   {"no_autodisable", ETNA_DBG_NO_AUTODISABLE, "Disable autodisable"},
   {"no_supertile",   ETNA_DBG_NO_SUPERTILE, "Disable supertiles"},
   {"no_early_z",     ETNA_DBG_NO_EARLY_Z, "Disable early z"},
   {"cflush_all",     ETNA_DBG_CFLUSH_ALL, "Flush every cache before state update"},
   {"flush_all",      ETNA_DBG_FLUSH_ALL, "Flush after every rendered primitive"},
   {"zero",           ETNA_DBG_ZERO, "Zero all resources after allocation"},
   {"draw_stall",     ETNA_DBG_DRAW_STALL, "Stall FE/PE after each rendered primitive"},
   {"shaderdb",       ETNA_DBG_SHADERDB, "Enable shaderdb output"},
   {"no_singlebuffer",ETNA_DBG_NO_SINGLEBUF, "Disable single buffer feature"},
   {"deqp",           ETNA_DBG_DEQP, "Hacks to run dEQP GLES3 tests"}, /* needs MESA_GLES_VERSION_OVERRIDE=3.0 */
   {"nocache",        ETNA_DBG_NOCACHE,    "Disable shader cache"},
   {"linear_pe",      ETNA_DBG_LINEAR_PE, "Enable linear PE"},
   {"no_msaa",        ETNA_DBG_NO_MSAA, "Disable MSAA support"},
   {"shared_ts",      ETNA_DBG_SHARED_TS, "Enable TS sharing"},
   {"perf",           ETNA_DBG_PERF, "Enable performance warnings"},
   {"npu_parallel",   ETNA_DBG_NPU_PARALLEL, "Enable parallelism inside NPU batches (unsafe)"},
   {"npu_no_batching",ETNA_DBG_NPU_NO_BATCHING, "Disable batching NPU jobs"},
   {"no_texdesc"     ,ETNA_DBG_NO_TEXDESC, "Disable texture descriptor"},
   DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(etna_mesa_debug, "ETNA_MESA_DEBUG", etna_debug_options, 0)
int etna_mesa_debug = 0;

static void
etna_screen_destroy(struct pipe_screen *pscreen)
{
   struct etna_screen *screen = etna_screen(pscreen);

   if (screen->dummy_bo)
      etna_bo_del(screen->dummy_bo);

   if (screen->dummy_rt_reloc.bo)
      etna_bo_del(screen->dummy_rt_reloc.bo);

   if (screen->perfmon)
      etna_perfmon_del(screen->perfmon);

   util_dynarray_fini(&screen->supported_pm_queries);

   etna_shader_screen_fini(pscreen);

   if (screen->pipe_nn)
      etna_pipe_del(screen->pipe_nn);

   if (screen->pipe)
      etna_pipe_del(screen->pipe);

   if (screen->npu && screen->npu != screen->gpu)
      etna_gpu_del(screen->npu);

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

   snprintf(buffer, sizeof(buffer), "Vivante GC%x rev %04x", priv->info->model,
            priv->info->revision);

   return buffer;
}

static const char *
etna_screen_get_vendor(struct pipe_screen *pscreen)
{
   return "Mesa";
}

static const char *
etna_screen_get_device_vendor(struct pipe_screen *pscreen)
{
   return "Vivante";
}

static void
etna_init_single_shader_caps(struct etna_screen *screen, enum pipe_shader_type shader)
{
   struct pipe_shader_caps *caps =
      (struct pipe_shader_caps *)&screen->base.shader_caps[shader];

   bool ubo_enable = screen->info->halti >= 2;
   if (DBG_ENABLED(ETNA_DBG_DEQP))
      ubo_enable = true;

   caps->max_instructions =
   caps->max_alu_instructions =
   caps->max_tex_instructions =
   caps->max_tex_indirections = ETNA_MAX_TOKENS;

   caps->max_control_flow_depth = ETNA_MAX_DEPTH; /* XXX */

   /* Maximum number of inputs for the vertex shader is the number
    * of vertex elements - each element defines one vertex shader
    * input register.  For the fragment shader, this is the number
    * of varyings. */
   caps->max_inputs = shader == PIPE_SHADER_FRAGMENT ?
      screen->specs.max_varyings : screen->specs.vertex_max_elements;
   caps->max_outputs = screen->specs.max_vs_outputs;
   caps->max_temps = 64; /* Max native temporaries. */
   caps->max_const_buffers = ubo_enable ? ETNA_MAX_CONST_BUF : 1;
   caps->cont_supported = true;
   caps->indirect_temp_addr = true;
   caps->indirect_const_addr = true;
   caps->tgsi_sqrt_supported = VIV_FEATURE(screen, ETNA_FEATURE_HAS_SQRT_TRIG);
   caps->integers = screen->info->halti >= 2;

   caps->max_texture_samplers =
   caps->max_sampler_views = shader == PIPE_SHADER_FRAGMENT
      ? screen->specs.fragment_sampler_count
      : screen->specs.vertex_sampler_count;

   caps->max_const_buffer0_size =
      ubo_enable ? 16384 /* 16384 so state tracker enables UBOs */ :
      (shader == PIPE_SHADER_FRAGMENT
       ? screen->specs.max_ps_uniforms * sizeof(float[4])
       : screen->specs.max_vs_uniforms * sizeof(float[4]));

   caps->supported_irs =
      (1 << PIPE_SHADER_IR_TGSI) |
      (1 << PIPE_SHADER_IR_NIR);
}

static void
etna_init_shader_caps(struct etna_screen *screen)
{
   etna_init_single_shader_caps(screen, PIPE_SHADER_VERTEX);
   etna_init_single_shader_caps(screen, PIPE_SHADER_FRAGMENT);
}

static void
etna_init_screen_caps(struct etna_screen *screen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&screen->base.caps;

   u_init_pipe_screen_caps(&screen->base, 1);

   /* Supported features (boolean caps). */
   caps->blend_equation_separate = true;
   caps->fs_coord_origin_upper_left = true;
   caps->fs_coord_pixel_center_half_integer = true;
   caps->fragment_shader_texture_lod = true;
   caps->fragment_shader_derivatives = true;
   caps->texture_barrier = true;
   caps->quads_follow_provoking_vertex_convention = true;
   caps->tgsi_texcoord = true;
   caps->vertex_color_unclamped = true;
   caps->mixed_color_depth_bits = true;
   caps->mixed_framebuffer_sizes = true;
   caps->string_marker = true;
   caps->frontend_noop = true;
   caps->framebuffer_no_attachment = true;
   caps->vertex_input_alignment = PIPE_VERTEX_INPUT_ALIGNMENT_4BYTE;
   caps->native_fence_fd = screen->drm_version >= ETNA_DRM_VERSION_FENCE_FD;
   caps->fs_position_is_sysval = true;
   caps->fs_face_is_integer_sysval = true; /* note: not integer */
   caps->fs_point_is_sysval = false;

   /* Memory */
   caps->constant_buffer_offset_alignment = 256;
   caps->min_map_buffer_alignment = 4096;

   caps->npot_textures = true; /* VIV_FEATURE(priv->dev, chipMinorFeatures1, NON_POWER_OF_TWO); */

   caps->anisotropic_filter =
   caps->texture_swizzle =
   caps->primitive_restart =
   caps->primitive_restart_fixed_index = VIV_FEATURE(screen, ETNA_FEATURE_HALTI0);

   caps->alpha_test = !VIV_FEATURE(screen, ETNA_FEATURE_PE_NO_ALPHA_TEST);

   caps->draw_indirect = VIV_FEATURE(screen, ETNA_FEATURE_HALTI5);

   /* Unsupported features. */
   caps->texture_buffer_offset_alignment = false;
   caps->texrect = false;

   /* Stream output. */
   caps->max_stream_output_buffers = DBG_ENABLED(ETNA_DBG_DEQP) ? 4 : 0;
   caps->max_stream_output_separate_components = 0;
   caps->max_stream_output_interleaved_components = 0;

   caps->max_vertex_attrib_stride = 128;
   caps->max_vertex_element_src_offset = 255;
   caps->max_vertex_buffers = screen->info->gpu.stream_count;
   caps->vs_instanceid =
   caps->vertex_element_instance_divisor = VIV_FEATURE(screen, ETNA_FEATURE_HALTI2);


   /* Texturing. */
   caps->texture_half_float_linear = VIV_FEATURE(screen, ETNA_FEATURE_HALF_FLOAT);
   caps->texture_shadow_map = true;
   caps->max_texture_2d_size = screen->specs.max_texture_size;
   caps->max_texture_array_layers =
      screen->info->halti >= 0 ? screen->specs.max_texture_size : 0; /* TODO: verify */
   unsigned log2_max_tex_size = util_last_bit(screen->specs.max_texture_size);
   assert(log2_max_tex_size > 0);
   caps->max_texture_3d_levels = screen->info->halti < 0 ? 0 : log2_max_tex_size;
   caps->max_texture_cube_levels = log2_max_tex_size;

   caps->min_texel_offset = -8;
   caps->max_texel_offset = 7;
   caps->seamless_cube_map_per_texture = screen->specs.seamless_cube_map;

   /* Render targets. */
   caps->max_render_targets = VIV_FEATURE(screen, ETNA_FEATURE_HALTI2) ?
      /* If the GPU supports float formats we need to reserve half of
       * the available render targets for emulation proposes.
       */
      screen->specs.num_rts / 2 :
      screen->specs.num_rts;
   caps->indep_blend_enable =
   caps->indep_blend_func = screen->info->halti >= 5;

   /* Queries. */
   caps->occlusion_query =
   caps->conditional_render =
   caps->conditional_render_inverted = VIV_FEATURE(screen, ETNA_FEATURE_HALTI0);

   /* Preferences */
   caps->texture_transfer_modes = 0;
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
   caps->max_texture_upload_memory_budget = MIN2(system_memory / 32, 64 * 1024 * 1024);

   caps->max_varyings = screen->specs.max_varyings;

   /* Generate the bitmask of supported draw primitives. */
   uint32_t modes = 1 << MESA_PRIM_POINTS |
      1 << MESA_PRIM_LINES |
      1 << MESA_PRIM_LINE_STRIP |
      1 << MESA_PRIM_TRIANGLES |
      1 << MESA_PRIM_TRIANGLE_FAN;

   /* TODO: The bug relates only to indexed draws, but here we signal
    * that there is no support for triangle strips at all. This should
    * be refined.
    */
   if (VIV_FEATURE(screen, ETNA_FEATURE_BUG_FIXES8))
      modes |= 1 << MESA_PRIM_TRIANGLE_STRIP;

   if (VIV_FEATURE(screen, ETNA_FEATURE_LINE_LOOP))
      modes |= 1 << MESA_PRIM_LINE_LOOP;

   caps->supported_prim_modes =
   caps->supported_prim_modes_with_restart = modes;

   caps->pci_group =
   caps->pci_bus =
   caps->pci_device =
   caps->pci_function = 0;
   caps->video_memory = 0;
   caps->uma = true;
   caps->graphics = !VIV_FEATURE(screen, ETNA_FEATURE_COMPUTE_ONLY);

   caps->min_line_width =
   caps->min_line_width_aa =
   caps->min_point_size =
   caps->min_point_size_aa = 1;

   caps->point_size_granularity =
   caps->line_width_granularity = 0.1;

   caps->max_line_width =
   caps->max_line_width_aa =
   caps->max_point_size =
   caps->max_point_size_aa = 8192.0f;

   caps->max_texture_anisotropy = 16.0f;
   caps->max_texture_lod_bias = util_last_bit(screen->specs.max_texture_size);
}

static bool
gpu_supports_texture_target(struct etna_screen *screen,
                            enum pipe_texture_target target)
{
   if (target == PIPE_TEXTURE_CUBE_ARRAY)
      return false;

   /* pre-halti has no array/3D */
   if (screen->info->halti < 0 &&
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

   /* Requires split sampler support, which the driver doesn't support, yet. */
   if (!util_format_is_compressed(format) &&
       util_format_get_blocksizebits(format) > 64)
      return false;

   if (fmt == TEXTURE_FORMAT_ETC1)
      supported = VIV_FEATURE(screen, ETNA_FEATURE_ETC1_TEXTURE_COMPRESSION);

   if (fmt >= TEXTURE_FORMAT_DXT1 && fmt <= TEXTURE_FORMAT_DXT4_DXT5)
      supported = VIV_FEATURE(screen, ETNA_FEATURE_DXT_TEXTURE_COMPRESSION);

   if (util_format_is_srgb(format))
      supported = VIV_FEATURE(screen, ETNA_FEATURE_HALTI0);

   if (fmt & EXT_FORMAT)
      supported = VIV_FEATURE(screen, ETNA_FEATURE_HALTI0);

   if (fmt & ASTC_FORMAT) {
      supported = screen->specs.tex_astc;
   }

   if (util_format_is_snorm(format))
      supported = VIV_FEATURE(screen, ETNA_FEATURE_HALTI1);

   if (format != PIPE_FORMAT_S8_UINT_Z24_UNORM &&
       (util_format_is_pure_integer(format) || util_format_is_float(format)))
      supported = VIV_FEATURE(screen, ETNA_FEATURE_HALTI2);


   if (!supported)
      return false;

   if (texture_format_needs_swiz(format))
      return VIV_FEATURE(screen, ETNA_FEATURE_HALTI0);

   return true;
}

static bool
gpu_supports_render_format(struct etna_screen *screen, enum pipe_format format,
                           unsigned sample_count)
{
   const uint32_t fmt = translate_pe_format(format);

   if (fmt == ETNA_NO_MATCH)
      return false;

   /* Requires split target support, which the driver doesn't support, yet. */
   if (util_format_get_blocksizebits(format) > 64)
      return false;

   if (sample_count > 1) {
      /* Explicitly disabled. */
      if (DBG_ENABLED(ETNA_DBG_NO_MSAA))
         return false;

      /* The hardware supports it. */
      if (!VIV_FEATURE(screen, ETNA_FEATURE_MSAA))
         return false;

      /* Number of samples must be allowed. */
      if (!translate_samples_to_xyscale(sample_count, NULL, NULL))
         return false;

      /* On SMALL_MSAA hardware 2x MSAA does not work. */
      if (sample_count == 2 && VIV_FEATURE(screen, ETNA_FEATURE_SMALL_MSAA))
         return false;

      /* BLT/RS supports the format. */
      if (screen->specs.use_blt) {
         if (translate_blt_format(format) == ETNA_NO_MATCH)
            return false;
      } else {
         if (translate_rs_format(format) == ETNA_NO_MATCH)
            return false;
      }
   }

   if (format == PIPE_FORMAT_R8_UNORM)
      return VIV_FEATURE(screen, ETNA_FEATURE_HALTI5);

   /* figure out 8bpp RS clear to enable these formats */
   if (format == PIPE_FORMAT_R8_SINT || format == PIPE_FORMAT_R8_UINT)
      return VIV_FEATURE(screen, ETNA_FEATURE_HALTI5);

   if (util_format_is_srgb(format))
      return VIV_FEATURE(screen, ETNA_FEATURE_HALTI3);

   if (util_format_is_pure_integer(format) || util_format_is_float(format))
      return VIV_FEATURE(screen, ETNA_FEATURE_HALTI2);

   if (format == PIPE_FORMAT_R8G8_UNORM)
      return VIV_FEATURE(screen, ETNA_FEATURE_HALTI2);

   /* any other extended format is HALTI0 (only R10G10B10A2?) */
   if (fmt >= PE_FORMAT_R16F)
      return VIV_FEATURE(screen, ETNA_FEATURE_HALTI0);

   return true;
}

static bool
gpu_supports_vertex_format(struct etna_screen *screen, enum pipe_format format)
{
   if (translate_vertex_format_type(format) == ETNA_NO_MATCH)
      return false;

   if (util_format_is_pure_integer(format))
      return VIV_FEATURE(screen, ETNA_FEATURE_HALTI2);

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

   /* For ARB_framebuffer_no_attachments - Short-circuit the rest of the logic. */
   if (format == PIPE_FORMAT_NONE && usage & PIPE_BIND_RENDER_TARGET)
      return true;

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
           VIV_FEATURE(screen, ETNA_FEATURE_32_BIT_INDICES))) {
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

static int etna_get_num_modifiers(struct etna_screen *screen)
{
   int num = ARRAY_SIZE(supported_modifiers);

   /* don't advertise split tiled formats on single pipe/buffer GPUs */
   if (screen->specs.pixel_pipes == 1 || screen->specs.single_buffer)
      num = 3;

   return num;
}

static void
etna_screen_query_dmabuf_modifiers(struct pipe_screen *pscreen,
                                   enum pipe_format format, int max,
                                   uint64_t *modifiers,
                                   unsigned int *external_only, int *count)
{
   struct etna_screen *screen = etna_screen(pscreen);
   int num_base_mods = etna_get_num_modifiers(screen);
   int mods_multiplier = 1;
   int i, j;

   if (DBG_ENABLED(ETNA_DBG_SHARED_TS) &&
       VIV_FEATURE(screen, ETNA_FEATURE_FAST_CLEAR)) {
      /* If TS is supported expose the TS modifiers. GPUs with feature
       * CACHE128B256BPERLINE have both 128B and 256B color tile TS modes,
       * older cores support exactly one TS layout.
       */
      if (VIV_FEATURE(screen, ETNA_FEATURE_CACHE128B256BPERLINE))
         if (screen->specs.v4_compression &&
             translate_ts_format(format) != ETNA_NO_MATCH)
            mods_multiplier += 4;
         else
            mods_multiplier += 2;
      else
         mods_multiplier += 1;
   }

   if (max > num_base_mods * mods_multiplier)
      max = num_base_mods * mods_multiplier;

   if (!max) {
      modifiers = NULL;
      max = num_base_mods * mods_multiplier;
   }

   for (i = 0, *count = 0; *count < max && i < num_base_mods; i++) {
      for (j = 0; *count < max && j < mods_multiplier; j++, (*count)++) {
         uint64_t ts_mod;

         if (j == 0) {
            ts_mod = 0;
         } else if (VIV_FEATURE(screen, ETNA_FEATURE_CACHE128B256BPERLINE)) {
            switch (j) {
            case 1:
               ts_mod = VIVANTE_MOD_TS_128_4;
               break;
            case 2:
               ts_mod = VIVANTE_MOD_TS_256_4;
               break;
            case 3:
               ts_mod = VIVANTE_MOD_TS_128_4 | VIVANTE_MOD_COMP_DEC400;
               break;
            case 4:
               ts_mod = VIVANTE_MOD_TS_256_4 | VIVANTE_MOD_COMP_DEC400;
            }
         } else {
            if (screen->specs.bits_per_tile == 2)
               ts_mod = VIVANTE_MOD_TS_64_2;
            else
               ts_mod = VIVANTE_MOD_TS_64_4;
         }

         if (modifiers)
            modifiers[*count] = supported_modifiers[i] | ts_mod;
         if (external_only)
            external_only[*count] = util_format_is_yuv(format) ? 1 : 0;
      }
   }
}

static bool
etna_screen_is_dmabuf_modifier_supported(struct pipe_screen *pscreen,
                                         uint64_t modifier,
                                         enum pipe_format format,
                                         bool *external_only)
{
   struct etna_screen *screen = etna_screen(pscreen);
   int num_base_mods = etna_get_num_modifiers(screen);
   uint64_t base_mod = modifier & ~VIVANTE_MOD_EXT_MASK;
   uint64_t ts_mod = modifier & VIVANTE_MOD_TS_MASK;
   int i;

   for (i = 0; i < num_base_mods; i++) {
      if (base_mod != supported_modifiers[i])
         continue;

      if ((modifier & VIVANTE_MOD_COMP_DEC400) &&
          (!screen->specs.v4_compression || translate_ts_format(format) == ETNA_NO_MATCH))
         return false;

      if (ts_mod) {
         if (!VIV_FEATURE(screen, ETNA_FEATURE_FAST_CLEAR))
            return false;

         if (VIV_FEATURE(screen, ETNA_FEATURE_CACHE128B256BPERLINE)) {
            if (ts_mod != VIVANTE_MOD_TS_128_4 &&
                ts_mod != VIVANTE_MOD_TS_256_4)
               return false;
         } else {
            if ((screen->specs.bits_per_tile == 2 &&
                 ts_mod != VIVANTE_MOD_TS_64_2) ||
                (screen->specs.bits_per_tile == 4 &&
                 ts_mod != VIVANTE_MOD_TS_64_4))
               return false;
         }
      }

      if (external_only)
         *external_only = util_format_is_yuv(format) ? 1 : 0;

      return true;
   }

   return false;
}

static unsigned int
etna_screen_get_dmabuf_modifier_planes(struct pipe_screen *pscreen,
                                       uint64_t modifier,
                                       enum pipe_format format)
{
   unsigned planes = util_format_get_num_planes(format);

   if (modifier & VIVANTE_MOD_TS_MASK)
      return planes * 2;

   return planes;
}

static void
etna_determine_num_rts(struct etna_screen *screen)
{
   if (screen->info->halti >= 2)
      screen->specs.num_rts = 8;
   else if (screen->info->halti >= 0)
      screen->specs.num_rts = 4;
   else
      screen->specs.num_rts = 1;
}

static void
etna_determine_uniform_limits(struct etna_screen *screen)
{
   /* values for the non unified case are taken from
    * gcmCONFIGUREUNIFORMS in the Vivante kernel driver file
    * drivers/mxc/gpu-viv/hal/kernel/inc/gc_hal_base.h.
    */
   if (screen->info->halti >= 1) {
      /* with halti1 we use unified constant mode */
      screen->specs.max_vs_uniforms = screen->specs.max_ps_uniforms =
            MIN2(512, screen->info->gpu.num_constants - 64);
   } else if (screen->info->model == chipModel_GC2000 &&
              (screen->info->revision == 0x5118 ||
               screen->info->revision == 0x5140)) {
      screen->specs.max_vs_uniforms = 256;
      screen->specs.max_ps_uniforms = 64;
   } else if (screen->info->gpu.num_constants == 320) {
      screen->specs.max_vs_uniforms = 256;
      screen->specs.max_ps_uniforms = 64;
   } else if (screen->info->gpu.num_constants > 256 &&
              screen->info->model == chipModel_GC1000) {
      /* All GC1000 series chips can only support 64 uniforms for ps on non-unified const mode. */
      screen->specs.max_vs_uniforms = 256;
      screen->specs.max_ps_uniforms = 64;
   } else if (screen->info->gpu.num_constants > 256) {
      screen->specs.max_vs_uniforms = 256;
      screen->specs.max_ps_uniforms = 256;
   } else if (screen->info->gpu.num_constants == 256) {
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
   if (screen->info->halti >= 1) {
      screen->specs.vertex_sampler_offset = 16;
      screen->specs.fragment_sampler_count = 16;
      screen->specs.vertex_sampler_count = 16;
   } else {
      screen->specs.vertex_sampler_offset = 8;
      screen->specs.fragment_sampler_count = 8;
      screen->specs.vertex_sampler_count = 4;
   }

   if (screen->info->model == 0x400)
      screen->specs.vertex_sampler_count = 0;
}

static void
etna_get_specs(struct etna_screen *screen)
{
   const struct etna_core_info *info = screen->info;
   uint32_t instruction_count = 0;

   /* Copy all relevant limits from etna_core_info. */
   if (info->type == ETNA_CORE_GPU) {
      instruction_count = info->gpu.max_instructions;
      screen->specs.pixel_pipes = info->gpu.pixel_pipes;

      if (screen->npu)
         info = etna_gpu_get_core_info(screen->npu);
   }

   if (info->type == ETNA_CORE_NPU) {
      if (etna_core_has_feature(info, ETNA_FEATURE_NN_XYDP0))
         screen->specs.nn_core_version = 8;
      else if (etna_core_has_feature(info, ETNA_FEATURE_VIP_V7))
         screen->specs.nn_core_version = 7;
      else
         screen->specs.nn_core_version = 6;
   }

   screen->info->halti = info->halti;
   if (screen->info->halti >= 0)
      DBG("etnaviv: GPU arch: HALTI%d", screen->info->halti);
   else
      DBG("etnaviv: GPU arch: pre-HALTI");

   screen->specs.can_supertile =
      VIV_FEATURE(screen, ETNA_FEATURE_SUPER_TILED);
   screen->specs.bits_per_tile =
      !VIV_FEATURE(screen, ETNA_FEATURE_2BITPERTILE) ||
      VIV_FEATURE(screen, ETNA_FEATURE_CACHE128B256BPERLINE) ? 4 : 2;

   screen->specs.ts_clear_value =
      VIV_FEATURE(screen, ETNA_FEATURE_DEC400) ? 0xffffffff :
      screen->specs.bits_per_tile == 4 ? 0x11111111 : 0x55555555;

   screen->specs.vs_need_z_div =
      screen->info->model < 0x1000 && screen->info->model != 0x880;
   screen->specs.has_unified_instmem = instruction_count > 256;
   screen->specs.has_new_transcendentals =
      VIV_FEATURE(screen, ETNA_FEATURE_HAS_FAST_TRANSCENDENTALS);
   screen->specs.has_no_oneconst_limit =
      VIV_FEATURE(screen, ETNA_FEATURE_SH_NO_ONECONST_LIMIT);
   screen->specs.v4_compression =
      VIV_FEATURE(screen, ETNA_FEATURE_V4_COMPRESSION);
   screen->specs.seamless_cube_map =
      (screen->info->model != 0x880) && /* Seamless cubemap is broken on GC880? */
      VIV_FEATURE(screen, ETNA_FEATURE_SEAMLESS_CUBE_MAP);

   if (screen->info->halti >= 5) {
      /* GC7000 - this core must load shaders from memory. */
      screen->specs.vs_offset = 0;
      screen->specs.ps_offset = 0;
      screen->specs.max_instructions = 0; /* Do not program shaders manually */
      screen->specs.has_icache = true;
   } else if (VIV_FEATURE(screen, ETNA_FEATURE_INSTRUCTION_CACHE)) {
      /* GC3000 - this core is capable of loading shaders from memory. It can
       * also run shaders from unified instruction states as a fallback, but the
       * offsets are slightly different.
       */
      screen->specs.vs_offset = 0xC000;
      /* State 08000-0C000 mirrors 0C000-0E000, and the Vivante driver uses
       * this mirror for writing PS instructions, probably safest to do the
       * same.
       */
      screen->specs.ps_offset = 0x8000;
      /* maximum number instructions for non-icache use */
      screen->specs.max_instructions = instruction_count;
      screen->specs.has_icache = true;
   } else {
      if (instruction_count > 256) {
         /* unified instruction states */
         screen->specs.vs_offset = 0xC000;
         screen->specs.ps_offset = 0xC000;
      } else {
         screen->specs.vs_offset = 0x4000;
         screen->specs.ps_offset = 0x6000;
      }
      screen->specs.max_instructions = instruction_count;
      screen->specs.has_icache = false;
   }

   if (VIV_FEATURE(screen, ETNA_FEATURE_HALTI0)) {
      screen->specs.vertex_max_elements = 16;
   } else {
      /* Etna_viv documentation seems confused over the correct value
       * here so choose the lower to be safe: HALTI0 says 16 i.s.o.
       * 10, but VERTEX_ELEMENT_CONFIG register says 16 i.s.o. 12. */
      screen->specs.vertex_max_elements = 10;
   }

   etna_determine_num_rts(screen);
   etna_determine_uniform_limits(screen);
   etna_determine_sampler_limits(screen);

   if (screen->info->halti >= 5) {
      screen->specs.has_unified_uniforms = true;
      screen->specs.vs_uniforms_offset = VIVS_SH_HALTI5_UNIFORMS_MIRROR(0);
      screen->specs.ps_uniforms_offset = VIVS_SH_HALTI5_UNIFORMS(0);
   } else if (screen->info->halti >= 1) {
      /* unified uniform memory on GC3000 - HALTI1 feature bit is just a guess
      */
      screen->specs.has_unified_uniforms = true;
      screen->specs.vs_uniforms_offset = VIVS_SH_UNIFORMS(0);
      screen->specs.ps_uniforms_offset = VIVS_SH_UNIFORMS(0);
   } else {
      screen->specs.has_unified_uniforms = false;
      screen->specs.vs_uniforms_offset = VIVS_VS_UNIFORMS(0);
      screen->specs.ps_uniforms_offset = VIVS_PS_UNIFORMS(0);
   }

   screen->specs.max_vs_outputs = screen->info->halti >= 5 ? 32 : 16;

   screen->specs.max_varyings = MIN3(ETNA_NUM_VARYINGS,
                                     info->gpu.max_varyings,
                                     /* one output slot used for position */
                                     screen->specs.max_vs_outputs - 1);

   screen->specs.max_texture_size =
      VIV_FEATURE(screen, ETNA_FEATURE_TEXTURE_8K) ? 8192 : 2048;
   screen->specs.max_rendertarget_size =
      VIV_FEATURE(screen, ETNA_FEATURE_RENDERTARGET_8K) ? 8192 : 2048;

   screen->specs.single_buffer = VIV_FEATURE(screen, ETNA_FEATURE_SINGLE_BUFFER);
   if (screen->specs.single_buffer)
      DBG("etnaviv: Single buffer mode enabled with %d pixel pipes", screen->specs.pixel_pipes);

   screen->specs.tex_astc = VIV_FEATURE(screen, ETNA_FEATURE_TEXTURE_ASTC) &&
                            !VIV_FEATURE(screen, ETNA_FEATURE_NO_ASTC);

   screen->specs.use_blt = VIV_FEATURE(screen, ETNA_FEATURE_BLT_ENGINE);

   /* Only allow fast clear with MC2.0 or MMUv2, as the TS unit bypasses the
    * memory offset for the MMUv1 linear window on MC1.0 and we have no way to
    * fixup the address.
    */
   if (!VIV_FEATURE(screen, ETNA_FEATURE_MC20) &&
       !VIV_FEATURE(screen, ETNA_FEATURE_MMU_VERSION))
      etna_core_disable_feature(screen->info, ETNA_FEATURE_FAST_CLEAR);
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
                          enum pipe_shader_ir ir, enum pipe_shader_type shader)
{
   return etna_compiler_get_options(etna_screen(pscreen)->compiler);
}

static struct disk_cache *
etna_get_disk_shader_cache(struct pipe_screen *pscreen)
{
   struct etna_screen *screen = etna_screen(pscreen);
   struct etna_compiler *compiler = screen->compiler;

   return compiler->disk_cache;
}

static int
etna_screen_get_fd(struct pipe_screen *pscreen)
{
   struct etna_screen *screen = etna_screen(pscreen);
   return etna_device_fd(screen->dev);
}

struct pipe_screen *
etna_screen_create(struct etna_device *dev, struct etna_gpu *gpu,
                   struct etna_gpu *npu, struct renderonly *ro)
{
   struct etna_screen *screen = CALLOC_STRUCT(etna_screen);
   struct pipe_screen *pscreen;

   if (!screen)
      return NULL;

   if (!gpu)
      gpu = npu;

   pscreen = &screen->base;
   screen->dev = dev;
   screen->gpu = gpu;
   screen->npu = npu;
   screen->ro = ro;
   screen->info = etna_gpu_get_core_info(gpu);

   screen->drm_version = etnaviv_device_version(screen->dev);
   etna_mesa_debug = debug_get_option_etna_mesa_debug();

   /* Disable autodisable for correct rendering with TS */
   etna_mesa_debug |= ETNA_DBG_NO_AUTODISABLE;

   screen->pipe = etna_pipe_new(gpu, ETNA_PIPE_3D);
   if (!screen->pipe) {
      DBG("could not create 3d pipe");
      goto fail;
   }

   if (npu && gpu != npu) {
      screen->pipe_nn = etna_pipe_new(npu, ETNA_PIPE_3D);
      if (!screen->pipe_nn) {
         DBG("could not create nn pipe");
         goto fail;
      }
   }

   /* apply debug options that disable individual features */
   if (DBG_ENABLED(ETNA_DBG_NO_EARLY_Z))
      etna_core_disable_feature(screen->info, ETNA_FEATURE_NO_EARLY_Z);
   if (DBG_ENABLED(ETNA_DBG_NO_TS))
      etna_core_disable_feature(screen->info, ETNA_FEATURE_FAST_CLEAR);
   if (DBG_ENABLED(ETNA_DBG_NO_AUTODISABLE))
      etna_core_disable_feature(screen->info, ETNA_FEATURE_AUTO_DISABLE);
   if (DBG_ENABLED(ETNA_DBG_NO_SUPERTILE))
      etna_core_disable_feature(screen->info, ETNA_FEATURE_SUPER_TILED);
   if (DBG_ENABLED(ETNA_DBG_NO_SINGLEBUF))
      etna_core_disable_feature(screen->info, ETNA_FEATURE_SINGLE_BUFFER);
   if (!DBG_ENABLED(ETNA_DBG_LINEAR_PE))
      etna_core_disable_feature(screen->info, ETNA_FEATURE_LINEAR_PE);

   etna_get_specs(screen);

   if (screen->info->halti >= 5 && !etnaviv_device_softpin_capable(dev)) {
      DBG("halti5 requires softpin");
      goto fail;
   }

   pscreen->destroy = etna_screen_destroy;
   pscreen->get_screen_fd = etna_screen_get_fd;
   pscreen->get_compiler_options = etna_get_compiler_options;
   pscreen->get_disk_shader_cache = etna_get_disk_shader_cache;

   pscreen->get_name = etna_screen_get_name;
   pscreen->get_vendor = etna_screen_get_vendor;
   pscreen->get_device_vendor = etna_screen_get_device_vendor;

   pscreen->context_create = etna_context_create;
   pscreen->is_format_supported = etna_screen_is_format_supported;
   pscreen->query_dmabuf_modifiers = etna_screen_query_dmabuf_modifiers;
   pscreen->is_dmabuf_modifier_supported = etna_screen_is_dmabuf_modifier_supported;
   pscreen->get_dmabuf_modifier_planes = etna_screen_get_dmabuf_modifier_planes;

   if (!etna_shader_screen_init(pscreen))
      goto fail;

   etna_fence_screen_init(pscreen);
   etna_query_screen_init(pscreen);
   etna_resource_screen_init(pscreen);

   etna_init_shader_caps(screen);
   etna_init_screen_caps(screen);

   util_dynarray_init(&screen->supported_pm_queries, NULL);
   slab_create_parent(&screen->transfer_pool, sizeof(struct etna_transfer), 16);

   if (screen->drm_version >= ETNA_DRM_VERSION_PERFMON)
      etna_pm_query_setup(screen);


   /* create dummy RT buffer, used when rendering with no color buffer */
   screen->dummy_bo = etna_bo_new(screen->dev, 64 * 64 * 4, DRM_ETNA_GEM_CACHE_WC);
   if (!screen->dummy_bo)
      goto fail;

   screen->dummy_rt_reloc.bo = screen->dummy_bo;
   screen->dummy_rt_reloc.offset = 0;
   screen->dummy_rt_reloc.flags = ETNA_RELOC_READ | ETNA_RELOC_WRITE;

   if (screen->info->halti >= 5) {
      void *buf;

      /* create an empty dummy texture descriptor */
      screen->dummy_desc_reloc.bo = etna_bo_new(screen->dev, 0x100,
                                                DRM_ETNA_GEM_CACHE_WC);
      if (!screen->dummy_desc_reloc.bo)
         goto fail;

      buf = etna_bo_map(screen->dummy_desc_reloc.bo);
      etna_bo_cpu_prep(screen->dummy_desc_reloc.bo, DRM_ETNA_PREP_WRITE);
      memset(buf, 0, 0x100);
      etna_bo_cpu_fini(screen->dummy_desc_reloc.bo);
      screen->dummy_desc_reloc.offset = 0;
      screen->dummy_desc_reloc.flags = ETNA_RELOC_READ;
   }

   return pscreen;

fail:
   etna_screen_destroy(pscreen);
   return NULL;
}
