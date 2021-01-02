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

#ifndef EMITINSTRUCTION_H
#define EMITINSTRUCTION_H

#include "compiler/nir/nir.h"
#include "sfn_defines.h"
#include "sfn_value.h"
#include "sfn_instruction_alu.h"

namespace r600 {

class ShaderFromNirProcessor;

class EmitInstruction
{
public:
   EmitInstruction(ShaderFromNirProcessor& processor);
   virtual ~EmitInstruction();
   bool emit(nir_instr* instr);

   static const std::set<AluModifiers> empty;
   static const std::set<AluModifiers> write;
   static const std::set<AluModifiers> last_write;
   static const std::set<AluModifiers> last;

protected:
   virtual bool do_emit(nir_instr* instr) = 0;

   // forwards from ValuePool
   PValue from_nir(const nir_src& v, unsigned component, unsigned swizzled);
   PValue from_nir(const nir_src& v, unsigned component);
   PValue from_nir(const nir_alu_src& v, unsigned component);
   PValue from_nir(const nir_tex_src& v, unsigned component);
   PValue from_nir(const nir_alu_dest& v, unsigned component);
   PValue from_nir(const nir_dest& v, unsigned component);

   int lookup_register_index(const nir_src& src) const;
   int lookup_register_index(const nir_dest& dst);
   PValue create_register_from_nir_src(const nir_src& src, unsigned comp);

   int allocate_temp_register();

   PGPRValue get_temp_register(int channel = -1);
   GPRVector get_temp_vec4();

   // forwards from ShaderFromNirProcessor
   void emit_instruction(Instruction *ir);
   void emit_instruction(AluInstruction *ir);
   bool emit_instruction(EAluOp opcode, PValue dest,
                         std::vector<PValue> src0,
                         const std::set<AluModifiers>& m_flags);

   PValue from_nir_with_fetch_constant(const nir_src& src, unsigned component, int channel = -1);
   GPRVector vec_from_nir_with_fetch_constant(const nir_src& src, unsigned mask,
                                              const GPRVector::Swizzle& swizzle, bool match = false);

   const nir_variable *get_deref_location(const nir_src& v) const;

   enum chip_class get_chip_class(void) const;

   PValue literal(uint32_t value);

   GPRVector vec_from_nir(const nir_dest& dst, int num_components);

   bool inject_register(unsigned sel, unsigned swizzle,
                        const PValue& reg, bool map);

   int remap_atomic_base(int base);
private:

   ShaderFromNirProcessor& m_proc;
};

}



#endif // EMITINSTRUCTION_H
