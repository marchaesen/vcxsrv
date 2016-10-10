/*
 * Copyright © 2015 Red Hat
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
 */

#include "st_nir.h"

#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_context.h"

#include "program/program.h"
#include "program/prog_statevars.h"
#include "program/prog_parameter.h"
#include "program/ir_to_mesa.h"
#include "main/mtypes.h"
#include "main/errors.h"
#include "main/shaderapi.h"
#include "main/uniforms.h"
#include "util/string_to_uint_map.h"

#include "st_context.h"
#include "st_program.h"
#include "st_glsl_types.h"

#include "compiler/nir/nir.h"
#include "compiler/glsl_types.h"
#include "compiler/glsl/glsl_to_nir.h"
#include "compiler/glsl/ir.h"


/* Depending on PIPE_CAP_TGSI_TEXCOORD (st->needs_texcoord_semantic) we
 * may need to fix up varying slots so the glsl->nir path is aligned
 * with the anything->tgsi->nir path.
 */
static void
st_nir_fixup_varying_slots(struct st_context *st, struct exec_list *var_list)
{
   if (st->needs_texcoord_semantic)
      return;

   nir_foreach_variable(var, var_list) {
      if (var->data.location >= VARYING_SLOT_VAR0) {
         var->data.location += 9;
      } else if ((var->data.location >= VARYING_SLOT_TEX0) &&
               (var->data.location <= VARYING_SLOT_TEX7)) {
         var->data.location += VARYING_SLOT_VAR0 - VARYING_SLOT_TEX0;
      }
   }
}

/* input location assignment for VS inputs must be handled specially, so
 * that it is aligned w/ st's vbo state.
 * (This isn't the case with, for ex, FS inputs, which only need to agree
 * on varying-slot w/ the VS outputs)
 */
static void
st_nir_assign_vs_in_locations(struct gl_program *prog, nir_shader *nir)
{
   unsigned attr, num_inputs = 0;
   unsigned input_to_index[VERT_ATTRIB_MAX] = {0};

   /* TODO de-duplicate w/ similar code in st_translate_vertex_program()? */
   for (attr = 0; attr < VERT_ATTRIB_MAX; attr++) {
      if ((prog->InputsRead & BITFIELD64_BIT(attr)) != 0) {
         input_to_index[attr] = num_inputs;
         num_inputs++;
         if ((prog->DoubleInputsRead & BITFIELD64_BIT(attr)) != 0) {
            /* add placeholder for second part of a double attribute */
            num_inputs++;
         }
      } else {
         input_to_index[attr] = ~0;
      }
   }

   nir->num_inputs = 0;
   nir_foreach_variable_safe(var, &nir->inputs) {
      attr = var->data.location;
      assert(attr < ARRAY_SIZE(input_to_index));

      if (input_to_index[attr] != ~0u) {
         var->data.driver_location = input_to_index[attr];
         nir->num_inputs++;
      } else {
         /* Move unused input variables to the globals list (with no
          * initialization), to avoid confusing drivers looking through the
          * inputs array and expecting to find inputs with a driver_location
          * set.
          */
         exec_node_remove(&var->node);
         var->data.mode = nir_var_global;
         exec_list_push_tail(&nir->globals, &var->node);
      }
   }
}

static int
st_nir_lookup_parameter_index(const struct gl_program_parameter_list *params,
                              const char *name)
{
   int loc = _mesa_lookup_parameter_index(params, name);

   /* is there a better way to do this?  If we have something like:
    *
    *    struct S {
    *           float f;
    *           vec4 v;
    *    };
    *    uniform S color;
    *
    * Then what we get in prog->Parameters looks like:
    *
    *    0: Name=color.f, Type=6, DataType=1406, Size=1
    *    1: Name=color.v, Type=6, DataType=8b52, Size=4
    *
    * So the name doesn't match up and _mesa_lookup_parameter_index()
    * fails.  In this case just find the first matching "color.*"..
    *
    * Note for arrays you could end up w/ color[n].f, for example.
    *
    * glsl_to_tgsi works slightly differently in this regard.  It is
    * emitting something more low level, so it just translates the
    * params list 1:1 to CONST[] regs.  Going from GLSL IR to TGSI,
    * it just calculates the additional offset of struct field members
    * in glsl_to_tgsi_visitor::visit(ir_dereference_record *ir) or
    * glsl_to_tgsi_visitor::visit(ir_dereference_array *ir).  It never
    * needs to work backwards to get base var loc from the param-list
    * which already has them separated out.
    */
   if (loc < 0) {
      int namelen = strlen(name);
      for (unsigned i = 0; i < params->NumParameters; i++) {
         struct gl_program_parameter *p = &params->Parameters[i];
         if ((strncmp(p->Name, name, namelen) == 0) &&
             ((p->Name[namelen] == '.') || (p->Name[namelen] == '['))) {
            loc = i;
            break;
         }
      }
   }

   return loc;
}

static void
st_nir_assign_uniform_locations(struct gl_program *prog,
                                struct gl_shader_program *shader_program,
                                struct exec_list *uniform_list, unsigned *size)
{
   int max = 0;
   int shaderidx = 0;

