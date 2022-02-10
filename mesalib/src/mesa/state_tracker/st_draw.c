/**************************************************************************
 *
 * Copyright 2007 VMware, Inc.
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

/*
 * This file implements the st_draw_vbo() function which is called from
 * Mesa's VBO module.  All point/line/triangle rendering is done through
 * this function whether the user called glBegin/End, glDrawArrays,
 * glDrawElements, glEvalMesh, or glCalList, etc.
 *
 * Authors:
 *   Keith Whitwell <keithw@vmware.com>
 */


#include "main/errors.h"

#include "main/image.h"
#include "main/bufferobj.h"
#include "main/macros.h"
#include "main/varray.h"

#include "compiler/glsl/ir_uniform.h"

#include "vbo/vbo.h"

#include "st_context.h"
#include "st_atom.h"
#include "st_cb_bitmap.h"
#include "st_debug.h"
#include "st_draw.h"
#include "st_program.h"
#include "st_util.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "util/u_cpu_detect.h"
#include "util/u_inlines.h"
#include "util/format/u_format.h"
#include "util/u_prim.h"
#include "util/u_draw.h"
#include "util/u_upload_mgr.h"
#include "util/u_threaded_context.h"
#include "draw/draw_context.h"
#include "cso_cache/cso_context.h"


/**
 * Translate OpenGL primtive type (GL_POINTS, GL_TRIANGLE_STRIP, etc) to
 * the corresponding Gallium type.
 */
static unsigned
translate_prim(const struct gl_context *ctx, unsigned prim)
{
   /* GL prims should match Gallium prims, spot-check a few */
   STATIC_ASSERT(GL_POINTS == PIPE_PRIM_POINTS);
   STATIC_ASSERT(GL_QUADS == PIPE_PRIM_QUADS);
   STATIC_ASSERT(GL_TRIANGLE_STRIP_ADJACENCY == PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY);
   STATIC_ASSERT(GL_PATCHES == PIPE_PRIM_PATCHES);

   return prim;
}

static inline void
prepare_draw(struct st_context *st, struct gl_context *ctx, uint64_t state_mask,
             enum st_pipeline pipeline)
{
   /* Mesa core state should have been validated already */
   assert(ctx->NewState == 0x0);

   if (unlikely(!st->bitmap.cache.empty))
      st_flush_bitmap_cache(st);

   st_invalidate_readpix_cache(st);

   /* Validate state. */
   if ((st->dirty | ctx->NewDriverState) & st->active_states & state_mask ||
       st->gfx_shaders_may_be_dirty) {
      st_validate_state(st, pipeline);
   }

   /* Pin threads regularly to the same Zen CCX that the main thread is
    * running on. The main thread can move between CCXs.
    */
   if (unlikely(st->pin_thread_counter != ST_L3_PINNING_DISABLED &&
                /* no glthread */
                !ctx->GLThread.enabled &&
                /* do it occasionally */
                ++st->pin_thread_counter % 512 == 0)) {
      st->pin_thread_counter = 0;

      int cpu = util_get_current_cpu();
      if (cpu >= 0) {
         struct pipe_context *pipe = st->pipe;
         uint16_t L3_cache = util_get_cpu_caps()->cpu_to_L3[cpu];

         if (L3_cache != U_CPU_INVALID_L3) {
            pipe->set_context_param(pipe,
                                    PIPE_CONTEXT_PARAM_PIN_THREADS_TO_L3_CACHE,
                                    L3_cache);
         }
      }
   }
}

