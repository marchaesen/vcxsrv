/*
 * Copyright © 2011 Red Hat All Rights Reserved.
 * Copyright © 2017 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

#include "ac_surface.h"
#include "amd_family.h"
#include "addrlib/amdgpu_asic_addr.h"
#include "ac_gpu_info.h"
#include "util/macros.h"
#include "util/u_atomic.h"
#include "util/u_math.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <amdgpu.h>
#include <amdgpu_drm.h>

#include "addrlib/addrinterface.h"

#ifndef CIASICIDGFXENGINE_SOUTHERNISLAND
#define CIASICIDGFXENGINE_SOUTHERNISLAND 0x0000000A
#endif

#ifndef CIASICIDGFXENGINE_ARCTICISLAND
#define CIASICIDGFXENGINE_ARCTICISLAND 0x0000000D
#endif

static unsigned get_first(unsigned x, unsigned y)
{
	return x;
}

static void addrlib_family_rev_id(enum radeon_family family,
                                 unsigned *addrlib_family,
                                 unsigned *addrlib_revid)
{
	switch (family) {
	case CHIP_TAHITI:
		*addrlib_family = FAMILY_SI;
		*addrlib_revid = get_first(AMDGPU_TAHITI_RANGE);
		break;
	case CHIP_PITCAIRN:
		*addrlib_family = FAMILY_SI;
		*addrlib_revid = get_first(AMDGPU_PITCAIRN_RANGE);
		break;
	case CHIP_VERDE:
		*addrlib_family = FAMILY_SI;
		*addrlib_revid =  get_first(AMDGPU_CAPEVERDE_RANGE);
		break;
	case CHIP_OLAND:
		*addrlib_family = FAMILY_SI;
		*addrlib_revid = get_first(AMDGPU_OLAND_RANGE);
		break;
	case CHIP_HAINAN:
		*addrlib_family = FAMILY_SI;
		*addrlib_revid = get_first(AMDGPU_HAINAN_RANGE);
		break;
	case CHIP_BONAIRE:
		*addrlib_family = FAMILY_CI;
		*addrlib_revid = get_first(AMDGPU_BONAIRE_RANGE);
		break;
	case CHIP_KAVERI:
		*addrlib_family = FAMILY_KV;
		*addrlib_revid = get_first(AMDGPU_SPECTRE_RANGE);
		break;
	case CHIP_KABINI:
		*addrlib_family = FAMILY_KV;
		*addrlib_revid = get_first(AMDGPU_KALINDI_RANGE);
		break;
	case CHIP_HAWAII:
		*addrlib_family = FAMILY_CI;
		*addrlib_revid = get_first(AMDGPU_HAWAII_RANGE);
		break;
	case CHIP_MULLINS:
		*addrlib_family = FAMILY_KV;
		*addrlib_revid = get_first(AMDGPU_GODAVARI_RANGE);
		break;
	case CHIP_TONGA:
		*addrlib_family = FAMILY_VI;
		*addrlib_revid = get_first(AMDGPU_TONGA_RANGE);
		break;
	case CHIP_ICELAND:
		*addrlib_family = FAMILY_VI;
		*addrlib_revid = get_first(AMDGPU_ICELAND_RANGE);
		break;
	case CHIP_CARRIZO:
		*addrlib_family = FAMILY_CZ;
		*addrlib_revid = get_first(AMDGPU_CARRIZO_RANGE);
		break;
	case CHIP_STONEY:
		*addrlib_family = FAMILY_CZ;
		*addrlib_revid = get_first(AMDGPU_STONEY_RANGE);
		break;
	case CHIP_FIJI:
		*addrlib_family = FAMILY_VI;
		*addrlib_revid = get_first(AMDGPU_FIJI_RANGE);
		break;
	case CHIP_POLARIS10:
		*addrlib_family = FAMILY_VI;
		*addrlib_revid = get_first(AMDGPU_POLARIS10_RANGE);
		break;
	case CHIP_POLARIS11:
		*addrlib_family = FAMILY_VI;
		*addrlib_revid = get_first(AMDGPU_POLARIS11_RANGE);
		break;
	case CHIP_POLARIS12:
		*addrlib_family = FAMILY_VI;
		*addrlib_revid = get_first(AMDGPU_POLARIS12_RANGE);
		break;
	case CHIP_VEGA10:
		*addrlib_family = FAMILY_AI;
		*addrlib_revid = get_first(AMDGPU_VEGA10_RANGE);
		break;
	case CHIP_VEGA12:
		*addrlib_family = FAMILY_AI;
		*addrlib_revid = get_first(AMDGPU_VEGA12_RANGE);
		break;
	case CHIP_RAVEN:
		*addrlib_family = FAMILY_RV;
		*addrlib_revid = get_first(AMDGPU_RAVEN_RANGE);
		break;
	default:
		fprintf(stderr, "amdgpu: Unknown family.\n");
	}
}

static void *ADDR_API allocSysMem(const ADDR_ALLOCSYSMEM_INPUT * pInput)
{
	return malloc(pInput->sizeInBytes);
}

static ADDR_E_RETURNCODE ADDR_API freeSysMem(const ADDR_FREESYSMEM_INPUT * pInput)
{
	free(pInput->pVirtAddr);
	return ADDR_OK;
}

ADDR_HANDLE amdgpu_addr_create(const struct radeon_info *info,
			       const struct amdgpu_gpu_info *amdinfo,
			       uint64_t *max_alignment)
{
	ADDR_CREATE_INPUT addrCreateInput = {0};
	ADDR_CREATE_OUTPUT addrCreateOutput = {0};
	ADDR_REGISTER_VALUE regValue = {0};
	ADDR_CREATE_FLAGS createFlags = {{0}};
	ADDR_GET_MAX_ALINGMENTS_OUTPUT addrGetMaxAlignmentsOutput = {0};
	ADDR_E_RETURNCODE addrRet;

	addrCreateInput.size = sizeof(ADDR_CREATE_INPUT);
	addrCreateOutput.size = sizeof(ADDR_CREATE_OUTPUT);

	regValue.gbAddrConfig = amdinfo->gb_addr_cfg;
	createFlags.value = 0;

       addrlib_family_rev_id(info->family, &addrCreateInput.chipFamily, &addrCreateInput.chipRevision);
	if (addrCreateInput.chipFamily == FAMILY_UNKNOWN)
		return NULL;

	if (addrCreateInput.chipFamily >= FAMILY_AI) {
		addrCreateInput.chipEngine = CIASICIDGFXENGINE_ARCTICISLAND;
		regValue.blockVarSizeLog2 = 0;
	} else {
		regValue.noOfBanks = amdinfo->mc_arb_ramcfg & 0x3;
		regValue.noOfRanks = (amdinfo->mc_arb_ramcfg & 0x4) >> 2;

		regValue.backendDisables = amdinfo->enabled_rb_pipes_mask;
		regValue.pTileConfig = amdinfo->gb_tile_mode;
		regValue.noOfEntries = ARRAY_SIZE(amdinfo->gb_tile_mode);
		if (addrCreateInput.chipFamily == FAMILY_SI) {
			regValue.pMacroTileConfig = NULL;
			regValue.noOfMacroEntries = 0;
		} else {
			regValue.pMacroTileConfig = amdinfo->gb_macro_tile_mode;
			regValue.noOfMacroEntries = ARRAY_SIZE(amdinfo->gb_macro_tile_mode);
		}

		createFlags.useTileIndex = 1;
		createFlags.useHtileSliceAlign = 1;

		addrCreateInput.chipEngine = CIASICIDGFXENGINE_SOUTHERNISLAND;
	}

	addrCreateInput.callbacks.allocSysMem = allocSysMem;
	addrCreateInput.callbacks.freeSysMem = freeSysMem;
	addrCreateInput.callbacks.debugPrint = 0;
	addrCreateInput.createFlags = createFlags;
	addrCreateInput.regValue = regValue;

	addrRet = AddrCreate(&addrCreateInput, &addrCreateOutput);
	if (addrRet != ADDR_OK)
		return NULL;

	if (max_alignment) {
		addrRet = AddrGetMaxAlignments(addrCreateOutput.hLib, &addrGetMaxAlignmentsOutput);
		if (addrRet == ADDR_OK){
			*max_alignment = addrGetMaxAlignmentsOutput.baseAlign;
		}
	}
	return addrCreateOutput.hLib;
}

static int surf_config_sanity(const struct ac_surf_config *config)
{
	/* all dimension must be at least 1 ! */
	if (!config->info.width || !config->info.height || !config->info.depth ||
	    !config->info.array_size || !config->info.levels)
		return -EINVAL;

	switch (config->info.samples) {
	case 0:
	case 1:
	case 2:
	case 4:
	case 8:
		break;
	default:
		return -EINVAL;
	}

	if (config->is_3d && config->info.array_size > 1)
		return -EINVAL;
	if (config->is_cube && config->info.depth > 1)
		return -EINVAL;

	return 0;
}

