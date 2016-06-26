/*
 * Copyright © 2014 Connor Abbott
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

#include "nir_instr_set.h"
#include "nir_vla.h"

#define HASH(hash, data) _mesa_fnv32_1a_accumulate((hash), (data))

static uint32_t
hash_src(uint32_t hash, const nir_src *src)
{
   assert(src->is_ssa);
   hash = HASH(hash, src->ssa);
   return hash;
}

static uint32_t
hash_alu_src(uint32_t hash, const nir_alu_src *src, unsigned num_components)
{
   hash = HASH(hash, src->abs);
   hash = HASH(hash, src->negate);

   for (unsigned i = 0; i < num_components; i++)
      hash = HASH(hash, src->swizzle[i]);

   hash = hash_src(hash, &src->src);
   return hash;
}

static uint32_t
hash_alu(uint32_t hash, const nir_alu_instr *instr)
{
   hash = HASH(hash, instr->op);
   hash = HASH(hash, instr->dest.dest.ssa.num_components);
   hash = HASH(hash, instr->dest.dest.ssa.bit_size);
   /* We explicitly don't hash instr->dest.dest.exact */

   if (nir_op_infos[instr->op].algebraic_properties & NIR_OP_IS_COMMUTATIVE) {
      assert(nir_op_infos[instr->op].num_inputs == 2);
      uint32_t hash0 = hash_alu_src(hash, &instr->src[0],
                                    nir_ssa_alu_instr_src_components(instr, 0));
      uint32_t hash1 = hash_alu_src(hash, &instr->src[1],
                                    nir_ssa_alu_instr_src_components(instr, 1));
      /* For commutative operations, we need some commutative way of
       * combining the hashes.  One option would be to XOR them but that
       * means that anything with two identical sources will hash to 0 and
       * that's common enough we probably don't want the guaranteed
       * collision.  Either addition or multiplication will also work.
       */
      hash = hash0 * hash1;
   } else {
      for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
         hash = hash_alu_src(hash, &instr->src[i],
                             nir_ssa_alu_instr_src_components(instr, i));
      }
   }

   return hash;
}

static uint32_t
hash_load_const(uint32_t hash, const nir_load_const_instr *instr)
{
   hash = HASH(hash, instr->def.num_components);

   unsigned size = instr->def.num_components * (instr->def.bit_size / 8);
   hash = _mesa_fnv32_1a_accumulate_block(hash, instr->value.f32, size);

   return hash;
}

static int
cmp_phi_src(const void *data1, const void *data2)
{
   nir_phi_src *src1 = *(nir_phi_src **)data1;
   nir_phi_src *src2 = *(nir_phi_src **)data2;
   return src1->pred - src2->pred;
}

static uint32_t
hash_phi(uint32_t hash, const nir_phi_instr *instr)
{
   hash = HASH(hash, instr->instr.block);

   /* sort sources by predecessor, since the order shouldn't matter */
   unsigned num_preds = instr->instr.block->predecessors->entries;
   NIR_VLA(nir_phi_src *, srcs, num_preds);
   unsigned i = 0;
   nir_foreach_phi_src(src, instr) {
      srcs[i++] = src;
   }

   qsort(srcs, num_preds, sizeof(nir_phi_src *), cmp_phi_src);

   for (i = 0; i < num_preds; i++) {
      hash = hash_src(hash, &srcs[i]->src);
      hash = HASH(hash, srcs[i]->pred);
   }

   return hash;
}

static uint32_t
hash_intrinsic(uint32_t hash, const nir_intrinsic_instr *instr)
{
   const nir_intrinsic_info *info = &nir_intrinsic_infos[instr->intrinsic];
   hash = HASH(hash, instr->intrinsic);

   if (info->has_dest) {
      hash = HASH(hash, instr->dest.ssa.num_components);
      hash = HASH(hash, instr->dest.ssa.bit_size);
   }

   assert(info->num_variables == 0);

   hash = _mesa_fnv32_1a_accumulate_block(hash, instr->const_index,
                                          info->num_indices
                                             * sizeof(instr->const_index[0]));
   return hash;
}

