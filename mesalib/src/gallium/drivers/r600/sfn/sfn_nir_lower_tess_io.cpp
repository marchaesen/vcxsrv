/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2022 Collabora LTD
 *
 * Author: Gert Wollny <gert.wollny@collabora.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "sfn_nir.h"

bool r600_lower_tess_io_filter(const nir_instr *instr, gl_shader_stage stage)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *op = nir_instr_as_intrinsic(instr);
   switch (op->intrinsic) {
   case nir_intrinsic_load_input:
      return stage == MESA_SHADER_TESS_CTRL || stage == MESA_SHADER_TESS_EVAL;
   case nir_intrinsic_load_output:
   case nir_intrinsic_load_per_vertex_input:
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_store_per_vertex_output:
   case nir_intrinsic_load_patch_vertices_in:
   case nir_intrinsic_load_tess_level_outer:
   case nir_intrinsic_load_tess_level_inner:
      return true;
   case nir_intrinsic_store_output:
      return stage == MESA_SHADER_TESS_CTRL || stage == MESA_SHADER_VERTEX;
   default:
      ;
   }
   return false;
}

static nir_ssa_def *
emit_load_param_base(nir_builder *b, nir_intrinsic_op op)
{
   nir_intrinsic_instr *result = nir_intrinsic_instr_create(b->shader, op);
	nir_ssa_dest_init(&result->instr, &result->dest,
                     4, 32, NULL);
   nir_builder_instr_insert(b, &result->instr);
   return &result->dest.ssa;
}

static int get_tcs_varying_offset(nir_intrinsic_instr *op)
{
   unsigned location = nir_intrinsic_io_semantics(op).location;

   switch (location) {
   case VARYING_SLOT_POS:
      return 0;
   case VARYING_SLOT_PSIZ:
      return 0x10;
   case VARYING_SLOT_CLIP_DIST0:
      return 0x20;
   case VARYING_SLOT_CLIP_DIST1:
      return 0x30;
   case VARYING_SLOT_TESS_LEVEL_OUTER:
      return 0;
   case VARYING_SLOT_TESS_LEVEL_INNER:
      return 0x10;
   default:
      if (location >= VARYING_SLOT_VAR0 &&
          location <= VARYING_SLOT_VAR31)
         return 0x10 * (location - VARYING_SLOT_VAR0) + 0x40;

      if (location >=  VARYING_SLOT_PATCH0) {
         return 0x10 * (location - VARYING_SLOT_PATCH0) + 0x20;
      }
   }
   return 0;
}

static inline nir_ssa_def *
r600_umad_24(nir_builder *b, nir_ssa_def *op1, nir_ssa_def *op2, nir_ssa_def *op3)
{
   return nir_build_alu(b, nir_op_umad24, op1, op2, op3, NULL);
}

static inline nir_ssa_def *
r600_tcs_base_address(nir_builder *b, nir_ssa_def *param_base, nir_ssa_def *rel_patch_id)
{
   return r600_umad_24(b,  nir_channel(b, param_base, 0),
                       rel_patch_id,
                       nir_channel(b, param_base, 3));
}


static nir_ssa_def *
emil_lsd_in_addr(nir_builder *b, nir_ssa_def *base, nir_ssa_def *patch_id, nir_intrinsic_instr *op)
{
   nir_ssa_def *addr = nir_build_alu(b, nir_op_umul24,
                                      nir_channel(b, base, 0),
                                      patch_id, NULL, NULL);

   auto idx1 = nir_src_as_const_value(op->src[0]);
   if (!idx1 || idx1->u32 != 0)
      addr = r600_umad_24(b, nir_channel(b, base, 1),
                          op->src[0].ssa, addr);

   auto offset = nir_imm_int(b, get_tcs_varying_offset(op));

   auto idx2 = nir_src_as_const_value(op->src[1]);
   if (!idx2 || idx2->u32 != 0)
      offset = nir_iadd(b, nir_ishl(b, op->src[1].ssa, nir_imm_int(b, 4)), offset);

   return nir_iadd(b, addr, offset);
}

