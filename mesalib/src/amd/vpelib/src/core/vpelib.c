/* Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <string.h>
#include "vpelib.h"
#include "vpe_priv.h"
#include "common.h"
#include "color_bg.h"
#include "color_gamma.h"
#include "cmd_builder.h"
#include "resource.h"
#include "color.h"
#include "vpec.h"
#include "vpe_desc_writer.h"
#include "dpp.h"
#include "mpc.h"
#include "opp.h"
#include "geometric_scaling.h"
#include <stdlib.h>
#include <time.h>

static void dummy_sys_event(enum vpe_event_id eventId, ...)
{
    // Do nothing, if no callback is provided for sys event
}

static void override_debug_option(
    struct vpe_debug_options *debug, const struct vpe_debug_options *user_debug)
{
    if ((debug == NULL) || (user_debug == NULL)) {
        return;
    }

    if (user_debug->flags.bg_bit_depth)
        debug->bg_bit_depth = user_debug->bg_bit_depth;

    if (user_debug->flags.cm_in_bypass)
        debug->cm_in_bypass = user_debug->cm_in_bypass;

    if (user_debug->flags.vpcnvc_bypass)
        debug->vpcnvc_bypass = user_debug->vpcnvc_bypass;

    if (user_debug->flags.mpc_bypass)
        debug->mpc_bypass = user_debug->mpc_bypass;

    if (user_debug->flags.disable_reuse_bit)
        debug->disable_reuse_bit = user_debug->disable_reuse_bit;

    if (user_debug->flags.identity_3dlut)
        debug->identity_3dlut = user_debug->identity_3dlut;

    if (user_debug->flags.sce_3dlut)
        debug->sce_3dlut = user_debug->sce_3dlut;

    if (user_debug->enable_mem_low_power.flags.cm)
        debug->enable_mem_low_power.bits.cm = user_debug->enable_mem_low_power.bits.cm;

    if (user_debug->enable_mem_low_power.flags.dscl)
        debug->enable_mem_low_power.bits.dscl = user_debug->enable_mem_low_power.bits.dscl;

    if (user_debug->enable_mem_low_power.flags.mpc)
        debug->enable_mem_low_power.bits.mpc = user_debug->enable_mem_low_power.bits.mpc;

    if (user_debug->flags.bg_color_fill_only)
        debug->bg_color_fill_only = user_debug->bg_color_fill_only;

    if (user_debug->flags.assert_when_not_support)
        debug->assert_when_not_support = user_debug->assert_when_not_support;

    if (user_debug->flags.bypass_ogam)
        debug->bypass_ogam = user_debug->bypass_ogam;

    if (user_debug->flags.bypass_gamcor)
        debug->bypass_gamcor = user_debug->bypass_gamcor;

    if (user_debug->flags.bypass_dpp_gamut_remap)
        debug->bypass_dpp_gamut_remap = user_debug->bypass_dpp_gamut_remap;

    if (user_debug->flags.bypass_post_csc)
        debug->bypass_post_csc = user_debug->bypass_post_csc;

    if (user_debug->flags.clamping_setting) {
        debug->clamping_setting = user_debug->clamping_setting;
        debug->clamping_params  = user_debug->clamping_params;
    }

    if (user_debug->flags.expansion_mode)
        debug->expansion_mode = user_debug->expansion_mode;

    if (user_debug->flags.bypass_per_pixel_alpha)
        debug->bypass_per_pixel_alpha = user_debug->bypass_per_pixel_alpha;

    if (user_debug->flags.opp_pipe_crc_ctrl)
        debug->opp_pipe_crc_ctrl = user_debug->opp_pipe_crc_ctrl;

    if (user_debug->flags.dpp_crc_ctrl)
        debug->dpp_crc_ctrl = user_debug->dpp_crc_ctrl;

    if (user_debug->flags.mpc_crc_ctrl)
        debug->mpc_crc_ctrl = user_debug->mpc_crc_ctrl;

    if (user_debug->flags.visual_confirm)
        debug->visual_confirm_params = user_debug->visual_confirm_params;

    if (user_debug->flags.skip_optimal_tap_check)
        debug->skip_optimal_tap_check = user_debug->skip_optimal_tap_check;

    if (user_debug->flags.bypass_blndgam)
        debug->bypass_blndgam = user_debug->bypass_blndgam;

    if (user_debug->flags.disable_lut_caching)
        debug->disable_lut_caching = user_debug->disable_lut_caching;
}

static void verify_collaboration_mode(struct vpe_priv *vpe_priv)
{
    if (vpe_priv->pub.level == VPE_IP_LEVEL_1_1) {
        if (vpe_priv->collaboration_mode == true && vpe_priv->collaborate_sync_index == 0) {
            srand((unsigned int)time(NULL)); // Initialization, should only be called once.
            // coverity[dont_call]
            uint32_t randnum                 = (uint32_t)rand() % 15;
            randnum                          = randnum << 12;
            vpe_priv->collaborate_sync_index = (int32_t)randnum;
        }
    } else if (vpe_priv->pub.level == VPE_IP_LEVEL_1_0) {
        vpe_priv->collaboration_mode = false;
    }
}

static enum vpe_status create_output_config_vector(struct vpe_priv *vpe_priv)
{
    uint32_t i;

    // output config vector stores all share-able configs that can be re-used later
    for (i = 0; i < vpe_priv->pub.caps->resource_caps.num_cdc_be; i++) {
        vpe_priv->output_ctx.configs[i] =
            vpe_vector_create(vpe_priv, sizeof(struct config_record), MIN_NUM_CONFIG);
        if (!vpe_priv->output_ctx.configs[i]) {
            return VPE_STATUS_NO_MEMORY;
        }
    }
    return VPE_STATUS_OK;
}

static void destroy_output_config_vector(struct vpe_priv *vpe_priv)
{
    uint32_t i;

    for (i = 0; i < vpe_priv->pub.caps->resource_caps.num_cdc_be; i++) {
        if (vpe_priv->output_ctx.configs[i]) {
            vpe_vector_free(vpe_priv->output_ctx.configs[i]);
            vpe_priv->output_ctx.configs[i] = NULL;
        }
    }
}

static void free_output_ctx(struct vpe_priv *vpe_priv)
{
    if (vpe_priv->output_ctx.gamut_remap)
        vpe_free(vpe_priv->output_ctx.gamut_remap);

    if (vpe_priv->output_ctx.output_tf)
        vpe_free(vpe_priv->output_ctx.output_tf);

    destroy_output_config_vector(vpe_priv);
}

struct vpe *vpe_create(const struct vpe_init_data *params)
{
    struct vpe_priv *vpe_priv;
    enum vpe_status  status;

    if (!params || (params->funcs.zalloc == NULL) || (params->funcs.free == NULL) ||
        (params->funcs.log == NULL))
        return NULL;

    vpe_priv =
        (struct vpe_priv *)params->funcs.zalloc(params->funcs.mem_ctx, sizeof(struct vpe_priv));
    if (!vpe_priv)
        return NULL;

    vpe_priv->init = *params;

    // Make sys event an optional feature but hooking up to dummy function if no callback is
    // provided
    if (vpe_priv->init.funcs.sys_event == NULL)
        vpe_priv->init.funcs.sys_event = dummy_sys_event;

    vpe_priv->pub.level =
        vpe_resource_parse_ip_version(params->ver_major, params->ver_minor, params->ver_rev);

    vpe_priv->pub.version = (VPELIB_API_VERSION_MAJOR << VPELIB_API_VERSION_MAJOR_SHIFT) |
                            (VPELIB_API_VERSION_MINOR << VPELIB_API_VERSION_MINOR_SHIFT);

    status = vpe_construct_resource(vpe_priv, vpe_priv->pub.level, &vpe_priv->resource);
    if (status != VPE_STATUS_OK) {
        vpe_free(vpe_priv);
        return NULL;
    }

    vpe_priv->vpe_cmd_vector =
        vpe_vector_create(vpe_priv, sizeof(struct vpe_cmd_info), MIN_VPE_CMD);
    if (!vpe_priv->vpe_cmd_vector) {
        vpe_free(vpe_priv);
        return NULL;
    }

    status = create_output_config_vector(vpe_priv);
    if (status != VPE_STATUS_OK) {
        destroy_output_config_vector(vpe_priv);
        vpe_free(vpe_priv);
        return NULL;
    }

    override_debug_option(&vpe_priv->init.debug, &params->debug);

    vpe_color_setup_x_points_distribution();
    vpe_color_setup_x_points_distribution_degamma();

    vpe_priv->ops_support      = false;
    vpe_priv->scale_yuv_matrix = true;

    vpe_priv->collaborate_sync_index = 0;
    return &vpe_priv->pub;
}

void vpe_destroy(struct vpe **vpe)
{
    struct vpe_priv *vpe_priv;

    if (!vpe || ((*vpe) == NULL))
        return;

    vpe_priv = container_of(*vpe, struct vpe_priv, pub);

    vpe_destroy_resource(vpe_priv, &vpe_priv->resource);

    free_output_ctx(vpe_priv);

    vpe_free_stream_ctx(vpe_priv);

    if (vpe_priv->vpe_cmd_vector)
        vpe_vector_free(vpe_priv->vpe_cmd_vector);

    if (vpe_priv->dummy_input_param)
        vpe_free(vpe_priv->dummy_input_param);

    if (vpe_priv->dummy_stream)
        vpe_free(vpe_priv->dummy_stream);

    vpe_free(vpe_priv);

    *vpe = NULL;
}

/*****************************************************************************************
 * populate_bg_stream
 * populate virtual stream for background output only
 * struct vpe* vpe
 *      [input] vpe context
 * const struct vpe_build_param* org_param
 *      [input] original parameter from caller
 * struct struct vpe_stream_ctx* stream_ctx
 *      [input/output] caller provided vpe_stream_ctx struct to populate
 *****************************************************************************************/
