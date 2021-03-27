/*
 * Copyright © 2020 Valve Corporation
 *
 * based on amdgpu winsys.
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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
#include "radv_null_winsys_public.h"

#include "radv_null_bo.h"
#include "radv_null_cs.h"

#include "ac_llvm_util.h"

/* Hardcode some GPU info that are needed for the driver or for some tools. */
static const struct {
	uint32_t pci_id;
	uint32_t num_render_backends;
	bool has_dedicated_vram;
} gpu_info[] = {
	[CHIP_TAHITI] = { 0x6780, 8, true },
	[CHIP_PITCAIRN] = { 0x6800, 8, true },
	[CHIP_VERDE] = { 0x6820, 4, true },
	[CHIP_OLAND] = { 0x6060, 2, true },
	[CHIP_HAINAN] = { 0x6660, 2, true },
	[CHIP_BONAIRE] = { 0x6640, 4, true },
	[CHIP_KAVERI] = { 0x1304, 2, false },
	[CHIP_KABINI] = { 0x9830, 2, false },
	[CHIP_HAWAII] = { 0x67A0, 16, true },
	[CHIP_TONGA] = { 0x6920, 8, true },
	[CHIP_ICELAND] = { 0x6900, 2, true },
	[CHIP_CARRIZO] = { 0x9870, 2, false },
	[CHIP_FIJI] = { 0x7300, 16, true },
	[CHIP_STONEY] = { 0x98E4, 2, false },
	[CHIP_POLARIS10] = { 0x67C0, 8, true },
	[CHIP_POLARIS11] = { 0x67E0, 4, true },
	[CHIP_POLARIS12] = { 0x6980, 4, true },
	[CHIP_VEGAM] = { 0x694C, 4, true },
	[CHIP_VEGA10] = { 0x6860, 16, true },
	[CHIP_VEGA12] = { 0x69A0, 8, true },
	[CHIP_VEGA20] = { 0x66A0, 16, true },
	[CHIP_RAVEN] = { 0x15DD, 2, false },
	[CHIP_RENOIR] = { 0x1636, 2, false },
	[CHIP_ARCTURUS] = { 0x738C, 2, true },
	[CHIP_NAVI10] = { 0x7310, 16, true },
	[CHIP_NAVI12] = { 0x7360, 8, true },
	[CHIP_NAVI14] = { 0x7340, 8, true },
	[CHIP_SIENNA_CICHLID] = { 0x73A0, 8, true },
	[CHIP_VANGOGH] = { 0x163F, 8, false },
	[CHIP_NAVY_FLOUNDER] = { 0x73C0, 8, true },
	[CHIP_DIMGREY_CAVEFISH] = { 0x73E0, 8, true },
};

static void radv_null_winsys_query_info(struct radeon_winsys *rws,
					struct radeon_info *info)
{
	const char *family = getenv("RADV_FORCE_FAMILY");
	unsigned i;

	info->chip_class = CLASS_UNKNOWN;
	info->family = CHIP_UNKNOWN;

	for (i = CHIP_TAHITI; i < CHIP_LAST; i++) {
		if (!strcmp(family, ac_get_llvm_processor_name(i))) {
			/* Override family and chip_class. */
			info->family = i;
			info->name = "OVERRIDDEN";

			if (i >= CHIP_SIENNA_CICHLID)
				info->chip_class = GFX10_3;
			else if (i >= CHIP_NAVI10)
				info->chip_class = GFX10;
			else if (i >= CHIP_VEGA10)
				info->chip_class = GFX9;
			else if (i >= CHIP_TONGA)
				info->chip_class = GFX8;
			else if (i >= CHIP_BONAIRE)
				info->chip_class = GFX7;
			else
				info->chip_class = GFX6;
		}
	}

	if (info->family == CHIP_UNKNOWN) {
		fprintf(stderr, "radv: Unknown family: %s\n", family);
		abort();
	}

	info->pci_id = gpu_info[info->family].pci_id;
	info->has_syncobj_wait_for_submit = true;
	info->max_se = 4;
        info->num_se = 4;
	if (info->chip_class >= GFX10_3)
		info->max_wave64_per_simd = 16;
	else if (info->chip_class >= GFX10)
		info->max_wave64_per_simd = 20;
	else if (info->family >= CHIP_POLARIS10 && info->family <= CHIP_VEGAM)
		info->max_wave64_per_simd = 8;
	else
		info->max_wave64_per_simd = 10;

	if (info->chip_class >= GFX10)
		info->num_physical_sgprs_per_simd = 128 * info->max_wave64_per_simd * 2;
	else if (info->chip_class >= GFX8)
		info->num_physical_sgprs_per_simd = 800;
	else
		info->num_physical_sgprs_per_simd = 512;

	info->num_physical_wave64_vgprs_per_simd = info->chip_class >= GFX10 ? 512 : 256;
	info->num_simd_per_compute_unit = info->chip_class >= GFX10 ? 2 : 4;
	info->lds_size_per_workgroup = info->chip_class >= GFX10 ? 128 * 1024 : 64 * 1024;
	info->lds_encode_granularity = info->chip_class >= GFX7 ? 128 * 4 : 64 * 4;
	info->lds_alloc_granularity = info->chip_class >= GFX10_3 ? 256 * 4 : info->lds_encode_granularity;
	info->max_render_backends = gpu_info[info->family].num_render_backends;

	info->has_dedicated_vram = gpu_info[info->family].has_dedicated_vram;
	info->has_packed_math_16bit = info->chip_class >= GFX9;
}

static void radv_null_winsys_destroy(struct radeon_winsys *rws)
{
	FREE(rws);
}

struct radeon_winsys *
radv_null_winsys_create()
{
	struct radv_null_winsys *ws;

	ws = calloc(1, sizeof(struct radv_null_winsys));
	if (!ws)
		return NULL;

	ws->base.destroy = radv_null_winsys_destroy;
	ws->base.query_info = radv_null_winsys_query_info;
	radv_null_bo_init_functions(ws);
	radv_null_cs_init_functions(ws);

	return &ws->base;
}
