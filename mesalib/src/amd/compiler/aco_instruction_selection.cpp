/*
 * Copyright © 2018 Valve Corporation
 * Copyright © 2018 Google
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

#include <algorithm>
#include <array>
#include <map>

#include "ac_shader_util.h"
#include "aco_ir.h"
#include "aco_builder.h"
#include "aco_interface.h"
#include "aco_instruction_selection_setup.cpp"
#include "util/fast_idiv_by_const.h"

namespace aco {
namespace {

class loop_info_RAII {
   isel_context* ctx;
   unsigned header_idx_old;
   Block* exit_old;
   bool divergent_cont_old;
   bool divergent_branch_old;
   bool divergent_if_old;

public:
   loop_info_RAII(isel_context* ctx, unsigned loop_header_idx, Block* loop_exit)
      : ctx(ctx),
        header_idx_old(ctx->cf_info.parent_loop.header_idx), exit_old(ctx->cf_info.parent_loop.exit),
        divergent_cont_old(ctx->cf_info.parent_loop.has_divergent_continue),
        divergent_branch_old(ctx->cf_info.parent_loop.has_divergent_branch),
        divergent_if_old(ctx->cf_info.parent_if.is_divergent)
   {
      ctx->cf_info.parent_loop.header_idx = loop_header_idx;
      ctx->cf_info.parent_loop.exit = loop_exit;
      ctx->cf_info.parent_loop.has_divergent_continue = false;
      ctx->cf_info.parent_loop.has_divergent_branch = false;
      ctx->cf_info.parent_if.is_divergent = false;
      ctx->cf_info.loop_nest_depth = ctx->cf_info.loop_nest_depth + 1;
   }

   ~loop_info_RAII()
   {
      ctx->cf_info.parent_loop.header_idx = header_idx_old;
      ctx->cf_info.parent_loop.exit = exit_old;
      ctx->cf_info.parent_loop.has_divergent_continue = divergent_cont_old;
      ctx->cf_info.parent_loop.has_divergent_branch = divergent_branch_old;
      ctx->cf_info.parent_if.is_divergent = divergent_if_old;
      ctx->cf_info.loop_nest_depth = ctx->cf_info.loop_nest_depth - 1;
      if (!ctx->cf_info.loop_nest_depth && !ctx->cf_info.parent_if.is_divergent)
         ctx->cf_info.exec_potentially_empty = false;
   }
};

struct if_context {
   Temp cond;

   bool divergent_old;
   bool exec_potentially_empty_old;

   unsigned BB_if_idx;
   unsigned invert_idx;
   bool then_branch_divergent;
   Block BB_invert;
   Block BB_endif;
};

static void visit_cf_list(struct isel_context *ctx,
                          struct exec_list *list);

static void add_logical_edge(unsigned pred_idx, Block *succ)
{
   succ->logical_preds.emplace_back(pred_idx);
}


static void add_linear_edge(unsigned pred_idx, Block *succ)
{
   succ->linear_preds.emplace_back(pred_idx);
}

static void add_edge(unsigned pred_idx, Block *succ)
{
   add_logical_edge(pred_idx, succ);
   add_linear_edge(pred_idx, succ);
}

static void append_logical_start(Block *b)
{
   Builder(NULL, b).pseudo(aco_opcode::p_logical_start);
}

static void append_logical_end(Block *b)
{
   Builder(NULL, b).pseudo(aco_opcode::p_logical_end);
}

Temp get_ssa_temp(struct isel_context *ctx, nir_ssa_def *def)
{
   assert(ctx->allocated[def->index].id());
   return ctx->allocated[def->index];
}

Temp emit_wqm(isel_context *ctx, Temp src, Temp dst=Temp(0, s1), bool program_needs_wqm = false)
{
   Builder bld(ctx->program, ctx->block);

   if (!dst.id())
      dst = bld.tmp(src.regClass());

   if (ctx->stage != fragment_fs) {
      if (!dst.id())
         return src;

      if (src.type() == RegType::vgpr || src.size() > 1)
         bld.copy(Definition(dst), src);
      else
         bld.sop1(aco_opcode::s_mov_b32, Definition(dst), src);
      return dst;
   }

   bld.pseudo(aco_opcode::p_wqm, Definition(dst), src);
   ctx->program->needs_wqm |= program_needs_wqm;
   return dst;
}

static Temp emit_bpermute(isel_context *ctx, Builder &bld, Temp index, Temp data)
{
   Temp index_x4 = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand(2u), index);

   /* Currently not implemented on GFX6-7 */
   assert(ctx->options->chip_class >= GFX8);

   if (ctx->options->chip_class <= GFX9 || ctx->program->wave_size == 32) {
      return bld.ds(aco_opcode::ds_bpermute_b32, bld.def(v1), index_x4, data);
   }

   /* GFX10, wave64 mode:
    * The bpermute instruction is limited to half-wave operation, which means that it can't
    * properly support subgroup shuffle like older generations (or wave32 mode), so we
    * emulate it here.
    */
   if (!ctx->has_gfx10_wave64_bpermute) {
      ctx->has_gfx10_wave64_bpermute = true;
      ctx->program->config->num_shared_vgprs = 8; /* Shared VGPRs are allocated in groups of 8 */
      ctx->program->vgpr_limit -= 4; /* We allocate 8 shared VGPRs, so we'll have 4 fewer normal VGPRs */
   }

   Temp lane_id = bld.vop3(aco_opcode::v_mbcnt_lo_u32_b32, bld.def(v1), Operand((uint32_t) -1), Operand(0u));
   lane_id = bld.vop3(aco_opcode::v_mbcnt_hi_u32_b32, bld.def(v1), Operand((uint32_t) -1), lane_id);
   Temp lane_is_hi = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(0x20u), lane_id);
   Temp index_is_hi = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(0x20u), index);
   Temp cmp = bld.vopc(aco_opcode::v_cmp_eq_u32, bld.def(s2, vcc), lane_is_hi, index_is_hi);

   return bld.reduction(aco_opcode::p_wave64_bpermute, bld.def(v1), bld.def(s2), bld.def(s1, scc),
                        bld.vcc(cmp), Operand(v2.as_linear()), index_x4, data, gfx10_wave64_bpermute);
}

Temp as_vgpr(isel_context *ctx, Temp val)
{
   if (val.type() == RegType::sgpr) {
      Builder bld(ctx->program, ctx->block);
      return bld.copy(bld.def(RegType::vgpr, val.size()), val);
   }
   assert(val.type() == RegType::vgpr);
   return val;
}

//assumes a != 0xffffffff
void emit_v_div_u32(isel_context *ctx, Temp dst, Temp a, uint32_t b)
{
   assert(b != 0);
   Builder bld(ctx->program, ctx->block);

   if (util_is_power_of_two_or_zero(b)) {
      bld.vop2(aco_opcode::v_lshrrev_b32, Definition(dst), Operand((uint32_t)util_logbase2(b)), a);
      return;
   }

   util_fast_udiv_info info = util_compute_fast_udiv_info(b, 32, 32);

   assert(info.multiplier <= 0xffffffff);

   bool pre_shift = info.pre_shift != 0;
   bool increment = info.increment != 0;
   bool multiply = true;
   bool post_shift = info.post_shift != 0;

   if (!pre_shift && !increment && !multiply && !post_shift) {
      bld.vop1(aco_opcode::v_mov_b32, Definition(dst), a);
      return;
   }

   Temp pre_shift_dst = a;
   if (pre_shift) {
      pre_shift_dst = (increment || multiply || post_shift) ? bld.tmp(v1) : dst;
      bld.vop2(aco_opcode::v_lshrrev_b32, Definition(pre_shift_dst), Operand((uint32_t)info.pre_shift), a);
   }

   Temp increment_dst = pre_shift_dst;
   if (increment) {
      increment_dst = (post_shift || multiply) ? bld.tmp(v1) : dst;
      bld.vadd32(Definition(increment_dst), Operand((uint32_t) info.increment), pre_shift_dst);
   }

   Temp multiply_dst = increment_dst;
   if (multiply) {
      multiply_dst = post_shift ? bld.tmp(v1) : dst;
      bld.vop3(aco_opcode::v_mul_hi_u32, Definition(multiply_dst), increment_dst,
               bld.vop1(aco_opcode::v_mov_b32, bld.def(v1), Operand((uint32_t)info.multiplier)));
   }

   if (post_shift) {
      bld.vop2(aco_opcode::v_lshrrev_b32, Definition(dst), Operand((uint32_t)info.post_shift), multiply_dst);
   }
}

void emit_extract_vector(isel_context* ctx, Temp src, uint32_t idx, Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   bld.pseudo(aco_opcode::p_extract_vector, Definition(dst), src, Operand(idx));
}


Temp emit_extract_vector(isel_context* ctx, Temp src, uint32_t idx, RegClass dst_rc)
{
   /* no need to extract the whole vector */
   if (src.regClass() == dst_rc) {
      assert(idx == 0);
      return src;
   }
   assert(src.size() > idx);
   Builder bld(ctx->program, ctx->block);
   auto it = ctx->allocated_vec.find(src.id());
   /* the size check needs to be early because elements other than 0 may be garbage */
   if (it != ctx->allocated_vec.end() && it->second[0].size() == dst_rc.size()) {
      if (it->second[idx].regClass() == dst_rc) {
         return it->second[idx];
      } else {
         assert(dst_rc.size() == it->second[idx].regClass().size());
         assert(dst_rc.type() == RegType::vgpr && it->second[idx].type() == RegType::sgpr);
         return bld.copy(bld.def(dst_rc), it->second[idx]);
      }
   }

   if (src.size() == dst_rc.size()) {
      assert(idx == 0);
      return bld.copy(bld.def(dst_rc), src);
   } else {
      Temp dst = bld.tmp(dst_rc);
      emit_extract_vector(ctx, src, idx, dst);
      return dst;
   }
}

void emit_split_vector(isel_context* ctx, Temp vec_src, unsigned num_components)
{
   if (num_components == 1)
      return;
   if (ctx->allocated_vec.find(vec_src.id()) != ctx->allocated_vec.end())
      return;
   aco_ptr<Pseudo_instruction> split{create_instruction<Pseudo_instruction>(aco_opcode::p_split_vector, Format::PSEUDO, 1, num_components)};
   split->operands[0] = Operand(vec_src);
   std::array<Temp,4> elems;
   for (unsigned i = 0; i < num_components; i++) {
      elems[i] = {ctx->program->allocateId(), RegClass(vec_src.type(), vec_src.size() / num_components)};
      split->definitions[i] = Definition(elems[i]);
   }
   ctx->block->instructions.emplace_back(std::move(split));
   ctx->allocated_vec.emplace(vec_src.id(), elems);
}

/* This vector expansion uses a mask to determine which elements in the new vector
 * come from the original vector. The other elements are undefined. */
void expand_vector(isel_context* ctx, Temp vec_src, Temp dst, unsigned num_components, unsigned mask)
{
   emit_split_vector(ctx, vec_src, util_bitcount(mask));

   if (vec_src == dst)
      return;

   Builder bld(ctx->program, ctx->block);
   if (num_components == 1) {
      if (dst.type() == RegType::sgpr)
         bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), vec_src);
      else
         bld.copy(Definition(dst), vec_src);
      return;
   }

   unsigned component_size = dst.size() / num_components;
   std::array<Temp,4> elems;

   aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, num_components, 1)};
   vec->definitions[0] = Definition(dst);
   unsigned k = 0;
   for (unsigned i = 0; i < num_components; i++) {
      if (mask & (1 << i)) {
         Temp src = emit_extract_vector(ctx, vec_src, k++, RegClass(vec_src.type(), component_size));
         if (dst.type() == RegType::sgpr)
            src = bld.as_uniform(src);
         vec->operands[i] = Operand(src);
      } else {
         vec->operands[i] = Operand(0u);
      }
      elems[i] = vec->operands[i].getTemp();
   }
   ctx->block->instructions.emplace_back(std::move(vec));
   ctx->allocated_vec.emplace(dst.id(), elems);
}

Temp as_divergent_bool(isel_context *ctx, Temp val, bool vcc_hint)
{
   if (val.regClass() == s2) {
      return val;
   } else {
      assert(val.regClass() == s1);
      Builder bld(ctx->program, ctx->block);
      Definition& def = bld.sop2(aco_opcode::s_cselect_b64, bld.def(s2),
                                 Operand((uint32_t) -1), Operand(0u), bld.scc(val)).def(0);
      if (vcc_hint)
         def.setHint(vcc);
      return def.getTemp();
   }
}

Temp as_uniform_bool(isel_context *ctx, Temp val)
{
   if (val.regClass() == s1) {
      return val;
   } else {
      assert(val.regClass() == s2);
      Builder bld(ctx->program, ctx->block);
      /* if we're currently in WQM mode, ensure that the source is also computed in WQM */
      return bld.sopc(aco_opcode::s_cmp_lg_u64, bld.def(s1, scc), Operand(0u), emit_wqm(ctx, val));
   }
}

Temp get_alu_src(struct isel_context *ctx, nir_alu_src src, unsigned size=1)
{
   if (src.src.ssa->num_components == 1 && src.swizzle[0] == 0 && size == 1)
      return get_ssa_temp(ctx, src.src.ssa);

   if (src.src.ssa->num_components == size) {
      bool identity_swizzle = true;
      for (unsigned i = 0; identity_swizzle && i < size; i++) {
         if (src.swizzle[i] != i)
            identity_swizzle = false;
      }
      if (identity_swizzle)
         return get_ssa_temp(ctx, src.src.ssa);
   }

   Temp vec = get_ssa_temp(ctx, src.src.ssa);
   unsigned elem_size = vec.size() / src.src.ssa->num_components;
   assert(elem_size > 0); /* TODO: 8 and 16-bit vectors not supported */
   assert(vec.size() % elem_size == 0);

   RegClass elem_rc = RegClass(vec.type(), elem_size);
   if (size == 1) {
      return emit_extract_vector(ctx, vec, src.swizzle[0], elem_rc);
   } else {
      assert(size <= 4);
      std::array<Temp,4> elems;
      aco_ptr<Pseudo_instruction> vec_instr{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, size, 1)};
      for (unsigned i = 0; i < size; ++i) {
         elems[i] = emit_extract_vector(ctx, vec, src.swizzle[i], elem_rc);
         vec_instr->operands[i] = Operand{elems[i]};
      }
      Temp dst{ctx->program->allocateId(), RegClass(vec.type(), elem_size * size)};
      vec_instr->definitions[0] = Definition(dst);
      ctx->block->instructions.emplace_back(std::move(vec_instr));
      ctx->allocated_vec.emplace(dst.id(), elems);
      return dst;
   }
}

Temp convert_pointer_to_64_bit(isel_context *ctx, Temp ptr)
{
   if (ptr.size() == 2)
      return ptr;
   Builder bld(ctx->program, ctx->block);
   if (ptr.type() == RegType::vgpr)
      ptr = bld.vop1(aco_opcode::v_readfirstlane_b32, bld.def(s1), ptr);
   return bld.pseudo(aco_opcode::p_create_vector, bld.def(s2),
                     ptr, Operand((unsigned)ctx->options->address32_hi));
}

void emit_sop2_instruction(isel_context *ctx, nir_alu_instr *instr, aco_opcode op, Temp dst, bool writes_scc)
{
   aco_ptr<SOP2_instruction> sop2{create_instruction<SOP2_instruction>(op, Format::SOP2, 2, writes_scc ? 2 : 1)};
   sop2->operands[0] = Operand(get_alu_src(ctx, instr->src[0]));
   sop2->operands[1] = Operand(get_alu_src(ctx, instr->src[1]));
   sop2->definitions[0] = Definition(dst);
   if (writes_scc)
      sop2->definitions[1] = Definition(ctx->program->allocateId(), scc, s1);
   ctx->block->instructions.emplace_back(std::move(sop2));
}

void emit_vop2_instruction(isel_context *ctx, nir_alu_instr *instr, aco_opcode op, Temp dst, bool commutative, bool swap_srcs=false)
{
   Builder bld(ctx->program, ctx->block);
   Temp src0 = get_alu_src(ctx, instr->src[swap_srcs ? 1 : 0]);
   Temp src1 = get_alu_src(ctx, instr->src[swap_srcs ? 0 : 1]);
   if (src1.type() == RegType::sgpr) {
      if (commutative && src0.type() == RegType::vgpr) {
         Temp t = src0;
         src0 = src1;
         src1 = t;
      } else if (src0.type() == RegType::vgpr &&
                 op != aco_opcode::v_madmk_f32 &&
                 op != aco_opcode::v_madak_f32 &&
                 op != aco_opcode::v_madmk_f16 &&
                 op != aco_opcode::v_madak_f16) {
         /* If the instruction is not commutative, we emit a VOP3A instruction */
         bld.vop2_e64(op, Definition(dst), src0, src1);
         return;
      } else {
         src1 = bld.copy(bld.def(RegType::vgpr, src1.size()), src1); //TODO: as_vgpr
      }
   }
   bld.vop2(op, Definition(dst), src0, src1);
}

void emit_vop3a_instruction(isel_context *ctx, nir_alu_instr *instr, aco_opcode op, Temp dst)
{
   Temp src0 = get_alu_src(ctx, instr->src[0]);
   Temp src1 = get_alu_src(ctx, instr->src[1]);
   Temp src2 = get_alu_src(ctx, instr->src[2]);

   /* ensure that the instruction has at most 1 sgpr operand
    * The optimizer will inline constants for us */
   if (src0.type() == RegType::sgpr && src1.type() == RegType::sgpr)
      src0 = as_vgpr(ctx, src0);
   if (src1.type() == RegType::sgpr && src2.type() == RegType::sgpr)
      src1 = as_vgpr(ctx, src1);
   if (src2.type() == RegType::sgpr && src0.type() == RegType::sgpr)
      src2 = as_vgpr(ctx, src2);

   Builder bld(ctx->program, ctx->block);
   bld.vop3(op, Definition(dst), src0, src1, src2);
}

void emit_vop1_instruction(isel_context *ctx, nir_alu_instr *instr, aco_opcode op, Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   bld.vop1(op, Definition(dst), get_alu_src(ctx, instr->src[0]));
}

void emit_vopc_instruction(isel_context *ctx, nir_alu_instr *instr, aco_opcode op, Temp dst)
{
   Temp src0 = get_alu_src(ctx, instr->src[0]);
   Temp src1 = get_alu_src(ctx, instr->src[1]);
   aco_ptr<Instruction> vopc;
   if (src1.type() == RegType::sgpr) {
      if (src0.type() == RegType::vgpr) {
         /* to swap the operands, we might also have to change the opcode */
         switch (op) {
            case aco_opcode::v_cmp_lt_f32:
               op = aco_opcode::v_cmp_gt_f32;
               break;
            case aco_opcode::v_cmp_ge_f32:
               op = aco_opcode::v_cmp_le_f32;
               break;
            case aco_opcode::v_cmp_lt_i32:
               op = aco_opcode::v_cmp_gt_i32;
               break;
            case aco_opcode::v_cmp_ge_i32:
               op = aco_opcode::v_cmp_le_i32;
               break;
            case aco_opcode::v_cmp_lt_u32:
               op = aco_opcode::v_cmp_gt_u32;
               break;
            case aco_opcode::v_cmp_ge_u32:
               op = aco_opcode::v_cmp_le_u32;
               break;
            case aco_opcode::v_cmp_lt_f64:
               op = aco_opcode::v_cmp_gt_f64;
               break;
            case aco_opcode::v_cmp_ge_f64:
               op = aco_opcode::v_cmp_le_f64;
               break;
            case aco_opcode::v_cmp_lt_i64:
               op = aco_opcode::v_cmp_gt_i64;
               break;
            case aco_opcode::v_cmp_ge_i64:
               op = aco_opcode::v_cmp_le_i64;
               break;
            case aco_opcode::v_cmp_lt_u64:
               op = aco_opcode::v_cmp_gt_u64;
               break;
            case aco_opcode::v_cmp_ge_u64:
               op = aco_opcode::v_cmp_le_u64;
               break;
            default: /* eq and ne are commutative */
               break;
         }
         Temp t = src0;
         src0 = src1;
         src1 = t;
      } else {
         src1 = as_vgpr(ctx, src1);
      }
   }
   Builder bld(ctx->program, ctx->block);
   bld.vopc(op, Definition(dst), src0, src1).def(0).setHint(vcc);
}

void emit_comparison(isel_context *ctx, nir_alu_instr *instr, aco_opcode op, Temp dst)
{
   if (dst.regClass() == s2) {
      emit_vopc_instruction(ctx, instr, op, dst);
      if (!ctx->divergent_vals[instr->dest.dest.ssa.index])
         emit_split_vector(ctx, dst, 2);
   } else if (dst.regClass() == s1) {
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      assert(src0.type() == RegType::sgpr && src1.type() == RegType::sgpr);

      Builder bld(ctx->program, ctx->block);
      bld.sopc(op, bld.scc(Definition(dst)), src0, src1);

   } else {
      assert(false);
   }
}

void emit_boolean_logic(isel_context *ctx, nir_alu_instr *instr, aco_opcode op32, aco_opcode op64, Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   Temp src0 = get_alu_src(ctx, instr->src[0]);
   Temp src1 = get_alu_src(ctx, instr->src[1]);
   if (dst.regClass() == s2) {
      bld.sop2(op64, Definition(dst), bld.def(s1, scc),
               as_divergent_bool(ctx, src0, false), as_divergent_bool(ctx, src1, false));
   } else {
      assert(dst.regClass() == s1);
      bld.sop2(op32, bld.def(s1), bld.scc(Definition(dst)),
               as_uniform_bool(ctx, src0), as_uniform_bool(ctx, src1));
   }
}


void emit_bcsel(isel_context *ctx, nir_alu_instr *instr, Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   Temp cond = get_alu_src(ctx, instr->src[0]);
   Temp then = get_alu_src(ctx, instr->src[1]);
   Temp els = get_alu_src(ctx, instr->src[2]);

   if (dst.type() == RegType::vgpr) {
      cond = as_divergent_bool(ctx, cond, true);

      aco_ptr<Instruction> bcsel;
      if (dst.size() == 1) {
         then = as_vgpr(ctx, then);
         els = as_vgpr(ctx, els);

         bld.vop2(aco_opcode::v_cndmask_b32, Definition(dst), els, then, cond);
      } else if (dst.size() == 2) {
         Temp then_lo = bld.tmp(v1), then_hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(then_lo), Definition(then_hi), then);
         Temp else_lo = bld.tmp(v1), else_hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(else_lo), Definition(else_hi), els);

         Temp dst0 = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), else_lo, then_lo, cond);
         Temp dst1 = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), else_hi, then_hi, cond);

         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      return;
   }

   if (instr->dest.dest.ssa.bit_size != 1) { /* uniform condition and values in sgpr */
      if (dst.regClass() == s1 || dst.regClass() == s2) {
         assert((then.regClass() == s1 || then.regClass() == s2) && els.regClass() == then.regClass());
         aco_opcode op = dst.regClass() == s1 ? aco_opcode::s_cselect_b32 : aco_opcode::s_cselect_b64;
         bld.sop2(op, Definition(dst), then, els, bld.scc(as_uniform_bool(ctx, cond)));
      } else {
         fprintf(stderr, "Unimplemented uniform bcsel bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      return;
   }

   /* boolean bcsel */
   assert(instr->dest.dest.ssa.bit_size == 1);

   if (dst.regClass() == s1)
      cond = as_uniform_bool(ctx, cond);

   if (cond.regClass() == s1) { /* uniform selection */
      aco_opcode op;
      if (dst.regClass() == s2) {
         op = aco_opcode::s_cselect_b64;
         then = as_divergent_bool(ctx, then, false);
         els = as_divergent_bool(ctx, els, false);
      } else {
         assert(dst.regClass() == s1);
         op = aco_opcode::s_cselect_b32;
         then = as_uniform_bool(ctx, then);
         els = as_uniform_bool(ctx, els);
      }
      bld.sop2(op, Definition(dst), then, els, bld.scc(cond));
      return;
   }

   /* divergent boolean bcsel
    * this implements bcsel on bools: dst = s0 ? s1 : s2
    * are going to be: dst = (s0 & s1) | (~s0 & s2) */
   assert (dst.regClass() == s2);
   then = as_divergent_bool(ctx, then, false);
   els = as_divergent_bool(ctx, els, false);

   if (cond.id() != then.id())
      then = bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.def(s1, scc), cond, then);

   if (cond.id() == els.id())
      bld.sop1(aco_opcode::s_mov_b64, Definition(dst), then);
   else
      bld.sop2(aco_opcode::s_or_b64, Definition(dst), bld.def(s1, scc), then,
               bld.sop2(aco_opcode::s_andn2_b64, bld.def(s2), bld.def(s1, scc), els, cond));
}

