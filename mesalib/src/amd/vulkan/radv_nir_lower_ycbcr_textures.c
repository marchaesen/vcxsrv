/*
 * Copyright © 2017 Intel Corporation
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

#include "radv_private.h"
#include "radv_shader.h"
#include "vk_format.h"
#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "nir/nir_vulkan.h"

struct ycbcr_state {
	nir_builder *builder;
	nir_ssa_def *image_size;
	nir_tex_instr *origin_tex;
	nir_deref_instr *tex_deref;
	const struct radv_sampler_ycbcr_conversion *conversion;
};

static nir_ssa_def *
get_texture_size(struct ycbcr_state *state, nir_deref_instr *texture)
{
	nir_builder *b = state->builder;
	const struct glsl_type *type = texture->type;
	nir_tex_instr *tex = nir_tex_instr_create(b->shader, 1);

	tex->op = nir_texop_txs;
	tex->sampler_dim = glsl_get_sampler_dim(type);
	tex->is_array = glsl_sampler_type_is_array(type);
	tex->is_shadow = glsl_sampler_type_is_shadow(type);
	tex->dest_type = nir_type_int;

	tex->src[0].src_type = nir_tex_src_texture_deref;
	tex->src[0].src = nir_src_for_ssa(&texture->dest.ssa);

	nir_ssa_dest_init(&tex->instr, &tex->dest,
	                  nir_tex_instr_dest_size(tex), 32, NULL);
	nir_builder_instr_insert(b, &tex->instr);

	return nir_i2f32(b, &tex->dest.ssa);
}

static nir_ssa_def *
implicit_downsampled_coord(nir_builder *b,
                           nir_ssa_def *value,
                           nir_ssa_def *max_value,
                           int div_scale)
{
	return nir_fadd(b,
	                value,
	                nir_fdiv(b,
	                         nir_imm_float(b, 1.0f),
	                         nir_fmul(b,
	                                  nir_imm_float(b, div_scale),
	                                  max_value)));
}

static nir_ssa_def *
implicit_downsampled_coords(struct ycbcr_state *state,
                            nir_ssa_def *old_coords)
{
	nir_builder *b = state->builder;
	const struct radv_sampler_ycbcr_conversion *conversion = state->conversion;
	nir_ssa_def *image_size = NULL;
	nir_ssa_def *comp[4] = { NULL, };
	const struct vk_format_description *fmt_desc = vk_format_description(state->conversion->format);
	const unsigned divisors[2] = {fmt_desc->width_divisor, fmt_desc->height_divisor};

	for (int c = 0; c < old_coords->num_components; c++) {
		if (c < ARRAY_SIZE(divisors) && divisors[c] > 1 &&
		    conversion->chroma_offsets[c] == VK_CHROMA_LOCATION_COSITED_EVEN) {
			if (!image_size)
				image_size = get_texture_size(state, state->tex_deref);

			comp[c] = implicit_downsampled_coord(b,
			                                     nir_channel(b, old_coords, c),
			                                     nir_channel(b, image_size, c),
			                                     divisors[c]);
		} else {
			comp[c] = nir_channel(b, old_coords, c);
		}
	}

	return nir_vec(b, comp, old_coords->num_components);
}

static nir_ssa_def *
create_plane_tex_instr_implicit(struct ycbcr_state *state,
                                uint32_t plane)
{
	nir_builder *b = state->builder;
	nir_tex_instr *old_tex = state->origin_tex;
	nir_tex_instr *tex = nir_tex_instr_create(b->shader, old_tex->num_srcs+ 1);
	for (uint32_t i = 0; i < old_tex->num_srcs; i++) {
		tex->src[i].src_type = old_tex->src[i].src_type;

		switch (old_tex->src[i].src_type) {
		case nir_tex_src_coord:
			if (plane && true/*state->conversion->chroma_reconstruction*/) {
				assert(old_tex->src[i].src.is_ssa);
				tex->src[i].src =
					nir_src_for_ssa(implicit_downsampled_coords(state,
					                                            old_tex->src[i].src.ssa));
				break;
			}
		/* fall through */
		default:
			nir_src_copy(&tex->src[i].src, &old_tex->src[i].src, tex);
			break;
		}
	}

	tex->src[tex->num_srcs - 1].src = nir_src_for_ssa(nir_imm_int(b, plane));
	tex->src[tex->num_srcs - 1].src_type = nir_tex_src_plane;

	tex->sampler_dim = old_tex->sampler_dim;
	tex->dest_type = old_tex->dest_type;
	tex->is_array = old_tex->is_array;

	tex->op = old_tex->op;
	tex->coord_components = old_tex->coord_components;
	tex->is_new_style_shadow = old_tex->is_new_style_shadow;
	tex->component = old_tex->component;

	tex->texture_index = old_tex->texture_index;
	tex->sampler_index = old_tex->sampler_index;

	nir_ssa_dest_init(&tex->instr, &tex->dest,
	                  old_tex->dest.ssa.num_components,
	                  nir_dest_bit_size(old_tex->dest), NULL);
	nir_builder_instr_insert(b, &tex->instr);

	return &tex->dest.ssa;
}

struct swizzle_info {
	unsigned plane[4];
	unsigned swizzle[4];
};

static struct swizzle_info
get_plane_swizzles(VkFormat format)
{
	int planes = vk_format_get_plane_count(format);
	switch (planes) {
	case 3:
		return (struct swizzle_info) {
			{2, 0, 1, 0},
			{0, 0, 0, 3}
		};
	case 2:
		return (struct swizzle_info) {
			{1, 0, 1, 0},
			{1, 0, 0, 3}
		};
	case 1:
		return (struct swizzle_info) {
			{0, 0, 0, 0},
			{0, 1, 2, 3}
		};
	default:
		unreachable("unhandled plane count for ycbcr swizzling");
	}
}


