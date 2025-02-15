/*
 * Copyright (c) 2008-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#include "git_sha1.h" /* For MESA_GIT_SHA1 */
#include "compiler/nir/nir.h"
#include "util/format/u_format.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_process.h"
#include "util/u_screen.h"
#include "util/u_string.h"
#include "util/u_math.h"

#include "svga_winsys.h"
#include "svga_public.h"
#include "svga_context.h"
#include "svga_format.h"
#include "svga_screen.h"
#include "svga_tgsi.h"
#include "svga_resource_texture.h"
#include "svga_resource.h"
#include "svga_debug.h"

#include "vm_basic_types.h"
#include "svga3d_shaderdefs.h"
#include "VGPU10ShaderTokens.h"

/* NOTE: this constant may get moved into a svga3d*.h header file */
#define SVGA3D_DX_MAX_RESOURCE_SIZE (128 * 1024 * 1024)

#ifndef MESA_GIT_SHA1
#define MESA_GIT_SHA1 "(unknown git revision)"
#endif

#if MESA_DEBUG
int SVGA_DEBUG = 0;

static const struct debug_named_value svga_debug_flags[] = {
   { "dma",         DEBUG_DMA, NULL },
   { "tgsi",        DEBUG_TGSI, NULL },
   { "pipe",        DEBUG_PIPE, NULL },
   { "state",       DEBUG_STATE, NULL },
   { "screen",      DEBUG_SCREEN, NULL },
   { "tex",         DEBUG_TEX, NULL },
   { "swtnl",       DEBUG_SWTNL, NULL },
   { "const",       DEBUG_CONSTS, NULL },
   { "viewport",    DEBUG_VIEWPORT, NULL },
   { "views",       DEBUG_VIEWS, NULL },
   { "perf",        DEBUG_PERF, NULL },
   { "flush",       DEBUG_FLUSH, NULL },
   { "sync",        DEBUG_SYNC, NULL },
   { "cache",       DEBUG_CACHE, NULL },
   { "streamout",   DEBUG_STREAMOUT, NULL },
   { "query",       DEBUG_QUERY, NULL },
   { "samplers",    DEBUG_SAMPLERS, NULL },
   { "image",       DEBUG_IMAGE, NULL },
   { "uav",         DEBUG_UAV, NULL },
   { "retry",       DEBUG_RETRY, NULL },
   DEBUG_NAMED_VALUE_END
};
#endif

static const char *
svga_get_vendor( struct pipe_screen *pscreen )
{
   return "VMware, Inc.";
}


static const char *
svga_get_name( struct pipe_screen *pscreen )
{
   const char *build = "", *llvm = "", *mutex = "";
   static char name[100];
#if MESA_DEBUG
   /* Only return internal details in the MESA_DEBUG version:
    */
   build = "build: DEBUG;";
   mutex = "mutex: " PIPE_ATOMIC ";";
#else
   build = "build: RELEASE;";
#endif
#if DRAW_LLVM_AVAILABLE
   llvm = "LLVM;";
#endif

   snprintf(name, sizeof(name), "SVGA3D; %s %s %s", build, mutex, llvm);
   return name;
}


/** Helper for querying float-valued device cap */
static float
get_float_cap(struct svga_winsys_screen *sws, SVGA3dDevCapIndex cap,
              float defaultVal)
{
   SVGA3dDevCapResult result;
   if (sws->get_cap(sws, cap, &result))
      return result.f;
   else
      return defaultVal;
}


/** Helper for querying uint-valued device cap */
static unsigned
get_uint_cap(struct svga_winsys_screen *sws, SVGA3dDevCapIndex cap,
             unsigned defaultVal)
{
   SVGA3dDevCapResult result;
   if (sws->get_cap(sws, cap, &result))
      return result.u;
   else
      return defaultVal;
}


/** Helper for querying boolean-valued device cap */
static bool
get_bool_cap(struct svga_winsys_screen *sws, SVGA3dDevCapIndex cap,
             bool defaultVal)
{
   SVGA3dDevCapResult result;
   if (sws->get_cap(sws, cap, &result))
      return result.b;
   else
      return defaultVal;
}

#define COMMON_OPTIONS                                                        \
   .lower_extract_byte = true,                                                \
   .lower_extract_word = true,                                                \
   .lower_insert_byte = true,                                                 \
   .lower_insert_word = true,                                                 \
   .lower_int64_options = nir_lower_imul_2x32_64 | nir_lower_divmod64,        \
   .lower_fdph = true,                                                        \
   .lower_flrp64 = true,                                                      \
   .lower_ldexp = true,                                                       \
   .lower_uniforms_to_ubo = true,                                             \
   .lower_vector_cmp = true,                                                  \
   .lower_cs_local_index_to_id = true,                                        \
   .max_unroll_iterations = 32

#define VGPU10_OPTIONS                                                        \
   .lower_doubles_options = nir_lower_dfloor | nir_lower_dsign | nir_lower_dceil | nir_lower_dtrunc | nir_lower_dround_even, \
   .lower_fmod = true,                                                        \
   .lower_fpow = true,                                                        \
   .support_indirect_inputs = (uint8_t)BITFIELD_MASK(PIPE_SHADER_TYPES),      \
   .support_indirect_outputs = (uint8_t)BITFIELD_MASK(PIPE_SHADER_TYPES)

