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

#include "util/u_atomic.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/format/u_format.h"

#include "drm/freedreno_drmif.h"

#include "ir3_shader.h"
#include "ir3_compiler.h"
#include "ir3_nir.h"

int
ir3_glsl_type_size(const struct glsl_type *type, bool bindless)
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
fixup_regfootprint(struct ir3_shader_variant *v, uint32_t gpu_id)
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
			int32_t regid = v->inputs[i].regid + n;
			if (v->inputs[i].half) {
				if (gpu_id < 500) {
					v->info.max_half_reg = MAX2(v->info.max_half_reg, regid >> 2);
				} else {
					v->info.max_reg = MAX2(v->info.max_reg, regid >> 3);
				}
			} else {
				v->info.max_reg = MAX2(v->info.max_reg, regid >> 2);
			}
		}
	}

	for (i = 0; i < v->outputs_count; i++) {
		int32_t regid = v->outputs[i].regid + 3;
		if (v->outputs[i].half) {
			if (gpu_id < 500) {
				v->info.max_half_reg = MAX2(v->info.max_half_reg, regid >> 2);
			} else {
				v->info.max_reg = MAX2(v->info.max_reg, regid >> 3);
			}
		} else {
			v->info.max_reg = MAX2(v->info.max_reg, regid >> 2);
		}
	}

	for (i = 0; i < v->num_sampler_prefetch; i++) {
		unsigned n = util_last_bit(v->sampler_prefetch[i].wrmask) - 1;
		int32_t regid = v->sampler_prefetch[i].dst + n;
		if (v->sampler_prefetch[i].half_precision) {
			if (gpu_id < 500) {
				v->info.max_half_reg = MAX2(v->info.max_half_reg, regid >> 2);
			} else {
				v->info.max_reg = MAX2(v->info.max_reg, regid >> 3);
			}
		} else {
			v->info.max_reg = MAX2(v->info.max_reg, regid >> 2);
		}
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
	v->constlen = MAX2(v->constlen, v->info.max_const + 1);

	fixup_regfootprint(v, gpu_id);

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
			"%s:%s", ir3_shader_stage(v), info->name);

	memcpy(fd_bo_map(v->bo), bin, sz);

	if (shader_debug_enabled(v->shader->type)) {
		fprintf(stdout, "Native code for unnamed %s shader %s:\n",
			ir3_shader_stage(v), v->shader->nir->info.name);
		if (v->shader->type == MESA_SHADER_FRAGMENT)
			fprintf(stdout, "SIMD0\n");
		ir3_shader_disasm(v, bin, stdout);
	}

	free(bin);

	/* no need to keep the ir around beyond this point: */
	ir3_destroy(v->ir);
	v->ir = NULL;
}

/*
 * For creating normal shader variants, 'nonbinning' is NULL.  For
 * creating binning pass shader, it is link to corresponding normal
 * (non-binning) variant.
 */
static struct ir3_shader_variant *
create_variant(struct ir3_shader *shader, struct ir3_shader_key *key,
		struct ir3_shader_variant *nonbinning)
{
	struct ir3_shader_variant *v = CALLOC_STRUCT(ir3_shader_variant);
	int ret;

	if (!v)
		return NULL;

	v->id = ++shader->variant_count;
	v->shader = shader;
	v->binning_pass = !!nonbinning;
	v->nonbinning = nonbinning;
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
	v = create_variant(shader, key, NULL);
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
	mtx_lock(&shader->variants_lock);
	struct ir3_shader_variant *v =
			shader_variant(shader, key, created);

	if (v && binning_pass) {
		if (!v->binning) {
			v->binning = create_variant(shader, key, v);
			*created = true;
		}
		mtx_unlock(&shader->variants_lock);
		return v->binning;
	}
	mtx_unlock(&shader->variants_lock);

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
	free(shader->const_state.immediates);
	ralloc_free(shader->nir);
	mtx_destroy(&shader->variants_lock);
	free(shader);
}

