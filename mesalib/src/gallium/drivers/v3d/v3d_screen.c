/*
 * Copyright © 2014-2017 Broadcom
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <sys/sysinfo.h>

#include "common/v3d_device_info.h"
#include "common/v3d_limits.h"
#include "util/os_misc.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/perf/cpu_trace.h"

#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/format/u_format.h"
#include "util/u_hash_table.h"
#include "util/u_screen.h"
#include "util/u_transfer_helper.h"
#include "util/ralloc.h"
#include "util/xmlconfig.h"

#include <xf86drm.h>
#include "v3d_screen.h"
#include "v3d_context.h"
#include "v3d_resource.h"
#include "compiler/v3d_compiler.h"
#include "drm-uapi/drm_fourcc.h"

const char *
v3d_screen_get_name(struct pipe_screen *pscreen)
{
        struct v3d_screen *screen = v3d_screen(pscreen);

        if (!screen->name) {
                screen->name = ralloc_asprintf(screen,
                                               "V3D %d.%d.%d.%d",
                                               screen->devinfo.ver / 10,
                                               screen->devinfo.ver % 10,
                                               screen->devinfo.rev,
                                               screen->devinfo.compat_rev);
        }

        return screen->name;
}

static const char *
v3d_screen_get_vendor(struct pipe_screen *pscreen)
{
        return "Broadcom";
}

static void
v3d_screen_destroy(struct pipe_screen *pscreen)
{
        struct v3d_screen *screen = v3d_screen(pscreen);

        v3d_perfcntrs_fini(screen->perfcnt);
        screen->perfcnt = NULL;

        _mesa_hash_table_destroy(screen->bo_handles, NULL);
        v3d_bufmgr_destroy(pscreen);
        slab_destroy_parent(&screen->transfer_pool);
        if (screen->ro)
                screen->ro->destroy(screen->ro);

#if USE_V3D_SIMULATOR
        v3d_simulator_destroy(screen->sim_file);
#endif

        v3d_compiler_free(screen->compiler);

#ifdef ENABLE_SHADER_CACHE
        if (screen->disk_cache)
                disk_cache_destroy(screen->disk_cache);
#endif

        u_transfer_helper_destroy(pscreen->transfer_helper);

        close(screen->fd);
        ralloc_free(pscreen);
}

static bool
v3d_has_feature(struct v3d_screen *screen, enum drm_v3d_param feature)
{
        struct drm_v3d_get_param p = {
                .param = feature,
        };
        int ret = v3d_ioctl(screen->fd, DRM_IOCTL_V3D_GET_PARAM, &p);

        if (ret != 0)
                return false;

        return p.value;
}

static void
v3d_init_shader_caps(struct v3d_screen *screen)
{
        for (unsigned i = 0; i <= PIPE_SHADER_COMPUTE; i++) {
                struct pipe_shader_caps *caps =
                        (struct pipe_shader_caps *)&screen->base.shader_caps[i];

                switch (i) {
                case PIPE_SHADER_VERTEX:
                case PIPE_SHADER_FRAGMENT:
                case PIPE_SHADER_GEOMETRY:
                        break;
                case PIPE_SHADER_COMPUTE:
                        if (!screen->has_csd)
                                continue;
                        break;
                default:
                        continue;
                }

                caps->max_instructions =
                caps->max_alu_instructions =
                caps->max_tex_instructions =
                caps->max_tex_indirections = 16384;
                caps->max_control_flow_depth = UINT_MAX;

                switch (i) {
                case PIPE_SHADER_VERTEX:
                        caps->max_inputs = V3D_MAX_VS_INPUTS / 4;
                        break;
                case PIPE_SHADER_GEOMETRY:
                        caps->max_inputs = V3D_MAX_GS_INPUTS / 4;
                        break;
                case PIPE_SHADER_FRAGMENT:
                        caps->max_inputs = V3D_MAX_FS_INPUTS / 4;
                        break;
                default:
                        break;
                }

                caps->max_outputs =
                        i == PIPE_SHADER_FRAGMENT ? 4 : V3D_MAX_FS_INPUTS / 4;

                caps->max_temps = 256; /* GL_MAX_PROGRAM_TEMPORARIES_ARB */
                /* Note: Limited by the offset size in
                 * v3d_unit_data_create().
                 */
                caps->max_const_buffer0_size = 16 * 1024 * sizeof(float);
                caps->max_const_buffers = 16;
                caps->indirect_temp_addr = true;
                caps->indirect_const_addr = true;
                caps->integers = true;
                caps->max_texture_samplers =
                caps->max_sampler_views = V3D_MAX_TEXTURE_SAMPLERS;

                caps->max_shader_buffers =
                        screen->has_cache_flush &&
                        (i != PIPE_SHADER_VERTEX && i != PIPE_SHADER_GEOMETRY) ?
                        PIPE_MAX_SHADER_BUFFERS : 0;

                caps->max_shader_images =
                        screen->has_cache_flush ? PIPE_MAX_SHADER_IMAGES : 0;

                caps->supported_irs = 1 << PIPE_SHADER_IR_NIR;
        }
}

