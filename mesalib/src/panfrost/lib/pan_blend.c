/*
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright (C) 2019-2021 Collabora, Ltd.
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

#include "pan_blend.h"
#include "pan_shader.h"
#include "pan_texture.h"
#include "panfrost/util/pan_lower_framebuffer.h"
#include "panfrost/util/nir_lower_blend.h"
#include "util/format/u_format.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"

/* Implements fixed-function blending on Midgard. */

/* Check if this is a special edge case blend factor, which may require the use
 * of clip modifiers */

static bool
factor_is_supported(enum blend_factor factor)
{
        return factor != BLEND_FACTOR_SRC_ALPHA_SATURATE &&
               factor != BLEND_FACTOR_SRC1_COLOR &&
               factor != BLEND_FACTOR_SRC1_ALPHA;
}

static bool
can_fixed_function_equation(enum blend_func blend_func,
                            enum blend_factor src_factor,
                            enum blend_factor dest_factor)
{
        if (blend_func != BLEND_FUNC_ADD &&
            blend_func != BLEND_FUNC_SUBTRACT &&
            blend_func != BLEND_FUNC_REVERSE_SUBTRACT)
                return false;

        if (!factor_is_supported(src_factor) ||
            !factor_is_supported(dest_factor))
                return false;

        if (src_factor != dest_factor &&
            src_factor != BLEND_FACTOR_ZERO &&
            dest_factor != BLEND_FACTOR_ZERO)
                return false;

        return true;
}

static unsigned
blend_factor_constant_mask(enum blend_factor factor)
{
        unsigned mask = 0;

        if (factor == BLEND_FACTOR_CONSTANT_COLOR)
                mask |= 0b0111; /* RGB */
        else if (factor == BLEND_FACTOR_CONSTANT_ALPHA)
                mask |= 0b1000; /* A */

        return mask;
}

unsigned
pan_blend_constant_mask(const struct pan_blend_state *state,
                        unsigned rt)
{
        const struct pan_blend_equation *e = &state->rts[rt].equation;

        return blend_factor_constant_mask(e->rgb_src_factor) |
               blend_factor_constant_mask(e->rgb_dst_factor) |
               blend_factor_constant_mask(e->alpha_src_factor) |
               blend_factor_constant_mask(e->alpha_dst_factor);
}

static bool
can_blend_constant(const struct panfrost_device *dev,
                   const struct pan_blend_state *state,
                   unsigned rt)
{
        unsigned constant_mask = pan_blend_constant_mask(state, rt);
        if (!constant_mask)
                return true;

        /* v6 doesn't support blend constants in FF blend equations. */
        if (dev->arch == 6)
                return false;

        unsigned first_constant = ffs(constant_mask) - 1;
        float constant = state->constants[first_constant];

        for (unsigned i = first_constant + 1; i < ARRAY_SIZE(state->constants); i++) {
                if (((1 << i) & constant_mask) &&
                    state->constants[i] != constant)
                        return false;
        }

        return true;
}

float
pan_blend_get_constant(ASSERTED const struct panfrost_device *dev,
                       const struct pan_blend_state *state,
                       unsigned rt)
{
        assert(can_blend_constant(dev, state, rt));

        unsigned constant_mask = pan_blend_constant_mask(state, rt);

        if (!constant_mask)
                return 0.0f;

        return state->constants[ffs(constant_mask) - 1];
}

bool
pan_blend_can_fixed_function(const struct panfrost_device *dev,
                             const struct pan_blend_state *state,
                             unsigned rt)
{
        const struct pan_blend_rt_state *rt_state = &state->rts[rt];

        /* LogicOp requires a blend shader */
        if (state->logicop_enable)
                return false;

        /* Not all formats can be blended by fixed-function hardware */
        if (!panfrost_blend_format(rt_state->format).internal)
                return false;

        if (!rt_state->equation.blend_enable)
                return true;

        if (!can_blend_constant(dev, state, rt))
                return false;

        return can_fixed_function_equation(rt_state->equation.rgb_func,
                                           rt_state->equation.rgb_src_factor,
                                           rt_state->equation.rgb_dst_factor) &&
               can_fixed_function_equation(rt_state->equation.alpha_func,
                                           rt_state->equation.alpha_src_factor,
                                           rt_state->equation.alpha_dst_factor);
}

static void
to_c_factor(enum blend_factor factor, bool invert_factor,
            struct MALI_BLEND_FUNCTION *function)
{
        function->invert_c = invert_factor;

