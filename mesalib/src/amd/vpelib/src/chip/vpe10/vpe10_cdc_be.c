/* Copyright 2024 Advanced Micro Devices, Inc.
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

#include "common.h"
#include "vpe_priv.h"
#include "vpe10_cdc_be.h"
#include "reg_helper.h"

#define CTX_BASE cdc_be
#define CTX      vpe10_cdc_be

enum mux_sel {
    MUX_SEL_ALPHA = 0,
    MUX_SEL_Y_G   = 1,
    MUX_SEL_CB_B  = 2,
    MUX_SEL_CR_R  = 3
};

static struct cdc_be_funcs cdc_be_func = {
    .check_output_format = vpe10_cdc_check_output_format,
    .program_global_sync = vpe10_cdc_program_global_sync,
    .program_p2b_config  = vpe10_cdc_program_p2b_config,
};

void vpe10_construct_cdc_be(struct vpe_priv *vpe_priv, struct cdc_be *cdc_be)
{
    cdc_be->vpe_priv = vpe_priv;
    cdc_be->funcs    = &cdc_be_func;
}

bool vpe10_cdc_check_output_format(struct cdc_be *cdc_be, enum vpe_surface_pixel_format format)
{
    if (vpe_is_32bit_packed_rgb(format))
        return true;
    if (vpe_is_fp16(format))
        return true;

    return false;
}

void vpe10_cdc_program_global_sync(
    struct cdc_be *cdc_be, uint32_t vupdate_offset, uint32_t vupdate_width, uint32_t vready_offset)
{
    PROGRAM_ENTRY();

    REG_SET_3(VPCDC_BE0_GLOBAL_SYNC_CONFIG, 0, BE0_VUPDATE_OFFSET, vupdate_offset,
        BE0_VUPDATE_WIDTH, vupdate_width, BE0_VREADY_OFFSET, vready_offset);
}

void vpe10_cdc_program_p2b_config(struct cdc_be *cdc_be, enum vpe_surface_pixel_format format,
    enum vpe_swizzle_mode_values swizzle, const struct vpe_rect *viewport,
    const struct vpe_rect *viewport_c)
{
    uint32_t bar_sel0       = (uint32_t)MUX_SEL_CB_B;
    uint32_t bar_sel1       = (uint32_t)MUX_SEL_Y_G;
    uint32_t bar_sel2       = (uint32_t)MUX_SEL_CR_R;
    uint32_t bar_sel3       = (uint32_t)MUX_SEL_ALPHA;
    uint32_t p2b_format_sel = 0;

    PROGRAM_ENTRY();

    switch (format) {
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBX8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRX8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_XRGB8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_XBGR8888:
        p2b_format_sel = 0;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB2101010:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA1010102:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA1010102:
        p2b_format_sel = 1;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ARGB16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616F:
        p2b_format_sel = 2;
        break;
    default:
        VPE_ASSERT(0);
        break;
    }

    switch (format) {
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBX8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA1010102:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_RGBA16161616F:
        bar_sel3 = (uint32_t)MUX_SEL_CR_R;
        bar_sel2 = (uint32_t)MUX_SEL_Y_G;
        bar_sel1 = (uint32_t)MUX_SEL_CB_B;
        bar_sel0 = (uint32_t)MUX_SEL_ALPHA;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_XBGR8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR2101010:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_ABGR16161616F:
        bar_sel3 = (uint32_t)MUX_SEL_ALPHA;
        bar_sel2 = (uint32_t)MUX_SEL_CB_B;
        bar_sel1 = (uint32_t)MUX_SEL_Y_G;
        bar_sel0 = (uint32_t)MUX_SEL_CR_R;
        break;
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRX8888:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA1010102:
    case VPE_SURFACE_PIXEL_FORMAT_GRPH_BGRA16161616F:
        bar_sel3 = (uint32_t)MUX_SEL_CB_B;
        bar_sel2 = (uint32_t)MUX_SEL_Y_G;
        bar_sel1 = (uint32_t)MUX_SEL_CR_R;
        bar_sel0 = (uint32_t)MUX_SEL_ALPHA;
        break;
    default:
        break;
    }

    REG_SET_5(VPCDC_BE0_P2B_CONFIG, 0, VPCDC_BE0_P2B_XBAR_SEL0, bar_sel0, VPCDC_BE0_P2B_XBAR_SEL1,
        bar_sel1, VPCDC_BE0_P2B_XBAR_SEL2, bar_sel2, VPCDC_BE0_P2B_XBAR_SEL3, bar_sel3,
        VPCDC_BE0_P2B_FORMAT_SEL, p2b_format_sel);
}
