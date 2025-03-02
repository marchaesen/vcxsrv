/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: MIT
 *
 *----------------------------------------------------------------------
 * File Name  : mat_funcs.c
 * Purpose    : Mathematical functions
 * Author     : Vladimir Lachine (vlachine@amd.com)
 * Date       : September 20, 2023
 * Version    : 1.2
 *----------------------------------------------------------------------
 */

#ifndef GM_SIM
#pragma code_seg("PAGED3PC")
#pragma data_seg("PAGED3PD")
#pragma const_seg("PAGED3PR")
#endif

#include "mat_funcs.h"
#include <math.h>

float mat_fast_log(float x);

void mat_eval_3x3(MATFLOAT mat[3][3], MATFLOAT vec_inp[3], MATFLOAT vec_out[3])
{
    int ni, nj;

    mat_3x1_zero(vec_out);
    for (ni = 0; ni < 3; ni++)
        for (nj = 0; nj < 3; nj++)
            vec_out[ni] += mat[ni][nj] * vec_inp[nj];
}

void mat_eval_3x3_off(MATFLOAT mat[3][3], MATFLOAT vec_off[3], MATFLOAT vec_inp[3], MATFLOAT vec_out[3])
{
    int nc;

    mat_eval_3x3(mat, vec_inp, vec_out);
    for (nc = 0; nc < 3; nc++)
        vec_out[nc] += vec_off[nc];
}

void mat_eval_off_3x3_off(MATFLOAT vec_off_inp[3], MATFLOAT mat[3][3],
    MATFLOAT vec_off_out[3], MATFLOAT vec_inp[3], MATFLOAT vec_out[3])
{
    MATFLOAT val_tmp[3];
    int nc;

    for (nc = 0; nc < 3; nc++)
        val_tmp[nc] = vec_inp[nc] + vec_off_inp[nc];
    mat_eval_3x3(mat, val_tmp, vec_out);
    for (nc = 0; nc < 3; nc++)
        vec_out[nc] += vec_off_out[nc];
}

void mat_mul3x3(MATFLOAT mat2[3][3], MATFLOAT mat1[3][3], MATFLOAT mat2x1[3][3])
{
    int ni, nj, nk;

    mat_3x3_zero(mat2x1);
    for (ni = 0; ni < 3; ni++)
        for (nj = 0; nj < 3; nj++)
            for (nk = 0; nk < 3; nk++)
                mat2x1[ni][nj] += mat2[ni][nk] * mat1[nk][nj];
}

int mat_inv3x3(MATFLOAT mat_inp[3][3], MATFLOAT mat_out[3][3])
{
/*
* Calculate the determinant of matrix A and determine if the
* the matrix is singular as limited by the MATFLOAT precision
* MATFLOATing-point data representation.
*/
    MATFLOAT det = 0.0;
    MATFLOAT pos = 0.0;
    MATFLOAT neg = 0.0;
    MATFLOAT temp;

    temp = mat_inp[0][0] * mat_inp[1][1] * mat_inp[2][2];
    if (temp >= 0.0)
        pos += temp;
    else
        neg += temp;
    temp = mat_inp[0][1] * mat_inp[1][2] * mat_inp[2][0];
    if (temp >= 0.0)
        pos += temp;
    else
        neg += temp;
    temp = mat_inp[0][2] * mat_inp[1][0] * mat_inp[2][1];
    if (temp >= 0.0)
        pos += temp;
    else
        neg += temp;
    temp = -mat_inp[0][2] * mat_inp[1][1] * mat_inp[2][0];
    if (temp >= 0.0)
        pos += temp;
    else
        neg += temp;
    temp = -mat_inp[0][1] * mat_inp[1][0] * mat_inp[2][2];
    if (temp >= 0.0)
        pos += temp;
    else
        neg += temp;
    temp = -mat_inp[0][0] * mat_inp[1][2] * mat_inp[2][1];
    if (temp >= 0.0)
        pos += temp;
    else
        neg += temp;
    det = pos + neg;

    /* Is the submatrix A singular? */
    if ((det == 0.0) || (MAT_ABS(det / (pos - neg)) < PRECISION_LIMIT))
        return 0; /* Matrix M has no mat_inpverse */

    /* Calculate inverse(A) = adj(A) / det(A) */
    mat_out[0][0] =  (mat_inp[1][1] * mat_inp[2][2] - mat_inp[1][2] * mat_inp[2][1]) / det;
    mat_out[1][0] = -(mat_inp[1][0] * mat_inp[2][2] - mat_inp[1][2] * mat_inp[2][0]) / det;
    mat_out[2][0] =  (mat_inp[1][0] * mat_inp[2][1] - mat_inp[1][1] * mat_inp[2][0]) / det;
    mat_out[0][1] = -(mat_inp[0][1] * mat_inp[2][2] - mat_inp[0][2] * mat_inp[2][1]) / det;
    mat_out[1][1] =  (mat_inp[0][0] * mat_inp[2][2] - mat_inp[0][2] * mat_inp[2][0]) / det;
    mat_out[2][1] = -(mat_inp[0][0] * mat_inp[2][1] - mat_inp[0][1] * mat_inp[2][0]) / det;
    mat_out[0][2] =  (mat_inp[0][1] * mat_inp[1][2] - mat_inp[0][2] * mat_inp[1][1]) / det;
    mat_out[1][2] = -(mat_inp[0][0] * mat_inp[1][2] - mat_inp[0][2] * mat_inp[1][0]) / det;
    mat_out[2][2] =  (mat_inp[0][0] * mat_inp[1][1] - mat_inp[0][1] * mat_inp[1][0]) / det;

    return 1;
}

