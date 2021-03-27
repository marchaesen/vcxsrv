/*
 * Copyright © 2014-2017 Broadcom
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

#include <inttypes.h>
#include "util/format/u_format.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/ralloc.h"
#include "util/hash_table.h"
#include "util/u_upload_mgr.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_parse.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "nir/tgsi_to_nir.h"
#include "compiler/v3d_compiler.h"
#include "v3d_context.h"
#include "broadcom/cle/v3d_packet_v33_pack.h"

static struct v3d_compiled_shader *
v3d_get_compiled_shader(struct v3d_context *v3d,
                        struct v3d_key *key, size_t key_size);
static void
v3d_setup_shared_precompile_key(struct v3d_uncompiled_shader *uncompiled,
                                struct v3d_key *key);

static gl_varying_slot
v3d_get_slot_for_driver_location(nir_shader *s, uint32_t driver_location)
{
        nir_foreach_shader_out_variable(var, s) {
                if (var->data.driver_location == driver_location) {
                        return var->data.location;
                }
        }

        return -1;
}

/**
 * Precomputes the TRANSFORM_FEEDBACK_OUTPUT_DATA_SPEC array for the shader.
 *
 * A shader can have 16 of these specs, and each one of them can write up to
 * 16 dwords.  Since we allow a total of 64 transform feedback output
 * components (not 16 vectors), we have to group the writes of multiple
 * varyings together in a single data spec.
 */
static void
v3d_set_transform_feedback_outputs(struct v3d_uncompiled_shader *so,
                                   const struct pipe_stream_output_info *stream_output)
{
        if (!stream_output->num_outputs)
                return;

        struct v3d_varying_slot slots[PIPE_MAX_SO_OUTPUTS * 4];
        int slot_count = 0;

        for (int buffer = 0; buffer < PIPE_MAX_SO_BUFFERS; buffer++) {
                uint32_t buffer_offset = 0;
                uint32_t vpm_start = slot_count;

                for (int i = 0; i < stream_output->num_outputs; i++) {
                        const struct pipe_stream_output *output =
                                &stream_output->output[i];

                        if (output->output_buffer != buffer)
                                continue;

                        /* We assume that the SO outputs appear in increasing
                         * order in the buffer.
                         */
                        assert(output->dst_offset >= buffer_offset);

                        /* Pad any undefined slots in the output */
                        for (int j = buffer_offset; j < output->dst_offset; j++) {
                                slots[slot_count] =
                                        v3d_slot_from_slot_and_component(VARYING_SLOT_POS, 0);
                                slot_count++;
                                buffer_offset++;
                        }

                        /* Set the coordinate shader up to output the
                         * components of this varying.
                         */
                        for (int j = 0; j < output->num_components; j++) {
                                gl_varying_slot slot =
                                        v3d_get_slot_for_driver_location(so->base.ir.nir, output->register_index);

                                slots[slot_count] =
                                        v3d_slot_from_slot_and_component(slot,
                                                                         output->start_component + j);
                                slot_count++;
                                buffer_offset++;
                        }
                }

                uint32_t vpm_size = slot_count - vpm_start;
                if (!vpm_size)
                        continue;

                uint32_t vpm_start_offset = vpm_start + 6;

                while (vpm_size) {
                        uint32_t write_size = MIN2(vpm_size, 1 << 4);

                        struct V3D33_TRANSFORM_FEEDBACK_OUTPUT_DATA_SPEC unpacked = {
                                /* We need the offset from the coordinate shader's VPM
                                 * output block, which has the [X, Y, Z, W, Xs, Ys]
                                 * values at the start.
                                 */
                                .first_shaded_vertex_value_to_output = vpm_start_offset,
                                .number_of_consecutive_vertex_values_to_output_as_32_bit_values = write_size,
                                .output_buffer_to_write_to = buffer,
                        };

                        /* GFXH-1559 */
                        assert(unpacked.first_shaded_vertex_value_to_output != 8 ||
                               so->num_tf_specs != 0);

                        assert(so->num_tf_specs != ARRAY_SIZE(so->tf_specs));
                        V3D33_TRANSFORM_FEEDBACK_OUTPUT_DATA_SPEC_pack(NULL,
                                                                       (void *)&so->tf_specs[so->num_tf_specs],
                                                                       &unpacked);

                        /* If point size is being written by the shader, then
                         * all the VPM start offsets are shifted up by one.
                         * We won't know that until the variant is compiled,
                         * though.
                         */
                        unpacked.first_shaded_vertex_value_to_output++;

                        /* GFXH-1559 */
                        assert(unpacked.first_shaded_vertex_value_to_output != 8 ||
                               so->num_tf_specs != 0);

                        V3D33_TRANSFORM_FEEDBACK_OUTPUT_DATA_SPEC_pack(NULL,
                                                                       (void *)&so->tf_specs_psiz[so->num_tf_specs],
                                                                       &unpacked);
                        so->num_tf_specs++;
                        vpm_start_offset += write_size;
                        vpm_size -= write_size;
                }
                so->base.stream_output.stride[buffer] =
                        stream_output->stride[buffer];
        }

        so->num_tf_outputs = slot_count;
        so->tf_outputs = ralloc_array(so->base.ir.nir, struct v3d_varying_slot,
                                      slot_count);
        memcpy(so->tf_outputs, slots, sizeof(*slots) * slot_count);
}

