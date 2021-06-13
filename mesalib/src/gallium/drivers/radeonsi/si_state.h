/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SI_STATE_H
#define SI_STATE_H

#include "pipebuffer/pb_slab.h"
#include "si_pm4.h"
#include "util/u_blitter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SI_NUM_GRAPHICS_SHADERS (PIPE_SHADER_TESS_EVAL + 1)
#define SI_NUM_SHADERS          (PIPE_SHADER_COMPUTE + 1)

#define SI_NUM_VERTEX_BUFFERS SI_MAX_ATTRIBS
#define SI_NUM_SAMPLERS       32 /* OpenGL textures units per shader */
#define SI_NUM_CONST_BUFFERS  16
#define SI_NUM_IMAGES         16
#define SI_NUM_IMAGE_SLOTS    (SI_NUM_IMAGES * 2) /* the second half are FMASK slots */
#define SI_NUM_SHADER_BUFFERS 32

struct si_screen;
struct si_shader;
struct si_shader_ctx_state;
struct si_shader_selector;
struct si_texture;
struct si_qbo_state;

struct si_state_blend {
   struct si_pm4_state pm4;
   uint32_t cb_target_mask;
   /* Set 0xf or 0x0 (4 bits) per render target if the following is
    * true. ANDed with spi_shader_col_format.
    */
   unsigned cb_target_enabled_4bit;
   unsigned blend_enable_4bit;
   unsigned need_src_alpha_4bit;
   unsigned commutative_4bit;
   unsigned dcc_msaa_corruption_4bit;
   bool alpha_to_coverage : 1;
   bool alpha_to_one : 1;
   bool dual_src_blend : 1;
   bool logicop_enable : 1;
};

struct si_state_rasterizer {
   struct si_pm4_state pm4;
   /* poly offset states for 16-bit, 24-bit, and 32-bit zbuffers */
   struct si_pm4_state *pm4_poly_offset;
   unsigned pa_sc_line_stipple;
   unsigned pa_cl_clip_cntl;
   float line_width;
   float max_point_size;
   unsigned ngg_cull_flags : 8;
   unsigned ngg_cull_flags_y_inverted : 8;
   unsigned sprite_coord_enable : 8;
   unsigned clip_plane_enable : 8;
   unsigned half_pixel_center : 1;
   unsigned flatshade : 1;
   unsigned flatshade_first : 1;
   unsigned two_side : 1;
   unsigned multisample_enable : 1;
   unsigned force_persample_interp : 1;
   unsigned line_stipple_enable : 1;
   unsigned poly_stipple_enable : 1;
   unsigned line_smooth : 1;
   unsigned poly_smooth : 1;
   unsigned uses_poly_offset : 1;
   unsigned clamp_fragment_color : 1;
   unsigned clamp_vertex_color : 1;
   unsigned rasterizer_discard : 1;
   unsigned scissor_enable : 1;
   unsigned clip_halfz : 1;
   unsigned cull_front : 1;
   unsigned cull_back : 1;
   unsigned depth_clamp_any : 1;
   unsigned provoking_vertex_first : 1;
   unsigned polygon_mode_enabled : 1;
   unsigned polygon_mode_is_lines : 1;
   unsigned polygon_mode_is_points : 1;
};

struct si_dsa_stencil_ref_part {
   uint8_t valuemask[2];
   uint8_t writemask[2];
};

struct si_dsa_order_invariance {
   /** Whether the final result in Z/S buffers is guaranteed to be
    * invariant under changes to the order in which fragments arrive. */
   bool zs : 1;

   /** Whether the set of fragments that pass the combined Z/S test is
    * guaranteed to be invariant under changes to the order in which
    * fragments arrive. */
   bool pass_set : 1;

   /** Whether the last fragment that passes the combined Z/S test at each
    * sample is guaranteed to be invariant under changes to the order in
    * which fragments arrive. */
   bool pass_last : 1;
};

struct si_state_dsa {
   struct si_pm4_state pm4;
   struct si_dsa_stencil_ref_part stencil_ref;

   /* 0 = without stencil buffer, 1 = when both Z and S buffers are present */
   struct si_dsa_order_invariance order_invariance[2];

