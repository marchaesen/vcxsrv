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


#include "compiler/nir/nir.h"
#include "util/u_helpers.h"
#include "util/u_memory.h"
#include "util/format/u_format.h"
#include "util/format/u_format_s3tc.h"
#include "util/u_screen.h"
#include "util/u_video.h"
#include "util/os_misc.h"
#include "util/os_time.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "draw/draw_context.h"

#include "frontend/sw_winsys.h"
#include "tgsi/tgsi_exec.h"

#include "sp_texture.h"
#include "sp_screen.h"
#include "sp_context.h"
#include "sp_fence.h"
#include "sp_public.h"

static const struct debug_named_value sp_debug_options[] = {
   {"vs",        SP_DBG_VS,         "dump vertex shader assembly to stderr"},
   {"gs",        SP_DBG_GS,         "dump geometry shader assembly to stderr"},
   {"fs",        SP_DBG_FS,         "dump fragment shader assembly to stderr"},
   {"cs",        SP_DBG_CS,         "dump compute shader assembly to stderr"},
   {"no_rast",   SP_DBG_NO_RAST,    "no-ops rasterization, for profiling purposes"},
   {"use_llvm",  SP_DBG_USE_LLVM,   "Use LLVM if available for shaders"},
   DEBUG_NAMED_VALUE_END
};

int sp_debug;
DEBUG_GET_ONCE_FLAGS_OPTION(sp_debug, "SOFTPIPE_DEBUG", sp_debug_options, 0)

static const char *
softpipe_get_vendor(struct pipe_screen *screen)
{
   return "Mesa";
}


static const char *
softpipe_get_name(struct pipe_screen *screen)
{
   return "softpipe";
}

static const nir_shader_compiler_options sp_compiler_options = {
   .fdot_replicates = true,
   .fuse_ffma32 = true,
   .fuse_ffma64 = true,
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .lower_insert_byte = true,
   .lower_insert_word = true,
   .lower_fdph = true,
   .lower_flrp64 = true,
   .lower_fmod = true,
   .lower_uniforms_to_ubo = true,
   .lower_vector_cmp = true,
   .lower_int64_options = nir_lower_imul_2x32_64,
   .max_unroll_iterations = 32,

   /* TGSI doesn't have a semantic for local or global index, just local and
    * workgroup id.
    */
   .lower_cs_local_index_to_id = true,
   .support_indirect_inputs = (uint8_t)BITFIELD_MASK(PIPE_SHADER_TYPES),
   .support_indirect_outputs = (uint8_t)BITFIELD_MASK(PIPE_SHADER_TYPES),
};

static const void *
softpipe_get_compiler_options(struct pipe_screen *pscreen,
                              enum pipe_shader_ir ir,
                              enum pipe_shader_type shader)
{
   assert(ir == PIPE_SHADER_IR_NIR);
   return &sp_compiler_options;
}

/**
 * Query format support for creating a texture, drawing surface, etc.
 * \param format  the format to test
 * \param type  one of PIPE_TEXTURE, PIPE_SURFACE
 */
