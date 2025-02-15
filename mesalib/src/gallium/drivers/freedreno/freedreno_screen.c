/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"

#include "util/format/u_format.h"
#include "util/format/u_format_s3tc.h"
#include "util/u_debug.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_screen.h"
#include "util/u_string.h"
#include "util/xmlconfig.h"

#include "util/os_time.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "drm-uapi/drm_fourcc.h"
#include <sys/sysinfo.h>

#include "freedreno_fence.h"
#include "freedreno_perfetto.h"
#include "freedreno_query.h"
#include "freedreno_resource.h"
#include "freedreno_screen.h"
#include "freedreno_util.h"

#include "a2xx/fd2_screen.h"
#include "a3xx/fd3_screen.h"
#include "a4xx/fd4_screen.h"
#include "a5xx/fd5_screen.h"
#include "a6xx/fd6_screen.h"

/* for fd_get_driver/device_uuid() */
#include "common/freedreno_uuid.h"

#include "a2xx/ir2.h"
#include "ir3/ir3_descriptor.h"
#include "ir3/ir3_gallium.h"
#include "ir3/ir3_nir.h"

/* clang-format off */
static const struct debug_named_value fd_debug_options[] = {
   {"msgs",      FD_DBG_MSGS,     "Print debug messages"},
   {"disasm",    FD_DBG_DISASM,   "Dump TGSI and adreno shader disassembly (a2xx only, see IR3_SHADER_DEBUG)"},
   {"dclear",    FD_DBG_DCLEAR,   "Mark all state dirty after clear"},
   {"ddraw",     FD_DBG_DDRAW,    "Mark all state dirty after draw"},
   {"noscis",    FD_DBG_NOSCIS,   "Disable scissor optimization"},
   {"direct",    FD_DBG_DIRECT,   "Force inline (SS_DIRECT) state loads"},
   {"gmem",      FD_DBG_GMEM,     "Use gmem rendering when it is permitted"},
   {"perf",      FD_DBG_PERF,     "Enable performance warnings"},
   {"nobin",     FD_DBG_NOBIN,    "Disable hw binning"},
   {"sysmem",    FD_DBG_SYSMEM,   "Use sysmem only rendering (no tiling)"},
   {"serialc",   FD_DBG_SERIALC,  "Disable asynchronous shader compile"},
   {"shaderdb",  FD_DBG_SHADERDB, "Enable shaderdb output"},
   {"nolrzfc",   FD_DBG_NOLRZFC,  "Disable LRZ fast-clear"},
   {"flush",     FD_DBG_FLUSH,    "Force flush after every draw"},
   {"inorder",   FD_DBG_INORDER,  "Disable reordering for draws/blits"},
   {"bstat",     FD_DBG_BSTAT,    "Print batch stats at context destroy"},
   {"nogrow",    FD_DBG_NOGROW,   "Disable \"growable\" cmdstream buffers, even if kernel supports it"},
   {"lrz",       FD_DBG_LRZ,      "Enable experimental LRZ support (a5xx)"},
   {"noindirect",FD_DBG_NOINDR,   "Disable hw indirect draws (emulate on CPU)"},
   {"noblit",    FD_DBG_NOBLIT,   "Disable blitter (fallback to generic blit path)"},
   {"hiprio",    FD_DBG_HIPRIO,   "Force high-priority context"},
   {"ttile",     FD_DBG_TTILE,    "Enable texture tiling (a2xx/a3xx/a5xx)"},
   {"perfcntrs", FD_DBG_PERFC,    "Expose performance counters"},
   {"noubwc",    FD_DBG_NOUBWC,   "Disable UBWC for all internal buffers"},
   {"nolrz",     FD_DBG_NOLRZ,    "Disable LRZ (a6xx)"},
   {"notile",    FD_DBG_NOTILE,   "Disable tiling for all internal buffers"},
   {"layout",    FD_DBG_LAYOUT,   "Dump resource layouts"},
   {"nofp16",    FD_DBG_NOFP16,   "Disable mediump precision lowering"},
   {"nohw",      FD_DBG_NOHW,     "Disable submitting commands to the HW"},
   {"nosbin",    FD_DBG_NOSBIN,   "Execute GMEM bins in raster order instead of 'S' pattern"},
   {"stomp",     FD_DBG_STOMP,    "Enable register stomper"},
   DEBUG_NAMED_VALUE_END
};
/* clang-format on */

DEBUG_GET_ONCE_FLAGS_OPTION(fd_mesa_debug, "FD_MESA_DEBUG", fd_debug_options, 0)

int fd_mesa_debug = 0;
bool fd_binning_enabled = true;

static const char *
fd_screen_get_name(struct pipe_screen *pscreen)
{
   return fd_dev_name(fd_screen(pscreen)->dev_id);
}

static const char *
fd_screen_get_vendor(struct pipe_screen *pscreen)
{
   return "freedreno";
}

static const char *
fd_screen_get_device_vendor(struct pipe_screen *pscreen)
{
   return "Qualcomm";
}

