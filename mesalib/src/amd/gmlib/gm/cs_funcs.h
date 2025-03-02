/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : cs_funcs.h
 * Purpose    : Color Space functions
 * Author     : Vladimir Lachine (vlachine@amd.com)
 * Date       : September 20, 2023
 * Version    : 1.4
 *-------------------------------------------------------------------------
 *
 */

#pragma once

#include "mat_funcs.h"

#ifdef __cplusplus
    extern "C" {
#endif

#define CS_MAX_LUMINANCE 10000.0
#define CS_SCALE_CCCS    125.0
#define CS_CHAD_D65    0x01    /* apply chromatic adaptation */

static MATFLOAT cs_mat_709_2020[3][3] = { /* BT.2087 */
    {0.6274, 0.3293, 0.0433},
    {0.0691, 0.9195, 0.0114},
    {0.0164, 0.0880, 0.8956}
};

enum cs_white_point_type {
    EWPT_NONE = 0,        /* NATIVE */
    EWPT_A = 1,
    EWPT_B = 2,
    EWPT_C = 3,
    EWPT_D50 = 4,
    EWPT_D55 = 5,
    EWPT_D65 = 6,        /* 709, sRRGB, ADOBE, APPLE */
    EWPT_D75 = 7,
    EWPT_9300 = 8,
    EWPT_E = 9,
    EWPT_F2 = 10,
    EWPT_F7 = 11,
    EWPT_F11 = 12,
    EWPT_DCIP3 = 13,    /* DCI-P3 */
    EWPT_11000 = 14,    /* 11000K */
    EWPT_NUM = 15        /* CUSTOM */
};

enum cs_gamma_type {
    EGT_LINEAR = 0,        /* LINEAR    */
    EGT_709 = 1,        /* 709 (SD/HD)    */
    EGT_ADOBE = 2,        /* ADOBE 1998    */
    EGT_DCIP3 = 3,        /* DCI-P3    */
    EGT_APPLE = 4,        /* APPLE    */
    EGT_sRGB = 5,        /* sRGB        */
    EGT_PQ = 6,        /* PQ        */
    EGT_HLG = 7,        /* HLG        */
    EGT_2_2 = 8,        /* 2.2        */
    EGT_2_4 = 9,        /* 2.4        */
    EGT_CUSTOM = 10        /* CUSTOM    */
};

enum cs_color_space_type {
    ECST_709 = 0,        /* 709(HD),sRGB */
    ECST_SMPTE = 1,        /* SMPTE RP125 (SD) */
    ECST_ADOBE = 2,        /* ADOBE 1998    */
    ECST_DCIP3 = 3,        /* DCI-P3    */
    ECST_APPLE = 4,        /* APPLE    */
    ECST_EBU = 5,        /* EBU 3213 (576i) */
    ECST_NTSC = 6,        /* NTSC 1953    */
    ECST_CIE = 7,        /* CIE        */
    ECST_BT2020 = 8,    /* BT.2020    */
    ECST_CUSTOM = 9        /* CUSTOM    */
};

enum cs_gamma_dir {
    EGD_NONLIN_2_LIN = 0,
    EGD_LIN_2_NONLIN = 1
};

struct s_cs_opts {
    /* Color Space Type: [0,9]=0 : 0-709, 1-SMPTE, 2-ADOBE1998, 3-DCI-P3, 4-APPLE,
        5-EBU3213, 6-NTSC, 7-CIE, 8-BT2020, 9-CUSTOM */
    enum cs_color_space_type    color_space_type;
    /* Gamma Type: [0,9]=1 : 0-LINEAR, 1-709, 2-ADOBE, 3-DCI-P3, 4-APPLE,
        5-sRGB, 6-PQ, 7-HLG, 8-G2.2, 9-G2.4, 10-CUSTOM */
    enum cs_gamma_type    gamma_type;
    MATFLOAT    luminance_limits[2];    /* luminance min/max in a range [0.0,10000.0]= {0.0,400.0} */
    MATFLOAT    pq_norm;    /* normalizatiion luminance for PQ: [0.0,10000.0] = 0.0 - no normalization */
    unsigned int    mode;        /* mode: {0,1}=0 : Enable/disable Chromatic adaptation */
    MATFLOAT    rgbw_xy[8];    /* Chromaticity: Red, Green, Blue, White in xy */
    MATFLOAT    gamma_parm[4];    /* Gamma parameters: (0.0,?,?,?) - PQ, (0.5,?,?,?) - HLG  */
};

struct s_color_space {
    /* input parameters */
    /* cs_color_space_type: [0,9]=9 : 0-709, 1-SMPTE, 2-ADOBE1998, 3-DCI-P3, 4-APPLE,
        5-EBU3213, 6-NTSC, 7-CIE, 8-BT2020, 9-CUSTOM */
    enum cs_color_space_type    color_space_type;
    /* cs_gamma_type: [0,9]=9 : 0-LINEAR, 1-709, 2-ADOBE, 3-DCI-P3, 4-APPLE,
        5-sRGB, 6-PQ, 7-HLG, 8-Gamma2.2, 9-CUSTOM */
    enum cs_gamma_type        gamma_type;
    /* luminances min/max/range normilized to 10000.0 in a range [0.0,1.0]=0.0,1.0,1.0 */
    MATFLOAT    luminance_limits[3];
    MATFLOAT    pq_norm;    /* normalizatiion luminance for PQ: [0.0,10000.0] = 0.0 - no normalization */
    unsigned int    mode;            /* mode: {0,1}=0 : CS_CHAD_D65 - Enable Chromatic Adaptation */
    /* custom or initialized parameters based on input parameters */
    MATFLOAT    rgbw_xy[8];        /* Red, Green, Blue, White in xy */
    MATFLOAT    gamma_parm[4];        /* Gamma parameters: 0.0,?,?,? - PQ, 0.5,?,?,? - HLG */
    /* calculated variables */
    MATFLOAT    luma_limits[3];        /* Min/max/range luma (PQ) normilized to 10000 : [0.0,1.0]=0,1,1 */
    MATFLOAT    mat_rgb2xyz[3][3];    /* RGB to XYZ matrix */
    MATFLOAT    mat_xyz2rgb[3][3];    /* XYZ to RGB matrix */
    MATFLOAT    mat_rgb2lms[3][3];    /* RGB to LMS matrix */
    MATFLOAT    mat_lms2rgb[3][3];    /* LMS to RGB matrix */
    MATFLOAT    mat_lms2itp[3][3];    /* LMS to ITP matrix */
    MATFLOAT    mat_itp2lms[3][3];    /* ITP to LMS matrix */
    MATFLOAT    mat_chad[3][3];        /* Chromatic Adaptation matrix */
    MATFLOAT    white_xyz[3];        /* White in XYZ */
    int        cct;            /* Correlated Color Temperature */
    MATFLOAT    hlg_system_gamma;    /* HLG OOTF system gamma for */
    MATFLOAT    hlg_beta;        /* user black level lift */
};

/* get internal constants */
const MATFLOAT *cs_get_gamma(enum cs_gamma_type gamma_type);
const MATFLOAT *cs_get_color_space(enum cs_color_space_type color_space_type);
const MATFLOAT *cs_get_white_point(enum cs_white_point_type white_point_type);

/* initilize color space functions */
void cs_set_opts_def(struct s_cs_opts *ptr_cs_opts);
void cs_init(struct s_cs_opts *ptr_cs_opts, struct s_color_space *ptr_color_space);
void cs_init_private(struct s_color_space *ptr_color_space);
void cs_copy(struct s_color_space *ptr_color_space_src, struct s_color_space *ptr_color_space_dst);
void cs_luminance_to_luma_limits(MATFLOAT luminance_limits[2], MATFLOAT luma_limits[3]);

/* color formats conversion functions */
void cs_xyy_to_xyz(MATFLOAT xyy_inp[3], MATFLOAT xyz_out[3]);
void cs_xyz_to_xyy(MATFLOAT xyz_inp[3], MATFLOAT xyy_out[3]);

void cs_xyzc_to_xyz(MATFLOAT xyz_inp[3], MATFLOAT xyz_out[3]);
void cs_xyz_to_xyzc(MATFLOAT xyz_inp[3], MATFLOAT xyz_out[3]);

void cs_rgb_to_itp(struct s_color_space *ptr_color_space, MATFLOAT rgb_inp[3], MATFLOAT itp_out[3]);
void cs_itp_to_rgb(struct s_color_space *ptr_color_space, MATFLOAT itp_inp[3], MATFLOAT rgb_out[3]);

void cs_ich_to_itp(MATFLOAT ich_inp[3], MATFLOAT itp_out[3]);
void cs_itp_to_ich(MATFLOAT itp_inp[3], MATFLOAT ich_out[3]);

void cs_rgb_to_yuv(MATFLOAT rgb_inp[3], MATFLOAT yuv_out[3]);
void cs_yuv_to_rgb(MATFLOAT yuv_inp[3], MATFLOAT rgb_out[3]);

MATFLOAT cs_nlin_to_lin(struct s_color_space *ptr_color_space, MATFLOAT val_inp);
void cs_nlin_to_lin_rgb(struct s_color_space *ptr_color_space, MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3]);

