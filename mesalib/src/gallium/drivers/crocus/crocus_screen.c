/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @file crocus_screen.c
 *
 * Screen related driver hooks and capability lists.
 *
 * A program may use multiple rendering contexts (crocus_context), but
 * they all share a common screen (crocus_screen).  Global driver state
 * can be stored in the screen; it may be accessed by multiple threads.
 */

#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "util/u_debug.h"
#include "util/u_inlines.h"
#include "util/format/u_format.h"
#include "util/u_transfer_helper.h"
#include "util/u_upload_mgr.h"
#include "util/ralloc.h"
#include "util/xmlconfig.h"
#include "drm-uapi/i915_drm.h"
#include "crocus_context.h"
#include "crocus_defines.h"
#include "crocus_fence.h"
#include "crocus_perf.h"
#include "crocus_pipe.h"
#include "crocus_resource.h"
#include "crocus_screen.h"
#include "intel/compiler/elk/elk_compiler.h"
#include "intel/common/intel_debug_identifier.h"
#include "intel/common/intel_gem.h"
#include "intel/common/intel_l3_config.h"
#include "intel/common/intel_uuid.h"
#include "crocus_monitor.h"

#define genX_call(devinfo, func, ...)                   \
   switch ((devinfo)->verx10) {                         \
   case 80:                                             \
      gfx8_##func(__VA_ARGS__);                         \
      break;                                            \
   case 75:                                             \
      gfx75_##func(__VA_ARGS__);                        \
      break;                                            \
   case 70:                                             \
      gfx7_##func(__VA_ARGS__);                         \
      break;                                            \
   case 60:                                             \
      gfx6_##func(__VA_ARGS__);                         \
      break;                                            \
   case 50:                                             \
      gfx5_##func(__VA_ARGS__);                         \
      break;                                            \
   case 45:                                             \
      gfx45_##func(__VA_ARGS__);                        \
      break;                                            \
   case 40:                                             \
      gfx4_##func(__VA_ARGS__);                         \
      break;                                            \
   default:                                             \
      unreachable("Unknown hardware generation");       \
   }

static const char *
crocus_get_vendor(struct pipe_screen *pscreen)
{
   return "Intel";
}

static const char *
crocus_get_device_vendor(struct pipe_screen *pscreen)
{
   return "Intel";
}

static void
crocus_get_device_uuid(struct pipe_screen *pscreen, char *uuid)
{
   struct crocus_screen *screen = (struct crocus_screen *)pscreen;

   intel_uuid_compute_device_id((uint8_t *)uuid, &screen->devinfo, PIPE_UUID_SIZE);
}

static void
crocus_get_driver_uuid(struct pipe_screen *pscreen, char *uuid)
{
   struct crocus_screen *screen = (struct crocus_screen *)pscreen;
   const struct intel_device_info *devinfo = &screen->devinfo;

   intel_uuid_compute_driver_id((uint8_t *)uuid, devinfo, PIPE_UUID_SIZE);
}

static const char *
crocus_get_name(struct pipe_screen *pscreen)
{
   struct crocus_screen *screen = (struct crocus_screen *)pscreen;
   const struct intel_device_info *devinfo = &screen->devinfo;
   static char buf[128];

   snprintf(buf, sizeof(buf), "Mesa %s", devinfo->name);
   return buf;
}

static uint64_t
get_aperture_size(int fd)
{
   struct drm_i915_gem_get_aperture aperture = {};
   intel_ioctl(fd, DRM_IOCTL_I915_GEM_GET_APERTURE, &aperture);
   return aperture.aper_size;
}

