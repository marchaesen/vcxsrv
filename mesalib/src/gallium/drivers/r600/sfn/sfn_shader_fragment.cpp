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

#include "pipe/p_defines.h"
#include "tgsi/tgsi_from_mesa.h"
#include "sfn_shader_fragment.h"
#include "sfn_instruction_fetch.h"

namespace r600 {

FragmentShaderFromNir::FragmentShaderFromNir(const nir_shader& nir,
                                             r600_shader& sh,
                                             r600_pipe_shader_selector &sel,
                                             const r600_shader_key &key,
                                             enum chip_class chip_class):
   ShaderFromNirProcessor(PIPE_SHADER_FRAGMENT, sel, sh, nir.scratch_size, chip_class, 0),
   m_max_color_exports(MAX2(key.ps.nr_cbufs,1)),
   m_max_counted_color_exports(0),
   m_two_sided_color(key.ps.color_two_side),
   m_last_pixel_export(nullptr),
   m_nir(nir),
   m_reserved_registers(0),
   m_frag_pos_index(0),
   m_need_back_color(false),
   m_front_face_loaded(false),
   m_depth_exports(0),
   m_apply_sample_mask(key.ps.apply_sample_id_mask),
   m_dual_source_blend(key.ps.dual_source_blend),
   m_pos_input(nullptr)
{
   for (auto&  i: m_interpolator) {
      i.enabled = false;
      i.ij_index= 0;
   }

   sh_info().rat_base = key.ps.nr_cbufs;
   sh_info().atomic_base = key.ps.first_atomic_counter;
}

unsigned barycentric_ij_index(nir_intrinsic_instr *instr)
{
   unsigned index = 0;
   switch (instr->intrinsic) {
   case nir_intrinsic_load_barycentric_sample:
      index = 0;
      break;
   case nir_intrinsic_load_barycentric_at_sample:
   case nir_intrinsic_load_barycentric_at_offset:
   case nir_intrinsic_load_barycentric_pixel:
      index = 1;
      break;
   case nir_intrinsic_load_barycentric_centroid:
      index = 2;
      break;
   default:
      unreachable("Unknown interpolator intrinsic");
   }

   switch (nir_intrinsic_interp_mode(instr)) {
   case INTERP_MODE_NONE:
   case INTERP_MODE_SMOOTH:
   case INTERP_MODE_COLOR:
      return index;
   case INTERP_MODE_NOPERSPECTIVE:
      return index + 3;
   case INTERP_MODE_FLAT:
   case INTERP_MODE_EXPLICIT:
   default:
      unreachable("unknown/unsupported mode for load_interpolated");
   }
   return 0;
}

bool FragmentShaderFromNir::process_load_input(nir_intrinsic_instr *instr,
                                               bool interpolated)
{
   sfn_log << SfnLog::io << "Parse " << instr->instr        
           << "\n";

   auto index = nir_src_as_const_value(instr->src[interpolated ? 1 : 0]);
   assert(index);

   unsigned location = nir_intrinsic_io_semantics(instr).location + index->u32;
   auto semantic = r600_get_varying_semantic(location);
   tgsi_semantic name = (tgsi_semantic)semantic.first;
   unsigned sid = semantic.second;


   if (location == VARYING_SLOT_POS) {
      m_sv_values.set(es_pos);
      m_pos_input = new ShaderInputVarying(name, sid, nir_intrinsic_base(instr) + index->u32,
                                               nir_intrinsic_component(instr),
                                               nir_dest_num_components(instr->dest),
                                               TGSI_INTERPOLATE_LINEAR, TGSI_INTERPOLATE_LOC_CENTER);
      m_shaderio.add_input(m_pos_input);
      return true;
   }

   if (location == VARYING_SLOT_FACE) {
      m_sv_values.set(es_face);
      return true;
   }


   tgsi_interpolate_mode tgsi_interpolate = TGSI_INTERPOLATE_CONSTANT;
   tgsi_interpolate_loc tgsi_loc = TGSI_INTERPOLATE_LOC_CENTER;

   bool uses_interpol_at_centroid = false;

   if (interpolated) {

      glsl_interp_mode mode = INTERP_MODE_NONE;
      auto parent = nir_instr_as_intrinsic(instr->src[0].ssa->parent_instr);
      mode = (glsl_interp_mode)nir_intrinsic_interp_mode(parent);
      switch (parent->intrinsic) {
      case nir_intrinsic_load_barycentric_sample:
         tgsi_loc = TGSI_INTERPOLATE_LOC_SAMPLE;
         break;
      case nir_intrinsic_load_barycentric_at_sample:
      case nir_intrinsic_load_barycentric_at_offset:
      case nir_intrinsic_load_barycentric_pixel:
         tgsi_loc = TGSI_INTERPOLATE_LOC_CENTER;
         break;
      case nir_intrinsic_load_barycentric_centroid:
         tgsi_loc = TGSI_INTERPOLATE_LOC_CENTROID;
         uses_interpol_at_centroid = true;
         break;
      default:
         std::cerr << "Instruction " << nir_intrinsic_infos[parent->intrinsic].name << " as parent of "
                   << nir_intrinsic_infos[instr->intrinsic].name
                   << " interpolator?\n";
         assert(0);
      }

      switch (mode) {
      case INTERP_MODE_NONE:
         if (name == TGSI_SEMANTIC_COLOR) {
            tgsi_interpolate = TGSI_INTERPOLATE_COLOR;
            break;
      }
         FALLTHROUGH;
      case INTERP_MODE_SMOOTH:
         tgsi_interpolate = TGSI_INTERPOLATE_PERSPECTIVE;
         break;
      case INTERP_MODE_NOPERSPECTIVE:
         tgsi_interpolate = TGSI_INTERPOLATE_LINEAR;
         break;
      case INTERP_MODE_FLAT:
         break;
      case INTERP_MODE_COLOR:
         tgsi_interpolate = TGSI_INTERPOLATE_COLOR;
         break;
      case INTERP_MODE_EXPLICIT:
      default:
         assert(0);
      }

      m_interpolators_used.set(barycentric_ij_index(parent));

   }

   switch (name) {
   case TGSI_SEMANTIC_COLOR: {
      auto input = m_shaderio.find_varying(name, sid);
      if (!input) {
         m_shaderio.add_input(new ShaderInputColor(name, sid,
                                                   nir_intrinsic_base(instr) + index->u32,
                                                   nir_intrinsic_component(instr),
                                                   nir_dest_num_components(instr->dest),
                                                   tgsi_interpolate, tgsi_loc));
      }  else {
         if (uses_interpol_at_centroid)
            input->set_uses_interpolate_at_centroid();

         auto varying = static_cast<ShaderInputVarying&>(*input);
         varying.update_mask(nir_dest_num_components(instr->dest),
                             nir_intrinsic_component(instr));
      }

      m_need_back_color = m_two_sided_color;
      return true;
   }
   case TGSI_SEMANTIC_PRIMID:
      sh_info().gs_prim_id_input = true;
      sh_info().ps_prim_id_input = m_shaderio.inputs().size();
      FALLTHROUGH;
   case TGSI_SEMANTIC_FOG:
   case TGSI_SEMANTIC_GENERIC:
   case TGSI_SEMANTIC_TEXCOORD:
   case TGSI_SEMANTIC_LAYER:
   case TGSI_SEMANTIC_PCOORD:
   case TGSI_SEMANTIC_VIEWPORT_INDEX:
   case TGSI_SEMANTIC_CLIPDIST: {
      auto input = m_shaderio.find_varying(name, sid);
      if (!input) {
         m_shaderio.add_input(new ShaderInputVarying(name, sid, nir_intrinsic_base(instr) + index->u32,
                                                     nir_intrinsic_component(instr),
                                                     nir_dest_num_components(instr->dest),
                                                     tgsi_interpolate, tgsi_loc));
      } else {
         if (uses_interpol_at_centroid)
            input->set_uses_interpolate_at_centroid();

         auto varying = static_cast<ShaderInputVarying&>(*input);
         varying.update_mask(nir_dest_num_components(instr->dest),
                             nir_intrinsic_component(instr));
      }

      return true;
   }
   default:
      return false;
   }
}


bool FragmentShaderFromNir::scan_sysvalue_access(nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *ii =  nir_instr_as_intrinsic(instr);

      switch (ii->intrinsic) {
      case nir_intrinsic_load_front_face:
         m_sv_values.set(es_face);
         break;
      case nir_intrinsic_load_sample_mask_in:
         m_sv_values.set(es_sample_mask_in);
         break;
      case nir_intrinsic_load_sample_pos:
         m_sv_values.set(es_sample_pos);
         FALLTHROUGH;
      case nir_intrinsic_load_sample_id:
         m_sv_values.set(es_sample_id);
         break;
      case nir_intrinsic_load_helper_invocation:
         m_sv_values.set(es_helper_invocation);
         sh_info().uses_helper_invocation = true;
         break;
      case nir_intrinsic_load_input:
         return process_load_input(ii, false);
      case nir_intrinsic_load_interpolated_input: {
         return process_load_input(ii, true);
      }
      case nir_intrinsic_store_output:
         return process_store_output(ii);

      default:
         ;
      }
   }
   default:
      ;
   }
   return true;
}

