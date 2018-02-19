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
#include "ir_visitor.h"
#include "ir_hierarchical_visitor.h"
#include "ir.h"
#include "compiler/nir/nir_control_flow.h"
#include "compiler/nir/nir_builder.h"
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

   void create_function(ir_function_signature *ir);

private:
   void add_instr(nir_instr *instr, unsigned num_components, unsigned bit_size);
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

} /* end of anonymous namespace */

static void
nir_remap_attributes(nir_shader *shader,
                     const nir_shader_compiler_options *options)
{
   if (options->vs_inputs_dual_locations) {
      nir_foreach_variable(var, &shader->inputs) {
         var->data.location +=
            _mesa_bitcount_64(shader->info.vs.double_inputs &
                              BITFIELD64_MASK(var->data.location));
      }
   }

   /* Once the remap is done, reset double_inputs_read, so later it will have
    * which location/slots are doubles */
   shader->info.vs.double_inputs = 0;
}

nir_shader *
glsl_to_nir(const struct gl_shader_program *shader_prog,
            gl_shader_stage stage,
            const nir_shader_compiler_options *options)
{
   struct gl_linked_shader *sh = shader_prog->_LinkedShaders[stage];

   nir_shader *shader = nir_shader_create(NULL, stage, options,
                                          &sh->Program->info);

   nir_visitor v1(shader);
   nir_function_visitor v2(&v1);
   v2.run(sh->ir);
   visit_exec_list(sh->ir, &v1);

   nir_lower_constant_initializers(shader, (nir_variable_mode)~0);

   /* Remap the locations to slots so those requiring two slots will occupy
    * two locations. For instance, if we have in the IR code a dvec3 attr0 in
    * location 0 and vec4 attr1 in location 1, in NIR attr0 will use
    * locations/slots 0 and 1, and attr1 will use location/slot 2 */
   if (shader->info.stage == MESA_SHADER_VERTEX)
      nir_remap_attributes(shader, options);

   shader->info.name = ralloc_asprintf(shader, "GLSL%d", shader_prog->Name);
   if (shader_prog->Label)
      shader->info.label = ralloc_strdup(shader, shader_prog->Label);

   /* Check for transform feedback varyings specified via the API */
   shader->info.has_transform_feedback_varyings =
      shader_prog->TransformFeedback.NumVarying > 0;

   /* Check for transform feedback varyings specified in the Shader */
   if (shader_prog->last_vert_prog)
      shader->info.has_transform_feedback_varyings |=
         shader_prog->last_vert_prog->sh.LinkedTransformFeedback->NumVarying > 0;

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
   this->result = NULL;
   this->impl = NULL;
   this->var = NULL;
   this->deref_head = NULL;
   this->deref_tail = NULL;
   memset(&this->b, 0, sizeof(this->b));
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

   nir_constant *ret = rzalloc(mem_ctx, nir_constant);

   const unsigned rows = ir->type->vector_elements;
   const unsigned cols = ir->type->matrix_columns;
   unsigned i;

   ret->num_elements = 0;
   switch (ir->type->base_type) {
   case GLSL_TYPE_UINT:
      /* Only float base types can be matrices. */
      assert(cols == 1);

      for (unsigned r = 0; r < rows; r++)
         ret->values[0].u32[r] = ir->value.u[r];

      break;

   case GLSL_TYPE_INT:
      /* Only float base types can be matrices. */
      assert(cols == 1);

      for (unsigned r = 0; r < rows; r++)
         ret->values[0].i32[r] = ir->value.i[r];

      break;

   case GLSL_TYPE_FLOAT:
      for (unsigned c = 0; c < cols; c++) {
         for (unsigned r = 0; r < rows; r++)
            ret->values[c].f32[r] = ir->value.f[c * rows + r];
      }
      break;

   case GLSL_TYPE_DOUBLE:
      for (unsigned c = 0; c < cols; c++) {
         for (unsigned r = 0; r < rows; r++)
            ret->values[c].f64[r] = ir->value.d[c * rows + r];
      }
      break;

   case GLSL_TYPE_UINT64:
      /* Only float base types can be matrices. */
      assert(cols == 1);

      for (unsigned r = 0; r < rows; r++)
         ret->values[0].u64[r] = ir->value.u64[r];
      break;

   case GLSL_TYPE_INT64:
      /* Only float base types can be matrices. */
      assert(cols == 1);

      for (unsigned r = 0; r < rows; r++)
         ret->values[0].i64[r] = ir->value.i64[r];
      break;

   case GLSL_TYPE_BOOL:
      /* Only float base types can be matrices. */
      assert(cols == 1);

      for (unsigned r = 0; r < rows; r++)
         ret->values[0].u32[r] = ir->value.b[r] ? NIR_TRUE : NIR_FALSE;

      break;

   case GLSL_TYPE_STRUCT:
   case GLSL_TYPE_ARRAY:
      ret->elements = ralloc_array(mem_ctx, nir_constant *,
                                   ir->type->length);
      ret->num_elements = ir->type->length;

      for (i = 0; i < ir->type->length; i++)
         ret->elements[i] = constant_copy(ir->const_elements[i], mem_ctx);
      break;

   default:
      unreachable("not reached");
   }

   return ret;
}

