/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : csc_api_funcs.h
 * Purpose    : Color Space Conversion 3DLUT functions
 * Author     : Vladimir Lachine (vlachine@amd.com)
 * Date       : June 09, 2023
 * Version    : 1.2
 *----------------------------------------------------------------------
 *
 */

#pragma once

#include "csc_funcs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct s_csc_api_opts { /* csc parameters */
    int    en_chad;    /* enable/disable chromatic adaptation: {0,1}=0 */
    struct s_cs_opts    cs_opts_src;    /* Source color space */
    struct s_cs_opts    cs_opts_dst;    /* Destination color space */
    /* 3DLUT parameters */
    int        en_merge_3dlut;
    int        num_pnts_3dlut;
    int        bitwidth_3dlut;
    unsigned short    *ptr_3dlut_rgb;
};

void csc_api_set_def(struct s_csc_api_opts *ptr_csc_api_opts);

int csc_api_gen_map(struct s_csc_api_opts *ptr_csc_api_opts, struct s_csc_map *ptr_csc_map);
int csc_api_gen_3dlut(struct s_csc_api_opts *ptr_csc_api_opts, struct s_csc_map *ptr_csc_map);

#ifdef __cplusplus
}
#endif
