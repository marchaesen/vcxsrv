/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * Based on radeon_winsys.h which is:
 * Copyright 2008 Corbin Simpson <MostAwesomeDude@gmail.com>
 * Copyright 2010 Marek Olšák <maraeo@gmail.com>
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

#ifndef RADV_RADEON_WINSYS_H
#define RADV_RADEON_WINSYS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "main/macros.h"
#include "amd_family.h"

#define FREE(x) free(x)

enum radeon_bo_domain { /* bitfield */
	RADEON_DOMAIN_GTT  = 2,
	RADEON_DOMAIN_VRAM = 4,
	RADEON_DOMAIN_VRAM_GTT = RADEON_DOMAIN_VRAM | RADEON_DOMAIN_GTT
};

enum radeon_bo_flag { /* bitfield */
	RADEON_FLAG_GTT_WC =        (1 << 0),
	RADEON_FLAG_CPU_ACCESS =    (1 << 1),
	RADEON_FLAG_NO_CPU_ACCESS = (1 << 2),
};

enum radeon_bo_usage { /* bitfield */
	RADEON_USAGE_READ = 2,
	RADEON_USAGE_WRITE = 4,
	RADEON_USAGE_READWRITE = RADEON_USAGE_READ | RADEON_USAGE_WRITE
};

enum ring_type {
	RING_GFX = 0,
	RING_COMPUTE,
	RING_DMA,
	RING_UVD,
	RING_VCE,
	RING_LAST,
};

struct radeon_winsys_cs {
	unsigned cdw;  /* Number of used dwords. */
	unsigned max_dw; /* Maximum number of dwords. */
	uint32_t *buf; /* The base pointer of the chunk. */
};

struct radeon_info {
	/* PCI info: domain:bus:dev:func */
	uint32_t                    pci_domain;
	uint32_t                    pci_bus;
	uint32_t                    pci_dev;
	uint32_t                    pci_func;

	/* Device info. */
	uint32_t                    pci_id;
	enum radeon_family          family;
	const char                  *name;
	enum chip_class             chip_class;
	uint32_t                    gart_page_size;
	uint64_t                    gart_size;
	uint64_t                    vram_size;
	bool                        has_dedicated_vram;
	bool                     has_virtual_memory;
	bool                        gfx_ib_pad_with_type2;
	bool                     has_sdma;
	bool                     has_uvd;
	uint32_t                    vce_fw_version;
	uint32_t                    vce_harvest_config;
	uint32_t                    clock_crystal_freq;

	/* Kernel info. */
	uint32_t                    drm_major; /* version */
	uint32_t                    drm_minor;
	uint32_t                    drm_patchlevel;
	bool                     has_userptr;

	/* Shader cores. */
	uint32_t                    r600_max_quad_pipes; /* wave size / 16 */
	uint32_t                    max_shader_clock;
	uint32_t                    num_good_compute_units;
	uint32_t                    max_se; /* shader engines */
	uint32_t                    max_sh_per_se; /* shader arrays per shader engine */

	/* Render backends (color + depth blocks). */
	uint32_t                    r300_num_gb_pipes;
	uint32_t                    r300_num_z_pipes;
	uint32_t                    r600_gb_backend_map; /* R600 harvest config */
	bool                     r600_gb_backend_map_valid;
	uint32_t                    r600_num_banks;
	uint32_t                    num_render_backends;
	uint32_t                    num_tile_pipes; /* pipe count from PIPE_CONFIG */
	uint32_t                    pipe_interleave_bytes;
	uint32_t                    enabled_rb_mask; /* GCN harvest config */

	/* Tile modes. */
	uint32_t                    si_tile_mode_array[32];
	uint32_t                    cik_macrotile_mode_array[16];
};

#define RADEON_SURF_MAX_LEVEL                   32

