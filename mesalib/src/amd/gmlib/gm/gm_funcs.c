/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : gm_funcs.c
 * Purpose    : Gamut Mapping functions
 * Author     : Vladimir Lachine (vlachine@amd.com)
 * Date       : November 12, 2024
 * Version    : 3.1
 *---------------------------------------------------------------------
 *
 */

#ifndef GM_SIM
#pragma code_seg("PAGED3PC")
#pragma data_seg("PAGED3PD")
#pragma const_seg("PAGED3PR")
#endif

#include "gm_funcs.h"

static float gm_lin2pq[GM_PQTAB_NUMPNTS];
static float gm_pq2lin[GM_PQTAB_NUMPNTS];

void gm_ctor(struct s_gamut_map *ptr_gamut_map, void*(*ptr_func_alloc)(unsigned int, void*), void(*ptr_func_free)(void *, void*), void* mem_context)
{
    ptr_gamut_map->ptr_func_alloc = ptr_func_alloc;
    ptr_gamut_map->ptr_func_free = ptr_func_free;
    ptr_gamut_map->memory_context = mem_context;
    ptr_gamut_map->ptr_edge_ic = 0;
    ptr_gamut_map->ptr_hr_src_hc = 0;
    ptr_gamut_map->ptr_hr_dst_hc = 0;
    ptr_gamut_map->ptr_org2_ic = 0;
    ptr_gamut_map->ptr_org3_ic = 0;
    ptr_gamut_map->ptr_cusp_src_ic = 0;
    ptr_gamut_map->ptr_cusp_dst_ic = 0;

    gm_gen_pq_lut(gm_lin2pq, GM_PQTAB_NUMPNTS, EGD_LIN_2_NONLIN);
    gm_gen_pq_lut(gm_pq2lin, GM_PQTAB_NUMPNTS, EGD_NONLIN_2_LIN);
    gm_set_def(ptr_gamut_map);
}

void gm_dtor(struct s_gamut_map *ptr_gamut_map)
{
    gm_free_mem(ptr_gamut_map);

    ptr_gamut_map->ptr_func_alloc = 0;
    ptr_gamut_map->ptr_func_free = 0;
}

void gm_alloc_mem(struct s_gamut_map *ptr_gamut_map)
{
    if (ptr_gamut_map->gamut_map_mode > EGMM_TM) {
        if (ptr_gamut_map->map_type != EMT_RAD)
            if (ptr_gamut_map->ptr_edge_ic == 0)
                ptr_gamut_map->ptr_edge_ic = (MATFLOAT *)ptr_gamut_map->ptr_func_alloc(
                    ptr_gamut_map->num_hue_pnts * ptr_gamut_map->num_edge_pnts * 2 * sizeof(MATFLOAT), 
                    ptr_gamut_map->memory_context);

        if (ptr_gamut_map->ptr_org2_ic == 0)
            ptr_gamut_map->ptr_org2_ic = (MATFLOAT *)ptr_gamut_map->ptr_func_alloc(
                ptr_gamut_map->num_hue_pnts * 2 * sizeof(MATFLOAT),
                ptr_gamut_map->memory_context);

        if (ptr_gamut_map->ptr_org3_ic == 0)
            ptr_gamut_map->ptr_org3_ic = (MATFLOAT *)ptr_gamut_map->ptr_func_alloc(
                ptr_gamut_map->num_hue_pnts * 2 * sizeof(MATFLOAT),
                ptr_gamut_map->memory_context);
    }

    if (ptr_gamut_map->hue_rot_mode != EHRM_NONE) {
        if (ptr_gamut_map->ptr_hr_src_hc == 0)
            ptr_gamut_map->ptr_hr_src_hc = (MATFLOAT *)ptr_gamut_map->ptr_func_alloc(
                GM_NUM_PRIM * ptr_gamut_map->num_int_pnts * 2 * sizeof(MATFLOAT),
                ptr_gamut_map->memory_context);

        if (ptr_gamut_map->ptr_hr_dst_hc == 0)
            ptr_gamut_map->ptr_hr_dst_hc = (MATFLOAT *)ptr_gamut_map->ptr_func_alloc(
                GM_NUM_PRIM * ptr_gamut_map->num_int_pnts * 2 * sizeof(MATFLOAT),
                ptr_gamut_map->memory_context);

    }

    if (ptr_gamut_map->ptr_cusp_src_ic == 0)
        ptr_gamut_map->ptr_cusp_src_ic = (MATFLOAT *)ptr_gamut_map->ptr_func_alloc(
            ptr_gamut_map->num_hue_pnts * 2 * sizeof(MATFLOAT),
            ptr_gamut_map->memory_context);

    if (ptr_gamut_map->ptr_cusp_dst_ic == 0)
        ptr_gamut_map->ptr_cusp_dst_ic = (MATFLOAT *)ptr_gamut_map->ptr_func_alloc(
            ptr_gamut_map->num_hue_pnts * 2 * sizeof(MATFLOAT),
            ptr_gamut_map->memory_context);
}

void gm_free_mem(struct s_gamut_map *ptr_gamut_map)
{
    if (ptr_gamut_map->ptr_edge_ic) {
        ptr_gamut_map->ptr_func_free(ptr_gamut_map->ptr_edge_ic, ptr_gamut_map->memory_context);
        ptr_gamut_map->ptr_edge_ic = 0;
    }

    if (ptr_gamut_map->ptr_hr_src_hc) {
        ptr_gamut_map->ptr_func_free(ptr_gamut_map->ptr_hr_src_hc, ptr_gamut_map->memory_context);
        ptr_gamut_map->ptr_hr_src_hc = 0;
    }

    if (ptr_gamut_map->ptr_hr_dst_hc) {
        ptr_gamut_map->ptr_func_free(ptr_gamut_map->ptr_hr_dst_hc, ptr_gamut_map->memory_context);
        ptr_gamut_map->ptr_hr_dst_hc = 0;
    }

    if (ptr_gamut_map->ptr_org2_ic) {
        ptr_gamut_map->ptr_func_free(ptr_gamut_map->ptr_org2_ic, ptr_gamut_map->memory_context);
        ptr_gamut_map->ptr_org2_ic = 0;
    }

    if (ptr_gamut_map->ptr_org3_ic) {
        ptr_gamut_map->ptr_func_free(ptr_gamut_map->ptr_org3_ic, ptr_gamut_map->memory_context);
        ptr_gamut_map->ptr_org3_ic = 0;
    }

    if (ptr_gamut_map->ptr_cusp_src_ic) {
        ptr_gamut_map->ptr_func_free(ptr_gamut_map->ptr_cusp_src_ic, ptr_gamut_map->memory_context);
        ptr_gamut_map->ptr_cusp_src_ic = 0;
    }

    if (ptr_gamut_map->ptr_cusp_dst_ic) {
        ptr_gamut_map->ptr_func_free(ptr_gamut_map->ptr_cusp_dst_ic, ptr_gamut_map->memory_context);
        ptr_gamut_map->ptr_cusp_dst_ic = 0;
    }
}

void gm_set_def(struct s_gamut_map *ptr_gamut_map)
{
    int nk;

    ptr_gamut_map->gamut_map_mode = EGMM_NONE;
    ptr_gamut_map->en_tm_scale_color = 1;
    ptr_gamut_map->hue_rot_mode = EHRM_NONE;
    ptr_gamut_map->mode = 0;
    ptr_gamut_map->num_hue_pnts = GM_NUM_HUE;
    ptr_gamut_map->num_edge_pnts = GM_NUM_EDGE;
    ptr_gamut_map->num_int_pnts = GM_NUM_INT;
    ptr_gamut_map->step_samp = GM_STEP_SAMP;
    ptr_gamut_map->edge_type = EET_RAD;
    ptr_gamut_map->map_type = EMT_SEG;
    ptr_gamut_map->org2_perc_c = GM_ORG2_PERC;
    for (nk = 0; nk < GM_NUM_PRIM; nk++) {
        ptr_gamut_map->vec_org1_factor[nk] = gm_vec_org13_factor_def[nk][0];
        ptr_gamut_map->vec_org3_factor[nk] = gm_vec_org13_factor_def[nk][1];
    }
    ptr_gamut_map->reserve = 0;
    ptr_gamut_map->show_pix_mode = ESPM_NONE;
    for (nk = 0; nk < 2; nk++)
        ptr_gamut_map->show_pix_hue_limits[nk] = 0.0;
}

int gm_init_gamuts(struct s_gamut_map *ptr_gamut_map, struct s_cs_opts *ptr_cs_opts_src,
    struct s_cs_opts *ptr_cs_opts_dst, unsigned int gm_mode, int update_msk)
{
    if (update_msk & GM_UPDATE_SRC) { /* init and generate prim and cusp points for source gamut */
        cs_init(ptr_cs_opts_src, &ptr_gamut_map->color_space_src);
        cs_genprim_itp(&ptr_gamut_map->color_space_src, GM_NUM_PRIM, (MATFLOAT *)gm_vec_cusp_rgb,
            ptr_gamut_map->vec_prim_src_ich);
    }

    if (update_msk & GM_UPDATE_DST) { /* init and generate prim and cusp points for target gamut */
        cs_init(ptr_cs_opts_dst, &ptr_gamut_map->color_space_dst);
        cs_genprim_itp(&ptr_gamut_map->color_space_dst, GM_NUM_PRIM, (MATFLOAT *)gm_vec_cusp_rgb,
            ptr_gamut_map->vec_prim_dst_ich);
    }

    /* calculate Luma Min/Max for Tone Mapping */
    if ((update_msk & GM_UPDATE_SRC) || (update_msk & GM_UPDATE_DST)) {
        MATFLOAT luma_rng_src = ptr_gamut_map->color_space_src.luma_limits[1] -
            ptr_gamut_map->color_space_src.luma_limits[0];
        ptr_gamut_map->lum_min = (ptr_gamut_map->color_space_dst.luma_limits[0] -
            ptr_gamut_map->color_space_src.luma_limits[0]) / luma_rng_src;
        ptr_gamut_map->lum_max = (ptr_gamut_map->color_space_dst.luma_limits[1] -
            ptr_gamut_map->color_space_src.luma_limits[0]) / luma_rng_src;
    }

    if (update_msk & GM_UPDATE_DST) {
        gm_free_mem(ptr_gamut_map);
        gm_alloc_mem(ptr_gamut_map);
    }

