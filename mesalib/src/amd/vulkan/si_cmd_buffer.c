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
#include "radv_shader.h"
#include "radv_cs.h"
#include "sid.h"
#include "gfx9d.h"
#include "radv_util.h"
#include "main/macros.h"

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
			radeon_set_config_reg(cs, R_00802C_GRBM_GFX_INDEX,
					      S_00802C_SE_INDEX(se) |
					      S_00802C_SH_BROADCAST_WRITES(1) |
					      S_00802C_INSTANCE_BROADCAST_WRITES(1));
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
		radeon_set_config_reg(cs, R_00802C_GRBM_GFX_INDEX,
				      S_00802C_SE_BROADCAST_WRITES(1) |
				      S_00802C_SH_BROADCAST_WRITES(1) |
				      S_00802C_INSTANCE_BROADCAST_WRITES(1));
	else
		radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX,
				       S_030800_SE_BROADCAST_WRITES(1) | S_030800_SH_BROADCAST_WRITES(1) |
				       S_030800_INSTANCE_BROADCAST_WRITES(1));
}

static void
si_emit_compute(struct radv_physical_device *physical_device,
                struct radeon_winsys_cs *cs)
{
	radeon_set_sh_reg_seq(cs, R_00B810_COMPUTE_START_X, 3);
	radeon_emit(cs, 0);
	radeon_emit(cs, 0);
	radeon_emit(cs, 0);

	radeon_set_sh_reg_seq(cs, R_00B854_COMPUTE_RESOURCE_LIMITS,
			      S_00B854_WAVES_PER_SH(0x3));
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

void
si_init_compute(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_physical_device *physical_device = cmd_buffer->device->physical_device;
	si_emit_compute(physical_device, cmd_buffer->cs);
}

/* 12.4 fixed-point */
static unsigned radv_pack_float_12p4(float x)
{
	return x <= 0    ? 0 :
	       x >= 4096 ? 0xffff : x * 16;
}

static void
si_set_raster_config(struct radv_physical_device *physical_device,
		     struct radeon_winsys_cs *cs)
{
	unsigned num_rb = MIN2(physical_device->rad_info.num_render_backends, 16);
	unsigned rb_mask = physical_device->rad_info.enabled_rb_mask;
	unsigned raster_config, raster_config_1;

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
	case CHIP_POLARIS12:
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
			"radv: Unknown GPU, using 0 for raster_config\n");
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
		si_write_harvested_raster_configs(physical_device, cs,
						  raster_config,
						  raster_config_1);
	}
}

static void
si_emit_config(struct radv_physical_device *physical_device,
	       struct radeon_winsys_cs *cs)
{
	int i;

	/* Only SI can disable CLEAR_STATE for now. */
	assert(physical_device->has_clear_state ||
	       physical_device->rad_info.chip_class == SI);