   ubyte alpha_func : 3;
   bool depth_enabled : 1;
   bool depth_write_enabled : 1;
   bool stencil_enabled : 1;
   bool stencil_write_enabled : 1;
   bool db_can_write : 1;
};

struct si_stencil_ref {
   struct pipe_stencil_ref state;
   struct si_dsa_stencil_ref_part dsa_part;
};

struct si_vertex_elements {
   struct si_resource *instance_divisor_factor_buffer;
   uint32_t rsrc_word3[SI_MAX_ATTRIBS];
   uint16_t src_offset[SI_MAX_ATTRIBS];
   uint8_t fix_fetch[SI_MAX_ATTRIBS];
   uint8_t format_size[SI_MAX_ATTRIBS];
   uint8_t vertex_buffer_index[SI_MAX_ATTRIBS];

   /* Bitmask of elements that always need a fixup to be applied. */
   uint16_t fix_fetch_always;

   /* Bitmask of elements whose fetch should always be opencoded. */
   uint16_t fix_fetch_opencode;

   /* Bitmask of elements which need to be opencoded if the vertex buffer
    * is unaligned. */
   uint16_t fix_fetch_unaligned;

   /* For elements in fix_fetch_unaligned: whether the effective
    * element load size as seen by the hardware is a dword (as opposed
    * to a short).
    */
   uint16_t hw_load_is_dword;

   /* Bitmask of vertex buffers requiring alignment check */
   uint16_t vb_alignment_check_mask;

   uint8_t count;
   bool uses_instance_divisors;

   uint16_t first_vb_use_mask;
   /* Vertex buffer descriptor list size aligned for optimal prefetch. */
   uint16_t vb_desc_list_alloc_size;
   uint16_t instance_divisor_is_one;     /* bitmask of inputs */
   uint16_t instance_divisor_is_fetched; /* bitmask of inputs */
};

union si_state {
   struct si_state_named {
      struct si_state_blend *blend;
      struct si_state_rasterizer *rasterizer;
      struct si_state_dsa *dsa;
      struct si_pm4_state *poly_offset;
      struct si_pm4_state *ls;
      struct si_pm4_state *hs;
      struct si_pm4_state *es;
      struct si_pm4_state *gs;
      struct si_pm4_state *vgt_shader_config;
      struct si_pm4_state *vs;
      struct si_pm4_state *ps;
   } named;
   struct si_pm4_state *array[sizeof(struct si_state_named) / sizeof(struct si_pm4_state *)];
};

#define SI_STATE_IDX(name) (offsetof(union si_state, named.name) / sizeof(struct si_pm4_state *))
#define SI_STATE_BIT(name) (1 << SI_STATE_IDX(name))
#define SI_NUM_STATES      (sizeof(union si_state) / sizeof(struct si_pm4_state *))

static inline unsigned si_states_that_always_roll_context(void)
{
   return (SI_STATE_BIT(blend) | SI_STATE_BIT(rasterizer) | SI_STATE_BIT(dsa) |
           SI_STATE_BIT(poly_offset) | SI_STATE_BIT(vgt_shader_config));
}

union si_state_atoms {
   struct si_atoms_s {
      /* The order matters. */
      struct si_atom render_cond;
      struct si_atom streamout_begin;
      struct si_atom streamout_enable; /* must be after streamout_begin */
      struct si_atom framebuffer;
      struct si_atom msaa_sample_locs;
      struct si_atom db_render_state;
      struct si_atom dpbb_state;
      struct si_atom msaa_config;
      struct si_atom sample_mask;
      struct si_atom cb_render_state;
      struct si_atom blend_color;
      struct si_atom clip_regs;
      struct si_atom clip_state;
      struct si_atom shader_pointers;
      struct si_atom guardband;
      struct si_atom scissors;
      struct si_atom viewports;
      struct si_atom stencil_ref;
      struct si_atom spi_map;
      struct si_atom scratch_state;
      struct si_atom window_rectangles;
      struct si_atom shader_query;
      struct si_atom ngg_cull_state;
   } s;
   struct si_atom array[sizeof(struct si_atoms_s) / sizeof(struct si_atom)];
};

