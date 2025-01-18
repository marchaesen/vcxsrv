/*
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright (C) 2019 Collabora, Ltd.
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "draw/draw_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "util/format/u_format.h"
#include "util/format/u_format_s3tc.h"
#include "util/os_time.h"
#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/u_process.h"
#include "util/u_screen.h"
#include "util/u_video.h"
#include "util/xmlconfig.h"

#include <fcntl.h>

#include "drm-uapi/drm_fourcc.h"
#include "drm-uapi/panfrost_drm.h"

#include "decode.h"
#include "pan_bo.h"
#include "pan_fence.h"
#include "pan_public.h"
#include "pan_resource.h"
#include "pan_screen.h"
#include "pan_shader.h"
#include "pan_util.h"

#include "pan_context.h"

#define DEFAULT_MAX_AFBC_PACKING_RATIO 90

/* clang-format off */
static const struct debug_named_value panfrost_debug_options[] = {
   {"perf",       PAN_DBG_PERF,     "Enable performance warnings"},
   {"trace",      PAN_DBG_TRACE,    "Trace the command stream"},
   {"dirty",      PAN_DBG_DIRTY,    "Always re-emit all state"},
   {"sync",       PAN_DBG_SYNC,     "Wait for each job's completion and abort on GPU faults"},
   {"nofp16",     PAN_DBG_NOFP16,    "Disable 16-bit support"},
   {"gl3",        PAN_DBG_GL3,      "Enable experimental GL 3.x implementation, up to 3.3"},
   {"noafbc",     PAN_DBG_NO_AFBC,  "Disable AFBC support"},
   {"nocrc",      PAN_DBG_NO_CRC,   "Disable transaction elimination"},
   {"msaa16",     PAN_DBG_MSAA16,   "Enable MSAA 8x and 16x support"},
   {"linear",     PAN_DBG_LINEAR,   "Force linear textures"},
   {"nocache",    PAN_DBG_NO_CACHE, "Disable BO cache"},
   {"dump",       PAN_DBG_DUMP,     "Dump all graphics memory"},
#ifdef PAN_DBG_OVERFLOW
   {"overflow",   PAN_DBG_OVERFLOW, "Check for buffer overflows in pool uploads"},
#endif
   {"yuv",        PAN_DBG_YUV,      "Tint YUV textures with blue for 1-plane and green for 2-plane"},
   {"forcepack",  PAN_DBG_FORCE_PACK,  "Force packing of AFBC textures on upload"},
   {"cs",         PAN_DBG_CS,       "Enable extra checks in command stream"},
   DEBUG_NAMED_VALUE_END
};
/* clang-format on */

static const char *
panfrost_get_name(struct pipe_screen *screen)
{
   return pan_device(screen)->model->name;
}

static const char *
panfrost_get_vendor(struct pipe_screen *screen)
{
   return "Mesa";
}

static const char *
panfrost_get_device_vendor(struct pipe_screen *screen)
{
   return "Arm";
}

static int
from_kmod_group_allow_priority_flags(
   enum pan_kmod_group_allow_priority_flags kmod_flags)
{
   int flags = 0;

   if (kmod_flags & PAN_KMOD_GROUP_ALLOW_PRIORITY_REALTIME)
      flags |= PIPE_CONTEXT_PRIORITY_REALTIME;

   if (kmod_flags & PAN_KMOD_GROUP_ALLOW_PRIORITY_HIGH)
      flags |= PIPE_CONTEXT_PRIORITY_HIGH;

   if (kmod_flags & PAN_KMOD_GROUP_ALLOW_PRIORITY_MEDIUM)
      flags |= PIPE_CONTEXT_PRIORITY_MEDIUM;

   if (kmod_flags & PAN_KMOD_GROUP_ALLOW_PRIORITY_LOW)
      flags |= PIPE_CONTEXT_PRIORITY_LOW;

   return flags;
}

static int
panfrost_get_shader_param(struct pipe_screen *screen,
                          enum pipe_shader_type shader,
                          enum pipe_shader_cap param)
{
   struct panfrost_device *dev = pan_device(screen);
   bool is_nofp16 = dev->debug & PAN_DBG_NOFP16;

   switch (shader) {
   case PIPE_SHADER_VERTEX:
   case PIPE_SHADER_FRAGMENT:
   case PIPE_SHADER_COMPUTE:
      break;
   default:
      return 0;
   }

