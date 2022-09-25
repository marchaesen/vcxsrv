/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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
#ifndef ACO_SHADER_INFO_H
#define ACO_SHADER_INFO_H

#include "shader_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ACO_MAX_SO_OUTPUTS 64
#define ACO_MAX_SO_BUFFERS 4
#define ACO_MAX_VERTEX_ATTRIBS 32
#define ACO_MAX_VBS 32

struct aco_vs_input_state {
   uint32_t instance_rate_inputs;
   uint32_t nontrivial_divisors;
   uint32_t post_shuffle;
   /* Having two separate fields instead of a single uint64_t makes it easier to remove attributes
    * using bitwise arithmetic.
    */
   uint32_t alpha_adjust_lo;
   uint32_t alpha_adjust_hi;

   uint32_t divisors[ACO_MAX_VERTEX_ATTRIBS];
   uint8_t formats[ACO_MAX_VERTEX_ATTRIBS];
};

struct aco_vs_prolog_key {
   struct aco_vs_input_state state;
   unsigned num_attributes;
   uint32_t misaligned_mask;
   bool is_ngg;
   gl_shader_stage next_stage;
};

struct aco_ps_epilog_key {
   uint32_t spi_shader_col_format;

   /* Bitmasks, each bit represents one of the 8 MRTs. */
   uint8_t color_is_int8;
   uint8_t color_is_int10;
   uint8_t enable_mrt_output_nan_fixup;
};

struct aco_vp_output_info {
   uint8_t vs_output_param_offset[VARYING_SLOT_MAX];
   uint8_t clip_dist_mask;
   uint8_t cull_dist_mask;
   uint8_t param_exports;
   uint8_t prim_param_exports;
   bool writes_pointsize;
   bool writes_layer;
   bool writes_layer_per_primitive;
   bool writes_viewport_index;
   bool writes_viewport_index_per_primitive;
   bool writes_primitive_shading_rate;
   bool writes_primitive_shading_rate_per_primitive;
   bool export_prim_id;
   bool export_clip_dists;
};

struct aco_stream_output {
   uint8_t location;
   uint8_t buffer;
   uint16_t offset;
   uint8_t component_mask;
   uint8_t stream;
};

struct aco_streamout_info {
   uint16_t num_outputs;
   struct aco_stream_output outputs[ACO_MAX_SO_OUTPUTS];
   uint16_t strides[ACO_MAX_SO_BUFFERS];
};

struct aco_shader_info {
   uint8_t wave_size;
   bool is_ngg;
   bool has_ngg_culling;
   bool has_ngg_early_prim_export;
   unsigned workgroup_size;
   struct aco_vp_output_info outinfo;
   struct {
      bool as_es;
      bool as_ls;
      bool tcs_in_out_eq;
      uint64_t tcs_temp_only_input_mask;
      bool use_per_attribute_vb_descs;
      uint32_t vb_desc_usage_mask;
      uint32_t input_slot_usage_mask;
      bool has_prolog;
      bool dynamic_inputs;
   } vs;
   struct {
      uint8_t output_usage_mask[VARYING_SLOT_VAR31 + 1];
      uint8_t num_stream_output_components[4];
      uint8_t output_streams[VARYING_SLOT_VAR31 + 1];
      unsigned vertices_out;
   } gs;
   struct {
      uint32_t num_lds_blocks;
   } tcs;
   struct {
      bool as_es;
   } tes;
   struct {
      bool writes_z;
      bool writes_stencil;
      bool writes_sample_mask;
      bool has_epilog;
      uint32_t num_interp;
      unsigned spi_ps_input;
   } ps;
   struct {
      uint8_t subgroup_size;
   } cs;
   struct aco_streamout_info so;

   uint32_t gfx9_gs_ring_lds_size;
};

enum aco_compiler_debug_level {
   ACO_COMPILER_DEBUG_LEVEL_PERFWARN,
   ACO_COMPILER_DEBUG_LEVEL_ERROR,
};

struct aco_stage_input {
   uint32_t optimisations_disabled : 1;
   uint32_t image_2d_view_of_3d : 1;
   struct {
      uint32_t instance_rate_inputs;
      uint32_t instance_rate_divisors[ACO_MAX_VERTEX_ATTRIBS];
      uint8_t vertex_attribute_formats[ACO_MAX_VERTEX_ATTRIBS];
      uint32_t vertex_attribute_bindings[ACO_MAX_VERTEX_ATTRIBS];
      uint32_t vertex_attribute_offsets[ACO_MAX_VERTEX_ATTRIBS];
      uint32_t vertex_attribute_strides[ACO_MAX_VERTEX_ATTRIBS];
      uint8_t vertex_binding_align[ACO_MAX_VBS];
   } vs;

   struct {
      unsigned tess_input_vertices;
   } tcs;

   struct {
      uint32_t col_format;

      /* Used to export alpha through MRTZ for alpha-to-coverage (GFX11+). */
      bool alpha_to_coverage_via_mrtz;
   } ps;
};

struct aco_compiler_options {
   struct aco_stage_input key;
   bool robust_buffer_access;
   bool dump_shader;
   bool dump_preoptir;
   bool record_ir;
   bool record_stats;
   bool has_ls_vgpr_init_bug;
   bool wgp_mode;
   enum radeon_family family;
   enum amd_gfx_level gfx_level;
   uint32_t address32_hi;
   struct {
      void (*func)(void *private_data, enum aco_compiler_debug_level level, const char *message);
      void *private_data;
   } debug;
};

#ifdef __cplusplus
}
#endif
#endif
