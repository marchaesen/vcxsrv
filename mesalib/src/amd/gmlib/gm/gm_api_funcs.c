/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : gm_api_funcs.c
 * Purpose    : Gamut Mapping API functions
 * Author     : Vladimir Lachine (vlachine@amd.com)
 * Date       : November 12, 2024
 * Version    : 3.1
 *----------------------------------------------------------------------
 *
 */

#ifndef GM_SIM
#pragma code_seg("PAGED3PC")
#pragma data_seg("PAGED3PD")
#pragma const_seg("PAGED3PR")
#endif

#include "gm_api_funcs.h"

/* non library helper functions */
/*
    // SESSION START
    struct s_gamut_map gamut_map;
    gm_ctor(&gamut_map, gm_api_alloc, gm_api_free);    // constructor - once per session

    struct s_gm_opts gm_opts;
    gm_api_set_def(&gm_opts);                // set default mapping
    gm_api_gen_map(&gm_opts, &gamut_map);    // generate default mapping

    gm_opts.ptr_3dlut_rgb = (unsigned short *)gamut_map.ptr_func_alloc(
    3 * sizeof(unsigned short) * gm_opts.num_pnts_3dlut * gm_opts.num_pnts_3dlut * gm_opts.num_pnts_3dlut);    // allocate 3DLUT memory

    SOURCE OR TARGET GAMUT IS CHANGED EVENT
    {
        // ...................
        // set parameters of src gamut, dst gamut and gamut mapping
        // ...................
        gm_opts.update_msk = GM_UPDATE_SRC;    // GM_UPDATE_SRC -
        update source gamut, GM_UPDATE_DST - update destination gamut or mapping parameters has been changed
        // or
        gm_opts.update_msk = GM_UPDATE_DST;    // GM_UPDATE_SRC - u
        pdate source gamut, GM_UPDATE_DST - update destination gamut or mapping parameters has been changed

        int rc = gm_api_gen_map(&gm_opts, &gamut_map);
        if (rc == 0) {
            rc = gm_api_gen_3dlut(&gm_opts, &gamut_map);        // generate 3DLUT
//            .................
//            load 3DLUT to HW registers
//            .................
        }
    }

    // SESSION END
    gamut_map.ptr_func_free(gm_opts.ptr_3dlut_rgb);    // free 3DLUT memory
    gm_dtor(&gamut_map);        // destructor - once per session
*/

int gm_api_gen_map(struct s_gm_opts *ptr_gm_opts, struct s_gamut_map *ptr_gamut_map)
{
    int rc;

    /* initialize gamut mapping staructure from api gamut options */
    if (ptr_gm_opts->update_msk & GM_UPDATE_DST)
        gm_api_init(ptr_gm_opts, ptr_gamut_map);

    /* init src and dst gamuts */
    rc = gm_init_gamuts(ptr_gamut_map, &ptr_gm_opts->cs_opts_src, &ptr_gm_opts->cs_opts_dst,
        ptr_gm_opts->mode, ptr_gm_opts->update_msk);

    /* generate gamut edge and other internal data */
    if (rc == 0)
        gm_gen_map(ptr_gamut_map, ptr_gm_opts->update_msk);

    ptr_gm_opts->update_msk = 0;

    return rc;
}

int gm_api_gen_3dlut(struct s_gm_opts *ptr_gm_opts, struct s_gamut_map *ptr_gamut_map)
{
    if (ptr_gm_opts->ptr_3dlut_rgb) {
        gm_gen_3dlut(ptr_gamut_map, ptr_gm_opts->num_pnts_3dlut,
                ptr_gm_opts->bitwidth_3dlut, ptr_gm_opts->en_merge_3dlut, ptr_gm_opts->ptr_3dlut_rgb);
        return 0;
    }
    return -1; /* something wrong */
}

