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


#include "main/errors.h"

#include "main/hash.h"
#include "main/mtypes.h"
#include "program/prog_parameter.h"
#include "program/prog_print.h"
#include "program/prog_to_nir.h"
#include "program/programopt.h"

#include "compiler/glsl/gl_nir.h"
#include "compiler/glsl/gl_nir_linker.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_serialize.h"
#include "draw/draw_context.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_shader_tokens.h"
#include "draw/draw_context.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_ureg.h"
#include "nir/nir_to_tgsi.h"

#include "util/u_memory.h"

#include "st_debug.h"
#include "st_cb_bitmap.h"
#include "st_cb_drawpixels.h"
#include "st_context.h"
#include "st_program.h"
#include "st_atifs_to_nir.h"
#include "st_nir.h"
#include "st_shader_cache.h"
#include "st_util.h"
#include "cso_cache/cso_context.h"


static void
destroy_program_variants(struct st_context *st, struct gl_program *target);

static void
set_affected_state_flags(uint64_t *states,
                         struct gl_program *prog,
                         uint64_t new_constants,
                         uint64_t new_sampler_views,
                         uint64_t new_samplers,
                         uint64_t new_images,
                         uint64_t new_ubos,
                         uint64_t new_ssbos,
                         uint64_t new_atomics)
{
   if (prog->Parameters->NumParameters)
      *states |= new_constants;

   if (prog->info.num_textures)
      *states |= new_sampler_views | new_samplers;

   if (prog->info.num_images)
      *states |= new_images;

   if (prog->info.num_ubos)
      *states |= new_ubos;

   if (prog->info.num_ssbos)
      *states |= new_ssbos;

   if (prog->info.num_abos)
      *states |= new_atomics;
}

/**
 * This determines which states will be updated when the shader is bound.
 */
void
st_set_prog_affected_state_flags(struct gl_program *prog)
{
   uint64_t *states;

   switch (prog->info.stage) {
   case MESA_SHADER_VERTEX:
      states = &prog->affected_states;

      *states = ST_NEW_VS_STATE |
                ST_NEW_RASTERIZER |
                ST_NEW_VERTEX_ARRAYS;

      set_affected_state_flags(states, prog,
                               ST_NEW_VS_CONSTANTS,
                               ST_NEW_VS_SAMPLER_VIEWS,
                               ST_NEW_VS_SAMPLERS,
                               ST_NEW_VS_IMAGES,
                               ST_NEW_VS_UBOS,
                               ST_NEW_VS_SSBOS,
                               ST_NEW_VS_ATOMICS);
      break;

   case MESA_SHADER_TESS_CTRL:
      states = &prog->affected_states;

      *states = ST_NEW_TCS_STATE;

      set_affected_state_flags(states, prog,
                               ST_NEW_TCS_CONSTANTS,
                               ST_NEW_TCS_SAMPLER_VIEWS,
                               ST_NEW_TCS_SAMPLERS,
                               ST_NEW_TCS_IMAGES,
                               ST_NEW_TCS_UBOS,
                               ST_NEW_TCS_SSBOS,
                               ST_NEW_TCS_ATOMICS);
      break;

   case MESA_SHADER_TESS_EVAL:
      states = &prog->affected_states;

      *states = ST_NEW_TES_STATE |
                ST_NEW_RASTERIZER;

      set_affected_state_flags(states, prog,
                               ST_NEW_TES_CONSTANTS,
                               ST_NEW_TES_SAMPLER_VIEWS,
                               ST_NEW_TES_SAMPLERS,
                               ST_NEW_TES_IMAGES,
                               ST_NEW_TES_UBOS,
                               ST_NEW_TES_SSBOS,
                               ST_NEW_TES_ATOMICS);
      break;

   case MESA_SHADER_GEOMETRY:
      states = &prog->affected_states;

      *states = ST_NEW_GS_STATE |
                ST_NEW_RASTERIZER;

      set_affected_state_flags(states, prog,
                               ST_NEW_GS_CONSTANTS,
                               ST_NEW_GS_SAMPLER_VIEWS,
                               ST_NEW_GS_SAMPLERS,
                               ST_NEW_GS_IMAGES,
                               ST_NEW_GS_UBOS,
                               ST_NEW_GS_SSBOS,
                               ST_NEW_GS_ATOMICS);
      break;

   case MESA_SHADER_FRAGMENT:
      states = &prog->affected_states;

      /* gl_FragCoord and glDrawPixels always use constants. */
      *states = ST_NEW_FS_STATE |
                ST_NEW_SAMPLE_SHADING |
                ST_NEW_FS_CONSTANTS;

      set_affected_state_flags(states, prog,
                               ST_NEW_FS_CONSTANTS,
                               ST_NEW_FS_SAMPLER_VIEWS,
                               ST_NEW_FS_SAMPLERS,
                               ST_NEW_FS_IMAGES,
                               ST_NEW_FS_UBOS,
                               ST_NEW_FS_SSBOS,
                               ST_NEW_FS_ATOMICS);
      break;

   case MESA_SHADER_COMPUTE:
      states = &prog->affected_states;

      *states = ST_NEW_CS_STATE;

      set_affected_state_flags(states, prog,
                               ST_NEW_CS_CONSTANTS,
                               ST_NEW_CS_SAMPLER_VIEWS,
                               ST_NEW_CS_SAMPLERS,
                               ST_NEW_CS_IMAGES,
                               ST_NEW_CS_UBOS,
                               ST_NEW_CS_SSBOS,
                               ST_NEW_CS_ATOMICS);
      break;

   default:
      unreachable("unhandled shader stage");
   }
}


