/*
 * Copyright 2019 Valve Corporation
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

#include "ac_shader_args.h"

#include "nir/nir_builder.h"

void ac_add_arg(struct ac_shader_args *info, enum ac_arg_regfile regfile, unsigned size,
                enum ac_arg_type type, struct ac_arg *arg)
{
   assert(info->arg_count < AC_MAX_ARGS);

   unsigned offset;
   if (regfile == AC_ARG_SGPR) {
      offset = info->num_sgprs_used;
      info->num_sgprs_used += size;
   } else {
      assert(regfile == AC_ARG_VGPR);
      offset = info->num_vgprs_used;
      info->num_vgprs_used += size;
   }

   info->args[info->arg_count].file = regfile;
   info->args[info->arg_count].offset = offset;
   info->args[info->arg_count].size = size;
   info->args[info->arg_count].type = type;

   if (arg) {
      arg->arg_index = info->arg_count;
      arg->used = true;
   }

   info->arg_count++;
}
