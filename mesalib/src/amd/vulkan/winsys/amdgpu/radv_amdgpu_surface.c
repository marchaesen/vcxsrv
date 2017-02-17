/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based on amdgpu winsys.
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
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

#include <errno.h>

#include "radv_private.h"
#include "addrlib/addrinterface.h"
#include "util/bitset.h"
#include "radv_amdgpu_winsys.h"
#include "radv_amdgpu_surface.h"
#include "sid.h"

#ifndef NO_ENTRIES
#define NO_ENTRIES 32
#endif

#ifndef NO_MACRO_ENTRIES
#define NO_MACRO_ENTRIES 16
#endif

#ifndef CIASICIDGFXENGINE_SOUTHERNISLAND
#define CIASICIDGFXENGINE_SOUTHERNISLAND 0x0000000A
#endif

static int radv_amdgpu_surface_sanity(const struct radeon_surf *surf)
{
	unsigned type = RADEON_SURF_GET(surf->flags, TYPE);

	if (!(surf->flags & RADEON_SURF_HAS_TILE_MODE_INDEX))
		return -EINVAL;

	/* all dimension must be at least 1 ! */
	if (!surf->npix_x || !surf->npix_y || !surf->npix_z ||
	    !surf->array_size)
		return -EINVAL;

	if (!surf->blk_w || !surf->blk_h || !surf->blk_d)
		return -EINVAL;

	switch (surf->nsamples) {
	case 1:
	case 2:
	case 4:
	case 8:
		break;
	default:
		return -EINVAL;
	}

	switch (type) {
	case RADEON_SURF_TYPE_1D:
		if (surf->npix_y > 1)
			return -EINVAL;
		/* fall through */
	case RADEON_SURF_TYPE_2D:
	case RADEON_SURF_TYPE_CUBEMAP:
		if (surf->npix_z > 1 || surf->array_size > 1)
			return -EINVAL;
		break;
	case RADEON_SURF_TYPE_3D:
		if (surf->array_size > 1)
			return -EINVAL;
		break;
	case RADEON_SURF_TYPE_1D_ARRAY:
		if (surf->npix_y > 1)
			return -EINVAL;
		/* fall through */
	case RADEON_SURF_TYPE_2D_ARRAY:
		if (surf->npix_z > 1)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void *ADDR_API radv_allocSysMem(const ADDR_ALLOCSYSMEM_INPUT * pInput)
{
	return malloc(pInput->sizeInBytes);
}

static ADDR_E_RETURNCODE ADDR_API radv_freeSysMem(const ADDR_FREESYSMEM_INPUT * pInput)
{
	free(pInput->pVirtAddr);
	return ADDR_OK;
}

ADDR_HANDLE radv_amdgpu_addr_create(struct amdgpu_gpu_info *amdinfo, int family, int rev_id,
				    enum chip_class chip_class)
{
	ADDR_CREATE_INPUT addrCreateInput = {0};
	ADDR_CREATE_OUTPUT addrCreateOutput = {0};
	ADDR_REGISTER_VALUE regValue = {0};
	ADDR_CREATE_FLAGS createFlags = {{0}};
	ADDR_E_RETURNCODE addrRet;

	addrCreateInput.size = sizeof(ADDR_CREATE_INPUT);
	addrCreateOutput.size = sizeof(ADDR_CREATE_OUTPUT);

	regValue.noOfBanks = amdinfo->mc_arb_ramcfg & 0x3;
	regValue.gbAddrConfig = amdinfo->gb_addr_cfg;
	regValue.noOfRanks = (amdinfo->mc_arb_ramcfg & 0x4) >> 2;

	regValue.backendDisables = amdinfo->backend_disable[0];
	regValue.pTileConfig = amdinfo->gb_tile_mode;
	regValue.noOfEntries = ARRAY_SIZE(amdinfo->gb_tile_mode);
	if (chip_class == SI) {
		regValue.pMacroTileConfig = NULL;
		regValue.noOfMacroEntries = 0;
	} else {
		regValue.pMacroTileConfig = amdinfo->gb_macro_tile_mode;
		regValue.noOfMacroEntries = ARRAY_SIZE(amdinfo->gb_macro_tile_mode);
	}

	createFlags.value = 0;
	createFlags.useTileIndex = 1;
	createFlags.degradeBaseLevel = 1;

	addrCreateInput.chipEngine = CIASICIDGFXENGINE_SOUTHERNISLAND;
	addrCreateInput.chipFamily = family;
	addrCreateInput.chipRevision = rev_id;
	addrCreateInput.createFlags = createFlags;
	addrCreateInput.callbacks.allocSysMem = radv_allocSysMem;
	addrCreateInput.callbacks.freeSysMem = radv_freeSysMem;
	addrCreateInput.callbacks.debugPrint = 0;
	addrCreateInput.regValue = regValue;

	addrRet = AddrCreate(&addrCreateInput, &addrCreateOutput);
	if (addrRet != ADDR_OK)
		return NULL;

	return addrCreateOutput.hLib;
}

static int radv_compute_level(ADDR_HANDLE addrlib,
                              struct radeon_surf *surf, bool is_stencil,
                              unsigned level, unsigned type, bool compressed,
                              ADDR_COMPUTE_SURFACE_INFO_INPUT *AddrSurfInfoIn,
                              ADDR_COMPUTE_SURFACE_INFO_OUTPUT *AddrSurfInfoOut,
                              ADDR_COMPUTE_DCCINFO_INPUT *AddrDccIn,
                              ADDR_COMPUTE_DCCINFO_OUTPUT *AddrDccOut)
{
	struct radeon_surf_level *surf_level;
	ADDR_E_RETURNCODE ret;

	AddrSurfInfoIn->mipLevel = level;
	AddrSurfInfoIn->width = u_minify(surf->npix_x, level);
	AddrSurfInfoIn->height = u_minify(surf->npix_y, level);

	if (type == RADEON_SURF_TYPE_3D)
		AddrSurfInfoIn->numSlices = u_minify(surf->npix_z, level);
	else if (type == RADEON_SURF_TYPE_CUBEMAP)
		AddrSurfInfoIn->numSlices = 6;
	else
		AddrSurfInfoIn->numSlices = surf->array_size;

	if (level > 0) {
		/* Set the base level pitch. This is needed for calculation
		 * of non-zero levels. */
		if (is_stencil)
			AddrSurfInfoIn->basePitch = surf->stencil_level[0].nblk_x;
		else
			AddrSurfInfoIn->basePitch = surf->level[0].nblk_x;

		/* Convert blocks to pixels for compressed formats. */
		if (compressed)
			AddrSurfInfoIn->basePitch *= surf->blk_w;
	}

	ret = AddrComputeSurfaceInfo(addrlib,
				     AddrSurfInfoIn,
				     AddrSurfInfoOut);
	if (ret != ADDR_OK)
		return ret;

	surf_level = is_stencil ? &surf->stencil_level[level] : &surf->level[level];
	surf_level->offset = align64(surf->bo_size, AddrSurfInfoOut->baseAlign);
	surf_level->slice_size = AddrSurfInfoOut->sliceSize;
	surf_level->pitch_bytes = AddrSurfInfoOut->pitch * (is_stencil ? 1 : surf->bpe);
	surf_level->npix_x = u_minify(surf->npix_x, level);
	surf_level->npix_y = u_minify(surf->npix_y, level);
	surf_level->npix_z = u_minify(surf->npix_z, level);
	surf_level->nblk_x = AddrSurfInfoOut->pitch;
	surf_level->nblk_y = AddrSurfInfoOut->height;
	if (type == RADEON_SURF_TYPE_3D)
		surf_level->nblk_z = AddrSurfInfoOut->depth;
	else
		surf_level->nblk_z = 1;

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
		surf->stencil_tiling_index[level] = AddrSurfInfoOut->tileIndex;
	else
		surf->tiling_index[level] = AddrSurfInfoOut->tileIndex;

	surf->bo_size = surf_level->offset + AddrSurfInfoOut->surfSize;

	/* Clear DCC fields at the beginning. */
	surf_level->dcc_offset = 0;
	surf_level->dcc_enabled = false;

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
			surf_level->dcc_enabled = true;
			surf->dcc_size = surf_level->dcc_offset + AddrDccOut->dccRamSize;
			surf->dcc_alignment = MAX2(surf->dcc_alignment, AddrDccOut->dccRamBaseAlign);
		}
	}

	return 0;
}