static int
type_size(const struct glsl_type *type, bool bindless)
{
        return glsl_count_attribute_slots(type, false);
}

static void
precompile_all_outputs(nir_shader *s,
                       struct v3d_varying_slot *outputs,
                       uint8_t *num_outputs)
{
        nir_foreach_shader_out_variable(var, s) {
                const int array_len = MAX2(glsl_get_length(var->type), 1);
                for (int j = 0; j < array_len; j++) {
                        const int slot = var->data.location + j;
                        const int num_components =
                                glsl_get_components(var->type);
                        for (int i = 0; i < num_components; i++) {
                                const int swiz = var->data.location_frac + i;
                                outputs[(*num_outputs)++] =
                                        v3d_slot_from_slot_and_component(slot,
                                                                         swiz);
                        }
                }
        }
}

/**
 * Precompiles a shader variant at shader state creation time if
 * V3D_DEBUG=precompile is set.  Used for shader-db
 * (https://gitlab.freedesktop.org/mesa/shader-db)
 */
static void
v3d_shader_precompile(struct v3d_context *v3d,
                      struct v3d_uncompiled_shader *so)
{
        nir_shader *s = so->base.ir.nir;

        if (s->info.stage == MESA_SHADER_FRAGMENT) {
                struct v3d_fs_key key = {
                        .base.shader_state = so,
                };

                nir_foreach_shader_out_variable(var, s) {
                        if (var->data.location == FRAG_RESULT_COLOR) {
                                key.cbufs |= 1 << 0;
                        } else if (var->data.location >= FRAG_RESULT_DATA0) {
                                key.cbufs |= 1 << (var->data.location -
                                                   FRAG_RESULT_DATA0);
                        }
                }

                key.logicop_func = PIPE_LOGICOP_COPY;

                v3d_setup_shared_precompile_key(so, &key.base);
                v3d_get_compiled_shader(v3d, &key.base, sizeof(key));
        } else if (s->info.stage == MESA_SHADER_GEOMETRY) {
                struct v3d_gs_key key = {
                        .base.shader_state = so,
                        .base.is_last_geometry_stage = true,
                };

                v3d_setup_shared_precompile_key(so, &key.base);

                precompile_all_outputs(s,
                                       key.used_outputs,
                                       &key.num_used_outputs);

                v3d_get_compiled_shader(v3d, &key.base, sizeof(key));

                /* Compile GS bin shader: only position (XXX: include TF) */
                key.is_coord = true;
                key.num_used_outputs = 0;
                for (int i = 0; i < 4; i++) {
                        key.used_outputs[key.num_used_outputs++] =
                                v3d_slot_from_slot_and_component(VARYING_SLOT_POS,
                                                                 i);
                }
                v3d_get_compiled_shader(v3d, &key.base, sizeof(key));
        } else {
                assert(s->info.stage == MESA_SHADER_VERTEX);
                struct v3d_vs_key key = {
                        .base.shader_state = so,
                        /* Emit fixed function outputs */
                        .base.is_last_geometry_stage = true,
                };

                v3d_setup_shared_precompile_key(so, &key.base);

                precompile_all_outputs(s,
                                       key.used_outputs,
                                       &key.num_used_outputs);

                v3d_get_compiled_shader(v3d, &key.base, sizeof(key));

                /* Compile VS bin shader: only position (XXX: include TF) */
                key.is_coord = true;
                key.num_used_outputs = 0;
                for (int i = 0; i < 4; i++) {
                        key.used_outputs[key.num_used_outputs++] =
                                v3d_slot_from_slot_and_component(VARYING_SLOT_POS,
                                                                 i);
                }
                v3d_get_compiled_shader(v3d, &key.base, sizeof(key));
        }
}

static void *
v3d_uncompiled_shader_create(struct pipe_context *pctx,
                             enum pipe_shader_ir type, void *ir)
{
        struct v3d_context *v3d = v3d_context(pctx);
        struct v3d_uncompiled_shader *so = CALLOC_STRUCT(v3d_uncompiled_shader);
        if (!so)
                return NULL;

        so->program_id = v3d->next_uncompiled_program_id++;