static void
fd_get_sample_pixel_grid(struct pipe_screen *pscreen, unsigned sample_count,
                         unsigned *out_width, unsigned *out_height)
{
   *out_width = 1;
   *out_height = 1;
}

static uint64_t
fd_screen_get_timestamp(struct pipe_screen *pscreen)
{
   struct fd_screen *screen = fd_screen(pscreen);

   if (screen->has_timestamp) {
      uint64_t n;
      fd_pipe_get_param(screen->pipe, FD_TIMESTAMP, &n);
      return ticks_to_ns(n);
   } else {
      int64_t cpu_time = os_time_get_nano();
      return cpu_time + screen->cpu_gpu_time_delta;
   }
}

static void
fd_screen_destroy(struct pipe_screen *pscreen)
{
   struct fd_screen *screen = fd_screen(pscreen);

   if (screen->aux_ctx)
      screen->aux_ctx->destroy(screen->aux_ctx);

   if (screen->tess_bo)
      fd_bo_del(screen->tess_bo);

   if (screen->pipe)
      fd_pipe_del(screen->pipe);

   if (screen->dev) {
      fd_device_purge(screen->dev);
      fd_device_del(screen->dev);
   }

   if (screen->ro)
      screen->ro->destroy(screen->ro);

   fd_bc_fini(&screen->batch_cache);
   fd_gmem_screen_fini(pscreen);

   slab_destroy_parent(&screen->transfer_pool);

   simple_mtx_destroy(&screen->lock);

   util_idalloc_mt_fini(&screen->buffer_ids);

   u_transfer_helper_destroy(pscreen->transfer_helper);

   if (screen->compiler)
      ir3_screen_fini(pscreen);

   free(screen->perfcntr_queries);
   free(screen);
}

static uint64_t
get_memory_size(struct fd_screen *screen)
{
   uint64_t system_memory;

   if (!os_get_total_physical_memory(&system_memory))
      return 0;
   if (fd_device_version(screen->dev) >= FD_VERSION_VA_SIZE) {
      uint64_t va_size;
      if (!fd_pipe_get_param(screen->pipe, FD_VA_SIZE, &va_size)) {
         system_memory = MIN2(system_memory, va_size);
      }
   }

   return system_memory;
}

static void
fd_query_memory_info(struct pipe_screen *pscreen,
                     struct pipe_memory_info *info)
{
   unsigned mem = get_memory_size(fd_screen(pscreen)) >> 10;

   memset(info, 0, sizeof(*info));

   info->total_device_memory = mem;
   info->avail_device_memory = mem;
}

static void
fd_init_shader_caps(struct fd_screen *screen)
{
   for (unsigned i = 0; i <= PIPE_SHADER_COMPUTE; i++) {
      struct pipe_shader_caps *caps =
         (struct pipe_shader_caps *)&screen->base.shader_caps[i];

      switch (i) {
      case PIPE_SHADER_TESS_CTRL:
      case PIPE_SHADER_TESS_EVAL:
      case PIPE_SHADER_GEOMETRY:
         if (!is_a6xx(screen))
            continue;
         break;
      case PIPE_SHADER_COMPUTE:
         if (!has_compute(screen))
            continue;
         break;
      default:
         break;
      }

      caps->max_instructions =
      caps->max_alu_instructions =
      caps->max_tex_instructions =
      caps->max_tex_indirections = 16384;

      caps->max_control_flow_depth = 8; /* XXX */

      caps->max_inputs = is_a6xx(screen) && i != PIPE_SHADER_GEOMETRY ?
         screen->info->a6xx.vs_max_inputs_count : 16;

      caps->max_outputs = is_a6xx(screen) ? 32 : 16;

      caps->max_temps = 64; /* Max native temporaries. */

      /* NOTE: seems to be limit for a3xx is actually 512 but
       * split between VS and FS.  Use lower limit of 256 to
       * avoid getting into impossible situations:
       */
      caps->max_const_buffer0_size =
         (is_a3xx(screen) || is_a4xx(screen) || is_a5xx(screen) || is_a6xx(screen) ? 4096 : 64) * sizeof(float[4]);

      caps->max_const_buffers = is_ir3(screen) ? 16 : 1;

      caps->cont_supported = true;

      /* a2xx compiler doesn't handle indirect: */
      caps->indirect_temp_addr =
      caps->indirect_const_addr = is_ir3(screen);

      caps->tgsi_sqrt_supported = true;

      caps->integers = is_ir3(screen);

      caps->int16 =
      caps->fp16 =
         (is_a5xx(screen) || is_a6xx(screen)) &&
         (i == PIPE_SHADER_COMPUTE || i == PIPE_SHADER_FRAGMENT) &&
         !FD_DBG(NOFP16);

      caps->max_texture_samplers =
      caps->max_sampler_views = 16;

      caps->supported_irs =
         (1 << PIPE_SHADER_IR_NIR) |
         /* tgsi_to_nir doesn't support all stages: */
         COND(i == PIPE_SHADER_VERTEX ||
              i == PIPE_SHADER_FRAGMENT ||
              i == PIPE_SHADER_COMPUTE,
              1 << PIPE_SHADER_IR_TGSI);

      if (is_a6xx(screen)) {
         caps->max_shader_buffers = IR3_BINDLESS_SSBO_COUNT;
         caps->max_shader_images = IR3_BINDLESS_IMAGE_COUNT;
      } else if (is_a4xx(screen) || is_a5xx(screen)) {
         /* a5xx (and a4xx for that matter) has one state-block
          * for compute-shader SSBO's and another that is shared
          * by VS/HS/DS/GS/FS..  so to simplify things for now
          * just advertise SSBOs for FS and CS.  We could possibly
          * do what blob does, and partition the space for
          * VS/HS/DS/GS/FS.  The blob advertises:
          *
          *   GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS: 4
          *   GL_MAX_GEOMETRY_SHADER_STORAGE_BLOCKS: 4
          *   GL_MAX_TESS_CONTROL_SHADER_STORAGE_BLOCKS: 4
          *   GL_MAX_TESS_EVALUATION_SHADER_STORAGE_BLOCKS: 4
          *   GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS: 4
          *   GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS: 24
          *   GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS: 24
          *
          * I think that way we could avoid having to patch shaders
          * for actual SSBO indexes by using a static partitioning.
          *
          * Note same state block is used for images and buffers,
          * but images also need texture state for read access
          * (isam/isam.3d)
          */
         if (i == PIPE_SHADER_FRAGMENT || i == PIPE_SHADER_COMPUTE) {
            caps->max_shader_buffers =
            caps->max_shader_images = 24;
         }
      }
   }
}

