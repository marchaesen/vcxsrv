/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : mat_funcs.h
 * Purpose    : Mathematical functions
 * Author     : Vladimir Lachine (vlachine@amd.com)
 * Date       : September 20, 2023
 * Version    : 1.2
 */

#pragma once

#ifdef __cplusplus
    extern "C" {
#endif

#define MATFLOAT double

/* precision for matrix inversion */
#define PRECISION_LIMIT (1.0e-15)

/* absolute value of a */
#define MAT_ABS(a)        (((a) < 0) ? -(a) : (a))

/* find minimum of a and b */
#define MAT_MIN(a, b)        (((a) < (b)) ? (a) : (b))

/* find maximum of a and b */
#define MAT_MAX(a, b)        (((a) > (b)) ? (a) : (b))

/* clip to range */
#define MAT_CLAMP(v, l, h)    ((v) < (l) ? (l) : ((v) > (h) ? (h) : v))

/* round a to nearest int */
#define MAT_ROUND(a)        (int)((a) + 0.5f)

/* take sign of a, either -1, 0, or 1 */
#define MAT_ZSGN(a)        (((a) < 0) ? -1 : (a) > 0 ? 1 : 0)

/* take binary sign of a, either -1, or 1 if >= 0 */
#define MAT_SGN(a)        (((a) < 0) ? -1 : 1)

/* swap a and b (see Gem by Wyvill) */
#define MAT_SWAP(a, b)    { a ^ = b; b ^ = a; a ^= b; }

/* linear interpolation from l (when a=0) to h (when a=1) */
/* (equal to (a*h)+((1-a)*l) */
#define MAT_LERP(a, l, h)    ((l) + (((h) - (l)) * (a)))

/* vector operations */
void mat_eval_3x3(MATFLOAT mat[3][3], MATFLOAT vec_inp[3], MATFLOAT vec_out[3]);
void mat_eval_3x3_off(MATFLOAT mat[3][3], MATFLOAT vec_off[3], MATFLOAT vec_inp[3], MATFLOAT vec_out[3]);
void mat_eval_off_3x3_off(MATFLOAT vec_off_inp[3], MATFLOAT mat[3][3],
    MATFLOAT vec_off_out[3], MATFLOAT vec_inp[3], MATFLOAT vec_out[3]);
void mat_mul3x3(MATFLOAT mat2[3][3], MATFLOAT mat1[3][3], MATFLOAT mat2x1[3][3]);
int mat_inv3x3(MATFLOAT mat_inp[3][3], MATFLOAT mat_out[3][3]);

void mat_3x1_zero(MATFLOAT vec_out[3]);
void mat_3x3_zero(MATFLOAT mat_out[3][3]);
void mat_3x3_unity(MATFLOAT mat_out[3][3]);
void mat_copy3x3(MATFLOAT mat_inp[3][3], MATFLOAT mat_out[3][3]);

int mat_round(MATFLOAT val);

MATFLOAT mat_int2flt(int val, int val_max);
int mat_flt2int(MATFLOAT val, int val_max);

void mat_gen_mat_off(MATFLOAT mat_inp[3][3], MATFLOAT vec_off_inp[3],
    MATFLOAT vec_off_out[3], MATFLOAT mat_res[3][3], MATFLOAT vec_off_res[3]);
void mat_scl_off(MATFLOAT vec_off_inp[3], MATFLOAT vec_off_out[3], int bitwidth);
void mat_cvt_cs(int vec_inp[3], int vec_out[3], int bitwidth, MATFLOAT mat[3][3], MATFLOAT vec_off[3], int is_clip);

MATFLOAT mat_norm_angle(MATFLOAT angle);

MATFLOAT mat_clamp(MATFLOAT val_inp, MATFLOAT val_min, MATFLOAT val_max);
int mat_is_valid(MATFLOAT val_inp, MATFLOAT val_min, MATFLOAT val_max);
int mat_is_valid_vec(MATFLOAT val_inp[], int size, MATFLOAT val_min, MATFLOAT val_max);
int mat_is_number(MATFLOAT val);
MATFLOAT mat_norm(MATFLOAT val_inp, MATFLOAT val_min, MATFLOAT val_rng);
MATFLOAT mat_denorm(MATFLOAT val_inp, MATFLOAT val_min, MATFLOAT val_rng);

void mat_copy(MATFLOAT vec_inp[], MATFLOAT vec_out[], int size);
void mat_set(MATFLOAT val_inp, MATFLOAT vec_out[], int size);

int mat_flt_to_index(MATFLOAT val_inp, MATFLOAT val_max, int num_pnts);
MATFLOAT mat_index_to_flt(int index, MATFLOAT val_max, int num_pnts);
MATFLOAT mat_flt_to_index_phase(MATFLOAT val_inp, MATFLOAT val_max, int num_pnts, int vec_ind[2]);
MATFLOAT mat_vec_to_index_phase(MATFLOAT val_inp, MATFLOAT vec_val[], int num_pnts, int vec_ind[2]);

int mat_int_to_index(int val_inp, int val_max, int num_indexes);
int mat_index_to_int(int index, int val_max, int num_indexes);
MATFLOAT mat_int_to_index_phase(int val_inp, int val_max, int num_indexes, int vec_val_ind[2]);
int mat_get_hue_index_2pi(MATFLOAT vec_hue[], int num_hue_pnts);
MATFLOAT mat_hue_to_index_phase(MATFLOAT val_inp, int num_hue_pnts,
    MATFLOAT vec_val[], MATFLOAT val_max, int index_max, int vec_ind_out[2]);

int mat_seg_intersection(MATFLOAT p0_xy[2], MATFLOAT p1_xy[2],
    MATFLOAT p2_xy[2], MATFLOAT p3_xy[2], MATFLOAT p_xy[2]);

MATFLOAT mat_linear(MATFLOAT vec_inp[2], MATFLOAT phs);
MATFLOAT mat_bilinear(MATFLOAT vec_inp[2][2], MATFLOAT vec_phs[2]);
MATFLOAT mat_trilinear(MATFLOAT vec_inp[2][2][2], MATFLOAT vec_phs[3]);
MATFLOAT mat_tetra(MATFLOAT vec_inp[2][2][2], MATFLOAT vec_phs[3]);
MATFLOAT mat_cubic(MATFLOAT vec_inp[4], MATFLOAT phs);

MATFLOAT mat_mse(MATFLOAT val1[], MATFLOAT val2[], int size);
MATFLOAT mat_sshape(MATFLOAT val, MATFLOAT gamma);
MATFLOAT mat_get_pi(void);

MATFLOAT mat_angle(MATFLOAT y, MATFLOAT x);
MATFLOAT mat_radius(MATFLOAT y, MATFLOAT x);
MATFLOAT mat_radius_vec(MATFLOAT val[], MATFLOAT org[], int size);
void mat_gain_vec(MATFLOAT vec_inp[], MATFLOAT vec_out[], MATFLOAT vec_org[], int size, MATFLOAT gain);

MATFLOAT mat_pow(MATFLOAT val0, MATFLOAT val1);
MATFLOAT mat_atan2(MATFLOAT y, MATFLOAT x);
MATFLOAT mat_cos(MATFLOAT val);
MATFLOAT mat_sin(MATFLOAT val);
MATFLOAT mat_sqrt(MATFLOAT val);
MATFLOAT mat_log(MATFLOAT val);
MATFLOAT mat_log2(MATFLOAT val);
MATFLOAT mat_log10(MATFLOAT val);
MATFLOAT mat_frexp(MATFLOAT val, int *exponent);

#ifndef GM_MAT_MATH
float mat_fast_rsqrt(float val);
float mat_fast_exp(float x);
#endif

MATFLOAT mat_exp(MATFLOAT val);

enum mat_order_3dlut {
    MAT_ORDER_RGB = 0,
    MAT_ORDER_BGR = 1
};

unsigned int mat_index_3dlut(int ind_r, int ind_g, int ind_b, int num_pnts, enum mat_order_3dlut order);

#ifdef __cplusplus
}
#endif