static nir_ssa_def *
build_swizzled_components(nir_builder *builder,
                          VkFormat format,
                          VkComponentMapping mapping,
                          nir_ssa_def **plane_values)
{
	struct swizzle_info plane_swizzle = get_plane_swizzles(format);
	enum vk_swizzle swizzles[4];
	nir_ssa_def *values[4];

	vk_format_compose_swizzles(&mapping, (const unsigned char[4]){0,1,2,3}, swizzles);

	nir_ssa_def *zero = nir_imm_float(builder, 0.0f);
	nir_ssa_def *one = nir_imm_float(builder, 1.0f);

	for (unsigned i = 0; i < 4; ++i) {
		switch(swizzles[i]) {
		case VK_SWIZZLE_X:
		case VK_SWIZZLE_Y:
		case VK_SWIZZLE_Z:
		case VK_SWIZZLE_W: {
			unsigned channel = swizzles[i] - VK_SWIZZLE_X;
			values[i] = nir_channel(builder,
			                        plane_values[plane_swizzle.plane[channel]],
			                        plane_swizzle.swizzle[channel]);
			break;
		}
		case VK_SWIZZLE_0:
			values[i] = zero;
			break;
		case VK_SWIZZLE_1:
			values[i] = one;
			break;
		default:
			unreachable("unhandled swizzle");
		}
	}
	return nir_vec(builder, values, 4);
}

static bool
try_lower_tex_ycbcr(const struct radv_pipeline_layout *layout,
                    nir_builder *builder,
                    nir_tex_instr *tex)
{
	int deref_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
	assert(deref_src_idx >= 0);
	nir_deref_instr *deref = nir_src_as_deref(tex->src[deref_src_idx].src);

	nir_variable *var = nir_deref_instr_get_variable(deref);
	const struct radv_descriptor_set_layout *set_layout =
		layout->set[var->data.descriptor_set].layout;
	const struct radv_descriptor_set_binding_layout *binding =
		&set_layout->binding[var->data.binding];
	const struct radv_sampler_ycbcr_conversion *ycbcr_samplers =
		radv_immutable_ycbcr_samplers(set_layout, var->data.binding);

	if (!ycbcr_samplers)
		return false;

	/* For the following instructions, we don't apply any change and let the
	 * instruction apply to the first plane.
	 */
	if (tex->op == nir_texop_txs ||
	    tex->op == nir_texop_query_levels ||
	    tex->op == nir_texop_lod)
		return false;

	assert(tex->texture_index == 0);
	unsigned array_index = 0;
	if (deref->deref_type != nir_deref_type_var) {
		assert(deref->deref_type == nir_deref_type_array);
		if (!nir_src_is_const(deref->arr.index))
			return false;
		array_index = nir_src_as_uint(deref->arr.index);
		array_index = MIN2(array_index, binding->array_size - 1);
	}
	const struct radv_sampler_ycbcr_conversion *ycbcr_sampler = ycbcr_samplers + array_index;

	if (ycbcr_sampler->format == VK_FORMAT_UNDEFINED)
		return false;

	struct ycbcr_state state = {
		.builder = builder,
		.origin_tex = tex,
		.tex_deref = deref,
		.conversion = ycbcr_sampler,
	};

	builder->cursor = nir_before_instr(&tex->instr);

	VkFormat format = state.conversion->format;
	const int plane_count = vk_format_get_plane_count(format);
	nir_ssa_def *plane_values[3];

	for (int p = 0; p < plane_count; ++p) {
		plane_values[p] = create_plane_tex_instr_implicit(&state, p);
	}

	nir_ssa_def *result = build_swizzled_components(builder, format, ycbcr_sampler->components, plane_values);
	if (state.conversion->ycbcr_model != VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY) {
		VkFormat first_format = vk_format_get_plane_format(format, 0);
		uint32_t bits = vk_format_get_component_bits(first_format, VK_FORMAT_COLORSPACE_RGB, VK_SWIZZLE_X);
		/* TODO: swizzle and bpcs */
		uint32_t bpcs[3] = {bits, bits, bits};
		result = nir_convert_ycbcr_to_rgb(builder,
		                                  state.conversion->ycbcr_model,
		                                  state.conversion->ycbcr_range,
		                                  result,
		                                  bpcs);
	}

	nir_ssa_def_rewrite_uses(&tex->dest.ssa, nir_src_for_ssa(result));
	nir_instr_remove(&tex->instr);

	return true;
}

bool
radv_nir_lower_ycbcr_textures(nir_shader *shader,
                             const struct radv_pipeline_layout *layout)
{
	bool progress = false;

	nir_foreach_function(function, shader) {
		if (!function->impl)
			continue;

		bool function_progress = false;
		nir_builder builder;
		nir_builder_init(&builder, function->impl);

		nir_foreach_block(block, function->impl) {
			nir_foreach_instr_safe(instr, block) {
				if (instr->type != nir_instr_type_tex)
					continue;

				nir_tex_instr *tex = nir_instr_as_tex(instr);
				function_progress |= try_lower_tex_ycbcr(layout, &builder, tex);
			}
		}

		if (function_progress) {
			nir_metadata_preserve(function->impl,
			                      nir_metadata_block_index |
			                      nir_metadata_dominance);
		}

		progress |= function_progress;
	}

	return progress;
}
