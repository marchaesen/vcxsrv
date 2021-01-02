/**************************************************************************
 *
 * Copyright 2007-2008 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#ifndef CSO_CONTEXT_H
#define CSO_CONTEXT_H

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "pipe/p_defines.h"
#include "cso_cache.h"


#ifdef	__cplusplus
extern "C" {
#endif

struct cso_context;
struct u_vbuf;

#define CSO_NO_USER_VERTEX_BUFFERS (1 << 0)
#define CSO_NO_64B_VERTEX_BUFFERS  (1 << 1)

struct cso_context *cso_create_context(struct pipe_context *pipe,
                                       unsigned flags);
void cso_destroy_context( struct cso_context *cso );
struct pipe_context *cso_get_pipe_context(struct cso_context *cso);


enum pipe_error cso_set_blend( struct cso_context *cso,
                               const struct pipe_blend_state *blend );


enum pipe_error cso_set_depth_stencil_alpha( struct cso_context *cso,
                                             const struct pipe_depth_stencil_alpha_state *dsa );



enum pipe_error cso_set_rasterizer( struct cso_context *cso,
                                    const struct pipe_rasterizer_state *rasterizer );


void
cso_set_samplers(struct cso_context *cso,
                 enum pipe_shader_type shader_stage,
                 unsigned count,
                 const struct pipe_sampler_state **states);


/* Alternate interface to support gallium frontends that like to modify
 * samplers one at a time:
 */
void
cso_single_sampler(struct cso_context *cso, enum pipe_shader_type shader_stage,
                   unsigned idx, const struct pipe_sampler_state *states);

void
cso_single_sampler_done(struct cso_context *cso,
                        enum pipe_shader_type shader_stage);


enum pipe_error cso_set_vertex_elements(struct cso_context *ctx,
                                        const struct cso_velems_state *velems);

void cso_set_vertex_buffers(struct cso_context *ctx,
                            unsigned start_slot, unsigned count,
                            const struct pipe_vertex_buffer *buffers);

void cso_set_stream_outputs(struct cso_context *ctx,
                            unsigned num_targets,
                            struct pipe_stream_output_target **targets,
                            const unsigned *offsets);


/*
 * We don't provide shader caching in CSO.  Most of the time the api provides
 * object semantics for shaders anyway, and the cases where it doesn't
 * (eg mesa's internally-generated texenv programs), it will be up to
 * gallium frontends to implement their own specialized caching.
 */

void cso_set_fragment_shader_handle(struct cso_context *ctx, void *handle);
void cso_set_vertex_shader_handle(struct cso_context *ctx, void *handle);
void cso_set_geometry_shader_handle(struct cso_context *ctx, void *handle);
void cso_set_tessctrl_shader_handle(struct cso_context *ctx, void *handle);
void cso_set_tesseval_shader_handle(struct cso_context *ctx, void *handle);
void cso_set_compute_shader_handle(struct cso_context *ctx, void *handle);


void cso_set_framebuffer(struct cso_context *cso,
                         const struct pipe_framebuffer_state *fb);


void cso_set_viewport(struct cso_context *cso,
                      const struct pipe_viewport_state *vp);
void cso_set_viewport_dims(struct cso_context *ctx,
                           float width, float height, boolean invert);

void cso_set_sample_mask(struct cso_context *cso, unsigned stencil_mask);

void cso_set_min_samples(struct cso_context *cso, unsigned min_samples);

void cso_set_stencil_ref(struct cso_context *cso,
                         const struct pipe_stencil_ref sr);

void cso_set_render_condition(struct cso_context *cso,
                              struct pipe_query *query,
                              boolean condition,
                              enum pipe_render_cond_flag mode);


#define CSO_BIT_AUX_VERTEX_BUFFER_SLOT    0x1
#define CSO_BIT_BLEND                     0x2
#define CSO_BIT_DEPTH_STENCIL_ALPHA       0x4
#define CSO_BIT_FRAGMENT_SAMPLERS         0x8
#define CSO_BIT_FRAGMENT_SAMPLER_VIEWS   0x10
#define CSO_BIT_FRAGMENT_SHADER          0x20
#define CSO_BIT_FRAMEBUFFER              0x40
#define CSO_BIT_GEOMETRY_SHADER          0x80
#define CSO_BIT_MIN_SAMPLES             0x100
#define CSO_BIT_RASTERIZER              0x200
#define CSO_BIT_RENDER_CONDITION        0x400
#define CSO_BIT_SAMPLE_MASK             0x800
#define CSO_BIT_STENCIL_REF            0x1000
#define CSO_BIT_STREAM_OUTPUTS         0x2000
#define CSO_BIT_TESSCTRL_SHADER        0x4000
#define CSO_BIT_TESSEVAL_SHADER        0x8000
#define CSO_BIT_VERTEX_ELEMENTS       0x10000
#define CSO_BIT_VERTEX_SHADER         0x20000
#define CSO_BIT_VIEWPORT              0x40000
#define CSO_BIT_PAUSE_QUERIES         0x80000
#define CSO_BIT_FRAGMENT_IMAGE0      0x100000

#define CSO_BITS_ALL_SHADERS (CSO_BIT_VERTEX_SHADER | \
                              CSO_BIT_FRAGMENT_SHADER | \
                              CSO_BIT_GEOMETRY_SHADER | \
                              CSO_BIT_TESSCTRL_SHADER | \
                              CSO_BIT_TESSEVAL_SHADER)

void cso_save_state(struct cso_context *cso, unsigned state_mask);
void cso_restore_state(struct cso_context *cso);


/* sampler view state */

void
cso_set_sampler_views(struct cso_context *cso,
                      enum pipe_shader_type shader_stage,
                      unsigned count,
                      struct pipe_sampler_view **views);


/* shader images */

void
cso_set_shader_images(struct cso_context *cso,
                      enum pipe_shader_type shader_stage,
                      unsigned start, unsigned count,
                      struct pipe_image_view *views);


/* constant buffers */

void cso_set_constant_buffer(struct cso_context *cso,
                             enum pipe_shader_type shader_stage,
                             unsigned index, struct pipe_constant_buffer *cb);
void cso_set_constant_buffer_resource(struct cso_context *cso,
                                      enum pipe_shader_type shader_stage,
                                      unsigned index,
                                      struct pipe_resource *buffer);
void cso_set_constant_user_buffer(struct cso_context *cso,
                                  enum pipe_shader_type shader_stage,
                                  unsigned index, void *ptr, unsigned size);
void cso_save_constant_buffer_slot0(struct cso_context *cso,
                                    enum pipe_shader_type shader_stage);
void cso_restore_constant_buffer_slot0(struct cso_context *cso,
                                       enum pipe_shader_type shader_stage);

/* Optimized version. */
void
cso_set_vertex_buffers_and_elements(struct cso_context *ctx,
                                    const struct cso_velems_state *velems,
                                    unsigned vb_count,
                                    unsigned unbind_trailing_vb_count,
                                    const struct pipe_vertex_buffer *vbuffers,
                                    bool uses_user_vertex_buffers);

/* drawing */

void
cso_draw_vbo(struct cso_context *cso,
             const struct pipe_draw_info *info,
             const struct pipe_draw_indirect_info *indirect,
             const struct pipe_draw_start_count draw);

void
cso_draw_arrays_instanced(struct cso_context *cso, uint mode,
                          uint start, uint count,
                          uint start_instance, uint instance_count);

void
cso_draw_arrays(struct cso_context *cso, uint mode, uint start, uint count);

#ifdef	__cplusplus
}
#endif

#endif
