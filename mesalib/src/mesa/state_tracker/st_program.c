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
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  *   Brian Paul
  */


#include "main/imports.h"
#include "main/hash.h"
#include "main/mtypes.h"
#include "program/prog_parameter.h"
#include "program/prog_print.h"
#include "program/programopt.h"

#include "compiler/nir/nir.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_shader_tokens.h"
#include "draw/draw_context.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_emulate.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_ureg.h"

#include "st_debug.h"
#include "st_cb_bitmap.h"
#include "st_cb_drawpixels.h"
#include "st_context.h"
#include "st_tgsi_lower_yuv.h"
#include "st_program.h"
#include "st_mesa_to_tgsi.h"
#include "st_atifs_to_tgsi.h"
#include "st_nir.h"
#include "cso_cache/cso_context.h"



/**
 * Delete a vertex program variant.  Note the caller must unlink
 * the variant from the linked list.
 */
static void
delete_vp_variant(struct st_context *st, struct st_vp_variant *vpv)
{
   if (vpv->driver_shader) 
      cso_delete_vertex_shader(st->cso_context, vpv->driver_shader);
      
   if (vpv->draw_shader)
      draw_delete_vertex_shader( st->draw, vpv->draw_shader );

   if (((vpv->tgsi.type == PIPE_SHADER_IR_TGSI)) && vpv->tgsi.tokens)
      ureg_free_tokens(vpv->tgsi.tokens);

   free( vpv );
}



/**
 * Clean out any old compilations:
 */
void
st_release_vp_variants( struct st_context *st,
                        struct st_vertex_program *stvp )
{
   struct st_vp_variant *vpv;

   for (vpv = stvp->variants; vpv; ) {
      struct st_vp_variant *next = vpv->next;
      delete_vp_variant(st, vpv);
      vpv = next;
   }

   stvp->variants = NULL;

   if ((stvp->tgsi.type == PIPE_SHADER_IR_TGSI) && stvp->tgsi.tokens) {
      tgsi_free_tokens(stvp->tgsi.tokens);
      stvp->tgsi.tokens = NULL;
   }
}



/**
 * Delete a fragment program variant.  Note the caller must unlink
 * the variant from the linked list.
 */
static void
delete_fp_variant(struct st_context *st, struct st_fp_variant *fpv)
{
   if (fpv->driver_shader) 
      cso_delete_fragment_shader(st->cso_context, fpv->driver_shader);
   free(fpv);
}


/**
 * Free all variants of a fragment program.
 */
void
st_release_fp_variants(struct st_context *st, struct st_fragment_program *stfp)
{
   struct st_fp_variant *fpv;

   for (fpv = stfp->variants; fpv; ) {
      struct st_fp_variant *next = fpv->next;
      delete_fp_variant(st, fpv);
      fpv = next;
   }

   stfp->variants = NULL;

   if ((stfp->tgsi.type == PIPE_SHADER_IR_TGSI) && stfp->tgsi.tokens) {
      ureg_free_tokens(stfp->tgsi.tokens);
      stfp->tgsi.tokens = NULL;
   }
}


/**
 * Delete a basic program variant.  Note the caller must unlink
 * the variant from the linked list.
 */
static void
delete_basic_variant(struct st_context *st, struct st_basic_variant *v,
                     GLenum target)
{
   if (v->driver_shader) {
      switch (target) {
      case GL_TESS_CONTROL_PROGRAM_NV:
         cso_delete_tessctrl_shader(st->cso_context, v->driver_shader);
         break;
      case GL_TESS_EVALUATION_PROGRAM_NV:
         cso_delete_tesseval_shader(st->cso_context, v->driver_shader);
         break;
      case GL_GEOMETRY_PROGRAM_NV:
         cso_delete_geometry_shader(st->cso_context, v->driver_shader);
         break;
      case GL_COMPUTE_PROGRAM_NV:
         cso_delete_compute_shader(st->cso_context, v->driver_shader);
         break;
      default:
         assert(!"this shouldn't occur");
      }
   }

   free(v);
}


/**
 * Free all basic program variants.
 */
void
st_release_basic_variants(struct st_context *st, GLenum target,
                          struct st_basic_variant **variants,
                          struct pipe_shader_state *tgsi)
{
   struct st_basic_variant *v;

   for (v = *variants; v; ) {
      struct st_basic_variant *next = v->next;
      delete_basic_variant(st, v, target);
      v = next;
   }

   *variants = NULL;

   if (tgsi->tokens) {
      ureg_free_tokens(tgsi->tokens);
      tgsi->tokens = NULL;
   }
}


/**
 * Free all variants of a compute program.
 */
void
st_release_cp_variants(struct st_context *st, struct st_compute_program *stcp)
{
   struct st_basic_variant **variants = &stcp->variants;
   struct st_basic_variant *v;

   for (v = *variants; v; ) {
      struct st_basic_variant *next = v->next;
      delete_basic_variant(st, v, stcp->Base.Base.Target);
      v = next;
   }

   *variants = NULL;

   if (stcp->tgsi.prog) {
      ureg_free_tokens(stcp->tgsi.prog);
      stcp->tgsi.prog = NULL;
   }
}


/**
 * Translate a vertex program.
 */
bool
st_translate_vertex_program(struct st_context *st,
                            struct st_vertex_program *stvp)
{
   struct ureg_program *ureg;
   enum pipe_error error;
   unsigned num_outputs = 0;
   unsigned attr;
   unsigned input_to_index[VERT_ATTRIB_MAX] = {0};
   unsigned output_slot_to_attr[VARYING_SLOT_MAX] = {0};
   ubyte output_semantic_name[VARYING_SLOT_MAX] = {0};
   ubyte output_semantic_index[VARYING_SLOT_MAX] = {0};

   stvp->num_inputs = 0;

   if (stvp->Base.IsPositionInvariant)
      _mesa_insert_mvp_code(st->ctx, &stvp->Base);

   /*
    * Determine number of inputs, the mappings between VERT_ATTRIB_x
    * and TGSI generic input indexes, plus input attrib semantic info.
    */
   for (attr = 0; attr < VERT_ATTRIB_MAX; attr++) {
      if ((stvp->Base.Base.InputsRead & BITFIELD64_BIT(attr)) != 0) {
         input_to_index[attr] = stvp->num_inputs;
         stvp->index_to_input[stvp->num_inputs] = attr;
         stvp->num_inputs++;
         if ((stvp->Base.Base.DoubleInputsRead & BITFIELD64_BIT(attr)) != 0) {
            /* add placeholder for second part of a double attribute */
            stvp->index_to_input[stvp->num_inputs] = ST_DOUBLE_ATTRIB_PLACEHOLDER;
            stvp->num_inputs++;
         }
      }
   }
   /* bit of a hack, presetup potentially unused edgeflag input */
   input_to_index[VERT_ATTRIB_EDGEFLAG] = stvp->num_inputs;
   stvp->index_to_input[stvp->num_inputs] = VERT_ATTRIB_EDGEFLAG;