        nir_shader *s;

        if (type == PIPE_SHADER_IR_NIR) {
                /* The backend takes ownership of the NIR shader on state
                 * creation.
                 */
                s = ir;
        } else {
                assert(type == PIPE_SHADER_IR_TGSI);

                if (V3D_DEBUG & V3D_DEBUG_TGSI) {
                        fprintf(stderr, "prog %d TGSI:\n",
                                so->program_id);
                        tgsi_dump(ir, 0);
                        fprintf(stderr, "\n");
                }
                s = tgsi_to_nir(ir, pctx->screen, false);
        }

        if (s->info.stage != MESA_SHADER_VERTEX &&
            s->info.stage != MESA_SHADER_GEOMETRY) {
                NIR_PASS_V(s, nir_lower_io,
                           nir_var_shader_in | nir_var_shader_out,
                           type_size, (nir_lower_io_options)0);
        }

        NIR_PASS_V(s, nir_lower_regs_to_ssa);
        NIR_PASS_V(s, nir_normalize_cubemap_coords);

        NIR_PASS_V(s, nir_lower_load_const_to_scalar);

        v3d_optimize_nir(s);

        NIR_PASS_V(s, nir_remove_dead_variables, nir_var_function_temp, NULL);

        /* Garbage collect dead instructions */
        nir_sweep(s);

        so->base.type = PIPE_SHADER_IR_NIR;
        so->base.ir.nir = s;

        if (V3D_DEBUG & (V3D_DEBUG_NIR |
                         v3d_debug_flag_for_shader_stage(s->info.stage))) {
                fprintf(stderr, "%s prog %d NIR:\n",
                        gl_shader_stage_name(s->info.stage),
                        so->program_id);
                nir_print_shader(s, stderr);
                fprintf(stderr, "\n");
        }

        if (V3D_DEBUG & V3D_DEBUG_PRECOMPILE)
                v3d_shader_precompile(v3d, so);

        return so;
}

static void
v3d_shader_debug_output(const char *message, void *data)
{
        struct v3d_context *v3d = data;

        pipe_debug_message(&v3d->debug, SHADER_INFO, "%s", message);
}

static void *
v3d_shader_state_create(struct pipe_context *pctx,
                        const struct pipe_shader_state *cso)
{
        struct v3d_uncompiled_shader *so =
                v3d_uncompiled_shader_create(pctx,
                                             cso->type,
                                             (cso->type == PIPE_SHADER_IR_TGSI ?
                                              (void *)cso->tokens :
                                              cso->ir.nir));

        v3d_set_transform_feedback_outputs(so, &cso->stream_output);

        return so;
}

struct v3d_compiled_shader *
v3d_get_compiled_shader(struct v3d_context *v3d,
                        struct v3d_key *key,
                        size_t key_size)
{
        struct v3d_uncompiled_shader *shader_state = key->shader_state;
        nir_shader *s = shader_state->base.ir.nir;

        struct hash_table *ht = v3d->prog.cache[s->info.stage];
        struct hash_entry *entry = _mesa_hash_table_search(ht, key);
        if (entry)
                return entry->data;

        struct v3d_compiled_shader *shader =
                rzalloc(NULL, struct v3d_compiled_shader);

        int program_id = shader_state->program_id;
        int variant_id =
                p_atomic_inc_return(&shader_state->compiled_variant_count);
        uint64_t *qpu_insts;
        uint32_t shader_size;

        qpu_insts = v3d_compile(v3d->screen->compiler, key,
                                &shader->prog_data.base, s,
                                v3d_shader_debug_output,
                                v3d,
                                program_id, variant_id, &shader_size);
        ralloc_steal(shader, shader->prog_data.base);

        v3d_set_shader_uniform_dirty_flags(shader);

        if (shader_size) {
                u_upload_data(v3d->state_uploader, 0, shader_size, 8,
                              qpu_insts, &shader->offset, &shader->resource);
        }

        free(qpu_insts);

        if (ht) {
                struct v3d_key *dup_key;
                dup_key = ralloc_size(shader, key_size);
                memcpy(dup_key, key, key_size);
                _mesa_hash_table_insert(ht, dup_key, shader);
        }

        if (shader->prog_data.base->spill_size >
            v3d->prog.spill_size_per_thread) {
                /* The TIDX register we use for choosing the area to access
                 * for scratch space is: (core << 6) | (qpu << 2) | thread.
                 * Even at minimum threadcount in a particular shader, that
                 * means we still multiply by qpus by 4.
                 */
                int total_spill_size = (v3d->screen->devinfo.qpu_count * 4 *
                                        shader->prog_data.base->spill_size);

                v3d_bo_unreference(&v3d->prog.spill_bo);
                v3d->prog.spill_bo = v3d_bo_alloc(v3d->screen,
                                                  total_spill_size, "spill");
                v3d->prog.spill_size_per_thread =
                        shader->prog_data.base->spill_size;
        }

        return shader;
}

