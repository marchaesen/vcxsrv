/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : csc_funcs.h
 * Purpose    : Color Space Conversion 3DLUT functions
 * Author     : Vladimir Lachine (vlachine@amd.com)
 * Date       : June 09, 2023
 * Version    : 1.2
 *----------------------------------------------------------------------
 *
 */

#pragma once

#include "cs_funcs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct s_csc_map {
    int    en_chad;    /* enable/disable chromatic adaptation: {0,1}=0 */
    struct s_color_space    color_space_src;    /* Source color space */
    struct s_color_space    color_space_dst;    /* Destination color space */
    MATFLOAT mat_csc[3][3];    /* color space conversion matrix */
};

/* constructor and destructor */
void csc_ctor(struct s_csc_map *ptr_csc_map);
void csc_dtor(struct s_csc_map *ptr_csc_map);

void csc_set_def(struct s_csc_map *ptr_csc_map);
int csc_init_map(struct s_csc_map *ptr_csc_map);

int csc_rgb_to_rgb(struct s_csc_map *ptr_csc_map, MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3]);

#ifdef __cplusplus
}
#endif
