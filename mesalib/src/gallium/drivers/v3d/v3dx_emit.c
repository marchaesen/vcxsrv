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

#include "util/format/u_format.h"
#include "util/half_float.h"
#include "v3d_context.h"
#include "broadcom/common/v3d_macros.h"
#include "broadcom/cle/v3dx_pack.h"
#include "broadcom/compiler/v3d_compiler.h"

static uint8_t
v3d_factor(enum pipe_blendfactor factor, bool dst_alpha_one)
{
        /* We may get a bad blendfactor when blending is disabled. */
        if (factor == 0)
                return V3D_BLEND_FACTOR_ZERO;

        switch (factor) {
        case PIPE_BLENDFACTOR_ZERO:
                return V3D_BLEND_FACTOR_ZERO;
        case PIPE_BLENDFACTOR_ONE:
                return V3D_BLEND_FACTOR_ONE;
        case PIPE_BLENDFACTOR_SRC_COLOR:
                return V3D_BLEND_FACTOR_SRC_COLOR;
        case PIPE_BLENDFACTOR_INV_SRC_COLOR:
                return V3D_BLEND_FACTOR_INV_SRC_COLOR;
        case PIPE_BLENDFACTOR_DST_COLOR:
                return V3D_BLEND_FACTOR_DST_COLOR;
        case PIPE_BLENDFACTOR_INV_DST_COLOR:
                return V3D_BLEND_FACTOR_INV_DST_COLOR;
        case PIPE_BLENDFACTOR_SRC_ALPHA:
                return V3D_BLEND_FACTOR_SRC_ALPHA;
        case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
                return V3D_BLEND_FACTOR_INV_SRC_ALPHA;
        case PIPE_BLENDFACTOR_DST_ALPHA:
                return (dst_alpha_one ?
                        V3D_BLEND_FACTOR_ONE :
                        V3D_BLEND_FACTOR_DST_ALPHA);
        case PIPE_BLENDFACTOR_INV_DST_ALPHA:
                return (dst_alpha_one ?
                        V3D_BLEND_FACTOR_ZERO :
                        V3D_BLEND_FACTOR_INV_DST_ALPHA);
        case PIPE_BLENDFACTOR_CONST_COLOR:
                return V3D_BLEND_FACTOR_CONST_COLOR;
        case PIPE_BLENDFACTOR_INV_CONST_COLOR:
                return V3D_BLEND_FACTOR_INV_CONST_COLOR;
        case PIPE_BLENDFACTOR_CONST_ALPHA:
                return V3D_BLEND_FACTOR_CONST_ALPHA;
        case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
                return V3D_BLEND_FACTOR_INV_CONST_ALPHA;
        case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
                return (dst_alpha_one ?
                        V3D_BLEND_FACTOR_ZERO :
                        V3D_BLEND_FACTOR_SRC_ALPHA_SATURATE);
        default:
                unreachable("Bad blend factor");
        }
}

#if V3D_VERSION < 40
static inline uint16_t
swizzled_border_color(const struct v3d_device_info *devinfo,
                      struct pipe_sampler_state *sampler,
                      struct v3d_sampler_view *sview,
                      int chan)
{
        const struct util_format_description *desc =
                util_format_description(sview->base.format);
        uint8_t swiz = chan;

        /* If we're doing swizzling in the sampler, then only rearrange the
         * border color for the mismatch between the V3D texture format and
         * the PIPE_FORMAT, since GL_ARB_texture_swizzle will be handled by
         * the sampler's swizzle.
         *
         * For swizzling in the shader, we don't do any pre-swizzling of the
         * border color.
         */
        if (v3d_get_tex_return_size(devinfo, sview->base.format,
                                    sampler->compare_mode) != 32)
                swiz = desc->swizzle[swiz];

        switch (swiz) {
        case PIPE_SWIZZLE_0:
                return _mesa_float_to_half(0.0);
        case PIPE_SWIZZLE_1:
                return _mesa_float_to_half(1.0);
        default:
                return _mesa_float_to_half(sampler->border_color.f[swiz]);
        }
}

