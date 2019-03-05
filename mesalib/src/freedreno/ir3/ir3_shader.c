/*
 * Copyright (C) 2014 Rob Clark <robclark@freedesktop.org>
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

#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_format.h"

#include "drm/freedreno_drmif.h"

#include "ir3_shader.h"
#include "ir3_compiler.h"
#include "ir3_nir.h"

int
ir3_glsl_type_size(const struct glsl_type *type)
{
	return glsl_count_attribute_slots(type, false);
}

static void
delete_variant(struct ir3_shader_variant *v)
{
	if (v->ir)
		ir3_destroy(v->ir);
	if (v->bo)
		fd_bo_del(v->bo);
	if (v->immediates)
		free(v->immediates);
	free(v);
}

/* for vertex shader, the inputs are loaded into registers before the shader
 * is executed, so max_regs from the shader instructions might not properly
 * reflect the # of registers actually used, especially in case passthrough
 * varyings.
 *
 * Likewise, for fragment shader, we can have some regs which are passed
 * input values but never touched by the resulting shader (ie. as result
 * of dead code elimination or simply because we don't know how to turn
 * the reg off.
 */
static void
fixup_regfootprint(struct ir3_shader_variant *v)
{
	unsigned i;

	for (i = 0; i < v->inputs_count; i++) {
		/* skip frag inputs fetch via bary.f since their reg's are
		 * not written by gpu before shader starts (and in fact the
		 * regid's might not even be valid)
		 */
		if (v->inputs[i].bary)
			continue;

		/* ignore high regs that are global to all threads in a warp
		 * (they exist by default) (a5xx+)
		 */
		if (v->inputs[i].regid >= regid(48,0))
			continue;

		if (v->inputs[i].compmask) {
			unsigned n = util_last_bit(v->inputs[i].compmask) - 1;
			int32_t regid = (v->inputs[i].regid + n) >> 2;
			v->info.max_reg = MAX2(v->info.max_reg, regid);
		}
	}

	for (i = 0; i < v->outputs_count; i++) {
		int32_t regid = (v->outputs[i].regid + 3) >> 2;
		v->info.max_reg = MAX2(v->info.max_reg, regid);
	}
}

/* wrapper for ir3_assemble() which does some info fixup based on
 * shader state.  Non-static since used by ir3_cmdline too.
 */
void * ir3_shader_assemble(struct ir3_shader_variant *v, uint32_t gpu_id)
{
	void *bin;

	bin = ir3_assemble(v->ir, &v->info, gpu_id);
	if (!bin)
		return NULL;

	if (gpu_id >= 400) {
		v->instrlen = v->info.sizedwords / (2 * 16);
	} else {
		v->instrlen = v->info.sizedwords / (2 * 4);
	}

	/* NOTE: if relative addressing is used, we set constlen in
	 * the compiler (to worst-case value) since we don't know in
	 * the assembler what the max addr reg value can be:
	 */
	v->constlen = MIN2(255, MAX2(v->constlen, v->info.max_const + 1));

	fixup_regfootprint(v);

	return bin;
}

static void
assemble_variant(struct ir3_shader_variant *v)
{
	struct ir3_compiler *compiler = v->shader->compiler;
	struct shader_info *info = &v->shader->nir->info;
	uint32_t gpu_id = compiler->gpu_id;
	uint32_t sz, *bin;

	bin = ir3_shader_assemble(v, gpu_id);
	sz = v->info.sizedwords * 4;

	v->bo = fd_bo_new(compiler->dev, sz,
			DRM_FREEDRENO_GEM_CACHE_WCOMBINE |
			DRM_FREEDRENO_GEM_TYPE_KMEM,
			"%s:%s", ir3_shader_stage(v->shader), info->name);

	memcpy(fd_bo_map(v->bo), bin, sz);

	if (ir3_shader_debug & IR3_DBG_DISASM) {
		struct ir3_shader_key key = v->key;
		printf("disassemble: type=%d, k={bp=%u,cts=%u,hp=%u}", v->type,
			v->binning_pass, key.color_two_side, key.half_precision);
		ir3_shader_disasm(v, bin, stdout);
	}

	if (shader_debug_enabled(v->shader->type)) {
		fprintf(stderr, "Native code for unnamed %s shader %s:\n",
			_mesa_shader_stage_to_string(v->shader->type),
			v->shader->nir->info.name);
		if (v->shader->type == MESA_SHADER_FRAGMENT)
			fprintf(stderr, "SIMD0\n");
		ir3_shader_disasm(v, bin, stderr);
	}

	free(bin);

	/* no need to keep the ir around beyond this point: */
	ir3_destroy(v->ir);
	v->ir = NULL;
}

