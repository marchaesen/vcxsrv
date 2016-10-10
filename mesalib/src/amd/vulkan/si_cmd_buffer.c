/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based on si_state.c
 * Copyright © 2015 Advanced Micro Devices, Inc.
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

/* command buffer handling for SI */

#include "radv_private.h"
#include "radv_cs.h"
#include "sid.h"
#include "radv_util.h"
#include "main/macros.h"

#define SI_GS_PER_ES 128

static void
si_write_harvested_raster_configs(struct radv_physical_device *physical_device,
                                  struct radeon_winsys_cs *cs,
				  unsigned raster_config,
				  unsigned raster_config_1)
{
	unsigned sh_per_se = MAX2(physical_device->rad_info.max_sh_per_se, 1);
	unsigned num_se = MAX2(physical_device->rad_info.max_se, 1);
	unsigned rb_mask = physical_device->rad_info.enabled_rb_mask;
	unsigned num_rb = MIN2(physical_device->rad_info.num_render_backends, 16);
	unsigned rb_per_pkr = MIN2(num_rb / num_se / sh_per_se, 2);
	unsigned rb_per_se = num_rb / num_se;
	unsigned se_mask[4];
	unsigned se;

	se_mask[0] = ((1 << rb_per_se) - 1) & rb_mask;
	se_mask[1] = (se_mask[0] << rb_per_se) & rb_mask;
	se_mask[2] = (se_mask[1] << rb_per_se) & rb_mask;
	se_mask[3] = (se_mask[2] << rb_per_se) & rb_mask;

	assert(num_se == 1 || num_se == 2 || num_se == 4);
	assert(sh_per_se == 1 || sh_per_se == 2);
	assert(rb_per_pkr == 1 || rb_per_pkr == 2);

	/* XXX: I can't figure out what the *_XSEL and *_YSEL
	 * fields are for, so I'm leaving them as their default
	 * values. */

	if ((num_se > 2) && ((!se_mask[0] && !se_mask[1]) ||
			     (!se_mask[2] && !se_mask[3]))) {
		raster_config_1 &= C_028354_SE_PAIR_MAP;

		if (!se_mask[0] && !se_mask[1]) {
			raster_config_1 |=
				S_028354_SE_PAIR_MAP(V_028354_RASTER_CONFIG_SE_PAIR_MAP_3);
		} else {
			raster_config_1 |=
				S_028354_SE_PAIR_MAP(V_028354_RASTER_CONFIG_SE_PAIR_MAP_0);
		}
	}

	for (se = 0; se < num_se; se++) {
		unsigned raster_config_se = raster_config;
		unsigned pkr0_mask = ((1 << rb_per_pkr) - 1) << (se * rb_per_se);
		unsigned pkr1_mask = pkr0_mask << rb_per_pkr;
		int idx = (se / 2) * 2;

		if ((num_se > 1) && (!se_mask[idx] || !se_mask[idx + 1])) {
			raster_config_se &= C_028350_SE_MAP;

			if (!se_mask[idx]) {
				raster_config_se |=
					S_028350_SE_MAP(V_028350_RASTER_CONFIG_SE_MAP_3);
			} else {
				raster_config_se |=
					S_028350_SE_MAP(V_028350_RASTER_CONFIG_SE_MAP_0);
			}
		}

		pkr0_mask &= rb_mask;
		pkr1_mask &= rb_mask;
		if (rb_per_se > 2 && (!pkr0_mask || !pkr1_mask)) {
			raster_config_se &= C_028350_PKR_MAP;

			if (!pkr0_mask) {
				raster_config_se |=
					S_028350_PKR_MAP(V_028350_RASTER_CONFIG_PKR_MAP_3);
			} else {
				raster_config_se |=
					S_028350_PKR_MAP(V_028350_RASTER_CONFIG_PKR_MAP_0);
			}
		}

		if (rb_per_se >= 2) {
			unsigned rb0_mask = 1 << (se * rb_per_se);
			unsigned rb1_mask = rb0_mask << 1;

			rb0_mask &= rb_mask;
			rb1_mask &= rb_mask;
			if (!rb0_mask || !rb1_mask) {
				raster_config_se &= C_028350_RB_MAP_PKR0;

				if (!rb0_mask) {
					raster_config_se |=
						S_028350_RB_MAP_PKR0(V_028350_RASTER_CONFIG_RB_MAP_3);
				} else {
					raster_config_se |=
						S_028350_RB_MAP_PKR0(V_028350_RASTER_CONFIG_RB_MAP_0);
				}
			}

			if (rb_per_se > 2) {
				rb0_mask = 1 << (se * rb_per_se + rb_per_pkr);
				rb1_mask = rb0_mask << 1;
				rb0_mask &= rb_mask;
				rb1_mask &= rb_mask;
				if (!rb0_mask || !rb1_mask) {
					raster_config_se &= C_028350_RB_MAP_PKR1;

					if (!rb0_mask) {
						raster_config_se |=
							S_028350_RB_MAP_PKR1(V_028350_RASTER_CONFIG_RB_MAP_3);
					} else {
						raster_config_se |=
							S_028350_RB_MAP_PKR1(V_028350_RASTER_CONFIG_RB_MAP_0);
					}
				}
			}
		}

		/* GRBM_GFX_INDEX has a different offset on SI and CI+ */
		if (physical_device->rad_info.chip_class < CIK)
			radeon_set_config_reg(cs, GRBM_GFX_INDEX,
					      SE_INDEX(se) | SH_BROADCAST_WRITES |
					      INSTANCE_BROADCAST_WRITES);
		else
			radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX,
					       S_030800_SE_INDEX(se) | S_030800_SH_BROADCAST_WRITES(1) |
					       S_030800_INSTANCE_BROADCAST_WRITES(1));
		radeon_set_context_reg(cs, R_028350_PA_SC_RASTER_CONFIG, raster_config_se);
		if (physical_device->rad_info.chip_class >= CIK)
			radeon_set_context_reg(cs, R_028354_PA_SC_RASTER_CONFIG_1, raster_config_1);
	}

	/* GRBM_GFX_INDEX has a different offset on SI and CI+ */
	if (physical_device->rad_info.chip_class < CIK)
		radeon_set_config_reg(cs, GRBM_GFX_INDEX,
				      SE_BROADCAST_WRITES | SH_BROADCAST_WRITES |
				      INSTANCE_BROADCAST_WRITES);
	else
		radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX,
				       S_030800_SE_BROADCAST_WRITES(1) | S_030800_SH_BROADCAST_WRITES(1) |
				       S_030800_INSTANCE_BROADCAST_WRITES(1));
}

static void
si_init_compute(struct radv_physical_device *physical_device,
                struct radeon_winsys_cs *cs)
{
	radeon_set_sh_reg_seq(cs, R_00B810_COMPUTE_START_X, 3);
	radeon_emit(cs, 0);
	radeon_emit(cs, 0);
	radeon_emit(cs, 0);