static const nir_shader_compiler_options svga_vgpu9_fragment_compiler_options = {
   COMMON_OPTIONS,
   .lower_bitops = true,
   .force_indirect_unrolling = nir_var_all,
   .force_indirect_unrolling_sampler = true,
   .no_integers = true,
};

static const nir_shader_compiler_options svga_vgpu9_vertex_compiler_options = {
   COMMON_OPTIONS,
   .lower_bitops = true,
   .force_indirect_unrolling = nir_var_function_temp,
   .force_indirect_unrolling_sampler = true,
   .no_integers = true,
   .support_indirect_inputs = BITFIELD_BIT(MESA_SHADER_VERTEX),
   .support_indirect_outputs = BITFIELD_BIT(MESA_SHADER_VERTEX),
};

static const nir_shader_compiler_options svga_vgpu10_compiler_options = {
   COMMON_OPTIONS,
   VGPU10_OPTIONS,
   .force_indirect_unrolling_sampler = true,
};

static const nir_shader_compiler_options svga_gl4_compiler_options = {
   COMMON_OPTIONS,
   VGPU10_OPTIONS,
};

static const void *
svga_get_compiler_options(struct pipe_screen *pscreen,
                          enum pipe_shader_ir ir,
                          enum pipe_shader_type shader)
{
   struct svga_screen *svgascreen = svga_screen(pscreen);
   struct svga_winsys_screen *sws = svgascreen->sws;

   assert(ir == PIPE_SHADER_IR_NIR);

   if (sws->have_gl43 || sws->have_sm5)
      return &svga_gl4_compiler_options;
   else if (sws->have_vgpu10)
      return &svga_vgpu10_compiler_options;
   else {
      if (shader == PIPE_SHADER_FRAGMENT)
         return &svga_vgpu9_fragment_compiler_options;
      else
         return &svga_vgpu9_vertex_compiler_options;
   }
}

static void
vgpu9_init_shader_caps(struct svga_screen *svgascreen)
{
   struct svga_winsys_screen *sws = svgascreen->sws;

   assert(!sws->have_vgpu10);

   struct pipe_shader_caps *caps =
      (struct pipe_shader_caps *)&svgascreen->screen.shader_caps[PIPE_SHADER_VERTEX];

   caps->max_instructions =
   caps->max_alu_instructions =
      get_uint_cap(sws, SVGA3D_DEVCAP_MAX_VERTEX_SHADER_INSTRUCTIONS, 512);

   caps->max_control_flow_depth = SVGA3D_MAX_NESTING_LEVEL;
   caps->max_inputs = 16;
   caps->max_outputs = 10;
   caps->max_const_buffer0_size = 256 * sizeof(float[4]);
   caps->max_const_buffers = 1;
   caps->max_temps =
      MIN2(get_uint_cap(sws, SVGA3D_DEVCAP_MAX_VERTEX_SHADER_TEMPS, 32),
           SVGA3D_TEMPREG_MAX);

   caps->indirect_const_addr = true;
   caps->supported_irs = (1 << PIPE_SHADER_IR_TGSI) | (1 << PIPE_SHADER_IR_NIR);

   caps = (struct pipe_shader_caps *)&svgascreen->screen.shader_caps[PIPE_SHADER_FRAGMENT];

   caps->max_instructions =
   caps->max_alu_instructions =
      get_uint_cap(sws, SVGA3D_DEVCAP_MAX_FRAGMENT_SHADER_INSTRUCTIONS, 512);
   caps->max_tex_instructions =
   caps->max_tex_indirections = 512;
   caps->max_control_flow_depth = SVGA3D_MAX_NESTING_LEVEL;
   caps->max_inputs = 10;
   caps->max_outputs = svgascreen->max_color_buffers;
   caps->max_const_buffer0_size = 224 * sizeof(float[4]);
   caps->max_const_buffers = 1;
   caps->max_temps =
      MIN2(get_uint_cap(sws, SVGA3D_DEVCAP_MAX_FRAGMENT_SHADER_TEMPS, 32),
           SVGA3D_TEMPREG_MAX);

   caps->max_texture_samplers =
   caps->max_sampler_views = 16;
   caps->supported_irs = (1 << PIPE_SHADER_IR_TGSI) | (1 << PIPE_SHADER_IR_NIR);
}

