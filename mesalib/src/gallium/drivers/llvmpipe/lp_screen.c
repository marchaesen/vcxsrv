/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#include "util/u_memory.h"
#include "util/u_math.h"
#include "util/u_cpu_detect.h"
#include "util/format/u_format.h"
#include "util/u_screen.h"
#include "util/u_string.h"
#include "util/format/u_format_s3tc.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "draw/draw_context.h"
#include "gallivm/lp_bld_type.h"
#include "gallivm/lp_bld_nir.h"
#include "util/disk_cache.h"
#include "util/hex.h"
#include "util/os_misc.h"
#include "util/os_time.h"
#include "util/u_helpers.h"
#include "util/anon_file.h"
#include "lp_texture.h"
#include "lp_fence.h"
#include "lp_jit.h"
#include "lp_screen.h"
#include "lp_context.h"
#include "lp_debug.h"
#include "lp_public.h"
#include "lp_limits.h"
#include "lp_rast.h"
#include "lp_cs_tpool.h"
#include "lp_flush.h"

#include "frontend/sw_winsys.h"

#include "nir.h"

#ifdef HAVE_LIBDRM
#include <xf86drm.h>
#include <fcntl.h>
#endif

int LP_DEBUG = 0;

static const struct debug_named_value lp_debug_flags[] = {
   { "pipe", DEBUG_PIPE, NULL },
   { "tgsi", DEBUG_TGSI, NULL },
   { "tex", DEBUG_TEX, NULL },
   { "setup", DEBUG_SETUP, NULL },
   { "rast", DEBUG_RAST, NULL },
   { "query", DEBUG_QUERY, NULL },
   { "screen", DEBUG_SCREEN, NULL },
   { "counters", DEBUG_COUNTERS, NULL },
   { "scene", DEBUG_SCENE, NULL },
   { "fence", DEBUG_FENCE, NULL },
   { "no_fastpath", DEBUG_NO_FASTPATH, NULL },
   { "linear", DEBUG_LINEAR, NULL },
   { "linear2", DEBUG_LINEAR2, NULL },
   { "mem", DEBUG_MEM, NULL },
   { "fs", DEBUG_FS, NULL },
   { "cs", DEBUG_CS, NULL },
   { "accurate_a0", DEBUG_ACCURATE_A0 },
   { "mesh", DEBUG_MESH },
   DEBUG_NAMED_VALUE_END
};

int LP_PERF = 0;
static const struct debug_named_value lp_perf_flags[] = {
   { "texmem",         PERF_TEX_MEM, NULL },
   { "no_mipmap",      PERF_NO_MIPMAPS, NULL },
   { "no_linear",      PERF_NO_LINEAR, NULL },
   { "no_mip_linear",  PERF_NO_MIP_LINEAR, NULL },
   { "no_tex",         PERF_NO_TEX, NULL },
   { "no_blend",       PERF_NO_BLEND, NULL },
   { "no_depth",       PERF_NO_DEPTH, NULL },
   { "no_alphatest",   PERF_NO_ALPHATEST, NULL },
   { "no_rast_linear", PERF_NO_RAST_LINEAR, NULL },
   { "no_shade",       PERF_NO_SHADE, NULL },
   DEBUG_NAMED_VALUE_END
};


static const char *
llvmpipe_get_vendor(struct pipe_screen *screen)
{
   return "Mesa";
}


static const char *
llvmpipe_get_name(struct pipe_screen *screen)
{
   struct llvmpipe_screen *lscreen = llvmpipe_screen(screen);
   return lscreen->renderer_string;
}


static void
llvmpipe_init_shader_caps(struct pipe_screen *screen)
{
   for (unsigned i = 0; i < ARRAY_SIZE(screen->shader_caps); i++) {
      struct pipe_shader_caps *caps = (struct pipe_shader_caps *)&screen->shader_caps[i];

      switch (i) {
      case PIPE_SHADER_FRAGMENT:
      case PIPE_SHADER_COMPUTE:
      case PIPE_SHADER_MESH:
      case PIPE_SHADER_TASK:
         gallivm_init_shader_caps(caps);
         break;
      case PIPE_SHADER_TESS_CTRL:
      case PIPE_SHADER_TESS_EVAL:
         /* Tessellation shader needs llvm coroutines support */
         if (!GALLIVM_COROUTINES)
            continue;
         FALLTHROUGH;
      case PIPE_SHADER_VERTEX:
      case PIPE_SHADER_GEOMETRY:
         draw_init_shader_caps(caps);

         if (debug_get_bool_option("DRAW_USE_LLVM", true)) {
            caps->max_const_buffers = LP_MAX_TGSI_CONST_BUFFERS;
         } else {
            /* At this time, the draw module and llvmpipe driver only
             * support vertex shader texture lookups when LLVM is enabled in
             * the draw module.
             */
            caps->max_texture_samplers = 0;
            caps->max_sampler_views = 0;
         }
         break;
      default:
         break;
      }
   }
}


