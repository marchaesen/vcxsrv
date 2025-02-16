/*
 * Copyright 2024 Collabora Ltd
 * Copyright 2023 Alyssa Rosenzweig
 * Copyright 2020 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "panfrost_compile.h"
#include "compiler/glsl_types.h"
#include "compiler/spirv/nir_spirv.h"
#include "panfrost/compiler/bifrost_compile.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_builder_opcodes.h"
#include "nir_intrinsics.h"
#include "nir_precompiled.h"
#include "pan_shader.h"
#include "shader_enums.h"

#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "panfrost/util/pan_ir.h"
#include "util/macros.h"
#include "util/u_dynarray.h"
#include <sys/mman.h>
#include "panfrost_compile.h"

static const struct spirv_to_nir_options spirv_options = {
   .environment = NIR_SPIRV_OPENCL,
   .shared_addr_format = nir_address_format_62bit_generic,
   .global_addr_format = nir_address_format_62bit_generic,
   .temp_addr_format = nir_address_format_62bit_generic,
   .constant_addr_format = nir_address_format_64bit_global,
   .create_library = true,
   .printf = true,
};

static const nir_shader_compiler_options *
get_compiler_options(unsigned arch)
{
   if (arch >= 9)
      return &bifrost_nir_options_v9;

   return &bifrost_nir_options_v6;
}

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
compile(void *memctx, const uint32_t *spirv, size_t spirv_size, unsigned arch)
{
   const nir_shader_compiler_options *nir_options = get_compiler_options(arch);

   nir_shader *nir =
      spirv_to_nir(spirv, spirv_size / 4, NULL, 0, MESA_SHADER_KERNEL,
                   "library", &spirv_options, nir_options);
   nir_validate_shader(nir, "after spirv_to_nir");
   nir_validate_ssa_dominance(nir, "after spirv_to_nir");
   ralloc_steal(memctx, nir);

   nir_fixup_is_exported(nir);

   NIR_PASS(_, nir, nir_lower_system_values);
   NIR_PASS(_, nir, nir_lower_calls_to_builtins);

   nir_lower_compute_system_values_options cs = {.global_id_is_32bit = true};
   NIR_PASS(_, nir, nir_lower_compute_system_values, &cs);

   NIR_PASS(_, nir, nir_lower_printf,
            &(const struct nir_lower_printf_options){
               .max_buffer_size = LIBPAN_PRINTF_BUFFER_SIZE - 8,
               .ptr_bit_size = 64,
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

   NIR_PASS(_, nir, nir_lower_convert_alu_types, NULL);
   NIR_PASS(_, nir, nir_opt_if, 0);
   NIR_PASS(_, nir, nir_opt_idiv_const, 16);

   /* Lower explicit IO here to ensure that we will not clash with different
    * address formats inside shaders */
   NIR_PASS(_, nir, nir_opt_deref);
   NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   NIR_PASS(_, nir, nir_lower_explicit_io,
            nir_var_shader_temp | nir_var_function_temp | nir_var_mem_shared |
               nir_var_mem_global,
            nir_address_format_62bit_generic);

   optimize(nir);

   return nir;
}

static nir_def *
load_sysval_from_push_const(nir_builder *b, unsigned offset, unsigned bit_size,
                            unsigned num_comps)
{
   return nir_load_push_constant(
      b, num_comps, bit_size,
      nir_imm_int(b, BIFROST_PRECOMPILED_KERNEL_SYSVALS_OFFSET + offset));
}

static bool
lower_sysvals(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *_data)
{
   const nir_shader *shader = b->shader;

   unsigned num_comps = intr->def.num_components;
   unsigned bit_size = intr->def.bit_size;
   nir_def *val = NULL;
   b->cursor = nir_before_instr(&intr->instr);

   switch (intr->intrinsic) {
   case nir_intrinsic_load_base_workgroup_id:
      /* The base is always 0 */
      val = nir_imm_zero(b, num_comps, bit_size);
      break;
   case nir_intrinsic_load_workgroup_size:
      /* We are never expecting the local size to be variable */
      assert(!shader->info.workgroup_size_variable);
      val = nir_vec3(b, nir_imm_int(b, shader->info.workgroup_size[0]),
                     nir_imm_int(b, shader->info.workgroup_size[1]),
                     nir_imm_int(b, shader->info.workgroup_size[2]));
      break;

   case nir_intrinsic_load_num_workgroups:
      val = load_sysval_from_push_const(
         b, offsetof(struct bifrost_precompiled_kernel_sysvals, num_workgroups),
         bit_size, num_comps);
      break;

   case nir_intrinsic_load_printf_buffer_address:
      val = load_sysval_from_push_const(
         b,
         offsetof(struct bifrost_precompiled_kernel_sysvals,
                  printf_buffer_address),
         bit_size, num_comps);
      break;

   default:
      return false;
   }

   b->cursor = nir_after_instr(&intr->instr);
   nir_def_replace(&intr->def, val);
   return true;
}

