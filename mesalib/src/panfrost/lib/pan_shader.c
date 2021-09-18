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

#include "pan_device.h"
#include "pan_shader.h"

#include "panfrost/midgard/midgard_compile.h"
#include "panfrost/bifrost/bifrost_compile.h"

const nir_shader_compiler_options *
pan_shader_get_compiler_options(const struct panfrost_device *dev)
{
        if (pan_is_bifrost(dev))
                return &bifrost_nir_options;

        return &midgard_nir_options;
}

static enum pipe_format
varying_format(nir_alu_type t, unsigned ncomps)
{
#define VARYING_FORMAT(ntype, nsz, ptype, psz) \
        { \
                .type = nir_type_ ## ntype ## nsz, \
                .formats = { \
                        PIPE_FORMAT_R ## psz ## _ ## ptype, \
                        PIPE_FORMAT_R ## psz ## G ## psz ## _ ## ptype, \
                        PIPE_FORMAT_R ## psz ## G ## psz ## B ## psz ## _ ## ptype, \
                        PIPE_FORMAT_R ## psz ## G ## psz ## B ## psz  ## A ## psz ## _ ## ptype, \
                } \
        }

        static const struct {
                nir_alu_type type;
                enum pipe_format formats[4];
        } conv[] = {
                VARYING_FORMAT(float, 32, FLOAT, 32),
                VARYING_FORMAT(int, 32, SINT, 32),
                VARYING_FORMAT(uint, 32, UINT, 32),
                VARYING_FORMAT(float, 16, FLOAT, 16),
                VARYING_FORMAT(int, 16, SINT, 16),
                VARYING_FORMAT(uint, 16, UINT, 16),
                VARYING_FORMAT(int, 8, SINT, 8),
                VARYING_FORMAT(uint, 8, UINT, 8),
                VARYING_FORMAT(bool, 32, UINT, 32),
                VARYING_FORMAT(bool, 16, UINT, 16),
                VARYING_FORMAT(bool, 8, UINT, 8),
                VARYING_FORMAT(bool, 1, UINT, 8),
        };
#undef VARYING_FORMAT

        assert(ncomps > 0 && ncomps <= ARRAY_SIZE(conv[0].formats));

        for (unsigned i = 0; i < ARRAY_SIZE(conv); i++) {
                if (conv[i].type == t)
                        return conv[i].formats[ncomps - 1];
        }

        return PIPE_FORMAT_NONE;
}

static void
collect_varyings(nir_shader *s, nir_variable_mode varying_mode,
                 struct pan_shader_varying *varyings,
                 unsigned *varying_count, bool is_bifrost)
{
        *varying_count = 0;

        unsigned comps[MAX_VARYING] = { 0 };

        nir_foreach_variable_with_modes(var, s, varying_mode) {
                unsigned loc = var->data.driver_location;
                const struct glsl_type *column =
                        glsl_without_array_or_matrix(var->type);
                unsigned chan = glsl_get_components(column);

                /* If we have a fractional location added, we need to increase the size
                 * so it will fit, i.e. a vec3 in YZW requires us to allocate a vec4.
                 * We could do better but this is an edge case as it is, normally
                 * packed varyings will be aligned.
                 */
                chan += var->data.location_frac;
                comps[loc] = MAX2(comps[loc], chan);
        }

        nir_foreach_variable_with_modes(var, s, varying_mode) {
                unsigned loc = var->data.driver_location;
                unsigned sz = glsl_count_attribute_slots(var->type, FALSE);
                const struct glsl_type *column =
                        glsl_without_array_or_matrix(var->type);
                enum glsl_base_type base_type = glsl_get_base_type(column);
                unsigned chan = comps[loc];

                nir_alu_type type = nir_get_nir_type_for_glsl_base_type(base_type);
                type = nir_alu_type_get_base_type(type);

                /* Can't do type conversion since GLSL IR packs in funny ways */
                if (is_bifrost && var->data.interpolation == INTERP_MODE_FLAT)
                        type = nir_type_uint;

                /* Demote to fp16 where possible. int16 varyings are TODO as the hw
                 * will saturate instead of wrap which is not conformant, so we need to
                 * insert i2i16/u2u16 instructions before the st_vary_32i/32u to get
                 * the intended behaviour.
                 */
                if (type == nir_type_float &&
                    (var->data.precision == GLSL_PRECISION_MEDIUM ||
                     var->data.precision == GLSL_PRECISION_LOW) &&
                    !s->info.has_transform_feedback_varyings) {
                        type |= 16;
                } else {
                        type |= 32;
                }

                enum pipe_format format = varying_format(type, chan);
                assert(format != PIPE_FORMAT_NONE);

                for (int c = 0; c < sz; ++c) {
                        varyings[loc + c].location = var->data.location + c;
                        varyings[loc + c].format = format;
                }

                *varying_count = MAX2(*varying_count, loc + sz);
        }
}

static enum mali_bifrost_register_file_format
bifrost_blend_type_from_nir(nir_alu_type nir_type)
{
        switch(nir_type) {
        case 0: /* Render target not in use */
                return 0;
        case nir_type_float16:
                return MALI_BIFROST_REGISTER_FILE_FORMAT_F16;
        case nir_type_float32:
                return MALI_BIFROST_REGISTER_FILE_FORMAT_F32;
        case nir_type_int32:
                return MALI_BIFROST_REGISTER_FILE_FORMAT_I32;
        case nir_type_uint32:
                return MALI_BIFROST_REGISTER_FILE_FORMAT_U32;
        case nir_type_int16:
                return MALI_BIFROST_REGISTER_FILE_FORMAT_I16;
        case nir_type_uint16:
                return MALI_BIFROST_REGISTER_FILE_FORMAT_U16;
        default:
                unreachable("Unsupported blend shader type for NIR alu type");
                return 0;
        }
}

