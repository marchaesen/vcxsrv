/*
 * Copyright © 2015 Connor Abbott
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

/**
 * nir_opt_vectorize() aims to vectorize ALU instructions.
 *
 * The default vectorization width is 4.
 * If desired, a callback function which returns the max vectorization width
 * per instruction can be provided.
 *
 * The max vectorization width must be a power of 2.
 */

#include "util/u_dynarray.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_vla.h"

#define XXH_INLINE_ALL
#include "util/xxhash.h"

#define HASH(hash, data) XXH32(&data, sizeof(data), hash)

static uint32_t
hash_src(uint32_t hash, const nir_src *src)
{
   void *hash_data = nir_src_is_const(*src) ? NULL : src->ssa;

   return HASH(hash, hash_data);
}

static uint32_t
hash_alu_src(uint32_t hash, const nir_alu_src *src,
             uint32_t num_components, uint32_t max_vec)
{
   /* hash whether a swizzle accesses elements beyond the maximum
    * vectorization factor:
    * For example accesses to .x and .y are considered different variables
    * compared to accesses to .z and .w for 16-bit vec2.
    */
   uint32_t swizzle = (src->swizzle[0] & ~(max_vec - 1));
   hash = HASH(hash, swizzle);

   return hash_src(hash, &src->src);
}

static uint32_t
hash_phi_src(uint32_t hash, const nir_phi_instr *phi, const nir_phi_src *src,
             uint32_t max_vec)
{
   hash = HASH(hash, src->pred);

   nir_scalar chased = nir_scalar_chase_movs(nir_get_scalar(src->src.ssa, 0));
   uint32_t swizzle = chased.comp & ~(max_vec - 1);
   hash = HASH(hash, swizzle);

   if (nir_scalar_is_const(chased)) {
      void *data = NULL;
      hash = HASH(hash, data);
   } else if (src->pred->index < phi->instr.block->index) {
      hash = HASH(hash, chased.def);
   } else {
      nir_instr *chased_instr = chased.def->parent_instr;
      hash = HASH(hash, chased_instr->type);

      if (chased_instr->type == nir_instr_type_alu)
         hash = HASH(hash, nir_instr_as_alu(chased_instr)->op);
   }

   return hash;
}

static uint32_t
hash_instr(const void *data)
{
   const nir_instr *instr = (nir_instr *)data;
   uint32_t hash = HASH(0, instr->type);

   if (instr->type == nir_instr_type_phi) {
      nir_phi_instr *phi = nir_instr_as_phi(instr);

      hash = HASH(hash, instr->block);
      hash = HASH(hash, phi->def.bit_size);

      /* The order of phi sources is not guaranteed so hash commutatively. */
      nir_foreach_phi_src(src, phi)
         hash *= hash_phi_src(0, phi, src, instr->pass_flags);

      return hash;
   }

   assert(instr->type == nir_instr_type_alu);
   nir_alu_instr *alu = nir_instr_as_alu(instr);

   hash = HASH(hash, alu->op);
   hash = HASH(hash, alu->def.bit_size);

   for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++)
      hash = hash_alu_src(hash, &alu->src[i],
                          alu->def.num_components,
                          instr->pass_flags);

   return hash;
}

static bool
srcs_equal(const nir_src *src1, const nir_src *src2)
{

   return src1->ssa == src2->ssa ||
          (nir_src_is_const(*src1) && nir_src_is_const(*src2));
}

static bool
alu_srcs_equal(const nir_alu_src *src1, const nir_alu_src *src2,
               uint32_t max_vec)
{
   uint32_t mask = ~(max_vec - 1);
   if ((src1->swizzle[0] & mask) != (src2->swizzle[0] & mask))
      return false;

   return srcs_equal(&src1->src, &src2->src);
}

