/*
 * Copyright (C) 2020 Collabora, Ltd.
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
 *
 * Authors (Collabora):
 *      Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

/**
 * Implements framebuffer format conversions in software for Midgard/Bifrost
 * blend shaders. This pass is designed for a single render target; Midgard
 * duplicates blend shaders for MRT to simplify everything. A particular
 * framebuffer format may be categorized as 1) typed load available, 2) typed
 * unpack available, or 3) software unpack only, and likewise for stores. The
 * first two types are handled in the compiler backend directly, so this module
 * is responsible for identifying type 3 formats (hardware dependent) and
 * inserting appropriate ALU code to perform the conversion from the packed
 * type to a designated unpacked type, and vice versa.
 *
 * The unpacked type depends on the format:
 *
 *      - For 32-bit float formats, 32-bit floats.
 *      - For other floats, 16-bit floats.
 *      - For 32-bit ints, 32-bit ints.
 *      - For 8-bit ints, 8-bit ints.
 *      - For other ints, 16-bit ints.
 *
 * The rationale is to optimize blending and logic op instructions by using the
 * smallest precision necessary to store the pixel losslessly.
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_format_convert.h"
#include "util/format/u_format.h"
#include "pan_lower_framebuffer.h"
#include "panfrost-quirks.h"

/* Determines the unpacked type best suiting a given format, so the rest of the
 * pipeline may be adjusted accordingly */

nir_alu_type
pan_unpacked_type_for_format(const struct util_format_description *desc)
{
        int c = util_format_get_first_non_void_channel(desc->format);

        if (c == -1)
                unreachable("Void format not renderable");

        bool large = (desc->channel[c].size > 16);
        bool bit8 = (desc->channel[c].size == 8);
        assert(desc->channel[c].size <= 32);

        if (desc->channel[c].normalized)
                return large ? nir_type_float32 : nir_type_float16;

        switch (desc->channel[c].type) {
        case UTIL_FORMAT_TYPE_UNSIGNED:
                return bit8 ? nir_type_uint8 :
                        large ? nir_type_uint32 : nir_type_uint16;
        case UTIL_FORMAT_TYPE_SIGNED:
                return bit8 ? nir_type_int8 :
                        large ? nir_type_int32 : nir_type_int16;
        case UTIL_FORMAT_TYPE_FLOAT:
                return large ? nir_type_float32 : nir_type_float16;
        default:
                unreachable("Format not renderable");
        }
}

enum pan_format_class
pan_format_class_load(const struct util_format_description *desc, unsigned quirks)
{
        /* Pure integers can be loaded via EXT_framebuffer_fetch and should be
         * handled as a raw load with a size conversion (it's cheap). Likewise,
         * since float framebuffers are internally implemented as raw (i.e.
         * integer) framebuffers with blend shaders to go back and forth, they
         * should be s/w as well */

        if (util_format_is_pure_integer(desc->format) || util_format_is_float(desc->format))
                return PAN_FORMAT_SOFTWARE;

        /* Check if we can do anything better than software architecturally */
        if (quirks & MIDGARD_NO_TYPED_BLEND_LOADS) {
                return (quirks & NO_BLEND_PACKS)
                        ? PAN_FORMAT_SOFTWARE : PAN_FORMAT_PACK;
        }

        /* Some formats are missing as typed on some GPUs but have unpacks */
        if (quirks & MIDGARD_MISSING_LOADS) {
                switch (desc->format) {
                case PIPE_FORMAT_R11G11B10_FLOAT:
                case PIPE_FORMAT_R10G10B10A2_UNORM:
                case PIPE_FORMAT_B10G10R10A2_UNORM:
                case PIPE_FORMAT_R10G10B10X2_UNORM:
                case PIPE_FORMAT_B10G10R10X2_UNORM:
                case PIPE_FORMAT_R10G10B10A2_UINT:
                        return PAN_FORMAT_PACK;
                default:
                        return PAN_FORMAT_NATIVE;
                }
        }

        /* Otherwise, we can do native */
        return PAN_FORMAT_NATIVE;
}

enum pan_format_class
pan_format_class_store(const struct util_format_description *desc, unsigned quirks)
{
        /* Check if we can do anything better than software architecturally */
        if (quirks & MIDGARD_NO_TYPED_BLEND_STORES) {
                return (quirks & NO_BLEND_PACKS)
                        ? PAN_FORMAT_SOFTWARE : PAN_FORMAT_PACK;
        }

        return PAN_FORMAT_NATIVE;
}

