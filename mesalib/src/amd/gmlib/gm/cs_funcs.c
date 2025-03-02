/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : cs_funcs.c
 * Purpose    : Color Space functions
 * Author     : Vladimir Lachine (vlachine@amd.com)
 * Date       : September 20, 2023
 * Version    : 1.4
 *----------------------------------------------------------------------
 *
 */

#ifndef GM_SIM
#pragma code_seg("PAGED3PC")
#pragma data_seg("PAGED3PD")
#pragma const_seg("PAGED3PR")
#endif

#include "cs_funcs.h"

static const MATFLOAT cs_vec_gamma[EGT_CUSTOM][4] = {
    /* c1        c2              c3          c4 */
    {1.0000,    1.00,            0.00,       0.000},        /* linear                */
    {1.0990,    0.45,            4.50,       0.018},        /* 709 (SD/HD)           */
    {1.0000,    1.0 / 2.1992,    0.0,        0.0},          /* Adobe RGB 1998        */
    {1.0000,    1.0 / 2.6,       0.0,        0.0},          /* DCI-P3 (SMPTE-231-2)  */
    {1.0000,    1.0 / 1.8,       0.0,        0.0},          /* Apple Trinitron       */
    {1.0550,    1.0 / 2.4,       12.92,      0.0031308},    /* sRGB                  */
    {0.0000,    0.0,             0.0,        0.0},          /* PQ                    */
    {0.5000,    0.0,             0.0,        0.0},          /* HLG                   */
    {1.0000,    1.0 / 2.2,       0.0,        0.0},          /* Gamma 2.2             */
    {1.0000,    1.0 / 2.4,       0.0,        0.0}           /* Gamma 2.4             */
};

static const MATFLOAT cs_vec_color_space[ECST_CUSTOM][8] = {
    /* Red (x, y), Green (x,y), Blue (x,y), White (x,y) */
    {0.6400, 0.3300, 0.3000, 0.6000, 0.1500, 0.0600, 0.312710, 0.329020},    /* ITU_R BT.709-5/sRGB (HDTV) */
    {0.6300, 0.3400, 0.3100, 0.5950, 0.1550, 0.0700, 0.312710, 0.329020},    /* SMPTE RP 145 (SDTV)        */
    {0.6400, 0.3300, 0.2100, 0.7100, 0.1500, 0.0600, 0.312710, 0.329020},    /* Adobe RGB (1998)           */
    {0.6800, 0.3200, 0.2650, 0.6900, 0.1500, 0.0600, 0.312710, 0.329020},    /* DCI P3 (SMPTE-231-2) P3D65 */
/*  {0.6800, 0.3200, 0.2650, 0.6900, 0.1500, 0.0600, 0.314000, 0.351000},    // DCI P3 (SMPTE-231-2) P3D60 */
/*  {0.6800, 0.3200, 0.2650, 0.6900, 0.1500, 0.0600, 0.314000, 0.351000},    // DCI P3 (SMPTE-231-2) P3DCI */
    {0.6250, 0.3400, 0.2800, 0.5950, 0.1550, 0.0700, 0.312710, 0.329020},    /* Apple                      */
    {0.6400, 0.3300, 0.2900, 0.6000, 0.1500, 0.0600, 0.312710, 0.329020},    /* EBU 3213/ITU (PAL/SEQAM)   */
    {0.6700, 0.3300, 0.2100, 0.7100, 0.1400, 0.0800, 0.310100, 0.316200},    /* NTSC 1953                  */
    {0.7350, 0.2650, 0.2740, 0.7170, 0.1660, 0.0090, 0.333300, 0.333300},    /* CIE RGB                    */
    {0.7080, 0.2920, 0.1700, 0.7970, 0.1310, 0.0460, 0.312710, 0.329020}     /* BT.2020                    */
};

static MATFLOAT cs_vec_white_point[EWPT_NUM][3] = {
    /* x, y, z */
    {1.000000, 1.000000, 1.000000},    /* NONE                                            */
    {0.447570, 0.407440, 0.144990},    /* A - Tungsten or Incandescent, 2856K             */
    {0.348400, 0.351600, 0.300000},    /* B - Direct Sunlight at Noon, 4874K (obsolete)   */
    {0.310060, 0.316150, 0.373790},    /* C - North Sky Daylight, 6774K                   */
    {0.345670, 0.358500, 0.295830},    /* D50 - Daylight, used for COlor Rendering, 500K  */
    {0.332420, 0.347430, 0.320150},    /* D55 - Daylight, used for Photograph, 5500K      */
    {0.312710, 0.329020, 0.358270},    /* D65 - New version of North Sky Daylight, 6504K  */
    {0.299020, 0.314850, 0.386130},    /* D75 - Daylight, 7500K                           */
    {0.284800, 0.293200, 0.422000},    /* 9300K - High eff. blue phosphor monitors, 9300K */
    {0.333330, 0.333330, 0.333340},    /* E - Uniform energy illuminant, 5400K            */
    {0.372070, 0.375120, 0.252810},    /* F2 - Cool White Fluorescent (CWF), 4200K        */
    {0.312850, 0.329180, 0.357970},    /* F7 - Broad-band Daylight Fluorescent, 6500K     */
    {0.380540, 0.376910, 0.242540},    /* F11 - Narrow-band White Fluorescent, 4000K      */
    {0.314000, 0.351000, 0.335000},    /* DCI-P3                                          */
    {0.277400, 0.283600, 0.438660}     /* 11000K - blue sky, 11000K */
};

static const MATFLOAT cs_vec_cct_xy[2 * CS_CCT_SIZE] = {
    0.652750, 0.344462, 0.638755, 0.356498, 0.625043, 0.367454, 0.611630, 0.377232, 0.598520, 0.385788, /* 1000 */
    0.585716, 0.393121, 0.573228, 0.399264, 0.561066, 0.404274, 0.549243, 0.408225, 0.537776, 0.411202,
    0.526676, 0.413297, 0.515956, 0.414601, 0.505624, 0.415207, 0.495685, 0.415201, 0.486142, 0.414665, /* 2000 */
    0.476993, 0.413675, 0.468234, 0.412299, 0.459857, 0.410598, 0.451855, 0.408629, 0.444216, 0.406440,
    0.436929, 0.404073, 0.429981, 0.401566, 0.423358, 0.398951, 0.417046, 0.396255, 0.411032, 0.393503, /* 3000 */
    0.405302, 0.390715, 0.399841, 0.387907, 0.394638, 0.385095, 0.389677, 0.382291, 0.384948, 0.379505,
    0.380438, 0.376746, 0.376135, 0.374019, 0.372029, 0.371332, 0.368108, 0.368687, 0.364364, 0.366090, /* 4000 */
    0.360786, 0.363543, 0.357366, 0.361048, 0.354095, 0.358605, 0.350965, 0.356217, 0.347969, 0.353884,
    0.345100, 0.351607, 0.342350, 0.349384, 0.339715, 0.347215, 0.337187, 0.345102, 0.334761, 0.343041, /* 5000 */
    0.332433, 0.341034, 0.330196, 0.339078, 0.328047, 0.337173, 0.325981, 0.335317, 0.323994, 0.333511,
    0.322082, 0.331752, 0.320241, 0.330039, 0.318468, 0.328371, 0.316760, 0.326747, 0.315113, 0.325166, /* 6000 */
    0.313524, 0.323626, 0.311992, 0.322127, 0.310513, 0.320667, 0.309085, 0.319245, 0.307705, 0.317860,
    0.306372, 0.316511, 0.305083, 0.315196, 0.303837, 0.313915, 0.302631, 0.312667, 0.301463, 0.311450, /* 7000 */
    0.300333, 0.310264, 0.299238, 0.309108, 0.298178, 0.307981, 0.297149, 0.306881, 0.296153, 0.305809,
    0.295186, 0.304763, 0.294247, 0.303743, 0.293337, 0.302747, 0.292453, 0.301775, 0.291594, 0.300826, /* 8000 */
    0.290760, 0.299899, 0.289949, 0.298995, 0.289161, 0.298111, 0.288395, 0.297248, 0.287649, 0.296405,
    0.286924, 0.295581, 0.286218, 0.294776, 0.285531, 0.293989, 0.284862, 0.293220, 0.284211, 0.292467, /* 9000 */
    0.283576, 0.291732, 0.282957, 0.291012, 0.282354, 0.290308, 0.281765, 0.289619, 0.281192, 0.288945,
    0.280632, 0.288286, 0.280086, 0.287640, 0.279553, 0.287007, 0.279033, 0.286388, 0.278525, 0.285782, /* 10000 */
    0.278029, 0.285188, 0.277544, 0.284606, 0.277071, 0.284036, 0.276608, 0.283477, 0.276156, 0.282930,
    0.275714, 0.282393, 0.275281, 0.281867, 0.274858, 0.281351, 0.274444, 0.280845, 0.274039, 0.280349, /* 11000 */
    0.273643, 0.279862, 0.273255, 0.279384, 0.272875, 0.278915, 0.272503, 0.278455, 0.272139, 0.278004,
    0.271782, 0.277561, 0.271433, 0.277126, 0.271090, 0.276699, 0.270755, 0.276279, 0.270426, 0.275867, /* 12000 */
    0.270103, 0.275462, 0.269787, 0.275065, 0.269476, 0.274674, 0.269172, 0.274290, 0.268874, 0.273913,
    0.268581, 0.273542, 0.268293, 0.273178, 0.268011, 0.272820, 0.267734, 0.272467, 0.267462, 0.272121, /* 13000 */
    0.267195, 0.271780, 0.266933, 0.271445, 0.266676, 0.271116, 0.266423, 0.270791, 0.266174, 0.270472,
    0.265930, 0.270158, 0.265690, 0.269849, 0.265454, 0.269545, 0.265223, 0.269246, 0.264995, 0.268952, /* 14000 */
    0.264771, 0.268662, 0.264550, 0.268376, 0.264334, 0.268095, 0.264121, 0.267818, 0.263911, 0.267545,
    0.263705, 0.267277, 0.263502, 0.267012, 0.263302, 0.266751, 0.263106, 0.266495, 0.262912, 0.266241, /* 15000 */
    0.262722, 0.265992, 0.262534, 0.265746, 0.262350, 0.265504, 0.262168, 0.265265, 0.261989, 0.265030,
    0.261813, 0.264798, 0.261640, 0.264569, 0.261469, 0.264343, 0.261300, 0.264121, 0.261134, 0.263901, /* 16000 */
    0.260971, 0.263685, 0.260809, 0.263471, 0.260651, 0.263261, 0.260494, 0.263053, 0.260340, 0.262848,
    0.260188, 0.262646, 0.260038, 0.262446, 0.259890, 0.262249, 0.259744, 0.262055, 0.259600, 0.261863, /* 17000 */
    0.259458, 0.261674, 0.259318, 0.261487, 0.259180, 0.261302, 0.259044, 0.261120, 0.258910, 0.260940,
    0.258778, 0.260762, 0.258647, 0.260587, 0.258518, 0.260414, 0.258390, 0.260243, 0.258265, 0.260074, /* 18000 */
    0.258141, 0.259907, 0.258018, 0.259742, 0.257897, 0.259579, 0.257778, 0.259418, 0.257660, 0.259259,
    0.257544, 0.259102, 0.257429, 0.258947, 0.257315, 0.258793, 0.257203, 0.258642, 0.257093, 0.258492, /* 19000 */
    0.256983, 0.258344, 0.256875, 0.258197, 0.256768, 0.258052, 0.256663, 0.257909, 0.256559, 0.257768,
    0.256456, 0.257628    /* 20000 */
};