   /* Compute mapping of vertex program outputs to slots.
    */
   for (attr = 0; attr < VARYING_SLOT_MAX; attr++) {
      if ((stvp->Base.Base.OutputsWritten & BITFIELD64_BIT(attr)) == 0) {
         stvp->result_to_output[attr] = ~0;
      }
      else {
         unsigned slot = num_outputs++;

         stvp->result_to_output[attr] = slot;
         output_slot_to_attr[slot] = attr;

         switch (attr) {
         case VARYING_SLOT_POS:
            output_semantic_name[slot] = TGSI_SEMANTIC_POSITION;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_COL0:
            output_semantic_name[slot] = TGSI_SEMANTIC_COLOR;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_COL1:
            output_semantic_name[slot] = TGSI_SEMANTIC_COLOR;
            output_semantic_index[slot] = 1;
            break;
         case VARYING_SLOT_BFC0:
            output_semantic_name[slot] = TGSI_SEMANTIC_BCOLOR;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_BFC1:
            output_semantic_name[slot] = TGSI_SEMANTIC_BCOLOR;
            output_semantic_index[slot] = 1;
            break;
         case VARYING_SLOT_FOGC:
            output_semantic_name[slot] = TGSI_SEMANTIC_FOG;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_PSIZ:
            output_semantic_name[slot] = TGSI_SEMANTIC_PSIZE;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_CLIP_DIST0:
            output_semantic_name[slot] = TGSI_SEMANTIC_CLIPDIST;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_CLIP_DIST1:
            output_semantic_name[slot] = TGSI_SEMANTIC_CLIPDIST;
            output_semantic_index[slot] = 1;
            break;
         case VARYING_SLOT_CULL_DIST0:
         case VARYING_SLOT_CULL_DIST1:
            /* these should have been lowered by GLSL */
            assert(0);
            break;
         case VARYING_SLOT_EDGE:
            assert(0);
            break;
         case VARYING_SLOT_CLIP_VERTEX:
            output_semantic_name[slot] = TGSI_SEMANTIC_CLIPVERTEX;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_LAYER:
            output_semantic_name[slot] = TGSI_SEMANTIC_LAYER;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_VIEWPORT:
            output_semantic_name[slot] = TGSI_SEMANTIC_VIEWPORT_INDEX;
            output_semantic_index[slot] = 0;
            break;

         case VARYING_SLOT_TEX0:
         case VARYING_SLOT_TEX1:
         case VARYING_SLOT_TEX2:
         case VARYING_SLOT_TEX3:
         case VARYING_SLOT_TEX4:
         case VARYING_SLOT_TEX5:
         case VARYING_SLOT_TEX6:
         case VARYING_SLOT_TEX7:
            if (st->needs_texcoord_semantic) {
               output_semantic_name[slot] = TGSI_SEMANTIC_TEXCOORD;
               output_semantic_index[slot] = attr - VARYING_SLOT_TEX0;
               break;
            }
            /* fall through */
         case VARYING_SLOT_VAR0:
         default:
            assert(attr >= VARYING_SLOT_VAR0 ||
                   (attr >= VARYING_SLOT_TEX0 && attr <= VARYING_SLOT_TEX7));
            output_semantic_name[slot] = TGSI_SEMANTIC_GENERIC;
            output_semantic_index[slot] =
               st_get_generic_varying_index(st, attr);
            break;
         }
      }
   }
   /* similar hack to above, presetup potentially unused edgeflag output */
   stvp->result_to_output[VARYING_SLOT_EDGE] = num_outputs;
   output_semantic_name[num_outputs] = TGSI_SEMANTIC_EDGEFLAG;
   output_semantic_index[num_outputs] = 0;

   /* ARB_vp: */
   if (!stvp->glsl_to_tgsi && !stvp->shader_program) {
      _mesa_remove_output_reads(&stvp->Base.Base, PROGRAM_OUTPUT);

      /* This determines which states will be updated when the assembly
       * shader is bound.
       */
      stvp->affected_states = ST_NEW_VS_STATE |
                              ST_NEW_RASTERIZER |
                              ST_NEW_VERTEX_ARRAYS;

      if (stvp->Base.Base.Parameters->NumParameters)
         stvp->affected_states |= ST_NEW_VS_CONSTANTS;

      /* No samplers are allowed in ARB_vp. */
   }

   if (stvp->shader_program) {
      nir_shader *nir = st_glsl_to_nir(st, &stvp->Base.Base,
                                       stvp->shader_program,
                                       MESA_SHADER_VERTEX);

      stvp->tgsi.type = PIPE_SHADER_IR_NIR;
      stvp->tgsi.ir.nir = nir;

      st_translate_stream_output_info2(&stvp->shader_program->LinkedTransformFeedback,
                                       stvp->result_to_output,
                                       &stvp->tgsi.stream_output);
      return true;
   }

   ureg = ureg_create_with_screen(PIPE_SHADER_VERTEX, st->pipe->screen);
   if (ureg == NULL)
      return false;

   if (stvp->Base.Base.ClipDistanceArraySize)
      ureg_property(ureg, TGSI_PROPERTY_NUM_CLIPDIST_ENABLED,
                    stvp->Base.Base.ClipDistanceArraySize);
   if (stvp->Base.Base.CullDistanceArraySize)
      ureg_property(ureg, TGSI_PROPERTY_NUM_CULLDIST_ENABLED,
                    stvp->Base.Base.CullDistanceArraySize);

   if (ST_DEBUG & DEBUG_MESA) {
      _mesa_print_program(&stvp->Base.Base);
      _mesa_print_program_parameters(st->ctx, &stvp->Base.Base);
      debug_printf("\n");
   }

   if (stvp->glsl_to_tgsi) {
      error = st_translate_program(st->ctx,
                                   PIPE_SHADER_VERTEX,
                                   ureg,
                                   stvp->glsl_to_tgsi,
                                   &stvp->Base.Base,
                                   /* inputs */
                                   stvp->num_inputs,
                                   input_to_index,
                                   NULL, /* inputSlotToAttr */
                                   NULL, /* input semantic name */
                                   NULL, /* input semantic index */
                                   NULL, /* interp mode */
                                   NULL, /* interp location */
                                   /* outputs */
                                   num_outputs,
                                   stvp->result_to_output,
                                   output_slot_to_attr,
                                   output_semantic_name,
                                   output_semantic_index);

      st_translate_stream_output_info(stvp->glsl_to_tgsi,
                                      stvp->result_to_output,
                                      &stvp->tgsi.stream_output);

      free_glsl_to_tgsi_visitor(stvp->glsl_to_tgsi);
      stvp->glsl_to_tgsi = NULL;
   } else
      error = st_translate_mesa_program(st->ctx,
                                        PIPE_SHADER_VERTEX,
                                        ureg,
                                        &stvp->Base.Base,
                                        /* inputs */
                                        stvp->num_inputs,
                                        input_to_index,
                                        NULL, /* input semantic name */
                                        NULL, /* input semantic index */
                                        NULL,
                                        /* outputs */
                                        num_outputs,
                                        stvp->result_to_output,
                                        output_semantic_name,
                                        output_semantic_index);

   if (error) {
      debug_printf("%s: failed to translate Mesa program:\n", __func__);
      _mesa_print_program(&stvp->Base.Base);
      debug_assert(0);
      return false;
   }

   stvp->tgsi.tokens = ureg_get_tokens(ureg, NULL);
   ureg_destroy(ureg);
   return stvp->tgsi.tokens != NULL;
}

static struct st_vp_variant *
st_create_vp_variant(struct st_context *st,
                     struct st_vertex_program *stvp,
                     const struct st_vp_variant_key *key)
{
   struct st_vp_variant *vpv = CALLOC_STRUCT(st_vp_variant);
   struct pipe_context *pipe = st->pipe;

   vpv->key = *key;
   vpv->tgsi.stream_output = stvp->tgsi.stream_output;
   vpv->num_inputs = stvp->num_inputs;

   if (stvp->tgsi.type == PIPE_SHADER_IR_NIR) {
      vpv->tgsi.type = PIPE_SHADER_IR_NIR;
      vpv->tgsi.ir.nir = nir_shader_clone(NULL, stvp->tgsi.ir.nir);
      if (key->clamp_color)
         NIR_PASS_V(vpv->tgsi.ir.nir, nir_lower_clamp_color_outputs);
      if (key->passthrough_edgeflags)
         NIR_PASS_V(vpv->tgsi.ir.nir, nir_lower_passthrough_edgeflags);

      st_finalize_nir(st, &stvp->Base.Base, vpv->tgsi.ir.nir);

      vpv->driver_shader = pipe->create_vs_state(pipe, &vpv->tgsi);
      /* driver takes ownership of IR: */
      vpv->tgsi.ir.nir = NULL;
      return vpv;
   }

   vpv->tgsi.tokens = tgsi_dup_tokens(stvp->tgsi.tokens);

   /* Emulate features. */
   if (key->clamp_color || key->passthrough_edgeflags) {
      const struct tgsi_token *tokens;
      unsigned flags =
         (key->clamp_color ? TGSI_EMU_CLAMP_COLOR_OUTPUTS : 0) |
         (key->passthrough_edgeflags ? TGSI_EMU_PASSTHROUGH_EDGEFLAG : 0);

      tokens = tgsi_emulate(vpv->tgsi.tokens, flags);

      if (tokens) {
         tgsi_free_tokens(vpv->tgsi.tokens);
         vpv->tgsi.tokens = tokens;

         if (key->passthrough_edgeflags)
            vpv->num_inputs++;
      } else
         fprintf(stderr, "mesa: cannot emulate deprecated features\n");
   }

   if (ST_DEBUG & DEBUG_TGSI) {
      tgsi_dump(vpv->tgsi.tokens, 0);
      debug_printf("\n");
   }

   vpv->driver_shader = pipe->create_vs_state(pipe, &vpv->tgsi);
   return vpv;
}


/**
 * Find/create a vertex program variant.
 */
struct st_vp_variant *
st_get_vp_variant(struct st_context *st,
                  struct st_vertex_program *stvp,
                  const struct st_vp_variant_key *key)
{
   struct st_vp_variant *vpv;

   /* Search for existing variant */
   for (vpv = stvp->variants; vpv; vpv = vpv->next) {
      if (memcmp(&vpv->key, key, sizeof(*key)) == 0) {
         break;
      }
   }

   if (!vpv) {
      /* create now */
      vpv = st_create_vp_variant(st, stvp, key);
      if (vpv) {
         /* insert into list */
         vpv->next = stvp->variants;
         stvp->variants = vpv;
      }
   }

   return vpv;
}