        switch (factor) {
        case BLEND_FACTOR_ZERO:
                function->c = MALI_BLEND_OPERAND_C_ZERO;
                break;

        case BLEND_FACTOR_SRC_ALPHA:
                function->c = MALI_BLEND_OPERAND_C_SRC_ALPHA;
                break;

        case BLEND_FACTOR_DST_ALPHA:
                function->c = MALI_BLEND_OPERAND_C_DEST_ALPHA;
                break;

        case BLEND_FACTOR_SRC_COLOR:
                function->c = MALI_BLEND_OPERAND_C_SRC;
                break;

        case BLEND_FACTOR_DST_COLOR:
                function->c = MALI_BLEND_OPERAND_C_DEST;
                break;

        case BLEND_FACTOR_CONSTANT_COLOR:
        case BLEND_FACTOR_CONSTANT_ALPHA:
                function->c = MALI_BLEND_OPERAND_C_CONSTANT;
                break;
        default:
                unreachable("Invalid blend factor");
        }

}

static void
to_panfrost_function(enum blend_func blend_func,
                     enum blend_factor src_factor,
                     bool invert_src,
                     enum blend_factor dest_factor,
                     bool invert_dest,
                     struct MALI_BLEND_FUNCTION *function)
{
        assert(can_fixed_function_equation(blend_func, src_factor, dest_factor));

        if (src_factor == BLEND_FACTOR_ZERO && !invert_src) {
                function->a = MALI_BLEND_OPERAND_A_ZERO;
                function->b = MALI_BLEND_OPERAND_B_DEST;
                if (blend_func == BLEND_FUNC_SUBTRACT)
                        function->negate_b = true;
                to_c_factor(dest_factor, invert_dest, function);
        } else if (src_factor == BLEND_FACTOR_ZERO && invert_src) {
                function->a = MALI_BLEND_OPERAND_A_SRC;
                function->b = MALI_BLEND_OPERAND_B_DEST;
                if (blend_func == BLEND_FUNC_SUBTRACT)
                        function->negate_b = true;
                else if (blend_func == BLEND_FUNC_REVERSE_SUBTRACT)
                        function->negate_a = true;
                to_c_factor(dest_factor, invert_dest, function);
        } else if (dest_factor == BLEND_FACTOR_ZERO && !invert_dest) {
                function->a = MALI_BLEND_OPERAND_A_ZERO;
                function->b = MALI_BLEND_OPERAND_B_SRC;
                if (blend_func == BLEND_FUNC_REVERSE_SUBTRACT)
                        function->negate_b = true;
                to_c_factor(src_factor, invert_src, function);
        } else if (dest_factor == BLEND_FACTOR_ZERO && invert_dest) {
                function->a = MALI_BLEND_OPERAND_A_DEST;
                function->b = MALI_BLEND_OPERAND_B_SRC;
                if (blend_func == BLEND_FUNC_SUBTRACT)
                        function->negate_a = true;
                else if (blend_func == BLEND_FUNC_REVERSE_SUBTRACT)
                        function->negate_b = true;
                to_c_factor(src_factor, invert_src, function);
        } else if (src_factor == dest_factor && invert_src == invert_dest) {
                function->a = MALI_BLEND_OPERAND_A_ZERO;
                to_c_factor(src_factor, invert_src, function);

                switch (blend_func) {
                case BLEND_FUNC_ADD:
                        function->b = MALI_BLEND_OPERAND_B_SRC_PLUS_DEST;
                        break;
                case BLEND_FUNC_REVERSE_SUBTRACT:
                        function->negate_b = true;
                        /* fall-through */
                case BLEND_FUNC_SUBTRACT:
                        function->b = MALI_BLEND_OPERAND_B_SRC_MINUS_DEST;
                        break;
                default:
                        unreachable("Invalid blend function");
                }
        } else {
                assert(src_factor == dest_factor && invert_src != invert_dest);

                function->a = MALI_BLEND_OPERAND_A_DEST;
                to_c_factor(src_factor, invert_src, function);

                switch (blend_func) {
                case BLEND_FUNC_ADD:
                        function->b = MALI_BLEND_OPERAND_B_SRC_MINUS_DEST;
                        break;
                case BLEND_FUNC_REVERSE_SUBTRACT:
                        function->b = MALI_BLEND_OPERAND_B_SRC_PLUS_DEST;
                        function->negate_b = true;
                        break;
                case BLEND_FUNC_SUBTRACT:
                        function->b = MALI_BLEND_OPERAND_B_SRC_PLUS_DEST;
                        function->negate_a = true;
                        break;
                default:
                        unreachable("Invalid blend function\n");
                }
        }
}

