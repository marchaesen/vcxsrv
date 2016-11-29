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

/**
 * State validation for vertex/fragment shaders.
 * Note that we have to delay most vertex/fragment shader translation
 * until rendering time since the linkage between the vertex outputs and
 * fragment inputs can vary depending on the pairing of shaders.
 *
 * Authors:
 *   Brian Paul
 */

#include "main/imports.h"
#include "main/mtypes.h"
#include "main/framebuffer.h"
#include "main/texobj.h"
#include "main/texstate.h"
#include "program/program.h"

#include "pipe/p_context.h"
#include "pipe/p_shader_tokens.h"
#include "util/u_simple_shaders.h"
#include "cso_cache/cso_context.h"
#include "util/u_debug.h"

#include "st_context.h"
#include "st_atom.h"
#include "st_program.h"
#include "st_texture.h"


/** Compress the fog function enums into a 2-bit value */
static GLuint
translate_fog_mode(GLenum mode)
{
   switch (mode) {
   case GL_LINEAR: return 1;
   case GL_EXP:    return 2;
   case GL_EXP2:   return 3;
   default:
      return 0;
   }
}

static unsigned
get_texture_target(struct gl_context *ctx, const unsigned unit)
{
   struct gl_texture_object *texObj = _mesa_get_tex_unit(ctx, unit)->_Current;
   gl_texture_index index;

   if (texObj) {
      index = _mesa_tex_target_to_index(ctx, texObj->Target);
   } else {
      /* fallback for missing texture */
      index = TEXTURE_2D_INDEX;
   }

   /* Map mesa texture target to TGSI texture target.
    * Copied from st_mesa_to_tgsi.c, the shadow part is omitted */
   switch(index) {
   case TEXTURE_2D_MULTISAMPLE_INDEX: return TGSI_TEXTURE_2D_MSAA;
   case TEXTURE_2D_MULTISAMPLE_ARRAY_INDEX: return TGSI_TEXTURE_2D_ARRAY_MSAA;
   case TEXTURE_BUFFER_INDEX: return TGSI_TEXTURE_BUFFER;
   case TEXTURE_1D_INDEX:   return TGSI_TEXTURE_1D;
   case TEXTURE_2D_INDEX:   return TGSI_TEXTURE_2D;
   case TEXTURE_3D_INDEX:   return TGSI_TEXTURE_3D;
   case TEXTURE_CUBE_INDEX: return TGSI_TEXTURE_CUBE;
   case TEXTURE_CUBE_ARRAY_INDEX: return TGSI_TEXTURE_CUBE_ARRAY;
   case TEXTURE_RECT_INDEX: return TGSI_TEXTURE_RECT;
   case TEXTURE_1D_ARRAY_INDEX:   return TGSI_TEXTURE_1D_ARRAY;
   case TEXTURE_2D_ARRAY_INDEX:   return TGSI_TEXTURE_2D_ARRAY;
   case TEXTURE_EXTERNAL_INDEX:   return TGSI_TEXTURE_2D;
   default:
      debug_assert(0);
      return TGSI_TEXTURE_1D;
   }
}


/**
 * Update fragment program state/atom.  This involves translating the
 * Mesa fragment program into a gallium fragment program and binding it.
 */
static void
update_fp( struct st_context *st )
{
   struct st_fragment_program *stfp;
   struct st_fp_variant_key key;

   assert(st->ctx->FragmentProgram._Current);
   stfp = st_fragment_program(st->ctx->FragmentProgram._Current);
   assert(stfp->Base.Target == GL_FRAGMENT_PROGRAM_ARB);

   memset(&key, 0, sizeof(key));
   key.st = st->has_shareable_shaders ? NULL : st;

   /* _NEW_FRAG_CLAMP */
   key.clamp_color = st->clamp_frag_color_in_shader &&
                     st->ctx->Color._ClampFragmentColor;

   /* _NEW_MULTISAMPLE | _NEW_BUFFERS */
   key.persample_shading =
      st->force_persample_in_shader &&
      _mesa_is_multisample_enabled(st->ctx) &&
      st->ctx->Multisample.SampleShading &&
      st->ctx->Multisample.MinSampleShadingValue *
      _mesa_geometric_samples(st->ctx->DrawBuffer) > 1;

   if (stfp->ati_fs) {
      unsigned u;

      if (st->ctx->Fog.Enabled) {
         key.fog = translate_fog_mode(st->ctx->Fog.Mode);
      }

      for (u = 0; u < MAX_NUM_FRAGMENT_REGISTERS_ATI; u++) {
         key.texture_targets[u] = get_texture_target(st->ctx, u);
      }
   }

   key.external = st_get_external_sampler_key(st, &stfp->Base);

   st->fp_variant = st_get_fp_variant(st, stfp, &key);

   st_reference_fragprog(st, &st->fp, stfp);

   cso_set_fragment_shader_handle(st->cso_context,
                                  st->fp_variant->driver_shader);
}


const struct st_tracked_state st_update_fp = {
   update_fp  					/* update */
};



/**
 * Update vertex program state/atom.  This involves translating the
 * Mesa vertex program into a gallium fragment program and binding it.
 */
