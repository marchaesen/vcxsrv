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

#ifdef PAN_ARCH
#include "pan_shader.h"
#endif

#include "pan_texture.h"
#include "panfrost/util/pan_lower_framebuffer.h"
#include "util/format/u_format.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_conversion_builder.h"
#include "compiler/nir/nir_lower_blend.h"

#ifndef PAN_ARCH

/* Fixed function blending */

static bool
factor_is_supported(enum blend_factor factor)
{
        return factor != BLEND_FACTOR_SRC_ALPHA_SATURATE &&
               factor != BLEND_FACTOR_SRC1_COLOR &&
               factor != BLEND_FACTOR_SRC1_ALPHA;
}

/* OpenGL allows encoding (src*dest + dest*src) which is incompatiblle with
 * Midgard style blending since there are two multiplies. However, it may be
 * factored as 2*src*dest = dest*(2*src), which can be encoded on Bifrost as 0
 * + dest * (2*src) wih the new source_2 value of C. Detect this case. */

static bool
is_2srcdest(enum blend_func blend_func,
            enum blend_factor src_factor,
            bool invert_src,
            enum blend_factor dest_factor,
            bool invert_dest,
            bool is_alpha)
{
        return (blend_func == BLEND_FUNC_ADD) &&
               ((src_factor == BLEND_FACTOR_DST_COLOR) ||
                ((src_factor == BLEND_FACTOR_DST_ALPHA) && is_alpha)) &&
               ((dest_factor == BLEND_FACTOR_SRC_COLOR) ||
                ((dest_factor == BLEND_FACTOR_SRC_ALPHA) && is_alpha)) &&
               !invert_src && !invert_dest;
}

static bool
can_fixed_function_equation(enum blend_func blend_func,
                            enum blend_factor src_factor,
                            bool invert_src,
                            enum blend_factor dest_factor,
                            bool invert_dest,
                            bool is_alpha,
                            bool supports_2src)
{
        if (is_2srcdest(blend_func, src_factor, invert_src,
                       dest_factor, invert_dest, is_alpha)) {

                return supports_2src;
        }

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
        if (factor == BLEND_FACTOR_CONSTANT_COLOR)
                return 0b0111; /* RGB */
        else if (factor == BLEND_FACTOR_CONSTANT_ALPHA)
                return 0b1000; /* A */
        else
                return 0b0000; /* - */
}

unsigned
pan_blend_constant_mask(const struct pan_blend_equation eq)
{
        return blend_factor_constant_mask(eq.rgb_src_factor) |
               blend_factor_constant_mask(eq.rgb_dst_factor) |
               blend_factor_constant_mask(eq.alpha_src_factor) |
               blend_factor_constant_mask(eq.alpha_dst_factor);
}

/* Only "homogenous" (scalar or vector with all components equal) constants are
 * valid for fixed-function, so check for this condition */

bool
pan_blend_is_homogenous_constant(unsigned mask, const float *constants)
{
        float constant = pan_blend_get_constant(mask, constants);

        u_foreach_bit(i, mask) {
                if (constants[i] != constant)
                        return false;
        }

        return true;
}

/* Determines if an equation can run in fixed function */

bool
pan_blend_can_fixed_function(const struct pan_blend_equation equation,
                             bool supports_2src)
{
        return !equation.blend_enable ||
               (can_fixed_function_equation(equation.rgb_func,
                                            equation.rgb_src_factor,
                                            equation.rgb_invert_src_factor,
                                            equation.rgb_dst_factor,
                                            equation.rgb_invert_dst_factor,
                                            false, supports_2src) &&
                can_fixed_function_equation(equation.alpha_func,
                                            equation.alpha_src_factor,
                                            equation.alpha_invert_src_factor,
                                            equation.alpha_dst_factor,
                                            equation.alpha_invert_dst_factor,
                                            true, supports_2src));
}