#define SI_ATOM_BIT(name) (1 << (offsetof(union si_state_atoms, s.name) / sizeof(struct si_atom)))
#define SI_NUM_ATOMS      (sizeof(union si_state_atoms) / sizeof(struct si_atom))

static inline unsigned si_atoms_that_always_roll_context(void)
{
   return (SI_ATOM_BIT(streamout_begin) | SI_ATOM_BIT(streamout_enable) | SI_ATOM_BIT(framebuffer) |
           SI_ATOM_BIT(msaa_sample_locs) | SI_ATOM_BIT(sample_mask) | SI_ATOM_BIT(blend_color) |
           SI_ATOM_BIT(clip_state) | SI_ATOM_BIT(scissors) | SI_ATOM_BIT(viewports) |
           SI_ATOM_BIT(stencil_ref) | SI_ATOM_BIT(scratch_state) | SI_ATOM_BIT(window_rectangles));
}

struct si_shader_data {
   uint32_t sh_base[SI_NUM_SHADERS];
};

#define SI_TRACKED_PA_CL_VS_OUT_CNTL__VS_MASK                                                      \
   (S_02881C_USE_VTX_POINT_SIZE(1) | S_02881C_USE_VTX_EDGE_FLAG(1) |                               \
    S_02881C_USE_VTX_RENDER_TARGET_INDX(1) | S_02881C_USE_VTX_VIEWPORT_INDX(1) |                   \
    S_02881C_VS_OUT_MISC_VEC_ENA(1) | S_02881C_VS_OUT_MISC_SIDE_BUS_ENA(1) |                       \
    S_02881C_USE_VTX_VRS_RATE(1))

/* The list of registers whose emitted values are remembered by si_context. */
enum si_tracked_reg
{
   SI_TRACKED_DB_RENDER_CONTROL, /* 2 consecutive registers */
   SI_TRACKED_DB_COUNT_CONTROL,

   SI_TRACKED_DB_RENDER_OVERRIDE2,
   SI_TRACKED_DB_SHADER_CONTROL,

   SI_TRACKED_CB_TARGET_MASK,
   SI_TRACKED_CB_DCC_CONTROL,

   SI_TRACKED_SX_PS_DOWNCONVERT, /* 3 consecutive registers */
   SI_TRACKED_SX_BLEND_OPT_EPSILON,
   SI_TRACKED_SX_BLEND_OPT_CONTROL,

   SI_TRACKED_PA_SC_LINE_CNTL, /* 2 consecutive registers */
   SI_TRACKED_PA_SC_AA_CONFIG,

   SI_TRACKED_DB_EQAA,
   SI_TRACKED_PA_SC_MODE_CNTL_1,

   SI_TRACKED_PA_SU_PRIM_FILTER_CNTL,
   SI_TRACKED_PA_SU_SMALL_PRIM_FILTER_CNTL,

   SI_TRACKED_PA_CL_VS_OUT_CNTL__VS, /* set with SI_TRACKED_PA_CL_VS_OUT_CNTL__VS_MASK*/
   SI_TRACKED_PA_CL_VS_OUT_CNTL__CL, /* set with ~SI_TRACKED_PA_CL_VS_OUT_CNTL__VS_MASK */
   SI_TRACKED_PA_CL_CLIP_CNTL,

   SI_TRACKED_PA_SC_BINNER_CNTL_0,
   SI_TRACKED_DB_DFSM_CONTROL,
   SI_TRACKED_DB_VRS_OVERRIDE_CNTL,

   SI_TRACKED_PA_CL_GB_VERT_CLIP_ADJ, /* 4 consecutive registers */
   SI_TRACKED_PA_CL_GB_VERT_DISC_ADJ,
   SI_TRACKED_PA_CL_GB_HORZ_CLIP_ADJ,
   SI_TRACKED_PA_CL_GB_HORZ_DISC_ADJ,

   SI_TRACKED_PA_SU_HARDWARE_SCREEN_OFFSET,
   SI_TRACKED_PA_SU_VTX_CNTL,

