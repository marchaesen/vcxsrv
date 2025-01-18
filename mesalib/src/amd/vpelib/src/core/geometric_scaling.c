/* Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom thesrc/core/color.c
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

#include "geometric_scaling.h"

/*
 * Geometric scaling is to support multiple pass resizing to achieve larger resize ratio.
 * User will need to set stream.flags.geometric_scaling = 1
 * With the gs flag set to true, VPE will disable following feature
 * 1. gamma remapping, the input tf will be used for output tf.
 * 2. gamut remapping, the input primiaries/range will be used for output.
 * 3. tone mapping, tone mapping will be disabled.
 * 4. blending, blending will be disabled.
 *
 */

void vpe_geometric_scaling_feature_skip(
    struct vpe_priv *vpe_priv, const struct vpe_build_param *param)
{
    if (param->streams && param->streams->flags.geometric_scaling) {
        /* copy input cs to output for skiping gamut and gamma conversion */
        vpe_priv->output_ctx.surface.cs.primaries = param->streams[0].surface_info.cs.primaries;
        vpe_priv->output_ctx.surface.cs.tf = param->streams[0].surface_info.cs.tf;
        vpe_priv->output_ctx.surface.cs.cositing  = VPE_CHROMA_COSITING_NONE;
        vpe_priv->output_ctx.surface.cs.range     = VPE_COLOR_RANGE_FULL;

        /* skip tone mapping */
        vpe_priv->stream_ctx[0].stream.tm_params.UID = 0;
        vpe_priv->stream_ctx[0].stream.tm_params.enable_3dlut = false;

        /* disable blending */
        vpe_priv->stream_ctx[0].stream.blend_info.blending = false;
    }

}

/*
 * Geometric scaling feature has two requirement when enabled:
 * 1. only support single input stream, no blending support.
 * 2. the target rect must equal to destination rect.
 */

enum vpe_status vpe_validate_geometric_scaling_support(const struct vpe_build_param *param)
{
    if (param->num_streams && param->streams) {
        if (param->streams[0].flags.geometric_scaling) {
            /* only support 1 stream */
            if (param->num_streams > 1) {
                return VPE_STATUS_GEOMETRICSCALING_ERROR;
            }

            /* dest rect must equal to target rect */
            if (param->target_rect.height != param->streams[0].scaling_info.dst_rect.height ||
                param->target_rect.width != param->streams[0].scaling_info.dst_rect.width ||
                param->target_rect.x != param->streams[0].scaling_info.dst_rect.x ||
                param->target_rect.y != param->streams[0].scaling_info.dst_rect.y)
                return VPE_STATUS_GEOMETRICSCALING_ERROR;
        }
    }
    return VPE_STATUS_OK;
}

void vpe_update_geometric_scaling(struct vpe_priv *vpe_priv, const struct vpe_build_param *param,
    bool *geometric_update, bool *geometric_scaling)
{
    bool cached_gds = false;
    bool is_gds     = false;

    if (param->num_streams == 1 && vpe_priv->stream_ctx) {
        cached_gds = vpe_priv->stream_ctx[0].geometric_scaling;
        is_gds = (bool)vpe_priv->stream_ctx[0].stream.flags.geometric_scaling;

        if (cached_gds != is_gds) {
            *geometric_update = true;
            // Vpe needs to apply the cooresponding whitepoint on the last pass
            // based on the input format of the first pass
            if (is_gds) { // First pass
                vpe_priv->stream_ctx[0].is_yuv_input =
                    vpe_priv->stream_ctx[0].stream.surface_info.cs.encoding ==
                    VPE_PIXEL_ENCODING_YCbCr;
            }
        }
    }
    *geometric_scaling = is_gds;
}