void mat_3x1_zero(MATFLOAT vec_out[3])
{
    int nc;

    for (nc = 0; nc < 3; nc++)
        vec_out[nc] = 0.0;
}

void mat_3x3_zero(MATFLOAT mat_out[3][3])
{
    int ni, nj;

    for (ni = 0; ni < 3; ni++)
        for (nj = 0; nj < 3; nj++)
            mat_out[ni][nj] = 0.0;
}

void mat_3x3_unity(MATFLOAT mat_out[3][3])
{
    int ni, nj;

    for (ni = 0; ni < 3; ni++)
        for (nj = 0; nj < 3; nj++)
            mat_out[ni][nj] = (ni == nj) ? 1.0f : 0.0f;
}

void mat_copy3x3(MATFLOAT mat_inp[3][3], MATFLOAT mat_out[3][3])
{
    int ni, nj;

    for (ni = 0; ni < 3; ni++)
        for (nj = 0; nj < 3; nj++)
            mat_out[ni][nj] = mat_inp[ni][nj];
}

int mat_round(MATFLOAT val)
{
    int sign = MAT_ZSGN(val);
    int val_out = (int)(MAT_ABS(val) + 0.5);

    return sign * val_out;
}

MATFLOAT mat_int2flt(int val, int val_max)
{
    return (MATFLOAT)val / (MATFLOAT)val_max;
}

int mat_flt2int(MATFLOAT val_inp, int val_max)
{
    MATFLOAT val_tmp = val_inp * (MATFLOAT)val_max;
    int val_out = mat_round(val_tmp);

    return MAT_CLAMP(val_out, 0, val_max);
}

void mat_gen_mat_off(MATFLOAT mat_inp[3][3], MATFLOAT vec_off_inp[3],
    MATFLOAT vec_off_out[3], MATFLOAT mat_res[3][3], MATFLOAT vec_off_res[3])
{
    int nc;

    /* construct transform. The 'inoff' is merged into output offset. */
    if (vec_off_out)
        for (nc = 0; nc < 3; nc++)
            vec_off_res[nc] = vec_off_out[nc];
    else
        mat_3x1_zero(vec_off_res);

    if (mat_inp)
        mat_copy3x3(mat_inp, mat_res);
    else
        mat_3x3_unity(mat_res);

    if (vec_off_inp)
        for (nc = 0; nc < 3; nc++)
            vec_off_res[nc] -= (mat_res[nc][0] * vec_off_inp[0] + mat_res[nc][1] *
                    vec_off_inp[1] + mat_res[nc][2] * vec_off_inp[2]);
}

void mat_scl_off(MATFLOAT vec_off_inp[3], MATFLOAT vec_off_out[3], int bitwidth)
{    /* output may be the same as input */
    int nc;

    for (nc = 0; nc < 3; nc++)
        vec_off_out[nc] = vec_off_inp[nc] * (MATFLOAT)(1 << bitwidth);
}

void mat_cvt_cs(int vec_inp[3], int vec_out[3], int bitwidth,
    MATFLOAT mat[3][3], MATFLOAT vec_off[3], int is_clip)
{
    int nc, ni;

    for (nc = 0; nc < 3; nc++) {
        MATFLOAT sum = vec_off[nc];

        for (ni = 0; ni < 3; ni++)
            sum += mat[nc][ni] * (MATFLOAT)vec_inp[ni];
        int nValue = mat_round(sum);
        if (is_clip) {
            const int cnMaxValue = (1 << bitwidth) - 1;

            MAT_CLAMP(nValue, 0, cnMaxValue);
        }
        vec_out[nc] = nValue;
    }
}

