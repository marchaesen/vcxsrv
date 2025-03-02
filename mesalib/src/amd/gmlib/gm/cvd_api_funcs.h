/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : cvd_api_funcs.h
 * Purpose    : Color Vision Deficiency functions
 * Author     : Vladimir Lachine (vlachine@amd.com)
 * Date       : January 21, 2020
 * Version    : 1.0
 *----------------------------------------------------------------------
 *
 */

#pragma once

#include "cvd_funcs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct s_cvd_api_opts {
    /* cvd parameters */
    enum cvd_mode        mode;        /* CVD mode: 0 - NONE, 1 - 3 sliders, 2 - 1 slider*/
    MATFLOAT            gain[3];    /* Compensation Gain: ([0] - Protanopia, [1] - Deuteranopia, [2] - Tritanopia: [0.0,2.0]=0.0 */
    struct s_cs_opts    cs_opts;    /* Color Space parameters */
    /* 3DLUT parameters */
    int                en_merge_3dlut;
    int                num_pnts_3dlut;
    int                bitwidth_3dlut;
    unsigned short    *ptr_3dlut_rgb;
};

void cvd_api_set_def(struct s_cvd_api_opts *ptr_api_cvd_opts);

int cvd_api_gen_map(struct s_cvd_api_opts *ptr_api_cvd_opts, struct s_cvd_map *ptr_cvd_map);
int cvd_api_gen_3dlut(struct s_cvd_api_opts *ptr_api_cvd_opts, struct s_cvd_map *ptr_cvd_map);

#ifdef __cplusplus
}
#endif