bool FragmentShaderFromNir::do_allocate_reserved_registers()
{
   assert(!m_reserved_registers);

   int face_reg_index = -1;
   int sample_id_index = -1;
   // enabled interpolators based on inputs
   for (unsigned i = 0; i < s_max_interpolators; ++i) {
      if (m_interpolators_used.test(i)) {
         sfn_log << SfnLog::io << "Interpolator " << i << " test enabled\n";
         m_interpolator[i].enabled = true;
      }
   }

   // sort the varying inputs
   m_shaderio.sort_varying_inputs();

   // handle interpolators
   int num_baryc = 0;
   for (int i = 0; i < 6; ++i) {
      if (m_interpolator[i].enabled) {
         sfn_log << SfnLog::io << "Interpolator " << i << " is enabled with ij=" << num_baryc <<" \n";

         m_interpolator[i].ij_index = num_baryc;

         unsigned sel = num_baryc / 2;
         unsigned chan = 2 * (num_baryc % 2);

         auto ip_i = new GPRValue(sel, chan + 1);
         ip_i->set_as_input();
         m_interpolator[i].i.reset(ip_i);
         inject_register(sel, chan + 1, m_interpolator[i].i, false);

         auto ip_j = new GPRValue(sel, chan);
         ip_j->set_as_input();
         m_interpolator[i].j.reset(ip_j);
         inject_register(sel, chan, m_interpolator[i].j, false);

         ++num_baryc;
      }
   }
   m_reserved_registers += (num_baryc + 1) >> 1;

   if (m_sv_values.test(es_pos)) {
      m_frag_pos_index = m_reserved_registers++;
      assert(m_pos_input);
      m_pos_input->set_gpr(m_frag_pos_index);
   }

   // handle system values
   if (m_sv_values.test(es_face) || m_need_back_color) {
      face_reg_index = m_reserved_registers++;
      m_front_face_reg = std::make_shared<GPRValue>(face_reg_index,0);
      m_front_face_reg->set_as_input();
      sfn_log << SfnLog::io << "Set front_face register to " <<  *m_front_face_reg << "\n";
      inject_register(m_front_face_reg->sel(), m_front_face_reg->chan(), m_front_face_reg, false);

      m_shaderio.add_input(new ShaderInputSystemValue(TGSI_SEMANTIC_FACE, face_reg_index));
      load_front_face();
   }

   if (m_sv_values.test(es_sample_mask_in)) {
      if (face_reg_index < 0)
         face_reg_index = m_reserved_registers++;

      m_sample_mask_reg = std::make_shared<GPRValue>(face_reg_index,2);
      m_sample_mask_reg->set_as_input();
      sfn_log << SfnLog::io << "Set sample mask in register to " <<  *m_sample_mask_reg << "\n";
      sh_info().nsys_inputs = 1;
      m_shaderio.add_input(new ShaderInputSystemValue(TGSI_SEMANTIC_SAMPLEMASK, face_reg_index));
   }

   if (m_sv_values.test(es_sample_id) ||
       m_sv_values.test(es_sample_mask_in)) {
      if (sample_id_index < 0)
         sample_id_index = m_reserved_registers++;

      m_sample_id_reg = std::make_shared<GPRValue>(sample_id_index, 3);
      m_sample_id_reg->set_as_input();
      sfn_log << SfnLog::io << "Set sample id register to " <<  *m_sample_id_reg << "\n";
      sh_info().nsys_inputs++;
      m_shaderio.add_input(new ShaderInputSystemValue(TGSI_SEMANTIC_SAMPLEID, sample_id_index));
   }

   // The back color handling is not emmited in the code, so we have
   // to add the inputs here and later we also need to inject the code to set
   // the right color
   if (m_need_back_color) {
      size_t ninputs = m_shaderio.inputs().size();
      for (size_t k = 0; k < ninputs; ++k) {
         ShaderInput& i = m_shaderio.input(k);

         if (i.name() != TGSI_SEMANTIC_COLOR)
            continue;

         ShaderInputColor& col = static_cast<ShaderInputColor&>(i);

         size_t next_pos = m_shaderio.size();
         auto bcol = new ShaderInputVarying(TGSI_SEMANTIC_BCOLOR, col, next_pos);
         m_shaderio.add_input(bcol);
         col.set_back_color(next_pos);
      }
      m_shaderio.set_two_sided();
   }

   m_shaderio.update_lds_pos();

   set_reserved_registers(m_reserved_registers);

   return true;
}

