/*
 * Copyright © 2024 Valve Corporation
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

#include "gl_nir_linker.h"
#include "linker_util.h"
#include "program/symbol_table.h"
#include "util/hash_table.h"
#include "main/shader_types.h"

struct function_sig {
   nir_function *func;

   struct list_head node;
};

typedef enum {
   PARAMETER_LIST_NO_MATCH,
   PARAMETER_LIST_EXACT_MATCH,
   PARAMETER_LIST_INEXACT_MATCH /* Match requires implicit conversion. */
} parameter_list_match_t;

/**
 * Check if two parameter lists match.
 *
 * list_a Parameters of the function definition.
 * list_b Actual parameters passed to the function.
 */
static parameter_list_match_t
parameter_lists_match(bool has_implicit_conversions,
                      bool has_implicit_int_to_uint_conversion,
                      nir_parameter *list_a, unsigned num_params_a,
                      nir_parameter *list_b, unsigned num_params_b)
{
   /* The lists have different length and by definition do not match. */
   if (num_params_a != num_params_b)
      return PARAMETER_LIST_NO_MATCH;

   nir_parameter *param_a;
   nir_parameter *param_b;

   /* This is set to true if there is an inexact match requiring an implicit
    * conversion. */
   bool inexact_match = false;

   for (int i = 0; i < num_params_a; i++) {
      param_a = &list_a[i];
      param_b = &list_b[i];

      if (param_a->type == param_b->type)
         continue;

      /* Try to find an implicit conversion from actual to param. */
      inexact_match = true;

      switch (param_a->mode) {
      case nir_var_function_in:
         if (param_a->implicit_conversion_prohibited ||
             !_mesa_glsl_can_implicitly_convert(param_b->type, param_a->type,
                                                has_implicit_conversions,
                                                has_implicit_int_to_uint_conversion))
            return PARAMETER_LIST_NO_MATCH;
         break;

      case nir_var_function_out:
         if (!_mesa_glsl_can_implicitly_convert(param_a->type, param_b->type,
                                                has_implicit_conversions,
                                                has_implicit_int_to_uint_conversion))
            return PARAMETER_LIST_NO_MATCH;
         break;

      case nir_var_function_inout:
         /* Since there are no bi-directional automatic conversions (e.g.,
          * there is int -> float but no float -> int), inout parameters must
          * be exact matches.
          */
         return PARAMETER_LIST_NO_MATCH;

      default:
         assert(false);
         return PARAMETER_LIST_NO_MATCH;
      }
   }

   if (inexact_match)
      return PARAMETER_LIST_INEXACT_MATCH;
   else
      return PARAMETER_LIST_EXACT_MATCH;
}


/* Classes of parameter match, sorted (mostly) best matches first.
 * See is_better_parameter_match() below for the exceptions.
 * */
typedef enum {
   PARAMETER_EXACT_MATCH,
   PARAMETER_FLOAT_TO_DOUBLE,
   PARAMETER_INT_TO_FLOAT,
   PARAMETER_INT_TO_DOUBLE,
   PARAMETER_OTHER_CONVERSION,
} parameter_match_t;


static parameter_match_t
get_parameter_match_type(const nir_parameter *param,
                         const nir_parameter *actual)
{
   const struct glsl_type *from_type;
   const struct glsl_type *to_type;

   if (param->mode == nir_var_function_out) {
      from_type = param->type;
      to_type = actual->type;
   } else {
      from_type = actual->type;
      to_type = param->type;
   }

   if (from_type == to_type)
      return PARAMETER_EXACT_MATCH;

   if (glsl_type_is_double(to_type)) {
      if (glsl_type_is_float(from_type))
         return PARAMETER_FLOAT_TO_DOUBLE;
      return PARAMETER_INT_TO_DOUBLE;
   }

   if (glsl_type_is_float(to_type))
      return PARAMETER_INT_TO_FLOAT;

   /* int -> uint and any other oddball conversions */
   return PARAMETER_OTHER_CONVERSION;
}

/* From section 6.1 of the GLSL 4.00 spec (and the ARB_gpu_shader5 spec):
 *
 * 1. An exact match is better than a match involving any implicit
 * conversion.
 *
 * 2. A match involving an implicit conversion from float to double
 * is better than match involving any other implicit conversion.
 *
 * [XXX: Not in GLSL 4.0: Only in ARB_gpu_shader5:
 * 3. A match involving an implicit conversion from either int or uint
 * to float is better than a match involving an implicit conversion
 * from either int or uint to double.]
 *
 * If none of the rules above apply to a particular pair of conversions,
 * neither conversion is considered better than the other.
 *
 * --
 *
 * Notably, the int->uint conversion is *not* considered to be better
 * or worse than int/uint->float or int/uint->double.
 */