    if (ptr_gamut_map->hue_rot_mode != EHRM_NONE) {    /* generate prim for intensity points */
        /* memory for src cusp points is reallocated if GM_UPDATE_DST */
        if ((update_msk & GM_UPDATE_SRC) || (update_msk & GM_UPDATE_DST))
            gm_genprim_hc(&ptr_gamut_map->color_space_src, ptr_gamut_map->ptr_hr_src_hc,
                ptr_gamut_map->num_int_pnts, ptr_gamut_map->color_space_dst.luma_limits,
                ptr_gamut_map->lum_min, ptr_gamut_map->lum_max);
        if (update_msk & GM_UPDATE_DST)
            gm_genprim_hc(&ptr_gamut_map->color_space_dst, ptr_gamut_map->ptr_hr_dst_hc,
                ptr_gamut_map->num_int_pnts, ptr_gamut_map->color_space_dst.luma_limits, 0.0, 1.0); /* no TM */
    }

    /* memory for src cusp points is reallocated if GM_UPDATE_DST */
    if ((update_msk & GM_UPDATE_SRC) || (update_msk & GM_UPDATE_DST))
        gm_gencusp_ic(ptr_gamut_map, 0); /* generate cusp points for source gamut */

    if (update_msk & GM_UPDATE_DST)
        gm_gencusp_ic(ptr_gamut_map, 1); /* generate cusp points for target gamut */

    ptr_gamut_map->mode = gm_mode;
    ptr_gamut_map->hue_max = 2.0 * mat_get_pi() * (1.0 - 1.0 / (MATFLOAT)ptr_gamut_map->num_hue_pnts);
    ptr_gamut_map->org1 = mat_denorm(GM_ORG1_FACTOR, ptr_gamut_map->color_space_dst.luma_limits[0],
        ptr_gamut_map->color_space_dst.luma_limits[2]);
    ptr_gamut_map->org3 = mat_denorm(GM_ORG3_FACTOR, ptr_gamut_map->color_space_dst.luma_limits[0],
        ptr_gamut_map->color_space_dst.luma_limits[2]);

    return 0;
}

int gm_check_gamut(struct s_gamut_map *ptr_gamut_map)
{
    struct s_color_space* ptr_cs_src = &ptr_gamut_map->color_space_src;
    struct s_color_space* ptr_cs_dst = &ptr_gamut_map->color_space_dst;

    if (ptr_gamut_map->gamut_map_mode != EGMM_NONE)
        if ((ptr_cs_src->luminance_limits[0] > ptr_cs_dst->luminance_limits[0]) ||
            (ptr_cs_src->luminance_limits[1] < ptr_cs_dst->luminance_limits[1])) {
            ptr_gamut_map->gamut_map_mode = EGMM_NONE;
            ptr_gamut_map->hue_rot_mode = EHRM_NONE;
            return -1;    /* non valid luminance limits */
        }

    return 0; /* valid parameters */
}

void gm_gencusp_ic(struct s_gamut_map *ptr_gamut_map, int color_space)
{
    struct s_color_space *ptr_color_space = color_space ? &ptr_gamut_map->color_space_dst : &ptr_gamut_map->color_space_src;
    MATFLOAT *ptr_cusp_ic = color_space ? ptr_gamut_map->ptr_cusp_dst_ic : ptr_gamut_map->ptr_cusp_src_ic;
    int num_phases = ptr_gamut_map->num_hue_pnts / GM_NUM_PRIM;
    int index = 0;
    MATFLOAT *ptr_hue = (MATFLOAT *)ptr_gamut_map->ptr_func_alloc(ptr_gamut_map->num_hue_pnts * sizeof(MATFLOAT), 
        ptr_gamut_map->memory_context);
    MATFLOAT *ptr_ic = (MATFLOAT *)ptr_gamut_map->ptr_func_alloc(ptr_gamut_map->num_hue_pnts * 2 * sizeof(MATFLOAT),
        ptr_gamut_map->memory_context);
    MATFLOAT rgb[3], itp[3];
    int np, ni, nc;

    for (np = 0; np < GM_NUM_PRIM; np++) {
        for (ni = 0; ni < num_phases; ni++) {
            MATFLOAT phase = (MATFLOAT)ni / (MATFLOAT)num_phases;

            int ind0 = np;
            int ind1 = (ind0 + 1) % GM_NUM_PRIM;
            for (nc = 0; nc < 3; nc++) {
                MATFLOAT val0 = gm_vec_cusp_rgb[ind0][nc];
                MATFLOAT val1 = gm_vec_cusp_rgb[ind1][nc];

                rgb[nc] = val0 + (val1 - val0) * phase;
            }
            cs_gamma_rgb(rgb, rgb, ptr_color_space->gamma_parm, EGD_NONLIN_2_LIN);    /* TBD */
            cs_denorm_rgb(rgb, ptr_color_space->luminance_limits[0], ptr_color_space->luminance_limits[2]);
            cs_clamp_rgb(rgb, ptr_color_space->luminance_limits[0], ptr_color_space->luminance_limits[1]);
            cs_rgb_to_itp(ptr_color_space, rgb, itp);

            if (color_space == 0) { /* tm and hr for source gamut */
                if (ptr_gamut_map->gamut_map_mode != EGMM_NONE) {
                    if ((ptr_gamut_map->lum_min > 0.0) || (ptr_gamut_map->lum_max < 1.0))
                        itp[0] = gm_tm_luma(itp[0], ptr_gamut_map->color_space_src.luma_limits,
                                ptr_gamut_map->lum_min, ptr_gamut_map->lum_max);
                    if (ptr_gamut_map->hue_rot_mode != EHRM_NONE)
                        gm_hr_itp(ptr_gamut_map, itp, itp, 0);
                }
            }

            ptr_ic[2 * index + 0] = itp[0];
            ptr_ic[2 * index + 1] = mat_radius(itp[2], itp[1]);
            ptr_hue[index] = mat_angle(itp[2], itp[1]);
            index++;
        }
    }

    gm_resample_hue_ic(ptr_hue, ptr_ic, ptr_cusp_ic, ptr_gamut_map->num_hue_pnts, ptr_gamut_map->num_hue_pnts);

    ptr_gamut_map->ptr_func_free(ptr_ic, ptr_gamut_map->memory_context);
    ptr_gamut_map->ptr_func_free(ptr_hue, ptr_gamut_map->memory_context);
}

void gm_gen_edge_hue(struct s_gamut_map *ptr_gamut_map, int hue_ind)
{
    MATFLOAT fHue = mat_index_to_flt(hue_ind, ptr_gamut_map->hue_max, ptr_gamut_map->num_hue_pnts);

    gm_genedge(&ptr_gamut_map->color_space_dst, ptr_gamut_map->color_space_dst.luma_limits,
        ptr_gamut_map->num_edge_pnts, ptr_gamut_map->edge_type, ptr_gamut_map->step_samp, fHue,
        &ptr_gamut_map->ptr_edge_ic[hue_ind * ptr_gamut_map->num_edge_pnts * 2], 
        ptr_gamut_map->mode & GM_PQTAB_GBD);

    /* correct edge for target cusp point - optional */
    if (ptr_gamut_map->mode & GM_CUSP_ADJUST)
        gm_edgecusp_adjust(&ptr_gamut_map->ptr_edge_ic[hue_ind * ptr_gamut_map->num_edge_pnts * 2],
            ptr_gamut_map->num_edge_pnts, &ptr_gamut_map->ptr_cusp_dst_ic[hue_ind * 2]);
}

/* resample to uniform hue */
void gm_resample_hue_ic(MATFLOAT *ptr_hue, MATFLOAT *ptr_ic_inp, MATFLOAT *ptr_ic_out, int num_hue_pnts_inp, int num_hue_pnts_out)
{
    const MATFLOAT gm_2pi = 2.0 * mat_get_pi();
    int index_2pi = mat_get_hue_index_2pi(ptr_hue, num_hue_pnts_inp);
    int ind1 = index_2pi;
    int ind0 = (ind1 > 0) ? ind1 - 1 : num_hue_pnts_inp - 1;
    MATFLOAT tar_inc_out = gm_2pi / (MATFLOAT)num_hue_pnts_out;
    MATFLOAT tar_acc_out = 0.0;
    MATFLOAT tar_inc_inp = ptr_hue[ind1] - ptr_hue[ind0];
    int ni;

    if (tar_inc_inp < 0.0)
        tar_inc_inp += gm_2pi;

    for (ni = 0; ni < num_hue_pnts_out; ni++) {
        MATFLOAT hue = ptr_hue[ind1];
        MATFLOAT delta_src, phs_src;

        if ((ind1 == index_2pi) && (ni > num_hue_pnts_out / 2))
            hue += gm_2pi;

        while (tar_acc_out >= hue) {
            ind0 = (ind0 + 1) % num_hue_pnts_inp;
            ind1 = (ind1 + 1) % num_hue_pnts_inp;
            hue = ptr_hue[ind1];
            if ((ind1 == index_2pi) && (ni > num_hue_pnts_out / 2)) {
                hue += gm_2pi;
            }
            tar_inc_inp = ptr_hue[ind1] - ptr_hue[ind0];

            if (tar_inc_inp < 0.0)
                tar_inc_inp += gm_2pi;
        }
        delta_src = tar_acc_out - ptr_hue[ind0];
        if (delta_src < 0.0)
            delta_src += gm_2pi;
        phs_src = delta_src / tar_inc_inp;

        ptr_ic_out[2 * ni + 0] = ptr_ic_inp[2 * ind0 + 0] + (ptr_ic_inp[2 * ind1 + 0] - ptr_ic_inp[2 * ind0 + 0]) * phs_src;
        ptr_ic_out[2 * ni + 1] = ptr_ic_inp[2 * ind0 + 1] + (ptr_ic_inp[2 * ind1 + 1] - ptr_ic_inp[2 * ind0 + 1]) * phs_src;

        tar_acc_out += tar_inc_out;
    }
}

