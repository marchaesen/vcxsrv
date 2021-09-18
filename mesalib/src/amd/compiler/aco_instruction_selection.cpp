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

#include "aco_instruction_selection.h"

#include "aco_builder.h"
#include "aco_ir.h"

#include "common/ac_exp_param.h"
#include "common/sid.h"
#include "vulkan/radv_descriptor_set.h"

#include "util/fast_idiv_by_const.h"
#include "util/memstream.h"

#include <array>
#include <functional>
#include <map>
#include <numeric>
#include <stack>
#include <utility>
#include <vector>

namespace aco {
namespace {

#define isel_err(...) _isel_err(ctx, __FILE__, __LINE__, __VA_ARGS__)

static void
_isel_err(isel_context* ctx, const char* file, unsigned line, const nir_instr* instr,
          const char* msg)
{
   char* out;
   size_t outsize;
   struct u_memstream mem;
   u_memstream_open(&mem, &out, &outsize);
   FILE* const memf = u_memstream_get(&mem);

   fprintf(memf, "%s: ", msg);
   nir_print_instr(instr, memf);
   u_memstream_close(&mem);

   _aco_err(ctx->program, file, line, out);
   free(out);
}

struct if_context {
   Temp cond;

   bool divergent_old;
   bool exec_potentially_empty_discard_old;
   bool exec_potentially_empty_break_old;
   uint16_t exec_potentially_empty_break_depth_old;

   unsigned BB_if_idx;
   unsigned invert_idx;
   bool uniform_has_then_branch;
   bool then_branch_divergent;
   Block BB_invert;
   Block BB_endif;
};

struct loop_context {
   Block loop_exit;

   unsigned header_idx_old;
   Block* exit_old;
   bool divergent_cont_old;
   bool divergent_branch_old;
   bool divergent_if_old;
};

static bool visit_cf_list(struct isel_context* ctx, struct exec_list* list);

static void
add_logical_edge(unsigned pred_idx, Block* succ)
{
   succ->logical_preds.emplace_back(pred_idx);
}

static void
add_linear_edge(unsigned pred_idx, Block* succ)
{
   succ->linear_preds.emplace_back(pred_idx);
}

static void
add_edge(unsigned pred_idx, Block* succ)
{
   add_logical_edge(pred_idx, succ);
   add_linear_edge(pred_idx, succ);
}

static void
append_logical_start(Block* b)
{
   Builder(NULL, b).pseudo(aco_opcode::p_logical_start);
}

static void
append_logical_end(Block* b)
{
   Builder(NULL, b).pseudo(aco_opcode::p_logical_end);
}

Temp
get_ssa_temp(struct isel_context* ctx, nir_ssa_def* def)
{
   uint32_t id = ctx->first_temp_id + def->index;
   return Temp(id, ctx->program->temp_rc[id]);
}

Temp
emit_mbcnt(isel_context* ctx, Temp dst, Operand mask = Operand(), Operand base = Operand::zero())
{
   Builder bld(ctx->program, ctx->block);
   assert(mask.isUndefined() || mask.isTemp() || (mask.isFixed() && mask.physReg() == exec));
   assert(mask.isUndefined() || mask.bytes() == bld.lm.bytes());

   if (ctx->program->wave_size == 32) {
      Operand mask_lo = mask.isUndefined() ? Operand::c32(-1u) : mask;
      return bld.vop3(aco_opcode::v_mbcnt_lo_u32_b32, Definition(dst), mask_lo, base);
   }

   Operand mask_lo = Operand::c32(-1u);
   Operand mask_hi = Operand::c32(-1u);

   if (mask.isTemp()) {
      RegClass rc = RegClass(mask.regClass().type(), 1);
      Builder::Result mask_split =
         bld.pseudo(aco_opcode::p_split_vector, bld.def(rc), bld.def(rc), mask);
      mask_lo = Operand(mask_split.def(0).getTemp());
      mask_hi = Operand(mask_split.def(1).getTemp());
   } else if (mask.physReg() == exec) {
      mask_lo = Operand(exec_lo, s1);
      mask_hi = Operand(exec_hi, s1);
   }

   Temp mbcnt_lo = bld.vop3(aco_opcode::v_mbcnt_lo_u32_b32, bld.def(v1), mask_lo, base);

   if (ctx->program->chip_class <= GFX7)
      return bld.vop2(aco_opcode::v_mbcnt_hi_u32_b32, Definition(dst), mask_hi, mbcnt_lo);
   else
      return bld.vop3(aco_opcode::v_mbcnt_hi_u32_b32_e64, Definition(dst), mask_hi, mbcnt_lo);
}

Temp
emit_wqm(Builder& bld, Temp src, Temp dst = Temp(0, s1), bool program_needs_wqm = false)
{
   if (!dst.id())
      dst = bld.tmp(src.regClass());

   assert(src.size() == dst.size());

   if (bld.program->stage != fragment_fs) {
      if (!dst.id())
         return src;

      bld.copy(Definition(dst), src);
      return dst;
   }

   bld.pseudo(aco_opcode::p_wqm, Definition(dst), src);
   bld.program->needs_wqm |= program_needs_wqm;
   return dst;
}

static Temp
emit_bpermute(isel_context* ctx, Builder& bld, Temp index, Temp data)
{
   if (index.regClass() == s1)
      return bld.readlane(bld.def(s1), data, index);

   if (ctx->options->chip_class <= GFX7) {
      /* GFX6-7: there is no bpermute instruction */
      Operand index_op(index);
      Operand input_data(data);
      index_op.setLateKill(true);
      input_data.setLateKill(true);

      return bld.pseudo(aco_opcode::p_bpermute, bld.def(v1), bld.def(bld.lm), bld.def(bld.lm, vcc),
                        index_op, input_data);
   } else if (ctx->options->chip_class >= GFX10 && ctx->program->wave_size == 64) {

      /* GFX10 wave64 mode: emulate full-wave bpermute */
      Temp index_is_lo =
         bld.vopc(aco_opcode::v_cmp_ge_u32, bld.def(bld.lm), Operand::c32(31u), index);
      Builder::Result index_is_lo_split =
         bld.pseudo(aco_opcode::p_split_vector, bld.def(s1), bld.def(s1), index_is_lo);
      Temp index_is_lo_n1 = bld.sop1(aco_opcode::s_not_b32, bld.def(s1), bld.def(s1, scc),
                                     index_is_lo_split.def(1).getTemp());
      Operand same_half = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2),
                                     index_is_lo_split.def(0).getTemp(), index_is_lo_n1);
      Operand index_x4 = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(2u), index);
      Operand input_data(data);

      index_x4.setLateKill(true);
      input_data.setLateKill(true);
      same_half.setLateKill(true);

      /* We need one pair of shared VGPRs:
       * Note, that these have twice the allocation granularity of normal VGPRs */
      ctx->program->config->num_shared_vgprs = 2 * ctx->program->dev.vgpr_alloc_granule;

      return bld.pseudo(aco_opcode::p_bpermute, bld.def(v1), bld.def(s2), bld.def(s1, scc),
                        index_x4, input_data, same_half);
   } else {
      /* GFX8-9 or GFX10 wave32: bpermute works normally */
      Temp index_x4 = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(2u), index);
      return bld.ds(aco_opcode::ds_bpermute_b32, bld.def(v1), index_x4, data);
   }
}

static Temp
emit_masked_swizzle(isel_context* ctx, Builder& bld, Temp src, unsigned mask)
{
   if (ctx->options->chip_class >= GFX8) {
      unsigned and_mask = mask & 0x1f;
      unsigned or_mask = (mask >> 5) & 0x1f;
      unsigned xor_mask = (mask >> 10) & 0x1f;

      uint16_t dpp_ctrl = 0xffff;

      // TODO: we could use DPP8 for some swizzles
      if (and_mask == 0x1f && or_mask < 4 && xor_mask < 4) {
         unsigned res[4] = {0, 1, 2, 3};
         for (unsigned i = 0; i < 4; i++)
            res[i] = ((res[i] | or_mask) ^ xor_mask) & 0x3;
         dpp_ctrl = dpp_quad_perm(res[0], res[1], res[2], res[3]);
      } else if (and_mask == 0x1f && !or_mask && xor_mask == 8) {
         dpp_ctrl = dpp_row_rr(8);
      } else if (and_mask == 0x1f && !or_mask && xor_mask == 0xf) {
         dpp_ctrl = dpp_row_mirror;
      } else if (and_mask == 0x1f && !or_mask && xor_mask == 0x7) {
         dpp_ctrl = dpp_row_half_mirror;
      }

      if (dpp_ctrl != 0xffff)
         return bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), src, dpp_ctrl);
   }

   return bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, mask, 0, false);
}

Temp
as_vgpr(isel_context* ctx, Temp val)
{
   if (val.type() == RegType::sgpr) {
      Builder bld(ctx->program, ctx->block);
      return bld.copy(bld.def(RegType::vgpr, val.size()), val);
   }
   assert(val.type() == RegType::vgpr);
   return val;
}

// assumes a != 0xffffffff
void
emit_v_div_u32(isel_context* ctx, Temp dst, Temp a, uint32_t b)
{
   assert(b != 0);
   Builder bld(ctx->program, ctx->block);

   if (util_is_power_of_two_or_zero(b)) {
      bld.vop2(aco_opcode::v_lshrrev_b32, Definition(dst), Operand::c32(util_logbase2(b)), a);
      return;
   }

   util_fast_udiv_info info = util_compute_fast_udiv_info(b, 32, 32);

   assert(info.multiplier <= 0xffffffff);

   bool pre_shift = info.pre_shift != 0;
   bool increment = info.increment != 0;
   bool multiply = true;
   bool post_shift = info.post_shift != 0;

   if (!pre_shift && !increment && !multiply && !post_shift) {
      bld.copy(Definition(dst), a);
      return;
   }

   Temp pre_shift_dst = a;
   if (pre_shift) {
      pre_shift_dst = (increment || multiply || post_shift) ? bld.tmp(v1) : dst;
      bld.vop2(aco_opcode::v_lshrrev_b32, Definition(pre_shift_dst), Operand::c32(info.pre_shift),
               a);
   }

   Temp increment_dst = pre_shift_dst;
   if (increment) {
      increment_dst = (post_shift || multiply) ? bld.tmp(v1) : dst;
      bld.vadd32(Definition(increment_dst), Operand::c32(info.increment), pre_shift_dst);
   }

   Temp multiply_dst = increment_dst;
   if (multiply) {
      multiply_dst = post_shift ? bld.tmp(v1) : dst;
      bld.vop3(aco_opcode::v_mul_hi_u32, Definition(multiply_dst), increment_dst,
               bld.copy(bld.def(v1), Operand::c32(info.multiplier)));
   }

   if (post_shift) {
      bld.vop2(aco_opcode::v_lshrrev_b32, Definition(dst), Operand::c32(info.post_shift),
               multiply_dst);
   }
}

void
emit_extract_vector(isel_context* ctx, Temp src, uint32_t idx, Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   bld.pseudo(aco_opcode::p_extract_vector, Definition(dst), src, Operand::c32(idx));
}

Temp
emit_extract_vector(isel_context* ctx, Temp src, uint32_t idx, RegClass dst_rc)
{
   /* no need to extract the whole vector */
   if (src.regClass() == dst_rc) {
      assert(idx == 0);
      return src;
   }

   assert(src.bytes() > (idx * dst_rc.bytes()));
   Builder bld(ctx->program, ctx->block);
   auto it = ctx->allocated_vec.find(src.id());
   if (it != ctx->allocated_vec.end() && dst_rc.bytes() == it->second[idx].regClass().bytes()) {
      if (it->second[idx].regClass() == dst_rc) {
         return it->second[idx];
      } else {
         assert(!dst_rc.is_subdword());
         assert(dst_rc.type() == RegType::vgpr && it->second[idx].type() == RegType::sgpr);
         return bld.copy(bld.def(dst_rc), it->second[idx]);
      }
   }

   if (dst_rc.is_subdword())
      src = as_vgpr(ctx, src);

   if (src.bytes() == dst_rc.bytes()) {
      assert(idx == 0);
      return bld.copy(bld.def(dst_rc), src);
   } else {
      Temp dst = bld.tmp(dst_rc);
      emit_extract_vector(ctx, src, idx, dst);
      return dst;
   }
}

void
emit_split_vector(isel_context* ctx, Temp vec_src, unsigned num_components)
{
   if (num_components == 1)
      return;
   if (ctx->allocated_vec.find(vec_src.id()) != ctx->allocated_vec.end())
      return;
   RegClass rc;
   if (num_components > vec_src.size()) {
      if (vec_src.type() == RegType::sgpr) {
         /* should still help get_alu_src() */
         emit_split_vector(ctx, vec_src, vec_src.size());
         return;
      }
      /* sub-dword split */
      rc = RegClass(RegType::vgpr, vec_src.bytes() / num_components).as_subdword();
   } else {
      rc = RegClass(vec_src.type(), vec_src.size() / num_components);
   }
   aco_ptr<Pseudo_instruction> split{create_instruction<Pseudo_instruction>(
      aco_opcode::p_split_vector, Format::PSEUDO, 1, num_components)};
   split->operands[0] = Operand(vec_src);
   std::array<Temp, NIR_MAX_VEC_COMPONENTS> elems;
   for (unsigned i = 0; i < num_components; i++) {
      elems[i] = ctx->program->allocateTmp(rc);
      split->definitions[i] = Definition(elems[i]);
   }
   ctx->block->instructions.emplace_back(std::move(split));
   ctx->allocated_vec.emplace(vec_src.id(), elems);
}

/* This vector expansion uses a mask to determine which elements in the new vector
 * come from the original vector. The other elements are undefined. */
void
expand_vector(isel_context* ctx, Temp vec_src, Temp dst, unsigned num_components, unsigned mask)
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
   std::array<Temp, NIR_MAX_VEC_COMPONENTS> elems;

   aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(
      aco_opcode::p_create_vector, Format::PSEUDO, num_components, 1)};
   vec->definitions[0] = Definition(dst);
   unsigned k = 0;
   for (unsigned i = 0; i < num_components; i++) {
      if (mask & (1 << i)) {
         Temp src =
            emit_extract_vector(ctx, vec_src, k++, RegClass(vec_src.type(), component_size));
         if (dst.type() == RegType::sgpr)
            src = bld.as_uniform(src);
         vec->operands[i] = Operand(src);
      } else {
         vec->operands[i] = Operand::zero(component_size == 2 ? 8 : 4);
      }
      elems[i] = vec->operands[i].getTemp();
   }
   ctx->block->instructions.emplace_back(std::move(vec));
   ctx->allocated_vec.emplace(dst.id(), elems);
}

/* adjust misaligned small bit size loads */
void
byte_align_scalar(isel_context* ctx, Temp vec, Operand offset, Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   Operand shift;
   Temp select = Temp();
   if (offset.isConstant()) {
      assert(offset.constantValue() && offset.constantValue() < 4);
      shift = Operand::c32(offset.constantValue() * 8);
   } else {
      /* bit_offset = 8 * (offset & 0x3) */
      Temp tmp =
         bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), offset, Operand::c32(3u));
      select = bld.tmp(s1);
      shift = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.scc(Definition(select)), tmp,
                       Operand::c32(3u));
   }

   if (vec.size() == 1) {
      bld.sop2(aco_opcode::s_lshr_b32, Definition(dst), bld.def(s1, scc), vec, shift);
   } else if (vec.size() == 2) {
      Temp tmp = dst.size() == 2 ? dst : bld.tmp(s2);
      bld.sop2(aco_opcode::s_lshr_b64, Definition(tmp), bld.def(s1, scc), vec, shift);
      if (tmp == dst)
         emit_split_vector(ctx, dst, 2);
      else
         emit_extract_vector(ctx, tmp, 0, dst);
   } else if (vec.size() == 3 || vec.size() == 4) {
      Temp lo = bld.tmp(s2), hi;
      if (vec.size() == 3) {
         /* this can happen if we use VMEM for a uniform load */
         hi = bld.tmp(s1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), vec);
      } else {
         hi = bld.tmp(s2);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), vec);
         hi = bld.pseudo(aco_opcode::p_extract_vector, bld.def(s1), hi, Operand::zero());
      }
      if (select != Temp())
         hi =
            bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1), hi, Operand::zero(), bld.scc(select));
      lo = bld.sop2(aco_opcode::s_lshr_b64, bld.def(s2), bld.def(s1, scc), lo, shift);
      Temp mid = bld.tmp(s1);
      lo = bld.pseudo(aco_opcode::p_split_vector, bld.def(s1), Definition(mid), lo);
      hi = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), hi, shift);
      mid = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), hi, mid);
      bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, mid);
      emit_split_vector(ctx, dst, 2);
   }
}

void
byte_align_vector(isel_context* ctx, Temp vec, Operand offset, Temp dst, unsigned component_size)
{
   Builder bld(ctx->program, ctx->block);
   if (offset.isTemp()) {
      Temp tmp[4] = {vec, vec, vec, vec};

      if (vec.size() == 4) {
         tmp[0] = bld.tmp(v1), tmp[1] = bld.tmp(v1), tmp[2] = bld.tmp(v1), tmp[3] = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(tmp[0]), Definition(tmp[1]),
                    Definition(tmp[2]), Definition(tmp[3]), vec);
      } else if (vec.size() == 3) {
         tmp[0] = bld.tmp(v1), tmp[1] = bld.tmp(v1), tmp[2] = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(tmp[0]), Definition(tmp[1]),
                    Definition(tmp[2]), vec);
      } else if (vec.size() == 2) {
         tmp[0] = bld.tmp(v1), tmp[1] = bld.tmp(v1), tmp[2] = tmp[1];
         bld.pseudo(aco_opcode::p_split_vector, Definition(tmp[0]), Definition(tmp[1]), vec);
      }
      for (unsigned i = 0; i < dst.size(); i++)
         tmp[i] = bld.vop3(aco_opcode::v_alignbyte_b32, bld.def(v1), tmp[i + 1], tmp[i], offset);

      vec = tmp[0];
      if (dst.size() == 2)
         vec = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), tmp[0], tmp[1]);

      offset = Operand::zero();
   }

   unsigned num_components = vec.bytes() / component_size;
   if (vec.regClass() == dst.regClass()) {
      assert(offset.constantValue() == 0);
      bld.copy(Definition(dst), vec);
      emit_split_vector(ctx, dst, num_components);
      return;
   }

   emit_split_vector(ctx, vec, num_components);
   std::array<Temp, NIR_MAX_VEC_COMPONENTS> elems;
   RegClass rc = RegClass(RegType::vgpr, component_size).as_subdword();

   assert(offset.constantValue() % component_size == 0);
   unsigned skip = offset.constantValue() / component_size;
   for (unsigned i = skip; i < num_components; i++)
      elems[i - skip] = emit_extract_vector(ctx, vec, i, rc);

   if (dst.type() == RegType::vgpr) {
      /* if dst is vgpr - split the src and create a shrunk version according to the mask. */
      num_components = dst.bytes() / component_size;
      aco_ptr<Pseudo_instruction> create_vec{create_instruction<Pseudo_instruction>(
         aco_opcode::p_create_vector, Format::PSEUDO, num_components, 1)};
      for (unsigned i = 0; i < num_components; i++)
         create_vec->operands[i] = Operand(elems[i]);
      create_vec->definitions[0] = Definition(dst);
      bld.insert(std::move(create_vec));

   } else if (skip) {
      /* if dst is sgpr - split the src, but move the original to sgpr. */
      vec = bld.pseudo(aco_opcode::p_as_uniform, bld.def(RegClass(RegType::sgpr, vec.size())), vec);
      byte_align_scalar(ctx, vec, offset, dst);
   } else {
      assert(dst.size() == vec.size());
      bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), vec);
   }

   ctx->allocated_vec.emplace(dst.id(), elems);
}

Temp
bool_to_vector_condition(isel_context* ctx, Temp val, Temp dst = Temp(0, s2))
{
   Builder bld(ctx->program, ctx->block);
   if (!dst.id())
      dst = bld.tmp(bld.lm);

   assert(val.regClass() == s1);
   assert(dst.regClass() == bld.lm);

   return bld.sop2(Builder::s_cselect, Definition(dst), Operand::c32(-1), Operand::zero(),
                   bld.scc(val));
}

Temp
bool_to_scalar_condition(isel_context* ctx, Temp val, Temp dst = Temp(0, s1))
{
   Builder bld(ctx->program, ctx->block);
   if (!dst.id())
      dst = bld.tmp(s1);

   assert(val.regClass() == bld.lm);
   assert(dst.regClass() == s1);

   /* if we're currently in WQM mode, ensure that the source is also computed in WQM */
   Temp tmp = bld.tmp(s1);
   bld.sop2(Builder::s_and, bld.def(bld.lm), bld.scc(Definition(tmp)), val, Operand(exec, bld.lm));
   return emit_wqm(bld, tmp, dst);
}

/**
 * Copies the first src_bits of the input to the output Temp. Input bits at positions larger than
 * src_bits and dst_bits are truncated.
 *
 * Sign extension may be applied using the sign_extend parameter. The position of the input sign
 * bit is indicated by src_bits in this case.
 *
 * If dst.bytes() is larger than dst_bits/8, the value of the upper bits is undefined.
 */
Temp
convert_int(isel_context* ctx, Builder& bld, Temp src, unsigned src_bits, unsigned dst_bits,
            bool sign_extend, Temp dst = Temp())
{
   assert(!(sign_extend && dst_bits < src_bits) &&
          "Shrinking integers is not supported for signed inputs");

   if (!dst.id()) {
      if (dst_bits % 32 == 0 || src.type() == RegType::sgpr)
         dst = bld.tmp(src.type(), DIV_ROUND_UP(dst_bits, 32u));
      else
         dst = bld.tmp(RegClass(RegType::vgpr, dst_bits / 8u).as_subdword());
   }

   assert(src.type() == RegType::sgpr || src_bits == src.bytes() * 8);
   assert(dst.type() == RegType::sgpr || dst_bits == dst.bytes() * 8);

   if (dst.bytes() == src.bytes() && dst_bits < src_bits) {
      /* Copy the raw value, leaving an undefined value in the upper bits for
       * the caller to handle appropriately */
      return bld.copy(Definition(dst), src);
   } else if (dst.bytes() < src.bytes()) {
      return bld.pseudo(aco_opcode::p_extract_vector, Definition(dst), src, Operand::zero());
   }

   Temp tmp = dst;
   if (dst_bits == 64)
      tmp = src_bits == 32 ? src : bld.tmp(src.type(), 1);

   if (tmp == src) {
   } else if (src.regClass() == s1) {
      assert(src_bits < 32);
      bld.pseudo(aco_opcode::p_extract, Definition(tmp), bld.def(s1, scc), src, Operand::zero(),
                 Operand::c32(src_bits), Operand::c32((unsigned)sign_extend));
   } else if (ctx->options->chip_class >= GFX8) {
      assert(src_bits < 32);
      assert(src_bits != 8 || src.regClass() == v1b);
      assert(src_bits != 16 || src.regClass() == v2b);
      assert(dst_bits >= 16);
      aco_ptr<SDWA_instruction> sdwa{
         create_instruction<SDWA_instruction>(aco_opcode::v_mov_b32, asSDWA(Format::VOP1), 1, 1)};
      sdwa->operands[0] = Operand(src);
      sdwa->definitions[0] = Definition(tmp);
      sdwa->sel[0] = SubdwordSel(src_bits / 8, 0, sign_extend);
      sdwa->dst_sel = tmp.bytes() == 2 ? SubdwordSel::uword : SubdwordSel::dword;
      bld.insert(std::move(sdwa));
   } else {
      assert(src_bits < 32);
      assert(ctx->options->chip_class == GFX6 || ctx->options->chip_class == GFX7);
      aco_opcode opcode = sign_extend ? aco_opcode::v_bfe_i32 : aco_opcode::v_bfe_u32;
      bld.vop3(opcode, Definition(tmp), src, Operand::zero(),
               Operand::c32(src_bits == 8 ? 8u : 16u));
   }

   if (dst_bits == 64) {
      if (sign_extend && dst.regClass() == s2) {
         Temp high =
            bld.sop2(aco_opcode::s_ashr_i32, bld.def(s1), bld.def(s1, scc), tmp, Operand::c32(31u));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), tmp, high);
      } else if (sign_extend && dst.regClass() == v2) {
         Temp high = bld.vop2(aco_opcode::v_ashrrev_i32, bld.def(v1), Operand::c32(31u), tmp);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), tmp, high);
      } else {
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), tmp, Operand::zero());
      }
   }

   return dst;
}

enum sgpr_extract_mode {
   sgpr_extract_sext,
   sgpr_extract_zext,
   sgpr_extract_undef,
};

Temp
extract_8_16_bit_sgpr_element(isel_context* ctx, Temp dst, nir_alu_src* src, sgpr_extract_mode mode)
{
   Temp vec = get_ssa_temp(ctx, src->src.ssa);
   unsigned src_size = src->src.ssa->bit_size;
   unsigned swizzle = src->swizzle[0];

   if (vec.size() > 1) {
      assert(src_size == 16);
      vec = emit_extract_vector(ctx, vec, swizzle / 2, s1);
      swizzle = swizzle & 1;
   }

   Builder bld(ctx->program, ctx->block);
   Temp tmp = dst.regClass() == s2 ? bld.tmp(s1) : dst;

   if (mode == sgpr_extract_undef && swizzle == 0)
      bld.copy(Definition(tmp), vec);
   else
      bld.pseudo(aco_opcode::p_extract, Definition(tmp), bld.def(s1, scc), Operand(vec),
                 Operand::c32(swizzle), Operand::c32(src_size),
                 Operand::c32((mode == sgpr_extract_sext)));

   if (dst.regClass() == s2)
      convert_int(ctx, bld, tmp, 32, 64, mode == sgpr_extract_sext, dst);

   return dst;
}

Temp
get_alu_src(struct isel_context* ctx, nir_alu_src src, unsigned size = 1)
{
   if (src.src.ssa->num_components == 1 && size == 1)
      return get_ssa_temp(ctx, src.src.ssa);

   Temp vec = get_ssa_temp(ctx, src.src.ssa);
   unsigned elem_size = src.src.ssa->bit_size / 8u;
   bool identity_swizzle = true;

   for (unsigned i = 0; identity_swizzle && i < size; i++) {
      if (src.swizzle[i] != i)
         identity_swizzle = false;
   }
   if (identity_swizzle)
      return emit_extract_vector(ctx, vec, 0, RegClass::get(vec.type(), elem_size * size));

   assert(elem_size > 0);
   assert(vec.bytes() % elem_size == 0);

   if (elem_size < 4 && vec.type() == RegType::sgpr && size == 1) {
      assert(src.src.ssa->bit_size == 8 || src.src.ssa->bit_size == 16);
      return extract_8_16_bit_sgpr_element(ctx, ctx->program->allocateTmp(s1), &src,
                                           sgpr_extract_undef);
   }

   bool as_uniform = elem_size < 4 && vec.type() == RegType::sgpr;
   if (as_uniform)
      vec = as_vgpr(ctx, vec);

   RegClass elem_rc = elem_size < 4 ? RegClass(vec.type(), elem_size).as_subdword()
                                    : RegClass(vec.type(), elem_size / 4);
   if (size == 1) {
      return emit_extract_vector(ctx, vec, src.swizzle[0], elem_rc);
   } else {
      assert(size <= 4);
      std::array<Temp, NIR_MAX_VEC_COMPONENTS> elems;
      aco_ptr<Pseudo_instruction> vec_instr{create_instruction<Pseudo_instruction>(
         aco_opcode::p_create_vector, Format::PSEUDO, size, 1)};
      for (unsigned i = 0; i < size; ++i) {
         elems[i] = emit_extract_vector(ctx, vec, src.swizzle[i], elem_rc);
         vec_instr->operands[i] = Operand{elems[i]};
      }
      Temp dst = ctx->program->allocateTmp(RegClass(vec.type(), elem_size * size / 4));
      vec_instr->definitions[0] = Definition(dst);
      ctx->block->instructions.emplace_back(std::move(vec_instr));
      ctx->allocated_vec.emplace(dst.id(), elems);
      return vec.type() == RegType::sgpr ? Builder(ctx->program, ctx->block).as_uniform(dst) : dst;
   }
}

Temp
get_alu_src_vop3p(struct isel_context* ctx, nir_alu_src src)
{
   /* returns v2b or v1 for vop3p usage.
    * The source expects exactly 2 16bit components
    * which are within the same dword
    */
   assert(src.src.ssa->bit_size == 16);
   assert(src.swizzle[0] >> 1 == src.swizzle[1] >> 1);

   Temp tmp = get_ssa_temp(ctx, src.src.ssa);
   if (tmp.size() == 1)
      return tmp;

   /* the size is larger than 1 dword: check the swizzle */
   unsigned dword = src.swizzle[0] >> 1;

   /* extract a full dword if possible */
   if (tmp.bytes() >= (dword + 1) * 4) {
      return emit_extract_vector(ctx, tmp, dword, RegClass(tmp.type(), 1));
   } else {
      /* This must be a swizzled access to %a.zz where %a is v6b */
      assert(((src.swizzle[0] | src.swizzle[1]) & 1) == 0);
      assert(tmp.regClass() == v6b && dword == 1);
      return emit_extract_vector(ctx, tmp, dword * 2, v2b);
   }
}

uint32_t
get_alu_src_ub(isel_context* ctx, nir_alu_instr* instr, int src_idx)
{
   nir_ssa_scalar scalar =
      nir_ssa_scalar{instr->src[src_idx].src.ssa, instr->src[src_idx].swizzle[0]};
   return nir_unsigned_upper_bound(ctx->shader, ctx->range_ht, scalar, &ctx->ub_config);
}

Temp
convert_pointer_to_64_bit(isel_context* ctx, Temp ptr, bool non_uniform = false)
{
   if (ptr.size() == 2)
      return ptr;
   Builder bld(ctx->program, ctx->block);
   if (ptr.type() == RegType::vgpr && !non_uniform)
      ptr = bld.as_uniform(ptr);
   return bld.pseudo(aco_opcode::p_create_vector, bld.def(RegClass(ptr.type(), 2)), ptr,
                     Operand::c32((unsigned)ctx->options->address32_hi));
}

void
emit_sop2_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst,
                      bool writes_scc, uint8_t uses_ub = 0)
{
   aco_ptr<SOP2_instruction> sop2{
      create_instruction<SOP2_instruction>(op, Format::SOP2, 2, writes_scc ? 2 : 1)};
   sop2->operands[0] = Operand(get_alu_src(ctx, instr->src[0]));
   sop2->operands[1] = Operand(get_alu_src(ctx, instr->src[1]));
   sop2->definitions[0] = Definition(dst);
   if (instr->no_unsigned_wrap)
      sop2->definitions[0].setNUW(true);
   if (writes_scc)
      sop2->definitions[1] = Definition(ctx->program->allocateId(s1), scc, s1);

   for (int i = 0; i < 2; i++) {
      if (uses_ub & (1 << i)) {
         uint32_t src_ub = get_alu_src_ub(ctx, instr, i);
         if (src_ub <= 0xffff)
            sop2->operands[i].set16bit(true);
         else if (src_ub <= 0xffffff)
            sop2->operands[i].set24bit(true);
      }
   }

   ctx->block->instructions.emplace_back(std::move(sop2));
}

void
emit_vop2_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst,
                      bool commutative, bool swap_srcs = false, bool flush_denorms = false,
                      bool nuw = false, uint8_t uses_ub = 0)
{
   Builder bld(ctx->program, ctx->block);
   bld.is_precise = instr->exact;

   Temp src0 = get_alu_src(ctx, instr->src[swap_srcs ? 1 : 0]);
   Temp src1 = get_alu_src(ctx, instr->src[swap_srcs ? 0 : 1]);
   if (src1.type() == RegType::sgpr) {
      if (commutative && src0.type() == RegType::vgpr) {
         Temp t = src0;
         src0 = src1;
         src1 = t;
      } else {
         src1 = as_vgpr(ctx, src1);
      }
   }

   Operand op0(src0);
   Operand op1(src1);

   for (int i = 0; i < 2; i++) {
      if (uses_ub & (1 << i)) {
         uint32_t src_ub = get_alu_src_ub(ctx, instr, swap_srcs ? !i : i);
         if (src_ub <= 0xffff)
            bld.set16bit(i ? op1 : op0);
         else if (src_ub <= 0xffffff)
            bld.set24bit(i ? op1 : op0);
      }
   }

   if (flush_denorms && ctx->program->chip_class < GFX9) {
      assert(dst.size() == 1);
      Temp tmp = bld.vop2(op, bld.def(v1), op0, op1);
      bld.vop2(aco_opcode::v_mul_f32, Definition(dst), Operand::c32(0x3f800000u), tmp);
   } else {
      if (nuw) {
         bld.nuw().vop2(op, Definition(dst), op0, op1);
      } else {
         bld.vop2(op, Definition(dst), op0, op1);
      }
   }
}

void
emit_vop2_instruction_logic64(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   bld.is_precise = instr->exact;

   Temp src0 = get_alu_src(ctx, instr->src[0]);
   Temp src1 = get_alu_src(ctx, instr->src[1]);

   if (src1.type() == RegType::sgpr) {
      assert(src0.type() == RegType::vgpr);
      std::swap(src0, src1);
   }

   Temp src00 = bld.tmp(src0.type(), 1);
   Temp src01 = bld.tmp(src0.type(), 1);
   bld.pseudo(aco_opcode::p_split_vector, Definition(src00), Definition(src01), src0);
   Temp src10 = bld.tmp(v1);
   Temp src11 = bld.tmp(v1);
   bld.pseudo(aco_opcode::p_split_vector, Definition(src10), Definition(src11), src1);
   Temp lo = bld.vop2(op, bld.def(v1), src00, src10);
   Temp hi = bld.vop2(op, bld.def(v1), src01, src11);
   bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
}

void
emit_vop3a_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst,
                       bool flush_denorms = false, unsigned num_sources = 2, bool swap_srcs = false)
{
   assert(num_sources == 2 || num_sources == 3);
   Temp src[3] = {Temp(0, v1), Temp(0, v1), Temp(0, v1)};
   bool has_sgpr = false;
   for (unsigned i = 0; i < num_sources; i++) {
      src[i] = get_alu_src(ctx, instr->src[swap_srcs ? 1 - i : i]);
      if (has_sgpr)
         src[i] = as_vgpr(ctx, src[i]);
      else
         has_sgpr = src[i].type() == RegType::sgpr;
   }

   Builder bld(ctx->program, ctx->block);
   bld.is_precise = instr->exact;
   if (flush_denorms && ctx->program->chip_class < GFX9) {
      Temp tmp;
      if (num_sources == 3)
         tmp = bld.vop3(op, bld.def(dst.regClass()), src[0], src[1], src[2]);
      else
         tmp = bld.vop3(op, bld.def(dst.regClass()), src[0], src[1]);
      if (dst.size() == 1)
         bld.vop2(aco_opcode::v_mul_f32, Definition(dst), Operand::c32(0x3f800000u), tmp);
      else
         bld.vop3(aco_opcode::v_mul_f64, Definition(dst), Operand::c64(0x3FF0000000000000), tmp);
   } else if (num_sources == 3) {
      bld.vop3(op, Definition(dst), src[0], src[1], src[2]);
   } else {
      bld.vop3(op, Definition(dst), src[0], src[1]);
   }
}

Builder::Result
emit_vop3p_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst,
                       bool swap_srcs = false)
{
   Temp src0 = get_alu_src_vop3p(ctx, instr->src[swap_srcs]);
   Temp src1 = get_alu_src_vop3p(ctx, instr->src[!swap_srcs]);
   if (src0.type() == RegType::sgpr && src1.type() == RegType::sgpr)
      src1 = as_vgpr(ctx, src1);
   assert(instr->dest.dest.ssa.num_components == 2);

   /* swizzle to opsel: all swizzles are either 0 (x) or 1 (y) */
   unsigned opsel_lo =
      (instr->src[!swap_srcs].swizzle[0] & 1) << 1 | (instr->src[swap_srcs].swizzle[0] & 1);
   unsigned opsel_hi =
      (instr->src[!swap_srcs].swizzle[1] & 1) << 1 | (instr->src[swap_srcs].swizzle[1] & 1);

   Builder bld(ctx->program, ctx->block);
   bld.is_precise = instr->exact;
   Builder::Result res = bld.vop3p(op, Definition(dst), src0, src1, opsel_lo, opsel_hi);
   emit_split_vector(ctx, dst, 2);
   return res;
}

void
emit_idot_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst, bool clamp)
{
   Temp src[3] = {Temp(0, v1), Temp(0, v1), Temp(0, v1)};
   bool has_sgpr = false;
   for (unsigned i = 0; i < 3; i++) {
      src[i] = get_alu_src(ctx, instr->src[i]);
      if (has_sgpr)
         src[i] = as_vgpr(ctx, src[i]);
      else
         has_sgpr = src[i].type() == RegType::sgpr;
   }

   Builder bld(ctx->program, ctx->block);
   bld.is_precise = instr->exact;
   bld.vop3p(op, Definition(dst), src[0], src[1], src[2], 0x0, 0x7).instr->vop3p().clamp = clamp;
}

void
emit_vop1_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   bld.is_precise = instr->exact;
   if (dst.type() == RegType::sgpr)
      bld.pseudo(aco_opcode::p_as_uniform, Definition(dst),
                 bld.vop1(op, bld.def(RegType::vgpr, dst.size()), get_alu_src(ctx, instr->src[0])));
   else
      bld.vop1(op, Definition(dst), get_alu_src(ctx, instr->src[0]));
}

void
emit_vopc_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst)
{
   Temp src0 = get_alu_src(ctx, instr->src[0]);
   Temp src1 = get_alu_src(ctx, instr->src[1]);
   assert(src0.size() == src1.size());

   aco_ptr<Instruction> vopc;
   if (src1.type() == RegType::sgpr) {
      if (src0.type() == RegType::vgpr) {
         /* to swap the operands, we might also have to change the opcode */
         switch (op) {
         case aco_opcode::v_cmp_lt_f16: op = aco_opcode::v_cmp_gt_f16; break;
         case aco_opcode::v_cmp_ge_f16: op = aco_opcode::v_cmp_le_f16; break;
         case aco_opcode::v_cmp_lt_i16: op = aco_opcode::v_cmp_gt_i16; break;
         case aco_opcode::v_cmp_ge_i16: op = aco_opcode::v_cmp_le_i16; break;
         case aco_opcode::v_cmp_lt_u16: op = aco_opcode::v_cmp_gt_u16; break;
         case aco_opcode::v_cmp_ge_u16: op = aco_opcode::v_cmp_le_u16; break;
         case aco_opcode::v_cmp_lt_f32: op = aco_opcode::v_cmp_gt_f32; break;
         case aco_opcode::v_cmp_ge_f32: op = aco_opcode::v_cmp_le_f32; break;
         case aco_opcode::v_cmp_lt_i32: op = aco_opcode::v_cmp_gt_i32; break;
         case aco_opcode::v_cmp_ge_i32: op = aco_opcode::v_cmp_le_i32; break;
         case aco_opcode::v_cmp_lt_u32: op = aco_opcode::v_cmp_gt_u32; break;
         case aco_opcode::v_cmp_ge_u32: op = aco_opcode::v_cmp_le_u32; break;
         case aco_opcode::v_cmp_lt_f64: op = aco_opcode::v_cmp_gt_f64; break;
         case aco_opcode::v_cmp_ge_f64: op = aco_opcode::v_cmp_le_f64; break;
         case aco_opcode::v_cmp_lt_i64: op = aco_opcode::v_cmp_gt_i64; break;
         case aco_opcode::v_cmp_ge_i64: op = aco_opcode::v_cmp_le_i64; break;
         case aco_opcode::v_cmp_lt_u64: op = aco_opcode::v_cmp_gt_u64; break;
         case aco_opcode::v_cmp_ge_u64: op = aco_opcode::v_cmp_le_u64; break;
         default: /* eq and ne are commutative */ break;
         }
         Temp t = src0;
         src0 = src1;
         src1 = t;
      } else {
         src1 = as_vgpr(ctx, src1);
      }
   }

   Builder bld(ctx->program, ctx->block);
   bld.vopc(op, bld.hint_vcc(Definition(dst)), src0, src1);
}

void
emit_sopc_instruction(isel_context* ctx, nir_alu_instr* instr, aco_opcode op, Temp dst)
{
   Temp src0 = get_alu_src(ctx, instr->src[0]);
   Temp src1 = get_alu_src(ctx, instr->src[1]);
   Builder bld(ctx->program, ctx->block);

   assert(dst.regClass() == bld.lm);
   assert(src0.type() == RegType::sgpr);
   assert(src1.type() == RegType::sgpr);
   assert(src0.regClass() == src1.regClass());

   /* Emit the SALU comparison instruction */
   Temp cmp = bld.sopc(op, bld.scc(bld.def(s1)), src0, src1);
   /* Turn the result into a per-lane bool */
   bool_to_vector_condition(ctx, cmp, dst);
}

void
emit_comparison(isel_context* ctx, nir_alu_instr* instr, Temp dst, aco_opcode v16_op,
                aco_opcode v32_op, aco_opcode v64_op, aco_opcode s32_op = aco_opcode::num_opcodes,
                aco_opcode s64_op = aco_opcode::num_opcodes)
{
   aco_opcode s_op = instr->src[0].src.ssa->bit_size == 64   ? s64_op
                     : instr->src[0].src.ssa->bit_size == 32 ? s32_op
                                                             : aco_opcode::num_opcodes;
   aco_opcode v_op = instr->src[0].src.ssa->bit_size == 64   ? v64_op
                     : instr->src[0].src.ssa->bit_size == 32 ? v32_op
                                                             : v16_op;
   bool use_valu = s_op == aco_opcode::num_opcodes || nir_dest_is_divergent(instr->dest.dest) ||
                   get_ssa_temp(ctx, instr->src[0].src.ssa).type() == RegType::vgpr ||
                   get_ssa_temp(ctx, instr->src[1].src.ssa).type() == RegType::vgpr;
   aco_opcode op = use_valu ? v_op : s_op;
   assert(op != aco_opcode::num_opcodes);
   assert(dst.regClass() == ctx->program->lane_mask);

   if (use_valu)
      emit_vopc_instruction(ctx, instr, op, dst);
   else
      emit_sopc_instruction(ctx, instr, op, dst);
}

void
emit_boolean_logic(isel_context* ctx, nir_alu_instr* instr, Builder::WaveSpecificOpcode op,
                   Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   Temp src0 = get_alu_src(ctx, instr->src[0]);
   Temp src1 = get_alu_src(ctx, instr->src[1]);

   assert(dst.regClass() == bld.lm);
   assert(src0.regClass() == bld.lm);
   assert(src1.regClass() == bld.lm);

   bld.sop2(op, Definition(dst), bld.def(s1, scc), src0, src1);
}

void
emit_bcsel(isel_context* ctx, nir_alu_instr* instr, Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   Temp cond = get_alu_src(ctx, instr->src[0]);
   Temp then = get_alu_src(ctx, instr->src[1]);
   Temp els = get_alu_src(ctx, instr->src[2]);

   assert(cond.regClass() == bld.lm);

   if (dst.type() == RegType::vgpr) {
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
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      return;
   }

   if (instr->dest.dest.ssa.bit_size == 1) {
      assert(dst.regClass() == bld.lm);
      assert(then.regClass() == bld.lm);
      assert(els.regClass() == bld.lm);
   }

   if (!nir_src_is_divergent(instr->src[0].src)) { /* uniform condition and values in sgpr */
      if (dst.regClass() == s1 || dst.regClass() == s2) {
         assert((then.regClass() == s1 || then.regClass() == s2) &&
                els.regClass() == then.regClass());
         assert(dst.size() == then.size());
         aco_opcode op =
            dst.regClass() == s1 ? aco_opcode::s_cselect_b32 : aco_opcode::s_cselect_b64;
         bld.sop2(op, Definition(dst), then, els, bld.scc(bool_to_scalar_condition(ctx, cond)));
      } else {
         isel_err(&instr->instr, "Unimplemented uniform bcsel bit size");
      }
      return;
   }

   /* divergent boolean bcsel
    * this implements bcsel on bools: dst = s0 ? s1 : s2
    * are going to be: dst = (s0 & s1) | (~s0 & s2) */
   assert(instr->dest.dest.ssa.bit_size == 1);

   if (cond.id() != then.id())
      then = bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), cond, then);

   if (cond.id() == els.id())
      bld.copy(Definition(dst), then);
   else
      bld.sop2(Builder::s_or, Definition(dst), bld.def(s1, scc), then,
               bld.sop2(Builder::s_andn2, bld.def(bld.lm), bld.def(s1, scc), els, cond));
}

void
emit_scaled_op(isel_context* ctx, Builder& bld, Definition dst, Temp val, aco_opcode op,
               uint32_t undo)
{
   /* multiply by 16777216 to handle denormals */
   Temp is_denormal =
      bld.vopc(aco_opcode::v_cmp_class_f32, bld.hint_vcc(bld.def(bld.lm)), as_vgpr(ctx, val),
               bld.copy(bld.def(v1), Operand::c32((1u << 7) | (1u << 4))));
   Temp scaled = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), Operand::c32(0x4b800000u), val);
   scaled = bld.vop1(op, bld.def(v1), scaled);
   scaled = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), Operand::c32(undo), scaled);

   Temp not_scaled = bld.vop1(op, bld.def(v1), val);

   bld.vop2(aco_opcode::v_cndmask_b32, dst, not_scaled, scaled, is_denormal);
}

void
emit_rcp(isel_context* ctx, Builder& bld, Definition dst, Temp val)
{
   if (ctx->block->fp_mode.denorm32 == 0) {
      bld.vop1(aco_opcode::v_rcp_f32, dst, val);
      return;
   }

   emit_scaled_op(ctx, bld, dst, val, aco_opcode::v_rcp_f32, 0x4b800000u);
}

void
emit_rsq(isel_context* ctx, Builder& bld, Definition dst, Temp val)
{
   if (ctx->block->fp_mode.denorm32 == 0) {
      bld.vop1(aco_opcode::v_rsq_f32, dst, val);
      return;
   }

   emit_scaled_op(ctx, bld, dst, val, aco_opcode::v_rsq_f32, 0x45800000u);
}

void
emit_sqrt(isel_context* ctx, Builder& bld, Definition dst, Temp val)
{
   if (ctx->block->fp_mode.denorm32 == 0) {
      bld.vop1(aco_opcode::v_sqrt_f32, dst, val);
      return;
   }

   emit_scaled_op(ctx, bld, dst, val, aco_opcode::v_sqrt_f32, 0x39800000u);
}

void
emit_log2(isel_context* ctx, Builder& bld, Definition dst, Temp val)
{
   if (ctx->block->fp_mode.denorm32 == 0) {
      bld.vop1(aco_opcode::v_log_f32, dst, val);
      return;
   }

   emit_scaled_op(ctx, bld, dst, val, aco_opcode::v_log_f32, 0xc1c00000u);
}

Temp
emit_trunc_f64(isel_context* ctx, Builder& bld, Definition dst, Temp val)
{
   if (ctx->options->chip_class >= GFX7)
      return bld.vop1(aco_opcode::v_trunc_f64, Definition(dst), val);

   /* GFX6 doesn't support V_TRUNC_F64, lower it. */
   /* TODO: create more efficient code! */
   if (val.type() == RegType::sgpr)
      val = as_vgpr(ctx, val);

   /* Split the input value. */
   Temp val_lo = bld.tmp(v1), val_hi = bld.tmp(v1);
   bld.pseudo(aco_opcode::p_split_vector, Definition(val_lo), Definition(val_hi), val);

   /* Extract the exponent and compute the unbiased value. */
   Temp exponent =
      bld.vop3(aco_opcode::v_bfe_u32, bld.def(v1), val_hi, Operand::c32(20u), Operand::c32(11u));
   exponent = bld.vsub32(bld.def(v1), exponent, Operand::c32(1023u));

   /* Extract the fractional part. */
   Temp fract_mask = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), Operand::c32(-1u),
                                Operand::c32(0x000fffffu));
   fract_mask = bld.vop3(aco_opcode::v_lshr_b64, bld.def(v2), fract_mask, exponent);

   Temp fract_mask_lo = bld.tmp(v1), fract_mask_hi = bld.tmp(v1);
   bld.pseudo(aco_opcode::p_split_vector, Definition(fract_mask_lo), Definition(fract_mask_hi),
              fract_mask);

   Temp fract_lo = bld.tmp(v1), fract_hi = bld.tmp(v1);
   Temp tmp = bld.vop1(aco_opcode::v_not_b32, bld.def(v1), fract_mask_lo);
   fract_lo = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), val_lo, tmp);
   tmp = bld.vop1(aco_opcode::v_not_b32, bld.def(v1), fract_mask_hi);
   fract_hi = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), val_hi, tmp);

   /* Get the sign bit. */
   Temp sign = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(0x80000000u), val_hi);

   /* Decide the operation to apply depending on the unbiased exponent. */
   Temp exp_lt0 = bld.vopc_e64(aco_opcode::v_cmp_lt_i32, bld.hint_vcc(bld.def(bld.lm)), exponent,
                               Operand::zero());
   Temp dst_lo = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), fract_lo,
                          bld.copy(bld.def(v1), Operand::zero()), exp_lt0);
   Temp dst_hi = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), fract_hi, sign, exp_lt0);
   Temp exp_gt51 = bld.vopc_e64(aco_opcode::v_cmp_gt_i32, bld.def(s2), exponent, Operand::c32(51u));
   dst_lo = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), dst_lo, val_lo, exp_gt51);
   dst_hi = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), dst_hi, val_hi, exp_gt51);

   return bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst_lo, dst_hi);
}

Temp
emit_floor_f64(isel_context* ctx, Builder& bld, Definition dst, Temp val)
{
   if (ctx->options->chip_class >= GFX7)
      return bld.vop1(aco_opcode::v_floor_f64, Definition(dst), val);

   /* GFX6 doesn't support V_FLOOR_F64, lower it (note that it's actually
    * lowered at NIR level for precision reasons). */
   Temp src0 = as_vgpr(ctx, val);

   Temp mask = bld.copy(bld.def(s1), Operand::c32(3u)); /* isnan */
   Temp min_val = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand::c32(-1u),
                             Operand::c32(0x3fefffffu));

   Temp isnan =
      bld.vopc_e64(aco_opcode::v_cmp_class_f64, bld.hint_vcc(bld.def(bld.lm)), src0, mask);
   Temp fract = bld.vop1(aco_opcode::v_fract_f64, bld.def(v2), src0);
   Temp min = bld.vop3(aco_opcode::v_min_f64, bld.def(v2), fract, min_val);

   Temp then_lo = bld.tmp(v1), then_hi = bld.tmp(v1);
   bld.pseudo(aco_opcode::p_split_vector, Definition(then_lo), Definition(then_hi), src0);
   Temp else_lo = bld.tmp(v1), else_hi = bld.tmp(v1);
   bld.pseudo(aco_opcode::p_split_vector, Definition(else_lo), Definition(else_hi), min);

   Temp dst0 = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), else_lo, then_lo, isnan);
   Temp dst1 = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), else_hi, then_hi, isnan);

   Temp v = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), dst0, dst1);

   Instruction* add = bld.vop3(aco_opcode::v_add_f64, Definition(dst), src0, v);
   add->vop3().neg[1] = true;

   return add->definitions[0].getTemp();
}

Temp
uadd32_sat(Builder& bld, Definition dst, Temp src0, Temp src1)
{
   if (bld.program->chip_class < GFX8) {
      Builder::Result add = bld.vadd32(bld.def(v1), src0, src1, true);
      return bld.vop2_e64(aco_opcode::v_cndmask_b32, dst, add.def(0).getTemp(), Operand::c32(-1),
                          add.def(1).getTemp());
   }

   Builder::Result add(NULL);
   if (bld.program->chip_class >= GFX9) {
      add = bld.vop2_e64(aco_opcode::v_add_u32, dst, src0, src1);
   } else {
      add = bld.vop2_e64(aco_opcode::v_add_co_u32, dst, bld.hint_vcc(bld.def(bld.lm)), src0, src1);
   }
   add.instr->vop3().clamp = 1;
   return dst.getTemp();
}

void
visit_alu_instr(isel_context* ctx, nir_alu_instr* instr)
{
   if (!instr->dest.dest.is_ssa) {
      isel_err(&instr->instr, "nir alu dst not in ssa");
      abort();
   }
   Builder bld(ctx->program, ctx->block);
   bld.is_precise = instr->exact;
   Temp dst = get_ssa_temp(ctx, &instr->dest.dest.ssa);
   switch (instr->op) {
   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
   case nir_op_vec5: {
      std::array<Temp, NIR_MAX_VEC_COMPONENTS> elems;
      unsigned num = instr->dest.dest.ssa.num_components;
      for (unsigned i = 0; i < num; ++i)
         elems[i] = get_alu_src(ctx, instr->src[i]);

      if (instr->dest.dest.ssa.bit_size >= 32 || dst.type() == RegType::vgpr) {
         aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(
            aco_opcode::p_create_vector, Format::PSEUDO, instr->dest.dest.ssa.num_components, 1)};
         RegClass elem_rc = RegClass::get(RegType::vgpr, instr->dest.dest.ssa.bit_size / 8u);
         for (unsigned i = 0; i < num; ++i) {
            if (elems[i].type() == RegType::sgpr && elem_rc.is_subdword())
               elems[i] = emit_extract_vector(ctx, elems[i], 0, elem_rc);
            vec->operands[i] = Operand{elems[i]};
         }
         vec->definitions[0] = Definition(dst);
         ctx->block->instructions.emplace_back(std::move(vec));
         ctx->allocated_vec.emplace(dst.id(), elems);
      } else {
         bool use_s_pack = ctx->program->chip_class >= GFX9;
         Temp mask = bld.copy(bld.def(s1), Operand::c32((1u << instr->dest.dest.ssa.bit_size) - 1));

         std::array<Temp, NIR_MAX_VEC_COMPONENTS> packed;
         uint32_t const_vals[NIR_MAX_VEC_COMPONENTS] = {};
         for (unsigned i = 0; i < num; i++) {
            unsigned packed_size = use_s_pack ? 16 : 32;
            unsigned idx = i * instr->dest.dest.ssa.bit_size / packed_size;
            unsigned offset = i * instr->dest.dest.ssa.bit_size % packed_size;
            if (nir_src_is_const(instr->src[i].src)) {
               const_vals[idx] |= nir_src_as_uint(instr->src[i].src) << offset;
               continue;
            }

            if (offset != packed_size - instr->dest.dest.ssa.bit_size)
               elems[i] =
                  bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), elems[i], mask);

            if (offset)
               elems[i] = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), elems[i],
                                   Operand::c32(offset));

            if (packed[idx].id())
               packed[idx] = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), elems[i],
                                      packed[idx]);
            else
               packed[idx] = elems[i];
         }

         if (use_s_pack) {
            for (unsigned i = 0; i < dst.size(); i++) {
               bool same = !!packed[i * 2].id() == !!packed[i * 2 + 1].id();

               if (packed[i * 2].id() && packed[i * 2 + 1].id())
                  packed[i] = bld.sop2(aco_opcode::s_pack_ll_b32_b16, bld.def(s1), packed[i * 2],
                                       packed[i * 2 + 1]);
               else if (packed[i * 2 + 1].id())
                  packed[i] = bld.sop2(aco_opcode::s_pack_ll_b32_b16, bld.def(s1),
                                       Operand::c32(const_vals[i * 2]), packed[i * 2 + 1]);
               else if (packed[i * 2].id())
                  packed[i] = bld.sop2(aco_opcode::s_pack_ll_b32_b16, bld.def(s1), packed[i * 2],
                                       Operand::c32(const_vals[i * 2 + 1]));

               if (same)
                  const_vals[i] = const_vals[i * 2] | (const_vals[i * 2 + 1] << 16);
               else
                  const_vals[i] = 0;
            }
         }

         for (unsigned i = 0; i < dst.size(); i++) {
            if (const_vals[i] && packed[i].id())
               packed[i] = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc),
                                    Operand::c32(const_vals[i]), packed[i]);
            else if (!packed[i].id())
               packed[i] = bld.copy(bld.def(s1), Operand::c32(const_vals[i]));
         }

         if (dst.size() == 1)
            bld.copy(Definition(dst), packed[0]);
         else if (dst.size() == 2)
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), packed[0], packed[1]);
         else
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), packed[0], packed[1],
                       packed[2]);
      }
      break;
   }
   case nir_op_mov: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (src.type() == RegType::vgpr && dst.type() == RegType::sgpr) {
         /* use size() instead of bytes() for 8/16-bit */
         assert(src.size() == dst.size() && "wrong src or dst register class for nir_op_mov");
         bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), src);
      } else {
         assert(src.bytes() == dst.bytes() && "wrong src or dst register class for nir_op_mov");
         bld.copy(Definition(dst), src);
      }
      break;
   }
   case nir_op_inot: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == v1 || dst.regClass() == v2b || dst.regClass() == v1b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_not_b32, dst);
      } else if (dst.regClass() == v2) {
         Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
         lo = bld.vop1(aco_opcode::v_not_b32, bld.def(v1), lo);
         hi = bld.vop1(aco_opcode::v_not_b32, bld.def(v1), hi);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
      } else if (dst.type() == RegType::sgpr) {
         aco_opcode opcode = dst.size() == 1 ? aco_opcode::s_not_b32 : aco_opcode::s_not_b64;
         bld.sop1(opcode, Definition(dst), bld.def(s1, scc), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_iabs: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == s1) {
         bld.sop1(aco_opcode::s_abs_i32, Definition(dst), bld.def(s1, scc), src);
      } else if (dst.regClass() == v1) {
         bld.vop2(aco_opcode::v_max_i32, Definition(dst), src,
                  bld.vsub32(bld.def(v1), Operand::zero(), src));
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_isign: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == s1) {
         Temp tmp =
            bld.sop2(aco_opcode::s_max_i32, bld.def(s1), bld.def(s1, scc), src, Operand::c32(-1));
         bld.sop2(aco_opcode::s_min_i32, Definition(dst), bld.def(s1, scc), tmp, Operand::c32(1u));
      } else if (dst.regClass() == s2) {
         Temp neg =
            bld.sop2(aco_opcode::s_ashr_i64, bld.def(s2), bld.def(s1, scc), src, Operand::c32(63u));
         Temp neqz;
         if (ctx->program->chip_class >= GFX8)
            neqz = bld.sopc(aco_opcode::s_cmp_lg_u64, bld.def(s1, scc), src, Operand::zero());
         else
            neqz =
               bld.sop2(aco_opcode::s_or_b64, bld.def(s2), bld.def(s1, scc), src, Operand::zero())
                  .def(1)
                  .getTemp();
         /* SCC gets zero-extended to 64 bit */
         bld.sop2(aco_opcode::s_or_b64, Definition(dst), bld.def(s1, scc), neg, bld.scc(neqz));
      } else if (dst.regClass() == v1) {
         bld.vop3(aco_opcode::v_med3_i32, Definition(dst), Operand::c32(-1), src, Operand::c32(1u));
      } else if (dst.regClass() == v2) {
         Temp upper = emit_extract_vector(ctx, src, 1, v1);
         Temp neg = bld.vop2(aco_opcode::v_ashrrev_i32, bld.def(v1), Operand::c32(31u), upper);
         Temp gtz =
            bld.vopc(aco_opcode::v_cmp_ge_i64, bld.hint_vcc(bld.def(bld.lm)), Operand::zero(), src);
         Temp lower = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::c32(1u), neg, gtz);
         upper = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::zero(), neg, gtz);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_imax: {
      if (dst.regClass() == v2b && ctx->program->chip_class >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_max_i16_e64, dst);
      } else if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_i16, dst, true);
      } else if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_max_i16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_i32, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_max_i32, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_umax: {
      if (dst.regClass() == v2b && ctx->program->chip_class >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_max_u16_e64, dst);
      } else if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_u16, dst, true);
      } else if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_max_u16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_u32, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_max_u32, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_imin: {
      if (dst.regClass() == v2b && ctx->program->chip_class >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_min_i16_e64, dst);
      } else if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_i16, dst, true);
      } else if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_min_i16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_i32, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_min_i32, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_umin: {
      if (dst.regClass() == v2b && ctx->program->chip_class >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_min_u16_e64, dst);
      } else if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_u16, dst, true);
      } else if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_min_u16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_u32, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_min_u32, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ior: {
      if (instr->dest.dest.ssa.bit_size == 1) {
         emit_boolean_logic(ctx, instr, Builder::s_or, dst);
      } else if (dst.regClass() == v1 || dst.regClass() == v2b || dst.regClass() == v1b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_or_b32, dst, true);
      } else if (dst.regClass() == v2) {
         emit_vop2_instruction_logic64(ctx, instr, aco_opcode::v_or_b32, dst);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_or_b32, dst, true);
      } else if (dst.regClass() == s2) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_or_b64, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_iand: {
      if (instr->dest.dest.ssa.bit_size == 1) {
         emit_boolean_logic(ctx, instr, Builder::s_and, dst);
      } else if (dst.regClass() == v1 || dst.regClass() == v2b || dst.regClass() == v1b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_and_b32, dst, true);
      } else if (dst.regClass() == v2) {
         emit_vop2_instruction_logic64(ctx, instr, aco_opcode::v_and_b32, dst);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_and_b32, dst, true);
      } else if (dst.regClass() == s2) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_and_b64, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ixor: {
      if (instr->dest.dest.ssa.bit_size == 1) {
         emit_boolean_logic(ctx, instr, Builder::s_xor, dst);
      } else if (dst.regClass() == v1 || dst.regClass() == v2b || dst.regClass() == v1b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_xor_b32, dst, true);
      } else if (dst.regClass() == v2) {
         emit_vop2_instruction_logic64(ctx, instr, aco_opcode::v_xor_b32, dst);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_xor_b32, dst, true);
      } else if (dst.regClass() == s2) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_xor_b64, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ushr: {
      if (dst.regClass() == v2b && ctx->program->chip_class >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_lshrrev_b16_e64, dst, false, 2, true);
      } else if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_lshrrev_b16, dst, false, true);
      } else if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_lshrrev_b16, dst, true);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_lshrrev_b32, dst, false, true);
      } else if (dst.regClass() == v2 && ctx->program->chip_class >= GFX8) {
         bld.vop3(aco_opcode::v_lshrrev_b64, Definition(dst), get_alu_src(ctx, instr->src[1]),
                  get_alu_src(ctx, instr->src[0]));
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_lshr_b64, dst);
      } else if (dst.regClass() == s2) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_lshr_b64, dst, true);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_lshr_b32, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ishl: {
      if (dst.regClass() == v2b && ctx->program->chip_class >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_lshlrev_b16_e64, dst, false, 2, true);
      } else if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_lshlrev_b16, dst, false, true);
      } else if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_lshlrev_b16, dst, true);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_lshlrev_b32, dst, false, true, false,
                               false, 1);
      } else if (dst.regClass() == v2 && ctx->program->chip_class >= GFX8) {
         bld.vop3(aco_opcode::v_lshlrev_b64, Definition(dst), get_alu_src(ctx, instr->src[1]),
                  get_alu_src(ctx, instr->src[0]));
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_lshl_b64, dst);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_lshl_b32, dst, true, 1);
      } else if (dst.regClass() == s2) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_lshl_b64, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ishr: {
      if (dst.regClass() == v2b && ctx->program->chip_class >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_ashrrev_i16_e64, dst, false, 2, true);
      } else if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_ashrrev_i16, dst, false, true);
      } else if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_ashrrev_i16, dst, true);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_ashrrev_i32, dst, false, true);
      } else if (dst.regClass() == v2 && ctx->program->chip_class >= GFX8) {
         bld.vop3(aco_opcode::v_ashrrev_i64, Definition(dst), get_alu_src(ctx, instr->src[1]),
                  get_alu_src(ctx, instr->src[0]));
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_ashr_i64, dst);
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_ashr_i32, dst, true);
      } else if (dst.regClass() == s2) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_ashr_i64, dst, true);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
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
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ufind_msb:
   case nir_op_ifind_msb: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (src.regClass() == s1 || src.regClass() == s2) {
         aco_opcode op = src.regClass() == s2
                            ? (instr->op == nir_op_ufind_msb ? aco_opcode::s_flbit_i32_b64
                                                             : aco_opcode::s_flbit_i32_i64)
                            : (instr->op == nir_op_ufind_msb ? aco_opcode::s_flbit_i32_b32
                                                             : aco_opcode::s_flbit_i32);
         Temp msb_rev = bld.sop1(op, bld.def(s1), src);

         Builder::Result sub = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.def(s1, scc),
                                        Operand::c32(src.size() * 32u - 1u), msb_rev);
         Temp msb = sub.def(0).getTemp();
         Temp carry = sub.def(1).getTemp();

         bld.sop2(aco_opcode::s_cselect_b32, Definition(dst), Operand::c32(-1), msb,
                  bld.scc(carry));
      } else if (src.regClass() == v1) {
         aco_opcode op =
            instr->op == nir_op_ufind_msb ? aco_opcode::v_ffbh_u32 : aco_opcode::v_ffbh_i32;
         Temp msb_rev = bld.tmp(v1);
         emit_vop1_instruction(ctx, instr, op, msb_rev);
         Temp msb = bld.tmp(v1);
         Temp carry =
            bld.vsub32(Definition(msb), Operand::c32(31u), Operand(msb_rev), true).def(1).getTemp();
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), msb, Operand::c32(-1), carry);
      } else if (src.regClass() == v2) {
         aco_opcode op =
            instr->op == nir_op_ufind_msb ? aco_opcode::v_ffbh_u32 : aco_opcode::v_ffbh_i32;

         Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);

         lo = uadd32_sat(bld, bld.def(v1), bld.copy(bld.def(s1), Operand::c32(32u)),
                         bld.vop1(op, bld.def(v1), lo));
         hi = bld.vop1(op, bld.def(v1), hi);
         Temp found_hi = bld.vopc(aco_opcode::v_cmp_lg_u32, bld.def(bld.lm), Operand::c32(-1), hi);

         Temp msb_rev = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), lo, hi, found_hi);

         Temp msb = bld.tmp(v1);
         Temp carry =
            bld.vsub32(Definition(msb), Operand::c32(63u), Operand(msb_rev), true).def(1).getTemp();
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), msb, Operand::c32(-1), carry);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_bitfield_reverse: {
      if (dst.regClass() == s1) {
         bld.sop1(aco_opcode::s_brev_b32, Definition(dst), get_alu_src(ctx, instr->src[0]));
      } else if (dst.regClass() == v1) {
         bld.vop1(aco_opcode::v_bfrev_b32, Definition(dst), get_alu_src(ctx, instr->src[0]));
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_iadd: {
      if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_add_u32, dst, true);
         break;
      } else if (dst.bytes() <= 2 && ctx->program->chip_class >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_add_u16_e64, dst);
         break;
      } else if (dst.bytes() <= 2 && ctx->program->chip_class >= GFX8) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_add_u16, dst, true);
         break;
      } else if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_add_u16, dst);
         break;
      }

      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.type() == RegType::vgpr && dst.bytes() <= 4) {
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
         Temp dst0 =
            bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.scc(Definition(carry)), src00, src10);
         Temp dst1 = bld.sop2(aco_opcode::s_addc_u32, bld.def(s1), bld.def(s1, scc), src01, src11,
                              bld.scc(carry));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
      } else if (dst.regClass() == v2) {
         Temp dst0 = bld.tmp(v1);
         Temp carry = bld.vadd32(Definition(dst0), src00, src10, true).def(1).getTemp();
         Temp dst1 = bld.vadd32(bld.def(v1), src01, src11, false, carry);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_uadd_sat: {
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == s1) {
         Temp tmp = bld.tmp(s1), carry = bld.tmp(s1);
         bld.sop2(aco_opcode::s_add_u32, Definition(tmp), bld.scc(Definition(carry)), src0, src1);
         bld.sop2(aco_opcode::s_cselect_b32, Definition(dst), Operand::c32(-1), tmp,
                  bld.scc(carry));
      } else if (dst.regClass() == v2b) {
         Instruction* add_instr;
         if (ctx->program->chip_class >= GFX10) {
            add_instr = bld.vop3(aco_opcode::v_add_u16_e64, Definition(dst), src0, src1).instr;
         } else {
            if (src1.type() == RegType::sgpr)
               std::swap(src0, src1);
            add_instr =
               bld.vop2_e64(aco_opcode::v_add_u16, Definition(dst), src0, as_vgpr(ctx, src1)).instr;
         }
         add_instr->vop3().clamp = 1;
      } else if (dst.regClass() == v1) {
         uadd32_sat(bld, Definition(dst), src0, src1);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_iadd_sat: {
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = as_vgpr(ctx, get_alu_src(ctx, instr->src[1]));
      if (dst.regClass() == v2b) {
         Instruction* add_instr =
            bld.vop3(aco_opcode::v_add_i16, Definition(dst), src0, src1).instr;
         add_instr->vop3().clamp = 1;
      } else if (dst.regClass() == v1) {
         Instruction* add_instr =
            bld.vop3(aco_opcode::v_add_i32, Definition(dst), src0, src1).instr;
         add_instr->vop3().clamp = 1;
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
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
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), Operand::zero(), Operand::c32(1u),
                      carry);
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
         carry = bld.sop2(aco_opcode::s_addc_u32, bld.def(s1), bld.scc(bld.def(s1)), src01, src11,
                          bld.scc(carry))
                    .def(1)
                    .getTemp();
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), carry, Operand::zero());
      } else if (dst.regClass() == v2) {
         Temp carry = bld.vadd32(bld.def(v1), src00, src10, true).def(1).getTemp();
         carry = bld.vadd32(bld.def(v1), src01, src11, true, carry).def(1).getTemp();
         carry = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::zero(),
                              Operand::c32(1u), carry);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), carry, Operand::zero());
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_isub: {
      if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_sub_i32, dst, true);
         break;
      } else if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_sub_u16, dst);
         break;
      }

      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == v1) {
         bld.vsub32(Definition(dst), src0, src1);
         break;
      } else if (dst.bytes() <= 2) {
         if (ctx->program->chip_class >= GFX10)
            bld.vop3(aco_opcode::v_sub_u16_e64, Definition(dst), src0, src1);
         else if (src1.type() == RegType::sgpr)
            bld.vop2(aco_opcode::v_subrev_u16, Definition(dst), src1, as_vgpr(ctx, src0));
         else if (ctx->program->chip_class >= GFX8)
            bld.vop2(aco_opcode::v_sub_u16, Definition(dst), src0, as_vgpr(ctx, src1));
         else
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
         Temp borrow = bld.tmp(s1);
         Temp dst0 =
            bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.scc(Definition(borrow)), src00, src10);
         Temp dst1 = bld.sop2(aco_opcode::s_subb_u32, bld.def(s1), bld.def(s1, scc), src01, src11,
                              bld.scc(borrow));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
      } else if (dst.regClass() == v2) {
         Temp lower = bld.tmp(v1);
         Temp borrow = bld.vsub32(Definition(lower), src00, src10, true).def(1).getTemp();
         Temp upper = bld.vsub32(bld.def(v1), src01, src11, false, borrow);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
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
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), Operand::zero(), Operand::c32(1u),
                      borrow);
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
         borrow = bld.sop2(aco_opcode::s_subb_u32, bld.def(s1), bld.scc(bld.def(s1)), src01, src11,
                           bld.scc(borrow))
                     .def(1)
                     .getTemp();
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), borrow, Operand::zero());
      } else if (dst.regClass() == v2) {
         Temp borrow = bld.vsub32(bld.def(v1), src00, src10, true).def(1).getTemp();
         borrow = bld.vsub32(bld.def(v1), src01, src11, true, Operand(borrow)).def(1).getTemp();
         borrow = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::zero(),
                               Operand::c32(1u), borrow);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), borrow, Operand::zero());
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_imul: {
      if (dst.bytes() <= 2 && ctx->program->chip_class >= GFX10) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_mul_lo_u16_e64, dst);
      } else if (dst.bytes() <= 2 && ctx->program->chip_class >= GFX8) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_mul_lo_u16, dst, true);
      } else if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_mul_lo_u16, dst);
      } else if (dst.type() == RegType::vgpr) {
         uint32_t src0_ub = get_alu_src_ub(ctx, instr, 0);
         uint32_t src1_ub = get_alu_src_ub(ctx, instr, 1);

         if (src0_ub <= 0xffffff && src1_ub <= 0xffffff) {
            bool nuw_16bit = src0_ub <= 0xffff && src1_ub <= 0xffff && src0_ub * src1_ub <= 0xffff;
            emit_vop2_instruction(ctx, instr, aco_opcode::v_mul_u32_u24, dst,
                                  true /* commutative */, false, false, nuw_16bit);
         } else if (nir_src_is_const(instr->src[0].src)) {
            bld.v_mul_imm(Definition(dst), get_alu_src(ctx, instr->src[1]),
                          nir_src_as_uint(instr->src[0].src), false);
         } else if (nir_src_is_const(instr->src[1].src)) {
            bld.v_mul_imm(Definition(dst), get_alu_src(ctx, instr->src[0]),
                          nir_src_as_uint(instr->src[1].src), false);
         } else {
            emit_vop3a_instruction(ctx, instr, aco_opcode::v_mul_lo_u32, dst);
         }
      } else if (dst.regClass() == s1) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_mul_i32, dst, false);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_umul_high: {
      if (dst.regClass() == s1 && ctx->options->chip_class >= GFX9) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_mul_hi_u32, dst, false);
      } else if (dst.bytes() == 4) {
         uint32_t src0_ub = get_alu_src_ub(ctx, instr, 0);
         uint32_t src1_ub = get_alu_src_ub(ctx, instr, 1);

         Temp tmp = dst.regClass() == s1 ? bld.tmp(v1) : dst;
         if (src0_ub <= 0xffffff && src1_ub <= 0xffffff) {
            emit_vop2_instruction(ctx, instr, aco_opcode::v_mul_hi_u32_u24, tmp, true);
         } else {
            emit_vop3a_instruction(ctx, instr, aco_opcode::v_mul_hi_u32, tmp);
         }

         if (dst.regClass() == s1)
            bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), tmp);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_imul_high: {
      if (dst.regClass() == v1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_mul_hi_i32, dst);
      } else if (dst.regClass() == s1 && ctx->options->chip_class >= GFX9) {
         emit_sop2_instruction(ctx, instr, aco_opcode::s_mul_hi_i32, dst, false);
      } else if (dst.regClass() == s1) {
         Temp tmp = bld.vop3(aco_opcode::v_mul_hi_i32, bld.def(v1), get_alu_src(ctx, instr->src[0]),
                             as_vgpr(ctx, get_alu_src(ctx, instr->src[1])));
         bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), tmp);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fmul: {
      if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_mul_f16, dst, true);
      } else if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_mul_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_mul_f32, dst, true);
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_mul_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fadd: {
      if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_add_f16, dst, true);
      } else if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_add_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_add_f32, dst, true);
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_add_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fsub: {
      if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         Instruction* add = emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_add_f16, dst);
         VOP3P_instruction& sub = add->vop3p();
         sub.neg_lo[1] = true;
         sub.neg_hi[1] = true;
         break;
      }

      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == v2b) {
         if (src1.type() == RegType::vgpr || src0.type() != RegType::vgpr)
            emit_vop2_instruction(ctx, instr, aco_opcode::v_sub_f16, dst, false);
         else
            emit_vop2_instruction(ctx, instr, aco_opcode::v_subrev_f16, dst, true);
      } else if (dst.regClass() == v1) {
         if (src1.type() == RegType::vgpr || src0.type() != RegType::vgpr)
            emit_vop2_instruction(ctx, instr, aco_opcode::v_sub_f32, dst, false);
         else
            emit_vop2_instruction(ctx, instr, aco_opcode::v_subrev_f32, dst, true);
      } else if (dst.regClass() == v2) {
         Instruction* add = bld.vop3(aco_opcode::v_add_f64, Definition(dst), as_vgpr(ctx, src0),
                                     as_vgpr(ctx, src1));
         add->vop3().neg[1] = true;
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fmax: {
      if (dst.regClass() == v2b) {
         // TODO: check fp_mode.must_flush_denorms16_64
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_f16, dst, true);
      } else if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_max_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_max_f32, dst, true, false,
                               ctx->block->fp_mode.must_flush_denorms32);
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_max_f64, dst,
                                ctx->block->fp_mode.must_flush_denorms16_64);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fmin: {
      if (dst.regClass() == v2b) {
         // TODO: check fp_mode.must_flush_denorms16_64
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_f16, dst, true);
      } else if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         emit_vop3p_instruction(ctx, instr, aco_opcode::v_pk_min_f16, dst, true);
      } else if (dst.regClass() == v1) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_min_f32, dst, true, false,
                               ctx->block->fp_mode.must_flush_denorms32);
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_min_f64, dst,
                                ctx->block->fp_mode.must_flush_denorms16_64);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_sdot_4x8_iadd: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot4_i32_i8, dst, false);
      break;
   }
   case nir_op_sdot_4x8_iadd_sat: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot4_i32_i8, dst, true);
      break;
   }
   case nir_op_udot_4x8_uadd: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot4_u32_u8, dst, false);
      break;
   }
   case nir_op_udot_4x8_uadd_sat: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot4_u32_u8, dst, true);
      break;
   }
   case nir_op_sdot_2x16_iadd: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot2_i32_i16, dst, false);
      break;
   }
   case nir_op_sdot_2x16_iadd_sat: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot2_i32_i16, dst, true);
      break;
   }
   case nir_op_udot_2x16_uadd: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot2_u32_u16, dst, false);
      break;
   }
   case nir_op_udot_2x16_uadd_sat: {
      emit_idot_instruction(ctx, instr, aco_opcode::v_dot2_u32_u16, dst, true);
      break;
   }
   case nir_op_cube_face_coord_amd: {
      Temp in = get_alu_src(ctx, instr->src[0], 3);
      Temp src[3] = {emit_extract_vector(ctx, in, 0, v1), emit_extract_vector(ctx, in, 1, v1),
                     emit_extract_vector(ctx, in, 2, v1)};
      Temp ma = bld.vop3(aco_opcode::v_cubema_f32, bld.def(v1), src[0], src[1], src[2]);
      ma = bld.vop1(aco_opcode::v_rcp_f32, bld.def(v1), ma);
      Temp sc = bld.vop3(aco_opcode::v_cubesc_f32, bld.def(v1), src[0], src[1], src[2]);
      Temp tc = bld.vop3(aco_opcode::v_cubetc_f32, bld.def(v1), src[0], src[1], src[2]);
      sc = bld.vop2(aco_opcode::v_add_f32, bld.def(v1), Operand::c32(0x3f000000u /*0.5*/),
                    bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), sc, ma));
      tc = bld.vop2(aco_opcode::v_add_f32, bld.def(v1), Operand::c32(0x3f000000u /*0.5*/),
                    bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), tc, ma));
      bld.pseudo(aco_opcode::p_create_vector, Definition(dst), sc, tc);
      break;
   }
   case nir_op_cube_face_index_amd: {
      Temp in = get_alu_src(ctx, instr->src[0], 3);
      Temp src[3] = {emit_extract_vector(ctx, in, 0, v1), emit_extract_vector(ctx, in, 1, v1),
                     emit_extract_vector(ctx, in, 2, v1)};
      bld.vop3(aco_opcode::v_cubeid_f32, Definition(dst), src[0], src[1], src[2]);
      break;
   }
   case nir_op_bcsel: {
      emit_bcsel(ctx, instr, dst);
      break;
   }
   case nir_op_frsq: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rsq_f16, dst);
      } else if (dst.regClass() == v1) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         emit_rsq(ctx, bld, Definition(dst), src);
      } else if (dst.regClass() == v2) {
         /* Lowered at NIR level for precision reasons. */
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rsq_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fneg: {
      if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         Temp src = get_alu_src_vop3p(ctx, instr->src[0]);
         bld.vop3p(aco_opcode::v_pk_mul_f16, Definition(dst), src, Operand::c16(0xBC00),
                   instr->src[0].swizzle[0] & 1, instr->src[0].swizzle[1] & 1);
         emit_split_vector(ctx, dst, 2);
         break;
      }
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == v2b) {
         bld.vop2(aco_opcode::v_mul_f16, Definition(dst), Operand::c16(0xbc00u), as_vgpr(ctx, src));
      } else if (dst.regClass() == v1) {
         bld.vop2(aco_opcode::v_mul_f32, Definition(dst), Operand::c32(0xbf800000u),
                  as_vgpr(ctx, src));
      } else if (dst.regClass() == v2) {
         if (ctx->block->fp_mode.must_flush_denorms16_64)
            src = bld.vop3(aco_opcode::v_mul_f64, bld.def(v2), Operand::c64(0x3FF0000000000000),
                           as_vgpr(ctx, src));
         Temp upper = bld.tmp(v1), lower = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), src);
         upper = bld.vop2(aco_opcode::v_xor_b32, bld.def(v1), Operand::c32(0x80000000u), upper);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fabs: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == v2b) {
         Instruction* mul = bld.vop2_e64(aco_opcode::v_mul_f16, Definition(dst),
                                         Operand::c16(0x3c00), as_vgpr(ctx, src))
                               .instr;
         mul->vop3().abs[1] = true;
      } else if (dst.regClass() == v1) {
         Instruction* mul = bld.vop2_e64(aco_opcode::v_mul_f32, Definition(dst),
                                         Operand::c32(0x3f800000u), as_vgpr(ctx, src))
                               .instr;
         mul->vop3().abs[1] = true;
      } else if (dst.regClass() == v2) {
         if (ctx->block->fp_mode.must_flush_denorms16_64)
            src = bld.vop3(aco_opcode::v_mul_f64, bld.def(v2), Operand::c64(0x3FF0000000000000),
                           as_vgpr(ctx, src));
         Temp upper = bld.tmp(v1), lower = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), src);
         upper = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(0x7FFFFFFFu), upper);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fsat: {
      if (dst.regClass() == v1 && instr->dest.dest.ssa.bit_size == 16) {
         Temp src = get_alu_src_vop3p(ctx, instr->src[0]);
         Instruction* vop3p =
            bld.vop3p(aco_opcode::v_pk_mul_f16, Definition(dst), src, Operand::c16(0x3C00),
                      instr->src[0].swizzle[0] & 1, instr->src[0].swizzle[1] & 1);
         vop3p->vop3p().clamp = true;
         emit_split_vector(ctx, dst, 2);
         break;
      }
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (dst.regClass() == v2b) {
         bld.vop3(aco_opcode::v_med3_f16, Definition(dst), Operand::c16(0u), Operand::c16(0x3c00),
                  src);
      } else if (dst.regClass() == v1) {
         bld.vop3(aco_opcode::v_med3_f32, Definition(dst), Operand::zero(),
                  Operand::c32(0x3f800000u), src);
         /* apparently, it is not necessary to flush denorms if this instruction is used with these
          * operands */
         // TODO: confirm that this holds under any circumstances
      } else if (dst.regClass() == v2) {
         Instruction* add = bld.vop3(aco_opcode::v_add_f64, Definition(dst), src, Operand::zero());
         add->vop3().clamp = true;
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_flog2: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_log_f16, dst);
      } else if (dst.regClass() == v1) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         emit_log2(ctx, bld, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_frcp: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rcp_f16, dst);
      } else if (dst.regClass() == v1) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         emit_rcp(ctx, bld, Definition(dst), src);
      } else if (dst.regClass() == v2) {
         /* Lowered at NIR level for precision reasons. */
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rcp_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fexp2: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_exp_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_exp_f32, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fsqrt: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_sqrt_f16, dst);
      } else if (dst.regClass() == v1) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         emit_sqrt(ctx, bld, Definition(dst), src);
      } else if (dst.regClass() == v2) {
         /* Lowered at NIR level for precision reasons. */
         emit_vop1_instruction(ctx, instr, aco_opcode::v_sqrt_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ffract: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_fract_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_fract_f32, dst);
      } else if (dst.regClass() == v2) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_fract_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ffloor: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_floor_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_floor_f32, dst);
      } else if (dst.regClass() == v2) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         emit_floor_f64(ctx, bld, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fceil: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_ceil_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_ceil_f32, dst);
      } else if (dst.regClass() == v2) {
         if (ctx->options->chip_class >= GFX7) {
            emit_vop1_instruction(ctx, instr, aco_opcode::v_ceil_f64, dst);
         } else {
            /* GFX6 doesn't support V_CEIL_F64, lower it. */
            /* trunc = trunc(src0)
             * if (src0 > 0.0 && src0 != trunc)
             *    trunc += 1.0
             */
            Temp src0 = get_alu_src(ctx, instr->src[0]);
            Temp trunc = emit_trunc_f64(ctx, bld, bld.def(v2), src0);
            Temp tmp0 =
               bld.vopc_e64(aco_opcode::v_cmp_gt_f64, bld.def(bld.lm), src0, Operand::zero());
            Temp tmp1 =
               bld.vopc(aco_opcode::v_cmp_lg_f64, bld.hint_vcc(bld.def(bld.lm)), src0, trunc);
            Temp cond = bld.sop2(aco_opcode::s_and_b64, bld.hint_vcc(bld.def(s2)), bld.def(s1, scc),
                                 tmp0, tmp1);
            Temp add = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1),
                                bld.copy(bld.def(v1), Operand::zero()),
                                bld.copy(bld.def(v1), Operand::c32(0x3ff00000u)), cond);
            add = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2),
                             bld.copy(bld.def(v1), Operand::zero()), add);
            bld.vop3(aco_opcode::v_add_f64, Definition(dst), trunc, add);
         }
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ftrunc: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_trunc_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_trunc_f32, dst);
      } else if (dst.regClass() == v2) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         emit_trunc_f64(ctx, bld, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fround_even: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rndne_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_rndne_f32, dst);
      } else if (dst.regClass() == v2) {
         if (ctx->options->chip_class >= GFX7) {
            emit_vop1_instruction(ctx, instr, aco_opcode::v_rndne_f64, dst);
         } else {
            /* GFX6 doesn't support V_RNDNE_F64, lower it. */
            Temp src0_lo = bld.tmp(v1), src0_hi = bld.tmp(v1);
            Temp src0 = get_alu_src(ctx, instr->src[0]);
            bld.pseudo(aco_opcode::p_split_vector, Definition(src0_lo), Definition(src0_hi), src0);

            Temp bitmask = bld.sop1(aco_opcode::s_brev_b32, bld.def(s1),
                                    bld.copy(bld.def(s1), Operand::c32(-2u)));
            Temp bfi =
               bld.vop3(aco_opcode::v_bfi_b32, bld.def(v1), bitmask,
                        bld.copy(bld.def(v1), Operand::c32(0x43300000u)), as_vgpr(ctx, src0_hi));
            Temp tmp =
               bld.vop3(aco_opcode::v_add_f64, bld.def(v2), src0,
                        bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), Operand::zero(), bfi));
            Instruction* sub =
               bld.vop3(aco_opcode::v_add_f64, bld.def(v2), tmp,
                        bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), Operand::zero(), bfi));
            sub->vop3().neg[1] = true;
            tmp = sub->definitions[0].getTemp();

            Temp v = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), Operand::c32(-1u),
                                Operand::c32(0x432fffffu));
            Instruction* vop3 =
               bld.vopc_e64(aco_opcode::v_cmp_gt_f64, bld.hint_vcc(bld.def(bld.lm)), src0, v);
            vop3->vop3().abs[0] = true;
            Temp cond = vop3->definitions[0].getTemp();

            Temp tmp_lo = bld.tmp(v1), tmp_hi = bld.tmp(v1);
            bld.pseudo(aco_opcode::p_split_vector, Definition(tmp_lo), Definition(tmp_hi), tmp);
            Temp dst0 = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), tmp_lo,
                                     as_vgpr(ctx, src0_lo), cond);
            Temp dst1 = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), tmp_hi,
                                     as_vgpr(ctx, src0_hi), cond);

            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), dst0, dst1);
         }
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fsin:
   case nir_op_fcos: {
      Temp src = as_vgpr(ctx, get_alu_src(ctx, instr->src[0]));
      aco_ptr<Instruction> norm;
      if (dst.regClass() == v2b) {
         Temp half_pi = bld.copy(bld.def(s1), Operand::c32(0x3118u));
         Temp tmp = bld.vop2(aco_opcode::v_mul_f16, bld.def(v1), half_pi, src);
         aco_opcode opcode =
            instr->op == nir_op_fsin ? aco_opcode::v_sin_f16 : aco_opcode::v_cos_f16;
         bld.vop1(opcode, Definition(dst), tmp);
      } else if (dst.regClass() == v1) {
         Temp half_pi = bld.copy(bld.def(s1), Operand::c32(0x3e22f983u));
         Temp tmp = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), half_pi, src);

         /* before GFX9, v_sin_f32 and v_cos_f32 had a valid input domain of [-256, +256] */
         if (ctx->options->chip_class < GFX9)
            tmp = bld.vop1(aco_opcode::v_fract_f32, bld.def(v1), tmp);

         aco_opcode opcode =
            instr->op == nir_op_fsin ? aco_opcode::v_sin_f32 : aco_opcode::v_cos_f32;
         bld.vop1(opcode, Definition(dst), tmp);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ldexp: {
      if (dst.regClass() == v2b) {
         emit_vop2_instruction(ctx, instr, aco_opcode::v_ldexp_f16, dst, false);
      } else if (dst.regClass() == v1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_ldexp_f32, dst);
      } else if (dst.regClass() == v2) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_ldexp_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_frexp_sig: {
      if (dst.regClass() == v2b) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_frexp_mant_f16, dst);
      } else if (dst.regClass() == v1) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_frexp_mant_f32, dst);
      } else if (dst.regClass() == v2) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_frexp_mant_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_frexp_exp: {
      if (instr->src[0].src.ssa->bit_size == 16) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         Temp tmp = bld.vop1(aco_opcode::v_frexp_exp_i16_f16, bld.def(v1), src);
         tmp = bld.pseudo(aco_opcode::p_extract_vector, bld.def(v1b), tmp, Operand::zero());
         convert_int(ctx, bld, tmp, 8, 32, true, dst);
      } else if (instr->src[0].src.ssa->bit_size == 32) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_frexp_exp_i32_f32, dst);
      } else if (instr->src[0].src.ssa->bit_size == 64) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_frexp_exp_i32_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_fsign: {
      Temp src = as_vgpr(ctx, get_alu_src(ctx, instr->src[0]));
      if (dst.regClass() == v2b) {
         assert(ctx->program->chip_class >= GFX9);
         /* replace negative zero with positive zero */
         src = bld.vop2(aco_opcode::v_add_f16, bld.def(v2b), Operand::zero(), src);
         src =
            bld.vop3(aco_opcode::v_med3_i16, bld.def(v2b), Operand::c16(-1), src, Operand::c16(1u));
         bld.vop1(aco_opcode::v_cvt_f16_i16, Definition(dst), src);
      } else if (dst.regClass() == v1) {
         src = bld.vop2(aco_opcode::v_add_f32, bld.def(v1), Operand::zero(), src);
         src =
            bld.vop3(aco_opcode::v_med3_i32, bld.def(v1), Operand::c32(-1), src, Operand::c32(1u));
         bld.vop1(aco_opcode::v_cvt_f32_i32, Definition(dst), src);
      } else if (dst.regClass() == v2) {
         Temp cond = bld.vopc(aco_opcode::v_cmp_nlt_f64, bld.hint_vcc(bld.def(bld.lm)),
                              Operand::zero(), src);
         Temp tmp = bld.copy(bld.def(v1), Operand::c32(0x3FF00000u));
         Temp upper = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), tmp,
                                   emit_extract_vector(ctx, src, 1, v1), cond);

         cond =
            bld.vopc(aco_opcode::v_cmp_le_f64, bld.hint_vcc(bld.def(bld.lm)), Operand::zero(), src);
         tmp = bld.copy(bld.def(v1), Operand::c32(0xBFF00000u));
         upper = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), tmp, upper, cond);

         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), Operand::zero(), upper);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_f2f16:
   case nir_op_f2f16_rtne: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size == 64)
         src = bld.vop1(aco_opcode::v_cvt_f32_f64, bld.def(v1), src);
      if (instr->op == nir_op_f2f16_rtne && ctx->block->fp_mode.round16_64 != fp_round_ne)
         /* We emit s_round_mode/s_setreg_imm32 in lower_to_hw_instr to
          * keep value numbering and the scheduler simpler.
          */
         bld.vop1(aco_opcode::p_cvt_f16_f32_rtne, Definition(dst), src);
      else
         bld.vop1(aco_opcode::v_cvt_f16_f32, Definition(dst), src);
      break;
   }
   case nir_op_f2f16_rtz: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size == 64)
         src = bld.vop1(aco_opcode::v_cvt_f32_f64, bld.def(v1), src);
      if (ctx->block->fp_mode.round16_64 == fp_round_tz)
         bld.vop1(aco_opcode::v_cvt_f16_f32, Definition(dst), src);
      else if (ctx->program->chip_class == GFX8 || ctx->program->chip_class == GFX9)
         bld.vop3(aco_opcode::v_cvt_pkrtz_f16_f32_e64, Definition(dst), src, Operand::zero());
      else
         bld.vop2(aco_opcode::v_cvt_pkrtz_f16_f32, Definition(dst), src, as_vgpr(ctx, src));
      break;
   }
   case nir_op_f2f32: {
      if (instr->src[0].src.ssa->bit_size == 16) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_f32_f16, dst);
      } else if (instr->src[0].src.ssa->bit_size == 64) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_f32_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_f2f64: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size == 16)
         src = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), src);
      bld.vop1(aco_opcode::v_cvt_f64_f32, Definition(dst), src);
      break;
   }
   case nir_op_i2f16: {
      assert(dst.regClass() == v2b);
      Temp src = get_alu_src(ctx, instr->src[0]);
      const unsigned input_size = instr->src[0].src.ssa->bit_size;
      if (input_size <= 16) {
         /* Expand integer to the size expected by the uint→float converter used below */
         unsigned target_size = (ctx->program->chip_class >= GFX8 ? 16 : 32);
         if (input_size != target_size) {
            src = convert_int(ctx, bld, src, input_size, target_size, true);
         }
      } else if (input_size == 64) {
         /* Truncate down to 32 bits; if any of the upper bits are relevant,
          * the value does not fall into the single-precision float range
          * anyway. SPIR-V does not mandate any specific behavior for such
          * large inputs.
          */
         src = convert_int(ctx, bld, src, 64, 32, false);
      }

      if (ctx->program->chip_class >= GFX8 && input_size <= 16) {
         bld.vop1(aco_opcode::v_cvt_f16_i16, Definition(dst), src);
      } else {
         /* Convert to f32 and then down to f16. This is needed to handle
          * inputs slightly outside the range [INT16_MIN, INT16_MAX],
          * which are representable via f16 but wouldn't be converted
          * correctly by v_cvt_f16_i16.
          *
          * This is also the fallback-path taken on GFX7 and earlier, which
          * do not support direct f16⟷i16 conversions.
          */
         src = bld.vop1(aco_opcode::v_cvt_f32_i32, bld.def(v1), src);
         bld.vop1(aco_opcode::v_cvt_f16_f32, Definition(dst), src);
      }
      break;
   }
   case nir_op_i2f32: {
      assert(dst.size() == 1);
      Temp src = get_alu_src(ctx, instr->src[0]);
      const unsigned input_size = instr->src[0].src.ssa->bit_size;
      if (input_size <= 32) {
         if (input_size <= 16) {
            /* Sign-extend to 32-bits */
            src = convert_int(ctx, bld, src, input_size, 32, true);
         }
         bld.vop1(aco_opcode::v_cvt_f32_i32, Definition(dst), src);
      } else {
         assert(input_size == 64);
         RegClass rc = RegClass(src.type(), 1);
         Temp lower = bld.tmp(rc), upper = bld.tmp(rc);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), src);
         lower = bld.vop1(aco_opcode::v_cvt_f64_u32, bld.def(v2), lower);
         upper = bld.vop1(aco_opcode::v_cvt_f64_i32, bld.def(v2), upper);
         upper = bld.vop3(aco_opcode::v_ldexp_f64, bld.def(v2), upper, Operand::c32(32u));
         upper = bld.vop3(aco_opcode::v_add_f64, bld.def(v2), lower, upper);
         bld.vop1(aco_opcode::v_cvt_f32_f64, Definition(dst), upper);
      }

      break;
   }
   case nir_op_i2f64: {
      if (instr->src[0].src.ssa->bit_size <= 32) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         if (instr->src[0].src.ssa->bit_size <= 16)
            src = convert_int(ctx, bld, src, instr->src[0].src.ssa->bit_size, 32, true);
         bld.vop1(aco_opcode::v_cvt_f64_i32, Definition(dst), src);
      } else if (instr->src[0].src.ssa->bit_size == 64) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         RegClass rc = RegClass(src.type(), 1);
         Temp lower = bld.tmp(rc), upper = bld.tmp(rc);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), src);
         lower = bld.vop1(aco_opcode::v_cvt_f64_u32, bld.def(v2), lower);
         upper = bld.vop1(aco_opcode::v_cvt_f64_i32, bld.def(v2), upper);
         upper = bld.vop3(aco_opcode::v_ldexp_f64, bld.def(v2), upper, Operand::c32(32u));
         bld.vop3(aco_opcode::v_add_f64, Definition(dst), lower, upper);

      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_u2f16: {
      assert(dst.regClass() == v2b);
      Temp src = get_alu_src(ctx, instr->src[0]);
      const unsigned input_size = instr->src[0].src.ssa->bit_size;
      if (input_size <= 16) {
         /* Expand integer to the size expected by the uint→float converter used below */
         unsigned target_size = (ctx->program->chip_class >= GFX8 ? 16 : 32);
         if (input_size != target_size) {
            src = convert_int(ctx, bld, src, input_size, target_size, false);
         }
      } else if (input_size == 64) {
         /* Truncate down to 32 bits; if any of the upper bits are non-zero,
          * the value does not fall into the single-precision float range
          * anyway. SPIR-V does not mandate any specific behavior for such
          * large inputs.
          */
         src = convert_int(ctx, bld, src, 64, 32, false);
      }

      if (ctx->program->chip_class >= GFX8) {
         /* float16 has a range of [0, 65519]. Converting from larger
          * inputs is UB, so we just need to consider the lower 16 bits */
         bld.vop1(aco_opcode::v_cvt_f16_u16, Definition(dst), src);
      } else {
         /* GFX7 and earlier do not support direct f16⟷u16 conversions */
         src = bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), src);
         bld.vop1(aco_opcode::v_cvt_f16_f32, Definition(dst), src);
      }
      break;
   }
   case nir_op_u2f32: {
      assert(dst.size() == 1);
      Temp src = get_alu_src(ctx, instr->src[0]);
      const unsigned input_size = instr->src[0].src.ssa->bit_size;
      if (input_size == 8) {
         bld.vop1(aco_opcode::v_cvt_f32_ubyte0, Definition(dst), src);
      } else if (input_size <= 32) {
         if (input_size == 16)
            src = convert_int(ctx, bld, src, instr->src[0].src.ssa->bit_size, 32, false);
         bld.vop1(aco_opcode::v_cvt_f32_u32, Definition(dst), src);
      } else {
         assert(input_size == 64);
         RegClass rc = RegClass(src.type(), 1);
         Temp lower = bld.tmp(rc), upper = bld.tmp(rc);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), src);
         lower = bld.vop1(aco_opcode::v_cvt_f64_u32, bld.def(v2), lower);
         upper = bld.vop1(aco_opcode::v_cvt_f64_u32, bld.def(v2), upper);
         upper = bld.vop3(aco_opcode::v_ldexp_f64, bld.def(v2), upper, Operand::c32(32u));
         upper = bld.vop3(aco_opcode::v_add_f64, bld.def(v2), lower, upper);
         bld.vop1(aco_opcode::v_cvt_f32_f64, Definition(dst), upper);
      }
      break;
   }
   case nir_op_u2f64: {
      if (instr->src[0].src.ssa->bit_size <= 32) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         if (instr->src[0].src.ssa->bit_size <= 16)
            src = convert_int(ctx, bld, src, instr->src[0].src.ssa->bit_size, 32, false);
         bld.vop1(aco_opcode::v_cvt_f64_u32, Definition(dst), src);
      } else if (instr->src[0].src.ssa->bit_size == 64) {
         Temp src = get_alu_src(ctx, instr->src[0]);
         RegClass rc = RegClass(src.type(), 1);
         Temp lower = bld.tmp(rc), upper = bld.tmp(rc);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), src);
         lower = bld.vop1(aco_opcode::v_cvt_f64_u32, bld.def(v2), lower);
         upper = bld.vop1(aco_opcode::v_cvt_f64_u32, bld.def(v2), upper);
         upper = bld.vop3(aco_opcode::v_ldexp_f64, bld.def(v2), upper, Operand::c32(32u));
         bld.vop3(aco_opcode::v_add_f64, Definition(dst), lower, upper);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_f2i8:
   case nir_op_f2i16: {
      if (instr->src[0].src.ssa->bit_size == 16) {
         if (ctx->program->chip_class >= GFX8) {
            emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_i16_f16, dst);
         } else {
            /* GFX7 and earlier do not support direct f16⟷i16 conversions */
            Temp tmp = bld.tmp(v1);
            emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_f32_f16, tmp);
            tmp = bld.vop1(aco_opcode::v_cvt_i32_f32, bld.def(v1), tmp);
            tmp = convert_int(ctx, bld, tmp, 32, instr->dest.dest.ssa.bit_size, false,
                              (dst.type() == RegType::sgpr) ? Temp() : dst);
            if (dst.type() == RegType::sgpr) {
               bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), tmp);
            }
         }
      } else if (instr->src[0].src.ssa->bit_size == 32) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_i32_f32, dst);
      } else {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_i32_f64, dst);
      }
      break;
   }
   case nir_op_f2u8:
   case nir_op_f2u16: {
      if (instr->src[0].src.ssa->bit_size == 16) {
         if (ctx->program->chip_class >= GFX8) {
            emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_u16_f16, dst);
         } else {
            /* GFX7 and earlier do not support direct f16⟷u16 conversions */
            Temp tmp = bld.tmp(v1);
            emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_f32_f16, tmp);
            tmp = bld.vop1(aco_opcode::v_cvt_u32_f32, bld.def(v1), tmp);
            tmp = convert_int(ctx, bld, tmp, 32, instr->dest.dest.ssa.bit_size, false,
                              (dst.type() == RegType::sgpr) ? Temp() : dst);
            if (dst.type() == RegType::sgpr) {
               bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), tmp);
            }
         }
      } else if (instr->src[0].src.ssa->bit_size == 32) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_u32_f32, dst);
      } else {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_u32_f64, dst);
      }
      break;
   }
   case nir_op_f2i32: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size == 16) {
         Temp tmp = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), src);
         if (dst.type() == RegType::vgpr) {
            bld.vop1(aco_opcode::v_cvt_i32_f32, Definition(dst), tmp);
         } else {
            bld.pseudo(aco_opcode::p_as_uniform, Definition(dst),
                       bld.vop1(aco_opcode::v_cvt_i32_f32, bld.def(v1), tmp));
         }
      } else if (instr->src[0].src.ssa->bit_size == 32) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_i32_f32, dst);
      } else if (instr->src[0].src.ssa->bit_size == 64) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_i32_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_f2u32: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size == 16) {
         Temp tmp = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), src);
         if (dst.type() == RegType::vgpr) {
            bld.vop1(aco_opcode::v_cvt_u32_f32, Definition(dst), tmp);
         } else {
            bld.pseudo(aco_opcode::p_as_uniform, Definition(dst),
                       bld.vop1(aco_opcode::v_cvt_u32_f32, bld.def(v1), tmp));
         }
      } else if (instr->src[0].src.ssa->bit_size == 32) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_u32_f32, dst);
      } else if (instr->src[0].src.ssa->bit_size == 64) {
         emit_vop1_instruction(ctx, instr, aco_opcode::v_cvt_u32_f64, dst);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_f2i64: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size == 16)
         src = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), src);

      if (instr->src[0].src.ssa->bit_size <= 32 && dst.type() == RegType::vgpr) {
         Temp exponent = bld.vop1(aco_opcode::v_frexp_exp_i32_f32, bld.def(v1), src);
         exponent = bld.vop3(aco_opcode::v_med3_i32, bld.def(v1), Operand::zero(), exponent,
                             Operand::c32(64u));
         Temp mantissa = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(0x7fffffu), src);
         Temp sign = bld.vop2(aco_opcode::v_ashrrev_i32, bld.def(v1), Operand::c32(31u), src);
         mantissa = bld.vop2(aco_opcode::v_or_b32, bld.def(v1), Operand::c32(0x800000u), mantissa);
         mantissa = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(7u), mantissa);
         mantissa = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), Operand::zero(), mantissa);
         Temp new_exponent = bld.tmp(v1);
         Temp borrow =
            bld.vsub32(Definition(new_exponent), Operand::c32(63u), exponent, true).def(1).getTemp();
         if (ctx->program->chip_class >= GFX8)
            mantissa = bld.vop3(aco_opcode::v_lshrrev_b64, bld.def(v2), new_exponent, mantissa);
         else
            mantissa = bld.vop3(aco_opcode::v_lshr_b64, bld.def(v2), mantissa, new_exponent);
         Temp saturate = bld.vop1(aco_opcode::v_bfrev_b32, bld.def(v1), Operand::c32(0xfffffffeu));
         Temp lower = bld.tmp(v1), upper = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), mantissa);
         lower = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), lower,
                              Operand::c32(0xffffffffu), borrow);
         upper = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), upper, saturate, borrow);
         lower = bld.vop2(aco_opcode::v_xor_b32, bld.def(v1), sign, lower);
         upper = bld.vop2(aco_opcode::v_xor_b32, bld.def(v1), sign, upper);
         Temp new_lower = bld.tmp(v1);
         borrow = bld.vsub32(Definition(new_lower), lower, sign, true).def(1).getTemp();
         Temp new_upper = bld.vsub32(bld.def(v1), upper, sign, false, borrow);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), new_lower, new_upper);

      } else if (instr->src[0].src.ssa->bit_size <= 32 && dst.type() == RegType::sgpr) {
         if (src.type() == RegType::vgpr)
            src = bld.as_uniform(src);
         Temp exponent = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc), src,
                                  Operand::c32(0x80017u));
         exponent = bld.sop2(aco_opcode::s_sub_i32, bld.def(s1), bld.def(s1, scc), exponent,
                             Operand::c32(126u));
         exponent = bld.sop2(aco_opcode::s_max_i32, bld.def(s1), bld.def(s1, scc), Operand::zero(),
                             exponent);
         exponent = bld.sop2(aco_opcode::s_min_i32, bld.def(s1), bld.def(s1, scc),
                             Operand::c32(64u), exponent);
         Temp mantissa = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc),
                                  Operand::c32(0x7fffffu), src);
         Temp sign =
            bld.sop2(aco_opcode::s_ashr_i32, bld.def(s1), bld.def(s1, scc), src, Operand::c32(31u));
         mantissa = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc),
                             Operand::c32(0x800000u), mantissa);
         mantissa = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), mantissa,
                             Operand::c32(7u));
         mantissa = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand::zero(), mantissa);
         exponent = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.def(s1, scc),
                             Operand::c32(63u), exponent);
         mantissa =
            bld.sop2(aco_opcode::s_lshr_b64, bld.def(s2), bld.def(s1, scc), mantissa, exponent);
         Temp cond = bld.sopc(aco_opcode::s_cmp_eq_u32, bld.def(s1, scc), exponent,
                              Operand::c32(0xffffffffu)); // exp >= 64
         Temp saturate = bld.sop1(aco_opcode::s_brev_b64, bld.def(s2), Operand::c32(0xfffffffeu));
         mantissa = bld.sop2(aco_opcode::s_cselect_b64, bld.def(s2), saturate, mantissa, cond);
         Temp lower = bld.tmp(s1), upper = bld.tmp(s1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), mantissa);
         lower = bld.sop2(aco_opcode::s_xor_b32, bld.def(s1), bld.def(s1, scc), sign, lower);
         upper = bld.sop2(aco_opcode::s_xor_b32, bld.def(s1), bld.def(s1, scc), sign, upper);
         Temp borrow = bld.tmp(s1);
         lower =
            bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.scc(Definition(borrow)), lower, sign);
         upper = bld.sop2(aco_opcode::s_subb_u32, bld.def(s1), bld.def(s1, scc), upper, sign,
                          bld.scc(borrow));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);

      } else if (instr->src[0].src.ssa->bit_size == 64) {
         Temp vec = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand::zero(),
                               Operand::c32(0x3df00000u));
         Temp trunc = emit_trunc_f64(ctx, bld, bld.def(v2), src);
         Temp mul = bld.vop3(aco_opcode::v_mul_f64, bld.def(v2), trunc, vec);
         vec = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand::zero(),
                          Operand::c32(0xc1f00000u));
         Temp floor = emit_floor_f64(ctx, bld, bld.def(v2), mul);
         Temp fma = bld.vop3(aco_opcode::v_fma_f64, bld.def(v2), floor, vec, trunc);
         Temp lower = bld.vop1(aco_opcode::v_cvt_u32_f64, bld.def(v1), fma);
         Temp upper = bld.vop1(aco_opcode::v_cvt_i32_f64, bld.def(v1), floor);
         if (dst.type() == RegType::sgpr) {
            lower = bld.as_uniform(lower);
            upper = bld.as_uniform(upper);
         }
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);

      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_f2u64: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (instr->src[0].src.ssa->bit_size == 16)
         src = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), src);

      if (instr->src[0].src.ssa->bit_size <= 32 && dst.type() == RegType::vgpr) {
         Temp exponent = bld.vop1(aco_opcode::v_frexp_exp_i32_f32, bld.def(v1), src);
         Temp exponent_in_range = bld.vopc(aco_opcode::v_cmp_ge_i32, bld.hint_vcc(bld.def(bld.lm)),
                                           Operand::c32(64u), exponent);
         exponent = bld.vop2(aco_opcode::v_max_i32, bld.def(v1), Operand::zero(), exponent);
         Temp mantissa = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(0x7fffffu), src);
         mantissa = bld.vop2(aco_opcode::v_or_b32, bld.def(v1), Operand::c32(0x800000u), mantissa);
         Temp exponent_small = bld.vsub32(bld.def(v1), Operand::c32(24u), exponent);
         Temp small = bld.vop2(aco_opcode::v_lshrrev_b32, bld.def(v1), exponent_small, mantissa);
         mantissa = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), Operand::zero(), mantissa);
         Temp new_exponent = bld.tmp(v1);
         Temp cond_small =
            bld.vsub32(Definition(new_exponent), exponent, Operand::c32(24u), true).def(1).getTemp();
         if (ctx->program->chip_class >= GFX8)
            mantissa = bld.vop3(aco_opcode::v_lshlrev_b64, bld.def(v2), new_exponent, mantissa);
         else
            mantissa = bld.vop3(aco_opcode::v_lshl_b64, bld.def(v2), mantissa, new_exponent);
         Temp lower = bld.tmp(v1), upper = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), mantissa);
         lower = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), lower, small, cond_small);
         upper = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), upper, Operand::zero(),
                              cond_small);
         lower = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::c32(0xffffffffu), lower,
                          exponent_in_range);
         upper = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::c32(0xffffffffu), upper,
                          exponent_in_range);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);

      } else if (instr->src[0].src.ssa->bit_size <= 32 && dst.type() == RegType::sgpr) {
         if (src.type() == RegType::vgpr)
            src = bld.as_uniform(src);
         Temp exponent = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc), src,
                                  Operand::c32(0x80017u));
         exponent = bld.sop2(aco_opcode::s_sub_i32, bld.def(s1), bld.def(s1, scc), exponent,
                             Operand::c32(126u));
         exponent = bld.sop2(aco_opcode::s_max_i32, bld.def(s1), bld.def(s1, scc), Operand::zero(),
                             exponent);
         Temp mantissa = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc),
                                  Operand::c32(0x7fffffu), src);
         mantissa = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc),
                             Operand::c32(0x800000u), mantissa);
         Temp exponent_small = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.def(s1, scc),
                                        Operand::c32(24u), exponent);
         Temp small = bld.sop2(aco_opcode::s_lshr_b32, bld.def(s1), bld.def(s1, scc), mantissa,
                               exponent_small);
         mantissa = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand::zero(), mantissa);
         Temp exponent_large = bld.sop2(aco_opcode::s_sub_u32, bld.def(s1), bld.def(s1, scc),
                                        exponent, Operand::c32(24u));
         mantissa = bld.sop2(aco_opcode::s_lshl_b64, bld.def(s2), bld.def(s1, scc), mantissa,
                             exponent_large);
         Temp cond =
            bld.sopc(aco_opcode::s_cmp_ge_i32, bld.def(s1, scc), Operand::c32(64u), exponent);
         mantissa = bld.sop2(aco_opcode::s_cselect_b64, bld.def(s2), mantissa,
                             Operand::c32(0xffffffffu), cond);
         Temp lower = bld.tmp(s1), upper = bld.tmp(s1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lower), Definition(upper), mantissa);
         Temp cond_small =
            bld.sopc(aco_opcode::s_cmp_le_i32, bld.def(s1, scc), exponent, Operand::c32(24u));
         lower = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1), small, lower, cond_small);
         upper =
            bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1), Operand::zero(), upper, cond_small);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);

      } else if (instr->src[0].src.ssa->bit_size == 64) {
         Temp vec = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand::zero(),
                               Operand::c32(0x3df00000u));
         Temp trunc = emit_trunc_f64(ctx, bld, bld.def(v2), src);
         Temp mul = bld.vop3(aco_opcode::v_mul_f64, bld.def(v2), trunc, vec);
         vec = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), Operand::zero(),
                          Operand::c32(0xc1f00000u));
         Temp floor = emit_floor_f64(ctx, bld, bld.def(v2), mul);
         Temp fma = bld.vop3(aco_opcode::v_fma_f64, bld.def(v2), floor, vec, trunc);
         Temp lower = bld.vop1(aco_opcode::v_cvt_u32_f64, bld.def(v1), fma);
         Temp upper = bld.vop1(aco_opcode::v_cvt_u32_f64, bld.def(v1), floor);
         if (dst.type() == RegType::sgpr) {
            lower = bld.as_uniform(lower);
            upper = bld.as_uniform(upper);
         }
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lower, upper);

      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_b2f16: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      assert(src.regClass() == bld.lm);

      if (dst.regClass() == s1) {
         src = bool_to_scalar_condition(ctx, src);
         bld.sop2(aco_opcode::s_mul_i32, Definition(dst), Operand::c32(0x3c00u), src);
      } else if (dst.regClass() == v2b) {
         Temp one = bld.copy(bld.def(v1), Operand::c32(0x3c00u));
         bld.vop2(aco_opcode::v_cndmask_b32, Definition(dst), Operand::zero(), one, src);
      } else {
         unreachable("Wrong destination register class for nir_op_b2f16.");
      }
      break;
   }
   case nir_op_b2f32: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      assert(src.regClass() == bld.lm);

      if (dst.regClass() == s1) {
         src = bool_to_scalar_condition(ctx, src);
         bld.sop2(aco_opcode::s_mul_i32, Definition(dst), Operand::c32(0x3f800000u), src);
      } else if (dst.regClass() == v1) {
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(dst), Operand::zero(),
                      Operand::c32(0x3f800000u), src);
      } else {
         unreachable("Wrong destination register class for nir_op_b2f32.");
      }
      break;
   }
   case nir_op_b2f64: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      assert(src.regClass() == bld.lm);

      if (dst.regClass() == s2) {
         src = bool_to_scalar_condition(ctx, src);
         bld.sop2(aco_opcode::s_cselect_b64, Definition(dst), Operand::c32(0x3f800000u),
                  Operand::zero(), bld.scc(src));
      } else if (dst.regClass() == v2) {
         Temp one = bld.copy(bld.def(v2), Operand::c32(0x3FF00000u));
         Temp upper =
            bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::zero(), one, src);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), Operand::zero(), upper);
      } else {
         unreachable("Wrong destination register class for nir_op_b2f64.");
      }
      break;
   }
   case nir_op_i2i8:
   case nir_op_i2i16:
   case nir_op_i2i32:
   case nir_op_i2i64: {
      if (dst.type() == RegType::sgpr && instr->src[0].src.ssa->bit_size < 32) {
         /* no need to do the extract in get_alu_src() */
         sgpr_extract_mode mode = instr->dest.dest.ssa.bit_size > instr->src[0].src.ssa->bit_size
                                     ? sgpr_extract_sext
                                     : sgpr_extract_undef;
         extract_8_16_bit_sgpr_element(ctx, dst, &instr->src[0], mode);
      } else {
         const unsigned input_bitsize = instr->src[0].src.ssa->bit_size;
         const unsigned output_bitsize = instr->dest.dest.ssa.bit_size;
         convert_int(ctx, bld, get_alu_src(ctx, instr->src[0]), input_bitsize, output_bitsize,
                     output_bitsize > input_bitsize, dst);
      }
      break;
   }
   case nir_op_u2u8:
   case nir_op_u2u16:
   case nir_op_u2u32:
   case nir_op_u2u64: {
      if (dst.type() == RegType::sgpr && instr->src[0].src.ssa->bit_size < 32) {
         /* no need to do the extract in get_alu_src() */
         sgpr_extract_mode mode = instr->dest.dest.ssa.bit_size > instr->src[0].src.ssa->bit_size
                                     ? sgpr_extract_zext
                                     : sgpr_extract_undef;
         extract_8_16_bit_sgpr_element(ctx, dst, &instr->src[0], mode);
      } else {
         convert_int(ctx, bld, get_alu_src(ctx, instr->src[0]), instr->src[0].src.ssa->bit_size,
                     instr->dest.dest.ssa.bit_size, false, dst);
      }
      break;
   }
   case nir_op_b2b32:
   case nir_op_b2i8:
   case nir_op_b2i16:
   case nir_op_b2i32:
   case nir_op_b2i64: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      assert(src.regClass() == bld.lm);

      Temp tmp = dst.bytes() == 8 ? bld.tmp(RegClass::get(dst.type(), 4)) : dst;
      if (tmp.regClass() == s1) {
         bool_to_scalar_condition(ctx, src, tmp);
      } else if (tmp.type() == RegType::vgpr) {
         bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(tmp), Operand::zero(), Operand::c32(1u),
                      src);
      } else {
         unreachable("Invalid register class for b2i32");
      }

      if (tmp != dst)
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), tmp, Operand::zero());
      break;
   }
   case nir_op_b2b1:
   case nir_op_i2b1: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      assert(dst.regClass() == bld.lm);

      if (src.type() == RegType::vgpr) {
         assert(src.regClass() == v1 || src.regClass() == v2);
         assert(dst.regClass() == bld.lm);
         bld.vopc(src.size() == 2 ? aco_opcode::v_cmp_lg_u64 : aco_opcode::v_cmp_lg_u32,
                  Definition(dst), Operand::zero(), src)
            .def(0)
            .setHint(vcc);
      } else {
         assert(src.regClass() == s1 || src.regClass() == s2);
         Temp tmp;
         if (src.regClass() == s2 && ctx->program->chip_class <= GFX7) {
            tmp =
               bld.sop2(aco_opcode::s_or_b64, bld.def(s2), bld.def(s1, scc), Operand::zero(), src)
                  .def(1)
                  .getTemp();
         } else {
            tmp = bld.sopc(src.size() == 2 ? aco_opcode::s_cmp_lg_u64 : aco_opcode::s_cmp_lg_u32,
                           bld.scc(bld.def(s1)), Operand::zero(), src);
         }
         bool_to_vector_condition(ctx, tmp, dst);
      }
      break;
   }
   case nir_op_unpack_64_2x32:
   case nir_op_unpack_32_2x16:
   case nir_op_unpack_64_4x16:
      bld.copy(Definition(dst), get_alu_src(ctx, instr->src[0]));
      emit_split_vector(ctx, dst, instr->op == nir_op_unpack_64_4x16 ? 4 : 2);
      break;
   case nir_op_pack_64_2x32_split: {
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);

      bld.pseudo(aco_opcode::p_create_vector, Definition(dst), src0, src1);
      break;
   }
   case nir_op_unpack_64_2x32_split_x:
      bld.pseudo(aco_opcode::p_split_vector, Definition(dst), bld.def(dst.regClass()),
                 get_alu_src(ctx, instr->src[0]));
      break;
   case nir_op_unpack_64_2x32_split_y:
      bld.pseudo(aco_opcode::p_split_vector, bld.def(dst.regClass()), Definition(dst),
                 get_alu_src(ctx, instr->src[0]));
      break;
   case nir_op_unpack_32_2x16_split_x:
      if (dst.type() == RegType::vgpr) {
         bld.pseudo(aco_opcode::p_split_vector, Definition(dst), bld.def(dst.regClass()),
                    get_alu_src(ctx, instr->src[0]));
      } else {
         bld.copy(Definition(dst), get_alu_src(ctx, instr->src[0]));
      }
      break;
   case nir_op_unpack_32_2x16_split_y:
      if (dst.type() == RegType::vgpr) {
         bld.pseudo(aco_opcode::p_split_vector, bld.def(dst.regClass()), Definition(dst),
                    get_alu_src(ctx, instr->src[0]));
      } else {
         bld.pseudo(aco_opcode::p_extract, Definition(dst), bld.def(s1, scc),
                    get_alu_src(ctx, instr->src[0]), Operand::c32(1u), Operand::c32(16u),
                    Operand::zero());
      }
      break;
   case nir_op_pack_32_2x16_split: {
      Temp src0 = get_alu_src(ctx, instr->src[0]);
      Temp src1 = get_alu_src(ctx, instr->src[1]);
      if (dst.regClass() == v1) {
         src0 = emit_extract_vector(ctx, src0, 0, v2b);
         src1 = emit_extract_vector(ctx, src1, 0, v2b);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), src0, src1);
      } else {
         src0 = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), src0,
                         Operand::c32(0xFFFFu));
         src1 = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), src1,
                         Operand::c32(16u));
         bld.sop2(aco_opcode::s_or_b32, Definition(dst), bld.def(s1, scc), src0, src1);
      }
      break;
   }
   case nir_op_pack_32_4x8: bld.copy(Definition(dst), get_alu_src(ctx, instr->src[0], 4)); break;
   case nir_op_pack_half_2x16_split: {
      if (dst.regClass() == v1) {
         nir_const_value* val = nir_src_as_const_value(instr->src[1].src);
         if (val && val->u32 == 0 && ctx->program->chip_class <= GFX9) {
            /* upper bits zero on GFX6-GFX9 */
            bld.vop1(aco_opcode::v_cvt_f16_f32, Definition(dst), get_alu_src(ctx, instr->src[0]));
         } else if (!ctx->block->fp_mode.care_about_round16_64 ||
                    ctx->block->fp_mode.round16_64 == fp_round_tz) {
            if (ctx->program->chip_class == GFX8 || ctx->program->chip_class == GFX9)
               emit_vop3a_instruction(ctx, instr, aco_opcode::v_cvt_pkrtz_f16_f32_e64, dst);
            else
               emit_vop2_instruction(ctx, instr, aco_opcode::v_cvt_pkrtz_f16_f32, dst, false);
         } else {
            Temp src0 =
               bld.vop1(aco_opcode::v_cvt_f16_f32, bld.def(v2b), get_alu_src(ctx, instr->src[0]));
            Temp src1 =
               bld.vop1(aco_opcode::v_cvt_f16_f32, bld.def(v2b), get_alu_src(ctx, instr->src[1]));
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), src0, src1);
         }
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_unpack_half_2x16_split_x_flush_to_zero:
   case nir_op_unpack_half_2x16_split_x: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (src.regClass() == v1)
         src = bld.pseudo(aco_opcode::p_split_vector, bld.def(v2b), bld.def(v2b), src);
      if (dst.regClass() == v1) {
         assert(ctx->block->fp_mode.must_flush_denorms16_64 ==
                (instr->op == nir_op_unpack_half_2x16_split_x_flush_to_zero));
         bld.vop1(aco_opcode::v_cvt_f32_f16, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_unpack_half_2x16_split_y_flush_to_zero:
   case nir_op_unpack_half_2x16_split_y: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (src.regClass() == s1)
         src =
            bld.sop2(aco_opcode::s_lshr_b32, bld.def(s1), bld.def(s1, scc), src, Operand::c32(16u));
      else
         src =
            bld.pseudo(aco_opcode::p_split_vector, bld.def(v2b), bld.def(v2b), src).def(1).getTemp();
      if (dst.regClass() == v1) {
         assert(ctx->block->fp_mode.must_flush_denorms16_64 ==
                (instr->op == nir_op_unpack_half_2x16_split_y_flush_to_zero));
         bld.vop1(aco_opcode::v_cvt_f32_f16, Definition(dst), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_sad_u8x4: {
      assert(dst.regClass() == v1);
      emit_vop3a_instruction(ctx, instr, aco_opcode::v_sad_u8, dst, false, 3u, false);
      break;
   }
   case nir_op_fquantize2f16: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      Temp f16 = bld.vop1(aco_opcode::v_cvt_f16_f32, bld.def(v1), src);
      Temp f32, cmp_res;

      if (ctx->program->chip_class >= GFX8) {
         Temp mask = bld.copy(
            bld.def(s1), Operand::c32(0x36Fu)); /* value is NOT negative/positive denormal value */
         cmp_res =
            bld.vopc_e64(aco_opcode::v_cmp_class_f16, bld.hint_vcc(bld.def(bld.lm)), f16, mask);
         f32 = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), f16);
      } else {
         /* 0x38800000 is smallest half float value (2^-14) in 32-bit float,
          * so compare the result and flush to 0 if it's smaller.
          */
         f32 = bld.vop1(aco_opcode::v_cvt_f32_f16, bld.def(v1), f16);
         Temp smallest = bld.copy(bld.def(s1), Operand::c32(0x38800000u));
         Instruction* tmp0 = bld.vopc_e64(aco_opcode::v_cmp_lt_f32, bld.def(bld.lm), f32, smallest);
         tmp0->vop3().abs[0] = true;
         Temp tmp1 =
            bld.vopc(aco_opcode::v_cmp_lg_f32, bld.hint_vcc(bld.def(bld.lm)), Operand::zero(), f32);
         cmp_res = bld.sop2(aco_opcode::s_nand_b64, bld.def(s2), bld.def(s1, scc),
                            tmp0->definitions[0].getTemp(), tmp1);
      }

      if (ctx->block->fp_mode.preserve_signed_zero_inf_nan32) {
         Temp copysign_0 =
            bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), Operand::zero(), as_vgpr(ctx, src));
         bld.vop2(aco_opcode::v_cndmask_b32, Definition(dst), copysign_0, f32, cmp_res);
      } else {
         bld.vop2(aco_opcode::v_cndmask_b32, Definition(dst), Operand::zero(), f32, cmp_res);
      }
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
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_bitfield_select: {

      /* dst = (insert & bitmask) | (base & ~bitmask) */
      if (dst.regClass() == s1) {
         Temp bitmask = get_alu_src(ctx, instr->src[0]);
         Temp insert = get_alu_src(ctx, instr->src[1]);
         Temp base = get_alu_src(ctx, instr->src[2]);
         aco_ptr<Instruction> sop2;
         nir_const_value* const_bitmask = nir_src_as_const_value(instr->src[0].src);
         nir_const_value* const_insert = nir_src_as_const_value(instr->src[1].src);
         Operand lhs;
         if (const_insert && const_bitmask) {
            lhs = Operand::c32(const_insert->u32 & const_bitmask->u32);
         } else {
            insert =
               bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), insert, bitmask);
            lhs = Operand(insert);
         }

         Operand rhs;
         nir_const_value* const_base = nir_src_as_const_value(instr->src[2].src);
         if (const_base && const_bitmask) {
            rhs = Operand::c32(const_base->u32 & ~const_bitmask->u32);
         } else {
            base = bld.sop2(aco_opcode::s_andn2_b32, bld.def(s1), bld.def(s1, scc), base, bitmask);
            rhs = Operand(base);
         }

         bld.sop2(aco_opcode::s_or_b32, Definition(dst), bld.def(s1, scc), rhs, lhs);

      } else if (dst.regClass() == v1) {
         emit_vop3a_instruction(ctx, instr, aco_opcode::v_bfi_b32, dst, false, 3);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_ubfe:
   case nir_op_ibfe: {
      if (dst.bytes() != 4)
         unreachable("Unsupported BFE bit size");

      if (dst.type() == RegType::sgpr) {
         Temp base = get_alu_src(ctx, instr->src[0]);

         nir_const_value* const_offset = nir_src_as_const_value(instr->src[1].src);
         nir_const_value* const_bits = nir_src_as_const_value(instr->src[2].src);
         if (const_offset && const_bits) {
            uint32_t extract = (const_bits->u32 << 16) | (const_offset->u32 & 0x1f);
            aco_opcode opcode =
               instr->op == nir_op_ubfe ? aco_opcode::s_bfe_u32 : aco_opcode::s_bfe_i32;
            bld.sop2(opcode, Definition(dst), bld.def(s1, scc), base, Operand::c32(extract));
            break;
         }

         Temp offset = get_alu_src(ctx, instr->src[1]);
         Temp bits = get_alu_src(ctx, instr->src[2]);
         if (instr->op == nir_op_ubfe) {
            Temp mask = bld.sop2(aco_opcode::s_bfm_b32, bld.def(s1), bits, offset);
            Temp masked =
               bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), base, mask);
            bld.sop2(aco_opcode::s_lshr_b32, Definition(dst), bld.def(s1, scc), masked, offset);
         } else {
            Operand bits_op = const_bits ? Operand::c32(const_bits->u32 << 16)
                                         : bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1),
                                                    bld.def(s1, scc), bits, Operand::c32(16u));
            Operand offset_op = const_offset
                                   ? Operand::c32(const_offset->u32 & 0x1fu)
                                   : bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc),
                                              offset, Operand::c32(0x1fu));

            Temp extract =
               bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), bits_op, offset_op);
            bld.sop2(aco_opcode::s_bfe_i32, Definition(dst), bld.def(s1, scc), base, extract);
         }

      } else {
         aco_opcode opcode =
            instr->op == nir_op_ubfe ? aco_opcode::v_bfe_u32 : aco_opcode::v_bfe_i32;
         emit_vop3a_instruction(ctx, instr, opcode, dst, false, 3);
      }
      break;
   }
   case nir_op_extract_u8:
   case nir_op_extract_i8:
   case nir_op_extract_u16:
   case nir_op_extract_i16: {
      bool is_signed = instr->op == nir_op_extract_i16 || instr->op == nir_op_extract_i8;
      unsigned comp = instr->op == nir_op_extract_u8 || instr->op == nir_op_extract_i8 ? 4 : 2;
      uint32_t bits = comp == 4 ? 8 : 16;
      unsigned index = nir_src_as_uint(instr->src[1].src);
      if (bits >= instr->dest.dest.ssa.bit_size || index * bits >= instr->dest.dest.ssa.bit_size) {
         assert(index == 0);
         bld.copy(Definition(dst), get_alu_src(ctx, instr->src[0]));
      } else if (dst.regClass() == s1 && instr->dest.dest.ssa.bit_size == 16) {
         Temp vec = get_ssa_temp(ctx, instr->src[0].src.ssa);
         unsigned swizzle = instr->src[0].swizzle[0];
         if (vec.size() > 1) {
            vec = emit_extract_vector(ctx, vec, swizzle / 2, s1);
            swizzle = swizzle & 1;
         }
         index += swizzle * instr->dest.dest.ssa.bit_size / bits;
         bld.pseudo(aco_opcode::p_extract, Definition(dst), bld.def(s1, scc), Operand(vec),
                    Operand::c32(index), Operand::c32(bits), Operand::c32(is_signed));
      } else {
         Temp src = get_alu_src(ctx, instr->src[0]);
         Definition def(dst);
         if (dst.bytes() == 8) {
            src = emit_extract_vector(ctx, src, index / comp, RegClass(src.type(), 1));
            index %= comp;
            def = bld.def(src.type(), 1);
         }
         assert(def.bytes() <= 4);
         if (def.regClass() == s1) {
            bld.pseudo(aco_opcode::p_extract, def, bld.def(s1, scc), Operand(src),
                       Operand::c32(index), Operand::c32(bits), Operand::c32(is_signed));
         } else {
            src = emit_extract_vector(ctx, src, 0, def.regClass());
            bld.pseudo(aco_opcode::p_extract, def, Operand(src), Operand::c32(index),
                       Operand::c32(bits), Operand::c32(is_signed));
         }
         if (dst.size() == 2)
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), def.getTemp(),
                       Operand::zero());
      }
      break;
   }
   case nir_op_insert_u8:
   case nir_op_insert_u16: {
      unsigned comp = instr->op == nir_op_insert_u8 ? 4 : 2;
      uint32_t bits = comp == 4 ? 8 : 16;
      unsigned index = nir_src_as_uint(instr->src[1].src);
      if (bits >= instr->dest.dest.ssa.bit_size || index * bits >= instr->dest.dest.ssa.bit_size) {
         assert(index == 0);
         bld.copy(Definition(dst), get_alu_src(ctx, instr->src[0]));
      } else {
         Temp src = get_alu_src(ctx, instr->src[0]);
         Definition def(dst);
         bool swap = false;
         if (dst.bytes() == 8) {
            src = emit_extract_vector(ctx, src, 0u, RegClass(src.type(), 1));
            swap = index >= comp;
            index %= comp;
            def = bld.def(src.type(), 1);
         }
         if (def.regClass() == s1) {
            bld.pseudo(aco_opcode::p_insert, def, bld.def(s1, scc), Operand(src),
                       Operand::c32(index), Operand::c32(bits));
         } else {
            src = emit_extract_vector(ctx, src, 0, def.regClass());
            bld.pseudo(aco_opcode::p_insert, def, Operand(src), Operand::c32(index),
                       Operand::c32(bits));
         }
         if (dst.size() == 2 && swap)
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), Operand::zero(),
                       def.getTemp());
         else if (dst.size() == 2)
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), def.getTemp(),
                       Operand::zero());
      }
      break;
   }
   case nir_op_bit_count: {
      Temp src = get_alu_src(ctx, instr->src[0]);
      if (src.regClass() == s1) {
         bld.sop1(aco_opcode::s_bcnt1_i32_b32, Definition(dst), bld.def(s1, scc), src);
      } else if (src.regClass() == v1) {
         bld.vop3(aco_opcode::v_bcnt_u32_b32, Definition(dst), src, Operand::zero());
      } else if (src.regClass() == v2) {
         bld.vop3(aco_opcode::v_bcnt_u32_b32, Definition(dst), emit_extract_vector(ctx, src, 1, v1),
                  bld.vop3(aco_opcode::v_bcnt_u32_b32, bld.def(v1),
                           emit_extract_vector(ctx, src, 0, v1), Operand::zero()));
      } else if (src.regClass() == s2) {
         bld.sop1(aco_opcode::s_bcnt1_i32_b64, Definition(dst), bld.def(s1, scc), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_op_flt: {
      emit_comparison(ctx, instr, dst, aco_opcode::v_cmp_lt_f16, aco_opcode::v_cmp_lt_f32,
                      aco_opcode::v_cmp_lt_f64);
      break;
   }
   case nir_op_fge: {
      emit_comparison(ctx, instr, dst, aco_opcode::v_cmp_ge_f16, aco_opcode::v_cmp_ge_f32,
                      aco_opcode::v_cmp_ge_f64);
      break;
   }
   case nir_op_feq: {
      emit_comparison(ctx, instr, dst, aco_opcode::v_cmp_eq_f16, aco_opcode::v_cmp_eq_f32,
                      aco_opcode::v_cmp_eq_f64);
      break;
   }
   case nir_op_fneu: {
      emit_comparison(ctx, instr, dst, aco_opcode::v_cmp_neq_f16, aco_opcode::v_cmp_neq_f32,
                      aco_opcode::v_cmp_neq_f64);
      break;
   }
   case nir_op_ilt: {
      emit_comparison(ctx, instr, dst, aco_opcode::v_cmp_lt_i16, aco_opcode::v_cmp_lt_i32,
                      aco_opcode::v_cmp_lt_i64, aco_opcode::s_cmp_lt_i32);
      break;
   }
   case nir_op_ige: {
      emit_comparison(ctx, instr, dst, aco_opcode::v_cmp_ge_i16, aco_opcode::v_cmp_ge_i32,
                      aco_opcode::v_cmp_ge_i64, aco_opcode::s_cmp_ge_i32);
      break;
   }
   case nir_op_ieq: {
      if (instr->src[0].src.ssa->bit_size == 1)
         emit_boolean_logic(ctx, instr, Builder::s_xnor, dst);
      else
         emit_comparison(
            ctx, instr, dst, aco_opcode::v_cmp_eq_i16, aco_opcode::v_cmp_eq_i32,
            aco_opcode::v_cmp_eq_i64, aco_opcode::s_cmp_eq_i32,
            ctx->program->chip_class >= GFX8 ? aco_opcode::s_cmp_eq_u64 : aco_opcode::num_opcodes);
      break;
   }
   case nir_op_ine: {
      if (instr->src[0].src.ssa->bit_size == 1)
         emit_boolean_logic(ctx, instr, Builder::s_xor, dst);
      else
         emit_comparison(
            ctx, instr, dst, aco_opcode::v_cmp_lg_i16, aco_opcode::v_cmp_lg_i32,
            aco_opcode::v_cmp_lg_i64, aco_opcode::s_cmp_lg_i32,
            ctx->program->chip_class >= GFX8 ? aco_opcode::s_cmp_lg_u64 : aco_opcode::num_opcodes);
      break;
   }
   case nir_op_ult: {
      emit_comparison(ctx, instr, dst, aco_opcode::v_cmp_lt_u16, aco_opcode::v_cmp_lt_u32,
                      aco_opcode::v_cmp_lt_u64, aco_opcode::s_cmp_lt_u32);
      break;
   }
   case nir_op_uge: {
      emit_comparison(ctx, instr, dst, aco_opcode::v_cmp_ge_u16, aco_opcode::v_cmp_ge_u32,
                      aco_opcode::v_cmp_ge_u64, aco_opcode::s_cmp_ge_u32);
      break;
   }
   case nir_op_fddx:
   case nir_op_fddy:
   case nir_op_fddx_fine:
   case nir_op_fddy_fine:
   case nir_op_fddx_coarse:
   case nir_op_fddy_coarse: {
      if (!nir_src_is_divergent(instr->src[0].src)) {
         /* Source is the same in all lanes, so the derivative is zero.
          * This also avoids emitting invalid IR.
          */
         bld.copy(Definition(dst), Operand::zero());
         break;
      }

      Temp src = as_vgpr(ctx, get_alu_src(ctx, instr->src[0]));
      uint16_t dpp_ctrl1, dpp_ctrl2;
      if (instr->op == nir_op_fddx_fine) {
         dpp_ctrl1 = dpp_quad_perm(0, 0, 2, 2);
         dpp_ctrl2 = dpp_quad_perm(1, 1, 3, 3);
      } else if (instr->op == nir_op_fddy_fine) {
         dpp_ctrl1 = dpp_quad_perm(0, 1, 0, 1);
         dpp_ctrl2 = dpp_quad_perm(2, 3, 2, 3);
      } else {
         dpp_ctrl1 = dpp_quad_perm(0, 0, 0, 0);
         if (instr->op == nir_op_fddx || instr->op == nir_op_fddx_coarse)
            dpp_ctrl2 = dpp_quad_perm(1, 1, 1, 1);
         else
            dpp_ctrl2 = dpp_quad_perm(2, 2, 2, 2);
      }

      Temp tmp;
      if (ctx->program->chip_class >= GFX8) {
         Temp tl = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), src, dpp_ctrl1);
         tmp = bld.vop2_dpp(aco_opcode::v_sub_f32, bld.def(v1), src, tl, dpp_ctrl2);
      } else {
         Temp tl = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, (1 << 15) | dpp_ctrl1);
         Temp tr = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, (1 << 15) | dpp_ctrl2);
         tmp = bld.vop2(aco_opcode::v_sub_f32, bld.def(v1), tr, tl);
      }
      emit_wqm(bld, tmp, dst, true);
      break;
   }
   default: isel_err(&instr->instr, "Unknown NIR ALU instr");
   }
}

void
visit_load_const(isel_context* ctx, nir_load_const_instr* instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->def);

   // TODO: we really want to have the resulting type as this would allow for 64bit literals
   // which get truncated the lsb if double and msb if int
   // for now, we only use s_mov_b64 with 64bit inline constants
   assert(instr->def.num_components == 1 && "Vector load_const should be lowered to scalar.");
   assert(dst.type() == RegType::sgpr);

   Builder bld(ctx->program, ctx->block);

   if (instr->def.bit_size == 1) {
      assert(dst.regClass() == bld.lm);
      int val = instr->value[0].b ? -1 : 0;
      Operand op = bld.lm.size() == 1 ? Operand::c32(val) : Operand::c64(val);
      bld.copy(Definition(dst), op);
   } else if (instr->def.bit_size == 8) {
      bld.copy(Definition(dst), Operand::c32(instr->value[0].u8));
   } else if (instr->def.bit_size == 16) {
      /* sign-extend to use s_movk_i32 instead of a literal */
      bld.copy(Definition(dst), Operand::c32(instr->value[0].i16));
   } else if (dst.size() == 1) {
      bld.copy(Definition(dst), Operand::c32(instr->value[0].u32));
   } else {
      assert(dst.size() != 1);
      aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(
         aco_opcode::p_create_vector, Format::PSEUDO, dst.size(), 1)};
      if (instr->def.bit_size == 64)
         for (unsigned i = 0; i < dst.size(); i++)
            vec->operands[i] = Operand::c32(instr->value[0].u64 >> i * 32);
      else {
         for (unsigned i = 0; i < dst.size(); i++)
            vec->operands[i] = Operand::c32(instr->value[i].u32);
      }
      vec->definitions[0] = Definition(dst);
      ctx->block->instructions.emplace_back(std::move(vec));
   }
}

uint32_t
widen_mask(uint32_t mask, unsigned multiplier)
{
   uint32_t new_mask = 0;
   for (unsigned i = 0; i < 32 && (1u << i) <= mask; ++i)
      if (mask & (1u << i))
         new_mask |= ((1u << multiplier) - 1u) << (i * multiplier);
   return new_mask;
}

struct LoadEmitInfo {
   Operand offset;
   Temp dst;
   unsigned num_components;
   unsigned component_size;
   Temp resource = Temp(0, s1);
   unsigned component_stride = 0;
   unsigned const_offset = 0;
   unsigned align_mul = 0;
   unsigned align_offset = 0;

   bool glc = false;
   bool slc = false;
   unsigned swizzle_component_size = 0;
   memory_sync_info sync;
   Temp soffset = Temp(0, s1);
};

struct EmitLoadParameters {
   using Callback = Temp (*)(Builder& bld, const LoadEmitInfo& info, Temp offset,
                             unsigned bytes_needed, unsigned align, unsigned const_offset,
                             Temp dst_hint);

   Callback callback;
   bool byte_align_loads;
   bool supports_8bit_16bit_loads;
   unsigned max_const_offset_plus_one;
};

void
emit_load(isel_context* ctx, Builder& bld, const LoadEmitInfo& info,
          const EmitLoadParameters& params)
{
   unsigned load_size = info.num_components * info.component_size;
   unsigned component_size = info.component_size;

   unsigned num_vals = 0;
   Temp* const vals = (Temp*)alloca(info.dst.bytes() * sizeof(Temp));

   unsigned const_offset = info.const_offset;

   const unsigned align_mul = info.align_mul ? info.align_mul : component_size;
   unsigned align_offset = (info.align_offset + const_offset) % align_mul;

   unsigned bytes_read = 0;
   while (bytes_read < load_size) {
      unsigned bytes_needed = load_size - bytes_read;

      /* add buffer for unaligned loads */
      int byte_align = 0;
      if (params.byte_align_loads) {
         byte_align = align_mul % 4 == 0 ? align_offset % 4 : -1;
      }

      if (byte_align) {
         if (bytes_needed > 2 || (bytes_needed == 2 && (align_mul % 2 || align_offset % 2)) ||
             !params.supports_8bit_16bit_loads) {
            if (info.component_stride) {
               assert(params.supports_8bit_16bit_loads && "unimplemented");
               bytes_needed = 2;
               byte_align = 0;
            } else {
               bytes_needed += byte_align == -1 ? 4 - info.align_mul : byte_align;
               bytes_needed = align(bytes_needed, 4);
            }
         } else {
            byte_align = 0;
         }
      }

      if (info.swizzle_component_size)
         bytes_needed = MIN2(bytes_needed, info.swizzle_component_size);
      if (info.component_stride)
         bytes_needed = MIN2(bytes_needed, info.component_size);

      bool need_to_align_offset = byte_align && (align_mul % 4 || align_offset % 4);

      /* reduce constant offset */
      Operand offset = info.offset;
      unsigned reduced_const_offset = const_offset;
      bool remove_const_offset_completely = need_to_align_offset;
      if (const_offset &&
          (remove_const_offset_completely || const_offset >= params.max_const_offset_plus_one)) {
         unsigned to_add = const_offset;
         if (remove_const_offset_completely) {
            reduced_const_offset = 0;
         } else {
            to_add =
               const_offset / params.max_const_offset_plus_one * params.max_const_offset_plus_one;
            reduced_const_offset %= params.max_const_offset_plus_one;
         }
         Temp offset_tmp = offset.isTemp() ? offset.getTemp() : Temp();
         if (offset.isConstant()) {
            offset = Operand::c32(offset.constantValue() + to_add);
         } else if (offset_tmp.regClass() == s1) {
            offset = bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc), offset_tmp,
                              Operand::c32(to_add));
         } else if (offset_tmp.regClass() == v1) {
            offset = bld.vadd32(bld.def(v1), offset_tmp, Operand::c32(to_add));
         } else {
            Temp lo = bld.tmp(offset_tmp.type(), 1);
            Temp hi = bld.tmp(offset_tmp.type(), 1);
            bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), offset_tmp);

            if (offset_tmp.regClass() == s2) {
               Temp carry = bld.tmp(s1);
               lo = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.scc(Definition(carry)), lo,
                             Operand::c32(to_add));
               hi = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.def(s1, scc), hi, carry);
               offset = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2), lo, hi);
            } else {
               Temp new_lo = bld.tmp(v1);
               Temp carry =
                  bld.vadd32(Definition(new_lo), lo, Operand::c32(to_add), true).def(1).getTemp();
               hi = bld.vadd32(bld.def(v1), hi, Operand::zero(), false, carry);
               offset = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), new_lo, hi);
            }
         }
      }

      /* align offset down if needed */
      Operand aligned_offset = offset;
      unsigned align = align_offset ? 1 << (ffs(align_offset) - 1) : align_mul;
      if (need_to_align_offset) {
         align = 4;
         Temp offset_tmp = offset.isTemp() ? offset.getTemp() : Temp();
         if (offset.isConstant()) {
            aligned_offset = Operand::c32(offset.constantValue() & 0xfffffffcu);
         } else if (offset_tmp.regClass() == s1) {
            aligned_offset = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc),
                                      Operand::c32(0xfffffffcu), offset_tmp);
         } else if (offset_tmp.regClass() == s2) {
            aligned_offset = bld.sop2(aco_opcode::s_and_b64, bld.def(s2), bld.def(s1, scc),
                                      Operand::c64(0xfffffffffffffffcllu), offset_tmp);
         } else if (offset_tmp.regClass() == v1) {
            aligned_offset =
               bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(0xfffffffcu), offset_tmp);
         } else if (offset_tmp.regClass() == v2) {
            Temp hi = bld.tmp(v1), lo = bld.tmp(v1);
            bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), offset_tmp);
            lo = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(0xfffffffcu), lo);
            aligned_offset = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), lo, hi);
         }
      }
      Temp aligned_offset_tmp =
         aligned_offset.isTemp() ? aligned_offset.getTemp() : bld.copy(bld.def(s1), aligned_offset);

      Temp val = params.callback(bld, info, aligned_offset_tmp, bytes_needed, align,
                                 reduced_const_offset, byte_align ? Temp() : info.dst);

      /* the callback wrote directly to dst */
      if (val == info.dst) {
         assert(num_vals == 0);
         emit_split_vector(ctx, info.dst, info.num_components);
         return;
      }

      /* shift result right if needed */
      if (params.byte_align_loads && info.component_size < 4) {
         Operand byte_align_off = Operand::c32(byte_align);
         if (byte_align == -1) {
            if (offset.isConstant())
               byte_align_off = Operand::c32(offset.constantValue() % 4u);
            else if (offset.size() == 2)
               byte_align_off = Operand(emit_extract_vector(ctx, offset.getTemp(), 0,
                                                            RegClass(offset.getTemp().type(), 1)));
            else
               byte_align_off = offset;
         }

         assert(val.bytes() >= load_size && "unimplemented");
         if (val.type() == RegType::sgpr)
            byte_align_scalar(ctx, val, byte_align_off, info.dst);
         else
            byte_align_vector(ctx, val, byte_align_off, info.dst, component_size);
         return;
      }

      /* add result to list and advance */
      if (info.component_stride) {
         assert(val.bytes() == info.component_size && "unimplemented");
         const_offset += info.component_stride;
         align_offset = (align_offset + info.component_stride) % align_mul;
      } else {
         const_offset += val.bytes();
         align_offset = (align_offset + val.bytes()) % align_mul;
      }
      bytes_read += val.bytes();
      vals[num_vals++] = val;
   }

   /* create array of components */
   unsigned components_split = 0;
   std::array<Temp, NIR_MAX_VEC_COMPONENTS> allocated_vec;
   bool has_vgprs = false;
   for (unsigned i = 0; i < num_vals;) {
      Temp* const tmp = (Temp*)alloca(num_vals * sizeof(Temp));
      unsigned num_tmps = 0;
      unsigned tmp_size = 0;
      RegType reg_type = RegType::sgpr;
      while ((!tmp_size || (tmp_size % component_size)) && i < num_vals) {
         if (vals[i].type() == RegType::vgpr)
            reg_type = RegType::vgpr;
         tmp_size += vals[i].bytes();
         tmp[num_tmps++] = vals[i++];
      }
      if (num_tmps > 1) {
         aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(
            aco_opcode::p_create_vector, Format::PSEUDO, num_tmps, 1)};
         for (unsigned j = 0; j < num_tmps; j++)
            vec->operands[j] = Operand(tmp[j]);
         tmp[0] = bld.tmp(RegClass::get(reg_type, tmp_size));
         vec->definitions[0] = Definition(tmp[0]);
         bld.insert(std::move(vec));
      }

      if (tmp[0].bytes() % component_size) {
         /* trim tmp[0] */
         assert(i == num_vals);
         RegClass new_rc =
            RegClass::get(reg_type, tmp[0].bytes() / component_size * component_size);
         tmp[0] =
            bld.pseudo(aco_opcode::p_extract_vector, bld.def(new_rc), tmp[0], Operand::zero());
      }

      RegClass elem_rc = RegClass::get(reg_type, component_size);

      unsigned start = components_split;

      if (tmp_size == elem_rc.bytes()) {
         allocated_vec[components_split++] = tmp[0];
      } else {
         assert(tmp_size % elem_rc.bytes() == 0);
         aco_ptr<Pseudo_instruction> split{create_instruction<Pseudo_instruction>(
            aco_opcode::p_split_vector, Format::PSEUDO, 1, tmp_size / elem_rc.bytes())};
         for (auto& def : split->definitions) {
            Temp component = bld.tmp(elem_rc);
            allocated_vec[components_split++] = component;
            def = Definition(component);
         }
         split->operands[0] = Operand(tmp[0]);
         bld.insert(std::move(split));
      }

      /* try to p_as_uniform early so we can create more optimizable code and
       * also update allocated_vec */
      for (unsigned j = start; j < components_split; j++) {
         if (allocated_vec[j].bytes() % 4 == 0 && info.dst.type() == RegType::sgpr)
            allocated_vec[j] = bld.as_uniform(allocated_vec[j]);
         has_vgprs |= allocated_vec[j].type() == RegType::vgpr;
      }
   }

   /* concatenate components and p_as_uniform() result if needed */
   if (info.dst.type() == RegType::vgpr || !has_vgprs)
      ctx->allocated_vec.emplace(info.dst.id(), allocated_vec);

   int padding_bytes =
      MAX2((int)info.dst.bytes() - int(allocated_vec[0].bytes() * info.num_components), 0);

   aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(
      aco_opcode::p_create_vector, Format::PSEUDO, info.num_components + !!padding_bytes, 1)};
   for (unsigned i = 0; i < info.num_components; i++)
      vec->operands[i] = Operand(allocated_vec[i]);
   if (padding_bytes)
      vec->operands[info.num_components] = Operand(RegClass::get(RegType::vgpr, padding_bytes));
   if (info.dst.type() == RegType::sgpr && has_vgprs) {
      Temp tmp = bld.tmp(RegType::vgpr, info.dst.size());
      vec->definitions[0] = Definition(tmp);
      bld.insert(std::move(vec));
      bld.pseudo(aco_opcode::p_as_uniform, Definition(info.dst), tmp);
   } else {
      vec->definitions[0] = Definition(info.dst);
      bld.insert(std::move(vec));
   }
}

Operand
load_lds_size_m0(Builder& bld)
{
   /* TODO: m0 does not need to be initialized on GFX9+ */
   return bld.m0((Temp)bld.copy(bld.def(s1, m0), Operand::c32(0xffffffffu)));
}

Temp
lds_load_callback(Builder& bld, const LoadEmitInfo& info, Temp offset, unsigned bytes_needed,
                  unsigned align, unsigned const_offset, Temp dst_hint)
{
   offset = offset.regClass() == s1 ? bld.copy(bld.def(v1), offset) : offset;

   Operand m = load_lds_size_m0(bld);

   bool large_ds_read = bld.program->chip_class >= GFX7;
   bool usable_read2 = bld.program->chip_class >= GFX7;

   bool read2 = false;
   unsigned size = 0;
   aco_opcode op;
   if (bytes_needed >= 16 && align % 16 == 0 && large_ds_read) {
      size = 16;
      op = aco_opcode::ds_read_b128;
   } else if (bytes_needed >= 16 && align % 8 == 0 && const_offset % 8 == 0 && usable_read2) {
      size = 16;
      read2 = true;
      op = aco_opcode::ds_read2_b64;
   } else if (bytes_needed >= 12 && align % 16 == 0 && large_ds_read) {
      size = 12;
      op = aco_opcode::ds_read_b96;
   } else if (bytes_needed >= 8 && align % 8 == 0) {
      size = 8;
      op = aco_opcode::ds_read_b64;
   } else if (bytes_needed >= 8 && align % 4 == 0 && const_offset % 4 == 0 && usable_read2) {
      size = 8;
      read2 = true;
      op = aco_opcode::ds_read2_b32;
   } else if (bytes_needed >= 4 && align % 4 == 0) {
      size = 4;
      op = aco_opcode::ds_read_b32;
   } else if (bytes_needed >= 2 && align % 2 == 0) {
      size = 2;
      op = bld.program->chip_class >= GFX9 ? aco_opcode::ds_read_u16_d16 : aco_opcode::ds_read_u16;
   } else {
      size = 1;
      op = bld.program->chip_class >= GFX9 ? aco_opcode::ds_read_u8_d16 : aco_opcode::ds_read_u8;
   }

   unsigned const_offset_unit = read2 ? size / 2u : 1u;
   unsigned const_offset_range = read2 ? 255 * const_offset_unit : 65536;

   if (const_offset > (const_offset_range - const_offset_unit)) {
      unsigned excess = const_offset - (const_offset % const_offset_range);
      offset = bld.vadd32(bld.def(v1), offset, Operand::c32(excess));
      const_offset -= excess;
   }

   const_offset /= const_offset_unit;

   RegClass rc = RegClass::get(RegType::vgpr, size);
   Temp val = rc == info.dst.regClass() && dst_hint.id() ? dst_hint : bld.tmp(rc);
   Instruction* instr;
   if (read2)
      instr = bld.ds(op, Definition(val), offset, m, const_offset, const_offset + 1);
   else
      instr = bld.ds(op, Definition(val), offset, m, const_offset);
   instr->ds().sync = info.sync;

   return val;
}

const EmitLoadParameters lds_load_params{lds_load_callback, false, true, UINT32_MAX};

Temp
smem_load_callback(Builder& bld, const LoadEmitInfo& info, Temp offset, unsigned bytes_needed,
                   unsigned align, unsigned const_offset, Temp dst_hint)
{
   unsigned size = 0;
   aco_opcode op;
   if (bytes_needed <= 4) {
      size = 1;
      op = info.resource.id() ? aco_opcode::s_buffer_load_dword : aco_opcode::s_load_dword;
   } else if (bytes_needed <= 8) {
      size = 2;
      op = info.resource.id() ? aco_opcode::s_buffer_load_dwordx2 : aco_opcode::s_load_dwordx2;
   } else if (bytes_needed <= 16) {
      size = 4;
      op = info.resource.id() ? aco_opcode::s_buffer_load_dwordx4 : aco_opcode::s_load_dwordx4;
   } else if (bytes_needed <= 32) {
      size = 8;
      op = info.resource.id() ? aco_opcode::s_buffer_load_dwordx8 : aco_opcode::s_load_dwordx8;
   } else {
      size = 16;
      op = info.resource.id() ? aco_opcode::s_buffer_load_dwordx16 : aco_opcode::s_load_dwordx16;
   }
   aco_ptr<SMEM_instruction> load{create_instruction<SMEM_instruction>(op, Format::SMEM, 2, 1)};
   if (info.resource.id()) {
      load->operands[0] = Operand(info.resource);
      load->operands[1] = Operand(offset);
   } else {
      load->operands[0] = Operand(offset);
      load->operands[1] = Operand::zero();
   }
   RegClass rc(RegType::sgpr, size);
   Temp val = dst_hint.id() && dst_hint.regClass() == rc ? dst_hint : bld.tmp(rc);
   load->definitions[0] = Definition(val);
   load->glc = info.glc;
   load->dlc = info.glc && bld.program->chip_class >= GFX10;
   load->sync = info.sync;
   bld.insert(std::move(load));
   return val;
}

const EmitLoadParameters smem_load_params{smem_load_callback, true, false, 1024};

Temp
mubuf_load_callback(Builder& bld, const LoadEmitInfo& info, Temp offset, unsigned bytes_needed,
                    unsigned align_, unsigned const_offset, Temp dst_hint)
{
   Operand vaddr = offset.type() == RegType::vgpr ? Operand(offset) : Operand(v1);
   Operand soffset = offset.type() == RegType::sgpr ? Operand(offset) : Operand::c32(0);

   if (info.soffset.id()) {
      if (soffset.isTemp())
         vaddr = bld.copy(bld.def(v1), soffset);
      soffset = Operand(info.soffset);
   }

   unsigned bytes_size = 0;
   aco_opcode op;
   if (bytes_needed == 1 || align_ % 2) {
      bytes_size = 1;
      op = aco_opcode::buffer_load_ubyte;
   } else if (bytes_needed == 2 || align_ % 4) {
      bytes_size = 2;
      op = aco_opcode::buffer_load_ushort;
   } else if (bytes_needed <= 4) {
      bytes_size = 4;
      op = aco_opcode::buffer_load_dword;
   } else if (bytes_needed <= 8) {
      bytes_size = 8;
      op = aco_opcode::buffer_load_dwordx2;
   } else if (bytes_needed <= 12 && bld.program->chip_class > GFX6) {
      bytes_size = 12;
      op = aco_opcode::buffer_load_dwordx3;
   } else {
      bytes_size = 16;
      op = aco_opcode::buffer_load_dwordx4;
   }
   aco_ptr<MUBUF_instruction> mubuf{create_instruction<MUBUF_instruction>(op, Format::MUBUF, 3, 1)};
   mubuf->operands[0] = Operand(info.resource);
   mubuf->operands[1] = vaddr;
   mubuf->operands[2] = soffset;
   mubuf->offen = (offset.type() == RegType::vgpr);
   mubuf->glc = info.glc;
   mubuf->dlc = info.glc && bld.program->chip_class >= GFX10;
   mubuf->slc = info.slc;
   mubuf->sync = info.sync;
   mubuf->offset = const_offset;
   mubuf->swizzled = info.swizzle_component_size != 0;
   RegClass rc = RegClass::get(RegType::vgpr, bytes_size);
   Temp val = dst_hint.id() && rc == dst_hint.regClass() ? dst_hint : bld.tmp(rc);
   mubuf->definitions[0] = Definition(val);
   bld.insert(std::move(mubuf));

   return val;
}

const EmitLoadParameters mubuf_load_params{mubuf_load_callback, true, true, 4096};
const EmitLoadParameters scratch_load_params{mubuf_load_callback, false, true, 4096};

Temp
get_gfx6_global_rsrc(Builder& bld, Temp addr)
{
   uint32_t rsrc_conf = S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                        S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);

   if (addr.type() == RegType::vgpr)
      return bld.pseudo(aco_opcode::p_create_vector, bld.def(s4), Operand::zero(), Operand::zero(),
                        Operand::c32(-1u), Operand::c32(rsrc_conf));
   return bld.pseudo(aco_opcode::p_create_vector, bld.def(s4), addr, Operand::c32(-1u),
                     Operand::c32(rsrc_conf));
}

Temp
global_load_callback(Builder& bld, const LoadEmitInfo& info, Temp offset, unsigned bytes_needed,
                     unsigned align_, unsigned const_offset, Temp dst_hint)
{
   unsigned bytes_size = 0;
   bool use_mubuf = bld.program->chip_class == GFX6;
   bool global = bld.program->chip_class >= GFX9;
   aco_opcode op;
   if (bytes_needed == 1) {
      bytes_size = 1;
      op = use_mubuf ? aco_opcode::buffer_load_ubyte
           : global  ? aco_opcode::global_load_ubyte
                     : aco_opcode::flat_load_ubyte;
   } else if (bytes_needed == 2) {
      bytes_size = 2;
      op = use_mubuf ? aco_opcode::buffer_load_ushort
           : global  ? aco_opcode::global_load_ushort
                     : aco_opcode::flat_load_ushort;
   } else if (bytes_needed <= 4) {
      bytes_size = 4;
      op = use_mubuf ? aco_opcode::buffer_load_dword
           : global  ? aco_opcode::global_load_dword
                     : aco_opcode::flat_load_dword;
   } else if (bytes_needed <= 8) {
      bytes_size = 8;
      op = use_mubuf ? aco_opcode::buffer_load_dwordx2
           : global  ? aco_opcode::global_load_dwordx2
                     : aco_opcode::flat_load_dwordx2;
   } else if (bytes_needed <= 12 && !use_mubuf) {
      bytes_size = 12;
      op = global ? aco_opcode::global_load_dwordx3 : aco_opcode::flat_load_dwordx3;
   } else {
      bytes_size = 16;
      op = use_mubuf ? aco_opcode::buffer_load_dwordx4
           : global  ? aco_opcode::global_load_dwordx4
                     : aco_opcode::flat_load_dwordx4;
   }
   RegClass rc = RegClass::get(RegType::vgpr, align(bytes_size, 4));
   Temp val = dst_hint.id() && rc == dst_hint.regClass() ? dst_hint : bld.tmp(rc);
   if (use_mubuf) {
      aco_ptr<MUBUF_instruction> mubuf{
         create_instruction<MUBUF_instruction>(op, Format::MUBUF, 3, 1)};
      mubuf->operands[0] = Operand(get_gfx6_global_rsrc(bld, offset));
      mubuf->operands[1] = offset.type() == RegType::vgpr ? Operand(offset) : Operand(v1);
      mubuf->operands[2] = Operand::zero();
      mubuf->glc = info.glc;
      mubuf->dlc = false;
      mubuf->offset = 0;
      mubuf->addr64 = offset.type() == RegType::vgpr;
      mubuf->disable_wqm = false;
      mubuf->sync = info.sync;
      mubuf->definitions[0] = Definition(val);
      bld.insert(std::move(mubuf));
   } else {
      offset = offset.regClass() == s2 ? bld.copy(bld.def(v2), offset) : offset;

      aco_ptr<FLAT_instruction> flat{
         create_instruction<FLAT_instruction>(op, global ? Format::GLOBAL : Format::FLAT, 2, 1)};
      flat->operands[0] = Operand(offset);
      flat->operands[1] = Operand(s1);
      flat->glc = info.glc;
      flat->dlc = info.glc && bld.program->chip_class >= GFX10;
      flat->sync = info.sync;
      flat->offset = 0u;
      flat->definitions[0] = Definition(val);
      bld.insert(std::move(flat));
   }

   return val;
}

const EmitLoadParameters global_load_params{global_load_callback, true, true, 1};

Temp
load_lds(isel_context* ctx, unsigned elem_size_bytes, unsigned num_components, Temp dst,
         Temp address, unsigned base_offset, unsigned align)
{
   assert(util_is_power_of_two_nonzero(align));

   Builder bld(ctx->program, ctx->block);

   LoadEmitInfo info = {Operand(as_vgpr(ctx, address)), dst, num_components, elem_size_bytes};
   info.align_mul = align;
   info.align_offset = 0;
   info.sync = memory_sync_info(storage_shared);
   info.const_offset = base_offset;
   emit_load(ctx, bld, info, lds_load_params);

   return dst;
}

void
split_store_data(isel_context* ctx, RegType dst_type, unsigned count, Temp* dst, unsigned* bytes,
                 Temp src)
{
   if (!count)
      return;

   Builder bld(ctx->program, ctx->block);

   /* count == 1 fast path */
   if (count == 1) {
      if (dst_type == RegType::sgpr)
         dst[0] = bld.as_uniform(src);
      else
         dst[0] = as_vgpr(ctx, src);
      return;
   }

   /* elem_size_bytes is the greatest common divisor which is a power of 2 */
   unsigned elem_size_bytes =
      1u << (ffs(std::accumulate(bytes, bytes + count, 8, std::bit_or<>{})) - 1);

   ASSERTED bool is_subdword = elem_size_bytes < 4;
   assert(!is_subdword || dst_type == RegType::vgpr);

   for (unsigned i = 0; i < count; i++)
      dst[i] = bld.tmp(RegClass::get(dst_type, bytes[i]));

   std::vector<Temp> temps;
   /* use allocated_vec if possible */
   auto it = ctx->allocated_vec.find(src.id());
   if (it != ctx->allocated_vec.end()) {
      if (!it->second[0].id())
         goto split;
      unsigned elem_size = it->second[0].bytes();
      assert(src.bytes() % elem_size == 0);

      for (unsigned i = 0; i < src.bytes() / elem_size; i++) {
         if (!it->second[i].id())
            goto split;
      }
      if (elem_size_bytes % elem_size)
         goto split;

      temps.insert(temps.end(), it->second.begin(), it->second.begin() + src.bytes() / elem_size);
      elem_size_bytes = elem_size;
   }

split:
   /* split src if necessary */
   if (temps.empty()) {
      if (is_subdword && src.type() == RegType::sgpr)
         src = as_vgpr(ctx, src);
      if (dst_type == RegType::sgpr)
         src = bld.as_uniform(src);

      unsigned num_elems = src.bytes() / elem_size_bytes;
      aco_ptr<Instruction> split{create_instruction<Pseudo_instruction>(
         aco_opcode::p_split_vector, Format::PSEUDO, 1, num_elems)};
      split->operands[0] = Operand(src);
      for (unsigned i = 0; i < num_elems; i++) {
         temps.emplace_back(bld.tmp(RegClass::get(dst_type, elem_size_bytes)));
         split->definitions[i] = Definition(temps.back());
      }
      bld.insert(std::move(split));
   }

   unsigned idx = 0;
   for (unsigned i = 0; i < count; i++) {
      unsigned op_count = dst[i].bytes() / elem_size_bytes;
      if (op_count == 1) {
         if (dst_type == RegType::sgpr)
            dst[i] = bld.as_uniform(temps[idx++]);
         else
            dst[i] = as_vgpr(ctx, temps[idx++]);
         continue;
      }

      aco_ptr<Instruction> vec{create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector,
                                                                      Format::PSEUDO, op_count, 1)};
      for (unsigned j = 0; j < op_count; j++) {
         Temp tmp = temps[idx++];
         if (dst_type == RegType::sgpr)
            tmp = bld.as_uniform(tmp);
         vec->operands[j] = Operand(tmp);
      }
      vec->definitions[0] = Definition(dst[i]);
      bld.insert(std::move(vec));
   }
   return;
}

bool
scan_write_mask(uint32_t mask, uint32_t todo_mask, int* start, int* count)
{
   unsigned start_elem = ffs(todo_mask) - 1;
   bool skip = !(mask & (1 << start_elem));
   if (skip)
      mask = ~mask & todo_mask;

   mask &= todo_mask;

   u_bit_scan_consecutive_range(&mask, start, count);

   return !skip;
}

void
advance_write_mask(uint32_t* todo_mask, int start, int count)
{
   *todo_mask &= ~u_bit_consecutive(0, count) << start;
}

void
store_lds(isel_context* ctx, unsigned elem_size_bytes, Temp data, uint32_t wrmask, Temp address,
          unsigned base_offset, unsigned align)
{
   assert(util_is_power_of_two_nonzero(align));
   assert(util_is_power_of_two_nonzero(elem_size_bytes) && elem_size_bytes <= 8);

   Builder bld(ctx->program, ctx->block);
   bool large_ds_write = ctx->options->chip_class >= GFX7;
   bool usable_write2 = ctx->options->chip_class >= GFX7;

   unsigned write_count = 0;
   Temp write_datas[32];
   unsigned offsets[32];
   unsigned bytes[32];
   aco_opcode opcodes[32];

   wrmask = widen_mask(wrmask, elem_size_bytes);

   uint32_t todo = u_bit_consecutive(0, data.bytes());
   while (todo) {
      int offset, byte;
      if (!scan_write_mask(wrmask, todo, &offset, &byte)) {
         offsets[write_count] = offset;
         bytes[write_count] = byte;
         opcodes[write_count] = aco_opcode::num_opcodes;
         write_count++;
         advance_write_mask(&todo, offset, byte);
         continue;
      }

      bool aligned2 = offset % 2 == 0 && align % 2 == 0;
      bool aligned4 = offset % 4 == 0 && align % 4 == 0;
      bool aligned8 = offset % 8 == 0 && align % 8 == 0;
      bool aligned16 = offset % 16 == 0 && align % 16 == 0;

      // TODO: use ds_write_b8_d16_hi/ds_write_b16_d16_hi if beneficial
      aco_opcode op = aco_opcode::num_opcodes;
      if (byte >= 16 && aligned16 && large_ds_write) {
         op = aco_opcode::ds_write_b128;
         byte = 16;
      } else if (byte >= 12 && aligned16 && large_ds_write) {
         op = aco_opcode::ds_write_b96;
         byte = 12;
      } else if (byte >= 8 && aligned8) {
         op = aco_opcode::ds_write_b64;
         byte = 8;
      } else if (byte >= 4 && aligned4) {
         op = aco_opcode::ds_write_b32;
         byte = 4;
      } else if (byte >= 2 && aligned2) {
         op = aco_opcode::ds_write_b16;
         byte = 2;
      } else if (byte >= 1) {
         op = aco_opcode::ds_write_b8;
         byte = 1;
      } else {
         assert(false);
      }

      offsets[write_count] = offset;
      bytes[write_count] = byte;
      opcodes[write_count] = op;
      write_count++;
      advance_write_mask(&todo, offset, byte);
   }

   Operand m = load_lds_size_m0(bld);

   split_store_data(ctx, RegType::vgpr, write_count, write_datas, bytes, data);

   for (unsigned i = 0; i < write_count; i++) {
      aco_opcode op = opcodes[i];
      if (op == aco_opcode::num_opcodes)
         continue;

      Temp split_data = write_datas[i];

      unsigned second = write_count;
      if (usable_write2 && (op == aco_opcode::ds_write_b32 || op == aco_opcode::ds_write_b64)) {
         for (second = i + 1; second < write_count; second++) {
            if (opcodes[second] == op && (offsets[second] - offsets[i]) % split_data.bytes() == 0) {
               op = split_data.bytes() == 4 ? aco_opcode::ds_write2_b32 : aco_opcode::ds_write2_b64;
               opcodes[second] = aco_opcode::num_opcodes;
               break;
            }
         }
      }

      bool write2 = op == aco_opcode::ds_write2_b32 || op == aco_opcode::ds_write2_b64;
      unsigned write2_off = (offsets[second] - offsets[i]) / split_data.bytes();

      unsigned inline_offset = base_offset + offsets[i];
      unsigned max_offset = write2 ? (255 - write2_off) * split_data.bytes() : 65535;
      Temp address_offset = address;
      if (inline_offset > max_offset) {
         address_offset = bld.vadd32(bld.def(v1), Operand::c32(base_offset), address_offset);
         inline_offset = offsets[i];
      }

      /* offsets[i] shouldn't be large enough for this to happen */
      assert(inline_offset <= max_offset);

      Instruction* instr;
      if (write2) {
         Temp second_data = write_datas[second];
         inline_offset /= split_data.bytes();
         instr = bld.ds(op, address_offset, split_data, second_data, m, inline_offset,
                        inline_offset + write2_off);
      } else {
         instr = bld.ds(op, address_offset, split_data, m, inline_offset);
      }
      instr->ds().sync = memory_sync_info(storage_shared);
   }
}

aco_opcode
get_buffer_store_op(unsigned bytes)
{
   switch (bytes) {
   case 1: return aco_opcode::buffer_store_byte;
   case 2: return aco_opcode::buffer_store_short;
   case 4: return aco_opcode::buffer_store_dword;
   case 8: return aco_opcode::buffer_store_dwordx2;
   case 12: return aco_opcode::buffer_store_dwordx3;
   case 16: return aco_opcode::buffer_store_dwordx4;
   }
   unreachable("Unexpected store size");
   return aco_opcode::num_opcodes;
}

void
split_buffer_store(isel_context* ctx, nir_intrinsic_instr* instr, bool smem, RegType dst_type,
                   Temp data, unsigned writemask, int swizzle_element_size, unsigned* write_count,
                   Temp* write_datas, unsigned* offsets)
{
   unsigned write_count_with_skips = 0;
   bool skips[16];
   unsigned bytes[16];

   /* determine how to split the data */
   unsigned todo = u_bit_consecutive(0, data.bytes());
   while (todo) {
      int offset, byte;
      skips[write_count_with_skips] = !scan_write_mask(writemask, todo, &offset, &byte);
      offsets[write_count_with_skips] = offset;
      if (skips[write_count_with_skips]) {
         bytes[write_count_with_skips] = byte;
         advance_write_mask(&todo, offset, byte);
         write_count_with_skips++;
         continue;
      }

      /* only supported sizes are 1, 2, 4, 8, 12 and 16 bytes and can't be
       * larger than swizzle_element_size */
      byte = MIN2(byte, swizzle_element_size);
      if (byte % 4)
         byte = byte > 4 ? byte & ~0x3 : MIN2(byte, 2);

      /* SMEM and GFX6 VMEM can't emit 12-byte stores */
      if ((ctx->program->chip_class == GFX6 || smem) && byte == 12)
         byte = 8;

      /* dword or larger stores have to be dword-aligned */
      unsigned align_mul = instr ? nir_intrinsic_align_mul(instr) : 4;
      unsigned align_offset = (instr ? nir_intrinsic_align_offset(instr) : 0) + offset;
      bool dword_aligned = align_offset % 4 == 0 && align_mul % 4 == 0;
      if (!dword_aligned)
         byte = MIN2(byte, (align_offset % 2 == 0 && align_mul % 2 == 0) ? 2 : 1);

      bytes[write_count_with_skips] = byte;
      advance_write_mask(&todo, offset, byte);
      write_count_with_skips++;
   }

   /* actually split data */
   split_store_data(ctx, dst_type, write_count_with_skips, write_datas, bytes, data);

   /* remove skips */
   for (unsigned i = 0; i < write_count_with_skips; i++) {
      if (skips[i])
         continue;
      write_datas[*write_count] = write_datas[i];
      offsets[*write_count] = offsets[i];
      (*write_count)++;
   }
}

Temp
create_vec_from_array(isel_context* ctx, Temp arr[], unsigned cnt, RegType reg_type,
                      unsigned elem_size_bytes, unsigned split_cnt = 0u, Temp dst = Temp())
{
   Builder bld(ctx->program, ctx->block);
   unsigned dword_size = elem_size_bytes / 4;

   if (!dst.id())
      dst = bld.tmp(RegClass(reg_type, cnt * dword_size));

   std::array<Temp, NIR_MAX_VEC_COMPONENTS> allocated_vec;
   aco_ptr<Pseudo_instruction> instr{
      create_instruction<Pseudo_instruction>(aco_opcode::p_create_vector, Format::PSEUDO, cnt, 1)};
   instr->definitions[0] = Definition(dst);

   for (unsigned i = 0; i < cnt; ++i) {
      if (arr[i].id()) {
         assert(arr[i].size() == dword_size);
         allocated_vec[i] = arr[i];
         instr->operands[i] = Operand(arr[i]);
      } else {
         Temp zero = bld.copy(bld.def(RegClass(reg_type, dword_size)),
                              Operand::zero(dword_size == 2 ? 8 : 4));
         allocated_vec[i] = zero;
         instr->operands[i] = Operand(zero);
      }
   }

   bld.insert(std::move(instr));

   if (split_cnt)
      emit_split_vector(ctx, dst, split_cnt);
   else
      ctx->allocated_vec.emplace(dst.id(), allocated_vec); /* emit_split_vector already does this */

   return dst;
}

inline unsigned
resolve_excess_vmem_const_offset(Builder& bld, Temp& voffset, unsigned const_offset)
{
   if (const_offset >= 4096) {
      unsigned excess_const_offset = const_offset / 4096u * 4096u;
      const_offset %= 4096u;

      if (!voffset.id())
         voffset = bld.copy(bld.def(v1), Operand::c32(excess_const_offset));
      else if (unlikely(voffset.regClass() == s1))
         voffset = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.def(s1, scc),
                            Operand::c32(excess_const_offset), Operand(voffset));
      else if (likely(voffset.regClass() == v1))
         voffset = bld.vadd32(bld.def(v1), Operand(voffset), Operand::c32(excess_const_offset));
      else
         unreachable("Unsupported register class of voffset");
   }

   return const_offset;
}

void
emit_single_mubuf_store(isel_context* ctx, Temp descriptor, Temp voffset, Temp soffset, Temp vdata,
                        unsigned const_offset = 0u, memory_sync_info sync = memory_sync_info(),
                        bool slc = false, bool swizzled = false)
{
   assert(vdata.id());
   assert(vdata.size() != 3 || ctx->program->chip_class != GFX6);
   assert(vdata.size() >= 1 && vdata.size() <= 4);

   Builder bld(ctx->program, ctx->block);
   aco_opcode op = get_buffer_store_op(vdata.bytes());
   const_offset = resolve_excess_vmem_const_offset(bld, voffset, const_offset);

   Operand voffset_op = voffset.id() ? Operand(as_vgpr(ctx, voffset)) : Operand(v1);
   Operand soffset_op = soffset.id() ? Operand(soffset) : Operand::zero();
   Builder::Result r =
      bld.mubuf(op, Operand(descriptor), voffset_op, soffset_op, Operand(vdata), const_offset,
                /* offen */ !voffset_op.isUndefined(), /* swizzled */ swizzled,
                /* idxen*/ false, /* addr64 */ false, /* disable_wqm */ false, /* glc */ true,
                /* dlc*/ false, /* slc */ slc);

   r.instr->mubuf().sync = sync;
}

void
store_vmem_mubuf(isel_context* ctx, Temp src, Temp descriptor, Temp voffset, Temp soffset,
                 unsigned base_const_offset, unsigned elem_size_bytes, unsigned write_mask,
                 bool allow_combining = true, memory_sync_info sync = memory_sync_info(),
                 bool slc = false)
{
   Builder bld(ctx->program, ctx->block);
   assert(elem_size_bytes == 2 || elem_size_bytes == 4 || elem_size_bytes == 8);
   assert(write_mask);
   write_mask = widen_mask(write_mask, elem_size_bytes);

   unsigned write_count = 0;
   Temp write_datas[32];
   unsigned offsets[32];
   split_buffer_store(ctx, NULL, false, RegType::vgpr, src, write_mask, allow_combining ? 16 : 4,
                      &write_count, write_datas, offsets);

   for (unsigned i = 0; i < write_count; i++) {
      unsigned const_offset = offsets[i] + base_const_offset;
      emit_single_mubuf_store(ctx, descriptor, voffset, soffset, write_datas[i], const_offset, sync,
                              slc, !allow_combining);
   }
}

void
load_vmem_mubuf(isel_context* ctx, Temp dst, Temp descriptor, Temp voffset, Temp soffset,
                unsigned base_const_offset, unsigned elem_size_bytes, unsigned num_components,
                unsigned stride = 0u, bool allow_combining = true, bool allow_reorder = true,
                bool slc = false)
{
   assert(elem_size_bytes == 2 || elem_size_bytes == 4 || elem_size_bytes == 8);
   assert((num_components * elem_size_bytes) == dst.bytes());
   assert(!!stride != allow_combining);

   Builder bld(ctx->program, ctx->block);

   LoadEmitInfo info = {Operand(voffset), dst, num_components, elem_size_bytes, descriptor};
   info.component_stride = allow_combining ? 0 : stride;
   info.glc = true;
   info.slc = slc;
   info.swizzle_component_size = allow_combining ? 0 : 4;
   info.align_mul = MIN2(elem_size_bytes, 4);
   info.align_offset = 0;
   info.soffset = soffset;
   info.const_offset = base_const_offset;
   emit_load(ctx, bld, info, mubuf_load_params);
}

Temp
wave_id_in_threadgroup(isel_context* ctx)
{
   Builder bld(ctx->program, ctx->block);
   return bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc),
                   get_arg(ctx, ctx->args->ac.merged_wave_info), Operand::c32(24u | (4u << 16)));
}

Temp
thread_id_in_threadgroup(isel_context* ctx)
{
   /* tid_in_tg = wave_id * wave_size + tid_in_wave */

   Builder bld(ctx->program, ctx->block);
   Temp tid_in_wave = emit_mbcnt(ctx, bld.tmp(v1));

   if (ctx->program->workgroup_size <= ctx->program->wave_size)
      return tid_in_wave;

   Temp wave_id_in_tg = wave_id_in_threadgroup(ctx);
   Temp num_pre_threads =
      bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), wave_id_in_tg,
               Operand::c32(ctx->program->wave_size == 64 ? 6u : 5u));
   return bld.vadd32(bld.def(v1), Operand(num_pre_threads), Operand(tid_in_wave));
}

Temp
get_tess_rel_patch_id(isel_context* ctx)
{
   Builder bld(ctx->program, ctx->block);

   switch (ctx->shader->info.stage) {
   case MESA_SHADER_TESS_CTRL:
      return bld.pseudo(aco_opcode::p_extract, bld.def(v1), get_arg(ctx, ctx->args->ac.tcs_rel_ids),
                        Operand::zero(), Operand::c32(8u), Operand::zero());
   case MESA_SHADER_TESS_EVAL: return get_arg(ctx, ctx->args->ac.tes_rel_patch_id);
   default: unreachable("Unsupported stage in get_tess_rel_patch_id");
   }
}

bool
store_output_to_temps(isel_context* ctx, nir_intrinsic_instr* instr)
{
   unsigned write_mask = nir_intrinsic_write_mask(instr);
   unsigned component = nir_intrinsic_component(instr);
   unsigned idx = nir_intrinsic_base(instr) * 4u + component;
   nir_src offset = *nir_get_io_offset_src(instr);

   if (!nir_src_is_const(offset) || nir_src_as_uint(offset))
      return false;

   Temp src = get_ssa_temp(ctx, instr->src[0].ssa);

   if (instr->src[0].ssa->bit_size == 64)
      write_mask = widen_mask(write_mask, 2);

   RegClass rc = instr->src[0].ssa->bit_size == 16 ? v2b : v1;

   for (unsigned i = 0; i < 8; ++i) {
      if (write_mask & (1 << i)) {
         ctx->outputs.mask[idx / 4u] |= 1 << (idx % 4u);
         ctx->outputs.temps[idx] = emit_extract_vector(ctx, src, i, rc);
      }
      idx++;
   }

   return true;
}

bool
load_input_from_temps(isel_context* ctx, nir_intrinsic_instr* instr, Temp dst)
{
   /* Only TCS per-vertex inputs are supported by this function.
    * Per-vertex inputs only match between the VS/TCS invocation id when the number of invocations
    * is the same.
    */
   if (ctx->shader->info.stage != MESA_SHADER_TESS_CTRL || !ctx->tcs_in_out_eq)
      return false;

   nir_src* off_src = nir_get_io_offset_src(instr);
   nir_src* vertex_index_src = nir_get_io_vertex_index_src(instr);
   nir_instr* vertex_index_instr = vertex_index_src->ssa->parent_instr;
   bool can_use_temps =
      nir_src_is_const(*off_src) && vertex_index_instr->type == nir_instr_type_intrinsic &&
      nir_instr_as_intrinsic(vertex_index_instr)->intrinsic == nir_intrinsic_load_invocation_id;

   if (!can_use_temps)
      return false;

   unsigned idx = nir_intrinsic_base(instr) * 4u + nir_intrinsic_component(instr) +
                  4 * nir_src_as_uint(*off_src);
   Temp* src = &ctx->inputs.temps[idx];
   create_vec_from_array(ctx, src, dst.size(), dst.regClass().type(), 4u, 0, dst);

   return true;
}

static void export_vs_varying(isel_context* ctx, int slot, bool is_pos, int* next_pos);

void
visit_store_output(isel_context* ctx, nir_intrinsic_instr* instr)
{
   if (ctx->stage == vertex_vs || ctx->stage == tess_eval_vs || ctx->stage == fragment_fs ||
       ctx->stage == vertex_ngg || ctx->stage == tess_eval_ngg ||
       (ctx->stage == vertex_tess_control_hs && ctx->shader->info.stage == MESA_SHADER_VERTEX) ||
       ctx->shader->info.stage == MESA_SHADER_GEOMETRY) {
      bool stored_to_temps = store_output_to_temps(ctx, instr);
      if (!stored_to_temps) {
         isel_err(instr->src[1].ssa->parent_instr, "Unimplemented output offset instruction");
         abort();
      }
   } else {
      unreachable("Shader stage not implemented");
   }

   /* For NGG VS and TES shaders the primitive ID is exported manually after the other exports so we
    * have to emit an exp here manually */
   if (ctx->stage.hw == HWStage::NGG &&
       (ctx->stage.has(SWStage::VS) || ctx->stage.has(SWStage::TES)) &&
       nir_intrinsic_io_semantics(instr).location == VARYING_SLOT_PRIMITIVE_ID)
      export_vs_varying(ctx, VARYING_SLOT_PRIMITIVE_ID, false, NULL);
}

void
emit_interp_instr(isel_context* ctx, unsigned idx, unsigned component, Temp src, Temp dst,
                  Temp prim_mask)
{
   Temp coord1 = emit_extract_vector(ctx, src, 0, v1);
   Temp coord2 = emit_extract_vector(ctx, src, 1, v1);

   Builder bld(ctx->program, ctx->block);

   if (dst.regClass() == v2b) {
      if (ctx->program->dev.has_16bank_lds) {
         assert(ctx->options->chip_class <= GFX8);
         Builder::Result interp_p1 =
            bld.vintrp(aco_opcode::v_interp_mov_f32, bld.def(v1), Operand::c32(2u) /* P0 */,
                       bld.m0(prim_mask), idx, component);
         interp_p1 = bld.vintrp(aco_opcode::v_interp_p1lv_f16, bld.def(v2b), coord1,
                                bld.m0(prim_mask), interp_p1, idx, component);
         bld.vintrp(aco_opcode::v_interp_p2_legacy_f16, Definition(dst), coord2, bld.m0(prim_mask),
                    interp_p1, idx, component);
      } else {
         aco_opcode interp_p2_op = aco_opcode::v_interp_p2_f16;

         if (ctx->options->chip_class == GFX8)
            interp_p2_op = aco_opcode::v_interp_p2_legacy_f16;

         Builder::Result interp_p1 = bld.vintrp(aco_opcode::v_interp_p1ll_f16, bld.def(v1), coord1,
                                                bld.m0(prim_mask), idx, component);
         bld.vintrp(interp_p2_op, Definition(dst), coord2, bld.m0(prim_mask), interp_p1, idx,
                    component);
      }
   } else {
      Builder::Result interp_p1 = bld.vintrp(aco_opcode::v_interp_p1_f32, bld.def(v1), coord1,
                                             bld.m0(prim_mask), idx, component);

      if (ctx->program->dev.has_16bank_lds)
         interp_p1.instr->operands[0].setLateKill(true);

      bld.vintrp(aco_opcode::v_interp_p2_f32, Definition(dst), coord2, bld.m0(prim_mask), interp_p1,
                 idx, component);
   }
}

void
emit_load_frag_coord(isel_context* ctx, Temp dst, unsigned num_components)
{
   Builder bld(ctx->program, ctx->block);

   aco_ptr<Pseudo_instruction> vec(create_instruction<Pseudo_instruction>(
      aco_opcode::p_create_vector, Format::PSEUDO, num_components, 1));
   for (unsigned i = 0; i < num_components; i++)
      vec->operands[i] = Operand(get_arg(ctx, ctx->args->ac.frag_pos[i]));
   if (G_0286CC_POS_W_FLOAT_ENA(ctx->program->config->spi_ps_input_ena)) {
      assert(num_components == 4);
      vec->operands[3] =
         bld.vop1(aco_opcode::v_rcp_f32, bld.def(v1), get_arg(ctx, ctx->args->ac.frag_pos[3]));
   }

   if (ctx->options->adjust_frag_coord_z &&
       G_0286CC_POS_Z_FLOAT_ENA(ctx->program->config->spi_ps_input_ena)) {
      /* Adjust gl_FragCoord.z for VRS due to a hw bug on some GFX10.3 chips. */
      Operand frag_z = vec->operands[2];
      Temp adjusted_frag_z = bld.tmp(v1);
      Temp tmp;

      /* dFdx fine */
      Temp tl = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), frag_z, dpp_quad_perm(0, 0, 2, 2));
      tmp = bld.vop2_dpp(aco_opcode::v_sub_f32, bld.def(v1), frag_z, tl, dpp_quad_perm(1, 1, 3, 3));
      emit_wqm(bld, tmp, adjusted_frag_z, true);

      /* adjusted_frag_z * 0.0625 + frag_z */
      adjusted_frag_z = bld.vop3(aco_opcode::v_fma_f32, bld.def(v1), adjusted_frag_z,
                                 Operand::c32(0x3d800000u /* 0.0625 */), frag_z);

      /* VRS Rate X = Ancillary[2:3] */
      Temp x_rate =
         bld.vop3(aco_opcode::v_bfe_u32, bld.def(v1), get_arg(ctx, ctx->args->ac.ancillary),
                  Operand::c32(2u), Operand::c32(2u));

      /* xRate = xRate == 0x1 ? adjusted_frag_z : frag_z. */
      Temp cond =
         bld.vopc(aco_opcode::v_cmp_eq_i32, bld.def(bld.lm), Operand::c32(1u), Operand(x_rate));
      vec->operands[2] =
         bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), frag_z, adjusted_frag_z, cond);
   }

   for (Operand& op : vec->operands)
      op = op.isUndefined() ? Operand::zero() : op;

   vec->definitions[0] = Definition(dst);
   ctx->block->instructions.emplace_back(std::move(vec));
   emit_split_vector(ctx, dst, num_components);
   return;
}

void
emit_load_frag_shading_rate(isel_context* ctx, Temp dst)
{
   Builder bld(ctx->program, ctx->block);
   Temp cond;

   /* VRS Rate X = Ancillary[2:3]
    * VRS Rate Y = Ancillary[4:5]
    */
   Temp x_rate = bld.vop3(aco_opcode::v_bfe_u32, bld.def(v1), get_arg(ctx, ctx->args->ac.ancillary),
                          Operand::c32(2u), Operand::c32(2u));
   Temp y_rate = bld.vop3(aco_opcode::v_bfe_u32, bld.def(v1), get_arg(ctx, ctx->args->ac.ancillary),
                          Operand::c32(4u), Operand::c32(2u));

   /* xRate = xRate == 0x1 ? Horizontal2Pixels : None. */
   cond = bld.vopc(aco_opcode::v_cmp_eq_i32, bld.def(bld.lm), Operand::c32(1u), Operand(x_rate));
   x_rate = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), bld.copy(bld.def(v1), Operand::zero()),
                     bld.copy(bld.def(v1), Operand::c32(4u)), cond);

   /* yRate = yRate == 0x1 ? Vertical2Pixels : None. */
   cond = bld.vopc(aco_opcode::v_cmp_eq_i32, bld.def(bld.lm), Operand::c32(1u), Operand(y_rate));
   y_rate = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), bld.copy(bld.def(v1), Operand::zero()),
                     bld.copy(bld.def(v1), Operand::c32(1u)), cond);

   bld.vop2(aco_opcode::v_or_b32, Definition(dst), Operand(x_rate), Operand(y_rate));
}

void
visit_load_interpolated_input(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   Temp coords = get_ssa_temp(ctx, instr->src[0].ssa);
   unsigned idx = nir_intrinsic_base(instr);
   unsigned component = nir_intrinsic_component(instr);
   Temp prim_mask = get_arg(ctx, ctx->args->ac.prim_mask);

   assert(nir_src_is_const(instr->src[1]) && !nir_src_as_uint(instr->src[1]));

   if (instr->dest.ssa.num_components == 1) {
      emit_interp_instr(ctx, idx, component, coords, dst, prim_mask);
   } else {
      aco_ptr<Pseudo_instruction> vec(create_instruction<Pseudo_instruction>(
         aco_opcode::p_create_vector, Format::PSEUDO, instr->dest.ssa.num_components, 1));
      for (unsigned i = 0; i < instr->dest.ssa.num_components; i++) {
         Temp tmp = ctx->program->allocateTmp(instr->dest.ssa.bit_size == 16 ? v2b : v1);
         emit_interp_instr(ctx, idx, component + i, coords, tmp, prim_mask);
         vec->operands[i] = Operand(tmp);
      }
      vec->definitions[0] = Definition(dst);
      ctx->block->instructions.emplace_back(std::move(vec));
   }
}

bool
check_vertex_fetch_size(isel_context* ctx, const ac_data_format_info* vtx_info, unsigned offset,
                        unsigned binding_align, unsigned channels)
{
   unsigned vertex_byte_size = vtx_info->chan_byte_size * channels;
   if (vtx_info->chan_byte_size != 4 && channels == 3)
      return false;

   /* Split typed vertex buffer loads on GFX6 and GFX10+ to avoid any
    * alignment issues that triggers memory violations and eventually a GPU
    * hang. This can happen if the stride (static or dynamic) is unaligned and
    * also if the VBO offset is aligned to a scalar (eg. stride is 8 and VBO
    * offset is 2 for R16G16B16A16_SNORM).
    */
   return (ctx->options->chip_class >= GFX7 && ctx->options->chip_class <= GFX9) ||
          (offset % vertex_byte_size == 0 && MAX2(binding_align, 1) % vertex_byte_size == 0);
}

uint8_t
get_fetch_data_format(isel_context* ctx, const ac_data_format_info* vtx_info, unsigned offset,
                      unsigned* channels, unsigned max_channels, unsigned binding_align)
{
   if (!vtx_info->chan_byte_size) {
      *channels = vtx_info->num_channels;
      return vtx_info->chan_format;
   }

   unsigned num_channels = *channels;
   if (!check_vertex_fetch_size(ctx, vtx_info, offset, binding_align, *channels)) {
      unsigned new_channels = num_channels + 1;
      /* first, assume more loads is worse and try using a larger data format */
      while (new_channels <= max_channels &&
             !check_vertex_fetch_size(ctx, vtx_info, offset, binding_align, new_channels)) {
         new_channels++;
      }

      if (new_channels > max_channels) {
         /* then try decreasing load size (at the cost of more loads) */
         new_channels = *channels;
         while (new_channels > 1 &&
                !check_vertex_fetch_size(ctx, vtx_info, offset, binding_align, new_channels))
            new_channels--;
      }

      if (new_channels < *channels)
         *channels = new_channels;
      num_channels = new_channels;
   }

   switch (vtx_info->chan_format) {
   case V_008F0C_BUF_DATA_FORMAT_8:
      return std::array<uint8_t, 4>{V_008F0C_BUF_DATA_FORMAT_8, V_008F0C_BUF_DATA_FORMAT_8_8,
                                    V_008F0C_BUF_DATA_FORMAT_INVALID,
                                    V_008F0C_BUF_DATA_FORMAT_8_8_8_8}[num_channels - 1];
   case V_008F0C_BUF_DATA_FORMAT_16:
      return std::array<uint8_t, 4>{V_008F0C_BUF_DATA_FORMAT_16, V_008F0C_BUF_DATA_FORMAT_16_16,
                                    V_008F0C_BUF_DATA_FORMAT_INVALID,
                                    V_008F0C_BUF_DATA_FORMAT_16_16_16_16}[num_channels - 1];
   case V_008F0C_BUF_DATA_FORMAT_32:
      return std::array<uint8_t, 4>{V_008F0C_BUF_DATA_FORMAT_32, V_008F0C_BUF_DATA_FORMAT_32_32,
                                    V_008F0C_BUF_DATA_FORMAT_32_32_32,
                                    V_008F0C_BUF_DATA_FORMAT_32_32_32_32}[num_channels - 1];
   }
   unreachable("shouldn't reach here");
   return V_008F0C_BUF_DATA_FORMAT_INVALID;
}

/* For 2_10_10_10 formats the alpha is handled as unsigned by pre-vega HW.
 * so we may need to fix it up. */
Temp
adjust_vertex_fetch_alpha(isel_context* ctx, unsigned adjustment, Temp alpha)
{
   Builder bld(ctx->program, ctx->block);

   if (adjustment == AC_FETCH_FORMAT_SSCALED)
      alpha = bld.vop1(aco_opcode::v_cvt_u32_f32, bld.def(v1), alpha);

   /* For the integer-like cases, do a natural sign extension.
    *
    * For the SNORM case, the values are 0.0, 0.333, 0.666, 1.0
    * and happen to contain 0, 1, 2, 3 as the two LSBs of the
    * exponent.
    */
   unsigned offset = adjustment == AC_FETCH_FORMAT_SNORM ? 23u : 0u;
   alpha =
      bld.vop3(aco_opcode::v_bfe_i32, bld.def(v1), alpha, Operand::c32(offset), Operand::c32(2u));

   /* Convert back to the right type. */
   if (adjustment == AC_FETCH_FORMAT_SNORM) {
      alpha = bld.vop1(aco_opcode::v_cvt_f32_i32, bld.def(v1), alpha);
      alpha = bld.vop2(aco_opcode::v_max_f32, bld.def(v1), Operand::c32(0xbf800000u), alpha);
   } else if (adjustment == AC_FETCH_FORMAT_SSCALED) {
      alpha = bld.vop1(aco_opcode::v_cvt_f32_i32, bld.def(v1), alpha);
   }

   return alpha;
}

void
visit_load_input(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   nir_src offset = *nir_get_io_offset_src(instr);

   if (ctx->shader->info.stage == MESA_SHADER_VERTEX) {

      if (!nir_src_is_const(offset) || nir_src_as_uint(offset))
         isel_err(offset.ssa->parent_instr,
                  "Unimplemented non-zero nir_intrinsic_load_input offset");

      Temp vertex_buffers =
         convert_pointer_to_64_bit(ctx, get_arg(ctx, ctx->args->ac.vertex_buffers));

      unsigned location = nir_intrinsic_base(instr) - VERT_ATTRIB_GENERIC0;
      unsigned component = nir_intrinsic_component(instr);
      unsigned bitsize = instr->dest.ssa.bit_size;
      unsigned attrib_binding = ctx->options->key.vs.vertex_attribute_bindings[location];
      uint32_t attrib_offset = ctx->options->key.vs.vertex_attribute_offsets[location];
      uint32_t attrib_stride = ctx->options->key.vs.vertex_attribute_strides[location];
      unsigned attrib_format = ctx->options->key.vs.vertex_attribute_formats[location];
      unsigned binding_align = ctx->options->key.vs.vertex_binding_align[attrib_binding];
      enum ac_fetch_format alpha_adjust = ctx->options->key.vs.alpha_adjust[location];

      unsigned dfmt = attrib_format & 0xf;
      unsigned nfmt = (attrib_format >> 4) & 0x7;
      const struct ac_data_format_info* vtx_info = ac_get_data_format_info(dfmt);

      unsigned mask = nir_ssa_def_components_read(&instr->dest.ssa) << component;
      unsigned num_channels = MIN2(util_last_bit(mask), vtx_info->num_channels);
      bool post_shuffle = ctx->options->key.vs.post_shuffle & (1 << location);
      if (post_shuffle)
         num_channels = MAX2(num_channels, 3);

      unsigned desc_index =
         ctx->program->info->vs.use_per_attribute_vb_descs ? location : attrib_binding;
      desc_index = util_bitcount(ctx->program->info->vs.vb_desc_usage_mask &
                                 u_bit_consecutive(0, desc_index));
      Operand off = bld.copy(bld.def(s1), Operand::c32(desc_index * 16u));
      Temp list = bld.smem(aco_opcode::s_load_dwordx4, bld.def(s4), vertex_buffers, off);

      Temp index;
      if (ctx->options->key.vs.instance_rate_inputs & (1u << location)) {
         uint32_t divisor = ctx->options->key.vs.instance_rate_divisors[location];
         Temp start_instance = get_arg(ctx, ctx->args->ac.start_instance);
         if (divisor) {
            Temp instance_id = get_arg(ctx, ctx->args->ac.instance_id);
            if (divisor != 1) {
               Temp divided = bld.tmp(v1);
               emit_v_div_u32(ctx, divided, as_vgpr(ctx, instance_id), divisor);
               index = bld.vadd32(bld.def(v1), start_instance, divided);
            } else {
               index = bld.vadd32(bld.def(v1), start_instance, instance_id);
            }
         } else {
            index = bld.copy(bld.def(v1), start_instance);
         }
      } else {
         index = bld.vadd32(bld.def(v1), get_arg(ctx, ctx->args->ac.base_vertex),
                            get_arg(ctx, ctx->args->ac.vertex_id));
      }

      Temp* const channels = (Temp*)alloca(num_channels * sizeof(Temp));
      unsigned channel_start = 0;
      bool direct_fetch = false;

      /* skip unused channels at the start */
      if (vtx_info->chan_byte_size && !post_shuffle) {
         channel_start = ffs(mask) - 1;
         for (unsigned i = 0; i < MIN2(channel_start, num_channels); i++)
            channels[i] = Temp(0, s1);
      } else if (vtx_info->chan_byte_size && post_shuffle && !(mask & 0x8)) {
         num_channels = 3 - (ffs(mask) - 1);
      }

      /* load channels */
      while (channel_start < num_channels) {
         unsigned fetch_component = num_channels - channel_start;
         unsigned fetch_offset = attrib_offset + channel_start * vtx_info->chan_byte_size;
         bool expanded = false;

         /* use MUBUF when possible to avoid possible alignment issues */
         /* TODO: we could use SDWA to unpack 8/16-bit attributes without extra instructions */
         bool use_mubuf =
            (nfmt == V_008F0C_BUF_NUM_FORMAT_FLOAT || nfmt == V_008F0C_BUF_NUM_FORMAT_UINT ||
             nfmt == V_008F0C_BUF_NUM_FORMAT_SINT) &&
            vtx_info->chan_byte_size == 4;
         unsigned fetch_dfmt = V_008F0C_BUF_DATA_FORMAT_INVALID;
         if (!use_mubuf) {
            fetch_dfmt =
               get_fetch_data_format(ctx, vtx_info, fetch_offset, &fetch_component,
                                     vtx_info->num_channels - channel_start, binding_align);
         } else {
            if (fetch_component == 3 && ctx->options->chip_class == GFX6) {
               /* GFX6 only supports loading vec3 with MTBUF, expand to vec4. */
               fetch_component = 4;
               expanded = true;
            }
         }

         unsigned fetch_bytes = fetch_component * bitsize / 8;

         Temp fetch_index = index;
         if (attrib_stride != 0 && fetch_offset > attrib_stride) {
            fetch_index =
               bld.vadd32(bld.def(v1), Operand::c32(fetch_offset / attrib_stride), fetch_index);
            fetch_offset = fetch_offset % attrib_stride;
         }

         Operand soffset = Operand::zero();
         if (fetch_offset >= 4096) {
            soffset = bld.copy(bld.def(s1), Operand::c32(fetch_offset / 4096 * 4096));
            fetch_offset %= 4096;
         }

         aco_opcode opcode;
         switch (fetch_bytes) {
         case 2:
            assert(!use_mubuf && bitsize == 16);
            opcode = aco_opcode::tbuffer_load_format_d16_x;
            break;
         case 4:
            if (bitsize == 16) {
               assert(!use_mubuf);
               opcode = aco_opcode::tbuffer_load_format_d16_xy;
            } else {
               opcode =
                  use_mubuf ? aco_opcode::buffer_load_dword : aco_opcode::tbuffer_load_format_x;
            }
            break;
         case 6:
            assert(!use_mubuf && bitsize == 16);
            opcode = aco_opcode::tbuffer_load_format_d16_xyz;
            break;
         case 8:
            if (bitsize == 16) {
               assert(!use_mubuf);
               opcode = aco_opcode::tbuffer_load_format_d16_xyzw;
            } else {
               opcode =
                  use_mubuf ? aco_opcode::buffer_load_dwordx2 : aco_opcode::tbuffer_load_format_xy;
            }
            break;
         case 12:
            assert(ctx->options->chip_class >= GFX7 ||
                   (!use_mubuf && ctx->options->chip_class == GFX6));
            opcode =
               use_mubuf ? aco_opcode::buffer_load_dwordx3 : aco_opcode::tbuffer_load_format_xyz;
            break;
         case 16:
            opcode =
               use_mubuf ? aco_opcode::buffer_load_dwordx4 : aco_opcode::tbuffer_load_format_xyzw;
            break;
         default: unreachable("Unimplemented load_input vector size");
         }

         Temp fetch_dst;
         if (channel_start == 0 && fetch_bytes == dst.bytes() && !post_shuffle && !expanded &&
             (alpha_adjust == AC_FETCH_FORMAT_NONE || num_channels <= 3)) {
            direct_fetch = true;
            fetch_dst = dst;
         } else {
            fetch_dst = bld.tmp(RegClass::get(RegType::vgpr, fetch_bytes));
         }

         if (use_mubuf) {
            Instruction* mubuf = bld.mubuf(opcode, Definition(fetch_dst), list, fetch_index,
                                           soffset, fetch_offset, false, false, true)
                                    .instr;
            mubuf->mubuf().vtx_binding = attrib_binding + 1;
         } else {
            Instruction* mtbuf = bld.mtbuf(opcode, Definition(fetch_dst), list, fetch_index,
                                           soffset, fetch_dfmt, nfmt, fetch_offset, false, true)
                                    .instr;
            mtbuf->mtbuf().vtx_binding = attrib_binding + 1;
         }

         emit_split_vector(ctx, fetch_dst, fetch_dst.size());

         if (fetch_component == 1) {
            channels[channel_start] = fetch_dst;
         } else {
            for (unsigned i = 0; i < MIN2(fetch_component, num_channels - channel_start); i++)
               channels[channel_start + i] =
                  emit_extract_vector(ctx, fetch_dst, i, bitsize == 16 ? v2b : v1);
         }

         channel_start += fetch_component;
      }

      if (!direct_fetch) {
         bool is_float =
            nfmt != V_008F0C_BUF_NUM_FORMAT_UINT && nfmt != V_008F0C_BUF_NUM_FORMAT_SINT;

         static const unsigned swizzle_normal[4] = {0, 1, 2, 3};
         static const unsigned swizzle_post_shuffle[4] = {2, 1, 0, 3};
         const unsigned* swizzle = post_shuffle ? swizzle_post_shuffle : swizzle_normal;
         unsigned num_components = instr->dest.ssa.num_components;

         aco_ptr<Instruction> vec{create_instruction<Pseudo_instruction>(
            aco_opcode::p_create_vector, Format::PSEUDO, num_components, 1)};
         std::array<Temp, NIR_MAX_VEC_COMPONENTS> elems;
         unsigned num_temp = 0;
         for (unsigned i = 0; i < num_components; i++) {
            unsigned idx = i + component;
            if (swizzle[idx] < num_channels && channels[swizzle[idx]].id()) {
               Temp channel = channels[swizzle[idx]];
               if (idx == 3 && alpha_adjust != AC_FETCH_FORMAT_NONE)
                  channel = adjust_vertex_fetch_alpha(ctx, alpha_adjust, channel);
               vec->operands[i] = Operand(channel);

               num_temp++;
               elems[i] = channel;
            } else if (is_float && idx == 3) {
               vec->operands[i] = Operand::c32(0x3f800000u);
            } else if (!is_float && idx == 3) {
               vec->operands[i] = Operand::c32(1u);
            } else {
               vec->operands[i] = Operand::zero();
            }
         }
         vec->definitions[0] = Definition(dst);
         ctx->block->instructions.emplace_back(std::move(vec));
         emit_split_vector(ctx, dst, num_components);

         if (num_temp == num_components)
            ctx->allocated_vec.emplace(dst.id(), elems);
      }
   } else if (ctx->shader->info.stage == MESA_SHADER_FRAGMENT) {
      if (!nir_src_is_const(offset) || nir_src_as_uint(offset))
         isel_err(offset.ssa->parent_instr,
                  "Unimplemented non-zero nir_intrinsic_load_input offset");

      Temp prim_mask = get_arg(ctx, ctx->args->ac.prim_mask);

      unsigned idx = nir_intrinsic_base(instr);
      unsigned component = nir_intrinsic_component(instr);
      unsigned vertex_id = 2; /* P0 */

      if (instr->intrinsic == nir_intrinsic_load_input_vertex) {
         nir_const_value* src0 = nir_src_as_const_value(instr->src[0]);
         switch (src0->u32) {
         case 0:
            vertex_id = 2; /* P0 */
            break;
         case 1:
            vertex_id = 0; /* P10 */
            break;
         case 2:
            vertex_id = 1; /* P20 */
            break;
         default: unreachable("invalid vertex index");
         }
      }

      if (instr->dest.ssa.num_components == 1) {
         bld.vintrp(aco_opcode::v_interp_mov_f32, Definition(dst), Operand::c32(vertex_id),
                    bld.m0(prim_mask), idx, component);
      } else {
         aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(
            aco_opcode::p_create_vector, Format::PSEUDO, instr->dest.ssa.num_components, 1)};
         for (unsigned i = 0; i < instr->dest.ssa.num_components; i++) {
            vec->operands[i] = bld.vintrp(
               aco_opcode::v_interp_mov_f32, bld.def(instr->dest.ssa.bit_size == 16 ? v2b : v1),
               Operand::c32(vertex_id), bld.m0(prim_mask), idx, component + i);
         }
         vec->definitions[0] = Definition(dst);
         bld.insert(std::move(vec));
      }
   } else {
      unreachable("Shader stage not implemented");
   }
}

void
visit_load_tcs_per_vertex_input(isel_context* ctx, nir_intrinsic_instr* instr)
{
   assert(ctx->shader->info.stage == MESA_SHADER_TESS_CTRL);

   Builder bld(ctx->program, ctx->block);
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   if (load_input_from_temps(ctx, instr, dst))
      return;

   unreachable("LDS-based TCS input should have been lowered in NIR.");
}

void
visit_load_per_vertex_input(isel_context* ctx, nir_intrinsic_instr* instr)
{
   switch (ctx->shader->info.stage) {
   case MESA_SHADER_TESS_CTRL: visit_load_tcs_per_vertex_input(ctx, instr); break;
   default: unreachable("Unimplemented shader stage");
   }
}

void
visit_load_tess_coord(isel_context* ctx, nir_intrinsic_instr* instr)
{
   assert(ctx->shader->info.stage == MESA_SHADER_TESS_EVAL);

   Builder bld(ctx->program, ctx->block);
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   Operand tes_u(get_arg(ctx, ctx->args->ac.tes_u));
   Operand tes_v(get_arg(ctx, ctx->args->ac.tes_v));
   Operand tes_w = Operand::zero();

   if (ctx->shader->info.tess.primitive_mode == GL_TRIANGLES) {
      Temp tmp = bld.vop2(aco_opcode::v_add_f32, bld.def(v1), tes_u, tes_v);
      tmp = bld.vop2(aco_opcode::v_sub_f32, bld.def(v1), Operand::c32(0x3f800000u /* 1.0f */), tmp);
      tes_w = Operand(tmp);
   }

   Temp tess_coord = bld.pseudo(aco_opcode::p_create_vector, Definition(dst), tes_u, tes_v, tes_w);
   emit_split_vector(ctx, tess_coord, 3);
}

Temp
load_desc_ptr(isel_context* ctx, unsigned desc_set)
{
   if (ctx->program->info->need_indirect_descriptor_sets) {
      Builder bld(ctx->program, ctx->block);
      Temp ptr64 = convert_pointer_to_64_bit(ctx, get_arg(ctx, ctx->args->descriptor_sets[0]));
      Operand off = bld.copy(bld.def(s1), Operand::c32(desc_set << 2));
      return bld.smem(aco_opcode::s_load_dword, bld.def(s1), ptr64, off); //, false, false, false);
   }

   return get_arg(ctx, ctx->args->descriptor_sets[desc_set]);
}

void
visit_load_resource(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp index = get_ssa_temp(ctx, instr->src[0].ssa);
   if (!nir_dest_is_divergent(instr->dest))
      index = bld.as_uniform(index);
   unsigned desc_set = nir_intrinsic_desc_set(instr);
   unsigned binding = nir_intrinsic_binding(instr);

   Temp desc_ptr;
   radv_pipeline_layout* pipeline_layout = ctx->options->layout;
   radv_descriptor_set_layout* layout = pipeline_layout->set[desc_set].layout;
   unsigned offset = layout->binding[binding].offset;
   unsigned stride;
   if (layout->binding[binding].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
       layout->binding[binding].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
      unsigned idx = pipeline_layout->set[desc_set].dynamic_offset_start +
                     layout->binding[binding].dynamic_offset_offset;
      desc_ptr = get_arg(ctx, ctx->args->ac.push_constants);
      offset = pipeline_layout->push_constant_size + 16 * idx;
      stride = 16;
   } else {
      desc_ptr = load_desc_ptr(ctx, desc_set);
      stride = layout->binding[binding].size;
   }

   if (nir_src_is_const(instr->src[0])) {
      index =
         bld.copy(bld.def(s1), Operand::c32((offset + nir_src_as_uint(instr->src[0]) * stride)));
   } else if (index.type() == RegType::vgpr) {
      if (stride != 1) {
         bool index24bit = layout->binding[binding].array_size <= 0x1000000;
         index = bld.v_mul_imm(bld.def(v1), index, stride, index24bit);
      }
      if (offset)
         index = bld.vadd32(bld.def(v1), Operand::c32(offset), index);
   } else {
      if (stride != 1)
         index = bld.sop2(aco_opcode::s_mul_i32, bld.def(s1), Operand::c32(stride), index);
      if (offset)
         index = bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc),
                          Operand::c32(offset), index);
   }

   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   std::array<Temp, NIR_MAX_VEC_COMPONENTS> elems;
   elems[0] = desc_ptr;
   elems[1] = index;
   ctx->allocated_vec.emplace(dst.id(), elems);
   bld.pseudo(aco_opcode::p_create_vector, Definition(dst), desc_ptr, index, Operand::zero());
}

void
load_buffer(isel_context* ctx, unsigned num_components, unsigned component_size, Temp dst,
            Temp rsrc, Temp offset, unsigned align_mul, unsigned align_offset, bool glc = false,
            bool allow_smem = true, memory_sync_info sync = memory_sync_info())
{
   Builder bld(ctx->program, ctx->block);

   bool use_smem =
      dst.type() != RegType::vgpr && (!glc || ctx->options->chip_class >= GFX8) && allow_smem;
   if (use_smem)
      offset = bld.as_uniform(offset);
   else {
      /* GFX6-7 are affected by a hw bug that prevents address clamping to
       * work correctly when the SGPR offset is used.
       */
      if (offset.type() == RegType::sgpr && ctx->options->chip_class < GFX8)
         offset = as_vgpr(ctx, offset);
   }

   LoadEmitInfo info = {Operand(offset), dst, num_components, component_size, rsrc};
   info.glc = glc;
   info.sync = sync;
   info.align_mul = align_mul;
   info.align_offset = align_offset;
   if (use_smem)
      emit_load(ctx, bld, info, smem_load_params);
   else
      emit_load(ctx, bld, info, mubuf_load_params);
}

Temp
load_buffer_rsrc(isel_context* ctx, Temp rsrc)
{
   Builder bld(ctx->program, ctx->block);
   Temp set_ptr = emit_extract_vector(ctx, rsrc, 0, RegClass(rsrc.type(), 1));
   Temp binding = bld.as_uniform(emit_extract_vector(ctx, rsrc, 1, RegClass(rsrc.type(), 1)));
   set_ptr = convert_pointer_to_64_bit(ctx, set_ptr);
   return bld.smem(aco_opcode::s_load_dwordx4, bld.def(s4), set_ptr, binding);
}

bool
is_inline_ubo(isel_context* ctx, nir_src rsrc)
{
   nir_binding binding = nir_chase_binding(rsrc);
   if (!binding.success)
      return false;

   radv_descriptor_set_layout* layout = ctx->options->layout->set[binding.desc_set].layout;
   return layout->binding[binding.binding].type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT;
}

void
visit_load_ubo(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   Temp rsrc = get_ssa_temp(ctx, instr->src[0].ssa);

   Builder bld(ctx->program, ctx->block);

   if (is_inline_ubo(ctx, instr->src[0])) {
      Temp set_ptr = bld.as_uniform(emit_extract_vector(ctx, rsrc, 0, RegClass(rsrc.type(), 1)));
      Temp binding_off =
         bld.as_uniform(emit_extract_vector(ctx, rsrc, 1, RegClass(rsrc.type(), 1)));
      rsrc = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.def(s1, scc), set_ptr, binding_off);

      uint32_t desc_type =
         S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
         S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);
      if (ctx->options->chip_class >= GFX10) {
         desc_type |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
                      S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW) | S_008F0C_RESOURCE_LEVEL(1);
      } else {
         desc_type |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                      S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
      }
      rsrc = bld.pseudo(aco_opcode::p_create_vector, bld.def(s4), rsrc,
                        Operand::c32(S_008F04_BASE_ADDRESS_HI(ctx->options->address32_hi)),
                        Operand::c32(0xFFFFFFFFu), Operand::c32(desc_type));
   } else {
      rsrc = load_buffer_rsrc(ctx, rsrc);
   }
   unsigned size = instr->dest.ssa.bit_size / 8;
   load_buffer(ctx, instr->num_components, size, dst, rsrc, get_ssa_temp(ctx, instr->src[1].ssa),
               nir_intrinsic_align_mul(instr), nir_intrinsic_align_offset(instr));
}

void
visit_load_sbt_amd(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   Temp index = get_ssa_temp(ctx, instr->src[0].ssa);
   unsigned binding = nir_intrinsic_binding(instr);
   unsigned base = nir_intrinsic_base(instr);

   index = as_vgpr(ctx, index);

   Builder bld(ctx->program, ctx->block);
   Temp desc_base = convert_pointer_to_64_bit(ctx, get_arg(ctx, ctx->args->ac.sbt_descriptors));
   Operand desc_off = bld.copy(bld.def(s1), Operand::c32(binding * 16u));
   Temp rsrc = bld.smem(aco_opcode::s_load_dwordx4, bld.def(s4), desc_base, desc_off);

   /* If we want more we need to implement */
   assert(instr->dest.ssa.bit_size == 32);
   assert(instr->num_components == 1);

   bld.mubuf(aco_opcode::buffer_load_dword, Definition(dst), rsrc, index, Operand::zero(), base,
             false, false, true);
}

void
visit_load_push_constant(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   unsigned offset = nir_intrinsic_base(instr);
   unsigned count = instr->dest.ssa.num_components;
   nir_const_value* index_cv = nir_src_as_const_value(instr->src[0]);

   if (index_cv && instr->dest.ssa.bit_size == 32) {
      unsigned start = (offset + index_cv->u32) / 4u;
      start -= ctx->args->ac.base_inline_push_consts;
      if (start + count <= ctx->args->ac.num_inline_push_consts) {
         std::array<Temp, NIR_MAX_VEC_COMPONENTS> elems;
         aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(
            aco_opcode::p_create_vector, Format::PSEUDO, count, 1)};
         for (unsigned i = 0; i < count; ++i) {
            elems[i] = get_arg(ctx, ctx->args->ac.inline_push_consts[start + i]);
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
      index = bld.nuw().sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc),
                             Operand::c32(offset), index);
   Temp ptr = convert_pointer_to_64_bit(ctx, get_arg(ctx, ctx->args->ac.push_constants));
   Temp vec = dst;
   bool trim = false;
   bool aligned = true;

   if (instr->dest.ssa.bit_size == 8) {
      aligned = index_cv && (offset + index_cv->u32) % 4 == 0;
      bool fits_in_dword = count == 1 || (index_cv && ((offset + index_cv->u32) % 4 + count) <= 4);
      if (!aligned)
         vec = fits_in_dword ? bld.tmp(s1) : bld.tmp(s2);
   } else if (instr->dest.ssa.bit_size == 16) {
      aligned = index_cv && (offset + index_cv->u32) % 4 == 0;
      if (!aligned)
         vec = count == 4 ? bld.tmp(s4) : count > 1 ? bld.tmp(s2) : bld.tmp(s1);
   }

   aco_opcode op;

   switch (vec.size()) {
   case 1: op = aco_opcode::s_load_dword; break;
   case 2: op = aco_opcode::s_load_dwordx2; break;
   case 3:
      vec = bld.tmp(s4);
      trim = true;
      FALLTHROUGH;
   case 4: op = aco_opcode::s_load_dwordx4; break;
   case 6:
      vec = bld.tmp(s8);
      trim = true;
      FALLTHROUGH;
   case 8: op = aco_opcode::s_load_dwordx8; break;
   default: unreachable("unimplemented or forbidden load_push_constant.");
   }

   bld.smem(op, Definition(vec), ptr, index).instr->smem().prevent_overflow = true;

   if (!aligned) {
      Operand byte_offset = index_cv ? Operand::c32((offset + index_cv->u32) % 4) : Operand(index);
      byte_align_scalar(ctx, vec, byte_offset, dst);
      return;
   }

   if (trim) {
      emit_split_vector(ctx, vec, 4);
      RegClass rc = dst.size() == 3 ? s1 : s2;
      bld.pseudo(aco_opcode::p_create_vector, Definition(dst), emit_extract_vector(ctx, vec, 0, rc),
                 emit_extract_vector(ctx, vec, 1, rc), emit_extract_vector(ctx, vec, 2, rc));
   }
   emit_split_vector(ctx, dst, instr->dest.ssa.num_components);
}

void
visit_load_constant(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   Builder bld(ctx->program, ctx->block);

   uint32_t desc_type =
      S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) | S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
      S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) | S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);
   if (ctx->options->chip_class >= GFX10) {
      desc_type |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
                   S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW) | S_008F0C_RESOURCE_LEVEL(1);
   } else {
      desc_type |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                   S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
   }

   unsigned base = nir_intrinsic_base(instr);
   unsigned range = nir_intrinsic_range(instr);

   Temp offset = get_ssa_temp(ctx, instr->src[0].ssa);
   if (base && offset.type() == RegType::sgpr)
      offset = bld.nuw().sop2(aco_opcode::s_add_u32, bld.def(s1), bld.def(s1, scc), offset,
                              Operand::c32(base));
   else if (base && offset.type() == RegType::vgpr)
      offset = bld.vadd32(bld.def(v1), Operand::c32(base), offset);

   Temp rsrc = bld.pseudo(aco_opcode::p_create_vector, bld.def(s4),
                          bld.pseudo(aco_opcode::p_constaddr, bld.def(s2), bld.def(s1, scc),
                                     Operand::c32(ctx->constant_data_offset)),
                          Operand::c32(MIN2(base + range, ctx->shader->constant_data_size)),
                          Operand::c32(desc_type));
   unsigned size = instr->dest.ssa.bit_size / 8;
   // TODO: get alignment information for subdword constants
   load_buffer(ctx, instr->num_components, size, dst, rsrc, offset, size, 0);
}

void
visit_discard_if(isel_context* ctx, nir_intrinsic_instr* instr)
{
   if (ctx->block->loop_nest_depth || ctx->cf_info.parent_if.is_divergent)
      ctx->cf_info.exec_potentially_empty_discard = true;

   ctx->program->needs_exact = true;

   // TODO: optimize uniform conditions
   Builder bld(ctx->program, ctx->block);
   Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
   assert(src.regClass() == bld.lm);
   src = bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), src, Operand(exec, bld.lm));
   bld.pseudo(aco_opcode::p_discard_if, src);
   ctx->block->kind |= block_kind_uses_discard_if;
   return;
}

void
visit_discard(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);

   if (ctx->block->loop_nest_depth || ctx->cf_info.parent_if.is_divergent)
      ctx->cf_info.exec_potentially_empty_discard = true;

   bool divergent =
      ctx->cf_info.parent_if.is_divergent || ctx->cf_info.parent_loop.has_divergent_continue;

   if (ctx->block->loop_nest_depth && (nir_instr_is_last(&instr->instr) && !divergent)) {
      /* we handle discards the same way as jump instructions */
      append_logical_end(ctx->block);

      /* in loops, discard behaves like break */
      Block* linear_target = ctx->cf_info.parent_loop.exit;
      ctx->block->kind |= block_kind_discard;

      /* uniform discard - loop ends here */
      assert(nir_instr_is_last(&instr->instr));
      ctx->block->kind |= block_kind_uniform;
      ctx->cf_info.has_branch = true;
      bld.branch(aco_opcode::p_branch, bld.hint_vcc(bld.def(s2)));
      add_linear_edge(ctx->block->index, linear_target);
      return;
   }

   /* it can currently happen that NIR doesn't remove the unreachable code */
   if (!nir_instr_is_last(&instr->instr)) {
      ctx->program->needs_exact = true;
      /* save exec somewhere temporarily so that it doesn't get
       * overwritten before the discard from outer exec masks */
      Temp cond = bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc),
                           Operand::c32(0xFFFFFFFF), Operand(exec, bld.lm));
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
      ctx->block->kind |= block_kind_uses_discard_if;
      bld.pseudo(aco_opcode::p_discard_if, Operand::c32(0xFFFFFFFFu));
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
should_declare_array(isel_context* ctx, enum glsl_sampler_dim sampler_dim, bool is_array)
{
   if (sampler_dim == GLSL_SAMPLER_DIM_BUF)
      return false;
   ac_image_dim dim = ac_get_sampler_dim(ctx->options->chip_class, sampler_dim, is_array);
   return dim == ac_image_cube || dim == ac_image_1darray || dim == ac_image_2darray ||
          dim == ac_image_2darraymsaa;
}

Temp
get_sampler_desc(isel_context* ctx, nir_deref_instr* deref_instr,
                 enum aco_descriptor_type desc_type, const nir_tex_instr* tex_instr, bool write)
{
   /* FIXME: we should lower the deref with some new nir_intrinsic_load_desc
      std::unordered_map<uint64_t, Temp>::iterator it = ctx->tex_desc.find((uint64_t) desc_type <<
      32 | deref_instr->dest.ssa.index); if (it != ctx->tex_desc.end()) return it->second;
   */
   Temp index = Temp();
   bool index_set = false;
   unsigned constant_index = 0;
   unsigned descriptor_set;
   unsigned base_index;
   Builder bld(ctx->program, ctx->block);

   if (!deref_instr) {
      assert(tex_instr);
      descriptor_set = 0;
      base_index = tex_instr->sampler_index;
   } else {
      while (deref_instr->deref_type != nir_deref_type_var) {
         unsigned array_size = glsl_get_aoa_size(deref_instr->type);
         if (!array_size)
            array_size = 1;

         assert(deref_instr->deref_type == nir_deref_type_array);
         nir_const_value* const_value = nir_src_as_const_value(deref_instr->arr.index);
         if (const_value) {
            constant_index += array_size * const_value->u32;
         } else {
            Temp indirect = get_ssa_temp(ctx, deref_instr->arr.index.ssa);
            if (indirect.type() == RegType::vgpr)
               indirect = bld.as_uniform(indirect);

            if (array_size != 1)
               indirect =
                  bld.sop2(aco_opcode::s_mul_i32, bld.def(s1), Operand::c32(array_size), indirect);

            if (!index_set) {
               index = indirect;
               index_set = true;
            } else {
               index =
                  bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc), index, indirect);
            }
         }

         deref_instr = nir_src_as_deref(deref_instr->parent);
      }
      descriptor_set = deref_instr->var->data.descriptor_set;
      base_index = deref_instr->var->data.binding;
   }

   Temp list = load_desc_ptr(ctx, descriptor_set);
   list = convert_pointer_to_64_bit(ctx, list);

   struct radv_descriptor_set_layout* layout = ctx->options->layout->set[descriptor_set].layout;
   struct radv_descriptor_set_binding_layout* binding = layout->binding + base_index;
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
   default: unreachable("invalid desc_type\n");
   }

   offset += constant_index * stride;

   if (desc_type == ACO_DESC_SAMPLER && binding->immutable_samplers_offset &&
       (!index_set || binding->immutable_samplers_equal)) {
      if (binding->immutable_samplers_equal)
         constant_index = 0;

      const uint32_t* samplers = radv_immutable_samplers(layout, binding);
      uint32_t dword0_mask = tex_instr->op == nir_texop_tg4 ? C_008F30_TRUNC_COORD : 0xffffffffu;
      return bld.pseudo(aco_opcode::p_create_vector, bld.def(s4),
                        Operand::c32(samplers[constant_index * 4 + 0] & dword0_mask),
                        Operand::c32(samplers[constant_index * 4 + 1]),
                        Operand::c32(samplers[constant_index * 4 + 2]),
                        Operand::c32(samplers[constant_index * 4 + 3]));
   }

   Operand off;
   if (!index_set) {
      off = bld.copy(bld.def(s1), Operand::c32(offset));
   } else {
      off = Operand(
         (Temp)bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc), Operand::c32(offset),
                        bld.sop2(aco_opcode::s_mul_i32, bld.def(s1), Operand::c32(stride), index)));
   }

   Temp res = bld.smem(opcode, bld.def(type), list, off);

   if (desc_type == ACO_DESC_PLANE_2) {
      Temp components[8];
      for (unsigned i = 0; i < 8; i++)
         components[i] = bld.tmp(s1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(components[0]), Definition(components[1]),
                 Definition(components[2]), Definition(components[3]), res);

      Temp desc2 = get_sampler_desc(ctx, deref_instr, ACO_DESC_PLANE_1, tex_instr, write);
      bld.pseudo(aco_opcode::p_split_vector, bld.def(s1), bld.def(s1), bld.def(s1), bld.def(s1),
                 Definition(components[4]), Definition(components[5]), Definition(components[6]),
                 Definition(components[7]), desc2);

      res = bld.pseudo(aco_opcode::p_create_vector, bld.def(s8), components[0], components[1],
                       components[2], components[3], components[4], components[5], components[6],
                       components[7]);
   } else if (desc_type == ACO_DESC_IMAGE && ctx->options->has_image_load_dcc_bug && !tex_instr &&
              !write) {
      Temp components[8];
      for (unsigned i = 0; i < 8; i++)
         components[i] = bld.tmp(s1);

      bld.pseudo(aco_opcode::p_split_vector, Definition(components[0]), Definition(components[1]),
                 Definition(components[2]), Definition(components[3]), Definition(components[4]),
                 Definition(components[5]), Definition(components[6]), Definition(components[7]),
                 res);

      /* WRITE_COMPRESS_ENABLE must be 0 for all image loads to workaround a
       * hardware bug.
       */
      components[6] = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), components[6],
                               bld.copy(bld.def(s1), Operand::c32(C_00A018_WRITE_COMPRESS_ENABLE)));

      res = bld.pseudo(aco_opcode::p_create_vector, bld.def(s8), components[0], components[1],
                       components[2], components[3], components[4], components[5], components[6],
                       components[7]);
   } else if (desc_type == ACO_DESC_SAMPLER && tex_instr->op == nir_texop_tg4) {
      Temp components[4];
      for (unsigned i = 0; i < 4; i++)
         components[i] = bld.tmp(s1);

      bld.pseudo(aco_opcode::p_split_vector, Definition(components[0]), Definition(components[1]),
                 Definition(components[2]), Definition(components[3]), res);

      /* We want to always use the linear filtering truncation behaviour for
       * nir_texop_tg4, even if the sampler uses nearest/point filtering.
       */
      components[0] = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), components[0],
                               Operand::c32(C_008F30_TRUNC_COORD));

      res = bld.pseudo(aco_opcode::p_create_vector, bld.def(s4), components[0], components[1],
                       components[2], components[3]);
   }

   return res;
}

static int
image_type_to_components_count(enum glsl_sampler_dim dim, bool array)
{
   switch (dim) {
   case GLSL_SAMPLER_DIM_BUF: return 1;
   case GLSL_SAMPLER_DIM_1D: return array ? 2 : 1;
   case GLSL_SAMPLER_DIM_2D: return array ? 3 : 2;
   case GLSL_SAMPLER_DIM_MS: return array ? 4 : 3;
   case GLSL_SAMPLER_DIM_3D:
   case GLSL_SAMPLER_DIM_CUBE: return 3;
   case GLSL_SAMPLER_DIM_RECT:
   case GLSL_SAMPLER_DIM_SUBPASS: return 2;
   case GLSL_SAMPLER_DIM_SUBPASS_MS: return 3;
   default: break;
   }
   return 0;
}

static MIMG_instruction*
emit_mimg(Builder& bld, aco_opcode op, Definition dst, Temp rsrc, Operand samp,
          std::vector<Temp> coords, unsigned wqm_mask = 0, Operand vdata = Operand(v1))
{
   /* Limit NSA instructions to 3 dwords on GFX10 to avoid stability issues. */
   unsigned max_nsa_size = bld.program->chip_class >= GFX10_3 ? 13 : 5;
   bool use_nsa = bld.program->chip_class >= GFX10 && coords.size() <= max_nsa_size;

   if (!use_nsa) {
      Temp coord = coords[0];
      if (coords.size() > 1) {
         coord = bld.tmp(RegType::vgpr, coords.size());

         aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(
            aco_opcode::p_create_vector, Format::PSEUDO, coords.size(), 1)};
         for (unsigned i = 0; i < coords.size(); i++)
            vec->operands[i] = Operand(coords[i]);
         vec->definitions[0] = Definition(coord);
         bld.insert(std::move(vec));
      } else if (coord.type() == RegType::sgpr) {
         coord = bld.copy(bld.def(v1), coord);
      }

      if (wqm_mask) {
         /* We don't need the bias, sample index, compare value or offset to be
          * computed in WQM but if the p_create_vector copies the coordinates, then it
          * needs to be in WQM. */
         coord = emit_wqm(bld, coord, bld.tmp(coord.regClass()), true);
      }

      coords[0] = coord;
      coords.resize(1);
   } else {
      for (unsigned i = 0; i < coords.size(); i++) {
         if (wqm_mask & (1u << i))
            coords[i] = emit_wqm(bld, coords[i], bld.tmp(coords[i].regClass()), true);
      }

      for (Temp& coord : coords) {
         if (coord.type() == RegType::sgpr)
            coord = bld.copy(bld.def(v1), coord);
      }
   }

   aco_ptr<MIMG_instruction> mimg{
      create_instruction<MIMG_instruction>(op, Format::MIMG, 3 + coords.size(), dst.isTemp())};
   if (dst.isTemp())
      mimg->definitions[0] = dst;
   mimg->operands[0] = Operand(rsrc);
   mimg->operands[1] = samp;
   mimg->operands[2] = vdata;
   for (unsigned i = 0; i < coords.size(); i++)
      mimg->operands[3 + i] = Operand(coords[i]);

   MIMG_instruction* res = mimg.get();
   bld.insert(std::move(mimg));
   return res;
}

void
visit_bvh64_intersect_ray_amd(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   Temp resource = get_ssa_temp(ctx, instr->src[0].ssa);
   Temp node = get_ssa_temp(ctx, instr->src[1].ssa);
   Temp tmax = get_ssa_temp(ctx, instr->src[2].ssa);
   Temp origin = get_ssa_temp(ctx, instr->src[3].ssa);
   Temp dir = get_ssa_temp(ctx, instr->src[4].ssa);
   Temp inv_dir = get_ssa_temp(ctx, instr->src[5].ssa);

   std::vector<Temp> args;
   args.push_back(emit_extract_vector(ctx, node, 0, v1));
   args.push_back(emit_extract_vector(ctx, node, 1, v1));
   args.push_back(as_vgpr(ctx, tmax));
   args.push_back(emit_extract_vector(ctx, origin, 0, v1));
   args.push_back(emit_extract_vector(ctx, origin, 1, v1));
   args.push_back(emit_extract_vector(ctx, origin, 2, v1));
   args.push_back(emit_extract_vector(ctx, dir, 0, v1));
   args.push_back(emit_extract_vector(ctx, dir, 1, v1));
   args.push_back(emit_extract_vector(ctx, dir, 2, v1));
   args.push_back(emit_extract_vector(ctx, inv_dir, 0, v1));
   args.push_back(emit_extract_vector(ctx, inv_dir, 1, v1));
   args.push_back(emit_extract_vector(ctx, inv_dir, 2, v1));

   MIMG_instruction* mimg = emit_mimg(bld, aco_opcode::image_bvh64_intersect_ray, Definition(dst),
                                      resource, Operand(s4), args);
   mimg->dim = ac_image_1d;
   mimg->dmask = 0xf;
   mimg->unrm = true;
   mimg->r128 = true;
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
static Temp
adjust_sample_index_using_fmask(isel_context* ctx, bool da, std::vector<Temp>& coords,
                                Operand sample_index, Temp fmask_desc_ptr)
{
   Builder bld(ctx->program, ctx->block);
   Temp fmask = bld.tmp(v1);
   unsigned dim = ctx->options->chip_class >= GFX10
                     ? ac_get_sampler_dim(ctx->options->chip_class, GLSL_SAMPLER_DIM_2D, da)
                     : 0;

   MIMG_instruction* load = emit_mimg(bld, aco_opcode::image_load, Definition(fmask),
                                      fmask_desc_ptr, Operand(s4), coords);
   load->glc = false;
   load->dlc = false;
   load->dmask = 0x1;
   load->unrm = true;
   load->da = da;
   load->dim = dim;

   Operand sample_index4;
   if (sample_index.isConstant()) {
      if (sample_index.constantValue() < 16) {
         sample_index4 = Operand::c32(sample_index.constantValue() << 2);
      } else {
         sample_index4 = Operand::zero();
      }
   } else if (sample_index.regClass() == s1) {
      sample_index4 = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), sample_index,
                               Operand::c32(2u));
   } else {
      assert(sample_index.regClass() == v1);
      sample_index4 =
         bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(2u), sample_index);
   }

   Temp final_sample;
   if (sample_index4.isConstant() && sample_index4.constantValue() == 0)
      final_sample = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(15u), fmask);
   else if (sample_index4.isConstant() && sample_index4.constantValue() == 28)
      final_sample = bld.vop2(aco_opcode::v_lshrrev_b32, bld.def(v1), Operand::c32(28u), fmask);
   else
      final_sample =
         bld.vop3(aco_opcode::v_bfe_u32, bld.def(v1), fmask, sample_index4, Operand::c32(4u));

   /* Don't rewrite the sample index if WORD1.DATA_FORMAT of the FMASK
    * resource descriptor is 0 (invalid),
    */
   Temp compare = bld.tmp(bld.lm);
   bld.vopc_e64(aco_opcode::v_cmp_lg_u32, Definition(compare), Operand::zero(),
                emit_extract_vector(ctx, fmask_desc_ptr, 1, s1))
      .def(0)
      .setHint(vcc);

   Temp sample_index_v = bld.copy(bld.def(v1), sample_index);

   /* Replace the MSAA sample index. */
   return bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), sample_index_v, final_sample, compare);
}

static std::vector<Temp>
get_image_coords(isel_context* ctx, const nir_intrinsic_instr* instr)
{

   Temp src0 = get_ssa_temp(ctx, instr->src[1].ssa);
   enum glsl_sampler_dim dim = nir_intrinsic_image_dim(instr);
   bool is_array = nir_intrinsic_image_array(instr);
   ASSERTED bool add_frag_pos =
      (dim == GLSL_SAMPLER_DIM_SUBPASS || dim == GLSL_SAMPLER_DIM_SUBPASS_MS);
   assert(!add_frag_pos && "Input attachments should be lowered.");
   bool is_ms = (dim == GLSL_SAMPLER_DIM_MS || dim == GLSL_SAMPLER_DIM_SUBPASS_MS);
   bool gfx9_1d = ctx->options->chip_class == GFX9 && dim == GLSL_SAMPLER_DIM_1D;
   int count = image_type_to_components_count(dim, is_array);
   std::vector<Temp> coords(count);
   Builder bld(ctx->program, ctx->block);

   if (is_ms) {
      count--;
      Temp src2 = get_ssa_temp(ctx, instr->src[2].ssa);
      /* get sample index */
      if (instr->intrinsic == nir_intrinsic_image_deref_load ||
          instr->intrinsic == nir_intrinsic_image_deref_sparse_load) {
         nir_const_value* sample_cv = nir_src_as_const_value(instr->src[2]);
         Operand sample_index = sample_cv ? Operand::c32(sample_cv->u32)
                                          : Operand(emit_extract_vector(ctx, src2, 0, v1));
         std::vector<Temp> fmask_load_address;
         for (unsigned i = 0; i < (is_array ? 3 : 2); i++)
            fmask_load_address.emplace_back(emit_extract_vector(ctx, src0, i, v1));

         Temp fmask_desc_ptr =
            get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr),
                             ACO_DESC_FMASK, nullptr, false);
         coords[count] = adjust_sample_index_using_fmask(ctx, is_array, fmask_load_address,
                                                         sample_index, fmask_desc_ptr);
      } else {
         coords[count] = emit_extract_vector(ctx, src2, 0, v1);
      }
   }

   if (gfx9_1d) {
      coords[0] = emit_extract_vector(ctx, src0, 0, v1);
      coords.resize(coords.size() + 1);
      coords[1] = bld.copy(bld.def(v1), Operand::zero());
      if (is_array)
         coords[2] = emit_extract_vector(ctx, src0, 1, v1);
   } else {
      for (int i = 0; i < count; i++)
         coords[i] = emit_extract_vector(ctx, src0, i, v1);
   }

   if (instr->intrinsic == nir_intrinsic_image_deref_load ||
       instr->intrinsic == nir_intrinsic_image_deref_sparse_load ||
       instr->intrinsic == nir_intrinsic_image_deref_store) {
      int lod_index = instr->intrinsic == nir_intrinsic_image_deref_store ? 4 : 3;
      bool level_zero =
         nir_src_is_const(instr->src[lod_index]) && nir_src_as_uint(instr->src[lod_index]) == 0;

      if (!level_zero)
         coords.emplace_back(get_ssa_temp(ctx, instr->src[lod_index].ssa));
   }

   return coords;
}

memory_sync_info
get_memory_sync_info(nir_intrinsic_instr* instr, storage_class storage, unsigned semantics)
{
   /* atomicrmw might not have NIR_INTRINSIC_ACCESS and there's nothing interesting there anyway */
   if (semantics & semantic_atomicrmw)
      return memory_sync_info(storage, semantics);

   unsigned access = nir_intrinsic_access(instr);

   if (access & ACCESS_VOLATILE)
      semantics |= semantic_volatile;
   if (access & ACCESS_CAN_REORDER)
      semantics |= semantic_can_reorder | semantic_private;

   return memory_sync_info(storage, semantics);
}

Operand
emit_tfe_init(Builder& bld, Temp dst)
{
   Temp tmp = bld.tmp(dst.regClass());

   aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(
      aco_opcode::p_create_vector, Format::PSEUDO, dst.size(), 1)};
   for (unsigned i = 0; i < dst.size(); i++)
      vec->operands[i] = Operand::zero();
   vec->definitions[0] = Definition(tmp);
   /* Since this is fixed to an instruction's definition register, any CSE will
    * just create copies. Copying costs about the same as zero-initialization,
    * but these copies can break up clauses.
    */
   vec->definitions[0].setNoCSE(true);
   bld.insert(std::move(vec));

   return Operand(tmp);
}

void
visit_image_load(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   const nir_variable* var =
      nir_deref_instr_get_variable(nir_instr_as_deref(instr->src[0].ssa->parent_instr));
   const enum glsl_sampler_dim dim = nir_intrinsic_image_dim(instr);
   bool is_array = nir_intrinsic_image_array(instr);
   bool is_sparse = instr->intrinsic == nir_intrinsic_image_deref_sparse_load;
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   memory_sync_info sync = get_memory_sync_info(instr, storage_image, 0);
   unsigned access = var->data.access | nir_intrinsic_access(instr);

   unsigned result_size = instr->dest.ssa.num_components - is_sparse;
   unsigned expand_mask =
      nir_ssa_def_components_read(&instr->dest.ssa) & u_bit_consecutive(0, result_size);
   expand_mask = MAX2(expand_mask, 1); /* this can be zero in the case of sparse image loads */
   if (dim == GLSL_SAMPLER_DIM_BUF)
      expand_mask = (1u << util_last_bit(expand_mask)) - 1u;
   unsigned dmask = expand_mask;
   if (instr->dest.ssa.bit_size == 64) {
      expand_mask &= 0x9;
      /* only R64_UINT and R64_SINT supported. x is in xy of the result, w in zw */
      dmask = ((expand_mask & 0x1) ? 0x3 : 0) | ((expand_mask & 0x8) ? 0xc : 0);
   }
   if (is_sparse)
      expand_mask |= 1 << result_size;
   unsigned num_components = util_bitcount(dmask) + is_sparse;

   Temp tmp;
   if (num_components == dst.size() && dst.type() == RegType::vgpr)
      tmp = dst;
   else
      tmp = ctx->program->allocateTmp(RegClass(RegType::vgpr, num_components));

   Temp resource = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr),
                                    dim == GLSL_SAMPLER_DIM_BUF ? ACO_DESC_BUFFER : ACO_DESC_IMAGE,
                                    nullptr, false);

   if (dim == GLSL_SAMPLER_DIM_BUF) {
      Temp vindex = emit_extract_vector(ctx, get_ssa_temp(ctx, instr->src[1].ssa), 0, v1);

      aco_opcode opcode;
      switch (util_bitcount(dmask)) {
      case 1: opcode = aco_opcode::buffer_load_format_x; break;
      case 2: opcode = aco_opcode::buffer_load_format_xy; break;
      case 3: opcode = aco_opcode::buffer_load_format_xyz; break;
      case 4: opcode = aco_opcode::buffer_load_format_xyzw; break;
      default: unreachable(">4 channel buffer image load");
      }
      aco_ptr<MUBUF_instruction> load{
         create_instruction<MUBUF_instruction>(opcode, Format::MUBUF, 3 + is_sparse, 1)};
      load->operands[0] = Operand(resource);
      load->operands[1] = Operand(vindex);
      load->operands[2] = Operand::c32(0);
      load->definitions[0] = Definition(tmp);
      load->idxen = true;
      load->glc = access & (ACCESS_VOLATILE | ACCESS_COHERENT);
      load->dlc = load->glc && ctx->options->chip_class >= GFX10;
      load->sync = sync;
      load->tfe = is_sparse;
      if (load->tfe)
         load->operands[3] = emit_tfe_init(bld, tmp);
      ctx->block->instructions.emplace_back(std::move(load));
   } else {
      std::vector<Temp> coords = get_image_coords(ctx, instr);

      bool level_zero = nir_src_is_const(instr->src[3]) && nir_src_as_uint(instr->src[3]) == 0;
      aco_opcode opcode = level_zero ? aco_opcode::image_load : aco_opcode::image_load_mip;

      Operand vdata = is_sparse ? emit_tfe_init(bld, tmp) : Operand(v1);
      MIMG_instruction* load =
         emit_mimg(bld, opcode, Definition(tmp), resource, Operand(s4), coords, 0, vdata);
      load->glc = access & (ACCESS_VOLATILE | ACCESS_COHERENT) ? 1 : 0;
      load->dlc = load->glc && ctx->options->chip_class >= GFX10;
      load->dim = ac_get_image_dim(ctx->options->chip_class, dim, is_array);
      load->dmask = dmask;
      load->unrm = true;
      load->da = should_declare_array(ctx, dim, is_array);
      load->sync = sync;
      load->tfe = is_sparse;
   }

   if (is_sparse && instr->dest.ssa.bit_size == 64) {
      /* The result components are 64-bit but the sparse residency code is
       * 32-bit. So add a zero to the end so expand_vector() works correctly.
       */
      tmp = bld.pseudo(aco_opcode::p_create_vector, bld.def(RegType::vgpr, tmp.size() + 1), tmp,
                       Operand::zero());
   }

   expand_vector(ctx, tmp, dst, instr->dest.ssa.num_components, expand_mask);
}

void
visit_image_store(isel_context* ctx, nir_intrinsic_instr* instr)
{
   const nir_variable* var =
      nir_deref_instr_get_variable(nir_instr_as_deref(instr->src[0].ssa->parent_instr));
   const enum glsl_sampler_dim dim = nir_intrinsic_image_dim(instr);
   bool is_array = nir_intrinsic_image_array(instr);
   Temp data = get_ssa_temp(ctx, instr->src[3].ssa);

   /* only R64_UINT and R64_SINT supported */
   if (instr->src[3].ssa->bit_size == 64 && data.bytes() > 8)
      data = emit_extract_vector(ctx, data, 0, RegClass(data.type(), 2));
   data = as_vgpr(ctx, data);

   memory_sync_info sync = get_memory_sync_info(instr, storage_image, 0);
   unsigned access = var->data.access | nir_intrinsic_access(instr);
   bool glc = ctx->options->chip_class == GFX6 ||
                    access & (ACCESS_VOLATILE | ACCESS_COHERENT | ACCESS_NON_READABLE)
                 ? 1
                 : 0;

   if (dim == GLSL_SAMPLER_DIM_BUF) {
      Temp rsrc = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr),
                                   ACO_DESC_BUFFER, nullptr, true);
      Temp vindex = emit_extract_vector(ctx, get_ssa_temp(ctx, instr->src[1].ssa), 0, v1);
      aco_opcode opcode;
      switch (data.size()) {
      case 1: opcode = aco_opcode::buffer_store_format_x; break;
      case 2: opcode = aco_opcode::buffer_store_format_xy; break;
      case 3: opcode = aco_opcode::buffer_store_format_xyz; break;
      case 4: opcode = aco_opcode::buffer_store_format_xyzw; break;
      default: unreachable(">4 channel buffer image store");
      }
      aco_ptr<MUBUF_instruction> store{
         create_instruction<MUBUF_instruction>(opcode, Format::MUBUF, 4, 0)};
      store->operands[0] = Operand(rsrc);
      store->operands[1] = Operand(vindex);
      store->operands[2] = Operand::c32(0);
      store->operands[3] = Operand(data);
      store->idxen = true;
      store->glc = glc;
      store->dlc = false;
      store->disable_wqm = true;
      store->sync = sync;
      ctx->program->needs_exact = true;
      ctx->block->instructions.emplace_back(std::move(store));
      return;
   }

   assert(data.type() == RegType::vgpr);
   std::vector<Temp> coords = get_image_coords(ctx, instr);
   Temp resource = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr),
                                    ACO_DESC_IMAGE, nullptr, true);

   bool level_zero = nir_src_is_const(instr->src[4]) && nir_src_as_uint(instr->src[4]) == 0;
   aco_opcode opcode = level_zero ? aco_opcode::image_store : aco_opcode::image_store_mip;

   Builder bld(ctx->program, ctx->block);
   MIMG_instruction* store =
      emit_mimg(bld, opcode, Definition(), resource, Operand(s4), coords, 0, Operand(data));
   store->glc = glc;
   store->dlc = false;
   store->dim = ac_get_image_dim(ctx->options->chip_class, dim, is_array);
   store->dmask = (1 << data.size()) - 1;
   store->unrm = true;
   store->da = should_declare_array(ctx, dim, is_array);
   store->disable_wqm = true;
   store->sync = sync;
   ctx->program->needs_exact = true;
   return;
}

void
visit_image_atomic(isel_context* ctx, nir_intrinsic_instr* instr)
{
   bool return_previous = !nir_ssa_def_is_unused(&instr->dest.ssa);
   const enum glsl_sampler_dim dim = nir_intrinsic_image_dim(instr);
   bool is_array = nir_intrinsic_image_array(instr);
   Builder bld(ctx->program, ctx->block);

   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[3].ssa));
   bool is_64bit = data.bytes() == 8;
   assert((data.bytes() == 4 || data.bytes() == 8) && "only 32/64-bit image atomics implemented.");

   if (instr->intrinsic == nir_intrinsic_image_deref_atomic_comp_swap)
      data = bld.pseudo(aco_opcode::p_create_vector, bld.def(is_64bit ? v4 : v2),
                        get_ssa_temp(ctx, instr->src[4].ssa), data);

   aco_opcode buf_op, buf_op64, image_op;
   switch (instr->intrinsic) {
   case nir_intrinsic_image_deref_atomic_add:
      buf_op = aco_opcode::buffer_atomic_add;
      buf_op64 = aco_opcode::buffer_atomic_add_x2;
      image_op = aco_opcode::image_atomic_add;
      break;
   case nir_intrinsic_image_deref_atomic_umin:
      buf_op = aco_opcode::buffer_atomic_umin;
      buf_op64 = aco_opcode::buffer_atomic_umin_x2;
      image_op = aco_opcode::image_atomic_umin;
      break;
   case nir_intrinsic_image_deref_atomic_imin:
      buf_op = aco_opcode::buffer_atomic_smin;
      buf_op64 = aco_opcode::buffer_atomic_smin_x2;
      image_op = aco_opcode::image_atomic_smin;
      break;
   case nir_intrinsic_image_deref_atomic_umax:
      buf_op = aco_opcode::buffer_atomic_umax;
      buf_op64 = aco_opcode::buffer_atomic_umax_x2;
      image_op = aco_opcode::image_atomic_umax;
      break;
   case nir_intrinsic_image_deref_atomic_imax:
      buf_op = aco_opcode::buffer_atomic_smax;
      buf_op64 = aco_opcode::buffer_atomic_smax_x2;
      image_op = aco_opcode::image_atomic_smax;
      break;
   case nir_intrinsic_image_deref_atomic_and:
      buf_op = aco_opcode::buffer_atomic_and;
      buf_op64 = aco_opcode::buffer_atomic_and_x2;
      image_op = aco_opcode::image_atomic_and;
      break;
   case nir_intrinsic_image_deref_atomic_or:
      buf_op = aco_opcode::buffer_atomic_or;
      buf_op64 = aco_opcode::buffer_atomic_or_x2;
      image_op = aco_opcode::image_atomic_or;
      break;
   case nir_intrinsic_image_deref_atomic_xor:
      buf_op = aco_opcode::buffer_atomic_xor;
      buf_op64 = aco_opcode::buffer_atomic_xor_x2;
      image_op = aco_opcode::image_atomic_xor;
      break;
   case nir_intrinsic_image_deref_atomic_exchange:
      buf_op = aco_opcode::buffer_atomic_swap;
      buf_op64 = aco_opcode::buffer_atomic_swap_x2;
      image_op = aco_opcode::image_atomic_swap;
      break;
   case nir_intrinsic_image_deref_atomic_comp_swap:
      buf_op = aco_opcode::buffer_atomic_cmpswap;
      buf_op64 = aco_opcode::buffer_atomic_cmpswap_x2;
      image_op = aco_opcode::image_atomic_cmpswap;
      break;
   case nir_intrinsic_image_deref_atomic_fmin:
      buf_op = aco_opcode::buffer_atomic_fmin;
      buf_op64 = aco_opcode::buffer_atomic_fmin_x2;
      image_op = aco_opcode::image_atomic_fmin;
      break;
   case nir_intrinsic_image_deref_atomic_fmax:
      buf_op = aco_opcode::buffer_atomic_fmax;
      buf_op64 = aco_opcode::buffer_atomic_fmax_x2;
      image_op = aco_opcode::image_atomic_fmax;
      break;
   default:
      unreachable("visit_image_atomic should only be called with "
                  "nir_intrinsic_image_deref_atomic_* instructions.");
   }

   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   memory_sync_info sync = get_memory_sync_info(instr, storage_image, semantic_atomicrmw);

   if (dim == GLSL_SAMPLER_DIM_BUF) {
      Temp vindex = emit_extract_vector(ctx, get_ssa_temp(ctx, instr->src[1].ssa), 0, v1);
      Temp resource = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr),
                                       ACO_DESC_BUFFER, nullptr, true);
      // assert(ctx->options->chip_class < GFX9 && "GFX9 stride size workaround not yet
      // implemented.");
      aco_ptr<MUBUF_instruction> mubuf{create_instruction<MUBUF_instruction>(
         is_64bit ? buf_op64 : buf_op, Format::MUBUF, 4, return_previous ? 1 : 0)};
      mubuf->operands[0] = Operand(resource);
      mubuf->operands[1] = Operand(vindex);
      mubuf->operands[2] = Operand::c32(0);
      mubuf->operands[3] = Operand(data);
      if (return_previous)
         mubuf->definitions[0] = Definition(dst);
      mubuf->offset = 0;
      mubuf->idxen = true;
      mubuf->glc = return_previous;
      mubuf->dlc = false; /* Not needed for atomics */
      mubuf->disable_wqm = true;
      mubuf->sync = sync;
      ctx->program->needs_exact = true;
      ctx->block->instructions.emplace_back(std::move(mubuf));
      return;
   }

   std::vector<Temp> coords = get_image_coords(ctx, instr);
   Temp resource = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr),
                                    ACO_DESC_IMAGE, nullptr, true);
   Definition def = return_previous ? Definition(dst) : Definition();
   MIMG_instruction* mimg =
      emit_mimg(bld, image_op, def, resource, Operand(s4), coords, 0, Operand(data));
   mimg->glc = return_previous;
   mimg->dlc = false; /* Not needed for atomics */
   mimg->dim = ac_get_image_dim(ctx->options->chip_class, dim, is_array);
   mimg->dmask = (1 << data.size()) - 1;
   mimg->unrm = true;
   mimg->da = should_declare_array(ctx, dim, is_array);
   mimg->disable_wqm = true;
   mimg->sync = sync;
   ctx->program->needs_exact = true;
   return;
}

void
get_buffer_size(isel_context* ctx, Temp desc, Temp dst)
{
   if (ctx->options->chip_class == GFX8) {
      /* we only have to divide by 1, 2, 4, 8, 12 or 16 */
      Builder bld(ctx->program, ctx->block);

      Temp size = emit_extract_vector(ctx, desc, 2, s1);

      Temp size_div3 = bld.vop3(aco_opcode::v_mul_hi_u32, bld.def(v1),
                                bld.copy(bld.def(v1), Operand::c32(0xaaaaaaabu)), size);
      size_div3 = bld.sop2(aco_opcode::s_lshr_b32, bld.def(s1), bld.def(s1, scc),
                           bld.as_uniform(size_div3), Operand::c32(1u));

      Temp stride = emit_extract_vector(ctx, desc, 1, s1);
      stride = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc), stride,
                        Operand::c32((5u << 16) | 16u));

      Temp is12 = bld.sopc(aco_opcode::s_cmp_eq_i32, bld.def(s1, scc), stride, Operand::c32(12u));
      size = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1), size_div3, size, bld.scc(is12));

      Temp shr_dst = dst.type() == RegType::vgpr ? bld.tmp(s1) : dst;
      bld.sop2(aco_opcode::s_lshr_b32, Definition(shr_dst), bld.def(s1, scc), size,
               bld.sop1(aco_opcode::s_ff1_i32_b32, bld.def(s1), stride));
      if (dst.type() == RegType::vgpr)
         bld.copy(Definition(dst), shr_dst);

      /* TODO: we can probably calculate this faster with v_skip when stride != 12 */
   } else {
      emit_extract_vector(ctx, desc, 2, dst);
   }
}

void
visit_image_size(isel_context* ctx, nir_intrinsic_instr* instr)
{
   const enum glsl_sampler_dim dim = nir_intrinsic_image_dim(instr);
   bool is_array = nir_intrinsic_image_array(instr);
   Builder bld(ctx->program, ctx->block);

   if (dim == GLSL_SAMPLER_DIM_BUF) {
      Temp desc = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr),
                                   ACO_DESC_BUFFER, NULL, false);
      return get_buffer_size(ctx, desc, get_ssa_temp(ctx, &instr->dest.ssa));
   }

   /* LOD */
   assert(nir_src_as_uint(instr->src[1]) == 0);
   std::vector<Temp> lod{bld.copy(bld.def(v1), Operand::zero())};

   /* Resource */
   Temp resource = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr),
                                    ACO_DESC_IMAGE, NULL, false);

   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   MIMG_instruction* mimg =
      emit_mimg(bld, aco_opcode::image_get_resinfo, Definition(dst), resource, Operand(s4), lod);
   uint8_t& dmask = mimg->dmask;
   mimg->dim = ac_get_image_dim(ctx->options->chip_class, dim, is_array);
   mimg->dmask = (1 << instr->dest.ssa.num_components) - 1;
   mimg->da = is_array;

   if (ctx->options->chip_class == GFX9 && dim == GLSL_SAMPLER_DIM_1D && is_array) {
      assert(instr->dest.ssa.num_components == 2);
      dmask = 0x5;
   }

   emit_split_vector(ctx, dst, instr->dest.ssa.num_components);
}

void
get_image_samples(isel_context* ctx, Definition dst, Temp resource)
{
   Builder bld(ctx->program, ctx->block);

   Temp dword3 = emit_extract_vector(ctx, resource, 3, s1);
   Temp samples_log2 = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc), dword3,
                                Operand::c32(16u | 4u << 16));
   Temp samples = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), Operand::c32(1u),
                           samples_log2);
   Temp type = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc), dword3,
                        Operand::c32(28u | 4u << 16 /* offset=28, width=4 */));

   Operand default_sample = Operand::c32(1u);
   if (ctx->options->robust_buffer_access) {
      /* Extract the second dword of the descriptor, if it's
       * all zero, then it's a null descriptor.
       */
      Temp dword1 = emit_extract_vector(ctx, resource, 1, s1);
      Temp is_non_null_descriptor =
         bld.sopc(aco_opcode::s_cmp_gt_u32, bld.def(s1, scc), dword1, Operand::zero());
      default_sample = Operand(is_non_null_descriptor);
   }

   Temp is_msaa = bld.sopc(aco_opcode::s_cmp_ge_u32, bld.def(s1, scc), type, Operand::c32(14u));
   bld.sop2(aco_opcode::s_cselect_b32, dst, samples, default_sample, bld.scc(is_msaa));
}

void
visit_image_samples(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   Temp resource = get_sampler_desc(ctx, nir_instr_as_deref(instr->src[0].ssa->parent_instr),
                                    ACO_DESC_IMAGE, NULL, false);
   get_image_samples(ctx, Definition(dst), resource);
}

void
visit_load_ssbo(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   unsigned num_components = instr->num_components;

   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   Temp rsrc = load_buffer_rsrc(ctx, get_ssa_temp(ctx, instr->src[0].ssa));

   unsigned access = nir_intrinsic_access(instr);
   bool glc = access & (ACCESS_VOLATILE | ACCESS_COHERENT);
   unsigned size = instr->dest.ssa.bit_size / 8;

   bool allow_smem = access & ACCESS_CAN_REORDER;

   load_buffer(ctx, num_components, size, dst, rsrc, get_ssa_temp(ctx, instr->src[1].ssa),
               nir_intrinsic_align_mul(instr), nir_intrinsic_align_offset(instr), glc, allow_smem,
               get_memory_sync_info(instr, storage_buffer, 0));
}

void
visit_store_ssbo(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp data = get_ssa_temp(ctx, instr->src[0].ssa);
   unsigned elem_size_bytes = instr->src[0].ssa->bit_size / 8;
   unsigned writemask = widen_mask(nir_intrinsic_write_mask(instr), elem_size_bytes);
   Temp offset = get_ssa_temp(ctx, instr->src[2].ssa);

   Temp rsrc = load_buffer_rsrc(ctx, get_ssa_temp(ctx, instr->src[1].ssa));

   memory_sync_info sync = get_memory_sync_info(instr, storage_buffer, 0);
   bool glc =
      nir_intrinsic_access(instr) & (ACCESS_VOLATILE | ACCESS_COHERENT | ACCESS_NON_READABLE);

   unsigned write_count = 0;
   Temp write_datas[32];
   unsigned offsets[32];
   split_buffer_store(ctx, instr, false, RegType::vgpr, data, writemask, 16, &write_count,
                      write_datas, offsets);

   /* GFX6-7 are affected by a hw bug that prevents address clamping to work
    * correctly when the SGPR offset is used.
    */
   if (offset.type() == RegType::sgpr && ctx->options->chip_class < GFX8)
      offset = as_vgpr(ctx, offset);

   for (unsigned i = 0; i < write_count; i++) {
      aco_opcode op = get_buffer_store_op(write_datas[i].bytes());

      aco_ptr<MUBUF_instruction> store{
         create_instruction<MUBUF_instruction>(op, Format::MUBUF, 4, 0)};
      store->operands[0] = Operand(rsrc);
      store->operands[1] = offset.type() == RegType::vgpr ? Operand(offset) : Operand(v1);
      store->operands[2] = offset.type() == RegType::sgpr ? Operand(offset) : Operand::c32(0);
      store->operands[3] = Operand(write_datas[i]);
      store->offset = offsets[i];
      store->offen = (offset.type() == RegType::vgpr);
      store->glc = glc;
      store->dlc = false;
      store->disable_wqm = true;
      store->sync = sync;
      ctx->program->needs_exact = true;
      ctx->block->instructions.emplace_back(std::move(store));
   }
}

void
visit_atomic_ssbo(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   bool return_previous = !nir_ssa_def_is_unused(&instr->dest.ssa);
   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[2].ssa));

   if (instr->intrinsic == nir_intrinsic_ssbo_atomic_comp_swap)
      data = bld.pseudo(aco_opcode::p_create_vector, bld.def(RegType::vgpr, data.size() * 2),
                        get_ssa_temp(ctx, instr->src[3].ssa), data);

   Temp offset = get_ssa_temp(ctx, instr->src[1].ssa);
   Temp rsrc = load_buffer_rsrc(ctx, get_ssa_temp(ctx, instr->src[0].ssa));

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
   case nir_intrinsic_ssbo_atomic_fmin:
      op32 = aco_opcode::buffer_atomic_fmin;
      op64 = aco_opcode::buffer_atomic_fmin_x2;
      break;
   case nir_intrinsic_ssbo_atomic_fmax:
      op32 = aco_opcode::buffer_atomic_fmax;
      op64 = aco_opcode::buffer_atomic_fmax_x2;
      break;
   default:
      unreachable(
         "visit_atomic_ssbo should only be called with nir_intrinsic_ssbo_atomic_* instructions.");
   }
   aco_opcode op = instr->dest.ssa.bit_size == 32 ? op32 : op64;
   aco_ptr<MUBUF_instruction> mubuf{
      create_instruction<MUBUF_instruction>(op, Format::MUBUF, 4, return_previous ? 1 : 0)};
   mubuf->operands[0] = Operand(rsrc);
   mubuf->operands[1] = offset.type() == RegType::vgpr ? Operand(offset) : Operand(v1);
   mubuf->operands[2] = offset.type() == RegType::sgpr ? Operand(offset) : Operand::c32(0);
   mubuf->operands[3] = Operand(data);
   if (return_previous)
      mubuf->definitions[0] = Definition(dst);
   mubuf->offset = 0;
   mubuf->offen = (offset.type() == RegType::vgpr);
   mubuf->glc = return_previous;
   mubuf->dlc = false; /* Not needed for atomics */
   mubuf->disable_wqm = true;
   mubuf->sync = get_memory_sync_info(instr, storage_buffer, semantic_atomicrmw);
   ctx->program->needs_exact = true;
   ctx->block->instructions.emplace_back(std::move(mubuf));
}

void
visit_get_ssbo_size(isel_context* ctx, nir_intrinsic_instr* instr)
{

   Temp rsrc = get_ssa_temp(ctx, instr->src[0].ssa);
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   bool non_uniform = dst.type() == RegType::vgpr;

   Builder bld(ctx->program, ctx->block);
   if (non_uniform) {
      Temp set_ptr = emit_extract_vector(ctx, rsrc, 0, RegClass(rsrc.type(), 1));
      Temp binding = emit_extract_vector(ctx, rsrc, 1, RegClass(rsrc.type(), 1));
      Temp index = bld.vadd32(bld.def(v1), set_ptr, binding);
      index = convert_pointer_to_64_bit(ctx, index, non_uniform);

      LoadEmitInfo info = {Operand(index), dst, 1, 4};
      info.align_mul = 4;
      info.const_offset = 8;
      emit_load(ctx, bld, info, global_load_params);
   } else {
      emit_extract_vector(ctx, load_buffer_rsrc(ctx, rsrc), 2, dst);
   }
}

void
visit_load_global(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   unsigned num_components = instr->num_components;
   unsigned component_size = instr->dest.ssa.bit_size / 8;

   LoadEmitInfo info = {Operand(get_ssa_temp(ctx, instr->src[0].ssa)),
                        get_ssa_temp(ctx, &instr->dest.ssa), num_components, component_size};
   info.glc = nir_intrinsic_access(instr) & (ACCESS_VOLATILE | ACCESS_COHERENT);
   info.align_mul = nir_intrinsic_align_mul(instr);
   info.align_offset = nir_intrinsic_align_offset(instr);
   info.sync = get_memory_sync_info(instr, storage_buffer, 0);
   /* VMEM stores don't update the SMEM cache and it's difficult to prove that
    * it's safe to use SMEM */
   bool can_use_smem = nir_intrinsic_access(instr) & ACCESS_NON_WRITEABLE;
   if (info.dst.type() == RegType::vgpr || (info.glc && ctx->options->chip_class < GFX8) ||
       !can_use_smem) {
      emit_load(ctx, bld, info, global_load_params);
   } else {
      info.offset = Operand(bld.as_uniform(info.offset));
      emit_load(ctx, bld, info, smem_load_params);
   }
}

void
visit_store_global(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   unsigned elem_size_bytes = instr->src[0].ssa->bit_size / 8;
   unsigned writemask = widen_mask(nir_intrinsic_write_mask(instr), elem_size_bytes);

   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   Temp addr = get_ssa_temp(ctx, instr->src[1].ssa);
   memory_sync_info sync = get_memory_sync_info(instr, storage_buffer, 0);
   bool glc =
      nir_intrinsic_access(instr) & (ACCESS_VOLATILE | ACCESS_COHERENT | ACCESS_NON_READABLE);

   if (ctx->options->chip_class >= GFX7)
      addr = as_vgpr(ctx, addr);

   unsigned write_count = 0;
   Temp write_datas[32];
   unsigned offsets[32];
   split_buffer_store(ctx, instr, false, RegType::vgpr, data, writemask, 16, &write_count,
                      write_datas, offsets);

   for (unsigned i = 0; i < write_count; i++) {
      if (ctx->options->chip_class >= GFX7) {
         unsigned offset = offsets[i];
         Temp store_addr = addr;
         if (offset > 0 && ctx->options->chip_class < GFX9) {
            Temp addr0 = bld.tmp(v1), addr1 = bld.tmp(v1);
            Temp new_addr0 = bld.tmp(v1), new_addr1 = bld.tmp(v1);
            Temp carry = bld.tmp(bld.lm);
            bld.pseudo(aco_opcode::p_split_vector, Definition(addr0), Definition(addr1), addr);

            bld.vop2(aco_opcode::v_add_co_u32, Definition(new_addr0),
                     bld.hint_vcc(Definition(carry)), Operand::c32(offset), addr0);
            bld.vop2(aco_opcode::v_addc_co_u32, Definition(new_addr1), bld.def(bld.lm),
                     Operand::zero(), addr1, carry)
               .def(1)
               .setHint(vcc);

            store_addr = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), new_addr0, new_addr1);

            offset = 0;
         }

         bool global = ctx->options->chip_class >= GFX9;
         aco_opcode op;
         switch (write_datas[i].bytes()) {
         case 1: op = global ? aco_opcode::global_store_byte : aco_opcode::flat_store_byte; break;
         case 2: op = global ? aco_opcode::global_store_short : aco_opcode::flat_store_short; break;
         case 4: op = global ? aco_opcode::global_store_dword : aco_opcode::flat_store_dword; break;
         case 8:
            op = global ? aco_opcode::global_store_dwordx2 : aco_opcode::flat_store_dwordx2;
            break;
         case 12:
            op = global ? aco_opcode::global_store_dwordx3 : aco_opcode::flat_store_dwordx3;
            break;
         case 16:
            op = global ? aco_opcode::global_store_dwordx4 : aco_opcode::flat_store_dwordx4;
            break;
         default: unreachable("store_global not implemented for this size.");
         }

         aco_ptr<FLAT_instruction> flat{
            create_instruction<FLAT_instruction>(op, global ? Format::GLOBAL : Format::FLAT, 3, 0)};
         flat->operands[0] = Operand(store_addr);
         flat->operands[1] = Operand(s1);
         flat->operands[2] = Operand(write_datas[i]);
         flat->glc = glc;
         flat->dlc = false;
         flat->offset = offset;
         flat->disable_wqm = true;
         flat->sync = sync;
         ctx->program->needs_exact = true;
         ctx->block->instructions.emplace_back(std::move(flat));
      } else {
         assert(ctx->options->chip_class == GFX6);

         aco_opcode op = get_buffer_store_op(write_datas[i].bytes());

         Temp rsrc = get_gfx6_global_rsrc(bld, addr);

         aco_ptr<MUBUF_instruction> mubuf{
            create_instruction<MUBUF_instruction>(op, Format::MUBUF, 4, 0)};
         mubuf->operands[0] = Operand(rsrc);
         mubuf->operands[1] = addr.type() == RegType::vgpr ? Operand(addr) : Operand(v1);
         mubuf->operands[2] = Operand::zero();
         mubuf->operands[3] = Operand(write_datas[i]);
         mubuf->glc = glc;
         mubuf->dlc = false;
         mubuf->offset = offsets[i];
         mubuf->addr64 = addr.type() == RegType::vgpr;
         mubuf->disable_wqm = true;
         mubuf->sync = sync;
         ctx->program->needs_exact = true;
         ctx->block->instructions.emplace_back(std::move(mubuf));
      }
   }
}

void
visit_global_atomic(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   bool return_previous = !nir_ssa_def_is_unused(&instr->dest.ssa);
   Temp addr = get_ssa_temp(ctx, instr->src[0].ssa);
   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));

   if (ctx->options->chip_class >= GFX7)
      addr = as_vgpr(ctx, addr);

   if (instr->intrinsic == nir_intrinsic_global_atomic_comp_swap)
      data = bld.pseudo(aco_opcode::p_create_vector, bld.def(RegType::vgpr, data.size() * 2),
                        get_ssa_temp(ctx, instr->src[2].ssa), data);

   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   aco_opcode op32, op64;

   if (ctx->options->chip_class >= GFX7) {
      bool global = ctx->options->chip_class >= GFX9;
      switch (instr->intrinsic) {
      case nir_intrinsic_global_atomic_add:
         op32 = global ? aco_opcode::global_atomic_add : aco_opcode::flat_atomic_add;
         op64 = global ? aco_opcode::global_atomic_add_x2 : aco_opcode::flat_atomic_add_x2;
         break;
      case nir_intrinsic_global_atomic_imin:
         op32 = global ? aco_opcode::global_atomic_smin : aco_opcode::flat_atomic_smin;
         op64 = global ? aco_opcode::global_atomic_smin_x2 : aco_opcode::flat_atomic_smin_x2;
         break;
      case nir_intrinsic_global_atomic_umin:
         op32 = global ? aco_opcode::global_atomic_umin : aco_opcode::flat_atomic_umin;
         op64 = global ? aco_opcode::global_atomic_umin_x2 : aco_opcode::flat_atomic_umin_x2;
         break;
      case nir_intrinsic_global_atomic_imax:
         op32 = global ? aco_opcode::global_atomic_smax : aco_opcode::flat_atomic_smax;
         op64 = global ? aco_opcode::global_atomic_smax_x2 : aco_opcode::flat_atomic_smax_x2;
         break;
      case nir_intrinsic_global_atomic_umax:
         op32 = global ? aco_opcode::global_atomic_umax : aco_opcode::flat_atomic_umax;
         op64 = global ? aco_opcode::global_atomic_umax_x2 : aco_opcode::flat_atomic_umax_x2;
         break;
      case nir_intrinsic_global_atomic_and:
         op32 = global ? aco_opcode::global_atomic_and : aco_opcode::flat_atomic_and;
         op64 = global ? aco_opcode::global_atomic_and_x2 : aco_opcode::flat_atomic_and_x2;
         break;
      case nir_intrinsic_global_atomic_or:
         op32 = global ? aco_opcode::global_atomic_or : aco_opcode::flat_atomic_or;
         op64 = global ? aco_opcode::global_atomic_or_x2 : aco_opcode::flat_atomic_or_x2;
         break;
      case nir_intrinsic_global_atomic_xor:
         op32 = global ? aco_opcode::global_atomic_xor : aco_opcode::flat_atomic_xor;
         op64 = global ? aco_opcode::global_atomic_xor_x2 : aco_opcode::flat_atomic_xor_x2;
         break;
      case nir_intrinsic_global_atomic_exchange:
         op32 = global ? aco_opcode::global_atomic_swap : aco_opcode::flat_atomic_swap;
         op64 = global ? aco_opcode::global_atomic_swap_x2 : aco_opcode::flat_atomic_swap_x2;
         break;
      case nir_intrinsic_global_atomic_comp_swap:
         op32 = global ? aco_opcode::global_atomic_cmpswap : aco_opcode::flat_atomic_cmpswap;
         op64 = global ? aco_opcode::global_atomic_cmpswap_x2 : aco_opcode::flat_atomic_cmpswap_x2;
         break;
      case nir_intrinsic_global_atomic_fmin:
         op32 = global ? aco_opcode::global_atomic_fmin : aco_opcode::flat_atomic_fmin;
         op64 = global ? aco_opcode::global_atomic_fmin_x2 : aco_opcode::flat_atomic_fmin_x2;
         break;
      case nir_intrinsic_global_atomic_fmax:
         op32 = global ? aco_opcode::global_atomic_fmax : aco_opcode::flat_atomic_fmax;
         op64 = global ? aco_opcode::global_atomic_fmax_x2 : aco_opcode::flat_atomic_fmax_x2;
         break;
      default:
         unreachable("visit_atomic_global should only be called with nir_intrinsic_global_atomic_* "
                     "instructions.");
      }

      aco_opcode op = instr->dest.ssa.bit_size == 32 ? op32 : op64;
      aco_ptr<FLAT_instruction> flat{create_instruction<FLAT_instruction>(
         op, global ? Format::GLOBAL : Format::FLAT, 3, return_previous ? 1 : 0)};
      flat->operands[0] = Operand(addr);
      flat->operands[1] = Operand(s1);
      flat->operands[2] = Operand(data);
      if (return_previous)
         flat->definitions[0] = Definition(dst);
      flat->glc = return_previous;
      flat->dlc = false; /* Not needed for atomics */
      flat->offset = 0;
      flat->disable_wqm = true;
      flat->sync = get_memory_sync_info(instr, storage_buffer, semantic_atomicrmw);
      ctx->program->needs_exact = true;
      ctx->block->instructions.emplace_back(std::move(flat));
   } else {
      assert(ctx->options->chip_class == GFX6);

      switch (instr->intrinsic) {
      case nir_intrinsic_global_atomic_add:
         op32 = aco_opcode::buffer_atomic_add;
         op64 = aco_opcode::buffer_atomic_add_x2;
         break;
      case nir_intrinsic_global_atomic_imin:
         op32 = aco_opcode::buffer_atomic_smin;
         op64 = aco_opcode::buffer_atomic_smin_x2;
         break;
      case nir_intrinsic_global_atomic_umin:
         op32 = aco_opcode::buffer_atomic_umin;
         op64 = aco_opcode::buffer_atomic_umin_x2;
         break;
      case nir_intrinsic_global_atomic_imax:
         op32 = aco_opcode::buffer_atomic_smax;
         op64 = aco_opcode::buffer_atomic_smax_x2;
         break;
      case nir_intrinsic_global_atomic_umax:
         op32 = aco_opcode::buffer_atomic_umax;
         op64 = aco_opcode::buffer_atomic_umax_x2;
         break;
      case nir_intrinsic_global_atomic_and:
         op32 = aco_opcode::buffer_atomic_and;
         op64 = aco_opcode::buffer_atomic_and_x2;
         break;
      case nir_intrinsic_global_atomic_or:
         op32 = aco_opcode::buffer_atomic_or;
         op64 = aco_opcode::buffer_atomic_or_x2;
         break;
      case nir_intrinsic_global_atomic_xor:
         op32 = aco_opcode::buffer_atomic_xor;
         op64 = aco_opcode::buffer_atomic_xor_x2;
         break;
      case nir_intrinsic_global_atomic_exchange:
         op32 = aco_opcode::buffer_atomic_swap;
         op64 = aco_opcode::buffer_atomic_swap_x2;
         break;
      case nir_intrinsic_global_atomic_comp_swap:
         op32 = aco_opcode::buffer_atomic_cmpswap;
         op64 = aco_opcode::buffer_atomic_cmpswap_x2;
         break;
      case nir_intrinsic_global_atomic_fmin:
         op32 = aco_opcode::buffer_atomic_fmin;
         op64 = aco_opcode::buffer_atomic_fmin_x2;
         break;
      case nir_intrinsic_global_atomic_fmax:
         op32 = aco_opcode::buffer_atomic_fmax;
         op64 = aco_opcode::buffer_atomic_fmax_x2;
         break;
      default:
         unreachable("visit_atomic_global should only be called with nir_intrinsic_global_atomic_* "
                     "instructions.");
      }

      Temp rsrc = get_gfx6_global_rsrc(bld, addr);

      aco_opcode op = instr->dest.ssa.bit_size == 32 ? op32 : op64;

      aco_ptr<MUBUF_instruction> mubuf{
         create_instruction<MUBUF_instruction>(op, Format::MUBUF, 4, return_previous ? 1 : 0)};
      mubuf->operands[0] = Operand(rsrc);
      mubuf->operands[1] = addr.type() == RegType::vgpr ? Operand(addr) : Operand(v1);
      mubuf->operands[2] = Operand::zero();
      mubuf->operands[3] = Operand(data);
      if (return_previous)
         mubuf->definitions[0] = Definition(dst);
      mubuf->glc = return_previous;
      mubuf->dlc = false;
      mubuf->offset = 0;
      mubuf->addr64 = addr.type() == RegType::vgpr;
      mubuf->disable_wqm = true;
      mubuf->sync = get_memory_sync_info(instr, storage_buffer, semantic_atomicrmw);
      ctx->program->needs_exact = true;
      ctx->block->instructions.emplace_back(std::move(mubuf));
   }
}

void
visit_load_buffer(isel_context* ctx, nir_intrinsic_instr* intrin)
{
   Builder bld(ctx->program, ctx->block);

   Temp dst = get_ssa_temp(ctx, &intrin->dest.ssa);
   Temp descriptor = bld.as_uniform(get_ssa_temp(ctx, intrin->src[0].ssa));
   Temp v_offset = as_vgpr(ctx, get_ssa_temp(ctx, intrin->src[1].ssa));
   Temp s_offset = bld.as_uniform(get_ssa_temp(ctx, intrin->src[2].ssa));

   bool swizzled = nir_intrinsic_is_swizzled(intrin);
   bool reorder = nir_intrinsic_can_reorder(intrin);
   bool slc = nir_intrinsic_slc_amd(intrin);

   unsigned const_offset = nir_intrinsic_base(intrin);
   unsigned elem_size_bytes = intrin->dest.ssa.bit_size / 8u;
   unsigned num_components = intrin->dest.ssa.num_components;
   unsigned swizzle_element_size = swizzled ? (ctx->program->chip_class <= GFX8 ? 4 : 16) : 0;

   load_vmem_mubuf(ctx, dst, descriptor, v_offset, s_offset, const_offset, elem_size_bytes,
                   num_components, swizzle_element_size, !swizzled, reorder, slc);
}

void
visit_store_buffer(isel_context* ctx, nir_intrinsic_instr* intrin)
{
   Temp store_src = get_ssa_temp(ctx, intrin->src[0].ssa);
   Temp descriptor = get_ssa_temp(ctx, intrin->src[1].ssa);
   Temp v_offset = get_ssa_temp(ctx, intrin->src[2].ssa);
   Temp s_offset = get_ssa_temp(ctx, intrin->src[3].ssa);

   bool swizzled = nir_intrinsic_is_swizzled(intrin);
   bool slc = nir_intrinsic_slc_amd(intrin);

   unsigned const_offset = nir_intrinsic_base(intrin);
   unsigned write_mask = nir_intrinsic_write_mask(intrin);
   unsigned elem_size_bytes = intrin->src[0].ssa->bit_size / 8u;

   nir_variable_mode mem_mode = nir_intrinsic_memory_modes(intrin);
   memory_sync_info sync(mem_mode == nir_var_shader_out ? storage_vmem_output : storage_none);

   store_vmem_mubuf(ctx, store_src, descriptor, v_offset, s_offset, const_offset, elem_size_bytes,
                    write_mask, !swizzled, sync, slc);
}

sync_scope
translate_nir_scope(nir_scope scope)
{
   switch (scope) {
   case NIR_SCOPE_NONE:
   case NIR_SCOPE_INVOCATION: return scope_invocation;
   case NIR_SCOPE_SUBGROUP: return scope_subgroup;
   case NIR_SCOPE_WORKGROUP: return scope_workgroup;
   case NIR_SCOPE_QUEUE_FAMILY: return scope_queuefamily;
   case NIR_SCOPE_DEVICE: return scope_device;
   case NIR_SCOPE_SHADER_CALL: unreachable("unsupported scope");
   }
   unreachable("invalid scope");
}

void
emit_scoped_barrier(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);

   unsigned semantics = 0;
   unsigned storage = 0;
   sync_scope mem_scope = translate_nir_scope(nir_intrinsic_memory_scope(instr));
   sync_scope exec_scope = translate_nir_scope(nir_intrinsic_execution_scope(instr));

   /* We use shared storage for the following:
    * - compute shaders expose it in their API
    * - when tessellation is used, TCS and VS I/O is lowered to shared memory
    * - when GS is used on GFX9+, VS->GS and TES->GS I/O is lowered to shared memory
    * - additionally, when NGG is used on GFX10+, shared memory is used for certain features
    */
   bool shared_storage_used = ctx->stage.hw == HWStage::CS || ctx->stage.hw == HWStage::LS ||
                              ctx->stage.hw == HWStage::HS ||
                              (ctx->stage.hw == HWStage::GS && ctx->program->chip_class >= GFX9) ||
                              ctx->stage.hw == HWStage::NGG;

   /* Workgroup barriers can hang merged shaders that can potentially have 0 threads in either half.
    * They are allowed in CS, TCS, and in any NGG shader.
    */
   ASSERTED bool workgroup_scope_allowed =
      ctx->stage.hw == HWStage::CS || ctx->stage.hw == HWStage::HS || ctx->stage.hw == HWStage::NGG;

   unsigned nir_storage = nir_intrinsic_memory_modes(instr);
   if (nir_storage & (nir_var_mem_ssbo | nir_var_mem_global))
      storage |= storage_buffer | storage_image; // TODO: split this when NIR gets nir_var_mem_image
   if (shared_storage_used && (nir_storage & nir_var_mem_shared))
      storage |= storage_shared;

   unsigned nir_semantics = nir_intrinsic_memory_semantics(instr);
   if (nir_semantics & NIR_MEMORY_ACQUIRE)
      semantics |= semantic_acquire | semantic_release;
   if (nir_semantics & NIR_MEMORY_RELEASE)
      semantics |= semantic_acquire | semantic_release;

   assert(!(nir_semantics & (NIR_MEMORY_MAKE_AVAILABLE | NIR_MEMORY_MAKE_VISIBLE)));
   assert(exec_scope != scope_workgroup || workgroup_scope_allowed);

   bld.barrier(aco_opcode::p_barrier,
               memory_sync_info((storage_class)storage, (memory_semantics)semantics, mem_scope),
               exec_scope);
}

void
visit_load_shared(isel_context* ctx, nir_intrinsic_instr* instr)
{
   // TODO: implement sparse reads using ds_read2_b32 and nir_ssa_def_components_read()
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   Temp address = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   Builder bld(ctx->program, ctx->block);

   unsigned elem_size_bytes = instr->dest.ssa.bit_size / 8;
   unsigned num_components = instr->dest.ssa.num_components;
   unsigned align = nir_intrinsic_align_mul(instr) ? nir_intrinsic_align(instr) : elem_size_bytes;
   load_lds(ctx, elem_size_bytes, num_components, dst, address, nir_intrinsic_base(instr), align);
}

void
visit_store_shared(isel_context* ctx, nir_intrinsic_instr* instr)
{
   unsigned writemask = nir_intrinsic_write_mask(instr);
   Temp data = get_ssa_temp(ctx, instr->src[0].ssa);
   Temp address = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));
   unsigned elem_size_bytes = instr->src[0].ssa->bit_size / 8;

   unsigned align = nir_intrinsic_align_mul(instr) ? nir_intrinsic_align(instr) : elem_size_bytes;
   store_lds(ctx, elem_size_bytes, data, writemask, address, nir_intrinsic_base(instr), align);
}

void
visit_shared_atomic(isel_context* ctx, nir_intrinsic_instr* instr)
{
   unsigned offset = nir_intrinsic_base(instr);
   Builder bld(ctx->program, ctx->block);
   Operand m = load_lds_size_m0(bld);
   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));
   Temp address = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));

   unsigned num_operands = 3;
   aco_opcode op32, op64, op32_rtn, op64_rtn;
   switch (instr->intrinsic) {
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
      op64_rtn = aco_opcode::ds_wrxchg_rtn_b64;
      break;
   case nir_intrinsic_shared_atomic_comp_swap:
      op32 = aco_opcode::ds_cmpst_b32;
      op64 = aco_opcode::ds_cmpst_b64;
      op32_rtn = aco_opcode::ds_cmpst_rtn_b32;
      op64_rtn = aco_opcode::ds_cmpst_rtn_b64;
      num_operands = 4;
      break;
   case nir_intrinsic_shared_atomic_fadd:
      op32 = aco_opcode::ds_add_f32;
      op32_rtn = aco_opcode::ds_add_rtn_f32;
      op64 = aco_opcode::num_opcodes;
      op64_rtn = aco_opcode::num_opcodes;
      break;
   case nir_intrinsic_shared_atomic_fmin:
      op32 = aco_opcode::ds_min_f32;
      op32_rtn = aco_opcode::ds_min_rtn_f32;
      op64 = aco_opcode::ds_min_f64;
      op64_rtn = aco_opcode::ds_min_rtn_f64;
      break;
   case nir_intrinsic_shared_atomic_fmax:
      op32 = aco_opcode::ds_max_f32;
      op32_rtn = aco_opcode::ds_max_rtn_f32;
      op64 = aco_opcode::ds_max_f64;
      op64_rtn = aco_opcode::ds_max_rtn_f64;
      break;
   default: unreachable("Unhandled shared atomic intrinsic");
   }

   bool return_previous = !nir_ssa_def_is_unused(&instr->dest.ssa);

   aco_opcode op;
   if (data.size() == 1) {
      assert(instr->dest.ssa.bit_size == 32);
      op = return_previous ? op32_rtn : op32;
   } else {
      assert(instr->dest.ssa.bit_size == 64);
      op = return_previous ? op64_rtn : op64;
   }

   if (offset > 65535) {
      address = bld.vadd32(bld.def(v1), Operand::c32(offset), address);
      offset = 0;
   }

   aco_ptr<DS_instruction> ds;
   ds.reset(
      create_instruction<DS_instruction>(op, Format::DS, num_operands, return_previous ? 1 : 0));
   ds->operands[0] = Operand(address);
   ds->operands[1] = Operand(data);
   if (num_operands == 4) {
      Temp data2 = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[2].ssa));
      ds->operands[2] = Operand(data2);
   }
   ds->operands[num_operands - 1] = m;
   ds->offset0 = offset;
   if (return_previous)
      ds->definitions[0] = Definition(get_ssa_temp(ctx, &instr->dest.ssa));
   ds->sync = memory_sync_info(storage_shared, semantic_atomicrmw);
   ctx->block->instructions.emplace_back(std::move(ds));
}

Temp
get_scratch_resource(isel_context* ctx)
{
   Builder bld(ctx->program, ctx->block);
   Temp scratch_addr = ctx->program->private_segment_buffer;
   if (ctx->stage != compute_cs)
      scratch_addr =
         bld.smem(aco_opcode::s_load_dwordx2, bld.def(s2), scratch_addr, Operand::zero());

   uint32_t rsrc_conf =
      S_008F0C_ADD_TID_ENABLE(1) | S_008F0C_INDEX_STRIDE(ctx->program->wave_size == 64 ? 3 : 2);

   if (ctx->program->chip_class >= GFX10) {
      rsrc_conf |= S_008F0C_FORMAT(V_008F0C_GFX10_FORMAT_32_FLOAT) |
                   S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW) | S_008F0C_RESOURCE_LEVEL(1);
   } else if (ctx->program->chip_class <=
              GFX7) { /* dfmt modifies stride on GFX8/GFX9 when ADD_TID_EN=1 */
      rsrc_conf |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                   S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
   }

   /* older generations need element size = 4 bytes. element size removed in GFX9 */
   if (ctx->program->chip_class <= GFX8)
      rsrc_conf |= S_008F0C_ELEMENT_SIZE(1);

   return bld.pseudo(aco_opcode::p_create_vector, bld.def(s4), scratch_addr, Operand::c32(-1u),
                     Operand::c32(rsrc_conf));
}

void
visit_load_scratch(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp rsrc = get_scratch_resource(ctx);
   Temp offset = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   LoadEmitInfo info = {Operand(offset), dst, instr->dest.ssa.num_components,
                        instr->dest.ssa.bit_size / 8u, rsrc};
   info.align_mul = nir_intrinsic_align_mul(instr);
   info.align_offset = nir_intrinsic_align_offset(instr);
   info.swizzle_component_size = ctx->program->chip_class <= GFX8 ? 4 : 0;
   info.sync = memory_sync_info(storage_scratch, semantic_private);
   info.soffset = ctx->program->scratch_offset;
   emit_load(ctx, bld, info, scratch_load_params);
}

void
visit_store_scratch(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Temp rsrc = get_scratch_resource(ctx);
   Temp data = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   Temp offset = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));

   unsigned elem_size_bytes = instr->src[0].ssa->bit_size / 8;
   unsigned writemask = widen_mask(nir_intrinsic_write_mask(instr), elem_size_bytes);

   unsigned write_count = 0;
   Temp write_datas[32];
   unsigned offsets[32];
   unsigned swizzle_component_size = ctx->program->chip_class <= GFX8 ? 4 : 16;
   split_buffer_store(ctx, instr, false, RegType::vgpr, data, writemask, swizzle_component_size,
                      &write_count, write_datas, offsets);

   for (unsigned i = 0; i < write_count; i++) {
      aco_opcode op = get_buffer_store_op(write_datas[i].bytes());
      Instruction* mubuf = bld.mubuf(op, rsrc, offset, ctx->program->scratch_offset, write_datas[i],
                                     offsets[i], true, true);
      mubuf->mubuf().sync = memory_sync_info(storage_scratch, semantic_private);
   }
}

void
visit_load_sample_mask_in(isel_context* ctx, nir_intrinsic_instr* instr)
{
   uint8_t log2_ps_iter_samples;
   if (ctx->program->info->ps.uses_sample_shading) {
      log2_ps_iter_samples = util_logbase2(ctx->options->key.fs.num_samples);
   } else {
      log2_ps_iter_samples = ctx->options->key.fs.log2_ps_iter_samples;
   }

   Builder bld(ctx->program, ctx->block);

   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

   if (log2_ps_iter_samples) {
      /* gl_SampleMaskIn[0] = (SampleCoverage & (1 << gl_SampleID)). */
      Temp sample_id =
         bld.vop3(aco_opcode::v_bfe_u32, bld.def(v1), get_arg(ctx, ctx->args->ac.ancillary),
                  Operand::c32(8u), Operand::c32(4u));
      Temp mask = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), sample_id,
                           bld.copy(bld.def(v1), Operand::c32(1u)));
      bld.vop2(aco_opcode::v_and_b32, Definition(dst), mask,
               get_arg(ctx, ctx->args->ac.sample_coverage));
   } else {
      bld.copy(Definition(dst), get_arg(ctx, ctx->args->ac.sample_coverage));
   }
}

void
visit_emit_vertex_with_counter(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);

   unsigned stream = nir_intrinsic_stream_id(instr);
   Temp next_vertex = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[0].ssa));
   next_vertex = bld.v_mul_imm(bld.def(v1), next_vertex, 4u);
   nir_const_value* next_vertex_cv = nir_src_as_const_value(instr->src[0]);

   /* get GSVS ring */
   Temp gsvs_ring =
      bld.smem(aco_opcode::s_load_dwordx4, bld.def(s4), ctx->program->private_segment_buffer,
               Operand::c32(RING_GSVS_GS * 16u));

   unsigned num_components = ctx->program->info->gs.num_stream_output_components[stream];

   unsigned stride = 4u * num_components * ctx->shader->info.gs.vertices_out;
   unsigned stream_offset = 0;
   for (unsigned i = 0; i < stream; i++) {
      unsigned prev_stride = 4u * ctx->program->info->gs.num_stream_output_components[i] *
                             ctx->shader->info.gs.vertices_out;
      stream_offset += prev_stride * ctx->program->wave_size;
   }

   /* Limit on the stride field for <= GFX7. */
   assert(stride < (1 << 14));

   Temp gsvs_dwords[4];
   for (unsigned i = 0; i < 4; i++)
      gsvs_dwords[i] = bld.tmp(s1);
   bld.pseudo(aco_opcode::p_split_vector, Definition(gsvs_dwords[0]), Definition(gsvs_dwords[1]),
              Definition(gsvs_dwords[2]), Definition(gsvs_dwords[3]), gsvs_ring);

   if (stream_offset) {
      Temp stream_offset_tmp = bld.copy(bld.def(s1), Operand::c32(stream_offset));

      Temp carry = bld.tmp(s1);
      gsvs_dwords[0] = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.scc(Definition(carry)),
                                gsvs_dwords[0], stream_offset_tmp);
      gsvs_dwords[1] = bld.sop2(aco_opcode::s_addc_u32, bld.def(s1), bld.def(s1, scc),
                                gsvs_dwords[1], Operand::zero(), bld.scc(carry));
   }

   gsvs_dwords[1] = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), gsvs_dwords[1],
                             Operand::c32(S_008F04_STRIDE(stride)));
   gsvs_dwords[2] = bld.copy(bld.def(s1), Operand::c32(ctx->program->wave_size));

   gsvs_ring = bld.pseudo(aco_opcode::p_create_vector, bld.def(s4), gsvs_dwords[0], gsvs_dwords[1],
                          gsvs_dwords[2], gsvs_dwords[3]);

   unsigned offset = 0;
   for (unsigned i = 0; i <= VARYING_SLOT_VAR31; i++) {
      if (ctx->program->info->gs.output_streams[i] != stream)
         continue;

      for (unsigned j = 0; j < 4; j++) {
         if (!(ctx->program->info->gs.output_usage_mask[i] & (1 << j)))
            continue;

         if (ctx->outputs.mask[i] & (1 << j)) {
            Operand vaddr_offset = next_vertex_cv ? Operand(v1) : Operand(next_vertex);
            unsigned const_offset = (offset + (next_vertex_cv ? next_vertex_cv->u32 : 0u)) * 4u;
            if (const_offset >= 4096u) {
               if (vaddr_offset.isUndefined())
                  vaddr_offset = bld.copy(bld.def(v1), Operand::c32(const_offset / 4096u * 4096u));
               else
                  vaddr_offset = bld.vadd32(bld.def(v1), Operand::c32(const_offset / 4096u * 4096u),
                                            vaddr_offset);
               const_offset %= 4096u;
            }

            aco_ptr<MTBUF_instruction> mtbuf{create_instruction<MTBUF_instruction>(
               aco_opcode::tbuffer_store_format_x, Format::MTBUF, 4, 0)};
            mtbuf->operands[0] = Operand(gsvs_ring);
            mtbuf->operands[1] = vaddr_offset;
            mtbuf->operands[2] = Operand(get_arg(ctx, ctx->args->ac.gs2vs_offset));
            mtbuf->operands[3] = Operand(ctx->outputs.temps[i * 4u + j]);
            mtbuf->offen = !vaddr_offset.isUndefined();
            mtbuf->dfmt = V_008F0C_BUF_DATA_FORMAT_32;
            mtbuf->nfmt = V_008F0C_BUF_NUM_FORMAT_UINT;
            mtbuf->offset = const_offset;
            mtbuf->glc = true;
            mtbuf->slc = true;
            mtbuf->sync = memory_sync_info(storage_vmem_output, semantic_can_reorder);
            bld.insert(std::move(mtbuf));
         }

         offset += ctx->shader->info.gs.vertices_out;
      }

      /* outputs for the next vertex are undefined and keeping them around can
       * create invalid IR with control flow */
      ctx->outputs.mask[i] = 0;
   }

   bld.sopp(aco_opcode::s_sendmsg, bld.m0(ctx->gs_wave_id), -1, sendmsg_gs(false, true, stream));
}

Temp
emit_boolean_reduce(isel_context* ctx, nir_op op, unsigned cluster_size, Temp src)
{
   Builder bld(ctx->program, ctx->block);

   if (cluster_size == 1) {
      return src;
   }
   if (op == nir_op_iand && cluster_size == 4) {
      /* subgroupClusteredAnd(val, 4) -> ~wqm(exec & ~val) */
      Temp tmp =
         bld.sop2(Builder::s_andn2, bld.def(bld.lm), bld.def(s1, scc), Operand(exec, bld.lm), src);
      return bld.sop1(Builder::s_not, bld.def(bld.lm), bld.def(s1, scc),
                      bld.sop1(Builder::s_wqm, bld.def(bld.lm), bld.def(s1, scc), tmp));
   } else if (op == nir_op_ior && cluster_size == 4) {
      /* subgroupClusteredOr(val, 4) -> wqm(val & exec) */
      return bld.sop1(
         Builder::s_wqm, bld.def(bld.lm), bld.def(s1, scc),
         bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), src, Operand(exec, bld.lm)));
   } else if (op == nir_op_iand && cluster_size == ctx->program->wave_size) {
      /* subgroupAnd(val) -> (exec & ~val) == 0 */
      Temp tmp =
         bld.sop2(Builder::s_andn2, bld.def(bld.lm), bld.def(s1, scc), Operand(exec, bld.lm), src)
            .def(1)
            .getTemp();
      Temp cond = bool_to_vector_condition(ctx, emit_wqm(bld, tmp));
      return bld.sop1(Builder::s_not, bld.def(bld.lm), bld.def(s1, scc), cond);
   } else if (op == nir_op_ior && cluster_size == ctx->program->wave_size) {
      /* subgroupOr(val) -> (val & exec) != 0 */
      Temp tmp =
         bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), src, Operand(exec, bld.lm))
            .def(1)
            .getTemp();
      return bool_to_vector_condition(ctx, tmp);
   } else if (op == nir_op_ixor && cluster_size == ctx->program->wave_size) {
      /* subgroupXor(val) -> s_bcnt1_i32_b64(val & exec) & 1 */
      Temp tmp =
         bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), src, Operand(exec, bld.lm));
      tmp = bld.sop1(Builder::s_bcnt1_i32, bld.def(s1), bld.def(s1, scc), tmp);
      tmp = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), tmp, Operand::c32(1u))
               .def(1)
               .getTemp();
      return bool_to_vector_condition(ctx, tmp);
   } else {
      /* subgroupClustered{And,Or,Xor}(val, n):
       *   lane_id = v_mbcnt_hi_u32_b32(-1, v_mbcnt_lo_u32_b32(-1, 0)) (just v_mbcnt_lo on wave32)
       *   cluster_offset = ~(n - 1) & lane_id cluster_mask = ((1 << n) - 1)
       * subgroupClusteredAnd():
       *   return ((val | ~exec) >> cluster_offset) & cluster_mask == cluster_mask
       * subgroupClusteredOr():
       *   return ((val & exec) >> cluster_offset) & cluster_mask != 0
       * subgroupClusteredXor():
       *   return v_bnt_u32_b32(((val & exec) >> cluster_offset) & cluster_mask, 0) & 1 != 0
       */
      Temp lane_id = emit_mbcnt(ctx, bld.tmp(v1));
      Temp cluster_offset = bld.vop2(aco_opcode::v_and_b32, bld.def(v1),
                                     Operand::c32(~uint32_t(cluster_size - 1)), lane_id);

      Temp tmp;
      if (op == nir_op_iand)
         tmp = bld.sop2(Builder::s_orn2, bld.def(bld.lm), bld.def(s1, scc), src,
                        Operand(exec, bld.lm));
      else
         tmp =
            bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), src, Operand(exec, bld.lm));

      uint32_t cluster_mask = cluster_size == 32 ? -1 : (1u << cluster_size) - 1u;

      if (ctx->program->chip_class <= GFX7)
         tmp = bld.vop3(aco_opcode::v_lshr_b64, bld.def(v2), tmp, cluster_offset);
      else if (ctx->program->wave_size == 64)
         tmp = bld.vop3(aco_opcode::v_lshrrev_b64, bld.def(v2), cluster_offset, tmp);
      else
         tmp = bld.vop2_e64(aco_opcode::v_lshrrev_b32, bld.def(v1), cluster_offset, tmp);
      tmp = emit_extract_vector(ctx, tmp, 0, v1);
      if (cluster_mask != 0xffffffff)
         tmp = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(cluster_mask), tmp);

      if (op == nir_op_iand) {
         return bld.vopc(aco_opcode::v_cmp_eq_u32, bld.hint_vcc(bld.lm), Operand::c32(cluster_mask),
                         tmp);
      } else if (op == nir_op_ior) {
         return bld.vopc(aco_opcode::v_cmp_lg_u32, bld.hint_vcc(bld.lm), Operand::zero(), tmp);
      } else if (op == nir_op_ixor) {
         tmp = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(1u),
                        bld.vop3(aco_opcode::v_bcnt_u32_b32, bld.def(v1), tmp, Operand::zero()));
         return bld.vopc(aco_opcode::v_cmp_lg_u32, bld.hint_vcc(bld.lm), Operand::zero(), tmp);
      }
      assert(false);
      return Temp();
   }
}

Temp
emit_boolean_exclusive_scan(isel_context* ctx, nir_op op, Temp src)
{
   Builder bld(ctx->program, ctx->block);
   assert(src.regClass() == bld.lm);

   /* subgroupExclusiveAnd(val) -> mbcnt(exec & ~val) == 0
    * subgroupExclusiveOr(val) -> mbcnt(val & exec) != 0
    * subgroupExclusiveXor(val) -> mbcnt(val & exec) & 1 != 0
    */
   Temp tmp;
   if (op == nir_op_iand)
      tmp =
         bld.sop2(Builder::s_andn2, bld.def(bld.lm), bld.def(s1, scc), Operand(exec, bld.lm), src);
   else
      tmp = bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), src, Operand(exec, bld.lm));

   Temp mbcnt = emit_mbcnt(ctx, bld.tmp(v1), Operand(tmp));

   if (op == nir_op_iand)
      return bld.vopc(aco_opcode::v_cmp_eq_u32, bld.hint_vcc(bld.lm), Operand::zero(), mbcnt);
   else if (op == nir_op_ior)
      return bld.vopc(aco_opcode::v_cmp_lg_u32, bld.hint_vcc(bld.lm), Operand::zero(), mbcnt);
   else if (op == nir_op_ixor)
      return bld.vopc(aco_opcode::v_cmp_lg_u32, bld.hint_vcc(bld.lm), Operand::zero(),
                      bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(1u), mbcnt));

   assert(false);
   return Temp();
}

Temp
emit_boolean_inclusive_scan(isel_context* ctx, nir_op op, Temp src)
{
   Builder bld(ctx->program, ctx->block);

   /* subgroupInclusiveAnd(val) -> subgroupExclusiveAnd(val) && val
    * subgroupInclusiveOr(val) -> subgroupExclusiveOr(val) || val
    * subgroupInclusiveXor(val) -> subgroupExclusiveXor(val) ^^ val
    */
   Temp tmp = emit_boolean_exclusive_scan(ctx, op, src);
   if (op == nir_op_iand)
      return bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), tmp, src);
   else if (op == nir_op_ior)
      return bld.sop2(Builder::s_or, bld.def(bld.lm), bld.def(s1, scc), tmp, src);
   else if (op == nir_op_ixor)
      return bld.sop2(Builder::s_xor, bld.def(bld.lm), bld.def(s1, scc), tmp, src);

   assert(false);
   return Temp();
}

ReduceOp
get_reduce_op(nir_op op, unsigned bit_size)
{
   switch (op) {
#define CASEI(name)                                                                                \
   case nir_op_##name:                                                                             \
      return (bit_size == 32)   ? name##32                                                         \
             : (bit_size == 16) ? name##16                                                         \
             : (bit_size == 8)  ? name##8                                                          \
                                : name##64;
#define CASEF(name)                                                                                \
   case nir_op_##name: return (bit_size == 32) ? name##32 : (bit_size == 16) ? name##16 : name##64;
      CASEI(iadd)
      CASEI(imul)
      CASEI(imin)
      CASEI(umin)
      CASEI(imax)
      CASEI(umax)
      CASEI(iand)
      CASEI(ior)
      CASEI(ixor)
      CASEF(fadd)
      CASEF(fmul)
      CASEF(fmin)
      CASEF(fmax)
   default: unreachable("unknown reduction op");
#undef CASEI
#undef CASEF
   }
}

void
emit_uniform_subgroup(isel_context* ctx, nir_intrinsic_instr* instr, Temp src)
{
   Builder bld(ctx->program, ctx->block);
   Definition dst(get_ssa_temp(ctx, &instr->dest.ssa));
   assert(dst.regClass().type() != RegType::vgpr);
   if (src.regClass().type() == RegType::vgpr)
      bld.pseudo(aco_opcode::p_as_uniform, dst, src);
   else
      bld.copy(dst, src);
}

void
emit_addition_uniform_reduce(isel_context* ctx, nir_op op, Definition dst, nir_src src, Temp count)
{
   Builder bld(ctx->program, ctx->block);
   Temp src_tmp = get_ssa_temp(ctx, src.ssa);

   if (op == nir_op_fadd) {
      src_tmp = as_vgpr(ctx, src_tmp);
      Temp tmp = dst.regClass() == s1 ? bld.tmp(src_tmp.regClass()) : dst.getTemp();

      if (src.ssa->bit_size == 16) {
         count = bld.vop1(aco_opcode::v_cvt_f16_u16, bld.def(v2b), count);
         bld.vop2(aco_opcode::v_mul_f16, Definition(tmp), count, src_tmp);
      } else {
         assert(src.ssa->bit_size == 32);
         count = bld.vop1(aco_opcode::v_cvt_f32_u32, bld.def(v1), count);
         bld.vop2(aco_opcode::v_mul_f32, Definition(tmp), count, src_tmp);
      }

      if (tmp != dst.getTemp())
         bld.pseudo(aco_opcode::p_as_uniform, dst, tmp);

      return;
   }

   if (dst.regClass() == s1)
      src_tmp = bld.as_uniform(src_tmp);

   if (op == nir_op_ixor && count.type() == RegType::sgpr)
      count =
         bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), count, Operand::c32(1u));
   else if (op == nir_op_ixor)
      count = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(1u), count);

   assert(dst.getTemp().type() == count.type());

   if (nir_src_is_const(src)) {
      if (nir_src_as_uint(src) == 1 && dst.bytes() <= 2)
         bld.pseudo(aco_opcode::p_extract_vector, dst, count, Operand::zero());
      else if (nir_src_as_uint(src) == 1)
         bld.copy(dst, count);
      else if (nir_src_as_uint(src) == 0 && dst.bytes() <= 2)
         bld.vop1(aco_opcode::v_mov_b32, dst, Operand::zero()); /* RA will use SDWA if possible */
      else if (nir_src_as_uint(src) == 0)
         bld.copy(dst, Operand::zero());
      else if (count.type() == RegType::vgpr)
         bld.v_mul_imm(dst, count, nir_src_as_uint(src));
      else
         bld.sop2(aco_opcode::s_mul_i32, dst, src_tmp, count);
   } else if (dst.bytes() <= 2 && ctx->program->chip_class >= GFX10) {
      bld.vop3(aco_opcode::v_mul_lo_u16_e64, dst, src_tmp, count);
   } else if (dst.bytes() <= 2 && ctx->program->chip_class >= GFX8) {
      bld.vop2(aco_opcode::v_mul_lo_u16, dst, src_tmp, count);
   } else if (dst.getTemp().type() == RegType::vgpr) {
      bld.vop3(aco_opcode::v_mul_lo_u32, dst, src_tmp, count);
   } else {
      bld.sop2(aco_opcode::s_mul_i32, dst, src_tmp, count);
   }
}

bool
emit_uniform_reduce(isel_context* ctx, nir_intrinsic_instr* instr)
{
   nir_op op = (nir_op)nir_intrinsic_reduction_op(instr);
   if (op == nir_op_imul || op == nir_op_fmul)
      return false;

   if (op == nir_op_iadd || op == nir_op_ixor || op == nir_op_fadd) {
      Builder bld(ctx->program, ctx->block);
      Definition dst(get_ssa_temp(ctx, &instr->dest.ssa));
      unsigned bit_size = instr->src[0].ssa->bit_size;
      if (bit_size > 32)
         return false;

      Temp thread_count =
         bld.sop1(Builder::s_bcnt1_i32, bld.def(s1), bld.def(s1, scc), Operand(exec, bld.lm));

      emit_addition_uniform_reduce(ctx, op, dst, instr->src[0], thread_count);
   } else {
      emit_uniform_subgroup(ctx, instr, get_ssa_temp(ctx, instr->src[0].ssa));
   }

   return true;
}

bool
emit_uniform_scan(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   Definition dst(get_ssa_temp(ctx, &instr->dest.ssa));
   nir_op op = (nir_op)nir_intrinsic_reduction_op(instr);
   bool inc = instr->intrinsic == nir_intrinsic_inclusive_scan;

   if (op == nir_op_imul || op == nir_op_fmul)
      return false;

   if (op == nir_op_iadd || op == nir_op_ixor || op == nir_op_fadd) {
      if (instr->src[0].ssa->bit_size > 32)
         return false;

      Temp packed_tid;
      if (inc)
         packed_tid = emit_mbcnt(ctx, bld.tmp(v1), Operand(exec, bld.lm), Operand::c32(1u));
      else
         packed_tid = emit_mbcnt(ctx, bld.tmp(v1), Operand(exec, bld.lm));

      emit_addition_uniform_reduce(ctx, op, dst, instr->src[0], packed_tid);
      return true;
   }

   assert(op == nir_op_imin || op == nir_op_umin || op == nir_op_imax || op == nir_op_umax ||
          op == nir_op_iand || op == nir_op_ior || op == nir_op_fmin || op == nir_op_fmax);

   if (inc) {
      emit_uniform_subgroup(ctx, instr, get_ssa_temp(ctx, instr->src[0].ssa));
      return true;
   }

   /* Copy the source and write the reduction operation identity to the first lane. */
   Temp lane = bld.sop1(Builder::s_ff1_i32, bld.def(s1), Operand(exec, bld.lm));
   Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
   ReduceOp reduce_op = get_reduce_op(op, instr->src[0].ssa->bit_size);
   if (dst.bytes() == 8) {
      Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
      bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
      uint32_t identity_lo = get_reduction_identity(reduce_op, 0);
      uint32_t identity_hi = get_reduction_identity(reduce_op, 1);

      lo =
         bld.writelane(bld.def(v1), bld.copy(bld.hint_m0(s1), Operand::c32(identity_lo)), lane, lo);
      hi =
         bld.writelane(bld.def(v1), bld.copy(bld.hint_m0(s1), Operand::c32(identity_hi)), lane, hi);
      bld.pseudo(aco_opcode::p_create_vector, dst, lo, hi);
   } else {
      uint32_t identity = get_reduction_identity(reduce_op, 0);
      bld.writelane(dst, bld.copy(bld.hint_m0(s1), Operand::c32(identity)), lane,
                    as_vgpr(ctx, src));
   }

   return true;
}

Temp
emit_reduction_instr(isel_context* ctx, aco_opcode aco_op, ReduceOp op, unsigned cluster_size,
                     Definition dst, Temp src)
{
   assert(src.bytes() <= 8);
   assert(src.type() == RegType::vgpr);

   Builder bld(ctx->program, ctx->block);

   unsigned num_defs = 0;
   Definition defs[5];
   defs[num_defs++] = dst;
   defs[num_defs++] = bld.def(bld.lm); /* used internally to save/restore exec */

   /* scalar identity temporary */
   bool need_sitmp = (ctx->program->chip_class <= GFX7 || ctx->program->chip_class >= GFX10) &&
                     aco_op != aco_opcode::p_reduce;
   if (aco_op == aco_opcode::p_exclusive_scan) {
      need_sitmp |= (op == imin8 || op == imin16 || op == imin32 || op == imin64 || op == imax8 ||
                     op == imax16 || op == imax32 || op == imax64 || op == fmin16 || op == fmin32 ||
                     op == fmin64 || op == fmax16 || op == fmax32 || op == fmax64 || op == fmul16 ||
                     op == fmul64);
   }
   if (need_sitmp)
      defs[num_defs++] = bld.def(RegType::sgpr, dst.size());

   /* scc clobber */
   defs[num_defs++] = bld.def(s1, scc);

   /* vcc clobber */
   bool clobber_vcc = false;
   if ((op == iadd32 || op == imul64) && ctx->program->chip_class < GFX9)
      clobber_vcc = true;
   if ((op == iadd8 || op == iadd16) && ctx->program->chip_class < GFX8)
      clobber_vcc = true;
   if (op == iadd64 || op == umin64 || op == umax64 || op == imin64 || op == imax64)
      clobber_vcc = true;

   if (clobber_vcc)
      defs[num_defs++] = bld.def(bld.lm, vcc);

   Pseudo_reduction_instruction* reduce = create_instruction<Pseudo_reduction_instruction>(
      aco_op, Format::PSEUDO_REDUCTION, 3, num_defs);
   reduce->operands[0] = Operand(src);
   /* setup_reduce_temp will update these undef operands if needed */
   reduce->operands[1] = Operand(RegClass(RegType::vgpr, dst.size()).as_linear());
   reduce->operands[2] = Operand(v1.as_linear());
   std::copy(defs, defs + num_defs, reduce->definitions.begin());

   reduce->reduce_op = op;
   reduce->cluster_size = cluster_size;
   bld.insert(std::move(reduce));

   return dst.getTemp();
}

void
emit_interp_center(isel_context* ctx, Temp dst, Temp pos1, Temp pos2)
{
   Builder bld(ctx->program, ctx->block);
   Temp persp_center = get_arg(ctx, ctx->args->ac.persp_center);
   Temp p1 = emit_extract_vector(ctx, persp_center, 0, v1);
   Temp p2 = emit_extract_vector(ctx, persp_center, 1, v1);

   Temp ddx_1, ddx_2, ddy_1, ddy_2;
   uint32_t dpp_ctrl0 = dpp_quad_perm(0, 0, 0, 0);
   uint32_t dpp_ctrl1 = dpp_quad_perm(1, 1, 1, 1);
   uint32_t dpp_ctrl2 = dpp_quad_perm(2, 2, 2, 2);

   /* Build DD X/Y */
   if (ctx->program->chip_class >= GFX8) {
      Temp tl_1 = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), p1, dpp_ctrl0);
      ddx_1 = bld.vop2_dpp(aco_opcode::v_sub_f32, bld.def(v1), p1, tl_1, dpp_ctrl1);
      ddy_1 = bld.vop2_dpp(aco_opcode::v_sub_f32, bld.def(v1), p1, tl_1, dpp_ctrl2);
      Temp tl_2 = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), p2, dpp_ctrl0);
      ddx_2 = bld.vop2_dpp(aco_opcode::v_sub_f32, bld.def(v1), p2, tl_2, dpp_ctrl1);
      ddy_2 = bld.vop2_dpp(aco_opcode::v_sub_f32, bld.def(v1), p2, tl_2, dpp_ctrl2);
   } else {
      Temp tl_1 = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), p1, (1 << 15) | dpp_ctrl0);
      ddx_1 = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), p1, (1 << 15) | dpp_ctrl1);
      ddx_1 = bld.vop2(aco_opcode::v_sub_f32, bld.def(v1), ddx_1, tl_1);
      ddx_2 = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), p1, (1 << 15) | dpp_ctrl2);
      ddx_2 = bld.vop2(aco_opcode::v_sub_f32, bld.def(v1), ddx_2, tl_1);
      Temp tl_2 = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), p2, (1 << 15) | dpp_ctrl0);
      ddy_1 = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), p2, (1 << 15) | dpp_ctrl1);
      ddy_1 = bld.vop2(aco_opcode::v_sub_f32, bld.def(v1), ddy_1, tl_2);
      ddy_2 = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), p2, (1 << 15) | dpp_ctrl2);
      ddy_2 = bld.vop2(aco_opcode::v_sub_f32, bld.def(v1), ddy_2, tl_2);
   }

   /* res_k = p_k + ddx_k * pos1 + ddy_k * pos2 */
   aco_opcode mad =
      ctx->program->chip_class >= GFX10_3 ? aco_opcode::v_fma_f32 : aco_opcode::v_mad_f32;
   Temp tmp1 = bld.vop3(mad, bld.def(v1), ddx_1, pos1, p1);
   Temp tmp2 = bld.vop3(mad, bld.def(v1), ddx_2, pos1, p2);
   tmp1 = bld.vop3(mad, bld.def(v1), ddy_1, pos2, tmp1);
   tmp2 = bld.vop3(mad, bld.def(v1), ddy_2, pos2, tmp2);
   Temp wqm1 = bld.tmp(v1);
   emit_wqm(bld, tmp1, wqm1, true);
   Temp wqm2 = bld.tmp(v1);
   emit_wqm(bld, tmp2, wqm2, true);
   bld.pseudo(aco_opcode::p_create_vector, Definition(dst), wqm1, wqm2);
   return;
}

Temp merged_wave_info_to_mask(isel_context* ctx, unsigned i);
void ngg_emit_sendmsg_gs_alloc_req(isel_context* ctx, Temp vtx_cnt, Temp prm_cnt);
static void create_vs_exports(isel_context* ctx);

void
visit_intrinsic(isel_context* ctx, nir_intrinsic_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   switch (instr->intrinsic) {
   case nir_intrinsic_load_barycentric_sample:
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_centroid: {
      glsl_interp_mode mode = (glsl_interp_mode)nir_intrinsic_interp_mode(instr);
      Temp bary = Temp(0, s2);
      switch (mode) {
      case INTERP_MODE_SMOOTH:
      case INTERP_MODE_NONE:
         if (instr->intrinsic == nir_intrinsic_load_barycentric_pixel)
            bary = get_arg(ctx, ctx->args->ac.persp_center);
         else if (instr->intrinsic == nir_intrinsic_load_barycentric_centroid)
            bary = ctx->persp_centroid;
         else if (instr->intrinsic == nir_intrinsic_load_barycentric_sample)
            bary = get_arg(ctx, ctx->args->ac.persp_sample);
         break;
      case INTERP_MODE_NOPERSPECTIVE:
         if (instr->intrinsic == nir_intrinsic_load_barycentric_pixel)
            bary = get_arg(ctx, ctx->args->ac.linear_center);
         else if (instr->intrinsic == nir_intrinsic_load_barycentric_centroid)
            bary = ctx->linear_centroid;
         else if (instr->intrinsic == nir_intrinsic_load_barycentric_sample)
            bary = get_arg(ctx, ctx->args->ac.linear_sample);
         break;
      default: break;
      }
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      Temp p1 = emit_extract_vector(ctx, bary, 0, v1);
      Temp p2 = emit_extract_vector(ctx, bary, 1, v1);
      bld.pseudo(aco_opcode::p_create_vector, Definition(dst), Operand(p1), Operand(p2));
      emit_split_vector(ctx, dst, 2);
      break;
   }
   case nir_intrinsic_load_barycentric_model: {
      Temp model = get_arg(ctx, ctx->args->ac.pull_model);

      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      Temp p1 = emit_extract_vector(ctx, model, 0, v1);
      Temp p2 = emit_extract_vector(ctx, model, 1, v1);
      Temp p3 = emit_extract_vector(ctx, model, 2, v1);
      bld.pseudo(aco_opcode::p_create_vector, Definition(dst), Operand(p1), Operand(p2),
                 Operand(p3));
      emit_split_vector(ctx, dst, 3);
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
      // TODO: bounds checking?
      if (addr.type() == RegType::sgpr) {
         Operand offset;
         if (const_addr) {
            sample_pos_offset += const_addr->u32 << 3;
            offset = Operand::c32(sample_pos_offset);
         } else if (ctx->options->chip_class >= GFX9) {
            offset = bld.sop2(aco_opcode::s_lshl3_add_u32, bld.def(s1), bld.def(s1, scc), addr,
                              Operand::c32(sample_pos_offset));
         } else {
            offset = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), addr,
                              Operand::c32(3u));
            offset = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), bld.def(s1, scc), offset,
                              Operand::c32(sample_pos_offset));
         }

         Operand off = bld.copy(bld.def(s1), Operand(offset));
         sample_pos =
            bld.smem(aco_opcode::s_load_dwordx2, bld.def(s2), private_segment_buffer, off);

      } else if (ctx->options->chip_class >= GFX9) {
         addr = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(3u), addr);
         sample_pos = bld.global(aco_opcode::global_load_dwordx2, bld.def(v2), addr,
                                 private_segment_buffer, sample_pos_offset);
      } else if (ctx->options->chip_class >= GFX7) {
         /* addr += private_segment_buffer + sample_pos_offset */
         Temp tmp0 = bld.tmp(s1);
         Temp tmp1 = bld.tmp(s1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(tmp0), Definition(tmp1),
                    private_segment_buffer);
         Definition scc_tmp = bld.def(s1, scc);
         tmp0 = bld.sop2(aco_opcode::s_add_u32, bld.def(s1), scc_tmp, tmp0,
                         Operand::c32(sample_pos_offset));
         tmp1 = bld.sop2(aco_opcode::s_addc_u32, bld.def(s1), bld.def(s1, scc), tmp1,
                         Operand::zero(), bld.scc(scc_tmp.getTemp()));
         addr = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(3u), addr);
         Temp pck0 = bld.tmp(v1);
         Temp carry = bld.vadd32(Definition(pck0), tmp0, addr, true).def(1).getTemp();
         tmp1 = as_vgpr(ctx, tmp1);
         Temp pck1 = bld.vop2_e64(aco_opcode::v_addc_co_u32, bld.def(v1),
                                  bld.hint_vcc(bld.def(bld.lm)), tmp1, Operand::zero(), carry);
         addr = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), pck0, pck1);

         /* sample_pos = flat_load_dwordx2 addr */
         sample_pos = bld.flat(aco_opcode::flat_load_dwordx2, bld.def(v2), addr, Operand(s1));
      } else {
         assert(ctx->options->chip_class == GFX6);

         uint32_t rsrc_conf = S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
                              S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
         Temp rsrc = bld.pseudo(aco_opcode::p_create_vector, bld.def(s4), private_segment_buffer,
                                Operand::zero(), Operand::c32(rsrc_conf));

         addr = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(3u), addr);
         addr = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), addr, Operand::zero());

         sample_pos = bld.tmp(v2);

         aco_ptr<MUBUF_instruction> load{create_instruction<MUBUF_instruction>(
            aco_opcode::buffer_load_dwordx2, Format::MUBUF, 3, 1)};
         load->definitions[0] = Definition(sample_pos);
         load->operands[0] = Operand(rsrc);
         load->operands[1] = Operand(addr);
         load->operands[2] = Operand::zero();
         load->offset = sample_pos_offset;
         load->offen = 0;
         load->addr64 = true;
         load->glc = false;
         load->dlc = false;
         load->disable_wqm = false;
         ctx->block->instructions.emplace_back(std::move(load));
      }

      /* sample_pos -= 0.5 */
      Temp pos1 = bld.tmp(RegClass(sample_pos.type(), 1));
      Temp pos2 = bld.tmp(RegClass(sample_pos.type(), 1));
      bld.pseudo(aco_opcode::p_split_vector, Definition(pos1), Definition(pos2), sample_pos);
      pos1 = bld.vop2_e64(aco_opcode::v_sub_f32, bld.def(v1), pos1, Operand::c32(0x3f000000u));
      pos2 = bld.vop2_e64(aco_opcode::v_sub_f32, bld.def(v1), pos2, Operand::c32(0x3f000000u));

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
               Operand::zero(), get_arg(ctx, ctx->args->ac.front_face))
         .def(0)
         .setHint(vcc);
      break;
   }
   case nir_intrinsic_load_view_index: {
      if (ctx->stage.has(SWStage::VS) || ctx->stage.has(SWStage::GS) ||
          ctx->stage.has(SWStage::TCS) || ctx->stage.has(SWStage::TES)) {
         Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
         bld.copy(Definition(dst), Operand(get_arg(ctx, ctx->args->ac.view_index)));
         break;
      }
      FALLTHROUGH;
   }
   case nir_intrinsic_load_layer_id: {
      unsigned idx = nir_intrinsic_base(instr);
      bld.vintrp(aco_opcode::v_interp_mov_f32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
                 Operand::c32(2u), bld.m0(get_arg(ctx, ctx->args->ac.prim_mask)), idx, 0);
      break;
   }
   case nir_intrinsic_load_frag_coord: {
      emit_load_frag_coord(ctx, get_ssa_temp(ctx, &instr->dest.ssa), 4);
      break;
   }
   case nir_intrinsic_load_frag_shading_rate:
      emit_load_frag_shading_rate(ctx, get_ssa_temp(ctx, &instr->dest.ssa));
      break;
   case nir_intrinsic_load_sample_pos: {
      Temp posx = get_arg(ctx, ctx->args->ac.frag_pos[0]);
      Temp posy = get_arg(ctx, ctx->args->ac.frag_pos[1]);
      bld.pseudo(
         aco_opcode::p_create_vector, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
         posx.id() ? bld.vop1(aco_opcode::v_fract_f32, bld.def(v1), posx) : Operand::zero(),
         posy.id() ? bld.vop1(aco_opcode::v_fract_f32, bld.def(v1), posy) : Operand::zero());
      break;
   }
   case nir_intrinsic_load_tess_coord: visit_load_tess_coord(ctx, instr); break;
   case nir_intrinsic_load_interpolated_input: visit_load_interpolated_input(ctx, instr); break;
   case nir_intrinsic_store_output: visit_store_output(ctx, instr); break;
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_input_vertex: visit_load_input(ctx, instr); break;
   case nir_intrinsic_load_per_vertex_input: visit_load_per_vertex_input(ctx, instr); break;
   case nir_intrinsic_load_ubo: visit_load_ubo(ctx, instr); break;
   case nir_intrinsic_load_push_constant: visit_load_push_constant(ctx, instr); break;
   case nir_intrinsic_load_constant: visit_load_constant(ctx, instr); break;
   case nir_intrinsic_vulkan_resource_index: visit_load_resource(ctx, instr); break;
   case nir_intrinsic_terminate:
   case nir_intrinsic_discard: visit_discard(ctx, instr); break;
   case nir_intrinsic_terminate_if:
   case nir_intrinsic_discard_if: visit_discard_if(ctx, instr); break;
   case nir_intrinsic_load_shared: visit_load_shared(ctx, instr); break;
   case nir_intrinsic_store_shared: visit_store_shared(ctx, instr); break;
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
   case nir_intrinsic_shared_atomic_fadd:
   case nir_intrinsic_shared_atomic_fmin:
   case nir_intrinsic_shared_atomic_fmax: visit_shared_atomic(ctx, instr); break;
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_sparse_load: visit_image_load(ctx, instr); break;
   case nir_intrinsic_image_deref_store: visit_image_store(ctx, instr); break;
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
   case nir_intrinsic_image_deref_atomic_fmin:
   case nir_intrinsic_image_deref_atomic_fmax: visit_image_atomic(ctx, instr); break;
   case nir_intrinsic_image_deref_size: visit_image_size(ctx, instr); break;
   case nir_intrinsic_image_deref_samples: visit_image_samples(ctx, instr); break;
   case nir_intrinsic_load_ssbo: visit_load_ssbo(ctx, instr); break;
   case nir_intrinsic_store_ssbo: visit_store_ssbo(ctx, instr); break;
   case nir_intrinsic_load_global: visit_load_global(ctx, instr); break;
   case nir_intrinsic_load_buffer_amd: visit_load_buffer(ctx, instr); break;
   case nir_intrinsic_store_buffer_amd: visit_store_buffer(ctx, instr); break;
   case nir_intrinsic_store_global: visit_store_global(ctx, instr); break;
   case nir_intrinsic_global_atomic_add:
   case nir_intrinsic_global_atomic_imin:
   case nir_intrinsic_global_atomic_umin:
   case nir_intrinsic_global_atomic_imax:
   case nir_intrinsic_global_atomic_umax:
   case nir_intrinsic_global_atomic_and:
   case nir_intrinsic_global_atomic_or:
   case nir_intrinsic_global_atomic_xor:
   case nir_intrinsic_global_atomic_exchange:
   case nir_intrinsic_global_atomic_comp_swap:
   case nir_intrinsic_global_atomic_fmin:
   case nir_intrinsic_global_atomic_fmax: visit_global_atomic(ctx, instr); break;
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
   case nir_intrinsic_ssbo_atomic_fmin:
   case nir_intrinsic_ssbo_atomic_fmax: visit_atomic_ssbo(ctx, instr); break;
   case nir_intrinsic_load_scratch: visit_load_scratch(ctx, instr); break;
   case nir_intrinsic_store_scratch: visit_store_scratch(ctx, instr); break;
   case nir_intrinsic_get_ssbo_size: visit_get_ssbo_size(ctx, instr); break;
   case nir_intrinsic_scoped_barrier: emit_scoped_barrier(ctx, instr); break;
   case nir_intrinsic_load_num_workgroups: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.copy(Definition(dst), Operand(get_arg(ctx, ctx->args->ac.num_work_groups)));
      emit_split_vector(ctx, dst, 3);
      break;
   }
   case nir_intrinsic_load_local_invocation_id: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.copy(Definition(dst), Operand(get_arg(ctx, ctx->args->ac.local_invocation_ids)));
      emit_split_vector(ctx, dst, 3);
      break;
   }
   case nir_intrinsic_load_workgroup_id: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      struct ac_arg* args = ctx->args->ac.workgroup_ids;
      bld.pseudo(aco_opcode::p_create_vector, Definition(dst),
                 args[0].used ? Operand(get_arg(ctx, args[0])) : Operand::zero(),
                 args[1].used ? Operand(get_arg(ctx, args[1])) : Operand::zero(),
                 args[2].used ? Operand(get_arg(ctx, args[2])) : Operand::zero());
      emit_split_vector(ctx, dst, 3);
      break;
   }
   case nir_intrinsic_load_local_invocation_index: {
      if (ctx->stage.hw == HWStage::LS || ctx->stage.hw == HWStage::HS) {
         bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
                  get_arg(ctx, ctx->args->ac.vs_rel_patch_id));
         break;
      } else if (ctx->stage.hw == HWStage::GS || ctx->stage.hw == HWStage::NGG) {
         bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)), thread_id_in_threadgroup(ctx));
         break;
      }

      Temp id = emit_mbcnt(ctx, bld.tmp(v1));

      /* The tg_size bits [6:11] contain the subgroup id,
       * we need this multiplied by the wave size, and then OR the thread id to it.
       */
      if (ctx->program->wave_size == 64) {
         /* After the s_and the bits are already multiplied by 64 (left shifted by 6) so we can just
          * feed that to v_or */
         Temp tg_num = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc),
                                Operand::c32(0xfc0u), get_arg(ctx, ctx->args->ac.tg_size));
         bld.vop2(aco_opcode::v_or_b32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)), tg_num,
                  id);
      } else {
         /* Extract the bit field and multiply the result by 32 (left shift by 5), then do the OR */
         Temp tg_num =
            bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc),
                     get_arg(ctx, ctx->args->ac.tg_size), Operand::c32(0x6u | (0x6u << 16)));
         bld.vop3(aco_opcode::v_lshl_or_b32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
                  tg_num, Operand::c32(0x5u), id);
      }
      break;
   }
   case nir_intrinsic_load_subgroup_id: {
      if (ctx->stage == compute_cs) {
         bld.sop2(aco_opcode::s_bfe_u32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
                  bld.def(s1, scc), get_arg(ctx, ctx->args->ac.tg_size),
                  Operand::c32(0x6u | (0x6u << 16)));
      } else if (ctx->stage.hw == HWStage::NGG) {
         /* Get the id of the current wave within the threadgroup (workgroup) */
         bld.sop2(aco_opcode::s_bfe_u32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
                  bld.def(s1, scc), get_arg(ctx, ctx->args->ac.merged_wave_info),
                  Operand::c32(24u | (4u << 16)));
      } else {
         bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)), Operand::zero());
      }
      break;
   }
   case nir_intrinsic_load_subgroup_invocation: {
      emit_mbcnt(ctx, get_ssa_temp(ctx, &instr->dest.ssa));
      break;
   }
   case nir_intrinsic_load_num_subgroups: {
      if (ctx->stage == compute_cs)
         bld.sop2(aco_opcode::s_and_b32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
                  bld.def(s1, scc), Operand::c32(0x3fu), get_arg(ctx, ctx->args->ac.tg_size));
      else if (ctx->stage.hw == HWStage::NGG)
         bld.sop2(aco_opcode::s_bfe_u32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
                  bld.def(s1, scc), get_arg(ctx, ctx->args->ac.merged_wave_info),
                  Operand::c32(28u | (4u << 16)));
      else
         bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)), Operand::c32(0x1u));
      break;
   }
   case nir_intrinsic_ballot: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

      if (instr->src[0].ssa->bit_size == 1) {
         assert(src.regClass() == bld.lm);
      } else if (instr->src[0].ssa->bit_size == 32 && src.regClass() == v1) {
         src = bld.vopc(aco_opcode::v_cmp_lg_u32, bld.def(bld.lm), Operand::zero(), src);
      } else if (instr->src[0].ssa->bit_size == 64 && src.regClass() == v2) {
         src = bld.vopc(aco_opcode::v_cmp_lg_u64, bld.def(bld.lm), Operand::zero(), src);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }

      /* Make sure that all inactive lanes return zero.
       * Value-numbering might remove the comparison above */
      src = bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), src, Operand(exec, bld.lm));
      if (dst.size() != bld.lm.size()) {
         /* Wave32 with ballot size set to 64 */
         src =
            bld.pseudo(aco_opcode::p_create_vector, bld.def(dst.regClass()), src, Operand::zero());
      }

      emit_wqm(bld, src, dst);
      break;
   }
   case nir_intrinsic_shuffle:
   case nir_intrinsic_read_invocation: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      if (!nir_src_is_divergent(instr->src[0])) {
         emit_uniform_subgroup(ctx, instr, src);
      } else {
         Temp tid = get_ssa_temp(ctx, instr->src[1].ssa);
         if (instr->intrinsic == nir_intrinsic_read_invocation ||
             !nir_src_is_divergent(instr->src[1]))
            tid = bld.as_uniform(tid);
         Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

         if (instr->dest.ssa.bit_size != 1)
            src = as_vgpr(ctx, src);

         if (src.regClass() == v1b || src.regClass() == v2b) {
            Temp tmp = bld.tmp(v1);
            tmp = emit_wqm(bld, emit_bpermute(ctx, bld, tid, src), tmp);
            if (dst.type() == RegType::vgpr)
               bld.pseudo(aco_opcode::p_split_vector, Definition(dst),
                          bld.def(src.regClass() == v1b ? v3b : v2b), tmp);
            else
               bld.pseudo(aco_opcode::p_as_uniform, Definition(dst), tmp);
         } else if (src.regClass() == v1) {
            emit_wqm(bld, emit_bpermute(ctx, bld, tid, src), dst);
         } else if (src.regClass() == v2) {
            Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
            bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
            lo = emit_wqm(bld, emit_bpermute(ctx, bld, tid, lo));
            hi = emit_wqm(bld, emit_bpermute(ctx, bld, tid, hi));
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
            emit_split_vector(ctx, dst, 2);
         } else if (instr->dest.ssa.bit_size == 1 && tid.regClass() == s1) {
            assert(src.regClass() == bld.lm);
            Temp tmp = bld.sopc(Builder::s_bitcmp1, bld.def(s1, scc), src, tid);
            bool_to_vector_condition(ctx, emit_wqm(bld, tmp), dst);
         } else if (instr->dest.ssa.bit_size == 1 && tid.regClass() == v1) {
            assert(src.regClass() == bld.lm);
            Temp tmp;
            if (ctx->program->chip_class <= GFX7)
               tmp = bld.vop3(aco_opcode::v_lshr_b64, bld.def(v2), src, tid);
            else if (ctx->program->wave_size == 64)
               tmp = bld.vop3(aco_opcode::v_lshrrev_b64, bld.def(v2), tid, src);
            else
               tmp = bld.vop2_e64(aco_opcode::v_lshrrev_b32, bld.def(v1), tid, src);
            tmp = emit_extract_vector(ctx, tmp, 0, v1);
            tmp = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(1u), tmp);
            emit_wqm(bld, bld.vopc(aco_opcode::v_cmp_lg_u32, bld.def(bld.lm), Operand::zero(), tmp),
                     dst);
         } else {
            isel_err(&instr->instr, "Unimplemented NIR instr bit size");
         }
      }
      break;
   }
   case nir_intrinsic_load_sample_id: {
      bld.vop3(aco_opcode::v_bfe_u32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               get_arg(ctx, ctx->args->ac.ancillary), Operand::c32(8u), Operand::c32(4u));
      break;
   }
   case nir_intrinsic_load_sample_mask_in: {
      visit_load_sample_mask_in(ctx, instr);
      break;
   }
   case nir_intrinsic_read_first_invocation: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      if (src.regClass() == v1b || src.regClass() == v2b || src.regClass() == v1) {
         emit_wqm(bld, bld.vop1(aco_opcode::v_readfirstlane_b32, bld.def(s1), src), dst);
      } else if (src.regClass() == v2) {
         Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
         lo = emit_wqm(bld, bld.vop1(aco_opcode::v_readfirstlane_b32, bld.def(s1), lo));
         hi = emit_wqm(bld, bld.vop1(aco_opcode::v_readfirstlane_b32, bld.def(s1), hi));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
         emit_split_vector(ctx, dst, 2);
      } else if (instr->dest.ssa.bit_size == 1) {
         assert(src.regClass() == bld.lm);
         Temp tmp = bld.sopc(Builder::s_bitcmp1, bld.def(s1, scc), src,
                             bld.sop1(Builder::s_ff1_i32, bld.def(s1), Operand(exec, bld.lm)));
         bool_to_vector_condition(ctx, emit_wqm(bld, tmp), dst);
      } else {
         bld.copy(Definition(dst), src);
      }
      break;
   }
   case nir_intrinsic_vote_all: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      assert(src.regClass() == bld.lm);
      assert(dst.regClass() == bld.lm);

      Temp tmp =
         bld.sop2(Builder::s_andn2, bld.def(bld.lm), bld.def(s1, scc), Operand(exec, bld.lm), src)
            .def(1)
            .getTemp();
      Temp cond = bool_to_vector_condition(ctx, emit_wqm(bld, tmp));
      bld.sop1(Builder::s_not, Definition(dst), bld.def(s1, scc), cond);
      break;
   }
   case nir_intrinsic_vote_any: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      assert(src.regClass() == bld.lm);
      assert(dst.regClass() == bld.lm);

      Temp tmp = bool_to_scalar_condition(ctx, src);
      bool_to_vector_condition(ctx, emit_wqm(bld, tmp), dst);
      break;
   }
   case nir_intrinsic_reduce:
   case nir_intrinsic_inclusive_scan:
   case nir_intrinsic_exclusive_scan: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      nir_op op = (nir_op)nir_intrinsic_reduction_op(instr);
      unsigned cluster_size =
         instr->intrinsic == nir_intrinsic_reduce ? nir_intrinsic_cluster_size(instr) : 0;
      cluster_size = util_next_power_of_two(
         MIN2(cluster_size ? cluster_size : ctx->program->wave_size, ctx->program->wave_size));

      if (!nir_src_is_divergent(instr->src[0]) && cluster_size == ctx->program->wave_size &&
          instr->dest.ssa.bit_size != 1) {
         /* We use divergence analysis to assign the regclass, so check if it's
          * working as expected */
         ASSERTED bool expected_divergent = instr->intrinsic == nir_intrinsic_exclusive_scan;
         if (instr->intrinsic == nir_intrinsic_inclusive_scan)
            expected_divergent = op == nir_op_iadd || op == nir_op_fadd || op == nir_op_ixor;
         assert(nir_dest_is_divergent(instr->dest) == expected_divergent);

         if (instr->intrinsic == nir_intrinsic_reduce) {
            if (emit_uniform_reduce(ctx, instr))
               break;
         } else if (emit_uniform_scan(ctx, instr)) {
            break;
         }
      }

      if (instr->dest.ssa.bit_size == 1) {
         if (op == nir_op_imul || op == nir_op_umin || op == nir_op_imin)
            op = nir_op_iand;
         else if (op == nir_op_iadd)
            op = nir_op_ixor;
         else if (op == nir_op_umax || op == nir_op_imax)
            op = nir_op_ior;
         assert(op == nir_op_iand || op == nir_op_ior || op == nir_op_ixor);

         switch (instr->intrinsic) {
         case nir_intrinsic_reduce:
            emit_wqm(bld, emit_boolean_reduce(ctx, op, cluster_size, src), dst);
            break;
         case nir_intrinsic_exclusive_scan:
            emit_wqm(bld, emit_boolean_exclusive_scan(ctx, op, src), dst);
            break;
         case nir_intrinsic_inclusive_scan:
            emit_wqm(bld, emit_boolean_inclusive_scan(ctx, op, src), dst);
            break;
         default: assert(false);
         }
      } else if (cluster_size == 1) {
         bld.copy(Definition(dst), src);
      } else {
         unsigned bit_size = instr->src[0].ssa->bit_size;

         src = emit_extract_vector(ctx, src, 0, RegClass::get(RegType::vgpr, bit_size / 8));

         ReduceOp reduce_op = get_reduce_op(op, bit_size);

         aco_opcode aco_op;
         switch (instr->intrinsic) {
         case nir_intrinsic_reduce: aco_op = aco_opcode::p_reduce; break;
         case nir_intrinsic_inclusive_scan: aco_op = aco_opcode::p_inclusive_scan; break;
         case nir_intrinsic_exclusive_scan: aco_op = aco_opcode::p_exclusive_scan; break;
         default: unreachable("unknown reduce intrinsic");
         }

         Temp tmp_dst = emit_reduction_instr(ctx, aco_op, reduce_op, cluster_size,
                                             bld.def(dst.regClass()), src);
         emit_wqm(bld, tmp_dst, dst);
      }
      break;
   }
   case nir_intrinsic_quad_broadcast: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      if (!nir_dest_is_divergent(instr->dest)) {
         emit_uniform_subgroup(ctx, instr, src);
      } else {
         Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
         unsigned lane = nir_src_as_const_value(instr->src[1])->u32;
         uint32_t dpp_ctrl = dpp_quad_perm(lane, lane, lane, lane);

         if (instr->dest.ssa.bit_size != 1)
            src = as_vgpr(ctx, src);

         if (instr->dest.ssa.bit_size == 1) {
            assert(src.regClass() == bld.lm);
            assert(dst.regClass() == bld.lm);
            uint32_t half_mask = 0x11111111u << lane;
            Temp mask_tmp = bld.pseudo(aco_opcode::p_create_vector, bld.def(s2),
                                       Operand::c32(half_mask), Operand::c32(half_mask));
            Temp tmp = bld.tmp(bld.lm);
            bld.sop1(Builder::s_wqm, Definition(tmp),
                     bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), mask_tmp,
                              bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), src,
                                       Operand(exec, bld.lm))));
            emit_wqm(bld, tmp, dst);
         } else if (instr->dest.ssa.bit_size == 8) {
            Temp tmp = bld.tmp(v1);
            if (ctx->program->chip_class >= GFX8)
               emit_wqm(bld, bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), src, dpp_ctrl), tmp);
            else
               emit_wqm(bld,
                        bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, (1 << 15) | dpp_ctrl),
                        tmp);
            bld.pseudo(aco_opcode::p_split_vector, Definition(dst), bld.def(v3b), tmp);
         } else if (instr->dest.ssa.bit_size == 16) {
            Temp tmp = bld.tmp(v1);
            if (ctx->program->chip_class >= GFX8)
               emit_wqm(bld, bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), src, dpp_ctrl), tmp);
            else
               emit_wqm(bld,
                        bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, (1 << 15) | dpp_ctrl),
                        tmp);
            bld.pseudo(aco_opcode::p_split_vector, Definition(dst), bld.def(v2b), tmp);
         } else if (instr->dest.ssa.bit_size == 32) {
            if (ctx->program->chip_class >= GFX8)
               emit_wqm(bld, bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), src, dpp_ctrl), dst);
            else
               emit_wqm(bld,
                        bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, (1 << 15) | dpp_ctrl),
                        dst);
         } else if (instr->dest.ssa.bit_size == 64) {
            Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
            bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
            if (ctx->program->chip_class >= GFX8) {
               lo = emit_wqm(bld, bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), lo, dpp_ctrl));
               hi = emit_wqm(bld, bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), hi, dpp_ctrl));
            } else {
               lo = emit_wqm(
                  bld, bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), lo, (1 << 15) | dpp_ctrl));
               hi = emit_wqm(
                  bld, bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), hi, (1 << 15) | dpp_ctrl));
            }
            bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
            emit_split_vector(ctx, dst, 2);
         } else {
            isel_err(&instr->instr, "Unimplemented NIR instr bit size");
         }
      }
      break;
   }
   case nir_intrinsic_quad_swap_horizontal:
   case nir_intrinsic_quad_swap_vertical:
   case nir_intrinsic_quad_swap_diagonal:
   case nir_intrinsic_quad_swizzle_amd: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      if (!nir_dest_is_divergent(instr->dest)) {
         emit_uniform_subgroup(ctx, instr, src);
         break;
      }
      uint16_t dpp_ctrl = 0;
      switch (instr->intrinsic) {
      case nir_intrinsic_quad_swap_horizontal: dpp_ctrl = dpp_quad_perm(1, 0, 3, 2); break;
      case nir_intrinsic_quad_swap_vertical: dpp_ctrl = dpp_quad_perm(2, 3, 0, 1); break;
      case nir_intrinsic_quad_swap_diagonal: dpp_ctrl = dpp_quad_perm(3, 2, 1, 0); break;
      case nir_intrinsic_quad_swizzle_amd: dpp_ctrl = nir_intrinsic_swizzle_mask(instr); break;
      default: break;
      }
      if (ctx->program->chip_class < GFX8)
         dpp_ctrl |= (1 << 15);

      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

      if (instr->dest.ssa.bit_size != 1)
         src = as_vgpr(ctx, src);

      if (instr->dest.ssa.bit_size == 1) {
         assert(src.regClass() == bld.lm);
         src = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::zero(),
                            Operand::c32(-1), src);
         if (ctx->program->chip_class >= GFX8)
            src = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), src, dpp_ctrl);
         else
            src = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, dpp_ctrl);
         Temp tmp = bld.vopc(aco_opcode::v_cmp_lg_u32, bld.def(bld.lm), Operand::zero(), src);
         emit_wqm(bld, tmp, dst);
      } else if (instr->dest.ssa.bit_size == 8) {
         Temp tmp = bld.tmp(v1);
         if (ctx->program->chip_class >= GFX8)
            emit_wqm(bld, bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), src, dpp_ctrl), tmp);
         else
            emit_wqm(bld, bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, dpp_ctrl), tmp);
         bld.pseudo(aco_opcode::p_split_vector, Definition(dst), bld.def(v3b), tmp);
      } else if (instr->dest.ssa.bit_size == 16) {
         Temp tmp = bld.tmp(v1);
         if (ctx->program->chip_class >= GFX8)
            emit_wqm(bld, bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), src, dpp_ctrl), tmp);
         else
            emit_wqm(bld, bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, dpp_ctrl), tmp);
         bld.pseudo(aco_opcode::p_split_vector, Definition(dst), bld.def(v2b), tmp);
      } else if (instr->dest.ssa.bit_size == 32) {
         Temp tmp;
         if (ctx->program->chip_class >= GFX8)
            tmp = bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), src, dpp_ctrl);
         else
            tmp = bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), src, dpp_ctrl);
         emit_wqm(bld, tmp, dst);
      } else if (instr->dest.ssa.bit_size == 64) {
         Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
         if (ctx->program->chip_class >= GFX8) {
            lo = emit_wqm(bld, bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), lo, dpp_ctrl));
            hi = emit_wqm(bld, bld.vop1_dpp(aco_opcode::v_mov_b32, bld.def(v1), hi, dpp_ctrl));
         } else {
            lo = emit_wqm(bld, bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), lo, dpp_ctrl));
            hi = emit_wqm(bld, bld.ds(aco_opcode::ds_swizzle_b32, bld.def(v1), hi, dpp_ctrl));
         }
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
         emit_split_vector(ctx, dst, 2);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_intrinsic_masked_swizzle_amd: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      if (!nir_dest_is_divergent(instr->dest)) {
         emit_uniform_subgroup(ctx, instr, src);
         break;
      }
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      uint32_t mask = nir_intrinsic_swizzle_mask(instr);

      if (instr->dest.ssa.bit_size != 1)
         src = as_vgpr(ctx, src);

      if (instr->dest.ssa.bit_size == 1) {
         assert(src.regClass() == bld.lm);
         src = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), Operand::zero(),
                            Operand::c32(-1), src);
         src = emit_masked_swizzle(ctx, bld, src, mask);
         Temp tmp = bld.vopc(aco_opcode::v_cmp_lg_u32, bld.def(bld.lm), Operand::zero(), src);
         emit_wqm(bld, tmp, dst);
      } else if (dst.regClass() == v1b) {
         Temp tmp = emit_wqm(bld, emit_masked_swizzle(ctx, bld, src, mask));
         emit_extract_vector(ctx, tmp, 0, dst);
      } else if (dst.regClass() == v2b) {
         Temp tmp = emit_wqm(bld, emit_masked_swizzle(ctx, bld, src, mask));
         emit_extract_vector(ctx, tmp, 0, dst);
      } else if (dst.regClass() == v1) {
         emit_wqm(bld, emit_masked_swizzle(ctx, bld, src, mask), dst);
      } else if (dst.regClass() == v2) {
         Temp lo = bld.tmp(v1), hi = bld.tmp(v1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(lo), Definition(hi), src);
         lo = emit_wqm(bld, emit_masked_swizzle(ctx, bld, lo, mask));
         hi = emit_wqm(bld, emit_masked_swizzle(ctx, bld, hi, mask));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
         emit_split_vector(ctx, dst, 2);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
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
         emit_wqm(bld, bld.writelane(bld.def(v1), val, lane, src), dst);
      } else if (dst.regClass() == v2) {
         Temp src_lo = bld.tmp(v1), src_hi = bld.tmp(v1);
         Temp val_lo = bld.tmp(s1), val_hi = bld.tmp(s1);
         bld.pseudo(aco_opcode::p_split_vector, Definition(src_lo), Definition(src_hi), src);
         bld.pseudo(aco_opcode::p_split_vector, Definition(val_lo), Definition(val_hi), val);
         Temp lo = emit_wqm(bld, bld.writelane(bld.def(v1), val_lo, lane, src_hi));
         Temp hi = emit_wqm(bld, bld.writelane(bld.def(v1), val_hi, lane, src_hi));
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), lo, hi);
         emit_split_vector(ctx, dst, 2);
      } else {
         isel_err(&instr->instr, "Unimplemented NIR instr bit size");
      }
      break;
   }
   case nir_intrinsic_mbcnt_amd: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp add_src = as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa));
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      /* Fit 64-bit mask for wave32 */
      src = emit_extract_vector(ctx, src, 0, RegClass(src.type(), bld.lm.size()));
      Temp wqm_tmp = emit_mbcnt(ctx, bld.tmp(v1), Operand(src), Operand(add_src));
      emit_wqm(bld, wqm_tmp, dst);
      break;
   }
   case nir_intrinsic_byte_permute_amd: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      assert(dst.regClass() == v1);
      assert(ctx->program->chip_class >= GFX8);
      bld.vop3(aco_opcode::v_perm_b32, Definition(dst), get_ssa_temp(ctx, instr->src[0].ssa),
               as_vgpr(ctx, get_ssa_temp(ctx, instr->src[1].ssa)),
               as_vgpr(ctx, get_ssa_temp(ctx, instr->src[2].ssa)));
      break;
   }
   case nir_intrinsic_lane_permute_16_amd: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      assert(ctx->program->chip_class >= GFX10);

      if (src.regClass() == s1) {
         bld.copy(Definition(dst), src);
      } else if (dst.regClass() == v1 && src.regClass() == v1) {
         bld.vop3(aco_opcode::v_permlane16_b32, Definition(dst), src,
                  bld.as_uniform(get_ssa_temp(ctx, instr->src[1].ssa)),
                  bld.as_uniform(get_ssa_temp(ctx, instr->src[2].ssa)));
      } else {
         isel_err(&instr->instr, "Unimplemented lane_permute_16_amd");
      }
      break;
   }
   case nir_intrinsic_load_helper_invocation:
   case nir_intrinsic_is_helper_invocation: {
      /* load_helper() after demote() get lowered to is_helper().
       * Otherwise, these two behave the same. */
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.pseudo(aco_opcode::p_is_helper, Definition(dst));
      ctx->block->kind |= block_kind_needs_lowering;
      ctx->program->needs_exact = true;
      break;
   }
   case nir_intrinsic_demote:
      bld.pseudo(aco_opcode::p_demote_to_helper, Operand::c32(-1u));

      if (ctx->block->loop_nest_depth || ctx->cf_info.parent_if.is_divergent)
         ctx->cf_info.exec_potentially_empty_discard = true;
      ctx->block->kind |= block_kind_uses_demote;
      ctx->program->needs_exact = true;
      break;
   case nir_intrinsic_demote_if: {
      Temp src = get_ssa_temp(ctx, instr->src[0].ssa);
      assert(src.regClass() == bld.lm);
      Temp cond =
         bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), src, Operand(exec, bld.lm));
      bld.pseudo(aco_opcode::p_demote_to_helper, cond);

      if (ctx->block->loop_nest_depth || ctx->cf_info.parent_if.is_divergent)
         ctx->cf_info.exec_potentially_empty_discard = true;
      ctx->block->kind |= block_kind_uses_demote;
      ctx->program->needs_exact = true;
      break;
   }
   case nir_intrinsic_first_invocation: {
      emit_wqm(bld, bld.sop1(Builder::s_ff1_i32, bld.def(s1), Operand(exec, bld.lm)),
               get_ssa_temp(ctx, &instr->dest.ssa));
      break;
   }
   case nir_intrinsic_last_invocation: {
      Temp flbit = bld.sop1(Builder::s_flbit_i32, bld.def(s1), Operand(exec, bld.lm));
      Temp last = bld.sop2(aco_opcode::s_sub_i32, bld.def(s1), bld.def(s1, scc),
                           Operand::c32(ctx->program->wave_size - 1u), flbit);
      emit_wqm(bld, last, get_ssa_temp(ctx, &instr->dest.ssa));
      break;
   }
   case nir_intrinsic_elect: {
      /* p_elect is lowered in aco_insert_exec_mask.
       * Use exec as an operand so value numbering and the pre-RA optimizer won't recognize
       * two p_elect with different exec masks as the same.
       */
      Temp elected = bld.pseudo(aco_opcode::p_elect, bld.def(bld.lm), Operand(exec, bld.lm));
      emit_wqm(bld, elected, get_ssa_temp(ctx, &instr->dest.ssa));
      ctx->block->kind |= block_kind_needs_lowering;
      break;
   }
   case nir_intrinsic_shader_clock: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      if (nir_intrinsic_memory_scope(instr) == NIR_SCOPE_SUBGROUP &&
          ctx->options->chip_class >= GFX10_3) {
         /* "((size - 1) << 11) | register" (SHADER_CYCLES is encoded as register 29) */
         Temp clock = bld.sopk(aco_opcode::s_getreg_b32, bld.def(s1), ((20 - 1) << 11) | 29);
         bld.pseudo(aco_opcode::p_create_vector, Definition(dst), clock, Operand::zero());
      } else {
         aco_opcode opcode = nir_intrinsic_memory_scope(instr) == NIR_SCOPE_DEVICE
                                ? aco_opcode::s_memrealtime
                                : aco_opcode::s_memtime;
         bld.smem(opcode, Definition(dst), memory_sync_info(0, semantic_volatile));
      }
      emit_split_vector(ctx, dst, 2);
      break;
   }
   case nir_intrinsic_load_vertex_id_zero_base: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.copy(Definition(dst), get_arg(ctx, ctx->args->ac.vertex_id));
      break;
   }
   case nir_intrinsic_load_first_vertex: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.copy(Definition(dst), get_arg(ctx, ctx->args->ac.base_vertex));
      break;
   }
   case nir_intrinsic_load_base_instance: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.copy(Definition(dst), get_arg(ctx, ctx->args->ac.start_instance));
      break;
   }
   case nir_intrinsic_load_instance_id: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.copy(Definition(dst), get_arg(ctx, ctx->args->ac.instance_id));
      break;
   }
   case nir_intrinsic_load_draw_id: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.copy(Definition(dst), get_arg(ctx, ctx->args->ac.draw_id));
      break;
   }
   case nir_intrinsic_load_invocation_id: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

      if (ctx->shader->info.stage == MESA_SHADER_GEOMETRY) {
         if (ctx->options->chip_class >= GFX10)
            bld.vop2_e64(aco_opcode::v_and_b32, Definition(dst), Operand::c32(127u),
                         get_arg(ctx, ctx->args->ac.gs_invocation_id));
         else
            bld.copy(Definition(dst), get_arg(ctx, ctx->args->ac.gs_invocation_id));
      } else if (ctx->shader->info.stage == MESA_SHADER_TESS_CTRL) {
         bld.vop3(aco_opcode::v_bfe_u32, Definition(dst), get_arg(ctx, ctx->args->ac.tcs_rel_ids),
                  Operand::c32(8u), Operand::c32(5u));
      } else {
         unreachable("Unsupported stage for load_invocation_id");
      }

      break;
   }
   case nir_intrinsic_load_primitive_id: {
      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);

      switch (ctx->shader->info.stage) {
      case MESA_SHADER_GEOMETRY:
         bld.copy(Definition(dst), get_arg(ctx, ctx->args->ac.gs_prim_id));
         break;
      case MESA_SHADER_TESS_CTRL:
         bld.copy(Definition(dst), get_arg(ctx, ctx->args->ac.tcs_patch_id));
         break;
      case MESA_SHADER_TESS_EVAL:
         bld.copy(Definition(dst), get_arg(ctx, ctx->args->ac.tes_patch_id));
         break;
      default:
         if (ctx->stage.hw == HWStage::NGG && !ctx->stage.has(SWStage::GS)) {
            /* In case of NGG, the GS threads always have the primitive ID
             * even if there is no SW GS. */
            bld.copy(Definition(dst), get_arg(ctx, ctx->args->ac.gs_prim_id));
            break;
         }
         unreachable("Unimplemented shader stage for nir_intrinsic_load_primitive_id");
      }

      break;
   }
   case nir_intrinsic_load_patch_vertices_in: {
      assert(ctx->shader->info.stage == MESA_SHADER_TESS_CTRL ||
             ctx->shader->info.stage == MESA_SHADER_TESS_EVAL);

      Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
      bld.copy(Definition(dst), Operand::c32(ctx->args->options->key.tcs.input_vertices));
      break;
   }
   case nir_intrinsic_emit_vertex_with_counter: {
      assert(ctx->stage.hw == HWStage::GS);
      visit_emit_vertex_with_counter(ctx, instr);
      break;
   }
   case nir_intrinsic_end_primitive_with_counter: {
      if (ctx->stage.hw != HWStage::NGG) {
         unsigned stream = nir_intrinsic_stream_id(instr);
         bld.sopp(aco_opcode::s_sendmsg, bld.m0(ctx->gs_wave_id), -1,
                  sendmsg_gs(true, false, stream));
      }
      break;
   }
   case nir_intrinsic_set_vertex_and_primitive_count: {
      assert(ctx->stage.hw == HWStage::GS);
      /* unused in the legacy pipeline, the HW keeps track of this for us */
      break;
   }
   case nir_intrinsic_load_tess_rel_patch_id_amd: {
      bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)), get_tess_rel_patch_id(ctx));
      break;
   }
   case nir_intrinsic_load_ring_tess_factors_amd: {
      bld.smem(aco_opcode::s_load_dwordx4, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               ctx->program->private_segment_buffer, Operand::c32(RING_HS_TESS_FACTOR * 16u));
      break;
   }
   case nir_intrinsic_load_ring_tess_factors_offset_amd: {
      bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               get_arg(ctx, ctx->args->ac.tcs_factor_offset));
      break;
   }
   case nir_intrinsic_load_ring_tess_offchip_amd: {
      bld.smem(aco_opcode::s_load_dwordx4, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               ctx->program->private_segment_buffer, Operand::c32(RING_HS_TESS_OFFCHIP * 16u));
      break;
   }
   case nir_intrinsic_load_ring_tess_offchip_offset_amd: {
      bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               get_arg(ctx, ctx->args->ac.tess_offchip_offset));
      break;
   }
   case nir_intrinsic_load_ring_esgs_amd: {
      unsigned ring = ctx->stage.hw == HWStage::ES ? RING_ESGS_VS : RING_ESGS_GS;
      bld.smem(aco_opcode::s_load_dwordx4, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               ctx->program->private_segment_buffer, Operand::c32(ring * 16u));
      break;
   }
   case nir_intrinsic_load_ring_es2gs_offset_amd: {
      bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               get_arg(ctx, ctx->args->ac.es2gs_offset));
      break;
   }
   case nir_intrinsic_load_gs_vertex_offset_amd: {
      /* GFX6-8 uses 6 separate args, while GFX9+ packs these into only 3 args. */
      unsigned b = nir_intrinsic_base(instr);
      assert(b <= (ctx->program->chip_class >= GFX9 ? 2 : 5));
      bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               get_arg(ctx, ctx->args->ac.gs_vtx_offset[b]));
      break;
   }
   case nir_intrinsic_has_input_vertex_amd:
   case nir_intrinsic_has_input_primitive_amd: {
      assert(ctx->stage.hw == HWStage::NGG);
      unsigned i = instr->intrinsic == nir_intrinsic_has_input_vertex_amd ? 0 : 1;
      bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)), merged_wave_info_to_mask(ctx, i));
      break;
   }
   case nir_intrinsic_load_workgroup_num_input_vertices_amd:
   case nir_intrinsic_load_workgroup_num_input_primitives_amd: {
      assert(ctx->stage.hw == HWStage::NGG);
      unsigned pos =
         instr->intrinsic == nir_intrinsic_load_workgroup_num_input_vertices_amd ? 12 : 22;
      bld.sop2(aco_opcode::s_bfe_u32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               bld.def(s1, scc), get_arg(ctx, ctx->args->ac.gs_tg_info),
               Operand::c32(pos | (9u << 16u)));
      break;
   }
   case nir_intrinsic_load_initial_edgeflags_amd: {
      assert(ctx->stage.hw == HWStage::NGG);

      Temp gs_invocation_id = get_arg(ctx, ctx->args->ac.gs_invocation_id);
      /* Get initial edgeflags for each vertex at bits 8, 9, 10 of gs_invocation_id. */
      Temp flags = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(0x700u), gs_invocation_id);
      /* Move the bits to their desired position: 8->9, 9->19, 10->29. */
      flags = bld.vop2(aco_opcode::v_mul_u32_u24, bld.def(v1), Operand::c32(0x80402u), flags);
      /* Remove garbage bits that are a byproduct of the multiplication. */
      bld.vop2(aco_opcode::v_and_b32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)), Operand::c32(0x20080200), flags);
      break;
   }
   case nir_intrinsic_load_packed_passthrough_primitive_amd: {
      bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               get_arg(ctx, ctx->args->ac.gs_vtx_offset[0]));
      break;
   }
   case nir_intrinsic_export_vertex_amd: {
      ctx->block->kind |= block_kind_export_end;
      create_vs_exports(ctx);
      break;
   }
   case nir_intrinsic_export_primitive_amd: {
      assert(ctx->stage.hw == HWStage::NGG);
      Temp prim_exp_arg = get_ssa_temp(ctx, instr->src[0].ssa);
      bld.exp(aco_opcode::exp, prim_exp_arg, Operand(v1), Operand(v1), Operand(v1),
              1 /* enabled mask */, V_008DFC_SQ_EXP_PRIM /* dest */, false /* compressed */,
              true /* done */, false /* valid mask */);
      break;
   }
   case nir_intrinsic_alloc_vertices_and_primitives_amd: {
      assert(ctx->stage.hw == HWStage::NGG);
      Temp num_vertices = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp num_primitives = get_ssa_temp(ctx, instr->src[1].ssa);
      ngg_emit_sendmsg_gs_alloc_req(ctx, num_vertices, num_primitives);
      break;
   }
   case nir_intrinsic_gds_atomic_add_amd: {
      Temp store_val = get_ssa_temp(ctx, instr->src[0].ssa);
      Temp gds_addr = get_ssa_temp(ctx, instr->src[1].ssa);
      Temp m0_val = get_ssa_temp(ctx, instr->src[2].ssa);
      Operand m = bld.m0((Temp)bld.copy(bld.def(s1, m0), bld.as_uniform(m0_val)));
      bld.ds(aco_opcode::ds_add_u32, as_vgpr(ctx, gds_addr), as_vgpr(ctx, store_val), m, 0u, 0u,
             true);
      break;
   }
   case nir_intrinsic_load_shader_query_enabled_amd: {
      unsigned cmp_bit = 0;
      Temp shader_query_enabled =
         bld.sopc(aco_opcode::s_bitcmp1_b32, bld.def(s1, scc),
                  get_arg(ctx, ctx->args->ngg_gs_state), Operand::c32(cmp_bit));
      bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               bool_to_vector_condition(ctx, shader_query_enabled));
      break;
   }
   case nir_intrinsic_load_cull_front_face_enabled_amd:
   case nir_intrinsic_load_cull_back_face_enabled_amd:
   case nir_intrinsic_load_cull_ccw_amd:
   case nir_intrinsic_load_cull_small_primitives_enabled_amd: {
      unsigned cmp_bit;
      if (instr->intrinsic == nir_intrinsic_load_cull_front_face_enabled_amd)
         cmp_bit = 0;
      else if (instr->intrinsic == nir_intrinsic_load_cull_back_face_enabled_amd)
         cmp_bit = 1;
      else if (instr->intrinsic == nir_intrinsic_load_cull_ccw_amd)
         cmp_bit = 2;
      else if (instr->intrinsic == nir_intrinsic_load_cull_small_primitives_enabled_amd)
         cmp_bit = 3;
      else
         unreachable("unimplemented culling intrinsic");

      Builder::Result enabled =
         bld.sopc(aco_opcode::s_bitcmp1_b32, bld.def(s1, scc),
                  get_arg(ctx, ctx->args->ngg_culling_settings), Operand::c32(cmp_bit));
      enabled.instr->definitions[0].setNoCSE(true);
      bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               bool_to_vector_condition(ctx, enabled));
      break;
   }
   case nir_intrinsic_load_sbt_amd: visit_load_sbt_amd(ctx, instr); break;
   case nir_intrinsic_bvh64_intersect_ray_amd: visit_bvh64_intersect_ray_amd(ctx, instr); break;
   case nir_intrinsic_load_cull_any_enabled_amd: {
      Builder::Result cull_any_enabled =
         bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc),
                  get_arg(ctx, ctx->args->ngg_culling_settings), Operand::c32(0x00ffffffu));
      cull_any_enabled.instr->definitions[1].setNoCSE(true);
      bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               bool_to_vector_condition(ctx, cull_any_enabled.def(1).getTemp()));
      break;
   }
   case nir_intrinsic_load_cull_small_prim_precision_amd: {
      /* Exponent is 8-bit signed int, move that into a signed 32-bit int. */
      Temp exponent = bld.sop2(aco_opcode::s_ashr_i32, bld.def(s1), bld.def(s1, scc),
                               get_arg(ctx, ctx->args->ngg_gs_state), Operand::c32(24u));
      /* small_prim_precision = 1.0 * 2^X */
      bld.vop3(aco_opcode::v_ldexp_f32, Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               Operand::c32(0x3f800000u), Operand(exponent));
      break;
   }
   case nir_intrinsic_load_viewport_x_scale: {
      bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               get_arg(ctx, ctx->args->ngg_viewport_scale[0]));
      break;
   }
   case nir_intrinsic_load_viewport_y_scale: {
      bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               get_arg(ctx, ctx->args->ngg_viewport_scale[1]));
      break;
   }
   case nir_intrinsic_load_viewport_x_offset: {
      bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               get_arg(ctx, ctx->args->ngg_viewport_translate[0]));
      break;
   }
   case nir_intrinsic_load_viewport_y_offset: {
      bld.copy(Definition(get_ssa_temp(ctx, &instr->dest.ssa)),
               get_arg(ctx, ctx->args->ngg_viewport_translate[1]));
      break;
   }
   case nir_intrinsic_overwrite_vs_arguments_amd: {
      ctx->arg_temps[ctx->args->ac.vertex_id.arg_index] = get_ssa_temp(ctx, instr->src[0].ssa);
      ctx->arg_temps[ctx->args->ac.instance_id.arg_index] = get_ssa_temp(ctx, instr->src[1].ssa);
      break;
   }
   case nir_intrinsic_overwrite_tes_arguments_amd: {
      ctx->arg_temps[ctx->args->ac.tes_u.arg_index] = get_ssa_temp(ctx, instr->src[0].ssa);
      ctx->arg_temps[ctx->args->ac.tes_v.arg_index] = get_ssa_temp(ctx, instr->src[1].ssa);
      ctx->arg_temps[ctx->args->ac.tes_rel_patch_id.arg_index] =
         get_ssa_temp(ctx, instr->src[2].ssa);
      ctx->arg_temps[ctx->args->ac.tes_patch_id.arg_index] = get_ssa_temp(ctx, instr->src[3].ssa);
      break;
   }
   default:
      isel_err(&instr->instr, "Unimplemented intrinsic instr");
      abort();

      break;
   }
}

void
tex_fetch_ptrs(isel_context* ctx, nir_tex_instr* instr, Temp* res_ptr, Temp* samp_ptr,
               Temp* fmask_ptr, enum glsl_base_type* stype)
{
   nir_deref_instr* texture_deref_instr = NULL;
   nir_deref_instr* sampler_deref_instr = NULL;
   int plane = -1;

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      switch (instr->src[i].src_type) {
      case nir_tex_src_texture_deref:
         texture_deref_instr = nir_src_as_deref(instr->src[i].src);
         break;
      case nir_tex_src_sampler_deref:
         sampler_deref_instr = nir_src_as_deref(instr->src[i].src);
         break;
      case nir_tex_src_plane: plane = nir_src_as_int(instr->src[i].src); break;
      default: break;
      }
   }

   *stype = glsl_get_sampler_result_type(texture_deref_instr->type);

   if (!sampler_deref_instr)
      sampler_deref_instr = texture_deref_instr;

   if (plane >= 0) {
      assert(instr->op != nir_texop_txf_ms && instr->op != nir_texop_samples_identical);
      assert(instr->sampler_dim != GLSL_SAMPLER_DIM_BUF);
      *res_ptr = get_sampler_desc(ctx, texture_deref_instr,
                                  (aco_descriptor_type)(ACO_DESC_PLANE_0 + plane), instr, false);
   } else if (instr->sampler_dim == GLSL_SAMPLER_DIM_BUF) {
      *res_ptr = get_sampler_desc(ctx, texture_deref_instr, ACO_DESC_BUFFER, instr, false);
   } else if (instr->op == nir_texop_fragment_mask_fetch) {
      *res_ptr = get_sampler_desc(ctx, texture_deref_instr, ACO_DESC_FMASK, instr, false);
   } else {
      *res_ptr = get_sampler_desc(ctx, texture_deref_instr, ACO_DESC_IMAGE, instr, false);
   }
   if (samp_ptr) {
      *samp_ptr = get_sampler_desc(ctx, sampler_deref_instr, ACO_DESC_SAMPLER, instr, false);

      if (instr->sampler_dim < GLSL_SAMPLER_DIM_RECT && ctx->options->chip_class < GFX8) {
         /* fix sampler aniso on SI/CI: samp[0] = samp[0] & img[7] */
         Builder bld(ctx->program, ctx->block);

         /* to avoid unnecessary moves, we split and recombine sampler and image */
         Temp img[8] = {bld.tmp(s1), bld.tmp(s1), bld.tmp(s1), bld.tmp(s1),
                        bld.tmp(s1), bld.tmp(s1), bld.tmp(s1), bld.tmp(s1)};
         Temp samp[4] = {bld.tmp(s1), bld.tmp(s1), bld.tmp(s1), bld.tmp(s1)};
         bld.pseudo(aco_opcode::p_split_vector, Definition(img[0]), Definition(img[1]),
                    Definition(img[2]), Definition(img[3]), Definition(img[4]), Definition(img[5]),
                    Definition(img[6]), Definition(img[7]), *res_ptr);
         bld.pseudo(aco_opcode::p_split_vector, Definition(samp[0]), Definition(samp[1]),
                    Definition(samp[2]), Definition(samp[3]), *samp_ptr);

         samp[0] = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), samp[0], img[7]);
         *res_ptr = bld.pseudo(aco_opcode::p_create_vector, bld.def(s8), img[0], img[1], img[2],
                               img[3], img[4], img[5], img[6], img[7]);
         *samp_ptr = bld.pseudo(aco_opcode::p_create_vector, bld.def(s4), samp[0], samp[1], samp[2],
                                samp[3]);
      }
   }
   if (fmask_ptr && (instr->op == nir_texop_txf_ms || instr->op == nir_texop_samples_identical))
      *fmask_ptr = get_sampler_desc(ctx, texture_deref_instr, ACO_DESC_FMASK, instr, false);
}

void
build_cube_select(isel_context* ctx, Temp ma, Temp id, Temp deriv, Temp* out_ma, Temp* out_sc,
                  Temp* out_tc)
{
   Builder bld(ctx->program, ctx->block);

   Temp deriv_x = emit_extract_vector(ctx, deriv, 0, v1);
   Temp deriv_y = emit_extract_vector(ctx, deriv, 1, v1);
   Temp deriv_z = emit_extract_vector(ctx, deriv, 2, v1);

   Operand neg_one = Operand::c32(0xbf800000u);
   Operand one = Operand::c32(0x3f800000u);
   Operand two = Operand::c32(0x40000000u);
   Operand four = Operand::c32(0x40800000u);

   Temp is_ma_positive =
      bld.vopc(aco_opcode::v_cmp_le_f32, bld.hint_vcc(bld.def(bld.lm)), Operand::zero(), ma);
   Temp sgn_ma = bld.vop2_e64(aco_opcode::v_cndmask_b32, bld.def(v1), neg_one, one, is_ma_positive);
   Temp neg_sgn_ma = bld.vop2(aco_opcode::v_sub_f32, bld.def(v1), Operand::zero(), sgn_ma);

   Temp is_ma_z = bld.vopc(aco_opcode::v_cmp_le_f32, bld.hint_vcc(bld.def(bld.lm)), four, id);
   Temp is_ma_y = bld.vopc(aco_opcode::v_cmp_le_f32, bld.def(bld.lm), two, id);
   is_ma_y = bld.sop2(Builder::s_andn2, bld.hint_vcc(bld.def(bld.lm)), is_ma_y, is_ma_z);
   Temp is_not_ma_x = bld.sop2(aco_opcode::s_or_b64, bld.hint_vcc(bld.def(bld.lm)),
                               bld.def(s1, scc), is_ma_z, is_ma_y);

   /* select sc */
   Temp tmp = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), deriv_z, deriv_x, is_not_ma_x);
   Temp sgn = bld.vop2_e64(
      aco_opcode::v_cndmask_b32, bld.def(v1),
      bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), neg_sgn_ma, sgn_ma, is_ma_z), one, is_ma_y);
   *out_sc = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), tmp, sgn);

   /* select tc */
   tmp = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), deriv_y, deriv_z, is_ma_y);
   sgn = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), neg_one, sgn_ma, is_ma_y);
   *out_tc = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), tmp, sgn);

   /* select ma */
   tmp = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1),
                  bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), deriv_x, deriv_y, is_ma_y),
                  deriv_z, is_ma_z);
   tmp = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(0x7fffffffu), tmp);
   *out_ma = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), two, tmp);
}

void
prepare_cube_coords(isel_context* ctx, std::vector<Temp>& coords, Temp* ddx, Temp* ddy,
                    bool is_deriv, bool is_array)
{
   Builder bld(ctx->program, ctx->block);
   Temp ma, tc, sc, id;
   aco_opcode madak =
      ctx->program->chip_class >= GFX10_3 ? aco_opcode::v_fmaak_f32 : aco_opcode::v_madak_f32;
   aco_opcode madmk =
      ctx->program->chip_class >= GFX10_3 ? aco_opcode::v_fmamk_f32 : aco_opcode::v_madmk_f32;

   if (is_array) {
      coords[3] = bld.vop1(aco_opcode::v_rndne_f32, bld.def(v1), coords[3]);

      /* see comment in ac_prepare_cube_coords() */
      if (ctx->options->chip_class <= GFX8)
         coords[3] = bld.vop2(aco_opcode::v_max_f32, bld.def(v1), Operand::zero(), coords[3]);
   }

   ma = bld.vop3(aco_opcode::v_cubema_f32, bld.def(v1), coords[0], coords[1], coords[2]);

   aco_ptr<VOP3_instruction> vop3a{
      create_instruction<VOP3_instruction>(aco_opcode::v_rcp_f32, asVOP3(Format::VOP1), 1, 1)};
   vop3a->operands[0] = Operand(ma);
   vop3a->abs[0] = true;
   Temp invma = bld.tmp(v1);
   vop3a->definitions[0] = Definition(invma);
   ctx->block->instructions.emplace_back(std::move(vop3a));

   sc = bld.vop3(aco_opcode::v_cubesc_f32, bld.def(v1), coords[0], coords[1], coords[2]);
   if (!is_deriv)
      sc = bld.vop2(madak, bld.def(v1), sc, invma, Operand::c32(0x3fc00000u /*1.5*/));

   tc = bld.vop3(aco_opcode::v_cubetc_f32, bld.def(v1), coords[0], coords[1], coords[2]);
   if (!is_deriv)
      tc = bld.vop2(madak, bld.def(v1), tc, invma, Operand::c32(0x3fc00000u /*1.5*/));

   id = bld.vop3(aco_opcode::v_cubeid_f32, bld.def(v1), coords[0], coords[1], coords[2]);

   if (is_deriv) {
      sc = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), sc, invma);
      tc = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), tc, invma);

      for (unsigned i = 0; i < 2; i++) {
         /* see comment in ac_prepare_cube_coords() */
         Temp deriv_ma;
         Temp deriv_sc, deriv_tc;
         build_cube_select(ctx, ma, id, i ? *ddy : *ddx, &deriv_ma, &deriv_sc, &deriv_tc);

         deriv_ma = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), deriv_ma, invma);

         Temp x = bld.vop2(aco_opcode::v_sub_f32, bld.def(v1),
                           bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), deriv_sc, invma),
                           bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), deriv_ma, sc));
         Temp y = bld.vop2(aco_opcode::v_sub_f32, bld.def(v1),
                           bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), deriv_tc, invma),
                           bld.vop2(aco_opcode::v_mul_f32, bld.def(v1), deriv_ma, tc));
         *(i ? ddy : ddx) = bld.pseudo(aco_opcode::p_create_vector, bld.def(v2), x, y);
      }

      sc = bld.vop2(aco_opcode::v_add_f32, bld.def(v1), Operand::c32(0x3fc00000u /*1.5*/), sc);
      tc = bld.vop2(aco_opcode::v_add_f32, bld.def(v1), Operand::c32(0x3fc00000u /*1.5*/), tc);
   }

   if (is_array)
      id = bld.vop2(madmk, bld.def(v1), coords[3], id, Operand::c32(0x41000000u /*8.0*/));
   coords.resize(3);
   coords[0] = sc;
   coords[1] = tc;
   coords[2] = id;
}

void
get_const_vec(nir_ssa_def* vec, nir_const_value* cv[4])
{
   if (vec->parent_instr->type != nir_instr_type_alu)
      return;
   nir_alu_instr* vec_instr = nir_instr_as_alu(vec->parent_instr);
   if (vec_instr->op != nir_op_vec(vec->num_components))
      return;

   for (unsigned i = 0; i < vec->num_components; i++) {
      cv[i] =
         vec_instr->src[i].swizzle[0] == 0 ? nir_src_as_const_value(vec_instr->src[i].src) : NULL;
   }
}

void
visit_tex(isel_context* ctx, nir_tex_instr* instr)
{
   Builder bld(ctx->program, ctx->block);
   bool has_bias = false, has_lod = false, level_zero = false, has_compare = false,
        has_offset = false, has_ddx = false, has_ddy = false, has_derivs = false,
        has_sample_index = false, has_clamped_lod = false;
   Temp resource, sampler, fmask_ptr, bias = Temp(), compare = Temp(), sample_index = Temp(),
                                      lod = Temp(), offset = Temp(), ddx = Temp(), ddy = Temp(),
                                      clamped_lod = Temp();
   std::vector<Temp> coords;
   std::vector<Temp> derivs;
   nir_const_value* sample_index_cv = NULL;
   nir_const_value* const_offset[4] = {NULL, NULL, NULL, NULL};
   enum glsl_base_type stype;
   tex_fetch_ptrs(ctx, instr, &resource, &sampler, &fmask_ptr, &stype);

   bool tg4_integer_workarounds = ctx->options->chip_class <= GFX8 && instr->op == nir_texop_tg4 &&
                                  (stype == GLSL_TYPE_UINT || stype == GLSL_TYPE_INT);
   bool tg4_integer_cube_workaround =
      tg4_integer_workarounds && instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE;

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      switch (instr->src[i].src_type) {
      case nir_tex_src_coord: {
         Temp coord = get_ssa_temp(ctx, instr->src[i].src.ssa);
         for (unsigned j = 0; j < coord.size(); j++)
            coords.emplace_back(emit_extract_vector(ctx, coord, j, v1));
         break;
      }
      case nir_tex_src_bias:
         bias = get_ssa_temp(ctx, instr->src[i].src.ssa);
         has_bias = true;
         break;
      case nir_tex_src_lod: {
         if (nir_src_is_const(instr->src[i].src) && nir_src_as_uint(instr->src[i].src) == 0) {
            level_zero = true;
         } else {
            lod = get_ssa_temp(ctx, instr->src[i].src.ssa);
            has_lod = true;
         }
         break;
      }
      case nir_tex_src_min_lod:
         clamped_lod = get_ssa_temp(ctx, instr->src[i].src.ssa);
         has_clamped_lod = true;
         break;
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
      default: break;
      }
   }

   if (instr->op == nir_texop_txs && instr->sampler_dim == GLSL_SAMPLER_DIM_BUF)
      return get_buffer_size(ctx, resource, get_ssa_temp(ctx, &instr->dest.ssa));

   if (instr->op == nir_texop_texture_samples) {
      get_image_samples(ctx, Definition(get_ssa_temp(ctx, &instr->dest.ssa)), resource);
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
            acc = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), acc,
                           Operand::c32(0x3Fu));

            if (i) {
               acc = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), acc,
                              Operand::c32(8u * i));
            }

            if (pack == Temp()) {
               pack = acc;
            } else {
               pack = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), pack, acc);
            }
         }

         if (pack_const && pack != Temp())
            pack = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc),
                            Operand::c32(pack_const), pack);
      } else {
         for (unsigned i = 0; i < offset.size(); i++) {
            if (const_offset[i])
               continue;

            acc = emit_extract_vector(ctx, offset, i, v1);
            acc = bld.vop2(aco_opcode::v_and_b32, bld.def(v1), Operand::c32(0x3Fu), acc);

            if (i) {
               acc = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(8u * i), acc);
            }

            if (pack == Temp()) {
               pack = acc;
            } else {
               pack = bld.vop2(aco_opcode::v_or_b32, bld.def(v1), pack, acc);
            }
         }

         if (pack_const && pack != Temp())
            pack = bld.sop2(aco_opcode::v_or_b32, bld.def(v1), Operand::c32(pack_const), pack);
      }
      if (pack_const && pack == Temp())
         offset = bld.copy(bld.def(v1), Operand::c32(pack_const));
      else if (pack == Temp())
         has_offset = false;
      else
         offset = pack;
   }

   if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE && instr->coord_components)
      prepare_cube_coords(ctx, coords, &ddx, &ddy, instr->op == nir_texop_txd,
                          instr->is_array && instr->op != nir_texop_lod);

   /* pack derivatives */
   if (has_ddx || has_ddy) {
      if (instr->sampler_dim == GLSL_SAMPLER_DIM_1D && ctx->options->chip_class == GFX9) {
         assert(has_ddx && has_ddy && ddx.size() == 1 && ddy.size() == 1);
         Temp zero = bld.copy(bld.def(v1), Operand::zero());
         derivs = {ddx, zero, ddy, zero};
      } else {
         for (unsigned i = 0; has_ddx && i < ddx.size(); i++)
            derivs.emplace_back(emit_extract_vector(ctx, ddx, i, v1));
         for (unsigned i = 0; has_ddy && i < ddy.size(); i++)
            derivs.emplace_back(emit_extract_vector(ctx, ddy, i, v1));
      }
      has_derivs = true;
   }

   if (instr->coord_components > 1 && instr->sampler_dim == GLSL_SAMPLER_DIM_1D &&
       instr->is_array && instr->op != nir_texop_txf)
      coords[1] = bld.vop1(aco_opcode::v_rndne_f32, bld.def(v1), coords[1]);

   if (instr->coord_components > 2 &&
       (instr->sampler_dim == GLSL_SAMPLER_DIM_2D || instr->sampler_dim == GLSL_SAMPLER_DIM_MS ||
        instr->sampler_dim == GLSL_SAMPLER_DIM_SUBPASS ||
        instr->sampler_dim == GLSL_SAMPLER_DIM_SUBPASS_MS) &&
       instr->is_array && instr->op != nir_texop_txf && instr->op != nir_texop_txf_ms &&
       instr->op != nir_texop_fragment_fetch && instr->op != nir_texop_fragment_mask_fetch)
      coords[2] = bld.vop1(aco_opcode::v_rndne_f32, bld.def(v1), coords[2]);

   if (ctx->options->chip_class == GFX9 && instr->sampler_dim == GLSL_SAMPLER_DIM_1D &&
       instr->op != nir_texop_lod && instr->coord_components) {
      assert(coords.size() > 0 && coords.size() < 3);

      coords.insert(std::next(coords.begin()),
                    bld.copy(bld.def(v1), instr->op == nir_texop_txf ? Operand::c32(0)
                                                                     : Operand::c32(0x3f000000)));
   }

   bool da = should_declare_array(ctx, instr->sampler_dim, instr->is_array);

   if (instr->op == nir_texop_samples_identical)
      resource = fmask_ptr;

   else if ((instr->sampler_dim == GLSL_SAMPLER_DIM_MS ||
             instr->sampler_dim == GLSL_SAMPLER_DIM_SUBPASS_MS) &&
            instr->op != nir_texop_txs && instr->op != nir_texop_fragment_fetch &&
            instr->op != nir_texop_fragment_mask_fetch) {
      assert(has_sample_index);
      Operand op(sample_index);
      if (sample_index_cv)
         op = Operand::c32(sample_index_cv->u32);
      sample_index = adjust_sample_index_using_fmask(ctx, da, coords, op, fmask_ptr);
   }

   if (has_offset && (instr->op == nir_texop_txf || instr->op == nir_texop_txf_ms)) {
      for (unsigned i = 0; i < std::min(offset.size(), instr->coord_components); i++) {
         Temp off = emit_extract_vector(ctx, offset, i, v1);
         coords[i] = bld.vadd32(bld.def(v1), coords[i], off);
      }
      has_offset = false;
   }

   /* Build tex instruction */
   unsigned dmask = nir_ssa_def_components_read(&instr->dest.ssa) & 0xf;
   if (instr->sampler_dim == GLSL_SAMPLER_DIM_BUF)
      dmask = u_bit_consecutive(0, util_last_bit(dmask));
   if (instr->is_sparse)
      dmask = MAX2(dmask, 1) | 0x10;
   unsigned dim =
      ctx->options->chip_class >= GFX10 && instr->sampler_dim != GLSL_SAMPLER_DIM_BUF
         ? ac_get_sampler_dim(ctx->options->chip_class, instr->sampler_dim, instr->is_array)
         : 0;
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   Temp tmp_dst = dst;

   /* gather4 selects the component by dmask and always returns vec4 (vec5 if sparse) */
   if (instr->op == nir_texop_tg4) {
      assert(instr->dest.ssa.num_components == (4 + instr->is_sparse));
      if (instr->is_shadow)
         dmask = 1;
      else
         dmask = 1 << instr->component;
      if (tg4_integer_cube_workaround || dst.type() == RegType::sgpr)
         tmp_dst = bld.tmp(instr->is_sparse ? v5 : v4);
   } else if (instr->op == nir_texop_samples_identical) {
      tmp_dst = bld.tmp(v1);
   } else if (util_bitcount(dmask) != instr->dest.ssa.num_components ||
              dst.type() == RegType::sgpr) {
      tmp_dst = bld.tmp(RegClass(RegType::vgpr, util_bitcount(dmask)));
   }

   if (instr->op == nir_texop_txs || instr->op == nir_texop_query_levels) {
      if (!has_lod)
         lod = bld.copy(bld.def(v1), Operand::zero());

      MIMG_instruction* tex = emit_mimg(bld, aco_opcode::image_get_resinfo, Definition(tmp_dst),
                                        resource, Operand(s4), std::vector<Temp>{lod});
      if (ctx->options->chip_class == GFX9 && instr->op == nir_texop_txs &&
          instr->sampler_dim == GLSL_SAMPLER_DIM_1D && instr->is_array) {
         tex->dmask = (dmask & 0x1) | ((dmask & 0x2) << 1);
      } else if (instr->op == nir_texop_query_levels) {
         tex->dmask = 1 << 3;
      } else {
         tex->dmask = dmask;
      }
      tex->da = da;
      tex->dim = dim;

      expand_vector(ctx, tmp_dst, dst, instr->dest.ssa.num_components, dmask);
      return;
   }

   Temp tg4_compare_cube_wa64 = Temp();

   if (tg4_integer_workarounds) {
      Temp tg4_lod = bld.copy(bld.def(v1), Operand::zero());
      Temp size = bld.tmp(v2);
      MIMG_instruction* tex = emit_mimg(bld, aco_opcode::image_get_resinfo, Definition(size),
                                        resource, Operand(s4), std::vector<Temp>{tg4_lod});
      tex->dim = dim;
      tex->dmask = 0x3;
      tex->da = da;
      emit_split_vector(ctx, size, size.size());

      Temp half_texel[2];
      for (unsigned i = 0; i < 2; i++) {
         half_texel[i] = emit_extract_vector(ctx, size, i, v1);
         half_texel[i] = bld.vop1(aco_opcode::v_cvt_f32_i32, bld.def(v1), half_texel[i]);
         half_texel[i] = bld.vop1(aco_opcode::v_rcp_iflag_f32, bld.def(v1), half_texel[i]);
         half_texel[i] = bld.vop2(aco_opcode::v_mul_f32, bld.def(v1),
                                  Operand::c32(0xbf000000 /*-0.5*/), half_texel[i]);
      }

      if (instr->sampler_dim == GLSL_SAMPLER_DIM_2D && !instr->is_array) {
         /* In vulkan, whether the sampler uses unnormalized
          * coordinates or not is a dynamic property of the
          * sampler. Hence, to figure out whether or not we
          * need to divide by the texture size, we need to test
          * the sampler at runtime. This tests the bit set by
          * radv_init_sampler().
          */
         unsigned bit_idx = ffs(S_008F30_FORCE_UNNORMALIZED(1)) - 1;
         Temp not_needed =
            bld.sopc(aco_opcode::s_bitcmp0_b32, bld.def(s1, scc), sampler, Operand::c32(bit_idx));

         not_needed = bool_to_vector_condition(ctx, not_needed);
         half_texel[0] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1),
                                  Operand::c32(0xbf000000 /*-0.5*/), half_texel[0], not_needed);
         half_texel[1] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1),
                                  Operand::c32(0xbf000000 /*-0.5*/), half_texel[1], not_needed);
      }

      Temp new_coords[2] = {bld.vop2(aco_opcode::v_add_f32, bld.def(v1), coords[0], half_texel[0]),
                            bld.vop2(aco_opcode::v_add_f32, bld.def(v1), coords[1], half_texel[1])};

      if (tg4_integer_cube_workaround) {
         /* see comment in ac_nir_to_llvm.c's lower_gather4_integer() */
         Temp* const desc = (Temp*)alloca(resource.size() * sizeof(Temp));
         aco_ptr<Instruction> split{create_instruction<Pseudo_instruction>(
            aco_opcode::p_split_vector, Format::PSEUDO, 1, resource.size())};
         split->operands[0] = Operand(resource);
         for (unsigned i = 0; i < resource.size(); i++) {
            desc[i] = bld.tmp(s1);
            split->definitions[i] = Definition(desc[i]);
         }
         ctx->block->instructions.emplace_back(std::move(split));

         Temp dfmt = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc), desc[1],
                              Operand::c32(20u | (6u << 16)));
         Temp compare_cube_wa = bld.sopc(aco_opcode::s_cmp_eq_u32, bld.def(s1, scc), dfmt,
                                         Operand::c32(V_008F14_IMG_DATA_FORMAT_8_8_8_8));

         Temp nfmt;
         if (stype == GLSL_TYPE_UINT) {
            nfmt = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1),
                            Operand::c32(V_008F14_IMG_NUM_FORMAT_USCALED),
                            Operand::c32(V_008F14_IMG_NUM_FORMAT_UINT), bld.scc(compare_cube_wa));
         } else {
            nfmt = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1),
                            Operand::c32(V_008F14_IMG_NUM_FORMAT_SSCALED),
                            Operand::c32(V_008F14_IMG_NUM_FORMAT_SINT), bld.scc(compare_cube_wa));
         }
         tg4_compare_cube_wa64 = bld.tmp(bld.lm);
         bool_to_vector_condition(ctx, compare_cube_wa, tg4_compare_cube_wa64);

         nfmt = bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), nfmt,
                         Operand::c32(26u));

         desc[1] = bld.sop2(aco_opcode::s_and_b32, bld.def(s1), bld.def(s1, scc), desc[1],
                            Operand::c32(C_008F14_NUM_FORMAT));
         desc[1] = bld.sop2(aco_opcode::s_or_b32, bld.def(s1), bld.def(s1, scc), desc[1], nfmt);

         aco_ptr<Instruction> vec{create_instruction<Pseudo_instruction>(
            aco_opcode::p_create_vector, Format::PSEUDO, resource.size(), 1)};
         for (unsigned i = 0; i < resource.size(); i++)
            vec->operands[i] = Operand(desc[i]);
         resource = bld.tmp(resource.regClass());
         vec->definitions[0] = Definition(resource);
         ctx->block->instructions.emplace_back(std::move(vec));

         new_coords[0] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), new_coords[0], coords[0],
                                  tg4_compare_cube_wa64);
         new_coords[1] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), new_coords[1], coords[1],
                                  tg4_compare_cube_wa64);
      }
      coords[0] = new_coords[0];
      coords[1] = new_coords[1];
   }

   if (instr->sampler_dim == GLSL_SAMPLER_DIM_BUF) {
      // FIXME: if (ctx->abi->gfx9_stride_size_workaround) return
      // ac_build_buffer_load_format_gfx9_safe()

      assert(coords.size() == 1);
      aco_opcode op;
      switch (util_last_bit(dmask & 0xf)) {
      case 1: op = aco_opcode::buffer_load_format_x; break;
      case 2: op = aco_opcode::buffer_load_format_xy; break;
      case 3: op = aco_opcode::buffer_load_format_xyz; break;
      case 4: op = aco_opcode::buffer_load_format_xyzw; break;
      default: unreachable("Tex instruction loads more than 4 components.");
      }

      aco_ptr<MUBUF_instruction> mubuf{
         create_instruction<MUBUF_instruction>(op, Format::MUBUF, 3 + instr->is_sparse, 1)};
      mubuf->operands[0] = Operand(resource);
      mubuf->operands[1] = Operand(coords[0]);
      mubuf->operands[2] = Operand::c32(0);
      mubuf->definitions[0] = Definition(tmp_dst);
      mubuf->idxen = true;
      mubuf->tfe = instr->is_sparse;
      if (mubuf->tfe)
         mubuf->operands[3] = emit_tfe_init(bld, tmp_dst);
      ctx->block->instructions.emplace_back(std::move(mubuf));

      expand_vector(ctx, tmp_dst, dst, instr->dest.ssa.num_components, dmask);
      return;
   }

   /* gather MIMG address components */
   std::vector<Temp> args;
   unsigned wqm_mask = 0;
   if (has_offset) {
      wqm_mask |= u_bit_consecutive(args.size(), 1);
      args.emplace_back(offset);
   }
   if (has_bias)
      args.emplace_back(bias);
   if (has_compare)
      args.emplace_back(compare);
   if (has_derivs)
      args.insert(args.end(), derivs.begin(), derivs.end());

   wqm_mask |= u_bit_consecutive(args.size(), coords.size());
   args.insert(args.end(), coords.begin(), coords.end());

   if (has_sample_index)
      args.emplace_back(sample_index);
   if (has_lod)
      args.emplace_back(lod);
   if (has_clamped_lod)
      args.emplace_back(clamped_lod);

   if (instr->op == nir_texop_txf || instr->op == nir_texop_txf_ms ||
       instr->op == nir_texop_samples_identical || instr->op == nir_texop_fragment_fetch ||
       instr->op == nir_texop_fragment_mask_fetch) {
      aco_opcode op = level_zero || instr->sampler_dim == GLSL_SAMPLER_DIM_MS ||
                            instr->sampler_dim == GLSL_SAMPLER_DIM_SUBPASS_MS
                         ? aco_opcode::image_load
                         : aco_opcode::image_load_mip;
      Operand vdata = instr->is_sparse ? emit_tfe_init(bld, tmp_dst) : Operand(v1);
      MIMG_instruction* tex =
         emit_mimg(bld, op, Definition(tmp_dst), resource, Operand(s4), args, 0, vdata);
      tex->dim = dim;
      tex->dmask = dmask & 0xf;
      tex->unrm = true;
      tex->da = da;
      tex->tfe = instr->is_sparse;

      if (instr->op == nir_texop_samples_identical) {
         assert(dmask == 1 && dst.regClass() == bld.lm);
         assert(dst.id() != tmp_dst.id());

         bld.vopc(aco_opcode::v_cmp_eq_u32, Definition(dst), Operand::zero(), tmp_dst)
            .def(0)
            .setHint(vcc);
      } else {
         expand_vector(ctx, tmp_dst, dst, instr->dest.ssa.num_components, dmask);
      }
      return;
   }

   // TODO: would be better to do this by adding offsets, but needs the opcodes ordered.
   aco_opcode opcode = aco_opcode::image_sample;
   if (has_offset) { /* image_sample_*_o */
      if (has_clamped_lod) {
         if (has_compare) {
            opcode = aco_opcode::image_sample_c_cl_o;
            if (has_derivs)
               opcode = aco_opcode::image_sample_c_d_cl_o;
            if (has_bias)
               opcode = aco_opcode::image_sample_c_b_cl_o;
         } else {
            opcode = aco_opcode::image_sample_cl_o;
            if (has_derivs)
               opcode = aco_opcode::image_sample_d_cl_o;
            if (has_bias)
               opcode = aco_opcode::image_sample_b_cl_o;
         }
      } else if (has_compare) {
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
   } else if (has_clamped_lod) { /* image_sample_*_cl */
      if (has_compare) {
         opcode = aco_opcode::image_sample_c_cl;
         if (has_derivs)
            opcode = aco_opcode::image_sample_c_d_cl;
         if (has_bias)
            opcode = aco_opcode::image_sample_c_b_cl;
      } else {
         opcode = aco_opcode::image_sample_cl;
         if (has_derivs)
            opcode = aco_opcode::image_sample_d_cl;
         if (has_bias)
            opcode = aco_opcode::image_sample_b_cl;
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
      if (has_offset) { /* image_gather4_*_o */
         if (has_compare) {
            opcode = aco_opcode::image_gather4_c_lz_o;
            if (has_lod)
               opcode = aco_opcode::image_gather4_c_l_o;
            if (has_bias)
               opcode = aco_opcode::image_gather4_c_b_o;
         } else {
            opcode = aco_opcode::image_gather4_lz_o;
            if (has_lod)
               opcode = aco_opcode::image_gather4_l_o;
            if (has_bias)
               opcode = aco_opcode::image_gather4_b_o;
         }
      } else {
         if (has_compare) {
            opcode = aco_opcode::image_gather4_c_lz;
            if (has_lod)
               opcode = aco_opcode::image_gather4_c_l;
            if (has_bias)
               opcode = aco_opcode::image_gather4_c_b;
         } else {
            opcode = aco_opcode::image_gather4_lz;
            if (has_lod)
               opcode = aco_opcode::image_gather4_l;
            if (has_bias)
               opcode = aco_opcode::image_gather4_b;
         }
      }
   } else if (instr->op == nir_texop_lod) {
      opcode = aco_opcode::image_get_lod;
   }

   bool implicit_derivs = bld.program->stage == fragment_fs && !has_derivs && !has_lod &&
                          !level_zero && instr->sampler_dim != GLSL_SAMPLER_DIM_MS &&
                          instr->sampler_dim != GLSL_SAMPLER_DIM_SUBPASS_MS;

   Operand vdata = instr->is_sparse ? emit_tfe_init(bld, tmp_dst) : Operand(v1);
   MIMG_instruction* tex = emit_mimg(bld, opcode, Definition(tmp_dst), resource, Operand(sampler),
                                     args, implicit_derivs ? wqm_mask : 0, vdata);
   tex->dim = dim;
   tex->dmask = dmask & 0xf;
   tex->da = da;
   tex->tfe = instr->is_sparse;

   if (tg4_integer_cube_workaround) {
      assert(tmp_dst.id() != dst.id());
      assert(tmp_dst.size() == dst.size());

      emit_split_vector(ctx, tmp_dst, tmp_dst.size());
      Temp val[4];
      for (unsigned i = 0; i < 4; i++) {
         val[i] = emit_extract_vector(ctx, tmp_dst, i, v1);
         Temp cvt_val;
         if (stype == GLSL_TYPE_UINT)
            cvt_val = bld.vop1(aco_opcode::v_cvt_u32_f32, bld.def(v1), val[i]);
         else
            cvt_val = bld.vop1(aco_opcode::v_cvt_i32_f32, bld.def(v1), val[i]);
         val[i] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), val[i], cvt_val,
                           tg4_compare_cube_wa64);
      }

      Temp tmp = dst.regClass() == tmp_dst.regClass() ? dst : bld.tmp(tmp_dst.regClass());
      if (instr->is_sparse)
         tmp_dst = bld.pseudo(aco_opcode::p_create_vector, Definition(tmp), val[0], val[1], val[2],
                              val[3], emit_extract_vector(ctx, tmp_dst, 4, v1));
      else
         tmp_dst = bld.pseudo(aco_opcode::p_create_vector, Definition(tmp), val[0], val[1], val[2],
                              val[3]);
   }
   unsigned mask = instr->op == nir_texop_tg4 ? (instr->is_sparse ? 0x1F : 0xF) : dmask;
   expand_vector(ctx, tmp_dst, dst, instr->dest.ssa.num_components, mask);
}

Operand
get_phi_operand(isel_context* ctx, nir_ssa_def* ssa, RegClass rc, bool logical)
{
   Temp tmp = get_ssa_temp(ctx, ssa);
   if (ssa->parent_instr->type == nir_instr_type_ssa_undef) {
      return Operand(rc);
   } else if (logical && ssa->bit_size == 1 &&
              ssa->parent_instr->type == nir_instr_type_load_const) {
      if (ctx->program->wave_size == 64)
         return Operand::c64(nir_instr_as_load_const(ssa->parent_instr)->value[0].b ? UINT64_MAX
                                                                                    : 0u);
      else
         return Operand::c32(nir_instr_as_load_const(ssa->parent_instr)->value[0].b ? UINT32_MAX
                                                                                    : 0u);
   } else {
      return Operand(tmp);
   }
}

void
visit_phi(isel_context* ctx, nir_phi_instr* instr)
{
   aco_ptr<Pseudo_instruction> phi;
   Temp dst = get_ssa_temp(ctx, &instr->dest.ssa);
   assert(instr->dest.ssa.bit_size != 1 || dst.regClass() == ctx->program->lane_mask);

   bool logical = !dst.is_linear() || nir_dest_is_divergent(instr->dest);
   logical |= (ctx->block->kind & block_kind_merge) != 0;
   aco_opcode opcode = logical ? aco_opcode::p_phi : aco_opcode::p_linear_phi;

   /* we want a sorted list of sources, since the predecessor list is also sorted */
   std::map<unsigned, nir_ssa_def*> phi_src;
   nir_foreach_phi_src (src, instr)
      phi_src[src->pred->index] = src->src.ssa;

   std::vector<unsigned>& preds = logical ? ctx->block->logical_preds : ctx->block->linear_preds;
   unsigned num_operands = 0;
   Operand* const operands = (Operand*)alloca(
      (std::max(exec_list_length(&instr->srcs), (unsigned)preds.size()) + 1) * sizeof(Operand));
   unsigned num_defined = 0;
   unsigned cur_pred_idx = 0;
   for (std::pair<unsigned, nir_ssa_def*> src : phi_src) {
      if (cur_pred_idx < preds.size()) {
         /* handle missing preds (IF merges with discard/break) and extra preds
          * (loop exit with discard) */
         unsigned block = ctx->cf_info.nir_to_aco[src.first];
         unsigned skipped = 0;
         while (cur_pred_idx + skipped < preds.size() && preds[cur_pred_idx + skipped] != block)
            skipped++;
         if (cur_pred_idx + skipped < preds.size()) {
            for (unsigned i = 0; i < skipped; i++)
               operands[num_operands++] = Operand(dst.regClass());
            cur_pred_idx += skipped;
         } else {
            continue;
         }
      }
      /* Handle missing predecessors at the end. This shouldn't happen with loop
       * headers and we can't ignore these sources for loop header phis. */
      if (!(ctx->block->kind & block_kind_loop_header) && cur_pred_idx >= preds.size())
         continue;
      cur_pred_idx++;
      Operand op = get_phi_operand(ctx, src.second, dst.regClass(), logical);
      operands[num_operands++] = op;
      num_defined += !op.isUndefined();
   }
   /* handle block_kind_continue_or_break at loop exit blocks */
   while (cur_pred_idx++ < preds.size())
      operands[num_operands++] = Operand(dst.regClass());

   /* If the loop ends with a break, still add a linear continue edge in case
    * that break is divergent or continue_or_break is used. We'll either remove
    * this operand later in visit_loop() if it's not necessary or replace the
    * undef with something correct. */
   if (!logical && ctx->block->kind & block_kind_loop_header) {
      nir_loop* loop = nir_cf_node_as_loop(instr->instr.block->cf_node.parent);
      nir_block* last = nir_loop_last_block(loop);
      if (last->successors[0] != instr->instr.block)
         operands[num_operands++] = Operand(RegClass());
   }

   /* we can use a linear phi in some cases if one src is undef */
   if (dst.is_linear() && ctx->block->kind & block_kind_merge && num_defined == 1) {
      phi.reset(create_instruction<Pseudo_instruction>(aco_opcode::p_linear_phi, Format::PSEUDO,
                                                       num_operands, 1));

      Block* linear_else = &ctx->program->blocks[ctx->block->linear_preds[1]];
      Block* invert = &ctx->program->blocks[linear_else->linear_preds[0]];
      assert(invert->kind & block_kind_invert);

      unsigned then_block = invert->linear_preds[0];

      Block* insert_block = NULL;
      for (unsigned i = 0; i < num_operands; i++) {
         Operand op = operands[i];
         if (op.isUndefined())
            continue;
         insert_block = ctx->block->logical_preds[i] == then_block ? invert : ctx->block;
         phi->operands[0] = op;
         break;
      }
      assert(insert_block); /* should be handled by the "num_defined == 0" case above */
      phi->operands[1] = Operand(dst.regClass());
      phi->definitions[0] = Definition(dst);
      insert_block->instructions.emplace(insert_block->instructions.begin(), std::move(phi));
      return;
   }

   phi.reset(create_instruction<Pseudo_instruction>(opcode, Format::PSEUDO, num_operands, 1));
   for (unsigned i = 0; i < num_operands; i++)
      phi->operands[i] = operands[i];
   phi->definitions[0] = Definition(dst);
   ctx->block->instructions.emplace(ctx->block->instructions.begin(), std::move(phi));
}

void
visit_undef(isel_context* ctx, nir_ssa_undef_instr* instr)
{
   Temp dst = get_ssa_temp(ctx, &instr->def);

   assert(dst.type() == RegType::sgpr);

   if (dst.size() == 1) {
      Builder(ctx->program, ctx->block).copy(Definition(dst), Operand::zero());
   } else {
      aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(
         aco_opcode::p_create_vector, Format::PSEUDO, dst.size(), 1)};
      for (unsigned i = 0; i < dst.size(); i++)
         vec->operands[i] = Operand::zero();
      vec->definitions[0] = Definition(dst);
      ctx->block->instructions.emplace_back(std::move(vec));
   }
}

void
begin_loop(isel_context* ctx, loop_context* lc)
{
   // TODO: we might want to wrap the loop around a branch if exec_potentially_empty=true
   append_logical_end(ctx->block);
   ctx->block->kind |= block_kind_loop_preheader | block_kind_uniform;
   Builder bld(ctx->program, ctx->block);
   bld.branch(aco_opcode::p_branch, bld.hint_vcc(bld.def(s2)));
   unsigned loop_preheader_idx = ctx->block->index;

   lc->loop_exit.kind |= (block_kind_loop_exit | (ctx->block->kind & block_kind_top_level));

   ctx->program->next_loop_depth++;

   Block* loop_header = ctx->program->create_and_insert_block();
   loop_header->kind |= block_kind_loop_header;
   add_edge(loop_preheader_idx, loop_header);
   ctx->block = loop_header;

   append_logical_start(ctx->block);

   lc->header_idx_old = std::exchange(ctx->cf_info.parent_loop.header_idx, loop_header->index);
   lc->exit_old = std::exchange(ctx->cf_info.parent_loop.exit, &lc->loop_exit);
   lc->divergent_cont_old = std::exchange(ctx->cf_info.parent_loop.has_divergent_continue, false);
   lc->divergent_branch_old = std::exchange(ctx->cf_info.parent_loop.has_divergent_branch, false);
   lc->divergent_if_old = std::exchange(ctx->cf_info.parent_if.is_divergent, false);
}

void
end_loop(isel_context* ctx, loop_context* lc)
{
   // TODO: what if a loop ends with a unconditional or uniformly branched continue
   //       and this branch is never taken?
   if (!ctx->cf_info.has_branch) {
      unsigned loop_header_idx = ctx->cf_info.parent_loop.header_idx;
      Builder bld(ctx->program, ctx->block);
      append_logical_end(ctx->block);

      if (ctx->cf_info.exec_potentially_empty_discard ||
          ctx->cf_info.exec_potentially_empty_break) {
         /* Discards can result in code running with an empty exec mask.
          * This would result in divergent breaks not ever being taken. As a
          * workaround, break the loop when the loop mask is empty instead of
          * always continuing. */
         ctx->block->kind |= (block_kind_continue_or_break | block_kind_uniform);
         unsigned block_idx = ctx->block->index;

         /* create helper blocks to avoid critical edges */
         Block* break_block = ctx->program->create_and_insert_block();
         break_block->kind = block_kind_uniform;
         bld.reset(break_block);
         bld.branch(aco_opcode::p_branch, bld.hint_vcc(bld.def(s2)));
         add_linear_edge(block_idx, break_block);
         add_linear_edge(break_block->index, &lc->loop_exit);

         Block* continue_block = ctx->program->create_and_insert_block();
         continue_block->kind = block_kind_uniform;
         bld.reset(continue_block);
         bld.branch(aco_opcode::p_branch, bld.hint_vcc(bld.def(s2)));
         add_linear_edge(block_idx, continue_block);
         add_linear_edge(continue_block->index, &ctx->program->blocks[loop_header_idx]);

         if (!ctx->cf_info.parent_loop.has_divergent_branch)
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
      bld.branch(aco_opcode::p_branch, bld.hint_vcc(bld.def(s2)));
   }

   ctx->cf_info.has_branch = false;
   ctx->program->next_loop_depth--;

   // TODO: if the loop has not a single exit, we must add one °°
   /* emit loop successor block */
   ctx->block = ctx->program->insert_block(std::move(lc->loop_exit));
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

   ctx->cf_info.parent_loop.header_idx = lc->header_idx_old;
   ctx->cf_info.parent_loop.exit = lc->exit_old;
   ctx->cf_info.parent_loop.has_divergent_continue = lc->divergent_cont_old;
   ctx->cf_info.parent_loop.has_divergent_branch = lc->divergent_branch_old;
   ctx->cf_info.parent_if.is_divergent = lc->divergent_if_old;
   if (!ctx->block->loop_nest_depth && !ctx->cf_info.parent_if.is_divergent)
      ctx->cf_info.exec_potentially_empty_discard = false;
}

void
emit_loop_jump(isel_context* ctx, bool is_break)
{
   Builder bld(ctx->program, ctx->block);
   Block* logical_target;
   append_logical_end(ctx->block);
   unsigned idx = ctx->block->index;

   if (is_break) {
      logical_target = ctx->cf_info.parent_loop.exit;
      add_logical_edge(idx, logical_target);
      ctx->block->kind |= block_kind_break;

      if (!ctx->cf_info.parent_if.is_divergent &&
          !ctx->cf_info.parent_loop.has_divergent_continue) {
         /* uniform break - directly jump out of the loop */
         ctx->block->kind |= block_kind_uniform;
         ctx->cf_info.has_branch = true;
         bld.branch(aco_opcode::p_branch, bld.hint_vcc(bld.def(s2)));
         add_linear_edge(idx, logical_target);
         return;
      }
      ctx->cf_info.parent_loop.has_divergent_branch = true;
   } else {
      logical_target = &ctx->program->blocks[ctx->cf_info.parent_loop.header_idx];
      add_logical_edge(idx, logical_target);
      ctx->block->kind |= block_kind_continue;

      if (!ctx->cf_info.parent_if.is_divergent) {
         /* uniform continue - directly jump to the loop header */
         ctx->block->kind |= block_kind_uniform;
         ctx->cf_info.has_branch = true;
         bld.branch(aco_opcode::p_branch, bld.hint_vcc(bld.def(s2)));
         add_linear_edge(idx, logical_target);
         return;
      }

      /* for potential uniform breaks after this continue,
         we must ensure that they are handled correctly */
      ctx->cf_info.parent_loop.has_divergent_continue = true;
      ctx->cf_info.parent_loop.has_divergent_branch = true;
   }

   if (ctx->cf_info.parent_if.is_divergent && !ctx->cf_info.exec_potentially_empty_break) {
      ctx->cf_info.exec_potentially_empty_break = true;
      ctx->cf_info.exec_potentially_empty_break_depth = ctx->block->loop_nest_depth;
   }

   /* remove critical edges from linear CFG */
   bld.branch(aco_opcode::p_branch, bld.hint_vcc(bld.def(s2)));
   Block* break_block = ctx->program->create_and_insert_block();
   break_block->kind |= block_kind_uniform;
   add_linear_edge(idx, break_block);
   /* the loop_header pointer might be invalidated by this point */
   if (!is_break)
      logical_target = &ctx->program->blocks[ctx->cf_info.parent_loop.header_idx];
   add_linear_edge(break_block->index, logical_target);
   bld.reset(break_block);
   bld.branch(aco_opcode::p_branch, bld.hint_vcc(bld.def(s2)));

   Block* continue_block = ctx->program->create_and_insert_block();
   add_linear_edge(idx, continue_block);
   append_logical_start(continue_block);
   ctx->block = continue_block;
}

void
emit_loop_break(isel_context* ctx)
{
   emit_loop_jump(ctx, true);
}

void
emit_loop_continue(isel_context* ctx)
{
   emit_loop_jump(ctx, false);
}

void
visit_jump(isel_context* ctx, nir_jump_instr* instr)
{
   /* visit_block() would usually do this but divergent jumps updates ctx->block */
   ctx->cf_info.nir_to_aco[instr->instr.block->index] = ctx->block->index;

   switch (instr->type) {
   case nir_jump_break: emit_loop_break(ctx); break;
   case nir_jump_continue: emit_loop_continue(ctx); break;
   default: isel_err(&instr->instr, "Unknown NIR jump instr"); abort();
   }
}

void
visit_block(isel_context* ctx, nir_block* block)
{
   nir_foreach_instr (instr, block) {
      switch (instr->type) {
      case nir_instr_type_alu: visit_alu_instr(ctx, nir_instr_as_alu(instr)); break;
      case nir_instr_type_load_const: visit_load_const(ctx, nir_instr_as_load_const(instr)); break;
      case nir_instr_type_intrinsic: visit_intrinsic(ctx, nir_instr_as_intrinsic(instr)); break;
      case nir_instr_type_tex: visit_tex(ctx, nir_instr_as_tex(instr)); break;
      case nir_instr_type_phi: visit_phi(ctx, nir_instr_as_phi(instr)); break;
      case nir_instr_type_ssa_undef: visit_undef(ctx, nir_instr_as_ssa_undef(instr)); break;
      case nir_instr_type_deref: break;
      case nir_instr_type_jump: visit_jump(ctx, nir_instr_as_jump(instr)); break;
      default: isel_err(instr, "Unknown NIR instr type");
      }
   }

   if (!ctx->cf_info.parent_loop.has_divergent_branch)
      ctx->cf_info.nir_to_aco[block->index] = ctx->block->index;
}

static Operand
create_continue_phis(isel_context* ctx, unsigned first, unsigned last,
                     aco_ptr<Instruction>& header_phi, Operand* vals)
{
   vals[0] = Operand(header_phi->definitions[0].getTemp());
   RegClass rc = vals[0].regClass();

   unsigned loop_nest_depth = ctx->program->blocks[first].loop_nest_depth;

   unsigned next_pred = 1;

   for (unsigned idx = first + 1; idx <= last; idx++) {
      Block& block = ctx->program->blocks[idx];
      if (block.loop_nest_depth != loop_nest_depth) {
         vals[idx - first] = vals[idx - 1 - first];
         continue;
      }

      if ((block.kind & block_kind_continue) && block.index != last) {
         vals[idx - first] = header_phi->operands[next_pred];
         next_pred++;
         continue;
      }

      bool all_same = true;
      for (unsigned i = 1; all_same && (i < block.linear_preds.size()); i++)
         all_same = vals[block.linear_preds[i] - first] == vals[block.linear_preds[0] - first];

      Operand val;
      if (all_same) {
         val = vals[block.linear_preds[0] - first];
      } else {
         aco_ptr<Instruction> phi(create_instruction<Pseudo_instruction>(
            aco_opcode::p_linear_phi, Format::PSEUDO, block.linear_preds.size(), 1));
         for (unsigned i = 0; i < block.linear_preds.size(); i++)
            phi->operands[i] = vals[block.linear_preds[i] - first];
         val = Operand(ctx->program->allocateTmp(rc));
         phi->definitions[0] = Definition(val.getTemp());
         block.instructions.emplace(block.instructions.begin(), std::move(phi));
      }
      vals[idx - first] = val;
   }

   return vals[last - first];
}

static void begin_uniform_if_then(isel_context* ctx, if_context* ic, Temp cond);
static void begin_uniform_if_else(isel_context* ctx, if_context* ic);
static void end_uniform_if(isel_context* ctx, if_context* ic);

static void
visit_loop(isel_context* ctx, nir_loop* loop)
{
   loop_context lc;
   begin_loop(ctx, &lc);

   /* NIR seems to allow this, and even though the loop exit has no predecessors, SSA defs from the
    * loop header are live. Handle this without complicating the ACO IR by creating a dummy break.
    */
   if (nir_cf_node_cf_tree_next(&loop->cf_node)->predecessors->entries == 0) {
      Builder bld(ctx->program, ctx->block);
      Temp cond = bld.copy(bld.def(s1, scc), Operand::zero());
      if_context ic;
      begin_uniform_if_then(ctx, &ic, cond);
      emit_loop_break(ctx);
      begin_uniform_if_else(ctx, &ic);
      end_uniform_if(ctx, &ic);
   }

   bool unreachable = visit_cf_list(ctx, &loop->body);

   unsigned loop_header_idx = ctx->cf_info.parent_loop.header_idx;

   /* Fixup phis in loop header from unreachable blocks.
    * has_branch/has_divergent_branch also indicates if the loop ends with a
    * break/continue instruction, but we don't emit those if unreachable=true */
   if (unreachable) {
      assert(ctx->cf_info.has_branch || ctx->cf_info.parent_loop.has_divergent_branch);
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

   /* Fixup linear phis in loop header from expecting a continue. Both this fixup
    * and the previous one shouldn't both happen at once because a break in the
    * merge block would get CSE'd */
   if (nir_loop_last_block(loop)->successors[0] != nir_loop_first_block(loop)) {
      unsigned num_vals = ctx->cf_info.has_branch ? 1 : (ctx->block->index - loop_header_idx + 1);
      Operand* const vals = (Operand*)alloca(num_vals * sizeof(Operand));
      for (aco_ptr<Instruction>& instr : ctx->program->blocks[loop_header_idx].instructions) {
         if (instr->opcode == aco_opcode::p_linear_phi) {
            if (ctx->cf_info.has_branch)
               instr->operands.pop_back();
            else
               instr->operands.back() =
                  create_continue_phis(ctx, loop_header_idx, ctx->block->index, instr, vals);
         } else if (!is_phi(instr)) {
            break;
         }
      }
   }

   end_loop(ctx, &lc);
}

static void
begin_divergent_if_then(isel_context* ctx, if_context* ic, Temp cond)
{
   ic->cond = cond;

   append_logical_end(ctx->block);
   ctx->block->kind |= block_kind_branch;

   /* branch to linear then block */
   assert(cond.regClass() == ctx->program->lane_mask);
   aco_ptr<Pseudo_branch_instruction> branch;
   branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_cbranch_z,
                                                              Format::PSEUDO_BRANCH, 1, 1));
   branch->definitions[0] = Definition(ctx->program->allocateTmp(s2));
   branch->definitions[0].setHint(vcc);
   branch->operands[0] = Operand(cond);
   ctx->block->instructions.push_back(std::move(branch));

   ic->BB_if_idx = ctx->block->index;
   ic->BB_invert = Block();
   /* Invert blocks are intentionally not marked as top level because they
    * are not part of the logical cfg. */
   ic->BB_invert.kind |= block_kind_invert;
   ic->BB_endif = Block();
   ic->BB_endif.kind |= (block_kind_merge | (ctx->block->kind & block_kind_top_level));

   ic->exec_potentially_empty_discard_old = ctx->cf_info.exec_potentially_empty_discard;
   ic->exec_potentially_empty_break_old = ctx->cf_info.exec_potentially_empty_break;
   ic->exec_potentially_empty_break_depth_old = ctx->cf_info.exec_potentially_empty_break_depth;
   ic->divergent_old = ctx->cf_info.parent_if.is_divergent;
   ctx->cf_info.parent_if.is_divergent = true;

   /* divergent branches use cbranch_execz */
   ctx->cf_info.exec_potentially_empty_discard = false;
   ctx->cf_info.exec_potentially_empty_break = false;
   ctx->cf_info.exec_potentially_empty_break_depth = UINT16_MAX;

   /** emit logical then block */
   ctx->program->next_divergent_if_logical_depth++;
   Block* BB_then_logical = ctx->program->create_and_insert_block();
   add_edge(ic->BB_if_idx, BB_then_logical);
   ctx->block = BB_then_logical;
   append_logical_start(BB_then_logical);
}

static void
begin_divergent_if_else(isel_context* ctx, if_context* ic)
{
   Block* BB_then_logical = ctx->block;
   append_logical_end(BB_then_logical);
   /* branch from logical then block to invert block */
   aco_ptr<Pseudo_branch_instruction> branch;
   branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_branch,
                                                              Format::PSEUDO_BRANCH, 0, 1));
   branch->definitions[0] = Definition(ctx->program->allocateTmp(s2));
   branch->definitions[0].setHint(vcc);
   BB_then_logical->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_then_logical->index, &ic->BB_invert);
   if (!ctx->cf_info.parent_loop.has_divergent_branch)
      add_logical_edge(BB_then_logical->index, &ic->BB_endif);
   BB_then_logical->kind |= block_kind_uniform;
   assert(!ctx->cf_info.has_branch);
   ic->then_branch_divergent = ctx->cf_info.parent_loop.has_divergent_branch;
   ctx->cf_info.parent_loop.has_divergent_branch = false;
   ctx->program->next_divergent_if_logical_depth--;

   /** emit linear then block */
   Block* BB_then_linear = ctx->program->create_and_insert_block();
   BB_then_linear->kind |= block_kind_uniform;
   add_linear_edge(ic->BB_if_idx, BB_then_linear);
   /* branch from linear then block to invert block */
   branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_branch,
                                                              Format::PSEUDO_BRANCH, 0, 1));
   branch->definitions[0] = Definition(ctx->program->allocateTmp(s2));
   branch->definitions[0].setHint(vcc);
   BB_then_linear->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_then_linear->index, &ic->BB_invert);

   /** emit invert merge block */
   ctx->block = ctx->program->insert_block(std::move(ic->BB_invert));
   ic->invert_idx = ctx->block->index;

   /* branch to linear else block (skip else) */
   branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_branch,
                                                              Format::PSEUDO_BRANCH, 0, 1));
   branch->definitions[0] = Definition(ctx->program->allocateTmp(s2));
   branch->definitions[0].setHint(vcc);
   ctx->block->instructions.push_back(std::move(branch));

   ic->exec_potentially_empty_discard_old |= ctx->cf_info.exec_potentially_empty_discard;
   ic->exec_potentially_empty_break_old |= ctx->cf_info.exec_potentially_empty_break;
   ic->exec_potentially_empty_break_depth_old = std::min(
      ic->exec_potentially_empty_break_depth_old, ctx->cf_info.exec_potentially_empty_break_depth);
   /* divergent branches use cbranch_execz */
   ctx->cf_info.exec_potentially_empty_discard = false;
   ctx->cf_info.exec_potentially_empty_break = false;
   ctx->cf_info.exec_potentially_empty_break_depth = UINT16_MAX;

   /** emit logical else block */
   ctx->program->next_divergent_if_logical_depth++;
   Block* BB_else_logical = ctx->program->create_and_insert_block();
   add_logical_edge(ic->BB_if_idx, BB_else_logical);
   add_linear_edge(ic->invert_idx, BB_else_logical);
   ctx->block = BB_else_logical;
   append_logical_start(BB_else_logical);
}

static void
end_divergent_if(isel_context* ctx, if_context* ic)
{
   Block* BB_else_logical = ctx->block;
   append_logical_end(BB_else_logical);

   /* branch from logical else block to endif block */
   aco_ptr<Pseudo_branch_instruction> branch;
   branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_branch,
                                                              Format::PSEUDO_BRANCH, 0, 1));
   branch->definitions[0] = Definition(ctx->program->allocateTmp(s2));
   branch->definitions[0].setHint(vcc);
   BB_else_logical->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_else_logical->index, &ic->BB_endif);
   if (!ctx->cf_info.parent_loop.has_divergent_branch)
      add_logical_edge(BB_else_logical->index, &ic->BB_endif);
   BB_else_logical->kind |= block_kind_uniform;
   ctx->program->next_divergent_if_logical_depth--;

   assert(!ctx->cf_info.has_branch);
   ctx->cf_info.parent_loop.has_divergent_branch &= ic->then_branch_divergent;

   /** emit linear else block */
   Block* BB_else_linear = ctx->program->create_and_insert_block();
   BB_else_linear->kind |= block_kind_uniform;
   add_linear_edge(ic->invert_idx, BB_else_linear);

   /* branch from linear else block to endif block */
   branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_branch,
                                                              Format::PSEUDO_BRANCH, 0, 1));
   branch->definitions[0] = Definition(ctx->program->allocateTmp(s2));
   branch->definitions[0].setHint(vcc);
   BB_else_linear->instructions.emplace_back(std::move(branch));
   add_linear_edge(BB_else_linear->index, &ic->BB_endif);

   /** emit endif merge block */
   ctx->block = ctx->program->insert_block(std::move(ic->BB_endif));
   append_logical_start(ctx->block);

   ctx->cf_info.parent_if.is_divergent = ic->divergent_old;
   ctx->cf_info.exec_potentially_empty_discard |= ic->exec_potentially_empty_discard_old;
   ctx->cf_info.exec_potentially_empty_break |= ic->exec_potentially_empty_break_old;
   ctx->cf_info.exec_potentially_empty_break_depth = std::min(
      ic->exec_potentially_empty_break_depth_old, ctx->cf_info.exec_potentially_empty_break_depth);
   if (ctx->block->loop_nest_depth == ctx->cf_info.exec_potentially_empty_break_depth &&
       !ctx->cf_info.parent_if.is_divergent) {
      ctx->cf_info.exec_potentially_empty_break = false;
      ctx->cf_info.exec_potentially_empty_break_depth = UINT16_MAX;
   }
   /* uniform control flow never has an empty exec-mask */
   if (!ctx->block->loop_nest_depth && !ctx->cf_info.parent_if.is_divergent) {
      ctx->cf_info.exec_potentially_empty_discard = false;
      ctx->cf_info.exec_potentially_empty_break = false;
      ctx->cf_info.exec_potentially_empty_break_depth = UINT16_MAX;
   }
}

static void
begin_uniform_if_then(isel_context* ctx, if_context* ic, Temp cond)
{
   assert(cond.regClass() == s1);

   append_logical_end(ctx->block);
   ctx->block->kind |= block_kind_uniform;

   aco_ptr<Pseudo_branch_instruction> branch;
   aco_opcode branch_opcode = aco_opcode::p_cbranch_z;
   branch.reset(
      create_instruction<Pseudo_branch_instruction>(branch_opcode, Format::PSEUDO_BRANCH, 1, 1));
   branch->definitions[0] = Definition(ctx->program->allocateTmp(s2));
   branch->definitions[0].setHint(vcc);
   branch->operands[0] = Operand(cond);
   branch->operands[0].setFixed(scc);
   ctx->block->instructions.emplace_back(std::move(branch));

   ic->BB_if_idx = ctx->block->index;
   ic->BB_endif = Block();
   ic->BB_endif.kind |= ctx->block->kind & block_kind_top_level;

   ctx->cf_info.has_branch = false;
   ctx->cf_info.parent_loop.has_divergent_branch = false;

   /** emit then block */
   ctx->program->next_uniform_if_depth++;
   Block* BB_then = ctx->program->create_and_insert_block();
   add_edge(ic->BB_if_idx, BB_then);
   append_logical_start(BB_then);
   ctx->block = BB_then;
}

static void
begin_uniform_if_else(isel_context* ctx, if_context* ic)
{
   Block* BB_then = ctx->block;

   ic->uniform_has_then_branch = ctx->cf_info.has_branch;
   ic->then_branch_divergent = ctx->cf_info.parent_loop.has_divergent_branch;

   if (!ic->uniform_has_then_branch) {
      append_logical_end(BB_then);
      /* branch from then block to endif block */
      aco_ptr<Pseudo_branch_instruction> branch;
      branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_branch,
                                                                 Format::PSEUDO_BRANCH, 0, 1));
      branch->definitions[0] = Definition(ctx->program->allocateTmp(s2));
      branch->definitions[0].setHint(vcc);
      BB_then->instructions.emplace_back(std::move(branch));
      add_linear_edge(BB_then->index, &ic->BB_endif);
      if (!ic->then_branch_divergent)
         add_logical_edge(BB_then->index, &ic->BB_endif);
      BB_then->kind |= block_kind_uniform;
   }

   ctx->cf_info.has_branch = false;
   ctx->cf_info.parent_loop.has_divergent_branch = false;

   /** emit else block */
   Block* BB_else = ctx->program->create_and_insert_block();
   add_edge(ic->BB_if_idx, BB_else);
   append_logical_start(BB_else);
   ctx->block = BB_else;
}

static void
end_uniform_if(isel_context* ctx, if_context* ic)
{
   Block* BB_else = ctx->block;

   if (!ctx->cf_info.has_branch) {
      append_logical_end(BB_else);
      /* branch from then block to endif block */
      aco_ptr<Pseudo_branch_instruction> branch;
      branch.reset(create_instruction<Pseudo_branch_instruction>(aco_opcode::p_branch,
                                                                 Format::PSEUDO_BRANCH, 0, 1));
      branch->definitions[0] = Definition(ctx->program->allocateTmp(s2));
      branch->definitions[0].setHint(vcc);
      BB_else->instructions.emplace_back(std::move(branch));
      add_linear_edge(BB_else->index, &ic->BB_endif);
      if (!ctx->cf_info.parent_loop.has_divergent_branch)
         add_logical_edge(BB_else->index, &ic->BB_endif);
      BB_else->kind |= block_kind_uniform;
   }

   ctx->cf_info.has_branch &= ic->uniform_has_then_branch;
   ctx->cf_info.parent_loop.has_divergent_branch &= ic->then_branch_divergent;

   /** emit endif merge block */
   ctx->program->next_uniform_if_depth--;
   if (!ctx->cf_info.has_branch) {
      ctx->block = ctx->program->insert_block(std::move(ic->BB_endif));
      append_logical_start(ctx->block);
   }
}

static bool
visit_if(isel_context* ctx, nir_if* if_stmt)
{
   Temp cond = get_ssa_temp(ctx, if_stmt->condition.ssa);
   Builder bld(ctx->program, ctx->block);
   aco_ptr<Pseudo_branch_instruction> branch;
   if_context ic;

   if (!nir_src_is_divergent(if_stmt->condition)) { /* uniform condition */
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

      assert(cond.regClass() == ctx->program->lane_mask);
      cond = bool_to_scalar_condition(ctx, cond);

      begin_uniform_if_then(ctx, &ic, cond);
      visit_cf_list(ctx, &if_stmt->then_list);

      begin_uniform_if_else(ctx, &ic);
      visit_cf_list(ctx, &if_stmt->else_list);

      end_uniform_if(ctx, &ic);
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

      begin_divergent_if_then(ctx, &ic, cond);
      visit_cf_list(ctx, &if_stmt->then_list);

      begin_divergent_if_else(ctx, &ic);
      visit_cf_list(ctx, &if_stmt->else_list);

      end_divergent_if(ctx, &ic);
   }

   return !ctx->cf_info.has_branch && !ctx->block->logical_preds.empty();
}

static bool
visit_cf_list(isel_context* ctx, struct exec_list* list)
{
   foreach_list_typed (nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block: visit_block(ctx, nir_cf_node_as_block(node)); break;
      case nir_cf_node_if:
         if (!visit_if(ctx, nir_cf_node_as_if(node)))
            return true;
         break;
      case nir_cf_node_loop: visit_loop(ctx, nir_cf_node_as_loop(node)); break;
      default: unreachable("unimplemented cf list type");
      }
   }
   return false;
}

static void
export_vs_varying(isel_context* ctx, int slot, bool is_pos, int* next_pos)
{
   assert(ctx->stage.hw == HWStage::VS || ctx->stage.hw == HWStage::NGG);

   int offset = (ctx->stage.has(SWStage::TES) && !ctx->stage.has(SWStage::GS))
                   ? ctx->program->info->tes.outinfo.vs_output_param_offset[slot]
                   : ctx->program->info->vs.outinfo.vs_output_param_offset[slot];
   unsigned mask = ctx->outputs.mask[slot];
   if (!is_pos && !mask)
      return;
   if (!is_pos && offset == AC_EXP_PARAM_UNDEFINED)
      return;
   aco_ptr<Export_instruction> exp{
      create_instruction<Export_instruction>(aco_opcode::exp, Format::EXP, 4, 0)};
   exp->enabled_mask = mask;
   for (unsigned i = 0; i < 4; ++i) {
      if (mask & (1 << i))
         exp->operands[i] = Operand(ctx->outputs.temps[slot * 4u + i]);
      else
         exp->operands[i] = Operand(v1);
   }
   /* GFX10 (Navi1x) skip POS0 exports if EXEC=0 and DONE=0, causing a hang.
    * Setting valid_mask=1 prevents it and has no other effect.
    */
   exp->valid_mask = ctx->options->chip_class == GFX10 && is_pos && *next_pos == 0;
   exp->done = false;
   exp->compressed = false;
   if (is_pos)
      exp->dest = V_008DFC_SQ_EXP_POS + (*next_pos)++;
   else
      exp->dest = V_008DFC_SQ_EXP_PARAM + offset;
   ctx->block->instructions.emplace_back(std::move(exp));
}

static void
export_vs_psiz_layer_viewport_vrs(isel_context* ctx, int* next_pos)
{
   aco_ptr<Export_instruction> exp{
      create_instruction<Export_instruction>(aco_opcode::exp, Format::EXP, 4, 0)};
   exp->enabled_mask = 0;
   for (unsigned i = 0; i < 4; ++i)
      exp->operands[i] = Operand(v1);
   if (ctx->outputs.mask[VARYING_SLOT_PSIZ]) {
      exp->operands[0] = Operand(ctx->outputs.temps[VARYING_SLOT_PSIZ * 4u]);
      exp->enabled_mask |= 0x1;
   }
   if (ctx->outputs.mask[VARYING_SLOT_LAYER]) {
      exp->operands[2] = Operand(ctx->outputs.temps[VARYING_SLOT_LAYER * 4u]);
      exp->enabled_mask |= 0x4;
   }
   if (ctx->outputs.mask[VARYING_SLOT_VIEWPORT]) {
      if (ctx->options->chip_class < GFX9) {
         exp->operands[3] = Operand(ctx->outputs.temps[VARYING_SLOT_VIEWPORT * 4u]);
         exp->enabled_mask |= 0x8;
      } else {
         Builder bld(ctx->program, ctx->block);

         Temp out = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(16u),
                             Operand(ctx->outputs.temps[VARYING_SLOT_VIEWPORT * 4u]));
         if (exp->operands[2].isTemp())
            out = bld.vop2(aco_opcode::v_or_b32, bld.def(v1), Operand(out), exp->operands[2]);

         exp->operands[2] = Operand(out);
         exp->enabled_mask |= 0x4;
      }
   }
   if (ctx->outputs.mask[VARYING_SLOT_PRIMITIVE_SHADING_RATE]) {
      exp->operands[1] = Operand(ctx->outputs.temps[VARYING_SLOT_PRIMITIVE_SHADING_RATE * 4u]);
      exp->enabled_mask |= 0x2;
   } else if (ctx->options->force_vrs_rates) {
      /* Bits [2:3] = VRS rate X
       * Bits [4:5] = VRS rate Y
       *
       * The range is [-2, 1]. Values:
       *   1: 2x coarser shading rate in that direction.
       *   0: normal shading rate
       *  -1: 2x finer shading rate (sample shading, not directional)
       *  -2: 4x finer shading rate (sample shading, not directional)
       *
       * Sample shading can't go above 8 samples, so both numbers can't be -2
       * at the same time.
       */
      Builder bld(ctx->program, ctx->block);
      Temp rates = bld.copy(bld.def(v1), Operand::c32((unsigned)ctx->options->force_vrs_rates));

      /* If Pos.W != 1 (typical for non-GUI elements), use 2x2 coarse shading. */
      Temp cond = bld.vopc(aco_opcode::v_cmp_neq_f32, bld.def(bld.lm), Operand::c32(0x3f800000u),
                           Operand(ctx->outputs.temps[VARYING_SLOT_POS + 3]));
      rates = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1),
                       bld.copy(bld.def(v1), Operand::zero()), rates, cond);

      exp->operands[1] = Operand(rates);
      exp->enabled_mask |= 0x2;
   }

   exp->valid_mask = ctx->options->chip_class == GFX10 && *next_pos == 0;
   exp->done = false;
   exp->compressed = false;
   exp->dest = V_008DFC_SQ_EXP_POS + (*next_pos)++;
   ctx->block->instructions.emplace_back(std::move(exp));
}

static void
create_vs_exports(isel_context* ctx)
{
   assert(ctx->stage.hw == HWStage::VS || ctx->stage.hw == HWStage::NGG);

   radv_vs_output_info* outinfo = (ctx->stage.has(SWStage::TES) && !ctx->stage.has(SWStage::GS))
                                     ? &ctx->program->info->tes.outinfo
                                     : &ctx->program->info->vs.outinfo;

   ctx->block->kind |= block_kind_export_end;

   if (outinfo->export_prim_id && ctx->stage.hw != HWStage::NGG) {
      ctx->outputs.mask[VARYING_SLOT_PRIMITIVE_ID] |= 0x1;
      if (ctx->stage.has(SWStage::TES))
         ctx->outputs.temps[VARYING_SLOT_PRIMITIVE_ID * 4u] =
            get_arg(ctx, ctx->args->ac.tes_patch_id);
      else
         ctx->outputs.temps[VARYING_SLOT_PRIMITIVE_ID * 4u] =
            get_arg(ctx, ctx->args->ac.vs_prim_id);
   }

   if (ctx->options->key.has_multiview_view_index) {
      ctx->outputs.mask[VARYING_SLOT_LAYER] |= 0x1;
      ctx->outputs.temps[VARYING_SLOT_LAYER * 4u] =
         as_vgpr(ctx, get_arg(ctx, ctx->args->ac.view_index));
   }

   /* Hardware requires position data to always be exported, even if the
    * application did not write gl_Position.
    */
   ctx->outputs.mask[VARYING_SLOT_POS] = 0xf;

   /* the order these position exports are created is important */
   int next_pos = 0;
   export_vs_varying(ctx, VARYING_SLOT_POS, true, &next_pos);

   bool writes_primitive_shading_rate =
      outinfo->writes_primitive_shading_rate || ctx->options->force_vrs_rates;
   if (outinfo->writes_pointsize || outinfo->writes_layer || outinfo->writes_viewport_index ||
       writes_primitive_shading_rate) {
      export_vs_psiz_layer_viewport_vrs(ctx, &next_pos);
   }
   if (ctx->num_clip_distances + ctx->num_cull_distances > 0)
      export_vs_varying(ctx, VARYING_SLOT_CLIP_DIST0, true, &next_pos);
   if (ctx->num_clip_distances + ctx->num_cull_distances > 4)
      export_vs_varying(ctx, VARYING_SLOT_CLIP_DIST1, true, &next_pos);

   if (ctx->export_clip_dists) {
      if (ctx->num_clip_distances + ctx->num_cull_distances > 0)
         export_vs_varying(ctx, VARYING_SLOT_CLIP_DIST0, false, &next_pos);
      if (ctx->num_clip_distances + ctx->num_cull_distances > 4)
         export_vs_varying(ctx, VARYING_SLOT_CLIP_DIST1, false, &next_pos);
   }

   for (unsigned i = 0; i <= VARYING_SLOT_VAR31; ++i) {
      if (i < VARYING_SLOT_VAR0 && i != VARYING_SLOT_LAYER && i != VARYING_SLOT_PRIMITIVE_ID &&
          i != VARYING_SLOT_VIEWPORT)
         continue;

      export_vs_varying(ctx, i, false, NULL);
   }
}

static bool
export_fs_mrt_z(isel_context* ctx)
{
   Builder bld(ctx->program, ctx->block);
   unsigned enabled_channels = 0;
   bool compr = false;
   Operand values[4];

   for (unsigned i = 0; i < 4; ++i) {
      values[i] = Operand(v1);
   }

   /* Both stencil and sample mask only need 16-bits. */
   if (!ctx->program->info->ps.writes_z &&
       (ctx->program->info->ps.writes_stencil || ctx->program->info->ps.writes_sample_mask)) {
      compr = true; /* COMPR flag */

      if (ctx->program->info->ps.writes_stencil) {
         /* Stencil should be in X[23:16]. */
         values[0] = Operand(ctx->outputs.temps[FRAG_RESULT_STENCIL * 4u]);
         values[0] = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(16u), values[0]);
         enabled_channels |= 0x3;
      }

      if (ctx->program->info->ps.writes_sample_mask) {
         /* SampleMask should be in Y[15:0]. */
         values[1] = Operand(ctx->outputs.temps[FRAG_RESULT_SAMPLE_MASK * 4u]);
         enabled_channels |= 0xc;
      }
   } else {
      if (ctx->program->info->ps.writes_z) {
         values[0] = Operand(ctx->outputs.temps[FRAG_RESULT_DEPTH * 4u]);
         enabled_channels |= 0x1;
      }

      if (ctx->program->info->ps.writes_stencil) {
         values[1] = Operand(ctx->outputs.temps[FRAG_RESULT_STENCIL * 4u]);
         enabled_channels |= 0x2;
      }

      if (ctx->program->info->ps.writes_sample_mask) {
         values[2] = Operand(ctx->outputs.temps[FRAG_RESULT_SAMPLE_MASK * 4u]);
         enabled_channels |= 0x4;
      }
   }

   /* GFX6 (except OLAND and HAINAN) has a bug that it only looks at the X
    * writemask component.
    */
   if (ctx->options->chip_class == GFX6 && ctx->options->family != CHIP_OLAND &&
       ctx->options->family != CHIP_HAINAN) {
      enabled_channels |= 0x1;
   }

   bld.exp(aco_opcode::exp, values[0], values[1], values[2], values[3], enabled_channels,
           V_008DFC_SQ_EXP_MRTZ, compr);

   return true;
}

static bool
export_fs_mrt_color(isel_context* ctx, int slot)
{
   Builder bld(ctx->program, ctx->block);
   unsigned write_mask = ctx->outputs.mask[slot];
   Operand values[4];

   for (unsigned i = 0; i < 4; ++i) {
      if (write_mask & (1 << i)) {
         values[i] = Operand(ctx->outputs.temps[slot * 4u + i]);
      } else {
         values[i] = Operand(v1);
      }
   }

   unsigned target, col_format;
   unsigned enabled_channels = 0;
   aco_opcode compr_op = (aco_opcode)0;
   bool compr = false;

   slot -= FRAG_RESULT_DATA0;
   target = V_008DFC_SQ_EXP_MRT + slot;
   col_format = (ctx->options->key.fs.col_format >> (4 * slot)) & 0xf;

   bool is_int8 = (ctx->options->key.fs.is_int8 >> slot) & 1;
   bool is_int10 = (ctx->options->key.fs.is_int10 >> slot) & 1;
   bool is_16bit = values[0].regClass() == v2b;

   /* Replace NaN by zero (only 32-bit) to fix game bugs if requested. */
   if (ctx->options->enable_mrt_output_nan_fixup && !is_16bit &&
       (col_format == V_028714_SPI_SHADER_32_R || col_format == V_028714_SPI_SHADER_32_GR ||
        col_format == V_028714_SPI_SHADER_32_AR || col_format == V_028714_SPI_SHADER_32_ABGR ||
        col_format == V_028714_SPI_SHADER_FP16_ABGR)) {
      for (int i = 0; i < 4; i++) {
         if (!(write_mask & (1 << i)))
            continue;

         Temp isnan = bld.vopc(aco_opcode::v_cmp_class_f32, bld.hint_vcc(bld.def(bld.lm)),
                               values[i], bld.copy(bld.def(v1), Operand::c32(3u)));
         values[i] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), values[i],
                              bld.copy(bld.def(v1), Operand::zero()), isnan);
      }
   }

   switch (col_format) {
   case V_028714_SPI_SHADER_32_R: enabled_channels = 1; break;

   case V_028714_SPI_SHADER_32_GR: enabled_channels = 0x3; break;

   case V_028714_SPI_SHADER_32_AR:
      if (ctx->options->chip_class >= GFX10) {
         /* Special case: on GFX10, the outputs are different for 32_AR */
         enabled_channels = 0x3;
         values[1] = values[3];
         values[3] = Operand(v1);
      } else {
         enabled_channels = 0x9;
      }
      break;

   case V_028714_SPI_SHADER_FP16_ABGR:
      for (int i = 0; i < 2; i++) {
         bool enabled = (write_mask >> (i * 2)) & 0x3;
         if (enabled) {
            enabled_channels |= 0x3 << (i * 2);
            if (is_16bit) {
               values[i] =
                  bld.pseudo(aco_opcode::p_create_vector, bld.def(v1),
                             values[i * 2].isUndefined() ? Operand(v2b) : values[i * 2],
                             values[i * 2 + 1].isUndefined() ? Operand(v2b) : values[i * 2 + 1]);
            } else if (ctx->options->chip_class == GFX8 || ctx->options->chip_class == GFX9) {
               values[i] =
                  bld.vop3(aco_opcode::v_cvt_pkrtz_f16_f32_e64, bld.def(v1),
                           values[i * 2].isUndefined() ? Operand::zero() : values[i * 2],
                           values[i * 2 + 1].isUndefined() ? Operand::zero() : values[i * 2 + 1]);
            } else {
               values[i] =
                  bld.vop2(aco_opcode::v_cvt_pkrtz_f16_f32, bld.def(v1),
                           values[i * 2].isUndefined() ? values[i * 2 + 1] : values[i * 2],
                           values[i * 2 + 1].isUndefined() ? values[i * 2] : values[i * 2 + 1]);
            }
         } else {
            values[i] = Operand(v1);
         }
      }
      values[2] = Operand(v1);
      values[3] = Operand(v1);
      compr = true;
      break;

   case V_028714_SPI_SHADER_UNORM16_ABGR:
      if (is_16bit && ctx->options->chip_class >= GFX9) {
         compr_op = aco_opcode::v_cvt_pknorm_u16_f16;
      } else {
         compr_op = aco_opcode::v_cvt_pknorm_u16_f32;
      }
      break;

   case V_028714_SPI_SHADER_SNORM16_ABGR:
      if (is_16bit && ctx->options->chip_class >= GFX9) {
         compr_op = aco_opcode::v_cvt_pknorm_i16_f16;
      } else {
         compr_op = aco_opcode::v_cvt_pknorm_i16_f32;
      }
      break;

   case V_028714_SPI_SHADER_UINT16_ABGR: {
      compr_op = aco_opcode::v_cvt_pk_u16_u32;
      if (is_int8 || is_int10) {
         /* clamp */
         uint32_t max_rgb = is_int8 ? 255 : is_int10 ? 1023 : 0;
         Temp max_rgb_val = bld.copy(bld.def(s1), Operand::c32(max_rgb));

         for (unsigned i = 0; i < 4; i++) {
            if ((write_mask >> i) & 1) {
               values[i] =
                  bld.vop2(aco_opcode::v_min_u32, bld.def(v1),
                           i == 3 && is_int10 ? Operand::c32(3u) : Operand(max_rgb_val), values[i]);
            }
         }
      } else if (is_16bit) {
         for (unsigned i = 0; i < 4; i++) {
            if ((write_mask >> i) & 1) {
               Temp tmp = convert_int(ctx, bld, values[i].getTemp(), 16, 32, false);
               values[i] = Operand(tmp);
            }
         }
      }
      break;
   }

   case V_028714_SPI_SHADER_SINT16_ABGR:
      compr_op = aco_opcode::v_cvt_pk_i16_i32;
      if (is_int8 || is_int10) {
         /* clamp */
         uint32_t max_rgb = is_int8 ? 127 : is_int10 ? 511 : 0;
         uint32_t min_rgb = is_int8 ? -128 : is_int10 ? -512 : 0;
         Temp max_rgb_val = bld.copy(bld.def(s1), Operand::c32(max_rgb));
         Temp min_rgb_val = bld.copy(bld.def(s1), Operand::c32(min_rgb));

         for (unsigned i = 0; i < 4; i++) {
            if ((write_mask >> i) & 1) {
               values[i] =
                  bld.vop2(aco_opcode::v_min_i32, bld.def(v1),
                           i == 3 && is_int10 ? Operand::c32(1u) : Operand(max_rgb_val), values[i]);
               values[i] = bld.vop2(aco_opcode::v_max_i32, bld.def(v1),
                                    i == 3 && is_int10 ? Operand::c32(-2u) : Operand(min_rgb_val),
                                    values[i]);
            }
         }
      } else if (is_16bit) {
         for (unsigned i = 0; i < 4; i++) {
            if ((write_mask >> i) & 1) {
               Temp tmp = convert_int(ctx, bld, values[i].getTemp(), 16, 32, true);
               values[i] = Operand(tmp);
            }
         }
      }
      break;

   case V_028714_SPI_SHADER_32_ABGR: enabled_channels = 0xF; break;

   case V_028714_SPI_SHADER_ZERO:
   default: return false;
   }

   if ((bool)compr_op) {
      for (int i = 0; i < 2; i++) {
         /* check if at least one of the values to be compressed is enabled */
         bool enabled = (write_mask >> (i * 2)) & 0x3;
         if (enabled) {
            enabled_channels |= 0x3 << (i * 2);
            values[i] = bld.vop3(
               compr_op, bld.def(v1), values[i * 2].isUndefined() ? Operand::zero() : values[i * 2],
               values[i * 2 + 1].isUndefined() ? Operand::zero() : values[i * 2 + 1]);
         } else {
            values[i] = Operand(v1);
         }
      }
      values[2] = Operand(v1);
      values[3] = Operand(v1);
      compr = true;
   } else if (!compr) {
      for (int i = 0; i < 4; i++)
         values[i] = enabled_channels & (1 << i) ? values[i] : Operand(v1);
   }

   bld.exp(aco_opcode::exp, values[0], values[1], values[2], values[3], enabled_channels, target,
           compr);
   return true;
}

static void
create_fs_null_export(isel_context* ctx)
{
   /* FS must always have exports.
    * So when there are none, we need to add a null export.
    */

   Builder bld(ctx->program, ctx->block);
   unsigned dest = V_008DFC_SQ_EXP_NULL;
   bld.exp(aco_opcode::exp, Operand(v1), Operand(v1), Operand(v1), Operand(v1),
           /* enabled_mask */ 0, dest, /* compr */ false, /* done */ true, /* vm */ true);
}

static void
create_fs_exports(isel_context* ctx)
{
   bool exported = false;

   /* Export depth, stencil and sample mask. */
   if (ctx->outputs.mask[FRAG_RESULT_DEPTH] || ctx->outputs.mask[FRAG_RESULT_STENCIL] ||
       ctx->outputs.mask[FRAG_RESULT_SAMPLE_MASK])
      exported |= export_fs_mrt_z(ctx);

   /* Export all color render targets. */
   for (unsigned i = FRAG_RESULT_DATA0; i < FRAG_RESULT_DATA7 + 1; ++i)
      if (ctx->outputs.mask[i])
         exported |= export_fs_mrt_color(ctx, i);

   if (!exported)
      create_fs_null_export(ctx);

   ctx->block->kind |= block_kind_export_end;
}

static void
create_workgroup_barrier(Builder& bld)
{
   bld.barrier(aco_opcode::p_barrier,
               memory_sync_info(storage_shared, semantic_acqrel, scope_workgroup), scope_workgroup);
}

static void
emit_stream_output(isel_context* ctx, Temp const* so_buffers, Temp const* so_write_offset,
                   const struct radv_stream_output* output)
{
   unsigned num_comps = util_bitcount(output->component_mask);
   unsigned writemask = (1 << num_comps) - 1;
   unsigned loc = output->location;
   unsigned buf = output->buffer;

   assert(num_comps && num_comps <= 4);
   if (!num_comps || num_comps > 4)
      return;

   unsigned first_comp = ffs(output->component_mask) - 1;

   Temp out[4];
   bool all_undef = true;
   assert(ctx->stage.hw == HWStage::VS);
   for (unsigned i = 0; i < num_comps; i++) {
      out[i] = ctx->outputs.temps[loc * 4 + first_comp + i];
      all_undef = all_undef && !out[i].id();
   }
   if (all_undef)
      return;

   while (writemask) {
      int start, count;
      u_bit_scan_consecutive_range(&writemask, &start, &count);
      if (count == 3 && ctx->options->chip_class == GFX6) {
         /* GFX6 doesn't support storing vec3, split it. */
         writemask |= 1u << (start + 2);
         count = 2;
      }

      unsigned offset = output->offset + start * 4;

      Temp write_data = ctx->program->allocateTmp(RegClass(RegType::vgpr, count));
      aco_ptr<Pseudo_instruction> vec{create_instruction<Pseudo_instruction>(
         aco_opcode::p_create_vector, Format::PSEUDO, count, 1)};
      for (int i = 0; i < count; ++i)
         vec->operands[i] =
            (ctx->outputs.mask[loc] & 1 << (start + i)) ? Operand(out[start + i]) : Operand::zero();
      vec->definitions[0] = Definition(write_data);
      ctx->block->instructions.emplace_back(std::move(vec));

      aco_opcode opcode;
      switch (count) {
      case 1: opcode = aco_opcode::buffer_store_dword; break;
      case 2: opcode = aco_opcode::buffer_store_dwordx2; break;
      case 3: opcode = aco_opcode::buffer_store_dwordx3; break;
      case 4: opcode = aco_opcode::buffer_store_dwordx4; break;
      default: unreachable("Unsupported dword count.");
      }

      aco_ptr<MUBUF_instruction> store{
         create_instruction<MUBUF_instruction>(opcode, Format::MUBUF, 4, 0)};
      store->operands[0] = Operand(so_buffers[buf]);
      store->operands[1] = Operand(so_write_offset[buf]);
      store->operands[2] = Operand::c32(0);
      store->operands[3] = Operand(write_data);
      if (offset > 4095) {
         /* Don't think this can happen in RADV, but maybe GL? It's easy to do this anyway. */
         Builder bld(ctx->program, ctx->block);
         store->operands[0] =
            bld.vadd32(bld.def(v1), Operand::c32(offset), Operand(so_write_offset[buf]));
      } else {
         store->offset = offset;
      }
      store->offen = true;
      store->glc = true;
      store->dlc = false;
      store->slc = true;
      ctx->block->instructions.emplace_back(std::move(store));
   }
}

static void
emit_streamout(isel_context* ctx, unsigned stream)
{
   Builder bld(ctx->program, ctx->block);

   Temp so_buffers[4];
   Temp buf_ptr = convert_pointer_to_64_bit(ctx, get_arg(ctx, ctx->args->streamout_buffers));
   for (unsigned i = 0; i < 4; i++) {
      unsigned stride = ctx->program->info->so.strides[i];
      if (!stride)
         continue;

      Operand off = bld.copy(bld.def(s1), Operand::c32(i * 16u));
      so_buffers[i] = bld.smem(aco_opcode::s_load_dwordx4, bld.def(s4), buf_ptr, off);
   }

   Temp so_vtx_count =
      bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc),
               get_arg(ctx, ctx->args->ac.streamout_config), Operand::c32(0x70010u));

   Temp tid = emit_mbcnt(ctx, bld.tmp(v1));

   Temp can_emit = bld.vopc(aco_opcode::v_cmp_gt_i32, bld.def(bld.lm), so_vtx_count, tid);

   if_context ic;
   begin_divergent_if_then(ctx, &ic, can_emit);

   bld.reset(ctx->block);

   Temp so_write_index =
      bld.vadd32(bld.def(v1), get_arg(ctx, ctx->args->ac.streamout_write_index), tid);

   Temp so_write_offset[4];

   for (unsigned i = 0; i < 4; i++) {
      unsigned stride = ctx->program->info->so.strides[i];
      if (!stride)
         continue;

      if (stride == 1) {
         Temp offset = bld.sop2(aco_opcode::s_add_i32, bld.def(s1), bld.def(s1, scc),
                                get_arg(ctx, ctx->args->ac.streamout_write_index),
                                get_arg(ctx, ctx->args->ac.streamout_offset[i]));
         Temp new_offset = bld.vadd32(bld.def(v1), offset, tid);

         so_write_offset[i] =
            bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(2u), new_offset);
      } else {
         Temp offset = bld.v_mul_imm(bld.def(v1), so_write_index, stride * 4u);
         Temp offset2 = bld.sop2(aco_opcode::s_mul_i32, bld.def(s1), Operand::c32(4u),
                                 get_arg(ctx, ctx->args->ac.streamout_offset[i]));
         so_write_offset[i] = bld.vadd32(bld.def(v1), offset, offset2);
      }
   }

   for (unsigned i = 0; i < ctx->program->info->so.num_outputs; i++) {
      struct radv_stream_output* output = &ctx->program->info->so.outputs[i];
      if (stream != output->stream)
         continue;

      emit_stream_output(ctx, so_buffers, so_write_offset, output);
   }

   begin_divergent_if_else(ctx, &ic);
   end_divergent_if(ctx, &ic);
}

Pseudo_instruction*
add_startpgm(struct isel_context* ctx)
{
   unsigned arg_count = ctx->args->ac.arg_count;
   if (ctx->stage == fragment_fs) {
      /* LLVM optimizes away unused FS inputs and computes spi_ps_input_addr
       * itself and then communicates the results back via the ELF binary.
       * Mirror what LLVM does by re-mapping the VGPR arguments here.
       *
       * TODO: If we made the FS input scanning code into a separate pass that
       * could run before argument setup, then this wouldn't be necessary
       * anymore.
       */
      struct ac_shader_args* args = &ctx->args->ac;
      arg_count = 0;
      for (unsigned i = 0, vgpr_arg = 0, vgpr_reg = 0; i < args->arg_count; i++) {
         if (args->args[i].file != AC_ARG_VGPR) {
            arg_count++;
            continue;
         }

         if (!(ctx->program->config->spi_ps_input_addr & (1 << vgpr_arg))) {
            args->args[i].skip = true;
         } else {
            args->args[i].offset = vgpr_reg;
            vgpr_reg += args->args[i].size;
            arg_count++;
         }
         vgpr_arg++;
      }
   }

   aco_ptr<Pseudo_instruction> startpgm{
      create_instruction<Pseudo_instruction>(aco_opcode::p_startpgm, Format::PSEUDO, 0, arg_count)};
   for (unsigned i = 0, arg = 0; i < ctx->args->ac.arg_count; i++) {
      if (ctx->args->ac.args[i].skip)
         continue;

      enum ac_arg_regfile file = ctx->args->ac.args[i].file;
      unsigned size = ctx->args->ac.args[i].size;
      unsigned reg = ctx->args->ac.args[i].offset;
      RegClass type = RegClass(file == AC_ARG_SGPR ? RegType::sgpr : RegType::vgpr, size);
      Temp dst = ctx->program->allocateTmp(type);
      ctx->arg_temps[i] = dst;
      startpgm->definitions[arg] = Definition(dst);
      startpgm->definitions[arg].setFixed(PhysReg{file == AC_ARG_SGPR ? reg : reg + 256});
      arg++;
   }
   Pseudo_instruction* instr = startpgm.get();
   ctx->block->instructions.push_back(std::move(startpgm));

   /* Stash these in the program so that they can be accessed later when
    * handling spilling.
    */
   ctx->program->private_segment_buffer = get_arg(ctx, ctx->args->ring_offsets);
   ctx->program->scratch_offset = get_arg(ctx, ctx->args->ac.scratch_offset);

   return instr;
}

void
fix_ls_vgpr_init_bug(isel_context* ctx, Pseudo_instruction* startpgm)
{
   assert(ctx->shader->info.stage == MESA_SHADER_VERTEX);
   Builder bld(ctx->program, ctx->block);
   constexpr unsigned hs_idx = 1u;
   Builder::Result hs_thread_count = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc),
                                              get_arg(ctx, ctx->args->ac.merged_wave_info),
                                              Operand::c32((8u << 16) | (hs_idx * 8u)));
   Temp ls_has_nonzero_hs_threads = bool_to_vector_condition(ctx, hs_thread_count.def(1).getTemp());

   /* If there are no HS threads, SPI mistakenly loads the LS VGPRs starting at VGPR 0. */

   Temp instance_id =
      bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), get_arg(ctx, ctx->args->ac.vertex_id),
               get_arg(ctx, ctx->args->ac.instance_id), ls_has_nonzero_hs_threads);
   Temp vs_rel_patch_id =
      bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), get_arg(ctx, ctx->args->ac.tcs_rel_ids),
               get_arg(ctx, ctx->args->ac.vs_rel_patch_id), ls_has_nonzero_hs_threads);
   Temp vertex_id =
      bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), get_arg(ctx, ctx->args->ac.tcs_patch_id),
               get_arg(ctx, ctx->args->ac.vertex_id), ls_has_nonzero_hs_threads);

   ctx->arg_temps[ctx->args->ac.instance_id.arg_index] = instance_id;
   ctx->arg_temps[ctx->args->ac.vs_rel_patch_id.arg_index] = vs_rel_patch_id;
   ctx->arg_temps[ctx->args->ac.vertex_id.arg_index] = vertex_id;
}

void
split_arguments(isel_context* ctx, Pseudo_instruction* startpgm)
{
   /* Split all arguments except for the first (ring_offsets) and the last
    * (exec) so that the dead channels don't stay live throughout the program.
    */
   for (int i = 1; i < startpgm->definitions.size(); i++) {
      if (startpgm->definitions[i].regClass().size() > 1) {
         emit_split_vector(ctx, startpgm->definitions[i].getTemp(),
                           startpgm->definitions[i].regClass().size());
      }
   }
}

void
handle_bc_optimize(isel_context* ctx)
{
   /* needed when SPI_PS_IN_CONTROL.BC_OPTIMIZE_DISABLE is set to 0 */
   Builder bld(ctx->program, ctx->block);
   uint32_t spi_ps_input_ena = ctx->program->config->spi_ps_input_ena;
   bool uses_center =
      G_0286CC_PERSP_CENTER_ENA(spi_ps_input_ena) || G_0286CC_LINEAR_CENTER_ENA(spi_ps_input_ena);
   bool uses_centroid = G_0286CC_PERSP_CENTROID_ENA(spi_ps_input_ena) ||
                        G_0286CC_LINEAR_CENTROID_ENA(spi_ps_input_ena);
   ctx->persp_centroid = get_arg(ctx, ctx->args->ac.persp_centroid);
   ctx->linear_centroid = get_arg(ctx, ctx->args->ac.linear_centroid);
   if (uses_center && uses_centroid) {
      Temp sel = bld.vopc_e64(aco_opcode::v_cmp_lt_i32, bld.hint_vcc(bld.def(bld.lm)),
                              get_arg(ctx, ctx->args->ac.prim_mask), Operand::zero());

      if (G_0286CC_PERSP_CENTROID_ENA(spi_ps_input_ena)) {
         Temp new_coord[2];
         for (unsigned i = 0; i < 2; i++) {
            Temp persp_centroid =
               emit_extract_vector(ctx, get_arg(ctx, ctx->args->ac.persp_centroid), i, v1);
            Temp persp_center =
               emit_extract_vector(ctx, get_arg(ctx, ctx->args->ac.persp_center), i, v1);
            new_coord[i] =
               bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), persp_centroid, persp_center, sel);
         }
         ctx->persp_centroid = bld.tmp(v2);
         bld.pseudo(aco_opcode::p_create_vector, Definition(ctx->persp_centroid),
                    Operand(new_coord[0]), Operand(new_coord[1]));
         emit_split_vector(ctx, ctx->persp_centroid, 2);
      }

      if (G_0286CC_LINEAR_CENTROID_ENA(spi_ps_input_ena)) {
         Temp new_coord[2];
         for (unsigned i = 0; i < 2; i++) {
            Temp linear_centroid =
               emit_extract_vector(ctx, get_arg(ctx, ctx->args->ac.linear_centroid), i, v1);
            Temp linear_center =
               emit_extract_vector(ctx, get_arg(ctx, ctx->args->ac.linear_center), i, v1);
            new_coord[i] = bld.vop2(aco_opcode::v_cndmask_b32, bld.def(v1), linear_centroid,
                                    linear_center, sel);
         }
         ctx->linear_centroid = bld.tmp(v2);
         bld.pseudo(aco_opcode::p_create_vector, Definition(ctx->linear_centroid),
                    Operand(new_coord[0]), Operand(new_coord[1]));
         emit_split_vector(ctx, ctx->linear_centroid, 2);
      }
   }
}

void
setup_fp_mode(isel_context* ctx, nir_shader* shader)
{
   Program* program = ctx->program;

   unsigned float_controls = shader->info.float_controls_execution_mode;

   program->next_fp_mode.preserve_signed_zero_inf_nan32 =
      float_controls & FLOAT_CONTROLS_SIGNED_ZERO_INF_NAN_PRESERVE_FP32;
   program->next_fp_mode.preserve_signed_zero_inf_nan16_64 =
      float_controls & (FLOAT_CONTROLS_SIGNED_ZERO_INF_NAN_PRESERVE_FP16 |
                        FLOAT_CONTROLS_SIGNED_ZERO_INF_NAN_PRESERVE_FP64);

   program->next_fp_mode.must_flush_denorms32 =
      float_controls & FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP32;
   program->next_fp_mode.must_flush_denorms16_64 =
      float_controls &
      (FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP16 | FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP64);

   program->next_fp_mode.care_about_round32 =
      float_controls &
      (FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP32 | FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP32);

   program->next_fp_mode.care_about_round16_64 =
      float_controls &
      (FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP16 | FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP64 |
       FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP16 | FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP64);

   /* default to preserving fp16 and fp64 denorms, since it's free for fp64 and
    * the precision seems needed for Wolfenstein: Youngblood to render correctly */
   if (program->next_fp_mode.must_flush_denorms16_64)
      program->next_fp_mode.denorm16_64 = 0;
   else
      program->next_fp_mode.denorm16_64 = fp_denorm_keep;

   /* preserving fp32 denorms is expensive, so only do it if asked */
   if (float_controls & FLOAT_CONTROLS_DENORM_PRESERVE_FP32)
      program->next_fp_mode.denorm32 = fp_denorm_keep;
   else
      program->next_fp_mode.denorm32 = 0;

   if (float_controls & FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP32)
      program->next_fp_mode.round32 = fp_round_tz;
   else
      program->next_fp_mode.round32 = fp_round_ne;

   if (float_controls &
       (FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP16 | FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP64))
      program->next_fp_mode.round16_64 = fp_round_tz;
   else
      program->next_fp_mode.round16_64 = fp_round_ne;

   ctx->block->fp_mode = program->next_fp_mode;
}

void
cleanup_cfg(Program* program)
{
   /* create linear_succs/logical_succs */
   for (Block& BB : program->blocks) {
      for (unsigned idx : BB.linear_preds)
         program->blocks[idx].linear_succs.emplace_back(BB.index);
      for (unsigned idx : BB.logical_preds)
         program->blocks[idx].logical_succs.emplace_back(BB.index);
   }
}

Temp
lanecount_to_mask(isel_context* ctx, Temp count, bool allow64 = true)
{
   assert(count.regClass() == s1);

   Builder bld(ctx->program, ctx->block);
   Temp mask = bld.sop2(aco_opcode::s_bfm_b64, bld.def(s2), count, Operand::zero());
   Temp cond;

   if (ctx->program->wave_size == 64) {
      /* If we know that all 64 threads can't be active at a time, we just use the mask as-is */
      if (!allow64)
         return mask;

      /* Special case for 64 active invocations, because 64 doesn't work with s_bfm */
      Temp active_64 = bld.sopc(aco_opcode::s_bitcmp1_b32, bld.def(s1, scc), count,
                                Operand::c32(6u /* log2(64) */));
      cond =
         bld.sop2(Builder::s_cselect, bld.def(bld.lm), Operand::c32(-1u), mask, bld.scc(active_64));
   } else {
      /* We use s_bfm_b64 (not _b32) which works with 32, but we need to extract the lower half of
       * the register */
      cond = emit_extract_vector(ctx, mask, 0, bld.lm);
   }

   return cond;
}

Temp
merged_wave_info_to_mask(isel_context* ctx, unsigned i)
{
   Builder bld(ctx->program, ctx->block);

   /* lanecount_to_mask() only cares about s0.u[6:0] so we don't need either s_bfe nor s_and here */
   Temp count = i == 0
                   ? get_arg(ctx, ctx->args->ac.merged_wave_info)
                   : bld.sop2(aco_opcode::s_lshr_b32, bld.def(s1), bld.def(s1, scc),
                              get_arg(ctx, ctx->args->ac.merged_wave_info), Operand::c32(i * 8u));

   return lanecount_to_mask(ctx, count);
}

void
ngg_emit_sendmsg_gs_alloc_req(isel_context* ctx, Temp vtx_cnt, Temp prm_cnt)
{
   assert(vtx_cnt.id() && prm_cnt.id());

   Builder bld(ctx->program, ctx->block);
   Temp prm_cnt_0;

   if (ctx->program->chip_class == GFX10 &&
       (ctx->stage.has(SWStage::GS) || ctx->program->info->has_ngg_culling)) {
      /* Navi 1x workaround: check whether the workgroup has no output.
       * If so, change the number of exported vertices and primitives to 1.
       */
      prm_cnt_0 = bld.sopc(aco_opcode::s_cmp_eq_u32, bld.def(s1, scc), prm_cnt, Operand::zero());
      prm_cnt = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1), Operand::c32(1u), prm_cnt,
                         bld.scc(prm_cnt_0));
      vtx_cnt = bld.sop2(aco_opcode::s_cselect_b32, bld.def(s1), Operand::c32(1u), vtx_cnt,
                         bld.scc(prm_cnt_0));
   }

   /* Put the number of vertices and primitives into m0 for the GS_ALLOC_REQ */
   Temp tmp =
      bld.sop2(aco_opcode::s_lshl_b32, bld.def(s1), bld.def(s1, scc), prm_cnt, Operand::c32(12u));
   tmp = bld.sop2(aco_opcode::s_or_b32, bld.m0(bld.def(s1)), bld.def(s1, scc), tmp, vtx_cnt);

   /* Request the SPI to allocate space for the primitives and vertices
    * that will be exported by the threadgroup.
    */
   bld.sopp(aco_opcode::s_sendmsg, bld.m0(tmp), -1, sendmsg_gs_alloc_req);

   if (prm_cnt_0.id()) {
      /* Navi 1x workaround: export a triangle with NaN coordinates when NGG has no output.
       * It can't have all-zero positions because that would render an undesired pixel with
       * conservative rasterization.
       */
      Temp first_lane = bld.sop1(Builder::s_ff1_i32, bld.def(s1), Operand(exec, bld.lm));
      Temp cond = bld.sop2(Builder::s_lshl, bld.def(bld.lm), bld.def(s1, scc),
                           Operand::c32_or_c64(1u, ctx->program->wave_size == 64), first_lane);
      cond = bld.sop2(Builder::s_cselect, bld.def(bld.lm), cond,
                      Operand::zero(ctx->program->wave_size == 64 ? 8 : 4), bld.scc(prm_cnt_0));

      if_context ic_prim_0;
      begin_divergent_if_then(ctx, &ic_prim_0, cond);
      bld.reset(ctx->block);
      ctx->block->kind |= block_kind_export_end;

      /* Use zero: means that it's a triangle whose every vertex index is 0. */
      Temp zero = bld.copy(bld.def(v1), Operand::zero());
      /* Use NaN for the coordinates, so that the rasterizer allways culls it.  */
      Temp nan_coord = bld.copy(bld.def(v1), Operand::c32(-1u));

      bld.exp(aco_opcode::exp, zero, Operand(v1), Operand(v1), Operand(v1), 1 /* enabled mask */,
              V_008DFC_SQ_EXP_PRIM /* dest */, false /* compressed */, true /* done */,
              false /* valid mask */);
      bld.exp(aco_opcode::exp, nan_coord, nan_coord, nan_coord, nan_coord, 0xf /* enabled mask */,
              V_008DFC_SQ_EXP_POS /* dest */, false /* compressed */, true /* done */,
              true /* valid mask */);

      begin_divergent_if_else(ctx, &ic_prim_0);
      end_divergent_if(ctx, &ic_prim_0);
      bld.reset(ctx->block);
   }
}

} /* end namespace */

void
select_program(Program* program, unsigned shader_count, struct nir_shader* const* shaders,
               ac_shader_config* config, struct radv_shader_args* args)
{
   isel_context ctx = setup_isel_context(program, shader_count, shaders, config, args, false);
   if_context ic_merged_wave_info;
   bool ngg_gs = ctx.stage.hw == HWStage::NGG && ctx.stage.has(SWStage::GS);

   for (unsigned i = 0; i < shader_count; i++) {
      nir_shader* nir = shaders[i];
      init_context(&ctx, nir);

      setup_fp_mode(&ctx, nir);

      if (!i) {
         /* needs to be after init_context() for FS */
         Pseudo_instruction* startpgm = add_startpgm(&ctx);
         append_logical_start(ctx.block);

         if (unlikely(args->options->has_ls_vgpr_init_bug && ctx.stage == vertex_tess_control_hs))
            fix_ls_vgpr_init_bug(&ctx, startpgm);

         split_arguments(&ctx, startpgm);

         if (program->stage.has(SWStage::VS) || program->stage.has(SWStage::TES)) {
            Builder(ctx.program, ctx.block).sopp(aco_opcode::s_setprio, -1u, 0x3u);
         }
      }

      /* In a merged VS+TCS HS, the VS implementation can be completely empty. */
      nir_function_impl* func = nir_shader_get_entrypoint(nir);
      bool empty_shader =
         nir_cf_list_is_empty_block(&func->body) &&
         ((nir->info.stage == MESA_SHADER_VERTEX &&
           (ctx.stage == vertex_tess_control_hs || ctx.stage == vertex_geometry_gs)) ||
          (nir->info.stage == MESA_SHADER_TESS_EVAL && ctx.stage == tess_eval_geometry_gs));

      bool check_merged_wave_info =
         ctx.tcs_in_out_eq ? i == 0 : (shader_count >= 2 && !empty_shader && !(ngg_gs && i == 1));
      bool endif_merged_wave_info =
         ctx.tcs_in_out_eq ? i == 1 : (check_merged_wave_info && !(ngg_gs && i == 1));

      if (program->chip_class == GFX10 && program->stage.hw == HWStage::NGG &&
          program->stage.num_sw_stages() == 1) {
         /* Workaround for Navi1x HW bug to ensure that all NGG waves launch before
          * s_sendmsg(GS_ALLOC_REQ). */
         Builder(ctx.program, ctx.block).sopp(aco_opcode::s_barrier, -1u, 0u);
      }

      if (check_merged_wave_info) {
         Temp cond = merged_wave_info_to_mask(&ctx, i);
         begin_divergent_if_then(&ctx, &ic_merged_wave_info, cond);
      }

      if (i) {
         Builder bld(ctx.program, ctx.block);

         /* Skip s_barrier from TCS when VS outputs are not stored in the LDS. */
         bool tcs_skip_barrier = ctx.stage == vertex_tess_control_hs &&
                                 ctx.tcs_temp_only_inputs == nir->info.inputs_read;

         if (!ngg_gs && !tcs_skip_barrier)
            create_workgroup_barrier(bld);

         if (ctx.stage == vertex_geometry_gs || ctx.stage == tess_eval_geometry_gs) {
            ctx.gs_wave_id = bld.pseudo(aco_opcode::p_extract, bld.def(s1, m0), bld.def(s1, scc),
                                        get_arg(&ctx, args->ac.merged_wave_info), Operand::c32(2u),
                                        Operand::c32(8u), Operand::zero());
         }
      } else if (ctx.stage == geometry_gs)
         ctx.gs_wave_id = get_arg(&ctx, args->ac.gs_wave_id);

      if (ctx.stage == fragment_fs)
         handle_bc_optimize(&ctx);

      visit_cf_list(&ctx, &func->body);

      if (ctx.program->info->so.num_outputs && ctx.stage.hw == HWStage::VS)
         emit_streamout(&ctx, 0);

      if (ctx.stage.hw == HWStage::VS) {
         create_vs_exports(&ctx);
      } else if (nir->info.stage == MESA_SHADER_GEOMETRY && !ngg_gs) {
         Builder bld(ctx.program, ctx.block);
         bld.barrier(aco_opcode::p_barrier,
                     memory_sync_info(storage_vmem_output, semantic_release, scope_device));
         bld.sopp(aco_opcode::s_sendmsg, bld.m0(ctx.gs_wave_id), -1,
                  sendmsg_gs_done(false, false, 0));
      }

      if (ctx.stage == fragment_fs) {
         create_fs_exports(&ctx);
      }

      if (endif_merged_wave_info) {
         begin_divergent_if_else(&ctx, &ic_merged_wave_info);
         end_divergent_if(&ctx, &ic_merged_wave_info);
      }

      if (i == 0 && ctx.stage == vertex_tess_control_hs && ctx.tcs_in_out_eq) {
         /* Outputs of the previous stage are inputs to the next stage */
         ctx.inputs = ctx.outputs;
         ctx.outputs = shader_io_state();
      }

      cleanup_context(&ctx);
   }

   program->config->float_mode = program->blocks[0].fp_mode.val;

   append_logical_end(ctx.block);
   ctx.block->kind |= block_kind_uniform;
   Builder bld(ctx.program, ctx.block);
   bld.sopp(aco_opcode::s_endpgm);

   cleanup_cfg(program);
}

void
select_gs_copy_shader(Program* program, struct nir_shader* gs_shader, ac_shader_config* config,
                      struct radv_shader_args* args)
{
   isel_context ctx = setup_isel_context(program, 1, &gs_shader, config, args, true);

   ctx.block->fp_mode = program->next_fp_mode;

   add_startpgm(&ctx);
   append_logical_start(ctx.block);

   Builder bld(ctx.program, ctx.block);

   Temp gsvs_ring = bld.smem(aco_opcode::s_load_dwordx4, bld.def(s4),
                             program->private_segment_buffer, Operand::c32(RING_GSVS_VS * 16u));

   Operand stream_id = Operand::zero();
   if (args->shader_info->so.num_outputs)
      stream_id = bld.sop2(aco_opcode::s_bfe_u32, bld.def(s1), bld.def(s1, scc),
                           get_arg(&ctx, ctx.args->ac.streamout_config), Operand::c32(0x20018u));

   Temp vtx_offset = bld.vop2(aco_opcode::v_lshlrev_b32, bld.def(v1), Operand::c32(2u),
                              get_arg(&ctx, ctx.args->ac.vertex_id));

   std::stack<if_context> if_contexts;

   for (unsigned stream = 0; stream < 4; stream++) {
      if (stream_id.isConstant() && stream != stream_id.constantValue())
         continue;

      unsigned num_components = args->shader_info->gs.num_stream_output_components[stream];
      if (stream > 0 && (!num_components || !args->shader_info->so.num_outputs))
         continue;

      memset(ctx.outputs.mask, 0, sizeof(ctx.outputs.mask));

      if (!stream_id.isConstant()) {
         Temp cond =
            bld.sopc(aco_opcode::s_cmp_eq_u32, bld.def(s1, scc), stream_id, Operand::c32(stream));
         if_contexts.emplace();
         begin_uniform_if_then(&ctx, &if_contexts.top(), cond);
         bld.reset(ctx.block);
      }

      unsigned offset = 0;
      for (unsigned i = 0; i <= VARYING_SLOT_VAR31; ++i) {
         if (args->shader_info->gs.output_streams[i] != stream)
            continue;

         unsigned output_usage_mask = args->shader_info->gs.output_usage_mask[i];
         unsigned length = util_last_bit(output_usage_mask);
         for (unsigned j = 0; j < length; ++j) {
            if (!(output_usage_mask & (1 << j)))
               continue;

            Temp val = bld.tmp(v1);
            unsigned const_offset = offset * args->shader_info->gs.vertices_out * 16 * 4;
            load_vmem_mubuf(&ctx, val, gsvs_ring, vtx_offset, Temp(), const_offset, 4, 1, 0u, true,
                            true, true);

            ctx.outputs.mask[i] |= 1 << j;
            ctx.outputs.temps[i * 4u + j] = val;

            offset++;
         }
      }

      if (args->shader_info->so.num_outputs) {
         emit_streamout(&ctx, stream);
         bld.reset(ctx.block);
      }

      if (stream == 0) {
         create_vs_exports(&ctx);
      }

      if (!stream_id.isConstant()) {
         begin_uniform_if_else(&ctx, &if_contexts.top());
         bld.reset(ctx.block);
      }
   }

   while (!if_contexts.empty()) {
      end_uniform_if(&ctx, &if_contexts.top());
      if_contexts.pop();
   }

   program->config->float_mode = program->blocks[0].fp_mode.val;

   append_logical_end(ctx.block);
   ctx.block->kind |= block_kind_uniform;
   bld.reset(ctx.block);
   bld.sopp(aco_opcode::s_endpgm);

   cleanup_cfg(program);
}

void
select_trap_handler_shader(Program* program, struct nir_shader* shader, ac_shader_config* config,
                           struct radv_shader_args* args)
{
   assert(args->options->chip_class == GFX8);

   init_program(program, compute_cs, args->shader_info, args->options->chip_class,
                args->options->family, args->options->wgp_mode, config);

   isel_context ctx = {};
   ctx.program = program;
   ctx.args = args;
   ctx.options = args->options;
   ctx.stage = program->stage;

   ctx.block = ctx.program->create_and_insert_block();
   ctx.block->kind = block_kind_top_level;

   program->workgroup_size = 1; /* XXX */

   add_startpgm(&ctx);
   append_logical_start(ctx.block);

   Builder bld(ctx.program, ctx.block);

   /* Load the buffer descriptor from TMA. */
   bld.smem(aco_opcode::s_load_dwordx4, Definition(PhysReg{ttmp4}, s4), Operand(PhysReg{tma}, s2),
            Operand::zero());

   /* Store TTMP0-TTMP1. */
   bld.smem(aco_opcode::s_buffer_store_dwordx2, Operand(PhysReg{ttmp4}, s4), Operand::zero(),
            Operand(PhysReg{ttmp0}, s2), memory_sync_info(), true);

   uint32_t hw_regs_idx[] = {
      2, /* HW_REG_STATUS */
      3, /* HW_REG_TRAP_STS */
      4, /* HW_REG_HW_ID */
      7, /* HW_REG_IB_STS */
   };

   /* Store some hardware registers. */
   for (unsigned i = 0; i < ARRAY_SIZE(hw_regs_idx); i++) {
      /* "((size - 1) << 11) | register" */
      bld.sopk(aco_opcode::s_getreg_b32, Definition(PhysReg{ttmp8}, s1),
               ((20 - 1) << 11) | hw_regs_idx[i]);

      bld.smem(aco_opcode::s_buffer_store_dword, Operand(PhysReg{ttmp4}, s4),
               Operand::c32(8u + i * 4), Operand(PhysReg{ttmp8}, s1), memory_sync_info(), true);
   }

   program->config->float_mode = program->blocks[0].fp_mode.val;

   append_logical_end(ctx.block);
   ctx.block->kind |= block_kind_uniform;
   bld.sopp(aco_opcode::s_endpgm);

   cleanup_cfg(program);
}
} // namespace aco
