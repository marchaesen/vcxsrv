/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include "nir.h"
#include "nir_builder.h"

/*
 * It is challenging to optimize the complex deref chains resulting from
 * nontrivial OpenCL C constructs. nir_opt_deref generally does a good job, but
 * occassionally we are forced to lower temporaries to scratch anyway. LLVM's
 * recent embrace of opaque pointers have exacerbated this problem.
 *
 * The "proper" solutions here are to smarten nir_opt_deref and/or to use LLVM's
 * own optimization passes to clean up the input IR. Both of these are
 * challenging projects for the medium-term.
 *
 * In the short term, this pass is a stopgap. After lowering away all derefs to
 * scratch, this pass can "unlower" scratch memory back into nir_variable
 * access. The lower->unlower pair is lossy. The point is not to reconstruct the
 * original derefs (that we failed to optimize), but instead just to model array
 * access that other NIR passes can optimize. The resulting array accesses will
 * generally optimize out if there are no indirects, or can be lowered to bcsel
 * instead of scratch if that's preferable for a driver.
 */

/*
 * This pass operates only on 32-bit scalars, so this callback instructs
 * nir_lower_mem_access_bit_sizes_options to turn all scratch access into
 * 32-bit scalars. We don't want to use 8-bit accesses, since that would be
 * challenging to optimize the resulting pack/unpack on some drivers. Larger
 * 32-bit access however requires nontrivial tracking to extract/insert. Since
 * nir_lower_mem_access_bit_sizes already has that code, we use it in this pass
 * instead of NIH'ing it here.
 */
static nir_mem_access_size_align
mem_access_cb(nir_intrinsic_op intrin, uint8_t bytes, uint8_t bit_size,
              uint32_t align, uint32_t align_offset, bool offset_is_const,
              enum gl_access_qualifier access, const void *cb_data)
{
   return (nir_mem_access_size_align){
      .num_components = 1,
      .bit_size = 32,
      .align = 4,
      .shift = nir_mem_access_shift_method_scalar,
   };
}

/*
 * Thanks to nir_lower_mem_access_bit_sizes, we can lower scratch intrinsics 1:1
 * to word-based array access.
 */
static bool
lower_scratch_to_var(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   nir_variable *scratch = data;
   b->cursor = nir_before_instr(&intr->instr);

   if (intr->intrinsic == nir_intrinsic_store_scratch) {
      nir_def *index = nir_udiv_aligned_4(b, intr->src[1].ssa);
      nir_def *value = intr->src[0].ssa;

      index = nir_u2uN(b, index, nir_get_ptr_bitsize(b->shader));
      nir_store_array_var(b, scratch, index, value, nir_component_mask(1));
   } else if (intr->intrinsic == nir_intrinsic_load_scratch) {
      nir_def *index = nir_udiv_aligned_4(b, intr->src[0].ssa);

      index = nir_u2uN(b, index, nir_get_ptr_bitsize(b->shader));
      nir_def_rewrite_uses(&intr->def, nir_load_array_var(b, scratch, index));
   } else {
      return false;
   }

   nir_instr_remove(&intr->instr);
   return true;
}

bool
nir_lower_scratch_to_var(nir_shader *nir)
{
   unsigned words = DIV_ROUND_UP(nir->scratch_size, 4);

   /* Early exit in the common case that scratch is not used. */
   if (words == 0) {
      return false;
   }

   /* First, lower bit sizes and vectors as required by lower_scratch_to_var */
   nir_lower_mem_access_bit_sizes_options lower_mem_access_options = {
      .modes = nir_var_shader_temp | nir_var_function_temp,
      .callback = mem_access_cb,
   };
   NIR_PASS(_, nir, nir_lower_mem_access_bit_sizes, &lower_mem_access_options);

   /* Then, back scratch by an array of words and turn all scratch access into
    * array access. We do this per-function, treating scratch as a
    * function-local stack. This is correct for single-function shaders (the
    * fully-inlined graphics case) and for collections of single-function
    * shaders (the vtn_bindgen2 case). It is sketchy for drivers supporting true
    * function calls, but before we can support that properly, we need to fix
    * NIR's definition of scratch to instead be stack. So this is what we need
    * for now, and hopefully this whole pass can be deleted someday.
    */
   nir_foreach_function_impl(impl, nir) {
      const glsl_type *type_ = glsl_array_type(glsl_uint_type(), words, 1);
      nir_variable *var = nir_local_variable_create(impl, type_, "scratch");
      nir_function_intrinsics_pass(impl, lower_scratch_to_var,
                                   nir_metadata_control_flow, var);
   }

   /* After lowering, we've eliminated all scratch in the shader. Really, this
    * should be per-function. Again, scratch is ill-defined in NIR for
    * multi-function and we need deeper fixes to NIR. This whole pass is a
    * bandage.
    */
   nir->scratch_size = 0;

   /* Now clean up the mess we made */
   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);
      NIR_PASS(progress, nir, nir_opt_constant_folding);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_dce);
   } while (progress);

   return true;
}
