/*
 * Copyright © 2020 Valve Corporation
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
 *
 */

/*
 * Optimizes atomics (with uniform offsets) using subgroup operations to ensure
 * only one atomic operation is done per subgroup. So res = atomicAdd(addr, 1)
 * would become something like:
 *
 * uint tmp = subgroupAdd(1);
 * uint res;
 * if (subgroupElect())
 *    res = atomicAdd(addr, tmp);
 * res = subgroupBroadcastFirst(res) + subgroupExclusiveAdd(1);
 *
 * This pass requires divergence information.
 */

#include "nir/nir.h"
#include "nir/nir_builder.h"

static nir_op
parse_atomic_op(nir_intrinsic_instr *intr, unsigned *offset_src,
                unsigned *data_src, unsigned *offset2_src)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_ssbo_atomic:
      *offset_src = 1;
      *data_src = 2;
      *offset2_src = *offset_src;
      return nir_atomic_op_to_alu(nir_intrinsic_atomic_op(intr));
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_global_atomic:
   case nir_intrinsic_deref_atomic:
      *offset_src = 0;
      *data_src = 1;
      *offset2_src = *offset_src;
      return nir_atomic_op_to_alu(nir_intrinsic_atomic_op(intr));
   case nir_intrinsic_global_atomic_amd:
      *offset_src = 0;
      *data_src = 1;
      *offset2_src = 2;
      return nir_atomic_op_to_alu(nir_intrinsic_atomic_op(intr));
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_atomic:
   case nir_intrinsic_bindless_image_atomic:
      *offset_src = 1;
      *data_src = 3;
      *offset2_src = *offset_src;
      return nir_atomic_op_to_alu(nir_intrinsic_atomic_op(intr));

   default:
      return nir_num_opcodes;
   }
}

static unsigned
get_dim(nir_scalar scalar)
{
   if (!scalar.def->divergent)
      return 0;

   if (nir_scalar_is_intrinsic(scalar)) {
      switch (nir_scalar_intrinsic_op(scalar)) {
      case nir_intrinsic_load_subgroup_invocation:
         return 0x8;
      case nir_intrinsic_load_global_invocation_index:
      case nir_intrinsic_load_local_invocation_index:
         return 0x7;
      case nir_intrinsic_load_global_invocation_id:
      case nir_intrinsic_load_local_invocation_id:
         return 1 << scalar.comp;
      default:
         break;
      }
   } else if (nir_scalar_is_alu(scalar)) {
      if (nir_scalar_alu_op(scalar) == nir_op_iadd ||
          nir_scalar_alu_op(scalar) == nir_op_imul) {
         nir_scalar src0 = nir_scalar_chase_alu_src(scalar, 0);
         nir_scalar src1 = nir_scalar_chase_alu_src(scalar, 1);

         unsigned src0_dim = get_dim(src0);
         if (!src0_dim && src0.def->divergent)
            return 0;
         unsigned src1_dim = get_dim(src1);
         if (!src1_dim && src1.def->divergent)
            return 0;

         return src0_dim | src1_dim;
      } else if (nir_scalar_alu_op(scalar) == nir_op_ishl) {
         nir_scalar src0 = nir_scalar_chase_alu_src(scalar, 0);
         nir_scalar src1 = nir_scalar_chase_alu_src(scalar, 1);
         return src1.def->divergent ? 0 : get_dim(src0);
      }
   }

   return 0;
}

/* Returns a bitmask of invocation indices that are compared against a subgroup
 * uniform value.
 */
static unsigned
match_invocation_comparison(nir_scalar scalar)
{
   bool is_alu = nir_scalar_is_alu(scalar);
   if (is_alu && nir_scalar_alu_op(scalar) == nir_op_iand) {
      return match_invocation_comparison(nir_scalar_chase_alu_src(scalar, 0)) |
             match_invocation_comparison(nir_scalar_chase_alu_src(scalar, 1));
   } else if (is_alu && nir_scalar_alu_op(scalar) == nir_op_ieq) {
      if (!nir_scalar_chase_alu_src(scalar, 0).def->divergent)
         return get_dim(nir_scalar_chase_alu_src(scalar, 1));
      if (!nir_scalar_chase_alu_src(scalar, 1).def->divergent)
         return get_dim(nir_scalar_chase_alu_src(scalar, 0));
   } else if (scalar.def->parent_instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(scalar.def->parent_instr);
      if (intrin->intrinsic == nir_intrinsic_elect) {
         return 0x8;
      } else if (intrin->intrinsic == nir_intrinsic_inverse_ballot) {
         unsigned bitcount = 0;
         for (unsigned i = 0; i < intrin->src[0].ssa->num_components; i++) {
            scalar = nir_scalar_resolved(intrin->src[0].ssa, i);
            if (!nir_scalar_is_const(scalar))
               return 0;
            bitcount += util_bitcount64(nir_scalar_as_uint(scalar));
         }
         if (bitcount <= 1)
            return 0x8;
      }
   }

   return 0;
}

/* Returns true if the intrinsic is already conditional so that at most one
 * invocation in the subgroup does the atomic.
 */
static bool
is_atomic_already_optimized(nir_shader *shader, nir_intrinsic_instr *instr)
{
   unsigned dims = 0;
   for (nir_cf_node *cf = &instr->instr.block->cf_node; cf; cf = cf->parent) {
      if (cf->type == nir_cf_node_if) {
         nir_block *first_then = nir_if_first_then_block(nir_cf_node_as_if(cf));
         nir_block *last_then = nir_if_last_then_block(nir_cf_node_as_if(cf));
         bool within_then = instr->instr.block->index >= first_then->index;
         within_then = within_then && instr->instr.block->index <= last_then->index;
         if (!within_then)
            continue;

         nir_scalar cond = { nir_cf_node_as_if(cf)->condition.ssa, 0 };
         dims |= match_invocation_comparison(cond);
      }
   }

   if (gl_shader_stage_uses_workgroup(shader->info.stage)) {
      unsigned dims_needed = 0;
      for (unsigned i = 0; i < 3; i++)
         dims_needed |= (shader->info.workgroup_size_variable ||
                         shader->info.workgroup_size[i] > 1)
                        << i;
      if ((dims & dims_needed) == dims_needed)
         return true;
   }

   return dims & 0x8;
}