/**
 * Delete a shader variant.  Note the caller must unlink the variant from
 * the linked list.
 */
static void
delete_variant(struct st_context *st, struct st_variant *v, GLenum target)
{
   if (v->driver_shader) {
      if (target == GL_VERTEX_PROGRAM_ARB &&
          ((struct st_common_variant*)v)->key.is_draw_shader) {
         /* Draw shader. */
         draw_delete_vertex_shader(st->draw, v->driver_shader);
      } else if (st->has_shareable_shaders || v->st == st) {
         /* The shader's context matches the calling context, or we
          * don't care.
          */
         switch (target) {
         case GL_VERTEX_PROGRAM_ARB:
            st->pipe->delete_vs_state(st->pipe, v->driver_shader);
            break;
         case GL_TESS_CONTROL_PROGRAM_NV:
            st->pipe->delete_tcs_state(st->pipe, v->driver_shader);
            break;
         case GL_TESS_EVALUATION_PROGRAM_NV:
            st->pipe->delete_tes_state(st->pipe, v->driver_shader);
            break;
         case GL_GEOMETRY_PROGRAM_NV:
            st->pipe->delete_gs_state(st->pipe, v->driver_shader);
            break;
         case GL_FRAGMENT_PROGRAM_ARB:
            st->pipe->delete_fs_state(st->pipe, v->driver_shader);
            break;
         case GL_COMPUTE_PROGRAM_NV:
            st->pipe->delete_compute_state(st->pipe, v->driver_shader);
            break;
         default:
            unreachable("bad shader type in delete_basic_variant");
         }
      } else {
         /* We can't delete a shader with a context different from the one
          * that created it.  Add it to the creating context's zombie list.
          */
         enum pipe_shader_type type =
            pipe_shader_type_from_mesa(_mesa_program_enum_to_shader_stage(target));

         st_save_zombie_shader(v->st, type, v->driver_shader);
      }
   }

   FREE(v);
}

static void
st_unbind_program(struct st_context *st, struct gl_program *p)
{
   /* Unbind the shader in cso_context and re-bind in st/mesa. */
   switch (p->info.stage) {
   case MESA_SHADER_VERTEX:
      cso_set_vertex_shader_handle(st->cso_context, NULL);
      st->dirty |= ST_NEW_VS_STATE;
      break;
   case MESA_SHADER_TESS_CTRL:
      cso_set_tessctrl_shader_handle(st->cso_context, NULL);
      st->dirty |= ST_NEW_TCS_STATE;
      break;
   case MESA_SHADER_TESS_EVAL:
      cso_set_tesseval_shader_handle(st->cso_context, NULL);
      st->dirty |= ST_NEW_TES_STATE;
      break;
   case MESA_SHADER_GEOMETRY:
      cso_set_geometry_shader_handle(st->cso_context, NULL);
      st->dirty |= ST_NEW_GS_STATE;
      break;
   case MESA_SHADER_FRAGMENT:
      cso_set_fragment_shader_handle(st->cso_context, NULL);
      st->dirty |= ST_NEW_FS_STATE;
      break;
   case MESA_SHADER_COMPUTE:
      cso_set_compute_shader_handle(st->cso_context, NULL);
      st->dirty |= ST_NEW_CS_STATE;
      break;
   default:
      unreachable("invalid shader type");
   }
}

/**
 * Free all basic program variants.
 */
void
st_release_variants(struct st_context *st, struct gl_program *p)
{
   struct st_variant *v;

   /* If we are releasing shaders, re-bind them, because we don't
    * know which shaders are bound in the driver.
    */
   if (p->variants)
      st_unbind_program(st, p);

   for (v = p->variants; v; ) {
      struct st_variant *next = v->next;
      delete_variant(st, v, p->Target);
      v = next;
   }

   p->variants = NULL;

   if (p->state.tokens) {
      ureg_free_tokens(p->state.tokens);
      p->state.tokens = NULL;
   }

   /* Note: Any setup of ->ir.nir that has had pipe->create_*_state called on
    * it has resulted in the driver taking ownership of the NIR.  Those
    * callers should be NULLing out the nir field in any pipe_shader_state
    * that might have this called in order to indicate that.
    *
    * GLSL IR and ARB programs will have set gl_program->nir to the same
    * shader as ir->ir.nir, so it will be freed by _mesa_delete_program().
    */
}

/**
 * Free all basic program variants and unref program.
 */
void
st_release_program(struct st_context *st, struct gl_program **p)
{
   if (!*p)
      return;

   destroy_program_variants(st, *p);
   _mesa_reference_program(st->ctx, p, NULL);
}

void
st_finalize_nir_before_variants(struct nir_shader *nir)
{
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);
   if (nir->options->lower_all_io_to_temps ||
       nir->options->lower_all_io_to_elements ||
       nir->info.stage == MESA_SHADER_VERTEX ||
       nir->info.stage == MESA_SHADER_GEOMETRY) {
      NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, true);
   }

   /* st_nir_assign_vs_in_locations requires correct shader info. */
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   st_nir_assign_vs_in_locations(nir);
}

static void
st_prog_to_nir_postprocess(struct st_context *st, nir_shader *nir,
                           struct gl_program *prog)
{
   struct pipe_screen *screen = st->screen;