static bool ALWAYS_INLINE
prepare_indexed_draw(/* pass both st and ctx to reduce dereferences */
                     struct st_context *st,
                     struct gl_context *ctx,
                     struct pipe_draw_info *info,
                     const struct pipe_draw_start_count_bias *draws,
                     unsigned num_draws)
{
   if (info->index_size) {
      /* Get index bounds for user buffers. */
      if (!info->index_bounds_valid &&
          st->draw_needs_minmax_index) {
         /* Return if this fails, which means all draws have count == 0. */
         if (!vbo_get_minmax_indices_gallium(ctx, info, draws, num_draws))
            return false;

         info->index_bounds_valid = true;
      }

      if (!info->has_user_indices) {
         if (st->pipe->draw_vbo == tc_draw_vbo) {
            /* Fast path for u_threaded_context. This eliminates the atomic
             * increment for the index buffer refcount when adding it into
             * the threaded batch buffer.
             */
            info->index.resource =
               _mesa_get_bufferobj_reference(ctx, info->index.gl_bo);
            info->take_index_buffer_ownership = true;
         } else {
            info->index.resource = info->index.gl_bo->buffer;
         }

         /* Return if the bound element array buffer doesn't have any backing
          * storage. (nothing to do)
          */
         if (unlikely(!info->index.resource))
            return false;
      }
   }
   return true;
}

static void
st_draw_gallium(struct gl_context *ctx,
                struct pipe_draw_info *info,
                unsigned drawid_offset,
                const struct pipe_draw_start_count_bias *draws,
                unsigned num_draws)
{
   struct st_context *st = st_context(ctx);

   prepare_draw(st, ctx, ST_PIPELINE_RENDER_STATE_MASK, ST_PIPELINE_RENDER);

   if (!prepare_indexed_draw(st, ctx, info, draws, num_draws))
      return;

   cso_multi_draw(st->cso_context, info, drawid_offset, draws, num_draws);
}

static void
st_draw_gallium_multimode(struct gl_context *ctx,
                          struct pipe_draw_info *info,
                          const struct pipe_draw_start_count_bias *draws,
                          const unsigned char *mode,
                          unsigned num_draws)
{
   struct st_context *st = st_context(ctx);

   prepare_draw(st, ctx, ST_PIPELINE_RENDER_STATE_MASK, ST_PIPELINE_RENDER);

   if (!prepare_indexed_draw(st, ctx, info, draws, num_draws))
      return;

   unsigned i, first;
   struct cso_context *cso = st->cso_context;

   /* Find consecutive draws where mode doesn't vary. */
   for (i = 0, first = 0; i <= num_draws; i++) {
      if (i == num_draws || mode[i] != mode[first]) {
         info->mode = mode[first];
         cso_multi_draw(cso, info, 0, &draws[first], i - first);
         first = i;

         /* We can pass the reference only once. st_buffer_object keeps
          * the reference alive for later draws.
          */
         info->take_index_buffer_ownership = false;
      }
   }
}

void
st_indirect_draw_vbo(struct gl_context *ctx,
                     GLuint mode,
                     struct gl_buffer_object *indirect_data,
                     GLsizeiptr indirect_offset,
                     unsigned draw_count,
                     unsigned stride,
                     struct gl_buffer_object *indirect_draw_count,
                     GLsizeiptr indirect_draw_count_offset,
                     const struct _mesa_index_buffer *ib,
                     bool primitive_restart,
                     unsigned restart_index)
{
   struct st_context *st = st_context(ctx);
   struct pipe_draw_info info;
   struct pipe_draw_indirect_info indirect;
   struct pipe_draw_start_count_bias draw = {0};

   assert(stride);
   prepare_draw(st, ctx, ST_PIPELINE_RENDER_STATE_MASK, ST_PIPELINE_RENDER);

   memset(&indirect, 0, sizeof(indirect));
   util_draw_init_info(&info);
   info.max_index = ~0u; /* so that u_vbuf can tell that it's unknown */

   if (ib) {
      struct gl_buffer_object *bufobj = ib->obj;

      /* indices are always in a real VBO */
      assert(bufobj);

      info.index_size = 1 << ib->index_size_shift;
      info.index.resource = bufobj->buffer;
      draw.start = pointer_to_offset(ib->ptr) >> ib->index_size_shift;

      info.restart_index = restart_index;
      info.primitive_restart = primitive_restart;
   }

   info.mode = translate_prim(ctx, mode);
   indirect.buffer = indirect_data->buffer;
   indirect.offset = indirect_offset;

   /* Viewperf2020/Maya draws with a buffer that has no storage. */
   if (!indirect.buffer)
      return;

   if (!st->has_multi_draw_indirect) {
      int i;

      assert(!indirect_draw_count);
      indirect.draw_count = 1;
      for (i = 0; i < draw_count; i++) {
         cso_draw_vbo(st->cso_context, &info, i, &indirect, draw);
         indirect.offset += stride;
      }
   } else {
      indirect.draw_count = draw_count;
      indirect.stride = stride;
      if (indirect_draw_count) {
         indirect.indirect_draw_count =
            indirect_draw_count->buffer;
         indirect.indirect_draw_count_offset = indirect_draw_count_offset;
      }
      cso_draw_vbo(st->cso_context, &info, 0, &indirect, draw);
   }
}

