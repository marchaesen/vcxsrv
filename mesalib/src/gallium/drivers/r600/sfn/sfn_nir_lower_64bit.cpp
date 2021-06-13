/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2020 Collabora LTD
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

#include "sfn_nir.h"

#include "nir.h"
#include "nir_builder.h"

#include <map>
#include <vector>
#include <iostream>

namespace r600 {

using std::map;
using std::pair;
using std::make_pair;
using std::vector;

class LowerSplit64BitVar : public NirLowerInstruction {
public:

   ~LowerSplit64BitVar();
   using VarSplit = pair<nir_variable*, nir_variable*>;
   using VarMap = map<unsigned, VarSplit>;

   nir_ssa_def *
   split_double_load_deref(nir_intrinsic_instr *intr);

   nir_ssa_def *
   split_double_store_deref(nir_intrinsic_instr *intr);

private:
   nir_ssa_def *
   split_load_deref_array(nir_intrinsic_instr *intr, nir_src& index);

   nir_ssa_def *
   split_load_deref_var(nir_intrinsic_instr *intr);

   nir_ssa_def *
   split_store_deref_array(nir_intrinsic_instr *intr, nir_deref_instr *deref);

   nir_ssa_def *
   split_store_deref_var(nir_intrinsic_instr *intr, nir_deref_instr *deref1);

   VarSplit get_var_pair(nir_variable *old_var);

   nir_ssa_def *
   merge_64bit_loads(nir_ssa_def *load1, nir_ssa_def *load2, bool out_is_vec3);

   nir_ssa_def *split_double_load(nir_intrinsic_instr *load1);

   nir_ssa_def *
   split_store_output(nir_intrinsic_instr *store1);

   nir_ssa_def *split_double_load_uniform(nir_intrinsic_instr *intr);

   nir_ssa_def *
   split_double_load_ssbo(nir_intrinsic_instr *intr);

   nir_ssa_def *
   split_double_load_ubo(nir_intrinsic_instr *intr);

   nir_ssa_def *
   split_reduction(nir_ssa_def *src[2][2], nir_op op1, nir_op op2, nir_op reduction);

   nir_ssa_def *
   split_reduction3(nir_alu_instr *alu,
                    nir_op op1, nir_op op2, nir_op reduction);

   nir_ssa_def *
   split_reduction4(nir_alu_instr *alu,
                    nir_op op1, nir_op op2, nir_op reduction);

   nir_ssa_def *split_bcsel(nir_alu_instr *alu);

   nir_ssa_def *split_load_const(nir_load_const_instr *lc);

   bool filter(const nir_instr *instr) const override;
   nir_ssa_def *lower(nir_instr *instr) override;