#define RADEON_SURF_TYPE_MASK                   0xFF
#define RADEON_SURF_TYPE_SHIFT                  0
#define     RADEON_SURF_TYPE_1D                     0
#define     RADEON_SURF_TYPE_2D                     1
#define     RADEON_SURF_TYPE_3D                     2
#define     RADEON_SURF_TYPE_CUBEMAP                3
#define     RADEON_SURF_TYPE_1D_ARRAY               4
#define     RADEON_SURF_TYPE_2D_ARRAY               5
#define RADEON_SURF_MODE_MASK                   0xFF
#define RADEON_SURF_MODE_SHIFT                  8
#define     RADEON_SURF_MODE_LINEAR_ALIGNED         1
#define     RADEON_SURF_MODE_1D                     2
#define     RADEON_SURF_MODE_2D                     3
#define RADEON_SURF_SCANOUT                     (1 << 16)
#define RADEON_SURF_ZBUFFER                     (1 << 17)
#define RADEON_SURF_SBUFFER                     (1 << 18)
#define RADEON_SURF_Z_OR_SBUFFER                (RADEON_SURF_ZBUFFER | RADEON_SURF_SBUFFER)
#define RADEON_SURF_HAS_SBUFFER_MIPTREE         (1 << 19)
#define RADEON_SURF_HAS_TILE_MODE_INDEX         (1 << 20)
#define RADEON_SURF_FMASK                       (1 << 21)
#define RADEON_SURF_DISABLE_DCC                 (1 << 22)

#define RADEON_SURF_GET(v, field)   (((v) >> RADEON_SURF_ ## field ## _SHIFT) & RADEON_SURF_ ## field ## _MASK)
#define RADEON_SURF_SET(v, field)   (((v) & RADEON_SURF_ ## field ## _MASK) << RADEON_SURF_ ## field ## _SHIFT)
#define RADEON_SURF_CLR(v, field)   ((v) & ~(RADEON_SURF_ ## field ## _MASK << RADEON_SURF_ ## field ## _SHIFT))

struct radeon_surf_level {
	uint64_t                    offset;
	uint64_t                    slice_size;
	uint32_t                    npix_x;
	uint32_t                    npix_y;
	uint32_t                    npix_z;
	uint32_t                    nblk_x;
	uint32_t                    nblk_y;
	uint32_t                    nblk_z;
	uint32_t                    pitch_bytes;
	uint32_t                    mode;
	uint64_t                    dcc_offset;
	uint64_t                    dcc_fast_clear_size;
	bool                        dcc_enabled;
};


/* surface defintions from the winsys */
struct radeon_surf {
	/* These are inputs to the calculator. */
	uint32_t                    npix_x;
	uint32_t                    npix_y;
	uint32_t                    npix_z;
	uint32_t                    blk_w;
	uint32_t                    blk_h;
	uint32_t                    blk_d;
	uint32_t                    array_size;
	uint32_t                    last_level;
	uint32_t                    bpe;
	uint32_t                    nsamples;
	uint32_t                    flags;

	/* These are return values. Some of them can be set by the caller, but
	 * they will be treated as hints (e.g. bankw, bankh) and might be
	 * changed by the calculator.
	 */
	uint64_t                    bo_size;
	uint64_t                    bo_alignment;
	/* This applies to EG and later. */
	uint32_t                    bankw;
	uint32_t                    bankh;
	uint32_t                    mtilea;
	uint32_t                    tile_split;
	uint32_t                    stencil_tile_split;
	uint64_t                    stencil_offset;
	struct radeon_surf_level    level[RADEON_SURF_MAX_LEVEL];
	struct radeon_surf_level    stencil_level[RADEON_SURF_MAX_LEVEL];
	uint32_t                    tiling_index[RADEON_SURF_MAX_LEVEL];
	uint32_t                    stencil_tiling_index[RADEON_SURF_MAX_LEVEL];
	uint32_t                    pipe_config;
	uint32_t                    num_banks;
	uint32_t                    macro_tile_index;
	uint32_t                    micro_tile_mode; /* displayable, thin, depth, rotated */

	/* Whether the depth miptree or stencil miptree as used by the DB are
	 * adjusted from their TC compatible form to ensure depth/stencil
	 * compatibility. If either is true, the corresponding plane cannot be
	 * sampled from.
	 */
	bool                        depth_adjusted;
	bool                        stencil_adjusted;

	uint64_t                    dcc_size;
	uint64_t                    dcc_alignment;
};

enum radeon_bo_layout {
	RADEON_LAYOUT_LINEAR = 0,
	RADEON_LAYOUT_TILED,
	RADEON_LAYOUT_SQUARETILED,

