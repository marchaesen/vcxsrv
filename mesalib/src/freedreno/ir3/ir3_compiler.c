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
	{"vs",         IR3_DBG_SHADER_VS,  "Print shader disasm for vertex shaders"},
	{"tcs",        IR3_DBG_SHADER_TCS, "Print shader disasm for tess ctrl shaders"},
	{"tes",        IR3_DBG_SHADER_TES, "Print shader disasm for tess eval shaders"},
	{"gs",         IR3_DBG_SHADER_GS,  "Print shader disasm for geometry shaders"},
	{"fs",         IR3_DBG_SHADER_FS,  "Print shader disasm for fragment shaders"},
	{"cs",         IR3_DBG_SHADER_CS,  "Print shader disasm for compute shaders"},
	{"disasm",     IR3_DBG_DISASM,     "Dump NIR and adreno shader disassembly"},
	{"optmsgs",    IR3_DBG_OPTMSGS,    "Enable optimizer debug messages"},
	{"forces2en",  IR3_DBG_FORCES2EN,  "Force s2en mode for tex sampler instructions"},
	{"nouboopt",   IR3_DBG_NOUBOOPT,   "Disable lowering UBO to uniform"},
	{"nofp16",     IR3_DBG_NOFP16,     "Don't lower mediump to fp16"},
	{"nocache",    IR3_DBG_NOCACHE,    "Disable shader cache"},
#ifdef DEBUG
	/* DEBUG-only options: */
	{"schedmsgs",  IR3_DBG_SCHEDMSGS,  "Enable scheduler debug messages"},
	{"ramsgs",     IR3_DBG_RAMSGS,     "Enable register-allocation debug messages"},
#endif
	DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(ir3_shader_debug, "IR3_SHADER_DEBUG", shader_debug_options, 0)

enum ir3_shader_debug ir3_shader_debug = 0;

void
ir3_compiler_destroy(struct ir3_compiler *compiler)
{
	ralloc_free(compiler);
}

struct ir3_compiler *
ir3_compiler_create(struct fd_device *dev, uint32_t gpu_id)
{
	struct ir3_compiler *compiler = rzalloc(NULL, struct ir3_compiler);

	ir3_shader_debug = debug_get_option_ir3_shader_debug();

	compiler->dev = dev;
	compiler->gpu_id = gpu_id;
	compiler->set = ir3_ra_alloc_reg_set(compiler, false);

	if (compiler->gpu_id >= 600) {
		compiler->mergedregs_set = ir3_ra_alloc_reg_set(compiler, true);
		compiler->samgq_workaround = true;
		/* a6xx split the pipeline state into geometry and fragment state, in
		 * order to let the VS run ahead of the FS. As a result there are now
		 * separate const files for the the fragment shader and everything
		 * else, and separate limits. There seems to be a shared limit, but
		 * it's higher than the vert or frag limits.
		 *
		 * TODO: The shared limit seems to be different on different on
		 * different models.
		 */
		compiler->max_const_pipeline = 640;
		compiler->max_const_frag = 512;
		compiler->max_const_geom = 512;
		compiler->max_const_safe = 128;

		/* Compute shaders don't share a const file with the FS. Instead they
		 * have their own file, which is smaller than the FS one.
		 *
		 * TODO: is this true on earlier gen's?
		 */
		compiler->max_const_compute = 256;

		/* TODO: implement clip+cull distances on earlier gen's */
		compiler->has_clip_cull = true;

		if (compiler->gpu_id == 650)
			compiler->tess_use_shared = true;
	} else {
		compiler->max_const_pipeline = 512;
		compiler->max_const_geom = 512;
		compiler->max_const_frag = 512;
		compiler->max_const_compute = 512;

		/* Note: this will have to change if/when we support tess+GS on
		 * earlier gen's.
		 */
		compiler->max_const_safe = 256;
	}

	if (compiler->gpu_id >= 400) {
		/* need special handling for "flat" */
		compiler->flat_bypass = true;
		compiler->levels_add_one = false;
		compiler->unminify_coords = false;
		compiler->txf_ms_with_isaml = false;
		compiler->array_index_add_half = true;
		compiler->instr_align = 16;
		compiler->const_upload_unit = 4;
	} else {
		/* no special handling for "flat" */
		compiler->flat_bypass = false;
		compiler->levels_add_one = true;
		compiler->unminify_coords = true;
		compiler->txf_ms_with_isaml = true;
		compiler->array_index_add_half = false;
		compiler->instr_align = 4;
		compiler->const_upload_unit = 8;
	}

	ir3_disk_cache_init(compiler);

	return compiler;
}
