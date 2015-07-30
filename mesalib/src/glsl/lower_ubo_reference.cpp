/*
 * Copyright © 2012 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file lower_ubo_reference.cpp
 *
 * IR lower pass to replace dereferences of variables in a uniform
 * buffer object with usage of ir_binop_ubo_load expressions, each of
 * which can read data up to the size of a vec4.
 *
 * This relieves drivers of the responsibility to deal with tricky UBO
 * layout issues like std140 structures and row_major matrices on
 * their own.
 */

#include "ir.h"
#include "ir_builder.h"
#include "ir_rvalue_visitor.h"
#include "main/macros.h"
#include "glsl_parser_extras.h"

using namespace ir_builder;

/**
 * Determine if a thing being dereferenced is row-major
 *
 * There is some trickery here.
 *
 * If the thing being dereferenced is a member of uniform block \b without an
 * instance name, then the name of the \c ir_variable is the field name of an
 * interface type.  If this field is row-major, then the thing referenced is
 * row-major.
 *
 * If the thing being dereferenced is a member of uniform block \b with an
 * instance name, then the last dereference in the tree will be an
 * \c ir_dereference_record.  If that record field is row-major, then the
 * thing referenced is row-major.
 */
static bool
is_dereferenced_thing_row_major(const ir_dereference *deref)
{
   bool matrix = false;
   const ir_rvalue *ir = deref;

   while (true) {
      matrix = matrix || ir->type->without_array()->is_matrix();

      switch (ir->ir_type) {
      case ir_type_dereference_array: {
         const ir_dereference_array *const array_deref =
            (const ir_dereference_array *) ir;

         ir = array_deref->array;
         break;
      }

      case ir_type_dereference_record: {
         const ir_dereference_record *const record_deref =
            (const ir_dereference_record *) ir;

         ir = record_deref->record;

         const int idx = ir->type->field_index(record_deref->field);
         assert(idx >= 0);

         const enum glsl_matrix_layout matrix_layout =
            glsl_matrix_layout(ir->type->fields.structure[idx].matrix_layout);

         switch (matrix_layout) {
         case GLSL_MATRIX_LAYOUT_INHERITED:
            break;
         case GLSL_MATRIX_LAYOUT_COLUMN_MAJOR:
            return false;
         case GLSL_MATRIX_LAYOUT_ROW_MAJOR:
            return matrix || deref->type->without_array()->is_record();
         }

         break;
      }

      case ir_type_dereference_variable: {
         const ir_dereference_variable *const var_deref =
            (const ir_dereference_variable *) ir;

         const enum glsl_matrix_layout matrix_layout =
            glsl_matrix_layout(var_deref->var->data.matrix_layout);

         switch (matrix_layout) {
         case GLSL_MATRIX_LAYOUT_INHERITED:
            assert(!matrix);
            return false;
         case GLSL_MATRIX_LAYOUT_COLUMN_MAJOR:
            return false;
         case GLSL_MATRIX_LAYOUT_ROW_MAJOR:
            return matrix || deref->type->without_array()->is_record();
         }

         unreachable("invalid matrix layout");
         break;
      }

      default:
         return false;
      }
   }

   /* The tree must have ended with a dereference that wasn't an
    * ir_dereference_variable.  That is invalid, and it should be impossible.
    */
   unreachable("invalid dereference tree");
   return false;
}

namespace {
class lower_ubo_reference_visitor : public ir_rvalue_enter_visitor {
public:
   lower_ubo_reference_visitor(struct gl_shader *shader)
   : shader(shader)
   {
   }

   void handle_rvalue(ir_rvalue **rvalue);
   ir_visitor_status visit_enter(ir_assignment *ir);

   void setup_for_load_or_store(ir_variable *var,
                                ir_dereference *deref,
                                ir_rvalue **offset,
                                unsigned *const_offset,
                                bool *row_major,
                                int *matrix_columns);
   ir_expression *ubo_load(const struct glsl_type *type,
			   ir_rvalue *offset);
   ir_call *ssbo_load(const struct glsl_type *type,
                      ir_rvalue *offset);

   void check_for_ssbo_store(ir_assignment *ir);
   void write_to_memory(ir_dereference *deref,
                        ir_variable *var,
                        ir_variable *write_var,
                        unsigned write_mask);
   ir_call *ssbo_store(ir_rvalue *deref, ir_rvalue *offset,
                       unsigned write_mask);

