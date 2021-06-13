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

#include "amd_family.h"
#include "util/format/u_format.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct ac_addrlib;

struct amdgpu_gpu_info;
struct radeon_info;

#define RADEON_SURF_MAX_LEVELS 15

enum radeon_surf_mode
{
   RADEON_SURF_MODE_LINEAR_ALIGNED = 1,
   RADEON_SURF_MODE_1D = 2,
   RADEON_SURF_MODE_2D = 3,
};

/* This describes D/S/Z/R swizzle modes.
 * Defined in the GB_TILE_MODEn.MICRO_TILE_MODE_NEW order.
 */
enum radeon_micro_mode
{
   RADEON_MICRO_MODE_DISPLAY = 0,
   RADEON_MICRO_MODE_STANDARD = 1,
   RADEON_MICRO_MODE_DEPTH = 2,
   RADEON_MICRO_MODE_RENDER = 3, /* gfx9 and older: rotated */
};

/* the first 16 bits are reserved for libdrm_radeon, don't use them */
#define RADEON_SURF_SCANOUT      (1 << 16)
#define RADEON_SURF_ZBUFFER      (1 << 17)
#define RADEON_SURF_SBUFFER      (1 << 18)
#define RADEON_SURF_Z_OR_SBUFFER (RADEON_SURF_ZBUFFER | RADEON_SURF_SBUFFER)
/* bits 19 and 20 are reserved for libdrm_radeon, don't use them */
#define RADEON_SURF_FMASK                 (1 << 21)
#define RADEON_SURF_DISABLE_DCC           (1ull << 22)
#define RADEON_SURF_TC_COMPATIBLE_HTILE   (1ull << 23)
#define RADEON_SURF_IMPORTED              (1ull << 24)
#define RADEON_SURF_CONTIGUOUS_DCC_LAYERS (1ull << 25)
#define RADEON_SURF_SHAREABLE             (1ull << 26)
#define RADEON_SURF_NO_RENDER_TARGET      (1ull << 27)
/* Force a swizzle mode (gfx9+) or tile mode (gfx6-8).
 * If this is not set, optimize for space. */
#define RADEON_SURF_FORCE_SWIZZLE_MODE    (1ull << 28)
#define RADEON_SURF_NO_FMASK              (1ull << 29)
#define RADEON_SURF_NO_HTILE              (1ull << 30)
#define RADEON_SURF_FORCE_MICRO_TILE_MODE (1ull << 31)
#define RADEON_SURF_PRT                   (1ull << 32)

struct legacy_surf_level {
   uint64_t offset;
   uint32_t slice_size_dw; /* in dwords; max = 4GB / 4. */
   uint32_t dcc_offset;    /* relative offset within DCC mip tree */
   uint32_t dcc_fast_clear_size;
   uint32_t dcc_slice_fast_clear_size;
   unsigned nblk_x : 15;
   unsigned nblk_y : 15;
   enum radeon_surf_mode mode : 2;
};

struct legacy_surf_fmask {
   unsigned slice_tile_max; /* max 4M */
   uint8_t tiling_index;    /* max 31 */
   uint8_t bankh;           /* max 8 */
   uint16_t pitch_in_pixels;
};

struct legacy_surf_layout {
   unsigned bankw : 4;               /* max 8 */
   unsigned bankh : 4;               /* max 8 */
   unsigned mtilea : 4;              /* max 8 */
   unsigned tile_split : 13;         /* max 4K */
   unsigned stencil_tile_split : 13; /* max 4K */
   unsigned pipe_config : 5;         /* max 17 */
   unsigned num_banks : 5;           /* max 16 */
   unsigned macro_tile_index : 4;    /* max 15 */

   /* Whether the depth miptree or stencil miptree as used by the DB are
    * adjusted from their TC compatible form to ensure depth/stencil
    * compatibility. If either is true, the corresponding plane cannot be
    * sampled from.
    */
   unsigned depth_adjusted : 1;
   unsigned stencil_adjusted : 1;

   struct legacy_surf_level level[RADEON_SURF_MAX_LEVELS];
   struct legacy_surf_level stencil_level[RADEON_SURF_MAX_LEVELS];
   uint8_t tiling_index[RADEON_SURF_MAX_LEVELS];
   uint8_t stencil_tiling_index[RADEON_SURF_MAX_LEVELS];
   struct legacy_surf_fmask fmask;
   unsigned cmask_slice_tile_max;
};

