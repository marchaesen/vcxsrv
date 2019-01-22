/*
 * Copyright © 2018 Intel Corporation
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
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_deref.h"

struct var_info {
   bool is_constant;
   bool found_read;
};

static nir_ssa_def *
build_constant_load(nir_builder *b, nir_deref_instr *deref,
                    glsl_type_size_align_func size_align)
{
   nir_variable *var = nir_deref_instr_get_variable(deref);

   const unsigned bit_size = glsl_get_bit_size(deref->type);
   const unsigned num_components = glsl_get_vector_elements(deref->type);

   UNUSED unsigned var_size, var_align;
   size_align(var->type, &var_size, &var_align);
   assert(var->data.location % var_align == 0);

   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_constant);
   load->num_components = num_components;
   nir_intrinsic_set_base(load, var->data.location);
   nir_intrinsic_set_range(load, var_size);
   load->src[0] = nir_src_for_ssa(nir_build_deref_offset(b, deref, size_align));
   nir_ssa_dest_init(&load->instr, &load->dest,
                     num_components, bit_size, NULL);
   nir_builder_instr_insert(b, &load->instr);

   if (load->dest.ssa.bit_size < 8) {
      /* Booleans are special-cased to be 32-bit
       *
       * Ideally, for drivers that can handle 32-bit booleans, we wouldn't
       * emit the i2b here.  However, at this point, the driver is likely to
       * still have 1-bit booleans so we need to at least convert bit sizes.
       * Unfortunately, we don't have a good way to annotate the load as
       * loading a known boolean value so the optimizer isn't going to be
       * able to get rid of the conversion.  Some day, we may solve that
       * problem but not today.
       */
      assert(glsl_type_is_boolean(deref->type));
      load->dest.ssa.bit_size = 32;
      return nir_i2b(b, &load->dest.ssa);
   } else {
      return &load->dest.ssa;
   }
}

static void
handle_constant_store(nir_builder *b, nir_intrinsic_instr *store,
                      glsl_type_size_align_func size_align)
{
   nir_deref_instr *deref = nir_src_as_deref(store->src[0]);
   assert(!nir_deref_instr_has_indirect(deref));

   nir_variable *var = nir_deref_instr_get_variable(deref);

   const unsigned bit_size = glsl_get_bit_size(deref->type);
   const unsigned num_components = glsl_get_vector_elements(deref->type);

   char *dst = (char *)b->shader->constant_data +
               var->data.location +
               nir_deref_instr_get_const_offset(deref, size_align);

   nir_const_value *val = nir_src_as_const_value(store->src[1]);
   switch (bit_size) {
   case 1:
      /* Booleans are special-cased to be 32-bit */
      for (unsigned i = 0; i < num_components; i++)
         ((int32_t *)dst)[i] = -(int)val->b[i];
      break;

   case 8:
      for (unsigned i = 0; i < num_components; i++)
         ((uint8_t *)dst)[i] = val->u8[i];
      break;

   case 16:
      for (unsigned i = 0; i < num_components; i++)
         ((uint16_t *)dst)[i] = val->u16[i];
      break;

   case 32:
      for (unsigned i = 0; i < num_components; i++)
         ((uint32_t *)dst)[i] = val->u32[i];
      break;

   case 64:
      for (unsigned i = 0; i < num_components; i++)
         ((uint64_t *)dst)[i] = val->u64[i];
      break;

   default:
      unreachable("Invalid bit size");
   }
}

/** Lower large constant variables to shader constant data
 *
 * This pass looks for large (type_size(var->type) > threshold) variables
 * which are statically constant and moves them into shader constant data.
 * This is especially useful when large tables are baked into the shader
 * source code because they can be moved into a UBO by the driver to reduce
 * register pressure and make indirect access cheaper.
 */