   void emit_access(bool is_write, ir_dereference *deref,
                    ir_variable *base_offset, unsigned int deref_offset,
                    bool row_major, int matrix_columns,
                    unsigned write_mask);

   void *mem_ctx;
   struct gl_shader *shader;
   struct gl_uniform_buffer_variable *ubo_var;
   ir_rvalue *uniform_block;
   bool progress;
   bool is_shader_storage;
};

/**
 * Determine the name of the interface block field
 *
 * This is the name of the specific member as it would appear in the
 * \c gl_uniform_buffer_variable::Name field in the shader's
 * \c UniformBlocks array.
 */
static const char *
interface_field_name(void *mem_ctx, char *base_name, ir_dereference *d,
                     ir_rvalue **nonconst_block_index)
{
   ir_rvalue *previous_index = NULL;
   *nonconst_block_index = NULL;

   while (d != NULL) {
      switch (d->ir_type) {
      case ir_type_dereference_variable: {
         ir_dereference_variable *v = (ir_dereference_variable *) d;
         if (previous_index
             && v->var->is_interface_instance()
             && v->var->type->is_array()) {

            ir_constant *const_index = previous_index->as_constant();
            if (!const_index) {
               *nonconst_block_index = previous_index;
               return ralloc_asprintf(mem_ctx, "%s[0]", base_name);
            } else {
               return ralloc_asprintf(mem_ctx,
                                      "%s[%d]",
                                      base_name,
                                      const_index->get_uint_component(0));
            }
         } else {
            return base_name;
         }

         break;
      }

      case ir_type_dereference_record: {
         ir_dereference_record *r = (ir_dereference_record *) d;

         d = r->record->as_dereference();
         break;
      }

      case ir_type_dereference_array: {
         ir_dereference_array *a = (ir_dereference_array *) d;

         d = a->array->as_dereference();
         previous_index = a->array_index;

         break;
      }

      default:
         assert(!"Should not get here.");
         break;
      }
   }

   assert(!"Should not get here.");
   return NULL;
}

void
lower_ubo_reference_visitor::setup_for_load_or_store(ir_variable *var,
                                                     ir_dereference *deref,
                                                     ir_rvalue **offset,
                                                     unsigned *const_offset,
                                                     bool *row_major,
                                                     int *matrix_columns)
{
   /* Determine the name of the interface block */
   ir_rvalue *nonconst_block_index;
   const char *const field_name =
      interface_field_name(mem_ctx, (char *) var->get_interface_type()->name,
                           deref, &nonconst_block_index);

   /* Locate the ubo block by interface name */
   this->uniform_block = NULL;
   for (unsigned i = 0; i < shader->NumUniformBlocks; i++) {
      if (strcmp(field_name, shader->UniformBlocks[i].Name) == 0) {

         ir_constant *index = new(mem_ctx) ir_constant(i);

         if (nonconst_block_index) {
            if (nonconst_block_index->type != glsl_type::uint_type)
               nonconst_block_index = i2u(nonconst_block_index);
            this->uniform_block = add(nonconst_block_index, index);
         } else {
            this->uniform_block = index;
         }

         this->is_shader_storage = shader->UniformBlocks[i].IsShaderStorage;

         struct gl_uniform_block *block = &shader->UniformBlocks[i];

         this->ubo_var = var->is_interface_instance()
            ? &block->Uniforms[0] : &block->Uniforms[var->data.location];

         break;
      }
   }

   assert(this->uniform_block);

   *offset = new(mem_ctx) ir_constant(0u);
   *const_offset = 0;
   *row_major = is_dereferenced_thing_row_major(deref);
   *matrix_columns = 1;

   /* Calculate the offset to the start of the region of the UBO
    * dereferenced by *rvalue.  This may be a variable offset if an
    * array dereference has a variable index.
    */
   while (deref) {
      switch (deref->ir_type) {
      case ir_type_dereference_variable: {
         *const_offset += ubo_var->Offset;
         deref = NULL;
         break;
      }

      case ir_type_dereference_array: {
         ir_dereference_array *deref_array = (ir_dereference_array *) deref;
         unsigned array_stride;
         if (deref_array->array->type->is_matrix() && *row_major) {
            /* When loading a vector out of a row major matrix, the
             * step between the columns (vectors) is the size of a
             * float, while the step between the rows (elements of a
             * vector) is handled below in emit_ubo_loads.
             */
            array_stride = 4;
            if (deref_array->array->type->is_double())
               array_stride *= 2;
            *matrix_columns = deref_array->array->type->matrix_columns;
         } else if (deref_array->type->is_interface()) {
            /* We're processing an array dereference of an interface instance
             * array. The thing being dereferenced *must* be a variable
             * dereference because interfaces cannot be embedded in other
             * types. In terms of calculating the offsets for the lowering
             * pass, we don't care about the array index. All elements of an
             * interface instance array will have the same offsets relative to
             * the base of the block that backs them.
             */
            assert(deref_array->array->as_dereference_variable());
            deref = deref_array->array->as_dereference();
            break;
         } else {
            /* Whether or not the field is row-major (because it might be a
             * bvec2 or something) does not affect the array itself. We need
             * to know whether an array element in its entirety is row-major.
             */
            const bool array_row_major =
               is_dereferenced_thing_row_major(deref_array);

            array_stride = deref_array->type->std140_size(array_row_major);
            array_stride = glsl_align(array_stride, 16);
         }

         ir_rvalue *array_index = deref_array->array_index;
         if (array_index->type->base_type == GLSL_TYPE_INT)
            array_index = i2u(array_index);

         ir_constant *const_index =
            array_index->constant_expression_value(NULL);
         if (const_index) {
            *const_offset += array_stride * const_index->value.u[0];
         } else {
            *offset = add(*offset,
                          mul(array_index,
                              new(mem_ctx) ir_constant(array_stride)));
         }
         deref = deref_array->array->as_dereference();
         break;
      }

      case ir_type_dereference_record: {
         ir_dereference_record *deref_record = (ir_dereference_record *) deref;
         const glsl_type *struct_type = deref_record->record->type;
         unsigned intra_struct_offset = 0;

         for (unsigned int i = 0; i < struct_type->length; i++) {
            const glsl_type *type = struct_type->fields.structure[i].type;

            ir_dereference_record *field_deref = new(mem_ctx)
               ir_dereference_record(deref_record->record,
                                     struct_type->fields.structure[i].name);
            const bool field_row_major =
               is_dereferenced_thing_row_major(field_deref);

            ralloc_free(field_deref);

            unsigned field_align = type->std140_base_alignment(field_row_major);

            intra_struct_offset = glsl_align(intra_struct_offset, field_align);

            if (strcmp(struct_type->fields.structure[i].name,
                       deref_record->field) == 0)
               break;

            intra_struct_offset += type->std140_size(field_row_major);

            /* If the field just examined was itself a structure, apply rule
             * #9:
             *
             *     "The structure may have padding at the end; the base offset
             *     of the member following the sub-structure is rounded up to
             *     the next multiple of the base alignment of the structure."
             */
            if (type->without_array()->is_record()) {
               intra_struct_offset = glsl_align(intra_struct_offset,
                                                field_align);

            }
         }

         *const_offset += intra_struct_offset;
         deref = deref_record->record->as_dereference();
         break;
      }

      default:
         assert(!"not reached");
         deref = NULL;
         break;
      }
   }
}

void
lower_ubo_reference_visitor::handle_rvalue(ir_rvalue **rvalue)
{
   if (!*rvalue)
      return;

   ir_dereference *deref = (*rvalue)->as_dereference();
   if (!deref)
      return;

   ir_variable *var = deref->variable_referenced();
   if (!var || !var->is_in_buffer_block())
      return;

   mem_ctx = ralloc_parent(shader->ir);

   ir_rvalue *offset = NULL;
   unsigned const_offset;
   bool row_major;
   int matrix_columns;

   /* Compute the offset to the start if the dereference as well as other
    * information we need to configure the write
    */
   setup_for_load_or_store(var, deref,
                           &offset, &const_offset,
                           &row_major, &matrix_columns);
   assert(offset);

   /* Now that we've calculated the offset to the start of the
    * dereference, walk over the type and emit loads into a temporary.
    */
   const glsl_type *type = (*rvalue)->type;
   ir_variable *load_var = new(mem_ctx) ir_variable(type,
						    "ubo_load_temp",
						    ir_var_temporary);
   base_ir->insert_before(load_var);

   ir_variable *load_offset = new(mem_ctx) ir_variable(glsl_type::uint_type,
						       "ubo_load_temp_offset",
						       ir_var_temporary);
   base_ir->insert_before(load_offset);
   base_ir->insert_before(assign(load_offset, offset));

   deref = new(mem_ctx) ir_dereference_variable(load_var);
   emit_access(false, deref, load_offset, const_offset,
               row_major, matrix_columns, 0);
   *rvalue = deref;

   progress = true;
}

ir_expression *
lower_ubo_reference_visitor::ubo_load(const glsl_type *type,
				      ir_rvalue *offset)
{
   ir_rvalue *block_ref = this->uniform_block->clone(mem_ctx, NULL);
   return new(mem_ctx)
      ir_expression(ir_binop_ubo_load,
                    type,
                    block_ref,
                    offset);

}

static bool
shader_storage_buffer_object(const _mesa_glsl_parse_state *state)
{
   return state->ARB_shader_storage_buffer_object_enable;
}

ir_call *
lower_ubo_reference_visitor::ssbo_store(ir_rvalue *deref,
                                        ir_rvalue *offset,
                                        unsigned write_mask)
{
   exec_list sig_params;

   ir_variable *block_ref = new(mem_ctx)
      ir_variable(glsl_type::uint_type, "block_ref" , ir_var_function_in);
   sig_params.push_tail(block_ref);

   ir_variable *offset_ref = new(mem_ctx)
      ir_variable(glsl_type::uint_type, "offset" , ir_var_function_in);
   sig_params.push_tail(offset_ref);

   ir_variable *val_ref = new(mem_ctx)
      ir_variable(deref->type, "value" , ir_var_function_in);
   sig_params.push_tail(val_ref);

   ir_variable *writemask_ref = new(mem_ctx)
      ir_variable(glsl_type::uint_type, "write_mask" , ir_var_function_in);
   sig_params.push_tail(writemask_ref);

   ir_function_signature *sig = new(mem_ctx)
      ir_function_signature(glsl_type::void_type, shader_storage_buffer_object);
   assert(sig);
   sig->replace_parameters(&sig_params);
   sig->is_intrinsic = true;

   ir_function *f = new(mem_ctx) ir_function("__intrinsic_store_ssbo");
   f->add_signature(sig);

   exec_list call_params;
   call_params.push_tail(this->uniform_block->clone(mem_ctx, NULL));
   call_params.push_tail(offset->clone(mem_ctx, NULL));
   call_params.push_tail(deref->clone(mem_ctx, NULL));
   call_params.push_tail(new(mem_ctx) ir_constant(write_mask));
   return new(mem_ctx) ir_call(sig, NULL, &call_params);
}

ir_call *
lower_ubo_reference_visitor::ssbo_load(const struct glsl_type *type,
                                       ir_rvalue *offset)
{
   exec_list sig_params;

   ir_variable *block_ref = new(mem_ctx)
      ir_variable(glsl_type::uint_type, "block_ref" , ir_var_function_in);
   sig_params.push_tail(block_ref);

   ir_variable *offset_ref = new(mem_ctx)
      ir_variable(glsl_type::uint_type, "offset_ref" , ir_var_function_in);
   sig_params.push_tail(offset_ref);

   ir_function_signature *sig =
      new(mem_ctx) ir_function_signature(type, shader_storage_buffer_object);
   assert(sig);
   sig->replace_parameters(&sig_params);
   sig->is_intrinsic = true;

   ir_function *f = new(mem_ctx) ir_function("__intrinsic_load_ssbo");
   f->add_signature(sig);

   ir_variable *result = new(mem_ctx)
      ir_variable(type, "ssbo_load_result", ir_var_temporary);
   base_ir->insert_before(result);
   ir_dereference_variable *deref_result = new(mem_ctx)
      ir_dereference_variable(result);

   exec_list call_params;
   call_params.push_tail(this->uniform_block->clone(mem_ctx, NULL));
   call_params.push_tail(offset->clone(mem_ctx, NULL));

   return new(mem_ctx) ir_call(sig, deref_result, &call_params);
}

static inline int
writemask_for_size(unsigned n)
{
   return ((1 << n) - 1);
}

/**
 * Takes a deref and recursively calls itself to break the deref down to the
 * point that the reads or writes generated are contiguous scalars or vectors.
 */
void
lower_ubo_reference_visitor::emit_access(bool is_write,
                                         ir_dereference *deref,
                                         ir_variable *base_offset,
                                         unsigned int deref_offset,
                                         bool row_major,
                                         int matrix_columns,
                                         unsigned write_mask)
{
   if (deref->type->is_record()) {
      unsigned int field_offset = 0;

      for (unsigned i = 0; i < deref->type->length; i++) {
         const struct glsl_struct_field *field =
            &deref->type->fields.structure[i];
         ir_dereference *field_deref =
            new(mem_ctx) ir_dereference_record(deref->clone(mem_ctx, NULL),
                                               field->name);

         field_offset =
            glsl_align(field_offset,
                       field->type->std140_base_alignment(row_major));

         emit_access(is_write, field_deref, base_offset,
                     deref_offset + field_offset,
                     row_major, 1,
                     writemask_for_size(field_deref->type->vector_elements));

         field_offset += field->type->std140_size(row_major);
      }
      return;
   }

   if (deref->type->is_array()) {
      unsigned array_stride =
         glsl_align(deref->type->fields.array->std140_size(row_major), 16);

      for (unsigned i = 0; i < deref->type->length; i++) {
         ir_constant *element = new(mem_ctx) ir_constant(i);
         ir_dereference *element_deref =
            new(mem_ctx) ir_dereference_array(deref->clone(mem_ctx, NULL),
                                              element);
         emit_access(is_write, element_deref, base_offset,
                     deref_offset + i * array_stride,
                     row_major, 1,
                     writemask_for_size(element_deref->type->vector_elements));
      }
      return;
   }

   if (deref->type->is_matrix()) {
      for (unsigned i = 0; i < deref->type->matrix_columns; i++) {
         ir_constant *col = new(mem_ctx) ir_constant(i);
         ir_dereference *col_deref =
            new(mem_ctx) ir_dereference_array(deref->clone(mem_ctx, NULL), col);

         if (row_major) {
            /* For a row-major matrix, the next column starts at the next
             * element.
             */
            int size_mul = deref->type->is_double() ? 8 : 4;
            emit_access(is_write, col_deref, base_offset,
                        deref_offset + i * size_mul,
                        row_major, deref->type->matrix_columns,
                        writemask_for_size(col_deref->type->vector_elements));
         } else {
            /* std140 always rounds the stride of arrays (and matrices) to a
             * vec4, so matrices are always 16 between columns/rows. With
             * doubles, they will be 32 apart when there are more than 2 rows.
             */
            int size_mul = (deref->type->is_double() &&
                            deref->type->vector_elements > 2) ? 32 : 16;
            emit_access(is_write, col_deref, base_offset,
                        deref_offset + i * size_mul,
                        row_major, deref->type->matrix_columns,
                        writemask_for_size(col_deref->type->vector_elements));
         }
      }
      return;
   }

   assert(deref->type->is_scalar() || deref->type->is_vector());

   if (!row_major) {
      ir_rvalue *offset =
         add(base_offset, new(mem_ctx) ir_constant(deref_offset));
      if (is_write)
         base_ir->insert_after(ssbo_store(deref, offset, write_mask));
      else {
         if (!this->is_shader_storage) {
             base_ir->insert_before(assign(deref->clone(mem_ctx, NULL),
                                           ubo_load(deref->type, offset)));
         } else {
            ir_call *load_ssbo = ssbo_load(deref->type, offset);
            base_ir->insert_before(load_ssbo);
            ir_rvalue *value = load_ssbo->return_deref->as_rvalue()->clone(mem_ctx, NULL);
            base_ir->insert_before(assign(deref->clone(mem_ctx, NULL), value));
         }
      }
   } else {
      unsigned N = deref->type->is_double() ? 8 : 4;

      /* We're dereffing a column out of a row-major matrix, so we
       * gather the vector from each stored row.
      */
      assert(deref->type->base_type == GLSL_TYPE_FLOAT ||
             deref->type->base_type == GLSL_TYPE_DOUBLE);
      /* Matrices, row_major or not, are stored as if they were
       * arrays of vectors of the appropriate size in std140.
       * Arrays have their strides rounded up to a vec4, so the
       * matrix stride is always 16. However a double matrix may either be 16
       * or 32 depending on the number of columns.
       */
      assert(matrix_columns <= 4);
      unsigned matrix_stride = glsl_align(matrix_columns * N, 16);

      const glsl_type *deref_type = deref->type->base_type == GLSL_TYPE_FLOAT ?
         glsl_type::float_type : glsl_type::double_type;

      for (unsigned i = 0; i < deref->type->vector_elements; i++) {
         ir_rvalue *chan_offset =
            add(base_offset,
                new(mem_ctx) ir_constant(deref_offset + i * matrix_stride));
         if (is_write) {
            base_ir->insert_after(ssbo_store(swizzle(deref, i, 1), chan_offset, 1));
         } else {
            if (!this->is_shader_storage) {
               base_ir->insert_before(assign(deref->clone(mem_ctx, NULL),
                                             ubo_load(deref_type, chan_offset),
                                             (1U << i)));
            } else {
               ir_call *load_ssbo = ssbo_load(deref_type, chan_offset);
               base_ir->insert_before(load_ssbo);
               ir_rvalue *value = load_ssbo->return_deref->as_rvalue()->clone(mem_ctx, NULL);
               base_ir->insert_before(assign(deref->clone(mem_ctx, NULL),
                                             value,
                                             (1U << i)));
            }
         }
      }
   }
}

void
lower_ubo_reference_visitor::write_to_memory(ir_dereference *deref,
                                             ir_variable *var,
                                             ir_variable *write_var,
                                             unsigned write_mask)
{
   ir_rvalue *offset = NULL;
   unsigned const_offset;
   bool row_major;
   int matrix_columns;

   /* Compute the offset to the start if the dereference as well as other
    * information we need to configure the write
    */
   setup_for_load_or_store(var, deref,
                           &offset, &const_offset,
                           &row_major, &matrix_columns);
   assert(offset);

   /* Now emit writes from the temporary to memory */
   ir_variable *write_offset =
      new(mem_ctx) ir_variable(glsl_type::uint_type,
                               "ssbo_store_temp_offset",
                               ir_var_temporary);

   base_ir->insert_before(write_offset);
   base_ir->insert_before(assign(write_offset, offset));

   deref = new(mem_ctx) ir_dereference_variable(write_var);
   emit_access(true, deref, write_offset, const_offset,
               row_major, matrix_columns, write_mask);
}

void
lower_ubo_reference_visitor::check_for_ssbo_store(ir_assignment *ir)
{
   if (!ir || !ir->lhs)
      return;

   ir_rvalue *rvalue = ir->lhs->as_rvalue();
   if (!rvalue)
      return;

   ir_dereference *deref = ir->lhs->as_dereference();
   if (!deref)
      return;

   ir_variable *var = ir->lhs->variable_referenced();
   if (!var || !var->is_in_buffer_block())
      return;

   /* We have a write to a buffer variable, so declare a temporary and rewrite
    * the assignment so that the temporary is the LHS.
    */
   mem_ctx = ralloc_parent(shader->ir);

   const glsl_type *type = rvalue->type;
   ir_variable *write_var = new(mem_ctx) ir_variable(type,
                                                     "ssbo_store_temp",
                                                     ir_var_temporary);
   base_ir->insert_before(write_var);
   ir->lhs = new(mem_ctx) ir_dereference_variable(write_var);

   /* Now we have to write the value assigned to the temporary back to memory */
   write_to_memory(deref, var, write_var, ir->write_mask);
   progress = true;
}


ir_visitor_status
lower_ubo_reference_visitor::visit_enter(ir_assignment *ir)
{
   check_for_ssbo_store(ir);
   return rvalue_visit(ir);
}

} /* unnamed namespace */

void
lower_ubo_reference(struct gl_shader *shader, exec_list *instructions)
{
   lower_ubo_reference_visitor v(shader);

   /* Loop over the instructions lowering references, because we take
    * a deref of a UBO array using a UBO dereference as the index will
    * produce a collection of instructions all of which have cloned
    * UBO dereferences for that array index.
    */
   do {
      v.progress = false;
      visit_list_elements(&v, instructions);
   } while (v.progress);
}