bool
pan_blend_is_opaque(const struct pan_blend_state *state, unsigned rt)
{
        const struct pan_blend_equation *equation = &state->rts[rt].equation;

        return equation->rgb_src_factor == BLEND_FACTOR_ZERO &&
               equation->rgb_invert_src_factor &&
               equation->rgb_dst_factor == BLEND_FACTOR_ZERO &&
               !equation->rgb_invert_dst_factor &&
               (equation->rgb_func == BLEND_FUNC_ADD ||
                equation->rgb_func == BLEND_FUNC_SUBTRACT) &&
               equation->alpha_src_factor == BLEND_FACTOR_ZERO &&
               equation->alpha_invert_src_factor &&
               equation->alpha_dst_factor == BLEND_FACTOR_ZERO &&
               !equation->alpha_invert_dst_factor &&
               (equation->alpha_func == BLEND_FUNC_ADD ||
                equation->alpha_func == BLEND_FUNC_SUBTRACT) &&
               equation->color_mask == 0xf;
}

static bool
is_dest_factor(enum blend_factor factor, bool alpha)
{
      return factor == BLEND_FACTOR_DST_ALPHA ||
             factor == BLEND_FACTOR_DST_COLOR ||
             (factor == BLEND_FACTOR_SRC_ALPHA_SATURATE && !alpha);
}

bool
pan_blend_reads_dest(const struct pan_blend_state *state, unsigned rt)
{
        const struct pan_blend_rt_state *rt_state = &state->rts[rt];

        if (state->logicop_enable ||
            (rt_state->equation.color_mask &&
             rt_state->equation.color_mask != 0xF))
                return true;

        if (is_dest_factor(rt_state->equation.rgb_src_factor, false) ||
            is_dest_factor(rt_state->equation.alpha_src_factor, true) ||
            rt_state->equation.rgb_dst_factor != BLEND_FACTOR_ZERO ||
            rt_state->equation.rgb_invert_dst_factor ||
            rt_state->equation.alpha_dst_factor != BLEND_FACTOR_ZERO ||
            rt_state->equation.alpha_invert_dst_factor)
                return true;

        return false;
}

/* Create the descriptor for a fixed blend mode given the corresponding Gallium
 * state, if possible. Return true and write out the blend descriptor into
 * blend_equation. If it is not possible with the fixed function
 * representation, return false to handle degenerate cases with a blend shader
 */

void
pan_blend_to_fixed_function_equation(ASSERTED const struct panfrost_device *dev,
                                     const struct pan_blend_state *state,
                                     unsigned rt,
                                     struct MALI_BLEND_EQUATION *equation)
{
        const struct pan_blend_rt_state *rt_state = &state->rts[rt];

        assert(pan_blend_can_fixed_function(dev, state, rt));

        /* If no blending is enabled, default back on `replace` mode */
        if (!rt_state->equation.blend_enable) {
                equation->color_mask = rt_state->equation.color_mask;
                equation->rgb.a = MALI_BLEND_OPERAND_A_SRC;
                equation->rgb.b = MALI_BLEND_OPERAND_B_SRC;
                equation->rgb.c = MALI_BLEND_OPERAND_C_ZERO;
                equation->alpha.a = MALI_BLEND_OPERAND_A_SRC;
                equation->alpha.b = MALI_BLEND_OPERAND_B_SRC;
                equation->alpha.c = MALI_BLEND_OPERAND_C_ZERO;
                return;
        }

        /* Try to compile the actual fixed-function blend */
        to_panfrost_function(rt_state->equation.rgb_func,
                             rt_state->equation.rgb_src_factor,
                             rt_state->equation.rgb_invert_src_factor,
                             rt_state->equation.rgb_dst_factor,
                             rt_state->equation.rgb_invert_dst_factor,
                             &equation->rgb);

        to_panfrost_function(rt_state->equation.alpha_func,
                             rt_state->equation.alpha_src_factor,
                             rt_state->equation.alpha_invert_src_factor,
                             rt_state->equation.alpha_dst_factor,
                             rt_state->equation.alpha_invert_dst_factor,
                             &equation->alpha);
        equation->color_mask = rt_state->equation.color_mask;
}

