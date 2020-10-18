/*
 * Copyright © 2019 Broadcom
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

/**
 * Implements lowering for logical operations.
 *
 * V3D doesn't have any hardware support for logic ops.  Instead, you read the
 * current contents of the destination from the tile buffer, then do math using
 * your output color and that destination value, and update the output color
 * appropriately.
 */

#include "util/format/u_format.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_format_convert.h"
#include "v3d_compiler.h"


typedef nir_ssa_def *(*nir_pack_func)(nir_builder *b, nir_ssa_def *c);
typedef nir_ssa_def *(*nir_unpack_func)(nir_builder *b, nir_ssa_def *c);

static bool
logicop_depends_on_dst_color(int logicop_func)
{
        switch (logicop_func) {
        case PIPE_LOGICOP_SET:
        case PIPE_LOGICOP_CLEAR:
        case PIPE_LOGICOP_COPY:
        case PIPE_LOGICOP_COPY_INVERTED:
                return false;
        default:
                return true;
        }
}

static nir_ssa_def *
v3d_logicop(nir_builder *b, int logicop_func,
            nir_ssa_def *src, nir_ssa_def *dst)
{
        switch (logicop_func) {
        case PIPE_LOGICOP_CLEAR:
                return nir_imm_int(b, 0);
        case PIPE_LOGICOP_NOR:
                return nir_inot(b, nir_ior(b, src, dst));
        case PIPE_LOGICOP_AND_INVERTED:
                return nir_iand(b, nir_inot(b, src), dst);
        case PIPE_LOGICOP_COPY_INVERTED:
                return nir_inot(b, src);
        case PIPE_LOGICOP_AND_REVERSE:
                return nir_iand(b, src, nir_inot(b, dst));
        case PIPE_LOGICOP_INVERT:
                return nir_inot(b, dst);
        case PIPE_LOGICOP_XOR:
                return nir_ixor(b, src, dst);
        case PIPE_LOGICOP_NAND:
                return nir_inot(b, nir_iand(b, src, dst));
        case PIPE_LOGICOP_AND:
                return nir_iand(b, src, dst);
        case PIPE_LOGICOP_EQUIV:
                return nir_inot(b, nir_ixor(b, src, dst));
        case PIPE_LOGICOP_NOOP:
                return dst;
        case PIPE_LOGICOP_OR_INVERTED:
                return nir_ior(b, nir_inot(b, src), dst);
        case PIPE_LOGICOP_OR_REVERSE:
                return nir_ior(b, src, nir_inot(b, dst));
        case PIPE_LOGICOP_OR:
                return nir_ior(b, src, dst);
        case PIPE_LOGICOP_SET:
                return nir_imm_int(b, ~0);
        default:
                fprintf(stderr, "Unknown logic op %d\n", logicop_func);
                /* FALLTHROUGH */
        case PIPE_LOGICOP_COPY:
                return src;
        }
}

static nir_ssa_def *
v3d_nir_get_swizzled_channel(nir_builder *b, nir_ssa_def **srcs, int swiz)
{
        switch (swiz) {
        default:
        case PIPE_SWIZZLE_NONE:
                fprintf(stderr, "warning: unknown swizzle\n");
                /* FALLTHROUGH */
        case PIPE_SWIZZLE_0:
                return nir_imm_float(b, 0.0);
        case PIPE_SWIZZLE_1:
                return nir_imm_float(b, 1.0);
        case PIPE_SWIZZLE_X:
        case PIPE_SWIZZLE_Y:
        case PIPE_SWIZZLE_Z:
        case PIPE_SWIZZLE_W:
                return srcs[swiz];
        }
}

static nir_ssa_def *
v3d_nir_swizzle_and_pack(nir_builder *b, nir_ssa_def **chans,
                         const uint8_t *swiz, nir_pack_func pack_func)
{
        nir_ssa_def *c[4];
        for (int i = 0; i < 4; i++)
                c[i] = v3d_nir_get_swizzled_channel(b, chans, swiz[i]);

        return pack_func(b, nir_vec4(b, c[0], c[1], c[2], c[3]));
}

static nir_ssa_def *
v3d_nir_unpack_and_swizzle(nir_builder *b, nir_ssa_def *packed,
                           const uint8_t *swiz, nir_unpack_func unpack_func)
{
        nir_ssa_def *unpacked = unpack_func(b, packed);

        nir_ssa_def *unpacked_chans[4];
        for (int i = 0; i < 4; i++)
                unpacked_chans[i] = nir_channel(b, unpacked, i);

        nir_ssa_def *c[4];
        for (int i = 0; i < 4; i++)
                c[i] = v3d_nir_get_swizzled_channel(b, unpacked_chans, swiz[i]);

        return nir_vec4(b, c[0], c[1], c[2], c[3]);
}