static int gfx6_compute_level(ADDR_HANDLE addrlib,
			      const struct ac_surf_config *config,
			      struct radeon_surf *surf, bool is_stencil,
			      unsigned level, bool compressed,
			      ADDR_COMPUTE_SURFACE_INFO_INPUT *AddrSurfInfoIn,
			      ADDR_COMPUTE_SURFACE_INFO_OUTPUT *AddrSurfInfoOut,
			      ADDR_COMPUTE_DCCINFO_INPUT *AddrDccIn,
			      ADDR_COMPUTE_DCCINFO_OUTPUT *AddrDccOut,
			      ADDR_COMPUTE_HTILE_INFO_INPUT *AddrHtileIn,
			      ADDR_COMPUTE_HTILE_INFO_OUTPUT *AddrHtileOut)
{
	struct legacy_surf_level *surf_level;
	ADDR_E_RETURNCODE ret;

	AddrSurfInfoIn->mipLevel = level;
	AddrSurfInfoIn->width = u_minify(config->info.width, level);
	AddrSurfInfoIn->height = u_minify(config->info.height, level);

	/* Make GFX6 linear surfaces compatible with GFX9 for hybrid graphics,
	 * because GFX9 needs linear alignment of 256 bytes.
	 */
	if (config->info.levels == 1 &&
	    AddrSurfInfoIn->tileMode == ADDR_TM_LINEAR_ALIGNED &&
	    AddrSurfInfoIn->bpp) {
		unsigned alignment = 256 / (AddrSurfInfoIn->bpp / 8);

		assert(util_is_power_of_two_or_zero(AddrSurfInfoIn->bpp));
		AddrSurfInfoIn->width = align(AddrSurfInfoIn->width, alignment);
	}

	if (config->is_3d)
		AddrSurfInfoIn->numSlices = u_minify(config->info.depth, level);
	else if (config->is_cube)
		AddrSurfInfoIn->numSlices = 6;
	else
		AddrSurfInfoIn->numSlices = config->info.array_size;

	if (level > 0) {
		/* Set the base level pitch. This is needed for calculation
		 * of non-zero levels. */
		if (is_stencil)
			AddrSurfInfoIn->basePitch = surf->u.legacy.stencil_level[0].nblk_x;
		else
			AddrSurfInfoIn->basePitch = surf->u.legacy.level[0].nblk_x;

		/* Convert blocks to pixels for compressed formats. */
		if (compressed)
			AddrSurfInfoIn->basePitch *= surf->blk_w;
	}

	ret = AddrComputeSurfaceInfo(addrlib,
				     AddrSurfInfoIn,
				     AddrSurfInfoOut);
	if (ret != ADDR_OK) {
		return ret;
	}

	surf_level = is_stencil ? &surf->u.legacy.stencil_level[level] : &surf->u.legacy.level[level];
	surf_level->offset = align64(surf->surf_size, AddrSurfInfoOut->baseAlign);
	surf_level->slice_size_dw = AddrSurfInfoOut->sliceSize / 4;
	surf_level->nblk_x = AddrSurfInfoOut->pitch;
	surf_level->nblk_y = AddrSurfInfoOut->height;

	switch (AddrSurfInfoOut->tileMode) {
	case ADDR_TM_LINEAR_ALIGNED:
		surf_level->mode = RADEON_SURF_MODE_LINEAR_ALIGNED;
		break;
	case ADDR_TM_1D_TILED_THIN1:
		surf_level->mode = RADEON_SURF_MODE_1D;
		break;
	case ADDR_TM_2D_TILED_THIN1:
		surf_level->mode = RADEON_SURF_MODE_2D;
		break;
	default:
		assert(0);
	}

	if (is_stencil)
		surf->u.legacy.stencil_tiling_index[level] = AddrSurfInfoOut->tileIndex;
	else
		surf->u.legacy.tiling_index[level] = AddrSurfInfoOut->tileIndex;

	surf->surf_size = surf_level->offset + AddrSurfInfoOut->surfSize;

	/* Clear DCC fields at the beginning. */
	surf_level->dcc_offset = 0;

	/* The previous level's flag tells us if we can use DCC for this level. */
	if (AddrSurfInfoIn->flags.dccCompatible &&
	    (level == 0 || AddrDccOut->subLvlCompressible)) {
		AddrDccIn->colorSurfSize = AddrSurfInfoOut->surfSize;
		AddrDccIn->tileMode = AddrSurfInfoOut->tileMode;
		AddrDccIn->tileInfo = *AddrSurfInfoOut->pTileInfo;
		AddrDccIn->tileIndex = AddrSurfInfoOut->tileIndex;
		AddrDccIn->macroModeIndex = AddrSurfInfoOut->macroModeIndex;

		ret = AddrComputeDccInfo(addrlib,
					 AddrDccIn,
					 AddrDccOut);

		if (ret == ADDR_OK) {
			surf_level->dcc_offset = surf->dcc_size;
			surf_level->dcc_fast_clear_size = AddrDccOut->dccFastClearSize;
			surf->num_dcc_levels = level + 1;
			surf->dcc_size = surf_level->dcc_offset + AddrDccOut->dccRamSize;
			surf->dcc_alignment = MAX2(surf->dcc_alignment, AddrDccOut->dccRamBaseAlign);
		}
	}

	/* TC-compatible HTILE. */
	if (!is_stencil &&
	    AddrSurfInfoIn->flags.depth &&
	    surf_level->mode == RADEON_SURF_MODE_2D &&
	    level == 0) {
		AddrHtileIn->flags.tcCompatible = AddrSurfInfoIn->flags.tcCompatible;
		AddrHtileIn->pitch = AddrSurfInfoOut->pitch;
		AddrHtileIn->height = AddrSurfInfoOut->height;
		AddrHtileIn->numSlices = AddrSurfInfoOut->depth;
		AddrHtileIn->blockWidth = ADDR_HTILE_BLOCKSIZE_8;
		AddrHtileIn->blockHeight = ADDR_HTILE_BLOCKSIZE_8;
		AddrHtileIn->pTileInfo = AddrSurfInfoOut->pTileInfo;
		AddrHtileIn->tileIndex = AddrSurfInfoOut->tileIndex;
		AddrHtileIn->macroModeIndex = AddrSurfInfoOut->macroModeIndex;

		ret = AddrComputeHtileInfo(addrlib,
					   AddrHtileIn,
					   AddrHtileOut);

		if (ret == ADDR_OK) {
			surf->htile_size = AddrHtileOut->htileBytes;
			surf->htile_slice_size = AddrHtileOut->sliceSize;
			surf->htile_alignment = AddrHtileOut->baseAlign;
		}
	}

	return 0;
}