static void
v3d_free_compiled_shader(struct v3d_compiled_shader *shader)
{
        pipe_resource_reference(&shader->resource, NULL);
        ralloc_free(shader);
}

static void
v3d_setup_shared_key(struct v3d_context *v3d, struct v3d_key *key,
                     struct v3d_texture_stateobj *texstate)
{
        const struct v3d_device_info *devinfo = &v3d->screen->devinfo;

        key->num_tex_used = texstate->num_textures;
        key->num_samplers_used = texstate->num_textures;
        assert(key->num_tex_used == key->num_samplers_used);
        for (int i = 0; i < texstate->num_textures; i++) {
                struct pipe_sampler_view *sampler = texstate->textures[i];
                struct v3d_sampler_view *v3d_sampler = v3d_sampler_view(sampler);
                struct pipe_sampler_state *sampler_state =
                        texstate->samplers[i];

                if (!sampler)
                        continue;

                key->sampler[i].return_size =
                        v3d_get_tex_return_size(devinfo,
                                                sampler->format,
                                                sampler_state->compare_mode);

                /* For 16-bit, we set up the sampler to always return 2
                 * channels (meaning no recompiles for most statechanges),
                 * while for 32 we actually scale the returns with channels.
                 */
                if (key->sampler[i].return_size == 16) {
                        key->sampler[i].return_channels = 2;
                } else if (devinfo->ver > 40) {
                        key->sampler[i].return_channels = 4;
                } else {
                        key->sampler[i].return_channels =
                                v3d_get_tex_return_channels(devinfo,
                                                            sampler->format);
                }

                if (key->sampler[i].return_size == 32 && devinfo->ver < 40) {
                        memcpy(key->tex[i].swizzle,
                               v3d_sampler->swizzle,
                               sizeof(v3d_sampler->swizzle));
                } else {
                        /* For 16-bit returns, we let the sampler state handle
                         * the swizzle.
                         */
                        key->tex[i].swizzle[0] = PIPE_SWIZZLE_X;
                        key->tex[i].swizzle[1] = PIPE_SWIZZLE_Y;
                        key->tex[i].swizzle[2] = PIPE_SWIZZLE_Z;
                        key->tex[i].swizzle[3] = PIPE_SWIZZLE_W;
                }
        }
}

static void
v3d_setup_shared_precompile_key(struct v3d_uncompiled_shader *uncompiled,
                                struct v3d_key *key)
{
        nir_shader *s = uncompiled->base.ir.nir;

        /* Note that below we access they key's texture and sampler fields
         * using the same index. On OpenGL they are the same (they are
         * combined)
         */
        key->num_tex_used = s->info.num_textures;
        key->num_samplers_used = s->info.num_textures;
        for (int i = 0; i < s->info.num_textures; i++) {
                key->sampler[i].return_size = 16;
                key->sampler[i].return_channels = 2;

                key->tex[i].swizzle[0] = PIPE_SWIZZLE_X;
                key->tex[i].swizzle[1] = PIPE_SWIZZLE_Y;
                key->tex[i].swizzle[2] = PIPE_SWIZZLE_Z;
                key->tex[i].swizzle[3] = PIPE_SWIZZLE_W;
        }
}