MATFLOAT mat_norm_angle(MATFLOAT angle)
{
    MATFLOAT pi2 = 2.0f * mat_get_pi();
    MATFLOAT angle_out = angle;

    if (angle_out < 0.0f)
        angle_out += pi2;
    else if (angle_out >= pi2)
        angle_out -= pi2;

    return angle_out;
}

MATFLOAT mat_clamp(MATFLOAT val_inp, MATFLOAT val_min, MATFLOAT val_max)
{
    return MAT_CLAMP(val_inp, val_min, val_max);
}

int mat_is_valid(MATFLOAT val_inp, MATFLOAT val_min, MATFLOAT val_max)
{
    return ((mat_is_number(val_inp) == 0) || (val_inp < val_min) || (val_inp > val_max)) ? 0 : 1;
}

int mat_is_valid_vec(MATFLOAT vec_inp[], int size, MATFLOAT val_min, MATFLOAT val_max)
{
    int ni;

    for (ni = 0; ni < size; ni++)
        if (mat_is_valid(vec_inp[ni], val_min, val_max) == 0)
            return 0;

    return 1;
}

int mat_is_number(MATFLOAT val)
{    /* Check if this is not NaN */
    return (val == val);
}

MATFLOAT mat_norm(MATFLOAT val_inp, MATFLOAT val_min, MATFLOAT val_rng)
{    /* map to [0.0,1.0] */
    return (val_inp - val_min) / val_rng;
}

MATFLOAT mat_denorm(MATFLOAT val_inp, MATFLOAT val_min, MATFLOAT val_rng)
{    /* map from [0.0,1.0] */
    return val_inp * val_rng + val_min;
}

void mat_copy(MATFLOAT vec_inp[], MATFLOAT vec_out[], int size)
{
    int nc;

    for (nc = 0; nc < size; nc++)
        vec_out[nc] = vec_inp[nc];
}

void mat_set(MATFLOAT val_inp, MATFLOAT vec_out[], int size)
{
    int nc;

    for (nc = 0; nc < size; nc++)
        vec_out[nc] = val_inp;
}

int mat_flt_to_index(MATFLOAT val_inp, MATFLOAT val_max, int num_pnts)
{
    MATFLOAT step = val_max / (MATFLOAT)(num_pnts - 1);

    return (int)(val_inp / step);
}

MATFLOAT mat_index_to_flt(int index, MATFLOAT val_max, int num_pnts)
{
    MATFLOAT step = val_max / (MATFLOAT)(num_pnts - 1);

    return (MATFLOAT)index * step;
}

MATFLOAT mat_flt_to_index_phase(MATFLOAT val_inp, MATFLOAT val_max, int num_pnts, int vec_ind[2])
{
    MATFLOAT step = val_max / (MATFLOAT)(num_pnts - 1);
    MATFLOAT tmp = val_inp / step;

    vec_ind[0] = (int)tmp;
    vec_ind[1] = vec_ind[0] + 1;
    if (vec_ind[1] > num_pnts - 1)
        vec_ind[1] = num_pnts - 1;

    return tmp - (MATFLOAT)vec_ind[0];
}

MATFLOAT mat_vec_to_index_phase(MATFLOAT val_inp, MATFLOAT vec_val[], int num_pnts, int vec_ind[2])
{
    int ind0, ind1;

    /* calculate indexes */
    for (ind0 = num_pnts - 1; ind0 >= 0; ind0--) {
        if (val_inp >= vec_val[ind0])
            break;
    }
    ind1 = MAT_MIN(ind0 + 1, num_pnts - 1);

    vec_ind[0] = ind0;
    vec_ind[1] = ind1;

    return (vec_val[ind0] == vec_val[ind1]) ? 0.0 : (val_inp - vec_val[ind0]) / (vec_val[ind1] - vec_val[ind0]);
}

int mat_int_to_index(int val_inp, int val_max, int num_indexes)
{
    return val_inp * (num_indexes - 1) / val_max;
}

int mat_index_to_int(int index, int val_max, int num_indexes)
{
    return index * val_max / (num_indexes - 1);
}

MATFLOAT mat_int_to_index_phase(int val_inp, int val_max, int num_indexes, int vec_val_ind[2])
{
    MATFLOAT step = (MATFLOAT)val_max / (MATFLOAT)(num_indexes - 1);

    vec_val_ind[0] = mat_int_to_index(val_inp, val_max, num_indexes);
    vec_val_ind[1] = MAT_MIN(vec_val_ind[0] + 1, num_indexes - 1);

    return (val_inp - mat_index_to_int(vec_val_ind[0], val_max, num_indexes)) / step;
}