static bool
softpipe_is_format_supported( struct pipe_screen *screen,
                              enum pipe_format format,
                              enum pipe_texture_target target,
                              unsigned sample_count,
                              unsigned storage_sample_count,
                              unsigned bind)
{
   struct sw_winsys *winsys = softpipe_screen(screen)->winsys;
   const struct util_format_description *format_desc;

   assert(target == PIPE_BUFFER ||
          target == PIPE_TEXTURE_1D ||
          target == PIPE_TEXTURE_1D_ARRAY ||
          target == PIPE_TEXTURE_2D ||
          target == PIPE_TEXTURE_2D_ARRAY ||
          target == PIPE_TEXTURE_RECT ||
          target == PIPE_TEXTURE_3D ||
          target == PIPE_TEXTURE_CUBE ||
          target == PIPE_TEXTURE_CUBE_ARRAY);

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   format_desc = util_format_description(format);

   if (sample_count > 1)
      return false;

   if (bind & (PIPE_BIND_DISPLAY_TARGET |
               PIPE_BIND_SCANOUT |
               PIPE_BIND_SHARED)) {
      if(!winsys->is_displaytarget_format_supported(winsys, bind, format))
         return false;
   }

   if (bind & PIPE_BIND_RENDER_TARGET) {
      if (format_desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS)
         return false;

      /*
       * Although possible, it is unnatural to render into compressed or YUV
       * surfaces. So disable these here to avoid going into weird paths
       * inside gallium frontends.
       */
      if (format_desc->block.width != 1 ||
          format_desc->block.height != 1)
         return false;
   }

   if (bind & PIPE_BIND_DEPTH_STENCIL) {
      if (format_desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
         return false;
   }

   if (format_desc->layout == UTIL_FORMAT_LAYOUT_ASTC ||
       format_desc->layout == UTIL_FORMAT_LAYOUT_ATC) {
      /* Software decoding is not hooked up. */
      return false;
   }

   if ((bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW)) &&
       ((bind & PIPE_BIND_DISPLAY_TARGET) == 0) &&
       target != PIPE_BUFFER) {
      const struct util_format_description *desc =
         util_format_description(format);
      if (desc->nr_channels == 3 && desc->is_array) {
         /* Don't support any 3-component formats for rendering/texturing
          * since we don't support the corresponding 8-bit 3 channel UNORM
          * formats.  This allows us to support GL_ARB_copy_image between
          * GL_RGB8 and GL_RGB8UI, for example.  Otherwise, we may be asked to
          * do a resource copy between PIPE_FORMAT_R8G8B8_UINT and
          * PIPE_FORMAT_R8G8B8X8_UNORM, for example, which will not work
          * (different bpp).
          */
         return false;
      }
   }

   if (format_desc->layout == UTIL_FORMAT_LAYOUT_ETC &&
       format != PIPE_FORMAT_ETC1_RGB8)
      return false;

   /*
    * All other operations (sampling, transfer, etc).
    */

   /*
    * Everything else should be supported by u_format.
    */
   return true;
}


static void
softpipe_init_shader_caps(struct softpipe_screen *sp_screen)
{
   for (unsigned i = 0; i <= PIPE_SHADER_COMPUTE; i++) {
      struct pipe_shader_caps *caps =
         (struct pipe_shader_caps *)&sp_screen->base.shader_caps[i];

      switch(i) {
      case PIPE_SHADER_VERTEX:
      case PIPE_SHADER_GEOMETRY:
         if (sp_screen->use_llvm) {
            draw_init_shader_caps(caps);
            break;
         }
         FALLTHROUGH;
      case PIPE_SHADER_FRAGMENT:
      case PIPE_SHADER_COMPUTE:
         tgsi_exec_init_shader_caps(caps);
         break;
      default:
         continue;
      }

      caps->supported_irs = (1 << PIPE_SHADER_IR_NIR) | (1 << PIPE_SHADER_IR_TGSI);
   }
}


static void
softpipe_init_compute_caps(struct softpipe_screen *sp_screen)
{
   struct pipe_compute_caps *caps =
      (struct pipe_compute_caps *)&sp_screen->base.compute_caps;

   caps->max_grid_size[0] =
   caps->max_grid_size[1] =
   caps->max_grid_size[2] = 65535;

   caps->max_block_size[0] =
   caps->max_block_size[1] =
   caps->max_block_size[2] = 1024;

   caps->max_threads_per_block = 1024;
   caps->max_local_size = 32768;
}