   /* We only allow observable side effects (memory writes) in compute and
    * fragment shaders. Side effects in the geometry pipeline cause
    * trouble with IDVS and conflict with our transform feedback lowering.
    */
   bool allow_side_effects = (shader != PIPE_SHADER_VERTEX);

   switch (param) {
   case PIPE_SHADER_CAP_MAX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_ALU_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INSTRUCTIONS:
   case PIPE_SHADER_CAP_MAX_TEX_INDIRECTIONS:
      return 16384; /* arbitrary */

   case PIPE_SHADER_CAP_MAX_CONTROL_FLOW_DEPTH:
      return 1024; /* arbitrary */

   case PIPE_SHADER_CAP_MAX_INPUTS:
      /* Used as ABI on Midgard */
      return 16;

   case PIPE_SHADER_CAP_MAX_OUTPUTS:
      return shader == PIPE_SHADER_FRAGMENT ? 8 : PIPE_MAX_ATTRIBS;

   case PIPE_SHADER_CAP_MAX_TEMPS:
      return 256; /* arbitrary */

   case PIPE_SHADER_CAP_MAX_CONST_BUFFER0_SIZE:
      return 16 * 1024 * sizeof(float);

   case PIPE_SHADER_CAP_MAX_CONST_BUFFERS:
      STATIC_ASSERT(PAN_MAX_CONST_BUFFERS < 0x100);
      return PAN_MAX_CONST_BUFFERS;

   case PIPE_SHADER_CAP_CONT_SUPPORTED:
      return 0;

   case PIPE_SHADER_CAP_INDIRECT_TEMP_ADDR:
      return dev->arch >= 6;

   case PIPE_SHADER_CAP_INDIRECT_CONST_ADDR:
      return 1;

   case PIPE_SHADER_CAP_SUBROUTINES:
      return 0;

   case PIPE_SHADER_CAP_TGSI_SQRT_SUPPORTED:
      return 0;

   case PIPE_SHADER_CAP_INTEGERS:
      return 1;

      /* The Bifrost compiler supports full 16-bit. Midgard could but int16
       * support is untested, so restrict INT16 to Bifrost. Midgard
       * architecturally cannot support fp16 derivatives. */

   case PIPE_SHADER_CAP_FP16:
   case PIPE_SHADER_CAP_GLSL_16BIT_CONSTS:
      return !is_nofp16;
   case PIPE_SHADER_CAP_FP16_DERIVATIVES:
   case PIPE_SHADER_CAP_FP16_CONST_BUFFERS:
      return dev->arch >= 6 && !is_nofp16;
   case PIPE_SHADER_CAP_INT16:
      /* Blocked on https://gitlab.freedesktop.org/mesa/mesa/-/issues/6075 */
      return false;

   case PIPE_SHADER_CAP_INT64_ATOMICS:
   case PIPE_SHADER_CAP_TGSI_ANY_INOUT_DECL_RANGE:
      return 0;

   case PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS:
      STATIC_ASSERT(PIPE_MAX_SAMPLERS < 0x10000);
      return PIPE_MAX_SAMPLERS;

   case PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS:
      STATIC_ASSERT(PIPE_MAX_SHADER_SAMPLER_VIEWS < 0x10000);
      return PIPE_MAX_SHADER_SAMPLER_VIEWS;

   case PIPE_SHADER_CAP_SUPPORTED_IRS:
      return (1 << PIPE_SHADER_IR_NIR);

   case PIPE_SHADER_CAP_MAX_SHADER_BUFFERS:
      return allow_side_effects ? 16 : 0;

   case PIPE_SHADER_CAP_MAX_SHADER_IMAGES:
      return allow_side_effects ? PIPE_MAX_SHADER_IMAGES : 0;

   case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTERS:
   case PIPE_SHADER_CAP_MAX_HW_ATOMIC_COUNTER_BUFFERS:
      return 0;

   default:
      return 0;
   }

   return 0;
}

static uint32_t
pipe_to_pan_bind_flags(uint32_t pipe_bind_flags)
{
   static_assert(PIPE_BIND_DEPTH_STENCIL == PAN_BIND_DEPTH_STENCIL, "");
   static_assert(PIPE_BIND_RENDER_TARGET == PAN_BIND_RENDER_TARGET, "");
   static_assert(PIPE_BIND_SAMPLER_VIEW == PAN_BIND_SAMPLER_VIEW, "");
   static_assert(PIPE_BIND_VERTEX_BUFFER == PAN_BIND_VERTEX_BUFFER, "");

   return pipe_bind_flags & (PAN_BIND_DEPTH_STENCIL | PAN_BIND_RENDER_TARGET |
                             PAN_BIND_VERTEX_BUFFER | PAN_BIND_SAMPLER_VIEW);
}

