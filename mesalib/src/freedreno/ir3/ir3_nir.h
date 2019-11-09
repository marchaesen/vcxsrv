/*
 * Copyright (C) 2015 Rob Clark <robclark@freedesktop.org>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef IR3_NIR_H_
#define IR3_NIR_H_

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/shader_enums.h"

#include "ir3_shader.h"

bool ir3_nir_apply_trig_workarounds(nir_shader *shader);
bool ir3_nir_lower_imul(nir_shader *shader);
bool ir3_nir_lower_tg4_to_tex(nir_shader *shader);
bool ir3_nir_lower_io_offsets(nir_shader *shader);
bool ir3_nir_lower_load_barycentric_at_sample(nir_shader *shader);
bool ir3_nir_lower_load_barycentric_at_offset(nir_shader *shader);
bool ir3_nir_move_varying_inputs(nir_shader *shader);
int ir3_nir_coord_offset(nir_ssa_def *ssa);
bool ir3_nir_lower_tex_prefetch(nir_shader *shader);


void ir3_nir_lower_vs_to_explicit_io(nir_shader *shader, struct ir3_shader *s);
void ir3_nir_lower_gs(nir_shader *shader, struct ir3_shader *s);

const nir_shader_compiler_options * ir3_get_compiler_options(struct ir3_compiler *compiler);
bool ir3_key_lowers_nir(const struct ir3_shader_key *key);
void ir3_optimize_nir(struct ir3_shader *shader, nir_shader *s,
		const struct ir3_shader_key *key);

bool ir3_nir_analyze_ubo_ranges(nir_shader *nir, struct ir3_shader *shader);

nir_ssa_def *
ir3_nir_try_propagate_bit_shift(nir_builder *b, nir_ssa_def *offset, int32_t shift);

#endif /* IR3_NIR_H_ */
