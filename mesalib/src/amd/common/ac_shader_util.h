/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
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

#define AC_SENDMSG_GS           2
#define AC_SENDMSG_GS_DONE      3
#define AC_SENDMSG_GS_ALLOC_REQ 9

#define AC_SENDMSG_GS_OP_NOP      (0 << 4)
#define AC_SENDMSG_GS_OP_CUT      (1 << 4)
#define AC_SENDMSG_GS_OP_EMIT     (2 << 4)
#define AC_SENDMSG_GS_OP_EMIT_CUT (3 << 4)

/* An extension of gl_access_qualifier describing other aspects of memory operations
 * for code generation.
 */
enum {
   /* Only one of LOAD/STORE/ATOMIC can be set. */
   ACCESS_TYPE_LOAD            = BITFIELD_BIT(27),
   ACCESS_TYPE_STORE           = BITFIELD_BIT(28),
   ACCESS_TYPE_ATOMIC          = BITFIELD_BIT(29),

   /* This access is expected to use an SMEM instruction if source operands are non-divergent.
    * Only loads can set this.
    */
   ACCESS_TYPE_SMEM            = BITFIELD_BIT(30),

   /* Whether a store offset or size alignment is less than 4. */
   ACCESS_MAY_STORE_SUBDWORD   = BITFIELD_BIT(31),
};

/* GFX6-11. The meaning of these enums is different between chips. They match LLVM definitions,
 * but they can also be used by ACO. Use ac_get_hw_cache_flags to get these.
 */
enum ac_cache_flags
{
   ac_glc = BITFIELD_BIT(0),
   ac_slc = BITFIELD_BIT(1),
   ac_dlc = BITFIELD_BIT(2),
   ac_swizzled = BITFIELD_BIT(3),
};

/* Cache-agnostic scope flags. */
enum gfx12_scope
{
   /* Memory access is coherent within a workgroup in CU mode.
    * There is no coherency between VMEM and SMEM.
    */
   gfx12_scope_cu,

   /* Memory access is coherent within an SE.
    * If there is no SE cache, this resolves to the device scope in the gfx domain.
    */
   gfx12_scope_se,

   /* Memory access is globally coherent within the device for all gfx blocks except CP and GE
    * depending on the chip (see below). This is referred to as the device scope. It's not coherent
    * with non-gfx blocks like DCN and VCN.
    *
    * If there a single global GL2 cache:
    *    - The device scope in the gfx domain resolves to GL2 scope in hw.
    *    - Memory access is cached in GL2.
    *    - radeon_info::cp_sdma_ge_use_system_memory_scope says whether CP, SDMA, and GE are
    *      not coherent. If true, some features need special handling. The list of the features
    *      and the suggested programming is:
    *      * tess factor ring for GE: use ACCESS_CP_GE_COHERENT_AMD (it selects the correct scope
    *        automatically)
    *      * query results read by shaders and SET_PREDICATION: use AMDGPU_VM_MTYPE_UC,
    *        but use VRAM for queries not read by the CPU for better performance
    *      * vertex indices for GE: flush GL2 after buffer stores, but don't invalidate
    *      * draw indirect for CP: flush GL2 after buffer stores, but don't invalidate
    *      * shader uploads via SDMA: invalidate GL2 at the beginning of IBs
    *      * PRIME buffer read by SDMA: the kernel flushes GL2 at the end of IBs
    *      * CP DMA clears/copies: use compute shaders or range-flush/invalidate GL2 around it
    *      * CP DMA prefetch: no change
    *      * COPY_DATA - FILLED_SIZE state for streamout, range-flush/invalidate GL2
    *      * WRITE_DATA - bindless descriptors: range-invalidate GL2
    *
    * If there is a separate GL2 cache per SE:
    *    - The device scope resolves to memory scope in hw.
    *    - Memory access is cached in MALL if MALL (infinity cache) is present.
    *    - radeon_info::cp_sdma_ge_use_system_memory_scope is always false in this case.
    */
   gfx12_scope_device,

   /* Memory scope. It's cached if MALL is present. This is called "system scope" in the ISA
    * documentation.
    */
   gfx12_scope_memory,
};

enum gfx12_load_temporal_hint
{
   /* VMEM and SMEM */
   gfx12_load_regular_temporal,
   gfx12_load_non_temporal,
   gfx12_load_high_temporal,
   /* VMEM$ treats SCOPE=3 and TH=3 as MALL bypass on GFX12. Don't use this combination in shaders. */
   gfx12_load_last_use_discard,
   /* VMEM only, far means the last level cache, near means other caches. */
   gfx12_load_near_non_temporal_far_regular_temporal,
   gfx12_load_near_regular_temporal_far_non_temporal,
   gfx12_load_near_non_temporal_far_high_temporal,
   gfx12_load_reserved,
};