   VarMap m_varmap;
   vector<nir_variable*> m_old_vars;
   vector<nir_instr *> m_old_stores;
};


bool
LowerSplit64BitVar::filter(const nir_instr *instr) const
{
   switch (instr->type) {
   case  nir_instr_type_intrinsic: {
      auto intr = nir_instr_as_intrinsic(instr);

      switch (intr->intrinsic) {
      case nir_intrinsic_load_deref:
      case nir_intrinsic_load_uniform:
      case nir_intrinsic_load_input:
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_load_ssbo:
         if (nir_dest_bit_size(intr->dest) != 64)
            return false;
         return nir_dest_num_components(intr->dest) >= 3;
      case nir_intrinsic_store_output:
         if (nir_src_bit_size(intr->src[0]) != 64)
            return false;
         return nir_src_num_components(intr->src[0]) >= 3;
      case nir_intrinsic_store_deref:
         if (nir_src_bit_size(intr->src[1]) != 64)
            return false;
         return nir_src_num_components(intr->src[1]) >= 3;
      default:
         return false;
      }
   }
   case  nir_instr_type_alu: {
      auto alu = nir_instr_as_alu(instr);
      switch (alu->op) {
      case nir_op_bcsel:
         if (nir_dest_num_components(alu->dest.dest) < 3)
            return false;
         return nir_dest_bit_size(alu->dest.dest) == 64;
      case nir_op_bany_fnequal3:
      case nir_op_bany_fnequal4:
      case nir_op_ball_fequal3:
      case nir_op_ball_fequal4:
      case nir_op_bany_inequal3:
      case nir_op_bany_inequal4:
      case nir_op_ball_iequal3:
      case nir_op_ball_iequal4:
      case nir_op_fdot3:
      case nir_op_fdot4:
         return nir_src_bit_size(alu->src[1].src) == 64;
      default:
         return false;
      }
   }
   case nir_instr_type_load_const: {
      auto lc = nir_instr_as_load_const(instr);
      if (lc->def.bit_size != 64)
         return false;
      return lc->def.num_components >= 3;
   }
   default:
      return false;
   }
}

nir_ssa_def *
LowerSplit64BitVar::merge_64bit_loads(nir_ssa_def *load1,
                                      nir_ssa_def *load2, bool out_is_vec3)
{
   if (out_is_vec3)
      return nir_vec3(b, nir_channel(b, load1, 0),
                      nir_channel(b, load1, 1),
                      nir_channel(b, load2, 0));
   else
      return nir_vec4(b, nir_channel(b, load1, 0),
                      nir_channel(b, load1, 1),
                      nir_channel(b, load2, 0),
                      nir_channel(b, load2, 1));
}

LowerSplit64BitVar::~LowerSplit64BitVar()
{
   for(auto&& v: m_old_vars)
      exec_node_remove(&v->node);

   for(auto&& v: m_old_stores)
      nir_instr_remove(v);
}

nir_ssa_def *
LowerSplit64BitVar::split_double_store_deref(nir_intrinsic_instr *intr)
{
   auto deref = nir_instr_as_deref(intr->src[0].ssa->parent_instr);
   if (deref->deref_type == nir_deref_type_var)
      return split_store_deref_var(intr, deref);
   else if (deref->deref_type == nir_deref_type_array)
      return split_store_deref_array(intr, deref);
   else {
      unreachable("only splitting of stores to vars and arrays is supported");
   }
}

nir_ssa_def *
LowerSplit64BitVar::split_double_load_deref(nir_intrinsic_instr *intr)
{
   auto deref = nir_instr_as_deref(intr->src[0].ssa->parent_instr);
   if (deref->deref_type == nir_deref_type_var)
      return split_load_deref_var(intr);
   else if (deref->deref_type == nir_deref_type_array)
      return split_load_deref_array(intr, deref->arr.index);
   else {
      unreachable(0 && "only splitting of loads from vars and arrays is supported");
   }
   m_old_stores.push_back(&intr->instr);
}

nir_ssa_def *
LowerSplit64BitVar::split_load_deref_array(nir_intrinsic_instr *intr, nir_src& index)
{
   auto old_var = nir_intrinsic_get_var(intr, 0);
   unsigned old_components = old_var->type->without_array()->components();

   assert(old_components > 2 && old_components <= 4);

   auto vars = get_var_pair(old_var);

   auto deref1 = nir_build_deref_var(b, vars.first);
   auto deref_array1 = nir_build_deref_array(b, deref1, nir_ssa_for_src(b, index, 1));
   auto load1 = nir_build_load_deref(b, 2, 64, &deref_array1->dest.ssa, (enum gl_access_qualifier)0);

   auto deref2 = nir_build_deref_var(b, vars.second);
   auto deref_array2 = nir_build_deref_array(b, deref2, nir_ssa_for_src(b, index, 1));

   auto load2 = nir_build_load_deref(b, old_components - 2, 64, &deref_array2->dest.ssa, (enum gl_access_qualifier)0);

   return merge_64bit_loads(load1, load2, old_components == 3);
}

nir_ssa_def *
LowerSplit64BitVar::split_store_deref_array(nir_intrinsic_instr *intr, nir_deref_instr *deref)
{
   auto old_var = nir_intrinsic_get_var(intr, 0);
   unsigned old_components = old_var->type->without_array()->components();

   assert(old_components > 2 && old_components <= 4);

   auto src_xy = nir_channels(b, intr->src[1].ssa, 3);

   auto vars = get_var_pair(old_var);

   auto deref1 = nir_build_deref_var(b, vars.first);
   auto deref_array1 = nir_build_deref_array(b, deref1, nir_ssa_for_src(b, deref->arr.index, 1));

   nir_build_store_deref(b, &deref_array1->dest.ssa, src_xy, 3);

   auto deref2 = nir_build_deref_var(b, vars.second);
   auto deref_array2 = nir_build_deref_array(b, deref2, nir_ssa_for_src(b, deref->arr.index, 1));

   if (old_components == 3)
      nir_build_store_deref(b, &deref_array2->dest.ssa, nir_channel(b, intr->src[1].ssa, 2), 1);
   else
      nir_build_store_deref(b, &deref_array2->dest.ssa, nir_channels(b, intr->src[1].ssa, 0xc), 3);

   return NIR_LOWER_INSTR_PROGRESS_REPLACE;
}

nir_ssa_def *
LowerSplit64BitVar::split_store_deref_var(nir_intrinsic_instr *intr, nir_deref_instr *deref)
{
   auto old_var = nir_intrinsic_get_var(intr, 0);
   unsigned old_components = old_var->type->without_array()->components();

   assert(old_components > 2 && old_components <= 4);

   auto src_xy = nir_channels(b, intr->src[1].ssa, 3);

   auto vars = get_var_pair(old_var);

   auto deref1 = nir_build_deref_var(b, vars.first);
   nir_build_store_deref(b, &deref1->dest.ssa, src_xy, 3);

   auto deref2 = nir_build_deref_var(b, vars.second);
   if (old_components == 3)
      nir_build_store_deref(b, &deref2->dest.ssa, nir_channel(b, intr->src[1].ssa, 2), 1);
   else
      nir_build_store_deref(b, &deref2->dest.ssa, nir_channels(b, intr->src[1].ssa, 0xc), 3);

   return NIR_LOWER_INSTR_PROGRESS_REPLACE;
}

nir_ssa_def *
LowerSplit64BitVar::split_load_deref_var(nir_intrinsic_instr *intr)
{
   auto old_var = nir_intrinsic_get_var(intr, 0);
   auto vars = get_var_pair(old_var);
   unsigned old_components = old_var->type->components();

   nir_deref_instr *deref1 = nir_build_deref_var(b, vars.first);
   auto *load1 = nir_load_deref(b, deref1);

   nir_deref_instr *deref2 = nir_build_deref_var(b, vars.second);
   deref2->type = vars.second->type;

   auto *load2 = nir_load_deref(b, deref2);

   return merge_64bit_loads(load1, load2, old_components == 3);
}

LowerSplit64BitVar::VarSplit
LowerSplit64BitVar::get_var_pair(nir_variable *old_var)
{
   auto split_vars = m_varmap.find(old_var->data.driver_location);

   assert(old_var->type->without_array()->components() > 2);

   if (split_vars == m_varmap.end()) {
      auto var1 = nir_variable_clone(old_var, b->shader);
      auto var2 = nir_variable_clone(old_var, b->shader);

      var1->type = glsl_dvec_type(2);
      var2->type = glsl_dvec_type(old_var->type->without_array()->components() - 2);

      if (old_var->type->is_array()) {
         var1->type = glsl_array_type(var1->type, old_var->type->array_size(), 0);
         var2->type = glsl_array_type(var2->type, old_var->type->array_size(), 0);
      }

      if (old_var->data.mode == nir_var_shader_in ||
          old_var->data.mode == nir_var_shader_out) {
         ++var2->data.driver_location;
         ++var2->data.location;
         nir_shader_add_variable(b->shader, var1);
         nir_shader_add_variable(b->shader, var2);
      } else if (old_var->data.mode == nir_var_function_temp) {
         exec_list_push_tail(&b->impl->locals, &var1->node);
         exec_list_push_tail(&b->impl->locals, &var2->node);
      }

      m_varmap[old_var->data.driver_location] = make_pair(var1, var2);
   }
   return m_varmap[old_var->data.driver_location];
}


nir_ssa_def *
LowerSplit64BitVar::split_double_load(nir_intrinsic_instr *load1)
{
   unsigned old_components = nir_dest_num_components(load1->dest);
   auto load2 = nir_instr_as_intrinsic(nir_instr_clone(b->shader, &load1->instr));
   nir_io_semantics sem = nir_intrinsic_io_semantics(load1);

   load1->dest.ssa.num_components = 2;
   sem.num_slots = 1;
   nir_intrinsic_set_io_semantics(load1, sem);

   load2->dest.ssa.num_components = old_components - 2;
   sem.location += 1;
   nir_intrinsic_set_io_semantics(load2, sem);
   nir_intrinsic_set_base(load2, nir_intrinsic_base(load1) + 1);
   nir_builder_instr_insert(b, &load2->instr);

   return merge_64bit_loads(&load1->dest.ssa, &load2->dest.ssa, old_components == 3);
}


nir_ssa_def *
LowerSplit64BitVar::split_store_output(nir_intrinsic_instr *store1)
{
   auto src = store1->src[0];
   unsigned old_components = nir_src_num_components(src);
   nir_io_semantics sem = nir_intrinsic_io_semantics(store1);

   auto store2 = nir_instr_as_intrinsic(nir_instr_clone(b->shader, &store1->instr));
   auto src1 = nir_channels(b, src.ssa, 3);
   auto src2 = nir_channels(b, src.ssa, old_components == 3 ? 4 : 0xc);

   nir_instr_rewrite_src(&store1->instr, &src, nir_src_for_ssa(src1));
   nir_intrinsic_set_write_mask(store1, 3);

   nir_instr_rewrite_src(&store2->instr, &src, nir_src_for_ssa(src2));
   nir_intrinsic_set_write_mask(store2, old_components == 3 ? 1 : 3);

   sem.num_slots = 1;
   nir_intrinsic_set_io_semantics(store1, sem);

   sem.location += 1;
   nir_intrinsic_set_io_semantics(store2, sem);
   nir_intrinsic_set_base(store2, nir_intrinsic_base(store1));

   nir_builder_instr_insert(b, &store2->instr);
   return NIR_LOWER_INSTR_PROGRESS;
}


nir_ssa_def *
LowerSplit64BitVar::split_double_load_uniform(nir_intrinsic_instr *intr)
{
   unsigned second_components = nir_dest_num_components(intr->dest) - 2;
   nir_intrinsic_instr *load2 = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_uniform);
   load2->src[0] = nir_src_for_ssa(nir_iadd_imm(b, intr->src[0].ssa, 1));
   nir_intrinsic_set_dest_type(load2, nir_intrinsic_dest_type(intr));
   nir_intrinsic_set_base(load2, nir_intrinsic_base(intr));
   nir_intrinsic_set_range(load2, nir_intrinsic_range(intr));
   load2->num_components = second_components;

