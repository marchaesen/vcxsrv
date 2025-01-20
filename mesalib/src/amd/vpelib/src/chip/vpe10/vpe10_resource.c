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
#include "vpe_priv.h"
#include "common.h"
#include "vpe10_resource.h"
#include "vpe10_cmd_builder.h"
#include "vpe10_vpec.h"
#include "vpe10_cdc_fe.h"
#include "vpe10_cdc_be.h"
#include "vpe10_dpp.h"
#include "vpe10_mpc.h"
#include "vpe10_opp.h"
#include "vpe10_background.h"
#include "vpe10_vpe_desc_writer.h"
#include "vpe10_plane_desc_writer.h"
#include "vpe10_config_writer.h"
#include "vpe10/inc/asic/bringup_vpe_6_1_0_offset.h"
#include "vpe10/inc/asic/bringup_vpe_6_1_0_sh_mask.h"
#include "vpe10/inc/asic/bringup_vpe_6_1_0_default.h"
#include "vpe10/inc/asic/vpe_1_0_offset.h"
#include "custom_fp16.h"
#include "custom_float.h"
#include "background.h"
#include "vpe_visual_confirm.h"
#include "color_bg.h"

#define LUT_NUM_ENTRIES   (17 * 17 * 17)
#define LUT_ENTRY_SIZE    (2)
#define LUT_NUM_COMPONENT (3)
#define LUT_BUFFER_SIZE   (LUT_NUM_ENTRIES * LUT_ENTRY_SIZE * LUT_NUM_COMPONENT)
// set field/register/bitfield name
#define SFRB(field_name, reg_name, post_fix) .field_name = reg_name##__##field_name##post_fix

#define BASE_INNER(seg_id) VPE_BASE__INST0_SEG##seg_id

#define BASE(seg_id) BASE_INNER(seg_id)