const MATFLOAT *cs_get_gamma(enum cs_gamma_type gamma_type)
{
    return cs_vec_gamma[(gamma_type < EGT_CUSTOM) ? gamma_type : EGT_LINEAR];
}

const MATFLOAT *cs_get_color_space(enum cs_color_space_type color_space_type)
{
    return cs_vec_color_space[(color_space_type < ECST_CUSTOM) ? color_space_type : ECST_709];
}

const MATFLOAT *cs_get_white_point(enum cs_white_point_type white_point_type)
{
    return cs_vec_white_point[(white_point_type < EWPT_NUM) ? white_point_type : EWPT_NONE];
}

void cs_set_opts_def(struct s_cs_opts *ptr_cs_opts)
{
    int ni;

    ptr_cs_opts->color_space_type = ECST_709;
    ptr_cs_opts->gamma_type = EGT_709;
    ptr_cs_opts->mode = 0;
    ptr_cs_opts->pq_norm = 0.0;
    ptr_cs_opts->luminance_limits[0] = 0.0;
    ptr_cs_opts->luminance_limits[1] = 400.0;
    for (ni = 0; ni < 8; ni++)
        ptr_cs_opts->rgbw_xy[ni] = cs_get_color_space(ECST_709)[ni];
    for (ni = 0; ni < 4; ni++)
        ptr_cs_opts->gamma_parm[ni] = cs_get_gamma(EGT_LINEAR)[ni];
}

void cs_init(struct s_cs_opts *ptr_cs_opts, struct s_color_space *ptr_color_space)
{
    int ni;

    ptr_color_space->color_space_type = ptr_cs_opts->color_space_type;
    ptr_color_space->gamma_type = ptr_cs_opts->gamma_type;
    ptr_color_space->mode = ptr_cs_opts->mode;
    ptr_color_space->pq_norm = (ptr_cs_opts->pq_norm > 0.0) ?
        cs_gamma_pq(ptr_cs_opts->pq_norm / CS_MAX_LUMINANCE, EGD_LIN_2_NONLIN) : 0.0;

    ptr_color_space->luminance_limits[0] = (MATFLOAT)ptr_cs_opts->luminance_limits[0] / CS_MAX_LUMINANCE;
    ptr_color_space->luminance_limits[1] = (MATFLOAT)ptr_cs_opts->luminance_limits[1] / CS_MAX_LUMINANCE;
    ptr_color_space->luminance_limits[2] = ptr_color_space->luminance_limits[1] -
            ptr_color_space->luminance_limits[0];

    for (int ni = 0; ni < 8; ni++)
        ptr_color_space->rgbw_xy[ni] = (ptr_cs_opts->color_space_type < ECST_CUSTOM) ?
                cs_get_color_space(ptr_cs_opts->color_space_type)[ni] : ptr_cs_opts->rgbw_xy[ni];

    for (ni = 0; ni < 4; ni++)
        ptr_color_space->gamma_parm[ni] = (ptr_cs_opts->gamma_type < EGT_CUSTOM) ?
                cs_get_gamma(ptr_cs_opts->gamma_type)[ni] : (MATFLOAT)ptr_cs_opts->gamma_parm[ni];

    cs_init_private(ptr_color_space);
}

void cs_init_private(struct s_color_space *ptr_color_space)
{
    static MATFLOAT mat_xyz2lms[3][3] = {
        /* ITU-R BT.2390-4, p36. */
        { 0.3592, 0.6976, -0.0358},
        {-0.1922, 1.1004,  0.0755},
        { 0.0070, 0.0749,  0.8434}
    };
    static MATFLOAT mat_lms2xyz[3][3] = {
        /* ITU-R BT.2390-4, p36. */
        { 2.0701800566956132, -1.3264568761030211,  0.2066160068478551},
        { 0.3649882500326574,  0.6804673628522352, -0.0454217530758532},
        {-0.0495955422389321, -0.0494211611867575,  1.1879959417328037}
    };
    static MATFLOAT mat_lms2itp[3][3] = {
        /* ITU-R BT.2020, BT.2390-4, p.36 */
        {             0.5,               0.5,              0.0},
        { 6610.0 / 4096.0, -13613.0 / 4096.0,  7003.0 / 4096.0},
        {17933.0 / 4096.0, -17390.0 / 4096.0,  -543.0 / 4096.0}
    };
    static MATFLOAT mat_itp2lms[3][3] = {
        /* ITU-R BT.2020, BT.2390-4, p.36 */
        {1.0,  0.00860903703793276,  0.11102962500302596},
        {1.0, -0.00860903703793276, -0.11102962500302596},
        {1.0,  0.56003133571067909, -0.32062717498731885}
    };

    int ni, nj;

    cs_luminance_to_luma_limits(ptr_color_space->luminance_limits, ptr_color_space->luma_limits);
    mat_3x3_unity(ptr_color_space->mat_chad);

    /* set white point */
    ptr_color_space->white_xyz[0] = ptr_color_space->rgbw_xy[6];
    ptr_color_space->white_xyz[1] = ptr_color_space->rgbw_xy[7];
    ptr_color_space->white_xyz[2] = 1.0;
    cs_xyy_to_xyz(ptr_color_space->white_xyz, ptr_color_space->white_xyz);

    /* generate RGB to XYZ and back matrixes */
    cs_genmat_rgb_to_xyz(ptr_color_space->rgbw_xy, ptr_color_space->mat_rgb2xyz);
    if (ptr_color_space->mode & CS_CHAD_D65) {
        /* Chromatic Adaptation from Color Space to D65 (BT.2020) */
        MATFLOAT mat_tmp[3][3];

        cs_genmat_chad(&ptr_color_space->rgbw_xy[6], (MATFLOAT *)cs_get_white_point(EWPT_D65),
            ptr_color_space->mat_chad);
        mat_copy3x3(ptr_color_space->mat_rgb2xyz, mat_tmp);
        mat_mul3x3(ptr_color_space->mat_chad, mat_tmp, ptr_color_space->mat_rgb2xyz);
    }
    mat_inv3x3(ptr_color_space->mat_rgb2xyz, ptr_color_space->mat_xyz2rgb);

    for (ni = 0; ni < 3; ni++)
        for (nj = 0; nj < 3; nj++) {
            ptr_color_space->mat_lms2itp[ni][nj] = mat_lms2itp[ni][nj];
            ptr_color_space->mat_itp2lms[ni][nj] = mat_itp2lms[ni][nj];
        }

    mat_mul3x3(mat_xyz2lms, ptr_color_space->mat_rgb2xyz, ptr_color_space->mat_rgb2lms);
    mat_mul3x3(ptr_color_space->mat_xyz2rgb, mat_lms2xyz, ptr_color_space->mat_lms2rgb);

    ptr_color_space->cct = cs_xy_to_cct(&ptr_color_space->rgbw_xy[6]);

    ptr_color_space->hlg_system_gamma = cs_hlg_system_gamma(ptr_color_space->luminance_limits[1]);
    ptr_color_space->hlg_beta = mat_sqrt(3.0 * mat_pow(ptr_color_space->luminance_limits[0] /
        ptr_color_space->luminance_limits[1], 1.0 / ptr_color_space->hlg_system_gamma));
}

