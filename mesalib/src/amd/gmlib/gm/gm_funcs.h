/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : gm_funcs.h
 * Purpose    : Gamut Mapping functions
 * Author     : Vladimir Lachine (vlachine@amd.com)
 * Date       : November 11, 2024
 * Version    : 3.1
 *----------------------------------------------------------------------
 *
 */

#pragma once

#include "mat_funcs.h"
#include "cs_funcs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GM_NUM_PRIM         6           /* number of primary/secondary colors */
#define GM_NUM_HUE          360         /* default number of hue slices in edge description grid */
#define GM_NUM_EDGE         181         /* default number of egde points per hue in edge description grid */
#define GM_NUM_INT          33          /* default number of intensity levels in HueRot grid */
#define GM_STEP_SAMP        0.0001      /* default accuracy of edge detection procedures (for 14 bits signal) */
#define GM_EDGE_ORG         0.5         /* default center point for edge description procedure */
#define GM_ORG1_FACTOR      0.5         /* Origin1 default intensity */
#define GM_ORG3_FACTOR      1.0         /* Origin3 default intensity */
#define GM_ORG2_PERC        0.9

#define GM_CUSP_ADJUST      0x01        /* Adjust cusp points */
#define GM_ZONE1_FLEX       0x02        /* Flexible zone 1 */
#define GM_PQTAB_3DLUT      0x04
#define GM_PQTAB_GBD        0x08
#define GM_SCALE_LUMA       0x04        /* Luma scaling */

#define GM_UPDATE_SRC       0x01
#define GM_UPDATE_DST       0x02

#define GM_HUE_SHIFT        0x01
#define GM_CHROMA_GAIN      0x02

#define GM_PQTAB_NUMPNTS    4097

enum gm_gamut_map_mode {
    EGMM_NONE = 0,       /* NONE */
    EGMM_TM = 1,         /* Tone Map (BT2390-4) */
    EGMM_TM_CHTO = 2,    /* Tone Map + CHTO (Constant Hue Triple Origin */
    EGMM_TM_CHSO = 3,    /* Tone Map + CHSO (Constant Hue Single Origin */
    EGMM_TM_CHCI = 4     /* Tone Map + CHCI (Constant Hue Constant Intensity) */
};

enum gm_hue_rot_mode {
    EHRM_NONE = 0,       /* NONE */
    EHRM_HR = 1,         /* Hue rotation */
    EHRM_CC = 2,         /* Chroma compression */
    EHRM_HR_CC = 3       /* Hue rotation + Chroma compression */
};

enum gm_map_type {
    EMT_SEG = 0,         /* intensity segment */
    EMT_RAD = 1,         /* arc segment */
    EMT_SEGRAD = 2       /* hybrid */
};

enum gm_edge_type {
    EET_RAD = 0,         /* elevation angle uniform */
    EET_CHROMA = 1       /* intensity uniform */
};

enum gm_show_pix_mode {
    ESPM_NONE = 0,       /* NONE */
    ESPM_NOMAP = 1,      /* Show pixels inside gamut */
    ESPM_MAP = 2,        /* Show pixels outside gamut */
    ESPM_MAPZ1 = 3,      /* Show pixels outside gamut in zone1 */
    ESPM_MAPZ2 = 4,      /* Show pixels outside gamut in zone2 */
    ESPM_MAPZ3 = 5,      /* Show pixels outside gamut in zone3 */
    ESPM_NUMZ = 6,       /* Show pixels zone number  */
    ESPM_HUEINP = 7,     /* Show input pixels with hue in range */
    ESPM_HUEOUT = 8      /* Show output pixels with hue in range */
};