int mat_get_hue_index_2pi(MATFLOAT vec_hue[], int num_hue_pnts)
{    /* find a point crossing 2PI */
    int index_2pi;

    for (index_2pi = num_hue_pnts - 1; index_2pi >= 1; index_2pi--)
        if (vec_hue[index_2pi] < vec_hue[index_2pi - 1])
            break;

    return index_2pi;
}

MATFLOAT mat_hue_to_index_phase(MATFLOAT val_inp, int num_hue_pnts,
    MATFLOAT vec_val[], MATFLOAT val_max, int index_max, int vec_ind_out[2])
{
    int ind0, ind1;
    MATFLOAT step, delta;

    /* calculate indexes */
    ind1 = index_max;
    while (val_inp >= vec_val[ind1]) {
        ind1 = (ind1 + 1) % num_hue_pnts;
        if (ind1 == index_max)
            break;
    }
    ind0 = (ind1 > 0) ? ind1 - 1 : num_hue_pnts - 1;

    /* calculate phase */
    step = vec_val[ind1] - vec_val[ind0];
    if (step < 0.0)
        step += val_max;
    delta = val_inp - vec_val[ind0];
    if (delta < 0.0)
        delta += val_max;

    vec_ind_out[0] = ind0;
    vec_ind_out[1] = ind1;

    return delta / step;
}

int mat_seg_intersection(MATFLOAT p0_xy[2], MATFLOAT p1_xy[2],
    MATFLOAT p2_xy[2], MATFLOAT p3_xy[2], MATFLOAT p_xy[2])
{
    MATFLOAT s1_x = p1_xy[0] - p0_xy[0];
    MATFLOAT s1_y = p1_xy[1] - p0_xy[1];
    MATFLOAT s2_x = p3_xy[0] - p2_xy[0];
    MATFLOAT s2_y = p3_xy[1] - p2_xy[1];
    MATFLOAT denom = -s2_x * s1_y + s1_x * s2_y;
    MATFLOAT s0_x, s0_y, s, t;

    if (denom == 0.0)
        return 0; /* no collision */

    s0_x = p0_xy[0] - p2_xy[0];
    s0_y = p0_xy[1] - p2_xy[1];

    s = (-s1_y * s0_x + s1_x * s0_y) / denom;
    if ((s < 0.0) || (s > 1.0))
        return 0; /* no collision */

    t = (s2_x * s0_y - s2_y * s0_x) / denom;
    if ((t < 0.0) || (t > 1.0))
        return 0; /* no collision */

    /* collision detected */
    p_xy[0] = p0_xy[0] + (t * s1_x);
    p_xy[1] = p0_xy[1] + (t * s1_y);

    return 1;
}

MATFLOAT mat_linear(MATFLOAT vec_inp[2], MATFLOAT phs)
{
    return vec_inp[0] + (vec_inp[1] - vec_inp[0]) * phs;
}

MATFLOAT mat_bilinear(MATFLOAT vec_inp[2][2], MATFLOAT vec_phs[2])
{
    int ni;
    MATFLOAT vec_tmp[2];

    for (ni = 0; ni < 2; ni++)
        vec_tmp[ni] = mat_linear(vec_inp[ni], vec_phs[0]);

    return mat_linear(vec_tmp, vec_phs[1]);
}

MATFLOAT mat_trilinear(MATFLOAT vec_inp[2][2][2], MATFLOAT vec_phs[3])
{
    int ni;
    MATFLOAT vec_tmp[2];

    for (ni = 0; ni < 2; ni++)
        vec_tmp[ni] = mat_bilinear(vec_inp[ni], vec_phs);

    return mat_linear(vec_tmp, vec_phs[2]);
}

