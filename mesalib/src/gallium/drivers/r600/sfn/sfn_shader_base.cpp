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

#include "../r600_pipe.h"
#include "../r600_shader.h"
#include "sfn_shader_vertex.h"

#include "sfn_shader_compute.h"
#include "sfn_shader_fragment.h"
#include "sfn_shader_geometry.h"
#include "sfn_liverange.h"
#include "sfn_ir_to_assembly.h"
#include "sfn_nir.h"
#include "sfn_instruction_misc.h"
#include "sfn_instruction_fetch.h"
#include "sfn_instruction_lds.h"

#include <iostream>

#define ENABLE_DEBUG 1

#ifdef ENABLE_DEBUG
#define DEBUG_SFN(X)  \
   do {\
      X; \
   } while (0)
#else
#define DEBUG_SFN(X)
#endif

namespace r600 {

using namespace std;


ShaderFromNirProcessor::ShaderFromNirProcessor(pipe_shader_type ptype,
                                               r600_pipe_shader_selector& sel,
                                               r600_shader &sh_info, int scratch_size,
                                               enum chip_class chip_class,
                                               int atomic_base):
   m_processor_type(ptype),
   m_nesting_depth(0),
   m_block_number(0),
   m_export_output(0, -1),
   m_sh_info(sh_info),
   m_chip_class(chip_class),
   m_tex_instr(*this),
   m_alu_instr(*this),
   m_ssbo_instr(*this),
   m_pending_else(nullptr),
   m_scratch_size(scratch_size),
   m_next_hwatomic_loc(0),
   m_sel(sel),
   m_atomic_base(atomic_base),
   m_image_count(0),
   last_emitted_alu(nullptr)
{
   m_sh_info.processor_type = ptype;

}


ShaderFromNirProcessor::~ShaderFromNirProcessor()
{
}

bool ShaderFromNirProcessor::scan_instruction(nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_tex: {
      nir_tex_instr *t = nir_instr_as_tex(instr);
      if (t->sampler_dim == GLSL_SAMPLER_DIM_BUF)
         sh_info().uses_tex_buffers = true;
      if (t->op == nir_texop_txs &&
          t->sampler_dim == GLSL_SAMPLER_DIM_CUBE &&
          t->is_array)
         sh_info().has_txq_cube_array_z_comp = true;
      break;
   }
   case nir_instr_type_intrinsic: {
      auto *i = nir_instr_as_intrinsic(instr);
      switch (i->intrinsic) {
      case nir_intrinsic_ssbo_atomic_add:
      case nir_intrinsic_image_atomic_add:
      case nir_intrinsic_ssbo_atomic_and:
      case nir_intrinsic_image_atomic_and:
      case nir_intrinsic_ssbo_atomic_or:
      case nir_intrinsic_image_atomic_or:
      case nir_intrinsic_ssbo_atomic_imin:
      case nir_intrinsic_image_atomic_imin:
      case nir_intrinsic_ssbo_atomic_imax:
      case nir_intrinsic_image_atomic_imax:
      case nir_intrinsic_ssbo_atomic_umin:
      case nir_intrinsic_image_atomic_umin:
      case nir_intrinsic_ssbo_atomic_umax:
      case nir_intrinsic_image_atomic_umax:
      case nir_intrinsic_ssbo_atomic_xor:
      case nir_intrinsic_image_atomic_xor:
      case nir_intrinsic_ssbo_atomic_exchange:
      case nir_intrinsic_image_atomic_exchange:
      case nir_intrinsic_image_atomic_comp_swap:
      case nir_intrinsic_ssbo_atomic_comp_swap:
         m_sel.info.writes_memory = 1;
         FALLTHROUGH;
      case nir_intrinsic_image_load:
         m_ssbo_instr.set_require_rat_return_address();
         break;
      case nir_intrinsic_image_size: {
         if (nir_intrinsic_image_dim(i) == GLSL_SAMPLER_DIM_CUBE &&
             nir_intrinsic_image_array(i) && nir_dest_num_components(i->dest) > 2)
            sh_info().has_txq_cube_array_z_comp = true;
      }



      default:
         ;
      }


   }
   default:
      ;
   }

   return scan_sysvalue_access(instr);
}

enum chip_class ShaderFromNirProcessor::get_chip_class(void) const
{
  return m_chip_class;
}

bool ShaderFromNirProcessor::allocate_reserved_registers()
{
   bool retval = do_allocate_reserved_registers();
   m_ssbo_instr.load_rat_return_address();
   if (sh_info().uses_atomics)
      m_ssbo_instr.load_atomic_inc_limits();
   m_ssbo_instr.set_ssbo_offset(m_image_count);
   return retval;
}

