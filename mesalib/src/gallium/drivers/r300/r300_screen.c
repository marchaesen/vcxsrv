/*
 * Copyright 2008 Corbin Simpson <MostAwesomeDude@gmail.com>
 * Copyright 2010 Marek Olšák <maraeo@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "util/format/u_format.h"
#include "util/format/u_format_s3tc.h"
#include "util/u_screen.h"
#include "util/u_memory.h"
#include "util/hex.h"
#include "util/os_time.h"
#include "util/xmlconfig.h"
#include "vl/vl_decoder.h"
#include "vl/vl_video_buffer.h"

#include "r300_context.h"
#include "r300_texture.h"
#include "r300_screen_buffer.h"
#include "r300_state_inlines.h"
#include "r300_public.h"
#include "compiler/r300_nir.h"

#include "draw/draw_context.h"

/* Return the identifier behind whom the brave coders responsible for this
 * amalgamation of code, sweat, and duct tape, routinely obscure their names.
 *
 * ...I should have just put "Corbin Simpson", but I'm not that cool.
 *
 * (Or egotistical. Yet.) */
static const char* r300_get_vendor(struct pipe_screen* pscreen)
{
    return "Mesa";
}

static const char* r300_get_device_vendor(struct pipe_screen* pscreen)
{
    return "ATI";
}

static const char* chip_families[] = {
    "unknown",
    "ATI R300",
    "ATI R350",
    "ATI RV350",
    "ATI RV370",
    "ATI RV380",
    "ATI RS400",
    "ATI RC410",
    "ATI RS480",
    "ATI R420",
    "ATI R423",
    "ATI R430",
    "ATI R480",
    "ATI R481",
    "ATI RV410",
    "ATI RS600",
    "ATI RS690",
    "ATI RS740",
    "ATI RV515",
    "ATI R520",
    "ATI RV530",
    "ATI R580",
    "ATI RV560",
    "ATI RV570"
};

static const char* r300_get_family_name(struct r300_screen* r300screen)
{
    return chip_families[r300screen->caps.family];
}

static const char* r300_get_name(struct pipe_screen* pscreen)
{
    struct r300_screen* r300screen = r300_screen(pscreen);

    return r300_get_family_name(r300screen);
}

static void r300_disk_cache_create(struct r300_screen* r300screen)
{
    struct mesa_sha1 ctx;
    unsigned char sha1[20];
    char cache_id[20 * 2 + 1];

    _mesa_sha1_init(&ctx);
    if (!disk_cache_get_function_identifier(r300_disk_cache_create,
                                            &ctx))
        return;

    _mesa_sha1_final(&ctx, sha1);
    mesa_bytes_to_hex(cache_id, sha1, 20);

    r300screen->disk_shader_cache =
                    disk_cache_create(r300_get_family_name(r300screen),
                                      cache_id,
                                      r300screen->debug);
}

static struct disk_cache* r300_get_disk_shader_cache(struct pipe_screen* pscreen)
{
	struct r300_screen* r300screen = r300_screen(pscreen);
	return r300screen->disk_shader_cache;
}

static int r300_get_video_param(struct pipe_screen *screen,
				enum pipe_video_profile profile,
				enum pipe_video_entrypoint entrypoint,
				enum pipe_video_cap param)
{
   switch (param) {
      case PIPE_VIDEO_CAP_SUPPORTED:
         return vl_profile_supported(screen, profile, entrypoint);
      case PIPE_VIDEO_CAP_NPOT_TEXTURES:
         return 0;
      case PIPE_VIDEO_CAP_MAX_WIDTH:
      case PIPE_VIDEO_CAP_MAX_HEIGHT:
         return vl_video_buffer_max_size(screen);
      case PIPE_VIDEO_CAP_PREFERRED_FORMAT:
         return PIPE_FORMAT_NV12;
      case PIPE_VIDEO_CAP_PREFERS_INTERLACED:
         return false;
      case PIPE_VIDEO_CAP_SUPPORTS_INTERLACED:
         return false;
      case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
         return true;
      case PIPE_VIDEO_CAP_MAX_LEVEL:
         return vl_level_supported(screen, profile);
      default:
         return 0;
   }
}

