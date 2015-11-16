/*
 * Copyright © 2014 Intel Corporation
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
 *
 * Authors:
 *    Connor Abbott (cwabbott0@gmail.com)
 *
 */

#include "glsl_to_nir.h"
#include "nir_control_flow.h"
#include "nir_builder.h"
#include "ir_visitor.h"
#include "ir_hierarchical_visitor.h"
#include "ir.h"
#include "main/imports.h"

/*
 * pass to lower GLSL IR to NIR
 *
 * This will lower variable dereferences to loads/stores of corresponding
 * variables in NIR - the variables will be converted to registers in a later
 * pass.
 */

namespace {

class nir_visitor : public ir_visitor
{
public:
   nir_visitor(nir_shader *shader);
   ~nir_visitor();

   virtual void visit(ir_variable *);
   virtual void visit(ir_function *);
   virtual void visit(ir_function_signature *);
   virtual void visit(ir_loop *);
   virtual void visit(ir_if *);
   virtual void visit(ir_discard *);
   virtual void visit(ir_loop_jump *);
   virtual void visit(ir_return *);
   virtual void visit(ir_call *);
   virtual void visit(ir_assignment *);
   virtual void visit(ir_emit_vertex *);
   virtual void visit(ir_end_primitive *);
   virtual void visit(ir_expression *);
   virtual void visit(ir_swizzle *);
   virtual void visit(ir_texture *);
   virtual void visit(ir_constant *);
   virtual void visit(ir_dereference_variable *);
   virtual void visit(ir_dereference_record *);
   virtual void visit(ir_dereference_array *);
   virtual void visit(ir_barrier *);

   void create_function(ir_function *ir);

private:
   void create_overload(ir_function_signature *ir, nir_function *function);
   void add_instr(nir_instr *instr, unsigned num_components);
   nir_ssa_def *evaluate_rvalue(ir_rvalue *ir);

   nir_alu_instr *emit(nir_op op, unsigned dest_size, nir_ssa_def **srcs);
   nir_alu_instr *emit(nir_op op, unsigned dest_size, nir_ssa_def *src1);
   nir_alu_instr *emit(nir_op op, unsigned dest_size, nir_ssa_def *src1,
                       nir_ssa_def *src2);
   nir_alu_instr *emit(nir_op op, unsigned dest_size, nir_ssa_def *src1,
                       nir_ssa_def *src2, nir_ssa_def *src3);

   bool supports_ints;

   nir_shader *shader;
   nir_function_impl *impl;
   nir_builder b;
   nir_ssa_def *result; /* result of the expression tree last visited */

   nir_deref_var *evaluate_deref(nir_instr *mem_ctx, ir_instruction *ir);

   /* the head of the dereference chain we're creating */
   nir_deref_var *deref_head;
   /* the tail of the dereference chain we're creating */
   nir_deref *deref_tail;

   nir_variable *var; /* variable created by ir_variable visitor */

   /* whether the IR we're operating on is per-function or global */
   bool is_global;

   /* map of ir_variable -> nir_variable */
   struct hash_table *var_table;

   /* map of ir_function_signature -> nir_function_overload */
   struct hash_table *overload_table;
};

/*
 * This visitor runs before the main visitor, calling create_function() for
 * each function so that the main visitor can resolve forward references in
 * calls.
 */

class nir_function_visitor : public ir_hierarchical_visitor
{
public:
   nir_function_visitor(nir_visitor *v) : visitor(v)
   {
   }
   virtual ir_visitor_status visit_enter(ir_function *);

private:
   nir_visitor *visitor;
};

}; /* end of anonymous namespace */

nir_shader *
glsl_to_nir(const struct gl_shader_program *shader_prog,
            gl_shader_stage stage,
            const nir_shader_compiler_options *options)
{
   struct gl_shader *sh = shader_prog->_LinkedShaders[stage];

   nir_shader *shader = nir_shader_create(NULL, stage, options);

   nir_visitor v1(shader);
   nir_function_visitor v2(&v1);
   v2.run(sh->ir);
   visit_exec_list(sh->ir, &v1);

   nir_lower_outputs_to_temporaries(shader);

   shader->info.name = ralloc_asprintf(shader, "GLSL%d", shader_prog->Name);
   if (shader_prog->Label)
      shader->info.label = ralloc_strdup(shader, shader_prog->Label);
   shader->info.num_textures = _mesa_fls(sh->Program->SamplersUsed);
   shader->info.num_ubos = sh->NumUniformBlocks;
   shader->info.num_abos = shader_prog->NumAtomicBuffers;
   shader->info.num_ssbos = sh->NumShaderStorageBlocks;
   shader->info.num_images = sh->NumImages;
   shader->info.inputs_read = sh->Program->InputsRead;
   shader->info.outputs_written = sh->Program->OutputsWritten;
   shader->info.patch_inputs_read = sh->Program->PatchInputsRead;
   shader->info.patch_outputs_written = sh->Program->PatchOutputsWritten;
   shader->info.system_values_read = sh->Program->SystemValuesRead;
   shader->info.uses_texture_gather = sh->Program->UsesGather;
   shader->info.uses_clip_distance_out =
      sh->Program->ClipDistanceArraySize != 0;
   shader->info.separate_shader = shader_prog->SeparateShader;
   shader->info.has_transform_feedback_varyings =
      shader_prog->TransformFeedback.NumVarying > 0;

   switch (stage) {
   case MESA_SHADER_GEOMETRY:
      shader->info.gs.vertices_in = shader_prog->Geom.VerticesIn;
      shader->info.gs.output_primitive = sh->Geom.OutputType;
      shader->info.gs.vertices_out = sh->Geom.VerticesOut;
      shader->info.gs.invocations = sh->Geom.Invocations;
      shader->info.gs.uses_end_primitive = shader_prog->Geom.UsesEndPrimitive;
      shader->info.gs.uses_streams = shader_prog->Geom.UsesStreams;
      break;

   case MESA_SHADER_FRAGMENT: {
      struct gl_fragment_program *fp =
         (struct gl_fragment_program *)sh->Program;

      shader->info.fs.uses_discard = fp->UsesKill;
      shader->info.fs.early_fragment_tests = sh->EarlyFragmentTests;
      shader->info.fs.depth_layout = fp->FragDepthLayout;
      break;
   }

   case MESA_SHADER_COMPUTE: {
      struct gl_compute_program *cp = (struct gl_compute_program *)sh->Program;
      shader->info.cs.local_size[0] = cp->LocalSize[0];
      shader->info.cs.local_size[1] = cp->LocalSize[1];
      shader->info.cs.local_size[2] = cp->LocalSize[2];
      break;
   }

   default:
      break; /* No stage-specific info */
   }

   return shader;
}

nir_visitor::nir_visitor(nir_shader *shader)
{
   this->supports_ints = shader->options->native_integers;
   this->shader = shader;
   this->is_global = true;
   this->var_table = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                             _mesa_key_pointer_equal);
   this->overload_table = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                                  _mesa_key_pointer_equal);
}

nir_visitor::~nir_visitor()
{
   _mesa_hash_table_destroy(this->var_table, NULL);
   _mesa_hash_table_destroy(this->overload_table, NULL);
}

nir_deref_var *
nir_visitor::evaluate_deref(nir_instr *mem_ctx, ir_instruction *ir)
{
   ir->accept(this);
   ralloc_steal(mem_ctx, this->deref_head);
   return this->deref_head;
}

static nir_constant *
constant_copy(ir_constant *ir, void *mem_ctx)
{
   if (ir == NULL)
      return NULL;

   nir_constant *ret = ralloc(mem_ctx, nir_constant);

   unsigned total_elems = ir->type->components();
   unsigned i;
   switch (ir->type->base_type) {
   case GLSL_TYPE_UINT:
      for (i = 0; i < total_elems; i++)
         ret->value.u[i] = ir->value.u[i];
      break;

   case GLSL_TYPE_INT:
      for (i = 0; i < total_elems; i++)
         ret->value.i[i] = ir->value.i[i];
      break;

   case GLSL_TYPE_FLOAT:
      for (i = 0; i < total_elems; i++)
         ret->value.f[i] = ir->value.f[i];
      break;

   case GLSL_TYPE_BOOL:
      for (i = 0; i < total_elems; i++)
         ret->value.b[i] = ir->value.b[i];
      break;

   case GLSL_TYPE_STRUCT:
      ret->elements = ralloc_array(mem_ctx, nir_constant *,
                                   ir->type->length);
      i = 0;
      foreach_in_list(ir_constant, field, &ir->components) {
         ret->elements[i] = constant_copy(field, mem_ctx);
         i++;
      }
      break;

   case GLSL_TYPE_ARRAY:
      ret->elements = ralloc_array(mem_ctx, nir_constant *,
                                   ir->type->length);

      for (i = 0; i < ir->type->length; i++)
         ret->elements[i] = constant_copy(ir->array_elements[i], mem_ctx);
      break;

   default:
      unreachable("not reached");
   }

   return ret;
}

