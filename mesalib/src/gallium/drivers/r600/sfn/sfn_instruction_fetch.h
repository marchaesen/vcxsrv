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

#ifndef SFN_INSTRUCTION_FETCH_H
#define SFN_INSTRUCTION_FETCH_H

#include "sfn_instruction_base.h"

namespace r600 {

class FetchInstruction : public Instruction {
public:

   FetchInstruction(EVFetchInstr vc_opcode,
                    EVFetchType fetch_type,
                    EVTXDataFormat data_format,
                    EVFetchNumFormat num_format,
                    EVFetchEndianSwap endian_swap,
                    const PValue src,
                    const GPRVector dst,
                    uint32_t offset,
                    bool is_mega_fetch,
                    uint32_t mega_fetch_count,
                    uint32_t buffer_id,
                    uint32_t semantic_id,

                    EBufferIndexMode buffer_index_mode,
                    bool uncached,
                    bool indexed,
                    int array_base,
                    int array_size,
                    int elm_size,
                    PValue buffer_offset,
                    const std::array<int, 4>& dest_swizzle);

   FetchInstruction(EVFetchInstr op,
                    EVFetchType type,
                    GPRVector dst,
                    PValue src, int offset,
                    int buffer_id, PValue buffer_offset,
                    EBufferIndexMode cp_rel,
                    bool use_const_field = false);

   FetchInstruction(GPRVector dst,
                    PValue src,
                    int buffer_id,
                    PValue buffer_offset,
                    EVTXDataFormat format,
                    EVFetchNumFormat num_format);

   FetchInstruction(GPRVector dst,
                    PValue src,
                    int buffer_id,
                    EBufferIndexMode cp_rel);

   FetchInstruction(GPRVector dst, PValue src, int scratch_size);

   void replace_values(const ValueSet& candidates, PValue new_value) override;
   EVFetchInstr vc_opcode() const { return m_vc_opcode;}
   EVFetchType fetch_type() const { return m_fetch_type;}

   EVTXDataFormat data_format() const { return m_data_format;}
   EVFetchNumFormat num_format() const { return m_num_format;}
   EVFetchEndianSwap endian_swap() const { return m_endian_swap;}

   const Value& src() const { return *m_src;}
   const GPRVector& dst() const { return m_dst;}
   uint32_t offset() const { return m_offset;}

   bool is_mega_fetchconst() { return m_is_mega_fetch;}
   uint32_t mega_fetch_count() const { return m_mega_fetch_count;}

   uint32_t buffer_id() const { return m_buffer_id;}
   uint32_t semantic_id() const { return m_semantic_id;}
   EBufferIndexMode buffer_index_mode() const{ return m_buffer_index_mode;}

   bool is_signed() const { return m_flags.test(vtx_format_comp_signed);}
   bool use_const_fields() const { return m_flags.test(vtx_use_const_field);}

   bool srf_mode_no_zero() const { return m_flags.test(vtx_srf_mode);}

   void set_flag(EVFetchFlagShift flag) {m_flags.set(flag);}

   bool uncached() const {return m_uncached; }
   bool indexed() const {return m_indexed; }
   int array_base()const {return m_array_base; }
   int array_size() const {return m_array_size; }
   int elm_size() const {return m_elm_size; }

   void set_buffer_offset(PValue buffer_offset) {
      m_buffer_offset = buffer_offset;
      add_remappable_src_value(&m_buffer_offset);
   }
   PValue buffer_offset() const { return m_buffer_offset; }

   void set_dest_swizzle(const std::array<int,4>& swz);
   void set_format(EVTXDataFormat fmt);

   int swz(int idx) const { return m_dest_swizzle[idx];}

   bool use_tc() const {return m_flags.test(vtx_use_tc);}

   bool use_vpm() const {return m_flags.test(vtx_vpm);}

   void prelude_append(Instruction *instr);

   const std::vector<PInstruction>& prelude() const;

   bool has_prelude() const {return !m_prelude.empty();}

private:
   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;

   EVFetchInstr m_vc_opcode;
   EVFetchType m_fetch_type;

   EVTXDataFormat m_data_format;
   EVFetchNumFormat m_num_format;
   EVFetchEndianSwap m_endian_swap;

   PValue m_src;
   GPRVector m_dst;
   uint32_t m_offset;

   bool m_is_mega_fetch;
   uint32_t m_mega_fetch_count;

   uint32_t m_buffer_id;
   uint32_t m_semantic_id;

   EBufferIndexMode m_buffer_index_mode;
   std::bitset<16> m_flags;
   bool m_uncached;
   bool m_indexed;
   int m_array_base;
   int m_array_size;
   int m_elm_size;
   PValue m_buffer_offset;
   std::array<int, 4> m_dest_swizzle;
   std::vector<PInstruction> m_prelude;
};

class LoadFromScratch: public FetchInstruction {
public:
   LoadFromScratch(GPRVector dst, PValue src, int scratch_size);
};

class FetchGDSOpResult : public FetchInstruction {
public:
   FetchGDSOpResult(const GPRVector dst, const PValue src);
};

class FetchTCSIOParam : public FetchInstruction {
public:
   FetchTCSIOParam(GPRVector dst, PValue src, int offset);
};

}

#endif // SFN_INSTRUCTION_FETCH_H