static uint32_t
translate_swizzle(unsigned char pipe_swizzle)
{
        switch (pipe_swizzle) {
        case PIPE_SWIZZLE_0:
                return 0;
        case PIPE_SWIZZLE_1:
                return 1;
        case PIPE_SWIZZLE_X:
        case PIPE_SWIZZLE_Y:
        case PIPE_SWIZZLE_Z:
        case PIPE_SWIZZLE_W:
                return 2 + pipe_swizzle;
        default:
                unreachable("unknown swizzle");
        }
}

static void
emit_one_texture(struct v3d_context *v3d, struct v3d_texture_stateobj *stage_tex,
                 int i)
{
        struct v3d_job *job = v3d->job;
        struct pipe_sampler_state *psampler = stage_tex->samplers[i];
        struct v3d_sampler_state *sampler = v3d_sampler_state(psampler);
        struct pipe_sampler_view *psview = stage_tex->textures[i];
        struct v3d_sampler_view *sview = v3d_sampler_view(psview);
        struct pipe_resource *prsc = psview->texture;
        struct v3d_resource *rsc = v3d_resource(prsc);
        const struct v3d_device_info *devinfo = &v3d->screen->devinfo;

        stage_tex->texture_state[i].offset =
                v3d_cl_ensure_space(&job->indirect,
                                    cl_packet_length(TEXTURE_SHADER_STATE),
                                    32);
        v3d_bo_set_reference(&stage_tex->texture_state[i].bo,
                             job->indirect.bo);

        uint32_t return_size = v3d_get_tex_return_size(devinfo, psview->format,
                                                       psampler->compare_mode);

        struct V3D33_TEXTURE_SHADER_STATE unpacked = {
                /* XXX */
                .border_color_red = swizzled_border_color(devinfo, psampler,
                                                          sview, 0),
                .border_color_green = swizzled_border_color(devinfo, psampler,
                                                            sview, 1),
                .border_color_blue = swizzled_border_color(devinfo, psampler,
                                                           sview, 2),
                .border_color_alpha = swizzled_border_color(devinfo, psampler,
                                                            sview, 3),

                /* In the normal texturing path, the LOD gets clamped between
                 * min/max, and the base_level field (set in the sampler view
                 * from first_level) only decides where the min/mag switch
                 * happens, so we need to use the LOD clamps to keep us
                 * between min and max.
                 *
                 * For txf, the LOD clamp is still used, despite GL not
                 * wanting that.  We will need to have a separate
                 * TEXTURE_SHADER_STATE that ignores psview->min/max_lod to
                 * support txf properly.
                 */
                .min_level_of_detail = MIN2(psview->u.tex.first_level +
                                            MAX2(psampler->min_lod, 0),
                                            psview->u.tex.last_level),
                .max_level_of_detail = MIN2(psview->u.tex.first_level +
                                            MAX2(psampler->max_lod,
                                                 psampler->min_lod),
                                            psview->u.tex.last_level),

                .texture_base_pointer = cl_address(rsc->bo,
                                                   rsc->slices[0].offset),

                .output_32_bit = return_size == 32,
        };

        /* Set up the sampler swizzle if we're doing 16-bit sampling.  For
         * 32-bit, we leave swizzling up to the shader compiler.
         *
         * Note: Contrary to the docs, the swizzle still applies even if the
         * return size is 32.  It's just that you probably want to swizzle in
         * the shader, because you need the Y/Z/W channels to be defined.
         */
        if (return_size == 32) {
                unpacked.swizzle_r = translate_swizzle(PIPE_SWIZZLE_X);
                unpacked.swizzle_g = translate_swizzle(PIPE_SWIZZLE_Y);
                unpacked.swizzle_b = translate_swizzle(PIPE_SWIZZLE_Z);
                unpacked.swizzle_a = translate_swizzle(PIPE_SWIZZLE_W);
        } else {
                unpacked.swizzle_r = translate_swizzle(sview->swizzle[0]);
                unpacked.swizzle_g = translate_swizzle(sview->swizzle[1]);
                unpacked.swizzle_b = translate_swizzle(sview->swizzle[2]);
                unpacked.swizzle_a = translate_swizzle(sview->swizzle[3]);
        }

        int min_img_filter = psampler->min_img_filter;
        int min_mip_filter = psampler->min_mip_filter;
        int mag_img_filter = psampler->mag_img_filter;

        if (return_size == 32) {
                min_mip_filter = PIPE_TEX_MIPFILTER_NEAREST;
                min_img_filter = PIPE_TEX_FILTER_NEAREST;
                mag_img_filter = PIPE_TEX_FILTER_NEAREST;
        }

        bool min_nearest = min_img_filter == PIPE_TEX_FILTER_NEAREST;
        switch (min_mip_filter) {
        case PIPE_TEX_MIPFILTER_NONE:
                unpacked.filter += min_nearest ? 2 : 0;
                break;
        case PIPE_TEX_MIPFILTER_NEAREST:
                unpacked.filter += min_nearest ? 4 : 8;
                break;
        case PIPE_TEX_MIPFILTER_LINEAR:
                unpacked.filter += min_nearest ? 4 : 8;
                unpacked.filter += 2;
                break;
        }

        if (mag_img_filter == PIPE_TEX_FILTER_NEAREST)
                unpacked.filter++;

        if (psampler->max_anisotropy > 8)
                unpacked.filter = V3D_TMU_FILTER_ANISOTROPIC_16_1;
        else if (psampler->max_anisotropy > 4)
                unpacked.filter = V3D_TMU_FILTER_ANISOTROPIC_8_1;
        else if (psampler->max_anisotropy > 2)
                unpacked.filter = V3D_TMU_FILTER_ANISOTROPIC_4_1;
        else if (psampler->max_anisotropy)
                unpacked.filter = V3D_TMU_FILTER_ANISOTROPIC_2_1;

        uint8_t packed[cl_packet_length(TEXTURE_SHADER_STATE)];
        cl_packet_pack(TEXTURE_SHADER_STATE)(&job->indirect, packed, &unpacked);

        for (int i = 0; i < ARRAY_SIZE(packed); i++)
                packed[i] |= sview->texture_shader_state[i] | sampler->texture_shader_state[i];

        /* TMU indirect structs need to be 32b aligned. */
        v3d_cl_ensure_space(&job->indirect, ARRAY_SIZE(packed), 32);
        cl_emit_prepacked(&job->indirect, &packed);
}

