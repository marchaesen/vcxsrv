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
#include "genxml/gen_macros.h"

struct panfrost_device;

#ifdef PAN_ARCH
const nir_shader_compiler_options *
GENX(pan_shader_get_compiler_options)(void);

void
GENX(pan_shader_compile)(nir_shader *nir,
                         struct panfrost_compile_inputs *inputs,
                         struct util_dynarray *binary,
                         struct pan_shader_info *info);

#if PAN_ARCH <= 5
static inline void
pan_shader_prepare_midgard_rsd(const struct pan_shader_info *info,
                               struct MALI_RENDERER_STATE *rsd)
{
        assert((info->push.count & 3) == 0);

        rsd->properties.uniform_count = info->push.count / 4;
        rsd->properties.shader_has_side_effects = info->writes_global;
        rsd->properties.fp_mode = MALI_FP_MODE_GL_INF_NAN_ALLOWED;

        /* For fragment shaders, work register count, early-z, reads at draw-time */

        if (info->stage != MESA_SHADER_FRAGMENT) {
                rsd->properties.work_register_count = info->work_reg_count;
        } else {
                rsd->properties.shader_reads_tilebuffer =
                        info->fs.outputs_read;

                /* However, forcing early-z in the shader overrides draw-time */
                rsd->properties.force_early_z =
                        info->fs.early_fragment_tests;
        }
}

#else

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
        rsd->properties.pixel_kill_operation = MALI_PIXEL_KILL_## kill; \
        rsd->properties.zs_update_operation = MALI_PIXEL_KILL_## update; \
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

        rsd->properties.shader_modifies_coverage = coverage;

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

#if PAN_ARCH >= 7
static enum mali_shader_register_allocation
pan_register_allocation(unsigned work_reg_count)
{
        return (work_reg_count <= 32) ?
                MALI_SHADER_REGISTER_ALLOCATION_32_PER_THREAD :
                MALI_SHADER_REGISTER_ALLOCATION_64_PER_THREAD;
}
#endif

#define pan_preloads(reg) (preload & BITFIELD64_BIT(reg))

static void
pan_make_preload(gl_shader_stage stage,
                 uint64_t preload,
                 struct MALI_PRELOAD *out)
{
        switch (stage) {
        case MESA_SHADER_VERTEX:
                out->vertex.position_result_address_lo = pan_preloads(58);
                out->vertex.position_result_address_hi = pan_preloads(59);
                out->vertex.vertex_id = pan_preloads(61);
                out->vertex.instance_id = pan_preloads(62);
                break;

        case MESA_SHADER_FRAGMENT:
                out->fragment.primitive_id = pan_preloads(57);
                out->fragment.primitive_flags = pan_preloads(58);
                out->fragment.fragment_position = pan_preloads(59);
                out->fragment.sample_mask_id = pan_preloads(61);
                out->fragment.coverage = true;
                break;

        default:
                out->compute.local_invocation_xy = pan_preloads(55);
                out->compute.local_invocation_z = pan_preloads(56);
                out->compute.work_group_x = pan_preloads(57);
                out->compute.work_group_y = pan_preloads(58);
                out->compute.work_group_z = pan_preloads(59);
                out->compute.global_invocation_x = pan_preloads(60);
                out->compute.global_invocation_y = pan_preloads(61);
                out->compute.global_invocation_z = pan_preloads(62);
                break;
        }
}

static inline void
pan_shader_prepare_bifrost_rsd(const struct pan_shader_info *info,
                               struct MALI_RENDERER_STATE *rsd)
{
        unsigned fau_count = DIV_ROUND_UP(info->push.count, 2);
        rsd->preload.uniform_count = fau_count;

#if PAN_ARCH >= 7
        rsd->properties.shader_register_allocation =
                pan_register_allocation(info->work_reg_count);
#endif

        pan_make_preload(info->stage, info->preload, &rsd->preload);

        if (info->stage == MESA_SHADER_FRAGMENT) {
                pan_shader_classify_pixel_kill_coverage(info, rsd);

                /* Match the mesa/st convention. If this needs to be flipped,
                 * nir_lower_pntc_ytransform will do so. */
                rsd->properties.point_sprite_coord_origin_max_y = true;

                rsd->properties.allow_forward_pixel_to_be_killed =
                        !info->fs.sidefx;

#if PAN_ARCH >= 7
                rsd->properties.shader_wait_dependency_6 = info->bifrost.wait_6;
                rsd->properties.shader_wait_dependency_7 = info->bifrost.wait_7;

                rsd->message_preload_1 = info->bifrost.messages[0];
                rsd->message_preload_2 = info->bifrost.messages[1];
#endif
        } else if (info->stage == MESA_SHADER_VERTEX && info->vs.secondary_enable) {
                rsd->secondary_preload.uniform_count = fau_count;

                pan_make_preload(info->stage, info->vs.secondary_preload,
                                 &rsd->secondary_preload);

                rsd->secondary_shader = rsd->shader.shader +
                                        info->vs.secondary_offset;

#if PAN_ARCH >= 7
                rsd->properties.secondary_shader_register_allocation =
                        pan_register_allocation(info->vs.secondary_work_reg_count);
#endif
        }
}

#endif

static inline void
pan_shader_prepare_rsd(const struct pan_shader_info *shader_info,
                       mali_ptr shader_ptr,
                       struct MALI_RENDERER_STATE *rsd)
{
#if PAN_ARCH <= 5
        shader_ptr |= shader_info->midgard.first_tag;
#endif

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

#if PAN_ARCH >= 6
        pan_shader_prepare_bifrost_rsd(shader_info, rsd);
#else
        pan_shader_prepare_midgard_rsd(shader_info, rsd);
#endif
}
#endif /* PAN_ARCH */

#endif
