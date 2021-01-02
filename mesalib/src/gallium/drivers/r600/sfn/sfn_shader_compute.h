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

#ifndef SFN_COMPUTE_SHADER_FROM_NIR_H
#define SFN_COMPUTE_SHADER_FROM_NIR_H

#include "sfn_shader_base.h"
#include "sfn_shaderio.h"
#include <bitset>

namespace r600 {

class ComputeShaderFromNir : public ShaderFromNirProcessor
{
public:
   ComputeShaderFromNir(r600_pipe_shader *sh,
                        r600_pipe_shader_selector& sel,
                        const r600_shader_key &key,
                        enum chip_class chip_class);

   bool scan_sysvalue_access(nir_instr *instr) override;

private:
   bool emit_intrinsic_instruction_override(nir_intrinsic_instr* instr) override;

   bool do_allocate_reserved_registers() override;
   bool do_process_inputs(nir_variable *input) override;
   bool do_process_outputs(nir_variable *output) override;
   bool do_emit_load_deref(const nir_variable *in_var, nir_intrinsic_instr* instr) override;
   bool do_emit_store_deref(const nir_variable *out_var, nir_intrinsic_instr* instr) override;
   void do_finalize() override;

   bool emit_load_3vec(nir_intrinsic_instr* instr, const std::array<PValue,3>& src);
   bool emit_load_num_work_groups(nir_intrinsic_instr* instr);

   int m_reserved_registers;
   std::array<PValue,3> m_workgroup_id;
   std::array<PValue,3> m_local_invocation_id;
};

}

#endif // SFN_COMPUTE_SHADER_FROM_NIR_H