bool
nir_opt_large_constants(nir_shader *shader,
                        glsl_type_size_align_func size_align,
                        unsigned threshold)
{
   /* Default to a natural alignment if none is provided */
   if (size_align == NULL)
      size_align = glsl_get_natural_size_align_bytes;

   /* This only works with a single entrypoint */
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   /* This pass can only be run once */
   assert(shader->constant_data == NULL && shader->constant_data_size == 0);

   /* The index parameter is unused for local variables so we'll use it for
    * indexing into our array of variable metadata.
    */
   unsigned num_locals = 0;
   nir_foreach_variable(var, &impl->locals)
      var->data.index = num_locals++;

   struct var_info *var_infos = malloc(num_locals * sizeof(struct var_info));
   for (unsigned i = 0; i < num_locals; i++) {
      var_infos[i] = (struct var_info) {
         .is_constant = true,
         .found_read = false,
      };
   }

   /* First, walk through the shader and figure out what variables we can
    * lower to the constant blob.
    */
   bool first_block = true;
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         bool src_is_const = false;
         nir_deref_instr *src_deref = NULL, *dst_deref = NULL;
         switch (intrin->intrinsic) {
         case nir_intrinsic_store_deref:
            dst_deref = nir_src_as_deref(intrin->src[0]);
            src_is_const = nir_src_is_const(intrin->src[1]);
            break;

         case nir_intrinsic_load_deref:
            src_deref = nir_src_as_deref(intrin->src[0]);
            break;

         case nir_intrinsic_copy_deref:
            /* We always assume the src and therefore the dst are not
             * constants here. Copy and constant propagation passes should
             * have taken care of this in most cases anyway.
             */
            dst_deref = nir_src_as_deref(intrin->src[0]);
            src_deref = nir_src_as_deref(intrin->src[1]);
            src_is_const = false;
            break;

         default:
            continue;
         }

         if (dst_deref && dst_deref->mode == nir_var_function_temp) {
            nir_variable *var = nir_deref_instr_get_variable(dst_deref);
            assert(var->data.mode == nir_var_function_temp);

            /* We only consider variables constant if they only have constant
             * stores, all the stores come before any reads, and all stores
             * come in the first block.  We also can't handle indirect stores.
             */
            struct var_info *info = &var_infos[var->data.index];
            if (!src_is_const || info->found_read || !first_block ||
                nir_deref_instr_has_indirect(dst_deref))
               info->is_constant = false;
         }

         if (src_deref && src_deref->mode == nir_var_function_temp) {
            nir_variable *var = nir_deref_instr_get_variable(src_deref);
            assert(var->data.mode == nir_var_function_temp);

            var_infos[var->data.index].found_read = true;
         }
      }
      first_block = false;
   }

   shader->constant_data_size = 0;
   nir_foreach_variable(var, &impl->locals) {
      struct var_info *info = &var_infos[var->data.index];
      if (!info->is_constant)
         continue;

      unsigned var_size, var_align;
      size_align(var->type, &var_size, &var_align);
      if (var_size <= threshold || !info->found_read) {
         /* Don't bother lowering small stuff or data that's never read */
         info->is_constant = false;
         continue;
      }

      var->data.location = ALIGN_POT(shader->constant_data_size, var_align);
      shader->constant_data_size = var->data.location + var_size;
   }

   if (shader->constant_data_size == 0) {
      free(var_infos);
      return false;
   }

   shader->constant_data = rzalloc_size(shader, shader->constant_data_size);

   nir_builder b;
   nir_builder_init(&b, impl);

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         switch (intrin->intrinsic) {
         case nir_intrinsic_load_deref: {
            nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
            if (deref->mode != nir_var_function_temp)
               continue;

            nir_variable *var = nir_deref_instr_get_variable(deref);
            struct var_info *info = &var_infos[var->data.index];
            if (info->is_constant) {
               b.cursor = nir_after_instr(&intrin->instr);
               nir_ssa_def *val = build_constant_load(&b, deref, size_align);
               nir_ssa_def_rewrite_uses(&intrin->dest.ssa,
                                        nir_src_for_ssa(val));
               nir_instr_remove(&intrin->instr);
               nir_deref_instr_remove_if_unused(deref);
            }
            break;
         }

         case nir_intrinsic_store_deref: {
            nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
            if (deref->mode != nir_var_function_temp)
               continue;

            nir_variable *var = nir_deref_instr_get_variable(deref);
            struct var_info *info = &var_infos[var->data.index];
            if (info->is_constant) {
               b.cursor = nir_after_instr(&intrin->instr);
               handle_constant_store(&b, intrin, size_align);
               nir_instr_remove(&intrin->instr);
               nir_deref_instr_remove_if_unused(deref);
            }
            break;
         }

         case nir_intrinsic_copy_deref: {
            nir_deref_instr *deref = nir_src_as_deref(intrin->src[1]);
            if (deref->mode != nir_var_function_temp)
               continue;

            nir_variable *var = nir_deref_instr_get_variable(deref);
            struct var_info *info = &var_infos[var->data.index];
            if (info->is_constant) {
               b.cursor = nir_after_instr(&intrin->instr);
               nir_ssa_def *val = build_constant_load(&b, deref, size_align);
               nir_store_deref(&b, nir_src_as_deref(intrin->src[0]), val, ~0);
               nir_instr_remove(&intrin->instr);
               nir_deref_instr_remove_if_unused(deref);
            }
            break;
         }

         default:
            continue;
         }
      }
   }

   /* Clean up the now unused variables */
   nir_foreach_variable_safe(var, &impl->locals) {
      if (var_infos[var->data.index].is_constant)
         exec_node_remove(&var->node);
   }

   free(var_infos);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
   return true;
}
