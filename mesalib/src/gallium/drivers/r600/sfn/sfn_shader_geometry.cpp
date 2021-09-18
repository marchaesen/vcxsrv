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

#include "sfn_shader_geometry.h"
#include "sfn_instruction_misc.h"
#include "sfn_instruction_fetch.h"
#include "sfn_shaderio.h"

namespace r600 {

GeometryShaderFromNir::GeometryShaderFromNir(r600_pipe_shader *sh,
                                             r600_pipe_shader_selector &sel,
                                             const r600_shader_key &key,
                                             enum chip_class chip_class):
   VertexStage(PIPE_SHADER_GEOMETRY, sel, sh->shader,
               sh->scratch_space_needed, chip_class, key.gs.first_atomic_counter),
   m_pipe_shader(sh),
   m_so_info(&sel.so),
   m_first_vertex_emitted(false),
   m_offset(0),
   m_next_input_ring_offset(0),
   m_key(key),
   m_clip_dist_mask(0),
   m_cur_ring_output(0),
   m_gs_tri_strip_adj_fix(false),
   m_input_mask(0)
{
   sh_info().atomic_base = key.gs.first_atomic_counter;
}

bool GeometryShaderFromNir::emit_store(nir_intrinsic_instr* instr)
{
   auto location = nir_intrinsic_io_semantics(instr).location;
   auto index = nir_src_as_const_value(instr->src[1]);
   assert(index);
   auto driver_location = nir_intrinsic_base(instr) + index->u32;

   uint32_t write_mask = nir_intrinsic_write_mask(instr);
   GPRVector::Swizzle swz = swizzle_from_mask(write_mask);

   auto out_value = vec_from_nir_with_fetch_constant(instr->src[0], write_mask, swz, true);

   sh_info().output[driver_location].write_mask = write_mask;

   auto ir = new MemRingOutIntruction(cf_mem_ring, mem_write_ind, out_value,
                                      4 * driver_location,
                                      instr->num_components, m_export_base[0]);
   streamout_data[location] = ir;

   return true;
}

bool GeometryShaderFromNir::scan_sysvalue_access(UNUSED nir_instr *instr)
{
   if (instr->type != nir_instr_type_intrinsic)
      return true;

   nir_intrinsic_instr *ii =  nir_instr_as_intrinsic(instr);

   switch (ii->intrinsic) {
   case nir_intrinsic_store_output:
      return process_store_output(ii);
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_per_vertex_input:
      return process_load_input(ii);
   default:
      return true;
   }
}

bool GeometryShaderFromNir::process_store_output(nir_intrinsic_instr* instr)
{
   auto location = nir_intrinsic_io_semantics(instr).location;
   auto index = nir_src_as_const_value(instr->src[1]);
   assert(index);

   auto driver_location = nir_intrinsic_base(instr) + index->u32;

   if (location == VARYING_SLOT_COL0 ||
       location == VARYING_SLOT_COL1 ||
       (location >= VARYING_SLOT_VAR0 &&
       location <= VARYING_SLOT_VAR31) ||
       (location >= VARYING_SLOT_TEX0 &&
       location <= VARYING_SLOT_TEX7) ||
       location == VARYING_SLOT_BFC0 ||
       location == VARYING_SLOT_BFC1 ||
       location == VARYING_SLOT_PNTC ||
       location == VARYING_SLOT_CLIP_VERTEX ||
       location == VARYING_SLOT_CLIP_DIST0 ||
       location == VARYING_SLOT_CLIP_DIST1 ||
       location == VARYING_SLOT_PRIMITIVE_ID ||
       location == VARYING_SLOT_POS ||
       location == VARYING_SLOT_PSIZ ||
       location == VARYING_SLOT_LAYER ||
       location == VARYING_SLOT_VIEWPORT ||
       location == VARYING_SLOT_FOGC) {
      r600_shader_io& io = sh_info().output[driver_location];

      auto semantic = r600_get_varying_semantic(location);
      io.name = semantic.first;
      io.sid = semantic.second;

      evaluate_spi_sid(io);

      if (sh_info().noutput <= driver_location)
         sh_info().noutput = driver_location + 1;

      if (location == VARYING_SLOT_CLIP_DIST0 ||
          location == VARYING_SLOT_CLIP_DIST1) {
         m_clip_dist_mask |= 1 << (location - VARYING_SLOT_CLIP_DIST0);
      }

      if (location == VARYING_SLOT_VIEWPORT) {
         sh_info().vs_out_viewport = 1;
         sh_info().vs_out_misc_write = 1;
      }
      return true;
   }
   return false;
}

bool GeometryShaderFromNir::process_load_input(nir_intrinsic_instr* instr)
{
   auto location = nir_intrinsic_io_semantics(instr).location;
   auto index = nir_src_as_const_value(instr->src[1]);
   assert(index);

   auto driver_location = nir_intrinsic_base(instr) + index->u32;

   if (location == VARYING_SLOT_POS ||
       location == VARYING_SLOT_PSIZ ||
       location == VARYING_SLOT_FOGC ||
       location == VARYING_SLOT_CLIP_VERTEX ||
       location == VARYING_SLOT_CLIP_DIST0 ||
       location == VARYING_SLOT_CLIP_DIST1 ||
       location == VARYING_SLOT_COL0 ||
       location == VARYING_SLOT_COL1 ||
       location == VARYING_SLOT_BFC0 ||
       location == VARYING_SLOT_BFC1 ||
       location == VARYING_SLOT_PNTC ||
       (location >= VARYING_SLOT_VAR0 &&
        location <= VARYING_SLOT_VAR31) ||
       (location >= VARYING_SLOT_TEX0 &&
       location <= VARYING_SLOT_TEX7)) {

      uint64_t bit = 1ull << location;
      if (!(bit & m_input_mask)) {
         r600_shader_io& io = sh_info().input[driver_location];
         auto semantic = r600_get_varying_semantic(location);
         io.name = semantic.first;
         io.sid = semantic.second;

         io.ring_offset = 16 * driver_location;
         ++sh_info().ninput;
         m_next_input_ring_offset += 16;
         m_input_mask |= bit;
      }
      return true;
   }
   return false;
}

bool GeometryShaderFromNir::do_allocate_reserved_registers()
{
   const int sel[6] = {0, 0 ,0, 1, 1, 1};
   const int chan[6] = {0, 1 ,3, 0, 1, 2};

   increment_reserved_registers();
   increment_reserved_registers();

   /* Reserve registers used by the shaders (should check how many
    * components are actually used */
   for (int i = 0; i < 6; ++i) {
      auto reg = new GPRValue(sel[i], chan[i]);
      reg->set_as_input();
      m_per_vertex_offsets[i].reset(reg);
      inject_register(sel[i], chan[i], m_per_vertex_offsets[i], false);
   }
   auto reg = new GPRValue(0, 2);
   reg->set_as_input();
   m_primitive_id.reset(reg);
   inject_register(0, 2, m_primitive_id, false);

   reg = new GPRValue(1, 3);
   reg->set_as_input();
   m_invocation_id.reset(reg);
   inject_register(1, 3, m_invocation_id, false);

   m_export_base[0] = get_temp_register(0);
   m_export_base[1] = get_temp_register(0);
   m_export_base[2] = get_temp_register(0);
   m_export_base[3] = get_temp_register(0);
   emit_instruction(new AluInstruction(op1_mov, m_export_base[0], Value::zero, {alu_write, alu_last_instr}));
   emit_instruction(new AluInstruction(op1_mov, m_export_base[1], Value::zero, {alu_write, alu_last_instr}));
   emit_instruction(new AluInstruction(op1_mov, m_export_base[2], Value::zero, {alu_write, alu_last_instr}));
   emit_instruction(new AluInstruction(op1_mov, m_export_base[3], Value::zero, {alu_write, alu_last_instr}));

   sh_info().ring_item_sizes[0] = m_next_input_ring_offset;

   if (m_key.gs.tri_strip_adj_fix)
      emit_adj_fix();

   return true;
}

void GeometryShaderFromNir::emit_adj_fix()
{
   PValue adjhelp0(new  GPRValue(m_export_base[0]->sel(), 1));
   emit_instruction(op2_and_int, adjhelp0, {m_primitive_id, Value::one_i}, {alu_write, alu_last_instr});

   int reg_indices[6];
   int reg_chanels[6] = {1, 2, 3, 1, 2, 3};

   int rotate_indices[6] = {4, 5, 0, 1, 2, 3};

   reg_indices[0] = reg_indices[1] = reg_indices[2] = m_export_base[1]->sel();
   reg_indices[3] = reg_indices[4] = reg_indices[5] = m_export_base[2]->sel();

   std::array<PValue, 6> adjhelp;

   AluInstruction *ir = nullptr;
   for (int i = 0; i < 6; i++) {
      adjhelp[i].reset(new GPRValue(reg_indices[i], reg_chanels[i]));
      ir = new AluInstruction(op3_cnde_int, adjhelp[i],
                             {adjhelp0, m_per_vertex_offsets[i],
                              m_per_vertex_offsets[rotate_indices[i]]},
                             {alu_write});
      if ((get_chip_class() == CAYMAN && i == 2) || (i  == 3))
         ir->set_flag(alu_last_instr);
      emit_instruction(ir);
   }
   ir->set_flag(alu_last_instr);

   for (int i = 0; i < 6; i++)
      m_per_vertex_offsets[i] = adjhelp[i];
}


bool GeometryShaderFromNir::emit_intrinsic_instruction_override(nir_intrinsic_instr* instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_emit_vertex:
      return emit_vertex(instr, false);
   case nir_intrinsic_end_primitive:
      return emit_vertex(instr, true);
   case nir_intrinsic_load_primitive_id:
      return load_preloaded_value(instr->dest, 0, m_primitive_id);
   case nir_intrinsic_load_invocation_id:
      return load_preloaded_value(instr->dest, 0, m_invocation_id);
   case nir_intrinsic_store_output:
      return emit_store(instr);
   case nir_intrinsic_load_per_vertex_input:
      return emit_load_per_vertex_input(instr);
   default:
      ;
   }
   return false;
}