static void
v3d_init_compute_caps(struct v3d_screen *screen)
{
        struct pipe_compute_caps *caps =
                (struct pipe_compute_caps *)&screen->base.compute_caps;

        if (!screen->has_csd)
                return;

        caps->address_bits = 32;

        snprintf(caps->ir_target, sizeof(caps->ir_target), "v3d");

        caps->grid_dimension = 3;

        /* GL_MAX_COMPUTE_SHADER_WORK_GROUP_COUNT: The CSD has a
         * 16-bit field for the number of workgroups in each
         * dimension.
         */
        caps->max_grid_size[0] =
        caps->max_grid_size[1] =
        caps->max_grid_size[2] = 65535;

        /* GL_MAX_COMPUTE_WORK_GROUP_SIZE */
        caps->max_block_size[0] =
        caps->max_block_size[1] =
        caps->max_block_size[2] = 256;

        /* GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS: This is
         * limited by WG_SIZE in the CSD.
         */
        caps->max_threads_per_block =
        caps->max_variable_threads_per_block = 256;

        /* GL_MAX_COMPUTE_SHARED_MEMORY_SIZE */
        caps->max_local_size = 32768;

        caps->max_private_size =
        caps->max_input_size = 4096;

        struct sysinfo si;
        sysinfo(&si);
        caps->max_global_size = si.totalram;
        caps->max_mem_alloc_size = MIN2(V3D_MAX_BUFFER_RANGE, si.totalram);

        caps->max_compute_units = 1;
        caps->images_supported = true;
        caps->subgroup_sizes = 16;
}