static uint32_t
hash_tex(uint32_t hash, const nir_tex_instr *instr)
{
   hash = HASH(hash, instr->op);
   hash = HASH(hash, instr->num_srcs);

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      hash = HASH(hash, instr->src[i].src_type);
      hash = hash_src(hash, &instr->src[i].src);
   }

   hash = HASH(hash, instr->coord_components);
   hash = HASH(hash, instr->sampler_dim);
   hash = HASH(hash, instr->is_array);
   hash = HASH(hash, instr->is_shadow);
   hash = HASH(hash, instr->is_new_style_shadow);
   unsigned component = instr->component;
   hash = HASH(hash, component);
   hash = HASH(hash, instr->texture_index);
   hash = HASH(hash, instr->texture_array_size);
   hash = HASH(hash, instr->sampler_index);

   assert(!instr->texture && !instr->sampler);

   return hash;
}

/* Computes a hash of an instruction for use in a hash table. Note that this
 * will only work for instructions where instr_can_rewrite() returns true, and
 * it should return identical hashes for two instructions that are the same
 * according nir_instrs_equal().
 */

static uint32_t
hash_instr(const void *data)
{
   const nir_instr *instr = data;
   uint32_t hash = _mesa_fnv32_1a_offset_bias;

   switch (instr->type) {
   case nir_instr_type_alu:
      hash = hash_alu(hash, nir_instr_as_alu(instr));
      break;
   case nir_instr_type_load_const:
      hash = hash_load_const(hash, nir_instr_as_load_const(instr));
      break;
   case nir_instr_type_phi:
      hash = hash_phi(hash, nir_instr_as_phi(instr));
      break;
   case nir_instr_type_intrinsic:
      hash = hash_intrinsic(hash, nir_instr_as_intrinsic(instr));
      break;
   case nir_instr_type_tex:
      hash = hash_tex(hash, nir_instr_as_tex(instr));
      break;
   default:
      unreachable("Invalid instruction type");
   }

   return hash;
}

bool
nir_srcs_equal(nir_src src1, nir_src src2)
{
   if (src1.is_ssa) {
      if (src2.is_ssa) {
         return src1.ssa == src2.ssa;
      } else {
         return false;
      }
   } else {
      if (src2.is_ssa) {
         return false;
      } else {
         if ((src1.reg.indirect == NULL) != (src2.reg.indirect == NULL))
            return false;

         if (src1.reg.indirect) {
            if (!nir_srcs_equal(*src1.reg.indirect, *src2.reg.indirect))
               return false;
         }

         return src1.reg.reg == src2.reg.reg &&
                src1.reg.base_offset == src2.reg.base_offset;
      }
   }
}

static bool
nir_alu_srcs_equal(const nir_alu_instr *alu1, const nir_alu_instr *alu2,
                   unsigned src1, unsigned src2)
{
   if (alu1->src[src1].abs != alu2->src[src2].abs ||
       alu1->src[src1].negate != alu2->src[src2].negate)
      return false;

   for (unsigned i = 0; i < nir_ssa_alu_instr_src_components(alu1, src1); i++) {
      if (alu1->src[src1].swizzle[i] != alu2->src[src2].swizzle[i])
         return false;
   }

   return nir_srcs_equal(alu1->src[src1].src, alu2->src[src2].src);
}

/* Returns "true" if two instructions are equal. Note that this will only
 * work for the subset of instructions defined by instr_can_rewrite(). Also,
 * it should only return "true" for instructions that hash_instr() will return
 * the same hash for (ignoring collisions, of course).
 */

