/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 * Copyright 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_FORMATS_H
#define AC_FORMATS_H

#include "amd_family.h"

#include "util/format/u_format.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t
ac_translate_buffer_numformat(const struct util_format_description *desc,
                              int first_non_void);

uint32_t
ac_translate_buffer_dataformat(const struct util_format_description *desc,
                              int first_non_void);

uint32_t
ac_translate_tex_numformat(const struct util_format_description *desc,
                           int first_non_void);

uint32_t
ac_translate_tex_dataformat(const struct radeon_info *info,
                            const struct util_format_description *desc,
                            int first_non_void);

unsigned
ac_get_cb_format(enum amd_gfx_level gfx_level, enum pipe_format format);

unsigned
ac_get_cb_number_type(enum pipe_format format);

unsigned
ac_translate_colorswap(enum amd_gfx_level gfx_level,
                       enum pipe_format format,
                       bool do_endian_swap);

bool
ac_is_colorbuffer_format_supported(enum amd_gfx_level gfx_level,
                                   enum pipe_format format);

uint32_t
ac_colorformat_endian_swap(uint32_t colorformat);

uint32_t
ac_translate_dbformat(enum pipe_format format);

bool
ac_is_zs_format_supported(enum pipe_format format);

uint32_t
ac_border_color_swizzle(const struct util_format_description *desc);

enum pipe_format
ac_simplify_cb_format(enum pipe_format format);

bool
ac_alpha_is_on_msb(const struct radeon_info *info, enum pipe_format format);

bool
ac_is_reduction_mode_supported(const struct radeon_info *info, enum pipe_format format,
                               bool shadow_samplers);

#ifdef __cplusplus
}
#endif

#endif