void cs_copy(struct s_color_space *ptr_color_space_src, struct s_color_space *ptr_color_space_dst)
{
    ptr_color_space_dst->color_space_type = ptr_color_space_src->color_space_type;
    ptr_color_space_dst->gamma_type = ptr_color_space_src->gamma_type;
    ptr_color_space_dst->mode = ptr_color_space_src->mode;
    ptr_color_space_dst->pq_norm = ptr_color_space_src->pq_norm;
    int ni, nj;

    for (ni = 0; ni < 3; ni++)
        ptr_color_space_dst->luminance_limits[ni] = ptr_color_space_src->luminance_limits[ni];
    for (ni = 0; ni < 8; ni++)
        ptr_color_space_dst->rgbw_xy[ni] = ptr_color_space_src->rgbw_xy[ni];
    for (ni = 0; ni < 4; ni++)
        ptr_color_space_dst->gamma_parm[ni] = ptr_color_space_src->gamma_parm[ni];
    for (ni = 0; ni < 3; ni++)
        for (nj = 0; nj < 3; nj++) {
            ptr_color_space_dst->mat_rgb2xyz[ni][nj] = ptr_color_space_src->mat_rgb2xyz[ni][nj];
            ptr_color_space_dst->mat_xyz2rgb[ni][nj] = ptr_color_space_src->mat_xyz2rgb[ni][nj];
            ptr_color_space_dst->mat_chad[ni][nj] = ptr_color_space_src->mat_chad[ni][nj];
            ptr_color_space_dst->mat_rgb2lms[ni][nj] = ptr_color_space_src->mat_rgb2lms[ni][nj];
            ptr_color_space_dst->mat_lms2rgb[ni][nj] = ptr_color_space_src->mat_lms2rgb[ni][nj];
            ptr_color_space_dst->mat_lms2itp[ni][nj] = ptr_color_space_src->mat_lms2itp[ni][nj];
            ptr_color_space_dst->mat_itp2lms[ni][nj] = ptr_color_space_src->mat_itp2lms[ni][nj];
        }
    for (ni = 0; ni < 3; ni++)
        ptr_color_space_dst->white_xyz[ni] = ptr_color_space_src->white_xyz[ni];
    ptr_color_space_dst->cct = ptr_color_space_src->cct;
}

void cs_luminance_to_luma_limits(MATFLOAT luminance_limits[2], MATFLOAT luma_limits[3])
{
    luma_limits[0] = cs_gamma_pq(luminance_limits[0], EGD_LIN_2_NONLIN);
    luma_limits[1] = cs_gamma_pq(luminance_limits[1], EGD_LIN_2_NONLIN);
    luma_limits[2] = luma_limits[1] - luma_limits[0];
}

void cs_xyy_to_xyz(MATFLOAT xyy_inp[3], MATFLOAT xyz_out[3])
{    /* output may be the same as input */
    MATFLOAT xyy_tmp[3];

    mat_copy(xyy_inp, xyy_tmp, 3);
    xyz_out[0] = (xyy_tmp[1] > 0.0) ? xyy_tmp[2] * xyy_tmp[0] / xyy_tmp[1] : 0.0;
    xyz_out[1] = xyy_tmp[2];
    xyz_out[2] = (xyy_tmp[1] > 0.0) ? xyy_tmp[2] * (1.0 - xyy_tmp[0] - xyy_tmp[1]) / xyy_tmp[1] : 0.0;
}

void cs_xyz_to_xyy(MATFLOAT xyz_inp[3], MATFLOAT xyy_out[3])
{    /* output may be the same as input */
    MATFLOAT sum = xyz_inp[0] + xyz_inp[1] + xyz_inp[2];

    xyy_out[2] = xyz_inp[1];
    xyy_out[1] = (sum > 0.0) ? xyz_inp[1] / sum : 0.0;
    xyy_out[0] = (sum > 0.0) ? xyz_inp[0] / sum : 0.0;
}

void cs_xyzc_to_xyz(MATFLOAT xyz_inp[3], MATFLOAT xyz_out[3])
{    /* output may be the same as input */
    MATFLOAT sum = xyz_inp[0] + xyz_inp[1] + xyz_inp[2];

    xyz_out[0] = (sum > 0.0) ? xyz_inp[0] / sum : 0.0;
    xyz_out[1] = (sum > 0.0) ? xyz_inp[1] / sum : 0.0;
    xyz_out[2] = 1.0 - xyz_out[0] - xyz_out[1];
}

void cs_xyz_to_xyzc(MATFLOAT xyz_inp[3], MATFLOAT xyz_out[3])
{    /* output may be the same as input */
    MATFLOAT xyz_tmp[3];

    mat_copy(xyz_inp, xyz_tmp, 3);
    xyz_out[0] = (xyz_tmp[1] > 0.0) ? xyz_tmp[0] / xyz_tmp[1] : 0.0;
    xyz_out[1] = 1.0;
    xyz_out[2] = (xyz_tmp[1] > 0.0) ? xyz_tmp[2] / xyz_tmp[1] : 0.0;
}

void cs_rgb_to_itp(struct s_color_space *ptr_color_space, MATFLOAT rgb_inp[3], MATFLOAT itp_out[3])
{    /* output may be the same as input */
    MATFLOAT lms[3];
    int nc;

    mat_eval_3x3(ptr_color_space->mat_rgb2lms, rgb_inp, lms);
    for (nc = 0; nc < 3; nc++)
        lms[nc] = cs_gamma_pq(lms[nc], EGD_LIN_2_NONLIN);
    mat_eval_3x3(ptr_color_space->mat_lms2itp, lms, itp_out);
}

void cs_itp_to_rgb(struct s_color_space *ptr_color_space, MATFLOAT itp_inp[3], MATFLOAT rgb_out[3])
{    /* output may be the same as input */
    MATFLOAT lms[3];
    int nc;

    mat_eval_3x3(ptr_color_space->mat_itp2lms, itp_inp, lms);
    for (nc = 0; nc < 3; nc++)
        lms[nc] = cs_gamma_pq(lms[nc], EGD_NONLIN_2_LIN);
    mat_eval_3x3(ptr_color_space->mat_lms2rgb, lms, rgb_out);
}

void cs_ich_to_itp(MATFLOAT ich_inp[3], MATFLOAT itp_out[3])
{    /* output must not be the same as input */
    itp_out[0] = ich_inp[0];
    itp_out[1] = ich_inp[1] * mat_cos(ich_inp[2]);
    itp_out[2] = ich_inp[1] * mat_sin(ich_inp[2]);
}

void cs_itp_to_ich(MATFLOAT itp_inp[3], MATFLOAT ich_out[3])
{    /* output must not be the same as input */
    ich_out[0] = itp_inp[0];
    ich_out[1] = mat_radius(itp_inp[2], itp_inp[1]);
    ich_out[2] = mat_angle(itp_inp[2], itp_inp[1]);
}

void cs_rgb_to_yuv(MATFLOAT rgb_inp[3], MATFLOAT yuv_out[3])
{    /* RGB to YCbCr709 from Charles Poynton "Digital Video and HD: Algorithms and Interfaces", p.371 */
    static MATFLOAT vec_off_inp[3] = { 0.0, 0.0, 0.0 };
    static MATFLOAT vec_off_out[3] = { 0.0, 0.5, 0.5 };
    static MATFLOAT mat_rgb_to_yuv[3][3] = {
        /*   R         G           B   */
        {  0.2126,         0.7152,       0.0722 },
        { -0.11457211,    -0.38542789,   0.5 },
        {  0.5,            -0.45415291,  -0.04584709}
    };

    mat_eval_off_3x3_off(vec_off_inp, mat_rgb_to_yuv, vec_off_out, rgb_inp, yuv_out);
    cs_clamp_rgb(yuv_out, 0.0, 1.0);
}

void cs_yuv_to_rgb(MATFLOAT yuv_inp[3], MATFLOAT rgb_out[3])
{    /* YCbCr709 to RGB from Charles Poynton "Digital Video and HD: Algorithms and Interfaces", p.371 */
    static MATFLOAT vec_off_inp[3] = { 0.0, -0.5, -0.5 };
    static MATFLOAT vec_off_out[3] = { 0.0,  0.0,  0.0 };
    static MATFLOAT mat_yuv_to_rgb[3][3] = {
        /*    Y        Cb        Cr */
        { 1.0,   0.0,          1.5748 },
        { 1.0,  -0.187324273, -0.468124273 },
        { 1.0,   1.8556,       0.0 }
    };

    mat_eval_off_3x3_off(vec_off_inp, mat_yuv_to_rgb, vec_off_out, yuv_inp, rgb_out);
    cs_clamp_rgb(rgb_out, 0.0, 1.0);
}

void cs_nlin_to_lin_rgb(struct s_color_space *ptr_color_space, MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3])
{
    if (ptr_color_space->gamma_type == EGT_HLG)
        cs_hlg_eotf(rgb_inp, rgb_out, ptr_color_space->luminance_limits,
            ptr_color_space->hlg_system_gamma, ptr_color_space->hlg_beta);
    else
        for (int nc = 0; nc < 3; nc++)
            rgb_out[nc] = cs_nlin_to_lin(ptr_color_space, rgb_inp[nc]);
}

MATFLOAT cs_nlin_to_lin(struct s_color_space *ptr_color_space, MATFLOAT val_inp)
{
    MATFLOAT val_out;

    if (ptr_color_space->gamma_type == EGT_PQ) {
        /* HDR PQ encoded signal is normilized to a range [0.0,1.0],
            where 0.0 mapped to 0.0 and 1.0 mapped to PQ-1(pq_norm) */
        if (ptr_color_space->pq_norm > 0.0)
            val_out = mat_denorm(val_inp, 0.0, ptr_color_space->pq_norm);
        else
            val_out = val_inp;
        val_out = mat_clamp(val_out, 0.0, 1.0);
        val_out = cs_gamma(val_out, ptr_color_space->gamma_parm, EGD_NONLIN_2_LIN);
    }
    else {
        /* SDR encoded signal is normilized to a range [0.0,1.0],
            where 0.0 mapped to Black (0,0,0) and 1.0 mapped to White (1,1,1) */
        val_out = cs_gamma(val_inp, ptr_color_space->gamma_parm, EGD_NONLIN_2_LIN);
        val_out = mat_denorm(val_out, ptr_color_space->luminance_limits[0], ptr_color_space->luminance_limits[2]);
        val_out = mat_clamp(val_out, 0.0, 1.0);
    }

    return val_out;
}