static void remap_shader_info(r600_shader& sh_info,
                              std::vector<rename_reg_pair>& map,
                              UNUSED ValueMap& values)
{
   for (unsigned i = 0; i < sh_info.num_arrays; ++i) {
      auto new_index = map[sh_info.arrays[i].gpr_start];
      if (new_index.valid)
         sh_info.arrays[i].gpr_start = new_index.new_reg;
      map[sh_info.arrays[i].gpr_start].used = true;
   }

   for (unsigned i = 0; i < sh_info.ninput; ++i) {
      sfn_log << SfnLog::merge << "Input " << i << " gpr:" << sh_info.input[i].gpr
              << " of map.size()\n";

      assert(sh_info.input[i].gpr < map.size());
      auto new_index = map[sh_info.input[i].gpr];
      if (new_index.valid)
         sh_info.input[i].gpr = new_index.new_reg;
      map[sh_info.input[i].gpr].used = true;
   }

   for (unsigned i = 0; i < sh_info.noutput; ++i) {
      assert(sh_info.output[i].gpr < map.size());
      auto new_index = map[sh_info.output[i].gpr];
      if (new_index.valid)
         sh_info.output[i].gpr = new_index.new_reg;
      map[sh_info.output[i].gpr].used = true;
   }
}

void ShaderFromNirProcessor::remap_registers()
{
   // register renumbering
   auto rc = register_count();
   if (!rc)
      return;

   std::vector<register_live_range> register_live_ranges(rc);

   auto temp_register_map = get_temp_registers();

   Shader sh{m_output, temp_register_map};
   LiverangeEvaluator().run(sh, register_live_ranges);
   auto register_map = get_temp_registers_remapping(register_live_ranges);

   sfn_log << SfnLog::merge << "=========Mapping===========\n";
   for (size_t  i = 0; i < register_map.size(); ++i)
      if (register_map[i].valid)
         sfn_log << SfnLog::merge << "Map:" << i << " -> " << register_map[i].new_reg << "\n";

   ValueRemapper vmap0(register_map, temp_register_map);
   for (auto& block: m_output)
      block.remap_registers(vmap0);

   remap_shader_info(m_sh_info, register_map, temp_register_map);

   /* Mark inputs as used registers, these registers should no be remapped */
   for (auto& v: sh.m_temp) {
      if (v.second->type() == Value::gpr) {
         const auto& g = static_cast<const GPRValue&>(*v.second);
         if (g.is_input())
            register_map[g.sel()].used = true;
      }
   }

   int new_index = 0;
   for (auto& i : register_map) {
      i.valid = i.used;
      if (i.used)
         i.new_reg = new_index++;
   }

   ValueRemapper vmap1(register_map, temp_register_map);
   for (auto& ir: m_output)
      ir.remap_registers(vmap1);

   remap_shader_info(m_sh_info, register_map, temp_register_map);
}

bool ShaderFromNirProcessor::process_uniforms(nir_variable *uniform)
{
   // m_uniform_type_map
   m_uniform_type_map[uniform->data.location] = uniform->type;

   if (uniform->type->contains_atomic()) {
      int natomics = uniform->type->atomic_size() / ATOMIC_COUNTER_SIZE;
      sh_info().nhwatomic += natomics;

      if (uniform->type->is_array())
         sh_info().indirect_files |= 1 << TGSI_FILE_HW_ATOMIC;

      sh_info().uses_atomics = 1;

      struct r600_shader_atomic& atom = sh_info().atomics[sh_info().nhwatomic_ranges];
      ++sh_info().nhwatomic_ranges;
      atom.buffer_id = uniform->data.binding;
      atom.hw_idx = m_atomic_base + m_next_hwatomic_loc;

      atom.start = uniform->data.offset >> 2;
      atom.end = atom.start + natomics - 1;

      if (m_atomic_base_map.find(uniform->data.binding) ==
          m_atomic_base_map.end())
         m_atomic_base_map[uniform->data.binding] = m_next_hwatomic_loc;

      m_next_hwatomic_loc += natomics;

      m_sel.info.file_count[TGSI_FILE_HW_ATOMIC] += atom.end  - atom.start + 1;

      sfn_log << SfnLog::io << "HW_ATOMIC file count: "
              << m_sel.info.file_count[TGSI_FILE_HW_ATOMIC] << "\n";
   }

   auto type = uniform->type->is_array() ? uniform->type->without_array(): uniform->type;
   if (type->is_image() || uniform->data.mode == nir_var_mem_ssbo) {
      sh_info().uses_images = 1;
      if (uniform->type->is_array() && ! (uniform->data.mode == nir_var_mem_ssbo))
         sh_info().indirect_files |= 1 << TGSI_FILE_IMAGE;
   }

   return true;
}

void ShaderFromNirProcessor::set_shader_info(const nir_shader *sh)
{
   m_image_count = sh->info.num_images;
   do_set_shader_info(sh);
}

void ShaderFromNirProcessor::do_set_shader_info(const nir_shader *sh)
{
   (void)sh;
}

bool ShaderFromNirProcessor::scan_inputs_read(const nir_shader *sh)
{
   return true;
}

void ShaderFromNirProcessor::set_var_address(nir_deref_instr *instr)
{
   auto& dest = instr->dest;
   unsigned index = dest.is_ssa ? dest.ssa.index : dest.reg.reg->index;
   assert(util_bitcount(instr->modes) == 1);
   m_var_mode[instr->var] = instr->modes;
   m_var_derefs[index] = instr->var;

   sfn_log << SfnLog::io << "Add var deref:" << index
           << " with DDL:" << instr->var->data.driver_location << "\n";
}

