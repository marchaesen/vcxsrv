/*
 * Copyrigh 2016 Red Hat Inc.
 * Based on anv:
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

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "nir/nir_builder.h"
#include "radv_meta.h"
#include "radv_private.h"
#include "radv_cs.h"
#include "sid.h"

#define TIMESTAMP_NOT_READY UINT64_MAX

static const int pipelinestat_block_size = 11 * 8;
static const unsigned pipeline_statistics_indices[] = {7, 6, 3, 4, 5, 2, 1, 0, 8, 9, 10};

static unsigned get_max_db(struct radv_device *device)
{
	unsigned num_db = device->physical_device->rad_info.num_render_backends;
	MAYBE_UNUSED unsigned rb_mask = device->physical_device->rad_info.enabled_rb_mask;

	/* Otherwise we need to change the query reset procedure */
	assert(rb_mask == ((1ull << num_db) - 1));

	return num_db;
}


static nir_ssa_def *nir_test_flag(nir_builder *b, nir_ssa_def *flags, uint32_t flag)
{
	return nir_i2b(b, nir_iand(b, flags, nir_imm_int(b, flag)));
}

static void radv_break_on_count(nir_builder *b, nir_variable *var, nir_ssa_def *count)
{
	nir_ssa_def *counter = nir_load_var(b, var);

	nir_if *if_stmt = nir_if_create(b->shader);
	if_stmt->condition = nir_src_for_ssa(nir_uge(b, counter, count));
	nir_cf_node_insert(b->cursor, &if_stmt->cf_node);

	b->cursor = nir_after_cf_list(&if_stmt->then_list);

	nir_jump_instr *instr = nir_jump_instr_create(b->shader, nir_jump_break);
	nir_builder_instr_insert(b, &instr->instr);

	b->cursor = nir_after_cf_node(&if_stmt->cf_node);
	counter = nir_iadd(b, counter, nir_imm_int(b, 1));
	nir_store_var(b, var, counter, 0x1);
}

static struct nir_ssa_def *
radv_load_push_int(nir_builder *b, unsigned offset, const char *name)
{
	nir_intrinsic_instr *flags = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(flags, 0);
	nir_intrinsic_set_range(flags, 16);
	flags->src[0] = nir_src_for_ssa(nir_imm_int(b, offset));
	flags->num_components = 1;
	nir_ssa_dest_init(&flags->instr, &flags->dest, 1, 32, name);
	nir_builder_instr_insert(b, &flags->instr);
	return &flags->dest.ssa;
}

static nir_shader *
build_occlusion_query_shader(struct radv_device *device) {
	/* the shader this builds is roughly
	 *
	 * push constants {
	 * 	uint32_t flags;
	 * 	uint32_t dst_stride;
	 * };
	 *
	 * uint32_t src_stride = 16 * db_count;
	 *
	 * location(binding = 0) buffer dst_buf;
	 * location(binding = 1) buffer src_buf;
	 *
	 * void main() {
	 * 	uint64_t result = 0;
	 * 	uint64_t src_offset = src_stride * global_id.x;
	 * 	uint64_t dst_offset = dst_stride * global_id.x;
	 * 	bool available = true;
	 * 	for (int i = 0; i < db_count; ++i) {
	 * 		uint64_t start = src_buf[src_offset + 16 * i];
	 * 		uint64_t end = src_buf[src_offset + 16 * i + 8];
	 * 		if ((start & (1ull << 63)) && (end & (1ull << 63)))
	 * 			result += end - start;
	 * 		else
	 * 			available = false;
	 * 	}
	 * 	uint32_t elem_size = flags & VK_QUERY_RESULT_64_BIT ? 8 : 4;
	 * 	if ((flags & VK_QUERY_RESULT_PARTIAL_BIT) || available) {
	 * 		if (flags & VK_QUERY_RESULT_64_BIT)
	 * 			dst_buf[dst_offset] = result;
	 * 		else
	 * 			dst_buf[dst_offset] = (uint32_t)result.
	 * 	}
	 * 	if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
	 * 		dst_buf[dst_offset + elem_size] = available;
	 * 	}
	 * }
	 */
	nir_builder b;
	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, "occlusion_query");
	b.shader->info.cs.local_size[0] = 64;
	b.shader->info.cs.local_size[1] = 1;
	b.shader->info.cs.local_size[2] = 1;

	nir_variable *result = nir_local_variable_create(b.impl, glsl_uint64_t_type(), "result");
	nir_variable *outer_counter = nir_local_variable_create(b.impl, glsl_int_type(), "outer_counter");
	nir_variable *start = nir_local_variable_create(b.impl, glsl_uint64_t_type(), "start");
	nir_variable *end = nir_local_variable_create(b.impl, glsl_uint64_t_type(), "end");
	nir_variable *available = nir_local_variable_create(b.impl, glsl_bool_type(), "available");
	unsigned db_count = get_max_db(device);

	nir_ssa_def *flags = radv_load_push_int(&b, 0, "flags");

	nir_intrinsic_instr *dst_buf = nir_intrinsic_instr_create(b.shader,
	                                                          nir_intrinsic_vulkan_resource_index);
	dst_buf->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	dst_buf->num_components = 1;
	nir_intrinsic_set_desc_set(dst_buf, 0);
	nir_intrinsic_set_binding(dst_buf, 0);
	nir_ssa_dest_init(&dst_buf->instr, &dst_buf->dest, dst_buf->num_components, 32, NULL);
	nir_builder_instr_insert(&b, &dst_buf->instr);

	nir_intrinsic_instr *src_buf = nir_intrinsic_instr_create(b.shader,
	                                                          nir_intrinsic_vulkan_resource_index);
	src_buf->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	src_buf->num_components = 1;
	nir_intrinsic_set_desc_set(src_buf, 0);
	nir_intrinsic_set_binding(src_buf, 1);
	nir_ssa_dest_init(&src_buf->instr, &src_buf->dest, src_buf->num_components, 32, NULL);
	nir_builder_instr_insert(&b, &src_buf->instr);

	nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
	nir_ssa_def *wg_id = nir_load_work_group_id(&b);
	nir_ssa_def *block_size = nir_imm_ivec4(&b,
	                                        b.shader->info.cs.local_size[0],
	                                        b.shader->info.cs.local_size[1],
	                                        b.shader->info.cs.local_size[2], 0);
	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);
	global_id = nir_channel(&b, global_id, 0); // We only care about x here.

	nir_ssa_def *input_stride = nir_imm_int(&b, db_count * 16);
	nir_ssa_def *input_base = nir_imul(&b, input_stride, global_id);
	nir_ssa_def *output_stride = radv_load_push_int(&b, 4, "output_stride");
	nir_ssa_def *output_base = nir_imul(&b, output_stride, global_id);


	nir_store_var(&b, result, nir_imm_int64(&b, 0), 0x1);
	nir_store_var(&b, outer_counter, nir_imm_int(&b, 0), 0x1);
	nir_store_var(&b, available, nir_imm_true(&b), 0x1);

	nir_loop *outer_loop = nir_loop_create(b.shader);
	nir_builder_cf_insert(&b, &outer_loop->cf_node);
	b.cursor = nir_after_cf_list(&outer_loop->body);

	nir_ssa_def *current_outer_count = nir_load_var(&b, outer_counter);
	radv_break_on_count(&b, outer_counter, nir_imm_int(&b, db_count));

	nir_ssa_def *load_offset = nir_imul(&b, current_outer_count, nir_imm_int(&b, 16));
	load_offset = nir_iadd(&b, input_base, load_offset);

	nir_intrinsic_instr *load = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_ssbo);
	load->src[0] = nir_src_for_ssa(&src_buf->dest.ssa);
	load->src[1] = nir_src_for_ssa(load_offset);
	nir_ssa_dest_init(&load->instr, &load->dest, 2, 64, NULL);
	load->num_components = 2;
	nir_builder_instr_insert(&b, &load->instr);

	nir_store_var(&b, start, nir_channel(&b, &load->dest.ssa, 0), 0x1);
	nir_store_var(&b, end, nir_channel(&b, &load->dest.ssa, 1), 0x1);

	nir_ssa_def *start_done = nir_ilt(&b, nir_load_var(&b, start), nir_imm_int64(&b, 0));
	nir_ssa_def *end_done = nir_ilt(&b, nir_load_var(&b, end), nir_imm_int64(&b, 0));

	nir_if *update_if = nir_if_create(b.shader);
	update_if->condition = nir_src_for_ssa(nir_iand(&b, start_done, end_done));
	nir_cf_node_insert(b.cursor, &update_if->cf_node);

	b.cursor = nir_after_cf_list(&update_if->then_list);

	nir_store_var(&b, result,
	              nir_iadd(&b, nir_load_var(&b, result),
	                           nir_isub(&b, nir_load_var(&b, end),
	                                        nir_load_var(&b, start))), 0x1);

	b.cursor = nir_after_cf_list(&update_if->else_list);

	nir_store_var(&b, available, nir_imm_false(&b), 0x1);

	b.cursor = nir_after_cf_node(&outer_loop->cf_node);

	/* Store the result if complete or if partial results have been requested. */

	nir_ssa_def *result_is_64bit = nir_test_flag(&b, flags, VK_QUERY_RESULT_64_BIT);
	nir_ssa_def *result_size = nir_bcsel(&b, result_is_64bit, nir_imm_int(&b, 8), nir_imm_int(&b, 4));

	nir_if *store_if = nir_if_create(b.shader);
	store_if->condition = nir_src_for_ssa(nir_ior(&b, nir_test_flag(&b, flags, VK_QUERY_RESULT_PARTIAL_BIT), nir_load_var(&b, available)));
	nir_cf_node_insert(b.cursor, &store_if->cf_node);

	b.cursor = nir_after_cf_list(&store_if->then_list);

	nir_if *store_64bit_if = nir_if_create(b.shader);
	store_64bit_if->condition = nir_src_for_ssa(result_is_64bit);
	nir_cf_node_insert(b.cursor, &store_64bit_if->cf_node);

	b.cursor = nir_after_cf_list(&store_64bit_if->then_list);

	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_ssbo);
	store->src[0] = nir_src_for_ssa(nir_load_var(&b, result));
	store->src[1] = nir_src_for_ssa(&dst_buf->dest.ssa);
	store->src[2] = nir_src_for_ssa(output_base);
	nir_intrinsic_set_write_mask(store, 0x1);
	store->num_components = 1;
	nir_builder_instr_insert(&b, &store->instr);

	b.cursor = nir_after_cf_list(&store_64bit_if->else_list);

	store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_ssbo);
	store->src[0] = nir_src_for_ssa(nir_u2u32(&b, nir_load_var(&b, result)));
	store->src[1] = nir_src_for_ssa(&dst_buf->dest.ssa);
	store->src[2] = nir_src_for_ssa(output_base);
	nir_intrinsic_set_write_mask(store, 0x1);
	store->num_components = 1;
	nir_builder_instr_insert(&b, &store->instr);

	b.cursor = nir_after_cf_node(&store_if->cf_node);

	/* Store the availability bit if requested. */

	nir_if *availability_if = nir_if_create(b.shader);
	availability_if->condition = nir_src_for_ssa(nir_test_flag(&b, flags, VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
	nir_cf_node_insert(b.cursor, &availability_if->cf_node);

	b.cursor = nir_after_cf_list(&availability_if->then_list);

	store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_ssbo);
	store->src[0] = nir_src_for_ssa(nir_b2i32(&b, nir_load_var(&b, available)));
	store->src[1] = nir_src_for_ssa(&dst_buf->dest.ssa);
	store->src[2] = nir_src_for_ssa(nir_iadd(&b, result_size, output_base));
	nir_intrinsic_set_write_mask(store, 0x1);
	store->num_components = 1;
	nir_builder_instr_insert(&b, &store->instr);

	return b.shader;
}