void
nir_visitor::visit(ir_variable *ir)
{
   nir_variable *var = ralloc(shader, nir_variable);
   var->type = ir->type;
   var->name = ralloc_strdup(var, ir->name);

   if (ir->is_interface_instance() && ir->get_max_ifc_array_access() != NULL) {
      unsigned size = ir->get_interface_type()->length;
      var->max_ifc_array_access = ralloc_array(var, unsigned, size);
      memcpy(var->max_ifc_array_access, ir->get_max_ifc_array_access(),
             size * sizeof(unsigned));
   } else {
      var->max_ifc_array_access = NULL;
   }

   var->data.read_only = ir->data.read_only;
   var->data.centroid = ir->data.centroid;
   var->data.sample = ir->data.sample;
   var->data.patch = ir->data.patch;
   var->data.invariant = ir->data.invariant;
   var->data.location = ir->data.location;

   switch(ir->data.mode) {
   case ir_var_auto:
   case ir_var_temporary:
      if (is_global)
         var->data.mode = nir_var_global;
      else
         var->data.mode = nir_var_local;
      break;

   case ir_var_function_in:
   case ir_var_function_out:
   case ir_var_function_inout:
   case ir_var_const_in:
      var->data.mode = nir_var_local;
      break;

   case ir_var_shader_in:
      if (shader->stage == MESA_SHADER_FRAGMENT &&
          ir->data.location == VARYING_SLOT_FACE) {
         /* For whatever reason, GLSL IR makes gl_FrontFacing an input */
         var->data.location = SYSTEM_VALUE_FRONT_FACE;
         var->data.mode = nir_var_system_value;
      } else if (shader->stage == MESA_SHADER_GEOMETRY &&
                 ir->data.location == VARYING_SLOT_PRIMITIVE_ID) {
         /* For whatever reason, GLSL IR makes gl_PrimitiveIDIn an input */
         var->data.location = SYSTEM_VALUE_PRIMITIVE_ID;
         var->data.mode = nir_var_system_value;
      } else {
         var->data.mode = nir_var_shader_in;
      }
      break;

   case ir_var_shader_out:
      var->data.mode = nir_var_shader_out;
      break;

   case ir_var_uniform:
      var->data.mode = nir_var_uniform;
      break;

   case ir_var_shader_storage:
      var->data.mode = nir_var_shader_storage;
      break;

   case ir_var_system_value:
      var->data.mode = nir_var_system_value;
      break;

   default:
      unreachable("not reached");
   }

   var->data.interpolation = ir->data.interpolation;
   var->data.origin_upper_left = ir->data.origin_upper_left;
   var->data.pixel_center_integer = ir->data.pixel_center_integer;
   var->data.explicit_location = ir->data.explicit_location;
   var->data.explicit_index = ir->data.explicit_index;
   var->data.explicit_binding = ir->data.explicit_binding;
   var->data.has_initializer = ir->data.has_initializer;
   var->data.is_unmatched_generic_inout = ir->data.is_unmatched_generic_inout;
   var->data.location_frac = ir->data.location_frac;
   var->data.from_named_ifc_block_array = ir->data.from_named_ifc_block_array;
   var->data.from_named_ifc_block_nonarray = ir->data.from_named_ifc_block_nonarray;

   switch (ir->data.depth_layout) {
   case ir_depth_layout_none:
      var->data.depth_layout = nir_depth_layout_none;
      break;
   case ir_depth_layout_any:
      var->data.depth_layout = nir_depth_layout_any;
      break;
   case ir_depth_layout_greater:
      var->data.depth_layout = nir_depth_layout_greater;
      break;
   case ir_depth_layout_less:
      var->data.depth_layout = nir_depth_layout_less;
      break;
   case ir_depth_layout_unchanged:
      var->data.depth_layout = nir_depth_layout_unchanged;
      break;
   default:
      unreachable("not reached");
   }

   var->data.index = ir->data.index;
   var->data.binding = ir->data.binding;
   var->data.atomic.offset = ir->data.atomic.offset;
   var->data.image.read_only = ir->data.image_read_only;
   var->data.image.write_only = ir->data.image_write_only;
   var->data.image.coherent = ir->data.image_coherent;
   var->data.image._volatile = ir->data.image_volatile;
   var->data.image.restrict_flag = ir->data.image_restrict;
   var->data.image.format = ir->data.image_format;
   var->data.max_array_access = ir->data.max_array_access;

   var->num_state_slots = ir->get_num_state_slots();
   if (var->num_state_slots > 0) {
      var->state_slots = ralloc_array(var, nir_state_slot,
                                      var->num_state_slots);

      ir_state_slot *state_slots = ir->get_state_slots();
      for (unsigned i = 0; i < var->num_state_slots; i++) {
         for (unsigned j = 0; j < 5; j++)
            var->state_slots[i].tokens[j] = state_slots[i].tokens[j];
         var->state_slots[i].swizzle = state_slots[i].swizzle;
      }
   } else {
      var->state_slots = NULL;
   }

   var->constant_initializer = constant_copy(ir->constant_initializer, var);

   var->interface_type = ir->get_interface_type();

   if (var->data.mode == nir_var_local)
      nir_function_impl_add_variable(impl, var);
   else
      nir_shader_add_variable(shader, var);

   _mesa_hash_table_insert(var_table, ir, var);
   this->var = var;
}

ir_visitor_status
nir_function_visitor::visit_enter(ir_function *ir)
{
   visitor->create_function(ir);
   return visit_continue_with_parent;
}


void
nir_visitor::create_function(ir_function *ir)
{
   nir_function *func = nir_function_create(this->shader, ir->name);
   foreach_in_list(ir_function_signature, sig, &ir->signatures) {
      create_overload(sig, func);
   }
}



void
nir_visitor::create_overload(ir_function_signature *ir, nir_function *function)
{
   if (ir->is_intrinsic)
      return;

   nir_function_overload *overload = nir_function_overload_create(function);

   unsigned num_params = ir->parameters.length();
   overload->num_params = num_params;
   overload->params = ralloc_array(shader, nir_parameter, num_params);

   unsigned i = 0;
   foreach_in_list(ir_variable, param, &ir->parameters) {
      switch (param->data.mode) {
      case ir_var_function_in:
         overload->params[i].param_type = nir_parameter_in;
         break;

      case ir_var_function_out:
         overload->params[i].param_type = nir_parameter_out;
         break;

      case ir_var_function_inout:
         overload->params[i].param_type = nir_parameter_inout;
         break;

      default:
         unreachable("not reached");
      }

      overload->params[i].type = param->type;
      i++;
   }

   overload->return_type = ir->return_type;

   _mesa_hash_table_insert(this->overload_table, ir, overload);
}

void
nir_visitor::visit(ir_function *ir)
{
   foreach_in_list(ir_function_signature, sig, &ir->signatures)
      sig->accept(this);
}

void
nir_visitor::visit(ir_function_signature *ir)
{
   if (ir->is_intrinsic)
      return;

   struct hash_entry *entry =
      _mesa_hash_table_search(this->overload_table, ir);

   assert(entry);
   nir_function_overload *overload = (nir_function_overload *) entry->data;

   if (ir->is_defined) {
      nir_function_impl *impl = nir_function_impl_create(overload);
      this->impl = impl;

      unsigned num_params = overload->num_params;
      impl->num_params = num_params;
      impl->params = ralloc_array(this->shader, nir_variable *, num_params);
      unsigned i = 0;
      foreach_in_list(ir_variable, param, &ir->parameters) {
         param->accept(this);
         impl->params[i] = this->var;
         i++;
      }

      if (overload->return_type == glsl_type::void_type) {
         impl->return_var = NULL;
      } else {
         impl->return_var = ralloc(this->shader, nir_variable);
         impl->return_var->name = ralloc_strdup(impl->return_var,
                                                "return_var");
         impl->return_var->type = overload->return_type;
      }

      this->is_global = false;

      nir_builder_init(&b, impl);
      b.cursor = nir_after_cf_list(&impl->body);
      visit_exec_list(&ir->body, this);

      this->is_global = true;
   } else {
      overload->impl = NULL;
   }
}

void
nir_visitor::visit(ir_loop *ir)
{
   nir_loop *loop = nir_loop_create(this->shader);
   nir_builder_cf_insert(&b, &loop->cf_node);

   b.cursor = nir_after_cf_list(&loop->body);
   visit_exec_list(&ir->body_instructions, this);
   b.cursor = nir_after_cf_node(&loop->cf_node);
}

void
nir_visitor::visit(ir_if *ir)
{
   nir_src condition =
      nir_src_for_ssa(evaluate_rvalue(ir->condition));

   nir_if *if_stmt = nir_if_create(this->shader);
   if_stmt->condition = condition;
   nir_builder_cf_insert(&b, &if_stmt->cf_node);

   b.cursor = nir_after_cf_list(&if_stmt->then_list);
   visit_exec_list(&ir->then_instructions, this);

   b.cursor = nir_after_cf_list(&if_stmt->else_list);
   visit_exec_list(&ir->else_instructions, this);

   b.cursor = nir_after_cf_node(&if_stmt->cf_node);
}

void
nir_visitor::visit(ir_discard *ir)
{
   /*
    * discards aren't treated as control flow, because before we lower them
    * they can appear anywhere in the shader and the stuff after them may still
    * be executed (yay, crazy GLSL rules!). However, after lowering, all the
    * discards will be immediately followed by a return.
    */

   nir_intrinsic_instr *discard;
   if (ir->condition) {
      discard = nir_intrinsic_instr_create(this->shader,
                                           nir_intrinsic_discard_if);
      discard->src[0] =
         nir_src_for_ssa(evaluate_rvalue(ir->condition));
   } else {
      discard = nir_intrinsic_instr_create(this->shader, nir_intrinsic_discard);
   }

   nir_builder_instr_insert(&b, &discard->instr);
}