static bool
phi_srcs_equal(nir_block *block, const nir_phi_src *src1,
               const nir_phi_src *src2, uint32_t max_vec)
{
   if (src1->pred != src2->pred)
      return false;

   /* Since phi sources don't have swizzles, they are swizzled using movs.
    * Get the real sources first.
    */
   nir_scalar chased1 = nir_scalar_chase_movs(nir_get_scalar(src1->src.ssa, 0));
   nir_scalar chased2 = nir_scalar_chase_movs(nir_get_scalar(src2->src.ssa, 0));

   if (nir_scalar_is_const(chased1) && nir_scalar_is_const(chased2))
      return true;

   uint32_t mask = ~(max_vec - 1);
   if ((chased1.comp & mask) != (chased2.comp & mask))
      return false;

   /* For phi sources whose defs we have already processed, we require that
    * they point to the same def like we do for ALU instructions.
    */
   if (src1->pred->index < block->index)
      return chased1.def == chased2.def;

   /* Otherwise (i.e., for loop back-edges), we haven't processed the sources
    * yet so they haven't been vectorized. In this case, try to guess if they
    * could be vectorized later. Keep it simple for now: if they are the same
    * type of instruction and, if ALU, have the same operation, assume they
    * might be vectorized later. Although this won't be true in general, this
    * heuristic is probable good enough in practice: since we check that other
    * (forward-edge) sources are vectorized, chances are the back-edge will
    * also be vectorized.
    */
   nir_instr *chased_instr1 = chased1.def->parent_instr;
   nir_instr *chased_instr2 = chased2.def->parent_instr;

   if (chased_instr1->type != chased_instr2->type)
      return false;

   if (chased_instr1->type != nir_instr_type_alu)
      return true;

   return nir_instr_as_alu(chased_instr1)->op ==
          nir_instr_as_alu(chased_instr2)->op;
}

static bool
instrs_equal(const void *data1, const void *data2)
{
   const nir_instr *instr1 = (nir_instr *)data1;
   const nir_instr *instr2 = (nir_instr *)data2;

   if (instr1->type != instr2->type)
      return false;

   if (instr1->type == nir_instr_type_phi) {
      if (instr1->block != instr2->block)
         return false;

      nir_phi_instr *phi1 = nir_instr_as_phi(instr1);
      nir_phi_instr *phi2 = nir_instr_as_phi(instr2);

      if (phi1->def.bit_size != phi2->def.bit_size)
         return false;

      nir_foreach_phi_src(src1, phi1) {
         nir_phi_src *src2 = nir_phi_get_src_from_block(phi2, src1->pred);

         if (!phi_srcs_equal(instr1->block, src1, src2, instr1->pass_flags))
            return false;
      }

      return true;
   }

   assert(instr1->type == nir_instr_type_alu);
   assert(instr2->type == nir_instr_type_alu);

   nir_alu_instr *alu1 = nir_instr_as_alu(instr1);
   nir_alu_instr *alu2 = nir_instr_as_alu(instr2);

   if (alu1->op != alu2->op)
      return false;

   if (alu1->def.bit_size != alu2->def.bit_size)
      return false;

   for (unsigned i = 0; i < nir_op_infos[alu1->op].num_inputs; i++) {
      if (!alu_srcs_equal(&alu1->src[i], &alu2->src[i], instr1->pass_flags))
         return false;
   }

   return true;
}

static bool
instr_can_rewrite(nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);

      /* Don't try and vectorize mov's. Either they'll be handled by copy
       * prop, or they're actually necessary and trying to vectorize them
       * would result in fighting with copy prop.
       */
      if (alu->op == nir_op_mov)
         return false;

      /* no need to hash instructions which are already vectorized */
      if (alu->def.num_components >= instr->pass_flags)
         return false;

      if (nir_op_infos[alu->op].output_size != 0)
         return false;

      for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
         if (nir_op_infos[alu->op].input_sizes[i] != 0)
            return false;

         /* don't hash instructions which are already swizzled
          * outside of max_components: these should better be scalarized */
         uint32_t mask = ~(instr->pass_flags - 1);
         for (unsigned j = 1; j < alu->def.num_components; j++) {
            if ((alu->src[i].swizzle[0] & mask) != (alu->src[i].swizzle[j] & mask))
               return false;
         }
      }

      return true;
   }

   case nir_instr_type_phi: {
      nir_phi_instr *phi = nir_instr_as_phi(instr);
      return phi->def.num_components < instr->pass_flags;
   }

   default:
      break;
   }

   return false;
}

