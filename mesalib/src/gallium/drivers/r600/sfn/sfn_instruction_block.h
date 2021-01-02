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


#ifndef sfn_instruction_block_h
#define sfn_instruction_block_h

#include "sfn_instruction_base.h"

namespace r600 {

class InstructionBlock : public Instruction
{
public:
	InstructionBlock(unsigned nesting_depth, unsigned block_number);

        void emit(PInstruction instr);


        std::vector<PInstruction>::const_iterator begin() const  {
           return m_block.begin();
        }
        std::vector<PInstruction>::const_iterator end() const {
           return m_block.end();
        }

        void remap_registers(ValueRemapper& map);

        size_t size() const {
           return m_block.size();
        }

        const PInstruction& operator [] (int i) const {
           return m_block[i];
        }

        unsigned number() const  {
           return m_block_number;
        }

        PInstruction last_instruction();

private:
        void do_evalue_liveness(LiverangeEvaluator& eval) const override;
        bool is_equal_to(const Instruction& lhs) const override;
        void do_print(std::ostream& os) const override;

        std::vector<PInstruction> m_block;

        unsigned m_block_number;
        unsigned m_nesting_depth;
};

}

#endif // INSTRUCTIONBLOCK_H