void
nir_visitor::visit(ir_variable *ir)
{
   /* TODO: In future we should switch to using the NIR lowering pass but for
    * now just ignore these variables as GLSL IR should have lowered them.
    * Anything remaining are just dead vars that weren't cleaned up.
    */
   if (ir->data.mode == ir_var_shader_shared)
      return;

   nir_variable *var = rzalloc(shader, nir_variable);
   var->type = ir->type;
   var->name = ralloc_strdup(var, ir->name);

   var->data.always_active_io = ir->data.always_active_io;
   var->data.read_only = ir->data.read_only;
   var->data.centroid = ir->data.centroid;
   var->data.sample = ir->data.sample;
   var->data.patch = ir->data.patch;
   var->data.invariant = ir->data.invariant;
   var->data.location = ir->data.location;
   var->data.stream = ir->data.stream;
   var->data.compact = false;

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
      if (shader->info.stage == MESA_SHADER_FRAGMENT &&
          ir->data.location == VARYING_SLOT_FACE) {
         /* For whatever reason, GLSL IR makes gl_FrontFacing an input */
         var->data.location = SYSTEM_VALUE_FRONT_FACE;
         var->data.mode = nir_var_system_value;
      } else if (shader->info.stage == MESA_SHADER_GEOMETRY &&
                 ir->data.location == VARYING_SLOT_PRIMITIVE_ID) {
         /* For whatever reason, GLSL IR makes gl_PrimitiveIDIn an input */
         var->data.location = SYSTEM_VALUE_PRIMITIVE_ID;
         var->data.mode = nir_var_system_value;
      } else {
         var->data.mode = nir_var_shader_in;

         if (shader->info.stage == MESA_SHADER_TESS_EVAL &&
             (ir->data.location == VARYING_SLOT_TESS_LEVEL_INNER ||
              ir->data.location == VARYING_SLOT_TESS_LEVEL_OUTER)) {
            var->data.compact = ir->type->without_array()->is_scalar();
         }
      }

      /* Mark all the locations that require two slots */
      if (shader->info.stage == MESA_SHADER_VERTEX &&
          glsl_type_is_dual_slot(glsl_without_array(var->type))) {
         for (uint i = 0; i < glsl_count_attribute_slots(var->type, true); i++) {
            uint64_t bitfield = BITFIELD64_BIT(var->data.location + i);
            shader->info.vs.double_inputs |= bitfield;
         }
      }
      break;

   case ir_var_shader_out:
      var->data.mode = nir_var_shader_out;
      if (shader->info.stage == MESA_SHADER_TESS_CTRL &&
          (ir->data.location == VARYING_SLOT_TESS_LEVEL_INNER ||
           ir->data.location == VARYING_SLOT_TESS_LEVEL_OUTER)) {
         var->data.compact = ir->type->without_array()->is_scalar();
      }
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
   var->data.location_frac = ir->data.location_frac;

   if (var->data.pixel_center_integer) {
      assert(shader->info.stage == MESA_SHADER_FRAGMENT);
      shader->info.fs.pixel_center_integer = true;
   }

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
   var->data.descriptor_set = 0;
   var->data.binding = ir->data.binding;
   var->data.offset = ir->data.offset;
   var->data.image.read_only = ir->data.memory_read_only;
   var->data.image.write_only = ir->data.memory_write_only;
   var->data.image.coherent = ir->data.memory_coherent;
   var->data.image._volatile = ir->data.memory_volatile;
   var->data.image.restrict_flag = ir->data.memory_restrict;
   var->data.image.format = ir->data.image_format;
   var->data.fb_fetch_output = ir->data.fb_fetch_output;

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
   foreach_in_list(ir_function_signature, sig, &ir->signatures) {
      visitor->create_function(sig);
   }
   return visit_continue_with_parent;
}

void
nir_visitor::create_function(ir_function_signature *ir)
{
   if (ir->is_intrinsic())
      return;

   nir_function *func = nir_function_create(shader, ir->function_name());

   assert(ir->parameters.is_empty());
   assert(ir->return_type == glsl_type::void_type);

   _mesa_hash_table_insert(this->overload_table, ir, func);
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
   if (ir->is_intrinsic())
      return;

   struct hash_entry *entry =
      _mesa_hash_table_search(this->overload_table, ir);

   assert(entry);
   nir_function *func = (nir_function *) entry->data;

   if (ir->is_defined) {
      nir_function_impl *impl = nir_function_impl_create(func);
      this->impl = impl;

      assert(strcmp(func->name, "main") == 0);
      assert(ir->parameters.is_empty());
      assert(func->return_type == glsl_type::void_type);

      this->is_global = false;

      nir_builder_init(&b, impl);
      b.cursor = nir_after_cf_list(&impl->body);
      visit_exec_list(&ir->body, this);

      this->is_global = true;
   } else {
      func->impl = NULL;
   }
}

void
nir_visitor::visit(ir_loop *ir)
{
   nir_push_loop(&b);
   visit_exec_list(&ir->body_instructions, this);
   nir_pop_loop(&b, NULL);
}