MATFLOAT cs_lin_to_nlin(struct s_color_space *ptr_color_space, MATFLOAT val_inp);
void cs_lin_to_nlin_rgb(struct s_color_space *ptr_color_space, MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3]);

/* internal matrixes genereation functions */
int cs_genmat_rgb_to_xyz(MATFLOAT rgbw_xy[8], MATFLOAT mat_rgb2xyz[3][3]);
int cs_genmat_xyz_to_rgb(MATFLOAT rgbw_xy[8], MATFLOAT mat_xyz2rgb[3][3]);
int cs_genmat_rgb_to_rgb(MATFLOAT rgbw_xy_src[8], MATFLOAT rgbw_xy_dst[8], MATFLOAT mat_rgb2rgb[3][3], int en_chad);
int cs_genmat_chad(MATFLOAT white_xy_src[2], MATFLOAT white_xy_dst[2], MATFLOAT mat_chad[3][3]);

/* gamma curves generation functions */
MATFLOAT cs_gamma(MATFLOAT val, MATFLOAT gamma_parm[4], enum cs_gamma_dir gamma_dir);
MATFLOAT cs_gamma_pq(MATFLOAT val, enum cs_gamma_dir gamma_dir);
MATFLOAT cs_gamma_1886(MATFLOAT val, MATFLOAT lb, MATFLOAT lw, MATFLOAT gamma);