static bool
nir_instrs_equal(const nir_instr *instr1, const nir_instr *instr2)
{
   if (instr1->type != instr2->type)
      return false;

   switch (instr1->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu1 = nir_instr_as_alu(instr1);
      nir_alu_instr *alu2 = nir_instr_as_alu(instr2);

      if (alu1->op != alu2->op)
         return false;

      /* TODO: We can probably acutally do something more inteligent such
       * as allowing different numbers and taking a maximum or something
       * here */
      if (alu1->dest.dest.ssa.num_components != alu2->dest.dest.ssa.num_components)
         return false;

      if (alu1->dest.dest.ssa.bit_size != alu2->dest.dest.ssa.bit_size)
         return false;

      /* We explicitly don't hash instr->dest.dest.exact */

      if (nir_op_infos[alu1->op].algebraic_properties & NIR_OP_IS_COMMUTATIVE) {
         assert(nir_op_infos[alu1->op].num_inputs == 2);
         return (nir_alu_srcs_equal(alu1, alu2, 0, 0) &&
                 nir_alu_srcs_equal(alu1, alu2, 1, 1)) ||
                (nir_alu_srcs_equal(alu1, alu2, 0, 1) &&
                 nir_alu_srcs_equal(alu1, alu2, 1, 0));
      } else {
         for (unsigned i = 0; i < nir_op_infos[alu1->op].num_inputs; i++) {
            if (!nir_alu_srcs_equal(alu1, alu2, i, i))
               return false;
         }
      }
      return true;
   }
   case nir_instr_type_tex: {
      nir_tex_instr *tex1 = nir_instr_as_tex(instr1);
      nir_tex_instr *tex2 = nir_instr_as_tex(instr2);

      if (tex1->op != tex2->op)
         return false;

      if (tex1->num_srcs != tex2->num_srcs)
         return false;
      for (unsigned i = 0; i < tex1->num_srcs; i++) {
         if (tex1->src[i].src_type != tex2->src[i].src_type ||
             !nir_srcs_equal(tex1->src[i].src, tex2->src[i].src)) {
            return false;
         }
      }

      if (tex1->coord_components != tex2->coord_components ||
          tex1->sampler_dim != tex2->sampler_dim ||
          tex1->is_array != tex2->is_array ||
          tex1->is_shadow != tex2->is_shadow ||
          tex1->is_new_style_shadow != tex2->is_new_style_shadow ||
          tex1->component != tex2->component ||
         tex1->texture_index != tex2->texture_index ||
         tex1->texture_array_size != tex2->texture_array_size ||
         tex1->sampler_index != tex2->sampler_index) {
         return false;
      }

      /* Don't support un-lowered sampler derefs currently. */
      assert(!tex1->texture && !tex1->sampler &&
             !tex2->texture && !tex2->sampler);

      return true;
   }
   case nir_instr_type_load_const: {
      nir_load_const_instr *load1 = nir_instr_as_load_const(instr1);
      nir_load_const_instr *load2 = nir_instr_as_load_const(instr2);

      if (load1->def.num_components != load2->def.num_components)
         return false;

      if (load1->def.bit_size != load2->def.bit_size)
         return false;

      return memcmp(load1->value.f32, load2->value.f32,
                    load1->def.num_components * (load1->def.bit_size / 8u)) == 0;
   }
   case nir_instr_type_phi: {
      nir_phi_instr *phi1 = nir_instr_as_phi(instr1);
      nir_phi_instr *phi2 = nir_instr_as_phi(instr2);

      if (phi1->instr.block != phi2->instr.block)
         return false;

      nir_foreach_phi_src(src1, phi1) {
         nir_foreach_phi_src(src2, phi2) {
            if (src1->pred == src2->pred) {
               if (!nir_srcs_equal(src1->src, src2->src))
                  return false;

               break;
            }
         }
      }

      return true;
   }
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrinsic1 = nir_instr_as_intrinsic(instr1);
      nir_intrinsic_instr *intrinsic2 = nir_instr_as_intrinsic(instr2);
      const nir_intrinsic_info *info =
         &nir_intrinsic_infos[intrinsic1->intrinsic];

      if (intrinsic1->intrinsic != intrinsic2->intrinsic ||
          intrinsic1->num_components != intrinsic2->num_components)
         return false;

      if (info->has_dest && intrinsic1->dest.ssa.num_components !=
                            intrinsic2->dest.ssa.num_components)
         return false;

      if (info->has_dest && intrinsic1->dest.ssa.bit_size !=
                            intrinsic2->dest.ssa.bit_size)
         return false;

      for (unsigned i = 0; i < info->num_srcs; i++) {
         if (!nir_srcs_equal(intrinsic1->src[i], intrinsic2->src[i]))
            return false;
      }

      assert(info->num_variables == 0);

      for (unsigned i = 0; i < info->num_indices; i++) {
         if (intrinsic1->const_index[i] != intrinsic2->const_index[i])
            return false;
      }

      return true;
   }
   case nir_instr_type_call:
   case nir_instr_type_jump:
   case nir_instr_type_ssa_undef:
   case nir_instr_type_parallel_copy:
   default:
      unreachable("Invalid instruction type");
   }

   return false;
}

static bool
src_is_ssa(nir_src *src, void *data)
{
   (void) data;
   return src->is_ssa;
}

