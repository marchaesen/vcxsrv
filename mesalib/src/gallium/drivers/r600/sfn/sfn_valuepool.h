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


#ifndef SFN_VALUEPOOL_H
#define SFN_VALUEPOOL_H

#include "sfn_value.h"
#include "sfn_value_gpr.h"

#include <set>
#include <queue>

namespace r600 {

using LiteralBuffer = std::map<unsigned, const nir_load_const_instr *>;

class ValueMap {
public:
   void insert(const PValue& v) {
      auto idx = index_from(v->sel(), v->chan());
      m_map[idx] = v;
   }
   PValue get_or_inject(uint32_t index, uint32_t chan) {
      auto idx = index_from(index, chan);
      auto v = m_map.find(idx);
      if (v == m_map.end()) {
         insert(PValue(new GPRValue(index, chan)));
         v = m_map.find(idx);
      }
      return v->second;
   }
   std::map<uint32_t, PValue>::const_iterator begin() const {return m_map.begin();}
   std::map<uint32_t, PValue>::const_iterator end() const {return m_map.end();}

private:
   uint32_t index_from(uint32_t index, uint32_t chan) {
      return (index << 3) + chan;
   }
   std::map<uint32_t, PValue> m_map;
};

/** \brief Class to keep track of registers, uniforms, and literals
 * This class holds the references to the uniforms and the literals
 * and is responsible for allocating the registers.
 */
class ValuePool
{
public:

   struct  array_entry {
      unsigned index;
      unsigned length;
      unsigned ncomponents;

      bool operator ()(const array_entry& a, const array_entry& b) const {
         return a.length < b.length || (a.length == b.length && a.ncomponents > b.ncomponents);
      }
   };

   using array_list = std::priority_queue<array_entry, std::vector<array_entry>,
                                          array_entry>;

   ValuePool();


   GPRVector vec_from_nir(const nir_dest& dst, int num_components);

   std::vector<PValue> varvec_from_nir(const nir_dest& src, int num_components);
   std::vector<PValue> varvec_from_nir(const nir_src& src, int num_components);

   PValue from_nir(const nir_src& v, unsigned component, unsigned swizzled);

   PValue from_nir(const nir_src& v, unsigned component);
   /** Get a register that is used as source register in an ALU instruction
    * The PValue holds one componet as specified. If the register refers to
    * a GPR it must already have been allocated, uniforms and literals on
    * the other hand might be pre-loaded.
    */
   PValue from_nir(const nir_alu_src& v, unsigned component);

   /** Get a register that is used as source register in an Texture instruction
    * The PValue holds one componet as specified.
    */
   PValue from_nir(const nir_tex_src& v, unsigned component);

   /** Allocate a register that is used as destination register in an ALU
    * instruction. The PValue holds one componet as specified.
    */
   PValue from_nir(const nir_alu_dest& v, unsigned component);

   /** Allocate a register that is used as destination register in any
    * instruction. The PValue holds one componet as specified.
    */
   PValue from_nir(const nir_dest& v, unsigned component);

   /** Get the register index mapped from the NIR code to the r600 ir
    * \param index NIR index of register
    * \returns r600 ir inxex
    */
   int lookup_register_index(const nir_src& src) const;

   /** Get the register index mapped from the NIR code to the r600 ir
    * \param index NIR index of register
    * \returns r600 ir inxex
    */
   int lookup_register_index(const nir_dest& dst);

   /** Inject a register into a given ssa index position
    * This is used to redirect loads from system values and vertex attributes
    * that are already loaded into registers */
   bool inject_register(unsigned sel, unsigned swizzle, const PValue &reg, bool map);

   /** Reserve space for a local register */
   void allocate_local_register(const nir_register& reg);
   void allocate_local_register(const nir_register &reg, array_list& arrays);

   void allocate_arrays(array_list& arrays);


   void increment_reserved_registers() {
      ++m_next_register_index;
   }

   void set_reserved_registers(unsigned rr) {
      m_next_register_index =rr;
   }

   /** Allocate a register that is is needed for lowering an instruction
    * that requires complex calculations,
    */
   int allocate_temp_register();

   /** Reserve a undef register, currently it uses (0,7),
    * \todo should be eliminated in the final pass
    */
   bool create_undef(nir_ssa_undef_instr* instr);

   /** Create a new register with the given index and store it in the
    * lookup map
    */
   PValue create_register_from_nir_src(const nir_src& sel, int comp);

   ValueMap get_temp_registers() const;

   PValue lookup_register(unsigned sel, unsigned swizzle, bool required);

   size_t register_count() const {return m_next_register_index;}

   PValue create_register(unsigned index, unsigned swizzle);

   unsigned get_dst_ssa_register_index(const nir_ssa_def& ssa);

   PValue literal(uint32_t value);

   PGPRValue get_temp_register(int channel = -1);

   GPRVector get_temp_vec4();

private:

   unsigned get_ssa_register_index(const nir_ssa_def& ssa) const;

   unsigned get_local_register_index(const nir_register& reg);

   unsigned get_local_register_index(const nir_register& reg) const;

   void allocate_ssa_register(const nir_ssa_def& ssa);

   void allocate_array(const nir_register& reg);


   /** Allocate a register index with the given component mask.
    * If one of the components is already been allocated the function
    * will signal an error bz returning -1, otherwise a register index is
    * returned.
    */
   int allocate_with_mask(unsigned index, unsigned mask, bool pre_alloc);

   /** Allocate a register index with the given component.
    * If the component is already been allocated the function
    * will signal an error bz returning -1, otherwise a register index is
    * returned.
    */
   int allocate_component(unsigned index, unsigned comp, bool pre_alloc);

   /** search for a new register with the given index in the
    * lookup map.
    * \param sel register sel value
    * \param swizzle register component, can also be 4,5, and 7
    * \param required true: in debug mode assert when register doesn't exist
    *                 false: return nullptr on failure
    */

   std::set<unsigned> m_ssa_undef;

   std::map<unsigned, unsigned> m_local_register_map;
   std::map<unsigned, unsigned> m_ssa_register_map;

   std::map<unsigned, PValue> m_registers;

   static PValue m_undef;

   struct VRec {
      unsigned index;
      unsigned mask;
      unsigned pre_alloc_mask;
   };
   std::map<unsigned, VRec> m_register_map;

   unsigned m_next_register_index;

   std::map<unsigned, PGPRArray> m_arrays_map;

   std::map<uint32_t, PValue> m_literals;

   int current_temp_reg_index;
   int next_temp_reg_comp;
};

}

#endif // SFN_VALUEPOOL_H
