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

#include "sfn_shader.h"
#include "sfn_instr.h"
#include "sfn_instr_alugroup.h"
#include "sfn_instr_export.h"
#include "sfn_instr_fetch.h"
#include "sfn_instr_lds.h"
#include "sfn_instr_mem.h"

#include "sfn_instr_controlflow.h"
#include "sfn_liverangeevaluator.h"

#include "sfn_shader_cs.h"
#include "sfn_shader_fs.h"
#include "sfn_shader_vs.h"
#include "sfn_shader_gs.h"
#include "sfn_shader_tess.h"

#include "sfn_debug.h"
#include "gallium/drivers/r600/r600_shader.h"

#include "tgsi/tgsi_from_mesa.h"

#include "nir.h"

#include <numeric>
#include <sstream>

namespace r600 {

using std::string;

std::pair<unsigned, unsigned>
r600_get_varying_semantic(unsigned varying_location)
{
   std::pair<unsigned, unsigned> result;
   tgsi_get_gl_varying_semantic(static_cast<gl_varying_slot>(varying_location),
                                true, &result.first, &result.second);

   if (result.first == TGSI_SEMANTIC_GENERIC) {
      result.second += 9;
   } else if (result.first == TGSI_SEMANTIC_PCOORD) {
      result.second = 8;
   }
   return result;
}

void ShaderIO::set_sid(int sid)
{
   m_sid = sid;
   switch (m_name) {
   case TGSI_SEMANTIC_POSITION:
   case TGSI_SEMANTIC_PSIZE:
   case TGSI_SEMANTIC_EDGEFLAG:
   case TGSI_SEMANTIC_FACE:
   case TGSI_SEMANTIC_SAMPLEMASK:
   case TGSI_SEMANTIC_CLIPVERTEX:
      m_spi_sid = 0;
   break;
   case TGSI_SEMANTIC_GENERIC:
   case TGSI_SEMANTIC_TEXCOORD:
   case TGSI_SEMANTIC_PCOORD:
      m_spi_sid = m_sid + 1;
   break;
   default:
      /* For non-generic params - pack name and sid into 8 bits */
      m_spi_sid = (0x80 | (m_name << 3) | m_sid) + 1;
   }
}

void ShaderIO::override_spi_sid(int spi)
{
   m_spi_sid = spi;
}

void ShaderIO::print(std::ostream& os) const
{
   os << m_type << " LOC:" << m_location << " NAME:" << m_name;
   do_print(os);

   if (m_sid > 0) {
      os << " SID:" << m_sid << " SPI_SID:" << m_spi_sid;
   }
}


ShaderIO::ShaderIO(const char *type, int loc, int name):
   m_type(type),
   m_location(loc),
   m_name(name)
{
}

ShaderOutput::ShaderOutput():
   ShaderIO("OUTPUT", -1, -1)
{
}

ShaderOutput::ShaderOutput(int location, int name, int writemask):
   ShaderIO("OUTPUT", location, name),
   m_writemask(writemask)
{

}

void ShaderOutput::do_print(std::ostream& os) const
{
   os << " MASK:" << m_writemask;
}


ShaderInput::ShaderInput(int location, int name):
   ShaderIO("INPUT", location, name)
{
}

ShaderInput::ShaderInput():
   ShaderInput(-1, -1)
{
}


void ShaderInput::do_print(std::ostream& os) const
{
   if (m_interpolator)
      os << " INTERP:" << m_interpolator;
   if (m_interpolate_loc)
      os << " ILOC:" << m_interpolate_loc;
   if (m_uses_interpolate_at_centroid)
      os << " USE_CENTROID";
}

void ShaderInput::set_interpolator(int interp, int interp_loc, bool uses_interpolate_at_centroid)
{
   m_interpolator = interp;
   m_interpolate_loc = interp_loc;
   m_uses_interpolate_at_centroid = uses_interpolate_at_centroid;
}

void ShaderInput::set_uses_interpolate_at_centroid()
{
   m_uses_interpolate_at_centroid = true;
}

Shader::Shader(const char *type_id):
   m_current_block(nullptr),
   m_type_id(type_id),
   m_chip_class(ISA_CC_R600),
   m_next_block(0)
{
   m_instr_factory = new InstrFactory();
   m_chain_instr.this_shader = this;
   start_new_block(0);
}

void Shader::set_input_gpr(int driver_lcation, int gpr)
{
   auto i = m_inputs.find(driver_lcation);
   assert(i != m_inputs.end());
   i->second.set_gpr(gpr);
}

bool Shader::add_info_from_string(std::istream& is)
{
   std::string type;
   is >> type;

   if (type == "CHIPCLASS")
      return read_chipclass(is);
   if (type == "OUTPUT")
      return read_output(is);
   if (type == "INPUT")
      return read_input(is);
   if (type == "PROP")
      return read_prop(is);
   if (type == "SYSVALUES")
      return allocate_registers_from_string(is, pin_fully);
   if (type == "REGISTERS")
      return allocate_registers_from_string(is, pin_free);
   if (type == "ARRAYS")
      return allocate_arrays_from_string(is);


   return false;
}

void Shader::emit_instruction_from_string(const std::string& s)
{

   sfn_log << SfnLog::instr << "Create Instr from '" << s << "'\n";
   if (s == "BLOCK_START") {
      if (!m_current_block->empty()) {
         start_new_block(m_current_block->nesting_offset());
         sfn_log << SfnLog::instr << "   Emit start block\n";
      }
      return;
   }

   if (s == "BLOCK_END") {
      return;
   }

   auto ir = m_instr_factory->from_string(s, m_current_block->nesting_depth());
   if (ir) {
      emit_instruction(ir);
      if (ir->end_block())
         start_new_block(ir->nesting_offset());
      sfn_log << SfnLog::instr << "   " << *ir << "\n";
   }
}

bool Shader::read_output(std::istream& is)
{
   string value;
   is >> value;
   int pos = int_from_string_with_prefix(value, "LOC:");
   is >> value;
   int name = int_from_string_with_prefix(value, "NAME:");
   is >> value;
   int mask = int_from_string_with_prefix(value, "MASK:");
   ShaderOutput output(pos, name, mask);

   value.clear();
   is >> value;
   if (!value.empty()) {
      int sid = int_from_string_with_prefix(value, "SID:");
      output.set_sid(sid);
      is >> value;
      int spi_sid = int_from_string_with_prefix(value, "SPI_SID:");
      assert(spi_sid == output.spi_sid());
   }

   add_output(output);
   return true;
}


bool Shader::read_input(std::istream& is)
{
   string value;
   is >> value;
   int pos = int_from_string_with_prefix(value, "LOC:");
   is >> value;
   int name = int_from_string_with_prefix(value, "NAME:");

   value.clear();

   ShaderInput input(pos, name);

   int interp = 0;
   int interp_loc = 0;
   bool use_centroid = false;

   is >> value;
   while (!value.empty()) {
      if (value.substr(0, 4) == "SID:") {
         int sid = int_from_string_with_prefix(value, "SID:");
         input.set_sid(sid);
      } else if (value.substr(0, 8) == "SPI_SID:") {
         int spi_sid = int_from_string_with_prefix(value, "SPI_SID:");
         assert(spi_sid == input.spi_sid());
      } else if (value.substr(0, 7) == "INTERP:") {
         interp = int_from_string_with_prefix(value, "INTERP:");
      } else if (value.substr(0, 5) == "ILOC:") {
         interp_loc = int_from_string_with_prefix(value, "ILOC:");
      } else if (value == "USE_CENTROID") {
         use_centroid = true;
      } else {
         std::cerr << "Unknown parse value '" << value << "'";
         assert(!value.c_str());
      }
      value.clear();
      is >> value;
   }

   input.set_interpolator(interp, interp_loc, use_centroid);

   add_input(input);
   return true;
}

bool Shader::allocate_registers_from_string(std::istream& is, Pin pin)
{
   std::string line;
   if (!std::getline(is, line))
      return false;

   std::istringstream iline(line);

   while (!iline.eof())  {
      string reg_str;
      iline >> reg_str;

      if (reg_str.empty())
         break;

      if (strchr(reg_str.c_str(), '@')) {
         value_factory().dest_from_string(reg_str);
      } else {
         RegisterVec4::Swizzle swz = {0,1,2,3};
         auto regs = value_factory().dest_vec4_from_string(reg_str, swz, pin);
         for (int i = 0; i < 4; ++i) {
            if (swz[i] < 4 && pin == pin_fully) {
               regs[i]->pin_live_range(true, false);
            }
         }
      }
   }
   return true;
}

bool Shader::allocate_arrays_from_string(std::istream& is)
{
   std::string line;
   if (!std::getline(is, line))
      return false;

   std::istringstream iline(line);

   while (!iline.eof())  {
      string reg_str;
      iline >> reg_str;

      if (reg_str.empty())
         break;

      value_factory().array_from_string(reg_str);
   }
   return true;
}

bool Shader::read_chipclass(std::istream& is)
{
   string name;
   is >> name;
   if (name == "R600")
      m_chip_class = ISA_CC_R600;
   else if (name == "R700")
      m_chip_class = ISA_CC_R700;
   else if (name == "EVERGREEN")
      m_chip_class = ISA_CC_EVERGREEN;
   else if (name == "CAYMAN")
      m_chip_class = ISA_CC_CAYMAN;
   else
      return false;
   return true;
}

void Shader::allocate_reserved_registers()
{
   m_instr_factory->value_factory().set_virtual_register_base(0);
   auto reserved_registers_end = do_allocate_reserved_registers();
   m_instr_factory->value_factory().set_virtual_register_base(reserved_registers_end);
   if (!m_atomics.empty()) {
      m_atomic_update = value_factory().temp_register();
      auto alu = new AluInstr(op1_mov, m_atomic_update,
                              value_factory().one_i(),
                              AluInstr::last_write);
      alu->set_alu_flag(alu_no_schedule_bias);
      emit_instruction(alu);
   }

   if(m_flags.test(sh_needs_sbo_ret_address)) {
      m_rat_return_address = value_factory().temp_register(0);
      auto temp0 = value_factory().temp_register(0);
      auto temp1 = value_factory().temp_register(1);
      auto temp2 = value_factory().temp_register(2);

      auto group = new AluGroup();
      group->add_instruction(new AluInstr(op1_mbcnt_32lo_accum_prev_int, temp0, value_factory().literal(-1), {alu_write}));
      group->add_instruction(new AluInstr(op1_mbcnt_32hi_int, temp1, value_factory().literal(-1), {alu_write}));
      emit_instruction(group);
      emit_instruction(new AluInstr(op3_muladd_uint24, temp2, value_factory().inline_const(ALU_SRC_SE_ID, 0),
                                          value_factory().literal(256), value_factory().inline_const(ALU_SRC_HW_WAVE_ID, 0), {alu_write, alu_last_instr}));
      emit_instruction(new AluInstr(op3_muladd_uint24, m_rat_return_address,
                                    temp2, value_factory().literal(0x40), temp0,
                                    {alu_write, alu_last_instr}));
   }
}

Shader *Shader::translate_from_nir(nir_shader *nir, const pipe_stream_output_info *so_info,
                                   struct r600_shader* gs_shader,
                                   r600_shader_key& key, r600_chip_class chip_class)
{
   Shader *shader = nullptr;

   switch (nir->info.stage) {
   case MESA_SHADER_FRAGMENT:
      if (chip_class >= ISA_CC_EVERGREEN)
         shader = new FragmentShaderEG(key);
      else
         shader = new FragmentShaderR600(key);
   break;
   case MESA_SHADER_VERTEX:
      shader = new VertexShader(so_info, gs_shader, key);
   break;
   case MESA_SHADER_GEOMETRY:
      shader = new GeometryShader(key);
   break;
   case MESA_SHADER_TESS_CTRL:
      shader = new TCSShader(key);
      break;
   case MESA_SHADER_TESS_EVAL:
      shader = new TESShader(so_info, gs_shader, key);
      break;
   case MESA_SHADER_COMPUTE:
      shader = new ComputeShader(key);
      break;
   default:
      return nullptr;
   }

   shader->set_info(nir);

   shader->set_chip_class(chip_class);
   if (!shader->process(nir))
      return nullptr;

   return shader;
}

void Shader::set_info(nir_shader *nir)
{
   m_scratch_size = nir->scratch_size;
}

ValueFactory& Shader::value_factory()
{
   return m_instr_factory->value_factory();
}


bool Shader::process(nir_shader *nir)
{
   m_ssbo_image_offset = nir->info.num_images;

   if (nir->info.use_legacy_math_rules)
      set_flag(sh_legacy_math_rules);

   nir_foreach_uniform_variable(var, nir)
         scan_uniforms(var);

   // at this point all functions should be inlined
   const nir_function *func = reinterpret_cast<const nir_function *>(exec_list_get_head_const(&nir->functions));

   if (!scan_shader(func))
      return false;

   allocate_reserved_registers();

   allocate_local_registers(&func->impl->registers);

   sfn_log << SfnLog::trans << "Process shader \n";
   foreach_list_typed(nir_cf_node, node, node, &func->impl->body) {
      if (!process_cf_node(node))
         return false;
   }

   finalize();

   return true;
}

void Shader::allocate_local_registers(const exec_list *registers)
{
   if (value_factory().allocate_registers(registers))
      m_indirect_files |= 1 << TGSI_FILE_TEMPORARY;
}

bool Shader::scan_shader(const nir_function *func)
{

   nir_foreach_block(block, func->impl) {
      nir_foreach_instr(instr, block) {
         if (!scan_instruction(instr)) {
            fprintf(stderr, "Unhandled sysvalue access ");
            nir_print_instr(instr, stderr);
            fprintf(stderr, "\n");
            return false;
         }
      }
   }

   int lds_pos = 0;
   for (auto& [index, input] : m_inputs) {
      if (input.need_lds_pos()) {
         if (chip_class() < ISA_CC_EVERGREEN)
            input.set_gpr(lds_pos);
         input.set_lds_pos(lds_pos++);
      }
   }

   int param_id = 0;
   for (auto& [index, out] : m_outputs) {
      if (out.is_param())
         out.set_pos(param_id++);
   }

   return true;
}

bool Shader::scan_uniforms(nir_variable *uniform)
{
   if (uniform->type->contains_atomic()) {
      int natomics = uniform->type->atomic_size() / ATOMIC_COUNTER_SIZE;
      m_nhwatomic += natomics;

      if (uniform->type->is_array())
         m_indirect_files |= 1 << TGSI_FILE_HW_ATOMIC;

      m_flags.set(sh_uses_atomics);

      r600_shader_atomic atom = {0};

      atom.buffer_id = uniform->data.binding;
      atom.hw_idx = m_atomic_base + m_next_hwatomic_loc;

      atom.start = uniform->data.offset >> 2;
      atom.end = atom.start + natomics - 1;

      if (m_atomic_base_map.find(uniform->data.binding) ==
          m_atomic_base_map.end())
         m_atomic_base_map[uniform->data.binding] = m_next_hwatomic_loc;

      m_next_hwatomic_loc += natomics;

      m_atomic_file_count += atom.end  - atom.start + 1;

      sfn_log << SfnLog::io << "HW_ATOMIC file count: "
              << m_atomic_file_count << "\n";

      m_atomics.push_back(atom);
   }

   auto type = uniform->type->is_array() ? uniform->type->without_array(): uniform->type;
   if (type->is_image() || uniform->data.mode == nir_var_mem_ssbo) {
      m_flags.set(sh_uses_images);
      if (uniform->type->is_array() && ! (uniform->data.mode == nir_var_mem_ssbo))
         m_indirect_files |= 1 << TGSI_FILE_IMAGE;
   }

   return true;
}


bool Shader::scan_instruction(nir_instr *instr)
{
   if (do_scan_instruction(instr))
      return true;

   if (instr->type != nir_instr_type_intrinsic)
      return true;

   auto intr = nir_instr_as_intrinsic(instr);

   // handle unhandled instructions
   switch (intr->intrinsic) {
   case nir_intrinsic_ssbo_atomic_add:
   case nir_intrinsic_ssbo_atomic_comp_swap:
   case nir_intrinsic_ssbo_atomic_or:
   case nir_intrinsic_ssbo_atomic_xor:
   case nir_intrinsic_ssbo_atomic_imax:
   case nir_intrinsic_ssbo_atomic_imin:
   case nir_intrinsic_ssbo_atomic_umax:
   case nir_intrinsic_ssbo_atomic_umin:
   case nir_intrinsic_ssbo_atomic_and:
   case nir_intrinsic_ssbo_atomic_exchange:
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_atomic_add:
   case nir_intrinsic_image_atomic_and:
   case nir_intrinsic_image_atomic_or:
   case nir_intrinsic_image_atomic_xor:
   case nir_intrinsic_image_atomic_exchange:
   case nir_intrinsic_image_atomic_comp_swap:
   case nir_intrinsic_image_atomic_umin:
   case nir_intrinsic_image_atomic_umax:
   case nir_intrinsic_image_atomic_imin:
   case nir_intrinsic_image_atomic_imax:
      m_flags.set(sh_needs_sbo_ret_address);
      FALLTHROUGH;
   case nir_intrinsic_image_store:
   case nir_intrinsic_store_ssbo:
      m_flags.set(sh_writes_memory);
      m_flags.set(sh_uses_images);
      break;
   case nir_intrinsic_memory_barrier_image:
   case nir_intrinsic_memory_barrier_buffer:
   case nir_intrinsic_memory_barrier:
   case nir_intrinsic_group_memory_barrier:
      m_chain_instr.prepare_mem_barrier = true;
   default:
      ;
   }
   return true;
}

bool Shader::process_cf_node(nir_cf_node *node)
{
   SFN_TRACE_FUNC(SfnLog::flow, "CF");

   switch (node->type) {
   case nir_cf_node_block:
   return process_block(nir_cf_node_as_block(node));
   case nir_cf_node_if:
   return process_if(nir_cf_node_as_if(node));
   case nir_cf_node_loop:
   return process_loop(nir_cf_node_as_loop(node));
   default:
   return false;
   }

}

static bool
child_block_empty (const exec_list& list)
{
   if (list.is_empty())
      return true;

   bool result = true;

   foreach_list_typed(nir_cf_node, n, node, &list) {

      if (n->type == nir_cf_node_block) {
         if (!nir_cf_node_as_block(n)->instr_list.is_empty())
            return false;
      }
      if (n->type == nir_cf_node_if)
         return false;
   }
   return result;
}

bool Shader::process_if(nir_if *if_stmt)
{
   SFN_TRACE_FUNC(SfnLog::flow, "IF");

   if (!emit_if_start(if_stmt))
      return false;

   foreach_list_typed(nir_cf_node, n, node, &if_stmt->then_list) {
      SFN_TRACE_FUNC(SfnLog::flow, "IF-then");
         if (!process_cf_node(n))
            return false;
   }

   if (!child_block_empty(if_stmt->else_list)) {
      if (!emit_control_flow(ControlFlowInstr::cf_else))
         return false;
      foreach_list_typed(nir_cf_node, n, node, &if_stmt->else_list)
            if (!process_cf_node(n)) return false;
   }

   if (!emit_control_flow(ControlFlowInstr::cf_endif))
      return false;

   return true;
}

bool Shader::emit_if_start(nir_if *if_stmt)
{
   auto value = value_factory().src(if_stmt->condition, 0);
   AluInstr *pred = new AluInstr(op2_pred_setne_int, value_factory().temp_register(),
                                 value, value_factory().zero(), AluInstr::last);
   pred->set_alu_flag(alu_update_exec);
   pred->set_alu_flag(alu_update_pred);
   pred->set_cf_type(cf_alu_push_before);

   IfInstr *ir = new IfInstr(pred);
   emit_instruction(ir);
   start_new_block(1);
   return true;
}

bool Shader::emit_control_flow(ControlFlowInstr::CFType type)
{
   auto ir = new ControlFlowInstr(type);
   emit_instruction(ir);
   int depth = 0;
   switch (type) {
   case ControlFlowInstr::cf_loop_begin:
      m_loops.push_back(ir);
      m_nloops++;
      depth = 1;
   break;
   case ControlFlowInstr::cf_loop_end:
      m_loops.pop_back();
      FALLTHROUGH;
   case ControlFlowInstr::cf_endif:
      depth = -1;
   break;
   default:
      ;
   }

   start_new_block(depth);
   return true;
}

bool Shader::process_loop(nir_loop *node)
{
   SFN_TRACE_FUNC(SfnLog::flow, "LOOP");
   if (!emit_control_flow(ControlFlowInstr::cf_loop_begin))
      return false;

   foreach_list_typed(nir_cf_node, n, node, &node->body)
         if (!process_cf_node(n)) return false;

   if (!emit_control_flow(ControlFlowInstr::cf_loop_end))
      return false;

   return true;
}

bool Shader::process_block(nir_block *block)
{
   SFN_TRACE_FUNC(SfnLog::flow, "BLOCK");

   nir_foreach_instr(instr, block) {
      sfn_log << SfnLog::instr << "FROM:" << *instr << "\n";
      bool r = process_instr(instr);
      if (!r) {
         sfn_log << SfnLog::err << "R600: Unsupported instruction: "
                 << *instr << "\n";
         return false;
      }
   }
   return true;
}

bool Shader::process_instr(nir_instr *instr)
{
   return m_instr_factory->from_nir(instr, *this);
}

bool Shader::process_intrinsic(nir_intrinsic_instr *intr)
{
   if (process_stage_intrinsic(intr))
      return true;

   if (GDSInstr::emit_atomic_counter(intr, *this)) {
      set_flag(sh_writes_memory);
      return true;
   }

   if (RatInstr::emit(intr, *this))
      return true;

   switch (intr->intrinsic) {
   case nir_intrinsic_store_output: return store_output(intr);
   case nir_intrinsic_load_input: return load_input(intr);
   case nir_intrinsic_load_uniform: return load_uniform(intr);
   case nir_intrinsic_load_ubo_vec4: return load_ubo(intr);
   case nir_intrinsic_store_scratch: return emit_store_scratch(intr);
   case nir_intrinsic_load_scratch: return emit_load_scratch(intr);
   case nir_intrinsic_store_local_shared_r600: return emit_local_store(intr);
   case nir_intrinsic_load_local_shared_r600: return emit_local_load(intr);
   case nir_intrinsic_load_tcs_in_param_base_r600: return emit_load_tcs_param_base(intr, 0);
   case nir_intrinsic_load_tcs_out_param_base_r600: return emit_load_tcs_param_base(intr, 16);
      // We only emit the group barrier, barriers across work groups
      // are not yet implemented
   case nir_intrinsic_control_barrier:
   case nir_intrinsic_memory_barrier_tcs_patch:
   case nir_intrinsic_memory_barrier_shared:
      return emit_barrier(intr);
   case nir_intrinsic_memory_barrier_atomic_counter:
      return true;
   case nir_intrinsic_group_memory_barrier:
   case nir_intrinsic_memory_barrier_image:
   case nir_intrinsic_memory_barrier_buffer:
   case nir_intrinsic_memory_barrier:
      return emit_wait_ack();

   case nir_intrinsic_shared_atomic_add:
   case nir_intrinsic_shared_atomic_and:
   case nir_intrinsic_shared_atomic_or:
   case nir_intrinsic_shared_atomic_imax:
   case nir_intrinsic_shared_atomic_umax:
   case nir_intrinsic_shared_atomic_imin:
   case nir_intrinsic_shared_atomic_umin:
   case nir_intrinsic_shared_atomic_xor:
   case nir_intrinsic_shared_atomic_exchange:
   case nir_intrinsic_shared_atomic_comp_swap:
      return emit_atomic_local_shared(intr);
   case nir_intrinsic_shader_clock:
      return emit_shader_clock(intr);

   default:
   return false;
   }
}

static ESDOp
lds_op_from_intrinsic(nir_intrinsic_op op, bool ret) {
   switch (op) {
   case nir_intrinsic_shared_atomic_add:
      return ret ? LDS_ADD_RET : LDS_ADD;
   case nir_intrinsic_shared_atomic_and:
      return ret ? LDS_AND_RET : LDS_AND;
   case nir_intrinsic_shared_atomic_or:
      return ret ? LDS_OR_RET : LDS_OR;
   case nir_intrinsic_shared_atomic_imax:
      return ret ? LDS_MAX_INT_RET : LDS_MAX_INT;
   case nir_intrinsic_shared_atomic_umax:
      return ret ? LDS_MAX_UINT_RET : LDS_MAX_UINT;
   case nir_intrinsic_shared_atomic_imin:
      return ret ? LDS_MIN_INT_RET : LDS_MIN_INT;
   case nir_intrinsic_shared_atomic_umin:
      return ret ? LDS_MIN_UINT_RET : LDS_MIN_UINT;
   case nir_intrinsic_shared_atomic_xor:
      return ret ? LDS_XOR_RET : LDS_XOR;
   case nir_intrinsic_shared_atomic_exchange:
      return LDS_XCHG_RET;
   case nir_intrinsic_shared_atomic_comp_swap:
      return LDS_CMP_XCHG_RET;
   default:
      unreachable("Unsupported shared atomic opcode");
   }
}

PRegister Shader::emit_load_to_register(PVirtualValue src)
{
   assert(src);
   PRegister dest = src->as_register();

   if (!dest) {
      dest = value_factory().temp_register();
      emit_instruction(new AluInstr(op1_mov, dest, src, AluInstr::last_write));
   }
   return dest;
}

bool Shader::emit_atomic_local_shared(nir_intrinsic_instr* instr)
{
   bool uses_retval = !instr->dest.is_ssa || !list_is_empty(&instr->dest.ssa.uses);

   auto& vf = value_factory();

   auto dest_value = uses_retval ? vf.dest(instr->dest, 0, pin_free) : nullptr;

   auto op = lds_op_from_intrinsic(instr->intrinsic, uses_retval);

   auto address = vf.src(instr->src[0], 0);

   AluInstr::SrcValues src;
   src.push_back(vf.src(instr->src[1], 0));

   if (unlikely(instr->intrinsic ==nir_intrinsic_shared_atomic_comp_swap))
      src.push_back(vf.src(instr->src[2], 0));
   emit_instruction(new LDSAtomicInstr(op, dest_value, address, src));
   return true;
}

auto Shader::evaluate_resource_offset(nir_intrinsic_instr *instr, int src_id) -> std::pair<int, PRegister>
{
   auto& vf = value_factory();

   PRegister uav_id{nullptr};
   int offset = 0;

   auto uav_id_const = nir_src_as_const_value(instr->src[src_id]);
   if (uav_id_const) {
      offset += uav_id_const->u32;
   } else {
      auto uav_id_val = vf.src(instr->src[src_id], 0);
      if (uav_id_val->as_register()) {
         uav_id = uav_id_val->as_register();
      } else {
         uav_id = vf.temp_register();
         emit_instruction(new AluInstr(op1_mov, uav_id, uav_id_val,
                                 AluInstr::last_write));
      }
   }
   return std::make_pair(offset, uav_id);
}


bool Shader::emit_store_scratch(nir_intrinsic_instr *intr)
{
   auto& vf = m_instr_factory->value_factory();

   int writemask = nir_intrinsic_write_mask(intr);


   RegisterVec4::Swizzle swz = {7,7,7,7};

   for (unsigned i = 0; i < intr->num_components; ++i)
      swz[i] = (1 << i) & writemask ? i : 7;

   auto value = vf.temp_vec4(pin_group, swz);
   AluInstr *ir = nullptr;
   for (unsigned i = 0; i < intr->num_components; ++i) {
      if (value[i]->chan() < 4) {
         ir = new AluInstr(op1_mov, value[i], vf.src(intr->src[0], i), AluInstr::write);
         ir->set_alu_flag(alu_no_schedule_bias);
         emit_instruction(ir);
      }
   }
   if (!ir)
      return true;

   ir->set_alu_flag(alu_last_instr);

   auto address = vf.src(intr->src[1], 0);


   int align = nir_intrinsic_align_mul(intr);
   int align_offset = nir_intrinsic_align_offset(intr);

   ScratchIOInstr *ws_ir = nullptr;

   int offset = -1;
   if (address->as_literal()) {
      offset = address->as_literal()->value();
   } else if (address->as_inline_const()) {
      auto il = address->as_inline_const();
      if (il->sel() == ALU_SRC_0)
         offset = 0;
      else if (il->sel() == ALU_SRC_1_INT)
         offset = 1;
   }

   if (offset >= 0) {
      ws_ir = new ScratchIOInstr(value, offset, align, align_offset, writemask);
   } else {
      auto addr_temp  = vf.temp_register(0);      
      auto load_addr = new AluInstr(op1_mov, addr_temp, address, AluInstr::last_write);
      load_addr->set_alu_flag(alu_no_schedule_bias);
      emit_instruction(load_addr);

      ws_ir = new ScratchIOInstr(value, addr_temp, align, align_offset, writemask, m_scratch_size);
   }
   emit_instruction(ws_ir);

   m_flags.set(sh_needs_scratch_space);
   return true;
}

bool Shader::emit_load_scratch(nir_intrinsic_instr *intr)
{
   auto addr = value_factory().src(intr->src[0], 0);
   auto dest = value_factory().dest_vec4(intr->dest, pin_group);

   if (chip_class() >= ISA_CC_R700) {
      RegisterVec4::Swizzle dest_swz = {7,7,7,7};

      for (unsigned i = 0; i < intr->num_components; ++i)
         dest_swz[i] = i;

      auto *ir = new LoadFromScratch(dest, dest_swz, addr, m_scratch_size);
      emit_instruction(ir);
      chain_scratch_read(ir);
   } else {
      int align = nir_intrinsic_align_mul(intr);
      int align_offset = nir_intrinsic_align_offset(intr);


      int offset = -1;
      if (addr->as_literal()) {
         offset = addr->as_literal()->value();
      } else if (addr->as_inline_const()) {
         auto il = addr->as_inline_const();
         if (il->sel() == ALU_SRC_0)
            offset = 0;
         else if (il->sel() == ALU_SRC_1_INT)
            offset = 1;
      }

      ScratchIOInstr *ir = nullptr;
      if (offset >= 0) {
         ir = new ScratchIOInstr(dest, offset, align, align_offset, 0xf, true);
      } else {
         auto addr_temp  = value_factory().temp_register(0);
         auto load_addr = new AluInstr(op1_mov, addr_temp, addr, AluInstr::last_write);
         load_addr->set_alu_flag(alu_no_schedule_bias);
         emit_instruction(load_addr);

         ir = new ScratchIOInstr(dest, addr_temp, align, align_offset, 0xf,
                                 m_scratch_size, true);
      }
      emit_instruction(ir);
   }


   m_flags.set(sh_needs_scratch_space);

   return true;

}

bool Shader::emit_local_store(nir_intrinsic_instr *instr)
{
   unsigned write_mask = nir_intrinsic_write_mask(instr);

   auto address = value_factory().src(instr->src[1], 0);
   int swizzle_base = 0;
   unsigned w = write_mask;
   while (!(w & 1)) {
      ++swizzle_base;
      w >>= 1;
   }
   write_mask = write_mask >> swizzle_base;

   if ((write_mask & 3) != 3) {
      auto value = value_factory().src(instr->src[0], swizzle_base);
      emit_instruction(new LDSAtomicInstr(LDS_WRITE, nullptr, address, {value}));
   } else {
      auto value = value_factory().src(instr->src[0], swizzle_base);
      auto value1 = value_factory().src(instr->src[0], swizzle_base + 1);
      emit_instruction(new LDSAtomicInstr(LDS_WRITE_REL, nullptr, address, {value, value1}));
   }
   return true;
}

bool Shader::emit_local_load(nir_intrinsic_instr* instr)
{
   auto address = value_factory().src_vec(instr->src[0], instr->num_components);
   auto dest_value = value_factory().dest_vec(instr->dest, instr->num_components);
   emit_instruction(new LDSReadInstr(dest_value, address));
   return true;
}

void Shader::chain_scratch_read(Instr *instr)
{
   m_chain_instr.apply(instr, &m_chain_instr.last_scratch_instr);
}

void Shader::chain_ssbo_read(Instr *instr)
{
   m_chain_instr.apply(instr, &m_chain_instr.last_ssbo_instr);
}

bool Shader::emit_wait_ack()
{
   start_new_block(0);
   emit_instruction(new ControlFlowInstr(ControlFlowInstr::cf_wait_ack));
   start_new_block(0);
   return true;
}

void Shader::InstructionChain::visit(ScratchIOInstr *instr)
{
   apply(instr, &last_scratch_instr);
}

void Shader::InstructionChain::visit(GDSInstr *instr)
{
   apply(instr, &last_gds_instr);
   Instr::Flags flag = instr->has_instr_flag(Instr::helper) ?
                          Instr::helper: Instr::vpm;
   for (auto& loop : this_shader->m_loops) {
      loop->set_instr_flag(flag);
   }
}

void Shader::InstructionChain::visit(RatInstr *instr)
{
   apply(instr, &last_ssbo_instr);
   Instr::Flags flag = instr->has_instr_flag(Instr::helper) ?
                          Instr::helper: Instr::vpm;
   for (auto& loop : this_shader->m_loops) {
      loop->set_instr_flag(flag);
   }

   if (prepare_mem_barrier)
      instr->set_ack();

   if (this_shader->m_current_block->inc_rat_emitted() > 15)
      this_shader->start_new_block(0);
}

void Shader::InstructionChain::apply(Instr *current, Instr **last) {
   if (*last)
      current->add_required_instr(*last);
   *last = current;
}

void Shader::emit_instruction(PInst instr)
{
   sfn_log << SfnLog::instr << "   " << *instr << "\n";
   instr->accept(m_chain_instr);
   m_current_block->push_back(instr);
}

bool Shader::load_uniform(nir_intrinsic_instr *intr)
{
   auto literal = nir_src_as_const_value(intr->src[0]);

   if (literal) {
      AluInstr *ir = nullptr;
      auto pin = intr->dest.is_ssa && nir_dest_num_components(intr->dest) == 1 ?
               pin_free : pin_none;
      for (unsigned i = 0; i < nir_dest_num_components(intr->dest); ++i) {

         sfn_log << SfnLog::io << "uniform "
                 << intr->dest.ssa.index << " const["<< i << "]: "<< intr->const_index[i] << "\n";

         auto uniform = value_factory().uniform(intr, i);
         ir = new AluInstr(op1_mov, value_factory().dest(intr->dest, i, pin),
                           uniform, {alu_write});
         emit_instruction(ir);
      }
      if (ir)
         ir->set_alu_flag(alu_last_instr);
      return true;
   } else {
      auto addr = value_factory().src(intr->src[0], 0);
      return load_uniform_indirect(intr, addr, 16 * nir_intrinsic_base(intr), 0);
   }
}

bool Shader::load_uniform_indirect(nir_intrinsic_instr *intr, PVirtualValue addr,
                                   int offset , int buffer_id)
{
   auto addr_reg = addr->as_register();
   if (!addr) {
      auto tmp = value_factory().temp_register();
      emit_instruction(new AluInstr(op1_mov, tmp, addr, AluInstr::last_write));
      addr = tmp;
   }

   RegisterVec4 dest = value_factory().dest_vec4(intr->dest, pin_group);

   auto ir = new LoadFromBuffer(dest, {0,1,2,3}, addr_reg, offset, buffer_id,
                                nullptr, fmt_32_32_32_32_float);
   emit_instruction(ir);
   m_flags.set(sh_indirect_const_file);
   return true;
}

bool Shader::emit_load_tcs_param_base(nir_intrinsic_instr* instr, int offset)
{
   auto src = value_factory().temp_register();
   emit_instruction(new AluInstr(op1_mov, src, value_factory().zero(),
                                 AluInstr::last_write));

   auto dest = value_factory().dest_vec4(instr->dest, pin_group);
   auto fetch = new LoadFromBuffer(dest, {0,1,2,3}, src, offset,
                                   R600_LDS_INFO_CONST_BUFFER, nullptr,
                                   fmt_32_32_32_32);

   fetch->set_fetch_flag(LoadFromBuffer::srf_mode);
   emit_instruction(fetch);

   return true;
}

bool Shader::emit_shader_clock(nir_intrinsic_instr* instr)
{
   auto& vf = value_factory();
   auto group = new AluGroup();
   group->add_instruction(new AluInstr(op1_mov, vf.dest(instr->dest, 0, pin_chan),
                                       vf.inline_const(ALU_SRC_TIME_LO, 0), AluInstr::write));
   group->add_instruction(new AluInstr(op1_mov, vf.dest(instr->dest, 1, pin_chan),
                                       vf.inline_const(ALU_SRC_TIME_HI, 0), AluInstr::last_write));
   emit_instruction(group);
   return true;
}


bool Shader::emit_barrier(nir_intrinsic_instr* intr)
{
   (void)intr;
   /* Put barrier into it's own block, so that optimizers and the
    * scheduler don't move code */
   start_new_block(0);
   auto op = new AluInstr(op0_group_barrier, 0);
   op->set_alu_flag(alu_last_instr);
   emit_instruction(op);
   start_new_block(0);
   return true;
}

bool Shader::load_ubo(nir_intrinsic_instr *instr)
{
   auto bufid = nir_src_as_const_value(instr->src[0]);
   auto buf_offset = nir_src_as_const_value(instr->src[1]);

   if (!buf_offset) {
      /* TODO: if bufid is constant then this can also be solved by using the CF indes
       * on the ALU block, and this would probably make sense when there are more then one
       * loads with the same buffer ID. */

      auto addr = value_factory().src(instr->src[1], 0)->as_register();
      RegisterVec4::Swizzle dest_swz {7,7,7,7};
      auto dest = value_factory().dest_vec4(instr->dest, pin_group);

      for (unsigned i = 0; i < nir_dest_num_components(instr->dest); ++i) {
         dest_swz[i] = i + nir_intrinsic_component(instr);
      }

      LoadFromBuffer *ir;
      if (bufid) {
         ir = new LoadFromBuffer(dest, dest_swz, addr, 0, 1 + bufid->u32,
                                 nullptr, fmt_32_32_32_32_float);
      } else {
         auto buffer_id = emit_load_to_register(value_factory().src(instr->src[0], 0));
         ir = new LoadFromBuffer(dest, dest_swz, addr, 0, 1, buffer_id,
                                 fmt_32_32_32_32_float);
      }
      emit_instruction(ir);
      return true;
   }

   /* direct load using the constant cache */
   if (bufid) {
      int buf_cmp = nir_intrinsic_component(instr);

      AluInstr *ir = nullptr;
      auto pin = instr->dest.is_ssa && nir_dest_num_components(instr->dest) == 1 ?
                    pin_free : pin_none;
      for (unsigned i = 0; i < nir_dest_num_components(instr->dest); ++i) {

         sfn_log << SfnLog::io << "UBO[" << bufid << "] "
                 << instr->dest.ssa.index << " const["<< i << "]: "<< instr->const_index[i] << "\n";

         auto uniform = value_factory().uniform(512 +  buf_offset->u32, i + buf_cmp, bufid->u32 + 1);
         ir = new AluInstr(op1_mov, value_factory().dest(instr->dest, i, pin),
                           uniform, {alu_write});
         emit_instruction(ir);
      }
      if (ir)
         ir->set_alu_flag(alu_last_instr);
      return true;
   } else {
      int buf_cmp = nir_intrinsic_component(instr);
      AluInstr *ir = nullptr;
      auto kc_id = value_factory().src(instr->src[0], 0);

      for (unsigned i = 0; i < nir_dest_num_components(instr->dest); ++i) {
         int cmp = buf_cmp + i;
         auto u = new UniformValue(512 +  buf_offset->u32, cmp, kc_id);
         auto dest = value_factory().dest(instr->dest, i, pin_none);
         ir = new AluInstr(op1_mov,  dest, u, AluInstr::write);
         emit_instruction(ir);
      }
      if (ir)
         ir->set_alu_flag(alu_last_instr);
      m_indirect_files |= 1 << TGSI_FILE_CONSTANT;
      return true;
   }
}

void Shader::start_new_block(int depth)
{
   int depth_offset = m_current_block ? m_current_block->nesting_depth() : 0;
   m_current_block = new Block(depth + depth_offset, m_next_block++);
   m_root.push_back(m_current_block);
}

bool Shader::emit_simple_mov(nir_dest& dest, int chan, PVirtualValue src, Pin pin)
{
   auto dst = value_factory().dest(dest, chan, pin);
   emit_instruction(new AluInstr(op1_mov, dst, src, AluInstr::last_write));
   return true;
}

void Shader::print(std::ostream& os) const
{
   print_header(os);

   for (auto& [dummy, i]: m_inputs) {
      i.print(os);
      os << "\n";
   }

   for (auto& [dummy, o]: m_outputs) {
      o.print(os);
      os << "\n";
   }

   os << "SHADER\n";
   for (auto& b : m_root)
      b->print(os);
}

const char *chip_class_names[] = {
   "R600",
   "R700",
   "EVERGREEN",
   "CAYMAN"
};

void Shader::print_header(std::ostream& os) const
{
   assert(m_chip_class <= ISA_CC_CAYMAN);
   os << m_type_id << "\n";
   os << "CHIPCLASS " << chip_class_names[m_chip_class] << "\n";
   print_properties(os);
}

void Shader::print_properties(std::ostream& os) const
{
   do_print_properties(os);
}

bool Shader::equal_to(const Shader& other) const
{
   if (m_root.size() != other.m_root.size())
      return false;
   return std::inner_product(m_root.begin(), m_root.end(),
                             other.m_root.begin(),
                             true,
                             [](bool lhs, bool rhs){ return lhs & rhs;},
                             [](const Block::Pointer lhs, const Block::Pointer rhs) -> bool {
                                return lhs->is_equal_to(*rhs);
                             });
}

void Shader::get_shader_info(r600_shader *sh_info)
{
   sh_info->ninput = m_inputs.size();
   int lds_pos = 0;
   int input_array_array_loc = 0;
   for (auto& [index, info] : m_inputs) {
      r600_shader_io& io = sh_info->input[input_array_array_loc++];

      io.sid = info.sid();
      io.gpr = info.gpr();
      io.spi_sid = info.spi_sid();
      io.ij_index = info.ij_index();
      io.name = info.name();
      io.interpolate = info.interpolator();
      io.interpolate_location = info.interpolate_loc();
      if (info.need_lds_pos())
         io.lds_pos = lds_pos++;
      else
         io.lds_pos = 0;

      io.ring_offset = info.ring_offset();
      io.uses_interpolate_at_centroid = info.uses_interpolate_at_centroid();

      sfn_log << SfnLog::io << "Emit Input [" << index << "] sid:" << io.sid << " spi_sid:" << io.spi_sid << "\n";
      assert(io.spi_sid >= 0);
   }

   sh_info->nlds = lds_pos;
   sh_info->noutput = m_outputs.size();
   sh_info->num_loops = m_nloops;
   int output_array_array_loc = 0;

   for (auto& [index, info] : m_outputs) {
      r600_shader_io& io = sh_info->output[output_array_array_loc++];
      io.sid = info.sid();
      io.gpr = info.gpr();
      io.spi_sid = info.spi_sid();
      io.name = info.name();
      io.write_mask = info.writemask();

      sfn_log << SfnLog::io << "Emit output[" << index << "] sid:" << io.sid << " spi_sid:" << io.spi_sid << "\n";
      assert(io.spi_sid >= 0);
   }

   sh_info->nhwatomic = m_nhwatomic;
   sh_info->atomic_base = m_atomic_base;
   sh_info->nhwatomic_ranges = m_atomics.size();
   for (unsigned i = 0; i < m_atomics.size(); ++i)
      sh_info->atomics[i] = m_atomics[i];

   if (m_flags.test(sh_indirect_const_file))
         sh_info->indirect_files |= 1 << TGSI_FILE_CONSTANT;

   if (m_flags.test(sh_indirect_atomic))
      sh_info->indirect_files |= 1 << TGSI_FILE_HW_ATOMIC;

   sh_info->uses_tex_buffers = m_flags.test(sh_uses_tex_buffer);

   value_factory().get_shader_info(sh_info);

   sh_info->needs_scratch_space = m_flags.test(sh_needs_scratch_space);
   sh_info->uses_images = m_flags.test(sh_uses_images);
   sh_info->uses_atomics = m_flags.test(sh_uses_atomics);
   sh_info->has_txq_cube_array_z_comp = m_flags.test(sh_txs_cube_array_comp);
   sh_info->indirect_files = m_indirect_files;
   do_get_shader_info(sh_info);
}

PRegister Shader::atomic_update()
{
   assert(m_atomic_update);
   return m_atomic_update;
}

int Shader::remap_atomic_base(int base)
{
   return m_atomic_base_map[base];
}

void Shader::do_get_shader_info(r600_shader *sh_info)
{
   sh_info->uses_atomics = m_nhwatomic > 0;
}


const ShaderInput& Shader::input(int base) const
{
   auto io = m_inputs.find(base);
   assert(io != m_inputs.end());
   return io->second;
}

const ShaderOutput& Shader::output(int base) const
{
   auto io = m_outputs.find(base);
   assert(io != m_outputs.end());
   return io->second;
}

LiveRangeMap Shader::prepare_live_range_map()
{
   return m_instr_factory->value_factory().prepare_live_range_map();

}

void Shader::reset_function(ShaderBlocks& new_root)
{
   std::swap(m_root, new_root);
}

void Shader::finalize()
{
   do_finalize();
}

void Shader::do_finalize()
{

}

}
