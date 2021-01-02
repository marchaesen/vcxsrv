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

#ifndef INSTRUCTION_TEX_H
#define INSTRUCTION_TEX_H

#include "sfn_instruction_base.h"

namespace r600 {

class TexInstruction : public Instruction {
public:
   enum Opcode {
      ld = FETCH_OP_LD,
      get_resinfo = FETCH_OP_GET_TEXTURE_RESINFO,
      get_nsampled = FETCH_OP_GET_NUMBER_OF_SAMPLES,
      get_tex_lod = FETCH_OP_GET_LOD,
      get_gradient_h = FETCH_OP_GET_GRADIENTS_H,
      get_gradient_v = FETCH_OP_GET_GRADIENTS_V,
      set_offsets = FETCH_OP_SET_TEXTURE_OFFSETS,
      keep_gradients = FETCH_OP_KEEP_GRADIENTS,
      set_gradient_h = FETCH_OP_SET_GRADIENTS_H,
      set_gradient_v = FETCH_OP_SET_GRADIENTS_V,
      sample = FETCH_OP_SAMPLE,
      sample_l = FETCH_OP_SAMPLE_L,
      sample_lb = FETCH_OP_SAMPLE_LB,
      sample_lz = FETCH_OP_SAMPLE_LZ,
      sample_g = FETCH_OP_SAMPLE_G,
      sample_g_lb = FETCH_OP_SAMPLE_G_L,
      gather4 = FETCH_OP_GATHER4,
      gather4_o =  FETCH_OP_GATHER4_O,

      sample_c = FETCH_OP_SAMPLE_C,
      sample_c_l = FETCH_OP_SAMPLE_C_L,
      sample_c_lb = FETCH_OP_SAMPLE_C_LB,
      sample_c_lz = FETCH_OP_SAMPLE_C_LZ,
      sample_c_g = FETCH_OP_SAMPLE_C_G,
      sample_c_g_lb = FETCH_OP_SAMPLE_C_G_L,
      gather4_c = FETCH_OP_GATHER4_C,
      gather4_c_o =  FETCH_OP_GATHER4_C_O,

   };

   enum Flags {
      x_unnormalized,
      y_unnormalized,
      z_unnormalized,
      w_unnormalized,
      grad_fine
   };

   TexInstruction(Opcode op, const GPRVector& dest, const GPRVector& src, unsigned sid,
                  unsigned rid, PValue sampler_offset);

   const GPRVector& src() const {return m_src;}
   const GPRVector& dst() const {return m_dst;}
   unsigned opcode() const {return m_opcode;}
   unsigned sampler_id() const {return m_sampler_id;}
   unsigned resource_id() const {return m_resource_id;}

   void replace_values(const ValueSet& candiates, PValue new_value) override;

   void set_offset(unsigned index, int32_t val);
   int get_offset(unsigned index) const;

   void set_inst_mode(int inst_mode) { m_inst_mode = inst_mode;}

   int inst_mode() const { return m_inst_mode;}

   void set_flag(Flags flag) {
      m_flags.set(flag);
   }

   PValue sampler_offset() const {
      return m_sampler_offset;
   }

   bool has_flag(Flags flag) const {
      return m_flags.test(flag);
   }

   int dest_swizzle(int i) const {
      assert(i < 4);
      return m_dest_swizzle[i];
   }

   void set_dest_swizzle(const std::array<int,4>& swz) {
      m_dest_swizzle = swz;
   }

   void set_gather_comp(int cmp);

private:
   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;

   static const char *opname(Opcode code);

   Opcode m_opcode;
   GPRVector m_dst;
   GPRVector m_src;
   unsigned m_sampler_id;
   unsigned m_resource_id;
   std::bitset<8> m_flags;
   int m_offset[3];
   int m_inst_mode;
   std::array<int,4> m_dest_swizzle;
   PValue m_sampler_offset;
};

bool r600_nir_lower_int_tg4(nir_shader *nir);
bool r600_nir_lower_txl_txf_array_or_cube(nir_shader *shader);

}

#endif // INSTRUCTION_TEX_H