static nir_ssa_def *
emil_lsd_out_addr(nir_builder *b, nir_ssa_def *base, nir_ssa_def *patch_id, nir_intrinsic_instr *op,
                  UNUSED nir_variable_mode mode, int src_offset)
{

   nir_ssa_def *addr1 = r600_umad_24(b, nir_channel(b, base, 0),
                                     patch_id,
                                     nir_channel(b, base, 2));
   nir_ssa_def *addr2 = r600_umad_24(b, nir_channel(b, base, 1),
                                     op->src[src_offset].ssa, addr1);
   int offset = get_tcs_varying_offset(op);
   return nir_iadd(b, nir_iadd(b, addr2,
                               nir_ishl(b, op->src[src_offset + 1].ssa, nir_imm_int(b,4))),
                               nir_imm_int(b, offset));
}

static nir_ssa_def *load_offset_group(nir_builder *b, int ncomponents)
{
   switch (ncomponents) {
   /* tess outer offsets */
   case 1: return nir_imm_int(b, 0);
   case 2: return nir_imm_ivec2(b, 0, 4);
   case 3: return r600_imm_ivec3(b, 0, 4, 8);
   case 4: return nir_imm_ivec4(b, 0, 4, 8, 12);
      /* tess inner offsets */
   case 5: return nir_imm_int(b, 16);
   case 6: return nir_imm_ivec2(b, 16, 20);
   default:
      debug_printf("Got %d components\n", ncomponents);
      unreachable("Unsupported component count");
   }
}

static nir_ssa_def *load_offset_group_from_mask(nir_builder *b, uint32_t mask)
{
   auto full_mask = nir_imm_ivec4(b, 0, 4, 8, 12);
   return nir_channels(b, full_mask, mask);
}

struct MaskQuery {
   uint32_t mask;
   uint32_t ssa_index;
   nir_alu_instr *alu;
   int index;
   uint32_t full_mask;
};

static bool update_alu_mask(nir_src *src, void *data)
{
   auto mq = reinterpret_cast<MaskQuery *>(data);

   if (mq->ssa_index == src->ssa->index) {
      mq->mask |= nir_alu_instr_src_read_mask(mq->alu, mq->index);
   }
   ++mq->index;

   return mq->mask != mq->full_mask;
}

static uint32_t get_dest_usee_mask(nir_intrinsic_instr *op)
{
   assert(op->dest.is_ssa);

   MaskQuery mq = {0};
   mq.full_mask = (1 << nir_dest_num_components(op->dest)) - 1;

   nir_foreach_use(use_src,  &op->dest.ssa) {
      auto use_instr = use_src->parent_instr;
      mq.ssa_index = use_src->ssa->index;

      switch (use_instr->type) {
      case nir_instr_type_alu: {
         mq.alu = nir_instr_as_alu(use_instr);
         mq.index = 0;
         if (!nir_foreach_src(use_instr, update_alu_mask, &mq))
            return 0xf;
         break;
      }
      case nir_instr_type_intrinsic:  {
         auto intr = nir_instr_as_intrinsic(use_instr);
         switch (intr->intrinsic) {
         case nir_intrinsic_store_output:
         case nir_intrinsic_store_per_vertex_output:
            mq.mask |= nir_intrinsic_write_mask(intr) << nir_intrinsic_component(intr);
            break;
         case nir_intrinsic_store_scratch:
         case nir_intrinsic_store_local_shared_r600:
            mq.mask |= nir_intrinsic_write_mask(intr);
            break;
         default:
            return 0xf;
         }
         break;
      }
      default:
         return 0xf;
      }

   }
   return mq.mask;
}