static void
update_vp( struct st_context *st )
{
   struct st_vertex_program *stvp;
   struct st_vp_variant_key key;

   /* find active shader and params -- Should be covered by
    * ST_NEW_VERTEX_PROGRAM
    */
   assert(st->ctx->VertexProgram._Current);
   stvp = st_vertex_program(st->ctx->VertexProgram._Current);
   assert(stvp->Base.Target == GL_VERTEX_PROGRAM_ARB);

   memset(&key, 0, sizeof key);
   key.st = st->has_shareable_shaders ? NULL : st;

   /* When this is true, we will add an extra input to the vertex
    * shader translation (for edgeflags), an extra output with
    * edgeflag semantics, and extend the vertex shader to pass through
    * the input to the output.  We'll need to use similar logic to set
    * up the extra vertex_element input for edgeflags.
    */
   key.passthrough_edgeflags = st->vertdata_edgeflags;

   key.clamp_color = st->clamp_vert_color_in_shader &&
                     st->ctx->Light._ClampVertexColor &&
                     (stvp->Base.info.outputs_written &
                      (VARYING_SLOT_COL0 |
                       VARYING_SLOT_COL1 |
                       VARYING_SLOT_BFC0 |
                       VARYING_SLOT_BFC1));

   st->vp_variant = st_get_vp_variant(st, stvp, &key);

   st_reference_vertprog(st, &st->vp, stvp);

   cso_set_vertex_shader_handle(st->cso_context, 
                                st->vp_variant->driver_shader);

   st->vertex_result_to_slot = stvp->result_to_output;
}


const struct st_tracked_state st_update_vp = {
   update_vp						/* update */
};



static void
update_gp( struct st_context *st )
{
   struct st_geometry_program *stgp;

   if (!st->ctx->GeometryProgram._Current) {
      cso_set_geometry_shader_handle(st->cso_context, NULL);
      st_reference_geomprog(st, &st->gp, NULL);
      return;
   }

   stgp = st_geometry_program(st->ctx->GeometryProgram._Current);
   assert(stgp->Base.Target == GL_GEOMETRY_PROGRAM_NV);

   st->gp_variant = st_get_basic_variant(st, PIPE_SHADER_GEOMETRY,
                                         &stgp->tgsi, &stgp->variants);

   st_reference_geomprog(st, &st->gp, stgp);

   cso_set_geometry_shader_handle(st->cso_context,
                                  st->gp_variant->driver_shader);
}

const struct st_tracked_state st_update_gp = {
   update_gp  				/* update */
};



static void
update_tcp( struct st_context *st )
{
   struct st_tessctrl_program *sttcp;

   if (!st->ctx->TessCtrlProgram._Current) {
      cso_set_tessctrl_shader_handle(st->cso_context, NULL);
      st_reference_tesscprog(st, &st->tcp, NULL);
      return;
   }

   sttcp = st_tessctrl_program(st->ctx->TessCtrlProgram._Current);
   assert(sttcp->Base.Target == GL_TESS_CONTROL_PROGRAM_NV);

   st->tcp_variant = st_get_basic_variant(st, PIPE_SHADER_TESS_CTRL,
                                          &sttcp->tgsi, &sttcp->variants);

   st_reference_tesscprog(st, &st->tcp, sttcp);

   cso_set_tessctrl_shader_handle(st->cso_context,
                                  st->tcp_variant->driver_shader);
}

const struct st_tracked_state st_update_tcp = {
   update_tcp  				/* update */
};



static void
update_tep( struct st_context *st )
{
   struct st_tesseval_program *sttep;

   if (!st->ctx->TessEvalProgram._Current) {
      cso_set_tesseval_shader_handle(st->cso_context, NULL);
      st_reference_tesseprog(st, &st->tep, NULL);
      return;
   }

   sttep = st_tesseval_program(st->ctx->TessEvalProgram._Current);
   assert(sttep->Base.Target == GL_TESS_EVALUATION_PROGRAM_NV);

   st->tep_variant = st_get_basic_variant(st, PIPE_SHADER_TESS_EVAL,
                                          &sttep->tgsi, &sttep->variants);

   st_reference_tesseprog(st, &st->tep, sttep);

   cso_set_tesseval_shader_handle(st->cso_context,
                                  st->tep_variant->driver_shader);
}

const struct st_tracked_state st_update_tep = {
   update_tep  				/* update */
};



static void
update_cp( struct st_context *st )
{
   struct st_compute_program *stcp;

   if (!st->ctx->ComputeProgram._Current) {
      cso_set_compute_shader_handle(st->cso_context, NULL);
      st_reference_compprog(st, &st->cp, NULL);
      return;
   }

   stcp = st_compute_program(st->ctx->ComputeProgram._Current);
   assert(stcp->Base.Target == GL_COMPUTE_PROGRAM_NV);

   st->cp_variant = st_get_cp_variant(st, &stcp->tgsi, &stcp->variants);

   st_reference_compprog(st, &st->cp, stcp);

   cso_set_compute_shader_handle(st->cso_context,
                                 st->cp_variant->driver_shader);
}

const struct st_tracked_state st_update_cp = {
   update_cp  				/* update */
};
