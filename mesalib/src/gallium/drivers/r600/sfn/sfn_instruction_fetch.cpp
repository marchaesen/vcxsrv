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

#include "sfn_instruction_fetch.h"

#include "gallium/drivers/r600/r600_pipe.h"

namespace r600 {

/* refactor this to add status create methods for specific tasks */
FetchInstruction::FetchInstruction(EVFetchInstr op,
                                   EVFetchType type,
                                   GPRVector dst,
                                   PValue src, int offset,
                                   int buffer_id, PValue buffer_offset,
                                   EBufferIndexMode cp_rel,
                                   bool use_const_field):
   Instruction(vtx),
   m_vc_opcode(op),
   m_fetch_type(type),
   m_endian_swap(vtx_es_none),
   m_src(src),
   m_dst(dst),
   m_offset(offset),
   m_is_mega_fetch(1),
   m_mega_fetch_count(16),
   m_buffer_id(buffer_id),
   m_semantic_id(0),
   m_buffer_index_mode(cp_rel),
   m_flags(0),
   m_uncached(false),
   m_indexed(false),
   m_array_base(0),
   m_array_size(0),
   m_elm_size(0),
   m_buffer_offset(buffer_offset),
   m_dest_swizzle({0,1,2,3})
{
   if (use_const_field) {
      m_flags.set(vtx_use_const_field);
      m_data_format = fmt_invalid;
      m_num_format = vtx_nf_norm;
   } else {
      m_flags.set(vtx_format_comp_signed);
      m_data_format = fmt_32_32_32_32_float;
      m_num_format = vtx_nf_scaled;
   }

   add_remappable_src_value(&m_src);
   add_remappable_src_value(&m_buffer_offset);

   add_remappable_dst_value(&m_dst);
}

/* Resource query */
FetchInstruction::FetchInstruction(EVFetchInstr vc_opcode,
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
                                   const std::array<int, 4>& dest_swizzle):
   Instruction(vtx),
   m_vc_opcode(vc_opcode),
   m_fetch_type(fetch_type),
   m_data_format(data_format),
   m_num_format(num_format),
   m_endian_swap(endian_swap),
   m_src(src),
   m_dst(dst),
   m_offset(offset),
   m_is_mega_fetch(is_mega_fetch),
   m_mega_fetch_count(mega_fetch_count),
   m_buffer_id(buffer_id),
   m_semantic_id(semantic_id),
   m_buffer_index_mode(buffer_index_mode),
   m_uncached(uncached),
   m_indexed(indexed),
   m_array_base(array_base),
   m_array_size(array_size),
   m_elm_size(elm_size),
   m_buffer_offset(buffer_offset),
   m_dest_swizzle(dest_swizzle)
{
   add_remappable_src_value(&m_src);
   add_remappable_dst_value(&m_dst);
   add_remappable_src_value(&m_buffer_offset);
}

FetchInstruction::FetchInstruction(GPRVector dst,
                                   PValue src,
                                   int buffer_id, PValue buffer_offset,
                                   EVTXDataFormat format,
                                   EVFetchNumFormat num_format):
   Instruction(vtx),
   m_vc_opcode(vc_fetch),
   m_fetch_type(no_index_offset),
   m_data_format(format),
   m_num_format(num_format),
   m_endian_swap(vtx_es_none),
   m_src(src),
   m_dst(dst),
   m_offset(0),
   m_is_mega_fetch(0),
   m_mega_fetch_count(0),
   m_buffer_id(buffer_id),
   m_semantic_id(0),
   m_buffer_index_mode(bim_none),
   m_flags(0),
   m_uncached(false),
   m_indexed(false),
   m_array_base(0),
   m_array_size(0),
   m_elm_size(1),
   m_buffer_offset(buffer_offset),
   m_dest_swizzle({0,1,2,3})
{
   m_flags.set(vtx_format_comp_signed);

   add_remappable_src_value(&m_src);
   add_remappable_dst_value(&m_dst);
   add_remappable_src_value(&m_buffer_offset);
}


/* Resource query */
FetchInstruction::FetchInstruction(GPRVector dst,
                                   PValue src,
                                   int buffer_id,
                                   EBufferIndexMode cp_rel):
   Instruction(vtx),
   m_vc_opcode(vc_get_buf_resinfo),
   m_fetch_type(no_index_offset),
   m_data_format(fmt_32_32_32_32),
   m_num_format(vtx_nf_norm),
   m_endian_swap(vtx_es_none),
   m_src(src),
   m_dst(dst),
   m_offset(0),
   m_is_mega_fetch(0),
   m_mega_fetch_count(16),
   m_buffer_id(buffer_id),
   m_semantic_id(0),
   m_buffer_index_mode(cp_rel),
   m_flags(0),
   m_uncached(false),
   m_indexed(false),
   m_array_base(0),
   m_array_size(0),
   m_elm_size(0),
   m_dest_swizzle({0,1,2,3})
{
   m_flags.set(vtx_format_comp_signed);
   add_remappable_src_value(&m_src);
   add_remappable_dst_value(&m_dst);
   add_remappable_src_value(&m_buffer_offset);
}

FetchInstruction::FetchInstruction(GPRVector dst, PValue src, int scratch_size):
   Instruction(vtx),
   m_vc_opcode(vc_read_scratch),
   m_fetch_type(vertex_data),
   m_data_format(fmt_32_32_32_32),
   m_num_format(vtx_nf_int),
   m_endian_swap(vtx_es_none),
   m_dst(dst),
   m_offset(0),
   m_is_mega_fetch(0),
   m_mega_fetch_count(16),
   m_buffer_id(0),
   m_semantic_id(0),
   m_buffer_index_mode(bim_none),
   m_flags(0),
   m_uncached(true),
   m_array_base(0),
   m_array_size(0),
   m_elm_size(3),
   m_dest_swizzle({0,1,2,3})
{
   if (src->type() == Value::literal) {
      const auto& lv = static_cast<const LiteralValue&>(*src);
      m_array_base = lv.value();
      m_indexed = false;
      m_src.reset(new GPRValue(0,0));
      m_array_size = 0;
   } else {
      m_array_base = 0;
      m_src = src;
      m_indexed = true;
      m_array_size = scratch_size - 1;
   }
   add_remappable_src_value(&m_src);
   add_remappable_dst_value(&m_dst);
   add_remappable_src_value(&m_buffer_offset);
}

void FetchInstruction::replace_values(const ValueSet& candiates, PValue new_value)
{
   if (!m_src)
      return;
   for (auto c: candiates) {
      for (int i = 0; i < 4; ++i) {
         if (*c == *m_dst.reg_i(i))
            m_dst.set_reg_i(i, new_value);
      }
      if (*m_src == *c)
         m_src = new_value;
   }
}


bool FetchInstruction::is_equal_to(const Instruction& lhs) const
{
   auto& l = static_cast<const FetchInstruction&>(lhs);
   if (m_src) {
      if (!l.m_src)
         return false;
      if (*m_src != *l.m_src)
         return false;
   } else {
      if (l.m_src)
         return false;
   }

   return m_vc_opcode == l.m_vc_opcode &&
         m_fetch_type == l.m_fetch_type &&
         m_data_format == l.m_data_format &&
         m_num_format == l.m_num_format &&
         m_endian_swap == l.m_endian_swap &&
         m_dst == l.m_dst &&
         m_offset == l.m_offset &&
         m_buffer_id == l.m_buffer_id &&
         m_semantic_id == l.m_semantic_id &&
         m_buffer_index_mode == l.m_buffer_index_mode &&
         m_flags == l.m_flags &&
         m_indexed == l.m_indexed &&
         m_uncached == l.m_uncached;
}

void FetchInstruction::set_format(EVTXDataFormat fmt)
{
   m_data_format = fmt;
}


void FetchInstruction::set_dest_swizzle(const std::array<int,4>& swz)
{
   m_dest_swizzle = swz;
}

void FetchInstruction::prelude_append(Instruction *instr)
{
   assert(instr);
   m_prelude.push_back(PInstruction(instr));
}

const std::vector<PInstruction>& FetchInstruction::prelude() const
{
   return m_prelude;
}

LoadFromScratch::LoadFromScratch(GPRVector dst, PValue src, int scratch_size):
   FetchInstruction(dst, src, scratch_size)
{
}

FetchGDSOpResult::FetchGDSOpResult(const GPRVector dst, const PValue src):
   FetchInstruction(vc_fetch,
                    no_index_offset,
                    fmt_32,
                    vtx_nf_int,
                    vtx_es_none,
                    src,
                    dst,
                    0,
                    false,
                    0xf,
                    R600_IMAGE_IMMED_RESOURCE_OFFSET,
                    0,
                    bim_none,
                    false,
                    false,
                    0,
                    0,
                    0,
                    PValue(),
                    {0,7,7,7})
{
   set_flag(vtx_srf_mode);
   set_flag(vtx_vpm);
}

FetchTCSIOParam::FetchTCSIOParam(GPRVector dst, PValue src, int offset):
   FetchInstruction(vc_fetch,
                    no_index_offset,
                    fmt_32_32_32_32,
                    vtx_nf_scaled,
                    vtx_es_none,
                    src,
                    dst,
                    offset,
                    false,
                    16,
                    R600_LDS_INFO_CONST_BUFFER,
                    0,
                    bim_none,
                    false,
                    false,
                    0,
                    0,
                    0,
                    PValue(),
                    {0,1,2,3})
{
   set_flag(vtx_srf_mode);
   set_flag(vtx_format_comp_signed);
}


static const char *fmt_descr[64] = {
   "INVALID",
   "8",
   "4_4",
   "3_3_2",
   "RESERVED_4",
   "16",
   "16F",
   "8_8",
   "5_6_5",
   "6_5_5",
   "1_5_5_5",
   "4_4_4_4",
   "5_5_5_1",
   "32",
   "32F",
   "16_16",
   "16_16F",
   "8_24",
   "8_24F",
   "24_8",
   "24_8F",
   "10_11_11",
   "10_11_11F",
   "11_11_10",
   "11_11_10F",
   "2_10_10_10",
   "8_8_8_8",
   "10_10_10_2",
   "X24_8_32F",
   "32_32",
   "32_32F",
   "16_16_16_16",
   "16_16_16_16F",
   "RESERVED_33",
   "32_32_32_32",
   "32_32_32_32F",
   "RESERVED_36",
   "1",
   "1_REVERSED",
   "GB_GR",
   "BG_RG",
   "32_AS_8",
   "32_AS_8_8",
   "5_9_9_9_SHAREDEXP",
   "8_8_8",
   "16_16_16",
   "16_16_16F",
   "32_32_32",
   "32_32_32F",
   "BC1",
   "BC2",
   "BC3",
   "BC4",
   "BC5",
   "APC0",
   "APC1",
   "APC2",
   "APC3",
   "APC4",
   "APC5",
   "APC6",
   "APC7",
   "CTX1",
   "RESERVED_63"
};


void FetchInstruction::do_print(std::ostream& os) const
{
   static const std::string num_format_char[] = {"norm", "int", "scaled"};
   static const std::string endian_swap_code[] = {
      "noswap", "8in16", "8in32"
   };
   static const char buffer_index_mode_char[] = "_01E";
   static const char *flag_string[] = {"WQM",  "CF", "signed", "no_zero",
                                       "nostride", "AC", "TC", "VPM"};
   switch (m_vc_opcode) {
   case vc_fetch:
      os << "Fetch " << m_dst;
      break;
   case vc_semantic:
      os << "Fetch Semantic ID:" << m_semantic_id;
      break;
   case vc_get_buf_resinfo:
      os << "Fetch BufResinfo:" << m_dst;
      break;
   case vc_read_scratch:
      os << "MEM_READ_SCRATCH:" << m_dst;
      break;
   default:
      os << "Fetch ERROR";
      return;
   }

   os << ", " << *m_src;

   if (m_offset)
      os << "+" << m_offset;

   os << " BUFID:" << m_buffer_id
      << " FMT:(" << fmt_descr[m_data_format]
      << " " << num_format_char[m_num_format]
      << " " << endian_swap_code[m_endian_swap]
      << ")";
   if (m_buffer_index_mode > 0)
      os << " IndexMode:" << buffer_index_mode_char[m_buffer_index_mode];


   if (m_is_mega_fetch)
      os << " MFC:" << m_mega_fetch_count;
   else
      os << " mfc*:" << m_mega_fetch_count;

   if (m_flags.any()) {
      os << " Flags:";
      for( int i = 0; i < vtx_unknwon; ++i) {
         if (m_flags.test(i))
            os << ' ' << flag_string[i];
      }
   }
}

}
