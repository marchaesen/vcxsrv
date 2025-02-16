/*
 * Copyright 2024 Valve Corporation
 * Copyright 2023 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "compiler/spirv/nir_spirv.h"
#include "util/u_printf.h"
#include "glsl_types.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_builder_opcodes.h"
#include "nir_precompiled.h"
#include "nir_serialize.h"

static const struct spirv_to_nir_options spirv_options = {
   .environment = NIR_SPIRV_OPENCL,
   .shared_addr_format = nir_address_format_62bit_generic,
   .global_addr_format = nir_address_format_62bit_generic,
   .temp_addr_format = nir_address_format_62bit_generic,
   .constant_addr_format = nir_address_format_64bit_global,
   .create_library = true,
   .printf = true,
};

struct nir_shader_compiler_options generic_opts = {
   /* TODO: Do we want to set has_*? Will drivers be able to lower
    * appropriately?
    */
   .fuse_ffma16 = true,
   .fuse_ffma32 = true,
   .fuse_ffma64 = true,

   .max_unroll_iterations = 32,
   .max_unroll_iterations_fp64 = 32,
};

static bool
rewrite_return(nir_builder *b, nir_intrinsic_instr *intr, void *return_deref)
{
   if (intr->intrinsic != nir_intrinsic_load_param)
      return false;

   unsigned idx = nir_intrinsic_param_idx(intr);
   if (idx == 0)
      nir_def_replace(&intr->def, return_deref);
   else
      nir_intrinsic_set_param_idx(intr, idx - 1);

   return true;
}

static void
lower_to_bindgen_return(nir_shader *nir)
{
   nir_foreach_function(libfunc, nir) {
      bool returns = libfunc->params[0].is_return;
      libfunc->pass_flags = returns;
      if (!returns)
         continue;

      nir_variable *ret = nir_local_variable_create(
         libfunc->impl, libfunc->params[0].type, "return");

      nir_builder b = nir_builder_at(nir_before_impl(libfunc->impl));
      nir_deref_instr *deref = nir_build_deref_var(&b, ret);

      nir_function_intrinsics_pass(libfunc->impl, rewrite_return,
                                   nir_metadata_control_flow, &deref->def);

      b.cursor = nir_after_impl(libfunc->impl);
      nir_bindgen_return(&b, nir_load_var(&b, ret));

      /* Remove the first parameter (the return deref), leaving only the true
       * parameters.
       */
      libfunc->num_params--;
      memcpy(libfunc->params, libfunc->params + 1,
             sizeof(libfunc->params[0]) * libfunc->num_params);
   }
}

/* Standard optimization loop */
static void
optimize(nir_shader *nir)
{
   bool progress;
   do {
      progress = false;

      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);

      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_lower_phis_to_scalar, true);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_peephole_select, 64, false, true);
      NIR_PASS(progress, nir, nir_opt_phi_precision);
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      NIR_PASS(progress, nir, nir_opt_deref);
      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);
      NIR_PASS(progress, nir, nir_opt_undef);

      NIR_PASS(progress, nir, nir_opt_loop_unroll);
      NIR_PASS(progress, nir, nir_opt_loop);
   } while (progress);

   NIR_PASS(progress, nir, nir_opt_shrink_vectors, true);
}