struct s_gamut_map {
    /* input parameters */
    enum gm_gamut_map_mode    gamut_map_mode;
    /* Gamut Map Mode: 0 - no gamut map, 1 - Tone Map BT2390-4, 2 - TM+CHTO, 3 - TM+CHSO, 4 - TM+CHCI */
    enum gm_hue_rot_mode      hue_rot_mode;
    /* Hue Rotation Mode: 0 - none, 1 - hue rotation, 2 - chroma compression, 3 - hue rotation and chroma compression */
    int                       en_tm_scale_color;
    /* Enable/Disable Color Scaling in Tone Mapping mode only: {0,1} = 1    */
    unsigned int              mode;
    /* Reserved for modifications of the Gamut Map algo */
    struct s_color_space      color_space_src;
    /* Source color space (primary RGBW chromaticity, gamma, and Luminance min/max) */
    struct s_color_space      color_space_dst;
    /* Destination color space (primary RGBW chromaticity, gamma and Luminance min/max) */
    /* CHTO input tuning parameters */
    MATFLOAT                  org2_perc_c;
    /* Origin2 percentage gap for chroma [0.0,1.0] = 0.9 */
    MATFLOAT                  vec_org1_factor[GM_NUM_PRIM];
    /* Factor of Origin1 for M,R,Y,G,C,B [0.0,2.0] = 1.3, 1.3, 1.3, 1.3, 1.2, 1.0 */
    MATFLOAT                  vec_org3_factor[GM_NUM_PRIM];
    /* Factor of Origin3 for M,R,Y,G,C,B [1.0,1.5] = 1.05, 1.2, 1.05, 1.05, 1.01, 1.05 */
    /* GM input tuning parameters */
    int                       num_hue_pnts;
    /* Number of hue grid points: [90,360]=360 */
    int                       num_edge_pnts;
    /* Number of edge IC grid points: [91, 181] = 181 */
    int                       num_int_pnts;
    /* Number of intensity grid points for primary hues: [5,33] = 33 */
    enum gm_edge_type         edge_type;/* Edge type: {0,1} = 0 : 0 - radius based EET_RAD, 1 - chroma based EET_CHROMA */
    enum gm_map_type          map_type;
    /* Map type: {0,1,2} = 0 : 0 - segments intersection SEG, 1 - radius sampling RAD, 2 hybrid - SEG+RAD */
    MATFLOAT                  step_samp;
    /* Sampling precision in IC space for edge search [0.00001,0.001]=0.0001 */
    int                       reserve;
    /* Reserved for debugging purpose */
    enum gm_show_pix_mode     show_pix_mode;
    /* SHow Pix Mode: [0,8]=0 : show pixel debugging mode */
    MATFLOAT                  show_pix_hue_limits[2];    /* Show Pixel mode hue ranges */
    /* calculated variables */
    MATFLOAT                  lum_min;
    /* minLum (BT2390-4) in PQ non-linear space */
    MATFLOAT                  lum_max;
    /* maxLum (BT2390-4) in PQ non-linear space */
    MATFLOAT                  vec_prim_src_ich[3 * GM_NUM_PRIM];
    /* ich for M,R,Y,G,C,B primaries of source gamut */
    MATFLOAT                  vec_prim_dst_ich[3 * GM_NUM_PRIM];
    /* ich for M,R,Y,G,C,B primaries of target gamut */
    MATFLOAT                  *ptr_cusp_src_ic;
    /* Intensity and chroma of Cusp num_hue_pnts points for source gamut */
    MATFLOAT                  *ptr_cusp_dst_ic;
    /* Intensity and chroma of Cusp num_hue_pnts points for target gamut */
    MATFLOAT                  *ptr_org2_ic;
    /* Intensity and chroma of Origin2 for num_hue_pnts points */
    MATFLOAT                  *ptr_org3_ic;
    /* Intensity and chroma of Origin3 for num_hue_pnts points */
    MATFLOAT                  *ptr_hr_src_hc;
    /* Source Primary Hue and Chroma for (GM_NUM_PRIM * num_int_pnts) points */
    MATFLOAT                  *ptr_hr_dst_hc;
    /* Target Primary Hue and Chroma for (GM_NUM_PRIM * num_int_pnts) points */
    MATFLOAT                  *ptr_edge_ic;
    /* Target gamut edge for (num_hue_pnts * num_edge_pnts) points */
    void                      *(*ptr_func_alloc)(unsigned int, void*);
    /* allocate memory function */
    void                     (*ptr_func_free)(void*, void*);
    /* deallocate memory function */
    void*                    memory_context;
    /*memory management context*/
    MATFLOAT                 hue_max;
    MATFLOAT                 org1;
    MATFLOAT                 org3;
    /* internally calculated constant */
};