static nir_shader *
build_pipeline_statistics_query_shader(struct radv_device *device) {
	/* the shader this builds is roughly
	 *
	 * push constants {
	 * 	uint32_t flags;
	 * 	uint32_t dst_stride;
	 * 	uint32_t stats_mask;
	 * 	uint32_t avail_offset;
	 * };
	 *
	 * uint32_t src_stride = pipelinestat_block_size * 2;
	 *
	 * location(binding = 0) buffer dst_buf;
	 * location(binding = 1) buffer src_buf;
	 *
	 * void main() {
	 * 	uint64_t src_offset = src_stride * global_id.x;
	 * 	uint64_t dst_base = dst_stride * global_id.x;
	 * 	uint64_t dst_offset = dst_base;
	 * 	uint32_t elem_size = flags & VK_QUERY_RESULT_64_BIT ? 8 : 4;
	 * 	uint32_t elem_count = stats_mask >> 16;
	 * 	uint32_t available32 = src_buf[avail_offset + 4 * global_id.x];
	 * 	if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
	 * 		dst_buf[dst_offset + elem_count * elem_size] = available32;
	 * 	}
	 * 	if ((bool)available32) {
	 * 		// repeat 11 times:
	 * 		if (stats_mask & (1 << 0)) {
	 * 			uint64_t start = src_buf[src_offset + 8 * indices[0]];
	 * 			uint64_t end = src_buf[src_offset + 8 * indices[0] + pipelinestat_block_size];
	 * 			uint64_t result = end - start;
	 * 			if (flags & VK_QUERY_RESULT_64_BIT)
	 * 				dst_buf[dst_offset] = result;
	 * 			else
	 * 				dst_buf[dst_offset] = (uint32_t)result.
	 * 			dst_offset += elem_size;
	 * 		}
	 * 	} else if (flags & VK_QUERY_RESULT_PARTIAL_BIT) {
	 *              // Set everything to 0 as we don't know what is valid.
	 * 		for (int i = 0; i < elem_count; ++i)
	 * 			dst_buf[dst_base + elem_size * i] = 0;
	 * 	}
	 * }
	 */
	nir_builder b;
	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, "pipeline_statistics_query");
	b.shader->info.cs.local_size[0] = 64;
	b.shader->info.cs.local_size[1] = 1;
	b.shader->info.cs.local_size[2] = 1;

	nir_variable *output_offset = nir_local_variable_create(b.impl, glsl_int_type(), "output_offset");

	nir_ssa_def *flags = radv_load_push_int(&b, 0, "flags");
	nir_ssa_def *stats_mask = radv_load_push_int(&b, 8, "stats_mask");
	nir_ssa_def *avail_offset = radv_load_push_int(&b, 12, "avail_offset");

	nir_intrinsic_instr *dst_buf = nir_intrinsic_instr_create(b.shader,
	                                                          nir_intrinsic_vulkan_resource_index);
	dst_buf->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	dst_buf->num_components = 1;;
	nir_intrinsic_set_desc_set(dst_buf, 0);
	nir_intrinsic_set_binding(dst_buf, 0);
	nir_ssa_dest_init(&dst_buf->instr, &dst_buf->dest, dst_buf->num_components, 32, NULL);
	nir_builder_instr_insert(&b, &dst_buf->instr);

	nir_intrinsic_instr *src_buf = nir_intrinsic_instr_create(b.shader,
	                                                          nir_intrinsic_vulkan_resource_index);
	src_buf->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	src_buf->num_components = 1;
	nir_intrinsic_set_desc_set(src_buf, 0);
	nir_intrinsic_set_binding(src_buf, 1);
	nir_ssa_dest_init(&src_buf->instr, &src_buf->dest, src_buf->num_components, 32, NULL);
	nir_builder_instr_insert(&b, &src_buf->instr);

	nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
	nir_ssa_def *wg_id = nir_load_work_group_id(&b);
	nir_ssa_def *block_size = nir_imm_ivec4(&b,
	                                        b.shader->info.cs.local_size[0],
	                                        b.shader->info.cs.local_size[1],
	                                        b.shader->info.cs.local_size[2], 0);
	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);
	global_id = nir_channel(&b, global_id, 0); // We only care about x here.

	nir_ssa_def *input_stride = nir_imm_int(&b, pipelinestat_block_size * 2);
	nir_ssa_def *input_base = nir_imul(&b, input_stride, global_id);
	nir_ssa_def *output_stride = radv_load_push_int(&b, 4, "output_stride");
	nir_ssa_def *output_base = nir_imul(&b, output_stride, global_id);


	avail_offset = nir_iadd(&b, avail_offset,
	                            nir_imul(&b, global_id, nir_imm_int(&b, 4)));

	nir_intrinsic_instr *load = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_ssbo);
	load->src[0] = nir_src_for_ssa(&src_buf->dest.ssa);
	load->src[1] = nir_src_for_ssa(avail_offset);
	nir_ssa_dest_init(&load->instr, &load->dest, 1, 32, NULL);
	load->num_components = 1;
	nir_builder_instr_insert(&b, &load->instr);
	nir_ssa_def *available32 = &load->dest.ssa;

	nir_ssa_def *result_is_64bit = nir_test_flag(&b, flags, VK_QUERY_RESULT_64_BIT);
	nir_ssa_def *elem_size = nir_bcsel(&b, result_is_64bit, nir_imm_int(&b, 8), nir_imm_int(&b, 4));
	nir_ssa_def *elem_count = nir_ushr(&b, stats_mask, nir_imm_int(&b, 16));

	/* Store the availability bit if requested. */

	nir_if *availability_if = nir_if_create(b.shader);
	availability_if->condition = nir_src_for_ssa(nir_test_flag(&b, flags, VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
	nir_cf_node_insert(b.cursor, &availability_if->cf_node);

	b.cursor = nir_after_cf_list(&availability_if->then_list);

	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_ssbo);
	store->src[0] = nir_src_for_ssa(available32);
	store->src[1] = nir_src_for_ssa(&dst_buf->dest.ssa);
	store->src[2] = nir_src_for_ssa(nir_iadd(&b, output_base, nir_imul(&b, elem_count, elem_size)));
	nir_intrinsic_set_write_mask(store, 0x1);
	store->num_components = 1;
	nir_builder_instr_insert(&b, &store->instr);

	b.cursor = nir_after_cf_node(&availability_if->cf_node);

	nir_if *available_if = nir_if_create(b.shader);
	available_if->condition = nir_src_for_ssa(nir_i2b(&b, available32));
	nir_cf_node_insert(b.cursor, &available_if->cf_node);

	b.cursor = nir_after_cf_list(&available_if->then_list);

	nir_store_var(&b, output_offset, output_base, 0x1);
	for (int i = 0; i < 11; ++i) {
		nir_if *store_if = nir_if_create(b.shader);
		store_if->condition = nir_src_for_ssa(nir_test_flag(&b, stats_mask, 1u << i));
		nir_cf_node_insert(b.cursor, &store_if->cf_node);

		b.cursor = nir_after_cf_list(&store_if->then_list);

		load = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_ssbo);
		load->src[0] = nir_src_for_ssa(&src_buf->dest.ssa);
		load->src[1] = nir_src_for_ssa(nir_iadd(&b, input_base,
		                                            nir_imm_int(&b, pipeline_statistics_indices[i] * 8)));
		nir_ssa_dest_init(&load->instr, &load->dest, 1, 64, NULL);
		load->num_components = 1;
		nir_builder_instr_insert(&b, &load->instr);
		nir_ssa_def *start = &load->dest.ssa;

		load = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_ssbo);
		load->src[0] = nir_src_for_ssa(&src_buf->dest.ssa);
		load->src[1] = nir_src_for_ssa(nir_iadd(&b, input_base,
		                                            nir_imm_int(&b, pipeline_statistics_indices[i] * 8 + pipelinestat_block_size)));
		nir_ssa_dest_init(&load->instr, &load->dest, 1, 64, NULL);
		load->num_components = 1;
		nir_builder_instr_insert(&b, &load->instr);
		nir_ssa_def *end = &load->dest.ssa;

		nir_ssa_def *result = nir_isub(&b, end, start);

		/* Store result */
		nir_if *store_64bit_if = nir_if_create(b.shader);
		store_64bit_if->condition = nir_src_for_ssa(result_is_64bit);
		nir_cf_node_insert(b.cursor, &store_64bit_if->cf_node);

		b.cursor = nir_after_cf_list(&store_64bit_if->then_list);

		nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_ssbo);
		store->src[0] = nir_src_for_ssa(result);
		store->src[1] = nir_src_for_ssa(&dst_buf->dest.ssa);
		store->src[2] = nir_src_for_ssa(nir_load_var(&b, output_offset));
		nir_intrinsic_set_write_mask(store, 0x1);
		store->num_components = 1;
		nir_builder_instr_insert(&b, &store->instr);

		b.cursor = nir_after_cf_list(&store_64bit_if->else_list);

		store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_ssbo);
		store->src[0] = nir_src_for_ssa(nir_u2u32(&b, result));
		store->src[1] = nir_src_for_ssa(&dst_buf->dest.ssa);
		store->src[2] = nir_src_for_ssa(nir_load_var(&b, output_offset));
		nir_intrinsic_set_write_mask(store, 0x1);
		store->num_components = 1;
		nir_builder_instr_insert(&b, &store->instr);

		b.cursor = nir_after_cf_node(&store_64bit_if->cf_node);

		nir_store_var(&b, output_offset,
		                  nir_iadd(&b, nir_load_var(&b, output_offset),
		                               elem_size), 0x1);

		b.cursor = nir_after_cf_node(&store_if->cf_node);
	}

	b.cursor = nir_after_cf_list(&available_if->else_list);

	available_if = nir_if_create(b.shader);
	available_if->condition = nir_src_for_ssa(nir_test_flag(&b, flags, VK_QUERY_RESULT_PARTIAL_BIT));
	nir_cf_node_insert(b.cursor, &available_if->cf_node);

	b.cursor = nir_after_cf_list(&available_if->then_list);

	/* Stores zeros in all outputs. */

	nir_variable *counter = nir_local_variable_create(b.impl, glsl_int_type(), "counter");
	nir_store_var(&b, counter, nir_imm_int(&b, 0), 0x1);

	nir_loop *loop = nir_loop_create(b.shader);
	nir_builder_cf_insert(&b, &loop->cf_node);
	b.cursor = nir_after_cf_list(&loop->body);

	nir_ssa_def *current_counter = nir_load_var(&b, counter);
	radv_break_on_count(&b, counter, elem_count);

	nir_ssa_def *output_elem = nir_iadd(&b, output_base,
	                                        nir_imul(&b, elem_size, current_counter));

	nir_if *store_64bit_if = nir_if_create(b.shader);
	store_64bit_if->condition = nir_src_for_ssa(result_is_64bit);
	nir_cf_node_insert(b.cursor, &store_64bit_if->cf_node);

	b.cursor = nir_after_cf_list(&store_64bit_if->then_list);

	store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_ssbo);
	store->src[0] = nir_src_for_ssa(nir_imm_int64(&b, 0));
	store->src[1] = nir_src_for_ssa(&dst_buf->dest.ssa);
	store->src[2] = nir_src_for_ssa(output_elem);
	nir_intrinsic_set_write_mask(store, 0x1);
	store->num_components = 1;
	nir_builder_instr_insert(&b, &store->instr);

	b.cursor = nir_after_cf_list(&store_64bit_if->else_list);

	store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_ssbo);
	store->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	store->src[1] = nir_src_for_ssa(&dst_buf->dest.ssa);
	store->src[2] = nir_src_for_ssa(output_elem);
	nir_intrinsic_set_write_mask(store, 0x1);
	store->num_components = 1;
	nir_builder_instr_insert(&b, &store->instr);

	b.cursor = nir_after_cf_node(&loop->cf_node);
	return b.shader;
}

