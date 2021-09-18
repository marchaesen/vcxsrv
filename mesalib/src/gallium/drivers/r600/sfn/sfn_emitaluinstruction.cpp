/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2018 Collabora LTD
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


#include "sfn_emitaluinstruction.h"
#include "sfn_debug.h"

#include "gallium/drivers/r600/r600_shader.h"

namespace r600 {

using std::vector;

EmitAluInstruction::EmitAluInstruction(ShaderFromNirProcessor& processor):
   EmitInstruction (processor)
{

}

bool EmitAluInstruction::do_emit(nir_instr* ir)
{
   const nir_alu_instr& instr = *nir_instr_as_alu(ir);

   r600::sfn_log << SfnLog::instr << "emit '"
                 << *ir
                 << " bitsize: " << static_cast<int>(instr.dest.dest.ssa.bit_size)
                 << "' (" << __func__ << ")\n";

   preload_src(instr);

   if (get_chip_class() == CAYMAN) {
      switch (instr.op) {
      case nir_op_fcos_r600: return emit_alu_cm_trig(instr, op1_cos);
      case nir_op_fexp2: return emit_alu_cm_trig(instr, op1_exp_ieee);
      case nir_op_flog2: return emit_alu_cm_trig(instr, op1_log_clamped);
      case nir_op_frcp: return emit_alu_cm_trig(instr, op1_recip_ieee);
      case nir_op_frsq: return emit_alu_cm_trig(instr, op1_recipsqrt_ieee1);
      case nir_op_fsin_r600: return emit_alu_cm_trig(instr, op1_sin);
      case nir_op_fsqrt: return emit_alu_cm_trig(instr, op1_sqrt_ieee);
         default:
            ;
      }
   }

   switch (instr.op) {
    /* These are in the ALU instruction list, but they should be texture instructions */
   case nir_op_b2b1: return emit_mov(instr);
   case nir_op_b2b32: return emit_mov(instr);
   case nir_op_b2f32: return emit_alu_b2f(instr);
   case nir_op_b2i32: return emit_b2i32(instr);
   case nir_op_b32all_fequal2: return emit_any_all_fcomp2(instr, op2_sete_dx10, true);
   case nir_op_b32all_fequal3: return emit_any_all_fcomp(instr, op2_sete, 3, true);
   case nir_op_b32all_fequal4: return emit_any_all_fcomp(instr, op2_sete, 4, true);
   case nir_op_b32all_iequal2: return emit_any_all_icomp(instr, op2_sete_int, 2, true);
   case nir_op_b32all_iequal3: return emit_any_all_icomp(instr, op2_sete_int, 3, true);
   case nir_op_b32all_iequal4: return emit_any_all_icomp(instr, op2_sete_int, 4, true);
   case nir_op_b32any_fnequal2: return emit_any_all_fcomp2(instr, op2_setne_dx10, false);
   case nir_op_b32any_fnequal3: return emit_any_all_fcomp(instr, op2_setne, 3, false);
   case nir_op_b32any_fnequal4: return emit_any_all_fcomp(instr, op2_setne, 4, false);
   case nir_op_b32any_inequal2: return emit_any_all_icomp(instr, op2_setne_int, 2, false);
   case nir_op_b32any_inequal3: return emit_any_all_icomp(instr, op2_setne_int, 3, false);
   case nir_op_b32any_inequal4: return emit_any_all_icomp(instr, op2_setne_int, 4, false);
   case nir_op_b32csel: return emit_alu_op3(instr, op3_cnde_int,  {0, 2, 1});
   case nir_op_ball_fequal2: return emit_any_all_fcomp2(instr, op2_sete_dx10, true);
   case nir_op_ball_fequal3: return emit_any_all_fcomp(instr, op2_sete, 3, true);
   case nir_op_ball_fequal4: return emit_any_all_fcomp(instr, op2_sete, 4, true);
   case nir_op_ball_iequal2: return emit_any_all_icomp(instr, op2_sete_int, 2, true);
   case nir_op_ball_iequal3: return emit_any_all_icomp(instr, op2_sete_int, 3, true);
   case nir_op_ball_iequal4: return emit_any_all_icomp(instr, op2_sete_int, 4, true);
   case nir_op_bany_fnequal2: return emit_any_all_fcomp2(instr, op2_setne_dx10, false);
   case nir_op_bany_fnequal3: return emit_any_all_fcomp(instr, op2_setne, 3, false);
   case nir_op_bany_fnequal4: return emit_any_all_fcomp(instr, op2_setne, 4, false);
   case nir_op_bany_inequal2: return emit_any_all_icomp(instr, op2_setne_int, 2, false);
   case nir_op_bany_inequal3: return emit_any_all_icomp(instr, op2_setne_int, 3, false);
   case nir_op_bany_inequal4: return emit_any_all_icomp(instr, op2_setne_int, 4, false);
   case nir_op_bcsel: return emit_alu_op3(instr, op3_cnde_int,  {0, 2, 1});
   case nir_op_bfm: return emit_alu_op2_int(instr, op2_bfm_int);
   case nir_op_bit_count: return emit_alu_op1(instr, op1_bcnt_int);

   case nir_op_bitfield_reverse: return emit_alu_op1(instr, op1_bfrev_int);
   case nir_op_bitfield_select: return emit_alu_op3(instr, op3_bfi_int);
   case nir_op_cube_r600: return emit_cube(instr);
   case nir_op_f2b1: return emit_alu_i2orf2_b1(instr, op2_setne_dx10);
   case nir_op_f2b32: return emit_alu_f2b32(instr);
   case nir_op_f2i32: return emit_alu_f2i32_or_u32(instr, op1_flt_to_int);
   case nir_op_f2u32: return emit_alu_f2i32_or_u32(instr, op1_flt_to_uint);
   case nir_op_fabs: return emit_alu_op1(instr, op1_mov, {1 << alu_src0_abs});
   case nir_op_fadd: return emit_alu_op2(instr, op2_add);
   case nir_op_fceil: return emit_alu_op1(instr, op1_ceil);
   case nir_op_fcos_r600: return emit_alu_trans_op1(instr, op1_cos);
   case nir_op_fcsel: return emit_alu_op3(instr, op3_cnde, {0, 2, 1});
   case nir_op_fcsel_ge: return emit_alu_op3(instr, op3_cndge, {0, 1, 2});
   case nir_op_fcsel_gt: return emit_alu_op3(instr, op3_cndgt, {0, 1, 2});

    /* These are in the ALU instruction list, but they should be texture instructions */
   case nir_op_fddx: return emit_tex_fdd(instr, TexInstruction::get_gradient_h, false);
   case nir_op_fddx_coarse: return emit_tex_fdd(instr, TexInstruction::get_gradient_h, false);
   case nir_op_fddx_fine: return emit_tex_fdd(instr, TexInstruction::get_gradient_h, true);
   case nir_op_fddy: return emit_tex_fdd(instr,TexInstruction::get_gradient_v, false);
   case nir_op_fddy_coarse:
   case nir_op_fddy_fine: return emit_tex_fdd(instr, TexInstruction::get_gradient_v,  true);
   case nir_op_fdot2: return emit_dot(instr, 2);
   case nir_op_fdot3: return emit_dot(instr, 3);
   case nir_op_fdot4: return emit_dot(instr, 4);
   case nir_op_fdph:  return emit_fdph(instr);
   case nir_op_feq32: return emit_alu_op2(instr, op2_sete_dx10);
   case nir_op_feq: return emit_alu_op2(instr, op2_sete_dx10);
   case nir_op_fexp2: return emit_alu_trans_op1(instr, op1_exp_ieee);
   case nir_op_ffloor: return emit_alu_op1(instr, op1_floor);
   case nir_op_ffma: return emit_alu_op3(instr, op3_muladd_ieee);
   case nir_op_ffract: return emit_alu_op1(instr, op1_fract);
   case nir_op_fge32: return emit_alu_op2(instr, op2_setge_dx10);
   case nir_op_fge: return emit_alu_op2(instr, op2_setge_dx10);
   case nir_op_find_lsb: return emit_alu_op1(instr, op1_ffbl_int);
   case nir_op_flog2: return emit_alu_trans_op1(instr, op1_log_clamped);
   case nir_op_flt32: return emit_alu_op2(instr, op2_setgt_dx10, op2_opt_reverse);
   case nir_op_flt: return emit_alu_op2(instr, op2_setgt_dx10, op2_opt_reverse);
   case nir_op_fmax: return emit_alu_op2(instr, op2_max_dx10);
   case nir_op_fmin: return emit_alu_op2(instr, op2_min_dx10);
   case nir_op_fmul: return emit_alu_op2(instr, op2_mul_ieee);
   case nir_op_fneg: return emit_alu_op1(instr, op1_mov, {1 << alu_src0_neg});
   case nir_op_fneu32: return emit_alu_op2(instr, op2_setne_dx10);
   case nir_op_fneu: return emit_alu_op2(instr, op2_setne_dx10);
   case nir_op_frcp: return emit_alu_trans_op1(instr, op1_recip_ieee);
   case nir_op_fround_even: return emit_alu_op1(instr, op1_rndne);
   case nir_op_frsq: return emit_alu_trans_op1(instr, op1_recipsqrt_ieee1);
   case nir_op_fsat: return emit_alu_op1(instr, op1_mov, {1 << alu_dst_clamp});
   case nir_op_fsin_r600: return emit_alu_trans_op1(instr, op1_sin);
   case nir_op_fsqrt: return emit_alu_trans_op1(instr, op1_sqrt_ieee);
   case nir_op_fsub: return emit_alu_op2(instr, op2_add, op2_opt_neg_src1);
   case nir_op_ftrunc: return emit_alu_op1(instr, op1_trunc);
   case nir_op_i2b1: return emit_alu_i2orf2_b1(instr, op2_setne_int);
   case nir_op_i2b32: return emit_alu_i2orf2_b1(instr, op2_setne_int);
   case nir_op_i2f32: return emit_alu_trans_op1(instr, op1_int_to_flt);
   case nir_op_iadd: return emit_alu_op2_int(instr, op2_add_int);
   case nir_op_iand: return emit_alu_op2_int(instr, op2_and_int);
   case nir_op_ibfe: return emit_alu_op3(instr, op3_bfe_int);
   case nir_op_i32csel_ge: return emit_alu_op3(instr, op3_cndge_int,  {0, 1, 2});
   case nir_op_i32csel_gt: return emit_alu_op3(instr, op3_cndgt_int,  {0, 1, 2});
   case nir_op_ieq32: return emit_alu_op2_int(instr, op2_sete_int);
   case nir_op_ieq: return emit_alu_op2_int(instr, op2_sete_int);
   case nir_op_ifind_msb_rev: return emit_alu_op1(instr, op1_ffbh_int);
   case nir_op_ige32: return emit_alu_op2_int(instr, op2_setge_int);
   case nir_op_ige: return emit_alu_op2_int(instr, op2_setge_int);
   case nir_op_ilt32: return emit_alu_op2_int(instr, op2_setgt_int, op2_opt_reverse);
   case nir_op_ilt: return emit_alu_op2_int(instr, op2_setgt_int, op2_opt_reverse);
   case nir_op_imax: return emit_alu_op2_int(instr, op2_max_int);
   case nir_op_imin: return emit_alu_op2_int(instr, op2_min_int);
   case nir_op_imul: return emit_alu_trans_op2(instr, op2_mullo_int);
   case nir_op_imul_high: return emit_alu_trans_op2(instr, op2_mulhi_int);
   case nir_op_ine32: return emit_alu_op2_int(instr, op2_setne_int);
   case nir_op_ine: return emit_alu_op2_int(instr, op2_setne_int);
   case nir_op_ineg: return emit_alu_ineg(instr);
   case nir_op_inot: return emit_alu_op1(instr, op1_not_int);
   case nir_op_ior: return emit_alu_op2_int(instr, op2_or_int);
   case nir_op_ishl: return emit_alu_op2_int(instr, op2_lshl_int);
   case nir_op_ishr: return emit_alu_op2_int(instr, op2_ashr_int);
   case nir_op_isub: return emit_alu_op2_int(instr, op2_sub_int);
   case nir_op_ixor: return emit_alu_op2_int(instr, op2_xor_int);
   case nir_op_mov:return emit_mov(instr);
   case nir_op_pack_64_2x32_split: return emit_pack_64_2x32_split(instr);
   case nir_op_pack_half_2x16_split: return emit_pack_32_2x16_split(instr);
   case nir_op_slt: return emit_alu_op2(instr, op2_setgt, op2_opt_reverse);
   case nir_op_sge: return emit_alu_op2(instr, op2_setge);
   case nir_op_u2f32: return emit_alu_trans_op1(instr, op1_uint_to_flt);
   case nir_op_ubfe: return emit_alu_op3(instr, op3_bfe_uint);
   case nir_op_ufind_msb_rev: return emit_alu_op1(instr, op1_ffbh_uint);
   case nir_op_uge32: return emit_alu_op2_int(instr, op2_setge_uint);
   case nir_op_uge: return emit_alu_op2_int(instr, op2_setge_uint);
   case nir_op_ult32: return emit_alu_op2_int(instr, op2_setgt_uint, op2_opt_reverse);
   case nir_op_ult: return emit_alu_op2_int(instr, op2_setgt_uint, op2_opt_reverse);
   case nir_op_umad24: return emit_alu_op3(instr, op3_muladd_uint24,  {0, 1, 2});
   case nir_op_umax: return emit_alu_op2_int(instr, op2_max_uint);
   case nir_op_umin: return emit_alu_op2_int(instr, op2_min_uint);
   case nir_op_umul24: return emit_alu_op2(instr, op2_mul_uint24);
   case nir_op_umul_high: return emit_alu_trans_op2(instr, op2_mulhi_uint);
   case nir_op_unpack_64_2x32_split_x: return emit_unpack_64_2x32_split(instr, 0);
   case nir_op_unpack_64_2x32_split_y: return emit_unpack_64_2x32_split(instr, 1);
   case nir_op_unpack_half_2x16_split_x: return emit_unpack_32_2x16_split_x(instr);
   case nir_op_unpack_half_2x16_split_y: return emit_unpack_32_2x16_split_y(instr);
   case nir_op_ushr: return emit_alu_op2_int(instr, op2_lshr_int);
   case nir_op_vec2: return emit_create_vec(instr, 2);
   case nir_op_vec3: return emit_create_vec(instr, 3);
   case nir_op_vec4: return emit_create_vec(instr, 4);
   default:
      return false;
   }
}

void EmitAluInstruction::preload_src(const nir_alu_instr& instr)
{
   const nir_op_info *op_info = &nir_op_infos[instr.op];
   assert(op_info->num_inputs <= 4);

   unsigned nsrc_comp = num_src_comp(instr);
   sfn_log << SfnLog::reg << "Preload:\n";
   for (unsigned i = 0; i < op_info->num_inputs; ++i) {
      for (unsigned c = 0; c < nsrc_comp; ++c) {
         m_src[i][c] = from_nir(instr.src[i], c);
         sfn_log << SfnLog::reg << " " << *m_src[i][c];

      }
      sfn_log << SfnLog::reg << "\n";
   }
   if (instr.op == nir_op_fdph) {
      m_src[1][3] = from_nir(instr.src[1], 3);
      sfn_log << SfnLog::reg << " extra:" << *m_src[1][3] << "\n";
   }

   split_constants(instr, nsrc_comp);
}

unsigned EmitAluInstruction::num_src_comp(const nir_alu_instr& instr)
{
   switch (instr.op) {
   case nir_op_fdot2:
   case nir_op_bany_inequal2:
   case nir_op_ball_iequal2:
   case nir_op_bany_fnequal2:
   case nir_op_ball_fequal2:
   case nir_op_b32any_inequal2:
   case nir_op_b32all_iequal2:
   case nir_op_b32any_fnequal2:
   case nir_op_b32all_fequal2:
   case nir_op_unpack_64_2x32_split_y:
      return 2;

   case nir_op_fdot3:
   case nir_op_bany_inequal3:
   case nir_op_ball_iequal3:
   case nir_op_bany_fnequal3:
   case nir_op_ball_fequal3:
   case nir_op_b32any_inequal3:
   case nir_op_b32all_iequal3:
   case nir_op_b32any_fnequal3:
   case nir_op_b32all_fequal3:
   case nir_op_cube_r600:
      return 3;

   case nir_op_fdot4:
   case nir_op_fdph:
   case nir_op_bany_inequal4:
   case nir_op_ball_iequal4:
   case nir_op_bany_fnequal4:
   case nir_op_ball_fequal4:
   case nir_op_b32any_inequal4:
   case nir_op_b32all_iequal4:
   case nir_op_b32any_fnequal4:
   case nir_op_b32all_fequal4:
      return 4;

   case nir_op_vec2:
   case nir_op_vec3:
   case nir_op_vec4:
      return 1;

   default:
      return nir_dest_num_components(instr.dest.dest);

   }
}

bool EmitAluInstruction::emit_cube(const nir_alu_instr& instr)
{
   AluInstruction *ir = nullptr;
   const uint16_t src0_chan[4] = {2, 2, 0, 1};
   const uint16_t src1_chan[4] = {1, 0, 2, 2};

   for (int i = 0; i < 4; ++i)  {
      ir = new AluInstruction(op2_cube, from_nir(instr.dest, i),
                              from_nir(instr.src[0], src0_chan[i]),
                              from_nir(instr.src[0], src1_chan[i]), {alu_write});
      emit_instruction(ir);
   }
   ir->set_flag(alu_last_instr);
   return true;
}

void EmitAluInstruction::split_constants(const nir_alu_instr& instr, unsigned nsrc_comp)
{
    const nir_op_info *op_info = &nir_op_infos[instr.op];
    if (op_info->num_inputs < 2)
       return;

    int nconst = 0;
    std::array<const UniformValue *,4> c;
    std::array<int,4> idx;
    for (unsigned i = 0; i < op_info->num_inputs; ++i) {
       PValue& src = m_src[i][0];
       assert(src);
       sfn_log << SfnLog::reg << "Split test " << *src;

       if (src->type() == Value::kconst) {
          c[nconst] = static_cast<const UniformValue *>(src.get());
          idx[nconst++] = i;
          sfn_log << SfnLog::reg << " is constant " << i;
       }
       sfn_log << SfnLog::reg << "\n";
    }

    if (nconst < 2)
       return;

    unsigned sel = c[0]->sel();
    unsigned kcache =  c[0]->kcache_bank();
    sfn_log << SfnLog::reg << "split " << nconst << " constants, sel[0] = " << sel; ;

    for (int i = 1; i < nconst; ++i) {
       sfn_log << "sel[" << i << "] = " <<  c[i]->sel() << "\n";
       if (c[i]->sel() != sel || c[i]->kcache_bank() != kcache) {
          AluInstruction *ir = nullptr;
          auto v = get_temp_vec4();
          for (unsigned k = 0; k < nsrc_comp; ++k) {
             ir = new AluInstruction(op1_mov, v[k], m_src[idx[i]][k], {write});
             emit_instruction(ir);
             m_src[idx[i]][k] = v[k];
          }
          make_last(ir);
       }
    }
}

bool EmitAluInstruction::emit_alu_inot(const nir_alu_instr& instr)
{
   if (instr.src[0].negate || instr.src[0].abs) {
      std::cerr << "source modifiers not supported with int ops\n";
      return false;
   }

   AluInstruction *ir = nullptr;
   for (int i = 0; i < 4 ; ++i) {
      if (instr.dest.write_mask & (1 << i)){
         ir = new AluInstruction(op1_not_int, from_nir(instr.dest, i),
                                 m_src[0][i], write);
         emit_instruction(ir);
      }
   }
   make_last(ir);
   return true;
}

bool EmitAluInstruction::emit_alu_op1(const nir_alu_instr& instr, EAluOp opcode,
                                      const AluOpFlags& flags)
{
   AluInstruction *ir = nullptr;
   for (int i = 0; i < 4 ; ++i) {
      if (instr.dest.write_mask & (1 << i)){
         ir = new AluInstruction(opcode, from_nir(instr.dest, i),
                                 m_src[0][i], write);

         if (flags.test(alu_src0_abs) || instr.src[0].abs)
            ir->set_flag(alu_src0_abs);

         if (instr.src[0].negate ^ flags.test(alu_src0_neg))
            ir->set_flag(alu_src0_neg);

         if (flags.test(alu_dst_clamp) || instr.dest.saturate)
             ir->set_flag(alu_dst_clamp);

         emit_instruction(ir);
      }
   }
   make_last(ir);

   return true;
}

bool EmitAluInstruction::emit_mov(const nir_alu_instr& instr)
{
   /* If the op is a plain move beween SSA values we can just forward
    * the register reference to the original register */
   if (instr.dest.dest.is_ssa && instr.src[0].src.is_ssa &&
       !instr.src[0].abs && !instr.src[0].negate  && !instr.dest.saturate) {
      bool result = true;
      for (int i = 0; i < 4 ; ++i) {
         if (instr.dest.write_mask & (1 << i)){
            result &= inject_register(instr.dest.dest.ssa.index, i,
                                      m_src[0][i], true);
         }
      }
      return result;
   } else {
      return emit_alu_op1(instr, op1_mov);
   }
}

bool EmitAluInstruction::emit_alu_trans_op1(const nir_alu_instr& instr, EAluOp opcode,
                                            bool absolute)
{
   AluInstruction *ir = nullptr;
   std::set<int> src_idx;

   if (get_chip_class() == CAYMAN) {
      int last_slot = (instr.dest.write_mask & 0x8) ? 4 : 3;
      for (int i = 0; i < last_slot; ++i) {
         bool write_comp = instr.dest.write_mask & (1 << i);
         ir = new AluInstruction(opcode, from_nir(instr.dest, i),
                                 m_src[0][write_comp ? i : 0], write_comp ? write : empty);
         if (absolute || instr.src[0].abs) ir->set_flag(alu_src0_abs);
         if (instr.src[0].negate) ir->set_flag(alu_src0_neg);
         if (instr.dest.saturate) ir->set_flag(alu_dst_clamp);

         if (i == (last_slot - 1)) ir->set_flag(alu_last_instr);

         emit_instruction(ir);
      }
   } else {
      for (int i = 0; i < 4 ; ++i) {
         if (instr.dest.write_mask & (1 << i)){
            ir = new AluInstruction(opcode, from_nir(instr.dest, i),
                                    m_src[0][i], last_write);
            if (absolute || instr.src[0].abs) ir->set_flag(alu_src0_abs);
            if (instr.src[0].negate) ir->set_flag(alu_src0_neg);
            if (instr.dest.saturate) ir->set_flag(alu_dst_clamp);
            emit_instruction(ir);
         }
      }
   }
   return true;
}

bool EmitAluInstruction::emit_alu_cm_trig(const nir_alu_instr& instr, EAluOp opcode)
{
   AluInstruction *ir = nullptr;
   std::set<int> src_idx;

   unsigned last_slot = (instr.dest.write_mask & 0x8) ? 4 : 3;

   for (unsigned j = 0; j < nir_dest_num_components(instr.dest.dest); ++j) {
      for (unsigned i = 0; i < last_slot; ++i) {
         bool write_comp = instr.dest.write_mask & (1 << j) && (i == j);
         ir = new AluInstruction(opcode, from_nir(instr.dest, i),
                                 m_src[0][j], write_comp ? write : empty);
         if (instr.src[0].abs) ir->set_flag(alu_src0_abs);
         if (instr.src[0].negate) ir->set_flag(alu_src0_neg);
         if (instr.dest.saturate) ir->set_flag(alu_dst_clamp);

         if (i == (last_slot - 1)) ir->set_flag(alu_last_instr);

         emit_instruction(ir);
      }
   }
   return true;
}


bool EmitAluInstruction::emit_alu_f2i32_or_u32(const nir_alu_instr& instr, EAluOp op)
{
   AluInstruction *ir = nullptr;

   if (get_chip_class() < CAYMAN) {
      std::array<PValue, 4> v;

      for (int i = 0; i < 4; ++i) {
         if (!(instr.dest.write_mask & (1 << i)))
            continue;
         v[i] = from_nir(instr.dest, i);
         ir = new AluInstruction(op1_trunc, v[i], m_src[0][i], {alu_write});
         if (instr.src[0].abs) ir->set_flag(alu_src0_abs);
         if (instr.src[0].negate) ir->set_flag(alu_src0_neg);
         emit_instruction(ir);
      }
      make_last(ir);

      for (int i = 0; i < 4; ++i) {
         if (!(instr.dest.write_mask & (1 << i)))
            continue;
         ir = new AluInstruction(op, v[i], v[i], {alu_write});
         emit_instruction(ir);
         if (op == op1_flt_to_uint)
            make_last(ir);
      }
      make_last(ir);
   } else {
      for (int i = 0; i < 4; ++i) {
         if (!(instr.dest.write_mask & (1 << i)))
            continue;
         ir = new AluInstruction(op, from_nir(instr.dest, i), m_src[0][i], {alu_write});
         if (instr.src[0].abs) ir->set_flag(alu_src0_abs);
         if (instr.src[0].negate) ir->set_flag(alu_src0_neg);
         emit_instruction(ir);
         if (op == op1_flt_to_uint)
            make_last(ir);
      }
      make_last(ir);
   }

   return true;
}

bool EmitAluInstruction::emit_alu_f2b32(const nir_alu_instr& instr)
{
   AluInstruction *ir = nullptr;
   for (int i = 0; i < 4 ; ++i) {
      if (instr.dest.write_mask & (1 << i)){
         ir = new AluInstruction(op2_setne_dx10, from_nir(instr.dest, i),
                                 m_src[0][i], literal(0.0f), write);
         emit_instruction(ir);
      }
   }
   make_last(ir);
   return true;
}

bool EmitAluInstruction::emit_b2i32(const nir_alu_instr& instr)
{
   AluInstruction *ir = nullptr;
   for (int i = 0; i < 4 ; ++i) {
      if (!(instr.dest.write_mask & (1 << i)))
         continue;

      ir = new AluInstruction(op2_and_int, from_nir(instr.dest, i),
                              m_src[0][i], Value::one_i, write);
     emit_instruction(ir);
   }
   make_last(ir);

   return true;
}

bool EmitAluInstruction::emit_pack_64_2x32_split(const nir_alu_instr& instr)
{
   AluInstruction *ir = nullptr;
   for (unsigned i = 0; i < 2; ++i) {
      if (!(instr.dest.write_mask & (1 << i)))
         continue;
     ir = new AluInstruction(op1_mov, from_nir(instr.dest, i),
                             m_src[0][i], write);
     emit_instruction(ir);
   }
   ir->set_flag(alu_last_instr);
   return true;
}

bool EmitAluInstruction::emit_unpack_64_2x32_split(const nir_alu_instr& instr, unsigned comp)
{
   emit_instruction(new AluInstruction(op1_mov, from_nir(instr.dest, 0),
                                       m_src[0][comp], last_write));
   return true;
}

bool EmitAluInstruction::emit_create_vec(const nir_alu_instr& instr, unsigned nc)
{
   AluInstruction *ir = nullptr;
   std::set<int> src_slot;
   for(unsigned i = 0; i < nc; ++i) {
      if (instr.dest.write_mask & (1 << i)){
         auto src = m_src[i][0];
         ir = new AluInstruction(op1_mov, from_nir(instr.dest, i), src, write);
         if (instr.dest.saturate) ir->set_flag(alu_dst_clamp);

         // FIXME: This is a rather crude approach to fix the problem that
         // r600 can't read from four different slots of the same component
         // here we check only for the register index
         if (src->type() == Value::gpr)
            src_slot.insert(src->sel());
         if (src_slot.size() >= 3) {
            src_slot.clear();
            ir->set_flag(alu_last_instr);
         }
         emit_instruction(ir);
      }
   }
   if (ir)
      ir->set_flag(alu_last_instr);
   return true;
}

bool EmitAluInstruction::emit_dot(const nir_alu_instr& instr, int n)
{
   const nir_alu_src& src0 = instr.src[0];
   const nir_alu_src& src1 = instr.src[1];

   AluInstruction *ir = nullptr;
   for (int i = 0; i < n ; ++i) {
      ir = new AluInstruction(op2_dot4_ieee, from_nir(instr.dest, i),
                              m_src[0][i], m_src[1][i],
                              instr.dest.write_mask & (1 << i) ? write : empty);

      if (src0.negate) ir->set_flag(alu_src0_neg);
      if (src0.abs) ir->set_flag(alu_src0_abs);
      if (src1.negate) ir->set_flag(alu_src1_neg);
      if (src1.abs) ir->set_flag(alu_src1_abs);

      if (instr.dest.saturate) ir->set_flag(alu_dst_clamp);
      emit_instruction(ir);
   }
   for (int i = n; i < 4 ; ++i) {
      ir = new AluInstruction(op2_dot4_ieee, from_nir(instr.dest, i),
                              Value::zero, Value::zero,
                              instr.dest.write_mask & (1 << i) ? write : empty);
      emit_instruction(ir);
   }

   if (ir)
      ir->set_flag(alu_last_instr);
   return true;
}

bool EmitAluInstruction::emit_fdph(const nir_alu_instr& instr)
{
   const nir_alu_src& src0 = instr.src[0];
   const nir_alu_src& src1 = instr.src[1];

   AluInstruction *ir = nullptr;
   for (int i = 0; i < 3 ; ++i) {
      ir = new AluInstruction(op2_dot4_ieee, from_nir(instr.dest, i),
                              m_src[0][i], m_src[1][i],
                              instr.dest.write_mask & (1 << i) ? write : empty);
      if (src0.negate) ir->set_flag(alu_src0_neg);
      if (src0.abs) ir->set_flag(alu_src0_abs);
      if (src1.negate) ir->set_flag(alu_src1_neg);
      if (src1.abs) ir->set_flag(alu_src1_abs);
      if (instr.dest.saturate) ir->set_flag(alu_dst_clamp);
      emit_instruction(ir);
   }

   ir = new AluInstruction(op2_dot4_ieee, from_nir(instr.dest, 3), Value::one_f,
                           m_src[1][3], (instr.dest.write_mask) & (1 << 3) ? write : empty);
   if (src1.negate) ir->set_flag(alu_src1_neg);
   if (src1.abs) ir->set_flag(alu_src1_abs);
   emit_instruction(ir);

   ir->set_flag(alu_last_instr);
   return true;

}

bool EmitAluInstruction::emit_alu_i2orf2_b1(const nir_alu_instr& instr, EAluOp op)
{
   AluInstruction *ir = nullptr;
   for (int i = 0; i < 4 ; ++i) {
      if (instr.dest.write_mask & (1 << i)) {
         ir = new AluInstruction(op, from_nir(instr.dest, i),
                                 m_src[0][i], Value::zero,
                                 write);
         emit_instruction(ir);
      }
   }
   if (ir)
      ir->set_flag(alu_last_instr);
   return true;
}

bool EmitAluInstruction::emit_alu_b2f(const nir_alu_instr& instr)
{
   AluInstruction *ir = nullptr;
   for (int i = 0; i < 4 ; ++i) {
      if (instr.dest.write_mask & (1 << i)){
         ir = new AluInstruction(op2_and_int, from_nir(instr.dest, i),
                                 m_src[0][i], Value::one_f, write);
         if (instr.src[0].negate) ir->set_flag(alu_src0_neg);
         if (instr.src[0].abs) ir->set_flag(alu_src0_abs);
         if (instr.dest.saturate) ir->set_flag(alu_dst_clamp);
         emit_instruction(ir);
      }
   }
   if (ir)
      ir->set_flag(alu_last_instr);
   return true;
}

bool EmitAluInstruction::emit_any_all_icomp(const nir_alu_instr& instr, EAluOp op, unsigned nc, bool all)
{

   AluInstruction *ir = nullptr;
   PValue v[4]; // this might need some additional temp register creation
   for (unsigned i = 0; i < 4 ; ++i)
      v[i] = from_nir(instr.dest, i);

   EAluOp combine = all ? op2_and_int : op2_or_int;

   /* For integers we can not use the modifiers, so this needs some emulation */
   /* Should actually be lowered with NIR */
   if (instr.src[0].negate == instr.src[1].negate &&
       instr.src[0].abs == instr.src[1].abs) {

      for (unsigned i = 0; i < nc ; ++i) {
         ir = new AluInstruction(op, v[i], m_src[0][i], m_src[1][i], write);
         emit_instruction(ir);
      }
      if (ir)
         ir->set_flag(alu_last_instr);
   } else {
      std::cerr << "Negate in iequal/inequal not (yet) supported\n";
      return false;
   }

   for (unsigned i = 0; i < nc/2 ; ++i) {
      ir = new AluInstruction(combine, v[2 * i], v[2 * i], v[2 * i + 1], write);
      emit_instruction(ir);
   }
   if (ir)
      ir->set_flag(alu_last_instr);

   if (nc > 2) {
      ir = new AluInstruction(combine, v[0], v[0], v[2], last_write);
      emit_instruction(ir);
   }

   return true;
}

bool EmitAluInstruction::emit_any_all_fcomp(const nir_alu_instr& instr, EAluOp op, unsigned nc, bool all)
{
   AluInstruction *ir = nullptr;
   PValue v[4]; // this might need some additional temp register creation
   for (unsigned i = 0; i < 4 ; ++i)
      v[i] = from_nir(instr.dest, i);

   for (unsigned i = 0; i < nc ; ++i) {
      ir = new AluInstruction(op, v[i], m_src[0][i], m_src[1][i], write);

      if (instr.src[0].abs)
         ir->set_flag(alu_src0_abs);
      if (instr.src[0].negate)
         ir->set_flag(alu_src0_neg);

      if (instr.src[1].abs)
         ir->set_flag(alu_src1_abs);
      if (instr.src[1].negate)
         ir->set_flag(alu_src1_neg);

      emit_instruction(ir);
   }
   if (ir)
      ir->set_flag(alu_last_instr);

   for (unsigned i = 0; i < nc ; ++i) {
      ir = new AluInstruction(op1_max4, v[i], v[i], write);
      if (all) ir->set_flag(alu_src0_neg);
      emit_instruction(ir);
   }

   for (unsigned i = nc; i < 4 ; ++i) {
      ir = new AluInstruction(op1_max4, v[i],
                              all ? Value::one_f : Value::zero, write);
      if (all)
         ir->set_flag(alu_src0_neg);

      emit_instruction(ir);
   }

   ir->set_flag(alu_last_instr);

   if (all)
      op = (op == op2_sete) ? op2_sete_dx10: op2_setne_dx10;
   else
      op = (op == op2_sete) ? op2_setne_dx10: op2_sete_dx10;

   ir = new AluInstruction(op, v[0], v[0], Value::one_f, last_write);
   if (all)
      ir->set_flag(alu_src1_neg);
   emit_instruction(ir);

   return true;
}

bool EmitAluInstruction::emit_any_all_fcomp2(const nir_alu_instr& instr, EAluOp op, bool all)
{
   AluInstruction *ir = nullptr;
   PValue v[4]; // this might need some additional temp register creation
   for (unsigned i = 0; i < 4 ; ++i)
      v[i] = from_nir(instr.dest, i);

   for (unsigned i = 0; i < 2 ; ++i) {
      ir = new AluInstruction(op, v[i], m_src[0][i], m_src[1][i], write);
      if (instr.src[0].abs)
         ir->set_flag(alu_src0_abs);
      if (instr.src[0].negate)
         ir->set_flag(alu_src0_neg);

      if (instr.src[1].abs)
         ir->set_flag(alu_src1_abs);
      if (instr.src[1].negate)
         ir->set_flag(alu_src1_neg);

      emit_instruction(ir);
   }
   if (ir)
      ir->set_flag(alu_last_instr);

   op = (op == op2_setne_dx10) ? op2_or_int: op2_and_int;
   ir = new AluInstruction(op, v[0], v[0], v[1], last_write);
   emit_instruction(ir);

   return true;
}

bool EmitAluInstruction::emit_alu_trans_op2(const nir_alu_instr& instr, EAluOp opcode)
{
   const nir_alu_src& src0 = instr.src[0];
   const nir_alu_src& src1 = instr.src[1];

   AluInstruction *ir = nullptr;

   if (get_chip_class() == CAYMAN) {
      for (int k = 0; k < 4; ++k) {
         if (instr.dest.write_mask & (1 << k)) {

            for (int i = 0; i < 4; i++) {
               ir = new AluInstruction(opcode, from_nir(instr.dest, i), m_src[0][k], m_src[1][k], (i == k) ? write : empty);
               if (src0.negate) ir->set_flag(alu_src0_neg);
               if (src0.abs) ir->set_flag(alu_src0_abs);
               if (src1.negate) ir->set_flag(alu_src1_neg);
               if (src1.abs) ir->set_flag(alu_src1_abs);
               if (instr.dest.saturate) ir->set_flag(alu_dst_clamp);
               if (i == 3) ir->set_flag(alu_last_instr);
               emit_instruction(ir);
            }
         }
      }
   } else {
      for (int i = 0; i < 4 ; ++i) {
         if (instr.dest.write_mask & (1 << i)){
            ir = new AluInstruction(opcode, from_nir(instr.dest, i), m_src[0][i], m_src[1][i], last_write);
            if (src0.negate) ir->set_flag(alu_src0_neg);
            if (src0.abs) ir->set_flag(alu_src0_abs);
            if (src1.negate) ir->set_flag(alu_src1_neg);
            if (src1.abs) ir->set_flag(alu_src1_abs);
            if (instr.dest.saturate) ir->set_flag(alu_dst_clamp);
            emit_instruction(ir);
         }
      }
   }
   return true;
}

bool EmitAluInstruction::emit_alu_op2_int(const nir_alu_instr& instr, EAluOp opcode, AluOp2Opts opts)
{

   const nir_alu_src& src0 = instr.src[0];
   const nir_alu_src& src1 = instr.src[1];

   if (src0.negate || src1.negate ||
       src0.abs || src1.abs) {
      std::cerr << "R600: don't support modifiers with integer operations";
      return false;
   }
   return emit_alu_op2(instr, opcode, opts);
}

bool EmitAluInstruction::emit_alu_op2(const nir_alu_instr& instr, EAluOp opcode, AluOp2Opts ops)
{
   const nir_alu_src *src0 = &instr.src[0];
   const nir_alu_src *src1 = &instr.src[1];

   int idx0 = 0;
   int idx1 = 1;
   if (ops & op2_opt_reverse) {
      std::swap(src0, src1);
      std::swap(idx0, idx1);
   }

   bool src1_negate = (ops & op2_opt_neg_src1) ^ src1->negate;

   AluInstruction *ir = nullptr;
   for (int i = 0; i < 4 ; ++i) {
      if (instr.dest.write_mask & (1 << i)){
         ir = new AluInstruction(opcode, from_nir(instr.dest, i),
                                 m_src[idx0][i], m_src[idx1][i], write);

         if (src0->negate) ir->set_flag(alu_src0_neg);
         if (src0->abs) ir->set_flag(alu_src0_abs);
         if (src1_negate) ir->set_flag(alu_src1_neg);
         if (src1->abs) ir->set_flag(alu_src1_abs);
         if (instr.dest.saturate) ir->set_flag(alu_dst_clamp);
         emit_instruction(ir);
      }
   }
   if (ir)
      ir->set_flag(alu_last_instr);
   return true;
}

bool EmitAluInstruction::emit_alu_op3(const nir_alu_instr& instr, EAluOp opcode,
                                      std::array<uint8_t, 3> reorder)
{
   const nir_alu_src *src[3];
   src[0] = &instr.src[reorder[0]];
   src[1] = &instr.src[reorder[1]];
   src[2] = &instr.src[reorder[2]];

   AluInstruction *ir = nullptr;
   for (int i = 0; i < 4 ; ++i) {
      if (instr.dest.write_mask & (1 << i)){
         ir = new AluInstruction(opcode, from_nir(instr.dest, i),
                                 m_src[reorder[0]][i],
                                 m_src[reorder[1]][i],
                                 m_src[reorder[2]][i],
               write);

         if (src[0]->negate) ir->set_flag(alu_src0_neg);
         if (src[1]->negate) ir->set_flag(alu_src1_neg);
         if (src[2]->negate) ir->set_flag(alu_src2_neg);

         if (instr.dest.saturate) ir->set_flag(alu_dst_clamp);
         ir->set_flag(alu_write);
         emit_instruction(ir);
      }
   }
   make_last(ir);
   return true;
}

bool EmitAluInstruction::emit_alu_ineg(const nir_alu_instr& instr)
{
   AluInstruction *ir = nullptr;
   for (int i = 0; i < 4 ; ++i) {
      if (instr.dest.write_mask & (1 << i)){
         ir = new AluInstruction(op2_sub_int, from_nir(instr.dest, i), Value::zero,
                                 m_src[0][i], write);
         emit_instruction(ir);
      }
   }
   if (ir)
      ir->set_flag(alu_last_instr);

   return true;
}

static const char swz[] = "xyzw01?_";

void EmitAluInstruction::split_alu_modifiers(const nir_alu_src& src,
                                             const GPRVector::Values& v, GPRVector::Values& out, int ncomp)
{

   AluInstruction *alu = nullptr;
   for (int i = 0; i < ncomp; ++i) {
      alu  = new AluInstruction(op1_mov,  out[i], v[i], {alu_write});
      if (src.abs)
         alu->set_flag(alu_src0_abs);
      if (src.negate)
         alu->set_flag(alu_src0_neg);
      emit_instruction(alu);
   }
   make_last(alu);
}

bool EmitAluInstruction::emit_tex_fdd(const nir_alu_instr& instr, TexInstruction::Opcode op,
                                      bool fine)
{

   GPRVector::Values v;
   std::array<int, 4> writemask = {0,1,2,3};

   int ncomp = nir_dest_num_components(instr.dest.dest);
   GPRVector::Swizzle src_swz = {7,7,7,7};
   for (auto i = 0; i < ncomp; ++i)
      src_swz[i] = instr.src[0].swizzle[i];

   auto src = vec_from_nir_with_fetch_constant(instr.src[0].src, (1 << ncomp) - 1, src_swz);

   if (instr.src[0].abs || instr.src[0].negate) {
      GPRVector tmp = get_temp_vec4();
      split_alu_modifiers(instr.src[0], src.values(), tmp.values(), ncomp);
      src = tmp;
   }

   for (int i = 0; i < 4; ++i) {
      writemask[i] = (instr.dest.write_mask & (1 << i)) ? i : 7;
      v[i] = from_nir(instr.dest, (i < ncomp) ? i : 0);
   }

   /* This is querying the dreivatives of the output fb, so we would either need
    * access to the neighboring pixels or to the framebuffer. Neither is currently
    * implemented */
   GPRVector dst(v);

   auto tex = new TexInstruction(op, dst, src, 0, R600_MAX_CONST_BUFFERS, PValue());
   tex->set_dest_swizzle(writemask);

   if (fine)
      tex->set_flag(TexInstruction::grad_fine);

   emit_instruction(tex);

   return true;
}

bool EmitAluInstruction::emit_unpack_32_2x16_split_y(const nir_alu_instr& instr)
{
   auto tmp = get_temp_register();
   emit_instruction(op2_lshr_int, tmp,
   {m_src[0][0], PValue(new LiteralValue(16))},
   {alu_write, alu_last_instr});

   emit_instruction(op1_flt16_to_flt32, from_nir(instr.dest, 0),
                                  {tmp}, {alu_write, alu_last_instr});

   return true;
}

bool EmitAluInstruction::emit_unpack_32_2x16_split_x(const nir_alu_instr& instr)
{
   emit_instruction(op1_flt16_to_flt32, from_nir(instr.dest, 0),
   {m_src[0][0]},{alu_write, alu_last_instr});
   return true;
}

bool EmitAluInstruction::emit_pack_32_2x16_split(const nir_alu_instr& instr)
{
   PValue x = get_temp_register();
   PValue y = get_temp_register();

   emit_instruction(op1_flt32_to_flt16, x,{m_src[0][0]},{alu_write});
   emit_instruction(op1_flt32_to_flt16, y,{m_src[1][0]},{alu_write, alu_last_instr});

   emit_instruction(op2_lshl_int, y, {y, PValue(new LiteralValue(16))},{alu_write, alu_last_instr});

   emit_instruction(op2_or_int, {from_nir(instr.dest, 0)} , {x, y},{alu_write, alu_last_instr});

   return true;
}

}