static enum vpe_status populate_bg_stream(struct vpe_priv *vpe_priv, const struct vpe_build_param *param, struct stream_ctx *stream_ctx)
{
    struct vpe_surface_info          *surface_info;
    struct vpe_scaling_info          *scaling_info;
    struct vpe_scaling_filter_coeffs *polyphaseCoeffs;
    struct vpe_stream                *stream;

    if (!param || !stream_ctx)
        return VPE_STATUS_ERROR;

    stream = &stream_ctx->stream;
    stream_ctx->stream_type = VPE_STREAM_TYPE_BG_GEN;

    // if output surface is too small, don't use it as dummy input
    // request 2x2 instead of 1x1 for bpc safety
    // as we are to treat output as input for RGB 1x1, need 4bytes at least
    // but if output is YUV, bpc will be smaller and need larger dimension

    if (param->dst_surface.plane_size.surface_size.width < VPE_MIN_VIEWPORT_SIZE ||
        param->dst_surface.plane_size.surface_size.height < VPE_MIN_VIEWPORT_SIZE ||
        param->dst_surface.plane_size.surface_pitch < 256 / 4 || // 256bytes, 4bpp
        param->target_rect.width < VPE_MIN_VIEWPORT_SIZE ||
        param->target_rect.height < VPE_MIN_VIEWPORT_SIZE) {
        return VPE_STATUS_ERROR;
    }

