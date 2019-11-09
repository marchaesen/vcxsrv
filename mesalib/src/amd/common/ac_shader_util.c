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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "ac_shader_util.h"
#include "sid.h"

unsigned
ac_get_spi_shader_z_format(bool writes_z, bool writes_stencil,
			   bool writes_samplemask)
{
	if (writes_z) {
		/* Z needs 32 bits. */
		if (writes_samplemask)
			return V_028710_SPI_SHADER_32_ABGR;
		else if (writes_stencil)
			return V_028710_SPI_SHADER_32_GR;
		else
			return V_028710_SPI_SHADER_32_R;
	} else if (writes_stencil || writes_samplemask) {
		/* Both stencil and sample mask need only 16 bits. */
		return V_028710_SPI_SHADER_UINT16_ABGR;
	} else {
		return V_028710_SPI_SHADER_ZERO;
	}
}

unsigned
ac_get_cb_shader_mask(unsigned spi_shader_col_format)
{
	unsigned i, cb_shader_mask = 0;

	for (i = 0; i < 8; i++) {
		switch ((spi_shader_col_format >> (i * 4)) & 0xf) {
		case V_028714_SPI_SHADER_ZERO:
			break;
		case V_028714_SPI_SHADER_32_R:
			cb_shader_mask |= 0x1 << (i * 4);
			break;
		case V_028714_SPI_SHADER_32_GR:
			cb_shader_mask |= 0x3 << (i * 4);
			break;
		case V_028714_SPI_SHADER_32_AR:
			cb_shader_mask |= 0x9 << (i * 4);
			break;
		case V_028714_SPI_SHADER_FP16_ABGR:
		case V_028714_SPI_SHADER_UNORM16_ABGR:
		case V_028714_SPI_SHADER_SNORM16_ABGR:
		case V_028714_SPI_SHADER_UINT16_ABGR:
		case V_028714_SPI_SHADER_SINT16_ABGR:
		case V_028714_SPI_SHADER_32_ABGR:
			cb_shader_mask |= 0xf << (i * 4);
			break;
		default:
			assert(0);
		}
	}
	return cb_shader_mask;
}

/**
 * Calculate the appropriate setting of VGT_GS_MODE when \p shader is a
 * geometry shader.
 */
uint32_t
ac_vgt_gs_mode(unsigned gs_max_vert_out, enum chip_class chip_class)
{
	unsigned cut_mode;

	if (gs_max_vert_out <= 128) {
		cut_mode = V_028A40_GS_CUT_128;
	} else if (gs_max_vert_out <= 256) {
		cut_mode = V_028A40_GS_CUT_256;
	} else if (gs_max_vert_out <= 512) {
		cut_mode = V_028A40_GS_CUT_512;
	} else {
		assert(gs_max_vert_out <= 1024);
		cut_mode = V_028A40_GS_CUT_1024;
	}

	return S_028A40_MODE(V_028A40_GS_SCENARIO_G) |
	       S_028A40_CUT_MODE(cut_mode)|
	       S_028A40_ES_WRITE_OPTIMIZE(chip_class <= GFX8) |
	       S_028A40_GS_WRITE_OPTIMIZE(1) |
	       S_028A40_ONCHIP(chip_class >= GFX9 ? 1 : 0);
}

