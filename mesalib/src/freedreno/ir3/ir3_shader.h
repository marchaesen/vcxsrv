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

#ifndef IR3_SHADER_H_
#define IR3_SHADER_H_

#include <stdio.h>

#include "compiler/shader_enums.h"
#include "compiler/nir/nir.h"
#include "util/bitscan.h"

#include "ir3.h"

struct glsl_type;

/* driver param indices: */
enum ir3_driver_param {
	/* compute shader driver params: */
	IR3_DP_NUM_WORK_GROUPS_X = 0,
	IR3_DP_NUM_WORK_GROUPS_Y = 1,
	IR3_DP_NUM_WORK_GROUPS_Z = 2,
	IR3_DP_LOCAL_GROUP_SIZE_X = 4,
	IR3_DP_LOCAL_GROUP_SIZE_Y = 5,
	IR3_DP_LOCAL_GROUP_SIZE_Z = 6,
	/* NOTE: gl_NumWorkGroups should be vec4 aligned because
	 * glDispatchComputeIndirect() needs to load these from
	 * the info->indirect buffer.  Keep that in mind when/if
	 * adding any addition CS driver params.
	 */
	IR3_DP_CS_COUNT   = 8,   /* must be aligned to vec4 */

	/* vertex shader driver params: */
	IR3_DP_VTXID_BASE = 0,
	IR3_DP_VTXCNT_MAX = 1,
	/* user-clip-plane components, up to 8x vec4's: */
	IR3_DP_UCP0_X     = 4,
	/* .... */
	IR3_DP_UCP7_W     = 35,
	IR3_DP_VS_COUNT   = 36   /* must be aligned to vec4 */
};

#define IR3_MAX_SHADER_BUFFERS   32
#define IR3_MAX_SHADER_IMAGES    32
#define IR3_MAX_SO_BUFFERS        4
#define IR3_MAX_SO_OUTPUTS       64

/**
 * For consts needed to pass internal values to shader which may or may not
 * be required, rather than allocating worst-case const space, we scan the
 * shader and allocate consts as-needed:
 *
 *   + SSBO sizes: only needed if shader has a get_buffer_size intrinsic
 *     for a given SSBO
 *
 *   + Image dimensions: needed to calculate pixel offset, but only for
 *     images that have a image_store intrinsic
 */
struct ir3_driver_const_layout {
	struct {
		uint32_t mask;  /* bitmask of SSBOs that have get_buffer_size */
		uint32_t count; /* number of consts allocated */
		/* one const allocated per SSBO which has get_buffer_size,
		 * ssbo_sizes.off[ssbo_id] is offset from start of ssbo_sizes
		 * consts:
		 */
		uint32_t off[IR3_MAX_SHADER_BUFFERS];
	} ssbo_size;

	struct {
		uint32_t mask;  /* bitmask of images that have image_store */
		uint32_t count; /* number of consts allocated */
		/* three const allocated per image which has image_store:
		 *  + cpp         (bytes per pixel)
		 *  + pitch       (y pitch)
		 *  + array_pitch (z pitch)
		 */
		uint32_t off[IR3_MAX_SHADER_IMAGES];
	} image_dims;
};

/**
 * A single output for vertex transform feedback.
 */
struct ir3_stream_output {
	unsigned register_index:6;  /**< 0 to 63 (OUT index) */
	unsigned start_component:2; /** 0 to 3 */
	unsigned num_components:3;  /** 1 to 4 */
	unsigned output_buffer:3;   /**< 0 to PIPE_MAX_SO_BUFFERS */
	unsigned dst_offset:16;     /**< offset into the buffer in dwords */
	unsigned stream:2;          /**< 0 to 3 */
};

/**
 * Stream output for vertex transform feedback.
 */
struct ir3_stream_output_info {
	unsigned num_outputs;
	/** stride for an entire vertex for each buffer in dwords */
	uint16_t stride[IR3_MAX_SO_BUFFERS];

	/**
	 * Array of stream outputs, in the order they are to be written in.
	 * Selected components are tightly packed into the output buffer.
	 */
	struct ir3_stream_output output[IR3_MAX_SO_OUTPUTS];
};

/* Configuration key used to identify a shader variant.. different
 * shader variants can be used to implement features not supported
 * in hw (two sided color), binning-pass vertex shader, etc.
 */
struct ir3_shader_key {
	union {
		struct {
			/*
			 * Combined Vertex/Fragment shader parameters:
			 */
			unsigned ucp_enables : 8;

			/* do we need to check {v,f}saturate_{s,t,r}? */
			unsigned has_per_samp : 1;