static void
llvmpipe_init_compute_caps(struct pipe_screen *screen)
{
   struct pipe_compute_caps *caps = (struct pipe_compute_caps *)&screen->compute_caps;

   caps->max_grid_size[0] =
   caps->max_grid_size[1] =
   caps->max_grid_size[2] = 65535;

   caps->max_block_size[0] =
   caps->max_block_size[1] =
   caps->max_block_size[2] = 1024;

   caps->max_threads_per_block = 1024;

   caps->max_local_size = 32768;
   caps->grid_dimension = 3;
   caps->max_global_size = 1 << 31;
   caps->max_mem_alloc_size = 1 << 31;
   caps->max_private_size = 1 << 31;
   caps->max_input_size = 1576;
   caps->images_supported = !!LP_MAX_TGSI_SHADER_IMAGES;
   caps->subgroup_sizes = lp_native_vector_width / 32;
   caps->max_subgroups = 1024 / (lp_native_vector_width / 32);
   caps->max_compute_units = 8;
   caps->max_clock_frequency = 300;
   caps->address_bits = sizeof(void*) * 8;
}


static void
llvmpipe_init_screen_caps(struct pipe_screen *screen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&screen->caps;

   u_init_pipe_screen_caps(screen, 0);

#ifdef HAVE_LIBDRM
   struct llvmpipe_screen *lscreen = llvmpipe_screen(screen);
#endif

#ifdef HAVE_LIBDRM
   if (lscreen->winsys->get_fd)
      caps->dmabuf = DRM_PRIME_CAP_IMPORT | DRM_PRIME_CAP_EXPORT;
#ifdef HAVE_LINUX_UDMABUF_H
   else if (lscreen->udmabuf_fd != -1)
      caps->dmabuf = DRM_PRIME_CAP_IMPORT | DRM_PRIME_CAP_EXPORT;
   else
      caps->dmabuf = DRM_PRIME_CAP_IMPORT;
#endif
#else
   caps->dmabuf = 0;
#endif

#if defined(HAVE_LIBDRM) && defined(HAVE_LINUX_UDMABUF_H)
   caps->native_fence_fd = lscreen->dummy_sync_fd != -1;
#endif
   caps->npot_textures = true;
   caps->mixed_framebuffer_sizes = true;
   caps->mixed_color_depth_bits = true;
   caps->anisotropic_filter = true;
   caps->fragment_shader_texture_lod = true;
   caps->fragment_shader_derivatives = true;
   caps->multiview = 2;
   caps->max_dual_source_render_targets = 1;
   caps->max_stream_output_buffers = PIPE_MAX_SO_BUFFERS;
   caps->max_render_targets = PIPE_MAX_COLOR_BUFS;
   caps->occlusion_query = true;
   caps->query_timestamp = true;
   caps->timer_resolution = true;
   caps->query_time_elapsed = true;
   caps->query_pipeline_statistics = true;
   caps->texture_mirror_clamp = true;
   caps->texture_mirror_clamp_to_edge = true;
   caps->texture_swizzle = true;
   caps->texture_shadow_lod = true;
   caps->max_texture_2d_size = 1 << (LP_MAX_TEXTURE_2D_LEVELS - 1);
   caps->max_texture_3d_levels = LP_MAX_TEXTURE_3D_LEVELS;
   caps->max_texture_cube_levels = LP_MAX_TEXTURE_CUBE_LEVELS;
   caps->max_texture_array_layers = LP_MAX_TEXTURE_ARRAY_LAYERS;
   caps->blend_equation_separate = true;
   caps->indep_blend_enable = true;
   caps->indep_blend_func = true;
   caps->fs_coord_origin_upper_left = true;
   caps->fs_coord_pixel_center_integer = true;
   caps->fs_coord_pixel_center_half_integer = true;
   caps->primitive_restart = true;
   caps->primitive_restart_fixed_index = true;
   caps->depth_clip_disable = true;
   caps->depth_clamp_enable = true;
   caps->shader_stencil_export = true;
   caps->vs_instanceid = true;
   caps->vertex_element_instance_divisor = true;
   caps->start_instance = true;
   caps->seamless_cube_map = true;
   caps->seamless_cube_map_per_texture = true;
   /* this is a lie could support arbitrary large offsets */
   caps->min_texture_gather_offset =
   caps->min_texel_offset = -32;
   caps->max_texture_gather_offset =
   caps->max_texel_offset = 31;
   caps->conditional_render = true;
   caps->texture_barrier = true;
   caps->max_stream_output_separate_components =
   caps->max_stream_output_interleaved_components = 16*4;
   caps->max_geometry_output_vertices =
   caps->max_geometry_total_output_components = 1024;
   caps->max_vertex_streams = 4;
   caps->max_vertex_attrib_stride = 2048;
   caps->stream_output_pause_resume = true;
   caps->stream_output_interleave_buffers = true;
   caps->vertex_color_unclamped = true;
   caps->vertex_color_clamped = true;
   caps->glsl_feature_level_compatibility =
   caps->glsl_feature_level = 450;
   caps->compute = GALLIVM_COROUTINES;
   caps->user_vertex_buffers = true;
   caps->tgsi_texcoord = true;
   caps->draw_indirect = true;

   caps->cube_map_array = true;
   caps->constant_buffer_offset_alignment = 16;
   caps->min_map_buffer_alignment = 64;
   caps->texture_buffer_objects = true;
   caps->linear_image_pitch_alignment = 1;
   caps->linear_image_base_address_alignment = 1;
   /* Adressing that many 64bpp texels fits in an i32 so this is a reasonable value */
   caps->max_texel_buffer_elements = LP_MAX_TEXEL_BUFFER_ELEMENTS;
   caps->texture_buffer_offset_alignment = 16;
   caps->texture_transfer_modes = 0;
   caps->max_viewports = PIPE_MAX_VIEWPORTS;
   caps->endianness = PIPE_ENDIAN_NATIVE;
   caps->tes_layer_viewport = true;
   caps->vs_layer_viewport = true;
   caps->max_texture_gather_components = 4;
   caps->vs_window_space_position = true;
   caps->fs_fine_derivative = true;
   caps->tgsi_tex_txf_lz = true;
   caps->sampler_view_target = true;
   caps->fake_sw_msaa = false;
   caps->texture_query_lod = true;
   caps->conditional_render_inverted = true;
   caps->shader_array_components = true;
   caps->doubles = true;
   caps->int64 = true;
   caps->query_so_overflow = true;
   caps->tgsi_div = true;
   caps->vendor_id = 0xFFFFFFFF;
   caps->device_id = 0xFFFFFFFF;

   /* XXX: Do we want to return the full amount fo system memory ? */
   uint64_t system_memory;
   if (os_get_total_physical_memory(&system_memory)) {
      if (sizeof(void *) == 4)
         /* Cap to 2 GB on 32 bits system. We do this because llvmpipe does
          * eat application memory, which is quite limited on 32 bits. App
          * shouldn't expect too much available memory. */
         system_memory = MIN2(system_memory, 2048 << 20);

      caps->video_memory = system_memory >> 20;
   } else {
      caps->video_memory = 0;
   }

   caps->uma = true;
   caps->query_memory_info = true;
   caps->clip_halfz = true;
   caps->polygon_offset_clamp = true;
   caps->texture_float_linear = true;
   caps->texture_half_float_linear = true;
   caps->cull_distance = true;
   caps->copy_between_compressed_and_plain_formats = true;
   caps->max_varyings = 32;
   caps->shader_buffer_offset_alignment = 16;
   caps->query_buffer_object = true;
   caps->draw_parameters = true;
   caps->fbfetch = 8;
   caps->fbfetch_coherent = true;
   caps->fbfetch_zs = true;
   caps->multi_draw_indirect = true;
   caps->multi_draw_indirect_params = true;
   caps->device_reset_status_query = true;
   caps->robust_buffer_access_behavior = true;
   caps->max_shader_patch_varyings = 32;
   caps->rasterizer_subpixel_bits = 8;
   caps->pci_group =
   caps->pci_bus =
   caps->pci_device =
   caps->pci_function = 0;
   caps->allow_mapped_buffers_during_execution = false;

   /* Can't expose shareable shaders because the draw shaders reference the
    * draw module's state, which is per-context.
    */
   caps->shareable_shaders = false;
   caps->max_gs_invocations = 32;
   caps->max_shader_buffer_size = LP_MAX_TGSI_SHADER_BUFFER_SIZE;
   caps->framebuffer_no_attachment = true;
   caps->tgsi_tg4_component_in_swizzle = true;
   caps->fs_face_is_integer_sysval = true;
   caps->resource_from_user_memory = true;
   caps->image_store_formatted = true;
   caps->image_load_formatted = true;
#ifdef PIPE_MEMORY_FD
   caps->memobj = true;
#endif
   caps->sampler_reduction_minmax = true;
   caps->texture_query_samples = true;
   caps->shader_group_vote = true;
   caps->shader_ballot = true;
   caps->image_atomic_float_add = true;
   caps->load_constbuf = true;
   caps->texture_multisample = true;
   caps->sample_shading = true;
   caps->gl_spirv = true;
   caps->post_depth_coverage = true;
   caps->shader_clock = true;
   caps->packed_uniforms = true;
   caps->system_svm = true;
   caps->atomic_float_minmax = LLVM_VERSION_MAJOR >= 15;
   caps->nir_images_as_deref = false;
   caps->alpha_to_coverage_dither_control = true;

   caps->min_line_width =
   caps->min_line_width_aa =
   caps->min_point_size =
   caps->min_point_size_aa = 1.0;
   caps->point_size_granularity =
   caps->line_width_granularity = 0.1;
   caps->max_line_width =
   caps->max_line_width_aa = 255.0; /* arbitrary */
   caps->max_point_size =
   caps->max_point_size_aa = LP_MAX_POINT_WIDTH; /* arbitrary */
   caps->max_texture_anisotropy = 16.0; /* not actually signficant at this time */
   caps->max_texture_lod_bias = 16.0; /* arbitrary */
}


