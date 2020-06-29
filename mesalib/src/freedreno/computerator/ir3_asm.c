/*
 * Copyright © 2020 Google, Inc.
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
 */

#include "ir3/ir3_assembler.h"
#include "ir3/ir3_compiler.h"

#include "ir3_asm.h"

struct ir3_kernel *
ir3_asm_assemble(struct ir3_compiler *c, FILE *in)
{
	struct ir3_kernel *kernel = calloc(1, sizeof(*kernel));
	struct ir3_shader *shader = ir3_parse_asm(c, &kernel->info, in);
	struct ir3_shader_variant *v = shader->variants;

	v->mergedregs = true;

	kernel->v = v;
	kernel->bin = v->bin;

	memcpy(kernel->base.local_size, kernel->info.local_size, sizeof(kernel->base.local_size));
	kernel->base.num_bufs = kernel->info.num_bufs;
	memcpy(kernel->base.buf_sizes, kernel->info.buf_sizes, sizeof(kernel->base.buf_sizes));

	unsigned sz = v->info.sizedwords * 4;

	v->bo = fd_bo_new(c->dev, sz,
			DRM_FREEDRENO_GEM_CACHE_WCOMBINE |
			DRM_FREEDRENO_GEM_TYPE_KMEM,
			"%s", ir3_shader_stage(v));

	memcpy(fd_bo_map(v->bo), kernel->bin, sz);

	return kernel;
}

void
ir3_asm_disassemble(struct ir3_kernel *k, FILE *out)
{
	ir3_shader_disasm(k->v, k->bin, out);
}