   NIR_PASS_V(nir, nir_lower_regs_to_ssa);
   nir_validate_shader(nir, "after st/ptn lower_regs_to_ssa");

   /* Lower outputs to temporaries to avoid reading from output variables (which
    * is permitted by the language but generally not implemented in HW).
    */
   NIR_PASS_V(nir, nir_lower_io_to_temporaries,
               nir_shader_get_entrypoint(nir),
               true, false);
   NIR_PASS_V(nir, nir_lower_global_vars_to_local);

   NIR_PASS_V(nir, st_nir_lower_wpos_ytransform, prog, screen);
   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_compute_system_values, NULL);

   /* Optimise NIR */
   NIR_PASS_V(nir, nir_opt_constant_folding);
   gl_nir_opts(nir);
   st_finalize_nir_before_variants(nir);

   if (st->allow_st_finalize_nir_twice) {
      char *msg = st_finalize_nir(st, prog, NULL, nir, true, true);
      free(msg);
   }

   nir_validate_shader(nir, "after st/glsl finalize_nir");
}

/**
 * Translate ARB (asm) program to NIR
 */
static nir_shader *
st_translate_prog_to_nir(struct st_context *st, struct gl_program *prog,
                         gl_shader_stage stage)
{
   const struct nir_shader_compiler_options *options =
      st_get_nir_compiler_options(st, prog->info.stage);

   /* Translate to NIR */
   nir_shader *nir = prog_to_nir(st->ctx, prog, options);

   st_prog_to_nir_postprocess(st, nir, prog);

   return nir;
}

/**
 * Prepare st_vertex_program info.
 *
 * attrib_to_index is an optional mapping from a vertex attrib to a shader
 * input index.
 */
void
st_prepare_vertex_program(struct gl_program *prog)
{
   struct gl_vertex_program *stvp = (struct gl_vertex_program *)prog;

   stvp->num_inputs = util_bitcount64(prog->info.inputs_read);
   stvp->vert_attrib_mask = prog->info.inputs_read;

   /* Compute mapping of vertex program outputs to slots. */
   memset(stvp->result_to_output, ~0, sizeof(stvp->result_to_output));
   unsigned num_outputs = 0;
   for (unsigned attr = 0; attr < VARYING_SLOT_MAX; attr++) {
      if (prog->info.outputs_written & BITFIELD64_BIT(attr))
         stvp->result_to_output[attr] = num_outputs++;
   }
   /* pre-setup potentially unused edgeflag output */
   stvp->result_to_output[VARYING_SLOT_EDGE] = num_outputs;
}

void
st_translate_stream_output_info(struct gl_program *prog)
{
   struct gl_transform_feedback_info *info = prog->sh.LinkedTransformFeedback;
   if (!info)
      return;

   /* Determine the (default) output register mapping for each output. */
   unsigned num_outputs = 0;
   ubyte output_mapping[VARYING_SLOT_TESS_MAX];
   memset(output_mapping, 0, sizeof(output_mapping));

   for (unsigned attr = 0; attr < VARYING_SLOT_MAX; attr++) {
      /* this output was added by mesa/st and should not be tracked for xfb:
       * drivers must check var->data.explicit_location to find the original output
       * and only emit that one for xfb
       */
      if (prog->skip_pointsize_xfb && attr == VARYING_SLOT_PSIZ)
         continue;
      if (prog->info.outputs_written & BITFIELD64_BIT(attr))
         output_mapping[attr] = num_outputs++;
   }

   /* Translate stream output info. */
   struct pipe_stream_output_info *so_info =
      &prog->state.stream_output;

   for (unsigned i = 0; i < info->NumOutputs; i++) {
      so_info->output[i].register_index =
         output_mapping[info->Outputs[i].OutputRegister];
      so_info->output[i].start_component = info->Outputs[i].ComponentOffset;
      so_info->output[i].num_components = info->Outputs[i].NumComponents;
      so_info->output[i].output_buffer = info->Outputs[i].OutputBuffer;
      so_info->output[i].dst_offset = info->Outputs[i].DstOffset;
      so_info->output[i].stream = info->Outputs[i].StreamId;
   }

   for (unsigned i = 0; i < PIPE_MAX_SO_BUFFERS; i++) {
      so_info->stride[i] = info->Buffers[i].Stride;
   }
   so_info->num_outputs = info->NumOutputs;
}

/**
 * Creates a driver shader from a NIR shader.  Takes ownership of the
 * passed nir_shader.
 */