static void
fd_init_compute_caps(struct fd_screen *screen)
{
   struct pipe_compute_caps *caps =
      (struct pipe_compute_caps *)&screen->base.compute_caps;

   if (!has_compute(screen))
      return;

   struct ir3_compiler *compiler = screen->compiler;

   caps->address_bits = screen->gen >= 5 ? 64 : 32;

   snprintf(caps->ir_target, sizeof(caps->ir_target), "ir3");

   caps->grid_dimension = 3;

   caps->max_grid_size[0] =
   caps->max_grid_size[1] =
   caps->max_grid_size[2] = 65535;

   caps->max_block_size[0] = 1024;
   caps->max_block_size[1] = 1024;
   caps->max_block_size[2] = 64;

   caps->max_threads_per_block = 1024;

   caps->max_global_size = screen->ram_size;

   caps->max_local_size = screen->info->cs_shared_mem_size;

   caps->max_private_size =
   caps->max_input_size = 4096;

   caps->max_mem_alloc_size = screen->ram_size;

   caps->max_clock_frequency = screen->max_freq / 1000000;

   caps->max_compute_units = 9999; // TODO

   caps->images_supported = true;

   caps->subgroup_sizes = 32; // TODO

   caps->max_variable_threads_per_block = compiler->max_variable_workgroup_size;
}