struct ir3_shader *
ir3_shader_from_nir(struct ir3_compiler *compiler, nir_shader *nir)
{
	struct ir3_shader *shader = CALLOC_STRUCT(ir3_shader);

	mtx_init(&shader->variants_lock, mtx_plain);
	shader->compiler = compiler;
	shader->id = p_atomic_inc_return(&shader->compiler->shader_count);
	shader->type = nir->info.stage;

	NIR_PASS_V(nir, nir_lower_io, nir_var_all, ir3_glsl_type_size,
			   (nir_lower_io_options)0);

	if (nir->info.stage == MESA_SHADER_FRAGMENT) {
		/* NOTE: lower load_barycentric_at_sample first, since it
		 * produces load_barycentric_at_offset:
		 */
		NIR_PASS_V(nir, ir3_nir_lower_load_barycentric_at_sample);
		NIR_PASS_V(nir, ir3_nir_lower_load_barycentric_at_offset);

		NIR_PASS_V(nir, ir3_nir_move_varying_inputs);
	}

	NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);

	NIR_PASS_V(nir, nir_lower_amul, ir3_glsl_type_size);

	/* do first pass optimization, ignoring the key: */
	ir3_optimize_nir(shader, nir, NULL);

	shader->nir = nir;
	if (ir3_shader_debug & IR3_DBG_DISASM) {
		printf("dump nir%d: type=%d", shader->id, shader->type);
		nir_print_shader(shader->nir, stdout);
	}

	return shader;
}

static void dump_reg(FILE *out, const char *name, uint32_t r)
{
	if (r != regid(63,0)) {
		const char *reg_type = (r & HALF_REG_ID) ? "hr" : "r";
		fprintf(out, "; %s: %s%d.%c\n", name, reg_type,
				(r & ~HALF_REG_ID) >> 2, "xyzw"[r & 0x3]);
	}
}

static void dump_output(FILE *out, struct ir3_shader_variant *so,
		unsigned slot, const char *name)
{
	uint32_t regid;
	regid = ir3_find_output_regid(so, slot);
	dump_reg(out, name, regid);
}

static const char *
input_name(struct ir3_shader_variant *so, int i)
{
	if (so->inputs[i].sysval) {
		return gl_system_value_name(so->inputs[i].slot);
	} else if (so->type == MESA_SHADER_VERTEX) {
		return gl_vert_attrib_name(so->inputs[i].slot);
	} else {
		return gl_varying_slot_name(so->inputs[i].slot);
	}
}

static const char *
output_name(struct ir3_shader_variant *so, int i)
{
	if (so->type == MESA_SHADER_FRAGMENT) {
		return gl_frag_result_name(so->outputs[i].slot);
	} else {
		switch (so->outputs[i].slot) {
		case VARYING_SLOT_GS_HEADER_IR3:
			return "GS_HEADER";
		case VARYING_SLOT_GS_VERTEX_FLAGS_IR3:
			return "GS_VERTEX_FLAGS";
		case VARYING_SLOT_TCS_HEADER_IR3:
			return "TCS_HEADER";
		default:
			return gl_varying_slot_name(so->outputs[i].slot);
		}
	}
}