static void
crocus_init_shader_caps(struct crocus_screen *screen)
{
   const struct intel_device_info *devinfo = &screen->devinfo;

   for (unsigned i = 0; i <= PIPE_SHADER_COMPUTE; i++) {
      struct pipe_shader_caps *caps =
         (struct pipe_shader_caps *)&screen->base.shader_caps[i];

      if (devinfo->ver < 6 &&
          i != PIPE_SHADER_VERTEX &&
          i != PIPE_SHADER_FRAGMENT)
         continue;

      if (devinfo->ver == 6 &&
          i != PIPE_SHADER_VERTEX &&
          i != PIPE_SHADER_FRAGMENT &&
          i != PIPE_SHADER_GEOMETRY)
         continue;

      caps->max_instructions = i == MESA_SHADER_FRAGMENT ? 1024 : 16384;
      caps->max_alu_instructions =
      caps->max_tex_instructions =
      caps->max_tex_indirections = i == MESA_SHADER_FRAGMENT ? 1024 : 0;

      caps->max_control_flow_depth = UINT_MAX;

      /* Gen7 vec4 geom backend */
      caps->max_inputs = i == MESA_SHADER_VERTEX || i == MESA_SHADER_GEOMETRY ? 16 : 32;
      caps->max_outputs = 32;
      caps->max_const_buffer0_size = 16 * 1024 * sizeof(float);
      caps->max_const_buffers = devinfo->ver >= 6 ? 16 : 1;
      caps->max_temps = 256; /* GL_MAX_PROGRAM_TEMPORARIES_ARB */

      /* Lie about these to avoid st/mesa's GLSL IR lowering of indirects,
       * which we don't want.  Our compiler backend will check elk_compiler's
       * options and call nir_lower_indirect_derefs appropriately anyway.
       */
      caps->indirect_temp_addr = true;
      caps->indirect_const_addr = true;

      caps->integers = true;
      caps->max_texture_samplers =
      caps->max_sampler_views =
         (devinfo->verx10 >= 75) ? CROCUS_MAX_TEXTURE_SAMPLERS : 16;

      if (devinfo->ver >= 7 &&
          (i == PIPE_SHADER_FRAGMENT || i == PIPE_SHADER_COMPUTE))
         caps->max_shader_images = CROCUS_MAX_TEXTURE_SAMPLERS;

      caps->max_shader_buffers =
         devinfo->ver >= 7 ? (CROCUS_MAX_ABOS + CROCUS_MAX_SSBOS) : 0;

      caps->supported_irs = 1 << PIPE_SHADER_IR_NIR;
   }
}

static void
crocus_init_compute_caps(struct crocus_screen *screen)
{
   struct pipe_compute_caps *caps =
      (struct pipe_compute_caps *)&screen->base.compute_caps;
   const struct intel_device_info *devinfo = &screen->devinfo;

   if (devinfo->ver < 7)
      return;

   const uint32_t max_invocations = 32 * devinfo->max_cs_workgroup_threads;

   caps->address_bits = 32;

   snprintf(caps->ir_target, sizeof(caps->ir_target), "gen");

   caps->grid_dimension = 3;

   caps->max_grid_size[0] =
   caps->max_grid_size[1] =
   caps->max_grid_size[2] = 65535;

   /* MaxComputeWorkGroupSize[0..2] */
   caps->max_block_size[0] =
   caps->max_block_size[1] =
   caps->max_block_size[2] = max_invocations;

   /* MaxComputeWorkGroupInvocations */
   caps->max_threads_per_block = max_invocations;

   /* MaxComputeSharedMemorySize */
   caps->max_local_size = 64 * 1024;

   caps->images_supported = true;

   caps->subgroup_sizes = ELK_SUBGROUP_SIZE;

   caps->max_variable_threads_per_block = max_invocations;
}