static void
v3d_init_screen_caps(struct v3d_screen *screen)
{
        struct pipe_caps *caps = (struct pipe_caps *)&screen->base.caps;

        u_init_pipe_screen_caps(&screen->base, 1);

        /* Supported features (boolean caps). */
        caps->vertex_color_unclamped = true;
        caps->npot_textures = true;
        caps->blend_equation_separate = true;
        caps->texture_multisample = true;
        caps->texture_swizzle = true;
        caps->vertex_element_instance_divisor = true;
        caps->start_instance = true;
        caps->vs_instanceid = true;
        caps->fragment_shader_texture_lod = true;
        caps->fragment_shader_derivatives = true;
        caps->primitive_restart_fixed_index = true;
        caps->emulate_nonfixed_primitive_restart = true;
        caps->primitive_restart = true;
        caps->occlusion_query = true;
        caps->stream_output_pause_resume = true;
        caps->draw_indirect = true;
        caps->multi_draw_indirect = true;
        caps->quads_follow_provoking_vertex_convention = true;
        caps->signed_vertex_buffer_offset = true;
        caps->shader_pack_half_float = true;
        caps->texture_half_float_linear = true;
        caps->framebuffer_no_attachment = true;
        caps->fs_face_is_integer_sysval = true;
        caps->tgsi_texcoord = true;
        caps->texture_mirror_clamp_to_edge = true;
        caps->sampler_view_target = true;
        caps->anisotropic_filter = true;
        caps->copy_between_compressed_and_plain_formats = true;
        caps->indep_blend_func = true;
        caps->conditional_render = true;
        caps->conditional_render_inverted = true;
        caps->cube_map_array = true;
        caps->texture_barrier = true;
        caps->polygon_offset_clamp = true;
        caps->texture_query_lod = true;

        caps->query_timestamp =
        caps->query_time_elapsed = screen->has_cpu_queue && screen->has_multisync;
        caps->texture_sampler_independent = false;

        /* We can't enable this flag, because it results in load_ubo
         * intrinsics across a 16b boundary, but v3d's TMU general
         * memory accesses wrap on 16b boundaries.
         */
        caps->packed_uniforms = false;

        caps->nir_images_as_deref = false;

        /* XXX perf: we don't want to emit these extra blits for
         * glReadPixels(), since we still have to do an uncached read
         * from the GPU of the result after waiting for the TFU blit
         * to happen.  However, disabling this introduces instability
         * in
         * dEQP-GLES31.functional.image_load_store.early_fragment_tests.*
         * and corruption in chromium's rendering.
         */
        caps->texture_transfer_modes = PIPE_TEXTURE_TRANSFER_BLIT;

        caps->compute = screen->has_csd;

        caps->generate_mipmap = v3d_has_feature(screen, DRM_V3D_PARAM_SUPPORTS_TFU);

        caps->indep_blend_enable = true;

        caps->constant_buffer_offset_alignment = V3D_NON_COHERENT_ATOM_SIZE;

        caps->max_texture_gather_components = 4;

        /* Disables shader storage when 0. */
        caps->shader_buffer_offset_alignment = screen->has_cache_flush ? 4 : 0;

        caps->glsl_feature_level = 330;

        caps->essl_feature_level = 310;

        caps->glsl_feature_level_compatibility = 140;

        caps->fs_coord_origin_upper_left = true;
        caps->fs_coord_origin_lower_left = false;
        caps->fs_coord_pixel_center_integer = false;
        caps->fs_coord_pixel_center_half_integer = true;

        caps->mixed_framebuffer_sizes = true;
        caps->mixed_color_depth_bits = true;

        caps->max_stream_output_buffers = 4;

        caps->max_varyings = V3D_MAX_FS_INPUTS / 4;

        /* Texturing. */
        caps->max_texture_2d_size =
                screen->nonmsaa_texture_size_limit ? 7680 : V3D_MAX_IMAGE_DIMENSION;
        caps->max_texture_cube_levels =
        caps->max_texture_3d_levels = V3D_MAX_MIP_LEVELS;
        caps->max_texture_array_layers = V3D_MAX_ARRAY_LAYERS;

        caps->max_render_targets = V3D_MAX_RENDER_TARGETS(screen->devinfo.ver);

        caps->vendor_id = 0x14E4;

        uint64_t system_memory;
        caps->video_memory = os_get_total_physical_memory(&system_memory) ?
                system_memory >> 20 : 0;

        caps->uma = true;

        caps->alpha_test = false;
        caps->flatshade = false;
        caps->two_sided_color = false;
        caps->vertex_color_clamped = false;
        caps->fragment_color_clamped = false;
        caps->gl_clamp = false;

        /* Geometry shaders */
        /* Minimum required by GLES 3.2 */
        caps->max_geometry_total_output_components = 1024;
        /* MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS / 4 */
        caps->max_geometry_output_vertices = 256;
        caps->max_gs_invocations = 32;

        caps->supported_prim_modes =
        caps->supported_prim_modes_with_restart = screen->prim_types;

        caps->texture_buffer_objects = true;

        caps->texture_buffer_offset_alignment = V3D_TMU_TEXEL_ALIGN;

        caps->image_store_formatted = false;

        caps->native_fence_fd = true;

        caps->depth_clip_disable = screen->devinfo.ver >= 71;

        caps->min_line_width =
        caps->min_line_width_aa =
        caps->min_point_size =
        caps->min_point_size_aa = 1;

        caps->point_size_granularity =
        caps->line_width_granularity = 0.1;

        caps->max_line_width =
        caps->max_line_width_aa = V3D_MAX_LINE_WIDTH;

        caps->max_point_size =
        caps->max_point_size_aa = V3D_MAX_POINT_SIZE;

        caps->max_texture_anisotropy = 16.0f;
        caps->max_texture_lod_bias = 16.0f;
}