static void
emit_textures(struct v3d_context *v3d, struct v3d_texture_stateobj *stage_tex)
{
        for (int i = 0; i < stage_tex->num_textures; i++) {
                if (stage_tex->textures[i])
                        emit_one_texture(v3d, stage_tex, i);
        }
}
#endif /* V3D_VERSION < 40 */

static uint32_t
translate_colormask(struct v3d_context *v3d, uint32_t colormask, int rt)
{
        if (v3d->swap_color_rb & (1 << rt)) {
                colormask = ((colormask & (2 | 8)) |
                             ((colormask & 1) << 2) |
                             ((colormask & 4) >> 2));
        }

        return (~colormask) & 0xf;
}

static void
emit_rt_blend(struct v3d_context *v3d, struct v3d_job *job,
              struct pipe_blend_state *blend, int rt)
{
        struct pipe_rt_blend_state *rtblend = &blend->rt[rt];

#if V3D_VERSION >= 40
        /* We don't need to emit blend state for disabled RTs. */
        if (!rtblend->blend_enable)
                return;
#endif

        cl_emit(&job->bcl, BLEND_CFG, config) {
#if V3D_VERSION >= 40
                if (blend->independent_blend_enable)
                        config.render_target_mask = 1 << rt;
                else
                        config.render_target_mask = (1 << V3D_MAX_DRAW_BUFFERS) - 1;
#else
                assert(rt == 0);
#endif

                config.color_blend_mode = rtblend->rgb_func;
                config.color_blend_dst_factor =
                        v3d_factor(rtblend->rgb_dst_factor,
                                   v3d->blend_dst_alpha_one);
                config.color_blend_src_factor =
                        v3d_factor(rtblend->rgb_src_factor,
                                   v3d->blend_dst_alpha_one);

                config.alpha_blend_mode = rtblend->alpha_func;
                config.alpha_blend_dst_factor =
                        v3d_factor(rtblend->alpha_dst_factor,
                                   v3d->blend_dst_alpha_one);
                config.alpha_blend_src_factor =
                        v3d_factor(rtblend->alpha_src_factor,
                                   v3d->blend_dst_alpha_one);
        }
}