enum gfx12_store_temporal_hint
{
   gfx12_store_regular_temporal,
   gfx12_store_non_temporal,
   gfx12_store_high_temporal,
   gfx12_store_high_temporal_stay_dirty,
   gfx12_store_near_non_temporal_far_regular_temporal,
   gfx12_store_near_regular_temporal_far_non_temporal,
   gfx12_store_near_non_temporal_far_high_temporal,
   gfx12_store_near_non_temporal_far_writeback,
};

enum gfx12_atomic_temporal_hint
{
   gfx12_atomic_return = BITFIELD_BIT(0),
   gfx12_atomic_non_temporal = BITFIELD_BIT(1),
   gfx12_atomic_accum_deferred_scope = BITFIELD_BIT(2), /* requires no return */
};

enum gfx12_speculative_data_read
{
   gfx12_spec_read_auto,
   gfx12_spec_read_force_on,
   gfx12_spec_read_force_off,
};

union ac_hw_cache_flags
{
   struct {
      /* This matches LLVM, but it can also be used by ACO for translation of ac_memop_flags. */
      uint8_t temporal_hint:3;   /* gfx12_{load,store,atomic}_temporal_hint */
      uint8_t scope:2;           /* gfx12_scope */
      uint8_t _reserved:1;
      uint8_t swizzled:1;        /* for swizzled buffer access (attribute ring) */
      uint8_t _pad:1;
   } gfx12;

   uint8_t value; /* ac_cache_flags (GFX6-11) or the gfx12 structure */
};

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

void ac_set_nir_options(struct radeon_info *info, bool use_llvm,
                        nir_shader_compiler_options *options);

bool ac_nir_mem_vectorize_callback(unsigned align_mul, unsigned align_offset, unsigned bit_size,
                                   unsigned num_components, unsigned hole_size,
                                   nir_intrinsic_instr *low, nir_intrinsic_instr *high, void *data);

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

unsigned ac_get_safe_fetch_size(const enum amd_gfx_level gfx_level, const struct ac_vtx_format_info* vtx_info,
                                const unsigned offset, const unsigned max_channels, const unsigned alignment,
                                const unsigned num_channels);

enum ac_image_dim ac_get_sampler_dim(enum amd_gfx_level gfx_level, enum glsl_sampler_dim dim,
                                     bool is_array);

enum ac_image_dim ac_get_image_dim(enum amd_gfx_level gfx_level, enum glsl_sampler_dim sdim,
                                   bool is_array);

unsigned ac_get_fs_input_vgpr_cnt(const struct ac_shader_config *config,
                                  uint8_t *num_fragcoord_components);

uint16_t ac_get_ps_iter_mask(unsigned ps_iter_samples);

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

uint32_t ac_compute_num_tess_patches(const struct radeon_info *info, uint32_t num_tcs_input_cp,
                                     uint32_t num_tcs_output_cp, uint32_t vram_per_patch,
                                     uint32_t lds_per_patch, uint32_t wave_size,
                                     bool tess_uses_primid);

uint32_t ac_compute_tess_lds_size(const struct radeon_info *info,
                                  uint32_t lds_per_patch, uint32_t num_patches);

uint32_t ac_apply_cu_en(uint32_t value, uint32_t clear_mask, unsigned value_shift,
                        const struct radeon_info *info);

void ac_get_scratch_tmpring_size(const struct radeon_info *info,
                                 unsigned bytes_per_wave, unsigned *max_seen_bytes_per_wave,
                                 uint32_t *tmpring_size);

unsigned
ac_ngg_nogs_get_pervertex_lds_size(gl_shader_stage stage,
                                   unsigned shader_num_outputs,
                                   bool streamout_enabled,
                                   bool export_prim_id,
                                   bool has_user_edgeflags,
                                   bool can_cull,
                                   bool uses_instance_id,
                                   bool uses_primitive_id);

unsigned
ac_ngg_get_scratch_lds_size(gl_shader_stage stage,
                            unsigned workgroup_size,
                            unsigned wave_size,
                            bool streamout_enabled,
                            bool can_cull);

enum gl_access_qualifier ac_get_mem_access_flags(const nir_intrinsic_instr *instr);

union ac_hw_cache_flags ac_get_hw_cache_flags(enum amd_gfx_level gfx_level,
                                              enum gl_access_qualifier access);

unsigned ac_get_all_edge_flag_bits(enum amd_gfx_level gfx_level);

unsigned ac_shader_io_get_unique_index_patch(unsigned semantic);

#ifdef __cplusplus
}
#endif

#endif
