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

#include "st_context.h"
#include "st_glsl_types.h"
#include "st_program.h"

#include "compiler/nir/nir.h"
#include "compiler/glsl_types.h"
#include "compiler/glsl/glsl_to_nir.h"
#include "compiler/glsl/gl_nir.h"
#include "compiler/glsl/ir.h"
#include "compiler/glsl/string_to_uint_map.h"


static int
type_size(const struct glsl_type *type)
{
   return type->count_attribute_slots(false);
}

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
      if ((prog->info.inputs_read & BITFIELD64_BIT(attr)) != 0) {
         input_to_index[attr] = num_inputs;
         num_inputs++;
         if ((prog->info.vs.double_inputs_read & BITFIELD64_BIT(attr)) != 0) {
            /* add placeholder for second part of a double attribute */
            num_inputs++;
         }
      } else {
         input_to_index[attr] = ~0;
      }
   }

   /* bit of a hack, mirroring st_translate_vertex_program */
   input_to_index[VERT_ATTRIB_EDGEFLAG] = num_inputs;

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

static void
st_nir_assign_var_locations(struct exec_list *var_list, unsigned *size,
                            gl_shader_stage stage)
{
   unsigned location = 0;
   unsigned assigned_locations[VARYING_SLOT_TESS_MAX];
   uint64_t processed_locs[2] = {0};

   const int base = stage == MESA_SHADER_FRAGMENT ?
      (int) FRAG_RESULT_DATA0 : (int) VARYING_SLOT_VAR0;

   int UNUSED last_loc = 0;
   nir_foreach_variable(var, var_list) {

      const struct glsl_type *type = var->type;
      if (nir_is_per_vertex_io(var, stage)) {
         assert(glsl_type_is_array(type));
         type = glsl_get_array_element(type);
      }

      unsigned var_size = type_size(type);

      /* Builtins don't allow component packing so we only need to worry about
       * user defined varyings sharing the same location.
       */
      bool processed = false;
      if (var->data.location >= base) {
         unsigned glsl_location = var->data.location - base;

         for (unsigned i = 0; i < var_size; i++) {
            if (processed_locs[var->data.index] &
                ((uint64_t)1 << (glsl_location + i)))
               processed = true;
            else
               processed_locs[var->data.index] |=
                  ((uint64_t)1 << (glsl_location + i));
         }
      }

      /* Because component packing allows varyings to share the same location
       * we may have already have processed this location.
       */
      if (processed) {
         unsigned driver_location = assigned_locations[var->data.location];
         var->data.driver_location = driver_location;
         *size += type_size(type);

         /* An array may be packed such that is crosses multiple other arrays
          * or variables, we need to make sure we have allocated the elements
          * consecutively if the previously proccessed var was shorter than
          * the current array we are processing.
          *
          * NOTE: The code below assumes the var list is ordered in ascending
          * location order.
          */
         assert(last_loc <= var->data.location);
         last_loc = var->data.location;
         unsigned last_slot_location = driver_location + var_size;
         if (last_slot_location > location) {
            unsigned num_unallocated_slots = last_slot_location - location;
            unsigned first_unallocated_slot = var_size - num_unallocated_slots;
            for (unsigned i = first_unallocated_slot; i < num_unallocated_slots; i++) {
               assigned_locations[var->data.location + i] = location;
               location++;
            }
         }
         continue;
      }

      for (unsigned i = 0; i < var_size; i++) {
         assigned_locations[var->data.location + i] = location + i;
      }

      var->data.driver_location = location;
      location += var_size;
   }

   *size += location;
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
st_nir_assign_uniform_locations(struct gl_context *ctx,
                                struct gl_program *prog,
                                struct gl_shader_program *shader_program,
                                struct exec_list *uniform_list, unsigned *size)
{
   int max = 0;
   int shaderidx = 0;
   int imageidx = 0;

   nir_foreach_variable(uniform, uniform_list) {
      int loc;

      /*
       * UBO's have their own address spaces, so don't count them towards the
       * number of global uniforms
       */
      if ((uniform->data.mode == nir_var_uniform || uniform->data.mode == nir_var_shader_storage) &&
          uniform->interface_type != NULL)
         continue;

      if (!uniform->data.bindless &&
          (uniform->type->is_sampler() || uniform->type->is_image())) {
         if (uniform->type->is_sampler())
            loc = shaderidx++;
         else
            loc = imageidx++;
      } else if (strncmp(uniform->name, "gl_", 3) == 0) {
         const gl_state_index16 *const stateTokens = uniform->state_slots[0].tokens;
         /* This state reference has already been setup by ir_to_mesa, but we'll
          * get the same index back here.
          */

         unsigned comps;
         const struct glsl_type *type = glsl_without_array(uniform->type);
         if (glsl_type_is_struct(type)) {
            comps = 4;
         } else {
            comps = glsl_get_vector_elements(type);
         }

         if (ctx->Const.PackedDriverUniformStorage) {
            loc = _mesa_add_sized_state_reference(prog->Parameters,
                                                  stateTokens, comps, false);
            loc = prog->Parameters->ParameterValueOffset[loc];
         } else {
            loc = _mesa_add_state_reference(prog->Parameters, stateTokens);
         }
      } else {
         loc = st_nir_lookup_parameter_index(prog->Parameters, uniform->name);

         if (ctx->Const.PackedDriverUniformStorage) {
            loc = prog->Parameters->ParameterValueOffset[loc];
         }
      }

      uniform->data.driver_location = loc;

      max = MAX2(max, loc + type_size(uniform->type));
   }
   *size = max;
}

void
st_nir_opts(nir_shader *nir)
{
   bool progress;
   do {
      progress = false;

      NIR_PASS_V(nir, nir_lower_vars_to_ssa);
      NIR_PASS_V(nir, nir_lower_alu_to_scalar);
      NIR_PASS_V(nir, nir_lower_phis_to_scalar);

      NIR_PASS_V(nir, nir_lower_pack);
      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_dce);
      if (nir_opt_trivial_continues(nir)) {
         progress = true;
         NIR_PASS(progress, nir, nir_copy_prop);
         NIR_PASS(progress, nir, nir_opt_dce);
      }
      NIR_PASS(progress, nir, nir_opt_if);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_peephole_select, 8);

      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      NIR_PASS(progress, nir, nir_opt_undef);
      NIR_PASS(progress, nir, nir_opt_conditional_discard);
      if (nir->options->max_unroll_iterations) {
         NIR_PASS(progress, nir, nir_opt_loop_unroll, (nir_variable_mode)0);
      }
   } while (progress);
}

