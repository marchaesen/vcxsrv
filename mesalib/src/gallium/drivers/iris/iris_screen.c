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
 * @file iris_screen.c
 *
 * Screen related driver hooks and capability lists.
 *
 * A program may use multiple rendering contexts (iris_context), but
 * they all share a common screen (iris_screen).  Global driver state
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
#include "util/os_file.h"
#include "util/u_cpu_detect.h"
#include "util/u_inlines.h"
#include "util/format/u_format.h"
#include "util/u_transfer_helper.h"
#include "util/u_upload_mgr.h"
#include "util/ralloc.h"
#include "util/xmlconfig.h"
#include "iris_context.h"
#include "iris_defines.h"
#include "iris_fence.h"
#include "iris_perf.h"
#include "iris_pipe.h"
#include "iris_resource.h"
#include "iris_screen.h"
#include "compiler/glsl_types.h"
#include "intel/common/intel_debug_identifier.h"
#include "intel/common/intel_gem.h"
#include "intel/common/intel_l3_config.h"
#include "intel/common/intel_uuid.h"
#include "iris_monitor.h"

#define genX_call(devinfo, func, ...)             \
   switch ((devinfo)->verx10) {                   \
   case 300:                                      \
      gfx30_##func(__VA_ARGS__);                  \
      break;                                      \
   case 200:                                      \
      gfx20_##func(__VA_ARGS__);                  \
      break;                                      \
   case 125:                                      \
      gfx125_##func(__VA_ARGS__);                 \
      break;                                      \
   case 120:                                      \
      gfx12_##func(__VA_ARGS__);                  \
      break;                                      \
   case 110:                                      \
      gfx11_##func(__VA_ARGS__);                  \
      break;                                      \
   case 90:                                       \
      gfx9_##func(__VA_ARGS__);                   \
      break;                                      \
   case 80:                                       \
      gfx8_##func(__VA_ARGS__);                   \
      break;                                      \
   default:                                       \
      unreachable("Unknown hardware generation"); \
   }

#ifndef INTEL_USE_ELK
static inline void gfx8_init_screen_state(struct iris_screen *screen) { unreachable("no elk support"); }
static inline void gfx8_init_screen_gen_state(struct iris_screen *screen) { unreachable("no elk support"); }
#endif

static const char *
iris_get_vendor(struct pipe_screen *pscreen)
{
   return "Intel";
}

static const char *
iris_get_device_vendor(struct pipe_screen *pscreen)
{
   return "Intel";
}

static void
iris_get_device_uuid(struct pipe_screen *pscreen, char *uuid)
{
   struct iris_screen *screen = (struct iris_screen *)pscreen;

   intel_uuid_compute_device_id((uint8_t *)uuid, screen->devinfo, PIPE_UUID_SIZE);
}

static void
iris_get_driver_uuid(struct pipe_screen *pscreen, char *uuid)
{
   struct iris_screen *screen = (struct iris_screen *)pscreen;
   const struct intel_device_info *devinfo = screen->devinfo;

   intel_uuid_compute_driver_id((uint8_t *)uuid, devinfo, PIPE_UUID_SIZE);
}

static void
iris_warn_cl()
{
   static bool warned = false;
   if (warned || INTEL_DEBUG(DEBUG_CL_QUIET))
      return;

   warned = true;
   fprintf(stderr, "WARNING: OpenCL support via iris driver is incomplete.\n"
                   "For a complete and conformant OpenCL implementation, use\n"
                   "https://github.com/intel/compute-runtime instead\n");
}

static const char *
iris_get_name(struct pipe_screen *pscreen)
{
   struct iris_screen *screen = (struct iris_screen *)pscreen;
   const struct intel_device_info *devinfo = screen->devinfo;
   static char buf[128];

   snprintf(buf, sizeof(buf), "Mesa %s", devinfo->name);
   return buf;
}

static const char *
iris_get_cl_cts_version(struct pipe_screen *pscreen)
{
   struct iris_screen *screen = (struct iris_screen *)pscreen;
   const struct intel_device_info *devinfo = screen->devinfo;

   /* https://www.khronos.org/conformance/adopters/conformant-products/opencl#submission_405 */
   if (devinfo->verx10 == 120)
      return "v2022-04-22-00";

   return NULL;
}

