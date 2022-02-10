/**************************************************************************
 * 
 * Copyright 2003 VMware, Inc.
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


#include <stdio.h>
#include "main/arrayobj.h"
#include "main/glheader.h"
#include "main/context.h"

#include "pipe/p_defines.h"
#include "st_context.h"
#include "st_atom.h"
#include "st_program.h"
#include "st_manager.h"
#include "st_util.h"

#include "util/u_cpu_detect.h"


typedef void (*update_func_t)(struct st_context *st);

/* The list state update functions. */
static update_func_t update_functions[ST_NUM_ATOMS];

static void
init_atoms_once(void)
{
#define ST_STATE(FLAG, st_update) update_functions[FLAG##_INDEX] = st_update;
#include "st_atom_list.h"
#undef ST_STATE

   if (util_get_cpu_caps()->has_popcnt)
      update_functions[ST_NEW_VERTEX_ARRAYS_INDEX] = st_update_array_with_popcnt;
}

void st_init_atoms( struct st_context *st )
{
   STATIC_ASSERT(ARRAY_SIZE(update_functions) <= 64);

   static once_flag flag = ONCE_FLAG_INIT;
   call_once(&flag, init_atoms_once);
}


void st_destroy_atoms( struct st_context *st )
{
   /* no-op */
}


/* Too complex to figure out, just check every time:
 */
static void check_program_state( struct st_context *st )
{
   struct gl_context *ctx = st->ctx;
   struct gl_program *old_vp = st->vp;
   struct gl_program *old_tcp = st->tcp;
   struct gl_program *old_tep = st->tep;
   struct gl_program *old_gp = st->gp;
   struct gl_program *old_fp = st->fp;

   struct gl_program *new_vp = ctx->VertexProgram._Current;
   struct gl_program *new_tcp = ctx->TessCtrlProgram._Current;
   struct gl_program *new_tep = ctx->TessEvalProgram._Current;
   struct gl_program *new_gp = ctx->GeometryProgram._Current;
   struct gl_program *new_fp = ctx->FragmentProgram._Current;
   uint64_t dirty = 0;
   unsigned num_viewports = 1;

   /* Flag states used by both new and old shaders to unbind shader resources
    * properly when transitioning to shaders that don't use them.
    */
   if (unlikely(new_vp != old_vp)) {
      ctx->Array.NewVertexElements = true;
      if (old_vp)
         dirty |= old_vp->affected_states;
      if (new_vp)
         dirty |= ST_NEW_VERTEX_PROGRAM(st, new_vp);
   }

   if (unlikely(new_tcp != old_tcp)) {
      if (old_tcp)
         dirty |= old_tcp->affected_states;
      if (new_tcp)
         dirty |= new_tcp->affected_states;
   }

   if (unlikely(new_tep != old_tep)) {
      if (old_tep)
         dirty |= old_tep->affected_states;
      if (new_tep)
         dirty |= new_tep->affected_states;
   }

   if (unlikely(new_gp != old_gp)) {
      if (old_gp)
         dirty |= old_gp->affected_states;
      if (new_gp)
         dirty |= new_gp->affected_states;
   }

   if (unlikely(new_fp != old_fp)) {
      if (old_fp)
         dirty |= old_fp->affected_states;
      if (new_fp)
         dirty |= new_fp->affected_states;
   }

   /* Find out the number of viewports. This determines how many scissors
    * and viewport states we need to update.
    */
   struct gl_program *last_prim_shader = new_gp ? new_gp :
                                         new_tep ? new_tep : new_vp;
   if (last_prim_shader &&
       last_prim_shader->info.outputs_written & (
             VARYING_BIT_VIEWPORT | VARYING_BIT_VIEWPORT_MASK))
      num_viewports = ctx->Const.MaxViewports;

   if (st->state.num_viewports != num_viewports) {
      st->state.num_viewports = num_viewports;
      dirty |= ST_NEW_VIEWPORT;

      if (ctx->Scissor.EnableFlags & u_bit_consecutive(0, num_viewports))
         dirty |= ST_NEW_SCISSOR;
   }

   st->dirty |= dirty;
}