void visit_alu_instr(isel_context *ctx, nir_alu_instr *instr)
{
   if (!instr->dest.dest.is_ssa) {
      fprintf(stderr, "nir alu dst not in ssa: ");
      nir_print_instr(&instr->instr, stderr);
      fprintf(stderr, "\n");
      abort();
   }
   Builder bld(ctx->program, ctx->block);
   Temp dst = get_ssa_temp(ctx, &instr->dest.dest.ssa);
   switch(instr->op) {
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4: {
      std::array<Temp,4> elems;
      aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, instr->dest.dest.ssa.num_components, 1)};
      for (unsigned i = 0; i < instr->dest.dest.ssa.num_components; ++i) {
         elems[i] = get_alu_src(ctx, instr->src[i]);
         vec->operands[i] = Operand{elems[i]};
      }
      vec->definitions[0] = Definition(dst);
      ctx->block->instructions.emplace_back(std::move(vec));
      ctx->allocated_vec.emplace(dst.id(), elems);
      break;
   }
   case nir_op_mov: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      aco_ptr<Instruction> mov;
      if (dst.type() == RegType::sgpr) {
         if (src.type() == RegType::vgpr)
            bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), src);
         else if (src.regClass() == s1)
            bld.sop1(aco_opcode::s_mov_b32, Definition(dst), src);
         else if (src.regClass() == s2)
            bld.sop1(aco_opcode::s_mov_b64, Definition(dst), src);
         else
            unreachable("wrong src register class for nir_op_imov");
      } else if (dst.regClass() == v1) {
         bld.vop1(aco_opcode::v_mov_b32, Definition(dst), src);
      } else if (dst.regClass() == v2) {
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), src);
      } else {
         nir_print_instr(&instr->instr, stderr);
         unreachable("Should have been lowered to scalar.");
      }
      break;
   }
   case nir_op_inot: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      /* uniform booleans */
      if (instr->dest.dest.ssa.bit_size == 1 && dst.regClass() == s1) {
         if (src.regClass() == s1) {
            /* in this case, src is either 1 or 0 */
            bld.sop2(aco_opcode::s_xor_b32, bld.def(s1), bld.scc(Definition(dst)), Operand(1u), src);
         } else {
            /* src is either exec_mask or 0 */
            assert(src.regClass() == s2);
            bld.sopc(aco_opcode::s_cmp_eq_u64, bld.scc(Definition(dst)), Operand(0u), src);
         }
      } else if (dst.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_not_b32, dst);
      } else if (dst.type() == RegType::sgpr) {
         aco_opcode opcode = dst.size() == 1 ? aco_opcode::s_not_b32 : aco_opcode::s_not_b64;
         bld.sop1(opcode, Definition(dst), bld.def(s1, scc), src);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_ineg: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == v1) {
         bld.vsub32(Definition(dst), Operand(0u), Operand(src));
      } else if (dst.regClass() == s1) {
         bld.sop2(aco_opcode::s_mul_i32, Definition(dst), Operand((uint32_t) -1), src);
      } else if (dst.size() == 2) {
         Temp src0 = bld.tmp(dst.type(), 1);
         Temp src1 = bld.tmp(dst.type(), 1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(src0), Definition(src1), src);

         if (dst.regClass() == s2) {
            Temp carry = bld.tmp(s1);
            Temp dst0 = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.scc(Definition(carry)), Operand(0u), src0);
            Temp dst1 = bld.sop2(aco_opcode::s_subb_u32, bld.def(s1), bld.def(s1, scc), Operand(0u), src1, carry);
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
         } else {
            Temp lower = bld.tmp(v1);
            Temp borrow = bld.vsub32(Definition(lower), Operand(0u), src0, true).def(1).getTemp();
            Temp upper = bld.vsub32(bld.def(v1), Operand(0u), src1, false, borrow);
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);
         }
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_iabs: {
      if (dst.regClass() == s1) {
         bld.sop1(aco_opcode::s_abs_i32, Definition(dst), bld.def(s1, scc), get_alu_src(ctx, instr->src[0]));
      } else if (dst.regClass() == v1) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         bld.vop2(aco_opcode::v_max_i32, Definition(dst), src, bld.vsub32(bld.def(v1), Operand(0u), src));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_isign: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == s1) {
         Temp tmp = bld.sop2(aco_opcode::s_ashr_i32, bld.def(s1), bld.def(s1, scc), src, Operand(31u));
         Temp gtz = bld.sopc(aco_opcode::s_cmp_gt_i32, bld.def(s1, scc), src, Operand(0u));
         bld.sop2(aco_opcode::s_add_i32, Definition(dst), bld.def(s1, scc), gtz, tmp);
      } else if (dst.regClass() == s2) {
         Temp neg = bld.sop2(aco_opcode::s_ashr_i64, bld.def(s2), bld.def(s1, scc), src, Operand(63u));
         Temp neqz = bld.sopc(aco_opcode::s_cmp_lg_u64, bld.def(s1, scc), src, Operand(0u));
         bld.sop2(aco_opcode::s_or_b64, Definition(dst), bld.def(s1, scc), neg, neqz);
      } else if (dst.regClass() == v1) {
         Temp tmp = bld.vop2(aco_opcode::v_ashrrev_i32, bld.def(v1), Operand(31u), src);
         Temp gtz = bld.vopc(aco_opcode::v_cmp_ge_i32, bld.hint_vcc(bld.def(s2)), Operand(0u), src);
         bld.vop2(aco_opcode::v_cndmask_b32, Definition(dst), Operand(1u), tmp, gtz);
      } else if (dst.regClass() == v2) {
         Temp upper = emit_extract_vector(ctx, src, 1, v1);
         Temp neg = bld.vop2(aco_opcode::v_ashrrev_i32, bld.def(v1), Operand(31u), upper);
         Temp gtz = bld.vopc(aco_opcode::v_cmp_ge_i64, bld.hint_vcc(bld.def(s2)), Operand(0u), src);
         Temp lower = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), Operand(1u), neg, gtz);
         upper = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), Operand(0u), neg, gtz);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_imax: {
      if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_i32, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_max_i32, dst, true);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_umax: {
      if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_u32, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_max_u32, dst, true);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_imin: {
      if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_i32, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_min_i32, dst, true);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_umin: {
      if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_u32, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_min_u32, dst, true);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_ior: {
      if (instr->dest.dest.ssa.bit_size == 1) {
         emit_boolean_logic(ctx, instr, aco_opcode::s_or_b32, aco_opcode::s_or_b64, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_or_b32, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_or_b32, dst, true);
      } else if (dst.regClass() == s2) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_or_b64, dst, true);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_iand: {
      if (instr->dest.dest.ssa.bit_size == 1) {
         emit_boolean_logic(ctx, instr, aco_opcode::s_and_b32, aco_opcode::s_and_b64, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_and_b32, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_and_b32, dst, true);
      } else if (dst.regClass() == s2) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_and_b64, dst, true);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_ixor: {
      if (instr->dest.dest.ssa.bit_size == 1) {
         emit_boolean_logic(ctx, instr, aco_opcode::s_xor_b32, aco_opcode::s_xor_b64, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_xor_b32, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_xor_b32, dst, true);
      } else if (dst.regClass() == s2) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_xor_b64, dst, true);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_ushr: {
      if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_lshrrev_b32, dst, false, true);
      } else if (dst.regClass() == v2) {
         bld.vop3(aco_opcode::v_lshrrev_b64, Definition(dst),
                  get_alu_src(ctx, instr->src[1]), get_alu_src(ctx, instr->src[0]));
      } else if (dst.regClass() == s2) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_lshr_b64, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_lshr_b32, dst, true);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_ishl: {
      if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_lshlrev_b32, dst, false, true);
      } else if (dst.regClass() == v2) {
         bld.vop3(aco_opcode::v_lshlrev_b64, Definition(dst),
                  get_alu_src(ctx, instr->src[1]), get_alu_src(ctx, instr->src[0]));
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_lshl_b32, dst, true);
      } else if (dst.regClass() == s2) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_lshl_b64, dst, true);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_ishr: {
      if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_ashrrev_i32, dst, false, true);
      } else if (dst.regClass() == v2) {
         bld.vop3(aco_opcode::v_ashrrev_i64, Definition(dst),
                  get_alu_src(ctx, instr->src[1]), get_alu_src(ctx, instr->src[0]));
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_ashr_i32, dst, true);
      } else if (dst.regClass() == s2) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_ashr_i64, dst, true);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_find_lsb: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (src.regClass() == s1) {
         bld.sop1(aco_opcode::s_ff1_i32_b32, Definition(dst), src);
      } else if (src.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_ffbl_b32, dst);
      } else if (src.regClass() == s2) {
         bld.sop1(aco_opcode::s_ff1_i32_b64, Definition(dst), src);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_ufind_msb:
   case nir_op_ifind_msb: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (src.regClass() == s1 || src.regClass() == s2) {
         aco_opcode op = src.regClass() == s2 ?
                         (instr->op == nir_op_ufind_msb ? aco_opcode::s_flbit_i32_b64 : aco_opcode::s_flbit_i32_i64) :
                         (instr->op == nir_op_ufind_msb ? aco_opcode::s_flbit_i32_b32 : aco_opcode::s_flbit_i32);
         Temp msb_rev = bld.sop1(op, bld.def(s1), src);

         Builder::Result sub = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.def(s1, scc),
                                        Operand(src.size() * 32u - 1u), msb_rev);
         Temp msb = sub.def(0).getTemp();
         Temp carry = sub.def(1).getTemp();

         bld.sop2(aco_opcode::s_cselect_b32, Definition(dst), Operand((uint32_t)-1), msb, carry);
      } else if (src.regClass() == v1) {
         aco_opcode op = instr->op == nir_op_ufind_msb ? aco_opcode::v_ffbh_u32 : aco_opcode::v_ffbh_i32;
         Temp msb_rev = bld.tmp(v1);
         emit_vop1_instruction(ctx, instr, op, msb_rev);
         Temp msb = bld.tmp(v1);
         Temp carry = bld.vsub32(Definition(msb), Operand(31u), Operand(msb_rev), true).def(1).getTemp();
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), msb, Operand((uint32_t)-1), carry);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_bitfield_reverse: {
      if (dst.regClass() == s1) {
         bld.sop1(aco_opcode::s_brev_b32, Definition(dst), get_alu_src(ctx, instr->src[0]));
      } else if (dst.regClass() == v1) {
         bld.vop1(aco_opcode::v_bfrev_b32, Definition(dst), get_alu_src(ctx, instr->src[0]));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_iadd: {
      if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_add_u32, dst, true);
         break;
      }

      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == v1) {
         bld.vadd32(Definition(dst), Operand(src0), Operand(src1));
         break;
      }

      assert(src0.size() == 2 && src1.size() == 2);
      Temp src00 = bld.tmp(src0.type(), 1);
      Temp src01 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src00), Definition(src01), src0);
      Temp src10 = bld.tmp(src1.type(), 1);
      Temp src11 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src10), Definition(src11), src1);

      if (dst.regClass() == s2) {
         Temp carry = bld.tmp(s1);
         Temp dst0 = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.scc(Definition(carry)), src00, src10);
         Temp dst1 = bld.sop2(aco_opcode::s_addc_u32, bld.def(s1), bld.def(s1, scc), src01, src11, bld.scc(carry));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
      } else if (dst.regClass() == v2) {
         Temp dst0 = bld.tmp(v1);
         Temp carry = bld.vadd32(Definition(dst0), src00, src10, true).def(1).getTemp();
         Temp dst1 = bld.vadd32(bld.def(v1), src01, src11, false, carry);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_uadd_sat: {
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == s1) {
         Temp tmp = bld.tmp(s1), carry = bld.tmp(s1);
         bld.sop2(aco_opcode::s_add_u32, Definition(tmp), bld.scc(Definition(carry)),
                  src0, src1);
         bld.sop2(aco_opcode::s_cselect_b32, Definition(dst), Operand((uint32_t) -1), tmp, bld.scc(carry));
      } else if (dst.regClass() == v1) {
         if (ctx->options->chip_class >= GFX9) {
            aco_ptr<VOP3A_instruction> add{create_instruction<VOP3A_instruction>(aco_opcode::v_add_u32, asVOP3(Format::VOP2), 2, 1)};
            add->operands[0] = Operand(src0);
            add->operands[1] = Operand(src1);
            add->definitions[0] = Definition(dst);
            add->clamp = 1;
            ctx->block->instructions.emplace_back(std::move(add));
         } else {
            if (src1.regClass() != v1)
               std::swap(src0, src1);
            assert(src1.regClass() == v1);
            Temp tmp = bld.tmp(v1);
            Temp carry = bld.vadd32(Definition(tmp), src0, src1, true).def(1).getTemp();
            bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), tmp, Operand((uint32_t) -1), carry);
         }
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_uadd_carry: {
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == s1) {
         bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.scc(Definition(dst)), src0, src1);
         break;
      }
      if (dst.regClass() == v1) {
         Temp carry = bld.vadd32(bld.def(v1), src0, src1, true).def(1).getTemp();
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), Operand(0u), Operand(1u), carry);
         break;
      }

      Temp src00 = bld.tmp(src0.type(), 1);
      Temp src01 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src00), Definition(src01), src0);
      Temp src10 = bld.tmp(src1.type(), 1);
      Temp src11 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src10), Definition(src11), src1);
      if (dst.regClass() == s2) {
         Temp carry = bld.tmp(s1);
         bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.scc(Definition(carry)), src00, src10);
         carry = bld.sop2(aco_opcode::s_addc_u32, bld.def(s1), bld.scc(bld.def(s1)), src01, src11, bld.scc(carry)).def(1).getTemp();
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), carry, Operand(0u));
      } else if (dst.regClass() == v2) {
         Temp carry = bld.vadd32(bld.def(v1), src00, src10, true).def(1).getTemp();
         carry = bld.vadd32(bld.def(v1), src01, src11, true, carry).def(1).getTemp();
         carry = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand(0u), Operand(1u), carry);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), carry, Operand(0u));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_isub: {
      if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_sub_i32, dst, true);
         break;
      }

      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == v1) {
         bld.vsub32(Definition(dst), src0, src1);
         break;
      }

      Temp src00 = bld.tmp(src0.type(), 1);
      Temp src01 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src00), Definition(src01), src0);
      Temp src10 = bld.tmp(src1.type(), 1);
      Temp src11 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src10), Definition(src11), src1);
      if (dst.regClass() == s2) {
         Temp carry = bld.tmp(s1);
         Temp dst0 = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.scc(Definition(carry)), src00, src10);
         Temp dst1 = bld.sop2(aco_opcode::s_subb_u32, bld.def(s1), bld.def(s1, scc), src01, src11, carry);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
      } else if (dst.regClass() == v2) {
         Temp lower = bld.tmp(v1);
         Temp borrow = bld.vsub32(Definition(lower), src00, src10, true).def(1).getTemp();
         Temp upper = bld.vsub32(bld.def(v1), src01, src11, false, borrow);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_usub_borrow: {
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == s1) {
         bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.scc(Definition(dst)), src0, src1);
         break;
      } else if (dst.regClass() == v1) {
         Temp borrow = bld.vsub32(bld.def(v1), src0, src1, true).def(1).getTemp();
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), Operand(0u), Operand(1u), borrow);
         break;
      }

      Temp src00 = bld.tmp(src0.type(), 1);
      Temp src01 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src00), Definition(src01), src0);
      Temp src10 = bld.tmp(src1.type(), 1);
      Temp src11 = bld.tmp(dst.type(), 1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(src10), Definition(src11), src1);
      if (dst.regClass() == s2) {
         Temp borrow = bld.tmp(s1);
         bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.scc(Definition(borrow)), src00, src10);
         borrow = bld.sop2(aco_opcode::s_subb_u32, bld.def(s1), bld.scc(bld.def(s1)), src01, src11, bld.scc(borrow)).def(1).getTemp();
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), borrow, Operand(0u));
      } else if (dst.regClass() == v2) {
         Temp borrow = bld.vsub32(bld.def(v1), src00, src10, true).def(1).getTemp();
         borrow = bld.vsub32(bld.def(v1), src01, src11, true, Operand(borrow)).def(1).getTemp();
         borrow = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand(0u), Operand(1u), borrow);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), borrow, Operand(0u));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_imul: {
      if (dst.regClass() == v1) {
         bld.vop3(aco_opcode::v_mul_lo_u32, Definition(dst),
                  get_alu_src(ctx, instr->src[0]), get_alu_src(ctx, instr->src[1]));
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_mul_i32, dst, false);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_umul_high: {
      if (dst.regClass() == v1) {
         bld.vop3(aco_opcode::v_mul_hi_u32, Definition(dst), get_alu_src(ctx, instr->src[0]), get_alu_src(ctx, instr->src[1]));
      } else if (dst.regClass() == s1 && ctx->options->chip_class >= GFX9) {
         bld.sop2(aco_opcode::s_mul_hi_u32, Definition(dst), get_alu_src(ctx, instr->src[0]), get_alu_src(ctx, instr->src[1]));
      } else if (dst.regClass() == s1) {
         Temp tmp = bld.vop3(aco_opcode::v_mul_hi_u32, bld.def(v1), get_alu_src(ctx, instr->src[0]),
                             as_vgpr(ctx, get_alu_src(ctx, instr->src[1])));
         bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), tmp);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_imul_high: {
      if (dst.regClass() == v1) {
         bld.vop3(aco_opcode::v_mul_hi_i32, Definition(dst), get_alu_src(ctx, instr->src[0]), get_alu_src(ctx, instr->src[1]));
      } else if (dst.regClass() == s1 && ctx->options->chip_class >= GFX9) {
         bld.sop2(aco_opcode::s_mul_hi_i32, Definition(dst), get_alu_src(ctx, instr->src[0]), get_alu_src(ctx, instr->src[1]));
      } else if (dst.regClass() == s1) {
         Temp tmp = bld.vop3(aco_opcode::v_mul_hi_i32, bld.def(v1), get_alu_src(ctx, instr->src[0]),
                             as_vgpr(ctx, get_alu_src(ctx, instr->src[1])));
         bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), tmp);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fmul: {
      if (dst.size() == 1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_mul_f32, dst, true);
      } else if (dst.size() == 2) {
         bld.vop3(aco_opcode::v_mul_f64, Definition(dst), get_alu_src(ctx, instr->src[0]),
                  as_vgpr(ctx, get_alu_src(ctx, instr->src[1])));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fadd: {
      if (dst.size() == 1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_add_f32, dst, true);
      } else if (dst.size() == 2) {
         bld.vop3(aco_opcode::v_add_f64, Definition(dst), get_alu_src(ctx, instr->src[0]),
                  as_vgpr(ctx, get_alu_src(ctx, instr->src[1])));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fsub: {
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.size() == 1) {
         if (src1.type() == RegType::vgpr || src0.type() != RegType::vgpr)
            emit_vop2_instruction(ctx, instr, aco_opcode::v_sub_f32, dst, false);
         else
            emit_vop2_instruction(ctx, instr, aco_opcode::v_subrev_f32, dst, true);
      } else if (dst.size() == 2) {
         Instruction* add = bld.vop3(aco_opcode::v_add_f64, Definition(dst),
                                     get_alu_src(ctx, instr->src[0]),
                                     as_vgpr(ctx, get_alu_src(ctx, instr->src[1])));
         VOP3A_instruction* sub = static_cast<VOP3A_instruction*>(add);
         sub->neg[1] = true;
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fmax: {
      if (dst.size() == 1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_f32, dst, true);
      } else if (dst.size() == 2) {
         bld.vop3(aco_opcode::v_max_f64, Definition(dst),
                  get_alu_src(ctx, instr->src[0]),
                  as_vgpr(ctx, get_alu_src(ctx, instr->src[1])));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fmin: {
      if (dst.size() == 1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_f32, dst, true);
      } else if (dst.size() == 2) {
         bld.vop3(aco_opcode::v_min_f64, Definition(dst),
                  get_alu_src(ctx, instr->src[0]),
                  as_vgpr(ctx, get_alu_src(ctx, instr->src[1])));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fmax3: {
      if (dst.size() == 1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_max3_f32, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fmin3: {
      if (dst.size() == 1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_min3_f32, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fmed3: {
      if (dst.size() == 1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_med3_f32, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_umax3: {
      if (dst.size() == 1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_max3_u32, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_umin3: {
      if (dst.size() == 1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_min3_u32, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_umed3: {
      if (dst.size() == 1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_med3_u32, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_imax3: {
      if (dst.size() == 1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_max3_i32, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_imin3: {
      if (dst.size() == 1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_min3_i32, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_imed3: {
      if (dst.size() == 1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_med3_i32, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_cube_face_coord: {
      Temp in = get_alu_src(ctx, instr->src[0], 3);
      Temp src[3] = { emit_extract_vector(ctx, in, 0, v1),
                      emit_extract_vector(ctx, in, 1, v1),
                      emit_extract_vector(ctx, in, 2, v1) };
      Temp ma = bld.vop3(aco_opcode::v_cubema_f32, bld.def(v1), src[0], src[1], src[2]);
      ma = bld.vop1(aco_opcode::v_rcp_f32, bld.def(v1), ma);
      Temp sc = bld.vop3(aco_opcode::v_cubesc_f32, bld.def(v1), src[0], src[1], src[2]);
      Temp tc = bld.vop3(aco_opcode::v_cubetc_f32, bld.def(v1), src[0], src[1], src[2]);
      sc = bld.vop2(aco_opcode::v_madak_f32, bld.def(v1), sc, ma, Operand(0x3f000000u/*0.5*/));
      tc = bld.vop2(aco_opcode::v_madak_f32, bld.def(v1), tc, ma, Operand(0x3f000000u/*0.5*/));
      bld.pseudo(aco_opcode::p_create_vector, Definition(dst), sc, tc);
      break;
   }
   case nir_op_cube_face_index: {
      Temp in = get_alu_src(ctx, instr->src[0], 3);
      Temp src[3] = { emit_extract_vector(ctx, in, 0, v1),
                      emit_extract_vector(ctx, in, 1, v1),
                      emit_extract_vector(ctx, in, 2, v1) };
      bld.vop3(aco_opcode::v_cubeid_f32, Definition(dst), src[0], src[1], src[2]);
      break;
   }
   case nir_op_bcsel: {
      emit_bcsel(ctx, instr, dst);
      break;
   }
   case nir_op_frsq: {
      if (dst.size() == 1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rsq_f32, dst);
      } else if (dst.size() == 2) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rsq_f64, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fneg: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.size() == 1) {
         bld.vop2(aco_opcode::v_xor_b32, Definition(dst), Operand(0x80000000u), as_vgpr(ctx, src));
      } else if (dst.size() == 2) {
         Temp upper = bld.tmp(v1), lower = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), src);
         upper = bld.vop2(aco_opcode::v_xor_b32, bld.def(v1), Operand(0x80000000u), upper);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fabs: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.size() == 1) {
         bld.vop2(aco_opcode::v_and_b32, Definition(dst), Operand(0x7FFFFFFFu), as_vgpr(ctx, src));
      } else if (dst.size() == 2) {
         Temp upper = bld.tmp(v1), lower = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), src);
         upper = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(0x7FFFFFFFu), upper);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fsat: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.size() == 1) {
         bld.vop3(aco_opcode::v_med3_f32, Definition(dst), Operand(0u), Operand(0x3f800000u), src);
      } else if (dst.size() == 2) {
         Instruction* add = bld.vop3(aco_opcode::v_add_f64, Definition(dst), src, Operand(0u));
         VOP3A_instruction* vop3 = static_cast<VOP3A_instruction*>(add);
         vop3->clamp = true;
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_flog2: {
      if (dst.size() == 1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_log_f32, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_frcp: {
      if (dst.size() == 1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rcp_f32, dst);
      } else if (dst.size() == 2) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rcp_f64, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fexp2: {
      if (dst.size() == 1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_exp_f32, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fsqrt: {
      if (dst.size() == 1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_sqrt_f32, dst);
      } else if (dst.size() == 2) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_sqrt_f64, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_ffract: {
      if (dst.size() == 1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_fract_f32, dst);
      } else if (dst.size() == 2) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_fract_f64, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_ffloor: {
      if (dst.size() == 1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_floor_f32, dst);
      } else if (dst.size() == 2) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_floor_f64, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fceil: {
      if (dst.size() == 1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_ceil_f32, dst);
      } else if (dst.size() == 2) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_ceil_f64, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_ftrunc: {
      if (dst.size() == 1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_trunc_f32, dst);
      } else if (dst.size() == 2) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_trunc_f64, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fround_even: {
      if (dst.size() == 1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rndne_f32, dst);
      } else if (dst.size() == 2) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rndne_f64, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fsin:
   case nir_op_fcos: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      aco_ptr<Instruction> norm;
      if (dst.size() == 1) {
         Temp tmp;
         Operand half_pi(0x3e22f983u);
         if (src.type() == RegType::sgpr)
            tmp = bld.vop2_e64(aco_opcode::v_mul_f32, bld.def(v1), half_pi, src);
         else
            tmp = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), half_pi, src);

         /* before GFX9, v_sin_f32 and v_cos_f32 had a valid input domain of [-256, +256] */
         if (ctx->options->chip_class < GFX9)
            tmp = bld.vop1(aco_opcode::v_fract_f32, bld.def(v1), tmp);

         aco_opcode opcode = instr->op == nir_op_fsin ? aco_opcode::v_sin_f32 : aco_opcode::v_cos_f32;
         bld.vop1(opcode, Definition(dst), tmp);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_ldexp: {
      if (dst.size() == 1) {
         bld.vop3(aco_opcode::v_ldexp_f32, Definition(dst),
                  as_vgpr(ctx, get_alu_src(ctx, instr->src[0])),
                  get_alu_src(ctx, instr->src[1]));
      } else if (dst.size() == 2) {
         bld.vop3(aco_opcode::v_ldexp_f64, Definition(dst),
                  as_vgpr(ctx, get_alu_src(ctx, instr->src[0])),
                  get_alu_src(ctx, instr->src[1]));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_frexp_sig: {
      if (dst.size() == 1) {
         bld.vop1(aco_opcode::v_frexp_mant_f32, Definition(dst),
                  get_alu_src(ctx, instr->src[0]));
      } else if (dst.size() == 2) {
         bld.vop1(aco_opcode::v_frexp_mant_f64, Definition(dst),
                  get_alu_src(ctx, instr->src[0]));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_frexp_exp: {
      if (instr->src[0].src.ssa->bit_size == 32) {
         bld.vop1(aco_opcode::v_frexp_exp_i32_f32, Definition(dst),
                  get_alu_src(ctx, instr->src[0]));
      } else if (instr->src[0].src.ssa->bit_size == 64) {
         bld.vop1(aco_opcode::v_frexp_exp_i32_f64, Definition(dst),
                  get_alu_src(ctx, instr->src[0]));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fsign: {
      Temp src = as_vgpr(ctx, get_alu_src(ctx, instr->src[0]));
      if (dst.size() == 1) {
         Temp cond = bld.vopc(aco_opcode::v_cmp_nlt_f32, bld.hint_vcc(bld.def(s2)), Operand(0u), src);
         src = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), Operand(0x3f800000u), src, cond);
         cond = bld.vopc(aco_opcode::v_cmp_le_f32, bld.hint_vcc(bld.def(s2)), Operand(0u), src);
         bld.vop2(aco_opcode::v_cndmask_b32, Definition(dst), Operand(0xbf800000u), src, cond);
      } else if (dst.size() == 2) {
         Temp cond = bld.vopc(aco_opcode::v_cmp_nlt_f64, bld.hint_vcc(bld.def(s2)), Operand(0u), src);
         Temp tmp = bld.vop1(aco_opcode::v_mov_b32, bld.def(v1), Operand(0x3FF00000u));
         Temp upper = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), tmp, src, cond);

         cond = bld.vopc(aco_opcode::v_cmp_le_f64, bld.hint_vcc(bld.def(s2)), Operand(0u), src);
         tmp = bld.vop1(aco_opcode::v_mov_b32, bld.def(v1), Operand(0xBFF00000u));
         upper = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), tmp, upper, cond);

         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), Operand(0u), upper);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_f2f32: {
      if (instr->src[0].src.ssa->bit_size == 64) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_f32_f64, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_f2f64: {
      if (instr->src[0].src.ssa->bit_size == 32) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_f64_f32, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_i2f32: {
      assert(dst.size() == 1);
      emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_f32_i32, dst);
      break;
   }
   case nir_op_i2f64: {
      if (instr->src[0].src.ssa->bit_size == 32) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_f64_i32, dst);
      } else if (instr->src[0].src.ssa->bit_size == 64) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         RegClass rc = RegClass(src.type(), 1);
         Temp lower = bld.tmp(rc), upper = bld.tmp(rc);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), src);
         lower = bld.vop1(aco_opcode::v_cvt_f64_u32, bld.def(v2), lower);
         upper = bld.vop1(aco_opcode::v_cvt_f64_i32, bld.def(v2), upper);
         upper = bld.vop3(aco_opcode::v_ldexp_f64, bld.def(v2), upper, Operand(32u));
         bld.vop3(aco_opcode::v_add_f64, Definition(dst), lower, upper);

      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_u2f32: {
      assert(dst.size() == 1);
      emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_f32_u32, dst);
      break;
   }
   case nir_op_u2f64: {
      if (instr->src[0].src.ssa->bit_size == 32) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_f64_u32, dst);
      } else if (instr->src[0].src.ssa->bit_size == 64) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         RegClass rc = RegClass(src.type(), 1);
         Temp lower = bld.tmp(rc), upper = bld.tmp(rc);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), src);
         lower = bld.vop1(aco_opcode::v_cvt_f64_u32, bld.def(v2), lower);
         upper = bld.vop1(aco_opcode::v_cvt_f64_u32, bld.def(v2), upper);
         upper = bld.vop3(aco_opcode::v_ldexp_f64, bld.def(v2), upper, Operand(32u));
         bld.vop3(aco_opcode::v_add_f64, Definition(dst), lower, upper);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_f2i32: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size == 32) {
         if (dst.type() == RegType::vgpr)
            bld.vop1(aco_opcode::v_cvt_i32_f32, Definition(dst), src);
         else
            bld.pseudo(aco_opcode::p_as_uniform, Definition(dst),
                       bld.vop1(aco_opcode::v_cvt_i32_f32, bld.def(v1), src));

      } else if (instr->src[0].src.ssa->bit_size == 64) {
         if (dst.type() == RegType::vgpr)
            bld.vop1(aco_opcode::v_cvt_i32_f64, Definition(dst), src);
         else
            bld.pseudo(aco_opcode::p_as_uniform, Definition(dst),
                       bld.vop1(aco_opcode::v_cvt_i32_f64, bld.def(v1), src));

      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_f2u32: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size == 32) {
         if (dst.type() == RegType::vgpr)
            bld.vop1(aco_opcode::v_cvt_u32_f32, Definition(dst), src);
         else
            bld.pseudo(aco_opcode::p_as_uniform, Definition(dst),
                       bld.vop1(aco_opcode::v_cvt_u32_f32, bld.def(v1), src));

      } else if (instr->src[0].src.ssa->bit_size == 64) {
         if (dst.type() == RegType::vgpr)
            bld.vop1(aco_opcode::v_cvt_u32_f64, Definition(dst), src);
         else
            bld.pseudo(aco_opcode::p_as_uniform, Definition(dst),
                       bld.vop1(aco_opcode::v_cvt_u32_f64, bld.def(v1), src));

      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_f2i64: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size == 32 && dst.type() == RegType::vgpr) {
         Temp exponent = bld.vop1(aco_opcode::v_frexp_exp_i32_f32, bld.def(v1), src);
         exponent = bld.vop3(aco_opcode::v_med3_i32, bld.def(v1), Operand(0x0u), exponent, Operand(64u));
         Temp mantissa = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(0x7fffffu), src);
         Temp sign = bld.vop2(aco_opcode::v_ashrrev_i32, bld.def(v1), Operand(31u), src);
         mantissa = bld.vop2(aco_opcode::v_or_b32, bld.def(v1), Operand(0x800000u), mantissa);
         mantissa = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand(7u), mantissa);
         mantissa = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), Operand(0u), mantissa);
         Temp new_exponent = bld.tmp(v1);
         Temp borrow = bld.vsub32(Definition(new_exponent), Operand(63u), exponent, true).def(1).getTemp();
         mantissa = bld.vop3(aco_opcode::v_lshrrev_b64, bld.def(v2), new_exponent, mantissa);
         Temp saturate = bld.vop1(aco_opcode::v_bfrev_b32, bld.def(v1), Operand(0xfffffffeu));
         Temp lower = bld.tmp(v1), upper = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), mantissa);
         lower = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), lower, Operand(0xffffffffu), borrow);
         upper = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), upper, saturate, borrow);
         lower = bld.vop2(aco_opcode::v_xor_b32, bld.def(v1), sign, lower);
         upper = bld.vop2(aco_opcode::v_xor_b32, bld.def(v1), sign, upper);
         Temp new_lower = bld.tmp(v1);
         borrow = bld.vsub32(Definition(new_lower), lower, sign, true).def(1).getTemp();
         Temp new_upper = bld.vsub32(bld.def(v1), upper, sign, false, borrow);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), new_lower, new_upper);

      } else if (instr->src[0].src.ssa->bit_size == 32 && dst.type() == RegType::sgpr) {
         if (src.type() == RegType::vgpr)
            src = bld.as_uniform(src);
         Temp exponent = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc), src, Operand(0x80017u));
         exponent = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.def(s1, scc), exponent, Operand(126u));
         exponent = bld.sop2(aco_opcode::s_max_u32, bld.def(s1), bld.def(s1, scc), Operand(0u), exponent);
         exponent = bld.sop2(aco_opcode::s_min_u32, bld.def(s1), bld.def(s1, scc), Operand(64u), exponent);
         Temp mantissa = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), Operand(0x7fffffu), src);
         Temp sign = bld.sop2(aco_opcode::s_ashr_i32, bld.def(s1), bld.def(s1, scc), src, Operand(31u));
         mantissa = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), Operand(0x800000u), mantissa);
         mantissa = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), mantissa, Operand(7u));
         mantissa = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand(0u), mantissa);
         exponent = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.def(s1, scc), Operand(63u), exponent);
         mantissa = bld.sop2(aco_opcode::s_lshr_b64, bld.def(s2), bld.def(s1, scc), mantissa, exponent);
         Temp cond = bld.sopc(aco_opcode::s_cmp_eq_u32, bld.def(s1, scc), exponent, Operand(0xffffffffu)); // exp >= 64
         Temp saturate = bld.sop1(aco_opcode::s_brev_b64, bld.def(s2), Operand(0xfffffffeu));
         mantissa = bld.sop2(aco_opcode::s_cselect_b64, bld.def(s2), saturate, mantissa, cond);
         Temp lower = bld.tmp(s1), upper = bld.tmp(s1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), mantissa);
         lower = bld.sop2(aco_opcode::s_xor_b32, bld.def(s1), bld.def(s1, scc), sign, lower);
         upper = bld.sop2(aco_opcode::s_xor_b32, bld.def(s1), bld.def(s1, scc), sign, upper);
         Temp borrow = bld.tmp(s1);
         lower = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.scc(Definition(borrow)), lower, sign);
         upper = bld.sop2(aco_opcode::s_subb_u32, bld.def(s1), bld.def(s1, scc), upper, sign, borrow);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);

      } else if (instr->src[0].src.ssa->bit_size == 64) {
         Temp vec = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand(0u), Operand(0x3df00000u));
         Temp trunc = bld.vop1(aco_opcode::v_trunc_f64, bld.def(v2), src);
         Temp mul = bld.vop3(aco_opcode::v_mul_f64, bld.def(v2), trunc, vec);
         vec = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand(0u), Operand(0xc1f00000u));
         Temp floor  = bld.vop1(aco_opcode::v_floor_f64, bld.def(v2), mul);
         Temp fma = bld.vop3(aco_opcode::v_fma_f64, bld.def(v2), floor, vec, trunc);
         Temp lower = bld.vop1(aco_opcode::v_cvt_u32_f64, bld.def(v1), fma);
         Temp upper = bld.vop1(aco_opcode::v_cvt_i32_f64, bld.def(v1), floor);
         if (dst.type() == RegType::sgpr) {
            lower = bld.as_uniform(lower);
            upper = bld.as_uniform(upper);
         }
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);

      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_f2u64: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size == 32 && dst.type() == RegType::vgpr) {
         Temp exponent = bld.vop1(aco_opcode::v_frexp_exp_i32_f32, bld.def(v1), src);
         Temp exponent_in_range = bld.vopc(aco_opcode::v_cmp_ge_i32, bld.hint_vcc(bld.def(s2)), Operand(64u), exponent);
         exponent = bld.vop2(aco_opcode::v_max_i32, bld.def(v1), Operand(0x0u), exponent);
         Temp mantissa = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(0x7fffffu), src);
         mantissa = bld.vop2(aco_opcode::v_or_b32, bld.def(v1), Operand(0x800000u), mantissa);
         Temp exponent_small = bld.vsub32(bld.def(v1), Operand(24u), exponent);
         Temp small = bld.vop2(aco_opcode::v_lshrrev_b32, bld.def(v1), exponent_small, mantissa);
         mantissa = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), Operand(0u), mantissa);
         Temp new_exponent = bld.tmp(v1);
         Temp cond_small = bld.vsub32(Definition(new_exponent), exponent, Operand(24u), true).def(1).getTemp();
         mantissa = bld.vop3(aco_opcode::v_lshlrev_b64, bld.def(v2), new_exponent, mantissa);
         Temp lower = bld.tmp(v1), upper = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), mantissa);
         lower = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), lower, small, cond_small);
         upper = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), upper, Operand(0u), cond_small);
         lower = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), Operand(0xffffffffu), lower, exponent_in_range);
         upper = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), Operand(0xffffffffu), upper, exponent_in_range);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);

      } else if (instr->src[0].src.ssa->bit_size == 32 && dst.type() == RegType::sgpr) {
         if (src.type() == RegType::vgpr)
            src = bld.as_uniform(src);
         Temp exponent = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc), src, Operand(0x80017u));
         exponent = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.def(s1, scc), exponent, Operand(126u));
         exponent = bld.sop2(aco_opcode::s_max_u32, bld.def(s1), bld.def(s1, scc), Operand(0u), exponent);
         Temp mantissa = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), Operand(0x7fffffu), src);
         mantissa = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), Operand(0x800000u), mantissa);
         Temp exponent_small = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.def(s1, scc), Operand(24u), exponent);
         Temp small = bld.sop2(aco_opcode::s_lshr_b32, bld.def(s1), bld.def(s1, scc), mantissa, exponent_small);
         mantissa = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand(0u), mantissa);
         Temp exponent_large = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.def(s1, scc), exponent, Operand(24u));
         mantissa = bld.sop2(aco_opcode::s_lshl_b64, bld.def(s2), bld.def(s1, scc), mantissa, exponent_large);
         Temp cond = bld.sopc(aco_opcode::s_cmp_ge_i32, bld.def(s1, scc), Operand(64u), exponent);
         mantissa = bld.sop2(aco_opcode::s_cselect_b64, bld.def(s2), mantissa, Operand(0xffffffffu), cond);
         Temp lower = bld.tmp(s1), upper = bld.tmp(s1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), mantissa);
         Temp cond_small = bld.sopc(aco_opcode::s_cmp_le_i32, bld.def(s1, scc), exponent, Operand(24u));
         lower = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1), small, lower, cond_small);
         upper = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1), Operand(0u), upper, cond_small);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);

      } else if (instr->src[0].src.ssa->bit_size == 64) {
         Temp vec = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand(0u), Operand(0x3df00000u));
         Temp trunc = bld.vop1(aco_opcode::v_trunc_f64, bld.def(v2), src);
         Temp mul = bld.vop3(aco_opcode::v_mul_f64, bld.def(v2), trunc, vec);
         vec = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand(0u), Operand(0xc1f00000u));
         Temp floor  = bld.vop1(aco_opcode::v_floor_f64, bld.def(v2), mul);
         Temp fma = bld.vop3(aco_opcode::v_fma_f64, bld.def(v2), floor, vec, trunc);
         Temp lower = bld.vop1(aco_opcode::v_cvt_u32_f64, bld.def(v1), fma);
         Temp upper = bld.vop1(aco_opcode::v_cvt_u32_f64, bld.def(v1), floor);
         if (dst.type() == RegType::sgpr) {
            lower = bld.as_uniform(lower);
            upper = bld.as_uniform(upper);
         }
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);

      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_b2f32: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == s1) {
         src = as_uniform_bool(ctx, src);
         bld.sop2(aco_opcode::s_mul_i32, Definition(dst), Operand(0x3f800000u), src);
      } else if (dst.regClass() == v1) {
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), Operand(0u), Operand(0x3f800000u),
                      as_divergent_bool(ctx, src, true));
      } else {
         unreachable("Wrong destination register class for nir_op_b2f32.");
      }
      break;
   }
   case nir_op_b2f64: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == s2) {
         src = as_uniform_bool(ctx, src);
         bld.sop2(aco_opcode::s_cselect_b64, Definition(dst), Operand(0x3f800000u), Operand(0u), bld.scc(src));
      } else if (dst.regClass() == v2) {
         Temp one = bld.vop1(aco_opcode::v_mov_b32, bld.def(v2), Operand(0x3FF00000u));
         Temp upper = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand(0u), one,
                      as_divergent_bool(ctx, src, true));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), Operand(0u), upper);
      } else {
         unreachable("Wrong destination register class for nir_op_b2f64.");
      }
      break;
   }
   case nir_op_i2i32: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size == 64) {
         /* we can actually just say dst = src, as it would map the lower register */
         emit_extract_vector(ctx, src, 0, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_u2u32: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size == 16) {
         if (dst.regClass() == s1) {
            bld.sop2(aco_opcode::s_and_b32, Definition(dst), bld.def(s1, scc), Operand(0xFFFFu), src);
         } else {
            // TODO: do better with SDWA
            bld.vop2(aco_opcode::v_and_b32, Definition(dst), Operand(0xFFFFu), src);
         }
      } else if (instr->src[0].src.ssa->bit_size == 64) {
         /* we can actually just say dst = src, as it would map the lower register */
         emit_extract_vector(ctx, src, 0, dst);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_i2i64: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size == 32) {
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), src, Operand(0u));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_u2u64: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size == 32) {
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), src, Operand(0u));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_b2i32: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == s1) {
         if (src.regClass() == s1) {
            bld.copy(Definition(dst), src);
         } else {
            // TODO: in a post-RA optimization, we can check if src is in VCC, and directly use VCCNZ
            assert(src.regClass() == s2);
            bld.sopc(aco_opcode::s_cmp_lg_u64, bld.scc(Definition(dst)), Operand(0u), src);
         }
      } else {
         assert(dst.regClass() == v1 && src.regClass() == s2);
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), Operand(0u), Operand(1u), src);
      }
      break;
   }
   case nir_op_i2b1: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == s2) {
         assert(src.regClass() == v1 || src.regClass() == v2);
         bld.vopc(src.size() == 2 ? aco_opcode::v_cmp_lg_u64 : aco_opcode::v_cmp_lg_u32,
                  Definition(dst), Operand(0u), src).def(0).setHint(vcc);
      } else {
         assert(src.regClass() == s1 && dst.regClass() == s1);
         bld.sopc(aco_opcode::s_cmp_lg_u32, bld.scc(Definition(dst)), Operand(0u), src);
      }
      break;
   }
   case nir_op_pack_64_2x32_split: {
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);

      bld.pseudo(aco_opcode::p_create_vector, Definition(dst), src0, src1);
      break;
   }
   case nir_op_unpack_64_2x32_split_x:
      bld.pseudo(aco_opcode::p_split_vector, Definition(dst), bld.def(dst.regClass()), get_alu_src(ctx, instr->src[0]));
      break;
   case nir_op_unpack_64_2x32_split_y:
      bld.pseudo(aco_opcode::p_split_vector, bld.def(dst.regClass()), Definition(dst), get_alu_src(ctx, instr->src[0]));
      break;
   case nir_op_pack_half_2x16: {
      Temp src = get_alu_src(ctx, instr->src[0], 2);

      if (dst.regClass() == v1) {
         Temp src0 = bld.tmp(v1);
         Temp src1 = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(src0), Definition(src1), src);
         bld.vop3(aco_opcode::v_cvt_pkrtz_f16_f32, Definition(dst), src0, src1);

      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_unpack_half_2x16_split_x: {
      if (dst.regClass() == v1) {
         Builder bld(ctx->program, ctx->block);
         bld.vop1(aco_opcode::v_cvt_f32_f16, Definition(dst), get_alu_src(ctx, instr->src[0]));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_unpack_half_2x16_split_y: {
      if (dst.regClass() == v1) {
         Builder bld(ctx->program, ctx->block);
         /* TODO: use SDWA here */
         bld.vop1(aco_opcode::v_cvt_f32_f16, Definition(dst),
                  bld.vop2(aco_opcode::v_lshrrev_b32, bld.def(v1), Operand(16u), as_vgpr(ctx, get_alu_src(ctx, instr->src[0]))));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_fquantize2f16: {
      Temp f16 = bld.vop1(aco_opcode::v_cvt_f16_f32, bld.def(v1), get_alu_src(ctx, instr->src[0]));

      Temp mask = bld.copy(bld.def(s1), Operand(0x36Fu)); /* value is NOT negative/positive denormal value */

      Temp cmp_res = bld.tmp(s2);
      bld.vopc_e64(aco_opcode::v_cmp_class_f16, Definition(cmp_res), f16, mask).def(0).setHint(vcc);

      Temp f32 = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), f16);

      bld.vop2(aco_opcode::v_cndmask_b32, Definition(dst), Operand(0u), f32, cmp_res);
      break;
   }
   case nir_op_bfm: {
      Temp bits = get_alu_src(ctx, instr->src[0]);
      Temp offset = get_alu_src(ctx, instr->src[1]);

      if (dst.regClass() == s1) {
         bld.sop2(aco_opcode::s_bfm_b32, Definition(dst), bits, offset);
      } else if (dst.regClass() == v1) {
         bld.vop3(aco_opcode::v_bfm_b32, Definition(dst), bits, offset);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_bitfield_select: {
      /* (mask & insert) | (~mask & base) */
      Temp bitmask = get_alu_src(ctx, instr->src[0]);
      Temp insert = get_alu_src(ctx, instr->src[1]);
      Temp base = get_alu_src(ctx, instr->src[2]);

      /* dst = (insert & bitmask) | (base & ~bitmask) */
      if (dst.regClass() == s1) {
         aco_ptr<Instruction> sop2;
         nir_const_value* const_bitmask = nir_src_as_const_value(instr->src[0].src);
         nir_const_value* const_insert = nir_src_as_const_value(instr->src[1].src);
         Operand lhs;
         if (const_insert && const_bitmask) {
            lhs = Operand(const_insert->u32 & const_bitmask->u32);
         } else {
            insert = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), insert, bitmask);
            lhs = Operand(insert);
         }

         Operand rhs;
         nir_const_value* const_base = nir_src_as_const_value(instr->src[2].src);
         if (const_base && const_bitmask) {
            rhs = Operand(const_base->u32 & ~const_bitmask->u32);
         } else {
            base = bld.sop2(aco_opcode::s_andn2_b32, bld.def(s1), bld.def(s1, scc), base, bitmask);
            rhs = Operand(base);
         }

         bld.sop2(aco_opcode::s_or_b32, Definition(dst), bld.def(s1, scc), rhs, lhs);

      } else if (dst.regClass() == v1) {
         if (base.type() == RegType::sgpr && (bitmask.type() == RegType::sgpr || (insert.type() == RegType::sgpr)))
            base = as_vgpr(ctx, base);
         if (insert.type() == RegType::sgpr && bitmask.type() == RegType::sgpr)
            insert = as_vgpr(ctx, insert);

         bld.vop3(aco_opcode::v_bfi_b32, Definition(dst), bitmask, insert, base);

      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_ubfe:
   case nir_op_ibfe: {
      Temp base = get_alu_src(ctx, instr->src[0]);
      Temp offset = get_alu_src(ctx, instr->src[1]);
      Temp bits = get_alu_src(ctx, instr->src[2]);

      if (dst.type() == RegType::sgpr) {
         Operand extract;
         nir_const_value* const_offset = nir_src_as_const_value(instr->src[1].src);
         nir_const_value* const_bits = nir_src_as_const_value(instr->src[2].src);
         if (const_offset && const_bits) {
            uint32_t const_extract = (const_bits->u32 << 16) | const_offset->u32;
            extract = Operand(const_extract);
         } else {
            Operand width;
            if (const_bits) {
               width = Operand(const_bits->u32 << 16);
            } else {
               width = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), bits, Operand(16u));
            }
            extract = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), offset, width);
         }

         aco_opcode opcode;
         if (dst.regClass() == s1) {
            if (instr->op == nir_op_ubfe)
               opcode = aco_opcode::s_bfe_u32;
            else
               opcode = aco_opcode::s_bfe_i32;
         } else if (dst.regClass() == s2) {
            if (instr->op == nir_op_ubfe)
               opcode = aco_opcode::s_bfe_u64;
            else
               opcode = aco_opcode::s_bfe_i64;
         } else {
            unreachable("Unsupported BFE bit size");
         }

         bld.sop2(opcode, Definition(dst), bld.def(s1, scc), base, extract);

      } else {
         aco_opcode opcode;
         if (dst.regClass() == v1) {
            if (instr->op == nir_op_ubfe)
               opcode = aco_opcode::v_bfe_u32;
            else
               opcode = aco_opcode::v_bfe_i32;
         } else {
            unreachable("Unsupported BFE bit size");
         }

         emit_vop3a_instruction(ctx, instr, opcode, dst);
      }
      break;
   }
   case nir_op_bit_count: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (src.regClass() == s1) {
         bld.sop1(aco_opcode::s_bcnt1_i32_b32, Definition(dst), bld.def(s1, scc), src);
      } else if (src.regClass() == v1) {
         bld.vop3(aco_opcode::v_bcnt_u32_b32, Definition(dst), src, Operand(0u));
      } else if (src.regClass() == v2) {
         bld.vop3(aco_opcode::v_bcnt_u32_b32, Definition(dst),
                  emit_extract_vector(ctx, src, 1, v1),
                  bld.vop3(aco_opcode::v_bcnt_u32_b32, bld.def(v1),
                           emit_extract_vector(ctx, src, 0, v1), Operand(0u)));
      } else if (src.regClass() == s2) {
         bld.sop1(aco_opcode::s_bcnt1_i32_b64, Definition(dst), bld.def(s1, scc), src);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_flt: {
      if (instr->src[0].src.ssa->bit_size == 32)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_lt_f32, dst);
      else if (instr->src[0].src.ssa->bit_size == 64)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_lt_f64, dst);
      break;
   }
   case nir_op_fge: {
      if (instr->src[0].src.ssa->bit_size == 32)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_ge_f32, dst);
      else if (instr->src[0].src.ssa->bit_size == 64)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_ge_f64, dst);
      break;
   }
   case nir_op_feq: {
      if (instr->src[0].src.ssa->bit_size == 32)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_eq_f32, dst);
      else if (instr->src[0].src.ssa->bit_size == 64)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_eq_f64, dst);
      break;
   }
   case nir_op_fne: {
      if (instr->src[0].src.ssa->bit_size == 32)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_neq_f32, dst);
      else if (instr->src[0].src.ssa->bit_size == 64)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_neq_f64, dst);
      break;
   }
   case nir_op_ilt: {
      if (dst.regClass() == s2 && instr->src[0].src.ssa->bit_size == 32)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_lt_i32, dst);
      else if (dst.regClass() == s1 && instr->src[0].src.ssa->bit_size == 32)
         emit_comparison(ctx, instr, aco_opcode::s_cmp_lt_i32, dst);
      else if (dst.regClass() == s2 && instr->src[0].src.ssa->bit_size == 64)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_lt_i64, dst);
      break;
   }
   case nir_op_ige: {
      if (dst.regClass() == s2 && instr->src[0].src.ssa->bit_size == 32)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_ge_i32, dst);
      else if (dst.regClass() == s1 && instr->src[0].src.ssa->bit_size == 32)
         emit_comparison(ctx, instr, aco_opcode::s_cmp_ge_i32, dst);
      else if (dst.regClass() == s2 && instr->src[0].src.ssa->bit_size == 64)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_ge_i64, dst);
      break;
   }
   case nir_op_ieq: {
      if (dst.regClass() == s2 && instr->src[0].src.ssa->bit_size == 32) {
         emit_comparison(ctx, instr, aco_opcode::v_cmp_eq_i32, dst);
      } else if (dst.regClass() == s1 && instr->src[0].src.ssa->bit_size == 32) {
         emit_comparison(ctx, instr, aco_opcode::s_cmp_eq_i32, dst);
      } else if (dst.regClass() == s2 && instr->src[0].src.ssa->bit_size == 64) {
         emit_comparison(ctx, instr, aco_opcode::v_cmp_eq_i64, dst);
      } else if (dst.regClass() == s1 && instr->src[0].src.ssa->bit_size == 64) {
         emit_comparison(ctx, instr, aco_opcode::s_cmp_eq_u64, dst);
      } else if (dst.regClass() == s1 && instr->src[0].src.ssa->bit_size == 1) {
         Temp src0 = get_alu_src(ctx, instr->src[0]);
         Temp src1 = get_alu_src(ctx, instr->src[1]);
         bld.sopc(aco_opcode::s_cmp_eq_i32, bld.scc(Definition(dst)),
                  as_uniform_bool(ctx, src0), as_uniform_bool(ctx, src1));
      } else if (dst.regClass() == s2 && instr->src[0].src.ssa->bit_size == 1) {
         Temp src0 = get_alu_src(ctx, instr->src[0]);
         Temp src1 = get_alu_src(ctx, instr->src[1]);
         bld.sop2(aco_opcode::s_xnor_b64, Definition(dst), bld.def(s1, scc),
                  as_divergent_bool(ctx, src0, false), as_divergent_bool(ctx, src1, false));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_ine: {
      if (dst.regClass() == s2 && instr->src[0].src.ssa->bit_size == 32) {
         emit_comparison(ctx, instr, aco_opcode::v_cmp_lg_i32, dst);
      } else if (dst.regClass() == s2 && instr->src[0].src.ssa->bit_size == 64) {
         emit_comparison(ctx, instr, aco_opcode::v_cmp_lg_i64, dst);
      } else if (dst.regClass() == s1 && instr->src[0].src.ssa->bit_size == 32) {
         emit_comparison(ctx, instr, aco_opcode::s_cmp_lg_i32, dst);
      } else if (dst.regClass() == s1 && instr->src[0].src.ssa->bit_size == 64) {
         emit_comparison(ctx, instr, aco_opcode::s_cmp_lg_u64, dst);
      } else if (dst.regClass() == s1 && instr->src[0].src.ssa->bit_size == 1) {
         Temp src0 = get_alu_src(ctx, instr->src[0]);
         Temp src1 = get_alu_src(ctx, instr->src[1]);
         bld.sopc(aco_opcode::s_cmp_lg_i32, bld.scc(Definition(dst)),
                  as_uniform_bool(ctx, src0), as_uniform_bool(ctx, src1));
      } else if (dst.regClass() == s2 && instr->src[0].src.ssa->bit_size == 1) {
         Temp src0 = get_alu_src(ctx, instr->src[0]);
         Temp src1 = get_alu_src(ctx, instr->src[1]);
         bld.sop2(aco_opcode::s_xor_b64, Definition(dst), bld.def(s1, scc),
                  as_divergent_bool(ctx, src0, false), as_divergent_bool(ctx, src1, false));
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_op_ult: {
      if (dst.regClass() == s2 && instr->src[0].src.ssa->bit_size == 32)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_lt_u32, dst);
      else if (dst.regClass() == s1 && instr->src[0].src.ssa->bit_size == 32)
         emit_comparison(ctx, instr, aco_opcode::s_cmp_lt_u32, dst);
      else if (dst.regClass() == s2 && instr->src[0].src.ssa->bit_size == 64)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_lt_u64, dst);
      break;
   }
   case nir_op_uge: {
      if (dst.regClass() == s2 && instr->src[0].src.ssa->bit_size == 32)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_ge_u32, dst);
      else if (dst.regClass() == s1 && instr->src[0].src.ssa->bit_size == 32)
         emit_comparison(ctx, instr, aco_opcode::s_cmp_ge_u32, dst);
      else if (dst.regClass() == s2 && instr->src[0].src.ssa->bit_size == 64)
         emit_comparison(ctx, instr, aco_opcode::v_cmp_ge_u64, dst);
      break;
   }
   case nir_op_fddx:
   case nir_op_fddy:
   case nir_op_fddx_fine:
   case nir_op_fddy_fine:
   case nir_op_fddx_coarse:
   case nir_op_fddy_coarse: {
      Definition tl = bld.def(v1);
      uint16_t dpp_ctrl;
      if (instr->op == nir_op_fddx_fine) {
         bld.vop1_dpp(aco_opcode::v_mov_b32, tl, get_alu_src(ctx, instr->src[0]), dpp_quad_perm(0, 0, 2, 2));
         dpp_ctrl = dpp_quad_perm(1, 1, 3, 3);
      } else if (instr->op == nir_op_fddy_fine) {
         bld.vop1_dpp(aco_opcode::v_mov_b32, tl, get_alu_src(ctx, instr->src[0]), dpp_quad_perm(0, 1, 0, 1));
         dpp_ctrl = dpp_quad_perm(2, 3, 2, 3);
      } else {
         bld.vop1_dpp(aco_opcode::v_mov_b32, tl, get_alu_src(ctx, instr->src[0]), dpp_quad_perm(0, 0, 0, 0));
         if (instr->op == nir_op_fddx || instr->op == nir_op_fddx_coarse)
            dpp_ctrl = dpp_quad_perm(1, 1, 1, 1);
         else
            dpp_ctrl = dpp_quad_perm(2, 2, 2, 2);
      }

      Definition tmp = bld.def(v1);
      bld.vop2_dpp(aco_opcode::v_sub_f32, tmp, get_alu_src(ctx, instr->src[0]), tl.getTemp(), dpp_ctrl);
      emit_wqm(ctx, tmp.getTemp(), dst, true);
      break;
   }
   default:
      fprintf(stderr, "Unknown NIR ALU instr: ");
      nir_print_instr(&instr->instr, stderr);
      fprintf(stderr, "\n");
   }
}