static int
iris_get_video_memory(struct iris_screen *screen)
{
   uint64_t vram = iris_bufmgr_vram_size(screen->bufmgr);
   uint64_t sram = iris_bufmgr_sram_size(screen->bufmgr);
   if (vram) {
      return vram / (1024 * 1024);
   } else if (sram) {
      return sram / (1024 * 1024);
   } else {
      /* This is the old code path, it get the GGTT size from the kernel
       * (which should always be 4Gb on Gfx8+).
       *
       * We should probably never end up here. This is just a fallback to get
       * some kind of value in case os_get_available_system_memory fails.
       */
      const struct intel_device_info *devinfo = screen->devinfo;
      /* Once a batch uses more than 75% of the maximum mappable size, we
       * assume that there's some fragmentation, and we start doing extra
       * flushing, etc.  That's the big cliff apps will care about.
       */
      const unsigned gpu_mappable_megabytes =
         (devinfo->aperture_bytes * 3 / 4) / (1024 * 1024);

      const long system_memory_pages = sysconf(_SC_PHYS_PAGES);
      const long system_page_size = sysconf(_SC_PAGE_SIZE);

      if (system_memory_pages <= 0 || system_page_size <= 0)
         return -1;

      const uint64_t system_memory_bytes =
         (uint64_t) system_memory_pages * (uint64_t) system_page_size;

      const unsigned system_memory_megabytes =
         (unsigned) (system_memory_bytes / (1024 * 1024));

      return MIN2(system_memory_megabytes, gpu_mappable_megabytes);
   }
}

static void
iris_init_shader_caps(struct iris_screen *screen)
{
   for (unsigned i = 0; i <= PIPE_SHADER_COMPUTE; i++) {
      struct pipe_shader_caps *caps =
         (struct pipe_shader_caps *)&screen->base.shader_caps[i];

      caps->max_instructions = i == PIPE_SHADER_FRAGMENT ? 1024 : 16384;
      caps->max_alu_instructions =
      caps->max_tex_instructions =
      caps->max_tex_indirections = i == PIPE_SHADER_FRAGMENT ? 1024 : 0;

      caps->max_control_flow_depth = UINT_MAX;

      caps->max_inputs = i == PIPE_SHADER_VERTEX ? 16 : 32;
      caps->max_outputs = 32;
      caps->max_const_buffer0_size = 16 * 1024 * sizeof(float);
      caps->max_const_buffers = 16;
      caps->max_temps = 256; /* GL_MAX_PROGRAM_TEMPORARIES_ARB */

      /* Lie about these to avoid st/mesa's GLSL IR lowering of indirects,
       * which we don't want.  Our compiler backend will check brw_compiler's
       * options and call nir_lower_indirect_derefs appropriately anyway.
       */
      caps->indirect_temp_addr = true;
      caps->indirect_const_addr = true;

      caps->integers = true;
      caps->max_texture_samplers = IRIS_MAX_SAMPLERS;
      caps->max_sampler_views = IRIS_MAX_TEXTURES;
      caps->max_shader_images = IRIS_MAX_IMAGES;
      caps->max_shader_buffers = IRIS_MAX_ABOS + IRIS_MAX_SSBOS;
      caps->supported_irs = 1 << PIPE_SHADER_IR_NIR;
   }
}

static void
iris_init_compute_caps(struct iris_screen *screen)
{
   struct pipe_compute_caps *caps =
      (struct pipe_compute_caps *)&screen->base.compute_caps;

   const struct intel_device_info *devinfo = screen->devinfo;

   const uint32_t max_invocations =
      MIN2(1024, 32 * devinfo->max_cs_workgroup_threads);

   /* This gets queried on OpenCL device init and is never queried by the
    * OpenGL state tracker.
    */
   caps->address_bits = 64;

   snprintf(caps->ir_target, sizeof(caps->ir_target), "gen");

   caps->grid_dimension = 3;

   caps->max_grid_size[0] =
   caps->max_grid_size[1] =
   caps->max_grid_size[2] = UINT32_MAX;

   /* MaxComputeWorkGroupSize[0..2] */
   caps->max_block_size[0] =
   caps->max_block_size[1] =
   caps->max_block_size[2] = max_invocations;

   /* MaxComputeWorkGroupInvocations */
   caps->max_threads_per_block =
   /* MaxComputeVariableGroupInvocations */
   caps->max_variable_threads_per_block = max_invocations;

   /* MaxComputeSharedMemorySize */
   caps->max_local_size = 64 * 1024;

   caps->images_supported = true;

   caps->subgroup_sizes = 32 | 16 | 8;

   caps->max_subgroups = devinfo->max_cs_workgroup_threads;

   caps->max_mem_alloc_size =
   caps->max_global_size = 1 << 30; /* TODO */

   caps->max_clock_frequency = 400; /* TODO */

   caps->max_compute_units = intel_device_info_subslice_total(devinfo);

   /* MaxComputeSharedMemorySize */
   caps->max_private_size = 64 * 1024;

   /* We could probably allow more; this is the OpenCL minimum */
   caps->max_input_size = 1024;
}

