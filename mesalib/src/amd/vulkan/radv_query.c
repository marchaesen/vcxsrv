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
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>

#include "nir/nir_builder.h"
#include "util/u_atomic.h"
#include "radv_acceleration_structure.h"
#include "radv_cs.h"
#include "radv_meta.h"
#include "radv_private.h"
#include "sid.h"

#define TIMESTAMP_NOT_READY UINT64_MAX

static const int pipelinestat_block_size = 11 * 8;
static const unsigned pipeline_statistics_indices[] = {7, 6, 3, 4, 5, 2, 1, 0, 8, 9, 10};

static unsigned
radv_get_pipeline_statistics_index(const VkQueryPipelineStatisticFlagBits flag)
{
   int offset = ffs(flag) - 1;
   assert(offset < ARRAY_SIZE(pipeline_statistics_indices));
   return pipeline_statistics_indices[offset];
}

static nir_ssa_def *
nir_test_flag(nir_builder *b, nir_ssa_def *flags, uint32_t flag)
{
   return nir_i2b(b, nir_iand(b, flags, nir_imm_int(b, flag)));
}

static void
radv_store_availability(nir_builder *b, nir_ssa_def *flags, nir_ssa_def *dst_buf,
                        nir_ssa_def *offset, nir_ssa_def *value32)
{
   nir_push_if(b, nir_test_flag(b, flags, VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));

   nir_push_if(b, nir_test_flag(b, flags, VK_QUERY_RESULT_64_BIT));

   nir_store_ssbo(b, nir_vec2(b, value32, nir_imm_int(b, 0)), dst_buf, offset, .align_mul = 8);

   nir_push_else(b, NULL);

   nir_store_ssbo(b, value32, dst_buf, offset);

   nir_pop_if(b, NULL);

   nir_pop_if(b, NULL);
}

static nir_shader *
build_occlusion_query_shader(struct radv_device *device)
{
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
    *		if (enabled_rb_mask & (1 << i)) {
    *			uint64_t start = src_buf[src_offset + 16 * i];
    *			uint64_t end = src_buf[src_offset + 16 * i + 8];
    *			if ((start & (1ull << 63)) && (end & (1ull << 63)))
    *				result += end - start;
    *			else
    *				available = false;
    *		}
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
   nir_builder b = radv_meta_init_shader(MESA_SHADER_COMPUTE, "occlusion_query");
   b.shader->info.workgroup_size[0] = 64;

   nir_variable *result = nir_local_variable_create(b.impl, glsl_uint64_t_type(), "result");
   nir_variable *outer_counter =
      nir_local_variable_create(b.impl, glsl_int_type(), "outer_counter");
   nir_variable *start = nir_local_variable_create(b.impl, glsl_uint64_t_type(), "start");
   nir_variable *end = nir_local_variable_create(b.impl, glsl_uint64_t_type(), "end");
   nir_variable *available = nir_local_variable_create(b.impl, glsl_bool_type(), "available");
   unsigned enabled_rb_mask = device->physical_device->rad_info.enabled_rb_mask;
   unsigned db_count = device->physical_device->rad_info.max_render_backends;

   nir_ssa_def *flags = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 0), .range = 16);

   nir_ssa_def *dst_buf = radv_meta_load_descriptor(&b, 0, 0);
   nir_ssa_def *src_buf = radv_meta_load_descriptor(&b, 0, 1);

   nir_ssa_def *global_id = get_global_ids(&b, 1);

   nir_ssa_def *input_stride = nir_imm_int(&b, db_count * 16);
   nir_ssa_def *input_base = nir_imul(&b, input_stride, global_id);
   nir_ssa_def *output_stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 4), .range = 16);
   nir_ssa_def *output_base = nir_imul(&b, output_stride, global_id);

   nir_store_var(&b, result, nir_imm_int64(&b, 0), 0x1);
   nir_store_var(&b, outer_counter, nir_imm_int(&b, 0), 0x1);
   nir_store_var(&b, available, nir_imm_true(&b), 0x1);

   nir_push_loop(&b);

   nir_ssa_def *current_outer_count = nir_load_var(&b, outer_counter);
   radv_break_on_count(&b, outer_counter, nir_imm_int(&b, db_count));

   nir_ssa_def *enabled_cond = nir_iand(&b, nir_imm_int(&b, enabled_rb_mask),
                                        nir_ishl(&b, nir_imm_int(&b, 1), current_outer_count));

   nir_push_if(&b, nir_i2b(&b, enabled_cond));

   nir_ssa_def *load_offset = nir_imul(&b, current_outer_count, nir_imm_int(&b, 16));
   load_offset = nir_iadd(&b, input_base, load_offset);

   nir_ssa_def *load = nir_load_ssbo(&b, 2, 64, src_buf, load_offset, .align_mul = 16);

   nir_store_var(&b, start, nir_channel(&b, load, 0), 0x1);
   nir_store_var(&b, end, nir_channel(&b, load, 1), 0x1);

   nir_ssa_def *start_done = nir_ilt(&b, nir_load_var(&b, start), nir_imm_int64(&b, 0));
   nir_ssa_def *end_done = nir_ilt(&b, nir_load_var(&b, end), nir_imm_int64(&b, 0));

   nir_push_if(&b, nir_iand(&b, start_done, end_done));

   nir_store_var(&b, result,
                 nir_iadd(&b, nir_load_var(&b, result),
                          nir_isub(&b, nir_load_var(&b, end), nir_load_var(&b, start))),
                 0x1);

   nir_push_else(&b, NULL);

   nir_store_var(&b, available, nir_imm_false(&b), 0x1);

   nir_pop_if(&b, NULL);
   nir_pop_if(&b, NULL);
   nir_pop_loop(&b, NULL);

   /* Store the result if complete or if partial results have been requested. */

   nir_ssa_def *result_is_64bit = nir_test_flag(&b, flags, VK_QUERY_RESULT_64_BIT);
   nir_ssa_def *result_size =
      nir_bcsel(&b, result_is_64bit, nir_imm_int(&b, 8), nir_imm_int(&b, 4));
   nir_push_if(&b, nir_ior(&b, nir_test_flag(&b, flags, VK_QUERY_RESULT_PARTIAL_BIT),
                           nir_load_var(&b, available)));

   nir_push_if(&b, result_is_64bit);

   nir_store_ssbo(&b, nir_load_var(&b, result), dst_buf, output_base, .align_mul = 8);

   nir_push_else(&b, NULL);

   nir_store_ssbo(&b, nir_u2u32(&b, nir_load_var(&b, result)), dst_buf, output_base,
                  .align_mul = 8);

   nir_pop_if(&b, NULL);
   nir_pop_if(&b, NULL);

   radv_store_availability(&b, flags, dst_buf, nir_iadd(&b, result_size, output_base),
                           nir_b2i32(&b, nir_load_var(&b, available)));

   return b.shader;
}