void visit_load_const(isel_context *ctx, nir_load_const_instr *instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->def);

   // TODO: we really want to have the resulting type as this would allow for 64bit literals
   // which get truncated the lsb if double and msb if int
   // for now, we only use s_mov_b64 with 64bit inline constants
   assert(instr->def.num_components == 1 && "Vector load_const should be lowered to scalar.");
   assert(dst.type() == RegType::sgpr);

   if (dst.size() == 1)
   {
      Builder(ctx->program, ctx->block).copy(Definition(dst), Operand(instr->value[0].u32));
   } else {
      assert(dst.size() != 1);
      aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, dst.size(), 1)};
      if (instr->def.bit_size == 64)
         for (unsigned i = 0; i < dst.size(); i++)
            vec->operands[i] = Operand{(uint32_t)(instr->value[0].u64 >> i * 32)};
      else {
         for (unsigned i = 0; i < dst.size(); i++)
            vec->operands[i] = Operand{instr->value[i].u32};
      }
      vec->definitions[0] = Definition(dst);
      ctx->block->instructions.emplace_back(std::move(vec));
   }
}

uint32_t widen_mask(uint32_t mask, unsigned multiplier)
{
   uint32_t new_mask = 0;
   for(unsigned i = 0; i < 32 && (1u << i) <= mask; ++i)
      if (mask & (1u << i))
         new_mask |= ((1u << multiplier) - 1u) << (i * multiplier);
   return new_mask;
}

void visit_store_vs_output(isel_context *ctx, nir_intrinsic_instr *instr)
{
   /* This wouldn't work inside control flow or with indirect offsets but
    * that doesn't happen because of nir_lower_io_to_temporaries(). */

   unsigned write_mask = nir_intrinsic_write_mask(instr);
   unsigned component = nir_intrinsic_component(instr);
   Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
   unsigned idx = nir_intrinsic_base(instr) + component;

   nir_instr *off_instr = instr->src[1].ssa->parent_instr;
   if (off_instr->type != nir_instr_type_load_const) {
      fprintf(stderr, "Unimplemented nir_intrinsic_load_input offset\n");
      nir_print_instr(off_instr, stderr);
      fprintf(stderr, "\n");
   }
   idx += nir_instr_as_load_const(off_instr)->value[0].u32 * 4u;

   if (instr->src[0].ssa->bit_size == 64)
      write_mask = widen_mask(write_mask, 2);

   for (unsigned i = 0; i < 8; ++i) {
      if (write_mask & (1 << i)) {
         ctx->vs_output.mask[idx / 4u] |= 1 << (idx % 4u);
         ctx->vs_output.outputs[idx / 4u][idx % 4u] = emit_extract_vector(ctx, src, i, v1);
      }
      idx++;
   }
}

void visit_store_fs_output(isel_context *ctx, nir_intrinsic_instr *instr)
{
   unsigned write_mask = nir_intrinsic_write_mask(instr);
   Operand values[4];
   Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
   for (unsigned i = 0; i < 4; ++i) {
      if (write_mask & (1 << i)) {
         Temp tmp = emit_extract_vector(ctx, src, i, v1);
         values[i] = Operand(tmp);
      } else {
         values[i] = Operand(v1);
      }
   }

   unsigned index = nir_intrinsic_base(instr) / 4;
   unsigned target, col_format;
   unsigned enabled_channels = 0xF;
   aco_opcode compr_op = (aco_opcode)0;

   nir_const_value* offset = nir_src_as_const_value(instr->src[1]);
   assert(offset && "Non-const offsets on exports not yet supported");
   index += offset->u32;

   assert(index != FRAG_RESULT_COLOR);

   /* Unlike vertex shader exports, it's fine to use multiple exports to
    * export separate channels of one target. So shaders which export both
    * FRAG_RESULT_SAMPLE_MASK and FRAG_RESULT_DEPTH should work fine.
    * TODO: combine the exports in those cases and create better code
    */

   if (index == FRAG_RESULT_SAMPLE_MASK) {

      if (ctx->program->info->ps.writes_z) {
         target = V_008DFC_SQ_EXP_MRTZ;
         enabled_channels = 0x4;
         col_format = (unsigned) -1;

         values[2] = values[0];
         values[0] = Operand(v1);
      } else {
         aco_ptr<Export_instruction> exp{create_instruction<Export_instruction>(aco_opcode::exp, Format::EXP, 4, 0)};
         exp->valid_mask = false;
         exp->done = false;
         exp->compressed = true;
         exp->dest = V_008DFC_SQ_EXP_MRTZ;
         exp->enabled_mask = 0xc;
         for (int i = 0; i < 4; i++)
            exp->operands[i] = Operand(v1);
         exp->operands[1] = Operand(values[0]);
         ctx->block->instructions.emplace_back(std::move(exp));
         return;
      }

   } else if (index == FRAG_RESULT_DEPTH) {

      target = V_008DFC_SQ_EXP_MRTZ;
      enabled_channels = 0x1;
      col_format = (unsigned) -1;

   } else if (index == FRAG_RESULT_STENCIL) {

      if (ctx->program->info->ps.writes_z) {
         target = V_008DFC_SQ_EXP_MRTZ;
         enabled_channels = 0x2;
         col_format = (unsigned) -1;

         values[1] = values[0];
         values[0] = Operand(v1);
      } else {
         aco_ptr<Instruction> shift{create_instruction<VOP2_instruction>(aco_opcode::v_lshlrev_b32, Format::VOP2, 2, 1)};
         shift->operands[0] = Operand((uint32_t) 16);
         shift->operands[1] = values[0];
         Temp tmp = {ctx->program->allocateId(), v1};
         shift->definitions[0] = Definition(tmp);
         ctx->block->instructions.emplace_back(std::move(shift));

         aco_ptr<Export_instruction> exp{create_instruction<Export_instruction>(aco_opcode::exp, Format::EXP, 4, 0)};
         exp->valid_mask = false;
         exp->done = false;
         exp->compressed = true;
         exp->dest = V_008DFC_SQ_EXP_MRTZ;
         exp->enabled_mask = 0x3;
         exp->operands[0] = Operand(tmp);
         for (int i = 1; i < 4; i++)
            exp->operands[i] = Operand(v1);
         ctx->block->instructions.emplace_back(std::move(exp));
         return;
      }

   } else {
      index -= FRAG_RESULT_DATA0;
      target = V_008DFC_SQ_EXP_MRT + index;
      col_format = (ctx->options->key.fs.col_format >> (4 * index)) & 0xf;
   }
   ASSERTED bool is_int8 = (ctx->options->key.fs.is_int8 >> index) & 1;
   ASSERTED bool is_int10 = (ctx->options->key.fs.is_int10 >> index) & 1;
   assert(!is_int8 && !is_int10);

   switch (col_format)
   {
   case V_028714_SPI_SHADER_ZERO:
      enabled_channels = 0; /* writemask */
      target = V_008DFC_SQ_EXP_NULL;
      break;

   case V_028714_SPI_SHADER_32_R:
      enabled_channels = 1;
      break;

   case V_028714_SPI_SHADER_32_GR:
      enabled_channels = 0x3;
      break;

   case V_028714_SPI_SHADER_32_AR:
      if (ctx->options->chip_class >= GFX10) {
         /* Special case: on GFX10, the outputs are different for 32_AR */
         enabled_channels = 0x3;
         values[1] = values[3];
      } else {
         enabled_channels = 0x9;
      }
      break;

   case V_028714_SPI_SHADER_FP16_ABGR:
      enabled_channels = 0x5;
      compr_op = aco_opcode::v_cvt_pkrtz_f16_f32;
      break;

   case V_028714_SPI_SHADER_UNORM16_ABGR:
      enabled_channels = 0x5;
      compr_op = aco_opcode::v_cvt_pknorm_u16_f32;
      break;

   case V_028714_SPI_SHADER_SNORM16_ABGR:
      enabled_channels = 0x5;
      compr_op = aco_opcode::v_cvt_pknorm_i16_f32;
      break;

   case V_028714_SPI_SHADER_UINT16_ABGR:
      enabled_channels = 0x5;
      compr_op = aco_opcode::v_cvt_pk_u16_u32;
      break;

   case V_028714_SPI_SHADER_SINT16_ABGR:
      enabled_channels = 0x5;
      compr_op = aco_opcode::v_cvt_pk_i16_i32;
      break;

   case V_028714_SPI_SHADER_32_ABGR:
      enabled_channels = 0xF;
      break;

   default:
      break;
   }

   if (target == V_008DFC_SQ_EXP_NULL)
      return;

   if ((bool)compr_op)
   {
      for (int i = 0; i < 2; i++)
      {
         /* check if at least one of the values to be compressed is enabled */
         unsigned enabled = (write_mask >> (i*2) | write_mask >> (i*2+1)) & 0x1;
         if (enabled) {
            enabled_channels |= enabled << (i*2);
            aco_ptr<VOP3A_instruction> compr{create_instruction<VOP3A_instruction>(compr_op, Format::VOP3A, 2, 1)};
            Temp tmp{ctx->program->allocateId(), v1};
            compr->operands[0] = values[i*2].isUndefined() ? Operand(0u) : values[i*2];
            compr->operands[1] = values[i*2+1].isUndefined() ? Operand(0u): values[i*2+1];
            compr->definitions[0] = Definition(tmp);
            values[i] = Operand(tmp);
            ctx->block->instructions.emplace_back(std::move(compr));
         } else {
            values[i] = Operand(v1);
         }
      }
   }

   aco_ptr<Export_instruction> exp{create_instruction<Export_instruction>(aco_opcode::exp, Format::EXP, 4, 0)};
   exp->valid_mask = false;
   exp->done = false;
   exp->compressed = (bool) compr_op;
   exp->dest = target;
   exp->enabled_mask = enabled_channels;
   if ((bool) compr_op) {
      for (int i = 0; i < 2; i++)
         exp->operands[i] = enabled_channels & (3 << (i * 2)) ? values[i] : Operand(v1);
      exp->operands[2] = Operand(v1);
      exp->operands[3] = Operand(v1);
   } else {
      for (int i = 0; i < 4; i++)
         exp->operands[i] = enabled_channels & (1 << i) ? values[i] : Operand(v1);
   }

   ctx->block->instructions.emplace_back(std::move(exp));
}

Operand load_lds_size_m0(isel_context *ctx)
{
   /* TODO: m0 does not need to be initialized on GFX9+ */
   Builder bld(ctx->program, ctx->block);
   return bld.m0((Temp)bld.sopk(aco_opcode::s_movk_i32, bld.def(s1, m0), 0xffff));
}

void load_lds(isel_context *ctx, unsigned elem_size_bytes, Temp dst,
              Temp address, unsigned base_offset, unsigned align)
{
   assert(util_is_power_of_two_nonzero(align) && align >= 4);

   Builder bld(ctx->program, ctx->block);

   Operand m = load_lds_size_m0(ctx);

   unsigned num_components = dst.size() * 4u / elem_size_bytes;
   unsigned bytes_read = 0;
   unsigned result_size = 0;
   unsigned total_bytes = num_components * elem_size_bytes;
   std::array<Temp, 4> result;

   while (bytes_read < total_bytes) {
      unsigned todo = total_bytes - bytes_read;
      bool aligned8 = bytes_read % 8 == 0 && align % 8 == 0;
      bool aligned16 = bytes_read % 16 == 0 && align % 16 == 0;

      aco_opcode op = aco_opcode::last_opcode;
      bool read2 = false;
      if (todo >= 16 && aligned16) {
         op = aco_opcode::ds_read_b128;
         todo = 16;
      } else if (todo >= 16 && aligned8) {
         op = aco_opcode::ds_read2_b64;
         read2 = true;
         todo = 16;
      } else if (todo >= 12 && aligned16) {
         op = aco_opcode::ds_read_b96;
         todo = 12;
      } else if (todo >= 8 && aligned8) {
         op = aco_opcode::ds_read_b64;
         todo = 8;
      } else if (todo >= 8) {
         op = aco_opcode::ds_read2_b32;
         read2 = true;
         todo = 8;
      } else if (todo >= 4) {
         op = aco_opcode::ds_read_b32;
         todo = 4;
      } else {
         assert(false);
      }
      assert(todo % elem_size_bytes == 0);
      unsigned num_elements = todo / elem_size_bytes;
      unsigned offset = base_offset + bytes_read;
      unsigned max_offset = read2 ? 1019 : 65535;

      Temp address_offset = address;
      if (offset > max_offset) {
         address_offset = bld.vadd32(bld.def(v1), Operand(base_offset), address_offset);
         offset = bytes_read;
      }
      assert(offset <= max_offset); /* bytes_read shouldn't be large enough for this to happen */

      Temp res;
      if (num_components == 1 && dst.type() == RegType::vgpr)
         res = dst;
      else
         res = bld.tmp(RegClass(RegType::vgpr, todo / 4));

      if (read2)
         res = bld.ds(op, Definition(res), address_offset, m, offset >> 2, (offset >> 2) + 1);
      else
         res = bld.ds(op, Definition(res), address_offset, m, offset);

      if (num_components == 1) {
         assert(todo == total_bytes);
         if (dst.type() == RegType::sgpr)
            bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), res);
         return;
      }

      if (dst.type() == RegType::sgpr)
         res = bld.as_uniform(res);

      if (num_elements == 1) {
         result[result_size++] = res;
      } else {
         assert(res != dst && res.size() % num_elements == 0);
         aco_ptr<Pseudo_instruction> split{create_instruction<Pseudo_instruction>(aco_opcode::p_split_vector, Format::PSEUDO, 1, num_elements)};
         split->operands[0] = Operand(res);
         for (unsigned i = 0; i < num_elements; i++)
            split->definitions[i] = Definition(result[result_size++] = bld.tmp(res.type(), elem_size_bytes / 4));
         ctx->block->instructions.emplace_back(std::move(split));
      }

      bytes_read += todo;
   }

   assert(result_size == num_components && result_size > 1);
   aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, result_size, 1)};
   for (unsigned i = 0; i < result_size; i++)
      vec->operands[i] = Operand(result[i]);
   vec->definitions[0] = Definition(dst);
   ctx->block->instructions.emplace_back(std::move(vec));
   ctx->allocated_vec.emplace(dst.id(), result);
}

Temp extract_subvector(isel_context *ctx, Temp data, unsigned start, unsigned size, RegType type)
{
   if (start == 0 && size == data.size())
      return type == RegType::vgpr ? as_vgpr(ctx, data) : data;

   unsigned size_hint = 1;
   auto it = ctx->allocated_vec.find(data.id());
   if (it != ctx->allocated_vec.end())
      size_hint = it->second[0].size();
   if (size % size_hint || start % size_hint)
      size_hint = 1;

   start /= size_hint;
   size /= size_hint;

   Temp elems[size];
   for (unsigned i = 0; i < size; i++)
      elems[i] = emit_extract_vector(ctx, data, start + i, RegClass(type, size_hint));

   if (size == 1)
      return type == RegType::vgpr ? as_vgpr(ctx, elems[0]) : elems[0];

   aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, size, 1)};
   for (unsigned i = 0; i < size; i++)
      vec->operands[i] = Operand(elems[i]);
   Temp res = {ctx->program->allocateId(), RegClass(type, size * size_hint)};
   vec->definitions[0] = Definition(res);
   ctx->block->instructions.emplace_back(std::move(vec));
   return res;
}

void ds_write_helper(isel_context *ctx, Operand m, Temp address, Temp data, unsigned data_start, unsigned total_size, unsigned offset0, unsigned offset1, unsigned align)
{
   Builder bld(ctx->program, ctx->block);
   unsigned bytes_written = 0;
   while (bytes_written < total_size * 4) {
      unsigned todo = total_size * 4 - bytes_written;
      bool aligned8 = bytes_written % 8 == 0 && align % 8 == 0;
      bool aligned16 = bytes_written % 16 == 0 && align % 16 == 0;

      aco_opcode op = aco_opcode::last_opcode;
      bool write2 = false;
      unsigned size = 0;
      if (todo >= 16 && aligned16) {
         op = aco_opcode::ds_write_b128;
         size = 4;
      } else if (todo >= 16 && aligned8) {
         op = aco_opcode::ds_write2_b64;
         write2 = true;
         size = 4;
      } else if (todo >= 12 && aligned16) {
         op = aco_opcode::ds_write_b96;
         size = 3;
      } else if (todo >= 8 && aligned8) {
         op = aco_opcode::ds_write_b64;
         size = 2;
      } else if (todo >= 8) {
         op = aco_opcode::ds_write2_b32;
         write2 = true;
         size = 2;
      } else if (todo >= 4) {
         op = aco_opcode::ds_write_b32;
         size = 1;
      } else {
         assert(false);
      }

      unsigned offset = offset0 + offset1 + bytes_written;
      unsigned max_offset = write2 ? 1020 : 65535;
      Temp address_offset = address;
      if (offset > max_offset) {
         address_offset = bld.vadd32(bld.def(v1), Operand(offset0), address_offset);
         offset = offset1 + bytes_written;
      }
      assert(offset <= max_offset); /* offset1 shouldn't be large enough for this to happen */

      if (write2) {
         Temp val0 = extract_subvector(ctx, data, data_start + (bytes_written >> 2), size / 2, RegType::vgpr);
         Temp val1 = extract_subvector(ctx, data, data_start + (bytes_written >> 2) + 1, size / 2, RegType::vgpr);
         bld.ds(op, address_offset, val0, val1, m, offset >> 2, (offset >> 2) + 1);
      } else {
         Temp val = extract_subvector(ctx, data, data_start + (bytes_written >> 2), size, RegType::vgpr);
         bld.ds(op, address_offset, val, m, offset);
      }

      bytes_written += size * 4;
   }
}

void store_lds(isel_context *ctx, unsigned elem_size_bytes, Temp data, uint32_t wrmask,
               Temp address, unsigned base_offset, unsigned align)
{
   assert(util_is_power_of_two_nonzero(align) && align >= 4);

   Operand m = load_lds_size_m0(ctx);

   /* we need at most two stores for 32bit variables */
   int start[2], count[2];
   u_bit_scan_consecutive_range(&wrmask, &start[0], &count[0]);
   u_bit_scan_consecutive_range(&wrmask, &start[1], &count[1]);
   assert(wrmask == 0);

   /* one combined store is sufficient */
   if (count[0] == count[1]) {
      Builder bld(ctx->program, ctx->block);

      Temp address_offset = address;
      if ((base_offset >> 2) + start[1] > 255) {
         address_offset = bld.vadd32(bld.def(v1), Operand(base_offset), address_offset);
         base_offset = 0;
      }

      assert(count[0] == 1);
      Temp val0 = emit_extract_vector(ctx, data, start[0], v1);
      Temp val1 = emit_extract_vector(ctx, data, start[1], v1);
      aco_opcode op = elem_size_bytes == 4 ? aco_opcode::ds_write2_b32 : aco_opcode::ds_write2_b64;
      base_offset = base_offset / elem_size_bytes;
      bld.ds(op, address_offset, val0, val1, m,
             base_offset + start[0], base_offset + start[1]);
      return;
   }

   for (unsigned i = 0; i < 2; i++) {
      if (count[i] == 0)
         continue;

      unsigned elem_size_words = elem_size_bytes / 4;
      ds_write_helper(ctx, m, address, data, start[i] * elem_size_words, count[i] * elem_size_words,
                      base_offset, start[i] * elem_size_bytes, align);
   }
   return;
}

void visit_store_output(isel_context *ctx, nir_intrinsic_instr *instr)
{
   if (ctx->stage == vertex_vs) {
      visit_store_vs_output(ctx, instr);
   } else if (ctx->stage == fragment_fs) {
      visit_store_fs_output(ctx, instr);
   } else {
      unreachable("Shader stage not implemented");
   }
}

void emit_interp_instr(isel_context *ctx, unsigned idx, unsigned component, Temp src, Temp dst, Temp prim_mask)
{
   Temp coord1 = emit_extract_vector(ctx, src, 0, v1);
   Temp coord2 = emit_extract_vector(ctx, src, 1, v1);

   Builder bld(ctx->program, ctx->block);
   Temp tmp = bld.vintrp(aco_opcode::v_interp_p1_f32, bld.def(v1), coord1, bld.m0(prim_mask), idx, component);
   bld.vintrp(aco_opcode::v_interp_p2_f32, Definition(dst), coord2, bld.m0(prim_mask), tmp, idx, component);
}

void emit_load_frag_coord(isel_context *ctx, Temp dst, unsigned num_components)
{
   aco_ptr<Pseudo_instruction> vec(create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, num_components, 1));
   for (unsigned i = 0; i < num_components; i++)
      vec->operands[i] = Operand(ctx->fs_inputs[fs_input::frag_pos_0 + i]);

   if (ctx->fs_vgpr_args[fs_input::frag_pos_3]) {
      assert(num_components == 4);
      Builder bld(ctx->program, ctx->block);
      vec->operands[3] = bld.vop1(aco_opcode::v_rcp_f32, bld.def(v1), ctx->fs_inputs[fs_input::frag_pos_3]);
   }

   for (Operand& op : vec->operands)
      op = op.isUndefined() ? Operand(0u) : op;

   vec->definitions[0] = Definition(dst);
   ctx->block->instructions.emplace_back(std::move(vec));
   emit_split_vector(ctx, dst, num_components);
   return;
}

void visit_load_interpolated_input(isel_context *ctx, nir_intrinsic_instr *instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   Temp coords = get_ssa_temp(ctx, instr->src[0].ssa);
   unsigned idx = nir_intrinsic_base(instr);
   unsigned component = nir_intrinsic_component(instr);
   Temp prim_mask = ctx->prim_mask;

   nir_const_value* offset = nir_src_as_const_value(instr->src[1]);
   if (offset) {
      assert(offset->u32 == 0);
   } else {
      /* the lower 15bit of the prim_mask contain the offset into LDS
       * while the upper bits contain the number of prims */
      Temp offset_src = get_ssa_temp(ctx, instr->src[1].ssa);
      assert(offset_src.regClass() == s1 && "TODO: divergent offsets...");
      Builder bld(ctx->program, ctx->block);
      Temp stride = bld.sop2(aco_opcode::s_lshr_b32, bld.def(s1), bld.def(s1, scc), prim_mask, Operand(16u));
      stride = bld.sop1(aco_opcode::s_bcnt1_i32_b32, bld.def(s1), bld.def(s1, scc), stride);
      stride = bld.sop2(aco_opcode::s_mul_i32, bld.def(s1), stride, Operand(48u));
      offset_src = bld.sop2(aco_opcode::s_mul_i32, bld.def(s1), stride, offset_src);
      prim_mask = bld.sop2(aco_opcode::s_add_i32, bld.def(s1, m0), bld.def(s1, scc), offset_src, prim_mask);
   }

   if (instr->dest.ssa.num_components == 1) {
      emit_interp_instr(ctx, idx, component, coords, dst, prim_mask);
   } else {
      aco_ptr<Pseudo_instruction> vec(create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, instr->dest.ssa.num_components, 1));
      for (unsigned i = 0; i < instr->dest.ssa.num_components; i++)
      {
         Temp tmp = {ctx->program->allocateId(), v1};
         emit_interp_instr(ctx, idx, component+i, coords, tmp, prim_mask);
         vec->operands[i] = Operand(tmp);
      }
      vec->definitions[0] = Definition(dst);
      ctx->block->instructions.emplace_back(std::move(vec));
   }
}

unsigned get_num_channels_from_data_format(unsigned data_format)
{
   switch (data_format) {
   case V_008F0C_BUF_DATA_FORMAT_8:
   case V_008F0C_BUF_DATA_FORMAT_16:
   case V_008F0C_BUF_DATA_FORMAT_32:
      return 1;
   case V_008F0C_BUF_DATA_FORMAT_8_8:
   case V_008F0C_BUF_DATA_FORMAT_16_16:
   case V_008F0C_BUF_DATA_FORMAT_32_32:
      return 2;
   case V_008F0C_BUF_DATA_FORMAT_10_11_11:
   case V_008F0C_BUF_DATA_FORMAT_11_11_10:
   case V_008F0C_BUF_DATA_FORMAT_32_32_32:
      return 3;
   case V_008F0C_BUF_DATA_FORMAT_8_8_8_8:
   case V_008F0C_BUF_DATA_FORMAT_10_10_10_2:
   case V_008F0C_BUF_DATA_FORMAT_2_10_10_10:
   case V_008F0C_BUF_DATA_FORMAT_16_16_16_16:
   case V_008F0C_BUF_DATA_FORMAT_32_32_32_32:
      return 4;
   default:
      break;
   }

   return 4;
}

/* For 2_10_10_10 formats the alpha is handled as unsigned by pre-vega HW.
 * so we may need to fix it up. */
Temp adjust_vertex_fetch_alpha(isel_context *ctx, unsigned adjustment, Temp alpha)
{
   Builder bld(ctx->program, ctx->block);

   if (adjustment == RADV_ALPHA_ADJUST_SSCALED)
      alpha = bld.vop1(aco_opcode::v_cvt_u32_f32, bld.def(v1), alpha);

   /* For the integer-like cases, do a natural sign extension.
    *
    * For the SNORM case, the values are 0.0, 0.333, 0.666, 1.0
    * and happen to contain 0, 1, 2, 3 as the two LSBs of the
    * exponent.
    */
   alpha = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand(adjustment == RADV_ALPHA_ADJUST_SNORM ? 7u : 30u), alpha);
   alpha = bld.vop2(aco_opcode::v_ashrrev_i32, bld.def(v1), Operand(30u), alpha);

   /* Convert back to the right type. */
   if (adjustment == RADV_ALPHA_ADJUST_SNORM) {
      alpha = bld.vop1(aco_opcode::v_cvt_f32_i32, bld.def(v1), alpha);
      Temp clamp = bld.vopc(aco_opcode::v_cmp_le_f32, bld.hint_vcc(bld.def(s2)), Operand(0xbf800000u), alpha);
      alpha = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), Operand(0xbf800000u), alpha, clamp);
   } else if (adjustment == RADV_ALPHA_ADJUST_SSCALED) {
      alpha = bld.vop1(aco_opcode::v_cvt_f32_i32, bld.def(v1), alpha);
   }

   return alpha;
}

void visit_load_input(isel_context *ctx, nir_intrinsic_instr *instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   if (ctx->stage & sw_vs) {

      nir_instr *off_instr = instr->src[0].ssa->parent_instr;
      if (off_instr->type != nir_instr_type_load_const) {
         fprintf(stderr, "Unimplemented nir_intrinsic_load_input offset\n");
         nir_print_instr(off_instr, stderr);
         fprintf(stderr, "\n");
      }
      uint32_t offset = nir_instr_as_load_const(off_instr)->value[0].u32;

      Temp vertex_buffers = convert_pointer_to_64_bit(ctx, ctx->vertex_buffers);

      unsigned location = nir_intrinsic_base(instr) / 4 - VERT_ATTRIB_GENERIC0 + offset;
      unsigned component = nir_intrinsic_component(instr);
      unsigned attrib_binding = ctx->options->key.vs.vertex_attribute_bindings[location];
      uint32_t attrib_offset = ctx->options->key.vs.vertex_attribute_offsets[location];
      uint32_t attrib_stride = ctx->options->key.vs.vertex_attribute_strides[location];
      unsigned attrib_format = ctx->options->key.vs.vertex_attribute_formats[location];

      unsigned dfmt = attrib_format & 0xf;

      unsigned nfmt = (attrib_format >> 4) & 0x7;
      unsigned num_dfmt_channels = get_num_channels_from_data_format(dfmt);
      unsigned mask = nir_ssa_def_components_read(&instr->dest.ssa) << component;
      unsigned num_channels = MIN2(util_last_bit(mask), num_dfmt_channels);
      unsigned alpha_adjust = (ctx->options->key.vs.alpha_adjust >> (location * 2)) & 3;
      bool post_shuffle = ctx->options->key.vs.post_shuffle & (1 << location);
      if (post_shuffle)
         num_channels = MAX2(num_channels, 3);

      Temp list = bld.smem(aco_opcode::s_load_dwordx4, bld.def(s4), vertex_buffers, Operand(attrib_binding * 16u));

      Temp index;
      if (ctx->options->key.vs.instance_rate_inputs & (1u << location)) {
         uint32_t divisor = ctx->options->key.vs.instance_rate_divisors[location];
         if (divisor) {
            ctx->needs_instance_id = true;

            if (divisor != 1) {
               Temp divided = bld.tmp(v1);
               emit_v_div_u32(ctx, divided, as_vgpr(ctx, ctx->instance_id), divisor);
               index = bld.vadd32(bld.def(v1), ctx->start_instance, divided);
            } else {
               index = bld.vadd32(bld.def(v1), ctx->start_instance, ctx->instance_id);
            }
         } else {
            index = bld.vop1(aco_opcode::v_mov_b32, bld.def(v1), ctx->start_instance);
         }
      } else {
         index = bld.vadd32(bld.def(v1), ctx->base_vertex, ctx->vertex_id);
      }

      if (attrib_stride != 0 && attrib_offset > attrib_stride) {
         index = bld.vadd32(bld.def(v1), Operand(attrib_offset / attrib_stride), index);
         attrib_offset = attrib_offset % attrib_stride;
      }

      Operand soffset(0u);
      if (attrib_offset >= 4096) {
         soffset = bld.copy(bld.def(s1), Operand(attrib_offset));
         attrib_offset = 0;
      }

      aco_opcode opcode;
      switch (num_channels) {
      case 1:
         opcode = aco_opcode::tbuffer_load_format_x;
         break;
      case 2:
         opcode = aco_opcode::tbuffer_load_format_xy;
         break;
      case 3:
         opcode = aco_opcode::tbuffer_load_format_xyz;
         break;
      case 4:
         opcode = aco_opcode::tbuffer_load_format_xyzw;
         break;
      default:
         unreachable("Unimplemented load_input vector size");
      }

      Temp tmp = post_shuffle || num_channels != dst.size() || alpha_adjust != RADV_ALPHA_ADJUST_NONE || component ? bld.tmp(RegType::vgpr, num_channels) : dst;

      aco_ptr<MTBUF_instruction> mubuf{create_instruction<MTBUF_instruction>(opcode, Format::MTBUF, 3, 1)};
      mubuf->operands[0] = Operand(index);
      mubuf->operands[1] = Operand(list);
      mubuf->operands[2] = soffset;
      mubuf->definitions[0] = Definition(tmp);
      mubuf->idxen = true;
      mubuf->can_reorder = true;
      mubuf->dfmt = dfmt;
      mubuf->nfmt = nfmt;
      assert(attrib_offset < 4096);
      mubuf->offset = attrib_offset;
      ctx->block->instructions.emplace_back(std::move(mubuf));

      emit_split_vector(ctx, tmp, tmp.size());

      if (tmp.id() != dst.id()) {
         bool is_float = nfmt != V_008F0C_BUF_NUM_FORMAT_UINT &&
                         nfmt != V_008F0C_BUF_NUM_FORMAT_SINT;

         static const unsigned swizzle_normal[4] = {0, 1, 2, 3};
         static const unsigned swizzle_post_shuffle[4] = {2, 1, 0, 3};
         const unsigned *swizzle = post_shuffle ? swizzle_post_shuffle : swizzle_normal;

         aco_ptr<Instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, dst.size(), 1)};
         for (unsigned i = 0; i < dst.size(); i++) {
            unsigned idx = i + component;
            if (idx == 3 && alpha_adjust != RADV_ALPHA_ADJUST_NONE && num_channels >= 4) {
               Temp alpha = emit_extract_vector(ctx, tmp, swizzle[3], v1);
               vec->operands[3] = Operand(adjust_vertex_fetch_alpha(ctx, alpha_adjust, alpha));
            } else if (idx < num_channels) {
               vec->operands[i] = Operand(emit_extract_vector(ctx, tmp, swizzle[idx], v1));
            } else if (is_float && idx == 3) {
               vec->operands[i] = Operand(0x3f800000u);
            } else if (!is_float && idx == 3) {
               vec->operands[i] = Operand(1u);
            } else {
               vec->operands[i] = Operand(0u);
            }
         }
         vec->definitions[0] = Definition(dst);
         ctx->block->instructions.emplace_back(std::move(vec));
         emit_split_vector(ctx, dst, dst.size());
      }

   } else if (ctx->stage == fragment_fs) {
      nir_instr *off_instr = instr->src[0].ssa->parent_instr;
      if (off_instr->type != nir_instr_type_load_const ||
          nir_instr_as_load_const(off_instr)->value[0].u32 != 0) {
         fprintf(stderr, "Unimplemented nir_intrinsic_load_input offset\n");
         nir_print_instr(off_instr, stderr);
         fprintf(stderr, "\n");
      }

      Temp prim_mask = ctx->prim_mask;
      nir_const_value* offset = nir_src_as_const_value(instr->src[0]);
      if (offset) {
         assert(offset->u32 == 0);
      } else {
         /* the lower 15bit of the prim_mask contain the offset into LDS
          * while the upper bits contain the number of prims */
         Temp offset_src = get_ssa_temp(ctx, instr->src[0].ssa);
         assert(offset_src.regClass() == s1 && "TODO: divergent offsets...");
         Builder bld(ctx->program, ctx->block);
         Temp stride = bld.sop2(aco_opcode::s_lshr_b32, bld.def(s1), bld.def(s1, scc), prim_mask, Operand(16u));
         stride = bld.sop1(aco_opcode::s_bcnt1_i32_b32, bld.def(s1), bld.def(s1, scc), stride);
         stride = bld.sop2(aco_opcode::s_mul_i32, bld.def(s1), stride, Operand(48u));
         offset_src = bld.sop2(aco_opcode::s_mul_i32, bld.def(s1), stride, offset_src);
         prim_mask = bld.sop2(aco_opcode::s_add_i32, bld.def(s1, m0), bld.def(s1, scc), offset_src, prim_mask);
      }

      unsigned idx = nir_intrinsic_base(instr);
      unsigned component = nir_intrinsic_component(instr);

      if (dst.size() == 1) {
         bld.vintrp(aco_opcode::v_interp_mov_f32, Definition(dst), Operand(2u), bld.m0(prim_mask), idx, component);
      } else {
         aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, dst.size(), 1)};
         for (unsigned i = 0; i < dst.size(); i++)
            vec->operands[i] = bld.vintrp(aco_opcode::v_interp_mov_f32, bld.def(v1), Operand(2u), bld.m0(prim_mask), idx, component + i);
         vec->definitions[0] = Definition(dst);
         bld.insert(std::move(vec));
      }

   } else {
      unreachable("Shader stage not implemented");
   }
}

