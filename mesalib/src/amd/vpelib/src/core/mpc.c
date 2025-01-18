/* Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <string.h>
#include "vpe_priv.h"
#include "mpc.h"

enum mpc_color_gamut_type {
    COLOR_GAMUT_RGB_TYPE,
    COLOR_GAMUT_YCBCR601_TYPE,
    COLOR_GAMUT_YCBCR709_TYPE,
    COLOR_GAMUT_YCBCR2020_TYPE
};

struct out_csc_2d_color_matrix_type {
    enum mpc_color_gamut_type color_gamut_type;
    enum color_range_type     color_range_type;
    uint16_t                  regval[12];
};

static const struct out_csc_2d_color_matrix_type output_csc_2d_matrix[] = {
    {COLOR_GAMUT_RGB_TYPE, COLOR_RANGE_FULL, {0x2000, 0, 0, 0, 0, 0x2000, 0, 0, 0, 0, 0x2000, 0}},
    {COLOR_GAMUT_RGB_TYPE, COLOR_RANGE_LIMITED_8BPC,
        {0x1b7b, 0, 0, 0x202, 0, 0x1b7b, 0, 0x202, 0, 0, 0x1b7b, 0x202}},
    {COLOR_GAMUT_RGB_TYPE, COLOR_RANGE_LIMITED_10BPC,
        {0x1b67, 0, 0, 0x201, 0, 0x1b67, 0, 0x201, 0, 0, 0x1b67, 0x201}},
    {COLOR_GAMUT_RGB_TYPE, COLOR_RANGE_LIMITED_16BPC,
        {0x1b60, 0, 0, 0x200, 0, 0x1b60, 0, 0x200, 0, 0, 0x1b60, 0x200}},

    {COLOR_GAMUT_YCBCR601_TYPE, COLOR_RANGE_FULL,
        {0x1000, 0xf29a, 0xfd66, 0x1000, 0x0991, 0x12c9, 0x03a6, 0x0000, 0xfa9a, 0xf566, 0x1000,
            0x1000}},
    {COLOR_GAMUT_YCBCR601_TYPE, COLOR_RANGE_LIMITED_8BPC,
        {0x0e0e, 0xf43b, 0xfdb7, 0x1010, 0x0838, 0x1022, 0x0322, 0x0202, 0xfb42, 0xf6b0, 0x0e0e,
            0x1010}},
    {COLOR_GAMUT_YCBCR601_TYPE, COLOR_RANGE_LIMITED_10BPC,
        {0x0e03, 0xf444, 0xfdb9, 0x1004, 0x0831, 0x1016, 0x0320, 0x0201, 0xfb45, 0xf6b8, 0x0e03,
            0x1004}},
    {COLOR_GAMUT_YCBCR601_TYPE, COLOR_RANGE_LIMITED_16BPC,
        {0x0db0, 0xf48a, 0xfdc6, 0x0fb0, 0x0830, 0x1012, 0x031f, 0x0200, 0xfb61, 0xf6ee, 0x0db0,
            0x0fb0}},

    {COLOR_GAMUT_YCBCR709_TYPE, COLOR_RANGE_FULL,
        {0x1000, 0xf177, 0xfe89, 0x1000, 0x06ce, 0x16e3, 0x024f, 0x0000, 0xfc55, 0xf3ab, 0x1000,
            0x1000}},
    {COLOR_GAMUT_YCBCR709_TYPE, COLOR_RANGE_LIMITED_8BPC,
        {0x0e0e, 0xf33c, 0xfeb6, 0x1010, 0x05d8, 0x13a8, 0x01fc, 0x0202, 0xfcc8, 0xf52a, 0x0e0e,
            0x1010}},
    {COLOR_GAMUT_YCBCR709_TYPE, COLOR_RANGE_LIMITED_10BPC,
        {0x0e03, 0xf345, 0xfeb7, 0x1004, 0x05d4, 0x1399, 0x01fa, 0x0201, 0xfcca, 0xf532, 0x0e03,
            0x1004}},
    {COLOR_GAMUT_YCBCR709_TYPE, COLOR_RANGE_LIMITED_16BPC,
        {0x0db0, 0xf391, 0xfebf, 0x0fb0, 0x05d2, 0x1394, 0x01fa, 0x0200, 0xfcdd, 0xf573, 0x0db0,
            0x0fb0}},

    {COLOR_GAMUT_YCBCR2020_TYPE, COLOR_RANGE_FULL,
        {0x1000, 0xf149, 0xfeb7, 0x1000, 0x0868, 0x15b2, 0x01e6, 0x0000, 0xfb88, 0xf478, 0x1000,
            0x1000}},
    {COLOR_GAMUT_YCBCR2020_TYPE, COLOR_RANGE_LIMITED_8BPC,
        {0x0e0e, 0xf313, 0xfedf, 0x1010, 0x0738, 0x12a2, 0x01a1, 0x0202, 0xfc13, 0xf5de, 0x0e0e,
            0x1010}},
    {COLOR_GAMUT_YCBCR2020_TYPE, COLOR_RANGE_LIMITED_10BPC,
        {0x0e03, 0xf31d, 0xfee0, 0x1004, 0x0733, 0x1294, 0x01a0, 0x0201, 0xfc16, 0xf5e7, 0x0e03,
            0x1004}},
    {COLOR_GAMUT_YCBCR2020_TYPE, COLOR_RANGE_LIMITED_16BPC,
        {0x0db0, 0xf36a, 0xfee6, 0x0fb0, 0x0731, 0x128f, 0x019f, 0x0200, 0xfc2d, 0xf622, 0x0db0,
            0x0fb0}}};

static bool is_ycbcr2020_limited_type(enum color_space color_space)
{
    return color_space == COLOR_SPACE_2020_YCBCR_LIMITED;
}

static enum mpc_color_gamut_type get_color_gamut_type(enum color_space color_space)
{
    switch (color_space) {
    case COLOR_SPACE_SRGB:
    case COLOR_SPACE_SRGB_LIMITED:
    case COLOR_SPACE_MSREF_SCRGB:
    case COLOR_SPACE_RGB601:
    case COLOR_SPACE_RGB601_LIMITED:
    case COLOR_SPACE_2020_RGB_FULLRANGE:
    case COLOR_SPACE_2020_RGB_LIMITEDRANGE:
        return COLOR_GAMUT_RGB_TYPE;
    case COLOR_SPACE_YCBCR601:
    case COLOR_SPACE_YCBCR601_LIMITED:
    case COLOR_SPACE_YCBCR_JFIF:
        return COLOR_GAMUT_YCBCR601_TYPE;
    case COLOR_SPACE_YCBCR709:
    case COLOR_SPACE_YCBCR709_LIMITED:
        return COLOR_GAMUT_YCBCR709_TYPE;
    case COLOR_SPACE_2020_YCBCR:
    case COLOR_SPACE_2020_YCBCR_LIMITED:
        return COLOR_GAMUT_YCBCR2020_TYPE;
    default:
        VPE_ASSERT(false);
        return COLOR_GAMUT_RGB_TYPE;
    }
}

#define NUM_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))

const uint16_t *vpe_find_color_matrix(
    enum color_space color_space, enum vpe_surface_pixel_format pixel_format, uint32_t *array_size)
{
    const uint16_t *val = NULL;
    enum mpc_color_gamut_type gamut = get_color_gamut_type(color_space);
    enum color_range_type     range = vpe_get_range_type(color_space, pixel_format);

    int i;
    int arr_size = NUM_ELEMENTS(output_csc_2d_matrix);
    
    for (i = 0; i < arr_size; i++)
        if (output_csc_2d_matrix[i].color_gamut_type == gamut &&
            output_csc_2d_matrix[i].color_range_type == range) {
            val = output_csc_2d_matrix[i].regval;
            *array_size = 12;
            break;
        }
    return val;
}