			/*
			 * Vertex shader variant parameters:
			 */
			unsigned vclamp_color : 1;

			/*
			 * Fragment shader variant parameters:
			 */
			unsigned color_two_side : 1;
			unsigned half_precision : 1;
			/* used when shader needs to handle flat varyings (a4xx)
			 * for front/back color inputs to frag shader:
			 */
			unsigned rasterflat : 1;
			unsigned fclamp_color : 1;
		};
		uint32_t global;
	};

	/* bitmask of sampler which needs coords clamped for vertex
	 * shader:
	 */
	uint16_t vsaturate_s, vsaturate_t, vsaturate_r;

	/* bitmask of sampler which needs coords clamped for frag
	 * shader:
	 */
	uint16_t fsaturate_s, fsaturate_t, fsaturate_r;

	/* bitmask of ms shifts */
	uint32_t vsamples, fsamples;

	/* bitmask of samplers which need astc srgb workaround: */
	uint16_t vastc_srgb, fastc_srgb;
};

static inline bool
ir3_shader_key_equal(struct ir3_shader_key *a, struct ir3_shader_key *b)
{
	/* slow-path if we need to check {v,f}saturate_{s,t,r} */
	if (a->has_per_samp || b->has_per_samp)
		return memcmp(a, b, sizeof(struct ir3_shader_key)) == 0;
	return a->global == b->global;
}

/* will the two keys produce different lowering for a fragment shader? */
static inline bool
ir3_shader_key_changes_fs(struct ir3_shader_key *key, struct ir3_shader_key *last_key)
{
	if (last_key->has_per_samp || key->has_per_samp) {
		if ((last_key->fsaturate_s != key->fsaturate_s) ||
				(last_key->fsaturate_t != key->fsaturate_t) ||
				(last_key->fsaturate_r != key->fsaturate_r) ||
				(last_key->fsamples != key->fsamples) ||
				(last_key->fastc_srgb != key->fastc_srgb))
			return true;
	}

	if (last_key->fclamp_color != key->fclamp_color)
		return true;

	if (last_key->color_two_side != key->color_two_side)
		return true;

	if (last_key->half_precision != key->half_precision)
		return true;

	if (last_key->rasterflat != key->rasterflat)
		return true;

	if (last_key->ucp_enables != key->ucp_enables)
		return true;

	return false;
}

/* will the two keys produce different lowering for a vertex shader? */
static inline bool
ir3_shader_key_changes_vs(struct ir3_shader_key *key, struct ir3_shader_key *last_key)
{
	if (last_key->has_per_samp || key->has_per_samp) {
		if ((last_key->vsaturate_s != key->vsaturate_s) ||
				(last_key->vsaturate_t != key->vsaturate_t) ||
				(last_key->vsaturate_r != key->vsaturate_r) ||
				(last_key->vsamples != key->vsamples) ||
				(last_key->vastc_srgb != key->vastc_srgb))
			return true;
	}

	if (last_key->vclamp_color != key->vclamp_color)
		return true;

	if (last_key->ucp_enables != key->ucp_enables)
		return true;

	return false;
}

/* clears shader-key flags which don't apply to the given shader
 * stage
 */
static inline void
ir3_normalize_key(struct ir3_shader_key *key, gl_shader_stage type)
{
	switch (type) {
	case MESA_SHADER_FRAGMENT:
		if (key->has_per_samp) {
			key->vsaturate_s = 0;
			key->vsaturate_t = 0;
			key->vsaturate_r = 0;
			key->vastc_srgb = 0;
			key->vsamples = 0;
		}
		break;
	case MESA_SHADER_VERTEX:
		key->color_two_side = false;
		key->half_precision = false;
		key->rasterflat = false;
		if (key->has_per_samp) {
			key->fsaturate_s = 0;
			key->fsaturate_t = 0;
			key->fsaturate_r = 0;
			key->fastc_srgb = 0;
			key->fsamples = 0;
		}
		break;
	default:
		/* TODO */
		break;
	}

}

struct ir3_shader_variant {
	struct fd_bo *bo;

	/* variant id (for debug) */
	uint32_t id;

	struct ir3_shader_key key;

	/* vertex shaders can have an extra version for hwbinning pass,
	 * which is pointed to by so->binning:
	 */
	bool binning_pass;
	struct ir3_shader_variant *binning;

	struct ir3_driver_const_layout const_layout;
	struct ir3_info info;
	struct ir3 *ir;

	/* Levels of nesting of flow control:
	 */
	unsigned branchstack;