static nir_shader *
build_tfb_query_shader(struct radv_device *device)
{
	/* the shader this builds is roughly
	 *
	 * uint32_t src_stride = 32;
	 *
	 * location(binding = 0) buffer dst_buf;
	 * location(binding = 1) buffer src_buf;
	 *
	 * void main() {
	 *	uint64_t result[2] = {};
	 *	bool available = false;
	 *	uint64_t src_offset = src_stride * global_id.x;
	 * 	uint64_t dst_offset = dst_stride * global_id.x;
	 * 	uint64_t *src_data = src_buf[src_offset];
	 *	uint32_t avail = (src_data[0] >> 32) &
	 *			 (src_data[1] >> 32) &
	 *			 (src_data[2] >> 32) &
	 *			 (src_data[3] >> 32);
	 *	if (avail & 0x80000000) {
	 *		result[0] = src_data[3] - src_data[1];
	 *		result[1] = src_data[2] - src_data[0];
	 *		available = true;
	 *	}
	 * 	uint32_t result_size = flags & VK_QUERY_RESULT_64_BIT ? 16 : 8;
	 * 	if ((flags & VK_QUERY_RESULT_PARTIAL_BIT) || available) {
	 *		if (flags & VK_QUERY_RESULT_64_BIT) {
	 *			dst_buf[dst_offset] = result;
	 *		} else {
	 *			dst_buf[dst_offset] = (uint32_t)result;
	 *		}
	 *	}
	 *	if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
	 *		dst_buf[dst_offset + result_size] = available;
	 * 	}
	 * }
	 */
	nir_builder b;
	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, "tfb_query");
	b.shader->info.cs.local_size[0] = 64;
	b.shader->info.cs.local_size[1] = 1;
	b.shader->info.cs.local_size[2] = 1;

	/* Create and initialize local variables. */
	nir_variable *result =
		nir_local_variable_create(b.impl,
					  glsl_vector_type(GLSL_TYPE_UINT64, 2),
					  "result");
	nir_variable *available =
		nir_local_variable_create(b.impl, glsl_bool_type(), "available");

	nir_store_var(&b, result,
		      nir_vec2(&b, nir_imm_int64(&b, 0),
				   nir_imm_int64(&b, 0)), 0x3);
	nir_store_var(&b, available, nir_imm_false(&b), 0x1);

	nir_ssa_def *flags = radv_load_push_int(&b, 0, "flags");

	/* Load resources. */
	nir_intrinsic_instr *dst_buf = nir_intrinsic_instr_create(b.shader,
	                                                          nir_intrinsic_vulkan_resource_index);
	dst_buf->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	dst_buf->num_components = 1;
	nir_intrinsic_set_desc_set(dst_buf, 0);
	nir_intrinsic_set_binding(dst_buf, 0);
	nir_ssa_dest_init(&dst_buf->instr, &dst_buf->dest, dst_buf->num_components, 32, NULL);
	nir_builder_instr_insert(&b, &dst_buf->instr);

	nir_intrinsic_instr *src_buf = nir_intrinsic_instr_create(b.shader,
	                                                          nir_intrinsic_vulkan_resource_index);
	src_buf->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	src_buf->num_components = 1;
	nir_intrinsic_set_desc_set(src_buf, 0);
	nir_intrinsic_set_binding(src_buf, 1);
	nir_ssa_dest_init(&src_buf->instr, &src_buf->dest, src_buf->num_components, 32, NULL);
	nir_builder_instr_insert(&b, &src_buf->instr);

	/* Compute global ID. */
	nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
	nir_ssa_def *wg_id = nir_load_work_group_id(&b);
	nir_ssa_def *block_size = nir_imm_ivec4(&b,
	                                        b.shader->info.cs.local_size[0],
	                                        b.shader->info.cs.local_size[1],
	                                        b.shader->info.cs.local_size[2], 0);
	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);
	global_id = nir_channel(&b, global_id, 0); // We only care about x here.

	/* Compute src/dst strides. */
	nir_ssa_def *input_stride = nir_imm_int(&b, 32);
	nir_ssa_def *input_base = nir_imul(&b, input_stride, global_id);
	nir_ssa_def *output_stride = radv_load_push_int(&b, 4, "output_stride");
	nir_ssa_def *output_base = nir_imul(&b, output_stride, global_id);

	/* Load data from the query pool. */
	nir_intrinsic_instr *load1 = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_ssbo);
	load1->src[0] = nir_src_for_ssa(&src_buf->dest.ssa);
	load1->src[1] = nir_src_for_ssa(input_base);
	nir_ssa_dest_init(&load1->instr, &load1->dest, 4, 32, NULL);
	load1->num_components = 4;
	nir_builder_instr_insert(&b, &load1->instr);

	nir_intrinsic_instr *load2 = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_ssbo);
	load2->src[0] = nir_src_for_ssa(&src_buf->dest.ssa);
	load2->src[1] = nir_src_for_ssa(nir_iadd(&b, input_base, nir_imm_int(&b, 16)));
	nir_ssa_dest_init(&load2->instr, &load2->dest, 4, 32, NULL);
	load2->num_components = 4;
	nir_builder_instr_insert(&b, &load2->instr);

	/* Check if result is available. */
	nir_ssa_def *avails[2];
	avails[0] = nir_iand(&b, nir_channel(&b, &load1->dest.ssa, 1),
				 nir_channel(&b, &load1->dest.ssa, 3));
	avails[1] = nir_iand(&b, nir_channel(&b, &load2->dest.ssa, 1),
				 nir_channel(&b, &load2->dest.ssa, 3));
	nir_ssa_def *result_is_available =
		nir_i2b(&b, nir_iand(&b, nir_iand(&b, avails[0], avails[1]),
			                 nir_imm_int(&b, 0x80000000)));

	/* Only compute result if available. */
	nir_if *available_if = nir_if_create(b.shader);
	available_if->condition = nir_src_for_ssa(result_is_available);
	nir_cf_node_insert(b.cursor, &available_if->cf_node);

	b.cursor = nir_after_cf_list(&available_if->then_list);

	/* Pack values. */
	nir_ssa_def *packed64[4];
	packed64[0] = nir_pack_64_2x32(&b, nir_vec2(&b,
						    nir_channel(&b, &load1->dest.ssa, 0),
						    nir_channel(&b, &load1->dest.ssa, 1)));
	packed64[1] = nir_pack_64_2x32(&b, nir_vec2(&b,
						    nir_channel(&b, &load1->dest.ssa, 2),
						    nir_channel(&b, &load1->dest.ssa, 3)));
	packed64[2] = nir_pack_64_2x32(&b, nir_vec2(&b,
						    nir_channel(&b, &load2->dest.ssa, 0),
						    nir_channel(&b, &load2->dest.ssa, 1)));
	packed64[3] = nir_pack_64_2x32(&b, nir_vec2(&b,
						    nir_channel(&b, &load2->dest.ssa, 2),
						    nir_channel(&b, &load2->dest.ssa, 3)));

	/* Compute result. */
	nir_ssa_def *num_primitive_written =
		nir_isub(&b, packed64[3], packed64[1]);
	nir_ssa_def *primitive_storage_needed =
		nir_isub(&b, packed64[2], packed64[0]);

	nir_store_var(&b, result,
		      nir_vec2(&b, num_primitive_written,
				   primitive_storage_needed), 0x3);
	nir_store_var(&b, available, nir_imm_true(&b), 0x1);

	b.cursor = nir_after_cf_node(&available_if->cf_node);

	/* Determine if result is 64 or 32 bit. */
	nir_ssa_def *result_is_64bit =
		nir_test_flag(&b, flags, VK_QUERY_RESULT_64_BIT);
	nir_ssa_def *result_size =
		nir_bcsel(&b, result_is_64bit, nir_imm_int(&b, 16),
			  nir_imm_int(&b, 8));

	/* Store the result if complete or partial results have been requested. */
	nir_if *store_if = nir_if_create(b.shader);
	store_if->condition =
		nir_src_for_ssa(nir_ior(&b, nir_test_flag(&b, flags, VK_QUERY_RESULT_PARTIAL_BIT),
					nir_load_var(&b, available)));
	nir_cf_node_insert(b.cursor, &store_if->cf_node);

	b.cursor = nir_after_cf_list(&store_if->then_list);

	/* Store result. */
	nir_if *store_64bit_if = nir_if_create(b.shader);
	store_64bit_if->condition = nir_src_for_ssa(result_is_64bit);
	nir_cf_node_insert(b.cursor, &store_64bit_if->cf_node);

	b.cursor = nir_after_cf_list(&store_64bit_if->then_list);

	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_ssbo);
	store->src[0] = nir_src_for_ssa(nir_load_var(&b, result));
	store->src[1] = nir_src_for_ssa(&dst_buf->dest.ssa);
	store->src[2] = nir_src_for_ssa(output_base);
	nir_intrinsic_set_write_mask(store, 0x3);
	store->num_components = 2;
	nir_builder_instr_insert(&b, &store->instr);

	b.cursor = nir_after_cf_list(&store_64bit_if->else_list);

	store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_ssbo);
	store->src[0] = nir_src_for_ssa(nir_u2u32(&b, nir_load_var(&b, result)));
	store->src[1] = nir_src_for_ssa(&dst_buf->dest.ssa);
	store->src[2] = nir_src_for_ssa(output_base);
	nir_intrinsic_set_write_mask(store, 0x3);
	store->num_components = 2;
	nir_builder_instr_insert(&b, &store->instr);

	b.cursor = nir_after_cf_node(&store_64bit_if->cf_node);

	b.cursor = nir_after_cf_node(&store_if->cf_node);

	/* Store the availability bit if requested. */
	nir_if *availability_if = nir_if_create(b.shader);
	availability_if->condition =
		nir_src_for_ssa(nir_test_flag(&b, flags, VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
	nir_cf_node_insert(b.cursor, &availability_if->cf_node);

	b.cursor = nir_after_cf_list(&availability_if->then_list);

	store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_ssbo);
	store->src[0] = nir_src_for_ssa(nir_b2i32(&b, nir_load_var(&b, available)));
	store->src[1] = nir_src_for_ssa(&dst_buf->dest.ssa);
	store->src[2] = nir_src_for_ssa(nir_iadd(&b, result_size, output_base));
	nir_intrinsic_set_write_mask(store, 0x1);
	store->num_components = 1;
	nir_builder_instr_insert(&b, &store->instr);

	b.cursor = nir_after_cf_node(&availability_if->cf_node);

	return b.shader;
}