/* Same as addrlib - AddrResourceType. */
enum gfx9_resource_type
{
   RADEON_RESOURCE_1D = 0,
   RADEON_RESOURCE_2D,
   RADEON_RESOURCE_3D,
};

struct gfx9_surf_flags {
   uint16_t swizzle_mode; /* tile mode */
   uint16_t epitch;       /* (pitch - 1) or (height - 1) */
};

struct gfx9_surf_meta_flags {
   unsigned rb_aligned : 1;   /* optimal for RBs */
   unsigned pipe_aligned : 1; /* optimal for TC */
   unsigned independent_64B_blocks : 1;
   unsigned independent_128B_blocks : 1;
   unsigned max_compressed_block_size : 2;
};

struct gfx9_surf_level {
   unsigned offset;
   unsigned size;
};

struct gfx9_surf_layout {
   struct gfx9_surf_flags surf;    /* color or depth surface */
   struct gfx9_surf_flags fmask;   /* not added to surf_size */
   struct gfx9_surf_flags stencil; /* added to surf_size, use stencil_offset */

   struct gfx9_surf_meta_flags dcc; /* metadata of color */

   enum gfx9_resource_type resource_type; /* 1D, 2D or 3D */
   uint16_t surf_pitch;                   /* in blocks */
   uint16_t surf_height;

   uint64_t surf_offset; /* 0 unless imported with an offset */
   /* The size of the 2D plane containing all mipmap levels. */
   uint64_t surf_slice_size;
   /* Mipmap level offset within the slice in bytes. Only valid for LINEAR. */
   uint32_t offset[RADEON_SURF_MAX_LEVELS];
   /* Mipmap level pitch in elements. Only valid for LINEAR. */
   uint16_t pitch[RADEON_SURF_MAX_LEVELS];

   uint16_t base_mip_width;
   uint16_t base_mip_height;

   uint64_t stencil_offset; /* separate stencil */

   uint8_t dcc_block_width;
   uint8_t dcc_block_height;
   uint8_t dcc_block_depth;

   /* Displayable DCC. This is always rb_aligned=0 and pipe_aligned=0.
    * The 3D engine doesn't support that layout except for chips with 1 RB.
    * All other chips must set rb_aligned=1.
    * A compute shader needs to convert from aligned DCC to unaligned.
    */
   uint32_t display_dcc_size;
   uint32_t display_dcc_alignment;
   uint16_t display_dcc_pitch_max; /* (mip chain pitch - 1) */
   uint16_t dcc_pitch_max;
   bool dcc_retile_use_uint16;     /* if all values fit into uint16_t */
   uint32_t dcc_retile_num_elements;
   void *dcc_retile_map;

   /* Offset within slice in bytes, only valid for prt images. */
   uint32_t prt_level_offset[RADEON_SURF_MAX_LEVELS];
   /* Pitch of level in blocks, only valid for prt images. */
   uint16_t prt_level_pitch[RADEON_SURF_MAX_LEVELS];

   /* DCC level info */
   struct gfx9_surf_level dcc_levels[RADEON_SURF_MAX_LEVELS];

   /* HTILE level info */
   struct gfx9_surf_level htile_levels[RADEON_SURF_MAX_LEVELS];
};

struct radeon_surf {
   /* Format properties. */
   unsigned blk_w : 4;
   unsigned blk_h : 4;
   unsigned bpe : 5;
   /* Number of mipmap levels where DCC is enabled starting from level 0.
    * Non-zero levels may be disabled due to alignment constraints, but not
    * the first level.
    */
   unsigned num_dcc_levels : 4;
   unsigned is_linear : 1;
   unsigned has_stencil : 1;
   /* This might be true even if micro_tile_mode isn't displayable or rotated. */
   unsigned is_displayable : 1;
   /* Displayable, thin, depth, rotated. AKA D,S,Z,R swizzle modes. */
   unsigned micro_tile_mode : 3;
   uint64_t flags;

    /*
     * DRM format modifier. Set to DRM_FORMAT_MOD_INVALID to have addrlib
     * select tiling parameters instead.
     */
    uint64_t modifier;

   /* These are return values. Some of them can be set by the caller, but
    * they will be treated as hints (e.g. bankw, bankh) and might be
    * changed by the calculator.
    */

   /* Not supported yet for depth + stencil. */
   uint8_t first_mip_tail_level;
   uint16_t prt_tile_width;
   uint16_t prt_tile_height;

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
   uint8_t tile_swizzle;
   uint8_t fmask_tile_swizzle;