static void
llvmpipe_get_driver_uuid(struct pipe_screen *pscreen, char *uuid)
{
   memset(uuid, 0, PIPE_UUID_SIZE);
   snprintf(uuid, PIPE_UUID_SIZE, "llvmpipeUUID");
}


static void
llvmpipe_get_device_uuid(struct pipe_screen *pscreen, char *uuid)
{
   memset(uuid, 0, PIPE_UUID_SIZE);
#if defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif /* __clang__ */
   snprintf(uuid, PIPE_UUID_SIZE, "mesa" PACKAGE_VERSION);
#if defined(__clang__)
#pragma GCC diagnostic pop
#endif /* __clang__ */
}


static const struct nir_shader_compiler_options gallivm_nir_options = {
   .lower_scmp = true,
   .lower_flrp32 = true,
   .lower_flrp64 = true,
   .lower_fsat = true,
   .lower_bitfield_insert = true,
   .lower_bitfield_extract = true,
   .lower_fdot = true,
   .lower_fdph = true,
   .lower_ffma16 = true,
   .lower_ffma32 = true,
   .lower_ffma64 = true,
   .lower_flrp16 = true,
   .lower_fmod = true,
   .lower_hadd = true,
   .lower_uadd_sat = true,
   .lower_usub_sat = true,
   .lower_iadd_sat = true,
   .lower_ldexp = true,
   .lower_pack_snorm_2x16 = true,
   .lower_pack_snorm_4x8 = true,
   .lower_pack_unorm_2x16 = true,
   .lower_pack_unorm_4x8 = true,
   .lower_pack_half_2x16 = true,
   .lower_pack_split = true,
   .lower_unpack_snorm_2x16 = true,
   .lower_unpack_snorm_4x8 = true,
   .lower_unpack_unorm_2x16 = true,
   .lower_unpack_unorm_4x8 = true,
   .lower_unpack_half_2x16 = true,
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .lower_insert_byte = true,
   .lower_insert_word = true,
   .lower_uadd_carry = true,
   .lower_usub_borrow = true,
   .lower_mul_2x32_64 = true,
   .lower_ifind_msb = true,
   .lower_int64_options = nir_lower_imul_2x32_64,
   .lower_doubles_options = nir_lower_dround_even,
   .max_unroll_iterations = 32,
   .lower_to_scalar = true,
   .lower_uniforms_to_ubo = true,
   .lower_vector_cmp = true,
   .lower_device_index_to_zero = true,
   .support_16bit_alu = true,
   .lower_fisnormal = true,
   .lower_fquantize2f16 = true,
   .lower_fminmax_signed_zero = true,
   .driver_functions = true,
   .scalarize_ddx = true,
   .support_indirect_inputs = (uint8_t)BITFIELD_MASK(PIPE_SHADER_TYPES),
   .support_indirect_outputs = (uint8_t)BITFIELD_MASK(PIPE_SHADER_TYPES),
};


