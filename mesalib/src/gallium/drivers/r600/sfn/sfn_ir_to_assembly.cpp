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

#include "sfn_ir_to_assembly.h"
#include "sfn_conditionaljumptracker.h"
#include "sfn_callstack.h"
#include "sfn_instruction_gds.h"
#include "sfn_instruction_misc.h"
#include "sfn_instruction_fetch.h"
#include "sfn_instruction_lds.h"

#include "../r600_shader.h"
#include "../r600_sq.h"

namespace r600 {

using std::vector;

struct AssemblyFromShaderLegacyImpl {

   AssemblyFromShaderLegacyImpl(r600_shader *sh, r600_shader_key *key);
   bool emit(const Instruction::Pointer i);
   void reset_addr_register() {m_last_addr.reset();}

private:
   bool emit_alu(const AluInstruction& ai, ECFAluOpCode cf_op);
   bool emit_export(const ExportInstruction & exi);
   bool emit_streamout(const StreamOutIntruction& instr);
   bool emit_memringwrite(const MemRingOutIntruction& instr);
   bool emit_tex(const TexInstruction & tex_instr);
   bool emit_vtx(const FetchInstruction& fetch_instr);
   bool emit_if_start(const IfInstruction & if_instr);
   bool emit_else(const ElseInstruction & else_instr);
   bool emit_endif(const IfElseEndInstruction & endif_instr);
   bool emit_emit_vertex(const EmitVertex &instr);

   bool emit_loop_begin(const LoopBeginInstruction& instr);
   bool emit_loop_end(const LoopEndInstruction& instr);
   bool emit_loop_break(const LoopBreakInstruction& instr);
   bool emit_loop_continue(const LoopContInstruction& instr);
   bool emit_wait_ack(const WaitAck& instr);
   bool emit_wr_scratch(const WriteScratchInstruction& instr);
   bool emit_gds(const GDSInstr& instr);
   bool emit_rat(const RatInstruction& instr);
   bool emit_ldswrite(const LDSWriteInstruction& instr);
   bool emit_ldsread(const LDSReadInstruction& instr);
   bool emit_ldsatomic(const LDSAtomicInstruction& instr);
   bool emit_tf_write(const GDSStoreTessFactor& instr);

   bool emit_load_addr(PValue addr);
   bool emit_fs_pixel_export(const ExportInstruction & exi);
   bool emit_vs_pos_export(const ExportInstruction & exi);
   bool emit_vs_param_export(const ExportInstruction & exi);
   bool copy_dst(r600_bytecode_alu_dst& dst, const Value& src);
   bool copy_src(r600_bytecode_alu_src& src, const Value& s);

   EBufferIndexMode emit_index_reg(const Value& reg, unsigned idx);