/// Translate a (dfmt, nfmt) pair into a chip-appropriate combined format
/// value for LLVM8+ tbuffer intrinsics.
unsigned
ac_get_tbuffer_format(enum chip_class chip_class,
		      unsigned dfmt, unsigned nfmt)
{
	// Some games try to access vertex buffers without a valid format.
	// This is a game bug, but we should still handle it gracefully.
	if (dfmt == V_008F0C_IMG_FORMAT_INVALID)
		return V_008F0C_IMG_FORMAT_INVALID;

	if (chip_class >= GFX10) {
		unsigned format;
		switch (dfmt) {
		default: unreachable("bad dfmt");
		case V_008F0C_BUF_DATA_FORMAT_INVALID: format = V_008F0C_IMG_FORMAT_INVALID; break;
		case V_008F0C_BUF_DATA_FORMAT_8: format = V_008F0C_IMG_FORMAT_8_UINT; break;
		case V_008F0C_BUF_DATA_FORMAT_8_8: format = V_008F0C_IMG_FORMAT_8_8_UINT; break;
		case V_008F0C_BUF_DATA_FORMAT_8_8_8_8: format = V_008F0C_IMG_FORMAT_8_8_8_8_UINT; break;
		case V_008F0C_BUF_DATA_FORMAT_16: format = V_008F0C_IMG_FORMAT_16_UINT; break;
		case V_008F0C_BUF_DATA_FORMAT_16_16: format = V_008F0C_IMG_FORMAT_16_16_UINT; break;
		case V_008F0C_BUF_DATA_FORMAT_16_16_16_16: format = V_008F0C_IMG_FORMAT_16_16_16_16_UINT; break;
		case V_008F0C_BUF_DATA_FORMAT_32: format = V_008F0C_IMG_FORMAT_32_UINT; break;
		case V_008F0C_BUF_DATA_FORMAT_32_32: format = V_008F0C_IMG_FORMAT_32_32_UINT; break;
		case V_008F0C_BUF_DATA_FORMAT_32_32_32: format = V_008F0C_IMG_FORMAT_32_32_32_UINT; break;
		case V_008F0C_BUF_DATA_FORMAT_32_32_32_32: format = V_008F0C_IMG_FORMAT_32_32_32_32_UINT; break;
		case V_008F0C_BUF_DATA_FORMAT_2_10_10_10: format = V_008F0C_IMG_FORMAT_2_10_10_10_UINT; break;
		}

		// Use the regularity properties of the combined format enum.
		//
		// Note: float is incompatible with 8-bit data formats,
		//       [us]{norm,scaled} are incomparible with 32-bit data formats.
		//       [us]scaled are not writable.
		switch (nfmt) {
		case V_008F0C_BUF_NUM_FORMAT_UNORM: format -= 4; break;
		case V_008F0C_BUF_NUM_FORMAT_SNORM: format -= 3; break;
		case V_008F0C_BUF_NUM_FORMAT_USCALED: format -= 2; break;
		case V_008F0C_BUF_NUM_FORMAT_SSCALED: format -= 1; break;
		default: unreachable("bad nfmt");
		case V_008F0C_BUF_NUM_FORMAT_UINT: break;
		case V_008F0C_BUF_NUM_FORMAT_SINT: format += 1; break;
		case V_008F0C_BUF_NUM_FORMAT_FLOAT: format += 2; break;
		}

		return format;
	} else {
		return dfmt | (nfmt << 4);
	}
}

enum ac_image_dim
ac_get_sampler_dim(enum chip_class chip_class, enum glsl_sampler_dim dim,
		   bool is_array)
{
	switch (dim) {
	case GLSL_SAMPLER_DIM_1D:
		if (chip_class == GFX9)
			return is_array ? ac_image_2darray : ac_image_2d;
		return is_array ? ac_image_1darray : ac_image_1d;
	case GLSL_SAMPLER_DIM_2D:
	case GLSL_SAMPLER_DIM_RECT:
	case GLSL_SAMPLER_DIM_EXTERNAL:
		return is_array ? ac_image_2darray : ac_image_2d;
	case GLSL_SAMPLER_DIM_3D:
		return ac_image_3d;
	case GLSL_SAMPLER_DIM_CUBE:
		return ac_image_cube;
	case GLSL_SAMPLER_DIM_MS:
		return is_array ? ac_image_2darraymsaa : ac_image_2dmsaa;
	case GLSL_SAMPLER_DIM_SUBPASS:
		return ac_image_2darray;
	case GLSL_SAMPLER_DIM_SUBPASS_MS:
		return ac_image_2darraymsaa;
	default:
		unreachable("bad sampler dim");
	}
}