Temp load_desc_ptr(isel_context *ctx, unsigned desc_set)
{
   if (ctx->program->info->need_indirect_descriptor_sets) {
      Builder bld(ctx->program, ctx->block);
      Temp ptr64 = convert_pointer_to_64_bit(ctx, ctx->descriptor_sets[0]);
      return bld.smem(aco_opcode::s_load_dword, bld.def(s1), ptr64, Operand(desc_set << 2));//, false, false, false);
   }

   return ctx->descriptor_sets[desc_set];
}


void visit_load_resource(isel_context *ctx, nir_intrinsic_instr *instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp index = get_ssa_temp(ctx, instr->src[0].ssa);
   if (!ctx->divergent_vals[instr->dest.ssa.index])
      index = bld.as_uniform(index);
   unsigned desc_set = nir_intrinsic_desc_set(instr);
   unsigned binding = nir_intrinsic_binding(instr);

   Temp desc_ptr;
   radv_pipeline_layout *pipeline_layout = ctx->options->layout;
   radv_descriptor_set_layout *layout = pipeline_layout->set[desc_set].layout;
   unsigned offset = layout->binding[binding].offset;
   unsigned stride;
   if (layout->binding[binding].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
       layout->binding[binding].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
      unsigned idx = pipeline_layout->set[desc_set].dynamic_offset_start + layout->binding[binding].dynamic_offset_offset;
      desc_ptr = ctx->push_constants;
      offset = pipeline_layout->push_constant_size + 16 * idx;
      stride = 16;
   } else {
      desc_ptr = load_desc_ptr(ctx, desc_set);
      stride = layout->binding[binding].size;
   }

   nir_const_value* nir_const_index = nir_src_as_const_value(instr->src[0]);
   unsigned const_index = nir_const_index ? nir_const_index->u32 : 0;
   if (stride != 1) {
      if (nir_const_index) {
         const_index = const_index * stride;
      } else if (index.type() == RegType::vgpr) {
         bool index24bit = layout->binding[binding].array_size <= 0x1000000;
         index = bld.v_mul_imm(bld.def(v1), index, stride, index24bit);
      } else {
         index = bld.sop2(aco_opcode::s_mul_i32, bld.def(s1), Operand(stride), Operand(index));
      }
   }
   if (offset) {
      if (nir_const_index) {
         const_index = const_index + offset;
      } else if (index.type() == RegType::vgpr) {
         index = bld.vadd32(bld.def(v1), Operand(offset), index);
      } else {
         index = bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc), Operand(offset), Operand(index));
      }
   }

   if (nir_const_index && const_index == 0) {
      index = desc_ptr;
   } else if (index.type() == RegType::vgpr) {
      index = bld.vadd32(bld.def(v1),
                         nir_const_index ? Operand(const_index) : Operand(index),
                         Operand(desc_ptr));
   } else {
      index = bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc),
                       nir_const_index ? Operand(const_index) : Operand(index),
                       Operand(desc_ptr));
   }

   bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)), index);
}

void load_buffer(isel_context *ctx, unsigned num_components, Temp dst,
                 Temp rsrc, Temp offset, bool glc=false, bool readonly=true)
{
   Builder bld(ctx->program, ctx->block);

   unsigned num_bytes = dst.size() * 4;
   bool dlc = glc && ctx->options->chip_class >= GFX10;

   aco_opcode op;
   if (dst.type() == RegType::vgpr || (glc && ctx->options->chip_class < GFX8)) {
      if (ctx->options->chip_class < GFX8)
         offset = as_vgpr(ctx, offset);

      Operand vaddr = offset.type() == RegType::vgpr ? Operand(offset) : Operand(v1);
      Operand soffset = offset.type() == RegType::sgpr ? Operand(offset) : Operand((uint32_t) 0);
      unsigned const_offset = 0;

      Temp lower = Temp();
      if (num_bytes > 16) {
         assert(num_components == 3 || num_components == 4);
         op = aco_opcode::buffer_load_dwordx4;
         lower = bld.tmp(v4);
         aco_ptr<MUBUF_instruction> mubuf{create_instruction<MUBUF_instruction>(op, Format::MUBUF, 3, 1)};
         mubuf->definitions[0] = Definition(lower);
         mubuf->operands[0] = vaddr;
         mubuf->operands[1] = Operand(rsrc);
         mubuf->operands[2] = soffset;
         mubuf->offen = (offset.type() == RegType::vgpr);
         mubuf->glc = glc;
         mubuf->dlc = dlc;
         mubuf->barrier = readonly ? barrier_none : barrier_buffer;
         mubuf->can_reorder = readonly;
         bld.insert(std::move(mubuf));
         emit_split_vector(ctx, lower, 2);
         num_bytes -= 16;
         const_offset = 16;
      }

      switch (num_bytes) {
         case 4:
            op = aco_opcode::buffer_load_dword;
            break;
         case 8:
            op = aco_opcode::buffer_load_dwordx2;
            break;
         case 12:
            op = aco_opcode::buffer_load_dwordx3;
            break;
         case 16:
            op = aco_opcode::buffer_load_dwordx4;
            break;
         default:
            unreachable("Load SSBO not implemented for this size.");
      }
      aco_ptr<MUBUF_instruction> mubuf{create_instruction<MUBUF_instruction>(op, Format::MUBUF, 3, 1)};
      mubuf->operands[0] = vaddr;
      mubuf->operands[1] = Operand(rsrc);
      mubuf->operands[2] = soffset;
      mubuf->offen = (offset.type() == RegType::vgpr);
      mubuf->glc = glc;
      mubuf->dlc = dlc;
      mubuf->barrier = readonly ? barrier_none : barrier_buffer;
      mubuf->can_reorder = readonly;
      mubuf->offset = const_offset;
      aco_ptr<Instruction> instr = std::move(mubuf);

      if (dst.size() > 4) {
         assert(lower != Temp());
         Temp upper = bld.tmp(RegType::vgpr, dst.size() - lower.size());
         instr->definitions[0] = Definition(upper);
         bld.insert(std::move(instr));
         if (dst.size() == 8)
            emit_split_vector(ctx, upper, 2);
         instr.reset(create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, dst.size() / 2, 1));
         instr->operands[0] = Operand(emit_extract_vector(ctx, lower, 0, v2));
         instr->operands[1] = Operand(emit_extract_vector(ctx, lower, 1, v2));
         instr->operands[2] = Operand(emit_extract_vector(ctx, upper, 0, v2));
         if (dst.size() == 8)
            instr->operands[3] = Operand(emit_extract_vector(ctx, upper, 1, v2));
      }

      if (dst.type() == RegType::sgpr) {
         Temp vec = bld.tmp(RegType::vgpr, dst.size());
         instr->definitions[0] = Definition(vec);
         bld.insert(std::move(instr));
         bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), vec);
      } else {
         instr->definitions[0] = Definition(dst);
         bld.insert(std::move(instr));
      }
   } else {
      switch (num_bytes) {
         case 4:
            op = aco_opcode::s_buffer_load_dword;
            break;
         case 8:
            op = aco_opcode::s_buffer_load_dwordx2;
            break;
         case 12:
         case 16:
            op = aco_opcode::s_buffer_load_dwordx4;
            break;
         case 24:
         case 32:
            op = aco_opcode::s_buffer_load_dwordx8;
            break;
         default:
            unreachable("Load SSBO not implemented for this size.");
      }
      aco_ptr<SMEM_instruction> load{create_instruction<SMEM_instruction>(op, Format::SMEM, 2, 1)};
      load->operands[0] = Operand(rsrc);
      load->operands[1] = Operand(bld.as_uniform(offset));
      assert(load->operands[1].getTemp().type() == RegType::sgpr);
      load->definitions[0] = Definition(dst);
      load->glc = glc;
      load->dlc = dlc;
      load->barrier = readonly ? barrier_none : barrier_buffer;
      load->can_reorder = false; // FIXME: currently, it doesn't seem beneficial due to how our scheduler works
      assert(ctx->options->chip_class >= GFX8 || !glc);

      /* trim vector */
      if (dst.size() == 3) {
         Temp vec = bld.tmp(s4);
         load->definitions[0] = Definition(vec);
         bld.insert(std::move(load));
         emit_split_vector(ctx, vec, 4);

         bld.pseudo(aco_opcode::p_create_vector, Definition(dst),
                    emit_extract_vector(ctx, vec, 0, s1),
                    emit_extract_vector(ctx, vec, 1, s1),
                    emit_extract_vector(ctx, vec, 2, s1));
      } else if (dst.size() == 6) {
         Temp vec = bld.tmp(s8);
         load->definitions[0] = Definition(vec);
         bld.insert(std::move(load));
         emit_split_vector(ctx, vec, 4);

         bld.pseudo(aco_opcode::p_create_vector, Definition(dst),
                    emit_extract_vector(ctx, vec, 0, s2),
                    emit_extract_vector(ctx, vec, 1, s2),
                    emit_extract_vector(ctx, vec, 2, s2));
      } else {
         bld.insert(std::move(load));
      }

   }
   emit_split_vector(ctx, dst, num_components);
}

void visit_load_ubo(isel_context *ctx, nir_intrinsic_instr *instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   Temp rsrc = get_ssa_temp(ctx, instr->src[0].ssa);

   Builder bld(ctx->program, ctx->block);

   nir_intrinsic_instr* idx_instr = nir_instr_as_intrinsic(instr->src[0].ssa->parent_instr);
   unsigned desc_set = nir_intrinsic_desc_set(idx_instr);
   unsigned binding = nir_intrinsic_binding(idx_instr);
   radv_descriptor_set_layout *layout = ctx->options->layout->set[desc_set].layout;

   if (layout->binding[binding].type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT) {
      uint32_t desc_type = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
                           S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                           S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
                           S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);
      if (ctx->options->chip_class >= GFX10) {
         desc_type |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_FLOAT) |
                      S_008F0C_OOB_SELECT(3) |
                      S_008F0C_RESOURCE_LEVEL(1);
      } else {
         desc_type |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                      S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
      }
      Temp upper_dwords = bld.pseudo(aco_opcode::p_create_vector, bld.def(s3),
                                     Operand(S_008F04_BASE_ADDRESS_HI(ctx->options->address32_hi)),
                                     Operand(0xFFFFFFFFu),
                                     Operand(desc_type));
      rsrc = bld.pseudo(aco_opcode::p_create_vector, bld.def(s4),
                        rsrc, upper_dwords);
   } else {
      rsrc = convert_pointer_to_64_bit(ctx, rsrc);
      rsrc = bld.smem(aco_opcode::s_load_dwordx4, bld.def(s4), rsrc, Operand(0u));
   }

   load_buffer(ctx, instr->num_components, dst, rsrc, get_ssa_temp(ctx, instr->src[1].ssa));
}

void visit_load_push_constant(isel_context *ctx, nir_intrinsic_instr *instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   unsigned offset = nir_intrinsic_base(instr);
   nir_const_value *index_cv = nir_src_as_const_value(instr->src[0]);
   if (index_cv && instr->dest.ssa.bit_size == 32) {

      unsigned count = instr->dest.ssa.num_components;
      unsigned start = (offset + index_cv->u32) / 4u;
      start -= ctx->base_inline_push_consts;
      if (start + count <= ctx->num_inline_push_consts) {
         std::array<Temp,NIR_MAX_VEC_COMPONENTS> elems;
         aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, count, 1)};
         for (unsigned i = 0; i < count; ++i) {
            elems[i] = ctx->inline_push_consts[start + i];
            vec->operands[i] = Operand{elems[i]};
         }
         vec->definitions[0] = Definition(dst);
         ctx->block->instructions.emplace_back(std::move(vec));
         ctx->allocated_vec.emplace(dst.id(), elems);
         return;
      }
   }

   Temp index = bld.as_uniform(get_ssa_temp(ctx, instr->src[0].ssa));
   if (offset != 0) // TODO check if index != 0 as well
      index = bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc), Operand(offset), index);
   Temp ptr = convert_pointer_to_64_bit(ctx, ctx->push_constants);
   Temp vec = dst;
   bool trim = false;
   aco_opcode op;

   switch (dst.size()) {
   case 1:
      op = aco_opcode::s_load_dword;
      break;
   case 2:
      op = aco_opcode::s_load_dwordx2;
      break;
   case 3:
      vec = bld.tmp(s4);
      trim = true;
   case 4:
      op = aco_opcode::s_load_dwordx4;
      break;
   case 6:
      vec = bld.tmp(s8);
      trim = true;
   case 8:
      op = aco_opcode::s_load_dwordx8;
      break;
   default:
      unreachable("unimplemented or forbidden load_push_constant.");
   }

   bld.smem(op, Definition(vec), ptr, index);

   if (trim) {
      emit_split_vector(ctx, vec, 4);
      RegClass rc = dst.size() == 3 ? s1 : s2;
      bld.pseudo(aco_opcode::p_create_vector, Definition(dst),
                 emit_extract_vector(ctx, vec, 0, rc),
                 emit_extract_vector(ctx, vec, 1, rc),
                 emit_extract_vector(ctx, vec, 2, rc));

   }
   emit_split_vector(ctx, dst, instr->dest.ssa.num_components);
}

void visit_load_constant(isel_context *ctx, nir_intrinsic_instr *instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   Builder bld(ctx->program, ctx->block);

   uint32_t desc_type = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
                        S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
                        S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
                        S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);
   if (ctx->options->chip_class >= GFX10) {
      desc_type |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_FLOAT) |
                   S_008F0C_OOB_SELECT(3) |
                   S_008F0C_RESOURCE_LEVEL(1);
   } else {
      desc_type |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                   S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
   }

   unsigned base = nir_intrinsic_base(instr);
   unsigned range = nir_intrinsic_range(instr);

   Temp offset = get_ssa_temp(ctx, instr->src[0].ssa);
   if (base && offset.type() == RegType::sgpr)
      offset = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.def(s1, scc), offset, Operand(base));
   else if (base && offset.type() == RegType::vgpr)
      offset = bld.vadd32(bld.def(v1), Operand(base), offset);

   Temp rsrc = bld.pseudo(aco_opcode::p_create_vector, bld.def(s4),
                          bld.sop1(aco_opcode::p_constaddr, bld.def(s2), bld.def(s1, scc), Operand(ctx->constant_data_offset)),
                          Operand(MIN2(base + range, ctx->shader->constant_data_size)),
                          Operand(desc_type));

   load_buffer(ctx, instr->num_components, dst, rsrc, offset);
}

void visit_discard_if(isel_context *ctx, nir_intrinsic_instr *instr)
{
   if (ctx->cf_info.loop_nest_depth || ctx->cf_info.parent_if.is_divergent)
      ctx->cf_info.exec_potentially_empty = true;

   ctx->program->needs_exact = true;

   // TODO: optimize uniform conditions
   Builder bld(ctx->program, ctx->block);
   Temp src = as_divergent_bool(ctx, get_ssa_temp(ctx, instr->src[0].ssa), false);
   src = bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.def(s1, scc), src, Operand(exec, s2));
   bld.pseudo(aco_opcode::p_discard_if, src);
   ctx->block->kind |= block_kind_uses_discard_if;
   return;
}

void visit_discard(isel_context* ctx, nir_intrinsic_instr *instr)
{
   Builder bld(ctx->program, ctx->block);

   if (ctx->cf_info.loop_nest_depth || ctx->cf_info.parent_if.is_divergent)
      ctx->cf_info.exec_potentially_empty = true;

   bool divergent = ctx->cf_info.parent_if.is_divergent ||
                    ctx->cf_info.parent_loop.has_divergent_continue;

   if (ctx->block->loop_nest_depth &&
       ((nir_instr_is_last(&instr->instr) && !divergent) || divergent)) {
      /* we handle discards the same way as jump instructions */
      append_logical_end(ctx->block);

      /* in loops, discard behaves like break */
      Block *linear_target = ctx->cf_info.parent_loop.exit;
      ctx->block->kind |= block_kind_discard;

      if (!divergent) {
         /* uniform discard - loop ends here */
         assert(nir_instr_is_last(&instr->instr));
         ctx->block->kind |= block_kind_uniform;
         ctx->cf_info.has_branch = true;
         bld.branch(aco_opcode::p_branch);
         add_linear_edge(ctx->block->index, linear_target);
         return;
      }

      /* we add a break right behind the discard() instructions */
      ctx->block->kind |= block_kind_break;
      unsigned idx = ctx->block->index;

      /* remove critical edges from linear CFG */
      bld.branch(aco_opcode::p_branch);
      Block* break_block = ctx->program->create_and_insert_block();
      break_block->loop_nest_depth = ctx->cf_info.loop_nest_depth;
      break_block->kind |= block_kind_uniform;
      add_linear_edge(idx, break_block);
      add_linear_edge(break_block->index, linear_target);
      bld.reset(break_block);
      bld.branch(aco_opcode::p_branch);

      Block* continue_block = ctx->program->create_and_insert_block();
      continue_block->loop_nest_depth = ctx->cf_info.loop_nest_depth;
      add_linear_edge(idx, continue_block);
      append_logical_start(continue_block);
      ctx->block = continue_block;

      return;
   }

   /* it can currently happen that NIR doesn't remove the unreachable code */
   if (!nir_instr_is_last(&instr->instr)) {
      ctx->program->needs_exact = true;
      /* save exec somewhere temporarily so that it doesn't get
       * overwritten before the discard from outer exec masks */
      Temp cond = bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.def(s1, scc), Operand(0xFFFFFFFF), Operand(exec, s2));
      bld.pseudo(aco_opcode::p_discard_if, cond);
      ctx->block->kind |= block_kind_uses_discard_if;
      return;
   }

   /* This condition is incorrect for uniformly branched discards in a loop
    * predicated by a divergent condition, but the above code catches that case
    * and the discard would end up turning into a discard_if.
    * For example:
    * if (divergent) {
    *    while (...) {
    *       if (uniform) {
    *          discard;
    *       }
    *    }
    * }
    */
   if (!ctx->cf_info.parent_if.is_divergent) {
      /* program just ends here */
      ctx->block->kind |= block_kind_uniform;
      bld.exp(aco_opcode::exp, Operand(v1), Operand(v1), Operand(v1), Operand(v1),
              0 /* enabled mask */, 9 /* dest */,
              false /* compressed */, true/* done */, true /* valid mask */);
      bld.sopp(aco_opcode::s_endpgm);
      // TODO: it will potentially be followed by a branch which is dead code to sanitize NIR phis
   } else {
      ctx->block->kind |= block_kind_discard;
      /* branch and linear edge is added by visit_if() */
   }
}

enum aco_descriptor_type {
   ACO_DESC_IMAGE,
   ACO_DESC_FMASK,
   ACO_DESC_SAMPLER,
   ACO_DESC_BUFFER,
   ACO_DESC_PLANE_0,
   ACO_DESC_PLANE_1,
   ACO_DESC_PLANE_2,
};

static bool
should_declare_array(isel_context *ctx, enum glsl_sampler_dim sampler_dim, bool is_array) {
   if (sampler_dim == GLSL_SAMPLER_DIM_BUF)
      return false;
   ac_image_dim dim = ac_get_sampler_dim(ctx->options->chip_class, sampler_dim, is_array);
   return dim == ac_image_cube ||
          dim == ac_image_1darray ||
          dim == ac_image_2darray ||
          dim == ac_image_2darraymsaa;
}

Temp get_sampler_desc(isel_context *ctx, nir_deref_instr *deref_instr,
                      enum aco_descriptor_type desc_type,
                      const nir_tex_instr *tex_instr, bool image, bool write)
{
/* FIXME: we should lower the deref with some new nir_intrinsic_load_desc
   std::unordered_map<uint64_t, Temp>::iterator it = ctx->tex_desc.find((uint64_t) desc_type << 32 | deref_instr->dest.ssa.index);
   if (it != ctx->tex_desc.end())
      return it->second;
*/
   Temp index = Temp();
   bool index_set = false;
   unsigned constant_index = 0;
   unsigned descriptor_set;
   unsigned base_index;
   Builder bld(ctx->program, ctx->block);

   if (!deref_instr) {
      assert(tex_instr && !image);
      descriptor_set = 0;
      base_index = tex_instr->sampler_index;
   } else {
      while(deref_instr->deref_type != nir_deref_type_var) {
         unsigned array_size = glsl_get_aoa_size(deref_instr->type);
         if (!array_size)
            array_size = 1;

         assert(deref_instr->deref_type == nir_deref_type_array);
         nir_const_value *const_value = nir_src_as_const_value(deref_instr->arr.index);
         if (const_value) {
            constant_index += array_size * const_value->u32;
         } else {
            Temp indirect = get_ssa_temp(ctx, deref_instr->arr.index.ssa);
            if (indirect.type() == RegType::vgpr)
               indirect = bld.vop1(aco_opcode::v_readfirstlane_b32, bld.def(s1), indirect);

            if (array_size != 1)
               indirect = bld.sop2(aco_opcode::s_mul_i32, bld.def(s1), Operand(array_size), indirect);

            if (!index_set) {
               index = indirect;
               index_set = true;
            } else {
               index = bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc), index, indirect);
            }
         }

         deref_instr = nir_src_as_deref(deref_instr->parent);
      }
      descriptor_set = deref_instr->var->data.descriptor_set;
      base_index = deref_instr->var->data.binding;
   }

   Temp list = load_desc_ptr(ctx, descriptor_set);
   list = convert_pointer_to_64_bit(ctx, list);

   struct radv_descriptor_set_layout *layout = ctx->options->layout->set[descriptor_set].layout;
   struct radv_descriptor_set_binding_layout *binding = layout->binding + base_index;
   unsigned offset = binding->offset;
   unsigned stride = binding->size;
   aco_opcode opcode;
   RegClass type;

   assert(base_index < layout->binding_count);

   switch (desc_type) {
   case ACO_DESC_IMAGE:
      type = s8;
      opcode = aco_opcode::s_load_dwordx8;
      break;
   case ACO_DESC_FMASK:
      type = s8;
      opcode = aco_opcode::s_load_dwordx8;
      offset += 32;
      break;
   case ACO_DESC_SAMPLER:
      type = s4;
      opcode = aco_opcode::s_load_dwordx4;
      if (binding->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
         offset += radv_combined_image_descriptor_sampler_offset(binding);
      break;
   case ACO_DESC_BUFFER:
      type = s4;
      opcode = aco_opcode::s_load_dwordx4;
      break;
   case ACO_DESC_PLANE_0:
   case ACO_DESC_PLANE_1:
      type = s8;
      opcode = aco_opcode::s_load_dwordx8;
      offset += 32 * (desc_type - ACO_DESC_PLANE_0);
      break;
   case ACO_DESC_PLANE_2:
      type = s4;
      opcode = aco_opcode::s_load_dwordx4;
      offset += 64;
      break;
   default:
      unreachable("invalid desc_type\n");
   }

   offset += constant_index * stride;

   if (desc_type == ACO_DESC_SAMPLER && binding->immutable_samplers_offset &&
      (!index_set || binding->immutable_samplers_equal)) {
      if (binding->immutable_samplers_equal)
         constant_index = 0;

      const uint32_t *samplers = radv_immutable_samplers(layout, binding);
      return bld.pseudo(aco_opcode::p_create_vector, bld.def(s4),
                        Operand(samplers[constant_index * 4 + 0]),
                        Operand(samplers[constant_index * 4 + 1]),
                        Operand(samplers[constant_index * 4 + 2]),
                        Operand(samplers[constant_index * 4 + 3]));
   }

   Operand off;
   if (!index_set) {
      off = Operand(offset);
   } else {
      off = Operand((Temp)bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc), Operand(offset),
                                   bld.sop2(aco_opcode::s_mul_i32, bld.def(s1), Operand(stride), index)));
   }

   Temp res = bld.smem(opcode, bld.def(type), list, off);

   if (desc_type == ACO_DESC_PLANE_2) {
      Temp components[8];
      for (unsigned i = 0; i < 8; i++)
         components[i] = bld.tmp(s1);
      bld.pseudo(aco_opcode::p_split_vector,
                 Definition(components[0]),
                 Definition(components[1]),
                 Definition(components[2]),
                 Definition(components[3]),
                 res);

      Temp desc2 = get_sampler_desc(ctx, deref_instr, ACO_DESC_PLANE_1, tex_instr, image, write);
      bld.pseudo(aco_opcode::p_split_vector,
                 bld.def(s1), bld.def(s1), bld.def(s1), bld.def(s1),
                 Definition(components[4]),
                 Definition(components[5]),
                 Definition(components[6]),
                 Definition(components[7]),
                 desc2);

      res = bld.pseudo(aco_opcode::p_create_vector, bld.def(s8),
                       components[0], components[1], components[2], components[3],
                       components[4], components[5], components[6], components[7]);
   }

   return res;
}

static int image_type_to_components_count(enum glsl_sampler_dim dim, bool array)
{
   switch (dim) {
   case GLSL_SAMPLER_DIM_BUF:
      return 1;
   case GLSL_SAMPLER_DIM_1D:
      return array ? 2 : 1;
   case GLSL_SAMPLER_DIM_2D:
      return array ? 3 : 2;
   case GLSL_SAMPLER_DIM_MS:
      return array ? 4 : 3;
   case GLSL_SAMPLER_DIM_3D:
   case GLSL_SAMPLER_DIM_CUBE:
      return 3;
   case GLSL_SAMPLER_DIM_RECT:
   case GLSL_SAMPLER_DIM_SUBPASS:
      return 2;
   case GLSL_SAMPLER_DIM_SUBPASS_MS:
      return 3;
   default:
      break;
   }
   return 0;
}


/* Adjust the sample index according to FMASK.
 *
 * For uncompressed MSAA surfaces, FMASK should return 0x76543210,
 * which is the identity mapping. Each nibble says which physical sample
 * should be fetched to get that sample.
 *
 * For example, 0x11111100 means there are only 2 samples stored and
 * the second sample covers 3/4 of the pixel. When reading samples 0
 * and 1, return physical sample 0 (determined by the first two 0s
 * in FMASK), otherwise return physical sample 1.
 *
 * The sample index should be adjusted as follows:
 *   sample_index = (fmask >> (sample_index * 4)) & 0xF;
 */
static Temp adjust_sample_index_using_fmask(isel_context *ctx, bool da, Temp coords, Operand sample_index, Temp fmask_desc_ptr)
{
   Builder bld(ctx->program, ctx->block);
   Temp fmask = bld.tmp(v1);
   unsigned dim = ctx->options->chip_class >= GFX10
                  ? ac_get_sampler_dim(ctx->options->chip_class, GLSL_SAMPLER_DIM_2D, da)
                  : 0;

   aco_ptr<MIMG_instruction> load{create_instruction<MIMG_instruction>(aco_opcode::image_load, Format::MIMG, 2, 1)};
   load->operands[0] = Operand(coords);
   load->operands[1] = Operand(fmask_desc_ptr);
   load->definitions[0] = Definition(fmask);
   load->glc = false;
   load->dlc = false;
   load->dmask = 0x1;
   load->unrm = true;
   load->da = da;
   load->dim = dim;
   load->can_reorder = true; /* fmask images shouldn't be modified */
   ctx->block->instructions.emplace_back(std::move(load));

   Operand sample_index4;
   if (sample_index.isConstant() && sample_index.constantValue() < 16) {
      sample_index4 = Operand(sample_index.constantValue() << 2);
   } else if (sample_index.regClass() == s1) {
      sample_index4 = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), sample_index, Operand(2u));
   } else {
      assert(sample_index.regClass() == v1);
      sample_index4 = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand(2u), sample_index);
   }

   Temp final_sample;
   if (sample_index4.isConstant() && sample_index4.constantValue() == 0)
      final_sample = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(15u), fmask);
   else if (sample_index4.isConstant() && sample_index4.constantValue() == 28)
      final_sample = bld.vop2(aco_opcode::v_lshrrev_b32, bld.def(v1), Operand(28u), fmask);
   else
      final_sample = bld.vop3(aco_opcode::v_bfe_u32, bld.def(v1), fmask, sample_index4, Operand(4u));

   /* Don't rewrite the sample index if WORD1.DATA_FORMAT of the FMASK
    * resource descriptor is 0 (invalid),
    */
   Temp compare = bld.tmp(s2);
   bld.vopc_e64(aco_opcode::v_cmp_lg_u32, Definition(compare),
                Operand(0u), emit_extract_vector(ctx, fmask_desc_ptr, 1, s1)).def(0).setHint(vcc);

   Temp sample_index_v = bld.vop1(aco_opcode::v_mov_b32, bld.def(v1), sample_index);

   /* Replace the MSAA sample index. */
   return bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), sample_index_v, final_sample, compare);
}

static Temp get_image_coords(isel_context *ctx, const nir_intrinsic_instr *instr, const struct glsl_type *type)
{

   Temp src0 = get_ssa_temp(ctx, instr->src[1].ssa);
   enum glsl_sampler_dim dim = glsl_get_sampler_dim(type);
   bool is_array = glsl_sampler_type_is_array(type);
   ASSERTED bool add_frag_pos = (dim == GLSL_SAMPLER_DIM_SUBPASS || dim == GLSL_SAMPLER_DIM_SUBPASS_MS);
   assert(!add_frag_pos && "Input attachments should be lowered.");
   bool is_ms = (dim == GLSL_SAMPLER_DIM_MS || dim == GLSL_SAMPLER_DIM_SUBPASS_MS);
   bool gfx9_1d = ctx->options->chip_class == GFX9 && dim == GLSL_SAMPLER_DIM_1D;
   int count = image_type_to_components_count(dim, is_array);
   std::vector<Operand> coords(count);

   if (is_ms) {
      Operand sample_index;
      nir_const_value *sample_cv = nir_src_as_const_value(instr->src[2]);
      if (sample_cv)
         sample_index = Operand(sample_cv->u32);
      else
         sample_index = Operand(emit_extract_vector(ctx, get_ssa_temp(ctx, instr->src[2].ssa), 0, v1));

      if (instr->intrinsic == nir_intrinsic_image_deref_load) {
         aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, is_array ? 3 : 2, 1)};
         for (unsigned i = 0; i < vec->operands.size(); i++)
            vec->operands[i] = Operand(emit_extract_vector(ctx, src0, i, v1));
         Temp fmask_load_address = {ctx->program->allocateId(), is_array ? v3 : v2};
         vec->definitions[0] = Definition(fmask_load_address);
         ctx->block->instructions.emplace_back(std::move(vec));

         Temp fmask_desc_ptr = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr), ACO_DESC_FMASK, nullptr, false, false);
         sample_index = Operand(adjust_sample_index_using_fmask(ctx, is_array, fmask_load_address, sample_index, fmask_desc_ptr));
      }
      count--;
      coords[count] = sample_index;
   }

   if (count == 1 && !gfx9_1d)
      return emit_extract_vector(ctx, src0, 0, v1);

   if (gfx9_1d) {
      coords[0] = Operand(emit_extract_vector(ctx, src0, 0, v1));
      coords.resize(coords.size() + 1);
      coords[1] = Operand((uint32_t) 0);
      if (is_array)
         coords[2] = Operand(emit_extract_vector(ctx, src0, 1, v1));
   } else {
      for (int i = 0; i < count; i++)
         coords[i] = Operand(emit_extract_vector(ctx, src0, i, v1));
   }

   aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, coords.size(), 1)};
   for (unsigned i = 0; i < coords.size(); i++)
      vec->operands[i] = coords[i];
   Temp res = {ctx->program->allocateId(), RegClass(RegType::vgpr, coords.size())};
   vec->definitions[0] = Definition(res);
   ctx->block->instructions.emplace_back(std::move(vec));
   return res;
}


void visit_image_load(isel_context *ctx, nir_intrinsic_instr *instr)
{
   Builder bld(ctx->program, ctx->block);
   const nir_variable *var = nir_deref_instr_get_variable(nir_instr_as_deref(instr->src[0].ssa->parent_instr));
   const struct glsl_type *type = glsl_without_array(var->type);
   const enum glsl_sampler_dim dim = glsl_get_sampler_dim(type);
   bool is_array = glsl_sampler_type_is_array(type);
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   if (dim == GLSL_SAMPLER_DIM_BUF) {
      unsigned mask = nir_ssa_def_components_read(&instr->dest.ssa);
      unsigned num_channels = util_last_bit(mask);
      Temp rsrc = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr), ACO_DESC_BUFFER, nullptr, true, true);
      Temp vindex = emit_extract_vector(ctx, get_ssa_temp(ctx, instr->src[1].ssa), 0, v1);

      aco_opcode opcode;
      switch (num_channels) {
      case 1:
         opcode = aco_opcode::buffer_load_format_x;
         break;
      case 2:
         opcode = aco_opcode::buffer_load_format_xy;
         break;
      case 3:
         opcode = aco_opcode::buffer_load_format_xyz;
         break;
      case 4:
         opcode = aco_opcode::buffer_load_format_xyzw;
         break;
      default:
         unreachable(">4 channel buffer image load");
      }
      aco_ptr<MUBUF_instruction> load{create_instruction<MUBUF_instruction>(opcode, Format::MUBUF, 3, 1)};
      load->operands[0] = Operand(vindex);
      load->operands[1] = Operand(rsrc);
      load->operands[2] = Operand((uint32_t) 0);
      Temp tmp;
      if (num_channels == instr->dest.ssa.num_components && dst.type() == RegType::vgpr)
         tmp = dst;
      else
         tmp = {ctx->program->allocateId(), RegClass(RegType::vgpr, num_channels)};
      load->definitions[0] = Definition(tmp);
      load->idxen = true;
      load->barrier = barrier_image;
      ctx->block->instructions.emplace_back(std::move(load));

      expand_vector(ctx, tmp, dst, instr->dest.ssa.num_components, (1 << num_channels) - 1);
      return;
   }

   Temp coords = get_image_coords(ctx, instr, type);
   Temp resource = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr), ACO_DESC_IMAGE, nullptr, true, true);

   unsigned dmask = nir_ssa_def_components_read(&instr->dest.ssa);
   unsigned num_components = util_bitcount(dmask);
   Temp tmp;
   if (num_components == instr->dest.ssa.num_components && dst.type() == RegType::vgpr)
      tmp = dst;
   else
      tmp = {ctx->program->allocateId(), RegClass(RegType::vgpr, num_components)};

   aco_ptr<MIMG_instruction> load{create_instruction<MIMG_instruction>(aco_opcode::image_load, Format::MIMG, 2, 1)};
   load->operands[0] = Operand(coords);
   load->operands[1] = Operand(resource);
   load->definitions[0] = Definition(tmp);
   load->glc = var->data.image.access & (ACCESS_VOLATILE | ACCESS_COHERENT) ? 1 : 0;
   load->dim = ac_get_image_dim(ctx->options->chip_class, dim, is_array);
   load->dmask = dmask;
   load->unrm = true;
   load->da = should_declare_array(ctx, dim, glsl_sampler_type_is_array(type));
   load->barrier = barrier_image;
   ctx->block->instructions.emplace_back(std::move(load));

   expand_vector(ctx, tmp, dst, instr->dest.ssa.num_components, dmask);
   return;
}

void visit_image_store(isel_context *ctx, nir_intrinsic_instr *instr)
{
   const nir_variable *var = nir_deref_instr_get_variable(nir_instr_as_deref(instr->src[0].ssa->parent_instr));
   const struct glsl_type *type = glsl_without_array(var->type);
   const enum glsl_sampler_dim dim = glsl_get_sampler_dim(type);
   bool is_array = glsl_sampler_type_is_array(type);
   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[3].ssa));

   bool glc = ctx->options->chip_class == GFX6 || var->data.image.access & (ACCESS_VOLATILE | ACCESS_COHERENT | ACCESS_NON_READABLE) ? 1 : 0;

   if (dim == GLSL_SAMPLER_DIM_BUF) {
      Temp rsrc = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr), ACO_DESC_BUFFER, nullptr, true, true);
      Temp vindex = emit_extract_vector(ctx, get_ssa_temp(ctx, instr->src[1].ssa), 0, v1);
      aco_opcode opcode;
      switch (data.size()) {
      case 1:
         opcode = aco_opcode::buffer_store_format_x;
         break;
      case 2:
         opcode = aco_opcode::buffer_store_format_xy;
         break;
      case 3:
         opcode = aco_opcode::buffer_store_format_xyz;
         break;
      case 4:
         opcode = aco_opcode::buffer_store_format_xyzw;
         break;
      default:
         unreachable(">4 channel buffer image store");
      }
      aco_ptr<MUBUF_instruction> store{create_instruction<MUBUF_instruction>(opcode, Format::MUBUF, 4, 0)};
      store->operands[0] = Operand(vindex);
      store->operands[1] = Operand(rsrc);
      store->operands[2] = Operand((uint32_t) 0);
      store->operands[3] = Operand(data);
      store->idxen = true;
      store->glc = glc;
      store->dlc = false;
      store->disable_wqm = true;
      store->barrier = barrier_image;
      ctx->program->needs_exact = true;
      ctx->block->instructions.emplace_back(std::move(store));
      return;
   }

   assert(data.type() == RegType::vgpr);
   Temp coords = get_image_coords(ctx, instr, type);
   Temp resource = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr), ACO_DESC_IMAGE, nullptr, true, true);

   aco_ptr<MIMG_instruction> store{create_instruction<MIMG_instruction>(aco_opcode::image_store, Format::MIMG, 4, 0)};
   store->operands[0] = Operand(coords);
   store->operands[1] = Operand(resource);
   store->operands[2] = Operand(s4);
   store->operands[3] = Operand(data);
   store->glc = glc;
   store->dlc = false;
   store->dim = ac_get_image_dim(ctx->options->chip_class, dim, is_array);
   store->dmask = (1 << data.size()) - 1;
   store->unrm = true;
   store->da = should_declare_array(ctx, dim, glsl_sampler_type_is_array(type));
   store->disable_wqm = true;
   store->barrier = barrier_image;
   ctx->program->needs_exact = true;
   ctx->block->instructions.emplace_back(std::move(store));
   return;
}