static bool
is_better_parameter_match(parameter_match_t a_match,
                          parameter_match_t b_match)
{
   if (a_match >= PARAMETER_INT_TO_FLOAT && b_match == PARAMETER_OTHER_CONVERSION)
      return false;

   return a_match < b_match;
}

/* From section 6.1 of the GLSL 4.00 spec (and the ARB_gpu_shader5 spec):
 *
 * "A function definition A is considered a better
 * match than function definition B if:
 *
 *   * for at least one function argument, the conversion for that argument
 *     in A is better than the corresponding conversion in B; and
 *
 *   * there is no function argument for which the conversion in B is better
 *     than the corresponding conversion in A.
 *
 * If a single function definition is considered a better match than every
 * other matching function definition, it will be used.  Otherwise, a
 * semantic error occurs and the shader will fail to compile."
 */
static bool
is_best_inexact_overload(nir_parameter *actual_parameters,
                         unsigned num_parameters,
                         nir_function **matches, int num_matches,
                         nir_function *sig)
{

   for (nir_function **other = matches; other < matches + num_matches; other++) {
      if (*other == sig)
         continue;

      nir_parameter *node_a = sig->params;
      nir_parameter *node_b = (*other)->params;

      bool better_for_some_parameter = false;

      for (unsigned i = 0; i < num_parameters; i++) {
         parameter_match_t a_match =
            get_parameter_match_type(&node_a[i], &actual_parameters[i]);
         parameter_match_t b_match =
            get_parameter_match_type(&node_b[i], &actual_parameters[i]);

         if (is_better_parameter_match(a_match, b_match))
               better_for_some_parameter = true;

         if (is_better_parameter_match(b_match, a_match))
               return false; /* B is better for this parameter */
      }

      if (!better_for_some_parameter)
         return false; /* A must be better than B for some parameter */
   }

   return true;
}

static nir_function *
choose_best_inexact_overload(nir_parameter *actual_parameters,
                             unsigned num_parameters,
                             nir_function **matches, int num_matches,
                             bool has_choose_best_inexact_overload)
{
   if (num_matches == 0)
      return NULL;

   if (num_matches == 1)
      return *matches;

   if (!has_choose_best_inexact_overload)
      return NULL;

   for (nir_function **sig = matches; sig < matches + num_matches; sig++) {
      if (is_best_inexact_overload(actual_parameters, num_parameters,
                                   matches, num_matches, *sig))
         return *sig;
   }

   /* no best candidate */
   return NULL;
}

static nir_function *
find_matching_signature(struct list_head *f_list,
                        nir_parameter *parameters,
                        unsigned num_parameters,
                        bool has_implicit_conversions,
                        bool has_implicit_int_to_uint_conversion)
{
   nir_function **inexact_matches = NULL;
   nir_function **inexact_matches_temp;
   nir_function *match = NULL;
   int num_inexact_matches = 0;

   /* From page 42 (page 49 of the PDF) of the GLSL 1.20 spec:
    *
    * "If an exact match is found, the other signatures are ignored, and
    *  the exact match is used.  Otherwise, if no exact match is found, then
    *  the implicit conversions in Section 4.1.10 "Implicit Conversions" will
    *  be applied to the calling arguments if this can make their types match
    *  a signature.  In this case, it is a semantic error if there are
    *  multiple ways to apply these conversions to the actual arguments of a
    *  call such that the call can be made to match multiple signatures."
    */
   list_for_each_entry(struct function_sig, sig, f_list, node) {
      switch (parameter_lists_match(has_implicit_conversions,
                                    has_implicit_int_to_uint_conversion,
                                    sig->func->params, sig->func->num_params,
                                    parameters, num_parameters)) {
      case PARAMETER_LIST_EXACT_MATCH:
         free(inexact_matches);
         return sig->func;
      case PARAMETER_LIST_INEXACT_MATCH:
         /* Subroutine signatures must match exactly */
         if (sig->func->is_subroutine)
            continue;

         inexact_matches_temp = (nir_function **)
               realloc(inexact_matches,
                       sizeof(*inexact_matches) *
                       (num_inexact_matches + 1));

         inexact_matches = inexact_matches_temp;
         inexact_matches[num_inexact_matches++] = sig->func;
         continue;
      case PARAMETER_LIST_NO_MATCH:
         continue;
      default:
         assert(false);
         return NULL;
      }
   }

   match = choose_best_inexact_overload(parameters, num_parameters,
                                        inexact_matches, num_inexact_matches,
                                        has_implicit_int_to_uint_conversion);

   free(inexact_matches);
   return match;
}