void gm_ctor(struct s_gamut_map *ptr_gamut_map,
    void*(*ptr_func_alloc)(unsigned int, void*),
    void(*ptr_func_free)(void*, void*),
    void* mem_context); /* constructor */
void gm_dtor(struct s_gamut_map *ptr_gamut_map);    /* destructor */
void gm_alloc_mem(struct s_gamut_map *ptr_gamut_map);
void gm_free_mem(struct s_gamut_map *ptr_gamut_map);

/* initialization functions */
void gm_set_def(struct s_gamut_map *gamut_map);
int gm_init_gamuts(struct s_gamut_map *ptr_gamut_map, struct s_cs_opts *ptr_cs_opts_src,
        struct s_cs_opts *ptr_cs_opts_dst, unsigned int gm_mode, int update_msk);
int gm_check_gamut(struct s_gamut_map *ptr_gamut_map);
void gm_gencusp_ic(struct s_gamut_map *ptr_gamut_map, int color_space);    /* color_space : 0 - source, 1 - target */

/* gamut map description generation functions */
void gm_gen_edge_hue(struct s_gamut_map* ptr_gamut_map, int hue_ind);

/* resampling functions */
void gm_resample_hc(MATFLOAT vec_ich_inp[][3], MATFLOAT *ptr_hc_out,
        int num_int_pnts_src, int num_int_pnts_dst);
void gm_resample_hue_ic(MATFLOAT *ptr_hue, MATFLOAT *ptr_ic_inp,
        MATFLOAT *ptr_ic_out, int num_hue_pnts_inp, int num_hue_pnts_out);
void gm_genprim_hc(struct s_color_space *ptr_color_space, MATFLOAT *ptr_hr_hc,
        int num_int_pnts, MATFLOAT luma_limits[3], MATFLOAT lum_min, MATFLOAT lum_max);

/* Origin2 and Origin3 generation functions */
void gm_genorg13_factor(struct s_gamut_map* ptr_gamut_map, MATFLOAT* ptr_org13_factor);
void gm_genorigin23_hue(struct s_gamut_map* ptr_gamut_map, MATFLOAT* ptr_org13_factor, int hue_ind);
void gm_getorigin23(struct s_color_space* ptr_color_space_src, struct s_color_space* ptr_color_space_dst,
    MATFLOAT hue, MATFLOAT org_13_factor[2], MATFLOAT org2_perc_c,MATFLOAT cusp_ic_src[2],
    MATFLOAT cusp_ic_dst[2], MATFLOAT origin2_ic[2], MATFLOAT origin3_ic[2], int en_pq_lut);

/* gamut map functions */
int gm_rgb_to_rgb(struct s_gamut_map *ptr_gamut_map, MATFLOAT rgb_inp[3], MATFLOAT rgb_out[3]);
MATFLOAT gm_tm_itp(MATFLOAT itp_inp[3], MATFLOAT itp_out[3], MATFLOAT luma_limits[3],
    MATFLOAT lum_min, MATFLOAT lum_max, int en_tm_scale_color, int en_tm_scale_luma); 
MATFLOAT gm_tm_luma(MATFLOAT luma, MATFLOAT luma_limits[3], MATFLOAT lum_min, MATFLOAT lum_max);
MATFLOAT gm_scale_luma(MATFLOAT luma, MATFLOAT luma_limits[3], MATFLOAT lum_min, MATFLOAT lum_max);
int gm_map_itp(struct s_gamut_map *ptr_gamut_map, MATFLOAT itp_inp[3], MATFLOAT itp_out[3]);
int gm_map_chto_itp(struct s_gamut_map *ptr_gamut_map, MATFLOAT itp_inp[3], MATFLOAT itp_out[3]);
int gm_map_chso_itp(struct s_gamut_map *ptr_gamut_map, MATFLOAT itp_inp[3], MATFLOAT itp_out[3]);
int gm_map_chci_itp(struct s_gamut_map *ptr_gamut_map, MATFLOAT itp_inp[3], MATFLOAT itp_out[3]);