static void
softpipe_init_screen_caps(struct softpipe_screen *sp_screen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&sp_screen->base.caps;

   u_init_pipe_screen_caps(&sp_screen->base, 0);

   caps->npot_textures = true;
   caps->mixed_framebuffer_sizes = true;
   caps->mixed_color_depth_bits = true;
   caps->fragment_shader_texture_lod = true;
   caps->fragment_shader_derivatives = true;
   caps->anisotropic_filter = true;
   caps->max_render_targets = PIPE_MAX_COLOR_BUFS;
   caps->max_dual_source_render_targets = 1;
   caps->occlusion_query = true;
   caps->query_time_elapsed = true;
   caps->query_pipeline_statistics = true;
   caps->texture_mirror_clamp = true;
   caps->texture_mirror_clamp_to_edge = true;
   caps->texture_swizzle = true;
   caps->max_texture_2d_size = 1 << (SP_MAX_TEXTURE_2D_LEVELS - 1);
   caps->max_texture_3d_levels = SP_MAX_TEXTURE_3D_LEVELS;
   caps->max_texture_cube_levels = SP_MAX_TEXTURE_CUBE_LEVELS;
   caps->blend_equation_separate = true;
   caps->indep_blend_enable = true;
   caps->indep_blend_func = true;
   caps->fs_coord_origin_upper_left = true;
   caps->fs_coord_origin_lower_left = true;
   caps->fs_coord_pixel_center_half_integer = true;
   caps->fs_coord_pixel_center_integer = true;
   caps->depth_clip_disable = true;
   caps->depth_bounds_test = true;
   caps->max_stream_output_buffers = PIPE_MAX_SO_BUFFERS;
   caps->max_stream_output_separate_components =
   caps->max_stream_output_interleaved_components = 16*4;
   caps->max_geometry_output_vertices =
   caps->max_geometry_total_output_components = 1024;
   caps->max_vertex_streams = sp_screen->use_llvm ? 1 : PIPE_MAX_VERTEX_STREAMS;
   caps->max_vertex_attrib_stride = 2048;
   caps->primitive_restart = true;
   caps->primitive_restart_fixed_index = true;
   caps->shader_stencil_export = true;
   caps->image_atomic_float_add = true;
   caps->vs_instanceid = true;
   caps->vertex_element_instance_divisor = true;
   caps->start_instance = true;
   caps->seamless_cube_map = true;
   caps->seamless_cube_map_per_texture = true;
   caps->max_texture_array_layers = 256; /* for GL3 */
   caps->min_texel_offset = -8;
   caps->max_texel_offset = 7;
   caps->conditional_render = true;
   caps->fragment_color_clamped = true;
   caps->vertex_color_unclamped = true; /* draw module */
   caps->vertex_color_clamped = true; /* draw module */
   caps->glsl_feature_level =
   caps->glsl_feature_level_compatibility = 400;
   caps->compute = true;
   caps->user_vertex_buffers = true;
   caps->stream_output_pause_resume = true;
   caps->stream_output_interleave_buffers = true;
   caps->vs_layer_viewport = true;
   caps->doubles = true;
   caps->int64 = true;
   caps->tgsi_div = true;
   caps->constant_buffer_offset_alignment = 16;
   caps->min_map_buffer_alignment = 64;
   caps->query_timestamp = true;
   caps->timer_resolution = true;
   caps->cube_map_array = true;
   caps->texture_buffer_objects = true;
   caps->max_texel_buffer_elements = 65536;
   caps->texture_buffer_offset_alignment = 16;
   caps->texture_transfer_modes = 0;
   caps->max_viewports = PIPE_MAX_VIEWPORTS;
   caps->endianness = PIPE_ENDIAN_NATIVE;
   caps->max_texture_gather_components = 4;
   caps->texture_gather_sm5 = true;
   caps->texture_query_lod = true;
   caps->vs_window_space_position = true;
   caps->fs_fine_derivative = true;
   caps->sampler_view_target = true;
   caps->fake_sw_msaa = true;
   caps->min_texture_gather_offset = -32;
   caps->max_texture_gather_offset = 31;
   caps->draw_indirect = true;
   caps->query_so_overflow = true;
   caps->nir_images_as_deref = false;

   /* Can't expose shareable shaders because the draw shaders reference the
    * draw module's state, which is per-context.
    */
   caps->shareable_shaders = false;

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

   caps->uma = false;
   caps->query_memory_info = true;
   caps->conditional_render_inverted = true;
   caps->clip_halfz = true;
   caps->texture_float_linear = true;
   caps->texture_half_float_linear = true;
   caps->framebuffer_no_attachment = true;
   caps->cull_distance = true;
   caps->copy_between_compressed_and_plain_formats = true;
   caps->shader_array_components = true;
   caps->tgsi_texcoord = true;
   caps->max_varyings = TGSI_EXEC_MAX_INPUT_ATTRIBS;
   caps->pci_group =
   caps->pci_bus =
   caps->pci_device =
   caps->pci_function = 0;
   caps->max_gs_invocations = 32;
   caps->max_shader_buffer_size = 1 << 27;
   caps->shader_buffer_offset_alignment = 4;
   caps->image_store_formatted = true;

   caps->min_line_width =
   caps->min_line_width_aa =
   caps->min_point_size =
   caps->min_point_size_aa = 1;
   caps->point_size_granularity =
   caps->line_width_granularity = 0.1;
   caps->max_line_width =
   caps->max_line_width_aa = 255.0; /* arbitrary */
   caps->max_point_size =
   caps->max_point_size_aa = 255.0; /* arbitrary */
   caps->max_texture_anisotropy = 16.0;
   caps->max_texture_lod_bias = 16.0; /* arbitrary */
}