    // set output surface as our dummy input
    surface_info                      = &stream->surface_info;
    scaling_info                      = &stream->scaling_info;
    polyphaseCoeffs                   = &stream->polyphase_scaling_coeffs;
    surface_info->address.type        = param->dst_surface.address.type;
    surface_info->address.tmz_surface = param->dst_surface.address.tmz_surface;
    surface_info->address.grph.addr.quad_part =
        param->dst_surface.address.grph.addr.quad_part;

    surface_info->swizzle                   = param->dst_surface.swizzle; // treat it as linear for simple
    surface_info->plane_size.surface_size.x = 0;
    surface_info->plane_size.surface_size.y = 0;
    // min width & height in pixels
    surface_info->plane_size.surface_size.width     = VPE_MIN_VIEWPORT_SIZE;
    surface_info->plane_size.surface_size.height    = VPE_MIN_VIEWPORT_SIZE;
    surface_info->plane_size.surface_pitch          = param->dst_surface.plane_size.surface_pitch;
    surface_info->plane_size.surface_aligned_height = param->dst_surface.plane_size.surface_aligned_height;
    surface_info->dcc.enable                        = false;
    surface_info->format                            = param->dst_surface.format;
    surface_info->cs.encoding                       = param->dst_surface.cs.encoding;
    surface_info->cs.range                          = param->dst_surface.cs.range;
    surface_info->cs.tf                             = param->dst_surface.cs.tf;
    surface_info->cs.cositing                       = param->dst_surface.cs.cositing;
    surface_info->cs.primaries                      = param->dst_surface.cs.primaries;
    scaling_info->src_rect.x                        = 0;
    scaling_info->src_rect.y                        = 0;
    scaling_info->src_rect.width                    = VPE_MIN_VIEWPORT_SIZE;
    scaling_info->src_rect.height                   = VPE_MIN_VIEWPORT_SIZE;
    scaling_info->dst_rect.x                        = param->target_rect.x;
    scaling_info->dst_rect.y                        = param->target_rect.y;
    scaling_info->dst_rect.width                    = VPE_MIN_VIEWPORT_SIZE;
    scaling_info->dst_rect.height                   = VPE_MIN_VIEWPORT_SIZE;
    scaling_info->taps.v_taps                       = 4;
    scaling_info->taps.h_taps                       = 4;
    scaling_info->taps.v_taps_c                     = 2;
    scaling_info->taps.h_taps_c                     = 2;