void ShaderFromNirProcessor::evaluate_spi_sid(r600_shader_io& io)
{
   switch (io.name) {
   case TGSI_SEMANTIC_POSITION:
   case TGSI_SEMANTIC_PSIZE:
   case TGSI_SEMANTIC_EDGEFLAG:
   case TGSI_SEMANTIC_FACE:
   case TGSI_SEMANTIC_SAMPLEMASK:
   case TGSI_SEMANTIC_CLIPVERTEX:
      io.spi_sid = 0;
      break;
   case TGSI_SEMANTIC_GENERIC:
   case TGSI_SEMANTIC_TEXCOORD:
   case TGSI_SEMANTIC_PCOORD:
      io.spi_sid = io.sid + 1;
      break;
   default:
      /* For non-generic params - pack name and sid into 8 bits */
      io.spi_sid = (0x80 | (io.name << 3) | io.sid) + 1;
   }   
}

const nir_variable *ShaderFromNirProcessor::get_deref_location(const nir_src& src) const
{
   unsigned index = src.is_ssa ? src.ssa->index : src.reg.reg->index;

   sfn_log << SfnLog::io << "Search for deref:" << index << "\n";

   auto v = m_var_derefs.find(index);
   if (v != m_var_derefs.end())
      return v->second;

     fprintf(stderr, "R600: could not find deref with index %d\n", index);

     return nullptr;

   /*nir_deref_instr *deref = nir_instr_as_deref(src.ssa->parent_instr);
   return  nir_deref_instr_get_variable(deref); */
}

bool ShaderFromNirProcessor::emit_tex_instruction(nir_instr* instr)
{
   return m_tex_instr.emit(instr);
}

void ShaderFromNirProcessor::emit_instruction(AluInstruction *ir)
{
   if (last_emitted_alu && !last_emitted_alu->flag(alu_last_instr)) {
      for (unsigned i = 0; i < ir->n_sources(); ++i) {
         auto& s = ir->src(i);
         if (s.type() == Value::kconst) {
            auto& c = static_cast<UniformValue&>(s);
            if (c.addr()) {
               last_emitted_alu->set_flag(alu_last_instr);
               break;
            }
         }
      }
   }
   last_emitted_alu = ir;
   emit_instruction_internal(ir);
}


void ShaderFromNirProcessor::emit_instruction(Instruction *ir)
{

   emit_instruction_internal(ir);
   last_emitted_alu = nullptr;
}

void ShaderFromNirProcessor::emit_instruction_internal(Instruction *ir)
{
   if (m_pending_else) {
      append_block(-1);
      m_output.back().emit(PInstruction(m_pending_else));
      append_block(1);
      m_pending_else = nullptr;
   }

   r600::sfn_log << SfnLog::instr << "     as '" << *ir << "'\n";
   if (m_output.empty())
      append_block(0);

   m_output.back().emit(Instruction::Pointer(ir));
}

void ShaderFromNirProcessor::emit_shader_start()
{
   /* placeholder, may become an abstract method */
   m_ssbo_instr.set_ssbo_offset(m_image_count);
}

bool ShaderFromNirProcessor::emit_jump_instruction(nir_jump_instr *instr)
{
   switch (instr->type) {
   case nir_jump_break: {
      auto b = new LoopBreakInstruction();
      emit_instruction(b);
      return true;
   }
   case nir_jump_continue: {
      auto  b = new LoopContInstruction();
      emit_instruction(b);
      return true;
   }
   default: {
      nir_instr *i = reinterpret_cast<nir_instr*>(instr);
      sfn_log << SfnLog::err << "Jump instrunction " << *i <<  " not supported\n";
      return false;
   }
   }
   return true;
}

bool ShaderFromNirProcessor::emit_alu_instruction(nir_instr* instr)
{
   return m_alu_instr.emit(instr);
}

bool ShaderFromNirProcessor::emit_deref_instruction_override(UNUSED nir_deref_instr* instr)
{
   return false;
}

bool ShaderFromNirProcessor::emit_loop_start(int loop_id)
{
   LoopBeginInstruction *loop = new LoopBeginInstruction();
   emit_instruction(loop);
   m_loop_begin_block_map[loop_id] = loop;
   append_block(1);
   return true;
}
bool ShaderFromNirProcessor::emit_loop_end(int loop_id)
{
   auto start = m_loop_begin_block_map.find(loop_id);
   if (start == m_loop_begin_block_map.end()) {
      sfn_log << SfnLog::err  << "End loop: Loop start for "
              << loop_id << "  not found\n";
      return false;
   }
   m_nesting_depth--;
   m_block_number++;
   m_output.push_back(InstructionBlock(m_nesting_depth, m_block_number));
   LoopEndInstruction *loop = new LoopEndInstruction(start->second);
   emit_instruction(loop);

   m_loop_begin_block_map.erase(start);
   return true;
}

bool ShaderFromNirProcessor::emit_if_start(int if_id, nir_if *if_stmt)
{

   auto value = from_nir(if_stmt->condition, 0, 0);
   AluInstruction *pred = new AluInstruction(op2_pred_setne_int, PValue(new GPRValue(0,0)),
                                             value, Value::zero, EmitInstruction::last);
   pred->set_flag(alu_update_exec);
   pred->set_flag(alu_update_pred);
   pred->set_cf_type(cf_alu_push_before);

   append_block(1);

   IfInstruction *ir = new IfInstruction(pred);
   emit_instruction(ir);
   assert(m_if_block_start_map.find(if_id) == m_if_block_start_map.end());
   m_if_block_start_map[if_id] = ir;
   return true;
}