static VkResult radv_device_init_meta_query_state_internal(struct radv_device *device)
{
	VkResult result;
	struct radv_shader_module occlusion_cs = { .nir = NULL };
	struct radv_shader_module pipeline_statistics_cs = { .nir = NULL };
	struct radv_shader_module tfb_cs = { .nir = NULL };

	mtx_lock(&device->meta_state.mtx);
	if (device->meta_state.query.pipeline_statistics_query_pipeline) {
		mtx_unlock(&device->meta_state.mtx);
		return VK_SUCCESS;
	}
	occlusion_cs.nir = build_occlusion_query_shader(device);
	pipeline_statistics_cs.nir = build_pipeline_statistics_query_shader(device);
	tfb_cs.nir = build_tfb_query_shader(device);

	VkDescriptorSetLayoutCreateInfo occlusion_ds_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
		.bindingCount = 2,
		.pBindings = (VkDescriptorSetLayoutBinding[]) {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = NULL
			},
			{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = NULL
			},
		}
	};

	result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device),
						&occlusion_ds_create_info,
						&device->meta_state.alloc,
						&device->meta_state.query.ds_layout);
	if (result != VK_SUCCESS)
		goto fail;

	VkPipelineLayoutCreateInfo occlusion_pl_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &device->meta_state.query.ds_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 16},
	};

	result = radv_CreatePipelineLayout(radv_device_to_handle(device),
					  &occlusion_pl_create_info,
					  &device->meta_state.alloc,
					  &device->meta_state.query.p_layout);
	if (result != VK_SUCCESS)
		goto fail;

	VkPipelineShaderStageCreateInfo occlusion_pipeline_shader_stage = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = radv_shader_module_to_handle(&occlusion_cs),
		.pName = "main",
		.pSpecializationInfo = NULL,
	};

	VkComputePipelineCreateInfo occlusion_vk_pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = occlusion_pipeline_shader_stage,
		.flags = 0,
		.layout = device->meta_state.query.p_layout,
	};

	result = radv_CreateComputePipelines(radv_device_to_handle(device),
					     radv_pipeline_cache_to_handle(&device->meta_state.cache),
					     1, &occlusion_vk_pipeline_info, NULL,
					     &device->meta_state.query.occlusion_query_pipeline);
	if (result != VK_SUCCESS)
		goto fail;

	VkPipelineShaderStageCreateInfo pipeline_statistics_pipeline_shader_stage = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = radv_shader_module_to_handle(&pipeline_statistics_cs),
		.pName = "main",
		.pSpecializationInfo = NULL,
	};

	VkComputePipelineCreateInfo pipeline_statistics_vk_pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = pipeline_statistics_pipeline_shader_stage,
		.flags = 0,
		.layout = device->meta_state.query.p_layout,
	};

	result = radv_CreateComputePipelines(radv_device_to_handle(device),
					     radv_pipeline_cache_to_handle(&device->meta_state.cache),
					     1, &pipeline_statistics_vk_pipeline_info, NULL,
					     &device->meta_state.query.pipeline_statistics_query_pipeline);
	if (result != VK_SUCCESS)
		goto fail;

	VkPipelineShaderStageCreateInfo tfb_pipeline_shader_stage = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = radv_shader_module_to_handle(&tfb_cs),
		.pName = "main",
		.pSpecializationInfo = NULL,
	};

	VkComputePipelineCreateInfo tfb_pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = tfb_pipeline_shader_stage,
		.flags = 0,
		.layout = device->meta_state.query.p_layout,
	};

	result = radv_CreateComputePipelines(radv_device_to_handle(device),
					     radv_pipeline_cache_to_handle(&device->meta_state.cache),
					     1, &tfb_pipeline_info, NULL,
					     &device->meta_state.query.tfb_query_pipeline);
