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

#include "sfn_instruction_misc.h"

namespace r600 {
EmitVertex::EmitVertex(int stream, bool cut):
   Instruction (emit_vtx),
   m_stream(stream),
   m_cut(cut)
{

}

bool EmitVertex::is_equal_to(const Instruction& lhs) const
{
   auto& oth = static_cast<const EmitVertex&>(lhs);
   return oth.m_stream == m_stream &&
         oth.m_cut == m_cut;
}

void EmitVertex::do_print(std::ostream& os) const
{
   os << (m_cut ? "EMIT_CUT_VERTEX @" : "EMIT_VERTEX @") << m_stream;
}

WaitAck::WaitAck(int nack):
   Instruction (wait_ack),
   m_nack(nack)
{

}

bool WaitAck::is_equal_to(const Instruction& lhs) const
{
   const auto& l = static_cast<const WaitAck&>(lhs);
   return m_nack == l.m_nack;
}

void WaitAck::do_print(std::ostream& os) const
{
   os << "WAIT_ACK @" << m_nack;
}

}