	radeon_set_sh_reg_seq(cs, R_00B854_COMPUTE_RESOURCE_LIMITS, 3);
	radeon_emit(cs, 0);
	/* R_00B858_COMPUTE_STATIC_THREAD_MGMT_SE0 / SE1 */
	radeon_emit(cs, S_00B858_SH0_CU_EN(0xffff) | S_00B858_SH1_CU_EN(0xffff));
	radeon_emit(cs, S_00B85C_SH0_CU_EN(0xffff) | S_00B85C_SH1_CU_EN(0xffff));

	if (physical_device->rad_info.chip_class >= CIK) {
		/* Also set R_00B858_COMPUTE_STATIC_THREAD_MGMT_SE2 / SE3 */
		radeon_set_sh_reg_seq(cs,
				      R_00B864_COMPUTE_STATIC_THREAD_MGMT_SE2, 2);
		radeon_emit(cs, S_00B864_SH0_CU_EN(0xffff) |
			    S_00B864_SH1_CU_EN(0xffff));
		radeon_emit(cs, S_00B868_SH0_CU_EN(0xffff) |
			    S_00B868_SH1_CU_EN(0xffff));
	}

	/* This register has been moved to R_00CD20_COMPUTE_MAX_WAVE_ID
	 * and is now per pipe, so it should be handled in the
	 * kernel if we want to use something other than the default value,
	 * which is now 0x22f.
	 */
	if (physical_device->rad_info.chip_class <= SI) {
		/* XXX: This should be:
		 * (number of compute units) * 4 * (waves per simd) - 1 */

		radeon_set_sh_reg(cs, R_00B82C_COMPUTE_MAX_WAVE_ID,
		                  0x190 /* Default value */);
	}
}


