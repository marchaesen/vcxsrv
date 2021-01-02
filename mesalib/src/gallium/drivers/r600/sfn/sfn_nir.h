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

#ifndef SFN_NIR_H
#define SFN_NIR_H

#include "nir.h"
#include "nir_builder.h"

#ifdef __cplusplus
#include "sfn_shader_base.h"
#include <vector>

namespace r600 {

bool r600_nir_lower_pack_unpack_2x16(nir_shader *shader);

bool r600_lower_scratch_addresses(nir_shader *shader);

bool r600_lower_ubo_to_align16(nir_shader *shader);

class Shader {
public:
   std::vector<InstructionBlock>& m_ir;
   ValueMap m_temp;
};

class ShaderFromNir {
public:
   ShaderFromNir();
   ~ShaderFromNir();

   unsigned ninputs() const;

   bool lower(const nir_shader *shader, r600_pipe_shader *sh,
              r600_pipe_shader_selector *sel, r600_shader_key &key,
              r600_shader *gs_shader, enum chip_class chip_class);

   bool process_declaration();

   pipe_shader_type processor_type() const;

   bool emit_instruction(nir_instr *instr);

   const std::vector<InstructionBlock> &shader_ir() const;

   Shader shader() const;
private:

   bool process_block();
   bool process_cf_node(nir_cf_node *node);
   bool process_if(nir_if *node);
   bool process_loop(nir_loop *node);
   bool process_block(nir_block *node);

   std::unique_ptr<ShaderFromNirProcessor> impl;
   const nir_shader *sh;

   enum chip_class chip_class;
   int m_current_if_id;
   int m_current_loop_id;
   std::stack<int> m_if_stack;
   int scratch_size;
};

class AssemblyFromShader {
public:
   virtual ~AssemblyFromShader();
   bool lower(const std::vector<InstructionBlock> &ir);
private:
   virtual bool do_lower(const std::vector<InstructionBlock>& ir)  = 0 ;
};

}

#endif

static inline nir_ssa_def *
r600_imm_ivec3(nir_builder *build, int x, int y, int z)
{
   nir_const_value v[3] = {
      nir_const_value_for_int(x, 32),
      nir_const_value_for_int(y, 32),
      nir_const_value_for_int(z, 32),
   };

   return nir_build_imm(build, 3, 32, v);
}

bool r600_lower_tess_io(nir_shader *shader, enum pipe_prim_type prim_type);
bool r600_append_tcs_TF_emission(nir_shader *shader, enum pipe_prim_type prim_type);

#ifdef __cplusplus
extern "C" {
#endif

bool r600_vectorize_vs_inputs(nir_shader *shader);


int r600_shader_from_nir(struct r600_context *rctx,
                         struct r600_pipe_shader *pipeshader,
                         union r600_shader_key *key);


#ifdef __cplusplus
}
#endif


#endif // SFN_NIR_H