    polyphaseCoeffs->taps      = scaling_info->taps;
    polyphaseCoeffs->nb_phases = 64;

    stream->blend_info.blending             = true;
    stream->blend_info.pre_multiplied_alpha = false;
    stream->blend_info.global_alpha         = true; // hardcoded upon DAL request
    stream->blend_info.global_alpha_value   = 0;    // transparent as we are dummy input

    stream->color_adj.brightness        = 0.0f;
    stream->color_adj.contrast          = 1.0f;
    stream->color_adj.hue               = 0.0f;
    stream->color_adj.saturation        = 1.0f;
    stream->rotation                    = VPE_ROTATION_ANGLE_0;
    stream->horizontal_mirror           = false;
    stream->vertical_mirror             = false;
    stream->enable_luma_key             = false;
    stream->lower_luma_bound            = 0;
    stream->upper_luma_bound            = 0;
    stream->flags.hdr_metadata          = 0;
    stream->flags.geometric_scaling     = 0;
    stream->use_external_scaling_coeffs = false;

    return VPE_STATUS_OK;
}

static uint32_t get_required_virtual_stream_count(struct vpe_priv *vpe_priv, const struct vpe_build_param *param)
{
    uint32_t result = 0;

    // Check for zero-input background stream
    // Normally we result++ instead of returning, but bg_color_fill_only removes other streams (and therefore other features)
    if (param->num_streams == 0 || vpe_priv->init.debug.bg_color_fill_only)
        return 1;

    return result;
}

static enum vpe_status populate_input_streams(struct vpe_priv *vpe_priv, const struct vpe_build_param *param, struct stream_ctx *stream_ctx_base)
{
    enum vpe_status    result = VPE_STATUS_OK;
    uint32_t           i;
    struct stream_ctx* stream_ctx;
    bool               input_h_mirror, output_h_mirror;

    vpe_priv->resource.check_h_mirror_support(&input_h_mirror, &output_h_mirror);

    for (i = 0; i < vpe_priv->num_input_streams; i++) {
        stream_ctx = &stream_ctx_base[i];
        stream_ctx->stream_type = VPE_STREAM_TYPE_INPUT;
        stream_ctx->stream_idx = (int32_t)i;

        stream_ctx->per_pixel_alpha =
            vpe_has_per_pixel_alpha(param->streams[i].surface_info.format);

        if (vpe_priv->init.debug.bypass_per_pixel_alpha) {
            stream_ctx->per_pixel_alpha = false;
        } else if (param->streams[i].enable_luma_key) {
            stream_ctx->per_pixel_alpha = true;
        }
        if (param->streams[i].horizontal_mirror && !input_h_mirror && output_h_mirror)
            stream_ctx->flip_horizonal_output = true;
        else
            stream_ctx->flip_horizonal_output = false;

        memcpy(&stream_ctx->stream, &param->streams[i], sizeof(struct vpe_stream));

        /* if top-bottom blending is not supported,
         * the 1st stream still can support blending with background,
         * however, the 2nd stream and onward can't enable blending.
         */
        if (i && param->streams[i].blend_info.blending &&
            !vpe_priv->pub.caps->color_caps.mpc.top_bottom_blending) {
            result = VPE_STATUS_ALPHA_BLENDING_NOT_SUPPORTED;
            break;
        }
    }

    return result;
}

static enum vpe_status populate_virtual_streams(struct vpe_priv* vpe_priv, const struct vpe_build_param* param, struct stream_ctx* stream_ctx_base, uint32_t num_virtual_streams)
{
    enum vpe_status    result             = VPE_STATUS_OK;
    uint32_t           virtual_stream_idx = 0;
    struct stream_ctx *stream_ctx;
    bool               input_h_mirror, output_h_mirror;

    vpe_priv->resource.check_h_mirror_support(&input_h_mirror, &output_h_mirror);

    // Background generation stream
    if (param->num_streams == 0 || vpe_priv->init.debug.bg_color_fill_only) {
        if (num_virtual_streams != 1)
            result = VPE_STATUS_ERROR;
        else
            result = populate_bg_stream(vpe_priv, param, &stream_ctx_base[virtual_stream_idx++]);
    }

