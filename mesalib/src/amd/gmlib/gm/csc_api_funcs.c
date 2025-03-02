/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : csc_api_funcs.c
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

#include "csc_api_funcs.h"

void csc_api_set_def(struct s_csc_api_opts *ptr_csc_api_opts)
{
    cs_set_opts_def(&ptr_csc_api_opts->cs_opts_src);
    cs_set_opts_def(&ptr_csc_api_opts->cs_opts_dst);
    ptr_csc_api_opts->en_chad = 0;

    /* 3DLUT */
    ptr_csc_api_opts->en_merge_3dlut = 0;
    ptr_csc_api_opts->num_pnts_3dlut = 17;
    ptr_csc_api_opts->bitwidth_3dlut = 12;
    ptr_csc_api_opts->ptr_3dlut_rgb = 0;
}

int csc_api_gen_map(struct s_csc_api_opts *ptr_csc_api_opts, struct s_csc_map *ptr_csc_map)
{
    cs_init(&ptr_csc_api_opts->cs_opts_src, &ptr_csc_map->color_space_src);
    cs_init(&ptr_csc_api_opts->cs_opts_dst, &ptr_csc_map->color_space_dst);

    ptr_csc_map->en_chad = ptr_csc_api_opts->en_chad;

    return csc_init_map(ptr_csc_map);
}

int csc_api_gen_3dlut(struct s_csc_api_opts *ptr_csc_api_opts, struct s_csc_map *ptr_csc_map)
{
    int index = 0;
    int value_max = (1 << ptr_csc_api_opts->bitwidth_3dlut) - 1;
    int nir, nig, nib;

    if (ptr_csc_api_opts->ptr_3dlut_rgb == 0)
        return -1;    /* something wrong */

    for (nir = 0; nir < ptr_csc_api_opts->num_pnts_3dlut; nir++)
        for (nig = 0; nig < ptr_csc_api_opts->num_pnts_3dlut; nig++)
            for (nib = 0; nib < ptr_csc_api_opts->num_pnts_3dlut; nib++) {
                unsigned short rgb[3];
                MATFLOAT rgb_inp[3], rgb_out[3];

                rgb[0] = ptr_csc_api_opts->en_merge_3dlut ? ptr_csc_api_opts->ptr_3dlut_rgb[index + 0] :
                    (nir * value_max) / (ptr_csc_api_opts->num_pnts_3dlut - 1);
                rgb[1] = ptr_csc_api_opts->en_merge_3dlut ? ptr_csc_api_opts->ptr_3dlut_rgb[index + 1] :
                    (nig * value_max) / (ptr_csc_api_opts->num_pnts_3dlut - 1);
                rgb[2] = ptr_csc_api_opts->en_merge_3dlut ? ptr_csc_api_opts->ptr_3dlut_rgb[index + 2] :
                    (nib * value_max) / (ptr_csc_api_opts->num_pnts_3dlut - 1);

                cs_short2flt_rgb(rgb, rgb_inp, value_max);
                csc_rgb_to_rgb(ptr_csc_map, rgb_inp, rgb_out);
                cs_flt2short_rgb(rgb_out, &ptr_csc_api_opts->ptr_3dlut_rgb[index], value_max);
                index += 3;
            }

    return 0;
}