void
nir_visitor::visit(ir_emit_vertex *ir)
{
   nir_intrinsic_instr *instr =
      nir_intrinsic_instr_create(this->shader, nir_intrinsic_emit_vertex);
   instr->const_index[0] = ir->stream_id();
   nir_builder_instr_insert(&b, &instr->instr);
}

void
nir_visitor::visit(ir_end_primitive *ir)
{
   nir_intrinsic_instr *instr =
      nir_intrinsic_instr_create(this->shader, nir_intrinsic_end_primitive);
   instr->const_index[0] = ir->stream_id();
   nir_builder_instr_insert(&b, &instr->instr);
}

void
nir_visitor::visit(ir_loop_jump *ir)
{
   nir_jump_type type;
   switch (ir->mode) {
   case ir_loop_jump::jump_break:
      type = nir_jump_break;
      break;
   case ir_loop_jump::jump_continue:
      type = nir_jump_continue;
      break;
   default:
      unreachable("not reached");
   }

   nir_jump_instr *instr = nir_jump_instr_create(this->shader, type);
   nir_builder_instr_insert(&b, &instr->instr);
}

void
nir_visitor::visit(ir_return *ir)
{
   if (ir->value != NULL) {
      nir_intrinsic_instr *copy =
         nir_intrinsic_instr_create(this->shader, nir_intrinsic_copy_var);

      copy->variables[0] = nir_deref_var_create(copy, this->impl->return_var);
      copy->variables[1] = evaluate_deref(&copy->instr, ir->value);
   }

   nir_jump_instr *instr = nir_jump_instr_create(this->shader, nir_jump_return);
   nir_builder_instr_insert(&b, &instr->instr);
}