    if (result != VPE_STATUS_OK)
        return result;

    for (virtual_stream_idx = 0; virtual_stream_idx < num_virtual_streams; virtual_stream_idx++) {
        stream_ctx             = &stream_ctx_base[virtual_stream_idx];
        stream_ctx->stream_idx = virtual_stream_idx + vpe_priv->num_input_streams;
        stream_ctx->per_pixel_alpha =
            vpe_has_per_pixel_alpha(stream_ctx->stream.surface_info.format);
        if (vpe_priv->init.debug.bypass_per_pixel_alpha) {
            stream_ctx->per_pixel_alpha = false;
        }
        if (stream_ctx->stream.horizontal_mirror && !input_h_mirror && output_h_mirror)
            stream_ctx->flip_horizonal_output = true;
        else
            stream_ctx->flip_horizonal_output = false;
    }

    return result;
}

enum vpe_status vpe_check_support(
    struct vpe *vpe, const struct vpe_build_param *param, struct vpe_bufs_req *req)
{
    struct vpe_priv   *vpe_priv;
    struct vpec       *vpec;
    struct dpp        *dpp;
    enum vpe_status    status;
    struct output_ctx *output_ctx = NULL;
    uint32_t           i, required_virtual_streams;

    vpe_priv        = container_of(vpe, struct vpe_priv, pub);
    vpec            = &vpe_priv->resource.vpec;
    dpp             = vpe_priv->resource.dpp[0];
    status          = VPE_STATUS_OK;

    vpe_priv->collaboration_mode = param->collaboration_mode;
    vpe_priv->vpe_num_instance   = param->num_instances;
    verify_collaboration_mode(vpe_priv);

    required_virtual_streams = get_required_virtual_stream_count(vpe_priv, param);

    if (!vpe_priv->stream_ctx ||
        vpe_priv->num_streams != (param->num_streams + vpe_priv->num_virtual_streams) ||
        vpe_priv->num_virtual_streams != required_virtual_streams) {
        if (vpe_priv->stream_ctx)
            vpe_free_stream_ctx(vpe_priv);

        vpe_priv->stream_ctx = vpe_alloc_stream_ctx(vpe_priv, param->num_streams + required_virtual_streams);
    }

    if (!vpe_priv->stream_ctx)
        status = VPE_STATUS_NO_MEMORY;
    else {
        vpe_priv->num_streams = param->num_streams + required_virtual_streams;
        vpe_priv->num_virtual_streams = required_virtual_streams;
        vpe_priv->num_input_streams = param->num_streams;
    }

    if (param->num_streams == 0 || vpe_priv->init.debug.bg_color_fill_only) {
        if (!((vpe_priv->num_streams == 1) &&
            (vpe_priv->num_virtual_streams == 1) &&
            (vpe_priv->num_input_streams == 0))) {
            vpe_free_stream_ctx(vpe_priv);
            vpe_priv->stream_ctx = vpe_alloc_stream_ctx(vpe_priv, 1);
            vpe_priv->num_streams = required_virtual_streams;
            vpe_priv->num_virtual_streams = required_virtual_streams;
            vpe_priv->num_input_streams = 0;
        }

        if (!vpe_priv->stream_ctx)
            status = VPE_STATUS_NO_MEMORY;
    }


    if (status == VPE_STATUS_OK) {  
        // output checking - check per asic support
        status = vpe_check_output_support(vpe, param);
        if (status != VPE_STATUS_OK) {
            vpe_log("fail output support check. status %d\n", (int)status);
        }
    }

    if (status == VPE_STATUS_OK) {
        // input checking - check per asic support
        for (i = 0; i < param->num_streams; i++) {
            status = vpe_check_input_support(vpe, &param->streams[i]);
            if (status != VPE_STATUS_OK) {
                vpe_log("fail input support check. status %d\n", (int)status);
                break;
            }
        }
    }

    if (status == VPE_STATUS_OK) {
        // input checking - check tone map support
        for (i = 0; i < param->num_streams; i++) {
            status = vpe_check_tone_map_support(vpe, &param->streams[i], param);
            if (status != VPE_STATUS_OK) {
                vpe_log("fail tone map support check. status %d\n", (int)status);
                break;
            }
        }
    }