/* calculate hue for primary colors for normilized uniform intensity */
void gm_genprim_hc(struct s_color_space *ptr_color_space, MATFLOAT *ptr_hr_hc, int num_int_pnts,
    MATFLOAT luma_limits[3], MATFLOAT lum_min, MATFLOAT lum_max)
{
    MATFLOAT step = 1.0 / (MATFLOAT)(num_int_pnts - 1);
    MATFLOAT vec_prim_ich[GM_NUM_INT][3];
    MATFLOAT prim_rgb[3], rgb[3], itp_src[3];
    int nk, ni, nc;

    for (nk = 0; nk < GM_NUM_PRIM; nk++) {
        mat_copy((MATFLOAT *)gm_vec_cusp_rgb[nk], prim_rgb, 3);
        for (ni = 0; ni < num_int_pnts; ni++) {
            for (nc = 0; nc < 3; nc++)
                rgb[nc] = prim_rgb[nc] * (MATFLOAT)ni * step;
            /* generate gamut prim points */
            cs_gamma_rgb(rgb, rgb, ptr_color_space->gamma_parm, EGD_NONLIN_2_LIN);
            cs_denorm_rgb(rgb, ptr_color_space->luminance_limits[0], ptr_color_space->luminance_limits[2]);
            cs_clamp_rgb(rgb, ptr_color_space->luminance_limits[0], ptr_color_space->luminance_limits[1]);
            cs_rgb_to_itp(ptr_color_space, rgb, itp_src);
            if ((lum_min > 0.0) || (lum_max < 1.0))
                itp_src[0] = gm_tm_luma(itp_src[0], ptr_color_space->luma_limits, lum_min, lum_max);
            cs_itp_to_ich(itp_src, vec_prim_ich[ni]);
            vec_prim_ich[ni][0] = mat_norm(vec_prim_ich[ni][0], luma_limits[0], luma_limits[2]);
            /* normilize to [0.0,1.0] from target luma limits */
            vec_prim_ich[ni][0] = MAT_CLAMP(vec_prim_ich[ni][0], 0.0, 1.0);
        }
        /* update Intensity=0.0 point */
        vec_prim_ich[0][0] = 0.0;
        vec_prim_ich[0][1] = 0.0;
        vec_prim_ich[0][2] = vec_prim_ich[1][2];
        /* update Intensity=1.0 point */
        vec_prim_ich[num_int_pnts - 1][0] = 1.0;
        vec_prim_ich[num_int_pnts - 1][1] = 0.0;
        vec_prim_ich[num_int_pnts - 1][2] = vec_prim_ich[num_int_pnts - 2][2];
        /* resample to uniform intensity */
        gm_resample_hc(vec_prim_ich, &ptr_hr_hc[nk * num_int_pnts * 2], num_int_pnts, num_int_pnts);
    }
}

/* calculate origin1 and origin1 factor */
void gm_genorg13_factor(struct s_gamut_map* ptr_gamut_map, MATFLOAT* ptr_org13_factor)
{
    MATFLOAT vec_org13_factor_prim[GM_NUM_PRIM * 2];
    int ni;

    for (ni = 0; ni < GM_NUM_PRIM; ni++) {
        vec_org13_factor_prim[2 * ni + 0] = ptr_gamut_map->vec_org1_factor[ni];
        vec_org13_factor_prim[2 * ni + 1] = ptr_gamut_map->vec_org3_factor[ni];
    }
    gm_resample_hue_ic(&ptr_gamut_map->vec_prim_dst_ich[2 * GM_NUM_PRIM], vec_org13_factor_prim,
        ptr_org13_factor, GM_NUM_PRIM, ptr_gamut_map->num_hue_pnts);
}

void gm_genorigin23_hue(struct s_gamut_map* ptr_gamut_map, MATFLOAT* ptr_org13_factor, int hue_ind)
{
    MATFLOAT hue = mat_index_to_flt(hue_ind, ptr_gamut_map->hue_max, ptr_gamut_map->num_hue_pnts);
    MATFLOAT cusp_ich_src[3], cusp_ich_dst[3];
    MATFLOAT org_13[2];

    cusp_ich_src[0] = ptr_gamut_map->ptr_cusp_src_ic[2 * hue_ind + 0];
    cusp_ich_src[1] = ptr_gamut_map->ptr_cusp_src_ic[2 * hue_ind + 1];
    cusp_ich_src[2] = hue;

    cusp_ich_dst[0] = ptr_gamut_map->ptr_cusp_dst_ic[2 * hue_ind + 0];
    cusp_ich_dst[1] = ptr_gamut_map->ptr_cusp_dst_ic[2 * hue_ind + 1];
    cusp_ich_dst[2] = hue;

    /* get Org1 */
    org_13[0] = (ptr_org13_factor[2 * hue_ind + 0] >= 1.0) ?
        ptr_gamut_map->org1 * ptr_org13_factor[2 * hue_ind + 0] :
        ptr_gamut_map->org1 + (cusp_ich_dst[0] - ptr_gamut_map->org1) * ptr_org13_factor[2 * hue_ind + 0];
    org_13[0] = MAT_CLAMP(org_13[0], ptr_gamut_map->org1, cusp_ich_dst[0]);
    /* get Org3 */
    org_13[1] = ptr_gamut_map->org3 * ptr_org13_factor[2 * hue_ind + 1];
    /* calculate Origin2 and Origin3 */
    gm_getorigin23(&ptr_gamut_map->color_space_src, &ptr_gamut_map->color_space_dst, hue, org_13, ptr_gamut_map->org2_perc_c,
        cusp_ich_src, cusp_ich_dst, &ptr_gamut_map->ptr_org2_ic[2 * hue_ind], &ptr_gamut_map->ptr_org3_ic[2 * hue_ind],
        ptr_gamut_map->mode & GM_PQTAB_GBD);
}

void gm_getorigin23(struct s_color_space *ptr_color_space_src, struct s_color_space *ptr_color_space_dst,
    MATFLOAT hue, MATFLOAT org_13_factor[2], MATFLOAT org2_perc_c,
    MATFLOAT cusp_ic_src[2], MATFLOAT cusp_ic_dst[2],
    MATFLOAT origin2_ic[2], MATFLOAT origin3_ic[2], int en_pq_lut)
{

    if ((cusp_ic_src[0] <= cusp_ic_dst[0]) || (cusp_ic_src[1] <= cusp_ic_dst[1])) {
        origin2_ic[0] = org_13_factor[0];
        origin2_ic[1] = 0.0;
        origin3_ic[0] = org_13_factor[1];
        origin3_ic[1] = (origin3_ic[0] - origin2_ic[0]) * cusp_ic_dst[1] / (cusp_ic_dst[0] - origin2_ic[0]);
        return;
    }

    MATFLOAT slope = (cusp_ic_src[0] - cusp_ic_dst[0]) / (cusp_ic_src[1] - cusp_ic_dst[1]);
    MATFLOAT offset = cusp_ic_dst[0] - slope * cusp_ic_dst[1];

    /* get Origin2 point */
    origin2_ic[0] = org_13_factor[0];
    origin2_ic[1] = (origin2_ic[0] - offset) / slope;
    if (origin2_ic[1] < 0.0) {
        origin2_ic[0] = origin2_ic[0] - origin2_ic[1] * slope;
        origin2_ic[1] = 0.0;
    } else {
        MATFLOAT ic_tmp[2];
        MATFLOAT ic_dst[2] = { origin2_ic[0], origin2_ic[1] };
        MATFLOAT ic_src[2] = { origin2_ic[0], origin2_ic[1] };
        MATFLOAT inc_ic[2] = { 0.0, GM_STEP_SAMP * 10.0 };
        MATFLOAT hue_sin_cos[2] = { mat_sin(hue), mat_cos(hue) };

        gm_sample_edge_ic(ptr_color_space_dst, hue_sin_cos, inc_ic, ic_dst, en_pq_lut);
        gm_sample_edge_ic(ptr_color_space_src, hue_sin_cos, inc_ic, ic_src, en_pq_lut);
        if (ic_src[1] < ic_dst[1]) {
            ic_tmp[0] = ic_src[0];
            ic_tmp[1] = ic_src[1];
        } else {
            ic_tmp[0] = ic_dst[0];
            ic_tmp[1] = ic_dst[1];
        }
        if (origin2_ic[1] > org2_perc_c * ic_tmp[1]) {
            origin2_ic[1] = org2_perc_c * ic_tmp[1];
            slope = (cusp_ic_src[0] - origin2_ic[0]) / (cusp_ic_src[1] - origin2_ic[1]);
            offset = origin2_ic[0] - slope * origin2_ic[1];
        }
    }
    /* get Origin3 point */
    origin3_ic[0] = org_13_factor[1];
    origin3_ic[1] = (origin3_ic[0] - offset) / slope;
}

/* resmapling for uniform normilized Intensity in a range [0.0,1.0] */
void gm_resample_hc(MATFLOAT vec_ich_inp[][3], MATFLOAT *ptr_hc_out, int num_int_pnts_inp, int num_int_pnts_out)
{
    MATFLOAT tar_inc_out = 1.0 / (MATFLOAT)(num_int_pnts_out - 1);
    MATFLOAT tar_inc_inp = vec_ich_inp[1][0] - vec_ich_inp[0][0];
    MATFLOAT tar_acc_out = 0.0;
    MATFLOAT phs_inp;
    int ind0 = 0;
    int ind1 = 1;
    int ni;

    for (ni = 0; ni < num_int_pnts_out; ni++) {
        while ((tar_acc_out >= vec_ich_inp[ind1][0]) && (ind1 > ind0)) {
            ind0 = MAT_MIN(ind0 + 1, num_int_pnts_inp - 1);
            ind1 = MAT_MIN(ind1 + 1, num_int_pnts_inp - 1);
            tar_inc_inp = vec_ich_inp[ind1][0] - vec_ich_inp[ind0][0];
        }
        phs_inp = (tar_inc_inp == 0.0) ? 0.0 : (tar_acc_out - vec_ich_inp[ind0][0]) / tar_inc_inp;
        ptr_hc_out[ni * 2 + 0] = vec_ich_inp[ind0][2] + (vec_ich_inp[ind1][2] - vec_ich_inp[ind0][2]) * phs_inp;
        ptr_hc_out[ni * 2 + 1] = vec_ich_inp[ind0][1] + (vec_ich_inp[ind1][1] - vec_ich_inp[ind0][1]) * phs_inp;
        tar_acc_out += tar_inc_out;
    }
}

int gm_rgb_to_rgb(struct s_gamut_map* ptr_gamut_map, MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3])
{    /* rgb_inp - linear space, linear space */
    MATFLOAT itp_inp[3], itp_out[3];
    int zone = 0;

    if (ptr_gamut_map->gamut_map_mode != EGMM_NONE) {
        gm_rgb_to_itp(&ptr_gamut_map->color_space_src, rgb_inp, itp_inp, ptr_gamut_map->mode & GM_PQTAB_3DLUT);
        zone = gm_map_itp(ptr_gamut_map, itp_inp, itp_out);
        gm_itp_to_rgb(&ptr_gamut_map->color_space_dst, itp_out, rgb_out, ptr_gamut_map->mode & GM_PQTAB_3DLUT);
    }
    else
        mat_copy(rgb_inp, rgb_out, 3);

    return zone;
}