void si_init_config(struct radv_physical_device *physical_device,
		    struct radv_cmd_buffer *cmd_buffer)
{
	unsigned num_rb = MIN2(physical_device->rad_info.num_render_backends, 16);
	unsigned rb_mask = physical_device->rad_info.enabled_rb_mask;
	unsigned raster_config, raster_config_1;
	int i;
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	radeon_emit(cs, PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
	radeon_emit(cs, CONTEXT_CONTROL_LOAD_ENABLE(1));
	radeon_emit(cs, CONTEXT_CONTROL_SHADOW_ENABLE(1));

	radeon_set_context_reg(cs, R_028A18_VGT_HOS_MAX_TESS_LEVEL, fui(64));
	radeon_set_context_reg(cs, R_028A1C_VGT_HOS_MIN_TESS_LEVEL, fui(0));

	/* FIXME calculate these values somehow ??? */
	radeon_set_context_reg(cs, R_028A54_VGT_GS_PER_ES, SI_GS_PER_ES);
	radeon_set_context_reg(cs, R_028A58_VGT_ES_PER_GS, 0x40);
	radeon_set_context_reg(cs, R_028A5C_VGT_GS_PER_VS, 0x2);

	radeon_set_context_reg(cs, R_028A8C_VGT_PRIMITIVEID_RESET, 0x0);
	radeon_set_context_reg(cs, R_028B28_VGT_STRMOUT_DRAW_OPAQUE_OFFSET, 0);

	radeon_set_context_reg(cs, R_028B98_VGT_STRMOUT_BUFFER_CONFIG, 0x0);
	radeon_set_context_reg(cs, R_028AB8_VGT_VTX_CNT_EN, 0x0);
	if (physical_device->rad_info.chip_class < CIK)
		radeon_set_config_reg(cs, R_008A14_PA_CL_ENHANCE, S_008A14_NUM_CLIP_SEQ(3) |
				      S_008A14_CLIP_VTX_REORDER_ENA(1));

	radeon_set_context_reg(cs, R_028BD4_PA_SC_CENTROID_PRIORITY_0, 0x76543210);
	radeon_set_context_reg(cs, R_028BD8_PA_SC_CENTROID_PRIORITY_1, 0xfedcba98);

	radeon_set_context_reg(cs, R_02882C_PA_SU_PRIM_FILTER_CNTL, 0);

	for (i = 0; i < 16; i++) {
		radeon_set_context_reg(cs, R_0282D0_PA_SC_VPORT_ZMIN_0 + i*8, 0);
		radeon_set_context_reg(cs, R_0282D4_PA_SC_VPORT_ZMAX_0 + i*8, fui(1.0));
	}

	switch (physical_device->rad_info.family) {
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
		raster_config = 0x2a00126a;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_VERDE:
		raster_config = 0x0000124a;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_OLAND:
		raster_config = 0x00000082;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_HAINAN:
		raster_config = 0x00000000;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_BONAIRE:
		raster_config = 0x16000012;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_HAWAII:
		raster_config = 0x3a00161a;
		raster_config_1 = 0x0000002e;
		break;
	case CHIP_FIJI:
		if (physical_device->rad_info.cik_macrotile_mode_array[0] == 0x000000e8) {
			/* old kernels with old tiling config */
			raster_config = 0x16000012;
			raster_config_1 = 0x0000002a;
		} else {
			raster_config = 0x3a00161a;
			raster_config_1 = 0x0000002e;
		}
		break;
	case CHIP_POLARIS10:
		raster_config = 0x16000012;
		raster_config_1 = 0x0000002a;
		break;
	case CHIP_POLARIS11:
		raster_config = 0x16000012;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_TONGA:
		raster_config = 0x16000012;
		raster_config_1 = 0x0000002a;
		break;
	case CHIP_ICELAND:
		if (num_rb == 1)
			raster_config = 0x00000000;
		else
			raster_config = 0x00000002;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_CARRIZO:
		raster_config = 0x00000002;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_KAVERI:
		/* KV should be 0x00000002, but that causes problems with radeon */
		raster_config = 0x00000000; /* 0x00000002 */
		raster_config_1 = 0x00000000;
		break;
	case CHIP_KABINI:
	case CHIP_MULLINS:
	case CHIP_STONEY:
		raster_config = 0x00000000;
		raster_config_1 = 0x00000000;
		break;
	default:
		fprintf(stderr,
			"radeonsi: Unknown GPU, using 0 for raster_config\n");
		raster_config = 0x00000000;
		raster_config_1 = 0x00000000;
		break;
	}

	/* Always use the default config when all backends are enabled
	 * (or when we failed to determine the enabled backends).
	 */
	if (!rb_mask || util_bitcount(rb_mask) >= num_rb) {
		radeon_set_context_reg(cs, R_028350_PA_SC_RASTER_CONFIG,
				       raster_config);
		if (physical_device->rad_info.chip_class >= CIK)
			radeon_set_context_reg(cs, R_028354_PA_SC_RASTER_CONFIG_1,
					       raster_config_1);
	} else {
		si_write_harvested_raster_configs(physical_device, cs, raster_config, raster_config_1);
	}

	radeon_set_context_reg(cs, R_028204_PA_SC_WINDOW_SCISSOR_TL, S_028204_WINDOW_OFFSET_DISABLE(1));
	radeon_set_context_reg(cs, R_028240_PA_SC_GENERIC_SCISSOR_TL, S_028240_WINDOW_OFFSET_DISABLE(1));
	radeon_set_context_reg(cs, R_028244_PA_SC_GENERIC_SCISSOR_BR,
			       S_028244_BR_X(16384) | S_028244_BR_Y(16384));
	radeon_set_context_reg(cs, R_028030_PA_SC_SCREEN_SCISSOR_TL, 0);
	radeon_set_context_reg(cs, R_028034_PA_SC_SCREEN_SCISSOR_BR,
			       S_028034_BR_X(16384) | S_028034_BR_Y(16384));

	radeon_set_context_reg(cs, R_02820C_PA_SC_CLIPRECT_RULE, 0xFFFF);
	radeon_set_context_reg(cs, R_028230_PA_SC_EDGERULE, 0xAAAAAAAA);
	/* PA_SU_HARDWARE_SCREEN_OFFSET must be 0 due to hw bug on SI */
	radeon_set_context_reg(cs, R_028234_PA_SU_HARDWARE_SCREEN_OFFSET, 0);
	radeon_set_context_reg(cs, R_028820_PA_CL_NANINF_CNTL, 0);

	radeon_set_context_reg(cs, R_028BE8_PA_CL_GB_VERT_CLIP_ADJ, fui(1.0));
	radeon_set_context_reg(cs, R_028BEC_PA_CL_GB_VERT_DISC_ADJ, fui(1.0));
	radeon_set_context_reg(cs, R_028BF0_PA_CL_GB_HORZ_CLIP_ADJ, fui(1.0));
	radeon_set_context_reg(cs, R_028BF4_PA_CL_GB_HORZ_DISC_ADJ, fui(1.0));

	radeon_set_context_reg(cs, R_028AC0_DB_SRESULTS_COMPARE_STATE0, 0x0);
	radeon_set_context_reg(cs, R_028AC4_DB_SRESULTS_COMPARE_STATE1, 0x0);
	radeon_set_context_reg(cs, R_028AC8_DB_PRELOAD_CONTROL, 0x0);
	radeon_set_context_reg(cs, R_02800C_DB_RENDER_OVERRIDE,
			       S_02800C_FORCE_HIS_ENABLE0(V_02800C_FORCE_DISABLE) |
			       S_02800C_FORCE_HIS_ENABLE1(V_02800C_FORCE_DISABLE));

	radeon_set_context_reg(cs, R_028400_VGT_MAX_VTX_INDX, ~0);
	radeon_set_context_reg(cs, R_028404_VGT_MIN_VTX_INDX, 0);
	radeon_set_context_reg(cs, R_028408_VGT_INDX_OFFSET, 0);

	if (physical_device->rad_info.chip_class >= CIK) {
		radeon_set_sh_reg(cs, R_00B41C_SPI_SHADER_PGM_RSRC3_HS, 0);
		radeon_set_sh_reg(cs, R_00B31C_SPI_SHADER_PGM_RSRC3_ES, S_00B31C_CU_EN(0xffff));
		radeon_set_sh_reg(cs, R_00B21C_SPI_SHADER_PGM_RSRC3_GS, S_00B21C_CU_EN(0xffff));

		if (physical_device->rad_info.num_good_compute_units /
		    (physical_device->rad_info.max_se * physical_device->rad_info.max_sh_per_se) <= 4) {
			/* Too few available compute units per SH. Disallowing
			 * VS to run on CU0 could hurt us more than late VS
			 * allocation would help.
			 *
			 * LATE_ALLOC_VS = 2 is the highest safe number.
			 */
			radeon_set_sh_reg(cs, R_00B51C_SPI_SHADER_PGM_RSRC3_LS, S_00B51C_CU_EN(0xffff));
			radeon_set_sh_reg(cs, R_00B118_SPI_SHADER_PGM_RSRC3_VS, S_00B118_CU_EN(0xffff));
			radeon_set_sh_reg(cs, R_00B11C_SPI_SHADER_LATE_ALLOC_VS, S_00B11C_LIMIT(2));
		} else {
			/* Set LATE_ALLOC_VS == 31. It should be less than
			 * the number of scratch waves. Limitations:
			 * - VS can't execute on CU0.
			 * - If HS writes outputs to LDS, LS can't execute on CU0.
			 */
			radeon_set_sh_reg(cs, R_00B51C_SPI_SHADER_PGM_RSRC3_LS, S_00B51C_CU_EN(0xfffe));
			radeon_set_sh_reg(cs, R_00B118_SPI_SHADER_PGM_RSRC3_VS, S_00B118_CU_EN(0xfffe));
			radeon_set_sh_reg(cs, R_00B11C_SPI_SHADER_LATE_ALLOC_VS, S_00B11C_LIMIT(31));
		}

		radeon_set_sh_reg(cs, R_00B01C_SPI_SHADER_PGM_RSRC3_PS, S_00B01C_CU_EN(0xffff));
	}

	if (physical_device->rad_info.chip_class >= VI) {
		radeon_set_context_reg(cs, R_028424_CB_DCC_CONTROL,
				       S_028424_OVERWRITE_COMBINER_MRT_SHARING_DISABLE(1) |
				       S_028424_OVERWRITE_COMBINER_WATERMARK(4));
		radeon_set_context_reg(cs, R_028C58_VGT_VERTEX_REUSE_BLOCK_CNTL, 30);
		radeon_set_context_reg(cs, R_028C5C_VGT_OUT_DEALLOC_CNTL, 32);
		radeon_set_context_reg(cs, R_028B50_VGT_TESS_DISTRIBUTION,
				       S_028B50_ACCUM_ISOLINE(32) |
				       S_028B50_ACCUM_TRI(11) |
				       S_028B50_ACCUM_QUAD(11) |
				       S_028B50_DONUT_SPLIT(16));
	} else {
		radeon_set_context_reg(cs, R_028C58_VGT_VERTEX_REUSE_BLOCK_CNTL, 14);
		radeon_set_context_reg(cs, R_028C5C_VGT_OUT_DEALLOC_CNTL, 16);
	}

	if (physical_device->rad_info.family == CHIP_STONEY)
		radeon_set_context_reg(cs, R_028C40_PA_SC_SHADER_CONTROL, 0);

	si_init_compute(physical_device, cs);
}

static void
get_viewport_xform(const VkViewport *viewport,
                   float scale[3], float translate[3])
{
	float x = viewport->x;
	float y = viewport->y;
	float half_width = 0.5f * viewport->width;
	float half_height = 0.5f * viewport->height;
	double n = viewport->minDepth;
	double f = viewport->maxDepth;

	scale[0] = half_width;
	translate[0] = half_width + x;
	scale[1] = half_height;
	translate[1] = half_height + y;

	scale[2] = (f - n);
	translate[2] = n;
}

void
si_write_viewport(struct radeon_winsys_cs *cs, int first_vp,
                  int count, const VkViewport *viewports)
{
	int i;

	if (count == 0) {
		radeon_set_context_reg_seq(cs, R_02843C_PA_CL_VPORT_XSCALE, 6);
		radeon_emit(cs, fui(1.0));
		radeon_emit(cs, fui(0.0));
		radeon_emit(cs, fui(1.0));
		radeon_emit(cs, fui(0.0));
		radeon_emit(cs, fui(1.0));
		radeon_emit(cs, fui(0.0));

		radeon_set_context_reg_seq(cs, R_0282D0_PA_SC_VPORT_ZMIN_0, 2);
		radeon_emit(cs, fui(0.0));
		radeon_emit(cs, fui(1.0));

		return;
	}
	radeon_set_context_reg_seq(cs, R_02843C_PA_CL_VPORT_XSCALE +
				   first_vp * 4 * 6, count * 6);

	for (i = 0; i < count; i++) {
		float scale[3], translate[3];


		get_viewport_xform(&viewports[i], scale, translate);
		radeon_emit(cs, fui(scale[0]));
		radeon_emit(cs, fui(translate[0]));
		radeon_emit(cs, fui(scale[1]));
		radeon_emit(cs, fui(translate[1]));
		radeon_emit(cs, fui(scale[2]));
		radeon_emit(cs, fui(translate[2]));
	}

	for (i = 0; i < count; i++) {
		float zmin = MIN2(viewports[i].minDepth, viewports[i].maxDepth);
		float zmax = MAX2(viewports[i].minDepth, viewports[i].maxDepth);
		radeon_set_context_reg_seq(cs, R_0282D0_PA_SC_VPORT_ZMIN_0 +
					   first_vp * 4 * 2, count * 2);
		radeon_emit(cs, fui(zmin));
		radeon_emit(cs, fui(zmax));
	}
}

void
si_write_scissors(struct radeon_winsys_cs *cs, int first,
                  int count, const VkRect2D *scissors)
{
	int i;
	if (count == 0)
		return;

	radeon_set_context_reg_seq(cs, R_028250_PA_SC_VPORT_SCISSOR_0_TL + first * 4 * 2, count * 2);
	for (i = 0; i < count; i++) {
		radeon_emit(cs, S_028250_TL_X(scissors[i].offset.x) |
			    S_028250_TL_Y(scissors[i].offset.y) |
			    S_028250_WINDOW_OFFSET_DISABLE(1));
		radeon_emit(cs, S_028254_BR_X(scissors[i].offset.x + scissors[i].extent.width) |
			    S_028254_BR_Y(scissors[i].offset.y + scissors[i].extent.height));
	}
}

uint32_t
si_get_ia_multi_vgt_param(struct radv_cmd_buffer *cmd_buffer)
{
	enum chip_class chip_class = cmd_buffer->device->instance->physicalDevice.rad_info.chip_class;
	struct radeon_info *info = &cmd_buffer->device->instance->physicalDevice.rad_info;
	unsigned prim = cmd_buffer->state.pipeline->graphics.prim;
	unsigned primgroup_size = 128; /* recommended without a GS */
	unsigned max_primgroup_in_wave = 2;
	/* SWITCH_ON_EOP(0) is always preferable. */
	bool wd_switch_on_eop = false;
	bool ia_switch_on_eop = false;
	bool ia_switch_on_eoi = false;
	bool partial_vs_wave = false;
	bool partial_es_wave = false;

	/* TODO GS */

	/* TODO TES */

	/* TODO linestipple */

	if (chip_class >= CIK) {
		/* WD_SWITCH_ON_EOP has no effect on GPUs with less than
		 * 4 shader engines. Set 1 to pass the assertion below.
		 * The other cases are hardware requirements. */
		if (info->max_se < 4 ||
		    prim == V_008958_DI_PT_POLYGON ||
		    prim == V_008958_DI_PT_LINELOOP ||
		    prim == V_008958_DI_PT_TRIFAN ||
		    prim == V_008958_DI_PT_TRISTRIP_ADJ)
			//	    info->primitive_restart ||
			//	    info->count_from_stream_output)
			wd_switch_on_eop = true;

		/* TODO HAWAII */

		/* Required on CIK and later. */
		if (info->max_se > 2 && !wd_switch_on_eop)
			ia_switch_on_eoi = true;

		/* Required by Hawaii and, for some special cases, by VI. */
#if 0
		if (ia_switch_on_eoi &&
		    (sctx->b.family == CHIP_HAWAII ||
		     (sctx->b.chip_class == VI &&
		      (sctx->gs_shader.cso || max_primgroup_in_wave != 2))))
			partial_vs_wave = true;
#endif

#if 0
		/* Instancing bug on Bonaire. */
		if (sctx->b.family == CHIP_BONAIRE && ia_switch_on_eoi &&
		    (info->indirect || info->instance_count > 1))
			partial_vs_wave = true;
#endif
		/* If the WD switch is false, the IA switch must be false too. */
		assert(wd_switch_on_eop || !ia_switch_on_eop);
	}
	/* If SWITCH_ON_EOI is set, PARTIAL_ES_WAVE must be set too. */
	if (ia_switch_on_eoi)
		partial_es_wave = true;

	/* GS requirement. */
#if 0
	if (SI_GS_PER_ES / primgroup_size >= sctx->screen->gs_table_depth - 3)
		partial_es_wave = true;
#endif

	/* Hw bug with single-primitive instances and SWITCH_ON_EOI
	 * on multi-SE chips. */
#if 0
	if (sctx->b.screen->info.max_se >= 2 && ia_switch_on_eoi &&
	    (info->indirect ||
	     (info->instance_count > 1 &&
	      si_num_prims_for_vertices(info) <= 1)))
		sctx->b.flags |= SI_CONTEXT_VGT_FLUSH;
#endif
	return S_028AA8_SWITCH_ON_EOP(ia_switch_on_eop) |
		S_028AA8_SWITCH_ON_EOI(ia_switch_on_eoi) |
		S_028AA8_PARTIAL_VS_WAVE_ON(partial_vs_wave) |
		S_028AA8_PARTIAL_ES_WAVE_ON(partial_es_wave) |
		S_028AA8_PRIMGROUP_SIZE(primgroup_size - 1) |
		S_028AA8_WD_SWITCH_ON_EOP(chip_class >= CIK ? wd_switch_on_eop : 0) |
		S_028AA8_MAX_PRIMGRP_IN_WAVE(chip_class >= VI ?
					     max_primgroup_in_wave : 0);

}

void
si_emit_cache_flush(struct radv_cmd_buffer *cmd_buffer)
{
	enum chip_class chip_class = cmd_buffer->device->instance->physicalDevice.rad_info.chip_class;
	unsigned cp_coher_cntl = 0;

	radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 128);