   SI_TRACKED_PA_SC_CLIPRECT_RULE,

   SI_TRACKED_PA_SC_LINE_STIPPLE,

   SI_TRACKED_VGT_ESGS_RING_ITEMSIZE,

   SI_TRACKED_VGT_GSVS_RING_OFFSET_1, /* 3 consecutive registers */
   SI_TRACKED_VGT_GSVS_RING_OFFSET_2,
   SI_TRACKED_VGT_GSVS_RING_OFFSET_3,

   SI_TRACKED_VGT_GSVS_RING_ITEMSIZE,
   SI_TRACKED_VGT_GS_MAX_VERT_OUT,

   SI_TRACKED_VGT_GS_VERT_ITEMSIZE, /* 4 consecutive registers */
   SI_TRACKED_VGT_GS_VERT_ITEMSIZE_1,
   SI_TRACKED_VGT_GS_VERT_ITEMSIZE_2,
   SI_TRACKED_VGT_GS_VERT_ITEMSIZE_3,

   SI_TRACKED_VGT_GS_INSTANCE_CNT,
   SI_TRACKED_VGT_GS_ONCHIP_CNTL,
   SI_TRACKED_VGT_GS_MAX_PRIMS_PER_SUBGROUP,
   SI_TRACKED_VGT_GS_MODE,
   SI_TRACKED_VGT_PRIMITIVEID_EN,
   SI_TRACKED_VGT_REUSE_OFF,
   SI_TRACKED_SPI_VS_OUT_CONFIG,
   SI_TRACKED_PA_CL_VTE_CNTL,
   SI_TRACKED_PA_CL_NGG_CNTL,
   SI_TRACKED_GE_MAX_OUTPUT_PER_SUBGROUP,
   SI_TRACKED_GE_NGG_SUBGRP_CNTL,

   SI_TRACKED_SPI_SHADER_IDX_FORMAT, /* 2 consecutive registers */
   SI_TRACKED_SPI_SHADER_POS_FORMAT,

   SI_TRACKED_SPI_PS_INPUT_ENA, /* 2 consecutive registers */
   SI_TRACKED_SPI_PS_INPUT_ADDR,

   SI_TRACKED_SPI_BARYC_CNTL,
   SI_TRACKED_SPI_PS_IN_CONTROL,

   SI_TRACKED_SPI_SHADER_Z_FORMAT, /* 2 consecutive registers */
   SI_TRACKED_SPI_SHADER_COL_FORMAT,

   SI_TRACKED_CB_SHADER_MASK,
   SI_TRACKED_VGT_TF_PARAM,
   SI_TRACKED_VGT_VERTEX_REUSE_BLOCK_CNTL,

   SI_TRACKED_GE_PC_ALLOC,

   SI_NUM_TRACKED_REGS,
};

struct si_tracked_regs {
   uint64_t reg_saved;
   uint32_t reg_value[SI_NUM_TRACKED_REGS];
   uint32_t spi_ps_input_cntl[32];
};

/* Private read-write buffer slots. */
enum
{
   SI_ES_RING_ESGS,
   SI_GS_RING_ESGS,

   SI_RING_GSVS,

   SI_VS_STREAMOUT_BUF0,
   SI_VS_STREAMOUT_BUF1,
   SI_VS_STREAMOUT_BUF2,
   SI_VS_STREAMOUT_BUF3,

   SI_HS_CONST_DEFAULT_TESS_LEVELS,
   SI_VS_CONST_INSTANCE_DIVISORS,
   SI_VS_CONST_CLIP_PLANES,
   SI_PS_CONST_POLY_STIPPLE,
   SI_PS_CONST_SAMPLE_POSITIONS,

   /* Image descriptor of color buffer 0 for KHR_blend_equation_advanced. */
   SI_PS_IMAGE_COLORBUF0,
   SI_PS_IMAGE_COLORBUF0_HI,
   SI_PS_IMAGE_COLORBUF0_FMASK,
   SI_PS_IMAGE_COLORBUF0_FMASK_HI,

   GFX10_GS_QUERY_BUF,

   SI_NUM_INTERNAL_BINDINGS,
};