void cs_pq_ootf(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3]);

void cs_sdr_to_pq(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT en_709_2020);

void cs_gamma_rgb(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT gamma_parm[4], enum cs_gamma_dir gamma_dir);

/* signal clipping functions */
int cs_min_rgb(MATFLOAT rgb[3], MATFLOAT val_min);
int cs_max_rgb(MATFLOAT rgb[3], MATFLOAT val_max);

/* signal validation functions */
int cs_is_valid_itp(struct s_color_space *ptr_color_space, MATFLOAT itp[3]);
int cs_is_valid_ic(struct s_color_space *ptr_color_space, MATFLOAT pnt_ic[2], MATFLOAT hue_sin_cos[2]);
int cs_is_valid_rgb(MATFLOAT rgb[3], MATFLOAT val_min, MATFLOAT val_max);
int cs_clip_rgb(MATFLOAT rgb[3], MATFLOAT val_min, MATFLOAT val_max);
void cs_clamp_rgb(MATFLOAT rgb[3], MATFLOAT val_min, MATFLOAT val_max);

/* signal normalization functions */
void cs_norm_rgb(MATFLOAT rgb[3], MATFLOAT val_min, MATFLOAT val_rng);
void cs_denorm_rgb(MATFLOAT rgb[3], MATFLOAT val_min, MATFLOAT val_rng);

/* signal format conversion functions */
void cs_int2flt_rgb(int rgb_inp[3], MATFLOAT rgb_out[3], int val_max);
void cs_flt2int_rgb(MATFLOAT rgb_inp[3], int rgb_out[3], int val_max);
void cs_short2flt_rgb(unsigned short rgb_inp[3], MATFLOAT rgb_out[3], int val_max);
void cs_flt2short_rgb(MATFLOAT rgb_inp[3], unsigned short rgb_out[3], int val_max);

void cs_genprim_itp(struct s_color_space *ptr_color_space,
    int num_prim, MATFLOAT *ptr_prim_rgb, MATFLOAT *ptr_prim_ich);

/* gamma curve handling functions */
MATFLOAT cs_soft_clip(MATFLOAT val, MATFLOAT limits_src[3], MATFLOAT limits_dst[3]);
MATFLOAT cs_gamma_to_gamma(MATFLOAT val, enum cs_gamma_type gamma_type_src, enum cs_gamma_type gamma_type_dst,
    MATFLOAT luminance_limits_dst[3], MATFLOAT luma_limits_src[3], MATFLOAT luma_limits_dst[3],
    MATFLOAT(*func_pq_to_pq)(MATFLOAT), int en_norm, int en_soft_clip);