void cs_lin_to_nlin_rgb(struct s_color_space *ptr_color_space, MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3])
{
    if (ptr_color_space->gamma_type == EGT_HLG)
        cs_hlg_oetf(rgb_inp, rgb_out, ptr_color_space->luminance_limits[1], ptr_color_space->hlg_system_gamma);
    else
        for (int nc = 0; nc < 3; nc++)
            rgb_out[nc] = cs_lin_to_nlin(ptr_color_space, rgb_inp[nc]);
}

MATFLOAT cs_lin_to_nlin(struct s_color_space *ptr_color_space, MATFLOAT val_inp)
{
    MATFLOAT val_out;

    if (ptr_color_space->gamma_type == EGT_PQ) {
        /* HDR PQ encoded signal is normilized to a range [0.0,1.0],
            where 0.0 mapped to 0.0 and 1.0 mapped to PQ-1(pq_norm) */
        val_out = cs_gamma(val_inp, ptr_color_space->gamma_parm, EGD_LIN_2_NONLIN);
        if (ptr_color_space->pq_norm > 0.0)
            val_out = mat_norm(val_out, 0.0, ptr_color_space->pq_norm);
        val_out = mat_clamp(val_out, 0.0, 1.0);
    }
    else {
        /* SDR encoded signal is normilized to a range [0.0,1.0],
            where 0.0 mapped to Black (0,0,0) and 1.0 mapped to White (1,1,1) */
        val_out = mat_norm(val_inp, ptr_color_space->luminance_limits[0], ptr_color_space->luminance_limits[2]);
        val_out = mat_clamp(val_out, 0.0, 1.0);
        val_out = cs_gamma(val_out, ptr_color_space->gamma_parm, EGD_LIN_2_NONLIN);
    }

    return val_out;
}

int cs_genmat_rgb_to_xyz(MATFLOAT rgbw[8], MATFLOAT mat_rgb2xyz[3][3])
{
    MATFLOAT white_xyz[3] = { rgbw[6], rgbw[7], 1.0 };
    MATFLOAT mat[3][3], mat_inv[3][3], white_k[3];
    int ni, nc;
    int rc;

    for (ni = 0; ni < 3; ni++) {    /* X, Y, Z */
        mat[0][ni] = rgbw[2 * ni + 0] / rgbw[2 * ni + 1];
        mat[1][ni] = 1.0;
        mat[2][ni] = (1.0 - rgbw[2 * ni + 0] - rgbw[2 * ni + 1]) / rgbw[2 * ni + 1];
    }
    rc = mat_inv3x3(mat, mat_inv);
    cs_xyy_to_xyz(white_xyz, white_xyz);
    mat_eval_3x3(mat_inv, white_xyz, white_k);
    for (ni = 0; ni < 3; ni++)
        for (nc = 0; nc < 3; nc++)
            mat_rgb2xyz[nc][ni] = white_k[ni] * mat[nc][ni];

    return rc;
}

int cs_genmat_xyz_to_rgb(MATFLOAT rgbw_xy[8], MATFLOAT mat_xyz2rgb[3][3])
{
    MATFLOAT mat_rgb2xyz[3][3];

    cs_genmat_rgb_to_xyz(rgbw_xy, mat_rgb2xyz);
    return mat_inv3x3(mat_rgb2xyz, mat_xyz2rgb);
}

int cs_genmat_rgb_to_rgb(MATFLOAT rgbw_xy_src[8], MATFLOAT rgbw_xy_dst[8], MATFLOAT mat_rgb2rgb[3][3], int en_chad)
{
    MATFLOAT mat_rgb2xyz[3][3], mat_xyz2rgb[3][3], mat_chad[3][3];
    int rc;

    cs_genmat_rgb_to_xyz(rgbw_xy_src, mat_rgb2xyz);
    rc = cs_genmat_xyz_to_rgb(rgbw_xy_dst, mat_xyz2rgb);

    if (en_chad) { /* Chromatic Adaptation */
        MATFLOAT mat_tmp[3][3];

        cs_genmat_chad(&rgbw_xy_src[6], &rgbw_xy_dst[6], mat_chad);
        mat_copy3x3(mat_rgb2xyz, mat_tmp);
        mat_mul3x3(mat_chad, mat_tmp, mat_rgb2xyz);
    }

    mat_mul3x3(mat_xyz2rgb, mat_rgb2xyz, mat_rgb2rgb);

    return rc;
}

int cs_genmat_chad(MATFLOAT white_xy_src[2], MATFLOAT white_xy_dst[2], MATFLOAT mat_chad[3][3])
{
    static MATFLOAT mat_bradford[3][3] = {
        /* Bradford matrix */
        { 0.8951000,  0.2664000, -0.1614000},
        {-0.7502000,  1.7135000,  0.0367000},
        { 0.0389000, -0.0685000,  1.0296000}
    };

    static MATFLOAT mat_bradford_inv[3][3] = {
        /* Bradford inverse matrix */
        { 0.9869929, -0.1470543, 0.1599627},
        { 0.4323053,  0.5183603, 0.0492912},
        {-0.0085287,  0.0400428, 0.9684867}
    };

#if 0    /* Not in used */
    static MATFLOAT mat_von_kries[3][3] = {
        /* Von Kries matrix */
        { 0.4002400, 0.7076000, -0.0808100},
        {-0.2263000, 1.1653200,  0.0457000},
        { 0.0000000, 0.0000000,  0.9182200}
    };

    static MATFLOAT mat_von_kries_inv[3][3] = {
        /* Von Kries inverse matrix */
        {1.8599364, -1.1293816,  0.2198974},
        {0.3611914,  0.6388125, -0.0000064},
        {0.0000000,  0.0000000,  1.0890636}
    };
#endif

    MATFLOAT vec_white_xyz_src[3] = { white_xy_src[0], white_xy_src[1], 1.0 };
    MATFLOAT vec_white_xyz_dst[3] = { white_xy_dst[0], white_xy_dst[1], 1.0 };
    MATFLOAT vec_lms[3][3];
    MATFLOAT rgb_src[3], rgb_dst[3];
    MATFLOAT mat_tmp[3][3];
    int nc;

    /* convert to XYZ */
    cs_xyy_to_xyz(vec_white_xyz_src, vec_white_xyz_src);
    cs_xyy_to_xyz(vec_white_xyz_dst, vec_white_xyz_dst);
    /* generate scales */
    mat_3x3_unity(vec_lms);
    mat_eval_3x3(mat_bradford, vec_white_xyz_src, rgb_src);
    mat_eval_3x3(mat_bradford, vec_white_xyz_dst, rgb_dst);
    for (nc = 0; nc < 3; nc++)
        vec_lms[nc][nc] = rgb_dst[nc] / rgb_src[nc];
    /* normalize */
    mat_mul3x3(vec_lms, mat_bradford, mat_tmp);
    mat_mul3x3(mat_bradford_inv, mat_tmp, mat_chad);

    return 0;
}

MATFLOAT cs_gamma(MATFLOAT val, MATFLOAT gamma_parm[4], enum cs_gamma_dir gamma_dir)
{
    MATFLOAT val_out;

    if (gamma_parm[0] == 0.0)
        val_out = cs_gamma_pq(val, gamma_dir);
    else if (gamma_parm[0] == 0.5)
        val_out = cs_gamma_hlg(val, gamma_dir);
    else {
        MATFLOAT c1 = gamma_parm[0];
        MATFLOAT c2 = gamma_parm[1];
        MATFLOAT c3 = gamma_parm[2];
        MATFLOAT c4 = gamma_parm[3];

        if (gamma_dir == EGD_LIN_2_NONLIN)
            val_out = ((val < c4) ? val * c3 : c1 * mat_pow(val, c2) + 1.0 - c1);
        else
            val_out = (val < c4 * c3) ? val / c3 : mat_pow((val + c1 - 1.0) / c1, 1.0 / c2);
    }

    return val_out;
}

/* R_REC-BT.2100-2-2 Table 4 */
/* input must be in arange [0,1] normilized to [0,10000]cd/m^2 in linear or non-linear space */
/* output must be in a range [0,1] normilized to [0,10000]cd/m^2 in linear or non-linear space */
MATFLOAT cs_gamma_pq(MATFLOAT val, enum cs_gamma_dir gamma_dir)
{
    static const MATFLOAT s_m1 = 0.1593017578125;
    static const MATFLOAT s_m2 = 78.84375;
    static const MATFLOAT s_c1 = 0.8359375;
    static const MATFLOAT s_c2 = 18.8515625;
    static const MATFLOAT s_c3 = 18.6875;

    MATFLOAT sign = (val < 0.0) ? -1.0 : 1.0;
    MATFLOAT val_out = MAT_ABS(val);
    MATFLOAT t1, t2, t;

    if (gamma_dir == EGD_LIN_2_NONLIN) { /* linear to PQ */
        MATFLOAT x = mat_pow(val_out, s_m1);

        t1 = (s_c2 * x) + s_c1;
        t2 = 1.0 + (s_c3 * x);
        t = t1 / t2;
        val_out = mat_pow(t, s_m2);
    } else { /* PQ to linear */
        MATFLOAT np = mat_pow(val_out, 1.0 / s_m2);

        t1 = np - s_c1;
        t1 = MAT_MAX(t1, 0.0);
        t2 = s_c2 - (s_c3 * np);
        t = t1 / t2;
        val_out = mat_pow(t, 1.0 / s_m1);
    }
    val_out *= sign;

    return val_out;
}