	radeon_emit(cs, PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
	radeon_emit(cs, CONTEXT_CONTROL_LOAD_ENABLE(1));
	radeon_emit(cs, CONTEXT_CONTROL_SHADOW_ENABLE(1));

	if (physical_device->has_clear_state) {
		radeon_emit(cs, PKT3(PKT3_CLEAR_STATE, 0, 0));
		radeon_emit(cs, 0);
	}

	if (physical_device->rad_info.chip_class <= VI)
		si_set_raster_config(physical_device, cs);

	radeon_set_context_reg(cs, R_028A18_VGT_HOS_MAX_TESS_LEVEL, fui(64));
	if (!physical_device->has_clear_state)
		radeon_set_context_reg(cs, R_028A1C_VGT_HOS_MIN_TESS_LEVEL, fui(0));

	/* FIXME calculate these values somehow ??? */
	if (physical_device->rad_info.chip_class <= VI) {
		radeon_set_context_reg(cs, R_028A54_VGT_GS_PER_ES, SI_GS_PER_ES);
		radeon_set_context_reg(cs, R_028A58_VGT_ES_PER_GS, 0x40);
	}

	if (!physical_device->has_clear_state) {
		radeon_set_context_reg(cs, R_028A5C_VGT_GS_PER_VS, 0x2);
		radeon_set_context_reg(cs, R_028A8C_VGT_PRIMITIVEID_RESET, 0x0);
		radeon_set_context_reg(cs, R_028B98_VGT_STRMOUT_BUFFER_CONFIG, 0x0);
	}

	radeon_set_context_reg(cs, R_028AA0_VGT_INSTANCE_STEP_RATE_0, 1);
	if (!physical_device->has_clear_state)
		radeon_set_context_reg(cs, R_028AB8_VGT_VTX_CNT_EN, 0x0);
	if (physical_device->rad_info.chip_class < CIK)
		radeon_set_config_reg(cs, R_008A14_PA_CL_ENHANCE, S_008A14_NUM_CLIP_SEQ(3) |
				      S_008A14_CLIP_VTX_REORDER_ENA(1));

	radeon_set_context_reg(cs, R_028BD4_PA_SC_CENTROID_PRIORITY_0, 0x76543210);
	radeon_set_context_reg(cs, R_028BD8_PA_SC_CENTROID_PRIORITY_1, 0xfedcba98);

	if (!physical_device->has_clear_state)
		radeon_set_context_reg(cs, R_02882C_PA_SU_PRIM_FILTER_CNTL, 0);

	/* CLEAR_STATE doesn't clear these correctly on certain generations.
	 * I don't know why. Deduced by trial and error.
	 */
	if (physical_device->rad_info.chip_class <= CIK) {
		radeon_set_context_reg(cs, R_028B28_VGT_STRMOUT_DRAW_OPAQUE_OFFSET, 0);
		radeon_set_context_reg(cs, R_028204_PA_SC_WINDOW_SCISSOR_TL,
				       S_028204_WINDOW_OFFSET_DISABLE(1));
		radeon_set_context_reg(cs, R_028240_PA_SC_GENERIC_SCISSOR_TL,
				       S_028240_WINDOW_OFFSET_DISABLE(1));
		radeon_set_context_reg(cs, R_028244_PA_SC_GENERIC_SCISSOR_BR,
				       S_028244_BR_X(16384) | S_028244_BR_Y(16384));
		radeon_set_context_reg(cs, R_028030_PA_SC_SCREEN_SCISSOR_TL, 0);
		radeon_set_context_reg(cs, R_028034_PA_SC_SCREEN_SCISSOR_BR,
				       S_028034_BR_X(16384) | S_028034_BR_Y(16384));
	}

	if (!physical_device->has_clear_state) {
		for (i = 0; i < 16; i++) {
			radeon_set_context_reg(cs, R_0282D0_PA_SC_VPORT_ZMIN_0 + i*8, 0);
			radeon_set_context_reg(cs, R_0282D4_PA_SC_VPORT_ZMAX_0 + i*8, fui(1.0));
		}
	}

	if (!physical_device->has_clear_state) {
		radeon_set_context_reg(cs, R_02820C_PA_SC_CLIPRECT_RULE, 0xFFFF);
		radeon_set_context_reg(cs, R_028230_PA_SC_EDGERULE, 0xAAAAAAAA);
		/* PA_SU_HARDWARE_SCREEN_OFFSET must be 0 due to hw bug on SI */
		radeon_set_context_reg(cs, R_028234_PA_SU_HARDWARE_SCREEN_OFFSET, 0);
		radeon_set_context_reg(cs, R_028820_PA_CL_NANINF_CNTL, 0);
		radeon_set_context_reg(cs, R_028AC0_DB_SRESULTS_COMPARE_STATE0, 0x0);
		radeon_set_context_reg(cs, R_028AC4_DB_SRESULTS_COMPARE_STATE1, 0x0);
		radeon_set_context_reg(cs, R_028AC8_DB_PRELOAD_CONTROL, 0x0);
	}

	radeon_set_context_reg(cs, R_02800C_DB_RENDER_OVERRIDE,
			       S_02800C_FORCE_HIS_ENABLE0(V_02800C_FORCE_DISABLE) |
			       S_02800C_FORCE_HIS_ENABLE1(V_02800C_FORCE_DISABLE));

	if (physical_device->rad_info.chip_class >= GFX9) {
		radeon_set_uconfig_reg(cs, R_030920_VGT_MAX_VTX_INDX, ~0);
		radeon_set_uconfig_reg(cs, R_030924_VGT_MIN_VTX_INDX, 0);
		radeon_set_uconfig_reg(cs, R_030928_VGT_INDX_OFFSET, 0);
	} else {
		/* These registers, when written, also overwrite the
		 * CLEAR_STATE context, so we can't rely on CLEAR_STATE setting
		 * them.  It would be an issue if there was another UMD
		 * changing them.
		 */
		radeon_set_context_reg(cs, R_028400_VGT_MAX_VTX_INDX, ~0);
		radeon_set_context_reg(cs, R_028404_VGT_MIN_VTX_INDX, 0);
		radeon_set_context_reg(cs, R_028408_VGT_INDX_OFFSET, 0);
	}

	if (physical_device->rad_info.chip_class >= CIK) {
		if (physical_device->rad_info.chip_class >= GFX9) {
			radeon_set_sh_reg(cs, R_00B41C_SPI_SHADER_PGM_RSRC3_HS,
					  S_00B41C_CU_EN(0xffff) | S_00B41C_WAVE_LIMIT(0x3F));
		} else {
			radeon_set_sh_reg(cs, R_00B51C_SPI_SHADER_PGM_RSRC3_LS,
					  S_00B51C_CU_EN(0xffff) | S_00B51C_WAVE_LIMIT(0x3F));
			radeon_set_sh_reg(cs, R_00B41C_SPI_SHADER_PGM_RSRC3_HS,
					  S_00B41C_WAVE_LIMIT(0x3F));
			radeon_set_sh_reg(cs, R_00B31C_SPI_SHADER_PGM_RSRC3_ES,
					  S_00B31C_CU_EN(0xffff) | S_00B31C_WAVE_LIMIT(0x3F));
			/* If this is 0, Bonaire can hang even if GS isn't being used.
			 * Other chips are unaffected. These are suboptimal values,
			 * but we don't use on-chip GS.
			 */
			radeon_set_context_reg(cs, R_028A44_VGT_GS_ONCHIP_CNTL,
					       S_028A44_ES_VERTS_PER_SUBGRP(64) |
					       S_028A44_GS_PRIMS_PER_SUBGRP(4));
		}
		radeon_set_sh_reg(cs, R_00B21C_SPI_SHADER_PGM_RSRC3_GS,
				  S_00B21C_CU_EN(0xffff) | S_00B21C_WAVE_LIMIT(0x3F));

		if (physical_device->rad_info.num_good_compute_units /
		    (physical_device->rad_info.max_se * physical_device->rad_info.max_sh_per_se) <= 4) {
			/* Too few available compute units per SH. Disallowing
			 * VS to run on CU0 could hurt us more than late VS
			 * allocation would help.
			 *
			 * LATE_ALLOC_VS = 2 is the highest safe number.
			 */
			radeon_set_sh_reg(cs, R_00B118_SPI_SHADER_PGM_RSRC3_VS,
					  S_00B118_CU_EN(0xffff) | S_00B118_WAVE_LIMIT(0x3F) );
			radeon_set_sh_reg(cs, R_00B11C_SPI_SHADER_LATE_ALLOC_VS, S_00B11C_LIMIT(2));
		} else {
			/* Set LATE_ALLOC_VS == 31. It should be less than
			 * the number of scratch waves. Limitations:
			 * - VS can't execute on CU0.
			 * - If HS writes outputs to LDS, LS can't execute on CU0.
			 */
			radeon_set_sh_reg(cs, R_00B118_SPI_SHADER_PGM_RSRC3_VS,
					  S_00B118_CU_EN(0xfffe) | S_00B118_WAVE_LIMIT(0x3F));
			radeon_set_sh_reg(cs, R_00B11C_SPI_SHADER_LATE_ALLOC_VS, S_00B11C_LIMIT(31));
		}

		radeon_set_sh_reg(cs, R_00B01C_SPI_SHADER_PGM_RSRC3_PS,
				  S_00B01C_CU_EN(0xffff) | S_00B01C_WAVE_LIMIT(0x3F));
	}

	if (physical_device->rad_info.chip_class >= VI) {
		uint32_t vgt_tess_distribution;
		radeon_set_context_reg(cs, R_028424_CB_DCC_CONTROL,
				       S_028424_OVERWRITE_COMBINER_MRT_SHARING_DISABLE(1) |
				       S_028424_OVERWRITE_COMBINER_WATERMARK(4));

		vgt_tess_distribution = S_028B50_ACCUM_ISOLINE(32) |
			S_028B50_ACCUM_TRI(11) |
			S_028B50_ACCUM_QUAD(11) |
			S_028B50_DONUT_SPLIT(16);

		if (physical_device->rad_info.family == CHIP_FIJI ||
		    physical_device->rad_info.family >= CHIP_POLARIS10)
			vgt_tess_distribution |= S_028B50_TRAP_SPLIT(3);

		radeon_set_context_reg(cs, R_028B50_VGT_TESS_DISTRIBUTION,
				       vgt_tess_distribution);
	} else if (!physical_device->has_clear_state) {
		radeon_set_context_reg(cs, R_028C58_VGT_VERTEX_REUSE_BLOCK_CNTL, 14);
		radeon_set_context_reg(cs, R_028C5C_VGT_OUT_DEALLOC_CNTL, 16);
	}

	if (physical_device->rad_info.chip_class >= GFX9) {
		unsigned num_se = physical_device->rad_info.max_se;
		unsigned pc_lines = 0;

		switch (physical_device->rad_info.family) {
		case CHIP_VEGA10:
			pc_lines = 4096;
			break;
		case CHIP_RAVEN:
			pc_lines = 1024;
			break;
		default:
			assert(0);
		}

		radeon_set_context_reg(cs, R_028C48_PA_SC_BINNER_CNTL_1,
				       S_028C48_MAX_ALLOC_COUNT(MIN2(128, pc_lines / (4 * num_se))) |
				       S_028C48_MAX_PRIM_PER_BATCH(1023));
		radeon_set_context_reg(cs, R_028C4C_PA_SC_CONSERVATIVE_RASTERIZATION_CNTL,
				       S_028C4C_NULL_SQUAD_AA_MASK_ENABLE(1));
		radeon_set_uconfig_reg(cs, R_030968_VGT_INSTANCE_BASE_ID, 0);
	}

	unsigned tmp = (unsigned)(1.0 * 8.0);
	radeon_set_context_reg_seq(cs, R_028A00_PA_SU_POINT_SIZE, 1);
	radeon_emit(cs, S_028A00_HEIGHT(tmp) | S_028A00_WIDTH(tmp));
	radeon_set_context_reg_seq(cs, R_028A04_PA_SU_POINT_MINMAX, 1);
	radeon_emit(cs, S_028A04_MIN_SIZE(radv_pack_float_12p4(0)) |
		    S_028A04_MAX_SIZE(radv_pack_float_12p4(8192/2)));

	if (!physical_device->has_clear_state) {
		radeon_set_context_reg(cs, R_028004_DB_COUNT_CONTROL,
				       S_028004_ZPASS_INCREMENT_DISABLE(1));
	}

	si_emit_compute(physical_device, cs);
}

void si_init_config(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_physical_device *physical_device = cmd_buffer->device->physical_device;

	si_emit_config(physical_device, cmd_buffer->cs);
}

void
cik_create_gfx_config(struct radv_device *device)
{
	struct radeon_winsys_cs *cs = device->ws->cs_create(device->ws, RING_GFX);
	if (!cs)
		return;

	si_emit_config(device->physical_device, cs);

	while (cs->cdw & 7) {
		if (device->physical_device->rad_info.gfx_ib_pad_with_type2)
			radeon_emit(cs, 0x80000000);
		else
			radeon_emit(cs, 0xffff1000);
	}

	device->gfx_init = device->ws->buffer_create(device->ws,
						     cs->cdw * 4, 4096,
						     RADEON_DOMAIN_GTT,
						     RADEON_FLAG_CPU_ACCESS|
						     RADEON_FLAG_NO_INTERPROCESS_SHARING |
						     RADEON_FLAG_READ_ONLY);
	if (!device->gfx_init)
		goto fail;

	void *map = device->ws->buffer_map(device->gfx_init);
	if (!map) {
		device->ws->buffer_destroy(device->gfx_init);
		device->gfx_init = NULL;
		goto fail;
	}
	memcpy(map, cs->buf, cs->cdw * 4);

	device->ws->buffer_unmap(device->gfx_init);
	device->gfx_init_size_dw = cs->cdw;
fail:
	device->ws->cs_destroy(cs);
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

	assert(count);
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

	radeon_set_context_reg_seq(cs, R_0282D0_PA_SC_VPORT_ZMIN_0 +
				   first_vp * 4 * 2, count * 2);
	for (i = 0; i < count; i++) {
		float zmin = MIN2(viewports[i].minDepth, viewports[i].maxDepth);
		float zmax = MAX2(viewports[i].minDepth, viewports[i].maxDepth);
		radeon_emit(cs, fui(zmin));
		radeon_emit(cs, fui(zmax));
	}
}

static VkRect2D si_scissor_from_viewport(const VkViewport *viewport)
{
	float scale[3], translate[3];
	VkRect2D rect;

	get_viewport_xform(viewport, scale, translate);

	rect.offset.x = translate[0] - abs(scale[0]);
	rect.offset.y = translate[1] - abs(scale[1]);
	rect.extent.width = ceilf(translate[0] + abs(scale[0])) - rect.offset.x;
	rect.extent.height = ceilf(translate[1] + abs(scale[1])) - rect.offset.y;

	return rect;
}

static VkRect2D si_intersect_scissor(const VkRect2D *a, const VkRect2D *b) {
	VkRect2D ret;
	ret.offset.x = MAX2(a->offset.x, b->offset.x);
	ret.offset.y = MAX2(a->offset.y, b->offset.y);
	ret.extent.width = MIN2(a->offset.x + a->extent.width,
	                        b->offset.x + b->extent.width) - ret.offset.x;
	ret.extent.height = MIN2(a->offset.y + a->extent.height,
	                         b->offset.y + b->extent.height) - ret.offset.y;
	return ret;
}

void
si_write_scissors(struct radeon_winsys_cs *cs, int first,
                  int count, const VkRect2D *scissors,
                  const VkViewport *viewports, bool can_use_guardband)
{
	int i;
	float scale[3], translate[3], guardband_x = INFINITY, guardband_y = INFINITY;
	const float max_range = 32767.0f;
	if (!count)
		return;

	radeon_set_context_reg_seq(cs, R_028250_PA_SC_VPORT_SCISSOR_0_TL + first * 4 * 2, count * 2);
	for (i = 0; i < count; i++) {
		VkRect2D viewport_scissor = si_scissor_from_viewport(viewports + i);
		VkRect2D scissor = si_intersect_scissor(&scissors[i], &viewport_scissor);

		get_viewport_xform(viewports + i, scale, translate);
		scale[0] = abs(scale[0]);
		scale[1] = abs(scale[1]);

		if (scale[0] < 0.5)
			scale[0] = 0.5;
		if (scale[1] < 0.5)
			scale[1] = 0.5;

		guardband_x = MIN2(guardband_x, (max_range - abs(translate[0])) / scale[0]);
		guardband_y = MIN2(guardband_y, (max_range - abs(translate[1])) / scale[1]);

		radeon_emit(cs, S_028250_TL_X(scissor.offset.x) |
			    S_028250_TL_Y(scissor.offset.y) |
			    S_028250_WINDOW_OFFSET_DISABLE(1));
		radeon_emit(cs, S_028254_BR_X(scissor.offset.x + scissor.extent.width) |
			    S_028254_BR_Y(scissor.offset.y + scissor.extent.height));
	}
	if (!can_use_guardband) {
		guardband_x = 1.0;
		guardband_y = 1.0;
	}

	radeon_set_context_reg_seq(cs, R_028BE8_PA_CL_GB_VERT_CLIP_ADJ, 4);
	radeon_emit(cs, fui(guardband_y));
	radeon_emit(cs, fui(1.0));
	radeon_emit(cs, fui(guardband_x));
	radeon_emit(cs, fui(1.0));
}

static inline unsigned
radv_prims_for_vertices(struct radv_prim_vertex_count *info, unsigned num)
{
	if (num == 0)
		return 0;

	if (info->incr == 0)
		return 0;

	if (num < info->min)
		return 0;

	return 1 + ((num - info->min) / info->incr);
}

uint32_t
si_get_ia_multi_vgt_param(struct radv_cmd_buffer *cmd_buffer,
			  bool instanced_draw, bool indirect_draw,
			  uint32_t draw_vertex_count)
{
	enum chip_class chip_class = cmd_buffer->device->physical_device->rad_info.chip_class;
	enum radeon_family family = cmd_buffer->device->physical_device->rad_info.family;
	struct radeon_info *info = &cmd_buffer->device->physical_device->rad_info;
	const unsigned max_primgroup_in_wave = 2;
	/* SWITCH_ON_EOP(0) is always preferable. */
	bool wd_switch_on_eop = false;
	bool ia_switch_on_eop = false;
	bool ia_switch_on_eoi = false;
	bool partial_vs_wave = false;
	bool partial_es_wave = cmd_buffer->state.pipeline->graphics.ia_multi_vgt_param.partial_es_wave;
	bool multi_instances_smaller_than_primgroup;

	multi_instances_smaller_than_primgroup = indirect_draw;
	if (!multi_instances_smaller_than_primgroup && instanced_draw) {
		uint32_t num_prims = radv_prims_for_vertices(&cmd_buffer->state.pipeline->graphics.prim_vertex_count, draw_vertex_count);
		if (num_prims < cmd_buffer->state.pipeline->graphics.ia_multi_vgt_param.primgroup_size)
			multi_instances_smaller_than_primgroup = true;
	}

	ia_switch_on_eoi = cmd_buffer->state.pipeline->graphics.ia_multi_vgt_param.ia_switch_on_eoi;
	partial_vs_wave = cmd_buffer->state.pipeline->graphics.ia_multi_vgt_param.partial_vs_wave;

	if (chip_class >= CIK) {
		wd_switch_on_eop = cmd_buffer->state.pipeline->graphics.ia_multi_vgt_param.wd_switch_on_eop;

		/* Hawaii hangs if instancing is enabled and WD_SWITCH_ON_EOP is 0.
		 * We don't know that for indirect drawing, so treat it as
		 * always problematic. */
		if (family == CHIP_HAWAII &&
		    (instanced_draw || indirect_draw))
			wd_switch_on_eop = true;

		/* Performance recommendation for 4 SE Gfx7-8 parts if
		 * instances are smaller than a primgroup.
		 * Assume indirect draws always use small instances.
		 * This is needed for good VS wave utilization.
		 */
		if (chip_class <= VI &&
		    info->max_se == 4 &&
		    multi_instances_smaller_than_primgroup)
			wd_switch_on_eop = true;

		/* Required on CIK and later. */
		if (info->max_se > 2 && !wd_switch_on_eop)
			ia_switch_on_eoi = true;

		/* Required by Hawaii and, for some special cases, by VI. */
		if (ia_switch_on_eoi &&
		    (family == CHIP_HAWAII ||
		     (chip_class == VI &&
		      /* max primgroup in wave is always 2 - leave this for documentation */
		      (radv_pipeline_has_gs(cmd_buffer->state.pipeline) || max_primgroup_in_wave != 2))))
			partial_vs_wave = true;

		/* Instancing bug on Bonaire. */
		if (family == CHIP_BONAIRE && ia_switch_on_eoi &&
		    (instanced_draw || indirect_draw))
			partial_vs_wave = true;

		/* If the WD switch is false, the IA switch must be false too. */
		assert(wd_switch_on_eop || !ia_switch_on_eop);
	}
	/* If SWITCH_ON_EOI is set, PARTIAL_ES_WAVE must be set too. */
	if (chip_class <= VI && ia_switch_on_eoi)
		partial_es_wave = true;

	if (radv_pipeline_has_gs(cmd_buffer->state.pipeline)) {
		/* GS hw bug with single-primitive instances and SWITCH_ON_EOI.
		 * The hw doc says all multi-SE chips are affected, but amdgpu-pro Vulkan
		 * only applies it to Hawaii. Do what amdgpu-pro Vulkan does.
		 */
		if (family == CHIP_HAWAII && ia_switch_on_eoi) {
			bool set_vgt_flush = indirect_draw;
			if (!set_vgt_flush && instanced_draw) {
				uint32_t num_prims = radv_prims_for_vertices(&cmd_buffer->state.pipeline->graphics.prim_vertex_count, draw_vertex_count);
				if (num_prims <= 1)
					set_vgt_flush = true;
			}
			if (set_vgt_flush)
				cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_VGT_FLUSH;
		}
	}

	return cmd_buffer->state.pipeline->graphics.ia_multi_vgt_param.base |
		S_028AA8_SWITCH_ON_EOP(ia_switch_on_eop) |
		S_028AA8_SWITCH_ON_EOI(ia_switch_on_eoi) |
		S_028AA8_PARTIAL_VS_WAVE_ON(partial_vs_wave) |
		S_028AA8_PARTIAL_ES_WAVE_ON(partial_es_wave) |
		S_028AA8_WD_SWITCH_ON_EOP(chip_class >= CIK ? wd_switch_on_eop : 0);

}

void si_cs_emit_write_event_eop(struct radeon_winsys_cs *cs,
				bool predicated,
				enum chip_class chip_class,
				bool is_mec,
				unsigned event, unsigned event_flags,
				unsigned data_sel,
				uint64_t va,
				uint32_t old_fence,
				uint32_t new_fence)
{
	unsigned op = EVENT_TYPE(event) |
		EVENT_INDEX(5) |
		event_flags;
	unsigned is_gfx8_mec = is_mec && chip_class < GFX9;

	if (chip_class >= GFX9 || is_gfx8_mec) {
		radeon_emit(cs, PKT3(PKT3_RELEASE_MEM, is_gfx8_mec ? 5 : 6, predicated));
		radeon_emit(cs, op);
		radeon_emit(cs, EOP_DATA_SEL(data_sel));
		radeon_emit(cs, va);            /* address lo */
		radeon_emit(cs, va >> 32);      /* address hi */
		radeon_emit(cs, new_fence);     /* immediate data lo */
		radeon_emit(cs, 0); /* immediate data hi */
		if (!is_gfx8_mec)
			radeon_emit(cs, 0); /* unused */
	} else {
		if (chip_class == CIK ||
		    chip_class == VI) {
			/* Two EOP events are required to make all engines go idle
			 * (and optional cache flushes executed) before the timestamp
			 * is written.
			 */
			radeon_emit(cs, PKT3(PKT3_EVENT_WRITE_EOP, 4, predicated));
			radeon_emit(cs, op);
			radeon_emit(cs, va);
			radeon_emit(cs, ((va >> 32) & 0xffff) | EOP_DATA_SEL(data_sel));
			radeon_emit(cs, old_fence); /* immediate data */
			radeon_emit(cs, 0); /* unused */
		}

		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE_EOP, 4, predicated));
		radeon_emit(cs, op);
		radeon_emit(cs, va);
		radeon_emit(cs, ((va >> 32) & 0xffff) | EOP_DATA_SEL(data_sel));
		radeon_emit(cs, new_fence); /* immediate data */
		radeon_emit(cs, 0); /* unused */
	}
}