static void
rewrite_uses(nir_builder *b, struct set *instr_set, nir_def *def1,
             nir_def *def2, nir_def *new_def)
{
   /* update all ALU uses */
   nir_foreach_use_safe(src, def1) {
      nir_instr *user_instr = nir_src_parent_instr(src);
      if (user_instr->type == nir_instr_type_alu) {
         /* Check if user is found in the hashset */
         struct set_entry *entry = _mesa_set_search(instr_set, user_instr);

         /* For ALU instructions, rewrite the source directly to avoid a
          * round-trip through copy propagation.
          */
         nir_src_rewrite(src, new_def);

         /* Rehash user if it was found in the hashset */
         if (entry && entry->key == user_instr) {
            _mesa_set_remove(instr_set, entry);
            _mesa_set_add(instr_set, user_instr);
         }
      }
   }

   nir_foreach_use_safe(src, def2) {
      if (nir_src_parent_instr(src)->type == nir_instr_type_alu) {
         /* For ALU instructions, rewrite the source directly to avoid a
          * round-trip through copy propagation.
          */
         nir_src_rewrite(src, new_def);

         nir_alu_src *alu_src = container_of(src, nir_alu_src, src);
         nir_alu_instr *use = nir_instr_as_alu(nir_src_parent_instr(src));
         unsigned components =
            nir_ssa_alu_instr_src_components(use, alu_src - use->src);
         for (unsigned i = 0; i < components; i++)
            alu_src->swizzle[i] += def1->num_components;
      }
   }

   /* update all other uses if there are any */
   unsigned swiz[NIR_MAX_VEC_COMPONENTS];

   if (!nir_def_is_unused(def1)) {
      for (unsigned i = 0; i < def1->num_components; i++)
         swiz[i] = i;
      nir_def *new_def1 = nir_swizzle(b, new_def, swiz, def1->num_components);
      nir_def_rewrite_uses(def1, new_def1);
   }

   if (!nir_def_is_unused(def2)) {
      for (unsigned i = 0; i < def2->num_components; i++)
         swiz[i] = i + def1->num_components;
      nir_def *new_def2 = nir_swizzle(b, new_def, swiz, def2->num_components);
      nir_def_rewrite_uses(def2, new_def2);
   }

   nir_instr_remove(def1->parent_instr);
   nir_instr_remove(def2->parent_instr);
}