bool ShaderFromNirProcessor::emit_else_start(int if_id)
{
   auto iif = m_if_block_start_map.find(if_id);
   if (iif == m_if_block_start_map.end()) {
      std::cerr << "Error: ELSE branch " << if_id << " without starting conditional branch\n";
      return false;
   }

   if (iif->second->type() != Instruction::cond_if) {
      std::cerr << "Error: ELSE branch " << if_id << " not started by an IF branch\n";
      return false;
   }
   IfInstruction *if_instr = static_cast<IfInstruction *>(iif->second);
   ElseInstruction *ir = new ElseInstruction(if_instr);
   m_if_block_start_map[if_id] = ir;
   m_pending_else = ir;

   return true;
}

bool ShaderFromNirProcessor::emit_ifelse_end(int if_id)
{
   auto ifelse = m_if_block_start_map.find(if_id);
   if (ifelse == m_if_block_start_map.end()) {
      std::cerr << "Error: ENDIF " << if_id << " without THEN or ELSE branch\n";
      return false;
   }

   if (ifelse->second->type() != Instruction::cond_if &&
       ifelse->second->type() != Instruction::cond_else) {
      std::cerr << "Error: ENDIF " << if_id << " doesn't close an IF or ELSE branch\n";
      return false;
   }
   /* Clear pending else, if the else branch was empty, non will be emitted */

   m_pending_else = nullptr;

   append_block(-1);
   IfElseEndInstruction *ir = new IfElseEndInstruction();
   emit_instruction(ir);

   return true;
}

bool ShaderFromNirProcessor::emit_load_tcs_param_base(nir_intrinsic_instr* instr, int offset)
{
   PValue src = get_temp_register();
   emit_instruction(new AluInstruction(op1_mov, src, Value::zero, {alu_write, alu_last_instr}));

   GPRVector dest = vec_from_nir(instr->dest, nir_dest_num_components(instr->dest));
   emit_instruction(new FetchTCSIOParam(dest, src, offset));

   return true;

}

bool ShaderFromNirProcessor::emit_load_local_shared(nir_intrinsic_instr* instr)
{
   auto address = varvec_from_nir(instr->src[0], instr->num_components);
   auto dest_value = varvec_from_nir(instr->dest, instr->num_components);

   emit_instruction(new LDSReadInstruction(address, dest_value));
   return true;
}

static unsigned
lds_op_from_intrinsic(nir_intrinsic_op op) {
   switch (op) {
   case nir_intrinsic_shared_atomic_add:
      return LDS_OP2_LDS_ADD_RET;
   case nir_intrinsic_shared_atomic_and:
      return LDS_OP2_LDS_AND_RET;
   case nir_intrinsic_shared_atomic_or:
      return LDS_OP2_LDS_OR_RET;
   case nir_intrinsic_shared_atomic_imax:
      return LDS_OP2_LDS_MAX_INT_RET;
   case nir_intrinsic_shared_atomic_umax:
      return LDS_OP2_LDS_MAX_UINT_RET;
   case nir_intrinsic_shared_atomic_imin:
      return LDS_OP2_LDS_MIN_INT_RET;
   case nir_intrinsic_shared_atomic_umin:
      return LDS_OP2_LDS_MIN_UINT_RET;
   case nir_intrinsic_shared_atomic_xor:
      return LDS_OP2_LDS_XOR_RET;
   case nir_intrinsic_shared_atomic_exchange:
      return LDS_OP2_LDS_XCHG_RET;
   case nir_intrinsic_shared_atomic_comp_swap:
      return LDS_OP3_LDS_CMP_XCHG_RET;
   default:
      unreachable("Unsupported shared atomic opcode");
   }
}

bool ShaderFromNirProcessor::emit_atomic_local_shared(nir_intrinsic_instr* instr)
{
   auto address = from_nir(instr->src[0], 0);
   auto dest_value = from_nir(instr->dest, 0);
   auto value = from_nir(instr->src[1], 0);
   auto op = lds_op_from_intrinsic(instr->intrinsic);

   if (unlikely(instr->intrinsic ==nir_intrinsic_shared_atomic_comp_swap)) {
      auto value2 = from_nir(instr->src[2], 0);
      emit_instruction(new LDSAtomicInstruction(dest_value, value, value2, address, op));
   } else {
      emit_instruction(new LDSAtomicInstruction(dest_value, value, address, op));
   }
   return true;
}


bool ShaderFromNirProcessor::emit_store_local_shared(nir_intrinsic_instr* instr)
{
   unsigned write_mask = nir_intrinsic_write_mask(instr);

   auto address = from_nir(instr->src[1], 0);
   int swizzle_base = (write_mask & 0x3) ? 0 : 2;
   write_mask |= write_mask >> 2;

   auto value =  from_nir(instr->src[0], swizzle_base);
   if (!(write_mask & 2)) {
      emit_instruction(new LDSWriteInstruction(address, 0, value));
   } else {
      auto value1 = from_nir(instr->src[0], swizzle_base + 1);
      emit_instruction(new LDSWriteInstruction(address, 0, value, value1));
   }

   return true;
}