/* hue rotation functions */
void gm_hr_itp(struct s_gamut_map *gamut_map, MATFLOAT itp_inp[3], MATFLOAT itp_out[3], int direction);
void gm_hr_ich(struct s_gamut_map *ptr_gamut_map, MATFLOAT ich_inp[3], MATFLOAT ich_out[3], int direction);
void gm_get_hr_parms(MATFLOAT ich[3], MATFLOAT luma_limits[3], MATFLOAT *ptr_hr_src_hc,
        MATFLOAT *ptr_hr_dst_hc, int num_int_pnts, MATFLOAT rot_hs_cg[2]);

/* segments intersection functions */
int gm_map_seg_itp(struct s_gamut_map *ptr_gamut_map, MATFLOAT itp_inp[3],
        MATFLOAT itp_out[3], int zone, MATFLOAT origin2_ic[2], MATFLOAT origin3_ic[2], int vec_hue_ind[2], MATFLOAT hue_phs);
int gm_map_rad_itp(struct s_gamut_map *ptr_gamut_map, MATFLOAT itp_inp[3],
        MATFLOAT itp_out[3], int zone, MATFLOAT origin2_ic[2], MATFLOAT origin3_ic[2], MATFLOAT hue);
int gm_map_segrad_itp(struct s_gamut_map *ptr_gamut_map, MATFLOAT itp_inp[3],
        MATFLOAT itp_out[3], int zone, MATFLOAT origin2_ic[2],
        MATFLOAT origin3_ic[2], MATFLOAT hue, int vec_hue_ind[2], MATFLOAT hue_phs);

/* interpolate Ic between two hues */
MATFLOAT gm_hue_to_index_phase(MATFLOAT hue, MATFLOAT hue_max, int num_hue_pnts, int vec_hue_ind[2]);
void gm_interp_ic(int vec_hue_ind[2], MATFLOAT hue_phs,
        MATFLOAT vec_pnt_ic[], MATFLOAT pnt_ic[2]);
void gm_getseg_ic(int vec_hue_ind[2], MATFLOAT hue_phs,
        int ind, int num_edge_pnts, MATFLOAT *ptr_edge_ic, MATFLOAT pnt_ic[2]);

/* Edge generation functions */
void gm_genedge(struct s_color_space *ptr_color_space, MATFLOAT luma_limits[3],
        int num_edge_pnts, enum gm_edge_type edge_type, MATFLOAT step_samp, MATFLOAT hue,
    MATFLOAT *ptr_edge_ic, int en_pq_lut);
void gm_genedge_int(struct s_color_space *ptr_color_space, MATFLOAT luma_limits[3],
        int num_edge_pnts, MATFLOAT hue, MATFLOAT step_samp, MATFLOAT *ptr_edge_ic,
        int en_pq_lut);
void gm_genedge_rad(struct s_color_space *ptr_color_space, MATFLOAT luma_limits[3],
        int num_edge_pnts, MATFLOAT hue, MATFLOAT step_samp, MATFLOAT *ptr_edge_ic,
    int en_pq_lut);
void gm_sample_edge_ic(struct s_color_space *ptr_color_space,
        MATFLOAT hue_cos_sin[2], MATFLOAT inc_ic[2], MATFLOAT pnt_ic[2],
    int en_pq_lut);
void gm_edgecusp_adjust(MATFLOAT *ptr_edge_ic, int num_edge_pnts, MATFLOAT cusp_ic[2]);

/* Gamut Map related functions */
int gm_get_zone(MATFLOAT itp[3], MATFLOAT origin2_ic[2], MATFLOAT origin3_ic[2], MATFLOAT luma_limits[3]);
int gm_map_zone1_seg(MATFLOAT itp_inp[3], MATFLOAT itp_out[3], int vec_hue_ind[2],
        MATFLOAT hue_phs, MATFLOAT origin2_ic[2], int num_edge_pnts, MATFLOAT *ptr_edge_ic, int pnt_map, int pnt_inc);