#define   G_009910_MICRO_TILE_MODE(x)          (((x) >> 0) & 0x03)
#define   G_009910_MICRO_TILE_MODE_NEW(x)      (((x) >> 22) & 0x07)

static void gfx6_set_micro_tile_mode(struct radeon_surf *surf,
				     const struct radeon_info *info)
{
	uint32_t tile_mode = info->si_tile_mode_array[surf->u.legacy.tiling_index[0]];

	if (info->chip_class >= CIK)
		surf->micro_tile_mode = G_009910_MICRO_TILE_MODE_NEW(tile_mode);
	else
		surf->micro_tile_mode = G_009910_MICRO_TILE_MODE(tile_mode);
}

static unsigned cik_get_macro_tile_index(struct radeon_surf *surf)
{
	unsigned index, tileb;

	tileb = 8 * 8 * surf->bpe;
	tileb = MIN2(surf->u.legacy.tile_split, tileb);

	for (index = 0; tileb > 64; index++)
		tileb >>= 1;

	assert(index < 16);
	return index;
}

/**
 * This must be called after the first level is computed.
 *
 * Copy surface-global settings like pipe/bank config from level 0 surface
 * computation, and compute tile swizzle.
 */
static int gfx6_surface_settings(ADDR_HANDLE addrlib,
				 const struct radeon_info *info,
				 const struct ac_surf_config *config,
				 ADDR_COMPUTE_SURFACE_INFO_OUTPUT* csio,
				 struct radeon_surf *surf)
{
	surf->surf_alignment = csio->baseAlign;
	surf->u.legacy.pipe_config = csio->pTileInfo->pipeConfig - 1;
	gfx6_set_micro_tile_mode(surf, info);

	/* For 2D modes only. */
	if (csio->tileMode >= ADDR_TM_2D_TILED_THIN1) {
		surf->u.legacy.bankw = csio->pTileInfo->bankWidth;
		surf->u.legacy.bankh = csio->pTileInfo->bankHeight;
		surf->u.legacy.mtilea = csio->pTileInfo->macroAspectRatio;
		surf->u.legacy.tile_split = csio->pTileInfo->tileSplitBytes;
		surf->u.legacy.num_banks = csio->pTileInfo->banks;
		surf->u.legacy.macro_tile_index = csio->macroModeIndex;
	} else {
		surf->u.legacy.macro_tile_index = 0;
	}

	/* Compute tile swizzle. */
	/* TODO: fix tile swizzle with mipmapping for SI */
	if ((info->chip_class >= CIK || config->info.levels == 1) &&
	    config->info.surf_index &&
	    surf->u.legacy.level[0].mode == RADEON_SURF_MODE_2D &&
	    !(surf->flags & (RADEON_SURF_Z_OR_SBUFFER | RADEON_SURF_SHAREABLE)) &&
	    (config->info.samples > 1 || !(surf->flags & RADEON_SURF_SCANOUT))) {
		ADDR_COMPUTE_BASE_SWIZZLE_INPUT AddrBaseSwizzleIn = {0};
		ADDR_COMPUTE_BASE_SWIZZLE_OUTPUT AddrBaseSwizzleOut = {0};

		AddrBaseSwizzleIn.size = sizeof(ADDR_COMPUTE_BASE_SWIZZLE_INPUT);
		AddrBaseSwizzleOut.size = sizeof(ADDR_COMPUTE_BASE_SWIZZLE_OUTPUT);

		AddrBaseSwizzleIn.surfIndex = p_atomic_inc_return(config->info.surf_index) - 1;
		AddrBaseSwizzleIn.tileIndex = csio->tileIndex;
		AddrBaseSwizzleIn.macroModeIndex = csio->macroModeIndex;
		AddrBaseSwizzleIn.pTileInfo = csio->pTileInfo;
		AddrBaseSwizzleIn.tileMode = csio->tileMode;

		int r = AddrComputeBaseSwizzle(addrlib, &AddrBaseSwizzleIn,
					       &AddrBaseSwizzleOut);
		if (r != ADDR_OK)
			return r;

		assert(AddrBaseSwizzleOut.tileSwizzle <=
		       u_bit_consecutive(0, sizeof(surf->tile_swizzle) * 8));
		surf->tile_swizzle = AddrBaseSwizzleOut.tileSwizzle;
	}
	return 0;
}

/**
 * Fill in the tiling information in \p surf based on the given surface config.
 *
 * The following fields of \p surf must be initialized by the caller:
 * blk_w, blk_h, bpe, flags.
 */
static int gfx6_compute_surface(ADDR_HANDLE addrlib,
				const struct radeon_info *info,
				const struct ac_surf_config *config,
				enum radeon_surf_mode mode,
				struct radeon_surf *surf)
{
	unsigned level;
	bool compressed;
	ADDR_COMPUTE_SURFACE_INFO_INPUT AddrSurfInfoIn = {0};
	ADDR_COMPUTE_SURFACE_INFO_OUTPUT AddrSurfInfoOut = {0};
	ADDR_COMPUTE_DCCINFO_INPUT AddrDccIn = {0};
	ADDR_COMPUTE_DCCINFO_OUTPUT AddrDccOut = {0};
	ADDR_COMPUTE_HTILE_INFO_INPUT AddrHtileIn = {0};
	ADDR_COMPUTE_HTILE_INFO_OUTPUT AddrHtileOut = {0};
	ADDR_TILEINFO AddrTileInfoIn = {0};
	ADDR_TILEINFO AddrTileInfoOut = {0};
	int r;

	AddrSurfInfoIn.size = sizeof(ADDR_COMPUTE_SURFACE_INFO_INPUT);
	AddrSurfInfoOut.size = sizeof(ADDR_COMPUTE_SURFACE_INFO_OUTPUT);
	AddrDccIn.size = sizeof(ADDR_COMPUTE_DCCINFO_INPUT);
	AddrDccOut.size = sizeof(ADDR_COMPUTE_DCCINFO_OUTPUT);
	AddrHtileIn.size = sizeof(ADDR_COMPUTE_HTILE_INFO_INPUT);
	AddrHtileOut.size = sizeof(ADDR_COMPUTE_HTILE_INFO_OUTPUT);
	AddrSurfInfoOut.pTileInfo = &AddrTileInfoOut;

	compressed = surf->blk_w == 4 && surf->blk_h == 4;

	/* MSAA and FMASK require 2D tiling. */
	if (config->info.samples > 1 ||
	    (surf->flags & RADEON_SURF_FMASK))
		mode = RADEON_SURF_MODE_2D;

	/* DB doesn't support linear layouts. */
	if (surf->flags & (RADEON_SURF_Z_OR_SBUFFER) &&
	    mode < RADEON_SURF_MODE_1D)
		mode = RADEON_SURF_MODE_1D;