static void radv_set_micro_tile_mode(struct radeon_surf *surf,
                                     struct radeon_info *info)
{
	uint32_t tile_mode = info->si_tile_mode_array[surf->tiling_index[0]];

	if (info->chip_class >= CIK)
		surf->micro_tile_mode = G_009910_MICRO_TILE_MODE_NEW(tile_mode);
	else
		surf->micro_tile_mode = G_009910_MICRO_TILE_MODE(tile_mode);
}

static unsigned cik_get_macro_tile_index(struct radeon_surf *surf)
{
	unsigned index, tileb;

	tileb = 8 * 8 * surf->bpe;
	tileb = MIN2(surf->tile_split, tileb);

	for (index = 0; tileb > 64; index++)
		tileb >>= 1;

	assert(index < 16);
	return index;
}

static int radv_amdgpu_winsys_surface_init(struct radeon_winsys *_ws,
					   struct radeon_surf *surf)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);
	unsigned level, mode, type;
	bool compressed;
	ADDR_COMPUTE_SURFACE_INFO_INPUT AddrSurfInfoIn = {0};
	ADDR_COMPUTE_SURFACE_INFO_OUTPUT AddrSurfInfoOut = {0};
	ADDR_COMPUTE_DCCINFO_INPUT AddrDccIn = {0};
	ADDR_COMPUTE_DCCINFO_OUTPUT AddrDccOut = {0};
	ADDR_TILEINFO AddrTileInfoIn = {0};
	ADDR_TILEINFO AddrTileInfoOut = {0};
	int r;

	r = radv_amdgpu_surface_sanity(surf);
	if (r)
		return r;

	AddrSurfInfoIn.size = sizeof(ADDR_COMPUTE_SURFACE_INFO_INPUT);
	AddrSurfInfoOut.size = sizeof(ADDR_COMPUTE_SURFACE_INFO_OUTPUT);
	AddrDccIn.size = sizeof(ADDR_COMPUTE_DCCINFO_INPUT);
	AddrDccOut.size = sizeof(ADDR_COMPUTE_DCCINFO_OUTPUT);
	AddrSurfInfoOut.pTileInfo = &AddrTileInfoOut;

	type = RADEON_SURF_GET(surf->flags, TYPE);
	mode = RADEON_SURF_GET(surf->flags, MODE);
	compressed = surf->blk_w == 4 && surf->blk_h == 4;

	/* MSAA and FMASK require 2D tiling. */
	if (surf->nsamples > 1 ||
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
		AddrDccIn.bpp = AddrSurfInfoIn.bpp = surf->bpe * 8;
	}

	AddrDccIn.numSamples = AddrSurfInfoIn.numSamples = surf->nsamples;
	AddrSurfInfoIn.tileIndex = -1;

	/* Set the micro tile type. */
	if (surf->flags & RADEON_SURF_SCANOUT)
		AddrSurfInfoIn.tileType = ADDR_DISPLAYABLE;
	else if (surf->flags & RADEON_SURF_Z_OR_SBUFFER)
		AddrSurfInfoIn.tileType = ADDR_DEPTH_SAMPLE_ORDER;
	else
		AddrSurfInfoIn.tileType = ADDR_NON_DISPLAYABLE;

	AddrSurfInfoIn.flags.color = !(surf->flags & RADEON_SURF_Z_OR_SBUFFER);
	AddrSurfInfoIn.flags.depth = (surf->flags & RADEON_SURF_ZBUFFER) != 0;
	AddrSurfInfoIn.flags.cube = type == RADEON_SURF_TYPE_CUBEMAP;
	AddrSurfInfoIn.flags.display = (surf->flags & RADEON_SURF_SCANOUT) != 0;
	AddrSurfInfoIn.flags.pow2Pad = surf->last_level > 0;
	AddrSurfInfoIn.flags.degrade4Space = 1;

	/* DCC notes:
	 * - If we add MSAA support, keep in mind that CB can't decompress 8bpp
	 *   with samples >= 4.
	 * - Mipmapped array textures have low performance (discovered by a closed
	 *   driver team).
	 */
	AddrSurfInfoIn.flags.dccCompatible = !(surf->flags & RADEON_SURF_Z_OR_SBUFFER) &&
		!(surf->flags & RADEON_SURF_DISABLE_DCC) &&
		!compressed && AddrDccIn.numSamples <= 1 &&
		((surf->array_size == 1 && surf->npix_z == 1) ||
		 surf->last_level == 0);

	AddrSurfInfoIn.flags.noStencil = (surf->flags & RADEON_SURF_SBUFFER) == 0;
	AddrSurfInfoIn.flags.compressZ = AddrSurfInfoIn.flags.depth;

	/* noStencil = 0 can result in a depth part that is incompatible with
	 * mipmapped texturing. So set noStencil = 1 when mipmaps are requested (in
	 * this case, we may end up setting stencil_adjusted).
	 *
	 * TODO: update addrlib to a newer version, remove this, and
	 * use flags.matchStencilTileCfg = 1 as an alternative fix.
	 */
	if (surf->last_level > 0)
		AddrSurfInfoIn.flags.noStencil = 1;

	/* Set preferred macrotile parameters. This is usually required
	 * for shared resources. This is for 2D tiling only. */
	if (AddrSurfInfoIn.tileMode >= ADDR_TM_2D_TILED_THIN1 &&
	    surf->bankw && surf->bankh && surf->mtilea && surf->tile_split) {
		/* If any of these parameters are incorrect, the calculation
		 * will fail. */
		AddrTileInfoIn.banks = surf->num_banks;
		AddrTileInfoIn.bankWidth = surf->bankw;
		AddrTileInfoIn.bankHeight = surf->bankh;
		AddrTileInfoIn.macroAspectRatio = surf->mtilea;
		AddrTileInfoIn.tileSplitBytes = surf->tile_split;
		AddrTileInfoIn.pipeConfig = surf->pipe_config + 1; /* +1 compared to GB_TILE_MODE */
		AddrSurfInfoIn.flags.degrade4Space = 0;
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

		if (ws->info.chip_class == SI) {
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
			if (AddrSurfInfoIn.tileType == ADDR_DISPLAYABLE)
				AddrSurfInfoIn.tileIndex = 10; /* 2D displayable */
			else
				AddrSurfInfoIn.tileIndex = 14; /* 2D non-displayable */
			AddrSurfInfoOut.macroModeIndex = cik_get_macro_tile_index(surf);
		}
	}

	surf->bo_size = 0;
	surf->dcc_size = 0;
	surf->dcc_alignment = 1;

	/* Calculate texture layout information. */
	for (level = 0; level <= surf->last_level; level++) {
		r = radv_compute_level(ws->addrlib, surf, false, level, type, compressed,
				       &AddrSurfInfoIn, &AddrSurfInfoOut, &AddrDccIn, &AddrDccOut);
		if (r)
			return r;

		if (level == 0) {
			surf->bo_alignment = AddrSurfInfoOut.baseAlign;
			surf->pipe_config = AddrSurfInfoOut.pTileInfo->pipeConfig - 1;
			radv_set_micro_tile_mode(surf, &ws->info);

			/* For 2D modes only. */
			if (AddrSurfInfoOut.tileMode >= ADDR_TM_2D_TILED_THIN1) {
				surf->bankw = AddrSurfInfoOut.pTileInfo->bankWidth;
				surf->bankh = AddrSurfInfoOut.pTileInfo->bankHeight;
				surf->mtilea = AddrSurfInfoOut.pTileInfo->macroAspectRatio;
				surf->tile_split = AddrSurfInfoOut.pTileInfo->tileSplitBytes;
				surf->num_banks = AddrSurfInfoOut.pTileInfo->banks;
				surf->macro_tile_index = AddrSurfInfoOut.macroModeIndex;
			} else {
				surf->macro_tile_index = 0;
			}
		}
	}

	/* Calculate texture layout information for stencil. */
	if (surf->flags & RADEON_SURF_SBUFFER) {
		AddrSurfInfoIn.bpp = 8;
		AddrSurfInfoIn.flags.depth = 0;
		AddrSurfInfoIn.flags.stencil = 1;
		/* This will be ignored if AddrSurfInfoIn.pTileInfo is NULL. */
		AddrTileInfoIn.tileSplitBytes = surf->stencil_tile_split;

		for (level = 0; level <= surf->last_level; level++) {
			r = radv_compute_level(ws->addrlib, surf, true, level, type, compressed,
					       &AddrSurfInfoIn, &AddrSurfInfoOut, &AddrDccIn, &AddrDccOut);
			if (r)
				return r;

			/* DB uses the depth pitch for both stencil and depth. */
			if (surf->stencil_level[level].nblk_x != surf->level[level].nblk_x)
				surf->stencil_adjusted = true;

			if (level == 0) {
				/* For 2D modes only. */
				if (AddrSurfInfoOut.tileMode >= ADDR_TM_2D_TILED_THIN1) {
					surf->stencil_tile_split =
						AddrSurfInfoOut.pTileInfo->tileSplitBytes;
				}
			}
		}
	}

	/* Recalculate the whole DCC miptree size including disabled levels.
	 * This is what addrlib does, but calling addrlib would be a lot more
	 * complicated.
	 */
#if 0
	if (surf->dcc_size && surf->last_level > 0) {
		surf->dcc_size = align64(surf->bo_size >> 8,
					 ws->info.pipe_interleave_bytes *
					 ws->info.num_tile_pipes);
	}
#endif
	return 0;
}

static int radv_amdgpu_winsys_surface_best(struct radeon_winsys *rws,
					   struct radeon_surf *surf)
{
	return 0;
}

void radv_amdgpu_surface_init_functions(struct radv_amdgpu_winsys *ws)
{
	ws->base.surface_init = radv_amdgpu_winsys_surface_init;
	ws->base.surface_best = radv_amdgpu_winsys_surface_best;
}