static bool
v3d_screen_is_format_supported(struct pipe_screen *pscreen,
                               enum pipe_format format,
                               enum pipe_texture_target target,
                               unsigned sample_count,
                               unsigned storage_sample_count,
                               unsigned usage)
{
        struct v3d_screen *screen = v3d_screen(pscreen);

        if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
                return false;

        if (sample_count > 1 && sample_count != V3D_MAX_SAMPLES)
                return false;

        if (target >= PIPE_MAX_TEXTURE_TYPES) {
                return false;
        }

        if (usage & PIPE_BIND_VERTEX_BUFFER) {
                switch (format) {
                case PIPE_FORMAT_R32G32B32A32_FLOAT:
                case PIPE_FORMAT_R32G32B32_FLOAT:
                case PIPE_FORMAT_R32G32_FLOAT:
                case PIPE_FORMAT_R32_FLOAT:
                case PIPE_FORMAT_R32G32B32A32_SNORM:
                case PIPE_FORMAT_R32G32B32_SNORM:
                case PIPE_FORMAT_R32G32_SNORM:
                case PIPE_FORMAT_R32_SNORM:
                case PIPE_FORMAT_R32G32B32A32_SSCALED:
                case PIPE_FORMAT_R32G32B32_SSCALED:
                case PIPE_FORMAT_R32G32_SSCALED:
                case PIPE_FORMAT_R32_SSCALED:
                case PIPE_FORMAT_R16G16B16A16_UNORM:
                case PIPE_FORMAT_R16G16B16A16_FLOAT:
                case PIPE_FORMAT_R16G16B16_UNORM:
                case PIPE_FORMAT_R16G16_UNORM:
                case PIPE_FORMAT_R16_UNORM:
                case PIPE_FORMAT_R16_FLOAT:
                case PIPE_FORMAT_R16G16B16A16_SNORM:
                case PIPE_FORMAT_R16G16B16_SNORM:
                case PIPE_FORMAT_R16G16_SNORM:
                case PIPE_FORMAT_R16G16_FLOAT:
                case PIPE_FORMAT_R16_SNORM:
                case PIPE_FORMAT_R16G16B16A16_USCALED:
                case PIPE_FORMAT_R16G16B16_USCALED:
                case PIPE_FORMAT_R16G16_USCALED:
                case PIPE_FORMAT_R16_USCALED:
                case PIPE_FORMAT_R16G16B16A16_SSCALED:
                case PIPE_FORMAT_R16G16B16_SSCALED:
                case PIPE_FORMAT_R16G16_SSCALED:
                case PIPE_FORMAT_R16_SSCALED:
                case PIPE_FORMAT_B8G8R8A8_UNORM:
                case PIPE_FORMAT_R8G8B8A8_UNORM:
                case PIPE_FORMAT_R8G8B8_UNORM:
                case PIPE_FORMAT_R8G8_UNORM:
                case PIPE_FORMAT_R8_UNORM:
                case PIPE_FORMAT_R8G8B8A8_SNORM:
                case PIPE_FORMAT_R8G8B8_SNORM:
                case PIPE_FORMAT_R8G8_SNORM:
                case PIPE_FORMAT_R8_SNORM:
                case PIPE_FORMAT_R8G8B8A8_USCALED:
                case PIPE_FORMAT_R8G8B8_USCALED:
                case PIPE_FORMAT_R8G8_USCALED:
                case PIPE_FORMAT_R8_USCALED:
                case PIPE_FORMAT_R8G8B8A8_SSCALED:
                case PIPE_FORMAT_R8G8B8_SSCALED:
                case PIPE_FORMAT_R8G8_SSCALED:
                case PIPE_FORMAT_R8_SSCALED:
                case PIPE_FORMAT_R10G10B10A2_UNORM:
                case PIPE_FORMAT_B10G10R10A2_UNORM:
                case PIPE_FORMAT_R10G10B10A2_SNORM:
                case PIPE_FORMAT_B10G10R10A2_SNORM:
                case PIPE_FORMAT_R10G10B10A2_USCALED:
                case PIPE_FORMAT_B10G10R10A2_USCALED:
                case PIPE_FORMAT_R10G10B10A2_SSCALED:
                case PIPE_FORMAT_B10G10R10A2_SSCALED:
                        break;
                default:
                        return false;
                }
        }

        /* FORMAT_NONE gets allowed for ARB_framebuffer_no_attachments's probe
         * of FRAMEBUFFER_MAX_SAMPLES
         */
        if ((usage & PIPE_BIND_RENDER_TARGET) &&
            format != PIPE_FORMAT_NONE &&
            !v3d_rt_format_supported(&screen->devinfo, format)) {
                return false;
        }

        /* We do not support EXT_float_blend (blending with 32F formats)*/
        if ((usage & PIPE_BIND_BLENDABLE) &&
            (format == PIPE_FORMAT_R32G32B32A32_FLOAT ||
             format == PIPE_FORMAT_R32G32_FLOAT ||
             format == PIPE_FORMAT_R32_FLOAT)) {
                return false;
        }

        if ((usage & PIPE_BIND_SAMPLER_VIEW) &&
            !v3d_tex_format_supported(&screen->devinfo, format)) {
                return false;
        }

        if ((usage & PIPE_BIND_DEPTH_STENCIL) &&
            !(format == PIPE_FORMAT_S8_UINT_Z24_UNORM ||
              format == PIPE_FORMAT_X8Z24_UNORM ||
              format == PIPE_FORMAT_Z16_UNORM ||
              format == PIPE_FORMAT_Z32_FLOAT ||
              format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT)) {
                return false;
        }

        if ((usage & PIPE_BIND_INDEX_BUFFER) &&
            !(format == PIPE_FORMAT_R8_UINT ||
              format == PIPE_FORMAT_R16_UINT ||
              format == PIPE_FORMAT_R32_UINT)) {
                return false;
        }

        if (usage & PIPE_BIND_SHADER_IMAGE) {
                switch (format) {
                /* FIXME: maybe we can implement a swizzle-on-writes to add
                 * support for BGRA-alike formats.
                 */
                case PIPE_FORMAT_A4B4G4R4_UNORM:
                case PIPE_FORMAT_A1B5G5R5_UNORM:
                case PIPE_FORMAT_B5G6R5_UNORM:
                case PIPE_FORMAT_B8G8R8A8_UNORM:
                case PIPE_FORMAT_X8Z24_UNORM:
                case PIPE_FORMAT_Z16_UNORM:
                        return false;
                default:
                        return true;
                }
        }

        return true;
}