   uint64_t surf_size;
   uint64_t fmask_size;
   uint32_t surf_alignment;
   uint32_t fmask_alignment;
   uint64_t fmask_slice_size;

   /* DCC and HTILE are very small. */
   uint32_t dcc_size;
   uint32_t dcc_slice_size;
   uint32_t dcc_alignment;

   uint32_t htile_size;
   uint32_t htile_slice_size;
   uint32_t htile_alignment;
   uint32_t num_htile_levels : 4;

   uint32_t cmask_size;
   uint32_t cmask_slice_size;
   uint32_t cmask_alignment;

   /* All buffers combined. */
   uint64_t htile_offset;
   uint64_t fmask_offset;
   uint64_t cmask_offset;
   uint64_t dcc_offset;
   uint64_t display_dcc_offset;
   uint64_t total_size;
   uint32_t alignment;

   union {
      /* Return values for GFX8 and older.
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
   uint8_t samples;         /* For Z/S: samples; For color: FMASK coverage samples */
   uint8_t storage_samples; /* For color: allocated samples */
   uint8_t levels;
   uint8_t num_channels; /* heuristic for displayability */
   uint16_t array_size;
   uint32_t *surf_index; /* Set a monotonic counter for tile swizzling. */
   uint32_t *fmask_surf_index;
};

struct ac_surf_config {
   struct ac_surf_info info;
   unsigned is_1d : 1;
   unsigned is_3d : 1;
   unsigned is_cube : 1;
};

struct ac_addrlib *ac_addrlib_create(const struct radeon_info *info, uint64_t *max_alignment);
void ac_addrlib_destroy(struct ac_addrlib *addrlib);
void *ac_addrlib_get_handle(struct ac_addrlib *addrlib);

int ac_compute_surface(struct ac_addrlib *addrlib, const struct radeon_info *info,
                       const struct ac_surf_config *config, enum radeon_surf_mode mode,
                       struct radeon_surf *surf);
void ac_surface_zero_dcc_fields(struct radeon_surf *surf);

void ac_surface_set_bo_metadata(const struct radeon_info *info, struct radeon_surf *surf,
                                uint64_t tiling_flags, enum radeon_surf_mode *mode);
void ac_surface_get_bo_metadata(const struct radeon_info *info, struct radeon_surf *surf,
                                uint64_t *tiling_flags);

bool ac_surface_set_umd_metadata(const struct radeon_info *info, struct radeon_surf *surf,
                                 unsigned num_storage_samples, unsigned num_mipmap_levels,
                                 unsigned size_metadata, const uint32_t metadata[64]);
void ac_surface_get_umd_metadata(const struct radeon_info *info, struct radeon_surf *surf,
                                 unsigned num_mipmap_levels, uint32_t desc[8],
                                 unsigned *size_metadata, uint32_t metadata[64]);

bool ac_surface_override_offset_stride(const struct radeon_info *info, struct radeon_surf *surf,
                                       unsigned num_mipmap_levels, uint64_t offset, unsigned pitch);

struct ac_modifier_options {
	bool dcc; /* Whether to allow DCC. */
	bool dcc_retile; /* Whether to allow use of a DCC retile map. */
};

bool ac_is_modifier_supported(const struct radeon_info *info,
                              const struct ac_modifier_options *options,
                              enum pipe_format format,
                              uint64_t modifier);
bool ac_get_supported_modifiers(const struct radeon_info *info,
                                const struct ac_modifier_options *options,
                                enum pipe_format format,
                                unsigned *mod_count,
                                uint64_t *mods);
bool ac_modifier_has_dcc(uint64_t modifier);
bool ac_modifier_has_dcc_retile(uint64_t modifier);

unsigned ac_surface_get_nplanes(const struct radeon_surf *surf);
uint64_t ac_surface_get_plane_offset(enum chip_class chip_class,
                                     const struct radeon_surf *surf,
                                     unsigned plane, unsigned layer);
uint64_t ac_surface_get_plane_stride(enum chip_class chip_class,
                                     const struct radeon_surf *surf,
                                     unsigned plane);
/* Of the whole miplevel, not an individual layer */
uint64_t ac_surface_get_plane_size(const struct radeon_surf *surf,
                                   unsigned plane);
uint32_t ac_surface_get_retile_map_size(const struct radeon_surf *surf);

void ac_surface_print_info(FILE *out, const struct radeon_info *info,
                           const struct radeon_surf *surf);

#ifdef __cplusplus
}
#endif

#endif /* AC_SURFACE_H */
