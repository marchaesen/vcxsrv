/*
 * Copyright 2025 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_worklist.h"

/* Various other IRs do not have 1bit booleans and instead use 0/1, 0/-1, 0/1.0
 * This pass detects phis with all sources in one of these representations and
 * converts the phi to 1bit. The cleanup of related alu is left to other passes
 * like nir_opt_algebraic.
 */

/* This enum is used to store what kind of bool the ssa def is in pass_flags.
 * It's a mask to allow multiple types for constant 0 and undef.
 */
enum bool_type {
   /* 0 is false, 1 is true. */
   bool_type_single_bit = BITFIELD_BIT(0),
   /* 0 is false, -1 is true. */
   bool_type_all_bits = BITFIELD_BIT(1),
   /* 0 is false, 1.0 is true. */
   bool_type_float = BITFIELD_BIT(2),

   bool_type_all_types = BITFIELD_MASK(3),
};

static inline uint8_t
src_pass_flags(nir_src *src)
{
   return src->ssa->parent_instr->pass_flags;
}

static inline nir_block *
block_get_loop_preheader(nir_block *block)
{
   nir_cf_node *parent = block->cf_node.parent;
   if (parent->type != nir_cf_node_loop)
      return NULL;
   if (block != nir_cf_node_cf_tree_first(parent))
      return NULL;
   return nir_cf_node_as_block(nir_cf_node_prev(parent));
}

static uint8_t
get_bool_types_const(nir_load_const_instr *load)
{
   uint8_t res = bool_type_all_types;
   unsigned bit_size = load->def.bit_size;
   for (unsigned i = 0; i < load->def.num_components; i++) {
      int64_t ival = nir_const_value_as_int(load->value[i], bit_size);
      if (ival == 0)
         continue;
      else if (ival == 1)
         res &= bool_type_single_bit;
      else if (ival == -1)
         res &= bool_type_all_bits;
      else if (bit_size >= 16 && nir_const_value_as_float(load->value[i], bit_size) == 1.0)
         res &= bool_type_float;
      else
         res = 0;
   }
   return res;
}

static uint8_t
get_bool_types_phi(nir_phi_instr *phi)
{
   uint8_t res = bool_type_all_types;
   nir_foreach_phi_src(phi_src, phi)
      res &= src_pass_flags(&phi_src->src);
   return res;
}

static uint8_t
negate_int_bool_types(nir_src *src)
{
   uint8_t src_types = src_pass_flags(src);
   uint8_t res = 0;
   if (src_types & bool_type_single_bit)
      res |= bool_type_all_bits;
   if (src_types & bool_type_all_bits)
      res |= bool_type_single_bit;
   return res;
}

static uint8_t
get_bool_types_alu(nir_alu_instr *alu)
{
   switch (alu->op) {
   case nir_op_b2i8:
   case nir_op_b2i16:
   case nir_op_b2i32:
   case nir_op_b2i64:
      return bool_type_single_bit;
   case nir_op_b2b8:
   case nir_op_b2b16:
   case nir_op_b2b32:
      return bool_type_all_bits;
   case nir_op_b2f16:
   case nir_op_b2f32:
   case nir_op_b2f64:
      return bool_type_float;
   case nir_op_ineg:
      return negate_int_bool_types(&alu->src[0].src);
   case nir_op_inot:
      return src_pass_flags(&alu->src[0].src) & bool_type_all_bits;
   case nir_op_bcsel:
      return src_pass_flags(&alu->src[1].src) & src_pass_flags(&alu->src[2].src);
   case nir_op_iand:
      if (src_pass_flags(&alu->src[0].src) & bool_type_all_bits)
         return src_pass_flags(&alu->src[1].src);
      if (src_pass_flags(&alu->src[1].src) & bool_type_all_bits)
         return src_pass_flags(&alu->src[0].src);
      FALLTHROUGH;
   case nir_op_imin:
   case nir_op_imax:
   case nir_op_umin:
   case nir_op_umax:
   case nir_op_ior:
   case nir_op_ixor:
      return src_pass_flags(&alu->src[0].src) & src_pass_flags(&alu->src[1].src);
   case nir_op_fmax:
   case nir_op_fmin:
   case nir_op_fmul:
   case nir_op_fmulz:
      return src_pass_flags(&alu->src[0].src) & src_pass_flags(&alu->src[1].src) & bool_type_float;
   default:
      return 0;
   }
}