static const void *
v3d_screen_get_compiler_options(struct pipe_screen *pscreen,
                                enum pipe_shader_ir ir,
                                enum pipe_shader_type shader)
{
        struct v3d_screen *screen = v3d_screen(pscreen);
        const struct v3d_device_info *devinfo = &screen->devinfo;

        static bool initialized = false;
        static nir_shader_compiler_options options = {
                .compact_arrays = true,
                .lower_uadd_sat = true,
                .lower_usub_sat = true,
                .lower_iadd_sat = true,
                .lower_all_io_to_temps = true,
                .lower_extract_byte = true,
                .lower_extract_word = true,
                .lower_insert_byte = true,
                .lower_insert_word = true,
                .lower_bitfield_insert = true,
                .lower_bitfield_extract = true,
                .lower_bitfield_reverse = true,
                .lower_bit_count = true,
                .lower_cs_local_id_to_index = true,
                .lower_ffract = true,
                .lower_fmod = true,
                .lower_pack_unorm_2x16 = true,
                .lower_pack_snorm_2x16 = true,
                .lower_pack_unorm_4x8 = true,
                .lower_pack_snorm_4x8 = true,
                .lower_unpack_unorm_4x8 = true,
                .lower_unpack_snorm_4x8 = true,
                .lower_pack_half_2x16 = true,
                .lower_unpack_half_2x16 = true,
                .lower_pack_32_2x16 = true,
                .lower_pack_32_2x16_split = true,
                .lower_unpack_32_2x16_split = true,
                .lower_fdiv = true,
                .lower_find_lsb = true,
                .lower_ffma16 = true,
                .lower_ffma32 = true,
                .lower_ffma64 = true,
                .lower_flrp32 = true,
                .lower_fpow = true,
                .lower_fsqrt = true,
                .lower_ifind_msb = true,
                .lower_isign = true,
                .lower_ldexp = true,
                .lower_hadd = true,
                .lower_fisnormal = true,
                .lower_mul_high = true,
                .lower_wpos_pntc = true,
                .lower_to_scalar = true,
                .lower_int64_options =
                        nir_lower_bcsel64 |
                        nir_lower_conv64 |
                        nir_lower_iadd64 |
                        nir_lower_icmp64 |
                        nir_lower_imul_2x32_64 |
                        nir_lower_imul64 |
                        nir_lower_ineg64 |
                        nir_lower_logic64 |
                        nir_lower_shift64 |
                        nir_lower_ufind_msb64,
                .lower_fquantize2f16 = true,
                .lower_ufind_msb = true,
                .has_fsub = true,
                .has_isub = true,
                .has_uclz = true,
                .divergence_analysis_options =
                       nir_divergence_multiple_workgroup_per_compute_subgroup,
                /* We don't currently support this in the backend, but that is
                 * okay because our NIR compiler sets the option
                 * lower_all_io_to_temps, which will eliminate indirect
                 * indexing on all input/output variables by translating it to
                 * indirect indexing on temporary variables instead, which we
                 * will then lower to scratch. We prefer this over setting this
                 * to 0, which would cause if-ladder injection to eliminate
                 * indirect indexing on inputs.
                 */
                .support_indirect_inputs = (uint8_t)BITFIELD_MASK(PIPE_SHADER_TYPES),
                .support_indirect_outputs = (uint8_t)BITFIELD_MASK(PIPE_SHADER_TYPES),
                /* This will enable loop unrolling in the state tracker so we won't
                 * be able to selectively disable it in backend if it leads to
                 * lower thread counts or TMU spills. Choose a conservative maximum to
                 * limit register pressure impact.
                 */
                .max_unroll_iterations = 16,
                .force_indirect_unrolling_sampler = true,
                .scalarize_ddx = true,
                .max_varying_expression_cost = 4,
        };