static nir_shader *
compile(void *memctx, const uint32_t *spirv, size_t spirv_size)
{
   const nir_shader_compiler_options *nir_options = &generic_opts;

   assert(spirv_size % 4 == 0);
   nir_shader *nir =
      spirv_to_nir(spirv, spirv_size / 4, NULL, 0, MESA_SHADER_KERNEL,
                   "library", &spirv_options, nir_options);
   nir_validate_shader(nir, "after spirv_to_nir");
   ralloc_steal(memctx, nir);

   nir_fixup_is_exported(nir);

   /* At the moment, entrypoints will be compiled to binaries by a different
    * tool, remove them as we are only interested in library functions for
    * bindgen.
    *
    * A future version of vtn_bindgen will handle the entrypoints too.
    */
   nir_remove_entrypoints(nir);

   NIR_PASS(_, nir, nir_lower_system_values);
   NIR_PASS(_, nir, nir_lower_calls_to_builtins);

   nir_lower_compute_system_values_options cs = {.global_id_is_32bit = true};
   NIR_PASS(_, nir, nir_lower_compute_system_values, &cs);

   NIR_PASS(_, nir, nir_lower_printf,
            &(const struct nir_lower_printf_options){
               .hash_format_strings = true,
            });

   /* We have to lower away local constant initializers right before we
    * inline functions.  That way they get properly initialized at the top
    * of the function and not at the top of its caller.
    */
   NIR_PASS(_, nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS(_, nir, nir_lower_returns);
   NIR_PASS(_, nir, nir_inline_functions);
   nir_remove_non_exported(nir);
   NIR_PASS(_, nir, nir_copy_prop);
   NIR_PASS(_, nir, nir_opt_deref);

   /* We can't deal with constant data, get rid of it */
   nir_lower_constant_to_temp(nir);

   /* We can go ahead and lower the rest of the constant initializers.  We do
    * this here so that nir_remove_dead_variables and split_per_member_structs
    * below see the corresponding stores.
    */
   NIR_PASS(_, nir, nir_lower_variable_initializers, ~0);

   /* LLVM loves take advantage of the fact that vec3s in OpenCL are 16B
    * aligned and so it can just read/write them as vec4s.  This results in a
    * LOT of vec4->vec3 casts on loads and stores.  One solution to this
    * problem is to get rid of all vec3 variables.
    */
   NIR_PASS(_, nir, nir_lower_vec3_to_vec4,
            nir_var_shader_temp | nir_var_function_temp | nir_var_mem_shared |
               nir_var_mem_global | nir_var_mem_constant);

   /* Bit more lowering... this doesn't seem to be load-bearing though.. */
   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_split_struct_vars, nir_var_function_temp);
   NIR_PASS(_, nir, nir_lower_var_copies);

   /* We assign explicit types early so that the optimizer can take advantage
    * of that information and hopefully get rid of some of our memcpys.
    */
   NIR_PASS(_, nir, nir_lower_vars_to_explicit_types,
            nir_var_uniform | nir_var_shader_temp | nir_var_function_temp |
               nir_var_mem_shared | nir_var_mem_global,
            glsl_get_cl_type_size_align);

   NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_all, NULL);

   /* Lower again, this time after dead-variables to get more compact variable
    * layouts.
    */
   NIR_PASS(_, nir, nir_lower_vars_to_explicit_types,
            nir_var_shader_temp | nir_var_function_temp | nir_var_mem_shared |
               nir_var_mem_global | nir_var_mem_constant,
            glsl_get_cl_type_size_align);
   assert(nir->constant_data_size == 0);

   NIR_PASS(_, nir, nir_lower_memcpy);

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_constant,
            nir_address_format_64bit_global);

   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_uniform,
            nir_address_format_32bit_offset_as_64bit);

   lower_to_bindgen_return(nir);

   NIR_PASS(_, nir, nir_opt_deref);
   NIR_PASS(_, nir, nir_lower_convert_alu_types, NULL);
   NIR_PASS(_, nir, nir_opt_if, 0);

   optimize(nir);

   /* Now lower returns so we can get rid of derefs */
   NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   NIR_PASS(_, nir, nir_lower_vars_to_explicit_types,
            nir_var_shader_temp | nir_var_function_temp | nir_var_mem_shared |
               nir_var_mem_global | nir_var_mem_constant,
            glsl_get_cl_type_size_align);

   NIR_PASS(_, nir, nir_lower_explicit_io,
            nir_var_shader_temp | nir_var_function_temp | nir_var_mem_shared |
               nir_var_mem_global,
            nir_address_format_62bit_generic);

   /* Try to optimize scratch access, since LLVM loooves its scratch. If this
    * makes progress, we need to lower the results.
    */
   bool scratch_lowered = false;
   NIR_PASS(scratch_lowered, nir, nir_lower_scratch_to_var);
   if (scratch_lowered) {
      NIR_PASS(_, nir, nir_lower_indirect_derefs, nir_var_function_temp, ~0);
   }

   /* Prune derefs/variables late, since scratch lowering leaves dead
    * derefs/variables and there's no point rerunning these passes.
    */
   NIR_PASS(_, nir, nir_remove_dead_derefs);
   NIR_PASS(_, nir, nir_remove_dead_variables,
            nir_var_function_temp | nir_var_shader_temp, NULL);

   /* Do a last round of clean up after the extra lowering */
   NIR_PASS(_, nir, nir_copy_prop);
   NIR_PASS(_, nir, nir_opt_constant_folding);
   NIR_PASS(_, nir, nir_opt_algebraic);
   NIR_PASS(_, nir, nir_opt_cse);
   NIR_PASS(_, nir, nir_opt_dce);

   /* Re-index SSA defs at the very end to make the NIR more legible. This
    * doesn't matter for correctness, but it's polite.
    */
   nir_foreach_function_impl(it, nir) {
      nir_index_ssa_defs(it);
   }

   return nir;
}