static unsigned
st_translate_interp(enum glsl_interp_mode glsl_qual, bool is_color)
{
   switch (glsl_qual) {
   case INTERP_MODE_NONE:
      if (is_color)
         return TGSI_INTERPOLATE_COLOR;
      return TGSI_INTERPOLATE_PERSPECTIVE;
   case INTERP_MODE_SMOOTH:
      return TGSI_INTERPOLATE_PERSPECTIVE;
   case INTERP_MODE_FLAT:
      return TGSI_INTERPOLATE_CONSTANT;
   case INTERP_MODE_NOPERSPECTIVE:
      return TGSI_INTERPOLATE_LINEAR;
   default:
      assert(0 && "unexpected interp mode in st_translate_interp()");
      return TGSI_INTERPOLATE_PERSPECTIVE;
   }
}


/**
 * Translate a Mesa fragment shader into a TGSI shader.
 */
bool
st_translate_fragment_program(struct st_context *st,
                              struct st_fragment_program *stfp)
{
   GLuint outputMapping[2 * FRAG_RESULT_MAX];
   GLuint inputMapping[VARYING_SLOT_MAX];
   GLuint inputSlotToAttr[VARYING_SLOT_MAX];
   GLuint interpMode[PIPE_MAX_SHADER_INPUTS];  /* XXX size? */
   GLuint interpLocation[PIPE_MAX_SHADER_INPUTS];
   GLuint attr;
   GLbitfield64 inputsRead;
   struct ureg_program *ureg;

   GLboolean write_all = GL_FALSE;

   ubyte input_semantic_name[PIPE_MAX_SHADER_INPUTS];
   ubyte input_semantic_index[PIPE_MAX_SHADER_INPUTS];
   uint fs_num_inputs = 0;

   ubyte fs_output_semantic_name[PIPE_MAX_SHADER_OUTPUTS];
   ubyte fs_output_semantic_index[PIPE_MAX_SHADER_OUTPUTS];
   uint fs_num_outputs = 0;

   memset(inputSlotToAttr, ~0, sizeof(inputSlotToAttr));

   /* Non-GLSL programs: */
   if (!stfp->glsl_to_tgsi && !stfp->shader_program) {
      _mesa_remove_output_reads(&stfp->Base.Base, PROGRAM_OUTPUT);
      if (st->ctx->Const.GLSLFragCoordIsSysVal)
         _mesa_program_fragment_position_to_sysval(&stfp->Base.Base);

      /* This determines which states will be updated when the assembly
       * shader is bound.
       *
       * fragment.position and glDrawPixels always use constants.
       */
      stfp->affected_states = ST_NEW_FS_STATE |
                              ST_NEW_SAMPLE_SHADING |
                              ST_NEW_FS_CONSTANTS;

      if (stfp->ati_fs) {
         /* Just set them for ATI_fs unconditionally. */
         stfp->affected_states |= ST_NEW_FS_SAMPLER_VIEWS |
                                  ST_NEW_RENDER_SAMPLERS;
      } else {
         /* ARB_fp */
         if (stfp->Base.Base.SamplersUsed)
            stfp->affected_states |= ST_NEW_FS_SAMPLER_VIEWS |
                                     ST_NEW_RENDER_SAMPLERS;
      }
   }

   /*
    * Convert Mesa program inputs to TGSI input register semantics.
    */
   inputsRead = stfp->Base.Base.InputsRead;
   for (attr = 0; attr < VARYING_SLOT_MAX; attr++) {
      if ((inputsRead & BITFIELD64_BIT(attr)) != 0) {
         const GLuint slot = fs_num_inputs++;

         inputMapping[attr] = slot;
         inputSlotToAttr[slot] = attr;
         if (stfp->Base.IsCentroid & BITFIELD64_BIT(attr))
            interpLocation[slot] = TGSI_INTERPOLATE_LOC_CENTROID;
         else if (stfp->Base.IsSample & BITFIELD64_BIT(attr))
            interpLocation[slot] = TGSI_INTERPOLATE_LOC_SAMPLE;
         else
            interpLocation[slot] = TGSI_INTERPOLATE_LOC_CENTER;

         switch (attr) {
         case VARYING_SLOT_POS:
            input_semantic_name[slot] = TGSI_SEMANTIC_POSITION;
            input_semantic_index[slot] = 0;
            interpMode[slot] = TGSI_INTERPOLATE_LINEAR;
            break;
         case VARYING_SLOT_COL0:
            input_semantic_name[slot] = TGSI_SEMANTIC_COLOR;
            input_semantic_index[slot] = 0;
            interpMode[slot] = st_translate_interp(stfp->Base.InterpQualifier[attr],
                                                   TRUE);
            break;
         case VARYING_SLOT_COL1:
            input_semantic_name[slot] = TGSI_SEMANTIC_COLOR;
            input_semantic_index[slot] = 1;
            interpMode[slot] = st_translate_interp(stfp->Base.InterpQualifier[attr],
                                                   TRUE);
            break;
         case VARYING_SLOT_FOGC:
            input_semantic_name[slot] = TGSI_SEMANTIC_FOG;
            input_semantic_index[slot] = 0;
            interpMode[slot] = TGSI_INTERPOLATE_PERSPECTIVE;
            break;
         case VARYING_SLOT_FACE:
            input_semantic_name[slot] = TGSI_SEMANTIC_FACE;
            input_semantic_index[slot] = 0;
            interpMode[slot] = TGSI_INTERPOLATE_CONSTANT;
            break;
         case VARYING_SLOT_PRIMITIVE_ID:
            input_semantic_name[slot] = TGSI_SEMANTIC_PRIMID;
            input_semantic_index[slot] = 0;
            interpMode[slot] = TGSI_INTERPOLATE_CONSTANT;
            break;
         case VARYING_SLOT_LAYER:
            input_semantic_name[slot] = TGSI_SEMANTIC_LAYER;
            input_semantic_index[slot] = 0;
            interpMode[slot] = TGSI_INTERPOLATE_CONSTANT;
            break;
         case VARYING_SLOT_VIEWPORT:
            input_semantic_name[slot] = TGSI_SEMANTIC_VIEWPORT_INDEX;
            input_semantic_index[slot] = 0;
            interpMode[slot] = TGSI_INTERPOLATE_CONSTANT;
            break;
         case VARYING_SLOT_CLIP_DIST0:
            input_semantic_name[slot] = TGSI_SEMANTIC_CLIPDIST;
            input_semantic_index[slot] = 0;
            interpMode[slot] = TGSI_INTERPOLATE_PERSPECTIVE;
            break;
         case VARYING_SLOT_CLIP_DIST1:
            input_semantic_name[slot] = TGSI_SEMANTIC_CLIPDIST;
            input_semantic_index[slot] = 1;
            interpMode[slot] = TGSI_INTERPOLATE_PERSPECTIVE;
            break;
         case VARYING_SLOT_CULL_DIST0:
         case VARYING_SLOT_CULL_DIST1:
            /* these should have been lowered by GLSL */
            assert(0);
            break;
            /* In most cases, there is nothing special about these
             * inputs, so adopt a convention to use the generic
             * semantic name and the mesa VARYING_SLOT_ number as the
             * index.
             *
             * All that is required is that the vertex shader labels
             * its own outputs similarly, and that the vertex shader
             * generates at least every output required by the
             * fragment shader plus fixed-function hardware (such as
             * BFC).
             *
             * However, some drivers may need us to identify the PNTC and TEXi
             * varyings if, for example, their capability to replace them with
             * sprite coordinates is limited.
             */
         case VARYING_SLOT_PNTC:
            if (st->needs_texcoord_semantic) {
               input_semantic_name[slot] = TGSI_SEMANTIC_PCOORD;
               input_semantic_index[slot] = 0;
               interpMode[slot] = TGSI_INTERPOLATE_LINEAR;
               break;
            }
            /* fall through */
         case VARYING_SLOT_TEX0:
         case VARYING_SLOT_TEX1:
         case VARYING_SLOT_TEX2:
         case VARYING_SLOT_TEX3:
         case VARYING_SLOT_TEX4:
         case VARYING_SLOT_TEX5:
         case VARYING_SLOT_TEX6:
         case VARYING_SLOT_TEX7:
            if (st->needs_texcoord_semantic) {
               input_semantic_name[slot] = TGSI_SEMANTIC_TEXCOORD;
               input_semantic_index[slot] = attr - VARYING_SLOT_TEX0;
               interpMode[slot] =
                  st_translate_interp(stfp->Base.InterpQualifier[attr], FALSE);
               break;
            }
            /* fall through */
         case VARYING_SLOT_VAR0:
         default:
            /* Semantic indices should be zero-based because drivers may choose
             * to assign a fixed slot determined by that index.
             * This is useful because ARB_separate_shader_objects uses location
             * qualifiers for linkage, and if the semantic index corresponds to
             * these locations, linkage passes in the driver become unecessary.
             *
             * If needs_texcoord_semantic is true, no semantic indices will be
             * consumed for the TEXi varyings, and we can base the locations of
             * the user varyings on VAR0.  Otherwise, we use TEX0 as base index.
             */
            assert(attr >= VARYING_SLOT_VAR0 || attr == VARYING_SLOT_PNTC ||
                   (attr >= VARYING_SLOT_TEX0 && attr <= VARYING_SLOT_TEX7));
            input_semantic_name[slot] = TGSI_SEMANTIC_GENERIC;
            input_semantic_index[slot] = st_get_generic_varying_index(st, attr);
            if (attr == VARYING_SLOT_PNTC)
               interpMode[slot] = TGSI_INTERPOLATE_LINEAR;
            else
               interpMode[slot] = st_translate_interp(stfp->Base.InterpQualifier[attr],
                                                      FALSE);
            break;
         }
      }
      else {
         inputMapping[attr] = -1;
      }
   }