/**
 * Query format support for creating a texture, drawing surface, etc.
 * \param format  the format to test
 * \param type  one of PIPE_TEXTURE, PIPE_SURFACE
 */
static bool
panfrost_is_format_supported(struct pipe_screen *screen,
                             enum pipe_format format,
                             enum pipe_texture_target target,
                             unsigned sample_count,
                             unsigned storage_sample_count, unsigned bind)
{
   struct panfrost_device *dev = pan_device(screen);

   /* MSAA 2x gets rounded up to 4x. MSAA 8x/16x only supported on v5+.
    * TODO: debug MSAA 8x/16x */

   switch (sample_count) {
   case 0:
   case 1:
   case 4:
      break;
   case 8:
   case 16:
      if (dev->debug & PAN_DBG_MSAA16)
         break;
      else
         return false;
   default:
      return false;
   }

   if (MAX2(sample_count, 1) != MAX2(storage_sample_count, 1))
      return false;

   /* Z16 causes dEQP failures on t720 */
   if (format == PIPE_FORMAT_Z16_UNORM && dev->arch <= 4)
      return false;

   /* Check we support the format with the given bind */

   unsigned pan_bind_flags = pipe_to_pan_bind_flags(bind);
   struct panfrost_format fmt = dev->formats[format];
   unsigned fmt_bind_flags = fmt.bind;

   /* Also check that compressed texture formats are supported on this
    * particular chip. They may not be depending on system integration
    * differences. */

   bool supported =
      !util_format_is_compressed(format) ||
      panfrost_supports_compressed_format(dev, fmt.texfeat_bit);

   if (!supported)
      return false;

   if (bind & PIPE_BIND_DEPTH_STENCIL) {
      /* On panfrost, S8_UINT is actually stored as X8S8_UINT, which
       * causes us headaches when we try to bind it as DEPTH_STENCIL;
       * the gallium driver doesn't handle this correctly. So reject
       * it for now.
       */
      switch (format) {
      case PIPE_FORMAT_S8_UINT:
         fmt_bind_flags &= ~PAN_BIND_DEPTH_STENCIL;
         break;
      default:
         /* no other special handling required yet */
         break;
      }
   }

   return MALI_EXTRACT_INDEX(fmt.hw) &&
      ((pan_bind_flags & ~fmt_bind_flags) == 0);
}

static void
panfrost_query_compression_rates(struct pipe_screen *screen,
                                 enum pipe_format format, int max,
                                 uint32_t *rates, int *count)
{
   struct panfrost_device *dev = pan_device(screen);

   if (!dev->has_afrc) {
      *count = 0;
      return;
   }

   *count = panfrost_afrc_query_rates(format, max, rates);
}

/* We always support linear and tiled operations, both external and internal.
 * We support AFBC for a subset of formats, and colourspace transform for a
 * subset of those. */

static void
panfrost_walk_dmabuf_modifiers(struct pipe_screen *screen,
                               enum pipe_format format, int max,
                               uint64_t *modifiers, unsigned int *external_only,
                               int *out_count, uint64_t test_modifier, bool allow_afrc)
{
   /* Query AFBC status */
   struct panfrost_device *dev = pan_device(screen);
   bool afbc =
      dev->has_afbc && panfrost_format_supports_afbc(dev->arch, format);
   bool ytr = panfrost_afbc_can_ytr(format);
   bool tiled_afbc = panfrost_afbc_can_tile(dev->arch);
   bool afrc = allow_afrc && dev->has_afrc && panfrost_format_supports_afrc(format);

   unsigned count = 0;

   for (unsigned i = 0; i < PAN_MODIFIER_COUNT; ++i) {
      if (drm_is_afbc(pan_best_modifiers[i])) {
         if (!afbc)
            continue;

         if ((pan_best_modifiers[i] & AFBC_FORMAT_MOD_SPLIT) &&
             !panfrost_afbc_can_split(dev->arch, format, pan_best_modifiers[i]))
            continue;

         if ((pan_best_modifiers[i] & AFBC_FORMAT_MOD_YTR) && !ytr)
            continue;

         if ((pan_best_modifiers[i] & AFBC_FORMAT_MOD_TILED) && !tiled_afbc)
            continue;
      }

      if (drm_is_afrc(pan_best_modifiers[i]) && !afrc)
         continue;

      if (test_modifier != DRM_FORMAT_MOD_INVALID &&
          test_modifier != pan_best_modifiers[i])
         continue;

      if (max > (int)count) {
         modifiers[count] = pan_best_modifiers[i];

         if (external_only)
            external_only[count] = false;
      }
      count++;
   }

   *out_count = count;
}