void FragmentShaderFromNir::emit_shader_start()
{
   if (m_sv_values.test(es_face))
      load_front_face();

   if (m_sv_values.test(es_pos)) {
      for (int i = 0; i < 4; ++i) {
         auto v = new GPRValue(m_frag_pos_index, i);
         v->set_as_input();
         auto reg = PValue(v);
         m_frag_pos[i] = reg;
      }
   }

   if (m_sv_values.test(es_helper_invocation)) {
      m_helper_invocation = get_temp_register();
      auto dummy = PValue(new GPRValue(m_helper_invocation->sel(), 7));
      emit_instruction(new AluInstruction(op1_mov, m_helper_invocation, literal(-1), {alu_write, alu_last_instr}));
      GPRVector dst({dummy, dummy, dummy, dummy});
      std::array<int,4> swz = {7,7,7,7};
      dst.set_reg_i(m_helper_invocation->chan(), m_helper_invocation);
      swz[m_helper_invocation->chan()] = 4;

      auto vtx = new FetchInstruction(dst, m_helper_invocation,
                                      R600_BUFFER_INFO_CONST_BUFFER, bim_none);
      vtx->set_flag(vtx_vpm);
      vtx->set_flag(vtx_use_tc);
      vtx->set_dest_swizzle(swz);
      emit_instruction(vtx);
   }
}

bool FragmentShaderFromNir::process_store_output(nir_intrinsic_instr *instr)
{

   auto semantic = nir_intrinsic_io_semantics(instr);
   unsigned driver_loc = nir_intrinsic_base(instr);

   if (sh_info().noutput <= driver_loc)
      sh_info().noutput = driver_loc + 1;

   r600_shader_io& io = sh_info().output[driver_loc];
   tgsi_get_gl_frag_result_semantic(static_cast<gl_frag_result>(semantic.location),
                                    &io.name, &io.sid);

   unsigned component = nir_intrinsic_component(instr);
   io.write_mask |= nir_intrinsic_write_mask(instr) << component;

   if (semantic.location == FRAG_RESULT_COLOR && !m_dual_source_blend) {
      sh_info().fs_write_all = true;
   }

   if (semantic.location == FRAG_RESULT_COLOR ||
       (semantic.location >= FRAG_RESULT_DATA0 &&
        semantic.location <= FRAG_RESULT_DATA7))  {
      ++m_max_counted_color_exports;

      /* Hack: force dual source output handling if one color output has a
       * dual_source_blend_index > 0 */
      if (semantic.location == FRAG_RESULT_COLOR &&
          semantic.dual_source_blend_index > 0)
         m_dual_source_blend = true;

      if (m_max_counted_color_exports > 1)
         sh_info().fs_write_all = false;
      return true;
   }

   if (semantic.location == FRAG_RESULT_DEPTH ||
       semantic.location == FRAG_RESULT_STENCIL ||
       semantic.location == FRAG_RESULT_SAMPLE_MASK) {
      io.write_mask = 15;
      return true;
   }

   return false;


}

