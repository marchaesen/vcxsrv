/*
 * Copyright © 2017 Red Hat
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

#ifndef AC_SHADER_INFO_H
#define AC_SHADER_INFO_H

struct nir_shader;
struct ac_nir_compiler_options;

struct ac_shader_info {
	bool needs_push_constants;
	uint32_t desc_set_used_mask;
	bool needs_multiview_view_index;
	struct {
		bool has_vertex_buffers; /* needs vertex buffers and base/start */
		bool needs_draw_id;
		bool needs_instance_id;
	} vs;
	struct {
		bool force_persample;
		bool needs_sample_positions;
		bool uses_input_attachments;
	} ps;
	struct {
		uint8_t grid_components_used;
	} cs;
};

/* A NIR pass to gather all the info needed to optimise the allocation patterns
 * for the RADV user sgprs
 */
void
ac_nir_shader_info_pass(struct nir_shader *nir,
			const struct ac_nir_compiler_options *options,
			struct ac_shader_info *info);

#endif
