/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : csc_funcs.c
 * Purpose    : Color Space Conversion 3DLUT functions
 * Author     : Vladimir Lachine (vlachine@amd.com)
 * Date       : June 09, 2023
 * Version    : 1.2
 *----------------------------------------------------------------------
 *
 */

#ifndef GM_SIM
#pragma code_seg("PAGED3PC")
#pragma data_seg("PAGED3PD")
#pragma const_seg("PAGED3PR")
#endif

#include "csc_funcs.h"

void csc_ctor(struct s_csc_map *ptr_csc_map)
{
    csc_set_def(ptr_csc_map);
}

void csc_dtor(struct s_csc_map *ptr_csc_map)
{
}

void csc_set_def(struct s_csc_map *ptr_csc_map)
{
    ptr_csc_map->en_chad = 0;
    mat_3x3_unity(ptr_csc_map->mat_csc);
}

int csc_init_map(struct s_csc_map *ptr_csc_map)
{
    cs_genmat_rgb_to_rgb(ptr_csc_map->color_space_src.rgbw_xy, ptr_csc_map->color_space_dst.rgbw_xy,
        ptr_csc_map->mat_csc, ptr_csc_map->en_chad);

    return 0;
}

int csc_rgb_to_rgb(struct s_csc_map *ptr_csc_map, MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3])
{
    MATFLOAT rgb_tmp[3];

    cs_nlin_to_lin_rgb(&ptr_csc_map->color_space_src, rgb_inp, rgb_tmp);
    mat_eval_3x3(ptr_csc_map->mat_csc, rgb_tmp, rgb_out);
    cs_clamp_rgb(rgb_out, 0.0, 1.0);
    cs_lin_to_nlin_rgb(&ptr_csc_map->color_space_dst, rgb_out, rgb_out);

    return 0;
}