struct pipe_shader_state *
st_create_nir_shader(struct st_context *st, struct pipe_shader_state *state)
{
   struct pipe_context *pipe = st->pipe;
   struct pipe_screen *screen = st->screen;

   assert(state->type == PIPE_SHADER_IR_NIR);
   nir_shader *nir = state->ir.nir;
   struct shader_info info = nir->info;
   gl_shader_stage stage = nir->info.stage;
   enum pipe_shader_type sh = pipe_shader_type_from_mesa(stage);

   if (ST_DEBUG & DEBUG_PRINT_IR) {
      fprintf(stderr, "NIR before handing off to driver:\n");
      nir_print_shader(nir, stderr);
   }

   if (PIPE_SHADER_IR_NIR !=
       screen->get_shader_param(screen, sh, PIPE_SHADER_CAP_PREFERRED_IR)) {
      /* u_screen.c defaults to images as deref enabled for some reason (which
       * is what radeonsi wants), but nir-to-tgsi requires lowered images.
       */
      if (screen->get_param(screen, PIPE_CAP_NIR_IMAGES_AS_DEREF))
         NIR_PASS_V(nir, gl_nir_lower_images, false);

      state->type = PIPE_SHADER_IR_TGSI;
      state->tokens = nir_to_tgsi(nir, screen);

      if (ST_DEBUG & DEBUG_PRINT_IR) {
         fprintf(stderr, "TGSI for driver after nir-to-tgsi:\n");
         tgsi_dump(state->tokens, 0);
         fprintf(stderr, "\n");
      }
   }

   struct pipe_shader_state *shader;
   switch (stage) {
   case MESA_SHADER_VERTEX:
      shader = pipe->create_vs_state(pipe, state);
      break;
   case MESA_SHADER_TESS_CTRL:
      shader = pipe->create_tcs_state(pipe, state);
      break;
   case MESA_SHADER_TESS_EVAL:
      shader = pipe->create_tes_state(pipe, state);
      break;
   case MESA_SHADER_GEOMETRY:
      shader = pipe->create_gs_state(pipe, state);
      break;
   case MESA_SHADER_FRAGMENT:
      shader = pipe->create_fs_state(pipe, state);
      break;
   case MESA_SHADER_COMPUTE: {
      struct pipe_compute_state cs = {0};
      cs.ir_type = state->type;
      cs.req_local_mem = info.shared_size;

      if (state->type == PIPE_SHADER_IR_NIR)
         cs.prog = state->ir.nir;
      else
         cs.prog = state->tokens;

      shader = pipe->create_compute_state(pipe, &cs);
      break;
   }
   default:
      unreachable("unsupported shader stage");
      return NULL;
   }

   if (state->type == PIPE_SHADER_IR_TGSI)
      tgsi_free_tokens(state->tokens);

   return shader;
}

/**
 * Translate a vertex program.
 */
static bool
st_translate_vertex_program(struct st_context *st,
                            struct gl_program *prog)
{
   /* ARB_vp: */
   if (prog->arb.IsPositionInvariant)
      _mesa_insert_mvp_code(st->ctx, prog);

   /* This determines which states will be updated when the assembly
      * shader is bound.
      */
   prog->affected_states = ST_NEW_VS_STATE |
                           ST_NEW_RASTERIZER |
                           ST_NEW_VERTEX_ARRAYS;

   if (prog->Parameters->NumParameters)
      prog->affected_states |= ST_NEW_VS_CONSTANTS;

   if (prog->nir)
      ralloc_free(prog->nir);

   if (prog->serialized_nir) {
      free(prog->serialized_nir);
      prog->serialized_nir = NULL;
   }

   prog->state.type = PIPE_SHADER_IR_NIR;
   prog->nir = st_translate_prog_to_nir(st, prog,
                                          MESA_SHADER_VERTEX);
   prog->info = prog->nir->info;

   st_prepare_vertex_program(prog);
   return true;
}

static struct nir_shader *
get_nir_shader(struct st_context *st, struct gl_program *prog)
{
   if (prog->nir) {
      nir_shader *nir = prog->nir;

      /* The first shader variant takes ownership of NIR, so that there is
       * no cloning. Additional shader variants are always generated from
       * serialized NIR to save memory.
       */
      prog->nir = NULL;
      assert(prog->serialized_nir && prog->serialized_nir_size);
      return nir;
   }

   struct blob_reader blob_reader;
   const struct nir_shader_compiler_options *options =
      st_get_nir_compiler_options(st, prog->info.stage);

   blob_reader_init(&blob_reader, prog->serialized_nir, prog->serialized_nir_size);
   return nir_deserialize(NULL, options, &blob_reader);
}

static void
lower_ucp(struct st_context *st,
          struct nir_shader *nir,
          unsigned ucp_enables,
          struct gl_program_parameter_list *params)
{
   if (nir->info.outputs_written & VARYING_BIT_CLIP_DIST0)
      NIR_PASS_V(nir, nir_lower_clip_disable, ucp_enables);
   else {
      struct pipe_screen *screen = st->screen;
      bool can_compact = screen->get_param(screen,
                                           PIPE_CAP_NIR_COMPACT_ARRAYS);
      bool use_eye = st->ctx->_Shader->CurrentProgram[MESA_SHADER_VERTEX] != NULL;

      gl_state_index16 clipplane_state[MAX_CLIP_PLANES][STATE_LENGTH] = {{0}};
      for (int i = 0; i < MAX_CLIP_PLANES; ++i) {
         if (use_eye) {
            clipplane_state[i][0] = STATE_CLIPPLANE;
            clipplane_state[i][1] = i;
         } else {
            clipplane_state[i][0] = STATE_CLIP_INTERNAL;
            clipplane_state[i][1] = i;
         }
         _mesa_add_state_reference(params, clipplane_state[i]);
      }

      if (nir->info.stage == MESA_SHADER_VERTEX ||
          nir->info.stage == MESA_SHADER_TESS_EVAL) {
         NIR_PASS_V(nir, nir_lower_clip_vs, ucp_enables,
                    true, can_compact, clipplane_state);
      } else if (nir->info.stage == MESA_SHADER_GEOMETRY) {
         NIR_PASS_V(nir, nir_lower_clip_gs, ucp_enables,
                    can_compact, clipplane_state);
      }

      NIR_PASS_V(nir, nir_lower_io_to_temporaries,
                 nir_shader_get_entrypoint(nir), true, false);
      NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   }
}