	if (cmd_buffer->state.flush_bits & RADV_CMD_FLAG_INV_ICACHE)
		cp_coher_cntl |= S_0085F0_SH_ICACHE_ACTION_ENA(1);
	if (cmd_buffer->state.flush_bits & RADV_CMD_FLAG_INV_SMEM_L1)
		cp_coher_cntl |= S_0085F0_SH_KCACHE_ACTION_ENA(1);
	if (cmd_buffer->state.flush_bits & RADV_CMD_FLAG_INV_VMEM_L1)
		cp_coher_cntl |= S_0085F0_TCL1_ACTION_ENA(1);
	if (cmd_buffer->state.flush_bits & RADV_CMD_FLAG_INV_GLOBAL_L2) {
		cp_coher_cntl |= S_0085F0_TC_ACTION_ENA(1);
		if (chip_class >= VI)
			cp_coher_cntl |= S_0301F0_TC_WB_ACTION_ENA(1);
	}

	if (cmd_buffer->state.flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_CB) {
		cp_coher_cntl |= S_0085F0_CB_ACTION_ENA(1) |
			S_0085F0_CB0_DEST_BASE_ENA(1) |
			S_0085F0_CB1_DEST_BASE_ENA(1) |
			S_0085F0_CB2_DEST_BASE_ENA(1) |
			S_0085F0_CB3_DEST_BASE_ENA(1) |
			S_0085F0_CB4_DEST_BASE_ENA(1) |
			S_0085F0_CB5_DEST_BASE_ENA(1) |
			S_0085F0_CB6_DEST_BASE_ENA(1) |
			S_0085F0_CB7_DEST_BASE_ENA(1);

		/* Necessary for DCC */
		if (cmd_buffer->device->instance->physicalDevice.rad_info.chip_class >= VI) {
			radeon_emit(cmd_buffer->cs, PKT3(PKT3_EVENT_WRITE_EOP, 4, 0));
			radeon_emit(cmd_buffer->cs, EVENT_TYPE(V_028A90_FLUSH_AND_INV_CB_DATA_TS) |
			                            EVENT_INDEX(5));
			radeon_emit(cmd_buffer->cs, 0);
			radeon_emit(cmd_buffer->cs, 0);
			radeon_emit(cmd_buffer->cs, 0);
			radeon_emit(cmd_buffer->cs, 0);
		}
	}

