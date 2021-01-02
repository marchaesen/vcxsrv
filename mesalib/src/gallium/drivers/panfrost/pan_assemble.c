/*
 * © Copyright 2018 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
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
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pan_bo.h"
#include "pan_context.h"
#include "pan_util.h"
#include "panfrost-quirks.h"

#include "compiler/nir/nir.h"
#include "nir/tgsi_to_nir.h"
#include "midgard/midgard_compile.h"
#include "bifrost/bifrost_compile.h"
#include "util/u_dynarray.h"
#include "util/u_upload_mgr.h"

#include "tgsi/tgsi_dump.h"

static void
pan_prepare_midgard_props(struct panfrost_shader_state *state,
                          gl_shader_stage stage)
{
        pan_prepare(&state->properties, RENDERER_PROPERTIES);
        state->properties.uniform_buffer_count = state->ubo_count;
        state->properties.midgard.uniform_count = state->uniform_count;
        state->properties.midgard.shader_has_side_effects = state->writes_global;

        /* TODO: Select the appropriate mode. Suppresing inf/nan works around
         * some bugs in gles2 apps (eg glmark2's terrain scene) but isn't
         * conformant on gles3 */
        state->properties.midgard.fp_mode = MALI_FP_MODE_GL_INF_NAN_SUPPRESSED;

        /* For fragment shaders, work register count, early-z, reads at draw-time */

        if (stage != MESA_SHADER_FRAGMENT)
                state->properties.midgard.work_register_count = state->work_reg_count;
}

static void
pan_prepare_bifrost_props(struct panfrost_shader_state *state,
                          gl_shader_stage stage)
{

        switch (stage) {
        case MESA_SHADER_VERTEX:
                pan_prepare(&state->properties, RENDERER_PROPERTIES);
                state->properties.bifrost.zs_update_operation = MALI_PIXEL_KILL_STRONG_EARLY;
                state->properties.uniform_buffer_count = state->ubo_count;

                pan_prepare(&state->preload, PRELOAD);
                state->preload.uniform_count = state->uniform_count;
                state->preload.vertex.vertex_id = true;
                state->preload.vertex.instance_id = true;
                break;
        case MESA_SHADER_FRAGMENT:
                pan_prepare(&state->properties, RENDERER_PROPERTIES);
                /* Early-Z set at draw-time */
                if (state->writes_depth || state->writes_stencil) {
                        state->properties.bifrost.zs_update_operation = MALI_PIXEL_KILL_FORCE_LATE;
                        state->properties.bifrost.pixel_kill_operation = MALI_PIXEL_KILL_FORCE_LATE;
                } else if (state->can_discard) {
                        state->properties.bifrost.zs_update_operation = MALI_PIXEL_KILL_FORCE_LATE;
                        state->properties.bifrost.pixel_kill_operation = MALI_PIXEL_KILL_WEAK_EARLY;
                } else {
                        state->properties.bifrost.zs_update_operation = MALI_PIXEL_KILL_STRONG_EARLY;
                        state->properties.bifrost.pixel_kill_operation = MALI_PIXEL_KILL_FORCE_EARLY;
                }
                state->properties.uniform_buffer_count = state->ubo_count;
                state->properties.bifrost.shader_modifies_coverage = state->can_discard;

                pan_prepare(&state->preload, PRELOAD);
                state->preload.uniform_count = state->uniform_count;
                state->preload.fragment.fragment_position = state->reads_frag_coord;
                state->preload.fragment.coverage = true;
                state->preload.fragment.primitive_flags = state->reads_face;
                break;
        default:
                unreachable("TODO");
        }
}

static void
pan_upload_shader_descriptor(struct panfrost_context *ctx,
                        struct panfrost_shader_state *state)
{
        const struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct mali_state_packed *out;

        u_upload_alloc(ctx->state_uploader, 0, MALI_RENDERER_STATE_LENGTH, MALI_RENDERER_STATE_LENGTH,
                        &state->upload.offset, &state->upload.rsrc, (void **) &out);

        pan_pack(out, RENDERER_STATE, cfg) {
                cfg.shader = state->shader;
                cfg.properties = state->properties;

                if (dev->quirks & IS_BIFROST)
                        cfg.preload = state->preload;
        }

        u_upload_unmap(ctx->state_uploader);
}

static unsigned
pan_format_from_nir_base(nir_alu_type base)
{
        switch (base) {
        case nir_type_int:
                return MALI_FORMAT_SINT;
        case nir_type_uint:
        case nir_type_bool:
                return MALI_FORMAT_UINT;
        case nir_type_float:
                return MALI_CHANNEL_FLOAT;
        default:
                unreachable("Invalid base");
        }
}