static void
v3d_update_compiled_fs(struct v3d_context *v3d, uint8_t prim_mode)
{
        struct v3d_job *job = v3d->job;
        struct v3d_fs_key local_key;
        struct v3d_fs_key *key = &local_key;
        nir_shader *s = v3d->prog.bind_fs->base.ir.nir;

        if (!(v3d->dirty & (VC5_DIRTY_PRIM_MODE |
                            VC5_DIRTY_BLEND |
                            VC5_DIRTY_FRAMEBUFFER |
                            VC5_DIRTY_ZSA |
                            VC5_DIRTY_RASTERIZER |
                            VC5_DIRTY_SAMPLE_STATE |
                            VC5_DIRTY_FRAGTEX |
                            VC5_DIRTY_UNCOMPILED_FS))) {
                return;
        }

        memset(key, 0, sizeof(*key));
        v3d_setup_shared_key(v3d, &key->base, &v3d->tex[PIPE_SHADER_FRAGMENT]);
        key->base.shader_state = v3d->prog.bind_fs;
        key->base.ucp_enables = v3d->rasterizer->base.clip_plane_enable;
        key->is_points = (prim_mode == PIPE_PRIM_POINTS);
        key->is_lines = (prim_mode >= PIPE_PRIM_LINES &&
                         prim_mode <= PIPE_PRIM_LINE_STRIP);
        key->line_smoothing = (key->is_lines &&
                               v3d_line_smoothing_enabled(v3d));
        if (v3d->blend->base.logicop_enable) {
                key->logicop_func = v3d->blend->base.logicop_func;
        } else {
                key->logicop_func = PIPE_LOGICOP_COPY;
        }
        if (job->msaa) {
                key->msaa = v3d->rasterizer->base.multisample;
                key->sample_coverage = (v3d->rasterizer->base.multisample &&
                                        v3d->sample_mask != (1 << V3D_MAX_SAMPLES) - 1);
                key->sample_alpha_to_coverage = v3d->blend->base.alpha_to_coverage;
                key->sample_alpha_to_one = v3d->blend->base.alpha_to_one;
        }

        key->swap_color_rb = v3d->swap_color_rb;

        for (int i = 0; i < v3d->framebuffer.nr_cbufs; i++) {
                struct pipe_surface *cbuf = v3d->framebuffer.cbufs[i];
                if (!cbuf)
                        continue;

                /* gl_FragColor's propagation to however many bound color
                 * buffers there are means that the shader compile needs to
                 * know what buffers are present.
                 */
                key->cbufs |= 1 << i;

                /* If logic operations are enabled then we might emit color
                 * reads and we need to know the color buffer format and
                 * swizzle for that.
                 */
                if (key->logicop_func != PIPE_LOGICOP_COPY) {
                        key->color_fmt[i].format = cbuf->format;
                        key->color_fmt[i].swizzle =
                                v3d_get_format_swizzle(&v3d->screen->devinfo,
                                                       cbuf->format);
                }

                const struct util_format_description *desc =
                        util_format_description(cbuf->format);

                if (desc->channel[0].type == UTIL_FORMAT_TYPE_FLOAT &&
                    desc->channel[0].size == 32) {
                        key->f32_color_rb |= 1 << i;
                }

                if (s->info.fs.untyped_color_outputs) {
                        if (util_format_is_pure_uint(cbuf->format))
                                key->uint_color_rb |= 1 << i;
                        else if (util_format_is_pure_sint(cbuf->format))
                                key->int_color_rb |= 1 << i;
                }
        }

        if (key->is_points) {
                key->point_sprite_mask =
                        v3d->rasterizer->base.sprite_coord_enable;
                key->point_coord_upper_left =
                        (v3d->rasterizer->base.sprite_coord_mode ==
                         PIPE_SPRITE_COORD_UPPER_LEFT);
        }

        struct v3d_compiled_shader *old_fs = v3d->prog.fs;
        v3d->prog.fs = v3d_get_compiled_shader(v3d, &key->base, sizeof(*key));
        if (v3d->prog.fs == old_fs)
                return;

        v3d->dirty |= VC5_DIRTY_COMPILED_FS;

        if (old_fs) {
                if (v3d->prog.fs->prog_data.fs->flat_shade_flags !=
                    old_fs->prog_data.fs->flat_shade_flags) {
                        v3d->dirty |= VC5_DIRTY_FLAT_SHADE_FLAGS;
                }

                if (v3d->prog.fs->prog_data.fs->noperspective_flags !=
                    old_fs->prog_data.fs->noperspective_flags) {
                        v3d->dirty |= VC5_DIRTY_NOPERSPECTIVE_FLAGS;
                }

                if (v3d->prog.fs->prog_data.fs->centroid_flags !=
                    old_fs->prog_data.fs->centroid_flags) {
                        v3d->dirty |= VC5_DIRTY_CENTROID_FLAGS;
                }
        }

        if (old_fs && memcmp(v3d->prog.fs->prog_data.fs->input_slots,
                             old_fs->prog_data.fs->input_slots,
                             sizeof(v3d->prog.fs->prog_data.fs->input_slots))) {
                v3d->dirty |= VC5_DIRTY_FS_INPUTS;
        }
}