#define COMMON_NIR_OPTIONS                    \
   .fdot_replicates = true,                   \
   .fuse_ffma32 = true,                       \
   .fuse_ffma64 = true,                       \
   .lower_bitops = true,                      \
   .lower_extract_byte = true,                \
   .lower_extract_word = true,                \
   .lower_fceil = true,                       \
   .lower_fdiv = true,                        \
   .lower_fdph = true,                        \
   .lower_ffloor = true,                      \
   .lower_flrp32 = true,                      \
   .lower_flrp64 = true,                      \
   .lower_fmod = true,                        \
   .lower_fsign = true,                       \
   .lower_fsqrt = true,                       \
   .lower_ftrunc = true,                      \
   .lower_insert_byte = true,                 \
   .lower_insert_word = true,                 \
   .lower_uniforms_to_ubo = true,             \
   .lower_vector_cmp = true,                  \
   .no_integers = true

static const nir_shader_compiler_options r500_vs_compiler_options = {
   COMMON_NIR_OPTIONS,
   .has_fused_comp_and_csel = true,

   /* Have HW loops support and 1024 max instr count, but don't unroll *too*
    * hard.
    */
   .max_unroll_iterations = 29,
};

static const nir_shader_compiler_options r500_fs_compiler_options = {
   COMMON_NIR_OPTIONS,
   .lower_fpow = true, /* POW is only in the VS */
   .has_fused_comp_and_csel = true,

   /* Have HW loops support and 512 max instr count, but don't unroll *too*
    * hard.
    */
   .max_unroll_iterations = 32,
};

static const nir_shader_compiler_options r300_vs_compiler_options = {
   COMMON_NIR_OPTIONS,
   .lower_fsat = true, /* No fsat in pre-r500 VS */
   .lower_sincos = true,

   /* Note: has HW loops support, but only 256 ALU instructions. */
   .max_unroll_iterations = 32,
};

static const nir_shader_compiler_options r400_vs_compiler_options = {
   COMMON_NIR_OPTIONS,
   .lower_fsat = true, /* No fsat in pre-r500 VS */

   /* Note: has HW loops support, but only 256 ALU instructions. */
   .max_unroll_iterations = 32,
};

static const nir_shader_compiler_options r300_fs_compiler_options = {
   COMMON_NIR_OPTIONS,
   .lower_fpow = true, /* POW is only in the VS */
   .lower_sincos = true,
   .has_fused_comp_and_csel = true,

    /* No HW loops support, so set it equal to ALU instr max */
   .max_unroll_iterations = 64,
};

static const nir_shader_compiler_options gallivm_compiler_options = {
   COMMON_NIR_OPTIONS,
   .has_fused_comp_and_csel = true,
   .max_unroll_iterations = 32,

   .support_indirect_inputs = (uint8_t)BITFIELD_MASK(PIPE_SHADER_TYPES),
   .support_indirect_outputs = (uint8_t)BITFIELD_MASK(PIPE_SHADER_TYPES),
};

static const void *
r300_get_compiler_options(struct pipe_screen *pscreen,
                          enum pipe_shader_ir ir,
                          enum pipe_shader_type shader)
{
   struct r300_screen* r300screen = r300_screen(pscreen);

   assert(ir == PIPE_SHADER_IR_NIR);

   if (shader == PIPE_SHADER_VERTEX && !r300screen->caps.has_tcl) {
      return &gallivm_compiler_options;
   } else if (r300screen->caps.is_r500) {
      if (shader == PIPE_SHADER_VERTEX)
         return &r500_vs_compiler_options;
       else
         return &r500_fs_compiler_options;
   } else {
      if (shader == PIPE_SHADER_VERTEX) {
         if (r300screen->caps.is_r400)
            return &r400_vs_compiler_options;

         return &r300_vs_compiler_options;
      } else {
         return &r300_fs_compiler_options;
      }
   }
}