static void
panfrost_query_dmabuf_modifiers(struct pipe_screen *screen,
                                enum pipe_format format, int max,
                                uint64_t *modifiers,
                                unsigned int *external_only, int *out_count)
{
   panfrost_walk_dmabuf_modifiers(screen, format, max, modifiers, external_only,
                                  out_count, DRM_FORMAT_MOD_INVALID, true);
}

static void
panfrost_query_compression_modifiers(struct pipe_screen *screen,
                                     enum pipe_format format, uint32_t rate,
                                     int max, uint64_t *modifiers, int *count)
{
   struct panfrost_device *dev = pan_device(screen);

   if (rate == PIPE_COMPRESSION_FIXED_RATE_NONE)
      /* no compression requested, return all non-afrc formats */
      panfrost_walk_dmabuf_modifiers(screen, format, max, modifiers,
                                     NULL, /* external_only */
                                     count,
                                     DRM_FORMAT_MOD_INVALID,
                                     false /* disallow afrc */);
   else if (dev->has_afrc)
      *count = panfrost_afrc_get_modifiers(format, rate, max, modifiers);
   else
      *count = 0;  /* compression requested but not supported */
}

static bool
panfrost_is_dmabuf_modifier_supported(struct pipe_screen *screen,
                                      uint64_t modifier,
                                      enum pipe_format format,
                                      bool *external_only)
{
   uint64_t unused;
   unsigned int uint_extern_only = 0;
   int count;

   panfrost_walk_dmabuf_modifiers(screen, format, 1, &unused, &uint_extern_only,
                                  &count, modifier, true);

   if (external_only)
      *external_only = uint_extern_only ? true : false;

   return count > 0;
}

static int
panfrost_get_compute_param(struct pipe_screen *pscreen,
                           enum pipe_shader_ir ir_type,
                           enum pipe_compute_cap param, void *ret)
{
   struct panfrost_device *dev = pan_device(pscreen);
   const char *const ir = "panfrost";

#define RET(x)                                                                 \
   do {                                                                        \
      if (ret)                                                                 \
         memcpy(ret, x, sizeof(x));                                            \
      return sizeof(x);                                                        \
   } while (0)

   switch (param) {
   case PIPE_COMPUTE_CAP_ADDRESS_BITS:
      RET((uint32_t[]){64});

   case PIPE_COMPUTE_CAP_IR_TARGET:
      if (ret)
         sprintf(ret, "%s", ir);
      return strlen(ir) * sizeof(char);

   case PIPE_COMPUTE_CAP_GRID_DIMENSION:
      RET((uint64_t[]){3});

   case PIPE_COMPUTE_CAP_MAX_GRID_SIZE:
      RET(((uint64_t[]){65535, 65535, 65535}));

   case PIPE_COMPUTE_CAP_MAX_BLOCK_SIZE:
      /* Unpredictable behaviour at larger sizes. Mali-G52 advertises
       * 384x384x384.
       *
       * On Midgard, we don't allow more than 128 threads in each
       * direction to match PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK.
       * That still exceeds the minimum-maximum.
       */
      if (dev->arch >= 6)
         RET(((uint64_t[]){256, 256, 256}));
      else
         RET(((uint64_t[]){128, 128, 128}));

   case PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK:
      /* On Bifrost and newer, all GPUs can support at least 256 threads
       * regardless of register usage, so we report 256.
       *
       * On Midgard, with maximum register usage, the maximum
       * thread count is only 64. We would like to report 64 here, but
       * the GLES3.1 spec minimum is 128, so we report 128 and limit
       * the register allocation of affected compute kernels.
       */
      RET((uint64_t[]){dev->arch >= 6 ? 256 : 128});

   case PIPE_COMPUTE_CAP_MAX_GLOBAL_SIZE:
   case PIPE_COMPUTE_CAP_MAX_MEM_ALLOC_SIZE: {
      uint64_t total_ram;

      if (!os_get_total_physical_memory(&total_ram))
         return 0;

      /* We don't want to burn too much ram with the GPU. If the user has 4GiB
       * or less, we use at most half. If they have more than 4GiB, we use 3/4.
       */
      uint64_t available_ram;
      if (total_ram <= 4ull * 1024 * 1024 * 1024)
         available_ram = total_ram / 2;
      else
         available_ram = total_ram * 3 / 4;

      /* 48bit address space max, with the lower 32MB reserved. We clamp
       * things so it matches kmod VA range limitations.
       */
      uint64_t user_va_start =
         panfrost_clamp_to_usable_va_range(dev->kmod.dev, PAN_VA_USER_START);
      uint64_t user_va_end =
         panfrost_clamp_to_usable_va_range(dev->kmod.dev, PAN_VA_USER_END);

      /* We cannot support more than the VA limit */
      RET((uint64_t[]){MIN2(available_ram, user_va_end - user_va_start)});
   }

   case PIPE_COMPUTE_CAP_MAX_LOCAL_SIZE:
      RET((uint64_t[]){32768});

   case PIPE_COMPUTE_CAP_MAX_PRIVATE_SIZE:
   case PIPE_COMPUTE_CAP_MAX_INPUT_SIZE:
      RET((uint64_t[]){4096});

   case PIPE_COMPUTE_CAP_MAX_CLOCK_FREQUENCY:
      RET((uint32_t[]){800 /* MHz -- TODO */});

   case PIPE_COMPUTE_CAP_MAX_COMPUTE_UNITS:
      RET((uint32_t[]){dev->core_count});

   case PIPE_COMPUTE_CAP_IMAGES_SUPPORTED:
      RET((uint32_t[]){1});

   case PIPE_COMPUTE_CAP_SUBGROUP_SIZES:
      RET((uint32_t[]){pan_subgroup_size(dev->arch)});

   case PIPE_COMPUTE_CAP_MAX_SUBGROUPS:
      RET((uint32_t[]){0 /* TODO */});

   case PIPE_COMPUTE_CAP_MAX_VARIABLE_THREADS_PER_BLOCK:
      RET((uint64_t[]){1024}); // TODO
   }