	if (cmd_buffer->state.flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_DB) {
		cp_coher_cntl |= S_0085F0_DB_ACTION_ENA(1) |
			S_0085F0_DB_DEST_BASE_ENA(1);
	}

	if (cmd_buffer->state.flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_CB_META) {
		radeon_emit(cmd_buffer->cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cmd_buffer->cs, EVENT_TYPE(V_028A90_FLUSH_AND_INV_CB_META) | EVENT_INDEX(0));
	}

	if (cmd_buffer->state.flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_DB_META) {
		radeon_emit(cmd_buffer->cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cmd_buffer->cs, EVENT_TYPE(V_028A90_FLUSH_AND_INV_DB_META) | EVENT_INDEX(0));
	}

	if (!(cmd_buffer->state.flush_bits & (RADV_CMD_FLAG_FLUSH_AND_INV_CB |
					      RADV_CMD_FLAG_FLUSH_AND_INV_DB))) {
		if (cmd_buffer->state.flush_bits & RADV_CMD_FLAG_PS_PARTIAL_FLUSH) {
			radeon_emit(cmd_buffer->cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
			radeon_emit(cmd_buffer->cs, EVENT_TYPE(V_028A90_PS_PARTIAL_FLUSH) | EVENT_INDEX(4));
		} else if (cmd_buffer->state.flush_bits & RADV_CMD_FLAG_VS_PARTIAL_FLUSH) {
			radeon_emit(cmd_buffer->cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
			radeon_emit(cmd_buffer->cs, EVENT_TYPE(V_028A90_VS_PARTIAL_FLUSH) | EVENT_INDEX(4));
		}
	}

	if (cmd_buffer->state.flush_bits & RADV_CMD_FLAG_CS_PARTIAL_FLUSH) {
		radeon_emit(cmd_buffer->cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cmd_buffer->cs, EVENT_TYPE(V_028A90_CS_PARTIAL_FLUSH) | EVENT_INDEX(4));
	}

	/* VGT state sync */
	if (cmd_buffer->state.flush_bits & RADV_CMD_FLAG_VGT_FLUSH) {
		radeon_emit(cmd_buffer->cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cmd_buffer->cs, EVENT_TYPE(V_028A90_VGT_FLUSH) | EVENT_INDEX(0));
	}

	/* Make sure ME is idle (it executes most packets) before continuing.
	 * This prevents read-after-write hazards between PFP and ME.
	 */
	if (cp_coher_cntl || (cmd_buffer->state.flush_bits & RADV_CMD_FLAG_CS_PARTIAL_FLUSH)) {
		radeon_emit(cmd_buffer->cs, PKT3(PKT3_PFP_SYNC_ME, 0, 0));
		radeon_emit(cmd_buffer->cs, 0);
	}

	/* When one of the DEST_BASE flags is set, SURFACE_SYNC waits for idle.
	 * Therefore, it should be last. Done in PFP.
	 */
	if (cp_coher_cntl) {
		/* ACQUIRE_MEM is only required on a compute ring. */
		radeon_emit(cmd_buffer->cs, PKT3(PKT3_SURFACE_SYNC, 3, 0));
		radeon_emit(cmd_buffer->cs, cp_coher_cntl);   /* CP_COHER_CNTL */
		radeon_emit(cmd_buffer->cs, 0xffffffff);      /* CP_COHER_SIZE */
		radeon_emit(cmd_buffer->cs, 0);               /* CP_COHER_BASE */
		radeon_emit(cmd_buffer->cs, 0x0000000A);      /* POLL_INTERVAL */
	}

	cmd_buffer->state.flush_bits = 0;
}


/* Set this if you want the 3D engine to wait until CP DMA is done.
 * It should be set on the last CP DMA packet. */
#define R600_CP_DMA_SYNC	(1 << 0) /* R600+ */

/* Set this if the source data was used as a destination in a previous CP DMA
 * packet. It's for preventing a read-after-write (RAW) hazard between two
 * CP DMA packets. */
#define SI_CP_DMA_RAW_WAIT	(1 << 1) /* SI+ */
#define CIK_CP_DMA_USE_L2	(1 << 2)

/* Alignment for optimal performance. */
#define CP_DMA_ALIGNMENT	32
/* The max number of bytes to copy per packet. */
#define CP_DMA_MAX_BYTE_COUNT	((1 << 21) - CP_DMA_ALIGNMENT)

static void si_emit_cp_dma_copy_buffer(struct radv_cmd_buffer *cmd_buffer,
				       uint64_t dst_va, uint64_t src_va,
				       unsigned size, unsigned flags)
{
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	uint32_t sync_flag = flags & R600_CP_DMA_SYNC ? S_411_CP_SYNC(1) : 0;
	uint32_t wr_confirm = !(flags & R600_CP_DMA_SYNC) ? S_414_DISABLE_WR_CONFIRM(1) : 0;
	uint32_t raw_wait = flags & SI_CP_DMA_RAW_WAIT ? S_414_RAW_WAIT(1) : 0;
	uint32_t sel = flags & CIK_CP_DMA_USE_L2 ?
			   S_411_SRC_SEL(V_411_SRC_ADDR_TC_L2) |
			   S_411_DSL_SEL(V_411_DST_ADDR_TC_L2) : 0;

	assert(size);
	assert((size & ((1<<21)-1)) == size);

	radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 9);

	if (cmd_buffer->device->instance->physicalDevice.rad_info.chip_class >= CIK) {
		radeon_emit(cs, PKT3(PKT3_DMA_DATA, 5, 0));
		radeon_emit(cs, sync_flag | sel);	/* CP_SYNC [31] */
		radeon_emit(cs, src_va);		/* SRC_ADDR_LO [31:0] */
		radeon_emit(cs, src_va >> 32);		/* SRC_ADDR_HI [31:0] */
		radeon_emit(cs, dst_va);		/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, dst_va >> 32);		/* DST_ADDR_HI [31:0] */
		radeon_emit(cs, size | wr_confirm | raw_wait);	/* COMMAND [29:22] | BYTE_COUNT [20:0] */
	} else {
		radeon_emit(cs, PKT3(PKT3_CP_DMA, 4, 0));
		radeon_emit(cs, src_va);			/* SRC_ADDR_LO [31:0] */
		radeon_emit(cs, sync_flag | ((src_va >> 32) & 0xffff)); /* CP_SYNC [31] | SRC_ADDR_HI [15:0] */
		radeon_emit(cs, dst_va);			/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, (dst_va >> 32) & 0xffff);	/* DST_ADDR_HI [15:0] */
		radeon_emit(cs, size | wr_confirm | raw_wait);	/* COMMAND [29:22] | BYTE_COUNT [20:0] */
	}

	/* CP DMA is executed in ME, but index buffers are read by PFP.
	 * This ensures that ME (CP DMA) is idle before PFP starts fetching
	 * indices. If we wanted to execute CP DMA in PFP, this packet
	 * should precede it.
	 */
	if (sync_flag) {
		radeon_emit(cs, PKT3(PKT3_PFP_SYNC_ME, 0, 0));
		radeon_emit(cs, 0);
	}
}