static nir_shader *
build_pipeline_statistics_query_shader(struct radv_device *device)
{
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
    * 			uint64_t end = src_buf[src_offset + 8 * indices[0] +
    * pipelinestat_block_size]; uint64_t result = end - start; if (flags & VK_QUERY_RESULT_64_BIT)
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
   nir_builder b = radv_meta_init_shader(MESA_SHADER_COMPUTE, "pipeline_statistics_query");
   b.shader->info.workgroup_size[0] = 64;

   nir_variable *output_offset =
      nir_local_variable_create(b.impl, glsl_int_type(), "output_offset");

   nir_ssa_def *flags = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 0), .range = 16);
   nir_ssa_def *stats_mask = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 8), .range = 16);
   nir_ssa_def *avail_offset = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 12), .range = 16);

   nir_ssa_def *dst_buf = radv_meta_load_descriptor(&b, 0, 0);
   nir_ssa_def *src_buf = radv_meta_load_descriptor(&b, 0, 1);

   nir_ssa_def *global_id = get_global_ids(&b, 1);

   nir_ssa_def *input_stride = nir_imm_int(&b, pipelinestat_block_size * 2);
   nir_ssa_def *input_base = nir_imul(&b, input_stride, global_id);
   nir_ssa_def *output_stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 4), .range = 16);
   nir_ssa_def *output_base = nir_imul(&b, output_stride, global_id);

   avail_offset = nir_iadd(&b, avail_offset, nir_imul(&b, global_id, nir_imm_int(&b, 4)));

   nir_ssa_def *available32 = nir_load_ssbo(&b, 1, 32, src_buf, avail_offset);

   nir_ssa_def *result_is_64bit = nir_test_flag(&b, flags, VK_QUERY_RESULT_64_BIT);
   nir_ssa_def *elem_size = nir_bcsel(&b, result_is_64bit, nir_imm_int(&b, 8), nir_imm_int(&b, 4));
   nir_ssa_def *elem_count = nir_ushr(&b, stats_mask, nir_imm_int(&b, 16));

   radv_store_availability(&b, flags, dst_buf,
                           nir_iadd(&b, output_base, nir_imul(&b, elem_count, elem_size)),
                           available32);

   nir_push_if(&b, nir_i2b(&b, available32));

   nir_store_var(&b, output_offset, output_base, 0x1);
   for (int i = 0; i < ARRAY_SIZE(pipeline_statistics_indices); ++i) {
      nir_push_if(&b, nir_test_flag(&b, stats_mask, 1u << i));

      nir_ssa_def *start_offset =
         nir_iadd(&b, input_base, nir_imm_int(&b, pipeline_statistics_indices[i] * 8));
      nir_ssa_def *start = nir_load_ssbo(&b, 1, 64, src_buf, start_offset);

      nir_ssa_def *end_offset =
         nir_iadd(&b, input_base,
                  nir_imm_int(&b, pipeline_statistics_indices[i] * 8 + pipelinestat_block_size));
      nir_ssa_def *end = nir_load_ssbo(&b, 1, 64, src_buf, end_offset);

      nir_ssa_def *result = nir_isub(&b, end, start);

      /* Store result */
      nir_push_if(&b, result_is_64bit);

      nir_store_ssbo(&b, result, dst_buf, nir_load_var(&b, output_offset));

      nir_push_else(&b, NULL);

      nir_store_ssbo(&b, nir_u2u32(&b, result), dst_buf, nir_load_var(&b, output_offset));

      nir_pop_if(&b, NULL);

      nir_store_var(&b, output_offset, nir_iadd(&b, nir_load_var(&b, output_offset), elem_size),
                    0x1);

      nir_pop_if(&b, NULL);
   }

   nir_push_else(&b, NULL); /* nir_i2b(&b, available32) */

   nir_push_if(&b, nir_test_flag(&b, flags, VK_QUERY_RESULT_PARTIAL_BIT));

   /* Stores zeros in all outputs. */

   nir_variable *counter = nir_local_variable_create(b.impl, glsl_int_type(), "counter");
   nir_store_var(&b, counter, nir_imm_int(&b, 0), 0x1);

   nir_loop *loop = nir_push_loop(&b);

   nir_ssa_def *current_counter = nir_load_var(&b, counter);
   radv_break_on_count(&b, counter, elem_count);

   nir_ssa_def *output_elem = nir_iadd(&b, output_base, nir_imul(&b, elem_size, current_counter));
   nir_push_if(&b, result_is_64bit);

   nir_store_ssbo(&b, nir_imm_int64(&b, 0), dst_buf, output_elem);

   nir_push_else(&b, NULL);

   nir_store_ssbo(&b, nir_imm_int(&b, 0), dst_buf, output_elem);

   nir_pop_if(&b, NULL);

   nir_pop_loop(&b, loop);
   nir_pop_if(&b, NULL); /* VK_QUERY_RESULT_PARTIAL_BIT */
   nir_pop_if(&b, NULL); /* nir_i2b(&b, available32) */
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
   nir_builder b = radv_meta_init_shader(MESA_SHADER_COMPUTE, "tfb_query");
   b.shader->info.workgroup_size[0] = 64;

   /* Create and initialize local variables. */
   nir_variable *result =
      nir_local_variable_create(b.impl, glsl_vector_type(GLSL_TYPE_UINT64, 2), "result");
   nir_variable *available = nir_local_variable_create(b.impl, glsl_bool_type(), "available");

   nir_store_var(&b, result, nir_vec2(&b, nir_imm_int64(&b, 0), nir_imm_int64(&b, 0)), 0x3);
   nir_store_var(&b, available, nir_imm_false(&b), 0x1);

   nir_ssa_def *flags = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 0), .range = 16);

   /* Load resources. */
   nir_ssa_def *dst_buf = radv_meta_load_descriptor(&b, 0, 0);
   nir_ssa_def *src_buf = radv_meta_load_descriptor(&b, 0, 1);

   /* Compute global ID. */
   nir_ssa_def *global_id = get_global_ids(&b, 1);

   /* Compute src/dst strides. */
   nir_ssa_def *input_stride = nir_imm_int(&b, 32);
   nir_ssa_def *input_base = nir_imul(&b, input_stride, global_id);
   nir_ssa_def *output_stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 4), .range = 16);
   nir_ssa_def *output_base = nir_imul(&b, output_stride, global_id);

   /* Load data from the query pool. */
   nir_ssa_def *load1 = nir_load_ssbo(&b, 4, 32, src_buf, input_base, .align_mul = 32);
   nir_ssa_def *load2 = nir_load_ssbo(
      &b, 4, 32, src_buf, nir_iadd(&b, input_base, nir_imm_int(&b, 16)), .align_mul = 16);

   /* Check if result is available. */
   nir_ssa_def *avails[2];
   avails[0] = nir_iand(&b, nir_channel(&b, load1, 1), nir_channel(&b, load1, 3));
   avails[1] = nir_iand(&b, nir_channel(&b, load2, 1), nir_channel(&b, load2, 3));
   nir_ssa_def *result_is_available =
      nir_i2b(&b, nir_iand(&b, nir_iand(&b, avails[0], avails[1]), nir_imm_int(&b, 0x80000000)));

   /* Only compute result if available. */
   nir_push_if(&b, result_is_available);

   /* Pack values. */
   nir_ssa_def *packed64[4];
   packed64[0] =
      nir_pack_64_2x32(&b, nir_vec2(&b, nir_channel(&b, load1, 0), nir_channel(&b, load1, 1)));
   packed64[1] =
      nir_pack_64_2x32(&b, nir_vec2(&b, nir_channel(&b, load1, 2), nir_channel(&b, load1, 3)));
   packed64[2] =
      nir_pack_64_2x32(&b, nir_vec2(&b, nir_channel(&b, load2, 0), nir_channel(&b, load2, 1)));
   packed64[3] =
      nir_pack_64_2x32(&b, nir_vec2(&b, nir_channel(&b, load2, 2), nir_channel(&b, load2, 3)));

   /* Compute result. */
   nir_ssa_def *num_primitive_written = nir_isub(&b, packed64[3], packed64[1]);
   nir_ssa_def *primitive_storage_needed = nir_isub(&b, packed64[2], packed64[0]);

   nir_store_var(&b, result, nir_vec2(&b, num_primitive_written, primitive_storage_needed), 0x3);
   nir_store_var(&b, available, nir_imm_true(&b), 0x1);

   nir_pop_if(&b, NULL);

   /* Determine if result is 64 or 32 bit. */
   nir_ssa_def *result_is_64bit = nir_test_flag(&b, flags, VK_QUERY_RESULT_64_BIT);
   nir_ssa_def *result_size =
      nir_bcsel(&b, result_is_64bit, nir_imm_int(&b, 16), nir_imm_int(&b, 8));

   /* Store the result if complete or partial results have been requested. */
   nir_push_if(&b, nir_ior(&b, nir_test_flag(&b, flags, VK_QUERY_RESULT_PARTIAL_BIT),
                           nir_load_var(&b, available)));

   /* Store result. */
   nir_push_if(&b, result_is_64bit);

   nir_store_ssbo(&b, nir_load_var(&b, result), dst_buf, output_base);

   nir_push_else(&b, NULL);

   nir_store_ssbo(&b, nir_u2u32(&b, nir_load_var(&b, result)), dst_buf, output_base);

   nir_pop_if(&b, NULL);
   nir_pop_if(&b, NULL);

   radv_store_availability(&b, flags, dst_buf, nir_iadd(&b, result_size, output_base),
                           nir_b2i32(&b, nir_load_var(&b, available)));

   return b.shader;
}