static void replace_load_instr(nir_builder *b, nir_intrinsic_instr *op, nir_ssa_def *addr)
{
   uint32_t mask = get_dest_usee_mask(op);
   if (mask) {
      nir_ssa_def *addr_outer = nir_iadd(b, addr, load_offset_group_from_mask(b, mask));
      if (nir_intrinsic_component(op))
         addr_outer = nir_iadd(b, addr_outer, nir_imm_int(b, 4 * nir_intrinsic_component(op)));

      auto new_load = nir_load_local_shared_r600(b, 32, addr_outer);

      auto undef = nir_ssa_undef(b, 1, 32);
      int comps = nir_dest_num_components(op->dest);
      nir_ssa_def *remix[4] = {undef, undef, undef, undef};

      int chan = 0;
      for (int i = 0; i < comps; ++i) {
         if (mask & (1 << i)) {
            remix[i] = nir_channel(b, new_load, chan++);
         }
      }
      auto new_load_remixed = nir_vec(b, remix, comps);
      nir_ssa_def_rewrite_uses(&op->dest.ssa, new_load_remixed);
   }
   nir_instr_remove(&op->instr);
}

static nir_ssa_def *
r600_load_rel_patch_id(nir_builder *b)
{
   auto patch_id = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_tcs_rel_patch_id_r600);
   nir_ssa_dest_init(&patch_id->instr, &patch_id->dest,
                     1, 32, NULL);
   nir_builder_instr_insert(b, &patch_id->instr);
   return &patch_id->dest.ssa;
}

static void
emit_store_lds(nir_builder *b, nir_intrinsic_instr *op, nir_ssa_def *addr)
{
   uint32_t orig_writemask = nir_intrinsic_write_mask(op) << nir_intrinsic_component(op);

   for (int i = 0; i < 2; ++i) {
      unsigned test_mask = (0x3 << 2 * i);
      unsigned wmask = orig_writemask & test_mask;
      if (!(wmask))
         continue;

      uint32_t writemask =  wmask >> nir_intrinsic_component(op);

      auto store_tcs_out = nir_intrinsic_instr_create(b->shader, nir_intrinsic_store_local_shared_r600);
      nir_intrinsic_set_write_mask(store_tcs_out, writemask);
      store_tcs_out->src[0] = nir_src_for_ssa(op->src[0].ssa);
      store_tcs_out->num_components = store_tcs_out->src[0].ssa->num_components;
      bool start_even = (orig_writemask & (1u << (2 * i)));

      auto addr2 = nir_iadd(b, addr, nir_imm_int(b, 8 * i + (start_even ? 0 : 4)));
      store_tcs_out->src[1] = nir_src_for_ssa(addr2);

      nir_builder_instr_insert(b, &store_tcs_out->instr);
   }
}

static nir_ssa_def *
emil_tcs_io_offset(nir_builder *b, nir_ssa_def *addr, nir_intrinsic_instr *op, int src_offset)
{
   int offset = get_tcs_varying_offset(op);
   return nir_iadd(b, nir_iadd(b, addr,
                               nir_ishl(b, op->src[src_offset].ssa, nir_imm_int(b,4))),
                               nir_imm_int(b, offset));
}


inline unsigned
outer_tf_components(pipe_prim_type prim_type)
{
   switch (prim_type) {
   case PIPE_PRIM_LINES: return 2;
   case PIPE_PRIM_TRIANGLES: return 3;
   case PIPE_PRIM_QUADS: return 4;
   default:
      return 0;
   }
}



