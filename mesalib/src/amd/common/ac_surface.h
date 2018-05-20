/*
 * Copyright © 2017 Advanced Micro Devices, Inc.
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

#ifndef AC_SURFACE_H
#define AC_SURFACE_H

#include <stdint.h>

#include "amd_family.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
typedef void* ADDR_HANDLE;

struct amdgpu_gpu_info;
struct radeon_info;

#define RADEON_SURF_MAX_LEVELS                  15

enum radeon_surf_mode {
    RADEON_SURF_MODE_LINEAR_ALIGNED = 1,
    RADEON_SURF_MODE_1D = 2,
    RADEON_SURF_MODE_2D = 3,
};

/* These are defined exactly like GB_TILE_MODEn.MICRO_TILE_MODE_NEW. */
enum radeon_micro_mode {
    RADEON_MICRO_MODE_DISPLAY = 0,
    RADEON_MICRO_MODE_THIN = 1,
    RADEON_MICRO_MODE_DEPTH = 2,
    RADEON_MICRO_MODE_ROTATED = 3,
};

/* the first 16 bits are reserved for libdrm_radeon, don't use them */
#define RADEON_SURF_SCANOUT                     (1 << 16)
#define RADEON_SURF_ZBUFFER                     (1 << 17)
#define RADEON_SURF_SBUFFER                     (1 << 18)
#define RADEON_SURF_Z_OR_SBUFFER                (RADEON_SURF_ZBUFFER | RADEON_SURF_SBUFFER)
/* bits 19 and 20 are reserved for libdrm_radeon, don't use them */
#define RADEON_SURF_FMASK                       (1 << 21)
#define RADEON_SURF_DISABLE_DCC                 (1 << 22)
#define RADEON_SURF_TC_COMPATIBLE_HTILE         (1 << 23)
#define RADEON_SURF_IMPORTED                    (1 << 24)
#define RADEON_SURF_OPTIMIZE_FOR_SPACE          (1 << 25)
#define RADEON_SURF_SHAREABLE                   (1 << 26)

struct legacy_surf_level {
    uint64_t                    offset;
    uint32_t                    slice_size_dw; /* in dwords; max = 4GB / 4. */
    uint32_t                    dcc_offset; /* relative offset within DCC mip tree */
    uint32_t                    dcc_fast_clear_size;
    unsigned                    nblk_x:15;
    unsigned                    nblk_y:15;
    enum radeon_surf_mode       mode:2;
};

struct legacy_surf_fmask {
    unsigned slice_tile_max; /* max 4M */
    uint8_t tiling_index;    /* max 31 */
    uint8_t bankh;           /* max 8 */
    uint16_t pitch_in_pixels;
};

struct legacy_surf_layout {
    unsigned                    bankw:4;  /* max 8 */
    unsigned                    bankh:4;  /* max 8 */
    unsigned                    mtilea:4; /* max 8 */
    unsigned                    tile_split:13;         /* max 4K */
    unsigned                    stencil_tile_split:13; /* max 4K */
    unsigned                    pipe_config:5;      /* max 17 */
    unsigned                    num_banks:5;        /* max 16 */
    unsigned                    macro_tile_index:4; /* max 15 */

    /* Whether the depth miptree or stencil miptree as used by the DB are
     * adjusted from their TC compatible form to ensure depth/stencil
     * compatibility. If either is true, the corresponding plane cannot be
     * sampled from.
     */
    unsigned                    depth_adjusted:1;
    unsigned                    stencil_adjusted:1;

    struct legacy_surf_level    level[RADEON_SURF_MAX_LEVELS];
    struct legacy_surf_level    stencil_level[RADEON_SURF_MAX_LEVELS];
    uint8_t                     tiling_index[RADEON_SURF_MAX_LEVELS];
    uint8_t                     stencil_tiling_index[RADEON_SURF_MAX_LEVELS];
    struct legacy_surf_fmask    fmask;
};

/* Same as addrlib - AddrResourceType. */
enum gfx9_resource_type {
    RADEON_RESOURCE_1D = 0,
    RADEON_RESOURCE_2D,
    RADEON_RESOURCE_3D,
};

struct gfx9_surf_flags {
    uint16_t                    swizzle_mode; /* tile mode */
    uint16_t                    epitch; /* (pitch - 1) or (height - 1) */
};

struct gfx9_surf_meta_flags {
    unsigned                    rb_aligned:1;   /* optimal for RBs */
    unsigned                    pipe_aligned:1; /* optimal for TC */
};