fail:
	if (result != VK_SUCCESS)
		radv_device_finish_meta_query_state(device);
	ralloc_free(occlusion_cs.nir);
	ralloc_free(pipeline_statistics_cs.nir);
	ralloc_free(tfb_cs.nir);
	mtx_unlock(&device->meta_state.mtx);
	return result;
}

VkResult radv_device_init_meta_query_state(struct radv_device *device, bool on_demand)
{
	if (on_demand)
		return VK_SUCCESS;

	return radv_device_init_meta_query_state_internal(device);
}

void radv_device_finish_meta_query_state(struct radv_device *device)
{
	if (device->meta_state.query.tfb_query_pipeline)
		radv_DestroyPipeline(radv_device_to_handle(device),
				     device->meta_state.query.tfb_query_pipeline,
				     &device->meta_state.alloc);

	if (device->meta_state.query.pipeline_statistics_query_pipeline)
		radv_DestroyPipeline(radv_device_to_handle(device),
				     device->meta_state.query.pipeline_statistics_query_pipeline,
				     &device->meta_state.alloc);

	if (device->meta_state.query.occlusion_query_pipeline)
		radv_DestroyPipeline(radv_device_to_handle(device),
				     device->meta_state.query.occlusion_query_pipeline,
				     &device->meta_state.alloc);

	if (device->meta_state.query.p_layout)
		radv_DestroyPipelineLayout(radv_device_to_handle(device),
					   device->meta_state.query.p_layout,
					   &device->meta_state.alloc);

	if (device->meta_state.query.ds_layout)
		radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
						device->meta_state.query.ds_layout,
						&device->meta_state.alloc);
}

static void radv_query_shader(struct radv_cmd_buffer *cmd_buffer,
                              VkPipeline *pipeline,
                              struct radeon_winsys_bo *src_bo,
                              struct radeon_winsys_bo *dst_bo,
                              uint64_t src_offset, uint64_t dst_offset,
                              uint32_t src_stride, uint32_t dst_stride,
                              uint32_t count, uint32_t flags,
                              uint32_t pipeline_stats_mask, uint32_t avail_offset)
{
	struct radv_device *device = cmd_buffer->device;
	struct radv_meta_saved_state saved_state;
	bool old_predicating;

	if (!*pipeline) {
		VkResult ret = radv_device_init_meta_query_state_internal(device);
		if (ret != VK_SUCCESS) {
			cmd_buffer->record_result = ret;
			return;
		}
	}

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_COMPUTE_PIPELINE |
		       RADV_META_SAVE_CONSTANTS |
		       RADV_META_SAVE_DESCRIPTORS);

	/* VK_EXT_conditional_rendering says that copy commands should not be
	 * affected by conditional rendering.
	 */
	old_predicating = cmd_buffer->state.predicating;
	cmd_buffer->state.predicating = false;

	struct radv_buffer dst_buffer = {
		.bo = dst_bo,
		.offset = dst_offset,
		.size = dst_stride * count
	};

	struct radv_buffer src_buffer = {
		.bo = src_bo,
		.offset = src_offset,
		.size = MAX2(src_stride * count, avail_offset + 4 * count - src_offset)
	};

	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
			     VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline);

	radv_meta_push_descriptor_set(cmd_buffer,
				      VK_PIPELINE_BIND_POINT_COMPUTE,
				      device->meta_state.query.p_layout,
				      0, /* set */
				      2, /* descriptorWriteCount */
				      (VkWriteDescriptorSet[]) {
				              {
				                      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				                      .dstBinding = 0,
				                      .dstArrayElement = 0,
				                      .descriptorCount = 1,
				                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				                      .pBufferInfo = &(VkDescriptorBufferInfo) {
				                              .buffer = radv_buffer_to_handle(&dst_buffer),
				                              .offset = 0,
				                              .range = VK_WHOLE_SIZE
				                      }
				              },
				              {
				                      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				                      .dstBinding = 1,
				                      .dstArrayElement = 0,
				                      .descriptorCount = 1,
				                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				                      .pBufferInfo = &(VkDescriptorBufferInfo) {
				                              .buffer = radv_buffer_to_handle(&src_buffer),
				                              .offset = 0,
				                              .range = VK_WHOLE_SIZE
				                      }
				              }
				      });

	/* Encode the number of elements for easy access by the shader. */
	pipeline_stats_mask &= 0x7ff;
	pipeline_stats_mask |= util_bitcount(pipeline_stats_mask) << 16;

	avail_offset -= src_offset;

	struct {
		uint32_t flags;
		uint32_t dst_stride;
		uint32_t pipeline_stats_mask;
		uint32_t avail_offset;
	} push_constants = {
		flags,
		dst_stride,
		pipeline_stats_mask,
		avail_offset
	};

	radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
				      device->meta_state.query.p_layout,
				      VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants),
				      &push_constants);

	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_INV_GLOBAL_L2 |
	                                RADV_CMD_FLAG_INV_VMEM_L1;

	if (flags & VK_QUERY_RESULT_WAIT_BIT)
		cmd_buffer->state.flush_bits |= RADV_CMD_FLUSH_AND_INV_FRAMEBUFFER;

	radv_unaligned_dispatch(cmd_buffer, count, 1, 1);

	/* Restore conditional rendering. */
	cmd_buffer->state.predicating = old_predicating;

	radv_meta_restore(&saved_state, cmd_buffer);
}

