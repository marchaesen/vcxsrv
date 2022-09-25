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
#ifndef RADV_ACO_SHADER_INFO_H
#define RADV_ACO_SHADER_INFO_H

/* this will convert from radv shader info to the ACO one. */

#include "aco_shader_info.h"

#define ASSIGN_FIELD(x) aco_info->x = radv->x
#define ASSIGN_FIELD_CP(x) memcpy(&aco_info->x, &radv->x, sizeof(radv->x))

static inline void
radv_aco_convert_shader_so_info(struct aco_shader_info *aco_info,
                       const struct radv_shader_info *radv)
{
   ASSIGN_FIELD(so.num_outputs);
   ASSIGN_FIELD_CP(so.outputs);
   ASSIGN_FIELD_CP(so.strides);
   /* enabled_stream_buffers_mask unused */
}

static inline void
radv_aco_convert_shader_vp_info(struct aco_vp_output_info *aco_info,
				const struct radv_vs_output_info *radv)
{
   ASSIGN_FIELD_CP(vs_output_param_offset);
   ASSIGN_FIELD(clip_dist_mask);
   ASSIGN_FIELD(cull_dist_mask);
   ASSIGN_FIELD(param_exports);
   ASSIGN_FIELD(prim_param_exports);
   ASSIGN_FIELD(writes_pointsize);
   ASSIGN_FIELD(writes_layer);
   ASSIGN_FIELD(writes_layer_per_primitive);
   ASSIGN_FIELD(writes_viewport_index);
   ASSIGN_FIELD(writes_viewport_index_per_primitive);
   ASSIGN_FIELD(writes_primitive_shading_rate);
   ASSIGN_FIELD(writes_primitive_shading_rate_per_primitive);
   ASSIGN_FIELD(export_prim_id);
   ASSIGN_FIELD(export_clip_dists);
   /* don't use export params */
}

static inline void
radv_aco_convert_shader_info(struct aco_shader_info *aco_info,
			     const struct radv_shader_info *radv)
{
   ASSIGN_FIELD(wave_size);
   ASSIGN_FIELD(is_ngg);
   ASSIGN_FIELD(has_ngg_culling);
   ASSIGN_FIELD(has_ngg_early_prim_export);
   ASSIGN_FIELD(workgroup_size);
   radv_aco_convert_shader_vp_info(&aco_info->outinfo, &radv->outinfo);
   ASSIGN_FIELD(vs.as_es);
   ASSIGN_FIELD(vs.as_ls);
   ASSIGN_FIELD(vs.tcs_in_out_eq);
   ASSIGN_FIELD(vs.tcs_temp_only_input_mask);
   ASSIGN_FIELD(vs.use_per_attribute_vb_descs);
   ASSIGN_FIELD(vs.vb_desc_usage_mask);
   ASSIGN_FIELD(vs.input_slot_usage_mask);
   ASSIGN_FIELD(vs.has_prolog);
   ASSIGN_FIELD(vs.dynamic_inputs);
   ASSIGN_FIELD_CP(gs.output_usage_mask);
   ASSIGN_FIELD_CP(gs.num_stream_output_components);
   ASSIGN_FIELD_CP(gs.output_streams);
   ASSIGN_FIELD(gs.vertices_out);
   ASSIGN_FIELD(tcs.num_lds_blocks);
   ASSIGN_FIELD(tes.as_es);
   ASSIGN_FIELD(ps.writes_z);
   ASSIGN_FIELD(ps.writes_stencil);
   ASSIGN_FIELD(ps.writes_sample_mask);
   ASSIGN_FIELD(ps.has_epilog);
   ASSIGN_FIELD(ps.num_interp);
   ASSIGN_FIELD(ps.spi_ps_input);
   ASSIGN_FIELD(cs.subgroup_size);
   radv_aco_convert_shader_so_info(aco_info, radv);
   aco_info->gfx9_gs_ring_lds_size = radv->gs_ring_info.lds_size;
}

#define ASSIGN_VS_STATE_FIELD(x) aco_info->state.x = radv->state->x
#define ASSIGN_VS_STATE_FIELD_CP(x) memcpy(&aco_info->state.x, &radv->state->x, sizeof(radv->state->x))
static inline void
radv_aco_convert_vs_prolog_key(struct aco_vs_prolog_key *aco_info,
			       const struct radv_vs_prolog_key *radv)
{
   ASSIGN_VS_STATE_FIELD(instance_rate_inputs);
   ASSIGN_VS_STATE_FIELD(nontrivial_divisors);
   ASSIGN_VS_STATE_FIELD(post_shuffle);
   ASSIGN_VS_STATE_FIELD(alpha_adjust_lo);
   ASSIGN_VS_STATE_FIELD(alpha_adjust_hi);
   ASSIGN_VS_STATE_FIELD_CP(divisors);
   ASSIGN_VS_STATE_FIELD_CP(formats);
   ASSIGN_FIELD(num_attributes);
   ASSIGN_FIELD(misaligned_mask);
   ASSIGN_FIELD(is_ngg);
   ASSIGN_FIELD(next_stage);
}

static inline void
radv_aco_convert_ps_epilog_key(struct aco_ps_epilog_key *aco_info,
			       const struct radv_ps_epilog_key *radv)
{
   ASSIGN_FIELD(spi_shader_col_format);
   ASSIGN_FIELD(color_is_int8);
   ASSIGN_FIELD(color_is_int10);
   ASSIGN_FIELD(enable_mrt_output_nan_fixup);
}

static inline void
radv_aco_convert_pipe_key(struct aco_stage_input *aco_info,
                          const struct radv_pipeline_key *radv)
{
   ASSIGN_FIELD(optimisations_disabled);
   ASSIGN_FIELD(image_2d_view_of_3d);
   ASSIGN_FIELD(vs.instance_rate_inputs);
   ASSIGN_FIELD_CP(vs.instance_rate_divisors);
   ASSIGN_FIELD_CP(vs.vertex_attribute_formats);
   ASSIGN_FIELD_CP(vs.vertex_attribute_bindings);
   ASSIGN_FIELD_CP(vs.vertex_attribute_offsets);
   ASSIGN_FIELD_CP(vs.vertex_attribute_strides);
   ASSIGN_FIELD_CP(vs.vertex_binding_align);
   ASSIGN_FIELD(tcs.tess_input_vertices);
   ASSIGN_FIELD(ps.col_format);
   ASSIGN_FIELD(ps.alpha_to_coverage_via_mrtz);
}

static inline void
radv_aco_convert_opts(struct aco_compiler_options *aco_info,
                      const struct radv_nir_compiler_options *radv)
{
   radv_aco_convert_pipe_key(&aco_info->key, &radv->key);
   ASSIGN_FIELD(robust_buffer_access);
   ASSIGN_FIELD(dump_shader);
   ASSIGN_FIELD(dump_preoptir);
   ASSIGN_FIELD(record_ir);
   ASSIGN_FIELD(record_stats);
   ASSIGN_FIELD(has_ls_vgpr_init_bug);
   ASSIGN_FIELD(wgp_mode);
   ASSIGN_FIELD(family);
   ASSIGN_FIELD(gfx_level);
   ASSIGN_FIELD(address32_hi);
   ASSIGN_FIELD(debug.func);
   ASSIGN_FIELD(debug.private_data);
}
#undef ASSIGN_VS_STATE_FIELD
#undef ASSIGN_VS_STATE_FIELD_CP
#undef ASSIGN_FIELD
#undef ASSIGN_FIELD_CP

#endif