   return 0;
}

static void
panfrost_init_screen_caps(struct panfrost_screen *screen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&screen->base.caps;

   u_init_pipe_screen_caps(&screen->base, 1);

   struct panfrost_device *dev = &screen->dev;

   /* Our GL 3.x implementation is WIP */
   bool is_gl3 = dev->debug & PAN_DBG_GL3;

   /* Native MRT is introduced with v5 */
   bool has_mrt = (dev->arch >= 5);

   caps->npot_textures = true;
   caps->mixed_color_depth_bits = true;
   caps->fragment_shader_texture_lod = true;
   caps->vertex_color_unclamped = true;
   caps->depth_clip_disable = true;
   caps->mixed_framebuffer_sizes = true;
   caps->frontend_noop = true;
   caps->sample_shading = true;
   caps->fragment_shader_derivatives = true;
   caps->framebuffer_no_attachment = true;
   caps->quads_follow_provoking_vertex_convention = true;
   caps->shader_pack_half_float = true;
   caps->has_const_bw = true;

   /* Removed in v9 (Valhall) */
   caps->depth_clip_disable_separate = dev->arch < 9;

   caps->max_render_targets =
   caps->fbfetch = has_mrt ? 8 : 1;
   caps->fbfetch_coherent = true;

   caps->max_dual_source_render_targets = 1;

   caps->occlusion_query = true;
   caps->primitive_restart = true;
   caps->primitive_restart_fixed_index = true;

   caps->anisotropic_filter =
      panfrost_device_gpu_rev(dev) >= dev->model->min_rev_anisotropic;

   /* Compile side is done for Bifrost, Midgard TODO. Needs some kernel
    * work to turn on, since CYCLE_COUNT_START needs to be issued. In
    * kbase, userspace requests this via BASE_JD_REQ_PERMON. There is not
    * yet way to request this with mainline TODO */
   caps->shader_clock = false;

   caps->vs_instanceid = true;
   caps->texture_multisample = true;
   caps->surface_sample_count = true;

   caps->sampler_view_target = true;
   caps->clip_halfz = true;
   caps->polygon_offset_clamp = true;
   caps->texture_swizzle = true;
   caps->texture_mirror_clamp_to_edge = true;
   caps->vertex_element_instance_divisor = true;
   caps->blend_equation_separate = true;
   caps->indep_blend_enable = true;
   caps->indep_blend_func = true;
   caps->generate_mipmap = true;
   caps->uma = true;
   caps->texture_float_linear = true;
   caps->texture_half_float_linear = true;
   caps->shader_array_components = true;
   caps->texture_buffer_objects = true;
   caps->packed_uniforms = true;
   caps->image_load_formatted = true;
   caps->cube_map_array = true;
   caps->compute = true;
   caps->int64 = true;

   caps->copy_between_compressed_and_plain_formats = true;

   caps->max_stream_output_buffers = PIPE_MAX_SO_BUFFERS;

   caps->max_stream_output_separate_components =
   caps->max_stream_output_interleaved_components = PIPE_MAX_SO_OUTPUTS;

   caps->stream_output_pause_resume = true;
   caps->stream_output_interleave_buffers = true;

   caps->max_texture_array_layers = 2048;

   caps->glsl_feature_level =
   caps->glsl_feature_level_compatibility = is_gl3 ? 330 : 140;
   caps->essl_feature_level = dev->arch >= 6 ? 320 : 310;

   caps->constant_buffer_offset_alignment = 16;

   /* v7 (only) restricts component orders with AFBC. To workaround, we
    * compose format swizzles with texture swizzles. pan_texture.c motsly
    * handles this but we need to fix up the border colour.
    */
   caps->texture_border_color_quirk = dev->arch == 7 || dev->arch >= 10 ?
      PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_FREEDRENO : 0;

   caps->max_texel_buffer_elements = PAN_MAX_TEXEL_BUFFER_ELEMENTS;

   /* Must be at least 64 for correct behaviour */
   caps->texture_buffer_offset_alignment = 64;

   caps->query_time_elapsed =
   caps->query_timestamp =
      dev->kmod.props.gpu_can_query_timestamp &&
      dev->kmod.props.timestamp_frequency != 0;

   caps->timer_resolution = pan_gpu_time_to_ns(dev, 1);

   /* The hardware requires element alignment for data conversion to work
    * as expected. If data conversion is not required, this restriction is
    * lifted on Midgard at a performance penalty. We conservatively
    * require element alignment for vertex buffers, using u_vbuf to
    * translate to match the hardware requirement.
    *
    * This is less heavy-handed than PIPE_VERTEX_INPUT_ALIGNMENT_4BYTE, which
    * would needlessly require alignment even for 8-bit formats.
    */
   caps->vertex_input_alignment = PIPE_VERTEX_INPUT_ALIGNMENT_ELEMENT;

   caps->max_texture_2d_size = 1 << (PAN_MAX_MIP_LEVELS - 1);

   caps->max_texture_3d_levels =
   caps->max_texture_cube_levels = PAN_MAX_MIP_LEVELS;

   /* pixel coord is in integer sysval on bifrost. */
   caps->fs_coord_pixel_center_integer = dev->arch >= 6;
   caps->fs_coord_pixel_center_half_integer = dev->arch < 6;

   /* Hardware is upper left */
   caps->fs_coord_origin_lower_left = false;

   caps->fs_coord_origin_upper_left = true;
   caps->tgsi_texcoord = true;

   /* We would prefer varyings on Midgard, but proper sysvals on Bifrost */
   caps->fs_face_is_integer_sysval =
   caps->fs_position_is_sysval =
   caps->fs_point_is_sysval = dev->arch >= 6;

   caps->seamless_cube_map = true;
   caps->seamless_cube_map_per_texture = true;

   caps->max_vertex_element_src_offset = 0xffff;

   caps->texture_transfer_modes = 0;

   caps->endianness = PIPE_ENDIAN_NATIVE;

   caps->max_texture_gather_components = 4;

   caps->min_texture_gather_offset = -8;

   caps->max_texture_gather_offset = 7;

   uint64_t system_memory;
   caps->video_memory = os_get_total_physical_memory(&system_memory) ?
      system_memory >> 20 : 0;

   caps->shader_stencil_export = true;
   caps->conditional_render = true;
   caps->conditional_render_inverted = true;

   caps->shader_buffer_offset_alignment = 4;

   caps->max_varyings = dev->arch >= 9 ? 16 : 32;

   /* Removed in v6 (Bifrost) */
   caps->gl_clamp =
   caps->texture_mirror_clamp =
   caps->alpha_test = dev->arch <= 5;

   /* Removed in v9 (Valhall). PRIMTIIVE_RESTART_FIXED_INDEX is of course
    * still supported as it is core GLES3.0 functionality
    */
   caps->emulate_nonfixed_primitive_restart = dev->arch >= 9;

   caps->flatshade = false;
   caps->two_sided_color = false;
   caps->clip_planes = 0;

   caps->packed_stream_output = false;

   caps->viewport_transform_lowered = true;
   caps->psiz_clamped = true;

   caps->nir_images_as_deref = false;

   caps->draw_indirect = true;

   caps->multi_draw_indirect = dev->arch >= 10;

   caps->start_instance =
   caps->draw_parameters = pan_is_bifrost(dev);

   /* Mali supports GLES and QUADS. Midgard and v6 Bifrost
    * support more */
   uint32_t modes = BITFIELD_MASK(MESA_PRIM_QUADS + 1);

   if (dev->arch <= 6) {
      modes |= BITFIELD_BIT(MESA_PRIM_QUAD_STRIP);
      modes |= BITFIELD_BIT(MESA_PRIM_POLYGON);
   }

   if (dev->arch >= 9) {
      /* Although Valhall is supposed to support quads, they
       * don't seem to work correctly. Disable to fix
       * arb-provoking-vertex-render.
       */
      modes &= ~BITFIELD_BIT(MESA_PRIM_QUADS);
   }

   caps->supported_prim_modes =
   caps->supported_prim_modes_with_restart = modes;

   caps->image_store_formatted = true;

   caps->native_fence_fd = true;

   caps->context_priority_mask =
      from_kmod_group_allow_priority_flags(
         dev->kmod.props.allowed_group_priorities_mask);

   caps->astc_decode_mode = dev->arch >= 9 && (dev->compressed_formats & (1 << 30));

   caps->min_line_width =
   caps->min_line_width_aa =
   caps->min_point_size =
   caps->min_point_size_aa = 1;

   caps->point_size_granularity =
   caps->line_width_granularity = 0.0625;

   caps->max_line_width =
   caps->max_line_width_aa =
   caps->max_point_size =
   caps->max_point_size_aa = 4095.9375;

   caps->max_texture_anisotropy = 16.0;

   caps->max_texture_lod_bias = 16.0; /* arbitrary */
}