MATFLOAT mat_tetra(MATFLOAT vec_inp[2][2][2], MATFLOAT vec_phs[3])
{
    MATFLOAT fx = vec_phs[2];
    MATFLOAT fy = vec_phs[1];
    MATFLOAT fz = vec_phs[0];
    MATFLOAT vec_c[3];
    MATFLOAT value;
    int nc;

    if (fx > fy) {
        if (fy > fz) { /* T0: x > y > z */
            vec_c[0] = vec_inp[1][0][0] - vec_inp[0][0][0];
            vec_c[1] = vec_inp[1][1][0] - vec_inp[1][0][0];
            vec_c[2] = vec_inp[1][1][1] - vec_inp[1][1][0];
        } else if (fx > fz) { /* T5: x > z > y */
            vec_c[0] = vec_inp[1][0][0] - vec_inp[0][0][0];
            vec_c[1] = vec_inp[1][1][1] - vec_inp[1][0][1];
            vec_c[2] = vec_inp[1][0][1] - vec_inp[1][0][0];
        } else { /* T4: z > x > y */
            vec_c[0] = vec_inp[1][0][1] - vec_inp[0][0][1];
            vec_c[1] = vec_inp[1][1][1] - vec_inp[1][0][1];
            vec_c[2] = vec_inp[0][0][1] - vec_inp[0][0][0];
        }
    } else {
        if (fx > fz) { /* T1: y > x > z */
            vec_c[0] = vec_inp[1][1][0] - vec_inp[0][1][0];
            vec_c[1] = vec_inp[0][1][0] - vec_inp[0][0][0];
            vec_c[2] = vec_inp[1][1][1] - vec_inp[1][1][0];
        } else if (fy > fz) { /* T2: y > z > x */
            vec_c[0] = vec_inp[1][1][1] - vec_inp[0][1][1];
            vec_c[1] = vec_inp[0][1][0] - vec_inp[0][0][0];
            vec_c[2] = vec_inp[0][1][1] - vec_inp[0][1][0];
        } else { /* T3: z > y > x */
            vec_c[0] = vec_inp[1][1][1] - vec_inp[0][1][1];
            vec_c[1] = vec_inp[0][1][1] - vec_inp[0][0][1];
            vec_c[2] = vec_inp[0][0][1] - vec_inp[0][0][0];
        }
    }

    value = vec_inp[0][0][0];
    for (nc = 0; nc < 3; nc++)
        value += vec_c[nc] * vec_phs[2 - nc];

    return MAT_CLAMP(value, 0.0, 1.0);
}

MATFLOAT mat_cubic(MATFLOAT vec_inp[4], MATFLOAT phs)
{
    return vec_inp[1] + 0.5 * phs * (vec_inp[2] - vec_inp[0] +
        phs * (2.0 * vec_inp[0] - 5.0 * vec_inp[1] + 4.0 * vec_inp[2] - vec_inp[3] +
        phs * (3.0 * (vec_inp[1] - vec_inp[2]) + vec_inp[3] - vec_inp[0])));
}

MATFLOAT mat_mse(MATFLOAT val1[], MATFLOAT val2[], int size)
{
    MATFLOAT err = 0.0;
    int nc;

    for (nc = 0; nc < size; nc++) {
        MATFLOAT err_tmp = val1[nc] - val2[nc];

        err += err_tmp * err_tmp;
    }

    return mat_sqrt(err);
}

MATFLOAT mat_sshape(MATFLOAT val, MATFLOAT gamma)
{
    MATFLOAT k = 0.5 * mat_pow(0.5, -gamma);
    MATFLOAT val_out = (val <= 0.5) ? k * mat_pow(val, gamma) : 1.0 - k * mat_pow((1.0 - val), gamma);

    return val_out;
}

MATFLOAT mat_radius_vec(MATFLOAT vec_val[], MATFLOAT vec_org[], int size)
{
    MATFLOAT radius = 0.0;
    int ni;

    for (ni = 0; ni < size; ni++)
        radius += (vec_val[ni] - vec_org[ni]) * (vec_val[ni] - vec_org[ni]);

    return mat_sqrt(radius);
}

void mat_gain_vec(MATFLOAT vec_inp[], MATFLOAT vec_out[], MATFLOAT vec_org[], int size, MATFLOAT gain)
{
    int ni;

    for (ni = 0; ni < 3; ni++)
        vec_out[ni] = vec_org[ni] + (vec_inp[ni] - vec_org[ni]) * gain;
}

MATFLOAT mat_get_pi(void)
{
#ifdef GM_MAT_MATH
    return (MATFLOAT)acos(-1.0);
#else
    return 3.14159265358979323;
#endif
}

MATFLOAT mat_angle(MATFLOAT y, MATFLOAT x)
{
    return mat_norm_angle(mat_atan2(y, x));
}

MATFLOAT mat_radius(MATFLOAT y, MATFLOAT x)
{
    return mat_sqrt(y * y + x * x);
}

MATFLOAT mat_pow(MATFLOAT val0, MATFLOAT val1)
{
    return (MATFLOAT)pow(val0, val1);
}

MATFLOAT mat_atan2(MATFLOAT y, MATFLOAT x)
{
    return (MATFLOAT)atan2(y, x);
}

MATFLOAT mat_cos(MATFLOAT val)
{
    return (MATFLOAT)cos(val);
}

MATFLOAT mat_sin(MATFLOAT val)
{
    return (MATFLOAT)sin(val);
}