   ConditionalJumpTracker m_jump_tracker;
   CallStack m_callstack;

public:
   r600_bytecode *m_bc;
   r600_shader *m_shader;
   r600_shader_key *m_key;
   r600_bytecode_output m_output;
   unsigned m_max_color_exports;
   bool has_pos_output;
   bool has_param_output;
   PValue m_last_addr;
   int m_loop_nesting;
   int m_nliterals_in_group;
   std::set<int> vtx_fetch_results;
};


AssemblyFromShaderLegacy::AssemblyFromShaderLegacy(struct r600_shader *sh,
                                                   r600_shader_key *key)
{
   impl = new AssemblyFromShaderLegacyImpl(sh, key);
}

AssemblyFromShaderLegacy::~AssemblyFromShaderLegacy()
{
   delete impl;
}

bool AssemblyFromShaderLegacy::do_lower(const std::vector<InstructionBlock>& ir)
{
   if (impl->m_shader->processor_type == PIPE_SHADER_VERTEX &&
       impl->m_shader->ninput > 0)
         r600_bytecode_add_cfinst(impl->m_bc, CF_OP_CALL_FS);


   std::vector<Instruction::Pointer> exports;

   for (const auto& block : ir) {
      for (const auto& i : block) {
         if (!impl->emit(i))
         return false;
      if (i->type() != Instruction::alu)
         impl->reset_addr_register();
      }
   }
   /*
   for (const auto& i : exports) {
      if (!impl->emit_export(static_cast<const ExportInstruction&>(*i)))
          return false;
   }*/


   const struct cf_op_info *last = nullptr;
   if (impl->m_bc->cf_last)
      last = r600_isa_cf(impl->m_bc->cf_last->op);

   /* alu clause instructions don't have EOP bit, so add NOP */
   if (!last || last->flags & CF_ALU || impl->m_bc->cf_last->op == CF_OP_LOOP_END
       || impl->m_bc->cf_last->op == CF_OP_POP)
      r600_bytecode_add_cfinst(impl->m_bc, CF_OP_NOP);

    /* A fetch shader only can't be EOP (results in hang), but we can replace it
     * by a NOP */
   else if (impl->m_bc->cf_last->op == CF_OP_CALL_FS)
      impl->m_bc->cf_last->op = CF_OP_NOP;

   if (impl->m_shader->bc.chip_class != CAYMAN)
      impl->m_bc->cf_last->end_of_program = 1;
   else
      cm_bytecode_add_cf_end(impl->m_bc);

   return true;
}

bool AssemblyFromShaderLegacyImpl::emit(const Instruction::Pointer i)
{
   if (i->type() != Instruction::vtx)
       vtx_fetch_results.clear();

   sfn_log << SfnLog::assembly << "Emit from '" << *i << "\n";
   switch (i->type()) {
   case Instruction::alu:
      return emit_alu(static_cast<const AluInstruction&>(*i), cf_alu_undefined);
   case Instruction::exprt:
      return emit_export(static_cast<const ExportInstruction&>(*i));
   case Instruction::tex:
      return emit_tex(static_cast<const TexInstruction&>(*i));
   case Instruction::vtx:
      return emit_vtx(static_cast<const FetchInstruction&>(*i));
   case Instruction::cond_if:
      return emit_if_start(static_cast<const IfInstruction&>(*i));
   case Instruction::cond_else:
      return emit_else(static_cast<const ElseInstruction&>(*i));
   case Instruction::cond_endif:
      return emit_endif(static_cast<const IfElseEndInstruction&>(*i));
   case Instruction::loop_begin:
      return emit_loop_begin(static_cast<const LoopBeginInstruction&>(*i));
   case Instruction::loop_end:
      return emit_loop_end(static_cast<const LoopEndInstruction&>(*i));
   case Instruction::loop_break:
      return emit_loop_break(static_cast<const LoopBreakInstruction&>(*i));
   case Instruction::loop_continue:
      return emit_loop_continue(static_cast<const LoopContInstruction&>(*i));
   case Instruction::streamout:
      return emit_streamout(static_cast<const StreamOutIntruction&>(*i));
   case Instruction::ring:
      return emit_memringwrite(static_cast<const MemRingOutIntruction&>(*i));
   case Instruction::emit_vtx:
      return emit_emit_vertex(static_cast<const EmitVertex&>(*i));
   case Instruction::wait_ack:
      return emit_wait_ack(static_cast<const WaitAck&>(*i));
   case Instruction::mem_wr_scratch:
      return emit_wr_scratch(static_cast<const WriteScratchInstruction&>(*i));
   case Instruction::gds:
      return emit_gds(static_cast<const GDSInstr&>(*i));
   case Instruction::rat:
      return emit_rat(static_cast<const RatInstruction&>(*i));
   case Instruction::lds_write:
      return emit_ldswrite(static_cast<const LDSWriteInstruction&>(*i));
   case Instruction::lds_read:
      return emit_ldsread(static_cast<const LDSReadInstruction&>(*i));
   case Instruction::lds_atomic:
      return emit_ldsatomic(static_cast<const LDSAtomicInstruction&>(*i));
   case Instruction::tf_write:
      return emit_tf_write(static_cast<const GDSStoreTessFactor&>(*i));
   default:
      return false;
   }
}

AssemblyFromShaderLegacyImpl::AssemblyFromShaderLegacyImpl(r600_shader *sh,
                                                           r600_shader_key *key):
   m_callstack(sh->bc),
   m_bc(&sh->bc),
   m_shader(sh),
   m_key(key),
   has_pos_output(false),
   has_param_output(false),
   m_loop_nesting(0),
   m_nliterals_in_group(0)
{
   m_max_color_exports = MAX2(m_key->ps.nr_cbufs, 1);
}

extern const std::map<EAluOp, int> opcode_map;

bool AssemblyFromShaderLegacyImpl::emit_load_addr(PValue addr)
{
   m_bc->ar_reg = addr->sel();
   m_bc->ar_chan = addr->chan();
   m_bc->ar_loaded = 0;
   m_last_addr = addr;

   sfn_log << SfnLog::assembly << "   Prepare " << *addr << " to address register\n";

   return true;
}

bool AssemblyFromShaderLegacyImpl::emit_alu(const AluInstruction& ai, ECFAluOpCode cf_op)
{

   struct r600_bytecode_alu alu;
   memset(&alu, 0, sizeof(alu));
   PValue addr_in_use;

   if (opcode_map.find(ai.opcode()) == opcode_map.end()) {
      std::cerr << "Opcode not handled for " << ai <<"\n";
      return false;
   }

   unsigned old_nliterals_in_group = m_nliterals_in_group;
   for (unsigned i = 0; i < ai.n_sources(); ++i) {
      auto& s = ai.src(i);
      if (s.type() == Value::literal)
         ++m_nliterals_in_group;
   }

   /* This instruction group would exeed the limit of literals, so
    * force a new instruction group by adding a NOP as last
    * instruction. This will no loner be needed with a real
    * scheduler */
   if (m_nliterals_in_group > 4) {
      sfn_log << SfnLog::assembly << "  Have " << m_nliterals_in_group << " inject a last op (nop)\n";
      alu.op = ALU_OP0_NOP;
      alu.last = 1;
      alu.dst.chan = 3;
      int retval = r600_bytecode_add_alu(m_bc, &alu);
      if (retval)
         return false;
      memset(&alu, 0, sizeof(alu));
      m_nliterals_in_group -= old_nliterals_in_group;
   }

   alu.op = opcode_map.at(ai.opcode());

   /* Missing test whether ai actually has a dest */
   auto dst = ai.dest();

   if (dst) {
      if (!copy_dst(alu.dst, *dst))
         return false;

      alu.dst.write = ai.flag(alu_write);
      alu.dst.clamp = ai.flag(alu_dst_clamp);

      if (dst->type() == Value::gpr_array_value) {
         auto& v = static_cast<const GPRArrayValue&>(*dst);
         PValue addr = v.indirect();
         if (addr) {
            if (!m_last_addr || *addr != *m_last_addr) {
               emit_load_addr(addr);
               addr_in_use = addr;
            }
            alu.dst.rel = addr ? 1 : 0;;
         }
      }
   }

   alu.is_op3 = ai.n_sources() == 3;

   for (unsigned i = 0; i < ai.n_sources(); ++i) {
      auto& s = ai.src(i);

      if (!copy_src(alu.src[i], s))
         return false;
      alu.src[i].neg = ai.flag(AluInstruction::src_neg_flags[i]);

      if (s.type() == Value::gpr_array_value) {
         auto& v = static_cast<const GPRArrayValue&>(s);
         PValue addr = v.indirect();
         if (addr) {
            assert(!addr_in_use || (*addr_in_use == *addr));
            if (!m_last_addr || *addr != *m_last_addr) {
               emit_load_addr(addr);
               addr_in_use = addr;
            }
            alu.src[i].rel = addr ? 1 : 0;
         }
      }
      if (!alu.is_op3)
         alu.src[i].abs = ai.flag(AluInstruction::src_abs_flags[i]);
   }

   if (ai.bank_swizzle() != alu_vec_unknown)
      alu.bank_swizzle_force = ai.bank_swizzle();

   alu.last = ai.flag(alu_last_instr);
   alu.update_pred = ai.flag(alu_update_pred);
   alu.execute_mask = ai.flag(alu_update_exec);

   /* If the destination register is equal to the last loaded address register
    * then clear the latter one, because the values will no longer be identical */
   if (m_last_addr)
      sfn_log << SfnLog::assembly << "  Current address register is " << *m_last_addr << "\n";

   if (dst)
      sfn_log << SfnLog::assembly << "  Current dst register is " << *dst << "\n";

   if (dst && m_last_addr)
      if (*dst == *m_last_addr) {
         sfn_log << SfnLog::assembly << "  Clear address register (was " << *m_last_addr << "\n";
         m_last_addr.reset();
      }

   if (cf_op == cf_alu_undefined)
      cf_op = ai.cf_type();

   unsigned type = 0;
   switch (cf_op) {
   case cf_alu: type = CF_OP_ALU; break;
   case cf_alu_push_before: type = CF_OP_ALU_PUSH_BEFORE; break;
   case cf_alu_pop_after: type = CF_OP_ALU_POP_AFTER; break;
   case cf_alu_pop2_after: type = CF_OP_ALU_POP2_AFTER; break;
   case cf_alu_break: type = CF_OP_ALU_BREAK; break;
   case cf_alu_else_after: type = CF_OP_ALU_ELSE_AFTER; break;
   case cf_alu_continue: type = CF_OP_ALU_CONTINUE; break;
   case cf_alu_extended: type = CF_OP_ALU_EXT; break;
   default:
      assert(0 && "cf_alu_undefined should have been replaced");
   }

   if (alu.last)
      m_nliterals_in_group = 0;

   bool retval = !r600_bytecode_add_alu_type(m_bc, &alu, type);

   if (ai.opcode() == op1_mova_int)
      m_bc->ar_loaded = 0;

   if (ai.opcode() == op1_set_cf_idx0)
      m_bc->index_loaded[0] = 1;

   if (ai.opcode() == op1_set_cf_idx1)
      m_bc->index_loaded[1] = 1;


   m_bc->force_add_cf |= (ai.opcode() == op2_kille ||
                          ai.opcode() == op2_killne_int ||
                          ai.opcode() == op1_set_cf_idx0 ||
                          ai.opcode() == op1_set_cf_idx1);
   return retval;
}

bool AssemblyFromShaderLegacyImpl::emit_vs_pos_export(const ExportInstruction & exi)
{
   r600_bytecode_output output;
   memset(&output, 0, sizeof(output));
   assert(exi.gpr().type() == Value::gpr_vector);
   const auto& gpr = exi.gpr();
   output.gpr = gpr.sel();
   output.elem_size = 3;
   output.swizzle_x = gpr.chan_i(0);
   output.swizzle_y = gpr.chan_i(1);
   output.swizzle_z = gpr.chan_i(2);
   output.swizzle_w = gpr.chan_i(3);
   output.burst_count = 1;
   output.array_base = 60 + exi.location();
   output.op = exi.is_last_export() ? CF_OP_EXPORT_DONE: CF_OP_EXPORT;
   output.type = exi.export_type();


   if (r600_bytecode_add_output(m_bc, &output)) {
      R600_ERR("Error adding pixel export at location %d\n", exi.location());
      return false;
   }

   return true;
}


bool AssemblyFromShaderLegacyImpl::emit_vs_param_export(const ExportInstruction & exi)
{
   r600_bytecode_output output;
   assert(exi.gpr().type() == Value::gpr_vector);
   const auto& gpr = exi.gpr();

   memset(&output, 0, sizeof(output));
   output.gpr = gpr.sel();
   output.elem_size = 3;
   output.swizzle_x = gpr.chan_i(0);
   output.swizzle_y = gpr.chan_i(1);
   output.swizzle_z = gpr.chan_i(2);
   output.swizzle_w = gpr.chan_i(3);
   output.burst_count = 1;
   output.array_base = exi.location();
   output.op = exi.is_last_export() ? CF_OP_EXPORT_DONE: CF_OP_EXPORT;
   output.type = exi.export_type();


   if (r600_bytecode_add_output(m_bc, &output)) {
      R600_ERR("Error adding pixel export at location %d\n", exi.location());
      return false;
   }

   return true;
}


bool AssemblyFromShaderLegacyImpl::emit_fs_pixel_export(const ExportInstruction & exi)
{
   if (exi.location() >= m_max_color_exports && exi.location()  < 60) {
      R600_ERR("shader_from_nir: ignore pixel export %u, because supported max is %u\n",
               exi.location(), m_max_color_exports);
      return true;
   }

   assert(exi.gpr().type() == Value::gpr_vector);
   const auto& gpr = exi.gpr();

   r600_bytecode_output output;
   memset(&output, 0, sizeof(output));

   output.gpr = gpr.sel();
   output.elem_size = 3;
   output.swizzle_x = gpr.chan_i(0);
   output.swizzle_y = gpr.chan_i(1);
   output.swizzle_z = gpr.chan_i(2);
   output.swizzle_w = m_key->ps.alpha_to_one ? 5 : gpr.chan_i(3); ;
   output.burst_count = 1;
   output.array_base = exi.location();
   output.op = exi.is_last_export() ? CF_OP_EXPORT_DONE: CF_OP_EXPORT;
   output.type = exi.export_type();


   if (r600_bytecode_add_output(m_bc, &output)) {
      R600_ERR("Error adding pixel export at location %d\n", exi.location());
      return false;
   }

   return true;
}


bool AssemblyFromShaderLegacyImpl::emit_export(const ExportInstruction & exi)
{
   switch (exi.export_type()) {
   case ExportInstruction::et_pixel:
      return emit_fs_pixel_export(exi);
   case ExportInstruction::et_pos:
      return emit_vs_pos_export(exi);
   case ExportInstruction::et_param:
      return emit_vs_param_export(exi);
   default:
      R600_ERR("shader_from_nir: export %d type not yet supported\n", exi.export_type());
      return false;
   }
}

bool AssemblyFromShaderLegacyImpl::emit_if_start(const IfInstruction & if_instr)
{
	bool needs_workaround = false;
   int elems = m_callstack.push(FC_PUSH_VPM);

   if (m_bc->chip_class == CAYMAN && m_bc->stack.loop > 1)
      needs_workaround = true;
   if (m_bc->family != CHIP_HEMLOCK &&
       m_bc->family != CHIP_CYPRESS &&
       m_bc->family != CHIP_JUNIPER) {
      unsigned dmod1 = (elems - 1) % m_bc->stack.entry_size;
		unsigned dmod2 = (elems) % m_bc->stack.entry_size;

      if (elems && (!dmod1 || !dmod2))
			needs_workaround = true;
	}

   auto& pred = if_instr.pred();
   auto op = cf_alu_push_before;

   if (needs_workaround) {
		r600_bytecode_add_cfinst(m_bc, CF_OP_PUSH);
                m_bc->cf_last->cf_addr = m_bc->cf_last->id + 2;
		op = cf_alu;
	}
   emit_alu(pred, op);

   r600_bytecode_add_cfinst(m_bc, CF_OP_JUMP);

   m_jump_tracker.push(m_bc->cf_last, jt_if);
   return true;
}

bool AssemblyFromShaderLegacyImpl::emit_else(UNUSED const ElseInstruction & else_instr)
{
   r600_bytecode_add_cfinst(m_bc, CF_OP_ELSE);
   m_bc->cf_last->pop_count = 1;
   return m_jump_tracker.add_mid(m_bc->cf_last, jt_if);
}

bool AssemblyFromShaderLegacyImpl::emit_endif(UNUSED const IfElseEndInstruction & endif_instr)
{
   m_callstack.pop(FC_PUSH_VPM);

   unsigned force_pop = m_bc->force_add_cf;
   if (!force_pop) {
      int alu_pop = 3;
      if (m_bc->cf_last) {
         if (m_bc->cf_last->op == CF_OP_ALU)
            alu_pop = 0;
         else if (m_bc->cf_last->op == CF_OP_ALU_POP_AFTER)
            alu_pop = 1;
      }
      alu_pop += 1;
      if (alu_pop == 1) {
         m_bc->cf_last->op = CF_OP_ALU_POP_AFTER;
         m_bc->force_add_cf = 1;
      } else if (alu_pop == 2) {
         m_bc->cf_last->op = CF_OP_ALU_POP2_AFTER;
         m_bc->force_add_cf = 1;
      } else {
         force_pop = 1;
      }
   }

   if (force_pop) {
      r600_bytecode_add_cfinst(m_bc, CF_OP_POP);
      m_bc->cf_last->pop_count = 1;
      m_bc->cf_last->cf_addr = m_bc->cf_last->id + 2;
   }

   return m_jump_tracker.pop(m_bc->cf_last, jt_if);
}

bool AssemblyFromShaderLegacyImpl::emit_loop_begin(UNUSED const LoopBeginInstruction& instr)
{
   r600_bytecode_add_cfinst(m_bc, CF_OP_LOOP_START_DX10);
   m_jump_tracker.push(m_bc->cf_last, jt_loop);
   m_callstack.push(FC_LOOP);
   ++m_loop_nesting;
   return true;
}

bool AssemblyFromShaderLegacyImpl::emit_loop_end(UNUSED const LoopEndInstruction& instr)
{
   r600_bytecode_add_cfinst(m_bc, CF_OP_LOOP_END);
   m_callstack.pop(FC_LOOP);
   assert(m_loop_nesting);
   --m_loop_nesting;
   return m_jump_tracker.pop(m_bc->cf_last, jt_loop);
}

bool AssemblyFromShaderLegacyImpl::emit_loop_break(UNUSED const LoopBreakInstruction& instr)
{
   r600_bytecode_add_cfinst(m_bc, CF_OP_LOOP_BREAK);
   return m_jump_tracker.add_mid(m_bc->cf_last, jt_loop);
}

bool AssemblyFromShaderLegacyImpl::emit_loop_continue(UNUSED const LoopContInstruction &instr)
{
   r600_bytecode_add_cfinst(m_bc, CF_OP_LOOP_CONTINUE);
   return m_jump_tracker.add_mid(m_bc->cf_last, jt_loop);
}

bool AssemblyFromShaderLegacyImpl::emit_streamout(const StreamOutIntruction& so_instr)
{
   struct r600_bytecode_output output;
   memset(&output, 0, sizeof(struct r600_bytecode_output));

   output.gpr = so_instr.gpr().sel();
   output.elem_size = so_instr.element_size();
   output.array_base = so_instr.array_base();
   output.type = V_SQ_CF_ALLOC_EXPORT_WORD0_SQ_EXPORT_WRITE;
   output.burst_count = so_instr.burst_count();
   output.array_size = so_instr.array_size();
   output.comp_mask = so_instr.comp_mask();
   output.op = so_instr.op();

   assert(output.op >= CF_OP_MEM_STREAM0_BUF0 && output.op <= CF_OP_MEM_STREAM3_BUF3);


   if (r600_bytecode_add_output(m_bc, &output))  {
      R600_ERR("shader_from_nir: Error creating stream output instruction\n");
      return false;
   }
   return true;
}


bool AssemblyFromShaderLegacyImpl::emit_memringwrite(const MemRingOutIntruction& instr)
{
   struct r600_bytecode_output output;
   memset(&output, 0, sizeof(struct r600_bytecode_output));

   output.gpr = instr.gpr().sel();
   output.type = instr.type();
   output.elem_size = 3;
   output.comp_mask = 0xf;
   output.burst_count = 1;
   output.op = instr.op();
   if (instr.type() == mem_write_ind || instr.type() == mem_write_ind_ack) {
      output.index_gpr = instr.index_reg();
      output.array_size = 0xfff;
   }
   output.array_base = instr.array_base();

   if (r600_bytecode_add_output(m_bc, &output)) {
      R600_ERR("shader_from_nir: Error creating mem ring write instruction\n");
      return false;
   }
   return true;
}


bool AssemblyFromShaderLegacyImpl::emit_tex(const TexInstruction & tex_instr)
{
   auto addr = tex_instr.sampler_offset();
   if (addr && (!m_bc->index_loaded[1] || m_loop_nesting
                ||  m_bc->index_reg[1] != addr->sel()
                ||  m_bc->index_reg_chan[1] != addr->chan())) {
      struct r600_bytecode_alu alu;
      memset(&alu, 0, sizeof(alu));
      alu.op = opcode_map.at(op1_mova_int);
      alu.dst.chan = 0;
      alu.src[0].sel = addr->sel();
      alu.src[0].chan = addr->chan();
      alu.last = 1;
      int r = r600_bytecode_add_alu(m_bc, &alu);
      if (r)
         return false;

      m_bc->ar_loaded = 0;

      alu.op = opcode_map.at(op1_set_cf_idx1);
      alu.dst.chan = 0;
      alu.src[0].sel = 0;
      alu.src[0].chan = 0;
      alu.last = 1;

      r = r600_bytecode_add_alu(m_bc, &alu);
      if (r)
         return false;

      m_bc->index_reg[1] = addr->sel();
      m_bc->index_reg_chan[1] = addr->chan();
      m_bc->index_loaded[1] = true;
   }

   r600_bytecode_tex tex;
   memset(&tex, 0, sizeof(struct r600_bytecode_tex));
   tex.op = tex_instr.opcode();
   tex.sampler_id = tex_instr.sampler_id();
   tex.sampler_index_mode = 0;
   tex.resource_id = tex_instr.resource_id();;
   tex.resource_index_mode = 0;
   tex.src_gpr = tex_instr.src().sel();
   tex.dst_gpr = tex_instr.dst().sel();
   tex.dst_sel_x = tex_instr.dest_swizzle(0);
   tex.dst_sel_y = tex_instr.dest_swizzle(1);
   tex.dst_sel_z = tex_instr.dest_swizzle(2);
   tex.dst_sel_w = tex_instr.dest_swizzle(3);
   tex.src_sel_x = tex_instr.src().chan_i(0);
   tex.src_sel_y = tex_instr.src().chan_i(1);
   tex.src_sel_z = tex_instr.src().chan_i(2);
   tex.src_sel_w = tex_instr.src().chan_i(3);
   tex.coord_type_x = !tex_instr.has_flag(TexInstruction::x_unnormalized);
   tex.coord_type_y = !tex_instr.has_flag(TexInstruction::y_unnormalized);
   tex.coord_type_z = !tex_instr.has_flag(TexInstruction::z_unnormalized);
   tex.coord_type_w = !tex_instr.has_flag(TexInstruction::w_unnormalized);
   tex.offset_x = tex_instr.get_offset(0);
   tex.offset_y = tex_instr.get_offset(1);
   tex.offset_z = tex_instr.get_offset(2);
   tex.resource_index_mode = (!!addr) ? 2 : 0;
   tex.sampler_index_mode = tex.resource_index_mode;

   if (tex_instr.opcode() == TexInstruction::get_gradient_h ||
       tex_instr.opcode() == TexInstruction::get_gradient_v)
      tex.inst_mod = tex_instr.has_flag(TexInstruction::grad_fine) ? 1 : 0;
   else
      tex.inst_mod = tex_instr.inst_mode();
   if (r600_bytecode_add_tex(m_bc, &tex)) {
      R600_ERR("shader_from_nir: Error creating tex assembly instruction\n");
      return false;
   }
   return true;
}

bool AssemblyFromShaderLegacyImpl::emit_vtx(const FetchInstruction& fetch_instr)
{
   int buffer_offset = 0;
   auto addr = fetch_instr.buffer_offset();
   auto index_mode = fetch_instr.buffer_index_mode();

   if (addr) {
      if (addr->type() == Value::literal) {
         const auto& boffs = static_cast<const LiteralValue&>(*addr);
         buffer_offset = boffs.value();
      } else {
         index_mode = emit_index_reg(*addr, 0);
      }
   }

   if (fetch_instr.has_prelude()) {
      for(auto &i : fetch_instr.prelude()) {
         if (!emit(i))
            return false;
      }
   }

   if (vtx_fetch_results.find(fetch_instr.src().sel()) !=
       vtx_fetch_results.end()) {
      m_bc->force_add_cf = 1;
      vtx_fetch_results.clear();
   }
   vtx_fetch_results.insert(fetch_instr.dst().sel());

   struct r600_bytecode_vtx vtx;
   memset(&vtx, 0, sizeof(vtx));
   vtx.op = fetch_instr.vc_opcode();
   vtx.buffer_id = fetch_instr.buffer_id() + buffer_offset;
   vtx.fetch_type = fetch_instr.fetch_type();
   vtx.src_gpr = fetch_instr.src().sel();
   vtx.src_sel_x = fetch_instr.src().chan();
   vtx.mega_fetch_count = fetch_instr.mega_fetch_count();
   vtx.dst_gpr = fetch_instr.dst().sel();
   vtx.dst_sel_x = fetch_instr.swz(0);		/* SEL_X */
   vtx.dst_sel_y = fetch_instr.swz(1);		/* SEL_Y */
   vtx.dst_sel_z = fetch_instr.swz(2);		/* SEL_Z */
   vtx.dst_sel_w = fetch_instr.swz(3);		/* SEL_W */
   vtx.use_const_fields = fetch_instr.use_const_fields();
   vtx.data_format = fetch_instr.data_format();
   vtx.num_format_all = fetch_instr.num_format();		/* NUM_FORMAT_SCALED */
   vtx.format_comp_all = fetch_instr.is_signed();	/* FORMAT_COMP_SIGNED */
   vtx.endian = fetch_instr.endian_swap();
   vtx.buffer_index_mode = index_mode;
   vtx.offset = fetch_instr.offset();
   vtx.indexed = fetch_instr.indexed();
   vtx.uncached = fetch_instr.uncached();
   vtx.elem_size = fetch_instr.elm_size();
   vtx.array_base = fetch_instr.array_base();
   vtx.array_size = fetch_instr.array_size();
   vtx.srf_mode_all = fetch_instr.srf_mode_no_zero();

   if (fetch_instr.use_tc()) {
      if ((r600_bytecode_add_vtx_tc(m_bc, &vtx))) {
         R600_ERR("shader_from_nir: Error creating tex assembly instruction\n");
         return false;
      }

   } else {
      if ((r600_bytecode_add_vtx(m_bc, &vtx))) {
         R600_ERR("shader_from_nir: Error creating tex assembly instruction\n");
         return false;
      }
   }

   m_bc->cf_last->vpm = fetch_instr.use_vpm();
   m_bc->cf_last->barrier = 1;

   return true;
}

bool AssemblyFromShaderLegacyImpl::emit_emit_vertex(const EmitVertex &instr)
{
   int r = r600_bytecode_add_cfinst(m_bc, instr.op());
   if (!r)
      m_bc->cf_last->count = instr.stream();
   assert(m_bc->cf_last->count < 4);

   return r == 0;
}

bool AssemblyFromShaderLegacyImpl::emit_wait_ack(const WaitAck& instr)
{
   int r = r600_bytecode_add_cfinst(m_bc, instr.op());
   if (!r)
      m_bc->cf_last->cf_addr = instr.n_ack();

   return r == 0;
}

bool AssemblyFromShaderLegacyImpl::emit_wr_scratch(const WriteScratchInstruction& instr)
{
   struct r600_bytecode_output cf;

   memset(&cf, 0, sizeof(struct r600_bytecode_output));

   cf.op = CF_OP_MEM_SCRATCH;
   cf.elem_size = 3;
   cf.gpr = instr.gpr().sel();
   cf.mark = 1;
   cf.comp_mask = instr.write_mask();
   cf.swizzle_x = 0;
   cf.swizzle_y = 1;
   cf.swizzle_z = 2;
   cf.swizzle_w = 3;
   cf.burst_count = 1;

   if (instr.indirect()) {
      cf.type = 3;
      cf.index_gpr = instr.address();

      /* The docu seems to be wrong here: In indirect addressing the
       * address_base seems to be the array_size */
      cf.array_size = instr.array_size();
   } else {
      cf.type = 2;
      cf.array_base = instr.location();
   }
   /* This should be 0, but the address calculation is apparently wrong */


   if (r600_bytecode_add_output(m_bc, &cf)){
      R600_ERR("shader_from_nir: Error creating SCRATCH_WR assembly instruction\n");
      return false;
   }

   return true;
}

extern const std::map<ESDOp, int> ds_opcode_map;

bool AssemblyFromShaderLegacyImpl::emit_gds(const GDSInstr& instr)
{
   struct r600_bytecode_gds gds;

   int uav_idx = -1;
   auto addr = instr.uav_id();
   if (addr->type() != Value::literal) {
      emit_index_reg(*addr, 1);
   } else {
      const LiteralValue& addr_reg = static_cast<const LiteralValue&>(*addr);
      uav_idx = addr_reg.value();
   }

   memset(&gds, 0, sizeof(struct r600_bytecode_gds));

   gds.op = ds_opcode_map.at(instr.op());
   gds.dst_gpr = instr.dest_sel();
   gds.uav_id = (uav_idx >= 0 ? uav_idx : 0) + instr.uav_base();
   gds.uav_index_mode = uav_idx >= 0 ? bim_none : bim_one;
   gds.src_gpr = instr.src_sel();

   gds.src_sel_x = instr.src_swizzle(0);
   gds.src_sel_y = instr.src_swizzle(1);
   gds.src_sel_z = instr.src_swizzle(2);

   gds.dst_sel_x = instr.dest_swizzle(0);
   gds.dst_sel_y = 7;
   gds.dst_sel_z = 7;
   gds.dst_sel_w = 7;
   gds.src_gpr2 = 0;
   gds.alloc_consume = 1; // Not Cayman

   int r = r600_bytecode_add_gds(m_bc, &gds);
   if (r)
      return false;
   m_bc->cf_last->vpm = 1;
   m_bc->cf_last->barrier = 1;
   return true;
}

bool AssemblyFromShaderLegacyImpl::emit_tf_write(const GDSStoreTessFactor& instr)
{
   struct r600_bytecode_gds gds;

   memset(&gds, 0, sizeof(struct r600_bytecode_gds));
   gds.src_gpr = instr.sel();
   gds.src_sel_x = instr.chan(0);
   gds.src_sel_y = instr.chan(1);
   gds.src_sel_z = 4;
   gds.dst_sel_x = 7;
   gds.dst_sel_y = 7;
   gds.dst_sel_z = 7;
   gds.dst_sel_w = 7;
   gds.op = FETCH_OP_TF_WRITE;

   if (r600_bytecode_add_gds(m_bc, &gds) != 0)
         return false;

   if (instr.chan(2) != 7) {
      memset(&gds, 0, sizeof(struct r600_bytecode_gds));
      gds.src_gpr = instr.sel();
      gds.src_sel_x = instr.chan(2);
      gds.src_sel_y = instr.chan(3);
      gds.src_sel_z = 4;
      gds.dst_sel_x = 7;
      gds.dst_sel_y = 7;
      gds.dst_sel_z = 7;
      gds.dst_sel_w = 7;
      gds.op = FETCH_OP_TF_WRITE;

      if (r600_bytecode_add_gds(m_bc, &gds))
         return false;
   }
   return true;
}

bool AssemblyFromShaderLegacyImpl::emit_ldswrite(const LDSWriteInstruction& instr)
{
   r600_bytecode_alu alu;
   memset(&alu, 0, sizeof(r600_bytecode_alu));

   alu.last = true;
   alu.is_lds_idx_op = true;
   copy_src(alu.src[0], instr.address());
   copy_src(alu.src[1], instr.value0());

   if (instr.num_components() == 1) {
      alu.op = LDS_OP2_LDS_WRITE;
   } else {
      alu.op = LDS_OP3_LDS_WRITE_REL;
      alu.lds_idx = 1;
      copy_src(alu.src[2], instr.value1());
   }

   return r600_bytecode_add_alu(m_bc, &alu) == 0;
}

bool AssemblyFromShaderLegacyImpl::emit_ldsread(const LDSReadInstruction& instr)
{
   int r;
   unsigned nread = 0;
   unsigned nfetch = 0;
   unsigned n_values = instr.num_values();

   r600_bytecode_alu alu_fetch;
   r600_bytecode_alu alu_read;

   /* We must add a new ALU clause if the fetch and read op would be split otherwise
    * r600_asm limites at 120 slots = 240 dwords */
   if (m_bc->cf_last->ndw > 240 - 4 * n_values)
      m_bc->force_add_cf = 1;

   while (nread < n_values) {
      if (nfetch < n_values) {
         memset(&alu_fetch, 0, sizeof(r600_bytecode_alu));
         alu_fetch.is_lds_idx_op = true;
         alu_fetch.op = LDS_OP1_LDS_READ_RET;

         copy_src(alu_fetch.src[0], instr.address(nfetch));
         alu_fetch.src[1].sel = V_SQ_ALU_SRC_0;
         alu_fetch.src[2].sel = V_SQ_ALU_SRC_0;
         alu_fetch.last = 1;
         r = r600_bytecode_add_alu(m_bc, &alu_fetch);
         m_bc->cf_last->nlds_read++;
         if (r)
            return false;
      }

      if (nfetch >= n_values) {
         memset(&alu_read, 0, sizeof(r600_bytecode_alu));
         copy_dst(alu_read.dst, instr.dest(nread));
         alu_read.op = ALU_OP1_MOV;
         alu_read.src[0].sel = EG_V_SQ_ALU_SRC_LDS_OQ_A_POP;
         alu_read.last = 1;
         alu_read.dst.write = 1;
         r = r600_bytecode_add_alu(m_bc, &alu_read);
         m_bc->cf_last->nqueue_read++;
         if (r)
            return false;
         ++nread;
      }
      ++nfetch;
   }
   assert(m_bc->cf_last->nlds_read == m_bc->cf_last->nqueue_read);

   return true;
}

bool AssemblyFromShaderLegacyImpl::emit_ldsatomic(const LDSAtomicInstruction& instr)
{
   if (m_bc->cf_last->ndw > 240 - 4)
      m_bc->force_add_cf = 1;

   r600_bytecode_alu alu_fetch;
   r600_bytecode_alu alu_read;

   memset(&alu_fetch, 0, sizeof(r600_bytecode_alu));
   alu_fetch.is_lds_idx_op = true;
   alu_fetch.op = instr.op();

   copy_src(alu_fetch.src[0], instr.address());
   copy_src(alu_fetch.src[1], instr.src0());

   if (instr.src1())
      copy_src(alu_fetch.src[2], *instr.src1());
   alu_fetch.last = 1;
   int r = r600_bytecode_add_alu(m_bc, &alu_fetch);
   if (r)
      return false;

   memset(&alu_read, 0, sizeof(r600_bytecode_alu));
   copy_dst(alu_read.dst, instr.dest());
   alu_read.op = ALU_OP1_MOV;
   alu_read.src[0].sel = EG_V_SQ_ALU_SRC_LDS_OQ_A_POP;
   alu_read.last = 1;
   alu_read.dst.write = 1;
   r = r600_bytecode_add_alu(m_bc, &alu_read);
   if (r)
      return false;
   return true;
}

bool AssemblyFromShaderLegacyImpl::emit_rat(const RatInstruction& instr)
{
   struct r600_bytecode_gds gds;

   int rat_idx = instr.rat_id();
   EBufferIndexMode rat_index_mode = bim_none;
   auto addr = instr.rat_id_offset();

   if (addr) {
      if (addr->type() != Value::literal) {
         rat_index_mode = emit_index_reg(*addr, 1);
      } else {
         const LiteralValue& addr_reg = static_cast<const LiteralValue&>(*addr);
         rat_idx += addr_reg.value();
      }
   }
   memset(&gds, 0, sizeof(struct r600_bytecode_gds));

   r600_bytecode_add_cfinst(m_bc, instr.cf_opcode());
   auto cf = m_bc->cf_last;
   cf->rat.id = rat_idx + m_shader->rat_base;
   cf->rat.inst = instr.rat_op();
   cf->rat.index_mode = rat_index_mode;
   cf->output.type = instr.need_ack() ? 3 : 1;
   cf->output.gpr = instr.data_gpr();
   cf->output.index_gpr = instr.index_gpr();
   cf->output.comp_mask = instr.comp_mask();
   cf->output.burst_count = instr.burst_count();
   assert(instr.data_swz(0) == PIPE_SWIZZLE_X);
   if (cf->rat.inst != RatInstruction::STORE_TYPED) {
      assert(instr.data_swz(1) == PIPE_SWIZZLE_Y ||
             instr.data_swz(1) == PIPE_SWIZZLE_MAX) ;
      assert(instr.data_swz(2) == PIPE_SWIZZLE_Z ||
             instr.data_swz(2) == PIPE_SWIZZLE_MAX) ;
   }

   cf->vpm = 1;
   cf->barrier = 1;
   cf->mark = instr.need_ack();
   cf->output.elem_size = instr.elm_size();
   return true;
}

EBufferIndexMode
AssemblyFromShaderLegacyImpl::emit_index_reg(const Value& addr, unsigned idx)
{
   assert(idx < 2);

   EAluOp idxop = idx ? op1_set_cf_idx1 : op1_set_cf_idx0;

   if (!m_bc->index_loaded[idx] || m_loop_nesting ||
       m_bc->index_reg[idx] != addr.sel()
       ||  m_bc->index_reg_chan[idx] != addr.chan()) {
      struct r600_bytecode_alu alu;

      // Make sure MOVA is not last instr in clause
      if ((m_bc->cf_last->ndw>>1) >= 110)
              m_bc->force_add_cf = 1;

      memset(&alu, 0, sizeof(alu));
      alu.op = opcode_map.at(op1_mova_int);
      alu.dst.chan = 0;
      alu.src[0].sel = addr.sel();
      alu.src[0].chan = addr.chan();
      alu.last = 1;
      sfn_log << SfnLog::assembly << "   mova_int, ";
      int r = r600_bytecode_add_alu(m_bc, &alu);
      if (r)
         return bim_invalid;

      m_bc->ar_loaded = 0;

      alu.op = opcode_map.at(idxop);
      alu.dst.chan = 0;
      alu.src[0].sel = 0;
      alu.src[0].chan = 0;
      alu.last = 1;
      sfn_log << SfnLog::assembly << "op1_set_cf_idx" << idx;
      r = r600_bytecode_add_alu(m_bc, &alu);
      if (r)
         return bim_invalid;

      m_bc->index_reg[idx] = addr.sel();
      m_bc->index_reg_chan[idx] = addr.chan();
      m_bc->index_loaded[idx] = true;
      sfn_log << SfnLog::assembly << "\n";
   }
   return idx == 0 ? bim_zero : bim_one;
}

bool AssemblyFromShaderLegacyImpl::copy_dst(r600_bytecode_alu_dst& dst,
                                            const Value& d)
{
   assert(d.type() == Value::gpr || d.type() == Value::gpr_array_value);

   if (d.sel() > 124) {
      R600_ERR("shader_from_nir: Don't support more then 124 GPRs, but try using %d\n", d.sel());
      return false;
   }

   dst.sel = d.sel();
   dst.chan = d.chan();

   if (m_bc->index_reg[1] == dst.sel &&
       m_bc->index_reg_chan[1] == dst.chan)
      m_bc->index_loaded[1] = false;

   if (m_bc->index_reg[0] == dst.sel &&
       m_bc->index_reg_chan[0] == dst.chan)
      m_bc->index_loaded[0] = false;

   return true;
}

bool AssemblyFromShaderLegacyImpl::copy_src(r600_bytecode_alu_src& src, const Value& s)
{

   if (s.type() == Value::gpr && s.sel() > 124) {
      R600_ERR("shader_from_nir: Don't support more then 124 GPRs, try using %d\n", s.sel());
      return false;
   }

   if (s.type() == Value::lds_direct)  {
      R600_ERR("shader_from_nir: LDS_DIRECT values not supported\n");
      return false;
   }

   if (s.type() == Value::kconst && s.sel() < 512)  {
      R600_ERR("shader_from_nir: Uniforms should have values >= 512, got %d \n", s.sel());
      return false;
   }

   if (s.type() == Value::literal) {
      auto& v = static_cast<const LiteralValue&>(s);
      if (v.value() == 0) {
         src.sel = ALU_SRC_0;
         src.chan = 0;
         --m_nliterals_in_group;
         return true;
      }
      if (v.value() == 1) {
         src.sel = ALU_SRC_1_INT;
         src.chan = 0;
         --m_nliterals_in_group;
         return true;
      }
      if (v.value_float() == 1.0f) {
         src.sel = ALU_SRC_1;
         src.chan = 0;
         --m_nliterals_in_group;
         return true;
      }
      if (v.value_float() == 0.5f) {
         src.sel = ALU_SRC_0_5;
         src.chan = 0;
         --m_nliterals_in_group;
         return true;
      }
      if (v.value() == 0xffffffff) {
         src.sel = ALU_SRC_M_1_INT;
         src.chan = 0;
         --m_nliterals_in_group;
         return true;
      }
      src.value = v.value();
   }

   src.sel = s.sel();
   src.chan = s.chan();
   if (s.type() == Value::kconst) {
      const UniformValue& cv = static_cast<const UniformValue&>(s);
      src.kc_bank = cv.kcache_bank();
      auto addr = cv.addr();
      if (addr) {
         src.kc_rel = 1;
         emit_index_reg(*addr, 0);
         auto type = m_bc->cf_last->op;
         if (r600_bytecode_add_cf(m_bc)) {
                 return false;
         }
         m_bc->cf_last->op = type;
      }
   }

   return true;
}

const std::map<EAluOp, int> opcode_map = {

   {op2_add, ALU_OP2_ADD},
   {op2_mul, ALU_OP2_MUL},
   {op2_mul_ieee, ALU_OP2_MUL_IEEE},
   {op2_max, ALU_OP2_MAX},
   {op2_min, ALU_OP2_MIN},
   {op2_max_dx10, ALU_OP2_MAX_DX10},
   {op2_min_dx10, ALU_OP2_MIN_DX10},
   {op2_sete, ALU_OP2_SETE},
   {op2_setgt, ALU_OP2_SETGT},
   {op2_setge, ALU_OP2_SETGE},
   {op2_setne, ALU_OP2_SETNE},
   {op2_sete_dx10, ALU_OP2_SETE_DX10},
   {op2_setgt_dx10, ALU_OP2_SETGT_DX10},
   {op2_setge_dx10, ALU_OP2_SETGE_DX10},
   {op2_setne_dx10, ALU_OP2_SETNE_DX10},
   {op1_fract, ALU_OP1_FRACT},
   {op1_trunc, ALU_OP1_TRUNC},
   {op1_ceil, ALU_OP1_CEIL},
   {op1_rndne, ALU_OP1_RNDNE},
   {op1_floor, ALU_OP1_FLOOR},
   {op2_ashr_int, ALU_OP2_ASHR_INT},
   {op2_lshr_int, ALU_OP2_LSHR_INT},
   {op2_lshl_int, ALU_OP2_LSHL_INT},
   {op1_mov, ALU_OP1_MOV},
   {op0_nop, ALU_OP0_NOP},
   {op2_mul_64, ALU_OP2_MUL_64},
   {op1v_flt64_to_flt32, ALU_OP1_FLT64_TO_FLT32},
   {op1v_flt32_to_flt64, ALU_OP1_FLT32_TO_FLT64},
   {op2_pred_setgt_uint, ALU_OP2_PRED_SETGT_UINT},
   {op2_pred_setge_uint, ALU_OP2_PRED_SETGE_UINT},
   {op2_pred_sete, ALU_OP2_PRED_SETE},
   {op2_pred_setgt, ALU_OP2_PRED_SETGT},
   {op2_pred_setge, ALU_OP2_PRED_SETGE},
   {op2_pred_setne, ALU_OP2_PRED_SETNE},
   //{op2_pred_set_inv, ALU_OP2_PRED_SET},
   //{op2_pred_set_clr, ALU_OP2_PRED_SET_CRL},
   //{op2_pred_set_restore, ALU_OP2_PRED_SET_RESTORE},
   {op2_pred_sete_push, ALU_OP2_PRED_SETE_PUSH},
   {op2_pred_setgt_push, ALU_OP2_PRED_SETGT_PUSH},
   {op2_pred_setge_push, ALU_OP2_PRED_SETGE_PUSH},
   {op2_pred_setne_push, ALU_OP2_PRED_SETNE_PUSH},
   {op2_kille, ALU_OP2_KILLE},
   {op2_killgt, ALU_OP2_KILLGT},
   {op2_killge, ALU_OP2_KILLGE},
   {op2_killne, ALU_OP2_KILLNE},
   {op2_and_int, ALU_OP2_AND_INT},
   {op2_or_int, ALU_OP2_OR_INT},
   {op2_xor_int, ALU_OP2_XOR_INT},
   {op1_not_int, ALU_OP1_NOT_INT},
   {op2_add_int, ALU_OP2_ADD_INT},
   {op2_sub_int, ALU_OP2_SUB_INT},
   {op2_max_int, ALU_OP2_MAX_INT},
   {op2_min_int, ALU_OP2_MIN_INT},
   {op2_max_uint, ALU_OP2_MAX_UINT},
   {op2_min_uint, ALU_OP2_MIN_UINT},
   {op2_sete_int, ALU_OP2_SETE_INT},
   {op2_setgt_int, ALU_OP2_SETGT_INT},
   {op2_setge_int, ALU_OP2_SETGE_INT},
   {op2_setne_int, ALU_OP2_SETNE_INT},
   {op2_setgt_uint, ALU_OP2_SETGT_UINT},
   {op2_setge_uint, ALU_OP2_SETGE_UINT},
   {op2_killgt_uint, ALU_OP2_KILLGT_UINT},
   {op2_killge_uint, ALU_OP2_KILLGE_UINT},
   //p2_prede_int, ALU_OP2_PREDE_INT},
   {op2_pred_setgt_int, ALU_OP2_PRED_SETGT_INT},
   {op2_pred_setge_int, ALU_OP2_PRED_SETGE_INT},
   {op2_pred_setne_int, ALU_OP2_PRED_SETNE_INT},
   {op2_kille_int, ALU_OP2_KILLE_INT},
   {op2_killgt_int, ALU_OP2_KILLGT_INT},
   {op2_killge_int, ALU_OP2_KILLGE_INT},
   {op2_killne_int, ALU_OP2_KILLNE_INT},
   {op2_pred_sete_push_int, ALU_OP2_PRED_SETE_PUSH_INT},
   {op2_pred_setgt_push_int, ALU_OP2_PRED_SETGT_PUSH_INT},
   {op2_pred_setge_push_int, ALU_OP2_PRED_SETGE_PUSH_INT},
   {op2_pred_setne_push_int, ALU_OP2_PRED_SETNE_PUSH_INT},
   {op2_pred_setlt_push_int, ALU_OP2_PRED_SETLT_PUSH_INT},
   {op2_pred_setle_push_int, ALU_OP2_PRED_SETLE_PUSH_INT},
   {op1_flt_to_int, ALU_OP1_FLT_TO_INT},
   {op1_bfrev_int, ALU_OP1_BFREV_INT},
   {op2_addc_uint, ALU_OP2_ADDC_UINT},
   {op2_subb_uint, ALU_OP2_SUBB_UINT},
   {op0_group_barrier, ALU_OP0_GROUP_BARRIER},
   {op0_group_seq_begin, ALU_OP0_GROUP_SEQ_BEGIN},
   {op0_group_seq_end, ALU_OP0_GROUP_SEQ_END},
   {op2_set_mode, ALU_OP2_SET_MODE},
   {op1_set_cf_idx0, ALU_OP0_SET_CF_IDX0},
   {op1_set_cf_idx1, ALU_OP0_SET_CF_IDX1},
   {op2_set_lds_size, ALU_OP2_SET_LDS_SIZE},
   {op1_exp_ieee, ALU_OP1_EXP_IEEE},
   {op1_log_clamped, ALU_OP1_LOG_CLAMPED},
   {op1_log_ieee, ALU_OP1_LOG_IEEE},
   {op1_recip_clamped, ALU_OP1_RECIP_CLAMPED},
   {op1_recip_ff, ALU_OP1_RECIP_FF},
   {op1_recip_ieee, ALU_OP1_RECIP_IEEE},
   {op1_recipsqrt_clamped, ALU_OP1_RECIPSQRT_CLAMPED},
   {op1_recipsqrt_ff, ALU_OP1_RECIPSQRT_FF},
   {op1_recipsqrt_ieee1, ALU_OP1_RECIPSQRT_IEEE},
   {op1_sqrt_ieee, ALU_OP1_SQRT_IEEE},
   {op1_sin, ALU_OP1_SIN},
   {op1_cos, ALU_OP1_COS},
   {op2_mullo_int, ALU_OP2_MULLO_INT},
   {op2_mulhi_int, ALU_OP2_MULHI_INT},
   {op2_mullo_uint, ALU_OP2_MULLO_UINT},
   {op2_mulhi_uint, ALU_OP2_MULHI_UINT},
   {op1_recip_int, ALU_OP1_RECIP_INT},
   {op1_recip_uint, ALU_OP1_RECIP_UINT},
   {op1_recip_64, ALU_OP2_RECIP_64},
   {op1_recip_clamped_64, ALU_OP2_RECIP_CLAMPED_64},
   {op1_recipsqrt_64, ALU_OP2_RECIPSQRT_64},
   {op1_recipsqrt_clamped_64, ALU_OP2_RECIPSQRT_CLAMPED_64},
   {op1_sqrt_64, ALU_OP2_SQRT_64},
   {op1_flt_to_uint, ALU_OP1_FLT_TO_UINT},
   {op1_int_to_flt, ALU_OP1_INT_TO_FLT},
   {op1_uint_to_flt, ALU_OP1_UINT_TO_FLT},
   {op2_bfm_int, ALU_OP2_BFM_INT},
   {op1_flt32_to_flt16, ALU_OP1_FLT32_TO_FLT16},
   {op1_flt16_to_flt32, ALU_OP1_FLT16_TO_FLT32},
   {op1_ubyte0_flt, ALU_OP1_UBYTE0_FLT},
   {op1_ubyte1_flt, ALU_OP1_UBYTE1_FLT},
   {op1_ubyte2_flt, ALU_OP1_UBYTE2_FLT},
   {op1_ubyte3_flt, ALU_OP1_UBYTE3_FLT},
   {op1_bcnt_int, ALU_OP1_BCNT_INT},
   {op1_ffbh_uint, ALU_OP1_FFBH_UINT},
   {op1_ffbl_int, ALU_OP1_FFBL_INT},
   {op1_ffbh_int, ALU_OP1_FFBH_INT},
   {op1_flt_to_uint4, ALU_OP1_FLT_TO_UINT4},
   {op2_dot_ieee, ALU_OP2_DOT_IEEE},
   {op1_flt_to_int_rpi, ALU_OP1_FLT_TO_INT_RPI},
   {op1_flt_to_int_floor, ALU_OP1_FLT_TO_INT_FLOOR},
   {op2_mulhi_uint24, ALU_OP2_MULHI_UINT24},
   {op1_mbcnt_32hi_int, ALU_OP1_MBCNT_32HI_INT},
   {op1_offset_to_flt, ALU_OP1_OFFSET_TO_FLT},
   {op2_mul_uint24, ALU_OP2_MUL_UINT24},
   {op1_bcnt_accum_prev_int, ALU_OP1_BCNT_ACCUM_PREV_INT},
   {op1_mbcnt_32lo_accum_prev_int, ALU_OP1_MBCNT_32LO_ACCUM_PREV_INT},
   {op2_sete_64, ALU_OP2_SETE_64},
   {op2_setne_64, ALU_OP2_SETNE_64},
   {op2_setgt_64, ALU_OP2_SETGT_64},
   {op2_setge_64, ALU_OP2_SETGE_64},
   {op2_min_64, ALU_OP2_MIN_64},
   {op2_max_64, ALU_OP2_MAX_64},
   {op2_dot4, ALU_OP2_DOT4},
   {op2_dot4_ieee, ALU_OP2_DOT4_IEEE},
   {op2_cube, ALU_OP2_CUBE},
   {op1_max4, ALU_OP1_MAX4},
   {op1_frexp_64, ALU_OP1_FREXP_64},
   {op1_ldexp_64, ALU_OP2_LDEXP_64},
   {op1_fract_64, ALU_OP1_FRACT_64},
   {op2_pred_setgt_64, ALU_OP2_PRED_SETGT_64},
   {op2_pred_sete_64, ALU_OP2_PRED_SETE_64},
   {op2_pred_setge_64, ALU_OP2_PRED_SETGE_64},
   {op2_add_64, ALU_OP2_ADD_64},
   {op1_mova_int, ALU_OP1_MOVA_INT},
   {op1v_flt64_to_flt32, ALU_OP1_FLT64_TO_FLT32},
   {op1_flt32_to_flt64, ALU_OP1_FLT32_TO_FLT64},
   {op2_sad_accum_prev_uint, ALU_OP2_SAD_ACCUM_PREV_UINT},
   {op2_dot, ALU_OP2_DOT},
   //p2_mul_prev, ALU_OP2_MUL_PREV},
   //p2_mul_ieee_prev, ALU_OP2_MUL_IEEE_PREV},
   //p2_add_prev, ALU_OP2_ADD_PREV},
   {op2_muladd_prev, ALU_OP2_MULADD_PREV},
   {op2_muladd_ieee_prev, ALU_OP2_MULADD_IEEE_PREV},
   {op2_interp_xy, ALU_OP2_INTERP_XY},
   {op2_interp_zw, ALU_OP2_INTERP_ZW},
   {op2_interp_x, ALU_OP2_INTERP_X},
   {op2_interp_z, ALU_OP2_INTERP_Z},
   {op0_store_flags, ALU_OP1_STORE_FLAGS},
   {op1_load_store_flags, ALU_OP1_LOAD_STORE_FLAGS},
   {op0_lds_1a, ALU_OP2_LDS_1A},
   {op0_lds_1a1d, ALU_OP2_LDS_1A1D},
   {op0_lds_2a, ALU_OP2_LDS_2A},
   {op1_interp_load_p0, ALU_OP1_INTERP_LOAD_P0},
   {op1_interp_load_p10, ALU_OP1_INTERP_LOAD_P10},
   {op1_interp_load_p20, ALU_OP1_INTERP_LOAD_P20},
      // {op 3 all left shift 6
   {op3_bfe_uint, ALU_OP3_BFE_UINT},
   {op3_bfe_int, ALU_OP3_BFE_INT},
   {op3_bfi_int, ALU_OP3_BFI_INT},
   {op3_fma, ALU_OP3_FMA},
   {op3_cndne_64, ALU_OP3_CNDNE_64},
   {op3_fma_64, ALU_OP3_FMA_64},
   {op3_lerp_uint, ALU_OP3_LERP_UINT},
   {op3_bit_align_int, ALU_OP3_BIT_ALIGN_INT},
   {op3_byte_align_int, ALU_OP3_BYTE_ALIGN_INT},
   {op3_sad_accum_uint, ALU_OP3_SAD_ACCUM_UINT},
   {op3_sad_accum_hi_uint, ALU_OP3_SAD_ACCUM_HI_UINT},
   {op3_muladd_uint24, ALU_OP3_MULADD_UINT24},
   {op3_lds_idx_op, ALU_OP3_LDS_IDX_OP},
   {op3_muladd, ALU_OP3_MULADD},
   {op3_muladd_m2, ALU_OP3_MULADD_M2},
   {op3_muladd_m4, ALU_OP3_MULADD_M4},
   {op3_muladd_d2, ALU_OP3_MULADD_D2},
   {op3_muladd_ieee, ALU_OP3_MULADD_IEEE},
   {op3_cnde, ALU_OP3_CNDE},
   {op3_cndgt, ALU_OP3_CNDGT},
   {op3_cndge, ALU_OP3_CNDGE},
   {op3_cnde_int, ALU_OP3_CNDE_INT},
   {op3_cndgt_int, ALU_OP3_CNDGT_INT},
   {op3_cndge_int, ALU_OP3_CNDGE_INT},
   {op3_mul_lit, ALU_OP3_MUL_LIT},
};

const std::map<ESDOp, int> ds_opcode_map = {
   {DS_OP_ADD, FETCH_OP_GDS_ADD},
   {DS_OP_SUB, FETCH_OP_GDS_SUB},
   {DS_OP_RSUB, FETCH_OP_GDS_RSUB},
   {DS_OP_INC, FETCH_OP_GDS_INC},
   {DS_OP_DEC, FETCH_OP_GDS_DEC},
   {DS_OP_MIN_INT, FETCH_OP_GDS_MIN_INT},
   {DS_OP_MAX_INT, FETCH_OP_GDS_MAX_INT},
   {DS_OP_MIN_UINT, FETCH_OP_GDS_MIN_UINT},
   {DS_OP_MAX_UINT, FETCH_OP_GDS_MAX_UINT},
   {DS_OP_AND, FETCH_OP_GDS_AND},
   {DS_OP_OR, FETCH_OP_GDS_OR},
   {DS_OP_XOR, FETCH_OP_GDS_XOR},
   {DS_OP_MSKOR, FETCH_OP_GDS_MSKOR},
   {DS_OP_WRITE, FETCH_OP_GDS_WRITE},
   {DS_OP_WRITE_REL, FETCH_OP_GDS_WRITE_REL},
   {DS_OP_WRITE2, FETCH_OP_GDS_WRITE2},
   {DS_OP_CMP_STORE, FETCH_OP_GDS_CMP_STORE},
   {DS_OP_CMP_STORE_SPF, FETCH_OP_GDS_CMP_STORE_SPF},
   {DS_OP_BYTE_WRITE, FETCH_OP_GDS_BYTE_WRITE},
   {DS_OP_SHORT_WRITE, FETCH_OP_GDS_SHORT_WRITE},
   {DS_OP_ADD_RET, FETCH_OP_GDS_ADD_RET},
   {DS_OP_SUB_RET, FETCH_OP_GDS_SUB_RET},
   {DS_OP_RSUB_RET, FETCH_OP_GDS_RSUB_RET},
   {DS_OP_INC_RET, FETCH_OP_GDS_INC_RET},
   {DS_OP_DEC_RET, FETCH_OP_GDS_DEC_RET},
   {DS_OP_MIN_INT_RET, FETCH_OP_GDS_MIN_INT_RET},
   {DS_OP_MAX_INT_RET, FETCH_OP_GDS_MAX_INT_RET},
   {DS_OP_MIN_UINT_RET, FETCH_OP_GDS_MIN_UINT_RET},
   {DS_OP_MAX_UINT_RET, FETCH_OP_GDS_MAX_UINT_RET},
   {DS_OP_AND_RET, FETCH_OP_GDS_AND_RET},
   {DS_OP_OR_RET, FETCH_OP_GDS_OR_RET},
   {DS_OP_XOR_RET, FETCH_OP_GDS_XOR_RET},
   {DS_OP_MSKOR_RET, FETCH_OP_GDS_MSKOR_RET},
   {DS_OP_XCHG_RET, FETCH_OP_GDS_XCHG_RET},
   {DS_OP_XCHG_REL_RET, FETCH_OP_GDS_XCHG_REL_RET},
   {DS_OP_XCHG2_RET, FETCH_OP_GDS_XCHG2_RET},
   {DS_OP_CMP_XCHG_RET, FETCH_OP_GDS_CMP_XCHG_RET},
   {DS_OP_CMP_XCHG_SPF_RET, FETCH_OP_GDS_CMP_XCHG_SPF_RET},
   {DS_OP_READ_RET, FETCH_OP_GDS_READ_RET},
   {DS_OP_READ_REL_RET, FETCH_OP_GDS_READ_REL_RET},
   {DS_OP_READ2_RET, FETCH_OP_GDS_READ2_RET},
   {DS_OP_READWRITE_RET, FETCH_OP_GDS_READWRITE_RET},
   {DS_OP_BYTE_READ_RET, FETCH_OP_GDS_BYTE_READ_RET},
   {DS_OP_UBYTE_READ_RET, FETCH_OP_GDS_UBYTE_READ_RET},
   {DS_OP_SHORT_READ_RET, FETCH_OP_GDS_SHORT_READ_RET},
   {DS_OP_USHORT_READ_RET, FETCH_OP_GDS_USHORT_READ_RET},
   {DS_OP_ATOMIC_ORDERED_ALLOC_RET, FETCH_OP_GDS_ATOMIC_ORDERED_ALLOC},
   {DS_OP_INVALID, 0},
};

}