static nir_shader *
build_timestamp_query_shader(struct radv_device *device)
{
   /* the shader this builds is roughly
    *
    * uint32_t src_stride = 8;
    *
    * location(binding = 0) buffer dst_buf;
    * location(binding = 1) buffer src_buf;
    *
    * void main() {
    *	uint64_t result = 0;
    *	bool available = false;
    *	uint64_t src_offset = src_stride * global_id.x;
    * 	uint64_t dst_offset = dst_stride * global_id.x;
    * 	uint64_t timestamp = src_buf[src_offset];
    *	if (timestamp != TIMESTAMP_NOT_READY) {
    *		result = timestamp;
    *		available = true;
    *	}
    * 	uint32_t result_size = flags & VK_QUERY_RESULT_64_BIT ? 8 : 4;
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
   nir_builder b = radv_meta_init_shader(MESA_SHADER_COMPUTE, "timestamp_query");
   b.shader->info.workgroup_size[0] = 64;

   /* Create and initialize local variables. */
   nir_variable *result = nir_local_variable_create(b.impl, glsl_uint64_t_type(), "result");
   nir_variable *available = nir_local_variable_create(b.impl, glsl_bool_type(), "available");

   nir_store_var(&b, result, nir_imm_int64(&b, 0), 0x1);
   nir_store_var(&b, available, nir_imm_false(&b), 0x1);

   nir_ssa_def *flags = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 0), .range = 16);

   /* Load resources. */
   nir_ssa_def *dst_buf = radv_meta_load_descriptor(&b, 0, 0);
   nir_ssa_def *src_buf = radv_meta_load_descriptor(&b, 0, 1);

   /* Compute global ID. */
   nir_ssa_def *global_id = get_global_ids(&b, 1);

   /* Compute src/dst strides. */
   nir_ssa_def *input_stride = nir_imm_int(&b, 8);
   nir_ssa_def *input_base = nir_imul(&b, input_stride, global_id);
   nir_ssa_def *output_stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 4), .range = 16);
   nir_ssa_def *output_base = nir_imul(&b, output_stride, global_id);

   /* Load data from the query pool. */
   nir_ssa_def *load = nir_load_ssbo(&b, 2, 32, src_buf, input_base, .align_mul = 8);

   /* Pack the timestamp. */
   nir_ssa_def *timestamp;
   timestamp =
      nir_pack_64_2x32(&b, nir_vec2(&b, nir_channel(&b, load, 0), nir_channel(&b, load, 1)));

   /* Check if result is available. */
   nir_ssa_def *result_is_available =
      nir_i2b(&b, nir_ine(&b, timestamp, nir_imm_int64(&b, TIMESTAMP_NOT_READY)));

   /* Only store result if available. */
   nir_push_if(&b, result_is_available);

   nir_store_var(&b, result, timestamp, 0x1);
   nir_store_var(&b, available, nir_imm_true(&b), 0x1);

   nir_pop_if(&b, NULL);

   /* Determine if result is 64 or 32 bit. */
   nir_ssa_def *result_is_64bit = nir_test_flag(&b, flags, VK_QUERY_RESULT_64_BIT);
   nir_ssa_def *result_size =
      nir_bcsel(&b, result_is_64bit, nir_imm_int(&b, 8), nir_imm_int(&b, 4));

   /* Store the result if complete or partial results have been requested. */
   nir_push_if(&b, nir_ior(&b, nir_test_flag(&b, flags, VK_QUERY_RESULT_PARTIAL_BIT),
                           nir_load_var(&b, available)));

   /* Store result. */
   nir_push_if(&b, result_is_64bit);

   nir_store_ssbo(&b, nir_load_var(&b, result), dst_buf, output_base);

   nir_push_else(&b, NULL);

   nir_store_ssbo(&b, nir_u2u32(&b, nir_load_var(&b, result)), dst_buf, output_base);

   nir_pop_if(&b, NULL);

   nir_pop_if(&b, NULL);

   radv_store_availability(&b, flags, dst_buf, nir_iadd(&b, result_size, output_base),
                           nir_b2i32(&b, nir_load_var(&b, available)));

   return b.shader;
}