static void
vgpu10_init_shader_caps(struct svga_screen *svgascreen)
{
   struct svga_winsys_screen *sws = svgascreen->sws;

   assert(sws->have_vgpu10);

    for (unsigned i = 0; i <= PIPE_SHADER_COMPUTE; i++) {
       struct pipe_shader_caps *caps =
          (struct pipe_shader_caps *)&svgascreen->screen.shader_caps[i];

       switch (i) {
       case PIPE_SHADER_TESS_CTRL:
       case PIPE_SHADER_TESS_EVAL:
          if (!sws->have_sm5)
             continue;
          break;
       case PIPE_SHADER_COMPUTE:
          if (!sws->have_gl43)
             continue;
          break;
       default:
          break;
       }

       /* NOTE: we do not query the device for any caps/limits at this time */

       /* Generally the same limits for vertex, geometry and fragment shaders */
       caps->max_instructions =
       caps->max_alu_instructions =
       caps->max_tex_instructions =
       caps->max_tex_indirections = 64 * 1024;
       caps->max_control_flow_depth = 64;

       switch (i) {
       case PIPE_SHADER_FRAGMENT:
          caps->max_inputs = VGPU10_MAX_PS_INPUTS;
          caps->max_outputs = VGPU10_MAX_PS_OUTPUTS;
          break;
       case PIPE_SHADER_GEOMETRY:
          caps->max_inputs = svgascreen->max_gs_inputs;
          caps->max_outputs = VGPU10_MAX_GS_OUTPUTS;
          break;
       case PIPE_SHADER_TESS_CTRL:
          caps->max_inputs = VGPU11_MAX_HS_INPUT_CONTROL_POINTS;
          caps->max_outputs = VGPU11_MAX_HS_OUTPUTS;
          break;
       case PIPE_SHADER_TESS_EVAL:
          caps->max_inputs = VGPU11_MAX_DS_INPUT_CONTROL_POINTS;
          caps->max_outputs = VGPU11_MAX_DS_OUTPUTS;
          break;
       default:
          caps->max_inputs = svgascreen->max_vs_inputs;
          caps->max_outputs = svgascreen->max_vs_outputs;
          break;
       }

       caps->max_const_buffer0_size =
          VGPU10_MAX_CONSTANT_BUFFER_ELEMENT_COUNT * sizeof(float[4]);
       caps->max_const_buffers = svgascreen->max_const_buffers;
       caps->max_temps = VGPU10_MAX_TEMPS;
       /* XXX verify */
       caps->indirect_temp_addr = true;
       caps->indirect_const_addr = true;
       caps->cont_supported = true;
       caps->tgsi_sqrt_supported = true;
       caps->subroutines = true;
       caps->integers = true;
       caps->max_texture_samplers =
       caps->max_sampler_views =
          sws->have_gl43 ? PIPE_MAX_SAMPLERS : SVGA3D_DX_MAX_SAMPLERS;
       caps->supported_irs =
          sws->have_gl43 ? (1 << PIPE_SHADER_IR_TGSI) | (1 << PIPE_SHADER_IR_NIR) : 0;
       caps->max_shader_images = sws->have_gl43 ? SVGA_MAX_IMAGES : 0;
       caps->max_shader_buffers = sws->have_gl43 ? SVGA_MAX_SHADER_BUFFERS : 0;
       caps->max_hw_atomic_counters =
       caps->max_hw_atomic_counter_buffers = sws->have_gl43 ? SVGA_MAX_ATOMIC_BUFFERS : 0;
    }
}

static void
svga_init_shader_caps(struct svga_screen *svgascreen)
{
   struct svga_winsys_screen *sws = svgascreen->sws;
   if (sws->have_vgpu10)
      vgpu10_init_shader_caps(svgascreen);
   else
      vgpu9_init_shader_caps(svgascreen);
}

static void
svga_init_compute_caps(struct svga_screen *svgascreen)
{
   struct svga_winsys_screen *sws = svgascreen->sws;

   if (!sws->have_gl43)
      return;

   struct pipe_compute_caps *caps =
      (struct pipe_compute_caps *)&svgascreen->screen.compute_caps;

   caps->max_grid_size[0] =
   caps->max_grid_size[1] =
   caps->max_grid_size[2] = 65535;

   caps->max_block_size[0] = 1024;
   caps->max_block_size[1] = 1024;
   caps->max_block_size[2] = 64;

   caps->max_threads_per_block = 1024;
   caps->max_local_size = 32768;
}