	/* Set the requested tiling mode. */
	switch (mode) {
	case RADEON_SURF_MODE_LINEAR_ALIGNED:
		AddrSurfInfoIn.tileMode = ADDR_TM_LINEAR_ALIGNED;
		break;
	case RADEON_SURF_MODE_1D:
		AddrSurfInfoIn.tileMode = ADDR_TM_1D_TILED_THIN1;
		break;
	case RADEON_SURF_MODE_2D:
		AddrSurfInfoIn.tileMode = ADDR_TM_2D_TILED_THIN1;
		break;
	default:
		assert(0);
	}

	/* The format must be set correctly for the allocation of compressed
	 * textures to work. In other cases, setting the bpp is sufficient.
	 */
	if (compressed) {
		switch (surf->bpe) {
		case 8:
			AddrSurfInfoIn.format = ADDR_FMT_BC1;
			break;
		case 16:
			AddrSurfInfoIn.format = ADDR_FMT_BC3;
			break;
		default:
			assert(0);
		}
	}
	else {
		AddrDccIn.bpp = AddrSurfInfoIn.bpp = surf->bpe * 8;
	}

	AddrDccIn.numSamples = AddrSurfInfoIn.numSamples =
		config->info.samples ? config->info.samples : 1;
	AddrSurfInfoIn.tileIndex = -1;

	/* Set the micro tile type. */
	if (surf->flags & RADEON_SURF_SCANOUT)
		AddrSurfInfoIn.tileType = ADDR_DISPLAYABLE;
	else if (surf->flags & (RADEON_SURF_Z_OR_SBUFFER | RADEON_SURF_FMASK))
		AddrSurfInfoIn.tileType = ADDR_DEPTH_SAMPLE_ORDER;
	else
		AddrSurfInfoIn.tileType = ADDR_NON_DISPLAYABLE;

	AddrSurfInfoIn.flags.color = !(surf->flags & RADEON_SURF_Z_OR_SBUFFER);
	AddrSurfInfoIn.flags.depth = (surf->flags & RADEON_SURF_ZBUFFER) != 0;
	AddrSurfInfoIn.flags.cube = config->is_cube;
	AddrSurfInfoIn.flags.fmask = (surf->flags & RADEON_SURF_FMASK) != 0;
	AddrSurfInfoIn.flags.display = (surf->flags & RADEON_SURF_SCANOUT) != 0;
	AddrSurfInfoIn.flags.pow2Pad = config->info.levels > 1;
	AddrSurfInfoIn.flags.tcCompatible = (surf->flags & RADEON_SURF_TC_COMPATIBLE_HTILE) != 0;

	/* Only degrade the tile mode for space if TC-compatible HTILE hasn't been
	 * requested, because TC-compatible HTILE requires 2D tiling.
	 */
	AddrSurfInfoIn.flags.opt4Space = !AddrSurfInfoIn.flags.tcCompatible &&
					 !AddrSurfInfoIn.flags.fmask &&
					 config->info.samples <= 1 &&
					 (surf->flags & RADEON_SURF_OPTIMIZE_FOR_SPACE);

	/* DCC notes:
	 * - If we add MSAA support, keep in mind that CB can't decompress 8bpp
	 *   with samples >= 4.
	 * - Mipmapped array textures have low performance (discovered by a closed
	 *   driver team).
	 */
	AddrSurfInfoIn.flags.dccCompatible =
		info->chip_class >= VI &&
		!(surf->flags & RADEON_SURF_Z_OR_SBUFFER) &&
		!(surf->flags & RADEON_SURF_DISABLE_DCC) &&
		!compressed &&
		((config->info.array_size == 1 && config->info.depth == 1) ||
		 config->info.levels == 1);

	AddrSurfInfoIn.flags.noStencil = (surf->flags & RADEON_SURF_SBUFFER) == 0;
	AddrSurfInfoIn.flags.compressZ = AddrSurfInfoIn.flags.depth;

	/* On CI/VI, the DB uses the same pitch and tile mode (except tilesplit)
	 * for Z and stencil. This can cause a number of problems which we work
	 * around here:
	 *
	 * - a depth part that is incompatible with mipmapped texturing
	 * - at least on Stoney, entirely incompatible Z/S aspects (e.g.
	 *   incorrect tiling applied to the stencil part, stencil buffer
	 *   memory accesses that go out of bounds) even without mipmapping
	 *
	 * Some piglit tests that are prone to different types of related
	 * failures:
	 *  ./bin/ext_framebuffer_multisample-upsample 2 stencil
	 *  ./bin/framebuffer-blit-levels {draw,read} stencil
	 *  ./bin/ext_framebuffer_multisample-unaligned-blit N {depth,stencil} {msaa,upsample,downsample}
	 *  ./bin/fbo-depth-array fs-writes-{depth,stencil} / {depth,stencil}-{clear,layered-clear,draw}
	 *  ./bin/depthstencil-render-miplevels 1024 d=s=z24_s8
	 */
	int stencil_tile_idx = -1;

	if (AddrSurfInfoIn.flags.depth && !AddrSurfInfoIn.flags.noStencil &&
	    (config->info.levels > 1 || info->family == CHIP_STONEY)) {
		/* Compute stencilTileIdx that is compatible with the (depth)
		 * tileIdx. This degrades the depth surface if necessary to
		 * ensure that a matching stencilTileIdx exists. */
		AddrSurfInfoIn.flags.matchStencilTileCfg = 1;

		/* Keep the depth mip-tail compatible with texturing. */
		AddrSurfInfoIn.flags.noStencil = 1;
	}

	/* Set preferred macrotile parameters. This is usually required
	 * for shared resources. This is for 2D tiling only. */
	if (AddrSurfInfoIn.tileMode >= ADDR_TM_2D_TILED_THIN1 &&
	    surf->u.legacy.bankw && surf->u.legacy.bankh &&
	    surf->u.legacy.mtilea && surf->u.legacy.tile_split) {
		assert(!(surf->flags & RADEON_SURF_FMASK));

		/* If any of these parameters are incorrect, the calculation
		 * will fail. */
		AddrTileInfoIn.banks = surf->u.legacy.num_banks;
		AddrTileInfoIn.bankWidth = surf->u.legacy.bankw;
		AddrTileInfoIn.bankHeight = surf->u.legacy.bankh;
		AddrTileInfoIn.macroAspectRatio = surf->u.legacy.mtilea;
		AddrTileInfoIn.tileSplitBytes = surf->u.legacy.tile_split;
		AddrTileInfoIn.pipeConfig = surf->u.legacy.pipe_config + 1; /* +1 compared to GB_TILE_MODE */
		AddrSurfInfoIn.flags.opt4Space = 0;
		AddrSurfInfoIn.pTileInfo = &AddrTileInfoIn;

		/* If AddrSurfInfoIn.pTileInfo is set, Addrlib doesn't set
		 * the tile index, because we are expected to know it if
		 * we know the other parameters.
		 *
		 * This is something that can easily be fixed in Addrlib.
		 * For now, just figure it out here.
		 * Note that only 2D_TILE_THIN1 is handled here.
		 */
		assert(!(surf->flags & RADEON_SURF_Z_OR_SBUFFER));
		assert(AddrSurfInfoIn.tileMode == ADDR_TM_2D_TILED_THIN1);

		if (info->chip_class == SI) {
			if (AddrSurfInfoIn.tileType == ADDR_DISPLAYABLE) {
				if (surf->bpe == 2)
					AddrSurfInfoIn.tileIndex = 11; /* 16bpp */
				else
					AddrSurfInfoIn.tileIndex = 12; /* 32bpp */
			} else {
				if (surf->bpe == 1)
					AddrSurfInfoIn.tileIndex = 14; /* 8bpp */
				else if (surf->bpe == 2)
					AddrSurfInfoIn.tileIndex = 15; /* 16bpp */
				else if (surf->bpe == 4)
					AddrSurfInfoIn.tileIndex = 16; /* 32bpp */
				else
					AddrSurfInfoIn.tileIndex = 17; /* 64bpp (and 128bpp) */
			}
		} else {
			/* CIK - VI */
			if (AddrSurfInfoIn.tileType == ADDR_DISPLAYABLE)
				AddrSurfInfoIn.tileIndex = 10; /* 2D displayable */
			else
				AddrSurfInfoIn.tileIndex = 14; /* 2D non-displayable */

			/* Addrlib doesn't set this if tileIndex is forced like above. */
			AddrSurfInfoOut.macroModeIndex = cik_get_macro_tile_index(surf);
		}
	}

