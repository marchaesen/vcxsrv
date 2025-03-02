/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : gm_api_funcs.h
 * Purpose    : Gamut Mapping API functions
 * Author     : Vladimir Lachine (vlachine@amd.com)
 * Date       : November 12, 2024
 * Version    : 3.1
 *----------------------------------------------------------------------
 *
*/

#pragma once

#include "gm_funcs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct s_gm_opts {
    enum gm_gamut_map_mode    gamut_map_mode;
    /* Gamut Map Mode: 0 - no gamut map, 1 - Tone Map BT2390-4, 2 - TM+CHTO, 3 - TM+CHSO, 4 - TM+CHCI */
    enum gm_hue_rot_mode      hue_rot_mode;
    /* Hue Rotation Mode: 0 - none, 1 - hue rotation, 2 - chroma compression, 3 - hue rotation and chroma compression */
    int                       en_tm_scale_color;
    /* Enable/Disable Color Scaling (valid for Tone Mapping mode only): {0,1} = 1    */
    unsigned int              mode;
    /* mode = 0 : Reserved for modifications of the Gamut Map algo */
    /* CHTO tuning parameters */
    MATFLOAT                  org2_perc_c;
    /* Origin2 percentage gap for chroma [0.7,095] = 0.9 */
    MATFLOAT                  vec_org1_factor[GM_NUM_PRIM];
    /* Factor of Origin1 for M,R,Y,G,C,B [1.0,1.4] = 1.3, 1.3, 1.3, 1.3, 1.2, 1.0 */
    MATFLOAT                  vec_org3_factor[GM_NUM_PRIM];
    /* Factor of Origin3 for M,R,Y,G,C,B [1.01,1,2] = 1.05, 1.2, 1.05, 1.05, 1.01, 1.05 */
    MATFLOAT                  step_samp;
    /* Sampling precision in IC space for edge search [0.00001,0.001]=0.0001 */
    enum gm_map_type          map_type;
    /* Map type: {0,1,2} = 0 : 0 - segments intersection SEG, 1 - radius sampling RAD, 2 hybrid - SEG+RAD */
    int                       num_hue_pnts;
    /* Number of hue grid points: [90,360]=360 */
    int                       num_edge_pnts;
    /* Number of edge IC grid points: [91, 181] = 181 */
    int                       num_int_pnts;
    /* Number of intensity grid points for primary hues: [5,33] = 33 */
    /* show pixel parameters */
    int                       reserve;
    /* Reserved for debugging purpose = 0 */
    enum gm_show_pix_mode     show_pix_mode;
    /* EShowPixMode: [0,8]=0 : show pixel debugging mode */
    MATFLOAT                  show_pix_hue_limits[2];
    /* Show Pixel mode hue ranges */
    /* color space parameters */
    struct s_cs_opts          cs_opts_src;
    struct s_cs_opts          cs_opts_dst;
    int                       update_msk;
    /* Update mask: GM_UPDATE_SRC - update source gamut, GM_UPDATE_DST - update destination gamut */
    /* 3DLUT parameters */
    int                       en_merge_3dlut;
    int                       num_pnts_3dlut;
    int                       bitwidth_3dlut;
    unsigned short            *ptr_3dlut_rgb;
};

int gm_api_gen_map(struct s_gm_opts *ptr_gm_opts, struct s_gamut_map *ptr_gamut_map);
int gm_api_gen_3dlut(struct s_gm_opts *ptr_gm_opts, struct s_gamut_map *ptr_gamut_map);

void gm_api_set_def(struct s_gm_opts *ptr_gm_opts);
void gm_api_init(struct s_gm_opts *ptr_gm_opts, struct s_gamut_map *ptr_gamut_map);

void *gm_api_alloc(unsigned int size_bytes, void* mem_ctx); /* alloc array */
void gm_api_free(void *ptr_mem, void* mem_ctx); /* free array */

#ifdef __cplusplus
}
#endif