bool ShaderFromNirProcessor::emit_intrinsic_instruction(nir_intrinsic_instr* instr)
{
   r600::sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(instr)
                 << "' (" << __func__ << ")\n";

   if (emit_intrinsic_instruction_override(instr))
      return true;

   if (m_ssbo_instr.emit(&instr->instr)) {
      m_sel.info.writes_memory = true;
      return true;
   }

   switch (instr->intrinsic) {
   case nir_intrinsic_load_deref: {
      auto var = get_deref_location(instr->src[0]);
      if (!var)
         return false;
      auto mode_helper = m_var_mode.find(var);
      if (mode_helper == m_var_mode.end()) {
         cerr << "r600-nir: variable '" << var->name << "' not found\n";
         return false;
      }
      switch (mode_helper->second) {
      case nir_var_function_temp:
         return emit_load_function_temp(var, instr);
      default:
         cerr << "r600-nir: Unsupported mode" << mode_helper->second
              << "for src variable\n";
         return false;
      }
   }
   case nir_intrinsic_store_scratch:
      return emit_store_scratch(instr);
   case nir_intrinsic_load_scratch:
      return emit_load_scratch(instr);
   case nir_intrinsic_load_uniform:
      return load_uniform(instr);
   case nir_intrinsic_discard:
   case nir_intrinsic_discard_if:
      return emit_discard_if(instr);
   case nir_intrinsic_load_ubo_vec4:
      return emit_load_ubo_vec4(instr);
   case nir_intrinsic_load_tcs_in_param_base_r600:
      return emit_load_tcs_param_base(instr, 0);
   case nir_intrinsic_load_tcs_out_param_base_r600:
      return emit_load_tcs_param_base(instr, 16);
   case nir_intrinsic_load_local_shared_r600:
   case nir_intrinsic_load_shared:
      return emit_load_local_shared(instr);
   case nir_intrinsic_store_local_shared_r600:
   case nir_intrinsic_store_shared:
      return emit_store_local_shared(instr);
   case nir_intrinsic_control_barrier:
   case nir_intrinsic_memory_barrier_tcs_patch:
   case nir_intrinsic_memory_barrier_shared:
   case nir_intrinsic_memory_barrier_buffer:
   case nir_intrinsic_memory_barrier:
   case nir_intrinsic_memory_barrier_image:
   case nir_intrinsic_group_memory_barrier:
      return emit_barrier(instr);
   case nir_intrinsic_memory_barrier_atomic_counter:
      return true;
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
      return emit_atomic_local_shared(instr);
   case nir_intrinsic_shader_clock:
      return emit_shader_clock(instr);
   case nir_intrinsic_copy_deref:
   case nir_intrinsic_load_constant:
   case nir_intrinsic_load_input:
   case nir_intrinsic_store_output:

   default:
      fprintf(stderr, "r600-nir: Unsupported intrinsic %d\n", instr->intrinsic);
      return false;
   }
   return false;
}

bool ShaderFromNirProcessor::emit_intrinsic_instruction_override(UNUSED nir_intrinsic_instr* instr)
{
   return false;
}

bool
ShaderFromNirProcessor::emit_load_function_temp(UNUSED const nir_variable *var, UNUSED nir_intrinsic_instr *instr)
{
   return false;
}

bool ShaderFromNirProcessor::emit_barrier(UNUSED nir_intrinsic_instr* instr)
{
   AluInstruction *ir = new AluInstruction(op0_group_barrier);
   ir->set_flag(alu_last_instr);
   emit_instruction(ir);
   return true;
}


bool ShaderFromNirProcessor::load_preloaded_value(const nir_dest& dest, int chan, PValue value, bool as_last)
{
   if (!dest.is_ssa) {
      auto ir = new AluInstruction(op1_mov, from_nir(dest, 0), value, {alu_write});
      if (as_last)
         ir->set_flag(alu_last_instr);
      emit_instruction(ir);
   } else {
      inject_register(dest.ssa.index, chan, value, true);
   }
   return true;
}

bool ShaderFromNirProcessor::emit_store_scratch(nir_intrinsic_instr* instr)
{
   PValue address = from_nir(instr->src[1], 0, 0);

   auto value = vec_from_nir_with_fetch_constant(instr->src[0], (1 << instr->num_components) - 1,
         swizzle_from_comps(instr->num_components));

   int writemask = nir_intrinsic_write_mask(instr);
   int align = nir_intrinsic_align_mul(instr);
   int align_offset = nir_intrinsic_align_offset(instr);

   WriteScratchInstruction *ir = nullptr;
   if (address->type() == Value::literal) {
      const auto& lv = static_cast<const LiteralValue&>(*address);
      ir = new WriteScratchInstruction(lv.value(), value, align, align_offset, writemask);
   } else {
      address = from_nir_with_fetch_constant(instr->src[1], 0);
      ir = new WriteScratchInstruction(address, value, align, align_offset,
                                       writemask, m_scratch_size);
   }
   emit_instruction(ir);
   sh_info().needs_scratch_space = 1;
   return true;
}