static nir_function *
clone_function(struct hash_table *remap_table,
               const nir_function *fxn, nir_shader *ns)
{
   nir_function *nfxn = nir_function_clone(ns, fxn);
   /* Needed for call instructions */
   _mesa_hash_table_insert(remap_table, fxn, nfxn);

   return nfxn;
}

bool
gl_nir_link_function_calls(struct gl_shader_program *prog,
                           struct gl_shader *main,
                           struct gl_linked_shader *linked_sh,
                           struct gl_shader **shader_list,
                           unsigned num_shaders)
{
   void *mem_ctx = ralloc_context(NULL);
   struct hash_table *var_lookup = _mesa_string_hash_table_create(mem_ctx);
   struct hash_table *func_lookup = _mesa_string_hash_table_create(mem_ctx);
   struct hash_table *remap_table = _mesa_pointer_hash_table_create(mem_ctx);

   nir_foreach_variable_in_shader(var, linked_sh->Program->nir) {
      _mesa_hash_table_insert(var_lookup, var->name, var);
   }

   nir_foreach_function(func, linked_sh->Program->nir) {
      if (!func->impl)
         continue;

      struct hash_entry *e = _mesa_hash_table_search(func_lookup, func->name);
      if (e) {
         struct list_head *f_list = (struct list_head *) e->data;

         nir_function *f = find_matching_signature(f_list, func->params,
                                                   func->num_params,
                                                   main->has_implicit_conversions,
                                                   main->has_implicit_int_to_uint_conversion);
         if (!f) {
            struct function_sig *func_sig = ralloc(mem_ctx, struct function_sig);
            func_sig->func = func;
            list_add(&func_sig->node, f_list);
         }
      } else {
         struct list_head *func_list = ralloc(mem_ctx, struct list_head);
         list_inithead(func_list);

         struct function_sig *func_sig = ralloc(mem_ctx, struct function_sig);
         func_sig->func = func;
         list_add(&func_sig->node, func_list);
         _mesa_hash_table_insert(func_lookup, func->name, func_list);
      }
   }

   for (unsigned i = 0; i < num_shaders; i++) {
      /* Skip shader object with main function as we have already cloned the
       * full shader.
       */
      if (main == shader_list[i])
         continue;

      /* Before cloning the shader check the lookup table to see if globals
       * have already been seen in a previous shader, if so update the remap
       * table.
       */
      nir_foreach_variable_in_shader(var, shader_list[i]->nir) {
         struct hash_entry *e =
            _mesa_hash_table_search(var_lookup, var->name);
         if (e) {
            _mesa_hash_table_insert(remap_table, var, e->data);

            nir_variable *m_var = (nir_variable *) e->data;
            if (glsl_type_is_array(var->type)) {
               /* It is possible to have a global array declared in multiple
                * shaders without a size.  The array is implicitly sized by
                * the maximal access to it in *any* shader.  Because of this,
                * we need to track the maximal access to the array as linking
                * pulls more functions in that access the array.
                */
               m_var->data.max_array_access =
                  MAX2(var->data.max_array_access,
                       m_var->data.max_array_access);

               if (glsl_array_size(m_var->type) == 0 &&
                   glsl_array_size(var->type) != 0)
                  m_var->type = var->type;
            }
            if (glsl_without_array(var->type) == var->interface_type) {
               /* Similarly, we need implicit sizes of arrays within interface
                * blocks to be sized by the maximal access in *any* shader.
                */
               int *linked_max_ifc_array_access = m_var->max_ifc_array_access;
               int *ir_max_ifc_array_access = var->max_ifc_array_access;

               assert(linked_max_ifc_array_access != NULL);
               assert(ir_max_ifc_array_access != NULL);

               for (unsigned j = 0; j < var->interface_type->length; j++) {
                  linked_max_ifc_array_access[j] =
                     MAX2(linked_max_ifc_array_access[j],
                          ir_max_ifc_array_access[j]);
               }
            }
         } else {
            nir_variable *nvar =
               nir_variable_clone(var, linked_sh->Program->nir);
            _mesa_hash_table_insert(remap_table, var, nvar);
            nir_shader_add_variable(linked_sh->Program->nir, nvar);
            _mesa_hash_table_insert(var_lookup, var->name, nvar);
         }
      }

      /* Clone functions into our combined shader */
      nir_foreach_function(func, shader_list[i]->nir) {
         nir_function *f = NULL;

         /* Try to find the signature in one of the shaders that is being
          * linked. If not found clone the function.
          */
         struct hash_entry *e = _mesa_hash_table_search(func_lookup, func->name);
         if (e) {
            struct list_head *f_list = (struct list_head *) e->data;

            f = find_matching_signature(f_list, func->params,
                                        func->num_params,
                                        false,
                                        false);
            if (!f) {
               struct function_sig *func_sig = ralloc(mem_ctx, struct function_sig);
               f = clone_function(remap_table, func, linked_sh->Program->nir);
               func_sig->func = f;
               if (func->impl)
                  list_add(&func_sig->node, f_list);
            } else {
               _mesa_hash_table_insert(remap_table, func, f);
            }
         } else {
            struct list_head *func_list = ralloc(mem_ctx, struct list_head);
            list_inithead(func_list);

            struct function_sig *func_sig = ralloc(mem_ctx, struct function_sig);
            f = clone_function(remap_table, func, linked_sh->Program->nir);
            func_sig->func = f;
            if (func->impl)
               list_add(&func_sig->node, func_list);
            _mesa_hash_table_insert(func_lookup, func->name, func_list);
         }
      }

      /* Now that all functions are cloned we can clone any function
       * implementations. We can't do this in the previous loop above because
       * glsl to nir places function declarations next to implementations i.e.
       * we have lost any predeclared function signatures so we won't always
       * find them in the remap table until they have all been processed.
       */
      nir_foreach_function(func, shader_list[i]->nir) {
         if (func->impl) {
            nir_function_impl *f_impl =
               nir_function_impl_clone_remap_globals(linked_sh->Program->nir,
                                                     func->impl, remap_table);

            struct hash_entry *e =
               _mesa_hash_table_search(remap_table, func);
            assert(e);

            nir_function *f = (nir_function *) e->data;

            assert(!f->impl);
            nir_function_set_impl(f, f_impl);
         }
      }
   }

   /* Now that all shaders have been combined together make sure all function
    * calls can be resolved.
    */
   nir_foreach_function_impl(impl, linked_sh->Program->nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type == nir_instr_type_call) {
               nir_call_instr *call = nir_instr_as_call(instr);

               /* If this was already set at compile time don't try to set it
                * again.
                */
               if (call->callee->impl)
                  continue;

               struct hash_entry *e = _mesa_hash_table_search(func_lookup,
                                                              call->callee->name);
               if (e) {
                  struct list_head *f_list = (struct list_head *) e->data;

                  nir_function *f =
                     find_matching_signature(f_list, call->callee->params,
                                             call->callee->num_params,
                                             main->has_implicit_conversions,
                                             main->has_implicit_int_to_uint_conversion);
                  if (f)
                     call->callee = f;
               }

               if (!call->callee->impl) {
                  linker_error(prog, "unresolved reference to function `%s'\n",
                               call->callee->name);
                  ralloc_free(mem_ctx);
                  return false;
               }
            }
         }
      }
   }

   /**
    * Link all out variables on a single stage which are not
    * directly used in a shader with the main function.
    */
   if (linked_sh->Stage != MESA_SHADER_FRAGMENT) {
      for (unsigned i = 0; i < num_shaders; i++) {
         /* Skip shader object with main function as we have already cloned
          * the full shader, including shader outputs.
          */
         if (main == shader_list[i])
            continue;

         nir_foreach_shader_out_variable(var, shader_list[i]->nir) {
            struct hash_entry *e =
               _mesa_hash_table_search(var_lookup, var->name);
            if (e)
               continue;

            nir_variable *nvar = nir_variable_clone(var, linked_sh->Program->nir);
            nir_shader_add_variable(linked_sh->Program->nir, nvar);
            _mesa_hash_table_insert(var_lookup, var->name, var);
         }
      }
   }


   /* Call fixup deref types as we may have set array sizes above */
   nir_fixup_deref_types(linked_sh->Program->nir);

   ralloc_free(mem_ctx);

   return true;
}