void st_update_edgeflags(struct st_context *st, bool per_vertex_edgeflags)
{
   bool edgeflags_enabled = st->ctx->Polygon.FrontMode != GL_FILL ||
                            st->ctx->Polygon.BackMode != GL_FILL;
   bool vertdata_edgeflags = edgeflags_enabled && per_vertex_edgeflags;

   if (vertdata_edgeflags != st->vertdata_edgeflags) {
      st->vertdata_edgeflags = vertdata_edgeflags;

      struct gl_program *vp = st->ctx->VertexProgram._Current;
      if (vp)
         st->dirty |= ST_NEW_VERTEX_PROGRAM(st, vp);
   }

   bool edgeflag_culls_prims = edgeflags_enabled && !vertdata_edgeflags &&
                               !st->ctx->Current.Attrib[VERT_ATTRIB_EDGEFLAG][0];
   if (edgeflag_culls_prims != st->edgeflag_culls_prims) {
      st->edgeflag_culls_prims = edgeflag_culls_prims;
      st->dirty |= ST_NEW_RASTERIZER;
   }
}

static void check_attrib_edgeflag(struct st_context *st)
{
   st_update_edgeflags(st, _mesa_draw_edge_flag_array_enabled(st->ctx));
}


/***********************************************************************
 * Update all derived state:
 */

void st_validate_state( struct st_context *st, enum st_pipeline pipeline )
{
   struct gl_context *ctx = st->ctx;
   uint64_t dirty, pipeline_mask;
   uint32_t dirty_lo, dirty_hi;

   /* Get Mesa driver state.
    *
    * Inactive states are shader states not used by shaders at the moment.
    */
   st->dirty |= ctx->NewDriverState & st->active_states & ST_ALL_STATES_MASK;
   ctx->NewDriverState &= ~st->dirty;

   /* Get pipeline state. */
   switch (pipeline) {
   case ST_PIPELINE_RENDER:
   case ST_PIPELINE_RENDER_NO_VARRAYS:
      if (st->ctx->API == API_OPENGL_COMPAT)
         check_attrib_edgeflag(st);

      if (st->gfx_shaders_may_be_dirty) {
         check_program_state(st);
         st->gfx_shaders_may_be_dirty = false;
      }

      st_manager_validate_framebuffers(st);

      if (pipeline == ST_PIPELINE_RENDER)
         pipeline_mask = ST_PIPELINE_RENDER_STATE_MASK;
      else
         pipeline_mask = ST_PIPELINE_RENDER_STATE_MASK_NO_VARRAYS;
      break;

   case ST_PIPELINE_CLEAR:
      st_manager_validate_framebuffers(st);
      pipeline_mask = ST_PIPELINE_CLEAR_STATE_MASK;
      break;

   case ST_PIPELINE_META:
      if (st->gfx_shaders_may_be_dirty) {
         check_program_state(st);
         st->gfx_shaders_may_be_dirty = false;
      }

      st_manager_validate_framebuffers(st);
      pipeline_mask = ST_PIPELINE_META_STATE_MASK;
      break;

   case ST_PIPELINE_UPDATE_FRAMEBUFFER:
      st_manager_validate_framebuffers(st);
      pipeline_mask = ST_PIPELINE_UPDATE_FB_STATE_MASK;
      break;

   case ST_PIPELINE_COMPUTE: {
      struct gl_program *old_cp = st->cp;
      struct gl_program *new_cp = ctx->ComputeProgram._Current;

      if (new_cp != old_cp) {
         if (old_cp)
            st->dirty |= old_cp->affected_states;
         assert(new_cp);
         st->dirty |= new_cp->affected_states;
      }

      st->compute_shader_may_be_dirty = false;

      /*
       * We add the ST_NEW_FB_STATE bit here as well, because glBindFramebuffer
       * acts as a barrier that breaks feedback loops between the framebuffer
       * and textures bound to the framebuffer, even when those textures are
       * accessed by compute shaders; so we must inform the driver of new
       * framebuffer state.
       */
      pipeline_mask = ST_PIPELINE_COMPUTE_STATE_MASK | ST_NEW_FB_STATE;
      break;
   }

   default:
      unreachable("Invalid pipeline specified");
   }

   dirty = st->dirty & pipeline_mask;
   if (!dirty)
      return;

   dirty_lo = dirty;
   dirty_hi = dirty >> 32;

   /* Update states.
    *
    * Don't use u_bit_scan64, it may be slower on 32-bit.
    */
   while (dirty_lo)
      update_functions[u_bit_scan(&dirty_lo)](st);
   while (dirty_hi)
      update_functions[32 + u_bit_scan(&dirty_hi)](st);

   /* Clear the render or compute state bits. */
   st->dirty &= ~pipeline_mask;
}