static uint8_t
get_bool_types(nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_undef:
      return bool_type_all_types;
   case nir_instr_type_load_const:
      return get_bool_types_const(nir_instr_as_load_const(instr));
   case nir_instr_type_phi:
      return get_bool_types_phi(nir_instr_as_phi(instr));
   case nir_instr_type_alu:
      return get_bool_types_alu(nir_instr_as_alu(instr));
   default:
      return 0;
   }
}

static bool
phi_to_bool(nir_builder *b, nir_phi_instr *phi, void *unused)
{
   if (!phi->instr.pass_flags || phi->def.bit_size == 1)
      return false;

   enum bool_type type = BITFIELD_BIT(ffs(phi->instr.pass_flags) - 1);

   unsigned bit_size = phi->def.bit_size;
   phi->def.bit_size = 1;

   nir_foreach_phi_src(phi_src, phi) {
      b->cursor = nir_after_block_before_jump(phi_src->pred);
      nir_def *src = phi_src->src.ssa;
      if (src == &phi->def)
         continue;
      else if (nir_src_is_undef(phi_src->src))
         src = nir_undef(b, phi->def.num_components, 1);
      else if (type == bool_type_float)
         src = nir_fneu_imm(b, src, 0);
      else
         src = nir_i2b(b, src);

      nir_src_rewrite(&phi_src->src, src);
   }

   b->cursor = nir_after_phis(phi->instr.block);

   nir_def *res = &phi->def;
   if (type == bool_type_single_bit)
      res = nir_b2iN(b, res, bit_size);
   else if (type == bool_type_all_bits)
      res = nir_bcsel(b, res, nir_imm_intN_t(b, -1, bit_size), nir_imm_intN_t(b, 0, bit_size));
   else if (type == bool_type_float)
      res = nir_b2fN(b, res, bit_size);
   else
      unreachable("invalid bool_type");

   nir_foreach_use_safe(src, &phi->def) {
      if (nir_src_parent_instr(src) == &phi->instr ||
          nir_src_parent_instr(src) == res->parent_instr)
         continue;
      nir_src_rewrite(src, res);
   }

   return true;
}

bool
nir_opt_phi_to_bool(nir_shader *shader)
{
   nir_instr_worklist *worklist = nir_instr_worklist_create();

   nir_foreach_function_impl(impl, shader) {
      nir_foreach_block(block, impl) {
         nir_block *preheader = block_get_loop_preheader(block);
         nir_foreach_instr(instr, block) {
            if (instr->type == nir_instr_type_phi && preheader) {
               nir_phi_src *phi_src = nir_phi_get_src_from_block(nir_instr_as_phi(instr), preheader);
               instr->pass_flags = src_pass_flags(&phi_src->src);
               /* We only know the types of the preheader phi source
                * so we need to revisit it later if nessecary.
                */
               if (instr->pass_flags)
                  nir_instr_worklist_push_tail(worklist, instr);
            } else {
               instr->pass_flags = get_bool_types(instr);
            }
         }
      }
   }

   nir_foreach_instr_in_worklist(instr, worklist) {
      uint8_t bool_types = get_bool_types(instr);
      if (instr->pass_flags != bool_types) {
         instr->pass_flags = bool_types;
         nir_foreach_use(use, nir_instr_def(instr))
            nir_instr_worklist_push_tail(worklist, nir_src_parent_instr(use));
      }
   }

   nir_instr_worklist_destroy(worklist);

   return nir_shader_phi_pass(shader, phi_to_bool, nir_metadata_control_flow, NULL);
}
