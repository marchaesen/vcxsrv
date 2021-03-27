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

#ifndef SFN_EXPORTINSTRUCTION_H
#define SFN_EXPORTINSTRUCTION_H

#include "sfn_instruction_base.h"

namespace r600 {

class WriteoutInstruction: public Instruction {
public:
   void replace_values(const ValueSet& candidates, PValue new_value) override;
   const GPRVector&  gpr() const {return m_value;}
   const GPRVector  *gpr_ptr() const {return &m_value;}
protected:
   WriteoutInstruction(instr_type t, const GPRVector& value);
private:
   virtual void replace_values_child(const ValueSet& candidates, PValue new_value);
   virtual void remap_registers_child(std::vector<rename_reg_pair>& map,
                        ValueMap& values);

   GPRVector m_value;
};

class ExportInstruction : public WriteoutInstruction {
public:
   enum ExportType {
      et_pixel,
      et_pos,
      et_param
   };

   ExportInstruction(unsigned loc, const GPRVector& value, ExportType type);
   void set_last();

   ExportType export_type() const {return m_type;}

   unsigned location() const {return m_loc;}
   bool is_last_export() const {return m_is_last;}

   void update_output_map(OutputRegisterMap& map) const;

private:
   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;

   ExportType m_type;
   unsigned m_loc;
   bool m_is_last;
};

class WriteScratchInstruction : public WriteoutInstruction {
public:

   WriteScratchInstruction(unsigned loc, const GPRVector& value, int align,
                           int align_offset, int writemask);
   WriteScratchInstruction(const PValue& address, const GPRVector& value,
                           int align, int align_offset, int writemask, int array_size);
   unsigned location() const {return m_loc;}

   int write_mask() const { return m_writemask;}
   int address() const { assert(m_address); return m_address->sel();}
   bool indirect() const { return !!m_address;}
   int array_size() const { return m_array_size;}

private:
   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;

   void replace_values_child(const ValueSet& candidates, PValue new_value) override;
   void remap_registers_child(std::vector<rename_reg_pair>& map,
                              ValueMap& values)override;

   unsigned m_loc;
   PValue m_address;
   unsigned m_align;
   unsigned m_align_offset;
   unsigned m_writemask;
   int m_array_size;
};


class StreamOutIntruction: public WriteoutInstruction {
public:
   StreamOutIntruction(const GPRVector& value, int num_components,
                       int array_base, int comp_mask, int out_buffer,
                       int stream);
   int element_size() const { return m_element_size;}
   int burst_count() const { return m_burst_count;}
   int array_base() const { return m_array_base;}
   int array_size() const { return m_array_size;}
   int comp_mask() const { return m_writemask;}
   unsigned op() const;

private:
   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;

   int m_element_size;
   int m_burst_count;
   int m_array_base;
   int m_array_size;
   int m_writemask;
   int m_output_buffer;
   int m_stream;
};

enum EMemWriteType {
   mem_write = 0,
   mem_write_ind = 1,
   mem_write_ack = 2,
   mem_write_ind_ack = 3,
};

class MemRingOutIntruction: public WriteoutInstruction {
public:

   MemRingOutIntruction(ECFOpCode ring, EMemWriteType type,
                        const GPRVector& value, unsigned base_addr,
                        unsigned ncomp, PValue m_index);

   unsigned op() const{return m_ring_op;}
   unsigned ncomp() const;
   unsigned addr() const {return m_base_address;}
   EMemWriteType type() const {return m_type;}
   unsigned index_reg() const {return m_index->sel();}
   unsigned array_base() const {return m_base_address; }
   void replace_values_child(const ValueSet& candidates, PValue new_value) override;
   void remap_registers_child(std::vector<rename_reg_pair>& map,
                        ValueMap& values) override;
   void patch_ring(int stream, PValue index);
private:
   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;

   ECFOpCode m_ring_op;
   EMemWriteType m_type;
   unsigned m_base_address;
   unsigned m_num_comp;
   PValue m_index;

};

}


#endif // SFN_EXPORTINSTRUCTION_H