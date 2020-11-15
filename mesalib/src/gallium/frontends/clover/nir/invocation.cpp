//
// Copyright 2019 Karol Herbst
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//

#include "invocation.hpp"

#include <tuple>

#include "core/device.hpp"
#include "core/error.hpp"
#include "core/module.hpp"
#include "pipe/p_state.h"
#include "util/algorithm.hpp"
#include "util/functional.hpp"

#include <compiler/glsl_types.h>
#include <compiler/nir/nir_builder.h>
#include <compiler/nir/nir_serialize.h>
#include <compiler/spirv/nir_spirv.h>
#include <util/u_math.h>

using namespace clover;

#ifdef HAVE_CLOVER_SPIRV

// Refs and unrefs the glsl_type_singleton.
static class glsl_type_ref {
public:
   glsl_type_ref() {
      glsl_type_singleton_init_or_ref();
   }

   ~glsl_type_ref() {
      glsl_type_singleton_decref();
   }
} glsl_type_ref;

static const nir_shader_compiler_options *
dev_get_nir_compiler_options(const device &dev)
{
   const void *co = dev.get_compiler_options(PIPE_SHADER_IR_NIR);
   return static_cast<const nir_shader_compiler_options*>(co);
}

static void debug_function(void *private_data,
                   enum nir_spirv_debug_level level, size_t spirv_offset,
                   const char *message)
{
   assert(private_data);
   auto r_log = reinterpret_cast<std::string *>(private_data);
   *r_log += message;
}

static void
clover_arg_size_align(const glsl_type *type, unsigned *size, unsigned *align)
{
   if (type == glsl_type::sampler_type) {
      *size = 0;
      *align = 1;
   } else if (type->is_image()) {
      *size = *align = sizeof(cl_mem);
   } else {
      *size = type->cl_size();
      *align = type->cl_alignment();
   }
}