static enum mali_blend_operand_c
to_c_factor(enum blend_factor factor)
{
        switch (factor) {
        case BLEND_FACTOR_ZERO:
                return MALI_BLEND_OPERAND_C_ZERO;

        case BLEND_FACTOR_SRC_ALPHA:
                return MALI_BLEND_OPERAND_C_SRC_ALPHA;

        case BLEND_FACTOR_DST_ALPHA:
                return MALI_BLEND_OPERAND_C_DEST_ALPHA;

        case BLEND_FACTOR_SRC_COLOR:
                return MALI_BLEND_OPERAND_C_SRC;

        case BLEND_FACTOR_DST_COLOR:
                return MALI_BLEND_OPERAND_C_DEST;

        case BLEND_FACTOR_CONSTANT_COLOR:
        case BLEND_FACTOR_CONSTANT_ALPHA:
                return MALI_BLEND_OPERAND_C_CONSTANT;

        default:
                unreachable("Unsupported blend factor");
        }
}

static void
to_panfrost_function(enum blend_func blend_func,
                     enum blend_factor src_factor,
                     bool invert_src,
                     enum blend_factor dest_factor,
                     bool invert_dest,
                     bool is_alpha,
                     struct MALI_BLEND_FUNCTION *function)
{
        assert(can_fixed_function_equation(blend_func, src_factor, invert_src,
                                           dest_factor, invert_dest, is_alpha, true));