static struct st_common_variant *
st_create_common_variant(struct st_context *st,
                         struct gl_program *prog,
                         const struct st_common_variant_key *key)
{
   struct st_common_variant *v = CALLOC_STRUCT(st_common_variant);
   struct pipe_shader_state state = {0};

   static const gl_state_index16 point_size_state[STATE_LENGTH] =
      { STATE_POINT_SIZE_CLAMPED, 0 };
   struct gl_program_parameter_list *params = prog->Parameters;

   v->key = *key;

   state.stream_output = prog->state.stream_output;

   bool finalize = false;

   state.type = PIPE_SHADER_IR_NIR;
   state.ir.nir = get_nir_shader(st, prog);
   const nir_shader_compiler_options *options = ((nir_shader *)state.ir.nir)->options;

   if (key->clamp_color) {
      NIR_PASS_V(state.ir.nir, nir_lower_clamp_color_outputs);
      finalize = true;
   }
   if (key->passthrough_edgeflags) {
      NIR_PASS_V(state.ir.nir, nir_lower_passthrough_edgeflags);
      finalize = true;
   }

   if (key->export_point_size) {
      /* if flag is set, shader must export psiz */
      _mesa_add_state_reference(params, point_size_state);
      NIR_PASS_V(state.ir.nir, nir_lower_point_size_mov,
                  point_size_state);

      finalize = true;
   }

   if (key->lower_ucp) {
      assert(!options->unify_interfaces);
      lower_ucp(st, state.ir.nir, key->lower_ucp, params);
      finalize = true;
   }

   if (st->emulate_gl_clamp &&
         (key->gl_clamp[0] || key->gl_clamp[1] || key->gl_clamp[2])) {
      nir_lower_tex_options tex_opts = {0};
      tex_opts.saturate_s = key->gl_clamp[0];
      tex_opts.saturate_t = key->gl_clamp[1];
      tex_opts.saturate_r = key->gl_clamp[2];
      NIR_PASS_V(state.ir.nir, nir_lower_tex, &tex_opts);
   }

   if (finalize || !st->allow_st_finalize_nir_twice) {
      char *msg = st_finalize_nir(st, prog, prog->shader_program, state.ir.nir,
                                    true, false);
      free(msg);

      /* Clip lowering and edgeflags may have introduced new varyings, so
       * update the inputs_read/outputs_written. However, with
       * unify_interfaces set (aka iris) the non-SSO varyings layout is
       * decided at link time with outputs_written updated so the two line
       * up.  A driver with this flag set may not use any of the lowering
       * passes that would change the varyings, so skip to make sure we don't
       * break its linkage.
       */
      if (!options->unify_interfaces) {
         nir_shader_gather_info(state.ir.nir,
                                 nir_shader_get_entrypoint(state.ir.nir));
      }
   }

   if (key->is_draw_shader) {
      NIR_PASS_V(state.ir.nir, gl_nir_lower_images, false);
      v->base.driver_shader = draw_create_vertex_shader(st->draw, &state);
   }
   else
      v->base.driver_shader = st_create_nir_shader(st, &state);

   return v;
}

static void
st_add_variant(struct st_variant **list, struct st_variant *v)
{
   struct st_variant *first = *list;

   /* Make sure that the default variant stays the first in the list, and insert
    * any later variants in as the second entry.
    */
   if (first) {
      v->next = first->next;
      first->next = v;
   } else {
      *list = v;
   }
}

/**
 * Find/create a vertex program variant.
 */
struct st_common_variant *
st_get_common_variant(struct st_context *st,
                      struct gl_program *prog,
                      const struct st_common_variant_key *key)
{
   struct st_common_variant *v;

   /* Search for existing variant */
   for (v = st_common_variant(prog->variants); v;
        v = st_common_variant(v->base.next)) {
      if (memcmp(&v->key, key, sizeof(*key)) == 0) {
         break;
      }
   }

   if (!v) {
      if (prog->variants != NULL) {
         _mesa_perf_debug(st->ctx, MESA_DEBUG_SEVERITY_MEDIUM,
                          "Compiling %s shader variant (%s%s%s%s%s%s)",
                          _mesa_shader_stage_to_string(prog->info.stage),
                          key->passthrough_edgeflags ? "edgeflags," : "",
                          key->clamp_color ? "clamp_color," : "",
                          key->export_point_size ? "point_size," : "",
                          key->lower_ucp ? "ucp," : "",
                          key->is_draw_shader ? "draw," : "",
                          key->gl_clamp[0] || key->gl_clamp[1] || key->gl_clamp[2] ? "GL_CLAMP," : "");
      }

      /* create now */
      v = st_create_common_variant(st, prog, key);
      if (v) {
         v->base.st = key->st;

         if (prog->info.stage == MESA_SHADER_VERTEX) {
            struct gl_vertex_program *vp = (struct gl_vertex_program *)prog;

            v->vert_attrib_mask =
               vp->vert_attrib_mask |
               (key->passthrough_edgeflags ? VERT_BIT_EDGEFLAG : 0);
         }

         st_add_variant(&prog->variants, &v->base);
      }
   }

   return v;
}


/**
 * Translate a non-GLSL Mesa fragment shader into a NIR shader.
 */
static bool
st_translate_fragment_program(struct st_context *st,
                              struct gl_program *fp)
{
   /* This determines which states will be updated when the assembly
    * shader is bound.
    *
    * fragment.position and glDrawPixels always use constants.
    */
   fp->affected_states = ST_NEW_FS_STATE |
                           ST_NEW_SAMPLE_SHADING |
                           ST_NEW_FS_CONSTANTS;

