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
#include "compiler/nir/nir_conversion_builder.h"

/* Fixed function blending */

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

        /* v7 only uses the constant from RT 0 (TODO: what if it's the same
         * constant? or a constant is shared?) */
        if (dev->arch == 7 && rt > 0)
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
        if (!panfrost_blendable_formats[rt_state->format].internal)
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
                     struct MALI_BLEND_FUNCTION *function)
{
        assert(can_fixed_function_equation(blend_func, src_factor, dest_factor));

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

nir_shader *
pan_blend_create_shader(const struct panfrost_device *dev,
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
                .scalar = pan_is_bifrost(dev),
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
                bool is_float = nir_alu_type_get_base_type(nir_type);
                s_src[i] = nir_convert_with_rounding(&b, s_src[i],
                                src_types[i], nir_type,
                                nir_rounding_mode_undef,
                                !is_float);
        }

        /* Build a trivial blend shader */
        nir_store_var(&b, c_out, s_src[0], 0xFF);

        options.src1 = s_src[1];

        NIR_PASS_V(b.shader, nir_lower_blend, options);

        return b.shader;
}

uint64_t
pan_blend_get_bifrost_desc(const struct panfrost_device *dev,
                           enum pipe_format fmt, unsigned rt,
                           unsigned force_size)
{
        const struct util_format_description *desc = util_format_description(fmt);
        uint64_t res;

        pan_pack(&res, BIFROST_INTERNAL_BLEND, cfg) {
                cfg.mode = MALI_BIFROST_BLEND_MODE_OPAQUE;
                cfg.fixed_function.num_comps = desc->nr_channels;
                cfg.fixed_function.rt = rt;

                nir_alu_type T = pan_unpacked_type_for_format(desc);

                if (force_size)
                        T = nir_alu_type_get_base_type(T) | force_size;

                switch (T) {
                case nir_type_float16:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_F16;
                        break;
                case nir_type_float32:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_F32;
                        break;
                case nir_type_int8:
                case nir_type_int16:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_I16;
                        break;
                case nir_type_int32:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_I32;
                        break;
                case nir_type_uint8:
                case nir_type_uint16:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_U16;
                        break;
                case nir_type_uint32:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_U32;
                        break;
                default:
                        unreachable("Invalid format");
                }

                cfg.fixed_function.conversion.memory_format =
                         panfrost_format_to_bifrost_blend(dev, fmt);
        }

        return res;
}

struct pan_blend_shader_variant *
pan_blend_get_shader_locked(const struct panfrost_device *dev,
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
                .has_constants = pan_blend_constant_mask(state, rt) != 0,
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

        nir_shader *nir = pan_blend_create_shader(dev, state, src0_type, src1_type, rt);

        /* Compile the NIR shader */
        struct panfrost_compile_inputs inputs = {
                .gpu_id = dev->gpu_id,
                .is_blend = true,
                .blend.rt = shader->key.rt,
                .blend.nr_samples = key.nr_samples,
                .rt_formats = { key.format },
        };

        if (key.has_constants)
                memcpy(inputs.blend.constants, state->constants, sizeof(inputs.blend.constants));

        if (pan_is_bifrost(dev)) {
                inputs.blend.bifrost_blend_desc =
                        pan_blend_get_bifrost_desc(dev, key.format, key.rt, 0);
        }

        struct pan_shader_info info;

        pan_shader_compile(dev, nir, &inputs, &variant->binary, &info);

        variant->work_reg_count = info.work_reg_count;
        if (!pan_is_bifrost(dev))
                variant->first_tag = info.midgard.first_tag;

        ralloc_free(nir);

        return variant;
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