static bool
clover_nir_lower_images(nir_shader *shader)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   ASSERTED int last_loc = -1;
   int num_rd_images = 0, num_wr_images = 0, num_samplers = 0;
   nir_foreach_uniform_variable(var, shader) {
      if (glsl_type_is_image(var->type) || glsl_type_is_sampler(var->type)) {
         /* Assume they come in order */
         assert(var->data.location > last_loc);
         last_loc = var->data.location;
      }

      /* TODO: Constant samplers */
      if (var->type == glsl_bare_sampler_type()) {
         var->data.driver_location = num_samplers++;
      } else if (glsl_type_is_image(var->type)) {
         if (var->data.access & ACCESS_NON_WRITEABLE)
            var->data.driver_location = num_rd_images++;
         else
            var->data.driver_location = num_wr_images++;
      } else {
         /* CL shouldn't have any sampled images */
         assert(!glsl_type_is_sampler(var->type));
      }
   }
   shader->info.num_textures = num_rd_images;
   shader->info.textures_used = (1 << num_rd_images) - 1;
   shader->info.num_images = num_wr_images;

   nir_builder b;
   nir_builder_init(&b, impl);

   bool progress = false;
   nir_foreach_block_reverse(block, impl) {
      nir_foreach_instr_reverse_safe(instr, block) {
         switch (instr->type) {
         case nir_instr_type_deref: {
            nir_deref_instr *deref = nir_instr_as_deref(instr);
            if (deref->deref_type != nir_deref_type_var)
               break;

            if (!glsl_type_is_image(deref->type) &&
                !glsl_type_is_sampler(deref->type))
               break;

            b.cursor = nir_instr_remove(&deref->instr);
            nir_ssa_def *loc =
               nir_imm_intN_t(&b, deref->var->data.driver_location,
                                  deref->dest.ssa.bit_size);
            nir_ssa_def_rewrite_uses(&deref->dest.ssa, nir_src_for_ssa(loc));
            progress = true;
            break;
         }

         case nir_instr_type_tex: {
            nir_tex_instr *tex = nir_instr_as_tex(instr);
            unsigned count = 0;
            for (unsigned i = 0; i < tex->num_srcs; i++) {
               if (tex->src[i].src_type == nir_tex_src_texture_deref ||
                   tex->src[i].src_type == nir_tex_src_sampler_deref) {
                  nir_deref_instr *deref = nir_src_as_deref(tex->src[i].src);
                  if (deref->deref_type == nir_deref_type_var) {
                     /* In this case, we know the actual variable */
                     if (tex->src[i].src_type == nir_tex_src_texture_deref)
                        tex->texture_index = deref->var->data.driver_location;
                     else
                        tex->sampler_index = deref->var->data.driver_location;
                     /* This source gets discarded */
                     nir_instr_rewrite_src(&tex->instr, &tex->src[i].src,
                                           NIR_SRC_INIT);
                     continue;
                  } else {
                     assert(tex->src[i].src.is_ssa);
                     b.cursor = nir_before_instr(&tex->instr);
                     /* Back-ends expect a 32-bit thing, not 64-bit */
                     nir_ssa_def *offset = nir_u2u32(&b, tex->src[i].src.ssa);
                     if (tex->src[i].src_type == nir_tex_src_texture_deref)
                        tex->src[count].src_type = nir_tex_src_texture_offset;
                     else
                        tex->src[count].src_type = nir_tex_src_sampler_offset;
                     nir_instr_rewrite_src(&tex->instr, &tex->src[count].src,
                                           nir_src_for_ssa(offset));
                  }
               } else {
                  /* If we've removed a source, move this one down */
                  if (count != i) {
                     assert(count < i);
                     tex->src[count].src_type = tex->src[i].src_type;
                     nir_instr_move_src(&tex->instr, &tex->src[count].src,
                                        &tex->src[i].src);
                  }
               }
               count++;
            }
            tex->num_srcs = count;
            progress = true;
            break;
         }

         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            switch (intrin->intrinsic) {
            case nir_intrinsic_image_deref_load:
            case nir_intrinsic_image_deref_store:
            case nir_intrinsic_image_deref_atomic_add:
            case nir_intrinsic_image_deref_atomic_imin:
            case nir_intrinsic_image_deref_atomic_umin:
            case nir_intrinsic_image_deref_atomic_imax:
            case nir_intrinsic_image_deref_atomic_umax:
            case nir_intrinsic_image_deref_atomic_and:
            case nir_intrinsic_image_deref_atomic_or:
            case nir_intrinsic_image_deref_atomic_xor:
            case nir_intrinsic_image_deref_atomic_exchange:
            case nir_intrinsic_image_deref_atomic_comp_swap:
            case nir_intrinsic_image_deref_atomic_fadd:
            case nir_intrinsic_image_deref_atomic_inc_wrap:
            case nir_intrinsic_image_deref_atomic_dec_wrap:
            case nir_intrinsic_image_deref_size:
            case nir_intrinsic_image_deref_samples: {
               assert(intrin->src[0].is_ssa);
               b.cursor = nir_before_instr(&intrin->instr);
               /* Back-ends expect a 32-bit thing, not 64-bit */
               nir_ssa_def *offset = nir_u2u32(&b, intrin->src[0].ssa);
               nir_rewrite_image_intrinsic(intrin, offset, false);
               progress = true;
               break;
            }

            default:
               break;
            }
            break;
         }

         default:
            break;
         }
      }
   }

   if (progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);
   } else {
      nir_metadata_preserve(impl, nir_metadata_all);
   }

   return progress;
}

struct clover_lower_nir_state {
   std::vector<module::argument> &args;
   uint32_t global_dims;
   nir_variable *constant_var;
   nir_variable *offset_vars[3];
};

static bool
clover_lower_nir_filter(const nir_instr *instr, const void *)
{
   return instr->type == nir_instr_type_intrinsic;
}