    if (status == VPE_STATUS_OK) {
        // output resource preparation for further checking (cache the result)
        output_ctx                     = &vpe_priv->output_ctx;
        output_ctx->surface            = param->dst_surface;
        output_ctx->mpc_bg_color       = param->bg_color;
        output_ctx->opp_bg_color       = param->bg_color;
        output_ctx->target_rect        = param->target_rect;
        output_ctx->alpha_mode         = param->alpha_mode;
        output_ctx->flags.hdr_metadata = param->flags.hdr_metadata;
        output_ctx->hdr_metadata       = param->hdr_metadata;

        vpe_vector_clear(vpe_priv->vpe_cmd_vector);
        output_ctx->clamping_params = vpe_priv->init.debug.clamping_params;
    }


    if (status == VPE_STATUS_OK) {
        // blending support check
        status = populate_input_streams(vpe_priv, param, vpe_priv->stream_ctx);
        if (status != VPE_STATUS_OK)
            vpe_log("fail input stream population. status %d\n", (int)status);
    }

    if (status == VPE_STATUS_OK) {
        status = populate_virtual_streams(vpe_priv, param, vpe_priv->stream_ctx + vpe_priv->num_input_streams, vpe_priv->num_virtual_streams);
        if (status != VPE_STATUS_OK)
            vpe_log("fail virtual stream population. status %d\n", (int)status);
    }

    if (status == VPE_STATUS_OK) {
        status = vpe_priv->resource.calculate_segments(vpe_priv, param);
        if (status != VPE_STATUS_OK)
            vpe_log("failed in calculate segments %d\n", (int)status);
    }

    if (status == VPE_STATUS_OK) {
        // if the bg_color support is false, there is a flag to verify if the bg_color falls in the
        // output gamut
        if (!vpe_priv->pub.caps->bg_color_check_support) {
            status = vpe_priv->resource.check_bg_color_support(vpe_priv, &output_ctx->mpc_bg_color);
            if (status != VPE_STATUS_OK) {
                vpe_log(
                    "failed in checking the background color versus the output color space %d\n",
                    (int)status);
            }
        }
    }

    if (status == VPE_STATUS_OK) {
        // Calculate the buffer needed (worst case)
        vpe_priv->resource.get_bufs_req(vpe_priv, &vpe_priv->bufs_required);
        *req                  = vpe_priv->bufs_required;
        vpe_priv->ops_support = true;
    }

    if (status == VPE_STATUS_OK) {
        status = vpe_validate_geometric_scaling_support(param);
    }

    if (vpe_priv->init.debug.assert_when_not_support)
        VPE_ASSERT(status == VPE_STATUS_OK);

    vpe_event(VPE_EVENT_CHECK_SUPPORT, vpe_priv->num_streams, param->target_rect.width,
        param->target_rect.height, status);

    return status;
}

enum vpe_status vpe_build_noops(struct vpe *vpe, uint32_t num_dword, uint32_t **ppcmd_space)
{
    struct vpe_priv    *vpe_priv;
    struct cmd_builder *builder;
    enum vpe_status     status;

    if (!vpe || !ppcmd_space || ((*ppcmd_space) == NULL))
        return VPE_STATUS_ERROR;

    vpe_priv = container_of(vpe, struct vpe_priv, pub);

    builder = &vpe_priv->resource.cmd_builder;

    status = builder->build_noops(vpe_priv, ppcmd_space, num_dword);

    return status;
}

static bool validate_cached_param(struct vpe_priv *vpe_priv, const struct vpe_build_param *param)
{
    uint32_t           i;
    struct output_ctx *output_ctx;

    if (vpe_priv->num_input_streams != param->num_streams &&
       !(vpe_priv->init.debug.bg_color_fill_only == true && vpe_priv->num_streams == 1))
        return false;

    if (vpe_priv->collaboration_mode != param->collaboration_mode)
        return false;

    if (param->num_instances > 0 && vpe_priv->vpe_num_instance != param->num_instances)
        return false;

    for (i = 0; i < vpe_priv->num_input_streams; i++) {
        struct vpe_stream stream = param->streams[i];

        vpe_clip_stream(
            &stream.scaling_info.src_rect, &stream.scaling_info.dst_rect, &param->target_rect);

        if (memcmp(&vpe_priv->stream_ctx[i].stream, &stream, sizeof(struct vpe_stream)))
            return false;
    }

    output_ctx = &vpe_priv->output_ctx;
    if (output_ctx->alpha_mode != param->alpha_mode)
        return false;

    if (memcmp(&output_ctx->mpc_bg_color, &param->bg_color, sizeof(struct vpe_color)))
        return false;

    if (memcmp(&output_ctx->opp_bg_color, &param->bg_color, sizeof(struct vpe_color)))
        return false;

    if (memcmp(&output_ctx->target_rect, &param->target_rect, sizeof(struct vpe_rect)))
        return false;

    if (memcmp(&output_ctx->surface, &param->dst_surface, sizeof(struct vpe_surface_info)))
        return false;

    return true;
}