/* First third of converting glsl_to_nir.. this leaves things in a pre-
 * nir_lower_io state, so that shader variants can more easily insert/
 * replace variables, etc.
 */
static nir_shader *
st_glsl_to_nir(struct st_context *st, struct gl_program *prog,
               struct gl_shader_program *shader_program,
               gl_shader_stage stage)
{
   const nir_shader_compiler_options *options =
      st->ctx->Const.ShaderCompilerOptions[prog->info.stage].NirOptions;
   assert(options);

   if (prog->nir)
      return prog->nir;

   nir_shader *nir = glsl_to_nir(shader_program, stage, options);

   /* Set the next shader stage hint for VS and TES. */
   if (!nir->info.separate_shader &&
       (nir->info.stage == MESA_SHADER_VERTEX ||
        nir->info.stage == MESA_SHADER_TESS_EVAL)) {

      unsigned prev_stages = (1 << (prog->info.stage + 1)) - 1;
      unsigned stages_mask =
         ~prev_stages & shader_program->data->linked_stages;

      nir->info.next_stage = stages_mask ?
         (gl_shader_stage) ffs(stages_mask) : MESA_SHADER_FRAGMENT;
   } else {
      nir->info.next_stage = MESA_SHADER_FRAGMENT;
   }

   nir_variable_mode mask =
      (nir_variable_mode) (nir_var_shader_in | nir_var_shader_out);
   nir_remove_dead_variables(nir, mask);

   if (options->lower_all_io_to_temps ||
       nir->info.stage == MESA_SHADER_VERTEX ||
       nir->info.stage == MESA_SHADER_GEOMETRY) {
      NIR_PASS_V(nir, nir_lower_io_to_temporaries,
                 nir_shader_get_entrypoint(nir),
                 true, true);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS_V(nir, nir_lower_io_to_temporaries,
                 nir_shader_get_entrypoint(nir),
                 true, false);
   }

   NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);

   st_nir_opts(nir);

   return nir;
}

/* Second third of converting glsl_to_nir. This creates uniforms, gathers
 * info on varyings, etc after NIR link time opts have been applied.
 */
static void
st_glsl_to_nir_post_opts(struct st_context *st, struct gl_program *prog,
                         struct gl_shader_program *shader_program)
{
   nir_shader *nir = prog->nir;