   nir_ssa_dest_init(&load2->instr, &load2->dest, second_components, 64, nullptr);
   nir_builder_instr_insert(b, &load2->instr);

   intr->dest.ssa.num_components = intr->num_components = 2;

   if (second_components == 1)
      return nir_vec3(b, nir_channel(b, &intr->dest.ssa, 0),
                      nir_channel(b, &intr->dest.ssa, 1),
                      nir_channel(b, &load2->dest.ssa, 0));
   else
      return nir_vec4(b, nir_channel(b, &intr->dest.ssa, 0),
                      nir_channel(b, &intr->dest.ssa, 1),
                      nir_channel(b, &load2->dest.ssa, 0),
                      nir_channel(b, &load2->dest.ssa, 1));
}

nir_ssa_def *
LowerSplit64BitVar::split_double_load_ssbo(nir_intrinsic_instr *intr)
{
   unsigned second_components = nir_dest_num_components(intr->dest) - 2;
   nir_intrinsic_instr *load2 = nir_instr_as_intrinsic(nir_instr_clone(b->shader, &intr->instr));

   auto new_src0 = nir_src_for_ssa(nir_iadd_imm(b, intr->src[0].ssa, 1));
   nir_instr_rewrite_src(&load2->instr, &load2->src[0], new_src0);
   load2->num_components = second_components;
   nir_ssa_dest_init(&load2->instr, &load2->dest, second_components, 64, nullptr);

   nir_intrinsic_set_dest_type(load2, nir_intrinsic_dest_type(intr));
   nir_builder_instr_insert(b, &load2->instr);

   intr->dest.ssa.num_components = intr->num_components = 2;

   return merge_64bit_loads(&intr->dest.ssa, &load2->dest.ssa, second_components == 1);
}