/**
 * Whether the format matches:
 *   PIPE_FORMAT_?10?10?10?2_UNORM
 */
static inline bool
util_format_is_rgba1010102_variant(const struct util_format_description *desc)
{
   static const unsigned size[4] = {10, 10, 10, 2};
   unsigned chan;

   if (desc->block.width != 1 ||
       desc->block.height != 1 ||
       desc->block.bits != 32)
      return false;

   for (chan = 0; chan < 4; ++chan) {
      if(desc->channel[chan].type != UTIL_FORMAT_TYPE_UNSIGNED &&
         desc->channel[chan].type != UTIL_FORMAT_TYPE_VOID)
         return false;
      if (desc->channel[chan].size != size[chan])
         return false;
   }

   return true;
}

static bool r300_is_blending_supported(struct r300_screen *rscreen,
                                       enum pipe_format format)
{
    int c;
    const struct util_format_description *desc =
        util_format_description(format);

    if (desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
        return false;

    c = util_format_get_first_non_void_channel(format);

    /* RGBA16F */
    if (rscreen->caps.is_r500 &&
        desc->nr_channels == 4 &&
        desc->channel[c].size == 16 &&
        desc->channel[c].type == UTIL_FORMAT_TYPE_FLOAT)
        return true;

    if (desc->channel[c].normalized &&
        desc->channel[c].type == UTIL_FORMAT_TYPE_UNSIGNED &&
        desc->channel[c].size >= 4 &&
        desc->channel[c].size <= 10) {
        /* RGB10_A2, RGBA8, RGB5_A1, RGBA4, RGB565 */
        if (desc->nr_channels >= 3)
            return true;

        if (format == PIPE_FORMAT_R8G8_UNORM)
            return true;

        /* R8, I8, L8, A8 */
        if (desc->nr_channels == 1)
            return true;
    }

    return false;
}

static bool r300_is_format_supported(struct pipe_screen* screen,
                                     enum pipe_format format,
                                     enum pipe_texture_target target,
                                     unsigned sample_count,
                                     unsigned storage_sample_count,
                                     unsigned usage)
{
    uint32_t retval = 0;
    bool is_r500 = r300_screen(screen)->caps.is_r500;
    bool is_r400 = r300_screen(screen)->caps.is_r400;
    bool is_color2101010 = format == PIPE_FORMAT_R10G10B10A2_UNORM ||
                           format == PIPE_FORMAT_R10G10B10X2_SNORM ||
                           format == PIPE_FORMAT_B10G10R10A2_UNORM ||
                           format == PIPE_FORMAT_B10G10R10X2_UNORM ||
                           format == PIPE_FORMAT_R10SG10SB10SA2U_NORM;
    bool is_ati1n = format == PIPE_FORMAT_RGTC1_UNORM ||
                    format == PIPE_FORMAT_RGTC1_SNORM ||
                    format == PIPE_FORMAT_LATC1_UNORM ||
                    format == PIPE_FORMAT_LATC1_SNORM;
    bool is_ati2n = format == PIPE_FORMAT_RGTC2_UNORM ||
                    format == PIPE_FORMAT_RGTC2_SNORM ||
                    format == PIPE_FORMAT_LATC2_UNORM ||
                    format == PIPE_FORMAT_LATC2_SNORM;
    bool is_half_float = format == PIPE_FORMAT_R16_FLOAT ||
                         format == PIPE_FORMAT_R16G16_FLOAT ||
                         format == PIPE_FORMAT_R16G16B16_FLOAT ||
                         format == PIPE_FORMAT_R16G16B16A16_FLOAT ||
                         format == PIPE_FORMAT_R16G16B16X16_FLOAT;
    const struct util_format_description *desc;

    if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
        return false;

    /* Check multisampling support. */
    switch (sample_count) {
        case 0:
        case 1:
            break;
        case 2:
        case 4:
        case 6:
            /* No texturing and scanout. */
            if (usage & (PIPE_BIND_SAMPLER_VIEW |
                         PIPE_BIND_DISPLAY_TARGET |
                         PIPE_BIND_SCANOUT)) {
                return false;
            }

            desc = util_format_description(format);

            if (is_r500) {
                /* Only allow depth/stencil, RGBA8, RGBA1010102, RGBA16F. */
                if (!util_format_is_depth_or_stencil(format) &&
                    !util_format_is_rgba8_variant(desc) &&
                    !util_format_is_rgba1010102_variant(desc) &&
                    format != PIPE_FORMAT_R16G16B16A16_FLOAT &&
                    format != PIPE_FORMAT_R16G16B16X16_FLOAT) {
                    return false;
                }
            } else {
                /* Only allow depth/stencil, RGBA8. */
                if (!util_format_is_depth_or_stencil(format) &&
                    !util_format_is_rgba8_variant(desc)) {
                    return false;
                }
            }
            break;
        default:
            return false;
    }

    /* Check sampler format support. */
    if ((usage & PIPE_BIND_SAMPLER_VIEW) &&
        /* these two are broken for an unknown reason */
        format != PIPE_FORMAT_R8G8B8X8_SNORM &&
        format != PIPE_FORMAT_R16G16B16X16_SNORM &&
        /* ATI1N is r5xx-only. */
        (is_r500 || !is_ati1n) &&
        /* ATI2N is supported on r4xx-r5xx. However state tracker can't handle
	 * fallbacks for ATI1N only, so if we enable ATI2N, we will crash for ATI1N.
	 * Therefore disable both on r400 for now. Additionally, some online source
	 * claim r300 can also do ATI2N.
	 */
        (is_r500 || !is_ati2n) &&
        r300_is_sampler_format_supported(format)) {
        retval |= PIPE_BIND_SAMPLER_VIEW;
    }

    /* Check colorbuffer format support. */
    if ((usage & (PIPE_BIND_RENDER_TARGET |
                  PIPE_BIND_DISPLAY_TARGET |
                  PIPE_BIND_SCANOUT |
                  PIPE_BIND_SHARED |
                  PIPE_BIND_BLENDABLE)) &&
        /* 2101010 cannot be rendered to on non-r5xx. */
        (!is_color2101010 || is_r500) &&
        r300_is_colorbuffer_format_supported(format)) {
        retval |= usage &
            (PIPE_BIND_RENDER_TARGET |
             PIPE_BIND_DISPLAY_TARGET |
             PIPE_BIND_SCANOUT |
             PIPE_BIND_SHARED);

        if (r300_is_blending_supported(r300_screen(screen), format)) {
            retval |= usage & PIPE_BIND_BLENDABLE;
        }
    }

    /* Check depth-stencil format support. */
    if (usage & PIPE_BIND_DEPTH_STENCIL &&
        r300_is_zs_format_supported(format)) {
        retval |= PIPE_BIND_DEPTH_STENCIL;
    }

    /* Check vertex buffer format support. */
    if (usage & PIPE_BIND_VERTEX_BUFFER) {
        if (r300_screen(screen)->caps.has_tcl) {
            /* Half float is supported on >= R400. */
            if ((is_r400 || is_r500 || !is_half_float) &&
                r300_translate_vertex_data_type(format) != R300_INVALID_FORMAT) {
                retval |= PIPE_BIND_VERTEX_BUFFER;
            }
        } else {
            /* SW TCL */
            if (!util_format_is_pure_integer(format)) {
                retval |= PIPE_BIND_VERTEX_BUFFER;
            }
        }
    }

    if (usage & PIPE_BIND_INDEX_BUFFER) {
       if (format == PIPE_FORMAT_R8_UINT ||
           format == PIPE_FORMAT_R16_UINT ||
           format == PIPE_FORMAT_R32_UINT)
          retval |= PIPE_BIND_INDEX_BUFFER;
    }

    return retval == usage;
}

static void r300_init_shader_caps(struct r300_screen* r300screen)
{
   bool is_r400 = r300screen->caps.is_r400;
   bool is_r500 = r300screen->caps.is_r500;

   struct pipe_shader_caps *caps =
      (struct pipe_shader_caps *)&r300screen->screen.shader_caps[PIPE_SHADER_VERTEX];

   if (r300screen->caps.has_tcl) {
      caps->max_instructions =
      caps->max_alu_instructions = is_r500 ? 1024 : 256;
      /* For loops; not sure about conditionals. */
      caps->max_control_flow_depth = is_r500 ? 4 : 0;
      caps->max_inputs = 16;
      caps->max_outputs = 10;
      caps->max_const_buffer0_size = 256 * sizeof(float[4]);
      caps->max_const_buffers = 1;
      caps->max_temps = 32;
      caps->indirect_const_addr = true;
      caps->tgsi_any_inout_decl_range = true;
   } else {
      draw_init_shader_caps(caps);

      caps->max_texture_samplers = 0;
      caps->max_sampler_views = 0;
      caps->subroutines = false;
      caps->max_shader_buffers = 0;
      caps->max_shader_images = 0;
      /* mesa/st requires that this cap is the same across stages, and the FS
       * can't do ints.
       */
      caps->integers = false;
      /* Even if gallivm NIR can do this, we call nir_to_tgsi manually and
       * TGSI can't.
       */
      caps->int16 = false;
      caps->fp16 = false;
      caps->fp16_derivatives = false;
      caps->fp16_const_buffers = false;
      /* While draw could normally handle this for the VS, the NIR lowering
       * to regs can't handle our non-native-integers, so we have to lower to
       * if ladders.
       */
      caps->indirect_temp_addr = false;
   }
   caps->supported_irs = (1 << PIPE_SHADER_IR_NIR) | (1 << PIPE_SHADER_IR_TGSI);

   caps = (struct pipe_shader_caps *)&r300screen->screen.shader_caps[PIPE_SHADER_FRAGMENT];

   caps->max_instructions = is_r500 || is_r400 ? 512 : 96;
   caps->max_alu_instructions = is_r500 || is_r400 ? 512 : 64;
   caps->max_tex_instructions = is_r500 || is_r400 ? 512 : 32;
   caps->max_tex_indirections = is_r500 ? 511 : 4;
   caps->max_control_flow_depth = is_r500 ? 64 : 0; /* Actually unlimited on r500. */
   /* 2 colors + 8 texcoords are always supported
    * (minus fog and wpos).
    *
    * R500 has the ability to turn 3rd and 4th color into
    * additional texcoords but there is no two-sided color
    * selection then. However the facing bit can be used instead. */
   caps->max_inputs = 10;
   caps->max_outputs = 4;
   caps->max_const_buffer0_size = (is_r500 ? 256 : 32) * sizeof(float[4]);
   caps->max_const_buffers = 1;
   caps->tgsi_any_inout_decl_range = true;
   caps->max_temps = is_r500 ? 128 : is_r400 ? 64 : 32;
   caps->max_texture_samplers =
   caps->max_sampler_views = r300screen->caps.num_tex_units;
   caps->supported_irs = (1 << PIPE_SHADER_IR_NIR) | (1 << PIPE_SHADER_IR_TGSI);
}

static void r300_init_screen_caps(struct r300_screen* r300screen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&r300screen->screen.caps;

   u_init_pipe_screen_caps(&r300screen->screen, 1);

   bool is_r500 = r300screen->caps.is_r500;

   /* Supported features (boolean caps). */
   caps->npot_textures = true;
   caps->mixed_framebuffer_sizes = true;
   caps->mixed_color_depth_bits = true;
   caps->anisotropic_filter = true;
   caps->occlusion_query = true;
   caps->texture_mirror_clamp = true;
   caps->texture_mirror_clamp_to_edge = true;
   caps->blend_equation_separate = true;
   caps->vertex_element_instance_divisor = true;
   caps->fs_coord_origin_upper_left = true;
   caps->fs_coord_pixel_center_half_integer = true;
   caps->conditional_render = true;
   caps->texture_barrier = true;
   caps->tgsi_can_compact_constants = true;
   caps->clip_halfz = true;
   caps->allow_mapped_buffers_during_execution = true;
   caps->legacy_math_rules = true;
   caps->tgsi_texcoord = true;
   caps->call_finalize_nir_in_linker = true;

   caps->texture_transfer_modes = PIPE_TEXTURE_TRANSFER_BLIT;

   caps->min_map_buffer_alignment = R300_BUFFER_ALIGNMENT;

   caps->constant_buffer_offset_alignment = 16;

   caps->glsl_feature_level =
   caps->glsl_feature_level_compatibility = 120;

   /* r300 cannot do swizzling of compressed textures. Supported otherwise. */
   caps->texture_swizzle = r300screen->caps.dxtc_swizzle;

   /* We don't support color clamping on r500, so that we can use color
    * interpolators for generic varyings. */
   caps->vertex_color_clamped = !is_r500;

   /* Supported on r500 only. */
   caps->vertex_color_unclamped =
   caps->mixed_colorbuffer_formats =
   caps->fragment_shader_texture_lod =
   caps->fragment_shader_derivatives = is_r500;

   caps->shareable_shaders = false;

   caps->max_gs_invocations = 32;
   caps->max_shader_buffer_size = 1 << 27;

   /* SWTCL-only features. */
   caps->primitive_restart =
   caps->primitive_restart_fixed_index =
   caps->user_vertex_buffers =
   caps->vs_window_space_position = !r300screen->caps.has_tcl;

   /* HWTCL-only features / limitations. */
   caps->vertex_input_alignment =
      r300screen->caps.has_tcl ? PIPE_VERTEX_INPUT_ALIGNMENT_4BYTE : PIPE_VERTEX_INPUT_ALIGNMENT_NONE;

   /* Texturing. */
   caps->max_texture_2d_size = is_r500 ? 4096 : 2048;
   caps->max_texture_3d_levels =
   caps->max_texture_cube_levels = is_r500 ? 13 : 12; /* 13 == 4096, 12 == 2048 */

   /* Render targets. */
   caps->max_render_targets = 4;
   caps->endianness = PIPE_ENDIAN_LITTLE;

   caps->max_viewports = 1;

   caps->max_vertex_attrib_stride = 2048;

   caps->max_varyings = 10;

   caps->prefer_imm_arrays_as_constbuf = false;

   caps->vendor_id = 0x1002;
   caps->device_id = r300screen->info.pci_id;
   caps->video_memory = r300screen->info.vram_size_kb >> 10;
   caps->uma = false;
   caps->pci_group = r300screen->info.pci.domain;
   caps->pci_bus = r300screen->info.pci.bus;
   caps->pci_device = r300screen->info.pci.dev;
   caps->pci_function = r300screen->info.pci.func;

   caps->min_line_width =
   caps->min_line_width_aa =
   caps->min_point_size =
   caps->min_point_size_aa = 1;
   caps->point_size_granularity =
   caps->line_width_granularity = 0.1;
   caps->max_line_width =
   caps->max_line_width_aa =
   caps->max_point_size =
   caps->max_point_size_aa =
      /* The maximum dimensions of the colorbuffer are our practical
       * rendering limits. 2048 pixels should be enough for anybody. */
      r300screen->caps.is_r500 ? 4096.0f :
      (r300screen->caps.is_r400 ? 4021.0f : 2560.0f);
   caps->max_texture_anisotropy = 16.0f;
   caps->max_texture_lod_bias = 16.0f;
}

static void r300_destroy_screen(struct pipe_screen* pscreen)
{
    struct r300_screen* r300screen = r300_screen(pscreen);
    struct radeon_winsys *rws = radeon_winsys(pscreen);

    if (rws && !rws->unref(rws))
      return;

    mtx_destroy(&r300screen->cmask_mutex);
    slab_destroy_parent(&r300screen->pool_transfers);

    disk_cache_destroy(r300screen->disk_shader_cache);

    if (rws)
      rws->destroy(rws);

    FREE(r300screen);
}

static void r300_fence_reference(struct pipe_screen *screen,
                                 struct pipe_fence_handle **ptr,
                                 struct pipe_fence_handle *fence)
{
    struct radeon_winsys *rws = r300_screen(screen)->rws;