static void
iris_init_screen_caps(struct iris_screen *screen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&screen->base.caps;

   u_init_pipe_screen_caps(&screen->base, 1);

   const struct intel_device_info *devinfo = screen->devinfo;

   caps->npot_textures = true;
   caps->anisotropic_filter = true;
   caps->occlusion_query = true;
   caps->query_time_elapsed = true;
   caps->texture_swizzle = true;
   caps->texture_mirror_clamp_to_edge = true;
   caps->blend_equation_separate = true;
   caps->fragment_shader_texture_lod = true;
   caps->fragment_shader_derivatives = true;
   caps->primitive_restart = true;
   caps->primitive_restart_fixed_index = true;
   caps->indep_blend_enable = true;
   caps->indep_blend_func = true;
   caps->fs_coord_origin_upper_left = true;
   caps->fs_coord_pixel_center_integer = true;
   caps->depth_clip_disable = true;
   caps->vs_instanceid = true;
   caps->vertex_element_instance_divisor = true;
   caps->seamless_cube_map = true;
   caps->seamless_cube_map_per_texture = true;
   caps->conditional_render = true;
   caps->texture_barrier = true;
   caps->stream_output_pause_resume = true;
   caps->vertex_color_unclamped = true;
   caps->compute = true;
   caps->start_instance = true;
   caps->query_timestamp = true;
   caps->texture_multisample = true;
   caps->cube_map_array = true;
   caps->texture_buffer_objects = true;
   caps->query_pipeline_statistics_single = true;
   caps->texture_query_lod = true;
   caps->sample_shading = true;
   caps->force_persample_interp = true;
   caps->draw_indirect = true;
   caps->multi_draw_indirect = true;
   caps->multi_draw_indirect_params = true;
   caps->mixed_framebuffer_sizes = true;
   caps->vs_layer_viewport = true;
   caps->tes_layer_viewport = true;
   caps->fs_fine_derivative = true;
   caps->shader_pack_half_float = true;
   caps->conditional_render_inverted = true;
   caps->clip_halfz = true;
   caps->tgsi_texcoord = true;
   caps->stream_output_interleave_buffers = true;
   caps->doubles = true;
   caps->int64 = true;
   caps->sampler_view_target = true;
   caps->robust_buffer_access_behavior = true;
   caps->device_reset_status_query = true;
   caps->copy_between_compressed_and_plain_formats = true;
   caps->framebuffer_no_attachment = true;
   caps->cull_distance = true;
   caps->packed_uniforms = true;
   caps->signed_vertex_buffer_offset = true;
   caps->texture_float_linear = true;
   caps->texture_half_float_linear = true;
   caps->polygon_offset_clamp = true;
   caps->query_so_overflow = true;
   caps->query_buffer_object = true;
   caps->tgsi_tex_txf_lz = true;
   caps->texture_query_samples = true;
   caps->shader_clock = true;
   caps->shader_ballot = true;
   caps->multisample_z_resolve = true;
   caps->clear_scissored = true;
   caps->shader_group_vote = true;
   caps->vs_window_space_position = true;
   caps->texture_gather_sm5 = true;
   caps->shader_array_components = true;
   caps->glsl_tess_levels_as_inputs = true;
   caps->load_constbuf = true;
   caps->draw_parameters = true;
   caps->fs_position_is_sysval = true;
   caps->fs_face_is_integer_sysval = true;
   caps->compute_shader_derivatives = true;
   caps->invalidate_buffer = true;
   caps->surface_reinterpret_blocks = true;
   caps->texture_shadow_lod = true;
   caps->shader_samples_identical = true;
   caps->gl_spirv = true;
   caps->gl_spirv_variable_pointers = true;
   caps->demote_to_helper_invocation = true;
   caps->native_fence_fd = true;
   caps->memobj = true;
   caps->mixed_color_depth_bits = true;
   caps->fence_signal = true;
   caps->image_store_formatted = true;
   caps->legacy_math_rules = true;
   caps->alpha_to_coverage_dither_control = true;
   caps->map_unsynchronized_thread_safe = true;
   caps->has_const_bw = true;
   caps->cl_gl_sharing = true;
   caps->uma = iris_bufmgr_vram_size(screen->bufmgr) == 0;
   caps->query_memory_info = iris_bufmgr_vram_size(screen->bufmgr) != 0;
   caps->prefer_back_buffer_reuse = false;
   caps->fbfetch = IRIS_MAX_DRAW_BUFFERS;
   caps->fbfetch_coherent = devinfo->ver >= 9 && devinfo->ver < 20;
   caps->conservative_raster_inner_coverage =
   caps->post_depth_coverage =
   caps->shader_stencil_export =
   caps->depth_clip_disable_separate =
   caps->fragment_shader_interlock =
   caps->atomic_float_minmax = devinfo->ver >= 9;
   caps->depth_bounds_test = devinfo->ver >= 12;
   caps->max_dual_source_render_targets = 1;
   caps->max_render_targets = IRIS_MAX_DRAW_BUFFERS;
   caps->max_texture_2d_size = 16384;
   caps->max_texture_cube_levels = IRIS_MAX_MIPLEVELS; /* 16384x16384 */
   caps->max_texture_3d_levels = 12; /* 2048x2048 */
   caps->max_stream_output_buffers = 4;
   caps->max_texture_array_layers = 2048;
   caps->max_stream_output_separate_components =
      IRIS_MAX_SOL_BINDINGS / IRIS_MAX_SOL_BUFFERS;
   caps->max_stream_output_interleaved_components = IRIS_MAX_SOL_BINDINGS;
   caps->glsl_feature_level =
   caps->glsl_feature_level_compatibility = 460;
   /* 3DSTATE_CONSTANT_XS requires the start of UBOs to be 32B aligned */
   caps->constant_buffer_offset_alignment = 32;
   caps->min_map_buffer_alignment = IRIS_MAP_BUFFER_ALIGNMENT;
   caps->shader_buffer_offset_alignment = 4;
   caps->max_shader_buffer_size = 1 << 27;
   caps->texture_buffer_offset_alignment = 16; // XXX: u_screen says 256 is the minimum value...
   caps->linear_image_pitch_alignment = 1;
   caps->linear_image_base_address_alignment = 1;
   caps->texture_transfer_modes = PIPE_TEXTURE_TRANSFER_BLIT;
   caps->max_texel_buffer_elements = IRIS_MAX_TEXTURE_BUFFER_SIZE;
   caps->max_viewports = 16;
   caps->max_geometry_output_vertices = 256;
   caps->max_geometry_total_output_components = 1024;
   caps->max_gs_invocations = 32;
   caps->max_texture_gather_components = 4;
   caps->min_texture_gather_offset = -32;
   caps->max_texture_gather_offset = 31;
   caps->max_vertex_streams = 4;
   caps->vendor_id = 0x8086;
   caps->device_id = screen->devinfo->pci_device_id;
   caps->video_memory = iris_get_video_memory(screen);
   caps->max_shader_patch_varyings =
   caps->max_varyings = 32;
   /* We want immediate arrays to go get uploaded as nir->constant_data by
    * nir_opt_large_constants() instead.
    */
   caps->prefer_imm_arrays_as_constbuf = false;
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

   caps->opencl_integer_functions =
   caps->integer_multiply_32x16 = true;

   /* Internal details of VF cache make this optimization harmful on GFX
    * version 8 and 9, because generated VERTEX_BUFFER_STATEs are cached
    * separately.
    */
   caps->allow_dynamic_vao_fastpath = devinfo->ver >= 11;

   caps->timer_resolution = DIV_ROUND_UP(1000000000ull, devinfo->timestamp_frequency);

   caps->device_protected_context =
      screen->kernel_features & KERNEL_HAS_PROTECTED_CONTEXT;

   caps->astc_void_extents_need_denorm_flush =
      devinfo->ver == 9 && !intel_device_info_is_9lp(devinfo);

   caps->min_line_width =
   caps->min_line_width_aa =
   caps->min_point_size =
   caps->min_point_size_aa = 1;

   caps->point_size_granularity =
   caps->line_width_granularity = 0.1;

   caps->max_line_width =
   caps->max_line_width_aa = 7.375f;

   caps->max_point_size =
   caps->max_point_size_aa = 255.0f;

   caps->max_texture_anisotropy = 16.0f;
   caps->max_texture_lod_bias = 15.0f;
}