void
nir_visitor::visit(ir_if *ir)
{
   nir_push_if(&b, evaluate_rvalue(ir->condition));
   visit_exec_list(&ir->then_instructions, this);
   nir_push_else(&b, NULL);
   visit_exec_list(&ir->else_instructions, this);
   nir_pop_if(&b, NULL);
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
   nir_intrinsic_set_stream_id(instr, ir->stream_id());
   nir_builder_instr_insert(&b, &instr->instr);
}

void
nir_visitor::visit(ir_end_primitive *ir)
{
   nir_intrinsic_instr *instr =
      nir_intrinsic_instr_create(this->shader, nir_intrinsic_end_primitive);
   nir_intrinsic_set_stream_id(instr, ir->stream_id());
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
   if (ir->callee->is_intrinsic()) {
      nir_intrinsic_op op;

      switch (ir->callee->intrinsic_id) {
      case ir_intrinsic_atomic_counter_read:
         op = nir_intrinsic_atomic_counter_read_var;
         break;
      case ir_intrinsic_atomic_counter_increment:
         op = nir_intrinsic_atomic_counter_inc_var;
         break;
      case ir_intrinsic_atomic_counter_predecrement:
         op = nir_intrinsic_atomic_counter_dec_var;
         break;
      case ir_intrinsic_atomic_counter_add:
         op = nir_intrinsic_atomic_counter_add_var;
         break;
      case ir_intrinsic_atomic_counter_and:
         op = nir_intrinsic_atomic_counter_and_var;
         break;
      case ir_intrinsic_atomic_counter_or:
         op = nir_intrinsic_atomic_counter_or_var;
         break;
      case ir_intrinsic_atomic_counter_xor:
         op = nir_intrinsic_atomic_counter_xor_var;
         break;
      case ir_intrinsic_atomic_counter_min:
         op = nir_intrinsic_atomic_counter_min_var;
         break;
      case ir_intrinsic_atomic_counter_max:
         op = nir_intrinsic_atomic_counter_max_var;
         break;
      case ir_intrinsic_atomic_counter_exchange:
         op = nir_intrinsic_atomic_counter_exchange_var;
         break;
      case ir_intrinsic_atomic_counter_comp_swap:
         op = nir_intrinsic_atomic_counter_comp_swap_var;
         break;
      case ir_intrinsic_image_load:
         op = nir_intrinsic_image_load;
         break;
      case ir_intrinsic_image_store:
         op = nir_intrinsic_image_store;
         break;
      case ir_intrinsic_image_atomic_add:
         op = nir_intrinsic_image_atomic_add;
         break;
      case ir_intrinsic_image_atomic_min:
         op = nir_intrinsic_image_atomic_min;
         break;
      case ir_intrinsic_image_atomic_max:
         op = nir_intrinsic_image_atomic_max;
         break;
      case ir_intrinsic_image_atomic_and:
         op = nir_intrinsic_image_atomic_and;
         break;
      case ir_intrinsic_image_atomic_or:
         op = nir_intrinsic_image_atomic_or;
         break;
      case ir_intrinsic_image_atomic_xor:
         op = nir_intrinsic_image_atomic_xor;
         break;
      case ir_intrinsic_image_atomic_exchange:
         op = nir_intrinsic_image_atomic_exchange;
         break;
      case ir_intrinsic_image_atomic_comp_swap:
         op = nir_intrinsic_image_atomic_comp_swap;
         break;
      case ir_intrinsic_memory_barrier:
         op = nir_intrinsic_memory_barrier;
         break;
      case ir_intrinsic_image_size:
         op = nir_intrinsic_image_size;
         break;
      case ir_intrinsic_image_samples:
         op = nir_intrinsic_image_samples;
         break;
      case ir_intrinsic_ssbo_store:
         op = nir_intrinsic_store_ssbo;
         break;
      case ir_intrinsic_ssbo_load:
         op = nir_intrinsic_load_ssbo;
         break;
      case ir_intrinsic_ssbo_atomic_add:
         op = nir_intrinsic_ssbo_atomic_add;
         break;
      case ir_intrinsic_ssbo_atomic_and:
         op = nir_intrinsic_ssbo_atomic_and;
         break;
      case ir_intrinsic_ssbo_atomic_or:
         op = nir_intrinsic_ssbo_atomic_or;
         break;
      case ir_intrinsic_ssbo_atomic_xor:
         op = nir_intrinsic_ssbo_atomic_xor;
         break;
      case ir_intrinsic_ssbo_atomic_min:
         assert(ir->return_deref);
         if (ir->return_deref->type == glsl_type::int_type)
            op = nir_intrinsic_ssbo_atomic_imin;
         else if (ir->return_deref->type == glsl_type::uint_type)
            op = nir_intrinsic_ssbo_atomic_umin;
         else
            unreachable("Invalid type");
         break;
      case ir_intrinsic_ssbo_atomic_max:
         assert(ir->return_deref);
         if (ir->return_deref->type == glsl_type::int_type)
            op = nir_intrinsic_ssbo_atomic_imax;
         else if (ir->return_deref->type == glsl_type::uint_type)
            op = nir_intrinsic_ssbo_atomic_umax;
         else
            unreachable("Invalid type");
         break;
      case ir_intrinsic_ssbo_atomic_exchange:
         op = nir_intrinsic_ssbo_atomic_exchange;
         break;
      case ir_intrinsic_ssbo_atomic_comp_swap:
         op = nir_intrinsic_ssbo_atomic_comp_swap;
         break;
      case ir_intrinsic_shader_clock:
         op = nir_intrinsic_shader_clock;
         break;
      case ir_intrinsic_group_memory_barrier:
         op = nir_intrinsic_group_memory_barrier;
         break;
      case ir_intrinsic_memory_barrier_atomic_counter:
         op = nir_intrinsic_memory_barrier_atomic_counter;
         break;
      case ir_intrinsic_memory_barrier_buffer:
         op = nir_intrinsic_memory_barrier_buffer;
         break;
      case ir_intrinsic_memory_barrier_image:
         op = nir_intrinsic_memory_barrier_image;
         break;
      case ir_intrinsic_memory_barrier_shared:
         op = nir_intrinsic_memory_barrier_shared;
         break;
      case ir_intrinsic_shared_load:
         op = nir_intrinsic_load_shared;
         break;
      case ir_intrinsic_shared_store:
         op = nir_intrinsic_store_shared;
         break;
      case ir_intrinsic_shared_atomic_add:
         op = nir_intrinsic_shared_atomic_add;
         break;
      case ir_intrinsic_shared_atomic_and:
         op = nir_intrinsic_shared_atomic_and;
         break;
      case ir_intrinsic_shared_atomic_or:
         op = nir_intrinsic_shared_atomic_or;
         break;
      case ir_intrinsic_shared_atomic_xor:
         op = nir_intrinsic_shared_atomic_xor;
         break;
      case ir_intrinsic_shared_atomic_min:
         assert(ir->return_deref);
         if (ir->return_deref->type == glsl_type::int_type)
            op = nir_intrinsic_shared_atomic_imin;
         else if (ir->return_deref->type == glsl_type::uint_type)
            op = nir_intrinsic_shared_atomic_umin;
         else
            unreachable("Invalid type");
         break;
      case ir_intrinsic_shared_atomic_max:
         assert(ir->return_deref);
         if (ir->return_deref->type == glsl_type::int_type)
            op = nir_intrinsic_shared_atomic_imax;
         else if (ir->return_deref->type == glsl_type::uint_type)
            op = nir_intrinsic_shared_atomic_umax;
         else
            unreachable("Invalid type");
         break;
      case ir_intrinsic_shared_atomic_exchange:
         op = nir_intrinsic_shared_atomic_exchange;
         break;
      case ir_intrinsic_shared_atomic_comp_swap:
         op = nir_intrinsic_shared_atomic_comp_swap;
         break;
      case ir_intrinsic_vote_any:
         op = nir_intrinsic_vote_any;
         break;
      case ir_intrinsic_vote_all:
         op = nir_intrinsic_vote_all;
         break;
      case ir_intrinsic_vote_eq:
         op = nir_intrinsic_vote_eq;
         break;
      case ir_intrinsic_ballot:
         op = nir_intrinsic_ballot;
         break;
      case ir_intrinsic_read_invocation:
         op = nir_intrinsic_read_invocation;
         break;
      case ir_intrinsic_read_first_invocation:
         op = nir_intrinsic_read_first_invocation;
         break;
      default:
         unreachable("not reached");
      }

      nir_intrinsic_instr *instr = nir_intrinsic_instr_create(shader, op);
      nir_dest *dest = &instr->dest;

      switch (op) {
      case nir_intrinsic_atomic_counter_read_var:
      case nir_intrinsic_atomic_counter_inc_var:
      case nir_intrinsic_atomic_counter_dec_var:
      case nir_intrinsic_atomic_counter_add_var:
      case nir_intrinsic_atomic_counter_min_var:
      case nir_intrinsic_atomic_counter_max_var:
      case nir_intrinsic_atomic_counter_and_var:
      case nir_intrinsic_atomic_counter_or_var:
      case nir_intrinsic_atomic_counter_xor_var:
      case nir_intrinsic_atomic_counter_exchange_var:
      case nir_intrinsic_atomic_counter_comp_swap_var: {
         /* Set the counter variable dereference. */
         exec_node *param = ir->actual_parameters.get_head();
         ir_dereference *counter = (ir_dereference *)param;

         instr->variables[0] = evaluate_deref(&instr->instr, counter);
         param = param->get_next();

         /* Set the intrinsic destination. */
         if (ir->return_deref) {
            nir_ssa_dest_init(&instr->instr, &instr->dest, 1, 32, NULL);
         }

         /* Set the intrinsic parameters. */
         if (!param->is_tail_sentinel()) {
            instr->src[0] =
               nir_src_for_ssa(evaluate_rvalue((ir_dereference *)param));
            param = param->get_next();
         }

         if (!param->is_tail_sentinel()) {
            instr->src[1] =
               nir_src_for_ssa(evaluate_rvalue((ir_dereference *)param));
            param = param->get_next();
         }

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
            nir_ssa_undef_instr_create(shader, 1, 32);
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
            unsigned num_components = ir->return_deref->type->vector_elements;
            if (instr->intrinsic == nir_intrinsic_image_size)
               instr->num_components = num_components;
            nir_ssa_dest_init(&instr->instr, &instr->dest,
                              num_components, 32, NULL);
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
         nir_ssa_dest_init(&instr->instr, &instr->dest, 2, 32, NULL);
         instr->num_components = 2;
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

         instr->src[0] = nir_src_for_ssa(evaluate_rvalue(val));
         instr->src[1] = nir_src_for_ssa(evaluate_rvalue(block));
         instr->src[2] = nir_src_for_ssa(evaluate_rvalue(offset));
         nir_intrinsic_set_write_mask(instr, write_mask->value.u[0]);
         instr->num_components = val->type->vector_elements;

         nir_builder_instr_insert(&b, &instr->instr);
         break;
      }
      case nir_intrinsic_load_ssbo: {
         exec_node *param = ir->actual_parameters.get_head();
         ir_rvalue *block = ((ir_instruction *)param)->as_rvalue();

         param = param->get_next();
         ir_rvalue *offset = ((ir_instruction *)param)->as_rvalue();

         instr->src[0] = nir_src_for_ssa(evaluate_rvalue(block));
         instr->src[1] = nir_src_for_ssa(evaluate_rvalue(offset));

         const glsl_type *type = ir->return_deref->var->type;
         instr->num_components = type->vector_elements;

         /* Setup destination register */
         unsigned bit_size = glsl_get_bit_size(type);
         nir_ssa_dest_init(&instr->instr, &instr->dest,
                           type->vector_elements, bit_size, NULL);

         /* Insert the created nir instruction now since in the case of boolean
          * result we will need to emit another instruction after it
          */
         nir_builder_instr_insert(&b, &instr->instr);

         /*
          * In SSBO/UBO's, a true boolean value is any non-zero value, but we
          * consider a true boolean to be ~0. Fix this up with a != 0
          * comparison.
          */
         if (type->is_boolean()) {
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
                              type->vector_elements, bit_size, NULL);
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
                           ir->return_deref->type->vector_elements, 32, NULL);
         nir_builder_instr_insert(&b, &instr->instr);
         break;
      }
      case nir_intrinsic_load_shared: {
         exec_node *param = ir->actual_parameters.get_head();
         ir_rvalue *offset = ((ir_instruction *)param)->as_rvalue();

         nir_intrinsic_set_base(instr, 0);
         instr->src[0] = nir_src_for_ssa(evaluate_rvalue(offset));

         const glsl_type *type = ir->return_deref->var->type;
         instr->num_components = type->vector_elements;

         /* Setup destination register */
         unsigned bit_size = glsl_get_bit_size(type);
         nir_ssa_dest_init(&instr->instr, &instr->dest,
                           type->vector_elements, bit_size, NULL);

         nir_builder_instr_insert(&b, &instr->instr);
         break;
      }
      case nir_intrinsic_store_shared: {
         exec_node *param = ir->actual_parameters.get_head();
         ir_rvalue *offset = ((ir_instruction *)param)->as_rvalue();

         param = param->get_next();
         ir_rvalue *val = ((ir_instruction *)param)->as_rvalue();

         param = param->get_next();
         ir_constant *write_mask = ((ir_instruction *)param)->as_constant();
         assert(write_mask);

         nir_intrinsic_set_base(instr, 0);
         instr->src[1] = nir_src_for_ssa(evaluate_rvalue(offset));

         nir_intrinsic_set_write_mask(instr, write_mask->value.u[0]);

         instr->src[0] = nir_src_for_ssa(evaluate_rvalue(val));
         instr->num_components = val->type->vector_elements;

         nir_builder_instr_insert(&b, &instr->instr);
         break;
      }
      case nir_intrinsic_shared_atomic_add:
      case nir_intrinsic_shared_atomic_imin:
      case nir_intrinsic_shared_atomic_umin:
      case nir_intrinsic_shared_atomic_imax:
      case nir_intrinsic_shared_atomic_umax:
      case nir_intrinsic_shared_atomic_and:
      case nir_intrinsic_shared_atomic_or:
      case nir_intrinsic_shared_atomic_xor:
      case nir_intrinsic_shared_atomic_exchange:
      case nir_intrinsic_shared_atomic_comp_swap: {
         int param_count = ir->actual_parameters.length();
         assert(param_count == 2 || param_count == 3);

         /* Offset */
         exec_node *param = ir->actual_parameters.get_head();
         ir_instruction *inst = (ir_instruction *) param;
         instr->src[0] = nir_src_for_ssa(evaluate_rvalue(inst->as_rvalue()));

         /* data1 parameter (this is always present) */
         param = param->get_next();
         inst = (ir_instruction *) param;
         instr->src[1] = nir_src_for_ssa(evaluate_rvalue(inst->as_rvalue()));

         /* data2 parameter (only with atomic_comp_swap) */
         if (param_count == 3) {
            assert(op == nir_intrinsic_shared_atomic_comp_swap);
            param = param->get_next();
            inst = (ir_instruction *) param;
            instr->src[2] =
               nir_src_for_ssa(evaluate_rvalue(inst->as_rvalue()));
         }

         /* Atomic result */
         assert(ir->return_deref);
         unsigned bit_size = glsl_get_bit_size(ir->return_deref->type);
         nir_ssa_dest_init(&instr->instr, &instr->dest,
                           ir->return_deref->type->vector_elements,
                           bit_size, NULL);
         nir_builder_instr_insert(&b, &instr->instr);
         break;
      }
      case nir_intrinsic_vote_any:
      case nir_intrinsic_vote_all:
      case nir_intrinsic_vote_eq: {
         nir_ssa_dest_init(&instr->instr, &instr->dest, 1, 32, NULL);

         ir_rvalue *value = (ir_rvalue *) ir->actual_parameters.get_head();
         instr->src[0] = nir_src_for_ssa(evaluate_rvalue(value));

         nir_builder_instr_insert(&b, &instr->instr);
         break;
      }

      case nir_intrinsic_ballot: {
         nir_ssa_dest_init(&instr->instr, &instr->dest,
                           ir->return_deref->type->vector_elements, 64, NULL);
         instr->num_components = ir->return_deref->type->vector_elements;

         ir_rvalue *value = (ir_rvalue *) ir->actual_parameters.get_head();
         instr->src[0] = nir_src_for_ssa(evaluate_rvalue(value));

         nir_builder_instr_insert(&b, &instr->instr);
         break;
      }
      case nir_intrinsic_read_invocation: {
         nir_ssa_dest_init(&instr->instr, &instr->dest,
                           ir->return_deref->type->vector_elements, 32, NULL);
         instr->num_components = ir->return_deref->type->vector_elements;

         ir_rvalue *value = (ir_rvalue *) ir->actual_parameters.get_head();
         instr->src[0] = nir_src_for_ssa(evaluate_rvalue(value));

         ir_rvalue *invocation = (ir_rvalue *) ir->actual_parameters.get_head()->next;
         instr->src[1] = nir_src_for_ssa(evaluate_rvalue(invocation));

         nir_builder_instr_insert(&b, &instr->instr);
         break;
      }
      case nir_intrinsic_read_first_invocation: {
         nir_ssa_dest_init(&instr->instr, &instr->dest,
                           ir->return_deref->type->vector_elements, 32, NULL);
         instr->num_components = ir->return_deref->type->vector_elements;

         ir_rvalue *value = (ir_rvalue *) ir->actual_parameters.get_head();
         instr->src[0] = nir_src_for_ssa(evaluate_rvalue(value));

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
         nir_intrinsic_set_write_mask(store_instr,
                                      (1 << store_instr->num_components) - 1);

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
   nir_function *callee = (nir_function *) entry->data;

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

   b.exact = ir->lhs->variable_referenced()->data.invariant ||
             ir->lhs->variable_referenced()->data.precise;

   if ((ir->rhs->as_dereference() || ir->rhs->as_constant()) &&
       (ir->write_mask == (1 << num_components) - 1 || ir->write_mask == 0)) {
      /* We're doing a plain-as-can-be copy, so emit a copy_var */
      nir_intrinsic_instr *copy =
         nir_intrinsic_instr_create(this->shader, nir_intrinsic_copy_var);

      copy->variables[0] = evaluate_deref(&copy->instr, ir->lhs);
      copy->variables[1] = evaluate_deref(&copy->instr, ir->rhs);

      if (ir->condition) {
         nir_push_if(&b, evaluate_rvalue(ir->condition));
         nir_builder_instr_insert(&b, &copy->instr);
         nir_pop_if(&b, NULL);
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
      /* GLSL IR will give us the input to the write-masked assignment in a
       * single packed vector.  So, for example, if the writemask is xzw, then
       * we have to swizzle x -> x, y -> z, and z -> w and get the y component
       * from the load.
       */
      unsigned swiz[4];
      unsigned component = 0;
      for (unsigned i = 0; i < 4; i++) {
         swiz[i] = ir->write_mask & (1 << i) ? component++ : 0;
      }
      src = nir_swizzle(&b, src, swiz, num_components, !supports_ints);
   }

   nir_intrinsic_instr *store =
      nir_intrinsic_instr_create(this->shader, nir_intrinsic_store_var);
   store->num_components = ir->lhs->type->vector_elements;
   nir_intrinsic_set_write_mask(store, ir->write_mask);
   store->variables[0] = nir_deref_var_clone(lhs_deref, store);
   store->src[0] = nir_src_for_ssa(src);

   if (ir->condition) {
      nir_push_if(&b, evaluate_rvalue(ir->condition));
      nir_builder_instr_insert(&b, &store->instr);
      nir_pop_if(&b, NULL);
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
nir_visitor::add_instr(nir_instr *instr, unsigned num_components,
                       unsigned bit_size)
{
   nir_dest *dest = get_instr_dest(instr);

   if (dest)
      nir_ssa_dest_init(instr, dest, num_components, bit_size, NULL);

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
      unsigned bit_size = glsl_get_bit_size(ir->type);
      add_instr(&load_instr->instr, ir->type->vector_elements, bit_size);
   }

   return this->result;
}

static bool
type_is_float(glsl_base_type type)
{
   return type == GLSL_TYPE_FLOAT || type == GLSL_TYPE_DOUBLE ||
      type == GLSL_TYPE_FLOAT16;
}

static bool
type_is_signed(glsl_base_type type)
{
   return type == GLSL_TYPE_INT || type == GLSL_TYPE_INT64 ||
      type == GLSL_TYPE_INT16;
}

void
nir_visitor::visit(ir_expression *ir)
{
   /* Some special cases */
   switch (ir->operation) {
   case ir_binop_ubo_load: {
      nir_intrinsic_instr *load =
         nir_intrinsic_instr_create(this->shader, nir_intrinsic_load_ubo);
      unsigned bit_size = glsl_get_bit_size(ir->type);
      load->num_components = ir->type->vector_elements;
      load->src[0] = nir_src_for_ssa(evaluate_rvalue(ir->operands[0]));
      load->src[1] = nir_src_for_ssa(evaluate_rvalue(ir->operands[1]));
      add_instr(&load->instr, ir->type->vector_elements, bit_size);

      /*
       * In UBO's, a true boolean value is any non-zero value, but we consider
       * a true boolean to be ~0. Fix this up with a != 0 comparison.
       */

      if (ir->type->is_boolean())
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

      unsigned bit_size =  glsl_get_bit_size(deref->type);
      add_instr(&intrin->instr, deref->type->vector_elements, bit_size);

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
   for (unsigned i = 0; i < ir->num_operands; i++)
      srcs[i] = evaluate_rvalue(ir->operands[i]);

   glsl_base_type types[4];
   for (unsigned i = 0; i < ir->num_operands; i++)
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
      result = type_is_float(types[0]) ? nir_fneg(&b, srcs[0])
                                       : nir_ineg(&b, srcs[0]);
      break;
   case ir_unop_abs:
      result = type_is_float(types[0]) ? nir_fabs(&b, srcs[0])
                                       : nir_iabs(&b, srcs[0]);
      break;
   case ir_unop_saturate:
      assert(type_is_float(types[0]));
      result = nir_fsat(&b, srcs[0]);
      break;
   case ir_unop_sign:
      result = type_is_float(types[0]) ? nir_fsign(&b, srcs[0])
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
      result = supports_ints ? nir_i2f32(&b, srcs[0]) : nir_fmov(&b, srcs[0]);
      break;
   case ir_unop_u2f:
      result = supports_ints ? nir_u2f32(&b, srcs[0]) : nir_fmov(&b, srcs[0]);
      break;
   case ir_unop_b2f:
      result = supports_ints ? nir_b2f(&b, srcs[0]) : nir_fmov(&b, srcs[0]);
      break;
   case ir_unop_f2i:
   case ir_unop_f2u:
   case ir_unop_f2b:
   case ir_unop_i2b:
   case ir_unop_b2i:
   case ir_unop_b2i64:
   case ir_unop_d2f:
   case ir_unop_f2d:
   case ir_unop_d2i:
   case ir_unop_d2u:
   case ir_unop_d2b:
   case ir_unop_i2d:
   case ir_unop_u2d:
   case ir_unop_i642i:
   case ir_unop_i642u:
   case ir_unop_i642f:
   case ir_unop_i642b:
   case ir_unop_i642d:
   case ir_unop_u642i:
   case ir_unop_u642u:
   case ir_unop_u642f:
   case ir_unop_u642d:
   case ir_unop_i2i64:
   case ir_unop_u2i64:
   case ir_unop_f2i64:
   case ir_unop_d2i64:
   case ir_unop_i2u64:
   case ir_unop_u2u64:
   case ir_unop_f2u64:
   case ir_unop_d2u64:
   case ir_unop_i2u:
   case ir_unop_u2i:
   case ir_unop_i642u64:
   case ir_unop_u642i64: {
      nir_alu_type src_type = nir_get_nir_type_for_glsl_base_type(types[0]);
      nir_alu_type dst_type = nir_get_nir_type_for_glsl_base_type(out_type);
      result = nir_build_alu(&b, nir_type_conversion_op(src_type, dst_type,
                                 nir_rounding_mode_undef),
                                 srcs[0], NULL, NULL, NULL);
      /* b2i and b2f don't have fixed bit-size versions so the builder will
       * just assume 32 and we have to fix it up here.
       */
      result->bit_size = nir_alu_type_get_type_size(dst_type);
      break;
   }

   case ir_unop_bitcast_i2f:
   case ir_unop_bitcast_f2i:
   case ir_unop_bitcast_u2f:
   case ir_unop_bitcast_f2u:
   case ir_unop_bitcast_i642d:
   case ir_unop_bitcast_d2i64:
   case ir_unop_bitcast_u642d:
   case ir_unop_bitcast_d2u64:
   case ir_unop_subroutine_to_int:
      /* no-op */
      result = nir_imov(&b, srcs[0]);
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
   case ir_unop_pack_sampler_2x32:
   case ir_unop_pack_image_2x32:
   case ir_unop_pack_double_2x32:
   case ir_unop_pack_int_2x32:
   case ir_unop_pack_uint_2x32:
      result = nir_pack_64_2x32(&b, srcs[0]);
      break;
   case ir_unop_unpack_sampler_2x32:
   case ir_unop_unpack_image_2x32:
   case ir_unop_unpack_double_2x32:
   case ir_unop_unpack_int_2x32:
   case ir_unop_unpack_uint_2x32:
      result = nir_unpack_64_2x32(&b, srcs[0]);
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
      unsigned bit_size = glsl_get_bit_size(ir->type);
      add_instr(&load->instr, ir->type->vector_elements, bit_size);
      return;
   }

   case ir_binop_add:
      result = type_is_float(out_type) ? nir_fadd(&b, srcs[0], srcs[1])
                                       : nir_iadd(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_sub:
      result = type_is_float(out_type) ? nir_fsub(&b, srcs[0], srcs[1])
                                       : nir_isub(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_mul:
      result = type_is_float(out_type) ? nir_fmul(&b, srcs[0], srcs[1])
                                       : nir_imul(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_div:
      if (type_is_float(out_type))
         result = nir_fdiv(&b, srcs[0], srcs[1]);
      else if (type_is_signed(out_type))
         result = nir_idiv(&b, srcs[0], srcs[1]);
      else
         result = nir_udiv(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_mod:
      result = type_is_float(out_type) ? nir_fmod(&b, srcs[0], srcs[1])
                                       : nir_umod(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_min:
      if (type_is_float(out_type))
         result = nir_fmin(&b, srcs[0], srcs[1]);
      else if (type_is_signed(out_type))
         result = nir_imin(&b, srcs[0], srcs[1]);
      else
         result = nir_umin(&b, srcs[0], srcs[1]);
      break;
   case ir_binop_max:
      if (type_is_float(out_type))
         result = nir_fmax(&b, srcs[0], srcs[1]);
      else if (type_is_signed(out_type))
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
      result = (type_is_signed(out_type)) ? nir_ishr(&b, srcs[0], srcs[1])
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
         if (type_is_float(types[0]))
            result = nir_flt(&b, srcs[0], srcs[1]);
         else if (type_is_signed(types[0]))
            result = nir_ilt(&b, srcs[0], srcs[1]);
         else
            result = nir_ult(&b, srcs[0], srcs[1]);
      } else {
         result = nir_slt(&b, srcs[0], srcs[1]);
      }
      break;
   case ir_binop_gequal:
      if (supports_ints) {
         if (type_is_float(types[0]))
            result = nir_fge(&b, srcs[0], srcs[1]);
         else if (type_is_signed(types[0]))
            result = nir_ige(&b, srcs[0], srcs[1]);
         else
            result = nir_uge(&b, srcs[0], srcs[1]);
      } else {
         result = nir_slt(&b, srcs[0], srcs[1]);
      }
      break;
   case ir_binop_equal:
      if (supports_ints) {
         if (type_is_float(types[0]))
            result = nir_feq(&b, srcs[0], srcs[1]);
         else
            result = nir_ieq(&b, srcs[0], srcs[1]);
      } else {
         result = nir_seq(&b, srcs[0], srcs[1]);
      }
      break;
   case ir_binop_nequal:
      if (supports_ints) {
         if (type_is_float(types[0]))
            result = nir_fne(&b, srcs[0], srcs[1]);
         else
            result = nir_ine(&b, srcs[0], srcs[1]);
      } else {
         result = nir_sne(&b, srcs[0], srcs[1]);
      }
      break;
   case ir_binop_all_equal:
      if (supports_ints) {
         if (type_is_float(types[0])) {
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
         if (type_is_float(types[0])) {
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

   case ir_samples_identical:
      op = nir_texop_samples_identical;
      num_srcs = 1; /* coordinate */
      break;

   default:
      unreachable("not reached");
   }

   if (ir->projector != NULL)
      num_srcs++;
   if (ir->shadow_comparator != NULL)
      num_srcs++;
   if (ir->offset != NULL)
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
   case GLSL_TYPE_BOOL:
   case GLSL_TYPE_UINT:
      instr->dest_type = nir_type_uint;
      break;
   default:
      unreachable("not reached");
   }

   instr->texture = evaluate_deref(&instr->instr, ir->sampler);

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

   if (ir->shadow_comparator != NULL) {
      instr->src[src_number].src =
         nir_src_for_ssa(evaluate_rvalue(ir->shadow_comparator));
      instr->src[src_number].src_type = nir_tex_src_comparator;
      src_number++;
   }

   if (ir->offset != NULL) {
      /* we don't support multiple offsets yet */
      assert(ir->offset->type->is_vector() || ir->offset->type->is_scalar());

      instr->src[src_number].src =
         nir_src_for_ssa(evaluate_rvalue(ir->offset));
      instr->src[src_number].src_type = nir_tex_src_offset;
      src_number++;
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

   unsigned bit_size = glsl_get_bit_size(ir->type);
   add_instr(&instr->instr, nir_tex_instr_dest_size(instr), bit_size);
}

void
nir_visitor::visit(ir_constant *ir)
{
   /*
    * We don't know if this variable is an array or struct that gets
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

   int field_index = ir->field_idx;
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
nir_visitor::visit(ir_barrier *)
{
   nir_intrinsic_instr *instr =
      nir_intrinsic_instr_create(this->shader, nir_intrinsic_barrier);
   nir_builder_instr_insert(&b, &instr->instr);
}