static void
fd_init_screen_caps(struct fd_screen *screen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&screen->base.caps;

   u_init_pipe_screen_caps(&screen->base, 1);

   /* this is probably not totally correct.. but it's a start: */

   /* Supported features (boolean caps). */
   caps->npot_textures = true;
   caps->mixed_framebuffer_sizes = true;
   caps->anisotropic_filter = true;
   caps->blend_equation_separate = true;
   caps->texture_swizzle = true;
   caps->fs_coord_origin_upper_left = true;
   caps->seamless_cube_map = true;
   caps->vertex_color_unclamped = true;
   caps->quads_follow_provoking_vertex_convention = true;
   caps->string_marker = true;
   caps->mixed_color_depth_bits = true;
   caps->texture_barrier = true;
   caps->invalidate_buffer = true;
   caps->glsl_tess_levels_as_inputs = true;
   caps->texture_mirror_clamp_to_edge = true;
   caps->gl_spirv = true;
   caps->fbfetch_coherent = true;
   caps->has_const_bw = true;

   caps->copy_between_compressed_and_plain_formats =
   caps->multi_draw_indirect =
   caps->draw_parameters =
   caps->multi_draw_indirect_params =
   caps->depth_bounds_test = is_a6xx(screen);

   caps->vertex_input_alignment =
      is_a2xx(screen) ? PIPE_VERTEX_INPUT_ALIGNMENT_4BYTE : PIPE_VERTEX_INPUT_ALIGNMENT_NONE;

   caps->fs_coord_pixel_center_integer = is_a2xx(screen);
   caps->fs_coord_pixel_center_half_integer = !is_a2xx(screen);

   caps->packed_uniforms = !is_a2xx(screen);

   caps->robust_buffer_access_behavior =
   caps->device_reset_status_query = screen->has_robustness;

   caps->compute = has_compute(screen);

   caps->texture_transfer_modes = screen->gen >= 6 ? PIPE_TEXTURE_TRANSFER_BLIT : 0;

   caps->pci_group =
   caps->pci_bus =
   caps->pci_device =
   caps->pci_function = 0;

   caps->supported_prim_modes =
   caps->supported_prim_modes_with_restart = screen->primtypes_mask;

   caps->fragment_shader_texture_lod =
   caps->fragment_shader_derivatives =
   caps->primitive_restart =
   caps->primitive_restart_fixed_index =
   caps->vs_instanceid =
   caps->vertex_element_instance_divisor =
   caps->indep_blend_enable =
   caps->indep_blend_func =
   caps->texture_buffer_objects =
   caps->texture_half_float_linear =
   caps->conditional_render =
   caps->conditional_render_inverted =
   caps->seamless_cube_map_per_texture =
   caps->clip_halfz =
      is_a3xx(screen) || is_a4xx(screen) || is_a5xx(screen) || is_a6xx(screen);

   caps->texture_multisample =
   caps->image_store_formatted =
   caps->image_load_formatted = is_a5xx(screen) || is_a6xx(screen);

   caps->fake_sw_msaa = !caps->texture_multisample;

   caps->surface_sample_count = is_a6xx(screen);

   caps->depth_clip_disable = is_a3xx(screen) || is_a4xx(screen) || is_a6xx(screen);

   caps->post_depth_coverage =
   caps->depth_clip_disable_separate =
   caps->demote_to_helper_invocation = is_a6xx(screen);

   caps->sampler_reduction_minmax =
   caps->sampler_reduction_minmax_arb =
      is_a6xx(screen) && screen->info->a6xx.has_sampler_minmax;

   caps->programmable_sample_locations =
      is_a6xx(screen) && screen->info->a6xx.has_sample_locations;

   caps->polygon_offset_clamp = is_a4xx(screen) || is_a5xx(screen) || is_a6xx(screen);

   caps->prefer_imm_arrays_as_constbuf = false;

   caps->texture_buffer_offset_alignment = is_a3xx(screen) ? 16 :
      (is_a4xx(screen) || is_a5xx(screen) || is_a6xx(screen) ? 64 : 0);
   caps->max_texel_buffer_elements =
      /* We could possibly emulate more by pretending 2d/rect textures and
       * splitting high bits of index into 2nd dimension..
       */
      is_a3xx(screen) ? A3XX_MAX_TEXEL_BUFFER_ELEMENTS_UINT :
      /* Note that the Vulkan blob on a540 and 640 report a
       * maxTexelBufferElements of just 65536 (the GLES3.2 and Vulkan
       * minimum).
       */
      (is_a4xx(screen) || is_a5xx(screen) || is_a6xx(screen) ?
       A4XX_MAX_TEXEL_BUFFER_ELEMENTS_UINT : 0);

   caps->texture_border_color_quirk = PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_FREEDRENO;

   caps->texture_float_linear =
   caps->cube_map_array =
   caps->sampler_view_target =
   caps->texture_query_lod = is_a4xx(screen) || is_a5xx(screen) || is_a6xx(screen);

   /* Note that a5xx can do this, it just can't (at least with
    * current firmware) do draw_indirect with base_instance.
    * Since draw_indirect is needed sooner (gles31 and gl40 vs
    * gl42), hide base_instance on a5xx.  :-/
    */
   caps->start_instance = is_a4xx(screen) || is_a6xx(screen);

   caps->constant_buffer_offset_alignment = 64;

   caps->int64 =
   caps->doubles = is_ir3(screen);

   caps->glsl_feature_level =
   caps->glsl_feature_level_compatibility =
      is_a6xx(screen) ? 460 : (is_ir3(screen) ? 140 : 120);

   caps->essl_feature_level =
      is_a4xx(screen) || is_a5xx(screen) || is_a6xx(screen) ? 320 :
      (is_ir3(screen) ? 300 : 120);

   caps->shader_buffer_offset_alignment =
      is_a6xx(screen) ? 64 : (is_a5xx(screen) || is_a4xx(screen) ? 4 : 0);

   caps->max_texture_gather_components =
      is_a4xx(screen) || is_a5xx(screen) || is_a6xx(screen) ? 4 : 0;

   /* TODO if we need this, do it in nir/ir3 backend to avoid breaking
    * precompile: */
   caps->force_persample_interp = false;

   caps->fbfetch =
      fd_device_version(screen->dev) >= FD_VERSION_GMEM_BASE && is_a6xx(screen) ?
      screen->max_rts : 0;
   caps->sample_shading = is_a6xx(screen);

   caps->context_priority_mask = screen->priority_mask;

   caps->draw_indirect = is_a4xx(screen) || is_a5xx(screen) || is_a6xx(screen);

   caps->framebuffer_no_attachment =
      is_a4xx(screen) || is_a5xx(screen) || is_a6xx(screen);

   /* name is confusing, but this turns on std430 packing */
   caps->load_constbuf = is_ir3(screen);

   caps->nir_images_as_deref = false;

   caps->vs_layer_viewport =
   caps->tes_layer_viewport = is_a6xx(screen);

   caps->max_viewports = is_a6xx(screen) ? 16 : 1;

   caps->max_varyings = is_a6xx(screen) ? 31 : 16;

   /* We don't really have a limit on this, it all goes into the main
    * memory buffer. Needs to be at least 120 / 4 (minimum requirement
    * for GL_MAX_TESS_PATCH_COMPONENTS).
    */
   caps->max_shader_patch_varyings = 128;

   caps->max_texture_upload_memory_budget = 64 * 1024 * 1024;

   caps->shareable_shaders = is_ir3(screen);

   /* Geometry shaders.. */
   caps->max_geometry_output_vertices = 256;
   caps->max_geometry_total_output_components = 2048;
   caps->max_gs_invocations = 32;

   /* Only a2xx has the half-border clamp mode in HW, just have mesa/st lower
    * it for later HW.
    */
   caps->gl_clamp = is_a2xx(screen);

   caps->clip_planes =
      /* Gens that support GS, have GS lowered into a quasi-VS which confuses
       * the frontend clip-plane lowering.  So we handle this in the backend
       *
       */
      screen->base.shader_caps[PIPE_SHADER_GEOMETRY].max_instructions ? 1 :
      /* On a3xx, there is HW support for GL user clip planes that
       * occasionally has to fall back to shader key-based lowering to clip
       * distances in the VS, and we don't support clip distances so that is
       * always shader-based lowering in the FS.
       *
       * On a4xx, there is no HW support for clip planes, so they are
       * always lowered to clip distances.  We also lack SW support for the
       * HW's clip distances in HW, so we do shader-based lowering in the FS
       * in the driver backend.
       *
       * On a5xx-a6xx, we have the HW clip distances hooked up, so we just let
       * mesa/st lower desktop GL's clip planes to clip distances in the last
       * vertex shader stage.
       *
       * NOTE: but see comment above about geometry shaders
       */
      (is_a5xx(screen) ? 0 : 1);

   /* Stream output. */
   caps->max_vertex_streams = is_a6xx(screen) ?  /* has SO + GS */
      PIPE_MAX_SO_BUFFERS : 0;
   caps->max_stream_output_buffers = is_ir3(screen) ? PIPE_MAX_SO_BUFFERS : 0;
   caps->stream_output_pause_resume =
   caps->stream_output_interleave_buffers =
   caps->fs_position_is_sysval =
   caps->tgsi_texcoord =
   caps->shader_array_components =
   caps->texture_query_samples =
   caps->fs_fine_derivative = is_ir3(screen);
   caps->shader_group_vote = is_a6xx(screen);
   caps->fs_face_is_integer_sysval = true;
   caps->fs_point_is_sysval = is_a2xx(screen);
   caps->max_stream_output_separate_components =
   caps->max_stream_output_interleaved_components = is_ir3(screen) ?
      16 * 4 /* should only be shader out limit? */ : 0;

   /* Texturing. */
   caps->max_texture_2d_size =
      is_a6xx(screen) || is_a5xx(screen) || is_a4xx(screen) ? 16384 : 8192;
   caps->max_texture_cube_levels =
      is_a6xx(screen) || is_a5xx(screen) || is_a4xx(screen) ? 15 : 14;

   caps->max_texture_3d_levels = is_a3xx(screen) ? 11 : 12;

   caps->max_texture_array_layers = is_a6xx(screen) ? 2048 :
      (is_a3xx(screen) || is_a4xx(screen) || is_a5xx(screen) ? 256 : 0);

   /* Render targets. */
   caps->max_render_targets = screen->max_rts;
   caps->max_dual_source_render_targets = (is_a3xx(screen) || is_a6xx(screen)) ? 1 : 0;

   /* Queries. */
   caps->occlusion_query =
      is_a3xx(screen) || is_a4xx(screen) || is_a5xx(screen) || is_a6xx(screen);
   caps->query_timestamp =
   caps->query_time_elapsed =
      /* only a4xx, requires new enough kernel so we know max_freq: */
      (screen->max_freq > 0) && (is_a4xx(screen) || is_a5xx(screen) || is_a6xx(screen));
   caps->timer_resolution = ticks_to_ns(1);
   caps->query_buffer_object =
   caps->query_so_overflow =
   caps->query_pipeline_statistics_single = is_a6xx(screen);

   caps->vendor_id = 0x5143;
   caps->device_id = 0xFFFFFFFF;

   caps->video_memory = get_memory_size(screen) >> 20;

   /* Enables GL_ATI_meminfo */
   caps->query_memory_info = get_memory_size(screen) != 0;

   caps->uma = true;
   caps->memobj = fd_device_version(screen->dev) >= FD_VERSION_MEMORY_FD;
   caps->native_fence_fd = fd_device_version(screen->dev) >= FD_VERSION_FENCE_FD;
   caps->fence_signal = screen->has_syncobj;
   caps->cull_distance = is_a6xx(screen);
   caps->shader_stencil_export = is_a6xx(screen);
   caps->two_sided_color = false;
   caps->throttle = screen->driconf.enable_throttling;

   caps->min_line_width =
   caps->min_line_width_aa =
   caps->min_point_size =
   caps->min_point_size_aa = 1;

   caps->point_size_granularity =
   caps->line_width_granularity = 0.1f;

   caps->max_line_width =
   caps->max_line_width_aa = 127.0f;

   caps->max_point_size =
   caps->max_point_size_aa = 4092.0f;

   caps->max_texture_anisotropy = 16.0f;
   caps->max_texture_lod_bias = 15.0f;
}

