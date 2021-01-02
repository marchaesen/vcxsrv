/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "r600_sq.h"
#include "r600_formats.h"
#include "r600_opcodes.h"
#include "r600_shader.h"
#include "r600_dump.h"
#include "r600d.h"
#include "sfn/sfn_nir.h"

#include "sb/sb_public.h"

#include "pipe/p_shader_tokens.h"
#include "tgsi/tgsi_info.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_scan.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_from_mesa.h"
#include "nir/tgsi_to_nir.h"
#include "nir/nir_to_tgsi_info.h"
#include "compiler/nir/nir.h"
#include "util/u_bitcast.h"
#include "util/u_memory.h"
#include "util/u_math.h"
#include <stdio.h>
#include <errno.h>

/* CAYMAN notes
Why CAYMAN got loops for lots of instructions is explained here.

-These 8xx t-slot only ops are implemented in all vector slots.
MUL_LIT, FLT_TO_UINT, INT_TO_FLT, UINT_TO_FLT
These 8xx t-slot only opcodes become vector ops, with all four
slots expecting the arguments on sources a and b. Result is
broadcast to all channels.
MULLO_INT, MULHI_INT, MULLO_UINT, MULHI_UINT, MUL_64
These 8xx t-slot only opcodes become vector ops in the z, y, and
x slots.
EXP_IEEE, LOG_IEEE/CLAMPED, RECIP_IEEE/CLAMPED/FF/INT/UINT/_64/CLAMPED_64
RECIPSQRT_IEEE/CLAMPED/FF/_64/CLAMPED_64
SQRT_IEEE/_64
SIN/COS
The w slot may have an independent co-issued operation, or if the
result is required to be in the w slot, the opcode above may be
issued in the w slot as well.
The compiler must issue the source argument to slots z, y, and x
*/

/* Contents of r0 on entry to various shaders

 VS - .x = VertexID
      .y = RelVertexID (??)
      .w = InstanceID

 GS - r0.xyw, r1.xyz = per-vertex offsets
      r0.z = PrimitiveID

 TCS - .x = PatchID
       .y = RelPatchID (??)
       .z = InvocationID
       .w = tess factor base.

 TES - .x = TessCoord.x
     - .y = TessCoord.y
     - .z = RelPatchID (??)
     - .w = PrimitiveID

 PS - face_gpr.z = SampleMask
      face_gpr.w = SampleID
*/
#define R600_SHADER_BUFFER_INFO_SEL (512 + R600_BUFFER_INFO_OFFSET / 16)
static int r600_shader_from_tgsi(struct r600_context *rctx,
				 struct r600_pipe_shader *pipeshader,
				 union r600_shader_key key);

static void r600_add_gpr_array(struct r600_shader *ps, int start_gpr,
                           int size, unsigned comp_mask) {

	if (!size)
		return;

	if (ps->num_arrays == ps->max_arrays) {
		ps->max_arrays += 64;
		ps->arrays = realloc(ps->arrays, ps->max_arrays *
		                     sizeof(struct r600_shader_array));
	}

	int n = ps->num_arrays;
	++ps->num_arrays;

	ps->arrays[n].comp_mask = comp_mask;
	ps->arrays[n].gpr_start = start_gpr;
	ps->arrays[n].gpr_count = size;
}

static void r600_dump_streamout(struct pipe_stream_output_info *so)
{
	unsigned i;

	fprintf(stderr, "STREAMOUT\n");
	for (i = 0; i < so->num_outputs; i++) {
		unsigned mask = ((1 << so->output[i].num_components) - 1) <<
				so->output[i].start_component;
		fprintf(stderr, "  %i: MEM_STREAM%d_BUF%i[%i..%i] <- OUT[%i].%s%s%s%s%s\n",
			i,
			so->output[i].stream,
			so->output[i].output_buffer,
			so->output[i].dst_offset, so->output[i].dst_offset + so->output[i].num_components - 1,
			so->output[i].register_index,
			mask & 1 ? "x" : "",
		        mask & 2 ? "y" : "",
		        mask & 4 ? "z" : "",
		        mask & 8 ? "w" : "",
			so->output[i].dst_offset < so->output[i].start_component ? " (will lower)" : "");
	}
}

static int store_shader(struct pipe_context *ctx,
			struct r600_pipe_shader *shader)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	uint32_t *ptr, i;

	if (shader->bo == NULL) {
		shader->bo = (struct r600_resource*)
			pipe_buffer_create(ctx->screen, 0, PIPE_USAGE_IMMUTABLE, shader->shader.bc.ndw * 4);
		if (shader->bo == NULL) {
			return -ENOMEM;
		}
		ptr = r600_buffer_map_sync_with_rings(
			&rctx->b, shader->bo,
			PIPE_MAP_WRITE | RADEON_MAP_TEMPORARY);
		if (R600_BIG_ENDIAN) {
			for (i = 0; i < shader->shader.bc.ndw; ++i) {
				ptr[i] = util_cpu_to_le32(shader->shader.bc.bytecode[i]);
			}
		} else {
			memcpy(ptr, shader->shader.bc.bytecode, shader->shader.bc.ndw * sizeof(*ptr));
		}
		rctx->b.ws->buffer_unmap(shader->bo->buf);
	}

	return 0;
}

extern const struct nir_shader_compiler_options r600_nir_options;
static int nshader = 0;
int r600_pipe_shader_create(struct pipe_context *ctx,
			    struct r600_pipe_shader *shader,
			    union r600_shader_key key)
{
	struct r600_context *rctx = (struct r600_context *)ctx;
	struct r600_pipe_shader_selector *sel = shader->selector;
	int r;
	struct r600_screen *rscreen = (struct r600_screen *)ctx->screen;
	
	int processor = sel->ir_type == PIPE_SHADER_IR_TGSI ?
		tgsi_get_processor_type(sel->tokens):
		pipe_shader_type_from_mesa(sel->nir->info.stage);
	
	bool dump = r600_can_dump_shader(&rctx->screen->b, processor);
	unsigned use_sb = !(rctx->screen->b.debug_flags & DBG_NO_SB) &&
		!(rscreen->b.debug_flags & DBG_NIR);
	unsigned sb_disasm;
	unsigned export_shader;
	
	shader->shader.bc.isa = rctx->isa;
	
	if (!(rscreen->b.debug_flags & DBG_NIR)) {
		assert(sel->ir_type == PIPE_SHADER_IR_TGSI);
		r = r600_shader_from_tgsi(rctx, shader, key);
		if (r) {
			R600_ERR("translation from TGSI failed !\n");
			goto error;
		}
	} else {
		if (sel->ir_type == PIPE_SHADER_IR_TGSI) {
			sel->nir = tgsi_to_nir(sel->tokens, ctx->screen, true);
			/* Lower int64 ops because we have some r600 build-in shaders that use it */
			if (!ctx->screen->get_param(ctx->screen, PIPE_CAP_DOUBLES)) {
				NIR_PASS_V(sel->nir, nir_lower_regs_to_ssa);
				NIR_PASS_V(sel->nir, nir_lower_alu_to_scalar, NULL, NULL);
				NIR_PASS_V(sel->nir, nir_lower_int64);
				NIR_PASS_V(sel->nir, nir_opt_vectorize, NULL, NULL);
			}
			NIR_PASS_V(sel->nir, nir_lower_flrp, ~0, false);
		}
		nir_tgsi_scan_shader(sel->nir, &sel->info, true);

		r = r600_shader_from_nir(rctx, shader, &key);
		if (r) {
			fprintf(stderr, "--Failed shader--------------------------------------------------\n");
			
			if (sel->ir_type == PIPE_SHADER_IR_TGSI) {
				fprintf(stderr, "--TGSI--------------------------------------------------------\n");
				tgsi_dump(sel->tokens, 0);
			}
			
			if (rscreen->b.debug_flags & DBG_NIR) {
				fprintf(stderr, "--NIR --------------------------------------------------------\n");
				nir_print_shader(sel->nir, stderr);
			}
			
			R600_ERR("translation from NIR failed !\n");
			goto error;
		}
	}
	
	if (dump) {
		if (sel->ir_type == PIPE_SHADER_IR_TGSI) {
			fprintf(stderr, "--TGSI--------------------------------------------------------\n");
			tgsi_dump(sel->tokens, 0);
		}
		
		if (sel->so.num_outputs) {
			r600_dump_streamout(&sel->so);
		}
	}
	
	if (shader->shader.processor_type == PIPE_SHADER_VERTEX) {
		/* only disable for vertex shaders in tess paths */
		if (key.vs.as_ls)
			use_sb = 0;
	}
	use_sb &= (shader->shader.processor_type != PIPE_SHADER_TESS_CTRL);
	use_sb &= (shader->shader.processor_type != PIPE_SHADER_TESS_EVAL);
	use_sb &= (shader->shader.processor_type != PIPE_SHADER_COMPUTE);

	/* disable SB for shaders using doubles */
	use_sb &= !shader->shader.uses_doubles;

	use_sb &= !shader->shader.uses_atomics;
	use_sb &= !shader->shader.uses_images;
	use_sb &= !shader->shader.uses_helper_invocation;

	/* Check if the bytecode has already been built. */
	if (!shader->shader.bc.bytecode) {
		r = r600_bytecode_build(&shader->shader.bc);
		if (r) {
			R600_ERR("building bytecode failed !\n");
			goto error;
		}
	}

	sb_disasm = use_sb || (rctx->screen->b.debug_flags & DBG_SB_DISASM);
	if (dump && !sb_disasm) {
		fprintf(stderr, "--------------------------------------------------------------\n");
		r600_bytecode_disasm(&shader->shader.bc);
		fprintf(stderr, "______________________________________________________________\n");
	} else if ((dump && sb_disasm) || use_sb) {
                r = r600_sb_bytecode_process(rctx, &shader->shader.bc, &shader->shader,
		                             dump, use_sb);
		if (r) {
			R600_ERR("r600_sb_bytecode_process failed !\n");
			goto error;
		}
	}

        if (dump) {
           FILE *f;
           char fname[1024];
           snprintf(fname, 1024, "shader_from_%s_%d.cpp",
                    (sel->ir_type == PIPE_SHADER_IR_TGSI ?
                        (rscreen->b.debug_flags & DBG_NIR ? "tgsi-nir" : "tgsi")
                      : "nir"), nshader);
           f = fopen(fname, "w");
           print_shader_info(f, nshader++, &shader->shader);
           print_shader_info(stderr, nshader++, &shader->shader);
           print_pipe_info(stderr, &sel->info);
           if (sel->ir_type == PIPE_SHADER_IR_TGSI) {
              fprintf(f, "/****TGSI**********************************\n");
              tgsi_dump_to_file(sel->tokens, 0, f);
           }

           if (rscreen->b.debug_flags & DBG_NIR){
              fprintf(f, "/****NIR **********************************\n");
              nir_print_shader(sel->nir, f);
           }
           fprintf(f, "******************************************/\n");
           fclose(f);
        }

	if (shader->gs_copy_shader) {
		if (dump) {
			// dump copy shader
			r = r600_sb_bytecode_process(rctx, &shader->gs_copy_shader->shader.bc,
						     &shader->gs_copy_shader->shader, dump, 0);
			if (r)
				goto error;
		}

		if ((r = store_shader(ctx, shader->gs_copy_shader)))
			goto error;
	}

	/* Store the shader in a buffer. */
	if ((r = store_shader(ctx, shader)))
		goto error;

	/* Build state. */
	switch (shader->shader.processor_type) {
	case PIPE_SHADER_TESS_CTRL:
		evergreen_update_hs_state(ctx, shader);
		break;
	case PIPE_SHADER_TESS_EVAL:
		if (key.tes.as_es)
			evergreen_update_es_state(ctx, shader);
		else
			evergreen_update_vs_state(ctx, shader);
		break;
	case PIPE_SHADER_GEOMETRY:
		if (rctx->b.chip_class >= EVERGREEN) {
			evergreen_update_gs_state(ctx, shader);
			evergreen_update_vs_state(ctx, shader->gs_copy_shader);
		} else {
			r600_update_gs_state(ctx, shader);
			r600_update_vs_state(ctx, shader->gs_copy_shader);
		}
		break;
	case PIPE_SHADER_VERTEX:
		export_shader = key.vs.as_es;
		if (rctx->b.chip_class >= EVERGREEN) {
			if (key.vs.as_ls)
				evergreen_update_ls_state(ctx, shader);
			else if (key.vs.as_es)
				evergreen_update_es_state(ctx, shader);
			else
				evergreen_update_vs_state(ctx, shader);
		} else {
			if (export_shader)
				r600_update_es_state(ctx, shader);
			else
				r600_update_vs_state(ctx, shader);
		}
		break;
	case PIPE_SHADER_FRAGMENT:
		if (rctx->b.chip_class >= EVERGREEN) {
			evergreen_update_ps_state(ctx, shader);
		} else {
			r600_update_ps_state(ctx, shader);
		}
		break;
	case PIPE_SHADER_COMPUTE:
		evergreen_update_ls_state(ctx, shader);
		break;
	default:
		r = -EINVAL;
		goto error;
	}
	return 0;

error:
	r600_pipe_shader_destroy(ctx, shader);
	return r;
}

void r600_pipe_shader_destroy(struct pipe_context *ctx UNUSED, struct r600_pipe_shader *shader)
{
	r600_resource_reference(&shader->bo, NULL);
	if (list_is_linked(&shader->shader.bc.cf))
		r600_bytecode_clear(&shader->shader.bc);
	r600_release_command_buffer(&shader->command_buffer);
}

/*
 * tgsi -> r600 shader
 */
struct r600_shader_tgsi_instruction;

struct r600_shader_src {
	unsigned				sel;
	unsigned				swizzle[4];
	unsigned				neg;
	unsigned				abs;
	unsigned				rel;
	unsigned				kc_bank;
	boolean					kc_rel; /* true if cache bank is indexed */
	uint32_t				value[4];
};

struct eg_interp {
	boolean					enabled;
	unsigned				ij_index;
};

struct r600_shader_ctx {
	struct tgsi_shader_info			info;
	struct tgsi_array_info			*array_infos;
	/* flag for each tgsi temp array if its been spilled or not */
	bool					*spilled_arrays;
	struct tgsi_parse_context		parse;
	const struct tgsi_token			*tokens;
	unsigned				type;
	unsigned				file_offset[TGSI_FILE_COUNT];
	unsigned				temp_reg;
	const struct r600_shader_tgsi_instruction	*inst_info;
	struct r600_bytecode			*bc;
	struct r600_shader			*shader;
	struct r600_shader_src			src[4];
	uint32_t				*literals;
	uint32_t				nliterals;
	uint32_t				max_driver_temp_used;
	/* needed for evergreen interpolation */
	struct eg_interp		eg_interpolators[6]; // indexed by Persp/Linear * 3 + sample/center/centroid
	/* evergreen/cayman also store sample mask in face register */
	int					face_gpr;
	/* sample id is .w component stored in fixed point position register */
	int					fixed_pt_position_gpr;
	int					colors_used;
	boolean                 clip_vertex_write;
	unsigned                cv_output;
	unsigned		edgeflag_output;
	int					helper_invoc_reg;
	int                                     cs_block_size_reg;
	int                                     cs_grid_size_reg;
	bool cs_block_size_loaded, cs_grid_size_loaded;
	int					fragcoord_input;
	int					next_ring_offset;
	int					gs_out_ring_offset;
	int					gs_next_vertex;
	struct r600_shader	*gs_for_vs;
	int					gs_export_gpr_tregs[4];
	int                                     gs_rotated_input[2];
	const struct pipe_stream_output_info	*gs_stream_output_info;
	unsigned				enabled_stream_buffers_mask;
	unsigned                                tess_input_info; /* temp with tess input offsets */
	unsigned                                tess_output_info; /* temp with tess input offsets */
	unsigned                                thread_id_gpr; /* temp with thread id calculated for images */
};

struct r600_shader_tgsi_instruction {
	unsigned	op;
	int (*process)(struct r600_shader_ctx *ctx);
};

static int emit_gs_ring_writes(struct r600_shader_ctx *ctx, const struct pipe_stream_output_info *so, int stream, bool ind);
static const struct r600_shader_tgsi_instruction r600_shader_tgsi_instruction[], eg_shader_tgsi_instruction[], cm_shader_tgsi_instruction[];
static int tgsi_helper_tempx_replicate(struct r600_shader_ctx *ctx);
static inline int callstack_push(struct r600_shader_ctx *ctx, unsigned reason);
static void fc_pushlevel(struct r600_shader_ctx *ctx, int type);
static int tgsi_else(struct r600_shader_ctx *ctx);
static int tgsi_endif(struct r600_shader_ctx *ctx);
static int tgsi_bgnloop(struct r600_shader_ctx *ctx);
static int tgsi_endloop(struct r600_shader_ctx *ctx);
static int tgsi_loop_brk_cont(struct r600_shader_ctx *ctx);
static int tgsi_fetch_rel_const(struct r600_shader_ctx *ctx,
                                unsigned int cb_idx, unsigned cb_rel, unsigned int offset, unsigned ar_chan,
                                unsigned int dst_reg);
static void r600_bytecode_src(struct r600_bytecode_alu_src *bc_src,
			const struct r600_shader_src *shader_src,
			unsigned chan);
static int do_lds_fetch_values(struct r600_shader_ctx *ctx, unsigned temp_reg,
			       unsigned dst_reg, unsigned mask);

static bool ctx_needs_stack_workaround_8xx(struct r600_shader_ctx *ctx)
{
	if (ctx->bc->family == CHIP_HEMLOCK ||
	    ctx->bc->family == CHIP_CYPRESS ||
	    ctx->bc->family == CHIP_JUNIPER)
		return false;
	return true;
}

static int tgsi_last_instruction(unsigned writemask)
{
	int i, lasti = 0;

	for (i = 0; i < 4; i++) {
		if (writemask & (1 << i)) {
			lasti = i;
		}
	}
	return lasti;
}

static int tgsi_is_supported(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *i = &ctx->parse.FullToken.FullInstruction;
	unsigned j;

	if (i->Instruction.NumDstRegs > 1 && i->Instruction.Opcode != TGSI_OPCODE_DFRACEXP) {
		R600_ERR("too many dst (%d)\n", i->Instruction.NumDstRegs);
		return -EINVAL;
	}
#if 0
	if (i->Instruction.Label) {
		R600_ERR("label unsupported\n");
		return -EINVAL;
	}
#endif
	for (j = 0; j < i->Instruction.NumSrcRegs; j++) {
		if (i->Src[j].Register.Dimension) {
			switch (i->Src[j].Register.File) {
			case TGSI_FILE_CONSTANT:
			case TGSI_FILE_HW_ATOMIC:
				break;
			case TGSI_FILE_INPUT:
				if (ctx->type == PIPE_SHADER_GEOMETRY ||
				    ctx->type == PIPE_SHADER_TESS_CTRL ||
				    ctx->type == PIPE_SHADER_TESS_EVAL)
					break;
				/* fallthrough */
			case TGSI_FILE_OUTPUT:
				if (ctx->type == PIPE_SHADER_TESS_CTRL)
					break;
				/* fallthrough */
			default:
				R600_ERR("unsupported src %d (file %d, dimension %d)\n", j,
					 i->Src[j].Register.File,
					 i->Src[j].Register.Dimension);
				return -EINVAL;
			}
		}
	}
	for (j = 0; j < i->Instruction.NumDstRegs; j++) {
		if (i->Dst[j].Register.Dimension) {
			if (ctx->type == PIPE_SHADER_TESS_CTRL)
				continue;
			R600_ERR("unsupported dst (dimension)\n");
			return -EINVAL;
		}
	}
	return 0;
}

int eg_get_interpolator_index(unsigned interpolate, unsigned location)
{
	if (interpolate == TGSI_INTERPOLATE_COLOR ||
		interpolate == TGSI_INTERPOLATE_LINEAR ||
		interpolate == TGSI_INTERPOLATE_PERSPECTIVE)
	{
		int is_linear = interpolate == TGSI_INTERPOLATE_LINEAR;
		int loc;

		switch(location) {
		case TGSI_INTERPOLATE_LOC_CENTER:
			loc = 1;
			break;
		case TGSI_INTERPOLATE_LOC_CENTROID:
			loc = 2;
			break;
		case TGSI_INTERPOLATE_LOC_SAMPLE:
		default:
			loc = 0; break;
		}

		return is_linear * 3 + loc;
	}

	return -1;
}

static void evergreen_interp_assign_ij_index(struct r600_shader_ctx *ctx,
		int input)
{
	int i = eg_get_interpolator_index(
		ctx->shader->input[input].interpolate,
		ctx->shader->input[input].interpolate_location);
	assert(i >= 0);
	ctx->shader->input[input].ij_index = ctx->eg_interpolators[i].ij_index;
}

static int evergreen_interp_alu(struct r600_shader_ctx *ctx, int input)
{
	int i, r;
	struct r600_bytecode_alu alu;
	int gpr = 0, base_chan = 0;
	int ij_index = ctx->shader->input[input].ij_index;

	/* work out gpr and base_chan from index */
	gpr = ij_index / 2;
	base_chan = (2 * (ij_index % 2)) + 1;

	for (i = 0; i < 8; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));

		if (i < 4)
			alu.op = ALU_OP2_INTERP_ZW;
		else
			alu.op = ALU_OP2_INTERP_XY;

		if ((i > 1) && (i < 6)) {
			alu.dst.sel = ctx->shader->input[input].gpr;
			alu.dst.write = 1;
		}

		alu.dst.chan = i % 4;

		alu.src[0].sel = gpr;
		alu.src[0].chan = (base_chan - (i % 2));

		alu.src[1].sel = V_SQ_ALU_SRC_PARAM_BASE + ctx->shader->input[input].lds_pos;

		alu.bank_swizzle_force = SQ_ALU_VEC_210;
		if ((i % 4) == 3)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int evergreen_interp_flat(struct r600_shader_ctx *ctx, int input)
{
	int i, r;
	struct r600_bytecode_alu alu;

	for (i = 0; i < 4; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));

		alu.op = ALU_OP1_INTERP_LOAD_P0;

		alu.dst.sel = ctx->shader->input[input].gpr;
		alu.dst.write = 1;

		alu.dst.chan = i;

		alu.src[0].sel = V_SQ_ALU_SRC_PARAM_BASE + ctx->shader->input[input].lds_pos;
		alu.src[0].chan = i;

		if (i == 3)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

/*
 * Special export handling in shaders
 *
 * shader export ARRAY_BASE for EXPORT_POS:
 * 60 is position
 * 61 is misc vector
 * 62, 63 are clip distance vectors
 *
 * The use of the values exported in 61-63 are controlled by PA_CL_VS_OUT_CNTL:
 * VS_OUT_MISC_VEC_ENA - enables the use of all fields in export 61
 * USE_VTX_POINT_SIZE - point size in the X channel of export 61
 * USE_VTX_EDGE_FLAG - edge flag in the Y channel of export 61
 * USE_VTX_RENDER_TARGET_INDX - render target index in the Z channel of export 61
 * USE_VTX_VIEWPORT_INDX - viewport index in the W channel of export 61
 * USE_VTX_KILL_FLAG - kill flag in the Z channel of export 61 (mutually
 * exclusive from render target index)
 * VS_OUT_CCDIST0_VEC_ENA/VS_OUT_CCDIST1_VEC_ENA - enable clip distance vectors
 *
 *
 * shader export ARRAY_BASE for EXPORT_PIXEL:
 * 0-7 CB targets
 * 61 computed Z vector
 *
 * The use of the values exported in the computed Z vector are controlled
 * by DB_SHADER_CONTROL:
 * Z_EXPORT_ENABLE - Z as a float in RED
 * STENCIL_REF_EXPORT_ENABLE - stencil ref as int in GREEN
 * COVERAGE_TO_MASK_ENABLE - alpha to mask in ALPHA
 * MASK_EXPORT_ENABLE - pixel sample mask in BLUE
 * DB_SOURCE_FORMAT - export control restrictions
 *
 */


/* Map name/sid pair from tgsi to the 8-bit semantic index for SPI setup */
static int r600_spi_sid(struct r600_shader_io * io)
{
	int index, name = io->name;

	/* These params are handled differently, they don't need
	 * semantic indices, so we'll use 0 for them.
	 */
	if (name == TGSI_SEMANTIC_POSITION ||
	    name == TGSI_SEMANTIC_PSIZE ||
	    name == TGSI_SEMANTIC_EDGEFLAG ||
	    name == TGSI_SEMANTIC_FACE ||
	    name == TGSI_SEMANTIC_SAMPLEMASK)
		index = 0;
	else {
		if (name == TGSI_SEMANTIC_GENERIC) {
			/* For generic params simply use sid from tgsi */
			index = 9 + io->sid;
		} else if (name == TGSI_SEMANTIC_TEXCOORD) {
			index = io->sid;
		} else {
			/* For non-generic params - pack name and sid into 8 bits */
			index = 0x80 | (name<<3) | (io->sid);
		}

		/* Make sure that all really used indices have nonzero value, so
		 * we can just compare it to 0 later instead of comparing the name
		 * with different values to detect special cases. */
		index++;
	}

	return index;
};

/* we need this to get a common lds index for vs/tcs/tes input/outputs */
int r600_get_lds_unique_index(unsigned semantic_name, unsigned index)
{
	switch (semantic_name) {
	case TGSI_SEMANTIC_POSITION:
		return 0;
	case TGSI_SEMANTIC_PSIZE:
		return 1;
	case TGSI_SEMANTIC_CLIPDIST:
		assert(index <= 1);
		return 2 + index;
	case TGSI_SEMANTIC_TEXCOORD:
		return 4 + index;
	case TGSI_SEMANTIC_GENERIC:
		if (index <= 63-4)
			return 4 + index;
		else
			/* same explanation as in the default statement,
			 * the only user hitting this is st/nine.
			 */
			return 0;

	/* patch indices are completely separate and thus start from 0 */
	case TGSI_SEMANTIC_TESSOUTER:
		return 0;
	case TGSI_SEMANTIC_TESSINNER:
		return 1;
	case TGSI_SEMANTIC_PATCH:
		return 2 + index;

	default:
		/* Don't fail here. The result of this function is only used
		 * for LS, TCS, TES, and GS, where legacy GL semantics can't
		 * occur, but this function is called for all vertex shaders
		 * before it's known whether LS will be compiled or not.
		 */
		return 0;
	}
}

/* turn input into interpolate on EG */
static int evergreen_interp_input(struct r600_shader_ctx *ctx, int index)
{
	int r = 0;

	if (ctx->shader->input[index].spi_sid) {
		ctx->shader->input[index].lds_pos = ctx->shader->nlds++;
		if (ctx->shader->input[index].interpolate > 0) {
			evergreen_interp_assign_ij_index(ctx, index);
			r = evergreen_interp_alu(ctx, index);
		} else {
			r = evergreen_interp_flat(ctx, index);
		}
	}
	return r;
}

static int select_twoside_color(struct r600_shader_ctx *ctx, int front, int back)
{
	struct r600_bytecode_alu alu;
	int i, r;
	int gpr_front = ctx->shader->input[front].gpr;
	int gpr_back = ctx->shader->input[back].gpr;

	for (i = 0; i < 4; i++) {
		memset(&alu, 0, sizeof(alu));
		alu.op = ALU_OP3_CNDGT;
		alu.is_op3 = 1;
		alu.dst.write = 1;
		alu.dst.sel = gpr_front;
		alu.src[0].sel = ctx->face_gpr;
		alu.src[1].sel = gpr_front;
		alu.src[2].sel = gpr_back;

		alu.dst.chan = i;
		alu.src[1].chan = i;
		alu.src[2].chan = i;
		alu.last = (i==3);

		if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
			return r;
	}

	return 0;
}

/* execute a single slot ALU calculation */
static int single_alu_op2(struct r600_shader_ctx *ctx, int op,
			  int dst_sel, int dst_chan,
			  int src0_sel, unsigned src0_chan_val,
			  int src1_sel, unsigned src1_chan_val)
{
	struct r600_bytecode_alu alu;
	int r, i;

	if (ctx->bc->chip_class == CAYMAN && op == ALU_OP2_MULLO_INT) {
		for (i = 0; i < 4; i++) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = op;
			alu.src[0].sel = src0_sel;
			if (src0_sel == V_SQ_ALU_SRC_LITERAL)
				alu.src[0].value = src0_chan_val;
			else
				alu.src[0].chan = src0_chan_val;
			alu.src[1].sel = src1_sel;
			if (src1_sel == V_SQ_ALU_SRC_LITERAL)
				alu.src[1].value = src1_chan_val;
			else
				alu.src[1].chan = src1_chan_val;
			alu.dst.sel = dst_sel;
			alu.dst.chan = i;
			alu.dst.write = i == dst_chan;
			alu.last = (i == 3);
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
		return 0;
	}

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = op;
	alu.src[0].sel = src0_sel;
	if (src0_sel == V_SQ_ALU_SRC_LITERAL)
		alu.src[0].value = src0_chan_val;
	else
		alu.src[0].chan = src0_chan_val;
	alu.src[1].sel = src1_sel;
	if (src1_sel == V_SQ_ALU_SRC_LITERAL)
		alu.src[1].value = src1_chan_val;
	else
		alu.src[1].chan = src1_chan_val;
	alu.dst.sel = dst_sel;
	alu.dst.chan = dst_chan;
	alu.dst.write = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	return 0;
}

/* execute a single slot ALU calculation */
static int single_alu_op3(struct r600_shader_ctx *ctx, int op,
			  int dst_sel, int dst_chan,
			  int src0_sel, unsigned src0_chan_val,
			  int src1_sel, unsigned src1_chan_val,
			  int src2_sel, unsigned src2_chan_val)
{
	struct r600_bytecode_alu alu;
	int r;

	/* validate this for other ops */
	assert(op == ALU_OP3_MULADD_UINT24 || op == ALU_OP3_CNDE_INT || op == ALU_OP3_BFE_UINT);
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = op;
	alu.src[0].sel = src0_sel;
	if (src0_sel == V_SQ_ALU_SRC_LITERAL)
		alu.src[0].value = src0_chan_val;
	else
		alu.src[0].chan = src0_chan_val;
	alu.src[1].sel = src1_sel;
	if (src1_sel == V_SQ_ALU_SRC_LITERAL)
		alu.src[1].value = src1_chan_val;
	else
		alu.src[1].chan = src1_chan_val;
	alu.src[2].sel = src2_sel;
	if (src2_sel == V_SQ_ALU_SRC_LITERAL)
		alu.src[2].value = src2_chan_val;
	else
		alu.src[2].chan = src2_chan_val;
	alu.dst.sel = dst_sel;
	alu.dst.chan = dst_chan;
	alu.is_op3 = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	return 0;
}

/* put it in temp_reg.x */
static int get_lds_offset0(struct r600_shader_ctx *ctx,
			   int rel_patch_chan,
			   int temp_reg, bool is_patch_var)
{
	int r;

	/* MUL temp.x, patch_stride (input_vals.x), rel_patch_id (r0.y (tcs)) */
	/* ADD
	   Dimension - patch0_offset (input_vals.z),
	   Non-dim - patch0_data_offset (input_vals.w)
	*/
	r = single_alu_op3(ctx, ALU_OP3_MULADD_UINT24,
			   temp_reg, 0,
			   ctx->tess_output_info, 0,
			   0, rel_patch_chan,
			   ctx->tess_output_info, is_patch_var ? 3 : 2);
	if (r)
		return r;
	return 0;
}

static inline int get_address_file_reg(struct r600_shader_ctx *ctx, int index)
{
	return index > 0 ? ctx->bc->index_reg[index - 1] : ctx->bc->ar_reg;
}

static int r600_get_temp(struct r600_shader_ctx *ctx)
{
	return ctx->temp_reg + ctx->max_driver_temp_used++;
}

static int vs_add_primid_output(struct r600_shader_ctx *ctx, int prim_id_sid)
{
	int i;
	i = ctx->shader->noutput++;
	ctx->shader->output[i].name = TGSI_SEMANTIC_PRIMID;
	ctx->shader->output[i].sid = 0;
	ctx->shader->output[i].gpr = 0;
	ctx->shader->output[i].interpolate = TGSI_INTERPOLATE_CONSTANT;
	ctx->shader->output[i].write_mask = 0x4;
	ctx->shader->output[i].spi_sid = prim_id_sid;

	return 0;
}

static int tgsi_barrier(struct r600_shader_ctx *ctx)
{
	struct r600_bytecode_alu alu;
	int r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ctx->inst_info->op;
	alu.last = 1;

	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	return 0;
}

static void choose_spill_arrays(struct r600_shader_ctx *ctx, int *regno, unsigned *scratch_space_needed)
{
	// pick largest array and spill it, repeat until the number of temps is under limit or we run out of arrays
	unsigned n = ctx->info.array_max[TGSI_FILE_TEMPORARY];
	unsigned narrays_left = n;
	bool *spilled = ctx->spilled_arrays; // assumed calloc:ed

	*scratch_space_needed = 0;
	while (*regno > 124 && narrays_left) {
		unsigned i;
		unsigned largest = 0;
		unsigned largest_index = 0;

		for (i = 0; i < n; i++) {
			unsigned size = ctx->array_infos[i].range.Last - ctx->array_infos[i].range.First + 1;
			if (!spilled[i] && size > largest) {
				largest = size;
				largest_index = i;
			}
		}

		spilled[largest_index] = true;
		*regno -= largest;
		*scratch_space_needed += largest;

		narrays_left --;
	}

	if (narrays_left == 0) {
		ctx->info.indirect_files &= ~(1 << TGSI_FILE_TEMPORARY);
	}
}

/* Take spilled temp arrays into account when translating tgsi register
 * indexes into r600 gprs if spilled is false, or scratch array offset if
 * spilled is true */
static int map_tgsi_reg_index_to_r600_gpr(struct r600_shader_ctx *ctx, unsigned tgsi_reg_index, bool *spilled)
{
	unsigned i;
	unsigned spilled_size = 0;

	for (i = 0; i < ctx->info.array_max[TGSI_FILE_TEMPORARY]; i++) {
		if (tgsi_reg_index >= ctx->array_infos[i].range.First && tgsi_reg_index <= ctx->array_infos[i].range.Last) {
			if (ctx->spilled_arrays[i]) {
				/* vec4 index into spilled scratch memory */
				*spilled = true;
				return tgsi_reg_index - ctx->array_infos[i].range.First + spilled_size;
			}
			else {
				/* regular GPR array */
				*spilled = false;
				return tgsi_reg_index - spilled_size + ctx->file_offset[TGSI_FILE_TEMPORARY];
			}
		}

		if (tgsi_reg_index < ctx->array_infos[i].range.First)
			break;
		if (ctx->spilled_arrays[i]) {
			spilled_size += ctx->array_infos[i].range.Last - ctx->array_infos[i].range.First + 1;
		}
	}

	/* regular GPR index, minus the holes from spilled arrays */
	*spilled = false;

	return tgsi_reg_index - spilled_size + ctx->file_offset[TGSI_FILE_TEMPORARY];
}

/* look up spill area base offset and array size for a spilled temp array */
static void get_spilled_array_base_and_size(struct r600_shader_ctx *ctx, unsigned tgsi_reg_index,
	unsigned *array_base, unsigned *array_size)
{
	unsigned i;
	unsigned offset = 0;

	for (i = 0; i < ctx->info.array_max[TGSI_FILE_TEMPORARY]; i++) {
		if (ctx->spilled_arrays[i]) {
			unsigned size = ctx->array_infos[i].range.Last - ctx->array_infos[i].range.First + 1;

			if (tgsi_reg_index >= ctx->array_infos[i].range.First && tgsi_reg_index <= ctx->array_infos[i].range.Last) {
				*array_base = offset;
				*array_size = size - 1; /* hw counts from 1 */

				return;
			}

			offset += size;
		}
	}
}

static int tgsi_declaration(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_declaration *d = &ctx->parse.FullToken.FullDeclaration;
	int r, i, j, count = d->Range.Last - d->Range.First + 1;

	switch (d->Declaration.File) {
	case TGSI_FILE_INPUT:
		for (j = 0; j < count; j++) {
			i = ctx->shader->ninput + j;
			assert(i < ARRAY_SIZE(ctx->shader->input));
			ctx->shader->input[i].name = d->Semantic.Name;
			ctx->shader->input[i].sid = d->Semantic.Index + j;
			ctx->shader->input[i].interpolate = d->Interp.Interpolate;
			ctx->shader->input[i].interpolate_location = d->Interp.Location;
			ctx->shader->input[i].gpr = ctx->file_offset[TGSI_FILE_INPUT] + d->Range.First + j;
			if (ctx->type == PIPE_SHADER_FRAGMENT) {
				ctx->shader->input[i].spi_sid = r600_spi_sid(&ctx->shader->input[i]);
				switch (ctx->shader->input[i].name) {
				case TGSI_SEMANTIC_FACE:
					if (ctx->face_gpr != -1)
						ctx->shader->input[i].gpr = ctx->face_gpr; /* already allocated by allocate_system_value_inputs */
					else
						ctx->face_gpr = ctx->shader->input[i].gpr;
					break;
				case TGSI_SEMANTIC_COLOR:
					ctx->colors_used++;
					break;
				case TGSI_SEMANTIC_POSITION:
					ctx->fragcoord_input = i;
					break;
				case TGSI_SEMANTIC_PRIMID:
					/* set this for now */
					ctx->shader->gs_prim_id_input = true;
					ctx->shader->ps_prim_id_input = i;
					break;
				}
				if (ctx->bc->chip_class >= EVERGREEN) {
					if ((r = evergreen_interp_input(ctx, i)))
						return r;
				}
			} else if (ctx->type == PIPE_SHADER_GEOMETRY) {
				/* FIXME probably skip inputs if they aren't passed in the ring */
				ctx->shader->input[i].ring_offset = ctx->next_ring_offset;
				ctx->next_ring_offset += 16;
				if (ctx->shader->input[i].name == TGSI_SEMANTIC_PRIMID)
					ctx->shader->gs_prim_id_input = true;
			}
		}
		ctx->shader->ninput += count;
		break;
	case TGSI_FILE_OUTPUT:
		for (j = 0; j < count; j++) {
			i = ctx->shader->noutput + j;
			assert(i < ARRAY_SIZE(ctx->shader->output));
			ctx->shader->output[i].name = d->Semantic.Name;
			ctx->shader->output[i].sid = d->Semantic.Index + j;
			ctx->shader->output[i].gpr = ctx->file_offset[TGSI_FILE_OUTPUT] + d->Range.First + j;
			ctx->shader->output[i].interpolate = d->Interp.Interpolate;
			ctx->shader->output[i].write_mask = d->Declaration.UsageMask;
			if (ctx->type == PIPE_SHADER_VERTEX ||
			    ctx->type == PIPE_SHADER_GEOMETRY ||
			    ctx->type == PIPE_SHADER_TESS_EVAL) {
				ctx->shader->output[i].spi_sid = r600_spi_sid(&ctx->shader->output[i]);
				switch (d->Semantic.Name) {
				case TGSI_SEMANTIC_CLIPDIST:
					break;
				case TGSI_SEMANTIC_PSIZE:
					ctx->shader->vs_out_misc_write = 1;
					ctx->shader->vs_out_point_size = 1;
					break;
				case TGSI_SEMANTIC_EDGEFLAG:
					ctx->shader->vs_out_misc_write = 1;
					ctx->shader->vs_out_edgeflag = 1;
					ctx->edgeflag_output = i;
					break;
				case TGSI_SEMANTIC_VIEWPORT_INDEX:
					ctx->shader->vs_out_misc_write = 1;
					ctx->shader->vs_out_viewport = 1;
					break;
				case TGSI_SEMANTIC_LAYER:
					ctx->shader->vs_out_misc_write = 1;
					ctx->shader->vs_out_layer = 1;
					break;
				case TGSI_SEMANTIC_CLIPVERTEX:
					ctx->clip_vertex_write = TRUE;
					ctx->cv_output = i;
					break;
				}
				if (ctx->type == PIPE_SHADER_GEOMETRY) {
					ctx->gs_out_ring_offset += 16;
				}
			} else if (ctx->type == PIPE_SHADER_FRAGMENT) {
				switch (d->Semantic.Name) {
				case TGSI_SEMANTIC_COLOR:
					ctx->shader->nr_ps_max_color_exports++;
					break;
				}
			}
		}
		ctx->shader->noutput += count;
		break;
	case TGSI_FILE_TEMPORARY:
		if (ctx->info.indirect_files & (1 << TGSI_FILE_TEMPORARY)) {
			if (d->Array.ArrayID) {
				bool spilled;
				unsigned idx = map_tgsi_reg_index_to_r600_gpr(ctx,
					d->Range.First,
					&spilled);

				if (!spilled) {
					r600_add_gpr_array(ctx->shader, idx,
						d->Range.Last - d->Range.First + 1, 0x0F);
				}
			}
		}
		break;

	case TGSI_FILE_CONSTANT:
	case TGSI_FILE_SAMPLER:
	case TGSI_FILE_SAMPLER_VIEW:
	case TGSI_FILE_ADDRESS:
	case TGSI_FILE_BUFFER:
	case TGSI_FILE_IMAGE:
	case TGSI_FILE_MEMORY:
		break;

	case TGSI_FILE_HW_ATOMIC:
		i = ctx->shader->nhwatomic_ranges;
		ctx->shader->atomics[i].start = d->Range.First;
		ctx->shader->atomics[i].end = d->Range.Last;
		ctx->shader->atomics[i].hw_idx = ctx->shader->atomic_base + ctx->shader->nhwatomic;
		ctx->shader->atomics[i].array_id = d->Array.ArrayID;
		ctx->shader->atomics[i].buffer_id = d->Dim.Index2D;
		ctx->shader->nhwatomic_ranges++;
		ctx->shader->nhwatomic += count;
		break;

	case TGSI_FILE_SYSTEM_VALUE:
		if (d->Semantic.Name == TGSI_SEMANTIC_SAMPLEMASK ||
			d->Semantic.Name == TGSI_SEMANTIC_SAMPLEID ||
			d->Semantic.Name == TGSI_SEMANTIC_SAMPLEPOS) {
			break; /* Already handled from allocate_system_value_inputs */
		} else if (d->Semantic.Name == TGSI_SEMANTIC_INSTANCEID) {
			break;
		} else if (d->Semantic.Name == TGSI_SEMANTIC_VERTEXID)
			break;
		else if (d->Semantic.Name == TGSI_SEMANTIC_INVOCATIONID)
			break;
		else if (d->Semantic.Name == TGSI_SEMANTIC_TESSINNER ||
			 d->Semantic.Name == TGSI_SEMANTIC_TESSOUTER) {
			int param = r600_get_lds_unique_index(d->Semantic.Name, 0);
			int dreg = d->Semantic.Name == TGSI_SEMANTIC_TESSINNER ? 3 : 2;
			unsigned temp_reg = r600_get_temp(ctx);

			r = get_lds_offset0(ctx, 2, temp_reg, true);
			if (r)
				return r;

			r = single_alu_op2(ctx, ALU_OP2_ADD_INT,
					   temp_reg, 0,
					   temp_reg, 0,
					   V_SQ_ALU_SRC_LITERAL, param * 16);
			if (r)
				return r;

			do_lds_fetch_values(ctx, temp_reg, dreg, 0xf);
		}
		else if (d->Semantic.Name == TGSI_SEMANTIC_TESSCOORD) {
			/* MOV r1.x, r0.x;
			   MOV r1.y, r0.y;
			*/
			for (i = 0; i < 2; i++) {
				struct r600_bytecode_alu alu;
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_MOV;
				alu.src[0].sel = 0;
				alu.src[0].chan = 0 + i;
				alu.dst.sel = 1;
				alu.dst.chan = 0 + i;
				alu.dst.write = 1;
				alu.last = (i == 1) ? 1 : 0;
				if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
					return r;
			}
			/* ADD r1.z, 1.0f, -r0.x */
			struct r600_bytecode_alu alu;
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP2_ADD;
			alu.src[0].sel = V_SQ_ALU_SRC_1;
			alu.src[1].sel = 1;
			alu.src[1].chan = 0;
			alu.src[1].neg = 1;
			alu.dst.sel = 1;
			alu.dst.chan = 2;
			alu.dst.write = 1;
			alu.last = 1;
			if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
				return r;

			/* ADD r1.z, r1.z, -r1.y */
			alu.op = ALU_OP2_ADD;
			alu.src[0].sel = 1;
			alu.src[0].chan = 2;
			alu.src[1].sel = 1;
			alu.src[1].chan = 1;
			alu.src[1].neg = 1;
			alu.dst.sel = 1;
			alu.dst.chan = 2;
			alu.dst.write = 1;
			alu.last = 1;
			if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
				return r;
			break;
		}
		break;
	default:
		R600_ERR("unsupported file %d declaration\n", d->Declaration.File);
		return -EINVAL;
	}
	return 0;
}

static int allocate_system_value_inputs(struct r600_shader_ctx *ctx, int gpr_offset)
{
	struct tgsi_parse_context parse;
	struct {
		boolean enabled;
		int *reg;
		unsigned name, alternate_name;
	} inputs[2] = {
		{ false, &ctx->face_gpr, TGSI_SEMANTIC_SAMPLEMASK, ~0u }, /* lives in Front Face GPR.z */

		{ false, &ctx->fixed_pt_position_gpr, TGSI_SEMANTIC_SAMPLEID, TGSI_SEMANTIC_SAMPLEPOS } /* SAMPLEID is in Fixed Point Position GPR.w */
	};
	int num_regs = 0;
	unsigned k, i;

	if (tgsi_parse_init(&parse, ctx->tokens) != TGSI_PARSE_OK) {
		return 0;
	}

	/* need to scan shader for system values and interpolateAtSample/Offset/Centroid */
	while (!tgsi_parse_end_of_tokens(&parse)) {
		tgsi_parse_token(&parse);

		if (parse.FullToken.Token.Type == TGSI_TOKEN_TYPE_INSTRUCTION) {
			const struct tgsi_full_instruction *inst = &parse.FullToken.FullInstruction;
			if (inst->Instruction.Opcode == TGSI_OPCODE_INTERP_SAMPLE ||
				inst->Instruction.Opcode == TGSI_OPCODE_INTERP_OFFSET ||
				inst->Instruction.Opcode == TGSI_OPCODE_INTERP_CENTROID)
			{
				int interpolate, location, k;

				if (inst->Instruction.Opcode == TGSI_OPCODE_INTERP_SAMPLE) {
					location = TGSI_INTERPOLATE_LOC_CENTER;
				} else if (inst->Instruction.Opcode == TGSI_OPCODE_INTERP_OFFSET) {
					location = TGSI_INTERPOLATE_LOC_CENTER;
					/* Needs sample positions, currently those are always available */
				} else {
					location = TGSI_INTERPOLATE_LOC_CENTROID;
				}

				interpolate = ctx->info.input_interpolate[inst->Src[0].Register.Index];
				k = eg_get_interpolator_index(interpolate, location);
				if (k >= 0)
					ctx->eg_interpolators[k].enabled = true;
			}
		} else if (parse.FullToken.Token.Type == TGSI_TOKEN_TYPE_DECLARATION) {
			struct tgsi_full_declaration *d = &parse.FullToken.FullDeclaration;
			if (d->Declaration.File == TGSI_FILE_SYSTEM_VALUE) {
				for (k = 0; k < ARRAY_SIZE(inputs); k++) {
					if (d->Semantic.Name == inputs[k].name ||
						d->Semantic.Name == inputs[k].alternate_name) {
						inputs[k].enabled = true;
					}
				}
			}
		}
	}

	tgsi_parse_free(&parse);

	if (ctx->info.reads_samplemask &&
	    (ctx->info.uses_linear_sample || ctx->info.uses_persp_sample)) {
		inputs[1].enabled = true;
	}

	if (ctx->bc->chip_class >= EVERGREEN) {
		int num_baryc = 0;
		/* assign gpr to each interpolator according to priority */
		for (i = 0; i < ARRAY_SIZE(ctx->eg_interpolators); i++) {
			if (ctx->eg_interpolators[i].enabled) {
				ctx->eg_interpolators[i].ij_index = num_baryc;
				num_baryc++;
			}
		}
		num_baryc = (num_baryc + 1) >> 1;
		gpr_offset += num_baryc;
	}

	for (i = 0; i < ARRAY_SIZE(inputs); i++) {
		boolean enabled = inputs[i].enabled;
		int *reg = inputs[i].reg;
		unsigned name = inputs[i].name;

		if (enabled) {
			int gpr = gpr_offset + num_regs++;
			ctx->shader->nsys_inputs++;

			// add to inputs, allocate a gpr
			k = ctx->shader->ninput++;
			ctx->shader->input[k].name = name;
			ctx->shader->input[k].sid = 0;
			ctx->shader->input[k].interpolate = TGSI_INTERPOLATE_CONSTANT;
			ctx->shader->input[k].interpolate_location = TGSI_INTERPOLATE_LOC_CENTER;
			*reg = ctx->shader->input[k].gpr = gpr;
		}
	}

	return gpr_offset + num_regs;
}

/*
 * for evergreen we need to scan the shader to find the number of GPRs we need to
 * reserve for interpolation and system values
 *
 * we need to know if we are going to emit any sample or centroid inputs
 * if perspective and linear are required
*/
static int evergreen_gpr_count(struct r600_shader_ctx *ctx)
{
	unsigned i;

	memset(&ctx->eg_interpolators, 0, sizeof(ctx->eg_interpolators));

	/*
	 * Could get this information from the shader info. But right now
	 * we interpolate all declared inputs, whereas the shader info will
	 * only contain the bits if the inputs are actually used, so it might
	 * not be safe...
	 */
	for (i = 0; i < ctx->info.num_inputs; i++) {
		int k;
		/* skip position/face/mask/sampleid */
		if (ctx->info.input_semantic_name[i] == TGSI_SEMANTIC_POSITION ||
		    ctx->info.input_semantic_name[i] == TGSI_SEMANTIC_FACE ||
		    ctx->info.input_semantic_name[i] == TGSI_SEMANTIC_SAMPLEMASK ||
		    ctx->info.input_semantic_name[i] == TGSI_SEMANTIC_SAMPLEID)
			continue;

		k = eg_get_interpolator_index(
			ctx->info.input_interpolate[i],
			ctx->info.input_interpolate_loc[i]);
		if (k >= 0)
			ctx->eg_interpolators[k].enabled = TRUE;
	}

	/* XXX PULL MODEL and LINE STIPPLE */

	return allocate_system_value_inputs(ctx, 0);
}

/* sample_id_sel == NULL means fetch for current sample */
static int load_sample_position(struct r600_shader_ctx *ctx, struct r600_shader_src *sample_id, int chan_sel)
{
	struct r600_bytecode_vtx vtx;
	int r, t1;

	t1 = r600_get_temp(ctx);

	memset(&vtx, 0, sizeof(struct r600_bytecode_vtx));
	vtx.op = FETCH_OP_VFETCH;
	vtx.buffer_id = R600_BUFFER_INFO_CONST_BUFFER;
	vtx.fetch_type = SQ_VTX_FETCH_NO_INDEX_OFFSET;
	if (sample_id == NULL) {
		assert(ctx->fixed_pt_position_gpr != -1);

		vtx.src_gpr = ctx->fixed_pt_position_gpr; // SAMPLEID is in .w;
		vtx.src_sel_x = 3;
	}
	else {
		struct r600_bytecode_alu alu;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		r600_bytecode_src(&alu.src[0], sample_id, chan_sel);
		alu.dst.sel = t1;
		alu.dst.write = 1;
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;

		vtx.src_gpr = t1;
		vtx.src_sel_x = 0;
	}
	vtx.mega_fetch_count = 16;
	vtx.dst_gpr = t1;
	vtx.dst_sel_x = 0;
	vtx.dst_sel_y = 1;
	vtx.dst_sel_z = 2;
	vtx.dst_sel_w = 3;
	vtx.data_format = FMT_32_32_32_32_FLOAT;
	vtx.num_format_all = 2;
	vtx.format_comp_all = 1;
	vtx.use_const_fields = 0;
	vtx.offset = 0;
	vtx.endian = r600_endian_swap(32);
	vtx.srf_mode_all = 1; /* SRF_MODE_NO_ZERO */

	r = r600_bytecode_add_vtx(ctx->bc, &vtx);
	if (r)
		return r;

	return t1;
}

static int eg_load_helper_invocation(struct r600_shader_ctx *ctx)
{
	int r;
	struct r600_bytecode_alu alu;

	/* do a vtx fetch with wqm set on the vtx fetch */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	alu.dst.sel = ctx->helper_invoc_reg;
	alu.dst.chan = 0;
	alu.src[0].sel = V_SQ_ALU_SRC_LITERAL;
	alu.src[0].value = 0xffffffff;
	alu.dst.write = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	/* do a vtx fetch in VPM mode */
	struct r600_bytecode_vtx vtx;
	memset(&vtx, 0, sizeof(vtx));
	vtx.op = FETCH_OP_GET_BUFFER_RESINFO;
	vtx.buffer_id = R600_BUFFER_INFO_CONST_BUFFER;
	vtx.fetch_type = SQ_VTX_FETCH_NO_INDEX_OFFSET;
	vtx.src_gpr = 0;
	vtx.mega_fetch_count = 16; /* no idea here really... */
	vtx.dst_gpr = ctx->helper_invoc_reg;
	vtx.dst_sel_x = 4;
	vtx.dst_sel_y = 7;		/* SEL_Y */
	vtx.dst_sel_z = 7;		/* SEL_Z */
	vtx.dst_sel_w = 7;		/* SEL_W */
	vtx.data_format = FMT_32;
	if ((r = r600_bytecode_add_vtx_tc(ctx->bc, &vtx)))
		return r;
	ctx->bc->cf_last->vpm = 1;
	return 0;
}

static int cm_load_helper_invocation(struct r600_shader_ctx *ctx)
{
	int r;
	struct r600_bytecode_alu alu;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	alu.dst.sel = ctx->helper_invoc_reg;
	alu.dst.chan = 0;
	alu.src[0].sel = V_SQ_ALU_SRC_LITERAL;
	alu.src[0].value = 0xffffffff;
	alu.dst.write = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	alu.dst.sel = ctx->helper_invoc_reg;
	alu.dst.chan = 0;
	alu.src[0].sel = V_SQ_ALU_SRC_0;
	alu.dst.write = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu_type(ctx->bc, &alu, CF_OP_ALU_VALID_PIXEL_MODE);
	if (r)
		return r;

	return ctx->helper_invoc_reg;
}

static int load_block_grid_size(struct r600_shader_ctx *ctx, bool load_block)
{
	struct r600_bytecode_vtx vtx;
	int r, t1;

	if (ctx->cs_block_size_loaded)
		return ctx->cs_block_size_reg;
	if (ctx->cs_grid_size_loaded)
		return ctx->cs_grid_size_reg;

	t1 = load_block ? ctx->cs_block_size_reg : ctx->cs_grid_size_reg;
	struct r600_bytecode_alu alu;
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	alu.src[0].sel = V_SQ_ALU_SRC_0;
	alu.dst.sel = t1;
	alu.dst.write = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	memset(&vtx, 0, sizeof(struct r600_bytecode_vtx));
	vtx.op = FETCH_OP_VFETCH;
	vtx.buffer_id = R600_BUFFER_INFO_CONST_BUFFER;
	vtx.fetch_type = SQ_VTX_FETCH_NO_INDEX_OFFSET;
	vtx.src_gpr = t1;
	vtx.src_sel_x = 0;

	vtx.mega_fetch_count = 16;
	vtx.dst_gpr = t1;
	vtx.dst_sel_x = 0;
	vtx.dst_sel_y = 1;
	vtx.dst_sel_z = 2;
	vtx.dst_sel_w = 7;
	vtx.data_format = FMT_32_32_32_32;
	vtx.num_format_all = 1;
	vtx.format_comp_all = 0;
	vtx.use_const_fields = 0;
	vtx.offset = load_block ? 0 : 16; // first element is size of buffer
	vtx.endian = r600_endian_swap(32);
	vtx.srf_mode_all = 1; /* SRF_MODE_NO_ZERO */

	r = r600_bytecode_add_vtx(ctx->bc, &vtx);
	if (r)
		return r;

	if (load_block)
		ctx->cs_block_size_loaded = true;
	else
		ctx->cs_grid_size_loaded = true;
	return t1;
}

static void tgsi_src(struct r600_shader_ctx *ctx,
		     const struct tgsi_full_src_register *tgsi_src,
		     struct r600_shader_src *r600_src)
{
	memset(r600_src, 0, sizeof(*r600_src));
	r600_src->swizzle[0] = tgsi_src->Register.SwizzleX;
	r600_src->swizzle[1] = tgsi_src->Register.SwizzleY;
	r600_src->swizzle[2] = tgsi_src->Register.SwizzleZ;
	r600_src->swizzle[3] = tgsi_src->Register.SwizzleW;
	r600_src->neg = tgsi_src->Register.Negate;
	r600_src->abs = tgsi_src->Register.Absolute;

	if (tgsi_src->Register.File == TGSI_FILE_TEMPORARY) {
		bool spilled;
		unsigned idx;

		idx = map_tgsi_reg_index_to_r600_gpr(ctx, tgsi_src->Register.Index, &spilled);

		if (spilled) {
			int reg = r600_get_temp(ctx);
			int r;

			r600_src->sel = reg;

			if (ctx->bc->chip_class < R700) {
				struct r600_bytecode_output cf;

				memset(&cf, 0, sizeof(struct r600_bytecode_output));
				cf.op = CF_OP_MEM_SCRATCH;
				cf.elem_size = 3;
				cf.gpr = reg;
				cf.comp_mask = 0xF;
				cf.swizzle_x = 0;
				cf.swizzle_y = 1;
				cf.swizzle_z = 2;
				cf.swizzle_w = 3;
				cf.burst_count = 1;

				get_spilled_array_base_and_size(ctx, tgsi_src->Register.Index,
					&cf.array_base, &cf.array_size);

				if (tgsi_src->Register.Indirect) {
					cf.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_READ_IND;
					cf.index_gpr = ctx->bc->ar_reg;
				}
				else {
					cf.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_READ;
					cf.array_base += idx;
					cf.array_size = 0;
				}

				r = r600_bytecode_add_output(ctx->bc, &cf);
			}
			else {
				struct r600_bytecode_vtx vtx;

				if (r600_bytecode_get_need_wait_ack(ctx->bc)) {
					r600_bytecode_need_wait_ack(ctx->bc, false);
					r = r600_bytecode_add_cfinst(ctx->bc, CF_OP_WAIT_ACK);
				}

				memset(&vtx, 0, sizeof(struct r600_bytecode_vtx));
				vtx.op = FETCH_OP_READ_SCRATCH;
				vtx.dst_gpr = reg;
				vtx.uncached = 1; // Must bypass cache since prior spill written in same invocation
				vtx.elem_size = 3;
				vtx.data_format = FMT_32_32_32_32;
				vtx.num_format_all = V_038010_SQ_NUM_FORMAT_INT;
				vtx.dst_sel_x = tgsi_src->Register.SwizzleX;
				vtx.dst_sel_y = tgsi_src->Register.SwizzleY;
				vtx.dst_sel_z = tgsi_src->Register.SwizzleZ;
				vtx.dst_sel_w = tgsi_src->Register.SwizzleW;

				get_spilled_array_base_and_size(ctx, tgsi_src->Register.Index,
					&vtx.array_base, &vtx.array_size);

				if (tgsi_src->Register.Indirect) {
					vtx.indexed = 1;
					vtx.src_gpr = ctx->bc->ar_reg;
				}
				else {
					vtx.array_base += idx;
					vtx.array_size = 0;
				}

				r = r600_bytecode_add_vtx(ctx->bc, &vtx);
			}

			if (r)
				return;
		}
		else {
			if (tgsi_src->Register.Indirect)
				r600_src->rel = V_SQ_REL_RELATIVE;

			r600_src->sel = idx;
		}

		return;
	}

	if (tgsi_src->Register.File == TGSI_FILE_IMMEDIATE) {
		int index;
		if ((tgsi_src->Register.SwizzleX == tgsi_src->Register.SwizzleY) &&
			(tgsi_src->Register.SwizzleX == tgsi_src->Register.SwizzleZ) &&
			(tgsi_src->Register.SwizzleX == tgsi_src->Register.SwizzleW)) {

			index = tgsi_src->Register.Index * 4 + tgsi_src->Register.SwizzleX;
			r600_bytecode_special_constants(ctx->literals[index], &r600_src->sel, &r600_src->neg, r600_src->abs);
			if (r600_src->sel != V_SQ_ALU_SRC_LITERAL)
				return;
		}
		index = tgsi_src->Register.Index;
		r600_src->sel = V_SQ_ALU_SRC_LITERAL;
		memcpy(r600_src->value, ctx->literals + index * 4, sizeof(r600_src->value));
	} else if (tgsi_src->Register.File == TGSI_FILE_SYSTEM_VALUE) {
		if (ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_SAMPLEMASK) {
			r600_src->swizzle[0] = 2; // Z value
			r600_src->swizzle[1] = 2;
			r600_src->swizzle[2] = 2;
			r600_src->swizzle[3] = 2;
			r600_src->sel = ctx->face_gpr;
		} else if (ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_SAMPLEID) {
			r600_src->swizzle[0] = 3; // W value
			r600_src->swizzle[1] = 3;
			r600_src->swizzle[2] = 3;
			r600_src->swizzle[3] = 3;
			r600_src->sel = ctx->fixed_pt_position_gpr;
		} else if (ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_SAMPLEPOS) {
			r600_src->swizzle[0] = 0;
			r600_src->swizzle[1] = 1;
			r600_src->swizzle[2] = 4;
			r600_src->swizzle[3] = 4;
			r600_src->sel = load_sample_position(ctx, NULL, -1);
		} else if (ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_INSTANCEID) {
			r600_src->swizzle[0] = 3;
			r600_src->swizzle[1] = 3;
			r600_src->swizzle[2] = 3;
			r600_src->swizzle[3] = 3;
			r600_src->sel = 0;
		} else if (ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_VERTEXID) {
			r600_src->swizzle[0] = 0;
			r600_src->swizzle[1] = 0;
			r600_src->swizzle[2] = 0;
			r600_src->swizzle[3] = 0;
			r600_src->sel = 0;
		} else if (ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_THREAD_ID) {
			r600_src->sel = 0;
		} else if (ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_BLOCK_ID) {
			r600_src->sel = 1;
		} else if (ctx->type != PIPE_SHADER_TESS_CTRL && ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_INVOCATIONID) {
			r600_src->swizzle[0] = 3;
			r600_src->swizzle[1] = 3;
			r600_src->swizzle[2] = 3;
			r600_src->swizzle[3] = 3;
			r600_src->sel = 1;
		} else if (ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_INVOCATIONID) {
			r600_src->swizzle[0] = 2;
			r600_src->swizzle[1] = 2;
			r600_src->swizzle[2] = 2;
			r600_src->swizzle[3] = 2;
			r600_src->sel = 0;
		} else if (ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_TESSCOORD) {
			r600_src->sel = 1;
		} else if (ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_TESSINNER) {
			r600_src->sel = 3;
		} else if (ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_TESSOUTER) {
			r600_src->sel = 2;
		} else if (ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_VERTICESIN) {
			r600_src->sel = ctx->tess_input_info;
			r600_src->swizzle[0] = 2;
			r600_src->swizzle[1] = 2;
			r600_src->swizzle[2] = 2;
			r600_src->swizzle[3] = 2;
		} else if (ctx->type == PIPE_SHADER_TESS_CTRL && ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_PRIMID) {
			r600_src->sel = 0;
			r600_src->swizzle[0] = 0;
			r600_src->swizzle[1] = 0;
			r600_src->swizzle[2] = 0;
			r600_src->swizzle[3] = 0;
		} else if (ctx->type == PIPE_SHADER_TESS_EVAL && ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_PRIMID) {
			r600_src->sel = 0;
			r600_src->swizzle[0] = 3;
			r600_src->swizzle[1] = 3;
			r600_src->swizzle[2] = 3;
			r600_src->swizzle[3] = 3;
		} else if (ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_GRID_SIZE) {
			r600_src->sel = load_block_grid_size(ctx, false);
		} else if (ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_BLOCK_SIZE) {
			r600_src->sel = load_block_grid_size(ctx, true);
		} else if (ctx->info.system_value_semantic_name[tgsi_src->Register.Index] == TGSI_SEMANTIC_HELPER_INVOCATION) {
			r600_src->sel = ctx->helper_invoc_reg;
			r600_src->swizzle[0] = 0;
			r600_src->swizzle[1] = 0;
			r600_src->swizzle[2] = 0;
			r600_src->swizzle[3] = 0;
		}
	} else {
		if (tgsi_src->Register.Indirect)
			r600_src->rel = V_SQ_REL_RELATIVE;
		r600_src->sel = tgsi_src->Register.Index;
		r600_src->sel += ctx->file_offset[tgsi_src->Register.File];
	}
	if (tgsi_src->Register.File == TGSI_FILE_CONSTANT) {
		if (tgsi_src->Register.Dimension) {
			r600_src->kc_bank = tgsi_src->Dimension.Index;
			if (tgsi_src->Dimension.Indirect) {
				r600_src->kc_rel = 1;
			}
		}
	}
}

static int tgsi_fetch_rel_const(struct r600_shader_ctx *ctx,
                                unsigned int cb_idx, unsigned cb_rel, unsigned int offset, unsigned ar_chan,
                                unsigned int dst_reg)
{
	struct r600_bytecode_vtx vtx;
	unsigned int ar_reg;
	int r;

	if (offset) {
		struct r600_bytecode_alu alu;

		memset(&alu, 0, sizeof(alu));

		alu.op = ALU_OP2_ADD_INT;
		alu.src[0].sel = ctx->bc->ar_reg;
		alu.src[0].chan = ar_chan;

		alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
		alu.src[1].value = offset;

		alu.dst.sel = dst_reg;
		alu.dst.chan = ar_chan;
		alu.dst.write = 1;
		alu.last = 1;

		if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
			return r;

		ar_reg = dst_reg;
	} else {
		ar_reg = ctx->bc->ar_reg;
	}

	memset(&vtx, 0, sizeof(vtx));
	vtx.buffer_id = cb_idx;
	vtx.fetch_type = SQ_VTX_FETCH_NO_INDEX_OFFSET;
	vtx.src_gpr = ar_reg;
	vtx.src_sel_x = ar_chan;
	vtx.mega_fetch_count = 16;
	vtx.dst_gpr = dst_reg;
	vtx.dst_sel_x = 0;		/* SEL_X */
	vtx.dst_sel_y = 1;		/* SEL_Y */
	vtx.dst_sel_z = 2;		/* SEL_Z */
	vtx.dst_sel_w = 3;		/* SEL_W */
	vtx.data_format = FMT_32_32_32_32_FLOAT;
	vtx.num_format_all = 2;		/* NUM_FORMAT_SCALED */
	vtx.format_comp_all = 1;	/* FORMAT_COMP_SIGNED */
	vtx.endian = r600_endian_swap(32);
	vtx.buffer_index_mode = cb_rel; // cb_rel ? V_SQ_CF_INDEX_0 : V_SQ_CF_INDEX_NONE;

	if ((r = r600_bytecode_add_vtx(ctx->bc, &vtx)))
		return r;

	return 0;
}

static int fetch_gs_input(struct r600_shader_ctx *ctx, struct tgsi_full_src_register *src, unsigned int dst_reg)
{
	struct r600_bytecode_vtx vtx;
	int r;
	unsigned index = src->Register.Index;
	unsigned vtx_id = src->Dimension.Index;
	int offset_reg = ctx->gs_rotated_input[vtx_id / 3];
	int offset_chan = vtx_id % 3;
	int t2 = 0;

	/* offsets of per-vertex data in ESGS ring are passed to GS in R0.x, R0.y,
	 * R0.w, R1.x, R1.y, R1.z (it seems R0.z is used for PrimitiveID) */

	if (offset_reg == ctx->gs_rotated_input[0] && offset_chan == 2)
		offset_chan = 3;

	if (src->Dimension.Indirect || src->Register.Indirect)
		t2 = r600_get_temp(ctx);

	if (src->Dimension.Indirect) {
		int treg[3];
		struct r600_bytecode_alu alu;
		int r, i;
		unsigned addr_reg;
		addr_reg = get_address_file_reg(ctx, src->DimIndirect.Index);
		if (src->DimIndirect.Index > 0) {
			r = single_alu_op2(ctx, ALU_OP1_MOV,
					   ctx->bc->ar_reg, 0,
					   addr_reg, 0,
					   0, 0);
			if (r)
				return r;
		}
		/*
		   we have to put the R0.x/y/w into Rt.x Rt+1.x Rt+2.x then index reg from Rt.
		   at least this is what fglrx seems to do. */
		for (i = 0; i < 3; i++) {
			treg[i] = r600_get_temp(ctx);
		}
		r600_add_gpr_array(ctx->shader, treg[0], 3, 0x0F);

		for (i = 0; i < 3; i++) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_MOV;
			alu.src[0].sel = ctx->gs_rotated_input[0];
			alu.src[0].chan = i == 2 ? 3 : i;
			alu.dst.sel = treg[i];
			alu.dst.chan = 0;
			alu.dst.write = 1;
			alu.last = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.src[0].sel = treg[0];
		alu.src[0].rel = 1;
		alu.dst.sel = t2;
		alu.dst.write = 1;
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
		offset_reg = t2;
		offset_chan = 0;
	}

	if (src->Register.Indirect) {
		int addr_reg;
		unsigned first = ctx->info.input_array_first[src->Indirect.ArrayID];

		addr_reg = get_address_file_reg(ctx, src->Indirect.Index);

		/* pull the value from index_reg */
		r = single_alu_op2(ctx, ALU_OP2_ADD_INT,
				   t2, 1,
				   addr_reg, 0,
				   V_SQ_ALU_SRC_LITERAL, first);
		if (r)
			return r;
		r = single_alu_op3(ctx, ALU_OP3_MULADD_UINT24,
				   t2, 0,
				   t2, 1,
				   V_SQ_ALU_SRC_LITERAL, 4,
				   offset_reg, offset_chan);
		if (r)
			return r;
		offset_reg = t2;
		offset_chan = 0;
		index = src->Register.Index - first;
	}

	memset(&vtx, 0, sizeof(vtx));
	vtx.buffer_id = R600_GS_RING_CONST_BUFFER;
	vtx.fetch_type = SQ_VTX_FETCH_NO_INDEX_OFFSET;
	vtx.src_gpr = offset_reg;
	vtx.src_sel_x = offset_chan;
	vtx.offset = index * 16; /*bytes*/
	vtx.mega_fetch_count = 16;
	vtx.dst_gpr = dst_reg;
	vtx.dst_sel_x = 0;		/* SEL_X */
	vtx.dst_sel_y = 1;		/* SEL_Y */
	vtx.dst_sel_z = 2;		/* SEL_Z */
	vtx.dst_sel_w = 3;		/* SEL_W */
	if (ctx->bc->chip_class >= EVERGREEN) {
		vtx.use_const_fields = 1;
	} else {
		vtx.data_format = FMT_32_32_32_32_FLOAT;
	}

	if ((r = r600_bytecode_add_vtx(ctx->bc, &vtx)))
		return r;

	return 0;
}

static int tgsi_split_gs_inputs(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	unsigned i;

	for (i = 0; i < inst->Instruction.NumSrcRegs; i++) {
		struct tgsi_full_src_register *src = &inst->Src[i];

		if (src->Register.File == TGSI_FILE_INPUT) {
			if (ctx->shader->input[src->Register.Index].name == TGSI_SEMANTIC_PRIMID) {
				/* primitive id is in R0.z */
				ctx->src[i].sel = 0;
				ctx->src[i].swizzle[0] = 2;
			}
		}
		if (src->Register.File == TGSI_FILE_INPUT && src->Register.Dimension) {
			int treg = r600_get_temp(ctx);

			fetch_gs_input(ctx, src, treg);
			ctx->src[i].sel = treg;
			ctx->src[i].rel = 0;
		}
	}
	return 0;
}


/* Tessellation shaders pass outputs to the next shader using LDS.
 *
 * LS outputs = TCS(HS) inputs
 * TCS(HS) outputs = TES(DS) inputs
 *
 * The LDS layout is:
 * - TCS inputs for patch 0
 * - TCS inputs for patch 1
 * - TCS inputs for patch 2		= get_tcs_in_current_patch_offset (if RelPatchID==2)
 * - ...
 * - TCS outputs for patch 0            = get_tcs_out_patch0_offset
 * - Per-patch TCS outputs for patch 0  = get_tcs_out_patch0_patch_data_offset
 * - TCS outputs for patch 1
 * - Per-patch TCS outputs for patch 1
 * - TCS outputs for patch 2            = get_tcs_out_current_patch_offset (if RelPatchID==2)
 * - Per-patch TCS outputs for patch 2  = get_tcs_out_current_patch_data_offset (if RelPatchID==2)
 * - ...
 *
 * All three shaders VS(LS), TCS, TES share the same LDS space.
 */
/* this will return with the dw address in temp_reg.x */
static int r600_get_byte_address(struct r600_shader_ctx *ctx, int temp_reg,
				 const struct tgsi_full_dst_register *dst,
				 const struct tgsi_full_src_register *src,
				 int stride_bytes_reg, int stride_bytes_chan)
{
	struct tgsi_full_dst_register reg;
	ubyte *name, *index, *array_first;
	int r;
	int param;
	struct tgsi_shader_info *info = &ctx->info;
	/* Set the register description. The address computation is the same
	 * for sources and destinations. */
	if (src) {
		reg.Register.File = src->Register.File;
		reg.Register.Index = src->Register.Index;
		reg.Register.Indirect = src->Register.Indirect;
		reg.Register.Dimension = src->Register.Dimension;
		reg.Indirect = src->Indirect;
		reg.Dimension = src->Dimension;
		reg.DimIndirect = src->DimIndirect;
	} else
		reg = *dst;

	/* If the register is 2-dimensional (e.g. an array of vertices
	 * in a primitive), calculate the base address of the vertex. */
	if (reg.Register.Dimension) {
		int sel, chan;
		if (reg.Dimension.Indirect) {
			unsigned addr_reg;
			assert (reg.DimIndirect.File == TGSI_FILE_ADDRESS);

			addr_reg = get_address_file_reg(ctx, reg.DimIndirect.Index);
			/* pull the value from index_reg */
			sel = addr_reg;
			chan = 0;
		} else {
			sel = V_SQ_ALU_SRC_LITERAL;
			chan = reg.Dimension.Index;
		}

		r = single_alu_op3(ctx, ALU_OP3_MULADD_UINT24,
				   temp_reg, 0,
				   stride_bytes_reg, stride_bytes_chan,
				   sel, chan,
				   temp_reg, 0);
		if (r)
			return r;
	}

	if (reg.Register.File == TGSI_FILE_INPUT) {
		name = info->input_semantic_name;
		index = info->input_semantic_index;
		array_first = info->input_array_first;
	} else if (reg.Register.File == TGSI_FILE_OUTPUT) {
		name = info->output_semantic_name;
		index = info->output_semantic_index;
		array_first = info->output_array_first;
	} else {
		assert(0);
		return -1;
	}
	if (reg.Register.Indirect) {
		int addr_reg;
		int first;
		/* Add the relative address of the element. */
		if (reg.Indirect.ArrayID)
			first = array_first[reg.Indirect.ArrayID];
		else
			first = reg.Register.Index;

		addr_reg = get_address_file_reg(ctx, reg.Indirect.Index);

		/* pull the value from index_reg */
		r = single_alu_op3(ctx, ALU_OP3_MULADD_UINT24,
				   temp_reg, 0,
				   V_SQ_ALU_SRC_LITERAL, 16,
				   addr_reg, 0,
				   temp_reg, 0);
		if (r)
			return r;

		param = r600_get_lds_unique_index(name[first],
						  index[first]);

	} else {
		param = r600_get_lds_unique_index(name[reg.Register.Index],
						  index[reg.Register.Index]);
	}

	/* add to base_addr - passed in temp_reg.x */
	if (param) {
		r = single_alu_op2(ctx, ALU_OP2_ADD_INT,
				   temp_reg, 0,
				   temp_reg, 0,
				   V_SQ_ALU_SRC_LITERAL, param * 16);
		if (r)
			return r;

	}
	return 0;
}

static int do_lds_fetch_values(struct r600_shader_ctx *ctx, unsigned temp_reg,
			       unsigned dst_reg, unsigned mask)
{
	struct r600_bytecode_alu alu;
	int r, i, lasti;

	if ((ctx->bc->cf_last->ndw>>1) >= 0x60)
		ctx->bc->force_add_cf = 1;

	lasti = tgsi_last_instruction(mask);
	for (i = 1; i <= lasti; i++) {
		if (!(mask & (1 << i)))
			continue;

		r = single_alu_op2(ctx, ALU_OP2_ADD_INT,
				   temp_reg, i,
				   temp_reg, 0,
				   V_SQ_ALU_SRC_LITERAL, 4 * i);
		if (r)
			return r;
	}
	for (i = 0; i <= lasti; i++) {
		if (!(mask & (1 << i)))
			continue;

		/* emit an LDS_READ_RET */
		memset(&alu, 0, sizeof(alu));
		alu.op = LDS_OP1_LDS_READ_RET;
		alu.src[0].sel = temp_reg;
		alu.src[0].chan = i;
		alu.src[1].sel = V_SQ_ALU_SRC_0;
		alu.src[2].sel = V_SQ_ALU_SRC_0;
		alu.dst.chan = 0;
		alu.is_lds_idx_op = true;
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	for (i = 0; i <= lasti; i++) {
		if (!(mask & (1 << i)))
			continue;

		/* then read from LDS_OQ_A_POP */
		memset(&alu, 0, sizeof(alu));

		alu.op = ALU_OP1_MOV;
		alu.src[0].sel = EG_V_SQ_ALU_SRC_LDS_OQ_A_POP;
		alu.src[0].chan = 0;
		alu.dst.sel = dst_reg;
		alu.dst.chan = i;
		alu.dst.write = 1;
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int fetch_mask(struct tgsi_src_register *reg)
{
	int mask = 0;
	mask |= 1 << reg->SwizzleX;
	mask |= 1 << reg->SwizzleY;
	mask |= 1 << reg->SwizzleZ;
	mask |= 1 << reg->SwizzleW;
	return mask;
}

static int fetch_tes_input(struct r600_shader_ctx *ctx, struct tgsi_full_src_register *src, unsigned int dst_reg)
{
	int r;
	unsigned temp_reg = r600_get_temp(ctx);

	r = get_lds_offset0(ctx, 2, temp_reg,
			    src->Register.Dimension ? false : true);
	if (r)
		return r;

	/* the base address is now in temp.x */
	r = r600_get_byte_address(ctx, temp_reg,
				  NULL, src, ctx->tess_output_info, 1);
	if (r)
		return r;

	r = do_lds_fetch_values(ctx, temp_reg, dst_reg, fetch_mask(&src->Register));
	if (r)
		return r;
	return 0;
}

static int fetch_tcs_input(struct r600_shader_ctx *ctx, struct tgsi_full_src_register *src, unsigned int dst_reg)
{
	int r;
	unsigned temp_reg = r600_get_temp(ctx);

	/* t.x = ips * r0.y */
	r = single_alu_op2(ctx, ALU_OP2_MUL_UINT24,
			   temp_reg, 0,
			   ctx->tess_input_info, 0,
			   0, 1);

	if (r)
		return r;

	/* the base address is now in temp.x */
	r = r600_get_byte_address(ctx, temp_reg,
				  NULL, src, ctx->tess_input_info, 1);
	if (r)
		return r;

	r = do_lds_fetch_values(ctx, temp_reg, dst_reg, fetch_mask(&src->Register));
	if (r)
		return r;
	return 0;
}

static int fetch_tcs_output(struct r600_shader_ctx *ctx, struct tgsi_full_src_register *src, unsigned int dst_reg)
{
	int r;
	unsigned temp_reg = r600_get_temp(ctx);

	r = get_lds_offset0(ctx, 1, temp_reg,
			    src->Register.Dimension ? false : true);
	if (r)
		return r;
	/* the base address is now in temp.x */
	r = r600_get_byte_address(ctx, temp_reg,
				  NULL, src,
				  ctx->tess_output_info, 1);
	if (r)
		return r;

	r = do_lds_fetch_values(ctx, temp_reg, dst_reg, fetch_mask(&src->Register));
	if (r)
		return r;
	return 0;
}

static int tgsi_split_lds_inputs(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	unsigned i;

	for (i = 0; i < inst->Instruction.NumSrcRegs; i++) {
		struct tgsi_full_src_register *src = &inst->Src[i];

		if (ctx->type == PIPE_SHADER_TESS_EVAL && src->Register.File == TGSI_FILE_INPUT) {
			int treg = r600_get_temp(ctx);
			fetch_tes_input(ctx, src, treg);
			ctx->src[i].sel = treg;
			ctx->src[i].rel = 0;
		}
		if (ctx->type == PIPE_SHADER_TESS_CTRL && src->Register.File == TGSI_FILE_INPUT) {
			int treg = r600_get_temp(ctx);
			fetch_tcs_input(ctx, src, treg);
			ctx->src[i].sel = treg;
			ctx->src[i].rel = 0;
		}
		if (ctx->type == PIPE_SHADER_TESS_CTRL && src->Register.File == TGSI_FILE_OUTPUT) {
			int treg = r600_get_temp(ctx);
			fetch_tcs_output(ctx, src, treg);
			ctx->src[i].sel = treg;
			ctx->src[i].rel = 0;
		}
	}
	return 0;
}

static int tgsi_split_constant(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, j, k, nconst, r;

	for (i = 0, nconst = 0; i < inst->Instruction.NumSrcRegs; i++) {
		if (inst->Src[i].Register.File == TGSI_FILE_CONSTANT) {
			nconst++;
		}
		tgsi_src(ctx, &inst->Src[i], &ctx->src[i]);
	}
	for (i = 0, j = nconst - 1; i < inst->Instruction.NumSrcRegs; i++) {
		if (inst->Src[i].Register.File != TGSI_FILE_CONSTANT) {
			continue;
		}

		if (ctx->src[i].rel) {
			int chan = inst->Src[i].Indirect.Swizzle;
			int treg = r600_get_temp(ctx);
			if ((r = tgsi_fetch_rel_const(ctx, ctx->src[i].kc_bank, ctx->src[i].kc_rel, ctx->src[i].sel - 512, chan, treg)))
				return r;

			ctx->src[i].kc_bank = 0;
			ctx->src[i].kc_rel = 0;
			ctx->src[i].sel = treg;
			ctx->src[i].rel = 0;
			j--;
		} else if (j > 0) {
			int treg = r600_get_temp(ctx);
			for (k = 0; k < 4; k++) {
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_MOV;
				alu.src[0].sel = ctx->src[i].sel;
				alu.src[0].chan = k;
				alu.src[0].rel = ctx->src[i].rel;
				alu.src[0].kc_bank = ctx->src[i].kc_bank;
				alu.src[0].kc_rel = ctx->src[i].kc_rel;
				alu.dst.sel = treg;
				alu.dst.chan = k;
				alu.dst.write = 1;
				if (k == 3)
					alu.last = 1;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}
			ctx->src[i].sel = treg;
			ctx->src[i].rel =0;
			j--;
		}
	}
	return 0;
}

/* need to move any immediate into a temp - for trig functions which use literal for PI stuff */
static int tgsi_split_literal_constant(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, j, k, nliteral, r;

	for (i = 0, nliteral = 0; i < inst->Instruction.NumSrcRegs; i++) {
		if (ctx->src[i].sel == V_SQ_ALU_SRC_LITERAL) {
			nliteral++;
		}
	}
	for (i = 0, j = nliteral - 1; i < inst->Instruction.NumSrcRegs; i++) {
		if (j > 0 && ctx->src[i].sel == V_SQ_ALU_SRC_LITERAL) {
			int treg = r600_get_temp(ctx);
			for (k = 0; k < 4; k++) {
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_MOV;
				alu.src[0].sel = ctx->src[i].sel;
				alu.src[0].chan = k;
				alu.src[0].value = ctx->src[i].value[k];
				alu.dst.sel = treg;
				alu.dst.chan = k;
				alu.dst.write = 1;
				if (k == 3)
					alu.last = 1;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}
			ctx->src[i].sel = treg;
			j--;
		}
	}
	return 0;
}

static int process_twoside_color_inputs(struct r600_shader_ctx *ctx)
{
	int i, r, count = ctx->shader->ninput;

	for (i = 0; i < count; i++) {
		if (ctx->shader->input[i].name == TGSI_SEMANTIC_COLOR) {
			r = select_twoside_color(ctx, i, ctx->shader->input[i].back_color_input);
			if (r)
				return r;
		}
	}
	return 0;
}

static int emit_streamout(struct r600_shader_ctx *ctx, struct pipe_stream_output_info *so,
						  int stream, unsigned *stream_item_size UNUSED)
{
	unsigned so_gpr[PIPE_MAX_SHADER_OUTPUTS];
	unsigned start_comp[PIPE_MAX_SHADER_OUTPUTS];
	int j, r;
	unsigned i;

	/* Sanity checking. */
	if (so->num_outputs > PIPE_MAX_SO_OUTPUTS) {
		R600_ERR("Too many stream outputs: %d\n", so->num_outputs);
		r = -EINVAL;
		goto out_err;
	}
	for (i = 0; i < so->num_outputs; i++) {
		if (so->output[i].output_buffer >= 4) {
			R600_ERR("Exceeded the max number of stream output buffers, got: %d\n",
				 so->output[i].output_buffer);
			r = -EINVAL;
			goto out_err;
		}
	}

	/* Initialize locations where the outputs are stored. */
	for (i = 0; i < so->num_outputs; i++) {

		so_gpr[i] = ctx->shader->output[so->output[i].register_index].gpr;
		start_comp[i] = so->output[i].start_component;
		/* Lower outputs with dst_offset < start_component.
		 *
		 * We can only output 4D vectors with a write mask, e.g. we can
		 * only output the W component at offset 3, etc. If we want
		 * to store Y, Z, or W at buffer offset 0, we need to use MOV
		 * to move it to X and output X. */
		if (so->output[i].dst_offset < so->output[i].start_component) {
			unsigned tmp = r600_get_temp(ctx);

			for (j = 0; j < so->output[i].num_components; j++) {
				struct r600_bytecode_alu alu;
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_MOV;
				alu.src[0].sel = so_gpr[i];
				alu.src[0].chan = so->output[i].start_component + j;

				alu.dst.sel = tmp;
				alu.dst.chan = j;
				alu.dst.write = 1;
				if (j == so->output[i].num_components - 1)
					alu.last = 1;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}
			start_comp[i] = 0;
			so_gpr[i] = tmp;
		}
	}

	/* Write outputs to buffers. */
	for (i = 0; i < so->num_outputs; i++) {
		struct r600_bytecode_output output;

		if (stream != -1 && stream != so->output[i].stream)
			continue;

		memset(&output, 0, sizeof(struct r600_bytecode_output));
		output.gpr = so_gpr[i];
		output.elem_size = so->output[i].num_components - 1;
		if (output.elem_size == 2)
			output.elem_size = 3; // 3 not supported, write 4 with junk at end
		output.array_base = so->output[i].dst_offset - start_comp[i];
		output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_WRITE;
		output.burst_count = 1;
		/* array_size is an upper limit for the burst_count
		 * with MEM_STREAM instructions */
		output.array_size = 0xFFF;
		output.comp_mask = ((1 << so->output[i].num_components) - 1) << start_comp[i];

		if (ctx->bc->chip_class >= EVERGREEN) {
			switch (so->output[i].output_buffer) {
			case 0:
				output.op = CF_OP_MEM_STREAM0_BUF0;
				break;
			case 1:
				output.op = CF_OP_MEM_STREAM0_BUF1;
				break;
			case 2:
				output.op = CF_OP_MEM_STREAM0_BUF2;
				break;
			case 3:
				output.op = CF_OP_MEM_STREAM0_BUF3;
				break;
			}
			output.op += so->output[i].stream * 4;
			assert(output.op >= CF_OP_MEM_STREAM0_BUF0 && output.op <= CF_OP_MEM_STREAM3_BUF3);
			ctx->enabled_stream_buffers_mask |= (1 << so->output[i].output_buffer) << so->output[i].stream * 4;
		} else {
			switch (so->output[i].output_buffer) {
			case 0:
				output.op = CF_OP_MEM_STREAM0;
				break;
			case 1:
				output.op = CF_OP_MEM_STREAM1;
				break;
			case 2:
				output.op = CF_OP_MEM_STREAM2;
				break;
			case 3:
				output.op = CF_OP_MEM_STREAM3;
					break;
			}
			ctx->enabled_stream_buffers_mask |= 1 << so->output[i].output_buffer;
		}
		r = r600_bytecode_add_output(ctx->bc, &output);
		if (r)
			goto out_err;
	}
	return 0;
out_err:
	return r;
}

static void convert_edgeflag_to_int(struct r600_shader_ctx *ctx)
{
	struct r600_bytecode_alu alu;
	unsigned reg;

	if (!ctx->shader->vs_out_edgeflag)
		return;

	reg = ctx->shader->output[ctx->edgeflag_output].gpr;

	/* clamp(x, 0, 1) */
	memset(&alu, 0, sizeof(alu));
	alu.op = ALU_OP1_MOV;
	alu.src[0].sel = reg;
	alu.dst.sel = reg;
	alu.dst.write = 1;
	alu.dst.clamp = 1;
	alu.last = 1;
	r600_bytecode_add_alu(ctx->bc, &alu);

	memset(&alu, 0, sizeof(alu));
	alu.op = ALU_OP1_FLT_TO_INT;
	alu.src[0].sel = reg;
	alu.dst.sel = reg;
	alu.dst.write = 1;
	alu.last = 1;
	r600_bytecode_add_alu(ctx->bc, &alu);
}

int generate_gs_copy_shader(struct r600_context *rctx,
                            struct r600_pipe_shader *gs,
                            struct pipe_stream_output_info *so)
{
	struct r600_shader_ctx ctx = {};
	struct r600_shader *gs_shader = &gs->shader;
	struct r600_pipe_shader *cshader;
	unsigned ocnt = gs_shader->noutput;
	struct r600_bytecode_alu alu;
	struct r600_bytecode_vtx vtx;
	struct r600_bytecode_output output;
	struct r600_bytecode_cf *cf_jump, *cf_pop,
		*last_exp_pos = NULL, *last_exp_param = NULL;
	int next_clip_pos = 61, next_param = 0;
	unsigned i, j;
	int ring;
	bool only_ring_0 = true;
	cshader = calloc(1, sizeof(struct r600_pipe_shader));
	if (!cshader)
		return 0;

	memcpy(cshader->shader.output, gs_shader->output, ocnt *
	       sizeof(struct r600_shader_io));

	cshader->shader.noutput = ocnt;

	ctx.shader = &cshader->shader;
	ctx.bc = &ctx.shader->bc;
	ctx.type = ctx.bc->type = PIPE_SHADER_VERTEX;

	r600_bytecode_init(ctx.bc, rctx->b.chip_class, rctx->b.family,
			   rctx->screen->has_compressed_msaa_texturing);

	ctx.bc->isa = rctx->isa;

	cf_jump = NULL;
	memset(cshader->shader.ring_item_sizes, 0, sizeof(cshader->shader.ring_item_sizes));

	/* R0.x = R0.x & 0x3fffffff */
	memset(&alu, 0, sizeof(alu));
	alu.op = ALU_OP2_AND_INT;
	alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
	alu.src[1].value = 0x3fffffff;
	alu.dst.write = 1;
	r600_bytecode_add_alu(ctx.bc, &alu);

	/* R0.y = R0.x >> 30 */
	memset(&alu, 0, sizeof(alu));
	alu.op = ALU_OP2_LSHR_INT;
	alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
	alu.src[1].value = 0x1e;
	alu.dst.chan = 1;
	alu.dst.write = 1;
	alu.last = 1;
	r600_bytecode_add_alu(ctx.bc, &alu);

	/* fetch vertex data from GSVS ring */
	for (i = 0; i < ocnt; ++i) {
		struct r600_shader_io *out = &ctx.shader->output[i];

		out->gpr = i + 1;
		out->ring_offset = i * 16;

		memset(&vtx, 0, sizeof(vtx));
		vtx.op = FETCH_OP_VFETCH;
		vtx.buffer_id = R600_GS_RING_CONST_BUFFER;
		vtx.fetch_type = SQ_VTX_FETCH_NO_INDEX_OFFSET;
		vtx.mega_fetch_count = 16;
		vtx.offset = out->ring_offset;
		vtx.dst_gpr = out->gpr;
		vtx.src_gpr = 0;
		vtx.dst_sel_x = 0;
		vtx.dst_sel_y = 1;
		vtx.dst_sel_z = 2;
		vtx.dst_sel_w = 3;
		if (rctx->b.chip_class >= EVERGREEN) {
			vtx.use_const_fields = 1;
		} else {
			vtx.data_format = FMT_32_32_32_32_FLOAT;
		}

		r600_bytecode_add_vtx(ctx.bc, &vtx);
	}
	ctx.temp_reg = i + 1;
	for (ring = 3; ring >= 0; --ring) {
		bool enabled = false;
		for (i = 0; i < so->num_outputs; i++) {
			if (so->output[i].stream == ring) {
				enabled = true;
				if (ring > 0)
					only_ring_0 = false;
				break;
			}
		}
		if (ring != 0 && !enabled) {
			cshader->shader.ring_item_sizes[ring] = 0;
			continue;
		}

		if (cf_jump) {
			// Patch up jump label
			r600_bytecode_add_cfinst(ctx.bc, CF_OP_POP);
			cf_pop = ctx.bc->cf_last;

			cf_jump->cf_addr = cf_pop->id + 2;
			cf_jump->pop_count = 1;
			cf_pop->cf_addr = cf_pop->id + 2;
			cf_pop->pop_count = 1;
		}

		/* PRED_SETE_INT __, R0.y, ring */
		memset(&alu, 0, sizeof(alu));
		alu.op = ALU_OP2_PRED_SETE_INT;
		alu.src[0].chan = 1;
		alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
		alu.src[1].value = ring;
		alu.execute_mask = 1;
		alu.update_pred = 1;
		alu.last = 1;
		r600_bytecode_add_alu_type(ctx.bc, &alu, CF_OP_ALU_PUSH_BEFORE);

		r600_bytecode_add_cfinst(ctx.bc, CF_OP_JUMP);
		cf_jump = ctx.bc->cf_last;

		if (enabled)
			emit_streamout(&ctx, so, only_ring_0 ? -1 : ring, &cshader->shader.ring_item_sizes[ring]);
		cshader->shader.ring_item_sizes[ring] = ocnt * 16;
	}

	/* bc adds nops - copy it */
	if (ctx.bc->chip_class == R600) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP0_NOP;
		alu.last = 1;
		r600_bytecode_add_alu(ctx.bc, &alu);

		r600_bytecode_add_cfinst(ctx.bc, CF_OP_NOP);
	}

	/* export vertex data */
	/* XXX factor out common code with r600_shader_from_tgsi ? */
	for (i = 0; i < ocnt; ++i) {
		struct r600_shader_io *out = &ctx.shader->output[i];
		bool instream0 = true;
		if (out->name == TGSI_SEMANTIC_CLIPVERTEX)
			continue;

		for (j = 0; j < so->num_outputs; j++) {
			if (so->output[j].register_index == i) {
				if (so->output[j].stream == 0)
					break;
				if (so->output[j].stream > 0)
					instream0 = false;
			}
		}
		if (!instream0)
			continue;
		memset(&output, 0, sizeof(output));
		output.gpr = out->gpr;
		output.elem_size = 3;
		output.swizzle_x = 0;
		output.swizzle_y = 1;
		output.swizzle_z = 2;
		output.swizzle_w = 3;
		output.burst_count = 1;
		output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_PARAM;
		output.op = CF_OP_EXPORT;
		switch (out->name) {
		case TGSI_SEMANTIC_POSITION:
			output.array_base = 60;
			output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_POS;
			break;

		case TGSI_SEMANTIC_PSIZE:
			output.array_base = 61;
			if (next_clip_pos == 61)
				next_clip_pos = 62;
			output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_POS;
			output.swizzle_y = 7;
			output.swizzle_z = 7;
			output.swizzle_w = 7;
			ctx.shader->vs_out_misc_write = 1;
			ctx.shader->vs_out_point_size = 1;
			break;
		case TGSI_SEMANTIC_LAYER:
			if (out->spi_sid) {
				/* duplicate it as PARAM to pass to the pixel shader */
				output.array_base = next_param++;
				r600_bytecode_add_output(ctx.bc, &output);
				last_exp_param = ctx.bc->cf_last;
			}
			output.array_base = 61;
			if (next_clip_pos == 61)
				next_clip_pos = 62;
			output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_POS;
			output.swizzle_x = 7;
			output.swizzle_y = 7;
			output.swizzle_z = 0;
			output.swizzle_w = 7;
			ctx.shader->vs_out_misc_write = 1;
			ctx.shader->vs_out_layer = 1;
			break;
		case TGSI_SEMANTIC_VIEWPORT_INDEX:
			if (out->spi_sid) {
				/* duplicate it as PARAM to pass to the pixel shader */
				output.array_base = next_param++;
				r600_bytecode_add_output(ctx.bc, &output);
				last_exp_param = ctx.bc->cf_last;
			}
			output.array_base = 61;
			if (next_clip_pos == 61)
				next_clip_pos = 62;
			output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_POS;
			ctx.shader->vs_out_misc_write = 1;
			ctx.shader->vs_out_viewport = 1;
			output.swizzle_x = 7;
			output.swizzle_y = 7;
			output.swizzle_z = 7;
			output.swizzle_w = 0;
			break;
		case TGSI_SEMANTIC_CLIPDIST:
			/* spi_sid is 0 for clipdistance outputs that were generated
			 * for clipvertex - we don't need to pass them to PS */
			ctx.shader->clip_dist_write = gs->shader.clip_dist_write;
			ctx.shader->cull_dist_write = gs->shader.cull_dist_write;
			ctx.shader->cc_dist_mask = gs->shader.cc_dist_mask;
			if (out->spi_sid) {
				/* duplicate it as PARAM to pass to the pixel shader */
				output.array_base = next_param++;
				r600_bytecode_add_output(ctx.bc, &output);
				last_exp_param = ctx.bc->cf_last;
			}
			output.array_base = next_clip_pos++;
			output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_POS;
			break;
		case TGSI_SEMANTIC_FOG:
			output.swizzle_y = 4; /* 0 */
			output.swizzle_z = 4; /* 0 */
			output.swizzle_w = 5; /* 1 */
			break;
		default:
			output.array_base = next_param++;
			break;
		}
		r600_bytecode_add_output(ctx.bc, &output);
		if (output.type == V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_PARAM)
			last_exp_param = ctx.bc->cf_last;
		else
			last_exp_pos = ctx.bc->cf_last;
	}

	if (!last_exp_pos) {
		memset(&output, 0, sizeof(output));
		output.gpr = 0;
		output.elem_size = 3;
		output.swizzle_x = 7;
		output.swizzle_y = 7;
		output.swizzle_z = 7;
		output.swizzle_w = 7;
		output.burst_count = 1;
		output.type = 2;
		output.op = CF_OP_EXPORT;
		output.array_base = 60;
		output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_POS;
		r600_bytecode_add_output(ctx.bc, &output);
		last_exp_pos = ctx.bc->cf_last;
	}

	if (!last_exp_param) {
		memset(&output, 0, sizeof(output));
		output.gpr = 0;
		output.elem_size = 3;
		output.swizzle_x = 7;
		output.swizzle_y = 7;
		output.swizzle_z = 7;
		output.swizzle_w = 7;
		output.burst_count = 1;
		output.type = 2;
		output.op = CF_OP_EXPORT;
		output.array_base = next_param++;
		output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_PARAM;
		r600_bytecode_add_output(ctx.bc, &output);
		last_exp_param = ctx.bc->cf_last;
	}

	last_exp_pos->op = CF_OP_EXPORT_DONE;
	last_exp_param->op = CF_OP_EXPORT_DONE;

	r600_bytecode_add_cfinst(ctx.bc, CF_OP_POP);
	cf_pop = ctx.bc->cf_last;

	cf_jump->cf_addr = cf_pop->id + 2;
	cf_jump->pop_count = 1;
	cf_pop->cf_addr = cf_pop->id + 2;
	cf_pop->pop_count = 1;

	if (ctx.bc->chip_class == CAYMAN)
		cm_bytecode_add_cf_end(ctx.bc);
	else {
		r600_bytecode_add_cfinst(ctx.bc, CF_OP_NOP);
		ctx.bc->cf_last->end_of_program = 1;
	}

	gs->gs_copy_shader = cshader;
	cshader->enabled_stream_buffers_mask = ctx.enabled_stream_buffers_mask;

	ctx.bc->nstack = 1;

	return r600_bytecode_build(ctx.bc);
}

static int emit_inc_ring_offset(struct r600_shader_ctx *ctx, int idx, bool ind)
{
	if (ind) {
		struct r600_bytecode_alu alu;
		int r;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_ADD_INT;
		alu.src[0].sel = ctx->gs_export_gpr_tregs[idx];
		alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
		alu.src[1].value = ctx->gs_out_ring_offset >> 4;
		alu.dst.sel = ctx->gs_export_gpr_tregs[idx];
		alu.dst.write = 1;
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int emit_gs_ring_writes(struct r600_shader_ctx *ctx, const struct pipe_stream_output_info *so UNUSED, int stream, bool ind)
{
	struct r600_bytecode_output output;
	int ring_offset;
	unsigned i, k;
	int effective_stream = stream == -1 ? 0 : stream;
	int idx = 0;

	for (i = 0; i < ctx->shader->noutput; i++) {
		if (ctx->gs_for_vs) {
			/* for ES we need to lookup corresponding ring offset expected by GS
			 * (map this output to GS input by name and sid) */
			/* FIXME precompute offsets */
			ring_offset = -1;
			for(k = 0; k < ctx->gs_for_vs->ninput; ++k) {
				struct r600_shader_io *in = &ctx->gs_for_vs->input[k];
				struct r600_shader_io *out = &ctx->shader->output[i];
				if (in->name == out->name && in->sid == out->sid)
					ring_offset = in->ring_offset;
			}

			if (ring_offset == -1)
				continue;
		} else {
			ring_offset = idx * 16;
			idx++;
		}

		if (stream > 0 && ctx->shader->output[i].name == TGSI_SEMANTIC_POSITION)
			continue;
		/* next_ring_offset after parsing input decls contains total size of
		 * single vertex data, gs_next_vertex - current vertex index */
		if (!ind)
			ring_offset += ctx->gs_out_ring_offset * ctx->gs_next_vertex;

		memset(&output, 0, sizeof(struct r600_bytecode_output));
		output.gpr = ctx->shader->output[i].gpr;
		output.elem_size = 3;
		output.comp_mask = 0xF;
		output.burst_count = 1;

		if (ind)
			output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_WRITE_IND;
		else
			output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_WRITE;

		switch (stream) {
		default:
		case 0:
			output.op = CF_OP_MEM_RING; break;
		case 1:
			output.op = CF_OP_MEM_RING1; break;
		case 2:
			output.op = CF_OP_MEM_RING2; break;
		case 3:
			output.op = CF_OP_MEM_RING3; break;
		}

		if (ind) {
			output.array_base = ring_offset >> 2; /* in dwords */
			output.array_size = 0xfff;
			output.index_gpr = ctx->gs_export_gpr_tregs[effective_stream];
		} else
			output.array_base = ring_offset >> 2; /* in dwords */
		r600_bytecode_add_output(ctx->bc, &output);
	}

	++ctx->gs_next_vertex;
	return 0;
}


static int r600_fetch_tess_io_info(struct r600_shader_ctx *ctx)
{
	int r;
	struct r600_bytecode_vtx vtx;
	int temp_val = ctx->temp_reg;
	/* need to store the TCS output somewhere */
	r = single_alu_op2(ctx, ALU_OP1_MOV,
			   temp_val, 0,
			   V_SQ_ALU_SRC_LITERAL, 0,
			   0, 0);
	if (r)
		return r;

	/* used by VS/TCS */
	if (ctx->tess_input_info) {
		/* fetch tcs input values into resv space */
		memset(&vtx, 0, sizeof(struct r600_bytecode_vtx));
		vtx.op = FETCH_OP_VFETCH;
		vtx.buffer_id = R600_LDS_INFO_CONST_BUFFER;
		vtx.fetch_type = SQ_VTX_FETCH_NO_INDEX_OFFSET;
		vtx.mega_fetch_count = 16;
		vtx.data_format = FMT_32_32_32_32;
		vtx.num_format_all = 2;
		vtx.format_comp_all = 1;
		vtx.use_const_fields = 0;
		vtx.endian = r600_endian_swap(32);
		vtx.srf_mode_all = 1;
		vtx.offset = 0;
		vtx.dst_gpr = ctx->tess_input_info;
		vtx.dst_sel_x = 0;
		vtx.dst_sel_y = 1;
		vtx.dst_sel_z = 2;
		vtx.dst_sel_w = 3;
		vtx.src_gpr = temp_val;
		vtx.src_sel_x = 0;

		r = r600_bytecode_add_vtx(ctx->bc, &vtx);
		if (r)
			return r;
	}

	/* used by TCS/TES */
	if (ctx->tess_output_info) {
		/* fetch tcs output values into resv space */
		memset(&vtx, 0, sizeof(struct r600_bytecode_vtx));
		vtx.op = FETCH_OP_VFETCH;
		vtx.buffer_id = R600_LDS_INFO_CONST_BUFFER;
		vtx.fetch_type = SQ_VTX_FETCH_NO_INDEX_OFFSET;
		vtx.mega_fetch_count = 16;
		vtx.data_format = FMT_32_32_32_32;
		vtx.num_format_all = 2;
		vtx.format_comp_all = 1;
		vtx.use_const_fields = 0;
		vtx.endian = r600_endian_swap(32);
		vtx.srf_mode_all = 1;
		vtx.offset = 16;
		vtx.dst_gpr = ctx->tess_output_info;
		vtx.dst_sel_x = 0;
		vtx.dst_sel_y = 1;
		vtx.dst_sel_z = 2;
		vtx.dst_sel_w = 3;
		vtx.src_gpr = temp_val;
		vtx.src_sel_x = 0;

		r = r600_bytecode_add_vtx(ctx->bc, &vtx);
		if (r)
			return r;
	}
	return 0;
}

static int emit_lds_vs_writes(struct r600_shader_ctx *ctx)
{
	int j, r;
	int temp_reg;
	unsigned i;

	/* fetch tcs input values into input_vals */
	ctx->tess_input_info = r600_get_temp(ctx);
	ctx->tess_output_info = 0;
	r = r600_fetch_tess_io_info(ctx);
	if (r)
		return r;

	temp_reg = r600_get_temp(ctx);
	/* dst reg contains LDS address stride * idx */
	/* MUL vertexID, vertex_dw_stride */
	r = single_alu_op2(ctx, ALU_OP2_MUL_UINT24,
			   temp_reg, 0,
			   ctx->tess_input_info, 1,
			   0, 1); /* rel id in r0.y? */
	if (r)
		return r;

	for (i = 0; i < ctx->shader->noutput; i++) {
		struct r600_bytecode_alu alu;
		int param = r600_get_lds_unique_index(ctx->shader->output[i].name,
						      ctx->shader->output[i].sid);

		if (param) {
			r = single_alu_op2(ctx, ALU_OP2_ADD_INT,
					   temp_reg, 1,
					   temp_reg, 0,
					   V_SQ_ALU_SRC_LITERAL, param * 16);
			if (r)
				return r;
		}

		r = single_alu_op2(ctx, ALU_OP2_ADD_INT,
				   temp_reg, 2,
				   temp_reg, param ? 1 : 0,
				   V_SQ_ALU_SRC_LITERAL, 8);
		if (r)
			return r;


		for (j = 0; j < 2; j++) {
			int chan = (j == 1) ? 2 : (param ? 1 : 0);
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = LDS_OP3_LDS_WRITE_REL;
			alu.src[0].sel = temp_reg;
			alu.src[0].chan = chan;
			alu.src[1].sel = ctx->shader->output[i].gpr;
			alu.src[1].chan = j * 2;
			alu.src[2].sel = ctx->shader->output[i].gpr;
			alu.src[2].chan = (j * 2) + 1;
			alu.last = 1;
			alu.dst.chan = 0;
			alu.lds_idx = 1;
			alu.is_lds_idx_op = true;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
	}
	return 0;
}

static int r600_store_tcs_output(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	const struct tgsi_full_dst_register *dst = &inst->Dst[0];
	int i, r, lasti;
	int temp_reg = r600_get_temp(ctx);
	struct r600_bytecode_alu alu;
	unsigned write_mask = dst->Register.WriteMask;

	if (inst->Dst[0].Register.File != TGSI_FILE_OUTPUT)
		return 0;

	r = get_lds_offset0(ctx, 1, temp_reg, dst->Register.Dimension ? false : true);
	if (r)
		return r;

	/* the base address is now in temp.x */
	r = r600_get_byte_address(ctx, temp_reg,
				  &inst->Dst[0], NULL, ctx->tess_output_info, 1);
	if (r)
		return r;

	/* LDS write */
	lasti = tgsi_last_instruction(write_mask);
	for (i = 1; i <= lasti; i++) {

		if (!(write_mask & (1 << i)))
			continue;
		r = single_alu_op2(ctx, ALU_OP2_ADD_INT,
				   temp_reg, i,
				   temp_reg, 0,
				   V_SQ_ALU_SRC_LITERAL, 4 * i);
		if (r)
			return r;
	}

	for (i = 0; i <= lasti; i++) {
		if (!(write_mask & (1 << i)))
			continue;

		if ((i == 0 && ((write_mask & 3) == 3)) ||
		    (i == 2 && ((write_mask & 0xc) == 0xc))) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = LDS_OP3_LDS_WRITE_REL;
			alu.src[0].sel = temp_reg;
			alu.src[0].chan = i;

			alu.src[1].sel = dst->Register.Index;
			alu.src[1].sel += ctx->file_offset[dst->Register.File];
			alu.src[1].chan = i;

			alu.src[2].sel = dst->Register.Index;
			alu.src[2].sel += ctx->file_offset[dst->Register.File];
			alu.src[2].chan = i + 1;
			alu.lds_idx = 1;
			alu.dst.chan = 0;
			alu.last = 1;
			alu.is_lds_idx_op = true;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
			i += 1;
			continue;
		}
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = LDS_OP2_LDS_WRITE;
		alu.src[0].sel = temp_reg;
		alu.src[0].chan = i;

		alu.src[1].sel = dst->Register.Index;
		alu.src[1].sel += ctx->file_offset[dst->Register.File];
		alu.src[1].chan = i;

		alu.src[2].sel = V_SQ_ALU_SRC_0;
		alu.dst.chan = 0;
		alu.last = 1;
		alu.is_lds_idx_op = true;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int r600_tess_factor_read(struct r600_shader_ctx *ctx,
				 int output_idx, int nc)
{
	int param;
	unsigned temp_reg = r600_get_temp(ctx);
	unsigned name = ctx->shader->output[output_idx].name;
	int dreg = ctx->shader->output[output_idx].gpr;
	int r;

	param = r600_get_lds_unique_index(name, 0);
	r = get_lds_offset0(ctx, 1, temp_reg, true);
	if (r)
		return r;

	if (param) {
		r = single_alu_op2(ctx, ALU_OP2_ADD_INT,
				   temp_reg, 0,
				   temp_reg, 0,
				   V_SQ_ALU_SRC_LITERAL, param * 16);
		if (r)
			return r;
	}

	do_lds_fetch_values(ctx, temp_reg, dreg, ((1u << nc) - 1));
	return 0;
}

static int r600_emit_tess_factor(struct r600_shader_ctx *ctx)
{
	int stride, outer_comps, inner_comps;
	int tessinner_idx = -1, tessouter_idx = -1;
	int i, r;
	unsigned j;
	int temp_reg = r600_get_temp(ctx);
	int treg[3] = {-1, -1, -1};
	struct r600_bytecode_alu alu;
	struct r600_bytecode_cf *cf_jump, *cf_pop;

	/* only execute factor emission for invocation 0 */
	/* PRED_SETE_INT __, R0.x, 0 */
	memset(&alu, 0, sizeof(alu));
	alu.op = ALU_OP2_PRED_SETE_INT;
	alu.src[0].chan = 2;
	alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
	alu.execute_mask = 1;
	alu.update_pred = 1;
	alu.last = 1;
	r600_bytecode_add_alu_type(ctx->bc, &alu, CF_OP_ALU_PUSH_BEFORE);

	r600_bytecode_add_cfinst(ctx->bc, CF_OP_JUMP);
	cf_jump = ctx->bc->cf_last;

	treg[0] = r600_get_temp(ctx);
	switch (ctx->shader->tcs_prim_mode) {
	case PIPE_PRIM_LINES:
		stride = 8; /* 2 dwords, 1 vec2 store */
		outer_comps = 2;
		inner_comps = 0;
		break;
	case PIPE_PRIM_TRIANGLES:
		stride = 16; /* 4 dwords, 1 vec4 store */
		outer_comps = 3;
		inner_comps = 1;
		treg[1] = r600_get_temp(ctx);
		break;
	case PIPE_PRIM_QUADS:
		stride = 24; /* 6 dwords, 2 stores (vec4 + vec2) */
		outer_comps = 4;
		inner_comps = 2;
		treg[1] = r600_get_temp(ctx);
		treg[2] = r600_get_temp(ctx);
		break;
	default:
		assert(0);
		return -1;
	}

	/* R0 is InvocationID, RelPatchID, PatchID, tf_base */
	/* TF_WRITE takes index in R.x, value in R.y */
	for (j = 0; j < ctx->shader->noutput; j++) {
		if (ctx->shader->output[j].name == TGSI_SEMANTIC_TESSINNER)
			tessinner_idx = j;
		if (ctx->shader->output[j].name == TGSI_SEMANTIC_TESSOUTER)
			tessouter_idx = j;
	}

	if (tessouter_idx == -1)
		return -1;

	if (tessinner_idx == -1 && inner_comps)
		return -1;

	if (tessouter_idx != -1) {
		r = r600_tess_factor_read(ctx, tessouter_idx, outer_comps);
		if (r)
			return r;
	}

	if (tessinner_idx != -1) {
		r = r600_tess_factor_read(ctx, tessinner_idx, inner_comps);
		if (r)
			return r;
	}

	/* r.x = tf_base(r0.w) + relpatchid(r0.y) * tf_stride */
	/* r.x = relpatchid(r0.y) * tf_stride */

	/* multiply incoming r0.y * stride - t.x = r0.y * stride */
	/* add incoming r0.w to it: t.x = t.x + r0.w */
	r = single_alu_op3(ctx, ALU_OP3_MULADD_UINT24,
			   temp_reg, 0,
			   0, 1,
			   V_SQ_ALU_SRC_LITERAL, stride,
			   0, 3);
	if (r)
		return r;

	for (i = 0; i < outer_comps + inner_comps; i++) {
		int out_idx = i >= outer_comps ? tessinner_idx : tessouter_idx;
		int out_comp = i >= outer_comps ? i - outer_comps : i;

		if (ctx->shader->tcs_prim_mode == PIPE_PRIM_LINES) {
			if (out_comp == 1)
				out_comp = 0;
			else if (out_comp == 0)
				out_comp = 1;
		}

		r = single_alu_op2(ctx, ALU_OP2_ADD_INT,
				   treg[i / 2], (2 * (i % 2)),
				   temp_reg, 0,
				   V_SQ_ALU_SRC_LITERAL, 4 * i);
		if (r)
			return r;
		r = single_alu_op2(ctx, ALU_OP1_MOV,
				   treg[i / 2], 1 + (2 * (i%2)),
				   ctx->shader->output[out_idx].gpr, out_comp,
				   0, 0);
		if (r)
			return r;
	}
	for (i = 0; i < outer_comps + inner_comps; i++) {
		struct r600_bytecode_gds gds;

		memset(&gds, 0, sizeof(struct r600_bytecode_gds));
		gds.src_gpr = treg[i / 2];
		gds.src_sel_x = 2 * (i % 2);
		gds.src_sel_y = 1 + (2 * (i % 2));
		gds.src_sel_z = 4;
		gds.dst_sel_x = 7;
		gds.dst_sel_y = 7;
		gds.dst_sel_z = 7;
		gds.dst_sel_w = 7;
		gds.op = FETCH_OP_TF_WRITE;
		r = r600_bytecode_add_gds(ctx->bc, &gds);
		if (r)
			return r;
	}

	// Patch up jump label
	r600_bytecode_add_cfinst(ctx->bc, CF_OP_POP);
	cf_pop = ctx->bc->cf_last;

	cf_jump->cf_addr = cf_pop->id + 2;
	cf_jump->pop_count = 1;
	cf_pop->cf_addr = cf_pop->id + 2;
	cf_pop->pop_count = 1;

	return 0;
}

/*
 * We have to work out the thread ID for load and atomic
 * operations, which store the returned value to an index
 * in an intermediate buffer.
 * The index is calculated by taking the thread id,
 * calculated from the MBCNT instructions.
 * Then the shader engine ID is multiplied by 256,
 * and the wave id is added.
 * Then the result is multipled by 64 and thread id is
 * added.
 */
static int load_thread_id_gpr(struct r600_shader_ctx *ctx)
{
	struct r600_bytecode_alu alu;
	int r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MBCNT_32LO_ACCUM_PREV_INT;
	alu.dst.sel = ctx->temp_reg;
	alu.dst.chan = 0;
	alu.src[0].sel = V_SQ_ALU_SRC_LITERAL;
	alu.src[0].value = 0xffffffff;
	alu.dst.write = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MBCNT_32HI_INT;
	alu.dst.sel = ctx->temp_reg;
	alu.dst.chan = 1;
	alu.src[0].sel = V_SQ_ALU_SRC_LITERAL;
	alu.src[0].value = 0xffffffff;
	alu.dst.write = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP3_MULADD_UINT24;
	alu.dst.sel = ctx->temp_reg;
	alu.dst.chan = 2;
	alu.src[0].sel = EG_V_SQ_ALU_SRC_SE_ID;
	alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
	alu.src[1].value = 256;
	alu.src[2].sel = EG_V_SQ_ALU_SRC_HW_WAVE_ID;
	alu.dst.write = 1;
	alu.is_op3 = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	r = single_alu_op3(ctx, ALU_OP3_MULADD_UINT24,
			   ctx->thread_id_gpr, 1,
			   ctx->temp_reg, 2,
			   V_SQ_ALU_SRC_LITERAL, 0x40,
			   ctx->temp_reg, 0);
	if (r)
		return r;
	return 0;
}

static int r600_shader_from_tgsi(struct r600_context *rctx,
				 struct r600_pipe_shader *pipeshader,
				 union r600_shader_key key)
{
	struct r600_screen *rscreen = rctx->screen;
	struct r600_shader *shader = &pipeshader->shader;
	struct tgsi_token *tokens = pipeshader->selector->tokens;
	struct pipe_stream_output_info so = pipeshader->selector->so;
	struct tgsi_full_immediate *immediate;
	struct r600_shader_ctx ctx;
	struct r600_bytecode_output output[ARRAY_SIZE(shader->output)];
	unsigned output_done, noutput;
	unsigned opcode;
	int j, k, r = 0;
	unsigned i;
	int next_param_base = 0, next_clip_base;
	int max_color_exports = MAX2(key.ps.nr_cbufs, 1);
	bool indirect_gprs;
	bool ring_outputs = false;
	bool lds_outputs = false;
	bool lds_inputs = false;
	bool pos_emitted = false;

	ctx.bc = &shader->bc;
	ctx.shader = shader;

	r600_bytecode_init(ctx.bc, rscreen->b.chip_class, rscreen->b.family,
			   rscreen->has_compressed_msaa_texturing);
	ctx.tokens = tokens;
	tgsi_scan_shader(tokens, &ctx.info);
	shader->indirect_files = ctx.info.indirect_files;

	int narrays = ctx.info.array_max[TGSI_FILE_TEMPORARY];
	ctx.array_infos = calloc(narrays, sizeof(*ctx.array_infos));
	ctx.spilled_arrays = calloc(narrays, sizeof(bool));
	tgsi_scan_arrays(tokens, TGSI_FILE_TEMPORARY, narrays, ctx.array_infos);

	shader->uses_helper_invocation = false;
	shader->uses_doubles = ctx.info.uses_doubles;
	shader->uses_atomics = ctx.info.file_mask[TGSI_FILE_HW_ATOMIC];
	shader->nsys_inputs = 0;

	shader->uses_images = ctx.info.file_count[TGSI_FILE_IMAGE] > 0 ||
		ctx.info.file_count[TGSI_FILE_BUFFER] > 0;
	indirect_gprs = ctx.info.indirect_files & ~((1 << TGSI_FILE_CONSTANT) | (1 << TGSI_FILE_SAMPLER));
	tgsi_parse_init(&ctx.parse, tokens);
	ctx.type = ctx.info.processor;
	shader->processor_type = ctx.type;
	ctx.bc->type = shader->processor_type;

	switch (ctx.type) {
	case PIPE_SHADER_VERTEX:
		shader->vs_as_gs_a = key.vs.as_gs_a;
		shader->vs_as_es = key.vs.as_es;
		shader->vs_as_ls = key.vs.as_ls;
		shader->atomic_base = key.vs.first_atomic_counter;
		if (shader->vs_as_es)
			ring_outputs = true;
		if (shader->vs_as_ls)
			lds_outputs = true;
		break;
	case PIPE_SHADER_GEOMETRY:
		ring_outputs = true;
		shader->atomic_base = key.gs.first_atomic_counter;
		shader->gs_tri_strip_adj_fix = key.gs.tri_strip_adj_fix;
		break;
	case PIPE_SHADER_TESS_CTRL:
		shader->tcs_prim_mode = key.tcs.prim_mode;
		shader->atomic_base = key.tcs.first_atomic_counter;
		lds_outputs = true;
		lds_inputs = true;
		break;
	case PIPE_SHADER_TESS_EVAL:
		shader->tes_as_es = key.tes.as_es;
		shader->atomic_base = key.tes.first_atomic_counter;
		lds_inputs = true;
		if (shader->tes_as_es)
			ring_outputs = true;
		break;
	case PIPE_SHADER_FRAGMENT:
		shader->two_side = key.ps.color_two_side;
		shader->atomic_base = key.ps.first_atomic_counter;
		shader->rat_base = key.ps.nr_cbufs;
		shader->image_size_const_offset = key.ps.image_size_const_offset;
		break;
	case PIPE_SHADER_COMPUTE:
		shader->rat_base = 0;
		shader->image_size_const_offset = ctx.info.file_count[TGSI_FILE_SAMPLER];
		break;
	default:
		break;
	}

	if (shader->vs_as_es || shader->tes_as_es) {
		ctx.gs_for_vs = &rctx->gs_shader->current->shader;
	} else {
		ctx.gs_for_vs = NULL;
	}

	ctx.next_ring_offset = 0;
	ctx.gs_out_ring_offset = 0;
	ctx.gs_next_vertex = 0;
	ctx.gs_stream_output_info = &so;

	ctx.thread_id_gpr = -1;
	ctx.face_gpr = -1;
	ctx.fixed_pt_position_gpr = -1;
	ctx.fragcoord_input = -1;
	ctx.colors_used = 0;
	ctx.clip_vertex_write = 0;

	ctx.helper_invoc_reg = -1;
	ctx.cs_block_size_reg = -1;
	ctx.cs_grid_size_reg = -1;
	ctx.cs_block_size_loaded = false;
	ctx.cs_grid_size_loaded = false;

	shader->nr_ps_color_exports = 0;
	shader->nr_ps_max_color_exports = 0;


	/* register allocations */
	/* Values [0,127] correspond to GPR[0..127].
	 * Values [128,159] correspond to constant buffer bank 0
	 * Values [160,191] correspond to constant buffer bank 1
	 * Values [256,511] correspond to cfile constants c[0..255]. (Gone on EG)
	 * Values [256,287] correspond to constant buffer bank 2 (EG)
	 * Values [288,319] correspond to constant buffer bank 3 (EG)
	 * Other special values are shown in the list below.
	 * 244  ALU_SRC_1_DBL_L: special constant 1.0 double-float, LSW. (RV670+)
	 * 245  ALU_SRC_1_DBL_M: special constant 1.0 double-float, MSW. (RV670+)
	 * 246  ALU_SRC_0_5_DBL_L: special constant 0.5 double-float, LSW. (RV670+)
	 * 247  ALU_SRC_0_5_DBL_M: special constant 0.5 double-float, MSW. (RV670+)
	 * 248	SQ_ALU_SRC_0: special constant 0.0.
	 * 249	SQ_ALU_SRC_1: special constant 1.0 float.
	 * 250	SQ_ALU_SRC_1_INT: special constant 1 integer.
	 * 251	SQ_ALU_SRC_M_1_INT: special constant -1 integer.
	 * 252	SQ_ALU_SRC_0_5: special constant 0.5 float.
	 * 253	SQ_ALU_SRC_LITERAL: literal constant.
	 * 254	SQ_ALU_SRC_PV: previous vector result.
	 * 255	SQ_ALU_SRC_PS: previous scalar result.
	 */
	for (i = 0; i < TGSI_FILE_COUNT; i++) {
		ctx.file_offset[i] = 0;
	}

	if (ctx.type == PIPE_SHADER_VERTEX)  {

		ctx.file_offset[TGSI_FILE_INPUT] = 1;
		if (ctx.info.num_inputs)
			r600_bytecode_add_cfinst(ctx.bc, CF_OP_CALL_FS);
	}
	if (ctx.type == PIPE_SHADER_FRAGMENT) {
		if (ctx.bc->chip_class >= EVERGREEN)
			ctx.file_offset[TGSI_FILE_INPUT] = evergreen_gpr_count(&ctx);
		else
			ctx.file_offset[TGSI_FILE_INPUT] = allocate_system_value_inputs(&ctx, ctx.file_offset[TGSI_FILE_INPUT]);

		for (i = 0; i < PIPE_MAX_SHADER_INPUTS; i++) {
			if (ctx.info.system_value_semantic_name[i] == TGSI_SEMANTIC_HELPER_INVOCATION) {
				ctx.helper_invoc_reg = ctx.file_offset[TGSI_FILE_INPUT]++;
				shader->uses_helper_invocation = true;
			}
		}
	}
	if (ctx.type == PIPE_SHADER_GEOMETRY) {
		/* FIXME 1 would be enough in some cases (3 or less input vertices) */
		ctx.file_offset[TGSI_FILE_INPUT] = 2;
	}
	if (ctx.type == PIPE_SHADER_TESS_CTRL)
		ctx.file_offset[TGSI_FILE_INPUT] = 1;
	if (ctx.type == PIPE_SHADER_TESS_EVAL) {
		bool add_tesscoord = false, add_tess_inout = false;
		ctx.file_offset[TGSI_FILE_INPUT] = 1;
		for (i = 0; i < PIPE_MAX_SHADER_INPUTS; i++) {
			/* if we have tesscoord save one reg */
			if (ctx.info.system_value_semantic_name[i] == TGSI_SEMANTIC_TESSCOORD)
				add_tesscoord = true;
			if (ctx.info.system_value_semantic_name[i] == TGSI_SEMANTIC_TESSINNER ||
			    ctx.info.system_value_semantic_name[i] == TGSI_SEMANTIC_TESSOUTER)
				add_tess_inout = true;
		}
		if (add_tesscoord || add_tess_inout)
			ctx.file_offset[TGSI_FILE_INPUT]++;
		if (add_tess_inout)
			ctx.file_offset[TGSI_FILE_INPUT]+=2;
	}
	if (ctx.type == PIPE_SHADER_COMPUTE) {
		ctx.file_offset[TGSI_FILE_INPUT] = 2;
		for (i = 0; i < PIPE_MAX_SHADER_INPUTS; i++) {
			if (ctx.info.system_value_semantic_name[i] == TGSI_SEMANTIC_GRID_SIZE)
				ctx.cs_grid_size_reg = ctx.file_offset[TGSI_FILE_INPUT]++;
			if (ctx.info.system_value_semantic_name[i] == TGSI_SEMANTIC_BLOCK_SIZE)
				ctx.cs_block_size_reg = ctx.file_offset[TGSI_FILE_INPUT]++;
		}
	}

	ctx.file_offset[TGSI_FILE_OUTPUT] =
			ctx.file_offset[TGSI_FILE_INPUT] +
			ctx.info.file_max[TGSI_FILE_INPUT] + 1;
	ctx.file_offset[TGSI_FILE_TEMPORARY] = ctx.file_offset[TGSI_FILE_OUTPUT] +
						ctx.info.file_max[TGSI_FILE_OUTPUT] + 1;

	/* Outside the GPR range. This will be translated to one of the
	 * kcache banks later. */
	ctx.file_offset[TGSI_FILE_CONSTANT] = 512;
	ctx.file_offset[TGSI_FILE_IMMEDIATE] = V_SQ_ALU_SRC_LITERAL;

	pipeshader->scratch_space_needed = 0;
	int regno = ctx.file_offset[TGSI_FILE_TEMPORARY] +
			ctx.info.file_max[TGSI_FILE_TEMPORARY];
	if (regno > 124) {
		choose_spill_arrays(&ctx, &regno, &pipeshader->scratch_space_needed);
		shader->indirect_files = ctx.info.indirect_files;
	}
	shader->needs_scratch_space = pipeshader->scratch_space_needed != 0;

	ctx.bc->ar_reg = ++regno;
	ctx.bc->index_reg[0] = ++regno;
	ctx.bc->index_reg[1] = ++regno;

	if (ctx.type == PIPE_SHADER_TESS_CTRL) {
		ctx.tess_input_info = ++regno;
		ctx.tess_output_info = ++regno;
	} else if (ctx.type == PIPE_SHADER_TESS_EVAL) {
		ctx.tess_input_info = ++regno;
		ctx.tess_output_info = ++regno;
	} else if (ctx.type == PIPE_SHADER_GEOMETRY) {
		ctx.gs_export_gpr_tregs[0] = ++regno;
		ctx.gs_export_gpr_tregs[1] = ++regno;
		ctx.gs_export_gpr_tregs[2] = ++regno;
		ctx.gs_export_gpr_tregs[3] = ++regno;
		if (ctx.shader->gs_tri_strip_adj_fix) {
			ctx.gs_rotated_input[0] = ++regno;
			ctx.gs_rotated_input[1] = ++regno;
		} else {
			ctx.gs_rotated_input[0] = 0;
			ctx.gs_rotated_input[1] = 1;
		}
	}

	if (shader->uses_images) {
		ctx.thread_id_gpr = ++regno;
	}
	ctx.temp_reg = ++regno;

	shader->max_arrays = 0;
	shader->num_arrays = 0;
	if (indirect_gprs) {

		if (ctx.info.indirect_files & (1 << TGSI_FILE_INPUT)) {
			r600_add_gpr_array(shader, ctx.file_offset[TGSI_FILE_INPUT],
			                   ctx.file_offset[TGSI_FILE_OUTPUT] -
			                   ctx.file_offset[TGSI_FILE_INPUT],
			                   0x0F);
		}
		if (ctx.info.indirect_files & (1 << TGSI_FILE_OUTPUT)) {
			r600_add_gpr_array(shader, ctx.file_offset[TGSI_FILE_OUTPUT],
			                   ctx.file_offset[TGSI_FILE_TEMPORARY] -
			                   ctx.file_offset[TGSI_FILE_OUTPUT],
			                   0x0F);
		}
	}

	ctx.nliterals = 0;
	ctx.literals = NULL;
	ctx.max_driver_temp_used = 0;

	shader->fs_write_all = ctx.info.properties[TGSI_PROPERTY_FS_COLOR0_WRITES_ALL_CBUFS] &&
			       ctx.info.colors_written == 1;
	shader->vs_position_window_space = ctx.info.properties[TGSI_PROPERTY_VS_WINDOW_SPACE_POSITION];
	shader->ps_conservative_z = (uint8_t)ctx.info.properties[TGSI_PROPERTY_FS_DEPTH_LAYOUT];

	if (ctx.type == PIPE_SHADER_VERTEX ||
	    ctx.type == PIPE_SHADER_GEOMETRY ||
	    ctx.type == PIPE_SHADER_TESS_EVAL) {
		shader->cc_dist_mask = (1 << (ctx.info.properties[TGSI_PROPERTY_NUM_CULLDIST_ENABLED] +
					      ctx.info.properties[TGSI_PROPERTY_NUM_CLIPDIST_ENABLED])) - 1;
		shader->clip_dist_write = (1 << ctx.info.properties[TGSI_PROPERTY_NUM_CLIPDIST_ENABLED]) - 1;
		shader->cull_dist_write = ((1 << ctx.info.properties[TGSI_PROPERTY_NUM_CULLDIST_ENABLED]) - 1) << ctx.info.properties[TGSI_PROPERTY_NUM_CLIPDIST_ENABLED];
	}

	if (shader->vs_as_gs_a)
		vs_add_primid_output(&ctx, key.vs.prim_id_out);

	if (ctx.thread_id_gpr != -1) {
		r = load_thread_id_gpr(&ctx);
		if (r)
			return r;
	}

	if (ctx.type == PIPE_SHADER_TESS_EVAL)
		r600_fetch_tess_io_info(&ctx);

	while (!tgsi_parse_end_of_tokens(&ctx.parse)) {
		tgsi_parse_token(&ctx.parse);
		switch (ctx.parse.FullToken.Token.Type) {
		case TGSI_TOKEN_TYPE_IMMEDIATE:
			immediate = &ctx.parse.FullToken.FullImmediate;
			ctx.literals = realloc(ctx.literals, (ctx.nliterals + 1) * 16);
			if(ctx.literals == NULL) {
				r = -ENOMEM;
				goto out_err;
			}
			ctx.literals[ctx.nliterals * 4 + 0] = immediate->u[0].Uint;
			ctx.literals[ctx.nliterals * 4 + 1] = immediate->u[1].Uint;
			ctx.literals[ctx.nliterals * 4 + 2] = immediate->u[2].Uint;
			ctx.literals[ctx.nliterals * 4 + 3] = immediate->u[3].Uint;
			ctx.nliterals++;
			break;
		case TGSI_TOKEN_TYPE_DECLARATION:
			r = tgsi_declaration(&ctx);
			if (r)
				goto out_err;
			break;
		case TGSI_TOKEN_TYPE_INSTRUCTION:
		case TGSI_TOKEN_TYPE_PROPERTY:
			break;
		default:
			R600_ERR("unsupported token type %d\n", ctx.parse.FullToken.Token.Type);
			r = -EINVAL;
			goto out_err;
		}
	}

	shader->ring_item_sizes[0] = ctx.next_ring_offset;
	shader->ring_item_sizes[1] = 0;
	shader->ring_item_sizes[2] = 0;
	shader->ring_item_sizes[3] = 0;

	/* Process two side if needed */
	if (shader->two_side && ctx.colors_used) {
		int i, count = ctx.shader->ninput;
		unsigned next_lds_loc = ctx.shader->nlds;

		/* additional inputs will be allocated right after the existing inputs,
		 * we won't need them after the color selection, so we don't need to
		 * reserve these gprs for the rest of the shader code and to adjust
		 * output offsets etc. */
		int gpr = ctx.file_offset[TGSI_FILE_INPUT] +
				ctx.info.file_max[TGSI_FILE_INPUT] + 1;

		/* if two sided and neither face or sample mask is used by shader, ensure face_gpr is emitted */
		if (ctx.face_gpr == -1) {
			i = ctx.shader->ninput++;
			ctx.shader->input[i].name = TGSI_SEMANTIC_FACE;
			ctx.shader->input[i].spi_sid = 0;
			ctx.shader->input[i].gpr = gpr++;
			ctx.face_gpr = ctx.shader->input[i].gpr;
		}

		for (i = 0; i < count; i++) {
			if (ctx.shader->input[i].name == TGSI_SEMANTIC_COLOR) {
				int ni = ctx.shader->ninput++;
				memcpy(&ctx.shader->input[ni],&ctx.shader->input[i], sizeof(struct r600_shader_io));
				ctx.shader->input[ni].name = TGSI_SEMANTIC_BCOLOR;
				ctx.shader->input[ni].spi_sid = r600_spi_sid(&ctx.shader->input[ni]);
				ctx.shader->input[ni].gpr = gpr++;
				// TGSI to LLVM needs to know the lds position of inputs.
				// Non LLVM path computes it later (in process_twoside_color)
				ctx.shader->input[ni].lds_pos = next_lds_loc++;
				ctx.shader->input[i].back_color_input = ni;
				if (ctx.bc->chip_class >= EVERGREEN) {
					if ((r = evergreen_interp_input(&ctx, ni)))
						return r;
				}
			}
		}
	}

	if (shader->fs_write_all && rscreen->b.chip_class >= EVERGREEN)
		shader->nr_ps_max_color_exports = 8;

	if (ctx.shader->uses_helper_invocation) {
		if (ctx.bc->chip_class == CAYMAN)
			r = cm_load_helper_invocation(&ctx);
		else
			r = eg_load_helper_invocation(&ctx);
		if (r)
			return r;
	}

	/*
	 * XXX this relies on fixed_pt_position_gpr only being present when
	 * this shader should be executed per sample. Should be the case for now...
	 */
	if (ctx.fixed_pt_position_gpr != -1 && ctx.info.reads_samplemask) {
		/*
		 * Fix up sample mask. The hw always gives us coverage mask for
		 * the pixel. However, for per-sample shading, we need the
		 * coverage for the shader invocation only.
		 * Also, with disabled msaa, only the first bit should be set
		 * (luckily the same fixup works for both problems).
		 * For now, we can only do it if we know this shader is always
		 * executed per sample (due to usage of bits in the shader
		 * forcing per-sample execution).
		 * If the fb is not multisampled, we'd do unnecessary work but
		 * it should still be correct.
		 * It will however do nothing for sample shading according
		 * to MinSampleShading.
		 */
		struct r600_bytecode_alu alu;
		int tmp = r600_get_temp(&ctx);
		assert(ctx.face_gpr != -1);
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));

		alu.op = ALU_OP2_LSHL_INT;
		alu.src[0].sel = V_SQ_ALU_SRC_LITERAL;
		alu.src[0].value = 0x1;
		alu.src[1].sel = ctx.fixed_pt_position_gpr;
		alu.src[1].chan = 3;
		alu.dst.sel = tmp;
		alu.dst.chan = 0;
		alu.dst.write = 1;
		alu.last = 1;
		if ((r = r600_bytecode_add_alu(ctx.bc, &alu)))
			return r;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_AND_INT;
		alu.src[0].sel = tmp;
		alu.src[1].sel = ctx.face_gpr;
		alu.src[1].chan = 2;
		alu.dst.sel = ctx.face_gpr;
		alu.dst.chan = 2;
		alu.dst.write = 1;
		alu.last = 1;
		if ((r = r600_bytecode_add_alu(ctx.bc, &alu)))
			return r;
	}

	if (ctx.fragcoord_input >= 0) {
		if (ctx.bc->chip_class == CAYMAN) {
			for (j = 0 ; j < 4; j++) {
				struct r600_bytecode_alu alu;
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_RECIP_IEEE;
				alu.src[0].sel = shader->input[ctx.fragcoord_input].gpr;
				alu.src[0].chan = 3;

				alu.dst.sel = shader->input[ctx.fragcoord_input].gpr;
				alu.dst.chan = j;
				alu.dst.write = (j == 3);
				alu.last = (j == 3);
				if ((r = r600_bytecode_add_alu(ctx.bc, &alu)))
					return r;
			}
		} else {
			struct r600_bytecode_alu alu;
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_RECIP_IEEE;
			alu.src[0].sel = shader->input[ctx.fragcoord_input].gpr;
			alu.src[0].chan = 3;

			alu.dst.sel = shader->input[ctx.fragcoord_input].gpr;
			alu.dst.chan = 3;
			alu.dst.write = 1;
			alu.last = 1;
			if ((r = r600_bytecode_add_alu(ctx.bc, &alu)))
				return r;
		}
	}

	if (ctx.type == PIPE_SHADER_GEOMETRY) {
		struct r600_bytecode_alu alu;
		int r;

		/* GS thread with no output workaround - emit a cut at start of GS */
		if (ctx.bc->chip_class == R600)
			r600_bytecode_add_cfinst(ctx.bc, CF_OP_CUT_VERTEX);

		for (j = 0; j < 4; j++) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_MOV;
			alu.src[0].sel = V_SQ_ALU_SRC_LITERAL;
			alu.src[0].value = 0;
			alu.dst.sel = ctx.gs_export_gpr_tregs[j];
			alu.dst.write = 1;
			alu.last = 1;
			r = r600_bytecode_add_alu(ctx.bc, &alu);
			if (r)
				return r;
		}

		if (ctx.shader->gs_tri_strip_adj_fix) {
			r = single_alu_op2(&ctx, ALU_OP2_AND_INT,
					   ctx.gs_rotated_input[0], 2,
					   0, 2,
					   V_SQ_ALU_SRC_LITERAL, 1);
			if (r)
				return r;

			for (i = 0; i < 6; i++) {
				int rotated = (i + 4) % 6;
				int offset_reg = i / 3;
				int offset_chan = i % 3;
				int rotated_offset_reg = rotated / 3;
				int rotated_offset_chan = rotated % 3;

				if (offset_reg == 0 && offset_chan == 2)
					offset_chan = 3;
				if (rotated_offset_reg == 0 && rotated_offset_chan == 2)
					rotated_offset_chan = 3;

				r = single_alu_op3(&ctx, ALU_OP3_CNDE_INT,
						   ctx.gs_rotated_input[offset_reg], offset_chan,
						   ctx.gs_rotated_input[0], 2,
						   offset_reg, offset_chan,
						   rotated_offset_reg, rotated_offset_chan);
				if (r)
					return r;
			}
		}
	}

	if (ctx.type == PIPE_SHADER_TESS_CTRL)
		r600_fetch_tess_io_info(&ctx);

	if (shader->two_side && ctx.colors_used) {
		if ((r = process_twoside_color_inputs(&ctx)))
			return r;
	}

	tgsi_parse_init(&ctx.parse, tokens);
	while (!tgsi_parse_end_of_tokens(&ctx.parse)) {
		tgsi_parse_token(&ctx.parse);
		switch (ctx.parse.FullToken.Token.Type) {
		case TGSI_TOKEN_TYPE_INSTRUCTION:
			r = tgsi_is_supported(&ctx);
			if (r)
				goto out_err;
			ctx.max_driver_temp_used = 0;
			/* reserve first tmp for everyone */
			r600_get_temp(&ctx);

			opcode = ctx.parse.FullToken.FullInstruction.Instruction.Opcode;
			if ((r = tgsi_split_constant(&ctx)))
				goto out_err;
			if ((r = tgsi_split_literal_constant(&ctx)))
				goto out_err;
			if (ctx.type == PIPE_SHADER_GEOMETRY) {
				if ((r = tgsi_split_gs_inputs(&ctx)))
					goto out_err;
			} else if (lds_inputs) {
				if ((r = tgsi_split_lds_inputs(&ctx)))
					goto out_err;
			}
			if (ctx.bc->chip_class == CAYMAN)
				ctx.inst_info = &cm_shader_tgsi_instruction[opcode];
			else if (ctx.bc->chip_class >= EVERGREEN)
				ctx.inst_info = &eg_shader_tgsi_instruction[opcode];
			else
				ctx.inst_info = &r600_shader_tgsi_instruction[opcode];

			ctx.bc->precise |= ctx.parse.FullToken.FullInstruction.Instruction.Precise;

			r = ctx.inst_info->process(&ctx);
			if (r)
				goto out_err;

			if (ctx.type == PIPE_SHADER_TESS_CTRL) {
				r = r600_store_tcs_output(&ctx);
				if (r)
					goto out_err;
			}
			break;
		default:
			break;
		}
	}

	/* Reset the temporary register counter. */
	ctx.max_driver_temp_used = 0;

	noutput = shader->noutput;

	if (!ring_outputs && ctx.clip_vertex_write) {
		unsigned clipdist_temp[2];

		clipdist_temp[0] = r600_get_temp(&ctx);
		clipdist_temp[1] = r600_get_temp(&ctx);

		/* need to convert a clipvertex write into clipdistance writes and not export
		   the clip vertex anymore */

		memset(&shader->output[noutput], 0, 2*sizeof(struct r600_shader_io));
		shader->output[noutput].name = TGSI_SEMANTIC_CLIPDIST;
		shader->output[noutput].gpr = clipdist_temp[0];
		noutput++;
		shader->output[noutput].name = TGSI_SEMANTIC_CLIPDIST;
		shader->output[noutput].gpr = clipdist_temp[1];
		noutput++;

		/* reset spi_sid for clipvertex output to avoid confusing spi */
		shader->output[ctx.cv_output].spi_sid = 0;

		shader->clip_dist_write = 0xFF;
		shader->cc_dist_mask = 0xFF;

		for (i = 0; i < 8; i++) {
			int oreg = i >> 2;
			int ochan = i & 3;

			for (j = 0; j < 4; j++) {
				struct r600_bytecode_alu alu;
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP2_DOT4;
				alu.src[0].sel = shader->output[ctx.cv_output].gpr;
				alu.src[0].chan = j;

				alu.src[1].sel = 512 + i;
				alu.src[1].kc_bank = R600_BUFFER_INFO_CONST_BUFFER;
				alu.src[1].chan = j;

				alu.dst.sel = clipdist_temp[oreg];
				alu.dst.chan = j;
				alu.dst.write = (j == ochan);
				if (j == 3)
					alu.last = 1;
				r = r600_bytecode_add_alu(ctx.bc, &alu);
				if (r)
					return r;
			}
		}
	}

	/* Add stream outputs. */
	if (so.num_outputs) {
		bool emit = false;
		if (!lds_outputs && !ring_outputs && ctx.type == PIPE_SHADER_VERTEX)
			emit = true;
		if (!ring_outputs && ctx.type == PIPE_SHADER_TESS_EVAL)
			emit = true;
		if (emit)
			emit_streamout(&ctx, &so, -1, NULL);
	}
	pipeshader->enabled_stream_buffers_mask = ctx.enabled_stream_buffers_mask;
	convert_edgeflag_to_int(&ctx);

	if (ctx.type == PIPE_SHADER_TESS_CTRL)
		r600_emit_tess_factor(&ctx);

	if (lds_outputs) {
		if (ctx.type == PIPE_SHADER_VERTEX) {
			if (ctx.shader->noutput)
				emit_lds_vs_writes(&ctx);
		}
	} else if (ring_outputs) {
		if (shader->vs_as_es || shader->tes_as_es) {
			ctx.gs_export_gpr_tregs[0] = r600_get_temp(&ctx);
			ctx.gs_export_gpr_tregs[1] = -1;
			ctx.gs_export_gpr_tregs[2] = -1;
			ctx.gs_export_gpr_tregs[3] = -1;

			emit_gs_ring_writes(&ctx, &so, -1, FALSE);
		}
	} else {
		/* Export output */
		next_clip_base = shader->vs_out_misc_write ? 62 : 61;

		for (i = 0, j = 0; i < noutput; i++, j++) {
			memset(&output[j], 0, sizeof(struct r600_bytecode_output));
			output[j].gpr = shader->output[i].gpr;
			output[j].elem_size = 3;
			output[j].swizzle_x = 0;
			output[j].swizzle_y = 1;
			output[j].swizzle_z = 2;
			output[j].swizzle_w = 3;
			output[j].burst_count = 1;
			output[j].type = 0xffffffff;
			output[j].op = CF_OP_EXPORT;
			switch (ctx.type) {
			case PIPE_SHADER_VERTEX:
			case PIPE_SHADER_TESS_EVAL:
				switch (shader->output[i].name) {
				case TGSI_SEMANTIC_POSITION:
					output[j].array_base = 60;
					output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_POS;
					pos_emitted = true;
					break;

				case TGSI_SEMANTIC_PSIZE:
					output[j].array_base = 61;
					output[j].swizzle_y = 7;
					output[j].swizzle_z = 7;
					output[j].swizzle_w = 7;
					output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_POS;
					pos_emitted = true;
					break;
				case TGSI_SEMANTIC_EDGEFLAG:
					output[j].array_base = 61;
					output[j].swizzle_x = 7;
					output[j].swizzle_y = 0;
					output[j].swizzle_z = 7;
					output[j].swizzle_w = 7;
					output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_POS;
					pos_emitted = true;
					break;
				case TGSI_SEMANTIC_LAYER:
					/* spi_sid is 0 for outputs that are
					 * not consumed by PS */
					if (shader->output[i].spi_sid) {
						output[j].array_base = next_param_base++;
						output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_PARAM;
						j++;
						memcpy(&output[j], &output[j-1], sizeof(struct r600_bytecode_output));
					}
					output[j].array_base = 61;
					output[j].swizzle_x = 7;
					output[j].swizzle_y = 7;
					output[j].swizzle_z = 0;
					output[j].swizzle_w = 7;
					output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_POS;
					pos_emitted = true;
					break;
				case TGSI_SEMANTIC_VIEWPORT_INDEX:
					/* spi_sid is 0 for outputs that are
					 * not consumed by PS */
					if (shader->output[i].spi_sid) {
						output[j].array_base = next_param_base++;
						output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_PARAM;
						j++;
						memcpy(&output[j], &output[j-1], sizeof(struct r600_bytecode_output));
					}
					output[j].array_base = 61;
					output[j].swizzle_x = 7;
					output[j].swizzle_y = 7;
					output[j].swizzle_z = 7;
					output[j].swizzle_w = 0;
					output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_POS;
					pos_emitted = true;
					break;
				case TGSI_SEMANTIC_CLIPVERTEX:
					j--;
					break;
				case TGSI_SEMANTIC_CLIPDIST:
					output[j].array_base = next_clip_base++;
					output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_POS;
					pos_emitted = true;
					/* spi_sid is 0 for clipdistance outputs that were generated
					 * for clipvertex - we don't need to pass them to PS */
					if (shader->output[i].spi_sid) {
						j++;
						/* duplicate it as PARAM to pass to the pixel shader */
						memcpy(&output[j], &output[j-1], sizeof(struct r600_bytecode_output));
						output[j].array_base = next_param_base++;
						output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_PARAM;
					}
					break;
				case TGSI_SEMANTIC_FOG:
					output[j].swizzle_y = 4; /* 0 */
					output[j].swizzle_z = 4; /* 0 */
					output[j].swizzle_w = 5; /* 1 */
					break;
				case TGSI_SEMANTIC_PRIMID:
					output[j].swizzle_x = 2;
					output[j].swizzle_y = 4; /* 0 */
					output[j].swizzle_z = 4; /* 0 */
					output[j].swizzle_w = 4; /* 0 */
					break;
				}

				break;
			case PIPE_SHADER_FRAGMENT:
				if (shader->output[i].name == TGSI_SEMANTIC_COLOR) {
					/* never export more colors than the number of CBs */
					if (shader->output[i].sid >= max_color_exports) {
						/* skip export */
						j--;
						continue;
					}
					output[j].swizzle_w = key.ps.alpha_to_one ? 5 : 3;
					output[j].array_base = shader->output[i].sid;
					output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_PIXEL;
					shader->nr_ps_color_exports++;
					shader->ps_color_export_mask |= (0xf << (shader->output[i].sid * 4));

					/* If the i-th target format is set, all previous target formats must
					 * be non-zero to avoid hangs. - from radeonsi, seems to apply to eg as well.
					 */
					if (shader->output[i].sid > 0)
						for (unsigned x = 0; x < shader->output[i].sid; x++)
							shader->ps_color_export_mask |= (1 << (x*4));

					if (shader->output[i].sid > shader->ps_export_highest)
						shader->ps_export_highest = shader->output[i].sid;
					if (shader->fs_write_all && (rscreen->b.chip_class >= EVERGREEN)) {
						for (k = 1; k < max_color_exports; k++) {
							j++;
							memset(&output[j], 0, sizeof(struct r600_bytecode_output));
							output[j].gpr = shader->output[i].gpr;
							output[j].elem_size = 3;
							output[j].swizzle_x = 0;
							output[j].swizzle_y = 1;
							output[j].swizzle_z = 2;
							output[j].swizzle_w = key.ps.alpha_to_one ? 5 : 3;
							output[j].burst_count = 1;
							output[j].array_base = k;
							output[j].op = CF_OP_EXPORT;
							output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_PIXEL;
							shader->nr_ps_color_exports++;
							if (k > shader->ps_export_highest)
								shader->ps_export_highest = k;
							shader->ps_color_export_mask |= (0xf << (j * 4));
						}
					}
				} else if (shader->output[i].name == TGSI_SEMANTIC_POSITION) {
					output[j].array_base = 61;
					output[j].swizzle_x = 2;
					output[j].swizzle_y = 7;
					output[j].swizzle_z = output[j].swizzle_w = 7;
					output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_PIXEL;
				} else if (shader->output[i].name == TGSI_SEMANTIC_STENCIL) {
					output[j].array_base = 61;
					output[j].swizzle_x = 7;
					output[j].swizzle_y = 1;
					output[j].swizzle_z = output[j].swizzle_w = 7;
					output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_PIXEL;
				} else if (shader->output[i].name == TGSI_SEMANTIC_SAMPLEMASK) {
					output[j].array_base = 61;
					output[j].swizzle_x = 7;
					output[j].swizzle_y = 7;
					output[j].swizzle_z = 0;
					output[j].swizzle_w = 7;
					output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_PIXEL;
				} else {
					R600_ERR("unsupported fragment output name %d\n", shader->output[i].name);
					r = -EINVAL;
					goto out_err;
				}
				break;
			case PIPE_SHADER_TESS_CTRL:
				break;
			default:
				R600_ERR("unsupported processor type %d\n", ctx.type);
				r = -EINVAL;
				goto out_err;
			}

			if (output[j].type == 0xffffffff) {
				output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_PARAM;
				output[j].array_base = next_param_base++;
			}
		}

		/* add fake position export */
		if ((ctx.type == PIPE_SHADER_VERTEX || ctx.type == PIPE_SHADER_TESS_EVAL) && pos_emitted == false) {
			memset(&output[j], 0, sizeof(struct r600_bytecode_output));
			output[j].gpr = 0;
			output[j].elem_size = 3;
			output[j].swizzle_x = 7;
			output[j].swizzle_y = 7;
			output[j].swizzle_z = 7;
			output[j].swizzle_w = 7;
			output[j].burst_count = 1;
			output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_POS;
			output[j].array_base = 60;
			output[j].op = CF_OP_EXPORT;
			j++;
		}

		/* add fake param output for vertex shader if no param is exported */
		if ((ctx.type == PIPE_SHADER_VERTEX || ctx.type == PIPE_SHADER_TESS_EVAL) && next_param_base == 0) {
			memset(&output[j], 0, sizeof(struct r600_bytecode_output));
			output[j].gpr = 0;
			output[j].elem_size = 3;
			output[j].swizzle_x = 7;
			output[j].swizzle_y = 7;
			output[j].swizzle_z = 7;
			output[j].swizzle_w = 7;
			output[j].burst_count = 1;
			output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_PARAM;
			output[j].array_base = 0;
			output[j].op = CF_OP_EXPORT;
			j++;
		}

		/* add fake pixel export */
		if (ctx.type == PIPE_SHADER_FRAGMENT && shader->nr_ps_color_exports == 0) {
			memset(&output[j], 0, sizeof(struct r600_bytecode_output));
			output[j].gpr = 0;
			output[j].elem_size = 3;
			output[j].swizzle_x = 7;
			output[j].swizzle_y = 7;
			output[j].swizzle_z = 7;
			output[j].swizzle_w = 7;
			output[j].burst_count = 1;
			output[j].type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_PIXEL;
			output[j].array_base = 0;
			output[j].op = CF_OP_EXPORT;
			j++;
			shader->nr_ps_color_exports++;
			shader->ps_color_export_mask = 0xf;
		}

		noutput = j;

		/* set export done on last export of each type */
		for (k = noutput - 1, output_done = 0; k >= 0; k--) {
			if (!(output_done & (1 << output[k].type))) {
				output_done |= (1 << output[k].type);
				output[k].op = CF_OP_EXPORT_DONE;
			}
		}
		/* add output to bytecode */
		for (i = 0; i < noutput; i++) {
			r = r600_bytecode_add_output(ctx.bc, &output[i]);
			if (r)
				goto out_err;
		}
	}

	/* add program end */
	if (ctx.bc->chip_class == CAYMAN)
		cm_bytecode_add_cf_end(ctx.bc);
	else {
		const struct cf_op_info *last = NULL;

		if (ctx.bc->cf_last)
			last = r600_isa_cf(ctx.bc->cf_last->op);

		/* alu clause instructions don't have EOP bit, so add NOP */
		if (!last || last->flags & CF_ALU || ctx.bc->cf_last->op == CF_OP_LOOP_END || ctx.bc->cf_last->op == CF_OP_POP)
			r600_bytecode_add_cfinst(ctx.bc, CF_OP_NOP);

		ctx.bc->cf_last->end_of_program = 1;
	}

	/* check GPR limit - we have 124 = 128 - 4
	 * (4 are reserved as alu clause temporary registers) */
	if (ctx.bc->ngpr > 124) {
		R600_ERR("GPR limit exceeded - shader requires %d registers\n", ctx.bc->ngpr);
		r = -ENOMEM;
		goto out_err;
	}

	if (ctx.type == PIPE_SHADER_GEOMETRY) {
		if ((r = generate_gs_copy_shader(rctx, pipeshader, &so)))
			return r;
	}

	free(ctx.spilled_arrays);
	free(ctx.array_infos);
	free(ctx.literals);
	tgsi_parse_free(&ctx.parse);
	return 0;
out_err:
	free(ctx.spilled_arrays);
	free(ctx.array_infos);
	free(ctx.literals);
	tgsi_parse_free(&ctx.parse);
	return r;
}

static int tgsi_unsupported(struct r600_shader_ctx *ctx)
{
	const unsigned tgsi_opcode =
		ctx->parse.FullToken.FullInstruction.Instruction.Opcode;
	R600_ERR("%s tgsi opcode unsupported\n",
		 tgsi_get_opcode_name(tgsi_opcode));
	return -EINVAL;
}

static int tgsi_end(struct r600_shader_ctx *ctx UNUSED)
{
	return 0;
}

static void r600_bytecode_src(struct r600_bytecode_alu_src *bc_src,
			const struct r600_shader_src *shader_src,
			unsigned chan)
{
	bc_src->sel = shader_src->sel;
	bc_src->chan = shader_src->swizzle[chan];
	bc_src->neg = shader_src->neg;
	bc_src->abs = shader_src->abs;
	bc_src->rel = shader_src->rel;
	bc_src->value = shader_src->value[bc_src->chan];
	bc_src->kc_bank = shader_src->kc_bank;
	bc_src->kc_rel = shader_src->kc_rel;
}

static void r600_bytecode_src_set_abs(struct r600_bytecode_alu_src *bc_src)
{
	bc_src->abs = 1;
	bc_src->neg = 0;
}

static void r600_bytecode_src_toggle_neg(struct r600_bytecode_alu_src *bc_src)
{
	bc_src->neg = !bc_src->neg;
}

static void tgsi_dst(struct r600_shader_ctx *ctx,
		     const struct tgsi_full_dst_register *tgsi_dst,
		     unsigned swizzle,
		     struct r600_bytecode_alu_dst *r600_dst)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;

	if (tgsi_dst->Register.File == TGSI_FILE_TEMPORARY) {
		bool spilled;
		unsigned idx;

		idx = map_tgsi_reg_index_to_r600_gpr(ctx, tgsi_dst->Register.Index, &spilled);

		if (spilled) {
			struct r600_bytecode_output cf;
			int reg = 0;
			int r;
			bool add_pending_output = true;

			memset(&cf, 0, sizeof(struct r600_bytecode_output));
			get_spilled_array_base_and_size(ctx, tgsi_dst->Register.Index,
				&cf.array_base, &cf.array_size);

			/* If no component has spilled, reserve a register and add the spill code
			 *  ctx->bc->n_pending_outputs is cleared after each instruction group */
			if (ctx->bc->n_pending_outputs == 0) {
				reg = r600_get_temp(ctx);
			} else {
				/* If we are already spilling and the output address is the same like
				* before then just reuse the same slot */
				struct r600_bytecode_output *tmpl = &ctx->bc->pending_outputs[ctx->bc->n_pending_outputs-1];
				if ((cf.array_base + idx == tmpl->array_base) ||
				    (cf.array_base == tmpl->array_base &&
				     tmpl->index_gpr == ctx->bc->ar_reg &&
				     tgsi_dst->Register.Indirect)) {
					reg = ctx->bc->pending_outputs[0].gpr;
					add_pending_output = false;
				} else {
					reg = r600_get_temp(ctx);
				}
			}

			r600_dst->sel = reg;
			r600_dst->chan = swizzle;
			r600_dst->write = 1;
			if (inst->Instruction.Saturate) {
				r600_dst->clamp = 1;
			}

			/* Add new outputs as pending */
			if (add_pending_output) {
				cf.op = CF_OP_MEM_SCRATCH;
				cf.elem_size = 3;
				cf.gpr = reg;
				cf.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_WRITE;
				cf.mark = 1;
				cf.comp_mask = inst->Dst[0].Register.WriteMask;
				cf.swizzle_x = 0;
				cf.swizzle_y = 1;
				cf.swizzle_z = 2;
				cf.swizzle_w = 3;
				cf.burst_count = 1;

				if (tgsi_dst->Register.Indirect) {
					if (ctx->bc->chip_class < R700)
						cf.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_WRITE_IND;
					else
						cf.type = 3; // V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_WRITE_IND_ACK;
					cf.index_gpr = ctx->bc->ar_reg;
			}
			else {
				cf.array_base += idx;
				cf.array_size = 0;
			}

			r = r600_bytecode_add_pending_output(ctx->bc, &cf);
			if (r)
				return;

			if (ctx->bc->chip_class >= R700)
				r600_bytecode_need_wait_ack(ctx->bc, true);
			}
			return;
		}
		else {
			r600_dst->sel = idx;
		}
	}
	else {
		r600_dst->sel = tgsi_dst->Register.Index;
		r600_dst->sel += ctx->file_offset[tgsi_dst->Register.File];
	}
	r600_dst->chan = swizzle;
	r600_dst->write = 1;
	if (inst->Instruction.Saturate) {
		r600_dst->clamp = 1;
	}
	if (ctx->type == PIPE_SHADER_TESS_CTRL) {
		if (tgsi_dst->Register.File == TGSI_FILE_OUTPUT) {
			return;
		}
	}
	if (tgsi_dst->Register.Indirect)
		r600_dst->rel = V_SQ_REL_RELATIVE;

}

static int tgsi_op2_64_params(struct r600_shader_ctx *ctx, bool singledest, bool swap, int dest_temp, int op_override)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	unsigned write_mask = inst->Dst[0].Register.WriteMask;
	struct r600_bytecode_alu alu;
	int i, j, r, lasti = tgsi_last_instruction(write_mask);
	int use_tmp = 0;
	int swizzle_x = inst->Src[0].Register.SwizzleX;

	if (singledest) {
		switch (write_mask) {
		case 0x1:
			if (swizzle_x == 2) {
				write_mask = 0xc;
				use_tmp = 3;
			} else
				write_mask = 0x3;
			break;
		case 0x2:
			if (swizzle_x == 2) {
				write_mask = 0xc;
				use_tmp = 3;
			} else {
				write_mask = 0x3;
				use_tmp = 1;
			}
			break;
		case 0x4:
			if (swizzle_x == 0) {
				write_mask = 0x3;
				use_tmp = 1;
			} else
				write_mask = 0xc;
			break;
		case 0x8:
			if (swizzle_x == 0) {
				write_mask = 0x3;
				use_tmp = 1;
			} else {
				write_mask = 0xc;
				use_tmp = 3;
			}
			break;
		}
	}

	lasti = tgsi_last_instruction(write_mask);
	for (i = 0; i <= lasti; i++) {

		if (!(write_mask & (1 << i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));

		if (singledest) {
			if (use_tmp || dest_temp) {
				alu.dst.sel = use_tmp ? ctx->temp_reg : dest_temp;
				alu.dst.chan = i;
				alu.dst.write = 1;
			} else {
				tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
			}
			if (i == 1 || i == 3)
				alu.dst.write = 0;
		} else
			tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);

		alu.op = op_override ? op_override : ctx->inst_info->op;
		if (ctx->parse.FullToken.FullInstruction.Instruction.Opcode == TGSI_OPCODE_DABS) {
			r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
		} else if (!swap) {
			for (j = 0; j < inst->Instruction.NumSrcRegs; j++) {
				r600_bytecode_src(&alu.src[j], &ctx->src[j], fp64_switch(i));
			}
		} else {
			r600_bytecode_src(&alu.src[0], &ctx->src[1], fp64_switch(i));
			r600_bytecode_src(&alu.src[1], &ctx->src[0], fp64_switch(i));
		}

		/* handle some special cases */
		if (i == 1 || i == 3) {
			switch (ctx->parse.FullToken.FullInstruction.Instruction.Opcode) {
			case TGSI_OPCODE_DABS:
				r600_bytecode_src_set_abs(&alu.src[0]);
				break;
			default:
				break;
			}
		}
		if (i == lasti) {
			alu.last = 1;
		}
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	if (use_tmp) {
		write_mask = inst->Dst[0].Register.WriteMask;

		lasti = tgsi_last_instruction(write_mask);
		/* move result from temp to dst */
		for (i = 0; i <= lasti; i++) {
			if (!(write_mask & (1 << i)))
				continue;

			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_MOV;

			if (dest_temp) {
				alu.dst.sel = dest_temp;
				alu.dst.chan = i;
				alu.dst.write = 1;
			} else
				tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
			alu.src[0].sel = ctx->temp_reg;
			alu.src[0].chan = use_tmp - 1;
			alu.last = (i == lasti);

			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
	}
	return 0;
}

static int tgsi_op2_64(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	unsigned write_mask = inst->Dst[0].Register.WriteMask;
	/* confirm writemasking */
	if ((write_mask & 0x3) != 0x3 &&
	    (write_mask & 0xc) != 0xc) {
		fprintf(stderr, "illegal writemask for 64-bit: 0x%x\n", write_mask);
		return -1;
	}
	return tgsi_op2_64_params(ctx, false, false, 0, 0);
}

static int tgsi_op2_64_single_dest(struct r600_shader_ctx *ctx)
{
	return tgsi_op2_64_params(ctx, true, false, 0, 0);
}

static int tgsi_op2_64_single_dest_s(struct r600_shader_ctx *ctx)
{
	return tgsi_op2_64_params(ctx, true, true, 0, 0);
}

static int tgsi_op3_64(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, j, r;
	int lasti = 3;
	int tmp = r600_get_temp(ctx);

	for (i = 0; i < lasti + 1; i++) {

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ctx->inst_info->op;
		for (j = 0; j < inst->Instruction.NumSrcRegs; j++) {
			r600_bytecode_src(&alu.src[j], &ctx->src[j], i == 3 ? 0 : 1);
		}

		if (inst->Dst[0].Register.WriteMask & (1 << i))
			tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		else
			alu.dst.sel = tmp;

		alu.dst.chan = i;
		alu.is_op3 = 1;
		if (i == lasti) {
			alu.last = 1;
		}
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_op2_s(struct r600_shader_ctx *ctx, int swap, int trans_only)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	unsigned write_mask = inst->Dst[0].Register.WriteMask;
	int i, j, r, lasti = tgsi_last_instruction(write_mask);
	/* use temp register if trans_only and more than one dst component */
	int use_tmp = trans_only && (write_mask ^ (1 << lasti));
	unsigned op = ctx->inst_info->op;

	if (op == ALU_OP2_MUL_IEEE &&
	    ctx->info.properties[TGSI_PROPERTY_MUL_ZERO_WINS])
		op = ALU_OP2_MUL;

	for (i = 0; i <= lasti; i++) {
		if (!(write_mask & (1 << i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		if (use_tmp) {
			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = i;
			alu.dst.write = 1;
		} else
			tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);

		alu.op = op;
		if (!swap) {
			for (j = 0; j < inst->Instruction.NumSrcRegs; j++) {
				r600_bytecode_src(&alu.src[j], &ctx->src[j], i);
			}
		} else {
			r600_bytecode_src(&alu.src[0], &ctx->src[1], i);
			r600_bytecode_src(&alu.src[1], &ctx->src[0], i);
		}
		if (i == lasti || trans_only) {
			alu.last = 1;
		}
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	if (use_tmp) {
		/* move result from temp to dst */
		for (i = 0; i <= lasti; i++) {
			if (!(write_mask & (1 << i)))
				continue;

			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_MOV;
			tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
			alu.src[0].sel = ctx->temp_reg;
			alu.src[0].chan = i;
			alu.last = (i == lasti);

			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
	}
	return 0;
}

static int tgsi_op2(struct r600_shader_ctx *ctx)
{
	return tgsi_op2_s(ctx, 0, 0);
}

static int tgsi_op2_swap(struct r600_shader_ctx *ctx)
{
	return tgsi_op2_s(ctx, 1, 0);
}

static int tgsi_op2_trans(struct r600_shader_ctx *ctx)
{
	return tgsi_op2_s(ctx, 0, 1);
}

static int tgsi_ineg(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r;
	int lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);

	for (i = 0; i < lasti + 1; i++) {

		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ctx->inst_info->op;

		alu.src[0].sel = V_SQ_ALU_SRC_0;

		r600_bytecode_src(&alu.src[1], &ctx->src[0], i);

		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);

		if (i == lasti) {
			alu.last = 1;
		}
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;

}

static int tgsi_dneg(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r;
	int lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);

	for (i = 0; i < lasti + 1; i++) {

		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;

		r600_bytecode_src(&alu.src[0], &ctx->src[0], i);

		if (i == 1 || i == 3)
			r600_bytecode_src_toggle_neg(&alu.src[0]);
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);

		if (i == lasti) {
			alu.last = 1;
		}
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;

}

static int tgsi_dfracexp(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	unsigned write_mask = inst->Dst[0].Register.WriteMask;
	int i, j, r;

	for (i = 0; i <= 3; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ctx->inst_info->op;

		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = i;
		alu.dst.write = 1;
		for (j = 0; j < inst->Instruction.NumSrcRegs; j++) {
			r600_bytecode_src(&alu.src[j], &ctx->src[j], fp64_switch(i));
		}

		if (i == 3)
			alu.last = 1;

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	/* Replicate significand result across channels. */
	for (i = 0; i <= 3; i++) {
		if (!(write_mask & (1 << i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.src[0].chan = (i & 1) + 2;
		alu.src[0].sel = ctx->temp_reg;

		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.dst.write = 1;
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	for (i = 0; i <= 3; i++) {
		if (inst->Dst[1].Register.WriteMask & (1 << i)) {
			/* MOV third channels to writemask dst1 */
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_MOV;
			alu.src[0].chan = 1;
			alu.src[0].sel = ctx->temp_reg;

			tgsi_dst(ctx, &inst->Dst[1], i, &alu.dst);
			alu.last = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
			break;
		}
	}
	return 0;
}


static int egcm_int_to_double(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, c, r;
	int write_mask = inst->Dst[0].Register.WriteMask;
	int temp_reg = r600_get_temp(ctx);

	assert(inst->Instruction.Opcode == TGSI_OPCODE_I2D ||
		inst->Instruction.Opcode == TGSI_OPCODE_U2D);

	for (c = 0; c < 2; c++) {
		int dchan = c * 2;
		if (write_mask & (0x3 << dchan)) {
	/* split into 24-bit int and 8-bit int */
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP2_AND_INT;
			alu.dst.sel = temp_reg;
			alu.dst.chan = dchan;
			r600_bytecode_src(&alu.src[0], &ctx->src[0], c);
			alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
			alu.src[1].value = 0xffffff00;
			alu.dst.write = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;

			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP2_AND_INT;
			alu.dst.sel = temp_reg;
			alu.dst.chan = dchan + 1;
			r600_bytecode_src(&alu.src[0], &ctx->src[0], c);
			alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
			alu.src[1].value = 0xff;
			alu.dst.write = 1;
			alu.last = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
	}

	for (c = 0; c < 2; c++) {
		int dchan = c * 2;
		if (write_mask & (0x3 << dchan)) {
			for (i = dchan; i <= dchan + 1; i++) {
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = i == dchan ? ctx->inst_info->op : ALU_OP1_UINT_TO_FLT;

				alu.src[0].sel = temp_reg;
				alu.src[0].chan = i;
				alu.dst.sel = temp_reg;
				alu.dst.chan = i;
				alu.dst.write = 1;
				if (ctx->bc->chip_class == CAYMAN)
					alu.last = i == dchan + 1;
				else
					alu.last = 1; /* trans only ops on evergreen */
				
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}
		}
	}

	for (c = 0; c < 2; c++) {
		int dchan = c * 2;
		if (write_mask & (0x3 << dchan)) {
			for (i = 0; i < 4; i++) {
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_FLT32_TO_FLT64;

				alu.src[0].chan = dchan + (i / 2);
				if (i == 0 || i == 2)
					alu.src[0].sel = temp_reg;
				else {
					alu.src[0].sel = V_SQ_ALU_SRC_LITERAL;
					alu.src[0].value = 0x0;
				}
				alu.dst.sel = ctx->temp_reg;
				alu.dst.chan = i;
				alu.last = i == 3;
				alu.dst.write = 1;

				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}

			for (i = 0; i <= 1; i++) {
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP2_ADD_64;

				alu.src[0].chan = fp64_switch(i);
				alu.src[0].sel = ctx->temp_reg;

				alu.src[1].chan = fp64_switch(i + 2);
				alu.src[1].sel = ctx->temp_reg;
				tgsi_dst(ctx, &inst->Dst[0], dchan + i, &alu.dst);
				alu.last = i == 1;

				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}
		}
	}

	return 0;
}

static int egcm_double_to_int(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r;
	int lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);
	int treg = r600_get_temp(ctx);
	assert(inst->Instruction.Opcode == TGSI_OPCODE_D2I ||
		inst->Instruction.Opcode == TGSI_OPCODE_D2U);

	/* do a 64->32 into a temp register */
	r = tgsi_op2_64_params(ctx, true, false, treg, ALU_OP1_FLT64_TO_FLT32);
	if (r)
		return r;

	for (i = 0; i <= lasti; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ctx->inst_info->op;

		alu.src[0].chan = i;
		alu.src[0].sel = treg;
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.last = (i == lasti);

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	return 0;
}

static int cayman_emit_unary_double_raw(struct r600_bytecode *bc,
					unsigned op,
					int dst_reg,
					struct r600_shader_src *src,
					bool abs)
{
	struct r600_bytecode_alu alu;
	const int last_slot = 3;
	int r;

	/* these have to write the result to X/Y by the looks of it */
	for (int i = 0 ; i < last_slot; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = op;

		r600_bytecode_src(&alu.src[0], src, 1);
		r600_bytecode_src(&alu.src[1], src, 0);

		if (abs)
			r600_bytecode_src_set_abs(&alu.src[1]);

		alu.dst.sel = dst_reg;
		alu.dst.chan = i;
		alu.dst.write = (i == 0 || i == 1);

		if (bc->chip_class != CAYMAN || i == last_slot - 1)
			alu.last = 1;
		r = r600_bytecode_add_alu(bc, &alu);
		if (r)
			return r;
	}

	return 0;
}

static int cayman_emit_double_instr(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	int i, r;
	struct r600_bytecode_alu alu;
	int lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);
	int t1 = ctx->temp_reg;

	/* should only be one src regs */
	assert(inst->Instruction.NumSrcRegs == 1);

	/* only support one double at a time */
	assert(inst->Dst[0].Register.WriteMask == TGSI_WRITEMASK_XY ||
	       inst->Dst[0].Register.WriteMask == TGSI_WRITEMASK_ZW);

	r = cayman_emit_unary_double_raw(
		ctx->bc, ctx->inst_info->op, t1,
		&ctx->src[0],
		ctx->parse.FullToken.FullInstruction.Instruction.Opcode == TGSI_OPCODE_DRSQ ||
		ctx->parse.FullToken.FullInstruction.Instruction.Opcode == TGSI_OPCODE_DSQRT);
	if (r)
		return r;

	for (i = 0 ; i <= lasti; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.src[0].sel = t1;
		alu.src[0].chan = (i == 0 || i == 2) ? 0 : 1;
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.dst.write = 1;
		if (i == lasti)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int cayman_emit_float_instr(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	int i, j, r;
	struct r600_bytecode_alu alu;
	int last_slot = (inst->Dst[0].Register.WriteMask & 0x8) ? 4 : 3;

	for (i = 0 ; i < last_slot; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ctx->inst_info->op;
		for (j = 0; j < inst->Instruction.NumSrcRegs; j++) {
			r600_bytecode_src(&alu.src[j], &ctx->src[j], 0);

			/* RSQ should take the absolute value of src */
			if (inst->Instruction.Opcode == TGSI_OPCODE_RSQ) {
				r600_bytecode_src_set_abs(&alu.src[j]);
			}
		}
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.dst.write = (inst->Dst[0].Register.WriteMask >> i) & 1;

		if (i == last_slot - 1)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int cayman_mul_int_instr(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	int i, j, k, r;
	struct r600_bytecode_alu alu;
	int lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);
	int t1 = ctx->temp_reg;

	for (k = 0; k <= lasti; k++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << k)))
			continue;

		for (i = 0 ; i < 4; i++) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ctx->inst_info->op;
			for (j = 0; j < inst->Instruction.NumSrcRegs; j++) {
				r600_bytecode_src(&alu.src[j], &ctx->src[j], k);
			}
			alu.dst.sel = t1;
			alu.dst.chan = i;
			alu.dst.write = (i == k);
			if (i == 3)
				alu.last = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
	}

	for (i = 0 ; i <= lasti; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.src[0].sel = t1;
		alu.src[0].chan = i;
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.dst.write = 1;
		if (i == lasti)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	return 0;
}


static int cayman_mul_double_instr(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	int i, j, k, r;
	struct r600_bytecode_alu alu;
	int lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);
	int t1 = ctx->temp_reg;

	/* t1 would get overwritten below if we actually tried to
	 * multiply two pairs of doubles at a time. */
	assert(inst->Dst[0].Register.WriteMask == TGSI_WRITEMASK_XY ||
	       inst->Dst[0].Register.WriteMask == TGSI_WRITEMASK_ZW);

	k = inst->Dst[0].Register.WriteMask == TGSI_WRITEMASK_XY ? 0 : 1;

	for (i = 0; i < 4; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ctx->inst_info->op;
		for (j = 0; j < inst->Instruction.NumSrcRegs; j++) {
			r600_bytecode_src(&alu.src[j], &ctx->src[j], k * 2 + ((i == 3) ? 0 : 1));
		}
		alu.dst.sel = t1;
		alu.dst.chan = i;
		alu.dst.write = 1;
		if (i == 3)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	for (i = 0; i <= lasti; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.src[0].sel = t1;
		alu.src[0].chan = i;
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.dst.write = 1;
		if (i == lasti)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	return 0;
}

/*
 * Emit RECIP_64 + MUL_64 to implement division.
 */
static int cayman_ddiv_instr(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	int r;
	struct r600_bytecode_alu alu;
	int t1 = ctx->temp_reg;
	int k;

	/* Only support one double at a time. This is the same constraint as
	 * in DMUL lowering. */
	assert(inst->Dst[0].Register.WriteMask == TGSI_WRITEMASK_XY ||
	       inst->Dst[0].Register.WriteMask == TGSI_WRITEMASK_ZW);

	k = inst->Dst[0].Register.WriteMask == TGSI_WRITEMASK_XY ? 0 : 1;

	r = cayman_emit_unary_double_raw(ctx->bc, ALU_OP2_RECIP_64, t1, &ctx->src[1], false);
	if (r)
		return r;

	for (int i = 0; i < 4; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_MUL_64;

		r600_bytecode_src(&alu.src[0], &ctx->src[0], k * 2 + ((i == 3) ? 0 : 1));

		alu.src[1].sel = t1;
		alu.src[1].chan = (i == 3) ? 0 : 1;

		alu.dst.sel = t1;
		alu.dst.chan = i;
		alu.dst.write = 1;
		if (i == 3)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	for (int i = 0; i < 2; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.src[0].sel = t1;
		alu.src[0].chan = i;
		tgsi_dst(ctx, &inst->Dst[0], k * 2 + i, &alu.dst);
		alu.dst.write = 1;
		if (i == 1)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

/*
 * r600 - trunc to -PI..PI range
 * r700 - normalize by dividing by 2PI
 * see fdo bug 27901
 */
static int tgsi_setup_trig(struct r600_shader_ctx *ctx)
{
	int r;
	struct r600_bytecode_alu alu;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP3_MULADD;
	alu.is_op3 = 1;

	alu.dst.chan = 0;
	alu.dst.sel = ctx->temp_reg;
	alu.dst.write = 1;

	r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);

	alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
	alu.src[1].chan = 0;
	alu.src[1].value = u_bitcast_f2u(0.5f * M_1_PI);
	alu.src[2].sel = V_SQ_ALU_SRC_0_5;
	alu.src[2].chan = 0;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_FRACT;

	alu.dst.chan = 0;
	alu.dst.sel = ctx->temp_reg;
	alu.dst.write = 1;

	alu.src[0].sel = ctx->temp_reg;
	alu.src[0].chan = 0;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP3_MULADD;
	alu.is_op3 = 1;

	alu.dst.chan = 0;
	alu.dst.sel = ctx->temp_reg;
	alu.dst.write = 1;

	alu.src[0].sel = ctx->temp_reg;
	alu.src[0].chan = 0;

	alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
	alu.src[1].chan = 0;
	alu.src[2].sel = V_SQ_ALU_SRC_LITERAL;
	alu.src[2].chan = 0;

	if (ctx->bc->chip_class == R600) {
		alu.src[1].value = u_bitcast_f2u(2.0f * M_PI);
		alu.src[2].value = u_bitcast_f2u(-M_PI);
	} else {
		alu.src[1].sel = V_SQ_ALU_SRC_1;
		alu.src[2].sel = V_SQ_ALU_SRC_0_5;
		alu.src[2].neg = 1;
	}

	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	return 0;
}

static int cayman_trig(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int last_slot = (inst->Dst[0].Register.WriteMask & 0x8) ? 4 : 3;
	int i, r;

	r = tgsi_setup_trig(ctx);
	if (r)
		return r;


	for (i = 0; i < last_slot; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ctx->inst_info->op;
		alu.dst.chan = i;

		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.dst.write = (inst->Dst[0].Register.WriteMask >> i) & 1;

		alu.src[0].sel = ctx->temp_reg;
		alu.src[0].chan = 0;
		if (i == last_slot - 1)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_trig(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r;
	int lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);

	r = tgsi_setup_trig(ctx);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ctx->inst_info->op;
	alu.dst.chan = 0;
	alu.dst.sel = ctx->temp_reg;
	alu.dst.write = 1;

	alu.src[0].sel = ctx->temp_reg;
	alu.src[0].chan = 0;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	/* replicate result */
	for (i = 0; i < lasti + 1; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;

		alu.src[0].sel = ctx->temp_reg;
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		if (i == lasti)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_kill(struct r600_shader_ctx *ctx)
{
	const struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r;

	for (i = 0; i < 4; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ctx->inst_info->op;

		alu.dst.chan = i;

		alu.src[0].sel = V_SQ_ALU_SRC_0;

		if (inst->Instruction.Opcode == TGSI_OPCODE_KILL) {
			alu.src[1].sel = V_SQ_ALU_SRC_1;
			alu.src[1].neg = 1;
		} else {
			r600_bytecode_src(&alu.src[1], &ctx->src[0], i);
		}
		if (i == 3) {
			alu.last = 1;
		}
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	/* kill must be last in ALU */
	ctx->bc->force_add_cf = 1;
	ctx->shader->uses_kill = TRUE;
	return 0;
}

static int tgsi_lit(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int r;

	/* tmp.x = max(src.y, 0.0) */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP2_MAX;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 1);
	alu.src[1].sel  = V_SQ_ALU_SRC_0; /*0.0*/
	alu.src[1].chan = 1;

	alu.dst.sel = ctx->temp_reg;
	alu.dst.chan = 0;
	alu.dst.write = 1;

	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	if (inst->Dst[0].Register.WriteMask & (1 << 2))
	{
		int chan;
		int sel;
		unsigned i;

		if (ctx->bc->chip_class == CAYMAN) {
			for (i = 0; i < 3; i++) {
				/* tmp.z = log(tmp.x) */
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_LOG_CLAMPED;
				alu.src[0].sel = ctx->temp_reg;
				alu.src[0].chan = 0;
				alu.dst.sel = ctx->temp_reg;
				alu.dst.chan = i;
				if (i == 2) {
					alu.dst.write = 1;
					alu.last = 1;
				} else
					alu.dst.write = 0;

				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}
		} else {
			/* tmp.z = log(tmp.x) */
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_LOG_CLAMPED;
			alu.src[0].sel = ctx->temp_reg;
			alu.src[0].chan = 0;
			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = 2;
			alu.dst.write = 1;
			alu.last = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}

		chan = alu.dst.chan;
		sel = alu.dst.sel;

		/* tmp.x = amd MUL_LIT(tmp.z, src.w, src.x ) */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_MUL_LIT;
		alu.src[0].sel  = sel;
		alu.src[0].chan = chan;
		r600_bytecode_src(&alu.src[1], &ctx->src[0], 3);
		r600_bytecode_src(&alu.src[2], &ctx->src[0], 0);
		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = 0;
		alu.dst.write = 1;
		alu.is_op3 = 1;
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;

		if (ctx->bc->chip_class == CAYMAN) {
			for (i = 0; i < 3; i++) {
				/* dst.z = exp(tmp.x) */
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_EXP_IEEE;
				alu.src[0].sel = ctx->temp_reg;
				alu.src[0].chan = 0;
				tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
				if (i == 2) {
					alu.dst.write = 1;
					alu.last = 1;
				} else
					alu.dst.write = 0;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}
		} else {
			/* dst.z = exp(tmp.x) */
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_EXP_IEEE;
			alu.src[0].sel = ctx->temp_reg;
			alu.src[0].chan = 0;
			tgsi_dst(ctx, &inst->Dst[0], 2, &alu.dst);
			alu.last = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
	}

	/* dst.x, <- 1.0  */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	alu.src[0].sel  = V_SQ_ALU_SRC_1; /*1.0*/
	alu.src[0].chan = 0;
	tgsi_dst(ctx, &inst->Dst[0], 0, &alu.dst);
	alu.dst.write = (inst->Dst[0].Register.WriteMask >> 0) & 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	/* dst.y = max(src.x, 0.0) */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP2_MAX;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
	alu.src[1].sel  = V_SQ_ALU_SRC_0; /*0.0*/
	alu.src[1].chan = 0;
	tgsi_dst(ctx, &inst->Dst[0], 1, &alu.dst);
	alu.dst.write = (inst->Dst[0].Register.WriteMask >> 1) & 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	/* dst.w, <- 1.0  */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	alu.src[0].sel  = V_SQ_ALU_SRC_1;
	alu.src[0].chan = 0;
	tgsi_dst(ctx, &inst->Dst[0], 3, &alu.dst);
	alu.dst.write = (inst->Dst[0].Register.WriteMask >> 3) & 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	return 0;
}

static int tgsi_rsq(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));

	alu.op = ALU_OP1_RECIPSQRT_IEEE;

	for (i = 0; i < inst->Instruction.NumSrcRegs; i++) {
		r600_bytecode_src(&alu.src[i], &ctx->src[i], 0);
		r600_bytecode_src_set_abs(&alu.src[i]);
	}
	alu.dst.sel = ctx->temp_reg;
	alu.dst.write = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	/* replicate result */
	return tgsi_helper_tempx_replicate(ctx);
}

static int tgsi_helper_tempx_replicate(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r;

	for (i = 0; i < 4; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.src[0].sel = ctx->temp_reg;
		alu.op = ALU_OP1_MOV;
		alu.dst.chan = i;
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.dst.write = (inst->Dst[0].Register.WriteMask >> i) & 1;
		if (i == 3)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_trans_srcx_replicate(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ctx->inst_info->op;
	for (i = 0; i < inst->Instruction.NumSrcRegs; i++) {
		r600_bytecode_src(&alu.src[i], &ctx->src[i], 0);
	}
	alu.dst.sel = ctx->temp_reg;
	alu.dst.write = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	/* replicate result */
	return tgsi_helper_tempx_replicate(ctx);
}

static int cayman_pow(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	int i, r;
	struct r600_bytecode_alu alu;
	int last_slot = (inst->Dst[0].Register.WriteMask & 0x8) ? 4 : 3;

	for (i = 0; i < 3; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_LOG_IEEE;
		r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = i;
		alu.dst.write = 1;
		if (i == 2)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	/* b * LOG2(a) */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP2_MUL;
	r600_bytecode_src(&alu.src[0], &ctx->src[1], 0);
	alu.src[1].sel = ctx->temp_reg;
	alu.dst.sel = ctx->temp_reg;
	alu.dst.write = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	for (i = 0; i < last_slot; i++) {
		/* POW(a,b) = EXP2(b * LOG2(a))*/
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_EXP_IEEE;
		alu.src[0].sel = ctx->temp_reg;

		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.dst.write = (inst->Dst[0].Register.WriteMask >> i) & 1;
		if (i == last_slot - 1)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_pow(struct r600_shader_ctx *ctx)
{
	struct r600_bytecode_alu alu;
	int r;

	/* LOG2(a) */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_LOG_IEEE;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
	alu.dst.sel = ctx->temp_reg;
	alu.dst.write = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	/* b * LOG2(a) */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP2_MUL;
	r600_bytecode_src(&alu.src[0], &ctx->src[1], 0);
	alu.src[1].sel = ctx->temp_reg;
	alu.dst.sel = ctx->temp_reg;
	alu.dst.write = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	/* POW(a,b) = EXP2(b * LOG2(a))*/
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_EXP_IEEE;
	alu.src[0].sel = ctx->temp_reg;
	alu.dst.sel = ctx->temp_reg;
	alu.dst.write = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	return tgsi_helper_tempx_replicate(ctx);
}

static int emit_mul_int_op(struct r600_bytecode *bc,
			   struct r600_bytecode_alu *alu_src)
{
	struct r600_bytecode_alu alu;
	int i, r;
	alu = *alu_src;
	if (bc->chip_class == CAYMAN) {
		for (i = 0; i < 4; i++) {
			alu.dst.chan = i;
			alu.dst.write = (i == alu_src->dst.chan);
			alu.last = (i == 3);

			r = r600_bytecode_add_alu(bc, &alu);
			if (r)
				return r;
		}
	} else {
		alu.last = 1;
		r = r600_bytecode_add_alu(bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_divmod(struct r600_shader_ctx *ctx, int mod, int signed_op)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r, j;
	unsigned write_mask = inst->Dst[0].Register.WriteMask;
	int lasti = tgsi_last_instruction(write_mask);
	int tmp0 = ctx->temp_reg;
	int tmp1 = r600_get_temp(ctx);
	int tmp2 = r600_get_temp(ctx);
	int tmp3 = r600_get_temp(ctx);
	int tmp4 = 0;

	/* Use additional temp if dst register and src register are the same */
	if (inst->Src[0].Register.Index == inst->Dst[0].Register.Index ||
	    inst->Src[1].Register.Index == inst->Dst[0].Register.Index) {
		tmp4 = r600_get_temp(ctx);
	}

	/* Unsigned path:
	 *
	 * we need to represent src1 as src2*q + r, where q - quotient, r - remainder
	 *
	 * 1. tmp0.x = rcp (src2)     = 2^32/src2 + e, where e is rounding error
	 * 2. tmp0.z = lo (tmp0.x * src2)
	 * 3. tmp0.w = -tmp0.z
	 * 4. tmp0.y = hi (tmp0.x * src2)
	 * 5. tmp0.z = (tmp0.y == 0 ? tmp0.w : tmp0.z)      = abs(lo(rcp*src2))
	 * 6. tmp0.w = hi (tmp0.z * tmp0.x)    = e, rounding error
	 * 7. tmp1.x = tmp0.x - tmp0.w
	 * 8. tmp1.y = tmp0.x + tmp0.w
	 * 9. tmp0.x = (tmp0.y == 0 ? tmp1.y : tmp1.x)
	 * 10. tmp0.z = hi(tmp0.x * src1)     = q
	 * 11. tmp0.y = lo (tmp0.z * src2)     = src2*q = src1 - r
	 *
	 * 12. tmp0.w = src1 - tmp0.y       = r
	 * 13. tmp1.x = tmp0.w >= src2		= r >= src2 (uint comparison)
	 * 14. tmp1.y = src1 >= tmp0.y      = r >= 0 (uint comparison)
	 *
	 * if DIV
	 *
	 *   15. tmp1.z = tmp0.z + 1			= q + 1
	 *   16. tmp1.w = tmp0.z - 1			= q - 1
	 *
	 * else MOD
	 *
	 *   15. tmp1.z = tmp0.w - src2			= r - src2
	 *   16. tmp1.w = tmp0.w + src2			= r + src2
	 *
	 * endif
	 *
	 * 17. tmp1.x = tmp1.x & tmp1.y
	 *
	 * DIV: 18. tmp0.z = tmp1.x==0 ? tmp0.z : tmp1.z
	 * MOD: 18. tmp0.z = tmp1.x==0 ? tmp0.w : tmp1.z
	 *
	 * 19. tmp0.z = tmp1.y==0 ? tmp1.w : tmp0.z
	 * 20. dst = src2==0 ? MAX_UINT : tmp0.z
	 *
	 * Signed path:
	 *
	 * Same as unsigned, using abs values of the operands,
	 * and fixing the sign of the result in the end.
	 */

	for (i = 0; i < 4; i++) {
		if (!(write_mask & (1<<i)))
			continue;

		if (signed_op) {

			/* tmp2.x = -src0 */
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP2_SUB_INT;

			alu.dst.sel = tmp2;
			alu.dst.chan = 0;
			alu.dst.write = 1;

			alu.src[0].sel = V_SQ_ALU_SRC_0;

			r600_bytecode_src(&alu.src[1], &ctx->src[0], i);

			alu.last = 1;
			if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
				return r;

			/* tmp2.y = -src1 */
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP2_SUB_INT;

			alu.dst.sel = tmp2;
			alu.dst.chan = 1;
			alu.dst.write = 1;

			alu.src[0].sel = V_SQ_ALU_SRC_0;

			r600_bytecode_src(&alu.src[1], &ctx->src[1], i);

			alu.last = 1;
			if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
				return r;

			/* tmp2.z sign bit is set if src0 and src2 signs are different */
			/* it will be a sign of the quotient */
			if (!mod) {

				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP2_XOR_INT;

				alu.dst.sel = tmp2;
				alu.dst.chan = 2;
				alu.dst.write = 1;

				r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
				r600_bytecode_src(&alu.src[1], &ctx->src[1], i);

				alu.last = 1;
				if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
					return r;
			}

			/* tmp2.x = |src0| */
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP3_CNDGE_INT;
			alu.is_op3 = 1;

			alu.dst.sel = tmp2;
			alu.dst.chan = 0;
			alu.dst.write = 1;

			r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
			r600_bytecode_src(&alu.src[1], &ctx->src[0], i);
			alu.src[2].sel = tmp2;
			alu.src[2].chan = 0;

			alu.last = 1;
			if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
				return r;

			/* tmp2.y = |src1| */
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP3_CNDGE_INT;
			alu.is_op3 = 1;

			alu.dst.sel = tmp2;
			alu.dst.chan = 1;
			alu.dst.write = 1;

			r600_bytecode_src(&alu.src[0], &ctx->src[1], i);
			r600_bytecode_src(&alu.src[1], &ctx->src[1], i);
			alu.src[2].sel = tmp2;
			alu.src[2].chan = 1;

			alu.last = 1;
			if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
				return r;

		}

		/* 1. tmp0.x = rcp_u (src2)     = 2^32/src2 + e, where e is rounding error */
		if (ctx->bc->chip_class == CAYMAN) {
			/* tmp3.x = u2f(src2) */
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_UINT_TO_FLT;

			alu.dst.sel = tmp3;
			alu.dst.chan = 0;
			alu.dst.write = 1;

			if (signed_op) {
				alu.src[0].sel = tmp2;
				alu.src[0].chan = 1;
			} else {
				r600_bytecode_src(&alu.src[0], &ctx->src[1], i);
			}

			alu.last = 1;
			if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
				return r;

			/* tmp0.x = recip(tmp3.x) */
			for (j = 0 ; j < 3; j++) {
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_RECIP_IEEE;

				alu.dst.sel = tmp0;
				alu.dst.chan = j;
				alu.dst.write = (j == 0);

				alu.src[0].sel = tmp3;
				alu.src[0].chan = 0;

				if (j == 2)
					alu.last = 1;
				if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
					return r;
			}

			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP2_MUL;

			alu.src[0].sel = tmp0;
			alu.src[0].chan = 0;

			alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
			alu.src[1].value = 0x4f800000;

			alu.dst.sel = tmp3;
			alu.dst.write = 1;
			alu.last = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;

			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_FLT_TO_UINT;

			alu.dst.sel = tmp0;
			alu.dst.chan = 0;
			alu.dst.write = 1;

			alu.src[0].sel = tmp3;
			alu.src[0].chan = 0;

			alu.last = 1;
			if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
				return r;

		} else {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_RECIP_UINT;

			alu.dst.sel = tmp0;
			alu.dst.chan = 0;
			alu.dst.write = 1;

			if (signed_op) {
				alu.src[0].sel = tmp2;
				alu.src[0].chan = 1;
			} else {
				r600_bytecode_src(&alu.src[0], &ctx->src[1], i);
			}

			alu.last = 1;
			if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
				return r;
		}

		/* 2. tmp0.z = lo (tmp0.x * src2) */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_MULLO_UINT;

		alu.dst.sel = tmp0;
		alu.dst.chan = 2;
		alu.dst.write = 1;

		alu.src[0].sel = tmp0;
		alu.src[0].chan = 0;
		if (signed_op) {
			alu.src[1].sel = tmp2;
			alu.src[1].chan = 1;
		} else {
			r600_bytecode_src(&alu.src[1], &ctx->src[1], i);
		}

		if ((r = emit_mul_int_op(ctx->bc, &alu)))
			return r;

		/* 3. tmp0.w = -tmp0.z */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_SUB_INT;

		alu.dst.sel = tmp0;
		alu.dst.chan = 3;
		alu.dst.write = 1;

		alu.src[0].sel = V_SQ_ALU_SRC_0;
		alu.src[1].sel = tmp0;
		alu.src[1].chan = 2;

		alu.last = 1;
		if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
			return r;

		/* 4. tmp0.y = hi (tmp0.x * src2) */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_MULHI_UINT;

		alu.dst.sel = tmp0;
		alu.dst.chan = 1;
		alu.dst.write = 1;

		alu.src[0].sel = tmp0;
		alu.src[0].chan = 0;

		if (signed_op) {
			alu.src[1].sel = tmp2;
			alu.src[1].chan = 1;
		} else {
			r600_bytecode_src(&alu.src[1], &ctx->src[1], i);
		}

		if ((r = emit_mul_int_op(ctx->bc, &alu)))
			return r;

		/* 5. tmp0.z = (tmp0.y == 0 ? tmp0.w : tmp0.z)      = abs(lo(rcp*src)) */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_CNDE_INT;
		alu.is_op3 = 1;

		alu.dst.sel = tmp0;
		alu.dst.chan = 2;
		alu.dst.write = 1;

		alu.src[0].sel = tmp0;
		alu.src[0].chan = 1;
		alu.src[1].sel = tmp0;
		alu.src[1].chan = 3;
		alu.src[2].sel = tmp0;
		alu.src[2].chan = 2;

		alu.last = 1;
		if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
			return r;

		/* 6. tmp0.w = hi (tmp0.z * tmp0.x)    = e, rounding error */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_MULHI_UINT;

		alu.dst.sel = tmp0;
		alu.dst.chan = 3;
		alu.dst.write = 1;

		alu.src[0].sel = tmp0;
		alu.src[0].chan = 2;

		alu.src[1].sel = tmp0;
		alu.src[1].chan = 0;

		if ((r = emit_mul_int_op(ctx->bc, &alu)))
				return r;

		/* 7. tmp1.x = tmp0.x - tmp0.w */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_SUB_INT;

		alu.dst.sel = tmp1;
		alu.dst.chan = 0;
		alu.dst.write = 1;

		alu.src[0].sel = tmp0;
		alu.src[0].chan = 0;
		alu.src[1].sel = tmp0;
		alu.src[1].chan = 3;

		alu.last = 1;
		if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
			return r;

		/* 8. tmp1.y = tmp0.x + tmp0.w */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_ADD_INT;

		alu.dst.sel = tmp1;
		alu.dst.chan = 1;
		alu.dst.write = 1;

		alu.src[0].sel = tmp0;
		alu.src[0].chan = 0;
		alu.src[1].sel = tmp0;
		alu.src[1].chan = 3;

		alu.last = 1;
		if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
			return r;

		/* 9. tmp0.x = (tmp0.y == 0 ? tmp1.y : tmp1.x) */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_CNDE_INT;
		alu.is_op3 = 1;

		alu.dst.sel = tmp0;
		alu.dst.chan = 0;
		alu.dst.write = 1;

		alu.src[0].sel = tmp0;
		alu.src[0].chan = 1;
		alu.src[1].sel = tmp1;
		alu.src[1].chan = 1;
		alu.src[2].sel = tmp1;
		alu.src[2].chan = 0;

		alu.last = 1;
		if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
			return r;

		/* 10. tmp0.z = hi(tmp0.x * src1)     = q */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_MULHI_UINT;

		alu.dst.sel = tmp0;
		alu.dst.chan = 2;
		alu.dst.write = 1;

		alu.src[0].sel = tmp0;
		alu.src[0].chan = 0;

		if (signed_op) {
			alu.src[1].sel = tmp2;
			alu.src[1].chan = 0;
		} else {
			r600_bytecode_src(&alu.src[1], &ctx->src[0], i);
		}

		if ((r = emit_mul_int_op(ctx->bc, &alu)))
			return r;

		/* 11. tmp0.y = lo (src2 * tmp0.z)     = src2*q = src1 - r */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_MULLO_UINT;

		alu.dst.sel = tmp0;
		alu.dst.chan = 1;
		alu.dst.write = 1;

		if (signed_op) {
			alu.src[0].sel = tmp2;
			alu.src[0].chan = 1;
		} else {
			r600_bytecode_src(&alu.src[0], &ctx->src[1], i);
		}

		alu.src[1].sel = tmp0;
		alu.src[1].chan = 2;

		if ((r = emit_mul_int_op(ctx->bc, &alu)))
			return r;

		/* 12. tmp0.w = src1 - tmp0.y       = r */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_SUB_INT;

		alu.dst.sel = tmp0;
		alu.dst.chan = 3;
		alu.dst.write = 1;

		if (signed_op) {
			alu.src[0].sel = tmp2;
			alu.src[0].chan = 0;
		} else {
			r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
		}

		alu.src[1].sel = tmp0;
		alu.src[1].chan = 1;

		alu.last = 1;
		if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
			return r;

		/* 13. tmp1.x = tmp0.w >= src2		= r >= src2 */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_SETGE_UINT;

		alu.dst.sel = tmp1;
		alu.dst.chan = 0;
		alu.dst.write = 1;

		alu.src[0].sel = tmp0;
		alu.src[0].chan = 3;
		if (signed_op) {
			alu.src[1].sel = tmp2;
			alu.src[1].chan = 1;
		} else {
			r600_bytecode_src(&alu.src[1], &ctx->src[1], i);
		}

		alu.last = 1;
		if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
			return r;

		/* 14. tmp1.y = src1 >= tmp0.y       = r >= 0 */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_SETGE_UINT;

		alu.dst.sel = tmp1;
		alu.dst.chan = 1;
		alu.dst.write = 1;

		if (signed_op) {
			alu.src[0].sel = tmp2;
			alu.src[0].chan = 0;
		} else {
			r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
		}

		alu.src[1].sel = tmp0;
		alu.src[1].chan = 1;

		alu.last = 1;
		if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
			return r;

		if (mod) { /* UMOD */

			/* 15. tmp1.z = tmp0.w - src2			= r - src2 */
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP2_SUB_INT;

			alu.dst.sel = tmp1;
			alu.dst.chan = 2;
			alu.dst.write = 1;

			alu.src[0].sel = tmp0;
			alu.src[0].chan = 3;

			if (signed_op) {
				alu.src[1].sel = tmp2;
				alu.src[1].chan = 1;
			} else {
				r600_bytecode_src(&alu.src[1], &ctx->src[1], i);
			}

			alu.last = 1;
			if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
				return r;

			/* 16. tmp1.w = tmp0.w + src2			= r + src2 */
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP2_ADD_INT;

			alu.dst.sel = tmp1;
			alu.dst.chan = 3;
			alu.dst.write = 1;

			alu.src[0].sel = tmp0;
			alu.src[0].chan = 3;
			if (signed_op) {
				alu.src[1].sel = tmp2;
				alu.src[1].chan = 1;
			} else {
				r600_bytecode_src(&alu.src[1], &ctx->src[1], i);
			}

			alu.last = 1;
			if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
				return r;

		} else { /* UDIV */

			/* 15. tmp1.z = tmp0.z + 1       = q + 1       DIV */
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP2_ADD_INT;

			alu.dst.sel = tmp1;
			alu.dst.chan = 2;
			alu.dst.write = 1;

			alu.src[0].sel = tmp0;
			alu.src[0].chan = 2;
			alu.src[1].sel = V_SQ_ALU_SRC_1_INT;

			alu.last = 1;
			if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
				return r;

			/* 16. tmp1.w = tmp0.z - 1			= q - 1 */
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP2_ADD_INT;

			alu.dst.sel = tmp1;
			alu.dst.chan = 3;
			alu.dst.write = 1;

			alu.src[0].sel = tmp0;
			alu.src[0].chan = 2;
			alu.src[1].sel = V_SQ_ALU_SRC_M_1_INT;

			alu.last = 1;
			if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
				return r;

		}

		/* 17. tmp1.x = tmp1.x & tmp1.y */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_AND_INT;

		alu.dst.sel = tmp1;
		alu.dst.chan = 0;
		alu.dst.write = 1;

		alu.src[0].sel = tmp1;
		alu.src[0].chan = 0;
		alu.src[1].sel = tmp1;
		alu.src[1].chan = 1;

		alu.last = 1;
		if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
			return r;

		/* 18. tmp0.z = tmp1.x==0 ? tmp0.z : tmp1.z    DIV */
		/* 18. tmp0.z = tmp1.x==0 ? tmp0.w : tmp1.z    MOD */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_CNDE_INT;
		alu.is_op3 = 1;

		alu.dst.sel = tmp0;
		alu.dst.chan = 2;
		alu.dst.write = 1;

		alu.src[0].sel = tmp1;
		alu.src[0].chan = 0;
		alu.src[1].sel = tmp0;
		alu.src[1].chan = mod ? 3 : 2;
		alu.src[2].sel = tmp1;
		alu.src[2].chan = 2;

		alu.last = 1;
		if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
			return r;

		/* 19. tmp0.z = tmp1.y==0 ? tmp1.w : tmp0.z */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_CNDE_INT;
		alu.is_op3 = 1;

		if (signed_op) {
			alu.dst.sel = tmp0;
			alu.dst.chan = 2;
			alu.dst.write = 1;
		} else {
			if (tmp4 > 0) {
				alu.dst.sel = tmp4;
				alu.dst.chan = i;
				alu.dst.write = 1;
			} else {
				tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
			}
		}

		alu.src[0].sel = tmp1;
		alu.src[0].chan = 1;
		alu.src[1].sel = tmp1;
		alu.src[1].chan = 3;
		alu.src[2].sel = tmp0;
		alu.src[2].chan = 2;

		alu.last = 1;
		if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
			return r;

		if (signed_op) {

			/* fix the sign of the result */

			if (mod) {

				/* tmp0.x = -tmp0.z */
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP2_SUB_INT;

				alu.dst.sel = tmp0;
				alu.dst.chan = 0;
				alu.dst.write = 1;

				alu.src[0].sel = V_SQ_ALU_SRC_0;
				alu.src[1].sel = tmp0;
				alu.src[1].chan = 2;

				alu.last = 1;
				if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
					return r;

				/* sign of the remainder is the same as the sign of src0 */
				/* tmp0.x = src0>=0 ? tmp0.z : tmp0.x */
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP3_CNDGE_INT;
				alu.is_op3 = 1;

				if (tmp4 > 0) {
					alu.dst.sel = tmp4;
					alu.dst.chan = i;
					alu.dst.write = 1;
				} else {
					tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
				}

				r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
				alu.src[1].sel = tmp0;
				alu.src[1].chan = 2;
				alu.src[2].sel = tmp0;
				alu.src[2].chan = 0;

				alu.last = 1;
				if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
					return r;

			} else {

				/* tmp0.x = -tmp0.z */
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP2_SUB_INT;

				alu.dst.sel = tmp0;
				alu.dst.chan = 0;
				alu.dst.write = 1;

				alu.src[0].sel = V_SQ_ALU_SRC_0;
				alu.src[1].sel = tmp0;
				alu.src[1].chan = 2;

				alu.last = 1;
				if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
					return r;

				/* fix the quotient sign (same as the sign of src0*src1) */
				/* tmp0.x = tmp2.z>=0 ? tmp0.z : tmp0.x */
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP3_CNDGE_INT;
				alu.is_op3 = 1;

				if (tmp4 > 0) {
					alu.dst.sel = tmp4;
					alu.dst.chan = i;
					alu.dst.write = 1;
				} else {
					tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
				}

				alu.src[0].sel = tmp2;
				alu.src[0].chan = 2;
				alu.src[1].sel = tmp0;
				alu.src[1].chan = 2;
				alu.src[2].sel = tmp0;
				alu.src[2].chan = 0;

				alu.last = 1;
				if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
					return r;
			}
		}
	}

	if (tmp4 > 0) {
		for (i = 0; i <= lasti; ++i) {
			if (!(write_mask & (1<<i)))
				continue;

			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_MOV;
			tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
			alu.src[0].sel = tmp4;
			alu.src[0].chan = i;

			if (i == lasti)
				alu.last = 1;
			if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
				return r;
		}
	}

	return 0;
}

static int tgsi_udiv(struct r600_shader_ctx *ctx)
{
	return tgsi_divmod(ctx, 0, 0);
}

static int tgsi_umod(struct r600_shader_ctx *ctx)
{
	return tgsi_divmod(ctx, 1, 0);
}

static int tgsi_idiv(struct r600_shader_ctx *ctx)
{
	return tgsi_divmod(ctx, 0, 1);
}

static int tgsi_imod(struct r600_shader_ctx *ctx)
{
	return tgsi_divmod(ctx, 1, 1);
}


static int tgsi_f2i(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r;
	unsigned write_mask = inst->Dst[0].Register.WriteMask;
	int last_inst = tgsi_last_instruction(write_mask);

	for (i = 0; i < 4; i++) {
		if (!(write_mask & (1<<i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_TRUNC;

		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = i;
		alu.dst.write = 1;

		r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
		if (i == last_inst)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	for (i = 0; i < 4; i++) {
		if (!(write_mask & (1<<i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ctx->inst_info->op;

		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);

		alu.src[0].sel = ctx->temp_reg;
		alu.src[0].chan = i;

		if (i == last_inst || alu.op == ALU_OP1_FLT_TO_UINT)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	return 0;
}

static int tgsi_iabs(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r;
	unsigned write_mask = inst->Dst[0].Register.WriteMask;
	int last_inst = tgsi_last_instruction(write_mask);

	/* tmp = -src */
	for (i = 0; i < 4; i++) {
		if (!(write_mask & (1<<i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_SUB_INT;

		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = i;
		alu.dst.write = 1;

		r600_bytecode_src(&alu.src[1], &ctx->src[0], i);
		alu.src[0].sel = V_SQ_ALU_SRC_0;

		if (i == last_inst)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	/* dst = (src >= 0 ? src : tmp) */
	for (i = 0; i < 4; i++) {
		if (!(write_mask & (1<<i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_CNDGE_INT;
		alu.is_op3 = 1;
		alu.dst.write = 1;

		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);

		r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
		r600_bytecode_src(&alu.src[1], &ctx->src[0], i);
		alu.src[2].sel = ctx->temp_reg;
		alu.src[2].chan = i;

		if (i == last_inst)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_issg(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r;
	unsigned write_mask = inst->Dst[0].Register.WriteMask;
	int last_inst = tgsi_last_instruction(write_mask);

	/* tmp = (src >= 0 ? src : -1) */
	for (i = 0; i < 4; i++) {
		if (!(write_mask & (1<<i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_CNDGE_INT;
		alu.is_op3 = 1;

		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = i;
		alu.dst.write = 1;

		r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
		r600_bytecode_src(&alu.src[1], &ctx->src[0], i);
		alu.src[2].sel = V_SQ_ALU_SRC_M_1_INT;

		if (i == last_inst)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	/* dst = (tmp > 0 ? 1 : tmp) */
	for (i = 0; i < 4; i++) {
		if (!(write_mask & (1<<i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_CNDGT_INT;
		alu.is_op3 = 1;
		alu.dst.write = 1;

		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);

		alu.src[0].sel = ctx->temp_reg;
		alu.src[0].chan = i;

		alu.src[1].sel = V_SQ_ALU_SRC_1_INT;

		alu.src[2].sel = ctx->temp_reg;
		alu.src[2].chan = i;

		if (i == last_inst)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}



static int tgsi_ssg(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	unsigned write_mask = inst->Dst[0].Register.WriteMask;
	int last_inst = tgsi_last_instruction(write_mask);
	struct r600_bytecode_alu alu;
	int i, r;

	/* tmp = (src > 0 ? 1 : src) */
	for (i = 0; i <= last_inst; i++) {
		if (!(write_mask & (1 << i)))
			continue;
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_CNDGT;
		alu.is_op3 = 1;

		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = i;

		r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
		alu.src[1].sel = V_SQ_ALU_SRC_1;
		r600_bytecode_src(&alu.src[2], &ctx->src[0], i);

		if (i == last_inst)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	/* dst = (-tmp > 0 ? -1 : tmp) */
	for (i = 0; i <= last_inst; i++) {
		if (!(write_mask & (1 << i)))
			continue;
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_CNDGT;
		alu.is_op3 = 1;
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);

		alu.src[0].sel = ctx->temp_reg;
		alu.src[0].chan = i;
		alu.src[0].neg = 1;

		alu.src[1].sel = V_SQ_ALU_SRC_1;
		alu.src[1].neg = 1;

		alu.src[2].sel = ctx->temp_reg;
		alu.src[2].chan = i;

		if (i == last_inst)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_bfi(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r, t1, t2;

	unsigned write_mask = inst->Dst[0].Register.WriteMask;
	int last_inst = tgsi_last_instruction(write_mask);

	t1 = r600_get_temp(ctx);

	for (i = 0; i < 4; i++) {
		if (!(write_mask & (1<<i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_SETGE_INT;
		r600_bytecode_src(&alu.src[0], &ctx->src[3], i);
		alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
		alu.src[1].value = 32;
		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = i;
		alu.dst.write = 1;
		alu.last = i == last_inst;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	for (i = 0; i < 4; i++) {
		if (!(write_mask & (1<<i)))
			continue;

		/* create mask tmp */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_BFM_INT;
		alu.dst.sel = t1;
		alu.dst.chan = i;
		alu.dst.write = 1;
		alu.last = i == last_inst;

		r600_bytecode_src(&alu.src[0], &ctx->src[3], i);
		r600_bytecode_src(&alu.src[1], &ctx->src[2], i);

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	t2 = r600_get_temp(ctx);

	for (i = 0; i < 4; i++) {
		if (!(write_mask & (1<<i)))
			continue;

		/* shift insert left */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_LSHL_INT;
		alu.dst.sel = t2;
		alu.dst.chan = i;
		alu.dst.write = 1;
		alu.last = i == last_inst;

		r600_bytecode_src(&alu.src[0], &ctx->src[1], i);
		r600_bytecode_src(&alu.src[1], &ctx->src[2], i);

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	for (i = 0; i < 4; i++) {
		if (!(write_mask & (1<<i)))
			continue;

		/* actual bitfield insert */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_BFI_INT;
		alu.is_op3 = 1;
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.dst.chan = i;
		alu.dst.write = 1;
		alu.last = i == last_inst;

		alu.src[0].sel = t1;
		alu.src[0].chan = i;
		alu.src[1].sel = t2;
		alu.src[1].chan = i;
		r600_bytecode_src(&alu.src[2], &ctx->src[0], i);

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	for (i = 0; i < 4; i++) {
		if (!(write_mask & (1<<i)))
			continue;
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_CNDE_INT;
		alu.is_op3 = 1;
		alu.src[0].sel = ctx->temp_reg;
		alu.src[0].chan = i;
		r600_bytecode_src(&alu.src[2], &ctx->src[1], i);

		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);

		alu.src[1].sel = alu.dst.sel;
		alu.src[1].chan = i;

		alu.last = i == last_inst;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_msb(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r, t1, t2;

	unsigned write_mask = inst->Dst[0].Register.WriteMask;
	int last_inst = tgsi_last_instruction(write_mask);

	assert(ctx->inst_info->op == ALU_OP1_FFBH_INT ||
		ctx->inst_info->op == ALU_OP1_FFBH_UINT);

	t1 = ctx->temp_reg;

	/* bit position is indexed from lsb by TGSI, and from msb by the hardware */
	for (i = 0; i < 4; i++) {
		if (!(write_mask & (1<<i)))
			continue;

		/* t1 = FFBH_INT / FFBH_UINT */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ctx->inst_info->op;
		alu.dst.sel = t1;
		alu.dst.chan = i;
		alu.dst.write = 1;
		alu.last = i == last_inst;

		r600_bytecode_src(&alu.src[0], &ctx->src[0], i);

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	t2 = r600_get_temp(ctx);

	for (i = 0; i < 4; i++) {
		if (!(write_mask & (1<<i)))
			continue;

		/* t2 = 31 - t1 */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_SUB_INT;
		alu.dst.sel = t2;
		alu.dst.chan = i;
		alu.dst.write = 1;
		alu.last = i == last_inst;

		alu.src[0].sel = V_SQ_ALU_SRC_LITERAL;
		alu.src[0].value = 31;
		alu.src[1].sel = t1;
		alu.src[1].chan = i;

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	for (i = 0; i < 4; i++) {
		if (!(write_mask & (1<<i)))
			continue;

		/* result = t1 >= 0 ? t2 : t1 */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_CNDGE_INT;
		alu.is_op3 = 1;
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.dst.chan = i;
		alu.dst.write = 1;
		alu.last = i == last_inst;

		alu.src[0].sel = t1;
		alu.src[0].chan = i;
		alu.src[1].sel = t2;
		alu.src[1].chan = i;
		alu.src[2].sel = t1;
		alu.src[2].chan = i;

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	return 0;
}

static int tgsi_interp_egcm(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int r, i = 0, k, interp_gpr, interp_base_chan, tmp, lasti;
	unsigned location;
	const int input = inst->Src[0].Register.Index + ctx->shader->nsys_inputs;

	assert(inst->Src[0].Register.File == TGSI_FILE_INPUT);

	/* Interpolators have been marked for use already by allocate_system_value_inputs */
	if (inst->Instruction.Opcode == TGSI_OPCODE_INTERP_OFFSET ||
		inst->Instruction.Opcode == TGSI_OPCODE_INTERP_SAMPLE) {
		location = TGSI_INTERPOLATE_LOC_CENTER; /* sample offset will be added explicitly */
	}
	else {
		location = TGSI_INTERPOLATE_LOC_CENTROID;
		ctx->shader->input[input].uses_interpolate_at_centroid = 1;
	}

	k = eg_get_interpolator_index(ctx->shader->input[input].interpolate, location);
	if (k < 0)
		k = 0;
	interp_gpr = ctx->eg_interpolators[k].ij_index / 2;
	interp_base_chan = 2 * (ctx->eg_interpolators[k].ij_index % 2);

	/* NOTE: currently offset is not perspective correct */
	if (inst->Instruction.Opcode == TGSI_OPCODE_INTERP_OFFSET ||
		inst->Instruction.Opcode == TGSI_OPCODE_INTERP_SAMPLE) {
		int sample_gpr = -1;
		int gradientsH, gradientsV;
		struct r600_bytecode_tex tex;

		if (inst->Instruction.Opcode == TGSI_OPCODE_INTERP_SAMPLE) {
			sample_gpr = load_sample_position(ctx, &ctx->src[1], ctx->src[1].swizzle[0]);
		}

		gradientsH = r600_get_temp(ctx);
		gradientsV = r600_get_temp(ctx);
		for (i = 0; i < 2; i++) {
			memset(&tex, 0, sizeof(struct r600_bytecode_tex));
			tex.op = i == 0 ? FETCH_OP_GET_GRADIENTS_H : FETCH_OP_GET_GRADIENTS_V;
			tex.src_gpr = interp_gpr;
			tex.src_sel_x = interp_base_chan + 0;
			tex.src_sel_y = interp_base_chan + 1;
			tex.src_sel_z = 0;
			tex.src_sel_w = 0;
			tex.dst_gpr = i == 0 ? gradientsH : gradientsV;
			tex.dst_sel_x = 0;
			tex.dst_sel_y = 1;
			tex.dst_sel_z = 7;
			tex.dst_sel_w = 7;
			tex.inst_mod = 1; // Use per pixel gradient calculation
			tex.sampler_id = 0;
			tex.resource_id = tex.sampler_id;
			r = r600_bytecode_add_tex(ctx->bc, &tex);
			if (r)
				return r;
		}

		for (i = 0; i < 2; i++) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP3_MULADD;
			alu.is_op3 = 1;
			alu.src[0].sel = gradientsH;
			alu.src[0].chan = i;
			if (inst->Instruction.Opcode == TGSI_OPCODE_INTERP_SAMPLE) {
				alu.src[1].sel = sample_gpr;
				alu.src[1].chan = 2;
			}
			else {
				r600_bytecode_src(&alu.src[1], &ctx->src[1], 0);
			}
			alu.src[2].sel = interp_gpr;
			alu.src[2].chan = interp_base_chan + i;
			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = i;
			alu.last = i == 1;

			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}

		for (i = 0; i < 2; i++) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP3_MULADD;
			alu.is_op3 = 1;
			alu.src[0].sel = gradientsV;
			alu.src[0].chan = i;
			if (inst->Instruction.Opcode == TGSI_OPCODE_INTERP_SAMPLE) {
				alu.src[1].sel = sample_gpr;
				alu.src[1].chan = 3;
			}
			else {
				r600_bytecode_src(&alu.src[1], &ctx->src[1], 1);
			}
			alu.src[2].sel = ctx->temp_reg;
			alu.src[2].chan = i;
			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = i;
			alu.last = i == 1;

			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
	}

	tmp = r600_get_temp(ctx);
	for (i = 0; i < 8; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = i < 4 ? ALU_OP2_INTERP_ZW : ALU_OP2_INTERP_XY;

		alu.dst.sel = tmp;
		if ((i > 1 && i < 6)) {
			alu.dst.write = 1;
		}
		else {
			alu.dst.write = 0;
		}
		alu.dst.chan = i % 4;

		if (inst->Instruction.Opcode == TGSI_OPCODE_INTERP_OFFSET ||
			inst->Instruction.Opcode == TGSI_OPCODE_INTERP_SAMPLE) {
			alu.src[0].sel = ctx->temp_reg;
			alu.src[0].chan = 1 - (i % 2);
		} else {
			alu.src[0].sel = interp_gpr;
			alu.src[0].chan = interp_base_chan + 1 - (i % 2);
		}
		alu.src[1].sel = V_SQ_ALU_SRC_PARAM_BASE + ctx->shader->input[input].lds_pos;
		alu.src[1].chan = 0;

		alu.last = i % 4 == 3;
		alu.bank_swizzle_force = SQ_ALU_VEC_210;

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	// INTERP can't swizzle dst
	lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);
	for (i = 0; i <= lasti; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.src[0].sel = tmp;
		alu.src[0].chan = ctx->src[0].swizzle[i];
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.dst.write = 1;
		alu.last = i == lasti;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	return 0;
}


static int tgsi_helper_copy(struct r600_shader_ctx *ctx, struct tgsi_full_instruction *inst)
{
	struct r600_bytecode_alu alu;
	int i, r;

	for (i = 0; i < 4; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		if (!(inst->Dst[0].Register.WriteMask & (1 << i))) {
			alu.op = ALU_OP0_NOP;
			alu.dst.chan = i;
		} else {
			alu.op = ALU_OP1_MOV;
			tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
			alu.src[0].sel = ctx->temp_reg;
			alu.src[0].chan = i;
		}
		if (i == 3) {
			alu.last = 1;
		}
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_make_src_for_op3(struct r600_shader_ctx *ctx,
                                 unsigned writemask,
                                 struct r600_bytecode_alu_src *bc_src,
                                 const struct r600_shader_src *shader_src)
{
	struct r600_bytecode_alu alu;
	int i, r;
	int lasti = tgsi_last_instruction(writemask);
	int temp_reg = 0;

	r600_bytecode_src(&bc_src[0], shader_src, 0);
	r600_bytecode_src(&bc_src[1], shader_src, 1);
	r600_bytecode_src(&bc_src[2], shader_src, 2);
	r600_bytecode_src(&bc_src[3], shader_src, 3);

	if (bc_src->abs) {
		temp_reg = r600_get_temp(ctx);

		for (i = 0; i < lasti + 1; i++) {
			if (!(writemask & (1 << i)))
				continue;
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_MOV;
			alu.dst.sel = temp_reg;
			alu.dst.chan = i;
			alu.dst.write = 1;
			alu.src[0] = bc_src[i];
			if (i == lasti) {
				alu.last = 1;
			}
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
			memset(&bc_src[i], 0, sizeof(*bc_src));
			bc_src[i].sel = temp_reg;
			bc_src[i].chan = i;
		}
	}
	return 0;
}

static int tgsi_op3_dst(struct r600_shader_ctx *ctx, int dst)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	struct r600_bytecode_alu_src srcs[4][4];
	int i, j, r;
	int lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);
	unsigned op = ctx->inst_info->op;

	if (op == ALU_OP3_MULADD_IEEE &&
	    ctx->info.properties[TGSI_PROPERTY_MUL_ZERO_WINS])
		op = ALU_OP3_MULADD;

	for (j = 0; j < inst->Instruction.NumSrcRegs; j++) {
		r = tgsi_make_src_for_op3(ctx, inst->Dst[0].Register.WriteMask,
					  srcs[j], &ctx->src[j]);
		if (r)
			return r;
	}

	for (i = 0; i < lasti + 1; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = op;
		for (j = 0; j < inst->Instruction.NumSrcRegs; j++) {
			alu.src[j] = srcs[j][i];
		}

		if (dst == -1) {
			tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		} else {
			alu.dst.sel = dst;
		}
		alu.dst.chan = i;
		alu.dst.write = 1;
		alu.is_op3 = 1;
		if (i == lasti) {
			alu.last = 1;
		}
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_op3(struct r600_shader_ctx *ctx)
{
	return tgsi_op3_dst(ctx, -1);
}

static int tgsi_dp(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, j, r;
	unsigned op = ctx->inst_info->op;
	if (op == ALU_OP2_DOT4_IEEE &&
	    ctx->info.properties[TGSI_PROPERTY_MUL_ZERO_WINS])
		op = ALU_OP2_DOT4;

	for (i = 0; i < 4; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = op;
		for (j = 0; j < inst->Instruction.NumSrcRegs; j++) {
			r600_bytecode_src(&alu.src[j], &ctx->src[j], i);
		}

		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.dst.chan = i;
		alu.dst.write = (inst->Dst[0].Register.WriteMask >> i) & 1;
		/* handle some special cases */
		switch (inst->Instruction.Opcode) {
		case TGSI_OPCODE_DP2:
			if (i > 1) {
				alu.src[0].sel = alu.src[1].sel = V_SQ_ALU_SRC_0;
				alu.src[0].chan = alu.src[1].chan = 0;
			}
			break;
		case TGSI_OPCODE_DP3:
			if (i > 2) {
				alu.src[0].sel = alu.src[1].sel = V_SQ_ALU_SRC_0;
				alu.src[0].chan = alu.src[1].chan = 0;
			}
			break;
		default:
			break;
		}
		if (i == 3) {
			alu.last = 1;
		}
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static inline boolean tgsi_tex_src_requires_loading(struct r600_shader_ctx *ctx,
						    unsigned index)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	return 	(inst->Src[index].Register.File != TGSI_FILE_TEMPORARY &&
		inst->Src[index].Register.File != TGSI_FILE_INPUT &&
		inst->Src[index].Register.File != TGSI_FILE_OUTPUT) ||
		ctx->src[index].neg || ctx->src[index].abs ||
		(inst->Src[index].Register.File == TGSI_FILE_INPUT && ctx->type == PIPE_SHADER_GEOMETRY);
}

static inline unsigned tgsi_tex_get_src_gpr(struct r600_shader_ctx *ctx,
					unsigned index)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	return ctx->file_offset[inst->Src[index].Register.File] + inst->Src[index].Register.Index;
}

static int do_vtx_fetch_inst(struct r600_shader_ctx *ctx, boolean src_requires_loading)
{
	struct r600_bytecode_vtx vtx;
	struct r600_bytecode_alu alu;
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	int src_gpr, r, i;
	int id = tgsi_tex_get_src_gpr(ctx, 1);
	int sampler_index_mode = inst->Src[1].Indirect.Index == 2 ? 2 : 0; // CF_INDEX_1 : CF_INDEX_NONE

	src_gpr = tgsi_tex_get_src_gpr(ctx, 0);
	if (src_requires_loading) {
		for (i = 0; i < 4; i++) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_MOV;
			r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = i;
			if (i == 3)
				alu.last = 1;
			alu.dst.write = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
		src_gpr = ctx->temp_reg;
	}

	memset(&vtx, 0, sizeof(vtx));
	vtx.op = FETCH_OP_VFETCH;
	vtx.buffer_id = id + R600_MAX_CONST_BUFFERS;
	vtx.fetch_type = SQ_VTX_FETCH_NO_INDEX_OFFSET;
	vtx.src_gpr = src_gpr;
	vtx.mega_fetch_count = 16;
	vtx.dst_gpr = ctx->file_offset[inst->Dst[0].Register.File] + inst->Dst[0].Register.Index;
	vtx.dst_sel_x = (inst->Dst[0].Register.WriteMask & 1) ? 0 : 7;		/* SEL_X */
	vtx.dst_sel_y = (inst->Dst[0].Register.WriteMask & 2) ? 1 : 7;		/* SEL_Y */
	vtx.dst_sel_z = (inst->Dst[0].Register.WriteMask & 4) ? 2 : 7;		/* SEL_Z */
	vtx.dst_sel_w = (inst->Dst[0].Register.WriteMask & 8) ? 3 : 7;		/* SEL_W */
	vtx.use_const_fields = 1;
	vtx.buffer_index_mode = sampler_index_mode;

	if ((r = r600_bytecode_add_vtx(ctx->bc, &vtx)))
		return r;

	if (ctx->bc->chip_class >= EVERGREEN)
		return 0;

	for (i = 0; i < 4; i++) {
		int lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_AND_INT;

		alu.dst.chan = i;
		alu.dst.sel = vtx.dst_gpr;
		alu.dst.write = 1;

		alu.src[0].sel = vtx.dst_gpr;
		alu.src[0].chan = i;

		alu.src[1].sel = R600_SHADER_BUFFER_INFO_SEL;
		alu.src[1].sel += (id * 2);
		alu.src[1].chan = i % 4;
		alu.src[1].kc_bank = R600_BUFFER_INFO_CONST_BUFFER;

		if (i == lasti)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	if (inst->Dst[0].Register.WriteMask & 3) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_OR_INT;

		alu.dst.chan = 3;
		alu.dst.sel = vtx.dst_gpr;
		alu.dst.write = 1;

		alu.src[0].sel = vtx.dst_gpr;
		alu.src[0].chan = 3;

		alu.src[1].sel = R600_SHADER_BUFFER_INFO_SEL + (id * 2) + 1;
		alu.src[1].chan = 0;
		alu.src[1].kc_bank = R600_BUFFER_INFO_CONST_BUFFER;

		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int r600_do_buffer_txq(struct r600_shader_ctx *ctx, int reg_idx, int offset, int eg_buffer_base)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	int r;
	int id = tgsi_tex_get_src_gpr(ctx, reg_idx) + offset;
	int sampler_index_mode = inst->Src[reg_idx].Indirect.Index == 2 ? 2 : 0; // CF_INDEX_1 : CF_INDEX_NONE

	if (ctx->bc->chip_class < EVERGREEN) {
		struct r600_bytecode_alu alu;
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.src[0].sel = R600_SHADER_BUFFER_INFO_SEL;
		/* r600 we have them at channel 2 of the second dword */
		alu.src[0].sel += (id * 2) + 1;
		alu.src[0].chan = 1;
		alu.src[0].kc_bank = R600_BUFFER_INFO_CONST_BUFFER;
		tgsi_dst(ctx, &inst->Dst[0], 0, &alu.dst);
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
		return 0;
	} else {
		struct r600_bytecode_vtx vtx;
		memset(&vtx, 0, sizeof(vtx));
		vtx.op = FETCH_OP_GET_BUFFER_RESINFO;
		vtx.buffer_id = id + eg_buffer_base;
		vtx.fetch_type = SQ_VTX_FETCH_NO_INDEX_OFFSET;
		vtx.src_gpr = 0;
		vtx.mega_fetch_count = 16; /* no idea here really... */
		vtx.dst_gpr = ctx->file_offset[inst->Dst[0].Register.File] + inst->Dst[0].Register.Index;
		vtx.dst_sel_x = (inst->Dst[0].Register.WriteMask & 1) ? 0 : 7;		/* SEL_X */
		vtx.dst_sel_y = (inst->Dst[0].Register.WriteMask & 2) ? 4 : 7;		/* SEL_Y */
		vtx.dst_sel_z = (inst->Dst[0].Register.WriteMask & 4) ? 4 : 7;		/* SEL_Z */
		vtx.dst_sel_w = (inst->Dst[0].Register.WriteMask & 8) ? 4 : 7;		/* SEL_W */
		vtx.data_format = FMT_32_32_32_32;
		vtx.buffer_index_mode = sampler_index_mode;

		if ((r = r600_bytecode_add_vtx_tc(ctx->bc, &vtx)))
			return r;
		return 0;
	}
}


static int tgsi_tex(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_tex tex;
	struct r600_bytecode_tex grad_offs[3];
	struct r600_bytecode_alu alu;
	unsigned src_gpr;
	int r, i, j, n_grad_offs = 0;
	int opcode;
	bool read_compressed_msaa = ctx->bc->has_compressed_msaa_texturing &&
				    inst->Instruction.Opcode == TGSI_OPCODE_TXF &&
				    (inst->Texture.Texture == TGSI_TEXTURE_2D_MSAA ||
				     inst->Texture.Texture == TGSI_TEXTURE_2D_ARRAY_MSAA);

	bool txf_add_offsets = inst->Texture.NumOffsets &&
			     inst->Instruction.Opcode == TGSI_OPCODE_TXF &&
			     inst->Texture.Texture != TGSI_TEXTURE_BUFFER;

	/* Texture fetch instructions can only use gprs as source.
	 * Also they cannot negate the source or take the absolute value */
	const boolean src_requires_loading = (inst->Instruction.Opcode != TGSI_OPCODE_TXQS &&
                                              tgsi_tex_src_requires_loading(ctx, 0)) ||
					     read_compressed_msaa || txf_add_offsets;

	boolean src_loaded = FALSE;
	unsigned sampler_src_reg = 1;
	int8_t offset_x = 0, offset_y = 0, offset_z = 0;
	boolean has_txq_cube_array_z = false;
	unsigned sampler_index_mode;
	int array_index_offset_channel = -1;

	if (inst->Instruction.Opcode == TGSI_OPCODE_TXQ &&
	    ((inst->Texture.Texture == TGSI_TEXTURE_CUBE_ARRAY ||
	      inst->Texture.Texture == TGSI_TEXTURE_SHADOWCUBE_ARRAY)))
		if (inst->Dst[0].Register.WriteMask & 4) {
			ctx->shader->has_txq_cube_array_z_comp = true;
			has_txq_cube_array_z = true;
		}

	if (inst->Instruction.Opcode == TGSI_OPCODE_TEX2 ||
	    inst->Instruction.Opcode == TGSI_OPCODE_TXB2 ||
	    inst->Instruction.Opcode == TGSI_OPCODE_TXL2 ||
	    inst->Instruction.Opcode == TGSI_OPCODE_TG4)
		sampler_src_reg = 2;

	/* TGSI moves the sampler to src reg 3 for TXD */
	if (inst->Instruction.Opcode == TGSI_OPCODE_TXD)
		sampler_src_reg = 3;

	sampler_index_mode = inst->Src[sampler_src_reg].Indirect.Index == 2 ? 2 : 0; // CF_INDEX_1 : CF_INDEX_NONE

	src_gpr = tgsi_tex_get_src_gpr(ctx, 0);

	if (inst->Texture.Texture == TGSI_TEXTURE_BUFFER) {
		if (inst->Instruction.Opcode == TGSI_OPCODE_TXQ) {
			if (ctx->bc->chip_class < EVERGREEN)
				ctx->shader->uses_tex_buffers = true;
			return r600_do_buffer_txq(ctx, 1, 0, R600_MAX_CONST_BUFFERS);
		}
		else if (inst->Instruction.Opcode == TGSI_OPCODE_TXF) {
			if (ctx->bc->chip_class < EVERGREEN)
				ctx->shader->uses_tex_buffers = true;
			return do_vtx_fetch_inst(ctx, src_requires_loading);
		}
	}

	if (inst->Instruction.Opcode == TGSI_OPCODE_TXP) {
		int out_chan;
		/* Add perspective divide */
		if (ctx->bc->chip_class == CAYMAN) {
			out_chan = 2;
			for (i = 0; i < 3; i++) {
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_RECIP_IEEE;
				r600_bytecode_src(&alu.src[0], &ctx->src[0], 3);

				alu.dst.sel = ctx->temp_reg;
				alu.dst.chan = i;
				if (i == 2)
					alu.last = 1;
				if (out_chan == i)
					alu.dst.write = 1;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}

		} else {
			out_chan = 3;
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_RECIP_IEEE;
			r600_bytecode_src(&alu.src[0], &ctx->src[0], 3);

			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = out_chan;
			alu.last = 1;
			alu.dst.write = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}

		for (i = 0; i < 3; i++) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP2_MUL;
			alu.src[0].sel = ctx->temp_reg;
			alu.src[0].chan = out_chan;
			r600_bytecode_src(&alu.src[1], &ctx->src[0], i);
			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = i;
			alu.dst.write = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.src[0].sel = V_SQ_ALU_SRC_1;
		alu.src[0].chan = 0;
		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = 3;
		alu.last = 1;
		alu.dst.write = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
		src_loaded = TRUE;
		src_gpr = ctx->temp_reg;
	}


	if ((inst->Texture.Texture == TGSI_TEXTURE_CUBE ||
	     inst->Texture.Texture == TGSI_TEXTURE_CUBE_ARRAY ||
	     inst->Texture.Texture == TGSI_TEXTURE_SHADOWCUBE ||
	     inst->Texture.Texture == TGSI_TEXTURE_SHADOWCUBE_ARRAY) &&
	    inst->Instruction.Opcode != TGSI_OPCODE_TXQ) {

		static const unsigned src0_swizzle[] = {2, 2, 0, 1};
		static const unsigned src1_swizzle[] = {1, 0, 2, 2};

		/* tmp1.xyzw = CUBE(R0.zzxy, R0.yxzz) */
		for (i = 0; i < 4; i++) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP2_CUBE;
			r600_bytecode_src(&alu.src[0], &ctx->src[0], src0_swizzle[i]);
			r600_bytecode_src(&alu.src[1], &ctx->src[0], src1_swizzle[i]);
			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = i;
			if (i == 3)
				alu.last = 1;
			alu.dst.write = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}

		/* tmp1.z = RCP_e(|tmp1.z|) */
		if (ctx->bc->chip_class == CAYMAN) {
			for (i = 0; i < 3; i++) {
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_RECIP_IEEE;
				alu.src[0].sel = ctx->temp_reg;
				alu.src[0].chan = 2;
				alu.src[0].abs = 1;
				alu.dst.sel = ctx->temp_reg;
				alu.dst.chan = i;
				if (i == 2)
					alu.dst.write = 1;
				if (i == 2)
					alu.last = 1;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}
		} else {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_RECIP_IEEE;
			alu.src[0].sel = ctx->temp_reg;
			alu.src[0].chan = 2;
			alu.src[0].abs = 1;
			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = 2;
			alu.dst.write = 1;
			alu.last = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}

		/* MULADD R0.x,  R0.x,  PS1,  (0x3FC00000, 1.5f).x
		 * MULADD R0.y,  R0.y,  PS1,  (0x3FC00000, 1.5f).x
		 * muladd has no writemask, have to use another temp
		 */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_MULADD;
		alu.is_op3 = 1;

		alu.src[0].sel = ctx->temp_reg;
		alu.src[0].chan = 0;
		alu.src[1].sel = ctx->temp_reg;
		alu.src[1].chan = 2;

		alu.src[2].sel = V_SQ_ALU_SRC_LITERAL;
		alu.src[2].chan = 0;
		alu.src[2].value = u_bitcast_f2u(1.5f);

		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = 0;
		alu.dst.write = 1;

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_MULADD;
		alu.is_op3 = 1;

		alu.src[0].sel = ctx->temp_reg;
		alu.src[0].chan = 1;
		alu.src[1].sel = ctx->temp_reg;
		alu.src[1].chan = 2;

		alu.src[2].sel = V_SQ_ALU_SRC_LITERAL;
		alu.src[2].chan = 0;
		alu.src[2].value = u_bitcast_f2u(1.5f);

		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = 1;
		alu.dst.write = 1;

		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
		/* write initial compare value into Z component
		  - W src 0 for shadow cube
		  - X src 1 for shadow cube array */
		if (inst->Texture.Texture == TGSI_TEXTURE_SHADOWCUBE ||
		    inst->Texture.Texture == TGSI_TEXTURE_SHADOWCUBE_ARRAY) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_MOV;
			if (inst->Texture.Texture == TGSI_TEXTURE_SHADOWCUBE_ARRAY)
				r600_bytecode_src(&alu.src[0], &ctx->src[1], 0);
			else
				r600_bytecode_src(&alu.src[0], &ctx->src[0], 3);
			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = 2;
			alu.dst.write = 1;
			alu.last = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}

		if (inst->Texture.Texture == TGSI_TEXTURE_CUBE_ARRAY ||
		    inst->Texture.Texture == TGSI_TEXTURE_SHADOWCUBE_ARRAY) {
			if (ctx->bc->chip_class >= EVERGREEN) {
				int mytmp = r600_get_temp(ctx);
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_MOV;
				alu.src[0].sel = ctx->temp_reg;
				alu.src[0].chan = 3;
				alu.dst.sel = mytmp;
				alu.dst.chan = 0;
				alu.dst.write = 1;
				alu.last = 1;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;

				/* Evaluate the array index according to floor(idx + 0.5). This
				 * needs to be done before merging the face select value, because
				 * otherwise the fractional part of the array index will interfere
				 * with the face select value */
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				r600_bytecode_src(&alu.src[0], &ctx->src[0], 3);
				alu.op = ALU_OP1_RNDNE;
				alu.dst.sel = ctx->temp_reg;
				alu.dst.chan = 3;
				alu.dst.write = 1;
				alu.last = 1;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;

				/* Because the array slice index and the cube face index are merged
				 * into one value we have to make sure the array slice index is >= 0,
				 * otherwise the face selection will fail */
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP2_MAX;
				alu.src[0].sel = ctx->temp_reg;
				alu.src[0].chan = 3;
				alu.src[1].sel = V_SQ_ALU_SRC_0;
				alu.dst.sel = ctx->temp_reg;
				alu.dst.chan = 3;
				alu.dst.write = 1;
				alu.last = 1;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;

				/* have to multiply original layer by 8 and add to face id (temp.w) in Z */
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP3_MULADD;
				alu.is_op3 = 1;
				alu.src[0].sel = ctx->temp_reg;
				alu.src[0].chan = 3;
				alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
				alu.src[1].chan = 0;
				alu.src[1].value = u_bitcast_f2u(8.0f);
				alu.src[2].sel = mytmp;
				alu.src[2].chan = 0;
				alu.dst.sel = ctx->temp_reg;
				alu.dst.chan = 3;
				alu.dst.write = 1;
				alu.last = 1;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			} else if (ctx->bc->chip_class < EVERGREEN) {
				memset(&tex, 0, sizeof(struct r600_bytecode_tex));
				tex.op = FETCH_OP_SET_CUBEMAP_INDEX;
				tex.sampler_id = tgsi_tex_get_src_gpr(ctx, sampler_src_reg);
				tex.resource_id = tex.sampler_id + R600_MAX_CONST_BUFFERS;
				tex.src_gpr = r600_get_temp(ctx);
				tex.src_sel_x = 0;
				tex.src_sel_y = 0;
				tex.src_sel_z = 0;
				tex.src_sel_w = 0;
				tex.dst_sel_x = tex.dst_sel_y = tex.dst_sel_z = tex.dst_sel_w = 7;
				tex.coord_type_x = 1;
				tex.coord_type_y = 1;
				tex.coord_type_z = 1;
				tex.coord_type_w = 1;
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_MOV;
				r600_bytecode_src(&alu.src[0], &ctx->src[0], 3);
				alu.dst.sel = tex.src_gpr;
				alu.dst.chan = 0;
				alu.last = 1;
				alu.dst.write = 1;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;

				r = r600_bytecode_add_tex(ctx->bc, &tex);
				if (r)
					return r;
			}

		}

		/* for cube forms of lod and bias we need to route things */
		if (inst->Instruction.Opcode == TGSI_OPCODE_TXB ||
		    inst->Instruction.Opcode == TGSI_OPCODE_TXL ||
		    inst->Instruction.Opcode == TGSI_OPCODE_TXB2 ||
		    inst->Instruction.Opcode == TGSI_OPCODE_TXL2) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_MOV;
			if (inst->Instruction.Opcode == TGSI_OPCODE_TXB2 ||
			    inst->Instruction.Opcode == TGSI_OPCODE_TXL2)
				r600_bytecode_src(&alu.src[0], &ctx->src[1], 0);
			else
				r600_bytecode_src(&alu.src[0], &ctx->src[0], 3);
			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = 2;
			alu.last = 1;
			alu.dst.write = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}

		src_loaded = TRUE;
		src_gpr = ctx->temp_reg;
	}

	if (inst->Instruction.Opcode == TGSI_OPCODE_TXD) {
		int temp_h = 0, temp_v = 0;
		int start_val = 0;

		/* if we've already loaded the src (i.e. CUBE don't reload it). */
		if (src_loaded == TRUE)
			start_val = 1;
		else
			src_loaded = TRUE;
		for (i = start_val; i < 3; i++) {
			int treg = r600_get_temp(ctx);

			if (i == 0)
				src_gpr = treg;
			else if (i == 1)
				temp_h = treg;
			else
				temp_v = treg;

			for (j = 0; j < 4; j++) {
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_MOV;
                                r600_bytecode_src(&alu.src[0], &ctx->src[i], j);
                                alu.dst.sel = treg;
                                alu.dst.chan = j;
                                if (j == 3)
                                   alu.last = 1;
                                alu.dst.write = 1;
                                r = r600_bytecode_add_alu(ctx->bc, &alu);
                                if (r)
                                    return r;
			}
		}
		for (i = 1; i < 3; i++) {
			/* set gradients h/v */
			struct r600_bytecode_tex *t = &grad_offs[n_grad_offs++];
			memset(t, 0, sizeof(struct r600_bytecode_tex));
			t->op = (i == 1) ? FETCH_OP_SET_GRADIENTS_H :
				FETCH_OP_SET_GRADIENTS_V;
			t->sampler_id = tgsi_tex_get_src_gpr(ctx, sampler_src_reg);
			t->sampler_index_mode = sampler_index_mode;
			t->resource_id = t->sampler_id + R600_MAX_CONST_BUFFERS;
			t->resource_index_mode = sampler_index_mode;

			t->src_gpr = (i == 1) ? temp_h : temp_v;
			t->src_sel_x = 0;
			t->src_sel_y = 1;
			t->src_sel_z = 2;
			t->src_sel_w = 3;

			t->dst_gpr = r600_get_temp(ctx); /* just to avoid confusing the asm scheduler */
			t->dst_sel_x = t->dst_sel_y = t->dst_sel_z = t->dst_sel_w = 7;
			if (inst->Texture.Texture != TGSI_TEXTURE_RECT) {
				t->coord_type_x = 1;
				t->coord_type_y = 1;
				t->coord_type_z = 1;
				t->coord_type_w = 1;
			}
		}
	}

	if (inst->Instruction.Opcode == TGSI_OPCODE_TG4) {
		/* Gather4 should follow the same rules as bilinear filtering, but the hardware
		 * incorrectly forces nearest filtering if the texture format is integer.
		 * The only effect it has on Gather4, which always returns 4 texels for
		 * bilinear filtering, is that the final coordinates are off by 0.5 of
		 * the texel size.
		 *
		 * The workaround is to subtract 0.5 from the unnormalized coordinates,
		 * or (0.5 / size) from the normalized coordinates.
		 */
		if (inst->Texture.ReturnType == TGSI_RETURN_TYPE_SINT ||
		    inst->Texture.ReturnType == TGSI_RETURN_TYPE_UINT) {
			int treg = r600_get_temp(ctx);

			/* mov array and comparison oordinate to temp_reg if needed */
			if ((inst->Texture.Texture == TGSI_TEXTURE_SHADOW2D ||
			     inst->Texture.Texture == TGSI_TEXTURE_2D_ARRAY ||
			     inst->Texture.Texture == TGSI_TEXTURE_SHADOW2D_ARRAY) && !src_loaded) {
				int end = inst->Texture.Texture == TGSI_TEXTURE_SHADOW2D_ARRAY ? 3 : 2;
				for (i = 2; i <= end; i++) {
					memset(&alu, 0, sizeof(struct r600_bytecode_alu));
					alu.op = ALU_OP1_MOV;
					alu.dst.sel = ctx->temp_reg;
					alu.dst.chan = i;
					alu.dst.write = 1;
					alu.last = (i == end);
					r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
					r = r600_bytecode_add_alu(ctx->bc, &alu);
					if (r)
						return r;
				}
			}

			if (inst->Texture.Texture == TGSI_TEXTURE_RECT ||
			    inst->Texture.Texture == TGSI_TEXTURE_SHADOWRECT) {
				for (i = 0; i < 2; i++) {
					memset(&alu, 0, sizeof(struct r600_bytecode_alu));
					alu.op = ALU_OP2_ADD;
					alu.dst.sel = ctx->temp_reg;
					alu.dst.chan = i;
					alu.dst.write = 1;
					alu.last = i == 1;
					if (src_loaded) {
						alu.src[0].sel = ctx->temp_reg;
						alu.src[0].chan = i;
					} else
						r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
					alu.src[1].sel = V_SQ_ALU_SRC_0_5;
					alu.src[1].neg = 1;
					r = r600_bytecode_add_alu(ctx->bc, &alu);
					if (r)
						return r;
				}
			} else {
				/* execute a TXQ */
				memset(&tex, 0, sizeof(struct r600_bytecode_tex));
				tex.op = FETCH_OP_GET_TEXTURE_RESINFO;
				tex.sampler_id = tgsi_tex_get_src_gpr(ctx, sampler_src_reg);
				tex.sampler_index_mode = sampler_index_mode;
				tex.resource_id = tex.sampler_id + R600_MAX_CONST_BUFFERS;
				tex.resource_index_mode = sampler_index_mode;
				tex.dst_gpr = treg;
				tex.src_sel_x = 4;
				tex.src_sel_y = 4;
				tex.src_sel_z = 4;
				tex.src_sel_w = 4;
				tex.dst_sel_x = 0;
				tex.dst_sel_y = 1;
				tex.dst_sel_z = 7;
				tex.dst_sel_w = 7;
				r = r600_bytecode_add_tex(ctx->bc, &tex);
				if (r)
					return r;

				/* coord.xy = -0.5 * (1.0/int_to_flt(size)) + coord.xy */
				if (ctx->bc->chip_class == CAYMAN) {
					/* */
					for (i = 0; i < 2; i++) {
						memset(&alu, 0, sizeof(struct r600_bytecode_alu));
						alu.op = ALU_OP1_INT_TO_FLT;
						alu.dst.sel = treg;
						alu.dst.chan = i;
						alu.dst.write = 1;
						alu.src[0].sel = treg;
						alu.src[0].chan = i;
						alu.last = (i == 1) ? 1 : 0;
						r = r600_bytecode_add_alu(ctx->bc, &alu);
						if (r)
							return r;
					}
					for (j = 0; j < 2; j++) {
						for (i = 0; i < 3; i++) {
							memset(&alu, 0, sizeof(struct r600_bytecode_alu));
							alu.op = ALU_OP1_RECIP_IEEE;
							alu.src[0].sel = treg;
							alu.src[0].chan = j;
							alu.dst.sel = treg;
							alu.dst.chan = i;
							if (i == 2)
								alu.last = 1;
							if (i == j)
								alu.dst.write = 1;
							r = r600_bytecode_add_alu(ctx->bc, &alu);
							if (r)
								return r;
						}
					}
				} else {
					for (i = 0; i < 2; i++) {
						memset(&alu, 0, sizeof(struct r600_bytecode_alu));
						alu.op = ALU_OP1_INT_TO_FLT;
						alu.dst.sel = treg;
						alu.dst.chan = i;
						alu.dst.write = 1;
						alu.src[0].sel = treg;
						alu.src[0].chan = i;
						alu.last = 1;
						r = r600_bytecode_add_alu(ctx->bc, &alu);
						if (r)
							return r;
					}
					for (i = 0; i < 2; i++) {
						memset(&alu, 0, sizeof(struct r600_bytecode_alu));
						alu.op = ALU_OP1_RECIP_IEEE;
						alu.src[0].sel = treg;
						alu.src[0].chan = i;
						alu.dst.sel = treg;
						alu.dst.chan = i;
						alu.last = 1;
						alu.dst.write = 1;
						r = r600_bytecode_add_alu(ctx->bc, &alu);
						if (r)
							return r;
					}
				}
				for (i = 0; i < 2; i++) {
					memset(&alu, 0, sizeof(struct r600_bytecode_alu));
					alu.op = ALU_OP3_MULADD;
					alu.is_op3 = 1;
					alu.dst.sel = ctx->temp_reg;
					alu.dst.chan = i;
					alu.dst.write = 1;
					alu.last = i == 1;
					alu.src[0].sel = treg;
					alu.src[0].chan = i;
					alu.src[1].sel = V_SQ_ALU_SRC_0_5;
					alu.src[1].neg = 1;
					if (src_loaded) {
						alu.src[2].sel = ctx->temp_reg;
						alu.src[2].chan = i;
					} else
						r600_bytecode_src(&alu.src[2], &ctx->src[0], i);
					r = r600_bytecode_add_alu(ctx->bc, &alu);
					if (r)
						return r;
				}
			}
			src_loaded = TRUE;
			src_gpr = ctx->temp_reg;
		}
	}

	if (src_requires_loading && !src_loaded) {
		for (i = 0; i < 4; i++) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_MOV;
			r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = i;
			if (i == 3)
				alu.last = 1;
			alu.dst.write = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
		src_loaded = TRUE;
		src_gpr = ctx->temp_reg;
	}

	/* get offset values */
	if (inst->Texture.NumOffsets) {
		assert(inst->Texture.NumOffsets == 1);

		/* The texture offset feature doesn't work with the TXF instruction
		 * and must be emulated by adding the offset to the texture coordinates. */
		if (txf_add_offsets) {
			const struct tgsi_texture_offset *off = inst->TexOffsets;

			switch (inst->Texture.Texture) {
			case TGSI_TEXTURE_3D:
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP2_ADD_INT;
				alu.src[0].sel = src_gpr;
				alu.src[0].chan = 2;
				alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
				alu.src[1].value = ctx->literals[4 * off[0].Index + off[0].SwizzleZ];
				alu.dst.sel = src_gpr;
				alu.dst.chan = 2;
				alu.dst.write = 1;
				alu.last = 1;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
				/* fall through */

			case TGSI_TEXTURE_2D:
			case TGSI_TEXTURE_SHADOW2D:
			case TGSI_TEXTURE_RECT:
			case TGSI_TEXTURE_SHADOWRECT:
			case TGSI_TEXTURE_2D_ARRAY:
			case TGSI_TEXTURE_SHADOW2D_ARRAY:
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP2_ADD_INT;
				alu.src[0].sel = src_gpr;
				alu.src[0].chan = 1;
				alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
				alu.src[1].value = ctx->literals[4 * off[0].Index + off[0].SwizzleY];
				alu.dst.sel = src_gpr;
				alu.dst.chan = 1;
				alu.dst.write = 1;
				alu.last = 1;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
				/* fall through */

			case TGSI_TEXTURE_1D:
			case TGSI_TEXTURE_SHADOW1D:
			case TGSI_TEXTURE_1D_ARRAY:
			case TGSI_TEXTURE_SHADOW1D_ARRAY:
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP2_ADD_INT;
				alu.src[0].sel = src_gpr;
				alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
				alu.src[1].value = ctx->literals[4 * off[0].Index + off[0].SwizzleX];
				alu.dst.sel = src_gpr;
				alu.dst.write = 1;
				alu.last = 1;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
				break;
				/* texture offsets do not apply to other texture targets */
			}
		} else {
			switch (inst->Texture.Texture) {
			case TGSI_TEXTURE_3D:
				offset_z = ctx->literals[4 * inst->TexOffsets[0].Index + inst->TexOffsets[0].SwizzleZ] << 1;
				/* fallthrough */
			case TGSI_TEXTURE_2D:
			case TGSI_TEXTURE_SHADOW2D:
			case TGSI_TEXTURE_RECT:
			case TGSI_TEXTURE_SHADOWRECT:
			case TGSI_TEXTURE_2D_ARRAY:
			case TGSI_TEXTURE_SHADOW2D_ARRAY:
				offset_y = ctx->literals[4 * inst->TexOffsets[0].Index + inst->TexOffsets[0].SwizzleY] << 1;
				/* fallthrough */
			case TGSI_TEXTURE_1D:
			case TGSI_TEXTURE_SHADOW1D:
			case TGSI_TEXTURE_1D_ARRAY:
			case TGSI_TEXTURE_SHADOW1D_ARRAY:
				offset_x = ctx->literals[4 * inst->TexOffsets[0].Index + inst->TexOffsets[0].SwizzleX] << 1;
			}
		}
	}

	/* Obtain the sample index for reading a compressed MSAA color texture.
	 * To read the FMASK, we use the ldfptr instruction, which tells us
	 * where the samples are stored.
	 * For uncompressed 8x MSAA surfaces, ldfptr should return 0x76543210,
	 * which is the identity mapping. Each nibble says which physical sample
	 * should be fetched to get that sample.
	 *
	 * Assume src.z contains the sample index. It should be modified like this:
	 *   src.z = (ldfptr() >> (src.z * 4)) & 0xF;
	 * Then fetch the texel with src.
	 */
	if (read_compressed_msaa) {
		unsigned sample_chan = 3;
		unsigned temp = r600_get_temp(ctx);
		assert(src_loaded);

		/* temp.w = ldfptr() */
		memset(&tex, 0, sizeof(struct r600_bytecode_tex));
		tex.op = FETCH_OP_LD;
		tex.inst_mod = 1; /* to indicate this is ldfptr */
		tex.sampler_id = tgsi_tex_get_src_gpr(ctx, sampler_src_reg);
		tex.sampler_index_mode = sampler_index_mode;
		tex.resource_id = tex.sampler_id + R600_MAX_CONST_BUFFERS;
		tex.resource_index_mode = sampler_index_mode;
		tex.src_gpr = src_gpr;
		tex.dst_gpr = temp;
		tex.dst_sel_x = 7; /* mask out these components */
		tex.dst_sel_y = 7;
		tex.dst_sel_z = 7;
		tex.dst_sel_w = 0; /* store X */
		tex.src_sel_x = 0;
		tex.src_sel_y = 1;
		tex.src_sel_z = 2;
		tex.src_sel_w = 3;
		tex.offset_x = offset_x;
		tex.offset_y = offset_y;
		tex.offset_z = offset_z;
		r = r600_bytecode_add_tex(ctx->bc, &tex);
		if (r)
			return r;

		/* temp.x = sample_index*4 */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_MULLO_INT;
		alu.src[0].sel = src_gpr;
		alu.src[0].chan = sample_chan;
		alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
		alu.src[1].value = 4;
		alu.dst.sel = temp;
		alu.dst.chan = 0;
		alu.dst.write = 1;
		r = emit_mul_int_op(ctx->bc, &alu);
		if (r)
			return r;

		/* sample_index = temp.w >> temp.x */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_LSHR_INT;
		alu.src[0].sel = temp;
		alu.src[0].chan = 3;
		alu.src[1].sel = temp;
		alu.src[1].chan = 0;
		alu.dst.sel = src_gpr;
		alu.dst.chan = sample_chan;
		alu.dst.write = 1;
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;

		/* sample_index & 0xF */
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_AND_INT;
		alu.src[0].sel = src_gpr;
		alu.src[0].chan = sample_chan;
		alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
		alu.src[1].value = 0xF;
		alu.dst.sel = src_gpr;
		alu.dst.chan = sample_chan;
		alu.dst.write = 1;
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
#if 0
		/* visualize the FMASK */
		for (i = 0; i < 4; i++) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_INT_TO_FLT;
			alu.src[0].sel = src_gpr;
			alu.src[0].chan = sample_chan;
			alu.dst.sel = ctx->file_offset[inst->Dst[0].Register.File] + inst->Dst[0].Register.Index;
			alu.dst.chan = i;
			alu.dst.write = 1;
			alu.last = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
		return 0;
#endif
	}

	/* does this shader want a num layers from TXQ for a cube array? */
	if (has_txq_cube_array_z) {
		int id = tgsi_tex_get_src_gpr(ctx, sampler_src_reg);

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;

		alu.src[0].sel = R600_SHADER_BUFFER_INFO_SEL;
		if (ctx->bc->chip_class >= EVERGREEN) {
			/* with eg each dword is number of cubes */
			alu.src[0].sel += id / 4;
			alu.src[0].chan = id % 4;
		} else {
			/* r600 we have them at channel 2 of the second dword */
			alu.src[0].sel += (id * 2) + 1;
			alu.src[0].chan = 2;
		}
		alu.src[0].kc_bank = R600_BUFFER_INFO_CONST_BUFFER;
		tgsi_dst(ctx, &inst->Dst[0], 2, &alu.dst);
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
		/* disable writemask from texture instruction */
		inst->Dst[0].Register.WriteMask &= ~4;
	}

	opcode = ctx->inst_info->op;
	if (opcode == FETCH_OP_GATHER4 &&
		inst->TexOffsets[0].File != TGSI_FILE_NULL &&
		inst->TexOffsets[0].File != TGSI_FILE_IMMEDIATE) {
		struct r600_bytecode_tex *t;
		opcode = FETCH_OP_GATHER4_O;

		/* GATHER4_O/GATHER4_C_O use offset values loaded by
		   SET_TEXTURE_OFFSETS instruction. The immediate offset values
		   encoded in the instruction are ignored. */
		t = &grad_offs[n_grad_offs++];
		memset(t, 0, sizeof(struct r600_bytecode_tex));
		t->op = FETCH_OP_SET_TEXTURE_OFFSETS;
		t->sampler_id = tgsi_tex_get_src_gpr(ctx, sampler_src_reg);
		t->sampler_index_mode = sampler_index_mode;
		t->resource_id = t->sampler_id + R600_MAX_CONST_BUFFERS;
		t->resource_index_mode = sampler_index_mode;

		t->src_gpr = ctx->file_offset[inst->TexOffsets[0].File] + inst->TexOffsets[0].Index;
		t->src_sel_x = inst->TexOffsets[0].SwizzleX;
		t->src_sel_y = inst->TexOffsets[0].SwizzleY;
		if (inst->Texture.Texture == TGSI_TEXTURE_2D_ARRAY ||
			 inst->Texture.Texture == TGSI_TEXTURE_SHADOW2D_ARRAY)
			/* make sure array index selector is 0, this is just a safety
			 * precausion because TGSI seems to emit something strange here */
			t->src_sel_z = 4;
		else
			t->src_sel_z = inst->TexOffsets[0].SwizzleZ;

		t->src_sel_w = 4;

		t->dst_sel_x = 7;
		t->dst_sel_y = 7;
		t->dst_sel_z = 7;
		t->dst_sel_w = 7;
	}

	if (inst->Texture.Texture == TGSI_TEXTURE_SHADOW1D ||
	    inst->Texture.Texture == TGSI_TEXTURE_SHADOW2D ||
	    inst->Texture.Texture == TGSI_TEXTURE_SHADOWRECT ||
	    inst->Texture.Texture == TGSI_TEXTURE_SHADOWCUBE ||
	    inst->Texture.Texture == TGSI_TEXTURE_SHADOW1D_ARRAY ||
	    inst->Texture.Texture == TGSI_TEXTURE_SHADOW2D_ARRAY ||
	    inst->Texture.Texture == TGSI_TEXTURE_SHADOWCUBE_ARRAY) {
		switch (opcode) {
		case FETCH_OP_SAMPLE:
			opcode = FETCH_OP_SAMPLE_C;
			break;
		case FETCH_OP_SAMPLE_L:
			opcode = FETCH_OP_SAMPLE_C_L;
			break;
		case FETCH_OP_SAMPLE_LB:
			opcode = FETCH_OP_SAMPLE_C_LB;
			break;
		case FETCH_OP_SAMPLE_G:
			opcode = FETCH_OP_SAMPLE_C_G;
			break;
		/* Texture gather variants */
		case FETCH_OP_GATHER4:
			opcode = FETCH_OP_GATHER4_C;
			break;
		case FETCH_OP_GATHER4_O:
			opcode = FETCH_OP_GATHER4_C_O;
			break;
		}
	}

	memset(&tex, 0, sizeof(struct r600_bytecode_tex));
	tex.op = opcode;

	tex.sampler_id = tgsi_tex_get_src_gpr(ctx, sampler_src_reg);
	tex.sampler_index_mode = sampler_index_mode;
	tex.resource_id = tex.sampler_id + R600_MAX_CONST_BUFFERS;
	tex.resource_index_mode = sampler_index_mode;
	tex.src_gpr = src_gpr;
	tex.dst_gpr = ctx->file_offset[inst->Dst[0].Register.File] + inst->Dst[0].Register.Index;

	if (inst->Instruction.Opcode == TGSI_OPCODE_DDX_FINE ||
		inst->Instruction.Opcode == TGSI_OPCODE_DDY_FINE) {
		tex.inst_mod = 1; /* per pixel gradient calculation instead of per 2x2 quad */
	}

	if (inst->Instruction.Opcode == TGSI_OPCODE_TG4) {
		int8_t texture_component_select = ctx->literals[4 * inst->Src[1].Register.Index + inst->Src[1].Register.SwizzleX];
		tex.inst_mod = texture_component_select;

		if (ctx->bc->chip_class == CAYMAN) {
			tex.dst_sel_x = (inst->Dst[0].Register.WriteMask & 1) ? 0 : 7;
			tex.dst_sel_y = (inst->Dst[0].Register.WriteMask & 2) ? 1 : 7;
			tex.dst_sel_z = (inst->Dst[0].Register.WriteMask & 4) ? 2 : 7;
			tex.dst_sel_w = (inst->Dst[0].Register.WriteMask & 8) ? 3 : 7;
		} else {
			/* GATHER4 result order is different from TGSI TG4 */
			tex.dst_sel_x = (inst->Dst[0].Register.WriteMask & 1) ? 1 : 7;
			tex.dst_sel_y = (inst->Dst[0].Register.WriteMask & 2) ? 2 : 7;
			tex.dst_sel_z = (inst->Dst[0].Register.WriteMask & 4) ? 0 : 7;
			tex.dst_sel_w = (inst->Dst[0].Register.WriteMask & 8) ? 3 : 7;
		}
	}
	else if (inst->Instruction.Opcode == TGSI_OPCODE_LODQ) {
		tex.dst_sel_x = (inst->Dst[0].Register.WriteMask & 2) ? 1 : 7;
		tex.dst_sel_y = (inst->Dst[0].Register.WriteMask & 1) ? 0 : 7;
		tex.dst_sel_z = 7;
		tex.dst_sel_w = 7;
	}
	else if (inst->Instruction.Opcode == TGSI_OPCODE_TXQS) {
		tex.dst_sel_x = 3;
		tex.dst_sel_y = 7;
		tex.dst_sel_z = 7;
		tex.dst_sel_w = 7;
	}
	else {
		tex.dst_sel_x = (inst->Dst[0].Register.WriteMask & 1) ? 0 : 7;
		tex.dst_sel_y = (inst->Dst[0].Register.WriteMask & 2) ? 1 : 7;
		tex.dst_sel_z = (inst->Dst[0].Register.WriteMask & 4) ? 2 : 7;
		tex.dst_sel_w = (inst->Dst[0].Register.WriteMask & 8) ? 3 : 7;
	}


	if (inst->Instruction.Opcode == TGSI_OPCODE_TXQS) {
		tex.src_sel_x = 4;
		tex.src_sel_y = 4;
		tex.src_sel_z = 4;
		tex.src_sel_w = 4;
	} else if (src_loaded) {
		tex.src_sel_x = 0;
		tex.src_sel_y = 1;
		tex.src_sel_z = 2;
		tex.src_sel_w = 3;
	} else {
		tex.src_sel_x = ctx->src[0].swizzle[0];
		tex.src_sel_y = ctx->src[0].swizzle[1];
		tex.src_sel_z = ctx->src[0].swizzle[2];
		tex.src_sel_w = ctx->src[0].swizzle[3];
		tex.src_rel = ctx->src[0].rel;
	}

	if (inst->Texture.Texture == TGSI_TEXTURE_CUBE ||
	    inst->Texture.Texture == TGSI_TEXTURE_SHADOWCUBE ||
	    inst->Texture.Texture == TGSI_TEXTURE_CUBE_ARRAY ||
	    inst->Texture.Texture == TGSI_TEXTURE_SHADOWCUBE_ARRAY) {
		tex.src_sel_x = 1;
		tex.src_sel_y = 0;
		tex.src_sel_z = 3;
		tex.src_sel_w = 2; /* route Z compare or Lod value into W */
	}

	if (inst->Texture.Texture != TGSI_TEXTURE_RECT &&
	    inst->Texture.Texture != TGSI_TEXTURE_SHADOWRECT) {
		tex.coord_type_x = 1;
		tex.coord_type_y = 1;
	}
	tex.coord_type_z = 1;
	tex.coord_type_w = 1;

	tex.offset_x = offset_x;
	tex.offset_y = offset_y;
	if (inst->Instruction.Opcode == TGSI_OPCODE_TG4 &&
		(inst->Texture.Texture == TGSI_TEXTURE_2D_ARRAY ||
		 inst->Texture.Texture == TGSI_TEXTURE_SHADOW2D_ARRAY)) {
		tex.offset_z = 0;
	}
	else {
		tex.offset_z = offset_z;
	}

	/* Put the depth for comparison in W.
	 * TGSI_TEXTURE_SHADOW2D_ARRAY already has the depth in W.
	 * Some instructions expect the depth in Z. */
	if ((inst->Texture.Texture == TGSI_TEXTURE_SHADOW1D ||
	     inst->Texture.Texture == TGSI_TEXTURE_SHADOW2D ||
	     inst->Texture.Texture == TGSI_TEXTURE_SHADOWRECT ||
	     inst->Texture.Texture == TGSI_TEXTURE_SHADOW1D_ARRAY) &&
	    opcode != FETCH_OP_SAMPLE_C_L &&
	    opcode != FETCH_OP_SAMPLE_C_LB) {
		tex.src_sel_w = tex.src_sel_z;
	}

	if (inst->Texture.Texture == TGSI_TEXTURE_1D_ARRAY ||
	    inst->Texture.Texture == TGSI_TEXTURE_SHADOW1D_ARRAY) {
		if (opcode == FETCH_OP_SAMPLE_C_L ||
		    opcode == FETCH_OP_SAMPLE_C_LB) {
			/* the array index is read from Y */
			tex.coord_type_y = 0;
			array_index_offset_channel = tex.src_sel_y;
		} else {
			/* the array index is read from Z */
			tex.coord_type_z = 0;
			tex.src_sel_z = tex.src_sel_y;
			array_index_offset_channel = tex.src_sel_z;
		}
	} else if (inst->Texture.Texture == TGSI_TEXTURE_2D_ARRAY ||
		    inst->Texture.Texture == TGSI_TEXTURE_SHADOW2D_ARRAY) {
		tex.coord_type_z = 0;
		array_index_offset_channel = tex.src_sel_z;
	} else if  ((inst->Texture.Texture == TGSI_TEXTURE_CUBE_ARRAY ||
		    inst->Texture.Texture == TGSI_TEXTURE_SHADOWCUBE_ARRAY) &&
		    (ctx->bc->chip_class >= EVERGREEN))
		/* the array index is read from Z, coordinate will be corrected elsewhere  */
		tex.coord_type_z = 0;

	/* We have array access to 1D or 2D ARRAY, the coordinates are not int ->
	 * evaluate the array index  */
	if (array_index_offset_channel >= 0 &&
		 opcode != FETCH_OP_LD &&
		 opcode != FETCH_OP_GET_TEXTURE_RESINFO) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.src[0].sel =  tex.src_gpr;
		alu.src[0].chan =  array_index_offset_channel;
		alu.src[0].rel = tex.src_rel;
		alu.op = ALU_OP1_RNDNE;
		alu.dst.sel = tex.src_gpr;
		alu.dst.chan = array_index_offset_channel;
		alu.dst.rel = tex.src_rel;
		alu.dst.write = 1;
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	/* mask unused source components */
	if (opcode == FETCH_OP_SAMPLE || opcode == FETCH_OP_GATHER4) {
		switch (inst->Texture.Texture) {
		case TGSI_TEXTURE_2D:
		case TGSI_TEXTURE_RECT:
			tex.src_sel_z = 7;
			tex.src_sel_w = 7;
			break;
		case TGSI_TEXTURE_1D_ARRAY:
			tex.src_sel_y = 7;
			tex.src_sel_w = 7;
			break;
		case TGSI_TEXTURE_1D:
			tex.src_sel_y = 7;
			tex.src_sel_z = 7;
			tex.src_sel_w = 7;
			break;
		}
	}

	/* Emit set gradient and offset instructions. */
	for (i = 0; i < n_grad_offs; ++i) {
		r = r600_bytecode_add_tex(ctx->bc, &grad_offs[i]);
		if (r)
			return r;
	}

	r = r600_bytecode_add_tex(ctx->bc, &tex);
	if (r)
		return r;

	/* add shadow ambient support  - gallium doesn't do it yet */
	return 0;
}

static int find_hw_atomic_counter(struct r600_shader_ctx *ctx,
				  struct tgsi_full_src_register *src)
{
	unsigned i;

	if (src->Register.Indirect) {
		for (i = 0; i < ctx->shader->nhwatomic_ranges; i++) {
			if (src->Indirect.ArrayID == ctx->shader->atomics[i].array_id)
				return ctx->shader->atomics[i].hw_idx;
		}
	} else {
		uint32_t index = src->Register.Index;
		for (i = 0; i < ctx->shader->nhwatomic_ranges; i++) {
			if (ctx->shader->atomics[i].buffer_id != (unsigned)src->Dimension.Index)
				continue;
			if (index > ctx->shader->atomics[i].end)
				continue;
			if (index < ctx->shader->atomics[i].start)
				continue;
			uint32_t offset = (index - ctx->shader->atomics[i].start);
			return ctx->shader->atomics[i].hw_idx + offset;
		}
	}
	assert(0);
	return -1;
}

static int tgsi_set_gds_temp(struct r600_shader_ctx *ctx,
			     int *uav_id_p, int *uav_index_mode_p)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	int uav_id, uav_index_mode = 0;
	int r;
	bool is_cm = (ctx->bc->chip_class == CAYMAN);

	uav_id = find_hw_atomic_counter(ctx, &inst->Src[0]);

	if (inst->Src[0].Register.Indirect) {
		if (is_cm) {
			struct r600_bytecode_alu alu;
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP2_LSHL_INT;
			alu.src[0].sel = get_address_file_reg(ctx, inst->Src[0].Indirect.Index);
			alu.src[0].chan = 0;
			alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
			alu.src[1].value = 2;
			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = 0;
			alu.dst.write = 1;
			alu.last = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;

			r = single_alu_op2(ctx, ALU_OP2_ADD_INT,
					   ctx->temp_reg, 0,
					   ctx->temp_reg, 0,
					   V_SQ_ALU_SRC_LITERAL, uav_id * 4);
			if (r)
				return r;
		} else
			uav_index_mode = 2;
	} else if (is_cm) {
		r = single_alu_op2(ctx, ALU_OP1_MOV,
				   ctx->temp_reg, 0,
				   V_SQ_ALU_SRC_LITERAL, uav_id * 4,
				   0, 0);
		if (r)
			return r;
	}
	*uav_id_p = uav_id;
	*uav_index_mode_p = uav_index_mode;
	return 0;
}

static int tgsi_load_gds(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	int r;
	struct r600_bytecode_gds gds;
	int uav_id = 0;
	int uav_index_mode = 0;
	bool is_cm = (ctx->bc->chip_class == CAYMAN);

	r = tgsi_set_gds_temp(ctx, &uav_id, &uav_index_mode);
	if (r)
		return r;

	memset(&gds, 0, sizeof(struct r600_bytecode_gds));
	gds.op = FETCH_OP_GDS_READ_RET;
	gds.dst_gpr = ctx->file_offset[inst->Dst[0].Register.File] + inst->Dst[0].Register.Index;
	gds.uav_id = is_cm ? 0 : uav_id;
	gds.uav_index_mode = is_cm ? 0 : uav_index_mode;
	gds.src_gpr = ctx->temp_reg;
	gds.src_sel_x = (is_cm) ? 0 : 4;
	gds.src_sel_y = 4;
	gds.src_sel_z = 4;
	gds.dst_sel_x = 0;
	gds.dst_sel_y = 7;
	gds.dst_sel_z = 7;
	gds.dst_sel_w = 7;
	gds.src_gpr2 = 0;
	gds.alloc_consume = !is_cm;
	r = r600_bytecode_add_gds(ctx->bc, &gds);
	if (r)
		return r;

	ctx->bc->cf_last->vpm = 1;
	return 0;
}

/* this fixes up 1D arrays properly */
static int load_index_src(struct r600_shader_ctx *ctx, int src_index, int *idx_gpr)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	int r, i;
	struct r600_bytecode_alu alu;
	int temp_reg = r600_get_temp(ctx);

	for (i = 0; i < 4; i++) {
		bool def_val = true, write_zero = false;
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.dst.sel = temp_reg;
		alu.dst.chan = i;

		switch (inst->Memory.Texture) {
		case TGSI_TEXTURE_BUFFER:
		case TGSI_TEXTURE_1D:
			if (i == 1 || i == 2 || i == 3) {
				write_zero = true;
			}
			break;
		case TGSI_TEXTURE_1D_ARRAY:
			if (i == 1 || i == 3)
				write_zero = true;
			else if (i == 2) {
				r600_bytecode_src(&alu.src[0], &ctx->src[src_index], 1);
				def_val = false;
			}
			break;
		case TGSI_TEXTURE_2D:
			if (i == 2 || i == 3)
				write_zero = true;
			break;
		default:
			if (i == 3)
				write_zero = true;
			break;
		}

		if (write_zero) {
			alu.src[0].sel = V_SQ_ALU_SRC_LITERAL;
			alu.src[0].value = 0;
		} else if (def_val) {
			r600_bytecode_src(&alu.src[0], &ctx->src[src_index], i);
		}

		if (i == 3)
			alu.last = 1;
		alu.dst.write = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	*idx_gpr = temp_reg;
	return 0;
}

static int load_buffer_coord(struct r600_shader_ctx *ctx, int src_idx,
			     int temp_reg)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	int r;
	if (inst->Src[src_idx].Register.File == TGSI_FILE_IMMEDIATE) {
		int value = (ctx->literals[4 * inst->Src[src_idx].Register.Index + inst->Src[src_idx].Register.SwizzleX]);
		r = single_alu_op2(ctx, ALU_OP1_MOV,
				   temp_reg, 0,
				   V_SQ_ALU_SRC_LITERAL, value >> 2,
				   0, 0);
		if (r)
			return r;
	} else {
		struct r600_bytecode_alu alu;
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_LSHR_INT;
		r600_bytecode_src(&alu.src[0], &ctx->src[src_idx], 0);
		alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
		alu.src[1].value = 2;
		alu.dst.sel = temp_reg;
		alu.dst.write = 1;
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_load_buffer(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	/* have to work out the offset into the RAT immediate return buffer */
	struct r600_bytecode_vtx vtx;
	struct r600_bytecode_cf *cf;
	int r;
	int temp_reg = r600_get_temp(ctx);
	unsigned rat_index_mode;
	unsigned base;

	rat_index_mode = inst->Src[0].Indirect.Index == 2 ? 2 : 0; // CF_INDEX_1 : CF_INDEX_NONE
	base = R600_IMAGE_REAL_RESOURCE_OFFSET + ctx->info.file_count[TGSI_FILE_IMAGE];

	r = load_buffer_coord(ctx, 1, temp_reg);
	if (r)
		return r;
	ctx->bc->cf_last->barrier = 1;
	memset(&vtx, 0, sizeof(struct r600_bytecode_vtx));
	vtx.op = FETCH_OP_VFETCH;
	vtx.buffer_id = inst->Src[0].Register.Index + base;
	vtx.buffer_index_mode = rat_index_mode;
	vtx.fetch_type = SQ_VTX_FETCH_NO_INDEX_OFFSET;
	vtx.src_gpr = temp_reg;
	vtx.src_sel_x = 0;
	vtx.dst_gpr = ctx->file_offset[inst->Dst[0].Register.File] + inst->Dst[0].Register.Index;
	vtx.dst_sel_x = (inst->Dst[0].Register.WriteMask & 1) ? 0 : 7;		/* SEL_X */
	vtx.dst_sel_y = (inst->Dst[0].Register.WriteMask & 2) ? 1 : 7;		/* SEL_Y */
	vtx.dst_sel_z = (inst->Dst[0].Register.WriteMask & 4) ? 2 : 7;		/* SEL_Z */
	vtx.dst_sel_w = (inst->Dst[0].Register.WriteMask & 8) ? 3 : 7;		/* SEL_W */
	vtx.num_format_all = 1;
	vtx.format_comp_all = 1;
	vtx.srf_mode_all = 0;

	if (inst->Dst[0].Register.WriteMask & 8) {
		vtx.data_format = FMT_32_32_32_32;
		vtx.use_const_fields = 0;
	} else if (inst->Dst[0].Register.WriteMask & 4) {
		vtx.data_format = FMT_32_32_32;
		vtx.use_const_fields = 0;
	} else if (inst->Dst[0].Register.WriteMask & 2) {
		vtx.data_format = FMT_32_32;
		vtx.use_const_fields = 0;
	} else {
		vtx.data_format = FMT_32;
		vtx.use_const_fields = 0;
	}

	r = r600_bytecode_add_vtx_tc(ctx->bc, &vtx);
	if (r)
		return r;
	cf = ctx->bc->cf_last;
	cf->barrier = 1;
	return 0;
}

static int tgsi_load_rat(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	/* have to work out the offset into the RAT immediate return buffer */
	struct r600_bytecode_vtx vtx;
	struct r600_bytecode_cf *cf;
	int r;
	int idx_gpr;
	unsigned format, num_format, format_comp, endian;
	const struct util_format_description *desc;
	unsigned rat_index_mode;
	unsigned immed_base;

	rat_index_mode = inst->Src[0].Indirect.Index == 2 ? 2 : 0; // CF_INDEX_1 : CF_INDEX_NONE

	immed_base = R600_IMAGE_IMMED_RESOURCE_OFFSET;
	r = load_index_src(ctx, 1, &idx_gpr);
	if (r)
		return r;

	if (rat_index_mode)
		egcm_load_index_reg(ctx->bc, 1, false);

	r600_bytecode_add_cfinst(ctx->bc, CF_OP_MEM_RAT);
	cf = ctx->bc->cf_last;

	cf->rat.id = ctx->shader->rat_base + inst->Src[0].Register.Index;
	cf->rat.inst = V_RAT_INST_NOP_RTN;
	cf->rat.index_mode = rat_index_mode;
	cf->output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_READ_IND;
	cf->output.gpr = ctx->thread_id_gpr;
	cf->output.index_gpr = idx_gpr;
	cf->output.comp_mask = 0xf;
	cf->output.burst_count = 1;
	cf->vpm = 1;
	cf->barrier = 1;
	cf->mark = 1;
	cf->output.elem_size = 0;

	r600_bytecode_add_cfinst(ctx->bc, CF_OP_WAIT_ACK);
	cf = ctx->bc->cf_last;
	cf->barrier = 1;

	desc = util_format_description(inst->Memory.Format);
	r600_vertex_data_type(inst->Memory.Format,
			      &format, &num_format, &format_comp, &endian);
	memset(&vtx, 0, sizeof(struct r600_bytecode_vtx));
	vtx.op = FETCH_OP_VFETCH;
	vtx.buffer_id = immed_base + inst->Src[0].Register.Index;
	vtx.buffer_index_mode = rat_index_mode;
	vtx.fetch_type = SQ_VTX_FETCH_NO_INDEX_OFFSET;
	vtx.src_gpr = ctx->thread_id_gpr;
	vtx.src_sel_x = 1;
	vtx.dst_gpr = ctx->file_offset[inst->Dst[0].Register.File] + inst->Dst[0].Register.Index;
	vtx.dst_sel_x = desc->swizzle[0];
	vtx.dst_sel_y = desc->swizzle[1];
	vtx.dst_sel_z = desc->swizzle[2];
	vtx.dst_sel_w = desc->swizzle[3];
	vtx.srf_mode_all = 1;
	vtx.data_format = format;
	vtx.num_format_all = num_format;
	vtx.format_comp_all = format_comp;
	vtx.endian = endian;
	vtx.offset = 0;
	vtx.mega_fetch_count = 3;
	r = r600_bytecode_add_vtx_tc(ctx->bc, &vtx);
	if (r)
		return r;
	cf = ctx->bc->cf_last;
	cf->barrier = 1;
	return 0;
}

static int tgsi_load_lds(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int r;
	int temp_reg = r600_get_temp(ctx);
	
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	r600_bytecode_src(&alu.src[0], &ctx->src[1], 0);
	alu.dst.sel = temp_reg;
	alu.dst.write = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	
	r = do_lds_fetch_values(ctx, temp_reg,
				ctx->file_offset[inst->Dst[0].Register.File] + inst->Dst[0].Register.Index, inst->Dst[0].Register.WriteMask);
	if (r)
		return r;
	return 0;
}

static int tgsi_load(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	if (inst->Src[0].Register.File == TGSI_FILE_IMAGE)
		return tgsi_load_rat(ctx);
	if (inst->Src[0].Register.File == TGSI_FILE_HW_ATOMIC)
		return tgsi_load_gds(ctx);
	if (inst->Src[0].Register.File == TGSI_FILE_BUFFER)
		return tgsi_load_buffer(ctx);
	if (inst->Src[0].Register.File == TGSI_FILE_MEMORY)
		return tgsi_load_lds(ctx);
	return 0;
}

static int tgsi_store_buffer_rat(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_cf *cf;
	int r, i;
	unsigned rat_index_mode;
	int lasti;
	int temp_reg = r600_get_temp(ctx), treg2 = r600_get_temp(ctx);

	r = load_buffer_coord(ctx, 0, treg2);
	if (r)
		return r;

	rat_index_mode = inst->Dst[0].Indirect.Index == 2 ? 2 : 0; // CF_INDEX_1 : CF_INDEX_NONE
	if (rat_index_mode)
		egcm_load_index_reg(ctx->bc, 1, false);

	for (i = 0; i <= 3; i++) {
		struct r600_bytecode_alu alu;
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.dst.sel = temp_reg;
		alu.dst.chan = i;
		alu.src[0].sel = V_SQ_ALU_SRC_0;
		alu.last = (i == 3);
		alu.dst.write = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);
	for (i = 0; i <= lasti; i++) {
		struct r600_bytecode_alu alu;
		if (!((1 << i) & inst->Dst[0].Register.WriteMask))
			continue;

		r = single_alu_op2(ctx, ALU_OP2_ADD_INT,
				   temp_reg, 0,
				   treg2, 0,
				   V_SQ_ALU_SRC_LITERAL, i);
		if (r)
			return r;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = 0;

		r600_bytecode_src(&alu.src[0], &ctx->src[1], i);
		alu.last = 1;
		alu.dst.write = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;

		r600_bytecode_add_cfinst(ctx->bc, CF_OP_MEM_RAT);
		cf = ctx->bc->cf_last;

		cf->rat.id = ctx->shader->rat_base + inst->Dst[0].Register.Index + ctx->info.file_count[TGSI_FILE_IMAGE];
		cf->rat.inst = V_RAT_INST_STORE_TYPED;
		cf->rat.index_mode = rat_index_mode;
		cf->output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_WRITE_IND;
		cf->output.gpr = ctx->temp_reg;
		cf->output.index_gpr = temp_reg;
		cf->output.comp_mask = 1;
		cf->output.burst_count = 1;
		cf->vpm = 1;
		cf->barrier = 1;
		cf->output.elem_size = 0;
	}
	return 0;
}

static int tgsi_store_rat(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_cf *cf;
	bool src_requires_loading = false;
	int val_gpr, idx_gpr;
	int r, i;
	unsigned rat_index_mode;

	rat_index_mode = inst->Dst[0].Indirect.Index == 2 ? 2 : 0; // CF_INDEX_1 : CF_INDEX_NONE

	r = load_index_src(ctx, 0, &idx_gpr);
	if (r)
		return r;

	if (inst->Src[1].Register.File != TGSI_FILE_TEMPORARY)
		src_requires_loading = true;

	if (src_requires_loading) {
		struct r600_bytecode_alu alu;
		for (i = 0; i < 4; i++) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_MOV;
			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = i;

			r600_bytecode_src(&alu.src[0], &ctx->src[1], i);
			if (i == 3)
				alu.last = 1;
			alu.dst.write = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
		val_gpr = ctx->temp_reg;
	} else
		val_gpr = tgsi_tex_get_src_gpr(ctx, 1);
	if (rat_index_mode)
		egcm_load_index_reg(ctx->bc, 1, false);

	r600_bytecode_add_cfinst(ctx->bc, CF_OP_MEM_RAT);
	cf = ctx->bc->cf_last;

	cf->rat.id = ctx->shader->rat_base + inst->Dst[0].Register.Index;
	cf->rat.inst = V_RAT_INST_STORE_TYPED;
	cf->rat.index_mode = rat_index_mode;
	cf->output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_WRITE_IND;
	cf->output.gpr = val_gpr;
	cf->output.index_gpr = idx_gpr;
	cf->output.comp_mask = 0xf;
	cf->output.burst_count = 1;
	cf->vpm = 1;
	cf->barrier = 1;
	cf->output.elem_size = 0;
	return 0;
}

static int tgsi_store_lds(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int r, i, lasti;
	int write_mask = inst->Dst[0].Register.WriteMask;
	int temp_reg = r600_get_temp(ctx);

	/* LDS write */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
	alu.dst.sel = temp_reg;
	alu.dst.write = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	lasti = tgsi_last_instruction(write_mask);
	for (i = 1; i <= lasti; i++) {
		if (!(write_mask & (1 << i)))
			continue;
		r = single_alu_op2(ctx, ALU_OP2_ADD_INT,
				   temp_reg, i,
				   temp_reg, 0,
				   V_SQ_ALU_SRC_LITERAL, 4 * i);
		if (r)
			return r;
	}
	for (i = 0; i <= lasti; i++) {
		if (!(write_mask & (1 << i)))
			continue;

		if ((i == 0 && ((write_mask & 3) == 3)) ||
		    (i == 2 && ((write_mask & 0xc) == 0xc))) {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = LDS_OP3_LDS_WRITE_REL;

			alu.src[0].sel = temp_reg;
			alu.src[0].chan = i;
			r600_bytecode_src(&alu.src[1], &ctx->src[1], i);
			r600_bytecode_src(&alu.src[2], &ctx->src[1], i + 1);
			alu.last = 1;
			alu.is_lds_idx_op = true;
			alu.lds_idx = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
			i += 1;
			continue;
		}
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = LDS_OP2_LDS_WRITE;

		alu.src[0].sel = temp_reg;
		alu.src[0].chan = i;
		r600_bytecode_src(&alu.src[1], &ctx->src[1], i);

		alu.last = 1;
		alu.is_lds_idx_op = true;

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_store(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	if (inst->Dst[0].Register.File == TGSI_FILE_BUFFER)
		return tgsi_store_buffer_rat(ctx);
	else if (inst->Dst[0].Register.File == TGSI_FILE_MEMORY)
		return tgsi_store_lds(ctx);
	else
		return tgsi_store_rat(ctx);
}

static int tgsi_atomic_op_rat(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	/* have to work out the offset into the RAT immediate return buffer */
	struct r600_bytecode_alu alu;
	struct r600_bytecode_vtx vtx;
	struct r600_bytecode_cf *cf;
	int r;
	int idx_gpr;
	unsigned format, num_format, format_comp, endian;
	const struct util_format_description *desc;
	unsigned rat_index_mode;
	unsigned immed_base;
	unsigned rat_base;

	immed_base = R600_IMAGE_IMMED_RESOURCE_OFFSET;
	rat_base = ctx->shader->rat_base;

        if (inst->Src[0].Register.File == TGSI_FILE_BUFFER) {
		immed_base += ctx->info.file_count[TGSI_FILE_IMAGE];
		rat_base += ctx->info.file_count[TGSI_FILE_IMAGE];

		r = load_buffer_coord(ctx, 1, ctx->temp_reg);
		if (r)
			return r;
		idx_gpr = ctx->temp_reg;
	} else {
		r = load_index_src(ctx, 1, &idx_gpr);
		if (r)
			return r;
	}

	rat_index_mode = inst->Src[0].Indirect.Index == 2 ? 2 : 0; // CF_INDEX_1 : CF_INDEX_NONE

	if (ctx->inst_info->op == V_RAT_INST_CMPXCHG_INT_RTN) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.dst.sel = ctx->thread_id_gpr;
		alu.dst.chan = 0;
		alu.dst.write = 1;
		r600_bytecode_src(&alu.src[0], &ctx->src[3], 0);
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.dst.sel = ctx->thread_id_gpr;
		if (ctx->bc->chip_class == CAYMAN)
			alu.dst.chan = 2;
		else
			alu.dst.chan = 3;
		alu.dst.write = 1;
		r600_bytecode_src(&alu.src[0], &ctx->src[2], 0);
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	} else {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.dst.sel = ctx->thread_id_gpr;
		alu.dst.chan = 0;
		alu.dst.write = 1;
		r600_bytecode_src(&alu.src[0], &ctx->src[2], 0);
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	if (rat_index_mode)
		egcm_load_index_reg(ctx->bc, 1, false);
	r600_bytecode_add_cfinst(ctx->bc, CF_OP_MEM_RAT);
	cf = ctx->bc->cf_last;

	cf->rat.id = rat_base + inst->Src[0].Register.Index;
	cf->rat.inst = ctx->inst_info->op;
	cf->rat.index_mode = rat_index_mode;
	cf->output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_READ_IND;
	cf->output.gpr = ctx->thread_id_gpr;
	cf->output.index_gpr = idx_gpr;
	cf->output.comp_mask = 0xf;
	cf->output.burst_count = 1;
	cf->vpm = 1;
	cf->barrier = 1;
	cf->mark = 1;
	cf->output.elem_size = 0;
	r600_bytecode_add_cfinst(ctx->bc, CF_OP_WAIT_ACK);
	cf = ctx->bc->cf_last;
	cf->barrier = 1;
	cf->cf_addr = 1;

	memset(&vtx, 0, sizeof(struct r600_bytecode_vtx));
	if (inst->Src[0].Register.File == TGSI_FILE_IMAGE) {
		desc = util_format_description(inst->Memory.Format);
		r600_vertex_data_type(inst->Memory.Format,
				      &format, &num_format, &format_comp, &endian);
		vtx.dst_sel_x = desc->swizzle[0];
	} else {
		format = FMT_32;
		num_format = 1;
		format_comp = 0;
		endian = 0;
		vtx.dst_sel_x = 0;
	}
	vtx.op = FETCH_OP_VFETCH;
	vtx.buffer_id = immed_base + inst->Src[0].Register.Index;
	vtx.buffer_index_mode = rat_index_mode;
	vtx.fetch_type = SQ_VTX_FETCH_NO_INDEX_OFFSET;
	vtx.src_gpr = ctx->thread_id_gpr;
	vtx.src_sel_x = 1;
	vtx.dst_gpr = ctx->file_offset[inst->Dst[0].Register.File] + inst->Dst[0].Register.Index;
	vtx.dst_sel_y = 7;
	vtx.dst_sel_z = 7;
	vtx.dst_sel_w = 7;
	vtx.use_const_fields = 0;
	vtx.srf_mode_all = 1;
	vtx.data_format = format;
	vtx.num_format_all = num_format;
	vtx.format_comp_all = format_comp;
	vtx.endian = endian;
	vtx.offset = 0;
	vtx.mega_fetch_count = 0xf;
	r = r600_bytecode_add_vtx_tc(ctx->bc, &vtx);
	if (r)
		return r;
	cf = ctx->bc->cf_last;
	cf->vpm = 1;
	cf->barrier = 1;
	return 0;
}

static int get_gds_op(int opcode)
{
	switch (opcode) {
	case TGSI_OPCODE_ATOMUADD:
		return FETCH_OP_GDS_ADD_RET;
	case TGSI_OPCODE_ATOMAND:
		return FETCH_OP_GDS_AND_RET;
	case TGSI_OPCODE_ATOMOR:
		return FETCH_OP_GDS_OR_RET;
	case TGSI_OPCODE_ATOMXOR:
		return FETCH_OP_GDS_XOR_RET;
	case TGSI_OPCODE_ATOMUMIN:
		return FETCH_OP_GDS_MIN_UINT_RET;
	case TGSI_OPCODE_ATOMUMAX:
		return FETCH_OP_GDS_MAX_UINT_RET;
	case TGSI_OPCODE_ATOMXCHG:
		return FETCH_OP_GDS_XCHG_RET;
	case TGSI_OPCODE_ATOMCAS:
		return FETCH_OP_GDS_CMP_XCHG_RET;
	default:
		return -1;
	}
}

static int tgsi_atomic_op_gds(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_gds gds;
	struct r600_bytecode_alu alu;
	int gds_op = get_gds_op(inst->Instruction.Opcode);
	int r;
	int uav_id = 0;
	int uav_index_mode = 0;
	bool is_cm = (ctx->bc->chip_class == CAYMAN);

	if (gds_op == -1) {
		fprintf(stderr, "unknown GDS op for opcode %d\n", inst->Instruction.Opcode);
		return -1;
	}

	r = tgsi_set_gds_temp(ctx, &uav_id, &uav_index_mode);
	if (r)
		return r;

	if (gds_op == FETCH_OP_GDS_CMP_XCHG_RET) {
		if (inst->Src[3].Register.File == TGSI_FILE_IMMEDIATE) {
			int value = (ctx->literals[4 * inst->Src[3].Register.Index + inst->Src[3].Register.SwizzleX]);
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_MOV;
			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = is_cm ? 2 : 1;
			alu.src[0].sel = V_SQ_ALU_SRC_LITERAL;
			alu.src[0].value = value;
			alu.last = 1;
			alu.dst.write = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		} else {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_MOV;
			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = is_cm ? 2 : 1;
			r600_bytecode_src(&alu.src[0], &ctx->src[3], 0);
			alu.last = 1;
			alu.dst.write = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
	}
	if (inst->Src[2].Register.File == TGSI_FILE_IMMEDIATE) {
		int value = (ctx->literals[4 * inst->Src[2].Register.Index + inst->Src[2].Register.SwizzleX]);
		int abs_value = abs(value);
		if (abs_value != value && gds_op == FETCH_OP_GDS_ADD_RET)
			gds_op = FETCH_OP_GDS_SUB_RET;
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = is_cm ? 1 : 0;
		alu.src[0].sel = V_SQ_ALU_SRC_LITERAL;
		alu.src[0].value = abs_value;
		alu.last = 1;
		alu.dst.write = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	} else {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;
		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = is_cm ? 1 : 0;
		r600_bytecode_src(&alu.src[0], &ctx->src[2], 0);
		alu.last = 1;
		alu.dst.write = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}


	memset(&gds, 0, sizeof(struct r600_bytecode_gds));
	gds.op = gds_op;
	gds.dst_gpr = ctx->file_offset[inst->Dst[0].Register.File] + inst->Dst[0].Register.Index;
	gds.uav_id = is_cm ? 0 : uav_id;
	gds.uav_index_mode = is_cm ? 0 : uav_index_mode;
	gds.src_gpr = ctx->temp_reg;
	gds.src_gpr2 = 0;
	gds.src_sel_x = is_cm ? 0 : 4;
	gds.src_sel_y = is_cm ? 1 : 0;
	if (gds_op == FETCH_OP_GDS_CMP_XCHG_RET)
		gds.src_sel_z = is_cm ? 2 : 1;
	else
		gds.src_sel_z = 7;
	gds.dst_sel_x = 0;
	gds.dst_sel_y = 7;
	gds.dst_sel_z = 7;
	gds.dst_sel_w = 7;
	gds.alloc_consume = !is_cm;

	r = r600_bytecode_add_gds(ctx->bc, &gds);
	if (r)
		return r;
	ctx->bc->cf_last->vpm = 1;
	return 0;
}

static int get_lds_op(int opcode)
{
	switch (opcode) {
	case TGSI_OPCODE_ATOMUADD:
		return LDS_OP2_LDS_ADD_RET;
	case TGSI_OPCODE_ATOMAND:
		return LDS_OP2_LDS_AND_RET;
	case TGSI_OPCODE_ATOMOR:
		return LDS_OP2_LDS_OR_RET;
	case TGSI_OPCODE_ATOMXOR:
		return LDS_OP2_LDS_XOR_RET;
	case TGSI_OPCODE_ATOMUMIN:
		return LDS_OP2_LDS_MIN_UINT_RET;
	case TGSI_OPCODE_ATOMUMAX:
		return LDS_OP2_LDS_MAX_UINT_RET;
	case TGSI_OPCODE_ATOMIMIN:
		return LDS_OP2_LDS_MIN_INT_RET;
	case TGSI_OPCODE_ATOMIMAX:
		return LDS_OP2_LDS_MAX_INT_RET;
	case TGSI_OPCODE_ATOMXCHG:
		return LDS_OP2_LDS_XCHG_RET;
	case TGSI_OPCODE_ATOMCAS:
		return LDS_OP3_LDS_CMP_XCHG_RET;
	default:
		return -1;
	}
}

static int tgsi_atomic_op_lds(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	int lds_op = get_lds_op(inst->Instruction.Opcode);
	int r;

	struct r600_bytecode_alu alu;
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = lds_op;
	alu.is_lds_idx_op = true;
	alu.last = 1;
	r600_bytecode_src(&alu.src[0], &ctx->src[1], 0);
	r600_bytecode_src(&alu.src[1], &ctx->src[2], 0);
	if (lds_op == LDS_OP3_LDS_CMP_XCHG_RET)
		r600_bytecode_src(&alu.src[2], &ctx->src[3], 0);
	else
		alu.src[2].sel = V_SQ_ALU_SRC_0;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	/* then read from LDS_OQ_A_POP */
	memset(&alu, 0, sizeof(alu));

	alu.op = ALU_OP1_MOV;
	alu.src[0].sel = EG_V_SQ_ALU_SRC_LDS_OQ_A_POP;
	alu.src[0].chan = 0;
	tgsi_dst(ctx, &inst->Dst[0], 0, &alu.dst);
	alu.dst.write = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	return 0;
}

static int tgsi_atomic_op(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	if (inst->Src[0].Register.File == TGSI_FILE_IMAGE)
		return tgsi_atomic_op_rat(ctx);
	if (inst->Src[0].Register.File == TGSI_FILE_HW_ATOMIC)
		return tgsi_atomic_op_gds(ctx);
	if (inst->Src[0].Register.File == TGSI_FILE_BUFFER)
		return tgsi_atomic_op_rat(ctx);
	if (inst->Src[0].Register.File == TGSI_FILE_MEMORY)
		return tgsi_atomic_op_lds(ctx);
	return 0;
}

static int tgsi_resq(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	unsigned sampler_index_mode;
	struct r600_bytecode_tex tex;
	int r;
	boolean has_txq_cube_array_z = false;

	if (inst->Src[0].Register.File == TGSI_FILE_BUFFER ||
	    (inst->Src[0].Register.File == TGSI_FILE_IMAGE && inst->Memory.Texture == TGSI_TEXTURE_BUFFER)) {
		if (ctx->bc->chip_class < EVERGREEN)
			ctx->shader->uses_tex_buffers = true;
		unsigned eg_buffer_base = 0;
		eg_buffer_base = R600_IMAGE_REAL_RESOURCE_OFFSET;
		if (inst->Src[0].Register.File == TGSI_FILE_BUFFER)
			eg_buffer_base += ctx->info.file_count[TGSI_FILE_IMAGE];
		return r600_do_buffer_txq(ctx, 0, ctx->shader->image_size_const_offset, eg_buffer_base);
	}

	if (inst->Memory.Texture == TGSI_TEXTURE_CUBE_ARRAY &&
	    inst->Dst[0].Register.WriteMask & 4) {
		ctx->shader->has_txq_cube_array_z_comp = true;
		has_txq_cube_array_z = true;
	}

	sampler_index_mode = inst->Src[0].Indirect.Index == 2 ? 2 : 0; // CF_INDEX_1 : CF_INDEX_NONE
	if (sampler_index_mode)
		egcm_load_index_reg(ctx->bc, 1, false);


	/* does this shader want a num layers from TXQ for a cube array? */
	if (has_txq_cube_array_z) {
		int id = tgsi_tex_get_src_gpr(ctx, 0) + ctx->shader->image_size_const_offset;
		struct r600_bytecode_alu alu;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP1_MOV;

		alu.src[0].sel = R600_SHADER_BUFFER_INFO_SEL;
		/* with eg each dword is either number of cubes */
		alu.src[0].sel += id / 4;
		alu.src[0].chan = id % 4;
		alu.src[0].kc_bank = R600_BUFFER_INFO_CONST_BUFFER;
		tgsi_dst(ctx, &inst->Dst[0], 2, &alu.dst);
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
		/* disable writemask from texture instruction */
		inst->Dst[0].Register.WriteMask &= ~4;
	}
	memset(&tex, 0, sizeof(struct r600_bytecode_tex));
	tex.op = ctx->inst_info->op;
	tex.sampler_id = R600_IMAGE_REAL_RESOURCE_OFFSET + inst->Src[0].Register.Index;
	tex.sampler_index_mode = sampler_index_mode;
	tex.resource_id = tex.sampler_id;
	tex.resource_index_mode = sampler_index_mode;
	tex.src_sel_x = 4;
	tex.src_sel_y = 4;
	tex.src_sel_z = 4;
	tex.src_sel_w = 4;
	tex.dst_sel_x = (inst->Dst[0].Register.WriteMask & 1) ? 0 : 7;
	tex.dst_sel_y = (inst->Dst[0].Register.WriteMask & 2) ? 1 : 7;
	tex.dst_sel_z = (inst->Dst[0].Register.WriteMask & 4) ? 2 : 7;
	tex.dst_sel_w = (inst->Dst[0].Register.WriteMask & 8) ? 3 : 7;
	tex.dst_gpr = ctx->file_offset[inst->Dst[0].Register.File] + inst->Dst[0].Register.Index;
	r = r600_bytecode_add_tex(ctx->bc, &tex);
	if (r)
		return r;

	return 0;
}

static int tgsi_lrp(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	unsigned lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);
	struct r600_bytecode_alu_src srcs[2][4];
	unsigned i;
	int r;

	/* optimize if it's just an equal balance */
	if (ctx->src[0].sel == V_SQ_ALU_SRC_0_5) {
		for (i = 0; i < lasti + 1; i++) {
			if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
				continue;

			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP2_ADD;
			r600_bytecode_src(&alu.src[0], &ctx->src[1], i);
			r600_bytecode_src(&alu.src[1], &ctx->src[2], i);
			alu.omod = 3;
			tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
			alu.dst.chan = i;
			if (i == lasti) {
				alu.last = 1;
			}
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
		return 0;
	}

	/* 1 - src0 */
	for (i = 0; i < lasti + 1; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_ADD;
		alu.src[0].sel = V_SQ_ALU_SRC_1;
		alu.src[0].chan = 0;
		r600_bytecode_src(&alu.src[1], &ctx->src[0], i);
		r600_bytecode_src_toggle_neg(&alu.src[1]);
		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = i;
		if (i == lasti) {
			alu.last = 1;
		}
		alu.dst.write = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	/* (1 - src0) * src2 */
	for (i = 0; i < lasti + 1; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_MUL;
		alu.src[0].sel = ctx->temp_reg;
		alu.src[0].chan = i;
		r600_bytecode_src(&alu.src[1], &ctx->src[2], i);
		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = i;
		if (i == lasti) {
			alu.last = 1;
		}
		alu.dst.write = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	/* src0 * src1 + (1 - src0) * src2 */

	for (i = 0; i < 2; i++) {
		r = tgsi_make_src_for_op3(ctx, inst->Dst[0].Register.WriteMask,
					  srcs[i], &ctx->src[i]);
		if (r)
			return r;
	}

	for (i = 0; i < lasti + 1; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_MULADD;
		alu.is_op3 = 1;
		alu.src[0] = srcs[0][i];
		alu.src[1] = srcs[1][i];
		alu.src[2].sel = ctx->temp_reg;
		alu.src[2].chan = i;

		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.dst.chan = i;
		if (i == lasti) {
			alu.last = 1;
		}
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_cmp(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r, j;
	int lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);
	struct r600_bytecode_alu_src srcs[3][4];

	unsigned op;

	if (ctx->src[0].abs && ctx->src[0].neg) {
		op = ALU_OP3_CNDE;
		ctx->src[0].abs = 0;
		ctx->src[0].neg = 0;
	} else {
		op = ALU_OP3_CNDGE;
	}

	for (j = 0; j < inst->Instruction.NumSrcRegs; j++) {
		r = tgsi_make_src_for_op3(ctx, inst->Dst[0].Register.WriteMask,
					  srcs[j], &ctx->src[j]);
		if (r)
			return r;
	}

	for (i = 0; i < lasti + 1; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = op;
		alu.src[0] = srcs[0][i];
		alu.src[1] = srcs[2][i];
		alu.src[2] = srcs[1][i];

		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.dst.chan = i;
		alu.dst.write = 1;
		alu.is_op3 = 1;
		if (i == lasti)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_ucmp(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r;
	int lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);

	for (i = 0; i < lasti + 1; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_CNDE_INT;
		r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
		r600_bytecode_src(&alu.src[1], &ctx->src[2], i);
		r600_bytecode_src(&alu.src[2], &ctx->src[1], i);
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.dst.chan = i;
		alu.dst.write = 1;
		alu.is_op3 = 1;
		if (i == lasti)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_exp(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int r;
	unsigned i;

	/* result.x = 2^floor(src); */
	if (inst->Dst[0].Register.WriteMask & 1) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));

		alu.op = ALU_OP1_FLOOR;
		r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);

		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = 0;
		alu.dst.write = 1;
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;

		if (ctx->bc->chip_class == CAYMAN) {
			for (i = 0; i < 3; i++) {
				alu.op = ALU_OP1_EXP_IEEE;
				alu.src[0].sel = ctx->temp_reg;
				alu.src[0].chan = 0;

				alu.dst.sel = ctx->temp_reg;
				alu.dst.chan = i;
				alu.dst.write = i == 0;
				alu.last = i == 2;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}
		} else {
			alu.op = ALU_OP1_EXP_IEEE;
			alu.src[0].sel = ctx->temp_reg;
			alu.src[0].chan = 0;

			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = 0;
			alu.dst.write = 1;
			alu.last = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
	}

	/* result.y = tmp - floor(tmp); */
	if ((inst->Dst[0].Register.WriteMask >> 1) & 1) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));

		alu.op = ALU_OP1_FRACT;
		r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);

		alu.dst.sel = ctx->temp_reg;
#if 0
		r = tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		if (r)
			return r;
#endif
		alu.dst.write = 1;
		alu.dst.chan = 1;

		alu.last = 1;

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	/* result.z = RoughApprox2ToX(tmp);*/
	if ((inst->Dst[0].Register.WriteMask >> 2) & 0x1) {
		if (ctx->bc->chip_class == CAYMAN) {
			for (i = 0; i < 3; i++) {
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_EXP_IEEE;
				r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);

				alu.dst.sel = ctx->temp_reg;
				alu.dst.chan = i;
				if (i == 2) {
					alu.dst.write = 1;
					alu.last = 1;
				}

				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}
		} else {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_EXP_IEEE;
			r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);

			alu.dst.sel = ctx->temp_reg;
			alu.dst.write = 1;
			alu.dst.chan = 2;

			alu.last = 1;

			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
	}

	/* result.w = 1.0;*/
	if ((inst->Dst[0].Register.WriteMask >> 3) & 0x1) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));

		alu.op = ALU_OP1_MOV;
		alu.src[0].sel = V_SQ_ALU_SRC_1;
		alu.src[0].chan = 0;

		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = 3;
		alu.dst.write = 1;
		alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return tgsi_helper_copy(ctx, inst);
}

static int tgsi_log(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int r;
	unsigned i;

	/* result.x = floor(log2(|src|)); */
	if (inst->Dst[0].Register.WriteMask & 1) {
		if (ctx->bc->chip_class == CAYMAN) {
			for (i = 0; i < 3; i++) {
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));

				alu.op = ALU_OP1_LOG_IEEE;
				r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
				r600_bytecode_src_set_abs(&alu.src[0]);

				alu.dst.sel = ctx->temp_reg;
				alu.dst.chan = i;
				if (i == 0)
					alu.dst.write = 1;
				if (i == 2)
					alu.last = 1;
				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}

		} else {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));

			alu.op = ALU_OP1_LOG_IEEE;
			r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
			r600_bytecode_src_set_abs(&alu.src[0]);

			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = 0;
			alu.dst.write = 1;
			alu.last = 1;
			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}

		alu.op = ALU_OP1_FLOOR;
		alu.src[0].sel = ctx->temp_reg;
		alu.src[0].chan = 0;

		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = 0;
		alu.dst.write = 1;
		alu.last = 1;

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	/* result.y = |src.x| / (2 ^ floor(log2(|src.x|))); */
	if ((inst->Dst[0].Register.WriteMask >> 1) & 1) {

		if (ctx->bc->chip_class == CAYMAN) {
			for (i = 0; i < 3; i++) {
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));

				alu.op = ALU_OP1_LOG_IEEE;
				r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
				r600_bytecode_src_set_abs(&alu.src[0]);

				alu.dst.sel = ctx->temp_reg;
				alu.dst.chan = i;
				if (i == 1)
					alu.dst.write = 1;
				if (i == 2)
					alu.last = 1;

				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}
		} else {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));

			alu.op = ALU_OP1_LOG_IEEE;
			r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
			r600_bytecode_src_set_abs(&alu.src[0]);

			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = 1;
			alu.dst.write = 1;
			alu.last = 1;

			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));

		alu.op = ALU_OP1_FLOOR;
		alu.src[0].sel = ctx->temp_reg;
		alu.src[0].chan = 1;

		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = 1;
		alu.dst.write = 1;
		alu.last = 1;

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;

		if (ctx->bc->chip_class == CAYMAN) {
			for (i = 0; i < 3; i++) {
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_EXP_IEEE;
				alu.src[0].sel = ctx->temp_reg;
				alu.src[0].chan = 1;

				alu.dst.sel = ctx->temp_reg;
				alu.dst.chan = i;
				if (i == 1)
					alu.dst.write = 1;
				if (i == 2)
					alu.last = 1;

				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}
		} else {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_EXP_IEEE;
			alu.src[0].sel = ctx->temp_reg;
			alu.src[0].chan = 1;

			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = 1;
			alu.dst.write = 1;
			alu.last = 1;

			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}

		if (ctx->bc->chip_class == CAYMAN) {
			for (i = 0; i < 3; i++) {
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));
				alu.op = ALU_OP1_RECIP_IEEE;
				alu.src[0].sel = ctx->temp_reg;
				alu.src[0].chan = 1;

				alu.dst.sel = ctx->temp_reg;
				alu.dst.chan = i;
				if (i == 1)
					alu.dst.write = 1;
				if (i == 2)
					alu.last = 1;

				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}
		} else {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));
			alu.op = ALU_OP1_RECIP_IEEE;
			alu.src[0].sel = ctx->temp_reg;
			alu.src[0].chan = 1;

			alu.dst.sel = ctx->temp_reg;
			alu.dst.chan = 1;
			alu.dst.write = 1;
			alu.last = 1;

			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));

		alu.op = ALU_OP2_MUL;

		r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
		r600_bytecode_src_set_abs(&alu.src[0]);

		alu.src[1].sel = ctx->temp_reg;
		alu.src[1].chan = 1;

		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = 1;
		alu.dst.write = 1;
		alu.last = 1;

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	/* result.z = log2(|src|);*/
	if ((inst->Dst[0].Register.WriteMask >> 2) & 1) {
		if (ctx->bc->chip_class == CAYMAN) {
			for (i = 0; i < 3; i++) {
				memset(&alu, 0, sizeof(struct r600_bytecode_alu));

				alu.op = ALU_OP1_LOG_IEEE;
				r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
				r600_bytecode_src_set_abs(&alu.src[0]);

				alu.dst.sel = ctx->temp_reg;
				if (i == 2)
					alu.dst.write = 1;
				alu.dst.chan = i;
				if (i == 2)
					alu.last = 1;

				r = r600_bytecode_add_alu(ctx->bc, &alu);
				if (r)
					return r;
			}
		} else {
			memset(&alu, 0, sizeof(struct r600_bytecode_alu));

			alu.op = ALU_OP1_LOG_IEEE;
			r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
			r600_bytecode_src_set_abs(&alu.src[0]);

			alu.dst.sel = ctx->temp_reg;
			alu.dst.write = 1;
			alu.dst.chan = 2;
			alu.last = 1;

			r = r600_bytecode_add_alu(ctx->bc, &alu);
			if (r)
				return r;
		}
	}

	/* result.w = 1.0; */
	if ((inst->Dst[0].Register.WriteMask >> 3) & 1) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));

		alu.op = ALU_OP1_MOV;
		alu.src[0].sel = V_SQ_ALU_SRC_1;
		alu.src[0].chan = 0;

		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = 3;
		alu.dst.write = 1;
		alu.last = 1;

		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	return tgsi_helper_copy(ctx, inst);
}

static int tgsi_eg_arl(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int r;
	int i, lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);
	unsigned reg = get_address_file_reg(ctx, inst->Dst[0].Register.Index);

	assert(inst->Dst[0].Register.Index < 3);
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));

	switch (inst->Instruction.Opcode) {
	case TGSI_OPCODE_ARL:
		alu.op = ALU_OP1_FLT_TO_INT_FLOOR;
		break;
	case TGSI_OPCODE_ARR:
		alu.op = ALU_OP1_FLT_TO_INT;
		break;
	case TGSI_OPCODE_UARL:
		alu.op = ALU_OP1_MOV;
		break;
	default:
		assert(0);
		return -1;
	}

	for (i = 0; i <= lasti; ++i) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;
		r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
		alu.last = i == lasti;
		alu.dst.sel = reg;
	        alu.dst.chan = i;
		alu.dst.write = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	if (inst->Dst[0].Register.Index > 0)
		ctx->bc->index_loaded[inst->Dst[0].Register.Index - 1] = 0;
	else
		ctx->bc->ar_loaded = 0;

	return 0;
}
static int tgsi_r600_arl(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int r;
	int i, lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);

	switch (inst->Instruction.Opcode) {
	case TGSI_OPCODE_ARL:
		memset(&alu, 0, sizeof(alu));
		alu.op = ALU_OP1_FLOOR;
		alu.dst.sel = ctx->bc->ar_reg;
		alu.dst.write = 1;
		for (i = 0; i <= lasti; ++i) {
			if (inst->Dst[0].Register.WriteMask & (1 << i))  {
				alu.dst.chan = i;
				r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
				alu.last = i == lasti;
				if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
					return r;
			}
		}

		memset(&alu, 0, sizeof(alu));
		alu.op = ALU_OP1_FLT_TO_INT;
		alu.src[0].sel = ctx->bc->ar_reg;
		alu.dst.sel = ctx->bc->ar_reg;
		alu.dst.write = 1;
		/* FLT_TO_INT is trans-only on r600/r700 */
		alu.last = TRUE;
		for (i = 0; i <= lasti; ++i) {
			alu.dst.chan = i;
			alu.src[0].chan = i;
			if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
				return r;
		}
		break;
	case TGSI_OPCODE_ARR:
		memset(&alu, 0, sizeof(alu));
		alu.op = ALU_OP1_FLT_TO_INT;
		alu.dst.sel = ctx->bc->ar_reg;
		alu.dst.write = 1;
		/* FLT_TO_INT is trans-only on r600/r700 */
		alu.last = TRUE;
		for (i = 0; i <= lasti; ++i) {
			if (inst->Dst[0].Register.WriteMask & (1 << i)) {
				alu.dst.chan = i;
				r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
				if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
					return r;
			}
		}
		break;
	case TGSI_OPCODE_UARL:
		memset(&alu, 0, sizeof(alu));
		alu.op = ALU_OP1_MOV;
		alu.dst.sel = ctx->bc->ar_reg;
		alu.dst.write = 1;
		for (i = 0; i <= lasti; ++i) {
			if (inst->Dst[0].Register.WriteMask & (1 << i)) {
				alu.dst.chan = i;
				r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
				alu.last = i == lasti;
				if ((r = r600_bytecode_add_alu(ctx->bc, &alu)))
					return r;
			}
		}
		break;
	default:
		assert(0);
		return -1;
	}

	ctx->bc->ar_loaded = 0;
	return 0;
}

static int tgsi_opdst(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, r = 0;

	for (i = 0; i < 4; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));

		alu.op = ALU_OP2_MUL;
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);

		if (i == 0 || i == 3) {
			alu.src[0].sel = V_SQ_ALU_SRC_1;
		} else {
			r600_bytecode_src(&alu.src[0], &ctx->src[0], i);
		}

		if (i == 0 || i == 2) {
			alu.src[1].sel = V_SQ_ALU_SRC_1;
		} else {
			r600_bytecode_src(&alu.src[1], &ctx->src[1], i);
		}
		if (i == 3)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int emit_logic_pred(struct r600_shader_ctx *ctx, int opcode, int alu_type,
			   struct r600_bytecode_alu_src *src)
{
	struct r600_bytecode_alu alu;
	int r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = opcode;
	alu.execute_mask = 1;
	alu.update_pred = 1;

	alu.dst.sel = ctx->temp_reg;
	alu.dst.write = 1;
	alu.dst.chan = 0;

	alu.src[0] = *src;
	alu.src[1].sel = V_SQ_ALU_SRC_0;
	alu.src[1].chan = 0;

	alu.last = 1;

	r = r600_bytecode_add_alu_type(ctx->bc, &alu, alu_type);
	if (r)
		return r;
	return 0;
}

static int pops(struct r600_shader_ctx *ctx, int pops)
{
	unsigned force_pop = ctx->bc->force_add_cf;

	if (!force_pop) {
		int alu_pop = 3;
		if (ctx->bc->cf_last) {
			if (ctx->bc->cf_last->op == CF_OP_ALU)
				alu_pop = 0;
			else if (ctx->bc->cf_last->op == CF_OP_ALU_POP_AFTER)
				alu_pop = 1;
		}
		alu_pop += pops;
		if (alu_pop == 1) {
			ctx->bc->cf_last->op = CF_OP_ALU_POP_AFTER;
			ctx->bc->force_add_cf = 1;
		} else if (alu_pop == 2) {
			ctx->bc->cf_last->op = CF_OP_ALU_POP2_AFTER;
			ctx->bc->force_add_cf = 1;
		} else {
			force_pop = 1;
		}
	}

	if (force_pop) {
		r600_bytecode_add_cfinst(ctx->bc, CF_OP_POP);
		ctx->bc->cf_last->pop_count = pops;
		ctx->bc->cf_last->cf_addr = ctx->bc->cf_last->id + 2;
	}

	return 0;
}

static inline int callstack_update_max_depth(struct r600_shader_ctx *ctx,
                                              unsigned reason)
{
	struct r600_stack_info *stack = &ctx->bc->stack;
	unsigned elements;
	int entries;

	unsigned entry_size = stack->entry_size;

	elements = (stack->loop + stack->push_wqm ) * entry_size;
	elements += stack->push;

	switch (ctx->bc->chip_class) {
	case R600:
	case R700:
		/* pre-r8xx: if any non-WQM PUSH instruction is invoked, 2 elements on
		 * the stack must be reserved to hold the current active/continue
		 * masks */
		if (reason == FC_PUSH_VPM || stack->push > 0) {
			elements += 2;
		}
		break;

	case CAYMAN:
		/* r9xx: any stack operation on empty stack consumes 2 additional
		 * elements */
		elements += 2;

		/* fallthrough */
		/* FIXME: do the two elements added above cover the cases for the
		 * r8xx+ below? */

	case EVERGREEN:
		/* r8xx+: 2 extra elements are not always required, but one extra
		 * element must be added for each of the following cases:
		 * 1. There is an ALU_ELSE_AFTER instruction at the point of greatest
		 *    stack usage.
		 *    (Currently we don't use ALU_ELSE_AFTER.)
		 * 2. There are LOOP/WQM frames on the stack when any flavor of non-WQM
		 *    PUSH instruction executed.
		 *
		 *    NOTE: it seems we also need to reserve additional element in some
		 *    other cases, e.g. when we have 4 levels of PUSH_VPM in the shader,
		 *    then STACK_SIZE should be 2 instead of 1 */
		if (reason == FC_PUSH_VPM || stack->push > 0) {
			elements += 1;
		}
		break;

	default:
		assert(0);
		break;
	}

	/* NOTE: it seems STACK_SIZE is interpreted by hw as if entry_size is 4
	 * for all chips, so we use 4 in the final formula, not the real entry_size
	 * for the chip */
	entry_size = 4;

	entries = (elements + (entry_size - 1)) / entry_size;

	if (entries > stack->max_entries)
		stack->max_entries = entries;
	return elements;
}

static inline void callstack_pop(struct r600_shader_ctx *ctx, unsigned reason)
{
	switch(reason) {
	case FC_PUSH_VPM:
		--ctx->bc->stack.push;
		assert(ctx->bc->stack.push >= 0);
		break;
	case FC_PUSH_WQM:
		--ctx->bc->stack.push_wqm;
		assert(ctx->bc->stack.push_wqm >= 0);
		break;
	case FC_LOOP:
		--ctx->bc->stack.loop;
		assert(ctx->bc->stack.loop >= 0);
		break;
	default:
		assert(0);
		break;
	}
}

static inline int callstack_push(struct r600_shader_ctx *ctx, unsigned reason)
{
	switch (reason) {
	case FC_PUSH_VPM:
		++ctx->bc->stack.push;
		break;
	case FC_PUSH_WQM:
		++ctx->bc->stack.push_wqm;
		break;
	case FC_LOOP:
		++ctx->bc->stack.loop;
		break;
	default:
		assert(0);
	}

	return callstack_update_max_depth(ctx, reason);
}

static void fc_set_mid(struct r600_shader_ctx *ctx, int fc_sp)
{
	struct r600_cf_stack_entry *sp = &ctx->bc->fc_stack[fc_sp];

	sp->mid = realloc((void *)sp->mid,
						sizeof(struct r600_bytecode_cf *) * (sp->num_mid + 1));
	sp->mid[sp->num_mid] = ctx->bc->cf_last;
	sp->num_mid++;
}

static void fc_pushlevel(struct r600_shader_ctx *ctx, int type)
{
	assert(ctx->bc->fc_sp < ARRAY_SIZE(ctx->bc->fc_stack));
	ctx->bc->fc_stack[ctx->bc->fc_sp].type = type;
	ctx->bc->fc_stack[ctx->bc->fc_sp].start = ctx->bc->cf_last;
	ctx->bc->fc_sp++;
}

static void fc_poplevel(struct r600_shader_ctx *ctx)
{
	struct r600_cf_stack_entry *sp = &ctx->bc->fc_stack[ctx->bc->fc_sp - 1];
	free(sp->mid);
	sp->mid = NULL;
	sp->num_mid = 0;
	sp->start = NULL;
	sp->type = 0;
	ctx->bc->fc_sp--;
}

#if 0
static int emit_return(struct r600_shader_ctx *ctx)
{
	r600_bytecode_add_cfinst(ctx->bc, CF_OP_RETURN));
	return 0;
}

static int emit_jump_to_offset(struct r600_shader_ctx *ctx, int pops, int offset)
{

	r600_bytecode_add_cfinst(ctx->bc, CF_OP_JUMP));
	ctx->bc->cf_last->pop_count = pops;
	/* XXX work out offset */
	return 0;
}

static int emit_setret_in_loop_flag(struct r600_shader_ctx *ctx, unsigned flag_value)
{
	return 0;
}

static void emit_testflag(struct r600_shader_ctx *ctx)
{

}

static void emit_return_on_flag(struct r600_shader_ctx *ctx, unsigned ifidx)
{
	emit_testflag(ctx);
	emit_jump_to_offset(ctx, 1, 4);
	emit_setret_in_loop_flag(ctx, V_SQ_ALU_SRC_0);
	pops(ctx, ifidx + 1);
	emit_return(ctx);
}

static void break_loop_on_flag(struct r600_shader_ctx *ctx, unsigned fc_sp)
{
	emit_testflag(ctx);

	r600_bytecode_add_cfinst(ctx->bc, ctx->inst_info->op);
	ctx->bc->cf_last->pop_count = 1;

	fc_set_mid(ctx, fc_sp);

	pops(ctx, 1);
}
#endif

static int emit_if(struct r600_shader_ctx *ctx, int opcode,
		   struct r600_bytecode_alu_src *src)
{
	int alu_type = CF_OP_ALU_PUSH_BEFORE;
	bool needs_workaround = false;
	int elems = callstack_push(ctx, FC_PUSH_VPM);

	if (ctx->bc->chip_class == CAYMAN && ctx->bc->stack.loop > 1)
		needs_workaround = true;

	if (ctx->bc->chip_class == EVERGREEN && ctx_needs_stack_workaround_8xx(ctx)) {
		unsigned dmod1 = (elems - 1) % ctx->bc->stack.entry_size;
		unsigned dmod2 = (elems) % ctx->bc->stack.entry_size;

		if (elems && (!dmod1 || !dmod2))
			needs_workaround = true;
	}

	/* There is a hardware bug on Cayman where a BREAK/CONTINUE followed by
	 * LOOP_STARTxxx for nested loops may put the branch stack into a state
	 * such that ALU_PUSH_BEFORE doesn't work as expected. Workaround this
	 * by replacing the ALU_PUSH_BEFORE with a PUSH + ALU */
	if (needs_workaround) {
		r600_bytecode_add_cfinst(ctx->bc, CF_OP_PUSH);
		ctx->bc->cf_last->cf_addr = ctx->bc->cf_last->id + 2;
		alu_type = CF_OP_ALU;
	}

	emit_logic_pred(ctx, opcode, alu_type, src);

	r600_bytecode_add_cfinst(ctx->bc, CF_OP_JUMP);

	fc_pushlevel(ctx, FC_IF);

	return 0;
}

static int tgsi_if(struct r600_shader_ctx *ctx)
{
	struct r600_bytecode_alu_src alu_src;
	r600_bytecode_src(&alu_src, &ctx->src[0], 0);

	return emit_if(ctx, ALU_OP2_PRED_SETNE, &alu_src);
}

static int tgsi_uif(struct r600_shader_ctx *ctx)
{
	struct r600_bytecode_alu_src alu_src;
	r600_bytecode_src(&alu_src, &ctx->src[0], 0);
	return emit_if(ctx, ALU_OP2_PRED_SETNE_INT, &alu_src);
}

static int tgsi_else(struct r600_shader_ctx *ctx)
{
	r600_bytecode_add_cfinst(ctx->bc, CF_OP_ELSE);
	ctx->bc->cf_last->pop_count = 1;

	fc_set_mid(ctx, ctx->bc->fc_sp - 1);
	ctx->bc->fc_stack[ctx->bc->fc_sp - 1].start->cf_addr = ctx->bc->cf_last->id;
	return 0;
}

static int tgsi_endif(struct r600_shader_ctx *ctx)
{
	int offset = 2;
	pops(ctx, 1);
	if (ctx->bc->fc_stack[ctx->bc->fc_sp - 1].type != FC_IF) {
		R600_ERR("if/endif unbalanced in shader\n");
		return -1;
	}

	/* ALU_EXTENDED needs 4 DWords instead of two, adjust jump target offset accordingly */
	if (ctx->bc->cf_last->eg_alu_extended)
			offset += 2;

	if (ctx->bc->fc_stack[ctx->bc->fc_sp - 1].mid == NULL) {
		ctx->bc->fc_stack[ctx->bc->fc_sp - 1].start->cf_addr = ctx->bc->cf_last->id + offset;
		ctx->bc->fc_stack[ctx->bc->fc_sp - 1].start->pop_count = 1;
	} else {
		ctx->bc->fc_stack[ctx->bc->fc_sp - 1].mid[0]->cf_addr = ctx->bc->cf_last->id + offset;
	}
	fc_poplevel(ctx);

	callstack_pop(ctx, FC_PUSH_VPM);
	return 0;
}

static int tgsi_bgnloop(struct r600_shader_ctx *ctx)
{
	/* LOOP_START_DX10 ignores the LOOP_CONFIG* registers, so it is not
	 * limited to 4096 iterations, like the other LOOP_* instructions. */
	r600_bytecode_add_cfinst(ctx->bc, CF_OP_LOOP_START_DX10);

	fc_pushlevel(ctx, FC_LOOP);

	/* check stack depth */
	callstack_push(ctx, FC_LOOP);
	return 0;
}

static int tgsi_endloop(struct r600_shader_ctx *ctx)
{
	int i;

	r600_bytecode_add_cfinst(ctx->bc, CF_OP_LOOP_END);

	if (ctx->bc->fc_stack[ctx->bc->fc_sp - 1].type != FC_LOOP) {
		R600_ERR("loop/endloop in shader code are not paired.\n");
		return -EINVAL;
	}

	/* fixup loop pointers - from r600isa
	   LOOP END points to CF after LOOP START,
	   LOOP START point to CF after LOOP END
	   BRK/CONT point to LOOP END CF
	*/
	ctx->bc->cf_last->cf_addr = ctx->bc->fc_stack[ctx->bc->fc_sp - 1].start->id + 2;

	ctx->bc->fc_stack[ctx->bc->fc_sp - 1].start->cf_addr = ctx->bc->cf_last->id + 2;

	for (i = 0; i < ctx->bc->fc_stack[ctx->bc->fc_sp - 1].num_mid; i++) {
		ctx->bc->fc_stack[ctx->bc->fc_sp - 1].mid[i]->cf_addr = ctx->bc->cf_last->id;
	}
	/* XXX add LOOPRET support */
	fc_poplevel(ctx);
	callstack_pop(ctx, FC_LOOP);
	return 0;
}

static int tgsi_loop_brk_cont(struct r600_shader_ctx *ctx)
{
	unsigned int fscp;

	for (fscp = ctx->bc->fc_sp; fscp > 0; fscp--)
	{
		if (FC_LOOP == ctx->bc->fc_stack[fscp - 1].type)
			break;
	}

	if (fscp == 0) {
		R600_ERR("Break not inside loop/endloop pair\n");
		return -EINVAL;
	}

	r600_bytecode_add_cfinst(ctx->bc, ctx->inst_info->op);

	fc_set_mid(ctx, fscp - 1);

	return 0;
}

static int tgsi_gs_emit(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	int stream = ctx->literals[inst->Src[0].Register.Index * 4 + inst->Src[0].Register.SwizzleX];
	int r;

	if (ctx->inst_info->op == CF_OP_EMIT_VERTEX)
		emit_gs_ring_writes(ctx, ctx->gs_stream_output_info, stream, TRUE);

	r = r600_bytecode_add_cfinst(ctx->bc, ctx->inst_info->op);
	if (!r) {
		ctx->bc->cf_last->count = stream; // Count field for CUT/EMIT_VERTEX indicates which stream
		if (ctx->inst_info->op == CF_OP_EMIT_VERTEX)
			return emit_inc_ring_offset(ctx, stream, TRUE);
	}
	return r;
}

static int tgsi_umad(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int i, j, r;
	int lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);

	/* src0 * src1 */
	for (i = 0; i < lasti + 1; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));

		alu.dst.chan = i;
		alu.dst.sel = ctx->temp_reg;
		alu.dst.write = 1;

		alu.op = ALU_OP2_MULLO_UINT;
		for (j = 0; j < 2; j++) {
			r600_bytecode_src(&alu.src[j], &ctx->src[j], i);
		}

		alu.last = 1;
		r = emit_mul_int_op(ctx->bc, &alu);
		if (r)
			return r;
	}


	for (i = 0; i < lasti + 1; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);

		alu.op = ALU_OP2_ADD_INT;

		alu.src[0].sel = ctx->temp_reg;
		alu.src[0].chan = i;

		r600_bytecode_src(&alu.src[1], &ctx->src[2], i);
		if (i == lasti) {
			alu.last = 1;
		}
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}
	return 0;
}

static int tgsi_pk2h(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int r, i;
	int lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);

	/* temp.xy = f32_to_f16(src) */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_FLT32_TO_FLT16;
	alu.dst.chan = 0;
	alu.dst.sel = ctx->temp_reg;
	alu.dst.write = 1;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	alu.dst.chan = 1;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 1);
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	/* dst.x = temp.y * 0x10000 + temp.x */
	for (i = 0; i < lasti + 1; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;

		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_MULADD_UINT24;
		alu.is_op3 = 1;
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.last = i == lasti;
		alu.src[0].sel = ctx->temp_reg;
		alu.src[0].chan = 1;
		alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
		alu.src[1].value = 0x10000;
		alu.src[2].sel = ctx->temp_reg;
		alu.src[2].chan = 0;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	return 0;
}

static int tgsi_up2h(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int r, i;
	int lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);

	/* temp.x = src.x */
	/* note: no need to mask out the high bits */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	alu.dst.chan = 0;
	alu.dst.sel = ctx->temp_reg;
	alu.dst.write = 1;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	/* temp.y = src.x >> 16 */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP2_LSHR_INT;
	alu.dst.chan = 1;
	alu.dst.sel = ctx->temp_reg;
	alu.dst.write = 1;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
	alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
	alu.src[1].value = 16;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	/* dst.wz = dst.xy = f16_to_f32(temp.xy) */
	for (i = 0; i < lasti + 1; i++) {
		if (!(inst->Dst[0].Register.WriteMask & (1 << i)))
			continue;
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		alu.op = ALU_OP1_FLT16_TO_FLT32;
		alu.src[0].sel = ctx->temp_reg;
		alu.src[0].chan = i % 2;
		alu.last = i == lasti;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	return 0;
}

static int tgsi_bfe(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int lasti = tgsi_last_instruction(inst->Dst[0].Register.WriteMask);
	int r, i;
	int dst = -1;

	if ((inst->Src[0].Register.File == inst->Dst[0].Register.File &&
	     inst->Src[0].Register.Index == inst->Dst[0].Register.Index) ||
	    (inst->Src[2].Register.File == inst->Dst[0].Register.File &&
	     inst->Src[2].Register.Index == inst->Dst[0].Register.Index))
		dst = r600_get_temp(ctx);

	r = tgsi_op3_dst(ctx, dst);
	if (r)
		return r;

	for (i = 0; i < lasti + 1; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP2_SETGE_INT;
		r600_bytecode_src(&alu.src[0], &ctx->src[2], i);
		alu.src[1].sel = V_SQ_ALU_SRC_LITERAL;
		alu.src[1].value = 32;
		alu.dst.sel = ctx->temp_reg;
		alu.dst.chan = i;
		alu.dst.write = 1;
		if (i == lasti)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	for (i = 0; i < lasti + 1; i++) {
		memset(&alu, 0, sizeof(struct r600_bytecode_alu));
		alu.op = ALU_OP3_CNDE_INT;
		alu.is_op3 = 1;
		alu.src[0].sel = ctx->temp_reg;
		alu.src[0].chan = i;

		tgsi_dst(ctx, &inst->Dst[0], i, &alu.dst);
		if (dst != -1)
			alu.src[1].sel = dst;
		else
			alu.src[1].sel = alu.dst.sel;
		alu.src[1].chan = i;
		r600_bytecode_src(&alu.src[2], &ctx->src[0], i);
		alu.dst.write = 1;
		if (i == lasti)
			alu.last = 1;
		r = r600_bytecode_add_alu(ctx->bc, &alu);
		if (r)
			return r;
	}

	return 0;
}

static int tgsi_clock(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	tgsi_dst(ctx, &inst->Dst[0], 0, &alu.dst);
	alu.src[0].sel = EG_V_SQ_ALU_SRC_TIME_LO;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	tgsi_dst(ctx, &inst->Dst[0], 1, &alu.dst);
	alu.src[0].sel = EG_V_SQ_ALU_SRC_TIME_HI;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	return 0;
}

static int emit_u64add(struct r600_shader_ctx *ctx, int op,
		       int treg,
		       int src0_sel, int src0_chan,
		       int src1_sel, int src1_chan)
{
	struct r600_bytecode_alu alu;
	int r;
	int opc;

	if (op == ALU_OP2_ADD_INT)
		opc = ALU_OP2_ADDC_UINT;
	else
		opc = ALU_OP2_SUBB_UINT;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = op;            ;
	alu.dst.sel = treg;
	alu.dst.chan = 0;
	alu.dst.write = 1;
	alu.src[0].sel = src0_sel;
	alu.src[0].chan = src0_chan + 0;
	alu.src[1].sel = src1_sel;
	alu.src[1].chan = src1_chan + 0;
	alu.src[1].neg = 0;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = op;
	alu.dst.sel = treg;
	alu.dst.chan = 1;
	alu.dst.write = 1;
	alu.src[0].sel = src0_sel;
	alu.src[0].chan = src0_chan + 1;
	alu.src[1].sel = src1_sel;
	alu.src[1].chan = src1_chan + 1;
	alu.src[1].neg = 0;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = opc;
	alu.dst.sel = treg;
	alu.dst.chan = 2;
	alu.dst.write = 1;
	alu.last = 1;
	alu.src[0].sel = src0_sel;
	alu.src[0].chan = src0_chan + 0;
	alu.src[1].sel = src1_sel;
	alu.src[1].chan = src1_chan + 0;
	alu.src[1].neg = 0;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = op;
	alu.dst.sel = treg;
	alu.dst.chan = 1;
	alu.dst.write = 1;
	alu.src[0].sel = treg;
	alu.src[0].chan = 1;
	alu.src[1].sel = treg;
	alu.src[1].chan = 2;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	return 0;
}

static int egcm_u64add(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int r;
	int treg = ctx->temp_reg;
	int op = ALU_OP2_ADD_INT, opc = ALU_OP2_ADDC_UINT;

	if (ctx->src[1].neg) {
		op = ALU_OP2_SUB_INT;
		opc = ALU_OP2_SUBB_UINT;
	}
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = op;            ;
	alu.dst.sel = treg;
	alu.dst.chan = 0;
	alu.dst.write = 1;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
	r600_bytecode_src(&alu.src[1], &ctx->src[1], 0);
	alu.src[1].neg = 0;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = op;
	alu.dst.sel = treg;
	alu.dst.chan = 1;
	alu.dst.write = 1;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 1);
	r600_bytecode_src(&alu.src[1], &ctx->src[1], 1);
	alu.src[1].neg = 0;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = opc              ;
	alu.dst.sel = treg;
	alu.dst.chan = 2;
	alu.dst.write = 1;
	alu.last = 1;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
	r600_bytecode_src(&alu.src[1], &ctx->src[1], 0);
	alu.src[1].neg = 0;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = op;
	tgsi_dst(ctx, &inst->Dst[0], 1, &alu.dst);
	alu.src[0].sel = treg;
	alu.src[0].chan = 1;
	alu.src[1].sel = treg;
	alu.src[1].chan = 2;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	tgsi_dst(ctx, &inst->Dst[0], 0, &alu.dst);
	alu.src[0].sel = treg;
	alu.src[0].chan = 0;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	return 0;
}

/* result.y = mul_high a, b
   result.x = mul a,b
   result.y += a.x * b.y + a.y * b.x;
*/
static int egcm_u64mul(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int r;
	int treg = ctx->temp_reg;

	/* temp.x = mul_lo a.x, b.x */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP2_MULLO_UINT;
	alu.dst.sel = treg;
	alu.dst.chan = 0;
	alu.dst.write = 1;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
	r600_bytecode_src(&alu.src[1], &ctx->src[1], 0);
	r = emit_mul_int_op(ctx->bc, &alu);
	if (r)
		return r;

	/* temp.y = mul_hi a.x, b.x */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP2_MULHI_UINT;
	alu.dst.sel = treg;
	alu.dst.chan = 1;
	alu.dst.write = 1;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
	r600_bytecode_src(&alu.src[1], &ctx->src[1], 0);
	r = emit_mul_int_op(ctx->bc, &alu);
	if (r)
		return r;

	/* temp.z = mul a.x, b.y */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP2_MULLO_UINT;
	alu.dst.sel = treg;
	alu.dst.chan = 2;
	alu.dst.write = 1;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
	r600_bytecode_src(&alu.src[1], &ctx->src[1], 1);
	r = emit_mul_int_op(ctx->bc, &alu);
	if (r)
		return r;

	/* temp.w = mul a.y, b.x */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP2_MULLO_UINT;
	alu.dst.sel = treg;
	alu.dst.chan = 3;
	alu.dst.write = 1;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 1);
	r600_bytecode_src(&alu.src[1], &ctx->src[1], 0);
	r = emit_mul_int_op(ctx->bc, &alu);
	if (r)
		return r;

	/* temp.z = temp.z + temp.w */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP2_ADD_INT;
	alu.dst.sel = treg;
	alu.dst.chan = 2;
	alu.dst.write = 1;
	alu.src[0].sel = treg;
	alu.src[0].chan = 2;
	alu.src[1].sel = treg;
	alu.src[1].chan = 3;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	/* temp.y = temp.y + temp.z */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP2_ADD_INT;
	alu.dst.sel = treg;
	alu.dst.chan = 1;
	alu.dst.write = 1;
	alu.src[0].sel = treg;
	alu.src[0].chan = 1;
	alu.src[1].sel = treg;
	alu.src[1].chan = 2;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	/* dst.x = temp.x */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	tgsi_dst(ctx, &inst->Dst[0], 0, &alu.dst);
	alu.src[0].sel = treg;
	alu.src[0].chan = 0;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	/* dst.y = temp.y */
	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	tgsi_dst(ctx, &inst->Dst[0], 1, &alu.dst);
	alu.src[0].sel = treg;
	alu.src[0].chan = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	return 0;
}

static int emit_u64sge(struct r600_shader_ctx *ctx,
		       int treg,
		       int src0_sel, int src0_base_chan,
		       int src1_sel, int src1_base_chan)
{
	int r;
	/* for 64-bit sge */
	/* result = (src0.y > src1.y) || ((src0.y == src1.y) && src0.x >= src1.x)) */
	r = single_alu_op2(ctx, ALU_OP2_SETGT_UINT,
			   treg, 1,
			   src0_sel, src0_base_chan + 1,
			   src1_sel, src1_base_chan + 1);
	if (r)
		return r;

	r = single_alu_op2(ctx, ALU_OP2_SETGE_UINT,
			   treg, 0,
			   src0_sel, src0_base_chan,
			   src1_sel, src1_base_chan);
	if (r)
		return r;

	r = single_alu_op2(ctx, ALU_OP2_SETE_INT,
			   treg, 2,
			   src0_sel, src0_base_chan + 1,
			   src1_sel, src1_base_chan + 1);
	if (r)
		return r;

	r = single_alu_op2(ctx, ALU_OP2_AND_INT,
			   treg, 0,
			   treg, 0,
			   treg, 2);
	if (r)
		return r;

	r = single_alu_op2(ctx, ALU_OP2_OR_INT,
			   treg, 0,
			   treg, 0,
			   treg, 1);
	if (r)
		return r;
	return 0;
}

/* this isn't a complete div it's just enough for qbo shader to work */
static int egcm_u64div(struct r600_shader_ctx *ctx)
{
	struct r600_bytecode_alu alu;
	struct r600_bytecode_alu_src alu_num_hi, alu_num_lo, alu_denom_hi, alu_denom_lo, alu_src;
	int r, i;
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;

	/* make sure we are dividing my a const with 0 in the high bits */
	if (ctx->src[1].sel != V_SQ_ALU_SRC_LITERAL)
		return -1;
	if (ctx->src[1].value[ctx->src[1].swizzle[1]] != 0)
		return -1;
	/* make sure we are doing one division */
	if (inst->Dst[0].Register.WriteMask != 0x3)
		return -1;

	/* emit_if uses ctx->temp_reg so we can't */
	int treg = r600_get_temp(ctx);
	int tmp_num = r600_get_temp(ctx);
	int sub_tmp = r600_get_temp(ctx);

	/* tmp quot are tmp_num.zw */
	r600_bytecode_src(&alu_num_lo, &ctx->src[0], 0);
	r600_bytecode_src(&alu_num_hi, &ctx->src[0], 1);
	r600_bytecode_src(&alu_denom_lo, &ctx->src[1], 0);
	r600_bytecode_src(&alu_denom_hi, &ctx->src[1], 1);

	/* MOV tmp_num.xy, numerator */
	r = single_alu_op2(ctx, ALU_OP1_MOV,
			   tmp_num, 0,
			   alu_num_lo.sel, alu_num_lo.chan,
			   0, 0);
	if (r)
		return r;
	r = single_alu_op2(ctx, ALU_OP1_MOV,
			   tmp_num, 1,
			   alu_num_hi.sel, alu_num_hi.chan,
			   0, 0);
	if (r)
		return r;

	r = single_alu_op2(ctx, ALU_OP1_MOV,
			   tmp_num, 2,
			   V_SQ_ALU_SRC_LITERAL, 0,
			   0, 0);
	if (r)
		return r;

	r = single_alu_op2(ctx, ALU_OP1_MOV,
			   tmp_num, 3,
			   V_SQ_ALU_SRC_LITERAL, 0,
			   0, 0);
	if (r)
		return r;

	/* treg 0 is log2_denom */
	/* normally this gets the MSB for the denom high value
	   - however we know this will always be 0 here. */
	r = single_alu_op2(ctx,
			   ALU_OP1_MOV,
			   treg, 0,
			   V_SQ_ALU_SRC_LITERAL, 32,
			   0, 0);
	if (r)
		return r;

	/* normally check demon hi for 0, but we know it is already */
	/* t0.z = num_hi >= denom_lo */
	r = single_alu_op2(ctx,
			   ALU_OP2_SETGE_UINT,
			   treg, 1,
			   alu_num_hi.sel, alu_num_hi.chan,
			   V_SQ_ALU_SRC_LITERAL, alu_denom_lo.value);
	if (r)
		return r;

	memset(&alu_src, 0, sizeof(alu_src));
	alu_src.sel = treg;
	alu_src.chan = 1;
	r = emit_if(ctx, ALU_OP2_PRED_SETNE_INT, &alu_src);
	if (r)
		return r;

	/* for loops in here */
	/* get msb t0.x = msb(src[1].x) first */
	int msb_lo = util_last_bit(alu_denom_lo.value);
	r = single_alu_op2(ctx, ALU_OP1_MOV,
			   treg, 0,
			   V_SQ_ALU_SRC_LITERAL, msb_lo,
			   0, 0);
	if (r)
		return r;

	/* unroll the asm here */
	for (i = 0; i < 31; i++) {
		r = single_alu_op2(ctx, ALU_OP2_SETGE_UINT,
				   treg, 2,
				   V_SQ_ALU_SRC_LITERAL, i,
				   treg, 0);
		if (r)
			return r;

		/* we can do this on the CPU */
		uint32_t denom_lo_shl = alu_denom_lo.value << (31 - i);
		/* t0.z = tmp_num.y >= t0.z */
		r = single_alu_op2(ctx, ALU_OP2_SETGE_UINT,
				   treg, 1,
				   tmp_num, 1,
				   V_SQ_ALU_SRC_LITERAL, denom_lo_shl);
		if (r)
			return r;

		r = single_alu_op2(ctx, ALU_OP2_AND_INT,
				   treg, 1,
				   treg, 1,
				   treg, 2);
		if (r)
			return r;

		memset(&alu_src, 0, sizeof(alu_src));
		alu_src.sel = treg;
		alu_src.chan = 1;
		r = emit_if(ctx, ALU_OP2_PRED_SETNE_INT, &alu_src);
		if (r)
			return r;

		r = single_alu_op2(ctx, ALU_OP2_SUB_INT,
				   tmp_num, 1,
				   tmp_num, 1,
				   V_SQ_ALU_SRC_LITERAL, denom_lo_shl);
		if (r)
			return r;

		r = single_alu_op2(ctx, ALU_OP2_OR_INT,
				   tmp_num, 3,
				   tmp_num, 3,
				   V_SQ_ALU_SRC_LITERAL, 1U << (31 - i));
		if (r)
			return r;

		r = tgsi_endif(ctx);
		if (r)
			return r;
	}

	/* log2_denom is always <= 31, so manually peel the last loop
	 * iteration.
	 */
	r = single_alu_op2(ctx, ALU_OP2_SETGE_UINT,
			   treg, 1,
			   tmp_num, 1,
			   V_SQ_ALU_SRC_LITERAL, alu_denom_lo.value);
	if (r)
		return r;

	memset(&alu_src, 0, sizeof(alu_src));
	alu_src.sel = treg;
	alu_src.chan = 1;
	r = emit_if(ctx, ALU_OP2_PRED_SETNE_INT, &alu_src);
	if (r)
		return r;

	r = single_alu_op2(ctx, ALU_OP2_SUB_INT,
			   tmp_num, 1,
			   tmp_num, 1,
			   V_SQ_ALU_SRC_LITERAL, alu_denom_lo.value);
	if (r)
		return r;

	r = single_alu_op2(ctx, ALU_OP2_OR_INT,
			   tmp_num, 3,
			   tmp_num, 3,
			   V_SQ_ALU_SRC_LITERAL, 1U);
	if (r)
		return r;
	r = tgsi_endif(ctx);
	if (r)
		return r;

	r = tgsi_endif(ctx);
	if (r)
		return r;

	/* onto the second loop to unroll */
	for (i = 0; i < 31; i++) {
		r = single_alu_op2(ctx, ALU_OP2_SETGE_UINT,
				   treg, 1,
				   V_SQ_ALU_SRC_LITERAL, (63 - (31 - i)),
				   treg, 0);
		if (r)
			return r;

		uint64_t denom_shl = (uint64_t)alu_denom_lo.value << (31 - i);
		r = single_alu_op2(ctx, ALU_OP1_MOV,
				   treg, 2,
				   V_SQ_ALU_SRC_LITERAL, (denom_shl & 0xffffffff),
				   0, 0);
		if (r)
			return r;

		r = single_alu_op2(ctx, ALU_OP1_MOV,
				   treg, 3,
				   V_SQ_ALU_SRC_LITERAL, (denom_shl >> 32),
				   0, 0);
		if (r)
			return r;

		r = emit_u64sge(ctx, sub_tmp,
				tmp_num, 0,
				treg, 2);
		if (r)
			return r;

		r = single_alu_op2(ctx, ALU_OP2_AND_INT,
				   treg, 1,
				   treg, 1,
				   sub_tmp, 0);
		if (r)
			return r;

		memset(&alu_src, 0, sizeof(alu_src));
		alu_src.sel = treg;
		alu_src.chan = 1;
		r = emit_if(ctx, ALU_OP2_PRED_SETNE_INT, &alu_src);
		if (r)
			return r;


		r = emit_u64add(ctx, ALU_OP2_SUB_INT,
				sub_tmp,
				tmp_num, 0,
				treg, 2);
		if (r)
			return r;

		r = single_alu_op2(ctx, ALU_OP1_MOV,
				   tmp_num, 0,
				   sub_tmp, 0,
				   0, 0);
		if (r)
			return r;

		r = single_alu_op2(ctx, ALU_OP1_MOV,
				   tmp_num, 1,
				   sub_tmp, 1,
				   0, 0);
		if (r)
			return r;

		r = single_alu_op2(ctx, ALU_OP2_OR_INT,
				   tmp_num, 2,
				   tmp_num, 2,
				   V_SQ_ALU_SRC_LITERAL, 1U << (31 - i));
		if (r)
			return r;

		r = tgsi_endif(ctx);
		if (r)
			return r;
	}

	/* log2_denom is always <= 63, so manually peel the last loop
	 * iteration.
	 */
	uint64_t denom_shl = (uint64_t)alu_denom_lo.value;
	r = single_alu_op2(ctx, ALU_OP1_MOV,
			   treg, 2,
			   V_SQ_ALU_SRC_LITERAL, (denom_shl & 0xffffffff),
			   0, 0);
	if (r)
		return r;

	r = single_alu_op2(ctx, ALU_OP1_MOV,
			   treg, 3,
			   V_SQ_ALU_SRC_LITERAL, (denom_shl >> 32),
			   0, 0);
	if (r)
		return r;

	r = emit_u64sge(ctx, sub_tmp,
			tmp_num, 0,
			treg, 2);
	if (r)
		return r;

	memset(&alu_src, 0, sizeof(alu_src));
	alu_src.sel = sub_tmp;
	alu_src.chan = 0;
	r = emit_if(ctx, ALU_OP2_PRED_SETNE_INT, &alu_src);
	if (r)
		return r;

	r = emit_u64add(ctx, ALU_OP2_SUB_INT,
			sub_tmp,
			tmp_num, 0,
			treg, 2);
	if (r)
		return r;

	r = single_alu_op2(ctx, ALU_OP2_OR_INT,
			   tmp_num, 2,
			   tmp_num, 2,
			   V_SQ_ALU_SRC_LITERAL, 1U);
	if (r)
		return r;
	r = tgsi_endif(ctx);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	tgsi_dst(ctx, &inst->Dst[0], 0, &alu.dst);
	alu.src[0].sel = tmp_num;
	alu.src[0].chan = 2;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP1_MOV;
	tgsi_dst(ctx, &inst->Dst[0], 1, &alu.dst);
	alu.src[0].sel = tmp_num;
	alu.src[0].chan = 3;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	return 0;
}

static int egcm_u64sne(struct r600_shader_ctx *ctx)
{
	struct tgsi_full_instruction *inst = &ctx->parse.FullToken.FullInstruction;
	struct r600_bytecode_alu alu;
	int r;
	int treg = ctx->temp_reg;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP2_SETNE_INT;
	alu.dst.sel = treg;
	alu.dst.chan = 0;
	alu.dst.write = 1;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 0);
	r600_bytecode_src(&alu.src[1], &ctx->src[1], 0);
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP2_SETNE_INT;
	alu.dst.sel = treg;
	alu.dst.chan = 1;
	alu.dst.write = 1;
	r600_bytecode_src(&alu.src[0], &ctx->src[0], 1);
	r600_bytecode_src(&alu.src[1], &ctx->src[1], 1);
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;

	memset(&alu, 0, sizeof(struct r600_bytecode_alu));
	alu.op = ALU_OP2_OR_INT;
	tgsi_dst(ctx, &inst->Dst[0], 0, &alu.dst);
	alu.src[0].sel = treg;
	alu.src[0].chan = 0;
	alu.src[1].sel = treg;
	alu.src[1].chan = 1;
	alu.last = 1;
	r = r600_bytecode_add_alu(ctx->bc, &alu);
	if (r)
		return r;
	return 0;
}

static const struct r600_shader_tgsi_instruction r600_shader_tgsi_instruction[] = {
	[TGSI_OPCODE_ARL]	= { ALU_OP0_NOP, tgsi_r600_arl},
	[TGSI_OPCODE_MOV]	= { ALU_OP1_MOV, tgsi_op2},
	[TGSI_OPCODE_LIT]	= { ALU_OP0_NOP, tgsi_lit},

	[TGSI_OPCODE_RCP]	= { ALU_OP1_RECIP_IEEE, tgsi_trans_srcx_replicate},

	[TGSI_OPCODE_RSQ]	= { ALU_OP0_NOP, tgsi_rsq},
	[TGSI_OPCODE_EXP]	= { ALU_OP0_NOP, tgsi_exp},
	[TGSI_OPCODE_LOG]	= { ALU_OP0_NOP, tgsi_log},
	[TGSI_OPCODE_MUL]	= { ALU_OP2_MUL_IEEE, tgsi_op2},
	[TGSI_OPCODE_ADD]	= { ALU_OP2_ADD, tgsi_op2},
	[TGSI_OPCODE_DP3]	= { ALU_OP2_DOT4_IEEE, tgsi_dp},
	[TGSI_OPCODE_DP4]	= { ALU_OP2_DOT4_IEEE, tgsi_dp},
	[TGSI_OPCODE_DST]	= { ALU_OP0_NOP, tgsi_opdst},
	/* MIN_DX10 returns non-nan result if one src is NaN, MIN returns NaN */
	[TGSI_OPCODE_MIN]	= { ALU_OP2_MIN_DX10, tgsi_op2},
	[TGSI_OPCODE_MAX]	= { ALU_OP2_MAX_DX10, tgsi_op2},
	[TGSI_OPCODE_SLT]	= { ALU_OP2_SETGT, tgsi_op2_swap},
	[TGSI_OPCODE_SGE]	= { ALU_OP2_SETGE, tgsi_op2},
	[TGSI_OPCODE_MAD]	= { ALU_OP3_MULADD_IEEE, tgsi_op3},
	[TGSI_OPCODE_LRP]	= { ALU_OP0_NOP, tgsi_lrp},
	[TGSI_OPCODE_FMA]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_SQRT]	= { ALU_OP1_SQRT_IEEE, tgsi_trans_srcx_replicate},
	[21]	= { ALU_OP0_NOP, tgsi_unsupported},
	[22]			= { ALU_OP0_NOP, tgsi_unsupported},
	[23]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_FRC]	= { ALU_OP1_FRACT, tgsi_op2},
	[25]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_FLR]	= { ALU_OP1_FLOOR, tgsi_op2},
	[TGSI_OPCODE_ROUND]	= { ALU_OP1_RNDNE, tgsi_op2},
	[TGSI_OPCODE_EX2]	= { ALU_OP1_EXP_IEEE, tgsi_trans_srcx_replicate},
	[TGSI_OPCODE_LG2]	= { ALU_OP1_LOG_IEEE, tgsi_trans_srcx_replicate},
	[TGSI_OPCODE_POW]	= { ALU_OP0_NOP, tgsi_pow},
	[31]	= { ALU_OP0_NOP, tgsi_unsupported},
	[32]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_CLOCK]     = { ALU_OP0_NOP, tgsi_unsupported},
	[34]			= { ALU_OP0_NOP, tgsi_unsupported},
	[35]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_COS]	= { ALU_OP1_COS, tgsi_trig},
	[TGSI_OPCODE_DDX]	= { FETCH_OP_GET_GRADIENTS_H, tgsi_tex},
	[TGSI_OPCODE_DDY]	= { FETCH_OP_GET_GRADIENTS_V, tgsi_tex},
	[TGSI_OPCODE_KILL]	= { ALU_OP2_KILLGT, tgsi_kill},  /* unconditional kill */
	[TGSI_OPCODE_PK2H]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_PK2US]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_PK4B]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_PK4UB]	= { ALU_OP0_NOP, tgsi_unsupported},
	[44]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_SEQ]	= { ALU_OP2_SETE, tgsi_op2},
	[46]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_SGT]	= { ALU_OP2_SETGT, tgsi_op2},
	[TGSI_OPCODE_SIN]	= { ALU_OP1_SIN, tgsi_trig},
	[TGSI_OPCODE_SLE]	= { ALU_OP2_SETGE, tgsi_op2_swap},
	[TGSI_OPCODE_SNE]	= { ALU_OP2_SETNE, tgsi_op2},
	[51]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_TEX]	= { FETCH_OP_SAMPLE, tgsi_tex},
	[TGSI_OPCODE_TXD]	= { FETCH_OP_SAMPLE_G, tgsi_tex},
	[TGSI_OPCODE_TXP]	= { FETCH_OP_SAMPLE, tgsi_tex},
	[TGSI_OPCODE_UP2H]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_UP2US]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_UP4B]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_UP4UB]	= { ALU_OP0_NOP, tgsi_unsupported},
	[59]			= { ALU_OP0_NOP, tgsi_unsupported},
	[60]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ARR]	= { ALU_OP0_NOP, tgsi_r600_arl},
	[62]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_CAL]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_RET]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_SSG]	= { ALU_OP0_NOP, tgsi_ssg},
	[TGSI_OPCODE_CMP]	= { ALU_OP0_NOP, tgsi_cmp},
	[67]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_TXB]	= { FETCH_OP_SAMPLE_LB, tgsi_tex},
	[69]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_DIV]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_DP2]	= { ALU_OP2_DOT4_IEEE, tgsi_dp},
	[TGSI_OPCODE_TXL]	= { FETCH_OP_SAMPLE_L, tgsi_tex},
	[TGSI_OPCODE_BRK]	= { CF_OP_LOOP_BREAK, tgsi_loop_brk_cont},
	[TGSI_OPCODE_IF]	= { ALU_OP0_NOP, tgsi_if},
	[TGSI_OPCODE_UIF]	= { ALU_OP0_NOP, tgsi_uif},
	[76]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ELSE]	= { ALU_OP0_NOP, tgsi_else},
	[TGSI_OPCODE_ENDIF]	= { ALU_OP0_NOP, tgsi_endif},
	[TGSI_OPCODE_DDX_FINE]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_DDY_FINE]	= { ALU_OP0_NOP, tgsi_unsupported},
	[81]			= { ALU_OP0_NOP, tgsi_unsupported},
	[82]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_CEIL]	= { ALU_OP1_CEIL, tgsi_op2},
	[TGSI_OPCODE_I2F]	= { ALU_OP1_INT_TO_FLT, tgsi_op2_trans},
	[TGSI_OPCODE_NOT]	= { ALU_OP1_NOT_INT, tgsi_op2},
	[TGSI_OPCODE_TRUNC]	= { ALU_OP1_TRUNC, tgsi_op2},
	[TGSI_OPCODE_SHL]	= { ALU_OP2_LSHL_INT, tgsi_op2_trans},
	[88]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_AND]	= { ALU_OP2_AND_INT, tgsi_op2},
	[TGSI_OPCODE_OR]	= { ALU_OP2_OR_INT, tgsi_op2},
	[TGSI_OPCODE_MOD]	= { ALU_OP0_NOP, tgsi_imod},
	[TGSI_OPCODE_XOR]	= { ALU_OP2_XOR_INT, tgsi_op2},
	[93]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_TXF]	= { FETCH_OP_LD, tgsi_tex},
	[TGSI_OPCODE_TXQ]	= { FETCH_OP_GET_TEXTURE_RESINFO, tgsi_tex},
	[TGSI_OPCODE_CONT]	= { CF_OP_LOOP_CONTINUE, tgsi_loop_brk_cont},
	[TGSI_OPCODE_EMIT]	= { CF_OP_EMIT_VERTEX, tgsi_gs_emit},
	[TGSI_OPCODE_ENDPRIM]	= { CF_OP_CUT_VERTEX, tgsi_gs_emit},
	[TGSI_OPCODE_BGNLOOP]	= { ALU_OP0_NOP, tgsi_bgnloop},
	[TGSI_OPCODE_BGNSUB]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ENDLOOP]	= { ALU_OP0_NOP, tgsi_endloop},
	[TGSI_OPCODE_ENDSUB]	= { ALU_OP0_NOP, tgsi_unsupported},
	[103]			= { FETCH_OP_GET_TEXTURE_RESINFO, tgsi_tex},
	[TGSI_OPCODE_TXQS]	= { FETCH_OP_GET_NUMBER_OF_SAMPLES, tgsi_tex},
	[TGSI_OPCODE_RESQ]	= { ALU_OP0_NOP, tgsi_unsupported},
	[106]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_NOP]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_FSEQ]	= { ALU_OP2_SETE_DX10, tgsi_op2},
	[TGSI_OPCODE_FSGE]	= { ALU_OP2_SETGE_DX10, tgsi_op2},
	[TGSI_OPCODE_FSLT]	= { ALU_OP2_SETGT_DX10, tgsi_op2_swap},
	[TGSI_OPCODE_FSNE]	= { ALU_OP2_SETNE_DX10, tgsi_op2_swap},
	[TGSI_OPCODE_MEMBAR]	= { ALU_OP0_NOP, tgsi_unsupported},
	[113]	= { ALU_OP0_NOP, tgsi_unsupported},
	[114]			= { ALU_OP0_NOP, tgsi_unsupported},
	[115]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_KILL_IF]	= { ALU_OP2_KILLGT, tgsi_kill},  /* conditional kill */
	[TGSI_OPCODE_END]	= { ALU_OP0_NOP, tgsi_end},  /* aka HALT */
	[TGSI_OPCODE_DFMA]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_F2I]	= { ALU_OP1_FLT_TO_INT, tgsi_op2_trans},
	[TGSI_OPCODE_IDIV]	= { ALU_OP0_NOP, tgsi_idiv},
	[TGSI_OPCODE_IMAX]	= { ALU_OP2_MAX_INT, tgsi_op2},
	[TGSI_OPCODE_IMIN]	= { ALU_OP2_MIN_INT, tgsi_op2},
	[TGSI_OPCODE_INEG]	= { ALU_OP2_SUB_INT, tgsi_ineg},
	[TGSI_OPCODE_ISGE]	= { ALU_OP2_SETGE_INT, tgsi_op2},
	[TGSI_OPCODE_ISHR]	= { ALU_OP2_ASHR_INT, tgsi_op2_trans},
	[TGSI_OPCODE_ISLT]	= { ALU_OP2_SETGT_INT, tgsi_op2_swap},
	[TGSI_OPCODE_F2U]	= { ALU_OP1_FLT_TO_UINT, tgsi_op2_trans},
	[TGSI_OPCODE_U2F]	= { ALU_OP1_UINT_TO_FLT, tgsi_op2_trans},
	[TGSI_OPCODE_UADD]	= { ALU_OP2_ADD_INT, tgsi_op2},
	[TGSI_OPCODE_UDIV]	= { ALU_OP0_NOP, tgsi_udiv},
	[TGSI_OPCODE_UMAD]	= { ALU_OP0_NOP, tgsi_umad},
	[TGSI_OPCODE_UMAX]	= { ALU_OP2_MAX_UINT, tgsi_op2},
	[TGSI_OPCODE_UMIN]	= { ALU_OP2_MIN_UINT, tgsi_op2},
	[TGSI_OPCODE_UMOD]	= { ALU_OP0_NOP, tgsi_umod},
	[TGSI_OPCODE_UMUL]	= { ALU_OP2_MULLO_UINT, tgsi_op2_trans},
	[TGSI_OPCODE_USEQ]	= { ALU_OP2_SETE_INT, tgsi_op2},
	[TGSI_OPCODE_USGE]	= { ALU_OP2_SETGE_UINT, tgsi_op2},
	[TGSI_OPCODE_USHR]	= { ALU_OP2_LSHR_INT, tgsi_op2_trans},
	[TGSI_OPCODE_USLT]	= { ALU_OP2_SETGT_UINT, tgsi_op2_swap},
	[TGSI_OPCODE_USNE]	= { ALU_OP2_SETNE_INT, tgsi_op2_swap},
	[TGSI_OPCODE_SWITCH]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_CASE]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_DEFAULT]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ENDSWITCH]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_I]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_I_MS]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_B]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_C]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_C_LZ]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_D]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_L]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_GATHER4]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SVIEWINFO]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_POS]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_INFO]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_UARL]	= { ALU_OP1_MOVA_INT, tgsi_r600_arl},
	[TGSI_OPCODE_UCMP]	= { ALU_OP0_NOP, tgsi_ucmp},
	[TGSI_OPCODE_IABS]	= { 0, tgsi_iabs},
	[TGSI_OPCODE_ISSG]	= { 0, tgsi_issg},
	[TGSI_OPCODE_LOAD]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_STORE]	= { ALU_OP0_NOP, tgsi_unsupported},
	[163]	= { ALU_OP0_NOP, tgsi_unsupported},
	[164]	= { ALU_OP0_NOP, tgsi_unsupported},
	[165]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_BARRIER]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ATOMUADD]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ATOMXCHG]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ATOMCAS]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ATOMAND]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ATOMOR]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ATOMXOR]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ATOMUMIN]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ATOMUMAX]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ATOMIMIN]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ATOMIMAX]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_TEX2]	= { FETCH_OP_SAMPLE, tgsi_tex},
	[TGSI_OPCODE_TXB2]	= { FETCH_OP_SAMPLE_LB, tgsi_tex},
	[TGSI_OPCODE_TXL2]	= { FETCH_OP_SAMPLE_L, tgsi_tex},
	[TGSI_OPCODE_IMUL_HI]	= { ALU_OP2_MULHI_INT, tgsi_op2_trans},
	[TGSI_OPCODE_UMUL_HI]	= { ALU_OP2_MULHI_UINT, tgsi_op2_trans},
	[TGSI_OPCODE_TG4]	= { FETCH_OP_GATHER4, tgsi_unsupported},
	[TGSI_OPCODE_LODQ]	= { FETCH_OP_GET_LOD, tgsi_unsupported},
	[TGSI_OPCODE_IBFE]	= { ALU_OP3_BFE_INT, tgsi_unsupported},
	[TGSI_OPCODE_UBFE]	= { ALU_OP3_BFE_UINT, tgsi_unsupported},
	[TGSI_OPCODE_BFI]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_BREV]	= { ALU_OP1_BFREV_INT, tgsi_unsupported},
	[TGSI_OPCODE_POPC]	= { ALU_OP1_BCNT_INT, tgsi_unsupported},
	[TGSI_OPCODE_LSB]	= { ALU_OP1_FFBL_INT, tgsi_unsupported},
	[TGSI_OPCODE_IMSB]	= { ALU_OP1_FFBH_INT, tgsi_unsupported},
	[TGSI_OPCODE_UMSB]	= { ALU_OP1_FFBH_UINT, tgsi_unsupported},
	[TGSI_OPCODE_INTERP_CENTROID]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_INTERP_SAMPLE]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_INTERP_OFFSET]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_LAST]	= { ALU_OP0_NOP, tgsi_unsupported},
};

static const struct r600_shader_tgsi_instruction eg_shader_tgsi_instruction[] = {
	[TGSI_OPCODE_ARL]	= { ALU_OP0_NOP, tgsi_eg_arl},
	[TGSI_OPCODE_MOV]	= { ALU_OP1_MOV, tgsi_op2},
	[TGSI_OPCODE_LIT]	= { ALU_OP0_NOP, tgsi_lit},
	[TGSI_OPCODE_RCP]	= { ALU_OP1_RECIP_IEEE, tgsi_trans_srcx_replicate},
	[TGSI_OPCODE_RSQ]	= { ALU_OP0_NOP, tgsi_rsq},
	[TGSI_OPCODE_EXP]	= { ALU_OP0_NOP, tgsi_exp},
	[TGSI_OPCODE_LOG]	= { ALU_OP0_NOP, tgsi_log},
	[TGSI_OPCODE_MUL]	= { ALU_OP2_MUL_IEEE, tgsi_op2},
	[TGSI_OPCODE_ADD]	= { ALU_OP2_ADD, tgsi_op2},
	[TGSI_OPCODE_DP3]	= { ALU_OP2_DOT4_IEEE, tgsi_dp},
	[TGSI_OPCODE_DP4]	= { ALU_OP2_DOT4_IEEE, tgsi_dp},
	[TGSI_OPCODE_DST]	= { ALU_OP0_NOP, tgsi_opdst},
	[TGSI_OPCODE_MIN]	= { ALU_OP2_MIN_DX10, tgsi_op2},
	[TGSI_OPCODE_MAX]	= { ALU_OP2_MAX_DX10, tgsi_op2},
	[TGSI_OPCODE_SLT]	= { ALU_OP2_SETGT, tgsi_op2_swap},
	[TGSI_OPCODE_SGE]	= { ALU_OP2_SETGE, tgsi_op2},
	[TGSI_OPCODE_MAD]	= { ALU_OP3_MULADD_IEEE, tgsi_op3},
	[TGSI_OPCODE_LRP]	= { ALU_OP0_NOP, tgsi_lrp},
	[TGSI_OPCODE_FMA]	= { ALU_OP3_FMA, tgsi_op3},
	[TGSI_OPCODE_SQRT]	= { ALU_OP1_SQRT_IEEE, tgsi_trans_srcx_replicate},
	[21]	= { ALU_OP0_NOP, tgsi_unsupported},
	[22]			= { ALU_OP0_NOP, tgsi_unsupported},
	[23]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_FRC]	= { ALU_OP1_FRACT, tgsi_op2},
	[25]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_FLR]	= { ALU_OP1_FLOOR, tgsi_op2},
	[TGSI_OPCODE_ROUND]	= { ALU_OP1_RNDNE, tgsi_op2},
	[TGSI_OPCODE_EX2]	= { ALU_OP1_EXP_IEEE, tgsi_trans_srcx_replicate},
	[TGSI_OPCODE_LG2]	= { ALU_OP1_LOG_IEEE, tgsi_trans_srcx_replicate},
	[TGSI_OPCODE_POW]	= { ALU_OP0_NOP, tgsi_pow},
	[31]	= { ALU_OP0_NOP, tgsi_unsupported},
	[32]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_CLOCK]     = { ALU_OP0_NOP, tgsi_clock},
	[34]			= { ALU_OP0_NOP, tgsi_unsupported},
	[35]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_COS]	= { ALU_OP1_COS, tgsi_trig},
	[TGSI_OPCODE_DDX]	= { FETCH_OP_GET_GRADIENTS_H, tgsi_tex},
	[TGSI_OPCODE_DDY]	= { FETCH_OP_GET_GRADIENTS_V, tgsi_tex},
	[TGSI_OPCODE_KILL]	= { ALU_OP2_KILLGT, tgsi_kill},  /* unconditional kill */
	[TGSI_OPCODE_PK2H]	= { ALU_OP0_NOP, tgsi_pk2h},
	[TGSI_OPCODE_PK2US]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_PK4B]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_PK4UB]	= { ALU_OP0_NOP, tgsi_unsupported},
	[44]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_SEQ]	= { ALU_OP2_SETE, tgsi_op2},
	[46]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_SGT]	= { ALU_OP2_SETGT, tgsi_op2},
	[TGSI_OPCODE_SIN]	= { ALU_OP1_SIN, tgsi_trig},
	[TGSI_OPCODE_SLE]	= { ALU_OP2_SETGE, tgsi_op2_swap},
	[TGSI_OPCODE_SNE]	= { ALU_OP2_SETNE, tgsi_op2},
	[51]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_TEX]	= { FETCH_OP_SAMPLE, tgsi_tex},
	[TGSI_OPCODE_TXD]	= { FETCH_OP_SAMPLE_G, tgsi_tex},
	[TGSI_OPCODE_TXP]	= { FETCH_OP_SAMPLE, tgsi_tex},
	[TGSI_OPCODE_UP2H]	= { ALU_OP0_NOP, tgsi_up2h},
	[TGSI_OPCODE_UP2US]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_UP4B]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_UP4UB]	= { ALU_OP0_NOP, tgsi_unsupported},
	[59]			= { ALU_OP0_NOP, tgsi_unsupported},
	[60]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ARR]	= { ALU_OP0_NOP, tgsi_eg_arl},
	[62]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_CAL]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_RET]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_SSG]	= { ALU_OP0_NOP, tgsi_ssg},
	[TGSI_OPCODE_CMP]	= { ALU_OP0_NOP, tgsi_cmp},
	[67]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_TXB]	= { FETCH_OP_SAMPLE_LB, tgsi_tex},
	[69]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_DIV]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_DP2]	= { ALU_OP2_DOT4_IEEE, tgsi_dp},
	[TGSI_OPCODE_TXL]	= { FETCH_OP_SAMPLE_L, tgsi_tex},
	[TGSI_OPCODE_BRK]	= { CF_OP_LOOP_BREAK, tgsi_loop_brk_cont},
	[TGSI_OPCODE_IF]	= { ALU_OP0_NOP, tgsi_if},
	[TGSI_OPCODE_UIF]	= { ALU_OP0_NOP, tgsi_uif},
	[76]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ELSE]	= { ALU_OP0_NOP, tgsi_else},
	[TGSI_OPCODE_ENDIF]	= { ALU_OP0_NOP, tgsi_endif},
	[TGSI_OPCODE_DDX_FINE]	= { FETCH_OP_GET_GRADIENTS_H, tgsi_tex},
	[TGSI_OPCODE_DDY_FINE]	= { FETCH_OP_GET_GRADIENTS_V, tgsi_tex},
	[82]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_CEIL]	= { ALU_OP1_CEIL, tgsi_op2},
	[TGSI_OPCODE_I2F]	= { ALU_OP1_INT_TO_FLT, tgsi_op2_trans},
	[TGSI_OPCODE_NOT]	= { ALU_OP1_NOT_INT, tgsi_op2},
	[TGSI_OPCODE_TRUNC]	= { ALU_OP1_TRUNC, tgsi_op2},
	[TGSI_OPCODE_SHL]	= { ALU_OP2_LSHL_INT, tgsi_op2},
	[88]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_AND]	= { ALU_OP2_AND_INT, tgsi_op2},
	[TGSI_OPCODE_OR]	= { ALU_OP2_OR_INT, tgsi_op2},
	[TGSI_OPCODE_MOD]	= { ALU_OP0_NOP, tgsi_imod},
	[TGSI_OPCODE_XOR]	= { ALU_OP2_XOR_INT, tgsi_op2},
	[93]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_TXF]	= { FETCH_OP_LD, tgsi_tex},
	[TGSI_OPCODE_TXQ]	= { FETCH_OP_GET_TEXTURE_RESINFO, tgsi_tex},
	[TGSI_OPCODE_CONT]	= { CF_OP_LOOP_CONTINUE, tgsi_loop_brk_cont},
	[TGSI_OPCODE_EMIT]	= { CF_OP_EMIT_VERTEX, tgsi_gs_emit},
	[TGSI_OPCODE_ENDPRIM]	= { CF_OP_CUT_VERTEX, tgsi_gs_emit},
	[TGSI_OPCODE_BGNLOOP]	= { ALU_OP0_NOP, tgsi_bgnloop},
	[TGSI_OPCODE_BGNSUB]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ENDLOOP]	= { ALU_OP0_NOP, tgsi_endloop},
	[TGSI_OPCODE_ENDSUB]	= { ALU_OP0_NOP, tgsi_unsupported},
	[103]			= { FETCH_OP_GET_TEXTURE_RESINFO, tgsi_tex},
	[TGSI_OPCODE_TXQS]	= { FETCH_OP_GET_NUMBER_OF_SAMPLES, tgsi_tex},
	[TGSI_OPCODE_RESQ]     	= { FETCH_OP_GET_TEXTURE_RESINFO, tgsi_resq},
	[106]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_NOP]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_FSEQ]	= { ALU_OP2_SETE_DX10, tgsi_op2},
	[TGSI_OPCODE_FSGE]	= { ALU_OP2_SETGE_DX10, tgsi_op2},
	[TGSI_OPCODE_FSLT]	= { ALU_OP2_SETGT_DX10, tgsi_op2_swap},
	[TGSI_OPCODE_FSNE]	= { ALU_OP2_SETNE_DX10, tgsi_op2_swap},
	[TGSI_OPCODE_MEMBAR]    = { ALU_OP0_GROUP_BARRIER, tgsi_barrier},
	[113]	= { ALU_OP0_NOP, tgsi_unsupported},
	[114]			= { ALU_OP0_NOP, tgsi_unsupported},
	[115]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_KILL_IF]	= { ALU_OP2_KILLGT, tgsi_kill},  /* conditional kill */
	[TGSI_OPCODE_END]	= { ALU_OP0_NOP, tgsi_end},  /* aka HALT */
	/* Refer below for TGSI_OPCODE_DFMA */
	[TGSI_OPCODE_F2I]	= { ALU_OP1_FLT_TO_INT, tgsi_f2i},
	[TGSI_OPCODE_IDIV]	= { ALU_OP0_NOP, tgsi_idiv},
	[TGSI_OPCODE_IMAX]	= { ALU_OP2_MAX_INT, tgsi_op2},
	[TGSI_OPCODE_IMIN]	= { ALU_OP2_MIN_INT, tgsi_op2},
	[TGSI_OPCODE_INEG]	= { ALU_OP2_SUB_INT, tgsi_ineg},
	[TGSI_OPCODE_ISGE]	= { ALU_OP2_SETGE_INT, tgsi_op2},
	[TGSI_OPCODE_ISHR]	= { ALU_OP2_ASHR_INT, tgsi_op2},
	[TGSI_OPCODE_ISLT]	= { ALU_OP2_SETGT_INT, tgsi_op2_swap},
	[TGSI_OPCODE_F2U]	= { ALU_OP1_FLT_TO_UINT, tgsi_f2i},
	[TGSI_OPCODE_U2F]	= { ALU_OP1_UINT_TO_FLT, tgsi_op2_trans},
	[TGSI_OPCODE_UADD]	= { ALU_OP2_ADD_INT, tgsi_op2},
	[TGSI_OPCODE_UDIV]	= { ALU_OP0_NOP, tgsi_udiv},
	[TGSI_OPCODE_UMAD]	= { ALU_OP0_NOP, tgsi_umad},
	[TGSI_OPCODE_UMAX]	= { ALU_OP2_MAX_UINT, tgsi_op2},
	[TGSI_OPCODE_UMIN]	= { ALU_OP2_MIN_UINT, tgsi_op2},
	[TGSI_OPCODE_UMOD]	= { ALU_OP0_NOP, tgsi_umod},
	[TGSI_OPCODE_UMUL]	= { ALU_OP2_MULLO_UINT, tgsi_op2_trans},
	[TGSI_OPCODE_USEQ]	= { ALU_OP2_SETE_INT, tgsi_op2},
	[TGSI_OPCODE_USGE]	= { ALU_OP2_SETGE_UINT, tgsi_op2},
	[TGSI_OPCODE_USHR]	= { ALU_OP2_LSHR_INT, tgsi_op2},
	[TGSI_OPCODE_USLT]	= { ALU_OP2_SETGT_UINT, tgsi_op2_swap},
	[TGSI_OPCODE_USNE]	= { ALU_OP2_SETNE_INT, tgsi_op2},
	[TGSI_OPCODE_SWITCH]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_CASE]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_DEFAULT]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ENDSWITCH]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_I]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_I_MS]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_B]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_C]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_C_LZ]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_D]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_L]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_GATHER4]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SVIEWINFO]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_POS]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_INFO]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_UARL]	= { ALU_OP1_MOVA_INT, tgsi_eg_arl},
	[TGSI_OPCODE_UCMP]	= { ALU_OP0_NOP, tgsi_ucmp},
	[TGSI_OPCODE_IABS]	= { 0, tgsi_iabs},
	[TGSI_OPCODE_ISSG]	= { 0, tgsi_issg},
	[TGSI_OPCODE_LOAD]	= { ALU_OP0_NOP, tgsi_load},
	[TGSI_OPCODE_STORE]	= { ALU_OP0_NOP, tgsi_store},
	[163]	= { ALU_OP0_NOP, tgsi_unsupported},
	[164]	= { ALU_OP0_NOP, tgsi_unsupported},
	[165]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_BARRIER]	= { ALU_OP0_GROUP_BARRIER, tgsi_barrier},
	[TGSI_OPCODE_ATOMUADD]	= { V_RAT_INST_ADD_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMXCHG]	= { V_RAT_INST_XCHG_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMCAS]	= { V_RAT_INST_CMPXCHG_INT_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMAND]	= { V_RAT_INST_AND_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMOR]	= { V_RAT_INST_OR_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMXOR]	= { V_RAT_INST_XOR_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMUMIN]	= { V_RAT_INST_MIN_UINT_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMUMAX]	= { V_RAT_INST_MAX_UINT_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMIMIN]	= { V_RAT_INST_MIN_INT_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMIMAX]	= { V_RAT_INST_MAX_INT_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_TEX2]	= { FETCH_OP_SAMPLE, tgsi_tex},
	[TGSI_OPCODE_TXB2]	= { FETCH_OP_SAMPLE_LB, tgsi_tex},
	[TGSI_OPCODE_TXL2]	= { FETCH_OP_SAMPLE_L, tgsi_tex},
	[TGSI_OPCODE_IMUL_HI]	= { ALU_OP2_MULHI_INT, tgsi_op2_trans},
	[TGSI_OPCODE_UMUL_HI]	= { ALU_OP2_MULHI_UINT, tgsi_op2_trans},
	[TGSI_OPCODE_TG4]	= { FETCH_OP_GATHER4, tgsi_tex},
	[TGSI_OPCODE_LODQ]	= { FETCH_OP_GET_LOD, tgsi_tex},
	[TGSI_OPCODE_IBFE]	= { ALU_OP3_BFE_INT, tgsi_bfe},
	[TGSI_OPCODE_UBFE]	= { ALU_OP3_BFE_UINT, tgsi_bfe},
	[TGSI_OPCODE_BFI]	= { ALU_OP0_NOP, tgsi_bfi},
	[TGSI_OPCODE_BREV]	= { ALU_OP1_BFREV_INT, tgsi_op2},
	[TGSI_OPCODE_POPC]	= { ALU_OP1_BCNT_INT, tgsi_op2},
	[TGSI_OPCODE_LSB]	= { ALU_OP1_FFBL_INT, tgsi_op2},
	[TGSI_OPCODE_IMSB]	= { ALU_OP1_FFBH_INT, tgsi_msb},
	[TGSI_OPCODE_UMSB]	= { ALU_OP1_FFBH_UINT, tgsi_msb},
	[TGSI_OPCODE_INTERP_CENTROID]	= { ALU_OP0_NOP, tgsi_interp_egcm},
	[TGSI_OPCODE_INTERP_SAMPLE]	= { ALU_OP0_NOP, tgsi_interp_egcm},
	[TGSI_OPCODE_INTERP_OFFSET]	= { ALU_OP0_NOP, tgsi_interp_egcm},
	[TGSI_OPCODE_F2D]	= { ALU_OP1_FLT32_TO_FLT64, tgsi_op2_64},
	[TGSI_OPCODE_D2F]	= { ALU_OP1_FLT64_TO_FLT32, tgsi_op2_64_single_dest},
	[TGSI_OPCODE_DABS]	= { ALU_OP1_MOV, tgsi_op2_64},
	[TGSI_OPCODE_DNEG]	= { ALU_OP2_ADD_64, tgsi_dneg},
	[TGSI_OPCODE_DADD]	= { ALU_OP2_ADD_64, tgsi_op2_64},
	[TGSI_OPCODE_DMUL]	= { ALU_OP2_MUL_64, cayman_mul_double_instr},
	[TGSI_OPCODE_DDIV]	= { 0, cayman_ddiv_instr },
	[TGSI_OPCODE_DMAX]	= { ALU_OP2_MAX_64, tgsi_op2_64},
	[TGSI_OPCODE_DMIN]	= { ALU_OP2_MIN_64, tgsi_op2_64},
	[TGSI_OPCODE_DSLT]	= { ALU_OP2_SETGT_64, tgsi_op2_64_single_dest_s},
	[TGSI_OPCODE_DSGE]	= { ALU_OP2_SETGE_64, tgsi_op2_64_single_dest},
	[TGSI_OPCODE_DSEQ]	= { ALU_OP2_SETE_64, tgsi_op2_64_single_dest},
	[TGSI_OPCODE_DSNE]	= { ALU_OP2_SETNE_64, tgsi_op2_64_single_dest},
	[TGSI_OPCODE_DRCP]	= { ALU_OP2_RECIP_64, cayman_emit_double_instr},
	[TGSI_OPCODE_DSQRT]	= { ALU_OP2_SQRT_64, cayman_emit_double_instr},
	[TGSI_OPCODE_DMAD]	= { ALU_OP3_FMA_64, tgsi_op3_64},
	[TGSI_OPCODE_DFMA]	= { ALU_OP3_FMA_64, tgsi_op3_64},
	[TGSI_OPCODE_DFRAC]	= { ALU_OP1_FRACT_64, tgsi_op2_64},
	[TGSI_OPCODE_DLDEXP]	= { ALU_OP2_LDEXP_64, tgsi_op2_64},
	[TGSI_OPCODE_DFRACEXP]	= { ALU_OP1_FREXP_64, tgsi_dfracexp},
	[TGSI_OPCODE_D2I]	= { ALU_OP1_FLT_TO_INT, egcm_double_to_int},
	[TGSI_OPCODE_I2D]	= { ALU_OP1_INT_TO_FLT, egcm_int_to_double},
	[TGSI_OPCODE_D2U]	= { ALU_OP1_FLT_TO_UINT, egcm_double_to_int},
	[TGSI_OPCODE_U2D]	= { ALU_OP1_UINT_TO_FLT, egcm_int_to_double},
	[TGSI_OPCODE_DRSQ]	= { ALU_OP2_RECIPSQRT_64, cayman_emit_double_instr},
	[TGSI_OPCODE_U64SNE]    = { ALU_OP0_NOP, egcm_u64sne },
	[TGSI_OPCODE_U64ADD]    = { ALU_OP0_NOP, egcm_u64add },
	[TGSI_OPCODE_U64MUL]    = { ALU_OP0_NOP, egcm_u64mul },
	[TGSI_OPCODE_U64DIV]    = { ALU_OP0_NOP, egcm_u64div },
	[TGSI_OPCODE_LAST]	= { ALU_OP0_NOP, tgsi_unsupported},
};

static const struct r600_shader_tgsi_instruction cm_shader_tgsi_instruction[] = {
	[TGSI_OPCODE_ARL]	= { ALU_OP0_NOP, tgsi_eg_arl},
	[TGSI_OPCODE_MOV]	= { ALU_OP1_MOV, tgsi_op2},
	[TGSI_OPCODE_LIT]	= { ALU_OP0_NOP, tgsi_lit},
	[TGSI_OPCODE_RCP]	= { ALU_OP1_RECIP_IEEE, cayman_emit_float_instr},
	[TGSI_OPCODE_RSQ]	= { ALU_OP1_RECIPSQRT_IEEE, cayman_emit_float_instr},
	[TGSI_OPCODE_EXP]	= { ALU_OP0_NOP, tgsi_exp},
	[TGSI_OPCODE_LOG]	= { ALU_OP0_NOP, tgsi_log},
	[TGSI_OPCODE_MUL]	= { ALU_OP2_MUL_IEEE, tgsi_op2},
	[TGSI_OPCODE_ADD]	= { ALU_OP2_ADD, tgsi_op2},
	[TGSI_OPCODE_DP3]	= { ALU_OP2_DOT4_IEEE, tgsi_dp},
	[TGSI_OPCODE_DP4]	= { ALU_OP2_DOT4_IEEE, tgsi_dp},
	[TGSI_OPCODE_DST]	= { ALU_OP0_NOP, tgsi_opdst},
	[TGSI_OPCODE_MIN]	= { ALU_OP2_MIN_DX10, tgsi_op2},
	[TGSI_OPCODE_MAX]	= { ALU_OP2_MAX_DX10, tgsi_op2},
	[TGSI_OPCODE_SLT]	= { ALU_OP2_SETGT, tgsi_op2_swap},
	[TGSI_OPCODE_SGE]	= { ALU_OP2_SETGE, tgsi_op2},
	[TGSI_OPCODE_MAD]	= { ALU_OP3_MULADD_IEEE, tgsi_op3},
	[TGSI_OPCODE_LRP]	= { ALU_OP0_NOP, tgsi_lrp},
	[TGSI_OPCODE_FMA]	= { ALU_OP3_FMA, tgsi_op3},
	[TGSI_OPCODE_SQRT]	= { ALU_OP1_SQRT_IEEE, cayman_emit_float_instr},
	[21]	= { ALU_OP0_NOP, tgsi_unsupported},
	[22]			= { ALU_OP0_NOP, tgsi_unsupported},
	[23]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_FRC]	= { ALU_OP1_FRACT, tgsi_op2},
	[25]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_FLR]	= { ALU_OP1_FLOOR, tgsi_op2},
	[TGSI_OPCODE_ROUND]	= { ALU_OP1_RNDNE, tgsi_op2},
	[TGSI_OPCODE_EX2]	= { ALU_OP1_EXP_IEEE, cayman_emit_float_instr},
	[TGSI_OPCODE_LG2]	= { ALU_OP1_LOG_IEEE, cayman_emit_float_instr},
	[TGSI_OPCODE_POW]	= { ALU_OP0_NOP, cayman_pow},
	[31]	= { ALU_OP0_NOP, tgsi_unsupported},
	[32]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_CLOCK]     = { ALU_OP0_NOP, tgsi_clock},
	[34]			= { ALU_OP0_NOP, tgsi_unsupported},
	[35]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_COS]	= { ALU_OP1_COS, cayman_trig},
	[TGSI_OPCODE_DDX]	= { FETCH_OP_GET_GRADIENTS_H, tgsi_tex},
	[TGSI_OPCODE_DDY]	= { FETCH_OP_GET_GRADIENTS_V, tgsi_tex},
	[TGSI_OPCODE_KILL]	= { ALU_OP2_KILLGT, tgsi_kill},  /* unconditional kill */
	[TGSI_OPCODE_PK2H]	= { ALU_OP0_NOP, tgsi_pk2h},
	[TGSI_OPCODE_PK2US]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_PK4B]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_PK4UB]	= { ALU_OP0_NOP, tgsi_unsupported},
	[44]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_SEQ]	= { ALU_OP2_SETE, tgsi_op2},
	[46]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_SGT]	= { ALU_OP2_SETGT, tgsi_op2},
	[TGSI_OPCODE_SIN]	= { ALU_OP1_SIN, cayman_trig},
	[TGSI_OPCODE_SLE]	= { ALU_OP2_SETGE, tgsi_op2_swap},
	[TGSI_OPCODE_SNE]	= { ALU_OP2_SETNE, tgsi_op2},
	[51]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_TEX]	= { FETCH_OP_SAMPLE, tgsi_tex},
	[TGSI_OPCODE_TXD]	= { FETCH_OP_SAMPLE_G, tgsi_tex},
	[TGSI_OPCODE_TXP]	= { FETCH_OP_SAMPLE, tgsi_tex},
	[TGSI_OPCODE_UP2H]	= { ALU_OP0_NOP, tgsi_up2h},
	[TGSI_OPCODE_UP2US]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_UP4B]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_UP4UB]	= { ALU_OP0_NOP, tgsi_unsupported},
	[59]			= { ALU_OP0_NOP, tgsi_unsupported},
	[60]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ARR]	= { ALU_OP0_NOP, tgsi_eg_arl},
	[62]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_CAL]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_RET]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_SSG]	= { ALU_OP0_NOP, tgsi_ssg},
	[TGSI_OPCODE_CMP]	= { ALU_OP0_NOP, tgsi_cmp},
	[67]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_TXB]	= { FETCH_OP_SAMPLE_LB, tgsi_tex},
	[69]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_DIV]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_DP2]	= { ALU_OP2_DOT4_IEEE, tgsi_dp},
	[TGSI_OPCODE_TXL]	= { FETCH_OP_SAMPLE_L, tgsi_tex},
	[TGSI_OPCODE_BRK]	= { CF_OP_LOOP_BREAK, tgsi_loop_brk_cont},
	[TGSI_OPCODE_IF]	= { ALU_OP0_NOP, tgsi_if},
	[TGSI_OPCODE_UIF]	= { ALU_OP0_NOP, tgsi_uif},
	[76]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ELSE]	= { ALU_OP0_NOP, tgsi_else},
	[TGSI_OPCODE_ENDIF]	= { ALU_OP0_NOP, tgsi_endif},
	[TGSI_OPCODE_DDX_FINE]	= { FETCH_OP_GET_GRADIENTS_H, tgsi_tex},
	[TGSI_OPCODE_DDY_FINE]	= { FETCH_OP_GET_GRADIENTS_V, tgsi_tex},
	[82]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_CEIL]	= { ALU_OP1_CEIL, tgsi_op2},
	[TGSI_OPCODE_I2F]	= { ALU_OP1_INT_TO_FLT, tgsi_op2},
	[TGSI_OPCODE_NOT]	= { ALU_OP1_NOT_INT, tgsi_op2},
	[TGSI_OPCODE_TRUNC]	= { ALU_OP1_TRUNC, tgsi_op2},
	[TGSI_OPCODE_SHL]	= { ALU_OP2_LSHL_INT, tgsi_op2},
	[88]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_AND]	= { ALU_OP2_AND_INT, tgsi_op2},
	[TGSI_OPCODE_OR]	= { ALU_OP2_OR_INT, tgsi_op2},
	[TGSI_OPCODE_MOD]	= { ALU_OP0_NOP, tgsi_imod},
	[TGSI_OPCODE_XOR]	= { ALU_OP2_XOR_INT, tgsi_op2},
	[93]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_TXF]	= { FETCH_OP_LD, tgsi_tex},
	[TGSI_OPCODE_TXQ]	= { FETCH_OP_GET_TEXTURE_RESINFO, tgsi_tex},
	[TGSI_OPCODE_CONT]	= { CF_OP_LOOP_CONTINUE, tgsi_loop_brk_cont},
	[TGSI_OPCODE_EMIT]	= { CF_OP_EMIT_VERTEX, tgsi_gs_emit},
	[TGSI_OPCODE_ENDPRIM]	= { CF_OP_CUT_VERTEX, tgsi_gs_emit},
	[TGSI_OPCODE_BGNLOOP]	= { ALU_OP0_NOP, tgsi_bgnloop},
	[TGSI_OPCODE_BGNSUB]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ENDLOOP]	= { ALU_OP0_NOP, tgsi_endloop},
	[TGSI_OPCODE_ENDSUB]	= { ALU_OP0_NOP, tgsi_unsupported},
	[103]			= { FETCH_OP_GET_TEXTURE_RESINFO, tgsi_tex},
	[TGSI_OPCODE_TXQS]	= { FETCH_OP_GET_NUMBER_OF_SAMPLES, tgsi_tex},
	[TGSI_OPCODE_RESQ]     	= { FETCH_OP_GET_TEXTURE_RESINFO, tgsi_resq},
	[106]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_NOP]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_FSEQ]	= { ALU_OP2_SETE_DX10, tgsi_op2},
	[TGSI_OPCODE_FSGE]	= { ALU_OP2_SETGE_DX10, tgsi_op2},
	[TGSI_OPCODE_FSLT]	= { ALU_OP2_SETGT_DX10, tgsi_op2_swap},
	[TGSI_OPCODE_FSNE]	= { ALU_OP2_SETNE_DX10, tgsi_op2_swap},
	[TGSI_OPCODE_MEMBAR]    = { ALU_OP0_GROUP_BARRIER, tgsi_barrier},
	[113]	= { ALU_OP0_NOP, tgsi_unsupported},
	[114]			= { ALU_OP0_NOP, tgsi_unsupported},
	[115]			= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_KILL_IF]	= { ALU_OP2_KILLGT, tgsi_kill},  /* conditional kill */
	[TGSI_OPCODE_END]	= { ALU_OP0_NOP, tgsi_end},  /* aka HALT */
	/* Refer below for TGSI_OPCODE_DFMA */
	[TGSI_OPCODE_F2I]	= { ALU_OP1_FLT_TO_INT, tgsi_op2},
	[TGSI_OPCODE_IDIV]	= { ALU_OP0_NOP, tgsi_idiv},
	[TGSI_OPCODE_IMAX]	= { ALU_OP2_MAX_INT, tgsi_op2},
	[TGSI_OPCODE_IMIN]	= { ALU_OP2_MIN_INT, tgsi_op2},
	[TGSI_OPCODE_INEG]	= { ALU_OP2_SUB_INT, tgsi_ineg},
	[TGSI_OPCODE_ISGE]	= { ALU_OP2_SETGE_INT, tgsi_op2},
	[TGSI_OPCODE_ISHR]	= { ALU_OP2_ASHR_INT, tgsi_op2},
	[TGSI_OPCODE_ISLT]	= { ALU_OP2_SETGT_INT, tgsi_op2_swap},
	[TGSI_OPCODE_F2U]	= { ALU_OP1_FLT_TO_UINT, tgsi_op2},
	[TGSI_OPCODE_U2F]	= { ALU_OP1_UINT_TO_FLT, tgsi_op2},
	[TGSI_OPCODE_UADD]	= { ALU_OP2_ADD_INT, tgsi_op2},
	[TGSI_OPCODE_UDIV]	= { ALU_OP0_NOP, tgsi_udiv},
	[TGSI_OPCODE_UMAD]	= { ALU_OP0_NOP, tgsi_umad},
	[TGSI_OPCODE_UMAX]	= { ALU_OP2_MAX_UINT, tgsi_op2},
	[TGSI_OPCODE_UMIN]	= { ALU_OP2_MIN_UINT, tgsi_op2},
	[TGSI_OPCODE_UMOD]	= { ALU_OP0_NOP, tgsi_umod},
	[TGSI_OPCODE_UMUL]	= { ALU_OP2_MULLO_INT, cayman_mul_int_instr},
	[TGSI_OPCODE_USEQ]	= { ALU_OP2_SETE_INT, tgsi_op2},
	[TGSI_OPCODE_USGE]	= { ALU_OP2_SETGE_UINT, tgsi_op2},
	[TGSI_OPCODE_USHR]	= { ALU_OP2_LSHR_INT, tgsi_op2},
	[TGSI_OPCODE_USLT]	= { ALU_OP2_SETGT_UINT, tgsi_op2_swap},
	[TGSI_OPCODE_USNE]	= { ALU_OP2_SETNE_INT, tgsi_op2},
	[TGSI_OPCODE_SWITCH]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_CASE]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_DEFAULT]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_ENDSWITCH]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_I]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_I_MS]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_B]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_C]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_C_LZ]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_D]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_L]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_GATHER4]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SVIEWINFO]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_POS]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_SAMPLE_INFO]	= { 0, tgsi_unsupported},
	[TGSI_OPCODE_UARL]	= { ALU_OP1_MOVA_INT, tgsi_eg_arl},
	[TGSI_OPCODE_UCMP]	= { ALU_OP0_NOP, tgsi_ucmp},
	[TGSI_OPCODE_IABS]	= { 0, tgsi_iabs},
	[TGSI_OPCODE_ISSG]	= { 0, tgsi_issg},
	[TGSI_OPCODE_LOAD]	= { ALU_OP0_NOP, tgsi_load},
	[TGSI_OPCODE_STORE]	= { ALU_OP0_NOP, tgsi_store},
	[163]	= { ALU_OP0_NOP, tgsi_unsupported},
	[164]	= { ALU_OP0_NOP, tgsi_unsupported},
	[165]	= { ALU_OP0_NOP, tgsi_unsupported},
	[TGSI_OPCODE_BARRIER]	= { ALU_OP0_GROUP_BARRIER, tgsi_barrier},
	[TGSI_OPCODE_ATOMUADD]	= { V_RAT_INST_ADD_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMXCHG]	= { V_RAT_INST_XCHG_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMCAS]	= { V_RAT_INST_CMPXCHG_INT_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMAND]	= { V_RAT_INST_AND_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMOR]	= { V_RAT_INST_OR_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMXOR]	= { V_RAT_INST_XOR_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMUMIN]	= { V_RAT_INST_MIN_UINT_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMUMAX]	= { V_RAT_INST_MAX_UINT_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMIMIN]	= { V_RAT_INST_MIN_INT_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_ATOMIMAX]	= { V_RAT_INST_MAX_INT_RTN, tgsi_atomic_op},
	[TGSI_OPCODE_TEX2]	= { FETCH_OP_SAMPLE, tgsi_tex},
	[TGSI_OPCODE_TXB2]	= { FETCH_OP_SAMPLE_LB, tgsi_tex},
	[TGSI_OPCODE_TXL2]	= { FETCH_OP_SAMPLE_L, tgsi_tex},
	[TGSI_OPCODE_IMUL_HI]	= { ALU_OP2_MULHI_INT, cayman_mul_int_instr},
	[TGSI_OPCODE_UMUL_HI]	= { ALU_OP2_MULHI_UINT, cayman_mul_int_instr},
	[TGSI_OPCODE_TG4]	= { FETCH_OP_GATHER4, tgsi_tex},
	[TGSI_OPCODE_LODQ]	= { FETCH_OP_GET_LOD, tgsi_tex},
	[TGSI_OPCODE_IBFE]	= { ALU_OP3_BFE_INT, tgsi_bfe},
	[TGSI_OPCODE_UBFE]	= { ALU_OP3_BFE_UINT, tgsi_bfe},
	[TGSI_OPCODE_BFI]	= { ALU_OP0_NOP, tgsi_bfi},
	[TGSI_OPCODE_BREV]	= { ALU_OP1_BFREV_INT, tgsi_op2},
	[TGSI_OPCODE_POPC]	= { ALU_OP1_BCNT_INT, tgsi_op2},
	[TGSI_OPCODE_LSB]	= { ALU_OP1_FFBL_INT, tgsi_op2},
	[TGSI_OPCODE_IMSB]	= { ALU_OP1_FFBH_INT, tgsi_msb},
	[TGSI_OPCODE_UMSB]	= { ALU_OP1_FFBH_UINT, tgsi_msb},
	[TGSI_OPCODE_INTERP_CENTROID]	= { ALU_OP0_NOP, tgsi_interp_egcm},
	[TGSI_OPCODE_INTERP_SAMPLE]	= { ALU_OP0_NOP, tgsi_interp_egcm},
	[TGSI_OPCODE_INTERP_OFFSET]	= { ALU_OP0_NOP, tgsi_interp_egcm},
	[TGSI_OPCODE_F2D]	= { ALU_OP1_FLT32_TO_FLT64, tgsi_op2_64},
	[TGSI_OPCODE_D2F]	= { ALU_OP1_FLT64_TO_FLT32, tgsi_op2_64_single_dest},
	[TGSI_OPCODE_DABS]	= { ALU_OP1_MOV, tgsi_op2_64},
	[TGSI_OPCODE_DNEG]	= { ALU_OP2_ADD_64, tgsi_dneg},
	[TGSI_OPCODE_DADD]	= { ALU_OP2_ADD_64, tgsi_op2_64},
	[TGSI_OPCODE_DMUL]	= { ALU_OP2_MUL_64, cayman_mul_double_instr},
	[TGSI_OPCODE_DDIV]	= { 0, cayman_ddiv_instr },
	[TGSI_OPCODE_DMAX]	= { ALU_OP2_MAX_64, tgsi_op2_64},
	[TGSI_OPCODE_DMIN]	= { ALU_OP2_MIN_64, tgsi_op2_64},
	[TGSI_OPCODE_DSLT]	= { ALU_OP2_SETGT_64, tgsi_op2_64_single_dest_s},
	[TGSI_OPCODE_DSGE]	= { ALU_OP2_SETGE_64, tgsi_op2_64_single_dest},
	[TGSI_OPCODE_DSEQ]	= { ALU_OP2_SETE_64, tgsi_op2_64_single_dest},
	[TGSI_OPCODE_DSNE]	= { ALU_OP2_SETNE_64, tgsi_op2_64_single_dest},
	[TGSI_OPCODE_DRCP]	= { ALU_OP2_RECIP_64, cayman_emit_double_instr},
	[TGSI_OPCODE_DSQRT]	= { ALU_OP2_SQRT_64, cayman_emit_double_instr},
	[TGSI_OPCODE_DMAD]	= { ALU_OP3_FMA_64, tgsi_op3_64},
	[TGSI_OPCODE_DFMA]	= { ALU_OP3_FMA_64, tgsi_op3_64},
	[TGSI_OPCODE_DFRAC]	= { ALU_OP1_FRACT_64, tgsi_op2_64},
	[TGSI_OPCODE_DLDEXP]	= { ALU_OP2_LDEXP_64, tgsi_op2_64},
	[TGSI_OPCODE_DFRACEXP]	= { ALU_OP1_FREXP_64, tgsi_dfracexp},
	[TGSI_OPCODE_D2I]	= { ALU_OP1_FLT_TO_INT, egcm_double_to_int},
	[TGSI_OPCODE_I2D]	= { ALU_OP1_INT_TO_FLT, egcm_int_to_double},
	[TGSI_OPCODE_D2U]	= { ALU_OP1_FLT_TO_UINT, egcm_double_to_int},
	[TGSI_OPCODE_U2D]	= { ALU_OP1_UINT_TO_FLT, egcm_int_to_double},
	[TGSI_OPCODE_DRSQ]	= { ALU_OP2_RECIPSQRT_64, cayman_emit_double_instr},
	[TGSI_OPCODE_U64SNE]    = { ALU_OP0_NOP, egcm_u64sne },
	[TGSI_OPCODE_U64ADD]    = { ALU_OP0_NOP, egcm_u64add },
	[TGSI_OPCODE_U64MUL]    = { ALU_OP0_NOP, egcm_u64mul },
	[TGSI_OPCODE_U64DIV]    = { ALU_OP0_NOP, egcm_u64div },
	[TGSI_OPCODE_LAST]	= { ALU_OP0_NOP, tgsi_unsupported},
};
