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

#ifndef FREEDRENO_DEVICE_INFO_H
#define FREEDRENO_DEVICE_INFO_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Freedreno hardware description and quirks
 */

struct freedreno_dev_info {
	/* alignment for size of tiles */
	uint32_t tile_align_w, tile_align_h;
	/* gmem load/store granularity */
	uint32_t gmem_align_w, gmem_align_h;
	/* max tile size */
	uint32_t tile_max_w, tile_max_h;
	
	uint32_t num_vsc_pipes;

	union {
		struct {
			/* Whether the PC_MULTIVIEW_MASK register exists. */
			bool supports_multiview_mask;

			/* info for setting RB_CCU_CNTL */
			uint32_t ccu_offset_gmem;
			uint32_t ccu_offset_bypass;
			bool ccu_cntl_gmem_unk2;

			struct {
				uint32_t RB_UNKNOWN_8E04_blit;
				uint32_t PC_UNKNOWN_9805;
				uint32_t SP_UNKNOWN_A0F8;
			} magic;
		} a6xx;
	};
};

void freedreno_dev_info_init(struct freedreno_dev_info *info, uint32_t gpu_id);

#endif /* FREEDRENO_DEVICE_INFO_H */