VkResult radv_CreateQueryPool(
	VkDevice                                    _device,
	const VkQueryPoolCreateInfo*                pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkQueryPool*                                pQueryPool)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_query_pool *pool = vk_alloc2(&device->alloc, pAllocator,
					       sizeof(*pool), 8,
					       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	uint32_t initial_value = pCreateInfo->queryType == VK_QUERY_TYPE_TIMESTAMP
				 ? TIMESTAMP_NOT_READY : 0;

	if (!pool)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);


	switch(pCreateInfo->queryType) {
	case VK_QUERY_TYPE_OCCLUSION:
		pool->stride = 16 * get_max_db(device);
		break;
	case VK_QUERY_TYPE_PIPELINE_STATISTICS:
		pool->stride = pipelinestat_block_size * 2;
		break;
	case VK_QUERY_TYPE_TIMESTAMP:
		pool->stride = 8;
		break;
	case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
		pool->stride = 32;
		break;
	default:
		unreachable("creating unhandled query type");
	}

	pool->type = pCreateInfo->queryType;
	pool->pipeline_stats_mask = pCreateInfo->pipelineStatistics;
	pool->availability_offset = pool->stride * pCreateInfo->queryCount;
	pool->size = pool->availability_offset;
	if (pCreateInfo->queryType == VK_QUERY_TYPE_PIPELINE_STATISTICS)
		pool->size += 4 * pCreateInfo->queryCount;

	pool->bo = device->ws->buffer_create(device->ws, pool->size,
					     64, RADEON_DOMAIN_GTT, RADEON_FLAG_NO_INTERPROCESS_SHARING,
					     RADV_BO_PRIORITY_QUERY_POOL);

	if (!pool->bo) {
		vk_free2(&device->alloc, pAllocator, pool);
		return vk_error(device->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);
	}

	pool->ptr = device->ws->buffer_map(pool->bo);

	if (!pool->ptr) {
		device->ws->buffer_destroy(pool->bo);
		vk_free2(&device->alloc, pAllocator, pool);
		return vk_error(device->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);
	}
	memset(pool->ptr, initial_value, pool->size);

	*pQueryPool = radv_query_pool_to_handle(pool);
	return VK_SUCCESS;
}

void radv_DestroyQueryPool(
	VkDevice                                    _device,
	VkQueryPool                                 _pool,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_query_pool, pool, _pool);

	if (!pool)
		return;

	device->ws->buffer_destroy(pool->bo);
	vk_free2(&device->alloc, pAllocator, pool);
}

VkResult radv_GetQueryPoolResults(
	VkDevice                                    _device,
	VkQueryPool                                 queryPool,
	uint32_t                                    firstQuery,
	uint32_t                                    queryCount,
	size_t                                      dataSize,
	void*                                       pData,
	VkDeviceSize                                stride,
	VkQueryResultFlags                          flags)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
	char *data = pData;
	VkResult result = VK_SUCCESS;

	for(unsigned i = 0; i < queryCount; ++i, data += stride) {
		char *dest = data;
		unsigned query = firstQuery + i;
		char *src = pool->ptr + query * pool->stride;
		uint32_t available;

		if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
			if (flags & VK_QUERY_RESULT_WAIT_BIT)
				while(!*(volatile uint32_t*)(pool->ptr + pool->availability_offset + 4 * query))
					;
			available = *(uint32_t*)(pool->ptr + pool->availability_offset + 4 * query);
		}

		switch (pool->type) {
		case VK_QUERY_TYPE_TIMESTAMP: {
			available = *(uint64_t *)src != TIMESTAMP_NOT_READY;

			if (flags & VK_QUERY_RESULT_WAIT_BIT) {
				while (*(volatile uint64_t *)src == TIMESTAMP_NOT_READY)
					;
				available = *(uint64_t *)src != TIMESTAMP_NOT_READY;
			}

			if (!available && !(flags & VK_QUERY_RESULT_PARTIAL_BIT))
				result = VK_NOT_READY;

			if (flags & VK_QUERY_RESULT_64_BIT) {
				if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT))
					*(uint64_t*)dest = *(uint64_t*)src;
				dest += 8;
			} else {
				if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT))
					*(uint32_t*)dest = *(uint32_t*)src;
				dest += 4;
			}
			break;
		}
		case VK_QUERY_TYPE_OCCLUSION: {
			volatile uint64_t const *src64 = (volatile uint64_t const *)src;
			uint64_t sample_count = 0;
			int db_count = get_max_db(device);
			available = 1;

			for (int i = 0; i < db_count; ++i) {
				uint64_t start, end;
				do {
					start = src64[2 * i];
					end = src64[2 * i + 1];
				} while ((!(start & (1ull << 63)) || !(end & (1ull << 63))) && (flags & VK_QUERY_RESULT_WAIT_BIT));

				if (!(start & (1ull << 63)) || !(end & (1ull << 63)))
					available = 0;
				else {
					sample_count += end - start;
				}
			}

			if (!available && !(flags & VK_QUERY_RESULT_PARTIAL_BIT))
				result = VK_NOT_READY;

			if (flags & VK_QUERY_RESULT_64_BIT) {
				if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT))
					*(uint64_t*)dest = sample_count;
				dest += 8;
			} else {
				if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT))
					*(uint32_t*)dest = sample_count;
				dest += 4;
			}
			break;
		}
		case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
			if (!available && !(flags & VK_QUERY_RESULT_PARTIAL_BIT))
				result = VK_NOT_READY;

			const uint64_t *start = (uint64_t*)src;
			const uint64_t *stop = (uint64_t*)(src + pipelinestat_block_size);
			if (flags & VK_QUERY_RESULT_64_BIT) {
				uint64_t *dst = (uint64_t*)dest;
				dest += util_bitcount(pool->pipeline_stats_mask) * 8;
				for(int i = 0; i < 11; ++i) {
					if(pool->pipeline_stats_mask & (1u << i)) {
						if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT))
							*dst = stop[pipeline_statistics_indices[i]] -
							       start[pipeline_statistics_indices[i]];
						dst++;
					}
				}

			} else {
				uint32_t *dst = (uint32_t*)dest;
				dest += util_bitcount(pool->pipeline_stats_mask) * 4;
				for(int i = 0; i < 11; ++i) {
					if(pool->pipeline_stats_mask & (1u << i)) {
						if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT))
							*dst = stop[pipeline_statistics_indices[i]] -
							       start[pipeline_statistics_indices[i]];
						dst++;
					}
				}
			}
			break;
		}
		case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT: {
			volatile uint64_t const *src64 = (volatile uint64_t const *)src;
			uint64_t num_primitives_written;
			uint64_t primitive_storage_needed;

			/* SAMPLE_STREAMOUTSTATS stores this structure:
			 * {
			 *	u64 NumPrimitivesWritten;
			 *	u64 PrimitiveStorageNeeded;
			 * }
			 */
			available = 1;
			for (int j = 0; j < 4; j++) {
				if (!(src64[j] & 0x8000000000000000UL))
					available = 0;
			}

			if (!available && !(flags & VK_QUERY_RESULT_PARTIAL_BIT))
				result = VK_NOT_READY;

			num_primitives_written = src64[3] - src64[1];
			primitive_storage_needed = src64[2] - src64[0];

			if (flags & VK_QUERY_RESULT_64_BIT) {
				if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT))
					*(uint64_t *)dest = num_primitives_written;
				dest += 8;
				if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT))
					*(uint64_t *)dest = primitive_storage_needed;
				dest += 8;
			} else {
				if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT))
					*(uint32_t *)dest = num_primitives_written;
				dest += 4;
				if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT))
					*(uint32_t *)dest = primitive_storage_needed;
				dest += 4;
			}
			break;
		}
		default:
			unreachable("trying to get results of unhandled query type");
		}

		if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
			if (flags & VK_QUERY_RESULT_64_BIT) {
				*(uint64_t*)dest = available;
			} else {
				*(uint32_t*)dest = available;
			}
		}
	}

	return result;
}