void visit_image_atomic(isel_context *ctx, nir_intrinsic_instr *instr)
{
   /* return the previous value if dest is ever used */
   bool return_previous = false;
   nir_foreach_use_safe(use_src, &instr->dest.ssa) {
      return_previous = true;
      break;
   }
   nir_foreach_if_use_safe(use_src, &instr->dest.ssa) {
      return_previous = true;
      break;
   }

   const nir_variable *var = nir_deref_instr_get_variable(nir_instr_as_deref(instr->src[0].ssa->parent_instr));
   const struct glsl_type *type = glsl_without_array(var->type);
   const enum glsl_sampler_dim dim = glsl_get_sampler_dim(type);
   bool is_array = glsl_sampler_type_is_array(type);
   Builder bld(ctx->program, ctx->block);

   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[3].ssa));
   assert(data.size() == 1 && "64bit ssbo atomics not yet implemented.");

   if (instr->intrinsic == nir_intrinsic_image_deref_atomic_comp_swap)
      data = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), get_ssa_temp(ctx, instr->src[4].ssa), data);

   aco_opcode buf_op, image_op;
   switch (instr->intrinsic) {
      case nir_intrinsic_image_deref_atomic_add:
         buf_op = aco_opcode::buffer_atomic_add;
         image_op = aco_opcode::image_atomic_add;
         break;
      case nir_intrinsic_image_deref_atomic_umin:
         buf_op = aco_opcode::buffer_atomic_umin;
         image_op = aco_opcode::image_atomic_umin;
         break;
      case nir_intrinsic_image_deref_atomic_imin:
         buf_op = aco_opcode::buffer_atomic_smin;
         image_op = aco_opcode::image_atomic_smin;
         break;
      case nir_intrinsic_image_deref_atomic_umax:
         buf_op = aco_opcode::buffer_atomic_umax;
         image_op = aco_opcode::image_atomic_umax;
         break;
      case nir_intrinsic_image_deref_atomic_imax:
         buf_op = aco_opcode::buffer_atomic_smax;
         image_op = aco_opcode::image_atomic_smax;
         break;
      case nir_intrinsic_image_deref_atomic_and:
         buf_op = aco_opcode::buffer_atomic_and;
         image_op = aco_opcode::image_atomic_and;
         break;
      case nir_intrinsic_image_deref_atomic_or:
         buf_op = aco_opcode::buffer_atomic_or;
         image_op = aco_opcode::image_atomic_or;
         break;
      case nir_intrinsic_image_deref_atomic_xor:
         buf_op = aco_opcode::buffer_atomic_xor;
         image_op = aco_opcode::image_atomic_xor;
         break;
      case nir_intrinsic_image_deref_atomic_exchange:
         buf_op = aco_opcode::buffer_atomic_swap;
         image_op = aco_opcode::image_atomic_swap;
         break;
      case nir_intrinsic_image_deref_atomic_comp_swap:
         buf_op = aco_opcode::buffer_atomic_cmpswap;
         image_op = aco_opcode::image_atomic_cmpswap;
         break;
      default:
         unreachable("visit_image_atomic should only be called with nir_intrinsic_image_deref_atomic_* instructions.");
   }

   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   if (dim == GLSL_SAMPLER_DIM_BUF) {
      Temp vindex = emit_extract_vector(ctx, get_ssa_temp(ctx, instr->src[1].ssa), 0, v1);
      Temp resource = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr), ACO_DESC_BUFFER, nullptr, true, true);
      //assert(ctx->options->chip_class < GFX9 && "GFX9 stride size workaround not yet implemented.");
      aco_ptr<MUBUF_instruction> mubuf{create_instruction<MUBUF_instruction>(buf_op, Format::MUBUF, 4, return_previous ? 1 : 0)};
      mubuf->operands[0] = Operand(vindex);
      mubuf->operands[1] = Operand(resource);
      mubuf->operands[2] = Operand((uint32_t)0);
      mubuf->operands[3] = Operand(data);
      if (return_previous)
         mubuf->definitions[0] = Definition(dst);
      mubuf->offset = 0;
      mubuf->idxen = true;
      mubuf->glc = return_previous;
      mubuf->dlc = false; /* Not needed for atomics */
      mubuf->disable_wqm = true;
      mubuf->barrier = barrier_image;
      ctx->program->needs_exact = true;
      ctx->block->instructions.emplace_back(std::move(mubuf));
      return;
   }

   Temp coords = get_image_coords(ctx, instr, type);
   Temp resource = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr), ACO_DESC_IMAGE, nullptr, true, true);
   aco_ptr<MIMG_instruction> mimg{create_instruction<MIMG_instruction>(image_op, Format::MIMG, 4, return_previous ? 1 : 0)};
   mimg->operands[0] = Operand(coords);
   mimg->operands[1] = Operand(resource);
   mimg->operands[2] = Operand(s4); /* no sampler */
   mimg->operands[3] = Operand(data);
   if (return_previous)
      mimg->definitions[0] = Definition(dst);
   mimg->glc = return_previous;
   mimg->dlc = false; /* Not needed for atomics */
   mimg->dim = ac_get_image_dim(ctx->options->chip_class, dim, is_array);
   mimg->dmask = (1 << data.size()) - 1;
   mimg->unrm = true;
   mimg->da = should_declare_array(ctx, dim, glsl_sampler_type_is_array(type));
   mimg->disable_wqm = true;
   mimg->barrier = barrier_image;
   ctx->program->needs_exact = true;
   ctx->block->instructions.emplace_back(std::move(mimg));
   return;
}

void get_buffer_size(isel_context *ctx, Temp desc, Temp dst, bool in_elements)
{
   if (in_elements && ctx->options->chip_class == GFX8) {
      Builder bld(ctx->program, ctx->block);

      Temp stride = emit_extract_vector(ctx, desc, 1, s1);
      stride = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc), stride, Operand((5u << 16) | 16u));
      stride = bld.vop1(aco_opcode::v_cvt_f32_ubyte0, bld.def(v1), stride);
      stride = bld.vop1(aco_opcode::v_rcp_iflag_f32, bld.def(v1), stride);

      Temp size = emit_extract_vector(ctx, desc, 2, s1);
      size = bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), size);

      Temp res = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), size, stride);
      res = bld.vop1(aco_opcode::v_cvt_u32_f32, bld.def(v1), res);
      bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), res);

      // TODO: we can probably calculate this faster on the scalar unit to do: size / stride{1,2,4,8,12,16}
      /* idea
       * for 1,2,4,8,16, the result is just (stride >> S_FF1_I32_B32)
       * in case 12 (or 3?), we have to divide by 3:
       * set v_skip in case it's 12 (if we also have to take care of 3, shift first)
       * use v_mul_hi_u32 with magic number to divide
       * we need some pseudo merge opcode to overwrite the original SALU result with readfirstlane
       * disable v_skip
       * total: 6 SALU + 2 VALU instructions vs 1 SALU + 6 VALU instructions
       */

   } else {
      emit_extract_vector(ctx, desc, 2, dst);
   }
}

void visit_image_size(isel_context *ctx, nir_intrinsic_instr *instr)
{
   const nir_variable *var = nir_deref_instr_get_variable(nir_instr_as_deref(instr->src[0].ssa->parent_instr));
   const struct glsl_type *type = glsl_without_array(var->type);
   const enum glsl_sampler_dim dim = glsl_get_sampler_dim(type);
   bool is_array = glsl_sampler_type_is_array(type);
   Builder bld(ctx->program, ctx->block);

   if (glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_BUF) {
      Temp desc = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr), ACO_DESC_BUFFER, NULL, true, false);
      return get_buffer_size(ctx, desc, get_ssa_temp(ctx, &instr->dest.ssa), true);
   }

   /* LOD */
   Temp lod = bld.vop1(aco_opcode::v_mov_b32, bld.def(v1), Operand(0u));

   /* Resource */
   Temp resource = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr), ACO_DESC_IMAGE, NULL, true, false);

   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   aco_ptr<MIMG_instruction> mimg{create_instruction<MIMG_instruction>(aco_opcode::image_get_resinfo, Format::MIMG, 2, 1)};
   mimg->operands[0] = Operand(lod);
   mimg->operands[1] = Operand(resource);
   unsigned& dmask = mimg->dmask;
   mimg->dim = ac_get_image_dim(ctx->options->chip_class, dim, is_array);
   mimg->dmask = (1 << instr->dest.ssa.num_components) - 1;
   mimg->da = glsl_sampler_type_is_array(type);
   mimg->can_reorder = true;
   Definition& def = mimg->definitions[0];
   ctx->block->instructions.emplace_back(std::move(mimg));

   if (glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_CUBE &&
       glsl_sampler_type_is_array(type)) {

      assert(instr->dest.ssa.num_components == 3);
      Temp tmp = {ctx->program->allocateId(), v3};
      def = Definition(tmp);
      emit_split_vector(ctx, tmp, 3);

      /* divide 3rd value by 6 by multiplying with magic number */
      Temp c = bld.copy(bld.def(s1), Operand((uint32_t) 0x2AAAAAAB));
      Temp by_6 = bld.vop3(aco_opcode::v_mul_hi_i32, bld.def(v1), emit_extract_vector(ctx, tmp, 2, v1), c);

      bld.pseudo(aco_opcode::p_create_vector, Definition(dst),
                 emit_extract_vector(ctx, tmp, 0, v1),
                 emit_extract_vector(ctx, tmp, 1, v1),
                 by_6);

   } else if (ctx->options->chip_class == GFX9 &&
              glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_1D &&
              glsl_sampler_type_is_array(type)) {
      assert(instr->dest.ssa.num_components == 2);
      def = Definition(dst);
      dmask = 0x5;
   } else {
      def = Definition(dst);
   }

   emit_split_vector(ctx, dst, instr->dest.ssa.num_components);
}

void visit_load_ssbo(isel_context *ctx, nir_intrinsic_instr *instr)
{
   Builder bld(ctx->program, ctx->block);
   unsigned num_components = instr->num_components;

   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   Temp rsrc = convert_pointer_to_64_bit(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   rsrc = bld.smem(aco_opcode::s_load_dwordx4, bld.def(s4), rsrc, Operand(0u));

   bool glc = nir_intrinsic_access(instr) & (ACCESS_VOLATILE | ACCESS_COHERENT);
   load_buffer(ctx, num_components, dst, rsrc, get_ssa_temp(ctx, instr->src[1].ssa), glc, false);
}

void visit_store_ssbo(isel_context *ctx, nir_intrinsic_instr *instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp data = get_ssa_temp(ctx, instr->src[0].ssa);
   unsigned elem_size_bytes = instr->src[0].ssa->bit_size / 8;
   unsigned writemask = nir_intrinsic_write_mask(instr);

   Temp offset;
   if (ctx->options->chip_class < GFX8)
      offset = as_vgpr(ctx,get_ssa_temp(ctx, instr->src[2].ssa));
   else
      offset = get_ssa_temp(ctx, instr->src[2].ssa);

   Temp rsrc = convert_pointer_to_64_bit(ctx, get_ssa_temp(ctx, instr->src[1].ssa));
   rsrc = bld.smem(aco_opcode::s_load_dwordx4, bld.def(s4), rsrc, Operand(0u));

   bool smem = !ctx->divergent_vals[instr->src[2].ssa->index] &&
               ctx->options->chip_class >= GFX8;
   if (smem)
      offset = bld.as_uniform(offset);
   bool smem_nonfs = smem && ctx->stage != fragment_fs;

   while (writemask) {
      int start, count;
      u_bit_scan_consecutive_range(&writemask, &start, &count);
      if (count == 3 && smem) {
         writemask |= 1u << (start + 2);
         count = 2;
      }
      int num_bytes = count * elem_size_bytes;

      if (num_bytes > 16) {
         assert(elem_size_bytes == 8);
         writemask |= (((count - 2) << 1) - 1) << (start + 2);
         count = 2;
         num_bytes = 16;
      }

      // TODO: check alignment of sub-dword stores
      // TODO: split 3 bytes. there is no store instruction for that

      Temp write_data;
      if (count != instr->num_components) {
         emit_split_vector(ctx, data, instr->num_components);
         aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, count, 1)};
         for (int i = 0; i < count; i++) {
            Temp elem = emit_extract_vector(ctx, data, start + i, RegClass(data.type(), elem_size_bytes / 4));
            vec->operands[i] = Operand(smem_nonfs ? bld.as_uniform(elem) : elem);
         }
         write_data = bld.tmp(smem_nonfs ? RegType::sgpr : data.type(), count * elem_size_bytes / 4);
         vec->definitions[0] = Definition(write_data);
         ctx->block->instructions.emplace_back(std::move(vec));
      } else if (!smem && data.type() != RegType::vgpr) {
         assert(num_bytes % 4 == 0);
         write_data = bld.copy(bld.def(RegType::vgpr, num_bytes / 4), data);
      } else if (smem_nonfs && data.type() == RegType::vgpr) {
         assert(num_bytes % 4 == 0);
         write_data = bld.as_uniform(data);
      } else {
         write_data = data;
      }

      aco_opcode vmem_op, smem_op;
      switch (num_bytes) {
         case 4:
            vmem_op = aco_opcode::buffer_store_dword;
            smem_op = aco_opcode::s_buffer_store_dword;
            break;
         case 8:
            vmem_op = aco_opcode::buffer_store_dwordx2;
            smem_op = aco_opcode::s_buffer_store_dwordx2;
            break;
         case 12:
            vmem_op = aco_opcode::buffer_store_dwordx3;
            smem_op = aco_opcode::last_opcode;
            assert(!smem);
            break;
         case 16:
            vmem_op = aco_opcode::buffer_store_dwordx4;
            smem_op = aco_opcode::s_buffer_store_dwordx4;
            break;
         default:
            unreachable("Store SSBO not implemented for this size.");
      }
      if (ctx->stage == fragment_fs)
         smem_op = aco_opcode::p_fs_buffer_store_smem;

      if (smem) {
         aco_ptr<SMEM_instruction> store{create_instruction<SMEM_instruction>(smem_op, Format::SMEM, 3, 0)};
         store->operands[0] = Operand(rsrc);
         if (start) {
            Temp off = bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc),
                                offset, Operand(start * elem_size_bytes));
            store->operands[1] = Operand(off);
         } else {
            store->operands[1] = Operand(offset);
         }
         if (smem_op != aco_opcode::p_fs_buffer_store_smem)
            store->operands[1].setFixed(m0);
         store->operands[2] = Operand(write_data);
         store->glc = nir_intrinsic_access(instr) & (ACCESS_VOLATILE | ACCESS_COHERENT | ACCESS_NON_READABLE);
         store->dlc = false;
         store->disable_wqm = true;
         store->barrier = barrier_buffer;
         ctx->block->instructions.emplace_back(std::move(store));
         ctx->program->wb_smem_l1_on_end = true;
         if (smem_op == aco_opcode::p_fs_buffer_store_smem) {
            ctx->block->kind |= block_kind_needs_lowering;
            ctx->program->needs_exact = true;
         }
      } else {
         aco_ptr<MUBUF_instruction> store{create_instruction<MUBUF_instruction>(vmem_op, Format::MUBUF, 4, 0)};
         store->operands[0] = offset.type() == RegType::vgpr ? Operand(offset) : Operand(v1);
         store->operands[1] = Operand(rsrc);
         store->operands[2] = offset.type() == RegType::sgpr ? Operand(offset) : Operand((uint32_t) 0);
         store->operands[3] = Operand(write_data);
         store->offset = start * elem_size_bytes;
         store->offen = (offset.type() == RegType::vgpr);
         store->glc = nir_intrinsic_access(instr) & (ACCESS_VOLATILE | ACCESS_COHERENT | ACCESS_NON_READABLE);
         store->dlc = false;
         store->disable_wqm = true;
         store->barrier = barrier_buffer;
         ctx->program->needs_exact = true;
         ctx->block->instructions.emplace_back(std::move(store));
      }
   }
}

void visit_atomic_ssbo(isel_context *ctx, nir_intrinsic_instr *instr)
{
   /* return the previous value if dest is ever used */
   bool return_previous = false;
   nir_foreach_use_safe(use_src, &instr->dest.ssa) {
      return_previous = true;
      break;
   }
   nir_foreach_if_use_safe(use_src, &instr->dest.ssa) {
      return_previous = true;
      break;
   }

   Builder bld(ctx->program, ctx->block);
   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[2].ssa));

   if (instr->intrinsic == nir_intrinsic_ssbo_atomic_comp_swap)
      data = bld.pseudo(aco_opcode::p_create_vector, bld.def(RegType::vgpr, data.size() * 2),
                        get_ssa_temp(ctx, instr->src[3].ssa), data);

   Temp offset;
   if (ctx->options->chip_class < GFX8)
      offset = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));
   else
      offset = get_ssa_temp(ctx, instr->src[1].ssa);

   Temp rsrc = convert_pointer_to_64_bit(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   rsrc = bld.smem(aco_opcode::s_load_dwordx4, bld.def(s4), rsrc, Operand(0u));

   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   aco_opcode op32, op64;
   switch (instr->intrinsic) {
      case nir_intrinsic_ssbo_atomic_add:
         op32 = aco_opcode::buffer_atomic_add;
         op64 = aco_opcode::buffer_atomic_add_x2;
         break;
      case nir_intrinsic_ssbo_atomic_imin:
         op32 = aco_opcode::buffer_atomic_smin;
         op64 = aco_opcode::buffer_atomic_smin_x2;
         break;
      case nir_intrinsic_ssbo_atomic_umin:
         op32 = aco_opcode::buffer_atomic_umin;
         op64 = aco_opcode::buffer_atomic_umin_x2;
         break;
      case nir_intrinsic_ssbo_atomic_imax:
         op32 = aco_opcode::buffer_atomic_smax;
         op64 = aco_opcode::buffer_atomic_smax_x2;
         break;
      case nir_intrinsic_ssbo_atomic_umax:
         op32 = aco_opcode::buffer_atomic_umax;
         op64 = aco_opcode::buffer_atomic_umax_x2;
         break;
      case nir_intrinsic_ssbo_atomic_and:
         op32 = aco_opcode::buffer_atomic_and;
         op64 = aco_opcode::buffer_atomic_and_x2;
         break;
      case nir_intrinsic_ssbo_atomic_or:
         op32 = aco_opcode::buffer_atomic_or;
         op64 = aco_opcode::buffer_atomic_or_x2;
         break;
      case nir_intrinsic_ssbo_atomic_xor:
         op32 = aco_opcode::buffer_atomic_xor;
         op64 = aco_opcode::buffer_atomic_xor_x2;
         break;
      case nir_intrinsic_ssbo_atomic_exchange:
         op32 = aco_opcode::buffer_atomic_swap;
         op64 = aco_opcode::buffer_atomic_swap_x2;
         break;
      case nir_intrinsic_ssbo_atomic_comp_swap:
         op32 = aco_opcode::buffer_atomic_cmpswap;
         op64 = aco_opcode::buffer_atomic_cmpswap_x2;
         break;
      default:
         unreachable("visit_atomic_ssbo should only be called with nir_intrinsic_ssbo_atomic_* instructions.");
   }
   aco_opcode op = instr->dest.ssa.bit_size == 32 ? op32 : op64;
   aco_ptr<MUBUF_instruction> mubuf{create_instruction<MUBUF_instruction>(op, Format::MUBUF, 4, return_previous ? 1 : 0)};
   mubuf->operands[0] = offset.type() == RegType::vgpr ? Operand(offset) : Operand(v1);
   mubuf->operands[1] = Operand(rsrc);
   mubuf->operands[2] = offset.type() == RegType::sgpr ? Operand(offset) : Operand((uint32_t) 0);
   mubuf->operands[3] = Operand(data);
   if (return_previous)
      mubuf->definitions[0] = Definition(dst);
   mubuf->offset = 0;
   mubuf->offen = (offset.type() == RegType::vgpr);
   mubuf->glc = return_previous;
   mubuf->dlc = false; /* Not needed for atomics */
   mubuf->disable_wqm = true;
   mubuf->barrier = barrier_buffer;
   ctx->program->needs_exact = true;
   ctx->block->instructions.emplace_back(std::move(mubuf));
}

void visit_get_buffer_size(isel_context *ctx, nir_intrinsic_instr *instr) {

   Temp index = convert_pointer_to_64_bit(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   Builder bld(ctx->program, ctx->block);
   Temp desc = bld.smem(aco_opcode::s_load_dwordx4, bld.def(s4), index, Operand(0u));
   get_buffer_size(ctx, desc, get_ssa_temp(ctx, &instr->dest.ssa), false);
}

void visit_load_global(isel_context *ctx, nir_intrinsic_instr *instr)
{
   Builder bld(ctx->program, ctx->block);
   unsigned num_components = instr->num_components;
   unsigned num_bytes = num_components * instr->dest.ssa.bit_size / 8;

   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   Temp addr = get_ssa_temp(ctx, instr->src[0].ssa);

   bool glc = nir_intrinsic_access(instr) & (ACCESS_VOLATILE | ACCESS_COHERENT);
   bool dlc = glc && ctx->options->chip_class >= GFX10;
   aco_opcode op;
   if (dst.type() == RegType::vgpr || (glc && ctx->options->chip_class < GFX8)) {
      bool global = ctx->options->chip_class >= GFX9;
      aco_opcode op;
      switch (num_bytes) {
      case 4:
         op = global ? aco_opcode::global_load_dword : aco_opcode::flat_load_dword;
         break;
      case 8:
         op = global ? aco_opcode::global_load_dwordx2 : aco_opcode::flat_load_dwordx2;
         break;
      case 12:
         op = global ? aco_opcode::global_load_dwordx3 : aco_opcode::flat_load_dwordx3;
         break;
      case 16:
         op = global ? aco_opcode::global_load_dwordx4 : aco_opcode::flat_load_dwordx4;
         break;
      default:
         unreachable("load_global not implemented for this size.");
      }
      aco_ptr<FLAT_instruction> flat{create_instruction<FLAT_instruction>(op, global ? Format::GLOBAL : Format::FLAT, 2, 1)};
      flat->operands[0] = Operand(addr);
      flat->operands[1] = Operand(s1);
      flat->glc = glc;
      flat->dlc = dlc;

      if (dst.type() == RegType::sgpr) {
         Temp vec = bld.tmp(RegType::vgpr, dst.size());
         flat->definitions[0] = Definition(vec);
         ctx->block->instructions.emplace_back(std::move(flat));
         bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), vec);
      } else {
         flat->definitions[0] = Definition(dst);
         ctx->block->instructions.emplace_back(std::move(flat));
      }
      emit_split_vector(ctx, dst, num_components);
   } else {
      switch (num_bytes) {
         case 4:
            op = aco_opcode::s_load_dword;
            break;
         case 8:
            op = aco_opcode::s_load_dwordx2;
            break;
         case 12:
         case 16:
            op = aco_opcode::s_load_dwordx4;
            break;
         default:
            unreachable("load_global not implemented for this size.");
      }
      aco_ptr<SMEM_instruction> load{create_instruction<SMEM_instruction>(op, Format::SMEM, 2, 1)};
      load->operands[0] = Operand(addr);
      load->operands[1] = Operand(0u);
      load->definitions[0] = Definition(dst);
      load->glc = glc;
      load->dlc = dlc;
      load->barrier = barrier_buffer;
      assert(ctx->options->chip_class >= GFX8 || !glc);

      if (dst.size() == 3) {
         /* trim vector */
         Temp vec = bld.tmp(s4);
         load->definitions[0] = Definition(vec);
         ctx->block->instructions.emplace_back(std::move(load));
         emit_split_vector(ctx, vec, 4);

         bld.pseudo(aco_opcode::p_create_vector, Definition(dst),
                    emit_extract_vector(ctx, vec, 0, s1),
                    emit_extract_vector(ctx, vec, 1, s1),
                    emit_extract_vector(ctx, vec, 2, s1));
      } else {
         ctx->block->instructions.emplace_back(std::move(load));
      }
   }
}

void visit_store_global(isel_context *ctx, nir_intrinsic_instr *instr)
{
   Builder bld(ctx->program, ctx->block);
   unsigned elem_size_bytes = instr->src[0].ssa->bit_size / 8;

   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   Temp addr = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));

   unsigned writemask = nir_intrinsic_write_mask(instr);
   while (writemask) {
      int start, count;
      u_bit_scan_consecutive_range(&writemask, &start, &count);
      unsigned num_bytes = count * elem_size_bytes;

      Temp write_data = data;
      if (count != instr->num_components) {
         aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, count, 1)};
         for (int i = 0; i < count; i++)
            vec->operands[i] = Operand(emit_extract_vector(ctx, data, start + i, v1));
         write_data = bld.tmp(RegType::vgpr, count);
         vec->definitions[0] = Definition(write_data);
         ctx->block->instructions.emplace_back(std::move(vec));
      }

      unsigned offset = start * elem_size_bytes;
      if (offset > 0 && ctx->options->chip_class < GFX9) {
         Temp addr0 = bld.tmp(v1), addr1 = bld.tmp(v1);
         Temp new_addr0 = bld.tmp(v1), new_addr1 = bld.tmp(v1);
         Temp carry = bld.tmp(s2);
         bld.pseudo(aco_opcode::p_split_vector, Definition(addr0), Definition(addr1), addr);

         bld.vop2(aco_opcode::v_add_co_u32, Definition(new_addr0), bld.hint_vcc(Definition(carry)),
                  Operand(offset), addr0);
         bld.vop2(aco_opcode::v_addc_co_u32, Definition(new_addr1), bld.def(s2),
                  Operand(0u), addr1,
                  carry).def(1).setHint(vcc);

         addr = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), new_addr0, new_addr1);

         offset = 0;
      }

      bool glc = nir_intrinsic_access(instr) & (ACCESS_VOLATILE | ACCESS_COHERENT | ACCESS_NON_READABLE);
      bool global = ctx->options->chip_class >= GFX9;
      aco_opcode op;
      switch (num_bytes) {
      case 4:
         op = global ? aco_opcode::global_store_dword : aco_opcode::flat_store_dword;
         break;
      case 8:
         op = global ? aco_opcode::global_store_dwordx2 : aco_opcode::flat_store_dwordx2;
         break;
      case 12:
         op = global ? aco_opcode::global_store_dwordx3 : aco_opcode::flat_store_dwordx3;
         break;
      case 16:
         op = global ? aco_opcode::global_store_dwordx4 : aco_opcode::flat_store_dwordx4;
         break;
      default:
         unreachable("store_global not implemented for this size.");
      }
      aco_ptr<FLAT_instruction> flat{create_instruction<FLAT_instruction>(op, global ? Format::GLOBAL : Format::FLAT, 3, 0)};
      flat->operands[0] = Operand(addr);
      flat->operands[1] = Operand(s1);
      flat->operands[2] = Operand(data);
      flat->glc = glc;
      flat->dlc = false;
      flat->offset = offset;
      ctx->block->instructions.emplace_back(std::move(flat));
   }
}

void emit_memory_barrier(isel_context *ctx, nir_intrinsic_instr *instr) {
   Builder bld(ctx->program, ctx->block);
   switch(instr->intrinsic) {
      case nir_intrinsic_group_memory_barrier:
      case nir_intrinsic_memory_barrier:
         bld.barrier(aco_opcode::p_memory_barrier_all);
         break;
      case nir_intrinsic_memory_barrier_atomic_counter:
         bld.barrier(aco_opcode::p_memory_barrier_atomic);
         break;
      case nir_intrinsic_memory_barrier_buffer:
         bld.barrier(aco_opcode::p_memory_barrier_buffer);
         break;
      case nir_intrinsic_memory_barrier_image:
         bld.barrier(aco_opcode::p_memory_barrier_image);
         break;
      case nir_intrinsic_memory_barrier_shared:
         bld.barrier(aco_opcode::p_memory_barrier_shared);
         break;
      default:
         unreachable("Unimplemented memory barrier intrinsic");
         break;
   }
}

void visit_load_shared(isel_context *ctx, nir_intrinsic_instr *instr)
{
   // TODO: implement sparse reads using ds_read2_b32 and nir_ssa_def_components_read()
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   assert(instr->dest.ssa.bit_size >= 32 && "Bitsize not supported in load_shared.");
   Temp address = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   Builder bld(ctx->program, ctx->block);

   unsigned elem_size_bytes = instr->dest.ssa.bit_size / 8;
   unsigned align = nir_intrinsic_align_mul(instr) ? nir_intrinsic_align(instr) : elem_size_bytes;
   load_lds(ctx, elem_size_bytes, dst, address, nir_intrinsic_base(instr), align);
}

void visit_store_shared(isel_context *ctx, nir_intrinsic_instr *instr)
{
   unsigned writemask = nir_intrinsic_write_mask(instr);
   Temp data = get_ssa_temp(ctx, instr->src[0].ssa);
   Temp address = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));
   unsigned elem_size_bytes = instr->src[0].ssa->bit_size / 8;
   assert(elem_size_bytes >= 4 && "Only 32bit & 64bit store_shared currently supported.");

   unsigned align = nir_intrinsic_align_mul(instr) ? nir_intrinsic_align(instr) : elem_size_bytes;
   store_lds(ctx, elem_size_bytes, data, writemask, address, nir_intrinsic_base(instr), align);
}

void visit_shared_atomic(isel_context *ctx, nir_intrinsic_instr *instr)
{
   unsigned offset = nir_intrinsic_base(instr);
   Operand m = load_lds_size_m0(ctx);
   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));
   Temp address = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));

   unsigned num_operands = 3;
   aco_opcode op32, op64, op32_rtn, op64_rtn;
   switch(instr->intrinsic) {
      case nir_intrinsic_shared_atomic_add:
         op32 = aco_opcode::ds_add_u32;
         op64 = aco_opcode::ds_add_u64;
         op32_rtn = aco_opcode::ds_add_rtn_u32;
         op64_rtn = aco_opcode::ds_add_rtn_u64;
         break;
      case nir_intrinsic_shared_atomic_imin:
         op32 = aco_opcode::ds_min_i32;
         op64 = aco_opcode::ds_min_i64;
         op32_rtn = aco_opcode::ds_min_rtn_i32;
         op64_rtn = aco_opcode::ds_min_rtn_i64;
         break;
      case nir_intrinsic_shared_atomic_umin:
         op32 = aco_opcode::ds_min_u32;
         op64 = aco_opcode::ds_min_u64;
         op32_rtn = aco_opcode::ds_min_rtn_u32;
         op64_rtn = aco_opcode::ds_min_rtn_u64;
         break;
      case nir_intrinsic_shared_atomic_imax:
         op32 = aco_opcode::ds_max_i32;
         op64 = aco_opcode::ds_max_i64;
         op32_rtn = aco_opcode::ds_max_rtn_i32;
         op64_rtn = aco_opcode::ds_max_rtn_i64;
         break;
      case nir_intrinsic_shared_atomic_umax:
         op32 = aco_opcode::ds_max_u32;
         op64 = aco_opcode::ds_max_u64;
         op32_rtn = aco_opcode::ds_max_rtn_u32;
         op64_rtn = aco_opcode::ds_max_rtn_u64;
         break;
      case nir_intrinsic_shared_atomic_and:
         op32 = aco_opcode::ds_and_b32;
         op64 = aco_opcode::ds_and_b64;
         op32_rtn = aco_opcode::ds_and_rtn_b32;
         op64_rtn = aco_opcode::ds_and_rtn_b64;
         break;
      case nir_intrinsic_shared_atomic_or:
         op32 = aco_opcode::ds_or_b32;
         op64 = aco_opcode::ds_or_b64;
         op32_rtn = aco_opcode::ds_or_rtn_b32;
         op64_rtn = aco_opcode::ds_or_rtn_b64;
         break;
      case nir_intrinsic_shared_atomic_xor:
         op32 = aco_opcode::ds_xor_b32;
         op64 = aco_opcode::ds_xor_b64;
         op32_rtn = aco_opcode::ds_xor_rtn_b32;
         op64_rtn = aco_opcode::ds_xor_rtn_b64;
         break;
      case nir_intrinsic_shared_atomic_exchange:
         op32 = aco_opcode::ds_write_b32;
         op64 = aco_opcode::ds_write_b64;
         op32_rtn = aco_opcode::ds_wrxchg_rtn_b32;
         op64_rtn = aco_opcode::ds_wrxchg2_rtn_b64;
         break;
      case nir_intrinsic_shared_atomic_comp_swap:
         op32 = aco_opcode::ds_cmpst_b32;
         op64 = aco_opcode::ds_cmpst_b64;
         op32_rtn = aco_opcode::ds_cmpst_rtn_b32;
         op64_rtn = aco_opcode::ds_cmpst_rtn_b64;
         num_operands = 4;
         break;
      default:
         unreachable("Unhandled shared atomic intrinsic");
   }

   /* return the previous value if dest is ever used */
   bool return_previous = false;
   nir_foreach_use_safe(use_src, &instr->dest.ssa) {
      return_previous = true;
      break;
   }
   nir_foreach_if_use_safe(use_src, &instr->dest.ssa) {
      return_previous = true;
      break;
   }

   aco_opcode op;
   if (data.size() == 1) {
      assert(instr->dest.ssa.bit_size == 32);
      op = return_previous ? op32_rtn : op32;
   } else {
      assert(instr->dest.ssa.bit_size == 64);
      op = return_previous ? op64_rtn : op64;
   }

   if (offset > 65535) {
      Builder bld(ctx->program, ctx->block);
      address = bld.vadd32(bld.def(v1), Operand(offset), address);
      offset = 0;
   }

   aco_ptr<DS_instruction> ds;
   ds.reset(create_instruction<DS_instruction>(op, Format::DS, num_operands, return_previous ? 1 : 0));
   ds->operands[0] = Operand(address);
   ds->operands[1] = Operand(data);
   if (num_operands == 4)
      ds->operands[2] = Operand(get_ssa_temp(ctx, instr->src[2].ssa));
   ds->operands[num_operands - 1] = m;
   ds->offset0 = offset;
   if (return_previous)
      ds->definitions[0] = Definition(get_ssa_temp(ctx, &instr->dest.ssa));
   ctx->block->instructions.emplace_back(std::move(ds));
}

Temp get_scratch_resource(isel_context *ctx)
{
   Builder bld(ctx->program, ctx->block);
   Temp scratch_addr = ctx->program->private_segment_buffer;
   if (ctx->stage != compute_cs)
      scratch_addr = bld.smem(aco_opcode::s_load_dwordx2, bld.def(s2), scratch_addr, Operand(0u));

   uint32_t rsrc_conf = S_008F0C_ADD_TID_ENABLE(1) |
                        S_008F0C_INDEX_STRIDE(ctx->program->wave_size == 64 ? 3 : 2);;

   if (ctx->program->chip_class >= GFX10) {
      rsrc_conf |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_FLOAT) |
                   S_008F0C_OOB_SELECT(3) |
                   S_008F0C_RESOURCE_LEVEL(1);
   } else if (ctx->program->chip_class <= GFX7) { /* dfmt modifies stride on GFX8/GFX9 when ADD_TID_EN=1 */
      rsrc_conf |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                   S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
   }

   /* older generations need element size = 16 bytes. element size removed in GFX9 */
   if (ctx->program->chip_class <= GFX8)
      rsrc_conf |= S_008F0C_ELEMENT_SIZE(3);

   return bld.pseudo(aco_opcode::p_create_vector, bld.def(s4), scratch_addr, Operand(-1u), Operand(rsrc_conf));
}

void visit_load_scratch(isel_context *ctx, nir_intrinsic_instr *instr) {
   assert(instr->dest.ssa.bit_size == 32 || instr->dest.ssa.bit_size == 64);
   Builder bld(ctx->program, ctx->block);
   Temp rsrc = get_scratch_resource(ctx);
   Temp offset = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   aco_opcode op;
   switch (dst.size()) {
      case 1:
         op = aco_opcode::buffer_load_dword;
         break;
      case 2:
         op = aco_opcode::buffer_load_dwordx2;
         break;
      case 3:
         op = aco_opcode::buffer_load_dwordx3;
         break;
      case 4:
         op = aco_opcode::buffer_load_dwordx4;
         break;
      case 6:
      case 8: {
         std::array<Temp,NIR_MAX_VEC_COMPONENTS> elems;
         Temp lower = bld.mubuf(aco_opcode::buffer_load_dwordx4,
                                bld.def(v4), offset, rsrc,
                                ctx->program->scratch_offset, 0, true);
         Temp upper = bld.mubuf(dst.size() == 6 ? aco_opcode::buffer_load_dwordx2 :
                                                  aco_opcode::buffer_load_dwordx4,
                                dst.size() == 6 ? bld.def(v2) : bld.def(v4),
                                offset, rsrc, ctx->program->scratch_offset, 16, true);
         emit_split_vector(ctx, lower, 2);
         elems[0] = emit_extract_vector(ctx, lower, 0, v2);
         elems[1] = emit_extract_vector(ctx, lower, 1, v2);
         if (dst.size() == 8) {
            emit_split_vector(ctx, upper, 2);
            elems[2] = emit_extract_vector(ctx, upper, 0, v2);
            elems[3] = emit_extract_vector(ctx, upper, 1, v2);
         } else {
            elems[2] = upper;
         }

         aco_ptr<Instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector,
                                                                         Format::PSEUDO, dst.size() / 2, 1)};
         for (unsigned i = 0; i < dst.size() / 2; i++)
            vec->operands[i] = Operand(elems[i]);
         vec->definitions[0] = Definition(dst);
         bld.insert(std::move(vec));
         ctx->allocated_vec.emplace(dst.id(), elems);
         return;
      }
      default:
         unreachable("Wrong dst size for nir_intrinsic_load_scratch");
   }

   bld.mubuf(op, Definition(dst), offset, rsrc, ctx->program->scratch_offset, 0, true);
   emit_split_vector(ctx, dst, instr->num_components);
}

void visit_store_scratch(isel_context *ctx, nir_intrinsic_instr *instr) {
   assert(instr->src[0].ssa->bit_size == 32 || instr->src[0].ssa->bit_size == 64);
   Builder bld(ctx->program, ctx->block);
   Temp rsrc = get_scratch_resource(ctx);
   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   Temp offset = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));

   unsigned elem_size_bytes = instr->src[0].ssa->bit_size / 8;
   unsigned writemask = nir_intrinsic_write_mask(instr);

   while (writemask) {
      int start, count;
      u_bit_scan_consecutive_range(&writemask, &start, &count);
      int num_bytes = count * elem_size_bytes;

      if (num_bytes > 16) {
         assert(elem_size_bytes == 8);
         writemask |= (((count - 2) << 1) - 1) << (start + 2);
         count = 2;
         num_bytes = 16;
      }

      // TODO: check alignment of sub-dword stores
      // TODO: split 3 bytes. there is no store instruction for that

      Temp write_data;
      if (count != instr->num_components) {
         aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, count, 1)};
         for (int i = 0; i < count; i++) {
            Temp elem = emit_extract_vector(ctx, data, start + i, RegClass(RegType::vgpr, elem_size_bytes / 4));
            vec->operands[i] = Operand(elem);
         }
         write_data = bld.tmp(RegClass(RegType::vgpr, count * elem_size_bytes / 4));
         vec->definitions[0] = Definition(write_data);
         ctx->block->instructions.emplace_back(std::move(vec));
      } else {
         write_data = data;
      }

      aco_opcode op;
      switch (num_bytes) {
         case 4:
            op = aco_opcode::buffer_store_dword;
            break;
         case 8:
            op = aco_opcode::buffer_store_dwordx2;
            break;
         case 12:
            op = aco_opcode::buffer_store_dwordx3;
            break;
         case 16:
            op = aco_opcode::buffer_store_dwordx4;
            break;
         default:
            unreachable("Invalid data size for nir_intrinsic_store_scratch.");
      }

      bld.mubuf(op, offset, rsrc, ctx->program->scratch_offset, write_data, start * elem_size_bytes, true);
   }
}

void visit_load_sample_mask_in(isel_context *ctx, nir_intrinsic_instr *instr) {
   uint8_t log2_ps_iter_samples;
   if (ctx->program->info->ps.force_persample) {
      log2_ps_iter_samples =
         util_logbase2(ctx->options->key.fs.num_samples);
   } else {
      log2_ps_iter_samples = ctx->options->key.fs.log2_ps_iter_samples;
   }

   /* The bit pattern matches that used by fixed function fragment
    * processing. */
   static const unsigned ps_iter_masks[] = {
      0xffff, /* not used */
      0x5555,
      0x1111,
      0x0101,
      0x0001,
   };
   assert(log2_ps_iter_samples < ARRAY_SIZE(ps_iter_masks));

   Builder bld(ctx->program, ctx->block);

   Temp sample_id = bld.vop3(aco_opcode::v_bfe_u32, bld.def(v1), ctx->fs_inputs[fs_input::ancillary], Operand(8u), Operand(4u));
   Temp ps_iter_mask = bld.vop1(aco_opcode::v_mov_b32, bld.def(v1), Operand(ps_iter_masks[log2_ps_iter_samples]));
   Temp mask = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), sample_id, ps_iter_mask);
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   bld.vop2(aco_opcode::v_and_b32, Definition(dst), mask, ctx->fs_inputs[fs_input::sample_coverage]);
}