/* Indices into sctx->descriptors, laid out so that gfx and compute pipelines
 * are contiguous:
 *
 *  0 - rw buffers
 *  1 - vertex const and shader buffers
 *  2 - vertex samplers and images
 *  3 - fragment const and shader buffer
 *   ...
 *  11 - compute const and shader buffers
 *  12 - compute samplers and images
 */
enum
{
   SI_SHADER_DESCS_CONST_AND_SHADER_BUFFERS,
   SI_SHADER_DESCS_SAMPLERS_AND_IMAGES,
   SI_NUM_SHADER_DESCS,
};

#define SI_DESCS_INTERNAL      0
#define SI_DESCS_FIRST_SHADER  1
#define SI_DESCS_FIRST_COMPUTE (SI_DESCS_FIRST_SHADER + PIPE_SHADER_COMPUTE * SI_NUM_SHADER_DESCS)
#define SI_NUM_DESCS           (SI_DESCS_FIRST_SHADER + SI_NUM_SHADERS * SI_NUM_SHADER_DESCS)

#define SI_DESCS_SHADER_MASK(name)                                                                 \
   u_bit_consecutive(SI_DESCS_FIRST_SHADER + PIPE_SHADER_##name * SI_NUM_SHADER_DESCS,             \
                     SI_NUM_SHADER_DESCS)

static inline unsigned si_const_and_shader_buffer_descriptors_idx(unsigned shader)
{
   return SI_DESCS_FIRST_SHADER + shader * SI_NUM_SHADER_DESCS +
          SI_SHADER_DESCS_CONST_AND_SHADER_BUFFERS;
}

static inline unsigned si_sampler_and_image_descriptors_idx(unsigned shader)
{
   return SI_DESCS_FIRST_SHADER + shader * SI_NUM_SHADER_DESCS +
          SI_SHADER_DESCS_SAMPLERS_AND_IMAGES;
}

/* This represents descriptors in memory, such as buffer resources,
 * image resources, and sampler states.
 */
struct si_descriptors {
   /* The list of descriptors in malloc'd memory. */
   uint32_t *list;
   /* The list in mapped GPU memory. */
   uint32_t *gpu_list;

   /* The buffer where the descriptors have been uploaded. */
   struct si_resource *buffer;
   uint64_t gpu_address;

   /* The maximum number of descriptors. */
   uint32_t num_elements;

   /* Slots that are used by currently-bound shaders.
    * It determines which slots are uploaded.
    */
   uint32_t first_active_slot;
   uint32_t num_active_slots;

   /* The SH register offset relative to USER_DATA*_0 where the pointer
    * to the descriptor array will be stored. */
   short shader_userdata_offset;
   /* The size of one descriptor. */
   ubyte element_dw_size;
   /* If there is only one slot enabled, bind it directly instead of
    * uploading descriptors. -1 if disabled. */
   signed char slot_index_to_bind_directly;
};

struct si_buffer_resources {
   struct pipe_resource **buffers; /* this has num_buffers elements */
   unsigned *offsets;              /* this has num_buffers elements */

   enum radeon_bo_priority priority : 6;
   enum radeon_bo_priority priority_constbuf : 6;

   /* The i-th bit is set if that element is enabled (non-NULL resource). */
   uint64_t enabled_mask;
   uint64_t writable_mask;
};

#define si_pm4_state_changed(sctx, member)                                                         \
   ((sctx)->queued.named.member != (sctx)->emitted.named.member)

#define si_pm4_state_enabled_and_changed(sctx, member)                                             \
   ((sctx)->queued.named.member && si_pm4_state_changed(sctx, member))

#define si_pm4_bind_state(sctx, member, value)                                                     \
   do {                                                                                            \
      (sctx)->queued.named.member = (value);                                                       \
      if (value && value != (sctx)->emitted.named.member)                                          \
         (sctx)->dirty_states |= SI_STATE_BIT(member);                                             \
      else                                                                                         \
         (sctx)->dirty_states &= ~SI_STATE_BIT(member);                                            \
   } while (0)