   /*
    * Semantics and mapping for outputs
    */
   {
      uint numColors = 0;
      GLbitfield64 outputsWritten = stfp->Base.Base.OutputsWritten;

      /* if z is written, emit that first */
      if (outputsWritten & BITFIELD64_BIT(FRAG_RESULT_DEPTH)) {
         fs_output_semantic_name[fs_num_outputs] = TGSI_SEMANTIC_POSITION;
         fs_output_semantic_index[fs_num_outputs] = 0;
         outputMapping[FRAG_RESULT_DEPTH] = fs_num_outputs;
         fs_num_outputs++;
         outputsWritten &= ~(1 << FRAG_RESULT_DEPTH);
      }

      if (outputsWritten & BITFIELD64_BIT(FRAG_RESULT_STENCIL)) {
         fs_output_semantic_name[fs_num_outputs] = TGSI_SEMANTIC_STENCIL;
         fs_output_semantic_index[fs_num_outputs] = 0;
         outputMapping[FRAG_RESULT_STENCIL] = fs_num_outputs;
         fs_num_outputs++;
         outputsWritten &= ~(1 << FRAG_RESULT_STENCIL);
      }

      if (outputsWritten & BITFIELD64_BIT(FRAG_RESULT_SAMPLE_MASK)) {
         fs_output_semantic_name[fs_num_outputs] = TGSI_SEMANTIC_SAMPLEMASK;
         fs_output_semantic_index[fs_num_outputs] = 0;
         outputMapping[FRAG_RESULT_SAMPLE_MASK] = fs_num_outputs;
         fs_num_outputs++;
         outputsWritten &= ~(1 << FRAG_RESULT_SAMPLE_MASK);
      }

      /* handle remaining outputs (color) */
      for (attr = 0; attr < ARRAY_SIZE(outputMapping); attr++) {
         const GLbitfield64 written = attr < FRAG_RESULT_MAX ? outputsWritten :
            stfp->Base.Base.SecondaryOutputsWritten;
         const unsigned loc = attr % FRAG_RESULT_MAX;

         if (written & BITFIELD64_BIT(loc)) {
            switch (loc) {
            case FRAG_RESULT_DEPTH:
            case FRAG_RESULT_STENCIL:
            case FRAG_RESULT_SAMPLE_MASK:
               /* handled above */
               assert(0);
               break;
            case FRAG_RESULT_COLOR:
               write_all = GL_TRUE; /* fallthrough */
            default:
               assert(loc == FRAG_RESULT_COLOR ||
                      (FRAG_RESULT_DATA0 <= loc && loc < FRAG_RESULT_MAX));
               fs_output_semantic_name[fs_num_outputs] = TGSI_SEMANTIC_COLOR;
               fs_output_semantic_index[fs_num_outputs] = numColors;
               outputMapping[attr] = fs_num_outputs;
               numColors++;
               break;
            }

            fs_num_outputs++;
         }
      }
   }

   if (stfp->shader_program) {
      nir_shader *nir = st_glsl_to_nir(st, &stfp->Base.Base,
                                       stfp->shader_program,
                                       MESA_SHADER_FRAGMENT);

      stfp->tgsi.type = PIPE_SHADER_IR_NIR;
      stfp->tgsi.ir.nir = nir;

      return true;
   }

   ureg = ureg_create_with_screen(PIPE_SHADER_FRAGMENT, st->pipe->screen);
   if (ureg == NULL)
      return false;

   if (ST_DEBUG & DEBUG_MESA) {
      _mesa_print_program(&stfp->Base.Base);
      _mesa_print_program_parameters(st->ctx, &stfp->Base.Base);
      debug_printf("\n");
   }
   if (write_all == GL_TRUE)
      ureg_property(ureg, TGSI_PROPERTY_FS_COLOR0_WRITES_ALL_CBUFS, 1);

   if (stfp->Base.FragDepthLayout != FRAG_DEPTH_LAYOUT_NONE) {
      switch (stfp->Base.FragDepthLayout) {
      case FRAG_DEPTH_LAYOUT_ANY:
         ureg_property(ureg, TGSI_PROPERTY_FS_DEPTH_LAYOUT,
                       TGSI_FS_DEPTH_LAYOUT_ANY);
         break;
      case FRAG_DEPTH_LAYOUT_GREATER:
         ureg_property(ureg, TGSI_PROPERTY_FS_DEPTH_LAYOUT,
                       TGSI_FS_DEPTH_LAYOUT_GREATER);
         break;
      case FRAG_DEPTH_LAYOUT_LESS:
         ureg_property(ureg, TGSI_PROPERTY_FS_DEPTH_LAYOUT,
                       TGSI_FS_DEPTH_LAYOUT_LESS);
         break;
      case FRAG_DEPTH_LAYOUT_UNCHANGED:
         ureg_property(ureg, TGSI_PROPERTY_FS_DEPTH_LAYOUT,
                       TGSI_FS_DEPTH_LAYOUT_UNCHANGED);
         break;
      default:
         assert(0);
      }
   }

   if (stfp->glsl_to_tgsi) {
      st_translate_program(st->ctx,
                           PIPE_SHADER_FRAGMENT,
                           ureg,
                           stfp->glsl_to_tgsi,
                           &stfp->Base.Base,
                           /* inputs */
                           fs_num_inputs,
                           inputMapping,
                           inputSlotToAttr,
                           input_semantic_name,
                           input_semantic_index,
                           interpMode,
                           interpLocation,
                           /* outputs */
                           fs_num_outputs,
                           outputMapping,
                           NULL,
                           fs_output_semantic_name,
                           fs_output_semantic_index);

      free_glsl_to_tgsi_visitor(stfp->glsl_to_tgsi);
      stfp->glsl_to_tgsi = NULL;
   } else if (stfp->ati_fs)
      st_translate_atifs_program(ureg,
                                 stfp->ati_fs,
                                 &stfp->Base.Base,
                                 /* inputs */
                                 fs_num_inputs,
                                 inputMapping,
                                 input_semantic_name,
                                 input_semantic_index,
                                 interpMode,
                                 /* outputs */
                                 fs_num_outputs,
                                 outputMapping,
                                 fs_output_semantic_name,
                                 fs_output_semantic_index);
   else
      st_translate_mesa_program(st->ctx,
                                PIPE_SHADER_FRAGMENT,
                                ureg,
                                &stfp->Base.Base,
                                /* inputs */
                                fs_num_inputs,
                                inputMapping,
                                input_semantic_name,
                                input_semantic_index,
                                interpMode,
                                /* outputs */
                                fs_num_outputs,
                                outputMapping,
                                fs_output_semantic_name,
                                fs_output_semantic_index);

   stfp->tgsi.tokens = ureg_get_tokens(ureg, NULL);
   ureg_destroy(ureg);
   return stfp->tgsi.tokens != NULL;
}