/* Convenience method */

static enum pan_format_class
pan_format_class(const struct util_format_description *desc, unsigned quirks, bool is_store)
{
        if (is_store)
                return pan_format_class_store(desc, quirks);
        else
                return pan_format_class_load(desc, quirks);
}

/* Software packs/unpacks, by format class. Packs take in the pixel value typed
 * as `pan_unpacked_type_for_format` of the format and return an i32vec4
 * suitable for storing (with components replicated to fill). Unpacks do the
 * reverse but cannot rely on replication.
 *
 * Pure 32 formats (R32F ... RGBA32F) are 32 unpacked, so just need to
 * replicate to fill */

static nir_ssa_def *
pan_pack_pure_32(nir_builder *b, nir_ssa_def *v)
{
        nir_ssa_def *replicated[4];

        for (unsigned i = 0; i < 4; ++i)
                replicated[i] = nir_channel(b, v, i % v->num_components);

        return nir_vec(b, replicated, 4);
}

static nir_ssa_def *
pan_unpack_pure_32(nir_builder *b, nir_ssa_def *pack, unsigned num_components)
{
        return nir_channels(b, pack, (1 << num_components) - 1);
}

/* Pure x16 formats are x16 unpacked, so it's similar, but we need to pack
 * upper/lower halves of course */

static nir_ssa_def *
pan_pack_pure_16(nir_builder *b, nir_ssa_def *v)
{
        nir_ssa_def *replicated[4];

        for (unsigned i = 0; i < 4; ++i) {
                unsigned c = 2 * i;

                nir_ssa_def *parts[2] = {
                        nir_channel(b, v, (c + 0) % v->num_components),
                        nir_channel(b, v, (c + 1) % v->num_components)
                };

                replicated[i] = nir_pack_32_2x16(b, nir_vec(b, parts, 2));
        }

        return nir_vec(b, replicated, 4);
}

static nir_ssa_def *
pan_unpack_pure_16(nir_builder *b, nir_ssa_def *pack, unsigned num_components)
{
        nir_ssa_def *unpacked[4];

        assert(num_components <= 4);

        for (unsigned i = 0; i < num_components; i += 2) {
                nir_ssa_def *halves = 
                        nir_unpack_32_2x16(b, nir_channel(b, pack, i >> 1));

                unpacked[i + 0] = nir_channel(b, halves, 0);
                unpacked[i + 1] = nir_channel(b, halves, 1);
        }

        for (unsigned i = num_components; i < 4; ++i)
                unpacked[i] = nir_imm_intN_t(b, 0, 16);

        return nir_vec(b, unpacked, 4);
}

/* And likewise for x8. pan_fill_4 fills a 4-channel vector with a n-channel
 * vector (n <= 4), replicating as needed. pan_replicate_4 constructs a
 * 4-channel vector from a scalar via replication */

static nir_ssa_def *
pan_fill_4(nir_builder *b, nir_ssa_def *v)
{
        nir_ssa_def *q[4];
        assert(v->num_components <= 4);

        for (unsigned j = 0; j < 4; ++j)
                q[j] = nir_channel(b, v, j % v->num_components);

        return nir_vec(b, q, 4);
}

static nir_ssa_def *
pan_extend(nir_builder *b, nir_ssa_def *v, unsigned N)
{
        nir_ssa_def *q[4];
        assert(v->num_components <= 4);
        assert(N <= 4);

        for (unsigned j = 0; j < v->num_components; ++j)
                q[j] = nir_channel(b, v, j);

        for (unsigned j = v->num_components; j < N; ++j)
                q[j] = nir_imm_int(b, 0);

        return nir_vec(b, q, N);
}

static nir_ssa_def *
pan_replicate_4(nir_builder *b, nir_ssa_def *v)
{
        nir_ssa_def *replicated[4] = { v, v, v, v };
        return nir_vec(b, replicated, 4);
}

static nir_ssa_def *
pan_pack_pure_8(nir_builder *b, nir_ssa_def *v)
{
        return pan_replicate_4(b, nir_pack_32_4x8(b, pan_fill_4(b, v)));
}