static void
softpipe_destroy_screen( struct pipe_screen *screen )
{
   FREE(screen);
}


/* This is often overriden by the co-state tracker.
 */
static void
softpipe_flush_frontbuffer(struct pipe_screen *_screen,
                           struct pipe_context *pipe,
                           struct pipe_resource *resource,
                           unsigned level, unsigned layer,
                           void *context_private,
                           unsigned nboxes,
                           struct pipe_box *sub_box)
{
   struct softpipe_screen *screen = softpipe_screen(_screen);
   struct sw_winsys *winsys = screen->winsys;
   struct softpipe_resource *texture = softpipe_resource(resource);

   assert(texture->dt);
   if (texture->dt)
      winsys->displaytarget_display(winsys, texture->dt, context_private, nboxes, sub_box);
}

static int
softpipe_screen_get_fd(struct pipe_screen *screen)
{
   struct sw_winsys *winsys = softpipe_screen(screen)->winsys;

   if (winsys->get_fd)
      return winsys->get_fd(winsys);
   else
      return -1;
}

/**
 * Create a new pipe_screen object
 * Note: we're not presently subclassing pipe_screen (no softpipe_screen).
 */
struct pipe_screen *
softpipe_create_screen(struct sw_winsys *winsys)
{
   struct softpipe_screen *screen = CALLOC_STRUCT(softpipe_screen);

   if (!screen)
      return NULL;

   sp_debug = debug_get_option_sp_debug();

   screen->winsys = winsys;

   screen->base.destroy = softpipe_destroy_screen;

   screen->base.get_name = softpipe_get_name;
   screen->base.get_vendor = softpipe_get_vendor;
   screen->base.get_device_vendor = softpipe_get_vendor; // TODO should be the CPU vendor
   screen->base.get_screen_fd = softpipe_screen_get_fd;
   screen->base.get_timestamp = u_default_get_timestamp;
   screen->base.query_memory_info = util_sw_query_memory_info;
   screen->base.is_format_supported = softpipe_is_format_supported;
   screen->base.context_create = softpipe_create_context;
   screen->base.flush_frontbuffer = softpipe_flush_frontbuffer;
   screen->base.get_compiler_options = softpipe_get_compiler_options;
   screen->use_llvm = sp_debug & SP_DBG_USE_LLVM;

   softpipe_init_screen_texture_funcs(&screen->base);
   softpipe_init_screen_fence_funcs(&screen->base);

   softpipe_init_shader_caps(screen);
   softpipe_init_compute_caps(screen);
   softpipe_init_screen_caps(screen);

   return &screen->base;
}