void gm_api_set_def(struct s_gm_opts *ptr_gm_opts)
{
    int nk;

    ptr_gm_opts->gamut_map_mode = EGMM_NONE;
    ptr_gm_opts->en_tm_scale_color = 1;
    ptr_gm_opts->hue_rot_mode = EHRM_NONE;
    ptr_gm_opts->mode = 0;
    ptr_gm_opts->step_samp = 0.0005;
    ptr_gm_opts->map_type = EMT_SEG;
    ptr_gm_opts->num_hue_pnts = 180;
    ptr_gm_opts->num_edge_pnts = 121;
    ptr_gm_opts->num_int_pnts = 33;
    ptr_gm_opts->org2_perc_c = GM_ORG2_PERC;

    for (nk = 0; nk < GM_NUM_PRIM; nk++) {
        ptr_gm_opts->vec_org1_factor[nk] = gm_vec_org13_factor_def[nk][0];
        ptr_gm_opts->vec_org3_factor[nk] = gm_vec_org13_factor_def[nk][1];
    }

    ptr_gm_opts->reserve = 0;
    ptr_gm_opts->show_pix_mode = ESPM_NONE;

    for (nk = 0; nk < 2; nk++)
        ptr_gm_opts->show_pix_hue_limits[nk] = 0.0;

    cs_set_opts_def(&ptr_gm_opts->cs_opts_src);
    cs_set_opts_def(&ptr_gm_opts->cs_opts_dst);

    ptr_gm_opts->update_msk = GM_UPDATE_SRC | GM_UPDATE_DST;

    ptr_gm_opts->en_merge_3dlut = 0;
    ptr_gm_opts->num_pnts_3dlut = 17;
    ptr_gm_opts->bitwidth_3dlut = 12;
}

void gm_api_init(struct s_gm_opts *ptr_gm_opts, struct s_gamut_map *ptr_gamut_map)
{
    int nk;

    gm_set_def(ptr_gamut_map);

    ptr_gamut_map->gamut_map_mode = ptr_gm_opts->gamut_map_mode;
    ptr_gamut_map->en_tm_scale_color = ptr_gm_opts->en_tm_scale_color;
    ptr_gamut_map->hue_rot_mode = ptr_gm_opts->hue_rot_mode;
    ptr_gamut_map->mode = ptr_gm_opts->mode;
    ptr_gamut_map->org2_perc_c = ptr_gm_opts->org2_perc_c;

    for (nk = 0; nk < GM_NUM_PRIM; nk++) {
        /* Factor of Origin1 for M,R,Y,G,C,B = 1.3, 1.3, 1.3, 1.3, 1.2, 1.0 */
        ptr_gamut_map->vec_org1_factor[nk] = ptr_gm_opts->vec_org1_factor[nk];
        /* Factor of Origin3 for M,R,Y,G,C,B = 1.05, 1.1, 1.1, 1.05, 1.01, 1.06 */
        ptr_gamut_map->vec_org3_factor[nk] = ptr_gm_opts->vec_org3_factor[nk];
    }

    ptr_gamut_map->step_samp = ptr_gm_opts->step_samp;            /* default is 0.0005 */
    ptr_gamut_map->map_type = ptr_gm_opts->map_type;            /* default is EMT_SEG */
    ptr_gamut_map->num_hue_pnts = ptr_gm_opts->num_hue_pnts;    /* default is 181 */
    ptr_gamut_map->num_edge_pnts = ptr_gm_opts->num_edge_pnts;  /* default is 121 */
    ptr_gamut_map->num_int_pnts = ptr_gm_opts->num_int_pnts;    /* default is 33 */

    ptr_gamut_map->reserve = ptr_gm_opts->reserve;
    ptr_gamut_map->show_pix_mode = ptr_gm_opts->show_pix_mode;

    for (nk = 0; nk < 2; nk++)
        ptr_gamut_map->show_pix_hue_limits[nk] = ptr_gm_opts->show_pix_hue_limits[nk];
}

#ifndef GM_SIM
#ifndef LINUX_DM
#include "dm_services.h"
#else
/* TBD: include for LINUX_DM */
#endif /* LINUX_DM */
#else
#include <stdlib.h>
#endif /* GM_SIM */

void *gm_api_alloc(unsigned int size_bytes, void* mem_ctx)
{
#ifndef GM_SIM
#ifndef LINUX_DM
    return dm_alloc(size_bytes);
#else
    /* TBD: alloc() for LINUX_DM */
#endif /* LINUX_DM */
#else
    return malloc(size_bytes);
#endif /* GM_SIM */
}

void gm_api_free(void *ptr_mem, void* mem_ctx)
{
#ifndef GM_SIM
#ifndef LINUX_DM
    dm_free(ptr_mem);
#else
    /* TBD: free() for LINUX_DM */
#endif /* LINUX_DM */
#else
    free(ptr_mem);
#endif /* GM_SIM */
}