/* EOTF 1886 */
/* input must be in arange [0,1] normilized to [Lb,Lw]cd/m^2 in non-linear space */
/* output must be in arange [0,1] normilized to [0,10000]cd/m^2 in linear space */
/* lb in a range [0,1] normalized to [0,10000]cd/m^2 in linear space */
/* lw in a range [0,1] normalized to [0,10000]cd/m^2 in linear space */
MATFLOAT cs_gamma_1886(MATFLOAT val, MATFLOAT lb, MATFLOAT lw, MATFLOAT gamma)
{
    MATFLOAT lb_nl = mat_pow(lb, 1.0 / gamma);
    MATFLOAT lw_nl = mat_pow(lw, 1.0 / gamma);
    MATFLOAT a = mat_pow(lw_nl - lb_nl, gamma);
    MATFLOAT b = lb_nl / (lw_nl - lb_nl);

    return a * mat_pow(MAT_MAX(val + b, 0.0), gamma);
}

/* rgb_inp[] in a range [0,1] normalized to [0,10000]cd/m^2 in linear space */
/* rgb_out[] in a range [0,1] normalized to [0,10000]cd/m^2 in linear space */
void cs_pq_ootf(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3])
{
    int nc;

    for (nc = 0; nc < 3; nc++) {
        MATFLOAT e = rgb_inp[nc] * 59.5208;
        MATFLOAT e709 = (e <= 0.018) ? 4.5 * e : 1.099 * mat_pow(e, 0.45) - 0.099; /* OETF 709 */
        MATFLOAT e1886 = mat_pow(e709, 2.4) / 100.0; /* EOTF 1886 */

        rgb_out[nc] = MAT_CLAMP(e1886, 0.0, 1.0);
    }
}

/* BT.2390 display referred */
/* rgb_inp[] in a range [0,1] normalized to [0,100]cd/m^2 in non-linear space */
/* rgb_out[] in a range [0,1] normalized to [0,10000]cd/m^2 in non-linear space */
void cs_sdr_to_pq(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT en_709_2020)
{
    MATFLOAT sdr_lb = 0.0;
    MATFLOAT sdr_lw = 100.0 / CS_MAX_LUMINANCE;
    MATFLOAT sdr_gamma = 2.4;
    MATFLOAT scale = 2.0;
    MATFLOAT rgb_lin[3];
    int nc;

    for (nc = 0; nc < 3; nc++)
        rgb_lin[nc] = cs_gamma_1886(rgb_inp[nc], sdr_lb, sdr_lw, sdr_gamma); /* [0,10000]cd/m^2 */

    if (en_709_2020) {
        MATFLOAT rgb_tmp[3];

        mat_copy(rgb_lin, rgb_tmp, 3);
        mat_eval_3x3(cs_mat_709_2020, rgb_tmp, rgb_lin); /* [0,10000]cd/m^2 */
    }

    for (nc = 0; nc < 3; nc++)
        rgb_lin[nc] = rgb_lin[nc] * scale; /* scale to 200cd/m^2 */

    cs_gamma_rgb(rgb_lin, rgb_out, (MATFLOAT *)cs_get_gamma(EGT_PQ), EGD_LIN_2_NONLIN); /* [0,10000]cd/m^2 */
}

void cs_gamma_rgb(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT gamma_parm[4], enum cs_gamma_dir gamma_dir)
{    /* output may be the same as input */
    int nc;

    for (nc = 0; nc < 3; nc++)
        rgb_out[nc] = cs_gamma(rgb_inp[nc], gamma_parm, gamma_dir);
}

int cs_min_rgb(MATFLOAT rgb[3], MATFLOAT val_min)
{
    int is_clip = 0;
    int nc;

    for (nc = 0; nc < 3; nc++) {
        MATFLOAT value = rgb[nc];

        rgb[nc] = MAT_MAX(value, val_min);
        is_clip |= (rgb[nc] == value) ? 0 : 1;
    }

    return is_clip;
}

int cs_max_rgb(MATFLOAT rgb[3], MATFLOAT val_max)
{
    int is_clip = 0;
    int nc;

    for (nc = 0; nc < 3; nc++) {
        MATFLOAT value = rgb[nc];

        rgb[nc] = MAT_MIN(value, val_max);
        is_clip |= (rgb[nc] == value) ? 0 : 1;
    }

    return is_clip;
}

int cs_is_valid_ic(struct s_color_space *ptr_color_space, MATFLOAT pnt_ic[2], MATFLOAT hue_sin_cos[2])
{
    MATFLOAT pnt_itp[3];

    pnt_itp[0] = pnt_ic[0];
    pnt_itp[1] = pnt_ic[1] * hue_sin_cos[1];
    pnt_itp[2] = pnt_ic[1] * hue_sin_cos[0];

    return cs_is_valid_itp(ptr_color_space, pnt_itp);
}

int cs_is_valid_itp(struct s_color_space *ptr_color_space, MATFLOAT itp[3])
{
    MATFLOAT rgb[3];

    cs_itp_to_rgb(ptr_color_space, itp, rgb);

    return cs_is_valid_rgb(rgb, ptr_color_space->luminance_limits[0], ptr_color_space->luminance_limits[1]);
}

int cs_is_valid_rgb(MATFLOAT rgb[3], MATFLOAT val_min, MATFLOAT val_max)
{
    return mat_is_valid_vec(rgb, 3, val_min, val_max);
}

int cs_clip_rgb(MATFLOAT rgb[3], MATFLOAT val_min, MATFLOAT val_max)
{
    int is_clip = cs_is_valid_rgb(rgb, val_min, val_max);

    if (is_clip == 0)
        cs_clamp_rgb(rgb, val_min, val_max);

    return is_clip ? 0 : 1;
}

void cs_clamp_rgb(MATFLOAT rgb[3], MATFLOAT val_min, MATFLOAT val_max)
{
    int nc;

    for (nc = 0; nc < 3; nc++)
        rgb[nc] = mat_clamp(rgb[nc], val_min, val_max);
}

void cs_norm_rgb(MATFLOAT rgb[3], MATFLOAT val_min, MATFLOAT val_rng)
{
    int nc;

    for (nc = 0; nc < 3; nc++)
        rgb[nc] = mat_norm(rgb[nc], val_min, val_rng);
}

void cs_denorm_rgb(MATFLOAT rgb[3], MATFLOAT val_min, MATFLOAT val_rng)
{
    int nc;

    for (nc = 0; nc < 3; nc++)
        rgb[nc] = mat_denorm(rgb[nc], val_min, val_rng);
}

void cs_int2flt_rgb(int rgb_inp[3], MATFLOAT rgb_out[3], int val_max)
{
    int nc;

    for (nc = 0; nc < 3; nc++)
        rgb_out[nc] = mat_int2flt(rgb_inp[nc], val_max);
}

void cs_flt2int_rgb(MATFLOAT rgb_inp[3], int rgb_out[3], int val_max)
{
    int nc;

    for (nc = 0; nc < 3; nc++)
        rgb_out[nc] = mat_flt2int(rgb_inp[nc], val_max);
}


void cs_short2flt_rgb(unsigned short rgb_inp[3], MATFLOAT rgb_out[3], int val_max)
{
    int nc;

    for (nc = 0; nc < 3; nc++)
        rgb_out[nc] = mat_int2flt(rgb_inp[nc], val_max);
}

void cs_flt2short_rgb(MATFLOAT rgb_inp[3], unsigned short rgb_out[3], int val_max)
{
    int nc;

    for (nc = 0; nc < 3; nc++)
        rgb_out[nc] = mat_flt2int(rgb_inp[nc], val_max);
}

void cs_genprim_itp(struct s_color_space *ptr_color_space, int num_prim,
        MATFLOAT *ptr_prim_rgb, MATFLOAT *ptr_prim_ich)
{
    int nk, nc;

    for (nk = 0; nk < num_prim; nk++) {
        MATFLOAT rgb[3], vec_itp[3], vec_ich[3];

        mat_copy(&ptr_prim_rgb[3 * nk], rgb, 3);
        cs_denorm_rgb(rgb, ptr_color_space->luminance_limits[0], ptr_color_space->luminance_limits[2]);
        cs_rgb_to_itp(ptr_color_space, rgb, vec_itp);
        cs_itp_to_ich(vec_itp, vec_ich);
        for (nc = 0; nc < 3; nc++)
            ptr_prim_ich[num_prim * nc + nk] = vec_ich[nc];
    }
}

MATFLOAT cs_soft_clip(MATFLOAT val, MATFLOAT limits_src[3], MATFLOAT limits_dst[3])
{    /* Based on BT.2390 - Src must be wider then Dst */
    const MATFLOAT epsilon = 0.000001;
    MATFLOAT val_min = (limits_dst[0] - limits_src[0]) / (limits_src[1] - limits_src[0]);
    MATFLOAT val_max = (limits_dst[1] - limits_src[0]) / (limits_src[1] - limits_src[0]);
    MATFLOAT ks = (1.5 * val_max) - 0.5;
    MATFLOAT e0, e1, e2, e3, e4;

    /* Input value must be normilized to [0.0,1.0] */
    e0 = val;
    e1 = mat_norm(e0, limits_src[0], limits_src[2]);
    e1 = mat_clamp(e1, 0.0, 1.0);

    if (e1 < ks)
        e2 = e1;
    else {
        MATFLOAT t = ((1.0 - ks) <= epsilon) ? (e1 - ks) : ((e1 - ks) / (1.0 - ks));
        MATFLOAT t2 = t * t;
        MATFLOAT t3 = t2 * t;

        e2 = (((2.0 * t3) - (3.0 * t2) + 1.0) * ks) + ((t3 - (2.0 * t2) + t) * (1.0 - ks)) + (((-2.0 * t3) +
            (3.0 * t2)) * val_max);
    }
    e3 = e2 + val_min * mat_pow((1.0 - e2), 4.0);

    /* Output value must be denormilized back to [limits_src[0], limits_src[1]] */
    e4 = mat_denorm(e3, limits_src[0], limits_src[2]);
    e4 = mat_clamp(e4, limits_src[0], limits_src[1]);

    return e4;
}

