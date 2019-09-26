/*
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
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

#include "compiler/nir/nir_builder.h"
#include "bifrost_compile.h"
#include "bifrost_opts.h"
#include "bifrost_sched.h"
#include "compiler_defines.h"
#include "disassemble.h"
#include "bifrost_print.h"

#define BI_DEBUG

static int
glsl_type_size(const struct glsl_type *type, bool bindless)
{
        return glsl_count_attribute_slots(type, false);
}

static void
optimize_nir(nir_shader *nir)
{
        bool progress;

        NIR_PASS_V(nir, nir_lower_io, nir_var_all, glsl_type_size, 0);
        NIR_PASS(progress, nir, nir_lower_regs_to_ssa);

        do {
                progress = false;

                NIR_PASS(progress, nir, nir_lower_io, nir_var_all, glsl_type_size, 0);

                NIR_PASS(progress, nir, nir_lower_var_copies);
                NIR_PASS(progress, nir, nir_lower_vars_to_ssa);

                NIR_PASS(progress, nir, nir_copy_prop);
                NIR_PASS(progress, nir, nir_opt_constant_folding);

                NIR_PASS(progress, nir, nir_lower_vars_to_ssa);
                NIR_PASS(progress, nir, nir_lower_alu_to_scalar, NULL, NULL);
                NIR_PASS(progress, nir, nir_opt_if, true);

        } while (progress);

        NIR_PASS(progress, nir, nir_copy_prop);
        NIR_PASS(progress, nir, nir_opt_dce);
}

static unsigned
nir_src_index(compiler_context *ctx, nir_src *src)
{
        if (src->is_ssa)
                return src->ssa->index;
        else
                return ctx->func->impl->ssa_alloc + src->reg.reg->index;
}

static unsigned
nir_dest_index(compiler_context *ctx, nir_dest *dst)
{
        if (dst->is_ssa)
                return dst->ssa.index;
        else
                return ctx->func->impl->ssa_alloc + dst->reg.reg->index;
}

static unsigned
nir_alu_src_index(compiler_context *ctx, nir_alu_src *src)
{
        return nir_src_index(ctx, &src->src);
}

struct bifrost_instruction *
mir_alloc_ins(struct bifrost_instruction instr)
{
        struct bifrost_instruction *heap_ins = malloc(sizeof(instr));
        memcpy(heap_ins, &instr, sizeof(instr));
        return heap_ins;
}

static void
emit_mir_instruction(struct compiler_context *ctx, struct bifrost_instruction instr)
{
        list_addtail(&(mir_alloc_ins(instr))->link, &ctx->current_block->instructions);
}

static void
bifrost_block_add_successor(bifrost_block *block, bifrost_block *successor)
{
        assert(block->num_successors < ARRAY_SIZE(block->successors));
        block->successors[block->num_successors++] = successor;
}

static void
emit_load_const(struct compiler_context *ctx, nir_load_const_instr *instr)
{
        nir_ssa_def def = instr->def;

        float *v = ralloc_array(NULL, float, 1);
        nir_const_value_to_array(v, instr->value, instr->def.num_components, f32);
        _mesa_hash_table_u64_insert(ctx->ssa_constants, def.index + 1, v);
}

static uint32_t
alloc_mir_temp(struct compiler_context *ctx)
{
        return SSA_TEMP_VALUE(ctx->mir_temp++);
}

static uint32_t
emit_ld_vary_addr_constant(struct compiler_context *ctx, uint32_t location)
{
        // LD_VAR_ADDR.f32 {R0, T1}, R61, R62, location:1, R12
        // ...
        // ST_VAR.v4 T1, R12, R13, R14, R4

        // R61-R62 is filled with information needed for varying interpolation
        // This loads a vec3 with the information that ST_VAR needs to work

        uint32_t mir_temp_location = alloc_mir_temp(ctx);
        // This instruction loads a vec3 starting from the initial register
        struct bifrost_instruction instr = {
                .op = op_ld_var_addr,
                .dest_components = 3,
                .ssa_args = {
                        .dest = mir_temp_location,
                        .src0 = SSA_FIXED_REGISTER(61),
                        .src1 = SSA_FIXED_REGISTER(62),
                        .src2 = SSA_INVALID_VALUE,
                        .src3 = SSA_INVALID_VALUE,
                },
                .literal_args[0] = location,
        };
        emit_mir_instruction(ctx, instr);

        return mir_temp_location;
}

// XXX: Doesn't support duplicated values in the components!
// RA WILL fail!
static void
emit_create_vector(struct compiler_context *ctx, unsigned dest, unsigned num_comps, uint32_t *comps)
{
        assert(num_comps <= 4 && "Can't make a vector larger than 4 components");

        // This instruction loads a vec3 starting from the initial register
        struct bifrost_instruction instr = {
                .op = op_create_vector,
                .dest_components = num_comps,
                .ssa_args = {
                        .dest = dest,
                }
        };

        uint32_t *srcs[4] = {
                &instr.ssa_args.src0,
                &instr.ssa_args.src1,
                &instr.ssa_args.src2,
                &instr.ssa_args.src3,
        };

        for (unsigned i = 0; i < 4; ++i) {
                if (i < num_comps)
                        *srcs[i] = comps[i];
                else
                        *srcs[i] = SSA_INVALID_VALUE;
        }
        emit_mir_instruction(ctx, instr);
}

static uint32_t
emit_extract_vector_element(struct compiler_context *ctx, unsigned ssa_vector, unsigned element)
{
        uint32_t mir_temp_location = alloc_mir_temp(ctx);
        // This instruction loads a vec3 starting from the initial register
        struct bifrost_instruction instr = {
                .op = op_extract_element,
                .dest_components = 1,
                .ssa_args = {
                        .dest = mir_temp_location,
                        .src0 = ssa_vector,
                        .src1 = SSA_INVALID_VALUE,
                        .src2 = SSA_INVALID_VALUE,
                        .src3 = SSA_INVALID_VALUE,
                },
                .literal_args[0] = element,
        };
        emit_mir_instruction(ctx, instr);

        return mir_temp_location;
}
static uint32_t
emit_movi(struct compiler_context *ctx, uint32_t literal)
{
        uint32_t mir_temp_location = alloc_mir_temp(ctx);
        // This instruction loads a vec3 starting from the initial register
        struct bifrost_instruction instr = {
                .op = op_movi,
                .dest_components = 1,
                .ssa_args = {
                        .dest = mir_temp_location,
                        .src0 = SSA_INVALID_VALUE,
                        .src1 = SSA_INVALID_VALUE,
                        .src2 = SSA_INVALID_VALUE,
                        .src3 = SSA_INVALID_VALUE,
                },
                .literal_args[0] = literal,
        };
        emit_mir_instruction(ctx, instr);

        return mir_temp_location;
}

static unsigned
nir_alu_src_index_scalar(compiler_context *ctx, nir_alu_instr *nir_instr, unsigned src)
{
        // NIR uses a combination of single channels plus swizzles to determine which component is pulled out of a source
        for (unsigned c = 0; c < NIR_MAX_VEC_COMPONENTS; c++) {
                if (!nir_alu_instr_channel_used(nir_instr, src, c))
                        continue;
                // Pull the swizzle from this element that is active and use it as the source
                unsigned element = nir_instr->src[src].swizzle[c];

                // Create an op that extracts an element from a vector
                return emit_extract_vector_element(ctx, nir_alu_src_index(ctx, &nir_instr->src[src]), element);
        }
        assert(0);
        return 0;
}

static void
emit_intrinsic(struct compiler_context *ctx, nir_intrinsic_instr *nir_instr)
{
        nir_const_value *const_offset;
        unsigned offset, reg;

        switch (nir_instr->intrinsic) {
        case nir_intrinsic_load_ubo: {
                nir_const_value *location = nir_src_as_const_value(nir_instr->src[0]);
                const_offset = nir_src_as_const_value(nir_instr->src[1]);
                assert (location && "no indirect ubo selection");
                assert (const_offset && "no indirect inputs");

                enum bifrost_ir_ops op;

                // load_ubo <UBO binding>, <byte offset>
                // ld_ubo <byte offset>, <UBO binding>
                switch (nir_dest_num_components(nir_instr->dest)) {
                case 1:
                        op = op_ld_ubo_v1;
                        break;
                case 2:
                        op = op_ld_ubo_v2;
                        break;
                case 3:
                        op = op_ld_ubo_v3;
                        break;
                case 4:
                        op = op_ld_ubo_v4;
                        break;
                default:
                        assert(0);
                        break;
                }

                reg = nir_dest_index(ctx, &nir_instr->dest);
                struct bifrost_instruction instr = {
                        .op = op,
                        .dest_components = nir_dest_num_components(nir_instr->dest),
                        .ssa_args = {
                                .dest = reg,
                                .src0 = SSA_INVALID_VALUE,
                                .src1 = SSA_INVALID_VALUE,
                                .src2 = SSA_INVALID_VALUE,
                                .src3 = SSA_INVALID_VALUE,
                        },
                        .literal_args[0] = nir_src_as_uint(nir_instr->src[1]),
                        .literal_args[1] = nir_src_as_uint(nir_instr->src[0]),
                };

                emit_mir_instruction(ctx, instr);
                break;
        }
        case nir_intrinsic_store_ssbo: {
                nir_const_value *location = nir_src_as_const_value(nir_instr->src[1]);
                const_offset = nir_src_as_const_value(nir_instr->src[2]);
                assert (location && "no indirect ubo selection");
                assert (const_offset && "no indirect inputs");

                // store_ssbo <Value>, <binding>, <offset>
                // store_vN <Addr>, <Value>
                reg = nir_src_index(ctx, &nir_instr->src[0]);

                enum bifrost_ir_ops op;
                switch (nir_src_num_components(nir_instr->src[0])) {
                case 1:
                        op = op_store_v1;
                        break;
                case 2:
                        op = op_store_v2;
                        break;
                case 3:
                        op = op_store_v3;
                        break;
                case 4:
                        op = op_store_v4;
                        break;
                default:
                        assert(0);
                        break;
                }

                struct bifrost_instruction instr = {
                        .op = op,
                        .dest_components = 0,
                        .ssa_args = {
                                .dest = SSA_INVALID_VALUE,
                                .src0 = reg,
                                .src1 = SSA_INVALID_VALUE,
                                .src2 = SSA_INVALID_VALUE,
                                .src3 = SSA_INVALID_VALUE,
                        },
                        .literal_args[0] = nir_src_as_uint(nir_instr->src[2]),
                };
                emit_mir_instruction(ctx, instr);
                break;
        }
        case nir_intrinsic_load_uniform:
                offset = nir_intrinsic_base(nir_instr);

                if (nir_src_is_const(nir_instr->src[0])) {
                        offset += nir_src_as_uint(nir_instr->src[0]);
                } else {
                        assert(0 && "Can't handle indirect load_uniform");
                }

                reg = nir_dest_index(ctx, &nir_instr->dest);

                unsigned num_components = nir_dest_num_components(nir_instr->dest);
                if (num_components == 1) {
                        struct bifrost_instruction instr = {
                                .op = op_mov,
                                .dest_components = 1,
                                .ssa_args = {
                                        .dest = reg,
                                        .src0 = SSA_FIXED_UREGISTER(offset),
                                        .src1 = SSA_INVALID_VALUE,
                                        .src2 = SSA_INVALID_VALUE,
                                        .src3 = SSA_INVALID_VALUE,
                                },
                        };
                        emit_mir_instruction(ctx, instr);
                } else {
                        uint32_t comps[4];

                        for (unsigned i = 0; i < nir_dest_num_components(nir_instr->dest); ++i) {
                                uint32_t temp_dest = alloc_mir_temp(ctx);
                                comps[i] = temp_dest;
                                struct bifrost_instruction instr = {
                                        .op = op_mov,
                                        .dest_components = 1,
                                        .ssa_args = {
                                                .dest = temp_dest,
                                                .src0 = SSA_FIXED_UREGISTER(offset + (i * 4)),
                                                .src1 = SSA_INVALID_VALUE,
                                                .src2 = SSA_INVALID_VALUE,
                                                .src3 = SSA_INVALID_VALUE,
                                        },
                                };
                                emit_mir_instruction(ctx, instr);
                        }

                        emit_create_vector(ctx, reg, num_components, comps);
                }
                break;

        case nir_intrinsic_load_input: {
                const_offset = nir_src_as_const_value(nir_instr->src[0]);
                assert (const_offset && "no indirect inputs");

                offset = nir_intrinsic_base(nir_instr) + nir_src_as_uint(nir_instr->src[0]);

                reg = nir_dest_index(ctx, &nir_instr->dest);

                enum bifrost_ir_ops op;
                switch (nir_dest_num_components(nir_instr->dest)) {
                case 1:
                        op = op_ld_attr_v1;
                        break;
                case 2:
                        op = op_ld_attr_v2;
                        break;
                case 3:
                        op = op_ld_attr_v3;
                        break;
                case 4:
                        op = op_ld_attr_v4;
                        break;
                default:
                        assert(0);
                        break;
                }

                struct bifrost_instruction instr = {
                        .op = op,
                        .dest_components = nir_dest_num_components(nir_instr->dest),
                        .ssa_args = {
                                .dest = reg,
                                .src0 = offset,
                                .src1 = SSA_INVALID_VALUE,
                                .src2 = SSA_INVALID_VALUE,
                                .src3 = SSA_INVALID_VALUE,
                        }
                };

                emit_mir_instruction(ctx, instr);
                break;
        }
        case nir_intrinsic_store_output: {
                const_offset = nir_src_as_const_value(nir_instr->src[1]);
                assert(const_offset && "no indirect outputs");

                offset = nir_intrinsic_base(nir_instr);
                if (ctx->stage == MESA_SHADER_FRAGMENT) {
                        int comp = nir_intrinsic_component(nir_instr);
                        offset += comp;
                        // XXX: Once we support more than colour output then this will need to change
                        void *entry = _mesa_hash_table_u64_search(ctx->outputs_nir_to_bi, offset + FRAG_RESULT_DATA0 + 1);

                        if (!entry) {
                                printf("WARNING: skipping fragment output\n");
                                break;
                        }

                        offset = (uintptr_t) (entry) - 1;
                        reg = nir_src_index(ctx, &nir_instr->src[0]);

                        enum bifrost_ir_ops op;
                        switch (nir_src_num_components(nir_instr->src[0])) {
                        case 1:
                                op = op_store_v1;
                                break;
                        case 2:
                                op = op_store_v2;
                                break;
                        case 3:
                                op = op_store_v3;
                                break;
                        case 4:
                                op = op_store_v4;
                                break;
                        default:
                                assert(0);
                                break;
                        }

                        // XXX: All offsets aren't vec4 aligned. Will need to adjust this in the future
                        // XXX: This needs to offset correctly in to memory so the blend step can pick it up
                        uint32_t movi = emit_movi(ctx, offset * 16);
                        uint32_t movi2 = emit_movi(ctx, 0);

                        uint32_t comps[2] = {
                                movi, movi2,
                        };
                        uint32_t offset_val = alloc_mir_temp(ctx);
                        emit_create_vector(ctx, offset_val, 2, comps);

                        struct bifrost_instruction instr = {
                                .op = op,
                                .dest_components = 0,
                                .ssa_args = {
                                        .dest = SSA_INVALID_VALUE,
                                        .src0 = offset_val,
                                        .src1 = reg,
                                        .src2 = SSA_INVALID_VALUE,
                                        .src3 = SSA_INVALID_VALUE,
                                }
                        };
                        emit_mir_instruction(ctx, instr);
                } else if (ctx->stage == MESA_SHADER_VERTEX) {
                        int comp = nir_intrinsic_component(nir_instr);
                        offset += comp;
                        void *entry = _mesa_hash_table_u64_search(ctx->varying_nir_to_bi, offset + 2);

                        if (!entry) {
                                printf("WARNING: skipping varying\n");
                                break;
                        }

                        offset = (uintptr_t) (entry) - 1;

                        reg = nir_src_index(ctx, &nir_instr->src[0]);
                        // LD_VAR_ADDR.f32 {R0, T1}, R61, R62, location:1, R12
                        // ...
                        // ST_VAR.v4 T1, R12, R13, R14, R4

                        offset = emit_ld_vary_addr_constant(ctx, offset);
                        enum bifrost_ir_ops op;
                        switch (nir_src_num_components(nir_instr->src[0])) {
                        case 1:
                                op = op_st_vary_v1;
                                break;
                        case 2:
                                op = op_st_vary_v2;
                                break;
                        case 3:
                                op = op_st_vary_v3;
                                break;
                        case 4:
                                op = op_st_vary_v4;
                                break;
                        default:
                                assert(0);
                                break;
                        }

                        struct bifrost_instruction instr = {
                                .op = op,
                                .dest_components = 0,
                                .ssa_args = {
                                        .dest = SSA_INVALID_VALUE,
                                        .src0 = offset,
                                        .src1 = reg,
                                        .src2 = SSA_INVALID_VALUE,
                                        .src3 = SSA_INVALID_VALUE,
                                }
                        };
                        emit_mir_instruction(ctx, instr);
                } else {
                        assert(0 && "Unknown store_output stage");
                }
                break;
        }
        default:
                printf ("Unhandled intrinsic %s\n", nir_intrinsic_infos[nir_instr->intrinsic].name);
                break;
        }
}

#define ALU_CASE(arguments, nir, name) \
	case nir_op_##nir: \
                argument_count = arguments; \
		op = op_##name; \
		break
#define ALU_CASE_MOD(arguments, nir, name, modifiers) \
	case nir_op_##nir: \
                argument_count = arguments; \
		op = op_##name; \
                src_modifiers = modifiers; \
		break

static void
emit_alu(struct compiler_context *ctx, nir_alu_instr *nir_instr)
{
        unsigned dest = nir_dest_index(ctx, &nir_instr->dest.dest);
        unsigned op = ~0U, argument_count;
        unsigned src_modifiers = 0;

        switch (nir_instr->op) {
                ALU_CASE(2, fmul, fmul_f32);
                ALU_CASE(2, fadd, fadd_f32);
                ALU_CASE_MOD(2, fsub, fadd_f32, SOURCE_MODIFIER(1, SRC_MOD_NEG));
                ALU_CASE(1, ftrunc, trunc);
                ALU_CASE(1, fceil, ceil);
                ALU_CASE(1, ffloor, floor);
                ALU_CASE(1, fround_even, roundeven);
                ALU_CASE(1, frcp, frcp_fast_f32);
                ALU_CASE(2, fmax, max_f32);
                ALU_CASE(2, fmin, min_f32);
                ALU_CASE(2, iadd, add_i32);
                ALU_CASE(2, isub, sub_i32);
                ALU_CASE(2, imul, mul_i32);
                ALU_CASE(2, iand, and_i32);
                ALU_CASE(2, ior, or_i32);
                ALU_CASE(2, ixor, xor_i32);
                ALU_CASE(2, ishl, lshift_i32);
                ALU_CASE(2, ushr, rshift_i32);
                ALU_CASE(2, ishr, arshift_i32);
        case nir_op_ineg: {
                unsigned src0 = nir_alu_src_index_scalar(ctx, nir_instr, 0);
                printf("ineg 0x%08x\n", src0);
                struct bifrost_instruction instr = {
                        .op = op_sub_i32,
                        .dest_components = 1,
                        .ssa_args = {
                                .dest = dest,
                                .src0 = SSA_FIXED_CONST_0,
                                .src1 = src0,
                                .src2 = SSA_INVALID_VALUE,
                                .src3 = SSA_INVALID_VALUE,
                        },
                };

                emit_mir_instruction(ctx, instr);
                return;

        }
        case nir_op_vec2: {
                uint32_t comps[3] = {
                        nir_alu_src_index(ctx, &nir_instr->src[0]),
                        nir_alu_src_index(ctx, &nir_instr->src[1]),
                };
                emit_create_vector(ctx, dest, 2, comps);
                return;
                break;
        }
        case nir_op_vec3: {
                uint32_t comps[3] = {
                        nir_alu_src_index(ctx, &nir_instr->src[0]),
                        nir_alu_src_index(ctx, &nir_instr->src[1]),
                        nir_alu_src_index(ctx, &nir_instr->src[2]),
                };
                emit_create_vector(ctx, dest, 3, comps);
                return;
                break;
        }
        case nir_op_vec4: {
                uint32_t comps[4] = {
                        nir_alu_src_index(ctx, &nir_instr->src[0]),
                        nir_alu_src_index(ctx, &nir_instr->src[1]),
                        nir_alu_src_index(ctx, &nir_instr->src[2]),
                        nir_alu_src_index(ctx, &nir_instr->src[3]),
                };
                emit_create_vector(ctx, dest, 4, comps);
                return;
                break;
        }
        case nir_op_fdiv: {
                unsigned src0 = nir_alu_src_index_scalar(ctx, nir_instr, 0);
                unsigned src1 = nir_alu_src_index_scalar(ctx, nir_instr, 1);
                uint32_t mir_temp_location = alloc_mir_temp(ctx);
                {
                        struct bifrost_instruction instr = {
                                .op = op_frcp_fast_f32,
                                .dest_components = 1,
                                .ssa_args = {
                                        .dest = mir_temp_location,
                                        .src0 = src1,
                                        .src1 = SSA_INVALID_VALUE,
                                        .src2 = SSA_INVALID_VALUE,
                                        .src3 = SSA_INVALID_VALUE,
                                },
                        };
                        emit_mir_instruction(ctx, instr);
                }

                struct bifrost_instruction instr = {
                        .op = op_fmul_f32,
                        .dest_components = 1,
                        .ssa_args = {
                                .dest = dest,
                                .src0 = src0,
                                .src1 = src1,
                                .src2 = SSA_INVALID_VALUE,
                                .src3 = SSA_INVALID_VALUE,
                        },
                        .src_modifiers = src_modifiers,
                };

                emit_mir_instruction(ctx, instr);
                return;
                break;
        }
        case nir_op_umin:
        case nir_op_imin:
        case nir_op_umax:
        case nir_op_imax: {
                unsigned src0 = nir_alu_src_index_scalar(ctx, nir_instr, 0);
                unsigned src1 = nir_alu_src_index_scalar(ctx, nir_instr, 1);
                struct bifrost_instruction instr = {
                        .op = op_csel_i32,
                        .dest_components = 1,
                        .ssa_args = {
                                .dest = dest,
                                .src0 = src0,
                                .src1 = src1,
                                .src2 = src0,
                                .src3 = src1,
                        },
                        .src_modifiers = src_modifiers,
                        .literal_args[0] = 0, /* XXX: Comparison operator */
                };

                emit_mir_instruction(ctx, instr);
                return;
                break;
        }
        case nir_op_umin3:
        case nir_op_imin3:
        case nir_op_umax3:
        case nir_op_imax3: {
                unsigned src0 = nir_alu_src_index_scalar(ctx, nir_instr, 0);
                unsigned src1 = nir_alu_src_index_scalar(ctx, nir_instr, 1);
                unsigned src2 = nir_alu_src_index_scalar(ctx, nir_instr, 2);

                unsigned op = 0;
                if (nir_instr->op == nir_op_umin3)
                        op = op_umin3_i32;
                else if (nir_instr->op == nir_op_imin3)
                        op = op_imin3_i32;
                else if (nir_instr->op == nir_op_umax3)
                        op = op_umax3_i32;
                else if (nir_instr->op == nir_op_imax3)
                        op = op_imax3_i32;
                struct bifrost_instruction instr = {
                        .op = op,
                        .dest_components = 1,
                        .ssa_args = {
                                .dest = dest,
                                .src0 = src0,
                                .src1 = src1,
                                .src2 = src2,
                                .src3 = SSA_INVALID_VALUE,
                        },
                        .src_modifiers = src_modifiers,
                };

                emit_mir_instruction(ctx, instr);

                return;
                break;
        }
        case nir_op_ine: {
                uint32_t movi = emit_movi(ctx, ~0U);
                unsigned src0 = nir_alu_src_index(ctx, &nir_instr->src[0]);
                unsigned src1 = nir_alu_src_index(ctx, &nir_instr->src[1]);
                struct bifrost_instruction instr = {
                        .op = op_csel_i32,
                        .dest_components = 1,
                        .ssa_args = {
                                .dest = dest,
                                .src0 = src0,
                                .src1 = src1,
                                .src2 = movi,
                                .src3 = SSA_FIXED_CONST_0,
                        },
                        .src_modifiers = src_modifiers,
                        .literal_args[0] = CSEL_IEQ, /* XXX: Comparison operator */
                };

                emit_mir_instruction(ctx, instr);
                return;
                break;
        }
        default:
                printf("Unhandled ALU op %s\n", nir_op_infos[nir_instr->op].name);
                return;
        }

        unsigned src0 = nir_alu_src_index_scalar(ctx, nir_instr, 0);
        unsigned src1 = argument_count >= 2 ? nir_alu_src_index_scalar(ctx, nir_instr, 1) : SSA_INVALID_VALUE;
        unsigned src2 = argument_count >= 3 ? nir_alu_src_index_scalar(ctx, nir_instr, 2) : SSA_INVALID_VALUE;
        unsigned src3 = argument_count >= 4 ? nir_alu_src_index_scalar(ctx, nir_instr, 3) : SSA_INVALID_VALUE;

        struct bifrost_instruction instr = {
                .op = op,
                .dest_components = 1,
                .ssa_args = {
                        .dest = dest,
                        .src0 = src0,
                        .src1 = src1,
                        .src2 = src2,
                        .src3 = src3,
                },
                .src_modifiers = src_modifiers,
        };

        emit_mir_instruction(ctx, instr);
}