static nir_ssa_def *
pan_unpack_pure_8(nir_builder *b, nir_ssa_def *pack, unsigned num_components)
{
        assert(num_components <= 4);
        nir_ssa_def *unpacked = nir_unpack_32_4x8(b, nir_channel(b, pack, 0));
        return nir_channels(b, unpacked, (1 << num_components) - 1);
}

/* UNORM 8 is unpacked to f16 vec4. We could directly use the un/pack_unorm_4x8
 * ops provided we replicate appropriately, but for packing we'd rather stay in
 * 8/16-bit whereas the NIR op forces 32-bit, so we do it manually */

static nir_ssa_def *
pan_pack_unorm_8(nir_builder *b, nir_ssa_def *v)
{
        return pan_replicate_4(b, nir_pack_32_4x8(b,
                nir_f2u8(b, nir_fround_even(b, nir_fmul(b, nir_fsat(b,
                        pan_fill_4(b, v)), nir_imm_float16(b, 255.0))))));
}

static nir_ssa_def *
pan_unpack_unorm_8(nir_builder *b, nir_ssa_def *pack, unsigned num_components)
{
        assert(num_components <= 4);
        nir_ssa_def *unpacked = nir_unpack_unorm_4x8(b, nir_channel(b, pack, 0));
        return nir_f2fmp(b, unpacked);
}

/* UNORM 4 is also unpacked to f16, which prevents us from using the shared
 * unpack which strongly assumes fp32. However, on the tilebuffer it is actually packed as:
 *      
 *      [AAAA] [0000] [BBBB] [0000] [GGGG] [0000] [RRRR] [0000] 
 *
 * In other words, spacing it out so we're aligned to bytes and on top. So
 * pack as:
 *
 *      pack_32_4x8(f2u8_rte(v * 15.0) << 4)
 */

static nir_ssa_def *
pan_pack_unorm_small(nir_builder *b, nir_ssa_def *v,
                nir_ssa_def *scales, nir_ssa_def *shifts)
{
        nir_ssa_def *f = nir_fmul(b, nir_fsat(b, pan_fill_4(b, v)), scales);
        nir_ssa_def *u8 = nir_f2u8(b, nir_fround_even(b, f));
        nir_ssa_def *s = nir_ishl(b, u8, shifts);
        nir_ssa_def *repl = nir_pack_32_4x8(b, s);

        return pan_replicate_4(b, repl);
}

static nir_ssa_def *
pan_unpack_unorm_small(nir_builder *b, nir_ssa_def *pack,
                nir_ssa_def *scales, nir_ssa_def *shifts)
{
        nir_ssa_def *channels = nir_unpack_32_4x8(b, nir_channel(b, pack, 0));
        nir_ssa_def *raw = nir_ushr(b, nir_i2imp(b, channels), shifts);
        return nir_fmul(b, nir_u2f16(b, raw), scales);
}

static nir_ssa_def *
pan_pack_unorm_4(nir_builder *b, nir_ssa_def *v)
{
        return pan_pack_unorm_small(b, v,
                nir_imm_vec4_16(b, 15.0, 15.0, 15.0, 15.0),
                nir_imm_ivec4(b, 4, 4, 4, 4));
}

static nir_ssa_def *
pan_unpack_unorm_4(nir_builder *b, nir_ssa_def *v)
{
        return pan_unpack_unorm_small(b, v,
                        nir_imm_vec4_16(b, 1.0 / 15.0, 1.0 / 15.0, 1.0 / 15.0, 1.0 / 15.0),
                        nir_imm_ivec4(b, 4, 4, 4, 4));
}

/* UNORM RGB5_A1 and RGB565 are similar */

static nir_ssa_def *
pan_pack_unorm_5551(nir_builder *b, nir_ssa_def *v)
{
        return pan_pack_unorm_small(b, v,
                        nir_imm_vec4_16(b, 31.0, 31.0, 31.0, 1.0),
                        nir_imm_ivec4(b, 3, 3, 3, 7));
}

static nir_ssa_def *
pan_unpack_unorm_5551(nir_builder *b, nir_ssa_def *v)
{
        return pan_unpack_unorm_small(b, v,
                        nir_imm_vec4_16(b, 1.0 / 31.0, 1.0 / 31.0, 1.0 / 31.0, 1.0),
                        nir_imm_ivec4(b, 3, 3, 3, 7));
}