void
st_draw_transform_feedback(struct gl_context *ctx, GLenum mode,
                           unsigned num_instances, unsigned stream,
                           struct gl_transform_feedback_object *tfb_vertcount)
{
   struct st_context *st = st_context(ctx);
   struct pipe_draw_info info;
   struct pipe_draw_indirect_info indirect;
   struct pipe_draw_start_count_bias draw = {0};

   prepare_draw(st, ctx, ST_PIPELINE_RENDER_STATE_MASK, ST_PIPELINE_RENDER);

   memset(&indirect, 0, sizeof(indirect));
   util_draw_init_info(&info);
   info.max_index = ~0u; /* so that u_vbuf can tell that it's unknown */
   info.mode = translate_prim(ctx, mode);
   info.instance_count = num_instances;

   /* Transform feedback drawing is always non-indexed. */
   /* Set info.count_from_stream_output. */
   indirect.count_from_stream_output = tfb_vertcount->draw_count[stream];
   if (indirect.count_from_stream_output == NULL)
      return;

   cso_draw_vbo(st->cso_context, &info, 0, &indirect, draw);
}

static void
st_draw_gallium_vertex_state(struct gl_context *ctx,
                             struct pipe_vertex_state *state,
                             struct pipe_draw_vertex_state_info info,
                             const struct pipe_draw_start_count_bias *draws,
                             const uint8_t *mode,
                             unsigned num_draws,
                             bool per_vertex_edgeflags)
{
   struct st_context *st = st_context(ctx);
   bool old_vertdata_edgeflags = st->vertdata_edgeflags;

   /* We don't flag any other states to make st_validate state update edge
    * flags, so we need to update them here.
    */
   st_update_edgeflags(st, per_vertex_edgeflags);

   prepare_draw(st, ctx, ST_PIPELINE_RENDER_STATE_MASK_NO_VARRAYS,
                ST_PIPELINE_RENDER_NO_VARRAYS);

   struct pipe_context *pipe = st->pipe;
   uint32_t velem_mask = ctx->VertexProgram._Current->info.inputs_read;

   if (!mode) {
      pipe->draw_vertex_state(pipe, state, velem_mask, info, draws, num_draws);
   } else {
      /* Find consecutive draws where mode doesn't vary. */
      for (unsigned i = 0, first = 0; i <= num_draws; i++) {
         if (i == num_draws || mode[i] != mode[first]) {
            unsigned current_num_draws = i - first;

            /* Increase refcount to be able to use take_vertex_state_ownership
             * with all draws.
             */
            if (i != num_draws && info.take_vertex_state_ownership)
               p_atomic_inc(&state->reference.count);

            info.mode = mode[first];
            pipe->draw_vertex_state(pipe, state, velem_mask, info, &draws[first],
                                    current_num_draws);
            first = i;
         }
      }
   }

   /* If per-vertex edge flags are different than the non-display-list state,
    *  just flag ST_NEW_VERTEX_ARRAY, which will also completely revalidate
    * edge flags in st_validate_state.
    */
   if (st->vertdata_edgeflags != old_vertdata_edgeflags) {
      ctx->Array.NewVertexElements = true;
      st->dirty |= ST_NEW_VERTEX_ARRAYS;
   }
}