static const char *
logicop_str(enum pipe_logicop logicop)
{
        switch (logicop) {
        case PIPE_LOGICOP_CLEAR: return "clear";
        case PIPE_LOGICOP_NOR: return "nor";
        case PIPE_LOGICOP_AND_INVERTED: return "and-inverted";
        case PIPE_LOGICOP_COPY_INVERTED: return "copy-inverted";
        case PIPE_LOGICOP_AND_REVERSE: return "and-reverse";
        case PIPE_LOGICOP_INVERT: return "invert";
        case PIPE_LOGICOP_XOR: return "xor";
        case PIPE_LOGICOP_NAND: return "nand";
        case PIPE_LOGICOP_AND: return "and";
        case PIPE_LOGICOP_EQUIV: return "equiv";
        case PIPE_LOGICOP_NOOP: return "noop";
        case PIPE_LOGICOP_OR_INVERTED: return "or-inverted";
        case PIPE_LOGICOP_COPY: return "copy";
        case PIPE_LOGICOP_OR_REVERSE: return "or-reverse";
        case PIPE_LOGICOP_OR: return "or";
        case PIPE_LOGICOP_SET: return "set";
        default: unreachable("Invalid logicop\n");
        }
}

static void
get_equation_str(const struct pan_blend_rt_state *rt_state,
                 char *str, unsigned len)
{
        const char *funcs[] = {
                "add", "sub", "reverse_sub", "min", "max",
        };
        const char *factors[] = {
                "zero", "src_color", "src1_color", "dst_color",
                "src_alpha", "src1_alpha", "dst_alpha",
                "const_color", "const_alpha", "src_alpha_sat",
        };
        int ret;

        if (!rt_state->equation.blend_enable) {
		ret = snprintf(str, len, "replace");
                assert(ret > 0);
                return;
        }

        if (rt_state->equation.color_mask & 7) {
                assert(rt_state->equation.rgb_func < ARRAY_SIZE(funcs));
                assert(rt_state->equation.rgb_src_factor < ARRAY_SIZE(factors));
                assert(rt_state->equation.rgb_dst_factor < ARRAY_SIZE(factors));
                ret = snprintf(str, len, "%s%s%s(func=%s,src_factor=%s%s,dst_factor=%s%s)%s",
                               (rt_state->equation.color_mask & 1) ? "R" : "",
                               (rt_state->equation.color_mask & 2) ? "G" : "",
                               (rt_state->equation.color_mask & 4) ? "B" : "",
                               funcs[rt_state->equation.rgb_func],
                               rt_state->equation.rgb_invert_src_factor ? "-" : "",
                               factors[rt_state->equation.rgb_src_factor],
                               rt_state->equation.rgb_invert_dst_factor ? "-" : "",
                               factors[rt_state->equation.rgb_dst_factor],
                               rt_state->equation.color_mask & 8 ? ";" : "");
                assert(ret > 0);
                str += ret;
                len -= ret;
         }

        if (rt_state->equation.color_mask & 8) {
                assert(rt_state->equation.alpha_func < ARRAY_SIZE(funcs));
                assert(rt_state->equation.alpha_src_factor < ARRAY_SIZE(factors));
                assert(rt_state->equation.alpha_dst_factor < ARRAY_SIZE(factors));
                ret = snprintf(str, len, "A(func=%s,src_factor=%s%s,dst_factor=%s%s)",
                               funcs[rt_state->equation.alpha_func],
                               rt_state->equation.alpha_invert_src_factor ? "-" : "",
                               factors[rt_state->equation.alpha_src_factor],
                               rt_state->equation.alpha_invert_dst_factor ? "-" : "",
                               factors[rt_state->equation.alpha_dst_factor]);
                assert(ret > 0);
                str += ret;
                len -= ret;
         }
}

static nir_ssa_def *
nir_iclamp(nir_builder *b, nir_ssa_def *v, int32_t lo, int32_t hi)
{
        return nir_imin(b, nir_imax(b, v, nir_imm_int(b, lo)), nir_imm_int(b, hi));
}

