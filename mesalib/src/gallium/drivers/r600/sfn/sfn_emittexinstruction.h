/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2018-2019 Collabora LTD
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

#ifndef SFN_EMITTEXINSTRUCTION_H
#define SFN_EMITTEXINSTRUCTION_H

#include "sfn_emitinstruction.h"
#include "sfn_instruction_tex.h"

namespace r600  {

class EmitTexInstruction : public EmitInstruction
{
public:
   EmitTexInstruction(ShaderFromNirProcessor& processor);

private:
   struct TexInputs {
      TexInputs();
      const nir_variable *sampler_deref;
      const nir_variable *texture_deref;
      GPRVector coord;
      PValue bias;
      PValue comperator;
      PValue lod;
      GPRVector ddx;
      GPRVector ddy;
      nir_src *offset;
      PValue gather_comp;
      PValue ms_index;
      PValue sampler_offset;
      PValue texture_offset;
   };


   bool emit_cube_tex(nir_tex_instr* instr, TexInputs& src);
   bool emit_cube_txf(nir_tex_instr* instr, TexInputs& src);
   bool emit_cube_txb(nir_tex_instr* instr, TexInputs& src);
   bool emit_cube_txl(nir_tex_instr* instr, TexInputs& src);
   bool emit_cube_txd(nir_tex_instr* instr, TexInputs& src);
   bool emit_cube_lod(nir_tex_instr* instr, TexInputs& src);
   bool emit_cube_tg4(nir_tex_instr* instr, TexInputs& src);
   bool emit_cube_prep(const GPRVector& coord, GPRVector& cubed, bool is_array);

   bool emit_tex_tex(nir_tex_instr* instr, TexInputs& src);

   bool emit_tex_txf(nir_tex_instr* instr, TexInputs &src);
   bool emit_tex_txb(nir_tex_instr* instr, TexInputs& src);
   bool emit_tex_txd(nir_tex_instr* instr, TexInputs& src);
   bool emit_tex_txl(nir_tex_instr* instr, TexInputs& src);
   bool emit_tex_txs(nir_tex_instr* instr, TexInputs& src,
                     const std::array<int, 4> &dest_swz);
   bool emit_tex_texture_samples(nir_tex_instr* instr, TexInputs& src,
                                 const std::array<int, 4> &dest_swz);
   bool emit_tex_lod(nir_tex_instr* instr, TexInputs& src);
   bool emit_tex_tg4(nir_tex_instr* instr, TexInputs& src);
   bool emit_tex_txf_ms(nir_tex_instr* instr, TexInputs& src);
   bool emit_buf_txf(nir_tex_instr* instr, TexInputs& src);

   bool get_inputs(const nir_tex_instr& instr, TexInputs &src);

   void set_rect_coordinate_flags(nir_tex_instr* instr, TexInstruction* ir) const;

   bool do_emit(nir_instr* instr) override;

   GPRVector make_dest(nir_tex_instr& instr);
   GPRVector make_dest(nir_tex_instr &instr, const std::array<int, 4> &swizzle);

   void set_offsets(TexInstruction* ir, nir_src *offset);
   void handle_array_index(const nir_tex_instr& instr, const GPRVector &src, TexInstruction* ir);

   struct SamplerId {
      int id;
      bool indirect;
   };

   SamplerId get_samplerr_id(int sampler_id, const nir_variable *deref);

};

}

#endif // SFN_EMITTEXINSTRUCTION_H