static void
panfrost_destroy_screen(struct pipe_screen *pscreen)
{
   struct panfrost_device *dev = pan_device(pscreen);
   struct panfrost_screen *screen = pan_screen(pscreen);

   panfrost_resource_screen_destroy(pscreen);
   panfrost_pool_cleanup(&screen->mempools.bin);
   panfrost_pool_cleanup(&screen->mempools.desc);
   pan_blend_shader_cache_cleanup(&dev->blend_shaders);

   if (screen->vtbl.screen_destroy)
      screen->vtbl.screen_destroy(pscreen);

   if (dev->ro)
      dev->ro->destroy(dev->ro);
   panfrost_close_device(dev);

   disk_cache_destroy(screen->disk_cache);
   ralloc_free(pscreen);
}

static const void *
panfrost_screen_get_compiler_options(struct pipe_screen *pscreen,
                                     enum pipe_shader_ir ir,
                                     enum pipe_shader_type shader)
{
   return pan_screen(pscreen)->vtbl.get_compiler_options();
}

static struct disk_cache *
panfrost_get_disk_shader_cache(struct pipe_screen *pscreen)
{
   return pan_screen(pscreen)->disk_cache;
}

static int
panfrost_get_screen_fd(struct pipe_screen *pscreen)
{
   return panfrost_device_fd(pan_device(pscreen));
}