/* input and output lumas are in a range [luma_limits[0], luma_limits[1]] */
MATFLOAT gm_tm_itp(MATFLOAT itp_inp[3], MATFLOAT itp_out[3], MATFLOAT luma_limits[3],
    MATFLOAT lum_min, MATFLOAT lum_max, int en_tm_scale_color, int en_tm_scale_luma)
{
    MATFLOAT color_scale = 1.0;
    MATFLOAT luma_inp = itp_inp[0];

    if (en_tm_scale_luma) /* LUMA scaling */
        itp_out[0] = gm_scale_luma(luma_inp, luma_limits, lum_min, lum_max);
    else /* LUMA correction as in BT.2390 */
        itp_out[0] = gm_tm_luma(luma_inp, luma_limits, lum_min, lum_max);

    /* CHROMA correction as in BT.2390 */
    if (en_tm_scale_color && (itp_out[0] != luma_inp)) {
        color_scale = (itp_out[0] < luma_inp) ? itp_out[0] / luma_inp : luma_inp / itp_out[0];
        itp_out[1] = itp_inp[1] * color_scale;
        itp_out[2] = itp_inp[2] * color_scale;
    }
    else {
        itp_out[1] = itp_inp[1];
        itp_out[2] = itp_inp[2];
    }

    return color_scale;
}

/* input and output lumas are in a range [luma_limits[0], luma_limits[1]] */
MATFLOAT gm_tm_luma(MATFLOAT luma, MATFLOAT luma_limits[3], MATFLOAT lum_min, MATFLOAT lum_max)
{
    const MATFLOAT cfEpsilon = 0.000001;
    MATFLOAT ks = (1.5 * lum_max) - 0.5;
    MATFLOAT b = lum_min;
    MATFLOAT e0, e1, e2, e3, e4;

    /* Input luma must be normilized to [0.0,1.0] */
    e0 = luma;
    e1 = mat_norm(e0, luma_limits[0], luma_limits[2]);
    e1 = mat_clamp(e1, 0.0, 1.0);

    if (e1 < ks) {
        e2 = e1;
    } else {
        MATFLOAT t = ((1.0 - ks) <= cfEpsilon) ? (e1 - ks) : ((e1 - ks) / (1.0 - ks));
        MATFLOAT t2 = t * t;
        MATFLOAT t3 = t2 * t;

        e2 = (((2.0 * t3) - (3.0 * t2) + 1.0) * ks) + ((t3 - (2.0 * t2) + t) * (1.0 - ks)) + (((-2.0 * t3) + (3.0 * t2)) * lum_max);
    }
    e3 = e2 + b * mat_pow((1.0 - e2), 4.0);

    /* Output luma must be denormilized back to [i_afLumaLim[0], i_afLumaLim[1]] */
    e4 = mat_denorm(e3, luma_limits[0], luma_limits[2]);
    e4 = mat_clamp(e4, luma_limits[0], luma_limits[1]);

    return e4;
}

/* input and output lumas are in a range [luma_limits[0], luma_limits[1]] */
MATFLOAT gm_scale_luma(MATFLOAT luma, MATFLOAT luma_limits[3], MATFLOAT lum_min, MATFLOAT lum_max)
{
    MATFLOAT e0, e1, e2, e3, e4;

    /* Input luma must be normilized to [0.0,1.0] */
    e0 = luma;
    e1 = mat_norm(e0, luma_limits[0], luma_limits[2]);
    e1 = mat_clamp(e1, 0.0, 1.0);

    e2 = (e1 - lum_min) * (lum_max - lum_min);
    e3 = e2 + lum_min;

    /* Output luma must be denormilized back to [i_afLumaLim[0], i_afLumaLim[1]] */
    e4 = mat_denorm(e3, luma_limits[0], luma_limits[2]);
    e4 = mat_clamp(e4, luma_limits[0], luma_limits[1]);

    return e4;
}

int gm_map_itp(struct s_gamut_map *ptr_gamut_map, MATFLOAT itp_inp[3], MATFLOAT itp_out[3])
{
    int zone = 0;
    MATFLOAT itp_tm[3], itp_hr[3];

    /* tone map */
    if ((ptr_gamut_map->lum_min > 0.0) || (ptr_gamut_map->lum_max < 1.0))
        gm_tm_itp(itp_inp, itp_tm, ptr_gamut_map->color_space_src.luma_limits,
            ptr_gamut_map->lum_min, ptr_gamut_map->lum_max,
            (ptr_gamut_map->gamut_map_mode == EGMM_TM) ? ptr_gamut_map->en_tm_scale_color : 0,
            (ptr_gamut_map->mode & GM_SCALE_LUMA) ? 1 : 0);
    else
        mat_copy(itp_inp, itp_tm, 3);

    /* hue rotation */
    if (ptr_gamut_map->hue_rot_mode != EHRM_NONE)
        gm_hr_itp(ptr_gamut_map, itp_tm, itp_hr, 0);
    else
        mat_copy(itp_tm, itp_hr, 3);

    /* color map */
    switch (ptr_gamut_map->gamut_map_mode) {
    case EGMM_TM_CHCI:
        zone = gm_map_chci_itp(ptr_gamut_map, itp_hr, itp_out);
        break;
    case EGMM_TM_CHSO:
        zone = gm_map_chso_itp(ptr_gamut_map, itp_hr, itp_out);
        break;
    case EGMM_TM_CHTO:
        zone = gm_map_chto_itp(ptr_gamut_map, itp_hr, itp_out);
        break;
    case EGMM_TM:
    default:
        mat_copy(itp_hr, itp_out, 3);
        break;
    }

    return zone;
}

int gm_map_chto_itp(struct s_gamut_map *ptr_gamut_map, MATFLOAT itp_inp[3], MATFLOAT itp_out[3])
{
    const MATFLOAT gm_2pi = 2.0 * mat_get_pi();
    int zone;
    int pnt_map = -1;
    int vec_hue_ind[2];
    MATFLOAT hue, hue_phs;
    MATFLOAT origin2_ic[2], origin3_ic[2];

    if (gm_is_valid_itp(&ptr_gamut_map->color_space_dst, itp_inp, ptr_gamut_map->mode & GM_PQTAB_3DLUT)) {
        mat_copy(itp_inp, itp_out, 3);
        return 0;
    }

    hue = mat_angle(itp_inp[2], itp_inp[1]);
    hue_phs = gm_hue_to_index_phase(hue, gm_2pi, ptr_gamut_map->num_hue_pnts, vec_hue_ind);
    gm_interp_ic(vec_hue_ind, hue_phs, ptr_gamut_map->ptr_org2_ic, origin2_ic);
    gm_interp_ic(vec_hue_ind, hue_phs, ptr_gamut_map->ptr_org3_ic, origin3_ic);

    zone = gm_get_zone(itp_inp, origin2_ic, origin3_ic, ptr_gamut_map->color_space_dst.luma_limits);
    if ((ptr_gamut_map->mode & GM_ZONE1_FLEX) && (zone == 1)) {
        /* correct origin2 for zone 1 to prevent noise bursting for dim content */
        MATFLOAT int0 = ptr_gamut_map->color_space_dst.luma_limits[0];
        MATFLOAT int1 = origin2_ic[0];
        MATFLOAT range_int = int1 - int0;
        MATFLOAT thresh_int = (int1 + int0) / 2.0;
        MATFLOAT phase;

        if (itp_inp[0] < thresh_int) {
            phase = (itp_inp[0] - int0) / range_int;
            origin2_ic[0] = itp_inp[0] + (int1 - itp_inp[0]) * phase;
        } else {
            phase = (int1 - itp_inp[0]) / range_int;
            origin2_ic[0] = int1 + (itp_inp[0] - int1) * phase;
        }
    }

    switch (ptr_gamut_map->map_type) {
    case EMT_SEG:
        pnt_map = gm_map_seg_itp(ptr_gamut_map, itp_inp, itp_out, zone, origin2_ic, origin3_ic, vec_hue_ind, hue_phs);
        break;
    case EMT_RAD:
        pnt_map = gm_map_rad_itp(ptr_gamut_map, itp_inp, itp_out, zone, origin2_ic, origin3_ic, hue);
        break;
    case EMT_SEGRAD:
        pnt_map = gm_map_segrad_itp(ptr_gamut_map, itp_inp, itp_out, zone, origin2_ic, origin3_ic, hue, vec_hue_ind, hue_phs);
        break;
    default:
        mat_copy(itp_inp, itp_out, 3);
        break;
    }

    return zone;
}

int gm_map_chso_itp(struct s_gamut_map *ptr_gamut_map, MATFLOAT itp_inp[3], MATFLOAT itp_out[3])
{
    const MATFLOAT gm_2pi = 2.0 * mat_get_pi();
    int zone = 1;
    int pnt_map = -1;
    int vec_hue_ind[2];
    MATFLOAT hue, hue_phs;
    MATFLOAT origin2_ic[2], origin3_ic[2];

    if (gm_is_valid_itp(&ptr_gamut_map->color_space_dst, itp_inp, ptr_gamut_map->mode & GM_PQTAB_3DLUT)) {
        mat_copy(itp_inp, itp_out, 3);
        return 0;
    }

    hue = mat_angle(itp_inp[2], itp_inp[1]);
    hue_phs = gm_hue_to_index_phase(hue, gm_2pi, ptr_gamut_map->num_hue_pnts, vec_hue_ind);
    gm_interp_ic(vec_hue_ind, hue_phs, ptr_gamut_map->ptr_org2_ic, origin2_ic);
    origin2_ic[1] = 0.0;
    origin3_ic[0] = itp_inp[0];
    origin3_ic[1] = mat_radius(itp_inp[2], itp_inp[1]);    /* chroma */

    switch (ptr_gamut_map->map_type) {
    case EMT_SEG:
        pnt_map = gm_map_seg_itp(ptr_gamut_map, itp_inp, itp_out, zone, origin2_ic, origin3_ic, vec_hue_ind, hue_phs);
        break;
    case EMT_RAD:
        pnt_map = gm_map_rad_itp(ptr_gamut_map, itp_inp, itp_out, zone, origin2_ic, origin3_ic, hue);
        break;
    case EMT_SEGRAD:
        pnt_map = gm_map_segrad_itp(ptr_gamut_map, itp_inp, itp_out, zone, origin2_ic, origin3_ic, hue, vec_hue_ind, hue_phs);
        break;
    default:
        mat_copy(itp_inp, itp_out, 3);
        break;
    }

    return zone;
}