MATFLOAT cs_gamma_to_gamma(MATFLOAT val, enum cs_gamma_type gamma_type_src, enum cs_gamma_type gamma_type_dst,
    MATFLOAT luminance_limits_dst[3], MATFLOAT luma_limits_src[3], MATFLOAT luma_limits_dst[3],
    MATFLOAT(*func_pq_to_pq)(MATFLOAT), int en_norm, int en_soft_clip)
{
    MATFLOAT val_out = cs_gamma(val, (MATFLOAT *)cs_get_gamma(gamma_type_src), EGD_NONLIN_2_LIN);    /* degamma */

    if (en_norm)
        val_out = mat_denorm(val_out, luminance_limits_dst[0], luminance_limits_dst[2]);/* denorm */
    val_out = mat_clamp(val_out, luminance_limits_dst[0], luminance_limits_dst[1]);        /* clamp */
    val_out = cs_gamma_pq(val_out, EGD_LIN_2_NONLIN);    /* LIN2PQ */
    val_out = func_pq_to_pq(val_out);                    /* PQ2PQ transform */
    if (en_soft_clip)
        val_out = cs_soft_clip(val_out, luma_limits_src, luma_limits_dst);    /* SoftClip */
    val_out = cs_gamma_pq(val_out, EGD_NONLIN_2_LIN);    /* PQ2LIN */
    if (en_norm)
        val_out = mat_norm(val_out, luminance_limits_dst[0], luminance_limits_dst[2]);    /* norm */
    val_out = mat_clamp(val_out, 0.0, 1.0);            /* clamp */
    val_out = cs_gamma(val_out, (MATFLOAT *)cs_get_gamma(gamma_type_dst), EGD_LIN_2_NONLIN);    /* regamma */

    return val_out;
}

int cs_xy_to_cct(MATFLOAT xy[2])
{ /* McCamy�s polynomial formula for CCT */
    MATFLOAT val = (xy[0] - 0.3320) / (xy[1] - 0.1858);
    MATFLOAT val2 = val * val;
    MATFLOAT val3 = val * val2;
    MATFLOAT cct = -449.0 * val3 + 3525.0 * val2 - 6823.0 * val + 5520.33;

    return MAT_ROUND(cct);
}

void cs_cct_to_xy(int cct, MATFLOAT xy[2])
{
    int val = MAT_CLAMP(cct, CS_CCT_MIN, CS_CCT_MAX) - CS_CCT_MIN;
    int vec_ind[2];
    MATFLOAT phase;
    MATFLOAT vec_x[2], vec_y[2];

    vec_ind[0] = val / CS_CCT_INC;
    vec_ind[1] = MAT_MIN(vec_ind[0] + 1, CS_CCT_SIZE - 1);
    phase = (MATFLOAT)(val - vec_ind[0] * CS_CCT_INC) / (MATFLOAT)CS_CCT_INC;

    vec_x[0] = cs_vec_cct_xy[2 * vec_ind[0] + 0];
    vec_x[1] = cs_vec_cct_xy[2 * vec_ind[1] + 0];
    vec_y[0] = cs_vec_cct_xy[2 * vec_ind[0] + 1];
    vec_y[1] = cs_vec_cct_xy[2 * vec_ind[1] + 1];

    xy[0] = mat_linear(vec_x, phase);
    xy[1] = mat_linear(vec_y, phase);
}

void cs_csc(struct s_color_space *ptr_cs_src, struct s_color_space *ptr_cs_dst,
    MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], int en_chad)
{
    MATFLOAT rgb_tmp[3];
    MATFLOAT mat_remap[3][3];

    cs_genmat_rgb_to_rgb(ptr_cs_src->rgbw_xy, ptr_cs_dst->rgbw_xy, mat_remap, en_chad);

    cs_nlin_to_lin_rgb(ptr_cs_src, rgb_inp, rgb_tmp);
    mat_eval_3x3(mat_remap, rgb_tmp, rgb_out);
    cs_clamp_rgb(rgb_out, 0.0, 1.0);
    cs_lin_to_nlin_rgb(ptr_cs_dst, rgb_out, rgb_out);
}

int cs_is_space(struct s_color_space *ptr_color_space,
    enum cs_color_space_type color_space_type, enum cs_gamma_type gamma_type)
{
    return ((ptr_color_space->color_space_type == color_space_type) &&
        (ptr_color_space->gamma_type == gamma_type)) ? 1 : 0;
}

void cs_init_type(MATFLOAT luminance_limits[2],
    enum cs_color_space_type color_space_type, enum cs_gamma_type gamma_type,
    struct s_color_space *ptr_color_space)
{
    struct s_cs_opts cs_opts = {0};

    cs_opts.color_space_type = color_space_type;
    cs_opts.gamma_type = gamma_type;
    cs_opts.mode = 0;
    cs_opts.pq_norm = 0.0;
    cs_opts.luminance_limits[0] = luminance_limits[0];
    cs_opts.luminance_limits[1] = luminance_limits[1];

    cs_init(&cs_opts, ptr_color_space);
}

void cs_init_BT709(MATFLOAT luminance_limits[2], struct s_color_space *ptr_color_space)
{
    cs_init_type(luminance_limits, ECST_709, EGT_709, ptr_color_space);
}

void cs_init_BT2100(MATFLOAT luminance_limits[2], struct s_color_space *ptr_color_space)
{
    cs_init_type(luminance_limits, ECST_BT2020, EGT_PQ, ptr_color_space);
}

void cs_rgb_to_ycbcr2020(MATFLOAT rgb_inp[3], MATFLOAT ycbcr_out[3])
{    /* ITU-R BT.2020 */
    ycbcr_out[0] = 0.2627 * rgb_inp[0] + 0.678 * rgb_inp[1] + 0.0593 * rgb_inp[2];
    ycbcr_out[1] = (rgb_inp[2] - ycbcr_out[0]) / 1.8814;
    ycbcr_out[2] = (rgb_inp[0] - ycbcr_out[0]) / 1.4746;
}

/* gamma = 1.2 - for reference display (1000 cd/m^2) and reference ambient light (5 cd/m^2) */
/* luminance_peak in a range [0,1] normilized to [0,10000]cd/m^2 in linear space */
MATFLOAT cs_ootf_gamma_peak(MATFLOAT gamma, MATFLOAT luminance_peak)
{    /* gamma correction for peak luminance of the display */
    return gamma * mat_pow(1.111, mat_log2(luminance_peak / 0.1));    /* normzlized to 1000 nits */
}

/* gamma = 1.2 - for reference display (1000 cd/m^2) and reference ambient light (5 cd/m^2) */
/* luminance_ambient in a range [0,1] normalized to [0,10000]cd/m^2 in linear space - ambient light in linear space */
MATFLOAT cs_ootf_gamma_amb(MATFLOAT gamma, MATFLOAT luminance_ambient)
{    /* gamma correction for ambient light */
    return gamma * mat_pow(0.98, mat_log2(luminance_ambient / 0.0005));    /* normalized to 5 nits */
}

MATFLOAT cs_gamma_adjust_sdr(MATFLOAT gamma, MATFLOAT luminance_peak)
{
    /* gamma correction for peak luminance of the display */
    if (luminance_peak <= 0.1)
        gamma = gamma * mat_pow(1.111, mat_log2(luminance_peak / 0.01));
    else if ((luminance_peak > 0.1) && (luminance_peak < 0.2))
        gamma = gamma + ((luminance_peak > 0.1) ? 0.42 * mat_log10(luminance_peak / 0.1) : 0.0);
    else
        gamma = gamma * mat_pow(1.111, mat_log2(luminance_peak / 0.1));

    return gamma;
}

void cs_chad_gains(MATFLOAT rgbw_xy[8], MATFLOAT w_xy[2], MATFLOAT rgb_gain[3])
{
    MATFLOAT rgb_white[3] = { 1.0, 1.0, 1.0 };
    MATFLOAT max_gain = 0.0;
    MATFLOAT mat_rgb2xyz[3][3], mat_xyz2rgb[3][3];
    MATFLOAT mat_chad[3][3];
    MATFLOAT xyz_inp[3], xyz_out[3];
    int nc;

    /* generate RGB to XYZ and back transformation matrixes */
    cs_genmat_rgb_to_xyz(rgbw_xy, mat_rgb2xyz);
    mat_inv3x3(mat_rgb2xyz, mat_xyz2rgb);
    /* generate matrix of white point conversion from display to target */
    cs_genmat_chad(&rgbw_xy[6], w_xy, mat_chad);
    /* map white to gains */
    mat_eval_3x3(mat_rgb2xyz, rgb_white, xyz_inp);
    mat_eval_3x3(mat_chad, xyz_inp, xyz_out);
    mat_eval_3x3(mat_xyz2rgb, xyz_out, rgb_gain);
    /* normalize gains to max */
    for (nc = 0; nc < 3; nc++)
        max_gain = MAT_MAX(max_gain, rgb_gain[nc]);
    for (nc = 0; nc < 3; nc++)
        rgb_gain[nc] = rgb_gain[nc] / max_gain;
}

void cs_genmat_cct(struct s_color_space *ptr_cs, int cct_shift, int norm, MATFLOAT mat_cct[3][3])
{
    MATFLOAT xy[2];
    MATFLOAT mat_chad[3][3];
    MATFLOAT mat_tmp[3][3];

    cs_cct_to_xy(ptr_cs->cct + cct_shift, xy);
    cs_genmat_chad(&ptr_cs->rgbw_xy[6], xy, mat_chad);
    mat_mul3x3(mat_chad, ptr_cs->mat_rgb2xyz, mat_tmp);
    mat_mul3x3(ptr_cs->mat_xyz2rgb, mat_tmp, mat_cct);

    if (norm) {
        MATFLOAT rgb_white[3] = { 1.0, 1.0, 1.0 };
        MATFLOAT max_gain = 0.0;
        MATFLOAT rgb_gain[3];
        int nc, ni;

        mat_eval_3x3(mat_cct, rgb_white, rgb_gain);
        for (nc = 0; nc < 3; nc++)
            max_gain = MAT_MAX(max_gain, rgb_gain[nc]);
        for (nc = 0; nc < 3; nc++)
            for (ni = 0; ni < 3; ni++)
                mat_cct[nc][ni] = mat_cct[nc][ni] / max_gain;
    }
}