static void
v3d_update_compiled_gs(struct v3d_context *v3d, uint8_t prim_mode)
{
        struct v3d_gs_key local_key;
        struct v3d_gs_key *key = &local_key;

        if (!(v3d->dirty & (VC5_DIRTY_GEOMTEX |
                            VC5_DIRTY_RASTERIZER |
                            VC5_DIRTY_UNCOMPILED_GS |
                            VC5_DIRTY_PRIM_MODE |
                            VC5_DIRTY_FS_INPUTS))) {
                return;
        }

        if (!v3d->prog.bind_gs) {
                v3d->prog.gs = NULL;
                v3d->prog.gs_bin = NULL;
                return;
        }

        memset(key, 0, sizeof(*key));
        v3d_setup_shared_key(v3d, &key->base, &v3d->tex[PIPE_SHADER_GEOMETRY]);
        key->base.shader_state = v3d->prog.bind_gs;
        key->base.ucp_enables = v3d->rasterizer->base.clip_plane_enable;
        key->base.is_last_geometry_stage = true;
        key->num_used_outputs = v3d->prog.fs->prog_data.fs->num_inputs;
        STATIC_ASSERT(sizeof(key->used_outputs) ==
                      sizeof(v3d->prog.fs->prog_data.fs->input_slots));
        memcpy(key->used_outputs, v3d->prog.fs->prog_data.fs->input_slots,
               sizeof(key->used_outputs));

        key->per_vertex_point_size =
                (prim_mode == PIPE_PRIM_POINTS &&
                 v3d->rasterizer->base.point_size_per_vertex);

        struct v3d_compiled_shader *gs =
                v3d_get_compiled_shader(v3d, &key->base, sizeof(*key));
        if (gs != v3d->prog.gs) {
                v3d->prog.gs = gs;
                v3d->dirty |= VC5_DIRTY_COMPILED_GS;
        }

        key->is_coord = true;

        /* The last bin-mode shader in the geometry pipeline only outputs
         * varyings used by transform feedback.
         */
        struct v3d_uncompiled_shader *shader_state = key->base.shader_state;
        memcpy(key->used_outputs, shader_state->tf_outputs,
               sizeof(*key->used_outputs) * shader_state->num_tf_outputs);
        if (shader_state->num_tf_outputs < key->num_used_outputs) {
                uint32_t size = sizeof(*key->used_outputs) *
                                (key->num_used_outputs -
                                 shader_state->num_tf_outputs);
                memset(&key->used_outputs[shader_state->num_tf_outputs],
                       0, size);
        }
        key->num_used_outputs = shader_state->num_tf_outputs;

        struct v3d_compiled_shader *old_gs = v3d->prog.gs;
        struct v3d_compiled_shader *gs_bin =
                v3d_get_compiled_shader(v3d, &key->base, sizeof(*key));
        if (gs_bin != old_gs) {
                v3d->prog.gs_bin = gs_bin;
                v3d->dirty |= VC5_DIRTY_COMPILED_GS_BIN;
        }

        if (old_gs && memcmp(v3d->prog.gs->prog_data.gs->input_slots,
                             old_gs->prog_data.gs->input_slots,
                             sizeof(v3d->prog.gs->prog_data.gs->input_slots))) {
                v3d->dirty |= VC5_DIRTY_GS_INPUTS;
        }
}