static void
emit_instr(struct compiler_context *ctx, struct nir_instr *instr)
{
        switch (instr->type) {
        case nir_instr_type_load_const:
                emit_load_const(ctx, nir_instr_as_load_const(instr));
                break;
        case nir_instr_type_intrinsic:
                emit_intrinsic(ctx, nir_instr_as_intrinsic(instr));
                break;
        case nir_instr_type_alu:
                emit_alu(ctx, nir_instr_as_alu(instr));
                break;
        case nir_instr_type_tex:
                printf("Unhandled NIR inst tex\n");
                break;
        case nir_instr_type_jump:
                printf("Unhandled NIR inst jump\n");
                break;
        case nir_instr_type_ssa_undef:
                printf("Unhandled NIR inst ssa_undef\n");
                break;
        default:
                printf("Unhandled instruction type\n");
                break;
        }

}

static bifrost_block *
emit_block(struct compiler_context *ctx, nir_block *block)
{
        bifrost_block *this_block = calloc(sizeof(bifrost_block), 1);
        list_addtail(&this_block->link, &ctx->blocks);

        ++ctx->block_count;

        /* Add this block to be a successor to the previous block */
        if (ctx->current_block)
                bifrost_block_add_successor(ctx->current_block, this_block);

        /* Set up current block */
        list_inithead(&this_block->instructions);
        ctx->current_block = this_block;

        nir_foreach_instr(instr, block) {
                emit_instr(ctx, instr);
                ++ctx->instruction_count;
        }

#ifdef BI_DEBUG
        print_mir_block(this_block, false);
#endif
        return this_block;
}