        if (!initialized) {
                options.lower_fsat = devinfo->ver < 71;
                initialized = true;
        }

        return &options;
}

static const uint64_t v3d_available_modifiers[] = {
   DRM_FORMAT_MOD_BROADCOM_UIF,
   DRM_FORMAT_MOD_LINEAR,
   DRM_FORMAT_MOD_BROADCOM_SAND128,
};

static void
v3d_screen_query_dmabuf_modifiers(struct pipe_screen *pscreen,
                                  enum pipe_format format, int max,
                                  uint64_t *modifiers,
                                  unsigned int *external_only,
                                  int *count)
{
        int i;
        int num_modifiers = ARRAY_SIZE(v3d_available_modifiers);

        switch (format) {
        case PIPE_FORMAT_P030:
                /* Expose SAND128, but not LINEAR or UIF */
                *count = 1;
                if (modifiers && max > 0) {
                        modifiers[0] = DRM_FORMAT_MOD_BROADCOM_SAND128;
                        if (external_only)
                                external_only[0] = true;
                }
                return;

        case PIPE_FORMAT_NV12:
                /* Expose UIF, LINEAR and SAND128 */
                break;
        
        case PIPE_FORMAT_R8_UNORM:
        case PIPE_FORMAT_R8G8_UNORM:
        case PIPE_FORMAT_R16_UNORM:
        case PIPE_FORMAT_R16G16_UNORM:
                /* Expose UIF, LINEAR and SAND128 */
		if (!modifiers) break;
                *count = MIN2(max, num_modifiers);
                for (i = 0; i < *count; i++) {
                        modifiers[i] = v3d_available_modifiers[i];
                        if (external_only)
                                external_only[i] = modifiers[i] == DRM_FORMAT_MOD_BROADCOM_SAND128;
                }
                return;

        default:
                /* Expose UIF and LINEAR, but not SAND128 */
                num_modifiers--;
        }

        if (!modifiers) {
                *count = num_modifiers;
                return;
        }

        *count = MIN2(max, num_modifiers);
        for (i = 0; i < *count; i++) {
                modifiers[i] = v3d_available_modifiers[i];
                if (external_only)
                        external_only[i] = util_format_is_yuv(format);
        }
}