static struct ir3_shader_variant *
create_variant(struct ir3_shader *shader, struct ir3_shader_key *key,
		bool binning_pass)
{
	struct ir3_shader_variant *v = CALLOC_STRUCT(ir3_shader_variant);
	int ret;

	if (!v)
		return NULL;

	v->id = ++shader->variant_count;
	v->shader = shader;
	v->binning_pass = binning_pass;
	v->key = *key;
	v->type = shader->type;

	ret = ir3_compile_shader_nir(shader->compiler, v);
	if (ret) {
		debug_error("compile failed!");
		goto fail;
	}

	assemble_variant(v);
	if (!v->bo) {
		debug_error("assemble failed!");
		goto fail;
	}

	return v;

fail:
	delete_variant(v);
	return NULL;
}

static inline struct ir3_shader_variant *
shader_variant(struct ir3_shader *shader, struct ir3_shader_key *key,
		bool *created)
{
	struct ir3_shader_variant *v;

	*created = false;

	for (v = shader->variants; v; v = v->next)
		if (ir3_shader_key_equal(key, &v->key))
			return v;

	/* compile new variant if it doesn't exist already: */
	v = create_variant(shader, key, false);
	if (v) {
		v->next = shader->variants;
		shader->variants = v;
		*created = true;
	}

	return v;
}

struct ir3_shader_variant *
ir3_shader_get_variant(struct ir3_shader *shader, struct ir3_shader_key *key,
		bool binning_pass, bool *created)
{
	struct ir3_shader_variant *v =
			shader_variant(shader, key, created);

	if (v && binning_pass) {
		if (!v->binning)
			v->binning = create_variant(shader, key, true);
		return v->binning;
	}

	return v;
}

void
ir3_shader_destroy(struct ir3_shader *shader)
{
	struct ir3_shader_variant *v, *t;
	for (v = shader->variants; v; ) {
		t = v;
		v = v->next;
		delete_variant(t);
	}
	ralloc_free(shader->nir);
	free(shader);
}

struct ir3_shader *
ir3_shader_from_nir(struct ir3_compiler *compiler, nir_shader *nir)
{
	struct ir3_shader *shader = CALLOC_STRUCT(ir3_shader);

	shader->compiler = compiler;
	shader->id = ++shader->compiler->shader_count;
	shader->type = nir->info.stage;

	NIR_PASS_V(nir, nir_lower_io, nir_var_all, ir3_glsl_type_size,
			   (nir_lower_io_options)0);

	/* do first pass optimization, ignoring the key: */
	shader->nir = ir3_optimize_nir(shader, nir, NULL);
	if (ir3_shader_debug & IR3_DBG_DISASM) {
		printf("dump nir%d: type=%d", shader->id, shader->type);
		nir_print_shader(shader->nir, stdout);
	}

	return shader;
}

static void dump_reg(FILE *out, const char *name, uint32_t r)
{
	if (r != regid(63,0))
		fprintf(out, "; %s: r%d.%c\n", name, r >> 2, "xyzw"[r & 0x3]);
}

static void dump_output(FILE *out, struct ir3_shader_variant *so,
		unsigned slot, const char *name)
{
	uint32_t regid;
	regid = ir3_find_output_regid(so, slot);
	dump_reg(out, name, regid);
}