static nir_ssa_def *
clover_lower_nir_instr(nir_builder *b, nir_instr *instr, void *_state)
{
   clover_lower_nir_state *state = reinterpret_cast<clover_lower_nir_state*>(_state);
   nir_intrinsic_instr *intrinsic = nir_instr_as_intrinsic(instr);

   switch (intrinsic->intrinsic) {
   case nir_intrinsic_load_base_global_invocation_id: {
      nir_ssa_def *loads[3];

      /* create variables if we didn't do so alrady */
      if (!state->offset_vars[0]) {
         /* TODO: fix for 64 bit */
         /* Even though we only place one scalar argument, clover will bind up to
          * three 32 bit values
         */
         unsigned location = state->args.size();
         state->args.emplace_back(module::argument::scalar, 4, 4, 4,
                                  module::argument::zero_ext,
                                  module::argument::grid_offset);

         const glsl_type *type = glsl_uint_type();
         for (uint32_t i = 0; i < 3; i++) {
            state->offset_vars[i] =
               nir_variable_create(b->shader, nir_var_uniform, type,
                                   "global_invocation_id_offsets");
            state->offset_vars[i]->data.location = location + i;
         }
      }

      for (int i = 0; i < 3; i++) {
         nir_variable *var = state->offset_vars[i];
         loads[i] = var ? nir_load_var(b, var) : nir_imm_int(b, 0);
      }

      return nir_u2u(b, nir_vec(b, loads, state->global_dims),
                     nir_dest_bit_size(intrinsic->dest));
   }
   case nir_intrinsic_load_constant_base_ptr: {
      return nir_load_var(b, state->constant_var);
   }

   default:
      return NULL;
   }
}

static bool
clover_lower_nir(nir_shader *nir, std::vector<module::argument> &args,
                 uint32_t dims, uint32_t pointer_bit_size)
{
   nir_variable *constant_var = NULL;
   if (nir->constant_data_size) {
      const glsl_type *type = pointer_bit_size == 64 ? glsl_uint64_t_type() : glsl_uint_type();

      constant_var = nir_variable_create(nir, nir_var_uniform, type,
                                         "constant_buffer_addr");
      constant_var->data.location = args.size();

      args.emplace_back(module::argument::global,
                        pointer_bit_size / 8, pointer_bit_size / 8, pointer_bit_size / 8,
                        module::argument::zero_ext,
                        module::argument::constant_buffer);
   }

   clover_lower_nir_state state = { args, dims, constant_var };
   return nir_shader_lower_instructions(nir,
      clover_lower_nir_filter, clover_lower_nir_instr, &state);
}

static spirv_to_nir_options
create_spirv_options(const device &dev, std::string &r_log)
{
   struct spirv_to_nir_options spirv_options = {};
   spirv_options.environment = NIR_SPIRV_OPENCL;
   if (dev.address_bits() == 32u) {
      spirv_options.shared_addr_format = nir_address_format_32bit_offset;
      spirv_options.global_addr_format = nir_address_format_32bit_global;
      spirv_options.temp_addr_format = nir_address_format_32bit_offset;
      spirv_options.constant_addr_format = nir_address_format_32bit_global;
   } else {
      spirv_options.shared_addr_format = nir_address_format_32bit_offset_as_64bit;
      spirv_options.global_addr_format = nir_address_format_64bit_global;
      spirv_options.temp_addr_format = nir_address_format_32bit_offset_as_64bit;
      spirv_options.constant_addr_format = nir_address_format_64bit_global;
   }
   spirv_options.caps.address = true;
   spirv_options.caps.float64 = true;
   spirv_options.caps.int8 = true;
   spirv_options.caps.int16 = true;
   spirv_options.caps.int64 = true;
   spirv_options.caps.kernel = true;
   spirv_options.caps.kernel_image = dev.image_support();
   spirv_options.caps.int64_atomics = dev.has_int64_atomics();
   spirv_options.debug.func = &debug_function;
   spirv_options.debug.private_data = &r_log;
   return spirv_options;
}