    rws->fence_reference(rws, ptr, fence);
}

static bool r300_fence_finish(struct pipe_screen *screen,
                              struct pipe_context *ctx,
                              struct pipe_fence_handle *fence,
                              uint64_t timeout)
{
    struct radeon_winsys *rws = r300_screen(screen)->rws;

    return rws->fence_wait(rws, fence, timeout);
}

static int r300_screen_get_fd(struct pipe_screen *screen)
{
    struct radeon_winsys *rws = r300_screen(screen)->rws;

    return rws->get_fd(rws);
}

struct pipe_screen* r300_screen_create(struct radeon_winsys *rws,
                                       const struct pipe_screen_config *config)
{
    struct r300_screen *r300screen = CALLOC_STRUCT(r300_screen);

    if (!r300screen) {
        FREE(r300screen);
        return NULL;
    }

    rws->query_info(rws, &r300screen->info);

    r300_init_debug(r300screen);
    r300_parse_chipset(r300screen->info.pci_id, &r300screen->caps);

    driParseConfigFiles(config->options, config->options_info, 0, "r300", NULL,
                        NULL, NULL, 0, NULL, 0);

#define OPT_BOOL(name, dflt, description)                                                          \
    r300screen->options.name = driQueryOptionb(config->options, "r300_" #name);
#include "r300_debug_options.h"

    if (SCREEN_DBG_ON(r300screen, DBG_NO_ZMASK) ||
        r300screen->options.nozmask)
        r300screen->caps.zmask_ram = 0;
    if (SCREEN_DBG_ON(r300screen, DBG_NO_HIZ) ||
        r300screen->options.nohiz)
        r300screen->caps.hiz_ram = 0;
    if (SCREEN_DBG_ON(r300screen, DBG_NO_TCL))
        r300screen->caps.has_tcl = false;

    if (SCREEN_DBG_ON(r300screen, DBG_IEEEMATH))
        r300screen->options.ieeemath = true;
    if (SCREEN_DBG_ON(r300screen, DBG_FFMATH))
        r300screen->options.ffmath = true;

    r300screen->rws = rws;
    r300screen->screen.destroy = r300_destroy_screen;
    r300screen->screen.get_name = r300_get_name;
    r300screen->screen.get_vendor = r300_get_vendor;
    r300screen->screen.get_compiler_options = r300_get_compiler_options;
    r300screen->screen.finalize_nir = r300_finalize_nir;
    r300screen->screen.get_device_vendor = r300_get_device_vendor;
    r300screen->screen.get_disk_shader_cache = r300_get_disk_shader_cache;
    r300screen->screen.get_screen_fd = r300_screen_get_fd;
    r300screen->screen.get_video_param = r300_get_video_param;
    r300screen->screen.is_format_supported = r300_is_format_supported;
    r300screen->screen.is_video_format_supported = vl_video_buffer_is_format_supported;
    r300screen->screen.context_create = r300_create_context;
    r300screen->screen.fence_reference = r300_fence_reference;
    r300screen->screen.fence_finish = r300_fence_finish;

    r300_init_screen_resource_functions(r300screen);

    r300_init_shader_caps(r300screen);
    r300_init_screen_caps(r300screen);

    r300_disk_cache_create(r300screen);

    slab_create_parent(&r300screen->pool_transfers, sizeof(struct pipe_transfer), 64);

    (void) mtx_init(&r300screen->cmask_mutex, mtx_plain);

    return &r300screen->screen;
}