static bool
v3d_screen_is_dmabuf_modifier_supported(struct pipe_screen *pscreen,
                                        uint64_t modifier,
                                        enum pipe_format format,
                                        bool *external_only)
{
        int i;
        if (fourcc_mod_broadcom_mod(modifier) == DRM_FORMAT_MOD_BROADCOM_SAND128) {
                switch(format) {
                case PIPE_FORMAT_NV12:
                case PIPE_FORMAT_P030:
                case PIPE_FORMAT_R8_UNORM:
                case PIPE_FORMAT_R8G8_UNORM:
                case PIPE_FORMAT_R16_UNORM:
                case PIPE_FORMAT_R16G16_UNORM:
                        if (external_only)
                                *external_only = true;
                        return true;
                default:
                        return false;
                }
        } else if (format == PIPE_FORMAT_P030) {
                /* For PIPE_FORMAT_P030 we don't expose LINEAR or UIF. */
                return false;
        }

        /* We don't want to generally allow DRM_FORMAT_MOD_BROADCOM_SAND128
         * modifier, that is the last v3d_available_modifiers. We only accept
         * it in the case of having a PIPE_FORMAT_NV12 or PIPE_FORMAT_P030.
         */
        assert(v3d_available_modifiers[ARRAY_SIZE(v3d_available_modifiers) - 1] ==
               DRM_FORMAT_MOD_BROADCOM_SAND128);
        for (i = 0; i < ARRAY_SIZE(v3d_available_modifiers) - 1; i++) {
                if (v3d_available_modifiers[i] == modifier) {
                        if (external_only)
                                *external_only = util_format_is_yuv(format);

                        return true;
                }
        }

        return false;
}

static enum pipe_format
v3d_screen_get_compatible_tlb_format(struct pipe_screen *screen,
                                     enum pipe_format format)
{
        switch (format) {
        case PIPE_FORMAT_R16G16_UNORM:
                return PIPE_FORMAT_R16G16_UINT;
        default:
                return format;
        }
}

static struct disk_cache *
v3d_screen_get_disk_shader_cache(struct pipe_screen *pscreen)
{
        struct v3d_screen *screen = v3d_screen(pscreen);

        return screen->disk_cache;
}

static int
v3d_screen_get_fd(struct pipe_screen *pscreen)
{
        struct v3d_screen *screen = v3d_screen(pscreen);

        return screen->fd;
}

struct pipe_screen *
v3d_screen_create(int fd, const struct pipe_screen_config *config,
                  struct renderonly *ro)
{
        struct v3d_screen *screen = rzalloc(NULL, struct v3d_screen);
        struct pipe_screen *pscreen;

        util_cpu_trace_init();

        pscreen = &screen->base;

        pscreen->destroy = v3d_screen_destroy;
        pscreen->get_screen_fd = v3d_screen_get_fd;
        pscreen->context_create = v3d_context_create;
        pscreen->is_format_supported = v3d_screen_is_format_supported;
        pscreen->get_canonical_format = v3d_screen_get_compatible_tlb_format;

        screen->fd = fd;
        screen->ro = ro;

        list_inithead(&screen->bo_cache.time_list);
        (void)mtx_init(&screen->bo_handles_mutex, mtx_plain);
        screen->bo_handles = util_hash_table_create_ptr_keys();

#if USE_V3D_SIMULATOR
        screen->sim_file = v3d_simulator_init(screen->fd);
#endif