static nir_ssa_def *
pan_pack_unorm_565(nir_builder *b, nir_ssa_def *v)
{
        return pan_pack_unorm_small(b, v,
                        nir_imm_vec4_16(b, 31.0, 63.0, 31.0, 0.0),
                        nir_imm_ivec4(b, 3, 2, 3, 0));
}

static nir_ssa_def *
pan_unpack_unorm_565(nir_builder *b, nir_ssa_def *v)
{
        return pan_unpack_unorm_small(b, v,
                        nir_imm_vec4_16(b, 1.0 / 31.0, 1.0 / 63.0, 1.0 / 31.0, 0.0),
                        nir_imm_ivec4(b, 3, 2, 3, 0));
}

/* RGB10_A2 is packed in the tilebuffer as the bottom 3 bytes being the top
 * 8-bits of RGB and the top byte being RGBA as 2-bits packed. As imirkin
 * pointed out, this means free conversion to RGBX8 */

static nir_ssa_def *
pan_pack_unorm_1010102(nir_builder *b, nir_ssa_def *v)
{
        nir_ssa_def *scale = nir_imm_vec4_16(b, 1023.0, 1023.0, 1023.0, 3.0);
        nir_ssa_def *s = nir_f2u32(b, nir_fround_even(b, nir_f2f32(b, nir_fmul(b, nir_fsat(b, v), scale))));

        nir_ssa_def *top8 = nir_ushr(b, s, nir_imm_ivec4(b, 0x2, 0x2, 0x2, 0x2));
        nir_ssa_def *top8_rgb = nir_pack_32_4x8(b, nir_u2u8(b, top8));

        nir_ssa_def *bottom2 = nir_iand(b, s, nir_imm_ivec4(b, 0x3, 0x3, 0x3, 0x3));

        nir_ssa_def *top =
                 nir_ior(b,
                        nir_ior(b, 
                                nir_ishl(b, nir_channel(b, bottom2, 0), nir_imm_int(b, 24 + 0)),
                                nir_ishl(b, nir_channel(b, bottom2, 1), nir_imm_int(b, 24 + 2))),
                        nir_ior(b, 
                                nir_ishl(b, nir_channel(b, bottom2, 2), nir_imm_int(b, 24 + 4)),
                                nir_ishl(b, nir_channel(b, bottom2, 3), nir_imm_int(b, 24 + 6))));

        nir_ssa_def *p = nir_ior(b, top, top8_rgb);
        return pan_replicate_4(b, p);
}

static nir_ssa_def *
pan_unpack_unorm_1010102(nir_builder *b, nir_ssa_def *packed)
{
        nir_ssa_def *p = nir_channel(b, packed, 0);
        nir_ssa_def *bytes = nir_unpack_32_4x8(b, p);
        nir_ssa_def *ubytes = nir_i2imp(b, bytes);

        nir_ssa_def *shifts = nir_ushr(b, pan_replicate_4(b, nir_channel(b, ubytes, 3)),
                        nir_imm_ivec4(b, 0, 2, 4, 6));
        nir_ssa_def *precision = nir_iand(b, shifts,
                        nir_i2imp(b, nir_imm_ivec4(b, 0x3, 0x3, 0x3, 0x3)));

        nir_ssa_def *top_rgb = nir_ishl(b, nir_channels(b, ubytes, 0x7), nir_imm_int(b, 2));
        top_rgb = nir_ior(b, nir_channels(b, precision, 0x7), top_rgb);

        nir_ssa_def *chans [4] = {
                nir_channel(b, top_rgb, 0),
                nir_channel(b, top_rgb, 1),
                nir_channel(b, top_rgb, 2),
                nir_channel(b, precision, 3)
        };

        nir_ssa_def *scale = nir_imm_vec4(b, 1.0 / 1023.0, 1.0 / 1023.0, 1.0 / 1023.0, 1.0 / 3.0);
        return nir_f2fmp(b, nir_fmul(b, nir_u2f32(b, nir_vec(b, chans, 4)), scale));
}

/* On the other hand, the pure int RGB10_A2 is identical to the spec */

static nir_ssa_def *
pan_pack_uint_1010102(nir_builder *b, nir_ssa_def *v)
{
        nir_ssa_def *shift = nir_ishl(b, nir_u2u32(b, v),
                        nir_imm_ivec4(b, 0, 10, 20, 30));

        nir_ssa_def *p = nir_ior(b,
                        nir_ior(b, nir_channel(b, shift, 0), nir_channel(b, shift, 1)),
                        nir_ior(b, nir_channel(b, shift, 2), nir_channel(b, shift, 3)));

        return pan_replicate_4(b, p);
}