static void
print_signature(FILE *fp, nir_function *f)
{
   bool returns = f->pass_flags;
   fprintf(fp, "%s\n", returns ? "nir_def *" : "void");
   fprintf(fp, "%s(nir_builder *b", f->name);

   for (unsigned i = 0; i < f->num_params; ++i) {
      fprintf(fp, ", nir_def *%s", f->params[i].name);
   }

   fprintf(fp, ")");
}

int
main(int argc, char **argv)
{
   if (argc != 4) {
      fprintf(stderr, "Usage: %s [input spir-v] [output .c] [output .h]\n",
              argv[0]);
      return 1;
   }

   const char *infile = argv[1];
   const char *outcfile = argv[2];
   const char *outhfile = argv[3];

   void *mem_ctx = ralloc_context(NULL);

   FILE *fin = fopen(infile, "rb");
   if (!fin) {
      fprintf(stderr, "Failed to open %s\n", infile);
      return 1;
   }

   fseek(fin, 0L, SEEK_END);
   size_t len = ftell(fin);
   rewind(fin);

   uint32_t *map = malloc(ALIGN_POT(len, 4));
   if (!map) {
      fprintf(stderr, "Failed to allocate");
      fclose(fin);
      return 1;
   }

   fread(map, 1, len, fin);
   fclose(fin);

   FILE *fp_c = fopen(outcfile, "w");
   if (!fp_c) {
      fprintf(stderr, "Failed to open %s\n", outcfile);
      free(map);
      return 1;
   }

   FILE *fp_h = fopen(outhfile, "w");
   if (!fp_h) {
      fprintf(stderr, "Failed to open %s\n", outhfile);
      free(map);
      fclose(fp_c);
      return 1;
   }

   glsl_type_singleton_init_or_ref();

   for (unsigned i = 0; i < 2; ++i) {
      FILE *fp = i ? fp_c : fp_h;

      fprintf(fp, "/*\n");
      fprintf(fp, " * Copyright Mesa3D Contributors\n");
      fprintf(fp, " * SPDX-License-Identifier: MIT\n");
      fprintf(fp, " *\n");
      fprintf(fp, " * Autogenerated file, do not edit\n");
      fprintf(fp, " */\n\n");

      if (fp == fp_h) {
         fprintf(fp, "#pragma once\n\n");
      }

      fprintf(fp, "#include \"compiler/nir/nir.h\"\n");
      fprintf(fp, "#include \"compiler/nir/nir_builder.h\"\n\n");
      fprintf(fp, "#include \"util/u_printf.h\"\n\n");

      fprintf(fp, "#ifdef __cplusplus\n");
      fprintf(fp, "extern \"C\" {\n");
      fprintf(fp, "#endif\n");
   }

   nir_shader *nir = compile(mem_ctx, map, len);

   nir_foreach_function(libfunc, nir) {
      bool returns = libfunc->pass_flags;

      /* Declare the function in the generated header */
      print_signature(fp_h, libfunc);
      fprintf(fp_h, ";\n\n");

      /* We don't know where the header will end up on the file system, so we
       * manually declare the signatures.
       */
      print_signature(fp_c, libfunc);
      fprintf(fp_c, ";\n\n");

      print_signature(fp_c, libfunc);
      fprintf(fp_c, "\n{\n");

      struct blob blob;
      blob_init(&blob);
      nir_serialize_function(&blob, libfunc);
      fprintf(fp_c, "   /*\n");
      nir_print_function_body(libfunc->impl, fp_c);
      fprintf(fp_c, "   */\n");
      fprintf(fp_c, "   ");
      nir_precomp_print_blob(fp_c, "impl", "nir", 0,
                             (const uint32_t *)blob.data, blob.size, true);
      blob_finish(&blob);

      if (libfunc->num_params > 0) {
         fprintf(fp_c, "   nir_def *args[%u] = { ", libfunc->num_params);
         for (unsigned a = 0; a < libfunc->num_params; ++a) {
            fprintf(fp_c, "%s%s", a ? ", " : "", libfunc->params[a].name);
         }
         fprintf(fp_c, " };\n");
      }

      fprintf(fp_c, "   ");
      if (returns)
         fprintf(fp_c, "return ");

      fprintf(fp_c,
              "nir_call_serialized(b, impl_0_nir, sizeof(impl_0_nir), %s);",
              libfunc->num_params > 0 ? "args" : "NULL");

      fprintf(fp_c, "\n}\n\n");
   }

   for (unsigned i = 0; i < 2; ++i) {
      FILE *fp = i ? fp_c : fp_h;

      fprintf(fp, "#ifdef __cplusplus\n");
      fprintf(fp, "} /* extern C */\n");
      fprintf(fp, "#endif\n");
   }

   fprintf(fp_c, "struct vtn_bindgen_dummy {\n");
   fprintf(fp_c, "   vtn_bindgen_dummy() {\n");
   fprintf(fp_c, "      /* Format strings:\n");
   fprintf(fp_c, "       *\n");
   for (unsigned i = 0; i < nir->printf_info_count; ++i) {
      u_printf_info *info = &nir->printf_info[i];
      const char *str = info->strings;
      fprintf(fp_c, "       * ");

      for (unsigned j = 0; j < strlen(str); ++j) {
         char c = str[j];
         if (c == '\n')
            fprintf(fp_c, "\\n");
         else if (c == '/' && j && str[j - 1] == '*')
            fprintf(fp_c, "\\/");
         else
            fprintf(fp_c, "%c", c);
      }

      fprintf(fp_c, "\n");
   }
   fprintf(fp_c, "       */\n");

   /* Stuff printf info into Mesa's singleton */
   struct blob blob;
   blob_init(&blob);
   u_printf_serialize_info(&blob, nir->printf_info, nir->printf_info_count);
   nir_precomp_print_blob(fp_c, "printf", "blob", 0,
                          (const uint32_t *)blob.data, blob.size, true);
   blob_finish(&blob);

   fprintf(fp_c, "      u_printf_singleton_init_or_ref();\n");
   fprintf(
      fp_c,
      "      u_printf_singleton_add_serialized((const void*)printf_0_blob, sizeof(printf_0_blob));\n");

   fprintf(fp_c, "   }\n");
   fprintf(fp_c, "\n");
   fprintf(fp_c, "   ~vtn_bindgen_dummy() {\n");
   fprintf(fp_c, "      u_printf_singleton_decref();\n");
   fprintf(fp_c, "   }\n");
   fprintf(fp_c, "};\n");
   fprintf(fp_c, "\n");
   fprintf(fp_c, "static vtn_bindgen_dummy vtn_bindgen_dummy_instance;\n");

   glsl_type_singleton_decref();
   fclose(fp_c);
   fclose(fp_h);
   free(map);
   ralloc_free(mem_ctx);
   return 0;
}