struct disk_cache *clover::nir::create_clc_disk_cache(void)
{
   struct mesa_sha1 ctx;
   unsigned char sha1[20];
   char cache_id[20 * 2 + 1];
   _mesa_sha1_init(&ctx);

   if (!disk_cache_get_function_identifier((void *)clover::nir::create_clc_disk_cache, &ctx))
      return NULL;

   _mesa_sha1_final(&ctx, sha1);

   disk_cache_format_hex_id(cache_id, sha1, 20 * 2);
   return disk_cache_create("clover-clc", cache_id, 0);
}

void clover::nir::check_for_libclc(const device &dev)
{
   if (!nir_can_find_libclc(dev.address_bits()))
      throw error(CL_COMPILER_NOT_AVAILABLE);
}

nir_shader *clover::nir::load_libclc_nir(const device &dev, std::string &r_log)
{
   spirv_to_nir_options spirv_options = create_spirv_options(dev, r_log);
   auto *compiler_options = dev_get_nir_compiler_options(dev);

   return nir_load_libclc_shader(dev.address_bits(), dev.clc_cache,
				 &spirv_options, compiler_options);
}

module clover::nir::spirv_to_nir(const module &mod, const device &dev,
                                 std::string &r_log)
{
   spirv_to_nir_options spirv_options = create_spirv_options(dev, r_log);
   std::shared_ptr<nir_shader> nir = dev.clc_nir;
   spirv_options.clc_shader = nir.get();

   module m;
   // We only insert one section.
   assert(mod.secs.size() == 1);
   auto &section = mod.secs[0];

   module::resource_id section_id = 0;
   for (const auto &sym : mod.syms) {
      assert(sym.section == 0);

      const auto *binary =
         reinterpret_cast<const pipe_binary_program_header *>(section.data.data());
      const uint32_t *data = reinterpret_cast<const uint32_t *>(binary->blob);
      const size_t num_words = binary->num_bytes / 4;
      const char *name = sym.name.c_str();
      auto *compiler_options = dev_get_nir_compiler_options(dev);

      nir_shader *nir = spirv_to_nir(data, num_words, nullptr, 0,
                                     MESA_SHADER_KERNEL, name,
                                     &spirv_options, compiler_options);
      if (!nir) {
         r_log += "Translation from SPIR-V to NIR for kernel \"" + sym.name +
                  "\" failed.\n";
         throw build_error();
      }

      nir->info.cs.local_size_variable = sym.reqd_work_group_size[0] == 0;
      nir->info.cs.local_size[0] = sym.reqd_work_group_size[0];
      nir->info.cs.local_size[1] = sym.reqd_work_group_size[1];
      nir->info.cs.local_size[2] = sym.reqd_work_group_size[2];
      nir_validate_shader(nir, "clover");

      // Inline all functions first.
      // according to the comment on nir_inline_functions
      NIR_PASS_V(nir, nir_lower_variable_initializers, nir_var_function_temp);
      NIR_PASS_V(nir, nir_lower_returns);
      NIR_PASS_V(nir, nir_lower_libclc, spirv_options.clc_shader);

      NIR_PASS_V(nir, nir_inline_functions);
      NIR_PASS_V(nir, nir_copy_prop);
      NIR_PASS_V(nir, nir_opt_deref);

      // Pick off the single entrypoint that we want.
      foreach_list_typed_safe(nir_function, func, node, &nir->functions) {
         if (!func->is_entrypoint)
            exec_node_remove(&func->node);
      }
      assert(exec_list_length(&nir->functions) == 1);

      nir_validate_shader(nir, "clover after function inlining");

      NIR_PASS_V(nir, nir_lower_variable_initializers, ~nir_var_function_temp);

      // copy propagate to prepare for lower_explicit_io
      NIR_PASS_V(nir, nir_split_var_copies);
      NIR_PASS_V(nir, nir_opt_copy_prop_vars);
      NIR_PASS_V(nir, nir_lower_var_copies);
      NIR_PASS_V(nir, nir_lower_vars_to_ssa);
      NIR_PASS_V(nir, nir_opt_dce);

      NIR_PASS_V(nir, nir_lower_convert_alu_types, NULL);

      NIR_PASS_V(nir, nir_lower_system_values);
      nir_lower_compute_system_values_options sysval_options = { 0 };
      sysval_options.has_base_global_invocation_id = true;
      NIR_PASS_V(nir, nir_lower_compute_system_values, &sysval_options);

      // constant fold before lowering mem constants
      NIR_PASS_V(nir, nir_opt_constant_folding);

      NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_mem_constant, NULL);
      NIR_PASS_V(nir, nir_lower_mem_constant_vars,
                 glsl_get_cl_type_size_align);
      NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_constant,
                 spirv_options.constant_addr_format);

      auto args = sym.args;
      NIR_PASS_V(nir, clover_lower_nir, args, dev.max_block_size().size(),
                 dev.address_bits());

      NIR_PASS_V(nir, nir_lower_vars_to_explicit_types,
                 nir_var_uniform, clover_arg_size_align);
      NIR_PASS_V(nir, nir_lower_vars_to_explicit_types,
                 nir_var_mem_shared | nir_var_mem_global |
                 nir_var_function_temp,
                 glsl_get_cl_type_size_align);

      NIR_PASS_V(nir, nir_opt_deref);
      NIR_PASS_V(nir, nir_lower_cl_images_to_tex);
      NIR_PASS_V(nir, clover_nir_lower_images);
      NIR_PASS_V(nir, nir_lower_memcpy);

      /* use offsets for kernel inputs (uniform) */
      NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_uniform,
                 nir->info.cs.ptr_size == 64 ?
                 nir_address_format_32bit_offset_as_64bit :
                 nir_address_format_32bit_offset);

      NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_constant,
                 spirv_options.constant_addr_format);
      NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_shared,
                 spirv_options.shared_addr_format);

      NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_function_temp,
                 spirv_options.temp_addr_format);

      NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_global,
                 spirv_options.global_addr_format);

      NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_all, NULL);

      if (compiler_options->lower_int64_options)
         NIR_PASS_V(nir, nir_lower_int64);

      NIR_PASS_V(nir, nir_opt_dce);

      if (nir->constant_data_size) {
         const char *ptr = reinterpret_cast<const char *>(nir->constant_data);
         const module::section constants {
            section_id,
            module::section::data_constant,
            nir->constant_data_size,
            { ptr, ptr + nir->constant_data_size }
         };
         nir->constant_data = NULL;
         nir->constant_data_size = 0;
         m.secs.push_back(constants);
      }

      struct blob blob;
      blob_init(&blob);
      nir_serialize(&blob, nir, false);

      ralloc_free(nir);

      const pipe_binary_program_header header { uint32_t(blob.size) };
      module::section text { section_id, module::section::text_executable, header.num_bytes, {} };
      text.data.insert(text.data.end(), reinterpret_cast<const char *>(&header),
                       reinterpret_cast<const char *>(&header) + sizeof(header));
      text.data.insert(text.data.end(), blob.data, blob.data + blob.size);

      free(blob.data);

      m.syms.emplace_back(sym.name, std::string(),
                          sym.reqd_work_group_size, section_id, 0, args);
      m.secs.push_back(text);
      section_id++;
   }
   return m;
}
#else
module clover::nir::spirv_to_nir(const module &mod, const device &dev, std::string &r_log)
{
   r_log += "SPIR-V support in clover is not enabled.\n";
   throw error(CL_LINKER_NOT_AVAILABLE);
}
#endif
