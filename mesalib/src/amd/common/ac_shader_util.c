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

#include "ac_nir_to_llvm.h"
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
	       S_028A40_ES_WRITE_OPTIMIZE(chip_class <= VI) |
	       S_028A40_GS_WRITE_OPTIMIZE(1) |
	       S_028A40_ONCHIP(chip_class >= GFX9 ? 1 : 0);
}

void
ac_export_mrt_z(struct ac_llvm_context *ctx, LLVMValueRef depth,
		LLVMValueRef stencil, LLVMValueRef samplemask,
		struct ac_export_args *args)
{
	unsigned mask = 0;
	unsigned format = ac_get_spi_shader_z_format(depth != NULL,
						     stencil != NULL,
						     samplemask != NULL);

	assert(depth || stencil || samplemask);

	memset(args, 0, sizeof(*args));

	args->valid_mask = 1; /* whether the EXEC mask is valid */
	args->done = 1; /* DONE bit */

	/* Specify the target we are exporting */
	args->target = V_008DFC_SQ_EXP_MRTZ;

	args->compr = 0; /* COMP flag */
	args->out[0] = LLVMGetUndef(ctx->f32); /* R, depth */
	args->out[1] = LLVMGetUndef(ctx->f32); /* G, stencil test val[0:7], stencil op val[8:15] */
	args->out[2] = LLVMGetUndef(ctx->f32); /* B, sample mask */
	args->out[3] = LLVMGetUndef(ctx->f32); /* A, alpha to mask */

	if (format == V_028710_SPI_SHADER_UINT16_ABGR) {
		assert(!depth);
		args->compr = 1; /* COMPR flag */

		if (stencil) {
			/* Stencil should be in X[23:16]. */
			stencil = ac_to_integer(ctx, stencil);
			stencil = LLVMBuildShl(ctx->builder, stencil,
					       LLVMConstInt(ctx->i32, 16, 0), "");
			args->out[0] = ac_to_float(ctx, stencil);
			mask |= 0x3;
		}
		if (samplemask) {
			/* SampleMask should be in Y[15:0]. */
			args->out[1] = samplemask;
			mask |= 0xc;
		}
	} else {
		if (depth) {
			args->out[0] = depth;
			mask |= 0x1;
		}
		if (stencil) {
			args->out[1] = stencil;
			mask |= 0x2;
		}
		if (samplemask) {
			args->out[2] = samplemask;
			mask |= 0x4;
		}
	}

	/* SI (except OLAND and HAINAN) has a bug that it only looks
	 * at the X writemask component. */
	if (ctx->chip_class == SI &&
	    ctx->family != CHIP_OLAND &&
	    ctx->family != CHIP_HAINAN)
		mask |= 0x1;

	/* Specify which components to enable */
	args->enabled_channels = mask;
}