static const void *
fd_get_compiler_options(struct pipe_screen *pscreen, enum pipe_shader_ir ir,
                        enum pipe_shader_type shader)
{
   struct fd_screen *screen = fd_screen(pscreen);

   if (is_ir3(screen))
      return ir3_get_compiler_options(screen->compiler);

   return ir2_get_compiler_options();
}

static struct disk_cache *
fd_get_disk_shader_cache(struct pipe_screen *pscreen)
{
   struct fd_screen *screen = fd_screen(pscreen);

   if (is_ir3(screen)) {
      struct ir3_compiler *compiler = screen->compiler;
      return compiler->disk_cache;
   }

   return NULL;
}

bool
fd_screen_bo_get_handle(struct pipe_screen *pscreen, struct fd_bo *bo,
                        struct renderonly_scanout *scanout, unsigned stride,
                        struct winsys_handle *whandle)
{
   struct fd_screen *screen = fd_screen(pscreen);

   whandle->stride = stride;

   if (whandle->type == WINSYS_HANDLE_TYPE_SHARED) {
      return fd_bo_get_name(bo, &whandle->handle) == 0;
   } else if (whandle->type == WINSYS_HANDLE_TYPE_KMS) {
      if (screen->ro) {
         return renderonly_get_handle(scanout, whandle);
      } else {
         uint32_t handle = fd_bo_handle(bo);
         if (!handle)
            return false;
         whandle->handle = handle;
         return true;
      }
   } else if (whandle->type == WINSYS_HANDLE_TYPE_FD) {
      int fd = fd_bo_dmabuf(bo);
      if (fd < 0)
         return false;
      whandle->handle = fd;
      return true;
   } else {
      return false;
   }
}