void
si_emit_wait_fence(struct radeon_winsys_cs *cs,
		   bool predicated,
		   uint64_t va, uint32_t ref,
		   uint32_t mask)
{
	radeon_emit(cs, PKT3(PKT3_WAIT_REG_MEM, 5, predicated));
	radeon_emit(cs, WAIT_REG_MEM_EQUAL | WAIT_REG_MEM_MEM_SPACE(1));
	radeon_emit(cs, va);
	radeon_emit(cs, va >> 32);
	radeon_emit(cs, ref); /* reference value */
	radeon_emit(cs, mask); /* mask */
	radeon_emit(cs, 4); /* poll interval */
}

static void
si_emit_acquire_mem(struct radeon_winsys_cs *cs,
                    bool is_mec,
		    bool predicated,
		    bool is_gfx9,
                    unsigned cp_coher_cntl)
{
	if (is_mec || is_gfx9) {
		uint32_t hi_val = is_gfx9 ? 0xffffff : 0xff;
		radeon_emit(cs, PKT3(PKT3_ACQUIRE_MEM, 5, predicated) |
		                            PKT3_SHADER_TYPE_S(is_mec));
		radeon_emit(cs, cp_coher_cntl);   /* CP_COHER_CNTL */
		radeon_emit(cs, 0xffffffff);      /* CP_COHER_SIZE */
		radeon_emit(cs, hi_val);          /* CP_COHER_SIZE_HI */
		radeon_emit(cs, 0);               /* CP_COHER_BASE */
		radeon_emit(cs, 0);               /* CP_COHER_BASE_HI */
		radeon_emit(cs, 0x0000000A);      /* POLL_INTERVAL */
	} else {
		/* ACQUIRE_MEM is only required on a compute ring. */
		radeon_emit(cs, PKT3(PKT3_SURFACE_SYNC, 3, predicated));
		radeon_emit(cs, cp_coher_cntl);   /* CP_COHER_CNTL */
		radeon_emit(cs, 0xffffffff);      /* CP_COHER_SIZE */
		radeon_emit(cs, 0);               /* CP_COHER_BASE */
		radeon_emit(cs, 0x0000000A);      /* POLL_INTERVAL */
	}
}