static char *
llvmpipe_finalize_nir(struct pipe_screen *screen,
                      struct nir_shader *nir)
{
   lp_build_opt_nir(nir);
   return NULL;
}


static inline const void *
llvmpipe_get_compiler_options(struct pipe_screen *screen,
                              enum pipe_shader_ir ir,
                              enum pipe_shader_type shader)
{
   assert(ir == PIPE_SHADER_IR_NIR);
   return &gallivm_nir_options;
}


bool
lp_storage_render_image_format_supported(enum pipe_format format)
{
   const struct util_format_description *format_desc = util_format_description(format);

   if (format_desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB) {
      /* this is a lie actually other formats COULD exist where we would fail */
      if (format_desc->nr_channels < 3)
         return false;
   } else if (format_desc->colorspace != UTIL_FORMAT_COLORSPACE_RGB) {
      return false;
   }

   if (format_desc->layout != UTIL_FORMAT_LAYOUT_PLAIN &&
       format != PIPE_FORMAT_R11G11B10_FLOAT)
      return false;

   assert(format_desc->block.width == 1);
   assert(format_desc->block.height == 1);

   if (format_desc->is_mixed)
      return false;

   if (!format_desc->is_array && !format_desc->is_bitmask &&
       format != PIPE_FORMAT_R11G11B10_FLOAT)
      return false;

   return true;
}