static VkResult
radv_device_init_meta_query_state_internal(struct radv_device *device)
{
   VkResult result;
   nir_shader *occlusion_cs = NULL;
   nir_shader *pipeline_statistics_cs = NULL;
   nir_shader *tfb_cs = NULL;
   nir_shader *timestamp_cs = NULL;

   mtx_lock(&device->meta_state.mtx);
   if (device->meta_state.query.pipeline_statistics_query_pipeline) {
      mtx_unlock(&device->meta_state.mtx);
      return VK_SUCCESS;
   }
   occlusion_cs = build_occlusion_query_shader(device);
   pipeline_statistics_cs = build_pipeline_statistics_query_shader(device);
   tfb_cs = build_tfb_query_shader(device);
   timestamp_cs = build_timestamp_query_shader(device);

   VkDescriptorSetLayoutCreateInfo occlusion_ds_create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
      .bindingCount = 2,
      .pBindings = (VkDescriptorSetLayoutBinding[]){
         {.binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
         {.binding = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
      }};

   result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device), &occlusion_ds_create_info,
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

   result =
      radv_CreatePipelineLayout(radv_device_to_handle(device), &occlusion_pl_create_info,
                                &device->meta_state.alloc, &device->meta_state.query.p_layout);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineShaderStageCreateInfo occlusion_pipeline_shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(occlusion_cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo occlusion_vk_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = occlusion_pipeline_shader_stage,
      .flags = 0,
      .layout = device->meta_state.query.p_layout,
   };

   result = radv_CreateComputePipelines(
      radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
      &occlusion_vk_pipeline_info, NULL, &device->meta_state.query.occlusion_query_pipeline);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineShaderStageCreateInfo pipeline_statistics_pipeline_shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(pipeline_statistics_cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo pipeline_statistics_vk_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = pipeline_statistics_pipeline_shader_stage,
      .flags = 0,
      .layout = device->meta_state.query.p_layout,
   };

   result = radv_CreateComputePipelines(
      radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
      &pipeline_statistics_vk_pipeline_info, NULL,
      &device->meta_state.query.pipeline_statistics_query_pipeline);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineShaderStageCreateInfo tfb_pipeline_shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(tfb_cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo tfb_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = tfb_pipeline_shader_stage,
      .flags = 0,
      .layout = device->meta_state.query.p_layout,
   };

   result = radv_CreateComputePipelines(
      radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
      &tfb_pipeline_info, NULL, &device->meta_state.query.tfb_query_pipeline);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineShaderStageCreateInfo timestamp_pipeline_shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(timestamp_cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo timestamp_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = timestamp_pipeline_shader_stage,
      .flags = 0,
      .layout = device->meta_state.query.p_layout,
   };

   result = radv_CreateComputePipelines(
      radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
      &timestamp_pipeline_info, NULL, &device->meta_state.query.timestamp_query_pipeline);

fail:
   if (result != VK_SUCCESS)
      radv_device_finish_meta_query_state(device);
   ralloc_free(occlusion_cs);
   ralloc_free(pipeline_statistics_cs);
   ralloc_free(tfb_cs);
   ralloc_free(timestamp_cs);
   mtx_unlock(&device->meta_state.mtx);
   return result;
}

VkResult
radv_device_init_meta_query_state(struct radv_device *device, bool on_demand)
{
   if (on_demand)
      return VK_SUCCESS;

   return radv_device_init_meta_query_state_internal(device);
}

void
radv_device_finish_meta_query_state(struct radv_device *device)
{
   if (device->meta_state.query.tfb_query_pipeline)
      radv_DestroyPipeline(radv_device_to_handle(device),
                           device->meta_state.query.tfb_query_pipeline, &device->meta_state.alloc);

   if (device->meta_state.query.pipeline_statistics_query_pipeline)
      radv_DestroyPipeline(radv_device_to_handle(device),
                           device->meta_state.query.pipeline_statistics_query_pipeline,
                           &device->meta_state.alloc);

   if (device->meta_state.query.occlusion_query_pipeline)
      radv_DestroyPipeline(radv_device_to_handle(device),
                           device->meta_state.query.occlusion_query_pipeline,
                           &device->meta_state.alloc);

   if (device->meta_state.query.timestamp_query_pipeline)
      radv_DestroyPipeline(radv_device_to_handle(device),
                           device->meta_state.query.timestamp_query_pipeline,
                           &device->meta_state.alloc);

   if (device->meta_state.query.p_layout)
      radv_DestroyPipelineLayout(radv_device_to_handle(device), device->meta_state.query.p_layout,
                                 &device->meta_state.alloc);

   if (device->meta_state.query.ds_layout)
      radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
                                      device->meta_state.query.ds_layout,
                                      &device->meta_state.alloc);
}