int cs_rgb_to_vsh(MATFLOAT rgb[3], MATFLOAT vsh[3])
{
    MATFLOAT r = rgb[0];
    MATFLOAT g = rgb[1];
    MATFLOAT b = rgb[2];
    MATFLOAT val_min, val_max, delta;

    val_max = (g > b) ? g : b;
    if (r > val_max)
        val_max = r;

    val_min = (g < b) ? g : b;
    if (r < val_min)
        val_min = r;

    vsh[0] = val_max;
    delta = val_max - val_min;

    if ((val_max != 0.0) && (delta != 0.0))
        vsh[1] = delta / val_max;
    else {
        vsh[2] = 0.0;
        vsh[1] = 0.0;
        return 1;
    }

    if (r == val_max)
        vsh[2] = (g - b) / delta;
    else if (g == val_max)
        vsh[2] = 2.0 + (b - r) / delta;
    else
        vsh[2] = 4.0 + (r - g) / delta;

    vsh[2] = vsh[2] * mat_get_pi() / 3.0;
    vsh[2] = mat_norm_angle(vsh[2]);    /* [0.0, 2PI) */

    return 0;
}

void cs_vsh_to_rgb(MATFLOAT vsh[3], MATFLOAT rgb[3])
{
    MATFLOAT v = vsh[0];
    MATFLOAT s = vsh[1];

    MATFLOAT r = v;
    MATFLOAT g = v;
    MATFLOAT b = v;

    if (s > 0.0) {
        MATFLOAT h = 3.0 * vsh[2] / mat_get_pi();
        int ni = MAT_CLAMP((int)h, 0, 5);
        MATFLOAT f = h - (MATFLOAT)ni;
        MATFLOAT p = v * (1.0 - s);
        MATFLOAT q = v * (1.0 - s * f);
        MATFLOAT t = v * (1.0 - s * (1.0 - f));

        switch (ni) {
        case 0:
            r = v;
            g = t;
            b = p;
            break;
        case 1:
            r = q;
            g = v;
            b = p;
            break;
        case 2:
            r = p;
            g = v;
            b = t;
            break;
        case 3:
            r = p;
            g = q;
            b = v;
            break;
        case 4:
            r = t;
            g = p;
            b = v;
            break;
        case 5:
            r = v;
            g = p;
            b = q;
            break;
        }
    }

    rgb[0] = r;
    rgb[1] = g;
    rgb[2] = b;
}

/* YUV functions */
void cs_yuv_to_ysh(MATFLOAT yuv_inp[3], MATFLOAT ysh_out[3])
{
    ysh_out[0] = yuv_inp[0];
    ysh_out[1] = mat_radius(yuv_inp[2] - 0.5, yuv_inp[1] - 0.5);
    ysh_out[2] = mat_angle(yuv_inp[2] - 0.5, yuv_inp[1] - 0.5);
}

void cs_ysh_to_yuv(MATFLOAT ysh_inp[3], MATFLOAT yuv_out[3])
{
    yuv_out[0] = ysh_inp[0];
    yuv_out[1] = ysh_inp[1] * mat_cos(ysh_inp[2]) + 0.5;
    yuv_out[2] = ysh_inp[1] * mat_sin(ysh_inp[2]) + 0.5;
}

/* CIE LAB functions */
void cs_rgb_to_lab(MATFLOAT rgb[3], MATFLOAT lab[3], struct s_color_space *ptr_color_space)
{
    MATFLOAT xyz[3];

    cs_gamma_rgb(rgb, rgb, ptr_color_space->gamma_parm, EGD_NONLIN_2_LIN);
    mat_eval_3x3(ptr_color_space->mat_rgb2xyz, rgb, xyz);
    cs_xyz_to_lab(xyz, lab, ptr_color_space->white_xyz);
}

void cs_lab_to_rgb(MATFLOAT lab[3], MATFLOAT rgb[3], struct s_color_space *ptr_color_space)
{
    MATFLOAT xyz[3];

    cs_lab_to_xyz(lab, xyz, ptr_color_space->white_xyz);
    mat_eval_3x3(ptr_color_space->mat_xyz2rgb, xyz, rgb);
    cs_clip_rgb(rgb, 0.0, 1.0);
    cs_gamma_rgb(rgb, rgb, ptr_color_space->gamma_parm, EGD_LIN_2_NONLIN);
}

void cs_xyz_to_lab(MATFLOAT xyz[3], MATFLOAT lab[3], MATFLOAT white_xyz[3])
{
    int nc;
    MATFLOAT f[3], ft;

    for (nc = 0; nc < 3; nc++) {
        ft = xyz[nc] / white_xyz[nc];
        f[nc] = (ft > CS_LAB_E) ? mat_pow(ft, 1.0 / 3.0) : (CS_LAB_K * ft + 16.0) / 116.0;
    }

    lab[0] = 116.0f * f[1] - 16.0;
    lab[1] = 500.0f * (f[0] - f[1]);
    lab[2] = 200.0f * (f[1] - f[2]);
}

void cs_lab_to_xyz(MATFLOAT lab[3], MATFLOAT xyz[3], MATFLOAT white_xyz[3])
{
    int nc;
    MATFLOAT f[3];
    MATFLOAT ft = (lab[0] + 16.0) / 116.0;

    f[0] = ft + lab[1] / 500.0;
    f[1] = ft;
    f[2] = ft - lab[2] / 200.0;

    xyz[0] = mat_pow(f[0], 3.0);
    if (xyz[0] <= CS_LAB_E)
        xyz[0] = (116.0 * f[0] - 16.0) / CS_LAB_K;

    if (lab[0] > CS_LAB_K * CS_LAB_E)
        xyz[1] = mat_pow((lab[0] + 16.0) / 116.0, 3.0);
    else
        xyz[1] = lab[0] / CS_LAB_K;

    xyz[2] = mat_pow(f[2], 3.0);
    if (xyz[2] <= CS_LAB_E)
        xyz[2] = (116.0 * f[2] - 16.0) / CS_LAB_K;

    for (nc = 0; nc < 3; nc++)
        xyz[nc] *= white_xyz[nc];
}

MATFLOAT cs_de94(MATFLOAT lab0[3], MATFLOAT lab1[3])
{
    static const MATFLOAT Kc = 1.0;
    static const MATFLOAT Kh = 1.0;
    static const MATFLOAT Kl = 1.0;
    static const MATFLOAT K1 = 0.045;
    static const MATFLOAT K2 = 0.015;

    MATFLOAT dL = lab0[0] - lab1[0];
    MATFLOAT C1 = mat_sqrt(lab0[1] * lab0[1] + lab0[2] * lab0[2]);
    MATFLOAT C2 = mat_sqrt(lab1[1] * lab1[1] + lab1[2] * lab1[2]);
    MATFLOAT dC = C1 - C2;

    MATFLOAT da = lab0[1] - lab1[1];
    MATFLOAT db = lab0[2] - lab1[2];
    MATFLOAT tmp = da * da + db * db - dC * dC;
    MATFLOAT dH = (tmp > 0) ? mat_sqrt(tmp) : 0.0;

    MATFLOAT Sl = 1.0;
    MATFLOAT Sc = 1.0 + K1 * C1;
    MATFLOAT Sh = 1.0 + K2 * C1;

    dL /= (Kl * Sl);
    dC /= (Kc * Sc);
    dH /= (Kh * Sh);

    return mat_sqrt(dL * dL + dC * dC + dH * dH);
}

/* gamma = 1.2 - for reference display (1000 cd/m^2) and reference ambient light (5 cd/m^2) */
/* luminance_peak in a range [0,1] normilized to [0,10000]cd/m^2 in linear space */
/* luminance_amb in a range [0,1] normalized to [0,10000]cd/m^2 in linear space - ambient light in linear space */
MATFLOAT cs_gamma_adjust(MATFLOAT gamma, MATFLOAT luminance_peak, MATFLOAT luminance_amb)
{
    /* gamma correction for peak luminance of the display */
    if (luminance_peak < 0.2)
        gamma = gamma + ((luminance_peak > 0.1) ? 0.42 * mat_log10(luminance_peak / 0.1) : 0.0);
    else
        gamma = gamma * mat_pow(1.111, mat_log2(luminance_peak / 0.1));
    /* gamma correction for ambient light */
    gamma = gamma - 0.076 * mat_log10(luminance_amb / 5.0);

    return gamma;
}

/* BT.2100 */
/* input must be in arange [0,1] normilized to [0,Lw]cd/m^2 in linear or non-linear space */
/* output must be in a range [0,1] normilized to [0,Lw]cd/m^2 in linear or non-linear space */
MATFLOAT cs_gamma_hlg(MATFLOAT val, enum cs_gamma_dir gamma_dir)
{
    static const MATFLOAT s_a = 0.17883277;
    static const MATFLOAT s_b = 0.28466892;
    static const MATFLOAT s_c = 0.55991073;

    MATFLOAT val_out;

    if (gamma_dir == EGD_LIN_2_NONLIN)
        val_out = (val <= (1.0 / 12.0)) ? mat_sqrt(3.0 * val) : s_a * mat_log(12.0 * val - s_b) + s_c;
    else
        val_out = (val <= 0.5) ? val * val / 3.0 : (mat_exp((val - s_c) / s_a) + s_b) / 12.0;

    return MAT_CLAMP(val_out, 0.0, 1.0);
}