enum ac_image_dim
ac_get_image_dim(enum chip_class chip_class, enum glsl_sampler_dim sdim,
		 bool is_array)
{
	enum ac_image_dim dim = ac_get_sampler_dim(chip_class, sdim, is_array);

	/* Match the resource type set in the descriptor. */
	if (dim == ac_image_cube ||
	    (chip_class <= GFX8 && dim == ac_image_3d))
		dim = ac_image_2darray;
	else if (sdim == GLSL_SAMPLER_DIM_2D && !is_array && chip_class == GFX9) {
		/* When a single layer of a 3D texture is bound, the shader
		 * will refer to a 2D target, but the descriptor has a 3D type.
		 * Since the HW ignores BASE_ARRAY in this case, we need to
		 * send 3 coordinates. This doesn't hurt when the underlying
		 * texture is non-3D.
		 */
		dim = ac_image_3d;
	}

	return dim;
}

unsigned
ac_get_fs_input_vgpr_cnt(const struct ac_shader_config *config,
			 signed char *face_vgpr_index_ptr,
			 signed char *ancillary_vgpr_index_ptr)
{
	unsigned num_input_vgprs = 0;
	signed char face_vgpr_index = -1;
	signed char ancillary_vgpr_index = -1;

	if (G_0286CC_PERSP_SAMPLE_ENA(config->spi_ps_input_addr))
		num_input_vgprs += 2;
	if (G_0286CC_PERSP_CENTER_ENA(config->spi_ps_input_addr))
		num_input_vgprs += 2;
	if (G_0286CC_PERSP_CENTROID_ENA(config->spi_ps_input_addr))
		num_input_vgprs += 2;
	if (G_0286CC_PERSP_PULL_MODEL_ENA(config->spi_ps_input_addr))
		num_input_vgprs += 3;
	if (G_0286CC_LINEAR_SAMPLE_ENA(config->spi_ps_input_addr))
		num_input_vgprs += 2;
	if (G_0286CC_LINEAR_CENTER_ENA(config->spi_ps_input_addr))
		num_input_vgprs += 2;
	if (G_0286CC_LINEAR_CENTROID_ENA(config->spi_ps_input_addr))
		num_input_vgprs += 2;
	if (G_0286CC_LINE_STIPPLE_TEX_ENA(config->spi_ps_input_addr))
		num_input_vgprs += 1;
	if (G_0286CC_POS_X_FLOAT_ENA(config->spi_ps_input_addr))
		num_input_vgprs += 1;
	if (G_0286CC_POS_Y_FLOAT_ENA(config->spi_ps_input_addr))
		num_input_vgprs += 1;
	if (G_0286CC_POS_Z_FLOAT_ENA(config->spi_ps_input_addr))
		num_input_vgprs += 1;
	if (G_0286CC_POS_W_FLOAT_ENA(config->spi_ps_input_addr))
		num_input_vgprs += 1;
	if (G_0286CC_FRONT_FACE_ENA(config->spi_ps_input_addr)) {
		face_vgpr_index = num_input_vgprs;
		num_input_vgprs += 1;
	}
	if (G_0286CC_ANCILLARY_ENA(config->spi_ps_input_addr)) {
		ancillary_vgpr_index = num_input_vgprs;
		num_input_vgprs += 1;
	}
	if (G_0286CC_SAMPLE_COVERAGE_ENA(config->spi_ps_input_addr))
		num_input_vgprs += 1;
	if (G_0286CC_POS_FIXED_PT_ENA(config->spi_ps_input_addr))
		num_input_vgprs += 1;

	if (face_vgpr_index_ptr)
		*face_vgpr_index_ptr = face_vgpr_index;
	if (ancillary_vgpr_index_ptr)
		*ancillary_vgpr_index_ptr = ancillary_vgpr_index;

	return num_input_vgprs;
}
