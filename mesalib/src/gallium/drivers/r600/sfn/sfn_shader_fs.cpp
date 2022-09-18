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


#include "sfn_debug.h"
#include "sfn_shader_fs.h"

#include "sfn_instr_alugroup.h"
#include "sfn_instr_tex.h"
#include "sfn_instr_fetch.h"
#include "sfn_instr_export.h"

#include "tgsi/tgsi_from_mesa.h"

#include <sstream>

namespace r600 {

using std::string;

FragmentShader::FragmentShader(const r600_shader_key& key):
   Shader("FS"),
   m_dual_source_blend(key.ps.dual_source_blend),
   m_max_color_exports(MAX2(key.ps.nr_cbufs, 1)),
   m_export_highest(0),
   m_num_color_exports(0),
   m_color_export_mask(0),
   m_depth_exports(0),
   m_last_pixel_export(nullptr),
   m_pos_input(127, false),
   m_fs_write_all(false),
   m_apply_sample_mask(key.ps.apply_sample_id_mask),
   m_rat_base(key.ps.nr_cbufs)
{
}

void FragmentShader::do_get_shader_info(r600_shader *sh_info)
{
   sh_info->processor_type = PIPE_SHADER_FRAGMENT;

   sh_info->ps_color_export_mask = m_color_export_mask;
   sh_info->ps_export_highest = m_export_highest;
   sh_info->nr_ps_color_exports = m_num_color_exports;

   sh_info->fs_write_all = m_fs_write_all;

   sh_info->rat_base = m_rat_base;
   sh_info->uses_kill = m_uses_discard;
   sh_info->gs_prim_id_input = m_gs_prim_id_input;
   sh_info->ps_prim_id_input = m_ps_prim_id_input &&
                               chip_class() >= ISA_CC_EVERGREEN;
   sh_info->nsys_inputs = m_nsys_inputs;
   sh_info->uses_helper_invocation = m_helper_invocation != nullptr;
}


bool FragmentShader::load_input(nir_intrinsic_instr *intr)
{
   auto& vf = value_factory();

   auto location = nir_intrinsic_io_semantics(intr).location;
   if (location == VARYING_SLOT_POS) {
      AluInstr *ir = nullptr;
      for (unsigned i = 0; i < nir_dest_num_components(intr->dest) ; ++i) {
         ir = new AluInstr(op1_mov,
                           vf.dest(intr->dest, i, pin_none),
                           m_pos_input[i],
                           AluInstr::write);
         emit_instruction(ir);
      }
      ir->set_alu_flag(alu_last_instr);
      return true;
   }

   if (location == VARYING_SLOT_FACE) {
      auto ir = new AluInstr(op2_setgt_dx10,
                             vf.dest(intr->dest, 0, pin_none),
                             m_face_input,
                             vf.inline_const(ALU_SRC_0, 0),
                             AluInstr::last_write);
      emit_instruction(ir);
      return true;
   }

   return load_input_hw(intr);
}

bool FragmentShader::store_output(nir_intrinsic_instr *intr)
{
   auto location = nir_intrinsic_io_semantics(intr).location;

   if (location == FRAG_RESULT_COLOR && !m_dual_source_blend) {
         m_fs_write_all = true;
   }

   return emit_export_pixel(*intr);
}

unsigned
barycentric_ij_index(nir_intrinsic_instr *intr)
{
   unsigned index = 0;
   switch (intr->intrinsic) {
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

   switch (nir_intrinsic_interp_mode(intr)) {
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

bool FragmentShader::process_stage_intrinsic(nir_intrinsic_instr *intr)
{
   if (process_stage_intrinsic_hw(intr))
      return true;

   switch (intr->intrinsic) {
   case nir_intrinsic_load_input:
      return load_input(intr);
   case nir_intrinsic_load_interpolated_input:
      return load_interpolated_input(intr);
   case nir_intrinsic_discard_if:
      m_uses_discard = true;
      emit_instruction(new AluInstr(op2_killne_int, nullptr,
                                    value_factory().src(intr->src[0], 0),
                                    value_factory().zero(),
                                    {AluInstr::last}));
      start_new_block(0);
      return true;
   case nir_intrinsic_discard:
      m_uses_discard = true;
      emit_instruction(new AluInstr(op2_kille_int, nullptr,
                                    value_factory().zero(),
                                    value_factory().zero(),
                                    {AluInstr::last}));
      return true;
   case nir_intrinsic_load_sample_mask_in:
      if (m_apply_sample_mask) {
         return emit_load_sample_mask_in(intr);
      } else
         return emit_simple_mov(intr->dest, 0, m_sample_mask_reg);
   case nir_intrinsic_load_sample_id:
      return emit_simple_mov(intr->dest, 0, m_sample_id_reg);
   case nir_intrinsic_load_helper_invocation:
       return emit_load_helper_invocation(intr);
   case nir_intrinsic_load_sample_pos:
      return emit_load_sample_pos(intr);
   default:
      return false;
   }
}

bool FragmentShader::load_interpolated_input(nir_intrinsic_instr *intr)
{
   auto& vf = value_factory();
   unsigned loc = nir_intrinsic_io_semantics(intr).location;
   switch (loc) {
   case VARYING_SLOT_POS:
      for (unsigned i = 0; i < nir_dest_num_components(intr->dest); ++i)
         vf.inject_value(intr->dest, i,  m_pos_input[i]);
      return true;
   case VARYING_SLOT_FACE:
      return false;
   default:
      ;
   }

   return load_interpolated_input_hw(intr);
}


int FragmentShader::do_allocate_reserved_registers()
{
   int next_register = allocate_interpolators_or_inputs();

   if (m_sv_values.test(es_pos)) {
      set_input_gpr(m_pos_driver_loc, next_register);
      m_pos_input = value_factory().allocate_pinned_vec4(next_register++, false);
      for (int i = 0; i < 4; ++i)
         m_pos_input[i]->pin_live_range(true);

   }

   int face_reg_index = -1;
   if (m_sv_values.test(es_face)) {
      set_input_gpr(m_face_driver_loc, next_register);
      face_reg_index = next_register++;
      m_face_input = value_factory().allocate_pinned_register(face_reg_index, 0);
      m_face_input->pin_live_range(true);
   }

   if (m_sv_values.test(es_sample_mask_in)) {
      if (face_reg_index < 0)
         face_reg_index = next_register++;
      m_sample_mask_reg = value_factory().allocate_pinned_register(face_reg_index, 2);
      m_sample_mask_reg->pin_live_range(true);
      sfn_log << SfnLog::io << "Set sample mask in register to " <<  *m_sample_mask_reg << "\n";
      m_nsys_inputs = 1;
      ShaderInput input(ninputs(), TGSI_SEMANTIC_SAMPLEMASK);
      input.set_gpr(face_reg_index);
      add_input(input);
   }

   if (m_sv_values.test(es_sample_id) ||
       m_sv_values.test(es_sample_mask_in)) {
      int sample_id_reg = next_register++;
      m_sample_id_reg = value_factory().allocate_pinned_register(sample_id_reg, 3);
      m_sample_id_reg->pin_live_range(true);
      sfn_log << SfnLog::io << "Set sample id register to " <<  *m_sample_id_reg << "\n";
      m_nsys_inputs++;
      ShaderInput input(ninputs(), TGSI_SEMANTIC_SAMPLEID);
      input.set_gpr(sample_id_reg);
      add_input(input);
   }

   if (m_sv_values.test(es_helper_invocation)) {
      m_helper_invocation = value_factory().allocate_pinned_register(next_register++, 0);
   }

   return next_register;
}

bool FragmentShader::do_scan_instruction(nir_instr *instr)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   auto intr = nir_instr_as_intrinsic(instr);
   switch (intr->intrinsic) {
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_sample:
   case nir_intrinsic_load_barycentric_at_sample:
   case nir_intrinsic_load_barycentric_at_offset:
   case nir_intrinsic_load_barycentric_centroid:
      m_interpolators_used.set(barycentric_ij_index(intr));
      break;
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
      break;
   case nir_intrinsic_load_input:
      return scan_input(intr, 0);
   case nir_intrinsic_load_interpolated_input:
      return scan_input(intr, 1);
   default:
      return false;
   }
   return true;
}

bool FragmentShader::emit_load_sample_mask_in(nir_intrinsic_instr* instr)
{
   auto& vf = value_factory();
   auto dest = vf.dest(instr->dest, 0, pin_free);
   auto tmp = vf.temp_register();
   assert(m_sample_id_reg);
   assert(m_sample_mask_reg);

   emit_instruction(new AluInstr(op2_lshl_int, tmp, vf.one_i(), m_sample_id_reg, AluInstr::last_write));
   emit_instruction(new AluInstr(op2_and_int, dest, tmp, m_sample_mask_reg, AluInstr::last_write));
   return true;
}

bool FragmentShader::emit_load_helper_invocation(nir_intrinsic_instr* instr)
{
   assert(m_helper_invocation);
   auto& vf = value_factory();
   emit_instruction(new AluInstr(op1_mov, m_helper_invocation, vf.literal(-1), AluInstr::last_write));
   RegisterVec4 destvec{m_helper_invocation, nullptr, nullptr, nullptr, pin_group};

   auto vtx = new LoadFromBuffer(destvec, {4,7,7,7}, m_helper_invocation, 0,
                                   R600_BUFFER_INFO_CONST_BUFFER, nullptr, fmt_32_32_32_32_float);
   vtx->set_fetch_flag(FetchInstr::vpm);
   vtx->set_fetch_flag(FetchInstr::use_tc);
   vtx->set_always_keep();
   auto dst = value_factory().dest(instr->dest, 0, pin_free);
   auto ir = new AluInstr(op1_mov, dst, m_helper_invocation, AluInstr::last_write);
   ir->add_required_instr(vtx);
   emit_instruction(vtx);
   emit_instruction(ir);

   return true;
}

bool FragmentShader::scan_input(nir_intrinsic_instr *intr, int index_src_id)
{
   auto index = nir_src_as_const_value(intr->src[index_src_id]);
   assert(index);

   const unsigned location_offset = chip_class() < ISA_CC_EVERGREEN ? 32 : 0; 
   bool uses_interpol_at_centroid = false;

   unsigned location = nir_intrinsic_io_semantics(intr).location  + index->u32;
   unsigned driver_location = nir_intrinsic_base(intr) + index->u32;
   auto semantic = r600_get_varying_semantic(location);
   tgsi_semantic name = (tgsi_semantic)semantic.first;
   unsigned sid = semantic.second;

   if (location == VARYING_SLOT_POS) {
      m_sv_values.set(es_pos);
      m_pos_driver_loc = driver_location + location_offset;
      ShaderInput pos_input(m_pos_driver_loc, name);
      pos_input.set_sid(sid);
      pos_input.set_interpolator(TGSI_INTERPOLATE_LINEAR, TGSI_INTERPOLATE_LOC_CENTER, false);
      add_input(pos_input);
      return true;
   }

   if (location == VARYING_SLOT_FACE) {
      m_sv_values.set(es_face);
      m_face_driver_loc = driver_location + location_offset;
      ShaderInput face_input(m_face_driver_loc, name);
      face_input.set_sid(sid);
      add_input(face_input);
      return true;
   }

   tgsi_interpolate_mode tgsi_interpolate = TGSI_INTERPOLATE_CONSTANT;
   tgsi_interpolate_loc tgsi_loc = TGSI_INTERPOLATE_LOC_CENTER;

   if (index_src_id > 0) {
      glsl_interp_mode mode = INTERP_MODE_NONE;
      auto parent = nir_instr_as_intrinsic(intr->src[0].ssa->parent_instr);
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
                   << nir_intrinsic_infos[intr->intrinsic].name
                   << " interpolator?\n";
         assert(0);
      }

      switch (mode) {
      case INTERP_MODE_NONE:
         if (name == TGSI_SEMANTIC_COLOR ||
             name == TGSI_SEMANTIC_BCOLOR) {
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
   }

   switch (name) {
   case TGSI_SEMANTIC_PRIMID:
      m_gs_prim_id_input = true;
      m_ps_prim_id_input = ninputs();
      FALLTHROUGH;
   case TGSI_SEMANTIC_COLOR:
   case TGSI_SEMANTIC_BCOLOR:
   case TGSI_SEMANTIC_FOG:
   case TGSI_SEMANTIC_GENERIC:
   case TGSI_SEMANTIC_TEXCOORD:
   case TGSI_SEMANTIC_LAYER:
   case TGSI_SEMANTIC_PCOORD:
   case TGSI_SEMANTIC_VIEWPORT_INDEX:
   case TGSI_SEMANTIC_CLIPDIST: {
      sfn_log << SfnLog::io <<  " have IO at " << driver_location << "\n";
      auto iinput = find_input(driver_location);
      if (iinput == input_not_found()) {
         ShaderInput input(driver_location, name);
         input.set_sid(sid);
         input.set_need_lds_pos();
         input.set_interpolator(tgsi_interpolate, tgsi_loc, uses_interpol_at_centroid);
         sfn_log << SfnLog::io <<  "add IO with LDS ID at " << input.location() << "\n";
         add_input(input);
         assert(find_input(input.location()) != input_not_found());
      } else {
         if (uses_interpol_at_centroid) {
            iinput->second.set_uses_interpolate_at_centroid();
         }
      }
      return true;
   }
   default:
      return false;
   }
}

bool FragmentShader::emit_export_pixel(nir_intrinsic_instr& intr)
{
   RegisterVec4::Swizzle swizzle;
   auto semantics = nir_intrinsic_io_semantics(&intr);
   unsigned driver_location = nir_intrinsic_base(&intr);
   unsigned write_mask = nir_intrinsic_write_mask(&intr);

   switch (semantics.location) {
   case FRAG_RESULT_DEPTH:
      swizzle = {0,7,7,7};
   break;
   case FRAG_RESULT_STENCIL:
      swizzle = {7,0,7,7};
   break;
   case FRAG_RESULT_SAMPLE_MASK:
      swizzle = {7,7,0,7};
   break;
   default:
      for (int i = 0; i < 4; ++i) {
         swizzle[i] = (1 << i) & write_mask ? i : 7;
      }
   }

   auto value = value_factory().src_vec4(intr.src[0], pin_group, swizzle);

   if (semantics.location == FRAG_RESULT_COLOR ||
       (semantics.location >= FRAG_RESULT_DATA0 &&
        semantics.location <= FRAG_RESULT_DATA7)) {

      ShaderOutput output(driver_location, TGSI_SEMANTIC_COLOR, write_mask);
      add_output(output);

      unsigned color_outputs = m_fs_write_all && chip_class() >= ISA_CC_R700 ?
                                  m_max_color_exports : 1;

      for (unsigned k = 0; k < color_outputs; ++k) {

         unsigned location = (m_dual_source_blend && (semantics.location == FRAG_RESULT_COLOR)
                              ? semantics.dual_source_blend_index : driver_location) + k - m_depth_exports;

         sfn_log << SfnLog::io << "Pixel output at loc:" << location << "\n";

         if (location >= m_max_color_exports) {
            sfn_log << SfnLog::io << "Pixel output loc:" << location
                    << " dl:" << driver_location
                    << " skipped  because  we have only "   << m_max_color_exports << " CBs\n";
            return true; ;
         }

         m_last_pixel_export = new ExportInstr(ExportInstr::pixel, location, value);

         if (m_export_highest < location)
            m_export_highest = location;

         m_num_color_exports++;

         /* Hack: force dual source output handling if one color output has a
          * dual_source_blend_index > 0 */
         if (semantics.location == FRAG_RESULT_COLOR &&
             semantics.dual_source_blend_index > 0)
            m_dual_source_blend = true;

         if (m_num_color_exports > 1)
            m_fs_write_all = false;
         unsigned mask = (0xfu << (location * 4));
         m_color_export_mask |= mask;

         emit_instruction(m_last_pixel_export);
      }
   } else if (semantics.location == FRAG_RESULT_DEPTH ||
              semantics.location == FRAG_RESULT_STENCIL ||
              semantics.location == FRAG_RESULT_SAMPLE_MASK) {
      m_depth_exports++;
      emit_instruction(new ExportInstr(ExportInstr::pixel, 61, value));
      int semantic = TGSI_SEMANTIC_POSITION;
      if (semantics.location == FRAG_RESULT_STENCIL)
         semantic = TGSI_SEMANTIC_STENCIL;
      else if (semantics.location == FRAG_RESULT_SAMPLE_MASK)
         semantic = TGSI_SEMANTIC_SAMPLEMASK;

      ShaderOutput output(driver_location, semantic, write_mask);
      add_output(output);

   } else {
      return false;
   }
   return true;
}

bool FragmentShader::emit_load_sample_pos(nir_intrinsic_instr* instr)
{
   auto dest = value_factory().dest_vec4(instr->dest, pin_group);


   auto fetch = new LoadFromBuffer(dest, {0,1,2,3}, m_sample_id_reg, 0,
                                   R600_BUFFER_INFO_CONST_BUFFER,
                                   nullptr, fmt_32_32_32_32_float);
   fetch->set_fetch_flag(FetchInstr::srf_mode);
   emit_instruction(fetch);
   return true;
}

void FragmentShader::do_finalize()
{
   if (!m_last_pixel_export) {
      RegisterVec4 value(0, false, {7,7,7,7});
      m_last_pixel_export = new ExportInstr(ExportInstr::pixel, 0, value);
      emit_instruction(m_last_pixel_export);
      m_num_color_exports++;
      m_color_export_mask |= 0xf;
   }
   m_last_pixel_export->set_is_last_export(true);
}

bool FragmentShader::read_prop(std::istream& is)
{
   string value;
   is >> value;

   auto splitpos = value.find(':');
   assert(splitpos != string::npos);

   std::istringstream ival(value);
   string name;
   string val;

   std::getline(ival, name, ':');

   if (name == "MAX_COLOR_EXPORTS")
      ival >> m_max_color_exports;
   else if (name == "COLOR_EXPORTS")
      ival >> m_num_color_exports;
   else if (name == "COLOR_EXPORT_MASK")
      ival >> m_color_export_mask;
   else if (name == "WRITE_ALL_COLORS")
      ival >> m_fs_write_all;
   else
      return false;
   return true;
}

void FragmentShader::do_print_properties(std::ostream& os) const
{
   os << "PROP MAX_COLOR_EXPORTS:"  << m_max_color_exports << "\n";
   os << "PROP COLOR_EXPORTS:"  << m_num_color_exports << "\n";
   os << "PROP COLOR_EXPORT_MASK:"  << m_color_export_mask << "\n";
   os << "PROP WRITE_ALL_COLORS:" << m_fs_write_all << "\n";
}

int FragmentShaderR600::allocate_interpolators_or_inputs()
{
   int pos = 0;
   auto& vf = value_factory();
   for (auto& [index, inp]: inputs()) {
      if (inp.need_lds_pos()) {

         RegisterVec4 input(vf.allocate_pinned_register(pos, 0),
                            vf.allocate_pinned_register(pos, 1),
                            vf.allocate_pinned_register(pos, 2),
                            vf.allocate_pinned_register(pos, 3), pin_fully);
         inp.set_gpr(pos++);
         for (int i = 0; i < 4; ++i) {
            input[i]->pin_live_range(true);
         }

         sfn_log << SfnLog::io << "Reseve input register at pos " <<
                    index << " as "  << input << " with register " << inp.gpr() << "\n";

         m_interpolated_inputs[index] = input;
      }
   }
   return pos;
}

bool FragmentShaderR600::load_input_hw(nir_intrinsic_instr *intr)
{
   auto& vf = value_factory();
   AluInstr *ir = nullptr;
   for (unsigned i = 0; i < nir_dest_num_components(intr->dest); ++i) {
      sfn_log << SfnLog::io << "Inject register "  << *m_interpolated_inputs[nir_intrinsic_base(intr)][i] << "\n";
      unsigned index = nir_intrinsic_component(intr) + i;
      assert (index < 4);
      if (intr->dest.is_ssa) {
         vf.inject_value(intr->dest, i, m_interpolated_inputs[nir_intrinsic_base(intr)][index]);
      } else {
         ir = new AluInstr(op1_mov, vf.dest(intr->dest, i, pin_none),
                     m_interpolated_inputs[nir_intrinsic_base(intr)][index],
                     AluInstr::write);
         emit_instruction(ir);
      }
   }
   if (ir)
      ir->set_alu_flag(alu_last_instr);
   return true;
}

bool FragmentShaderR600::process_stage_intrinsic_hw(nir_intrinsic_instr *intr)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_load_barycentric_centroid:
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_sample:
      return true;
   default:
      return false;
   }
}

bool FragmentShaderR600::load_interpolated_input_hw(nir_intrinsic_instr *intr)
{
   return load_input_hw(intr);
}

bool FragmentShaderEG::load_input_hw(nir_intrinsic_instr *intr)
{
   auto& vf = value_factory();
   auto io = input(nir_intrinsic_base(intr));
   auto comp = nir_intrinsic_component(intr);

   bool need_temp = comp > 0 || !intr->dest.is_ssa;
   AluInstr *ir = nullptr;
   for (unsigned i = 0; i < nir_dest_num_components(intr->dest) ; ++i) {
      if (need_temp) {
         auto tmp = vf.temp_register(comp + i);
         ir = new AluInstr(op1_interp_load_p0,
                           tmp,
                           new InlineConstant(ALU_SRC_PARAM_BASE + io.lds_pos(), i + comp),
                           AluInstr::last_write);
         emit_instruction(ir);
         emit_instruction(new AluInstr(op1_mov, vf.dest(intr->dest, i, pin_chan), tmp, AluInstr::last_write));
      } else {

         ir = new AluInstr(op1_interp_load_p0,
                           vf.dest(intr->dest, i, pin_chan),
                           new InlineConstant(ALU_SRC_PARAM_BASE + io.lds_pos(), i),
                           AluInstr::write);
         emit_instruction(ir);
      }

   }
   ir->set_alu_flag(alu_last_instr);
   return true;
}

int FragmentShaderEG::allocate_interpolators_or_inputs()
{
   for (unsigned i = 0; i < s_max_interpolators; ++i) {
      if (interpolators_used(i)) {
         sfn_log << SfnLog::io << "Interpolator " << i << " test enabled\n";
         m_interpolator[i].enabled = true;
      }
   }

   int num_baryc = 0;
   for (int i = 0; i < 6; ++i) {
      if (m_interpolator[i].enabled) {
         sfn_log << SfnLog::io << "Interpolator " << i << " is enabled with ij=" << num_baryc <<" \n";
         unsigned sel = num_baryc / 2;
         unsigned chan = 2 * (num_baryc % 2);

         m_interpolator[i].i = value_factory().allocate_pinned_register(sel, chan + 1);
         m_interpolator[i].i->pin_live_range(true, false);

         m_interpolator[i].j = value_factory().allocate_pinned_register(sel, chan);
         m_interpolator[i].j->pin_live_range(true, false);

         m_interpolator[i].ij_index = num_baryc++;
      }
   }
   return (num_baryc + 1) >> 1;
}

bool FragmentShaderEG::process_stage_intrinsic_hw(nir_intrinsic_instr *intr)
{
   auto& vf = value_factory();
   switch (intr->intrinsic) {
   case nir_intrinsic_load_barycentric_centroid:
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_sample: {
      unsigned ij = barycentric_ij_index(intr);
      vf.inject_value(intr->dest, 0, m_interpolator[ij].i);
      vf.inject_value(intr->dest, 1, m_interpolator[ij].j);
      return true;
   }
   case nir_intrinsic_load_barycentric_at_offset:
      return load_barycentric_at_offset(intr);
   case nir_intrinsic_load_barycentric_at_sample:
      return load_barycentric_at_sample(intr);
   default:
      return false;
   }
}

bool FragmentShaderEG::load_interpolated_input_hw(nir_intrinsic_instr *intr)
{
   auto& vf = value_factory();
   auto param = nir_src_as_const_value(intr->src[1]);
   assert(param && "Indirect PS inputs not (yet) supported");

   int dest_num_comp = nir_dest_num_components(intr->dest);
   int start_comp = nir_intrinsic_component(intr);
   bool need_temp = start_comp > 0 || !intr->dest.is_ssa;

   auto dst = need_temp ? vf.temp_vec4(pin_chan) : vf.dest_vec4(intr->dest, pin_chan);

   InterpolateParams params;

   params.i = vf.src(intr->src[0], 0);
   params.j = vf.src(intr->src[0], 1);
   params.base = input(nir_intrinsic_base(intr)).lds_pos();

   if (!load_interpolated(dst, params, dest_num_comp, start_comp))
      return false;

   if (need_temp) {
      AluInstr *ir = nullptr;
      for (unsigned i = 0; i < nir_dest_num_components(intr->dest); ++i) {
         auto real_dst = vf.dest(intr->dest, i, pin_chan);
         ir = new AluInstr(op1_mov, real_dst, dst[i + start_comp], AluInstr::write);
         emit_instruction(ir);
      }
      assert(ir);
      ir->set_alu_flag(alu_last_instr);
   }

   return true;
}

bool FragmentShaderEG::load_interpolated(RegisterVec4& dest, const InterpolateParams& params,
                                         int num_dest_comp, int start_comp)
{
   sfn_log << SfnLog::io << "Using Interpolator (" << *params.j << ", " << *params.i <<  ")" << "\n";

   if (num_dest_comp == 1) {
      switch (start_comp) {
      case 0: return load_interpolated_one_comp(dest, params, op2_interp_x);
      case 1: return load_interpolated_two_comp_for_one(dest, params,  op2_interp_xy, 1);
      case 2: return load_interpolated_one_comp(dest, params, op2_interp_z);
      case 3: return load_interpolated_two_comp_for_one(dest, params, op2_interp_zw, 3);
      default:
         assert(0);
      }
   }

   if (num_dest_comp == 2) {
      switch (start_comp) {
      case 0: return load_interpolated_two_comp(dest, params, op2_interp_xy, 0x3);
      case 2: return load_interpolated_two_comp(dest, params, op2_interp_zw, 0xc);
      case 1: return load_interpolated_one_comp(dest, params, op2_interp_z) &&
               load_interpolated_two_comp_for_one(dest, params, op2_interp_xy, 1);
      default:
         assert(0);
      }
   }

   if (num_dest_comp == 3 && start_comp == 0)
      return load_interpolated_two_comp(dest, params, op2_interp_xy, 0x3) &&
            load_interpolated_one_comp(dest, params, op2_interp_z);

   int full_write_mask = ((1 << num_dest_comp) - 1) << start_comp;

   bool success = load_interpolated_two_comp(dest, params, op2_interp_zw, full_write_mask & 0xc);
   success &= load_interpolated_two_comp(dest, params, op2_interp_xy, full_write_mask & 0x3);
   return success;
}


bool FragmentShaderEG::load_barycentric_at_sample(nir_intrinsic_instr* instr)
{
   auto& vf = value_factory();
   RegisterVec4 slope = vf.temp_vec4(pin_group);
   auto  src = emit_load_to_register(vf.src(instr->src[0], 0));
   auto fetch = new LoadFromBuffer(slope, {0, 1,2, 3}, src, 0,
                                   R600_BUFFER_INFO_CONST_BUFFER, nullptr, fmt_32_32_32_32_float);

   fetch->set_fetch_flag(FetchInstr::srf_mode);
   emit_instruction(fetch);

   auto grad = vf.temp_vec4(pin_group);

   auto interpolator = m_interpolator[barycentric_ij_index(instr)];
   assert(interpolator.enabled);

   RegisterVec4 interp(interpolator.j, interpolator.i, nullptr, nullptr, pin_group);

   auto tex = new TexInstr(TexInstr::get_gradient_h, grad, {0, 1, 7, 7}, interp, 0, 0);
   tex->set_tex_flag(TexInstr::grad_fine);
   tex->set_tex_flag(TexInstr::x_unnormalized);
   tex->set_tex_flag(TexInstr::y_unnormalized);
   tex->set_tex_flag(TexInstr::z_unnormalized);
   tex->set_tex_flag(TexInstr::w_unnormalized);
   emit_instruction(tex);

   tex = new TexInstr(TexInstr::get_gradient_v, grad, {7,7,0,1}, interp, 0, 0);
   tex->set_tex_flag(TexInstr::x_unnormalized);
   tex->set_tex_flag(TexInstr::y_unnormalized);
   tex->set_tex_flag(TexInstr::z_unnormalized);
   tex->set_tex_flag(TexInstr::w_unnormalized);
   tex->set_tex_flag(TexInstr::grad_fine);
   emit_instruction(tex);

   auto tmp0 = vf.temp_register();
   auto tmp1 = vf.temp_register();

   emit_instruction(new AluInstr(op3_muladd, tmp0, grad[0], slope[2], interpolator.j, {alu_write}));
   emit_instruction(new AluInstr(op3_muladd, tmp1, grad[1], slope[2], interpolator.i, {alu_write, alu_last_instr}));

   emit_instruction(new AluInstr(op3_muladd, vf.dest(instr->dest, 0, pin_none), grad[3], slope[3], tmp1, {alu_write}));
   emit_instruction(new AluInstr(op3_muladd, vf.dest(instr->dest, 1, pin_none), grad[2], slope[3], tmp0, {alu_write, alu_last_instr}));

   return true;
}

bool FragmentShaderEG::load_barycentric_at_offset(nir_intrinsic_instr* instr)
{
   auto& vf = value_factory();
   auto interpolator = m_interpolator[barycentric_ij_index(instr)];

   auto help = vf.temp_vec4(pin_group);
   RegisterVec4 interp(interpolator.j, interpolator.i, nullptr, nullptr, pin_group);

   auto getgradh = new TexInstr(TexInstr::get_gradient_h, help, {0,1,7,7}, interp, 0, 0);
   getgradh->set_tex_flag(TexInstr::x_unnormalized);
   getgradh->set_tex_flag(TexInstr::y_unnormalized);
   getgradh->set_tex_flag(TexInstr::z_unnormalized);
   getgradh->set_tex_flag(TexInstr::w_unnormalized);
   getgradh->set_tex_flag(TexInstr::grad_fine);
   emit_instruction(getgradh);

   auto getgradv = new TexInstr(TexInstr::get_gradient_v, help, {7,7,0,1}, interp, 0, 0);
   getgradv->set_tex_flag(TexInstr::x_unnormalized);
   getgradv->set_tex_flag(TexInstr::y_unnormalized);
   getgradv->set_tex_flag(TexInstr::z_unnormalized);
   getgradv->set_tex_flag(TexInstr::w_unnormalized);
   getgradv->set_tex_flag(TexInstr::grad_fine);
   emit_instruction(getgradv);

   auto ofs_x = vf.src(instr->src[0], 0);
   auto ofs_y = vf.src(instr->src[0], 1);
   auto tmp0 = vf.temp_register();
   auto tmp1 = vf.temp_register();
   emit_instruction(new AluInstr(op3_muladd, tmp0, help[0], ofs_x, interpolator.j, {alu_write}));
   emit_instruction(new AluInstr(op3_muladd, tmp1, help[1], ofs_x, interpolator.i, {alu_write, alu_last_instr}));
   emit_instruction(new AluInstr(op3_muladd, vf.dest(instr->dest, 0, pin_none), help[3], ofs_y, tmp1, {alu_write}));
   emit_instruction(new AluInstr(op3_muladd, vf.dest(instr->dest, 1, pin_none), help[2], ofs_y, tmp0, {alu_write, alu_last_instr}));

   return true;
}

bool FragmentShaderEG::load_interpolated_one_comp(RegisterVec4& dest,
                                                const InterpolateParams& params,
                                                EAluOp op)
{
   auto group = new AluGroup();
   bool success = true;

   AluInstr *ir = nullptr;
   for (unsigned i = 0; i < 2 && success; ++i) {
      int chan = i;
      if (op == op2_interp_z)
         chan += 2;


      ir = new AluInstr(op, dest[chan],
                        i & 1 ? params.j : params.i,
                        new InlineConstant(ALU_SRC_PARAM_BASE + params.base, chan),
                        i == 0  ? AluInstr::write : AluInstr::last);

      ir->set_bank_swizzle(alu_vec_210);
      success = group->add_instruction(ir);
   }
   ir->set_alu_flag(alu_last_instr);
   if (success)
      emit_instruction(group);
   return success;
}

bool FragmentShaderEG::load_interpolated_two_comp(RegisterVec4& dest,
                                                const InterpolateParams& params,
                                                EAluOp op, int writemask)
{
   auto group = new AluGroup();
   bool success = true;

   AluInstr *ir = nullptr;
   assert(params.j);
   assert(params.i);
   for (unsigned i = 0; i < 4 ; ++i) {
      ir = new AluInstr(op, dest[i], i & 1 ? params.j : params.i,
                        new InlineConstant(ALU_SRC_PARAM_BASE + params.base, i),
                        (writemask & (1 << i)) ? AluInstr::write : AluInstr::empty);
      ir->set_bank_swizzle(alu_vec_210);
      success = group->add_instruction(ir);
   }
   ir->set_alu_flag(alu_last_instr);
   if (success)
      emit_instruction(group);
   return success;
}

bool FragmentShaderEG::load_interpolated_two_comp_for_one(RegisterVec4& dest,
                                                          const InterpolateParams& params, EAluOp op,
                                                          int comp)
{
   auto group = new AluGroup();
   bool success = true;
   AluInstr *ir = nullptr;

   for (int i = 0; i <  4 ; ++i) {
      ir = new AluInstr(op, dest[i], i & 1 ? params.j : params.i,
                        new InlineConstant(ALU_SRC_PARAM_BASE + params.base, i),
                        i == comp ? AluInstr::write : AluInstr::empty);
      ir->set_bank_swizzle(alu_vec_210);
      success = group->add_instruction(ir);
   }
   ir->set_alu_flag(alu_last_instr);
   if (success)
      emit_instruction(group);

   return success;
}


FragmentShaderEG::Interpolator::Interpolator():
   enabled(false)
{
}

}