	surf->has_stencil = !!(surf->flags & RADEON_SURF_SBUFFER);
	surf->num_dcc_levels = 0;
	surf->surf_size = 0;
	surf->dcc_size = 0;
	surf->dcc_alignment = 1;
	surf->htile_size = 0;
	surf->htile_slice_size = 0;
	surf->htile_alignment = 1;

	const bool only_stencil = (surf->flags & RADEON_SURF_SBUFFER) &&
				  !(surf->flags & RADEON_SURF_ZBUFFER);

	/* Calculate texture layout information. */
	if (!only_stencil) {
		for (level = 0; level < config->info.levels; level++) {
			r = gfx6_compute_level(addrlib, config, surf, false, level, compressed,
					       &AddrSurfInfoIn, &AddrSurfInfoOut,
					       &AddrDccIn, &AddrDccOut, &AddrHtileIn, &AddrHtileOut);
			if (r)
				return r;

			if (level > 0)
				continue;

			/* Check that we actually got a TC-compatible HTILE if
			 * we requested it (only for level 0, since we're not
			 * supporting HTILE on higher mip levels anyway). */
			assert(AddrSurfInfoOut.tcCompatible ||
			       !AddrSurfInfoIn.flags.tcCompatible ||
			       AddrSurfInfoIn.flags.matchStencilTileCfg);

			if (AddrSurfInfoIn.flags.matchStencilTileCfg) {
				if (!AddrSurfInfoOut.tcCompatible) {
					AddrSurfInfoIn.flags.tcCompatible = 0;
					surf->flags &= ~RADEON_SURF_TC_COMPATIBLE_HTILE;
				}

				AddrSurfInfoIn.flags.matchStencilTileCfg = 0;
				AddrSurfInfoIn.tileIndex = AddrSurfInfoOut.tileIndex;
				stencil_tile_idx = AddrSurfInfoOut.stencilTileIdx;

				assert(stencil_tile_idx >= 0);
			}

			r = gfx6_surface_settings(addrlib, info, config,
						  &AddrSurfInfoOut, surf);
			if (r)
				return r;
		}
	}

	/* Calculate texture layout information for stencil. */
	if (surf->flags & RADEON_SURF_SBUFFER) {
		AddrSurfInfoIn.tileIndex = stencil_tile_idx;
		AddrSurfInfoIn.bpp = 8;
		AddrSurfInfoIn.flags.depth = 0;
		AddrSurfInfoIn.flags.stencil = 1;
		AddrSurfInfoIn.flags.tcCompatible = 0;
		/* This will be ignored if AddrSurfInfoIn.pTileInfo is NULL. */
		AddrTileInfoIn.tileSplitBytes = surf->u.legacy.stencil_tile_split;

		for (level = 0; level < config->info.levels; level++) {
			r = gfx6_compute_level(addrlib, config, surf, true, level, compressed,
					       &AddrSurfInfoIn, &AddrSurfInfoOut,
					       &AddrDccIn, &AddrDccOut,
					       NULL, NULL);
			if (r)
				return r;

			/* DB uses the depth pitch for both stencil and depth. */
			if (!only_stencil) {
				if (surf->u.legacy.stencil_level[level].nblk_x !=
				    surf->u.legacy.level[level].nblk_x)
					surf->u.legacy.stencil_adjusted = true;
			} else {
				surf->u.legacy.level[level].nblk_x =
					surf->u.legacy.stencil_level[level].nblk_x;
			}

			if (level == 0) {
				if (only_stencil) {
					r = gfx6_surface_settings(addrlib, info, config,
								  &AddrSurfInfoOut, surf);
					if (r)
						return r;
				}

				/* For 2D modes only. */
				if (AddrSurfInfoOut.tileMode >= ADDR_TM_2D_TILED_THIN1) {
					surf->u.legacy.stencil_tile_split =
						AddrSurfInfoOut.pTileInfo->tileSplitBytes;
				}
			}
		}
	}

	/* Recalculate the whole DCC miptree size including disabled levels.
	 * This is what addrlib does, but calling addrlib would be a lot more
	 * complicated.
	 */
	if (surf->dcc_size && config->info.levels > 1) {
		/* The smallest miplevels that are never compressed by DCC
		 * still read the DCC buffer via TC if the base level uses DCC,
		 * and for some reason the DCC buffer needs to be larger if
		 * the miptree uses non-zero tile_swizzle. Otherwise there are
		 * VM faults.
		 *
		 * "dcc_alignment * 4" was determined by trial and error.
		 */
		surf->dcc_size = align64(surf->surf_size >> 8,
					 surf->dcc_alignment * 4);
	}

	/* Make sure HTILE covers the whole miptree, because the shader reads
	 * TC-compatible HTILE even for levels where it's disabled by DB.
	 */
	if (surf->htile_size && config->info.levels > 1)
		surf->htile_size *= 2;

	surf->is_linear = surf->u.legacy.level[0].mode == RADEON_SURF_MODE_LINEAR_ALIGNED;
	surf->is_displayable = surf->is_linear ||
			       surf->micro_tile_mode == RADEON_MICRO_MODE_DISPLAY ||
			       surf->micro_tile_mode == RADEON_MICRO_MODE_ROTATED;
	return 0;
}

/* This is only called when expecting a tiled layout. */
static int
gfx9_get_preferred_swizzle_mode(ADDR_HANDLE addrlib,
				ADDR2_COMPUTE_SURFACE_INFO_INPUT *in,
				bool is_fmask, AddrSwizzleMode *swizzle_mode)
{
	ADDR_E_RETURNCODE ret;
	ADDR2_GET_PREFERRED_SURF_SETTING_INPUT sin = {0};
	ADDR2_GET_PREFERRED_SURF_SETTING_OUTPUT sout = {0};

	sin.size = sizeof(ADDR2_GET_PREFERRED_SURF_SETTING_INPUT);
	sout.size = sizeof(ADDR2_GET_PREFERRED_SURF_SETTING_OUTPUT);

	sin.flags = in->flags;
	sin.resourceType = in->resourceType;
	sin.format = in->format;
	sin.resourceLoction = ADDR_RSRC_LOC_INVIS;
	/* TODO: We could allow some of these: */
	sin.forbiddenBlock.micro = 1; /* don't allow the 256B swizzle modes */
	sin.forbiddenBlock.var = 1; /* don't allow the variable-sized swizzle modes */
	sin.forbiddenBlock.linear = 1; /* don't allow linear swizzle modes */
	sin.bpp = in->bpp;
	sin.width = in->width;
	sin.height = in->height;
	sin.numSlices = in->numSlices;
	sin.numMipLevels = in->numMipLevels;
	sin.numSamples = in->numSamples;
	sin.numFrags = in->numFrags;

	if (is_fmask) {
		sin.flags.color = 0;
		sin.flags.fmask = 1;
	}

	ret = Addr2GetPreferredSurfaceSetting(addrlib, &sin, &sout);
	if (ret != ADDR_OK)
		return ret;

	*swizzle_mode = sout.swizzleMode;
	return 0;
}