static void
emit_flat_shade_flags(struct v3d_job *job,
                      int varying_offset,
                      uint32_t varyings,
                      enum V3DX(Varying_Flags_Action) lower,
                      enum V3DX(Varying_Flags_Action) higher)
{
        cl_emit(&job->bcl, FLAT_SHADE_FLAGS, flags) {
                flags.varying_offset_v0 = varying_offset;
                flags.flat_shade_flags_for_varyings_v024 = varyings;
                flags.action_for_flat_shade_flags_of_lower_numbered_varyings =
                        lower;
                flags.action_for_flat_shade_flags_of_higher_numbered_varyings =
                        higher;
        }
}

#if V3D_VERSION >= 40
static void
emit_noperspective_flags(struct v3d_job *job,
                         int varying_offset,
                         uint32_t varyings,
                         enum V3DX(Varying_Flags_Action) lower,
                         enum V3DX(Varying_Flags_Action) higher)
{
        cl_emit(&job->bcl, NON_PERSPECTIVE_FLAGS, flags) {
                flags.varying_offset_v0 = varying_offset;
                flags.non_perspective_flags_for_varyings_v024 = varyings;
                flags.action_for_non_perspective_flags_of_lower_numbered_varyings =
                        lower;
                flags.action_for_non_perspective_flags_of_higher_numbered_varyings =
                        higher;
        }
}

static void
emit_centroid_flags(struct v3d_job *job,
                    int varying_offset,
                    uint32_t varyings,
                    enum V3DX(Varying_Flags_Action) lower,
                    enum V3DX(Varying_Flags_Action) higher)
{
        cl_emit(&job->bcl, CENTROID_FLAGS, flags) {
                flags.varying_offset_v0 = varying_offset;
                flags.centroid_flags_for_varyings_v024 = varyings;
                flags.action_for_centroid_flags_of_lower_numbered_varyings =
                        lower;
                flags.action_for_centroid_flags_of_higher_numbered_varyings =
                        higher;
        }
}
#endif /* V3D_VERSION >= 40 */

static bool
emit_varying_flags(struct v3d_job *job, uint32_t *flags,
                   void (*flag_emit_callback)(struct v3d_job *job,
                                              int varying_offset,
                                              uint32_t flags,
                                              enum V3DX(Varying_Flags_Action) lower,
                                              enum V3DX(Varying_Flags_Action) higher))
{
        struct v3d_context *v3d = job->v3d;
        bool emitted_any = false;

        for (int i = 0; i < ARRAY_SIZE(v3d->prog.fs->prog_data.fs->flat_shade_flags); i++) {
                if (!flags[i])
                        continue;

                if (emitted_any) {
                        flag_emit_callback(job, i, flags[i],
                                           V3D_VARYING_FLAGS_ACTION_UNCHANGED,
                                           V3D_VARYING_FLAGS_ACTION_UNCHANGED);
                } else if (i == 0) {
                        flag_emit_callback(job, i, flags[i],
                                           V3D_VARYING_FLAGS_ACTION_UNCHANGED,
                                           V3D_VARYING_FLAGS_ACTION_ZEROED);
                } else {
                        flag_emit_callback(job, i, flags[i],
                                           V3D_VARYING_FLAGS_ACTION_ZEROED,
                                           V3D_VARYING_FLAGS_ACTION_ZEROED);
                }
                emitted_any = true;
        }

        return emitted_any;
}

static inline struct v3d_uncompiled_shader *
get_tf_shader(struct v3d_context *v3d)
{
        if (v3d->prog.bind_gs)
                return v3d->prog.bind_gs;
        else
                return v3d->prog.bind_vs;
}