	/* the instructions length is in units of instruction groups
	 * (4 instructions for a3xx, 16 instructions for a4xx.. each
	 * instruction is 2 dwords):
	 */
	unsigned instrlen;

	/* the constants length is in units of vec4's, and is the sum of
	 * the uniforms and the built-in compiler constants
	 */
	unsigned constlen;

	/* number of uniforms (in vec4), not including built-in compiler
	 * constants, etc.
	 */
	unsigned num_uniforms;

	unsigned num_ubos;

	/* About Linkage:
	 *   + Let the frag shader determine the position/compmask for the
	 *     varyings, since it is the place where we know if the varying
	 *     is actually used, and if so, which components are used.  So
	 *     what the hw calls "outloc" is taken from the "inloc" of the
	 *     frag shader.
	 *   + From the vert shader, we only need the output regid
	 */

	bool frag_coord, frag_face, color0_mrt;

	/* NOTE: for input/outputs, slot is:
	 *   gl_vert_attrib  - for VS inputs
	 *   gl_varying_slot - for VS output / FS input
	 *   gl_frag_result  - for FS output
	 */

	/* varyings/outputs: */
	unsigned outputs_count;
	struct {
		uint8_t slot;
		uint8_t regid;
	} outputs[16 + 2];  /* +POSITION +PSIZE */
	bool writes_pos, writes_psize;

	/* attributes (VS) / varyings (FS):
	 * Note that sysval's should come *after* normal inputs.
	 */
	unsigned inputs_count;
	struct {
		uint8_t slot;
		uint8_t regid;
		uint8_t compmask;
		uint8_t ncomp;
		/* location of input (ie. offset passed to bary.f, etc).  This
		 * matches the SP_VS_VPC_DST_REG.OUTLOCn value (a3xx and a4xx
		 * have the OUTLOCn value offset by 8, presumably to account
		 * for gl_Position/gl_PointSize)
		 */
		uint8_t inloc;
		/* vertex shader specific: */
		bool    sysval     : 1;   /* slot is a gl_system_value */
		/* fragment shader specific: */
		bool    bary       : 1;   /* fetched varying (vs one loaded into reg) */
		bool    rasterflat : 1;   /* special handling for emit->rasterflat */
		enum glsl_interp_mode interpolate;
	} inputs[16 + 2];  /* +POSITION +FACE */

	/* sum of input components (scalar).  For frag shaders, it only counts
	 * the varying inputs:
	 */
	unsigned total_in;

	/* For frag shaders, the total number of inputs (not scalar,
	 * ie. SP_VS_PARAM_REG.TOTALVSOUTVAR)
	 */
	unsigned varying_in;

	/* number of samplers/textures (which are currently 1:1): */
	int num_samp;

	/* do we have one or more SSBO instructions: */
	bool has_ssbo;

	/* do we have kill instructions: */
	bool has_kill;

	/* Layout of constant registers, each section (in vec4). Pointer size
	 * is 32b (a3xx, a4xx), or 64b (a5xx+), which effects the size of the
	 * UBO and stream-out consts.
	 */
	struct {
		/* user const start at zero */
		unsigned ubo;
		/* NOTE that a3xx might need a section for SSBO addresses too */
		unsigned ssbo_sizes;
		unsigned image_dims;
		unsigned driver_param;
		unsigned tfbo;
		unsigned immediate;
	} constbase;

	unsigned immediates_count;
	unsigned immediates_size;
	struct {
		uint32_t val[4];
	} *immediates;

	/* for astc srgb workaround, the number/base of additional
	 * alpha tex states we need, and index of original tex states
	 */
	struct {
		unsigned base, count;
		unsigned orig_idx[16];
	} astc_srgb;

	/* shader variants form a linked list: */
	struct ir3_shader_variant *next;

	/* replicated here to avoid passing extra ptrs everywhere: */
	gl_shader_stage type;
	struct ir3_shader *shader;
};

struct ir3_shader {
	gl_shader_stage type;

	/* shader id (for debug): */
	uint32_t id;
	uint32_t variant_count;

	/* so we know when we can disable TGSI related hacks: */
	bool from_tgsi;

	struct ir3_compiler *compiler;

	struct nir_shader *nir;
	struct ir3_stream_output_info stream_output;

	struct ir3_shader_variant *variants;
};

void * ir3_shader_assemble(struct ir3_shader_variant *v, uint32_t gpu_id);
struct ir3_shader_variant * ir3_shader_get_variant(struct ir3_shader *shader,
		struct ir3_shader_key *key, bool binning_pass, bool *created);