static nir_ssa_def *
pan_unpack_uint_1010102(nir_builder *b, nir_ssa_def *packed)
{
        nir_ssa_def *chan = nir_channel(b, packed, 0);

        nir_ssa_def *shift = nir_ushr(b, pan_replicate_4(b, chan),
                        nir_imm_ivec4(b, 0, 10, 20, 30));

        nir_ssa_def *mask = nir_iand(b, shift,
                        nir_imm_ivec4(b, 0x3ff, 0x3ff, 0x3ff, 0x3));

        return nir_i2imp(b, mask);
}

/* NIR means we can *finally* catch a break */

static nir_ssa_def *
pan_pack_r11g11b10(nir_builder *b, nir_ssa_def *v)
{
        return pan_replicate_4(b, nir_format_pack_11f11f10f(b, 
                                nir_f2f32(b, v)));
}

static nir_ssa_def *
pan_unpack_r11g11b10(nir_builder *b, nir_ssa_def *v)
{
        nir_ssa_def *f32 = nir_format_unpack_11f11f10f(b, nir_channel(b, v, 0));
        nir_ssa_def *f16 = nir_f2fmp(b, f32);

        /* Extend to vec4 with alpha */
        nir_ssa_def *components[4] = {
                nir_channel(b, f16, 0),
                nir_channel(b, f16, 1),
                nir_channel(b, f16, 2),
                nir_imm_float16(b, 1.0)
        };

        return nir_vec(b, components, 4);
}

/* Wrapper around sRGB conversion */

static nir_ssa_def *
pan_linear_to_srgb(nir_builder *b, nir_ssa_def *linear)
{
        nir_ssa_def *rgb = nir_channels(b, linear, 0x7);

        /* TODO: fp16 native conversion */
        nir_ssa_def *srgb = nir_f2fmp(b,
                        nir_format_linear_to_srgb(b, nir_f2f32(b, rgb)));

        nir_ssa_def *comp[4] = {
                nir_channel(b, srgb, 0),
                nir_channel(b, srgb, 1),
                nir_channel(b, srgb, 2),
                nir_channel(b, linear, 3),
        };

        return nir_vec(b, comp, 4);
}

static nir_ssa_def *
pan_srgb_to_linear(nir_builder *b, nir_ssa_def *srgb)
{
        nir_ssa_def *rgb = nir_channels(b, srgb, 0x7);

        /* TODO: fp16 native conversion */
        nir_ssa_def *linear = nir_f2fmp(b,
                        nir_format_srgb_to_linear(b, nir_f2f32(b, rgb)));

        nir_ssa_def *comp[4] = {
                nir_channel(b, linear, 0),
                nir_channel(b, linear, 1),
                nir_channel(b, linear, 2),
                nir_channel(b, srgb, 3),
        };

        return nir_vec(b, comp, 4);
}



/* Generic dispatches for un/pack regardless of format */

static bool
pan_is_unorm4(const struct util_format_description *desc)
{
        switch (desc->format) {
        case PIPE_FORMAT_B4G4R4A4_UNORM:
        case PIPE_FORMAT_B4G4R4X4_UNORM:
        case PIPE_FORMAT_A4R4_UNORM:
        case PIPE_FORMAT_R4A4_UNORM:
        case PIPE_FORMAT_A4B4G4R4_UNORM:
        case PIPE_FORMAT_R4G4B4A4_UNORM:
                return true;
        default:
                return false;
        }
 
}