// set register with block id and default val, init lastWrittenVal as default while isWritten set to
// false
#define SRIDFVL(reg_name, block, id)                                                               \
    .reg_name = {BASE(reg##reg_name##_BASE_IDX) + reg##reg_name, reg##reg_name##_##DEFAULT,        \
        reg##reg_name##_##DEFAULT, false}

#define SRIDFVL1(reg_name)                                                                          \
    .reg_name = {BASE(reg##reg_name##_BASE_IDX) + reg##reg_name, reg##reg_name##_##DEFAULT,         \
        reg##reg_name##_##DEFAULT, false}

#define SRIDFVL2(reg_name, block, id)                                                                  \
    .block##_##reg_name = {BASE(reg##block##id##_##reg_name##_BASE_IDX) + reg##block##id##_##reg_name, \
        reg##block##id##_##reg_name##_##DEFAULT, reg##block##id##_##reg_name##_##DEFAULT, false}

#define SRIDFVL3(reg_name, block, id)                                                                  \
    .block##_##reg_name = {BASE(reg##block##_##reg_name##_BASE_IDX) + reg##block##_##reg_name,         \
        reg##block##_##reg_name##_##DEFAULT, reg##block##_##reg_name##_##DEFAULT, false}

/***************** CDC FE registers ****************/
#define cdc_fe_regs(id) [id] = {CDC_FE_REG_LIST_VPE10(id)}

static struct vpe10_cdc_fe_registers cdc_fe_regs[] = {cdc_fe_regs(0)};

static const struct vpe10_cdc_fe_shift cdc_fe_shift = {CDC_FE_FIELD_LIST_VPE10(__SHIFT)};

static const struct vpe10_cdc_fe_mask cdc_fe_mask = {CDC_FE_FIELD_LIST_VPE10(_MASK)};

/***************** CDC BE registers ****************/
#define cdc_be_regs(id) [id] = {CDC_BE_REG_LIST_VPE10(id)}

static struct vpe10_cdc_be_registers cdc_be_regs[] = {cdc_be_regs(0)};

static const struct vpe10_cdc_be_shift cdc_be_shift = {CDC_BE_FIELD_LIST_VPE10(__SHIFT)};

static const struct vpe10_cdc_be_mask cdc_be_mask = {CDC_BE_FIELD_LIST_VPE10(_MASK)};

/***************** DPP registers ****************/
#define dpp_regs(id) [id] = {DPP_REG_LIST_VPE10(id)}

static struct vpe10_dpp_registers dpp_regs[] = {dpp_regs(0)};

static const struct vpe10_dpp_shift dpp_shift = {DPP_FIELD_LIST_VPE10(__SHIFT)};

static const struct vpe10_dpp_mask dpp_mask = {DPP_FIELD_LIST_VPE10(_MASK)};

/***************** MPC registers ****************/
#define mpc_regs(id) [id] = {MPC_REG_LIST_VPE10(id)}

static struct vpe10_mpc_registers mpc_regs[] = {mpc_regs(0)};

static const struct vpe10_mpc_shift mpc_shift = {MPC_FIELD_LIST_VPE10(__SHIFT)};

static const struct vpe10_mpc_mask mpc_mask = {MPC_FIELD_LIST_VPE10(_MASK)};

/***************** OPP registers ****************/
#define opp_regs(id) [id] = {OPP_REG_LIST_VPE10(id)}

static struct vpe10_opp_registers opp_regs[] = {opp_regs(0)};

static const struct vpe10_opp_shift opp_shift = {OPP_FIELD_LIST_VPE10(__SHIFT)};

static const struct vpe10_opp_mask opp_mask = {OPP_FIELD_LIST_VPE10(_MASK)};

static struct vpe_caps caps = {
    .lut_size               = LUT_BUFFER_SIZE,
    .rotation_support       = 0,
    .h_mirror_support       = 1,
    .v_mirror_support       = 0,
    .is_apu                 = 1,
    .bg_color_check_support = 0,
    .resource_caps =
        {
            .num_dpp       = 1,
            .num_opp       = 1,
            .num_mpc_3dlut = 1,
            .num_queue     = 8,
            .num_cdc_be    = 1,
        },
    .color_caps = {.dpp =
                       {
                           .pre_csc    = 1,
                           .luma_key   = 0,
                           .color_key  = 1,
                           .dgam_ram   = 0,
                           .post_csc   = 1,
                           .gamma_corr = 1,
                           .hw_3dlut   = 1,
                           .ogam_ram   = 1, /**< programmable gam in output -> gamma_corr */
                           .ocsc       = 0,
                           .dgam_rom_caps =
                               {
                                   .srgb     = 1,
                                   .bt2020   = 1,
                                   .gamma2_2 = 1,
                                   .pq       = 1,
                                   .hlg      = 1,
                               },
                       },
        .mpc =
            {
                .gamut_remap         = 1,
                .ogam_ram            = 1,
                .ocsc                = 1,
                .shared_3d_lut       = 1,
                .global_alpha        = 1,
                .top_bottom_blending = 0,
            }},
    .plane_caps =
        {
            .per_pixel_alpha = 1,
            .input_pixel_format_support =
                {
                    .argb_packed_32b = 1,
                    .nv12            = 1,
                    .fp16            = 0,
                    .p010            = 1, /**< planar 4:2:0 10-bit */
                    .p016            = 0, /**< planar 4:2:0 16-bit */
                    .ayuv            = 0, /**< packed 4:4:4 */
                    .yuy2 = 0
                },
            .output_pixel_format_support =
                {
                    .argb_packed_32b = 1,
                    .nv12            = 0,
                    .fp16            = 1,
                    .p010            = 0, /**< planar 4:2:0 10-bit */
                    .p016            = 0, /**< planar 4:2:0 16-bit */
                    .ayuv            = 0, /**< packed 4:4:4 */
                    .yuy2 = 0
                },
            .max_upscale_factor = 64000,

            /*
             * 4:1 downscaling ratio : 1000 / 4 = 250
             * vpelib does not support more than 4:1 to preserve quality
             * due to the limitation of using maximum number of 8 taps
             */
            .max_downscale_factor = 250,

            .pitch_alignment    = 256,
            .addr_alignment     = 256,
            .max_viewport_width = 1024,
        },
};

static bool vpe10_init_scaler_data(struct vpe_priv *vpe_priv, struct stream_ctx *stream_ctx,
    struct scaler_data *scl_data, struct vpe_rect *src_rect, struct vpe_rect *dst_rect)
{
    struct dpp *dpp;
    dpp = vpe_priv->resource.dpp[0];

    calculate_scaling_ratios(scl_data, src_rect, dst_rect, stream_ctx->stream.surface_info.format);

    scl_data->taps.v_taps   = stream_ctx->stream.scaling_info.taps.v_taps;
    scl_data->taps.h_taps   = stream_ctx->stream.scaling_info.taps.h_taps;
    scl_data->taps.v_taps_c = stream_ctx->stream.scaling_info.taps.v_taps_c;
    scl_data->taps.h_taps_c = stream_ctx->stream.scaling_info.taps.h_taps_c;
    if (!vpe_priv->init.debug.skip_optimal_tap_check) {
        if (!dpp->funcs->get_optimal_number_of_taps(src_rect, dst_rect, &scl_data->taps)) {
            return false;
        }
    }

    if ((stream_ctx->stream.use_external_scaling_coeffs ==
            false) || /* don't try to optimize is the scaler is configured externally*/
        (stream_ctx->stream.polyphase_scaling_coeffs.taps.h_taps == 0) ||
        (stream_ctx->stream.polyphase_scaling_coeffs.taps.v_taps == 0)) {
        scl_data->polyphase_filter_coeffs = 0;
    } else {
        if ((stream_ctx->stream.polyphase_scaling_coeffs.taps.h_taps !=
                stream_ctx->stream.scaling_info.taps.h_taps) ||
            (stream_ctx->stream.polyphase_scaling_coeffs.taps.v_taps !=
                stream_ctx->stream.scaling_info.taps.v_taps)) {
            return false; // sanity check to make sure the taps structures are the same
        }
        scl_data->taps = stream_ctx->stream.polyphase_scaling_coeffs
                             .taps; /* use the extenally provided tap configuration*/
        scl_data->polyphase_filter_coeffs = &stream_ctx->stream.polyphase_scaling_coeffs;
    }
    // bypass scaler if all ratios are 1
    if (IDENTITY_RATIO(scl_data->ratios.horz))
        scl_data->taps.h_taps = 1;
    if (IDENTITY_RATIO(scl_data->ratios.vert))
        scl_data->taps.v_taps = 1;

    return true;
}

enum vpe_status vpe10_set_num_segments(struct vpe_priv *vpe_priv, struct stream_ctx *stream_ctx,
    struct scaler_data *scl_data, struct vpe_rect *src_rect, struct vpe_rect *dst_rect,
    uint32_t *max_seg_width)
{

    uint16_t       num_segs;
    struct dpp    *dpp         = vpe_priv->resource.dpp[0];
    const uint32_t max_lb_size = dpp->funcs->get_line_buffer_size();

    *max_seg_width = min(*max_seg_width, max_lb_size / scl_data->taps.v_taps);

    num_segs = vpe_get_num_segments(vpe_priv, src_rect, dst_rect, *max_seg_width);

    stream_ctx->segment_ctx = vpe_alloc_segment_ctx(vpe_priv, num_segs);
    if (!stream_ctx->segment_ctx)
        return VPE_STATUS_NO_MEMORY;

    stream_ctx->num_segments = num_segs;

    return VPE_STATUS_OK;
}

bool vpe10_get_dcc_compression_output_cap(const struct vpe *vpe, const struct vpe_dcc_surface_param *params, struct vpe_surface_dcc_cap *cap)
{
    cap->capable = false;
    return cap->capable;
}

bool vpe10_get_dcc_compression_input_cap(const struct vpe *vpe, const struct vpe_dcc_surface_param *params, struct vpe_surface_dcc_cap *cap)
{
    cap->capable = false;
    return cap->capable;
}

static struct vpe_cap_funcs cap_funcs =
{
    .get_dcc_compression_output_cap = vpe10_get_dcc_compression_output_cap,
    .get_dcc_compression_input_cap  = vpe10_get_dcc_compression_input_cap
};

struct cdc_fe *vpe10_cdc_fe_create(struct vpe_priv *vpe_priv, int inst)
{
    struct vpe10_cdc_fe *vpe10_cdc_fe = vpe_zalloc(sizeof(struct vpe10_cdc_fe));

    if (!vpe10_cdc_fe)
        return NULL;

    vpe10_construct_cdc_fe(vpe_priv, &vpe10_cdc_fe->base);

    vpe10_cdc_fe->regs  = &cdc_fe_regs[inst];
    vpe10_cdc_fe->mask  = &cdc_fe_mask;
    vpe10_cdc_fe->shift = &cdc_fe_shift;

    return &vpe10_cdc_fe->base;
}

struct cdc_be *vpe10_cdc_be_create(struct vpe_priv *vpe_priv, int inst)
{
    struct vpe10_cdc_be *vpe10_cdc_be = vpe_zalloc(sizeof(struct vpe10_cdc_be));

    if (!vpe10_cdc_be)
        return NULL;

    vpe10_construct_cdc_be(vpe_priv, &vpe10_cdc_be->base);

    vpe10_cdc_be->regs  = &cdc_be_regs[inst];
    vpe10_cdc_be->mask  = &cdc_be_mask;
    vpe10_cdc_be->shift = &cdc_be_shift;

    return &vpe10_cdc_be->base;
}

struct dpp *vpe10_dpp_create(struct vpe_priv *vpe_priv, int inst)
{
    struct vpe10_dpp *vpe10_dpp = vpe_zalloc(sizeof(struct vpe10_dpp));

    if (!vpe10_dpp)
        return NULL;

    vpe10_construct_dpp(vpe_priv, &vpe10_dpp->base);

    vpe10_dpp->regs  = &dpp_regs[inst];
    vpe10_dpp->mask  = &dpp_mask;
    vpe10_dpp->shift = &dpp_shift;

    return &vpe10_dpp->base;
}

struct mpc *vpe10_mpc_create(struct vpe_priv *vpe_priv, int inst)
{
    struct vpe10_mpc *vpe10_mpc = vpe_zalloc(sizeof(struct vpe10_mpc));

    if (!vpe10_mpc)
        return NULL;

    vpe10_construct_mpc(vpe_priv, &vpe10_mpc->base);

    vpe10_mpc->regs  = &mpc_regs[inst];
    vpe10_mpc->mask  = &mpc_mask;
    vpe10_mpc->shift = &mpc_shift;

    return &vpe10_mpc->base;
}

struct opp *vpe10_opp_create(struct vpe_priv *vpe_priv, int inst)
{
    struct vpe10_opp *vpe10_opp = vpe_zalloc(sizeof(struct vpe10_opp));

    if (!vpe10_opp)
        return NULL;

    vpe10_construct_opp(vpe_priv, &vpe10_opp->base);

    vpe10_opp->regs  = &opp_regs[inst];
    vpe10_opp->mask  = &opp_mask;
    vpe10_opp->shift = &opp_shift;

    return &vpe10_opp->base;
}

enum vpe_status vpe10_construct_resource(struct vpe_priv *vpe_priv, struct resource *res)
{
    struct vpe *vpe = &vpe_priv->pub;

    vpe->caps      = &caps;
    vpe->cap_funcs = &cap_funcs;

    vpe10_construct_vpec(vpe_priv, &res->vpec);

    res->cdc_fe[0] = vpe10_cdc_fe_create(vpe_priv, 0);
    if (!res->cdc_fe[0])
        goto err;

    res->dpp[0] = vpe10_dpp_create(vpe_priv, 0);
    if (!res->dpp[0])
        goto err;

    res->mpc[0] = vpe10_mpc_create(vpe_priv, 0);
    if (!res->mpc[0])
        goto err;

    res->cdc_be[0] = vpe10_cdc_be_create(vpe_priv, 0);
    if (!res->cdc_be[0])
        goto err;

    res->opp[0] = vpe10_opp_create(vpe_priv, 0);
    if (!res->opp[0])
        goto err;

    vpe10_construct_cmd_builder(vpe_priv, &res->cmd_builder);
    vpe10_construct_vpe_desc_writer(&vpe_priv->vpe_desc_writer);
    vpe10_construct_plane_desc_writer(&vpe_priv->plane_desc_writer);
    vpe10_config_writer_init(&vpe_priv->config_writer);

    vpe_priv->num_pipe = 1;

    res->internal_hdr_normalization = 1;

    res->check_input_color_space           = vpe10_check_input_color_space;
    res->check_output_color_space          = vpe10_check_output_color_space;
    res->check_h_mirror_support            = vpe10_check_h_mirror_support;
    res->calculate_segments                = vpe10_calculate_segments;
    res->set_num_segments                  = vpe10_set_num_segments;
    res->split_bg_gap                      = vpe10_split_bg_gap;
    res->calculate_dst_viewport_and_active = vpe10_calculate_dst_viewport_and_active;
    res->find_bg_gaps                      = vpe_find_bg_gaps;
    res->create_bg_segments                = vpe_create_bg_segments;
    res->populate_cmd_info                 = vpe10_populate_cmd_info;
    res->program_frontend                  = vpe10_program_frontend;
    res->program_backend                   = vpe10_program_backend;
    res->get_bufs_req                      = vpe10_get_bufs_req;
    res->check_bg_color_support            = vpe10_check_bg_color_support;
    res->check_mirror_rotation_support     = vpe10_check_mirror_rotation_support;
    res->update_blnd_gamma                 = vpe10_update_blnd_gamma;

    return VPE_STATUS_OK;
err:
    vpe10_destroy_resource(vpe_priv, res);
    return VPE_STATUS_ERROR;
}

void vpe10_destroy_resource(struct vpe_priv *vpe_priv, struct resource *res)
{
    if (res->cdc_fe[0] != NULL) {
        vpe_free(container_of(res->cdc_fe[0], struct vpe10_cdc_fe, base));
        res->cdc_fe[0] = NULL;
    }

    if (res->dpp[0] != NULL) {
        vpe_free(container_of(res->dpp[0], struct vpe10_dpp, base));
        res->dpp[0] = NULL;
    }

    if (res->mpc[0] != NULL) {
        vpe_free(container_of(res->mpc[0], struct vpe10_mpc, base));
        res->mpc[0] = NULL;
    }

    if (res->cdc_be[0] != NULL) {
        vpe_free(container_of(res->cdc_be[0], struct vpe10_cdc_be, base));
        res->cdc_be[0] = NULL;
    }

    if (res->opp[0] != NULL) {
        vpe_free(container_of(res->opp[0], struct vpe10_opp, base));
        res->opp[0] = NULL;
    }
}

bool vpe10_check_input_color_space(struct vpe_priv *vpe_priv, enum vpe_surface_pixel_format format,
    const struct vpe_color_space *vcs)
{
    enum color_space         cs;
    enum color_transfer_func tf;

    vpe_color_get_color_space_and_tf(vcs, &cs, &tf);
    if (cs == COLOR_SPACE_UNKNOWN || tf == TRANSFER_FUNC_UNKNOWN)
        return false;

    return true;
}

bool vpe10_check_output_color_space(struct vpe_priv *vpe_priv, enum vpe_surface_pixel_format format,
    const struct vpe_color_space *vcs)
{
    enum color_space         cs;
    enum color_transfer_func tf;

    // packed 32bit rgb
    if (vcs->encoding != VPE_PIXEL_ENCODING_RGB)
        return false;

    vpe_color_get_color_space_and_tf(vcs, &cs, &tf);
    if (cs == COLOR_SPACE_UNKNOWN || tf == TRANSFER_FUNC_UNKNOWN)
        return false;

    if (vpe_is_fp16(format) && tf != TRANSFER_FUNC_LINEAR)
        return false;

    return true;
}

bool vpe10_check_h_mirror_support(bool *input_mirror, bool *output_mirror)
{
    *input_mirror  = false;
    *output_mirror = true;
    return true;
}

void vpe10_calculate_dst_viewport_and_active(
    struct segment_ctx *segment_ctx, uint32_t max_seg_width)
{
    struct scaler_data *data        = &segment_ctx->scaler_data;
    struct stream_ctx  *stream_ctx  = segment_ctx->stream_ctx;
    struct vpe_priv    *vpe_priv    = stream_ctx->vpe_priv;
    struct vpe_rect    *dst_rect    = &stream_ctx->stream.scaling_info.dst_rect;
    struct vpe_rect    *target_rect = &vpe_priv->output_ctx.target_rect;

    uint32_t vpc_div = vpe_is_yuv420(vpe_priv->output_ctx.surface.format) ? 2 : 1;

    data->dst_viewport.x     = data->recout.x + dst_rect->x;
    data->dst_viewport.width = data->recout.width;

    // 1st stream will cover the background
    // extends the v_active to cover the full target_rect's height
    if (stream_ctx->stream_idx == 0) {
        data->recout.x            = 0;
        data->recout.y            = dst_rect->y - target_rect->y;
        data->dst_viewport.y      = target_rect->y;
        data->dst_viewport.height = target_rect->height;

        if (!stream_ctx->flip_horizonal_output) {
            /* first segment :
             * if the dst_viewport.width is not 1024,
             * and we need background on the left, extend the active to cover as much as it can
             */
            if (segment_ctx->segment_idx == 0) {
                uint32_t remain_gap = min(max_seg_width - data->dst_viewport.width,
                    (uint32_t)(data->dst_viewport.x - target_rect->x));
                data->recout.x      = (int32_t)remain_gap;

                data->dst_viewport.x -= (int32_t)remain_gap;
                data->dst_viewport.width += remain_gap;
            }
            // last segment
            if (segment_ctx->segment_idx == stream_ctx->num_segments - 1) {
                uint32_t remain_gap = min(max_seg_width - data->dst_viewport.width,
                    (uint32_t)((target_rect->x + (int32_t)target_rect->width) -
                               (data->dst_viewport.x + (int32_t)data->dst_viewport.width)));

                data->dst_viewport.width += remain_gap;
            }
        }
    } else {
        data->dst_viewport.y      = data->recout.y + dst_rect->y;
        data->dst_viewport.height = data->recout.height;
        data->recout.y            = 0;
        data->recout.x            = 0;
    }

    data->dst_viewport_c.x      = data->dst_viewport.x / (int32_t)vpc_div;
    data->dst_viewport_c.y      = data->dst_viewport.y / (int32_t)vpc_div;
    data->dst_viewport_c.width  = data->dst_viewport.width / vpc_div;
    data->dst_viewport_c.height = data->dst_viewport.height / vpc_div;

    // [h/v]_active
    data->h_active = data->dst_viewport.width;
    data->v_active = data->dst_viewport.height;
}


static uint16_t get_max_gap_num(
    struct vpe_priv* vpe_priv, const struct vpe_build_param* params, uint32_t max_seg_width)
{
    const uint16_t num_multiple = vpe_priv->vpe_num_instance ? vpe_priv->vpe_num_instance : 1;
    bool is_color_fill = (vpe_priv->num_streams == 1) && (vpe_priv->stream_ctx[0].stream_type == VPE_STREAM_TYPE_BG_GEN);

    uint16_t max_gaps =
        (uint16_t)(max((params->target_rect.width + max_seg_width - 1) / max_seg_width, 1));

    /* If the stream width is less than max_seg_width - 1024, and it
    * lies inside a max_seg_width window of the background, vpe needs
    * an extra bg segment to store that.
       1    2  3  4   5
    |....|....|.**.|....|
    |....|....|.**.|....|
    |....|....|.**.|....|

     (*: stream
      .: background
      |: 1k separator)

    */

    if (!is_color_fill) {
        // full colorfillOnly case, no need to + 1 as the gap won't be seaprated by stream dst
        // for non-colorfillOnly case, +1 for worst case the gap is separated by stream dst
        max_gaps += 1;
    }

    if (max_gaps % num_multiple > 0) {
        max_gaps += num_multiple - (max_gaps % num_multiple);
    }

    return max_gaps;
}

enum vpe_status vpe10_calculate_segments(
    struct vpe_priv *vpe_priv, const struct vpe_build_param *params)
{
    enum vpe_status     res;
    struct vpe_rect    *gaps;
    uint16_t            gaps_cnt, max_gaps;
    uint16_t            stream_idx, seg_idx;
    struct stream_ctx  *stream_ctx;
    struct segment_ctx *segment_ctx;
    uint32_t            max_seg_width = vpe_priv->pub.caps->plane_caps.max_viewport_width;
    struct scaler_data  scl_data;
    struct vpe_rect    *src_rect;
    struct vpe_rect    *dst_rect;
    uint32_t            factor;
    const uint32_t      max_upscale_factor   = vpe_priv->pub.caps->plane_caps.max_upscale_factor;
    const uint32_t      max_downscale_factor = vpe_priv->pub.caps->plane_caps.max_downscale_factor;
    struct dpp         *dpp                  = vpe_priv->resource.dpp[0];
    const uint32_t      max_lb_size          = dpp->funcs->get_line_buffer_size();

    for (stream_idx = 0; stream_idx < vpe_priv->num_streams; stream_idx++) {
        stream_ctx = &vpe_priv->stream_ctx[stream_idx];
        src_rect   = &stream_ctx->stream.scaling_info.src_rect;
        dst_rect   = &stream_ctx->stream.scaling_info.dst_rect;

        if (stream_ctx->stream_type == VPE_STREAM_TYPE_BG_GEN)
            continue;

        if (src_rect->width < VPE_MIN_VIEWPORT_SIZE || src_rect->height < VPE_MIN_VIEWPORT_SIZE ||
            dst_rect->width < VPE_MIN_VIEWPORT_SIZE || dst_rect->height < VPE_MIN_VIEWPORT_SIZE) {
            return VPE_STATUS_VIEWPORT_SIZE_NOT_SUPPORTED;
        }

        vpe_clip_stream(src_rect, dst_rect, &params->target_rect);

        if (src_rect->width <= 0 || src_rect->height <= 0 || dst_rect->width <= 0 ||
            dst_rect->height <= 0) {
            vpe_log("calculate_segments: after clipping, src or dst rect contains no area. Skip "
                    "this stream.\n");
            stream_ctx->num_segments = 0;
            continue;
        }

        /* If the source frame size in either dimension is 1 then the scaling ratio becomes 0
         * in that dimension. If destination frame size in any dimesnion is 1 the scaling ratio
         * is NAN.
         */
        if (src_rect->width < VPE_MIN_VIEWPORT_SIZE || src_rect->height < VPE_MIN_VIEWPORT_SIZE ||
            dst_rect->width < VPE_MIN_VIEWPORT_SIZE || dst_rect->height < VPE_MIN_VIEWPORT_SIZE) {
            return VPE_STATUS_VIEWPORT_SIZE_NOT_SUPPORTED;
        }
        factor = (uint32_t)vpe_fixpt_ceil(
            vpe_fixpt_from_fraction((1000 * dst_rect->width), src_rect->width));
        if (factor > max_upscale_factor || factor < max_downscale_factor)
            return VPE_STATUS_SCALING_RATIO_NOT_SUPPORTED;

        // initialize scaling data
        if (!vpe10_init_scaler_data(vpe_priv, stream_ctx, &scl_data, src_rect, dst_rect))
            return VPE_STATUS_SCALING_RATIO_NOT_SUPPORTED;

        res = vpe_priv->resource.set_num_segments(
            vpe_priv, stream_ctx, &scl_data, src_rect, dst_rect, &max_seg_width);
        if (res != VPE_STATUS_OK)
            return res;

        for (seg_idx = 0; seg_idx < stream_ctx->num_segments; seg_idx++) {
            segment_ctx              = &stream_ctx->segment_ctx[seg_idx];
            segment_ctx->segment_idx = seg_idx;
            segment_ctx->stream_ctx  = stream_ctx;

            segment_ctx->scaler_data.ratios = scl_data.ratios;
            segment_ctx->scaler_data.taps   = scl_data.taps;
            if (stream_ctx->stream.use_external_scaling_coeffs) {
                segment_ctx->scaler_data.polyphase_filter_coeffs =
                    &stream_ctx->stream.polyphase_scaling_coeffs;
            } else {
                segment_ctx->scaler_data.polyphase_filter_coeffs = 0;
            }
            res = vpe_resource_build_scaling_params(segment_ctx);
            if (res != VPE_STATUS_OK)
                return res;

            vpe_priv->resource.calculate_dst_viewport_and_active(segment_ctx, max_seg_width);
        }
    }

    max_seg_width = vpe_priv->pub.caps->plane_caps.max_viewport_width;
 
    max_gaps = get_max_gap_num(vpe_priv, params, max_seg_width);

    gaps = vpe_zalloc(sizeof(struct vpe_rect) * max_gaps);
    if (!gaps)
        return VPE_STATUS_NO_MEMORY;

    gaps_cnt = vpe_priv->resource.find_bg_gaps(vpe_priv, &(params->target_rect), gaps, max_gaps);

    if (gaps_cnt > 0)
        vpe_priv->resource.create_bg_segments(vpe_priv, gaps, gaps_cnt, VPE_CMD_OPS_BG);

    if (gaps != NULL) {
        vpe_free(gaps);
        gaps = NULL;
    }

    vpe_handle_output_h_mirror(vpe_priv);

    res = vpe_priv->resource.populate_cmd_info(vpe_priv);

    if (res == VPE_STATUS_OK)
        res = vpe_create_visual_confirm_segs(vpe_priv, params, max_seg_width);

    return res;
}

static void build_clamping_params(
    struct opp *opp, struct clamping_and_pixel_encoding_params *clamping)
{
    struct vpe_priv         *vpe_priv     = opp->vpe_priv;
    struct vpe_surface_info *dst_surface  = &vpe_priv->output_ctx.surface;
    enum vpe_color_range     output_range = dst_surface->cs.range;

    memset(clamping, 0, sizeof(*clamping));
    clamping->clamping_level = CLAMPING_FULL_RANGE;
    clamping->c_depth        = vpe_get_color_depth(dst_surface->format);
    if (output_range == VPE_COLOR_RANGE_STUDIO) {
        if (!vpe_priv->init.debug.clamping_setting) {
            switch (clamping->c_depth) {
            case COLOR_DEPTH_888:
                clamping->clamping_level = CLAMPING_LIMITED_RANGE_8BPC;
                break;
            case COLOR_DEPTH_101010:
                clamping->clamping_level = CLAMPING_LIMITED_RANGE_10BPC;
                break;
            case COLOR_DEPTH_121212:
                clamping->clamping_level = CLAMPING_LIMITED_RANGE_12BPC;
                break;
            default:
                clamping->clamping_level =
                    CLAMPING_FULL_RANGE; // for all the others bit depths set the full range
                break;
            }
        } else {
            switch (vpe_priv->init.debug.clamping_params.clamping_range) {
            case VPE_CLAMPING_LIMITED_RANGE_8BPC:
                clamping->clamping_level = CLAMPING_LIMITED_RANGE_8BPC;
                break;
            case VPE_CLAMPING_LIMITED_RANGE_10BPC:
                clamping->clamping_level = CLAMPING_LIMITED_RANGE_10BPC;
                break;
            case VPE_CLAMPING_LIMITED_RANGE_12BPC:
                clamping->clamping_level = CLAMPING_LIMITED_RANGE_12BPC;
                break;
            default:
                clamping->clamping_level =
                    CLAMPING_LIMITED_RANGE_PROGRAMMABLE; // for all the others set to programmable
                                                         // range
                clamping->r_clamp_component_lower =
                    vpe_priv->output_ctx.clamping_params.r_clamp_component_lower;
                clamping->g_clamp_component_lower =
                    vpe_priv->output_ctx.clamping_params.g_clamp_component_lower;
                clamping->b_clamp_component_lower =
                    vpe_priv->output_ctx.clamping_params.b_clamp_component_lower;
                clamping->r_clamp_component_upper =
                    vpe_priv->output_ctx.clamping_params.r_clamp_component_upper;
                clamping->g_clamp_component_upper =
                    vpe_priv->output_ctx.clamping_params.g_clamp_component_upper;
                clamping->b_clamp_component_upper =
                    vpe_priv->output_ctx.clamping_params.b_clamp_component_upper;
                break;
            }
        }
    }
}

int32_t vpe10_program_frontend(struct vpe_priv *vpe_priv, uint32_t pipe_idx, uint32_t cmd_idx,
    uint32_t cmd_input_idx, bool seg_only)
{
    struct vpe_cmd_info *cmd_info = vpe_vector_get(vpe_priv->vpe_cmd_vector, cmd_idx);
    VPE_ASSERT(cmd_info);

    struct vpe_cmd_input      *cmd_input    = &cmd_info->inputs[cmd_input_idx];
    struct stream_ctx         *stream_ctx   = &vpe_priv->stream_ctx[cmd_input->stream_idx];
    struct vpe_surface_info   *surface_info = &stream_ctx->stream.surface_info;
    struct cdc_fe             *cdc_fe       = vpe_priv->resource.cdc_fe[pipe_idx];
    struct dpp                *dpp          = vpe_priv->resource.dpp[pipe_idx];
    struct mpc                *mpc          = vpe_priv->resource.mpc[pipe_idx];
    enum input_csc_select      select       = INPUT_CSC_SELECT_BYPASS;
    uint32_t                   hw_mult      = 0;
    struct cnv_keyer_params    keyer_params;
    struct custom_float_format fmt;

    vpe_priv->fe_cb_ctx.stream_idx = cmd_input->stream_idx;
    vpe_priv->fe_cb_ctx.vpe_priv   = vpe_priv;

    config_writer_set_callback(
        &vpe_priv->config_writer, &vpe_priv->fe_cb_ctx, vpe_frontend_config_callback);

    config_writer_set_type(&vpe_priv->config_writer, CONFIG_TYPE_DIRECT, pipe_idx);

    if (!seg_only) {
        /* start front-end programming that can be shared among segments */
        vpe_priv->fe_cb_ctx.stream_sharing = true;

        cdc_fe->funcs->program_surface_config(cdc_fe, surface_info->format,
            stream_ctx->stream.rotation,
            // set to false as h_mirror is not supported by input, only supported in output
            false, surface_info->swizzle);
        cdc_fe->funcs->program_crossbar_config(cdc_fe, surface_info->format);

        dpp->funcs->program_cnv(dpp, surface_info->format, vpe_priv->expansion_mode);
        if (stream_ctx->bias_scale)
            dpp->funcs->program_cnv_bias_scale(dpp, stream_ctx->bias_scale);

        dpp->funcs->build_keyer_params(dpp, stream_ctx, &keyer_params);
        dpp->funcs->program_alpha_keyer(dpp, &keyer_params);

        /* If input adjustment exists, program the ICSC with those values. */
        if (stream_ctx->input_cs) {
            select = INPUT_CSC_SELECT_ICSC;
            dpp->funcs->program_post_csc(dpp, stream_ctx->cs, select, stream_ctx->input_cs);
        } else {
            dpp->funcs->program_post_csc(dpp, stream_ctx->cs, select, NULL);
        }
        dpp->funcs->program_input_transfer_func(dpp, stream_ctx->input_tf);
        dpp->funcs->program_gamut_remap(dpp, stream_ctx->gamut_remap);

        // for not bypass mode, we always are in single layer coming from DPP and output to OPP
        mpc->funcs->program_mpcc_mux(mpc, MPC_MPCCID_0, MPC_MUX_TOPSEL_DPP0, MPC_MUX_BOTSEL_DISABLE,
            MPC_MUX_OUTMUX_MPCC0, MPC_MUX_OPPID_OPP0);

        // program shaper, 3dlut and 1dlut in MPC for stream before blend
        mpc->funcs->program_movable_cm(
            mpc, stream_ctx->in_shaper_func, stream_ctx->lut3d_func, stream_ctx->blend_tf, false);

        // program hdr_mult
        fmt.exponenta_bits = 6;
        fmt.mantissa_bits  = 12;
        fmt.sign           = true;
        if (stream_ctx->stream.tm_params.UID || stream_ctx->stream.tm_params.enable_3dlut) {
            if (!vpe_convert_to_custom_float_format(
                    stream_ctx->lut3d_func->hdr_multiplier, &fmt, &hw_mult)) {
                VPE_ASSERT(0);
            }
        } else {
            if (!vpe_convert_to_custom_float_format(stream_ctx->white_point_gain, &fmt, &hw_mult)) {
                VPE_ASSERT(0);
            }
        }
        dpp->funcs->set_hdr_multiplier(dpp, hw_mult);

        if (vpe_priv->init.debug.dpp_crc_ctrl)
            dpp->funcs->program_crc(dpp, true);

        if (vpe_priv->init.debug.mpc_crc_ctrl)
            mpc->funcs->program_crc(mpc, true);

        // put other hw programming for stream specific that can be shared here

        config_writer_complete(&vpe_priv->config_writer);
    }

    vpe10_create_stream_ops_config(vpe_priv, pipe_idx, stream_ctx, cmd_input, cmd_info->ops);

    /* start segment specific programming */
    vpe_priv->fe_cb_ctx.stream_sharing    = false;
    vpe_priv->fe_cb_ctx.stream_op_sharing = false;
    vpe_priv->fe_cb_ctx.cmd_type          = VPE_CMD_TYPE_COMPOSITING;

    cdc_fe->funcs->program_viewport(
        cdc_fe, &cmd_input->scaler_data.viewport, &cmd_input->scaler_data.viewport_c);

    dpp->funcs->set_segment_scaler(dpp, &cmd_input->scaler_data);

    config_writer_complete(&vpe_priv->config_writer);

    return 0;
}

int32_t vpe10_program_backend(
    struct vpe_priv *vpe_priv, uint32_t pipe_idx, uint32_t cmd_idx, bool seg_only)
{
    struct output_ctx       *output_ctx   = &vpe_priv->output_ctx;
    struct vpe_surface_info *surface_info = &vpe_priv->output_ctx.surface;

    struct cdc_be *cdc_be = vpe_priv->resource.cdc_be[pipe_idx];
    struct opp *opp = vpe_priv->resource.opp[pipe_idx];
    struct mpc *mpc = vpe_priv->resource.mpc[pipe_idx];

    struct bit_depth_reduction_params         fmt_bit_depth;
    struct clamping_and_pixel_encoding_params clamp_param;
    enum color_depth                          display_color_depth;
    uint16_t                                  alpha_16;
    bool                                      opp_dig_bypass = false;

    vpe_priv->be_cb_ctx.vpe_priv = vpe_priv;
    config_writer_set_callback(
        &vpe_priv->config_writer, &vpe_priv->be_cb_ctx, vpe_backend_config_callback);

    config_writer_set_type(&vpe_priv->config_writer, CONFIG_TYPE_DIRECT, pipe_idx);

    if (!seg_only) {
        /* start back-end programming that can be shared among segments */
        vpe_priv->be_cb_ctx.share = true;

        cdc_be->funcs->program_p2b_config(
            cdc_be, surface_info->format, surface_info->swizzle, &output_ctx->target_rect, NULL);
        cdc_be->funcs->program_global_sync(cdc_be, VPE10_CDC_VUPDATE_OFFSET_DEFAULT,
            VPE10_CDC_VUPDATE_WIDTH_DEFAULT, VPE10_CDC_VREADY_OFFSET_DEFAULT);

        mpc->funcs->set_output_transfer_func(mpc, output_ctx);
        // program shaper, 3dlut and 1dlut in MPC for after blend
        // Note: cannot program both before and after blend CM
        // caller should ensure only one is programmed
        // mpc->funcs->program_movable_cm(mpc, output_ctx->in_shaper_func,
        //    output_ctx->lut3d_func, output_ctx->blend_tf, true);
        mpc->funcs->program_mpc_out(mpc, surface_info->format);

        // Post blend gamut remap
        mpc->funcs->set_gamut_remap(mpc, output_ctx->gamut_remap);

        if (vpe_is_fp16(surface_info->format)) {
            if (vpe_priv->output_ctx.alpha_mode == VPE_ALPHA_BGCOLOR)
                vpe_convert_from_float_to_fp16(
                    (double)vpe_priv->output_ctx.mpc_bg_color.rgba.a, &alpha_16);
            else
                vpe_convert_from_float_to_fp16(1.0, &alpha_16);

            opp_dig_bypass = true;
        } else {
            if (vpe_priv->output_ctx.alpha_mode == VPE_ALPHA_BGCOLOR)
                alpha_16 = (uint16_t)(vpe_priv->output_ctx.mpc_bg_color.rgba.a * 0xffff);
            else
                alpha_16 = 0xffff;
        }

        opp->funcs->program_pipe_alpha(opp, alpha_16);
        opp->funcs->program_pipe_bypass(opp, opp_dig_bypass);

        display_color_depth = vpe_get_color_depth(surface_info->format);
        build_clamping_params(opp, &clamp_param);
        vpe_resource_build_bit_depth_reduction_params(opp, &fmt_bit_depth);

        // disable dynamic expansion for now as no use case
        opp->funcs->set_dyn_expansion(opp, false, display_color_depth);
        opp->funcs->program_fmt(opp, &fmt_bit_depth, &clamp_param);
        if (vpe_priv->init.debug.opp_pipe_crc_ctrl)
            opp->funcs->program_pipe_crc(opp, true);

        config_writer_complete(&vpe_priv->config_writer);
    }

    return 0;
}

enum vpe_status vpe10_populate_cmd_info(struct vpe_priv *vpe_priv)
{
    uint16_t             stream_idx;
    uint16_t             segment_idx;
    struct stream_ctx   *stream_ctx;
    struct vpe_cmd_info  cmd_info = {0};
    bool                 tm_enabled;

    for (stream_idx = 0; stream_idx < vpe_priv->num_streams; stream_idx++) {
        stream_ctx = &vpe_priv->stream_ctx[stream_idx];

        tm_enabled = stream_ctx->stream.tm_params.UID != 0 || stream_ctx->stream.tm_params.enable_3dlut;

        for (segment_idx = 0; segment_idx < stream_ctx->num_segments; segment_idx++) {

            cmd_info.inputs[0].stream_idx  = stream_idx;
            cmd_info.cd                    = (uint8_t)(stream_ctx->num_segments - segment_idx - 1);
            cmd_info.inputs[0].scaler_data = stream_ctx->segment_ctx[segment_idx].scaler_data;
            cmd_info.num_outputs           = 1;

            cmd_info.outputs[0].dst_viewport =
                stream_ctx->segment_ctx[segment_idx].scaler_data.dst_viewport;
            cmd_info.outputs[0].dst_viewport_c =
                stream_ctx->segment_ctx[segment_idx].scaler_data.dst_viewport_c;

            cmd_info.num_inputs         = 1;
            cmd_info.ops                = VPE_CMD_OPS_COMPOSITING;
            cmd_info.tm_enabled         = tm_enabled;
            cmd_info.insert_start_csync = false;
            cmd_info.insert_end_csync   = false;
            vpe_vector_push(vpe_priv->vpe_cmd_vector, &cmd_info);

            // The following codes are only valid if blending is supported
            /*
            if (cmd_info->ops == VPE_CMD_OPS_BLENDING) {
                if (cmd_info->cd == (stream_ctx->num_segments - 1)) {
                    cmd_info->insert_start_csync = true;
                }

                if (cmd_info->cd == 0) {
                    cmd_info->insert_end_csync = true;
                }
            }
            */
        }
    }

    return VPE_STATUS_OK;
}

void vpe10_create_stream_ops_config(struct vpe_priv *vpe_priv, uint32_t pipe_idx,
    struct stream_ctx *stream_ctx, struct vpe_cmd_input *cmd_input, enum vpe_cmd_ops ops)
{
    /* put all hw programming that can be shared according to the cmd type within a stream here */
    struct mpcc_blnd_cfg blndcfg  = {0};
    struct dpp          *dpp      = vpe_priv->resource.dpp[pipe_idx];
    struct mpc          *mpc      = vpe_priv->resource.mpc[pipe_idx];
    enum vpe_cmd_type    cmd_type = VPE_CMD_TYPE_COUNT;
    struct vpe_vector   *config_vector;

    vpe_priv->fe_cb_ctx.stream_op_sharing = true;
    vpe_priv->fe_cb_ctx.stream_sharing    = false;

    if (ops == VPE_CMD_OPS_BG) {
        cmd_type = VPE_CMD_TYPE_BG;
    } else if (ops == VPE_CMD_OPS_COMPOSITING) {
        cmd_type = VPE_CMD_TYPE_COMPOSITING;
    } else if (ops == VPE_CMD_OPS_BG_VSCF_INPUT) {
        cmd_type = VPE_CMD_TYPE_BG_VSCF_INPUT;
    } else if (ops == VPE_CMD_OPS_BG_VSCF_OUTPUT) {
        cmd_type = VPE_CMD_TYPE_BG_VSCF_OUTPUT;
    } else
        return;

    // return if already generated
    config_vector = stream_ctx->stream_op_configs[pipe_idx][cmd_type];
    if (config_vector->num_elements)
        return;

    vpe_priv->fe_cb_ctx.cmd_type = cmd_type;

    dpp->funcs->set_frame_scaler(dpp, &cmd_input->scaler_data);

    if (ops == VPE_CMD_OPS_BG_VSCF_INPUT) {
        blndcfg.bg_color = vpe_get_visual_confirm_color(stream_ctx->stream.surface_info.format,
            stream_ctx->stream.surface_info.cs, vpe_priv->output_ctx.cs,
            vpe_priv->output_ctx.output_tf, vpe_priv->output_ctx.surface.format,
            (stream_ctx->stream.tm_params.UID != 0 || stream_ctx->stream.tm_params.enable_3dlut));
    } else if (ops == VPE_CMD_OPS_BG_VSCF_OUTPUT) {
        blndcfg.bg_color = vpe_get_visual_confirm_color(vpe_priv->output_ctx.surface.format,
            vpe_priv->output_ctx.surface.cs, vpe_priv->output_ctx.cs,
            vpe_priv->output_ctx.output_tf, vpe_priv->output_ctx.surface.format,
            false); // 3DLUT should only affect input visual confirm
    } else {
        blndcfg.bg_color = vpe_priv->output_ctx.mpc_bg_color;
    }
    blndcfg.global_gain          = 0xff;
    blndcfg.pre_multiplied_alpha = false;

    if (stream_ctx->stream.blend_info.blending) {
        if (stream_ctx->per_pixel_alpha) {
            blndcfg.alpha_mode = MPCC_ALPHA_BLEND_MODE_PER_PIXEL_ALPHA_COMBINED_GLOBAL_GAIN;

            blndcfg.pre_multiplied_alpha = stream_ctx->stream.blend_info.pre_multiplied_alpha;
            if (stream_ctx->stream.blend_info.global_alpha)
                blndcfg.global_gain =
                    (uint8_t)(stream_ctx->stream.blend_info.global_alpha_value * 0xff);
        } else {
            blndcfg.alpha_mode = MPCC_ALPHA_BLEND_MODE_GLOBAL_ALPHA;
            if (stream_ctx->stream.blend_info.global_alpha == true) {
                VPE_ASSERT(stream_ctx->stream.blend_info.global_alpha_value <= 1.0f);
                blndcfg.global_alpha =
                    (uint8_t)(stream_ctx->stream.blend_info.global_alpha_value * 0xff);
            } else {
                // Global alpha not enabled, make top layer opaque
                blndcfg.global_alpha = 0xff;
            }
        }
    } else {
        blndcfg.alpha_mode   = MPCC_ALPHA_BLEND_MODE_GLOBAL_ALPHA;
        blndcfg.global_alpha = 0xff;
    }

    if (cmd_type == VPE_CMD_TYPE_BG || cmd_type == VPE_CMD_TYPE_BG_VSCF_INPUT ||
        cmd_type == VPE_CMD_TYPE_BG_VSCF_OUTPUT) {
        // for bg commands, make top layer transparent
        // as global alpha only works when global alpha mode, set global alpha mode as well
        blndcfg.global_alpha = 0;
        blndcfg.global_gain  = 0xff;
        blndcfg.alpha_mode   = MPCC_ALPHA_BLEND_MODE_GLOBAL_ALPHA;
    }

    blndcfg.overlap_only     = false;
    blndcfg.bottom_gain_mode = 0;

    switch (vpe_priv->init.debug.bg_bit_depth) {
    case 8:
        blndcfg.background_color_bpc = 0;
        break;
    case 9:
        blndcfg.background_color_bpc = 1;
        break;
    case 10:
        blndcfg.background_color_bpc = 2;
        break;
    case 11:
        blndcfg.background_color_bpc = 3;
        break;
    case 12:
    default:
        blndcfg.background_color_bpc = 4; // 12 bit. DAL's choice;
        break;
    }

    blndcfg.top_gain            = 0x1f000;
    blndcfg.bottom_inside_gain  = 0x1f000;
    blndcfg.bottom_outside_gain = 0x1f000;

    mpc->funcs->program_mpcc_blending(mpc, MPC_MPCCID_0, &blndcfg);

    config_writer_complete(&vpe_priv->config_writer);
}

#define VPE10_GENERAL_VPE_DESC_SIZE                144   // 4 * (4 + (2 * MAX_NUM_SAVED_CONFIG))
#define VPE10_GENERAL_EMB_USAGE_FRAME_SHARED       6000  // currently max 4804 is recorded
#define VPE10_GENERAL_EMB_USAGE_3DLUT_FRAME_SHARED 40960 // currently max 35192 is recorded
#define VPE10_GENERAL_EMB_USAGE_BG_SHARED          3600 // currently max 52 + 128 + 1356 +1020 +92 + 60 + 116 = 2824 is recorded
#define VPE10_GENERAL_EMB_USAGE_SEG_NON_SHARED                                                     \
    240 // segment specific config + plane descripor size. currently max 92 + 72 = 164 is recorded.

void vpe10_get_bufs_req(struct vpe_priv *vpe_priv, struct vpe_bufs_req *req)
{
    uint32_t             i;
    struct vpe_cmd_info *cmd_info;
    uint32_t             stream_idx                 = 0xFFFFFFFF;
    uint64_t             emb_req                    = 0;
    bool                 have_visual_confirm_input  = false;
    bool                 have_visual_confirm_output = false;

    req->cmd_buf_size = 0;
    req->emb_buf_size = 0;

    for (i = 0; i < vpe_priv->vpe_cmd_vector->num_elements; i++) {
        cmd_info = vpe_vector_get(vpe_priv->vpe_cmd_vector, i);
        VPE_ASSERT(cmd_info);

        // each cmd consumes one VPE descriptor
        req->cmd_buf_size += VPE10_GENERAL_VPE_DESC_SIZE;

        // if a command represents the first segment of a stream,
        // total amount of config sizes is added, but for other segments
        // just the segment specific config size is added
        if (cmd_info->ops == VPE_CMD_OPS_COMPOSITING) {
            if (stream_idx != cmd_info->inputs[0].stream_idx) {
                emb_req    = cmd_info->tm_enabled ? VPE10_GENERAL_EMB_USAGE_3DLUT_FRAME_SHARED
                                                  : VPE10_GENERAL_EMB_USAGE_FRAME_SHARED;
                stream_idx = cmd_info->inputs[0].stream_idx;
            } else {
                emb_req = VPE10_GENERAL_EMB_USAGE_SEG_NON_SHARED;
            }
        } else if (cmd_info->ops == VPE_CMD_OPS_BG) {
            emb_req =
                i > 0 ? VPE10_GENERAL_EMB_USAGE_SEG_NON_SHARED : VPE10_GENERAL_EMB_USAGE_BG_SHARED;
        } else if (cmd_info->ops == VPE_CMD_OPS_BG_VSCF_INPUT) {
            emb_req = have_visual_confirm_input ? VPE10_GENERAL_EMB_USAGE_SEG_NON_SHARED
                                                : VPE10_GENERAL_EMB_USAGE_BG_SHARED;
            have_visual_confirm_input = true;
        } else if (cmd_info->ops == VPE_CMD_OPS_BG_VSCF_OUTPUT) {
            emb_req = have_visual_confirm_output ? VPE10_GENERAL_EMB_USAGE_SEG_NON_SHARED
                                                 : VPE10_GENERAL_EMB_USAGE_BG_SHARED;
            have_visual_confirm_output = true;
        } else {
            VPE_ASSERT(0);
        }

        req->emb_buf_size += emb_req;
    }
}

enum vpe_status vpe10_check_mirror_rotation_support(const struct vpe_stream *stream)
{
    VPE_ASSERT(stream != NULL);

    if (stream->rotation != VPE_ROTATION_ANGLE_0)
        return VPE_STATUS_ROTATION_NOT_SUPPORTED;

    if (stream->vertical_mirror)
        return VPE_STATUS_MIRROR_NOT_SUPPORTED;

    return VPE_STATUS_OK;
}

/* This function generates software points for the blnd gam programming block.
   The logic for the blndgam/ogam programming sequence is a function of:
   1. Output Range (Studio Full)
   2. 3DLUT usage
   3. Output format (HDR SDR)

   SDR Out or studio range out
      TM Case
         BLNDGAM : NL -> NL*S + B
         OGAM    : Bypass
      Non TM Case
         BLNDGAM : L -> NL*S + B
         OGAM    : Bypass
   Full range HDR Out
      TM Case
         BLNDGAM : NL -> L
         OGAM    : L -> NL
      Non TM Case
         BLNDGAM : Bypass
         OGAM    : L -> NL

*/
enum vpe_status vpe10_update_blnd_gamma(struct vpe_priv *vpe_priv,
    const struct vpe_build_param *param, const struct vpe_stream *stream,
    struct transfer_func *blnd_tf)
{
    struct output_ctx       *output_ctx;
    struct vpe_color_space   tm_out_cs;
    struct fixed31_32        x_scale       = vpe_fixpt_one;
    struct fixed31_32        y_scale       = vpe_fixpt_one;
    struct fixed31_32        y_bias        = vpe_fixpt_zero;
    bool                     is_studio     = false;
    bool                     can_bypass    = false;
    bool                     lut3d_enabled = false;
    enum color_space         cs            = COLOR_SPACE_2020_RGB_FULLRANGE;
    enum color_transfer_func tf            = TRANSFER_FUNC_LINEAR;
    enum vpe_status          status        = VPE_STATUS_OK;
    const struct vpe_tonemap_params *tm_params     = &stream->tm_params;

    is_studio = (param->dst_surface.cs.range == VPE_COLOR_RANGE_STUDIO);
    output_ctx = &vpe_priv->output_ctx;
    lut3d_enabled = tm_params->UID != 0 || tm_params->enable_3dlut;

    if (stream->flags.geometric_scaling) {
        vpe_color_update_degamma_tf(vpe_priv, tf, x_scale, y_scale, y_bias, true, blnd_tf);
    } else {
        if (is_studio) {

            if (vpe_is_rgb8(param->dst_surface.format)) {
                y_scale = STUDIO_RANGE_SCALE_8_BIT;
                y_bias  = STUDIO_RANGE_FOOT_ROOM_8_BIT;
            } else {
                y_scale = STUDIO_RANGE_SCALE_10_BIT;
                y_bias  = STUDIO_RANGE_FOOT_ROOM_10_BIT;
            }
        }
        // If SDR out -> Blend should be NL
        // If studio out -> No choice but to blend in NL
        if (!vpe_is_HDR(output_ctx->tf) || (is_studio)) {
            if (lut3d_enabled) {
                tf = TRANSFER_FUNC_LINEAR;
            } else {
                tf = output_ctx->tf;
            }

            if (vpe_is_fp16(param->dst_surface.format)) {
                y_scale = vpe_fixpt_mul_int(y_scale, CCCS_NORM);
            }
            vpe_color_update_regamma_tf(
                vpe_priv, tf, x_scale, y_scale, y_bias, can_bypass, blnd_tf);
        } else {

            if (lut3d_enabled) {
                vpe_color_build_tm_cs(tm_params, &param->dst_surface, &tm_out_cs);
                vpe_color_get_color_space_and_tf(&tm_out_cs, &cs, &tf);
            } else {
                can_bypass = true;
            }

            vpe_color_update_degamma_tf(
                vpe_priv, tf, x_scale, y_scale, y_bias, can_bypass, blnd_tf);
        }
    }
    return status;
}

static enum vpe_status bg_color_outside_cs_gamut(
    const struct vpe_priv *vpe_priv, struct vpe_color *bg_color)
{
    enum color_space              cs;
    enum color_transfer_func      tf;
    struct vpe_color              bg_color_copy = *bg_color;
    const struct vpe_color_space *vcs           = &vpe_priv->output_ctx.surface.cs;

    vpe_color_get_color_space_and_tf(vcs, &cs, &tf);

    if ((bg_color->is_ycbcr)) {
        // using the bg_color_copy instead as bg_csc will modify it
        // we should not do modification in checking stage
        // otherwise validate_cached_param() will fail
        if (vpe_bg_csc(&bg_color_copy, cs)) {
            return VPE_STATUS_BG_COLOR_OUT_OF_RANGE;
        }
    }
    return VPE_STATUS_OK;
}

/*
    In order to support background color fill correctly, we need to do studio -> full range
    conversion before the blend block. However, there is also a requirement for HDR output to be
    blended in linear space. Hence, if we have PQ out and studio range, we need to make sure no
    blending will occur. Otherwise the job is invalid.

*/
static enum vpe_status is_valid_blend(const struct vpe_priv *vpe_priv, struct vpe_color *bg_color)
{

    enum vpe_status               status = VPE_STATUS_OK;
    const struct vpe_color_space *vcs    = &vpe_priv->output_ctx.surface.cs;
    struct stream_ctx *stream_ctx = vpe_priv->stream_ctx; // Only need to check the first stream.

    if ((vcs->range == VPE_COLOR_RANGE_STUDIO) && (vcs->tf == VPE_TF_PQ) &&
        ((stream_ctx->stream.surface_info.cs.encoding == VPE_PIXEL_ENCODING_RGB) ||
            vpe_is_global_bg_blend_applied(stream_ctx)))
        status = VPE_STATUS_BG_COLOR_OUT_OF_RANGE;

    return status;
}

enum vpe_status vpe10_check_bg_color_support(struct vpe_priv *vpe_priv, struct vpe_color *bg_color)
{

    enum vpe_status status = VPE_STATUS_OK;

    /* no need for background filling as for target rect equal to dest rect */
    if (vpe_rec_is_equal(vpe_priv->output_ctx.target_rect,
            vpe_priv->stream_ctx[0].stream.scaling_info.dst_rect)) {
        return VPE_STATUS_OK;
    }

    status = is_valid_blend(vpe_priv, bg_color);

    if (status == VPE_STATUS_OK)
        status = bg_color_outside_cs_gamut(vpe_priv, bg_color);

    return status;
}