static struct st_fp_variant *
st_create_fp_variant(struct st_context *st,
                     struct st_fragment_program *stfp,
                     const struct st_fp_variant_key *key)
{
   struct pipe_context *pipe = st->pipe;
   struct st_fp_variant *variant = CALLOC_STRUCT(st_fp_variant);
   struct pipe_shader_state tgsi = {0};
   struct gl_program_parameter_list *params = stfp->Base.Base.Parameters;
   static const gl_state_index texcoord_state[STATE_LENGTH] =
      { STATE_INTERNAL, STATE_CURRENT_ATTRIB, VERT_ATTRIB_TEX0 };
   static const gl_state_index scale_state[STATE_LENGTH] =
      { STATE_INTERNAL, STATE_PT_SCALE };
   static const gl_state_index bias_state[STATE_LENGTH] =
      { STATE_INTERNAL, STATE_PT_BIAS };

   if (!variant)
      return NULL;

   if (stfp->tgsi.type == PIPE_SHADER_IR_NIR) {
      tgsi.type = PIPE_SHADER_IR_NIR;
      tgsi.ir.nir = nir_shader_clone(NULL, stfp->tgsi.ir.nir);

      if (key->clamp_color)
         NIR_PASS_V(tgsi.ir.nir, nir_lower_clamp_color_outputs);

      if (key->persample_shading) {
          nir_shader *shader = tgsi.ir.nir;
          nir_foreach_variable(var, &shader->inputs)
             var->data.sample = true;
      }

      assert(!(key->bitmap && key->drawpixels));

      /* glBitmap */
      if (key->bitmap) {
         nir_lower_bitmap_options options = {0};

         variant->bitmap_sampler = ffs(~stfp->Base.Base.SamplersUsed) - 1;
         options.sampler = variant->bitmap_sampler;
         options.swizzle_xxxx = (st->bitmap.tex_format == PIPE_FORMAT_L8_UNORM);

         NIR_PASS_V(tgsi.ir.nir, nir_lower_bitmap, &options);
      }

      /* glDrawPixels (color only) */
      if (key->drawpixels) {
         nir_lower_drawpixels_options options = {{0}};
         unsigned samplers_used = stfp->Base.Base.SamplersUsed;

         /* Find the first unused slot. */
         variant->drawpix_sampler = ffs(~samplers_used) - 1;
         options.drawpix_sampler = variant->drawpix_sampler;
         samplers_used |= (1 << variant->drawpix_sampler);

         options.pixel_maps = key->pixelMaps;
         if (key->pixelMaps) {
            variant->pixelmap_sampler = ffs(~samplers_used) - 1;
            options.pixelmap_sampler = variant->pixelmap_sampler;
         }

         options.scale_and_bias = key->scaleAndBias;
         if (key->scaleAndBias) {
            _mesa_add_state_reference(params, scale_state);
            memcpy(options.scale_state_tokens, scale_state,
                   sizeof(options.scale_state_tokens));
            _mesa_add_state_reference(params, bias_state);
            memcpy(options.bias_state_tokens, bias_state,
                   sizeof(options.bias_state_tokens));
         }

         _mesa_add_state_reference(params, texcoord_state);
         memcpy(options.texcoord_state_tokens, texcoord_state,
                sizeof(options.texcoord_state_tokens));

         NIR_PASS_V(tgsi.ir.nir, nir_lower_drawpixels, &options);
      }

      if (unlikely(key->external.lower_nv12 || key->external.lower_iyuv)) {
         nir_lower_tex_options options = {0};
         options.lower_y_uv_external = key->external.lower_nv12;
         options.lower_y_u_v_external = key->external.lower_iyuv;
         NIR_PASS_V(tgsi.ir.nir, nir_lower_tex, &options);
      }

      st_finalize_nir(st, &stfp->Base.Base, tgsi.ir.nir);

      if (unlikely(key->external.lower_nv12 || key->external.lower_iyuv)) {
         /* This pass needs to happen *after* nir_lower_sampler */
         NIR_PASS_V(tgsi.ir.nir, st_nir_lower_tex_src_plane,
                    ~stfp->Base.Base.SamplersUsed,
                    key->external.lower_nv12,
                    key->external.lower_iyuv);
      }

      variant->driver_shader = pipe->create_fs_state(pipe, &tgsi);
      variant->key = *key;

      return variant;
   }

   tgsi.tokens = stfp->tgsi.tokens;

   assert(!(key->bitmap && key->drawpixels));

   /* Fix texture targets and add fog for ATI_fs */
   if (stfp->ati_fs) {
      const struct tgsi_token *tokens = st_fixup_atifs(tgsi.tokens, key);

      if (tokens)
         tgsi.tokens = tokens;
      else
         fprintf(stderr, "mesa: cannot post-process ATI_fs\n");
   }

   /* Emulate features. */
   if (key->clamp_color || key->persample_shading) {
      const struct tgsi_token *tokens;
      unsigned flags =
         (key->clamp_color ? TGSI_EMU_CLAMP_COLOR_OUTPUTS : 0) |
         (key->persample_shading ? TGSI_EMU_FORCE_PERSAMPLE_INTERP : 0);

      tokens = tgsi_emulate(tgsi.tokens, flags);

      if (tokens) {
         if (tgsi.tokens != stfp->tgsi.tokens)
            tgsi_free_tokens(tgsi.tokens);
         tgsi.tokens = tokens;
      } else
         fprintf(stderr, "mesa: cannot emulate deprecated features\n");
   }

   /* glBitmap */
   if (key->bitmap) {
      const struct tgsi_token *tokens;

      variant->bitmap_sampler = ffs(~stfp->Base.Base.SamplersUsed) - 1;

      tokens = st_get_bitmap_shader(tgsi.tokens,
                                    st->internal_target,
                                    variant->bitmap_sampler,
                                    st->needs_texcoord_semantic,
                                    st->bitmap.tex_format ==
                                    PIPE_FORMAT_L8_UNORM);

      if (tokens) {
         if (tgsi.tokens != stfp->tgsi.tokens)
            tgsi_free_tokens(tgsi.tokens);
         tgsi.tokens = tokens;
      } else
         fprintf(stderr, "mesa: cannot create a shader for glBitmap\n");
   }

   /* glDrawPixels (color only) */
   if (key->drawpixels) {
      const struct tgsi_token *tokens;
      unsigned scale_const = 0, bias_const = 0, texcoord_const = 0;

      /* Find the first unused slot. */
      variant->drawpix_sampler = ffs(~stfp->Base.Base.SamplersUsed) - 1;

      if (key->pixelMaps) {
         unsigned samplers_used = stfp->Base.Base.SamplersUsed |
                                  (1 << variant->drawpix_sampler);

         variant->pixelmap_sampler = ffs(~samplers_used) - 1;
      }

      if (key->scaleAndBias) {
         scale_const = _mesa_add_state_reference(params, scale_state);
         bias_const = _mesa_add_state_reference(params, bias_state);
      }

      texcoord_const = _mesa_add_state_reference(params, texcoord_state);

      tokens = st_get_drawpix_shader(tgsi.tokens,
                                     st->needs_texcoord_semantic,
                                     key->scaleAndBias, scale_const,
                                     bias_const, key->pixelMaps,
                                     variant->drawpix_sampler,
                                     variant->pixelmap_sampler,
                                     texcoord_const, st->internal_target);

      if (tokens) {
         if (tgsi.tokens != stfp->tgsi.tokens)
            tgsi_free_tokens(tgsi.tokens);
         tgsi.tokens = tokens;
      } else
         fprintf(stderr, "mesa: cannot create a shader for glDrawPixels\n");
   }

   if (unlikely(key->external.lower_nv12 || key->external.lower_iyuv)) {
      const struct tgsi_token *tokens;

      /* samplers inserted would conflict, but this should be unpossible: */
      assert(!(key->bitmap || key->drawpixels));

      tokens = st_tgsi_lower_yuv(tgsi.tokens,
                                 ~stfp->Base.Base.SamplersUsed,
                                 key->external.lower_nv12,
                                 key->external.lower_iyuv);
      if (tokens) {
         if (tgsi.tokens != stfp->tgsi.tokens)
            tgsi_free_tokens(tgsi.tokens);
         tgsi.tokens = tokens;
      } else {
         fprintf(stderr, "mesa: cannot create a shader for samplerExternalOES\n");
      }
   }

   if (ST_DEBUG & DEBUG_TGSI) {
      tgsi_dump(tgsi.tokens, 0);
      debug_printf("\n");
   }

   /* fill in variant */
   variant->driver_shader = pipe->create_fs_state(pipe, &tgsi);
   variant->key = *key;

   if (tgsi.tokens != stfp->tgsi.tokens)
      tgsi_free_tokens(tgsi.tokens);
   return variant;
}

/**
 * Translate fragment program if needed.
 */
struct st_fp_variant *
st_get_fp_variant(struct st_context *st,
                  struct st_fragment_program *stfp,
                  const struct st_fp_variant_key *key)
{
   struct st_fp_variant *fpv;

   /* Search for existing variant */
   for (fpv = stfp->variants; fpv; fpv = fpv->next) {
      if (memcmp(&fpv->key, key, sizeof(*key)) == 0) {
         break;
      }
   }

   if (!fpv) {
      /* create new */
      fpv = st_create_fp_variant(st, stfp, key);
      if (fpv) {
         /* insert into list */
         fpv->next = stfp->variants;
         stfp->variants = fpv;
      }
   }

   return fpv;
}