static void
print_shader(FILE *fp, const char *name, const char *suffix, uint32_t variant,
             nir_shader *nir, struct pan_shader_info *shader_info,
             struct util_dynarray *binary)
{
   struct bifrost_precompiled_kernel_info info =
      bifrost_precompiled_pack_kernel_info(nir, shader_info, binary);
   size_t sz_B = sizeof(info) + binary->size;
   size_t sz_el = DIV_ROUND_UP(sz_B, 4);
   uint32_t *mem = calloc(sz_el, 4);

   memcpy(mem, &info, sizeof(info));
   memcpy((uint8_t *)mem + sizeof(info), binary->data, binary->size);

   nir_precomp_print_blob(fp, name, suffix, variant, mem, sz_B, true);
   free(mem);
}

static nir_def *
load_kernel_input(nir_builder *b, unsigned num_components, unsigned bit_size,
                  unsigned offset_B)
{
   return nir_load_push_constant(
      b, num_components, bit_size,
      nir_imm_int(b, BIFROST_PRECOMPILED_KERNEL_ARGS_OFFSET + offset_B));
}

/* Always assume default as we generate per gen already */
static const char *
remap_variant(nir_function *func, unsigned variant, const char *target)
{
   return "default";
}

void pan_shader_compile_v6(nir_shader *nir,
                           struct panfrost_compile_inputs *inputs,
                           struct util_dynarray *binary,
                           struct pan_shader_info *info);

void pan_shader_compile_v7(nir_shader *nir,
                           struct panfrost_compile_inputs *inputs,
                           struct util_dynarray *binary,
                           struct pan_shader_info *info);

void pan_shader_compile_v9(nir_shader *nir,
                           struct panfrost_compile_inputs *inputs,
                           struct util_dynarray *binary,
                           struct pan_shader_info *info);

void pan_shader_compile_v10(nir_shader *nir,
                            struct panfrost_compile_inputs *inputs,
                            struct util_dynarray *binary,
                            struct pan_shader_info *info);

static void
shader_compile(int arch, nir_shader *nir,
               struct panfrost_compile_inputs *inputs,
               struct util_dynarray *binary, struct pan_shader_info *info)
{
   switch (arch) {
   case 6:
      pan_shader_compile_v6(nir, inputs, binary, info);
      break;
   case 7:
      pan_shader_compile_v7(nir, inputs, binary, info);
      break;
   case 9:
      pan_shader_compile_v9(nir, inputs, binary, info);
      break;
   case 10:
      pan_shader_compile_v10(nir, inputs, binary, info);
      break;
   default:
      unreachable("Unknown arch!");
   }
}