bool ShaderFromNirProcessor::emit_load_scratch(nir_intrinsic_instr* instr)
{
   PValue address = from_nir_with_fetch_constant(instr->src[0], 0);
   std::array<PValue, 4> dst_val;
   for (int i = 0; i < 4; ++i)
      dst_val[i] = from_nir(instr->dest, i < instr->num_components ? i : 7);

   GPRVector dst(dst_val);
   auto ir = new LoadFromScratch(dst, address, m_scratch_size);
   ir->prelude_append(new WaitAck(0));
   emit_instruction(ir);
   sh_info().needs_scratch_space = 1;
   return true;
}

bool ShaderFromNirProcessor::emit_shader_clock(nir_intrinsic_instr* instr)
{
   emit_instruction(new AluInstruction(op1_mov, from_nir(instr->dest, 0),
                                       PValue(new InlineConstValue(ALU_SRC_TIME_LO, 0)), EmitInstruction::write));
   emit_instruction(new AluInstruction(op1_mov, from_nir(instr->dest, 1),
                                       PValue(new InlineConstValue(ALU_SRC_TIME_HI, 0)), EmitInstruction::last_write));
   return true;
}

GPRVector ShaderFromNirProcessor::vec_from_nir_with_fetch_constant(const nir_src& src,
                                                                   unsigned mask,
                                                                   const GPRVector::Swizzle& swizzle,
                                                                   bool match)
{
   bool use_same = true;
   GPRVector::Values v;

   std::array<bool,4> used_swizzles = {false, false, false, false};

   /* Check whether all sources come from a GPR, and,
    * if requested, whether they are swizzled as expected */

   for (int i = 0; i < 4 && use_same; ++i)  {
      if ((1 << i) & mask) {
         if (swizzle[i] < 4) {
            v[i] = from_nir(src, swizzle[i]);
            assert(v[i]);
            use_same &= (v[i]->type() == Value::gpr);
            if (match) {
               use_same &= (v[i]->chan() == swizzle[i]);
            }
            used_swizzles[v[i]->chan()] = true;
         }
      }
   }


   /* Now check whether all inputs come from the same GPR, and fill
    * empty slots in the vector with unused swizzles, bail out if
    * the sources are not from the same GPR
    */

   if (use_same) {
      int next_free_swizzle = 0;
      while (used_swizzles[next_free_swizzle] && next_free_swizzle < 4)
         next_free_swizzle++;

      /* Find the first GPR index used */
      int i = 0;
      while (!v[i] && i < 4) ++i;
      assert(i < 4);
      unsigned sel = v[i]->sel();


      for (i = 0; i < 4 && use_same; ++i) {
         if (!v[i]) {
            if (swizzle[i] >= 4)
               v[i] = PValue(new GPRValue(sel, swizzle[i]));
            else {
               assert(next_free_swizzle < 4);
               v[i] = PValue(new GPRValue(sel, next_free_swizzle));
               used_swizzles[next_free_swizzle] = true;
               while (next_free_swizzle < 4 && used_swizzles[next_free_swizzle])
                  next_free_swizzle++;
            }
         }
         else
            use_same &= v[i]->sel() == sel;
      }
   }

   /* We can't re-use the source data because they either need re-swizzling, or
    * they didn't come all from a GPR or the same GPR, so copy to a new vector
    */
   if (!use_same) {
      AluInstruction *ir = nullptr;
      GPRVector result = get_temp_vec4(swizzle);
      for (int i = 0; i < 4; ++i) {
         if (swizzle[i] < 4 && (mask & (1 << i))) {
            ir = new AluInstruction(op1_mov, result[i], from_nir(src, swizzle[i]),
                                    EmitInstruction::write);
            emit_instruction(ir);
         }
      }
      if (ir)
         ir->set_flag(alu_last_instr);
      return result;
   } else
      return GPRVector(v);;
}

