/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef AC_SHADER_UTIL_H
#define AC_SHADER_UTIL_H

#include <stdbool.h>
#include <stdint.h>

#include "amd_family.h"
#include "ac_binary.h"
#include "compiler/nir/nir.h"

#ifdef __cplusplus
extern "C" {
#endif

enum ac_image_dim {
	ac_image_1d,
	ac_image_2d,
	ac_image_3d,
	ac_image_cube, // includes cube arrays
	ac_image_1darray,
	ac_image_2darray,
	ac_image_2dmsaa,
	ac_image_2darraymsaa,
};

unsigned
ac_get_spi_shader_z_format(bool writes_z, bool writes_stencil,
			   bool writes_samplemask);

unsigned
ac_get_cb_shader_mask(unsigned spi_shader_col_format);

uint32_t
ac_vgt_gs_mode(unsigned gs_max_vert_out, enum chip_class chip_class);

unsigned
ac_get_tbuffer_format(enum chip_class chip_class,
		      unsigned dfmt, unsigned nfmt);

enum ac_image_dim
ac_get_sampler_dim(enum chip_class chip_class, enum glsl_sampler_dim dim,
		   bool is_array);

enum ac_image_dim
ac_get_image_dim(enum chip_class chip_class, enum glsl_sampler_dim sdim,
		 bool is_array);

unsigned
ac_get_fs_input_vgpr_cnt(const struct ac_shader_config *config,
			 signed char *face_vgpr_index,
			 signed char *ancillary_vgpr_index);

#ifdef __cplusplus
}
#endif

#endif