/**
 * Translate a program. This is common code for geometry and tessellation
 * shaders.
 */
static void
st_translate_program_common(struct st_context *st,
                            struct gl_program *prog,
                            struct glsl_to_tgsi_visitor *glsl_to_tgsi,
                            struct ureg_program *ureg,
                            unsigned tgsi_processor,
                            struct pipe_shader_state *out_state)
{
   GLuint inputSlotToAttr[VARYING_SLOT_TESS_MAX];
   GLuint inputMapping[VARYING_SLOT_TESS_MAX];
   GLuint outputSlotToAttr[VARYING_SLOT_TESS_MAX];
   GLuint outputMapping[VARYING_SLOT_TESS_MAX];
   GLuint attr;

   ubyte input_semantic_name[PIPE_MAX_SHADER_INPUTS];
   ubyte input_semantic_index[PIPE_MAX_SHADER_INPUTS];
   uint num_inputs = 0;

   ubyte output_semantic_name[PIPE_MAX_SHADER_OUTPUTS];
   ubyte output_semantic_index[PIPE_MAX_SHADER_OUTPUTS];
   uint num_outputs = 0;

   GLint i;

   memset(inputSlotToAttr, 0, sizeof(inputSlotToAttr));
   memset(inputMapping, 0, sizeof(inputMapping));
   memset(outputSlotToAttr, 0, sizeof(outputSlotToAttr));
   memset(outputMapping, 0, sizeof(outputMapping));
   memset(out_state, 0, sizeof(*out_state));

   if (prog->ClipDistanceArraySize)
      ureg_property(ureg, TGSI_PROPERTY_NUM_CLIPDIST_ENABLED,
                    prog->ClipDistanceArraySize);
   if (prog->CullDistanceArraySize)
      ureg_property(ureg, TGSI_PROPERTY_NUM_CULLDIST_ENABLED,
                    prog->CullDistanceArraySize);

   /*
    * Convert Mesa program inputs to TGSI input register semantics.
    */
   for (attr = 0; attr < VARYING_SLOT_MAX; attr++) {
      if ((prog->InputsRead & BITFIELD64_BIT(attr)) != 0) {
         const GLuint slot = num_inputs++;

         inputMapping[attr] = slot;
         inputSlotToAttr[slot] = attr;

         switch (attr) {
         case VARYING_SLOT_PRIMITIVE_ID:
            assert(tgsi_processor == PIPE_SHADER_GEOMETRY);
            input_semantic_name[slot] = TGSI_SEMANTIC_PRIMID;
            input_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_POS:
            input_semantic_name[slot] = TGSI_SEMANTIC_POSITION;
            input_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_COL0:
            input_semantic_name[slot] = TGSI_SEMANTIC_COLOR;
            input_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_COL1:
            input_semantic_name[slot] = TGSI_SEMANTIC_COLOR;
            input_semantic_index[slot] = 1;
            break;
         case VARYING_SLOT_FOGC:
            input_semantic_name[slot] = TGSI_SEMANTIC_FOG;
            input_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_CLIP_VERTEX:
            input_semantic_name[slot] = TGSI_SEMANTIC_CLIPVERTEX;
            input_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_CLIP_DIST0:
            input_semantic_name[slot] = TGSI_SEMANTIC_CLIPDIST;
            input_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_CLIP_DIST1:
            input_semantic_name[slot] = TGSI_SEMANTIC_CLIPDIST;
            input_semantic_index[slot] = 1;
            break;
         case VARYING_SLOT_CULL_DIST0:
         case VARYING_SLOT_CULL_DIST1:
            /* these should have been lowered by GLSL */
            assert(0);
            break;
         case VARYING_SLOT_PSIZ:
            input_semantic_name[slot] = TGSI_SEMANTIC_PSIZE;
            input_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_TEX0:
         case VARYING_SLOT_TEX1:
         case VARYING_SLOT_TEX2:
         case VARYING_SLOT_TEX3:
         case VARYING_SLOT_TEX4:
         case VARYING_SLOT_TEX5:
         case VARYING_SLOT_TEX6:
         case VARYING_SLOT_TEX7:
            if (st->needs_texcoord_semantic) {
               input_semantic_name[slot] = TGSI_SEMANTIC_TEXCOORD;
               input_semantic_index[slot] = attr - VARYING_SLOT_TEX0;
               break;
            }
            /* fall through */
         case VARYING_SLOT_VAR0:
         default:
            assert(attr >= VARYING_SLOT_VAR0 ||
                   (attr >= VARYING_SLOT_TEX0 && attr <= VARYING_SLOT_TEX7));
            input_semantic_name[slot] = TGSI_SEMANTIC_GENERIC;
            input_semantic_index[slot] =
               st_get_generic_varying_index(st, attr);
            break;
         }
      }
   }

   /* Also add patch inputs. */
   for (attr = 0; attr < 32; attr++) {
      if (prog->PatchInputsRead & (1u << attr)) {
         GLuint slot = num_inputs++;
         GLuint patch_attr = VARYING_SLOT_PATCH0 + attr;

         inputMapping[patch_attr] = slot;
         inputSlotToAttr[slot] = patch_attr;
         input_semantic_name[slot] = TGSI_SEMANTIC_PATCH;
         input_semantic_index[slot] = attr;
      }
   }

   /* initialize output semantics to defaults */
   for (i = 0; i < PIPE_MAX_SHADER_OUTPUTS; i++) {
      output_semantic_name[i] = TGSI_SEMANTIC_GENERIC;
      output_semantic_index[i] = 0;
   }

   /*
    * Determine number of outputs, the (default) output register
    * mapping and the semantic information for each output.
    */
   for (attr = 0; attr < VARYING_SLOT_MAX; attr++) {
      if (prog->OutputsWritten & BITFIELD64_BIT(attr)) {
         GLuint slot = num_outputs++;

         outputMapping[attr] = slot;
         outputSlotToAttr[slot] = attr;

         switch (attr) {
         case VARYING_SLOT_POS:
            assert(slot == 0);
            output_semantic_name[slot] = TGSI_SEMANTIC_POSITION;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_COL0:
            output_semantic_name[slot] = TGSI_SEMANTIC_COLOR;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_COL1:
            output_semantic_name[slot] = TGSI_SEMANTIC_COLOR;
            output_semantic_index[slot] = 1;
            break;
         case VARYING_SLOT_BFC0:
            output_semantic_name[slot] = TGSI_SEMANTIC_BCOLOR;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_BFC1:
            output_semantic_name[slot] = TGSI_SEMANTIC_BCOLOR;
            output_semantic_index[slot] = 1;
            break;
         case VARYING_SLOT_FOGC:
            output_semantic_name[slot] = TGSI_SEMANTIC_FOG;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_PSIZ:
            output_semantic_name[slot] = TGSI_SEMANTIC_PSIZE;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_CLIP_VERTEX:
            output_semantic_name[slot] = TGSI_SEMANTIC_CLIPVERTEX;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_CLIP_DIST0:
            output_semantic_name[slot] = TGSI_SEMANTIC_CLIPDIST;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_CLIP_DIST1:
            output_semantic_name[slot] = TGSI_SEMANTIC_CLIPDIST;
            output_semantic_index[slot] = 1;
            break;
         case VARYING_SLOT_CULL_DIST0:
         case VARYING_SLOT_CULL_DIST1:
            /* these should have been lowered by GLSL */
            assert(0);
            break;
         case VARYING_SLOT_LAYER:
            output_semantic_name[slot] = TGSI_SEMANTIC_LAYER;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_PRIMITIVE_ID:
            output_semantic_name[slot] = TGSI_SEMANTIC_PRIMID;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_VIEWPORT:
            output_semantic_name[slot] = TGSI_SEMANTIC_VIEWPORT_INDEX;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_TESS_LEVEL_OUTER:
            output_semantic_name[slot] = TGSI_SEMANTIC_TESSOUTER;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_TESS_LEVEL_INNER:
            output_semantic_name[slot] = TGSI_SEMANTIC_TESSINNER;
            output_semantic_index[slot] = 0;
            break;
         case VARYING_SLOT_TEX0:
         case VARYING_SLOT_TEX1:
         case VARYING_SLOT_TEX2:
         case VARYING_SLOT_TEX3:
         case VARYING_SLOT_TEX4:
         case VARYING_SLOT_TEX5:
         case VARYING_SLOT_TEX6:
         case VARYING_SLOT_TEX7:
            if (st->needs_texcoord_semantic) {
               output_semantic_name[slot] = TGSI_SEMANTIC_TEXCOORD;
               output_semantic_index[slot] = attr - VARYING_SLOT_TEX0;
               break;
            }
            /* fall through */
         case VARYING_SLOT_VAR0:
         default:
            assert(slot < ARRAY_SIZE(output_semantic_name));
            assert(attr >= VARYING_SLOT_VAR0 ||
                   (attr >= VARYING_SLOT_TEX0 && attr <= VARYING_SLOT_TEX7));
            output_semantic_name[slot] = TGSI_SEMANTIC_GENERIC;
            output_semantic_index[slot] =
               st_get_generic_varying_index(st, attr);
            break;
         }
      }
   }

