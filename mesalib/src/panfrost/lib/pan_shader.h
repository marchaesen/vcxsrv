/*
 * Copyright (C) 2021 Collabora, Ltd.
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

#ifndef __PAN_SHADER_H__
#define __PAN_SHADER_H__

#include "compiler/nir/nir.h"
#include "panfrost/util/pan_ir.h"

#include "pan_device.h"
#include "gen_macros.h"

struct panfrost_device;

const nir_shader_compiler_options *
pan_shader_get_compiler_options(const struct panfrost_device *dev);

void
pan_shader_compile(const struct panfrost_device *dev,
                   nir_shader *nir,
                   const struct panfrost_compile_inputs *inputs,
                   struct util_dynarray *binary,
                   struct pan_shader_info *info);

static inline void
pan_shader_prepare_midgard_rsd(const struct pan_shader_info *info,
                               struct MALI_RENDERER_STATE *rsd)
{
        assert((info->push.count & 3) == 0);

        rsd->properties.midgard.uniform_count = info->push.count / 4;
        rsd->properties.midgard.shader_has_side_effects = info->writes_global;
        rsd->properties.midgard.fp_mode = MALI_FP_MODE_GL_INF_NAN_ALLOWED;

        /* For fragment shaders, work register count, early-z, reads at draw-time */

        if (info->stage != MESA_SHADER_FRAGMENT) {
                rsd->properties.midgard.work_register_count = info->work_reg_count;
        } else {
                rsd->properties.midgard.shader_reads_tilebuffer =
                        info->fs.outputs_read;

                /* However, forcing early-z in the shader overrides draw-time */
                rsd->properties.midgard.force_early_z =
                        info->fs.early_fragment_tests;
        }
}

/* Classify a shader into the following pixel kill categories:
 *
 * (force early, strong early): no side effects/depth/stencil/coverage writes (force)
 * (weak early, weak early): no side effects/depth/stencil/coverage writes
 * (weak early, force late): no side effects/depth/stencil writes
 * (force late, weak early): side effects but no depth/stencil/coverage writes
 * (force late, force early): only run for side effects
 * (force late, force late): depth/stencil writes
 *
 * Note that discard is considered a coverage write. TODO: what about
 * alpha-to-coverage?
 * */

#define SET_PIXEL_KILL(kill, update) do { \
        rsd->properties.bifrost.pixel_kill_operation = MALI_PIXEL_KILL_## kill; \
        rsd->properties.bifrost.zs_update_operation = MALI_PIXEL_KILL_## update; \
} while(0)

static inline void
pan_shader_classify_pixel_kill_coverage(const struct pan_shader_info *info,
                struct MALI_RENDERER_STATE *rsd)
{
        bool force_early = info->fs.early_fragment_tests;
        bool sidefx = info->writes_global;
        bool coverage = info->fs.writes_coverage || info->fs.can_discard;
        bool depth = info->fs.writes_depth;
        bool stencil = info->fs.writes_stencil;

        rsd->properties.bifrost.shader_modifies_coverage = coverage;

        if (force_early)
                SET_PIXEL_KILL(FORCE_EARLY, STRONG_EARLY);
        else if (depth || stencil || (sidefx && coverage))
                SET_PIXEL_KILL(FORCE_LATE, FORCE_LATE);
        else if (sidefx)
                SET_PIXEL_KILL(FORCE_LATE, WEAK_EARLY);
        else if (coverage)
                SET_PIXEL_KILL(WEAK_EARLY, FORCE_LATE);
        else
                SET_PIXEL_KILL(WEAK_EARLY, WEAK_EARLY);
}

#undef SET_PIXEL_KILL

static inline void
pan_shader_prepare_bifrost_rsd(const struct panfrost_device *dev,
                               const struct pan_shader_info *info,
                               struct MALI_RENDERER_STATE *rsd)
{
        unsigned fau_count = DIV_ROUND_UP(info->push.count, 2);
        rsd->preload.uniform_count = fau_count;