int
panfrost_get_driver_query_info(struct pipe_screen *pscreen, unsigned index,
                               struct pipe_driver_query_info *info)
{
   int num_queries = ARRAY_SIZE(panfrost_driver_query_list);

   if (!info)
      return num_queries;

   if (index >= num_queries)
      return 0;

   *info = panfrost_driver_query_list[index];

   return 1;
}

static uint64_t
panfrost_get_timestamp(struct pipe_screen *pscreen)
{
   struct panfrost_device *dev = pan_device(pscreen);

   return pan_gpu_time_to_ns(dev, pan_kmod_query_timestamp(dev->kmod.dev));
}

struct pipe_screen *
panfrost_create_screen(int fd, const struct pipe_screen_config *config,
                       struct renderonly *ro)
{
   /* Create the screen */
   struct panfrost_screen *screen = rzalloc(NULL, struct panfrost_screen);

   if (!screen)
      return NULL;

   struct panfrost_device *dev = pan_device(&screen->base);

   driParseConfigFiles(config->options, config->options_info, 0,
                       "panfrost", NULL, NULL, NULL, 0, NULL, 0);

   /* Debug must be set first for pandecode to work correctly */
   dev->debug =
      debug_get_flags_option("PAN_MESA_DEBUG", panfrost_debug_options, 0);
   screen->max_afbc_packing_ratio = debug_get_num_option(
      "PAN_MAX_AFBC_PACKING_RATIO", DEFAULT_MAX_AFBC_PACKING_RATIO);