MATFLOAT mat_log2(MATFLOAT val)
{
    return (MATFLOAT)(mat_log(val) / mat_log(2.0));
}

MATFLOAT mat_log10(MATFLOAT val)
{
    return (MATFLOAT)(mat_log(val) / mat_log(10.0));
}

MATFLOAT mat_frexp(MATFLOAT val, int *exponent)
{
    return (MATFLOAT)frexp(val, exponent);
}

#ifndef GM_MAT_MATH
static const unsigned char root_recip_table[128] = {
    0x69, 0x66, 0x63, 0x61, 0x5E, 0x5B, 0x59, 0x57, /* for x =(2.0 ... 3.99)*(4^n) */
    0x54, 0x52, 0x50, 0x4D, 0x4B, 0x49, 0x47, 0x45, /* (exponent is even) */
    0x43, 0x41, 0x3F, 0x3D, 0x3B, 0x39, 0x37, 0x36,
    0x34, 0x32, 0x30, 0x2F, 0x2D, 0x2C, 0x2A, 0x28,
    0x27, 0x25, 0x24, 0x22, 0x21, 0x1F, 0x1E, 0x1D,
    0x1B, 0x1A, 0x19, 0x17, 0x16, 0x15, 0x14, 0x12,
    0x11, 0x10, 0x0F, 0x0D, 0x0C, 0x0B, 0x0A, 0x09,
    0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
    0xFE, 0xFA, 0xF6, 0xF3, 0xEF, 0xEB, 0xE8, 0xE4, /* for x =(1.0 ... 1.99)*(4^n) */
    0xE1, 0xDE, 0xDB, 0xD7, 0xD4, 0xD1, 0xCE, 0xCB, /* (exponent is odd) */
    0xC9, 0xC6, 0xC3, 0xC0, 0xBE, 0xBB, 0xB8, 0xB6,
    0xB3, 0xB1, 0xAF, 0xAC, 0xAA, 0xA8, 0xA5, 0xA3,
    0xA1, 0x9F, 0x9D, 0x9B, 0x99, 0x97, 0x95, 0x93,
    0x91, 0x8F, 0x8D, 0x8B, 0x89, 0x87, 0x86, 0x84,
    0x82, 0x80, 0x7F, 0x7D, 0x7B, 0x7A, 0x78, 0x77,
    0x75, 0x74, 0x72, 0x71, 0x6F, 0x6E, 0x6C, 0x6B
};

/*
 * find a reciprocal of square-root of x, using a similar method.
 * an approximation is found, using the 6 MSBs of the mantissa,
 * and the LSB of the exponent.
 * The exponent mapping is a bit tricker than in the RECIPS case:
 * we want
 *    125,126 -> 127
 *    127,128 -> 126
 *    129,130 -> 125
 *    131,132 -> 124
 *
 * So, we can take original exponent, add 131, then >>1, then
 * take the 1's complement.
 * The result is accurate +/- 1 lsb in float precision. I'm not
 * sure exactly what the full range of this is, it should
 * work for any values >0, except for denormals.
 *
 * iterative method:
 * Cavanagh, J. 1984. Digital Computer Arithmetic. McGraw-Hill. Page 278.
 */
float mat_fast_rsqrt(float val)
{
    union {
        float fval;
        unsigned int uval;
    } u;
    unsigned int new_mant;
    float rsqa, rprod;

    u.fval = val;
    u.uval &= 0x7FFFFFFF;        /* can't have sign */
    val = u.fval * 0.5f;

    new_mant = root_recip_table[(u.uval >> 17) & 0x7F];
    /*
     * create modified exponent    ; drop in new mantissa
     */
    u.uval = (~((u.uval + 0x41800000) >> 1) & 0x7F800000) + (new_mant << 15);
    rsqa = u.fval;
    /*
     * note: we could do
     *  rsqa *= 1.5f - rsqa*rsqa * x
     * but there are cases where x is very small
     * (zero or denormal) and rsqa*rsqa could overflow. We generate
     * the wrong answer in these cases, but at least it isn't a NaN.
     */
    rprod = val * rsqa;
    rsqa *= 1.5f - rprod * rsqa;
    rprod = val * rsqa;
    rsqa *= 1.5f - rprod * rsqa;
    rprod = val * rsqa;
    rsqa *= 1.5f - rprod * rsqa;

    return rsqa;
}