bool ShaderFromNirProcessor::emit_load_ubo_vec4(nir_intrinsic_instr* instr)
{
   auto bufid = nir_src_as_const_value(instr->src[0]);
   auto buf_offset = nir_src_as_const_value(instr->src[1]);
   auto base = nir_intrinsic_base(instr);

   if (!buf_offset) {
      /* TODO: if buf_offset is constant then this can also be solved by using the CF indes
       * on the ALU block, and this would probably make sense when there are more then one
       * loads with the same buffer ID. */

      PValue addr = from_nir_with_fetch_constant(instr->src[1], 0);
      GPRVector trgt;
      std::array<int, 4> swz = {7,7,7,7};
      for (unsigned i = 0; i < 4; ++i) {
         if (i < nir_dest_num_components(instr->dest)) {
            trgt.set_reg_i(i, from_nir(instr->dest, i));
            swz[i] = i + nir_intrinsic_component(instr);
         } else {
            trgt.set_reg_i(i, from_nir(instr->dest, 7));
         }
      }

      FetchInstruction *ir;
      if (bufid) {
         ir = new FetchInstruction(vc_fetch, no_index_offset, trgt, addr, base,
                                              1 + bufid->u32, nullptr, bim_none);
      } else {
         PValue bufid = from_nir(instr->src[0], 0, 0);
         ir = new FetchInstruction(vc_fetch, no_index_offset, trgt, addr, base,
                                              1, bufid, bim_zero);
      }
      ir->set_dest_swizzle(swz);
      emit_instruction(ir);
      m_sh_info.indirect_files |= 1 << TGSI_FILE_CONSTANT;
      return true;
   }

   uint32_t offset = 512 + base + buf_offset->u32;

   if (bufid) {
      int buf_cmp = nir_intrinsic_component(instr);
      AluInstruction *ir = nullptr;
      for (unsigned i = 0; i < nir_dest_num_components(instr->dest); ++i) {
         int cmp = buf_cmp + i;
         assert(cmp < 4);
         auto u = PValue(new UniformValue(offset, cmp, bufid->u32 + 1));
         if (instr->dest.is_ssa)
            load_preloaded_value(instr->dest, i, u);
         else {
            ir = new AluInstruction(op1_mov, from_nir(instr->dest, i), u, {alu_write});
            emit_instruction(ir);
         }
      }
      if (ir)
         ir->set_flag(alu_last_instr);
      return true;

   } else {
      int buf_cmp = nir_intrinsic_component(instr);
      AluInstruction *ir = nullptr;
      auto kc_id = from_nir(instr->src[0], 0);
      for (unsigned i = 0; i < nir_dest_num_components(instr->dest); ++i) {
         int cmp = buf_cmp + i;
         auto u = PValue(new UniformValue(offset, cmp, kc_id));
         if (instr->dest.is_ssa)
            load_preloaded_value(instr->dest, i, u);
         else {
            ir = new AluInstruction(op1_mov, from_nir(instr->dest, i), u, {alu_write});
            emit_instruction(ir);
         }
      }
      if (ir)
         ir->set_flag(alu_last_instr);
      return true;
   }
}

bool ShaderFromNirProcessor::emit_discard_if(nir_intrinsic_instr* instr)
{
   r600::sfn_log << SfnLog::instr << "emit '"
                 << *reinterpret_cast<nir_instr*>(instr)
                 << "' (" << __func__ << ")\n";

   if (instr->intrinsic == nir_intrinsic_discard_if) {
      emit_instruction(new AluInstruction(op2_killne_int, PValue(new GPRValue(0,0)),
                          {from_nir(instr->src[0], 0, 0), Value::zero}, {alu_last_instr}));

   } else {
      emit_instruction(new AluInstruction(op2_kille, PValue(new GPRValue(0,0)),
                       {Value::zero, Value::zero}, {alu_last_instr}));
   }
   m_sh_info.uses_kill = 1;
   return true;
}

bool ShaderFromNirProcessor::load_uniform(nir_intrinsic_instr* instr)
{
   r600::sfn_log << SfnLog::instr << __func__ << ": emit '"
                 << *reinterpret_cast<nir_instr*>(instr)
                 << "'\n";


   /* If the target register is a SSA register and the loading is not
    * indirect then we can do lazy loading, i.e. the uniform value can
    * be used directly. Otherwise we have to load the data for real
    * rigt away.
    */
   auto literal = nir_src_as_const_value(instr->src[0]);
   int base = nir_intrinsic_base(instr);

   if (literal) {
      AluInstruction *ir = nullptr;
      for (unsigned i = 0; i < nir_dest_num_components(instr->dest); ++i) {
         PValue u = PValue(new UniformValue(512 + literal->u32 + base, i));
         sfn_log << SfnLog::io << "uniform "
                 << instr->dest.ssa.index << " const["<< i << "]: "<< instr->const_index[i] << "\n";

         if (instr->dest.is_ssa)
            load_preloaded_value(instr->dest, i, u);
         else {
            ir = new AluInstruction(op1_mov, from_nir(instr->dest, i),
                                                   u, {alu_write});
             emit_instruction(ir);
         }
      }
      if (ir)
         ir->set_flag(alu_last_instr);
   } else {
      PValue addr = from_nir(instr->src[0], 0, 0);
      return load_uniform_indirect(instr, addr, 16 * base, 0);
   }
   return true;
}

bool ShaderFromNirProcessor::load_uniform_indirect(nir_intrinsic_instr* instr, PValue addr, int offest, int bufferid)
{
   if (!addr) {
      std::cerr << "r600-nir: don't know how uniform is addressed\n";
      return false;
   }

   GPRVector trgt;
   std::array<int, 4> swz = {7,7,7,7};
   for (int i = 0; i < 4; ++i) {
      trgt.set_reg_i(i, from_nir(instr->dest, i));
      swz[i] = i;
   }

   if (addr->type() != Value::gpr) {
      emit_instruction(op1_mov, trgt.reg_i(0), {addr}, {alu_write, alu_last_instr});
      addr = trgt.reg_i(0);
   }

   auto ir = new FetchInstruction(vc_fetch, no_index_offset, trgt, addr, offest,
                                  bufferid, PValue(), bim_none);
   ir->set_dest_swizzle(swz);
   emit_instruction(ir);
   m_sh_info.indirect_files |= 1 << TGSI_FILE_CONSTANT;
   return true;
}