nir_shader *
pan_blend_create_shader(const struct panfrost_device *dev,
                        const struct pan_blend_state *state,
                        unsigned rt)
{
        const struct pan_blend_rt_state *rt_state = &state->rts[rt];
        char equation_str[128] = { 0 };

        get_equation_str(rt_state, equation_str, sizeof(equation_str));

        nir_builder b =
                nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
                                               pan_shader_get_compiler_options(dev),
                                               "pan_blend(rt=%d,fmt=%s,nr_samples=%d,%s=%s)",
                                               rt, util_format_name(rt_state->format),
                                               rt_state->nr_samples,
                                               state->logicop_enable ? "logicop" : "equation",
                                               state->logicop_enable ?
                                               logicop_str(state->logicop_func) : equation_str);

        const struct util_format_description *format_desc =
                util_format_description(rt_state->format);
        nir_alu_type nir_type = pan_unpacked_type_for_format(format_desc);
        enum glsl_base_type glsl_type = nir_get_glsl_base_type_for_nir_type(nir_type);

        nir_lower_blend_options options = {
                .logicop_enable = state->logicop_enable,
                .logicop_func = state->logicop_func,
                .colormask = rt_state->equation.color_mask,
                .half = nir_type == nir_type_float16,
                .format = rt_state->format,
                .is_bifrost = pan_is_bifrost(dev),
        };

        if (!rt_state->equation.blend_enable) {
                static const nir_lower_blend_channel replace = {
                        .func = BLEND_FUNC_ADD,
                        .src_factor = BLEND_FACTOR_ZERO,
                        .invert_src_factor = true,
                        .dst_factor = BLEND_FACTOR_ZERO,
                        .invert_dst_factor = false,
                };

                options.rgb = replace;
                options.alpha = replace;
        } else {
                options.rgb.func = rt_state->equation.rgb_func;
                options.rgb.src_factor = rt_state->equation.rgb_src_factor;
                options.rgb.invert_src_factor = rt_state->equation.rgb_invert_src_factor;
                options.rgb.dst_factor = rt_state->equation.rgb_dst_factor;
                options.rgb.invert_dst_factor = rt_state->equation.rgb_invert_dst_factor;
                options.alpha.func = rt_state->equation.alpha_func;
                options.alpha.src_factor = rt_state->equation.alpha_src_factor;
                options.alpha.invert_src_factor = rt_state->equation.alpha_invert_src_factor;
                options.alpha.dst_factor = rt_state->equation.alpha_dst_factor;
                options.alpha.invert_dst_factor = rt_state->equation.alpha_invert_dst_factor;
        }

	nir_variable *c_src =
                nir_variable_create(b.shader, nir_var_shader_in,
                                    glsl_vector_type(GLSL_TYPE_FLOAT, 4),
                                    "gl_Color");
        c_src->data.location = VARYING_SLOT_COL0;
        nir_variable *c_src1 =
                nir_variable_create(b.shader, nir_var_shader_in,
                                    glsl_vector_type(GLSL_TYPE_FLOAT, 4),
                                    "gl_Color1");
        c_src1->data.location = VARYING_SLOT_VAR0;
        c_src1->data.driver_location = 1;
        nir_variable *c_out =
                nir_variable_create(b.shader, nir_var_shader_out,
                                    glsl_vector_type(glsl_type, 4),
                                    "gl_FragColor");
        c_out->data.location = FRAG_RESULT_COLOR;

        nir_ssa_def *s_src[] = {nir_load_var(&b, c_src), nir_load_var(&b, c_src1)};

        for (int i = 0; i < ARRAY_SIZE(s_src); ++i) {
                switch (nir_type) {
                case nir_type_float16:
                        s_src[i] = nir_f2f16(&b, s_src[i]);
                        break;
                case nir_type_int16:
                        s_src[i] = nir_i2i16(&b, nir_iclamp(&b, s_src[i], -32768, 32767));
                        break;
                case nir_type_uint16:
                        s_src[i] = nir_u2u16(&b, nir_umin(&b, s_src[i], nir_imm_int(&b, 65535)));
                        break;
                case nir_type_int8:
                        s_src[i] = nir_i2i8(&b, nir_iclamp(&b, s_src[i], -128, 127));
                        break;
                case nir_type_uint8:
                        s_src[i] = nir_u2u8(&b, nir_umin(&b, s_src[i], nir_imm_int(&b, 255)));
                        break;
                default:
                        break;
                }
        }

        /* Build a trivial blend shader */
        nir_store_var(&b, c_out, s_src[0], 0xFF);

        options.src1 = s_src[1];

        NIR_PASS_V(b.shader, nir_lower_blend, options);

        return b.shader;
}
