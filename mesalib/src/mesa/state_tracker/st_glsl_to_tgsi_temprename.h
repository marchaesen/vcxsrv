/*
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

#ifndef MESA_GLSL_TO_TGSI_TEMPRENAME_H
#define MESA_GLSL_TO_TGSI_TEMPRENAME_H

#include "st_glsl_to_tgsi_private.h"

/** Storage to record the required life time of a temporary register
 * begin == end == -1 indicates that the register can be reused without
 * limitations. Otherwise, "begin" indicates the first instruction in which
 * a write operation may target this temporary, and end indicates the
 * last instruction in which a value can be read from this temporary.
 * Hence, a register R2 can be merged with a register R1 if R1.end <= R2.begin.
 */
struct lifetime {
   int begin;
   int end;
};

/** Evaluates the required life times of temporary registers in a shader.
 * The life time estimation can only be run sucessfully if the shader doesn't
 * call a subroutine.
 * @param[in] mem_ctx a memory context that can be used with the ralloc_* functions
 * @param[in] instructions the shader to be anlzyed
 * @param[in] ntemps number of temporaries reserved for this shader
 * @param[in,out] lifetimes memory location to store the estimated required
 *   life times for each temporary register. The parameter must point to
 *   allocated memory that can hold ntemps lifetime structures. On output
 *   the life times contains the life times for the registers with the
 *   exception of TEMP[0].
 * @returns: true if the lifetimes were estimated, false if not (i.e. if a
 * subroutine was called).
 */
bool
get_temp_registers_required_lifetimes(void *mem_ctx, exec_list *instructions,
                                      int ntemps, struct lifetime *lifetimes);
/** Estimate the merge remapping of the registers.
 * @param[in] mem_ctx a memory context that can be used with the ralloc_* functions
 * @param[in] ntemps number of temporaries reserved for this shader
 * @param[in] lifetimes required life time for each temporary register.
 * @param[in,out] result memory location to store the register remapping table.
 *  On input the parameter must point to allocated memory that can hold the
 *  renaming information for ntemps registers, on output the mapping is stored.
 *  Note that TEMP[0] is not considered for register renaming.
 */
void get_temp_registers_remapping(void *mem_ctx, int ntemps,
                                  const struct lifetime* lifetimes,
                                  struct rename_reg_pair *result);

#endif