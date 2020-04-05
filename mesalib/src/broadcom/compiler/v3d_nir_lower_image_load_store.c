/*
 * Copyright © 2018 Intel Corporation
 * Copyright © 2018 Broadcom
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

#include "v3d_compiler.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_format_convert.h"

/** @file v3d_nir_lower_image_load_store.c
 *
 * Performs any necessary lowering of GL_ARB_shader_image_load_store
 * operations.
 *
 * On V3D 4.x, we just need to do format conversion for stores such that the
 * GPU can effectively memcpy the arguments (in increments of 32-bit words)
 * into the texel.  Loads are the same as texturing, where we may need to
 * unpack from 16-bit ints or floats.
 *
 * On V3D 3.x, to implement image load store we would need to do manual tiling
 * calculations and load/store using the TMU general memory access path.
 */

bool
v3d_gl_format_is_return_32(enum pipe_format format)
{
        const struct util_format_description *desc =
                util_format_description(format);
        const struct util_format_channel_description *chan = &desc->channel[0];

        return chan->size > 16 || (chan->size == 16 && chan->normalized);
}

/* Packs a 32-bit vector of colors in the range [0, (1 << bits[i]) - 1] to a
 * 32-bit SSA value, with as many channels as necessary to store all the bits
 */
static nir_ssa_def *
pack_bits(nir_builder *b, nir_ssa_def *color, const unsigned *bits,
          int num_components, bool mask)
{
        nir_ssa_def *results[4];
        int offset = 0;
        for (int i = 0; i < num_components; i++) {
                nir_ssa_def *chan = nir_channel(b, color, i);

                /* Channels being stored shouldn't cross a 32-bit boundary. */
                assert((offset & ~31) == ((offset + bits[i] - 1) & ~31));

                if (mask) {
                        chan = nir_iand(b, chan,
                                        nir_imm_int(b, (1 << bits[i]) - 1));
                }

                if (offset % 32 == 0) {
                        results[offset / 32] = chan;
                } else {
                        results[offset / 32] =
                                nir_ior(b, results[offset / 32],
                                        nir_ishl(b, chan,
                                                 nir_imm_int(b, offset % 32)));
                }
                offset += bits[i];
        }

        return nir_vec(b, results, DIV_ROUND_UP(offset, 32));
}

static void
v3d_nir_lower_image_store(nir_builder *b, nir_intrinsic_instr *instr)
{
        enum pipe_format format = nir_intrinsic_format(instr);
        const struct util_format_description *desc =
                util_format_description(format);
        const struct util_format_channel_description *r_chan = &desc->channel[0];
        unsigned num_components = util_format_get_nr_components(format);

        b->cursor = nir_before_instr(&instr->instr);

        nir_ssa_def *color = nir_channels(b,
                                          nir_ssa_for_src(b, instr->src[3], 4),
                                          (1 << num_components) - 1);
        nir_ssa_def *formatted = NULL;

        if (format == PIPE_FORMAT_R11G11B10_FLOAT) {
                formatted = nir_format_pack_11f11f10f(b, color);
        } else if (format == PIPE_FORMAT_R9G9B9E5_FLOAT) {
                formatted = nir_format_pack_r9g9b9e5(b, color);
        } else if (r_chan->size == 32) {
                /* For 32-bit formats, we just have to move the vector
                 * across (possibly reducing the number of channels).
                 */
                formatted = color;
        } else {
                static const unsigned bits_8[4] = {8, 8, 8, 8};
                static const unsigned bits_16[4] = {16, 16, 16, 16};
                static const unsigned bits_1010102[4] = {10, 10, 10, 2};
                const unsigned *bits;

                switch (r_chan->size) {
                case 8:
                        bits = bits_8;
                        break;
                case 10:
                        bits = bits_1010102;
                        break;
                case 16:
                        bits = bits_16;
                        break;
                default:
                        unreachable("unrecognized bits");
                }

                bool pack_mask = false;
                if (r_chan->pure_integer &&
                    r_chan->type == UTIL_FORMAT_TYPE_SIGNED) {
                        formatted = nir_format_clamp_sint(b, color, bits);
                        pack_mask = true;
                } else if (r_chan->pure_integer &&
                           r_chan->type == UTIL_FORMAT_TYPE_UNSIGNED) {
                        formatted = nir_format_clamp_uint(b, color, bits);
                } else if (r_chan->normalized &&
                           r_chan->type == UTIL_FORMAT_TYPE_SIGNED) {
                        formatted = nir_format_float_to_snorm(b, color, bits);
                        pack_mask = true;
                } else if (r_chan->normalized &&
                           r_chan->type == UTIL_FORMAT_TYPE_UNSIGNED) {
                        formatted = nir_format_float_to_unorm(b, color, bits);
                } else {
                        assert(r_chan->size == 16);
                        assert(r_chan->type == UTIL_FORMAT_TYPE_FLOAT);
                        formatted = nir_format_float_to_half(b, color);
                }

                formatted = pack_bits(b, formatted, bits, num_components,
                                      pack_mask);
        }

        nir_instr_rewrite_src(&instr->instr, &instr->src[3],
                              nir_src_for_ssa(formatted));
        instr->num_components = formatted->num_components;
}

static void
v3d_nir_lower_image_load(nir_builder *b, nir_intrinsic_instr *instr)
{
        static const unsigned bits16[] = {16, 16, 16, 16};
        enum pipe_format format = nir_intrinsic_format(instr);

        if (v3d_gl_format_is_return_32(format))
                return;

        b->cursor = nir_after_instr(&instr->instr);

        assert(instr->dest.is_ssa);
        nir_ssa_def *result = &instr->dest.ssa;
        if (util_format_is_pure_uint(format)) {
                result = nir_format_unpack_uint(b, result, bits16, 4);
        } else if (util_format_is_pure_sint(format)) {
                result = nir_format_unpack_sint(b, result, bits16, 4);
        } else {
            nir_ssa_def *rg = nir_channel(b, result, 0);
            nir_ssa_def *ba = nir_channel(b, result, 1);
            result = nir_vec4(b,
                              nir_unpack_half_2x16_split_x(b, rg),
                              nir_unpack_half_2x16_split_y(b, rg),
                              nir_unpack_half_2x16_split_x(b, ba),
                              nir_unpack_half_2x16_split_y(b, ba));
        }

        nir_ssa_def_rewrite_uses_after(&instr->dest.ssa, nir_src_for_ssa(result),
                                       result->parent_instr);
}

void
v3d_nir_lower_image_load_store(nir_shader *s)
{
        nir_foreach_function(function, s) {
                if (!function->impl)
                        continue;

                nir_builder b;
                nir_builder_init(&b, function->impl);

                nir_foreach_block(block, function->impl) {
                        nir_foreach_instr_safe(instr, block) {
                                if (instr->type != nir_instr_type_intrinsic)
                                        continue;

                                nir_intrinsic_instr *intr =
                                        nir_instr_as_intrinsic(instr);

                                switch (intr->intrinsic) {
                                case nir_intrinsic_image_load:
                                        v3d_nir_lower_image_load(&b, intr);
                                        break;
                                case nir_intrinsic_image_store:
                                        v3d_nir_lower_image_store(&b, intr);
                                        break;
                                default:
                                        break;
                                }
                        }
                }

                nir_metadata_preserve(function->impl,
                                      nir_metadata_block_index |
                                      nir_metadata_dominance);
        }
}