int gm_map_chci_itp(struct s_gamut_map *ptr_gamut_map, MATFLOAT itp_inp[3], MATFLOAT itp_out[3])
{
    const MATFLOAT gm_2pi = 2.0 * mat_get_pi();
    int zone = 1;
    int pnt_map = -1;
    MATFLOAT origin2_ic[2] = { itp_inp[0], 0.0 };
    MATFLOAT origin3_ic[2] = { itp_inp[0], 0.0 };
    int vec_hue_ind[2];
    MATFLOAT hue, hue_phs;

    if (gm_is_valid_itp(&ptr_gamut_map->color_space_dst, itp_inp, ptr_gamut_map->mode & GM_PQTAB_3DLUT)) {
        mat_copy(itp_inp, itp_out, 3);
        return 0;
    }

    hue = mat_angle(itp_inp[2], itp_inp[1]);
    hue_phs = gm_hue_to_index_phase(hue, gm_2pi, ptr_gamut_map->num_hue_pnts, vec_hue_ind);
    switch (ptr_gamut_map->map_type) {
    case EMT_SEG:
        pnt_map = gm_map_seg_itp(ptr_gamut_map, itp_inp, itp_out, zone, origin2_ic, origin3_ic, vec_hue_ind, hue_phs);
        break;
    case EMT_RAD:
        pnt_map = gm_map_rad_itp(ptr_gamut_map, itp_inp, itp_out, zone, origin2_ic, origin3_ic, hue);
        break;
    case EMT_SEGRAD:
        pnt_map = gm_map_segrad_itp(ptr_gamut_map, itp_inp, itp_out, zone, origin2_ic, origin3_ic, hue, vec_hue_ind, hue_phs);
        break;
    default:
        mat_copy(itp_inp, itp_out, 3);
        break;
    }

    return zone;
}

/* direction : 0 - src to dst (forward), 1 - dst to src (backward) */
void gm_hr_itp(struct s_gamut_map *ptr_gamut_map, MATFLOAT itp_inp[3], MATFLOAT itp_out[3], int direction)
{
    MATFLOAT ich_inp[3], ich_out[3];

    cs_itp_to_ich(itp_inp, ich_inp);
    gm_hr_ich(ptr_gamut_map, ich_inp, ich_out, direction);
    cs_ich_to_itp(ich_out, itp_out);
}

/* direction : 0 - src to dst (forward), 1 - dst to src (backward) */
void gm_hr_ich(struct s_gamut_map *ptr_gamut_map, MATFLOAT ich_inp[3], MATFLOAT ich_out[3], int direction)
{
    MATFLOAT *ptr_hr_src_hc = direction ? ptr_gamut_map->ptr_hr_dst_hc : ptr_gamut_map->ptr_hr_src_hc;
    MATFLOAT *ptr_hr_dst_hc = direction ? ptr_gamut_map->ptr_hr_src_hc : ptr_gamut_map->ptr_hr_dst_hc;
    MATFLOAT rot_hs_cg[2];

    /* get hue shift and chroma gain parameeters */
    gm_get_hr_parms(ich_inp, ptr_gamut_map->color_space_dst.luma_limits, ptr_hr_src_hc, ptr_hr_dst_hc, ptr_gamut_map->num_int_pnts, rot_hs_cg);

    ich_out[0] = ich_inp[0];
    ich_out[1] = (ptr_gamut_map->hue_rot_mode & GM_CHROMA_GAIN) ? ich_inp[1] * rot_hs_cg[1] : ich_inp[1];
    ich_out[2] = (ptr_gamut_map->hue_rot_mode & GM_HUE_SHIFT) ? mat_norm_angle(ich_inp[2] + rot_hs_cg[0]) : ich_inp[2];
}

void gm_get_hr_parms(MATFLOAT ich[3], MATFLOAT luma_limits[3], MATFLOAT *ptr_hr_src_hc,
        MATFLOAT *ptr_hr_dst_hc, int num_int_pnts, MATFLOAT rot_hs_cg[2])
{
    const MATFLOAT gm_2pi = 2.0 * mat_get_pi();
    MATFLOAT vec_hc_src[2][GM_NUM_PRIM], vec_hc_dst[2][GM_NUM_PRIM];
    MATFLOAT int_src, hue_src, hue_dst, chroma_src, chroma_dst;
    int vec_int_ind[2];
    MATFLOAT int_phs;
    int vec_hue_ind[2];
    MATFLOAT hue_phs;
    int nk, ni;

    hue_src = ich[2];
    int_src = mat_norm(ich[0], luma_limits[0], luma_limits[2]);    /* normilize to [0.0,1.0] */
    int_phs = mat_flt_to_index_phase(int_src, 1.0, num_int_pnts, vec_int_ind);
    for (nk = 0; nk < GM_NUM_PRIM; nk++) {
        int ind0 = (nk * num_int_pnts + vec_int_ind[0]) * 2;
        int ind1 = (nk * num_int_pnts + vec_int_ind[1]) * 2;
        for (ni = 0; ni < 2; ni++) {
            vec_hc_src[ni][nk] = ptr_hr_src_hc[ind0 + ni] + (ptr_hr_src_hc[ind1 + ni] - ptr_hr_src_hc[ind0 + ni]) * int_phs;
            vec_hc_dst[ni][nk] = ptr_hr_dst_hc[ind0 + ni] + (ptr_hr_dst_hc[ind1 + ni] - ptr_hr_dst_hc[ind0 + ni]) * int_phs;
        }
    }

    hue_phs = mat_hue_to_index_phase(hue_src, GM_NUM_PRIM, vec_hc_src[0], gm_2pi, 0, vec_hue_ind);
    if (vec_hue_ind[1] == 0)
        vec_hc_dst[0][vec_hue_ind[1]] += gm_2pi;    /* correct hue for 2pi crossing */

    /* calulate hue rotation */
    hue_dst = vec_hc_dst[0][vec_hue_ind[0]] + (vec_hc_dst[0][vec_hue_ind[1]] - vec_hc_dst[0][vec_hue_ind[0]]) * hue_phs;
    hue_dst = mat_norm_angle(hue_dst);
    rot_hs_cg[0] = hue_dst - hue_src;

    /* calculate chroma gain */
    chroma_src = vec_hc_src[1][vec_hue_ind[0]] + (vec_hc_src[1][vec_hue_ind[1]] - vec_hc_src[1][vec_hue_ind[0]]) * hue_phs;
    chroma_dst = vec_hc_dst[1][vec_hue_ind[0]] + (vec_hc_dst[1][vec_hue_ind[1]] - vec_hc_dst[1][vec_hue_ind[0]]) * hue_phs;
    rot_hs_cg[1] = (chroma_src > 0.0) ? MAT_MIN(chroma_dst / chroma_src, 1.0) : 1.0;
}

int gm_map_seg_itp(struct s_gamut_map *ptr_gamut_map, MATFLOAT itp_inp[3], MATFLOAT itp_out[3],
    int zone, MATFLOAT origin2_ic[2], MATFLOAT origin3_ic[2], int vec_hue_ind[2], MATFLOAT hue_phs)
{
    int pnt_map = -1;

    switch (zone) {
    case 1:
        pnt_map = gm_map_zone1_seg(itp_inp, itp_out, vec_hue_ind, hue_phs, origin2_ic,
            ptr_gamut_map->num_edge_pnts, ptr_gamut_map->ptr_edge_ic, 0, ptr_gamut_map->num_edge_pnts - 1);
        break;
    case 2:
        pnt_map = gm_map_zone2_seg(itp_inp, itp_out, vec_hue_ind, hue_phs, origin2_ic,
            ptr_gamut_map->num_edge_pnts, ptr_gamut_map->ptr_edge_ic, ptr_gamut_map->num_edge_pnts - 1, 0);
        break;
    case 3:
        pnt_map = gm_map_zone3_seg(itp_inp, itp_out, vec_hue_ind, hue_phs, origin3_ic,
            ptr_gamut_map->num_edge_pnts, ptr_gamut_map->ptr_edge_ic, ptr_gamut_map->num_edge_pnts - 1, 0);
        break;
    default:
        mat_copy(itp_inp, itp_out, 3);
        break;
    }

    return pnt_map;
}

int gm_map_rad_itp(struct s_gamut_map *ptr_gamut_map, MATFLOAT itp_inp[3], MATFLOAT itp_out[3],
    int zone, MATFLOAT origin2_ic[2], MATFLOAT origin3_ic[2], MATFLOAT hue)
{
    switch (zone) {
    case 1:
        gm_map_zone1_rad(&ptr_gamut_map->color_space_dst, itp_inp, itp_out,
            ptr_gamut_map->step_samp, origin2_ic, hue, ptr_gamut_map->mode & GM_PQTAB_3DLUT);
        break;
    case 2:
        gm_map_zone2_rad(&ptr_gamut_map->color_space_dst, itp_inp, itp_out,
            ptr_gamut_map->step_samp, origin2_ic, hue, ptr_gamut_map->mode & GM_PQTAB_3DLUT);
        break;
    case 3:
        gm_map_zone3_rad(&ptr_gamut_map->color_space_dst, itp_inp, itp_out,
            ptr_gamut_map->step_samp, origin3_ic, hue, ptr_gamut_map->mode & GM_PQTAB_3DLUT);
        break;
    default:
        mat_copy(itp_inp, itp_out, 3);
        break;
    }

    return 1;
}

int gm_map_segrad_itp(struct s_gamut_map *ptr_gamut_map, MATFLOAT itp_inp[3], MATFLOAT itp_out[3],
    int zone, MATFLOAT origin2_ic[2], MATFLOAT origin3_ic[2], MATFLOAT hue, int vec_hue_ind[2], MATFLOAT hue_phs)
{
    int pnt_map = -1;
    MATFLOAT seg_itp[3];

    switch (zone) {
    case 1:
        pnt_map = gm_map_zone1_seg(itp_inp, seg_itp, vec_hue_ind, hue_phs, origin2_ic,
            ptr_gamut_map->num_edge_pnts, ptr_gamut_map->ptr_edge_ic, 0, ptr_gamut_map->num_edge_pnts - 1);
        gm_map_zone1_rad(&ptr_gamut_map->color_space_dst, seg_itp, itp_out, ptr_gamut_map->step_samp,
            origin2_ic, hue, ptr_gamut_map->mode & GM_PQTAB_3DLUT);
        break;
    case 2:
        pnt_map = gm_map_zone2_seg(itp_inp, seg_itp, vec_hue_ind, hue_phs, origin2_ic,
            ptr_gamut_map->num_edge_pnts, ptr_gamut_map->ptr_edge_ic, ptr_gamut_map->num_edge_pnts - 1, 0);
        gm_map_zone2_rad(&ptr_gamut_map->color_space_dst, seg_itp, itp_out, ptr_gamut_map->step_samp,
            origin2_ic, hue, ptr_gamut_map->mode & GM_PQTAB_3DLUT);
        break;
    case 3:
        pnt_map = gm_map_zone3_seg(itp_inp, seg_itp, vec_hue_ind, hue_phs, origin3_ic,
            ptr_gamut_map->num_edge_pnts, ptr_gamut_map->ptr_edge_ic, ptr_gamut_map->num_edge_pnts - 1, 0);
        gm_map_zone3_rad(&ptr_gamut_map->color_space_dst, seg_itp, itp_out, ptr_gamut_map->step_samp,
            origin3_ic, hue, ptr_gamut_map->mode & GM_PQTAB_3DLUT);
        break;
    default:
        mat_copy(itp_inp, itp_out, 3);
        break;
    }

    return pnt_map;
}