static void
crocus_init_screen_caps(struct crocus_screen *screen)
{
   const struct intel_device_info *devinfo = &screen->devinfo;
   struct pipe_caps *caps = (struct pipe_caps *)&screen->base.caps;

   u_init_pipe_screen_caps(&screen->base, 1);

   caps->npot_textures = true;
   caps->anisotropic_filter = true;
   caps->occlusion_query = true;
   caps->texture_swizzle = true;
   caps->texture_mirror_clamp_to_edge = true;
   caps->blend_equation_separate = true;
   caps->fragment_shader_texture_lod = true;
   caps->fragment_shader_derivatives = true;
   caps->primitive_restart = true;
   caps->primitive_restart_fixed_index = true;
   caps->indep_blend_enable = true;
   caps->fs_coord_origin_upper_left = true;
   caps->fs_coord_pixel_center_integer = true;
   caps->depth_clip_disable = true;
   caps->vs_instanceid = true;
   caps->vertex_element_instance_divisor = true;
   caps->seamless_cube_map = true;
   caps->seamless_cube_map_per_texture = true;
   caps->conditional_render = true;
   caps->texture_barrier = true;
   caps->vertex_color_unclamped = true;
   caps->start_instance = true;
   caps->force_persample_interp = true;
   caps->mixed_framebuffer_sizes = true;
   caps->vs_layer_viewport = true;
   caps->tes_layer_viewport = true;
   caps->uma = true;
   caps->clip_halfz = true;
   caps->tgsi_texcoord = true;
   caps->device_reset_status_query = true;
   caps->copy_between_compressed_and_plain_formats = true;
   caps->signed_vertex_buffer_offset = true;
   caps->texture_float_linear = true;
   caps->texture_half_float_linear = true;
   caps->polygon_offset_clamp = true;
   caps->tgsi_tex_txf_lz = true;
   caps->multisample_z_resolve = true;
   caps->shader_group_vote = true;
   caps->vs_window_space_position = true;
   caps->texture_gather_sm5 = true;
   caps->shader_array_components = true;
   caps->glsl_tess_levels_as_inputs = true;
   caps->fs_position_is_sysval = true;
   caps->fs_face_is_integer_sysval = true;
   caps->invalidate_buffer = true;
   caps->surface_reinterpret_blocks = true;
   caps->fence_signal = true;
   caps->demote_to_helper_invocation = true;
   caps->gl_clamp = true;
   caps->legacy_math_rules = true;
   caps->native_fence_fd = true;

   caps->int64 =
   caps->shader_ballot =
   caps->packed_uniforms = devinfo->ver == 8;

   caps->quads_follow_provoking_vertex_convention = devinfo->ver <= 5;

   caps->texture_query_lod =
   caps->query_time_elapsed = devinfo->ver >= 5;

   caps->draw_indirect =
   caps->multi_draw_indirect =
   caps->multi_draw_indirect_params =
   caps->framebuffer_no_attachment =
   caps->fs_fine_derivative =
   caps->stream_output_interleave_buffers =
   caps->shader_clock =
   caps->texture_query_samples =
   caps->compute =
   caps->sampler_view_target =
   caps->shader_samples_identical =
   caps->shader_pack_half_float =
   caps->gl_spirv =
   caps->gl_spirv_variable_pointers =
   caps->compute_shader_derivatives =
   caps->doubles =
   caps->memobj =
   caps->image_store_formatted =
   caps->alpha_to_coverage_dither_control = devinfo->ver >= 7;

   caps->query_buffer_object =
   caps->robust_buffer_access_behavior = devinfo->verx10 >= 75;

   caps->cull_distance =
   caps->query_pipeline_statistics_single =
   caps->stream_output_pause_resume =
   caps->sample_shading =
   caps->cube_map_array =
   caps->query_so_overflow =
   caps->texture_multisample =
   caps->conditional_render_inverted =
   caps->query_timestamp =
   caps->texture_buffer_objects =
   caps->indep_blend_func =
   caps->texture_shadow_lod =
   caps->load_constbuf =
   caps->draw_parameters =
   caps->clear_scissored = devinfo->ver >= 6;

   caps->fbfetch = devinfo->verx10 >= 45 ? ELK_MAX_DRAW_BUFFERS : 0;
   /* in theory CL (965gm) can do this */
   caps->max_dual_source_render_targets = devinfo->verx10 >= 45 ? 1 : 0;
   caps->max_render_targets = ELK_MAX_DRAW_BUFFERS;
   caps->max_texture_2d_size = devinfo->ver >= 7 ? 16384 : 8192;
   caps->max_texture_cube_levels = devinfo->ver >= 7 ?
      CROCUS_MAX_MIPLEVELS /* 16384x16384 */ : CROCUS_MAX_MIPLEVELS - 1; /* 8192x8192 */
   caps->max_texture_3d_levels = 12; /* 2048x2048 */
   caps->max_stream_output_buffers = (devinfo->ver >= 6) ? 4 : 0;
   caps->max_texture_array_layers = devinfo->ver >= 7 ? 2048 : 512;
   caps->max_stream_output_separate_components =
      ELK_MAX_SOL_BINDINGS / CROCUS_MAX_SOL_BUFFERS;
   caps->max_stream_output_interleaved_components = ELK_MAX_SOL_BINDINGS;
   caps->glsl_feature_level_compatibility =
   caps->glsl_feature_level = devinfo->verx10 >= 75 ? 460 :
      (devinfo->ver >= 7 ? 420 : (devinfo->ver >= 6 ? 330 : 140));
   caps->clip_planes = devinfo->verx10 < 45 ? 6 : 1; // defaults to MAX (8)
   /* 3DSTATE_CONSTANT_XS requires the start of UBOs to be 32B aligned */
   caps->constant_buffer_offset_alignment = 32;
   caps->min_map_buffer_alignment = CROCUS_MAP_BUFFER_ALIGNMENT;
   caps->shader_buffer_offset_alignment = devinfo->ver >= 7 ? 4 : 0;
   caps->max_shader_buffer_size = devinfo->ver >= 7 ? (1 << 27) : 0;
   caps->texture_buffer_offset_alignment = 16; // XXX: u_screen says 256 is the minimum value...
   caps->texture_transfer_modes = PIPE_TEXTURE_TRANSFER_BLIT;
   caps->max_texel_buffer_elements = CROCUS_MAX_TEXTURE_BUFFER_SIZE;
   caps->max_viewports = devinfo->ver >= 6 ? 16 : 1;
   caps->max_geometry_output_vertices = devinfo->ver >= 6 ? 256 : 0;
   caps->max_geometry_total_output_components = devinfo->ver >= 6 ? 1024 : 0;
   caps->max_gs_invocations = devinfo->ver >= 7 ? 32 : 1;
   caps->max_texture_gather_components = devinfo->ver >= 7 ? 4 :
      (devinfo->ver == 6 ? 1 : 0);
   caps->min_texture_gather_offset = devinfo->ver >= 7 ? -32 :
      (devinfo->ver == 6 ? -8 : 0);
   caps->max_texture_gather_offset = devinfo->ver >= 7 ? 31 :
      (devinfo->ver == 6 ? 7 : 0);
   caps->max_vertex_streams = devinfo->ver >= 7 ? 4 : 1;
   caps->vendor_id = 0x8086;
   caps->device_id = screen->pci_id;

   /* Once a batch uses more than 75% of the maximum mappable size, we
    * assume that there's some fragmentation, and we start doing extra
    * flushing, etc.  That's the big cliff apps will care about.
    */
   const unsigned gpu_mappable_megabytes =
      (screen->aperture_threshold) / (1024 * 1024);

   const long system_memory_pages = sysconf(_SC_PHYS_PAGES);
   const long system_page_size = sysconf(_SC_PAGE_SIZE);

   if (system_memory_pages <= 0 || system_page_size <= 0) {
      caps->video_memory = -1;
   } else {
      const uint64_t system_memory_bytes =
         (uint64_t) system_memory_pages * (uint64_t) system_page_size;

      const unsigned system_memory_megabytes =
         (unsigned) (system_memory_bytes / (1024 * 1024));

      caps->video_memory = MIN2(system_memory_megabytes, gpu_mappable_megabytes);
   }

   caps->max_shader_patch_varyings =
   caps->max_varyings = (screen->devinfo.ver >= 6) ? 32 : 16;
   /* AMD_pinned_memory assumes the flexibility of using client memory
    * for any buffer (incl. vertex buffers) which rules out the prospect
    * of using snooped buffers, as using snooped buffers without
    * cogniscience is likely to be detrimental to performance and require
    * extensive checking in the driver for correctness, e.g. to prevent
    * illegal snoop <-> snoop transfers.
    */
   caps->resource_from_user_memory = devinfo->has_llc;
   caps->throttle = !screen->driconf.disable_throttling;

   caps->context_priority_mask =
      PIPE_CONTEXT_PRIORITY_LOW |
      PIPE_CONTEXT_PRIORITY_MEDIUM |
      PIPE_CONTEXT_PRIORITY_HIGH;

   caps->frontend_noop = true;
   // XXX: don't hardcode 00:00:02.0 PCI here
   caps->pci_group = 0;
   caps->pci_bus = 0;
   caps->pci_device = 2;
   caps->pci_function = 0;

   caps->hardware_gl_select = false;

   caps->timer_resolution = DIV_ROUND_UP(1000000000ull, devinfo->timestamp_frequency);

   caps->min_line_width =
   caps->min_line_width_aa =
   caps->min_point_size =
   caps->min_point_size_aa = 1;

   caps->point_size_granularity =
   caps->line_width_granularity = 0.1;

   caps->max_line_width =
   caps->max_line_width_aa = devinfo->ver >= 6 ? 7.375f : 7.0f;

   caps->max_point_size =
   caps->max_point_size_aa = 255.0f;

   caps->max_texture_anisotropy = 16.0f;
   caps->max_texture_lod_bias = 15.0f;
}