/* si_descriptors.c */
void si_set_mutable_tex_desc_fields(struct si_screen *sscreen, struct si_texture *tex,
                                    const struct legacy_surf_level *base_level_info,
                                    unsigned base_level, unsigned first_level, unsigned block_width,
                                    bool is_stencil, uint16_t access, uint32_t *state);
void si_update_ps_colorbuf0_slot(struct si_context *sctx);
void si_get_pipe_constant_buffer(struct si_context *sctx, uint shader, uint slot,
                                 struct pipe_constant_buffer *cbuf);
void si_get_shader_buffers(struct si_context *sctx, enum pipe_shader_type shader, uint start_slot,
                           uint count, struct pipe_shader_buffer *sbuf);
void si_set_ring_buffer(struct si_context *sctx, uint slot, struct pipe_resource *buffer,
                        unsigned stride, unsigned num_records, bool add_tid, bool swizzle,
                        unsigned element_size, unsigned index_stride, uint64_t offset);
void si_init_all_descriptors(struct si_context *sctx);
bool si_upload_graphics_shader_descriptors(struct si_context *sctx);
bool si_upload_compute_shader_descriptors(struct si_context *sctx);
void si_release_all_descriptors(struct si_context *sctx);
void si_gfx_resources_add_all_to_bo_list(struct si_context *sctx);
void si_compute_resources_add_all_to_bo_list(struct si_context *sctx);
bool si_gfx_resources_check_encrypted(struct si_context *sctx);
bool si_compute_resources_check_encrypted(struct si_context *sctx);
void si_shader_pointers_mark_dirty(struct si_context *sctx);
void si_add_all_descriptors_to_bo_list(struct si_context *sctx);
void si_update_all_texture_descriptors(struct si_context *sctx);
void si_shader_change_notify(struct si_context *sctx);
void si_update_needs_color_decompress_masks(struct si_context *sctx);
void si_emit_graphics_shader_pointers(struct si_context *sctx);
void si_emit_compute_shader_pointers(struct si_context *sctx);
void si_set_internal_const_buffer(struct si_context *sctx, uint slot,
                                  const struct pipe_constant_buffer *input);
void si_set_internal_shader_buffer(struct si_context *sctx, uint slot,
                                   const struct pipe_shader_buffer *sbuffer);
void si_set_active_descriptors(struct si_context *sctx, unsigned desc_idx,
                               uint64_t new_active_mask);
void si_set_active_descriptors_for_shader(struct si_context *sctx, struct si_shader_selector *sel);
bool si_bindless_descriptor_can_reclaim_slab(void *priv, struct pb_slab_entry *entry);
struct pb_slab *si_bindless_descriptor_slab_alloc(void *priv, unsigned heap, unsigned entry_size,
                                                  unsigned group_index);
void si_bindless_descriptor_slab_free(void *priv, struct pb_slab *pslab);
void si_rebind_buffer(struct si_context *sctx, struct pipe_resource *buf);
/* si_state.c */
void si_init_state_compute_functions(struct si_context *sctx);
void si_init_state_functions(struct si_context *sctx);
void si_init_screen_state_functions(struct si_screen *sscreen);
void si_init_cs_preamble_state(struct si_context *sctx, bool uses_reg_shadowing);
void si_make_buffer_descriptor(struct si_screen *screen, struct si_resource *buf,
                               enum pipe_format format, unsigned offset, unsigned size,
                               uint32_t *state);
struct pipe_sampler_view *si_create_sampler_view_custom(struct pipe_context *ctx,
                                                        struct pipe_resource *texture,
                                                        const struct pipe_sampler_view *state,
                                                        unsigned width0, unsigned height0,
                                                        unsigned force_level);
void si_update_fb_dirtiness_after_rendering(struct si_context *sctx);
void si_update_ps_iter_samples(struct si_context *sctx);
void si_save_qbo_state(struct si_context *sctx, struct si_qbo_state *st);
void si_restore_qbo_state(struct si_context *sctx, struct si_qbo_state *st);
void si_set_occlusion_query_state(struct si_context *sctx, bool old_perfect_enable);