bool
lp_storage_image_format_supported(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R32G32B32A32_FLOAT:
   case PIPE_FORMAT_R16G16B16A16_FLOAT:
   case PIPE_FORMAT_R32G32_FLOAT:
   case PIPE_FORMAT_R16G16_FLOAT:
   case PIPE_FORMAT_R11G11B10_FLOAT:
   case PIPE_FORMAT_R32_FLOAT:
   case PIPE_FORMAT_R16_FLOAT:
   case PIPE_FORMAT_R32G32B32A32_UINT:
   case PIPE_FORMAT_R16G16B16A16_UINT:
   case PIPE_FORMAT_R10G10B10A2_UINT:
   case PIPE_FORMAT_R8G8B8A8_UINT:
   case PIPE_FORMAT_R32G32_UINT:
   case PIPE_FORMAT_R16G16_UINT:
   case PIPE_FORMAT_R8G8_UINT:
   case PIPE_FORMAT_R32_UINT:
   case PIPE_FORMAT_R16_UINT:
   case PIPE_FORMAT_R8_UINT:
   case PIPE_FORMAT_R32G32B32A32_SINT:
   case PIPE_FORMAT_R16G16B16A16_SINT:
   case PIPE_FORMAT_R8G8B8A8_SINT:
   case PIPE_FORMAT_R32G32_SINT:
   case PIPE_FORMAT_R16G16_SINT:
   case PIPE_FORMAT_R8G8_SINT:
   case PIPE_FORMAT_R32_SINT:
   case PIPE_FORMAT_R16_SINT:
   case PIPE_FORMAT_R8_SINT:
   case PIPE_FORMAT_R16G16B16A16_UNORM:
   case PIPE_FORMAT_R10G10B10A2_UNORM:
   case PIPE_FORMAT_R8G8B8A8_UNORM:
   case PIPE_FORMAT_R16G16_UNORM:
   case PIPE_FORMAT_R8G8_UNORM:
   case PIPE_FORMAT_R16_UNORM:
   case PIPE_FORMAT_R8_UNORM:
   case PIPE_FORMAT_R16G16B16A16_SNORM:
   case PIPE_FORMAT_R8G8B8A8_SNORM:
   case PIPE_FORMAT_R16G16_SNORM:
   case PIPE_FORMAT_R8G8_SNORM:
   case PIPE_FORMAT_R16_SNORM:
   case PIPE_FORMAT_R8_SNORM:
   case PIPE_FORMAT_B8G8R8A8_UNORM:
   case PIPE_FORMAT_A8_UNORM:
      return true;
   default:
      return false;
   }
}


/**
 * Query format support for creating a texture, drawing surface, etc.
 * \param format  the format to test
 * \param type  one of PIPE_TEXTURE, PIPE_SURFACE
 */