MATFLOAT gm_hue_to_index_phase(MATFLOAT hue, MATFLOAT hue_max, int num_hue_pnts, int vec_hue_ind[2])
{
    MATFLOAT hue_step = hue_max / (MATFLOAT)num_hue_pnts;
    MATFLOAT hue_max_ind = hue_step * (MATFLOAT)(num_hue_pnts - 1);
    MATFLOAT tmp = (MATFLOAT)(num_hue_pnts - 1) / hue_max_ind;

    vec_hue_ind[0] = (int)(hue * tmp);
    vec_hue_ind[1] = (vec_hue_ind[0] + 1) % num_hue_pnts;

    return (hue - (MATFLOAT)vec_hue_ind[0] / tmp) / hue_step;
}

void gm_interp_ic(int vec_hue_ind[2], MATFLOAT hue_phs, MATFLOAT vec_pnt_ic[], MATFLOAT pnt_ic[2])
{
    int off0 = vec_hue_ind[0] << 1;
    int off1 = vec_hue_ind[1] << 1;

    pnt_ic[0] = vec_pnt_ic[off0 + 0] + (vec_pnt_ic[off1 + 0] - vec_pnt_ic[off0 + 0]) * hue_phs;
    pnt_ic[1] = vec_pnt_ic[off0 + 1] + (vec_pnt_ic[off1 + 1] - vec_pnt_ic[off0 + 1]) * hue_phs;
}

void gm_getseg_ic(int vec_hue_ind[2], MATFLOAT hue_phs, int ind_seg, int num_edge_pnts,
    MATFLOAT *ptr_edge_ic, MATFLOAT pnt_ic[2])
{
    int off0 = (vec_hue_ind[0] * num_edge_pnts + ind_seg) << 1;
    int off1 = (vec_hue_ind[1] * num_edge_pnts + ind_seg) << 1;
    MATFLOAT pnt0_ic[2], pnt1_ic[2];

    pnt0_ic[0] = ptr_edge_ic[off0 + 0];
    pnt0_ic[1] = ptr_edge_ic[off0 + 1];
    pnt1_ic[0] = ptr_edge_ic[off1 + 0];
    pnt1_ic[1] = ptr_edge_ic[off1 + 1];

    pnt_ic[0] = pnt0_ic[0] + (pnt1_ic[0] - pnt0_ic[0]) * hue_phs;
    pnt_ic[1] = pnt0_ic[1] + (pnt1_ic[1] - pnt0_ic[1]) * hue_phs;
}

void gm_genedge(struct s_color_space *ptr_color_space, MATFLOAT luma_limits[3], int num_edge_pnts,
        enum gm_edge_type edge_type, MATFLOAT step_samp, MATFLOAT hue, MATFLOAT *ptr_edge_ic, int en_pq_lut)
{
    if (edge_type == EET_CHROMA) /* chroma for constant intensity */
        gm_genedge_int(ptr_color_space, luma_limits, num_edge_pnts, hue, step_samp, ptr_edge_ic, en_pq_lut);
    else /* intensity and chroma for constant elevaltion angle */
        gm_genedge_rad(ptr_color_space, luma_limits, num_edge_pnts, hue, step_samp, ptr_edge_ic, en_pq_lut);
}

void gm_genedge_int(struct s_color_space *ptr_color_space, MATFLOAT luma_limits[3], int num_edge_pnts,
        MATFLOAT hue, MATFLOAT step_samp, MATFLOAT *ptr_edge_ic, int en_pq_lut)
{
    MATFLOAT hue_sin_cos[2] = { mat_sin(hue), mat_cos(hue) };
    MATFLOAT step_int = luma_limits[2] / (MATFLOAT)(num_edge_pnts - 1);
    MATFLOAT pnt_ic[2] = { luma_limits[0], 0.0 };
    MATFLOAT inc_ic[2] = { 0.0, step_samp };
    MATFLOAT vec_chroma_prev[2] = { pnt_ic[1], pnt_ic[1] };
    int np;

    ptr_edge_ic[0] = pnt_ic[0];
    ptr_edge_ic[1] = pnt_ic[1];
    for (np = 1; np < num_edge_pnts - 1; np++) {
        pnt_ic[0] += step_int;
        pnt_ic[1] = 2.0 * vec_chroma_prev[1] - vec_chroma_prev[0];    /* linear predictor */
        pnt_ic[1] = MAT_MAX(pnt_ic[1], 0.0);
        gm_sample_edge_ic(ptr_color_space, hue_sin_cos, inc_ic, pnt_ic, en_pq_lut);
        vec_chroma_prev[0] = vec_chroma_prev[1];
        vec_chroma_prev[1] = pnt_ic[1];
        ptr_edge_ic[np * 2 + 0] = pnt_ic[0];
        ptr_edge_ic[np * 2 + 1] = pnt_ic[1];
    }
    ptr_edge_ic[(num_edge_pnts - 1) * 2 + 0] = luma_limits[1];
    ptr_edge_ic[(num_edge_pnts - 1) * 2 + 1] = 0.0;
}

void gm_genedge_rad(struct s_color_space *ptr_color_space, MATFLOAT luma_limits[3], int num_edge_pnts,
    MATFLOAT hue, MATFLOAT step_samp, MATFLOAT *ptr_edge_ic, int en_pq_lut)
{
    const MATFLOAT gm_pi = mat_get_pi();
    MATFLOAT hue_sin_cos[2] = { mat_sin(hue), mat_cos(hue) };
    MATFLOAT step_angle = gm_pi / (MATFLOAT)(num_edge_pnts - 1);
    MATFLOAT vec_org[2] = { mat_denorm(GM_EDGE_ORG, ptr_color_space->luma_limits[0], ptr_color_space->luma_limits[2]), 0.0 };
    MATFLOAT angle = step_angle;
    MATFLOAT radius = vec_org[0] - luma_limits[0];
    MATFLOAT vec_radius_prev[2] = { radius, radius };
    int np;

    ptr_edge_ic[0] = luma_limits[0];
    ptr_edge_ic[1] = 0.0;
    for (np = 1; np < num_edge_pnts - 1; np++) {
        MATFLOAT ang_sin_cos[2] = { mat_sin(angle), mat_cos(angle) };
        MATFLOAT inc_ic[2] = {-step_samp * ang_sin_cos[1], step_samp * ang_sin_cos[0] };
        MATFLOAT pnt_ic[2];

        if (np > 1)
            radius = 2.0 * vec_radius_prev[1] - vec_radius_prev[0];    /* linear predictor */
        pnt_ic[0] = vec_org[0] - radius * ang_sin_cos[1];
        pnt_ic[1] = radius * ang_sin_cos[0];
        gm_sample_edge_ic(ptr_color_space, hue_sin_cos, inc_ic, pnt_ic, en_pq_lut);
        vec_radius_prev[0] = vec_radius_prev[1];
        vec_radius_prev[1] = mat_radius(vec_org[0] - pnt_ic[0], pnt_ic[1]);
        ptr_edge_ic[np * 2 + 0] = pnt_ic[0];
        ptr_edge_ic[np * 2 + 1] = pnt_ic[1];
        angle += step_angle;
    }
    ptr_edge_ic[(num_edge_pnts - 1) * 2 + 0] = luma_limits[1];
    ptr_edge_ic[(num_edge_pnts - 1) * 2 + 1] = 0.0;
}

void gm_edgecusp_adjust(MATFLOAT *ptr_edge_ic, int num_edge_pnts, MATFLOAT cusp_ic[2])
{
    int ind0, ind1;
    MATFLOAT delta0, delta1;

    for (ind1 = 2 * (num_edge_pnts >> 2); ind1 < 2 * num_edge_pnts; ind1 += 2) {
        if (ptr_edge_ic[ind1] >= cusp_ic[0]) {
            ind0 = ind1 - 2;
            delta1 = ptr_edge_ic[ind1] - cusp_ic[0];
            delta0 = cusp_ic[0] - ptr_edge_ic[ind0];
            if (delta0 < delta1) {
                ptr_edge_ic[ind0] = cusp_ic[0];
                ptr_edge_ic[ind0 + 1] = cusp_ic[1];
            } else {
                ptr_edge_ic[ind1] = cusp_ic[0];
                ptr_edge_ic[ind1 + 1] = cusp_ic[1];
            }
            break;
        }
    }
}

void gm_sample_edge_ic(struct s_color_space *ptr_color_space, MATFLOAT hue_sin_cos[2],
    MATFLOAT inc_ic[2], MATFLOAT pnt_ic[2], int en_pq_lut)
{
    if (gm_is_valid_ic(ptr_color_space, pnt_ic, hue_sin_cos, en_pq_lut)) {
        do {
            pnt_ic[0] += inc_ic[0];
            pnt_ic[1] += inc_ic[1];
        } while (gm_is_valid_ic(ptr_color_space, pnt_ic, hue_sin_cos, en_pq_lut));
        pnt_ic[0] -= inc_ic[0];
        pnt_ic[1] -= inc_ic[1];
    } else {
        do {
            pnt_ic[0] -= inc_ic[0];
            pnt_ic[1] -= inc_ic[1];
            pnt_ic[1] = MAT_MAX(pnt_ic[1], 0.0); /* for zone 3 */
        } while (!gm_is_valid_ic(ptr_color_space, pnt_ic, hue_sin_cos, en_pq_lut) && (pnt_ic[1] > 0.0));
    }
}


int gm_get_zone(MATFLOAT itp[3], MATFLOAT origin2_ic[2], MATFLOAT origin3_ic[2], MATFLOAT luma_limits[3])
{
    MATFLOAT chroma = mat_radius(itp[2], itp[1]);
    MATFLOAT slope, offset;

    if (itp[0] < origin2_ic[0])
        return 1;

    slope = (origin3_ic[0] - origin2_ic[0]) / (origin3_ic[1] - origin2_ic[1]);
    offset = origin2_ic[0] - slope * origin2_ic[1];

    if (itp[0] < slope * chroma + offset)
        return 2;

    return 3;
}