static void
svga_init_screen_caps(struct svga_screen *svgascreen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&svgascreen->screen.caps;

   u_init_pipe_screen_caps(&svgascreen->screen, 0);

   struct svga_winsys_screen *sws = svgascreen->sws;
   SVGA3dDevCapResult result;

   caps->npot_textures = true;
   caps->mixed_framebuffer_sizes = true;
   caps->mixed_color_depth_bits = true;
   /*
    * "In virtually every OpenGL implementation and hardware,
    * GL_MAX_DUAL_SOURCE_DRAW_BUFFERS is 1"
    * http://www.opengl.org/wiki/Blending
    */
   caps->max_dual_source_render_targets = sws->have_vgpu10 ? 1 : 0;
   caps->anisotropic_filter = true;
   caps->max_render_targets = svgascreen->max_color_buffers;
   caps->occlusion_query = true;
   caps->texture_buffer_objects = sws->have_vgpu10;
   caps->texture_buffer_offset_alignment = sws->have_vgpu10 ? 16 : 0;

   caps->texture_swizzle = true;
   caps->constant_buffer_offset_alignment = 256;

   unsigned size = 1 << (SVGA_MAX_TEXTURE_LEVELS - 1);
   if (sws->get_cap(sws, SVGA3D_DEVCAP_MAX_TEXTURE_WIDTH, &result))
      size = MIN2(result.u, size);
   else
      size = 2048;
   if (sws->get_cap(sws, SVGA3D_DEVCAP_MAX_TEXTURE_HEIGHT, &result))
      size = MIN2(result.u, size);
   else
      size = 2048;
   caps->max_texture_2d_size = size;

   caps->max_texture_3d_levels =
      sws->get_cap(sws, SVGA3D_DEVCAP_MAX_VOLUME_EXTENT, &result) ?
      MIN2(util_logbase2(result.u) + 1, SVGA_MAX_TEXTURE_LEVELS) : 8; /* max 128x128x128 */

   caps->max_texture_cube_levels = util_last_bit(caps->max_texture_2d_size);

   caps->max_texture_array_layers =
      sws->have_sm5 ? SVGA3D_SM5_MAX_SURFACE_ARRAYSIZE :
      (sws->have_vgpu10 ? SVGA3D_SM4_MAX_SURFACE_ARRAYSIZE : 0);

   caps->blend_equation_separate = true; /* req. for GL 1.5 */

   caps->fs_coord_origin_upper_left = true;
   caps->fs_coord_pixel_center_half_integer = sws->have_vgpu10;
   caps->fs_coord_pixel_center_integer = !sws->have_vgpu10;

   /* The color outputs of vertex shaders are not clamped */
   caps->vertex_color_unclamped = true;
   caps->vertex_color_clamped = sws->have_vgpu10;

   caps->glsl_feature_level =
   caps->glsl_feature_level_compatibility =
      sws->have_gl43 ? 430 : (sws->have_sm5 ? 410 : (sws->have_vgpu10 ? 330 : 120));

   caps->texture_transfer_modes = 0;

   caps->fragment_shader_texture_lod = true;
   caps->fragment_shader_derivatives = true;

   caps->depth_clip_disable =
   caps->indep_blend_enable =
   caps->conditional_render =
   caps->query_timestamp =
   caps->vs_instanceid =
   caps->vertex_element_instance_divisor =
   caps->seamless_cube_map =
   caps->fake_sw_msaa = sws->have_vgpu10;

   caps->max_stream_output_buffers = sws->have_vgpu10 ? SVGA3D_DX_MAX_SOTARGETS : 0;
   caps->max_stream_output_separate_components = sws->have_vgpu10 ? 4 : 0;
   caps->max_stream_output_interleaved_components =
      sws->have_sm5 ? SVGA3D_MAX_STREAMOUT_DECLS :
      (sws->have_vgpu10 ? SVGA3D_MAX_DX10_STREAMOUT_DECLS : 0);
   caps->stream_output_pause_resume = sws->have_sm5;
   caps->stream_output_interleave_buffers = sws->have_sm5;
   caps->texture_multisample = svgascreen->ms_samples;

   /* convert bytes to texels for the case of the largest texel
    * size: float[4].
    */
   caps->max_texel_buffer_elements =
      SVGA3D_DX_MAX_RESOURCE_SIZE / (4 * sizeof(float));

   caps->min_texel_offset = sws->have_vgpu10 ? VGPU10_MIN_TEXEL_FETCH_OFFSET : 0;
   caps->max_texel_offset = sws->have_vgpu10 ? VGPU10_MAX_TEXEL_FETCH_OFFSET : 0;

   caps->min_texture_gather_offset = 0;
   caps->max_texture_gather_offset = 0;

   caps->max_geometry_output_vertices = sws->have_vgpu10 ? 256 : 0;
   caps->max_geometry_total_output_components = sws->have_vgpu10 ? 1024 : 0;

   /* may be a sw fallback, depending on restart index */
   caps->primitive_restart = true;
   caps->primitive_restart_fixed_index = true;

   caps->generate_mipmap = sws->have_generate_mipmap_cmd;

   caps->native_fence_fd = sws->have_fence_fd;

   caps->quads_follow_provoking_vertex_convention = true;

   caps->cube_map_array =
   caps->indep_blend_func =
   caps->sample_shading =
   caps->force_persample_interp =
   caps->texture_query_lod = sws->have_sm4_1;

   /* SM4_1 supports only single-channel textures where as SM5 supports
    * all four channel textures */
   caps->max_texture_gather_components = sws->have_sm5 ? 4 : (sws->have_sm4_1 ? 1 : 0);
   caps->draw_indirect = sws->have_sm5;
   caps->max_vertex_streams = sws->have_sm5 ? 4 : 0;
   caps->compute = sws->have_gl43;
   /* According to the spec, max varyings does not include the components
    * for position, so remove one count from the max for position.
    */
   caps->max_varyings = sws->have_vgpu10 ? VGPU10_MAX_PS_INPUTS-1 : 10;
   caps->buffer_map_persistent_coherent = sws->have_coherent;

   caps->start_instance = sws->have_sm5;
   caps->robust_buffer_access_behavior = sws->have_sm5;

   caps->sampler_view_target = sws->have_gl43;

   caps->framebuffer_no_attachment = sws->have_gl43;

   caps->clip_halfz = sws->have_gl43;
   caps->shareable_shaders = false;

   caps->pci_group =
   caps->pci_bus =
   caps->pci_device =
   caps->pci_function = 0;
   caps->shader_buffer_offset_alignment = sws->have_gl43 ? 16 : 0;

   caps->max_combined_shader_output_resources =
   caps->max_combined_shader_buffers = sws->have_gl43 ? SVGA_MAX_SHADER_BUFFERS : 0;
   caps->max_combined_hw_atomic_counters =
   caps->max_combined_hw_atomic_counter_buffers =
      sws->have_gl43 ? SVGA_MAX_ATOMIC_BUFFERS : 0;
   caps->min_map_buffer_alignment = 64;
   caps->vertex_input_alignment =
      sws->have_vgpu10 ? PIPE_VERTEX_INPUT_ALIGNMENT_ELEMENT : PIPE_VERTEX_INPUT_ALIGNMENT_4BYTE;
   caps->max_vertex_attrib_stride = 2048;

   assert((!sws->have_vgpu10 && svgascreen->max_viewports == 1) ||
          (sws->have_vgpu10 &&
           svgascreen->max_viewports == SVGA3D_DX_MAX_VIEWPORTS));
   caps->max_viewports = svgascreen->max_viewports;

   caps->endianness = PIPE_ENDIAN_LITTLE;

   caps->vendor_id = 0x15ad; /* VMware Inc. */
   caps->device_id = sws->device_id ? sws->device_id : 0x0405; /* assume SVGA II */
   caps->video_memory = 1; /* XXX: Query the host ? */
   caps->copy_between_compressed_and_plain_formats = sws->have_vgpu10;
   caps->doubles = sws->have_sm5;
   caps->uma = false;
   caps->allow_mapped_buffers_during_execution = false;
   caps->tgsi_div = true;
   caps->max_gs_invocations = 32;
   caps->max_shader_buffer_size = 1 << 27;
   /* Verify this once protocol is finalized. Setting it to minimum value. */
   caps->max_shader_patch_varyings = sws->have_sm5 ? 30 : 0;
   caps->texture_float_linear = true;
   caps->texture_half_float_linear = true;
   caps->tgsi_texcoord = sws->have_vgpu10 ? 1 : 0;
   caps->image_store_formatted = sws->have_gl43;

   caps->min_line_width =
   caps->min_line_width_aa =
   caps->min_point_size =
   caps->min_point_size_aa = 1;
   caps->point_size_granularity =
   caps->line_width_granularity = 0.1;
   caps->max_line_width = svgascreen->maxLineWidth;
   caps->max_line_width_aa = svgascreen->maxLineWidthAA;

   caps->max_point_size =
   caps->max_point_size_aa = svgascreen->maxPointSize;

   caps->max_texture_anisotropy =
      get_uint_cap(sws, SVGA3D_DEVCAP_MAX_TEXTURE_ANISOTROPY, 4);

   caps->max_texture_lod_bias = 15.0;
}

