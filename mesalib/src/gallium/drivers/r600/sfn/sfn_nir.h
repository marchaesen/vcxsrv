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
#include "sfn_shader.h"
#include <vector>

namespace r600 {

class NirLowerInstruction {
public:
	NirLowerInstruction();

	bool run(nir_shader *shader);

private:
	static bool filter_instr(const nir_instr *instr, const void *data);
        static nir_ssa_def *lower_instr(nir_builder *b, nir_instr *instr,  void *data);

        void set_builder(nir_builder *_b) { b = _b;}

	virtual bool filter(const nir_instr *instr) const = 0;
	virtual nir_ssa_def *lower(nir_instr *instr) = 0;
protected:
	nir_builder *b;
};

bool r600_lower_scratch_addresses(nir_shader *shader);

bool r600_lower_ubo_to_align16(nir_shader *shader);

bool r600_nir_split_64bit_io(nir_shader *sh);

bool r600_nir_64_to_vec2(nir_shader *sh);

bool r600_merge_vec2_stores(nir_shader *shader);

bool r600_split_64bit_uniforms_and_ubo(nir_shader *sh);
bool r600_lower_64bit_to_vec2(nir_shader *sh);
bool r600_split_64bit_alu_and_phi(nir_shader *sh);
bool r600_lower_clipvertex_to_clipdist(nir_shader *sh);


class AssemblyFromShader {
public:
   virtual ~AssemblyFromShader();
   bool lower(const Shader& s);
private:
   virtual bool do_lower(const Shader& s)  = 0 ;
};

}

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
bool r600_lower_tess_coord(nir_shader *sh, enum pipe_prim_type prim_type);

bool
r600_legalize_image_load_store(nir_shader *shader);


#else
#include "gallium/drivers/r600/r600_shader.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

bool r600_vectorize_vs_inputs(nir_shader *shader);

bool r600_lower_to_scalar_instr_filter(const nir_instr *instr, const void *);

int r600_shader_from_nir(struct r600_context *rctx,
                         struct r600_pipe_shader *pipeshader,
                         union r600_shader_key *key);

#ifdef __cplusplus
}
#endif


#endif // SFN_NIR_H