bool GeometryShaderFromNir::emit_vertex(nir_intrinsic_instr* instr, bool cut)
{
   int stream = nir_intrinsic_stream_id(instr);
   assert(stream < 4);

   for(auto v: streamout_data) {
      if (stream == 0 || v.first != VARYING_SLOT_POS) {
         v.second->patch_ring(stream, m_export_base[stream]);
         emit_instruction(v.second);
      } else
         delete v.second;
   }
   streamout_data.clear();
   emit_instruction(new EmitVertex(stream, cut));

   if (!cut)
      emit_instruction(new AluInstruction(op2_add_int, m_export_base[stream], m_export_base[stream],
                                          PValue(new LiteralValue(sh_info().noutput)),
                                          {alu_write, alu_last_instr}));

   return true;
}

bool GeometryShaderFromNir::emit_load_per_vertex_input(nir_intrinsic_instr* instr)
{
   auto dest = vec_from_nir(instr->dest, 4);

   std::array<int, 4> swz = {7,7,7,7};
   for (unsigned i = 0; i < nir_dest_num_components(instr->dest); ++i) {
      swz[i] = i + nir_intrinsic_component(instr);
   }

   auto literal_index = nir_src_as_const_value(instr->src[0]);

   if (!literal_index) {
      sfn_log << SfnLog::err << "GS: Indirect input addressing not (yet) supported\n";
      return false;
   }
   assert(literal_index->u32 < 6);
   assert(nir_intrinsic_io_semantics(instr).num_slots == 1);

   PValue addr = m_per_vertex_offsets[literal_index->u32];
   auto fetch = new FetchInstruction(vc_fetch, no_index_offset, dest, addr,
                                     16 * nir_intrinsic_base(instr),
                                     R600_GS_RING_CONST_BUFFER, PValue(), bim_none, true);
   fetch->set_dest_swizzle(swz);

   emit_instruction(fetch);
   return true;
}

void GeometryShaderFromNir::do_finalize()
{
   if (m_clip_dist_mask) {
      int num_clip_dist = 4 * util_bitcount(m_clip_dist_mask);
      sh_info().cc_dist_mask = (1 << num_clip_dist) - 1;
      sh_info().clip_dist_write = (1 << num_clip_dist) - 1;
   }
}

}
