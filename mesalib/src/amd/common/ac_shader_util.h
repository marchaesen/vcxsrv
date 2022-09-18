/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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

#ifndef AC_SHADER_UTIL_H
#define AC_SHADER_UTIL_H

#include "ac_binary.h"
#include "amd_family.h"
#include "compiler/nir/nir.h"
#include "compiler/shader_enums.h"
#include "util/format/u_format.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ac_image_dim
{
   ac_image_1d,
   ac_image_2d,
   ac_image_3d,
   ac_image_cube, // includes cube arrays
   ac_image_1darray,
   ac_image_2darray,
   ac_image_2dmsaa,
   ac_image_2darraymsaa,
};

struct ac_data_format_info {
   uint8_t element_size;
   uint8_t num_channels;
   uint8_t chan_byte_size;
   uint8_t chan_format;
};

enum ac_vs_input_alpha_adjust {
   AC_ALPHA_ADJUST_NONE = 0,
   AC_ALPHA_ADJUST_SNORM = 1,
   AC_ALPHA_ADJUST_SSCALED = 2,
   AC_ALPHA_ADJUST_SINT = 3,
};

struct ac_vtx_format_info {
   uint16_t dst_sel;
   uint8_t element_size;
   uint8_t num_channels;
   uint8_t chan_byte_size; /* 0 for packed formats */

   /* These last three are dependent on the family. */

   uint8_t has_hw_format;
   /* Index is number of channels minus one. Use any index for packed formats.
    * GFX6-8 is dfmt[0:3],nfmt[4:7].
    */
   uint8_t hw_format[4];
   enum ac_vs_input_alpha_adjust alpha_adjust : 8;
};

struct ac_spi_color_formats {
   unsigned normal : 8;
   unsigned alpha : 8;
   unsigned blend : 8;
   unsigned blend_alpha : 8;
};

/* For ac_build_fetch_format.
 *
 * Note: FLOAT must be 0 (used for convenience of encoding in radeonsi).
 */
enum ac_fetch_format
{
   AC_FETCH_FORMAT_FLOAT = 0,
   AC_FETCH_FORMAT_FIXED,
   AC_FETCH_FORMAT_UNORM,
   AC_FETCH_FORMAT_SNORM,
   AC_FETCH_FORMAT_USCALED,
   AC_FETCH_FORMAT_SSCALED,
   AC_FETCH_FORMAT_UINT,
   AC_FETCH_FORMAT_SINT,
   AC_FETCH_FORMAT_NONE,
};

enum ac_descriptor_type
{
   AC_DESC_IMAGE,
   AC_DESC_FMASK,
   AC_DESC_SAMPLER,
   AC_DESC_BUFFER,
   AC_DESC_PLANE_0,
   AC_DESC_PLANE_1,
   AC_DESC_PLANE_2,
};

unsigned ac_get_spi_shader_z_format(bool writes_z, bool writes_stencil, bool writes_samplemask,
                                    bool writes_mrt0_alpha);

unsigned ac_get_cb_shader_mask(unsigned spi_shader_col_format);

uint32_t ac_vgt_gs_mode(unsigned gs_max_vert_out, enum amd_gfx_level gfx_level);

unsigned ac_get_tbuffer_format(enum amd_gfx_level gfx_level, unsigned dfmt, unsigned nfmt);

const struct ac_data_format_info *ac_get_data_format_info(unsigned dfmt);

const struct ac_vtx_format_info *ac_get_vtx_format_info_table(enum amd_gfx_level level,
                                                              enum radeon_family family);

const struct ac_vtx_format_info *ac_get_vtx_format_info(enum amd_gfx_level level,
                                                        enum radeon_family family,
                                                        enum pipe_format fmt);

enum ac_image_dim ac_get_sampler_dim(enum amd_gfx_level gfx_level, enum glsl_sampler_dim dim,
                                     bool is_array);

enum ac_image_dim ac_get_image_dim(enum amd_gfx_level gfx_level, enum glsl_sampler_dim sdim,
                                   bool is_array);

unsigned ac_get_fs_input_vgpr_cnt(const struct ac_shader_config *config,
                                  signed char *face_vgpr_index, signed char *ancillary_vgpr_index,
                                  signed char *sample_coverage_vgpr_index_ptr);

void ac_choose_spi_color_formats(unsigned format, unsigned swap, unsigned ntype,
                                 bool is_depth, bool use_rbplus,
                                 struct ac_spi_color_formats *formats);

void ac_compute_late_alloc(const struct radeon_info *info, bool ngg, bool ngg_culling,
                           bool uses_scratch, unsigned *late_alloc_wave64, unsigned *cu_mask);

unsigned ac_compute_cs_workgroup_size(const uint16_t sizes[3], bool variable, unsigned max);

unsigned ac_compute_lshs_workgroup_size(enum amd_gfx_level gfx_level, gl_shader_stage stage,
                                        unsigned tess_num_patches,
                                        unsigned tess_patch_in_vtx,
                                        unsigned tess_patch_out_vtx);

unsigned ac_compute_esgs_workgroup_size(enum amd_gfx_level gfx_level, unsigned wave_size,
                                        unsigned es_verts, unsigned gs_inst_prims);

unsigned ac_compute_ngg_workgroup_size(unsigned es_verts, unsigned gs_inst_prims,
                                       unsigned max_vtx_out, unsigned prim_amp_factor);

void ac_set_reg_cu_en(void *cs, unsigned reg_offset, uint32_t value, uint32_t clear_mask,
                      unsigned value_shift, const struct radeon_info *info,
                      void set_sh_reg(void*, unsigned, uint32_t));

void ac_get_scratch_tmpring_size(const struct radeon_info *info, bool compute,
                                 unsigned bytes_per_wave, unsigned *max_seen_bytes_per_wave,
                                 uint32_t *tmpring_size);

#ifdef __cplusplus
}
#endif

#endif