        if (src_factor == BLEND_FACTOR_ZERO && !invert_src) {
                function->a = MALI_BLEND_OPERAND_A_ZERO;
                function->b = MALI_BLEND_OPERAND_B_DEST;
                if (blend_func == BLEND_FUNC_SUBTRACT)
                        function->negate_b = true;
                function->invert_c = invert_dest;
                function->c = to_c_factor(dest_factor);
        } else if (src_factor == BLEND_FACTOR_ZERO && invert_src) {
                function->a = MALI_BLEND_OPERAND_A_SRC;
                function->b = MALI_BLEND_OPERAND_B_DEST;
                if (blend_func == BLEND_FUNC_SUBTRACT)
                        function->negate_b = true;
                else if (blend_func == BLEND_FUNC_REVERSE_SUBTRACT)
                        function->negate_a = true;
                function->invert_c = invert_dest;
                function->c = to_c_factor(dest_factor);
        } else if (dest_factor == BLEND_FACTOR_ZERO && !invert_dest) {
                function->a = MALI_BLEND_OPERAND_A_ZERO;
                function->b = MALI_BLEND_OPERAND_B_SRC;
                if (blend_func == BLEND_FUNC_REVERSE_SUBTRACT)
                        function->negate_b = true;
                function->invert_c = invert_src;
                function->c = to_c_factor(src_factor);
        } else if (dest_factor == BLEND_FACTOR_ZERO && invert_dest) {
                function->a = MALI_BLEND_OPERAND_A_DEST;
                function->b = MALI_BLEND_OPERAND_B_SRC;
                if (blend_func == BLEND_FUNC_SUBTRACT)
                        function->negate_a = true;
                else if (blend_func == BLEND_FUNC_REVERSE_SUBTRACT)
                        function->negate_b = true;
                function->invert_c = invert_src;
                function->c = to_c_factor(src_factor);
        } else if (src_factor == dest_factor && invert_src == invert_dest) {
                function->a = MALI_BLEND_OPERAND_A_ZERO;
                function->invert_c = invert_src;
                function->c = to_c_factor(src_factor);

                switch (blend_func) {
                case BLEND_FUNC_ADD:
                        function->b = MALI_BLEND_OPERAND_B_SRC_PLUS_DEST;
                        break;
                case BLEND_FUNC_REVERSE_SUBTRACT:
                        function->negate_b = true;
                        FALLTHROUGH;
                case BLEND_FUNC_SUBTRACT:
                        function->b = MALI_BLEND_OPERAND_B_SRC_MINUS_DEST;
                        break;
                default:
                        unreachable("Invalid blend function");
                }
        } else if (is_2srcdest(blend_func, src_factor, invert_src, dest_factor,
                                invert_dest, is_alpha)) {
                /* src*dest + dest*src = 2*src*dest = 0 + dest*(2*src) */
                function->a = MALI_BLEND_OPERAND_A_ZERO;
                function->b = MALI_BLEND_OPERAND_B_DEST;
                function->c = MALI_BLEND_OPERAND_C_SRC_X_2;
        } else {
                assert(src_factor == dest_factor && invert_src != invert_dest);

                function->a = MALI_BLEND_OPERAND_A_DEST;
                function->invert_c = invert_src;
                function->c = to_c_factor(src_factor);

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
pan_blend_is_opaque(const struct pan_blend_equation equation)
{
        /* If a channel is masked out, we can't use opaque mode even if
         * blending is disabled, since we need a tilebuffer read in there */
        if (equation.color_mask != 0xF)
                return false;

        /* With nothing masked out, disabled bledning is opaque */
        if (!equation.blend_enable)
                return true;

        /* Also detect open-coded opaque blending */
        return equation.rgb_src_factor == BLEND_FACTOR_ZERO &&
               equation.rgb_invert_src_factor &&
               equation.rgb_dst_factor == BLEND_FACTOR_ZERO &&
               !equation.rgb_invert_dst_factor &&
               (equation.rgb_func == BLEND_FUNC_ADD ||
                equation.rgb_func == BLEND_FUNC_SUBTRACT) &&
               equation.alpha_src_factor == BLEND_FACTOR_ZERO &&
               equation.alpha_invert_src_factor &&
               equation.alpha_dst_factor == BLEND_FACTOR_ZERO &&
               !equation.alpha_invert_dst_factor &&
               (equation.alpha_func == BLEND_FUNC_ADD ||
                equation.alpha_func == BLEND_FUNC_SUBTRACT);
}

static bool
is_dest_factor(enum blend_factor factor, bool alpha)
{
      return factor == BLEND_FACTOR_DST_ALPHA ||
             factor == BLEND_FACTOR_DST_COLOR ||
             (factor == BLEND_FACTOR_SRC_ALPHA_SATURATE && !alpha);
}

/* Determines if a blend equation reads back the destination. This can occur by
 * explicitly referencing the destination in the blend equation, or by using a
 * partial writemask. */

bool
pan_blend_reads_dest(const struct pan_blend_equation equation)
{
        return (equation.color_mask && equation.color_mask != 0xF) ||
                is_dest_factor(equation.rgb_src_factor, false) ||
                is_dest_factor(equation.alpha_src_factor, true) ||
                equation.rgb_dst_factor != BLEND_FACTOR_ZERO ||
                equation.rgb_invert_dst_factor ||
                equation.alpha_dst_factor != BLEND_FACTOR_ZERO ||
                equation.alpha_invert_dst_factor;
}

/* Create the descriptor for a fixed blend mode given the corresponding API
 * state. Assumes the equation can be represented as fixed-function. */

void
pan_blend_to_fixed_function_equation(const struct pan_blend_equation equation,
                                     struct MALI_BLEND_EQUATION *out)
{
        /* If no blending is enabled, default back on `replace` mode */
        if (!equation.blend_enable) {
                out->color_mask = equation.color_mask;
                out->rgb.a = MALI_BLEND_OPERAND_A_SRC;
                out->rgb.b = MALI_BLEND_OPERAND_B_SRC;
                out->rgb.c = MALI_BLEND_OPERAND_C_ZERO;
                out->alpha.a = MALI_BLEND_OPERAND_A_SRC;
                out->alpha.b = MALI_BLEND_OPERAND_B_SRC;
                out->alpha.c = MALI_BLEND_OPERAND_C_ZERO;
                return;
        }

        /* Compile the fixed-function blend */
        to_panfrost_function(equation.rgb_func,
                             equation.rgb_src_factor,
                             equation.rgb_invert_src_factor,
                             equation.rgb_dst_factor,
                             equation.rgb_invert_dst_factor,
                             false, &out->rgb);

        to_panfrost_function(equation.alpha_func,
                             equation.alpha_src_factor,
                             equation.alpha_invert_src_factor,
                             equation.alpha_dst_factor,
                             equation.alpha_invert_dst_factor,
                             true, &out->alpha);
        out->color_mask = equation.color_mask;
}

uint32_t
pan_pack_blend(const struct pan_blend_equation equation)
{
        STATIC_ASSERT(sizeof(uint32_t) == MALI_BLEND_EQUATION_LENGTH);

        uint32_t out = 0;

        pan_pack(&out, BLEND_EQUATION, cfg) {
                pan_blend_to_fixed_function_equation(equation, &cfg);
        }

        return out;
}

static uint32_t pan_blend_shader_key_hash(const void *key)
{
        return _mesa_hash_data(key, sizeof(struct pan_blend_shader_key));
}

static bool pan_blend_shader_key_equal(const void *a, const void *b)
{
        return !memcmp(a, b, sizeof(struct pan_blend_shader_key));
}

void
pan_blend_shaders_init(struct panfrost_device *dev)
{
        dev->blend_shaders.shaders =
                _mesa_hash_table_create(NULL, pan_blend_shader_key_hash,
                                        pan_blend_shader_key_equal);
        pthread_mutex_init(&dev->blend_shaders.lock, NULL);
}

void
pan_blend_shaders_cleanup(struct panfrost_device *dev)
{
        _mesa_hash_table_destroy(dev->blend_shaders.shaders, NULL);
}

#else /* ifndef PAN_ARCH */

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

static bool
pan_inline_blend_constants(nir_builder *b, nir_instr *instr, void *data)
{
        if (instr->type != nir_instr_type_intrinsic)
                return false;

        nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
        if (intr->intrinsic != nir_intrinsic_load_blend_const_color_rgba)
                return false;

        float *floats = data;
        const nir_const_value constants[4] = {
                { .f32 = floats[0] },
                { .f32 = floats[1] },
                { .f32 = floats[2] },
                { .f32 = floats[3] }
        };

        b->cursor = nir_after_instr(instr);
        nir_ssa_def *constant = nir_build_imm(b, 4, 32, constants);
        nir_ssa_def_rewrite_uses(&intr->dest.ssa, constant);
        nir_instr_remove(instr);
        return true;
}

nir_shader *
GENX(pan_blend_create_shader)(const struct panfrost_device *dev,
                              const struct pan_blend_state *state,
                              nir_alu_type src0_type,
                              nir_alu_type src1_type,
                              unsigned rt)
{
        const struct pan_blend_rt_state *rt_state = &state->rts[rt];
        char equation_str[128] = { 0 };

        get_equation_str(rt_state, equation_str, sizeof(equation_str));

        nir_builder b =
                nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
                                               GENX(pan_shader_get_compiler_options)(),
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
                .rt[0].colormask = rt_state->equation.color_mask,
                .format[0] = rt_state->format
        };

        if (!rt_state->equation.blend_enable) {
                static const nir_lower_blend_channel replace = {
                        .func = BLEND_FUNC_ADD,
                        .src_factor = BLEND_FACTOR_ZERO,
                        .invert_src_factor = true,
                        .dst_factor = BLEND_FACTOR_ZERO,
                        .invert_dst_factor = false,
                };

                options.rt[0].rgb = replace;
                options.rt[0].alpha = replace;
        } else {
                options.rt[0].rgb.func = rt_state->equation.rgb_func;
                options.rt[0].rgb.src_factor = rt_state->equation.rgb_src_factor;
                options.rt[0].rgb.invert_src_factor = rt_state->equation.rgb_invert_src_factor;
                options.rt[0].rgb.dst_factor = rt_state->equation.rgb_dst_factor;
                options.rt[0].rgb.invert_dst_factor = rt_state->equation.rgb_invert_dst_factor;
                options.rt[0].alpha.func = rt_state->equation.alpha_func;
                options.rt[0].alpha.src_factor = rt_state->equation.alpha_src_factor;
                options.rt[0].alpha.invert_src_factor = rt_state->equation.alpha_invert_src_factor;
                options.rt[0].alpha.dst_factor = rt_state->equation.alpha_dst_factor;
                options.rt[0].alpha.invert_dst_factor = rt_state->equation.alpha_invert_dst_factor;
        }

        nir_alu_type src_types[] = { src0_type ?: nir_type_float32, src1_type ?: nir_type_float32 };

        /* HACK: workaround buggy TGSI shaders (u_blitter) */
        for (unsigned i = 0; i < ARRAY_SIZE(src_types); ++i) {
                src_types[i] = nir_alu_type_get_base_type(nir_type) |
                        nir_alu_type_get_type_size(src_types[i]);
        }

	nir_variable *c_src =
                nir_variable_create(b.shader, nir_var_shader_in,
                                    glsl_vector_type(nir_get_glsl_base_type_for_nir_type(src_types[0]), 4),
                                    "gl_Color");
        c_src->data.location = VARYING_SLOT_COL0;
        nir_variable *c_src1 =
                nir_variable_create(b.shader, nir_var_shader_in,
                                    glsl_vector_type(nir_get_glsl_base_type_for_nir_type(src_types[1]), 4),
                                    "gl_Color1");
        c_src1->data.location = VARYING_SLOT_VAR0;
        c_src1->data.driver_location = 1;
        nir_variable *c_out =
                nir_variable_create(b.shader, nir_var_shader_out,
                                    glsl_vector_type(glsl_type, 4),
                                    "gl_FragColor");
        c_out->data.location = FRAG_RESULT_DATA0;

        nir_ssa_def *s_src[] = {nir_load_var(&b, c_src), nir_load_var(&b, c_src1)};

        /* Saturate integer conversions */
        for (int i = 0; i < ARRAY_SIZE(s_src); ++i) {
                nir_alu_type T = nir_alu_type_get_base_type(nir_type);
                s_src[i] = nir_convert_with_rounding(&b, s_src[i],
                                src_types[i], nir_type,
                                nir_rounding_mode_undef,
                                T != nir_type_float);
        }

        /* Build a trivial blend shader */
        nir_store_var(&b, c_out, s_src[0], 0xFF);

        options.src1 = s_src[1];

        NIR_PASS_V(b.shader, nir_lower_blend, options);
        nir_shader_instructions_pass(b.shader, pan_inline_blend_constants,
                        nir_metadata_block_index | nir_metadata_dominance,
                        (void *) state->constants);

        return b.shader;
}

#if PAN_ARCH >= 6
uint64_t
GENX(pan_blend_get_internal_desc)(const struct panfrost_device *dev,
                                  enum pipe_format fmt, unsigned rt,
                                  unsigned force_size, bool dithered)
{
        const struct util_format_description *desc = util_format_description(fmt);
        uint64_t res;

        pan_pack(&res, INTERNAL_BLEND, cfg) {
                cfg.mode = MALI_BLEND_MODE_OPAQUE;
                cfg.fixed_function.num_comps = desc->nr_channels;
                cfg.fixed_function.rt = rt;

                nir_alu_type T = pan_unpacked_type_for_format(desc);

                if (force_size)
                        T = nir_alu_type_get_base_type(T) | force_size;

                switch (T) {
                case nir_type_float16:
                        cfg.fixed_function.conversion.register_format =
                                MALI_REGISTER_FILE_FORMAT_F16;
                        break;
                case nir_type_float32:
                        cfg.fixed_function.conversion.register_format =
                                MALI_REGISTER_FILE_FORMAT_F32;
                        break;
                case nir_type_int8:
                case nir_type_int16:
                        cfg.fixed_function.conversion.register_format =
                                MALI_REGISTER_FILE_FORMAT_I16;
                        break;
                case nir_type_int32:
                        cfg.fixed_function.conversion.register_format =
                                MALI_REGISTER_FILE_FORMAT_I32;
                        break;
                case nir_type_uint8:
                case nir_type_uint16:
                        cfg.fixed_function.conversion.register_format =
                                MALI_REGISTER_FILE_FORMAT_U16;
                        break;
                case nir_type_uint32:
                        cfg.fixed_function.conversion.register_format =
                                MALI_REGISTER_FILE_FORMAT_U32;
                        break;
                default:
                        unreachable("Invalid format");
                }

                cfg.fixed_function.conversion.memory_format =
                         panfrost_format_to_bifrost_blend(dev, fmt, dithered);
        }

        return res;
}
#endif

struct pan_blend_shader_variant *
GENX(pan_blend_get_shader_locked)(const struct panfrost_device *dev,
                                  const struct pan_blend_state *state,
                                  nir_alu_type src0_type,
                                  nir_alu_type src1_type,
                                  unsigned rt)
{
        struct pan_blend_shader_key key = {
                .format = state->rts[rt].format,
                .src0_type = src0_type,
                .src1_type = src1_type,
                .rt = rt,
                .has_constants = pan_blend_constant_mask(state->rts[rt].equation) != 0,
                .logicop_enable = state->logicop_enable,
                .logicop_func = state->logicop_func,
                .nr_samples = state->rts[rt].nr_samples,
                .equation = state->rts[rt].equation,
        };

        struct hash_entry *he = _mesa_hash_table_search(dev->blend_shaders.shaders, &key);
        struct pan_blend_shader *shader = he ? he->data : NULL;

        if (!shader) {
                shader = rzalloc(dev->blend_shaders.shaders, struct pan_blend_shader);
                shader->key = key;
                list_inithead(&shader->variants);
                _mesa_hash_table_insert(dev->blend_shaders.shaders, &shader->key, shader);
        }

        list_for_each_entry(struct pan_blend_shader_variant, iter,
                            &shader->variants, node) {
                if (!key.has_constants ||
                    !memcmp(iter->constants, state->constants, sizeof(iter->constants))) {
                        return iter;
                }
        }

        struct pan_blend_shader_variant *variant = NULL;

        if (shader->nvariants < PAN_BLEND_SHADER_MAX_VARIANTS) {
                variant = rzalloc(shader, struct pan_blend_shader_variant);
                memcpy(variant->constants, state->constants, sizeof(variant->constants));
                util_dynarray_init(&variant->binary, variant);
                list_add(&variant->node, &shader->variants);
                shader->nvariants++;
        } else {
                variant = list_last_entry(&shader->variants, struct pan_blend_shader_variant, node);
                list_del(&variant->node);
                list_add(&variant->node, &shader->variants);
                util_dynarray_clear(&variant->binary);
        }

        nir_shader *nir =
                GENX(pan_blend_create_shader)(dev, state, src0_type, src1_type, rt);

        /* Compile the NIR shader */
        struct panfrost_compile_inputs inputs = {
                .gpu_id = dev->gpu_id,
                .is_blend = true,
                .blend.rt = shader->key.rt,
                .blend.nr_samples = key.nr_samples,
                .rt_formats = { key.format },
        };

#if PAN_ARCH >= 6
        inputs.blend.bifrost_blend_desc =
                GENX(pan_blend_get_internal_desc)(dev, key.format, key.rt, 0, false);
#endif

        struct pan_shader_info info;

        GENX(pan_shader_compile)(nir, &inputs, &variant->binary, &info);

        variant->work_reg_count = info.work_reg_count;

#if PAN_ARCH <= 5
        variant->first_tag = info.midgard.first_tag;
#endif

        ralloc_free(nir);

        return variant;
}
#endif /* ifndef PAN_ARCH */