struct ir3_shader * ir3_shader_from_nir(struct ir3_compiler *compiler, nir_shader *nir);
void ir3_shader_destroy(struct ir3_shader *shader);
void ir3_shader_disasm(struct ir3_shader_variant *so, uint32_t *bin, FILE *out);
uint64_t ir3_shader_outputs(const struct ir3_shader *so);

int
ir3_glsl_type_size(const struct glsl_type *type);

static inline const char *
ir3_shader_stage(struct ir3_shader *shader)
{
	switch (shader->type) {
	case MESA_SHADER_VERTEX:     return "VERT";
	case MESA_SHADER_FRAGMENT:   return "FRAG";
	case MESA_SHADER_COMPUTE:    return "CL";
	default:
		unreachable("invalid type");
		return NULL;
	}
}

/*
 * Helper/util:
 */

static inline int
ir3_find_output(const struct ir3_shader_variant *so, gl_varying_slot slot)
{
	int j;

	for (j = 0; j < so->outputs_count; j++)
		if (so->outputs[j].slot == slot)
			return j;

	/* it seems optional to have a OUT.BCOLOR[n] for each OUT.COLOR[n]
	 * in the vertex shader.. but the fragment shader doesn't know this
	 * so  it will always have both IN.COLOR[n] and IN.BCOLOR[n].  So
	 * at link time if there is no matching OUT.BCOLOR[n], we must map
	 * OUT.COLOR[n] to IN.BCOLOR[n].  And visa versa if there is only
	 * a OUT.BCOLOR[n] but no matching OUT.COLOR[n]
	 */
	if (slot == VARYING_SLOT_BFC0) {
		slot = VARYING_SLOT_COL0;
	} else if (slot == VARYING_SLOT_BFC1) {
		slot = VARYING_SLOT_COL1;
	} else if (slot == VARYING_SLOT_COL0) {
		slot = VARYING_SLOT_BFC0;
	} else if (slot == VARYING_SLOT_COL1) {
		slot = VARYING_SLOT_BFC1;
	} else {
		return 0;
	}

	for (j = 0; j < so->outputs_count; j++)
		if (so->outputs[j].slot == slot)
			return j;

	debug_assert(0);

	return 0;
}

static inline int
ir3_next_varying(const struct ir3_shader_variant *so, int i)
{
	while (++i < so->inputs_count)
		if (so->inputs[i].compmask && so->inputs[i].bary)
			break;
	return i;
}

struct ir3_shader_linkage {
	uint8_t max_loc;
	uint8_t cnt;
	struct {
		uint8_t regid;
		uint8_t compmask;
		uint8_t loc;
	} var[32];
};

static inline void
ir3_link_add(struct ir3_shader_linkage *l, uint8_t regid, uint8_t compmask, uint8_t loc)
{
	int i = l->cnt++;

	debug_assert(i < ARRAY_SIZE(l->var));

	l->var[i].regid    = regid;
	l->var[i].compmask = compmask;
	l->var[i].loc      = loc;
	l->max_loc = MAX2(l->max_loc, loc + util_last_bit(compmask));
}

static inline void
ir3_link_shaders(struct ir3_shader_linkage *l,
		const struct ir3_shader_variant *vs,
		const struct ir3_shader_variant *fs)
{
	int j = -1, k;

	while (l->cnt < ARRAY_SIZE(l->var)) {
		j = ir3_next_varying(fs, j);

		if (j >= fs->inputs_count)
			break;

		if (fs->inputs[j].inloc >= fs->total_in)
			continue;

		k = ir3_find_output(vs, fs->inputs[j].slot);

		ir3_link_add(l, vs->outputs[k].regid,
			fs->inputs[j].compmask, fs->inputs[j].inloc);
	}
}

static inline uint32_t
ir3_find_output_regid(const struct ir3_shader_variant *so, unsigned slot)
{
	int j;
	for (j = 0; j < so->outputs_count; j++)
		if (so->outputs[j].slot == slot)
			return so->outputs[j].regid;
	return regid(63, 0);
}

static inline uint32_t
ir3_find_sysval_regid(const struct ir3_shader_variant *so, unsigned slot)
{
	int j;
	for (j = 0; j < so->inputs_count; j++)
		if (so->inputs[j].sysval && (so->inputs[j].slot == slot))
			return so->inputs[j].regid;
	return regid(63, 0);
}

/* calculate register footprint in terms of half-regs (ie. one full
 * reg counts as two half-regs).
 */
static inline uint32_t
ir3_shader_halfregs(const struct ir3_shader_variant *v)
{
	return (2 * (v->info.max_reg + 1)) + (v->info.max_half_reg + 1);
}

#endif /* IR3_SHADER_H_ */