static uint64_t
iris_get_timestamp(struct pipe_screen *pscreen)
{
   struct iris_screen *screen = (struct iris_screen *) pscreen;
   uint64_t result;

   if (!intel_gem_read_render_timestamp(iris_bufmgr_get_fd(screen->bufmgr),
                                        screen->devinfo->kmd_type, &result))
      return 0;

   result = intel_device_info_timebase_scale(screen->devinfo, result);

   return result;
}

void
iris_screen_destroy(struct iris_screen *screen)
{
   intel_perf_free(screen->perf_cfg);
   iris_destroy_screen_measure(screen);
   util_queue_destroy(&screen->shader_compiler_queue);
   glsl_type_singleton_decref();
   iris_bo_unreference(screen->workaround_bo);
   iris_bo_unreference(screen->breakpoint_bo);
   u_transfer_helper_destroy(screen->base.transfer_helper);
   iris_bufmgr_unref(screen->bufmgr);
   disk_cache_destroy(screen->disk_cache);
   close(screen->winsys_fd);
   ralloc_free(screen);
}

static void
iris_screen_unref(struct pipe_screen *pscreen)
{
   iris_pscreen_unref(pscreen);
}

static void
iris_query_memory_info(struct pipe_screen *pscreen,
                       struct pipe_memory_info *info)
{
   struct iris_screen *screen = (struct iris_screen *)pscreen;
   struct intel_device_info di;
   memcpy(&di, screen->devinfo, sizeof(di));

