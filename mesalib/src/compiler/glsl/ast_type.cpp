/*
 * Copyright © 2010 Intel Corporation
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

#include "ast.h"

void
ast_type_specifier::print(void) const
{
   if (structure) {
      structure->print();
   } else {
      printf("%s ", type_name);
   }

   if (array_specifier) {
      array_specifier->print();
   }
}

bool
ast_fully_specified_type::has_qualifiers(_mesa_glsl_parse_state *state) const
{
   /* 'subroutine' isnt a real qualifier. */
   ast_type_qualifier subroutine_only;
   subroutine_only.flags.i = 0;
   subroutine_only.flags.q.subroutine = 1;
   subroutine_only.flags.q.subroutine_def = 1;
   if (state->has_explicit_uniform_location()) {
      subroutine_only.flags.q.explicit_index = 1;
   }
   return (this->qualifier.flags.i & ~subroutine_only.flags.i) != 0;
}

bool ast_type_qualifier::has_interpolation() const
{
   return this->flags.q.smooth
          || this->flags.q.flat
          || this->flags.q.noperspective;
}

bool
ast_type_qualifier::has_layout() const
{
   return this->flags.q.origin_upper_left
          || this->flags.q.pixel_center_integer
          || this->flags.q.depth_any
          || this->flags.q.depth_greater
          || this->flags.q.depth_less
          || this->flags.q.depth_unchanged
          || this->flags.q.std140
          || this->flags.q.std430
          || this->flags.q.shared
          || this->flags.q.column_major
          || this->flags.q.row_major
          || this->flags.q.packed
          || this->flags.q.explicit_location
          || this->flags.q.explicit_image_format
          || this->flags.q.explicit_index
          || this->flags.q.explicit_binding
          || this->flags.q.explicit_offset
          || this->flags.q.explicit_stream;
}

bool
ast_type_qualifier::has_storage() const
{
   return this->flags.q.constant
          || this->flags.q.attribute
          || this->flags.q.varying
          || this->flags.q.in
          || this->flags.q.out
          || this->flags.q.uniform
          || this->flags.q.buffer
          || this->flags.q.shared_storage;
}

bool
ast_type_qualifier::has_auxiliary_storage() const
{
   return this->flags.q.centroid
          || this->flags.q.sample
          || this->flags.q.patch;
}

/**
 * This function merges both duplicate identifies within a single layout and
 * multiple layout qualifiers on a single variable declaration. The
 * is_single_layout_merge param is used differentiate between the two.
 */