   nir_foreach_variable(uniform, uniform_list) {
      int loc;

      /*
       * UBO's have their own address spaces, so don't count them towards the
       * number of global uniforms
       */
      if ((uniform->data.mode == nir_var_uniform || uniform->data.mode == nir_var_shader_storage) &&
          uniform->interface_type != NULL)
         continue;

      if (uniform->type->is_sampler()) {
         unsigned val;
         bool found = shader_program->UniformHash->get(val, uniform->name);
         loc = shaderidx++;
         assert(found);
         /* this ensure that nir_lower_samplers looks at the correct
          * shader_program->UniformStorage[location]:
          */
         uniform->data.location = val;
      } else if (strncmp(uniform->name, "gl_", 3) == 0) {
         const gl_state_index *const stateTokens = (gl_state_index *)uniform->state_slots[0].tokens;
         /* This state reference has already been setup by ir_to_mesa, but we'll
          * get the same index back here.
          */
         loc = _mesa_add_state_reference(prog->Parameters, stateTokens);
      } else {
         loc = st_nir_lookup_parameter_index(prog->Parameters, uniform->name);
      }

      uniform->data.driver_location = loc;

      max = MAX2(max, loc + st_glsl_type_size(uniform->type));
   }
   *size = max;
}

extern "C" {

/* First half of converting glsl_to_nir.. this leaves things in a pre-
 * nir_lower_io state, so that shader variants can more easily insert/
 * replace variables, etc.
 */
nir_shader *
st_glsl_to_nir(struct st_context *st, struct gl_program *prog,
               struct gl_shader_program *shader_program,
               gl_shader_stage stage)
{
   struct pipe_screen *pscreen = st->pipe->screen;
   enum pipe_shader_type ptarget = st_shader_stage_to_ptarget(stage);
   const nir_shader_compiler_options *options;
   nir_shader *nir;

   assert(pscreen->get_compiler_options);   /* drivers using NIR must implement this */

   options = (const nir_shader_compiler_options *)
      pscreen->get_compiler_options(pscreen, PIPE_SHADER_IR_NIR, ptarget);
   assert(options);

   if (prog->nir)
      return prog->nir;

   nir = glsl_to_nir(shader_program, stage, options);
   prog->nir = nir;

   NIR_PASS_V(nir, nir_lower_io_to_temporaries,
         nir_shader_get_entrypoint(nir),
         true, true);
   NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, st_nir_lower_builtin);

   /* fragment shaders may need : */
   if (stage == MESA_SHADER_FRAGMENT) {
      static const gl_state_index wposTransformState[STATE_LENGTH] = {
         STATE_INTERNAL, STATE_FB_WPOS_Y_TRANSFORM
      };
      nir_lower_wpos_ytransform_options wpos_options = {0};
      struct pipe_screen *pscreen = st->pipe->screen;

      memcpy(wpos_options.state_tokens, wposTransformState,
             sizeof(wpos_options.state_tokens));
      wpos_options.fs_coord_origin_upper_left =
         pscreen->get_param(pscreen, PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT);
      wpos_options.fs_coord_origin_lower_left =
         pscreen->get_param(pscreen, PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT);
      wpos_options.fs_coord_pixel_center_integer =
         pscreen->get_param(pscreen, PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER);
      wpos_options.fs_coord_pixel_center_half_integer =
         pscreen->get_param(pscreen, PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER);

      if (nir_lower_wpos_ytransform(nir, &wpos_options)) {
         nir_validate_shader(nir);
         _mesa_add_state_reference(prog->Parameters, wposTransformState);
      }
   }

   if (st->ctx->_Shader->Flags & GLSL_DUMP) {
      _mesa_log("\n");
      _mesa_log("NIR IR for linked %s program %d:\n",
             _mesa_shader_stage_to_string(stage),
             shader_program->Name);
      nir_print_shader(nir, _mesa_get_log_file());
      _mesa_log("\n\n");
   }

   return nir;
}

/* TODO any better helper somewhere to sort a list? */

static void
insert_sorted(struct exec_list *var_list, nir_variable *new_var)
{
   nir_foreach_variable(var, var_list) {
      if (var->data.location > new_var->data.location) {
         exec_node_insert_node_before(&var->node, &new_var->node);
         return;
      }
   }
   exec_list_push_tail(var_list, &new_var->node);
}

static void
sort_varyings(struct exec_list *var_list)
{
   struct exec_list new_list;
   exec_list_make_empty(&new_list);
   nir_foreach_variable_safe(var, var_list) {
      exec_node_remove(&var->node);
      insert_sorted(&new_list, var);
   }
   exec_list_move_nodes_to(&new_list, var_list);
}

/* Second half of preparing nir from glsl, which happens after shader
 * variant lowering.
 */
