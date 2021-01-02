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

#include "sfn_shader_compute.h"
#include "sfn_instruction_fetch.h"

namespace r600 {

ComputeShaderFromNir::ComputeShaderFromNir(r600_pipe_shader *sh,
                                           r600_pipe_shader_selector& sel,
                                           UNUSED const r600_shader_key& key,
                                           enum chip_class chip_class):
     ShaderFromNirProcessor (PIPE_SHADER_COMPUTE, sel, sh->shader,
                             sh->scratch_space_needed, chip_class, 0),
     m_reserved_registers(0)
{
}

bool ComputeShaderFromNir::scan_sysvalue_access(UNUSED nir_instr *instr)
{
   return true;
}
bool ComputeShaderFromNir::do_allocate_reserved_registers()
{
   int thread_id_sel = m_reserved_registers++;
   int wg_id_sel = m_reserved_registers++;

   for (int i = 0; i < 3; ++i) {
      auto tmp = new GPRValue(thread_id_sel, i);
      tmp->set_as_input();
      tmp->set_keep_alive();
      m_local_invocation_id[i] = PValue(tmp);
      inject_register(tmp->sel(), i, m_local_invocation_id[i], false);

      tmp = new GPRValue(wg_id_sel, i);
      tmp->set_as_input();
      tmp->set_keep_alive();
      m_workgroup_id[i] = PValue(tmp);
      inject_register(tmp->sel(), i, m_workgroup_id[i], false);
   }
   return true;
}

bool ComputeShaderFromNir::emit_intrinsic_instruction_override(nir_intrinsic_instr* instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_local_invocation_id:
      return emit_load_3vec(instr, m_local_invocation_id);
   case nir_intrinsic_load_work_group_id:
      return emit_load_3vec(instr, m_workgroup_id);
   case nir_intrinsic_load_num_work_groups:
      return emit_load_num_work_groups(instr);
   default:
      return false;
   }
}

bool ComputeShaderFromNir::emit_load_3vec(nir_intrinsic_instr* instr,
                                          const std::array<PValue,3>& src)
{
   for (int i = 0; i < 3; ++i)
      load_preloaded_value(instr->dest, i, src[i], i == 2);
   return true;
}

bool ComputeShaderFromNir::emit_load_num_work_groups(nir_intrinsic_instr* instr)
{
   int temp = allocate_temp_register();
   PValue a_zero(new GPRValue(temp, 1));
   emit_instruction(new AluInstruction(op1_mov, a_zero, Value::zero, EmitInstruction::last_write));
   GPRVector dest;
   for (int i = 0; i < 3; ++i)
      dest.set_reg_i(i, from_nir(instr->dest, i));
   dest.set_reg_i(3, from_nir(instr->dest, 7));

   auto ir = new FetchInstruction(vc_fetch, no_index_offset,
                                  fmt_32_32_32_32, vtx_nf_int, vtx_es_none, a_zero, dest, 16,
                                  false, 16, R600_BUFFER_INFO_CONST_BUFFER, 0,
                                  bim_none, false, false, 0, 0, 0, PValue(), {0,1,2,7});
   ir->set_flag(vtx_srf_mode);
   emit_instruction(ir);
   return true;
}

bool ComputeShaderFromNir::do_process_inputs(UNUSED nir_variable *input)
{
   return true;
}

bool ComputeShaderFromNir::do_process_outputs(UNUSED nir_variable *output)
{
   return true;
}

bool ComputeShaderFromNir::do_emit_load_deref(UNUSED const nir_variable *in_var,
                                              UNUSED nir_intrinsic_instr* instr)
{
   return true;
}

bool ComputeShaderFromNir::do_emit_store_deref(UNUSED const nir_variable *out_var,
                                               UNUSED nir_intrinsic_instr* instr)
{
   return true;
}
void ComputeShaderFromNir::do_finalize()
{

}

}