Temp emit_boolean_reduce(isel_context *ctx, nir_op op, unsigned cluster_size, Temp src)
{
   Builder bld(ctx->program, ctx->block);

   if (cluster_size == 1) {
      return src;
   } if (op == nir_op_iand && cluster_size == 4) {
      //subgroupClusteredAnd(val, 4) -> ~wqm(exec & ~val)
      Temp tmp = bld.sop2(aco_opcode::s_andn2_b64, bld.def(s2), bld.def(s1, scc), Operand(exec, s2), src);
      return bld.sop1(aco_opcode::s_not_b64, bld.def(s2), bld.def(s1, scc),
                      bld.sop1(aco_opcode::s_wqm_b64, bld.def(s2), bld.def(s1, scc), tmp));
   } else if (op == nir_op_ior && cluster_size == 4) {
      //subgroupClusteredOr(val, 4) -> wqm(val & exec)
      return bld.sop1(aco_opcode::s_wqm_b64, bld.def(s2), bld.def(s1, scc),
                      bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.def(s1, scc), src, Operand(exec, s2)));
   } else if (op == nir_op_iand && cluster_size == 64) {
      //subgroupAnd(val) -> (exec & ~val) == 0
      Temp tmp = bld.sop2(aco_opcode::s_andn2_b64, bld.def(s2), bld.def(s1, scc), Operand(exec, s2), src).def(1).getTemp();
      return bld.sopc(aco_opcode::s_cmp_eq_u32, bld.def(s1, scc), tmp, Operand(0u));
   } else if (op == nir_op_ior && cluster_size == 64) {
      //subgroupOr(val) -> (val & exec) != 0
      return bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.def(s1, scc), src, Operand(exec, s2)).def(1).getTemp();
   } else if (op == nir_op_ixor && cluster_size == 64) {
      //subgroupXor(val) -> s_bcnt1_i32_b64(val & exec) & 1
      Temp tmp = bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.def(s1, scc), src, Operand(exec, s2));
      tmp = bld.sop1(aco_opcode::s_bcnt1_i32_b64, bld.def(s2), bld.def(s1, scc), tmp);
      return bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), tmp, Operand(1u)).def(1).getTemp();
   } else {
      //subgroupClustered{And,Or,Xor}(val, n) ->
      //lane_id = v_mbcnt_hi_u32_b32(-1, v_mbcnt_lo_u32_b32(-1, 0))
      //cluster_offset = ~(n - 1) & lane_id
      //cluster_mask = ((1 << n) - 1)
      //subgroupClusteredAnd():
      //   return ((val | ~exec) >> cluster_offset) & cluster_mask == cluster_mask
      //subgroupClusteredOr():
      //   return ((val & exec) >> cluster_offset) & cluster_mask != 0
      //subgroupClusteredXor():
      //   return v_bnt_u32_b32(((val & exec) >> cluster_offset) & cluster_mask, 0) & 1 != 0
      Temp lane_id = bld.vop3(aco_opcode::v_mbcnt_hi_u32_b32, bld.def(v1), Operand((uint32_t) -1),
                              bld.vop3(aco_opcode::v_mbcnt_lo_u32_b32, bld.def(v1), Operand((uint32_t) -1), Operand(0u)));
      Temp cluster_offset = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(~uint32_t(cluster_size - 1)), lane_id);

      Temp tmp;
      if (op == nir_op_iand)
         tmp = bld.sop2(aco_opcode::s_orn2_b64, bld.def(s2), bld.def(s1, scc), src, Operand(exec, s2));
      else
         tmp = bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.def(s1, scc), src, Operand(exec, s2));

      uint32_t cluster_mask = cluster_size == 32 ? -1 : (1u << cluster_size) - 1u;
      tmp = bld.vop3(aco_opcode::v_lshrrev_b64, bld.def(v2), cluster_offset, tmp);
      tmp = emit_extract_vector(ctx, tmp, 0, v1);
      if (cluster_mask != 0xffffffff)
         tmp = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(cluster_mask), tmp);

      Definition cmp_def = Definition();
      if (op == nir_op_iand) {
         cmp_def = bld.vopc(aco_opcode::v_cmp_eq_u32, bld.def(s2), Operand(cluster_mask), tmp).def(0);
      } else if (op == nir_op_ior) {
         cmp_def = bld.vopc(aco_opcode::v_cmp_lg_u32, bld.def(s2), Operand(0u), tmp).def(0);
      } else if (op == nir_op_ixor) {
         tmp = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(1u),
                        bld.vop3(aco_opcode::v_bcnt_u32_b32, bld.def(v1), tmp, Operand(0u)));
         cmp_def = bld.vopc(aco_opcode::v_cmp_lg_u32, bld.def(s2), Operand(0u), tmp).def(0);
      }
      cmp_def.setHint(vcc);
      return cmp_def.getTemp();
   }
}

Temp emit_boolean_exclusive_scan(isel_context *ctx, nir_op op, Temp src)
{
   Builder bld(ctx->program, ctx->block);

   //subgroupExclusiveAnd(val) -> mbcnt(exec & ~val) == 0
   //subgroupExclusiveOr(val) -> mbcnt(val & exec) != 0
   //subgroupExclusiveXor(val) -> mbcnt(val & exec) & 1 != 0
   Temp tmp;
   if (op == nir_op_iand)
      tmp = bld.sop2(aco_opcode::s_andn2_b64, bld.def(s2), bld.def(s1, scc), Operand(exec, s2), src);
   else
      tmp = bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.def(s1, scc), src, Operand(exec, s2));

   Builder::Result lohi = bld.pseudo(aco_opcode::p_split_vector, bld.def(s1), bld.def(s1), tmp);
   Temp lo = lohi.def(0).getTemp();
   Temp hi = lohi.def(1).getTemp();
   Temp mbcnt = bld.vop3(aco_opcode::v_mbcnt_hi_u32_b32, bld.def(v1), hi,
                         bld.vop3(aco_opcode::v_mbcnt_lo_u32_b32, bld.def(v1), lo, Operand(0u)));

   Definition cmp_def = Definition();
   if (op == nir_op_iand)
      cmp_def = bld.vopc(aco_opcode::v_cmp_eq_u32, bld.def(s2), Operand(0u), mbcnt).def(0);
   else if (op == nir_op_ior)
      cmp_def = bld.vopc(aco_opcode::v_cmp_lg_u32, bld.def(s2), Operand(0u), mbcnt).def(0);
   else if (op == nir_op_ixor)
      cmp_def = bld.vopc(aco_opcode::v_cmp_lg_u32, bld.def(s2), Operand(0u),
                         bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(1u), mbcnt)).def(0);
   cmp_def.setHint(vcc);
   return cmp_def.getTemp();
}

Temp emit_boolean_inclusive_scan(isel_context *ctx, nir_op op, Temp src)
{
   Builder bld(ctx->program, ctx->block);

   //subgroupInclusiveAnd(val) -> subgroupExclusiveAnd(val) && val
   //subgroupInclusiveOr(val) -> subgroupExclusiveOr(val) || val
   //subgroupInclusiveXor(val) -> subgroupExclusiveXor(val) ^^ val
   Temp tmp = emit_boolean_exclusive_scan(ctx, op, src);
   if (op == nir_op_iand)
      return bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.def(s1, scc), tmp, src);
   else if (op == nir_op_ior)
      return bld.sop2(aco_opcode::s_or_b64, bld.def(s2), bld.def(s1, scc), tmp, src);
   else if (op == nir_op_ixor)
      return bld.sop2(aco_opcode::s_xor_b64, bld.def(s2), bld.def(s1, scc), tmp, src);

   assert(false);
   return Temp();
}

void emit_uniform_subgroup(isel_context *ctx, nir_intrinsic_instr *instr, Temp src)
{
   Builder bld(ctx->program, ctx->block);
   Definition dst(get_ssa_temp(ctx, &instr->dest.ssa));
   if (src.regClass().type() == RegType::vgpr) {
      bld.pseudo(aco_opcode::p_as_uniform, dst, src);
   } else if (instr->dest.ssa.bit_size == 1 && src.regClass() == s2) {
      bld.sopc(aco_opcode::s_cmp_lg_u64, bld.scc(dst), Operand(0u), Operand(src));
   } else if (src.regClass() == s1) {
      bld.sop1(aco_opcode::s_mov_b32, dst, src);
   } else if (src.regClass() == s2) {
      bld.sop1(aco_opcode::s_mov_b64, dst, src);
   } else {
      fprintf(stderr, "Unimplemented NIR instr bit size: ");
      nir_print_instr(&instr->instr, stderr);
      fprintf(stderr, "\n");
   }
}

void emit_interp_center(isel_context *ctx, Temp dst, Temp pos1, Temp pos2)
{
   Builder bld(ctx->program, ctx->block);
   Temp p1 = ctx->fs_inputs[fs_input::persp_center_p1];
   Temp p2 = ctx->fs_inputs[fs_input::persp_center_p2];

   /* Build DD X/Y */
   Temp tl_1 = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), p1, dpp_quad_perm(0, 0, 0, 0));
   Temp ddx_1 = bld.vop2_dpp(aco_opcode::v_sub_f32, bld.def(v1), p1, tl_1, dpp_quad_perm(1, 1, 1, 1));
   Temp ddy_1 = bld.vop2_dpp(aco_opcode::v_sub_f32, bld.def(v1), p1, tl_1, dpp_quad_perm(2, 2, 2, 2));
   Temp tl_2 = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), p2, dpp_quad_perm(0, 0, 0, 0));
   Temp ddx_2 = bld.vop2_dpp(aco_opcode::v_sub_f32, bld.def(v1), p2, tl_2, dpp_quad_perm(1, 1, 1, 1));
   Temp ddy_2 = bld.vop2_dpp(aco_opcode::v_sub_f32, bld.def(v1), p2, tl_2, dpp_quad_perm(2, 2, 2, 2));

   /* res_k = p_k + ddx_k * pos1 + ddy_k * pos2 */
   Temp tmp1 = bld.vop3(aco_opcode::v_mad_f32, bld.def(v1), ddx_1, pos1, p1);
   Temp tmp2 = bld.vop3(aco_opcode::v_mad_f32, bld.def(v1), ddx_2, pos1, p2);
   tmp1 = bld.vop3(aco_opcode::v_mad_f32, bld.def(v1), ddy_1, pos2, tmp1);
   tmp2 = bld.vop3(aco_opcode::v_mad_f32, bld.def(v1), ddy_2, pos2, tmp2);
   Temp wqm1 = bld.tmp(v1);
   emit_wqm(ctx, tmp1, wqm1, true);
   Temp wqm2 = bld.tmp(v1);
   emit_wqm(ctx, tmp2, wqm2, true);
   bld.pseudo(aco_opcode::p_create_vector, Definition(dst), wqm1, wqm2);
   return;
}

void visit_intrinsic(isel_context *ctx, nir_intrinsic_instr *instr)
{
   Builder bld(ctx->program, ctx->block);
   switch(instr->intrinsic) {
   case nir_intrinsic_load_barycentric_sample:
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_centroid: {
      glsl_interp_mode mode = (glsl_interp_mode)nir_intrinsic_interp_mode(instr);
      fs_input input = get_interp_input(instr->intrinsic, mode);

      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      if (input == fs_input::max_inputs) {
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst),
                    Operand(0u), Operand(0u));
      } else {
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst),
                    ctx->fs_inputs[input],
                    ctx->fs_inputs[input + 1]);
      }
      emit_split_vector(ctx, dst, 2);
      break;
   }
   case nir_intrinsic_load_barycentric_at_sample: {
      uint32_t sample_pos_offset = RING_PS_SAMPLE_POSITIONS * 16;
      switch (ctx->options->key.fs.num_samples) {
         case 2: sample_pos_offset += 1 << 3; break;
         case 4: sample_pos_offset += 3 << 3; break;
         case 8: sample_pos_offset += 7 << 3; break;
         default: break;
      }
      Temp sample_pos;
      Temp addr = get_ssa_temp(ctx, instr->src[0].ssa);
      nir_const_value* const_addr = nir_src_as_const_value(instr->src[0]);
      Temp private_segment_buffer = ctx->program->private_segment_buffer;
      if (addr.type() == RegType::sgpr) {
         Operand offset;
         if (const_addr) {
            sample_pos_offset += const_addr->u32 << 3;
            offset = Operand(sample_pos_offset);
         } else if (ctx->options->chip_class >= GFX9) {
            offset = bld.sop2(aco_opcode::s_lshl3_add_u32, bld.def(s1), bld.def(s1, scc), addr, Operand(sample_pos_offset));
         } else {
            offset = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), addr, Operand(3u));
            offset = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.def(s1, scc), addr, Operand(sample_pos_offset));
         }
         sample_pos = bld.smem(aco_opcode::s_load_dwordx2, bld.def(s2), private_segment_buffer, Operand(offset));

      } else if (ctx->options->chip_class >= GFX9) {
         addr = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand(3u), addr);
         sample_pos = bld.global(aco_opcode::global_load_dwordx2, bld.def(v2), addr, private_segment_buffer, sample_pos_offset);
      } else {
         /* addr += private_segment_buffer + sample_pos_offset */
         Temp tmp0 = bld.tmp(s1);
         Temp tmp1 = bld.tmp(s1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(tmp0), Definition(tmp1), private_segment_buffer);
         Definition scc_tmp = bld.def(s1, scc);
         tmp0 = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), scc_tmp, tmp0, Operand(sample_pos_offset));
         tmp1 = bld.sop2(aco_opcode::s_addc_u32, bld.def(s1), bld.def(s1, scc), tmp1, Operand(0u), bld.scc(scc_tmp.getTemp()));
         addr = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand(3u), addr);
         Temp pck0 = bld.tmp(v1);
         Temp carry = bld.vadd32(Definition(pck0), tmp0, addr, true).def(1).getTemp();
         tmp1 = as_vgpr(ctx, tmp1);
         Temp pck1 = bld.vop2_e64(aco_opcode::v_addc_co_u32, bld.def(v1), bld.hint_vcc(bld.def(s2)), tmp1, Operand(0u), carry);
         addr = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), pck0, pck1);

         /* sample_pos = flat_load_dwordx2 addr */
         sample_pos = bld.flat(aco_opcode::flat_load_dwordx2, bld.def(v2), addr, Operand(s1));
      }

      /* sample_pos -= 0.5 */
      Temp pos1 = bld.tmp(RegClass(sample_pos.type(), 1));
      Temp pos2 = bld.tmp(RegClass(sample_pos.type(), 1));
      bld.pseudo(aco_opcode::p_split_vector, Definition(pos1), Definition(pos2), sample_pos);
      pos1 = bld.vop2_e64(aco_opcode::v_sub_f32, bld.def(v1), pos1, Operand(0x3f000000u));
      pos2 = bld.vop2_e64(aco_opcode::v_sub_f32, bld.def(v1), pos2, Operand(0x3f000000u));

      emit_interp_center(ctx, get_ssa_temp(ctx, &instr->dest.ssa), pos1, pos2);
      break;
   }
   case nir_intrinsic_load_barycentric_at_offset: {
      Temp offset = get_ssa_temp(ctx, instr->src[0].ssa);
      RegClass rc = RegClass(offset.type(), 1);
      Temp pos1 = bld.tmp(rc), pos2 = bld.tmp(rc);
      bld.pseudo(aco_opcode::p_split_vector, Definition(pos1), Definition(pos2), offset);
      emit_interp_center(ctx, get_ssa_temp(ctx, &instr->dest.ssa), pos1, pos2);
      break;
   }
   case nir_intrinsic_load_front_face: {
      bld.vopc(aco_opcode::v_cmp_lg_u32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               Operand(0u), ctx->fs_inputs[fs_input::front_face]).def(0).setHint(vcc);
      break;
   }
   case nir_intrinsic_load_view_index:
   case nir_intrinsic_load_layer_id: {
      if (instr->intrinsic == nir_intrinsic_load_view_index && (ctx->stage & sw_vs)) {
         Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
         bld.copy(Definition(dst), Operand(ctx->view_index));
         break;
      }

      unsigned idx = nir_intrinsic_base(instr);
      bld.vintrp(aco_opcode::v_interp_mov_f32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
                 Operand(2u), bld.m0(ctx->prim_mask), idx, 0);
      break;
   }
   case nir_intrinsic_load_frag_coord: {
      emit_load_frag_coord(ctx, get_ssa_temp(ctx, &instr->dest.ssa), 4);
      break;
   }
   case nir_intrinsic_load_sample_pos: {
      Temp posx = ctx->fs_inputs[fs_input::frag_pos_0];
      Temp posy = ctx->fs_inputs[fs_input::frag_pos_1];
      bld.pseudo(aco_opcode::p_create_vector, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
                 posx.id() ? bld.vop1(aco_opcode::v_fract_f32, bld.def(v1), posx) : Operand(0u),
                 posy.id() ? bld.vop1(aco_opcode::v_fract_f32, bld.def(v1), posy) : Operand(0u));
      break;
   }
   case nir_intrinsic_load_interpolated_input:
      visit_load_interpolated_input(ctx, instr);
      break;
   case nir_intrinsic_store_output:
      visit_store_output(ctx, instr);
      break;
   case nir_intrinsic_load_input:
      visit_load_input(ctx, instr);
      break;
   case nir_intrinsic_load_ubo:
      visit_load_ubo(ctx, instr);
      break;
   case nir_intrinsic_load_push_constant:
      visit_load_push_constant(ctx, instr);
      break;
   case nir_intrinsic_load_constant:
      visit_load_constant(ctx, instr);
      break;
   case nir_intrinsic_vulkan_resource_index:
      visit_load_resource(ctx, instr);
      break;
   case nir_intrinsic_discard:
      visit_discard(ctx, instr);
      break;
   case nir_intrinsic_discard_if:
      visit_discard_if(ctx, instr);
      break;
   case nir_intrinsic_load_shared:
      visit_load_shared(ctx, instr);
      break;
   case nir_intrinsic_store_shared:
      visit_store_shared(ctx, instr);
      break;
   case nir_intrinsic_shared_atomic_add:
   case nir_intrinsic_shared_atomic_imin:
   case nir_intrinsic_shared_atomic_umin:
   case nir_intrinsic_shared_atomic_imax:
   case nir_intrinsic_shared_atomic_umax:
   case nir_intrinsic_shared_atomic_and:
   case nir_intrinsic_shared_atomic_or:
   case nir_intrinsic_shared_atomic_xor:
   case nir_intrinsic_shared_atomic_exchange:
   case nir_intrinsic_shared_atomic_comp_swap:
      visit_shared_atomic(ctx, instr);
      break;
   case nir_intrinsic_image_deref_load:
      visit_image_load(ctx, instr);
      break;
   case nir_intrinsic_image_deref_store:
      visit_image_store(ctx, instr);
      break;
   case nir_intrinsic_image_deref_atomic_add:
   case nir_intrinsic_image_deref_atomic_umin:
   case nir_intrinsic_image_deref_atomic_imin:
   case nir_intrinsic_image_deref_atomic_umax:
   case nir_intrinsic_image_deref_atomic_imax:
   case nir_intrinsic_image_deref_atomic_and:
   case nir_intrinsic_image_deref_atomic_or:
   case nir_intrinsic_image_deref_atomic_xor:
   case nir_intrinsic_image_deref_atomic_exchange:
   case nir_intrinsic_image_deref_atomic_comp_swap:
      visit_image_atomic(ctx, instr);
      break;
   case nir_intrinsic_image_deref_size:
      visit_image_size(ctx, instr);
      break;
   case nir_intrinsic_load_ssbo:
      visit_load_ssbo(ctx, instr);
      break;
   case nir_intrinsic_store_ssbo:
      visit_store_ssbo(ctx, instr);
      break;
   case nir_intrinsic_load_global:
      visit_load_global(ctx, instr);
      break;
   case nir_intrinsic_store_global:
      visit_store_global(ctx, instr);
      break;
   case nir_intrinsic_ssbo_atomic_add:
   case nir_intrinsic_ssbo_atomic_imin:
   case nir_intrinsic_ssbo_atomic_umin:
   case nir_intrinsic_ssbo_atomic_imax:
   case nir_intrinsic_ssbo_atomic_umax:
   case nir_intrinsic_ssbo_atomic_and:
   case nir_intrinsic_ssbo_atomic_or:
   case nir_intrinsic_ssbo_atomic_xor:
   case nir_intrinsic_ssbo_atomic_exchange:
   case nir_intrinsic_ssbo_atomic_comp_swap:
      visit_atomic_ssbo(ctx, instr);
      break;
   case nir_intrinsic_load_scratch:
      visit_load_scratch(ctx, instr);
      break;
   case nir_intrinsic_store_scratch:
      visit_store_scratch(ctx, instr);
      break;
   case nir_intrinsic_get_buffer_size:
      visit_get_buffer_size(ctx, instr);
      break;
   case nir_intrinsic_barrier: {
      unsigned* bsize = ctx->program->info->cs.block_size;
      unsigned workgroup_size = bsize[0] * bsize[1] * bsize[2];
      if (workgroup_size > 64)
         bld.sopp(aco_opcode::s_barrier);
      break;
   }
   case nir_intrinsic_group_memory_barrier:
   case nir_intrinsic_memory_barrier:
   case nir_intrinsic_memory_barrier_atomic_counter:
   case nir_intrinsic_memory_barrier_buffer:
   case nir_intrinsic_memory_barrier_image:
   case nir_intrinsic_memory_barrier_shared:
      emit_memory_barrier(ctx, instr);
      break;
   case nir_intrinsic_load_num_work_groups:
   case nir_intrinsic_load_work_group_id:
   case nir_intrinsic_load_local_invocation_id: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      Temp* ids;
      if (instr->intrinsic == nir_intrinsic_load_num_work_groups)
         ids = ctx->num_workgroups;
      else if (instr->intrinsic == nir_intrinsic_load_work_group_id)
         ids = ctx->workgroup_ids;
      else
         ids = ctx->local_invocation_ids;
      bld.pseudo(aco_opcode::p_create_vector, Definition(dst),
                 ids[0].id() ? Operand(ids[0]) : Operand(1u),
                 ids[1].id() ? Operand(ids[1]) : Operand(1u),
                 ids[2].id() ? Operand(ids[2]) : Operand(1u));
      emit_split_vector(ctx, dst, 3);
      break;
   }
   case nir_intrinsic_load_local_invocation_index: {
      Temp id = bld.vop3(aco_opcode::v_mbcnt_hi_u32_b32, bld.def(v1), Operand((uint32_t) -1),
                         bld.vop3(aco_opcode::v_mbcnt_lo_u32_b32, bld.def(v1), Operand((uint32_t) -1), Operand(0u)));
      Temp tg_num = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), Operand(0xfc0u), ctx->tg_size);
      bld.vop2(aco_opcode::v_or_b32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)), tg_num, id);
      break;
   }
   case nir_intrinsic_load_subgroup_id: {
      if (ctx->stage == compute_cs) {
         Temp tg_num = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), Operand(0xfc0u), ctx->tg_size);
         bld.sop2(aco_opcode::s_lshr_b32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)), bld.def(s1, scc), tg_num, Operand(0x6u));
      } else {
         bld.sop1(aco_opcode::s_mov_b32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)), Operand(0x0u));
      }
      break;
   }
   case nir_intrinsic_load_subgroup_invocation: {
      bld.vop3(aco_opcode::v_mbcnt_hi_u32_b32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)), Operand((uint32_t) -1),
               bld.vop3(aco_opcode::v_mbcnt_lo_u32_b32, bld.def(v1), Operand((uint32_t) -1), Operand(0u)));
      break;
   }
   case nir_intrinsic_load_num_subgroups: {
      if (ctx->stage == compute_cs)
         bld.sop2(aco_opcode::s_and_b32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)), bld.def(s1, scc), Operand(0x3fu), ctx->tg_size);
      else
         bld.sop1(aco_opcode::s_mov_b32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)), Operand(0x1u));
      break;
   }
   case nir_intrinsic_ballot: {
      Definition tmp = bld.def(s2);
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      if (instr->src[0].ssa->bit_size == 1 && src.regClass() == s2) {
         bld.sop2(aco_opcode::s_and_b64, tmp, bld.def(s1, scc), Operand(exec, s2), src);
      } else if (instr->src[0].ssa->bit_size == 1 && src.regClass() == s1) {
         bld.sop2(aco_opcode::s_cselect_b64, tmp, Operand(exec, s2), Operand(0u), bld.scc(src));
      } else if (instr->src[0].ssa->bit_size == 32 && src.regClass() == v1) {
         bld.vopc(aco_opcode::v_cmp_lg_u32, tmp, Operand(0u), src);
      } else if (instr->src[0].ssa->bit_size == 64 && src.regClass() == v2) {
         bld.vopc(aco_opcode::v_cmp_lg_u64, tmp, Operand(0u), src);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      emit_wqm(ctx, tmp.getTemp(), get_ssa_temp(ctx, &instr->dest.ssa));
      break;
   }
   case nir_intrinsic_shuffle: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      if (!ctx->divergent_vals[instr->dest.ssa.index]) {
         emit_uniform_subgroup(ctx, instr, src);
      } else {
         Temp tid = get_ssa_temp(ctx, instr->src[1].ssa);
         assert(tid.regClass() == v1);
         Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
         if (src.regClass() == v1) {
            emit_wqm(ctx, emit_bpermute(ctx, bld, tid, src), dst);
         } else if (src.regClass() == v2) {
            Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
            bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
            lo = emit_wqm(ctx, emit_bpermute(ctx, bld, tid, lo));
            hi = emit_wqm(ctx, emit_bpermute(ctx, bld, tid, hi));
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
            emit_split_vector(ctx, dst, 2);
         } else if (instr->dest.ssa.bit_size == 1 && src.regClass() == s2) {
            Temp tmp = bld.vop3(aco_opcode::v_lshrrev_b64, bld.def(v2), tid, src);
            tmp = emit_extract_vector(ctx, tmp, 0, v1);
            tmp = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(1u), tmp);
            emit_wqm(ctx, bld.vopc(aco_opcode::v_cmp_lg_u32, bld.def(s2), Operand(0u), tmp), dst);
         } else {
            fprintf(stderr, "Unimplemented NIR instr bit size: ");
            nir_print_instr(&instr->instr, stderr);
            fprintf(stderr, "\n");
         }
      }
      break;
   }
   case nir_intrinsic_load_sample_id: {
      bld.vop3(aco_opcode::v_bfe_u32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               ctx->fs_inputs[ancillary], Operand(8u), Operand(4u));
      break;
   }
   case nir_intrinsic_load_sample_mask_in: {
      visit_load_sample_mask_in(ctx, instr);
      break;
   }
   case nir_intrinsic_read_first_invocation: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      if (src.regClass() == v1) {
         emit_wqm(ctx,
                  bld.vop1(aco_opcode::v_readfirstlane_b32, bld.def(s1), src),
                  dst);
      } else if (src.regClass() == v2) {
         Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
         lo = emit_wqm(ctx, bld.vop1(aco_opcode::v_readfirstlane_b32, bld.def(s1), lo));
         hi = emit_wqm(ctx, bld.vop1(aco_opcode::v_readfirstlane_b32, bld.def(s1), hi));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
         emit_split_vector(ctx, dst, 2);
      } else if (instr->dest.ssa.bit_size == 1 && src.regClass() == s2) {
         emit_wqm(ctx,
                  bld.sopc(aco_opcode::s_bitcmp1_b64, bld.def(s1, scc), src,
                           bld.sop1(aco_opcode::s_ff1_i32_b64, bld.def(s1), Operand(exec, s2))),
                  dst);
      } else if (src.regClass() == s1) {
         bld.sop1(aco_opcode::s_mov_b32, Definition(dst), src);
      } else if (src.regClass() == s2) {
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), src);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_intrinsic_read_invocation: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp lane = get_ssa_temp(ctx, instr->src[1].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      assert(lane.regClass() == s1);
      if (src.regClass() == v1) {
         emit_wqm(ctx, bld.vop3(aco_opcode::v_readlane_b32, bld.def(s1), src, lane), dst);
      } else if (src.regClass() == v2) {
         Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
         lo = emit_wqm(ctx, bld.vop3(aco_opcode::v_readlane_b32, bld.def(s1), lo, lane));
         hi = emit_wqm(ctx, bld.vop3(aco_opcode::v_readlane_b32, bld.def(s1), hi, lane));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
         emit_split_vector(ctx, dst, 2);
      } else if (instr->dest.ssa.bit_size == 1 && src.regClass() == s2) {
         emit_wqm(ctx, bld.sopc(aco_opcode::s_bitcmp1_b64, bld.def(s1, scc), src, lane), dst);
      } else if (src.regClass() == s1) {
         bld.sop1(aco_opcode::s_mov_b32, Definition(dst), src);
      } else if (src.regClass() == s2) {
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), src);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_intrinsic_vote_all: {
      Temp src = as_divergent_bool(ctx, get_ssa_temp(ctx, instr->src[0].ssa), false);
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      assert(src.regClass() == s2);
      assert(dst.regClass() == s1);

      Definition tmp = bld.def(s1);
      bld.sopc(aco_opcode::s_cmp_eq_u64, bld.scc(tmp),
               bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.def(s1, scc), src, Operand(exec, s2)),
               Operand(exec, s2));
      emit_wqm(ctx, tmp.getTemp(), dst);
      break;
   }
   case nir_intrinsic_vote_any: {
      Temp src = as_divergent_bool(ctx, get_ssa_temp(ctx, instr->src[0].ssa), false);
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      assert(src.regClass() == s2);
      assert(dst.regClass() == s1);

      Definition tmp = bld.def(s1);
      bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.scc(tmp), src, Operand(exec, s2));
      emit_wqm(ctx, tmp.getTemp(), dst);
      break;
   }
   case nir_intrinsic_reduce:
   case nir_intrinsic_inclusive_scan:
   case nir_intrinsic_exclusive_scan: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      nir_op op = (nir_op) nir_intrinsic_reduction_op(instr);
      unsigned cluster_size = instr->intrinsic == nir_intrinsic_reduce ?
         nir_intrinsic_cluster_size(instr) : 0;
      cluster_size = util_next_power_of_two(MIN2(cluster_size ? cluster_size : 64, 64));

      if (!ctx->divergent_vals[instr->src[0].ssa->index] && (op == nir_op_ior || op == nir_op_iand)) {
         emit_uniform_subgroup(ctx, instr, src);
      } else if (instr->dest.ssa.bit_size == 1) {
         if (op == nir_op_imul || op == nir_op_umin || op == nir_op_imin)
            op = nir_op_iand;
         else if (op == nir_op_iadd)
            op = nir_op_ixor;
         else if (op == nir_op_umax || op == nir_op_imax)
            op = nir_op_ior;
         assert(op == nir_op_iand || op == nir_op_ior || op == nir_op_ixor);

         switch (instr->intrinsic) {
         case nir_intrinsic_reduce:
            emit_wqm(ctx, emit_boolean_reduce(ctx, op, cluster_size, src), dst);
            break;
         case nir_intrinsic_exclusive_scan:
            emit_wqm(ctx, emit_boolean_exclusive_scan(ctx, op, src), dst);
            break;
         case nir_intrinsic_inclusive_scan:
            emit_wqm(ctx, emit_boolean_inclusive_scan(ctx, op, src), dst);
            break;
         default:
            assert(false);
         }
      } else if (cluster_size == 1) {
         bld.copy(Definition(dst), src);
      } else {
         src = as_vgpr(ctx, src);

         ReduceOp reduce_op;
         switch (op) {
         #define CASE(name) case nir_op_##name: reduce_op = (src.regClass() == v1) ? name##32 : name##64; break;
            CASE(iadd)
            CASE(imul)
            CASE(fadd)
            CASE(fmul)
            CASE(imin)
            CASE(umin)
            CASE(fmin)
            CASE(imax)
            CASE(umax)
            CASE(fmax)
            CASE(iand)
            CASE(ior)
            CASE(ixor)
            default:
               unreachable("unknown reduction op");
         #undef CASE
         }

         aco_opcode aco_op;
         switch (instr->intrinsic) {
            case nir_intrinsic_reduce: aco_op = aco_opcode::p_reduce; break;
            case nir_intrinsic_inclusive_scan: aco_op = aco_opcode::p_inclusive_scan; break;
            case nir_intrinsic_exclusive_scan: aco_op = aco_opcode::p_exclusive_scan; break;
            default:
               unreachable("unknown reduce intrinsic");
         }

         aco_ptr<Pseudo_reduction_instruction> reduce{create_instruction<Pseudo_reduction_instruction>(aco_op, Format::PSEUDO_REDUCTION, 3, 5)};
         reduce->operands[0] = Operand(src);
         // filled in by aco_reduce_assign.cpp, used internally as part of the
         // reduce sequence
         assert(dst.size() == 1 || dst.size() == 2);
         reduce->operands[1] = Operand(RegClass(RegType::vgpr, dst.size()).as_linear());
         reduce->operands[2] = Operand(v1.as_linear());

         Temp tmp_dst = bld.tmp(dst.regClass());
         reduce->definitions[0] = Definition(tmp_dst);
         reduce->definitions[1] = bld.def(s2); // used internally
         reduce->definitions[2] = Definition();
         reduce->definitions[3] = Definition(scc, s1);
         reduce->definitions[4] = Definition();
         reduce->reduce_op = reduce_op;
         reduce->cluster_size = cluster_size;
         ctx->block->instructions.emplace_back(std::move(reduce));

         emit_wqm(ctx, tmp_dst, dst);
      }
      break;
   }
   case nir_intrinsic_quad_broadcast: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      if (!ctx->divergent_vals[instr->dest.ssa.index]) {
         emit_uniform_subgroup(ctx, instr, src);
      } else {
         Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
         unsigned lane = nir_src_as_const_value(instr->src[1])->u32;
         if (instr->dest.ssa.bit_size == 1 && src.regClass() == s2) {
            uint32_t half_mask = 0x11111111u << lane;
            Temp mask_tmp = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand(half_mask), Operand(half_mask));
            Temp tmp = bld.tmp(s2);
            bld.sop1(aco_opcode::s_wqm_b64, Definition(tmp),
                     bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.def(s1, scc), mask_tmp,
                              bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.def(s1, scc), src, Operand(exec, s2))));
            emit_wqm(ctx, tmp, dst);
         } else if (instr->dest.ssa.bit_size == 32) {
            emit_wqm(ctx,
                     bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), src,
                                  dpp_quad_perm(lane, lane, lane, lane)),
                     dst);
         } else if (instr->dest.ssa.bit_size == 64) {
            Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
            bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
            lo = emit_wqm(ctx, bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), lo, dpp_quad_perm(lane, lane, lane, lane)));
            hi = emit_wqm(ctx, bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), hi, dpp_quad_perm(lane, lane, lane, lane)));
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
            emit_split_vector(ctx, dst, 2);
         } else {
            fprintf(stderr, "Unimplemented NIR instr bit size: ");
            nir_print_instr(&instr->instr, stderr);
            fprintf(stderr, "\n");
         }
      }
      break;
   }
   case nir_intrinsic_quad_swap_horizontal:
   case nir_intrinsic_quad_swap_vertical:
   case nir_intrinsic_quad_swap_diagonal:
   case nir_intrinsic_quad_swizzle_amd: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      if (!ctx->divergent_vals[instr->dest.ssa.index]) {
         emit_uniform_subgroup(ctx, instr, src);
         break;
      }
      uint16_t dpp_ctrl = 0;
      switch (instr->intrinsic) {
      case nir_intrinsic_quad_swap_horizontal:
         dpp_ctrl = dpp_quad_perm(1, 0, 3, 2);
         break;
      case nir_intrinsic_quad_swap_vertical:
         dpp_ctrl = dpp_quad_perm(2, 3, 0, 1);
         break;
      case nir_intrinsic_quad_swap_diagonal:
         dpp_ctrl = dpp_quad_perm(3, 2, 1, 0);
         break;
      case nir_intrinsic_quad_swizzle_amd: {
         dpp_ctrl = nir_intrinsic_swizzle_mask(instr);
         break;
      }
      default:
         break;
      }

      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      if (instr->dest.ssa.bit_size == 1 && src.regClass() == s2) {
         src = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand(0u), Operand((uint32_t)-1), src);
         src = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), src, dpp_ctrl);
         Temp tmp = bld.vopc(aco_opcode::v_cmp_lg_u32, bld.def(s2), Operand(0u), src);
         emit_wqm(ctx, tmp, dst);
      } else if (instr->dest.ssa.bit_size == 32) {
         Temp tmp = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), src, dpp_ctrl);
         emit_wqm(ctx, tmp, dst);
      } else if (instr->dest.ssa.bit_size == 64) {
         Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
         lo = emit_wqm(ctx, bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), lo, dpp_ctrl));
         hi = emit_wqm(ctx, bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), hi, dpp_ctrl));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
         emit_split_vector(ctx, dst, 2);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_intrinsic_masked_swizzle_amd: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      if (!ctx->divergent_vals[instr->dest.ssa.index]) {
         emit_uniform_subgroup(ctx, instr, src);
         break;
      }
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      uint32_t mask = nir_intrinsic_swizzle_mask(instr);
      if (dst.regClass() == v1) {
         emit_wqm(ctx,
                  bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, mask, 0, false),
                  dst);
      } else if (dst.regClass() == v2) {
         Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
         lo = emit_wqm(ctx, bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), lo, mask, 0, false));
         hi = emit_wqm(ctx, bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), hi, mask, 0, false));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
         emit_split_vector(ctx, dst, 2);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_intrinsic_write_invocation_amd: {
      Temp src = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
      Temp val = bld.as_uniform(get_ssa_temp(ctx, instr->src[1].ssa));
      Temp lane = bld.as_uniform(get_ssa_temp(ctx, instr->src[2].ssa));
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      if (dst.regClass() == v1) {
         /* src2 is ignored for writelane. RA assigns the same reg for dst */
         emit_wqm(ctx, bld.vop3(aco_opcode::v_writelane_b32, bld.def(v1), val, lane, src), dst);
      } else if (dst.regClass() == v2) {
         Temp src_lo = bld.tmp(v1), src_hi = bld.tmp(v1);
         Temp val_lo = bld.tmp(s1), val_hi = bld.tmp(s1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(src_lo), Definition(src_hi), src);
         bld.pseudo(aco_opcode::p_split_vector, Definition(val_lo), Definition(val_hi), val);
         Temp lo = emit_wqm(ctx, bld.vop3(aco_opcode::v_writelane_b32, bld.def(v1), val_lo, lane, src_hi));
         Temp hi = emit_wqm(ctx, bld.vop3(aco_opcode::v_writelane_b32, bld.def(v1), val_hi, lane, src_hi));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
         emit_split_vector(ctx, dst, 2);
      } else {
         fprintf(stderr, "Unimplemented NIR instr bit size: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
      }
      break;
   }
   case nir_intrinsic_mbcnt_amd: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      RegClass rc = RegClass(src.type(), 1);
      Temp mask_lo = bld.tmp(rc), mask_hi = bld.tmp(rc);
      bld.pseudo(aco_opcode::p_split_vector, Definition(mask_lo), Definition(mask_hi), src);
      Temp tmp = bld.vop3(aco_opcode::v_mbcnt_lo_u32_b32, bld.def(v1), mask_lo, Operand(0u));
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      Temp wqm_tmp = bld.vop3(aco_opcode::v_mbcnt_hi_u32_b32, bld.def(v1), mask_hi, tmp);
      emit_wqm(ctx, wqm_tmp, dst);
      break;
   }
   case nir_intrinsic_load_helper_invocation: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.pseudo(aco_opcode::p_load_helper, Definition(dst));
      ctx->block->kind |= block_kind_needs_lowering;
      ctx->program->needs_exact = true;
      break;
   }
   case nir_intrinsic_is_helper_invocation: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.pseudo(aco_opcode::p_is_helper, Definition(dst));
      ctx->block->kind |= block_kind_needs_lowering;
      ctx->program->needs_exact = true;
      break;
   }
   case nir_intrinsic_demote:
      bld.pseudo(aco_opcode::p_demote_to_helper);
      ctx->block->kind |= block_kind_uses_demote;
      ctx->program->needs_exact = true;
      break;
   case nir_intrinsic_demote_if: {
      Temp cond = bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.def(s1, scc),
                           as_divergent_bool(ctx, get_ssa_temp(ctx, instr->src[0].ssa), false),
                           Operand(exec, s2));
      bld.pseudo(aco_opcode::p_demote_to_helper, cond);
      ctx->block->kind |= block_kind_uses_demote;
      ctx->program->needs_exact = true;
      break;
   }
   case nir_intrinsic_first_invocation: {
      emit_wqm(ctx, bld.sop1(aco_opcode::s_ff1_i32_b64, bld.def(s1), Operand(exec, s2)),
               get_ssa_temp(ctx, &instr->dest.ssa));
      break;
   }
   case nir_intrinsic_shader_clock:
      bld.smem(aco_opcode::s_memtime, Definition(get_ssa_temp(ctx, &instr->dest.ssa)), false);
      emit_split_vector(ctx, get_ssa_temp(ctx, &instr->dest.ssa), 2);
      break;
   case nir_intrinsic_load_vertex_id_zero_base: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.copy(Definition(dst), ctx->vertex_id);
      break;
   }
   case nir_intrinsic_load_first_vertex: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.copy(Definition(dst), ctx->base_vertex);
      break;
   }
   case nir_intrinsic_load_base_instance: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.copy(Definition(dst), ctx->start_instance);
      break;
   }
   case nir_intrinsic_load_instance_id: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.copy(Definition(dst), ctx->instance_id);
      break;
   }
   case nir_intrinsic_load_draw_id: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.copy(Definition(dst), ctx->draw_id);
      break;
   }
   default:
      fprintf(stderr, "Unimplemented intrinsic instr: ");
      nir_print_instr(&instr->instr, stderr);
      fprintf(stderr, "\n");
      abort();

      break;
   }
}