void
si_cs_emit_cache_flush(struct radeon_winsys_cs *cs,
                       enum chip_class chip_class,
		       uint32_t *flush_cnt,
		       uint64_t flush_va,
                       bool is_mec,
                       enum radv_cmd_flush_bits flush_bits)
{
	unsigned cp_coher_cntl = 0;
	uint32_t flush_cb_db = flush_bits & (RADV_CMD_FLAG_FLUSH_AND_INV_CB |
					     RADV_CMD_FLAG_FLUSH_AND_INV_DB);
	
	if (flush_bits & RADV_CMD_FLAG_INV_ICACHE)
		cp_coher_cntl |= S_0085F0_SH_ICACHE_ACTION_ENA(1);
	if (flush_bits & RADV_CMD_FLAG_INV_SMEM_L1)
		cp_coher_cntl |= S_0085F0_SH_KCACHE_ACTION_ENA(1);

	if (chip_class <= VI) {
		if (flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_CB) {
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
			if (chip_class >= VI) {
				si_cs_emit_write_event_eop(cs,
							   false,
							   chip_class,
							   is_mec,
							   V_028A90_FLUSH_AND_INV_CB_DATA_TS,
							   0, 0, 0, 0, 0);
			}
		}
		if (flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_DB) {
			cp_coher_cntl |= S_0085F0_DB_ACTION_ENA(1) |
				S_0085F0_DB_DEST_BASE_ENA(1);
		}
	}