static void
radv_query_shader(struct radv_cmd_buffer *cmd_buffer, VkPipeline *pipeline,
                  struct radeon_winsys_bo *src_bo, struct radeon_winsys_bo *dst_bo,
                  uint64_t src_offset, uint64_t dst_offset, uint32_t src_stride,
                  uint32_t dst_stride, size_t dst_size, uint32_t count, uint32_t flags,
                  uint32_t pipeline_stats_mask, uint32_t avail_offset)
{
   struct radv_device *device = cmd_buffer->device;
   struct radv_meta_saved_state saved_state;
   struct radv_buffer src_buffer, dst_buffer;
   bool old_predicating;

   if (!*pipeline) {
      VkResult ret = radv_device_init_meta_query_state_internal(device);
      if (ret != VK_SUCCESS) {
         cmd_buffer->record_result = ret;
         return;
      }
   }

   radv_meta_save(
      &saved_state, cmd_buffer,
      RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_CONSTANTS | RADV_META_SAVE_DESCRIPTORS);

   /* VK_EXT_conditional_rendering says that copy commands should not be
    * affected by conditional rendering.
    */
   old_predicating = cmd_buffer->state.predicating;
   cmd_buffer->state.predicating = false;

   uint64_t src_buffer_size = MAX2(src_stride * count, avail_offset + 4 * count - src_offset);
   uint64_t dst_buffer_size = dst_stride * (count - 1) + dst_size;

   radv_buffer_init(&src_buffer, device, src_bo, src_buffer_size, src_offset);
   radv_buffer_init(&dst_buffer, device, dst_bo, dst_buffer_size, dst_offset);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        *pipeline);

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->meta_state.query.p_layout, 0, /* set */
      2, /* descriptorWriteCount */
      (VkWriteDescriptorSet[]){
         {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .pBufferInfo = &(VkDescriptorBufferInfo){.buffer = radv_buffer_to_handle(&dst_buffer),
                                                   .offset = 0,
                                                   .range = VK_WHOLE_SIZE}},
         {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstBinding = 1,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .pBufferInfo = &(VkDescriptorBufferInfo){.buffer = radv_buffer_to_handle(&src_buffer),
                                                   .offset = 0,
                                                   .range = VK_WHOLE_SIZE}}});

   /* Encode the number of elements for easy access by the shader. */
   pipeline_stats_mask &= 0x7ff;
   pipeline_stats_mask |= util_bitcount(pipeline_stats_mask) << 16;

   avail_offset -= src_offset;

   struct {
      uint32_t flags;
      uint32_t dst_stride;
      uint32_t pipeline_stats_mask;
      uint32_t avail_offset;
   } push_constants = {flags, dst_stride, pipeline_stats_mask, avail_offset};

   radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), device->meta_state.query.p_layout,
                         VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), &push_constants);

   cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_INV_L2 | RADV_CMD_FLAG_INV_VCACHE;

   if (flags & VK_QUERY_RESULT_WAIT_BIT)
      cmd_buffer->state.flush_bits |= RADV_CMD_FLUSH_AND_INV_FRAMEBUFFER;

   radv_unaligned_dispatch(cmd_buffer, count, 1, 1);

   /* Restore conditional rendering. */
   cmd_buffer->state.predicating = old_predicating;

   radv_buffer_finish(&src_buffer);
   radv_buffer_finish(&dst_buffer);

   radv_meta_restore(&saved_state, cmd_buffer);
}

static bool
radv_query_pool_needs_gds(struct radv_device *device, struct radv_query_pool *pool)
{
   /* The number of primitives generated by geometry shader invocations is
    * only counted by the hardware if GS uses the legacy path. When NGG GS
    * is used, the hardware can't know the number of generated primitives
    * and we have to it manually inside the shader. To achieve that, the
    * driver does a plain GDS atomic to accumulate that value.
    * TODO: fix use of NGG GS and non-NGG GS inside the same begin/end
    * query.
    */
   return device->physical_device->use_ngg &&
          (pool->pipeline_stats_mask & VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT);
}

static void
radv_destroy_query_pool(struct radv_device *device, const VkAllocationCallbacks *pAllocator,
                        struct radv_query_pool *pool)
{
   if (pool->bo)
      device->ws->buffer_destroy(device->ws, pool->bo);
   vk_object_base_finish(&pool->base);
   vk_free2(&device->vk.alloc, pAllocator, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateQueryPool(VkDevice _device, const VkQueryPoolCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_query_pool *pool =
      vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*pool), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (!pool)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pool->base, VK_OBJECT_TYPE_QUERY_POOL);