bool FragmentShaderFromNir::emit_load_sample_mask_in(nir_intrinsic_instr* instr)
{
   auto dest = from_nir(instr->dest, 0);
   assert(m_sample_id_reg);
   assert(m_sample_mask_reg);

   emit_instruction(new AluInstruction(op2_lshl_int, dest, Value::one_i, m_sample_id_reg, EmitInstruction::last_write));
   emit_instruction(new AluInstruction(op2_and_int, dest, dest, m_sample_mask_reg, EmitInstruction::last_write));
   return true;
}

bool FragmentShaderFromNir::emit_intrinsic_instruction_override(nir_intrinsic_instr* instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_sample_mask_in:
      if (m_apply_sample_mask) {
         return emit_load_sample_mask_in(instr);
      } else
         return load_preloaded_value(instr->dest, 0, m_sample_mask_reg);
   case nir_intrinsic_load_sample_id:
      return load_preloaded_value(instr->dest, 0, m_sample_id_reg);
   case nir_intrinsic_load_front_face:
      return load_preloaded_value(instr->dest, 0, m_front_face_reg);
   case nir_intrinsic_load_sample_pos:
      return emit_load_sample_pos(instr);
   case nir_intrinsic_load_helper_invocation:
      return load_preloaded_value(instr->dest, 0, m_helper_invocation);
   case nir_intrinsic_load_input:
      return emit_load_input(instr);
   case nir_intrinsic_load_barycentric_sample:
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_centroid:  {
      unsigned ij = barycentric_ij_index(instr);
      return load_preloaded_value(instr->dest, 0, m_interpolator[ij].i) &&
            load_preloaded_value(instr->dest, 1, m_interpolator[ij].j);
   }
   case nir_intrinsic_load_barycentric_at_offset:
         return load_barycentric_at_offset(instr);
   case nir_intrinsic_load_barycentric_at_sample:
      return load_barycentric_at_sample(instr);

   case nir_intrinsic_load_interpolated_input: {
      return emit_load_interpolated_input(instr);
   }
   case nir_intrinsic_store_output:
      return emit_store_output(instr);

   default:
      return false;
   }
}

bool FragmentShaderFromNir::emit_store_output(nir_intrinsic_instr* instr)
{
   auto location = nir_intrinsic_io_semantics(instr).location;

   if (location == FRAG_RESULT_COLOR)
      return emit_export_pixel(instr, m_dual_source_blend ? 1 : m_max_color_exports);

   if ((location >= FRAG_RESULT_DATA0 &&
        location <= FRAG_RESULT_DATA7) ||
       location == FRAG_RESULT_DEPTH ||
       location == FRAG_RESULT_STENCIL ||
       location == FRAG_RESULT_SAMPLE_MASK)
      return emit_export_pixel(instr, 1);

   sfn_log << SfnLog::err << "r600-NIR: Unimplemented store_output for " << location << ")\n";
   return false;

}

bool FragmentShaderFromNir::emit_load_interpolated_input(nir_intrinsic_instr* instr)
{
   unsigned loc = nir_intrinsic_io_semantics(instr).location;
   switch (loc) {
   case VARYING_SLOT_POS:
      for (unsigned i = 0; i < nir_dest_num_components(instr->dest); ++i) {
         load_preloaded_value(instr->dest, i, m_frag_pos[i]);
      }
      return true;
   case VARYING_SLOT_FACE:
      return load_preloaded_value(instr->dest, 0, m_front_face_reg);
   default:
      ;
   }

   auto param = nir_src_as_const_value(instr->src[1]);
   assert(param && "Indirect PS inputs not (yet) supported");

   auto& io = m_shaderio.input(param->u32 + nir_intrinsic_base(instr), nir_intrinsic_component(instr));
   auto dst = nir_intrinsic_component(instr) ? get_temp_vec4() : vec_from_nir(instr->dest, 4);

   io.set_gpr(dst.sel());

   Interpolator ip = {true, 0, from_nir(instr->src[0], 0), from_nir(instr->src[0], 1)};


   if (!load_interpolated(dst, io, ip, nir_dest_num_components(instr->dest),
                          nir_intrinsic_component(instr)))
      return false;

   if (m_need_back_color && io.name() == TGSI_SEMANTIC_COLOR) {

      auto & color_input  = static_cast<ShaderInputColor&> (io);
      auto& bgio = m_shaderio.input(color_input.back_color_input_index());

      GPRVector bgcol = get_temp_vec4();
      bgio.set_gpr(bgcol.sel());
      load_interpolated(bgcol, bgio, ip, nir_dest_num_components(instr->dest), 0);

      load_front_face();

      AluInstruction *ir = nullptr;
      for (unsigned i = 0; i < 4 ; ++i) {
         ir = new AluInstruction(op3_cnde, dst[i], m_front_face_reg, bgcol[i], dst[i], {alu_write});
         emit_instruction(ir);
      }
      if (ir)
         ir->set_flag(alu_last_instr);
   }


   AluInstruction *ir = nullptr;
   if (nir_intrinsic_component(instr) != 0) {
      for (unsigned i = 0; i < nir_dest_num_components(instr->dest); ++i) {
         ir = new AluInstruction(op1_mov, from_nir(instr->dest, i), dst[i + nir_intrinsic_component(instr)], {alu_write});
         emit_instruction(ir);
      }
      if (ir)
         ir->set_flag(alu_last_instr);
   }

   return true;
}