static nir_ssa_def *
pan_unpack(nir_builder *b,
                const struct util_format_description *desc,
                nir_ssa_def *packed)
{
        if (util_format_is_unorm8(desc))
                return pan_unpack_unorm_8(b, packed, desc->nr_channels);

        if (pan_is_unorm4(desc))
                return pan_unpack_unorm_4(b, packed);

        if (desc->is_array) {
                int c = util_format_get_first_non_void_channel(desc->format);
                assert(c >= 0);
                struct util_format_channel_description d = desc->channel[c];

                if (d.size == 32 || d.size == 16) {
                        assert(!d.normalized);
                        assert(d.type == UTIL_FORMAT_TYPE_FLOAT || d.pure_integer);

                        return d.size == 32 ? pan_unpack_pure_32(b, packed, desc->nr_channels) :
                                pan_unpack_pure_16(b, packed, desc->nr_channels);
                } else if (d.size == 8) {
                        assert(d.pure_integer);
                        return pan_unpack_pure_8(b, packed, desc->nr_channels);
                } else {
                        unreachable("Unrenderable size");
                }
        }

        switch (desc->format) {
        case PIPE_FORMAT_B5G5R5A1_UNORM:
        case PIPE_FORMAT_R5G5B5A1_UNORM:
                return pan_unpack_unorm_5551(b, packed);
        case PIPE_FORMAT_B5G6R5_UNORM:
                return pan_unpack_unorm_565(b, packed);
        case PIPE_FORMAT_R10G10B10A2_UNORM:
                return pan_unpack_unorm_1010102(b, packed);
        case PIPE_FORMAT_R10G10B10A2_UINT:
                return pan_unpack_uint_1010102(b, packed);
        case PIPE_FORMAT_R11G11B10_FLOAT:
                return pan_unpack_r11g11b10(b, packed);
        default:
                break;
        }

        fprintf(stderr, "%s\n", desc->name);
        unreachable("Unknown format");
}

static nir_ssa_def *
pan_pack(nir_builder *b,
                const struct util_format_description *desc,
                nir_ssa_def *unpacked)
{
        if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB)
                unpacked = pan_linear_to_srgb(b, unpacked);

        if (util_format_is_unorm8(desc))
                return pan_pack_unorm_8(b, unpacked);

        if (pan_is_unorm4(desc))
                return pan_pack_unorm_4(b, unpacked);

        if (desc->is_array) {
                int c = util_format_get_first_non_void_channel(desc->format);
                assert(c >= 0);
                struct util_format_channel_description d = desc->channel[c];

                if (d.size == 32 || d.size == 16) {
                        assert(!d.normalized);
                        assert(d.type == UTIL_FORMAT_TYPE_FLOAT || d.pure_integer);

                        return d.size == 32 ? pan_pack_pure_32(b, unpacked) :
                                pan_pack_pure_16(b, unpacked);
                } else if (d.size == 8) {
                        assert(d.pure_integer);
                        return pan_pack_pure_8(b, unpacked);
                } else {
                        unreachable("Unrenderable size");
                }
        }

        switch (desc->format) {
        case PIPE_FORMAT_B5G5R5A1_UNORM:
        case PIPE_FORMAT_R5G5B5A1_UNORM:
                return pan_pack_unorm_5551(b, unpacked);
        case PIPE_FORMAT_B5G6R5_UNORM:
                return pan_pack_unorm_565(b, unpacked);
        case PIPE_FORMAT_R10G10B10A2_UNORM:
                return pan_pack_unorm_1010102(b, unpacked);
        case PIPE_FORMAT_R10G10B10A2_UINT:
                return pan_pack_uint_1010102(b, unpacked);
        case PIPE_FORMAT_R11G11B10_FLOAT:
                return pan_pack_r11g11b10(b, unpacked);
        default:
                break;
        }

        fprintf(stderr, "%s\n", desc->name);
        unreachable("Unknown format");
}

static void
pan_lower_fb_store(nir_shader *shader,
                nir_builder *b,
                nir_intrinsic_instr *intr,
                const struct util_format_description *desc,
                unsigned quirks)
{
        /* For stores, add conversion before */
        nir_ssa_def *unpacked = nir_ssa_for_src(b, intr->src[1], 4);
        nir_ssa_def *packed = pan_pack(b, desc, unpacked);

        nir_intrinsic_instr *new =
                nir_intrinsic_instr_create(shader, nir_intrinsic_store_raw_output_pan);
        new->src[0] = nir_src_for_ssa(packed);
        new->num_components = 4;
        nir_builder_instr_insert(b, &new->instr);
}

static nir_ssa_def *
pan_sample_id(nir_builder *b, int sample)
{
        return (sample >= 0) ? nir_imm_int(b, sample) : nir_load_sample_id(b);
}