   /* Also add patch outputs. */
   for (attr = 0; attr < 32; attr++) {
      if (prog->PatchOutputsWritten & (1u << attr)) {
         GLuint slot = num_outputs++;
         GLuint patch_attr = VARYING_SLOT_PATCH0 + attr;

         outputMapping[patch_attr] = slot;
         outputSlotToAttr[slot] = patch_attr;
         output_semantic_name[slot] = TGSI_SEMANTIC_PATCH;
         output_semantic_index[slot] = attr;
      }
   }

   st_translate_program(st->ctx,
                        tgsi_processor,
                        ureg,
                        glsl_to_tgsi,
                        prog,
                        /* inputs */
                        num_inputs,
                        inputMapping,
                        inputSlotToAttr,
                        input_semantic_name,
                        input_semantic_index,
                        NULL,
                        NULL,
                        /* outputs */
                        num_outputs,
                        outputMapping,
                        outputSlotToAttr,
                        output_semantic_name,
                        output_semantic_index);

   out_state->tokens = ureg_get_tokens(ureg, NULL);
   ureg_destroy(ureg);

   st_translate_stream_output_info(glsl_to_tgsi,
                                   outputMapping,
                                   &out_state->stream_output);

   if ((ST_DEBUG & DEBUG_TGSI) && (ST_DEBUG & DEBUG_MESA)) {
      _mesa_print_program(prog);
      debug_printf("\n");
   }

   if (ST_DEBUG & DEBUG_TGSI) {
      tgsi_dump(out_state->tokens, 0);
      debug_printf("\n");
   }
}


/**
 * Translate a geometry program to create a new variant.
 */
bool
st_translate_geometry_program(struct st_context *st,
                              struct st_geometry_program *stgp)
{
   struct ureg_program *ureg;

   ureg = ureg_create_with_screen(PIPE_SHADER_GEOMETRY, st->pipe->screen);
   if (ureg == NULL)
      return false;

   ureg_property(ureg, TGSI_PROPERTY_GS_INPUT_PRIM, stgp->Base.InputType);
   ureg_property(ureg, TGSI_PROPERTY_GS_OUTPUT_PRIM, stgp->Base.OutputType);
   ureg_property(ureg, TGSI_PROPERTY_GS_MAX_OUTPUT_VERTICES,
                 stgp->Base.VerticesOut);
   ureg_property(ureg, TGSI_PROPERTY_GS_INVOCATIONS, stgp->Base.Invocations);

   st_translate_program_common(st, &stgp->Base.Base, stgp->glsl_to_tgsi, ureg,
                               PIPE_SHADER_GEOMETRY, &stgp->tgsi);

   free_glsl_to_tgsi_visitor(stgp->glsl_to_tgsi);
   stgp->glsl_to_tgsi = NULL;
   return true;
}


/**
 * Get/create a basic program variant.
 */
struct st_basic_variant *
st_get_basic_variant(struct st_context *st,
                     unsigned pipe_shader,
                     struct pipe_shader_state *tgsi,
                     struct st_basic_variant **variants)
{
   struct pipe_context *pipe = st->pipe;
   struct st_basic_variant *v;
   struct st_basic_variant_key key;

   memset(&key, 0, sizeof(key));
   key.st = st->has_shareable_shaders ? NULL : st;

   /* Search for existing variant */
   for (v = *variants; v; v = v->next) {
      if (memcmp(&v->key, &key, sizeof(key)) == 0) {
         break;
      }
   }

   if (!v) {
      /* create new */
      v = CALLOC_STRUCT(st_basic_variant);
      if (v) {
         /* fill in new variant */
         switch (pipe_shader) {
         case PIPE_SHADER_TESS_CTRL:
            v->driver_shader = pipe->create_tcs_state(pipe, tgsi);
            break;
         case PIPE_SHADER_TESS_EVAL:
            v->driver_shader = pipe->create_tes_state(pipe, tgsi);
            break;
         case PIPE_SHADER_GEOMETRY:
            v->driver_shader = pipe->create_gs_state(pipe, tgsi);
            break;
         default:
            assert(!"unhandled shader type");
            free(v);
            return NULL;
         }

         v->key = key;

         /* insert into list */
         v->next = *variants;
         *variants = v;
      }
   }

   return v;
}


/**
 * Translate a tessellation control program to create a new variant.
 */
bool
st_translate_tessctrl_program(struct st_context *st,
                              struct st_tessctrl_program *sttcp)
{
   struct ureg_program *ureg;

   ureg = ureg_create_with_screen(PIPE_SHADER_TESS_CTRL, st->pipe->screen);
   if (ureg == NULL)
      return false;

   ureg_property(ureg, TGSI_PROPERTY_TCS_VERTICES_OUT,
                 sttcp->Base.VerticesOut);

   st_translate_program_common(st, &sttcp->Base.Base, sttcp->glsl_to_tgsi,
                               ureg, PIPE_SHADER_TESS_CTRL, &sttcp->tgsi);

   free_glsl_to_tgsi_visitor(sttcp->glsl_to_tgsi);
   sttcp->glsl_to_tgsi = NULL;
   return true;
}


/**
 * Translate a tessellation evaluation program to create a new variant.
 */
bool
st_translate_tesseval_program(struct st_context *st,
                              struct st_tesseval_program *sttep)
{
   struct ureg_program *ureg;

   ureg = ureg_create_with_screen(PIPE_SHADER_TESS_EVAL, st->pipe->screen);
   if (ureg == NULL)
      return false;

   if (sttep->Base.PrimitiveMode == GL_ISOLINES)
      ureg_property(ureg, TGSI_PROPERTY_TES_PRIM_MODE, GL_LINES);
   else
      ureg_property(ureg, TGSI_PROPERTY_TES_PRIM_MODE, sttep->Base.PrimitiveMode);

   switch (sttep->Base.Spacing) {
   case GL_EQUAL:
      ureg_property(ureg, TGSI_PROPERTY_TES_SPACING, PIPE_TESS_SPACING_EQUAL);
      break;
   case GL_FRACTIONAL_EVEN:
      ureg_property(ureg, TGSI_PROPERTY_TES_SPACING,
                    PIPE_TESS_SPACING_FRACTIONAL_EVEN);
      break;
   case GL_FRACTIONAL_ODD:
      ureg_property(ureg, TGSI_PROPERTY_TES_SPACING,
                    PIPE_TESS_SPACING_FRACTIONAL_ODD);
      break;
   default:
      assert(0);
   }

   ureg_property(ureg, TGSI_PROPERTY_TES_VERTEX_ORDER_CW,
                 sttep->Base.VertexOrder == GL_CW);
   ureg_property(ureg, TGSI_PROPERTY_TES_POINT_MODE, sttep->Base.PointMode);

   st_translate_program_common(st, &sttep->Base.Base, sttep->glsl_to_tgsi,
                               ureg, PIPE_SHADER_TESS_EVAL, &sttep->tgsi);

   free_glsl_to_tgsi_visitor(sttep->glsl_to_tgsi);
   sttep->glsl_to_tgsi = NULL;
   return true;
}


/**
 * Translate a compute program to create a new variant.
 */
bool
st_translate_compute_program(struct st_context *st,
                             struct st_compute_program *stcp)
{
   struct ureg_program *ureg;
   struct pipe_shader_state prog;

   ureg = ureg_create_with_screen(PIPE_SHADER_COMPUTE, st->pipe->screen);
   if (ureg == NULL)
      return false;

   st_translate_program_common(st, &stcp->Base.Base, stcp->glsl_to_tgsi, ureg,
                               PIPE_SHADER_COMPUTE, &prog);

   stcp->tgsi.ir_type = PIPE_SHADER_IR_TGSI;
   stcp->tgsi.prog = prog.tokens;
   stcp->tgsi.req_local_mem = stcp->Base.SharedSize;
   stcp->tgsi.req_private_mem = 0;
   stcp->tgsi.req_input_mem = 0;

   free_glsl_to_tgsi_visitor(stcp->glsl_to_tgsi);
   stcp->glsl_to_tgsi = NULL;
   return true;
}