nir_ssa_def *
LowerSplit64BitVar::split_double_load_ubo(nir_intrinsic_instr *intr)
{
   unsigned second_components = nir_dest_num_components(intr->dest) - 2;
   nir_intrinsic_instr *load2 = nir_instr_as_intrinsic(nir_instr_clone(b->shader, &intr->instr));
   load2->src[0] = intr->src[0];
   load2->src[1] = nir_src_for_ssa(nir_iadd_imm(b, intr->src[1].ssa, 16));
   nir_intrinsic_set_range_base(load2, nir_intrinsic_range_base(intr) + 16);
   nir_intrinsic_set_range(load2, nir_intrinsic_range(intr));
   nir_intrinsic_set_access(load2, nir_intrinsic_access(intr));
   nir_intrinsic_set_align_mul(load2, nir_intrinsic_align_mul(intr));
   nir_intrinsic_set_align_offset(load2, nir_intrinsic_align_offset(intr) + 16);

   load2->num_components = second_components;

   nir_ssa_dest_init(&load2->instr, &load2->dest, second_components, 64, nullptr);
   nir_builder_instr_insert(b, &load2->instr);

   intr->dest.ssa.num_components = intr->num_components = 2;

   return merge_64bit_loads(&intr->dest.ssa, &load2->dest.ssa, second_components == 1);
}

nir_ssa_def *
LowerSplit64BitVar::split_reduction(nir_ssa_def *src[2][2], nir_op op1, nir_op op2, nir_op reduction)
{
   auto cmp0 = nir_build_alu(b, op1, src[0][0], src[0][1], nullptr, nullptr);
   auto cmp1 = nir_build_alu(b, op2, src[1][0], src[1][1], nullptr, nullptr);
   return nir_build_alu(b, reduction, cmp0, cmp1, nullptr, nullptr);
}

nir_ssa_def *
LowerSplit64BitVar::split_reduction3(nir_alu_instr *alu,
                                     nir_op op1, nir_op op2, nir_op reduction)
{
   nir_ssa_def *src[2][2];

   src[0][0] = nir_channels(b, nir_ssa_for_src(b, alu->src[0].src, 2), 3);
   src[0][1] = nir_channels(b, nir_ssa_for_src(b, alu->src[1].src, 2), 3);

   src[1][0]  = nir_channel(b, nir_ssa_for_src(b, alu->src[0].src, 3), 2);
   src[1][1]  = nir_channel(b, nir_ssa_for_src(b, alu->src[1].src, 3), 2);

   return split_reduction(src, op1, op2, reduction);
}

