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

#include "sfn_instruction_tex.h"
#include "nir_builder.h"
#include "nir_builtin_builder.h"

namespace r600 {

TexInstruction::TexInstruction(Opcode op, const GPRVector &dest, const GPRVector &src,
                               unsigned sid, unsigned rid, PValue sampler_offset):
   Instruction(tex),
   m_opcode(op),
   m_dst(dest),
   m_src(src),
   m_sampler_id(sid),
   m_resource_id(rid),
   m_flags(0),
   m_inst_mode(0),
   m_dest_swizzle{0,1,2,3},
   m_sampler_offset(sampler_offset)

{
   memset(m_offset, 0, sizeof (m_offset));

   add_remappable_src_value(&m_src);
   add_remappable_src_value(&m_sampler_offset);
   add_remappable_dst_value(&m_dst);
}

void TexInstruction::set_gather_comp(int cmp)
{
   m_inst_mode = cmp;
}

void TexInstruction::replace_values(const ValueSet& candiates, PValue new_value)
{
   // I wonder whether we can actually end up here ...
   for (auto c: candiates) {
      if (*c == *m_src.reg_i(c->chan()))
         m_src.set_reg_i(c->chan(), new_value);
      if (*c == *m_dst.reg_i(c->chan()))
         m_dst.set_reg_i(c->chan(), new_value);
   }
}

void TexInstruction::set_offset(unsigned index, int32_t val)
{
   assert(index < 3);
   m_offset[index] = val;
}

int TexInstruction::get_offset(unsigned index) const
{
   assert(index < 3);
   return (m_offset[index] << 1 & 0x1f);
}

bool TexInstruction::is_equal_to(const Instruction& rhs) const
{
   assert(rhs.type() == tex);
   const auto& r = static_cast<const TexInstruction&>(rhs);
   return (m_opcode == r.m_opcode &&
           m_dst == r.m_dst &&
           m_src == r.m_src &&
           m_sampler_id == r.m_sampler_id &&
           m_resource_id == r.m_resource_id);
}

void TexInstruction::do_print(std::ostream& os) const
{
   const char *map_swz = "xyzw01?_";
   os << opname(m_opcode) << " R" << m_dst.sel() << ".";
   for (int i = 0; i < 4; ++i)
      os << map_swz[m_dest_swizzle[i]];

   os << " " << m_src
      << " RESID:"  << m_resource_id << " SAMPLER:"
      << m_sampler_id;
}

const char *TexInstruction::opname(Opcode op)
{
   switch (op) {
   case ld: return "LD";
   case get_resinfo: return "GET_TEXTURE_RESINFO";
   case get_nsampled: return "GET_NUMBER_OF_SAMPLES";
   case get_tex_lod: return "GET_LOD";
   case get_gradient_h: return "GET_GRADIENTS_H";
   case get_gradient_v: return "GET_GRADIENTS_V";
   case set_offsets: return "SET_TEXTURE_OFFSETS";
   case keep_gradients: return "KEEP_GRADIENTS";
   case set_gradient_h: return "SET_GRADIENTS_H";
   case set_gradient_v: return "SET_GRADIENTS_V";
   case sample: return "SAMPLE";
   case sample_l: return "SAMPLE_L";
   case sample_lb: return "SAMPLE_LB";
   case sample_lz: return "SAMPLE_LZ";
   case sample_g: return "SAMPLE_G";
   case sample_g_lb: return "SAMPLE_G_L";
   case gather4: return "GATHER4";
   case gather4_o: return "GATHER4_O";
   case sample_c: return "SAMPLE_C";
   case sample_c_l: return "SAMPLE_C_L";
   case sample_c_lb: return "SAMPLE_C_LB";
   case sample_c_lz: return "SAMPLE_C_LZ";
   case sample_c_g: return "SAMPLE_C_G";
   case sample_c_g_lb: return "SAMPLE_C_G_L";
   case gather4_c: return "GATHER4_C";
   case gather4_c_o: return "OP_GATHER4_C_O";
   }
   return "ERROR";
}



static bool lower_coord_shift_normalized(nir_builder& b, nir_tex_instr *tex)
{
   b.cursor = nir_before_instr(&tex->instr);

   nir_ssa_def * size = nir_i2f32(&b, nir_get_texture_size(&b, tex));
   nir_ssa_def *scale = nir_frcp(&b, size);

   int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   nir_ssa_def *corr = nir_fadd(&b,
                                nir_fmul(&b, nir_imm_float(&b, -0.5f), scale),
                                tex->src[coord_index].src.ssa);
   nir_instr_rewrite_src(&tex->instr, &tex->src[coord_index].src,
                         nir_src_for_ssa(corr));
   return true;
}

static bool lower_coord_shift_unnormalized(nir_builder& b, nir_tex_instr *tex)
{
   b.cursor = nir_before_instr(&tex->instr);
   int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   nir_ssa_def *corr = nir_fadd(&b, tex->src[coord_index].src.ssa,
                                nir_imm_float(&b, -0.5f));
   nir_instr_rewrite_src(&tex->instr, &tex->src[coord_index].src,
                         nir_src_for_ssa(corr));
   return true;
}

static bool
r600_nir_lower_int_tg4_impl(nir_function_impl *impl)
{
   nir_builder b;
   nir_builder_init(&b, impl);

   bool progress = false;
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type == nir_instr_type_tex) {
            nir_tex_instr *tex = nir_instr_as_tex(instr);
            if (tex->op == nir_texop_tg4 &&
                tex->sampler_dim != GLSL_SAMPLER_DIM_CUBE) {
               if (nir_alu_type_get_base_type(tex->dest_type) != nir_type_float) {
                  if (tex->sampler_dim != GLSL_SAMPLER_DIM_RECT)
                     lower_coord_shift_normalized(b, tex);
                  else
                     lower_coord_shift_unnormalized(b, tex);
                  progress = true;
               }
            }
         }
      }
   }
   return progress;
}