void radv_CmdCopyQueryPoolResults(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                stride,
    VkQueryResultFlags                          flags)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
	RADV_FROM_HANDLE(radv_buffer, dst_buffer, dstBuffer);
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	unsigned elem_size = (flags & VK_QUERY_RESULT_64_BIT) ? 8 : 4;
	uint64_t va = radv_buffer_get_va(pool->bo);
	uint64_t dest_va = radv_buffer_get_va(dst_buffer->bo);
	dest_va += dst_buffer->offset + dstOffset;

	radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, pool->bo);
	radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, dst_buffer->bo);

	switch (pool->type) {
	case VK_QUERY_TYPE_OCCLUSION:
		if (flags & VK_QUERY_RESULT_WAIT_BIT) {
			for(unsigned i = 0; i < queryCount; ++i, dest_va += stride) {
				unsigned query = firstQuery + i;
				uint64_t src_va = va + query * pool->stride + pool->stride - 4;

				radeon_check_space(cmd_buffer->device->ws, cs, 7);

				/* Waits on the upper word of the last DB entry */
				radv_cp_wait_mem(cs, WAIT_REG_MEM_GREATER_OR_EQUAL,
						 src_va, 0x80000000, 0xffffffff);
			}
		}
		radv_query_shader(cmd_buffer, &cmd_buffer->device->meta_state.query.occlusion_query_pipeline,
		                  pool->bo, dst_buffer->bo, firstQuery * pool->stride,
		                  dst_buffer->offset + dstOffset,
		                  pool->stride, stride,
		                  queryCount, flags, 0, 0);
		break;
	case VK_QUERY_TYPE_PIPELINE_STATISTICS:
		if (flags & VK_QUERY_RESULT_WAIT_BIT) {
			for(unsigned i = 0; i < queryCount; ++i, dest_va += stride) {
				unsigned query = firstQuery + i;

				radeon_check_space(cmd_buffer->device->ws, cs, 7);

				uint64_t avail_va = va + pool->availability_offset + 4 * query;

				/* This waits on the ME. All copies below are done on the ME */
				radv_cp_wait_mem(cs, WAIT_REG_MEM_EQUAL,
						 avail_va, 1, 0xffffffff);
			}
		}
		radv_query_shader(cmd_buffer, &cmd_buffer->device->meta_state.query.pipeline_statistics_query_pipeline,
		                  pool->bo, dst_buffer->bo, firstQuery * pool->stride,
		                  dst_buffer->offset + dstOffset,
		                  pool->stride, stride, queryCount, flags,
		                  pool->pipeline_stats_mask,
		                  pool->availability_offset + 4 * firstQuery);
		break;
	case VK_QUERY_TYPE_TIMESTAMP:
		for(unsigned i = 0; i < queryCount; ++i, dest_va += stride) {
			unsigned query = firstQuery + i;
			uint64_t local_src_va = va  + query * pool->stride;

			MAYBE_UNUSED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cs, 19);


			if (flags & VK_QUERY_RESULT_WAIT_BIT) {
				/* Wait on the high 32 bits of the timestamp in
				 * case the low part is 0xffffffff.
				 */
				radv_cp_wait_mem(cs, WAIT_REG_MEM_NOT_EQUAL,
						 local_src_va + 4,
						 TIMESTAMP_NOT_READY >> 32,
						 0xffffffff);
			}
			if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
				uint64_t avail_dest_va = dest_va + elem_size;

				radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
				radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_SRC_MEM) |
						COPY_DATA_DST_SEL(COPY_DATA_DST_MEM_GRBM));
				radeon_emit(cs, local_src_va);
				radeon_emit(cs, local_src_va >> 32);
				radeon_emit(cs, avail_dest_va);
				radeon_emit(cs, avail_dest_va >> 32);
			}

			radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
			radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_SRC_MEM) |
					COPY_DATA_DST_SEL(COPY_DATA_DST_MEM_GRBM) |
					((flags & VK_QUERY_RESULT_64_BIT) ? COPY_DATA_COUNT_SEL : 0));
			radeon_emit(cs, local_src_va);
			radeon_emit(cs, local_src_va >> 32);
			radeon_emit(cs, dest_va);
			radeon_emit(cs, dest_va >> 32);


			assert(cs->cdw <= cdw_max);
		}
		break;
	case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
		if (flags & VK_QUERY_RESULT_WAIT_BIT) {
			for(unsigned i = 0; i < queryCount; i++) {
				unsigned query = firstQuery + i;
				uint64_t src_va = va + query * pool->stride;

				radeon_check_space(cmd_buffer->device->ws, cs, 7 * 4);

				/* Wait on the upper word of all results. */
				for (unsigned j = 0; j < 4; j++, src_va += 8) {
					radv_cp_wait_mem(cs, WAIT_REG_MEM_GREATER_OR_EQUAL,
							 src_va + 4, 0x80000000,
							 0xffffffff);
				}
			}
		}

		radv_query_shader(cmd_buffer, &cmd_buffer->device->meta_state.query.tfb_query_pipeline,
		                  pool->bo, dst_buffer->bo,
				  firstQuery * pool->stride,
		                  dst_buffer->offset + dstOffset,
		                  pool->stride, stride,
				  queryCount, flags, 0, 0);
		break;
	default:
		unreachable("trying to get results of unhandled query type");
	}

}

void radv_CmdResetQueryPool(
	VkCommandBuffer                             commandBuffer,
	VkQueryPool                                 queryPool,
	uint32_t                                    firstQuery,
	uint32_t                                    queryCount)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
	uint32_t value = pool->type == VK_QUERY_TYPE_TIMESTAMP
			 ? TIMESTAMP_NOT_READY : 0;
	uint32_t flush_bits = 0;

	flush_bits |= radv_fill_buffer(cmd_buffer, pool->bo,
				       firstQuery * pool->stride,
				       queryCount * pool->stride, value);

	if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
		flush_bits |= radv_fill_buffer(cmd_buffer, pool->bo,
					       pool->availability_offset + firstQuery * 4,
					       queryCount * 4, 0);
	}

	if (flush_bits) {
		/* Only need to flush caches for the compute shader path. */
		cmd_buffer->pending_reset_query = true;
		cmd_buffer->state.flush_bits |= flush_bits;
	}
}

void radv_ResetQueryPoolEXT(
	VkDevice                                   _device,
	VkQueryPool                                 queryPool,
	uint32_t                                    firstQuery,
	uint32_t                                    queryCount)
{
	RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);

	uint32_t value = pool->type == VK_QUERY_TYPE_TIMESTAMP
			 ? TIMESTAMP_NOT_READY : 0;
	uint32_t *data =  (uint32_t*)(pool->ptr + firstQuery * pool->stride);
	uint32_t *data_end = (uint32_t*)(pool->ptr + (firstQuery + queryCount) * pool->stride);

	for(uint32_t *p = data; p != data_end; ++p)
		*p = value;

	if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
		memset(pool->ptr + pool->availability_offset + firstQuery * 4,
		       0, queryCount * 4);
	}
}

static unsigned event_type_for_stream(unsigned stream)
{
	switch (stream) {
	default:
	case 0: return V_028A90_SAMPLE_STREAMOUTSTATS;
	case 1: return V_028A90_SAMPLE_STREAMOUTSTATS1;
	case 2: return V_028A90_SAMPLE_STREAMOUTSTATS2;
	case 3: return V_028A90_SAMPLE_STREAMOUTSTATS3;
	}
}

