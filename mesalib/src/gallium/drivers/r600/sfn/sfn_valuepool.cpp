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

#include "sfn_debug.h"
#include "sfn_value_gpr.h"
#include "sfn_valuepool.h"

#include <iostream>
#include <queue>

namespace r600 {

using std::vector;
using std::pair;
using std::make_pair;
using std::queue;

ValuePool::ValuePool():
   m_next_register_index(0),
   current_temp_reg_index(0),
   next_temp_reg_comp(4)
{
}

PValue ValuePool::m_undef = Value::zero;

GPRVector ValuePool::vec_from_nir(const nir_dest& dst, int num_components)
{
   std::array<PValue, 4> result;
   for (int i = 0; i < 4; ++i)
      result[i] = from_nir(dst, i < num_components ? i : 7);
   return GPRVector(result);
}

std::vector<PValue> ValuePool::varvec_from_nir(const nir_dest& dst, int num_components)
{
   std::vector<PValue> result(num_components);
   for (int i = 0; i < num_components; ++i)
      result[i] = from_nir(dst, i);
   return result;
}


std::vector<PValue> ValuePool::varvec_from_nir(const nir_src& src, int num_components)
{
   std::vector<PValue> result(num_components);
   int i;
   for (i = 0; i < num_components; ++i)
      result[i] = from_nir(src, i);

   return result;
}


PValue ValuePool::from_nir(const nir_src& v, unsigned component, unsigned swizzled)
{
   sfn_log << SfnLog::reg << "Search " << (v.is_ssa ? "ssa_reg " : "reg ")
           << (v.is_ssa ? v.ssa->index : v.reg.reg->index);

   if (!v.is_ssa) {
      int idx = lookup_register_index(v);
      sfn_log << SfnLog::reg << "  -> got index " <<  idx << "\n";
      if (idx >= 0) {
         auto reg = lookup_register(idx, swizzled, false);
         if (reg) {
            if (reg->type() == Value::gpr_vector) {
               auto& array = static_cast<GPRArray&>(*reg);
               reg = array.get_indirect(v.reg.base_offset,
                                        v.reg.indirect ?
                                           from_nir(*v.reg.indirect, 0, 0) : nullptr,
                                        component);
            }
            return reg;
         }
      }
      assert(0 && "local registers should always be found");
   }

   unsigned index = v.ssa->index;
   /* For undefs we use zero and let ()yet to be implemeneted dce deal with it */
   if (m_ssa_undef.find(index) != m_ssa_undef.end())
      return Value::zero;


   int idx = lookup_register_index(v);
   sfn_log << SfnLog::reg << "  -> got index " <<  idx << "\n";
   if (idx >= 0) {
      auto reg = lookup_register(idx, swizzled, false);
      if (reg)
         return reg;
   }

   auto literal_val = nir_src_as_const_value(v);
   if (literal_val) {
      assert(v.is_ssa);
      switch (v.ssa->bit_size) {
      case 1:
         return PValue(new LiteralValue(literal_val[swizzled].b ? 0xffffffff : 0, component));
      case 32:
         return literal(literal_val[swizzled].u32);
      default:
         sfn_log << SfnLog::reg << "Unsupported bit size " << v.ssa->bit_size
                 << " fall back to 32\n";
         return PValue(new LiteralValue(literal_val[swizzled].u32, component));
      }
   }

   return PValue();
}

PValue ValuePool::from_nir(const nir_src& v, unsigned component)
{
   return from_nir(v, component, component);
}

PValue ValuePool::from_nir(const nir_tex_src &v, unsigned component)
{
   return from_nir(v.src, component, component);
}

PValue ValuePool::from_nir(const nir_alu_src &v, unsigned component)
{
   return from_nir(v.src, component, v.swizzle[component]);
}

PGPRValue ValuePool::get_temp_register(int channel)
{
   /* Skip to next register to get the channel we want */
   if (channel >= 0) {
      if (next_temp_reg_comp <= channel)
         next_temp_reg_comp = channel;
      else
         next_temp_reg_comp = 4;
   }

   if (next_temp_reg_comp > 3) {
      current_temp_reg_index = allocate_temp_register();
      next_temp_reg_comp = 0;
   }
   return std::make_shared<GPRValue>(current_temp_reg_index, next_temp_reg_comp++);
}

GPRVector ValuePool::get_temp_vec4()
{
   int sel = allocate_temp_register();
   return GPRVector(sel, {0,1,2,3});
}

PValue ValuePool::create_register_from_nir_src(const nir_src& src, int comp)
{
   int idx = src.is_ssa ? get_dst_ssa_register_index(*src.ssa):
                          get_local_register_index(*src.reg.reg);

   auto retval = lookup_register(idx, comp, false);
   if (!retval || retval->type() != Value::gpr || retval->type() != Value::gpr_array_value)
      retval = create_register(idx, comp);
   return retval;
}

PValue ValuePool::from_nir(const nir_alu_dest &v, unsigned component)
{
   //assert(v->write_mask & (1 << component));
   return from_nir(v.dest, component);
}

int ValuePool::lookup_register_index(const nir_dest& dst)
{
   return dst.is_ssa ? get_dst_ssa_register_index(dst.ssa):
                       get_local_register_index(*dst.reg.reg);
}

int ValuePool::lookup_register_index(const nir_src& src) const
{
   int index = 0;

   index = src.is_ssa ?
              get_ssa_register_index(*src.ssa) :
              get_local_register_index(*src.reg.reg);

   sfn_log << SfnLog::reg << " LIDX:" << index;

   auto r = m_register_map.find(index);
   if (r == m_register_map.end()) {
      return -1;
   }
   return static_cast<int>(r->second.index);
}


int ValuePool::allocate_component(unsigned index, unsigned comp, bool pre_alloc)
{
   assert(comp < 8);
   return allocate_with_mask(index, 1 << comp, pre_alloc);
}

int ValuePool::allocate_temp_register()
{
   return m_next_register_index++;
}


PValue ValuePool::from_nir(const nir_dest& v, unsigned component)
{
   int idx = lookup_register_index(v);
   sfn_log << SfnLog::reg << __func__  << ": ";
   if (v.is_ssa)
      sfn_log << "ssa_" << v.ssa.index;
   else
      sfn_log << "r" << v.reg.reg->index;
   sfn_log << " -> " << idx << "\n";

   auto retval = lookup_register(idx, component, false);
   if (!retval)
      retval = create_register(idx, component);

   if (retval->type() == Value::gpr_vector) {
      assert(!v.is_ssa);
      auto& array = static_cast<GPRArray&>(*retval);
      retval = array.get_indirect(v.reg.base_offset,
                                  v.reg.indirect ?
                                  from_nir(*v.reg.indirect, 0, 0) : nullptr,
                                  component);
   }

   return retval;
}

ValueMap ValuePool::get_temp_registers() const
{
   ValueMap result;

   for (auto& v : m_registers) {
      if (v.second->type() == Value::gpr)
         result.insert(v.second);
      else if (v.second->type() == Value::gpr_vector) {
         auto& array = static_cast<GPRArray&>(*v.second);
         array.collect_registers(result);
      }
   }
   return result;
}

static const char swz[] = "xyzw01?_";

PValue ValuePool::create_register(unsigned sel, unsigned swizzle)
{
   sfn_log << SfnLog::reg
           <<"Create register " << sel  << '.' << swz[swizzle] << "\n";
   auto retval = PValue(new GPRValue(sel, swizzle));
   m_registers[(sel << 3) + swizzle] = retval;
   return retval;
}

bool ValuePool::inject_register(unsigned sel, unsigned swizzle,
                                const PValue& reg, bool map)
{
   uint32_t ssa_index = sel;

   if (map) {
      auto pos = m_ssa_register_map.find(sel);
      if (pos == m_ssa_register_map.end())
         ssa_index = m_next_register_index++;
      else
         ssa_index = pos->second;
   }

   sfn_log << SfnLog::reg
           << "Inject register " << sel  << '.' << swz[swizzle]
           << " at index " <<  ssa_index << " ...";

   if (map)
      m_ssa_register_map[sel] = ssa_index;

   allocate_with_mask(ssa_index, swizzle, true);

   unsigned idx = (ssa_index << 3) + swizzle;
   auto p = m_registers.find(idx);
   if ( (p != m_registers.end()) && *p->second != *reg) {
      std::cerr << "Register location (" << ssa_index << ", " << swizzle << ") was already reserved\n";
      assert(0);
      return false;
   }
   sfn_log << SfnLog::reg << " at idx:" << idx << " to " << *reg << "\n";
   m_registers[idx] = reg;

   if (m_next_register_index <= ssa_index)
      m_next_register_index = ssa_index + 1;
   return true;
}


PValue ValuePool::lookup_register(unsigned sel, unsigned swizzle,
                                  bool required)
{

   PValue retval;
   sfn_log << SfnLog::reg
           << "lookup register " << sel  << '.' << swz[swizzle] << "("
           << ((sel << 3) + swizzle) << ")...";


   auto reg = m_registers.find((sel << 3) + swizzle);
   if (reg != m_registers.end()) {
      sfn_log << SfnLog::reg << " -> Found " << *reg->second << "\n";
      retval = reg->second;
   } else if (swizzle == 7) {
      PValue retval = create_register(sel, swizzle);
      sfn_log << SfnLog::reg << " -> Created " << *retval << "\n";
   } else if (required) {
      sfn_log << SfnLog::reg << "Register (" << sel << ", "
              << swizzle << ") not found but required\n";
      assert(0 && "Unallocated register value requested\n");
   }
   sfn_log << SfnLog::reg << " -> Not required and not  allocated\n";
   return retval;
}

unsigned ValuePool::get_dst_ssa_register_index(const nir_ssa_def& ssa)
{
   sfn_log << SfnLog::reg << __func__ << ": search dst ssa "
           << ssa.index;

   auto pos = m_ssa_register_map.find(ssa.index);
   if (pos == m_ssa_register_map.end()) {
      sfn_log << SfnLog::reg << " Need to allocate ...";
      allocate_ssa_register(ssa);
      pos = m_ssa_register_map.find(ssa.index);
      assert(pos != m_ssa_register_map.end());
   }
   sfn_log << SfnLog::reg << "... got " << pos->second << "\n";
   return pos->second;
}

unsigned ValuePool::get_ssa_register_index(const nir_ssa_def& ssa) const
{
   sfn_log << SfnLog::reg << __func__ << ": search ssa "
           << ssa.index;

   auto pos = m_ssa_register_map.find(ssa.index);
   sfn_log << SfnLog::reg << " got " << pos->second<< "\n";
   if (pos == m_ssa_register_map.end()) {
      sfn_log << SfnLog::reg << __func__ << ": ssa register "
              << ssa.index << " lookup failed\n";
      return -1;
   }
   return pos->second;
}

unsigned ValuePool::get_local_register_index(const nir_register& reg)
{
   auto pos = m_local_register_map.find(reg.index);
   if (pos == m_local_register_map.end()) {
      allocate_local_register(reg);
      pos = m_local_register_map.find(reg.index);
      assert(pos != m_local_register_map.end());
   }
   return pos->second;
}

unsigned ValuePool::get_local_register_index(const nir_register& reg) const
{
   auto pos = m_local_register_map.find(reg.index);
   if (pos == m_local_register_map.end()) {
      sfn_log << SfnLog::err << __func__ << ": local register "
              << reg.index << " lookup failed";
      return -1;
   }
   return pos->second;
}

void ValuePool::allocate_ssa_register(const nir_ssa_def& ssa)
{
   sfn_log << SfnLog::reg << "ValuePool: Allocate ssa register " << ssa.index
           << " as " << m_next_register_index << "\n";
   int index = m_next_register_index++;
   m_ssa_register_map[ssa.index] = index;
   allocate_with_mask(index, 0xf, true);
}

void ValuePool::allocate_arrays(array_list& arrays)
{
   int ncomponents = 0;
   int current_index = m_next_register_index;
   unsigned instance = 0;

   while (!arrays.empty()) {
      auto a = arrays.top();
      arrays.pop();

      /* This is a bit hackish, return an id that encodes the array merge. To make sure
       * that the mapping doesn't go wrong we have to make sure the arrays is longer than
       * the number of instances in this arrays slot */
      if (a.ncomponents + ncomponents > 4 ||
          a.length < instance) {
         current_index = m_next_register_index;
         ncomponents = 0;
         instance = 0;
      }

      if (ncomponents == 0)
         m_next_register_index += a.length;

      uint32_t mask = ((1 << a.ncomponents) - 1) << ncomponents;

      PValue  array = PValue(new GPRArray(current_index, a.length, mask, ncomponents));

      sfn_log << SfnLog::reg << "Add array at "<< current_index
              << " of size " << a.length << " with " << a.ncomponents
              << " components, mask " << mask << "\n";

      m_local_register_map[a.index] = current_index + instance;

      for (unsigned  i = 0; i < a.ncomponents; ++i)
         m_registers[((current_index  + instance) << 3) + i] = array;

      VRec next_reg = {current_index + instance, mask, mask};
      m_register_map[current_index + instance] = next_reg;

      ncomponents += a.ncomponents;
      ++instance;
   }
}

void ValuePool::allocate_local_register(const nir_register& reg)
{
   int index = m_next_register_index++;
   m_local_register_map[reg.index] = index;
   allocate_with_mask(index, 0xf, true);

   /* Create actual register and map it */;
   for (int i = 0; i < 4; ++i) {
      int k = (index << 3) + i;
      m_registers[k] = PValue(new GPRValue(index, i));
   }
}

void ValuePool::allocate_local_register(const nir_register& reg, array_list& arrays)
{
   sfn_log << SfnLog::reg << "ValuePool: Allocate local register " << reg.index
           << " as " << m_next_register_index << "\n";

   if (reg.num_array_elems) {
      array_entry ae = {reg.index, reg.num_array_elems, reg.num_components};
      arrays.push(ae);
   }
   else
      allocate_local_register(reg);
}

bool ValuePool::create_undef(nir_ssa_undef_instr* instr)
{
   m_ssa_undef.insert(instr->def.index);
   return true;
}

int ValuePool::allocate_with_mask(unsigned index, unsigned mask, bool pre_alloc)
{
   int retval;
   VRec next_register = { index, mask };

   sfn_log << SfnLog::reg << (pre_alloc ? "Pre-alloc" : "Allocate")
           << " register (" << index << ", " << mask << ")\n";
   retval = index;
   auto r = m_register_map.find(index);

   if (r != m_register_map.end()) {
      if ((r->second.mask & next_register.mask) &&
          !(r->second.pre_alloc_mask & next_register.mask)) {
         std::cerr << "r600 ERR: register ("
                   << index << ", " << mask
                   << ") already allocated as (" << r->second.index << ", "
                   << r->second.mask << ", " << r->second.pre_alloc_mask
                   << ") \n";
         retval = -1;
      } else {
         r->second.mask |= next_register.mask;
         if (pre_alloc)
            r->second.pre_alloc_mask |= next_register.mask;
         retval = r->second.index;
      }
   } else  {
      if (pre_alloc)
         next_register.pre_alloc_mask = mask;
      m_register_map[index] = next_register;
      retval = next_register.index;
   }

   sfn_log << SfnLog::reg << "Allocate register (" << index << "," << mask << ") in R"
           << retval << "\n";

   return retval;
}

PValue ValuePool::literal(uint32_t value)
{
   auto l = m_literals.find(value);
   if (l != m_literals.end())
      return l->second;

   m_literals[value] = PValue(new LiteralValue(value));
   return m_literals[value];
}

}