   if (fp->ati_fs) {
      /* Just set them for ATI_fs unconditionally. */
      fp->affected_states |= ST_NEW_FS_SAMPLER_VIEWS |
                                 ST_NEW_FS_SAMPLERS;
   } else {
      /* ARB_fp */
      if (fp->SamplersUsed)
         fp->affected_states |= ST_NEW_FS_SAMPLER_VIEWS |
                                    ST_NEW_FS_SAMPLERS;
   }

   /* Translate to NIR.  ATI_fs translates at variant time. */
   if (!fp->ati_fs) {
      nir_shader *nir =
         st_translate_prog_to_nir(st, fp, MESA_SHADER_FRAGMENT);

      if (fp->nir)
         ralloc_free(fp->nir);
      if (fp->serialized_nir) {
         free(fp->serialized_nir);
         fp->serialized_nir = NULL;
      }
      fp->state.type = PIPE_SHADER_IR_NIR;
      fp->nir = nir;
   }

   return true;
}

static struct st_fp_variant *
st_create_fp_variant(struct st_context *st,
                     struct gl_program *fp,
                     const struct st_fp_variant_key *key)
{
   struct st_fp_variant *variant = CALLOC_STRUCT(st_fp_variant);
   struct pipe_shader_state state = {0};
   struct gl_program_parameter_list *params = fp->Parameters;
   static const gl_state_index16 texcoord_state[STATE_LENGTH] =
      { STATE_CURRENT_ATTRIB, VERT_ATTRIB_TEX0 };
   static const gl_state_index16 scale_state[STATE_LENGTH] =
      { STATE_PT_SCALE };
   static const gl_state_index16 bias_state[STATE_LENGTH] =
      { STATE_PT_BIAS };
   static const gl_state_index16 alpha_ref_state[STATE_LENGTH] =
      { STATE_ALPHA_REF };

   if (!variant)
      return NULL;

   /* Translate ATI_fs to NIR at variant time because that's when we have the
    * texture types.
    */
   if (fp->ati_fs) {
      const struct nir_shader_compiler_options *options =
         st_get_nir_compiler_options(st, MESA_SHADER_FRAGMENT);

      nir_shader *s = st_translate_atifs_program(fp->ati_fs, key, fp, options);

      st_prog_to_nir_postprocess(st, s, fp);

      state.ir.nir = s;
   } else {
      state.ir.nir = get_nir_shader(st, fp);
   }
   state.type = PIPE_SHADER_IR_NIR;

   bool finalize = false;

   if (key->clamp_color) {
      NIR_PASS_V(state.ir.nir, nir_lower_clamp_color_outputs);
      finalize = true;
   }

   if (key->lower_flatshade) {
      NIR_PASS_V(state.ir.nir, nir_lower_flatshade);
      finalize = true;
   }

   if (key->lower_alpha_func != COMPARE_FUNC_ALWAYS) {
      _mesa_add_state_reference(params, alpha_ref_state);
      NIR_PASS_V(state.ir.nir, nir_lower_alpha_test, key->lower_alpha_func,
                  false, alpha_ref_state);
      finalize = true;
   }

   if (key->lower_two_sided_color) {
      bool face_sysval = st->ctx->Const.GLSLFrontFacingIsSysVal;
      NIR_PASS_V(state.ir.nir, nir_lower_two_sided_color, face_sysval);
      finalize = true;
   }

   if (key->persample_shading) {
      nir_shader *shader = state.ir.nir;
      nir_foreach_shader_in_variable(var, shader)
         var->data.sample = true;
      finalize = true;
   }

   if (key->lower_texcoord_replace) {
      bool point_coord_is_sysval = st->ctx->Const.GLSLPointCoordIsSysVal;
      NIR_PASS_V(state.ir.nir, nir_lower_texcoord_replace,
                  key->lower_texcoord_replace, point_coord_is_sysval, false);
      finalize = true;
   }

   if (st->emulate_gl_clamp &&
         (key->gl_clamp[0] || key->gl_clamp[1] || key->gl_clamp[2])) {
      nir_lower_tex_options tex_opts = {0};
      tex_opts.saturate_s = key->gl_clamp[0];
      tex_opts.saturate_t = key->gl_clamp[1];
      tex_opts.saturate_r = key->gl_clamp[2];
      NIR_PASS_V(state.ir.nir, nir_lower_tex, &tex_opts);
      finalize = true;
   }

   assert(!(key->bitmap && key->drawpixels));

   /* glBitmap */
   if (key->bitmap) {
      nir_lower_bitmap_options options = {0};

      variant->bitmap_sampler = ffs(~fp->SamplersUsed) - 1;
      options.sampler = variant->bitmap_sampler;
      options.swizzle_xxxx = st->bitmap.tex_format == PIPE_FORMAT_R8_UNORM;

      NIR_PASS_V(state.ir.nir, nir_lower_bitmap, &options);
      finalize = true;
   }

   /* glDrawPixels (color only) */
   if (key->drawpixels) {
      nir_lower_drawpixels_options options = {{0}};
      unsigned samplers_used = fp->SamplersUsed;

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

      NIR_PASS_V(state.ir.nir, nir_lower_drawpixels, &options);
      finalize = true;
   }

   bool need_lower_tex_src_plane = false;

   if (unlikely(key->external.lower_nv12 || key->external.lower_iyuv ||
                  key->external.lower_xy_uxvx || key->external.lower_yx_xuxv ||
                  key->external.lower_ayuv || key->external.lower_xyuv ||
                  key->external.lower_yuv || key->external.lower_yu_yv ||
                  key->external.lower_y41x)) {

      st_nir_lower_samplers(st->screen, state.ir.nir,
                              fp->shader_program, fp);

      nir_lower_tex_options options = {0};
      options.lower_y_uv_external = key->external.lower_nv12;
      options.lower_y_u_v_external = key->external.lower_iyuv;
      options.lower_xy_uxvx_external = key->external.lower_xy_uxvx;
      options.lower_yx_xuxv_external = key->external.lower_yx_xuxv;
      options.lower_ayuv_external = key->external.lower_ayuv;
      options.lower_xyuv_external = key->external.lower_xyuv;
      options.lower_yuv_external = key->external.lower_yuv;
      options.lower_yu_yv_external = key->external.lower_yu_yv;
      options.lower_y41x_external = key->external.lower_y41x;
      options.bt709_external = key->external.bt709;
      options.bt2020_external = key->external.bt2020;
      options.yuv_full_range_external = key->external.yuv_full_range;
      NIR_PASS_V(state.ir.nir, nir_lower_tex, &options);
      finalize = true;
      need_lower_tex_src_plane = true;
   }

   if (finalize || !st->allow_st_finalize_nir_twice) {
      char *msg = st_finalize_nir(st, fp, fp->shader_program, state.ir.nir,
                                    false, false);
      free(msg);
   }

   /* This pass needs to happen *after* nir_lower_sampler */
   if (unlikely(need_lower_tex_src_plane)) {
      NIR_PASS_V(state.ir.nir, st_nir_lower_tex_src_plane,
                  ~fp->SamplersUsed,
                  key->external.lower_nv12 | key->external.lower_xy_uxvx |
                     key->external.lower_yx_xuxv,
                  key->external.lower_iyuv);
      finalize = true;
   }

   if (finalize || !st->allow_st_finalize_nir_twice) {
      /* Some of the lowering above may have introduced new varyings */
      nir_shader_gather_info(state.ir.nir,
                              nir_shader_get_entrypoint(state.ir.nir));

      struct pipe_screen *screen = st->screen;
      if (screen->finalize_nir) {
         char *msg = screen->finalize_nir(screen, state.ir.nir);
         free(msg);
      }
   }

   variant->base.driver_shader = st_create_nir_shader(st, &state);
   variant->key = *key;

   return variant;
}