void tex_fetch_ptrs(isel_context *ctx, nir_tex_instr *instr,
                    Temp *res_ptr, Temp *samp_ptr, Temp *fmask_ptr,
                    enum glsl_base_type *stype)
{
   nir_deref_instr *texture_deref_instr = NULL;
   nir_deref_instr *sampler_deref_instr = NULL;
   int plane = -1;

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      switch (instr->src[i].src_type) {
      case nir_tex_src_texture_deref:
         texture_deref_instr = nir_src_as_deref(instr->src[i].src);
         break;
      case nir_tex_src_sampler_deref:
         sampler_deref_instr = nir_src_as_deref(instr->src[i].src);
         break;
      case nir_tex_src_plane:
         plane = nir_src_as_int(instr->src[i].src);
         break;
      default:
         break;
      }
   }

   *stype = glsl_get_sampler_result_type(texture_deref_instr->type);

   if (!sampler_deref_instr)
      sampler_deref_instr = texture_deref_instr;

   if (plane >= 0) {
      assert(instr->op != nir_texop_txf_ms &&
             instr->op != nir_texop_samples_identical);
      assert(instr->sampler_dim  != GLSL_SAMPLER_DIM_BUF);
      *res_ptr = get_sampler_desc(ctx, texture_deref_instr, (aco_descriptor_type)(ACO_DESC_PLANE_0 + plane), instr, false, false);
   } else if (instr->sampler_dim  == GLSL_SAMPLER_DIM_BUF) {
      *res_ptr = get_sampler_desc(ctx, texture_deref_instr, ACO_DESC_BUFFER, instr, false, false);
   } else {
      *res_ptr = get_sampler_desc(ctx, texture_deref_instr, ACO_DESC_IMAGE, instr, false, false);
   }
   if (samp_ptr) {
      *samp_ptr = get_sampler_desc(ctx, sampler_deref_instr, ACO_DESC_SAMPLER, instr, false, false);
      if (instr->sampler_dim < GLSL_SAMPLER_DIM_RECT && ctx->options->chip_class < GFX8) {
         fprintf(stderr, "Unimplemented sampler descriptor: ");
         nir_print_instr(&instr->instr, stderr);
         fprintf(stderr, "\n");
         abort();
         // TODO: build samp_ptr = and(samp_ptr, res_ptr)
      }
   }
   if (fmask_ptr && (instr->op == nir_texop_txf_ms ||
                     instr->op == nir_texop_samples_identical))
      *fmask_ptr = get_sampler_desc(ctx, texture_deref_instr, ACO_DESC_FMASK, instr, false, false);
}

void build_cube_select(isel_context *ctx, Temp ma, Temp id, Temp deriv,
                       Temp *out_ma, Temp *out_sc, Temp *out_tc)
{
   Builder bld(ctx->program, ctx->block);

   Temp deriv_x = emit_extract_vector(ctx, deriv, 0, v1);
   Temp deriv_y = emit_extract_vector(ctx, deriv, 1, v1);
   Temp deriv_z = emit_extract_vector(ctx, deriv, 2, v1);

   Operand neg_one(0xbf800000u);
   Operand one(0x3f800000u);
   Operand two(0x40000000u);
   Operand four(0x40800000u);

   Temp is_ma_positive = bld.vopc(aco_opcode::v_cmp_le_f32, bld.hint_vcc(bld.def(s2)), Operand(0u), ma);
   Temp sgn_ma = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), neg_one, one, is_ma_positive);
   Temp neg_sgn_ma = bld.vop2(aco_opcode::v_sub_f32, bld.def(v1), Operand(0u), sgn_ma);

   Temp is_ma_z = bld.vopc(aco_opcode::v_cmp_le_f32, bld.hint_vcc(bld.def(s2)), four, id);
   Temp is_ma_y = bld.vopc(aco_opcode::v_cmp_le_f32, bld.def(s2), two, id);
   is_ma_y = bld.sop2(aco_opcode::s_andn2_b64, bld.hint_vcc(bld.def(s2)), is_ma_y, is_ma_z);
   Temp is_not_ma_x = bld.sop2(aco_opcode::s_or_b64, bld.hint_vcc(bld.def(s2)), bld.def(s1, scc), is_ma_z, is_ma_y);

   // select sc
   Temp tmp = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), deriv_z, deriv_x, is_not_ma_x);
   Temp sgn = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1),
                       bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), neg_sgn_ma, sgn_ma, is_ma_z),
                       one, is_ma_y);
   *out_sc = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), tmp, sgn);

   // select tc
   tmp = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), deriv_y, deriv_z, is_ma_y);
   sgn = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), neg_one, sgn_ma, is_ma_y);
   *out_tc = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), tmp, sgn);

   // select ma
   tmp = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1),
                  bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), deriv_x, deriv_y, is_ma_y),
                  deriv_z, is_ma_z);
   tmp = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(0x7fffffffu), tmp);
   *out_ma = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), two, tmp);
}

void prepare_cube_coords(isel_context *ctx, Temp* coords, Temp* ddx, Temp* ddy, bool is_deriv, bool is_array)
{
   Builder bld(ctx->program, ctx->block);
   Temp coord_args[4], ma, tc, sc, id;
   for (unsigned i = 0; i < (is_array ? 4 : 3); i++)
      coord_args[i] = emit_extract_vector(ctx, *coords, i, v1);

   if (is_array) {
      coord_args[3] = bld.vop1(aco_opcode::v_rndne_f32, bld.def(v1), coord_args[3]);

      // see comment in ac_prepare_cube_coords()
      if (ctx->options->chip_class <= GFX8)
         coord_args[3] = bld.vop2(aco_opcode::v_max_f32, bld.def(v1), Operand(0u), coord_args[3]);
   }

   ma = bld.vop3(aco_opcode::v_cubema_f32, bld.def(v1), coord_args[0], coord_args[1], coord_args[2]);

   aco_ptr<VOP3A_instruction> vop3a{create_instruction<VOP3A_instruction>(aco_opcode::v_rcp_f32, asVOP3(Format::VOP1), 1, 1)};
   vop3a->operands[0] = Operand(ma);
   vop3a->abs[0] = true;
   Temp invma = bld.tmp(v1);
   vop3a->definitions[0] = Definition(invma);
   ctx->block->instructions.emplace_back(std::move(vop3a));

   sc = bld.vop3(aco_opcode::v_cubesc_f32, bld.def(v1), coord_args[0], coord_args[1], coord_args[2]);
   if (!is_deriv)
      sc = bld.vop2(aco_opcode::v_madak_f32, bld.def(v1), sc, invma, Operand(0x3fc00000u/*1.5*/));

   tc = bld.vop3(aco_opcode::v_cubetc_f32, bld.def(v1), coord_args[0], coord_args[1], coord_args[2]);
   if (!is_deriv)
      tc = bld.vop2(aco_opcode::v_madak_f32, bld.def(v1), tc, invma, Operand(0x3fc00000u/*1.5*/));

   id = bld.vop3(aco_opcode::v_cubeid_f32, bld.def(v1), coord_args[0], coord_args[1], coord_args[2]);

   if (is_deriv) {
      sc = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), sc, invma);
      tc = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), tc, invma);

      for (unsigned i = 0; i < 2; i++) {
         // see comment in ac_prepare_cube_coords()
         Temp deriv_ma;
         Temp deriv_sc, deriv_tc;
         build_cube_select(ctx, ma, id, i ? *ddy : *ddx,
                           &deriv_ma, &deriv_sc, &deriv_tc);

         deriv_ma = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), deriv_ma, invma);

         Temp x = bld.vop2(aco_opcode::v_sub_f32, bld.def(v1),
                               bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), deriv_sc, invma),
                               bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), deriv_ma, sc));
         Temp y = bld.vop2(aco_opcode::v_sub_f32, bld.def(v1),
                               bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), deriv_tc, invma),
                               bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), deriv_ma, tc));
         *(i ? ddy : ddx) = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), x, y);
      }

      sc = bld.vop2(aco_opcode::v_add_f32, bld.def(v1), Operand(0x3fc00000u/*1.5*/), sc);
      tc = bld.vop2(aco_opcode::v_add_f32, bld.def(v1), Operand(0x3fc00000u/*1.5*/), tc);
   }

   if (is_array)
      id = bld.vop2(aco_opcode::v_madmk_f32, bld.def(v1), coord_args[3], id, Operand(0x41000000u/*8.0*/));
   *coords = bld.pseudo(aco_opcode::p_create_vector, bld.def(v3), sc, tc, id);

}

Temp apply_round_slice(isel_context *ctx, Temp coords, unsigned idx)
{
   Temp coord_vec[3];
   for (unsigned i = 0; i < coords.size(); i++)
      coord_vec[i] = emit_extract_vector(ctx, coords, i, v1);

   Builder bld(ctx->program, ctx->block);
   coord_vec[idx] = bld.vop1(aco_opcode::v_rndne_f32, bld.def(v1), coord_vec[idx]);

   aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, coords.size(), 1)};
   for (unsigned i = 0; i < coords.size(); i++)
      vec->operands[i] = Operand(coord_vec[i]);
   Temp res = bld.tmp(RegType::vgpr, coords.size());
   vec->definitions[0] = Definition(res);
   ctx->block->instructions.emplace_back(std::move(vec));
   return res;
}

void get_const_vec(nir_ssa_def *vec, nir_const_value *cv[4])
{
   if (vec->parent_instr->type != nir_instr_type_alu)
      return;
   nir_alu_instr *vec_instr = nir_instr_as_alu(vec->parent_instr);
   if (vec_instr->op != nir_op_vec(vec->num_components))
      return;

   for (unsigned i = 0; i < vec->num_components; i++) {
      cv[i] = vec_instr->src[i].swizzle[0] == 0 ?
              nir_src_as_const_value(vec_instr->src[i].src) : NULL;
   }
}

void visit_tex(isel_context *ctx, nir_tex_instr *instr)
{
   Builder bld(ctx->program, ctx->block);
   bool has_bias = false, has_lod = false, level_zero = false, has_compare = false,
        has_offset = false, has_ddx = false, has_ddy = false, has_derivs = false, has_sample_index = false;
   Temp resource, sampler, fmask_ptr, bias = Temp(), coords, compare = Temp(), sample_index = Temp(),
        lod = Temp(), offset = Temp(), ddx = Temp(), ddy = Temp(), derivs = Temp();
   nir_const_value *sample_index_cv = NULL;
   nir_const_value *const_offset[4] = {NULL, NULL, NULL, NULL};
   enum glsl_base_type stype;
   tex_fetch_ptrs(ctx, instr, &resource, &sampler, &fmask_ptr, &stype);

   bool tg4_integer_workarounds = ctx->options->chip_class <= GFX8 && instr->op == nir_texop_tg4 &&
                                  (stype == GLSL_TYPE_UINT || stype == GLSL_TYPE_INT);
   bool tg4_integer_cube_workaround = tg4_integer_workarounds &&
                                      instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE;

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      switch (instr->src[i].src_type) {
      case nir_tex_src_coord:
         coords = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[i].src.ssa));
         break;
      case nir_tex_src_bias:
         if (instr->op == nir_texop_txb) {
            bias = get_ssa_temp(ctx, instr->src[i].src.ssa);
            has_bias = true;
         }
         break;
      case nir_tex_src_lod: {
         nir_const_value *val = nir_src_as_const_value(instr->src[i].src);

         if (val && val->f32 <= 0.0) {
            level_zero = true;
         } else {
            lod = get_ssa_temp(ctx, instr->src[i].src.ssa);
            has_lod = true;
         }
         break;
      }
      case nir_tex_src_comparator:
         if (instr->is_shadow) {
            compare = get_ssa_temp(ctx, instr->src[i].src.ssa);
            has_compare = true;
         }
         break;
      case nir_tex_src_offset:
         offset = get_ssa_temp(ctx, instr->src[i].src.ssa);
         get_const_vec(instr->src[i].src.ssa, const_offset);
         has_offset = true;
         break;
      case nir_tex_src_ddx:
         ddx = get_ssa_temp(ctx, instr->src[i].src.ssa);
         has_ddx = true;
         break;
      case nir_tex_src_ddy:
         ddy = get_ssa_temp(ctx, instr->src[i].src.ssa);
         has_ddy = true;
         break;
      case nir_tex_src_ms_index:
         sample_index = get_ssa_temp(ctx, instr->src[i].src.ssa);
         sample_index_cv = nir_src_as_const_value(instr->src[i].src);
         has_sample_index = true;
         break;
      case nir_tex_src_texture_offset:
      case nir_tex_src_sampler_offset:
      default:
         break;
      }
   }
// TODO: all other cases: structure taken from ac_nir_to_llvm.c
   if (instr->op == nir_texop_txs && instr->sampler_dim == GLSL_SAMPLER_DIM_BUF)
      return get_buffer_size(ctx, resource, get_ssa_temp(ctx, &instr->dest.ssa), true);

   if (instr->op == nir_texop_texture_samples) {
      Temp dword3 = emit_extract_vector(ctx, resource, 3, s1);

      Temp samples_log2 = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc), dword3, Operand(16u | 4u<<16));
      Temp samples = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), Operand(1u), samples_log2);
      Temp type = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc), dword3, Operand(28u | 4u<<16 /* offset=28, width=4 */));
      Temp is_msaa = bld.sopc(aco_opcode::s_cmp_ge_u32, bld.def(s1, scc), type, Operand(14u));

      bld.sop2(aco_opcode::s_cselect_b32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               samples, Operand(1u), bld.scc(is_msaa));
      return;
   }

   if (has_offset && instr->op != nir_texop_txf && instr->op != nir_texop_txf_ms) {
      aco_ptr<Instruction> tmp_instr;
      Temp acc, pack = Temp();

      uint32_t pack_const = 0;
      for (unsigned i = 0; i < offset.size(); i++) {
         if (!const_offset[i])
            continue;
         pack_const |= (const_offset[i]->u32 & 0x3Fu) << (8u * i);
      }

      if (offset.type() == RegType::sgpr) {
         for (unsigned i = 0; i < offset.size(); i++) {
            if (const_offset[i])
               continue;

            acc = emit_extract_vector(ctx, offset, i, s1);
            acc = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), acc, Operand(0x3Fu));

            if (i) {
               acc = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), acc, Operand(8u * i));
            }

            if (pack == Temp()) {
               pack = acc;
            } else {
               pack = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), pack, acc);
            }
         }

         if (pack_const && pack != Temp())
            pack = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), Operand(pack_const), pack);
      } else {
         for (unsigned i = 0; i < offset.size(); i++) {
            if (const_offset[i])
               continue;

            acc = emit_extract_vector(ctx, offset, i, v1);
            acc = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand(0x3Fu), acc);

            if (i) {
               acc = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand(8u * i), acc);
            }

            if (pack == Temp()) {
               pack = acc;
            } else {
               pack = bld.vop2(aco_opcode::v_or_b32, bld.def(v1), pack, acc);
            }
         }

         if (pack_const && pack != Temp())
            pack = bld.sop2(aco_opcode::v_or_b32, bld.def(v1), Operand(pack_const), pack);
      }
      if (pack_const && pack == Temp())
         offset = bld.vop1(aco_opcode::v_mov_b32, bld.def(v1), Operand(pack_const));
      else if (pack == Temp())
         has_offset = false;
      else
         offset = pack;
   }

   if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE && instr->coord_components)
      prepare_cube_coords(ctx, &coords, &ddx, &ddy, instr->op == nir_texop_txd, instr->is_array && instr->op != nir_texop_lod);

   /* pack derivatives */
   if (has_ddx || has_ddy) {
      if (instr->sampler_dim == GLSL_SAMPLER_DIM_1D && ctx->options->chip_class == GFX9) {
         derivs = bld.pseudo(aco_opcode::p_create_vector, bld.def(v4),
                             ddx, Operand(0u), ddy, Operand(0u));
      } else {
         derivs = bld.pseudo(aco_opcode::p_create_vector, bld.def(RegType::vgpr, ddx.size() + ddy.size()), ddx, ddy);
      }
      has_derivs = true;
   }

   if (instr->coord_components > 1 &&
       instr->sampler_dim == GLSL_SAMPLER_DIM_1D &&
       instr->is_array &&
       instr->op != nir_texop_txf)
      coords = apply_round_slice(ctx, coords, 1);

   if (instr->coord_components > 2 &&
      (instr->sampler_dim == GLSL_SAMPLER_DIM_2D ||
       instr->sampler_dim == GLSL_SAMPLER_DIM_MS ||
       instr->sampler_dim == GLSL_SAMPLER_DIM_SUBPASS ||
       instr->sampler_dim == GLSL_SAMPLER_DIM_SUBPASS_MS) &&
       instr->is_array &&
       instr->op != nir_texop_txf && instr->op != nir_texop_txf_ms)
      coords = apply_round_slice(ctx, coords, 2);

   if (ctx->options->chip_class == GFX9 &&
       instr->sampler_dim == GLSL_SAMPLER_DIM_1D &&
       instr->op != nir_texop_lod && instr->coord_components) {
      assert(coords.size() > 0 && coords.size() < 3);

      aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, coords.size() + 1, 1)};
      vec->operands[0] = Operand(emit_extract_vector(ctx, coords, 0, v1));
      vec->operands[1] = instr->op == nir_texop_txf ? Operand((uint32_t) 0) : Operand((uint32_t) 0x3f000000);
      if (coords.size() > 1)
         vec->operands[2] = Operand(emit_extract_vector(ctx, coords, 1, v1));
      coords = bld.tmp(RegType::vgpr, coords.size() + 1);
      vec->definitions[0] = Definition(coords);
      ctx->block->instructions.emplace_back(std::move(vec));
   }

   bool da = should_declare_array(ctx, instr->sampler_dim, instr->is_array);

   if (instr->op == nir_texop_samples_identical)
      resource = fmask_ptr;

   else if ((instr->sampler_dim == GLSL_SAMPLER_DIM_MS ||
             instr->sampler_dim == GLSL_SAMPLER_DIM_SUBPASS_MS) &&
            instr->op != nir_texop_txs) {
      assert(has_sample_index);
      Operand op(sample_index);
      if (sample_index_cv)
         op = Operand(sample_index_cv->u32);
      sample_index = adjust_sample_index_using_fmask(ctx, da, coords, op, fmask_ptr);
   }

   if (has_offset && (instr->op == nir_texop_txf || instr->op == nir_texop_txf_ms)) {
      Temp split_coords[coords.size()];
      emit_split_vector(ctx, coords, coords.size());
      for (unsigned i = 0; i < coords.size(); i++)
         split_coords[i] = emit_extract_vector(ctx, coords, i, v1);

      unsigned i = 0;
      for (; i < std::min(offset.size(), instr->coord_components); i++) {
         Temp off = emit_extract_vector(ctx, offset, i, v1);
         split_coords[i] = bld.vadd32(bld.def(v1), split_coords[i], off);
      }

      aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, coords.size(), 1)};
      for (unsigned i = 0; i < coords.size(); i++)
         vec->operands[i] = Operand(split_coords[i]);
      coords = bld.tmp(coords.regClass());
      vec->definitions[0] = Definition(coords);
      ctx->block->instructions.emplace_back(std::move(vec));

      has_offset = false;
   }

   /* Build tex instruction */
   unsigned dmask = nir_ssa_def_components_read(&instr->dest.ssa);
   unsigned dim = ctx->options->chip_class >= GFX10 && instr->sampler_dim != GLSL_SAMPLER_DIM_BUF
                  ? ac_get_sampler_dim(ctx->options->chip_class, instr->sampler_dim, instr->is_array)
                  : 0;
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   Temp tmp_dst = dst;

   /* gather4 selects the component by dmask and always returns vec4 */
   if (instr->op == nir_texop_tg4) {
      assert(instr->dest.ssa.num_components == 4);
      if (instr->is_shadow)
         dmask = 1;
      else
         dmask = 1 << instr->component;
      if (tg4_integer_cube_workaround || dst.type() == RegType::sgpr)
         tmp_dst = bld.tmp(v4);
   } else if (instr->op == nir_texop_samples_identical) {
      tmp_dst = bld.tmp(v1);
   } else if (util_bitcount(dmask) != instr->dest.ssa.num_components || dst.type() == RegType::sgpr) {
      tmp_dst = bld.tmp(RegClass(RegType::vgpr, util_bitcount(dmask)));
   }

   aco_ptr<MIMG_instruction> tex;
   if (instr->op == nir_texop_txs || instr->op == nir_texop_query_levels) {
      if (!has_lod)
         lod = bld.vop1(aco_opcode::v_mov_b32, bld.def(v1), Operand(0u));

      bool div_by_6 = instr->op == nir_texop_txs &&
                      instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE &&
                      instr->is_array &&
                      (dmask & (1 << 2));
      if (tmp_dst.id() == dst.id() && div_by_6)
         tmp_dst = bld.tmp(tmp_dst.regClass());

      tex.reset(create_instruction<MIMG_instruction>(aco_opcode::image_get_resinfo, Format::MIMG, 2, 1));
      tex->operands[0] = Operand(as_vgpr(ctx,lod));
      tex->operands[1] = Operand(resource);
      if (ctx->options->chip_class == GFX9 &&
          instr->op == nir_texop_txs &&
          instr->sampler_dim == GLSL_SAMPLER_DIM_1D &&
          instr->is_array) {
         tex->dmask = (dmask & 0x1) | ((dmask & 0x2) << 1);
      } else if (instr->op == nir_texop_query_levels) {
         tex->dmask = 1 << 3;
      } else {
         tex->dmask = dmask;
      }
      tex->da = da;
      tex->definitions[0] = Definition(tmp_dst);
      tex->dim = dim;
      tex->can_reorder = true;
      ctx->block->instructions.emplace_back(std::move(tex));

      if (div_by_6) {
         /* divide 3rd value by 6 by multiplying with magic number */
         emit_split_vector(ctx, tmp_dst, tmp_dst.size());
         Temp c = bld.copy(bld.def(s1), Operand((uint32_t) 0x2AAAAAAB));
         Temp by_6 = bld.vop3(aco_opcode::v_mul_hi_i32, bld.def(v1), emit_extract_vector(ctx, tmp_dst, 2, v1), c);
         assert(instr->dest.ssa.num_components == 3);
         Temp tmp = dst.type() == RegType::vgpr ? dst : bld.tmp(v3);
         tmp_dst = bld.pseudo(aco_opcode::p_create_vector, Definition(tmp),
                              emit_extract_vector(ctx, tmp_dst, 0, v1),
                              emit_extract_vector(ctx, tmp_dst, 1, v1),
                              by_6);

      }

      expand_vector(ctx, tmp_dst, dst, instr->dest.ssa.num_components, dmask);
      return;
   }

   Temp tg4_compare_cube_wa64 = Temp();

   if (tg4_integer_workarounds) {
      tex.reset(create_instruction<MIMG_instruction>(aco_opcode::image_get_resinfo, Format::MIMG, 2, 1));
      tex->operands[0] = bld.vop1(aco_opcode::v_mov_b32, bld.def(v1), Operand(0u));
      tex->operands[1] = Operand(resource);
      tex->dim = dim;
      tex->dmask = 0x3;
      tex->da = da;
      Temp size = bld.tmp(v2);
      tex->definitions[0] = Definition(size);
      tex->can_reorder = true;
      ctx->block->instructions.emplace_back(std::move(tex));
      emit_split_vector(ctx, size, size.size());

      Temp half_texel[2];
      for (unsigned i = 0; i < 2; i++) {
         half_texel[i] = emit_extract_vector(ctx, size, i, v1);
         half_texel[i] = bld.vop1(aco_opcode::v_cvt_f32_i32, bld.def(v1), half_texel[i]);
         half_texel[i] = bld.vop1(aco_opcode::v_rcp_iflag_f32, bld.def(v1), half_texel[i]);
         half_texel[i] = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), Operand(0xbf000000/*-0.5*/), half_texel[i]);
      }

      Temp orig_coords[2] = {
         emit_extract_vector(ctx, coords, 0, v1),
         emit_extract_vector(ctx, coords, 1, v1)};
      Temp new_coords[2] = {
         bld.vop2(aco_opcode::v_add_f32, bld.def(v1), orig_coords[0], half_texel[0]),
         bld.vop2(aco_opcode::v_add_f32, bld.def(v1), orig_coords[1], half_texel[1])
      };

      if (tg4_integer_cube_workaround) {
         // see comment in ac_nir_to_llvm.c's lower_gather4_integer()
         Temp desc[resource.size()];
         aco_ptr<Instruction> split{create_instruction<Pseudo_instruction>(aco_opcode::p_split_vector,
                                                                           Format::PSEUDO, 1, resource.size())};
         split->operands[0] = Operand(resource);
         for (unsigned i = 0; i < resource.size(); i++) {
            desc[i] = bld.tmp(s1);
            split->definitions[i] = Definition(desc[i]);
         }
         ctx->block->instructions.emplace_back(std::move(split));

         Temp dfmt = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc), desc[1], Operand(20u | (6u << 16)));
         Temp compare_cube_wa = bld.sopc(aco_opcode::s_cmp_eq_u32, bld.def(s1, scc), dfmt,
                                         Operand((uint32_t)V_008F14_IMG_DATA_FORMAT_8_8_8_8));

         Temp nfmt;
         if (stype == GLSL_TYPE_UINT) {
            nfmt = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1),
                            Operand((uint32_t)V_008F14_IMG_NUM_FORMAT_USCALED),
                            Operand((uint32_t)V_008F14_IMG_NUM_FORMAT_UINT),
                            bld.scc(compare_cube_wa));
         } else {
            nfmt = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1),
                            Operand((uint32_t)V_008F14_IMG_NUM_FORMAT_SSCALED),
                            Operand((uint32_t)V_008F14_IMG_NUM_FORMAT_SINT),
                            bld.scc(compare_cube_wa));
         }
         tg4_compare_cube_wa64 = as_divergent_bool(ctx, compare_cube_wa, true);
         nfmt = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), nfmt, Operand(26u));

         desc[1] = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), desc[1],
                            Operand((uint32_t)C_008F14_NUM_FORMAT));
         desc[1] = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), desc[1], nfmt);

         aco_ptr<Instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector,
                                                                         Format::PSEUDO, resource.size(), 1)};
         for (unsigned i = 0; i < resource.size(); i++)
            vec->operands[i] = Operand(desc[i]);
         resource = bld.tmp(resource.regClass());
         vec->definitions[0] = Definition(resource);
         ctx->block->instructions.emplace_back(std::move(vec));

         new_coords[0] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1),
                                  new_coords[0], orig_coords[0], tg4_compare_cube_wa64);
         new_coords[1] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1),
                                  new_coords[1], orig_coords[1], tg4_compare_cube_wa64);
      }

      if (coords.size() == 3) {
         coords = bld.pseudo(aco_opcode::p_create_vector, bld.def(v3),
                             new_coords[0], new_coords[1],
                             emit_extract_vector(ctx, coords, 2, v1));
      } else {
         assert(coords.size() == 2);
         coords = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2),
                             new_coords[0], new_coords[1]);
      }
   }

   if (!(has_ddx && has_ddy) && !has_lod && !level_zero &&
       instr->sampler_dim != GLSL_SAMPLER_DIM_MS &&
       instr->sampler_dim != GLSL_SAMPLER_DIM_SUBPASS_MS)
      coords = emit_wqm(ctx, coords, bld.tmp(coords.regClass()), true);

   std::vector<Operand> args;
   if (has_offset)
      args.emplace_back(Operand(offset));
   if (has_bias)
      args.emplace_back(Operand(bias));
   if (has_compare)
      args.emplace_back(Operand(compare));
   if (has_derivs)
      args.emplace_back(Operand(derivs));
   args.emplace_back(Operand(coords));
   if (has_sample_index)
      args.emplace_back(Operand(sample_index));
   if (has_lod)
      args.emplace_back(lod);

   Operand arg;
   if (args.size() > 1) {
      aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, args.size(), 1)};
      unsigned size = 0;
      for (unsigned i = 0; i < args.size(); i++) {
         size += args[i].size();
         vec->operands[i] = args[i];
      }
      RegClass rc = RegClass(RegType::vgpr, size);
      Temp tmp = bld.tmp(rc);
      vec->definitions[0] = Definition(tmp);
      ctx->block->instructions.emplace_back(std::move(vec));
      arg = Operand(tmp);
   } else {
      assert(args[0].isTemp());
      arg = Operand(as_vgpr(ctx, args[0].getTemp()));
   }

   if (instr->sampler_dim == GLSL_SAMPLER_DIM_BUF) {
      //FIXME: if (ctx->abi->gfx9_stride_size_workaround) return ac_build_buffer_load_format_gfx9_safe()

      assert(coords.size() == 1);
      unsigned last_bit = util_last_bit(nir_ssa_def_components_read(&instr->dest.ssa));
      aco_opcode op;
      switch (last_bit) {
      case 1:
         op = aco_opcode::buffer_load_format_x; break;
      case 2:
         op = aco_opcode::buffer_load_format_xy; break;
      case 3:
         op = aco_opcode::buffer_load_format_xyz; break;
      case 4:
         op = aco_opcode::buffer_load_format_xyzw; break;
      default:
         unreachable("Tex instruction loads more than 4 components.");
      }

      /* if the instruction return value matches exactly the nir dest ssa, we can use it directly */
      if (last_bit == instr->dest.ssa.num_components && dst.type() == RegType::vgpr)
         tmp_dst = dst;
      else
         tmp_dst = bld.tmp(RegType::vgpr, last_bit);

      aco_ptr<MUBUF_instruction> mubuf{create_instruction<MUBUF_instruction>(op, Format::MUBUF, 3, 1)};
      mubuf->operands[0] = Operand(coords);
      mubuf->operands[1] = Operand(resource);
      mubuf->operands[2] = Operand((uint32_t) 0);
      mubuf->definitions[0] = Definition(tmp_dst);
      mubuf->idxen = true;
      mubuf->can_reorder = true;
      ctx->block->instructions.emplace_back(std::move(mubuf));

      expand_vector(ctx, tmp_dst, dst, instr->dest.ssa.num_components, (1 << last_bit) - 1);
      return;
   }


   if (instr->op == nir_texop_txf ||
       instr->op == nir_texop_txf_ms ||
       instr->op == nir_texop_samples_identical) {
      aco_opcode op = level_zero || instr->sampler_dim == GLSL_SAMPLER_DIM_MS ? aco_opcode::image_load : aco_opcode::image_load_mip;
      tex.reset(create_instruction<MIMG_instruction>(op, Format::MIMG, 2, 1));
      tex->operands[0] = Operand(arg);
      tex->operands[1] = Operand(resource);
      tex->dim = dim;
      tex->dmask = dmask;
      tex->unrm = true;
      tex->da = da;
      tex->definitions[0] = Definition(tmp_dst);
      tex->can_reorder = true;
      ctx->block->instructions.emplace_back(std::move(tex));

      if (instr->op == nir_texop_samples_identical) {
         assert(dmask == 1 && dst.regClass() == v1);
         assert(dst.id() != tmp_dst.id());

         Temp tmp = bld.tmp(s2);
         bld.vopc(aco_opcode::v_cmp_eq_u32, Definition(tmp), Operand(0u), tmp_dst).def(0).setHint(vcc);
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), Operand(0u), Operand((uint32_t)-1), tmp);

      } else {
         expand_vector(ctx, tmp_dst, dst, instr->dest.ssa.num_components, dmask);
      }
      return;
   }

   // TODO: would be better to do this by adding offsets, but needs the opcodes ordered.
   aco_opcode opcode = aco_opcode::image_sample;
   if (has_offset) { /* image_sample_*_o */
      if (has_compare) {
         opcode = aco_opcode::image_sample_c_o;
         if (has_derivs)
            opcode = aco_opcode::image_sample_c_d_o;
         if (has_bias)
            opcode = aco_opcode::image_sample_c_b_o;
         if (level_zero)
            opcode = aco_opcode::image_sample_c_lz_o;
         if (has_lod)
            opcode = aco_opcode::image_sample_c_l_o;
      } else {
         opcode = aco_opcode::image_sample_o;
         if (has_derivs)
            opcode = aco_opcode::image_sample_d_o;
         if (has_bias)
            opcode = aco_opcode::image_sample_b_o;
         if (level_zero)
            opcode = aco_opcode::image_sample_lz_o;
         if (has_lod)
            opcode = aco_opcode::image_sample_l_o;
      }
   } else { /* no offset */
      if (has_compare) {
         opcode = aco_opcode::image_sample_c;
         if (has_derivs)
            opcode = aco_opcode::image_sample_c_d;
         if (has_bias)
            opcode = aco_opcode::image_sample_c_b;
         if (level_zero)
            opcode = aco_opcode::image_sample_c_lz;
         if (has_lod)
            opcode = aco_opcode::image_sample_c_l;
      } else {
         opcode = aco_opcode::image_sample;
         if (has_derivs)
            opcode = aco_opcode::image_sample_d;
         if (has_bias)
            opcode = aco_opcode::image_sample_b;
         if (level_zero)
            opcode = aco_opcode::image_sample_lz;
         if (has_lod)
            opcode = aco_opcode::image_sample_l;
      }
   }

   if (instr->op == nir_texop_tg4) {
      if (has_offset) {
         opcode = aco_opcode::image_gather4_lz_o;
         if (has_compare)
            opcode = aco_opcode::image_gather4_c_lz_o;
      } else {
         opcode = aco_opcode::image_gather4_lz;
         if (has_compare)
            opcode = aco_opcode::image_gather4_c_lz;
      }
   } else if (instr->op == nir_texop_lod) {
      opcode = aco_opcode::image_get_lod;
   }

   tex.reset(create_instruction<MIMG_instruction>(opcode, Format::MIMG, 3, 1));
   tex->operands[0] = arg;
   tex->operands[1] = Operand(resource);
   tex->operands[2] = Operand(sampler);
   tex->dim = dim;
   tex->dmask = dmask;
   tex->da = da;
   tex->definitions[0] = Definition(tmp_dst);
   tex->can_reorder = true;
   ctx->block->instructions.emplace_back(std::move(tex));

   if (tg4_integer_cube_workaround) {
      assert(tmp_dst.id() != dst.id());
      assert(tmp_dst.size() == dst.size() && dst.size() == 4);

      emit_split_vector(ctx, tmp_dst, tmp_dst.size());
      Temp val[4];
      for (unsigned i = 0; i < dst.size(); i++) {
         val[i] = emit_extract_vector(ctx, tmp_dst, i, v1);
         Temp cvt_val;
         if (stype == GLSL_TYPE_UINT)
            cvt_val = bld.vop1(aco_opcode::v_cvt_u32_f32, bld.def(v1), val[i]);
         else
            cvt_val = bld.vop1(aco_opcode::v_cvt_i32_f32, bld.def(v1), val[i]);
         val[i] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), val[i], cvt_val, tg4_compare_cube_wa64);
      }
      Temp tmp = dst.regClass() == v4 ? dst : bld.tmp(v4);
      tmp_dst = bld.pseudo(aco_opcode::p_create_vector, Definition(tmp),
                           val[0], val[1], val[2], val[3]);
   }
   unsigned mask = instr->op == nir_texop_tg4 ? 0xF : dmask;
   expand_vector(ctx, tmp_dst, dst, instr->dest.ssa.num_components, mask);

}


Operand get_phi_operand(isel_context *ctx, nir_ssa_def *ssa)
{
   Temp tmp = get_ssa_temp(ctx, ssa);
   if (ssa->parent_instr->type == nir_instr_type_ssa_undef)
      return Operand(tmp.regClass());
   else
      return Operand(tmp);
}