void
nir_visitor::visit(ir_call *ir)
{
   if (ir->callee->is_intrinsic) {
      nir_intrinsic_op op;
      if (strcmp(ir->callee_name(), "__intrinsic_atomic_read") == 0) {
         op = nir_intrinsic_atomic_counter_read_var;
      } else if (strcmp(ir->callee_name(), "__intrinsic_atomic_increment") == 0) {
         op = nir_intrinsic_atomic_counter_inc_var;
      } else if (strcmp(ir->callee_name(), "__intrinsic_atomic_predecrement") == 0) {
         op = nir_intrinsic_atomic_counter_dec_var;
      } else if (strcmp(ir->callee_name(), "__intrinsic_image_load") == 0) {
         op = nir_intrinsic_image_load;
      } else if (strcmp(ir->callee_name(), "__intrinsic_image_store") == 0) {
         op = nir_intrinsic_image_store;
      } else if (strcmp(ir->callee_name(), "__intrinsic_image_atomic_add") == 0) {
         op = nir_intrinsic_image_atomic_add;
      } else if (strcmp(ir->callee_name(), "__intrinsic_image_atomic_min") == 0) {
         op = nir_intrinsic_image_atomic_min;
      } else if (strcmp(ir->callee_name(), "__intrinsic_image_atomic_max") == 0) {
         op = nir_intrinsic_image_atomic_max;
      } else if (strcmp(ir->callee_name(), "__intrinsic_image_atomic_and") == 0) {
         op = nir_intrinsic_image_atomic_and;
      } else if (strcmp(ir->callee_name(), "__intrinsic_image_atomic_or") == 0) {
         op = nir_intrinsic_image_atomic_or;
      } else if (strcmp(ir->callee_name(), "__intrinsic_image_atomic_xor") == 0) {
         op = nir_intrinsic_image_atomic_xor;
      } else if (strcmp(ir->callee_name(), "__intrinsic_image_atomic_exchange") == 0) {
         op = nir_intrinsic_image_atomic_exchange;
      } else if (strcmp(ir->callee_name(), "__intrinsic_image_atomic_comp_swap") == 0) {
         op = nir_intrinsic_image_atomic_comp_swap;
      } else if (strcmp(ir->callee_name(), "__intrinsic_memory_barrier") == 0) {
         op = nir_intrinsic_memory_barrier;
      } else if (strcmp(ir->callee_name(), "__intrinsic_image_size") == 0) {
         op = nir_intrinsic_image_size;
      } else if (strcmp(ir->callee_name(), "__intrinsic_image_samples") == 0) {
         op = nir_intrinsic_image_samples;
      } else if (strcmp(ir->callee_name(), "__intrinsic_store_ssbo") == 0) {
         op = nir_intrinsic_store_ssbo;
      } else if (strcmp(ir->callee_name(), "__intrinsic_load_ssbo") == 0) {
         op = nir_intrinsic_load_ssbo;
      } else if (strcmp(ir->callee_name(), "__intrinsic_ssbo_atomic_add_internal") == 0) {
         op = nir_intrinsic_ssbo_atomic_add;
      } else if (strcmp(ir->callee_name(), "__intrinsic_ssbo_atomic_and_internal") == 0) {
         op = nir_intrinsic_ssbo_atomic_and;
      } else if (strcmp(ir->callee_name(), "__intrinsic_ssbo_atomic_or_internal") == 0) {
         op = nir_intrinsic_ssbo_atomic_or;
      } else if (strcmp(ir->callee_name(), "__intrinsic_ssbo_atomic_xor_internal") == 0) {
         op = nir_intrinsic_ssbo_atomic_xor;
      } else if (strcmp(ir->callee_name(), "__intrinsic_ssbo_atomic_min_internal") == 0) {
         assert(ir->return_deref);
         if (ir->return_deref->type == glsl_type::int_type)
            op = nir_intrinsic_ssbo_atomic_imin;
         else if (ir->return_deref->type == glsl_type::uint_type)
            op = nir_intrinsic_ssbo_atomic_umin;
         else
            unreachable("Invalid type");
      } else if (strcmp(ir->callee_name(), "__intrinsic_ssbo_atomic_max_internal") == 0) {
         assert(ir->return_deref);
         if (ir->return_deref->type == glsl_type::int_type)
            op = nir_intrinsic_ssbo_atomic_imax;
         else if (ir->return_deref->type == glsl_type::uint_type)
            op = nir_intrinsic_ssbo_atomic_umax;
         else
            unreachable("Invalid type");
      } else if (strcmp(ir->callee_name(), "__intrinsic_ssbo_atomic_exchange_internal") == 0) {
         op = nir_intrinsic_ssbo_atomic_exchange;
      } else if (strcmp(ir->callee_name(), "__intrinsic_ssbo_atomic_comp_swap_internal") == 0) {
         op = nir_intrinsic_ssbo_atomic_comp_swap;
      } else if (strcmp(ir->callee_name(), "__intrinsic_shader_clock") == 0) {
         op = nir_intrinsic_shader_clock;
      } else if (strcmp(ir->callee_name(), "__intrinsic_group_memory_barrier") == 0) {
         op = nir_intrinsic_group_memory_barrier;
      } else if (strcmp(ir->callee_name(), "__intrinsic_memory_barrier_atomic_counter") == 0) {
         op = nir_intrinsic_memory_barrier_atomic_counter;
      } else if (strcmp(ir->callee_name(), "__intrinsic_memory_barrier_buffer") == 0) {
         op = nir_intrinsic_memory_barrier_buffer;
      } else if (strcmp(ir->callee_name(), "__intrinsic_memory_barrier_image") == 0) {
         op = nir_intrinsic_memory_barrier_image;
      } else if (strcmp(ir->callee_name(), "__intrinsic_memory_barrier_shared") == 0) {
         op = nir_intrinsic_memory_barrier_shared;
      } else {
         unreachable("not reached");
      }

      nir_intrinsic_instr *instr = nir_intrinsic_instr_create(shader, op);
      nir_dest *dest = &instr->dest;

      switch (op) {
      case nir_intrinsic_atomic_counter_read_var:
      case nir_intrinsic_atomic_counter_inc_var:
      case nir_intrinsic_atomic_counter_dec_var: {
         ir_dereference *param =
            (ir_dereference *) ir->actual_parameters.get_head();
         instr->variables[0] = evaluate_deref(&instr->instr, param);
         nir_ssa_dest_init(&instr->instr, &instr->dest, 1, NULL);
         nir_builder_instr_insert(&b, &instr->instr);
         break;
      }
      case nir_intrinsic_image_load:
      case nir_intrinsic_image_store:
      case nir_intrinsic_image_atomic_add:
      case nir_intrinsic_image_atomic_min:
      case nir_intrinsic_image_atomic_max:
      case nir_intrinsic_image_atomic_and:
      case nir_intrinsic_image_atomic_or:
      case nir_intrinsic_image_atomic_xor:
      case nir_intrinsic_image_atomic_exchange:
      case nir_intrinsic_image_atomic_comp_swap:
      case nir_intrinsic_image_samples:
      case nir_intrinsic_image_size: {
         nir_ssa_undef_instr *instr_undef =
            nir_ssa_undef_instr_create(shader, 1);
         nir_builder_instr_insert(&b, &instr_undef->instr);

         /* Set the image variable dereference. */
         exec_node *param = ir->actual_parameters.get_head();
         ir_dereference *image = (ir_dereference *)param;
         const glsl_type *type =
            image->variable_referenced()->type->without_array();

         instr->variables[0] = evaluate_deref(&instr->instr, image);
         param = param->get_next();

         /* Set the intrinsic destination. */
         if (ir->return_deref) {
            const nir_intrinsic_info *info =
                    &nir_intrinsic_infos[instr->intrinsic];
            nir_ssa_dest_init(&instr->instr, &instr->dest,
                              info->dest_components, NULL);
         }

         if (op == nir_intrinsic_image_size ||
             op == nir_intrinsic_image_samples) {
            nir_builder_instr_insert(&b, &instr->instr);
            break;
         }

         /* Set the address argument, extending the coordinate vector to four
          * components.
          */
         nir_ssa_def *src_addr =
            evaluate_rvalue((ir_dereference *)param);
         nir_ssa_def *srcs[4];

         for (int i = 0; i < 4; i++) {
            if (i < type->coordinate_components())
               srcs[i] = nir_channel(&b, src_addr, i);
            else
               srcs[i] = &instr_undef->def;
         }

         instr->src[0] = nir_src_for_ssa(nir_vec(&b, srcs, 4));
         param = param->get_next();

         /* Set the sample argument, which is undefined for single-sample
          * images.
          */
         if (type->sampler_dimensionality == GLSL_SAMPLER_DIM_MS) {
            instr->src[1] =
               nir_src_for_ssa(evaluate_rvalue((ir_dereference *)param));
            param = param->get_next();
         } else {
            instr->src[1] = nir_src_for_ssa(&instr_undef->def);
         }

         /* Set the intrinsic parameters. */
         if (!param->is_tail_sentinel()) {
            instr->src[2] =
               nir_src_for_ssa(evaluate_rvalue((ir_dereference *)param));
            param = param->get_next();
         }

         if (!param->is_tail_sentinel()) {
            instr->src[3] =
               nir_src_for_ssa(evaluate_rvalue((ir_dereference *)param));
            param = param->get_next();
         }
         nir_builder_instr_insert(&b, &instr->instr);
         break;
      }
      case nir_intrinsic_memory_barrier:
      case nir_intrinsic_group_memory_barrier:
      case nir_intrinsic_memory_barrier_atomic_counter:
      case nir_intrinsic_memory_barrier_buffer:
      case nir_intrinsic_memory_barrier_image:
      case nir_intrinsic_memory_barrier_shared:
         nir_builder_instr_insert(&b, &instr->instr);
         break;
      case nir_intrinsic_shader_clock:
         nir_ssa_dest_init(&instr->instr, &instr->dest, 1, NULL);
         nir_builder_instr_insert(&b, &instr->instr);
         break;
      case nir_intrinsic_store_ssbo: {
         exec_node *param = ir->actual_parameters.get_head();
         ir_rvalue *block = ((ir_instruction *)param)->as_rvalue();

         param = param->get_next();
         ir_rvalue *offset = ((ir_instruction *)param)->as_rvalue();

         param = param->get_next();
         ir_rvalue *val = ((ir_instruction *)param)->as_rvalue();

         param = param->get_next();
         ir_constant *write_mask = ((ir_instruction *)param)->as_constant();
         assert(write_mask);

         /* Check if we need the indirect version */
         ir_constant *const_offset = offset->as_constant();
         if (!const_offset) {
            op = nir_intrinsic_store_ssbo_indirect;
            ralloc_free(instr);
            instr = nir_intrinsic_instr_create(shader, op);
            instr->src[2] = nir_src_for_ssa(evaluate_rvalue(offset));
            instr->const_index[0] = 0;
         } else {
            instr->const_index[0] = const_offset->value.u[0];
         }

         instr->const_index[1] = write_mask->value.u[0];

         instr->src[0] = nir_src_for_ssa(evaluate_rvalue(val));
         instr->num_components = val->type->vector_elements;

         instr->src[1] = nir_src_for_ssa(evaluate_rvalue(block));
         nir_builder_instr_insert(&b, &instr->instr);
         break;
      }
      case nir_intrinsic_load_ssbo: {
         exec_node *param = ir->actual_parameters.get_head();
         ir_rvalue *block = ((ir_instruction *)param)->as_rvalue();

         param = param->get_next();
         ir_rvalue *offset = ((ir_instruction *)param)->as_rvalue();

         /* Check if we need the indirect version */
         ir_constant *const_offset = offset->as_constant();
         if (!const_offset) {
            op = nir_intrinsic_load_ssbo_indirect;
            ralloc_free(instr);
            instr = nir_intrinsic_instr_create(shader, op);
            instr->src[1] = nir_src_for_ssa(evaluate_rvalue(offset));
            instr->const_index[0] = 0;
            dest = &instr->dest;
         } else {
            instr->const_index[0] = const_offset->value.u[0];
         }

         instr->src[0] = nir_src_for_ssa(evaluate_rvalue(block));

         const glsl_type *type = ir->return_deref->var->type;
         instr->num_components = type->vector_elements;

         /* Setup destination register */
         nir_ssa_dest_init(&instr->instr, &instr->dest,
                           type->vector_elements, NULL);

         /* Insert the created nir instruction now since in the case of boolean
          * result we will need to emit another instruction after it
          */
         nir_builder_instr_insert(&b, &instr->instr);

         /*
          * In SSBO/UBO's, a true boolean value is any non-zero value, but we
          * consider a true boolean to be ~0. Fix this up with a != 0
          * comparison.
          */
         if (type->base_type == GLSL_TYPE_BOOL) {
            nir_alu_instr *load_ssbo_compare =
               nir_alu_instr_create(shader, nir_op_ine);
            load_ssbo_compare->src[0].src.is_ssa = true;
            load_ssbo_compare->src[0].src.ssa = &instr->dest.ssa;
            load_ssbo_compare->src[1].src =
               nir_src_for_ssa(nir_imm_int(&b, 0));
            for (unsigned i = 0; i < type->vector_elements; i++)
               load_ssbo_compare->src[1].swizzle[i] = 0;
            nir_ssa_dest_init(&load_ssbo_compare->instr,
                              &load_ssbo_compare->dest.dest,
                              type->vector_elements, NULL);
            load_ssbo_compare->dest.write_mask = (1 << type->vector_elements) - 1;
            nir_builder_instr_insert(&b, &load_ssbo_compare->instr);
            dest = &load_ssbo_compare->dest.dest;
         }
         break;
      }
      case nir_intrinsic_ssbo_atomic_add:
      case nir_intrinsic_ssbo_atomic_imin:
      case nir_intrinsic_ssbo_atomic_umin:
      case nir_intrinsic_ssbo_atomic_imax:
      case nir_intrinsic_ssbo_atomic_umax:
      case nir_intrinsic_ssbo_atomic_and:
      case nir_intrinsic_ssbo_atomic_or:
      case nir_intrinsic_ssbo_atomic_xor:
      case nir_intrinsic_ssbo_atomic_exchange:
      case nir_intrinsic_ssbo_atomic_comp_swap: {
         int param_count = ir->actual_parameters.length();
         assert(param_count == 3 || param_count == 4);

         /* Block index */
         exec_node *param = ir->actual_parameters.get_head();
         ir_instruction *inst = (ir_instruction *) param;
         instr->src[0] = nir_src_for_ssa(evaluate_rvalue(inst->as_rvalue()));

         /* Offset */
         param = param->get_next();
         inst = (ir_instruction *) param;
         instr->src[1] = nir_src_for_ssa(evaluate_rvalue(inst->as_rvalue()));

         /* data1 parameter (this is always present) */
         param = param->get_next();
         inst = (ir_instruction *) param;
         instr->src[2] = nir_src_for_ssa(evaluate_rvalue(inst->as_rvalue()));

         /* data2 parameter (only with atomic_comp_swap) */
         if (param_count == 4) {
            assert(op == nir_intrinsic_ssbo_atomic_comp_swap);
            param = param->get_next();
            inst = (ir_instruction *) param;
            instr->src[3] = nir_src_for_ssa(evaluate_rvalue(inst->as_rvalue()));
         }

         /* Atomic result */
         assert(ir->return_deref);
         nir_ssa_dest_init(&instr->instr, &instr->dest,
                           ir->return_deref->type->vector_elements, NULL);
         nir_builder_instr_insert(&b, &instr->instr);
         break;
      }
      default:
         unreachable("not reached");
      }

      if (ir->return_deref) {
         nir_intrinsic_instr *store_instr =
            nir_intrinsic_instr_create(shader, nir_intrinsic_store_var);
         store_instr->num_components = ir->return_deref->type->vector_elements;

         store_instr->variables[0] =
            evaluate_deref(&store_instr->instr, ir->return_deref);
         store_instr->src[0] = nir_src_for_ssa(&dest->ssa);

         nir_builder_instr_insert(&b, &store_instr->instr);
      }

      return;
   }

   struct hash_entry *entry =
      _mesa_hash_table_search(this->overload_table, ir->callee);
   assert(entry);
   nir_function_overload *callee = (nir_function_overload *) entry->data;

   nir_call_instr *instr = nir_call_instr_create(this->shader, callee);

   unsigned i = 0;
   foreach_in_list(ir_dereference, param, &ir->actual_parameters) {
      instr->params[i] = evaluate_deref(&instr->instr, param);
      i++;
   }

   instr->return_deref = evaluate_deref(&instr->instr, ir->return_deref);
   nir_builder_instr_insert(&b, &instr->instr);
}