bool FragmentShaderFromNir::load_barycentric_at_offset(nir_intrinsic_instr* instr)
{
   auto interpolator = m_interpolator[barycentric_ij_index(instr)];
   PValue dummy(new GPRValue(interpolator.i->sel(), 0));

   GPRVector help = get_temp_vec4();
   GPRVector interp({interpolator.j, interpolator.i, dummy, dummy});

   auto getgradh = new TexInstruction(TexInstruction::get_gradient_h, help, interp, 0, 0, PValue());
   getgradh->set_dest_swizzle({0,1,7,7});
   getgradh->set_flag(TexInstruction::x_unnormalized);
   getgradh->set_flag(TexInstruction::y_unnormalized);
   getgradh->set_flag(TexInstruction::z_unnormalized);
   getgradh->set_flag(TexInstruction::w_unnormalized);
   getgradh->set_flag(TexInstruction::grad_fine);
   emit_instruction(getgradh);

   auto getgradv = new TexInstruction(TexInstruction::get_gradient_v, help, interp, 0, 0, PValue());
   getgradv->set_dest_swizzle({7,7,0,1});
   getgradv->set_flag(TexInstruction::x_unnormalized);
   getgradv->set_flag(TexInstruction::y_unnormalized);
   getgradv->set_flag(TexInstruction::z_unnormalized);
   getgradv->set_flag(TexInstruction::w_unnormalized);
   getgradv->set_flag(TexInstruction::grad_fine);
   emit_instruction(getgradv);

   PValue ofs_x = from_nir(instr->src[0], 0);
   PValue ofs_y = from_nir(instr->src[0], 1);
   emit_instruction(new AluInstruction(op3_muladd, help.reg_i(0), help.reg_i(0), ofs_x, interpolator.j, {alu_write}));
   emit_instruction(new AluInstruction(op3_muladd, help.reg_i(1), help.reg_i(1), ofs_x, interpolator.i, {alu_write, alu_last_instr}));
   emit_instruction(new AluInstruction(op3_muladd, from_nir(instr->dest, 0), help.reg_i(3), ofs_y, help.reg_i(1), {alu_write}));
   emit_instruction(new AluInstruction(op3_muladd, from_nir(instr->dest, 1), help.reg_i(2), ofs_y, help.reg_i(0), {alu_write, alu_last_instr}));

   return true;
}

bool FragmentShaderFromNir::load_barycentric_at_sample(nir_intrinsic_instr* instr)
{
   GPRVector slope = get_temp_vec4();

   auto fetch = new FetchInstruction(vc_fetch, no_index_offset, slope,
                                     from_nir_with_fetch_constant(instr->src[0], 0),
                                     0, R600_BUFFER_INFO_CONST_BUFFER, PValue(), bim_none);
   fetch->set_flag(vtx_srf_mode);
   emit_instruction(fetch);

   GPRVector grad = get_temp_vec4();

   auto interpolator = m_interpolator[barycentric_ij_index(instr)];
   assert(interpolator.enabled);
   PValue dummy(new GPRValue(interpolator.i->sel(), 0));

   GPRVector src({interpolator.j, interpolator.i, dummy, dummy});

   auto tex = new TexInstruction(TexInstruction::get_gradient_h, grad, src, 0, 0, PValue());
   tex->set_flag(TexInstruction::grad_fine);
   tex->set_flag(TexInstruction::x_unnormalized);
   tex->set_flag(TexInstruction::y_unnormalized);
   tex->set_flag(TexInstruction::z_unnormalized);
   tex->set_flag(TexInstruction::w_unnormalized);
   tex->set_dest_swizzle({0,1,7,7});
   emit_instruction(tex);

   tex = new TexInstruction(TexInstruction::get_gradient_v, grad, src, 0, 0, PValue());
   tex->set_flag(TexInstruction::x_unnormalized);
   tex->set_flag(TexInstruction::y_unnormalized);
   tex->set_flag(TexInstruction::z_unnormalized);
   tex->set_flag(TexInstruction::w_unnormalized);
   tex->set_flag(TexInstruction::grad_fine);
   tex->set_dest_swizzle({7,7,0,1});
   emit_instruction(tex);

   emit_instruction(new AluInstruction(op3_muladd, slope.reg_i(0), {grad.reg_i(0), slope.reg_i(2), interpolator.j}, {alu_write}));
   emit_instruction(new AluInstruction(op3_muladd, slope.reg_i(1), {grad.reg_i(1), slope.reg_i(2), interpolator.i}, {alu_write, alu_last_instr}));

   emit_instruction(new AluInstruction(op3_muladd, from_nir(instr->dest, 0), {grad.reg_i(3), slope.reg_i(3), slope.reg_i(1)}, {alu_write}));
   emit_instruction(new AluInstruction(op3_muladd, from_nir(instr->dest, 1), {grad.reg_i(2), slope.reg_i(3), slope.reg_i(0)}, {alu_write, alu_last_instr}));

   return true;
}