        if (dev->arch == 7) {
                rsd->properties.bifrost.shader_register_allocation =
                        (info->work_reg_count <= 32) ?
                        MALI_SHADER_REGISTER_ALLOCATION_32_PER_THREAD :
                        MALI_SHADER_REGISTER_ALLOCATION_64_PER_THREAD;
        }

        switch (info->stage) {
        case MESA_SHADER_VERTEX:
                rsd->preload.vertex.vertex_id = true;
                rsd->preload.vertex.instance_id = true;
                break;

        case MESA_SHADER_FRAGMENT:
                pan_shader_classify_pixel_kill_coverage(info, rsd);

                if (dev->arch > 6) {
                        rsd->properties.bifrost.shader_wait_dependency_6 = info->bifrost.wait_6;
                        rsd->properties.bifrost.shader_wait_dependency_7 = info->bifrost.wait_7;
                }

                rsd->properties.bifrost.allow_forward_pixel_to_be_killed =
                        !info->fs.sidefx;

                rsd->preload.fragment.fragment_position = info->fs.reads_frag_coord;
                rsd->preload.fragment.coverage = true;
                rsd->preload.fragment.primitive_flags = info->fs.reads_face;

                /* Contains sample ID and sample mask. Sample position and
                 * helper invocation are expressed in terms of the above, so
                 * preload for those too */
                rsd->preload.fragment.sample_mask_id =
                        info->fs.reads_sample_id |
                        info->fs.reads_sample_pos |
                        info->fs.reads_sample_mask_in |
                        info->fs.reads_helper_invocation |
                        info->fs.sample_shading;

                rsd->message_preload_1 = info->bifrost.messages[0];
                rsd->message_preload_2 = info->bifrost.messages[1];
                break;

        case MESA_SHADER_COMPUTE:
                rsd->preload.compute.local_invocation_xy = true;
                rsd->preload.compute.local_invocation_z = true;
                rsd->preload.compute.work_group_x = true;
                rsd->preload.compute.work_group_y = true;
                rsd->preload.compute.work_group_z = true;
                rsd->preload.compute.global_invocation_x = true;
                rsd->preload.compute.global_invocation_y = true;
                rsd->preload.compute.global_invocation_z = true;
                break;

        default:
                unreachable("TODO");
        }
}

static inline void
pan_shader_prepare_rsd(const struct panfrost_device *dev,
                       const struct pan_shader_info *shader_info,
                       mali_ptr shader_ptr,
                       struct MALI_RENDERER_STATE *rsd)
{
        if (!pan_is_bifrost(dev))
                shader_ptr |= shader_info->midgard.first_tag;

        rsd->shader.shader = shader_ptr;
        rsd->shader.attribute_count = shader_info->attribute_count;
        rsd->shader.varying_count = shader_info->varyings.input_count +
                                   shader_info->varyings.output_count;
        rsd->shader.texture_count = shader_info->texture_count;
        rsd->shader.sampler_count = shader_info->sampler_count;
        rsd->properties.shader_contains_barrier = shader_info->contains_barrier;
        rsd->properties.uniform_buffer_count = shader_info->ubo_count;

        if (shader_info->stage == MESA_SHADER_FRAGMENT) {
                rsd->properties.shader_contains_barrier |=
                        shader_info->fs.helper_invocations;
                rsd->properties.stencil_from_shader =
                        shader_info->fs.writes_stencil;
                rsd->properties.depth_source =
                        shader_info->fs.writes_depth ?
                        MALI_DEPTH_SOURCE_SHADER :
                        MALI_DEPTH_SOURCE_FIXED_FUNCTION;

                /* This also needs to be set if the API forces per-sample
                 * shading, but that'll just got ORed in */
                rsd->multisample_misc.evaluate_per_sample =
                        shader_info->fs.sample_shading;
        }

        if (pan_is_bifrost(dev))
                pan_shader_prepare_bifrost_rsd(dev, shader_info, rsd);
        else
                pan_shader_prepare_midgard_rsd(shader_info, rsd);
}

#endif