/* Emit a CP DMA packet to clear a buffer. The size must fit in bits [20:0]. */
static void si_emit_cp_dma_clear_buffer(struct radv_cmd_buffer *cmd_buffer,
					uint64_t dst_va, unsigned size,
					uint32_t clear_value, unsigned flags)
{
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	uint32_t sync_flag = flags & R600_CP_DMA_SYNC ? S_411_CP_SYNC(1) : 0;
	uint32_t wr_confirm = !(flags & R600_CP_DMA_SYNC) ? S_414_DISABLE_WR_CONFIRM(1) : 0;
	uint32_t raw_wait = flags & SI_CP_DMA_RAW_WAIT ? S_414_RAW_WAIT(1) : 0;
	uint32_t dst_sel = flags & CIK_CP_DMA_USE_L2 ? S_411_DSL_SEL(V_411_DST_ADDR_TC_L2) : 0;

	assert(size);
	assert((size & ((1<<21)-1)) == size);

	radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 9);

	if (cmd_buffer->device->instance->physicalDevice.rad_info.chip_class >= CIK) {
		radeon_emit(cs, PKT3(PKT3_DMA_DATA, 5, 0));
		radeon_emit(cs, sync_flag | dst_sel | S_411_SRC_SEL(V_411_DATA)); /* CP_SYNC [31] | SRC_SEL[30:29] */
		radeon_emit(cs, clear_value);		/* DATA [31:0] */
		radeon_emit(cs, 0);
		radeon_emit(cs, dst_va);		/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, dst_va >> 32);		/* DST_ADDR_HI [15:0] */
		radeon_emit(cs, size | wr_confirm | raw_wait);	/* COMMAND [29:22] | BYTE_COUNT [20:0] */
	} else {
		radeon_emit(cs, PKT3(PKT3_CP_DMA, 4, 0));
		radeon_emit(cs, clear_value);		/* DATA [31:0] */
		radeon_emit(cs, sync_flag | S_411_SRC_SEL(V_411_DATA)); /* CP_SYNC [31] | SRC_SEL[30:29] */
		radeon_emit(cs, dst_va);			/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, (dst_va >> 32) & 0xffff);	/* DST_ADDR_HI [15:0] */
		radeon_emit(cs, size | wr_confirm | raw_wait);	/* COMMAND [29:22] | BYTE_COUNT [20:0] */
	}

	/* See "copy_buffer" for explanation. */
	if (sync_flag) {
		radeon_emit(cs, PKT3(PKT3_PFP_SYNC_ME, 0, 0));
		radeon_emit(cs, 0);
	}
}

static void si_cp_dma_prepare(struct radv_cmd_buffer *cmd_buffer, uint64_t byte_count,
			      uint64_t remaining_size, unsigned *flags)
{

	/* Flush the caches for the first copy only.
	 * Also wait for the previous CP DMA operations.
	 */
	if (cmd_buffer->state.flush_bits) {
		si_emit_cache_flush(cmd_buffer);
		*flags |= SI_CP_DMA_RAW_WAIT;
	}

	/* Do the synchronization after the last dma, so that all data
	 * is written to memory.
	 */
	if (byte_count == remaining_size)
		*flags |= R600_CP_DMA_SYNC;
}

static void si_cp_dma_realign_engine(struct radv_cmd_buffer *cmd_buffer, unsigned size)
{
	uint64_t va;
	uint32_t offset;
	unsigned dma_flags = 0;
	unsigned buf_size = CP_DMA_ALIGNMENT * 2;
	void *ptr;

	assert(size < CP_DMA_ALIGNMENT);

	radv_cmd_buffer_upload_alloc(cmd_buffer, buf_size, CP_DMA_ALIGNMENT,  &offset, &ptr);

	va = cmd_buffer->device->ws->buffer_get_va(cmd_buffer->upload.upload_bo);
	va += offset;

	si_cp_dma_prepare(cmd_buffer, size, size, &dma_flags);

	si_emit_cp_dma_copy_buffer(cmd_buffer, va, va + CP_DMA_ALIGNMENT, size,
				   dma_flags);
}

