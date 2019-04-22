/*
 * Copyright © 2019 Red Hat Inc.
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
 * \file
 *
 * Lower bindless image operations by turning the image_deref_* into a
 * bindless_image_* intrinsic and adding a load_deref on the previous deref
 * source. All applicable indicies are also set so that fetching the variable
 * in the backend wouldn't be needed anymore.
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_deref.h"

#include "compiler/glsl/gl_nir.h"

static bool
lower_impl(nir_builder *b, nir_instr *instr) {
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instr);

   nir_deref_instr *deref;
   nir_variable *var;

   switch (intrinsic->intrinsic) {
   case nir_intrinsic_image_deref_atomic_add:
   case nir_intrinsic_image_deref_atomic_min:
   case nir_intrinsic_image_deref_atomic_max:
   case nir_intrinsic_image_deref_atomic_and:
   case nir_intrinsic_image_deref_atomic_or:
   case nir_intrinsic_image_deref_atomic_xor:
   case nir_intrinsic_image_deref_atomic_exchange:
   case nir_intrinsic_image_deref_atomic_comp_swap:
   case nir_intrinsic_image_deref_atomic_fadd:
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_samples:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_store: {
      deref = nir_src_as_deref(intrinsic->src[0]);
      var = nir_deref_instr_get_variable(deref);
      break;
   }
   default:
      return false;
   }

   if (deref->mode == nir_var_uniform && !var->data.bindless)
      return false;

   b->cursor = nir_before_instr(instr);
   nir_ssa_def *handle = nir_load_deref(b, deref);
   nir_rewrite_image_intrinsic(intrinsic, handle, true);
   return true;
}

bool
gl_nir_lower_bindless_images(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_builder b;
         nir_builder_init(&b, function->impl);

         nir_foreach_block(block, function->impl)
            nir_foreach_instr(instr, block)
               progress |= lower_impl(&b, instr);
      }
   }

   return progress;
}