/* Perform a reduction and/or exclusive scan. */
static void
reduce_data(nir_builder *b, nir_op op, nir_def *data,
            nir_def **reduce, nir_def **scan)
{
   if (scan) {
      *scan = nir_exclusive_scan(b, data, .reduction_op = op);
      if (reduce) {
         nir_def *last_lane = nir_last_invocation(b);
         nir_def *res = nir_build_alu(b, op, *scan, data, NULL, NULL);
         *reduce = nir_read_invocation(b, res, last_lane);
      }
   } else {
      *reduce = nir_reduce(b, data, .reduction_op = op);
   }
}

static nir_def *
optimize_atomic(nir_builder *b, nir_intrinsic_instr *intrin, bool return_prev)
{
   unsigned offset_src = 0;
   unsigned data_src = 0;
   unsigned offset2_src = 0;
   nir_op op = parse_atomic_op(intrin, &offset_src, &data_src, &offset2_src);
   nir_def *data = intrin->src[data_src].ssa;

   /* Separate uniform reduction and scan is faster than doing a combined scan+reduce */
   bool combined_scan_reduce = return_prev &&
                               nir_src_is_divergent(&intrin->src[data_src]);
   nir_def *reduce = NULL, *scan = NULL;
   reduce_data(b, op, data, &reduce, combined_scan_reduce ? &scan : NULL);

   nir_src_rewrite(&intrin->src[data_src], reduce);

   nir_def *cond = nir_elect(b, 1);

   nir_if *nif = nir_push_if(b, cond);

   nir_instr_remove(&intrin->instr);
   nir_builder_instr_insert(b, &intrin->instr);

   if (return_prev) {
      nir_push_else(b, nif);

      nir_def *undef = nir_undef(b, 1, intrin->def.bit_size);

      nir_pop_if(b, nif);
      nir_def *result = nir_if_phi(b, &intrin->def, undef);
      result = nir_read_first_invocation(b, result);

      if (!combined_scan_reduce)
         reduce_data(b, op, data, NULL, &scan);

      return nir_build_alu(b, op, result, scan, NULL, NULL);
   } else {
      nir_pop_if(b, nif);
      return NULL;
   }
}

static void
optimize_and_rewrite_atomic(nir_builder *b, nir_intrinsic_instr *intrin,
                            bool fs_atomics_predicated)
{
   nir_if *helper_nif = NULL;
   if (b->shader->info.stage == MESA_SHADER_FRAGMENT && !fs_atomics_predicated) {
      nir_def *helper = nir_is_helper_invocation(b, 1);
      helper_nif = nir_push_if(b, nir_inot(b, helper));
   }

   bool return_prev = !nir_def_is_unused(&intrin->def);

   nir_def old_result = intrin->def;
   list_replace(&intrin->def.uses, &old_result.uses);
   nir_def_init(&intrin->instr, &intrin->def, 1,
                intrin->def.bit_size);

   nir_def *result = optimize_atomic(b, intrin, return_prev);

   if (helper_nif) {
      nir_push_else(b, helper_nif);
      nir_def *undef = result ? nir_undef(b, 1, result->bit_size) : NULL;
      nir_pop_if(b, helper_nif);
      if (result)
         result = nir_if_phi(b, result, undef);
   }

   if (result) {
      /* It's possible the result is used as source for another atomic,
       * so this needs to be correct.
       */
      result->divergent = old_result.divergent;
      nir_def_rewrite_uses(&old_result, result);
   }
}

static bool
opt_uniform_atomics(nir_function_impl *impl, bool fs_atomics_predicated)
{
   bool progress = false;
   nir_builder b = nir_builder_create(impl);

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         unsigned offset_src, data_src, offset2_src;
         if (parse_atomic_op(intrin, &offset_src, &data_src, &offset2_src) ==
             nir_num_opcodes)
            continue;

         if (nir_src_is_divergent(&intrin->src[offset_src]))
            continue;
         if (nir_src_is_divergent(&intrin->src[offset2_src]))
            continue;

         if (is_atomic_already_optimized(b.shader, intrin))
            continue;

         b.cursor = nir_before_instr(instr);
         optimize_and_rewrite_atomic(&b, intrin, fs_atomics_predicated);
         progress = true;
      }
   }

   return progress;
}

bool
nir_opt_uniform_atomics(nir_shader *shader, bool fs_atomics_predicated)
{
   bool progress = false;

   /* A 1x1x1 workgroup only ever has one active lane, so there's no point in
    * optimizing any atomics.
    */
   if (gl_shader_stage_uses_workgroup(shader->info.stage) &&
       !shader->info.workgroup_size_variable &&
       shader->info.workgroup_size[0] == 1 && shader->info.workgroup_size[1] == 1 &&
       shader->info.workgroup_size[2] == 1)
      return false;

   nir_foreach_function_impl(impl, shader) {
      nir_metadata_require(impl, nir_metadata_block_index | nir_metadata_divergence);

      bool impl_progress = opt_uniform_atomics(impl, fs_atomics_predicated);
      progress |= nir_progress(impl_progress, impl,
                               nir_metadata_none);
   }

   return progress;
}
