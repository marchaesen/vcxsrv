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

#ifndef SFN_INSTRUCTION_MISC_H
#define SFN_INSTRUCTION_MISC_H

#include "sfn_instruction_base.h"

namespace r600 {

class EmitVertex : public Instruction {
public:
   EmitVertex(int stream, bool cut);
   ECFOpCode op() const {return m_cut ? cf_cut_vertex: cf_emit_vertex;}
   int stream() const { return m_stream;}
private:

   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;
   int m_stream;
   bool m_cut;
};

class WaitAck : public Instruction {
public:
   WaitAck(int nack);
   ECFOpCode op() const {return cf_wait_ack;}
   int n_ack() const {return m_nack;}
private:

   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;
   int m_nack;
};

}

#endif // SFN_INSTRUCTION_MISC_H