   switch (pCreateInfo->queryType) {
   case VK_QUERY_TYPE_OCCLUSION:
      pool->stride = 16 * device->physical_device->rad_info.max_render_backends;
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      pool->stride = pipelinestat_block_size * 2;
      break;
   case VK_QUERY_TYPE_TIMESTAMP:
   case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR:
   case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR:
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

   VkResult result = device->ws->buffer_create(device->ws, pool->size, 64, RADEON_DOMAIN_GTT,
                                               RADEON_FLAG_NO_INTERPROCESS_SHARING,
                                               RADV_BO_PRIORITY_QUERY_POOL, 0, &pool->bo);
   if (result != VK_SUCCESS) {
      radv_destroy_query_pool(device, pAllocator, pool);
      return vk_error(device, result);
   }

   pool->ptr = device->ws->buffer_map(pool->bo);
   if (!pool->ptr) {
      radv_destroy_query_pool(device, pAllocator, pool);
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   *pQueryPool = radv_query_pool_to_handle(pool);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyQueryPool(VkDevice _device, VkQueryPool _pool, const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_query_pool, pool, _pool);

   if (!pool)
      return;

   radv_destroy_query_pool(device, pAllocator, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetQueryPoolResults(VkDevice _device, VkQueryPool queryPool, uint32_t firstQuery,
                         uint32_t queryCount, size_t dataSize, void *pData, VkDeviceSize stride,
                         VkQueryResultFlags flags)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
   char *data = pData;
   VkResult result = VK_SUCCESS;

   if (vk_device_is_lost(&device->vk))
      return VK_ERROR_DEVICE_LOST;

   for (unsigned query_idx = 0; query_idx < queryCount; ++query_idx, data += stride) {
      char *dest = data;
      unsigned query = firstQuery + query_idx;
      char *src = pool->ptr + query * pool->stride;
      uint32_t available;

      switch (pool->type) {
      case VK_QUERY_TYPE_TIMESTAMP:
      case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR:
      case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR: {
         uint64_t const *src64 = (uint64_t const *)src;
         uint64_t value;

         do {
            value = p_atomic_read(src64);
         } while (value == TIMESTAMP_NOT_READY && (flags & VK_QUERY_RESULT_WAIT_BIT));

         available = value != TIMESTAMP_NOT_READY;

         if (!available && !(flags & VK_QUERY_RESULT_PARTIAL_BIT))
            result = VK_NOT_READY;

         if (flags & VK_QUERY_RESULT_64_BIT) {
            if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT))
               *(uint64_t *)dest = value;
            dest += 8;
         } else {
            if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT))
               *(uint32_t *)dest = (uint32_t)value;
            dest += 4;
         }
         break;
      }
      case VK_QUERY_TYPE_OCCLUSION: {
         uint64_t const *src64 = (uint64_t const *)src;
         uint32_t db_count = device->physical_device->rad_info.max_render_backends;
         uint32_t enabled_rb_mask = device->physical_device->rad_info.enabled_rb_mask;
         uint64_t sample_count = 0;
         available = 1;

         for (int i = 0; i < db_count; ++i) {
            uint64_t start, end;

            if (!(enabled_rb_mask & (1 << i)))
               continue;

            do {
               start = p_atomic_read(src64 + 2 * i);
               end = p_atomic_read(src64 + 2 * i + 1);
            } while ((!(start & (1ull << 63)) || !(end & (1ull << 63))) &&
                     (flags & VK_QUERY_RESULT_WAIT_BIT));

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
               *(uint64_t *)dest = sample_count;
            dest += 8;
         } else {
            if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT))
               *(uint32_t *)dest = sample_count;
            dest += 4;
         }
         break;
      }
      case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
         const uint32_t *avail_ptr =
            (const uint32_t *)(pool->ptr + pool->availability_offset + 4 * query);

         do {
            available = p_atomic_read(avail_ptr);
         } while (!available && (flags & VK_QUERY_RESULT_WAIT_BIT));

         if (!available && !(flags & VK_QUERY_RESULT_PARTIAL_BIT))
            result = VK_NOT_READY;

         const uint64_t *start = (uint64_t *)src;
         const uint64_t *stop = (uint64_t *)(src + pipelinestat_block_size);
         if (flags & VK_QUERY_RESULT_64_BIT) {
            uint64_t *dst = (uint64_t *)dest;
            dest += util_bitcount(pool->pipeline_stats_mask) * 8;
            for (int i = 0; i < ARRAY_SIZE(pipeline_statistics_indices); ++i) {
               if (pool->pipeline_stats_mask & (1u << i)) {
                  if (available || (flags & VK_QUERY_RESULT_PARTIAL_BIT))
                     *dst = stop[pipeline_statistics_indices[i]] -
                            start[pipeline_statistics_indices[i]];
                  dst++;
               }
            }

         } else {
            uint32_t *dst = (uint32_t *)dest;
            dest += util_bitcount(pool->pipeline_stats_mask) * 4;
            for (int i = 0; i < ARRAY_SIZE(pipeline_statistics_indices); ++i) {
               if (pool->pipeline_stats_mask & (1u << i)) {
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
         uint64_t const *src64 = (uint64_t const *)src;
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
            if (!(p_atomic_read(src64 + j) & 0x8000000000000000UL))
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
            *(uint64_t *)dest = available;
         } else {
            *(uint32_t *)dest = available;
         }
      }
   }

   return result;
}

static void
emit_query_flush(struct radv_cmd_buffer *cmd_buffer, struct radv_query_pool *pool)
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

static size_t
radv_query_result_size(const struct radv_query_pool *pool, VkQueryResultFlags flags)
{
   unsigned values = (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) ? 1 : 0;
   switch (pool->type) {
   case VK_QUERY_TYPE_TIMESTAMP:
   case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR:
   case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR:
   case VK_QUERY_TYPE_OCCLUSION:
      values += 1;
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      values += util_bitcount(pool->pipeline_stats_mask);
      break;
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      values += 2;
      break;
   default:
      unreachable("trying to get size of unhandled query type");
   }
   return values * ((flags & VK_QUERY_RESULT_64_BIT) ? 8 : 4);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                             uint32_t firstQuery, uint32_t queryCount, VkBuffer dstBuffer,
                             VkDeviceSize dstOffset, VkDeviceSize stride, VkQueryResultFlags flags)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
   RADV_FROM_HANDLE(radv_buffer, dst_buffer, dstBuffer);
   struct radeon_cmdbuf *cs = cmd_buffer->cs;
   uint64_t va = radv_buffer_get_va(pool->bo);
   uint64_t dest_va = radv_buffer_get_va(dst_buffer->bo);
   size_t dst_size = radv_query_result_size(pool, flags);
   dest_va += dst_buffer->offset + dstOffset;

   if (!queryCount)
      return;

   radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, pool->bo);
   radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, dst_buffer->bo);

   /* From the Vulkan spec 1.1.108:
    *
    * "vkCmdCopyQueryPoolResults is guaranteed to see the effect of
    *  previous uses of vkCmdResetQueryPool in the same queue, without any
    *  additional synchronization."
    *
    * So, we have to flush the caches if the compute shader path was used.
    */
   emit_query_flush(cmd_buffer, pool);

   switch (pool->type) {
   case VK_QUERY_TYPE_OCCLUSION:
      if (flags & VK_QUERY_RESULT_WAIT_BIT) {
         unsigned enabled_rb_mask = cmd_buffer->device->physical_device->rad_info.enabled_rb_mask;
         uint32_t rb_avail_offset = 16 * util_last_bit(enabled_rb_mask) - 4;
         for (unsigned i = 0; i < queryCount; ++i, dest_va += stride) {
            unsigned query = firstQuery + i;
            uint64_t src_va = va + query * pool->stride + rb_avail_offset;

            radeon_check_space(cmd_buffer->device->ws, cs, 7);

            /* Waits on the upper word of the last DB entry */
            radv_cp_wait_mem(cs, WAIT_REG_MEM_GREATER_OR_EQUAL, src_va, 0x80000000, 0xffffffff);
         }
      }
      radv_query_shader(cmd_buffer, &cmd_buffer->device->meta_state.query.occlusion_query_pipeline,
                        pool->bo, dst_buffer->bo, firstQuery * pool->stride,
                        dst_buffer->offset + dstOffset, pool->stride, stride, dst_size, queryCount,
                        flags, 0, 0);
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      if (flags & VK_QUERY_RESULT_WAIT_BIT) {
         for (unsigned i = 0; i < queryCount; ++i, dest_va += stride) {
            unsigned query = firstQuery + i;

            radeon_check_space(cmd_buffer->device->ws, cs, 7);

            uint64_t avail_va = va + pool->availability_offset + 4 * query;

            /* This waits on the ME. All copies below are done on the ME */
            radv_cp_wait_mem(cs, WAIT_REG_MEM_EQUAL, avail_va, 1, 0xffffffff);
         }
      }
      radv_query_shader(
         cmd_buffer, &cmd_buffer->device->meta_state.query.pipeline_statistics_query_pipeline,
         pool->bo, dst_buffer->bo, firstQuery * pool->stride, dst_buffer->offset + dstOffset,
         pool->stride, stride, dst_size, queryCount, flags, pool->pipeline_stats_mask,
         pool->availability_offset + 4 * firstQuery);
      break;
   case VK_QUERY_TYPE_TIMESTAMP:
   case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR:
   case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR:
      if (flags & VK_QUERY_RESULT_WAIT_BIT) {
         for (unsigned i = 0; i < queryCount; ++i, dest_va += stride) {
            unsigned query = firstQuery + i;
            uint64_t local_src_va = va + query * pool->stride;

            radeon_check_space(cmd_buffer->device->ws, cs, 7);

            /* Wait on the high 32 bits of the timestamp in
             * case the low part is 0xffffffff.
             */
            radv_cp_wait_mem(cs, WAIT_REG_MEM_NOT_EQUAL, local_src_va + 4,
                             TIMESTAMP_NOT_READY >> 32, 0xffffffff);
         }
      }

      radv_query_shader(cmd_buffer, &cmd_buffer->device->meta_state.query.timestamp_query_pipeline,
                        pool->bo, dst_buffer->bo, firstQuery * pool->stride,
                        dst_buffer->offset + dstOffset, pool->stride, stride, dst_size, queryCount,
                        flags, 0, 0);
      break;
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      if (flags & VK_QUERY_RESULT_WAIT_BIT) {
         for (unsigned i = 0; i < queryCount; i++) {
            unsigned query = firstQuery + i;
            uint64_t src_va = va + query * pool->stride;

            radeon_check_space(cmd_buffer->device->ws, cs, 7 * 4);

            /* Wait on the upper word of all results. */
            for (unsigned j = 0; j < 4; j++, src_va += 8) {
               radv_cp_wait_mem(cs, WAIT_REG_MEM_GREATER_OR_EQUAL, src_va + 4, 0x80000000,
                                0xffffffff);
            }
         }
      }

      radv_query_shader(cmd_buffer, &cmd_buffer->device->meta_state.query.tfb_query_pipeline,
                        pool->bo, dst_buffer->bo, firstQuery * pool->stride,
                        dst_buffer->offset + dstOffset, pool->stride, stride, dst_size, queryCount,
                        flags, 0, 0);
      break;
   default:
      unreachable("trying to get results of unhandled query type");
   }
}

