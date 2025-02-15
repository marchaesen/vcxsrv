/*
 * Copyright © 2020 Microsoft Corporation
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

#include "nir.h"
#include "nir_builder.h"
#include "nir_builder_opcodes.h"

#include "util/u_math.h"
#include "util/u_printf.h"

static bool
lower_printf_intrin(nir_builder *b, nir_intrinsic_instr *prntf, void *_options)
{
   const nir_lower_printf_options *options = _options;
   if (prntf->intrinsic != nir_intrinsic_printf &&
       prntf->intrinsic != nir_intrinsic_printf_abort)
      return false;

   b->cursor = nir_before_instr(&prntf->instr);

   const unsigned ptr_bit_size =
      options->ptr_bit_size != 0 ? options->ptr_bit_size : nir_get_ptr_bitsize(b->shader);

   nir_def *buffer_addr = nir_load_printf_buffer_address(b, ptr_bit_size);

   /* For aborts, just write a nonzero value to the aborted? flag. The printf
    * buffer layout looks like:
    *
    *    uint32_t size;
    *    uint32_t aborted;
    *    uint32_t data[];
    */
   if (prntf->intrinsic == nir_intrinsic_printf_abort) {
      nir_store_global(b, nir_iadd_imm(b, buffer_addr, 4), 4, nir_imm_int(b, 1),
                       nir_component_mask(1));

      /* Halt is a jump instruction so can only appear at the end of a block.
       * The abort might be in the middle of a block. So, wrap the halt and let
       * control flow optimization clean up after us.
       */
      nir_push_if(b, nir_imm_true(b));
      {
         nir_jump(b, nir_jump_halt);
      }
      nir_pop_if(b, NULL);

      nir_instr_remove(&prntf->instr);
      return true;
   }

   nir_def *fmt_str_id = prntf->src[0].ssa;
   if (options->hash_format_strings) {
      /* Rather than store the index of the format string, instead store the
       * hash of the format string itself. This is invariant across shaders
       * which may be more convenient.
       */
      unsigned idx = nir_src_as_uint(prntf->src[0]) - 1;
      assert(idx < b->shader->printf_info_count && "must be in-bounds");

      uint32_t hash = u_printf_hash(&b->shader->printf_info[idx]);
      fmt_str_id = nir_imm_int(b, hash);
   }

   nir_deref_instr *args = nir_src_as_deref(prntf->src[1]);
   assert(args->deref_type == nir_deref_type_var);

   /* Atomic add a buffer size counter to determine where to write.  If
    * overflowed, return -1, otherwise, store the arguments and return 0.
    */
   nir_deref_instr *buffer =
      nir_build_deref_cast(b, buffer_addr, nir_var_mem_global,
                           glsl_array_type(glsl_uint8_t_type(), 0, 4), 0);

   /* Align the struct size to 4 */
   assert(glsl_type_is_struct_or_ifc(args->type));
   int args_size = align(glsl_get_cl_size(args->type), 4);
   assert(fmt_str_id->bit_size == 32);
   int fmt_str_id_size = 4;

   /* Increment the counter at the beginning of the buffer */
   const unsigned counter_size = 4;
   nir_deref_instr *counter = nir_build_deref_array_imm(b, buffer, 0);
   counter = nir_build_deref_cast(b, &counter->def,
                                  nir_var_mem_global,
                                  glsl_uint_type(), 0);
   counter->cast.align_mul = 4;
   nir_def *offset =
      nir_deref_atomic(b, 32, &counter->def,
                       nir_imm_int(b, fmt_str_id_size + args_size),
                       .atomic_op = nir_atomic_op_iadd);

   /* Check if we're still in-bounds */
   nir_def *buffer_size;
   if (options->max_buffer_size) {
      buffer_size = nir_imm_int(b, options->max_buffer_size);
   } else {
      buffer_size = nir_load_printf_buffer_size(b);
   }

   unsigned this_printf_size = args_size + fmt_str_id_size + counter_size;
   nir_push_if(b, nir_ult(b, offset, nir_iadd_imm(b, buffer_size, -this_printf_size)));

   nir_def *printf_succ_val = nir_imm_int(b, 0);

   offset = nir_u2uN(b, offset, ptr_bit_size);

   /* Write the format string ID */
   nir_deref_instr *fmt_str_id_deref = nir_build_deref_array(b, buffer, offset);
   fmt_str_id_deref = nir_build_deref_cast(b, &fmt_str_id_deref->def,
                                           nir_var_mem_global,
                                           glsl_uint_type(), 0);
   fmt_str_id_deref->cast.align_mul = 4;
   nir_store_deref(b, fmt_str_id_deref, fmt_str_id, ~0);

   /* Write the format args */
   for (unsigned i = 0; i < glsl_get_length(args->type); ++i) {
      nir_deref_instr *arg_deref = nir_build_deref_struct(b, args, i);
      nir_def *arg = nir_load_deref(b, arg_deref);
      const struct glsl_type *arg_type = arg_deref->type;

      unsigned field_offset = glsl_get_struct_field_offset(args->type, i);
      nir_def *arg_offset =
         nir_iadd_imm(b, offset, fmt_str_id_size + field_offset);
      nir_deref_instr *dst_arg_deref =
         nir_build_deref_array(b, buffer, arg_offset);
      dst_arg_deref = nir_build_deref_cast(b, &dst_arg_deref->def,
                                           nir_var_mem_global, arg_type, 0);
      assert(field_offset % 4 == 0);
      dst_arg_deref->cast.align_mul = 4;
      nir_store_deref(b, dst_arg_deref, arg, ~0);
   }

   nir_push_else(b, NULL);
   nir_def *printf_fail_val = nir_imm_int(b, -1);
   nir_pop_if(b, NULL);

   nir_def *ret_val = nir_if_phi(b, printf_succ_val, printf_fail_val);
   nir_def_replace(&prntf->def, ret_val);

   return true;
}