static nir_ssa_def *
pack_unorm_rgb10a2(nir_builder *b, nir_ssa_def *c)
{
        const unsigned bits[4] = { 10, 10, 10, 2 };
        nir_ssa_def *unorm = nir_format_float_to_unorm(b, c, bits);

        nir_ssa_def *chans[4];
        for (int i = 0; i < 4; i++)
                chans[i] = nir_channel(b, unorm, i);

        nir_ssa_def *result = nir_mov(b, chans[0]);
        int offset = bits[0];
        for (int i = 1; i < 4; i++) {
                nir_ssa_def *shifted_chan =
                        nir_ishl(b, chans[i], nir_imm_int(b, offset));
                result = nir_ior(b, result, shifted_chan);
                offset += bits[i];
        }
        return result;
}

static nir_ssa_def *
unpack_unorm_rgb10a2(nir_builder *b, nir_ssa_def *c)
{
        const unsigned bits[4] = { 10, 10, 10, 2 };
        const unsigned masks[4] = { BITFIELD_MASK(bits[0]),
                                    BITFIELD_MASK(bits[1]),
                                    BITFIELD_MASK(bits[2]),
                                    BITFIELD_MASK(bits[3]) };

        nir_ssa_def *chans[4];
        for (int i = 0; i < 4; i++) {
                nir_ssa_def *unorm = nir_iand(b, c, nir_imm_int(b, masks[i]));
                chans[i] = nir_format_unorm_to_float(b, unorm, &bits[i]);
                c = nir_ushr(b, c, nir_imm_int(b, bits[i]));
        }

        return nir_vec4(b, chans[0], chans[1], chans[2], chans[3]);
}

static const uint8_t *
v3d_get_format_swizzle_for_rt(struct v3d_compile *c, int rt)
{
        static const uint8_t ident[4] = { 0, 1, 2, 3 };

        /* We will automatically swap R and B channels for BGRA formats
         * on tile loads and stores (see 'swap_rb' field in v3d_resource) so
         * we want to treat these surfaces as if they were regular RGBA formats.
         */
        if (c->fs_key->color_fmt[rt].swizzle[0] == 2 &&
            c->fs_key->color_fmt[rt].format != PIPE_FORMAT_B5G6R5_UNORM) {
                return ident;
        } else {
                return  c->fs_key->color_fmt[rt].swizzle;
        }
}

static nir_ssa_def *
v3d_nir_get_tlb_color(nir_builder *b, int rt, int sample)
{
        nir_ssa_def *color[4];
        for (int i = 0; i < 4; i++) {
                nir_intrinsic_instr *load =
                        nir_intrinsic_instr_create(b->shader,
                                                   nir_intrinsic_load_tlb_color_v3d);
                load->num_components = 1;
                nir_intrinsic_set_base(load, sample);
                nir_intrinsic_set_component(load, i);
                load->src[0] = nir_src_for_ssa(nir_imm_int(b, rt));
                nir_ssa_dest_init(&load->instr, &load->dest, 1, 32, NULL);
                nir_builder_instr_insert(b, &load->instr);
                color[i] = &load->dest.ssa;
        }

        return nir_vec4(b, color[0], color[1], color[2], color[3]);
}

static nir_ssa_def *
v3d_emit_logic_op_raw(struct v3d_compile *c, nir_builder *b,
                      nir_ssa_def **src_chans, nir_ssa_def **dst_chans,
                      int rt, int sample)
{
        const uint8_t *fmt_swz = v3d_get_format_swizzle_for_rt(c, rt);

        nir_ssa_def *op_res[4];
        for (int i = 0; i < 4; i++) {
                nir_ssa_def *src = src_chans[i];
                nir_ssa_def *dst =
                        v3d_nir_get_swizzled_channel(b, dst_chans, fmt_swz[i]);
                op_res[i] = v3d_logicop(b, c->fs_key->logicop_func, src, dst);
        }

        nir_ssa_def *r[4];
        for (int i = 0; i < 4; i++)
                r[i] = v3d_nir_get_swizzled_channel(b, op_res, fmt_swz[i]);

        return nir_vec4(b, r[0], r[1], r[2], r[3]);
}

static nir_ssa_def *
v3d_emit_logic_op_unorm(struct v3d_compile *c, nir_builder *b,
                        nir_ssa_def **src_chans, nir_ssa_def **dst_chans,
                        int rt, int sample,
                        nir_pack_func pack_func, nir_unpack_func unpack_func)
{
        const uint8_t src_swz[4] = { 0, 1, 2, 3 };
        nir_ssa_def *packed_src =
                v3d_nir_swizzle_and_pack(b, src_chans, src_swz, pack_func);

        const uint8_t *fmt_swz = v3d_get_format_swizzle_for_rt(c, rt);
        nir_ssa_def *packed_dst =
                v3d_nir_swizzle_and_pack(b, dst_chans, fmt_swz, pack_func);

        nir_ssa_def *packed_result =
                v3d_logicop(b, c->fs_key->logicop_func, packed_src, packed_dst);

        return v3d_nir_unpack_and_swizzle(b, packed_result, fmt_swz, unpack_func);
}