	if (flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_CB_META) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_FLUSH_AND_INV_CB_META) | EVENT_INDEX(0));
	}

	if (flush_bits & RADV_CMD_FLAG_FLUSH_AND_INV_DB_META) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_FLUSH_AND_INV_DB_META) | EVENT_INDEX(0));
	}

	if (flush_bits & RADV_CMD_FLAG_PS_PARTIAL_FLUSH) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_PS_PARTIAL_FLUSH) | EVENT_INDEX(4));
	} else if (flush_bits & RADV_CMD_FLAG_VS_PARTIAL_FLUSH) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_VS_PARTIAL_FLUSH) | EVENT_INDEX(4));
	}

	if (flush_bits & RADV_CMD_FLAG_CS_PARTIAL_FLUSH) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_CS_PARTIAL_FLUSH) | EVENT_INDEX(4));
	}

	if (chip_class >= GFX9 && flush_cb_db) {
		unsigned cb_db_event, tc_flags;

#if 0
		/* This breaks a bunch of:
		   dEQP-VK.renderpass.dedicated_allocation.formats.d32_sfloat_s8_uint.input*.
		   use the big hammer always.
		*/
		/* Set the CB/DB flush event. */
		switch (flush_cb_db) {
		case RADV_CMD_FLAG_FLUSH_AND_INV_CB:
			cb_db_event = V_028A90_FLUSH_AND_INV_CB_DATA_TS;
			break;
		case RADV_CMD_FLAG_FLUSH_AND_INV_DB:
			cb_db_event = V_028A90_FLUSH_AND_INV_DB_DATA_TS;
			break;
		default:
			/* both CB & DB */
			cb_db_event = V_028A90_CACHE_FLUSH_AND_INV_TS_EVENT;
		}
#else
		cb_db_event = V_028A90_CACHE_FLUSH_AND_INV_TS_EVENT;
#endif
		/* These are the only allowed combinations. If you need to
		 * do multiple operations at once, do them separately.
		 * All operations that invalidate L2 also seem to invalidate
		 * metadata. Volatile (VOL) and WC flushes are not listed here.
		 *
		 * TC    | TC_WB         = writeback & invalidate L2 & L1
		 * TC    | TC_WB | TC_NC = writeback & invalidate L2 for MTYPE == NC
		 *         TC_WB | TC_NC = writeback L2 for MTYPE == NC
		 * TC            | TC_NC = invalidate L2 for MTYPE == NC
		 * TC    | TC_MD         = writeback & invalidate L2 metadata (DCC, etc.)
		 * TCL1                  = invalidate L1
		 */
		tc_flags = EVENT_TC_ACTION_ENA |
		           EVENT_TC_MD_ACTION_ENA;

		/* Ideally flush TC together with CB/DB. */
		if (flush_bits & RADV_CMD_FLAG_INV_GLOBAL_L2) {
			/* Writeback and invalidate everything in L2 & L1. */
			tc_flags = EVENT_TC_ACTION_ENA |
			           EVENT_TC_WB_ACTION_ENA;


			/* Clear the flags. */
		        flush_bits &= ~(RADV_CMD_FLAG_INV_GLOBAL_L2 |
					 RADV_CMD_FLAG_WRITEBACK_GLOBAL_L2 |
					 RADV_CMD_FLAG_INV_VMEM_L1);
		}
		assert(flush_cnt);
		uint32_t old_fence = (*flush_cnt)++;

		si_cs_emit_write_event_eop(cs, false, chip_class, false, cb_db_event, tc_flags, 1,
					   flush_va, old_fence, *flush_cnt);
		si_emit_wait_fence(cs, false, flush_va, *flush_cnt, 0xffffffff);
	}

	/* VGT state sync */
	if (flush_bits & RADV_CMD_FLAG_VGT_FLUSH) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_VGT_FLUSH) | EVENT_INDEX(0));
	}

	/* Make sure ME is idle (it executes most packets) before continuing.
	 * This prevents read-after-write hazards between PFP and ME.
	 */
	if ((cp_coher_cntl ||
	     (flush_bits & (RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
			    RADV_CMD_FLAG_INV_VMEM_L1 |
			    RADV_CMD_FLAG_INV_GLOBAL_L2 |
			    RADV_CMD_FLAG_WRITEBACK_GLOBAL_L2))) &&
	    !is_mec) {
		radeon_emit(cs, PKT3(PKT3_PFP_SYNC_ME, 0, 0));
		radeon_emit(cs, 0);
	}

	if ((flush_bits & RADV_CMD_FLAG_INV_GLOBAL_L2) ||
	    (chip_class <= CIK && (flush_bits & RADV_CMD_FLAG_WRITEBACK_GLOBAL_L2))) {
		si_emit_acquire_mem(cs, is_mec, false, chip_class >= GFX9,
				    cp_coher_cntl |
				    S_0085F0_TC_ACTION_ENA(1) |
				    S_0085F0_TCL1_ACTION_ENA(1) |
				    S_0301F0_TC_WB_ACTION_ENA(chip_class >= VI));
		cp_coher_cntl = 0;
	} else {
		if(flush_bits & RADV_CMD_FLAG_WRITEBACK_GLOBAL_L2) {
			/* WB = write-back
			 * NC = apply to non-coherent MTYPEs
			 *      (i.e. MTYPE <= 1, which is what we use everywhere)
			 *
			 * WB doesn't work without NC.
			 */
			si_emit_acquire_mem(cs, is_mec, false,
					    chip_class >= GFX9,
					    cp_coher_cntl |
					    S_0301F0_TC_WB_ACTION_ENA(1) |
					    S_0301F0_TC_NC_ACTION_ENA(1));
			cp_coher_cntl = 0;
		}
		if (flush_bits & RADV_CMD_FLAG_INV_VMEM_L1) {
			si_emit_acquire_mem(cs, is_mec,
					    false, chip_class >= GFX9,
					    cp_coher_cntl |
					    S_0085F0_TCL1_ACTION_ENA(1));
			cp_coher_cntl = 0;
		}
	}

	/* When one of the DEST_BASE flags is set, SURFACE_SYNC waits for idle.
	 * Therefore, it should be last. Done in PFP.
	 */
	if (cp_coher_cntl)
		si_emit_acquire_mem(cs, is_mec, false, chip_class >= GFX9, cp_coher_cntl);
}

