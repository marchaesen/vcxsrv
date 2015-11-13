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
is_dereferenced_thing_row_major(const ir_rvalue *deref)
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
                                ir_rvalue *deref,
                                ir_rvalue **offset,
                                unsigned *const_offset,
                                bool *row_major,
                                int *matrix_columns,
                                unsigned packing);
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
                    unsigned packing, unsigned write_mask);

   ir_visitor_status visit_enter(class ir_expression *);
   ir_expression *calculate_ssbo_unsized_array_length(ir_expression *expr);
   void check_ssbo_unsized_array_length_expression(class ir_expression *);
   void check_ssbo_unsized_array_length_assignment(ir_assignment *ir);

   ir_expression *process_ssbo_unsized_array_length(ir_rvalue **,
                                                    ir_dereference *,
                                                    ir_variable *);
   ir_expression *emit_ssbo_get_buffer_size();

   unsigned calculate_unsized_array_stride(ir_dereference *deref,
                                           unsigned packing);

   ir_call *lower_ssbo_atomic_intrinsic(ir_call *ir);
   ir_call *check_for_ssbo_atomic_intrinsic(ir_call *ir);
   ir_visitor_status visit_enter(ir_call *ir);

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
interface_field_name(void *mem_ctx, char *base_name, ir_rvalue *d,
                     ir_rvalue **nonconst_block_index)
{
   *nonconst_block_index = NULL;
   char *name_copy = NULL;
   size_t base_length = 0;

   /* Loop back through the IR until we find the uniform block */
   ir_rvalue *ir = d;
   while (ir != NULL) {
      switch (ir->ir_type) {
      case ir_type_dereference_variable: {
         /* Exit loop */
         ir = NULL;
         break;
      }

      case ir_type_dereference_record: {
         ir_dereference_record *r = (ir_dereference_record *) ir;
         ir = r->record->as_dereference();

         /* If we got here it means any previous array subscripts belong to
          * block members and not the block itself so skip over them in the
          * next pass.
          */
         d = ir;
         break;
      }

      case ir_type_dereference_array: {
         ir_dereference_array *a = (ir_dereference_array *) ir;
         ir = a->array->as_dereference();
         break;
      }

      case ir_type_swizzle: {
         ir_swizzle *s = (ir_swizzle *) ir;
         ir = s->val->as_dereference();
         /* Skip swizzle in the next pass */
         d = ir;
         break;
      }

      default:
         assert(!"Should not get here.");
         break;
      }
   }

   while (d != NULL) {
      switch (d->ir_type) {
      case ir_type_dereference_variable: {
         ir_dereference_variable *v = (ir_dereference_variable *) d;
         if (name_copy != NULL &&
             v->var->is_interface_instance() &&
             v->var->type->is_array()) {
            return name_copy;
         } else {
            *nonconst_block_index = NULL;
            return base_name;
         }

         break;
      }

      case ir_type_dereference_array: {
         ir_dereference_array *a = (ir_dereference_array *) d;
         size_t new_length;

         if (name_copy == NULL) {
            name_copy = ralloc_strdup(mem_ctx, base_name);
            base_length = strlen(name_copy);
         }

         /* For arrays of arrays we start at the innermost array and work our
          * way out so we need to insert the subscript at the base of the
          * name string rather than just attaching it to the end.
          */
         new_length = base_length;
         ir_constant *const_index = a->array_index->as_constant();
         char *end = ralloc_strdup(NULL, &name_copy[new_length]);
         if (!const_index) {
            ir_rvalue *array_index = a->array_index;
            if (array_index->type != glsl_type::uint_type)
               array_index = i2u(array_index);

            if (a->array->type->is_array() &&
                a->array->type->fields.array->is_array()) {
               ir_constant *base_size = new(mem_ctx)
                  ir_constant(a->array->type->fields.array->arrays_of_arrays_size());
               array_index = mul(array_index, base_size);
            }

            if (*nonconst_block_index) {
               *nonconst_block_index = add(*nonconst_block_index, array_index);
            } else {
               *nonconst_block_index = array_index;
            }

            ralloc_asprintf_rewrite_tail(&name_copy, &new_length, "[0]%s",
                                         end);
         } else {
            ralloc_asprintf_rewrite_tail(&name_copy, &new_length, "[%d]%s",
                                         const_index->get_uint_component(0),
                                         end);
         }
         ralloc_free(end);

         d = a->array->as_dereference();

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
                                                     ir_rvalue *deref,
                                                     ir_rvalue **offset,
                                                     unsigned *const_offset,
                                                     bool *row_major,
                                                     int *matrix_columns,
                                                     unsigned packing)
{
   /* Determine the name of the interface block */
   ir_rvalue *nonconst_block_index;
   const char *const field_name =
      interface_field_name(mem_ctx, (char *) var->get_interface_type()->name,
                           deref, &nonconst_block_index);

   /* Locate the block by interface name */
   this->is_shader_storage = var->is_in_shader_storage_block();
   unsigned num_blocks;
   struct gl_uniform_block **blocks;
   if (this->is_shader_storage) {
      num_blocks = shader->NumShaderStorageBlocks;
      blocks = shader->ShaderStorageBlocks;
   } else {
      num_blocks = shader->NumUniformBlocks;
      blocks = shader->UniformBlocks;
   }
   this->uniform_block = NULL;
   for (unsigned i = 0; i < num_blocks; i++) {
      if (strcmp(field_name, blocks[i]->Name) == 0) {

         ir_constant *index = new(mem_ctx) ir_constant(i);

         if (nonconst_block_index) {
            this->uniform_block = add(nonconst_block_index, index);
         } else {
            this->uniform_block = index;
         }

         this->ubo_var = var->is_interface_instance()
            ? &blocks[i]->Uniforms[0] : &blocks[i]->Uniforms[var->data.location];

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
         if (deref_array->array->type->is_vector()) {
            /* We get this when storing or loading a component out of a vector
             * with a non-constant index. This happens for v[i] = f where v is
             * a vector (or m[i][j] = f where m is a matrix). If we don't
             * lower that here, it gets turned into v = vector_insert(v, i,
             * f), which loads the entire vector, modifies one component and
             * then write the entire thing back.  That breaks if another
             * thread or SIMD channel is modifying the same vector.
             */
            array_stride = 4;
            if (deref_array->array->type->is_double())
               array_stride *= 2;
         } else if (deref_array->array->type->is_matrix() && *row_major) {
            /* When loading a vector out of a row major matrix, the
             * step between the columns (vectors) is the size of a
             * float, while the step between the rows (elements of a
             * vector) is handled below in emit_ubo_loads.
             */
            array_stride = 4;
            if (deref_array->array->type->is_double())
               array_stride *= 2;
            *matrix_columns = deref_array->array->type->matrix_columns;
         } else if (deref_array->type->without_array()->is_interface()) {
            /* We're processing an array dereference of an interface instance
             * array. The thing being dereferenced *must* be a variable
             * dereference because interfaces cannot be embedded in other
             * types. In terms of calculating the offsets for the lowering
             * pass, we don't care about the array index. All elements of an
             * interface instance array will have the same offsets relative to
             * the base of the block that backs them.
             */
            deref = deref_array->array->as_dereference();
            break;
         } else {
            /* Whether or not the field is row-major (because it might be a
             * bvec2 or something) does not affect the array itself. We need
             * to know whether an array element in its entirety is row-major.
             */
            const bool array_row_major =
               is_dereferenced_thing_row_major(deref_array);

            /* The array type will give the correct interface packing
             * information
             */
            if (packing == GLSL_INTERFACE_PACKING_STD430) {
               array_stride = deref_array->type->std430_array_stride(array_row_major);
            } else {
               array_stride = deref_array->type->std140_size(array_row_major);
               array_stride = glsl_align(array_stride, 16);
            }
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

            unsigned field_align = 0;

            if (packing == GLSL_INTERFACE_PACKING_STD430)
               field_align = type->std430_base_alignment(field_row_major);
            else
               field_align = type->std140_base_alignment(field_row_major);

            intra_struct_offset = glsl_align(intra_struct_offset, field_align);

            if (strcmp(struct_type->fields.structure[i].name,
                       deref_record->field) == 0)
               break;

            if (packing == GLSL_INTERFACE_PACKING_STD430)
               intra_struct_offset += type->std430_size(field_row_major);
            else
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

      case ir_type_swizzle: {
         ir_swizzle *deref_swizzle = (ir_swizzle *) deref;

         assert(deref_swizzle->mask.num_components == 1);

         *const_offset += deref_swizzle->mask.x * sizeof(int);
         deref = deref_swizzle->val->as_dereference();
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
   unsigned packing = var->get_interface_type()->interface_packing;

   /* Compute the offset to the start if the dereference as well as other
    * information we need to configure the write
    */
   setup_for_load_or_store(var, deref,
                           &offset, &const_offset,
                           &row_major, &matrix_columns,
                           packing);
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
               row_major, matrix_columns, packing, 0);
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
                                         unsigned packing,
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
                     row_major, 1, packing,
                     writemask_for_size(field_deref->type->vector_elements));

         field_offset += field->type->std140_size(row_major);
      }
      return;
   }

   if (deref->type->is_array()) {
      unsigned array_stride = packing == GLSL_INTERFACE_PACKING_STD430 ?
         deref->type->fields.array->std430_array_stride(row_major) :
         glsl_align(deref->type->fields.array->std140_size(row_major), 16);

      for (unsigned i = 0; i < deref->type->length; i++) {
         ir_constant *element = new(mem_ctx) ir_constant(i);
         ir_dereference *element_deref =
            new(mem_ctx) ir_dereference_array(deref->clone(mem_ctx, NULL),
                                              element);
         emit_access(is_write, element_deref, base_offset,
                     deref_offset + i * array_stride,
                     row_major, 1, packing,
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
                        row_major, deref->type->matrix_columns, packing,
                        writemask_for_size(col_deref->type->vector_elements));
         } else {
            int size_mul;

            /* std430 doesn't round up vec2 size to a vec4 size */
            if (packing == GLSL_INTERFACE_PACKING_STD430 &&
                deref->type->vector_elements == 2 &&
                !deref->type->is_double()) {
               size_mul = 8;
            } else {
               /* std140 always rounds the stride of arrays (and matrices) to a
                * vec4, so matrices are always 16 between columns/rows. With
                * doubles, they will be 32 apart when there are more than 2 rows.
                *
                * For both std140 and std430, if the member is a
                * three-'component vector with components consuming N basic
                * machine units, the base alignment is 4N. For vec4, base
                * alignment is 4N.
                */
               size_mul = (deref->type->is_double() &&
                           deref->type->vector_elements > 2) ? 32 : 16;
            }

            emit_access(is_write, col_deref, base_offset,
                        deref_offset + i * size_mul,
                        row_major, deref->type->matrix_columns, packing,
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
      unsigned matrix_stride = 0;
      /* Matrix stride for std430 mat2xY matrices are not rounded up to
       * vec4 size. From OpenGL 4.3 spec, section 7.6.2.2 "Standard Uniform
       * Block Layout":
       *
       * "2. If the member is a two- or four-component vector with components
       * consuming N basic machine units, the base alignment is 2N or 4N,
       * respectively." [...]
       * "4. If the member is an array of scalars or vectors, the base alignment
       * and array stride are set to match the base alignment of a single array
       * element, according to rules (1), (2), and (3), and rounded up to the
       * base alignment of a vec4." [...]
       * "7. If the member is a row-major matrix with C columns and R rows, the
       * matrix is stored identically to an array of R row vectors with C
       * components each, according to rule (4)." [...]
       * "When using the std430 storage layout, shader storage blocks will be
       * laid out in buffer storage identically to uniform and shader storage
       * blocks using the std140 layout, except that the base alignment and
       * stride of arrays of scalars and vectors in rule 4 and of structures in
       * rule 9 are not rounded up a multiple of the base alignment of a vec4."
       */
      if (packing == GLSL_INTERFACE_PACKING_STD430 && matrix_columns == 2)
         matrix_stride = 2 * N;
      else
         matrix_stride = glsl_align(matrix_columns * N, 16);

      const glsl_type *deref_type = deref->type->base_type == GLSL_TYPE_FLOAT ?
         glsl_type::float_type : glsl_type::double_type;

      for (unsigned i = 0; i < deref->type->vector_elements; i++) {
         ir_rvalue *chan_offset =
            add(base_offset,
                new(mem_ctx) ir_constant(deref_offset + i * matrix_stride));
         if (is_write) {
            /* If the component is not in the writemask, then don't
             * store any value.
             */
            if (!((1 << i) & write_mask))
               continue;

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
   unsigned packing = var->get_interface_type()->interface_packing;

   /* Compute the offset to the start if the dereference as well as other
    * information we need to configure the write
    */
   setup_for_load_or_store(var, deref,
                           &offset, &const_offset,
                           &row_major, &matrix_columns,
                           packing);
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
               row_major, matrix_columns, packing, write_mask);
}

ir_visitor_status
lower_ubo_reference_visitor::visit_enter(ir_expression *ir)
{
   check_ssbo_unsized_array_length_expression(ir);
   return rvalue_visit(ir);
}

ir_expression *
lower_ubo_reference_visitor::calculate_ssbo_unsized_array_length(ir_expression *expr)
{
   if (expr->operation !=
       ir_expression_operation(ir_unop_ssbo_unsized_array_length))
      return NULL;

   ir_rvalue *rvalue = expr->operands[0]->as_rvalue();
   if (!rvalue ||
       !rvalue->type->is_array() || !rvalue->type->is_unsized_array())
      return NULL;

   ir_dereference *deref = expr->operands[0]->as_dereference();
   if (!deref)
      return NULL;

   ir_variable *var = expr->operands[0]->variable_referenced();
   if (!var || !var->is_in_shader_storage_block())
      return NULL;
   return process_ssbo_unsized_array_length(&rvalue, deref, var);
}

void
lower_ubo_reference_visitor::check_ssbo_unsized_array_length_expression(ir_expression *ir)
{
   if (ir->operation ==
       ir_expression_operation(ir_unop_ssbo_unsized_array_length)) {
         /* Don't replace this unop if it is found alone. It is going to be
          * removed by the optimization passes or replaced if it is part of
          * an ir_assignment or another ir_expression.
          */
         return;
   }

   for (unsigned i = 0; i < ir->get_num_operands(); i++) {
      if (ir->operands[i]->ir_type != ir_type_expression)
         continue;
      ir_expression *expr = (ir_expression *) ir->operands[i];
      ir_expression *temp = calculate_ssbo_unsized_array_length(expr);
      if (!temp)
         continue;

      delete expr;
      ir->operands[i] = temp;
   }
}

void
lower_ubo_reference_visitor::check_ssbo_unsized_array_length_assignment(ir_assignment *ir)
{
   if (!ir->rhs || ir->rhs->ir_type != ir_type_expression)
      return;

   ir_expression *expr = (ir_expression *) ir->rhs;
   ir_expression *temp = calculate_ssbo_unsized_array_length(expr);
   if (!temp)
      return;

   delete expr;
   ir->rhs = temp;
   return;
}

ir_expression *
lower_ubo_reference_visitor::emit_ssbo_get_buffer_size()
{
   ir_rvalue *block_ref = this->uniform_block->clone(mem_ctx, NULL);
   return new(mem_ctx) ir_expression(ir_unop_get_buffer_size,
                                     glsl_type::int_type,
                                     block_ref);
}

unsigned
lower_ubo_reference_visitor::calculate_unsized_array_stride(ir_dereference *deref,
                                                            unsigned packing)
{
   unsigned array_stride = 0;

   switch (deref->ir_type) {
   case ir_type_dereference_variable:
   {
      ir_dereference_variable *deref_var = (ir_dereference_variable *)deref;
      const struct glsl_type *unsized_array_type = NULL;
      /* An unsized array can be sized by other lowering passes, so pick
       * the first field of the array which has the data type of the unsized
       * array.
       */
      unsized_array_type = deref_var->var->type->fields.array;

      /* Whether or not the field is row-major (because it might be a
       * bvec2 or something) does not affect the array itself. We need
       * to know whether an array element in its entirety is row-major.
       */
      const bool array_row_major =
         is_dereferenced_thing_row_major(deref_var);

      if (packing == GLSL_INTERFACE_PACKING_STD430) {
         array_stride = unsized_array_type->std430_array_stride(array_row_major);
      } else {
         array_stride = unsized_array_type->std140_size(array_row_major);
         array_stride = glsl_align(array_stride, 16);
      }
      break;
   }
   case ir_type_dereference_record:
   {
      ir_dereference_record *deref_record = (ir_dereference_record *) deref;
      ir_dereference *interface_deref =
         deref_record->record->as_dereference();
      assert(interface_deref != NULL);
      const struct glsl_type *interface_type = interface_deref->type;
      unsigned record_length = interface_type->length;
      /* Unsized array is always the last element of the interface */
      const struct glsl_type *unsized_array_type =
         interface_type->fields.structure[record_length - 1].type->fields.array;

      const bool array_row_major =
         is_dereferenced_thing_row_major(deref_record);

      if (packing == GLSL_INTERFACE_PACKING_STD430) {
         array_stride = unsized_array_type->std430_array_stride(array_row_major);
      } else {
         array_stride = unsized_array_type->std140_size(array_row_major);
         array_stride = glsl_align(array_stride, 16);
      }
      break;
   }
   default:
      unreachable("Unsupported dereference type");
   }
   return array_stride;
}

ir_expression *
lower_ubo_reference_visitor::process_ssbo_unsized_array_length(ir_rvalue **rvalue,
                                                               ir_dereference *deref,
                                                               ir_variable *var)
{
   mem_ctx = ralloc_parent(*rvalue);

   ir_rvalue *base_offset = NULL;
   unsigned const_offset;
   bool row_major;
   int matrix_columns;
   unsigned packing = var->get_interface_type()->interface_packing;
   int unsized_array_stride = calculate_unsized_array_stride(deref, packing);

   /* Compute the offset to the start if the dereference as well as other
    * information we need to calculate the length.
    */
   setup_for_load_or_store(var, deref,
                           &base_offset, &const_offset,
                           &row_major, &matrix_columns,
                           packing);
   /* array.length() =
    *  max((buffer_object_size - offset_of_array) / stride_of_array, 0)
    */
   ir_expression *buffer_size = emit_ssbo_get_buffer_size();

   ir_expression *offset_of_array = new(mem_ctx)
      ir_expression(ir_binop_add, base_offset,
                    new(mem_ctx) ir_constant(const_offset));
   ir_expression *offset_of_array_int = new(mem_ctx)
      ir_expression(ir_unop_u2i, offset_of_array);

   ir_expression *sub = new(mem_ctx)
      ir_expression(ir_binop_sub, buffer_size, offset_of_array_int);
   ir_expression *div =  new(mem_ctx)
      ir_expression(ir_binop_div, sub,
                    new(mem_ctx) ir_constant(unsized_array_stride));
   ir_expression *max = new(mem_ctx)
      ir_expression(ir_binop_max, div, new(mem_ctx) ir_constant(0));

   return max;
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
   check_ssbo_unsized_array_length_assignment(ir);
   check_for_ssbo_store(ir);
   return rvalue_visit(ir);
}

/* Lowers the intrinsic call to a new internal intrinsic that swaps the
 * access to the buffer variable in the first parameter by an offset
 * and block index. This involves creating the new internal intrinsic
 * (i.e. the new function signature).
 */
ir_call *
lower_ubo_reference_visitor::lower_ssbo_atomic_intrinsic(ir_call *ir)
{
   /* SSBO atomics usually have 2 parameters, the buffer variable and an
    * integer argument. The exception is CompSwap, that has an additional
    * integer parameter.
    */
   int param_count = ir->actual_parameters.length();
   assert(param_count == 2 || param_count == 3);

   /* First argument must be a scalar integer buffer variable */
   exec_node *param = ir->actual_parameters.get_head();
   ir_instruction *inst = (ir_instruction *) param;
   assert(inst->ir_type == ir_type_dereference_variable ||
          inst->ir_type == ir_type_dereference_array ||
          inst->ir_type == ir_type_dereference_record ||
          inst->ir_type == ir_type_swizzle);

   ir_rvalue *deref = (ir_rvalue *) inst;
   assert(deref->type->is_scalar() && deref->type->is_integer());

   ir_variable *var = deref->variable_referenced();
   assert(var);

   /* Compute the offset to the start if the dereference and the
    * block index
    */
   mem_ctx = ralloc_parent(shader->ir);

   ir_rvalue *offset = NULL;
   unsigned const_offset;
   bool row_major;
   int matrix_columns;
   unsigned packing = var->get_interface_type()->interface_packing;

   setup_for_load_or_store(var, deref,
                           &offset, &const_offset,
                           &row_major, &matrix_columns,
                           packing);
   assert(offset);
   assert(!row_major);
   assert(matrix_columns == 1);

   ir_rvalue *deref_offset =
      add(offset, new(mem_ctx) ir_constant(const_offset));
   ir_rvalue *block_index = this->uniform_block->clone(mem_ctx, NULL);

   /* Create the new internal function signature that will take a block
    * index and offset instead of a buffer variable
    */
   exec_list sig_params;
   ir_variable *sig_param = new(mem_ctx)
      ir_variable(glsl_type::uint_type, "block_ref" , ir_var_function_in);
   sig_params.push_tail(sig_param);

   sig_param = new(mem_ctx)
      ir_variable(glsl_type::uint_type, "offset" , ir_var_function_in);
   sig_params.push_tail(sig_param);

   const glsl_type *type = deref->type->base_type == GLSL_TYPE_INT ?
      glsl_type::int_type : glsl_type::uint_type;
   sig_param = new(mem_ctx)
         ir_variable(type, "data1", ir_var_function_in);
   sig_params.push_tail(sig_param);

   if (param_count == 3) {
      sig_param = new(mem_ctx)
            ir_variable(type, "data2", ir_var_function_in);
      sig_params.push_tail(sig_param);
   }

   ir_function_signature *sig =
      new(mem_ctx) ir_function_signature(deref->type,
                                         shader_storage_buffer_object);
   assert(sig);
   sig->replace_parameters(&sig_params);
   sig->is_intrinsic = true;

   char func_name[64];
   sprintf(func_name, "%s_internal", ir->callee_name());
   ir_function *f = new(mem_ctx) ir_function(func_name);
   f->add_signature(sig);

   /* Now, create the call to the internal intrinsic */
   exec_list call_params;
   call_params.push_tail(block_index);
   call_params.push_tail(deref_offset);
   param = ir->actual_parameters.get_head()->get_next();
   ir_rvalue *param_as_rvalue = ((ir_instruction *) param)->as_rvalue();
   call_params.push_tail(param_as_rvalue->clone(mem_ctx, NULL));
   if (param_count == 3) {
      param = param->get_next();
      param_as_rvalue = ((ir_instruction *) param)->as_rvalue();
      call_params.push_tail(param_as_rvalue->clone(mem_ctx, NULL));
   }
   ir_dereference_variable *return_deref =
      ir->return_deref->clone(mem_ctx, NULL);
   return new(mem_ctx) ir_call(sig, return_deref, &call_params);
}

ir_call *
lower_ubo_reference_visitor::check_for_ssbo_atomic_intrinsic(ir_call *ir)
{
   const char *callee = ir->callee_name();
   if (!strcmp("__intrinsic_ssbo_atomic_add", callee) ||
       !strcmp("__intrinsic_ssbo_atomic_min", callee) ||
       !strcmp("__intrinsic_ssbo_atomic_max", callee) ||
       !strcmp("__intrinsic_ssbo_atomic_and", callee) ||
       !strcmp("__intrinsic_ssbo_atomic_or", callee) ||
       !strcmp("__intrinsic_ssbo_atomic_xor", callee) ||
       !strcmp("__intrinsic_ssbo_atomic_exchange", callee) ||
       !strcmp("__intrinsic_ssbo_atomic_comp_swap", callee)) {
      return lower_ssbo_atomic_intrinsic(ir);
   }

   return ir;
}


ir_visitor_status
lower_ubo_reference_visitor::visit_enter(ir_call *ir)
{
   ir_call *new_ir = check_for_ssbo_atomic_intrinsic(ir);
   if (new_ir != ir) {
      progress = true;
      base_ir->replace_with(new_ir);
      return visit_continue_with_parent;
   }

   return rvalue_visit(ir);
}


} /* unnamed namespace */

void
lower_ubo_reference(struct gl_shader *shader)
{
   lower_ubo_reference_visitor v(shader);

   /* Loop over the instructions lowering references, because we take
    * a deref of a UBO array using a UBO dereference as the index will
    * produce a collection of instructions all of which have cloned
    * UBO dereferences for that array index.
    */
   do {
      v.progress = false;
      visit_list_elements(&v, shader->ir);
   } while (v.progress);
}
