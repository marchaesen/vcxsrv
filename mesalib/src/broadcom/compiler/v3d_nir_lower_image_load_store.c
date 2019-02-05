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
v3d_gl_format_is_return_32(GLenum format)
{
        switch (format) {
        case GL_R8:
        case GL_R8_SNORM:
        case GL_R8UI:
        case GL_R8I:
        case GL_RG8:
        case GL_RG8_SNORM:
        case GL_RG8UI:
        case GL_RG8I:
        case GL_RGBA8:
        case GL_RGBA8_SNORM:
        case GL_RGBA8UI:
        case GL_RGBA8I:
        case GL_R11F_G11F_B10F:
        case GL_RGB10_A2:
        case GL_RGB10_A2UI:
        case GL_R16F:
        case GL_R16UI:
        case GL_R16I:
        case GL_RG16F:
        case GL_RG16UI:
        case GL_RG16I:
        case GL_RGBA16F:
        case GL_RGBA16UI:
        case GL_RGBA16I:
                return false;
        case GL_R16:
        case GL_R16_SNORM:
        case GL_RG16:
        case GL_RG16_SNORM:
        case GL_RGBA16:
        case GL_RGBA16_SNORM:
        case GL_R32F:
        case GL_R32UI:
        case GL_R32I:
        case GL_RG32F:
        case GL_RG32UI:
        case GL_RG32I:
        case GL_RGBA32F:
        case GL_RGBA32UI:
        case GL_RGBA32I:
                return true;
        default:
                unreachable("Invalid image format");
        }
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

static nir_ssa_def *
pack_unorm(nir_builder *b, nir_ssa_def *color, const unsigned *bits,
           int num_components)
{
        color = nir_channels(b, color, (1 << num_components) - 1);
        color = nir_format_float_to_unorm(b, color, bits);
        return pack_bits(b, color, bits, color->num_components, false);
}

static nir_ssa_def *
pack_snorm(nir_builder *b, nir_ssa_def *color, const unsigned *bits,
           int num_components)
{
        color = nir_channels(b, color, (1 << num_components) - 1);
        color = nir_format_float_to_snorm(b, color, bits);
        return pack_bits(b, color, bits, color->num_components, true);
}

static nir_ssa_def *
pack_uint(nir_builder *b, nir_ssa_def *color, const unsigned *bits,
          int num_components)
{
        color = nir_channels(b, color, (1 << num_components) - 1);
        color = nir_format_clamp_uint(b, color, bits);
        return pack_bits(b, color, bits, num_components, false);
}

static nir_ssa_def *
pack_sint(nir_builder *b, nir_ssa_def *color, const unsigned *bits,
          int num_components)
{
        color = nir_channels(b, color, (1 << num_components) - 1);
        color = nir_format_clamp_sint(b, color, bits);
        return pack_bits(b, color, bits, num_components, true);
}

static nir_ssa_def *
pack_half(nir_builder *b, nir_ssa_def *color, const unsigned *bits,
          int num_components)
{
        color = nir_channels(b, color, (1 << num_components) - 1);
        color = nir_format_float_to_half(b, color);
        return pack_bits(b, color, bits, color->num_components, false);
}

static void
v3d_nir_lower_image_store(nir_builder *b, nir_intrinsic_instr *instr)
{
        nir_variable *var = nir_intrinsic_get_var(instr, 0);
        GLenum format = var->data.image.format;
        static const unsigned bits_8[4] = {8, 8, 8, 8};
        static const unsigned bits_16[4] = {16, 16, 16, 16};
        static const unsigned bits_1010102[4] = {10, 10, 10, 2};

        b->cursor = nir_before_instr(&instr->instr);

        nir_ssa_def *unformatted = nir_ssa_for_src(b, instr->src[3], 4);
        nir_ssa_def *formatted = NULL;
        switch (format) {
        case GL_RGBA32F:
        case GL_RGBA32UI:
        case GL_RGBA32I:
                /* For 4-component 32-bit components, there's no packing to be
                 * done.
                 */
                return;

        case GL_R32F:
        case GL_R32UI:
        case GL_R32I:
                /* For other 32-bit components, just reduce the size of
                 * the input vector.
                 */
                formatted = nir_channels(b, unformatted, 1);
                break;
        case GL_RG32F:
        case GL_RG32UI:
        case GL_RG32I:
                formatted = nir_channels(b, unformatted, 2);
                break;

        case GL_R8:
                formatted = pack_unorm(b, unformatted, bits_8, 1);
                break;
        case GL_RG8:
                formatted = pack_unorm(b, unformatted, bits_8, 2);
                break;
        case GL_RGBA8:
                formatted = pack_unorm(b, unformatted, bits_8, 4);
                break;

        case GL_R8_SNORM:
                formatted = pack_snorm(b, unformatted, bits_8, 1);
                break;
        case GL_RG8_SNORM:
                formatted = pack_snorm(b, unformatted, bits_8, 2);
                break;
        case GL_RGBA8_SNORM:
                formatted = pack_snorm(b, unformatted, bits_8, 4);
                break;

        case GL_R16:
                formatted = pack_unorm(b, unformatted, bits_16, 1);
                break;
        case GL_RG16:
                formatted = pack_unorm(b, unformatted, bits_16, 2);
                break;
        case GL_RGBA16:
                formatted = pack_unorm(b, unformatted, bits_16, 4);
                break;

        case GL_R16_SNORM:
                formatted = pack_snorm(b, unformatted, bits_16, 1);
                break;
        case GL_RG16_SNORM:
                formatted = pack_snorm(b, unformatted, bits_16, 2);
                break;
        case GL_RGBA16_SNORM:
                formatted = pack_snorm(b, unformatted, bits_16, 4);
                break;

        case GL_R16F:
                formatted = pack_half(b, unformatted, bits_16, 1);
                break;
        case GL_RG16F:
                formatted = pack_half(b, unformatted, bits_16, 2);
                break;
        case GL_RGBA16F:
                formatted = pack_half(b, unformatted, bits_16, 4);
                break;

        case GL_R8UI:
                formatted = pack_uint(b, unformatted, bits_8, 1);
                break;
        case GL_R8I:
                formatted = pack_sint(b, unformatted, bits_8, 1);
                break;
        case GL_RG8UI:
                formatted = pack_uint(b, unformatted, bits_8, 2);
                break;
        case GL_RG8I:
                formatted = pack_sint(b, unformatted, bits_8, 2);
                break;
        case GL_RGBA8UI:
                formatted = pack_uint(b, unformatted, bits_8, 4);
                break;
        case GL_RGBA8I:
                formatted = pack_sint(b, unformatted, bits_8, 4);
                break;

        case GL_R16UI:
                formatted = pack_uint(b, unformatted, bits_16, 1);
                break;
        case GL_R16I:
                formatted = pack_sint(b, unformatted, bits_16, 1);
                break;
        case GL_RG16UI:
                formatted = pack_uint(b, unformatted, bits_16, 2);
                break;
        case GL_RG16I:
                formatted = pack_sint(b, unformatted, bits_16, 2);
                break;
        case GL_RGBA16UI:
                formatted = pack_uint(b, unformatted, bits_16, 4);
                break;
        case GL_RGBA16I:
                formatted = pack_sint(b, unformatted, bits_16, 4);
                break;

        case GL_R11F_G11F_B10F:
                formatted = nir_format_pack_11f11f10f(b, unformatted);
                break;
        case GL_RGB9_E5:
                formatted = nir_format_pack_r9g9b9e5(b, unformatted);
                break;

        case GL_RGB10_A2:
                formatted = pack_unorm(b, unformatted, bits_1010102, 4);
                break;

        case GL_RGB10_A2UI:
                formatted = pack_uint(b, unformatted, bits_1010102, 4);
                break;

        default:
                unreachable("bad format");
        }

        nir_instr_rewrite_src(&instr->instr, &instr->src[3],
                              nir_src_for_ssa(formatted));
        instr->num_components = formatted->num_components;
}

static void
v3d_nir_lower_image_load(nir_builder *b, nir_intrinsic_instr *instr)
{
        static const unsigned bits16[] = {16, 16, 16, 16};
        nir_variable *var = nir_intrinsic_get_var(instr, 0);
        const struct glsl_type *sampler_type = glsl_without_array(var->type);
        enum glsl_base_type base_type =
                glsl_get_sampler_result_type(sampler_type);

        if (v3d_gl_format_is_return_32(var->data.image.format))
                return;

        b->cursor = nir_after_instr(&instr->instr);

        assert(instr->dest.is_ssa);
        nir_ssa_def *result = &instr->dest.ssa;
        if (base_type == GLSL_TYPE_FLOAT) {
            nir_ssa_def *rg = nir_channel(b, result, 0);
            nir_ssa_def *ba = nir_channel(b, result, 1);
            result = nir_vec4(b,
                              nir_unpack_half_2x16_split_x(b, rg),
                              nir_unpack_half_2x16_split_y(b, rg),
                              nir_unpack_half_2x16_split_x(b, ba),
                              nir_unpack_half_2x16_split_y(b, ba));
        } else if (base_type == GLSL_TYPE_INT) {
                result = nir_format_unpack_sint(b, result, bits16, 4);
        } else {
                assert(base_type == GLSL_TYPE_UINT);
                result = nir_format_unpack_uint(b, result, bits16, 4);
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
                                case nir_intrinsic_image_deref_load:
                                        v3d_nir_lower_image_load(&b, intr);
                                        break;
                                case nir_intrinsic_image_deref_store:
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