bool
ast_type_qualifier::merge_qualifier(YYLTYPE *loc,
				    _mesa_glsl_parse_state *state,
                                    const ast_type_qualifier &q,
                                    bool is_single_layout_merge)
{
   ast_type_qualifier ubo_mat_mask;
   ubo_mat_mask.flags.i = 0;
   ubo_mat_mask.flags.q.row_major = 1;
   ubo_mat_mask.flags.q.column_major = 1;

   ast_type_qualifier ubo_layout_mask;
   ubo_layout_mask.flags.i = 0;
   ubo_layout_mask.flags.q.std140 = 1;
   ubo_layout_mask.flags.q.packed = 1;
   ubo_layout_mask.flags.q.shared = 1;
   ubo_layout_mask.flags.q.std430 = 1;

   ast_type_qualifier ubo_binding_mask;
   ubo_binding_mask.flags.i = 0;
   ubo_binding_mask.flags.q.explicit_binding = 1;
   ubo_binding_mask.flags.q.explicit_offset = 1;

   ast_type_qualifier stream_layout_mask;
   stream_layout_mask.flags.i = 0;
   stream_layout_mask.flags.q.stream = 1;

   /* Uniform block layout qualifiers get to overwrite each
    * other (rightmost having priority), while all other
    * qualifiers currently don't allow duplicates.
    */
   ast_type_qualifier allowed_duplicates_mask;
   allowed_duplicates_mask.flags.i =
      ubo_mat_mask.flags.i |
      ubo_layout_mask.flags.i |
      ubo_binding_mask.flags.i;

   /* Geometry shaders can have several layout qualifiers
    * assigning different stream values.
    */
   if (state->stage == MESA_SHADER_GEOMETRY)
      allowed_duplicates_mask.flags.i |=
         stream_layout_mask.flags.i;

   if (is_single_layout_merge && !state->has_enhanced_layouts() &&
       (this->flags.i & q.flags.i & ~allowed_duplicates_mask.flags.i) != 0) {
      _mesa_glsl_error(loc, state,
		       "duplicate layout qualifiers used");
      return false;
   }

   if (q.flags.q.prim_type) {
      if (this->flags.q.prim_type && this->prim_type != q.prim_type) {
	 _mesa_glsl_error(loc, state,
			  "conflicting primitive type qualifiers used");
	 return false;
      }
      this->prim_type = q.prim_type;
   }

   if (q.flags.q.max_vertices) {
      if (this->max_vertices) {
         this->max_vertices->merge_qualifier(q.max_vertices);
      } else {
         this->max_vertices = q.max_vertices;
      }
   }

   if (q.flags.q.subroutine_def) {
      if (this->flags.q.subroutine_def) {
	 _mesa_glsl_error(loc, state,
			  "conflicting subroutine qualifiers used");
      } else {
         this->subroutine_list = q.subroutine_list;
      }
   }

   if (q.flags.q.invocations) {
      if (this->invocations) {
         this->invocations->merge_qualifier(q.invocations);
      } else {
         this->invocations = q.invocations;
      }
   }

   if (state->stage == MESA_SHADER_GEOMETRY &&
       state->has_explicit_attrib_stream()) {
      if (!this->flags.q.explicit_stream) {
         if (q.flags.q.stream) {
            this->flags.q.stream = 1;
            this->stream = q.stream;
         } else if (!this->flags.q.stream && this->flags.q.out) {
            /* Assign default global stream value */
            this->flags.q.stream = 1;
            this->stream = state->out_qualifier->stream;
         }
      }
   }

   if (q.flags.q.vertices) {
      if (this->vertices) {
         this->vertices->merge_qualifier(q.vertices);
      } else {
         this->vertices = q.vertices;
      }
   }

   if (q.flags.q.vertex_spacing) {
      if (this->flags.q.vertex_spacing && this->vertex_spacing != q.vertex_spacing) {
	 _mesa_glsl_error(loc, state,
			  "conflicting vertex spacing used");
	 return false;
      }
      this->vertex_spacing = q.vertex_spacing;
   }

   if (q.flags.q.ordering) {
      if (this->flags.q.ordering && this->ordering != q.ordering) {
	 _mesa_glsl_error(loc, state,
			  "conflicting ordering used");
	 return false;
      }
      this->ordering = q.ordering;
   }

   if (q.flags.q.point_mode) {
      if (this->flags.q.point_mode && this->point_mode != q.point_mode) {
	 _mesa_glsl_error(loc, state,
			  "conflicting point mode used");
	 return false;
      }
      this->point_mode = q.point_mode;
   }

   if ((q.flags.i & ubo_mat_mask.flags.i) != 0)
      this->flags.i &= ~ubo_mat_mask.flags.i;
   if ((q.flags.i & ubo_layout_mask.flags.i) != 0)
      this->flags.i &= ~ubo_layout_mask.flags.i;

   for (int i = 0; i < 3; i++) {
      if (q.flags.q.local_size & (1 << i)) {
         if (this->local_size[i]) {
            this->local_size[i]->merge_qualifier(q.local_size[i]);
         } else {
            this->local_size[i] = q.local_size[i];
         }
      }
   }

   this->flags.i |= q.flags.i;

   if (q.flags.q.explicit_location)
      this->location = q.location;

   if (q.flags.q.explicit_index)
      this->index = q.index;

   if (q.flags.q.explicit_binding)
      this->binding = q.binding;

   if (q.flags.q.explicit_offset)
      this->offset = q.offset;

   if (q.precision != ast_precision_none)
      this->precision = q.precision;

   if (q.flags.q.explicit_image_format) {
      this->image_format = q.image_format;
      this->image_base_type = q.image_base_type;
   }

   return true;
}