/**
 * Translate fragment program if needed.
 */
struct st_fp_variant *
st_get_fp_variant(struct st_context *st,
                  struct gl_program *fp,
                  const struct st_fp_variant_key *key)
{
   struct st_fp_variant *fpv;

   /* Search for existing variant */
   for (fpv = st_fp_variant(fp->variants); fpv;
        fpv = st_fp_variant(fpv->base.next)) {
      if (memcmp(&fpv->key, key, sizeof(*key)) == 0) {
         break;
      }
   }

   if (!fpv) {
      /* create new */

      if (fp->variants != NULL) {
         _mesa_perf_debug(st->ctx, MESA_DEBUG_SEVERITY_MEDIUM,
                          "Compiling fragment shader variant (%s%s%s%s%s%s%s%s%s%s%s%s%s)",
                          key->bitmap ? "bitmap," : "",
                          key->drawpixels ? "drawpixels," : "",
                          key->scaleAndBias ? "scale_bias," : "",
                          key->pixelMaps ? "pixel_maps," : "",
                          key->clamp_color ? "clamp_color," : "",
                          key->persample_shading ? "persample_shading," : "",
                          key->fog ? "fog," : "",
                          key->lower_two_sided_color ? "twoside," : "",
                          key->lower_flatshade ? "flatshade," : "",
                          key->lower_texcoord_replace ? "texcoord_replace," : "",
                          key->lower_alpha_func ? "alpha_compare," : "",
                          /* skipped ATI_fs targets */
                          fp->ExternalSamplersUsed ? "external?," : "",
                          key->gl_clamp[0] || key->gl_clamp[1] || key->gl_clamp[2] ? "GL_CLAMP," : "");
      }

      fpv = st_create_fp_variant(st, fp, key);
      if (fpv) {
         fpv->base.st = key->st;

         st_add_variant(&fp->variants, &fpv->base);
      }
   }

   return fpv;
}

/**
 * Vert/Geom/Frag programs have per-context variants.  Free all the
 * variants attached to the given program which match the given context.
 */
static void
destroy_program_variants(struct st_context *st, struct gl_program *p)
{
   if (!p || p == &_mesa_DummyProgram)
      return;

   struct st_variant *v, **prevPtr = &p->variants;
   bool unbound = false;

   for (v = p->variants; v; ) {
      struct st_variant *next = v->next;
      if (v->st == st) {
         if (!unbound) {
            st_unbind_program(st, p);
            unbound = true;
         }

         /* unlink from list */
         *prevPtr = next;
         /* destroy this variant */
         delete_variant(st, v, p->Target);
      }
      else {
         prevPtr = &v->next;
      }
      v = next;
   }
}


/**
 * Callback for _mesa_HashWalk.  Free all the shader's program variants
 * which match the given context.
 */
static void
destroy_shader_program_variants_cb(void *data, void *userData)
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
destroy_program_variants_cb(void *data, void *userData)
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