void
si_emit_cache_flush(struct radv_cmd_buffer *cmd_buffer)
{
	bool is_compute = cmd_buffer->queue_family_index == RADV_QUEUE_COMPUTE;

	if (is_compute)
		cmd_buffer->state.flush_bits &= ~(RADV_CMD_FLAG_FLUSH_AND_INV_CB |
	                                          RADV_CMD_FLAG_FLUSH_AND_INV_CB_META |
	                                          RADV_CMD_FLAG_FLUSH_AND_INV_DB |
	                                          RADV_CMD_FLAG_FLUSH_AND_INV_DB_META |
	                                          RADV_CMD_FLAG_PS_PARTIAL_FLUSH |
	                                          RADV_CMD_FLAG_VS_PARTIAL_FLUSH |
	                                          RADV_CMD_FLAG_VGT_FLUSH);

	if (!cmd_buffer->state.flush_bits)
		return;

	enum chip_class chip_class = cmd_buffer->device->physical_device->rad_info.chip_class;
	radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 128);

	uint32_t *ptr = NULL;
	uint64_t va = 0;
	if (chip_class == GFX9) {
		va = radv_buffer_get_va(cmd_buffer->gfx9_fence_bo) + cmd_buffer->gfx9_fence_offset;
		ptr = &cmd_buffer->gfx9_fence_idx;
	}
	si_cs_emit_cache_flush(cmd_buffer->cs,
	                       cmd_buffer->device->physical_device->rad_info.chip_class,
			       ptr, va,
	                       radv_cmd_buffer_uses_mec(cmd_buffer),
	                       cmd_buffer->state.flush_bits);


	if (unlikely(cmd_buffer->device->trace_bo))
		radv_cmd_buffer_trace_emit(cmd_buffer);

	cmd_buffer->state.flush_bits = 0;
}