static bool
r600_lower_tess_io_impl(nir_builder *b, nir_instr *instr, enum pipe_prim_type prim_type)
{
   static nir_ssa_def *load_in_param_base = nullptr;
   static nir_ssa_def *load_out_param_base = nullptr;

   b->cursor = nir_before_instr(instr);
   nir_intrinsic_instr *op = nir_instr_as_intrinsic(instr);

   if (b->shader->info.stage == MESA_SHADER_TESS_CTRL) {
      load_in_param_base = emit_load_param_base(b, nir_intrinsic_load_tcs_in_param_base_r600);
      load_out_param_base = emit_load_param_base(b, nir_intrinsic_load_tcs_out_param_base_r600);
   } else if (b->shader->info.stage == MESA_SHADER_TESS_EVAL) {
      load_in_param_base = emit_load_param_base(b, nir_intrinsic_load_tcs_out_param_base_r600);
   } else if (b->shader->info.stage == MESA_SHADER_VERTEX) {
      load_out_param_base = emit_load_param_base(b, nir_intrinsic_load_tcs_in_param_base_r600);
   }

   auto rel_patch_id = r600_load_rel_patch_id(b);

   unsigned tf_inner_address_offset = 0;
   unsigned ncomps_correct = 0;

   switch (op->intrinsic) {
   case nir_intrinsic_load_patch_vertices_in: {
      nir_ssa_def *vertices_in;
      if (b->shader->info.stage == MESA_SHADER_TESS_CTRL)
         vertices_in = nir_channel(b, load_in_param_base, 2);
      else {
         auto base = emit_load_param_base(b, nir_intrinsic_load_tcs_in_param_base_r600);
         vertices_in = nir_channel(b, base, 2);
      }
      nir_ssa_def_rewrite_uses(&op->dest.ssa, vertices_in);
      nir_instr_remove(&op->instr);
      return true;
   }
   case nir_intrinsic_load_per_vertex_input: {
      nir_ssa_def *addr =
            b->shader->info.stage == MESA_SHADER_TESS_CTRL ?
               emil_lsd_in_addr(b, load_in_param_base, rel_patch_id, op) :
               emil_lsd_out_addr(b, load_in_param_base, rel_patch_id, op, nir_var_shader_in, 0);
      replace_load_instr(b, op, addr);
      return true;
   }
   case nir_intrinsic_store_per_vertex_output: {
      nir_ssa_def *addr = emil_lsd_out_addr(b, load_out_param_base, rel_patch_id, op, nir_var_shader_out, 1);
      emit_store_lds(b, op, addr);
      nir_instr_remove(instr);
      return true;
   }
   case nir_intrinsic_load_per_vertex_output: {
      nir_ssa_def *addr = emil_lsd_out_addr(b, load_out_param_base, rel_patch_id, op, nir_var_shader_out, 0);
      replace_load_instr(b, op, addr);
      return true;
   }
   case nir_intrinsic_store_output: {
      nir_ssa_def *addr = (b->shader->info.stage == MESA_SHADER_TESS_CTRL) ?
                             r600_tcs_base_address(b, load_out_param_base, rel_patch_id):
                             nir_build_alu(b, nir_op_umul24,
                                           nir_channel(b, load_out_param_base, 1),
                                           rel_patch_id, NULL, NULL);
      addr = emil_tcs_io_offset(b, addr, op, 1);
      emit_store_lds(b, op, addr);
      nir_instr_remove(instr);
      return true;
   }
   case nir_intrinsic_load_output: {
      nir_ssa_def *addr = r600_tcs_base_address(b, load_out_param_base, rel_patch_id);
      addr = emil_tcs_io_offset(b, addr, op, 0);
      replace_load_instr(b, op, addr);
      return true;
   }
   case nir_intrinsic_load_input: {
      nir_ssa_def *addr = r600_tcs_base_address(b, load_in_param_base, rel_patch_id);
      addr = emil_tcs_io_offset(b, addr, op, 0);
      replace_load_instr(b, op, addr);
      return true;
   }
   case nir_intrinsic_load_tess_level_inner:
      tf_inner_address_offset = 4;
      ncomps_correct = 2;
      FALLTHROUGH;
   case nir_intrinsic_load_tess_level_outer: {
      auto ncomps = outer_tf_components(prim_type);
      if (!ncomps)
         return false;
      ncomps -= ncomps_correct;
      auto base = emit_load_param_base(b, nir_intrinsic_load_tcs_out_param_base_r600);
      auto rel_patch_id = r600_load_rel_patch_id(b);
      nir_ssa_def *addr0 = r600_tcs_base_address(b, base, rel_patch_id);
      nir_ssa_def *addr_outer = nir_iadd(b, addr0, load_offset_group(b, tf_inner_address_offset + ncomps));

      auto tf = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_local_shared_r600);
      tf->num_components = ncomps;
      tf->src[0] = nir_src_for_ssa(addr_outer);
      nir_ssa_dest_init(&tf->instr, &tf->dest,
                        tf->num_components, 32, NULL);
      nir_builder_instr_insert(b, &tf->instr);

      nir_ssa_def_rewrite_uses(&op->dest.ssa, &tf->dest.ssa);
      nir_instr_remove(instr);
      return true;
   }
   default:
      ;
   }

   return false;
}