int gm_map_zone2_seg(MATFLOAT itp_inp[3], MATFLOAT itp_out[3], int vec_hue_ind[2],
        MATFLOAT hue_phs, MATFLOAT origin2_ic[2], int num_edge_pnts, MATFLOAT *ptr_edge_ic, int pnt_map, int pnt_inc);
int gm_map_zone3_seg(MATFLOAT itp_inp[3], MATFLOAT itp_out[3], int vec_hue_ind[2],
        MATFLOAT hue_phs, MATFLOAT origin3_ic[2], int num_edge_pnts, MATFLOAT *ptr_edge_ic, int pnt_map, int pnt_inc);
void gm_map_zone1_rad(struct s_color_space *ptr_color_space, MATFLOAT itp_inp[3],
        MATFLOAT itp_out[3], MATFLOAT step_samp, MATFLOAT origin2_ic[2], MATFLOAT hue, int num_itr);
void gm_map_zone2_rad(struct s_color_space *ptr_color_space, MATFLOAT itp_inp[3],
        MATFLOAT itp_out[3], MATFLOAT step_samp, MATFLOAT origin2_ic[2], MATFLOAT hue, int num_itr);
void gm_map_zone3_rad(struct s_color_space *ptr_color_space, MATFLOAT itp_inp[3],
        MATFLOAT itp_out[3], MATFLOAT step_samp, MATFLOAT origin3_ic[2], MATFLOAT hue, int num_itr);

/* Show Pixel debugging functions */
void gm_show_pix(int zone, MATFLOAT itp_src[3], MATFLOAT itp_dst[3],
    MATFLOAT rgb[3], enum gm_show_pix_mode show_pix_mode, MATFLOAT hue_limits[2]);

void gm_rgb_to_itp(struct s_color_space* ptr_color_space, MATFLOAT rgb_inp[3], MATFLOAT itp_out[3], int en_pq_lut);
void gm_itp_to_rgb(struct s_color_space* ptr_color_space, MATFLOAT itp_inp[3], MATFLOAT rgb_out[3], int en_pq_lut);

int gm_is_valid_itp(struct s_color_space* ptr_color_space, MATFLOAT itp[3], int en_pq_lut);
int gm_is_valid_ic(struct s_color_space* ptr_color_space, MATFLOAT pnt_ic[2], MATFLOAT hue_sin_cos[2], int en_pq_lut);

void gm_gen_pq_lut(float* ptr_lut, int num_pnts, enum cs_gamma_dir gamma_dir);
MATFLOAT gm_pq_lut(MATFLOAT val, enum cs_gamma_dir gamma_dir);
int gm_seg_intersection(MATFLOAT p0_xy[2], MATFLOAT p1_xy[2], MATFLOAT s1_xy[2],
    MATFLOAT p2_xy[2], MATFLOAT p3_xy[2], MATFLOAT p_xy[2]);


/* MULTI-THREADING */
/* for multi-threading implementation the following function must be overwritten */
void gm_gen_map(struct s_gamut_map* ptr_gamut_map, int update_msk);
void gm_gen_3dlut(struct s_gamut_map* ptr_gamut_map, int num_pnts,
    int bitwidth, int en_merge, unsigned short* ptr_3dlut_rgb);
/* end MULTI-THREADING */

/* global constants */
static const MATFLOAT gm_vec_org13_factor_def[GM_NUM_PRIM][2] = {
    {1.3, 1.05},    /* M */
    {1.3, 1.10},    /* R */
    {1.3, 1.10},    /* Y */
    {1.3, 1.05},    /* G */
    {1.2, 1.01},    /* C */
    {1.0, 1.06}     /* B */
};

static const MATFLOAT gm_vec_cusp_rgb[GM_NUM_PRIM][3] = {
    {1.0, 0.0, 1.0},    /* M */
    {1.0, 0.0, 0.0},    /* R */
    {1.0, 1.0, 0.0},    /* Y */
    {0.0, 1.0, 0.0},    /* G */
    {0.0, 1.0, 1.0},    /* C */
    {0.0, 0.0, 1.0}     /* B */
};

#ifdef __cplusplus
}
#endif