static uint32_t
query_clear_value(VkQueryType type)
{
   switch (type) {
   case VK_QUERY_TYPE_TIMESTAMP:
   case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR:
   case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR:
      return (uint32_t)TIMESTAMP_NOT_READY;
   default:
      return 0;
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery,
                       uint32_t queryCount)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
   uint32_t value = query_clear_value(pool->type);
   uint32_t flush_bits = 0;

   /* Make sure to sync all previous work if the given command buffer has
    * pending active queries. Otherwise the GPU might write queries data
    * after the reset operation.
    */
   cmd_buffer->state.flush_bits |= cmd_buffer->active_query_flush_bits;

   flush_bits |= radv_fill_buffer(cmd_buffer, NULL, pool->bo, firstQuery * pool->stride,
                                  queryCount * pool->stride, value);

   if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
      flush_bits |= radv_fill_buffer(cmd_buffer, NULL, pool->bo,
                                     pool->availability_offset + firstQuery * 4, queryCount * 4, 0);
   }

   if (flush_bits) {
      /* Only need to flush caches for the compute shader path. */
      cmd_buffer->pending_reset_query = true;
      cmd_buffer->state.flush_bits |= flush_bits;
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_ResetQueryPool(VkDevice _device, VkQueryPool queryPool, uint32_t firstQuery,
                    uint32_t queryCount)
{
   RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);

   uint32_t value = query_clear_value(pool->type);
   uint32_t *data = (uint32_t *)(pool->ptr + firstQuery * pool->stride);
   uint32_t *data_end = (uint32_t *)(pool->ptr + (firstQuery + queryCount) * pool->stride);

   for (uint32_t *p = data; p != data_end; ++p)
      *p = value;

   if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
      memset(pool->ptr + pool->availability_offset + firstQuery * 4, 0, queryCount * 4);
   }
}

static unsigned
event_type_for_stream(unsigned stream)
{
   switch (stream) {
   default:
   case 0:
      return V_028A90_SAMPLE_STREAMOUTSTATS;
   case 1:
      return V_028A90_SAMPLE_STREAMOUTSTATS1;
   case 2:
      return V_028A90_SAMPLE_STREAMOUTSTATS2;
   case 3:
      return V_028A90_SAMPLE_STREAMOUTSTATS3;
   }
}

static void
emit_begin_query(struct radv_cmd_buffer *cmd_buffer, struct radv_query_pool *pool, uint64_t va,
                 VkQueryType query_type, VkQueryControlFlags flags, uint32_t index)
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

      if (radv_query_pool_needs_gds(cmd_buffer->device, pool)) {
         int idx = radv_get_pipeline_statistics_index(
            VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT);

         /* Make sure GDS is idle before copying the value. */
         cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_PS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_L2;
         si_emit_cache_flush(cmd_buffer);

         va += 8 * idx;

         radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
         radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_GDS) | COPY_DATA_DST_SEL(COPY_DATA_DST_MEM) |
                            COPY_DATA_WR_CONFIRM);
         radeon_emit(cs, 0);
         radeon_emit(cs, 0);
         radeon_emit(cs, va);
         radeon_emit(cs, va >> 32);

         /* Record that the command buffer needs GDS. */
         cmd_buffer->gds_needed = true;

         cmd_buffer->state.active_pipeline_gds_queries++;
      }
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