   if (!intel_device_info_update_memory_info(&di, screen->fd))
      return;

   info->total_device_memory =
      (di.mem.vram.mappable.size + di.mem.vram.unmappable.size) / 1024;
   info->avail_device_memory =
      (di.mem.vram.mappable.free + di.mem.vram.unmappable.free) / 1024;
   info->total_staging_memory = di.mem.sram.mappable.size / 1024;
   info->avail_staging_memory = di.mem.sram.mappable.free / 1024;

   /* Neither kernel gives us any way to calculate this information */
   info->device_memory_evicted = 0;
   info->nr_device_memory_evictions = 0;
}

static struct disk_cache *
iris_get_disk_shader_cache(struct pipe_screen *pscreen)
{
   struct iris_screen *screen = (struct iris_screen *) pscreen;
   return screen->disk_cache;
}

static const struct intel_l3_config *
iris_get_default_l3_config(const struct intel_device_info *devinfo,
                           bool compute)
{
   bool wants_dc_cache = true;
   bool has_slm = compute;
   const struct intel_l3_weights w =
      intel_get_default_l3_weights(devinfo, wants_dc_cache, has_slm);
   return intel_get_l3_config(devinfo, w);
}

static void
iris_detect_kernel_features(struct iris_screen *screen)
{
   const struct intel_device_info *devinfo = screen->devinfo;
   /* Kernel 5.2+ */
   if (intel_gem_supports_syncobj_wait(screen->fd))
      screen->kernel_features |= KERNEL_HAS_WAIT_FOR_SUBMIT;
   if (intel_gem_supports_protected_context(screen->fd, devinfo->kmd_type))
      screen->kernel_features |= KERNEL_HAS_PROTECTED_CONTEXT;
}

static bool
iris_init_identifier_bo(struct iris_screen *screen)
{
   void *bo_map;

   bo_map = iris_bo_map(NULL, screen->workaround_bo, MAP_READ | MAP_WRITE);
   if (!bo_map)
      return false;

   assert(iris_bo_is_real(screen->workaround_bo));

   screen->workaround_address = (struct iris_address) {
      .bo = screen->workaround_bo,
      .offset = ALIGN(
         intel_debug_write_identifiers(bo_map, 4096, "Iris"), 32),
   };

   iris_bo_unmap(screen->workaround_bo);

   return true;
}