        if (!v3d_get_device_info(screen->fd, &screen->devinfo, &v3d_ioctl))
                goto fail;

        screen->perfcnt = v3d_perfcntrs_init(&screen->devinfo, screen->fd);
        if (!screen->perfcnt)
                goto fail;

        driParseConfigFiles(config->options, config->options_info, 0, "v3d",
                            NULL, NULL, NULL, 0, NULL, 0);

        /* We have to driCheckOption for the simulator mode to not assertion
         * fail on not having our XML config.
         */
        const char *nonmsaa_name = "v3d_nonmsaa_texture_size_limit";
        screen->nonmsaa_texture_size_limit =
                driCheckOption(config->options, nonmsaa_name, DRI_BOOL) &&
                driQueryOptionb(config->options, nonmsaa_name);

        slab_create_parent(&screen->transfer_pool, sizeof(struct v3d_transfer), 16);

        screen->has_csd = v3d_has_feature(screen, DRM_V3D_PARAM_SUPPORTS_CSD);
        screen->has_cache_flush =
                v3d_has_feature(screen, DRM_V3D_PARAM_SUPPORTS_CACHE_FLUSH);
        screen->has_perfmon = v3d_has_feature(screen, DRM_V3D_PARAM_SUPPORTS_PERFMON);
        screen->has_cpu_queue = v3d_has_feature(screen, DRM_V3D_PARAM_SUPPORTS_CPU_QUEUE);
        screen->has_multisync = v3d_has_feature(screen, DRM_V3D_PARAM_SUPPORTS_MULTISYNC_EXT);

        v3d_fence_screen_init(screen);

        v3d_process_debug_variable();

        v3d_resource_screen_init(pscreen);

        screen->compiler = v3d_compiler_init(&screen->devinfo, 0);

#ifdef ENABLE_SHADER_CACHE
        v3d_disk_cache_init(screen);
#endif

        pscreen->get_name = v3d_screen_get_name;
        pscreen->get_vendor = v3d_screen_get_vendor;
        pscreen->get_device_vendor = v3d_screen_get_vendor;
        pscreen->get_compiler_options = v3d_screen_get_compiler_options;
        pscreen->get_disk_shader_cache = v3d_screen_get_disk_shader_cache;
        pscreen->query_dmabuf_modifiers = v3d_screen_query_dmabuf_modifiers;
        pscreen->is_dmabuf_modifier_supported =
                v3d_screen_is_dmabuf_modifier_supported;

        if (screen->has_perfmon) {
                pscreen->get_driver_query_group_info = v3d_get_driver_query_group_info;
                pscreen->get_driver_query_info = v3d_get_driver_query_info;
        }

        /* Generate the bitmask of supported draw primitives. */
        screen->prim_types = BITFIELD_BIT(MESA_PRIM_POINTS) |
                             BITFIELD_BIT(MESA_PRIM_LINES) |
                             BITFIELD_BIT(MESA_PRIM_LINE_LOOP) |
                             BITFIELD_BIT(MESA_PRIM_LINE_STRIP) |
                             BITFIELD_BIT(MESA_PRIM_TRIANGLES) |
                             BITFIELD_BIT(MESA_PRIM_TRIANGLE_STRIP) |
                             BITFIELD_BIT(MESA_PRIM_TRIANGLE_FAN) |
                             BITFIELD_BIT(MESA_PRIM_LINES_ADJACENCY) |
                             BITFIELD_BIT(MESA_PRIM_LINE_STRIP_ADJACENCY) |
                             BITFIELD_BIT(MESA_PRIM_TRIANGLES_ADJACENCY) |
                             BITFIELD_BIT(MESA_PRIM_TRIANGLE_STRIP_ADJACENCY);

        v3d_init_shader_caps(screen);
        v3d_init_compute_caps(screen);
        v3d_init_screen_caps(screen);

        return pscreen;

fail:
        close(fd);
        ralloc_free(pscreen);
        return NULL;
}