	RADEON_LAYOUT_UNKNOWN
};

/* Tiling info for display code, DRI sharing, and other data. */
struct radeon_bo_metadata {
	/* Tiling flags describing the texture layout for display code
	 * and DRI sharing.
	 */
	enum radeon_bo_layout   microtile;
	enum radeon_bo_layout   macrotile;
	unsigned                pipe_config;
	unsigned                bankw;
	unsigned                bankh;
	unsigned                tile_split;
	unsigned                mtilea;
	unsigned                num_banks;
	unsigned                stride;
	bool                    scanout;

	/* Additional metadata associated with the buffer, in bytes.
	 * The maximum size is 64 * 4. This is opaque for the winsys & kernel.
	 * Supported by amdgpu only.
	 */
	uint32_t                size_metadata;
	uint32_t                metadata[64];
};

struct radeon_winsys_bo;
struct radeon_winsys_fence;

struct radeon_winsys {
	void (*destroy)(struct radeon_winsys *ws);

	void (*query_info)(struct radeon_winsys *ws,
			   struct radeon_info *info);

	struct radeon_winsys_bo *(*buffer_create)(struct radeon_winsys *ws,
						  uint64_t size,
						  unsigned alignment,
						  enum radeon_bo_domain domain,
						  enum radeon_bo_flag flags);

	void (*buffer_destroy)(struct radeon_winsys_bo *bo);
	void *(*buffer_map)(struct radeon_winsys_bo *bo);

	struct radeon_winsys_bo *(*buffer_from_fd)(struct radeon_winsys *ws,
						   int fd,
						   unsigned *stride, unsigned *offset);

	bool (*buffer_get_fd)(struct radeon_winsys *ws,
			      struct radeon_winsys_bo *bo,
			      int *fd);

	void (*buffer_unmap)(struct radeon_winsys_bo *bo);

	uint64_t (*buffer_get_va)(struct radeon_winsys_bo *bo);

	void (*buffer_set_metadata)(struct radeon_winsys_bo *bo,
				    struct radeon_bo_metadata *md);
	struct radeon_winsys_ctx *(*ctx_create)(struct radeon_winsys *ws);
	void (*ctx_destroy)(struct radeon_winsys_ctx *ctx);

	bool (*ctx_wait_idle)(struct radeon_winsys_ctx *ctx);

	struct radeon_winsys_cs *(*cs_create)(struct radeon_winsys *ws,
					      enum ring_type ring_type);

	void (*cs_destroy)(struct radeon_winsys_cs *cs);

	void (*cs_reset)(struct radeon_winsys_cs *cs);

	bool (*cs_finalize)(struct radeon_winsys_cs *cs);

	void (*cs_grow)(struct radeon_winsys_cs * cs, size_t min_size);

	int (*cs_submit)(struct radeon_winsys_ctx *ctx,
			 struct radeon_winsys_cs **cs_array,
			 unsigned cs_count,
			 bool can_patch,
			 struct radeon_winsys_fence *fence);

	void (*cs_add_buffer)(struct radeon_winsys_cs *cs,
			      struct radeon_winsys_bo *bo,
			      uint8_t priority);

	void (*cs_execute_secondary)(struct radeon_winsys_cs *parent,
				    struct radeon_winsys_cs *child);

	int (*surface_init)(struct radeon_winsys *ws,
			    struct radeon_surf *surf);

	int (*surface_best)(struct radeon_winsys *ws,
			    struct radeon_surf *surf);

	struct radeon_winsys_fence *(*create_fence)();
	void (*destroy_fence)(struct radeon_winsys_fence *fence);
	bool (*fence_wait)(struct radeon_winsys *ws,
			   struct radeon_winsys_fence *fence,
			   bool absolute,
			   uint64_t timeout);
};

static inline void radeon_emit(struct radeon_winsys_cs *cs, uint32_t value)
{
	cs->buf[cs->cdw++] = value;
}

static inline void radeon_emit_array(struct radeon_winsys_cs *cs,
				     const uint32_t *values, unsigned count)
{
	memcpy(cs->buf + cs->cdw, values, count * 4);
	cs->cdw += count;
}

#endif /* RADV_RADEON_WINSYS_H */
