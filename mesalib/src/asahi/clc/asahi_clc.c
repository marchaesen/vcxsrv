/*
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2020 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "asahi_clc.h"
#include "asahi/compiler/agx_compile.h"
#include "asahi/compiler/agx_nir.h"
#include "compiler/glsl_types.h"
#include "compiler/spirv/nir_spirv.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_builder_opcodes.h"
#include "nir_intrinsics.h"
#include "nir_precompiled.h"
#include "shader_enums.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "util/macros.h"
#include <sys/mman.h>

const char *targets[] = {"g13g", "g13x"};

#define foreach_target(target)                                                 \
   for (const char **target = &targets[0];                                     \
        target < &targets[ARRAY_SIZE(targets)]; ++target)

static const struct spirv_to_nir_options spirv_options = {
   .environment = NIR_SPIRV_OPENCL,
   .shared_addr_format = nir_address_format_62bit_generic,
   .global_addr_format = nir_address_format_62bit_generic,
   .temp_addr_format = nir_address_format_62bit_generic,
   .constant_addr_format = nir_address_format_64bit_global,
   .create_library = true,
   .printf = true,
};

/* Standard optimization loop */
static void
optimize(nir_shader *nir)
{
   bool progress;
   do {
      progress = false;

      NIR_PASS(progress, nir, nir_split_var_copies);
      NIR_PASS(progress, nir, nir_split_struct_vars, nir_var_function_temp);
      NIR_PASS(progress, nir, nir_lower_var_copies);
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
      NIR_PASS(progress, nir, nir_lower_undef_to_zero);

      NIR_PASS(progress, nir, nir_opt_shrink_vectors, true);
      NIR_PASS(progress, nir, nir_opt_loop_unroll);

   } while (progress);
}

static nir_shader *
compile(void *memctx, const uint32_t *spirv, size_t spirv_size)
{
   const nir_shader_compiler_options *nir_options = &agx_nir_options;

   assert(spirv_size % 4 == 0);
   nir_shader *nir =
      spirv_to_nir(spirv, spirv_size / 4, NULL, 0, MESA_SHADER_KERNEL,
                   "library", &spirv_options, nir_options);
   nir_validate_shader(nir, "after spirv_to_nir");
   ralloc_steal(memctx, nir);

   nir_fixup_is_exported(nir);

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

   /* We assign explicit types early so that the optimizer can take advantage
    * of that information and hopefully get rid of some of our memcpys.
    */
   NIR_PASS(_, nir, nir_lower_vars_to_explicit_types,
            nir_var_uniform | nir_var_shader_temp | nir_var_function_temp |
               nir_var_mem_shared | nir_var_mem_global,
            glsl_get_cl_type_size_align);

   optimize(nir);

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

   /* Note: we cannot lower explicit I/O here, because we need derefs in tact
    * for function calls into the library to work.
    */

   NIR_PASS(_, nir, nir_lower_convert_alu_types, NULL);
   NIR_PASS(_, nir, nir_opt_if, 0);
   NIR_PASS(_, nir, nir_opt_idiv_const, 16);

   NIR_PASS(_, nir, agx_nir_lower_texture_early, false /* support_lod_bias */);
   NIR_PASS(_, nir, agx_nir_lower_texture);
   NIR_PASS(_, nir, agx_nir_lower_multisampled_image_store);

   optimize(nir);

   return nir;
}

static void
print_shader(FILE *fp, const char *name, const char *suffix, uint32_t variant,
             struct agx_shader_part *p)
{
   struct agx_precompiled_kernel_info info = agx_compact_kernel_info(&p->info);
   size_t sz_B = sizeof(info) + p->info.binary_size;
   size_t sz_el = DIV_ROUND_UP(sz_B, 4);
   uint32_t *mem = calloc(sz_el, 4);

   memcpy(mem, &info, sizeof(info));
   memcpy((uint8_t *)mem + sizeof(info), p->binary, p->info.binary_size);

   nir_precomp_print_blob(fp, name, suffix, variant, mem, sz_B, true);
   free(mem);
}

static bool
gather_atomic_info(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   bool *any = data;

   switch (intr->intrinsic) {
   case nir_intrinsic_global_atomic:
   case nir_intrinsic_global_atomic_agx:
   case nir_intrinsic_deref_atomic:
   case nir_intrinsic_global_atomic_swap:
   case nir_intrinsic_global_atomic_swap_agx:
   case nir_intrinsic_deref_atomic_swap:
      *any = true;
      return false;
   default:
      return false;
   }
}

/* G13X variants are only compiled when atomics are used */
static const char *
remap_variant(nir_function *func, unsigned variant, const char *target)
{
   bool has_atomic = func->pass_flags & BITFIELD_BIT(variant);

   if (!has_atomic && !strcmp(target, "g13x"))
      return "g13g";
   else
      return target;
}

static nir_def *
load_kernel_input(nir_builder *b, unsigned num_components, unsigned bit_size,
                  unsigned offset_B)
{
   assert((offset_B & 1) == 0 && "half-aligned");
   return nir_load_preamble(b, num_components, bit_size, .base = offset_B / 2);
}

