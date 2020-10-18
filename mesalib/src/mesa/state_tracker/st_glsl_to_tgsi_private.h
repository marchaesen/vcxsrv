/*
 * Copyright © 2010 Intel Corporation
 * Copyright © 2011 Bryan Cain
 * Copyright © 2017 Gert Wollny
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef ST_GLSL_TO_TGSI_PRIVATE_H
#define ST_GLSL_TO_TGSI_PRIVATE_H

#include "mesa/main/mtypes.h"
#include "program/prog_parameter.h"
#include "compiler/glsl_types.h"
#include "compiler/glsl/ir.h"
#include "tgsi/tgsi_info.h"
#include <ostream>

int swizzle_for_size(int size);

class st_dst_reg;
/**
 * This struct is a corresponding struct to TGSI ureg_src.
 */
class st_src_reg {
public:
   st_src_reg(gl_register_file file, int index, const glsl_type *type,
              int component = 0, unsigned array_id = 0);

   st_src_reg(gl_register_file file, int index, enum glsl_base_type type);

   st_src_reg(gl_register_file file, int index, enum glsl_base_type type, int index2D);

   st_src_reg();
   st_src_reg(const st_src_reg &reg);
   void operator=(const st_src_reg &reg);
   void reset();

   explicit st_src_reg(st_dst_reg reg);

   st_src_reg get_abs();

   int32_t index; /**< temporary index, VERT_ATTRIB_*, VARYING_SLOT_*, etc. */
   int16_t index2D;

   uint16_t swizzle; /**< SWIZZLE_XYZWONEZERO swizzles from Mesa. */
   int negate:4; /**< NEGATE_XYZW mask from mesa */
   unsigned abs:1;
   enum glsl_base_type type:6; /** GLSL_TYPE_* from GLSL IR (enum glsl_base_type) */
   unsigned has_index2:1;
   gl_register_file file:6; /**< PROGRAM_* from Mesa */
   /*
    * Is this the second half of a double register pair?
    * currently used for input mapping only.
    */
   unsigned double_reg2:1;
   unsigned is_double_vertex_input:1;
   unsigned array_id:10;
   /** Register index should be offset by the integer in this reg. */
   st_src_reg *reladdr;
   st_src_reg *reladdr2;

   bool is_legal_tgsi_address_operand() const
   {
      /* 2D registers can't be used as an address operand, or if the address
       * operand itself is a result of indirect addressing.
       */
      return (type == GLSL_TYPE_INT || type == GLSL_TYPE_UINT) &&
             !has_index2 && !reladdr && !reladdr2;
   }
};

bool operator == (const st_src_reg& lhs, const st_src_reg& rhs);

std::ostream& operator << (std::ostream& os, const st_src_reg& reg);

class st_dst_reg {
public:
   st_dst_reg(gl_register_file file, int writemask, enum glsl_base_type type, int index);

   st_dst_reg(gl_register_file file, int writemask, enum glsl_base_type type);

   st_dst_reg();
   st_dst_reg(const st_dst_reg &reg);
   void operator=(const st_dst_reg &reg);

   explicit st_dst_reg(st_src_reg reg);

   int32_t index; /**< temporary index, VERT_ATTRIB_*, VARYING_SLOT_*, etc. */
   int16_t index2D;
   gl_register_file file:6; /**< PROGRAM_* from Mesa */
   unsigned writemask:4; /**< Bitfield of WRITEMASK_[XYZW] */
   enum glsl_base_type type:6; /** GLSL_TYPE_* from GLSL IR (enum glsl_base_type) */
   unsigned has_index2:1;
   unsigned array_id:10;

   /** Register index should be offset by the integer in this reg. */
   st_src_reg *reladdr;
   st_src_reg *reladdr2;
};

bool operator == (const st_dst_reg& lhs, const st_dst_reg& rhs);

std::ostream& operator << (std::ostream& os, const st_dst_reg& reg);


class glsl_to_tgsi_instruction : public exec_node {
public:
   DECLARE_RALLOC_CXX_OPERATORS(glsl_to_tgsi_instruction)

   st_dst_reg dst[2];
   st_src_reg src[4];
   st_src_reg resource; /**< sampler or buffer register */
   st_src_reg *tex_offsets;

   /** Pointer to the ir source this tree came fe02549fdrom for debugging */
   ir_instruction *ir;

   enum tgsi_opcode op:10; /**< TGSI opcode */
   unsigned precise:1;
   unsigned saturate:1;
   unsigned is_64bit_expanded:1;
   unsigned sampler_base:5;
   unsigned sampler_array_size:6; /**< 1-based size of sampler array, 1 if not array */
   gl_texture_index tex_target:5;
   glsl_base_type tex_type:6;
   unsigned tex_shadow:1;
   enum pipe_format image_format:10;
   unsigned tex_offset_num_offset:3;
   unsigned dead_mask:4; /**< Used in dead code elimination */
   unsigned buffer_access:3; /**< bitmask of TGSI_MEMORY_x bits */
   unsigned read_only:1;
   unsigned gather_component:2; /* 0, 1, 2, 3 */

   const struct tgsi_opcode_info *info;

   void print(std::ostream& os) const;
};

inline std::ostream&
operator << (std::ostream& os, const glsl_to_tgsi_instruction& instr)
{
   instr.print(os);
   return os;
}

struct rename_reg_pair {
   bool valid;
   int new_reg;
};

inline static bool
is_resource_instruction(unsigned opcode)
{
   switch (opcode) {
   case TGSI_OPCODE_RESQ:
   case TGSI_OPCODE_LOAD:
   case TGSI_OPCODE_ATOMUADD:
   case TGSI_OPCODE_ATOMXCHG:
   case TGSI_OPCODE_ATOMCAS:
   case TGSI_OPCODE_ATOMAND:
   case TGSI_OPCODE_ATOMOR:
   case TGSI_OPCODE_ATOMXOR:
   case TGSI_OPCODE_ATOMUMIN:
   case TGSI_OPCODE_ATOMUMAX:
   case TGSI_OPCODE_ATOMIMIN:
   case TGSI_OPCODE_ATOMIMAX:
   case TGSI_OPCODE_ATOMFADD:
   case TGSI_OPCODE_ATOMINC_WRAP:
   case TGSI_OPCODE_ATOMDEC_WRAP:
   case TGSI_OPCODE_IMG2HND:
      return true;
   default:
      return false;
   }
}

inline static unsigned
num_inst_dst_regs(const glsl_to_tgsi_instruction *op)
{
   return op->info->num_dst;
}

inline static unsigned
num_inst_src_regs(const glsl_to_tgsi_instruction *op)
{
   return op->info->is_tex || is_resource_instruction(op->op) ?
      op->info->num_src - 1 : op->info->num_src;
}
#endif