static int gfx9_compute_miptree(ADDR_HANDLE addrlib,
				const struct ac_surf_config *config,
				struct radeon_surf *surf, bool compressed,
				ADDR2_COMPUTE_SURFACE_INFO_INPUT *in)
{
	ADDR2_MIP_INFO mip_info[RADEON_SURF_MAX_LEVELS] = {};
	ADDR2_COMPUTE_SURFACE_INFO_OUTPUT out = {0};
	ADDR_E_RETURNCODE ret;

	out.size = sizeof(ADDR2_COMPUTE_SURFACE_INFO_OUTPUT);
	out.pMipInfo = mip_info;

	ret = Addr2ComputeSurfaceInfo(addrlib, in, &out);
	if (ret != ADDR_OK)
		return ret;

	if (in->flags.stencil) {
		surf->u.gfx9.stencil.swizzle_mode = in->swizzleMode;
		surf->u.gfx9.stencil.epitch = out.epitchIsHeight ? out.mipChainHeight - 1 :
								   out.mipChainPitch - 1;
		surf->surf_alignment = MAX2(surf->surf_alignment, out.baseAlign);
		surf->u.gfx9.stencil_offset = align(surf->surf_size, out.baseAlign);
		surf->surf_size = surf->u.gfx9.stencil_offset + out.surfSize;
		return 0;
	}

	surf->u.gfx9.surf.swizzle_mode = in->swizzleMode;
	surf->u.gfx9.surf.epitch = out.epitchIsHeight ? out.mipChainHeight - 1 :
							out.mipChainPitch - 1;

	/* CMASK fast clear uses these even if FMASK isn't allocated.
	 * FMASK only supports the Z swizzle modes, whose numbers are multiples of 4.
	 */
	surf->u.gfx9.fmask.swizzle_mode = surf->u.gfx9.surf.swizzle_mode & ~0x3;
	surf->u.gfx9.fmask.epitch = surf->u.gfx9.surf.epitch;

	surf->u.gfx9.surf_slice_size = out.sliceSize;
	surf->u.gfx9.surf_pitch = out.pitch;
	surf->u.gfx9.surf_height = out.height;
	surf->surf_size = out.surfSize;
	surf->surf_alignment = out.baseAlign;

	if (in->swizzleMode == ADDR_SW_LINEAR) {
		for (unsigned i = 0; i < in->numMipLevels; i++)
			surf->u.gfx9.offset[i] = mip_info[i].offset;
	}

	if (in->flags.depth) {
		assert(in->swizzleMode != ADDR_SW_LINEAR);

		/* HTILE */
		ADDR2_COMPUTE_HTILE_INFO_INPUT hin = {0};
		ADDR2_COMPUTE_HTILE_INFO_OUTPUT hout = {0};

		hin.size = sizeof(ADDR2_COMPUTE_HTILE_INFO_INPUT);
		hout.size = sizeof(ADDR2_COMPUTE_HTILE_INFO_OUTPUT);

		hin.hTileFlags.pipeAligned = !in->flags.metaPipeUnaligned;
		hin.hTileFlags.rbAligned = !in->flags.metaRbUnaligned;
		hin.depthFlags = in->flags;
		hin.swizzleMode = in->swizzleMode;
		hin.unalignedWidth = in->width;
		hin.unalignedHeight = in->height;
		hin.numSlices = in->numSlices;
		hin.numMipLevels = in->numMipLevels;

		ret = Addr2ComputeHtileInfo(addrlib, &hin, &hout);
		if (ret != ADDR_OK)
			return ret;

		surf->u.gfx9.htile.rb_aligned = hin.hTileFlags.rbAligned;
		surf->u.gfx9.htile.pipe_aligned = hin.hTileFlags.pipeAligned;
		surf->htile_size = hout.htileBytes;
		surf->htile_slice_size = hout.sliceSize;
		surf->htile_alignment = hout.baseAlign;
	} else {
		/* Compute tile swizzle for the color surface.
		 * All *_X and *_T modes can use the swizzle.
		 */
		if (config->info.surf_index &&
		    in->swizzleMode >= ADDR_SW_64KB_Z_T &&
		    !out.mipChainInTail &&
		    !(surf->flags & RADEON_SURF_SHAREABLE) &&
		    (in->numSamples > 1 || !(surf->flags & RADEON_SURF_SCANOUT))) {
			ADDR2_COMPUTE_PIPEBANKXOR_INPUT xin = {0};
			ADDR2_COMPUTE_PIPEBANKXOR_OUTPUT xout = {0};

			xin.size = sizeof(ADDR2_COMPUTE_PIPEBANKXOR_INPUT);
			xout.size = sizeof(ADDR2_COMPUTE_PIPEBANKXOR_OUTPUT);

			xin.surfIndex = p_atomic_inc_return(config->info.surf_index) - 1;
			xin.flags = in->flags;
			xin.swizzleMode = in->swizzleMode;
			xin.resourceType = in->resourceType;
			xin.format = in->format;
			xin.numSamples = in->numSamples;
			xin.numFrags = in->numFrags;

			ret = Addr2ComputePipeBankXor(addrlib, &xin, &xout);
			if (ret != ADDR_OK)
				return ret;

			assert(xout.pipeBankXor <=
			       u_bit_consecutive(0, sizeof(surf->tile_swizzle) * 8));
			surf->tile_swizzle = xout.pipeBankXor;
		}

		/* DCC */
		if (!(surf->flags & RADEON_SURF_DISABLE_DCC) &&
		    !compressed &&
		    in->swizzleMode != ADDR_SW_LINEAR) {
			ADDR2_COMPUTE_DCCINFO_INPUT din = {0};
			ADDR2_COMPUTE_DCCINFO_OUTPUT dout = {0};
			ADDR2_META_MIP_INFO meta_mip_info[RADEON_SURF_MAX_LEVELS] = {};

			din.size = sizeof(ADDR2_COMPUTE_DCCINFO_INPUT);
			dout.size = sizeof(ADDR2_COMPUTE_DCCINFO_OUTPUT);
			dout.pMipInfo = meta_mip_info;

			din.dccKeyFlags.pipeAligned = !in->flags.metaPipeUnaligned;
			din.dccKeyFlags.rbAligned = !in->flags.metaRbUnaligned;
			din.colorFlags = in->flags;
			din.resourceType = in->resourceType;
			din.swizzleMode = in->swizzleMode;
			din.bpp = in->bpp;
			din.unalignedWidth = in->width;
			din.unalignedHeight = in->height;
			din.numSlices = in->numSlices;
			din.numFrags = in->numFrags;
			din.numMipLevels = in->numMipLevels;
			din.dataSurfaceSize = out.surfSize;

			ret = Addr2ComputeDccInfo(addrlib, &din, &dout);
			if (ret != ADDR_OK)
				return ret;

			surf->u.gfx9.dcc.rb_aligned = din.dccKeyFlags.rbAligned;
			surf->u.gfx9.dcc.pipe_aligned = din.dccKeyFlags.pipeAligned;
			surf->u.gfx9.dcc_pitch_max = dout.pitch - 1;
			surf->dcc_size = dout.dccRamSize;
			surf->dcc_alignment = dout.dccRamBaseAlign;
			surf->num_dcc_levels = in->numMipLevels;

			/* Disable DCC for levels that are in the mip tail.
			 *
			 * There are two issues that this is intended to
			 * address:
			 *
			 * 1. Multiple mip levels may share a cache line. This
			 *    can lead to corruption when switching between
			 *    rendering to different mip levels because the
			 *    RBs don't maintain coherency.
			 *
			 * 2. Texturing with metadata after rendering sometimes
			 *    fails with corruption, probably for a similar
			 *    reason.
			 *
			 * Working around these issues for all levels in the
			 * mip tail may be overly conservative, but it's what
			 * Vulkan does.
			 *
			 * Alternative solutions that also work but are worse:
			 * - Disable DCC entirely.
			 * - Flush TC L2 after rendering.
			 */
			for (unsigned i = 0; i < in->numMipLevels; i++) {
				if (meta_mip_info[i].inMiptail) {
					surf->num_dcc_levels = i;
					break;
				}
			}

			if (!surf->num_dcc_levels)
				surf->dcc_size = 0;
		}

		/* FMASK */
		if (in->numSamples > 1) {
			ADDR2_COMPUTE_FMASK_INFO_INPUT fin = {0};
			ADDR2_COMPUTE_FMASK_INFO_OUTPUT fout = {0};

			fin.size = sizeof(ADDR2_COMPUTE_FMASK_INFO_INPUT);
			fout.size = sizeof(ADDR2_COMPUTE_FMASK_INFO_OUTPUT);

			ret = gfx9_get_preferred_swizzle_mode(addrlib, in, true, &fin.swizzleMode);
			if (ret != ADDR_OK)
				return ret;

			fin.unalignedWidth = in->width;
			fin.unalignedHeight = in->height;
			fin.numSlices = in->numSlices;
			fin.numSamples = in->numSamples;
			fin.numFrags = in->numFrags;

			ret = Addr2ComputeFmaskInfo(addrlib, &fin, &fout);
			if (ret != ADDR_OK)
				return ret;

			surf->u.gfx9.fmask.swizzle_mode = fin.swizzleMode;
			surf->u.gfx9.fmask.epitch = fout.pitch - 1;
			surf->u.gfx9.fmask_size = fout.fmaskBytes;
			surf->u.gfx9.fmask_alignment = fout.baseAlign;

			/* Compute tile swizzle for the FMASK surface. */
			if (config->info.fmask_surf_index &&
			    fin.swizzleMode >= ADDR_SW_64KB_Z_T &&
			    !(surf->flags & RADEON_SURF_SHAREABLE)) {
				ADDR2_COMPUTE_PIPEBANKXOR_INPUT xin = {0};
				ADDR2_COMPUTE_PIPEBANKXOR_OUTPUT xout = {0};

				xin.size = sizeof(ADDR2_COMPUTE_PIPEBANKXOR_INPUT);
				xout.size = sizeof(ADDR2_COMPUTE_PIPEBANKXOR_OUTPUT);

				/* This counter starts from 1 instead of 0. */
				xin.surfIndex = p_atomic_inc_return(config->info.fmask_surf_index);
				xin.flags = in->flags;
				xin.swizzleMode = in->swizzleMode;
				xin.resourceType = in->resourceType;
				xin.format = in->format;
				xin.numSamples = in->numSamples;
				xin.numFrags = in->numFrags;

				ret = Addr2ComputePipeBankXor(addrlib, &xin, &xout);
				if (ret != ADDR_OK)
					return ret;

				assert(xout.pipeBankXor <=
				       u_bit_consecutive(0, sizeof(surf->u.gfx9.fmask_tile_swizzle) * 8));
				surf->u.gfx9.fmask_tile_swizzle = xout.pipeBankXor;
			}
		}

		/* CMASK */
		if (in->swizzleMode != ADDR_SW_LINEAR) {
			ADDR2_COMPUTE_CMASK_INFO_INPUT cin = {0};
			ADDR2_COMPUTE_CMASK_INFO_OUTPUT cout = {0};

			cin.size = sizeof(ADDR2_COMPUTE_CMASK_INFO_INPUT);
			cout.size = sizeof(ADDR2_COMPUTE_CMASK_INFO_OUTPUT);

			if (in->numSamples) {
				/* FMASK is always aligned. */
				cin.cMaskFlags.pipeAligned = 1;
				cin.cMaskFlags.rbAligned = 1;
			} else {
				cin.cMaskFlags.pipeAligned = !in->flags.metaPipeUnaligned;
				cin.cMaskFlags.rbAligned = !in->flags.metaRbUnaligned;
			}
			cin.colorFlags = in->flags;
			cin.resourceType = in->resourceType;
			cin.unalignedWidth = in->width;
			cin.unalignedHeight = in->height;
			cin.numSlices = in->numSlices;

			if (in->numSamples > 1)
				cin.swizzleMode = surf->u.gfx9.fmask.swizzle_mode;
			else
				cin.swizzleMode = in->swizzleMode;

			ret = Addr2ComputeCmaskInfo(addrlib, &cin, &cout);
			if (ret != ADDR_OK)
				return ret;

			surf->u.gfx9.cmask.rb_aligned = cin.cMaskFlags.rbAligned;
			surf->u.gfx9.cmask.pipe_aligned = cin.cMaskFlags.pipeAligned;
			surf->u.gfx9.cmask_size = cout.cmaskBytes;
			surf->u.gfx9.cmask_alignment = cout.baseAlign;
		}
	}

	return 0;
}