bool FragmentShaderFromNir::emit_load_input(nir_intrinsic_instr* instr)
{
   unsigned loc = nir_intrinsic_io_semantics(instr).location;
   auto param = nir_src_as_const_value(instr->src[0]);
   assert(param && "Indirect PS inputs not (yet) supported");

   auto& io = m_shaderio.input(param->u32 + nir_intrinsic_base(instr), nir_intrinsic_component(instr));

   assert(nir_intrinsic_io_semantics(instr).num_slots == 1);

   unsigned num_components = nir_dest_num_components(instr->dest);

   switch (loc) {
   case VARYING_SLOT_POS:
      for (unsigned i = 0; i < num_components; ++i) {
         load_preloaded_value(instr->dest, i, m_frag_pos[i]);
      }
      return true;
   case VARYING_SLOT_FACE:
      return load_preloaded_value(instr->dest, 0, m_front_face_reg);
   default:
      ;
   }

   auto dst = nir_intrinsic_component(instr) ? get_temp_vec4() : vec_from_nir(instr->dest, 4);

   AluInstruction *ir = nullptr;
   for (unsigned i = 0; i < 4 ; ++i) {
      ir = new AluInstruction(op1_interp_load_p0, dst[i],
                              PValue(new InlineConstValue(ALU_SRC_PARAM_BASE +
                                                          io.lds_pos(), i)),
                              EmitInstruction::write);
      emit_instruction(ir);
   }
   ir->set_flag(alu_last_instr);

   /* TODO: back color */
   if (m_need_back_color && io.name() == TGSI_SEMANTIC_COLOR) {
      Interpolator ip = {false, 0, NULL, NULL};

      auto & color_input  = static_cast<ShaderInputColor&> (io);
      auto& bgio = m_shaderio.input(color_input.back_color_input_index());

      GPRVector bgcol = get_temp_vec4();
      bgio.set_gpr(bgcol.sel());
      load_interpolated(bgcol, bgio, ip, num_components, 0);

      load_front_face();

      AluInstruction *ir = nullptr;
      for (unsigned i = 0; i < 4 ; ++i) {
         ir = new AluInstruction(op3_cnde, dst[i], m_front_face_reg, bgcol[i], dst[i], {alu_write});
         emit_instruction(ir);
      }
      if (ir)
         ir->set_flag(alu_last_instr);
   }

   if (nir_intrinsic_component(instr) != 0) {
      for (unsigned i = 0; i < nir_dest_num_components(instr->dest); ++i) {
         ir = new AluInstruction(op1_mov, from_nir(instr->dest, i), dst[i + nir_intrinsic_component(instr)], {alu_write});
         emit_instruction(ir);
      }
      if (ir)
         ir->set_flag(alu_last_instr);
   }


   return true;
}

void FragmentShaderFromNir::load_front_face()
{
   assert(m_front_face_reg);
   if (m_front_face_loaded)
      return;

   auto ir = new AluInstruction(op2_setge_dx10, m_front_face_reg, m_front_face_reg,
                                Value::zero, {alu_write, alu_last_instr});
   m_front_face_loaded = true;
   emit_instruction(ir);
}

bool FragmentShaderFromNir::emit_load_sample_pos(nir_intrinsic_instr* instr)
{
   GPRVector dest = vec_from_nir(instr->dest, nir_dest_num_components(instr->dest));
   auto fetch = new FetchInstruction(vc_fetch,
                                     no_index_offset,
                                     fmt_32_32_32_32_float,
                                     vtx_nf_scaled,
                                     vtx_es_none,
                                     m_sample_id_reg,
                                     dest,
                                     0,
                                     false,
                                     0xf,
                                     R600_BUFFER_INFO_CONST_BUFFER,
                                     0,
                                     bim_none,
                                     false,
                                     false,
                                     0,
                                     0,
                                     0,
                                     PValue(),
                                     {0,1,2,3});
   fetch->set_flag(vtx_srf_mode);
   emit_instruction(fetch);
   return true;
}