struct gfx9_surf_layout {
    struct gfx9_surf_flags      surf;    /* color or depth surface */
    struct gfx9_surf_flags      fmask;   /* not added to surf_size */
    struct gfx9_surf_flags      stencil; /* added to surf_size, use stencil_offset */

    struct gfx9_surf_meta_flags dcc;   /* metadata of color */
    struct gfx9_surf_meta_flags htile; /* metadata of depth and stencil */
    struct gfx9_surf_meta_flags cmask; /* metadata of fmask */

    enum gfx9_resource_type     resource_type; /* 1D, 2D or 3D */
    uint16_t                    surf_pitch; /* in blocks */
    uint16_t                    surf_height;

    uint64_t                    surf_offset; /* 0 unless imported with an offset */
    /* The size of the 2D plane containing all mipmap levels. */
    uint64_t                    surf_slice_size;
    /* Mipmap level offset within the slice in bytes. Only valid for LINEAR. */
    uint32_t                    offset[RADEON_SURF_MAX_LEVELS];

    uint16_t                    dcc_pitch_max;  /* (mip chain pitch - 1) */

    uint64_t                    stencil_offset; /* separate stencil */
    uint64_t                    cmask_size;

    uint32_t                    cmask_alignment;
};

struct radeon_surf {
    /* Format properties. */
    unsigned                    blk_w:4;
    unsigned                    blk_h:4;
    unsigned                    bpe:5;
    /* Number of mipmap levels where DCC is enabled starting from level 0.
     * Non-zero levels may be disabled due to alignment constraints, but not
     * the first level.
     */
    unsigned                    num_dcc_levels:4;
    unsigned                    is_linear:1;
    unsigned                    has_stencil:1;
    /* This might be true even if micro_tile_mode isn't displayable or rotated. */
    unsigned                    is_displayable:1;
    /* Displayable, thin, depth, rotated. AKA D,S,Z,R swizzle modes. */
    unsigned                    micro_tile_mode:3;
    uint32_t                    flags;

    /* These are return values. Some of them can be set by the caller, but
     * they will be treated as hints (e.g. bankw, bankh) and might be
     * changed by the calculator.
     */

    /* Tile swizzle can be OR'd with low bits of the BASE_256B address.
     * The value is the same for all mipmap levels. Supported tile modes:
     * - GFX6: Only macro tiling.
     * - GFX9: Only *_X and *_T swizzle modes. Level 0 must not be in the mip
     *   tail.
     *
     * Only these surfaces are allowed to set it:
     * - color (if it doesn't have to be displayable)
     * - DCC (same tile swizzle as color)
     * - FMASK
     * - CMASK if it's TC-compatible or if the gen is GFX9
     * - depth/stencil if HTILE is not TC-compatible and if the gen is not GFX9
     */
    uint8_t                     tile_swizzle;
    uint8_t                     fmask_tile_swizzle;

    uint64_t                    surf_size;
    uint64_t                    fmask_size;
    /* DCC and HTILE are very small. */
    uint32_t                    dcc_size;
    uint32_t                    htile_size;

    uint32_t                    htile_slice_size;

    uint32_t                    surf_alignment;
    uint32_t                    fmask_alignment;
    uint32_t                    dcc_alignment;
    uint32_t                    htile_alignment;

    union {
        /* R600-VI return values.
         *
         * Some of them can be set by the caller if certain parameters are
         * desirable. The allocator will try to obey them.
         */
        struct legacy_surf_layout legacy;

        /* GFX9+ return values. */
        struct gfx9_surf_layout gfx9;
    } u;
};

struct ac_surf_info {
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	uint8_t samples; /* For Z/S: samples; For color: FMASK coverage samples */
	uint8_t color_samples; /* For color: color samples */
	uint8_t levels;
	uint8_t num_channels; /* heuristic for displayability */
	uint16_t array_size;
	uint32_t *surf_index; /* Set a monotonic counter for tile swizzling. */
	uint32_t *fmask_surf_index;
};

struct ac_surf_config {
	struct ac_surf_info info;
	unsigned is_3d : 1;
	unsigned is_cube : 1;
};

ADDR_HANDLE amdgpu_addr_create(const struct radeon_info *info,
			       const struct amdgpu_gpu_info *amdinfo,
			       uint64_t *max_alignment);

int ac_compute_surface(ADDR_HANDLE addrlib, const struct radeon_info *info,
		       const struct ac_surf_config * config,
		       enum radeon_surf_mode mode,
		       struct radeon_surf *surf);

#ifdef __cplusplus
}
#endif

#endif /* AC_SURFACE_H */