static int gfx9_compute_surface(ADDR_HANDLE addrlib,
				const struct radeon_info *info,
				const struct ac_surf_config *config,
				enum radeon_surf_mode mode,
				struct radeon_surf *surf)
{
	bool compressed;
	ADDR2_COMPUTE_SURFACE_INFO_INPUT AddrSurfInfoIn = {0};
	int r;

	assert(!(surf->flags & RADEON_SURF_FMASK));

	AddrSurfInfoIn.size = sizeof(ADDR2_COMPUTE_SURFACE_INFO_INPUT);

	compressed = surf->blk_w == 4 && surf->blk_h == 4;

	/* The format must be set correctly for the allocation of compressed
	 * textures to work. In other cases, setting the bpp is sufficient. */
	if (compressed) {
		switch (surf->bpe) {
		case 8:
			AddrSurfInfoIn.format = ADDR_FMT_BC1;
			break;
		case 16:
			AddrSurfInfoIn.format = ADDR_FMT_BC3;
			break;
		default:
			assert(0);
		}
	} else {
		switch (surf->bpe) {
		case 1:
			assert(!(surf->flags & RADEON_SURF_ZBUFFER));
			AddrSurfInfoIn.format = ADDR_FMT_8;
			break;
		case 2:
			assert(surf->flags & RADEON_SURF_ZBUFFER ||
			       !(surf->flags & RADEON_SURF_SBUFFER));
			AddrSurfInfoIn.format = ADDR_FMT_16;
			break;
		case 4:
			assert(surf->flags & RADEON_SURF_ZBUFFER ||
			       !(surf->flags & RADEON_SURF_SBUFFER));
			AddrSurfInfoIn.format = ADDR_FMT_32;
			break;
		case 8:
			assert(!(surf->flags & RADEON_SURF_Z_OR_SBUFFER));
			AddrSurfInfoIn.format = ADDR_FMT_32_32;
			break;
		case 16:
			assert(!(surf->flags & RADEON_SURF_Z_OR_SBUFFER));
			AddrSurfInfoIn.format = ADDR_FMT_32_32_32_32;
			break;
		default:
			assert(0);
		}
		AddrSurfInfoIn.bpp = surf->bpe * 8;
	}