void
nir_visitor::visit(ir_assignment *ir)
{
   unsigned num_components = ir->lhs->type->vector_elements;

   if ((ir->rhs->as_dereference() || ir->rhs->as_constant()) &&
       (ir->write_mask == (1 << num_components) - 1 || ir->write_mask == 0)) {
      /* We're doing a plain-as-can-be copy, so emit a copy_var */
      nir_intrinsic_instr *copy =
         nir_intrinsic_instr_create(this->shader, nir_intrinsic_copy_var);

      copy->variables[0] = evaluate_deref(&copy->instr, ir->lhs);
      copy->variables[1] = evaluate_deref(&copy->instr, ir->rhs);

      if (ir->condition) {
         nir_if *if_stmt = nir_if_create(this->shader);
         if_stmt->condition = nir_src_for_ssa(evaluate_rvalue(ir->condition));
         nir_builder_cf_insert(&b, &if_stmt->cf_node);
         nir_instr_insert_after_cf_list(&if_stmt->then_list, &copy->instr);
         b.cursor = nir_after_cf_node(&if_stmt->cf_node);
      } else {
         nir_builder_instr_insert(&b, &copy->instr);
      }
      return;
   }

   assert(ir->rhs->type->is_scalar() || ir->rhs->type->is_vector());

   ir->lhs->accept(this);
   nir_deref_var *lhs_deref = this->deref_head;
   nir_ssa_def *src = evaluate_rvalue(ir->rhs);

   if (ir->write_mask != (1 << num_components) - 1 && ir->write_mask != 0) {
      /*
       * We have no good way to update only part of a variable, so just load
       * the LHS and do a vec operation to combine the old with the new, and
       * then store it
       * back into the LHS. Copy propagation should get rid of the mess.
       */

      nir_intrinsic_instr *load =
         nir_intrinsic_instr_create(this->shader, nir_intrinsic_load_var);
      load->num_components = ir->lhs->type->vector_elements;
      nir_ssa_dest_init(&load->instr, &load->dest, num_components, NULL);
      load->variables[0] = lhs_deref;
      ralloc_steal(load, load->variables[0]);
      nir_builder_instr_insert(&b, &load->instr);

      nir_ssa_def *srcs[4];

      unsigned component = 0;
      for (unsigned i = 0; i < ir->lhs->type->vector_elements; i++) {
         if (ir->write_mask & (1 << i)) {
            /* GLSL IR will give us the input to the write-masked assignment
             * in a single packed vector.  So, for example, if the
             * writemask is xzw, then we have to swizzle x -> x, y -> z,
             * and z -> w and get the y component from the load.
             */
            srcs[i] = nir_channel(&b, src, component++);
         } else {
            srcs[i] = nir_channel(&b, &load->dest.ssa, i);
         }
      }

      src = nir_vec(&b, srcs, ir->lhs->type->vector_elements);
   }

   nir_intrinsic_instr *store =
      nir_intrinsic_instr_create(this->shader, nir_intrinsic_store_var);
   store->num_components = ir->lhs->type->vector_elements;
   nir_deref *store_deref = nir_copy_deref(store, &lhs_deref->deref);
   store->variables[0] = nir_deref_as_var(store_deref);
   store->src[0] = nir_src_for_ssa(src);

   if (ir->condition) {
      nir_if *if_stmt = nir_if_create(this->shader);
      if_stmt->condition = nir_src_for_ssa(evaluate_rvalue(ir->condition));
      nir_builder_cf_insert(&b, &if_stmt->cf_node);
      nir_instr_insert_after_cf_list(&if_stmt->then_list, &store->instr);
      b.cursor = nir_after_cf_node(&if_stmt->cf_node);
   } else {
      nir_builder_instr_insert(&b, &store->instr);
   }
}

/*
 * Given an instruction, returns a pointer to its destination or NULL if there
 * is no destination.
 *
 * Note that this only handles instructions we generate at this level.
 */
static nir_dest *
get_instr_dest(nir_instr *instr)
{
   nir_alu_instr *alu_instr;
   nir_intrinsic_instr *intrinsic_instr;
   nir_tex_instr *tex_instr;

   switch (instr->type) {
      case nir_instr_type_alu:
         alu_instr = nir_instr_as_alu(instr);
         return &alu_instr->dest.dest;

      case nir_instr_type_intrinsic:
         intrinsic_instr = nir_instr_as_intrinsic(instr);
         if (nir_intrinsic_infos[intrinsic_instr->intrinsic].has_dest)
            return &intrinsic_instr->dest;
         else
            return NULL;

      case nir_instr_type_tex:
         tex_instr = nir_instr_as_tex(instr);
         return &tex_instr->dest;

      default:
         unreachable("not reached");
   }

   return NULL;
}

void
nir_visitor::add_instr(nir_instr *instr, unsigned num_components)
{
   nir_dest *dest = get_instr_dest(instr);

   if (dest)
      nir_ssa_dest_init(instr, dest, num_components, NULL);

   nir_builder_instr_insert(&b, instr);

   if (dest) {
      assert(dest->is_ssa);
      this->result = &dest->ssa;
   }
}

nir_ssa_def *
nir_visitor::evaluate_rvalue(ir_rvalue* ir)
{
   ir->accept(this);
   if (ir->as_dereference() || ir->as_constant()) {
      /*
       * A dereference is being used on the right hand side, which means we
       * must emit a variable load.
       */

      nir_intrinsic_instr *load_instr =
         nir_intrinsic_instr_create(this->shader, nir_intrinsic_load_var);
      load_instr->num_components = ir->type->vector_elements;
      load_instr->variables[0] = this->deref_head;
      ralloc_steal(load_instr, load_instr->variables[0]);
      add_instr(&load_instr->instr, ir->type->vector_elements);
   }

   return this->result;
}