bool
ast_type_qualifier::merge_out_qualifier(YYLTYPE *loc,
                                        _mesa_glsl_parse_state *state,
                                        const ast_type_qualifier &q,
                                        ast_node* &node, bool create_node)
{
   void *mem_ctx = state;
   const bool r = this->merge_qualifier(loc, state, q, false);

   if (state->stage == MESA_SHADER_GEOMETRY) {
      if (q.flags.q.prim_type) {
         /* Make sure this is a valid output primitive type. */
         switch (q.prim_type) {
         case GL_POINTS:
         case GL_LINE_STRIP:
         case GL_TRIANGLE_STRIP:
            break;
         default:
            _mesa_glsl_error(loc, state, "invalid geometry shader output "
                             "primitive type");
            break;
         }
      }

      /* Allow future assigments of global out's stream id value */
      this->flags.q.explicit_stream = 0;
   } else if (state->stage == MESA_SHADER_TESS_CTRL) {
      if (create_node) {
         node = new(mem_ctx) ast_tcs_output_layout(*loc);
      }
   } else {
      _mesa_glsl_error(loc, state, "out layout qualifiers only valid in "
                       "tessellation control or geometry shaders");
   }

   return r;
}

bool
ast_type_qualifier::merge_in_qualifier(YYLTYPE *loc,
                                       _mesa_glsl_parse_state *state,
                                       const ast_type_qualifier &q,
                                       ast_node* &node, bool create_node)
{
   void *mem_ctx = state;
   bool create_gs_ast = false;
   bool create_cs_ast = false;
   ast_type_qualifier valid_in_mask;
   valid_in_mask.flags.i = 0;

   switch (state->stage) {
   case MESA_SHADER_TESS_EVAL:
      if (q.flags.q.prim_type) {
         /* Make sure this is a valid input primitive type. */
         switch (q.prim_type) {
         case GL_TRIANGLES:
         case GL_QUADS:
         case GL_ISOLINES:
            break;
         default:
            _mesa_glsl_error(loc, state,
                             "invalid tessellation evaluation "
                             "shader input primitive type");
            break;
         }
      }

      valid_in_mask.flags.q.prim_type = 1;
      valid_in_mask.flags.q.vertex_spacing = 1;
      valid_in_mask.flags.q.ordering = 1;
      valid_in_mask.flags.q.point_mode = 1;
      break;
   case MESA_SHADER_GEOMETRY:
      if (q.flags.q.prim_type) {
         /* Make sure this is a valid input primitive type. */
         switch (q.prim_type) {
         case GL_POINTS:
         case GL_LINES:
         case GL_LINES_ADJACENCY:
         case GL_TRIANGLES:
         case GL_TRIANGLES_ADJACENCY:
            break;
         default:
            _mesa_glsl_error(loc, state,
                             "invalid geometry shader input primitive type");
            break;
         }
      }

      create_gs_ast |=
         q.flags.q.prim_type &&
         !state->in_qualifier->flags.q.prim_type;

      valid_in_mask.flags.q.prim_type = 1;
      valid_in_mask.flags.q.invocations = 1;
      break;
   case MESA_SHADER_FRAGMENT:
      valid_in_mask.flags.q.early_fragment_tests = 1;
      break;
   case MESA_SHADER_COMPUTE:
      create_cs_ast |=
         q.flags.q.local_size != 0 &&
         state->in_qualifier->flags.q.local_size == 0;

      valid_in_mask.flags.q.local_size = 7;
      break;
   default:
      _mesa_glsl_error(loc, state,
                       "input layout qualifiers only valid in "
                       "geometry, fragment and compute shaders");
      break;
   }

   /* Generate an error when invalid input layout qualifiers are used. */
   if ((q.flags.i & ~valid_in_mask.flags.i) != 0) {
      _mesa_glsl_error(loc, state,
		       "invalid input layout qualifiers used");
      return false;
   }

   /* Input layout qualifiers can be specified multiple
    * times in separate declarations, as long as they match.
    */
   if (this->flags.q.prim_type) {
      if (q.flags.q.prim_type &&
          this->prim_type != q.prim_type) {
         _mesa_glsl_error(loc, state,
                          "conflicting input primitive %s specified",
                          state->stage == MESA_SHADER_GEOMETRY ?
                          "type" : "mode");
      }
   } else if (q.flags.q.prim_type) {
      state->in_qualifier->flags.q.prim_type = 1;
      state->in_qualifier->prim_type = q.prim_type;
   }

   if (q.flags.q.invocations) {
      this->flags.q.invocations = 1;
      if (this->invocations) {
         this->invocations->merge_qualifier(q.invocations);
      } else {
         this->invocations = q.invocations;
      }
   }

   if (q.flags.q.early_fragment_tests) {
      state->fs_early_fragment_tests = true;
   }

   if (this->flags.q.vertex_spacing) {
      if (q.flags.q.vertex_spacing &&
          this->vertex_spacing != q.vertex_spacing) {
         _mesa_glsl_error(loc, state,
                          "conflicting vertex spacing specified");
      }
   } else if (q.flags.q.vertex_spacing) {
      this->flags.q.vertex_spacing = 1;
      this->vertex_spacing = q.vertex_spacing;
   }

   if (this->flags.q.ordering) {
      if (q.flags.q.ordering &&
          this->ordering != q.ordering) {
         _mesa_glsl_error(loc, state,
                          "conflicting ordering specified");
      }
   } else if (q.flags.q.ordering) {
      this->flags.q.ordering = 1;
      this->ordering = q.ordering;
   }

   if (this->flags.q.point_mode) {
      if (q.flags.q.point_mode &&
          this->point_mode != q.point_mode) {
         _mesa_glsl_error(loc, state,
                          "conflicting point mode specified");
      }
   } else if (q.flags.q.point_mode) {
      this->flags.q.point_mode = 1;
      this->point_mode = q.point_mode;
   }

   if (create_node) {
      if (create_gs_ast) {
         node = new(mem_ctx) ast_gs_input_layout(*loc, q.prim_type);
      } else if (create_cs_ast) {
         node = new(mem_ctx) ast_cs_input_layout(*loc, q.local_size);
      }
   }

   return true;
}

