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

#include "st_glsl_to_tgsi_private.h"
#include <tgsi/tgsi_info.h>
#include <mesa/program/prog_instruction.h>

static int swizzle_for_type(const glsl_type *type, int component = 0)
{
   unsigned num_elements = 4;

   if (type) {
      type = type->without_array();
      if (type->is_scalar() || type->is_vector() || type->is_matrix())
         num_elements = type->vector_elements;
   }

   int swizzle = swizzle_for_size(num_elements);
   assert(num_elements + component <= 4);

   swizzle += component * MAKE_SWIZZLE4(1, 1, 1, 1);
   return swizzle;
}

static st_src_reg *
dup_reladdr(const st_src_reg *input)
{
   if (!input)
      return NULL;

   st_src_reg *reg = ralloc(input, st_src_reg);
   if (!reg) {
      assert(!"can't create reladdr, expect shader breakage");
      return NULL;
   }

   *reg = *input;
   return reg;
}

st_src_reg::st_src_reg(gl_register_file file, int index, const glsl_type *type,
                       int component, unsigned array_id)
{
   assert(file != PROGRAM_ARRAY || array_id != 0);
   this->file = file;
   this->index = index;
   this->swizzle = swizzle_for_type(type, component);
   this->negate = 0;
   this->abs = 0;
   this->index2D = 0;
   this->type = type ? type->base_type : GLSL_TYPE_ERROR;
   this->reladdr = NULL;
   this->reladdr2 = NULL;
   this->has_index2 = false;
   this->double_reg2 = false;
   this->array_id = array_id;
   this->is_double_vertex_input = false;
}

st_src_reg::st_src_reg(gl_register_file file, int index, enum glsl_base_type type)
{
   assert(file != PROGRAM_ARRAY); /* need array_id > 0 */
   this->type = type;
   this->file = file;
   this->index = index;
   this->index2D = 0;
   this->swizzle = SWIZZLE_XYZW;
   this->negate = 0;
   this->abs = 0;
   this->reladdr = NULL;
   this->reladdr2 = NULL;
   this->has_index2 = false;
   this->double_reg2 = false;
   this->array_id = 0;
   this->is_double_vertex_input = false;
}

st_src_reg::st_src_reg(gl_register_file file, int index, enum glsl_base_type type, int index2D)
{
   assert(file != PROGRAM_ARRAY); /* need array_id > 0 */
   this->type = type;
   this->file = file;
   this->index = index;
   this->index2D = index2D;
   this->swizzle = SWIZZLE_XYZW;
   this->negate = 0;
   this->abs = 0;
   this->reladdr = NULL;
   this->reladdr2 = NULL;
   this->has_index2 = false;
   this->double_reg2 = false;
   this->array_id = 0;
   this->is_double_vertex_input = false;
}

st_src_reg::st_src_reg()
{
   this->type = GLSL_TYPE_ERROR;
   this->file = PROGRAM_UNDEFINED;
   this->index = 0;
   this->index2D = 0;
   this->swizzle = 0;
   this->negate = 0;
   this->abs = 0;
   this->reladdr = NULL;
   this->reladdr2 = NULL;
   this->has_index2 = false;
   this->double_reg2 = false;
   this->array_id = 0;
   this->is_double_vertex_input = false;
}

st_src_reg::st_src_reg(const st_src_reg &reg)
{
   *this = reg;
}

void st_src_reg::operator=(const st_src_reg &reg)
{
   this->type = reg.type;
   this->file = reg.file;
   this->index = reg.index;
   this->index2D = reg.index2D;
   this->swizzle = reg.swizzle;
   this->negate = reg.negate;
   this->abs = reg.abs;
   this->reladdr = dup_reladdr(reg.reladdr);
   this->reladdr2 = dup_reladdr(reg.reladdr2);
   this->has_index2 = reg.has_index2;
   this->double_reg2 = reg.double_reg2;
   this->array_id = reg.array_id;
   this->is_double_vertex_input = reg.is_double_vertex_input;
}

st_src_reg::st_src_reg(st_dst_reg reg)
{
   this->type = reg.type;
   this->file = reg.file;
   this->index = reg.index;
   this->swizzle = SWIZZLE_XYZW;
   this->negate = 0;
   this->abs = 0;
   this->reladdr = dup_reladdr(reg.reladdr);
   this->index2D = reg.index2D;
   this->reladdr2 = dup_reladdr(reg.reladdr2);
   this->has_index2 = reg.has_index2;
   this->double_reg2 = false;
   this->array_id = reg.array_id;
   this->is_double_vertex_input = false;
}

st_src_reg st_src_reg::get_abs()
{
   st_src_reg reg = *this;
   reg.negate = 0;
   reg.abs = 1;
   return reg;
}

st_dst_reg::st_dst_reg(st_src_reg reg)
{
   this->type = reg.type;
   this->file = reg.file;
   this->index = reg.index;
   this->writemask = WRITEMASK_XYZW;
   this->reladdr = dup_reladdr(reg.reladdr);
   this->index2D = reg.index2D;
   this->reladdr2 = dup_reladdr(reg.reladdr2);
   this->has_index2 = reg.has_index2;
   this->array_id = reg.array_id;
}

st_dst_reg::st_dst_reg(gl_register_file file, int writemask, enum glsl_base_type type, int index)
{
   assert(file != PROGRAM_ARRAY); /* need array_id > 0 */
   this->file = file;
   this->index = index;
   this->index2D = 0;
   this->writemask = writemask;
   this->reladdr = NULL;
   this->reladdr2 = NULL;
   this->has_index2 = false;
   this->type = type;
   this->array_id = 0;
}

st_dst_reg::st_dst_reg(gl_register_file file, int writemask, enum glsl_base_type type)
{
   assert(file != PROGRAM_ARRAY); /* need array_id > 0 */
   this->file = file;
   this->index = 0;
   this->index2D = 0;
   this->writemask = writemask;
   this->reladdr = NULL;
   this->reladdr2 = NULL;
   this->has_index2 = false;
   this->type = type;
   this->array_id = 0;
}

st_dst_reg::st_dst_reg()
{
   this->type = GLSL_TYPE_ERROR;
   this->file = PROGRAM_UNDEFINED;
   this->index = 0;
   this->index2D = 0;
   this->writemask = 0;
   this->reladdr = NULL;
   this->reladdr2 = NULL;
   this->has_index2 = false;
   this->array_id = 0;
}

st_dst_reg::st_dst_reg(const st_dst_reg &reg)
{
   *this = reg;
}

void st_dst_reg::operator=(const st_dst_reg &reg)
{
   this->type = reg.type;
   this->file = reg.file;
   this->index = reg.index;
   this->writemask = reg.writemask;
   this->reladdr = dup_reladdr(reg.reladdr);
   this->index2D = reg.index2D;
   this->reladdr2 = dup_reladdr(reg.reladdr2);
   this->has_index2 = reg.has_index2;
   this->array_id = reg.array_id;
}