void
st_finalize_nir(struct st_context *st, struct gl_program *prog, nir_shader *nir)
{
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, nir_lower_io_types);

   if (nir->stage == MESA_SHADER_VERTEX) {
      /* Needs special handling so drvloc matches the vbo state: */
      st_nir_assign_vs_in_locations(prog, nir);
      /* Re-lower global vars, to deal with any dead VS inputs. */
      NIR_PASS_V(nir, nir_lower_global_vars_to_local);

      sort_varyings(&nir->outputs);
      nir_assign_var_locations(&nir->outputs,
                               &nir->num_outputs,
                               VARYING_SLOT_VAR0,
                               st_glsl_type_size);
      st_nir_fixup_varying_slots(st, &nir->outputs);
   } else if (nir->stage == MESA_SHADER_FRAGMENT) {
      sort_varyings(&nir->inputs);
      nir_assign_var_locations(&nir->inputs,
                               &nir->num_inputs,
                               VARYING_SLOT_VAR0,
                               st_glsl_type_size);
      st_nir_fixup_varying_slots(st, &nir->inputs);
      nir_assign_var_locations(&nir->outputs,
                               &nir->num_outputs,
                               FRAG_RESULT_DATA0,
                               st_glsl_type_size);
   } else {
      unreachable("invalid shader type for tgsi bypass\n");
   }

   struct gl_shader_program *shader_program;
   switch (nir->stage) {
   case MESA_SHADER_VERTEX:
      shader_program = ((struct st_vertex_program *)prog)->shader_program;
      break;
   case MESA_SHADER_FRAGMENT:
      shader_program = ((struct st_fragment_program *)prog)->shader_program;
      break;
   default:
      assert(!"should not be reached");
      return;
   }

   st_nir_assign_uniform_locations(prog, shader_program,
                                   &nir->uniforms, &nir->num_uniforms);

   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_io, nir_var_all, st_glsl_type_size,
              (nir_lower_io_options)0);
   NIR_PASS_V(nir, nir_lower_samplers, shader_program);
}

struct gl_program *
st_nir_get_mesa_program(struct gl_context *ctx,
                        struct gl_shader_program *shader_program,
                        struct gl_linked_shader *shader)
{
   struct gl_program *prog;
   GLenum target = _mesa_shader_stage_to_program(shader->Stage);

   validate_ir_tree(shader->ir);

   prog = ctx->Driver.NewProgram(ctx, target, shader_program->Name);
   if (!prog)
      return NULL;

   prog->Parameters = _mesa_new_parameter_list();

   _mesa_copy_linked_program_data(shader->Stage, shader_program, prog);
   _mesa_generate_parameters_list_for_uniforms(shader_program, shader,
                                               prog->Parameters);

   /* Make a pass over the IR to add state references for any built-in
    * uniforms that are used.  This has to be done now (during linking).
    * Code generation doesn't happen until the first time this shader is
    * used for rendering.  Waiting until then to generate the parameters is
    * too late.  At that point, the values for the built-in uniforms won't
    * get sent to the shader.
    */
   foreach_in_list(ir_instruction, node, shader->ir) {
      ir_variable *var = node->as_variable();

      if ((var == NULL) || (var->data.mode != ir_var_uniform) ||
          (strncmp(var->name, "gl_", 3) != 0))
         continue;

      const ir_state_slot *const slots = var->get_state_slots();
      assert(slots != NULL);

      for (unsigned int i = 0; i < var->get_num_state_slots(); i++) {
         _mesa_add_state_reference(prog->Parameters,
                                   (gl_state_index *) slots[i].tokens);
      }
   }

   if (ctx->_Shader->Flags & GLSL_DUMP) {
      _mesa_log("\n");
      _mesa_log("GLSL IR for linked %s program %d:\n",
             _mesa_shader_stage_to_string(shader->Stage),
             shader_program->Name);
      _mesa_print_ir(_mesa_get_log_file(), shader->ir, NULL);
      _mesa_log("\n\n");
   }

   prog->Instructions = NULL;
   prog->NumInstructions = 0;

   do_set_program_inouts(shader->ir, prog, shader->Stage);

   prog->SamplersUsed = shader->active_samplers;
   prog->ShadowSamplers = shader->shadow_samplers;
   prog->ExternalSamplersUsed = gl_external_samplers(shader);
   _mesa_update_shader_textures_used(shader_program, prog);

   _mesa_reference_program(ctx, &shader->Program, prog);

   /* Avoid reallocation of the program parameter list, because the uniform
    * storage is only associated with the original parameter list.
    * This should be enough for Bitmap and DrawPixels constants.
    */
   _mesa_reserve_parameter_storage(prog->Parameters, 8);

   /* This has to be done last.  Any operation the can cause
    * prog->ParameterValues to get reallocated (e.g., anything that adds a
    * program constant) has to happen before creating this linkage.
    */
   _mesa_associate_uniform_storage(ctx, shader_program, prog->Parameters);

   struct st_vertex_program *stvp;
   struct st_fragment_program *stfp;

   switch (shader->Stage) {
   case MESA_SHADER_VERTEX:
      stvp = (struct st_vertex_program *)prog;
      stvp->shader_program = shader_program;
      break;
   case MESA_SHADER_FRAGMENT:
      stfp = (struct st_fragment_program *)prog;
      stfp->shader_program = shader_program;
      break;
   default:
      assert(!"should not be reached");
      return NULL;
   }

   return prog;
}

} /* extern "C" */