   /* Make a pass over the IR to add state references for any built-in
    * uniforms that are used.  This has to be done now (during linking).
    * Code generation doesn't happen until the first time this shader is
    * used for rendering.  Waiting until then to generate the parameters is
    * too late.  At that point, the values for the built-in uniforms won't
    * get sent to the shader.
    */
   nir_foreach_variable(var, &nir->uniforms) {
      if (strncmp(var->name, "gl_", 3) == 0) {
         const nir_state_slot *const slots = var->state_slots;
         assert(var->state_slots != NULL);

         const struct glsl_type *type = glsl_without_array(var->type);
         for (unsigned int i = 0; i < var->num_state_slots; i++) {
            unsigned comps;
            if (glsl_type_is_struct(type)) {
               /* Builtin struct require specical handling for now we just
                * make all members vec4. See st_nir_lower_builtin.
                */
               comps = 4;
            } else {
               comps = glsl_get_vector_elements(type);
            }

            if (st->ctx->Const.PackedDriverUniformStorage) {
               _mesa_add_sized_state_reference(prog->Parameters,
                                               slots[i].tokens,
                                               comps, false);
            } else {
               _mesa_add_state_reference(prog->Parameters,
                                         slots[i].tokens);
            }
         }
      }
   }

   /* Avoid reallocation of the program parameter list, because the uniform
    * storage is only associated with the original parameter list.
    * This should be enough for Bitmap and DrawPixels constants.
    */
   _mesa_reserve_parameter_storage(prog->Parameters, 8);

   /* This has to be done last.  Any operation the can cause
    * prog->ParameterValues to get reallocated (e.g., anything that adds a
    * program constant) has to happen before creating this linkage.
    */
   _mesa_associate_uniform_storage(st->ctx, shader_program, prog, true);

   st_set_prog_affected_state_flags(prog);

   NIR_PASS_V(nir, st_nir_lower_builtin);
   NIR_PASS_V(nir, gl_nir_lower_atomics, shader_program, true);

   if (st->ctx->_Shader->Flags & GLSL_DUMP) {
      _mesa_log("\n");
      _mesa_log("NIR IR for linked %s program %d:\n",
             _mesa_shader_stage_to_string(prog->info.stage),
             shader_program->Name);
      nir_print_shader(nir, _mesa_get_log_file());
      _mesa_log("\n\n");
   }
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

static void
set_st_program(struct gl_program *prog,
               struct gl_shader_program *shader_program,
               nir_shader *nir)
{
   struct st_vertex_program *stvp;
   struct st_common_program *stp;
   struct st_fragment_program *stfp;
   struct st_compute_program *stcp;

   switch (prog->info.stage) {
   case MESA_SHADER_VERTEX:
      stvp = (struct st_vertex_program *)prog;
      stvp->shader_program = shader_program;
      stvp->tgsi.type = PIPE_SHADER_IR_NIR;
      stvp->tgsi.ir.nir = nir;
      break;
   case MESA_SHADER_GEOMETRY:
   case MESA_SHADER_TESS_CTRL:
   case MESA_SHADER_TESS_EVAL:
      stp = (struct st_common_program *)prog;
      stp->shader_program = shader_program;
      stp->tgsi.type = PIPE_SHADER_IR_NIR;
      stp->tgsi.ir.nir = nir;
      break;
   case MESA_SHADER_FRAGMENT:
      stfp = (struct st_fragment_program *)prog;
      stfp->shader_program = shader_program;
      stfp->tgsi.type = PIPE_SHADER_IR_NIR;
      stfp->tgsi.ir.nir = nir;
      break;
   case MESA_SHADER_COMPUTE:
      stcp = (struct st_compute_program *)prog;
      stcp->shader_program = shader_program;
      stcp->tgsi.ir_type = PIPE_SHADER_IR_NIR;
      stcp->tgsi.prog = nir;
      break;
   default:
      unreachable("unknown shader stage");
   }
}

static void
st_nir_get_mesa_program(struct gl_context *ctx,
                        struct gl_shader_program *shader_program,
                        struct gl_linked_shader *shader)
{
   struct st_context *st = st_context(ctx);
   struct gl_program *prog;

   validate_ir_tree(shader->ir);

   prog = shader->Program;

   prog->Parameters = _mesa_new_parameter_list();

   _mesa_copy_linked_program_data(shader_program, shader);
   _mesa_generate_parameters_list_for_uniforms(ctx, shader_program, shader,
                                               prog->Parameters);

   if (ctx->_Shader->Flags & GLSL_DUMP) {
      _mesa_log("\n");
      _mesa_log("GLSL IR for linked %s program %d:\n",
             _mesa_shader_stage_to_string(shader->Stage),
             shader_program->Name);
      _mesa_print_ir(_mesa_get_log_file(), shader->ir, NULL);
      _mesa_log("\n\n");
   }