int gm_map_zone1_seg(MATFLOAT itp_inp[3], MATFLOAT itp_out[3], int vec_hue_ind[2], MATFLOAT hue_phs,
    MATFLOAT origin2_ic[2], int num_edge_pnts, MATFLOAT *ptr_edge_ic, int pnt_fst, int pnt_lst)
{
    int pnt_inc = (pnt_fst < pnt_lst) ? 1 : -1;
    MATFLOAT pnt0_ich[3], pnt1_ich[3];
    MATFLOAT pnt_ich[3];
    MATFLOAT vec_seg_ic[2][2];
    MATFLOAT s_ic[2];
    int np;

    cs_itp_to_ich(itp_inp, pnt0_ich);
    pnt1_ich[0] = origin2_ic[0];
    pnt1_ich[1] = 0.0;
    pnt1_ich[2] = pnt0_ich[2];
    s_ic[0] = pnt1_ich[0] - pnt0_ich[0];
    s_ic[1] = pnt1_ich[1] - pnt0_ich[1];

    gm_getseg_ic(vec_hue_ind, hue_phs, pnt_fst, num_edge_pnts, ptr_edge_ic, vec_seg_ic[0]);

    for (np = pnt_fst + pnt_inc; (pnt_inc > 0) ? np <= pnt_lst : np >= pnt_lst; np += pnt_inc) {
        gm_getseg_ic(vec_hue_ind, hue_phs, np, num_edge_pnts, ptr_edge_ic, vec_seg_ic[1]);
        if (gm_seg_intersection(pnt0_ich, pnt1_ich, s_ic, vec_seg_ic[0], vec_seg_ic[1], pnt_ich)) {
            pnt_ich[2] = pnt0_ich[2];
            cs_ich_to_itp(pnt_ich, itp_out);
            return np;
        }
        mat_copy(vec_seg_ic[1], vec_seg_ic[0], 2);
    }

    mat_copy(itp_inp, itp_out, 3); /* Should not happen */

    return -1;
}

int gm_map_zone2_seg(MATFLOAT itp_inp[3], MATFLOAT itp_out[3], int vec_hue_ind[2], MATFLOAT hue_phs,
    MATFLOAT origin2_ic[2], int num_edge_pnts, MATFLOAT *ptr_edge_ic, int pnt_fst, int pnt_lst)
{
    int pnt_inc = (pnt_fst < pnt_lst) ? 1 : -1;
    MATFLOAT pnt0_ich[3], pnt1_ich[3];
    MATFLOAT pnt_ich[3];
    MATFLOAT vec_seg_ic[2][2];
    MATFLOAT s_ic[2];
    int np;

    cs_itp_to_ich(itp_inp, pnt0_ich);
    pnt1_ich[0] = origin2_ic[0];
    pnt1_ich[1] = origin2_ic[1];
    pnt1_ich[2] = pnt0_ich[2];
    s_ic[0] = pnt1_ich[0] - pnt0_ich[0];
    s_ic[1] = pnt1_ich[1] - pnt0_ich[1];

    gm_getseg_ic(vec_hue_ind, hue_phs, pnt_fst, num_edge_pnts, ptr_edge_ic, vec_seg_ic[0]);

    for (np = pnt_fst + pnt_inc; (pnt_inc > 0) ? np <= pnt_lst : np >= pnt_lst; np += pnt_inc) {
        gm_getseg_ic(vec_hue_ind, hue_phs, np, num_edge_pnts, ptr_edge_ic, vec_seg_ic[1]);
        if (gm_seg_intersection(pnt0_ich, pnt1_ich, s_ic, vec_seg_ic[0], vec_seg_ic[1], pnt_ich)) {
            pnt_ich[2] = pnt0_ich[2];
            cs_ich_to_itp(pnt_ich, itp_out);
            return np;
        }
        mat_copy(vec_seg_ic[1], vec_seg_ic[0], 2);
    }

    mat_copy(itp_inp, itp_out, 3); /* Should not happen */

    return -1;
}

int gm_map_zone3_seg(MATFLOAT itp_inp[3], MATFLOAT itp_out[3], int vec_hue_ind[2], MATFLOAT hue_phs,
    MATFLOAT origin3_ic[2], int num_edge_pnts, MATFLOAT *ptr_edge_ic, int pnt_fst, int pnt_lst)
{
    int pnt_inc = (pnt_fst < pnt_lst) ? 1 : -1;
    MATFLOAT pnt0_ich[3], pnt1_ich[3];
    MATFLOAT pnt_ich[3];
    MATFLOAT s_ic[2];
    MATFLOAT vec_seg_ic[2][2];
    MATFLOAT slope, offset;
    int np;

    cs_itp_to_ich(itp_inp, pnt0_ich);
    slope = (origin3_ic[0] - pnt0_ich[0]) / (origin3_ic[1] - pnt0_ich[1]);
    offset = pnt0_ich[0] - slope * pnt0_ich[1];
    pnt0_ich[0] = offset;
    pnt0_ich[1] = 0.0;

    pnt1_ich[0] = origin3_ic[0];
    pnt1_ich[1] = origin3_ic[1];
    pnt1_ich[2] = pnt0_ich[2];
    s_ic[0] = pnt1_ich[0] - pnt0_ich[0];
    s_ic[1] = pnt1_ich[1] - pnt0_ich[1];

    gm_getseg_ic(vec_hue_ind, hue_phs, num_edge_pnts - 1, num_edge_pnts, ptr_edge_ic, vec_seg_ic[0]);

    /* prevent non-intersection for the last segment */
    if (pnt0_ich[0] >= vec_seg_ic[0][0]) {
        itp_out[0] = vec_seg_ic[0][0];
        itp_out[1] = 0.0;
        itp_out[2] = 0.0;
        return num_edge_pnts - 1;
    }

    if (pnt_fst != num_edge_pnts - 1)
        gm_getseg_ic(vec_hue_ind, hue_phs, pnt_fst, num_edge_pnts, ptr_edge_ic, vec_seg_ic[0]);

    for (np = pnt_fst + pnt_inc; (pnt_inc > 0) ? np <= pnt_lst : np >= pnt_lst; np += pnt_inc) {
        gm_getseg_ic(vec_hue_ind, hue_phs, np, num_edge_pnts, ptr_edge_ic, vec_seg_ic[1]);
        if (gm_seg_intersection(pnt0_ich, pnt1_ich, s_ic, vec_seg_ic[0], vec_seg_ic[1], pnt_ich)) {
            pnt_ich[2] = pnt0_ich[2];
            cs_ich_to_itp(pnt_ich, itp_out);
            return np;
        }
        mat_copy(vec_seg_ic[1], vec_seg_ic[0], 2);
    }

    mat_copy(itp_inp, itp_out, 3);    /* Should not happen */

    return -1;
}

void gm_map_zone1_rad(struct s_color_space *ptr_color_space, MATFLOAT itp_inp[3], MATFLOAT itp_out[3],
    MATFLOAT step_samp, MATFLOAT origin2_ic[2], MATFLOAT hue, int en_pq_lut)
{
    MATFLOAT hue_sin_cos[2] = { mat_sin(hue), mat_cos(hue) };
    MATFLOAT chroma = mat_radius(itp_inp[2], itp_inp[1]);
    MATFLOAT int_tmp = origin2_ic[0] - itp_inp[0];
    MATFLOAT angle = mat_angle(chroma, int_tmp);
    MATFLOAT pnt_ic[2] = { itp_inp[0], chroma };
    MATFLOAT inc_ic[2] = { -step_samp * mat_cos(angle), step_samp * mat_sin(angle) };

    gm_sample_edge_ic(ptr_color_space, hue_sin_cos, inc_ic, pnt_ic, en_pq_lut);

    itp_out[0] = pnt_ic[0];
    itp_out[1] = pnt_ic[1] * hue_sin_cos[1];
    itp_out[2] = pnt_ic[1] * hue_sin_cos[0];
}

void gm_map_zone2_rad(struct s_color_space *ptr_color_space, MATFLOAT itp_inp[3], MATFLOAT itp_out[3],
    MATFLOAT step_samp, MATFLOAT origin2_ic[2], MATFLOAT hue, int en_pq_lut)
{
    MATFLOAT hue_sin_cos[2] = { mat_sin(hue), mat_cos(hue) };
    MATFLOAT chroma = mat_radius(itp_inp[2], itp_inp[1]);
    MATFLOAT int_tmp = itp_inp[0] - origin2_ic[0];
    MATFLOAT angle = mat_angle(int_tmp, chroma - origin2_ic[1]);
    MATFLOAT pnt_ic[2] = { itp_inp[0], chroma };
    MATFLOAT inc_ic[2] = { step_samp * mat_sin(angle), step_samp * mat_cos(angle) };

    gm_sample_edge_ic(ptr_color_space, hue_sin_cos, inc_ic, pnt_ic, en_pq_lut);

    itp_out[0] = pnt_ic[0];
    itp_out[1] = pnt_ic[1] * hue_sin_cos[1];
    itp_out[2] = pnt_ic[1] * hue_sin_cos[0];
}

void gm_map_zone3_rad(struct s_color_space *ptr_color_space, MATFLOAT itp_inp[3], MATFLOAT itp_out[3],
    MATFLOAT step_samp, MATFLOAT origin3_ic[2], MATFLOAT hue, int en_pq_lut)
{
    MATFLOAT hue_sin_cos[2] = { mat_sin(hue), mat_cos(hue) };
    MATFLOAT chroma = mat_radius(itp_inp[2], itp_inp[1]);
    MATFLOAT int_tmp = origin3_ic[0] - itp_inp[0];
    MATFLOAT angle = mat_angle(int_tmp, origin3_ic[1] - chroma);
    MATFLOAT pnt_ic[2] = { itp_inp[0], chroma };
    MATFLOAT inc_ic[2] = { step_samp * mat_sin(angle), step_samp * mat_cos(angle) };

    gm_sample_edge_ic(ptr_color_space, hue_sin_cos, inc_ic, pnt_ic, en_pq_lut);

    itp_out[0] = pnt_ic[0];
    itp_out[1] = pnt_ic[1] * hue_sin_cos[1];
    itp_out[2] = pnt_ic[1] * hue_sin_cos[0];
}

void gm_show_pix(int zone, MATFLOAT itp_src[3], MATFLOAT itp_dst[3], MATFLOAT rgb[3],
    enum gm_show_pix_mode show_pix_mode, MATFLOAT hue_limits[2])
{
    MATFLOAT hue = mat_angle(itp_src[2], itp_src[1]);

    switch (show_pix_mode) {
    case ESPM_NOMAP:
        if (zone != 0)
            mat_set(0.5, rgb, 3);
        break;
    case ESPM_MAP:
        if (zone == 0)
            mat_set(0.5, rgb, 3);
        break;
    case ESPM_MAPZ1:
        if (zone != 1)
            mat_set(0.5, rgb, 3);
        break;
    case ESPM_MAPZ2:
        if (zone != 2)
            mat_set(0.5, rgb, 3);
        break;
    case ESPM_MAPZ3:
        if (zone != 3)
            mat_set(0.5, rgb, 3);
        break;
    case ESPM_NUMZ:
        mat_set((MATFLOAT)zone / 3.0, rgb, 3);
        break;
    case ESPM_HUEINP:
        if ((hue < hue_limits[0]) || (hue > hue_limits[1]))
            mat_set(0.5, rgb, 3);
        break;
    case ESPM_HUEOUT:
        if ((hue < hue_limits[0]) || (hue > hue_limits[1]))
            mat_set(0.5, rgb, 3);
        break;
    default:
        break;
    }
}