	AddrSurfInfoIn.flags.color = !(surf->flags & RADEON_SURF_Z_OR_SBUFFER);
	AddrSurfInfoIn.flags.depth = (surf->flags & RADEON_SURF_ZBUFFER) != 0;
	AddrSurfInfoIn.flags.display = (surf->flags & RADEON_SURF_SCANOUT) != 0;
	/* flags.texture currently refers to TC-compatible HTILE */
	AddrSurfInfoIn.flags.texture = AddrSurfInfoIn.flags.color ||
				       surf->flags & RADEON_SURF_TC_COMPATIBLE_HTILE;
	AddrSurfInfoIn.flags.opt4space = 1;

	AddrSurfInfoIn.numMipLevels = config->info.levels;
	AddrSurfInfoIn.numSamples = config->info.samples ? config->info.samples : 1;
	AddrSurfInfoIn.numFrags = AddrSurfInfoIn.numSamples;

	/* GFX9 doesn't support 1D depth textures, so allocate all 1D textures
	 * as 2D to avoid having shader variants for 1D vs 2D, so all shaders
	 * must sample 1D textures as 2D. */
	if (config->is_3d)
		AddrSurfInfoIn.resourceType = ADDR_RSRC_TEX_3D;
	else
		AddrSurfInfoIn.resourceType = ADDR_RSRC_TEX_2D;

	AddrSurfInfoIn.width = config->info.width;
	AddrSurfInfoIn.height = config->info.height;

	if (config->is_3d)
		AddrSurfInfoIn.numSlices = config->info.depth;
	else if (config->is_cube)
		AddrSurfInfoIn.numSlices = 6;
	else
		AddrSurfInfoIn.numSlices = config->info.array_size;

	/* This is propagated to HTILE/DCC/CMASK. */
	AddrSurfInfoIn.flags.metaPipeUnaligned = 0;
	AddrSurfInfoIn.flags.metaRbUnaligned = 0;

	switch (mode) {
	case RADEON_SURF_MODE_LINEAR_ALIGNED:
		assert(config->info.samples <= 1);
		assert(!(surf->flags & RADEON_SURF_Z_OR_SBUFFER));
		AddrSurfInfoIn.swizzleMode = ADDR_SW_LINEAR;
		break;

	case RADEON_SURF_MODE_1D:
	case RADEON_SURF_MODE_2D:
		if (surf->flags & RADEON_SURF_IMPORTED) {
			AddrSurfInfoIn.swizzleMode = surf->u.gfx9.surf.swizzle_mode;
			break;
		}

		r = gfx9_get_preferred_swizzle_mode(addrlib, &AddrSurfInfoIn, false,
						    &AddrSurfInfoIn.swizzleMode);
		if (r)
			return r;
		break;

	default:
		assert(0);
	}

	surf->u.gfx9.resource_type = AddrSurfInfoIn.resourceType;
	surf->has_stencil = !!(surf->flags & RADEON_SURF_SBUFFER);

	surf->num_dcc_levels = 0;
	surf->surf_size = 0;
	surf->dcc_size = 0;
	surf->htile_size = 0;
	surf->htile_slice_size = 0;
	surf->u.gfx9.surf_offset = 0;
	surf->u.gfx9.stencil_offset = 0;
	surf->u.gfx9.fmask_size = 0;
	surf->u.gfx9.cmask_size = 0;

	/* Calculate texture layout information. */
	r = gfx9_compute_miptree(addrlib, config, surf, compressed,
				 &AddrSurfInfoIn);
	if (r)
		return r;

	/* Calculate texture layout information for stencil. */
	if (surf->flags & RADEON_SURF_SBUFFER) {
		AddrSurfInfoIn.flags.stencil = 1;
		AddrSurfInfoIn.bpp = 8;
		AddrSurfInfoIn.format = ADDR_FMT_8;

		if (!AddrSurfInfoIn.flags.depth) {
			r = gfx9_get_preferred_swizzle_mode(addrlib, &AddrSurfInfoIn, false,
							    &AddrSurfInfoIn.swizzleMode);
			if (r)
				return r;
		} else
			AddrSurfInfoIn.flags.depth = 0;

		r = gfx9_compute_miptree(addrlib, config, surf, compressed,
					 &AddrSurfInfoIn);
		if (r)
			return r;
	}

	surf->is_linear = surf->u.gfx9.surf.swizzle_mode == ADDR_SW_LINEAR;

	/* Query whether the surface is displayable. */
	bool displayable = false;
	r = Addr2IsValidDisplaySwizzleMode(addrlib, surf->u.gfx9.surf.swizzle_mode,
					   surf->bpe * 8, &displayable);
	if (r)
		return r;
	surf->is_displayable = displayable;

	switch (surf->u.gfx9.surf.swizzle_mode) {
		/* S = standard. */
		case ADDR_SW_256B_S:
		case ADDR_SW_4KB_S:
		case ADDR_SW_64KB_S:
		case ADDR_SW_VAR_S:
		case ADDR_SW_64KB_S_T:
		case ADDR_SW_4KB_S_X:
		case ADDR_SW_64KB_S_X:
		case ADDR_SW_VAR_S_X:
			surf->micro_tile_mode = RADEON_MICRO_MODE_THIN;
			break;

		/* D = display. */
		case ADDR_SW_LINEAR:
		case ADDR_SW_256B_D:
		case ADDR_SW_4KB_D:
		case ADDR_SW_64KB_D:
		case ADDR_SW_VAR_D:
		case ADDR_SW_64KB_D_T:
		case ADDR_SW_4KB_D_X:
		case ADDR_SW_64KB_D_X:
		case ADDR_SW_VAR_D_X:
			surf->micro_tile_mode = RADEON_MICRO_MODE_DISPLAY;
			break;

		/* R = rotated. */
		case ADDR_SW_256B_R:
		case ADDR_SW_4KB_R:
		case ADDR_SW_64KB_R:
		case ADDR_SW_VAR_R:
		case ADDR_SW_64KB_R_T:
		case ADDR_SW_4KB_R_X:
		case ADDR_SW_64KB_R_X:
		case ADDR_SW_VAR_R_X:
			surf->micro_tile_mode = RADEON_MICRO_MODE_ROTATED;
			break;

		/* Z = depth. */
		case ADDR_SW_4KB_Z:
		case ADDR_SW_64KB_Z:
		case ADDR_SW_VAR_Z:
		case ADDR_SW_64KB_Z_T:
		case ADDR_SW_4KB_Z_X:
		case ADDR_SW_64KB_Z_X:
		case ADDR_SW_VAR_Z_X:
			surf->micro_tile_mode = RADEON_MICRO_MODE_DEPTH;
			break;

		default:
			assert(0);
	}

	/* Temporary workaround to prevent VM faults and hangs. */
	if (info->family == CHIP_VEGA12)
		surf->u.gfx9.fmask_size *= 8;

	return 0;
}

int ac_compute_surface(ADDR_HANDLE addrlib, const struct radeon_info *info,
		       const struct ac_surf_config *config,
		       enum radeon_surf_mode mode,
		       struct radeon_surf *surf)
{
	int r;

	r = surf_config_sanity(config);
	if (r)
		return r;

	if (info->chip_class >= GFX9)
		return gfx9_compute_surface(addrlib, info, config, mode, surf);
	else
		return gfx6_compute_surface(addrlib, info, config, mode, surf);
}