static uint64_t
crocus_get_timestamp(struct pipe_screen *pscreen)
{
   struct crocus_screen *screen = (struct crocus_screen *) pscreen;
   uint64_t result;

   if (!intel_gem_read_render_timestamp(crocus_bufmgr_get_fd(screen->bufmgr),
                                        screen->devinfo.kmd_type, &result))
      return 0;

   result = intel_device_info_timebase_scale(&screen->devinfo, result);
   result &= (1ull << TIMESTAMP_BITS) - 1;

   return result;
}

void
crocus_screen_destroy(struct crocus_screen *screen)
{
   intel_perf_free(screen->perf_cfg);
   u_transfer_helper_destroy(screen->base.transfer_helper);
   crocus_bufmgr_unref(screen->bufmgr);
   disk_cache_destroy(screen->disk_cache);
   close(screen->winsys_fd);
   ralloc_free(screen);
}

static void
crocus_screen_unref(struct pipe_screen *pscreen)
{
   crocus_pscreen_unref(pscreen);
}

static void
crocus_query_memory_info(struct pipe_screen *pscreen,
                         struct pipe_memory_info *info)
{
}

static const void *
crocus_get_compiler_options(struct pipe_screen *pscreen,
                            enum pipe_shader_ir ir,
                            enum pipe_shader_type pstage)
{
   struct crocus_screen *screen = (struct crocus_screen *) pscreen;
   gl_shader_stage stage = stage_from_pipe(pstage);
   assert(ir == PIPE_SHADER_IR_NIR);