/* HLG OOTF */
/* rgb_inp[] in a range [0,1] normalized to [0,Lw]cd/m^2 in linear space */
/* rgb_out[] in a range [0,1] normalized to [0,10000]cd/m^2 in linear space */
/* luminance_peak in a range [0,1] normalized to [0,10000]cd/m^2 in linear space - mastering Lb and Lw */
/* system_gamma = 1.2 - for reference display (1000 cd/m^2) and reference ambient light (5 cd/m^2) */
void cs_hlg_ootf(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT luminance_peak, MATFLOAT system_gamma)
{    /* output may be the same as input */
    MATFLOAT ys = 0.2627 * rgb_inp[0] + 0.6780 * rgb_inp[1] + 0.0593 * rgb_inp[2];
    MATFLOAT scale = mat_pow(ys, system_gamma - 1.0);
    int nc;

    for (nc = 0; nc < 3; nc++) {
        rgb_out[nc] = rgb_inp[nc] * scale * luminance_peak;
        rgb_out[nc] = MAT_CLAMP(rgb_out[nc], 0.0, 1.0);
    }
}

/* HLG OOTF_INV */
/* rgb_inp[] in a range [0,1] normalized to [0,10000]cd/m^2 in linear space */
/* rgb_out[] in a range [0,1] normalized to [0,Lw]cd/m^2 in linear space */
/* luminance_peak in a range [0,1] normalized to [0,10000]cd/m^2 in linear space - mastering Lb and Lw */
/* system_gamma = 1.2 - for reference display (1000 cd/m^2) and reference ambient light (5 cd/m^2) */
void cs_hlg_ootf_inv(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT luminance_peak, MATFLOAT system_gamma)
{    /* output may be the same as input */
    MATFLOAT yd = (0.2627 * rgb_inp[0] + 0.6780 * rgb_inp[1] + 0.0593 * rgb_inp[2]) / luminance_peak;
    MATFLOAT scale = mat_pow(yd, (1.0 - system_gamma) / system_gamma) / luminance_peak;
    int nc;

    for (nc = 0; nc < 3; nc++) {
        rgb_out[nc] = rgb_inp[nc] * scale;
        rgb_out[nc] = MAT_CLAMP(rgb_out[nc], 0.0, 1.0);
    }
}

/* HLG OETF */
/* rgb_inp[] in a range [0,1] normalized to [0,Lw]cd/m^2 in linear space */
/* rgb_out[] in a range [0,1] normalized to [0,Lw]cd/m^2 in non-linear space */
/* luminance_peak in a range [0,1] normalized to [0,10000]cd/m^2 in linear space - mastering Lb and Lw */
/* system_gamma = 1.2 - for reference display (1000 cd/m^2) and reference ambient light (5 cd/m^2) */
void cs_hlg_oetf(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT luminance_peak, MATFLOAT system_gamma)
{    /* output may be the same as input */
    int nc;

    cs_hlg_ootf_inv(rgb_inp, rgb_out, luminance_peak, system_gamma);
    for (nc = 0; nc < 3; nc++)
        rgb_out[nc] = cs_gamma_hlg(rgb_out[nc], EGD_LIN_2_NONLIN);
}

/* HLG EOTF */
/* rgb_inp[] in a range [0,1] normalized to [0,Lw]cd/m^2 in non-linear space */
/* rgb_out[] in a range [0,1] normalized to [0,Lw]cd/m^2 in linear space */
/* vec_luminace in a range [0,1] normalized to [0,10000]cd/m^2 in linear space - mastering Lb and Lw */
/* system_gamma = 1.2 - for reference display (1000 cd/m^2) and reference ambient light (5 cd/m^2) */
/* beta - user black level lift (= 0.0) */
void cs_hlg_eotf(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT luminance_limits[3],
    MATFLOAT system_gamma, MATFLOAT beta)
{    /* output may be the same as input */
    int nc;

    for (nc = 0; nc < 3; nc++) {
        rgb_out[nc] = MAT_MAX((1.0 - beta) * rgb_inp[nc] + beta, 0.0);
        rgb_out[nc] = cs_gamma_hlg(rgb_out[nc], EGD_NONLIN_2_LIN);
    }
    cs_hlg_ootf(rgb_out, rgb_out, luminance_limits[1], system_gamma);
}

/* HLG system gamma calculation */
/* peak_luminance - Lw */
MATFLOAT cs_hlg_system_gamma(MATFLOAT peak_luminance)
{
    MATFLOAT norm_peak = peak_luminance / (1000.0 / CS_MAX_LUMINANCE);
    MATFLOAT system_gamma;

    if ((peak_luminance < 400.0 / CS_MAX_LUMINANCE) || (peak_luminance > 2000.0 / CS_MAX_LUMINANCE))
        system_gamma = 1.2 * mat_pow(1.111, mat_log2(norm_peak));
    else
        system_gamma = 1.2 + 0.42 * mat_log10(norm_peak);

    return system_gamma;
}

#if 0
/* PQ to HLG Transcode  */
/* rgb_inp[] in a range [0,1] normalized to [0,10000]cd/m^2 in non-linear space */
/* rgb_out[] in a range [0,1] normalized to [0,Lw]cd/m^2 in non-linear space */
/* luminance_peak in a range [0,1] normalized to [0,10000]cd/m^2 in linear space - mastering Lb and Lw */
/* gamma = 1.2 - for reference display (1000 cd/m^2) and reference ambient light (5 cd/m^2) */
void cs_pq_to_hlg(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT luminance_peak, MATFLOAT gamma)
{
    MATFLOAT rgb_lin[3];
    int nc;

    for (nc = 0; nc < 3; nc++)
        rgb_lin[nc] = cs_gamma_pq(rgb_inp[nc], EGD_NONLIN_2_LIN);    /* PQ to Linear [0,10000]->[0,10000] */

    cs_hlg_ootf_inv(rgb_lin, rgb_lin, luminance_peak, gamma);    /* OOTF-1 - [0,10000]->[0,Lw] */
    cs_hlg_oetf(rgb_lin, rgb_out, luminance_peak, gamma);    /* Linear to HLG - [0,Lw]->[0,Lw] */
}

/* HLG to PQ Transcode  */
/* rgb_inp[] in a range [0,1] normalized to [0,Lw]cd/m^2 in non-linear space */
/* rgb_out[] in a range [0,1] normalized to [0,10000]cd/m^2 in non-linear space */
/* vec_luminace in a range [0,1] normalized to [0,10000]cd/m^2 in linear space - mastering Lb and Lw */
/* gamma = 1.2 - for reference display (1000 cd/m^2) and reference ambient light (5 cd/m^2) */
void cs_hlg_to_pq(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT vec_luminance[3], MATFLOAT gamma)
{
    MATFLOAT rgb_lin[3];
    int nc;

    cs_hlg_eotf(rgb_inp, rgb_lin, vec_luminance, gamma);    /* HLG to Linear - [0,Lw]->[0,Lw] */
    cs_hlg_ootf(rgb_lin, rgb_lin, vec_luminance[1], gamma);    /* OOTF - [0,Lw]->[0,10000] */

    for (nc = 0; nc < 3; nc++)
        rgb_out[nc] = cs_gamma_pq(rgb_lin[nc], EGD_LIN_2_NONLIN);    /* Linear to PQ [0,10000]->[0,1000] */
}

/* BT.2390 display referred simplified */
/* rgb_inp[] in a range [0,1] normalized to [0,100]cd/m^2 in non-linear space */
/* rgb_out[] in a range [0,1] normalized to [0,1000]cd/m^2 in non-linear space */
void cs_sdr_to_hlg(MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3], MATFLOAT en_709_2020)
{
    MATFLOAT sdr_lb = 0.0;
    MATFLOAT sdr_lw = 100.0 / 10000.0;
    MATFLOAT sdr_gamma = 2.4;
    MATFLOAT scale = 0.2546; /* 0.75HLG = 392cd/m^2 */
    MATFLOAT hlg_lw = 1000.0 / 10000.0;
    MATFLOAT hlg_amb = 5.0 / 10000.0;
    MATFLOAT hlg_gamma = cs_gamma_adjust(1.2, hlg_lw, hlg_amb);
    MATFLOAT gamma = 1.03;
    MATFLOAT rgb_lin[3];
    int nc;

    for (nc = 0; nc < 3; nc++) {
        rgb_lin[nc] = cs_gamma_1886(rgb_inp[nc], sdr_lb, sdr_lw, sdr_gamma); /* [0,10000]cd/m^2 */
        rgb_lin[nc] = rgb_lin[nc] / sdr_lw; /* [0,sdr_lw]cd/m^2 */
        rgb_lin[nc] = MAT_CLAMP(rgb_lin[nc], 0.0, 1.0);
    }

    if (en_709_2020) {
        MATFLOAT rgb_tmp[3];

        mat_copy(rgb_lin, rgb_tmp, 3);
        mat_eval_3x3(cs_mat_709_2020, rgb_tmp, rgb_lin); /* [0,sdr_lw]cd/m^2 */
    }

    for (nc = 0; nc < 3; nc++) {
        rgb_lin[nc] = rgb_lin[nc] * scale; /* scale to 392cd/m^2 [0,hlg_lw] */
        rgb_lin[nc] = mat_pow(rgb_lin[nc], 1.0 / gamma); /* [0,hlg_lw] */
    }

    cs_hlg_oetf(rgb_lin, rgb_out, hlg_lw, hlg_gamma); /* Linear to HLG - [0,hlg_lw]cd/m^2->[0,hlg_lw]cd/m^2 */
}
#endif