enum vpe_status vpe_build_commands(
    struct vpe *vpe, const struct vpe_build_param *param, struct vpe_build_bufs *bufs)
{
    struct vpe_priv      *vpe_priv;
    struct cmd_builder   *builder;
    enum vpe_status       status = VPE_STATUS_OK;
    uint32_t              cmd_idx, pipe_idx, stream_idx, cmd_type_idx;
    struct vpe_build_bufs curr_bufs;
    int64_t               cmd_buf_size;
    int64_t               emb_buf_size;
    uint64_t              cmd_buf_gpu_a, cmd_buf_cpu_a;
    uint64_t              emb_buf_gpu_a, emb_buf_cpu_a;
    struct vpe_vector    *config_vector;
    struct vpe_cmd_info  *cmd_info;

    if (!vpe || !param || !bufs)
        return VPE_STATUS_ERROR;

    vpe_priv = container_of(vpe, struct vpe_priv, pub);

    if (!vpe_priv->ops_support) {
        VPE_ASSERT(vpe_priv->ops_support);
        status = VPE_STATUS_NOT_SUPPORTED;
    }

    if (status == VPE_STATUS_OK) {
        if (!validate_cached_param(vpe_priv, param)) {
            status = VPE_STATUS_PARAM_CHECK_ERROR;
        }
    }

    if (status == VPE_STATUS_OK) {
        vpe_geometric_scaling_feature_skip(vpe_priv, param);

        if (bufs->cmd_buf.size == 0 || bufs->emb_buf.size == 0) {
            /* Here we directly return without setting ops_support to false
             *  becaues the supported check is already passed
             * and the caller can come again with correct buffer size.
             */
            bufs->cmd_buf.size = vpe_priv->bufs_required.cmd_buf_size;
            bufs->emb_buf.size = vpe_priv->bufs_required.emb_buf_size;

            return VPE_STATUS_OK;
        } else if ((bufs->cmd_buf.size < vpe_priv->bufs_required.cmd_buf_size) ||
                   (bufs->emb_buf.size < vpe_priv->bufs_required.emb_buf_size)) {
            status = VPE_STATUS_INVALID_BUFFER_SIZE;
        }
    }

    builder = &vpe_priv->resource.cmd_builder;

    // store buffers original values
    cmd_buf_cpu_a = bufs->cmd_buf.cpu_va;
    cmd_buf_gpu_a = bufs->cmd_buf.gpu_va;
    cmd_buf_size  = bufs->cmd_buf.size;

    emb_buf_cpu_a = bufs->emb_buf.cpu_va;
    emb_buf_gpu_a = bufs->emb_buf.gpu_va;
    emb_buf_size  = bufs->emb_buf.size;

    // curr_bufs is used for tracking the built size and next pointers
    curr_bufs = *bufs;

    // copy the param, reset saved configs
    for (stream_idx = 0; stream_idx < vpe_priv->num_streams; stream_idx++) {
        struct stream_ctx *stream_ctx = &vpe_priv->stream_ctx[stream_idx];

        for (pipe_idx = 0; pipe_idx < MAX_INPUT_PIPE; pipe_idx++) {
            config_vector = stream_ctx->configs[pipe_idx];
            if (config_vector)
                vpe_vector_clear(config_vector);

            for (cmd_type_idx = 0; cmd_type_idx < VPE_CMD_TYPE_COUNT; cmd_type_idx++) {
                config_vector = stream_ctx->stream_op_configs[pipe_idx][cmd_type_idx];
                if (config_vector)
                    vpe_vector_clear(config_vector);
            }
        }
    }

    for (pipe_idx = 0; pipe_idx < vpe_priv->pub.caps->resource_caps.num_cdc_be; pipe_idx++) {
        config_vector = vpe_priv->output_ctx.configs[pipe_idx];
        if (config_vector)
            vpe_vector_clear(config_vector);
    }

    // Reset pipes
    vpe_pipe_reset(vpe_priv);