int
main(int argc, const char **argv)
{
   if (argc != 6) {
      fprintf(
         stderr,
         "Usage: %s [library name] [arch] [input spir-v] [output header] [output C]\n",
         argv[0]);
      return 1;
   }

   const char *library_name = argv[1];
   const char *target_arch_str = argv[2];
   const char *input_spirv_path = argv[3];
   const char *output_h_path = argv[4];
   const char *output_c_path = argv[5];

   int target_arch = atoi(target_arch_str);

   if (target_arch < 4 || target_arch > 10) {
      fprintf(stderr, "Unsupported target arch %d\n", target_arch);
      return 1;
   }

   void *mem_ctx = ralloc_context(NULL);
   if (mem_ctx == NULL) {
      fprintf(stderr, "mem_ctx allocation failed\n");
      goto err_out;
   }

   int fd = open(input_spirv_path, O_RDONLY);
   if (fd < 0) {
      fprintf(stderr, "Failed to open %s\n", input_spirv_path);
      goto input_spirv_open_failed;
   }

   off_t spirv_len = lseek(fd, 0, SEEK_END);
   const void *spirv_map = mmap(NULL, spirv_len, PROT_READ, MAP_PRIVATE, fd, 0);
   close(fd);

   if (spirv_map == MAP_FAILED) {
      fprintf(stderr, "Failed to mmap the file: errno=%d, %s\n", errno,
              strerror(errno));
      goto input_spirv_open_failed;
   }

   FILE *fp_h = fopen(output_h_path, "w");
   if (fp_h == NULL) {
      fprintf(stderr, "Failed to open %s for writting\n", output_h_path);
      goto input_spirv_open_failed;
   }

   FILE *fp_c = fopen(output_c_path, "w");
   if (fp_c == NULL) {
      fprintf(stderr, "Failed to open %s for writting\n", output_c_path);
      goto fp_c_open_failed;
   }

   glsl_type_singleton_init_or_ref();

   /* POSIX basename can modify the content of the path */
   char *tmp_out_h_path = strdup(output_h_path);
   const char *output_h_file_name = basename(tmp_out_h_path);
   nir_precomp_print_header(fp_c, fp_h, "Collabora Ltd", output_h_file_name);
   free(tmp_out_h_path);

   nir_shader *nir = compile(mem_ctx, spirv_map, spirv_len, target_arch);

   /* load_preamble works at 32-bit granularity */
   struct nir_precomp_opts opt = {.arg_align_B = 4};

   nir_foreach_entrypoint(libfunc, nir) {
      if (target_arch < 6) {
         fprintf(
            stderr,
            "ERROR: Attempting to compile entrypoint %s on Midgard, this is unsupported!\n",
            libfunc->name);
         goto invalid_precomp;
      }

      unsigned nr_vars = nir_precomp_nr_variants(libfunc);

      nir_precomp_print_layout_struct(fp_h, &opt, libfunc);

      for (unsigned v = 0; v < nr_vars; ++v) {
         nir_shader *s = nir_precompiled_build_variant(
            libfunc, v, get_compiler_options(target_arch), &opt,
            load_kernel_input);

         /* Because we do nir_lower_explicit_io on temp variable early on, we
          * lose the scratch_size when we build the shader variant so we need
          * to readjust it here. */
         s->scratch_size = MAX2(s->scratch_size, nir->scratch_size);

         struct panfrost_compile_inputs inputs = {
            .gpu_id = target_arch << 12,
            .no_ubo_to_push = true,
         };

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

         /* Unroll loops before lowering indirects */
         bool progress = false;
         do {
            progress = false;
            NIR_PASS(progress, s, nir_opt_loop);
         } while (progress);

         pan_shader_preprocess(s, inputs.gpu_id);

         NIR_PASS(_, s, nir_opt_deref);
         NIR_PASS(_, s, nir_lower_vars_to_ssa);
         NIR_PASS(_, s, nir_lower_explicit_io,
                  nir_var_shader_temp | nir_var_function_temp |
                     nir_var_mem_shared | nir_var_mem_global,
                  nir_address_format_62bit_generic);

         NIR_PASS(_, s, nir_shader_intrinsics_pass, lower_sysvals,
                  nir_metadata_control_flow, NULL);

         nir_shader *clone = nir_shader_clone(NULL, s);

         struct util_dynarray shader_binary;
         struct pan_shader_info shader_info = {0};
         util_dynarray_init(&shader_binary, NULL);
         shader_compile(target_arch, clone, &inputs, &shader_binary,
                        &shader_info);

         assert(shader_info.push.count * 4 <=
                   BIFROST_PRECOMPILED_KERNEL_ARGS_SIZE &&
                "Too many kernel arguments!");

         print_shader(fp_c, libfunc->name, "default", v, s, &shader_info,
                      &shader_binary);
         util_dynarray_fini(&shader_binary);
         ralloc_free(clone);

         ralloc_free(s);
      }
   }

   nir_precomp_print_program_enum(fp_h, nir, library_name);
   nir_precomp_print_dispatch_macros(fp_h, &opt, nir);

   char target_name[12];
   snprintf(target_name, sizeof(target_name), "default_v%d", target_arch);
   nir_precomp_print_extern_binary_map(fp_h, library_name, target_name);
   nir_precomp_print_binary_map(fp_c, nir, library_name, target_name,
                                remap_variant);

   glsl_type_singleton_decref();
   fclose(fp_c);
   fclose(fp_h);
   ralloc_free(mem_ctx);

   return 0;

invalid_precomp:
   glsl_type_singleton_decref();
fp_c_open_failed:
   fclose(fp_h);
input_spirv_open_failed:
   ralloc_free(mem_ctx);
err_out:
   return 1;
}