void
emit_if(struct compiler_context *ctx, nir_if *nir_inst);

static struct bifrost_block *
emit_cf_list(struct compiler_context *ctx, struct exec_list *list)
{
        struct bifrost_block *start_block = NULL;
        foreach_list_typed(nir_cf_node, node, node, list) {
                switch (node->type) {
                case nir_cf_node_block: {
                        bifrost_block *block = emit_block(ctx, nir_cf_node_as_block(node));

                        if (!start_block)
                                start_block = block;

                        break;
                }

                case nir_cf_node_if:
                        emit_if(ctx, nir_cf_node_as_if(node));
                        break;

                default:
                case nir_cf_node_loop:
                case nir_cf_node_function:
                        assert(0);
                        break;
                }
        }

        return start_block;
}

void
emit_if(struct compiler_context *ctx, nir_if *nir_inst)
{

        // XXX: Conditional branch instruction can do a variety of comparisons with the sources
        // Merge the source instruction `ine` with our conditional branch
        {
                uint32_t movi = emit_movi(ctx, ~0U);
                struct bifrost_instruction instr = {
                        .op = op_branch,
                        .dest_components = 0,
                        .ssa_args = {
                                .dest = SSA_INVALID_VALUE,
                                .src0 = nir_src_index(ctx, &nir_inst->condition),
                                .src1 = movi,
                                .src2 = SSA_INVALID_VALUE,
                                .src3 = SSA_INVALID_VALUE,
                        },
                        .src_modifiers = 0,
                        .literal_args[0] = BR_COND_EQ, /* XXX: Comparison Arg type */
                        .literal_args[1] = 0, /* XXX: Branch target */
                };

                emit_mir_instruction(ctx, instr);
        }

        bifrost_instruction *true_branch = mir_last_instr_in_block(ctx->current_block);

        bifrost_block *true_block = emit_cf_list(ctx, &nir_inst->then_list);

        {
                struct bifrost_instruction instr = {
                        .op = op_branch,
                        .dest_components = 0,
                        .ssa_args = {
                                .dest = SSA_INVALID_VALUE,
                                .src0 = SSA_INVALID_VALUE,
                                .src1 = SSA_INVALID_VALUE,
                                .src2 = SSA_INVALID_VALUE,
                                .src3 = SSA_INVALID_VALUE,
                        },
                        .src_modifiers = 0,
                        .literal_args[0] = BR_ALWAYS, /* XXX: ALWAYS */
                        .literal_args[1] = 0, /* XXX: Branch target */
                };

                emit_mir_instruction(ctx, instr);
        }
        bifrost_instruction *true_exit_branch = mir_last_instr_in_block(ctx->current_block);

        unsigned false_idx = ctx->block_count;
        unsigned inst_count = ctx->instruction_count;

        bifrost_block *false_block = emit_cf_list(ctx, &nir_inst->else_list);

        unsigned if_footer_idx = ctx->block_count;
        assert(true_block);
        assert(false_block);


        if (ctx->instruction_count == inst_count) {
                // If the else branch didn't have anything in it then we can remove the dead jump
                mir_remove_instr(true_exit_branch);
        } else {
                true_exit_branch->literal_args[1] = if_footer_idx;
        }

        true_branch->literal_args[1] = false_idx;
}