static void
pan_lower_fb_load(nir_shader *shader,
                nir_builder *b,
                nir_intrinsic_instr *intr,
                const struct util_format_description *desc,
                unsigned base, int sample, unsigned quirks)
{
        nir_intrinsic_instr *new = nir_intrinsic_instr_create(shader,
                       nir_intrinsic_load_raw_output_pan);
        new->num_components = 4;
        new->src[0] = nir_src_for_ssa(pan_sample_id(b, sample));

        nir_intrinsic_set_base(new, base);

        nir_ssa_dest_init(&new->instr, &new->dest, 4, 32, NULL);
        nir_builder_instr_insert(b, &new->instr);

        /* Convert the raw value */
        nir_ssa_def *packed = &new->dest.ssa;
        nir_ssa_def *unpacked = pan_unpack(b, desc, packed);

        if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB)
                unpacked = pan_srgb_to_linear(b, unpacked);

        /* Convert to the size of the load intrinsic.
         *
         * We can assume that the type will match with the framebuffer format:
         *
         * Page 170 of the PDF of the OpenGL ES 3.0.6 spec says:
         *
         * If [UNORM or SNORM, convert to fixed-point]; otherwise no type
         * conversion is applied. If the values written by the fragment shader
         * do not match the format(s) of the corresponding color buffer(s),
         * the result is undefined.
         */

        unsigned bits = nir_dest_bit_size(intr->dest);

        nir_alu_type src_type;
        if (desc->channel[0].pure_integer) {
                if (desc->channel[0].type == UTIL_FORMAT_TYPE_SIGNED)
                        src_type = nir_type_int;
                else
                        src_type = nir_type_uint;
        } else {
                src_type = nir_type_float;
        }

        unpacked = nir_convert_to_bit_size(b, unpacked, src_type, bits);
        unpacked = pan_extend(b, unpacked, nir_dest_num_components(intr->dest));

        nir_src rewritten = nir_src_for_ssa(unpacked);
        nir_ssa_def_rewrite_uses_after(&intr->dest.ssa, rewritten, &intr->instr);
}

bool
pan_lower_framebuffer(nir_shader *shader, const enum pipe_format *rt_fmts,
                      bool is_blend, unsigned quirks)
{
        if (shader->info.stage != MESA_SHADER_FRAGMENT)
               return false;

        bool progress = false;

        nir_foreach_function(func, shader) {
                nir_foreach_block(block, func->impl) {
                        nir_foreach_instr_safe(instr, block) {
                                if (instr->type != nir_instr_type_intrinsic)
                                        continue;

                                nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

                                bool is_load = intr->intrinsic == nir_intrinsic_load_deref;
                                bool is_store = intr->intrinsic == nir_intrinsic_store_deref;

                                if (!(is_load || (is_store && is_blend)))
                                        continue;

                                nir_variable *var = nir_intrinsic_get_var(intr, 0);

                                if (var->data.mode != nir_var_shader_out)
                                        continue;

                                unsigned base = var->data.driver_location;

                                unsigned rt;
                                if (var->data.location == FRAG_RESULT_COLOR)
                                        rt = 0;
                                else if (var->data.location >= FRAG_RESULT_DATA0)
                                        rt = var->data.location - FRAG_RESULT_DATA0;
                                else
                                        continue;

                                if (rt_fmts[rt] == PIPE_FORMAT_NONE)
                                        continue;

                                const struct util_format_description *desc =
                                   util_format_description(rt_fmts[rt]);

                                enum pan_format_class fmt_class =
                                        pan_format_class(desc, quirks, is_store);

                                /* Don't lower */
                                if (fmt_class == PAN_FORMAT_NATIVE)
                                        continue;

                                /* EXT_shader_framebuffer_fetch requires
                                 * per-sample loads.
                                 * MSAA blend shaders are not yet handled, so
                                 * for now always load sample 0. */
                                int sample = is_blend ? 0 : -1;

                                nir_builder b;
                                nir_builder_init(&b, func->impl);

                                if (is_store) {
                                        b.cursor = nir_before_instr(instr);
                                        pan_lower_fb_store(shader, &b, intr, desc, quirks);
                                } else {
                                        b.cursor = nir_after_instr(instr);
                                        pan_lower_fb_load(shader, &b, intr, desc, base, sample, quirks);
                                }

                                nir_instr_remove(instr);

                                progress = true;
                        }
                }

                nir_metadata_preserve(func->impl, nir_metadata_block_index |
                                nir_metadata_dominance);
        }

        return progress;
}