static nir_instr *
instr_try_combine_phi(struct set *instr_set, nir_phi_instr *phi1, nir_phi_instr *phi2)
{
   assert(phi1->def.bit_size == phi2->def.bit_size);
   unsigned phi1_components = phi1->def.num_components;
   unsigned phi2_components = phi2->def.num_components;
   unsigned total_components = phi1_components + phi2_components;

   assert(phi1->instr.pass_flags == phi2->instr.pass_flags);
   if (total_components > phi1->instr.pass_flags)
      return NULL;

   assert(phi1->instr.block == phi2->instr.block);
   nir_block *block = phi1->instr.block;

   nir_builder b = nir_builder_at(nir_after_instr(&phi1->instr));
   nir_phi_instr *new_phi = nir_phi_instr_create(b.shader);
   nir_def_init(&new_phi->instr, &new_phi->def, total_components,
                phi1->def.bit_size);
   nir_builder_instr_insert(&b, &new_phi->instr);
   new_phi->instr.pass_flags = phi1->instr.pass_flags;

   assert(exec_list_length(&phi1->srcs) == exec_list_length(&phi2->srcs));

   nir_foreach_phi_src(src1, phi1) {
      nir_phi_src *src2 = nir_phi_get_src_from_block(phi2, src1->pred);
      nir_block *pred_block = src1->pred;

      nir_scalar new_srcs[NIR_MAX_VEC_COMPONENTS];

      for (unsigned i = 0; i < phi1_components; i++) {
         nir_scalar s = nir_get_scalar(src1->src.ssa, i);
         new_srcs[i] = nir_scalar_chase_movs(s);
      }

      for (unsigned i = 0; i < phi2_components; i++) {
         nir_scalar s = nir_get_scalar(src2->src.ssa, i);
         new_srcs[phi1_components + i] = nir_scalar_chase_movs(s);
      }

      nir_def *new_src;

      if (nir_scalar_is_const(new_srcs[0])) {
         nir_const_value value[NIR_MAX_VEC_COMPONENTS];

         for (unsigned i = 0; i < total_components; i++) {
            assert(nir_scalar_is_const(new_srcs[i]));
            value[i] = nir_scalar_as_const_value(new_srcs[i]);
         }

         b.cursor = nir_after_block_before_jump(pred_block);
         unsigned bit_size = src1->src.ssa->bit_size;
         new_src = nir_build_imm(&b, total_components, bit_size, value);
      } else if (pred_block->index < block->index) {
         nir_def *def = new_srcs[0].def;
         unsigned swizzle[NIR_MAX_VEC_COMPONENTS];

         for (unsigned i = 0; i < total_components; i++) {
            assert(new_srcs[i].def == def);
            swizzle[i] = new_srcs[i].comp;
         }

         b.cursor = nir_after_instr_and_phis(def->parent_instr);
         new_src = nir_swizzle(&b, def, swizzle, total_components);
      } else {
         /* This is a loop back-edge so we haven't vectorized the sources yet.
          * Combine them in a vec which, if they are vectorized later, will be
          * cleaned up by copy propagation.
          */
         b.cursor = nir_after_block_before_jump(pred_block);
         new_src = nir_vec_scalars(&b, new_srcs, total_components);
      }

      nir_phi_src *new_phi_src =
         nir_phi_instr_add_src(new_phi, src1->pred, new_src);
      list_addtail(&new_phi_src->src.use_link, &new_src->uses);
   }

   b.cursor = nir_after_phis(block);
   rewrite_uses(&b, instr_set, &phi1->def, &phi2->def, &new_phi->def);

   return &new_phi->instr;
}

static nir_instr *
instr_try_combine_alu(struct set *instr_set, nir_alu_instr *alu1, nir_alu_instr *alu2)
{
   assert(alu1->def.bit_size == alu2->def.bit_size);
   unsigned alu1_components = alu1->def.num_components;
   unsigned alu2_components = alu2->def.num_components;
   unsigned total_components = alu1_components + alu2_components;

   assert(alu1->instr.pass_flags == alu2->instr.pass_flags);
   if (total_components > alu1->instr.pass_flags)
      return NULL;

   nir_builder b = nir_builder_at(nir_after_instr(&alu1->instr));

   nir_alu_instr *new_alu = nir_alu_instr_create(b.shader, alu1->op);
   nir_def_init(&new_alu->instr, &new_alu->def, total_components,
                alu1->def.bit_size);
   new_alu->instr.pass_flags = alu1->instr.pass_flags;

   /* If either channel is exact, we have to preserve it even if it's
    * not optimal for other channels.
    */
   new_alu->exact = alu1->exact || alu2->exact;

   /* fp_fast_math is a set of FLOAT_CONTROLS_*_PRESERVE_*.  Preserve anything
    * preserved by either instruction.
    */
   new_alu->fp_fast_math = alu1->fp_fast_math | alu2->fp_fast_math;

   /* If all channels don't wrap, we can say that the whole vector doesn't
    * wrap.
    */
   new_alu->no_signed_wrap = alu1->no_signed_wrap && alu2->no_signed_wrap;
   new_alu->no_unsigned_wrap = alu1->no_unsigned_wrap && alu2->no_unsigned_wrap;

   for (unsigned i = 0; i < nir_op_infos[alu1->op].num_inputs; i++) {
      /* handle constant merging case */
      if (alu1->src[i].src.ssa != alu2->src[i].src.ssa) {
         nir_const_value *c1 = nir_src_as_const_value(alu1->src[i].src);
         nir_const_value *c2 = nir_src_as_const_value(alu2->src[i].src);
         assert(c1 && c2);
         nir_const_value value[NIR_MAX_VEC_COMPONENTS];
         unsigned bit_size = alu1->src[i].src.ssa->bit_size;

         for (unsigned j = 0; j < total_components; j++) {
            value[j].u64 = j < alu1_components ? c1[alu1->src[i].swizzle[j]].u64 : c2[alu2->src[i].swizzle[j - alu1_components]].u64;
         }
         nir_def *def = nir_build_imm(&b, total_components, bit_size, value);

         new_alu->src[i].src = nir_src_for_ssa(def);
         for (unsigned j = 0; j < total_components; j++)
            new_alu->src[i].swizzle[j] = j;
         continue;
      }

      new_alu->src[i].src = alu1->src[i].src;

      for (unsigned j = 0; j < alu1_components; j++)
         new_alu->src[i].swizzle[j] = alu1->src[i].swizzle[j];

      for (unsigned j = 0; j < alu2_components; j++) {
         new_alu->src[i].swizzle[j + alu1_components] =
            alu2->src[i].swizzle[j];
      }
   }

   nir_builder_instr_insert(&b, &new_alu->instr);
   rewrite_uses(&b, instr_set, &alu1->def, &alu2->def, &new_alu->def);

   return &new_alu->instr;
}