   prog->ExternalSamplersUsed = gl_external_samplers(prog);
   _mesa_update_shader_textures_used(shader_program, prog);

   nir_shader *nir = st_glsl_to_nir(st, prog, shader_program, shader->Stage);

   set_st_program(prog, shader_program, nir);
   prog->nir = nir;
}

static void
st_nir_link_shaders(nir_shader **producer, nir_shader **consumer)
{
   nir_lower_io_arrays_to_elements(*producer, *consumer);

   NIR_PASS_V(*producer, nir_remove_dead_variables, nir_var_shader_out);
   NIR_PASS_V(*consumer, nir_remove_dead_variables, nir_var_shader_in);

   if (nir_remove_unused_varyings(*producer, *consumer)) {
      NIR_PASS_V(*producer, nir_lower_global_vars_to_local);
      NIR_PASS_V(*consumer, nir_lower_global_vars_to_local);

      /* The backend might not be able to handle indirects on
       * temporaries so we need to lower indirects on any of the
       * varyings we have demoted here.
       *
       * TODO: radeonsi shouldn't need to do this, however LLVM isn't
       * currently smart enough to handle indirects without causing excess
       * spilling causing the gpu to hang.
       *
       * See the following thread for more details of the problem:
       * https://lists.freedesktop.org/archives/mesa-dev/2017-July/162106.html
       */
      nir_variable_mode indirect_mask = nir_var_local;

      NIR_PASS_V(*producer, nir_lower_indirect_derefs, indirect_mask);
      NIR_PASS_V(*consumer, nir_lower_indirect_derefs, indirect_mask);

      st_nir_opts(*producer);
      st_nir_opts(*consumer);
   }
}

extern "C" {

bool
st_link_nir(struct gl_context *ctx,
            struct gl_shader_program *shader_program)
{
   struct st_context *st = st_context(ctx);

   /* Determine first and last stage. */
   unsigned first = MESA_SHADER_STAGES;
   unsigned last = 0;
   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      if (!shader_program->_LinkedShaders[i])
         continue;
      if (first == MESA_SHADER_STAGES)
         first = i;
      last = i;
   }

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      struct gl_linked_shader *shader = shader_program->_LinkedShaders[i];
      if (shader == NULL)
         continue;

      st_nir_get_mesa_program(ctx, shader_program, shader);

      nir_variable_mode mask = (nir_variable_mode) 0;
      if (i != first)
         mask = (nir_variable_mode)(mask | nir_var_shader_in);

      if (i != last)
         mask = (nir_variable_mode)(mask | nir_var_shader_out);

      nir_shader *nir = shader->Program->nir;
      NIR_PASS_V(nir, nir_lower_io_to_scalar_early, mask);
      st_nir_opts(nir);
   }

   /* Linking the stages in the opposite order (from fragment to vertex)
    * ensures that inter-shader outputs written to in an earlier stage
    * are eliminated if they are (transitively) not used in a later
    * stage.
    */
   int next = last;
   for (int i = next - 1; i >= 0; i--) {
      struct gl_linked_shader *shader = shader_program->_LinkedShaders[i];
      if (shader == NULL)
         continue;

      st_nir_link_shaders(&shader->Program->nir,
                          &shader_program->_LinkedShaders[next]->Program->nir);
      next = i;
   }

   int prev = -1;
   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      struct gl_linked_shader *shader = shader_program->_LinkedShaders[i];
      if (shader == NULL)
         continue;

      nir_shader *nir = shader->Program->nir;

      /* fragment shaders may need : */
      if (nir->info.stage == MESA_SHADER_FRAGMENT) {
         static const gl_state_index16 wposTransformState[STATE_LENGTH] = {
            STATE_INTERNAL, STATE_FB_WPOS_Y_TRANSFORM
         };
         nir_lower_wpos_ytransform_options wpos_options = { { 0 } };
         struct pipe_screen *pscreen = st->pipe->screen;

         memcpy(wpos_options.state_tokens, wposTransformState,
                sizeof(wpos_options.state_tokens));
         wpos_options.fs_coord_origin_upper_left =
            pscreen->get_param(pscreen,
                               PIPE_CAP_TGSI_FS_COORD_ORIGIN_UPPER_LEFT);
         wpos_options.fs_coord_origin_lower_left =
            pscreen->get_param(pscreen,
                               PIPE_CAP_TGSI_FS_COORD_ORIGIN_LOWER_LEFT);
         wpos_options.fs_coord_pixel_center_integer =
            pscreen->get_param(pscreen,
                               PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_INTEGER);
         wpos_options.fs_coord_pixel_center_half_integer =
            pscreen->get_param(pscreen,
                               PIPE_CAP_TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER);

         if (nir_lower_wpos_ytransform(nir, &wpos_options)) {
            nir_validate_shader(nir);
            _mesa_add_state_reference(shader->Program->Parameters,
                                      wposTransformState);
         }
      }