   return screen->compiler->nir_options[stage];
}

static struct disk_cache *
crocus_get_disk_shader_cache(struct pipe_screen *pscreen)
{
   struct crocus_screen *screen = (struct crocus_screen *) pscreen;
   return screen->disk_cache;
}

static const struct intel_l3_config *
crocus_get_default_l3_config(const struct intel_device_info *devinfo,
                             bool compute)
{
   bool wants_dc_cache = true;
   bool has_slm = compute;
   const struct intel_l3_weights w =
      intel_get_default_l3_weights(devinfo, wants_dc_cache, has_slm);
   return intel_get_l3_config(devinfo, w);
}

static void
crocus_shader_debug_log(void *data, unsigned *id, const char *fmt, ...)
{
   struct util_debug_callback *dbg = data;
   va_list args;

   if (!dbg->debug_message)
      return;

   va_start(args, fmt);
   dbg->debug_message(dbg->data, id, UTIL_DEBUG_TYPE_SHADER_INFO, fmt, args);
   va_end(args);
}

static void
crocus_shader_perf_log(void *data, unsigned *id, const char *fmt, ...)
{
   struct util_debug_callback *dbg = data;
   va_list args;
   va_start(args, fmt);

   if (INTEL_DEBUG(DEBUG_PERF)) {
      va_list args_copy;
      va_copy(args_copy, args);
      vfprintf(stderr, fmt, args_copy);
      va_end(args_copy);
   }

   if (dbg->debug_message) {
      dbg->debug_message(dbg->data, id, UTIL_DEBUG_TYPE_PERF_INFO, fmt, args);
   }

   va_end(args);
}

static int
crocus_screen_get_fd(struct pipe_screen *pscreen)
{
   struct crocus_screen *screen = (struct crocus_screen *)pscreen;

   return screen->winsys_fd;
}