static bool
llvmpipe_is_format_supported(struct pipe_screen *_screen,
                             enum pipe_format format,
                             enum pipe_texture_target target,
                             unsigned sample_count,
                             unsigned storage_sample_count,
                             unsigned bind)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);
   struct sw_winsys *winsys = screen->winsys;
   const struct util_format_description *format_desc =
      util_format_description(format);

   assert(target == PIPE_BUFFER ||
          target == PIPE_TEXTURE_1D ||
          target == PIPE_TEXTURE_1D_ARRAY ||
          target == PIPE_TEXTURE_2D ||
          target == PIPE_TEXTURE_2D_ARRAY ||
          target == PIPE_TEXTURE_RECT ||
          target == PIPE_TEXTURE_3D ||
          target == PIPE_TEXTURE_CUBE ||
          target == PIPE_TEXTURE_CUBE_ARRAY);

   if (sample_count != 0 && sample_count != 1 && sample_count != 4)
      return false;

   if (bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_SHADER_IMAGE))
      if (!lp_storage_render_image_format_supported(format))
         return false;

   if (bind & PIPE_BIND_SHADER_IMAGE) {
      if (!lp_storage_image_format_supported(format))
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

      /* Disable 64-bit integer formats for RT/samplers.
       * VK CTS crashes with these and they don't make much sense.
       */
      int c = util_format_get_first_non_void_channel(format_desc->format);
      if (c >= 0) {
         if (format_desc->channel[c].pure_integer &&
             format_desc->channel[c].size == 64)
            return false;
      }

   }

   if (!(bind & PIPE_BIND_VERTEX_BUFFER) &&
       util_format_is_scaled(format))
      return false;

   if (bind & PIPE_BIND_DISPLAY_TARGET) {
      if (!winsys->is_displaytarget_format_supported(winsys, bind, format))
         return false;
   }

   if (bind & PIPE_BIND_DEPTH_STENCIL) {
      if (format_desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
         return false;

      if (format_desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
         return false;
   }

   if (format_desc->layout == UTIL_FORMAT_LAYOUT_ASTC ||
       format_desc->layout == UTIL_FORMAT_LAYOUT_ATC) {
      /* Software decoding is not hooked up. */
      return false;
   }

   if (format_desc->layout == UTIL_FORMAT_LAYOUT_ETC &&
       format != PIPE_FORMAT_ETC1_RGB8)
      return false;

   /* planar not supported natively */
   if ((format_desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED ||
        format_desc->layout == UTIL_FORMAT_LAYOUT_PLANAR2 ||
        format_desc->layout == UTIL_FORMAT_LAYOUT_PLANAR3) &&
       target == PIPE_BUFFER)
      return false;

   if (format_desc->colorspace == UTIL_FORMAT_COLORSPACE_YUV) {
      if (format == PIPE_FORMAT_UYVY ||
          format == PIPE_FORMAT_YUYV ||
          format == PIPE_FORMAT_NV12)
         return true;
      return false;
   }

   /*
    * Everything can be supported by u_format
    * (those without fetch_rgba_float might be not but shouldn't hit that)
    */

   return true;
}


static void
llvmpipe_flush_frontbuffer(struct pipe_screen *_screen,
                           struct pipe_context *_pipe,
                           struct pipe_resource *resource,
                           unsigned level, unsigned layer,
                           void *context_private,
                           unsigned nboxes,
                           struct pipe_box *sub_box)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);
   struct sw_winsys *winsys = screen->winsys;
   struct llvmpipe_resource *texture = llvmpipe_resource(resource);

   assert(texture->dt);

   if (texture->dt) {
      if (_pipe)
         llvmpipe_flush_resource(_pipe, resource, 0, true, true,
                                 false, "frontbuffer");
      winsys->displaytarget_display(winsys, texture->dt,
                                    context_private, nboxes, sub_box);
   }
}


static void
llvmpipe_destroy_screen(struct pipe_screen *_screen)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);

   if (screen->cs_tpool)
      lp_cs_tpool_destroy(screen->cs_tpool);

   if (screen->rast)
      lp_rast_destroy(screen->rast);

   lp_jit_screen_cleanup(screen);

   disk_cache_destroy(screen->disk_shader_cache);

   glsl_type_singleton_decref();

#if defined(HAVE_LIBDRM) && defined(HAVE_LINUX_UDMABUF_H)
   if (screen->udmabuf_fd != -1)
      close(screen->udmabuf_fd);
#endif

