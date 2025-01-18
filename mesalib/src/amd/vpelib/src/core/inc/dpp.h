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

#pragma once

#include "vpe_types.h"
#include "hw_shared.h"
#include "color.h"
#include "transform.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dpp;
struct vpe_priv;
struct vpe_csc_matrix;

struct cnv_alpha_2bit_lut {
    int lut0;
    int lut1;
    int lut2;
    int lut3;
};

struct cnv_keyer_params {
    uint8_t             keyer_en;
    uint8_t             is_color_key;
    enum vpe_keyer_mode keyer_mode;
    union {
        struct {
            uint16_t color_keyer_green_low;
            uint16_t color_keyer_green_high;
            uint16_t color_keyer_alpha_low;
            uint16_t color_keyer_alpha_high;
            uint16_t color_keyer_red_low;
            uint16_t color_keyer_red_high;
            uint16_t color_keyer_blue_low;
            uint16_t color_keyer_blue_high;
        } color_keyer;

        struct {
            uint16_t lower_luma_bound;
            uint16_t upper_luma_bound;
        } luma_keyer;
    };
};

enum input_csc_select {
    INPUT_CSC_SELECT_BYPASS = 0,
    INPUT_CSC_SELECT_ICSC   = 1,
};

struct dpp_funcs {

    bool (*get_optimal_number_of_taps)(
        struct vpe_rect *src_rect, struct vpe_rect *dst_rect, struct vpe_scaling_taps *taps);

    void (*dscl_calc_lb_num_partitions)(const struct scaler_data *scl_data,
        enum lb_memory_config lb_config, uint32_t *num_part_y, uint32_t *num_part_c);

    /** non segment specific */
    void (*program_cnv)(
        struct dpp *dpp, enum vpe_surface_pixel_format format, enum vpe_expansion_mode mode);

    void (*program_pre_dgam)(struct dpp *dpp, enum color_transfer_func tr);

    void (*program_cnv_bias_scale)(struct dpp *dpp, struct bias_and_scale *bias_and_scale);

    void (*build_keyer_params)(struct dpp *dpp, const struct stream_ctx *stream_ctx,
        struct cnv_keyer_params *keyer_params);

    void (*program_alpha_keyer)(struct dpp *dpp, const struct cnv_keyer_params *keyer_params);

    void (*program_input_transfer_func)(struct dpp *dpp, struct transfer_func *input_tf);

    void (*program_gamut_remap)(struct dpp *dpp, struct colorspace_transform *gamut_remap);

    /*program post scaler scs block in dpp CM*/
    void (*program_post_csc)(struct dpp *dpp, enum color_space color_space,
        enum input_csc_select input_select, struct vpe_csc_matrix *input_cs);

    void (*set_hdr_multiplier)(struct dpp *dpp, uint32_t multiplier);

    /** scaler */
    void (*set_segment_scaler)(struct dpp *dpp, const struct scaler_data *scl_data);

    void (*set_frame_scaler)(struct dpp *dpp, const struct scaler_data *scl_data);

    uint32_t (*get_line_buffer_size)(void);

    bool (*validate_number_of_taps)(struct dpp *dpp, struct scaler_data *scl_data);

    void (*program_crc)(struct dpp *opp, bool enable);
};

struct dpp {
    struct vpe_priv  *vpe_priv;
    struct dpp_funcs *funcs;
    unsigned int      inst;

    struct pwl_params degamma_params;
};

#ifdef __cplusplus
}
#endif