static int
iris_screen_get_fd(struct pipe_screen *pscreen)
{
   struct iris_screen *screen = (struct iris_screen *) pscreen;

   return screen->winsys_fd;
}

static void
iris_set_damage_region(struct pipe_screen *pscreen, struct pipe_resource *pres,
                       unsigned int nrects, const struct pipe_box *rects)
{
   struct iris_resource *res = (struct iris_resource *)pres;

   res->use_damage = nrects > 0;
   if (!res->use_damage)
      return;

   res->damage.x = INT32_MAX;
   res->damage.y = INT32_MAX;
   res->damage.width = 0;
   res->damage.height = 0;

   for (unsigned i = 0; i < nrects; i++) {
      res->damage.x = MIN2(res->damage.x, rects[i].x);
      res->damage.y = MIN2(res->damage.y, rects[i].y);
      res->damage.width = MAX2(res->damage.width, rects[i].width + rects[i].x);
      res->damage.height = MAX2(res->damage.height, rects[i].height + rects[i].y);

      if (unlikely(res->damage.x == 0 &&
                   res->damage.y == 0 &&
                   res->damage.width == res->base.b.width0 &&
                   res->damage.height == res->base.b.height0))
         break;
   }

   res->damage.x = MAX2(res->damage.x, 0);
   res->damage.y = MAX2(res->damage.y, 0);
   res->damage.width = MIN2(res->damage.width, res->base.b.width0);
   res->damage.height = MIN2(res->damage.height, res->base.b.height0);
}