struct pipe_screen *
crocus_screen_create(int fd, const struct pipe_screen_config *config)
{
   struct crocus_screen *screen = rzalloc(NULL, struct crocus_screen);
   if (!screen)
      return NULL;

   if (!intel_get_device_info_from_fd(fd, &screen->devinfo, 4, 8))
      return NULL;
   screen->pci_id = screen->devinfo.pci_device_id;

   if (screen->devinfo.ver > 8)
      return NULL;

   if (screen->devinfo.ver == 8) {
      /* bind to cherryview or bdw if forced */
      if (screen->devinfo.platform != INTEL_PLATFORM_CHV &&
          !getenv("CROCUS_GEN8"))
         return NULL;
   }

   p_atomic_set(&screen->refcount, 1);

   screen->aperture_bytes = get_aperture_size(fd);
   screen->aperture_threshold = screen->aperture_bytes * 3 / 4;

   driParseConfigFiles(config->options, config->options_info, 0, "crocus",
                       NULL, NULL, NULL, 0, NULL, 0);

   bool bo_reuse = false;
   int bo_reuse_mode = driQueryOptioni(config->options, "bo_reuse");
   switch (bo_reuse_mode) {
   case DRI_CONF_BO_REUSE_DISABLED:
      break;
   case DRI_CONF_BO_REUSE_ALL:
      bo_reuse = true;
      break;
   }

   screen->bufmgr = crocus_bufmgr_get_for_fd(&screen->devinfo, fd, bo_reuse);
   if (!screen->bufmgr)
      return NULL;
   screen->fd = crocus_bufmgr_get_fd(screen->bufmgr);
   screen->winsys_fd = fd;

   process_intel_debug_variable();

   screen->driconf.dual_color_blend_by_location =
      driQueryOptionb(config->options, "dual_color_blend_by_location");
   screen->driconf.disable_throttling =
      driQueryOptionb(config->options, "disable_throttling");
   screen->driconf.always_flush_cache =
      driQueryOptionb(config->options, "always_flush_cache");
   screen->driconf.limit_trig_input_range =
      driQueryOptionb(config->options, "limit_trig_input_range");
   screen->driconf.lower_depth_range_rate =
      driQueryOptionf(config->options, "lower_depth_range_rate");

   screen->precompile = debug_get_bool_option("shader_precompile", true);

   isl_device_init(&screen->isl_dev, &screen->devinfo);

   screen->compiler = elk_compiler_create(screen, &screen->devinfo);
   screen->compiler->shader_debug_log = crocus_shader_debug_log;
   screen->compiler->shader_perf_log = crocus_shader_perf_log;
   screen->compiler->supports_shader_constants = false;
   screen->compiler->constant_buffer_0_is_relative = true;

   if (screen->devinfo.ver >= 7) {
      screen->l3_config_3d = crocus_get_default_l3_config(&screen->devinfo, false);
      screen->l3_config_cs = crocus_get_default_l3_config(&screen->devinfo, true);
   }

   crocus_disk_cache_init(screen);

   slab_create_parent(&screen->transfer_pool,
                      sizeof(struct crocus_transfer), 64);

   struct pipe_screen *pscreen = &screen->base;

   crocus_init_screen_fence_functions(pscreen);
   crocus_init_screen_resource_functions(pscreen);

   pscreen->destroy = crocus_screen_unref;
   pscreen->get_name = crocus_get_name;
   pscreen->get_vendor = crocus_get_vendor;
   pscreen->get_device_vendor = crocus_get_device_vendor;
   pscreen->get_screen_fd = crocus_screen_get_fd;
   pscreen->get_compiler_options = crocus_get_compiler_options;
   pscreen->get_device_uuid = crocus_get_device_uuid;
   pscreen->get_driver_uuid = crocus_get_driver_uuid;
   pscreen->get_disk_shader_cache = crocus_get_disk_shader_cache;
   pscreen->is_format_supported = crocus_is_format_supported;
   pscreen->context_create = crocus_create_context;
   pscreen->get_timestamp = crocus_get_timestamp;
   pscreen->query_memory_info = crocus_query_memory_info;
   pscreen->get_driver_query_group_info = crocus_get_monitor_group_info;
   pscreen->get_driver_query_info = crocus_get_monitor_info;

   crocus_init_shader_caps(screen);
   crocus_init_compute_caps(screen);
   crocus_init_screen_caps(screen);

   genX_call(&screen->devinfo, crocus_init_screen_state, screen);
   genX_call(&screen->devinfo, crocus_init_screen_query, screen);
   return pscreen;
}