static void
v3d_update_compiled_vs(struct v3d_context *v3d, uint8_t prim_mode)
{
        struct v3d_vs_key local_key;
        struct v3d_vs_key *key = &local_key;

        if (!(v3d->dirty & (VC5_DIRTY_VERTTEX |
                            VC5_DIRTY_VTXSTATE |
                            VC5_DIRTY_UNCOMPILED_VS |
                            (v3d->prog.bind_gs ? 0 : VC5_DIRTY_RASTERIZER) |
                            (v3d->prog.bind_gs ? 0 : VC5_DIRTY_PRIM_MODE) |
                            (v3d->prog.bind_gs ? VC5_DIRTY_GS_INPUTS :
                                                 VC5_DIRTY_FS_INPUTS)))) {
                return;
        }

        memset(key, 0, sizeof(*key));
        v3d_setup_shared_key(v3d, &key->base, &v3d->tex[PIPE_SHADER_VERTEX]);
        key->base.shader_state = v3d->prog.bind_vs;
        key->base.ucp_enables = v3d->rasterizer->base.clip_plane_enable;
        key->base.is_last_geometry_stage = !v3d->prog.bind_gs;

        if (!v3d->prog.bind_gs) {
            key->num_used_outputs = v3d->prog.fs->prog_data.fs->num_inputs;
            STATIC_ASSERT(sizeof(key->used_outputs) ==
                          sizeof(v3d->prog.fs->prog_data.fs->input_slots));
            memcpy(key->used_outputs, v3d->prog.fs->prog_data.fs->input_slots,
                   sizeof(key->used_outputs));
        } else {
            key->num_used_outputs = v3d->prog.gs->prog_data.gs->num_inputs;
            STATIC_ASSERT(sizeof(key->used_outputs) ==
                          sizeof(v3d->prog.gs->prog_data.gs->input_slots));
            memcpy(key->used_outputs, v3d->prog.gs->prog_data.gs->input_slots,
                   sizeof(key->used_outputs));
        }

        key->per_vertex_point_size =
                (prim_mode == PIPE_PRIM_POINTS &&
                 v3d->rasterizer->base.point_size_per_vertex);

        nir_shader *s = v3d->prog.bind_vs->base.ir.nir;
        uint64_t inputs_read = s->info.inputs_read;
        assert(util_bitcount(inputs_read) <= v3d->vtx->num_elements);

        while (inputs_read) {
                int location = u_bit_scan64(&inputs_read);
                nir_variable *var =
                        nir_find_variable_with_location(s, nir_var_shader_in, location);
                assert (var != NULL);
                int driver_location = var->data.driver_location;
                switch (v3d->vtx->pipe[driver_location].src_format) {
                case PIPE_FORMAT_B8G8R8A8_UNORM:
                case PIPE_FORMAT_B10G10R10A2_UNORM:
                case PIPE_FORMAT_B10G10R10A2_SNORM:
                case PIPE_FORMAT_B10G10R10A2_USCALED:
                case PIPE_FORMAT_B10G10R10A2_SSCALED:
                        key->va_swap_rb_mask |= 1 << location;
                        break;
                default:
                        break;
                }
        }

        struct v3d_compiled_shader *vs =
                v3d_get_compiled_shader(v3d, &key->base, sizeof(*key));
        if (vs != v3d->prog.vs) {
                v3d->prog.vs = vs;
                v3d->dirty |= VC5_DIRTY_COMPILED_VS;
        }

        key->is_coord = true;

        /* Coord shaders only output varyings used by transform feedback,
         * unless they are linked to other shaders in the geometry side
         * of the pipeline, since in that case any of the output varyings
         * could be required in later geometry stages to compute
         * gl_Position or TF outputs.
         */
        if (!v3d->prog.bind_gs) {
                struct v3d_uncompiled_shader *shader_state =
                        key->base.shader_state;
                memcpy(key->used_outputs, shader_state->tf_outputs,
                       sizeof(*key->used_outputs) *
                       shader_state->num_tf_outputs);
                if (shader_state->num_tf_outputs < key->num_used_outputs) {
                        uint32_t tail_bytes =
                                sizeof(*key->used_outputs) *
                                (key->num_used_outputs -
                                 shader_state->num_tf_outputs);
                        memset(&key->used_outputs[shader_state->num_tf_outputs],
                               0, tail_bytes);
                }
                key->num_used_outputs = shader_state->num_tf_outputs;
        }

        struct v3d_compiled_shader *cs =
                v3d_get_compiled_shader(v3d, &key->base, sizeof(*key));
        if (cs != v3d->prog.cs) {
                v3d->prog.cs = cs;
                v3d->dirty |= VC5_DIRTY_COMPILED_CS;
        }
}

void
v3d_update_compiled_shaders(struct v3d_context *v3d, uint8_t prim_mode)
{
        v3d_update_compiled_fs(v3d, prim_mode);
        v3d_update_compiled_gs(v3d, prim_mode);
        v3d_update_compiled_vs(v3d, prim_mode);
}

void
v3d_update_compiled_cs(struct v3d_context *v3d)
{
        struct v3d_key local_key;
        struct v3d_key *key = &local_key;

        if (!(v3d->dirty & (VC5_DIRTY_UNCOMPILED_CS |
                            VC5_DIRTY_COMPTEX))) {
                return;
        }

        memset(key, 0, sizeof(*key));
        v3d_setup_shared_key(v3d, key, &v3d->tex[PIPE_SHADER_COMPUTE]);
        key->shader_state = v3d->prog.bind_compute;

        struct v3d_compiled_shader *cs =
                v3d_get_compiled_shader(v3d, key, sizeof(*key));
        if (cs != v3d->prog.compute) {
                v3d->prog.compute = cs;
                v3d->dirty |= VC5_DIRTY_COMPILED_CS; /* XXX */
        }
}

static uint32_t
fs_cache_hash(const void *key)
{
        return _mesa_hash_data(key, sizeof(struct v3d_fs_key));
}

static uint32_t
gs_cache_hash(const void *key)
{
        return _mesa_hash_data(key, sizeof(struct v3d_gs_key));
}

static uint32_t
vs_cache_hash(const void *key)
{
        return _mesa_hash_data(key, sizeof(struct v3d_vs_key));
}

static uint32_t
cs_cache_hash(const void *key)
{
        return _mesa_hash_data(key, sizeof(struct v3d_key));
}

static bool
fs_cache_compare(const void *key1, const void *key2)
{
        return memcmp(key1, key2, sizeof(struct v3d_fs_key)) == 0;
}

static bool
gs_cache_compare(const void *key1, const void *key2)
{
        return memcmp(key1, key2, sizeof(struct v3d_gs_key)) == 0;
}

static bool
vs_cache_compare(const void *key1, const void *key2)
{
        return memcmp(key1, key2, sizeof(struct v3d_vs_key)) == 0;
}