void
nir_visitor::visit(ir_expression *ir)
{
   /* Some special cases */
   switch (ir->operation) {
   case ir_binop_ubo_load: {
      ir_constant *const_index = ir->operands[1]->as_constant();

      nir_intrinsic_op op;
      if (const_index) {
         op = nir_intrinsic_load_ubo;
      } else {
         op = nir_intrinsic_load_ubo_indirect;
      }
      nir_intrinsic_instr *load = nir_intrinsic_instr_create(this->shader, op);
      load->num_components = ir->type->vector_elements;
      load->const_index[0] = const_index ? const_index->value.u[0] : 0; /* base offset */
      load->src[0] = nir_src_for_ssa(evaluate_rvalue(ir->operands[0]));
      if (!const_index)
         load->src[1] = nir_src_for_ssa(evaluate_rvalue(ir->operands[1]));
      add_instr(&load->instr, ir->type->vector_elements);

      /*
       * In UBO's, a true boolean value is any non-zero value, but we consider
       * a true boolean to be ~0. Fix this up with a != 0 comparison.
       */

      if (ir->type->base_type == GLSL_TYPE_BOOL)
         this->result = nir_ine(&b, &load->dest.ssa, nir_imm_int(&b, 0));

      return;
   }

   case ir_unop_interpolate_at_centroid:
   case ir_binop_interpolate_at_offset:
   case ir_binop_interpolate_at_sample: {
      ir_dereference *deref = ir->operands[0]->as_dereference();
      ir_swizzle *swizzle = NULL;
      if (!deref) {
         /* the api does not allow a swizzle here, but the varying packing code
          * may have pushed one into here.
          */
         swizzle = ir->operands[0]->as_swizzle();
         assert(swizzle);
         deref = swizzle->val->as_dereference();
         assert(deref);
      }

      deref->accept(this);

      nir_intrinsic_op op;
      if (this->deref_head->var->data.mode == nir_var_shader_in) {
         switch (ir->operation) {
         case ir_unop_interpolate_at_centroid:
            op = nir_intrinsic_interp_var_at_centroid;
            break;
         case ir_binop_interpolate_at_offset:
            op = nir_intrinsic_interp_var_at_offset;
            break;
         case ir_binop_interpolate_at_sample:
            op = nir_intrinsic_interp_var_at_sample;
            break;
         default:
            unreachable("Invalid interpolation intrinsic");
         }
      } else {
         /* This case can happen if the vertex shader does not write the
          * given varying.  In this case, the linker will lower it to a
          * global variable.  Since interpolating a variable makes no
          * sense, we'll just turn it into a load which will probably
          * eventually end up as an SSA definition.
          */
         assert(this->deref_head->var->data.mode == nir_var_global);
         op = nir_intrinsic_load_var;
      }

      nir_intrinsic_instr *intrin = nir_intrinsic_instr_create(shader, op);
      intrin->num_components = deref->type->vector_elements;
      intrin->variables[0] = this->deref_head;
      ralloc_steal(intrin, intrin->variables[0]);

      if (intrin->intrinsic == nir_intrinsic_interp_var_at_offset ||
          intrin->intrinsic == nir_intrinsic_interp_var_at_sample)
         intrin->src[0] = nir_src_for_ssa(evaluate_rvalue(ir->operands[1]));

      add_instr(&intrin->instr, deref->type->vector_elements);

      if (swizzle) {
         unsigned swiz[4] = {
            swizzle->mask.x, swizzle->mask.y, swizzle->mask.z, swizzle->mask.w
         };

         result = nir_swizzle(&b, result, swiz,
                              swizzle->type->vector_elements, false);
      }

      return;
   }

   default:
      break;
   }

   nir_ssa_def *srcs[4];
   for (unsigned i = 0; i < ir->get_num_operands(); i++)
      srcs[i] = evaluate_rvalue(ir->operands[i]);

   glsl_base_type types[4];
   for (unsigned i = 0; i < ir->get_num_operands(); i++)
      if (supports_ints)
         types[i] = ir->operands[i]->type->base_type;
      else
         types[i] = GLSL_TYPE_FLOAT;

   glsl_base_type out_type;
   if (supports_ints)
      out_type = ir->type->base_type;
   else
      out_type = GLSL_TYPE_FLOAT;

   switch (ir->operation) {
   case ir_unop_bit_not: result = nir_inot(&b, srcs[0]); break;
   case ir_unop_logic_not:
      result = supports_ints ? nir_inot(&b, srcs[0]) : nir_fnot(&b, srcs[0]);
      break;
   case ir_unop_neg:
      result = (types[0] == GLSL_TYPE_FLOAT) ? nir_fneg(&b, srcs[0])
                                             : nir_ineg(&b, srcs[0]);
      break;
   case ir_unop_abs:
      result = (types[0] == GLSL_TYPE_FLOAT) ? nir_fabs(&b, srcs[0])
                                             : nir_iabs(&b, srcs[0]);
      break;
   case ir_unop_saturate:
      assert(types[0] == GLSL_TYPE_FLOAT);
      result = nir_fsat(&b, srcs[0]);
      break;
   case ir_unop_sign:
      result = (types[0] == GLSL_TYPE_FLOAT) ? nir_fsign(&b, srcs[0])
                                             : nir_isign(&b, srcs[0]);
      break;
   case ir_unop_rcp:  result = nir_frcp(&b, srcs[0]);  break;
   case ir_unop_rsq:  result = nir_frsq(&b, srcs[0]);  break;
   case ir_unop_sqrt: result = nir_fsqrt(&b, srcs[0]); break;
   case ir_unop_exp:  unreachable("ir_unop_exp should have been lowered");
   case ir_unop_log:  unreachable("ir_unop_log should have been lowered");
   case ir_unop_exp2: result = nir_fexp2(&b, srcs[0]); break;
   case ir_unop_log2: result = nir_flog2(&b, srcs[0]); break;
   case ir_unop_i2f:
      result = supports_ints ? nir_i2f(&b, srcs[0]) : nir_fmov(&b, srcs[0]);
      break;
   case ir_unop_u2f:
      result = supports_ints ? nir_u2f(&b, srcs[0]) : nir_fmov(&b, srcs[0]);
      break;
   case ir_unop_b2f:
      result = supports_ints ? nir_b2f(&b, srcs[0]) : nir_fmov(&b, srcs[0]);
      break;
   case ir_unop_f2i:  result = nir_f2i(&b, srcs[0]);   break;
   case ir_unop_f2u:  result = nir_f2u(&b, srcs[0]);   break;
   case ir_unop_f2b:  result = nir_f2b(&b, srcs[0]);   break;
   case ir_unop_i2b:  result = nir_i2b(&b, srcs[0]);   break;
   case ir_unop_b2i:  result = nir_b2i(&b, srcs[0]);   break;
   case ir_unop_i2u:
   case ir_unop_u2i:
   case ir_unop_bitcast_i2f:
   case ir_unop_bitcast_f2i:
   case ir_unop_bitcast_u2f:
   case ir_unop_bitcast_f2u:
   case ir_unop_subroutine_to_int:
      /* no-op */
      result = nir_imov(&b, srcs[0]);
      break;
   case ir_unop_any:
      switch (ir->operands[0]->type->vector_elements) {
      case 2:
         result = supports_ints ? nir_bany2(&b, srcs[0])
                                : nir_fany2(&b, srcs[0]);
         break;
      case 3:
         result = supports_ints ? nir_bany3(&b, srcs[0])
                                : nir_fany3(&b, srcs[0]);
         break;
      case 4:
         result = supports_ints ? nir_bany4(&b, srcs[0])
                                : nir_fany4(&b, srcs[0]);
         break;
      default:
         unreachable("not reached");
      }
      break;
   case ir_unop_trunc: result = nir_ftrunc(&b, srcs[0]); break;
   case ir_unop_ceil:  result = nir_fceil(&b, srcs[0]); break;
   case ir_unop_floor: result = nir_ffloor(&b, srcs[0]); break;
   case ir_unop_fract: result = nir_ffract(&b, srcs[0]); break;
   case ir_unop_round_even: result = nir_fround_even(&b, srcs[0]); break;
   case ir_unop_sin:   result = nir_fsin(&b, srcs[0]); break;
   case ir_unop_cos:   result = nir_fcos(&b, srcs[0]); break;
   case ir_unop_dFdx:        result = nir_fddx(&b, srcs[0]); break;
   case ir_unop_dFdy:        result = nir_fddy(&b, srcs[0]); break;
   case ir_unop_dFdx_fine:   result = nir_fddx_fine(&b, srcs[0]); break;
   case ir_unop_dFdy_fine:   result = nir_fddy_fine(&b, srcs[0]); break;
   case ir_unop_dFdx_coarse: result = nir_fddx_coarse(&b, srcs[0]); break;
   case ir_unop_dFdy_coarse: result = nir_fddy_coarse(&b, srcs[0]); break;
   case ir_unop_pack_snorm_2x16:
      result = nir_pack_snorm_2x16(&b, srcs[0]);
      break;
   case ir_unop_pack_snorm_4x8:
      result = nir_pack_snorm_4x8(&b, srcs[0]);
      break;
   case ir_unop_pack_unorm_2x16:
      result = nir_pack_unorm_2x16(&b, srcs[0]);
      break;
   case ir_unop_pack_unorm_4x8:
      result = nir_pack_unorm_4x8(&b, srcs[0]);
      break;
   case ir_unop_pack_half_2x16:
      result = nir_pack_half_2x16(&b, srcs[0]);
      break;
   case ir_unop_unpack_snorm_2x16:
      result = nir_unpack_snorm_2x16(&b, srcs[0]);
      break;
   case ir_unop_unpack_snorm_4x8:
      result = nir_unpack_snorm_4x8(&b, srcs[0]);
      break;
   case ir_unop_unpack_unorm_2x16:
      result = nir_unpack_unorm_2x16(&b, srcs[0]);
      break;
   case ir_unop_unpack_unorm_4x8:
      result = nir_unpack_unorm_4x8(&b, srcs[0]);
      break;
   case ir_unop_unpack_half_2x16:
      result = nir_unpack_half_2x16(&b, srcs[0]);
      break;
   case ir_unop_unpack_half_2x16_split_x:
      result = nir_unpack_half_2x16_split_x(&b, srcs[0]);
      break;
   case ir_unop_unpack_half_2x16_split_y:
      result = nir_unpack_half_2x16_split_y(&b, srcs[0]);
      break;
   case ir_unop_bitfield_reverse:
      result = nir_bitfield_reverse(&b, srcs[0]);
      break;
   case ir_unop_bit_count:
      result = nir_bit_count(&b, srcs[0]);
      break;
   case ir_unop_find_msb:
      switch (types[0]) {
      case GLSL_TYPE_UINT:
         result = nir_ufind_msb(&b, srcs[0]);
         break;
      case GLSL_TYPE_INT:
         result = nir_ifind_msb(&b, srcs[0]);
         break;
      default:
         unreachable("Invalid type for findMSB()");
      }
      break;
   case ir_unop_find_lsb:
      result = nir_find_lsb(&b, srcs[0]);
      break;

   case ir_unop_noise:
      switch (ir->type->vector_elements) {
      case 1:
         switch (ir->operands[0]->type->vector_elements) {
            case 1: result = nir_fnoise1_1(&b, srcs[0]); break;
            case 2: result = nir_fnoise1_2(&b, srcs[0]); break;
            case 3: result = nir_fnoise1_3(&b, srcs[0]); break;
            case 4: result = nir_fnoise1_4(&b, srcs[0]); break;
            default: unreachable("not reached");
         }
         break;
      case 2:
         switch (ir->operands[0]->type->vector_elements) {
            case 1: result = nir_fnoise2_1(&b, srcs[0]); break;
            case 2: result = nir_fnoise2_2(&b, srcs[0]); break;
            case 3: result = nir_fnoise2_3(&b, srcs[0]); break;
            case 4: result = nir_fnoise2_4(&b, srcs[0]); break;
            default: unreachable("not reached");
         }
         break;
      case 3:
         switch (ir->operands[0]->type->vector_elements) {
            case 1: result = nir_fnoise3_1(&b, srcs[0]); break;
            case 2: result = nir_fnoise3_2(&b, srcs[0]); break;
            case 3: result = nir_fnoise3_3(&b, srcs[0]); break;
            case 4: result = nir_fnoise3_4(&b, srcs[0]); break;
            default: unreachable("not reached");
         }
         break;
      case 4:
         switch (ir->operands[0]->type->vector_elements) {
            case 1: result = nir_fnoise4_1(&b, srcs[0]); break;
            case 2: result = nir_fnoise4_2(&b, srcs[0]); break;
            case 3: result = nir_fnoise4_3(&b, srcs[0]); break;
            case 4: result = nir_fnoise4_4(&b, srcs[0]); break;
            default: unreachable("not reached");
         }
         break;
      default:
         unreachable("not reached");
      }
      break;
   case ir_unop_get_buffer_size: {
      nir_intrinsic_instr *load = nir_intrinsic_instr_create(
         this->shader,
         nir_intrinsic_get_buffer_size);
      load->num_components = ir->type->vector_elements;
      load->src[0] = nir_src_for_ssa(evaluate_rvalue(ir->operands[0]));
      add_instr(&load->instr, ir->type->vector_elements);
      return;
   }

   case ir_binop_add:
      result = (out_type == GLSL_TYPE_FLOAT) ? nir_fadd(&b, srcs[0], srcs[1])
                                             : nir_iadd(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_sub:
      result = (out_type == GLSL_TYPE_FLOAT) ? nir_fsub(&b, srcs[0], srcs[1])
                                             : nir_isub(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_mul:
      result = (out_type == GLSL_TYPE_FLOAT) ? nir_fmul(&b, srcs[0], srcs[1])
                                             : nir_imul(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_div:
      if (out_type == GLSL_TYPE_FLOAT)
         result = nir_fdiv(&b, srcs[0], srcs[1]);
      else if (out_type == GLSL_TYPE_INT)
         result = nir_idiv(&b, srcs[0], srcs[1]);
      else
         result = nir_udiv(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_mod:
      result = (out_type == GLSL_TYPE_FLOAT) ? nir_fmod(&b, srcs[0], srcs[1])
                                             : nir_umod(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_min:
      if (out_type == GLSL_TYPE_FLOAT)
         result = nir_fmin(&b, srcs[0], srcs[1]);
      else if (out_type == GLSL_TYPE_INT)
         result = nir_imin(&b, srcs[0], srcs[1]);
      else
         result = nir_umin(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_max:
      if (out_type == GLSL_TYPE_FLOAT)
         result = nir_fmax(&b, srcs[0], srcs[1]);
      else if (out_type == GLSL_TYPE_INT)
         result = nir_imax(&b, srcs[0], srcs[1]);
      else
         result = nir_umax(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_pow: result = nir_fpow(&b, srcs[0], srcs[1]); break;
   case ir_binop_bit_and: result = nir_iand(&b, srcs[0], srcs[1]); break;
   case ir_binop_bit_or: result = nir_ior(&b, srcs[0], srcs[1]); break;
   case ir_binop_bit_xor: result = nir_ixor(&b, srcs[0], srcs[1]); break;
   case ir_binop_logic_and:
      result = supports_ints ? nir_iand(&b, srcs[0], srcs[1])
                             : nir_fand(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_logic_or:
      result = supports_ints ? nir_ior(&b, srcs[0], srcs[1])
                             : nir_for(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_logic_xor:
      result = supports_ints ? nir_ixor(&b, srcs[0], srcs[1])
                             : nir_fxor(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_lshift: result = nir_ishl(&b, srcs[0], srcs[1]); break;
   case ir_binop_rshift:
      result = (out_type == GLSL_TYPE_INT) ? nir_ishr(&b, srcs[0], srcs[1])
                                           : nir_ushr(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_imul_high:
      result = (out_type == GLSL_TYPE_INT) ? nir_imul_high(&b, srcs[0], srcs[1])
                                           : nir_umul_high(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_carry:  result = nir_uadd_carry(&b, srcs[0], srcs[1]);  break;
   case ir_binop_borrow: result = nir_usub_borrow(&b, srcs[0], srcs[1]); break;
   case ir_binop_less:
      if (supports_ints) {
         if (types[0] == GLSL_TYPE_FLOAT)
            result = nir_flt(&b, srcs[0], srcs[1]);
         else if (types[0] == GLSL_TYPE_INT)
            result = nir_ilt(&b, srcs[0], srcs[1]);
         else
            result = nir_ult(&b, srcs[0], srcs[1]);
      } else {
         result = nir_slt(&b, srcs[0], srcs[1]);
      }
      break;
   case ir_binop_greater:
      if (supports_ints) {
         if (types[0] == GLSL_TYPE_FLOAT)
            result = nir_flt(&b, srcs[1], srcs[0]);
         else if (types[0] == GLSL_TYPE_INT)
            result = nir_ilt(&b, srcs[1], srcs[0]);
         else
            result = nir_ult(&b, srcs[1], srcs[0]);
      } else {
         result = nir_slt(&b, srcs[1], srcs[0]);
      }
      break;
   case ir_binop_lequal:
      if (supports_ints) {
         if (types[0] == GLSL_TYPE_FLOAT)
            result = nir_fge(&b, srcs[1], srcs[0]);
         else if (types[0] == GLSL_TYPE_INT)
            result = nir_ige(&b, srcs[1], srcs[0]);
         else
            result = nir_uge(&b, srcs[1], srcs[0]);
      } else {
         result = nir_slt(&b, srcs[1], srcs[0]);
      }
      break;
   case ir_binop_gequal:
      if (supports_ints) {
         if (types[0] == GLSL_TYPE_FLOAT)
            result = nir_fge(&b, srcs[0], srcs[1]);
         else if (types[0] == GLSL_TYPE_INT)
            result = nir_ige(&b, srcs[0], srcs[1]);
         else
            result = nir_uge(&b, srcs[0], srcs[1]);
      } else {
         result = nir_slt(&b, srcs[0], srcs[1]);
      }
      break;
   case ir_binop_equal:
      if (supports_ints) {
         if (types[0] == GLSL_TYPE_FLOAT)
            result = nir_feq(&b, srcs[0], srcs[1]);
         else
            result = nir_ieq(&b, srcs[0], srcs[1]);
      } else {
         result = nir_seq(&b, srcs[0], srcs[1]);
      }
      break;
   case ir_binop_nequal:
      if (supports_ints) {
         if (types[0] == GLSL_TYPE_FLOAT)
            result = nir_fne(&b, srcs[0], srcs[1]);
         else
            result = nir_ine(&b, srcs[0], srcs[1]);
      } else {
         result = nir_sne(&b, srcs[0], srcs[1]);
      }
      break;
   case ir_binop_all_equal:
      if (supports_ints) {
         if (types[0] == GLSL_TYPE_FLOAT) {
            switch (ir->operands[0]->type->vector_elements) {
               case 1: result = nir_feq(&b, srcs[0], srcs[1]); break;
               case 2: result = nir_ball_fequal2(&b, srcs[0], srcs[1]); break;
               case 3: result = nir_ball_fequal3(&b, srcs[0], srcs[1]); break;
               case 4: result = nir_ball_fequal4(&b, srcs[0], srcs[1]); break;
               default:
                  unreachable("not reached");
            }
         } else {
            switch (ir->operands[0]->type->vector_elements) {
               case 1: result = nir_ieq(&b, srcs[0], srcs[1]); break;
               case 2: result = nir_ball_iequal2(&b, srcs[0], srcs[1]); break;
               case 3: result = nir_ball_iequal3(&b, srcs[0], srcs[1]); break;
               case 4: result = nir_ball_iequal4(&b, srcs[0], srcs[1]); break;
               default:
                  unreachable("not reached");
            }
         }
      } else {
         switch (ir->operands[0]->type->vector_elements) {
            case 1: result = nir_seq(&b, srcs[0], srcs[1]); break;
            case 2: result = nir_fall_equal2(&b, srcs[0], srcs[1]); break;
            case 3: result = nir_fall_equal3(&b, srcs[0], srcs[1]); break;
            case 4: result = nir_fall_equal4(&b, srcs[0], srcs[1]); break;
            default:
               unreachable("not reached");
         }
      }
      break;
   case ir_binop_any_nequal:
      if (supports_ints) {
         if (types[0] == GLSL_TYPE_FLOAT) {
            switch (ir->operands[0]->type->vector_elements) {
               case 1: result = nir_fne(&b, srcs[0], srcs[1]); break;
               case 2: result = nir_bany_fnequal2(&b, srcs[0], srcs[1]); break;
               case 3: result = nir_bany_fnequal3(&b, srcs[0], srcs[1]); break;
               case 4: result = nir_bany_fnequal4(&b, srcs[0], srcs[1]); break;
               default:
                  unreachable("not reached");
            }
         } else {
            switch (ir->operands[0]->type->vector_elements) {
               case 1: result = nir_ine(&b, srcs[0], srcs[1]); break;
               case 2: result = nir_bany_inequal2(&b, srcs[0], srcs[1]); break;
               case 3: result = nir_bany_inequal3(&b, srcs[0], srcs[1]); break;
               case 4: result = nir_bany_inequal4(&b, srcs[0], srcs[1]); break;
               default:
                  unreachable("not reached");
            }
         }
      } else {
         switch (ir->operands[0]->type->vector_elements) {
            case 1: result = nir_sne(&b, srcs[0], srcs[1]); break;
            case 2: result = nir_fany_nequal2(&b, srcs[0], srcs[1]); break;
            case 3: result = nir_fany_nequal3(&b, srcs[0], srcs[1]); break;
            case 4: result = nir_fany_nequal4(&b, srcs[0], srcs[1]); break;
            default:
               unreachable("not reached");
         }
      }
      break;
   case ir_binop_dot:
      switch (ir->operands[0]->type->vector_elements) {
         case 2: result = nir_fdot2(&b, srcs[0], srcs[1]); break;
         case 3: result = nir_fdot3(&b, srcs[0], srcs[1]); break;
         case 4: result = nir_fdot4(&b, srcs[0], srcs[1]); break;
         default:
            unreachable("not reached");
      }
      break;

   case ir_binop_pack_half_2x16_split:
         result = nir_pack_half_2x16_split(&b, srcs[0], srcs[1]);
         break;
   case ir_binop_bfm:   result = nir_bfm(&b, srcs[0], srcs[1]);   break;
   case ir_binop_ldexp: result = nir_ldexp(&b, srcs[0], srcs[1]); break;
   case ir_triop_fma:
      result = nir_ffma(&b, srcs[0], srcs[1], srcs[2]);
      break;
   case ir_triop_lrp:
      result = nir_flrp(&b, srcs[0], srcs[1], srcs[2]);
      break;
   case ir_triop_csel:
      if (supports_ints)
         result = nir_bcsel(&b, srcs[0], srcs[1], srcs[2]);
      else
         result = nir_fcsel(&b, srcs[0], srcs[1], srcs[2]);
      break;
   case ir_triop_bfi:
      result = nir_bfi(&b, srcs[0], srcs[1], srcs[2]);
      break;
   case ir_triop_bitfield_extract:
      result = (out_type == GLSL_TYPE_INT) ?
         nir_ibitfield_extract(&b, srcs[0], srcs[1], srcs[2]) :
         nir_ubitfield_extract(&b, srcs[0], srcs[1], srcs[2]);
      break;
   case ir_quadop_bitfield_insert:
      result = nir_bitfield_insert(&b, srcs[0], srcs[1], srcs[2], srcs[3]);
      break;
   case ir_quadop_vector:
      result = nir_vec(&b, srcs, ir->type->vector_elements);
      break;

   default:
      unreachable("not reached");
   }
}

void
nir_visitor::visit(ir_swizzle *ir)
{
   unsigned swizzle[4] = { ir->mask.x, ir->mask.y, ir->mask.z, ir->mask.w };
   result = nir_swizzle(&b, evaluate_rvalue(ir->val), swizzle,
                        ir->type->vector_elements, !supports_ints);
}

void
nir_visitor::visit(ir_texture *ir)
{
   unsigned num_srcs;
   nir_texop op;
   switch (ir->op) {
   case ir_tex:
      op = nir_texop_tex;
      num_srcs = 1; /* coordinate */
      break;

   case ir_txb:
   case ir_txl:
      op = (ir->op == ir_txb) ? nir_texop_txb : nir_texop_txl;
      num_srcs = 2; /* coordinate, bias/lod */
      break;

   case ir_txd:
      op = nir_texop_txd; /* coordinate, dPdx, dPdy */
      num_srcs = 3;
      break;

   case ir_txf:
      op = nir_texop_txf;
      if (ir->lod_info.lod != NULL)
         num_srcs = 2; /* coordinate, lod */
      else
         num_srcs = 1; /* coordinate */
      break;

   case ir_txf_ms:
      op = nir_texop_txf_ms;
      num_srcs = 2; /* coordinate, sample_index */
      break;

   case ir_txs:
      op = nir_texop_txs;
      if (ir->lod_info.lod != NULL)
         num_srcs = 1; /* lod */
      else
         num_srcs = 0;
      break;

   case ir_lod:
      op = nir_texop_lod;
      num_srcs = 1; /* coordinate */
      break;

   case ir_tg4:
      op = nir_texop_tg4;
      num_srcs = 1; /* coordinate */
      break;

   case ir_query_levels:
      op = nir_texop_query_levels;
      num_srcs = 0;
      break;

   case ir_texture_samples:
      op = nir_texop_texture_samples;
      num_srcs = 0;
      break;

   default:
      unreachable("not reached");
   }

   if (ir->projector != NULL)
      num_srcs++;
   if (ir->shadow_comparitor != NULL)
      num_srcs++;
   if (ir->offset != NULL && ir->offset->as_constant() == NULL)
      num_srcs++;

   nir_tex_instr *instr = nir_tex_instr_create(this->shader, num_srcs);

   instr->op = op;
   instr->sampler_dim =
      (glsl_sampler_dim) ir->sampler->type->sampler_dimensionality;
   instr->is_array = ir->sampler->type->sampler_array;
   instr->is_shadow = ir->sampler->type->sampler_shadow;
   if (instr->is_shadow)
      instr->is_new_style_shadow = (ir->type->vector_elements == 1);
   switch (ir->type->base_type) {
   case GLSL_TYPE_FLOAT:
      instr->dest_type = nir_type_float;
      break;
   case GLSL_TYPE_INT:
      instr->dest_type = nir_type_int;
      break;
   case GLSL_TYPE_UINT:
      instr->dest_type = nir_type_unsigned;
      break;
   default:
      unreachable("not reached");
   }

   instr->sampler = evaluate_deref(&instr->instr, ir->sampler);

   unsigned src_number = 0;

   if (ir->coordinate != NULL) {
      instr->coord_components = ir->coordinate->type->vector_elements;
      instr->src[src_number].src =
         nir_src_for_ssa(evaluate_rvalue(ir->coordinate));
      instr->src[src_number].src_type = nir_tex_src_coord;
      src_number++;
   }

   if (ir->projector != NULL) {
      instr->src[src_number].src =
         nir_src_for_ssa(evaluate_rvalue(ir->projector));
      instr->src[src_number].src_type = nir_tex_src_projector;
      src_number++;
   }

   if (ir->shadow_comparitor != NULL) {
      instr->src[src_number].src =
         nir_src_for_ssa(evaluate_rvalue(ir->shadow_comparitor));
      instr->src[src_number].src_type = nir_tex_src_comparitor;
      src_number++;
   }

   if (ir->offset != NULL) {
      /* we don't support multiple offsets yet */
      assert(ir->offset->type->is_vector() || ir->offset->type->is_scalar());

      ir_constant *const_offset = ir->offset->as_constant();
      if (const_offset != NULL) {
         for (unsigned i = 0; i < const_offset->type->vector_elements; i++)
            instr->const_offset[i] = const_offset->value.i[i];
      } else {
         instr->src[src_number].src =
            nir_src_for_ssa(evaluate_rvalue(ir->offset));
         instr->src[src_number].src_type = nir_tex_src_offset;
         src_number++;
      }
   }

   switch (ir->op) {
   case ir_txb:
      instr->src[src_number].src =
         nir_src_for_ssa(evaluate_rvalue(ir->lod_info.bias));
      instr->src[src_number].src_type = nir_tex_src_bias;
      src_number++;
      break;

   case ir_txl:
   case ir_txf:
   case ir_txs:
      if (ir->lod_info.lod != NULL) {
         instr->src[src_number].src =
            nir_src_for_ssa(evaluate_rvalue(ir->lod_info.lod));
         instr->src[src_number].src_type = nir_tex_src_lod;
         src_number++;
      }
      break;

   case ir_txd:
      instr->src[src_number].src =
         nir_src_for_ssa(evaluate_rvalue(ir->lod_info.grad.dPdx));
      instr->src[src_number].src_type = nir_tex_src_ddx;
      src_number++;
      instr->src[src_number].src =
         nir_src_for_ssa(evaluate_rvalue(ir->lod_info.grad.dPdy));
      instr->src[src_number].src_type = nir_tex_src_ddy;
      src_number++;
      break;

   case ir_txf_ms:
      instr->src[src_number].src =
         nir_src_for_ssa(evaluate_rvalue(ir->lod_info.sample_index));
      instr->src[src_number].src_type = nir_tex_src_ms_index;
      src_number++;
      break;

   case ir_tg4:
      instr->component = ir->lod_info.component->as_constant()->value.u[0];
      break;

   default:
      break;
   }

   assert(src_number == num_srcs);

   add_instr(&instr->instr, nir_tex_instr_dest_size(instr));
}

void
nir_visitor::visit(ir_constant *ir)
{
   /*
    * We don't know if this variable is an an array or struct that gets
    * dereferenced, so do the safe thing an make it a variable with a
    * constant initializer and return a dereference.
    */

   nir_variable *var =
      nir_local_variable_create(this->impl, ir->type, "const_temp");
   var->data.read_only = true;
   var->constant_initializer = constant_copy(ir, var);

   this->deref_head = nir_deref_var_create(this->shader, var);
   this->deref_tail = &this->deref_head->deref;
}

void
nir_visitor::visit(ir_dereference_variable *ir)
{
   struct hash_entry *entry =
      _mesa_hash_table_search(this->var_table, ir->var);
   assert(entry);
   nir_variable *var = (nir_variable *) entry->data;

   nir_deref_var *deref = nir_deref_var_create(this->shader, var);
   this->deref_head = deref;
   this->deref_tail = &deref->deref;
}

void
nir_visitor::visit(ir_dereference_record *ir)
{
   ir->record->accept(this);

   int field_index = this->deref_tail->type->field_index(ir->field);
   assert(field_index >= 0);

   nir_deref_struct *deref = nir_deref_struct_create(this->deref_tail, field_index);
   deref->deref.type = ir->type;
   this->deref_tail->child = &deref->deref;
   this->deref_tail = &deref->deref;
}

void
nir_visitor::visit(ir_dereference_array *ir)
{
   nir_deref_array *deref = nir_deref_array_create(this->shader);
   deref->deref.type = ir->type;

   ir_constant *const_index = ir->array_index->as_constant();
   if (const_index != NULL) {
      deref->deref_array_type = nir_deref_array_type_direct;
      deref->base_offset = const_index->value.u[0];
   } else {
      deref->deref_array_type = nir_deref_array_type_indirect;
      deref->indirect =
         nir_src_for_ssa(evaluate_rvalue(ir->array_index));
   }

   ir->array->accept(this);

   this->deref_tail->child = &deref->deref;
   ralloc_steal(this->deref_tail, deref);
   this->deref_tail = &deref->deref;
}

void
nir_visitor::visit(ir_barrier *ir)
{
   nir_intrinsic_instr *instr =
      nir_intrinsic_instr_create(this->shader, nir_intrinsic_barrier);
   nir_builder_instr_insert(&b, &instr->instr);
}