AluInstruction *ShaderFromNirProcessor::emit_load_literal(const nir_load_const_instr * literal, const nir_src& src, unsigned writemask)
{
   AluInstruction *ir = nullptr;
   for (int i = 0; i < literal->def.num_components ; ++i) {
      if (writemask & (1 << i)){
         PValue lsrc;
         switch (literal->def.bit_size) {

         case 1:
            sfn_log << SfnLog::reg << "Got literal of bit size 1\n";
            lsrc = literal->value[i].b ?
                     PValue(new LiteralValue( 0xffffffff, i)) :
                     Value::zero;
            break;
         case 32:
            sfn_log << SfnLog::reg << "Got literal of bit size 32\n";
            if (literal->value[i].u32 == 0)
               lsrc = Value::zero;
            else if (literal->value[i].u32 == 1)
               lsrc = Value::one_i;
            else if (literal->value[i].f32 == 1.0f)
               lsrc = Value::one_f;
            else if (literal->value[i].f32 == 0.5f)
               lsrc = Value::zero_dot_5;
            else
               lsrc = PValue(new LiteralValue(literal->value[i].u32, i));
            break;
         default:
            sfn_log << SfnLog::reg << "Got literal of bit size " << literal->def.bit_size
                    << " falling back to 32 bit\n";
            lsrc = PValue(new LiteralValue(literal->value[i].u32, i));
         }
         ir = new AluInstruction(op1_mov, create_register_from_nir_src(src, i), lsrc, EmitInstruction::write);

         emit_instruction(ir);
      }
   }
   return ir;
}

PValue ShaderFromNirProcessor::from_nir_with_fetch_constant(const nir_src& src, unsigned component, int channel)
{
   PValue value = from_nir(src, component);
   if (value->type() != Value::gpr &&
       value->type() != Value::gpr_vector &&
       value->type() != Value::gpr_array_value) {
      PValue retval = get_temp_register(channel);
      emit_instruction(new AluInstruction(op1_mov, retval, value,
                                          EmitInstruction::last_write));
      value = retval;
   }
   return value;
}

bool ShaderFromNirProcessor::emit_deref_instruction(nir_deref_instr* instr)
{
   r600::sfn_log << SfnLog::instr << __func__ << ": emit '"
                 << *reinterpret_cast<nir_instr*>(instr)
                 << "'\n";

   /* Give the specific shader type a chance to process this, i.e. Geometry and
    * tesselation shaders need specialized deref_array, for the other shaders
    * it is lowered.
    */
   if (emit_deref_instruction_override(instr))
      return true;

   switch (instr->deref_type) {
   case nir_deref_type_var:
      set_var_address(instr);
      return true;
   case nir_deref_type_array:
   case nir_deref_type_array_wildcard:
   case nir_deref_type_struct:
   case nir_deref_type_cast:
   default:
      fprintf(stderr, "R600: deref type %d not supported\n", instr->deref_type);
   }
   return false;
}

bool ShaderFromNirProcessor::emit_instruction(EAluOp opcode, PValue dest,
                                              std::vector<PValue> srcs,
                                              const std::set<AluModifiers>& m_flags)
{
   AluInstruction *ir = new AluInstruction(opcode, dest, srcs, m_flags);
   emit_instruction(ir);
   return true;
}

void ShaderFromNirProcessor::add_param_output_reg(int loc, const GPRVector *gpr)
{
   m_output_register_map[loc] = gpr;
}

void ShaderFromNirProcessor::emit_export_instruction(WriteoutInstruction *ir)
{
   r600::sfn_log << SfnLog::instr << "     as '" << *ir << "'\n";
   m_export_output.emit(PInstruction(ir));
}

const GPRVector * ShaderFromNirProcessor::output_register(unsigned location) const
{
   const GPRVector *retval = nullptr;
   auto val = m_output_register_map.find(location);
   if (val != m_output_register_map.end())
      retval =  val->second;
   return retval;
}

void ShaderFromNirProcessor::set_input(unsigned pos, PValue var)
{
   r600::sfn_log << SfnLog::io << "Set input[" << pos << "] =" << *var <<  "\n";
   m_inputs[pos] = var;
}

void ShaderFromNirProcessor::set_output(unsigned pos, int sel)
{
   r600::sfn_log << SfnLog::io << "Set output[" << pos << "] =" << sel <<  "\n";
   m_outputs[pos] = sel;
}

void ShaderFromNirProcessor::append_block(int nesting_change)
{
   m_nesting_depth += nesting_change;
   m_output.push_back(InstructionBlock(m_nesting_depth, m_block_number++));
}

void ShaderFromNirProcessor::get_array_info(r600_shader& shader) const
{
   shader.num_arrays = m_reg_arrays.size();
   if (shader.num_arrays) {
      shader.arrays = (r600_shader_array *)calloc(shader.num_arrays, sizeof(r600_shader_array));
      for (unsigned i = 0; i < shader.num_arrays; ++i) {
         shader.arrays[i].comp_mask = m_reg_arrays[i]->mask();
         shader.arrays[i].gpr_start = m_reg_arrays[i]->sel();
         shader.arrays[i].gpr_count = m_reg_arrays[i]->size();
      }
      shader.indirect_files |= (1 << TGSI_FILE_TEMPORARY);
   }
}

void ShaderFromNirProcessor::finalize()
{
   do_finalize();

   for (auto& i : m_inputs)
      m_sh_info.input[i.first].gpr = i.second->sel();

   for (auto& i : m_outputs)
      m_sh_info.output[i.first].gpr = i.second;

   m_output.push_back(m_export_output);
}

}
