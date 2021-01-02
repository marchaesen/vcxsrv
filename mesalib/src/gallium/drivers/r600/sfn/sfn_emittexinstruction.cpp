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

#include "sfn_emittexinstruction.h"
#include "sfn_shader_base.h"
#include "sfn_instruction_fetch.h"

namespace r600 {

EmitTexInstruction::EmitTexInstruction(ShaderFromNirProcessor &processor):
   EmitInstruction (processor)
{
}

bool EmitTexInstruction::do_emit(nir_instr* instr)
{
   nir_tex_instr* ir = nir_instr_as_tex(instr);

   TexInputs src;
   if (!get_inputs(*ir, src))
      return false;

   if (ir->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
      switch (ir->op) {
      case nir_texop_tex:
         return emit_cube_tex(ir, src);
      case nir_texop_txf:
         return emit_cube_txf(ir, src);
      case nir_texop_txb:
         return emit_cube_txb(ir, src);
      case nir_texop_txl:
         return emit_cube_txl(ir, src);
      case nir_texop_txs:
         return emit_tex_txs(ir, src, {0,1,2,3});
      case nir_texop_txd:
         return emit_cube_txd(ir, src);
      case nir_texop_lod:
         return emit_cube_lod(ir, src);
      case nir_texop_tg4:
         return emit_cube_tg4(ir, src);
      case nir_texop_query_levels:
         return emit_tex_txs(ir, src, {3,7,7,7});
      default:
         return false;
      }
   } else if (ir->sampler_dim == GLSL_SAMPLER_DIM_BUF) {
      switch (ir->op) {
      case nir_texop_txf:
         return emit_buf_txf(ir, src);
      case nir_texop_txs:
         return emit_tex_txs(ir, src, {0,1,2,3});
      default:
         return false;
      }
   } else {
      switch (ir->op) {
      case nir_texop_tex:
         return emit_tex_tex(ir, src);
      case nir_texop_txf:
         return emit_tex_txf(ir, src);
      case nir_texop_txb:
         return emit_tex_txb(ir, src);
      case nir_texop_txl:
         return emit_tex_txl(ir, src);
      case nir_texop_txd:
         return emit_tex_txd(ir, src);
      case nir_texop_txs:
         return emit_tex_txs(ir, src, {0,1,2,3});
      case nir_texop_lod:
         return emit_tex_lod(ir, src);
      case nir_texop_tg4:
         return emit_tex_tg4(ir, src);
      case nir_texop_txf_ms:
         return emit_tex_txf_ms(ir, src);
      case nir_texop_query_levels:
         return emit_tex_txs(ir, src, {3,7,7,7});
      case nir_texop_texture_samples:
         return emit_tex_texture_samples(ir, src, {3,7,7,7});
      default:

         return false;
      }
   }
}

bool EmitTexInstruction::emit_cube_txf(UNUSED nir_tex_instr* instr, UNUSED TexInputs &src)
{
   return false;
}

bool EmitTexInstruction::emit_cube_txd(nir_tex_instr* instr, TexInputs& tex_src)
{

   assert(instr->src[0].src.is_ssa);

   r600::sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(instr)
                 << "' (" << __func__ << ")\n";

   auto tex_op = TexInstruction::sample_g;

   std::array<PValue, 4> v;
   for (int i = 0; i < 4; ++i)
      v[i] = from_nir(instr->dest, i);

   GPRVector cubed(v);
   emit_cube_prep(tex_src.coord, cubed, instr->is_array);

   std::array<PValue,4> dst_elms;
   std::array<PValue,4> src_elms;

   const uint16_t lookup[4] = {1, 0, 3, 2};
   for (uint16_t i = 0; i < 4; ++i) {
      dst_elms[i] = v[i];
      src_elms[i] = cubed.reg_i(lookup[i]);
   }

   GPRVector empty_dst(0, {7,7,7,7});

   if (instr->is_shadow)  {
      emit_instruction(new AluInstruction(op1_mov, src_elms[3], tex_src.comperator,
                       {alu_last_instr, alu_write}));
      tex_op = TexInstruction::sample_c_g;
   }


   PValue half(new LiteralValue(0.5f));
   for (int i = 0; i < 3; ++i) {
      emit_instruction(new AluInstruction(op2_mul_ieee, tex_src.ddx.reg_i(i), {tex_src.ddx.reg_i(i), half},
      {alu_last_instr, alu_write}));
   }
   for (int i = 0; i < 3; ++i) {
      emit_instruction(new AluInstruction(op2_mul_ieee, tex_src.ddy.reg_i(i), {tex_src.ddy.reg_i(i), half},
      {alu_last_instr, alu_write}));
   }

   auto sampler = get_samplerr_id(instr->sampler_index, tex_src.sampler_deref);
   assert(!sampler.indirect);

   TexInstruction *irgh = new TexInstruction(TexInstruction::set_gradient_h, empty_dst, tex_src.ddx,
                                             sampler.id, sampler.id + R600_MAX_CONST_BUFFERS, tex_src.sampler_offset);
   irgh->set_dest_swizzle({7,7,7,7});

   TexInstruction *irgv = new TexInstruction(TexInstruction::set_gradient_v, empty_dst, tex_src.ddy,
                           sampler.id, sampler.id + R600_MAX_CONST_BUFFERS, tex_src.sampler_offset);
   irgv->set_dest_swizzle({7,7,7,7});

   GPRVector dst(dst_elms);
   GPRVector src(src_elms);
   TexInstruction *ir = new TexInstruction(tex_op, dst, src, instr->sampler_index,
                                 sampler.id + R600_MAX_CONST_BUFFERS, tex_src.sampler_offset);

   set_rect_coordinate_flags(instr, ir);
   //set_offsets(ir, tex_src.offset);

   emit_instruction(irgh);
   emit_instruction(irgv);
   emit_instruction(ir);
   return true;
}


bool EmitTexInstruction::emit_cube_txl(nir_tex_instr* instr, TexInputs& tex_src)
{
   assert(instr->src[0].src.is_ssa);

   if (instr->is_shadow)
      return false;

   r600::sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(instr)
                 << "' (" << __func__ << ")\n";

   std::array<PValue, 4> v;
   for (int i = 0; i < 4; ++i)
      v[i] = from_nir(instr->dest, i);

   GPRVector cubed(v);
   emit_cube_prep(tex_src.coord, cubed, instr->is_array);

   std::array<PValue,4> dst_elms;
   std::array<PValue,4> src_elms;

   const uint16_t lookup[4] = {1, 0, 3, 2};
   for (uint16_t i = 0; i < 4; ++i) {
      dst_elms[i] = v[i];
      src_elms[i] = cubed.reg_i(lookup[i]);
   }

   auto *ir = new AluInstruction(op1_mov, src_elms[3], tex_src.lod,
                                 {alu_last_instr, alu_write});
   emit_instruction(ir);

   GPRVector src(src_elms);
   GPRVector dst(dst_elms);

   auto sampler = get_samplerr_id(instr->sampler_index, tex_src.sampler_deref);
   assert(!sampler.indirect);

   auto tir = new TexInstruction(TexInstruction::sample_l, dst, src,
                                 sampler.id,sampler.id + R600_MAX_CONST_BUFFERS,
                                 tex_src.sampler_offset);

   if (instr->is_array)
      tir->set_flag(TexInstruction::z_unnormalized);

   emit_instruction(tir);
   return true;
}

bool EmitTexInstruction::emit_cube_lod(nir_tex_instr* instr, TexInputs& src)
{
   auto tex_op = TexInstruction::get_tex_lod;

   std::array<PValue, 4> v;
   for (int i = 0; i < 4; ++i)
      v[i] = from_nir(instr->dest, i);

   GPRVector cubed(v);
   emit_cube_prep(src.coord, cubed, instr->is_array);

   auto sampler = get_samplerr_id(instr->sampler_index, src.sampler_deref);
   assert(!sampler.indirect);

   auto dst = make_dest(*instr);
   auto irt = new TexInstruction(tex_op, dst, cubed, sampler.id,
                                 sampler.id + R600_MAX_CONST_BUFFERS,
                                 src.sampler_offset);

   emit_instruction(irt);
   return true;

}


bool EmitTexInstruction::emit_cube_txb(nir_tex_instr* instr, TexInputs& tex_src)
{
   assert(instr->src[0].src.is_ssa);

   r600::sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(instr)
                 << "' (" << __func__ << ")\n";

   std::array<PValue, 4> v;
   for (int i = 0; i < 4; ++i)
      v[i] = from_nir(instr->dest, i);

   GPRVector cubed(v);
   emit_cube_prep(tex_src.coord, cubed, instr->is_array);

   std::array<PValue,4> dst_elms;
   std::array<PValue,4> src_elms;

   const uint16_t lookup[4] = {1, 0, 3, 2};
   for (uint16_t i = 0; i < 4; ++i) {
      dst_elms[i] = v[i];
      src_elms[i] = v[lookup[i]];
   }

   GPRVector src(src_elms);
   GPRVector dst(dst_elms);

   auto tex_op = TexInstruction::sample_lb;
   if (!instr->is_shadow)  {
      emit_instruction(new AluInstruction(op1_mov, src_elms[3], tex_src.bias,
                                          {alu_last_instr, alu_write}));
   } else {
      emit_instruction(new AluInstruction(op1_mov, src_elms[3], tex_src.comperator,
                       {alu_last_instr, alu_write}));
      tex_op = TexInstruction::sample_c_lb;
   }

   auto sampler = get_samplerr_id(instr->sampler_index, tex_src.sampler_deref);
   assert(!sampler.indirect && "Indirect sampler selection not yet supported");

   auto tir = new TexInstruction(tex_op, dst, src,
                                 sampler.id,
                                 sampler.id + R600_MAX_CONST_BUFFERS, tex_src.sampler_offset);
   emit_instruction(tir);
   return true;

}

bool EmitTexInstruction::emit_cube_tex(nir_tex_instr* instr, TexInputs& tex_src)
{
   std::array<PValue, 4> v;
   for (int i = 0; i < 4; ++i)
      v[i] = from_nir(instr->dest, i);

   auto tex_op = TexInstruction::sample;
   GPRVector cubed(v);
   emit_cube_prep(tex_src.coord, cubed, instr->is_array);

   std::array<PValue,4> dst_elms;
   std::array<PValue,4> src_elms;

   const uint16_t lookup[4] = {1, 0, 3, 2};
   for (uint16_t i = 0; i < 4; ++i) {
      dst_elms[i] = v[i];
      src_elms[i] = v[lookup[i]];
   }

   if (instr->is_shadow)  {
      emit_instruction(new AluInstruction(op1_mov, src_elms[3], tex_src.comperator,
                       {alu_last_instr, alu_write}));
      tex_op = TexInstruction::sample_c;
   }

   GPRVector dst(dst_elms);
   GPRVector src(src_elms);

   auto sampler = get_samplerr_id(instr->sampler_index, tex_src.sampler_deref);
   assert(!sampler.indirect && "Indirect sampler selection not yet supported");

   auto tir = new TexInstruction(tex_op, dst, src,
                                 sampler.id,
                                 sampler.id + R600_MAX_CONST_BUFFERS, tex_src.sampler_offset);
   if (instr->is_array)
      tir->set_flag(TexInstruction::z_unnormalized);

   emit_instruction(tir);
   return true;

}

bool EmitTexInstruction::emit_cube_prep(const GPRVector& coord, GPRVector& cubed, bool is_array)
{
   AluInstruction *ir = nullptr;
   const uint16_t src0_chan[4] = {2, 2, 0, 1};
   const uint16_t src1_chan[4] = {1, 0, 2, 2};

   for (int i = 0; i < 4; ++i)  {
      ir = new AluInstruction(op2_cube, cubed.reg_i(i), coord.reg_i(src0_chan[i]),
                              coord.reg_i(src1_chan[i]), {alu_write});

      emit_instruction(ir);
   }
   ir->set_flag(alu_last_instr);

   ir = new AluInstruction(op1_recip_ieee, cubed.reg_i(2), cubed.reg_i(2), {alu_write, alu_last_instr});
   ir->set_flag(alu_src0_abs);
   emit_instruction(ir);

   PValue one_p_5(new LiteralValue(1.5f));
   for (int i = 0; i < 2; ++i)  {
      ir = new AluInstruction(op3_muladd, cubed.reg_i(i), cubed.reg_i(i), cubed.reg_i(2),
                              one_p_5, {alu_write});
      emit_instruction(ir);
   }
   ir->set_flag(alu_last_instr);

   if (is_array) {
      auto face = cubed.reg_i(3);
      PValue array_index = get_temp_register();

      ir = new AluInstruction(op1_rndne, array_index, coord.reg_i(3), {alu_write, alu_last_instr});
      emit_instruction(ir);

      ir = new AluInstruction(op2_max, array_index, {array_index, Value::zero}, {alu_write, alu_last_instr});
      emit_instruction(ir);

      ir = new AluInstruction(op3_muladd, face, {array_index, PValue (new LiteralValue(8.0f)), face},
                              {alu_write, alu_last_instr});
      emit_instruction(ir);
   }

   return true;
}

bool EmitTexInstruction::emit_buf_txf(nir_tex_instr* instr, TexInputs &src)
{
   auto dst = make_dest(*instr);

   auto ir = new FetchInstruction(vc_fetch, no_index_offset, dst, src.coord.reg_i(0), 0,
                                  instr->texture_index +  R600_MAX_CONST_BUFFERS,
                                  src.texture_offset, bim_none);
   ir->set_flag(vtx_use_const_field);
   emit_instruction(ir);
   return true;
}

bool EmitTexInstruction::emit_tex_tex(nir_tex_instr* instr, TexInputs& src)
{

   r600::sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(instr)
                 << "' (" << __func__ << ")\n";

   auto tex_op = TexInstruction::sample;

   auto sampler = get_samplerr_id(instr->sampler_index, src.sampler_deref);
   assert(!sampler.indirect);

   if (instr->is_shadow)  {
      emit_instruction(new AluInstruction(op1_mov, src.coord.reg_i(3), src.comperator,
                       {alu_last_instr, alu_write}));
      tex_op = TexInstruction::sample_c;
   }

   auto dst = make_dest(*instr);
   auto irt = new TexInstruction(tex_op, dst, src.coord, sampler.id,
                                 sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);
   if (instr->is_array)
      handle_array_index(*instr, src.coord, irt);

   set_rect_coordinate_flags(instr, irt);
   set_offsets(irt, src.offset);

   emit_instruction(irt);
   return true;
}

bool EmitTexInstruction::emit_tex_txd(nir_tex_instr* instr, TexInputs& src)
{
   r600::sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(instr)
                 << "' (" << __func__ << ")\n";

   auto tex_op = TexInstruction::sample_g;
   auto dst = make_dest(*instr);

   GPRVector empty_dst(0,{7,7,7,7});

   if (instr->is_shadow)  {
      emit_instruction(new AluInstruction(op1_mov, src.coord.reg_i(3), src.comperator,
                       {alu_last_instr, alu_write}));
      tex_op = TexInstruction::sample_c_g;
   }

   auto sampler = get_samplerr_id(instr->sampler_index, src.sampler_deref);
   assert(!sampler.indirect && "Indirect sampler selection not yet supported");

   TexInstruction *irgh = new TexInstruction(TexInstruction::set_gradient_h, empty_dst, src.ddx,
                                             sampler.id,
                                             sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);
   irgh->set_dest_swizzle({7,7,7,7});

   TexInstruction *irgv = new TexInstruction(TexInstruction::set_gradient_v, empty_dst, src.ddy,
                           sampler.id, sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);
   irgv->set_dest_swizzle({7,7,7,7});

   TexInstruction *ir = new TexInstruction(tex_op, dst, src.coord, sampler.id,
                                           sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);
   if (instr->is_array)
      handle_array_index(*instr, src.coord, ir);

   set_rect_coordinate_flags(instr, ir);
   set_offsets(ir, src.offset);

   emit_instruction(irgh);
   emit_instruction(irgv);
   emit_instruction(ir);
   return true;
}

bool EmitTexInstruction::emit_tex_txf(nir_tex_instr* instr, TexInputs& src)
{
   r600::sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(instr)
                 << "' (" << __func__ << ")\n";

   auto dst = make_dest(*instr);

   if (*src.coord.reg_i(3) != *src.lod) {
      if (src.coord.sel() != src.lod->sel())
         emit_instruction(new AluInstruction(op1_mov, src.coord.reg_i(3), src.lod, {alu_write, alu_last_instr}));
      else
         src.coord.set_reg_i(3, src.lod);
   }

   auto sampler = get_samplerr_id(instr->sampler_index, src.sampler_deref);
   assert(!sampler.indirect);

   /* txf doesn't need rounding for the array index, but 1D has the array index
    * in the z component */
   if (instr->is_array && instr->sampler_dim == GLSL_SAMPLER_DIM_1D)
      src.coord.set_reg_i(2, src.coord.reg_i(1));

   auto tex_ir = new TexInstruction(TexInstruction::ld, dst, src.coord,
                                    sampler.id,
                                    sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);


   if (src.offset) {
      assert(src.offset->is_ssa);
      AluInstruction *ir = nullptr;
      for (unsigned i = 0; i < src.offset->ssa->num_components; ++i) {
         ir = new AluInstruction(op2_add_int, src.coord.reg_i(i),
                  {src.coord.reg_i(i), from_nir(*src.offset, i, i)}, {alu_write});
         emit_instruction(ir);
      }
      if (ir)
         ir->set_flag(alu_last_instr);
   }

   emit_instruction(tex_ir);
   return true;
}

bool EmitTexInstruction::emit_tex_lod(nir_tex_instr* instr, TexInputs& src)
{
   auto tex_op = TexInstruction::get_tex_lod;

   auto sampler = get_samplerr_id(instr->sampler_index, src.sampler_deref);
   assert(!sampler.indirect && "Indirect sampler selection not yet supported");

   auto dst = make_dest(*instr);
   auto irt = new TexInstruction(tex_op, dst, src.coord, sampler.id,
                                 sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);
   irt->set_dest_swizzle({1,0,7,7});
   emit_instruction(irt);
   return true;

}

bool EmitTexInstruction::emit_tex_txl(nir_tex_instr* instr, TexInputs& src)
{
   r600::sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(instr)
                 << "' (" << __func__ << ")\n";

   auto tex_op = TexInstruction::sample_l;
   if (instr->is_shadow)  {
      if (src.coord.sel() != src.comperator->sel())
         emit_instruction(new AluInstruction(op1_mov, src.coord.reg_i(2), src.comperator, {alu_write}));
      else
         src.coord.set_reg_i(2, src.comperator);
      tex_op = TexInstruction::sample_c_l;
   }

   if (src.coord.sel() != src.lod->sel())
      emit_instruction(new AluInstruction(op1_mov, src.coord.reg_i(3), src.lod, {last_write}));
   else
      src.coord.set_reg_i(3, src.lod);

   auto sampler = get_samplerr_id(instr->sampler_index, src.sampler_deref);
   assert(!sampler.indirect && "Indirect sampler selection not yet supported");

   auto dst = make_dest(*instr);
   auto irt = new TexInstruction(tex_op, dst, src.coord, sampler.id,
                                 sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);

   if (instr->is_array)
      handle_array_index(*instr, src.coord, irt);

   set_rect_coordinate_flags(instr, irt);
   set_offsets(irt, src.offset);

   emit_instruction(irt);
   return true;
}

bool EmitTexInstruction::emit_tex_txb(nir_tex_instr* instr, TexInputs& src)
{
   auto tex_op = TexInstruction::sample_lb;

   std::array<uint8_t, 4> in_swizzle = {0,1,2,3};

   if (instr->is_shadow) {
      if (src.coord.sel() != src.comperator->sel())
         emit_instruction(new AluInstruction(op1_mov, src.coord.reg_i(2), src.comperator, {alu_write}));
      else
         src.coord.set_reg_i(2, src.comperator);
      tex_op = TexInstruction::sample_c_lb;
   }

   if (src.coord.sel() != src.bias->sel())
      emit_instruction(new AluInstruction(op1_mov, src.coord.reg_i(3), src.bias, {last_write}));
   else
      src.coord.set_reg_i(3, src.bias);

   GPRVector tex_src(src.coord, in_swizzle);

   auto sampler = get_samplerr_id(instr->sampler_index, src.sampler_deref);
   assert(!sampler.indirect && "Indirect sampler selection not yet supported");

   auto dst = make_dest(*instr);
   auto irt = new TexInstruction(tex_op, dst, tex_src, sampler.id,
                                 sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);
   if (instr->is_array)
      handle_array_index(*instr, tex_src, irt);

   set_rect_coordinate_flags(instr, irt);
   set_offsets(irt, src.offset);

   emit_instruction(irt);
   return true;
}

bool EmitTexInstruction::emit_tex_txs(nir_tex_instr* instr, TexInputs& tex_src,
                                      const std::array<int,4>& dest_swz)
{
   std::array<PValue,4> dst_elms;
   std::array<PValue,4> src_elms;

   for (uint16_t i = 0; i < 4; ++i) {
      dst_elms[i] = from_nir(instr->dest, (i < instr->dest.ssa.num_components) ? i : 7);
   }

   GPRVector dst(dst_elms);

   if (instr->sampler_dim == GLSL_SAMPLER_DIM_BUF) {
      emit_instruction(new FetchInstruction(dst, PValue(new GPRValue(0, 7)),
                       instr->sampler_index + R600_MAX_CONST_BUFFERS,
                       bim_none));
   } else {
      for (uint16_t i = 0; i < 4; ++i)
         src_elms[i] =  tex_src.lod;
      GPRVector src(src_elms);

      auto sampler = get_samplerr_id(instr->sampler_index, tex_src.sampler_deref);
      assert(!sampler.indirect && "Indirect sampler selection not yet supported");

      auto ir = new TexInstruction(TexInstruction::get_resinfo, dst, src,
                                   sampler.id,
                                   sampler.id + R600_MAX_CONST_BUFFERS, tex_src.sampler_offset);
      ir->set_dest_swizzle(dest_swz);
      emit_instruction(ir);
   }

   return true;

}

bool EmitTexInstruction::emit_tex_texture_samples(nir_tex_instr* instr, TexInputs& src,
                                                  const std::array<int, 4> &dest_swz)
{
   GPRVector dest = vec_from_nir(instr->dest, nir_dest_num_components(instr->dest));
   GPRVector help{0,{4,4,4,4}};

   auto dyn_offset = PValue();
   int res_id = R600_MAX_CONST_BUFFERS + instr->sampler_index;

   auto ir = new TexInstruction(TexInstruction::get_nsampled, dest, help,
                                0, res_id, src.sampler_offset);
   ir->set_dest_swizzle(dest_swz);
   emit_instruction(ir);
   return true;
}

bool EmitTexInstruction::emit_tex_tg4(nir_tex_instr* instr, TexInputs& src)
{
   r600::sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(instr)
                 << "' (" << __func__ << ")\n";

   TexInstruction *set_ofs = nullptr;

   auto tex_op = TexInstruction::gather4;

   if (instr->is_shadow)  {
      emit_instruction(new AluInstruction(op1_mov, src.coord.reg_i(3), src.comperator,
                       {alu_last_instr, alu_write}));
      tex_op = TexInstruction::gather4_c;
   }

   auto sampler = get_samplerr_id(instr->sampler_index, src.sampler_deref);
   assert(!sampler.indirect && "Indirect sampler selection not yet supported");

   bool literal_offset = false;
   if (src.offset) {
      literal_offset =  nir_src_as_const_value(*src.offset) != 0;
      r600::sfn_log << SfnLog::tex << " really have offsets and they are " <<
                       (literal_offset ? "literal" : "varying") <<
                       "\n";

      if (!literal_offset) {
         GPRVector::Swizzle swizzle = {4,4,4,4};
         for (unsigned i = 0; i < instr->coord_components; ++i)
            swizzle[i] = i;

         int noffsets = instr->coord_components;
         if (instr->is_array)
            --noffsets;

         auto ofs = vec_from_nir_with_fetch_constant(*src.offset,
                                                     ( 1 << noffsets) - 1,
                                                     swizzle);
         GPRVector dummy(0, {7,7,7,7});
         tex_op = (tex_op == TexInstruction::gather4_c) ?
                     TexInstruction::gather4_c_o : TexInstruction::gather4_o;

         set_ofs = new TexInstruction(TexInstruction::set_offsets, dummy,
                                           ofs, sampler.id,
                                      sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);
         set_ofs->set_dest_swizzle({7,7,7,7});
      }
   }


   /* pre CAYMAN needs swizzle */
   auto dst = make_dest(*instr);
   auto irt = new TexInstruction(tex_op, dst, src.coord, sampler.id,
                                 sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);

   irt->set_dest_swizzle({1,2,0,3});
   irt->set_gather_comp(instr->component);

   if (instr->is_array)
      handle_array_index(*instr, src.coord, irt);

   if (literal_offset) {
      r600::sfn_log << SfnLog::tex << "emit literal offsets\n";
      set_offsets(irt, src.offset);
   }

   set_rect_coordinate_flags(instr, irt);

   if (set_ofs)
      emit_instruction(set_ofs);

   emit_instruction(irt);
   return true;
}

bool EmitTexInstruction::emit_cube_tg4(nir_tex_instr* instr, TexInputs& tex_src)
{
   std::array<PValue, 4> v;
   for (int i = 0; i < 4; ++i)
      v[i] = from_nir(instr->dest, i);

   auto tex_op = TexInstruction::gather4;
   GPRVector cubed(v);
   emit_cube_prep(tex_src.coord, cubed, instr->is_array);

   std::array<PValue,4> dst_elms;
   std::array<PValue,4> src_elms;

   const uint16_t lookup[4] = {1, 0, 3, 2};
   for (uint16_t i = 0; i < 4; ++i) {
      dst_elms[i] = v[i];
      src_elms[i] = v[lookup[i]];
   }

   if (instr->is_shadow)  {
      emit_instruction(new AluInstruction(op1_mov, src_elms[3], tex_src.comperator,
                       {alu_last_instr, alu_write}));
      tex_op = TexInstruction::gather4_c;
   }

   GPRVector dst(dst_elms);
   GPRVector src(src_elms);

   auto sampler = get_samplerr_id(instr->sampler_index, tex_src.sampler_deref);
   assert(!sampler.indirect && "Indirect sampler selection not yet supported");

   auto tir = new TexInstruction(tex_op, dst, src, sampler.id,
                                 sampler.id + R600_MAX_CONST_BUFFERS, tex_src.sampler_offset);

   tir->set_gather_comp(instr->component);

   tir->set_dest_swizzle({1, 2, 0, 3});

   if (instr->is_array)
      tir->set_flag(TexInstruction::z_unnormalized);

   emit_instruction(tir);
   return true;
}

bool EmitTexInstruction::emit_tex_txf_ms(nir_tex_instr* instr, TexInputs& src)
{
   assert(instr->src[0].src.is_ssa);

   r600::sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(instr)
                 << "' (" << __func__ << ")\n";

   auto sampler = get_samplerr_id(instr->sampler_index, src.sampler_deref);
   assert(!sampler.indirect && "Indirect sampler selection not yet supported");

   int sample_id = allocate_temp_register();

   GPRVector sample_id_dest(sample_id, {0,7,7,7});
   PValue help(new GPRValue(sample_id, 1));

   /* FIXME: Texture destination registers must be handled differently,
    * because the swizzle identfies which source componnet has to be written
    * at a certain position, and the target register is actually different.
    * At this point we just add a helper register, but for later work (scheduling
    * and optimization on the r600 IR level, this needs to be implemented
    * differently */


   emit_instruction(new AluInstruction(op1_mov, src.coord.reg_i(3),
                                       src.ms_index,
                                       {alu_write, alu_last_instr}));

   auto tex_sample_id_ir = new TexInstruction(TexInstruction::ld, sample_id_dest, src.coord,
                                              sampler.id,
                                              sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);
   tex_sample_id_ir->set_flag(TexInstruction::x_unnormalized);
   tex_sample_id_ir->set_flag(TexInstruction::y_unnormalized);
   tex_sample_id_ir->set_flag(TexInstruction::z_unnormalized);
   tex_sample_id_ir->set_flag(TexInstruction::w_unnormalized);
   tex_sample_id_ir->set_inst_mode(1);

   emit_instruction(tex_sample_id_ir);


   if (src.ms_index->type() != Value::literal ||
       static_cast<const LiteralValue&>(*src.ms_index).value() != 0) {
      emit_instruction(new AluInstruction(op2_lshl_int, help,
                                          src.ms_index, literal(2),
      {alu_write, alu_last_instr}));

      emit_instruction(new AluInstruction(op2_lshr_int, sample_id_dest.reg_i(0),
                                          {sample_id_dest.reg_i(0), help},
                                          {alu_write, alu_last_instr}));
   }

   emit_instruction(new AluInstruction(op2_and_int, src.coord.reg_i(3),
                                       {sample_id_dest.reg_i(0), PValue(new LiteralValue(15))},
                                       {alu_write, alu_last_instr}));

   auto dst = make_dest(*instr);

   /* txf doesn't need rounding for the array index, but 1D has the array index
    * in the z component */
   if (instr->is_array && instr->sampler_dim == GLSL_SAMPLER_DIM_1D)
      src.coord.set_reg_i(2, src.coord.reg_i(1));

   auto tex_ir = new TexInstruction(TexInstruction::ld, dst, src.coord,
                                    sampler.id,
                                    sampler.id + R600_MAX_CONST_BUFFERS, src.sampler_offset);


   if (src.offset) {
      assert(src.offset->is_ssa);
      AluInstruction *ir = nullptr;
      for (unsigned i = 0; i < src.offset->ssa->num_components; ++i) {
         ir = new AluInstruction(op2_add_int, src.coord.reg_i(i),
                  {src.coord.reg_i(i), from_nir(*src.offset, i, i)}, {alu_write});
         emit_instruction(ir);
      }
      if (ir)
         ir->set_flag(alu_last_instr);
   }

   emit_instruction(tex_ir);
   return true;
}

bool EmitTexInstruction::get_inputs(const nir_tex_instr& instr, TexInputs &src)
{
   sfn_log << SfnLog::tex << "Get Inputs with " << instr.coord_components << " components\n";

   unsigned grad_components = instr.coord_components;
   if (instr.is_array)
      --grad_components;


   src.offset = nullptr;
   bool retval = true;
   for (unsigned i = 0; i < instr.num_srcs; ++i) {
      switch (instr.src[i].src_type) {
      case nir_tex_src_bias:
         src.bias = from_nir(instr.src[i], 0);
         break;

      case nir_tex_src_coord: {
         src.coord = vec_from_nir_with_fetch_constant(instr.src[i].src,
                                                      (1 << instr.coord_components) - 1,
         {0,1,2,3});
      } break;
      case nir_tex_src_comparator:
         src.comperator = from_nir(instr.src[i], 0);
         break;
      case nir_tex_src_ddx: {
         sfn_log << SfnLog::tex << "Get DDX ";
         src.ddx = vec_from_nir_with_fetch_constant(instr.src[i].src,
                                                    (1 << grad_components) - 1,
                                                    swizzle_from_comps(grad_components));
         sfn_log << SfnLog::tex << src.ddx << "\n";
      } break;
      case nir_tex_src_ddy:{
         sfn_log << SfnLog::tex << "Get DDY ";
         src.ddy = vec_from_nir_with_fetch_constant(instr.src[i].src,
                                                    (1 << grad_components) - 1,
                                                    swizzle_from_comps(grad_components));
         sfn_log << SfnLog::tex << src.ddy << "\n";
      }  break;
      case nir_tex_src_lod:
         src.lod = from_nir_with_fetch_constant(instr.src[i].src, 0);
         break;
      case nir_tex_src_offset:
         sfn_log << SfnLog::tex << "  -- Find offset\n";
         src.offset = &instr.src[i].src;
         break;
      case nir_tex_src_sampler_deref:
         src.sampler_deref = get_deref_location(instr.src[i].src);
         break;
      case nir_tex_src_texture_deref:
         src.texture_deref = get_deref_location(instr.src[i].src);
         break;
      case nir_tex_src_ms_index:
         src.ms_index = from_nir(instr.src[i], 0);
         break;
      case nir_tex_src_texture_offset:
         src.texture_offset = from_nir(instr.src[i], 0);
         break;
      case nir_tex_src_sampler_offset:
         src.sampler_offset = from_nir(instr.src[i], 0);
         break;
      case nir_tex_src_plane:
      case nir_tex_src_projector:
      case nir_tex_src_min_lod:
      case nir_tex_src_ms_mcs:
      default:
         sfn_log << SfnLog::tex << "Texture source type " <<  instr.src[i].src_type << " not supported\n";
         retval = false;
      }
   }
   return retval;
}

GPRVector EmitTexInstruction::make_dest(nir_tex_instr& instr)
{
   int num_dest_components = instr.dest.is_ssa ? instr.dest.ssa.num_components :
                                                 instr.dest.reg.reg->num_components;
   std::array<PValue,4> dst_elms;
   for (uint16_t i = 0; i < 4; ++i)
      dst_elms[i] = from_nir(instr.dest, (i < num_dest_components) ? i : 7);
   return GPRVector(dst_elms);
}


GPRVector EmitTexInstruction::make_dest(nir_tex_instr& instr,
                                        const std::array<int, 4>& swizzle)
{
   int num_dest_components = instr.dest.is_ssa ? instr.dest.ssa.num_components :
                                                 instr.dest.reg.reg->num_components;
   std::array<PValue,4> dst_elms;
   for (uint16_t i = 0; i < 4; ++i) {
      int k = swizzle[i];
      dst_elms[i] = from_nir(instr.dest, (k < num_dest_components) ? k : 7);
   }
   return GPRVector(dst_elms);
}

void EmitTexInstruction::set_rect_coordinate_flags(nir_tex_instr* instr,
                                                   TexInstruction* ir) const
{
   if (instr->sampler_dim == GLSL_SAMPLER_DIM_RECT) {
      ir->set_flag(TexInstruction::x_unnormalized);
      ir->set_flag(TexInstruction::y_unnormalized);
   }
}

void EmitTexInstruction::set_offsets(TexInstruction* ir, nir_src *offset)
{
   if (!offset)
      return;

   assert(offset->is_ssa);
   auto literal = nir_src_as_const_value(*offset);
   assert(literal);

   for (int i = 0; i < offset->ssa->num_components; ++i) {
      ir->set_offset(i, literal[i].i32);
   }
}

void EmitTexInstruction::handle_array_index(const nir_tex_instr& instr, const GPRVector& src, TexInstruction *ir)
{
   int src_idx = instr.sampler_dim == GLSL_SAMPLER_DIM_1D ? 1 : 2;
   emit_instruction(new AluInstruction(op1_rndne, src.reg_i(2), src.reg_i(src_idx),
                                       {alu_last_instr, alu_write}));
   ir->set_flag(TexInstruction::z_unnormalized);
}

EmitTexInstruction::SamplerId
EmitTexInstruction::get_samplerr_id(int sampler_id, const nir_variable *deref)
{
   EmitTexInstruction::SamplerId result = {sampler_id, false};

   if (deref) {
      assert(glsl_type_is_sampler(deref->type));
      result.id = deref->data.binding;
   }
   return result;
}

EmitTexInstruction::TexInputs::TexInputs():
   sampler_deref(nullptr),
   texture_deref(nullptr),
   offset(nullptr)
{
}

}