static bool
is_format_supported(struct pipe_screen *pscreen,
                    enum pipe_format format,
                    uint64_t modifier)
{
   struct fd_screen *screen = fd_screen(pscreen);
   if (screen->is_format_supported)
      return screen->is_format_supported(pscreen, format, modifier);
   return modifier == DRM_FORMAT_MOD_LINEAR;
}

static void
fd_screen_query_dmabuf_modifiers(struct pipe_screen *pscreen,
                                 enum pipe_format format, int max,
                                 uint64_t *modifiers,
                                 unsigned int *external_only, int *count)
{
   const uint64_t all_modifiers[] = {
      DRM_FORMAT_MOD_LINEAR,
      DRM_FORMAT_MOD_QCOM_COMPRESSED,
      DRM_FORMAT_MOD_QCOM_TILED3,
   };

   int num = 0;

   for (int i = 0; i < ARRAY_SIZE(all_modifiers); i++) {
      if (!is_format_supported(pscreen, format, all_modifiers[i]))
         continue;

      if (num < max) {
         if (modifiers)
            modifiers[num] = all_modifiers[i];

         if (external_only)
            external_only[num] = false;
      }

      num++;
   }

   *count = num;
}

static bool
fd_screen_is_dmabuf_modifier_supported(struct pipe_screen *pscreen,
                                       uint64_t modifier,
                                       enum pipe_format format,
                                       bool *external_only)
{
   return is_format_supported(pscreen, format, modifier);
}

struct fd_bo *
fd_screen_bo_from_handle(struct pipe_screen *pscreen,
                         struct winsys_handle *whandle)
{
   struct fd_screen *screen = fd_screen(pscreen);
   struct fd_bo *bo;