/* CCT handling functions */
#define CS_CCT_MIN 1000
#define CS_CCT_MAX 20000
#define CS_CCT_INC 100
#define CS_CCT_SIZE ((CS_CCT_MAX - CS_CCT_MIN) / CS_CCT_INC + 1)

int cs_xy_to_cct(MATFLOAT white_xy[2]);
void cs_cct_to_xy(int cct, MATFLOAT xy[2]);
void cs_csc(struct s_color_space *ptr_cs_src, struct s_color_space *ptr_cs_dst,
    MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], int en_chad);
int cs_is_space(struct s_color_space *ptr_color_space,
    enum cs_color_space_type color_space_type, enum cs_gamma_type gamma_type);

void cs_init_type(MATFLOAT luminance_limits[2],
    enum cs_color_space_type color_space_type, enum cs_gamma_type gamma_type,
    struct s_color_space *ptr_color_space);
void cs_init_BT709(MATFLOAT luminance_limits[2], struct s_color_space *ptr_color_space);
void cs_init_BT2100(MATFLOAT luminance_limits[2], struct s_color_space *ptr_color_space);
void cs_rgb_to_ycbcr2020(MATFLOAT rgb_inp[3], MATFLOAT ycbcr_out[3]);

MATFLOAT cs_ootf_gamma_peak(MATFLOAT gamma, MATFLOAT luminance_peak);
MATFLOAT cs_ootf_gamma_amb(MATFLOAT gamma, MATFLOAT luminance_ambient);
MATFLOAT cs_gamma_adjust_sdr(MATFLOAT gamma, MATFLOAT luminance_peak);
MATFLOAT cs_gamma_adjust(MATFLOAT gamma, MATFLOAT luminance_peak, MATFLOAT luminance_amb);

void cs_chad_gains(MATFLOAT rgbw_xy[8], MATFLOAT w_xy[2], MATFLOAT rgb_gain[3]);
void cs_genmat_cct(struct s_color_space *ptr_cs, int cct_shift, int norm, MATFLOAT mat_cct[3][3]);

/* HSV functions */
int cs_rgb_to_vsh(MATFLOAT rgb[3], MATFLOAT vsh[3]);
void cs_vsh_to_rgb(MATFLOAT vsh[3], MATFLOAT rgb[3]);

/* YUV functions */
void cs_yuv_to_ysh(MATFLOAT yuv_inp[3], MATFLOAT ysh_out[3]);
void cs_ysh_to_yuv(MATFLOAT ysh_inp[3], MATFLOAT yuv_out[3]);

/* CIELAB functions */
#define CS_LAB_E 0.008856
#define CS_LAB_K 903.3

void cs_rgb_to_lab(MATFLOAT rgb[3], MATFLOAT lab[3], struct s_color_space *ptr_color_space);
void cs_lab_to_rgb(MATFLOAT lab[3], MATFLOAT rgb[3], struct s_color_space *ptr_color_space);
void cs_xyz_to_lab(MATFLOAT xyz[3], MATFLOAT lab[3], MATFLOAT white_xyz[3]);
void cs_lab_to_xyz(MATFLOAT lab[3], MATFLOAT xyz[3], MATFLOAT white_xyz[3]);
MATFLOAT cs_de94(MATFLOAT lab0[3], MATFLOAT lab1[3]);

/* HLG functions */
MATFLOAT cs_gamma_hlg(MATFLOAT val, enum cs_gamma_dir gamma_dir);
void cs_hlg_ootf(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT luminance_peak, MATFLOAT system_gamma);
void cs_hlg_ootf_inv(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT luminance_peak, MATFLOAT gamma);
void cs_hlg_oetf(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT luminance_peak, MATFLOAT system_gamma);
void cs_hlg_eotf(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT luminance_limits[3],
    MATFLOAT system_gamma, MATFLOAT beta);
MATFLOAT cs_hlg_system_gamma(MATFLOAT peak_luminance);

#if 0
void cs_pq_to_hlg(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT luminance_peak, MATFLOAT system_gamma);
void cs_hlg_to_pq(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT vec_luminance[3],
    MATFLOAT system_gamma, MATFLOAT beta);
void cs_sdr_to_hlg(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT en_709_2020);
#endif

#ifdef __cplusplus
}
#endif