nir_ssa_def *
LowerSplit64BitVar::split_reduction4(nir_alu_instr *alu,
                                     nir_op op1, nir_op op2, nir_op reduction)
{
   nir_ssa_def *src[2][2];

   src[0][0] = nir_channels(b, nir_ssa_for_src(b, alu->src[0].src, 2), 3);
   src[0][1] = nir_channels(b, nir_ssa_for_src(b, alu->src[1].src, 2), 3);

   src[1][0]  = nir_channels(b, nir_ssa_for_src(b, alu->src[0].src, 4), 0xc);
   src[1][1]  = nir_channels(b, nir_ssa_for_src(b, alu->src[1].src, 4), 0xc);

   return split_reduction(src, op1, op2, reduction);
}

nir_ssa_def *
LowerSplit64BitVar::split_bcsel(nir_alu_instr *alu)
{
   static nir_ssa_def *dest[4];
   for (unsigned i = 0; i < nir_dest_num_components(alu->dest.dest); ++i) {
      dest[i] = nir_bcsel(b,
                          nir_channel(b, alu->src[0].src.ssa, i),
                          nir_channel(b, alu->src[1].src.ssa, i),
                          nir_channel(b, alu->src[2].src.ssa, i));
   }
   return nir_vec(b, dest, nir_dest_num_components(alu->dest.dest));
}

nir_ssa_def *
LowerSplit64BitVar::split_load_const(nir_load_const_instr *lc)
{
   nir_ssa_def *ir[4];
   for (unsigned i = 0; i < lc->def.num_components; ++i)
      ir[i] = nir_imm_double(b, lc->value[i].f64);

   return nir_vec(b, ir, lc->def.num_components);
}

nir_ssa_def *
LowerSplit64BitVar::lower(nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_intrinsic: {
      auto intr = nir_instr_as_intrinsic(instr);
      switch (intr->intrinsic) {
      case nir_intrinsic_load_deref:
         return this->split_double_load_deref(intr);
      case nir_intrinsic_load_uniform:
         return split_double_load_uniform(intr);
      case nir_intrinsic_load_ubo:
         return split_double_load_ubo(intr);
      case nir_intrinsic_load_ssbo:
         return split_double_load_ssbo(intr);
      case nir_intrinsic_load_input:
         return split_double_load(intr);
      case nir_intrinsic_store_output:
         return split_store_output(intr);
      case nir_intrinsic_store_deref:
         return split_double_store_deref(intr);
      default:
         assert(0);
      }
   }
   case  nir_instr_type_alu: {
      auto alu = nir_instr_as_alu(instr);
      nir_print_instr(instr, stderr);
      fprintf(stderr, "\n");
      switch (alu->op) {
      case nir_op_bany_fnequal3:
         return split_reduction3(alu, nir_op_bany_fnequal2, nir_op_fneu, nir_op_ior);
      case nir_op_ball_fequal3:
         return split_reduction3(alu, nir_op_ball_fequal2, nir_op_feq, nir_op_iand);
      case nir_op_bany_inequal3:
         return split_reduction3(alu, nir_op_bany_inequal2, nir_op_ine, nir_op_ior);
      case nir_op_ball_iequal3:
         return split_reduction3(alu, nir_op_ball_iequal2, nir_op_ieq, nir_op_iand);
      case nir_op_fdot3:
         return split_reduction3(alu, nir_op_fdot2, nir_op_fmul, nir_op_fadd);
      case nir_op_bany_fnequal4:
         return split_reduction4(alu, nir_op_bany_fnequal2, nir_op_bany_fnequal2, nir_op_ior);
      case nir_op_ball_fequal4:
         return split_reduction4(alu, nir_op_ball_fequal2, nir_op_ball_fequal2, nir_op_iand);
      case nir_op_bany_inequal4:
         return split_reduction4(alu, nir_op_bany_inequal2, nir_op_bany_inequal2, nir_op_ior);
      case nir_op_ball_iequal4:
         return split_reduction4(alu, nir_op_bany_fnequal2, nir_op_bany_fnequal2, nir_op_ior);
      case nir_op_fdot4:
         return split_reduction4(alu, nir_op_fdot2, nir_op_fdot2, nir_op_fadd);
      case nir_op_bcsel:
         return split_bcsel(alu);
      default:
         assert(0);
      }
   }
   case nir_instr_type_load_const: {
      auto lc = nir_instr_as_load_const(instr);
      return split_load_const(lc);
   }
   default:
      assert(0);
   }
   return nullptr;
}

/* Split 64 bit instruction so that at most two 64 bit components are
 * used in one instruction */

bool
r600_nir_split_64bit_io(nir_shader *sh)
{
   return LowerSplit64BitVar().run(sh);
}

/* */
class Lower64BitToVec2 : public NirLowerInstruction {

private:
   bool filter(const nir_instr *instr) const override;
   nir_ssa_def *lower(nir_instr *instr) override;

   nir_ssa_def *load_deref_64_to_vec2(nir_intrinsic_instr *intr);
   nir_ssa_def *load_uniform_64_to_vec2(nir_intrinsic_instr *intr);
   nir_ssa_def *load_ssbo_64_to_vec2(nir_intrinsic_instr *intr);
   nir_ssa_def *load_64_to_vec2(nir_intrinsic_instr *intr);
   nir_ssa_def *store_64_to_vec2(nir_intrinsic_instr *intr);
};