struct pipe_screen *
iris_screen_create(int fd, const struct pipe_screen_config *config)
{
   struct iris_screen *screen = rzalloc(NULL, struct iris_screen);
   if (!screen)
      return NULL;

   driParseConfigFiles(config->options, config->options_info, 0, "iris",
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

   process_intel_debug_variable();

   screen->bufmgr = iris_bufmgr_get_for_fd(fd, bo_reuse);
   if (!screen->bufmgr)
      return NULL;

   screen->devinfo = iris_bufmgr_get_device_info(screen->bufmgr);
   p_atomic_set(&screen->refcount, 1);

   /* Here are the i915 features we need for Iris (in chronological order) :
    *    - I915_PARAM_HAS_EXEC_NO_RELOC     (3.10)
    *    - I915_PARAM_HAS_EXEC_HANDLE_LUT   (3.10)
    *    - I915_PARAM_HAS_EXEC_BATCH_FIRST  (4.13)
    *    - I915_PARAM_HAS_EXEC_FENCE_ARRAY  (4.14)
    *    - I915_PARAM_HAS_CONTEXT_ISOLATION (4.16)
    *
    * Checking the last feature availability will include all previous ones.
    */
   if (!screen->devinfo->has_context_isolation) {
      debug_error("Kernel is too old (4.16+ required) or unusable for Iris.\n"
                  "Check your dmesg logs for loading failures.\n");
      return NULL;
   }

   screen->fd = iris_bufmgr_get_fd(screen->bufmgr);
   screen->winsys_fd = os_dupfd_cloexec(fd);

   screen->id = iris_bufmgr_create_screen_id(screen->bufmgr);

   screen->workaround_bo =
      iris_bo_alloc(screen->bufmgr, "workaround", 4096, 4096,
                    IRIS_MEMZONE_OTHER, BO_ALLOC_NO_SUBALLOC | BO_ALLOC_CAPTURE);
   if (!screen->workaround_bo)
      return NULL;

   screen->breakpoint_bo = iris_bo_alloc(screen->bufmgr, "breakpoint", 4, 4,
                                         IRIS_MEMZONE_OTHER, BO_ALLOC_ZEROED);
   if (!screen->breakpoint_bo)
      return NULL;

   if (!iris_init_identifier_bo(screen))
      return NULL;

   screen->driconf.dual_color_blend_by_location =
      driQueryOptionb(config->options, "dual_color_blend_by_location");
   screen->driconf.disable_throttling =
      driQueryOptionb(config->options, "disable_throttling");
   screen->driconf.always_flush_cache = INTEL_DEBUG(DEBUG_STALL) ||
      driQueryOptionb(config->options, "always_flush_cache");
   screen->driconf.sync_compile =
      driQueryOptionb(config->options, "sync_compile");
   screen->driconf.limit_trig_input_range =
      driQueryOptionb(config->options, "limit_trig_input_range");
   screen->driconf.lower_depth_range_rate =
      driQueryOptionf(config->options, "lower_depth_range_rate");
   screen->driconf.intel_enable_wa_14018912822 =
      driQueryOptionb(config->options, "intel_enable_wa_14018912822");
   screen->driconf.enable_tbimr =
      driQueryOptionb(config->options, "intel_tbimr");
   screen->driconf.generated_indirect_threshold =
      driQueryOptioni(config->options, "generated_indirect_threshold");

   screen->precompile = debug_get_bool_option("shader_precompile", true);

   isl_device_init(&screen->isl_dev, screen->devinfo);
   screen->isl_dev.dummy_aux_address = iris_bufmgr_get_dummy_aux_address(screen->bufmgr);

   screen->isl_dev.sampler_route_to_lsc =
      driQueryOptionb(config->options, "intel_sampler_route_to_lsc");

   iris_compiler_init(screen);

   screen->l3_config_3d = iris_get_default_l3_config(screen->devinfo, false);
   screen->l3_config_cs = iris_get_default_l3_config(screen->devinfo, true);

   iris_disk_cache_init(screen);

   slab_create_parent(&screen->transfer_pool,
                      sizeof(struct iris_transfer), 64);

   iris_detect_kernel_features(screen);

   struct pipe_screen *pscreen = &screen->base;

   iris_init_screen_fence_functions(pscreen);
   iris_init_screen_resource_functions(pscreen);
   iris_init_screen_measure(screen);

   pscreen->destroy = iris_screen_unref;
   pscreen->get_name = iris_get_name;
   pscreen->get_vendor = iris_get_vendor;
   pscreen->get_device_vendor = iris_get_device_vendor;
   pscreen->get_cl_cts_version = iris_get_cl_cts_version;
   pscreen->get_screen_fd = iris_screen_get_fd;
   pscreen->get_compiler_options = iris_get_compiler_options;
   pscreen->get_device_uuid = iris_get_device_uuid;
   pscreen->get_driver_uuid = iris_get_driver_uuid;
   pscreen->get_disk_shader_cache = iris_get_disk_shader_cache;
   pscreen->is_format_supported = iris_is_format_supported;
   pscreen->context_create = iris_create_context;
   pscreen->get_timestamp = iris_get_timestamp;
   pscreen->query_memory_info = iris_query_memory_info;
   pscreen->get_driver_query_group_info = iris_get_monitor_group_info;
   pscreen->get_driver_query_info = iris_get_monitor_info;
   pscreen->set_damage_region = iris_set_damage_region;
   iris_init_screen_program_functions(pscreen);

   iris_init_shader_caps(screen);
   iris_init_compute_caps(screen);
   iris_init_screen_caps(screen);

   genX_call(screen->devinfo, init_screen_state, screen);
   genX_call(screen->devinfo, init_screen_gen_state, screen);

   glsl_type_singleton_init_or_ref();

   intel_driver_ds_init();

   /* FINISHME: Big core vs little core (for CPUs that have both kinds of
    * cores) and, possibly, thread vs core should be considered here too.
    */
   unsigned compiler_threads = 1;
   const struct util_cpu_caps_t *caps = util_get_cpu_caps();
   unsigned hw_threads = caps->nr_cpus;

   if (hw_threads >= 12) {
      compiler_threads = hw_threads * 3 / 4;
   } else if (hw_threads >= 6) {
      compiler_threads = hw_threads - 2;
   } else if (hw_threads >= 2) {
      compiler_threads = hw_threads - 1;
   }

   if (!util_queue_init(&screen->shader_compiler_queue,
                        "sh", 64, compiler_threads,
                        UTIL_QUEUE_INIT_RESIZE_IF_FULL |
                        UTIL_QUEUE_INIT_SET_FULL_THREAD_AFFINITY,
                        NULL)) {
      iris_screen_destroy(screen);
      return NULL;
   }

   return pscreen;
}