void si_cp_dma_buffer_copy(struct radv_cmd_buffer *cmd_buffer,
			   uint64_t src_va, uint64_t dest_va,
			   uint64_t size)
{
	uint64_t main_src_va, main_dest_va;
	uint64_t skipped_size = 0, realign_size = 0;


	if (cmd_buffer->device->instance->physicalDevice.rad_info.family <= CHIP_CARRIZO ||
	    cmd_buffer->device->instance->physicalDevice.rad_info.family == CHIP_STONEY) {
		/* If the size is not aligned, we must add a dummy copy at the end
		 * just to align the internal counter. Otherwise, the DMA engine
		 * would slow down by an order of magnitude for following copies.
		 */
		if (size % CP_DMA_ALIGNMENT)
			realign_size = CP_DMA_ALIGNMENT - (size % CP_DMA_ALIGNMENT);

		/* If the copy begins unaligned, we must start copying from the next
		 * aligned block and the skipped part should be copied after everything
		 * else has been copied. Only the src alignment matters, not dst.
		 */
		if (src_va % CP_DMA_ALIGNMENT) {
			skipped_size = CP_DMA_ALIGNMENT - (src_va % CP_DMA_ALIGNMENT);
			/* The main part will be skipped if the size is too small. */
			skipped_size = MIN2(skipped_size, size);
			size -= skipped_size;
		}
	}
	main_src_va = src_va + skipped_size;
	main_dest_va = dest_va + skipped_size;

	while (size) {
		unsigned dma_flags = 0;
		unsigned byte_count = MIN2(size, CP_DMA_MAX_BYTE_COUNT);

		si_cp_dma_prepare(cmd_buffer, byte_count,
				  size + skipped_size + realign_size,
				  &dma_flags);

		si_emit_cp_dma_copy_buffer(cmd_buffer, main_dest_va, main_src_va,
					   byte_count, dma_flags);

		size -= byte_count;
		main_src_va += byte_count;
		main_dest_va += byte_count;
	}

	if (skipped_size) {
		unsigned dma_flags = 0;

		si_cp_dma_prepare(cmd_buffer, skipped_size,
				  size + skipped_size + realign_size,
				  &dma_flags);

		si_emit_cp_dma_copy_buffer(cmd_buffer, dest_va, src_va,
					   skipped_size, dma_flags);
	}
	if (realign_size)
		si_cp_dma_realign_engine(cmd_buffer, realign_size);
}

void si_cp_dma_clear_buffer(struct radv_cmd_buffer *cmd_buffer, uint64_t va,
			    uint64_t size, unsigned value)
{

	if (!size)
		return;

	assert(va % 4 == 0 && size % 4 == 0);

	while (size) {
		unsigned byte_count = MIN2(size, CP_DMA_MAX_BYTE_COUNT);
		unsigned dma_flags = 0;

		si_cp_dma_prepare(cmd_buffer, byte_count, size, &dma_flags);

		/* Emit the clear packet. */
		si_emit_cp_dma_clear_buffer(cmd_buffer, va, byte_count, value,
					    dma_flags);

		size -= byte_count;
		va += byte_count;
	}
}

/* For MSAA sample positions. */
#define FILL_SREG(s0x, s0y, s1x, s1y, s2x, s2y, s3x, s3y)  \
	(((s0x) & 0xf) | (((unsigned)(s0y) & 0xf) << 4) |		   \
	(((unsigned)(s1x) & 0xf) << 8) | (((unsigned)(s1y) & 0xf) << 12) |	   \
	(((unsigned)(s2x) & 0xf) << 16) | (((unsigned)(s2y) & 0xf) << 20) |	   \
	 (((unsigned)(s3x) & 0xf) << 24) | (((unsigned)(s3y) & 0xf) << 28))


/* 2xMSAA
 * There are two locations (4, 4), (-4, -4). */
const uint32_t eg_sample_locs_2x[4] = {
	FILL_SREG(4, 4, -4, -4, 4, 4, -4, -4),
	FILL_SREG(4, 4, -4, -4, 4, 4, -4, -4),
	FILL_SREG(4, 4, -4, -4, 4, 4, -4, -4),
	FILL_SREG(4, 4, -4, -4, 4, 4, -4, -4),
};
const unsigned eg_max_dist_2x = 4;
/* 4xMSAA
 * There are 4 locations: (-2, 6), (6, -2), (-6, 2), (2, 6). */
const uint32_t eg_sample_locs_4x[4] = {
	FILL_SREG(-2, -6, 6, -2, -6, 2, 2, 6),
	FILL_SREG(-2, -6, 6, -2, -6, 2, 2, 6),
	FILL_SREG(-2, -6, 6, -2, -6, 2, 2, 6),
	FILL_SREG(-2, -6, 6, -2, -6, 2, 2, 6),
};
const unsigned eg_max_dist_4x = 6;

/* Cayman 8xMSAA */
static const uint32_t cm_sample_locs_8x[] = {
	FILL_SREG( 1, -3, -1,  3, 5,  1, -3, -5),
	FILL_SREG( 1, -3, -1,  3, 5,  1, -3, -5),
	FILL_SREG( 1, -3, -1,  3, 5,  1, -3, -5),
	FILL_SREG( 1, -3, -1,  3, 5,  1, -3, -5),
	FILL_SREG(-5,  5, -7, -1, 3,  7,  7, -7),
	FILL_SREG(-5,  5, -7, -1, 3,  7,  7, -7),
	FILL_SREG(-5,  5, -7, -1, 3,  7,  7, -7),
	FILL_SREG(-5,  5, -7, -1, 3,  7,  7, -7),
};
static const unsigned cm_max_dist_8x = 8;
/* Cayman 16xMSAA */
static const uint32_t cm_sample_locs_16x[] = {
	FILL_SREG( 1,  1, -1, -3, -3,  2,  4, -1),
	FILL_SREG( 1,  1, -1, -3, -3,  2,  4, -1),
	FILL_SREG( 1,  1, -1, -3, -3,  2,  4, -1),
	FILL_SREG( 1,  1, -1, -3, -3,  2,  4, -1),
	FILL_SREG(-5, -2,  2,  5,  5,  3,  3, -5),
	FILL_SREG(-5, -2,  2,  5,  5,  3,  3, -5),
	FILL_SREG(-5, -2,  2,  5,  5,  3,  3, -5),
	FILL_SREG(-5, -2,  2,  5,  5,  3,  3, -5),
	FILL_SREG(-2,  6,  0, -7, -4, -6, -6,  4),
	FILL_SREG(-2,  6,  0, -7, -4, -6, -6,  4),
	FILL_SREG(-2,  6,  0, -7, -4, -6, -6,  4),
	FILL_SREG(-2,  6,  0, -7, -4, -6, -6,  4),
	FILL_SREG(-8,  0,  7, -4,  6,  7, -7, -8),
	FILL_SREG(-8,  0,  7, -4,  6,  7, -7, -8),
	FILL_SREG(-8,  0,  7, -4,  6,  7, -7, -8),
	FILL_SREG(-8,  0,  7, -4,  6,  7, -7, -8),
};
static const unsigned cm_max_dist_16x = 8;