bool
Lower64BitToVec2::filter(const nir_instr *instr) const
{
   switch (instr->type) {
   case nir_instr_type_intrinsic:  {
      auto intr = nir_instr_as_intrinsic(instr);

      switch (intr->intrinsic) {
      case nir_intrinsic_load_deref:
      case nir_intrinsic_load_input:
      case nir_intrinsic_load_uniform:
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_load_ubo_vec4:
      case nir_intrinsic_load_ssbo:
         return nir_dest_bit_size(intr->dest) == 64;
      case nir_intrinsic_store_deref: {
         if (nir_src_bit_size(intr->src[1]) == 64)
            return true;
         auto var = nir_intrinsic_get_var(intr, 0);
         if (var->type->without_array()->bit_size() == 64)
            return true;
         return (var->type->without_array()->components() != intr->num_components);
      }
      default:
         return false;
      }
   }
   case nir_instr_type_alu: {
      auto alu = nir_instr_as_alu(instr);
      return nir_dest_bit_size(alu->dest.dest) == 64;
   }
   case nir_instr_type_phi: {
      auto phi = nir_instr_as_phi(instr);
      return nir_dest_bit_size(phi->dest) == 64;
   }
   case nir_instr_type_load_const:  {
      auto lc = nir_instr_as_load_const(instr);
      return lc->def.bit_size == 64;
   }
   case nir_instr_type_ssa_undef:  {
      auto undef = nir_instr_as_ssa_undef(instr);
      return undef->def.bit_size == 64;
   }
   default:
      return false;
   }
}

nir_ssa_def *
Lower64BitToVec2::lower(nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_intrinsic:  {
      auto intr = nir_instr_as_intrinsic(instr);
      switch (intr->intrinsic) {
      case nir_intrinsic_load_deref:
         return load_deref_64_to_vec2(intr);
      case nir_intrinsic_load_uniform:
         return load_uniform_64_to_vec2(intr);
      case nir_intrinsic_load_ssbo:
         return load_ssbo_64_to_vec2(intr);
      case nir_intrinsic_load_input:
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_load_ubo_vec4:
         return load_64_to_vec2(intr);
      case nir_intrinsic_store_deref:
         return store_64_to_vec2(intr);
      default:

         return nullptr;
      }
   }
   case nir_instr_type_alu: {
      auto alu = nir_instr_as_alu(instr);
      alu->dest.dest.ssa.bit_size = 32;
      alu->dest.dest.ssa.num_components *= 2;
      alu->dest.write_mask = (1 << alu->dest.dest.ssa.num_components) - 1;
      switch (alu->op) {
      case nir_op_pack_64_2x32_split:
         alu->op = nir_op_vec2;
         break;
      case nir_op_pack_64_2x32:
         alu->op = nir_op_mov;
         break;
      case nir_op_vec2:
         return nir_vec4(b,
                         nir_channel(b, alu->src[0].src.ssa, 0),
                         nir_channel(b, alu->src[0].src.ssa, 1),
                         nir_channel(b, alu->src[1].src.ssa, 0),
                         nir_channel(b, alu->src[1].src.ssa, 1));
      default:
         return NULL;
      }
      return NIR_LOWER_INSTR_PROGRESS;
   }
   case nir_instr_type_phi: {
      auto phi = nir_instr_as_phi(instr);
      phi->dest.ssa.bit_size = 32;
      phi->dest.ssa.num_components = 2;
      return NIR_LOWER_INSTR_PROGRESS;
   }
   case nir_instr_type_load_const:  {
      auto lc = nir_instr_as_load_const(instr);
      assert(lc->def.num_components < 3);
      nir_const_value val[4] = {0};
      for (uint i = 0; i < lc->def.num_components; ++i) {
         uint64_t v = lc->value[i].u64;
         val[0].u32 = v & 0xffffffff;
         val[1].u32 = (v >> 32) & 0xffffffff;
      }

      return nir_build_imm(b, 2 * lc->def.num_components, 32, val);
   }
   case nir_instr_type_ssa_undef:  {
      auto undef = nir_instr_as_ssa_undef(instr);
      undef->def.num_components *= 2;
      undef->def.bit_size = 32;
      return NIR_LOWER_INSTR_PROGRESS;
   }
   default:
      return nullptr;
   }

}