bool
ast_layout_expression::process_qualifier_constant(struct _mesa_glsl_parse_state *state,
                                                  const char *qual_indentifier,
                                                  unsigned *value,
                                                  bool can_be_zero)
{
   int min_value = 0;
   bool first_pass = true;
   *value = 0;

   if (!can_be_zero)
      min_value = 1;

   for (exec_node *node = layout_const_expressions.head;
           !node->is_tail_sentinel(); node = node->next) {

      exec_list dummy_instructions;
      ast_node *const_expression = exec_node_data(ast_node, node, link);

      ir_rvalue *const ir = const_expression->hir(&dummy_instructions, state);

      ir_constant *const const_int = ir->constant_expression_value();
      if (const_int == NULL || !const_int->type->is_integer()) {
         YYLTYPE loc = const_expression->get_location();
         _mesa_glsl_error(&loc, state, "%s must be an integral constant "
                          "expression", qual_indentifier);
         return false;
      }

      if (const_int->value.i[0] < min_value) {
         YYLTYPE loc = const_expression->get_location();
         _mesa_glsl_error(&loc, state, "%s layout qualifier is invalid "
                          "(%d < %d)", qual_indentifier,
                          const_int->value.i[0], min_value);
         return false;
      }

      if (!first_pass && *value != const_int->value.u[0]) {
         YYLTYPE loc = const_expression->get_location();
         _mesa_glsl_error(&loc, state, "%s layout qualifier does not "
		          "match previous declaration (%d vs %d)",
                          qual_indentifier, *value, const_int->value.i[0]);
         return false;
      } else {
         first_pass = false;
         *value = const_int->value.u[0];
      }

      /* If the location is const (and we've verified that
       * it is) then no instructions should have been emitted
       * when we converted it to HIR. If they were emitted,
       * then either the location isn't const after all, or
       * we are emitting unnecessary instructions.
       */
      assert(dummy_instructions.is_empty());
   }

   return true;
}