static void emit_query_flush(struct radv_cmd_buffer *cmd_buffer,
			     struct radv_query_pool *pool)
{
	if (cmd_buffer->pending_reset_query) {
		if (pool->size >= RADV_BUFFER_OPS_CS_THRESHOLD) {
			/* Only need to flush caches if the query pool size is
			 * large enough to be resetted using the compute shader
			 * path. Small pools don't need any cache flushes
			 * because we use a CP dma clear.
			 */
			si_emit_cache_flush(cmd_buffer);
		}
	}
}

static void emit_begin_query(struct radv_cmd_buffer *cmd_buffer,
			     uint64_t va,
			     VkQueryType query_type,
			     VkQueryControlFlags flags,
			     uint32_t index)
{
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	switch (query_type) {
	case VK_QUERY_TYPE_OCCLUSION:
		radeon_check_space(cmd_buffer->device->ws, cs, 7);

		++cmd_buffer->state.active_occlusion_queries;
		if (cmd_buffer->state.active_occlusion_queries == 1) {
			if (flags & VK_QUERY_CONTROL_PRECISE_BIT) {
				/* This is the first occlusion query, enable
				 * the hint if the precision bit is set.
				 */
				cmd_buffer->state.perfect_occlusion_queries_enabled = true;
			}

			radv_set_db_count_control(cmd_buffer);
		} else {
			if ((flags & VK_QUERY_CONTROL_PRECISE_BIT) &&
			    !cmd_buffer->state.perfect_occlusion_queries_enabled) {
				/* This is not the first query, but this one
				 * needs to enable precision, DB_COUNT_CONTROL
				 * has to be updated accordingly.
				 */
				cmd_buffer->state.perfect_occlusion_queries_enabled = true;

				radv_set_db_count_control(cmd_buffer);
			}
		}

		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_ZPASS_DONE) | EVENT_INDEX(1));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		break;
	case VK_QUERY_TYPE_PIPELINE_STATISTICS:
		radeon_check_space(cmd_buffer->device->ws, cs, 4);

		++cmd_buffer->state.active_pipeline_queries;
		if (cmd_buffer->state.active_pipeline_queries == 1) {
			cmd_buffer->state.flush_bits &= ~RADV_CMD_FLAG_STOP_PIPELINE_STATS;
			cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_START_PIPELINE_STATS;
		}

		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_SAMPLE_PIPELINESTAT) | EVENT_INDEX(2));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		break;
	case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
		radeon_check_space(cmd_buffer->device->ws, cs, 4);

		assert(index < MAX_SO_STREAMS);

		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(event_type_for_stream(index)) | EVENT_INDEX(3));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		break;
	default:
		unreachable("beginning unhandled query type");
	}

}

static void emit_end_query(struct radv_cmd_buffer *cmd_buffer,
			   uint64_t va, uint64_t avail_va,
			   VkQueryType query_type, uint32_t index)
{
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	switch (query_type) {
	case VK_QUERY_TYPE_OCCLUSION:
		radeon_check_space(cmd_buffer->device->ws, cs, 14);

		cmd_buffer->state.active_occlusion_queries--;
		if (cmd_buffer->state.active_occlusion_queries == 0) {
			radv_set_db_count_control(cmd_buffer);

			/* Reset the perfect occlusion queries hint now that no
			 * queries are active.
			 */
			cmd_buffer->state.perfect_occlusion_queries_enabled = false;
		}

		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_ZPASS_DONE) | EVENT_INDEX(1));
		radeon_emit(cs, va + 8);
		radeon_emit(cs, (va + 8) >> 32);

		break;
	case VK_QUERY_TYPE_PIPELINE_STATISTICS:
		radeon_check_space(cmd_buffer->device->ws, cs, 16);

		cmd_buffer->state.active_pipeline_queries--;
		if (cmd_buffer->state.active_pipeline_queries == 0) {
			cmd_buffer->state.flush_bits &= ~RADV_CMD_FLAG_START_PIPELINE_STATS;
			cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_STOP_PIPELINE_STATS;
		}
		va += pipelinestat_block_size;

		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_SAMPLE_PIPELINESTAT) | EVENT_INDEX(2));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);

		si_cs_emit_write_event_eop(cs,
					   cmd_buffer->device->physical_device->rad_info.chip_class,
					   radv_cmd_buffer_uses_mec(cmd_buffer),
					   V_028A90_BOTTOM_OF_PIPE_TS, 0,
					   EOP_DATA_SEL_VALUE_32BIT,
					   avail_va, 1,
					   cmd_buffer->gfx9_eop_bug_va);
		break;
	case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
		radeon_check_space(cmd_buffer->device->ws, cs, 4);

		assert(index < MAX_SO_STREAMS);

		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(event_type_for_stream(index)) | EVENT_INDEX(3));
		radeon_emit(cs, (va + 16));
		radeon_emit(cs, (va + 16) >> 32);
		break;
	default:
		unreachable("ending unhandled query type");
	}
}

void radv_CmdBeginQueryIndexedEXT(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags,
    uint32_t                                    index)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	uint64_t va = radv_buffer_get_va(pool->bo);

	radv_cs_add_buffer(cmd_buffer->device->ws, cs, pool->bo);

	emit_query_flush(cmd_buffer, pool);

	va += pool->stride * query;

	emit_begin_query(cmd_buffer, va, pool->type, flags, index);
}

void radv_CmdBeginQuery(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags)
{
	radv_CmdBeginQueryIndexedEXT(commandBuffer, queryPool, query, flags, 0);
}

void radv_CmdEndQueryIndexedEXT(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    uint32_t                                    index)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
	uint64_t va = radv_buffer_get_va(pool->bo);
	uint64_t avail_va = va + pool->availability_offset + 4 * query;
	va += pool->stride * query;

	/* Do not need to add the pool BO to the list because the query must
	 * currently be active, which means the BO is already in the list.
	 */
	emit_end_query(cmd_buffer, va, avail_va, pool->type, index);

	/*
	 * For multiview we have to emit a query for each bit in the mask,
	 * however the first query we emit will get the totals for all the
	 * operations, so we don't want to get a real value in the other
	 * queries. This emits a fake begin/end sequence so the waiting
	 * code gets a completed query value and doesn't hang, but the
	 * query returns 0.
	 */
	if (cmd_buffer->state.subpass && cmd_buffer->state.subpass->view_mask) {
		uint64_t avail_va = va + pool->availability_offset + 4 * query;


		for (unsigned i = 1; i < util_bitcount(cmd_buffer->state.subpass->view_mask); i++) {
			va += pool->stride;
			avail_va += 4;
			emit_begin_query(cmd_buffer, va, pool->type, 0, 0);
			emit_end_query(cmd_buffer, va, avail_va, pool->type, 0);
		}
	}
}

void radv_CmdEndQuery(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
	radv_CmdEndQueryIndexedEXT(commandBuffer, queryPool, query, 0);
}

void radv_CmdWriteTimestamp(
    VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlagBits                     pipelineStage,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
	bool mec = radv_cmd_buffer_uses_mec(cmd_buffer);
	struct radeon_cmdbuf *cs = cmd_buffer->cs;
	uint64_t va = radv_buffer_get_va(pool->bo);
	uint64_t query_va = va + pool->stride * query;

	radv_cs_add_buffer(cmd_buffer->device->ws, cs, pool->bo);

	emit_query_flush(cmd_buffer, pool);

	int num_queries = 1;
	if (cmd_buffer->state.subpass && cmd_buffer->state.subpass->view_mask)
		num_queries = util_bitcount(cmd_buffer->state.subpass->view_mask);

	MAYBE_UNUSED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cs, 28 * num_queries);

	for (unsigned i = 0; i < num_queries; i++) {
		switch(pipelineStage) {
		case VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:
			radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
			radeon_emit(cs, COPY_DATA_COUNT_SEL | COPY_DATA_WR_CONFIRM |
				    COPY_DATA_SRC_SEL(COPY_DATA_TIMESTAMP) |
				    COPY_DATA_DST_SEL(V_370_MEM));
			radeon_emit(cs, 0);
			radeon_emit(cs, 0);
			radeon_emit(cs, query_va);
			radeon_emit(cs, query_va >> 32);
			break;
		default:
			si_cs_emit_write_event_eop(cs,
						   cmd_buffer->device->physical_device->rad_info.chip_class,
						   mec,
						   V_028A90_BOTTOM_OF_PIPE_TS, 0,
						   EOP_DATA_SEL_TIMESTAMP,
						   query_va, 0,
						   cmd_buffer->gfx9_eop_bug_va);
			break;
		}
		query_va += pool->stride;
	}
	assert(cmd_buffer->cs->cdw <= cdw_max);
}