bool FragmentShaderFromNir::load_interpolated(GPRVector &dest,
                                              ShaderInput& io, const Interpolator &ip,
                                              int num_components, int start_comp)
{
   // replace io with ShaderInputVarying
   if (io.interpolate() > 0) {

      sfn_log << SfnLog::io << "Using Interpolator (" << *ip.j << ", " << *ip.i <<  ")" << "\n";

      if (num_components == 1) {
         switch (start_comp) {
         case 0: return load_interpolated_one_comp(dest, io, ip, op2_interp_x);
         case 1: return load_interpolated_two_comp_for_one(dest, io, ip, op2_interp_xy, 0, 1);
         case 2: return load_interpolated_one_comp(dest, io, ip, op2_interp_z);
         case 3: return load_interpolated_two_comp_for_one(dest, io, ip, op2_interp_zw, 2, 3);
         default:
            assert(0);
         }
      }

      if (num_components == 2) {
         switch (start_comp) {
         case 0: return load_interpolated_two_comp(dest, io, ip, op2_interp_xy, 0x3);
         case 2: return load_interpolated_two_comp(dest, io, ip, op2_interp_zw, 0xc);
         case 1: return load_interpolated_one_comp(dest, io, ip, op2_interp_z) &&
                  load_interpolated_two_comp_for_one(dest, io, ip, op2_interp_xy, 0, 1);
         default:
            assert(0);
         }
      }

      if (num_components == 3 && start_comp == 0)
         return load_interpolated_two_comp(dest, io, ip, op2_interp_xy, 0x3) &&
               load_interpolated_one_comp(dest, io, ip, op2_interp_z);

      int full_write_mask = ((1 << num_components) - 1) << start_comp;

      bool success = load_interpolated_two_comp(dest, io, ip, op2_interp_zw, full_write_mask & 0xc);
      success &= load_interpolated_two_comp(dest, io, ip, op2_interp_xy, full_write_mask & 0x3);
      return success;

   } else {
      AluInstruction *ir = nullptr;
      for (unsigned i = 0; i < 4 ; ++i) {
         ir = new AluInstruction(op1_interp_load_p0, dest[i],
                                 PValue(new InlineConstValue(ALU_SRC_PARAM_BASE + io.lds_pos(), i)),
                                 EmitInstruction::write);
         emit_instruction(ir);
      }
      ir->set_flag(alu_last_instr);
   }
   return true;
}

bool FragmentShaderFromNir::load_interpolated_one_comp(GPRVector &dest,
                                                       ShaderInput& io, const Interpolator& ip, EAluOp op)
{
   for (unsigned i = 0; i < 2 ; ++i) {
      int chan = i;
      if (op == op2_interp_z)
         chan += 2;


      auto ir = new AluInstruction(op, dest[chan], i & 1 ? ip.j : ip.i,
                                   PValue(new InlineConstValue(ALU_SRC_PARAM_BASE + io.lds_pos(), i)),
                                   i == 0  ? EmitInstruction::write : EmitInstruction::last);
      dest.pin_to_channel(chan);

      ir->set_bank_swizzle(alu_vec_210);
      emit_instruction(ir);
   }
   return true;
}

bool FragmentShaderFromNir::load_interpolated_two_comp(GPRVector &dest, ShaderInput& io,
                                                       const Interpolator& ip, EAluOp op, int writemask)
{
   AluInstruction *ir = nullptr;
   assert(ip.j);
   assert(ip.i);
   for (unsigned i = 0; i < 4 ; ++i) {
      ir = new AluInstruction(op, dest[i], i & 1 ? ip.j : ip.i, PValue(new InlineConstValue(ALU_SRC_PARAM_BASE + io.lds_pos(), i)),
                              (writemask & (1 << i)) ? EmitInstruction::write : EmitInstruction::empty);
      dest.pin_to_channel(i);
      ir->set_bank_swizzle(alu_vec_210);
      emit_instruction(ir);
   }
   ir->set_flag(alu_last_instr);
   return true;
}

bool FragmentShaderFromNir::load_interpolated_two_comp_for_one(GPRVector &dest,
                                                               ShaderInput& io, const Interpolator& ip,
                                                               EAluOp op, UNUSED int start, int comp)
{
   AluInstruction *ir = nullptr;
   for (int i = 0; i <  4 ; ++i) {
      ir = new AluInstruction(op, dest[i], i & 1 ? ip.j : ip.i,
                                   PValue(new InlineConstValue(ALU_SRC_PARAM_BASE + io.lds_pos(), i)),
                                   i == comp ? EmitInstruction::write : EmitInstruction::empty);
      ir->set_bank_swizzle(alu_vec_210);
      dest.pin_to_channel(i);
      emit_instruction(ir);
   }
   ir->set_flag(alu_last_instr);
   return true;
}


bool FragmentShaderFromNir::emit_export_pixel(nir_intrinsic_instr* instr, int outputs)
{
   std::array<uint32_t,4> swizzle;
   unsigned writemask = nir_intrinsic_write_mask(instr);
   auto semantics = nir_intrinsic_io_semantics(instr);
   unsigned driver_location = nir_intrinsic_base(instr);

   switch (semantics.location) {
   case FRAG_RESULT_DEPTH:
      writemask = 1;
      swizzle = {0,7,7,7};
      break;
   case FRAG_RESULT_STENCIL:
      writemask = 2;
      swizzle = {7,0,7,7};
      break;
   case FRAG_RESULT_SAMPLE_MASK:
      writemask = 4;
      swizzle = {7,7,0,7};
      break;
   default:
      for (int i = 0; i < 4; ++i) {
         swizzle[i] = (i < instr->num_components) ? i : 7;
      }
   }

   auto value = vec_from_nir_with_fetch_constant(instr->src[0], writemask, swizzle);

   set_output(driver_location, value.sel());

   if (semantics.location == FRAG_RESULT_COLOR ||
       (semantics.location >= FRAG_RESULT_DATA0 &&
        semantics.location <= FRAG_RESULT_DATA7)) {
      for (int k = 0 ; k < outputs; ++k) {

         unsigned location = (m_dual_source_blend && (semantics.location == FRAG_RESULT_COLOR)
                             ? semantics.dual_source_blend_index : driver_location) + k - m_depth_exports;

         sfn_log << SfnLog::io << "Pixel output at loc:" << location << "\n";

         if (location >= m_max_color_exports) {
            sfn_log << SfnLog::io << "Pixel output loc:" << location
                    << " dl:" << driver_location
                    << " skipped  because  we have only "   << m_max_color_exports << " CBs\n";
            continue;
         }

         m_last_pixel_export = new ExportInstruction(location, value, ExportInstruction::et_pixel);

         if (sh_info().ps_export_highest < location)
            sh_info().ps_export_highest = location;

         sh_info().nr_ps_color_exports++;

         unsigned mask = (0xfu << (location * 4));
         sh_info().ps_color_export_mask |= mask;

         emit_export_instruction(m_last_pixel_export);
      };
   } else if (semantics.location == FRAG_RESULT_DEPTH ||
              semantics.location == FRAG_RESULT_STENCIL ||
              semantics.location == FRAG_RESULT_SAMPLE_MASK) {
      m_depth_exports++;
      emit_export_instruction(new ExportInstruction(61, value, ExportInstruction::et_pixel));
   } else {
      return false;
   }
   return true;
}