/**
 * Get/create compute program variant.
 */
struct st_basic_variant *
st_get_cp_variant(struct st_context *st,
                  struct pipe_compute_state *tgsi,
                  struct st_basic_variant **variants)
{
   struct pipe_context *pipe = st->pipe;
   struct st_basic_variant *v;
   struct st_basic_variant_key key;

   memset(&key, 0, sizeof(key));
   key.st = st->has_shareable_shaders ? NULL : st;

   /* Search for existing variant */
   for (v = *variants; v; v = v->next) {
      if (memcmp(&v->key, &key, sizeof(key)) == 0) {
         break;
      }
   }

   if (!v) {
      /* create new */
      v = CALLOC_STRUCT(st_basic_variant);
      if (v) {
         /* fill in new variant */
         v->driver_shader = pipe->create_compute_state(pipe, tgsi);
         v->key = key;

         /* insert into list */
         v->next = *variants;
         *variants = v;
      }
   }

   return v;
}


/**
 * Vert/Geom/Frag programs have per-context variants.  Free all the
 * variants attached to the given program which match the given context.
 */
static void
destroy_program_variants(struct st_context *st, struct gl_program *target)
{
   if (!target || target == &_mesa_DummyProgram)
      return;

   switch (target->Target) {
   case GL_VERTEX_PROGRAM_ARB:
      {
         struct st_vertex_program *stvp = (struct st_vertex_program *) target;
         struct st_vp_variant *vpv, **prevPtr = &stvp->variants;

         for (vpv = stvp->variants; vpv; ) {
            struct st_vp_variant *next = vpv->next;
            if (vpv->key.st == st) {
               /* unlink from list */
               *prevPtr = next;
               /* destroy this variant */
               delete_vp_variant(st, vpv);
            }
            else {
               prevPtr = &vpv->next;
            }
            vpv = next;
         }
      }
      break;
   case GL_FRAGMENT_PROGRAM_ARB:
      {
         struct st_fragment_program *stfp =
            (struct st_fragment_program *) target;
         struct st_fp_variant *fpv, **prevPtr = &stfp->variants;

         for (fpv = stfp->variants; fpv; ) {
            struct st_fp_variant *next = fpv->next;
            if (fpv->key.st == st) {
               /* unlink from list */
               *prevPtr = next;
               /* destroy this variant */
               delete_fp_variant(st, fpv);
            }
            else {
               prevPtr = &fpv->next;
            }
            fpv = next;
         }
      }
      break;
   case GL_GEOMETRY_PROGRAM_NV:
   case GL_TESS_CONTROL_PROGRAM_NV:
   case GL_TESS_EVALUATION_PROGRAM_NV:
   case GL_COMPUTE_PROGRAM_NV:
      {
         struct st_geometry_program *gp = (struct st_geometry_program*)target;
         struct st_tessctrl_program *tcp = (struct st_tessctrl_program*)target;
         struct st_tesseval_program *tep = (struct st_tesseval_program*)target;
         struct st_compute_program *cp = (struct st_compute_program*)target;
         struct st_basic_variant **variants =
            target->Target == GL_GEOMETRY_PROGRAM_NV ? &gp->variants :
            target->Target == GL_TESS_CONTROL_PROGRAM_NV ? &tcp->variants :
            target->Target == GL_TESS_EVALUATION_PROGRAM_NV ? &tep->variants :
            target->Target == GL_COMPUTE_PROGRAM_NV ? &cp->variants :
            NULL;
         struct st_basic_variant *v, **prevPtr = variants;

         for (v = *variants; v; ) {
            struct st_basic_variant *next = v->next;
            if (v->key.st == st) {
               /* unlink from list */
               *prevPtr = next;
               /* destroy this variant */
               delete_basic_variant(st, v, target->Target);
            }
            else {
               prevPtr = &v->next;
            }
            v = next;
         }
      }
      break;
   default:
      _mesa_problem(NULL, "Unexpected program target 0x%x in "
                    "destroy_program_variants_cb()", target->Target);
   }
}


/**
 * Callback for _mesa_HashWalk.  Free all the shader's program variants
 * which match the given context.
 */
static void
destroy_shader_program_variants_cb(GLuint key, void *data, void *userData)
{
   struct st_context *st = (struct st_context *) userData;
   struct gl_shader *shader = (struct gl_shader *) data;

   switch (shader->Type) {
   case GL_SHADER_PROGRAM_MESA:
      {
         struct gl_shader_program *shProg = (struct gl_shader_program *) data;
         GLuint i;

	 for (i = 0; i < ARRAY_SIZE(shProg->_LinkedShaders); i++) {
	    if (shProg->_LinkedShaders[i])
               destroy_program_variants(st, shProg->_LinkedShaders[i]->Program);
	 }
      }
      break;
   case GL_VERTEX_SHADER:
   case GL_FRAGMENT_SHADER:
   case GL_GEOMETRY_SHADER:
   case GL_TESS_CONTROL_SHADER:
   case GL_TESS_EVALUATION_SHADER:
   case GL_COMPUTE_SHADER:
      break;
   default:
      assert(0);
   }
}


/**
 * Callback for _mesa_HashWalk.  Free all the program variants which match
 * the given context.
 */
static void
destroy_program_variants_cb(GLuint key, void *data, void *userData)
{
   struct st_context *st = (struct st_context *) userData;
   struct gl_program *program = (struct gl_program *) data;
   destroy_program_variants(st, program);
}


/**
 * Walk over all shaders and programs to delete any variants which
 * belong to the given context.
 * This is called during context tear-down.
 */
void
st_destroy_program_variants(struct st_context *st)
{
   /* If shaders can be shared with other contexts, the last context will
    * call DeleteProgram on all shaders, releasing everything.
    */
   if (st->has_shareable_shaders)
      return;

   /* ARB vert/frag program */
   _mesa_HashWalk(st->ctx->Shared->Programs,
                  destroy_program_variants_cb, st);

   /* GLSL vert/frag/geom shaders */
   _mesa_HashWalk(st->ctx->Shared->ShaderObjects,
                  destroy_shader_program_variants_cb, st);
}


/**
 * For debugging, print/dump the current vertex program.
 */
void
st_print_current_vertex_program(void)
{
   GET_CURRENT_CONTEXT(ctx);

   if (ctx->VertexProgram._Current) {
      struct st_vertex_program *stvp =
         (struct st_vertex_program *) ctx->VertexProgram._Current;
      struct st_vp_variant *stv;

      debug_printf("Vertex program %u\n", stvp->Base.Base.Id);

      for (stv = stvp->variants; stv; stv = stv->next) {
         debug_printf("variant %p\n", stv);
         tgsi_dump(stv->tgsi.tokens, 0);
      }
   }
}


/**
 * Compile one shader variant.
 */
void
st_precompile_shader_variant(struct st_context *st,
                             struct gl_program *prog)
{
   switch (prog->Target) {
   case GL_VERTEX_PROGRAM_ARB: {
      struct st_vertex_program *p = (struct st_vertex_program *)prog;
      struct st_vp_variant_key key;

      memset(&key, 0, sizeof(key));
      key.st = st->has_shareable_shaders ? NULL : st;
      st_get_vp_variant(st, p, &key);
      break;
   }

   case GL_TESS_CONTROL_PROGRAM_NV: {
      struct st_tessctrl_program *p = (struct st_tessctrl_program *)prog;
      st_get_basic_variant(st, PIPE_SHADER_TESS_CTRL, &p->tgsi, &p->variants);
      break;
   }

   case GL_TESS_EVALUATION_PROGRAM_NV: {
      struct st_tesseval_program *p = (struct st_tesseval_program *)prog;
      st_get_basic_variant(st, PIPE_SHADER_TESS_EVAL, &p->tgsi, &p->variants);
      break;
   }

   case GL_GEOMETRY_PROGRAM_NV: {
      struct st_geometry_program *p = (struct st_geometry_program *)prog;
      st_get_basic_variant(st, PIPE_SHADER_GEOMETRY, &p->tgsi, &p->variants);
      break;
   }

   case GL_FRAGMENT_PROGRAM_ARB: {
      struct st_fragment_program *p = (struct st_fragment_program *)prog;
      struct st_fp_variant_key key;

      memset(&key, 0, sizeof(key));
      key.st = st->has_shareable_shaders ? NULL : st;
      st_get_fp_variant(st, p, &key);
      break;
   }

   case GL_COMPUTE_PROGRAM_NV: {
      struct st_compute_program *p = (struct st_compute_program *)prog;
      st_get_cp_variant(st, &p->tgsi, &p->variants);
      break;
   }

   default:
      assert(0);
   }
}