/* sets the CP predication state using a boolean stored at va */
void
si_emit_set_predication_state(struct radv_cmd_buffer *cmd_buffer, uint64_t va)
{
	uint32_t op = 0;

	if (va)
		op = PRED_OP(PREDICATION_OP_BOOL64) | PREDICATION_DRAW_VISIBLE;
	if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX9) {
		radeon_emit(cmd_buffer->cs, PKT3(PKT3_SET_PREDICATION, 2, 0));
		radeon_emit(cmd_buffer->cs, op);
		radeon_emit(cmd_buffer->cs, va);
		radeon_emit(cmd_buffer->cs, va >> 32);
	} else {
		radeon_emit(cmd_buffer->cs, PKT3(PKT3_SET_PREDICATION, 1, 0));
		radeon_emit(cmd_buffer->cs, va);
		radeon_emit(cmd_buffer->cs, op | ((va >> 32) & 0xFF));
	}
}

/* Set this if you want the 3D engine to wait until CP DMA is done.
 * It should be set on the last CP DMA packet. */
#define CP_DMA_SYNC	(1 << 0)

/* Set this if the source data was used as a destination in a previous CP DMA
 * packet. It's for preventing a read-after-write (RAW) hazard between two
 * CP DMA packets. */
#define CP_DMA_RAW_WAIT	(1 << 1)
#define CP_DMA_USE_L2	(1 << 2)
#define CP_DMA_CLEAR	(1 << 3)

/* Alignment for optimal performance. */
#define SI_CPDMA_ALIGNMENT	32

/* The max number of bytes that can be copied per packet. */
static inline unsigned cp_dma_max_byte_count(struct radv_cmd_buffer *cmd_buffer)
{
	unsigned max = cmd_buffer->device->physical_device->rad_info.chip_class >= GFX9 ?
			       S_414_BYTE_COUNT_GFX9(~0u) :
			       S_414_BYTE_COUNT_GFX6(~0u);

	/* make it aligned for optimal performance */
	return max & ~(SI_CPDMA_ALIGNMENT - 1);
}

/* Emit a CP DMA packet to do a copy from one buffer to another, or to clear
 * a buffer. The size must fit in bits [20:0]. If CP_DMA_CLEAR is set, src_va is a 32-bit
 * clear value.
 */
static void si_emit_cp_dma(struct radv_cmd_buffer *cmd_buffer,
			   uint64_t dst_va, uint64_t src_va,
			   unsigned size, unsigned flags)
{
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	uint32_t header = 0, command = 0;

	assert(size);
	assert(size <= cp_dma_max_byte_count(cmd_buffer));

	radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, 9);
	if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX9)
		command |= S_414_BYTE_COUNT_GFX9(size);
	else
		command |= S_414_BYTE_COUNT_GFX6(size);

	/* Sync flags. */
	if (flags & CP_DMA_SYNC)
		header |= S_411_CP_SYNC(1);
	else {
		if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX9)
			command |= S_414_DISABLE_WR_CONFIRM_GFX9(1);
		else
			command |= S_414_DISABLE_WR_CONFIRM_GFX6(1);
	}

	if (flags & CP_DMA_RAW_WAIT)
		command |= S_414_RAW_WAIT(1);

	/* Src and dst flags. */
	if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX9 &&
	    !(flags & CP_DMA_CLEAR) &&
	    src_va == dst_va)
		header |= S_411_DSL_SEL(V_411_NOWHERE); /* prefetch only */
	else if (flags & CP_DMA_USE_L2)
		header |= S_411_DSL_SEL(V_411_DST_ADDR_TC_L2);

	if (flags & CP_DMA_CLEAR)
		header |= S_411_SRC_SEL(V_411_DATA);
	else if (flags & CP_DMA_USE_L2)
		header |= S_411_SRC_SEL(V_411_SRC_ADDR_TC_L2);

	if (cmd_buffer->device->physical_device->rad_info.chip_class >= CIK) {
		radeon_emit(cs, PKT3(PKT3_DMA_DATA, 5, cmd_buffer->state.predicating));
		radeon_emit(cs, header);
		radeon_emit(cs, src_va);		/* SRC_ADDR_LO [31:0] */
		radeon_emit(cs, src_va >> 32);		/* SRC_ADDR_HI [31:0] */
		radeon_emit(cs, dst_va);		/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, dst_va >> 32);		/* DST_ADDR_HI [31:0] */
		radeon_emit(cs, command);
	} else {
		assert(!(flags & CP_DMA_USE_L2));
		header |= S_411_SRC_ADDR_HI(src_va >> 32);
		radeon_emit(cs, PKT3(PKT3_CP_DMA, 4, cmd_buffer->state.predicating));
		radeon_emit(cs, src_va);			/* SRC_ADDR_LO [31:0] */
		radeon_emit(cs, header);			/* SRC_ADDR_HI [15:0] + flags. */
		radeon_emit(cs, dst_va);			/* DST_ADDR_LO [31:0] */
		radeon_emit(cs, (dst_va >> 32) & 0xffff);	/* DST_ADDR_HI [15:0] */
		radeon_emit(cs, command);
	}

	/* CP DMA is executed in ME, but index buffers are read by PFP.
	 * This ensures that ME (CP DMA) is idle before PFP starts fetching
	 * indices. If we wanted to execute CP DMA in PFP, this packet
	 * should precede it.
	 */
	if ((flags & CP_DMA_SYNC) && cmd_buffer->queue_family_index == RADV_QUEUE_GENERAL) {
		radeon_emit(cs, PKT3(PKT3_PFP_SYNC_ME, 0, cmd_buffer->state.predicating));
		radeon_emit(cs, 0);
	}

	if (unlikely(cmd_buffer->device->trace_bo))
		radv_cmd_buffer_trace_emit(cmd_buffer);
}