   if (whandle->type == WINSYS_HANDLE_TYPE_SHARED) {
      bo = fd_bo_from_name(screen->dev, whandle->handle);
   } else if (whandle->type == WINSYS_HANDLE_TYPE_KMS) {
      bo = fd_bo_from_handle(screen->dev, whandle->handle, 0);
   } else if (whandle->type == WINSYS_HANDLE_TYPE_FD) {
      bo = fd_bo_from_dmabuf(screen->dev, whandle->handle);
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

static void
_fd_fence_ref(struct pipe_screen *pscreen, struct pipe_fence_handle **ptr,
              struct pipe_fence_handle *pfence)
{
   fd_pipe_fence_ref(ptr, pfence);
}

static void
fd_screen_get_device_uuid(struct pipe_screen *pscreen, char *uuid)
{
   struct fd_screen *screen = fd_screen(pscreen);

   fd_get_device_uuid(uuid, screen->dev_id);
}

static void
fd_screen_get_driver_uuid(struct pipe_screen *pscreen, char *uuid)
{
   fd_get_driver_uuid(uuid);
}

static int
fd_screen_get_fd(struct pipe_screen *pscreen)
{
   struct fd_screen *screen = fd_screen(pscreen);
   return fd_device_fd(screen->dev);
}

struct pipe_screen *
fd_screen_create(int fd,
                 const struct pipe_screen_config *config,
                 struct renderonly *ro)
{
   struct fd_device *dev = fd_device_new_dup(fd);
   if (!dev)
      return NULL;

   struct fd_screen *screen = CALLOC_STRUCT(fd_screen);
   struct pipe_screen *pscreen;
   uint64_t val;

   fd_mesa_debug = debug_get_option_fd_mesa_debug();

   if (FD_DBG(NOBIN))
      fd_binning_enabled = false;

   if (!screen)
      return NULL;

#ifdef HAVE_PERFETTO
   fd_perfetto_init();
#endif

   util_gpuvis_init();

   pscreen = &screen->base;

   screen->dev = dev;
   screen->ro = ro;

   // maybe this should be in context?
   screen->pipe = fd_pipe_new(screen->dev, FD_PIPE_3D);
   if (!screen->pipe) {
      DBG("could not create 3d pipe");
      goto fail;
   }

   if (fd_pipe_get_param(screen->pipe, FD_GMEM_SIZE, &val)) {
      DBG("could not get GMEM size");
      goto fail;
   }
   screen->gmemsize_bytes = debug_get_num_option("FD_MESA_GMEM", val);

   if (fd_device_version(dev) >= FD_VERSION_GMEM_BASE) {
      fd_pipe_get_param(screen->pipe, FD_GMEM_BASE, &screen->gmem_base);
   }

   if (fd_pipe_get_param(screen->pipe, FD_MAX_FREQ, &val)) {
      DBG("could not get gpu freq");
      /* this limits what performance related queries are
       * supported but is not fatal
       */
      screen->max_freq = 0;
   } else {
      screen->max_freq = val;
   }

   if (fd_pipe_get_param(screen->pipe, FD_TIMESTAMP, &val) == 0)
      screen->has_timestamp = true;

   screen->dev_id = fd_pipe_dev_id(screen->pipe);

   if (fd_pipe_get_param(screen->pipe, FD_GPU_ID, &val)) {
      DBG("could not get gpu-id");
      goto fail;
   }
   screen->gpu_id = val;

   if (fd_pipe_get_param(screen->pipe, FD_CHIP_ID, &val)) {
      DBG("could not get chip-id");
      /* older kernels may not have this property: */
      unsigned core = screen->gpu_id / 100;
      unsigned major = (screen->gpu_id % 100) / 10;
      unsigned minor = screen->gpu_id % 10;
      unsigned patch = 0; /* assume the worst */
      val = (patch & 0xff) | ((minor & 0xff) << 8) | ((major & 0xff) << 16) |
            ((core & 0xff) << 24);
   }
   screen->chip_id = val;
   screen->gen = fd_dev_gen(screen->dev_id);

   if (fd_pipe_get_param(screen->pipe, FD_NR_PRIORITIES, &val)) {
      DBG("could not get # of rings");
      screen->priority_mask = 0;
   } else {
      /* # of rings equates to number of unique priority values: */
      screen->priority_mask = (1 << val) - 1;

      /* Lowest numerical value (ie. zero) is highest priority: */
      screen->prio_high = 0;

      /* Highest numerical value is lowest priority: */
      screen->prio_low = val - 1;

      /* Pick midpoint for normal priority.. note that whatever the
       * range of possible priorities, since we divide by 2 the
       * result will either be an integer or an integer plus 0.5,
       * in which case it will round down to an integer, so int
       * division will give us an appropriate result in either
       * case:
       */
      screen->prio_norm = val / 2;
   }

   if (fd_device_version(dev) >= FD_VERSION_ROBUSTNESS)
      screen->has_robustness = true;

   screen->has_syncobj = fd_has_syncobj(screen->dev);

   /* parse driconf configuration now for device specific overrides: */
   driParseConfigFiles(config->options, config->options_info, 0, "msm",
                       NULL, fd_dev_name(screen->dev_id), NULL, 0, NULL, 0);

   screen->driconf.conservative_lrz =
         !driQueryOptionb(config->options, "disable_conservative_lrz");
   screen->driconf.enable_throttling =
         !driQueryOptionb(config->options, "disable_throttling");
   screen->driconf.dual_color_blend_by_location =
         driQueryOptionb(config->options, "dual_color_blend_by_location");

   struct sysinfo si;
   sysinfo(&si);
   screen->ram_size = si.totalram;

   DBG("Pipe Info:");
   DBG(" GPU-id:          %s", fd_dev_name(screen->dev_id));
   DBG(" Chip-id:         0x%016"PRIx64, screen->chip_id);
   DBG(" GMEM size:       0x%08x", screen->gmemsize_bytes);

   const struct fd_dev_info info = fd_dev_info(screen->dev_id);
   if (!info.chip) {
      mesa_loge("unsupported GPU: a%03d", screen->gpu_id);
      goto fail;
   }

   screen->dev_info = info;
   screen->info = &screen->dev_info;

   /* explicitly checking for GPU revisions that are known to work.  This
    * may be overly conservative for a3xx, where spoofing the gpu_id with
    * the blob driver seems to generate identical cmdstream dumps.  But
    * on a2xx, there seem to be small differences between the GPU revs
    * so it is probably better to actually test first on real hardware
    * before enabling:
    *
    * If you have a different adreno version, feel free to add it to one
    * of the cases below and see what happens.  And if it works, please
    * send a patch ;-)
    */
   switch (screen->gen) {
   case 2:
      fd2_screen_init(pscreen);
      break;
   case 3:
      fd3_screen_init(pscreen);
      break;
   case 4:
      fd4_screen_init(pscreen);
      break;
   case 5:
      fd5_screen_init(pscreen);
      break;
   case 6:
   case 7:
      fd6_screen_init(pscreen);
      break;
   default:
      mesa_loge("unsupported GPU generation: a%uxx", screen->gen);
      goto fail;
   }

   /* fdN_screen_init() should set this: */
   assert(screen->primtypes);
   screen->primtypes_mask = 0;
   for (unsigned i = 0; i <= MESA_PRIM_COUNT; i++)
      if (screen->primtypes[i])
         screen->primtypes_mask |= (1 << i);

   if (FD_DBG(PERFC)) {
      screen->perfcntr_groups =
         fd_perfcntrs(screen->dev_id, &screen->num_perfcntr_groups);
   }

   /* NOTE: don't enable if we have too old of a kernel to support
    * growable cmdstream buffers, since memory requirement for cmdstream
    * buffers would be too much otherwise.
    */
   if (fd_device_version(dev) >= FD_VERSION_UNLIMITED_CMDS)
      screen->reorder = !FD_DBG(INORDER);

   fd_bc_init(&screen->batch_cache);

   list_inithead(&screen->context_list);

   util_idalloc_mt_init_tc(&screen->buffer_ids);

   (void)simple_mtx_init(&screen->lock, mtx_plain);

   pscreen->destroy = fd_screen_destroy;
   pscreen->get_screen_fd = fd_screen_get_fd;
   pscreen->query_memory_info = fd_query_memory_info;
   pscreen->get_compiler_options = fd_get_compiler_options;
   pscreen->get_disk_shader_cache = fd_get_disk_shader_cache;

   fd_resource_screen_init(pscreen);
   fd_query_screen_init(pscreen);
   fd_gmem_screen_init(pscreen);

   pscreen->get_name = fd_screen_get_name;
   pscreen->get_vendor = fd_screen_get_vendor;
   pscreen->get_device_vendor = fd_screen_get_device_vendor;

   pscreen->get_sample_pixel_grid = fd_get_sample_pixel_grid;

   pscreen->get_timestamp = fd_screen_get_timestamp;

   pscreen->fence_reference = _fd_fence_ref;
   pscreen->fence_finish = fd_pipe_fence_finish;
   pscreen->fence_get_fd = fd_pipe_fence_get_fd;

   pscreen->query_dmabuf_modifiers = fd_screen_query_dmabuf_modifiers;
   pscreen->is_dmabuf_modifier_supported =
      fd_screen_is_dmabuf_modifier_supported;

   pscreen->get_device_uuid = fd_screen_get_device_uuid;
   pscreen->get_driver_uuid = fd_screen_get_driver_uuid;

   fd_init_shader_caps(screen);
   fd_init_compute_caps(screen);
   fd_init_screen_caps(screen);

   slab_create_parent(&screen->transfer_pool, sizeof(struct fd_transfer), 16);

   simple_mtx_init(&screen->aux_ctx_lock, mtx_plain);

   return pscreen;

fail:
   fd_screen_destroy(pscreen);
   return NULL;
}

struct fd_context *
fd_screen_aux_context_get(struct pipe_screen *pscreen)
{
   struct fd_screen *screen = fd_screen(pscreen);

   simple_mtx_lock(&screen->aux_ctx_lock);

   if (!screen->aux_ctx) {
      screen->aux_ctx = pscreen->context_create(pscreen, NULL, 0);
   }

   return fd_context(screen->aux_ctx);
}

void
fd_screen_aux_context_put(struct pipe_screen *pscreen)
{
   struct fd_screen *screen = fd_screen(pscreen);

   screen->aux_ctx->flush(screen->aux_ctx, NULL, 0);
   simple_mtx_unlock(&screen->aux_ctx_lock);
}