      NIR_PASS_V(nir, nir_lower_system_values);

      nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
      shader->Program->info = nir->info;

      if (prev != -1) {
         nir_compact_varyings(shader_program->_LinkedShaders[prev]->Program->nir,
                              nir, ctx->API != API_OPENGL_COMPAT);
      }
      prev = i;
   }

   for (unsigned i = 0; i < MESA_SHADER_STAGES; i++) {
      struct gl_linked_shader *shader = shader_program->_LinkedShaders[i];
      if (shader == NULL)
         continue;

      st_glsl_to_nir_post_opts(st, shader->Program, shader_program);

      assert(shader->Program);
      if (!ctx->Driver.ProgramStringNotify(ctx,
                                           _mesa_shader_stage_to_program(i),
                                           shader->Program)) {
         _mesa_reference_program(ctx, &shader->Program, NULL);
         return false;
      }
   }

   return true;
}

/* Last third of preparing nir from glsl, which happens after shader
 * variant lowering.
 */
void
st_finalize_nir(struct st_context *st, struct gl_program *prog,
                struct gl_shader_program *shader_program, nir_shader *nir)
{
   struct pipe_screen *screen = st->pipe->screen;
   const nir_shader_compiler_options *options =
      st->ctx->Const.ShaderCompilerOptions[prog->info.stage].NirOptions;

   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);
   if (options->lower_all_io_to_temps ||
       nir->info.stage == MESA_SHADER_VERTEX ||
       nir->info.stage == MESA_SHADER_GEOMETRY) {
      NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, true);
   }

   if (nir->info.stage == MESA_SHADER_VERTEX) {
      /* Needs special handling so drvloc matches the vbo state: */
      st_nir_assign_vs_in_locations(prog, nir);
      /* Re-lower global vars, to deal with any dead VS inputs. */
      NIR_PASS_V(nir, nir_lower_global_vars_to_local);

      sort_varyings(&nir->outputs);
      st_nir_assign_var_locations(&nir->outputs,
                                  &nir->num_outputs,
                                  nir->info.stage);
      st_nir_fixup_varying_slots(st, &nir->outputs);
   } else if (nir->info.stage == MESA_SHADER_GEOMETRY ||
              nir->info.stage == MESA_SHADER_TESS_CTRL ||
              nir->info.stage == MESA_SHADER_TESS_EVAL) {
      sort_varyings(&nir->inputs);
      st_nir_assign_var_locations(&nir->inputs,
                                  &nir->num_inputs,
                                  nir->info.stage);
      st_nir_fixup_varying_slots(st, &nir->inputs);

      sort_varyings(&nir->outputs);
      st_nir_assign_var_locations(&nir->outputs,
                                  &nir->num_outputs,
                                  nir->info.stage);
      st_nir_fixup_varying_slots(st, &nir->outputs);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      sort_varyings(&nir->inputs);
      st_nir_assign_var_locations(&nir->inputs,
                                  &nir->num_inputs,
                                  nir->info.stage);
      st_nir_fixup_varying_slots(st, &nir->inputs);
      st_nir_assign_var_locations(&nir->outputs,
                                  &nir->num_outputs,
                                  nir->info.stage);
   } else if (nir->info.stage == MESA_SHADER_COMPUTE) {
       /* TODO? */
   } else {
      unreachable("invalid shader type for tgsi bypass\n");
   }

   NIR_PASS_V(nir, nir_lower_atomics_to_ssbo,
         st->ctx->Const.Program[nir->info.stage].MaxAtomicBuffers);

   st_nir_assign_uniform_locations(st->ctx, prog, shader_program,
                                   &nir->uniforms, &nir->num_uniforms);

   if (st->ctx->Const.PackedDriverUniformStorage) {
      NIR_PASS_V(nir, nir_lower_io, nir_var_uniform, st_glsl_type_dword_size,
                 (nir_lower_io_options)0);
      NIR_PASS_V(nir, st_nir_lower_uniforms_to_ubo);
   }

   if (screen->get_param(screen, PIPE_CAP_NIR_SAMPLERS_AS_DEREF))
      NIR_PASS_V(nir, gl_nir_lower_samplers_as_deref, shader_program);
   else
      NIR_PASS_V(nir, gl_nir_lower_samplers, shader_program);
}

} /* extern "C" */