static void
svga_fence_reference(struct pipe_screen *screen,
                     struct pipe_fence_handle **ptr,
                     struct pipe_fence_handle *fence)
{
   struct svga_winsys_screen *sws = svga_screen(screen)->sws;
   sws->fence_reference(sws, ptr, fence);
}


static bool
svga_fence_finish(struct pipe_screen *screen,
                  struct pipe_context *ctx,
                  struct pipe_fence_handle *fence,
                  uint64_t timeout)
{
   struct svga_winsys_screen *sws = svga_screen(screen)->sws;
   bool retVal;

   SVGA_STATS_TIME_PUSH(sws, SVGA_STATS_TIME_FENCEFINISH);

   if (!timeout) {
      retVal = sws->fence_signalled(sws, fence, 0) == 0;
   }
   else {
      SVGA_DBG(DEBUG_DMA|DEBUG_PERF, "%s fence_ptr %p\n",
               __func__, fence);

      retVal = sws->fence_finish(sws, fence, timeout, 0) == 0;
   }

   SVGA_STATS_TIME_POP(sws);

   return retVal;
}


static int
svga_fence_get_fd(struct pipe_screen *screen,
                  struct pipe_fence_handle *fence)
{
   struct svga_winsys_screen *sws = svga_screen(screen)->sws;

   return sws->fence_get_fd(sws, fence, true);
}


static int
svga_get_driver_query_info(struct pipe_screen *screen,
                           unsigned index,
                           struct pipe_driver_query_info *info)
{
#define QUERY(NAME, ENUM, UNITS) \
   {NAME, ENUM, {0}, UNITS, PIPE_DRIVER_QUERY_RESULT_TYPE_AVERAGE, 0, 0x0}

