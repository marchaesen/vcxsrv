/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : cvd_funcs.h
 * Purpose    : Color Vision Deficiency functions
 * Author     : Vladimir Lachine (vlachine@amd.com)
 * Date       : January 21, 2020
 * Version    : 1.0
 *----------------------------------------------------------------------
 *
 */

#pragma once

#include "cs_funcs.h"

#ifdef __cplusplus
extern "C" {
#endif

enum cvd_mode {
    ECM_NONE = 0,    /* NONE */
    ECM_DALTON_SLD3 = 1,    /* DALTONIZATION 3 control sliders */
    ECM_DALTON_SLD1 = 2,    /* DALTONIZATION 1 control slider */
    ECM_NUM = 3
};

enum cvd_type {
    ECVDT_PROTANOPIA = 0,    /* protanopia */
    ECVDT_DEUTERANOPIA = 1,    /* deuteranopia */
    ECVDT_TRITANOPIA = 2,    /* tritanopia */
    ECVDT_NUM = 3
};

struct s_cvd_map {
    /* input parameters */
    enum cvd_mode            mode;            /* Enable/disable CVD: {0,1,2}=0 */
    MATFLOAT                gain[3];        /* Compensation Gain: ([0] - Protanopia, [1] - Deuteranopia, [2] - Tritanopia: [0.0,2.0]=0.0 */
    struct s_color_space    color_space;    /* Color Space (primary RGBW chromaticity, gamma, and Luminance min/max) */
};

/* constructor and destructor */
void cvd_ctor(struct s_cvd_map *ptr_cvd_map);
void cvd_dtor(struct s_cvd_map *ptr_cvd_map);

void cvd_set_def(struct s_cvd_map *ptr_cvd_map);

int cvd_rgb_to_rgb(struct s_cvd_map *ptr_cvd_map, MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3]);
void cvd_model_rgb(struct s_color_space *ptr_color_space, MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3],
    enum cvd_type type);
int cvd_rgb_to_rgb_dalton(struct s_cvd_map *ptr_cvd_map, MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3]);

#ifdef __cplusplus
}
#endif