    if (status == VPE_STATUS_OK) {
        status = vpe_color_update_color_space_and_tf(vpe_priv, param);
        if (status != VPE_STATUS_OK) {
            vpe_log("failed in updating color space and tf %d\n", (int)status);
        }
    }

    if (status == VPE_STATUS_OK) {
        status = vpe_color_update_movable_cm(vpe_priv, param);
        if (status != VPE_STATUS_OK) {
            vpe_log("failed in updating movable 3d lut unit %d\n", (int)status);
        }
    }

    if (status == VPE_STATUS_OK) {
        status = vpe_color_update_whitepoint(vpe_priv, param);
        if (status != VPE_STATUS_OK) {
            vpe_log("failed updating whitepoint gain %d\n", (int)status);
        }
    }

    if (status == VPE_STATUS_OK) {
        /* since the background is generated by the first stream,
         * the 3dlut enablement for the background color conversion
         * is used based on the information of the first stream.
         */
        vpe_bg_color_convert(vpe_priv->output_ctx.cs, vpe_priv->output_ctx.output_tf,
            vpe_priv->output_ctx.surface.format, &vpe_priv->output_ctx.mpc_bg_color,
            &vpe_priv->output_ctx.opp_bg_color, vpe_priv->stream_ctx[0].enable_3dlut);

        if (vpe_priv->collaboration_mode == true) {
            status = builder->build_collaborate_sync_cmd(vpe_priv, &curr_bufs);
            if (status != VPE_STATUS_OK) {
                vpe_log("failed in building collaborate sync cmd %d\n", (int)status);
            }
        }
        for (cmd_idx = 0; cmd_idx < vpe_priv->vpe_cmd_vector->num_elements; cmd_idx++) {
            status = builder->build_vpe_cmd(vpe_priv, &curr_bufs, cmd_idx);
            if (status != VPE_STATUS_OK) {
                vpe_log("failed in building vpe cmd %d\n", (int)status);
                break;
            }

            cmd_info = vpe_vector_get(vpe_priv->vpe_cmd_vector, cmd_idx);
            if (cmd_info == NULL) {
                status = VPE_STATUS_ERROR;
                break;
            }

            if ((vpe_priv->collaboration_mode == true) && (cmd_info->insert_end_csync == true)) {
                status = builder->build_collaborate_sync_cmd(vpe_priv, &curr_bufs);
                if (status != VPE_STATUS_OK) {
                    vpe_log("failed in building collaborate sync cmd %d\n", (int)status);
                    break;
                }

                // Add next collaborate sync start command when this vpe_cmd isn't the final one.
                if (cmd_idx < (uint32_t)(vpe_priv->vpe_cmd_vector->num_elements - 1)) {
                    status = builder->build_collaborate_sync_cmd(vpe_priv, &curr_bufs);
                    if (status != VPE_STATUS_OK) {
                        vpe_log("failed in building collaborate sync cmd %d\n", (int)status);
                        break;
                    }
                }
            }
        }
        if ((status == VPE_STATUS_OK) && (vpe_priv->collaboration_mode == true)) {
            status = builder->build_collaborate_sync_cmd(vpe_priv, &curr_bufs);
            if (status != VPE_STATUS_OK) {
                vpe_log("failed in building collaborate sync cmd %d\n", (int)status);
            }
        }
    }

    if (status == VPE_STATUS_OK) {
        bufs->cmd_buf.size   = cmd_buf_size - curr_bufs.cmd_buf.size; // used cmd buffer size
        bufs->cmd_buf.gpu_va = cmd_buf_gpu_a;
        bufs->cmd_buf.cpu_va = cmd_buf_cpu_a;

        bufs->emb_buf.size   = emb_buf_size - curr_bufs.emb_buf.size; // used emb buffer size
        bufs->emb_buf.gpu_va = emb_buf_gpu_a;
        bufs->emb_buf.cpu_va = emb_buf_cpu_a;
    }

    vpe_priv->ops_support = false;

    if (vpe_priv->init.debug.assert_when_not_support)
        VPE_ASSERT(status == VPE_STATUS_OK);

    return status;
}

void vpe_get_optimal_num_of_taps(struct vpe *vpe, struct vpe_scaling_info *scaling_info)
{
    struct vpe_priv *vpe_priv;
    struct dpp      *dpp;

    vpe_priv = container_of(vpe, struct vpe_priv, pub);
    dpp      = vpe_priv->resource.dpp[0];

    dpp->funcs->get_optimal_number_of_taps(
        &scaling_info->src_rect, &scaling_info->dst_rect, &scaling_info->taps);
}