void gm_gen_3dlut(struct s_gamut_map* ptr_gamut_map, int num_pnts, int bitwidth,
    int en_merge, unsigned short* ptr_3dlut_rgb)
{
    int val_max = (1 << bitwidth) - 1;
    int index = 0;
    int nir, nig, nib;
    unsigned short rgb[3];
    MATFLOAT rgb_src[3], rgb_dst[3];
    MATFLOAT rgb_src_lin[3], rgb_dst_lin[3];

    #ifdef GM_SIM
    #pragma omp parallel for private(index, nig, nib, rgb, rgb_src, rgb_dst, rgb_src_lin, rgb_dst_lin)
    #endif
    for (nir = 0; nir < num_pnts; nir++) {
        index = num_pnts * num_pnts * nir * 3;
        rgb[0] = en_merge ? ptr_3dlut_rgb[index + 0] : (nir * val_max) / (num_pnts - 1);
        rgb_src[0] = mat_int2flt(rgb[0], val_max);
        rgb_src_lin[0] = cs_nlin_to_lin(&ptr_gamut_map->color_space_src, rgb_src[0]);
        for (nig = 0; nig < num_pnts; nig++) {
            rgb[1] = en_merge ? ptr_3dlut_rgb[index + 1] : (nig * val_max) / (num_pnts - 1);
            rgb_src[1] = mat_int2flt(rgb[1], val_max);
            rgb_src_lin[1] = cs_nlin_to_lin(&ptr_gamut_map->color_space_src, rgb_src[1]);
            for (nib = 0; nib < num_pnts; nib++) {
                rgb[2] = en_merge ? ptr_3dlut_rgb[index + 2] : (nib * val_max) / (num_pnts - 1);
                rgb_src[2] = mat_int2flt(rgb[2], val_max);
                rgb_src_lin[2] = cs_nlin_to_lin(&ptr_gamut_map->color_space_src, rgb_src[2]);

                gm_rgb_to_rgb(ptr_gamut_map, rgb_src_lin, rgb_dst_lin);
                cs_lin_to_nlin_rgb(&ptr_gamut_map->color_space_dst, rgb_dst_lin, rgb_dst);
                cs_flt2short_rgb(rgb_dst, &ptr_3dlut_rgb[index], val_max);
                index += 3;

            }
        }
    }
}

void gm_gen_map(struct s_gamut_map* ptr_gamut_map, int update_msk)
{
    if (ptr_gamut_map->gamut_map_mode == EGMM_TM_CHTO)
        if (update_msk & (GM_UPDATE_SRC | GM_UPDATE_DST)) {
            MATFLOAT* ptr_org13_factor = (MATFLOAT*)ptr_gamut_map->ptr_func_alloc(ptr_gamut_map->num_hue_pnts * 2 * sizeof(MATFLOAT),
                ptr_gamut_map->memory_context);
            int nh;

            gm_genorg13_factor(ptr_gamut_map, ptr_org13_factor);
            #ifdef GM_SIM
            #pragma omp parallel for num_threads(10)
            #endif
            for (nh = 0; nh < ptr_gamut_map->num_hue_pnts; nh++) {
                /* generate origin 2 and 3 points per hue slice */
                gm_genorigin23_hue(ptr_gamut_map, ptr_org13_factor, nh);
            }

            ptr_gamut_map->ptr_func_free(ptr_org13_factor, ptr_gamut_map->memory_context);
        }

    if ((ptr_gamut_map->gamut_map_mode > EGMM_TM) && (ptr_gamut_map->map_type != EMT_RAD))
        if (update_msk & GM_UPDATE_DST) {
            int nh;

            #ifdef GM_SIM
            #pragma omp parallel for num_threads(10)
            #endif
            for (nh = 0; nh < ptr_gamut_map->num_hue_pnts; nh++){
                /* generate GBD per hue slice */
                gm_gen_edge_hue(ptr_gamut_map, nh);
            }
        }
}

void gm_rgb_to_itp(struct s_color_space* ptr_color_space, MATFLOAT rgb_inp[3], MATFLOAT itp_out[3], int en_pq_lut)
{    /* output may be the same as input */
    MATFLOAT lms[3];
    int nc;

    mat_eval_3x3(ptr_color_space->mat_rgb2lms, rgb_inp, lms);
    for (nc = 0; nc < 3; nc++)
        lms[nc] = en_pq_lut ? gm_pq_lut(lms[nc], EGD_LIN_2_NONLIN) :
        cs_gamma_pq(lms[nc], EGD_LIN_2_NONLIN);
    mat_eval_3x3(ptr_color_space->mat_lms2itp, lms, itp_out);
}

void gm_itp_to_rgb(struct s_color_space* ptr_color_space, MATFLOAT itp_inp[3], MATFLOAT rgb_out[3], int en_pq_lut)
{    /* output may be the same as input */
    MATFLOAT lms[3];
    int nc;

    mat_eval_3x3(ptr_color_space->mat_itp2lms, itp_inp, lms);
    for (nc = 0; nc < 3; nc++)
        lms[nc] = en_pq_lut ? gm_pq_lut(lms[nc], EGD_NONLIN_2_LIN) :
        cs_gamma_pq(lms[nc], EGD_NONLIN_2_LIN);
    mat_eval_3x3(ptr_color_space->mat_lms2rgb, lms, rgb_out);
}

int gm_is_valid_itp(struct s_color_space* ptr_color_space, MATFLOAT itp[3], int en_pq_lut)
{
    MATFLOAT rgb[3];

    gm_itp_to_rgb(ptr_color_space, itp, rgb, en_pq_lut);

    return cs_is_valid_rgb(rgb, ptr_color_space->luminance_limits[0], ptr_color_space->luminance_limits[1]);
}

int gm_is_valid_ic(struct s_color_space* ptr_color_space, MATFLOAT pnt_ic[2], MATFLOAT hue_sin_cos[2], int en_pq_lut)
{
    MATFLOAT pnt_itp[3];

    pnt_itp[0] = pnt_ic[0];
    pnt_itp[1] = pnt_ic[1] * hue_sin_cos[1];
    pnt_itp[2] = pnt_ic[1] * hue_sin_cos[0];

    return gm_is_valid_itp(ptr_color_space, pnt_itp, en_pq_lut);
}

void gm_gen_pq_lut(float* ptr_lut, int num_pnts, enum cs_gamma_dir gamma_dir)
{
    int ni;

    if (gamma_dir == EGD_LIN_2_NONLIN) {
        MATFLOAT increment = mat_pow(2.0, -32.0) / 128.0; /* also == pow(2,-39) or pow(2,-32)/128 */
        MATFLOAT value = 0.0;

        for (ni = 0; ni < num_pnts; ni++) {
            ptr_lut[ni] = (float)cs_gamma_pq(value, gamma_dir);
            /* every 128 pts, region changes and delta between pts doubles */
            if ((ni > 0) && (ni % 128 == 0))
                increment *= 2.0;
            value += increment;
        }

    }
    else
        for (ni = 0; ni < num_pnts; ni++)
            ptr_lut[ni] = (float)cs_gamma_pq((MATFLOAT)ni / (MATFLOAT)(num_pnts - 1), gamma_dir);
}

MATFLOAT gm_pq_lut(MATFLOAT val, enum cs_gamma_dir gamma_dir)
{
    static const MATFLOAT gm_inc = 1.0 / (MATFLOAT)((long long)1 << 32);
    MATFLOAT sign = (val < 0.0) ? -1.0 : 1.0;
    MATFLOAT val_abs = MAT_ABS(val);
    MATFLOAT val_out, vec_inp[2], phs;
    int vec_ind[2];

    if (gamma_dir == EGD_LIN_2_NONLIN)
        if (val_abs >= gm_inc) {
            int exp;
            MATFLOAT mantissa = mat_frexp(val_abs, &exp);
            MATFLOAT tmp = (mantissa - 0.5) * 256.0;

            vec_ind[0] = (int)tmp;
            phs = tmp - (MATFLOAT)vec_ind[0];
            vec_ind[0] += (exp + 31) << 7;
            vec_ind[1] = vec_ind[0] + 1;
            if (vec_ind[1] > GM_PQTAB_NUMPNTS - 1)
                vec_ind[1] = GM_PQTAB_NUMPNTS - 1;
            vec_inp[0] = gm_lin2pq[vec_ind[0]];
            vec_inp[1] = gm_lin2pq[vec_ind[1]];
            val_out = mat_linear(vec_inp, phs);
        }
        else
            val_out = gm_lin2pq[0];
    else {
        MATFLOAT tmp = val_abs * (MATFLOAT)(GM_PQTAB_NUMPNTS - 1);
        vec_ind[0] = (int)tmp;
        phs = tmp - (MATFLOAT)vec_ind[0];
        vec_ind[1] = vec_ind[0] + 1;
        if (vec_ind[1] > GM_PQTAB_NUMPNTS - 1)
            vec_ind[1] = GM_PQTAB_NUMPNTS - 1;
        vec_inp[0] = gm_pq2lin[vec_ind[0]];
        vec_inp[1] = gm_pq2lin[vec_ind[1]];
        val_out = mat_linear(vec_inp, phs);
    }

    return val_out * sign;
}

int gm_seg_intersection(MATFLOAT p0_xy[2], MATFLOAT p1_xy[2], MATFLOAT s1_xy[2],
    MATFLOAT p2_xy[2], MATFLOAT p3_xy[2], MATFLOAT p_xy[2])
{
    MATFLOAT s2_x = p3_xy[0] - p2_xy[0];
    MATFLOAT s2_y = p3_xy[1] - p2_xy[1];
    MATFLOAT denom = -s2_x * s1_xy[1] + s1_xy[0] * s2_y;
    MATFLOAT s0_x, s0_y, s, t;

    if (denom == 0.0)
        return 0; /* no collision */

    s0_x = p0_xy[0] - p2_xy[0];
    s0_y = p0_xy[1] - p2_xy[1];

    s = (-s1_xy[1] * s0_x + s1_xy[0] * s0_y) / denom;
    if ((s < 0.0) || (s > 1.0))
        return 0; /* no collision */

    t = (s2_x * s0_y - s2_y * s0_x) / denom;
    if ((t < 0.0) || (t > 1.0))
        return 0; /* no collision */

    /* collision detected */
    p_xy[0] = p0_xy[0] + (t * s1_xy[0]);
    p_xy[1] = p0_xy[1] + (t * s1_xy[1]);

    return 1;
}