bool FragmentShaderFromNir::emit_export_pixel(const nir_variable *out_var, nir_intrinsic_instr* instr, int outputs)
{
   std::array<uint32_t,4> swizzle;
   unsigned writemask = nir_intrinsic_write_mask(instr);
   switch (out_var->data.location) {
   case FRAG_RESULT_DEPTH:
      writemask = 1;
      swizzle = {0,7,7,7};
      break;
   case FRAG_RESULT_STENCIL:
      writemask = 2;
      swizzle = {7,0,7,7};
      break;
   case FRAG_RESULT_SAMPLE_MASK:
      writemask = 4;
      swizzle = {7,7,0,7};
      break;
   default:
      for (int i = 0; i < 4; ++i) {
         swizzle[i] = (i < instr->num_components) ? i : 7;
      }
   }

   auto value = vec_from_nir_with_fetch_constant(instr->src[1], writemask, swizzle);

   set_output(out_var->data.driver_location, value.sel());

   if (out_var->data.location == FRAG_RESULT_COLOR ||
       (out_var->data.location >= FRAG_RESULT_DATA0 &&
        out_var->data.location <= FRAG_RESULT_DATA7)) {
      for (int k = 0 ; k < outputs; ++k) {

         unsigned location = (m_dual_source_blend && (out_var->data.location == FRAG_RESULT_COLOR)
                             ? out_var->data.index : out_var->data.driver_location) + k - m_depth_exports;

         sfn_log << SfnLog::io << "Pixel output " << out_var->name << " at loc:" << location << "\n";

         if (location >= m_max_color_exports) {
            sfn_log << SfnLog::io << "Pixel output loc:" << location
                    << " dl:" << out_var->data.location
                    << " skipped  because  we have only "   << m_max_color_exports << " CBs\n";
            continue;
         }

         m_last_pixel_export = new ExportInstruction(location, value, ExportInstruction::et_pixel);

         if (sh_info().ps_export_highest < location)
            sh_info().ps_export_highest = location;

         sh_info().nr_ps_color_exports++;

         unsigned mask = (0xfu << (location * 4));
         sh_info().ps_color_export_mask |= mask;

         emit_export_instruction(m_last_pixel_export);
      };
   } else if (out_var->data.location == FRAG_RESULT_DEPTH ||
              out_var->data.location == FRAG_RESULT_STENCIL ||
              out_var->data.location == FRAG_RESULT_SAMPLE_MASK) {
      m_depth_exports++;
      emit_export_instruction(new ExportInstruction(61, value, ExportInstruction::et_pixel));
   } else {
      return false;
   }
   return true;
}

void FragmentShaderFromNir::do_finalize()
{
   // update shader io info and set LDS etc.
   sh_info().ninput = m_shaderio.inputs().size();

   sfn_log << SfnLog::io << "Have " << sh_info().ninput << " inputs\n";
   for (size_t i = 0; i < sh_info().ninput; ++i) {
      ShaderInput& input = m_shaderio.input(i);
      int ij_idx = (input.ij_index() < 6 &&
                    input.ij_index() >= 0) ? input.ij_index() : 0;
      input.set_ioinfo(sh_info().input[i], m_interpolator[ij_idx].ij_index);
   }

   sh_info().two_side = m_shaderio.two_sided();
   sh_info().nlds = m_shaderio.nlds();

   sh_info().nr_ps_max_color_exports = m_max_counted_color_exports;

   if (sh_info().fs_write_all) {
      sh_info().nr_ps_max_color_exports = m_max_color_exports;
   }

   if (!m_last_pixel_export) {
      GPRVector v(0, {7,7,7,7});
      m_last_pixel_export = new ExportInstruction(0, v, ExportInstruction::et_pixel);
      sh_info().nr_ps_color_exports++;
      sh_info().ps_color_export_mask = 0xf;
      emit_export_instruction(m_last_pixel_export);
   }

   m_last_pixel_export->set_last();

   if (sh_info().fs_write_all)
      sh_info().nr_ps_max_color_exports = 8;
}

}
