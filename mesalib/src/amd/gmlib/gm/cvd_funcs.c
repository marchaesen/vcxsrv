/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : cvd_funcs.c
 * Purpose    : Color Vision Deficiency functions
 * Author     : Vladimir Lachine (vlachine@amd.com)
 * Date       : January 21, 2020
 * Version    : 1.0
 *----------------------------------------------------------------------
 *
 */

#ifndef GM_SIM
#pragma code_seg("PAGED3PC")
#pragma data_seg("PAGED3PD")
#pragma const_seg("PAGED3PR")
#endif

#include "cvd_funcs.h"

void cvd_ctor(struct s_cvd_map *ptr_cvd_map)
{
    cvd_set_def(ptr_cvd_map);
}

void cvd_dtor(struct s_cvd_map *ptr_cvd_map)
{
    cvd_set_def(ptr_cvd_map);
}

void cvd_set_def(struct s_cvd_map *ptr_cvd_map)
{
    int nk;

    ptr_cvd_map->mode = ECM_NONE;

    for (nk = 0; nk < 3; nk++)
        ptr_cvd_map->gain[nk] = 0.0;

}

int cvd_rgb_to_rgb(struct s_cvd_map *ptr_cvd_map, MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3])
{
    int rc = 0;

    if (ptr_cvd_map->mode != ECM_NONE)
        rc = cvd_rgb_to_rgb_dalton(ptr_cvd_map, rgb_inp, rgb_out);
    else
        mat_copy(rgb_inp, rgb_out, 3);

    return rc;
}

void cvd_model_rgb(struct s_color_space *ptr_color_space, MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3],
    enum cvd_type type)
{
    static MATFLOAT cvd_mat_rgb2lms[3][3] = {
        {17.8824, 43.5161, 4.11935},
        {3.45565, 27.1554, 3.86714},
        {0.0299566, 0.184309, 1.46709}
    };
    static MATFLOAT cvd_mat_lms2rgb[3][3] = {
        { 0.080944, -0.130504, 0.116721},
        {-0.0102485, 0.0540194, -0.113615},
        {-0.000365294, -0.00412163, 0.693513}
    };
    static MATFLOAT cvd_mat_model[ECVDT_NUM][3][3] = {
        {/* protanopia */     {0.0, 2.02324, -2.52581}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}},
        {/* deuteranopia */   {1.0, 0.0, 0.0}, {0.494207, 0.0, 1.24827}, {0.0, 0.0, 1.0}},
//      {/* tritanopia */     {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {-0.395913, 0.801109, 0.0}}
        {/* tritanopia */     {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {-0.012245, 0.0720345, 0.0}}
    };

    MATFLOAT lms_inp[3], lms_out[3];

    mat_eval_3x3(cvd_mat_rgb2lms, rgb_inp, lms_inp);
    mat_eval_3x3(cvd_mat_model[type], lms_inp, lms_out);
    mat_eval_3x3(cvd_mat_lms2rgb, lms_out, rgb_out);
    cs_clamp_rgb(rgb_out, 0.0, 1.0);
}

int cvd_rgb_to_rgb_dalton(struct s_cvd_map *ptr_cvd_map, MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3])
{
    static MATFLOAT cvd_mat_err[ECVDT_NUM][3][3] = {
        {/* protanopia */      {-0.5, 0.0, 0.0}, {1.0,  1.0, 0.0}, {1.0, 0.0,  1.0}},
        {/* deuteranopia */    { 1.0, 1.0, 0.0}, {0.0, -0.5, 0.0}, {0.0, 1.0,  1.0}},
        {/* tritanopia */      { 1.0, 0.0, 1.0}, {0.0,  1.0, 1.0}, {0.0, 0.0, -0.5}}
    };

    MATFLOAT rgb_inp_lin[3], rgb_out_lin[3];
    MATFLOAT rgb_err_map[ECVDT_NUM][3];
    MATFLOAT err_map;
    MATFLOAT gain;
    int nc, nk;

    cs_gamma_rgb(rgb_inp, rgb_inp_lin, ptr_cvd_map->color_space.gamma_parm, EGD_NONLIN_2_LIN);
    mat_copy(rgb_inp_lin, rgb_out_lin, 3);

    for (nk = 0; nk < 3; nk++) {
        MATFLOAT rgb_cvd[3], rgb_err[3];

        cvd_model_rgb(&ptr_cvd_map->color_space, rgb_inp_lin, rgb_cvd, nk);
        for (nc = 0; nc < 3; nc++)
            rgb_err[nc] = rgb_inp_lin[nc] - rgb_cvd[nc];
        mat_eval_3x3(cvd_mat_err[nk], rgb_err, rgb_err_map[nk]);
    }

    if (ptr_cvd_map->mode == ECM_DALTON_SLD3) {    /* ECM_DALTON_SLD3 */
        for (nk = 0; nk < 3; nk++) {
            gain = ptr_cvd_map->gain[nk] * 0.5;
            for (nc = 0; nc < 3; nc++)
                rgb_out_lin[nc] += rgb_err_map[nk][nc] * gain;
        }
    } else {    /* ECM_DALTON_SLD1 */
        for (nc = 0; nc < 3; nc++) {
            if (ptr_cvd_map->gain[0] <= 1.0)
                err_map = ptr_cvd_map->gain[0] * rgb_err_map[0][nc];
            else if (ptr_cvd_map->gain[0] <= 2.0)
                err_map = rgb_err_map[0][nc] + (ptr_cvd_map->gain[0] - 1.0) * (rgb_err_map[1][nc] - rgb_err_map[0][nc]);
            else
                err_map = rgb_err_map[1][nc] + (ptr_cvd_map->gain[0] - 2.0) * (rgb_err_map[2][nc] - rgb_err_map[1][nc]);
            rgb_out_lin[nc] += err_map;
        }
    }

    cs_clamp_rgb(rgb_out_lin, 0.0, 1.0);
    cs_gamma_rgb(rgb_out_lin, rgb_out, ptr_cvd_map->color_space.gamma_parm, EGD_LIN_2_NONLIN);

    return 0;
}