#define Declare_Special_Float(cnst) { union { unsigned int ui; float f; } u; u.ui = (cnst); return u.f; }
float FLT_INF(void);
float FLT_MINF(void);
float FLT_NAN(void);
float FLT_INF(void) Declare_Special_Float(0x7F800000);
float FLT_MINF(void) Declare_Special_Float(0xFF800000);
float FLT_NAN(void) Declare_Special_Float(0x7F800001);
/*
 * table below is
 * a = log(x+1), b = exp(-a);
 * comment shows range of x to which each line applies.
 */
static const float log_tab[64] = {
    0.000000000f,   1.000000000f,  /* 0 to  0.0111657 */
    0.022311565f,   0.977935498f,  /* ... to  0.0340233 */
    0.044580154f,   0.956398938f,  /* ... to  0.0572837 */
    0.066807851f,   0.935374915f,  /* ... to  0.0810282 */
    0.089004092f,   0.914841830f,  /* ... to  0.1052765 */
    0.111178130f,   0.894779348f,  /* ... to  0.1300487 */
    0.133338988f,   0.875168370f,  /* ... to  0.1553661 */
    0.155495435f,   0.855990985f,  /* ... to  0.1812505 */
    0.177655950f,   0.837230423f,  /* ... to  0.2077248 */
    0.199828684f,   0.818871027f,  /* ... to  0.2348125 */
    0.222021341f,   0.800898272f,  /* ... to  0.2625375 */
    0.244241118f,   0.783298744f,  /* ... to  0.2909245 */
    0.266494602f,   0.766060139f,  /* ... to  0.3199984 */
    0.288787603f,   0.749171310f,  /* ... to  0.3497841 */
    0.311125100f,   0.732622219f,  /* ... to  0.3803064 */
    0.333510906f,   0.716404086f,  /* ... to  0.4115894 */
    0.355947524f,   0.700509379f,  /* ... to  0.4436560 */
    0.378435910f,   0.684931867f,  /* ... to  0.4765275 */
    0.400975198f,   0.669666670f,  /* ... to  0.5102230 */
    0.423562229f,   0.654710433f,  /* ... to  0.5447579 */
    0.446191430f,   0.640061233f,  /* ... to  0.5801435 */
    0.468854219f,   0.625718795f,  /* ... to  0.6163859 */
    0.491538733f,   0.611684450f,  /* ... to  0.6534842 */
    0.514229417f,   0.597961196f,  /* ... to  0.6914296 */
    0.536906660f,   0.584553682f,  /* ... to  0.7302038 */
    0.559546530f,   0.571468149f,  /* ... to  0.7697776 */
    0.582120657f,   0.558712272f,  /* ... to  0.8101096 */
    0.604596078f,   0.546295042f,  /* ... to  0.8511456 */
    0.626935601f,   0.534226378f,  /* ... to  0.8928175 */
    0.649098098f,   0.522516823f,  /* ... to  0.9350435 */
    0.671039402f,   0.511176983f,  /* ... to  0.9777287 */
    0.693147182f,   0.500000000f,  /* ....to  0.9999999 */
};

/*
 * FAST LN function
 *
 * (1) split the number into its base-2 exponent 'e', and
 *   a mantissa 'xm' in range 1.0 .. 1.99999
 *
 * (2) using a cubic, find y0 = approx. ln(xm)
 * (3) scale this, round it to a table index 0...31.
 *   From the table, get a log value, (which will be added to the result)
 *   and a scale factor.
 *   Multiply xm by the scale factor, result xe is very close to 1.
 *
 * (4) find ye = log(xe) using a taylor series around xe=1
 * (5) result is is yt+ye+log(2)*exp, where yt is from the table (1st col)
 * and exp is the original exponent.
 * Note that multiplying the input by the second column of the the table,
 * and adding the 1st column of the table to the result, has no net effect.
 */
float mat_fast_log(float x)
{
    union {
        float f;
        unsigned int ui;
    } u;
    float xm1, xe, ye;
    int tabind;
    int ex;

    u.f = x;
    ex = ((u.ui >> 23) & 0x1FF) - 127;
    if ((ex <= -127) || (ex >= 128)) {
        if ((ex & 0xFF) == 1)
            return FLT_MINF();    /* was 0.0 or -0.0 (or denormal) */
        return FLT_NAN();
    }
    u.ui -= ex << 23;
    /*
     * now u.f is in range 1.0 ... 1.99999
     */
    xm1 = u.f - 1.0f;        /* 0. 1.0 */
    /*
     * The table above and the cubic below were generated together
     */
    tabind = MAT_ROUND(((xm1 * 0.1328047513f - 0.4396575689f) * xm1 * xm1 + xm1) * 44.75f);
    /*
     * tabind is in range 0..31.
     * multiply u.f by the second value in the table, subtract 1
     */
    xe = u.f * log_tab[2 * tabind + 1] - 1.0f;    /* result is  +/- .0114 */

    /*
     * find the log(xe+1) using taylor series; add to (a) amount from exponent
     * (b) amount from table
     */
    ye = ((-0.25f * xe + 0.333333333f) * xe - 0.5f) * xe * xe;
    ye += xe;
    return  0.693147182f * (float)ex + log_tab[2 * tabind] + ye;
}