nir_ssa_def *
Lower64BitToVec2::load_deref_64_to_vec2(nir_intrinsic_instr *intr)
{
   auto deref = nir_instr_as_deref(intr->src[0].ssa->parent_instr);
   auto var = nir_intrinsic_get_var(intr, 0);
   unsigned components = var->type->without_array()->components();
   if (var->type->without_array()->bit_size() == 64) {
      components *= 2;
      if (deref->deref_type == nir_deref_type_var) {
         var->type = glsl_vec_type(components);
      } else if (deref->deref_type == nir_deref_type_array) {

         var->type = glsl_array_type(glsl_vec_type(components),
                                     var->type->array_size(), 0);

      } else {
         nir_print_shader(b->shader, stderr);
         assert(0 && "Only lowring of var and array derefs supported\n");
      }
   }
   deref->type = var->type;
   if (deref->deref_type == nir_deref_type_array) {
      auto deref_array = nir_instr_as_deref(deref->parent.ssa->parent_instr);
      deref_array->type = var->type;
      deref->type = deref_array->type->without_array();
   }

   intr->num_components = components;
   intr->dest.ssa.bit_size = 32;
   intr->dest.ssa.num_components = components;
   return NIR_LOWER_INSTR_PROGRESS;
}

nir_ssa_def *
Lower64BitToVec2::store_64_to_vec2(nir_intrinsic_instr *intr)
{
   auto deref = nir_instr_as_deref(intr->src[0].ssa->parent_instr);
   auto var = nir_intrinsic_get_var(intr, 0);

   unsigned components = var->type->without_array()->components();
   unsigned wrmask = nir_intrinsic_write_mask(intr);
   if (var->type->without_array()->bit_size() == 64) {
      components *= 2;
      if (deref->deref_type == nir_deref_type_var) {
         var->type = glsl_vec_type(components);
      } else if (deref->deref_type == nir_deref_type_array) {
         var->type = glsl_array_type(glsl_vec_type(components),
                                     var->type->array_size(), 0);
      } else {
            nir_print_shader(b->shader, stderr);
            assert(0 && "Only lowring of var and array derefs supported\n");
      }
   }
   deref->type = var->type;
   if (deref->deref_type == nir_deref_type_array) {
      auto deref_array = nir_instr_as_deref(deref->parent.ssa->parent_instr);
      deref_array->type = var->type;
      deref->type = deref_array->type->without_array();
   }
   intr->num_components = components;
   nir_intrinsic_set_write_mask(intr, wrmask == 1 ? 3 : 0xf);
   return NIR_LOWER_INSTR_PROGRESS;
}


nir_ssa_def *
Lower64BitToVec2::load_uniform_64_to_vec2(nir_intrinsic_instr *intr)
{
   intr->num_components *= 2;
   intr->dest.ssa.bit_size = 32;
   intr->dest.ssa.num_components *= 2;
   nir_intrinsic_set_dest_type(intr, nir_type_float32);
   return NIR_LOWER_INSTR_PROGRESS;
}

nir_ssa_def *
Lower64BitToVec2::load_64_to_vec2(nir_intrinsic_instr *intr)
{
   intr->num_components *= 2;
   intr->dest.ssa.bit_size = 32;
   intr->dest.ssa.num_components *= 2;
   nir_intrinsic_set_component(intr, nir_intrinsic_component(intr) * 2);
   return NIR_LOWER_INSTR_PROGRESS;
}

nir_ssa_def *
Lower64BitToVec2::load_ssbo_64_to_vec2(nir_intrinsic_instr *intr)
{
   intr->num_components *= 2;
   intr->dest.ssa.bit_size = 32;
   intr->dest.ssa.num_components *= 2;
   return NIR_LOWER_INSTR_PROGRESS;
}

static bool store_64bit_intr(nir_src *src, void *state)
{
   bool *s = (bool *)state;
   *s = nir_src_bit_size(*src) == 64;
   return !*s;
}

static bool double2vec2(nir_src *src, void *state)
{
   if (nir_src_bit_size(*src) != 64)
      return true;

   assert(src->is_ssa);
   src->ssa->bit_size = 32;
   src->ssa->num_components *= 2;
   return true;
}