void
ir3_shader_disasm(struct ir3_shader_variant *so, uint32_t *bin, FILE *out)
{
	struct ir3 *ir = so->ir;
	struct ir3_register *reg;
	const char *type = ir3_shader_stage(so->shader);
	uint8_t regid;
	unsigned i;

	for (i = 0; i < ir->ninputs; i++) {
		if (!ir->inputs[i]) {
			fprintf(out, "; in%d unused\n", i);
			continue;
		}
		reg = ir->inputs[i]->regs[0];
		regid = reg->num;
		fprintf(out, "@in(%sr%d.%c)\tin%d\n",
				(reg->flags & IR3_REG_HALF) ? "h" : "",
				(regid >> 2), "xyzw"[regid & 0x3], i);
	}

	for (i = 0; i < ir->noutputs; i++) {
		if (!ir->outputs[i]) {
			fprintf(out, "; out%d unused\n", i);
			continue;
		}
		/* kill shows up as a virtual output.. skip it! */
		if (is_kill(ir->outputs[i]))
			continue;
		reg = ir->outputs[i]->regs[0];
		regid = reg->num;
		fprintf(out, "@out(%sr%d.%c)\tout%d\n",
				(reg->flags & IR3_REG_HALF) ? "h" : "",
				(regid >> 2), "xyzw"[regid & 0x3], i);
	}

	for (i = 0; i < so->immediates_count; i++) {
		fprintf(out, "@const(c%d.x)\t", so->constbase.immediate + i);
		fprintf(out, "0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
				so->immediates[i].val[0],
				so->immediates[i].val[1],
				so->immediates[i].val[2],
				so->immediates[i].val[3]);
	}

	disasm_a3xx(bin, so->info.sizedwords, 0, out, ir->compiler->gpu_id);

	switch (so->type) {
	case MESA_SHADER_VERTEX:
		fprintf(out, "; %s: outputs:", type);
		for (i = 0; i < so->outputs_count; i++) {
			uint8_t regid = so->outputs[i].regid;
			fprintf(out, " r%d.%c (%s)",
					(regid >> 2), "xyzw"[regid & 0x3],
					gl_varying_slot_name(so->outputs[i].slot));
		}
		fprintf(out, "\n");
		fprintf(out, "; %s: inputs:", type);
		for (i = 0; i < so->inputs_count; i++) {
			uint8_t regid = so->inputs[i].regid;
			fprintf(out, " r%d.%c (cm=%x,il=%u,b=%u)",
					(regid >> 2), "xyzw"[regid & 0x3],
					so->inputs[i].compmask,
					so->inputs[i].inloc,
					so->inputs[i].bary);
		}
		fprintf(out, "\n");
		break;
	case MESA_SHADER_FRAGMENT:
		fprintf(out, "; %s: outputs:", type);
		for (i = 0; i < so->outputs_count; i++) {
			uint8_t regid = so->outputs[i].regid;
			fprintf(out, " r%d.%c (%s)",
					(regid >> 2), "xyzw"[regid & 0x3],
					gl_frag_result_name(so->outputs[i].slot));
		}
		fprintf(out, "\n");
		fprintf(out, "; %s: inputs:", type);
		for (i = 0; i < so->inputs_count; i++) {
			uint8_t regid = so->inputs[i].regid;
			fprintf(out, " r%d.%c (%s,cm=%x,il=%u,b=%u)",
					(regid >> 2), "xyzw"[regid & 0x3],
					gl_varying_slot_name(so->inputs[i].slot),
					so->inputs[i].compmask,
					so->inputs[i].inloc,
					so->inputs[i].bary);
		}
		fprintf(out, "\n");
		break;
	default:
		/* TODO */
		break;
	}

	/* print generic shader info: */
	fprintf(out, "; %s prog %d/%d: %u instructions, %d half, %d full\n",
			type, so->shader->id, so->id,
			so->info.instrs_count,
			so->info.max_half_reg + 1,
			so->info.max_reg + 1);

	fprintf(out, "; %d const, %u constlen\n",
			so->info.max_const + 1,
			so->constlen);

	fprintf(out, "; %u (ss), %u (sy)\n", so->info.ss, so->info.sy);

	/* print shader type specific info: */
	switch (so->type) {
	case MESA_SHADER_VERTEX:
		dump_output(out, so, VARYING_SLOT_POS, "pos");
		dump_output(out, so, VARYING_SLOT_PSIZ, "psize");
		break;
	case MESA_SHADER_FRAGMENT:
		dump_reg(out, "pos (bary)",
			ir3_find_sysval_regid(so, SYSTEM_VALUE_VARYING_COORD));
		dump_output(out, so, FRAG_RESULT_DEPTH, "posz");
		if (so->color0_mrt) {
			dump_output(out, so, FRAG_RESULT_COLOR, "color");
		} else {
			dump_output(out, so, FRAG_RESULT_DATA0, "data0");
			dump_output(out, so, FRAG_RESULT_DATA1, "data1");
			dump_output(out, so, FRAG_RESULT_DATA2, "data2");
			dump_output(out, so, FRAG_RESULT_DATA3, "data3");
			dump_output(out, so, FRAG_RESULT_DATA4, "data4");
			dump_output(out, so, FRAG_RESULT_DATA5, "data5");
			dump_output(out, so, FRAG_RESULT_DATA6, "data6");
			dump_output(out, so, FRAG_RESULT_DATA7, "data7");
		}
		/* these two are hard-coded since we don't know how to
		 * program them to anything but all 0's...
		 */
		if (so->frag_coord)
			fprintf(out, "; fragcoord: r0.x\n");
		if (so->frag_face)
			fprintf(out, "; fragface: hr0.x\n");
		break;
	default:
		/* TODO */
		break;
	}

	fprintf(out, "\n");
}

uint64_t
ir3_shader_outputs(const struct ir3_shader *so)
{
	return so->nir->info.outputs_written;
}