static unsigned
pan_format_from_nir_size(nir_alu_type base, unsigned size)
{
        if (base == nir_type_float) {
                switch (size) {
                case 16: return MALI_FORMAT_SINT;
                case 32: return MALI_FORMAT_UNORM;
                default:
                        unreachable("Invalid float size for format");
                }
        } else {
                switch (size) {
                case 1:
                case 8:  return MALI_CHANNEL_8;
                case 16: return MALI_CHANNEL_16;
                case 32: return MALI_CHANNEL_32;
                default:
                         unreachable("Invalid int size for format");
                }
        }
}

static enum mali_format
pan_format_from_glsl(const struct glsl_type *type, unsigned precision, unsigned frac)
{
        const struct glsl_type *column = glsl_without_array_or_matrix(type);
        enum glsl_base_type glsl_base = glsl_get_base_type(column);
        nir_alu_type t = nir_get_nir_type_for_glsl_base_type(glsl_base);
        unsigned chan = glsl_get_components(column);

        /* If we have a fractional location added, we need to increase the size
         * so it will fit, i.e. a vec3 in YZW requires us to allocate a vec4.
         * We could do better but this is an edge case as it is, normally
         * packed varyings will be aligned. */
        chan += frac;

        assert(chan >= 1 && chan <= 4);

        unsigned base = nir_alu_type_get_base_type(t);
        unsigned size = nir_alu_type_get_type_size(t);

        /* Demote to fp16 where possible. int16 varyings are TODO as the hw
         * will saturate instead of wrap which is not conformant, so we need to
         * insert i2i16/u2u16 instructions before the st_vary_32i/32u to get
         * the intended behaviour */

        bool is_16 = (precision == GLSL_PRECISION_MEDIUM)
                || (precision == GLSL_PRECISION_LOW);

        if (is_16 && base == nir_type_float)
                size = 16;
        else
                size = 32;

        return pan_format_from_nir_base(base) |
                pan_format_from_nir_size(base, size) |
                MALI_NR_CHANNELS(chan);
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
panfrost_shader_compile(struct panfrost_context *ctx,
                        enum pipe_shader_ir ir_type,
                        const void *ir,
                        gl_shader_stage stage,
                        struct panfrost_shader_state *state,
                        uint64_t *outputs_written)
{
        struct panfrost_device *dev = pan_device(ctx->base.screen);

        nir_shader *s;

        if (ir_type == PIPE_SHADER_IR_NIR) {
                s = nir_shader_clone(NULL, ir);
        } else {
                assert (ir_type == PIPE_SHADER_IR_TGSI);
                s = tgsi_to_nir(ir, ctx->base.screen, false);
        }

        s->info.stage = stage;

        /* Call out to Midgard compiler given the above NIR */
        struct panfrost_compile_inputs inputs = {
                .gpu_id = dev->gpu_id,
                .shaderdb = !!(dev->debug & PAN_DBG_PRECOMPILE),
        };

        memcpy(inputs.rt_formats, state->rt_formats, sizeof(inputs.rt_formats));

        panfrost_program *program;

        if (dev->quirks & IS_BIFROST)
                program = bifrost_compile_shader_nir(NULL, s, &inputs);
        else
                program = midgard_compile_shader_nir(NULL, s, &inputs);

        /* Prepare the compiled binary for upload */
        mali_ptr shader = 0;
        unsigned attribute_count = 0, varying_count = 0;
        int size = program->compiled.size;

        if (size) {
                state->bo = panfrost_bo_create(dev, size, PAN_BO_EXECUTE);
                memcpy(state->bo->ptr.cpu, program->compiled.data, size);
                shader = state->bo->ptr.gpu;
        }

        /* Midgard needs the first tag on the bottom nibble */

        if (!(dev->quirks & IS_BIFROST)) {
                /* If size = 0, we tag as "end-of-shader" */

                if (size)
                        shader |= program->first_tag;
                else
                        shader = 0x1;
        }

        state->sysval_count = program->sysval_count;
        memcpy(state->sysval, program->sysvals, sizeof(state->sysval[0]) * state->sysval_count);

        bool vertex_id = s->info.system_values_read & (1 << SYSTEM_VALUE_VERTEX_ID);
        bool instance_id = s->info.system_values_read & (1 << SYSTEM_VALUE_INSTANCE_ID);

        state->writes_global = s->info.writes_memory;

        switch (stage) {
        case MESA_SHADER_VERTEX:
                attribute_count = util_bitcount64(s->info.inputs_read);
                varying_count = util_bitcount64(s->info.outputs_written);

                if (vertex_id)
                        attribute_count = MAX2(attribute_count, PAN_VERTEX_ID + 1);

                if (instance_id)
                        attribute_count = MAX2(attribute_count, PAN_INSTANCE_ID + 1);

                break;
        case MESA_SHADER_FRAGMENT:
                for (unsigned i = 0; i < ARRAY_SIZE(state->blend_ret_addrs); i++) {
                        if (!program->blend_ret_offsets[i])
                                continue;

                        state->blend_ret_addrs[i] = (state->bo->ptr.gpu & UINT32_MAX) +
                                                    program->blend_ret_offsets[i];
                        assert(!(state->blend_ret_addrs[i] & 0x7));
                }
                varying_count = util_bitcount64(s->info.inputs_read);
                if (s->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH))
                        state->writes_depth = true;
                if (s->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_STENCIL))
                        state->writes_stencil = true;

                uint64_t outputs_read = s->info.outputs_read;
                if (outputs_read & BITFIELD64_BIT(FRAG_RESULT_COLOR))
                        outputs_read |= BITFIELD64_BIT(FRAG_RESULT_DATA0);

                state->outputs_read = outputs_read >> FRAG_RESULT_DATA0;

                /* List of reasons we need to execute frag shaders when things
                 * are masked off */

                state->fs_sidefx =
                        s->info.writes_memory ||
                        s->info.fs.uses_discard ||
                        s->info.fs.uses_demote;
                break;
        case MESA_SHADER_COMPUTE:
                /* TODO: images */
                state->shared_size = s->info.cs.shared_size;
                break;
        default:
                unreachable("Unknown shader state");
        }

        state->can_discard = s->info.fs.uses_discard;
        state->helper_invocations = s->info.fs.needs_quad_helper_invocations;
        state->stack_size = program->tls_size;

        state->reads_frag_coord = (s->info.inputs_read & (1 << VARYING_SLOT_POS)) ||
                                  (s->info.system_values_read & (1 << SYSTEM_VALUE_FRAG_COORD));
        state->reads_point_coord = s->info.inputs_read & (1 << VARYING_SLOT_PNTC);
        state->reads_face = (s->info.inputs_read & (1 << VARYING_SLOT_FACE)) ||
                            (s->info.system_values_read & (1 << SYSTEM_VALUE_FRONT_FACE));
        state->writes_point_size = s->info.outputs_written & (1 << VARYING_SLOT_PSIZ);

        if (outputs_written)
                *outputs_written = s->info.outputs_written;

        /* Separate as primary uniform count is truncated. Sysvals are prefix
         * uniforms */
        state->uniform_count = MIN2(s->num_uniforms + program->sysval_count, program->uniform_cutoff);
        state->work_reg_count = program->work_register_count;

        if (dev->quirks & IS_BIFROST)
                for (unsigned i = 0; i < ARRAY_SIZE(state->blend_types); i++)
                        state->blend_types[i] = bifrost_blend_type_from_nir(program->blend_types[i]);

        /* Record the varying mapping for the command stream's bookkeeping */

        nir_variable_mode varying_mode =
                        stage == MESA_SHADER_VERTEX ? nir_var_shader_out : nir_var_shader_in;

        nir_foreach_variable_with_modes(var, s, varying_mode) {
                unsigned loc = var->data.driver_location;
                unsigned sz = glsl_count_attribute_slots(var->type, FALSE);

                for (int c = 0; c < sz; ++c) {
                        state->varyings_loc[loc + c] = var->data.location + c;
                        state->varyings[loc + c] = pan_format_from_glsl(var->type,
                                        var->data.precision, var->data.location_frac);
                }
        }

        /* Needed for linkage */
        state->attribute_count = attribute_count;
        state->varying_count = varying_count;
        state->ubo_count = s->info.num_ubos + 1; /* off-by-one for uniforms */

        /* Prepare the descriptors at compile-time */
        state->shader.shader = shader;
        state->shader.attribute_count = attribute_count;
        state->shader.varying_count = varying_count;
        state->shader.texture_count = s->info.num_textures;
        state->shader.sampler_count = s->info.num_textures;

        if (dev->quirks & IS_BIFROST)
                pan_prepare_bifrost_props(state, stage);
        else
                pan_prepare_midgard_props(state, stage);

        state->properties.stencil_from_shader = state->writes_stencil;
        state->properties.shader_contains_barrier = state->helper_invocations;
        state->properties.depth_source = state->writes_depth ?
                                         MALI_DEPTH_SOURCE_SHADER :
                                         MALI_DEPTH_SOURCE_FIXED_FUNCTION;

        if (stage != MESA_SHADER_FRAGMENT)
                pan_upload_shader_descriptor(ctx, state);

        ralloc_free(program);

        /* In both clone and tgsi_to_nir paths, the shader is ralloc'd against
         * a NULL context */
        ralloc_free(s);
}