void
pan_shader_compile(const struct panfrost_device *dev,
                   nir_shader *s,
                   const struct panfrost_compile_inputs *inputs,
                   struct util_dynarray *binary,
                   struct pan_shader_info *info)
{
        memset(info, 0, sizeof(*info));

        if (pan_is_bifrost(dev))
                bifrost_compile_shader_nir(s, inputs, binary, info);
        else
                midgard_compile_shader_nir(s, inputs, binary, info);

        info->stage = s->info.stage;
        info->contains_barrier = s->info.uses_memory_barrier ||
                                 s->info.uses_control_barrier;
        info->separable = s->info.separate_shader;

        switch (info->stage) {
        case MESA_SHADER_VERTEX:
                info->attribute_count = util_bitcount64(s->info.inputs_read);

                bool vertex_id = BITSET_TEST(s->info.system_values_read,
                                             SYSTEM_VALUE_VERTEX_ID_ZERO_BASE);
                if (vertex_id && !pan_is_bifrost(dev))
                        info->attribute_count = MAX2(info->attribute_count, PAN_VERTEX_ID + 1);

                bool instance_id = BITSET_TEST(s->info.system_values_read,
                                               SYSTEM_VALUE_INSTANCE_ID);
                if (instance_id && !pan_is_bifrost(dev))
                        info->attribute_count = MAX2(info->attribute_count, PAN_INSTANCE_ID + 1);

                info->vs.writes_point_size =
                        s->info.outputs_written & (1 << VARYING_SLOT_PSIZ);
                collect_varyings(s, nir_var_shader_out, info->varyings.output,
                                 &info->varyings.output_count, pan_is_bifrost(dev));
                break;
        case MESA_SHADER_FRAGMENT:
                if (s->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH))
                        info->fs.writes_depth = true;
                if (s->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL))
                        info->fs.writes_stencil = true;
                if (s->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK))
                        info->fs.writes_coverage = true;

                info->fs.outputs_read = s->info.outputs_read >> FRAG_RESULT_DATA0;
                info->fs.outputs_written = s->info.outputs_written >> FRAG_RESULT_DATA0;

                /* EXT_shader_framebuffer_fetch requires per-sample */
                info->fs.sample_shading = s->info.fs.uses_sample_shading ||
                                          info->fs.outputs_read;

                info->fs.can_discard = s->info.fs.uses_discard;
                info->fs.helper_invocations = s->info.fs.needs_quad_helper_invocations;
                info->fs.early_fragment_tests = s->info.fs.early_fragment_tests;

                /* List of reasons we need to execute frag shaders when things
                 * are masked off */

                info->fs.sidefx = s->info.writes_memory ||
                                  s->info.fs.uses_discard ||
                                  s->info.fs.uses_demote;

                /* With suitable ZSA/blend, is early-z possible? */
                info->fs.can_early_z =
                        !info->fs.sidefx &&
                        !info->fs.writes_depth &&
                        !info->fs.writes_stencil &&
                        !info->fs.writes_coverage;

                /* Similiarly with suitable state, is FPK possible? */
                info->fs.can_fpk =
                        !info->fs.writes_depth &&
                        !info->fs.writes_stencil &&
                        !info->fs.writes_coverage &&
                        !info->fs.can_discard &&
                        !info->fs.outputs_read;

                info->fs.reads_frag_coord =
                        (s->info.inputs_read & (1 << VARYING_SLOT_POS)) ||
                        BITSET_TEST(s->info.system_values_read, SYSTEM_VALUE_FRAG_COORD);
                info->fs.reads_point_coord =
                        s->info.inputs_read & (1 << VARYING_SLOT_PNTC);
                info->fs.reads_face =
                        (s->info.inputs_read & (1 << VARYING_SLOT_FACE)) ||
                        BITSET_TEST(s->info.system_values_read, SYSTEM_VALUE_FRONT_FACE);
                info->fs.reads_sample_id =
                        BITSET_TEST(s->info.system_values_read, SYSTEM_VALUE_SAMPLE_ID);
                info->fs.reads_sample_pos =
                        BITSET_TEST(s->info.system_values_read, SYSTEM_VALUE_SAMPLE_POS);
                info->fs.reads_sample_mask_in =
                        BITSET_TEST(s->info.system_values_read, SYSTEM_VALUE_SAMPLE_MASK_IN);
                info->fs.reads_helper_invocation =
                        BITSET_TEST(s->info.system_values_read, SYSTEM_VALUE_HELPER_INVOCATION);
                collect_varyings(s, nir_var_shader_in, info->varyings.input,
                                 &info->varyings.input_count, pan_is_bifrost(dev));
                break;
        case MESA_SHADER_COMPUTE:
                info->wls_size = s->info.shared_size;
                break;
        default:
                unreachable("Unknown shader state");
        }

        info->outputs_written = s->info.outputs_written;

        /* Sysvals have dedicated UBO */
        if (info->sysvals.sysval_count)
                info->ubo_count = MAX2(s->info.num_ubos + 1, inputs->sysval_ubo + 1);
        else
                info->ubo_count = s->info.num_ubos;

        info->attribute_count += util_last_bit(s->info.images_used);
        info->writes_global = s->info.writes_memory;

        info->sampler_count = info->texture_count = BITSET_LAST_BIT(s->info.textures_used);

        /* This is "redundant" information, but is needed in a draw-time hot path */
        if (pan_is_bifrost(dev)) {
                for (unsigned i = 0; i < ARRAY_SIZE(info->bifrost.blend); ++i) {
                        info->bifrost.blend[i].format =
                                bifrost_blend_type_from_nir(info->bifrost.blend[i].type);
                }
        }
}
