/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2019 Collabora LTD
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

#ifndef sfn_shader_from_nir_h
#define sfn_shader_from_nir_h


#include "gallium/drivers/r600/r600_shader.h"

#include "compiler/nir/nir.h"
#include "compiler/nir_types.h"

#include "sfn_instruction_block.h"
#include "sfn_instruction_export.h"
#include "sfn_alu_defines.h"
#include "sfn_valuepool.h"
#include "sfn_debug.h"
#include "sfn_instruction_cf.h"
#include "sfn_emittexinstruction.h"
#include "sfn_emitaluinstruction.h"
#include "sfn_emitssboinstruction.h"

#include <vector>
#include <set>
#include <stack>
#include <unordered_map>

struct nir_instr;

namespace r600 {

extern SfnLog sfn_log;

class ShaderFromNirProcessor : public ValuePool {
public:
   ShaderFromNirProcessor(pipe_shader_type ptype, r600_pipe_shader_selector& sel,
                          r600_shader& sh_info, int scratch_size, enum chip_class _chip_class,
                          int atomic_base);
   virtual ~ShaderFromNirProcessor();

   void emit_instruction(Instruction *ir);

   PValue from_nir_with_fetch_constant(const nir_src& src, unsigned component, int channel = -1);
   GPRVector vec_from_nir_with_fetch_constant(const nir_src& src, unsigned mask,
                                              const GPRVector::Swizzle& swizzle, bool match = false);

   bool emit_instruction(EAluOp opcode, PValue dest,
                         std::vector<PValue> src0,
                         const std::set<AluModifiers>& m_flags);
   void emit_export_instruction(WriteoutInstruction *ir);
   void emit_instruction(AluInstruction *ir);

   void split_constants(nir_alu_instr* instr);
   void remap_registers();

   const nir_variable *get_deref_location(const nir_src& src) const;

   r600_shader& sh_info() {return m_sh_info;}
   void add_param_output_reg(int loc, const GPRVector *gpr);
   void set_output(unsigned pos, int sel);
   const GPRVector *output_register(unsigned location) const;
   void evaluate_spi_sid(r600_shader_io &io);

   enum chip_class get_chip_class() const;

   int remap_atomic_base(int base) {
      return m_atomic_base_map[base];
   }

   void get_array_info(r600_shader& shader) const;

   virtual bool scan_inputs_read(const nir_shader *sh);
   void set_shader_info(const nir_shader *sh);

protected:

   void set_var_address(nir_deref_instr *instr);
   void set_input(unsigned pos, PValue var);

   bool scan_instruction(nir_instr *instr);

   virtual bool scan_sysvalue_access(nir_instr *instr) = 0;

   bool emit_if_start(int if_id, nir_if *if_stmt);
   bool emit_else_start(int if_id);
   bool emit_ifelse_end(int if_id);

   bool emit_loop_start(int loop_id);
   bool emit_loop_end(int loop_id);
   bool emit_jump_instruction(nir_jump_instr *instr);

   bool emit_load_tcs_param_base(nir_intrinsic_instr* instr, int offset);
   bool emit_load_local_shared(nir_intrinsic_instr* instr);
   bool emit_store_local_shared(nir_intrinsic_instr* instr);
   bool emit_atomic_local_shared(nir_intrinsic_instr* instr);

   bool emit_barrier(nir_intrinsic_instr* instr);

   bool load_preloaded_value(const nir_dest& dest, int chan, PValue value,
                             bool as_last = true);

   void inc_atomic_file_count();

   virtual void do_set_shader_info(const nir_shader *sh);

   enum ESlots {
      es_face,
      es_instanceid,
      es_invocation_id,
      es_patch_id,
      es_pos,
      es_rel_patch_id,
      es_sample_mask_in,
      es_sample_id,
      es_sample_pos,
      es_tess_factor_base,
      es_vertexid,
      es_tess_coord,
      es_primitive_id,
      es_helper_invocation,
      es_last
   };

   std::bitset<es_last> m_sv_values;

   bool allocate_reserved_registers();


private:
   virtual bool do_allocate_reserved_registers() = 0;


   void emit_instruction_internal(Instruction *ir);

   bool emit_alu_instruction(nir_instr *instr);
   bool emit_deref_instruction(nir_deref_instr* instr);
   bool emit_intrinsic_instruction(nir_intrinsic_instr* instr);
   virtual bool emit_intrinsic_instruction_override(nir_intrinsic_instr* instr);
   bool emit_tex_instruction(nir_instr* instr);
   bool emit_discard_if(nir_intrinsic_instr* instr);
   bool emit_load_ubo_vec4(nir_intrinsic_instr* instr);
   bool emit_ssbo_atomic_add(nir_intrinsic_instr* instr);
   bool load_uniform_indirect(nir_intrinsic_instr* instr, PValue addr, int offest, int bufid);

   /* Code creating functions */
   bool emit_load_function_temp(const nir_variable *var, nir_intrinsic_instr *instr);
   AluInstruction *emit_load_literal(const nir_load_const_instr *literal, const nir_src& src, unsigned writemask);

   bool load_uniform(nir_intrinsic_instr* instr);
   bool process_uniforms(nir_variable *uniform);

   void append_block(int nesting_change);

   virtual void emit_shader_start();
   virtual bool emit_deref_instruction_override(nir_deref_instr* instr);

   bool emit_store_scratch(nir_intrinsic_instr* instr);
   bool emit_load_scratch(nir_intrinsic_instr* instr);
   bool emit_shader_clock(nir_intrinsic_instr* instr);
   virtual void do_finalize() = 0;

   void finalize();
   friend class ShaderFromNir;

   std::set<nir_variable*> m_arrays;

   std::map<unsigned, PValue> m_inputs;
   std::map<unsigned, int> m_outputs;

   std::map<unsigned, nir_variable*> m_var_derefs;
   std::map<const nir_variable *, nir_variable_mode> m_var_mode;

   std::map<unsigned, const glsl_type*>  m_uniform_type_map;
   std::map<int, IfElseInstruction *> m_if_block_start_map;
   std::map<int, LoopBeginInstruction *> m_loop_begin_block_map;

   pipe_shader_type m_processor_type;

   std::vector<InstructionBlock> m_output;
   unsigned m_nesting_depth;
   unsigned m_block_number;
   InstructionBlock m_export_output;
   r600_shader& m_sh_info;
   enum chip_class m_chip_class;
   EmitTexInstruction m_tex_instr;
   EmitAluInstruction m_alu_instr;
   EmitSSBOInstruction m_ssbo_instr;
   OutputRegisterMap m_output_register_map;

   IfElseInstruction *m_pending_else;
   int m_scratch_size;
   int m_next_hwatomic_loc;

   r600_pipe_shader_selector& m_sel;
   int m_atomic_base ;
   int m_image_count;

   std::unordered_map<int, int> m_atomic_base_map;
   AluInstruction *last_emitted_alu;
};

}

#endif
