/*
 * Copyright © 2016 Red Hat
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

/* Lowering pass that lowers accesses to built-in uniform variables.
 * Built-in uniforms are not necessarily packed the same way that
 * normal uniform structs are, for example:
 *
 *    struct gl_FogParameters {
 *       vec4 color;
 *       float density;
 *       float start;
 *       float end;
 *       float scale;
 *    };
 *
 * is packed into vec4[2], whereas the same struct would be packed
 * (by gallium), as vec4[5] if it where not built-in.  Because of
 * this, we need to replace (for example) access like:
 *
 *    vec1 ssa_1 = intrinsic load_var () (gl_Fog.start) ()
 *
 * with:
 *
 *    vec4 ssa_2 = intrinsic load_var () (fog.params) ()
 *    vec1 ssa_1 = ssa_2.y
 *
 * with appropriate substitutions in the uniform variables list:
 *
 *    decl_var uniform INTERP_MODE_NONE gl_FogParameters gl_Fog (0, 0)
 *
 * would become:
 *
 *    decl_var uniform INTERP_MODE_NONE vec4 state.fog.color (0, 0)
 *    decl_var uniform INTERP_MODE_NONE vec4 state.fog.params (0, 1)
 *
 * See in particular 'struct gl_builtin_uniform_element'.
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "st_nir.h"
#include "compiler/glsl/ir.h"
#include "uniforms.h"
#include "program/prog_instruction.h"

typedef struct {
   nir_shader *shader;
   nir_builder builder;
   void *mem_ctx;
} lower_builtin_state;

static const struct gl_builtin_uniform_element *
get_element(const struct gl_builtin_uniform_desc *desc, nir_deref_var *deref)
{
   nir_deref *tail = &deref->deref;

   if ((desc->num_elements == 1) && (desc->elements[0].field == NULL))
      return NULL;

   /* we handle arrays in get_variable(): */
   if (tail->child->deref_type == nir_deref_type_array)
      tail = tail->child;

   /* don't need to deal w/ non-struct or array of non-struct: */
   if (!tail->child)
      return NULL;

   if (tail->child->deref_type != nir_deref_type_struct)
      return NULL;

   nir_deref_struct *deref_struct = nir_deref_as_struct(tail->child);

   assert(deref_struct->index < desc->num_elements);

   return &desc->elements[deref_struct->index];
}

static nir_variable *
get_variable(lower_builtin_state *state, nir_deref_var *deref,
             const struct gl_builtin_uniform_element *element)
{
   nir_shader *shader = state->shader;
   gl_state_index16 tokens[STATE_LENGTH];

   memcpy(tokens, element->tokens, sizeof(tokens));

   if (deref->deref.child->deref_type == nir_deref_type_array) {
      nir_deref_array *darr = nir_deref_as_array(deref->deref.child);

      assert(darr->deref_array_type == nir_deref_array_type_direct);

      /* we need to fixup the array index slot: */
      switch (tokens[0]) {
      case STATE_MODELVIEW_MATRIX:
      case STATE_PROJECTION_MATRIX:
      case STATE_MVP_MATRIX:
      case STATE_TEXTURE_MATRIX:
      case STATE_PROGRAM_MATRIX:
      case STATE_LIGHT:
      case STATE_LIGHTPROD:
      case STATE_TEXGEN:
      case STATE_TEXENV_COLOR:
      case STATE_CLIPPLANE:
         tokens[1] = darr->base_offset;
         break;
      }
   }

   char *name = _mesa_program_state_string(tokens);

   nir_foreach_variable(var, &shader->uniforms) {
      if (strcmp(var->name, name) == 0) {
         free(name);
         return var;
      }
   }

   /* variable doesn't exist yet, so create it: */
   nir_variable *var =
      nir_variable_create(shader, nir_var_uniform, glsl_vec4_type(), name);

   var->num_state_slots = 1;
   var->state_slots = ralloc_array(var, nir_state_slot, 1);
   memcpy(var->state_slots[0].tokens, tokens,
          sizeof(var->state_slots[0].tokens));

   free(name);

   return var;
}

static bool
lower_builtin_block(lower_builtin_state *state, nir_block *block)
{
   nir_builder *b = &state->builder;

   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      if (intrin->intrinsic != nir_intrinsic_load_var)
         continue;

      nir_variable *var = intrin->variables[0]->var;
      if (var->data.mode != nir_var_uniform)
         continue;

      /* built-in's will always start with "gl_" */
      if (strncmp(var->name, "gl_", 3) != 0)
         continue;

      const struct gl_builtin_uniform_desc *desc =
         _mesa_glsl_get_builtin_uniform_desc(var->name);

      /* if no descriptor, it isn't something we need to handle specially: */
      if (!desc)
         continue;

      const struct gl_builtin_uniform_element *element =
         get_element(desc, intrin->variables[0]);

      /* matrix elements (array_deref) do not need special handling: */
      if (!element)
         continue;

      /* remove existing var from uniform list: */
      exec_node_remove(&var->node);
      /* the _self_link() ensures we can remove multiple times, rather than
       * trying to keep track of what we have already removed:
       */
      exec_node_self_link(&var->node);

      nir_variable *new_var =
         get_variable(state, intrin->variables[0], element);

      b->cursor = nir_before_instr(instr);

      nir_ssa_def *def = nir_load_var(b, new_var);

      /* swizzle the result: */
      unsigned swiz[4];
      for (unsigned i = 0; i < 4; i++) {
         swiz[i] = GET_SWZ(element->swizzle, i);
         assert(swiz[i] <= SWIZZLE_W);
      }
      def = nir_swizzle(b, def, swiz, intrin->num_components, true);

      /* and rewrite uses of original instruction: */
      assert(intrin->dest.is_ssa);
      nir_ssa_def_rewrite_uses(&intrin->dest.ssa, nir_src_for_ssa(def));

      /* at this point intrin should be unused.  We need to remove it
       * (rather than waiting for DCE pass) to avoid dangling reference
       * to remove'd var.  And we have to remove the original uniform
       * var since we don't want it to get uniform space allocated.
       */
      nir_instr_remove(&intrin->instr);
   }

   return true;
}

static void
lower_builtin_impl(lower_builtin_state *state, nir_function_impl *impl)
{
   nir_builder_init(&state->builder, impl);
   state->mem_ctx = ralloc_parent(impl);

   nir_foreach_block(block, impl) {
      lower_builtin_block(state, block);
   }

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}

void
st_nir_lower_builtin(nir_shader *shader)
{
   lower_builtin_state state;
   state.shader = shader;
   nir_foreach_function(function, shader) {
      if (function->impl)
         lower_builtin_impl(&state, function->impl);
   }
}