int
bifrost_compile_shader_nir(nir_shader *nir, struct bifrost_program *program)
{
        struct compiler_context ictx = {
                .nir = nir,
                .stage = nir->info.stage,
        };

        struct compiler_context *ctx = &ictx;

        ctx->mir_temp = 0;

        /* Initialize at a global (not block) level hash tables */
        ctx->ssa_constants = _mesa_hash_table_u64_create(NULL);
        ctx->hash_to_temp = _mesa_hash_table_u64_create(NULL);

        /* Assign actual uniform location, skipping over samplers */
        ctx->uniform_nir_to_bi  = _mesa_hash_table_u64_create(NULL);

        nir_foreach_variable(var, &nir->uniforms) {
                if (glsl_get_base_type(var->type) == GLSL_TYPE_SAMPLER) continue;

                for (int col = 0; col < glsl_get_matrix_columns(var->type); ++col) {
                        int id = ctx->uniform_count++;
                        _mesa_hash_table_u64_insert(ctx->uniform_nir_to_bi, var->data.driver_location + col + 1, (void *) ((uintptr_t) (id + 1)));
                }
        }

        if (ctx->stage == MESA_SHADER_VERTEX) {
                ctx->varying_nir_to_bi = _mesa_hash_table_u64_create(NULL);
                nir_foreach_variable(var, &nir->outputs) {
                        if (var->data.location < VARYING_SLOT_VAR0) {
                                if (var->data.location == VARYING_SLOT_POS)
                                        ctx->varying_count++;
                                _mesa_hash_table_u64_insert(ctx->varying_nir_to_bi, var->data.driver_location + 1, (void *) ((uintptr_t) (1)));

                                continue;
                        }

                        for (int col = 0; col < glsl_get_matrix_columns(var->type); ++col) {
                                for (int comp = 0; comp < 4; ++comp) {
                                        int id = comp + ctx->varying_count++;
                                        _mesa_hash_table_u64_insert(ctx->varying_nir_to_bi, var->data.driver_location + col + comp + 1, (void *) ((uintptr_t) (id + 1)));
                                }
                        }
                }

        } else if (ctx->stage == MESA_SHADER_FRAGMENT) {
                ctx->outputs_nir_to_bi = _mesa_hash_table_u64_create(NULL);
                nir_foreach_variable(var, &nir->outputs) {
                        if (var->data.location >= FRAG_RESULT_DATA0 && var->data.location <= FRAG_RESULT_DATA7) {
                                int id = ctx->outputs_count++;
                                printf("Driver location: %d with id %d\n", var->data.location + 1, id);
                                _mesa_hash_table_u64_insert(ctx->outputs_nir_to_bi, var->data.location + 1, (void *) ((uintptr_t) (id + 1)));
                        }
                }
        }

        /* Optimisation passes */
        optimize_nir(nir);

#ifdef BI_DEBUG
        nir_print_shader(nir, stdout);
#endif

        /* Generate machine IR for shader */
        nir_foreach_function(func, nir) {
                nir_builder _b;
                ctx->b = &_b;
                nir_builder_init(ctx->b, func->impl);

                list_inithead(&ctx->blocks);
                ctx->block_count = 0;
                ctx->func = func;

                emit_cf_list(ctx, &func->impl->body);

                break; // XXX: Once we support multi function shaders then implement
        }

        util_dynarray_init(&program->compiled, NULL);

        // MIR pre-RA optimizations

        bool progress = false;

        do {
                progress = false;
                mir_foreach_block(ctx, block) {
                        // XXX: Not yet working
//                        progress |= bifrost_opt_branch_fusion(ctx, block);
                }
        } while (progress);

        schedule_program(ctx);

#ifdef BI_DEBUG
        nir_print_shader(nir, stdout);
        disassemble_bifrost(program->compiled.data, program->compiled.size, false);
#endif
        return 0;
}