bool
nir_lower_printf(nir_shader *nir, const nir_lower_printf_options *options)
{
   return nir_shader_intrinsics_pass(nir, lower_printf_intrin,
                                     nir_metadata_none,
                                     (void *)options);
}

struct buffer_opts {
   uint64_t address;
   uint32_t size;
};

static bool
lower_printf_buffer(nir_builder *b, nir_intrinsic_instr *intr, void *_options)
{
   const struct buffer_opts *options = _options;

   uint64_t value = 0;
   if (intr->intrinsic == nir_intrinsic_load_printf_buffer_address)
      value = options->address;
   else if (intr->intrinsic == nir_intrinsic_load_printf_buffer_size)
      value = options->size;

   if (value == 0)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_def_replace(&intr->def, nir_imm_intN_t(b, value, intr->def.bit_size));
   return true;
}

bool
nir_lower_printf_buffer(nir_shader *nir, uint64_t address, uint32_t size)
{
   struct buffer_opts opts = { .address = address, .size = size };

   return nir_shader_intrinsics_pass(nir, lower_printf_buffer,
                                     nir_metadata_control_flow, &opts);
}

void
nir_printf_fmt(nir_builder *b, unsigned ptr_bit_size, const char *fmt, ...)
{
   u_printf_info info = {
      .strings = ralloc_strdup(b->shader, fmt),
      .string_size = strlen(fmt) + 1,
   };

   va_list ap;
   size_t pos = 0;
   size_t args_size = 0;

   va_start(ap, fmt);
   while ((pos = util_printf_next_spec_pos(fmt, pos)) != -1) {
      unsigned arg_size;
      switch (fmt[pos]) {
      case 'c': arg_size = 1; break;
      case 'd': arg_size = 4; break;
      case 'e': arg_size = 4; break;
      case 'E': arg_size = 4; break;
      case 'f': arg_size = 4; break;
      case 'F': arg_size = 4; break;
      case 'G': arg_size = 4; break;
      case 'a': arg_size = 4; break;
      case 'A': arg_size = 4; break;
      case 'i': arg_size = 4; break;
      case 'u': arg_size = 4; break;
      case 'x': arg_size = 4; break;
      case 'X': arg_size = 4; break;
      case 'p': arg_size = 8; break;
      default:  unreachable("invalid");
      }

      ASSERTED nir_def *def = va_arg(ap, nir_def*);
      assert(def->bit_size / 8 == arg_size);

      info.num_args++;
      info.arg_sizes = reralloc(b->shader, info.arg_sizes, unsigned,
                                info.num_args);
      info.arg_sizes[info.num_args - 1] = arg_size;

      args_size += arg_size;
   }
   va_end(ap);

   nir_def *buffer_addr =
      nir_load_printf_buffer_address(
         b, ptr_bit_size ? ptr_bit_size : nir_get_ptr_bitsize(b->shader));
   nir_def *buffer_offset =
      nir_global_atomic(b, 32, buffer_addr,
                        nir_imm_int(b, args_size + sizeof(uint32_t)),
                        .atomic_op = nir_atomic_op_iadd);

   uint32_t total_size = sizeof(uint32_t); /* identifier */
   for (unsigned a = 0; a < info.num_args; a++)
      total_size += info.arg_sizes[a];

   nir_push_if(b, nir_ilt(b, nir_iadd_imm(b, buffer_offset, total_size),
                             nir_load_printf_buffer_size(b)));
   {
      nir_def *identifier = nir_imm_int(b, u_printf_hash(&info));
      nir_def *store_addr =
         nir_iadd(b, buffer_addr, nir_u2uN(b, buffer_offset, buffer_addr->bit_size));
      nir_store_global(b, store_addr, 4, identifier, 0x1);

      /* Arguments */
      va_start(ap, fmt);
      unsigned store_offset = sizeof(uint32_t);
      for (unsigned a = 0; a < info.num_args; a++) {
         nir_def *def = va_arg(ap, nir_def*);

         nir_store_global(b, nir_iadd_imm(b, store_addr, store_offset),
                          4, def, 0x1);

         store_offset += info.arg_sizes[a];
      }
      va_end(ap);
   }
   nir_pop_if(b, NULL);

   /* Add the format string to the printf singleton, registering the hash for
    * the driver. This isn't actually correct, because the shader may be cached
    * and reused in the future but the singleton will die along with the logical
    * device. However, nir_printf_fmt is a debugging aid used in conjunction
    * with directly modifying the Mesa code, there are never uses of
    * nir_printf_fmt checked into the tree. Rebuilding Mesa invalidates the disk
    * cache anyway, so this will more or less do what we want without requiring
    * lots of extra plumbing to soften this edge case. And disabling the disk
    * cache while debugging compiler issues is a good practice anyway.
    */
   u_printf_singleton_add(&info, 1);
}