static bool
cs_cache_compare(const void *key1, const void *key2)
{
        return memcmp(key1, key2, sizeof(struct v3d_key)) == 0;
}

static void
v3d_shader_state_delete(struct pipe_context *pctx, void *hwcso)
{
        struct v3d_context *v3d = v3d_context(pctx);
        struct v3d_uncompiled_shader *so = hwcso;
        nir_shader *s = so->base.ir.nir;

        hash_table_foreach(v3d->prog.cache[s->info.stage], entry) {
                const struct v3d_key *key = entry->key;
                struct v3d_compiled_shader *shader = entry->data;

                if (key->shader_state != so)
                        continue;

                if (v3d->prog.fs == shader)
                        v3d->prog.fs = NULL;
                if (v3d->prog.vs == shader)
                        v3d->prog.vs = NULL;
                if (v3d->prog.cs == shader)
                        v3d->prog.cs = NULL;
                if (v3d->prog.compute == shader)
                        v3d->prog.compute = NULL;

                _mesa_hash_table_remove(v3d->prog.cache[s->info.stage], entry);
                v3d_free_compiled_shader(shader);
        }

        ralloc_free(so->base.ir.nir);
        free(so);
}

static void
v3d_fp_state_bind(struct pipe_context *pctx, void *hwcso)
{
        struct v3d_context *v3d = v3d_context(pctx);
        v3d->prog.bind_fs = hwcso;
        v3d->dirty |= VC5_DIRTY_UNCOMPILED_FS;
}

static void
v3d_gp_state_bind(struct pipe_context *pctx, void *hwcso)
{
        struct v3d_context *v3d = v3d_context(pctx);
        v3d->prog.bind_gs = hwcso;
        v3d->dirty |= VC5_DIRTY_UNCOMPILED_GS;
}

static void
v3d_vp_state_bind(struct pipe_context *pctx, void *hwcso)
{
        struct v3d_context *v3d = v3d_context(pctx);
        v3d->prog.bind_vs = hwcso;
        v3d->dirty |= VC5_DIRTY_UNCOMPILED_VS;
}

static void
v3d_compute_state_bind(struct pipe_context *pctx, void *state)
{
        struct v3d_context *v3d = v3d_context(pctx);

        v3d->prog.bind_compute = state;
        v3d->dirty |= VC5_DIRTY_UNCOMPILED_CS;
}

static void *
v3d_create_compute_state(struct pipe_context *pctx,
                         const struct pipe_compute_state *cso)
{
        return v3d_uncompiled_shader_create(pctx, cso->ir_type,
                                            (void *)cso->prog);
}

void
v3d_program_init(struct pipe_context *pctx)
{
        struct v3d_context *v3d = v3d_context(pctx);

        pctx->create_vs_state = v3d_shader_state_create;
        pctx->delete_vs_state = v3d_shader_state_delete;

        pctx->create_gs_state = v3d_shader_state_create;
        pctx->delete_gs_state = v3d_shader_state_delete;

        pctx->create_fs_state = v3d_shader_state_create;
        pctx->delete_fs_state = v3d_shader_state_delete;

        pctx->bind_fs_state = v3d_fp_state_bind;
        pctx->bind_gs_state = v3d_gp_state_bind;
        pctx->bind_vs_state = v3d_vp_state_bind;

        if (v3d->screen->has_csd) {
                pctx->create_compute_state = v3d_create_compute_state;
                pctx->delete_compute_state = v3d_shader_state_delete;
                pctx->bind_compute_state = v3d_compute_state_bind;
        }

        v3d->prog.cache[MESA_SHADER_VERTEX] =
                _mesa_hash_table_create(pctx, vs_cache_hash, vs_cache_compare);
        v3d->prog.cache[MESA_SHADER_GEOMETRY] =
                _mesa_hash_table_create(pctx, gs_cache_hash, gs_cache_compare);
        v3d->prog.cache[MESA_SHADER_FRAGMENT] =
                _mesa_hash_table_create(pctx, fs_cache_hash, fs_cache_compare);
        v3d->prog.cache[MESA_SHADER_COMPUTE] =
                _mesa_hash_table_create(pctx, cs_cache_hash, cs_cache_compare);
}

void
v3d_program_fini(struct pipe_context *pctx)
{
        struct v3d_context *v3d = v3d_context(pctx);

        for (int i = 0; i < MESA_SHADER_STAGES; i++) {
                struct hash_table *cache = v3d->prog.cache[i];
                if (!cache)
                        continue;

                hash_table_foreach(cache, entry) {
                        struct v3d_compiled_shader *shader = entry->data;
                        v3d_free_compiled_shader(shader);
                        _mesa_hash_table_remove(cache, entry);
                }
        }

        v3d_bo_unreference(&v3d->prog.spill_bo);
}
