 /*
  * Copyright © 2020 Valve Corporation
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
  *
  */

#include "freedreno_dev_info.h"
#include "util/macros.h"

static inline unsigned
max_bitfield_val(unsigned high, unsigned low, unsigned shift)
{
	return BITFIELD_MASK(high - low) << shift;
}

void
freedreno_dev_info_init(struct freedreno_dev_info *info, uint32_t gpu_id)
{
	if (gpu_id >= 600) {
		info->gmem_align_w = 16;
		info->gmem_align_h = 4;
		info->tile_align_w = gpu_id == 650 ? 96 : 32;
		info->tile_align_h = 32;
		/* based on GRAS_BIN_CONTROL: */
		info->tile_max_w   = 1024;  /* max_bitfield_val(5, 0, 5) */
		info->tile_max_h   = max_bitfield_val(14, 8, 4);
		info->num_vsc_pipes = 32;

		switch (gpu_id) {
		case 615:
		case 618:
			info->num_sp_cores = 1;
			info->fibers_per_sp = 128 * 16;
			info->a6xx.ccu_offset_gmem = 0x7c000;
			info->a6xx.ccu_offset_bypass = 0x10000;
			info->a6xx.ccu_cntl_gmem_unk2 = true;
			info->a6xx.supports_multiview_mask = false;
			info->a6xx.magic.RB_UNKNOWN_8E04_blit = 0x00100000;
			info->a6xx.magic.PC_UNKNOWN_9805 = 0;
			info->a6xx.magic.SP_UNKNOWN_A0F8 = 0;
			break;
		case 630:
			info->num_sp_cores = 2;
			info->fibers_per_sp = 128 * 16;
			info->a6xx.ccu_offset_gmem = 0xf8000;
			info->a6xx.ccu_offset_bypass = 0x20000;
			info->a6xx.ccu_cntl_gmem_unk2 = true;
			info->a6xx.supports_multiview_mask = false;
			info->a6xx.magic.RB_UNKNOWN_8E04_blit = 0x01000000;
			info->a6xx.magic.PC_UNKNOWN_9805 = 1;
			info->a6xx.magic.SP_UNKNOWN_A0F8 = 1;
			break;
		case 640:
			info->num_sp_cores = 2;
			/* The wavefront ID returned by the getwid instruction has a
			 * maximum of 3 * 10 - 1, or so it seems. However the swizzled
			 * index used in the mem offset calcuation is
			 * "(wid / 3) | ((wid % 3) << 4)", so that the actual max is
			 * around 3 * 16. Furthermore, with the per-fiber layout, the HW
			 * swizzles the wavefront index and fiber index itself, and it
			 * pads the number of wavefronts to 4 * 16 to make the swizzling
			 * simpler, so we have to bump the number of wavefronts to 4 * 16
			 * for the per-fiber layout. We could theoretically reduce it for
			 * the per-wave layout though.
			 */
			info->fibers_per_sp = 128 * 4 * 16;
			info->a6xx.ccu_offset_gmem = 0xf8000;
			info->a6xx.ccu_offset_bypass = 0x20000;
			info->a6xx.supports_multiview_mask = true;
			info->a6xx.magic.RB_UNKNOWN_8E04_blit = 0x00100000;
			info->a6xx.magic.PC_UNKNOWN_9805 = 1;
			info->a6xx.magic.SP_UNKNOWN_A0F8 = 1;
			info->a6xx.has_z24uint_s8uint = true;
			break;
		case 650:
			info->num_sp_cores = 3;
			info->fibers_per_sp = 128 * 2 * 16;
			info->a6xx.ccu_offset_gmem = 0x114000;
			info->a6xx.ccu_offset_bypass = 0x30000;
			info->a6xx.supports_multiview_mask = true;
			info->a6xx.magic.RB_UNKNOWN_8E04_blit = 0x04100000;
			info->a6xx.magic.PC_UNKNOWN_9805 = 2;
			info->a6xx.magic.SP_UNKNOWN_A0F8 = 2;
			info->a6xx.has_z24uint_s8uint = true;
			break;
		default:
			/* Drivers should be doing their own version filtering, so we
			 * should never get here.
			 */
			unreachable("missing a6xx config");
		}
	} else if (gpu_id >= 500) {
		info->gmem_align_w = info->tile_align_w = 64;
		info->gmem_align_h = info->tile_align_h = 32;
		/* based on VSC_BIN_SIZE: */
		info->tile_max_w   = 1024;  /* max_bitfield_val(7, 0, 5) */
		info->tile_max_h   = max_bitfield_val(16, 9, 5);
		info->num_vsc_pipes = 16;
	} else if (gpu_id >= 400) {
		info->gmem_align_w = info->tile_align_w = 32;
		info->gmem_align_h = info->tile_align_h = 32;
		/* based on VSC_BIN_SIZE: */
		info->tile_max_w   = 1024;  /* max_bitfield_val(4, 0, 5) */
		info->tile_max_h   = max_bitfield_val(9, 5, 5);
		info->num_vsc_pipes = 8;
	} else if (gpu_id >= 300) {
		info->gmem_align_w = info->tile_align_w = 32;
		info->gmem_align_h = info->tile_align_h = 32;
		/* based on VSC_BIN_SIZE: */
		info->tile_max_w   = 992;  /* max_bitfield_val(4, 0, 5) */
		info->tile_max_h   = max_bitfield_val(9, 5, 5);
		info->num_vsc_pipes = 8;
	} else {
		info->gmem_align_w = info->tile_align_w = 32;
		info->gmem_align_h = info->tile_align_h = 32;
		info->tile_max_w   = 512;
		info->tile_max_h   = ~0; /* TODO */
		info->num_vsc_pipes = 8;
	}
}