static nir_ssa_def *
v3d_nir_emit_logic_op(struct v3d_compile *c, nir_builder *b,
                      nir_ssa_def *src, int rt, int sample)
{
        nir_ssa_def *dst = v3d_nir_get_tlb_color(b, rt, sample);

        nir_ssa_def *src_chans[4], *dst_chans[4];
        for (unsigned i = 0; i < 4; i++) {
                src_chans[i] = nir_channel(b, src, i);
                dst_chans[i] = nir_channel(b, dst, i);
        }

        if (c->fs_key->color_fmt[rt].format == PIPE_FORMAT_R10G10B10A2_UNORM) {
                return v3d_emit_logic_op_unorm(
                        c, b, src_chans, dst_chans, rt, 0,
                        pack_unorm_rgb10a2, unpack_unorm_rgb10a2);
        }

        if (util_format_is_unorm(c->fs_key->color_fmt[rt].format)) {
                return v3d_emit_logic_op_unorm(
                        c, b, src_chans, dst_chans, rt, 0,
                        nir_pack_unorm_4x8, nir_unpack_unorm_4x8);
        }

        return v3d_emit_logic_op_raw(c, b, src_chans, dst_chans, rt, 0);
}

static void
v3d_emit_ms_output(struct v3d_compile *c, nir_builder *b,
                   nir_ssa_def *color, nir_src *offset,
                   nir_alu_type type, int rt, int sample)
{

        nir_intrinsic_instr *store =
                nir_intrinsic_instr_create(b->shader,
                                           nir_intrinsic_store_tlb_sample_color_v3d);
        store->num_components = 4;
        nir_intrinsic_set_base(store, sample);
        nir_intrinsic_set_component(store, 0);
        nir_intrinsic_set_src_type(store, type);
        store->src[0] = nir_src_for_ssa(color);
        store->src[1] = nir_src_for_ssa(nir_imm_int(b, rt));
        nir_builder_instr_insert(b, &store->instr);
}

static void
v3d_nir_lower_logic_op_instr(struct v3d_compile *c,
                             nir_builder *b,
                             nir_intrinsic_instr *intr,
                             int rt)
{
        nir_ssa_def *frag_color = intr->src[0].ssa;


        const int logic_op = c->fs_key->logicop_func;
        if (c->fs_key->msaa && logicop_depends_on_dst_color(logic_op)) {
                c->msaa_per_sample_output = true;

                nir_src *offset = &intr->src[1];
                nir_alu_type type = nir_intrinsic_src_type(intr);
                for (int i = 0; i < V3D_MAX_SAMPLES; i++) {
                        nir_ssa_def *sample =
                                v3d_nir_emit_logic_op(c, b, frag_color, rt, i);

                        v3d_emit_ms_output(c, b, sample, offset, type, rt, i);
                }

                nir_instr_remove(&intr->instr);
        } else {
                nir_ssa_def *result =
                        v3d_nir_emit_logic_op(c, b, frag_color, rt, 0);

                nir_instr_rewrite_src(&intr->instr, &intr->src[0],
                                      nir_src_for_ssa(result));
                intr->num_components = result->num_components;
        }
}

static bool
v3d_nir_lower_logic_ops_block(nir_block *block, struct v3d_compile *c)
{
        nir_foreach_instr_safe(instr, block) {
                if (instr->type != nir_instr_type_intrinsic)
                        continue;

                nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
                if (intr->intrinsic != nir_intrinsic_store_output)
                        continue;

                nir_foreach_shader_out_variable(var, c->s) {
                        const int driver_loc = var->data.driver_location;
                        if (driver_loc != nir_intrinsic_base(intr))
                                continue;

                        const int loc = var->data.location;
                        if (loc != FRAG_RESULT_COLOR &&
                            (loc < FRAG_RESULT_DATA0 ||
                             loc >= FRAG_RESULT_DATA0 + V3D_MAX_DRAW_BUFFERS)) {
                                continue;
                        }

                        /* Logic operations do not apply on floating point or
                         * sRGB enabled render targets.
                         */
                        const int rt = driver_loc;
                        assert(rt < V3D_MAX_DRAW_BUFFERS);

                        const enum pipe_format format =
                                c->fs_key->color_fmt[rt].format;
                        if (util_format_is_float(format) ||
                            util_format_is_srgb(format)) {
                                continue;
                        }

                        nir_function_impl *impl =
                                nir_cf_node_get_function(&block->cf_node);
                        nir_builder b;
                        nir_builder_init(&b, impl);
                        b.cursor = nir_before_instr(&intr->instr);
                        v3d_nir_lower_logic_op_instr(c, &b, intr, rt);
                }
        }

        return true;
}

void
v3d_nir_lower_logic_ops(nir_shader *s, struct v3d_compile *c)
{
        /* Nothing to do if logic op is 'copy src to dst' or if logic ops are
         * disabled (we set the logic op to copy in that case).
         */
        if (c->fs_key->logicop_func == PIPE_LOGICOP_COPY)
                return;

        nir_foreach_function(function, s) {
                if (function->impl) {
                        nir_foreach_block(block, function->impl)
                                v3d_nir_lower_logic_ops_block(block, c);

                        nir_metadata_preserve(function->impl,
                                              nir_metadata_block_index |
                                              nir_metadata_dominance);
                }
        }
}
