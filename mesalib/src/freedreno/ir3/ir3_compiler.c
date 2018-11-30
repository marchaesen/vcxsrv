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

#include "util/ralloc.h"

#include "ir3_compiler.h"

static const struct debug_named_value shader_debug_options[] = {
		{"vs", IR3_DBG_SHADER_VS, "Print shader disasm for vertex shaders"},
		{"fs", IR3_DBG_SHADER_FS, "Print shader disasm for fragment shaders"},
		{"cs", IR3_DBG_SHADER_CS, "Print shader disasm for compute shaders"},
		{"disasm",  IR3_DBG_DISASM, "Dump NIR and adreno shader disassembly"},
		{"optmsgs", IR3_DBG_OPTMSGS,"Enable optimizer debug messages"},
		DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(ir3_shader_debug, "IR3_SHADER_DEBUG", shader_debug_options, 0)

enum ir3_shader_debug ir3_shader_debug = 0;

struct ir3_compiler * ir3_compiler_create(struct fd_device *dev, uint32_t gpu_id)
{
	struct ir3_compiler *compiler = rzalloc(NULL, struct ir3_compiler);

	ir3_shader_debug = debug_get_option_ir3_shader_debug();

	compiler->dev = dev;
	compiler->gpu_id = gpu_id;
	compiler->set = ir3_ra_alloc_reg_set(compiler);

	if (compiler->gpu_id >= 400) {
		/* need special handling for "flat" */
		compiler->flat_bypass = true;
		compiler->levels_add_one = false;
		compiler->unminify_coords = false;
		compiler->txf_ms_with_isaml = false;
		compiler->array_index_add_half = true;
	} else {
		/* no special handling for "flat" */
		compiler->flat_bypass = false;
		compiler->levels_add_one = true;
		compiler->unminify_coords = true;
		compiler->txf_ms_with_isaml = true;
		compiler->array_index_add_half = false;
	}

	return compiler;
}