   static const struct pipe_driver_query_info queries[] = {
      /* per-frame counters */
      QUERY("num-draw-calls", SVGA_QUERY_NUM_DRAW_CALLS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-fallbacks", SVGA_QUERY_NUM_FALLBACKS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-flushes", SVGA_QUERY_NUM_FLUSHES,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-validations", SVGA_QUERY_NUM_VALIDATIONS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("map-buffer-time", SVGA_QUERY_MAP_BUFFER_TIME,
            PIPE_DRIVER_QUERY_TYPE_MICROSECONDS),
      QUERY("num-buffers-mapped", SVGA_QUERY_NUM_BUFFERS_MAPPED,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-textures-mapped", SVGA_QUERY_NUM_TEXTURES_MAPPED,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-bytes-uploaded", SVGA_QUERY_NUM_BYTES_UPLOADED,
            PIPE_DRIVER_QUERY_TYPE_BYTES),
      QUERY("num-command-buffers", SVGA_QUERY_NUM_COMMAND_BUFFERS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("command-buffer-size", SVGA_QUERY_COMMAND_BUFFER_SIZE,
            PIPE_DRIVER_QUERY_TYPE_BYTES),
      QUERY("flush-time", SVGA_QUERY_FLUSH_TIME,
            PIPE_DRIVER_QUERY_TYPE_MICROSECONDS),
      QUERY("surface-write-flushes", SVGA_QUERY_SURFACE_WRITE_FLUSHES,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-readbacks", SVGA_QUERY_NUM_READBACKS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-resource-updates", SVGA_QUERY_NUM_RESOURCE_UPDATES,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-buffer-uploads", SVGA_QUERY_NUM_BUFFER_UPLOADS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-const-buf-updates", SVGA_QUERY_NUM_CONST_BUF_UPDATES,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-const-updates", SVGA_QUERY_NUM_CONST_UPDATES,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-shader-relocations", SVGA_QUERY_NUM_SHADER_RELOCATIONS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-surface-relocations", SVGA_QUERY_NUM_SURFACE_RELOCATIONS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),

      /* running total counters */
      QUERY("memory-used", SVGA_QUERY_MEMORY_USED,
            PIPE_DRIVER_QUERY_TYPE_BYTES),
      QUERY("num-shaders", SVGA_QUERY_NUM_SHADERS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-resources", SVGA_QUERY_NUM_RESOURCES,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-state-objects", SVGA_QUERY_NUM_STATE_OBJECTS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-surface-views", SVGA_QUERY_NUM_SURFACE_VIEWS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-generate-mipmap", SVGA_QUERY_NUM_GENERATE_MIPMAP,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-failed-allocations", SVGA_QUERY_NUM_FAILED_ALLOCATIONS,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
      QUERY("num-commands-per-draw", SVGA_QUERY_NUM_COMMANDS_PER_DRAW,
            PIPE_DRIVER_QUERY_TYPE_FLOAT),
      QUERY("shader-mem-used", SVGA_QUERY_SHADER_MEM_USED,
            PIPE_DRIVER_QUERY_TYPE_UINT64),
   };
#undef QUERY

   if (!info)
      return ARRAY_SIZE(queries);

   if (index >= ARRAY_SIZE(queries))
      return 0;

   *info = queries[index];
   return 1;
}


static void
init_logging(struct pipe_screen *screen)
{
   struct svga_screen *svgascreen = svga_screen(screen);
   static const char *log_prefix = "Mesa: ";
   char host_log[1000];

   /* Log Version to Host */
   snprintf(host_log, sizeof(host_log) - strlen(log_prefix),
            "%s%s\n", log_prefix, svga_get_name(screen));
   svgascreen->sws->host_log(svgascreen->sws, host_log);

   snprintf(host_log, sizeof(host_log) - strlen(log_prefix),
            "%s" PACKAGE_VERSION MESA_GIT_SHA1, log_prefix);
   svgascreen->sws->host_log(svgascreen->sws, host_log);

   /* If the SVGA_EXTRA_LOGGING env var is set, log the process's command
    * line (program name and arguments).
    */
   if (debug_get_bool_option("SVGA_EXTRA_LOGGING", false)) {
      char cmdline[1000];
      if (util_get_command_line(cmdline, sizeof(cmdline))) {
         snprintf(host_log, sizeof(host_log) - strlen(log_prefix),
                  "%s%s\n", log_prefix, cmdline);
         svgascreen->sws->host_log(svgascreen->sws, host_log);
      }
   }
}


/**
 * no-op logging function to use when SVGA_NO_LOGGING is set.
 */
static void
nop_host_log(struct svga_winsys_screen *sws, const char *message)
{
   /* nothing */
}


static void
svga_destroy_screen( struct pipe_screen *screen )
{
   struct svga_screen *svgascreen = svga_screen(screen);

   svga_screen_cache_cleanup(svgascreen);

   mtx_destroy(&svgascreen->swc_mutex);
   mtx_destroy(&svgascreen->tex_mutex);

   svgascreen->sws->destroy(svgascreen->sws);

   FREE(svgascreen);
}


static int
svga_screen_get_fd( struct pipe_screen *screen )
{
   struct svga_winsys_screen *sws = svga_screen(screen)->sws;

   return sws->get_fd(sws);
}


/**
 * Create a new svga_screen object
 */
struct pipe_screen *
svga_screen_create(struct svga_winsys_screen *sws)
{
   struct svga_screen *svgascreen;
   struct pipe_screen *screen;

#if MESA_DEBUG
   SVGA_DEBUG = debug_get_flags_option("SVGA_DEBUG", svga_debug_flags, 0 );
#endif

   svgascreen = CALLOC_STRUCT(svga_screen);
   if (!svgascreen)
      goto error1;

   svgascreen->debug.force_level_surface_view =
      debug_get_bool_option("SVGA_FORCE_LEVEL_SURFACE_VIEW", false);
   svgascreen->debug.force_surface_view =
      debug_get_bool_option("SVGA_FORCE_SURFACE_VIEW", false);
   svgascreen->debug.force_sampler_view =
      debug_get_bool_option("SVGA_FORCE_SAMPLER_VIEW", false);
   svgascreen->debug.no_surface_view =
      debug_get_bool_option("SVGA_NO_SURFACE_VIEW", false);
   svgascreen->debug.no_sampler_view =
      debug_get_bool_option("SVGA_NO_SAMPLER_VIEW", false);
   svgascreen->debug.no_cache_index_buffers =
      debug_get_bool_option("SVGA_NO_CACHE_INDEX_BUFFERS", false);

   screen = &svgascreen->screen;

   screen->destroy = svga_destroy_screen;
   screen->get_name = svga_get_name;
   screen->get_vendor = svga_get_vendor;
   screen->get_device_vendor = svga_get_vendor; // TODO actual device vendor
   screen->get_screen_fd = svga_screen_get_fd;
   screen->get_compiler_options = svga_get_compiler_options;
   screen->get_timestamp = NULL;
   screen->is_format_supported = svga_is_format_supported;
   screen->context_create = svga_context_create;
   screen->fence_reference = svga_fence_reference;
   screen->fence_finish = svga_fence_finish;
   screen->fence_get_fd = svga_fence_get_fd;

   screen->get_driver_query_info = svga_get_driver_query_info;

   svgascreen->sws = sws;

   svga_init_screen_resource_functions(svgascreen);

   if (sws->get_hw_version) {
      svgascreen->hw_version = sws->get_hw_version(sws);
   } else {
      svgascreen->hw_version = SVGA3D_HWVERSION_WS65_B1;
   }

   if (svgascreen->hw_version < SVGA3D_HWVERSION_WS8_B1) {
      /* too old for 3D acceleration */
      debug_printf("Hardware version 0x%x is too old for accerated 3D\n",
                   svgascreen->hw_version);
      goto error2;
   }

   if (sws->have_gl43) {
      svgascreen->forcedSampleCount =
         get_uint_cap(sws, SVGA3D_DEVCAP_MAX_FORCED_SAMPLE_COUNT, 0);

      sws->have_gl43 = sws->have_gl43 && (svgascreen->forcedSampleCount >= 4);

      /* Allow a temporary environment variable to enable/disable GL43 support.
       */
      sws->have_gl43 =
         debug_get_bool_option("SVGA_GL43", sws->have_gl43);

      svgascreen->debug.sampler_state_mapping =
         debug_get_bool_option("SVGA_SAMPLER_STATE_MAPPING", false);
   }
   else {
      /* sampler state mapping code is only enabled with GL43
       * due to the limitation in SW Renderer. (VMware bug 2825014)
       */
      svgascreen->debug.sampler_state_mapping = false;
   }

   debug_printf("%s enabled\n",
                sws->have_gl43 ? "SM5+" :
                sws->have_sm5 ? "SM5" :
                sws->have_sm4_1 ? "SM4_1" :
                sws->have_vgpu10 ? "VGPU10" : "VGPU9");

   debug_printf("Mesa: %s %s (%s)\n", svga_get_name(screen),
                PACKAGE_VERSION, MESA_GIT_SHA1);

   /*
    * The D16, D24X8, and D24S8 formats always do an implicit shadow compare
    * when sampled from, where as the DF16, DF24, and D24S8_INT do not.  So
    * we prefer the later when available.
    *
    * This mimics hardware vendors extensions for D3D depth sampling. See also
    * http://aras-p.info/texts/D3D9GPUHacks.html
    */

   {
      bool has_df16, has_df24, has_d24s8_int;
      SVGA3dSurfaceFormatCaps caps;
      SVGA3dSurfaceFormatCaps mask;
      mask.value = 0;
      mask.zStencil = 1;
      mask.texture = 1;

      svgascreen->depth.z16 = SVGA3D_Z_D16;
      svgascreen->depth.x8z24 = SVGA3D_Z_D24X8;
      svgascreen->depth.s8z24 = SVGA3D_Z_D24S8;

      svga_get_format_cap(svgascreen, SVGA3D_Z_DF16, &caps);
      has_df16 = (caps.value & mask.value) == mask.value;

      svga_get_format_cap(svgascreen, SVGA3D_Z_DF24, &caps);
      has_df24 = (caps.value & mask.value) == mask.value;

      svga_get_format_cap(svgascreen, SVGA3D_Z_D24S8_INT, &caps);
      has_d24s8_int = (caps.value & mask.value) == mask.value;

      /* XXX: We might want some other logic here.
       * Like if we only have d24s8_int we should
       * emulate the other formats with that.
       */
      if (has_df16) {
         svgascreen->depth.z16 = SVGA3D_Z_DF16;
      }
      if (has_df24) {
         svgascreen->depth.x8z24 = SVGA3D_Z_DF24;
      }
      if (has_d24s8_int) {
         svgascreen->depth.s8z24 = SVGA3D_Z_D24S8_INT;
      }
   }

   /* Query device caps
    */
   if (sws->have_vgpu10) {
      svgascreen->haveProvokingVertex
         = get_bool_cap(sws, SVGA3D_DEVCAP_DX_PROVOKING_VERTEX, false);
      svgascreen->haveLineSmooth = true;
      svgascreen->maxPointSize = 80.0F;
      svgascreen->max_color_buffers = SVGA3D_DX_MAX_RENDER_TARGETS;

      /* Multisample samples per pixel */
      if (sws->have_sm4_1 && debug_get_bool_option("SVGA_MSAA", true)) {
         if (get_bool_cap(sws, SVGA3D_DEVCAP_MULTISAMPLE_2X, false))
            svgascreen->ms_samples |= 1 << 1;
         if (get_bool_cap(sws, SVGA3D_DEVCAP_MULTISAMPLE_4X, false))
            svgascreen->ms_samples |= 1 << 3;
      }

      if (sws->have_sm5 && debug_get_bool_option("SVGA_MSAA", true)) {
         if (get_bool_cap(sws, SVGA3D_DEVCAP_MULTISAMPLE_8X, false))
            svgascreen->ms_samples |= 1 << 7;
      }

      /* Maximum number of constant buffers */
      if (sws->have_gl43) {
         svgascreen->max_const_buffers = SVGA_MAX_CONST_BUFS;
      }
      else {
         svgascreen->max_const_buffers =
            get_uint_cap(sws, SVGA3D_DEVCAP_DX_MAX_CONSTANT_BUFFERS, 1);
         svgascreen->max_const_buffers = MIN2(svgascreen->max_const_buffers,
                                              SVGA_MAX_CONST_BUFS);
      }

      svgascreen->haveBlendLogicops =
         get_bool_cap(sws, SVGA3D_DEVCAP_LOGIC_BLENDOPS, false);

      screen->is_format_supported = svga_is_dx_format_supported;

      svgascreen->max_viewports = SVGA3D_DX_MAX_VIEWPORTS;

      /* Shader limits */
      if (sws->have_sm4_1) {
         svgascreen->max_vs_inputs  = VGPU10_1_MAX_VS_INPUTS;
         svgascreen->max_vs_outputs = VGPU10_1_MAX_VS_OUTPUTS;
         svgascreen->max_gs_inputs  = VGPU10_1_MAX_GS_INPUTS;
      }
      else {
         svgascreen->max_vs_inputs  = VGPU10_MAX_VS_INPUTS;
         svgascreen->max_vs_outputs = VGPU10_MAX_VS_OUTPUTS;
         svgascreen->max_gs_inputs  = VGPU10_MAX_GS_INPUTS;
      }
   }
   else {
      /* VGPU9 */
      unsigned vs_ver = get_uint_cap(sws, SVGA3D_DEVCAP_VERTEX_SHADER_VERSION,
                                     SVGA3DVSVERSION_NONE);
      unsigned fs_ver = get_uint_cap(sws, SVGA3D_DEVCAP_FRAGMENT_SHADER_VERSION,
                                     SVGA3DPSVERSION_NONE);

      /* we require Shader model 3.0 or later */
      if (fs_ver < SVGA3DPSVERSION_30 || vs_ver < SVGA3DVSVERSION_30) {
         goto error2;
      }

      svgascreen->haveProvokingVertex = false;

      svgascreen->haveLineSmooth =
         get_bool_cap(sws, SVGA3D_DEVCAP_LINE_AA, false);

      svgascreen->maxPointSize =
         get_float_cap(sws, SVGA3D_DEVCAP_MAX_POINT_SIZE, 1.0f);
      /* Keep this to a reasonable size to avoid failures in conform/pntaa.c */
      svgascreen->maxPointSize = MIN2(svgascreen->maxPointSize, 80.0f);

      /* The SVGA3D device always supports 4 targets at this time, regardless
       * of what querying SVGA3D_DEVCAP_MAX_RENDER_TARGETS might return.
       */
      svgascreen->max_color_buffers = 4;

      /* Only support one constant buffer
       */
      svgascreen->max_const_buffers = 1;

      /* No multisampling */
      svgascreen->ms_samples = 0;

      /* Only one viewport */
      svgascreen->max_viewports = 1;

      /* Shader limits */
      svgascreen->max_vs_inputs  = 16;
      svgascreen->max_vs_outputs = 10;
      svgascreen->max_gs_inputs  = 0;
   }

   /* common VGPU9 / VGPU10 caps */
   svgascreen->haveLineStipple =
      get_bool_cap(sws, SVGA3D_DEVCAP_LINE_STIPPLE, false);

   svgascreen->maxLineWidth =
      MAX2(1.0, get_float_cap(sws, SVGA3D_DEVCAP_MAX_LINE_WIDTH, 1.0f));

   svgascreen->maxLineWidthAA =
      MAX2(1.0, get_float_cap(sws, SVGA3D_DEVCAP_MAX_AA_LINE_WIDTH, 1.0f));

   if (0) {
      debug_printf("svga: haveProvokingVertex %u\n",
                   svgascreen->haveProvokingVertex);
      debug_printf("svga: haveLineStip %u  "
                   "haveLineSmooth %u  maxLineWidth %.2f  maxLineWidthAA %.2f\n",
                   svgascreen->haveLineStipple, svgascreen->haveLineSmooth,
                   svgascreen->maxLineWidth, svgascreen->maxLineWidthAA);
      debug_printf("svga: maxPointSize %g\n", svgascreen->maxPointSize);
      debug_printf("svga: msaa samples mask: 0x%x\n", svgascreen->ms_samples);
   }

   (void) mtx_init(&svgascreen->tex_mutex, mtx_plain);
   (void) mtx_init(&svgascreen->swc_mutex, mtx_plain | mtx_recursive);

   svga_screen_cache_init(svgascreen);

   svga_init_shader_caps(svgascreen);
   svga_init_compute_caps(svgascreen);
   svga_init_screen_caps(svgascreen);

   if (debug_get_bool_option("SVGA_NO_LOGGING", false) == true) {
      svgascreen->sws->host_log = nop_host_log;
   } else {
      init_logging(screen);
   }

   return screen;
error2:
   FREE(svgascreen);
error1:
   return NULL;
}


struct svga_winsys_screen *
svga_winsys_screen(struct pipe_screen *screen)
{
   return svga_screen(screen)->sws;
}


#if MESA_DEBUG
struct svga_screen *
svga_screen(struct pipe_screen *screen)
{
   assert(screen);
   assert(screen->destroy == svga_destroy_screen);
   return (struct svga_screen *)screen;
}
#endif