void
ir3_shader_disasm(struct ir3_shader_variant *so, uint32_t *bin, FILE *out)
{
	struct ir3 *ir = so->ir;
	struct ir3_register *reg;
	const char *type = ir3_shader_stage(so);
	uint8_t regid;
	unsigned i;

	struct ir3_instruction *instr;
	foreach_input_n(instr, i, ir) {
		reg = instr->regs[0];
		regid = reg->num;
		fprintf(out, "@in(%sr%d.%c)\tin%d",
				(reg->flags & IR3_REG_HALF) ? "h" : "",
				(regid >> 2), "xyzw"[regid & 0x3], i);

		if (reg->wrmask > 0x1)
			fprintf(out, " (wrmask=0x%x)", reg->wrmask);
		fprintf(out, "\n");
	}

	/* print pre-dispatch texture fetches: */
	for (i = 0; i < so->num_sampler_prefetch; i++) {
		const struct ir3_sampler_prefetch *fetch = &so->sampler_prefetch[i];
		fprintf(out, "@tex(%sr%d.%c)\tsrc=%u, samp=%u, tex=%u, wrmask=%x, cmd=%u\n",
				fetch->half_precision ? "h" : "",
				fetch->dst >> 2, "xyzw"[fetch->dst & 0x3],
				fetch->src, fetch->samp_id, fetch->tex_id,
				fetch->wrmask, fetch->cmd);
	}

	foreach_output_n(instr, i, ir) {
		reg = instr->regs[0];
		regid = reg->num;
		fprintf(out, "@out(%sr%d.%c)\tout%d",
				(reg->flags & IR3_REG_HALF) ? "h" : "",
				(regid >> 2), "xyzw"[regid & 0x3], i);
		if (reg->wrmask > 0x1)
			fprintf(out, " (wrmask=0x%x)", reg->wrmask);
		fprintf(out, "\n");
	}

	struct ir3_const_state *const_state = &so->shader->const_state;
	for (i = 0; i < const_state->immediates_count; i++) {
		fprintf(out, "@const(c%d.x)\t", const_state->offsets.immediate + i);
		fprintf(out, "0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
				const_state->immediates[i].val[0],
				const_state->immediates[i].val[1],
				const_state->immediates[i].val[2],
				const_state->immediates[i].val[3]);
	}

	disasm_a3xx(bin, so->info.sizedwords, 0, out, ir->compiler->gpu_id);

	fprintf(out, "; %s: outputs:", type);
	for (i = 0; i < so->outputs_count; i++) {
		uint8_t regid = so->outputs[i].regid;
		const char *reg_type = so->outputs[i].half ? "hr" : "r";
		fprintf(out, " %s%d.%c (%s)",
				reg_type, (regid >> 2), "xyzw"[regid & 0x3],
				output_name(so, i));
	}
	fprintf(out, "\n");

	fprintf(out, "; %s: inputs:", type);
	for (i = 0; i < so->inputs_count; i++) {
		uint8_t regid = so->inputs[i].regid;
		fprintf(out, " r%d.%c (%s slot=%d cm=%x,il=%u,b=%u)",
				(regid >> 2), "xyzw"[regid & 0x3],
				input_name(so, i),
				so->inputs[i].slot,
				so->inputs[i].compmask,
				so->inputs[i].inloc,
				so->inputs[i].bary);
	}
	fprintf(out, "\n");

	/* print generic shader info: */
	fprintf(out, "; %s prog %d/%d: %u instructions, %d half, %d full\n",
			type, so->shader->id, so->id,
			so->info.instrs_count,
			so->info.max_half_reg + 1,
			so->info.max_reg + 1);

	fprintf(out, "; %u constlen\n", so->constlen);

	fprintf(out, "; %u (ss), %u (sy)\n", so->info.ss, so->info.sy);

	fprintf(out, "; max_sun=%u\n", ir->max_sun);

	/* print shader type specific info: */
	switch (so->type) {
	case MESA_SHADER_VERTEX:
		dump_output(out, so, VARYING_SLOT_POS, "pos");
		dump_output(out, so, VARYING_SLOT_PSIZ, "psize");
		break;
	case MESA_SHADER_FRAGMENT:
		dump_reg(out, "pos (ij_pixel)",
			ir3_find_sysval_regid(so, SYSTEM_VALUE_BARYCENTRIC_PIXEL));
		dump_reg(out, "pos (ij_centroid)",
			ir3_find_sysval_regid(so, SYSTEM_VALUE_BARYCENTRIC_CENTROID));
		dump_reg(out, "pos (ij_size)",
			ir3_find_sysval_regid(so, SYSTEM_VALUE_BARYCENTRIC_SIZE));
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