static void
emit_end_query(struct radv_cmd_buffer *cmd_buffer, struct radv_query_pool *pool, uint64_t va,
               uint64_t avail_va, VkQueryType query_type, uint32_t index)
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

      si_cs_emit_write_event_eop(cs, cmd_buffer->device->physical_device->rad_info.chip_class,
                                 radv_cmd_buffer_uses_mec(cmd_buffer), V_028A90_BOTTOM_OF_PIPE_TS,
                                 0, EOP_DST_SEL_MEM, EOP_DATA_SEL_VALUE_32BIT, avail_va, 1,
                                 cmd_buffer->gfx9_eop_bug_va);

      if (radv_query_pool_needs_gds(cmd_buffer->device, pool)) {
         int idx = radv_get_pipeline_statistics_index(
            VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT);

         /* Make sure GDS is idle before copying the value. */
         cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_PS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_L2;
         si_emit_cache_flush(cmd_buffer);

         va += 8 * idx;

         radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
         radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_GDS) | COPY_DATA_DST_SEL(COPY_DATA_DST_MEM) |
                            COPY_DATA_WR_CONFIRM);
         radeon_emit(cs, 0);
         radeon_emit(cs, 0);
         radeon_emit(cs, va);
         radeon_emit(cs, va >> 32);

         cmd_buffer->state.active_pipeline_gds_queries--;
      }
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

   cmd_buffer->active_query_flush_bits |= RADV_CMD_FLAG_PS_PARTIAL_FLUSH |
                                          RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_L2 |
                                          RADV_CMD_FLAG_INV_VCACHE;
   if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX9) {
      cmd_buffer->active_query_flush_bits |=
         RADV_CMD_FLAG_FLUSH_AND_INV_CB | RADV_CMD_FLAG_FLUSH_AND_INV_DB;
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdBeginQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query,
                             VkQueryControlFlags flags, uint32_t index)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
   struct radeon_cmdbuf *cs = cmd_buffer->cs;
   uint64_t va = radv_buffer_get_va(pool->bo);

   radv_cs_add_buffer(cmd_buffer->device->ws, cs, pool->bo);

   emit_query_flush(cmd_buffer, pool);

   va += pool->stride * query;

   emit_begin_query(cmd_buffer, pool, va, pool->type, flags, index);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query,
                   VkQueryControlFlags flags)
{
   radv_CmdBeginQueryIndexedEXT(commandBuffer, queryPool, query, flags, 0);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdEndQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query,
                           uint32_t index)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
   uint64_t va = radv_buffer_get_va(pool->bo);
   uint64_t avail_va = va + pool->availability_offset + 4 * query;
   va += pool->stride * query;

   /* Do not need to add the pool BO to the list because the query must
    * currently be active, which means the BO is already in the list.
    */
   emit_end_query(cmd_buffer, pool, va, avail_va, pool->type, index);

   /*
    * For multiview we have to emit a query for each bit in the mask,
    * however the first query we emit will get the totals for all the
    * operations, so we don't want to get a real value in the other
    * queries. This emits a fake begin/end sequence so the waiting
    * code gets a completed query value and doesn't hang, but the
    * query returns 0.
    */
   if (cmd_buffer->state.subpass && cmd_buffer->state.subpass->view_mask) {
      for (unsigned i = 1; i < util_bitcount(cmd_buffer->state.subpass->view_mask); i++) {
         va += pool->stride;
         avail_va += 4;
         emit_begin_query(cmd_buffer, pool, va, pool->type, 0, 0);
         emit_end_query(cmd_buffer, pool, va, avail_va, pool->type, 0);
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query)
{
   radv_CmdEndQueryIndexedEXT(commandBuffer, queryPool, query, 0);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdWriteTimestamp2KHR(VkCommandBuffer commandBuffer, VkPipelineStageFlags2KHR stage,
                           VkQueryPool queryPool, uint32_t query)
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

   ASSERTED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cs, 28 * num_queries);

   for (unsigned i = 0; i < num_queries; i++) {
      if (stage == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR) {
         radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
         radeon_emit(cs, COPY_DATA_COUNT_SEL | COPY_DATA_WR_CONFIRM |
                            COPY_DATA_SRC_SEL(COPY_DATA_TIMESTAMP) | COPY_DATA_DST_SEL(V_370_MEM));
         radeon_emit(cs, 0);
         radeon_emit(cs, 0);
         radeon_emit(cs, query_va);
         radeon_emit(cs, query_va >> 32);
      } else {
         si_cs_emit_write_event_eop(cs, cmd_buffer->device->physical_device->rad_info.chip_class,
                                    mec, V_028A90_BOTTOM_OF_PIPE_TS, 0, EOP_DST_SEL_MEM,
                                    EOP_DATA_SEL_TIMESTAMP, query_va, 0,
                                    cmd_buffer->gfx9_eop_bug_va);
      }
      query_va += pool->stride;
   }

   cmd_buffer->active_query_flush_bits |= RADV_CMD_FLAG_PS_PARTIAL_FLUSH |
                                          RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_L2 |
                                          RADV_CMD_FLAG_INV_VCACHE;
   if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX9) {
      cmd_buffer->active_query_flush_bits |=
         RADV_CMD_FLAG_FLUSH_AND_INV_CB | RADV_CMD_FLAG_FLUSH_AND_INV_DB;
   }

   assert(cmd_buffer->cs->cdw <= cdw_max);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdWriteAccelerationStructuresPropertiesKHR(
   VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount,
   const VkAccelerationStructureKHR *pAccelerationStructures, VkQueryType queryType,
   VkQueryPool queryPool, uint32_t firstQuery)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
   struct radeon_cmdbuf *cs = cmd_buffer->cs;
   uint64_t pool_va = radv_buffer_get_va(pool->bo);
   uint64_t query_va = pool_va + pool->stride * firstQuery;

   radv_cs_add_buffer(cmd_buffer->device->ws, cs, pool->bo);

   emit_query_flush(cmd_buffer, pool);

   ASSERTED unsigned cdw_max =
      radeon_check_space(cmd_buffer->device->ws, cs, 6 * accelerationStructureCount);

   for (uint32_t i = 0; i < accelerationStructureCount; ++i) {
      RADV_FROM_HANDLE(radv_acceleration_structure, accel_struct, pAccelerationStructures[i]);
      uint64_t va = radv_accel_struct_get_va(accel_struct);

      switch (queryType) {
      case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR:
         va += offsetof(struct radv_accel_struct_header, compacted_size);
         break;
      case VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR:
         va += offsetof(struct radv_accel_struct_header, serialization_size);
         break;
      default:
         unreachable("Unhandle accel struct query type.");
      }

      radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
      radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_SRC_MEM) | COPY_DATA_DST_SEL(COPY_DATA_DST_MEM) |
                         COPY_DATA_COUNT_SEL | COPY_DATA_WR_CONFIRM);
      radeon_emit(cs, va);
      radeon_emit(cs, va >> 32);
      radeon_emit(cs, query_va);
      radeon_emit(cs, query_va >> 32);

      query_va += pool->stride;
   }

   assert(cmd_buffer->cs->cdw <= cdw_max);
}