int
main(int argc, char **argv)
{
   if (argc != 4) {
      fprintf(stderr, "Usage: %s [input spir-v] [output header] [output C]\n",
              argv[0]);
      return 1;
   }

   const char *infile = argv[1];
   const char *outh_file = argv[2];
   const char *outc_file = argv[3];

   void *mem_ctx = ralloc_context(NULL);

   int fd = open(infile, O_RDONLY);
   if (fd < 0) {
      fprintf(stderr, "Failed to open %s\n", infile);
      ralloc_free(mem_ctx);
      return 1;
   }

   off_t spirv_len = lseek(fd, 0, SEEK_END);
   const void *spirv_map = mmap(NULL, spirv_len, PROT_READ, MAP_PRIVATE, fd, 0);
   close(fd);
   if (spirv_map == MAP_FAILED) {
      fprintf(stderr, "Failed to mmap the file: errno=%d, %s\n", errno,
              strerror(errno));
      ralloc_free(mem_ctx);
      return 1;
   }

   FILE *fp_h = fopen(outh_file, "w");
   FILE *fp_c = fopen(outc_file, "w");
   glsl_type_singleton_init_or_ref();

   nir_precomp_print_header(fp_c, fp_h, "The Asahi Linux Contributors",
                            "libagx_shaders.h");

   nir_shader *nir = compile(mem_ctx, spirv_map, spirv_len);

   /* load_preamble works at 16-bit granularity */
   struct nir_precomp_opts opt = {.arg_align_B = 2};

   nir_foreach_entrypoint(libfunc, nir) {
      libfunc->pass_flags = 0;
      struct nir_precomp_layout layout =
         nir_precomp_derive_layout(&opt, libfunc);
      unsigned nr_vars = nir_precomp_nr_variants(libfunc);

      nir_precomp_print_layout_struct(fp_h, &opt, libfunc);

      for (unsigned v = 0; v < nr_vars; ++v) {
         nir_shader *s = nir_precompiled_build_variant(
            libfunc, v, &agx_nir_options, &opt, load_kernel_input);

         nir_link_shader_functions(s, nir);
         NIR_PASS(_, s, nir_inline_functions);
         nir_remove_non_entrypoints(s);
         NIR_PASS(_, s, nir_opt_deref);
         NIR_PASS(_, s, nir_lower_vars_to_ssa);
         NIR_PASS(_, s, nir_remove_dead_derefs);
         NIR_PASS(_, s, nir_remove_dead_variables,
                  nir_var_function_temp | nir_var_shader_temp, NULL);
         NIR_PASS(_, s, nir_lower_vars_to_explicit_types,
                  nir_var_shader_temp | nir_var_function_temp,
                  glsl_get_cl_type_size_align);

         NIR_PASS(_, s, nir_lower_vars_to_explicit_types, nir_var_mem_shared,
                  glsl_get_cl_type_size_align);

         NIR_PASS(_, s, nir_lower_explicit_io, nir_var_mem_shared,
                  nir_address_format_62bit_generic);

         /* Unroll loops before lowering indirects */
         bool progress = false;
         do {
            progress = false;
            NIR_PASS(progress, s, nir_opt_loop);
         } while (progress);

         agx_preprocess_nir(s);

         NIR_PASS(_, s, nir_opt_deref);
         NIR_PASS(_, s, nir_lower_vars_to_ssa);
         NIR_PASS(_, s, nir_lower_explicit_io,
                  nir_var_shader_temp | nir_var_function_temp |
                     nir_var_mem_shared | nir_var_mem_global,
                  nir_address_format_62bit_generic);

         bool has_atomic = false;
         nir_shader_intrinsics_pass(s, gather_atomic_info, nir_metadata_all,
                                    &has_atomic);
         if (has_atomic) {
            libfunc->pass_flags |= BITFIELD_BIT(v);
         }

         foreach_target(target)
         {
            /* Skip unused variants */
            if (strcmp(*target, remap_variant(libfunc, v, *target)))
               continue;

            struct agx_shader_part compiled;
            bool is_helper = !strcmp(libfunc->name, "libagx_helper");
            struct agx_shader_key key = {
               .promote_constants = !is_helper,
               .reserved_preamble = layout.size_B / 2,
               .is_helper = is_helper,
            };

            if (has_atomic) {
               key.dev.needs_g13x_coherency =
                  u_tristate_make(!strcmp(*target, "g13x"));
            }

            nir_shader *clone = nir_shader_clone(NULL, s);
            agx_compile_shader_nir(clone, &key, NULL, &compiled);
            print_shader(fp_c, libfunc->name, *target, v, &compiled);
            free(compiled.binary);
            ralloc_free(clone);

            assert(compiled.info.scratch_size == 0 &&
                   "internal shaders do not spill");

            assert(compiled.info.preamble_scratch_size == 0 &&
                   "internal shader preambles do not spill");
         }

         ralloc_free(s);
      }
   }

   nir_precomp_print_program_enum(fp_h, nir, "libagx");
   nir_precomp_print_dispatch_macros(fp_h, &opt, nir);

   /* For each target, generate a table mapping programs to binaries */
   foreach_target(target)
   {
      nir_precomp_print_extern_binary_map(fp_h, "libagx", *target);
      nir_precomp_print_binary_map(fp_c, nir, "libagx", *target, remap_variant);
   }

   glsl_type_singleton_decref();
   fclose(fp_c);
   fclose(fp_h);
   ralloc_free(mem_ctx);
   return 0;
}