#if DETECT_OS_LINUX
   util_vma_heap_finish(&screen->mem_heap);

   close(screen->fd_mem_alloc);
   mtx_destroy(&screen->mem_mutex);
#endif
   mtx_destroy(&screen->rast_mutex);
   mtx_destroy(&screen->cs_mutex);
   FREE(screen);
}


/**
 * Fence reference counting.
 */
static void
llvmpipe_fence_reference(struct pipe_screen *screen,
                         struct pipe_fence_handle **ptr,
                         struct pipe_fence_handle *fence)
{
   struct lp_fence **old = (struct lp_fence **) ptr;
   struct lp_fence *f = (struct lp_fence *) fence;

   lp_fence_reference(old, f);
}


/**
 * Wait for the fence to finish.
 */
static bool
llvmpipe_fence_finish(struct pipe_screen *screen,
                      struct pipe_context *ctx,
                      struct pipe_fence_handle *fence_handle,
                      uint64_t timeout)
{
   struct lp_fence *f = (struct lp_fence *) fence_handle;

   if (!timeout)
      return lp_fence_signalled(f);

   if (!lp_fence_signalled(f)) {
      if (timeout != OS_TIMEOUT_INFINITE)
         return lp_fence_timedwait(f, timeout);

      lp_fence_wait(f);
   }
   return true;
}


static void
update_cache_sha1_cpu(struct mesa_sha1 *ctx)
{
   const struct util_cpu_caps_t *cpu_caps = util_get_cpu_caps();
   /*
    * Don't need the cpu cache affinity stuff. The rest
    * is contained in first 5 dwords.
    */
   STATIC_ASSERT(offsetof(struct util_cpu_caps_t, num_L3_caches)
                 == 5 * sizeof(uint32_t));
   _mesa_sha1_update(ctx, cpu_caps, 5 * sizeof(uint32_t));
}


static void
lp_disk_cache_create(struct llvmpipe_screen *screen)
{
   struct mesa_sha1 ctx;
   unsigned gallivm_perf = gallivm_get_perf_flags();
   unsigned char sha1[20];
   char cache_id[20 * 2 + 1];
   _mesa_sha1_init(&ctx);

   if (!disk_cache_get_function_identifier(lp_disk_cache_create, &ctx) ||
       !disk_cache_get_function_identifier(LLVMLinkInMCJIT, &ctx))
      return;

   _mesa_sha1_update(&ctx, &gallivm_perf, sizeof(gallivm_perf));
   update_cache_sha1_cpu(&ctx);
   _mesa_sha1_final(&ctx, sha1);
   mesa_bytes_to_hex(cache_id, sha1, 20);

   screen->disk_shader_cache = disk_cache_create("llvmpipe", cache_id, 0);
}


static struct disk_cache *
lp_get_disk_shader_cache(struct pipe_screen *_screen)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);

   return screen->disk_shader_cache;
}

static int
llvmpipe_screen_get_fd(struct pipe_screen *_screen)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(_screen);
   struct sw_winsys *winsys = screen->winsys;

   if (winsys->get_fd)
      return winsys->get_fd(winsys);
   else
      return -1;
}


void
lp_disk_cache_find_shader(struct llvmpipe_screen *screen,
                          struct lp_cached_code *cache,
                          unsigned char ir_sha1_cache_key[20])
{
   unsigned char sha1[CACHE_KEY_SIZE];

   if (!screen->disk_shader_cache)
      return;
   disk_cache_compute_key(screen->disk_shader_cache, ir_sha1_cache_key,
                          20, sha1);

   size_t binary_size;
   uint8_t *buffer = disk_cache_get(screen->disk_shader_cache,
                                    sha1, &binary_size);
   if (!buffer) {
      cache->data_size = 0;
      return;
   }
   cache->data_size = binary_size;
   cache->data = buffer;
}


void
lp_disk_cache_insert_shader(struct llvmpipe_screen *screen,
                            struct lp_cached_code *cache,
                            unsigned char ir_sha1_cache_key[20])
{
   unsigned char sha1[CACHE_KEY_SIZE];

   if (!screen->disk_shader_cache || !cache->data_size || cache->dont_cache)
      return;
   disk_cache_compute_key(screen->disk_shader_cache, ir_sha1_cache_key,
                          20, sha1);
   disk_cache_put(screen->disk_shader_cache, sha1, cache->data,
                  cache->data_size, NULL);
}