static bool
dest_is_ssa(nir_dest *dest, void *data)
{
   (void) data;
   return dest->is_ssa;
}

/* This function determines if uses of an instruction can safely be rewritten
 * to use another identical instruction instead. Note that this function must
 * be kept in sync with hash_instr() and nir_instrs_equal() -- only
 * instructions that pass this test will be handed on to those functions, and
 * conversely they must handle everything that this function returns true for.
 */

static bool
instr_can_rewrite(nir_instr *instr)
{
   /* We only handle SSA. */
   if (!nir_foreach_dest(instr, dest_is_ssa, NULL) ||
       !nir_foreach_src(instr, src_is_ssa, NULL))
      return false;

   switch (instr->type) {
   case nir_instr_type_alu:
   case nir_instr_type_load_const:
   case nir_instr_type_phi:
      return true;
   case nir_instr_type_tex: {
      nir_tex_instr *tex = nir_instr_as_tex(instr);

      /* Don't support un-lowered sampler derefs currently. */
      if (tex->texture || tex->sampler)
         return false;

      return true;
   }
   case nir_instr_type_intrinsic: {
      const nir_intrinsic_info *info =
         &nir_intrinsic_infos[nir_instr_as_intrinsic(instr)->intrinsic];
      return (info->flags & NIR_INTRINSIC_CAN_ELIMINATE) &&
             (info->flags & NIR_INTRINSIC_CAN_REORDER) &&
             info->num_variables == 0; /* not implemented yet */
   }
   case nir_instr_type_call:
   case nir_instr_type_jump:
   case nir_instr_type_ssa_undef:
      return false;
   case nir_instr_type_parallel_copy:
   default:
      unreachable("Invalid instruction type");
   }

   return false;
}

static nir_ssa_def *
nir_instr_get_dest_ssa_def(nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_alu:
      assert(nir_instr_as_alu(instr)->dest.dest.is_ssa);
      return &nir_instr_as_alu(instr)->dest.dest.ssa;
   case nir_instr_type_load_const:
      return &nir_instr_as_load_const(instr)->def;
   case nir_instr_type_phi:
      assert(nir_instr_as_phi(instr)->dest.is_ssa);
      return &nir_instr_as_phi(instr)->dest.ssa;
   case nir_instr_type_intrinsic:
      assert(nir_instr_as_intrinsic(instr)->dest.is_ssa);
      return &nir_instr_as_intrinsic(instr)->dest.ssa;
   case nir_instr_type_tex:
      assert(nir_instr_as_tex(instr)->dest.is_ssa);
      return &nir_instr_as_tex(instr)->dest.ssa;
   default:
      unreachable("We never ask for any of these");
   }
}

static bool
cmp_func(const void *data1, const void *data2)
{
   return nir_instrs_equal(data1, data2);
}

struct set *
nir_instr_set_create(void *mem_ctx)
{
   return _mesa_set_create(mem_ctx, hash_instr, cmp_func);
}

void
nir_instr_set_destroy(struct set *instr_set)
{
   _mesa_set_destroy(instr_set, NULL);
}

bool
nir_instr_set_add_or_rewrite(struct set *instr_set, nir_instr *instr)
{
   if (!instr_can_rewrite(instr))
      return false;

   struct set_entry *entry = _mesa_set_search(instr_set, instr);
   if (entry) {
      nir_ssa_def *def = nir_instr_get_dest_ssa_def(instr);
      nir_instr *match = (nir_instr *) entry->key;
      nir_ssa_def *new_def = nir_instr_get_dest_ssa_def(match);

      /* It's safe to replace an exact instruction with an inexact one as
       * long as we make it exact.  If we got here, the two instructions are
       * exactly identical in every other way so, once we've set the exact
       * bit, they are the same.
       */
      if (instr->type == nir_instr_type_alu && nir_instr_as_alu(instr)->exact)
         nir_instr_as_alu(match)->exact = true;

      nir_ssa_def_rewrite_uses(def, nir_src_for_ssa(new_def));
      return true;
   }

   _mesa_set_add(instr_set, instr);
   return false;
}

void
nir_instr_set_remove(struct set *instr_set, nir_instr *instr)
{
   if (!instr_can_rewrite(instr))
      return;

   struct set_entry *entry = _mesa_set_search(instr_set, instr);
   if (entry)
      _mesa_set_remove(instr_set, entry);
}