/*
 * This lowering pass works around a bug in r600 when doing TG4 from
 * integral valued samplers.

 * Gather4 should follow the same rules as bilinear filtering, but the hardware
 * incorrectly forces nearest filtering if the texture format is integer.
 * The only effect it has on Gather4, which always returns 4 texels for
 * bilinear filtering, is that the final coordinates are off by 0.5 of
 * the texel size.
*/

bool r600_nir_lower_int_tg4(nir_shader *shader)
{
   bool progress = false;
   bool need_lowering = false;

   nir_foreach_uniform_variable(var, shader) {
      if (var->type->is_sampler()) {
         if (glsl_base_type_is_integer(var->type->sampled_type)) {
            need_lowering = true;
         }
      }
   }

   if (need_lowering) {
      nir_foreach_function(function, shader) {
         if (function->impl && r600_nir_lower_int_tg4_impl(function->impl))
            progress = true;
      }
   }

   return progress;
}

static
bool lower_txl_txf_array_or_cube(nir_builder *b, nir_tex_instr *tex)
{
   assert(tex->op == nir_texop_txb || tex->op == nir_texop_txl);
   assert(nir_tex_instr_src_index(tex, nir_tex_src_ddx) < 0);
   assert(nir_tex_instr_src_index(tex, nir_tex_src_ddy) < 0);

   b->cursor = nir_before_instr(&tex->instr);

   int lod_idx = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   int bias_idx = nir_tex_instr_src_index(tex, nir_tex_src_bias);
   int min_lod_idx = nir_tex_instr_src_index(tex, nir_tex_src_min_lod);
   assert (lod_idx >= 0 || bias_idx >= 0);

   nir_ssa_def *size = nir_i2f32(b, nir_get_texture_size(b, tex));
   nir_ssa_def *lod = (lod_idx >= 0) ?
                         nir_ssa_for_src(b, tex->src[lod_idx].src, 1) :
                         nir_get_texture_lod(b, tex);

   if (bias_idx >= 0)
      lod = nir_fadd(b, lod,nir_ssa_for_src(b, tex->src[bias_idx].src, 1));

   if (min_lod_idx >= 0)
      lod = nir_fmax(b, lod, nir_ssa_for_src(b, tex->src[min_lod_idx].src, 1));

   /* max lod? */

   nir_ssa_def *lambda_exp =  nir_fexp2(b, lod);
   nir_ssa_def *scale = NULL;

   if  (tex->is_array) {
      int cmp_mask = (1 << (size->num_components - 1)) - 1;
      scale = nir_frcp(b, nir_channels(b, size,
                                       (nir_component_mask_t)cmp_mask));
   } else if (tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
      unsigned int swizzle[NIR_MAX_VEC_COMPONENTS] = {0,0,0,0};
      scale = nir_frcp(b, nir_channels(b, size, 1));
      scale = nir_swizzle(b, scale, swizzle, 3);
   }

   nir_ssa_def *grad = nir_fmul(b, lambda_exp, scale);

   if (lod_idx >= 0)
      nir_tex_instr_remove_src(tex, lod_idx);
   if (bias_idx >= 0)
      nir_tex_instr_remove_src(tex, bias_idx);
   if (min_lod_idx >= 0)
      nir_tex_instr_remove_src(tex, min_lod_idx);
   nir_tex_instr_add_src(tex, nir_tex_src_ddx, nir_src_for_ssa(grad));
   nir_tex_instr_add_src(tex, nir_tex_src_ddy, nir_src_for_ssa(grad));

   tex->op = nir_texop_txd;
   return true;
}


static bool
r600_nir_lower_txl_txf_array_or_cube_impl(nir_function_impl *impl)
{
   nir_builder b;
   nir_builder_init(&b, impl);

   bool progress = false;
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type == nir_instr_type_tex) {
            nir_tex_instr *tex = nir_instr_as_tex(instr);

            if (tex->is_shadow &&
                (tex->op == nir_texop_txl || tex->op == nir_texop_txb) &&
                (tex->is_array || tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE))
               progress |= lower_txl_txf_array_or_cube(&b, tex);
         }
      }
   }
   return progress;
}

bool
r600_nir_lower_txl_txf_array_or_cube(nir_shader *shader)
{
   bool progress = false;
   nir_foreach_function(function, shader) {
      if (function->impl && r600_nir_lower_txl_txf_array_or_cube_impl(function->impl))
         progress = true;
   }
   return progress;
}


}