void visit_phi(isel_context *ctx, nir_phi_instr *instr)
{
   aco_ptr<Pseudo_instruction> phi;
   unsigned num_src = exec_list_length(&instr->srcs);
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   aco_opcode opcode = !dst.is_linear() || ctx->divergent_vals[instr->dest.ssa.index] ? aco_opcode::p_phi : aco_opcode::p_linear_phi;

   std::map<unsigned, nir_ssa_def*> phi_src;
   bool all_undef = true;
   nir_foreach_phi_src(src, instr) {
      phi_src[src->pred->index] = src->src.ssa;
      if (src->src.ssa->parent_instr->type != nir_instr_type_ssa_undef)
         all_undef = false;
   }
   if (all_undef) {
      Builder bld(ctx->program, ctx->block);
      if (dst.regClass() == s1) {
         bld.sop1(aco_opcode::s_mov_b32, Definition(dst), Operand(0u));
      } else if (dst.regClass() == v1) {
         bld.vop1(aco_opcode::v_mov_b32, Definition(dst), Operand(0u));
      } else {
         aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, dst.size(), 1)};
         for (unsigned i = 0; i < dst.size(); i++)
            vec->operands[i] = Operand(0u);
         vec->definitions[0] = Definition(dst);
         ctx->block->instructions.emplace_back(std::move(vec));
      }
      return;
   }

   /* try to scalarize vector phis */
   if (dst.size() > 1) {
      // TODO: scalarize linear phis on divergent ifs
      bool can_scalarize = (opcode == aco_opcode::p_phi || !(ctx->block->kind & block_kind_merge));
      std::array<Temp, 4> new_vec;
      for (std::pair<const unsigned, nir_ssa_def*>& pair : phi_src) {
         Operand src = get_phi_operand(ctx, pair.second);
         if (src.isTemp() && ctx->allocated_vec.find(src.tempId()) == ctx->allocated_vec.end()) {
            can_scalarize = false;
            break;
         }
      }
      if (can_scalarize) {
         unsigned num_components = instr->dest.ssa.num_components;
         assert(dst.size() % num_components == 0);
         RegClass rc = RegClass(dst.type(), dst.size() / num_components);

         aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, num_components, 1)};
         for (unsigned k = 0; k < num_components; k++) {
            phi.reset(create_instruction<Pseudo_instruction>(opcode, Format::PSEUDO, num_src, 1));
            std::map<unsigned, nir_ssa_def*>::iterator it = phi_src.begin();
            for (unsigned i = 0; i < num_src; i++) {
               Operand src = get_phi_operand(ctx, it->second);
               phi->operands[i] = src.isTemp() ? Operand(ctx->allocated_vec[src.tempId()][k]) : Operand(rc);
               ++it;
            }
            Temp phi_dst = {ctx->program->allocateId(), rc};
            phi->definitions[0] = Definition(phi_dst);
            ctx->block->instructions.emplace(ctx->block->instructions.begin(), std::move(phi));
            new_vec[k] = phi_dst;
            vec->operands[k] = Operand(phi_dst);
         }
         vec->definitions[0] = Definition(dst);
         ctx->block->instructions.emplace_back(std::move(vec));
         ctx->allocated_vec.emplace(dst.id(), new_vec);
         return;
      }
   }

   unsigned extra_src = 0;
   if (opcode == aco_opcode::p_linear_phi && (ctx->block->kind & block_kind_loop_exit) &&
       ctx->program->blocks[ctx->block->index-2].kind & block_kind_continue_or_break) {
      extra_src++;
   }

   phi.reset(create_instruction<Pseudo_instruction>(opcode, Format::PSEUDO, num_src + extra_src, 1));

   /* if we have a linear phi on a divergent if, we know that one src is undef */
   if (opcode == aco_opcode::p_linear_phi && ctx->block->kind & block_kind_merge) {
      assert(extra_src == 0);
      Block* block;
      /* we place the phi either in the invert-block or in the current block */
      if (phi_src.begin()->second->parent_instr->type != nir_instr_type_ssa_undef) {
         assert((++phi_src.begin())->second->parent_instr->type == nir_instr_type_ssa_undef);
         Block& linear_else = ctx->program->blocks[ctx->block->linear_preds[1]];
         block = &ctx->program->blocks[linear_else.linear_preds[0]];
         assert(block->kind & block_kind_invert);
         phi->operands[0] = get_phi_operand(ctx, phi_src.begin()->second);
      } else {
         assert((++phi_src.begin())->second->parent_instr->type != nir_instr_type_ssa_undef);
         block = ctx->block;
         phi->operands[0] = get_phi_operand(ctx, (++phi_src.begin())->second);
      }
      phi->operands[1] = Operand(dst.regClass());
      phi->definitions[0] = Definition(dst);
      block->instructions.emplace(block->instructions.begin(), std::move(phi));
      return;
   }

   std::map<unsigned, nir_ssa_def*>::iterator it = phi_src.begin();
   for (unsigned i = 0; i < num_src; i++) {
      phi->operands[i] = get_phi_operand(ctx, it->second);
      ++it;
   }
   for (unsigned i = 0; i < extra_src; i++)
      phi->operands[num_src + i] = Operand(dst.regClass());
   phi->definitions[0] = Definition(dst);
   ctx->block->instructions.emplace(ctx->block->instructions.begin(), std::move(phi));
}


void visit_undef(isel_context *ctx, nir_ssa_undef_instr *instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->def);

   assert(dst.type() == RegType::sgpr);

   if (dst.size() == 1) {
      Builder(ctx->program, ctx->block).copy(Definition(dst), Operand(0u));
   } else {
      aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, dst.size(), 1)};
      for (unsigned i = 0; i < dst.size(); i++)
         vec->operands[i] = Operand(0u);
      vec->definitions[0] = Definition(dst);
      ctx->block->instructions.emplace_back(std::move(vec));
   }
}

void visit_jump(isel_context *ctx, nir_jump_instr *instr)
{
   Builder bld(ctx->program, ctx->block);
   Block *logical_target;
   append_logical_end(ctx->block);
   unsigned idx = ctx->block->index;

   switch (instr->type) {
   case nir_jump_break:
      logical_target = ctx->cf_info.parent_loop.exit;
      add_logical_edge(idx, logical_target);
      ctx->block->kind |= block_kind_break;

      if (!ctx->cf_info.parent_if.is_divergent &&
          !ctx->cf_info.parent_loop.has_divergent_continue) {
         /* uniform break - directly jump out of the loop */
         ctx->block->kind |= block_kind_uniform;
         ctx->cf_info.has_branch = true;
         bld.branch(aco_opcode::p_branch);
         add_linear_edge(idx, logical_target);
         return;
      }
      ctx->cf_info.parent_loop.has_divergent_branch = true;
      break;
   case nir_jump_continue:
      logical_target = &ctx->program->blocks[ctx->cf_info.parent_loop.header_idx];
      add_logical_edge(idx, logical_target);
      ctx->block->kind |= block_kind_continue;

      if (ctx->cf_info.parent_if.is_divergent) {
         /* for potential uniform breaks after this continue,
            we must ensure that they are handled correctly */
         ctx->cf_info.parent_loop.has_divergent_continue = true;
         ctx->cf_info.parent_loop.has_divergent_branch = true;
      } else {
         /* uniform continue - directly jump to the loop header */
         ctx->block->kind |= block_kind_uniform;
         ctx->cf_info.has_branch = true;
         bld.branch(aco_opcode::p_branch);
         add_linear_edge(idx, logical_target);
         return;
      }
      break;
   default:
      fprintf(stderr, "Unknown NIR jump instr: ");
      nir_print_instr(&instr->instr, stderr);
      fprintf(stderr, "\n");
      abort();
   }

   /* remove critical edges from linear CFG */
   bld.branch(aco_opcode::p_branch);
   Block* break_block = ctx->program->create_and_insert_block();
   break_block->loop_nest_depth = ctx->cf_info.loop_nest_depth;
   break_block->kind |= block_kind_uniform;
   add_linear_edge(idx, break_block);
   /* the loop_header pointer might be invalidated by this point */
   if (instr->type == nir_jump_continue)
      logical_target = &ctx->program->blocks[ctx->cf_info.parent_loop.header_idx];
   add_linear_edge(break_block->index, logical_target);
   bld.reset(break_block);
   bld.branch(aco_opcode::p_branch);

   Block* continue_block = ctx->program->create_and_insert_block();
   continue_block->loop_nest_depth = ctx->cf_info.loop_nest_depth;
   add_linear_edge(idx, continue_block);
   append_logical_start(continue_block);
   ctx->block = continue_block;
   return;
}

void visit_block(isel_context *ctx, nir_block *block)
{
   nir_foreach_instr(instr, block) {
      switch (instr->type) {
      case nir_instr_type_alu:
         visit_alu_instr(ctx, nir_instr_as_alu(instr));
         break;
      case nir_instr_type_load_const:
         visit_load_const(ctx, nir_instr_as_load_const(instr));
         break;
      case nir_instr_type_intrinsic:
         visit_intrinsic(ctx, nir_instr_as_intrinsic(instr));
         break;
      case nir_instr_type_tex:
         visit_tex(ctx, nir_instr_as_tex(instr));
         break;
      case nir_instr_type_phi:
         visit_phi(ctx, nir_instr_as_phi(instr));
         break;
      case nir_instr_type_ssa_undef:
         visit_undef(ctx, nir_instr_as_ssa_undef(instr));
         break;
      case nir_instr_type_deref:
         break;
      case nir_instr_type_jump:
         visit_jump(ctx, nir_instr_as_jump(instr));
         break;
      default:
         fprintf(stderr, "Unknown NIR instr type: ");
         nir_print_instr(instr, stderr);
         fprintf(stderr, "\n");
         //abort();
      }
   }
}



static void visit_loop(isel_context *ctx, nir_loop *loop)
{
   append_logical_end(ctx->block);
   ctx->block->kind |= block_kind_loop_preheader | block_kind_uniform;
   Builder bld(ctx->program, ctx->block);
   bld.branch(aco_opcode::p_branch);
   unsigned loop_preheader_idx = ctx->block->index;

   Block loop_exit = Block();
   loop_exit.loop_nest_depth = ctx->cf_info.loop_nest_depth;
   loop_exit.kind |= (block_kind_loop_exit | (ctx->block->kind & block_kind_top_level));

   Block* loop_header = ctx->program->create_and_insert_block();
   loop_header->loop_nest_depth = ctx->cf_info.loop_nest_depth + 1;
   loop_header->kind |= block_kind_loop_header;
   add_edge(loop_preheader_idx, loop_header);
   ctx->block = loop_header;

   /* emit loop body */
   unsigned loop_header_idx = loop_header->index;
   loop_info_RAII loop_raii(ctx, loop_header_idx, &loop_exit);
   append_logical_start(ctx->block);
   visit_cf_list(ctx, &loop->body);

   //TODO: what if a loop ends with a unconditional or uniformly branched continue and this branch is never taken?
   if (!ctx->cf_info.has_branch) {
      append_logical_end(ctx->block);
      if (ctx->cf_info.exec_potentially_empty) {
         /* Discards can result in code running with an empty exec mask.
          * This would result in divergent breaks not ever being taken. As a
          * workaround, break the loop when the loop mask is empty instead of
          * always continuing. */
         ctx->block->kind |= (block_kind_continue_or_break | block_kind_uniform);
         unsigned block_idx = ctx->block->index;

         /* create helper blocks to avoid critical edges */
         Block *break_block = ctx->program->create_and_insert_block();
         break_block->loop_nest_depth = ctx->cf_info.loop_nest_depth;
         break_block->kind = block_kind_uniform;
         bld.reset(break_block);
         bld.branch(aco_opcode::p_branch);
         add_linear_edge(block_idx, break_block);
         add_linear_edge(break_block->index, &loop_exit);

         Block *continue_block = ctx->program->create_and_insert_block();
         continue_block->loop_nest_depth = ctx->cf_info.loop_nest_depth;
         continue_block->kind = block_kind_uniform;
         bld.reset(continue_block);
         bld.branch(aco_opcode::p_branch);
         add_linear_edge(block_idx, continue_block);
         add_linear_edge(continue_block->index, &ctx->program->blocks[loop_header_idx]);

         add_logical_edge(block_idx, &ctx->program->blocks[loop_header_idx]);
         ctx->block = &ctx->program->blocks[block_idx];
      } else {
         ctx->block->kind |= (block_kind_continue | block_kind_uniform);
         if (!ctx->cf_info.parent_loop.has_divergent_branch)
            add_edge(ctx->block->index, &ctx->program->blocks[loop_header_idx]);
         else
            add_linear_edge(ctx->block->index, &ctx->program->blocks[loop_header_idx]);
      }

      bld.reset(ctx->block);
      bld.branch(aco_opcode::p_branch);
   }

   /* fixup phis in loop header from unreachable blocks */
   if (ctx->cf_info.has_branch || ctx->cf_info.parent_loop.has_divergent_branch) {
      bool linear = ctx->cf_info.has_branch;
      bool logical = ctx->cf_info.has_branch || ctx->cf_info.parent_loop.has_divergent_branch;
      for (aco_ptr<Instruction>& instr : ctx->program->blocks[loop_header_idx].instructions) {
         if ((logical && instr->opcode == aco_opcode::p_phi) ||
             (linear && instr->opcode == aco_opcode::p_linear_phi)) {
            /* the last operand should be the one that needs to be removed */
            instr->operands.pop_back();
         } else if (!is_phi(instr)) {
            break;
         }
      }
   }

   ctx->cf_info.has_branch = false;

   // TODO: if the loop has not a single exit, we must add one °°
   /* emit loop successor block */
   ctx->block = ctx->program->insert_block(std::move(loop_exit));
   append_logical_start(ctx->block);

   #if 0
   // TODO: check if it is beneficial to not branch on continues
   /* trim linear phis in loop header */
   for (auto&& instr : loop_entry->instructions) {
      if (instr->opcode == aco_opcode::p_linear_phi) {
         aco_ptr<Pseudo_instruction> new_phi{create_instruction<Pseudo_instruction>(aco_opcode::p_linear_phi, Format::PSEUDO, loop_entry->linear_predecessors.size(), 1)};
         new_phi->definitions[0] = instr->definitions[0];
         for (unsigned i = 0; i < new_phi->operands.size(); i++)
            new_phi->operands[i] = instr->operands[i];
         /* check that the remaining operands are all the same */
         for (unsigned i = new_phi->operands.size(); i < instr->operands.size(); i++)
            assert(instr->operands[i].tempId() == instr->operands.back().tempId());
         instr.swap(new_phi);
      } else if (instr->opcode == aco_opcode::p_phi) {
         continue;
      } else {
         break;
      }
   }
   #endif
}

static void begin_divergent_if_then(isel_context *ctx, if_context *ic, Temp cond)
{
   ic->cond = cond;

   append_logical_end(ctx->block);
   ctx->block->kind |= block_kind_branch;

   /* branch to linear then block */
   assert(cond.regClass() == s2);
   aco_ptr<Pseudo_branch_instruction> branch;
   branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_cbranch_z, Format::PSEUDO_BRANCH, 1, 0));
   branch->operands[0] = Operand(cond);
   ctx->block->instructions.push_back(std::move(branch));

   ic->BB_if_idx = ctx->block->index;
   ic->BB_invert = Block();
   ic->BB_invert.loop_nest_depth = ctx->cf_info.loop_nest_depth;
   /* Invert blocks are intentionally not marked as top level because they
    * are not part of the logical cfg. */
   ic->BB_invert.kind |= block_kind_invert;
   ic->BB_endif = Block();
   ic->BB_endif.loop_nest_depth = ctx->cf_info.loop_nest_depth;
   ic->BB_endif.kind |= (block_kind_merge | (ctx->block->kind & block_kind_top_level));

   ic->exec_potentially_empty_old = ctx->cf_info.exec_potentially_empty;
   ic->divergent_old = ctx->cf_info.parent_if.is_divergent;
   ctx->cf_info.parent_if.is_divergent = true;
   ctx->cf_info.exec_potentially_empty = false; /* divergent branches use cbranch_execz */

   /** emit logical then block */
   Block* BB_then_logical = ctx->program->create_and_insert_block();
   BB_then_logical->loop_nest_depth = ctx->cf_info.loop_nest_depth;
   add_edge(ic->BB_if_idx, BB_then_logical);
   ctx->block = BB_then_logical;
   append_logical_start(BB_then_logical);
}

static void begin_divergent_if_else(isel_context *ctx, if_context *ic)
{
   Block *BB_then_logical = ctx->block;
   append_logical_end(BB_then_logical);
    /* branch from logical then block to invert block */
   aco_ptr<Pseudo_branch_instruction> branch;
   branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
   BB_then_logical->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_then_logical->index, &ic->BB_invert);
   if (!ctx->cf_info.parent_loop.has_divergent_branch)
      add_logical_edge(BB_then_logical->index, &ic->BB_endif);
   BB_then_logical->kind |= block_kind_uniform;
   assert(!ctx->cf_info.has_branch);
   ic->then_branch_divergent = ctx->cf_info.parent_loop.has_divergent_branch;
   ctx->cf_info.parent_loop.has_divergent_branch = false;

   /** emit linear then block */
   Block* BB_then_linear = ctx->program->create_and_insert_block();
   BB_then_linear->loop_nest_depth = ctx->cf_info.loop_nest_depth;
   BB_then_linear->kind |= block_kind_uniform;
   add_linear_edge(ic->BB_if_idx, BB_then_linear);
   /* branch from linear then block to invert block */
   branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
   BB_then_linear->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_then_linear->index, &ic->BB_invert);

   /** emit invert merge block */
   ctx->block = ctx->program->insert_block(std::move(ic->BB_invert));
   ic->invert_idx = ctx->block->index;

   /* branch to linear else block (skip else) */
   branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_cbranch_nz, Format::PSEUDO_BRANCH, 1, 0));
   branch->operands[0] = Operand(ic->cond);
   ctx->block->instructions.push_back(std::move(branch));

   ic->exec_potentially_empty_old |= ctx->cf_info.exec_potentially_empty;
   ctx->cf_info.exec_potentially_empty = false; /* divergent branches use cbranch_execz */

   /** emit logical else block */
   Block* BB_else_logical = ctx->program->create_and_insert_block();
   BB_else_logical->loop_nest_depth = ctx->cf_info.loop_nest_depth;
   add_logical_edge(ic->BB_if_idx, BB_else_logical);
   add_linear_edge(ic->invert_idx, BB_else_logical);
   ctx->block = BB_else_logical;
   append_logical_start(BB_else_logical);
}

static void end_divergent_if(isel_context *ctx, if_context *ic)
{
   Block *BB_else_logical = ctx->block;
   append_logical_end(BB_else_logical);

   /* branch from logical else block to endif block */
   aco_ptr<Pseudo_branch_instruction> branch;
   branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
   BB_else_logical->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_else_logical->index, &ic->BB_endif);
   if (!ctx->cf_info.parent_loop.has_divergent_branch)
      add_logical_edge(BB_else_logical->index, &ic->BB_endif);
   BB_else_logical->kind |= block_kind_uniform;

   assert(!ctx->cf_info.has_branch);
   ctx->cf_info.parent_loop.has_divergent_branch &= ic->then_branch_divergent;


   /** emit linear else block */
   Block* BB_else_linear = ctx->program->create_and_insert_block();
   BB_else_linear->loop_nest_depth = ctx->cf_info.loop_nest_depth;
   BB_else_linear->kind |= block_kind_uniform;
   add_linear_edge(ic->invert_idx, BB_else_linear);

   /* branch from linear else block to endif block */
   branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
   BB_else_linear->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_else_linear->index, &ic->BB_endif);


   /** emit endif merge block */
   ctx->block = ctx->program->insert_block(std::move(ic->BB_endif));
   append_logical_start(ctx->block);


   ctx->cf_info.parent_if.is_divergent = ic->divergent_old;
   ctx->cf_info.exec_potentially_empty |= ic->exec_potentially_empty_old;
   /* uniform control flow never has an empty exec-mask */
   if (!ctx->cf_info.loop_nest_depth && !ctx->cf_info.parent_if.is_divergent)
      ctx->cf_info.exec_potentially_empty = false;
}

static void visit_if(isel_context *ctx, nir_if *if_stmt)
{
   Temp cond = get_ssa_temp(ctx, if_stmt->condition.ssa);
   Builder bld(ctx->program, ctx->block);
   aco_ptr<Pseudo_branch_instruction> branch;

   if (!ctx->divergent_vals[if_stmt->condition.ssa->index]) { /* uniform condition */
      /**
       * Uniform conditionals are represented in the following way*) :
       *
       * The linear and logical CFG:
       *                        BB_IF
       *                        /    \
       *       BB_THEN (logical)      BB_ELSE (logical)
       *                        \    /
       *                        BB_ENDIF
       *
       * *) Exceptions may be due to break and continue statements within loops
       *    If a break/continue happens within uniform control flow, it branches
       *    to the loop exit/entry block. Otherwise, it branches to the next
       *    merge block.
       **/
      append_logical_end(ctx->block);
      ctx->block->kind |= block_kind_uniform;

      /* emit branch */
      if (cond.regClass() == s2) {
         // TODO: in a post-RA optimizer, we could check if the condition is in VCC and omit this instruction
         cond = as_uniform_bool(ctx, cond);
      }
      branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_cbranch_z, Format::PSEUDO_BRANCH, 1, 0));
      branch->operands[0] = Operand(cond);
      branch->operands[0].setFixed(scc);
      ctx->block->instructions.emplace_back(std::move(branch));

      unsigned BB_if_idx = ctx->block->index;
      Block BB_endif = Block();
      BB_endif.loop_nest_depth = ctx->cf_info.loop_nest_depth;
      BB_endif.kind |= ctx->block->kind & block_kind_top_level;

      /** emit then block */
      Block* BB_then = ctx->program->create_and_insert_block();
      BB_then->loop_nest_depth = ctx->cf_info.loop_nest_depth;
      add_edge(BB_if_idx, BB_then);
      append_logical_start(BB_then);
      ctx->block = BB_then;
      visit_cf_list(ctx, &if_stmt->then_list);
      BB_then = ctx->block;
      bool then_branch = ctx->cf_info.has_branch;
      bool then_branch_divergent = ctx->cf_info.parent_loop.has_divergent_branch;

      if (!then_branch) {
         append_logical_end(BB_then);
         /* branch from then block to endif block */
         branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
         BB_then->instructions.emplace_back(std::move(branch));
         add_linear_edge(BB_then->index, &BB_endif);
         if (!then_branch_divergent)
            add_logical_edge(BB_then->index, &BB_endif);
         BB_then->kind |= block_kind_uniform;
      }

      ctx->cf_info.has_branch = false;
      ctx->cf_info.parent_loop.has_divergent_branch = false;

      /** emit else block */
      Block* BB_else = ctx->program->create_and_insert_block();
      BB_else->loop_nest_depth = ctx->cf_info.loop_nest_depth;
      add_edge(BB_if_idx, BB_else);
      append_logical_start(BB_else);
      ctx->block = BB_else;
      visit_cf_list(ctx, &if_stmt->else_list);
      BB_else = ctx->block;

      if (!ctx->cf_info.has_branch) {
         append_logical_end(BB_else);
         /* branch from then block to endif block */
         branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_branch, Format::PSEUDO_BRANCH, 0, 0));
         BB_else->instructions.emplace_back(std::move(branch));
         add_linear_edge(BB_else->index, &BB_endif);
         if (!ctx->cf_info.parent_loop.has_divergent_branch)
            add_logical_edge(BB_else->index, &BB_endif);
         BB_else->kind |= block_kind_uniform;
      }

      ctx->cf_info.has_branch &= then_branch;
      ctx->cf_info.parent_loop.has_divergent_branch &= then_branch_divergent;

      /** emit endif merge block */
      if (!ctx->cf_info.has_branch) {
         ctx->block = ctx->program->insert_block(std::move(BB_endif));
         append_logical_start(ctx->block);
      }
   } else { /* non-uniform condition */
      /**
       * To maintain a logical and linear CFG without critical edges,
       * non-uniform conditionals are represented in the following way*) :
       *
       * The linear CFG:
       *                        BB_IF
       *                        /    \
       *       BB_THEN (logical)      BB_THEN (linear)
       *                        \    /
       *                        BB_INVERT (linear)
       *                        /    \
       *       BB_ELSE (logical)      BB_ELSE (linear)
       *                        \    /
       *                        BB_ENDIF
       *
       * The logical CFG:
       *                        BB_IF
       *                        /    \
       *       BB_THEN (logical)      BB_ELSE (logical)
       *                        \    /
       *                        BB_ENDIF
       *
       * *) Exceptions may be due to break and continue statements within loops
       **/

      if_context ic;

      begin_divergent_if_then(ctx, &ic, cond);
      visit_cf_list(ctx, &if_stmt->then_list);

      begin_divergent_if_else(ctx, &ic);
      visit_cf_list(ctx, &if_stmt->else_list);

      end_divergent_if(ctx, &ic);
   }
}

static void visit_cf_list(isel_context *ctx,
                          struct exec_list *list)
{
   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block:
         visit_block(ctx, nir_cf_node_as_block(node));
         break;
      case nir_cf_node_if:
         visit_if(ctx, nir_cf_node_as_if(node));
         break;
      case nir_cf_node_loop:
         visit_loop(ctx, nir_cf_node_as_loop(node));
         break;
      default:
         unreachable("unimplemented cf list type");
      }
   }
}

static void export_vs_varying(isel_context *ctx, int slot, bool is_pos, int *next_pos)
{
   int offset = ctx->program->info->vs.outinfo.vs_output_param_offset[slot];
   uint64_t mask = ctx->vs_output.mask[slot];
   if (!is_pos && !mask)
      return;
   if (!is_pos && offset == AC_EXP_PARAM_UNDEFINED)
      return;
   aco_ptr<Export_instruction> exp{create_instruction<Export_instruction>(aco_opcode::exp, Format::EXP, 4, 0)};
   exp->enabled_mask = mask;
   for (unsigned i = 0; i < 4; ++i) {
      if (mask & (1 << i))
         exp->operands[i] = Operand(ctx->vs_output.outputs[slot][i]);
      else
         exp->operands[i] = Operand(v1);
   }
   exp->valid_mask = false;
   exp->done = false;
   exp->compressed = false;
   if (is_pos)
      exp->dest = V_008DFC_SQ_EXP_POS + (*next_pos)++;
   else
      exp->dest = V_008DFC_SQ_EXP_PARAM + offset;
   ctx->block->instructions.emplace_back(std::move(exp));
}

static void export_vs_psiz_layer_viewport(isel_context *ctx, int *next_pos)
{
   aco_ptr<Export_instruction> exp{create_instruction<Export_instruction>(aco_opcode::exp, Format::EXP, 4, 0)};
   exp->enabled_mask = 0;
   for (unsigned i = 0; i < 4; ++i)
      exp->operands[i] = Operand(v1);
   if (ctx->vs_output.mask[VARYING_SLOT_PSIZ]) {
      exp->operands[0] = Operand(ctx->vs_output.outputs[VARYING_SLOT_PSIZ][0]);
      exp->enabled_mask |= 0x1;
   }
   if (ctx->vs_output.mask[VARYING_SLOT_LAYER]) {
      exp->operands[2] = Operand(ctx->vs_output.outputs[VARYING_SLOT_LAYER][0]);
      exp->enabled_mask |= 0x4;
   }
   if (ctx->vs_output.mask[VARYING_SLOT_VIEWPORT]) {
      if (ctx->options->chip_class < GFX9) {
         exp->operands[3] = Operand(ctx->vs_output.outputs[VARYING_SLOT_VIEWPORT][0]);
         exp->enabled_mask |= 0x8;
      } else {
         Builder bld(ctx->program, ctx->block);

         Temp out = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand(16u),
                             Operand(ctx->vs_output.outputs[VARYING_SLOT_VIEWPORT][0]));
         if (exp->operands[2].isTemp())
            out = bld.vop2(aco_opcode::v_or_b32, bld.def(v1), Operand(out), exp->operands[2]);

         exp->operands[2] = Operand(out);
         exp->enabled_mask |= 0x4;
      }
   }
   exp->valid_mask = false;
   exp->done = false;
   exp->compressed = false;
   exp->dest = V_008DFC_SQ_EXP_POS + (*next_pos)++;
   ctx->block->instructions.emplace_back(std::move(exp));
}

static void create_vs_exports(isel_context *ctx)
{
   radv_vs_output_info *outinfo = &ctx->program->info->vs.outinfo;

   if (outinfo->export_prim_id) {
      ctx->vs_output.mask[VARYING_SLOT_PRIMITIVE_ID] |= 0x1;
      ctx->vs_output.outputs[VARYING_SLOT_PRIMITIVE_ID][0] = ctx->vs_prim_id;
   }

   if (ctx->options->key.has_multiview_view_index) {
      ctx->vs_output.mask[VARYING_SLOT_LAYER] |= 0x1;
      ctx->vs_output.outputs[VARYING_SLOT_LAYER][0] = as_vgpr(ctx, ctx->view_index);
   }

   /* the order these position exports are created is important */
   int next_pos = 0;
   export_vs_varying(ctx, VARYING_SLOT_POS, true, &next_pos);
   if (outinfo->writes_pointsize || outinfo->writes_layer || outinfo->writes_viewport_index) {
      export_vs_psiz_layer_viewport(ctx, &next_pos);
   }
   if (ctx->num_clip_distances + ctx->num_cull_distances > 0)
      export_vs_varying(ctx, VARYING_SLOT_CLIP_DIST0, true, &next_pos);
   if (ctx->num_clip_distances + ctx->num_cull_distances > 4)
      export_vs_varying(ctx, VARYING_SLOT_CLIP_DIST1, true, &next_pos);

   if (ctx->options->key.vs_common_out.export_clip_dists) {
      if (ctx->num_clip_distances + ctx->num_cull_distances > 0)
         export_vs_varying(ctx, VARYING_SLOT_CLIP_DIST0, false, &next_pos);
      if (ctx->num_clip_distances + ctx->num_cull_distances > 4)
         export_vs_varying(ctx, VARYING_SLOT_CLIP_DIST1, false, &next_pos);
   }

   for (unsigned i = 0; i <= VARYING_SLOT_VAR31; ++i) {
      if (i < VARYING_SLOT_VAR0 && i != VARYING_SLOT_LAYER &&
          i != VARYING_SLOT_PRIMITIVE_ID)
         continue;

      export_vs_varying(ctx, i, false, NULL);
   }
}

static void emit_stream_output(isel_context *ctx,
                               Temp const *so_buffers,
                               Temp const *so_write_offset,
                               const struct radv_stream_output *output)
{
   unsigned num_comps = util_bitcount(output->component_mask);
   unsigned loc = output->location;
   unsigned buf = output->buffer;
   unsigned offset = output->offset;

   assert(num_comps && num_comps <= 4);
   if (!num_comps || num_comps > 4)
      return;

   unsigned start = ffs(output->component_mask) - 1;

   Temp out[4];
   bool all_undef = true;
   assert(ctx->stage == vertex_vs);
   for (unsigned i = 0; i < num_comps; i++) {
      out[i] = ctx->vs_output.outputs[loc][start + i];
      all_undef = all_undef && !out[i].id();
   }
   if (all_undef)
      return;

   Temp write_data = {ctx->program->allocateId(), RegClass(RegType::vgpr, num_comps)};
   aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, num_comps, 1)};
   for (unsigned i = 0; i < num_comps; ++i)
      vec->operands[i] = (ctx->vs_output.mask[loc] & 1 << i) ? Operand(out[i]) : Operand(0u);
   vec->definitions[0] = Definition(write_data);
   ctx->block->instructions.emplace_back(std::move(vec));

   aco_opcode opcode;
   switch (num_comps) {
   case 1:
      opcode = aco_opcode::buffer_store_dword;
      break;
   case 2:
      opcode = aco_opcode::buffer_store_dwordx2;
      break;
   case 3:
      opcode = aco_opcode::buffer_store_dwordx3;
      break;
   case 4:
      opcode = aco_opcode::buffer_store_dwordx4;
      break;
   }

   aco_ptr<MUBUF_instruction> store{create_instruction<MUBUF_instruction>(opcode, Format::MUBUF, 4, 0)};
   store->operands[0] = Operand(so_write_offset[buf]);
   store->operands[1] = Operand(so_buffers[buf]);
   store->operands[2] = Operand((uint32_t) 0);
   store->operands[3] = Operand(write_data);
   if (offset > 4095) {
      /* Don't think this can happen in RADV, but maybe GL? It's easy to do this anyway. */
      Builder bld(ctx->program, ctx->block);
      store->operands[0] = bld.vadd32(bld.def(v1), Operand(offset), Operand(so_write_offset[buf]));
   } else {
      store->offset = offset;
   }
   store->offen = true;
   store->glc = true;
   store->dlc = false;
   store->slc = true;
   store->can_reorder = true;
   ctx->block->instructions.emplace_back(std::move(store));
}

static void emit_streamout(isel_context *ctx, unsigned stream)
{
   Builder bld(ctx->program, ctx->block);

   Temp so_buffers[4];
   Temp buf_ptr = convert_pointer_to_64_bit(ctx, ctx->streamout_buffers);
   for (unsigned i = 0; i < 4; i++) {
      unsigned stride = ctx->program->info->so.strides[i];
      if (!stride)
         continue;

      so_buffers[i] = bld.smem(aco_opcode::s_load_dwordx4, bld.def(s4), buf_ptr, Operand(i * 16u));
   }

   Temp so_vtx_count = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc),
                                ctx->streamout_config, Operand(0x70010u));

   Temp tid = bld.vop3(aco_opcode::v_mbcnt_hi_u32_b32, bld.def(v1), Operand((uint32_t) -1),
                       bld.vop3(aco_opcode::v_mbcnt_lo_u32_b32, bld.def(v1), Operand((uint32_t) -1), Operand(0u)));

   Temp can_emit = bld.vopc(aco_opcode::v_cmp_gt_i32, bld.def(s2), so_vtx_count, tid);

   if_context ic;
   begin_divergent_if_then(ctx, &ic, can_emit);

   bld.reset(ctx->block);

   Temp so_write_index = bld.vadd32(bld.def(v1), ctx->streamout_write_idx, tid);

   Temp so_write_offset[4];

   for (unsigned i = 0; i < 4; i++) {
      unsigned stride = ctx->program->info->so.strides[i];
      if (!stride)
         continue;

      if (stride == 1) {
         Temp offset = bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc),
                                ctx->streamout_write_idx, ctx->streamout_offset[i]);
         Temp new_offset = bld.vadd32(bld.def(v1), offset, tid);

         so_write_offset[i] = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand(2u), new_offset);
      } else {
         Temp offset = bld.v_mul_imm(bld.def(v1), so_write_index, stride * 4u);
         Temp offset2 = bld.sop2(aco_opcode::s_mul_i32, bld.def(s1), Operand(4u), ctx->streamout_offset[i]);
         so_write_offset[i] = bld.vadd32(bld.def(v1), offset, offset2);
      }
   }

   for (unsigned i = 0; i < ctx->program->info->so.num_outputs; i++) {
      struct radv_stream_output *output =
         &ctx->program->info->so.outputs[i];
      if (stream != output->stream)
         continue;

      emit_stream_output(ctx, so_buffers, so_write_offset, output);
   }

   begin_divergent_if_else(ctx, &ic);
   end_divergent_if(ctx, &ic);
}

} /* end namespace */

void handle_bc_optimize(isel_context *ctx)
{
   /* needed when SPI_PS_IN_CONTROL.BC_OPTIMIZE_DISABLE is set to 0 */
   Builder bld(ctx->program, ctx->block);
   uint32_t spi_ps_input_ena = ctx->program->config->spi_ps_input_ena;
   bool uses_center = G_0286CC_PERSP_CENTER_ENA(spi_ps_input_ena) || G_0286CC_LINEAR_CENTER_ENA(spi_ps_input_ena);
   bool uses_centroid = G_0286CC_PERSP_CENTROID_ENA(spi_ps_input_ena) || G_0286CC_LINEAR_CENTROID_ENA(spi_ps_input_ena);
   if (uses_center && uses_centroid) {
      Temp sel = bld.vopc_e64(aco_opcode::v_cmp_lt_i32, bld.hint_vcc(bld.def(s2)), ctx->prim_mask, Operand(0u));

      if (G_0286CC_PERSP_CENTROID_ENA(spi_ps_input_ena)) {
         for (unsigned i = 0; i < 2; i++) {
            Temp new_coord = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1),
                                      ctx->fs_inputs[fs_input::persp_centroid_p1 + i],
                                      ctx->fs_inputs[fs_input::persp_center_p1 + i],
                                      sel);
            ctx->fs_inputs[fs_input::persp_centroid_p1 + i] = new_coord;
         }
      }

      if (G_0286CC_LINEAR_CENTROID_ENA(spi_ps_input_ena)) {
         for (unsigned i = 0; i < 2; i++) {
            Temp new_coord = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1),
                                      ctx->fs_inputs[fs_input::linear_centroid_p1 + i],
                                      ctx->fs_inputs[fs_input::linear_center_p1 + i],
                                      sel);
            ctx->fs_inputs[fs_input::linear_centroid_p1 + i] = new_coord;
         }
      }
   }
}

void select_program(Program *program,
                    unsigned shader_count,
                    struct nir_shader *const *shaders,
                    ac_shader_config* config,
                    struct radv_shader_info *info,
                    struct radv_nir_compiler_options *options)
{
   isel_context ctx = setup_isel_context(program, shader_count, shaders, config, info, options);

   for (unsigned i = 0; i < shader_count; i++) {
      nir_shader *nir = shaders[i];
      init_context(&ctx, nir);

      if (!i) {
         add_startpgm(&ctx); /* needs to be after init_context() for FS */
         append_logical_start(ctx.block);
      }

      if_context ic;
      if (shader_count >= 2) {
         Builder bld(ctx.program, ctx.block);
         Temp count = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc), ctx.merged_wave_info, Operand((8u << 16) | (i * 8u)));
         Temp thread_id = bld.vop3(aco_opcode::v_mbcnt_hi_u32_b32, bld.def(v1), Operand((uint32_t) -1),
                                   bld.vop3(aco_opcode::v_mbcnt_lo_u32_b32, bld.def(v1), Operand((uint32_t) -1), Operand(0u)));
         Temp cond = bld.vopc(aco_opcode::v_cmp_gt_u32, bld.hint_vcc(bld.def(s2)), count, thread_id);

         begin_divergent_if_then(&ctx, &ic, cond);
      }

      if (i) {
         Builder bld(ctx.program, ctx.block);
         bld.barrier(aco_opcode::p_memory_barrier_shared); //TODO: different barriers are needed for different stages
         bld.sopp(aco_opcode::s_barrier);
      }

      if (ctx.stage == fragment_fs)
         handle_bc_optimize(&ctx);

      nir_function_impl *func = nir_shader_get_entrypoint(nir);
      visit_cf_list(&ctx, &func->body);

      if (ctx.program->info->so.num_outputs/*&& !ctx->is_gs_copy_shader */)
         emit_streamout(&ctx, 0);

      if (ctx.stage == vertex_vs)
         create_vs_exports(&ctx);

      if (shader_count >= 2) {
         begin_divergent_if_else(&ctx, &ic);
         end_divergent_if(&ctx, &ic);
      }

      ralloc_free(ctx.divergent_vals);
   }

   append_logical_end(ctx.block);
   ctx.block->kind |= block_kind_uniform;
   Builder bld(ctx.program, ctx.block);
   if (ctx.program->wb_smem_l1_on_end)
      bld.smem(aco_opcode::s_dcache_wb, false);
   bld.sopp(aco_opcode::s_endpgm);

   /* cleanup CFG */
   for (Block& BB : program->blocks) {
      for (unsigned idx : BB.linear_preds)
         program->blocks[idx].linear_succs.emplace_back(BB.index);
      for (unsigned idx : BB.logical_preds)
         program->blocks[idx].logical_succs.emplace_back(BB.index);
   }
}
}