   if (panfrost_open_device(screen, fd, dev)) {
      ralloc_free(screen);
      return NULL;
   }

   if (dev->debug & PAN_DBG_NO_AFBC)
      dev->has_afbc = false;

   /* Bail early on unsupported hardware */
   if (dev->model == NULL) {
      debug_printf("panfrost: Unsupported model %X",
                   panfrost_device_gpu_id(dev));
      panfrost_destroy_screen(&(screen->base));
      return NULL;
   }

   screen->force_afbc_packing = dev->debug & PAN_DBG_FORCE_PACK;
   if (!screen->force_afbc_packing)
      screen->force_afbc_packing = driQueryOptionb(config->options,
                                                   "pan_force_afbc_packing");

   const char *option = debug_get_option("PAN_AFRC_RATE", NULL);
   if (!option) {
      screen->force_afrc_rate = -1;
   } else if (strcmp(option, "default") == 0) {
      screen->force_afrc_rate = PIPE_COMPRESSION_FIXED_RATE_DEFAULT;
   } else {
      int64_t rate =
         debug_parse_num_option(option, PIPE_COMPRESSION_FIXED_RATE_NONE);
      screen->force_afrc_rate = rate;
   }

   screen->csf_tiler_heap.chunk_size = driQueryOptioni(config->options,
                                                       "pan_csf_chunk_size");
   screen->csf_tiler_heap.initial_chunks = driQueryOptioni(config->options,
                                                           "pan_csf_initial_chunks");
   screen->csf_tiler_heap.max_chunks = driQueryOptioni(config->options,
                                                       "pan_csf_max_chunks");

   dev->ro = ro;

   screen->base.destroy = panfrost_destroy_screen;

   screen->base.get_screen_fd = panfrost_get_screen_fd;
   screen->base.get_name = panfrost_get_name;
   screen->base.get_vendor = panfrost_get_vendor;
   screen->base.get_device_vendor = panfrost_get_device_vendor;
   screen->base.get_driver_query_info = panfrost_get_driver_query_info;
   screen->base.get_shader_param = panfrost_get_shader_param;
   screen->base.get_compute_param = panfrost_get_compute_param;
   screen->base.get_timestamp = panfrost_get_timestamp;
   screen->base.is_format_supported = panfrost_is_format_supported;
   screen->base.query_dmabuf_modifiers = panfrost_query_dmabuf_modifiers;
   screen->base.is_dmabuf_modifier_supported =
      panfrost_is_dmabuf_modifier_supported;
   screen->base.context_create = panfrost_create_context;
   screen->base.get_compiler_options = panfrost_screen_get_compiler_options;
   screen->base.get_disk_shader_cache = panfrost_get_disk_shader_cache;
   screen->base.fence_reference = panfrost_fence_reference;
   screen->base.fence_finish = panfrost_fence_finish;
   screen->base.fence_get_fd = panfrost_fence_get_fd;
   screen->base.set_damage_region = panfrost_resource_set_damage_region;
   screen->base.query_compression_rates = panfrost_query_compression_rates;
   screen->base.query_compression_modifiers =
      panfrost_query_compression_modifiers;

   panfrost_resource_screen_init(&screen->base);
   pan_blend_shader_cache_init(&dev->blend_shaders,
                               panfrost_device_gpu_id(dev));

   panfrost_init_screen_caps(screen);

   panfrost_disk_cache_init(screen);

   if (panfrost_pool_init(&screen->mempools.bin, NULL, dev, PAN_BO_EXECUTE,
                          4096, "Preload shaders", false, true) ||
       panfrost_pool_init(&screen->mempools.desc, NULL, dev, 0, 65536,
                          "Preload RSDs", false, true)) {
      panfrost_destroy_screen(&(screen->base));
      return NULL;
   }

   if (dev->arch == 4)
      panfrost_cmdstream_screen_init_v4(screen);
   else if (dev->arch == 5)
      panfrost_cmdstream_screen_init_v5(screen);
   else if (dev->arch == 6)
      panfrost_cmdstream_screen_init_v6(screen);
   else if (dev->arch == 7)
      panfrost_cmdstream_screen_init_v7(screen);
   else if (dev->arch == 9)
      panfrost_cmdstream_screen_init_v9(screen);
   else if (dev->arch == 10)
      panfrost_cmdstream_screen_init_v10(screen);
   else
      unreachable("Unhandled architecture major");

   return &screen->base;
}
