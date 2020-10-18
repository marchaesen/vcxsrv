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
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/format/u_format.h"

#include "drm/freedreno_drmif.h"

#include "ir3_shader.h"
#include "ir3_compiler.h"
#include "ir3_nir.h"

#include "disasm.h"

int
ir3_glsl_type_size(const struct glsl_type *type, bool bindless)
{
	return glsl_count_attribute_slots(type, false);
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
			int32_t regid = v->inputs[i].regid + n;
			if (v->inputs[i].half) {
				if (!v->mergedregs) {
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
		/* for ex, VS shaders with tess don't have normal varying outs: */
		if (!VALIDREG(v->outputs[i].regid))
			continue;
		int32_t regid = v->outputs[i].regid + 3;
		if (v->outputs[i].half) {
			if (!v->mergedregs) {
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
			if (!v->mergedregs) {
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
void * ir3_shader_assemble(struct ir3_shader_variant *v)
{
	const struct ir3_compiler *compiler = v->shader->compiler;
	void *bin;

	bin = ir3_assemble(v);
	if (!bin)
		return NULL;

	/* NOTE: if relative addressing is used, we set constlen in
	 * the compiler (to worst-case value) since we don't know in
	 * the assembler what the max addr reg value can be:
	 */
	v->constlen = MAX2(v->constlen, v->info.max_const + 1);

	/* On a4xx and newer, constlen must be a multiple of 16 dwords even though
	 * uploads are in units of 4 dwords. Round it up here to make calculations
	 * regarding the shared constlen simpler.
	 */
	if (compiler->gpu_id >= 400)
		v->constlen = align(v->constlen, 4);

	fixup_regfootprint(v);

	return bin;
}

static void
assemble_variant(struct ir3_shader_variant *v)
{
	v->bin = ir3_shader_assemble(v);

	if (shader_debug_enabled(v->shader->type)) {
		fprintf(stdout, "Native code for unnamed %s shader %s:\n",
			ir3_shader_stage(v), v->shader->nir->info.name);
		if (v->shader->type == MESA_SHADER_FRAGMENT)
			fprintf(stdout, "SIMD0\n");
		ir3_shader_disasm(v, v->bin, stdout);
	}

	/* no need to keep the ir around beyond this point: */
	ir3_destroy(v->ir);
	v->ir = NULL;
}

static bool
compile_variant(struct ir3_shader_variant *v)
{
	int ret = ir3_compile_shader_nir(v->shader->compiler, v);
	if (ret) {
		_debug_printf("compile failed! (%s:%s)", v->shader->nir->info.name,
				v->shader->nir->info.label);
		return false;
	}

	assemble_variant(v);
	if (!v->bin) {
		_debug_printf("assemble failed! (%s:%s)", v->shader->nir->info.name,
				v->shader->nir->info.label);
		return false;
	}

	return true;
}

/*
 * For creating normal shader variants, 'nonbinning' is NULL.  For
 * creating binning pass shader, it is link to corresponding normal
 * (non-binning) variant.
 */
static struct ir3_shader_variant *
alloc_variant(struct ir3_shader *shader, const struct ir3_shader_key *key,
		struct ir3_shader_variant *nonbinning)
{
	void *mem_ctx = shader;
	/* hang the binning variant off it's non-binning counterpart instead
	 * of the shader, to simplify the error cleanup paths
	 */
	if (nonbinning)
		mem_ctx = nonbinning;
	struct ir3_shader_variant *v = rzalloc_size(mem_ctx, sizeof(*v));

	if (!v)
		return NULL;

	v->id = ++shader->variant_count;
	v->shader = shader;
	v->binning_pass = !!nonbinning;
	v->nonbinning = nonbinning;
	v->key = *key;
	v->type = shader->type;
	v->mergedregs = shader->compiler->gpu_id >= 600;

	if (!v->binning_pass)
		v->const_state = rzalloc_size(v, sizeof(*v->const_state));

	return v;
}

static bool
needs_binning_variant(struct ir3_shader_variant *v)
{
	if ((v->type == MESA_SHADER_VERTEX) && ir3_has_binning_vs(&v->key))
		return true;
	return false;
}

static struct ir3_shader_variant *
create_variant(struct ir3_shader *shader, const struct ir3_shader_key *key)
{
	struct ir3_shader_variant *v = alloc_variant(shader, key, NULL);

	if (!v)
		goto fail;

	if (needs_binning_variant(v)) {
		v->binning = alloc_variant(shader, key, v);
		if (!v->binning)
			goto fail;
	}

	if (ir3_disk_cache_retrieve(shader->compiler, v))
		return v;

	if (!shader->nir_finalized) {
		ir3_nir_post_finalize(shader->compiler, shader->nir);

		if (ir3_shader_debug & IR3_DBG_DISASM) {
			printf("dump nir%d: type=%d", shader->id, shader->type);
			nir_print_shader(shader->nir, stdout);
		}

		shader->nir_finalized = true;
	}

	if (!compile_variant(v))
		goto fail;

	if (needs_binning_variant(v) && !compile_variant(v->binning))
		goto fail;

	ir3_disk_cache_store(shader->compiler, v);

	return v;

fail:
	ralloc_free(v);
	return NULL;
}

static inline struct ir3_shader_variant *
shader_variant(struct ir3_shader *shader, const struct ir3_shader_key *key)
{
	struct ir3_shader_variant *v;

	for (v = shader->variants; v; v = v->next)
		if (ir3_shader_key_equal(key, &v->key))
			return v;

	return NULL;
}

struct ir3_shader_variant *
ir3_shader_get_variant(struct ir3_shader *shader, const struct ir3_shader_key *key,
		bool binning_pass, bool *created)
{
	mtx_lock(&shader->variants_lock);
	struct ir3_shader_variant *v = shader_variant(shader, key);

	if (!v) {
		/* compile new variant if it doesn't exist already: */
		v = create_variant(shader, key);
		if (v) {
			v->next = shader->variants;
			shader->variants = v;
			*created = true;
		}
	}

	if (v && binning_pass) {
		v = v->binning;
		assert(v);
	}

	mtx_unlock(&shader->variants_lock);

	return v;
}

void
ir3_shader_destroy(struct ir3_shader *shader)
{
	ralloc_free(shader->nir);
	mtx_destroy(&shader->variants_lock);
	ralloc_free(shader);
}

/**
 * Creates a bitmask of the used bits of the shader key by this particular
 * shader.  Used by the gallium driver to skip state-dependent recompiles when
 * possible.
 */
static void
ir3_setup_used_key(struct ir3_shader *shader)
{
	nir_shader *nir = shader->nir;
	struct shader_info *info = &nir->info;
	struct ir3_shader_key *key = &shader->key_mask;

	/* This key flag is just used to make for a cheaper ir3_shader_key_equal
	 * check in the common case.
	 */
	key->has_per_samp = true;

	key->safe_constlen = true;

	key->ucp_enables = 0xff;

	if (info->stage == MESA_SHADER_FRAGMENT) {
		key->fsaturate_s = ~0;
		key->fsaturate_t = ~0;
		key->fsaturate_r = ~0;
		key->fastc_srgb = ~0;
		key->fsamples = ~0;

		if (info->inputs_read & VARYING_BITS_COLOR) {
			key->rasterflat = true;
			key->color_two_side = true;
		}

		if (info->inputs_read & VARYING_BIT_LAYER) {
			key->layer_zero = true;
		}

		if (info->inputs_read & VARYING_BIT_VIEWPORT) {
			key->view_zero = true;
		}

		if ((info->outputs_written & ~(FRAG_RESULT_DEPTH |
								FRAG_RESULT_STENCIL |
								FRAG_RESULT_SAMPLE_MASK)) != 0) {
			key->fclamp_color = true;
		}

		/* Only used for deciding on behavior of
		 * nir_intrinsic_load_barycentric_sample
		 */
		key->msaa = info->fs.uses_sample_qualifier;
	} else {
		key->tessellation = ~0;
		key->has_gs = true;

		if (info->outputs_written & VARYING_BITS_COLOR)
			key->vclamp_color = true;

		if (info->stage == MESA_SHADER_VERTEX) {
			key->vsaturate_s = ~0;
			key->vsaturate_t = ~0;
			key->vsaturate_r = ~0;
			key->vastc_srgb = ~0;
			key->vsamples = ~0;
		}
	}
}


/* Given an array of constlen's, decrease some of them so that the sum stays
 * within "combined_limit" while trying to fairly share the reduction. Returns
 * a bitfield of which stages should be trimmed.
 */
static uint32_t
trim_constlens(unsigned *constlens,
			   unsigned first_stage, unsigned last_stage,
			   unsigned combined_limit, unsigned safe_limit)
{
   unsigned cur_total = 0;
   for (unsigned i = first_stage; i <= last_stage; i++) {
      cur_total += constlens[i];
   }

   unsigned max_stage = 0;
   unsigned max_const = 0;
   uint32_t trimmed = 0;

   while (cur_total > combined_limit) {
	   for (unsigned i = first_stage; i <= last_stage; i++) {
		   if (constlens[i] >= max_const) {
			   max_stage = i;
			   max_const = constlens[i];
		   }
	   }

	   assert(max_const > safe_limit);
	   trimmed |= 1 << max_stage;
	   cur_total = cur_total - max_const + safe_limit;
	   constlens[max_stage] = safe_limit;
   }

   return trimmed;
}

/* Figures out which stages in the pipeline to use the "safe" constlen for, in
 * order to satisfy all shared constlen limits.
 */
uint32_t
ir3_trim_constlen(struct ir3_shader_variant **variants,
				  const struct ir3_compiler *compiler)
{
	unsigned constlens[MESA_SHADER_STAGES] = {};

	for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
		if (variants[i])
			constlens[i] = variants[i]->constlen;
	}

	uint32_t trimmed = 0;
	STATIC_ASSERT(MESA_SHADER_STAGES <= 8 * sizeof(trimmed));

	/* There are two shared limits to take into account, the geometry limit on
	 * a6xx and the total limit. The frag limit on a6xx only matters for a
	 * single stage, so it's always satisfied with the first variant.
	 */
	if (compiler->gpu_id >= 600) {
		trimmed |=
			trim_constlens(constlens, MESA_SHADER_VERTEX, MESA_SHADER_GEOMETRY,
						   compiler->max_const_geom, compiler->max_const_safe);
	}
	trimmed |=
		trim_constlens(constlens, MESA_SHADER_VERTEX, MESA_SHADER_FRAGMENT,
					   compiler->max_const_pipeline, compiler->max_const_safe);

	return trimmed;
}

struct ir3_shader *
ir3_shader_from_nir(struct ir3_compiler *compiler, nir_shader *nir,
		unsigned reserved_user_consts, struct ir3_stream_output_info *stream_output)
{
	struct ir3_shader *shader = rzalloc_size(NULL, sizeof(*shader));

	mtx_init(&shader->variants_lock, mtx_plain);
	shader->compiler = compiler;
	shader->id = p_atomic_inc_return(&shader->compiler->shader_count);
	shader->type = nir->info.stage;
	if (stream_output)
		memcpy(&shader->stream_output, stream_output, sizeof(shader->stream_output));
	shader->num_reserved_user_consts = reserved_user_consts;
	shader->nir = nir;

	ir3_disk_cache_init_shader_key(compiler, shader);

	ir3_setup_used_key(shader);

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

	foreach_input_n (instr, i, ir) {
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
		fprintf(out, "@tex(%sr%d.%c)\tsrc=%u, samp=%u, tex=%u, wrmask=0x%x, cmd=%u\n",
				fetch->half_precision ? "h" : "",
				fetch->dst >> 2, "xyzw"[fetch->dst & 0x3],
				fetch->src, fetch->samp_id, fetch->tex_id,
				fetch->wrmask, fetch->cmd);
	}

	foreach_output_n (instr, i, ir) {
		reg = instr->regs[0];
		regid = reg->num;
		fprintf(out, "@out(%sr%d.%c)\tout%d",
				(reg->flags & IR3_REG_HALF) ? "h" : "",
				(regid >> 2), "xyzw"[regid & 0x3], i);
		if (reg->wrmask > 0x1)
			fprintf(out, " (wrmask=0x%x)", reg->wrmask);
		fprintf(out, "\n");
	}

	const struct ir3_const_state *const_state = ir3_const_state(so);
	for (i = 0; i < DIV_ROUND_UP(const_state->immediates_count, 4); i++) {
		fprintf(out, "@const(c%d.x)\t", const_state->offsets.immediate + i);
		fprintf(out, "0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
				const_state->immediates[i * 4 + 0],
				const_state->immediates[i * 4 + 1],
				const_state->immediates[i * 4 + 2],
				const_state->immediates[i * 4 + 3]);
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
	fprintf(out, "; %s prog %d/%d: %u instr, %u nops, %u non-nops, %u mov, %u cov, %u dwords\n",
			type, so->shader->id, so->id,
			so->info.instrs_count,
			so->info.nops_count,
			so->info.instrs_count - so->info.nops_count,
			so->info.mov_count, so->info.cov_count,
			so->info.sizedwords);

	fprintf(out, "; %s prog %d/%d: %u last-baryf, %d half, %d full, %u constlen\n",
			type, so->shader->id, so->id,
			so->info.last_baryf,
			so->info.max_half_reg + 1,
			so->info.max_reg + 1,
			so->constlen);

	fprintf(out, "; %s prog %d/%d: %u cat0, %u cat1, %u cat2, %u cat3, %u cat4, %u cat5, %u cat6, %u cat7, \n",
			type, so->shader->id, so->id,
			so->info.instrs_per_cat[0],
			so->info.instrs_per_cat[1],
			so->info.instrs_per_cat[2],
			so->info.instrs_per_cat[3],
			so->info.instrs_per_cat[4],
			so->info.instrs_per_cat[5],
			so->info.instrs_per_cat[6],
			so->info.instrs_per_cat[7]);

	fprintf(out, "; %s prog %d/%d: %u sstall, %u (ss), %u (sy), %d max_sun, %d loops\n",
			type, so->shader->id, so->id,
			so->info.sstall,
			so->info.ss,
			so->info.sy,
			so->max_sun,
			so->loops);

	/* print shader type specific info: */
	switch (so->type) {
	case MESA_SHADER_VERTEX:
		dump_output(out, so, VARYING_SLOT_POS, "pos");
		dump_output(out, so, VARYING_SLOT_PSIZ, "psize");
		break;
	case MESA_SHADER_FRAGMENT:
		dump_reg(out, "pos (ij_pixel)",
			ir3_find_sysval_regid(so, SYSTEM_VALUE_BARYCENTRIC_PERSP_PIXEL));
		dump_reg(out, "pos (ij_centroid)",
			ir3_find_sysval_regid(so, SYSTEM_VALUE_BARYCENTRIC_PERSP_CENTROID));
		dump_reg(out, "pos (ij_size)",
			ir3_find_sysval_regid(so, SYSTEM_VALUE_BARYCENTRIC_PERSP_SIZE));
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
		dump_reg(out, "fragcoord",
			ir3_find_sysval_regid(so, SYSTEM_VALUE_FRAG_COORD));
		dump_reg(out, "fragface",
			ir3_find_sysval_regid(so, SYSTEM_VALUE_FRONT_FACE));
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