bool
llvmpipe_screen_late_init(struct llvmpipe_screen *screen)
{
   bool ret = true;
   mtx_lock(&screen->late_mutex);

   if (screen->late_init_done)
      goto out;

   screen->rast = lp_rast_create(screen->num_threads);
   if (!screen->rast) {
      ret = false;
      goto out;
   }

   screen->cs_tpool = lp_cs_tpool_create(screen->num_threads);
   if (!screen->cs_tpool) {
      lp_rast_destroy(screen->rast);
      ret = false;
      goto out;
   }

   if (!lp_jit_screen_init(screen)) {
      ret = false;
      goto out;
   }

   lp_build_init(); /* get lp_native_vector_width initialised */

   lp_disk_cache_create(screen);
   screen->late_init_done = true;
out:
   mtx_unlock(&screen->late_mutex);
   return ret;
}


/**
 * Create a new pipe_screen object
 */
struct pipe_screen *
llvmpipe_create_screen(struct sw_winsys *winsys)
{
   struct llvmpipe_screen *screen;

   glsl_type_singleton_init_or_ref();

   LP_DEBUG = debug_get_flags_option("LP_DEBUG", lp_debug_flags, 0 );

   LP_PERF = debug_get_flags_option("LP_PERF", lp_perf_flags, 0 );

   screen = CALLOC_STRUCT(llvmpipe_screen);
   if (!screen)
      return NULL;

   screen->winsys = winsys;

   screen->base.destroy = llvmpipe_destroy_screen;

   screen->base.get_name = llvmpipe_get_name;
   screen->base.get_vendor = llvmpipe_get_vendor;
   screen->base.get_device_vendor = llvmpipe_get_vendor; // TODO should be the CPU vendor
   screen->base.get_screen_fd = llvmpipe_screen_get_fd;
   screen->base.get_compiler_options = llvmpipe_get_compiler_options;
   screen->base.is_format_supported = llvmpipe_is_format_supported;

   screen->base.context_create = llvmpipe_create_context;
   screen->base.flush_frontbuffer = llvmpipe_flush_frontbuffer;
   screen->base.fence_reference = llvmpipe_fence_reference;
   screen->base.fence_finish = llvmpipe_fence_finish;

   screen->base.get_timestamp = u_default_get_timestamp;

   screen->base.query_memory_info = util_sw_query_memory_info;

   screen->base.get_driver_uuid = llvmpipe_get_driver_uuid;
   screen->base.get_device_uuid = llvmpipe_get_device_uuid;

   screen->base.finalize_nir = llvmpipe_finalize_nir;

   screen->base.get_disk_shader_cache = lp_get_disk_shader_cache;
   llvmpipe_init_screen_resource_funcs(&screen->base);

   screen->allow_cl = !!getenv("LP_CL");
   screen->num_threads = util_get_cpu_caps()->nr_cpus > 1
      ? util_get_cpu_caps()->nr_cpus : 0;
   screen->num_threads = debug_get_num_option("LP_NUM_THREADS",
                                              screen->num_threads);
   screen->num_threads = MIN2(screen->num_threads, LP_MAX_THREADS);

#if defined(HAVE_LIBDRM) && defined(HAVE_LINUX_UDMABUF_H)
   screen->udmabuf_fd = open("/dev/udmabuf", O_RDWR);
   llvmpipe_init_screen_fence_funcs(&screen->base);
#endif

   uint64_t alignment;
   if (!os_get_page_size(&alignment))
      alignment = 256;

#if DETECT_OS_LINUX
   (void) mtx_init(&screen->mem_mutex, mtx_plain);

   util_vma_heap_init(&screen->mem_heap, alignment, UINT64_MAX - alignment);
   screen->mem_heap.alloc_high = false;
   screen->fd_mem_alloc = os_create_anonymous_file(0, "allocation fd");
#endif

   snprintf(screen->renderer_string, sizeof(screen->renderer_string),
            "llvmpipe (LLVM " MESA_LLVM_VERSION_STRING ", %u bits)",
            lp_build_init_native_width() );

   list_inithead(&screen->ctx_list);
   (void) mtx_init(&screen->ctx_mutex, mtx_plain);
   (void) mtx_init(&screen->cs_mutex, mtx_plain);
   (void) mtx_init(&screen->rast_mutex, mtx_plain);

   (void) mtx_init(&screen->late_mutex, mtx_plain);

   llvmpipe_init_shader_caps(&screen->base);
   llvmpipe_init_compute_caps(&screen->base);
   llvmpipe_init_screen_caps(&screen->base);

   return &screen->base;
}
