/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : cvd_api_funcs.c
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

#include "cvd_api_funcs.h"

void cvd_api_set_def(struct s_cvd_api_opts *ptr_api_cvd_opts)
{
    int nk;

    ptr_api_cvd_opts->mode = ECM_NONE;
    for (nk = 0; nk < 3; nk++)
        ptr_api_cvd_opts->gain[nk] = 0.0;

    cs_set_opts_def(&ptr_api_cvd_opts->cs_opts);

    ptr_api_cvd_opts->en_merge_3dlut = 0;
    ptr_api_cvd_opts->num_pnts_3dlut = 17;
    ptr_api_cvd_opts->bitwidth_3dlut = 12;
    ptr_api_cvd_opts->ptr_3dlut_rgb = 0;
}

int cvd_api_gen_map(struct s_cvd_api_opts *ptr_api_cvd_opts, struct s_cvd_map *ptr_cvd_map)
{
    int nk;

    cvd_set_def(ptr_cvd_map);

    ptr_cvd_map->mode = ptr_api_cvd_opts->mode;
    for (nk = 0; nk < 3; nk++)
        ptr_cvd_map->gain[nk] = ptr_api_cvd_opts->gain[nk];

    cs_init(&ptr_api_cvd_opts->cs_opts, &ptr_cvd_map->color_space);

    return 0;
}

int cvd_api_gen_3dlut(struct s_cvd_api_opts *ptr_api_cvd_opts, struct s_cvd_map *ptr_cvd_map)
{
    int index = 0;
    int nir, nig, nib;
    int value_max;

    if (ptr_api_cvd_opts->ptr_3dlut_rgb == 0)
        return -1;    /* something wrong */

    value_max = (1 << ptr_api_cvd_opts->bitwidth_3dlut) - 1;
    for (nir = 0; nir < ptr_api_cvd_opts->num_pnts_3dlut; nir++)
        for (nig = 0; nig < ptr_api_cvd_opts->num_pnts_3dlut; nig++)
            for (nib = 0; nib < ptr_api_cvd_opts->num_pnts_3dlut; nib++) {
                unsigned short rgb[3];
                MATFLOAT rgb_inp[3], rgb_out[3];

                rgb[0] = ptr_api_cvd_opts->en_merge_3dlut ? ptr_api_cvd_opts->ptr_3dlut_rgb[index + 0] :
                    (nir * value_max) / (ptr_api_cvd_opts->num_pnts_3dlut - 1);
                rgb[1] = ptr_api_cvd_opts->en_merge_3dlut ? ptr_api_cvd_opts->ptr_3dlut_rgb[index + 1] :
                    (nig * value_max) / (ptr_api_cvd_opts->num_pnts_3dlut - 1);
                rgb[2] = ptr_api_cvd_opts->en_merge_3dlut ? ptr_api_cvd_opts->ptr_3dlut_rgb[index + 2] :
                    (nib * value_max) / (ptr_api_cvd_opts->num_pnts_3dlut - 1);

                cs_short2flt_rgb(rgb, rgb_inp, value_max);
                cvd_rgb_to_rgb(ptr_cvd_map, rgb_inp, rgb_out);
                cs_flt2short_rgb(rgb_out, &ptr_api_cvd_opts->ptr_3dlut_rgb[index], value_max);

                index += 3;
            }

    return 0;
}