static const float exp_table[16] =
{
    /* (1/6) * 2^(i/16.), to float precision */
    0.166666672f,  0.174045637f,  0.181751296f,  0.189798102f,
    0.198201180f,  0.206976309f,  0.216139928f,  0.225709260f,
    0.235702261f,  0.246137694f,  0.257035136f,  0.268415064f,
    0.280298799f,  0.292708695f,  0.305668026f,  0.319201082f
};

/*
 * FAST_EXP does an exponential function.
 * This is done using a table lookup to
 * get close and a taylor series to
 * get accurate.
 *
 * if y = exp(x) = (2^m)*(P^n)*exp(f),   where P = 2^(1/16),
 *
 * then x = ln(2^m)  + ln(P^n) + f
 *        = ln(P^(16*m+n)) + f
 *        = ln(P) * [ 16*m +n ] +f
 * let k = ln(P) = ln(2)/16 = 0.043321698785
 *
 * so x = k*[16*m + n] + f
 *
 * For a given x, we find m,n,f such that:
 *   m is an integer
 *   n is in integer 0..15
 *   f is as close to zero as possible: +/- k/2
 *
 * Then we find y = (2^m)*(P^n)*exp(f)
 *
 *  where 2^m is an exponent adjustment, P^n is a table lookup
 *  and exp(f) is calculated. The 4th term in the series
 *  for exp(f) is at most k^4/(16*24) = 9.17e-9, so we only
 *  need to do up to the 3rd order.
 *
 * One more quirk:
 *  exp(f) is evaluated via
 *  6*exp(f) = ((f + 3)*f + 6)*f + 6
 *
 * To compensate, the numbers in the P^n table are really 1/6 as
 * big as they should be.
 *
 * Example: exp(13.2)
 *   13.2 * (1/k) = 304.697, round to 305 => m*16+n = 305
 *   f = 13.2 - k * 305 = -0.013118
 *   m = 19, n = 1
 *
 *   6*exp(f) = ((f + 3)*f + 6)*f + 6 = 5.921805
 *   exp_table[n] * (6*exp(f)) = .174046 * 5.921805 = 1.030664
 *    multiply that by 2^m (=5.24288e5)  -> 5.40365e5
 *
 */
float mat_fast_exp(float x)
{
    int m, n;
    union {
        unsigned ui;
        float f;
    } u;

    n = MAT_ROUND(x * 23.08312065f);        /* 16/log(2) */
    /*
     * range check on n now
     */
    if ((n <= -2016) || (n >= 2048)) {
        if (n < 0)
            return 0.0f;
        else
            return FLT_INF();
    }
    x -= (float)n * 0.043321698785f;    /* log(2)/16. */

    m = (n >> 4);
    x = ((x + 3.0f) * x + 6.0f) * x + 6.0f;
    u.f = x * exp_table[n & 15];
    u.ui += (m << 23);    /* exponent adjust */

    return u.f;
}
#endif

MATFLOAT mat_sqrt(MATFLOAT val)
{
#ifndef GM_MAT_MATH
    return 1.0 / (MATFLOAT)mat_fast_rsqrt((float)val);
#else
    return (MATFLOAT)sqrt(val);
#endif
}

MATFLOAT mat_log(MATFLOAT val)
{ /* base e */
#ifdef GM_MAT_MATH
    return (MATFLOAT)log(val);
#else
    return (MATFLOAT)mat_fast_log((float)val);
#endif
}

MATFLOAT mat_exp(MATFLOAT val)
{
#ifdef GM_MAT_MATH
    return (MATFLOAT)exp(val);
#else
    return (MATFLOAT)mat_fast_exp((float)val);
#endif
}

unsigned int mat_index_3dlut(int ind_r, int ind_g, int ind_b, int num_pnts, enum mat_order_3dlut order)
{
    unsigned int index;

    switch (order) {
        case MAT_ORDER_RGB:
            index = (ind_b * num_pnts + ind_g) * num_pnts + ind_r;
            break;
        case MAT_ORDER_BGR:
        default:
            index = (ind_r * num_pnts + ind_g) * num_pnts + ind_b;
            break;
    }

    return index;
}