bool
r600_nir_64_to_vec2(nir_shader *sh)
{
   vector<nir_instr*> intr64bit;
   nir_foreach_function(function, sh) {
      if (function->impl) {
         nir_builder b;
         nir_builder_init(&b, function->impl);

         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               switch (instr->type) {
               case nir_instr_type_alu: {
                  bool success = false;
                  nir_foreach_src(instr, store_64bit_intr, &success);
                  if (success)
                     intr64bit.push_back(instr);
                  break;
               }
               case nir_instr_type_intrinsic: {
                  auto ir = nir_instr_as_intrinsic(instr);
                  switch (ir->intrinsic) {
                  case nir_intrinsic_store_output:
                  case nir_intrinsic_store_ssbo: {
                     bool success = false;
                     nir_foreach_src(instr, store_64bit_intr, &success);
                     if (success) {
                        auto wm = nir_intrinsic_write_mask(ir);
                        nir_intrinsic_set_write_mask(ir, (wm == 1) ? 3 : 0xf);
                        ir->num_components *= 2;
                     }
                     break;
                  }
                  default:
                     ;
                  }
               }
               default:
                  ;
               }
            }
         }
      }
   }

   bool result = Lower64BitToVec2().run(sh);

   if (result || !intr64bit.empty()) {

      for(auto&& instr: intr64bit) {
         if (instr->type == nir_instr_type_alu) {
            auto alu = nir_instr_as_alu(instr);
            auto alu_info = nir_op_infos[alu->op];
            for (unsigned i = 0; i < alu_info.num_inputs; ++i) {
               int swizzle[NIR_MAX_VEC_COMPONENTS] = {0};
               for (unsigned k = 0; k < NIR_MAX_VEC_COMPONENTS / 2; k++) {
                  if (!nir_alu_instr_channel_used(alu, i, k)) {
                     continue;
                  }

                  switch (alu->op) {
                  case nir_op_unpack_64_2x32_split_x:
                     swizzle[2 * k] = alu->src[i].swizzle[k] * 2;
                     alu->op = nir_op_mov;
                     break;
                  case nir_op_unpack_64_2x32_split_y:
                     swizzle[2 * k] = alu->src[i].swizzle[k] * 2 + 1;
                     alu->op = nir_op_mov;
                     break;
                  case nir_op_unpack_64_2x32:
                     alu->op = nir_op_mov;
                     break;
                  case nir_op_bcsel:
                     if (i == 0) {
                        swizzle[2 * k] = swizzle[2 * k + 1] = alu->src[i].swizzle[k] * 2;
                        break;
                     }
                     /* fallthrough */
                  default:
                     swizzle[2 * k] = alu->src[i].swizzle[k] * 2;
                     swizzle[2 * k + 1] = alu->src[i].swizzle[k] * 2 + 1;
                  }
               }
               for (unsigned k = 0; k < NIR_MAX_VEC_COMPONENTS; ++k) {
                  alu->src[i].swizzle[k] = swizzle[k];
               }
            }
         } else
            nir_foreach_src(instr, double2vec2, nullptr);
      }
      result = true;
   }

   return result;
}

using std::map;
using std::vector;
using std::pair;

class StoreMerger {
public:
   StoreMerger(nir_shader *shader);
   void collect_stores();
   bool combine();
   void combine_one_slot(vector<nir_intrinsic_instr*>& stores);

   using StoreCombos = map<unsigned, vector<nir_intrinsic_instr*>>;

   StoreCombos m_stores;
   nir_shader *sh;
};

StoreMerger::StoreMerger(nir_shader *shader):
   sh(shader)
{
}


void StoreMerger::collect_stores()
{
   unsigned vertex = 0;
   nir_foreach_function(function, sh) {
      if (function->impl) {
         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type != nir_instr_type_intrinsic)
                  continue;

               auto ir = nir_instr_as_intrinsic(instr);
               if (ir->intrinsic == nir_intrinsic_emit_vertex ||
                   ir->intrinsic == nir_intrinsic_emit_vertex_with_counter) {
                  ++vertex;
                  continue;
               }
               if (ir->intrinsic != nir_intrinsic_store_output)
                  continue;

               unsigned index = nir_intrinsic_base(ir) + 64 * vertex +
                                8 * 64 * nir_intrinsic_io_semantics(ir).gs_streams;
               m_stores[index].push_back(ir);
            }
         }
      }
   }
}

bool StoreMerger::combine()
{
   bool progress = false;
   for(auto&& i : m_stores) {
      if (i.second.size() < 2)
         continue;

      combine_one_slot(i.second);
      progress = true;
   }
   return progress;
}

void StoreMerger::combine_one_slot(vector<nir_intrinsic_instr*>& stores)
{
   nir_ssa_def *srcs[4] = {nullptr};

   nir_builder b;
   nir_builder_init(&b, nir_shader_get_entrypoint(sh));
   auto last_store = *stores.rbegin();

   b.cursor = nir_before_instr(&last_store->instr);

   unsigned comps = 0;
   unsigned writemask = 0;
   unsigned first_comp = 4;
   for (auto&& store : stores) {
      int cmp = nir_intrinsic_component(store);
      for (unsigned i = 0; i < nir_src_num_components(store->src[0]); ++i, ++comps) {
         unsigned out_comp = i + cmp;
         srcs[out_comp] = nir_channel(&b, store->src[0].ssa, i);
         writemask |= 1 << out_comp;
         if (first_comp > out_comp)
            first_comp = out_comp;
      }
   }

   auto new_src = nir_vec(&b, srcs, comps);

   nir_instr_rewrite_src(&last_store->instr, &last_store->src[0], nir_src_for_ssa(new_src));
   last_store->num_components = comps;
   nir_intrinsic_set_component(last_store, first_comp);
   nir_intrinsic_set_write_mask(last_store, writemask);

   for (auto i = stores.begin(); i != stores.end() - 1; ++i)
      nir_instr_remove(&(*i)->instr);
}

bool r600_merge_vec2_stores(nir_shader *shader)
{
   r600::StoreMerger merger(shader);
   merger.collect_stores();
   return merger.combine();
}

} // end namespace r600