struct si_fast_udiv_info32 {
   unsigned multiplier; /* the "magic number" multiplier */
   unsigned pre_shift;  /* shift for the dividend before multiplying */
   unsigned post_shift; /* shift for the dividend after multiplying */
   int increment;       /* 0 or 1; if set then increment the numerator, using one of
                           the two strategies */
};

struct si_fast_udiv_info32 si_compute_fast_udiv_info32(uint32_t D, unsigned num_bits);

/* si_state_binning.c */
void si_emit_dpbb_state(struct si_context *sctx);

/* si_state_shaders.c */
void si_get_ir_cache_key(struct si_shader_selector *sel, bool ngg, bool es,
                         unsigned char ir_sha1_cache_key[20]);
bool si_shader_cache_load_shader(struct si_screen *sscreen, unsigned char ir_sha1_cache_key[20],
                                 struct si_shader *shader);
void si_shader_cache_insert_shader(struct si_screen *sscreen, unsigned char ir_sha1_cache_key[20],
                                   struct si_shader *shader, bool insert_into_disk_cache);
bool si_shader_mem_ordered(struct si_shader *shader);
bool si_update_shaders(struct si_context *sctx);
void si_init_screen_live_shader_cache(struct si_screen *sscreen);
void si_init_shader_functions(struct si_context *sctx);
bool si_init_shader_cache(struct si_screen *sscreen);
void si_destroy_shader_cache(struct si_screen *sscreen);
void si_schedule_initial_compile(struct si_context *sctx, gl_shader_stage stage,
                                 struct util_queue_fence *ready_fence,
                                 struct si_compiler_ctx_state *compiler_ctx_state, void *job,
                                 util_queue_execute_func execute);
void si_get_active_slot_masks(const struct si_shader_info *info, uint64_t *const_and_shader_buffers,
                              uint64_t *samplers_and_images);
int si_shader_select_with_key(struct si_screen *sscreen, struct si_shader_ctx_state *state,
                              struct si_compiler_ctx_state *compiler_state,
                              struct si_shader_key *key, int thread_index, bool optimized_or_none);
void si_shader_selector_key_vs(struct si_context *sctx, struct si_shader_selector *vs,
                               struct si_shader_key *key, struct si_vs_prolog_bits *prolog_key);
unsigned si_get_input_prim(const struct si_shader_selector *gs);
bool si_update_ngg(struct si_context *sctx);

/* si_state_draw.c */
void si_prim_discard_signal_next_compute_ib_start(struct si_context *sctx);
void si_trace_emit(struct si_context *sctx);
void si_init_draw_functions(struct si_context *sctx);

/* si_state_msaa.c */
void si_init_msaa_functions(struct si_context *sctx);
void si_emit_sample_locations(struct radeon_cmdbuf *cs, int nr_samples);

/* si_state_streamout.c */
void si_streamout_buffers_dirty(struct si_context *sctx);
void si_emit_streamout_end(struct si_context *sctx);
void si_update_prims_generated_query_state(struct si_context *sctx, unsigned type, int diff);
void si_init_streamout_functions(struct si_context *sctx);

static inline unsigned si_get_constbuf_slot(unsigned slot)
{
   /* Constant buffers are in slots [32..47], ascending */
   return SI_NUM_SHADER_BUFFERS + slot;
}

static inline unsigned si_get_shaderbuf_slot(unsigned slot)
{
   /* shader buffers are in slots [31..0], descending */
   return SI_NUM_SHADER_BUFFERS - 1 - slot;
}

static inline unsigned si_get_sampler_slot(unsigned slot)
{
   /* 32 samplers are in sampler slots [16..47], 16 dw per slot, ascending */
   /* those are equivalent to image slots [32..95], 8 dw per slot, ascending  */
   return SI_NUM_IMAGE_SLOTS / 2 + slot;
}

static inline unsigned si_get_image_slot(unsigned slot)
{
   /* image slots are in [31..0] (sampler slots [15..0]), descending */
   /* images are in slots [31..16], while FMASKs are in slots [15..0] */
   return SI_NUM_IMAGE_SLOTS - 1 - slot;
}

#ifdef __cplusplus
}
#endif

#endif