bool
st_can_add_pointsize_to_program(struct st_context *st, struct gl_program *prog)
{
   nir_shader *nir = prog->nir;
   if (!nir)
      return true; //fixedfunction
   assert(nir->info.stage == MESA_SHADER_VERTEX ||
          nir->info.stage == MESA_SHADER_TESS_EVAL ||
          nir->info.stage == MESA_SHADER_GEOMETRY);
   if (nir->info.outputs_written & VARYING_BIT_PSIZ)
      return false;
   unsigned max_components = nir->info.stage == MESA_SHADER_GEOMETRY ?
                             st->ctx->Const.MaxGeometryTotalOutputComponents :
                             st->ctx->Const.Program[nir->info.stage].MaxOutputComponents;
   unsigned num_components = 0;
   unsigned needed_components = nir->info.stage == MESA_SHADER_GEOMETRY ? nir->info.gs.vertices_out : 1;
   nir_foreach_shader_out_variable(var, nir) {
      num_components += glsl_count_dword_slots(var->type, false);
   }
   /* Ensure that there is enough attribute space to emit at least one primitive */
   if (nir->info.stage == MESA_SHADER_GEOMETRY) {
      if (num_components + needed_components > st->ctx->Const.Program[nir->info.stage].MaxOutputComponents)
         return false;
      num_components *= nir->info.gs.vertices_out;
   }

   return num_components + needed_components <= max_components;
}

/**
 * Compile one shader variant.
 */
static void
st_precompile_shader_variant(struct st_context *st,
                             struct gl_program *prog)
{
   switch (prog->Target) {
   case GL_VERTEX_PROGRAM_ARB:
   case GL_TESS_CONTROL_PROGRAM_NV:
   case GL_TESS_EVALUATION_PROGRAM_NV:
   case GL_GEOMETRY_PROGRAM_NV:
   case GL_COMPUTE_PROGRAM_NV: {
      struct st_common_variant_key key;

      memset(&key, 0, sizeof(key));

      if (st->ctx->API == API_OPENGL_COMPAT &&
          st->clamp_vert_color_in_shader &&
          (prog->info.outputs_written & (VARYING_SLOT_COL0 |
                                         VARYING_SLOT_COL1 |
                                         VARYING_SLOT_BFC0 |
                                         VARYING_SLOT_BFC1))) {
         key.clamp_color = true;
      }

      key.st = st->has_shareable_shaders ? NULL : st;
      st_get_common_variant(st, prog, &key);
      break;
   }

   case GL_FRAGMENT_PROGRAM_ARB: {
      struct st_fp_variant_key key;

      memset(&key, 0, sizeof(key));

      key.st = st->has_shareable_shaders ? NULL : st;
      key.lower_alpha_func = COMPARE_FUNC_ALWAYS;
      if (prog->ati_fs) {
         for (int i = 0; i < ARRAY_SIZE(key.texture_index); i++)
            key.texture_index[i] = TEXTURE_2D_INDEX;
      }
      st_get_fp_variant(st, prog, &key);
      break;
   }

   default:
      assert(0);
   }
}

void
st_serialize_nir(struct gl_program *prog)
{
   if (!prog->serialized_nir) {
      struct blob blob;
      size_t size;

      blob_init(&blob);
      nir_serialize(&blob, prog->nir, false);
      blob_finish_get_buffer(&blob, &prog->serialized_nir, &size);
      prog->serialized_nir_size = size;
   }
}

void
st_finalize_program(struct st_context *st, struct gl_program *prog)
{
   if (st->current_program[prog->info.stage] == prog) {
      if (prog->info.stage == MESA_SHADER_VERTEX) {
         st->ctx->Array.NewVertexElements = true;
         st->dirty |= ST_NEW_VERTEX_PROGRAM(st, prog);
      } else {
         st->dirty |= prog->affected_states;
      }
   }

   if (prog->nir) {
      nir_sweep(prog->nir);

      /* This is only needed for ARB_vp/fp programs and when the disk cache
       * is disabled. If the disk cache is enabled, GLSL programs are
       * serialized in write_nir_to_cache.
       */
      st_serialize_nir(prog);
   }

   /* Always create the default variant of the program. */
   st_precompile_shader_variant(st, prog);
}

/**
 * Called when the program's text/code is changed.  We have to free
 * all shader variants and corresponding gallium shaders when this happens.
 */
GLboolean
st_program_string_notify( struct gl_context *ctx,
                          GLenum target,
                          struct gl_program *prog )
{
   struct st_context *st = st_context(ctx);

   /* GLSL-to-NIR should not end up here. */
   assert(!prog->shader_program);

   st_release_variants(st, prog);

   if (target == GL_FRAGMENT_PROGRAM_ARB ||
       target == GL_FRAGMENT_SHADER_ATI) {
      if (target == GL_FRAGMENT_SHADER_ATI) {
         assert(prog->ati_fs);
         assert(prog->ati_fs->Program == prog);

         st_init_atifs_prog(ctx, prog);
      }

      if (!st_translate_fragment_program(st, prog))
         return false;
   } else if (target == GL_VERTEX_PROGRAM_ARB) {
      if (!st_translate_vertex_program(st, prog))
         return false;
      if (st->lower_point_size && st_can_add_pointsize_to_program(st, prog)) {
         prog->skip_pointsize_xfb = true;
         NIR_PASS_V(prog->nir, st_nir_add_point_size);
      }
   }

   st_finalize_program(st, prog);
   return GL_TRUE;
}