unsigned radv_cayman_get_maxdist(int log_samples)
{
	unsigned max_dist[] = {
		0,
		eg_max_dist_2x,
		eg_max_dist_4x,
		cm_max_dist_8x,
		cm_max_dist_16x
	};
	return max_dist[log_samples];
}

void radv_cayman_emit_msaa_sample_locs(struct radeon_winsys_cs *cs, int nr_samples)
{
	switch (nr_samples) {
	default:
	case 1:
		radeon_set_context_reg(cs, CM_R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, 0);
		radeon_set_context_reg(cs, CM_R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0, 0);
		radeon_set_context_reg(cs, CM_R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0, 0);
		radeon_set_context_reg(cs, CM_R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0, 0);
		break;
	case 2:
		radeon_set_context_reg(cs, CM_R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, eg_sample_locs_2x[0]);
		radeon_set_context_reg(cs, CM_R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0, eg_sample_locs_2x[1]);
		radeon_set_context_reg(cs, CM_R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0, eg_sample_locs_2x[2]);
		radeon_set_context_reg(cs, CM_R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0, eg_sample_locs_2x[3]);
		break;
	case 4:
		radeon_set_context_reg(cs, CM_R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, eg_sample_locs_4x[0]);
		radeon_set_context_reg(cs, CM_R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0, eg_sample_locs_4x[1]);
		radeon_set_context_reg(cs, CM_R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0, eg_sample_locs_4x[2]);
		radeon_set_context_reg(cs, CM_R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0, eg_sample_locs_4x[3]);
		break;
	case 8:
		radeon_set_context_reg_seq(cs, CM_R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, 14);
		radeon_emit(cs, cm_sample_locs_8x[0]);
		radeon_emit(cs, cm_sample_locs_8x[4]);
		radeon_emit(cs, 0);
		radeon_emit(cs, 0);
		radeon_emit(cs, cm_sample_locs_8x[1]);
		radeon_emit(cs, cm_sample_locs_8x[5]);
		radeon_emit(cs, 0);
		radeon_emit(cs, 0);
		radeon_emit(cs, cm_sample_locs_8x[2]);
		radeon_emit(cs, cm_sample_locs_8x[6]);
		radeon_emit(cs, 0);
		radeon_emit(cs, 0);
		radeon_emit(cs, cm_sample_locs_8x[3]);
		radeon_emit(cs, cm_sample_locs_8x[7]);
		break;
	case 16:
		radeon_set_context_reg_seq(cs, CM_R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, 16);
		radeon_emit(cs, cm_sample_locs_16x[0]);
		radeon_emit(cs, cm_sample_locs_16x[4]);
		radeon_emit(cs, cm_sample_locs_16x[8]);
		radeon_emit(cs, cm_sample_locs_16x[12]);
		radeon_emit(cs, cm_sample_locs_16x[1]);
		radeon_emit(cs, cm_sample_locs_16x[5]);
		radeon_emit(cs, cm_sample_locs_16x[9]);
		radeon_emit(cs, cm_sample_locs_16x[13]);
		radeon_emit(cs, cm_sample_locs_16x[2]);
		radeon_emit(cs, cm_sample_locs_16x[6]);
		radeon_emit(cs, cm_sample_locs_16x[10]);
		radeon_emit(cs, cm_sample_locs_16x[14]);
		radeon_emit(cs, cm_sample_locs_16x[3]);
		radeon_emit(cs, cm_sample_locs_16x[7]);
		radeon_emit(cs, cm_sample_locs_16x[11]);
		radeon_emit(cs, cm_sample_locs_16x[15]);
		break;
	}
}

static void radv_cayman_get_sample_position(struct radv_device *device,
					    unsigned sample_count,
					    unsigned sample_index, float *out_value)
{
	int offset, index;
	struct {
		int idx:4;
	} val;
	switch (sample_count) {
	case 1:
	default:
		out_value[0] = out_value[1] = 0.5;
		break;
	case 2:
		offset = 4 * (sample_index * 2);
		val.idx = (eg_sample_locs_2x[0] >> offset) & 0xf;
		out_value[0] = (float)(val.idx + 8) / 16.0f;
		val.idx = (eg_sample_locs_2x[0] >> (offset + 4)) & 0xf;
		out_value[1] = (float)(val.idx + 8) / 16.0f;
		break;
	case 4:
		offset = 4 * (sample_index * 2);
		val.idx = (eg_sample_locs_4x[0] >> offset) & 0xf;
		out_value[0] = (float)(val.idx + 8) / 16.0f;
		val.idx = (eg_sample_locs_4x[0] >> (offset + 4)) & 0xf;
		out_value[1] = (float)(val.idx + 8) / 16.0f;
		break;
	case 8:
		offset = 4 * (sample_index % 4 * 2);
		index = (sample_index / 4) * 4;
		val.idx = (cm_sample_locs_8x[index] >> offset) & 0xf;
		out_value[0] = (float)(val.idx + 8) / 16.0f;
		val.idx = (cm_sample_locs_8x[index] >> (offset + 4)) & 0xf;
		out_value[1] = (float)(val.idx + 8) / 16.0f;
		break;
	case 16:
		offset = 4 * (sample_index % 4 * 2);
		index = (sample_index / 4) * 4;
		val.idx = (cm_sample_locs_16x[index] >> offset) & 0xf;
		out_value[0] = (float)(val.idx + 8) / 16.0f;
		val.idx = (cm_sample_locs_16x[index] >> (offset + 4)) & 0xf;
		out_value[1] = (float)(val.idx + 8) / 16.0f;
		break;
	}
}

void radv_device_init_msaa(struct radv_device *device)
{
	int i;
	radv_cayman_get_sample_position(device, 1, 0, device->sample_locations_1x[0]);

	for (i = 0; i < 2; i++)
		radv_cayman_get_sample_position(device, 2, i, device->sample_locations_2x[i]);
	for (i = 0; i < 4; i++)
		radv_cayman_get_sample_position(device, 4, i, device->sample_locations_4x[i]);
	for (i = 0; i < 8; i++)
		radv_cayman_get_sample_position(device, 8, i, device->sample_locations_8x[i]);
	for (i = 0; i < 16; i++)
		radv_cayman_get_sample_position(device, 16, i, device->sample_locations_16x[i]);
}
