/*
 * Copyright © 2016 Bas Nieuwenhuizen
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

#ifndef AC_NIR_TO_LLVM_H
#define AC_NIR_TO_LLVM_H

#include <stdbool.h>
#include "llvm-c/Core.h"
#include "llvm-c/TargetMachine.h"
#include "amd_family.h"
#include "compiler/shader_enums.h"

struct nir_shader;
struct nir_variable;
struct ac_llvm_context;
struct ac_shader_abi;

/* Interpolation locations */
#define INTERP_CENTER 0
#define INTERP_CENTROID 1
#define INTERP_SAMPLE 2

static inline unsigned ac_llvm_reg_index_soa(unsigned index, unsigned chan)
{
	return (index * 4) + chan;
}

void ac_lower_indirect_derefs(struct nir_shader *nir, enum chip_class);

void ac_nir_translate(struct ac_llvm_context *ac, struct ac_shader_abi *abi,
		      struct nir_shader *nir);

void
ac_handle_shader_output_decl(struct ac_llvm_context *ctx,
			     struct ac_shader_abi *abi,
			     struct nir_shader *nir,
			     struct nir_variable *variable,
			     gl_shader_stage stage);

void ac_emit_barrier(struct ac_llvm_context *ac, gl_shader_stage stage);

#endif /* AC_NIR_TO_LLVM_H */