void si_cp_dma_prefetch(struct radv_cmd_buffer *cmd_buffer, uint64_t va,
                        unsigned size)
{
	uint64_t aligned_va = va & ~(SI_CPDMA_ALIGNMENT - 1);
	uint64_t aligned_size = ((va + size + SI_CPDMA_ALIGNMENT -1) & ~(SI_CPDMA_ALIGNMENT - 1)) - aligned_va;

	si_emit_cp_dma(cmd_buffer, aligned_va, aligned_va,
		       aligned_size, CP_DMA_USE_L2);
}

static void si_cp_dma_prepare(struct radv_cmd_buffer *cmd_buffer, uint64_t byte_count,
			      uint64_t remaining_size, unsigned *flags)
{

	/* Flush the caches for the first copy only.
	 * Also wait for the previous CP DMA operations.
	 */
	if (cmd_buffer->state.flush_bits) {
		si_emit_cache_flush(cmd_buffer);
		*flags |= CP_DMA_RAW_WAIT;
	}

	/* Do the synchronization after the last dma, so that all data
	 * is written to memory.
	 */
	if (byte_count == remaining_size)
		*flags |= CP_DMA_SYNC;
}

static void si_cp_dma_realign_engine(struct radv_cmd_buffer *cmd_buffer, unsigned size)
{
	uint64_t va;
	uint32_t offset;
	unsigned dma_flags = 0;
	unsigned buf_size = SI_CPDMA_ALIGNMENT * 2;
	void *ptr;

	assert(size < SI_CPDMA_ALIGNMENT);

	radv_cmd_buffer_upload_alloc(cmd_buffer, buf_size, SI_CPDMA_ALIGNMENT,  &offset, &ptr);

	va = radv_buffer_get_va(cmd_buffer->upload.upload_bo);
	va += offset;

	si_cp_dma_prepare(cmd_buffer, size, size, &dma_flags);

	si_emit_cp_dma(cmd_buffer, va, va + SI_CPDMA_ALIGNMENT, size,
		       dma_flags);
}

void si_cp_dma_buffer_copy(struct radv_cmd_buffer *cmd_buffer,
			   uint64_t src_va, uint64_t dest_va,
			   uint64_t size)
{
	uint64_t main_src_va, main_dest_va;
	uint64_t skipped_size = 0, realign_size = 0;


	if (cmd_buffer->device->physical_device->rad_info.family <= CHIP_CARRIZO ||
	    cmd_buffer->device->physical_device->rad_info.family == CHIP_STONEY) {
		/* If the size is not aligned, we must add a dummy copy at the end
		 * just to align the internal counter. Otherwise, the DMA engine
		 * would slow down by an order of magnitude for following copies.
		 */
		if (size % SI_CPDMA_ALIGNMENT)
			realign_size = SI_CPDMA_ALIGNMENT - (size % SI_CPDMA_ALIGNMENT);

		/* If the copy begins unaligned, we must start copying from the next
		 * aligned block and the skipped part should be copied after everything
		 * else has been copied. Only the src alignment matters, not dst.
		 */
		if (src_va % SI_CPDMA_ALIGNMENT) {
			skipped_size = SI_CPDMA_ALIGNMENT - (src_va % SI_CPDMA_ALIGNMENT);
			/* The main part will be skipped if the size is too small. */
			skipped_size = MIN2(skipped_size, size);
			size -= skipped_size;
		}
	}
	main_src_va = src_va + skipped_size;
	main_dest_va = dest_va + skipped_size;

	while (size) {
		unsigned dma_flags = 0;
		unsigned byte_count = MIN2(size, cp_dma_max_byte_count(cmd_buffer));

		si_cp_dma_prepare(cmd_buffer, byte_count,
				  size + skipped_size + realign_size,
				  &dma_flags);

		si_emit_cp_dma(cmd_buffer, main_dest_va, main_src_va,
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

		si_emit_cp_dma(cmd_buffer, dest_va, src_va,
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
		unsigned byte_count = MIN2(size, cp_dma_max_byte_count(cmd_buffer));
		unsigned dma_flags = CP_DMA_CLEAR;

		si_cp_dma_prepare(cmd_buffer, byte_count, size, &dma_flags);

		/* Emit the clear packet. */
		si_emit_cp_dma(cmd_buffer, va, value, byte_count,
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
		radeon_set_context_reg(cs, R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, 0);
		radeon_set_context_reg(cs, R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0, 0);
		radeon_set_context_reg(cs, R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0, 0);
		radeon_set_context_reg(cs, R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0, 0);
		break;
	case 2:
		radeon_set_context_reg(cs, R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, eg_sample_locs_2x[0]);
		radeon_set_context_reg(cs, R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0, eg_sample_locs_2x[1]);
		radeon_set_context_reg(cs, R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0, eg_sample_locs_2x[2]);
		radeon_set_context_reg(cs, R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0, eg_sample_locs_2x[3]);
		break;
	case 4:
		radeon_set_context_reg(cs, R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, eg_sample_locs_4x[0]);
		radeon_set_context_reg(cs, R_028C08_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y0_0, eg_sample_locs_4x[1]);
		radeon_set_context_reg(cs, R_028C18_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y1_0, eg_sample_locs_4x[2]);
		radeon_set_context_reg(cs, R_028C28_PA_SC_AA_SAMPLE_LOCS_PIXEL_X1Y1_0, eg_sample_locs_4x[3]);
		break;
	case 8:
		radeon_set_context_reg_seq(cs, R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, 14);
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
		radeon_set_context_reg_seq(cs, R_028BF8_PA_SC_AA_SAMPLE_LOCS_PIXEL_X0Y0_0, 16);
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
