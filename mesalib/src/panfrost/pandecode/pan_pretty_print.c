/*
 * © Copyright 2017-2098 The Panfrost Communiy
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

#include "pan_pretty_print.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Some self-contained prettyprinting functions shared between pandecode and
 * the main driver */

#define DEFINE_CASE(name) case MALI_## name: return "MALI_" #name
char *pandecode_format(enum mali_format format)
{
        static char unk_format_str[10];

        switch (format) {
                DEFINE_CASE(RGB565);
                DEFINE_CASE(RGB5_A1_UNORM);
                DEFINE_CASE(RGB10_A2_UNORM);
                DEFINE_CASE(RGB10_A2_SNORM);
                DEFINE_CASE(RGB10_A2UI);
                DEFINE_CASE(RGB10_A2I);
                DEFINE_CASE(NV12);
                DEFINE_CASE(Z32_UNORM);
                DEFINE_CASE(R32_FIXED);
                DEFINE_CASE(RG32_FIXED);
                DEFINE_CASE(RGB32_FIXED);
                DEFINE_CASE(RGBA32_FIXED);
                DEFINE_CASE(R11F_G11F_B10F);
                DEFINE_CASE(R9F_G9F_B9F_E5F);
                DEFINE_CASE(VARYING_POS);
                DEFINE_CASE(VARYING_DISCARD);

                DEFINE_CASE(R8_SNORM);
                DEFINE_CASE(R16_SNORM);
                DEFINE_CASE(R32_SNORM);
                DEFINE_CASE(RG8_SNORM);
                DEFINE_CASE(RG16_SNORM);
                DEFINE_CASE(RG32_SNORM);
                DEFINE_CASE(RGB8_SNORM);
                DEFINE_CASE(RGB16_SNORM);
                DEFINE_CASE(RGB32_SNORM);
                DEFINE_CASE(RGBA8_SNORM);
                DEFINE_CASE(RGBA16_SNORM);
                DEFINE_CASE(RGBA32_SNORM);

                DEFINE_CASE(R8UI);
                DEFINE_CASE(R16UI);
                DEFINE_CASE(R32UI);
                DEFINE_CASE(RG8UI);
                DEFINE_CASE(RG16UI);
                DEFINE_CASE(RG32UI);
                DEFINE_CASE(RGB8UI);
                DEFINE_CASE(RGB16UI);
                DEFINE_CASE(RGB32UI);
                DEFINE_CASE(RGBA8UI);
                DEFINE_CASE(RGBA16UI);
                DEFINE_CASE(RGBA32UI);

                DEFINE_CASE(R8_UNORM);
                DEFINE_CASE(R16_UNORM);
                DEFINE_CASE(R32_UNORM);
                DEFINE_CASE(R32F);
                DEFINE_CASE(RG8_UNORM);
                DEFINE_CASE(RG16_UNORM);
                DEFINE_CASE(RG32_UNORM);
                DEFINE_CASE(RG32F);
                DEFINE_CASE(RGB8_UNORM);
                DEFINE_CASE(RGB16_UNORM);
                DEFINE_CASE(RGB32_UNORM);
                DEFINE_CASE(RGB32F);
                DEFINE_CASE(RGBA4_UNORM);
                DEFINE_CASE(RGBA8_UNORM);
                DEFINE_CASE(RGBA16_UNORM);
                DEFINE_CASE(RGBA32_UNORM);
                DEFINE_CASE(RGBA32F);

                DEFINE_CASE(R8I);
                DEFINE_CASE(R16I);
                DEFINE_CASE(R32I);
                DEFINE_CASE(RG8I);
                DEFINE_CASE(R16F);
                DEFINE_CASE(RG16I);
                DEFINE_CASE(RG32I);
                DEFINE_CASE(RG16F);
                DEFINE_CASE(RGB8I);
                DEFINE_CASE(RGB16I);
                DEFINE_CASE(RGB32I);
                DEFINE_CASE(RGB16F);
                DEFINE_CASE(RGBA8I);
                DEFINE_CASE(RGBA16I);
                DEFINE_CASE(RGBA32I);
                DEFINE_CASE(RGBA16F);

                DEFINE_CASE(RGBA4);
                DEFINE_CASE(RGBA8_2);
                DEFINE_CASE(RGB10_A2_2);
        default:
                snprintf(unk_format_str, sizeof(unk_format_str), "MALI_0x%02x", format);
                return unk_format_str;
        }
}

#undef DEFINE_CASE

/* Helper to dump fixed-function blend part for debugging */

static const char *
panfrost_factor_name(enum mali_dominant_factor factor)
{
        switch (factor) {
        case MALI_DOMINANT_UNK0:
                return "unk0";

        case MALI_DOMINANT_ZERO:
                return "zero";

        case MALI_DOMINANT_SRC_COLOR:
                return "source color";

        case MALI_DOMINANT_DST_COLOR:
                return "dest color";

        case MALI_DOMINANT_UNK4:
                return "unk4";

        case MALI_DOMINANT_SRC_ALPHA:
                return "source alpha";

        case MALI_DOMINANT_DST_ALPHA:
                return "dest alpha";

        case MALI_DOMINANT_CONSTANT:
                return "constant";
        }

        return "unreachable";
}

static const char *
panfrost_modifier_name(enum mali_blend_modifier mod)
{
        switch (mod) {
        case MALI_BLEND_MOD_UNK0:
                return "unk0";

        case MALI_BLEND_MOD_NORMAL:
                return "normal";

        case MALI_BLEND_MOD_SOURCE_ONE:
                return "source one";

        case MALI_BLEND_MOD_DEST_ONE:
                return "dest one";
        }

        return "unreachable";
}

static void
panfrost_print_fixed_part(const char *name, unsigned u)
{
        struct mali_blend_mode part;
        memcpy(&part, &u, sizeof(part));

        printf("%s blend mode (%X):\n", name, u);

        printf(" %s dominant:\n",
               (part.dominant == MALI_BLEND_DOM_SOURCE) ? "source" : "destination");

        printf("   %s\n", panfrost_factor_name(part.dominant_factor));

        if (part.complement_dominant)
                printf("   complement\n");


        printf(" nondominant %s\n",
               (part.nondominant_mode == MALI_BLEND_NON_MIRROR) ? "mirror" : "zero");


        printf(" mode: %s\n", panfrost_modifier_name(part.clip_modifier));

        if (part.negate_source) printf(" negate source\n");

        if (part.negate_dest) printf(" negate dest\n");

        assert(!(part.unused_0 || part.unused_1));
}

void
panfrost_print_blend_equation(struct mali_blend_equation eq)
{
        printf("\n");
        panfrost_print_fixed_part("RGB", eq.rgb_mode);
        panfrost_print_fixed_part("Alpha", eq.alpha_mode);

        assert(!eq.zero1);

        printf("Mask: %s%s%s%s\n",
               (eq.color_mask & MALI_MASK_R) ? "R" : "",
               (eq.color_mask & MALI_MASK_G) ? "G" : "",
               (eq.color_mask & MALI_MASK_B) ? "B" : "",
               (eq.color_mask & MALI_MASK_A) ? "A" : "");
}