void
v3dX(emit_state)(struct pipe_context *pctx)
{
        struct v3d_context *v3d = v3d_context(pctx);
        struct v3d_job *job = v3d->job;
        bool rasterizer_discard = v3d->rasterizer->base.rasterizer_discard;

        if (v3d->dirty & (V3D_DIRTY_SCISSOR | V3D_DIRTY_VIEWPORT |
                          V3D_DIRTY_RASTERIZER)) {
                float *vpscale = v3d->viewport.scale;
                float *vptranslate = v3d->viewport.translate;
                float vp_minx = -fabsf(vpscale[0]) + vptranslate[0];
                float vp_maxx = fabsf(vpscale[0]) + vptranslate[0];
                float vp_miny = -fabsf(vpscale[1]) + vptranslate[1];
                float vp_maxy = fabsf(vpscale[1]) + vptranslate[1];

                /* Clip to the scissor if it's enabled, but still clip to the
                 * drawable regardless since that controls where the binner
                 * tries to put things.
                 *
                 * Additionally, always clip the rendering to the viewport,
                 * since the hardware does guardband clipping, meaning
                 * primitives would rasterize outside of the view volume.
                 */
                uint32_t minx, miny, maxx, maxy;
                if (!v3d->rasterizer->base.scissor) {
                        minx = MAX2(vp_minx, 0);
                        miny = MAX2(vp_miny, 0);
                        maxx = MIN2(vp_maxx, job->draw_width);
                        maxy = MIN2(vp_maxy, job->draw_height);
                } else {
                        minx = MAX2(vp_minx, v3d->scissor.minx);
                        miny = MAX2(vp_miny, v3d->scissor.miny);
                        maxx = MIN2(vp_maxx, v3d->scissor.maxx);
                        maxy = MIN2(vp_maxy, v3d->scissor.maxy);
                }

                cl_emit(&job->bcl, CLIP_WINDOW, clip) {
                        clip.clip_window_left_pixel_coordinate = minx;
                        clip.clip_window_bottom_pixel_coordinate = miny;
                        if (maxx > minx && maxy > miny) {
                                clip.clip_window_width_in_pixels = maxx - minx;
                                clip.clip_window_height_in_pixels = maxy - miny;
                        } else if (V3D_VERSION < 41) {
                                /* The HW won't entirely clip out when scissor
                                 * w/h is 0.  Just treat it the same as
                                 * rasterizer discard.
                                 */
                                rasterizer_discard = true;
                                clip.clip_window_width_in_pixels = 1;
                                clip.clip_window_height_in_pixels = 1;
                        }
                }

                job->draw_min_x = MIN2(job->draw_min_x, minx);
                job->draw_min_y = MIN2(job->draw_min_y, miny);
                job->draw_max_x = MAX2(job->draw_max_x, maxx);
                job->draw_max_y = MAX2(job->draw_max_y, maxy);

                if (!v3d->rasterizer->base.scissor) {
                    job->scissor.disabled = true;
                } else if (!job->scissor.disabled &&
                           (v3d->dirty & V3D_DIRTY_SCISSOR)) {
                        if (job->scissor.count < MAX_JOB_SCISSORS) {
                                job->scissor.rects[job->scissor.count].min_x =
                                        v3d->scissor.minx;
                                job->scissor.rects[job->scissor.count].min_y =
                                        v3d->scissor.miny;
                                job->scissor.rects[job->scissor.count].max_x =
                                        v3d->scissor.maxx - 1;
                                job->scissor.rects[job->scissor.count].max_y =
                                        v3d->scissor.maxy - 1;
                                job->scissor.count++;
                        } else {
                                job->scissor.disabled = true;
                                perf_debug("Too many scissor rects.");
                        }
                }
        }

        if (v3d->dirty & (V3D_DIRTY_RASTERIZER |
                          V3D_DIRTY_ZSA |
                          V3D_DIRTY_BLEND |
                          V3D_DIRTY_COMPILED_FS)) {
                cl_emit(&job->bcl, CFG_BITS, config) {
                        config.enable_forward_facing_primitive =
                                !rasterizer_discard &&
                                !(v3d->rasterizer->base.cull_face &
                                  PIPE_FACE_FRONT);
                        config.enable_reverse_facing_primitive =
                                !rasterizer_discard &&
                                !(v3d->rasterizer->base.cull_face &
                                  PIPE_FACE_BACK);
                        /* This seems backwards, but it's what gets the
                         * clipflat test to pass.
                         */
                        config.clockwise_primitives =
                                v3d->rasterizer->base.front_ccw;

                        config.enable_depth_offset =
                                v3d->rasterizer->base.offset_tri;

                        /* V3D follows GL behavior where the sample mask only
                         * applies when MSAA is enabled.  Gallium has sample
                         * mask apply anyway, and the MSAA blit shaders will
                         * set sample mask without explicitly setting
                         * rasterizer oversample.  Just force it on here,
                         * since the blit shaders are the only way to have
                         * !multisample && samplemask != 0xf.
                         */
                        config.rasterizer_oversample_mode =
                                v3d->rasterizer->base.multisample ||
                                v3d->sample_mask != 0xf;

                        config.direct3d_provoking_vertex =
                                v3d->rasterizer->base.flatshade_first;

                        config.blend_enable = v3d->blend->blend_enables;

                        /* Note: EZ state may update based on the compiled FS,
                         * along with ZSA
                         */
                        config.early_z_updates_enable =
                                (job->ez_state != V3D_EZ_DISABLED);
                        if (v3d->zsa->base.depth_enabled) {
                                config.z_updates_enable =
                                        v3d->zsa->base.depth_writemask;
                                config.early_z_enable =
                                        config.early_z_updates_enable;
                                config.depth_test_function =
                                        v3d->zsa->base.depth_func;
                        } else {
                                config.depth_test_function = PIPE_FUNC_ALWAYS;
                        }

                        config.stencil_enable =
                                v3d->zsa->base.stencil[0].enabled;

                        /* Use nicer line caps when line smoothing is
                         * enabled
                         */
                        config.line_rasterization =
                                v3d_line_smoothing_enabled(v3d) ? 1 : 0;
                }

        }

        if (v3d->dirty & V3D_DIRTY_RASTERIZER &&
            v3d->rasterizer->base.offset_tri) {
                if (job->zsbuf &&
                    job->zsbuf->format == PIPE_FORMAT_Z16_UNORM) {
                        cl_emit_prepacked_sized(&job->bcl,
                                                v3d->rasterizer->depth_offset_z16,
                                                cl_packet_length(DEPTH_OFFSET));
                } else {
                        cl_emit_prepacked_sized(&job->bcl,
                                                v3d->rasterizer->depth_offset,
                                                cl_packet_length(DEPTH_OFFSET));
                }
        }

        if (v3d->dirty & V3D_DIRTY_RASTERIZER) {
                cl_emit(&job->bcl, POINT_SIZE, point_size) {
                        point_size.point_size = v3d->rasterizer->point_size;
                }

                cl_emit(&job->bcl, LINE_WIDTH, line_width) {
                        line_width.line_width = v3d_get_real_line_width(v3d);
                }
        }

        if (v3d->dirty & V3D_DIRTY_VIEWPORT) {
                cl_emit(&job->bcl, CLIPPER_XY_SCALING, clip) {
                        clip.viewport_half_width_in_1_256th_of_pixel =
                                v3d->viewport.scale[0] * 256.0f;
                        clip.viewport_half_height_in_1_256th_of_pixel =
                                v3d->viewport.scale[1] * 256.0f;
                }

                cl_emit(&job->bcl, CLIPPER_Z_SCALE_AND_OFFSET, clip) {
                        clip.viewport_z_offset_zc_to_zs =
                                v3d->viewport.translate[2];
                        clip.viewport_z_scale_zc_to_zs =
                                v3d->viewport.scale[2];
                }
                cl_emit(&job->bcl, CLIPPER_Z_MIN_MAX_CLIPPING_PLANES, clip) {
                        float z1 = (v3d->viewport.translate[2] -
                                    v3d->viewport.scale[2]);
                        float z2 = (v3d->viewport.translate[2] +
                                    v3d->viewport.scale[2]);
                        clip.minimum_zw = MIN2(z1, z2);
                        clip.maximum_zw = MAX2(z1, z2);
                }

                cl_emit(&job->bcl, VIEWPORT_OFFSET, vp) {
                        vp.viewport_centre_x_coordinate =
                                v3d->viewport.translate[0];
                        vp.viewport_centre_y_coordinate =
                                v3d->viewport.translate[1];
                }
        }

        if (v3d->dirty & V3D_DIRTY_BLEND) {
                struct v3d_blend_state *blend = v3d->blend;

                if (blend->blend_enables) {
#if V3D_VERSION >= 40
                        cl_emit(&job->bcl, BLEND_ENABLES, enables) {
                                enables.mask = blend->blend_enables;
                        }
#endif

                        if (blend->base.independent_blend_enable) {
                                for (int i = 0; i < V3D_MAX_DRAW_BUFFERS; i++)
                                        emit_rt_blend(v3d, job, &blend->base, i);
                        } else {
                                emit_rt_blend(v3d, job, &blend->base, 0);
                        }
                }
        }

        if (v3d->dirty & V3D_DIRTY_BLEND) {
                struct pipe_blend_state *blend = &v3d->blend->base;

                cl_emit(&job->bcl, COLOR_WRITE_MASKS, mask) {
                        for (int i = 0; i < 4; i++) {
                                int rt = blend->independent_blend_enable ? i : 0;
                                int rt_mask = blend->rt[rt].colormask;

                                mask.mask |= translate_colormask(v3d, rt_mask,
                                                                 i) << (4 * i);
                        }
                }
        }

        /* GFXH-1431: On V3D 3.x, writing BLEND_CONFIG resets the constant
         * color.
         */
        if (v3d->dirty & V3D_DIRTY_BLEND_COLOR ||
            (V3D_VERSION < 41 && (v3d->dirty & V3D_DIRTY_BLEND))) {
                cl_emit(&job->bcl, BLEND_CONSTANT_COLOR, color) {
                        color.red_f16 = (v3d->swap_color_rb ?
                                          v3d->blend_color.hf[2] :
                                          v3d->blend_color.hf[0]);
                        color.green_f16 = v3d->blend_color.hf[1];
                        color.blue_f16 = (v3d->swap_color_rb ?
                                           v3d->blend_color.hf[0] :
                                           v3d->blend_color.hf[2]);
                        color.alpha_f16 = v3d->blend_color.hf[3];
                }
        }

        if (v3d->dirty & (V3D_DIRTY_ZSA | V3D_DIRTY_STENCIL_REF)) {
                struct pipe_stencil_state *front = &v3d->zsa->base.stencil[0];
                struct pipe_stencil_state *back = &v3d->zsa->base.stencil[1];

                if (front->enabled) {
                        cl_emit_with_prepacked(&job->bcl, STENCIL_CFG,
                                               v3d->zsa->stencil_front, config) {
                                config.stencil_ref_value =
                                        v3d->stencil_ref.ref_value[0];
                        }
                }

                if (back->enabled) {
                        cl_emit_with_prepacked(&job->bcl, STENCIL_CFG,
                                               v3d->zsa->stencil_back, config) {
                                config.stencil_ref_value =
                                        v3d->stencil_ref.ref_value[1];
                        }
                }
        }

#if V3D_VERSION < 40
        /* Pre-4.x, we have texture state that depends on both the sampler and
         * the view, so we merge them together at draw time.
         */
        if (v3d->dirty & V3D_DIRTY_FRAGTEX)
                emit_textures(v3d, &v3d->tex[PIPE_SHADER_FRAGMENT]);

        if (v3d->dirty & V3D_DIRTY_GEOMTEX)
                emit_textures(v3d, &v3d->tex[PIPE_SHADER_GEOMETRY]);

        if (v3d->dirty & V3D_DIRTY_VERTTEX)
                emit_textures(v3d, &v3d->tex[PIPE_SHADER_VERTEX]);
#endif

        if (v3d->dirty & V3D_DIRTY_FLAT_SHADE_FLAGS) {
                if (!emit_varying_flags(job,
                                        v3d->prog.fs->prog_data.fs->flat_shade_flags,
                                        emit_flat_shade_flags)) {
                        cl_emit(&job->bcl, ZERO_ALL_FLAT_SHADE_FLAGS, flags);
                }
        }

#if V3D_VERSION >= 40
        if (v3d->dirty & V3D_DIRTY_NOPERSPECTIVE_FLAGS) {
                if (!emit_varying_flags(job,
                                        v3d->prog.fs->prog_data.fs->noperspective_flags,
                                        emit_noperspective_flags)) {
                        cl_emit(&job->bcl, ZERO_ALL_NON_PERSPECTIVE_FLAGS, flags);
                }
        }

        if (v3d->dirty & V3D_DIRTY_CENTROID_FLAGS) {
                if (!emit_varying_flags(job,
                                        v3d->prog.fs->prog_data.fs->centroid_flags,
                                        emit_centroid_flags)) {
                        cl_emit(&job->bcl, ZERO_ALL_CENTROID_FLAGS, flags);
                }
        }
#endif

        /* Set up the transform feedback data specs (which VPM entries to
         * output to which buffers).
         */
        if (v3d->dirty & (V3D_DIRTY_STREAMOUT |
                          V3D_DIRTY_RASTERIZER |
                          V3D_DIRTY_PRIM_MODE)) {
                struct v3d_streamout_stateobj *so = &v3d->streamout;
                if (so->num_targets) {
                        bool psiz_per_vertex = (v3d->prim_mode == PIPE_PRIM_POINTS &&
                                                v3d->rasterizer->base.point_size_per_vertex);
                        struct v3d_uncompiled_shader *tf_shader =
                                get_tf_shader(v3d);
                        uint16_t *tf_specs = (psiz_per_vertex ?
                                              tf_shader->tf_specs_psiz :
                                              tf_shader->tf_specs);

#if V3D_VERSION >= 40
                        bool tf_enabled = v3d_transform_feedback_enabled(v3d);
                        job->tf_enabled |= tf_enabled;

                        cl_emit(&job->bcl, TRANSFORM_FEEDBACK_SPECS, tfe) {
                                tfe.number_of_16_bit_output_data_specs_following =
                                        tf_shader->num_tf_specs;
                                tfe.enable = tf_enabled;
                        };
#else /* V3D_VERSION < 40 */
                        cl_emit(&job->bcl, TRANSFORM_FEEDBACK_ENABLE, tfe) {
                                tfe.number_of_32_bit_output_buffer_address_following =
                                        so->num_targets;
                                tfe.number_of_16_bit_output_data_specs_following =
                                        tf_shader->num_tf_specs;
                        };
#endif /* V3D_VERSION < 40 */
                        for (int i = 0; i < tf_shader->num_tf_specs; i++) {
                                cl_emit_prepacked(&job->bcl, &tf_specs[i]);
                        }
                } else {
#if V3D_VERSION >= 40
                        cl_emit(&job->bcl, TRANSFORM_FEEDBACK_SPECS, tfe) {
                                tfe.enable = false;
                        };
#endif /* V3D_VERSION >= 40 */
                }
        }

        /* Set up the transform feedback buffers. */
        if (v3d->dirty & V3D_DIRTY_STREAMOUT) {
                struct v3d_uncompiled_shader *tf_shader = get_tf_shader(v3d);
                struct v3d_streamout_stateobj *so = &v3d->streamout;
                for (int i = 0; i < so->num_targets; i++) {
                        const struct pipe_stream_output_target *target =
                                so->targets[i];
                        struct v3d_resource *rsc = target ?
                                v3d_resource(target->buffer) : NULL;
                        struct pipe_shader_state *ss = &tf_shader->base;
                        struct pipe_stream_output_info *info = &ss->stream_output;
                        uint32_t offset = (v3d->streamout.offsets[i] *
                                           info->stride[i] * 4);

#if V3D_VERSION >= 40
                        if (!target)
                                continue;

                        cl_emit(&job->bcl, TRANSFORM_FEEDBACK_BUFFER, output) {
                                output.buffer_address =
                                        cl_address(rsc->bo,
                                                   target->buffer_offset +
                                                   offset);
                                output.buffer_size_in_32_bit_words =
                                        (target->buffer_size - offset) >> 2;
                                output.buffer_number = i;
                        }
#else /* V3D_VERSION < 40 */
                        cl_emit(&job->bcl, TRANSFORM_FEEDBACK_OUTPUT_ADDRESS, output) {
                                if (target) {
                                        output.address =
                                                cl_address(rsc->bo,
                                                           target->buffer_offset +
                                                           offset);
                                }
                        };
#endif /* V3D_VERSION < 40 */
                        if (target) {
                                v3d_job_add_tf_write_resource(v3d->job,
                                                              target->buffer);
                        }
                        /* XXX: buffer_size? */
                }
        }

        if (v3d->dirty & V3D_DIRTY_OQ) {
                cl_emit(&job->bcl, OCCLUSION_QUERY_COUNTER, counter) {
                        if (v3d->active_queries && v3d->current_oq) {
                                counter.address = cl_address(v3d->current_oq, 0);
                        }
                }
        }

#if V3D_VERSION >= 40
        if (v3d->dirty & V3D_DIRTY_SAMPLE_STATE) {
                cl_emit(&job->bcl, SAMPLE_STATE, state) {
                        /* Note: SampleCoverage was handled at the
                         * frontend level by converting to sample_mask.
                         */
                        state.coverage = 1.0;
                        state.mask = job->msaa ? v3d->sample_mask : 0xf;
                }
        }
#endif
}