void
st_init_draw_functions(struct pipe_screen *screen,
                       struct dd_function_table *functions)
{
   functions->DrawGallium = st_draw_gallium;
   functions->DrawGalliumMultiMode = st_draw_gallium_multimode;

   if (screen->get_param(screen, PIPE_CAP_DRAW_VERTEX_STATE)) {
      functions->DrawGalliumVertexState = st_draw_gallium_vertex_state;
      functions->CreateGalliumVertexState = st_create_gallium_vertex_state;
   }
}


void
st_destroy_draw(struct st_context *st)
{
   draw_destroy(st->draw);
}

/**
 * Getter for the draw_context, so that initialization of it can happen only
 * when needed (the TGSI exec machines take up quite a bit of memory).
 */
struct draw_context *
st_get_draw_context(struct st_context *st)
{
   if (!st->draw) {
      st->draw = draw_create(st->pipe);
      if (!st->draw) {
         _mesa_error(st->ctx, GL_OUT_OF_MEMORY, "feedback fallback allocation");
         return NULL;
      }
   }

   /* Disable draw options that might convert points/lines to tris, etc.
    * as that would foul-up feedback/selection mode.
    */
   draw_wide_line_threshold(st->draw, 1000.0f);
   draw_wide_point_threshold(st->draw, 1000.0f);
   draw_enable_line_stipple(st->draw, FALSE);
   draw_enable_point_sprites(st->draw, FALSE);

   return st->draw;
}

/**
 * Draw a quad with given position, texcoords and color.
 */
bool
st_draw_quad(struct st_context *st,
             float x0, float y0, float x1, float y1, float z,
             float s0, float t0, float s1, float t1,
             const float *color,
             unsigned num_instances)
{
   struct pipe_vertex_buffer vb = {0};
   struct st_util_vertex *verts;

   vb.stride = sizeof(struct st_util_vertex);

   u_upload_alloc(st->pipe->stream_uploader, 0,
                  4 * sizeof(struct st_util_vertex), 4,
                  &vb.buffer_offset, &vb.buffer.resource, (void **) &verts);
   if (!vb.buffer.resource) {
      return false;
   }

   /* lower-left */
   verts[0].x = x0;
   verts[0].y = y1;
   verts[0].z = z;
   verts[0].r = color[0];
   verts[0].g = color[1];
   verts[0].b = color[2];
   verts[0].a = color[3];
   verts[0].s = s0;
   verts[0].t = t0;

   /* lower-right */
   verts[1].x = x1;
   verts[1].y = y1;
   verts[1].z = z;
   verts[1].r = color[0];
   verts[1].g = color[1];
   verts[1].b = color[2];
   verts[1].a = color[3];
   verts[1].s = s1;
   verts[1].t = t0;

   /* upper-right */
   verts[2].x = x1;
   verts[2].y = y0;
   verts[2].z = z;
   verts[2].r = color[0];
   verts[2].g = color[1];
   verts[2].b = color[2];
   verts[2].a = color[3];
   verts[2].s = s1;
   verts[2].t = t1;

   /* upper-left */
   verts[3].x = x0;
   verts[3].y = y0;
   verts[3].z = z;
   verts[3].r = color[0];
   verts[3].g = color[1];
   verts[3].b = color[2];
   verts[3].a = color[3];
   verts[3].s = s0;
   verts[3].t = t1;

   u_upload_unmap(st->pipe->stream_uploader);

   cso_set_vertex_buffers(st->cso_context, 0, 1, 0, false, &vb);
   st->last_num_vbuffers = MAX2(st->last_num_vbuffers, 1);

   if (num_instances > 1) {
      cso_draw_arrays_instanced(st->cso_context, PIPE_PRIM_TRIANGLE_FAN, 0, 4,
                                0, num_instances);
   } else {
      cso_draw_arrays(st->cso_context, PIPE_PRIM_TRIANGLE_FAN, 0, 4);
   }

   pipe_resource_reference(&vb.buffer.resource, NULL);

   return true;
}