/*
 * Tries to combine two instructions whose sources are different components of
 * the same instructions into one vectorized instruction. Note that instr1
 * should dominate instr2.
 */
static nir_instr *
instr_try_combine(struct set *instr_set, nir_instr *instr1, nir_instr *instr2)
{
   switch (instr1->type) {
   case nir_instr_type_alu:
      assert(instr2->type == nir_instr_type_alu);
      return instr_try_combine_alu(instr_set, nir_instr_as_alu(instr1),
                                   nir_instr_as_alu(instr2));

   case nir_instr_type_phi:
      assert(instr2->type == nir_instr_type_phi);
      return instr_try_combine_phi(instr_set, nir_instr_as_phi(instr1),
                                   nir_instr_as_phi(instr2));

   default:
      unreachable("Unsupported instruction type");
   }
}

static struct set *
vec_instr_set_create(void)
{
   return _mesa_set_create(NULL, hash_instr, instrs_equal);
}

static void
vec_instr_set_destroy(struct set *instr_set)
{
   _mesa_set_destroy(instr_set, NULL);
}

static bool
vec_instr_set_add_or_rewrite(struct set *instr_set, nir_instr *instr,
                             nir_vectorize_cb filter, void *data)
{
   /* set max vector to instr pass flags: this is used to hash swizzles */
   instr->pass_flags = filter ? filter(instr, data) : 4;
   assert(util_is_power_of_two_or_zero(instr->pass_flags));

   if (!instr_can_rewrite(instr))
      return false;

   struct set_entry *entry = _mesa_set_search(instr_set, instr);
   if (entry) {
      nir_instr *old_instr = (nir_instr *)entry->key;

      /* We cannot combine the instructions if the old one doesn't dominate
       * the new one. Since we will never encounter a block again that is
       * dominated by the old instruction, overwrite it with the new one in
       * the instruction set.
       */
      if (!nir_block_dominates(old_instr->block, instr->block)) {
         entry->key = instr;
         return false;
      }

      _mesa_set_remove(instr_set, entry);
      nir_instr *new_instr = instr_try_combine(instr_set, old_instr, instr);
      if (new_instr) {
         if (instr_can_rewrite(new_instr))
            _mesa_set_add(instr_set, new_instr);
         return true;
      }
   }

   _mesa_set_add(instr_set, instr);
   return false;
}

static bool
nir_opt_vectorize_impl(nir_function_impl *impl,
                       nir_vectorize_cb filter, void *data)
{
   struct set *instr_set = vec_instr_set_create();

   nir_metadata_require(impl, nir_metadata_control_flow);

   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         progress |= vec_instr_set_add_or_rewrite(instr_set, instr, filter, data);
      }
   }

   nir_progress(progress, impl, nir_metadata_control_flow);

   vec_instr_set_destroy(instr_set);
   return progress;
}

bool
nir_opt_vectorize(nir_shader *shader, nir_vectorize_cb filter,
                  void *data)
{
   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      progress |= nir_opt_vectorize_impl(impl, filter, data);
   }

   return progress;
}