bool r600_lower_tess_io(nir_shader *shader, enum pipe_prim_type prim_type)
{
   bool progress = false;
   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_builder b;
         nir_builder_init(&b, function->impl);

         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type != nir_instr_type_intrinsic)
                  continue;

               if (r600_lower_tess_io_filter(instr, shader->info.stage))
                  progress |= r600_lower_tess_io_impl(&b, instr, prim_type);
            }
         }
      }
   }
   return progress;
}

bool r600_emit_tf(nir_builder *b, nir_ssa_def *val)
{
   nir_intrinsic_instr *store_tf = nir_intrinsic_instr_create(b->shader, nir_intrinsic_store_tf_r600);
   store_tf->num_components = val->num_components;
   store_tf->src[0] = nir_src_for_ssa(val);
   nir_builder_instr_insert(b, &store_tf->instr);
   return true;
}

bool r600_append_tcs_TF_emission(nir_shader *shader, enum pipe_prim_type prim_type) {
   if (shader->info.stage != MESA_SHADER_TESS_CTRL)
      return false;

   nir_foreach_function(function, shader) {
      nir_foreach_block(block, function->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic == nir_intrinsic_store_tf_r600) {
               return false;
            }
         }
      }
   }
   nir_builder builder;
   nir_builder *b = &builder;

   assert(exec_list_length(&shader->functions) == 1);
   nir_function *f = (nir_function *)shader->functions.get_head();
   nir_builder_init(b, f->impl);

   auto outer_comps = outer_tf_components(prim_type);
   if (!outer_comps)
      return false;

   unsigned inner_comps = outer_comps - 2;
   unsigned stride = (inner_comps + outer_comps) * 4;

   b->cursor = nir_after_cf_list(&f->impl->body);

   auto invocation_id = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_invocation_id);
	nir_ssa_dest_init(&invocation_id->instr, &invocation_id->dest,
                     1, 32, NULL);
   nir_builder_instr_insert(b, &invocation_id->instr);

   nir_push_if(b, nir_ieq_imm(b, &invocation_id->dest.ssa, 0));
   auto base = emit_load_param_base(b, nir_intrinsic_load_tcs_out_param_base_r600);
   auto rel_patch_id = r600_load_rel_patch_id(b);

   nir_ssa_def *addr0 = r600_tcs_base_address(b, base, rel_patch_id);

   nir_ssa_def *addr_outer = nir_iadd(b, addr0, load_offset_group(b, outer_comps));
   auto tf_outer = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_local_shared_r600);
   tf_outer->num_components = outer_comps;
   tf_outer->src[0] = nir_src_for_ssa(addr_outer);
   nir_ssa_dest_init(&tf_outer->instr, &tf_outer->dest,
                     tf_outer->num_components, 32, NULL);
   nir_builder_instr_insert(b, &tf_outer->instr);

   std::vector<nir_ssa_def *> tf_out;


   auto tf_out_base = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_tcs_tess_factor_base_r600);
	nir_ssa_dest_init(&tf_out_base->instr, &tf_out_base->dest,
                     1, 32, NULL);
   nir_builder_instr_insert(b, &tf_out_base->instr);

   auto out_addr0 = nir_build_alu(b, nir_op_umad24,
                                  rel_patch_id,
                                  nir_imm_int(b, stride),
                                  &tf_out_base->dest.ssa,
                                  NULL);
   int chanx = 0;
   int chany = 1;

   if (prim_type == PIPE_PRIM_LINES)
      std::swap(chanx, chany);


   auto v0 = nir_vec4(b, out_addr0, nir_channel(b, &tf_outer->dest.ssa, chanx),
                      nir_iadd(b, out_addr0, nir_imm_int(b, 4)),
                      nir_channel(b, &tf_outer->dest.ssa, chany));

   tf_out.push_back(v0);
   if (outer_comps > 2) {
      auto v1 = (outer_comps > 3) ? nir_vec4(b, nir_iadd(b, out_addr0, nir_imm_int(b, 8)),
                                             nir_channel(b, &tf_outer->dest.ssa, 2),
                                             nir_iadd(b, out_addr0, nir_imm_int(b, 12)),
                                             nir_channel(b, &tf_outer->dest.ssa, 3)) :
                                    nir_vec2(b, nir_iadd(b, out_addr0, nir_imm_int(b, 8)),
                                             nir_channel(b, &tf_outer->dest.ssa, 2));
      tf_out.push_back(v1);
   }

   if (inner_comps) {
      nir_ssa_def *addr1 = nir_iadd(b, addr0, load_offset_group(b, 4 + inner_comps));
      auto tf_inner = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_local_shared_r600);
      tf_inner->num_components = inner_comps;
      tf_inner->src[0] = nir_src_for_ssa(addr1);
      nir_ssa_dest_init(&tf_inner->instr, &tf_inner->dest,
                        tf_inner->num_components, 32, NULL);
      nir_builder_instr_insert(b, &tf_inner->instr);

      auto v2 = (inner_comps > 1) ? nir_vec4(b, nir_iadd(b, out_addr0, nir_imm_int(b, 16)),
                                             nir_channel(b, &tf_inner->dest.ssa, 0),
                                             nir_iadd(b, out_addr0, nir_imm_int(b, 20)),
                                             nir_channel(b, &tf_inner->dest.ssa, 1)):
                                    nir_vec2(b, nir_iadd(b, out_addr0, nir_imm_int(b, 12)),
                                             nir_channel(b, &tf_inner->dest.ssa, 0));
      tf_out.push_back(v2);
   }

   for (auto tf: tf_out)
      r600_emit_tf(b, tf);

   nir_pop_if(b, nullptr);

   nir_metadata_preserve(f->impl, nir_metadata_none);

   return true;
}

static bool
r600_lower_tess_coord_filter(const nir_instr *instr, UNUSED const void *_options)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;
   auto intr = nir_instr_as_intrinsic(instr);
   return intr->intrinsic == nir_intrinsic_load_tess_coord;
}

static nir_ssa_def *
r600_lower_tess_coord_impl(nir_builder *b, UNUSED nir_instr *instr, void *_options)
{
   pipe_prim_type prim_type = *(pipe_prim_type *)_options;

   auto tc_xy = nir_load_tess_coord_r600(b);

   auto tc_x = nir_channel(b, tc_xy, 0);
   auto tc_y = nir_channel(b, tc_xy, 1);

   if (prim_type == PIPE_PRIM_TRIANGLES)
      return nir_vec3(b, tc_x, tc_y, nir_fsub(b, nir_imm_float(b, 1.0),
                                              nir_fadd(b, tc_x, tc_y)));
   else
      return nir_vec3(b, tc_x, tc_y, nir_imm_float(b, 0.0));
}


bool r600_lower_tess_coord(nir_shader *sh, enum pipe_prim_type prim_type)
{
   return nir_shader_lower_instructions(sh, r600_lower_tess_coord_filter,
                                        r600_lower_tess_coord_impl, &prim_type);
}
